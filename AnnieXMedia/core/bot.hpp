// Authored By Certified Coders © 2026
// Converted from AnnieXMedia/core/bot.py to C++20 using TDLib (td::ClientManager & td::td_api)
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

struct InlineButton {
    std::string text;
    std::string callback_data;
    std::string url;
    std::string switch_inline_query;
};

using InlineKeyboardMarkup = std::vector<std::vector<InlineButton>>;

struct IncomingMessage {
    int64_t id = 0;
    int64_t chat_id = 0;
    int64_t sender_user_id = 0;
    std::string sender_first_name;
    std::string sender_username;
    std::string chat_title;
    std::string chat_username;
    bool is_private = false;
    bool is_group = false;
    bool is_channel = false;
    std::string text;
    std::string caption;
    int64_t reply_to_message_id = 0;
    int64_t reply_sender_user_id = 0;
    std::string reply_sender_first_name;
    std::string audio_file_id;
    std::string video_file_id;
    std::string voice_file_id;
    std::string photo_file_id;
    std::string sticker_file_id;
    std::string document_file_id;
    int duration = 0;
    int64_t file_size = 0;
    bool is_forwarded = false;
    bool is_service_video_chat_started = false;
    bool is_service_video_chat_ended = false;
    std::vector<int64_t> new_chat_member_ids;

    std::string mention() const {
        return "<a href=\"tg://user?id=" + std::to_string(sender_user_id) + "\">" +
               (sender_first_name.empty() ? "User" : sender_first_name) + "</a>";
    }
};

struct IncomingCallbackQuery {
    int64_t id = 0;
    int64_t chat_id = 0;
    int64_t message_id = 0;
    int64_t sender_user_id = 0;
    std::string sender_first_name;
    std::string sender_username;
    std::string data;

    std::string mention() const {
        return "<a href=\"tg://user?id=" + std::to_string(sender_user_id) + "\">" +
               (sender_first_name.empty() ? "User" : sender_first_name) + "</a>";
    }
};

using MessageHandlerFn = std::function<bool(const IncomingMessage&)>;
using CallbackHandlerFn = std::function<bool(const IncomingCallbackQuery&)>;

class MusicBotClient {
    std::unique_ptr<td::ClientManager> client_manager_;
    std::int32_t client_id_{0};
    std::atomic<std::uint64_t> current_query_id_{1};
    std::atomic<bool> running_{false};
    std::atomic<bool> authorized_{false};

    std::mutex handlers_mutex_;
    std::map<std::uint64_t, std::function<void(td_api::object_ptr<td_api::Object>)>> query_callbacks_;
    std::vector<MessageHandlerFn> message_handlers_;
    std::vector<CallbackHandlerFn> callback_handlers_;
    std::thread receiver_thread_;

    void process_response(td::ClientManager::Response response);
    void process_update(td_api::object_ptr<td_api::Object> update);
    void on_authorization_state_update(td_api::object_ptr<td_api::AuthorizationState> auth_state);

    td_api::object_ptr<td_api::ReplyMarkup> build_reply_markup(const InlineKeyboardMarkup& keyboard) const;
    td_api::object_ptr<td_api::formattedText> parse_html_text(const std::string& html) const;

public:
    int64_t id = 0;
    std::string username;
    std::string name;
    std::string mention;

    MusicBotClient();
    ~MusicBotClient();

    void send_query(td_api::object_ptr<td_api::Function> f,
                    std::function<void(td_api::object_ptr<td_api::Object>)> handler = {});
    td_api::object_ptr<td_api::Object> send_query_sync(td_api::object_ptr<td_api::Function> f, int timeout_sec = 10);

    void start();
    void stop();
    void idle();

    void on_message(MessageHandlerFn fn);
    void on_callback_query(CallbackHandlerFn fn);

    // High-level Telegram Bot API equivalents on top of TDLib
    int64_t send_message(int64_t chat_id, const std::string& text,
                         const InlineKeyboardMarkup& reply_markup = {},
                         int64_t reply_to_message_id = 0,
                         bool disable_web_page_preview = true);

    int64_t send_photo(int64_t chat_id, const std::string& photo_path_or_url,
                       const std::string& caption = "",
                       const InlineKeyboardMarkup& reply_markup = {},
                       int64_t reply_to_message_id = 0);

    int64_t send_audio(int64_t chat_id, const std::string& audio_path,
                       const std::string& caption = "",
                       const std::string& title = "",
                       int duration = 0,
                       const InlineKeyboardMarkup& reply_markup = {},
                       int64_t reply_to_message_id = 0);

    int64_t send_video(int64_t chat_id, const std::string& video_path,
                       const std::string& caption = "",
                       int duration = 0,
                       const InlineKeyboardMarkup& reply_markup = {},
                       int64_t reply_to_message_id = 0);

    int64_t send_document(int64_t chat_id, const std::string& doc_path,
                          const std::string& caption = "",
                          int64_t reply_to_message_id = 0);

    int64_t send_sticker(int64_t chat_id, const std::string& sticker_file_id,
                         int64_t reply_to_message_id = 0);

    int64_t send_dice(int64_t chat_id, const std::string& emoji, int64_t reply_to_message_id = 0);

    void edit_message_text(int64_t chat_id, int64_t message_id, const std::string& text,
                           const InlineKeyboardMarkup& reply_markup = {});

    void edit_message_caption(int64_t chat_id, int64_t message_id, const std::string& caption,
                              const InlineKeyboardMarkup& reply_markup = {});

    void delete_message(int64_t chat_id, int64_t message_id);
    void delete_messages(int64_t chat_id, const std::vector<int64_t>& message_ids);

    void answer_callback_query(int64_t callback_query_id, const std::string& text = "", bool show_alert = false);

    bool is_admin(int64_t chat_id, int64_t user_id);
    std::vector<int64_t> get_chat_admins(int64_t chat_id);
    int32_t get_chat_member_count(int64_t chat_id);

    bool ban_chat_member(int64_t chat_id, int64_t user_id);
    bool unban_chat_member(int64_t chat_id, int64_t user_id);
    bool mute_chat_member(int64_t chat_id, int64_t user_id, int32_t until_date = 0);
    bool unmute_chat_member(int64_t chat_id, int64_t user_id);
    bool promote_chat_member(int64_t chat_id, int64_t user_id, bool full_promote = false);
    bool demote_chat_member(int64_t chat_id, int64_t user_id);

    bool pin_chat_message(int64_t chat_id, int64_t message_id, bool disable_notification = false);
    bool unpin_chat_message(int64_t chat_id, int64_t message_id = 0);
    bool set_chat_title(int64_t chat_id, const std::string& title);
    bool set_chat_description(int64_t chat_id, const std::string& description);
    bool set_chat_photo(int64_t chat_id, const std::string& photo_path);
    bool delete_chat_photo(int64_t chat_id);
    bool leave_chat(int64_t chat_id);

    std::string export_chat_invite_link(int64_t chat_id);
    std::string download_file(const std::string& remote_file_id);
};

extern MusicBotClient app;

} // namespace AnnieXMedia::core
