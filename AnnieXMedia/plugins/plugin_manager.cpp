#include "AnnieXMedia/plugins/plugin_manager.hpp"
#include "AnnieXMedia/logging.hpp"
#include <algorithm>
#include <sstream>

namespace AnnieXMedia::plugins {

static void replace_all(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}

std::string normalize_arabic(std::string s) {
    replace_all(s, "أ", "ا");
    replace_all(s, "إ", "ا");
    replace_all(s, "آ", "ا");
    replace_all(s, "ة", "ه");
    replace_all(s, "ى", "ي");
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

std::pair<std::string, std::string> parse_command(const std::string& text) {
    if (text.empty()) return {"", ""};
    std::string trimmed = text;
    while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(trimmed.begin());
    if (trimmed.empty()) return {"", ""};

    // Multi-word Arabic commands check first
    static const std::vector<std::string> multi_word_cmds = {
        "قفل البحث", "تعطيل البحث", "فتح البحث", "تفعيل البحث",
        "حذف قائمتي", "حذف البلاي ليست", "راديو القنوات",
        "وقف مؤقت", "توقيف مؤقت", "مين في الكول", "م ف ك",
        "رفع ادمن", "تنزيل ادمن", "قائمة الادمن",
        "رفع الجودة", "ارفع الجودة", "قفل الجودة", "اقفل الجودة",
        "قفل التنزيل", "فتح التنزيل", "قفل كيب البحث", "تفعيل كيب البحث",
        "فتح الكول", "افتح الكول", "قفل الكول", "اقفل الكول",
        "مواعيد الصلاة", "اوقات الصلاة", "تفعيل الاذان", "تعطيل الاذان", "تيست اذان",
        "اذكار الصباح", "اذكار المساء",
        "شد سماح", "شد ميوت", "فك الكتم", "مسح التحذيرات",
        "الغاء التثبيت", "تغيير الصورة", "ضع صورة", "حذف الصورة",
        "تغيير الاسم", "تغيير الوصف", "حذف الكل",
        "دخول المساعد", "خروج المساعد", "مغادرة الكل",
        "تفعيل المغادرة", "شغل المغادرة", "ايقاف المغادرة", "وقف المغادرة", "تعطيل المغادرة",
        "تفعيل الاقتراحات", "تعطيل الاقتراحات", "اعمل اي",
        "كيب اكس او", "تعطيل اكس او", "تفعيل اكس او", "لعبة xo", "يا سورس"
    };

    std::string clean_prefix = trimmed;
    if (!clean_prefix.empty() && (clean_prefix[0] == '/' || clean_prefix[0] == '!' || clean_prefix[0] == '.' || clean_prefix[0] == '#')) {
        clean_prefix.erase(clean_prefix.begin());
    }

    for (const auto& mw : multi_word_cmds) {
        if (clean_prefix.rfind(mw, 0) == 0) {
            std::string rest = clean_prefix.substr(mw.size());
            if (rest.empty() || rest[0] == ' ' || rest[0] == '\n') {
                while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
                return {mw, rest};
            }
        }
    }

    // Single-word command
    auto space_pos = clean_prefix.find_first_of(" \n\t");
    std::string cmd = (space_pos == std::string::npos) ? clean_prefix : clean_prefix.substr(0, space_pos);
    std::string args = (space_pos == std::string::npos) ? "" : clean_prefix.substr(space_pos + 1);
    while (!args.empty() && args.front() == ' ') args.erase(args.begin());

    // Strip @BotUsername from command if present
    auto at_pos = cmd.find('@');
    if (at_pos != std::string::npos) {
        cmd = cmd.substr(0, at_pos);
    }
    return {cmd, args};
}

void register_all_plugins() {
    register_security_plugins();
    register_play_plugins();
    register_admin_plugins();
    register_bot_plugins();
    register_sudo_plugins();
    register_azan_plugins();
    register_manager_plugins();
    register_tools_misc_kishu_plugins();
    register_ai_plugins();
    LOGGER("AnnieXMedia.plugins").info("✅ All C++ Modules Loaded Successfully.");
}

} // namespace AnnieXMedia::plugins
