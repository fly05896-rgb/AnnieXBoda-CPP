// Authored By Certified Coders © 2026
// Converted from AnnieXMedia/plugins/AzanSystem/* to C++20
#include "AnnieXMedia/plugins/plugin_manager.hpp"
#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/core/call.hpp"
#include "AnnieXMedia/core/git.hpp"
#include "AnnieXMedia/core/mongo.hpp"
#include "AnnieXMedia/logging.hpp"
#include "AnnieXMedia/misc.hpp"
#include "AnnieXMedia/platforms/Youtube.hpp"
#include "config.hpp"

#include <chrono>
#include <ctime>
#include <random>
#include <regex>
#include <thread>
#include <vector>

namespace AnnieXMedia::plugins {

static const std::vector<std::string> ATHKAR_GENERAL = {
    "أَصْبَحْنَا وَأَصْبَحَ الْمُلْكُ لِلَّهِ، وَالْحَمْدُ لِلَّهِ، لَا إِلَهَ إِلَّا اللَّهُ وَحْدَهُ لَا شَرِيكَ لَهُ، لَهُ الْمُلْكُ وَلَهُ الْحَمْدُ، وَهُوَ عَلَى كُلِّ شَيْءٍ قَدِيرٌ.",
    "اللَّهُمَّ أَنْتَ رَبِّي لَا إِلَهَ إِلَّا أَنْتَ، خَلَقْتَنِي وَأَنَا عَبْدُكَ، وَأَنَا عَلَى عَهْدِكَ وَوَعْدِكَ مَا اسْتَطَعْتُ، أَعُوذُ بِكَ مِنْ شَرِّ مَا صَنَعْتُ، أَبُوءُ لَكَ بِنِعْمَتِكَ عَلَيَّ، وَأَبُوءُ بِذَنْبِي، فَاغْفِرْ لِي، فَإِنَّهُ لَا يَغْفِرُ الذُّنُوبَ إِلَّا أَنْتَ.",
    "رَضِيتُ بِاللَّهِ رَبًّا، وَبِالْإِسْلَامِ دِينًا، وَبِمُحَمَّدٍ ﷺ نَبِيًّا.",
    "حَسْبِيَ اللَّهُ لَا إِلَهَ إِلَّا هُوَ، عَلَيْهِ تَوَكَّلْتُ، وَهُوَ رَبُّ الْعَرْشِ الْعَظِيمِ.",
    "بِسْمِ اللَّهِ الَّذِي لَا يَضُرُّ مَعَ اسْمِهِ شَيْءٌ فِي الْأَرْضِ وَلَا فِي السَّمَاءِ، وَهُوَ السَّمِيعُ الْعَلِيمُ.",
    "سُبْحَانَ اللَّهِ وَبِحَمْدِهِ، عَدَدَ خَلْقِهِ، وَرِضَا نَفْسِهِ، وَزِنَةَ عَرْشِهِ، وَمِدَادَ كَلِمَاتِهِ.",
    "سُبْحَانَ اللَّهِ، وَالْحَمْدُ لِلَّهِ، وَلَا إِلَهَ إِلَّا اللَّهُ، وَاللَّهُ أَكْبَرُ، وَلَا حَوْلَ وَلَا قُوَّةَ إِلَّا بِاللَّهِ.",
    "اللَّهُمَّ صَلِّ وَسَلِّمْ وَبَارِكْ عَلَى نَبِيِّنَا مُحَمَّدٍ، وَعَلَى آلِهِ وَصَحْبِهِ أَجْمَعِينَ.",
    "رَبَّنَا آتِنَا فِي الدُّنْيَا حَسَنَةً، وَفِي الْآخِرَةِ حَسَنَةً، وَقِنَا عَذَابَ النَّارِ.",
    "سُبْحَانَ اللَّهِ وَبِحَمْدِهِ، سُبْحَانَ اللَّهِ الْعَظِيمِ."
};

static const std::vector<std::string> MORNING_DUAS = {
    "اللهم بك أصبحنا، وبك أمسينا، وبك نحيا، وبك نموت، وإليك النشور 🤍",
    "أصبحنا وأصبح الملك لله، والحمد لله، لا إله إلا الله وحده لا شريك له 💕",
    "اللهم إني أسألك خير هذا اليوم، فتحه، ونصره، ونوره، وبركته، وهداه 🤎",
    "يا حي يا قيوم برحمتك أستغيث، أصلح لي شأني كله ولا تكلني إلى نفسي طرفة عين 🤎",
    "اللهم إني أسألك علماً نافعاً، ورزقاً طيباً، وعملاً متقبلاً 💕"
};

static const std::vector<std::string> QURAN_VERSES = {
    "﴿وَمَا كَانَ اللَّهُ لِيُعْجِزَهُ مِن شَيْءٍ﴾ .💙",
    "﴿لَا يُكَلِّفُ اللَّهُ نَفْسًا إِلَّا وُسْعَهَا ۚ لَهَا مَا كَسَبَتْ وَعَلَيْهَا مَا اكتَسَبَتْ﴾💙",
    "{وَمَا كَانَ اللهُ مُعَذِّبَهُمْ وَهُمْ يَسْتَغْفِرُونَ} 💙 .",
    "﴿ نُصيبُ بِرَحمَتِنا مَن نَشاءُ وَلا نُضيعُ أَجرَ المُحسِنينَ ﴾💙",
    "﴿ لَا تَقْنَطُوا مِنْ رَحْمَةِ اللَّهِ ۚ﴾ .💙",
    "۞ إِنَّمَا يُوفَّى الصَّابِرُونَ أَجْرَهُمْ بِغَيْرِ حِسَابٍ ۞💙",
    "﴿وَإِذَا سَأَلَكَ عِبَادِي عَنِّي فَإِنِّي قَرِيبٌ أُجِيبُ دَعْوَةَ الدَّاعِ إِذَا دَعَانِ﴾ 💙",
    "﴿فَإِنَّ مَعَ الْعُسْرِ يُسْرًا * إِنَّ مَعَ الْعُسْرِ يُسْرًا﴾ 💙",
    "﴿لَا تَحْزَنْ إِنَّ اللَّهَ مَعَنَا﴾ 💙",
    "﴿أَلَا بِذِكْرِ اللَّهِ تَطْمَئِنُّ الْقُلُوبُ﴾ 💙",
    "﴿وَمَا تَوْفِيقِي إِلَّا بِاللَّهِ عَلَيْهِ تَوَكَّلْتُ وَإِلَيْهِ أُنِيبُ﴾ 💙",
    "﴿وَكَانَ فَضْلُ اللَّهِ عَلَيْكَ عَظِيمًا﴾ 💙"
};

static std::string fetch_cairo_prayer_times() {
    auto [out, rc] = core::run_shell_command(
        "curl -fsSL -m 8 \"https://api.aladhan.com/v1/timingsByCity?city=Cairo&country=Egypt&method=5\""
    );
    if (rc != 0 || out.empty()) return "";
    auto extract = [&](const std::string& key) -> std::string {
        std::regex re("\"" + key + "\"\\s*:\\s*\"([0-9]{2}:[0-9]{2})\"");
        std::smatch m;
        if (std::regex_search(out, m, re)) return m[1].str();
        return "--:--";
    };
    return "🕌 <b>مواقيت الصلاة اليوم بتوقيت القاهرة:</b>\n\n"
           "• الفجر: <code>" + extract("Fajr") + "</code>\n"
           "• الظهر: <code>" + extract("Dhuhr") + "</code>\n"
           "• العصر: <code>" + extract("Asr") + "</code>\n"
           "• المغرب: <code>" + extract("Maghrib") + "</code>\n"
           "• العشاء: <code>" + extract("Isha") + "</code>\n";
}

void register_azan_plugins() {
    core::app.on_message([](const core::IncomingMessage& msg) -> bool {
        if (config::BANNED_USERS.count(msg.sender_user_id)) return false;
        auto [cmd, args] = parse_command(msg.text);
        if (cmd.empty()) return false;

        // 1. تفعيل / تعطيل الأذان
        if (cmd == "تفعيل الاذان" && msg.is_group) {
            if (!core::app.is_admin(msg.chat_id, msg.sender_user_id) && !is_sudo(msg.sender_user_id)) {
                core::app.send_message(msg.chat_id, "هذا الأمر للمشرفين فقط.", {}, msg.id);
                return true;
            }
            core::mongodb.collection("azan_final_pro_db")->set_field(std::to_string(msg.chat_id), "azan_active", "1");
            core::app.send_message(msg.chat_id, "🕌 تم تفعيل نظام الأذان والأذكار التلقائي في هذه المجموعة بتوقيت القاهرة.", {}, msg.id);
            return true;
        }

        if (cmd == "تعطيل الاذان" && msg.is_group) {
            if (!core::app.is_admin(msg.chat_id, msg.sender_user_id) && !is_sudo(msg.sender_user_id)) {
                core::app.send_message(msg.chat_id, "هذا الأمر للمشرفين فقط.", {}, msg.id);
                return true;
            }
            core::mongodb.collection("azan_final_pro_db")->set_field(std::to_string(msg.chat_id), "azan_active", "0");
            core::app.send_message(msg.chat_id, "🔕 تم تعطيل خدمة الأذان التلقائي في هذه المجموعة.", {}, msg.id);
            return true;
        }

        // 2. مواقيت الصلاة
        if (cmd == "الصلاة" || cmd == "الصلاه" || cmd == "مواعيد الصلاة" || cmd == "اوقات الصلاة") {
            std::string report = fetch_cairo_prayer_times();
            if (report.empty()) report = "تعذر جلب المواقيت حالياً، حاول مرة أخرى.";
            core::app.send_message(msg.chat_id, report, {}, msg.id);
            return true;
        }

        // 3. تيست اذان (تشغيل الأذان في المكالمة الصوتية عبر C++ NTgCalls)
        if (cmd == "تيست اذان" && msg.is_group) {
            if (!core::app.is_admin(msg.chat_id, msg.sender_user_id) && !is_sudo(msg.sender_user_id)) return true;
            core::app.send_message(msg.chat_id, "🕌 جاري تشغيل الأذان في المكالمة الصوتية...", {}, msg.id);
            auto td = platforms::YouTube.track("اذان عبد الباسط عبد الصمد", false);
            std::string link = platforms::YouTube.get_direct_link(td.link, true);
            try {
                core::StreamController.join_call(msg.chat_id, msg.chat_id, link, false);
            } catch (const std::exception& e) {
                core::app.send_message(msg.chat_id, std::string("❌ تعذر تشغيل الأذان في الكول: ") + e.what(), {}, msg.id);
            }
            return true;
        }

        // 4. الأذكار والأدعية (اذكار، ذكر، دعاء، اذكار الصباح، اذكار المساء)
        if (cmd == "اذكار" || cmd == "ذكر" || cmd == "دعاء") {
            const auto& item = ATHKAR_GENERAL[msg.id % ATHKAR_GENERAL.size()];
            core::app.send_message(msg.chat_id, "📿 " + item, {}, msg.id);
            return true;
        }

        if (cmd == "اذكار الصباح" || cmd == "اذكار المساء") {
            std::string out = "🌅 <b>" + cmd + ":</b>\n\n";
            for (const auto& d : MORNING_DUAS) out += "• " + d + "\n\n";
            core::app.send_message(msg.chat_id, out, {}, msg.id);
            return true;
        }

        // 5. الآيات القرآنية (قران، آية، ايه)
        if (cmd == "قران" || cmd == "قرآن" || cmd == "آية" || cmd == "ايه") {
            const auto& v = QURAN_VERSES[msg.id % QURAN_VERSES.size()];
            core::app.send_message(msg.chat_id, v, {}, msg.id);
            return true;
        }

        return false;
    });
}

} // namespace AnnieXMedia::plugins
