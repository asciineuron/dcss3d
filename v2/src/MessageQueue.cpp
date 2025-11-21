#include "MessageQueue.hpp"
#include <future>
#include <format>
#include <filesystem>
#include <functional>
#include <cstring>
#include <errno.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace fs = std::filesystem;

void GameResponseQueue::processMessages(std::span<json> messages)
{
    // for each message loop listeners, async() their handleMessage() implementations if they respond to this message type
    std::vector<std::future<void>> results;
    for (const json& message : messages) {
        if (!m_listeners.contains(message["msg"]))
            continue;
        for (auto handlerPtr : m_listeners.at(message["msg"])) {
            // if weak ptr still active:
            if (auto handler = handlerPtr.lock()) {
                results.push_back(std::async(&MessageHandler::handleMessage, handler.get(), message));
            }
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

    if (connect(m_sockfd, (struct sockaddr*)&remote, sizeof(struct sockaddr_un)) == -1) {
        throw std::runtime_error(std::format("socket connect failure: {}", std::strerror(errno)));
    }

    m_pollfd = { .fd = m_sockfd, .events = POLLIN };

    m_pollThread = std::thread(&NetworkManager::pollLoop, this);
}

NetworkManager::~NetworkManager()
{
    m_isPolling = false;
    m_pollThread.join();
    fs::remove(m_socketPath);
}

void NetworkManager::sendMessage(const json& message)
{
    std::string messageString = message.dump();
    uint32_t messageLength = static_cast<uint32_t>(messageString.length());

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
std::vector<json> parseResponseMessages(std::span<const uint8_t> response)
{
    std::vector<json> messageList;
    json responseMessages = json::parse(response.begin(), response.end());
    for (auto& msg : responseMessages["msgs"].items()) {
        messageList.push_back(std::move(msg));
    }
    return messageList;
}

// TODO: need some way of signaling error/exit when poll hangs up, exception? try reconnect?
void NetworkManager::pollLoop()
{
    if (m_isPolling)
        return;
    m_isPolling = true;
    while (m_isPolling) {
        int numEvents = poll(&m_pollfd, 1, -1);
        if (numEvents <= 0) {
            m_isPolling = false;
            throw std::runtime_error(std::format("poll failure: {}", std::strerror(errno)));
        }

        if (m_pollfd.revents & POLLHUP) {
            m_isPolling = false;
            throw std::runtime_error("socket hangup");
        }

        uint32_t responseLength;
        if (recv(m_sockfd, &responseLength, sizeof(responseLength), 0) != sizeof(responseLength)) {
            m_isPolling = false;
            throw std::runtime_error(std::format("recv failure: {}", std::strerror(errno)));
        }

        if (responseLength > m_responseBuffer.size()) {
            m_responseBuffer.reserve(responseLength); // don't need +1 for \0 since constructed to string
        }

        uint32_t bytesRead = 0;
        uint32_t bytesLeft = responseLength;
        uint32_t currentBytes = 0;
        while (bytesLeft > 0) {
            if ((currentBytes = recv(m_sockfd, m_responseBuffer.data() + bytesRead, bytesLeft, 0)) == -1) {
                m_isPolling = false;
                throw std::runtime_error(std::format("recv failure: {}", std::strerror(errno)));
            }
            bytesRead += currentBytes;
            bytesLeft -= currentBytes;
        }

        std::vector<json> messages = parseResponseMessages(m_responseBuffer);

        {
            std::scoped_lock lock(m_backlogMutex);
            m_responseBacklog.insert(m_responseBacklog.end(),
                std::make_move_iterator(messages.begin()),
                std::make_move_iterator(messages.end()));
        }
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
