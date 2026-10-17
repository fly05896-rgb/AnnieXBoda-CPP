#include "AnnieXMedia/core/userbot.hpp"
#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/logging.hpp"
#include "config.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <regex>

namespace AnnieXMedia::core {

std::vector<int> assistants;
std::vector<int64_t> assistantids;
Userbot userbot;

AssistantClient::AssistantClient(int index, std::string session_config)
    : index_(index), session_config_(std::move(session_config)) {
    td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(0));
    client_manager_ = std::make_unique<td::ClientManager>();
    client_id_ = client_manager_->create_client_id();
}

AssistantClient::~AssistantClient() {
    stop();
}

void AssistantClient::send_query(td_api::object_ptr<td_api::Function> f,
                                 std::function<void(td_api::object_ptr<td_api::Object>)> cb) {
    uint64_t qid = query_id_.fetch_add(1);
    if (cb) {
        std::lock_guard<std::mutex> lock(mtx_);
        callbacks_[qid] = std::move(cb);
    }
    client_manager_->send(client_id_, qid, std::move(f));
}

td_api::object_ptr<td_api::Object> AssistantClient::send_query_sync(td_api::object_ptr<td_api::Function> f, int timeout_sec) {
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

void AssistantClient::on_auth_state(td_api::object_ptr<td_api::AuthorizationState> state) {
    if (!state) return;
    switch (state->get_id()) {
        case td_api::authorizationStateWaitTdlibParameters::ID: {
            std::string db_dir = "td_db/assistant_" + std::to_string(index_);
            std::filesystem::create_directories(db_dir);
            auto req = td_api::make_object<td_api::setTdlibParameters>();
            req->database_directory_ = db_dir;
            req->use_message_database_ = true;
            req->use_secret_chats_ = false;
            req->api_id_ = config::API_ID > 0 ? config::API_ID : 94575;
            req->api_hash_ = config::API_HASH.size() >= 20 ? config::API_HASH : "a3406de8d171bb422bb6ddf3bbd800e2";
            req->system_language_code_ = "ar";
            req->device_model_ = "AnnieAssis" + std::to_string(index_);
            req->system_version_ = "Linux";
            req->application_version_ = "3.0.0";
            send_query(std::move(req), [this](td_api::object_ptr<td_api::Object> obj) {
                if (obj && obj->get_id() == td_api::error::ID) {
                    auto* err = static_cast<const td_api::error*>(obj.get());
                    LOGGER("AnnieXMedia.core.userbot").error("❌ خطأ في setTdlibParameters للمساعد " + std::to_string(index_) + ": " + err->message_);
                }
            });
            break;
        }
        case td_api::authorizationStateWaitPhoneNumber::ID: {
            std::string phone_env = config::getenv_str(("ASSISTANT_PHONE" + std::to_string(index_)).c_str(), "");
            if (phone_env.empty() && !session_config_.empty() && (session_config_[0] == '+' || std::isdigit(static_cast<unsigned char>(session_config_[0])))) {
                phone_env = session_config_;
            }
            if (!phone_env.empty()) {
                LOGGER("AnnieXMedia.core.userbot").info("📲 جارٍ إرسال رقم هاتف المساعد " + std::to_string(index_) + " (" + phone_env + ") لطلب كود التفعيل...");
                send_query(td_api::make_object<td_api::setAuthenticationPhoneNumber>(phone_env, nullptr),
                           [this](td_api::object_ptr<td_api::Object> obj) {
                               if (obj && obj->get_id() == td_api::error::ID) {
                                   auto* err = static_cast<const td_api::error*>(obj.get());
                                   LOGGER("AnnieXMedia.core.userbot").error("❌ فشل إرسال رقم المساعد " + std::to_string(index_) + ": " + err->message_);
                               }
                           });
            } else {
                LOGGER("AnnieXMedia.core.userbot").warning(
                    "📲 المساعد " + std::to_string(index_) + " بانتظار رقم الهاتف! أرسل في خاص البوت: رقم المساعد +201xxxxxxxxx"
                );
            }
            break;
        }
        case td_api::authorizationStateWaitCode::ID: {
            std::string code_env = config::getenv_str(("ASSISTANT_CODE" + std::to_string(index_)).c_str(), "");
            if (!code_env.empty()) {
                send_query(td_api::make_object<td_api::checkAuthenticationCode>(code_env));
            } else {
                LOGGER("AnnieXMedia.core.userbot").warning(
                    "📲 المساعد " + std::to_string(index_) + " بانتظار كود التفعيل! أرسل الكود الآن في جروب السجل أو في خاص البوت: كود المساعد 1 2 3 4 5"
                );
                app.send_message(
                    config::OWNER_ID,
                    "📲 <b>المساعد " + std::to_string(index_) + " بانتظار كود التفعيل!</b>\n"
                    "أرسل الكود الآن هنا في الشات مفصولاً بمسافات هكذا:\n"
                    "<code>كود المساعد 1 2 3 4 5</code>"
                );
            }
            break;
        }
        case td_api::authorizationStateWaitPassword::ID: {
            std::string pass_env = config::getenv_str(("ASSISTANT_PASSWORD" + std::to_string(index_)).c_str(), "");
            if (!pass_env.empty()) {
                send_query(td_api::make_object<td_api::checkAuthenticationPassword>(pass_env));
            } else {
                LOGGER("AnnieXMedia.core.userbot").warning(
                    "🔐 المساعد " + std::to_string(index_) + " محمي بكلمة مرور التحقق بخطوتين (2FA)! بانتظار إدخال الباسورد في شات البوت..."
                );
                app.send_message(
                    config::OWNER_ID,
                    "🔐 <b>حساب المساعد " + std::to_string(index_) + " محمي بكلمة مرور التحقق بخطوتين (2FA)!</b>\n"
                    "أرسل كلمة المرور الآن هنا في الشات هكذا:\n"
                    "<code>باسورد المساعد كلمه_السر</code>"
                );
            }
            break;
        }
        case td_api::authorizationStateReady::ID: {
            bool was_ready = authorized_.exchange(true);
            if (!was_ready) {
                std::thread([this]() {
                    auto me_obj = this->send_query_sync(td_api::make_object<td_api::getMe>(), 8);
                    if (me_obj && me_obj->get_id() == td_api::user::ID) {
                        auto me = td_api::move_object_as<td_api::user>(me_obj);
                        this->id = me->id_;
                        this->name = me->first_name_ + (me->last_name_.empty() ? "" : (" " + me->last_name_));
                        if (me->usernames_ && !me->usernames_->active_usernames_.empty()) {
                            this->username = me->usernames_->active_usernames_[0];
                        }
                        if (std::find(assistants.begin(), assistants.end(), index_) == assistants.end()) {
                            assistants.push_back(index_);
                        }
                        if (std::find(assistantids.begin(), assistantids.end(), this->id) == assistantids.end()) {
                            assistantids.push_back(this->id);
                        }
                        LOGGER("AnnieXMedia.core.userbot").info(
                            "☔ تم تشغيل المساعد " + std::to_string(index_) + " بنجاح بهوية: " +
                            this->name + " (@" + this->username + ") [ID: " + std::to_string(this->id) + "]"
                        );

                        this->send_query_sync(td_api::make_object<td_api::getChats>(nullptr, 100), 6);
                        std::string start_text = "☔ تم بدء تشغيل الحساب المساعد " + std::to_string(index_) +
                                                 " بنجاح بهوية: " + this->name + " (@" + this->username + ")";
                        if (config::LOGGER_ID != 0) {
                            this->send_message(config::LOGGER_ID, start_text);
                        }
                        if (config::OWNER_ID != 0) {
                            this->send_message(config::OWNER_ID, start_text);
                            app.send_message(
                                config::OWNER_ID,
                                "✅ <b>تم تشغيل الحساب المساعد " + std::to_string(index_) + " بنجاح!</b>\n"
                                "• الاسم: " + this->name + "\n"
                                "• اليوزر: @" + this->username + "\n"
                                "• الآيدي: <code>" + std::to_string(this->id) + "</code>"
                            );
                        }
                    }
                }).detach();
            }
            break;
        }
        default:
            break;
    }
}

void AssistantClient::process_response(td::ClientManager::Response response) {
    if (!response.object) return;
    if (response.request_id == 0) {
        if (response.object->get_id() == td_api::updateAuthorizationState::ID) {
            auto u = td_api::move_object_as<td_api::updateAuthorizationState>(response.object);
            on_auth_state(std::move(u->authorization_state_));
        } else if (response.object->get_id() == td_api::updateChatVideoChat::ID) {
            auto u = td_api::move_object_as<td_api::updateChatVideoChat>(response.object);
            if (u->video_chat_) {
                std::lock_guard<std::mutex> lock(mtx_);
                chat_group_call_ids_[u->chat_id_] = u->video_chat_->group_call_id_;
            }
        }
        return;
    }
    std::function<void(td_api::object_ptr<td_api::Object>)> cb;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = callbacks_.find(response.request_id);
        if (it != callbacks_.end()) {
            cb = std::move(it->second);
            callbacks_.erase(it);
        }
    }
    if (cb) cb(std::move(response.object));
}

bool AssistantClient::start() {
    if (session_config_.empty()) return false;
    running_.store(true);
    send_query(td_api::make_object<td_api::getOption>("version"));

    worker_ = std::thread([this]() {
        while (running_.load()) {
            auto resp = client_manager_->receive(1.0);
            if (resp.object) process_response(std::move(resp));
        }
    });

    for (int i = 0; i < 50 && !authorized_.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!authorized_.load()) {
        assistants.push_back(index_);
        LOGGER("AnnieXMedia.core.userbot").info(
            "⏳ المساعد " + std::to_string(index_) + " ينتظر إدخال كود التفعيل من تيليجرام (أرسل الكود في شات البوت أو جروب السجل بدون عمل Deploy)..."
        );
    }
    return true;
}

void AssistantClient::stop() {
    if (!running_.exchange(false)) return;
    send_query(td_api::make_object<td_api::close>());
    if (worker_.joinable()) worker_.join();
}

int32_t AssistantClient::get_or_create_group_call_id(int64_t chat_id, bool auto_create) {
    auto chat_obj = send_query_sync(td_api::make_object<td_api::getChat>(chat_id), 6);
    if (!chat_obj || chat_obj->get_id() == td_api::error::ID) {
        send_query_sync(td_api::make_object<td_api::getChats>(nullptr, 100), 5);
        chat_obj = send_query_sync(td_api::make_object<td_api::getChat>(chat_id), 6);
    }
    if (!chat_obj || chat_obj->get_id() != td_api::chat::ID) {
        std::string inv = app.export_chat_invite_link(chat_id);
        if (!inv.empty()) {
            LOGGER("AnnieXMedia.core.userbot").info("🔗 Assistant joining chat " + std::to_string(chat_id) + " via invite link...");
            join_chat_by_link_or_username(inv);
            chat_obj = send_query_sync(td_api::make_object<td_api::getChat>(chat_id), 6);
        }
    }
    if (chat_obj && chat_obj->get_id() == td_api::chat::ID) {
        auto* c = static_cast<const td_api::chat*>(chat_obj.get());
        if (c->video_chat_ && c->video_chat_->group_call_id_ != 0) {
            std::lock_guard<std::mutex> lock(mtx_);
            chat_group_call_ids_[chat_id] = c->video_chat_->group_call_id_;
            return c->video_chat_->group_call_id_;
        }
    } else if (chat_obj && chat_obj->get_id() == td_api::error::ID) {
        auto* err = static_cast<const td_api::error*>(chat_obj.get());
        LOGGER("AnnieXMedia.core.userbot").error("❌ Assistant getChat(" + std::to_string(chat_id) + ") failed: " + err->message_);
    }

    if (auto_create) {
        auto created = send_query_sync(td_api::make_object<td_api::createVideoChat>(chat_id, config::BOT_NAME, 0, false), 8);
        if (created && created->get_id() == td_api::groupCallId::ID) {
            int32_t gcid = static_cast<const td_api::groupCallId*>(created.get())->id_;
            std::lock_guard<std::mutex> lock(mtx_);
            chat_group_call_ids_[chat_id] = gcid;
            return gcid;
        }
        if (created && created->get_id() == td_api::error::ID) {
            auto* err = static_cast<const td_api::error*>(created.get());
            LOGGER("AnnieXMedia.core.userbot").error("❌ Assistant createVideoChat(" + std::to_string(chat_id) + ") failed: " + err->message_);
        }
    }
    return 0;
}

std::string AssistantClient::join_group_call_signaling(int64_t chat_id, const std::string& join_payload_json,
                                                       int32_t audio_source_ssrc, bool is_muted, bool is_video_enabled) {
    int32_t group_call_id = get_or_create_group_call_id(chat_id, true);
    if (group_call_id == 0) {
        throw std::runtime_error("لم يتم العثور على مكالمة صوتية مفتوحة في المجموعة (تأكد من فتح المكالمة ووجود المساعد في المجموعة).");
    }

    auto join_params = td_api::make_object<td_api::groupCallJoinParameters>(
        audio_source_ssrc,
        join_payload_json,
        is_muted,
        is_video_enabled
    );

    td_api::object_ptr<td_api::MessageSender> participant_id = nullptr;
    if (this->id != 0) {
        participant_id = td_api::make_object<td_api::messageSenderUser>(this->id);
    }

    auto req = td_api::make_object<td_api::joinVideoChat>(
        group_call_id,
        std::move(participant_id),
        std::move(join_params),
        ""
    );

    auto resp = send_query_sync(std::move(req), 12);
    if (resp && resp->get_id() == td_api::text::ID) {
        std::string transport = static_cast<const td_api::text*>(resp.get())->text_;
        LOGGER("AnnieXMedia.core.userbot").info("✅ TDLib joinVideoChat succeeded (" + std::to_string(transport.size()) + " bytes transport).");
        return transport;
    }
    if (resp && resp->get_id() == td_api::error::ID) {
        auto* err = static_cast<const td_api::error*>(resp.get());
        LOGGER("AnnieXMedia.core.userbot").error("❌ TDLib joinVideoChat error: [" + std::to_string(err->code_) + "] " + err->message_);
        throw std::runtime_error("TDLib joinVideoChat error: " + err->message_);
    }
    throw std::runtime_error("Failed to receive transport payload from joinVideoChat");
}

bool AssistantClient::leave_group_call_signaling(int64_t chat_id, bool end_call) {
    int32_t gcid = 0;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = chat_group_call_ids_.find(chat_id);
        if (it != chat_group_call_ids_.end()) gcid = it->second;
    }
    if (gcid == 0) gcid = get_or_create_group_call_id(chat_id, false);
    if (gcid == 0) return false;

    if (end_call) {
        auto r = send_query_sync(td_api::make_object<td_api::endGroupCall>(gcid), 5);
        return r && r->get_id() == td_api::ok::ID;
    } else {
        auto r = send_query_sync(td_api::make_object<td_api::leaveGroupCall>(gcid), 5);
        return r && r->get_id() == td_api::ok::ID;
    }
}

bool AssistantClient::set_call_volume(int64_t chat_id, int volume_percent) {
    int32_t gcid = get_or_create_group_call_id(chat_id, false);
    if (gcid == 0) return false;
    int32_t td_vol = std::max(100, std::min(20000, volume_percent * 100));
    auto participant = td_api::make_object<td_api::messageSenderUser>(this->id);
    auto r = send_query_sync(
        td_api::make_object<td_api::setGroupCallParticipantVolumeLevel>(gcid, std::move(participant), td_vol), 5
    );
    return r && r->get_id() == td_api::ok::ID;
}

std::vector<int64_t> AssistantClient::get_call_participants(int64_t chat_id) {
    std::vector<int64_t> res;
    int32_t gcid = get_or_create_group_call_id(chat_id, false);
    if (gcid == 0) return res;
    send_query_sync(td_api::make_object<td_api::loadGroupCallParticipants>(gcid, 100), 5);
    auto gc = send_query_sync(td_api::make_object<td_api::getGroupCall>(gcid), 5);
    if (gc && gc->get_id() == td_api::groupCall::ID) {
        auto* g = static_cast<const td_api::groupCall*>(gc.get());
        for (const auto& sp : g->recent_speakers_) {
            if (sp && sp->participant_id_ && sp->participant_id_->get_id() == td_api::messageSenderUser::ID) {
                res.push_back(static_cast<const td_api::messageSenderUser*>(sp->participant_id_.get())->user_id_);
            }
        }
    }
    return res;
}

bool AssistantClient::join_chat_by_link_or_username(const std::string& link_or_username) {
    if (link_or_username.find("t.me/") != std::string::npos || link_or_username.find("http") != std::string::npos) {
        auto r = send_query_sync(td_api::make_object<td_api::joinChatByInviteLink>(link_or_username), 8);
        return r && r->get_id() == td_api::chat::ID;
    }
    std::string clean = link_or_username;
    if (!clean.empty() && clean[0] == '@') clean.erase(clean.begin());
    auto pub = send_query_sync(td_api::make_object<td_api::searchPublicChat>(clean), 8);
    if (pub && pub->get_id() == td_api::chat::ID) {
        int64_t cid = static_cast<const td_api::chat*>(pub.get())->id_;
        auto r = send_query_sync(td_api::make_object<td_api::joinChat>(cid), 8);
        return r && r->get_id() == td_api::ok::ID;
    }
    return false;
}

bool AssistantClient::leave_chat(int64_t chat_id) {
    auto r = send_query_sync(td_api::make_object<td_api::leaveChat>(chat_id), 5);
    return r && r->get_id() == td_api::ok::ID;
}

void AssistantClient::send_message(int64_t chat_id, const std::string& text) {
    if (chat_id > 0) {
        send_query_sync(td_api::make_object<td_api::createPrivateChat>(chat_id, false), 3);
    }
    auto req = td_api::make_object<td_api::sendMessage>();
    req->chat_id_ = chat_id;
    auto content = td_api::make_object<td_api::inputMessageText>();
    auto ft = td_api::make_object<td_api::formattedText>();
    ft->text_ = text;
    content->text_ = std::move(ft);
    req->input_message_content_ = std::move(content);
    send_query(std::move(req), [chat_id](td_api::object_ptr<td_api::Object> obj) {
        if (obj && obj->get_id() == td_api::error::ID) {
            auto* err = static_cast<const td_api::error*>(obj.get());
            LOGGER("AnnieXMedia.core.userbot").warning("⚠️ Assistant send_message(" + std::to_string(chat_id) + ") warning: " + err->message_);
        }
    });
}

Userbot::Userbot() = default;

void Userbot::start() {
    LOGGER("AnnieXMedia.core.userbot").info("💝 جارٍ بدء تشغيل حسابات المساعدين (TDLib Native)...");
    if (!config::STRING1.empty()) {
        one = std::make_shared<AssistantClient>(1, config::STRING1);
        one->start();
    }
    if (!config::STRING2.empty()) {
        two = std::make_shared<AssistantClient>(2, config::STRING2);
        two->start();
    }
    if (!config::STRING3.empty()) {
        three = std::make_shared<AssistantClient>(3, config::STRING3);
        three->start();
    }
    if (!config::STRING4.empty()) {
        four = std::make_shared<AssistantClient>(4, config::STRING4);
        four->start();
    }
    if (!config::STRING5.empty()) {
        five = std::make_shared<AssistantClient>(5, config::STRING5);
        five->start();
    }
}

void Userbot::stop() {
    LOGGER("AnnieXMedia.core.userbot").info("☔ جارٍ إيقاف المساعدين...");
    if (one) one->stop();
    if (two) two->stop();
    if (three) three->stop();
    if (four) four->stop();
    if (five) five->stop();
}

std::shared_ptr<AssistantClient> Userbot::get_by_index(int index) const {
    switch (index) {
        case 1: return one;
        case 2: return two;
        case 3: return three;
        case 4: return four;
        case 5: return five;
        default: return one;
    }
}

} // namespace AnnieXMedia::core
