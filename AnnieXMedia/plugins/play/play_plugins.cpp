#include "AnnieXMedia/plugins/plugin_manager.hpp"
#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/core/call.hpp"
#include "AnnieXMedia/core/userbot.hpp"
#include "AnnieXMedia/misc.hpp"
#include "AnnieXMedia/platforms/Apple.hpp"
#include "AnnieXMedia/platforms/Resso.hpp"
#include "AnnieXMedia/platforms/Soundcloud.hpp"
#include "AnnieXMedia/platforms/Spotify.hpp"
#include "AnnieXMedia/platforms/Telegram.hpp"
#include "AnnieXMedia/platforms/Youtube.hpp"
#include "AnnieXMedia/utils/database.hpp"
#include "AnnieXMedia/utils/inline.hpp"
#include "AnnieXMedia/utils/stream/stream.hpp"
#include "config.hpp"
#include "strings/strings.hpp"

#include <map>
#include <random>

namespace AnnieXMedia::plugins {

static const std::map<std::string, std::string> RADIO_STATIONS = {
    {"القرآن الكريم", "https://stream.radiojar.com/8s5u5tpdtwzuv"},
    {"نجوم اف ام", "https://ssl.mz-audiostreaming.com/nogoumfm"},
    {"نايل اف ام", "https://ssl.mz-audiostreaming.com/nilefm"},
    {"نغم اف ام", "https://ssl.mz-audiostreaming.com/naghamfm"},
    {"ميجا اف ام", "https://ssl.mz-audiostreaming.com/megafm"},
    {"الراديو 9090", "https://9090streaming.mobtada.com/9090FMEGYPT"},
    {"راديو مصر", "https://live.radiomasr.net/RADIOMASR"},
    {"محطة مصر", "https://s3.radio.co/s95f66299d/listen"},
    {"شعبى اف ام", "https://radio.masr.me/sha3byfm"},
    {"اون سبورت اف ام", "https://stream.radiojar.com/4884313205tv"}
};

void register_play_plugins() {
    core::app.on_message([](const core::IncomingMessage& msg) -> bool {
        if (config::BANNED_USERS.count(msg.sender_user_id)) return false;
        auto [cmd, args] = parse_command(msg.text);
        if (cmd.empty()) return false;

        // 1. قفل / فتح البحث (للمطورين)
        if ((cmd == "قفل البحث" || cmd == "تعطيل البحث") && is_sudo(msg.sender_user_id)) {
            utils::database::set_config("search_locked", true);
            core::app.send_message(msg.chat_id, "<b>تم قفل البحث بنجاح .</b>", {}, msg.id);
            return true;
        }
        if ((cmd == "فتح البحث" || cmd == "تفعيل البحث") && is_sudo(msg.sender_user_id)) {
            utils::database::set_config("search_locked", false);
            core::app.send_message(msg.chat_id, "<b>تم فتح البحث بنجاح .</b>", {}, msg.id);
            return true;
        }

        // 2. أوامر التشغيل الرئيسية (تشغيل، شغل، فيد، فيديو، play, vplay, cplay, cvplay, playforce, vplayforce)
        if (cmd == "play" || cmd == "vplay" || cmd == "cplay" || cmd == "cvplay" ||
            cmd == "playforce" || cmd == "vplayforce" || cmd == "cplayforce" || cmd == "cvplayforce" ||
            cmd == "تشغيل" || cmd == "شغل" || cmd == "فيد" || cmd == "فيديو") {
            if (!msg.is_group) return false;
            utils::database::add_served_chat(msg.chat_id);

            if (utils::database::get_config("search_locked") && !is_sudo(msg.sender_user_id)) {
                core::app.send_message(msg.chat_id, "- البحـث مغلـق .", {}, msg.id);
                return true;
            }

            bool is_video = (cmd == "vplay" || cmd == "cvplay" || cmd == "vplayforce" || cmd == "cvplayforce" ||
                             cmd == "فيد" || cmd == "فيديو");
            bool forceplay = (cmd.find("force") != std::string::npos);
            bool cplay = (cmd[0] == 'c');

            int64_t target_chat_id = msg.chat_id;
            if (cplay) {
                int64_t cm = utils::database::get_cmode(msg.chat_id);
                if (cm == 0) {
                    core::app.send_message(msg.chat_id, "» يرجى ربط قناة أولاً عبر الأمر /channelplay", {}, msg.id);
                    return true;
                }
                target_chat_id = cm;
            }

            std::string lang = utils::database::get_lang(msg.chat_id);
            std::string wait_text = config::AYU[msg.id % config::AYU.size()];

            if (args.empty() && msg.audio_file_id.empty() && msg.video_file_id.empty() && msg.voice_file_id.empty()) {
                core::app.send_message(msg.chat_id, strings::tr(lang, "play_1"), {}, msg.id);
                return true;
            }

            int64_t mystic_id = core::app.send_message(msg.chat_id, wait_text, {}, msg.id);

            try {
                platforms::TrackDetails details;
                if (platforms::Spotify.valid(args)) {
                    details = platforms::Spotify.track(args);
                } else if (platforms::SoundCloud.valid(args)) {
                    details = platforms::SoundCloud.download(args);
                } else if (platforms::Apple.valid(args)) {
                    details = platforms::Apple.track(args);
                } else if (platforms::Resso.valid(args)) {
                    details = platforms::Resso.track(args);
                } else {
                    details = platforms::YouTube.track(args, false);
                }

                std::string stype = (details.duration_min == "Live") ? "live" : "youtube";
                utils::stream::start_stream(
                    lang,
                    mystic_id,
                    msg.sender_user_id,
                    details,
                    target_chat_id,
                    msg.sender_first_name.empty() ? "User" : msg.sender_first_name,
                    msg.chat_id,
                    is_video,
                    stype,
                    forceplay
                );
            } catch (const std::exception& e) {
                core::app.edit_message_text(msg.chat_id, mystic_id, std::string("❌ حدث خطأ أثناء التشغيل:\n") + e.what());
            }
            return true;
        }

        // 3. نظام الراديو المباشر (راديو، radio, cradio, راديو القنوات)
        if (cmd == "راديو" || cmd == "radio" || cmd == "cradio" || cmd == "راديو القنوات") {
            if (!msg.is_group) return false;
            if (!is_sudo(msg.sender_user_id) && !core::app.is_admin(msg.chat_id, msg.sender_user_id)) {
                core::app.send_message(msg.chat_id, "هذا الامر مخصص للمشرفين فقط.", {}, msg.id);
                return true;
            }

            std::string stations_str;
            for (const auto& [name, url] : RADIO_STATIONS) {
                stations_str += "• " + name + "\n";
            }

            if (args.empty()) {
                core::app.send_message(
                    msg.chat_id,
                    "يرجى اختيار محطة للتشغيل:\n\n" + stations_str + "\nمثال: راديو القرآن الكريم",
                    {}, msg.id
                );
                return true;
            }

            std::string norm_in = normalize_arabic(args);
            std::string target_url, target_name;
            for (const auto& [name, url] : RADIO_STATIONS) {
                if (normalize_arabic(name) == norm_in) {
                    target_url = url;
                    target_name = name;
                    break;
                }
            }

            if (target_url.empty()) {
                core::app.send_message(
                    msg.chat_id,
                    "لم يتم العثور على هذه المحطة.\n\nالمحطات المتاحة:\n" + stations_str,
                    {}, msg.id
                );
                return true;
            }

            int64_t status_id = core::app.send_message(msg.chat_id, "جاري تجهيز البث المباشر...", {}, msg.id);
            std::string lang = utils::database::get_lang(msg.chat_id);
            int64_t target_chat = (cmd == "cradio" || cmd == "راديو القنوات") ? utils::database::get_cmode(msg.chat_id) : msg.chat_id;
            if (target_chat == 0) target_chat = msg.chat_id;

            platforms::TrackDetails radio_td;
            radio_td.title = "إذاعة " + target_name;
            radio_td.link = target_url;
            radio_td.file_path = target_url;
            radio_td.vidid = "telegram";
            radio_td.duration_min = "بث مباشر 📻";
            radio_td.thumb = config::STREAM_IMG_URL;

            try {
                utils::stream::start_stream(
                    lang, status_id, msg.sender_user_id, radio_td,
                    target_chat, msg.sender_first_name.empty() ? "Admin" : msg.sender_first_name,
                    msg.chat_id, false, "index", false
                );
            } catch (const std::exception& e) {
                core::app.edit_message_text(msg.chat_id, status_id, std::string("حدث خطأ أثناء تشغيل الراديو: ") + e.what());
            }
            return true;
        }

        // 4. أوامر قائمة التشغيل (playlist, قائمتي, ماي ليست, delplaylist, حذف قائمتي)
        if (cmd == "playlist" || cmd == "قائمتي" || cmd == "ماي ليست") {
            auto plist = utils::database::get_playlist(msg.sender_user_id);
            if (plist.empty()) {
                core::app.send_message(msg.chat_id, "» قائمة التشغيل الخاصة بك فارغة حالياً.", {}, msg.id);
                return true;
            }
            std::string out = "🎵 <b>قائمة التشغيل الخاصة بك:</b>\n\n";
            int idx = 1;
            for (const auto& [vid, title] : plist) {
                out += std::to_string(idx++) + ". " + title + " (<code>" + vid + "</code>)\n";
            }
            core::app.send_message(msg.chat_id, out, {}, msg.id);
            return true;
        }

        if (cmd == "delplaylist" || cmd == "حذف قائمتي" || cmd == "حذف البلاي ليست") {
            utils::database::delete_playlist(msg.sender_user_id);
            core::app.send_message(msg.chat_id, "✅ تم مسح قائمة التشغيل الخاصة بك بنجاح.", {}, msg.id);
            return true;
        }

        // 5. channelplay & playmode
        if (cmd == "channelplay") {
            if (!core::app.is_admin(msg.chat_id, msg.sender_user_id)) return true;
            if (args == "disable") {
                utils::database::set_cmode(msg.chat_id, 0);
                core::app.send_message(msg.chat_id, "✅ تم تعطيل التشغيل في القنوات.", {}, msg.id);
            } else if (!args.empty()) {
                try {
                    int64_t cid = std::stoll(args);
                    utils::database::set_cmode(msg.chat_id, cid);
                    core::app.send_message(msg.chat_id, "✅ تم ربط القناة بنجاح: <code>" + std::to_string(cid) + "</code>", {}, msg.id);
                } catch (...) {
                    core::app.send_message(msg.chat_id, "يرجى إرسال معرف القناة الرقمي، مثال: <code>/channelplay -100123456789</code>", {}, msg.id);
                }
            }
            return true;
        }

        if (cmd == "playmode" || cmd == "mode") {
            std::string lang = utils::database::get_lang(msg.chat_id);
            core::app.send_message(
                msg.chat_id,
                strings::format_str(strings::tr(lang, "playmode_1"), {core::app.name}),
                utils::inline_markups::setting_markup(lang),
                msg.id
            );
            return true;
        }

        return false;
    });
}

} // namespace AnnieXMedia::plugins
