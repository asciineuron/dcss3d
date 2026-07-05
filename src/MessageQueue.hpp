#pragma once
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <poll.h>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

class GameTime;

class MessageHandler {
public:
    virtual void handleMessage(const json& message) = 0;
};

class NetworkManager {
public:
    NetworkManager(std::string_view socketPath);
    ~NetworkManager();

    NetworkManager(NetworkManager&) = delete;
    NetworkManager(NetworkManager&&) = delete;

    std::vector<json> getNewMessages();
    void sendMessage(const json& message);

    std::mutex messageMutex;
    // use via messageMutex:
    const auto& responseHistory() const { return m_responseHistory; };
    // mutex not needed since writes aren't in poll loop, but on main thread:
    const auto& sendHistory() const { return m_sendHistory; };
    // TODO: some way to view these unified/interleaved by timestamps?

    bool isConnected() const { return m_isConnected; }
    void reconnect();

private:
    std::atomic<bool> m_isConnected;
    std::vector<json> m_responseBacklog;
    std::string m_socketPath;
    int m_sockfd;
    int m_pipeCancel[2];
    struct pollfd m_pollfds[2]; // from socket and pipe for cancel
    std::atomic<bool> m_isPolling;
    std::thread m_pollThread;
    std::vector<char> m_responseBuffer;

    constexpr static const float s_connectTimeoutSec = 5.0f;

    constexpr static int s_messageHistorySize = 200;
    std::deque<std::string> m_responseHistory; // TODO: add e.g. timestamp info?
    std::deque<std::string> m_sendHistory;

    void pollLoop();

    // TODO: implement these
};

// using handlerConfig = std::unordered_map<std::string, std::vector<MessageHandler*>>;
using handlerConfig = std::unordered_map<std::string, std::vector<std::reference_wrapper<MessageHandler>>>;
void processMessages(handlerConfig& config, std::span<json> messages);
