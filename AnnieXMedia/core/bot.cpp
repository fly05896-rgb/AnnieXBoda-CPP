#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/logging.hpp"
#include "config.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <thread>
#include <unordered_map>

namespace AnnieXMedia::core {

MusicBotClient app;

static std::mutex g_cache_mtx;
static std::unordered_map<int64_t, std::pair<std::string, std::string>> g_user_cache;
static std::unordered_map<int64_t, std::pair<std::string, std::string>> g_chat_cache;
static std::unordered_map<int64_t, int64_t> g_msg_id_map;

static int64_t resolve_message_id(int64_t msg_id) {
    if (msg_id == 0) return 0;
    for (int i = 0; i < 12; ++i) {
        {
            std::lock_guard<std::mutex> lk(g_cache_mtx);
            auto it = g_msg_id_map.find(msg_id);
            if (it != g_msg_id_map.end()) return it->second;
        }
        if ((msg_id & 0xFFFFF) == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return msg_id;
}

static std::string fix_html_links(const std::string& html) {
    static const std::regex unquoted_href(R"(<a\s+href=([^"'\s>]+)>)");
    return std::regex_replace(html, unquoted_href, "<a href=\"$1\">");
}

static std::string ensure_local_image(const std::string& path_or_url) {
    if (path_or_url.empty()) return "";
    if (std::filesystem::exists(path_or_url) && std::filesystem::file_size(path_or_url) > 256) {
        return path_or_url;
    }
    if (path_or_url.rfind("http://", 0) == 0 || path_or_url.rfind("https://", 0) == 0) {
        std::filesystem::create_directories(config::DOWNLOAD_PATH + "/thumbs");
        std::string hash_name = std::to_string(std::hash<std::string>{}(path_or_url));
        std::string local_file = config::DOWNLOAD_PATH + "/thumbs/img_" + hash_name + ".jpg";
        if (std::filesystem::exists(local_file) && std::filesystem::file_size(local_file) > 512) {
            return local_file;
        }
        std::string cmd = "curl -4 -sL --max-time 8 -A \"Mozilla/5.0\" \"" + path_or_url + "\" -o \"" + local_file + "\"";
        (void)std::system(cmd.c_str());
        if (std::filesystem::exists(local_file) && std::filesystem::file_size(local_file) > 512) {
            return local_file;
        }
    }
    return "";
}

MusicBotClient::MusicBotClient() {
    td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(0));
    client_manager_ = std::make_unique<td::ClientManager>();
    client_id_ = client_manager_->create_client_id();
    LOGGER("AnnieXMedia.core.bot").info("Bot client initialized with native C++ TDLib engine.");
}

MusicBotClient::~MusicBotClient() {
    stop();
}

void MusicBotClient::send_query(td_api::object_ptr<td_api::Function> f,
                                std::function<void(td_api::object_ptr<td_api::Object>)> handler) {
    auto query_id = current_query_id_.fetch_add(1);
    if (handler) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        query_callbacks_.emplace(query_id, std::move(handler));
    }
    client_manager_->send(client_id_, query_id, std::move(f));
}

td_api::object_ptr<td_api::Object> MusicBotClient::send_query_sync(td_api::object_ptr<td_api::Function> f, int timeout_sec) {
    auto prom = std::make_shared<std::promise<td_api::object_ptr<td_api::Object>>>();
    auto fut = prom->get_future();
    send_query(std::move(f), [prom](td_api::object_ptr<td_api::Object> obj) {
        try { prom->set_value(std::move(obj)); } catch (...) {}
    });
    if (fut.wait_for(std::chrono::seconds(timeout_sec)) == std::future_status::ready) {
        return fut.get();
    }
    return nullptr;
}

void MusicBotClient::on_authorization_state_update(td_api::object_ptr<td_api::AuthorizationState> auth_state) {
    if (!auth_state) return;
    switch (auth_state->get_id()) {
        case td_api::authorizationStateWaitTdlibParameters::ID: {
            std::filesystem::create_directories("td_db/bot");
            auto req = td_api::make_object<td_api::setTdlibParameters>();
            req->database_directory_ = "td_db/bot";
            req->use_message_database_ = true;
            req->use_secret_chats_ = false;
            req->api_id_ = config::API_ID > 0 ? config::API_ID : 94575;
            req->api_hash_ = config::API_HASH.size() >= 20 ? config::API_HASH : "a3406de8d171bb422bb6ddf3bbd800e2";
            req->system_language_code_ = "ar";
            req->device_model_ = "AnnieXBoda C++ Server";
            req->system_version_ = "Linux x86_64";
            req->application_version_ = "3.0.0";
            send_query(std::move(req), [](td_api::object_ptr<td_api::Object> obj) {
                if (obj && obj->get_id() == td_api::error::ID) {
                    auto* err = static_cast<const td_api::error*>(obj.get());
                    LOGGER("AnnieXMedia.core.bot").error("❌ خطأ في setTdlibParameters: [" + std::to_string(err->code_) + "] " + err->message_);
                }
            });
            break;
        }
        case td_api::authorizationStateWaitPhoneNumber::ID: {
            if (config::BOT_TOKEN.empty() || config::BOT_TOKEN.find(':') == std::string::npos) {
                LOGGER("AnnieXMedia.core.bot").error("⚠️ قيمة BOT_TOKEN في السكرتس غير صحيحة!");
            } else {
                send_query(td_api::make_object<td_api::checkAuthenticationBotToken>(config::BOT_TOKEN),
                           [](td_api::object_ptr<td_api::Object> obj) {
                               if (obj && obj->get_id() == td_api::error::ID) {
                                   auto* err = static_cast<const td_api::error*>(obj.get());
                                   LOGGER("AnnieXMedia.core.bot").error("❌ خطأ في checkAuthenticationBotToken: [" + std::to_string(err->code_) + "] " + err->message_);
                               }
                           });
            }
            break;
        }
        case td_api::authorizationStateReady::ID: {
            authorized_.store(true);
            LOGGER("AnnieXMedia.core.bot").info("✅ TDLib Bot Authorization Ready.");
            break;
        }
        default:
            break;
    }
}

void MusicBotClient::process_update(td_api::object_ptr<td_api::Object> update) {
    if (!update) return;

    if (update->get_id() == td_api::updateAuthorizationState::ID) {
        auto u = td_api::move_object_as<td_api::updateAuthorizationState>(update);
        on_authorization_state_update(std::move(u->authorization_state_));
        return;
    }

    if (update->get_id() == td_api::updateUser::ID) {
        auto u = td_api::move_object_as<td_api::updateUser>(update);
        if (u->user_) {
            std::string full_name = u->user_->first_name_ + (u->user_->last_name_.empty() ? "" : (" " + u->user_->last_name_));
            std::string uname;
            if (u->user_->usernames_ && !u->user_->usernames_->active_usernames_.empty()) {
                uname = u->user_->usernames_->active_usernames_[0];
            }
            std::lock_guard<std::mutex> lk(g_cache_mtx);
            g_user_cache[u->user_->id_] = {full_name, uname};
        }
        return;
    }

    if (update->get_id() == td_api::updateNewChat::ID) {
        auto u = td_api::move_object_as<td_api::updateNewChat>(update);
        if (u->chat_) {
            std::lock_guard<std::mutex> lk(g_cache_mtx);
            g_chat_cache[u->chat_->id_].first = u->chat_->title_;
        }
        return;
    }

    if (update->get_id() == td_api::updateMessageSendSucceeded::ID) {
        auto u = td_api::move_object_as<td_api::updateMessageSendSucceeded>(update);
        if (u->message_) {
            std::lock_guard<std::mutex> lk(g_cache_mtx);
            g_msg_id_map[u->old_message_id_] = u->message_->id_;
        }
        return;
    }

    if (update->get_id() == td_api::updateNewMessage::ID) {
        auto u = td_api::move_object_as<td_api::updateNewMessage>(update);
        if (!u->message_ || u->message_->is_outgoing_) return;

        IncomingMessage msg;
        msg.id = u->message_->id_;
        msg.chat_id = u->message_->chat_id_;
        msg.is_private = (msg.chat_id > 0);
        msg.is_group = (msg.chat_id < 0);
        msg.is_forwarded = (u->message_->forward_info_ != nullptr);

        if (u->message_->sender_id_ && u->message_->sender_id_->get_id() == td_api::messageSenderUser::ID) {
            auto* sender = static_cast<const td_api::messageSenderUser*>(u->message_->sender_id_.get());
            msg.sender_user_id = sender->user_id_;
            {
                std::lock_guard<std::mutex> lk(g_cache_mtx);
                auto it = g_user_cache.find(msg.sender_user_id);
                if (it != g_user_cache.end()) {
                    msg.sender_first_name = it->second.first;
                    msg.sender_username = it->second.second;
                }
            }
        }
        {
            std::lock_guard<std::mutex> lk(g_cache_mtx);
            auto it = g_chat_cache.find(msg.chat_id);
            if (it != g_chat_cache.end()) {
                msg.chat_title = it->second.first;
                msg.chat_username = it->second.second;
            }
        }

        if (u->message_->reply_to_ && u->message_->reply_to_->get_id() == td_api::messageReplyToMessage::ID) {
            auto* rep = static_cast<const td_api::messageReplyToMessage*>(u->message_->reply_to_.get());
            msg.reply_to_message_id = rep->message_id_;
        }

        if (u->message_->content_) {
            switch (u->message_->content_->get_id()) {
                case td_api::messageText::ID: {
                    auto* c = static_cast<const td_api::messageText*>(u->message_->content_.get());
                    if (c->text_) msg.text = c->text_->text_;
                    break;
                }
                case td_api::messageAudio::ID: {
                    auto* c = static_cast<const td_api::messageAudio*>(u->message_->content_.get());
                    if (c->caption_) msg.caption = c->caption_->text_;
                    if (c->audio_ && c->audio_->audio_ && c->audio_->audio_->remote_) {
                        msg.audio_file_id = c->audio_->audio_->remote_->id_;
                        msg.duration = c->audio_->duration_;
                        msg.file_size = c->audio_->audio_->size_;
                    }
                    break;
                }
                case td_api::messageVideo::ID: {
                    auto* c = static_cast<const td_api::messageVideo*>(u->message_->content_.get());
                    if (c->caption_) msg.caption = c->caption_->text_;
                    if (c->video_ && c->video_->video_ && c->video_->video_->remote_) {
                        msg.video_file_id = c->video_->video_->remote_->id_;
                        msg.duration = c->video_->duration_;
                        msg.file_size = c->video_->video_->size_;
                    }
                    break;
                }
                case td_api::messageVoiceNote::ID: {
                    auto* c = static_cast<const td_api::messageVoiceNote*>(u->message_->content_.get());
                    if (c->caption_) msg.caption = c->caption_->text_;
                    if (c->voice_note_ && c->voice_note_->voice_ && c->voice_note_->voice_->remote_) {
                        msg.voice_file_id = c->voice_note_->voice_->remote_->id_;
                        msg.duration = c->voice_note_->duration_;
                    }
                    break;
                }
                case td_api::messagePhoto::ID: {
                    auto* c = static_cast<const td_api::messagePhoto*>(u->message_->content_.get());
                    if (c->caption_) msg.caption = c->caption_->text_;
                    if (c->photo_ && !c->photo_->sizes_.empty() && c->photo_->sizes_.back()->photo_->remote_) {
                        msg.photo_file_id = c->photo_->sizes_.back()->photo_->remote_->id_;
                    }
                    break;
                }
                case td_api::messageSticker::ID: {
                    auto* c = static_cast<const td_api::messageSticker*>(u->message_->content_.get());
                    if (c->sticker_ && c->sticker_->sticker_ && c->sticker_->sticker_->remote_) {
                        msg.sticker_file_id = c->sticker_->sticker_->remote_->id_;
                    }
                    break;
                }
                case td_api::messageVideoChatStarted::ID:
                    msg.is_service_video_chat_started = true;
                    break;
                case td_api::messageVideoChatEnded::ID:
                    msg.is_service_video_chat_ended = true;
                    break;
                case td_api::messageChatAddMembers::ID: {
                    auto* c = static_cast<const td_api::messageChatAddMembers*>(u->message_->content_.get());
                    msg.new_chat_member_ids = c->member_user_ids_;
                    break;
                }
                default:
                    break;
            }
        }

        std::vector<MessageHandlerFn> handlers;
        {
            std::lock_guard<std::mutex> lock(handlers_mutex_);
            handlers = message_handlers_;
        }
        std::thread([this, handlers, msg]() mutable {
            if (msg.sender_first_name.empty() && msg.sender_user_id != 0) {
                auto u_obj = this->send_query_sync(td_api::make_object<td_api::getUser>(msg.sender_user_id), 2);
                if (u_obj && u_obj->get_id() == td_api::user::ID) {
                    auto* usr = static_cast<const td_api::user*>(u_obj.get());
                    msg.sender_first_name = usr->first_name_ + (usr->last_name_.empty() ? "" : (" " + usr->last_name_));
                    if (usr->usernames_ && !usr->usernames_->active_usernames_.empty()) {
                        msg.sender_username = usr->usernames_->active_usernames_[0];
                    }
                }
            }
            if (msg.chat_title.empty()) {
                auto c_obj = this->send_query_sync(td_api::make_object<td_api::getChat>(msg.chat_id), 2);
                if (c_obj && c_obj->get_id() == td_api::chat::ID) {
                    msg.chat_title = static_cast<const td_api::chat*>(c_obj.get())->title_;
                }
            }
            for (const auto& h : handlers) {
                try {
                    if (h(msg)) break;
                } catch (const std::exception& e) {
                    LOGGER("AnnieXMedia.core.bot").error(std::string("Message handler exception: ") + e.what());
                }
            }
        }).detach();
        return;
    }

    if (update->get_id() == td_api::updateNewCallbackQuery::ID) {
        auto u = td_api::move_object_as<td_api::updateNewCallbackQuery>(update);
        IncomingCallbackQuery cb;
        cb.id = u->id_;
        cb.chat_id = u->chat_id_;
        cb.message_id = u->message_id_;
        cb.sender_user_id = u->sender_user_id_;
        {
            std::lock_guard<std::mutex> lk(g_cache_mtx);
            auto it = g_user_cache.find(cb.sender_user_id);
            if (it != g_user_cache.end()) {
                cb.sender_first_name = it->second.first;
                cb.sender_username = it->second.second;
            }
        }
        if (u->payload_ && u->payload_->get_id() == td_api::callbackQueryPayloadData::ID) {
            auto* p = static_cast<const td_api::callbackQueryPayloadData*>(u->payload_.get());
            cb.data = p->data_;
        }
        std::vector<CallbackHandlerFn> handlers;
        {
            std::lock_guard<std::mutex> lock(handlers_mutex_);
            handlers = callback_handlers_;
        }
        std::thread([handlers, cb]() {
            for (const auto& h : handlers) {
                try {
                    if (h(cb)) break;
                } catch (const std::exception& e) {
                    LOGGER("AnnieXMedia.core.bot").error(std::string("Callback handler exception: ") + e.what());
                }
            }
        }).detach();
    }
}

void MusicBotClient::process_response(td::ClientManager::Response response) {
    if (!response.object) return;
    if (response.request_id == 0) {
        process_update(std::move(response.object));
        return;
    }
    std::function<void(td_api::object_ptr<td_api::Object>)> cb;
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        auto it = query_callbacks_.find(response.request_id);
        if (it != query_callbacks_.end()) {
            cb = std::move(it->second);
            query_callbacks_.erase(it);
        }
    }
    if (cb) cb(std::move(response.object));
}

void MusicBotClient::start() {
    running_.store(true);
    send_query(td_api::make_object<td_api::getOption>("version"));

    receiver_thread_ = std::thread([this]() {
        while (running_.load()) {
            auto response = client_manager_->receive(1.0);
            if (response.object) {
                process_response(std::move(response));
            }
        }
    });

    for (int i = 0; i < 200 && !authorized_.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto me_obj = send_query_sync(td_api::make_object<td_api::getMe>(), 10);
    if (me_obj && me_obj->get_id() == td_api::user::ID) {
        auto me = td_api::move_object_as<td_api::user>(me_obj);
        this->id = me->id_;
        this->name = me->first_name_ + (me->last_name_.empty() ? "" : (" " + me->last_name_));
        if (me->usernames_ && !me->usernames_->active_usernames_.empty()) {
            this->username = me->usernames_->active_usernames_[0];
        } else {
            this->username = config::BOT_USERNAME;
        }
        this->mention = "<a href=\"tg://user?id=" + std::to_string(this->id) + "\">" + this->name + "</a>";
        config::BOT_USERNAME = this->username;
        config::BOT_NAME = this->name;
    } else {
        this->name = config::BOT_NAME;
        this->username = config::BOT_USERNAME;
    }

    if (config::LOGGER_ID != 0) {
        (void)send_query_sync(td_api::make_object<td_api::getChat>(config::LOGGER_ID), 3);
        send_message(
            config::LOGGER_ID,
            "<u><b>» " + this->mention + " ʙᴏᴛ sᴛᴀʀᴛᴇᴅ (C++ TDLib & Native NTgCalls) :</b></u>\n\n"
            "ɪᴅ : <code>" + std::to_string(this->id) + "</code>\n"
            "ɴᴀᴍᴇ : " + this->name + "\n"
            "ᴜsᴇʀɴᴀᴍᴇ : @" + this->username
        );
    }

    LOGGER("AnnieXMedia.core.bot").info("✅ Music Bot started as " + this->name + " (@" + this->username + ")");
}

void MusicBotClient::stop() {
    if (!running_.exchange(false)) return;
    send_query(td_api::make_object<td_api::close>());
    if (receiver_thread_.joinable()) receiver_thread_.join();
}

void MusicBotClient::idle() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void MusicBotClient::on_message(MessageHandlerFn fn) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    message_handlers_.push_back(std::move(fn));
}

void MusicBotClient::on_callback_query(CallbackHandlerFn fn) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    callback_handlers_.push_back(std::move(fn));
}

td_api::object_ptr<td_api::formattedText> MusicBotClient::parse_html_text(const std::string& html) const {
    std::string fixed_html = fix_html_links(html);
    auto res = td::ClientManager::execute(
        td_api::make_object<td_api::parseTextEntities>(fixed_html, td_api::make_object<td_api::textParseModeHTML>())
    );
    if (res && res->get_id() == td_api::formattedText::ID) {
        return td_api::move_object_as<td_api::formattedText>(res);
    }
    if (res && res->get_id() == td_api::error::ID) {
        auto* err = static_cast<const td_api::error*>(res.get());
        LOGGER("AnnieXMedia.core.bot").warning("⚠️ HTML Parse warning: " + err->message_ + " | input: " + fixed_html);
    }
    auto ft = td_api::make_object<td_api::formattedText>();
    ft->text_ = fixed_html;
    return ft;
}

td_api::object_ptr<td_api::ReplyMarkup> MusicBotClient::build_reply_markup(const InlineKeyboardMarkup& keyboard) const {
    if (keyboard.empty()) return nullptr;
    auto markup = td_api::make_object<td_api::replyMarkupInlineKeyboard>();
    for (const auto& row : keyboard) {
        std::vector<td_api::object_ptr<td_api::inlineKeyboardButton>> td_row;
        for (const auto& btn : row) {
            td_api::object_ptr<td_api::InlineKeyboardButtonType> type;
            if (!btn.url.empty()) {
                type = td_api::make_object<td_api::inlineKeyboardButtonTypeUrl>(btn.url);
            } else if (!btn.switch_inline_query.empty()) {
                type = td_api::make_object<td_api::inlineKeyboardButtonTypeSwitchInline>(btn.switch_inline_query, nullptr);
            } else {
                type = td_api::make_object<td_api::inlineKeyboardButtonTypeCallback>(btn.callback_data);
            }
            auto kb_btn = td_api::make_object<td_api::inlineKeyboardButton>();
            kb_btn->text_ = btn.text;
            kb_btn->type_ = std::move(type);
            td_row.push_back(std::move(kb_btn));
        }
        markup->rows_.push_back(std::move(td_row));
    }
    return markup;
}

int64_t MusicBotClient::send_message(int64_t chat_id, const std::string& text,
                                     const InlineKeyboardMarkup& reply_markup,
                                     int64_t reply_to_message_id,
                                     bool disable_web_page_preview) {
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    if (reply_to_message_id != 0) {
        auto rep = td_api::make_object<td_api::inputMessageReplyToMessage>();
        rep->message_id_ = resolve_message_id(reply_to_message_id);
        req->reply_to_ = std::move(rep);
    }
    req->reply_markup_ = build_reply_markup(reply_markup);

    auto content = td_api::make_object<td_api::inputMessageText>();
    content->text_ = parse_html_text(text);
    if (disable_web_page_preview) {
        auto lpo = td_api::make_object<td_api::linkPreviewOptions>();
        lpo->is_disabled_ = true;
        content->link_preview_options_ = std::move(lpo);
    }
    req->input_message_content_ = std::move(content);

    auto resp = send_query_sync(std::move(req), 8);
    if (resp && resp->get_id() == td_api::message::ID) {
        return static_cast<const td_api::message*>(resp.get())->id_;
    }
    if (resp && resp->get_id() == td_api::error::ID && chat_id != config::LOGGER_ID) {
        auto* err = static_cast<const td_api::error*>(resp.get());
        LOGGER("AnnieXMedia.core.bot").error("❌ send_message error in chat " + std::to_string(chat_id) + ": [" + std::to_string(err->code_) + "] " + err->message_);
    }
    return 0;
}

int64_t MusicBotClient::send_photo(int64_t chat_id, const std::string& photo_path_or_url,
                                   const std::string& caption,
                                   const InlineKeyboardMarkup& reply_markup,
                                   int64_t reply_to_message_id) {
    std::string local_img = ensure_local_image(photo_path_or_url);
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    if (reply_to_message_id != 0) {
        auto rep = td_api::make_object<td_api::inputMessageReplyToMessage>();
        rep->message_id_ = resolve_message_id(reply_to_message_id);
        req->reply_to_ = std::move(rep);
    }
    req->reply_markup_ = build_reply_markup(reply_markup);

    auto content = td_api::make_object<td_api::inputMessagePhoto>();
    auto ip = td_api::make_object<td_api::inputPhoto>();
    if (!local_img.empty()) {
        ip->photo_ = td_api::make_object<td_api::inputFileLocal>(local_img);
    } else if (std::filesystem::exists(photo_path_or_url)) {
        ip->photo_ = td_api::make_object<td_api::inputFileLocal>(photo_path_or_url);
    } else {
        ip->photo_ = td_api::make_object<td_api::inputFileRemote>(photo_path_or_url);
    }
    content->photo_ = std::move(ip);
    content->caption_ = parse_html_text(caption);
    req->input_message_content_ = std::move(content);

    auto resp = send_query_sync(std::move(req), 12);
    if (resp && resp->get_id() == td_api::message::ID) {
        return static_cast<const td_api::message*>(resp.get())->id_;
    }
    if (resp && resp->get_id() == td_api::error::ID) {
        auto* err = static_cast<const td_api::error*>(resp.get());
        LOGGER("AnnieXMedia.core.bot").warning("⚠️ send_photo fallback to text (reason: " + err->message_ + ")");
    }
    return send_message(chat_id, caption, reply_markup, reply_to_message_id);
}

int64_t MusicBotClient::send_audio(int64_t chat_id, const std::string& audio_path,
                                   const std::string& caption, const std::string& title,
                                   int duration, const InlineKeyboardMarkup& reply_markup,
                                   int64_t reply_to_message_id) {
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    if (reply_to_message_id != 0) {
        auto rep = td_api::make_object<td_api::inputMessageReplyToMessage>();
        rep->message_id_ = resolve_message_id(reply_to_message_id);
        req->reply_to_ = std::move(rep);
    }
    req->reply_markup_ = build_reply_markup(reply_markup);

    auto content = td_api::make_object<td_api::inputMessageAudio>();
    auto ia = td_api::make_object<td_api::inputAudio>();
    ia->audio_ = std::filesystem::exists(audio_path)
        ? td_api::object_ptr<td_api::InputFile>(td_api::make_object<td_api::inputFileLocal>(audio_path))
        : td_api::object_ptr<td_api::InputFile>(td_api::make_object<td_api::inputFileRemote>(audio_path));
    ia->duration_ = duration;
    ia->title_ = title;
    content->audio_ = std::move(ia);
    content->caption_ = parse_html_text(caption);
    req->input_message_content_ = std::move(content);

    auto resp = send_query_sync(std::move(req), 30);
    if (resp && resp->get_id() == td_api::message::ID) {
        return static_cast<const td_api::message*>(resp.get())->id_;
    }
    return 0;
}

int64_t MusicBotClient::send_video(int64_t chat_id, const std::string& video_path,
                                   const std::string& caption, int duration,
                                   const InlineKeyboardMarkup& reply_markup,
                                   int64_t reply_to_message_id) {
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    if (reply_to_message_id != 0) {
        auto rep = td_api::make_object<td_api::inputMessageReplyToMessage>();
        rep->message_id_ = resolve_message_id(reply_to_message_id);
        req->reply_to_ = std::move(rep);
    }
    req->reply_markup_ = build_reply_markup(reply_markup);

    auto content = td_api::make_object<td_api::inputMessageVideo>();
    auto iv = td_api::make_object<td_api::inputVideo>();
    iv->video_ = std::filesystem::exists(video_path)
        ? td_api::object_ptr<td_api::InputFile>(td_api::make_object<td_api::inputFileLocal>(video_path))
        : td_api::object_ptr<td_api::InputFile>(td_api::make_object<td_api::inputFileRemote>(video_path));
    iv->duration_ = duration;
    iv->supports_streaming_ = true;
    content->video_ = std::move(iv);
    content->caption_ = parse_html_text(caption);
    req->input_message_content_ = std::move(content);

    auto resp = send_query_sync(std::move(req), 30);
    if (resp && resp->get_id() == td_api::message::ID) {
        return static_cast<const td_api::message*>(resp.get())->id_;
    }
    return 0;
}

int64_t MusicBotClient::send_document(int64_t chat_id, const std::string& doc_path,
                                      const std::string& caption, int64_t reply_to_message_id) {
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    if (reply_to_message_id != 0) {
        auto rep = td_api::make_object<td_api::inputMessageReplyToMessage>();
        rep->message_id_ = resolve_message_id(reply_to_message_id);
        req->reply_to_ = std::move(rep);
    }
    auto content = td_api::make_object<td_api::inputMessageDocument>();
    auto idoc = td_api::make_object<td_api::inputDocument>();
    idoc->document_ = td_api::make_object<td_api::inputFileLocal>(doc_path);
    content->document_ = std::move(idoc);
    content->caption_ = parse_html_text(caption);
    req->input_message_content_ = std::move(content);
    auto resp = send_query_sync(std::move(req), 20);
    if (resp && resp->get_id() == td_api::message::ID) {
        return static_cast<const td_api::message*>(resp.get())->id_;
    }
    return 0;
}

int64_t MusicBotClient::send_sticker(int64_t chat_id, const std::string& sticker_file_id, int64_t reply_to_message_id) {
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    if (reply_to_message_id != 0) {
        auto rep = td_api::make_object<td_api::inputMessageReplyToMessage>();
        rep->message_id_ = resolve_message_id(reply_to_message_id);
        req->reply_to_ = std::move(rep);
    }
    auto content = td_api::make_object<td_api::inputMessageSticker>();
    auto ist = td_api::make_object<td_api::inputSticker>();
    ist->sticker_ = td_api::make_object<td_api::inputFileRemote>(sticker_file_id);
    content->sticker_ = std::move(ist);
    req->input_message_content_ = std::move(content);
    auto resp = send_query_sync(std::move(req), 8);
    if (resp && resp->get_id() == td_api::message::ID) {
        return static_cast<const td_api::message*>(resp.get())->id_;
    }
    return 0;
}

int64_t MusicBotClient::send_dice(int64_t chat_id, const std::string& emoji, int64_t reply_to_message_id) {
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    if (reply_to_message_id != 0) {
        auto rep = td_api::make_object<td_api::inputMessageReplyToMessage>();
        rep->message_id_ = resolve_message_id(reply_to_message_id);
        req->reply_to_ = std::move(rep);
    }
    auto content = td_api::make_object<td_api::inputMessageDice>(emoji, false);
    req->input_message_content_ = std::move(content);
    auto resp = send_query_sync(std::move(req), 8);
    if (resp && resp->get_id() == td_api::message::ID) {
        return static_cast<const td_api::message*>(resp.get())->id_;
    }
    return 0;
}

void MusicBotClient::edit_message_text(int64_t chat_id, int64_t message_id, const std::string& text,
                                       const InlineKeyboardMarkup& reply_markup) {
    int64_t real_id = resolve_message_id(message_id);
    auto req = td_api::make_object<td_api::editMessageText>();
    req->chat_id_ = chat_id;
    req->message_id_ = real_id;
    req->reply_markup_ = build_reply_markup(reply_markup);
    auto content = td_api::make_object<td_api::inputMessageText>();
    content->text_ = parse_html_text(text);
    req->input_message_content_ = std::move(content);
    send_query(std::move(req), [this, chat_id, text, reply_markup](td_api::object_ptr<td_api::Object> obj) {
        if (obj && obj->get_id() == td_api::error::ID) {
            this->send_message(chat_id, text, reply_markup);
        }
    });
}

void MusicBotClient::edit_message_caption(int64_t chat_id, int64_t message_id, const std::string& caption,
                                          const InlineKeyboardMarkup& reply_markup) {
    int64_t real_id = resolve_message_id(message_id);
    auto req = td_api::make_object<td_api::editMessageCaption>();
    req->chat_id_ = chat_id;
    req->message_id_ = real_id;
    req->reply_markup_ = build_reply_markup(reply_markup);
    req->caption_ = parse_html_text(caption);
    send_query(std::move(req));
}

void MusicBotClient::delete_message(int64_t chat_id, int64_t message_id) {
    int64_t real_id = resolve_message_id(message_id);
    delete_messages(chat_id, {real_id, message_id});
}

void MusicBotClient::delete_messages(int64_t chat_id, const std::vector<int64_t>& message_ids) {
    if (message_ids.empty()) return;
    auto copy_ids = message_ids;
    send_query(td_api::make_object<td_api::deleteMessages>(chat_id, std::move(copy_ids), true));
}

void MusicBotClient::answer_callback_query(int64_t callback_query_id, const std::string& text, bool show_alert) {
    send_query(td_api::make_object<td_api::answerCallbackQuery>(callback_query_id, text, show_alert, "", 0));
}

bool MusicBotClient::is_admin(int64_t chat_id, int64_t user_id) {
    if (user_id == config::OWNER_ID) return true;
    auto req = td_api::make_object<td_api::getChatMember>(
        chat_id, td_api::make_object<td_api::messageSenderUser>(user_id)
    );
    auto resp = send_query_sync(std::move(req), 5);
    if (resp && resp->get_id() == td_api::chatMember::ID) {
        auto* cm = static_cast<const td_api::chatMember*>(resp.get());
        if (cm->status_) {
            auto sid = cm->status_->get_id();
            return (sid == td_api::chatMemberStatusCreator::ID || sid == td_api::chatMemberStatusAdministrator::ID);
        }
    }
    return false;
}

std::vector<int64_t> MusicBotClient::get_chat_admins(int64_t chat_id) {
    std::vector<int64_t> admins;
    auto resp = send_query_sync(td_api::make_object<td_api::getChatAdministrators>(chat_id), 6);
    if (resp && resp->get_id() == td_api::chatAdministrators::ID) {
        auto* ca = static_cast<const td_api::chatAdministrators*>(resp.get());
        for (const auto& a : ca->administrators_) {
            if (a) admins.push_back(a->user_id_);
        }
    }
    return admins;
}

int32_t MusicBotClient::get_chat_member_count(int64_t chat_id) {
    auto resp = send_query_sync(td_api::make_object<td_api::getChat>(chat_id), 5);
    if (resp && resp->get_id() == td_api::chat::ID) {
        auto* c = static_cast<const td_api::chat*>(resp.get());
        if (c->type_ && c->type_->get_id() == td_api::chatTypeSupergroup::ID) {
            auto* sg = static_cast<const td_api::chatTypeSupergroup*>(c->type_.get());
            auto sg_resp = send_query_sync(td_api::make_object<td_api::getSupergroupFullInfo>(sg->supergroup_id_), 5);
            if (sg_resp && sg_resp->get_id() == td_api::supergroupFullInfo::ID) {
                return static_cast<const td_api::supergroupFullInfo*>(sg_resp.get())->member_count_;
            }
        }
    }
    return 0;
}

bool MusicBotClient::ban_chat_member(int64_t chat_id, int64_t user_id) {
    auto req = td_api::make_object<td_api::setChatMemberStatus>(
        chat_id,
        td_api::make_object<td_api::messageSenderUser>(user_id),
        td_api::make_object<td_api::chatMemberStatusBanned>(0)
    );
    auto r = send_query_sync(std::move(req), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::unban_chat_member(int64_t chat_id, int64_t user_id) {
    auto req = td_api::make_object<td_api::setChatMemberStatus>(
        chat_id,
        td_api::make_object<td_api::messageSenderUser>(user_id),
        td_api::make_object<td_api::chatMemberStatusLeft>()
    );
    auto r = send_query_sync(std::move(req), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::mute_chat_member(int64_t chat_id, int64_t user_id, int32_t until_date) {
    auto perms = td_api::make_object<td_api::chatPermissions>();
    auto status = td_api::make_object<td_api::chatMemberStatusRestricted>(true, until_date, std::move(perms));
    auto req = td_api::make_object<td_api::setChatMemberStatus>(
        chat_id,
        td_api::make_object<td_api::messageSenderUser>(user_id),
        std::move(status)
    );
    auto r = send_query_sync(std::move(req), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::unmute_chat_member(int64_t chat_id, int64_t user_id) {
    auto perms = td_api::make_object<td_api::chatPermissions>();
    perms->can_send_basic_messages_ = true;
    perms->can_send_audios_ = true;
    perms->can_send_documents_ = true;
    perms->can_send_photos_ = true;
    perms->can_send_videos_ = true;
    perms->can_send_video_notes_ = true;
    perms->can_send_voice_notes_ = true;
    perms->can_send_polls_ = true;
    perms->can_send_other_messages_ = true;
    perms->can_add_link_previews_ = true;
    auto status = td_api::make_object<td_api::chatMemberStatusRestricted>(true, 0, std::move(perms));
    auto req = td_api::make_object<td_api::setChatMemberStatus>(
        chat_id,
        td_api::make_object<td_api::messageSenderUser>(user_id),
        std::move(status)
    );
    auto r = send_query_sync(std::move(req), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::promote_chat_member(int64_t chat_id, int64_t user_id, bool full_promote) {
    auto rights = td_api::make_object<td_api::chatAdministratorRights>();
    rights->can_manage_chat_ = true;
    rights->can_change_info_ = full_promote;
    rights->can_delete_messages_ = true;
    rights->can_invite_users_ = true;
    rights->can_restrict_members_ = true;
    rights->can_pin_messages_ = true;
    rights->can_manage_topics_ = true;
    rights->can_promote_members_ = full_promote;
    rights->can_manage_video_chats_ = true;
    auto status = td_api::make_object<td_api::chatMemberStatusAdministrator>(true, std::move(rights));
    auto req = td_api::make_object<td_api::setChatMemberStatus>(
        chat_id,
        td_api::make_object<td_api::messageSenderUser>(user_id),
        std::move(status)
    );
    auto r = send_query_sync(std::move(req), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::demote_chat_member(int64_t chat_id, int64_t user_id) {
    auto req = td_api::make_object<td_api::setChatMemberStatus>(
        chat_id,
        td_api::make_object<td_api::messageSenderUser>(user_id),
        td_api::make_object<td_api::chatMemberStatusMember>(0)
    );
    auto r = send_query_sync(std::move(req), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::pin_chat_message(int64_t chat_id, int64_t message_id, bool disable_notification) {
    auto r = send_query_sync(td_api::make_object<td_api::pinChatMessage>(chat_id, message_id, disable_notification, false), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::unpin_chat_message(int64_t chat_id, int64_t message_id) {
    if (message_id == 0) {
        auto r = send_query_sync(td_api::make_object<td_api::unpinAllChatMessages>(chat_id), 5);
        return r && r->get_id() == td_api::ok::ID;
    }
    auto r = send_query_sync(td_api::make_object<td_api::unpinChatMessage>(chat_id, message_id), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::set_chat_title(int64_t chat_id, const std::string& title) {
    auto r = send_query_sync(td_api::make_object<td_api::setChatTitle>(chat_id, title), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::set_chat_description(int64_t chat_id, const std::string& description) {
    auto r = send_query_sync(td_api::make_object<td_api::setChatDescription>(chat_id, description), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::set_chat_photo(int64_t chat_id, const std::string& photo_path) {
    auto r = send_query_sync(td_api::make_object<td_api::setChatPhoto>(
        chat_id,
        td_api::make_object<td_api::inputChatPhotoStatic>(td_api::make_object<td_api::inputFileLocal>(photo_path))
    ), 10);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::delete_chat_photo(int64_t chat_id) {
    auto r = send_query_sync(td_api::make_object<td_api::setChatPhoto>(chat_id, nullptr), 5);
    return r && r->get_id() == td_api::ok::ID;
}

bool MusicBotClient::leave_chat(int64_t chat_id) {
    auto r = send_query_sync(td_api::make_object<td_api::leaveChat>(chat_id), 5);
    return r && r->get_id() == td_api::ok::ID;
}

std::string MusicBotClient::export_chat_invite_link(int64_t chat_id) {
    auto r = send_query_sync(td_api::make_object<td_api::createChatInviteLink>(chat_id, "", 0, 0, false), 5);
    if (r && r->get_id() == td_api::chatInviteLink::ID) {
        return static_cast<const td_api::chatInviteLink*>(r.get())->invite_link_;
    }
    return "";
}

std::string MusicBotClient::download_file(const std::string& remote_file_id) {
    auto rf = send_query_sync(td_api::make_object<td_api::getRemoteFile>(remote_file_id, nullptr), 5);
    if (rf && rf->get_id() == td_api::file::ID) {
        int32_t file_id = static_cast<const td_api::file*>(rf.get())->id_;
        auto dl = send_query_sync(td_api::make_object<td_api::downloadFile>(file_id, 32, 0, 0, true), 60);
        if (dl && dl->get_id() == td_api::file::ID) {
            auto* f = static_cast<const td_api::file*>(dl.get());
            if (f->local_) return f->local_->path_;
        }
    }
    return "";
}

} // namespace AnnieXMedia::core
