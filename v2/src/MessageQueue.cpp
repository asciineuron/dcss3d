#include "MessageQueue.hpp"
#include <cstring>
#include <errno.h>
#include <filesystem>
#include <format>
#include <future>
#include <iostream>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace fs = std::filesystem;

void processMessages(handlerConfig& config, std::span<json> messages)
{
    // for each message loop listeners, async() their handleMessage() implementations if they respond to this message type
    std::vector<std::future<void>> results;

    for (const json& message : messages) {
        if (!config.contains(message["msg"]))
            continue;

        spdlog::debug("processing message: {}", message.dump());
        for (auto handler : config.at(message["msg"])) {
            results.push_back(std::async(&MessageHandler::handleMessage, handler, message));
        }
    }

    for (auto& result : results)
        result.wait();
}

NetworkManager::NetworkManager(std::string_view socketPath)
    : m_isPolling { false }
    , m_socketPath { socketPath }
{
    if ((m_sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        throw std::runtime_error(std::format("socket creation failure: {}", std::strerror(errno)));
    }

    struct sockaddr_un remote = { .sun_family = PF_LOCAL };
    strcpy(remote.sun_path, m_socketPath.c_str());
    spdlog::debug("socket path: '{}'", m_socketPath);

    if (connect(m_sockfd, (struct sockaddr*)&remote, sizeof(struct sockaddr_un)) == -1) {
        throw std::runtime_error(std::format("socket connect failure for socket path {}: {}", socketPath, std::strerror(errno)));
    }

    pipe(m_pipeCancel);

    m_pollfds[0] = { .fd = m_sockfd, .events = POLLIN };
    m_pollfds[1] = { .fd = m_pipeCancel[0], .events = POLLIN };

    m_pollThread = std::thread(&NetworkManager::pollLoop, this);
}

NetworkManager::~NetworkManager()
{
    m_isPolling = false;
    write(m_pipeCancel[1], ".", 1);
    m_pollThread.join();
    fs::remove(m_socketPath);
}

void NetworkManager::sendMessage(const json& message)
{
    std::string messageString = message.dump();
    uint32_t messageLength = static_cast<uint32_t>(messageString.length());
    spdlog::debug("sending message '{}'", messageString);

    if (send(m_sockfd, &messageLength, sizeof(messageLength), 0) != sizeof(messageLength))
        throw std::runtime_error(std::format("send failure: {}", std::strerror(errno)));

    int totSent = 0;
    int thisSend = 0;
    while (totSent < messageLength) {
        if ((thisSend = send(m_sockfd, (messageString.c_str() + totSent),
                 messageLength - totSent, 0))
            < 1) {
            // 0 for disconnect is also fatal
            throw std::runtime_error(std::format("send failure: {}", std::strerror(errno)));
        }
        totSent += thisSend;
    }
}

// convert input message into a list of json msg dicts
std::vector<json> parseResponseMessages(std::span<const char> response)
{
    // std::string asString(response.begin(), response.end());
    // std::cout << "response as string: " << asString << "\n";
    std::vector<json> messageList;
    json responseMessages = json::parse(response.begin(), response.end());
    // std::cout << "responseMessages: " << responseMessages << std::endl;
    if (responseMessages.contains("msgs")) {
        // message list
        for (auto& msg : responseMessages["msgs"].items()) {
            messageList.push_back(std::move(msg));
        }
    } else if (responseMessages.contains("msg")) {
        // single message
        messageList.push_back(std::move(responseMessages));
    }

    // spdlog::debug("parseResponseMessages() messageList:");
    // for (const auto& message : messageList) {
    //     spdlog::debug("{}", message.dump());
    // }
    return messageList;
}

// TODO: need some way of signaling error/exit when poll hangs up, exception? try reconnect?
void NetworkManager::pollLoop()
{
    if (m_isPolling)
        return;
    m_isPolling = true;
    while (m_isPolling) {
        int numEvents = poll(&m_pollfds[0], 2, -1);

        if (numEvents <= 0) {
            m_isPolling = false;
            throw std::runtime_error(std::format("poll failure: {}", std::strerror(errno)));
        }

        if (m_pollfds[1].revents & POLLIN) {
            spdlog::debug("received quit message from pipe");
            m_isPolling = false;
            break;
        }

        if (m_pollfds[0].revents & POLLHUP) {
            m_isPolling = false;
            throw std::runtime_error("socket hangup");
        }

        uint32_t responseLength;
        if (recv(m_sockfd, &responseLength, sizeof(responseLength), 0) != sizeof(responseLength)) {
            m_isPolling = false;
            throw std::runtime_error(std::format("recv failure: {}", std::strerror(errno)));
        }
        spdlog::debug("got response length: {}", responseLength);

        // need to clear old trailing message or potentially resize:
        m_responseBuffer.assign(responseLength, 0);

        uint32_t bytesRead = 0;
        uint32_t bytesLeft = responseLength;
        uint32_t currentBytes = 0;
        while (bytesLeft > 0) {
            if ((currentBytes = recv(m_sockfd, &m_responseBuffer[bytesRead], bytesLeft, 0)) == -1) {
                m_isPolling = false;
                throw std::runtime_error(std::format("recv failure: {}", std::strerror(errno)));
            }
            bytesRead += currentBytes;
            bytesLeft -= currentBytes;
            spdlog::debug("just read {} bytes of data", currentBytes);
        }
        // std::cerr << "DEBUG: buffer data: " << std::format("{}", m_responseBuffer) << "\n";

        if (m_responseBuffer.size() == 0)
            continue;

        std::vector<json> messages = parseResponseMessages(m_responseBuffer);

        {
            std::scoped_lock lock(m_backlogMutex);
            m_responseBacklog.insert(m_responseBacklog.end(),
                std::make_move_iterator(messages.begin()),
                std::make_move_iterator(messages.end()));
        }
        // std::cerr << "DEBUG: m_responseBacklog at end of pollLoop() iter: " << m_responseBacklog << std::endl;
    }
}

std::vector<json> NetworkManager::getNewMessages()
{
    std::vector<json> poppedMessages {};
    {
        std::scoped_lock lock(m_backlogMutex);
        m_responseBacklog.swap(poppedMessages);
    }
    return poppedMessages;
}
