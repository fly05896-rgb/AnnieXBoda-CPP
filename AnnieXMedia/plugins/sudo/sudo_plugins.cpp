#include "AnnieXMedia/plugins/plugin_manager.hpp"
#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/core/git.hpp"
#include "AnnieXMedia/core/mongo.hpp"
#include "AnnieXMedia/misc.hpp"
#include "AnnieXMedia/utils/database.hpp"
#include "config.hpp"

#include <cstdlib>
#include <filesystem>

namespace AnnieXMedia::plugins {

void register_sudo_plugins() {
    core::app.on_message([](const core::IncomingMessage& msg) -> bool {
        auto [cmd, args] = parse_command(msg.text);
        if (cmd.empty()) return false;

        // Owner-only commands: addsudo, delsudo, delallsudo, backup, post
        if (msg.sender_user_id == config::OWNER_ID) {
            if (cmd == "addsudo") {
                int64_t target = msg.reply_sender_user_id;
                if (target == 0 && !args.empty()) { try { target = std::stoll(args); } catch (...) {} }
                if (target != 0) {
                    SUDOERS.insert(target);
                    std::vector<int64_t> list(SUDOERS.begin(), SUDOERS.end());
                    core::mongodb.collection("sudoers")->set_int_list("sudo", "sudoers", list);
                    core::app.send_message(msg.chat_id, "✅ تمت ترقية المستخدم إلى قائمة المطورين: <code>" + std::to_string(target) + "</code>", {}, msg.id);
                }
                return true;
            }
            if (cmd == "delsudo" || cmd == "rmsudo") {
                int64_t target = msg.reply_sender_user_id;
                if (target == 0 && !args.empty()) { try { target = std::stoll(args); } catch (...) {} }
                if (target != 0 && target != config::OWNER_ID) {
                    SUDOERS.erase(target);
                    std::vector<int64_t> list(SUDOERS.begin(), SUDOERS.end());
                    core::mongodb.collection("sudoers")->set_int_list("sudo", "sudoers", list);
                    core::app.send_message(msg.chat_id, "✅ تم تنزيل المستخدم من قائمة المطورين.", {}, msg.id);
                }
                return true;
            }
            if (cmd == "delallsudo") {
                SUDOERS.clear();
                SUDOERS.insert(config::OWNER_ID);
                core::mongodb.collection("sudoers")->set_int_list("sudo", "sudoers", {config::OWNER_ID});
                core::app.send_message(msg.chat_id, "✅ تم حذف جميع المطورين الثانويين.", {}, msg.id);
                return true;
            }
        }

        if (cmd == "sudolist" || cmd == "listsudo" || cmd == "sudoers") {
            std::string txt = "👑 <b>قائمة مطوري البوت:</b>\n\n• المالك: <code>" + std::to_string(config::OWNER_ID) + "</code>\n";
            for (auto id : SUDOERS) {
                if (id != config::OWNER_ID) txt += "• مطور: <code>" + std::to_string(id) + "</code>\n";
            }
            core::app.send_message(msg.chat_id, txt, {}, msg.id);
            return true;
        }

        if (!is_sudo(msg.sender_user_id)) return false;

        // Sudo commands: gban, ungban, block, unblock, maintenance, logger, autoend, getlog, restart, update
        if (cmd == "gban" || cmd == "globalban") {
            int64_t target = msg.reply_sender_user_id;
            if (target == 0 && !args.empty()) { try { target = std::stoll(args); } catch (...) {} }
            if (target != 0 && !is_sudo(target)) {
                utils::database::add_gban_user(target);
                config::BANNED_USERS.insert(target);
                for (auto cid : utils::database::get_served_chats()) {
                    core::app.ban_chat_member(cid, target);
                }
                core::app.send_message(msg.chat_id, "🚫 تم الحظر العام للمستخدم: <code>" + std::to_string(target) + "</code>", {}, msg.id);
            }
            return true;
        }

        if (cmd == "ungban") {
            int64_t target = msg.reply_sender_user_id;
            if (target == 0 && !args.empty()) { try { target = std::stoll(args); } catch (...) {} }
            if (target != 0) {
                utils::database::remove_gban_user(target);
                config::BANNED_USERS.erase(target);
                core::app.send_message(msg.chat_id, "✅ تم إلغاء الحظر العام عن: <code>" + std::to_string(target) + "</code>", {}, msg.id);
            }
            return true;
        }

        if (cmd == "gbannedusers" || cmd == "gbanlist") {
            auto list = utils::database::get_gbanned();
            std::string txt = "🚫 <b>قائمة المحظورين عام (" + std::to_string(list.size()) + "):</b>\n";
            for (auto u : list) txt += "• <code>" + std::to_string(u) + "</code>\n";
            core::app.send_message(msg.chat_id, txt, {}, msg.id);
            return true;
        }

        if (cmd == "block") {
            int64_t target = msg.reply_sender_user_id;
            if (target == 0 && !args.empty()) { try { target = std::stoll(args); } catch (...) {} }
            if (target != 0 && !is_sudo(target)) {
                utils::database::add_banned_user(target);
                config::BANNED_USERS.insert(target);
                core::app.send_message(msg.chat_id, "🚷 تم حظر المستخدم من استخدام البوت.", {}, msg.id);
            }
            return true;
        }

        if (cmd == "unblock") {
            int64_t target = msg.reply_sender_user_id;
            if (target == 0 && !args.empty()) { try { target = std::stoll(args); } catch (...) {} }
            if (target != 0) {
                utils::database::remove_banned_user(target);
                config::BANNED_USERS.erase(target);
                core::app.send_message(msg.chat_id, "✅ تم فك حظر المستخدم من البوت.", {}, msg.id);
            }
            return true;
        }

        if (cmd == "blchat" || cmd == "blacklistchat") {
            int64_t cid = args.empty() ? msg.chat_id : std::stoll(args);
            utils::database::blacklist_chat(cid);
            core::app.send_message(msg.chat_id, "✅ تم حظر المجموعة: <code>" + std::to_string(cid) + "</code>", {}, msg.id);
            return true;
        }

        if (cmd == "whitelistchat" || cmd == "unblacklistchat" || cmd == "unblchat") {
            int64_t cid = args.empty() ? msg.chat_id : std::stoll(args);
            utils::database::whitelist_chat(cid);
            core::app.send_message(msg.chat_id, "✅ تم رفع الحظر عن المجموعة: <code>" + std::to_string(cid) + "</code>", {}, msg.id);
            return true;
        }

        if (cmd == "maintenance") {
            if (args == "enable") {
                utils::database::maintenance_on();
                core::app.send_message(msg.chat_id, "🛠 تم تفعيل وضع الصيانة.", {}, msg.id);
            } else {
                utils::database::maintenance_off();
                core::app.send_message(msg.chat_id, "✅ تم إيقاف وضع الصيانة.", {}, msg.id);
            }
            return true;
        }

        if (cmd == "autoend") {
            if (args == "enable") {
                utils::database::autoend_on();
                core::app.send_message(msg.chat_id, "✅ تم تفعيل الإنهاء التلقائي للمكالمات الفارغة.", {}, msg.id);
            } else {
                utils::database::autoend_off();
                core::app.send_message(msg.chat_id, "✅ تم تعطيل الإنهاء التلقائي.", {}, msg.id);
            }
            return true;
        }

        if (cmd == "getlog" || cmd == "logs" || cmd == "getlogs") {
            if (std::filesystem::exists("log.txt")) {
                core::app.send_document(msg.chat_id, "log.txt", "📄 سجل تشغيل البوت (C++ Logs)", msg.id);
            } else {
                core::app.send_message(msg.chat_id, "لا يوجد ملف سجل حالياً.", {}, msg.id);
            }
            return true;
        }

        if (cmd == "update" || cmd == "gitpull") {
            auto [out, rc] = core::run_shell_command("git pull");
            core::app.send_message(msg.chat_id, "🔄 <b>نتيجة التحديث:</b>\n<code>" + out + "</code>", {}, msg.id);
            return true;
        }

        if (cmd == "restart") {
            core::app.send_message(msg.chat_id, "🔄 جاري إعادة تشغيل البوت...", {}, msg.id);
            std::exit(0);
            return true;
        }

        return false;
    });
}

} // namespace AnnieXMedia::plugins
