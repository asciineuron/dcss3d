#include "MessageQueue.hpp"
#include "Turn.hpp"
#include <SDL3/SDL.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <errno.h>
#include <filesystem>
#include <format>
#include <future>
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
        if (errno == ECONNREFUSED) {
            // if socket not open yet, keep trying for up to s_connectTimeoutSec seconds
            uint64_t start = SDL_GetTicks();
            uint64_t now = start;
            bool hasConnected = false;
            // TODO 2/25 replace SDL tick time with proper std chrono implementation cast to seconds
            while (!hasConnected && now - start < 1000 * s_connectTimeoutSec) {
                // keep trying to connect. if fail for any reason other than connection refused, abort. break loop when successful and hasConnected
                if (connect(m_sockfd, (struct sockaddr*)&remote, sizeof(struct sockaddr_un)) == -1) {
                    if (errno != ECONNREFUSED)
                        throw std::runtime_error(std::format("socket connect failure for socket path {}: {}", socketPath, std::strerror(errno)));
                    else {
                        now = SDL_GetTicks();
                        continue;
                    }
                }
                hasConnected = true;
                break;
            }
            if (!hasConnected)
                throw std::runtime_error(std::format("socket still failed to connect"));
        }
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
    close(m_sockfd);
    fs::remove(m_socketPath);
}

void NetworkManager::sendMessage(const json& message)
{
    std::string messageString = message.dump();

    // save send history
    m_sendHistory.push_back(messageString);
    int excessSize = m_sendHistory.size() - s_messageHistorySize;
    for (int i = 0; i < excessSize; i++) {
        m_sendHistory.pop_front();
    }

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

        if (m_responseBuffer.size() == 0)
            continue;

        std::vector<json> messages = parseResponseMessages(m_responseBuffer);

        {
            std::scoped_lock lock(messageMutex);

            // add to history too
            for (const auto& message : messages) {
                m_responseHistory.emplace_back(std::move(message.dump()));
            }
            // drop old excess elements
            int excessSize = m_responseHistory.size() - s_messageHistorySize;
            for (int i = 0; i < excessSize; i++) {
                m_responseHistory.pop_front();
            }

            // move to backlog
            m_responseBacklog.insert(m_responseBacklog.end(),
                std::make_move_iterator(messages.begin()),
                std::make_move_iterator(messages.end()));

        }
    }
}

void NetworkManager::playGame()
{
    sendMessage(loginMessage("asciineuron", "password"));

    sendMessage(playMessage());

    chooseCharacter();
}

void NetworkManager::chooseCharacter(std::array<char, 3> speciesBackgroundWeapon)
{
    bool didSetSpecies = false;
    bool didSetBackground = false;
    bool didSetWeapon = false;

    while (!(didSetSpecies && didSetBackground && didSetWeapon)) {
        for (json message : getNewMessages()) {
            // TODO what happens if we get an incorrect message here?? e.g. out of order
            // for now let's assume that *never* happens once we've worked out bugs
            if (message["msg"] == "ui-push") {
                if (message["type"] == "species") {
                    sendMessage({ { "msg", "input" }, { "text", speciesBackgroundWeapon[0] } });
                    didSetSpecies = true;
                } else if (message["type"] == "background") {
                    sendMessage({ { "msg", "input" }, { "text", speciesBackgroundWeapon[1] } });
                    didSetBackground = true;
                } else if (message["type"] == "weapon") {
                    sendMessage({ { "msg", "input" }, { "text", speciesBackgroundWeapon[2] } });
                    didSetWeapon = true;
                }
            }
        }
    }
}

std::vector<json> NetworkManager::getNewMessages()
{
    std::vector<json> poppedMessages {};
    {
        std::scoped_lock lock(messageMutex);
        m_responseBacklog.swap(poppedMessages);
    }
    return poppedMessages;
}
