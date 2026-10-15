#include "strings/strings.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace strings {

static std::unordered_map<std::string, LangMap> all_languages;
static LangMap fallback_ar;

static std::string unescape_yaml(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char next = s[i + 1];
            if (next == 'n') { out.push_back('\n'); ++i; continue; }
            if (next == 't') { out.push_back('\t'); ++i; continue; }
            if (next == '"') { out.push_back('"'); ++i; continue; }
        }
        out.push_back(s[i]);
    }
    return out;
}

static LangMap parse_yaml_file(const std::string& filepath) {
    LangMap result;
    std::ifstream in(filepath);
    if (!in.is_open()) return result;

    std::string line;
    std::string current_key;
    std::ostringstream multiline_val;
    bool in_multiline = false;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        if (in_multiline) {
            if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
                std::string trimmed = line;
                while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) trimmed.erase(trimmed.begin());
                multiline_val << trimmed << "\n";
                continue;
            } else {
                result[current_key] = unescape_yaml(multiline_val.str());
                in_multiline = false;
            }
        }

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());

        if (val == "|" || val == ">") {
            current_key = key;
            multiline_val.str("");
            multiline_val.clear();
            in_multiline = true;
            continue;
        }

        if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') || (val.front() == '\'' && val.back() == '\''))) {
            val = val.substr(1, val.size() - 2);
        }
        result[key] = unescape_yaml(val);
    }
    if (in_multiline && !current_key.empty()) {
        result[current_key] = unescape_yaml(multiline_val.str());
    }
    return result;
}

void init_strings(const std::string& langs_dir) {
    if (!std::filesystem::exists(langs_dir)) return;
    for (const auto& entry : std::filesystem::directory_iterator(langs_dir)) {
        if (entry.path().extension() == ".yml") {
            std::string code = entry.path().stem().string();
            all_languages[code] = parse_yaml_file(entry.path().string());
        }
    }
    if (all_languages.count("ar")) {
        fallback_ar = all_languages["ar"];
    } else if (all_languages.count("en")) {
        fallback_ar = all_languages["en"];
    }
    // Fill missing keys in other languages from en/ar
    if (all_languages.count("en")) {
        for (auto& [code, lmap] : all_languages) {
            if (code == "en") continue;
            for (const auto& [k, v] : all_languages["en"]) {
                if (!lmap.count(k)) lmap[k] = v;
            }
        }
    }
}

const LangMap& get_string(const std::string& lang) {
    auto it = all_languages.find(lang);
    if (it != all_languages.end()) return it->second;
    auto it_ar = all_languages.find("ar");
    if (it_ar != all_languages.end()) return it_ar->second;
    return fallback_ar;
}

std::string tr(const std::string& lang, const std::string& key) {
    const auto& lmap = get_string(lang);
    auto it = lmap.find(key);
    if (it != lmap.end()) return it->second;
    return key;
}

std::string format_str(std::string tmpl, const std::vector<std::string>& args) {
    for (size_t i = 0; i < args.size(); ++i) {
        std::string token = "{" + std::to_string(i) + "}";
        size_t pos = 0;
        while ((pos = tmpl.find(token, pos)) != std::string::npos) {
            tmpl.replace(pos, token.length(), args[i]);
            pos += args[i].length();
        }
    }
    return tmpl;
}

const std::unordered_map<std::string, std::string> HELP_SECTIONS_AR = {
    {"hb1", "🎵 <b>أوامر المشرفين والتحكم في البث:</b>\n\n"
            "• <code>وقف مؤقت</code> أو <code>/pause</code> : إيقاف البث مؤقتاً\n"
            "• <code>كمل</code> أو <code>/resume</code> : استئناف البث\n"
            "• <code>تخطي</code> أو <code>/skip</code> : تخطي للمقطع التالي\n"
            "• <code>ايقاف</code> أو <code>/stop</code> : إنهاء البث ومغادرة الكول\n"
            "• <code>تكرار</code> أو <code>/loop</code> : تكرار المقطع الحالي\n"
            "• <code>قدم</code> / <code>رجع</code> أو <code>/seek</code> : تقديم أو تأخير البث\n"
            "• <code>السرعة</code> أو <code>/speed</code> : التحكم في سرعة الصوت والفيديو\n"
            "• <code>الصوت</code> أو <code>/volume</code> : تغيير مستوى الصوت (1-200%)"},
    {"hb2", "🔐 <b>أوامر الرتب والمفوضين (Auth):</b>\n\n"
            "• <code>/auth</code> أو <code>رفع ادمن</code> : منح عضو صلاحية التحكم بالمشغل\n"
            "• <code>/unauth</code> أو <code>تنزيل ادمن</code> : سحب صلاحية المشغل\n"
            "• <code>/authusers</code> أو <code>قائمة الادمن</code> : عرض المفوضين"},
    {"hb3", "📢 <b>أوامر الإذاعة والحظر العام (للمطورين):</b>\n\n"
            "• <code>/broadcast</code> : إذاعة شاملة للمجموعات والمستخدمين\n"
            "• <code>/gban</code> و <code>/ungban</code> : حظر وفك حظر عام\n"
            "• <code>/block</code> و <code>/unblock</code> : حظر مستخدم من البوت"},
    {"hb4", "🎧 <b>أوامر التشغيل والتحميل والراديو:</b>\n\n"
            "• <code>تشغيل</code> أو <code>شغل</code> + اسم الأغنية أو الرابط\n"
            "• <code>فيديو</code> أو <code>فيد</code> + اسم الفيديو أو الرابط\n"
            "• <code>راديو</code> + اسم المحطة (القرآن الكريم، نجوم اف ام، نايل اف ام، 9090)\n"
            "• <code>يوت</code> أو <code>/song</code> + الاسم : تحميل صوت أو فيديو مباشرة\n"
            "• <code>قائمتي</code> أو <code>/playlist</code> : إدارة قائمة التشغيل الخاصة بك"},
    {"hb5", "🕌 <b>نظام الأذان والحماية والذكاء الاصطناعي:</b>\n\n"
            "• <code>تفعيل الاذان</code> / <code>مواعيد الصلاة</code> / <code>اذكار</code> / <code>قران</code>\n"
            "• <code>قفل</code> / <code>فتح</code> + (الروابط، الصور، السب، الاباحي، التوجيه، إلخ)\n"
            "• <code>سماح</code> / <code>شد سماح</code> / <code>كتم</code> / <code>فك الكتم</code>\n"
            "• التحدث المباشر مع الذكاء الاصطناعي المصري المتطور"}
};

} // namespace strings
