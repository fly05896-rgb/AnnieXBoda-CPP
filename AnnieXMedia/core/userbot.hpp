// Authored By Certified Coders © 2026
// Converted from AnnieXMedia/core/userbot.py to C++20 TDLib Assistant Clients
#pragma once

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace AnnieXMedia::core {

namespace td_api = td::td_api;

extern std::vector<int> assistants;
extern std::vector<int64_t> assistantids;

class AssistantClient {
    int index_;
    std::string session_config_;
    std::unique_ptr<td::ClientManager> client_manager_;
    int32_t client_id_{0};
    std::atomic<uint64_t> query_id_{1};
    std::atomic<bool> running_{false};
    std::atomic<bool> authorized_{false};
    std::mutex mtx_;
    std::map<uint64_t, std::function<void(td_api::object_ptr<td_api::Object>)>> callbacks_;
    std::map<int64_t, int32_t> chat_group_call_ids_;
    std::thread worker_;

    void process_response(td::ClientManager::Response response);
    void on_auth_state(td_api::object_ptr<td_api::AuthorizationState> state);

public:
    int64_t id = 0;
    std::string name;
    std::string username;

    AssistantClient(int index, std::string session_config);
    ~AssistantClient();

    void send_query(td_api::object_ptr<td_api::Function> f,
                    std::function<void(td_api::object_ptr<td_api::Object>)> cb = {});
    td_api::object_ptr<td_api::Object> send_query_sync(td_api::object_ptr<td_api::Function> f, int timeout_sec = 10);

    bool start();
    void stop();
    bool is_active() const { return running_.load(); }

    // Group Call / VideoChat Signaling via TDLib for Native C++ NTgCalls
    int32_t get_or_create_group_call_id(int64_t chat_id, bool auto_create = true);
    std::string join_group_call_signaling(int64_t chat_id, const std::string& join_payload_json,
                                          int32_t audio_source_ssrc, bool is_muted, bool is_video_enabled);
    bool leave_group_call_signaling(int64_t chat_id, bool end_call = false);
    bool set_call_volume(int64_t chat_id, int volume_percent);
    std::vector<int64_t> get_call_participants(int64_t chat_id);
    bool join_chat_by_link_or_username(const std::string& link_or_username);
    bool leave_chat(int64_t chat_id);
    void send_message(int64_t chat_id, const std::string& text);
};

class Userbot {
public:
    std::shared_ptr<AssistantClient> one;
    std::shared_ptr<AssistantClient> two;
    std::shared_ptr<AssistantClient> three;
    std::shared_ptr<AssistantClient> four;
    std::shared_ptr<AssistantClient> five;

    Userbot();
    void start();
    void stop();
    std::shared_ptr<AssistantClient> get_by_index(int index) const;
};

extern Userbot userbot;

} // namespace AnnieXMedia::core
