#include "AnnieXMedia/plugins/plugin_manager.hpp"
#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/core/call.hpp"
#include "AnnieXMedia/misc.hpp"
#include "AnnieXMedia/utils/database.hpp"
#include "AnnieXMedia/utils/inline.hpp"
#include "config.hpp"
#include "strings/strings.hpp"

#include <algorithm>
#include <random>

namespace AnnieXMedia::plugins {

static bool check_play_admin(int64_t chat_id, int64_t user_id) {
    if (is_sudo(user_id)) return true;
    if (utils::database::is_nonadmin_chat(chat_id)) return true;
    auto auths = utils::database::get_authuser_names(chat_id);
    if (std::find(auths.begin(), auths.end(), std::to_string(user_id)) != auths.end()) return true;
    return core::app.is_admin(chat_id, user_id);
}

void register_admin_plugins() {
    core::app.on_message([](const core::IncomingMessage& msg) -> bool {
        if (!msg.is_group || config::BANNED_USERS.count(msg.sender_user_id)) return false;
        auto [cmd, args] = parse_command(msg.text);
        if (cmd.empty()) return false;
        std::string lang = utils::database::get_lang(msg.chat_id);

        // 1. Pause (وقف مؤقت)
        if (cmd == "pause" || cmd == "cpause" || cmd == "وقف مؤقت" || cmd == "توقيف مؤقت" || cmd == "اسكت") {
            if (!check_play_admin(msg.chat_id, msg.sender_user_id)) {
                core::app.send_message(msg.chat_id, strings::tr(lang, "admin_14"), {}, msg.id);
                return true;
            }
            if (!utils::database::is_active_chat(msg.chat_id)) {
                core::app.send_message(msg.chat_id, strings::tr(lang, "general_5"), {}, msg.id);
                return true;
            }
            utils::database::music_off(msg.chat_id);
            core::StreamController.pause_stream(msg.chat_id);
            core::app.send_message(
                msg.chat_id,
                strings::format_str(strings::tr(lang, "admin_2"), {msg.mention()}),
                utils::inline_markups::close_markup(lang),
                msg.id
            );
            return true;
        }

        // 2. Resume (كمل / استئناف)
        if (cmd == "resume" || cmd == "cresume" || cmd == "استئناف" || cmd == "كمل" || cmd == "تابع") {
            if (!check_play_admin(msg.chat_id, msg.sender_user_id)) {
                core::app.send_message(msg.chat_id, strings::tr(lang, "admin_14"), {}, msg.id);
                return true;
            }
            if (!utils::database::is_active_chat(msg.chat_id)) {
                core::app.send_message(msg.chat_id, strings::tr(lang, "general_5"), {}, msg.id);
                return true;
            }
            utils::database::music_on(msg.chat_id);
            core::StreamController.resume_stream(msg.chat_id);
            core::app.send_message(
                msg.chat_id,
                strings::format_str(strings::tr(lang, "admin_4"), {msg.mention()}),
                utils::inline_markups::close_markup(lang),
                msg.id
            );
            return true;
        }

        // 3. Stop / End (ايقاف / انهاء)
        if (cmd == "stop" || cmd == "end" || cmd == "cstop" || cmd == "cend" ||
            cmd == "ايقاف" || cmd == "انهاء" || cmd == "قف") {
            if (!check_play_admin(msg.chat_id, msg.sender_user_id)) {
                core::app.send_message(msg.chat_id, strings::tr(lang, "admin_14"), {}, msg.id);
                return true;
            }
            core::StreamController.stop_stream(msg.chat_id);
            core::app.send_message(
                msg.chat_id,
                strings::format_str(strings::tr(lang, "admin_5"), {msg.mention()}),
                utils::inline_markups::close_markup(lang),
                msg.id
            );
            return true;
        }

        // 4. Skip (تخطي / التالي)
        if (cmd == "skip" || cmd == "cskip" || cmd == "next" || cmd == "cnext" || cmd == "تخطي" || cmd == "التالي") {
            if (!check_play_admin(msg.chat_id, msg.sender_user_id)) {
                core::app.send_message(msg.chat_id, strings::tr(lang, "admin_14"), {}, msg.id);
                return true;
            }
            auto [ntg, ass] = core::StreamController.get_group_assistant(msg.chat_id);
            core::StreamController.play(ntg, ass, msg.chat_id);
            core::app.send_message(
                msg.chat_id,
                "⏭ تم تخطي المقطع بواسطة: " + msg.mention(),
                utils::inline_markups::close_markup(lang),
                msg.id
            );
            return true;
        }

        // 5. Loop (تكرار / كرر)
        if (cmd == "loop" || cmd == "cloop" || cmd == "تكرار" || cmd == "كرر") {
            if (!check_play_admin(msg.chat_id, msg.sender_user_id)) return true;
            if (args.empty()) {
                core::app.send_message(msg.chat_id, strings::tr(lang, "admin_17"), {}, msg.id);
                return true;
            }
            if (args == "disable" || args == "تعطيل" || args == "0") {
                utils::database::set_loop(msg.chat_id, 0);
                core::app.send_message(msg.chat_id, strings::format_str(strings::tr(lang, "admin_19"), {msg.mention()}), {}, msg.id);
            } else if (args == "enable" || args == "تفعيل") {
                utils::database::set_loop(msg.chat_id, 10);
                core::app.send_message(msg.chat_id, strings::format_str(strings::tr(lang, "admin_18"), {"10", msg.mention()}), {}, msg.id);
            } else {
                int cnt = 5;
                try { cnt = std::max(1, std::min(10, std::stoi(args))); } catch (...) {}
                utils::database::set_loop(msg.chat_id, cnt);
                core::app.send_message(msg.chat_id, strings::format_str(strings::tr(lang, "admin_18"), {std::to_string(cnt), msg.mention()}), {}, msg.id);
            }
            return true;
        }

        // 6. Shuffle (خلط / عشوائي)
        if (cmd == "shuffle" || cmd == "cshuffle" || cmd == "خلط" || cmd == "عشوائي") {
            if (!check_play_admin(msg.chat_id, msg.sender_user_id)) return true;
            std::lock_guard<std::mutex> lock(AnnieXMedia::db_mutex);
            auto& q = AnnieXMedia::db[msg.chat_id];
            if (q.size() <= 2) {
                core::app.send_message(msg.chat_id, strings::tr(lang, "admin_15"), {}, msg.id);
                return true;
            }
            std::mt19937 rng(std::random_device{}());
            std::shuffle(q.begin() + 1, q.end(), rng);
            core::app.send_message(msg.chat_id, strings::format_str(strings::tr(lang, "admin_16"), {msg.mention()}), {}, msg.id);
            return true;
        }

        // 7. Seek (مرر / قدم / رجع / seek)
        if (cmd == "seek" || cmd == "cseek" || cmd == "seekback" || cmd == "cseekback" ||
            cmd == "مرر" || cmd == "قدم" || cmd == "رجع") {
            if (!check_play_admin(msg.chat_id, msg.sender_user_id)) return true;
            int sec = 0;
            try { sec = std::stoi(args); } catch (...) {
                core::app.send_message(msg.chat_id, strings::tr(lang, "admin_20"), {}, msg.id);
                return true;
            }
            config::QueueItem cur;
            {
                std::lock_guard<std::mutex> lock(AnnieXMedia::db_mutex);
                if (AnnieXMedia::db[msg.chat_id].empty()) return true;
                cur = AnnieXMedia::db[msg.chat_id][0];
            }
            bool backward = (cmd == "seekback" || cmd == "cseekback" || cmd == "رجع");
            int target = backward ? std::max(0, cur.played - sec) : std::min(cur.seconds, cur.played + sec);
            core::StreamController.seek_stream(msg.chat_id, cur.file, target, cur.seconds, cur.streamtype);
            core::app.send_message(msg.chat_id, "⏩ تم ضبط موضع البث بنجاح.", {}, msg.id);
            return true;
        }

        // 8. Speed (السرعة / speed)
        if (cmd == "speed" || cmd == "cspeed" || cmd == "slow" || cmd == "cslow" || cmd == "playback" || cmd == "السرعة") {
            if (!check_play_admin(msg.chat_id, msg.sender_user_id)) return true;
            core::app.send_message(
                msg.chat_id,
                strings::format_str(strings::tr(lang, "admin_28"), {core::app.name}),
                utils::inline_markups::speed_markup(lang, msg.chat_id),
                msg.id
            );
            return true;
        }

        // 9. Volume (الصوت / volume)
        if (cmd == "volume" || cmd == "cvolume" || cmd == "الصوت" || cmd == "مستوى الصوت") {
            if (!check_play_admin(msg.chat_id, msg.sender_user_id)) return true;
            int vol = 100;
            try { vol = std::max(1, std::min(200, std::stoi(args))); } catch (...) {
                core::app.send_message(msg.chat_id, "يرجى تحديد مستوى الصوت بين 1 و 200، مثال: <code>الصوت 150</code>", {}, msg.id);
                return true;
            }
            try {
                core::StreamController.change_volume_call(msg.chat_id, vol);
                core::app.send_message(msg.chat_id, "🔊 تم تغيير مستوى الصوت إلى <b>" + std::to_string(vol) + "%</b>", {}, msg.id);
            } catch (const std::exception& e) {
                core::app.send_message(msg.chat_id, std::string("❌ فشل تغيير الصوت: ") + e.what(), {}, msg.id);
            }
            return true;
        }

        // 10. VC Info (مين في الكول / م ف ك / vcinfo)
        if (cmd == "vcinfo" || cmd == "vcmembers" || cmd == "مين في الكول" || cmd == "م ف ك") {
            auto [ntg, ass] = core::StreamController.get_group_assistant(msg.chat_id);
            if (!ass) return true;
            auto parts = ass->get_call_participants(msg.chat_id);
            if (parts.empty()) {
                core::app.send_message(msg.chat_id, "🎙 لا يوجد متحدثون ظاهرون في المكالمة حالياً.", {}, msg.id);
                return true;
            }
            std::string txt = "🎙 <b>الموجودون في المكالمة الصوتية (" + std::to_string(parts.size()) + "):</b>\n\n";
            for (auto uid : parts) {
                txt += "• <a href=\"tg://user?id=" + std::to_string(uid) + "\">" + std::to_string(uid) + "</a>\n";
            }
            core::app.send_message(msg.chat_id, txt, {}, msg.id);
            return true;
        }

        // 11. Auth / Unauth / Authlist
        if (cmd == "auth" || cmd == "رفع ادمن") {
            if (!core::app.is_admin(msg.chat_id, msg.sender_user_id)) return true;
            int64_t target = msg.reply_sender_user_id;
            if (target == 0 && !args.empty()) {
                try { target = std::stoll(args); } catch (...) {}
            }
            if (target == 0) {
                core::app.send_message(msg.chat_id, strings::tr(lang, "general_1"), {}, msg.id);
                return true;
            }
            utils::database::save_authuser(msg.chat_id, std::to_string(target), {{"auth_user_id", std::to_string(target)}});
            core::app.send_message(msg.chat_id, strings::format_str(strings::tr(lang, "auth_2"), {std::to_string(target)}), {}, msg.id);
            return true;
        }

        if (cmd == "unauth" || cmd == "تنزيل ادمن") {
            if (!core::app.is_admin(msg.chat_id, msg.sender_user_id)) return true;
            int64_t target = msg.reply_sender_user_id;
            if (target == 0 && !args.empty()) {
                try { target = std::stoll(args); } catch (...) {}
            }
            if (target == 0) {
                core::app.send_message(msg.chat_id, strings::tr(lang, "general_1"), {}, msg.id);
                return true;
            }
            utils::database::delete_authuser(msg.chat_id, std::to_string(target));
            core::app.send_message(msg.chat_id, strings::format_str(strings::tr(lang, "auth_4"), {std::to_string(target)}), {}, msg.id);
            return true;
        }

        if (cmd == "authlist" || cmd == "authusers" || cmd == "قائمة الادمن") {
            auto list = utils::database::get_authuser_names(msg.chat_id);
            if (list.empty()) {
                core::app.send_message(msg.chat_id, "لا يوجد مستخدمون مفوضون في هذه المجموعة.", {}, msg.id);
                return true;
            }
            std::string out = "🔐 <b>قائمة المفوضين في المجموعة:</b>\n\n";
            for (const auto& u : list) {
                out += "• <code>" + u + "</code>\n";
            }
            core::app.send_message(msg.chat_id, out, {}, msg.id);
            return true;
        }

        return false;
    });

    // Register Admin Callbacks
    core::app.on_callback_query([](const core::IncomingCallbackQuery& cb) -> bool {
        if (cb.data == "close") {
            core::app.delete_message(cb.chat_id, cb.message_id);
            return true;
        }
        if (cb.data.rfind("ADMIN ", 0) == 0) {
            if (!check_play_admin(cb.chat_id, cb.sender_user_id)) {
                core::app.answer_callback_query(cb.id, "ليس لديك صلاحيات المشرف للتحكم بالبث.", true);
                return true;
            }
            std::string payload = cb.data.substr(6);
            auto bar = payload.find('|');
            std::string action = payload.substr(0, bar);
            int64_t target_chat = cb.chat_id;
            if (bar != std::string::npos) {
                try { target_chat = std::stoll(payload.substr(bar + 1)); } catch (...) {}
            }
            std::string lang = utils::database::get_lang(target_chat);

            if (action == "Pause") {
                utils::database::music_off(target_chat);
                core::StreamController.pause_stream(target_chat);
                core::app.answer_callback_query(cb.id, "⏸ تم إيقاف البث مؤقتاً");
                core::app.send_message(cb.chat_id, strings::format_str(strings::tr(lang, "admin_2"), {cb.mention()}));
            } else if (action == "Resume") {
                utils::database::music_on(target_chat);
                core::StreamController.resume_stream(target_chat);
                core::app.answer_callback_query(cb.id, "▶️ تم استئناف البث");
                core::app.send_message(cb.chat_id, strings::format_str(strings::tr(lang, "admin_4"), {cb.mention()}));
            } else if (action == "Stop") {
                core::StreamController.stop_stream(target_chat);
                core::app.answer_callback_query(cb.id, "⏹ تم إنهاء البث");
                core::app.send_message(cb.chat_id, strings::format_str(strings::tr(lang, "admin_5"), {cb.mention()}));
            } else if (action == "Skip" || action == "Replay") {
                auto [ntg, ass] = core::StreamController.get_group_assistant(target_chat);
                if (action == "Replay") {
                    utils::database::set_loop(target_chat, utils::database::get_loop(target_chat) + 1);
                }
                core::StreamController.play(ntg, ass, target_chat);
                core::app.answer_callback_query(cb.id, "⏭ تم التحديث");
            }
            return true;
        }
        if (cb.data.rfind("SpeedUP ", 0) == 0) {
            if (!check_play_admin(cb.chat_id, cb.sender_user_id)) {
                core::app.answer_callback_query(cb.id, "للمشرفين فقط.", true);
                return true;
            }
            std::string payload = cb.data.substr(8);
            auto bar = payload.find('|');
            if (bar != std::string::npos) {
                int64_t cid = std::stoll(payload.substr(0, bar));
                double spd = std::stod(payload.substr(bar + 1));
                std::lock_guard<std::mutex> lock(AnnieXMedia::db_mutex);
                if (!AnnieXMedia::db[cid].empty()) {
                    std::string f = AnnieXMedia::db[cid][0].file;
                    core::StreamController.speedup_stream(cid, f, spd, AnnieXMedia::db[cid]);
                    core::app.answer_callback_query(cb.id, "✅ تم تغيير السرعة");
                }
            }
            return true;
        }
        return false;
    });
}

} // namespace AnnieXMedia::plugins
