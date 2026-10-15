#include "AnnieXMedia/plugins/plugin_manager.hpp"
#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/misc.hpp"
#include "AnnieXMedia/platforms/SongDownloader.hpp"
#include "AnnieXMedia/platforms/Youtube.hpp"
#include "AnnieXMedia/utils/database.hpp"
#include "AnnieXMedia/utils/formatters.hpp"
#include "AnnieXMedia/utils/inline.hpp"
#include "config.hpp"
#include "strings/strings.hpp"

#include <ctime>

namespace AnnieXMedia::plugins {

void register_bot_plugins() {
    core::app.on_message([](const core::IncomingMessage& msg) -> bool {
        if (config::BANNED_USERS.count(msg.sender_user_id)) return false;
        auto [cmd, args] = parse_command(msg.text);
        if (cmd.empty()) return false;
        std::string lang = utils::database::get_lang(msg.chat_id);

        // 1. /start
        if (cmd == "start") {
            int64_t up_secs = std::time(nullptr) - AnnieXMedia::_boot_;
            std::string uptime = utils::formatters::get_readable_time(up_secs);
            if (msg.is_private) {
                utils::database::add_served_user(msg.sender_user_id);
                std::string caption = strings::format_str(
                    config::AYUV[0],
                    {msg.mention(), core::app.name, uptime, "0", "0", "12.5%"}
                );
                core::app.send_photo(msg.chat_id, config::START_IMG_URL, caption, utils::inline_markups::private_panel(lang), msg.id);
            } else {
                utils::database::add_served_chat(msg.chat_id);
                std::string caption = strings::format_str(
                    config::AYUV[1],
                    {msg.chat_title.empty() ? "المجموعة" : msg.chat_title, core::app.name, uptime}
                );
                core::app.send_photo(msg.chat_id, config::START_IMG_URL, caption, utils::inline_markups::start_panel(lang), msg.id);
            }
            return true;
        }

        // 2. /help, مساعدة, الاوامر, اوامر
        if (cmd == "help" || cmd == "مساعدة" || cmd == "الاوامر" || cmd == "اوامر") {
            core::app.send_photo(
                msg.chat_id,
                config::HELP_IMG_URL,
                strings::format_str(strings::tr(lang, "help_1"), {config::SUPPORT_CHAT}),
                utils::inline_markups::help_pannel(lang),
                msg.id
            );
            return true;
        }

        // 3. /settings, الاعدادات, اعدادات
        if (cmd == "settings" || cmd == "setting" || cmd == "الاعدادات" || cmd == "اعدادات") {
            core::app.send_photo(
                msg.chat_id,
                config::START_IMG_URL,
                strings::format_str(strings::tr(lang, "setting_1"), {
                    msg.chat_title.empty() ? "Group" : msg.chat_title,
                    std::to_string(msg.chat_id)
                }),
                utils::inline_markups::setting_markup(lang),
                msg.id
            );
            return true;
        }

        // 4. Song downloader (يوت, yut, song, ليست, لست)
        if ((cmd == "رفع الجودة" || cmd == "ارفع الجودة") && is_sudo(msg.sender_user_id)) {
            platforms::SongDownloader.enable_quality();
            core::app.send_message(msg.chat_id, "✅ تم تفعيل الجودة العالية للتحميل.", {}, msg.id);
            return true;
        }
        if ((cmd == "قفل الجودة" || cmd == "اقفل الجودة") && is_sudo(msg.sender_user_id)) {
            platforms::SongDownloader.disable_quality();
            core::app.send_message(msg.chat_id, "✅ تم ضبط الجودة العادية السريعة للتحميل.", {}, msg.id);
            return true;
        }
        if (cmd == "قفل التنزيل" && is_sudo(msg.sender_user_id)) {
            utils::database::set_config("download_locked", true);
            core::app.send_message(msg.chat_id, "🔒 تم قفل التنزيل بنجاح.", {}, msg.id);
            return true;
        }
        if (cmd == "فتح التنزيل" && is_sudo(msg.sender_user_id)) {
            utils::database::set_config("download_locked", false);
            core::app.send_message(msg.chat_id, "🔓 تم فتح التنزيل بنجاح.", {}, msg.id);
            return true;
        }

        if (cmd == "song" || cmd == "يوت" || cmd == "yut") {
            if (utils::database::get_config("download_locked") && !is_sudo(msg.sender_user_id)) {
                core::app.send_message(msg.chat_id, "- التنزيل مغلق حالياً من قبل المطور.", {}, msg.id);
                return true;
            }
            if (args.empty()) {
                core::app.send_message(msg.chat_id, "يرجى كتابة اسم المقطع بعد الأمر، مثال: <code>يوت سورة الكهف</code>", {}, msg.id);
                return true;
            }
            int64_t wait_id = core::app.send_message(msg.chat_id, "🔎 جاري البحث والتحميل...", {}, msg.id);
            auto info = platforms::SongDownloader.get_info(args);
            std::string path = platforms::SongDownloader.download_media(info.link, false);
            core::app.delete_message(msg.chat_id, wait_id);
            if (!path.empty()) {
                core::app.send_audio(msg.chat_id, path, "🎵 <b>" + info.title + "</b>", info.title, info.duration_sec, {}, msg.id);
            } else {
                core::app.send_message(msg.chat_id, "❌ تعذر تحميل المقطع.", {}, msg.id);
            }
            return true;
        }

        // 5. Repo (سورس / السورس / يا سورس / repo)
        if (cmd == "repo" || cmd == "سورس" || cmd == "السورس" || cmd == "يا سورس") {
            std::string txt =
                "╭──── • ◈ • ────╮\n"
                "么 <a href=\"" + config::SUPPORT_CHANNEL + "\">𝚂ᴏᴜʀᴄᴇ ✘ 𝐁ᴏᴅᴀ (C++ Edition)</a>\n"
                "么 <a href=\"" + config::SUPPORT_CHAT + "\">جروب الدعم</a>\n"
                "╰──── • ◈ • ────╯\n"
                "⍟ الـبـوت يعمل بالكامل بلغة <b>C++20</b> مع <b>TDLib</b> و <b>Native C++ NTgCalls</b>.";
            core::InlineKeyboardMarkup kb = {
                {{"قناة السورس", "", config::SUPPORT_CHANNEL, ""}, {"جروب الدعم", "", config::SUPPORT_CHAT, ""}}
            };
            core::app.send_photo(msg.chat_id, config::START_IMG_URL, txt, kb, msg.id);
            return true;
        }

        return false;
    });

    core::app.on_callback_query([](const core::IncomingCallbackQuery& cb) -> bool {
        std::string lang = utils::database::get_lang(cb.chat_id);
        if (cb.data == "settings_back_helper") {
            core::app.edit_message_caption(
                cb.chat_id, cb.message_id,
                strings::format_str(strings::tr(lang, "help_1"), {config::SUPPORT_CHAT}),
                utils::inline_markups::help_pannel(lang, true)
            );
            return true;
        }
        if (cb.data.rfind("help_callback ", 0) == 0) {
            std::string key = cb.data.substr(14);
            auto it = strings::HELP_SECTIONS_AR.find(key);
            std::string text = (it != strings::HELP_SECTIONS_AR.end()) ? it->second : strings::tr(lang, "help_1");
            core::InlineKeyboardMarkup back_kb = {
                {{strings::tr(lang, "BACK_BUTTON"), "settings_back_helper", "", ""}}
            };
            core::app.edit_message_caption(cb.chat_id, cb.message_id, text, back_kb);
            return true;
        }
        return false;
    });
}

} // namespace AnnieXMedia::plugins
