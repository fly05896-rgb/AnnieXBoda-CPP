// Authored By Certified Coders © 2026
// Converted from AnnieXMedia/plugins/Manager/* to C++20
#include "AnnieXMedia/plugins/plugin_manager.hpp"
#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/core/call.hpp"
#include "AnnieXMedia/core/userbot.hpp"
#include "AnnieXMedia/misc.hpp"
#include "AnnieXMedia/mongo/welcomedb.hpp"
#include "AnnieXMedia/utils/database.hpp"
#include "config.hpp"

namespace AnnieXMedia::plugins {

void register_manager_plugins() {
    core::app.on_message([](const core::IncomingMessage& msg) -> bool {
        if (config::BANNED_USERS.count(msg.sender_user_id)) return false;

        // Welcome new members in groups
        if (msg.is_group && !msg.new_chat_member_ids.empty() && mongo::is_welcome_on(msg.chat_id)) {
            for (auto uid : msg.new_chat_member_ids) {
                core::app.send_message(
                    msg.chat_id,
                    "🎉 أهلاً بك <a href=\"tg://user?id=" + std::to_string(uid) + "\">عزيزي العضو</a> في المجموعة! نورتنا 🤍",
                    {}, msg.id
                );
            }
            return true;
        }

        auto [cmd, args] = parse_command(msg.text);
        if (cmd.empty()) return false;

        // 1. ID & Info (ايدي / id / info / كشف)
        if (cmd == "id" || cmd == "ايدي") {
            int64_t target_uid = msg.reply_sender_user_id != 0 ? msg.reply_sender_user_id : msg.sender_user_id;
            std::string txt = "🆔 <b>معلومات المعرف:</b>\n\n"
                              "• معرف المستخدم: <code>" + std::to_string(target_uid) + "</code>\n"
                              "• معرف الدردشة: <code>" + std::to_string(msg.chat_id) + "</code>";
            core::app.send_message(msg.chat_id, txt, {}, msg.id);
            return true;
        }

        if (cmd == "info" || cmd == "userinfo" || cmd == "whois" || cmd == "كشف") {
            int64_t target_uid = msg.reply_sender_user_id != 0 ? msg.reply_sender_user_id : msg.sender_user_id;
            std::string role = is_sudo(target_uid) ? "مطور بالبوت 👑" :
                               (core::app.is_admin(msg.chat_id, target_uid) ? "مشرف المجموعة ⭐️" : "عضو 🤍");
            std::string txt = "👤 <b>بطاقة العضو:</b>\n\n"
                              "• الآيدي: <code>" + std::to_string(target_uid) + "</code>\n"
                              "• الرتبة: <b>" + role + "</b>\n"
                              "• المجموعة: <code>" + std::to_string(msg.chat_id) + "</code>";
            core::app.send_message(msg.chat_id, txt, {}, msg.id);
            return true;
        }

        if (!msg.is_group) return false;
        bool is_adm = is_sudo(msg.sender_user_id) || core::app.is_admin(msg.chat_id, msg.sender_user_id);

        // 2. Pin / Unpin / SetTitle / SetDescription / Del
        if ((cmd == "pin" || cmd == "تثبيت") && is_adm && msg.reply_to_message_id != 0) {
            core::app.pin_chat_message(msg.chat_id, msg.reply_to_message_id, false);
            core::app.send_message(msg.chat_id, "📌 تم تثبيت الرسالة بنجاح.", {}, msg.id);
            return true;
        }
        if ((cmd == "unpin" || cmd == "الغاء التثبيت") && is_adm) {
            core::app.unpin_chat_message(msg.chat_id, msg.reply_to_message_id);
            core::app.send_message(msg.chat_id, "📌 تم إلغاء تثبيت الرسالة.", {}, msg.id);
            return true;
        }
        if ((cmd == "settitle" || cmd == "تغيير الاسم") && is_adm && !args.empty()) {
            core::app.set_chat_title(msg.chat_id, args);
            core::app.send_message(msg.chat_id, "✅ تم تغيير اسم المجموعة إلى: <b>" + args + "</b>", {}, msg.id);
            return true;
        }
        if ((cmd == "setdiscription" || cmd == "تغيير الوصف") && is_adm && !args.empty()) {
            core::app.set_chat_description(msg.chat_id, args);
            core::app.send_message(msg.chat_id, "✅ تم تحديث وصف المجموعة بنجاح.", {}, msg.id);
            return true;
        }
        if ((cmd == "removephoto" || cmd == "حذف الصورة") && is_adm) {
            core::app.delete_chat_photo(msg.chat_id);
            core::app.send_message(msg.chat_id, "✅ تم حذف صورة المجموعة.", {}, msg.id);
            return true;
        }
        if ((cmd == "del" || cmd == "مسح") && is_adm && msg.reply_to_message_id != 0) {
            core::app.delete_messages(msg.chat_id, {msg.reply_to_message_id, msg.id});
            return true;
        }

        // 3. Promote / Fullpromote / Demote
        if ((cmd == "promote" || cmd == "fullpromote" || cmd == "demote") && is_adm && msg.reply_sender_user_id != 0) {
            if (cmd == "demote") {
                core::app.demote_chat_member(msg.chat_id, msg.reply_sender_user_id);
                core::app.send_message(msg.chat_id, "✅ تم تنزيل المشرف بنجاح.", {}, msg.id);
            } else {
                core::app.promote_chat_member(msg.chat_id, msg.reply_sender_user_id, cmd == "fullpromote");
                core::app.send_message(msg.chat_id, "✅ تم رفع العضو مشرفاً في المجموعة.", {}, msg.id);
            }
            return true;
        }

        // 4. Admins list & Tag all
        if (cmd == "admins" || cmd == "staff" || cmd == "المشرفين") {
            auto adms = core::app.get_chat_admins(msg.chat_id);
            std::string txt = "⭐️ <b>مشرفو المجموعة (" + std::to_string(adms.size()) + "):</b>\n\n";
            for (auto uid : adms) {
                txt += "• <a href=\"tg://user?id=" + std::to_string(uid) + "\">" + std::to_string(uid) + "</a>\n";
            }
            core::app.send_message(msg.chat_id, txt, {}, msg.id);
            return true;
        }

        if ((cmd == "utag" || cmd == "all" || cmd == "mention" || cmd == "tagall") && is_adm) {
            auto adms = core::app.get_chat_admins(msg.chat_id);
            std::string txt = "📢 <b>نداء عام: " + args + "</b>\n\n";
            for (auto uid : adms) {
                txt += "<a href=\"tg://user?id=" + std::to_string(uid) + "\">•</a> ";
            }
            core::app.send_message(msg.chat_id, txt, {}, msg.id);
            return true;
        }

        // 5. Assistant Join / Leave (ادخل، انضم، اخرج، غادر، userbotjoin, userbotleave)
        if ((cmd == "ادخل" || cmd == "انضم" || cmd == "دخول المساعد" || cmd == "userbotjoin") && is_adm) {
            auto [ntg, ass] = core::StreamController.get_group_assistant(msg.chat_id);
            if (!ass) return true;
            std::string link = !msg.chat_username.empty() ? msg.chat_username : core::app.export_chat_invite_link(msg.chat_id);
            if (!link.empty() && ass->join_chat_by_link_or_username(link)) {
                core::app.send_message(msg.chat_id, "✅ انضم الحساب المساعد إلى المجموعة بنجاح.", {}, msg.id);
            } else {
                core::app.send_message(msg.chat_id, "ℹ️ الحساب المساعد موجود بالفعل أو يحتاج صلاحية دعوة المستخدمين.", {}, msg.id);
            }
            return true;
        }

        if ((cmd == "اخرج" || cmd == "غادر" || cmd == "خروج المساعد" || cmd == "userbotleave") && is_adm) {
            auto [ntg, ass] = core::StreamController.get_group_assistant(msg.chat_id);
            if (ass) {
                ass->leave_chat(msg.chat_id);
                core::app.send_message(msg.chat_id, "👋 غادر الحساب المساعد المجموعة.", {}, msg.id);
            }
            return true;
        }

        // 6. Language (lang, setlang, language)
        if ((cmd == "lang" || cmd == "setlang" || cmd == "language") && is_adm) {
            std::string target_lang = args.empty() ? "ar" : args;
            utils::database::set_lang(msg.chat_id, target_lang);
            core::app.send_message(msg.chat_id, "✅ تم ضبط لغة البوت في المجموعة على: <code>" + target_lang + "</code>", {}, msg.id);
            return true;
        }

        return false;
    });
}

} // namespace AnnieXMedia::plugins
