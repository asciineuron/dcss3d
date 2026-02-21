#pragma once

#include <atomic>
#include <deque>
#include <forward_list>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <poll.h>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

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

private:
    std::mutex m_backlogMutex;
    std::vector<json> m_responseBacklog;
    std::string m_socketPath;
    int m_sockfd;
    int m_pipeCancel[2];
    struct pollfd m_pollfds[2]; // from socket and pipe for cancel
    std::atomic<bool> m_isPolling;
    std::thread m_pollThread;
    std::vector<char> m_responseBuffer;

    void pollLoop();
};

// using handlerConfig = std::unordered_map<std::string, std::vector<MessageHandler*>>;
using handlerConfig = std::unordered_map<std::string, std::vector<std::reference_wrapper<MessageHandler>>>;
void processMessages(handlerConfig& config, std::span<json> messages);
