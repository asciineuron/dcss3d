#pragma once

#include <atomic>
#include <deque>
#include <forward_list>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
#include <span>
#include <string_view>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <poll.h>

using json = nlohmann::json;

class MessageHandler {
public:
    virtual void handleMessage(const json& message) = 0;
};

class NetworkManager {
public:
    NetworkManager(std::string_view socketPath = "./dcss3d.sock");
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
    struct pollfd m_pollfd;
    std::atomic<bool> m_isPolling;
    std::thread m_pollThread;
    std::vector<uint8_t> m_responseBuffer;

    void pollLoop();
};

class GameResponseQueue {
public:
    void addHandler(std::vector<std::string> messageTypes, std::shared_ptr<MessageHandler> handler);
    void processMessages(std::span<json> messages); // run through available messages

private:
    std::unordered_map<std::string, std::vector<std::weak_ptr<MessageHandler>>> m_listeners;
};
