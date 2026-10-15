// Authored By Certified Coders © 2026
// Converted from AnnieXMedia/plugins/security/* to C++20
#include "AnnieXMedia/plugins/plugin_manager.hpp"
#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/core/mongo.hpp"
#include "AnnieXMedia/misc.hpp"
#include "config.hpp"

#include <map>
#include <regex>
#include <string>
#include <vector>

namespace AnnieXMedia::plugins {

static const std::map<std::string, std::string> LOCK_MAP = {
    {"الروابط", "links"}, {"المعرفات", "usernames"}, {"التاك", "hashtags"},
    {"الشارحه", "slashes"}, {"التثبيت", "pin"}, {"المتحركه", "animations"},
    {"الشات", "all"}, {"الصور", "photos"}, {"الملصقات", "stickers"},
    {"الملفات", "docs"}, {"البوتات", "bots"}, {"التكرار", "flood"},
    {"الكلايش", "long_msgs"}, {"الانلاين", "inline"}, {"الفيديو", "videos"},
    {"البصمات", "voice"}, {"السيلفي", "video_notes"}, {"الماركدوان", "markdown"},
    {"التوجيه", "forward"}, {"الاغاني", "audio"}, {"الجهات", "contacts"},
    {"الاشعارات", "service"}, {"السب", "porn_text"}, {"الاباحي", "porn_media"}
};

static const std::vector<std::string> BAD_WORDS = {
    "سكس", "نيك", "شرموط", "منيوك", "كسمك", "زب", "فحل", "بورن", "متناك", " قحبه", "فاجره", "متناكه", "خول"
};

static bool is_locked(int64_t chat_id, const std::string& key) {
    auto doc = core::mongodb.collection("protection_locks")->find_one(std::to_string(chat_id));
    if (!doc) return false;
    return doc->get_bool(key, false);
}

static void set_lock(int64_t chat_id, const std::string& key, bool locked) {
    core::mongodb.collection("protection_locks")->set_field(std::to_string(chat_id), key, locked ? "1" : "0");
}

void register_security_plugins() {
    core::app.on_message([](const core::IncomingMessage& msg) -> bool {
        if (!msg.is_group) return false;
        bool user_is_admin = is_sudo(msg.sender_user_id) || core::app.is_admin(msg.chat_id, msg.sender_user_id);

        auto [cmd, args] = parse_command(msg.text);

        // 1. أوامر الإدارة المباشرة (سماح، شد سماح، كتم، شد ميوت، فك الكتم)
        if (cmd == "سماح" || cmd == "شد سماح" || cmd == "كتم" || cmd == "شد ميوت" || cmd == "فك الكتم") {
            if (!user_is_admin) {
                core::app.send_message(msg.chat_id, "هذا الأمر مخصص للمشرفين فقط.", {}, msg.id);
                return true;
            }
            if (msg.reply_sender_user_id == 0) {
                core::app.send_message(msg.chat_id, "يجب الرد على رسالة المستخدم لتنفيذ هذا الأمر.", {}, msg.id);
                return true;
            }
            int64_t target = msg.reply_sender_user_id;
            if (cmd == "سماح") {
                core::app.promote_chat_member(msg.chat_id, target, false);
                core::app.send_message(msg.chat_id, "✅ تم منح صلاحيات الأدمن للمستخدم.", {}, msg.id);
            } else if (cmd == "شد سماح") {
                core::app.demote_chat_member(msg.chat_id, target);
                core::app.send_message(msg.chat_id, "✅ تم سحب صلاحيات الأدمن من المستخدم.", {}, msg.id);
            } else if (cmd == "كتم" || cmd == "شد ميوت") {
                core::app.mute_chat_member(msg.chat_id, target, 0);
                core::app.send_message(msg.chat_id, "🔇 تم كتم المستخدم في المجموعة.", {}, msg.id);
            } else if (cmd == "فك الكتم") {
                core::app.unmute_chat_member(msg.chat_id, target);
                core::app.send_message(msg.chat_id, "🔊 تم فك الكتم عن المستخدم.", {}, msg.id);
            }
            return true;
        }

        // 2. أوامر قفل / فتح (الروابط، الصور، السب، الشات، إلخ)
        if ((cmd == "قفل" || cmd == "فتح") && !args.empty()) {
            auto it = LOCK_MAP.find(args);
            if (it != LOCK_MAP.end()) {
                if (!user_is_admin) {
                    core::app.send_message(msg.chat_id, "هذا الأمر مخصص للمشرفين فقط.", {}, msg.id);
                    return true;
                }
                bool lock_state = (cmd == "قفل");
                set_lock(msg.chat_id, it->second, lock_state);
                core::app.send_message(
                    msg.chat_id,
                    (lock_state ? "🔒 تم قفل <b>" : "🔓 تم فتح <b>") + args + "</b> في هذه المجموعة.",
                    {}, msg.id
                );
                return true;
            }
        }

        // 3. فحص الرسائل التلقائي (لغير المشرفين)
        if (!user_is_admin) {
            bool violated = false;
            if (is_locked(msg.chat_id, "all")) violated = true;
            if (!violated && is_locked(msg.chat_id, "links") &&
                (msg.text.find("http://") != std::string::npos || msg.text.find("https://") != std::string::npos || msg.text.find("t.me/") != std::string::npos)) {
                violated = true;
            }
            if (!violated && is_locked(msg.chat_id, "usernames") && msg.text.find('@') != std::string::npos) violated = true;
            if (!violated && is_locked(msg.chat_id, "hashtags") && msg.text.find('#') != std::string::npos) violated = true;
            if (!violated && is_locked(msg.chat_id, "photos") && !msg.photo_file_id.empty()) violated = true;
            if (!violated && is_locked(msg.chat_id, "videos") && !msg.video_file_id.empty()) violated = true;
            if (!violated && is_locked(msg.chat_id, "stickers") && !msg.sticker_file_id.empty()) violated = true;
            if (!violated && is_locked(msg.chat_id, "voice") && !msg.voice_file_id.empty()) violated = true;
            if (!violated && is_locked(msg.chat_id, "audio") && !msg.audio_file_id.empty()) violated = true;
            if (!violated && is_locked(msg.chat_id, "forward") && msg.is_forwarded) violated = true;
            if (!violated && is_locked(msg.chat_id, "long_msgs") && msg.text.size() > 800) violated = true;

            if (!violated && is_locked(msg.chat_id, "porn_text")) {
                for (const auto& bw : BAD_WORDS) {
                    if (msg.text.find(bw) != std::string::npos) {
                        violated = true;
                        break;
                    }
                }
            }

            if (violated) {
                core::app.delete_message(msg.chat_id, msg.id);
                return true;
            }
        }

        return false;
    });
}

} // namespace AnnieXMedia::plugins
