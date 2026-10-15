#include "AnnieXMedia/utils/inline.hpp"
#include "config.hpp"
#include "strings/strings.hpp"

namespace AnnieXMedia::utils::inline_markups {

core::InlineKeyboardMarkup stream_markup(const std::string& lang, int64_t chat_id) {
    std::string cid = std::to_string(chat_id);
    return {
        {
            {"▷", "ADMIN Resume|" + cid, "", ""},
            {"II", "ADMIN Pause|" + cid, "", ""},
            {"↻", "ADMIN Replay|" + cid, "", ""},
            {"‣‣I", "ADMIN Skip|" + cid, "", ""},
            {"▢", "ADMIN Stop|" + cid, "", ""}
        },
        {
            {strings::tr(lang, "CLOSE_BUTTON"), "close", "", ""}
        }
    };
}

core::InlineKeyboardMarkup stream_markup_timer(const std::string& lang, int64_t chat_id, const std::string& played, const std::string& dur) {
    std::string cid = std::to_string(chat_id);
    return {
        {
            {played + " ━━━━━━━━●──── " + dur, "GetTimer", "", ""}
        },
        {
            {"▷", "ADMIN Resume|" + cid, "", ""},
            {"II", "ADMIN Pause|" + cid, "", ""},
            {"↻", "ADMIN Replay|" + cid, "", ""},
            {"‣‣I", "ADMIN Skip|" + cid, "", ""},
            {"▢", "ADMIN Stop|" + cid, "", ""}
        },
        {
            {strings::tr(lang, "CLOSE_BUTTON"), "close", "", ""}
        }
    };
}

core::InlineKeyboardMarkup aq_markup(const std::string& lang, int64_t chat_id) {
    (void)chat_id;
    return {
        {
            {strings::tr(lang, "CLOSE_BUTTON"), "close", "", ""}
        }
    };
}

core::InlineKeyboardMarkup close_markup(const std::string& lang) {
    return {
        {
            {strings::tr(lang, "CLOSE_BUTTON"), "close", "", ""}
        }
    };
}

core::InlineKeyboardMarkup start_panel(const std::string& lang) {
    return {
        {
            {strings::tr(lang, "S_B_1"), "", "https://t.me/" + core::app.username + "?startgroup=true", ""}
        },
        {
            {strings::tr(lang, "S_B_2"), "", config::SUPPORT_CHAT, ""},
            {strings::tr(lang, "S_B_4"), "", config::SUPPORT_CHANNEL, ""}
        }
    };
}

core::InlineKeyboardMarkup private_panel(const std::string& lang) {
    return {
        {
            {strings::tr(lang, "S_B_3"), "", "https://t.me/" + core::app.username + "?startgroup=true", ""}
        },
        {
            {strings::tr(lang, "S_B_4"), "settings_back_helper", "", ""}
        },
        {
            {strings::tr(lang, "S_B_2"), "", config::SUPPORT_CHAT, ""},
            {strings::tr(lang, "S_B_6"), "", config::SUPPORT_CHANNEL, ""}
        },
        {
            {strings::tr(lang, "S_B_5"), "", "https://t.me/" + config::OWNER_USERNAME, ""}
        }
    };
}

core::InlineKeyboardMarkup help_pannel(const std::string& lang, bool is_callback) {
    (void)is_callback;
    return {
        {
            {"أوامر المشرفين 🎛", "help_callback hb1", "", ""},
            {"أوامر المفوضين 🔐", "help_callback hb2", "", ""}
        },
        {
            {"أوامر المطورين 📢", "help_callback hb3", "", ""},
            {"أوامر التشغيل 🎧", "help_callback hb4", "", ""}
        },
        {
            {"الأذان والحماية والـ AI 🕌", "help_callback hb5", "", ""}
        },
        {
            {strings::tr(lang, "CLOSE_BUTTON"), "close", "", ""}
        }
    };
}

core::InlineKeyboardMarkup setting_markup(const std::string& lang) {
    return {
        {
            {strings::tr(lang, "ST_B_1"), "AU", "", ""},
            {strings::tr(lang, "ST_B_3"), "LG", "", ""}
        },
        {
            {strings::tr(lang, "ST_B_2"), "PM", "", ""},
            {strings::tr(lang, "ST_B_4"), "VM", "", ""}
        },
        {
            {strings::tr(lang, "CLOSE_BUTTON"), "close", "", ""}
        }
    };
}

core::InlineKeyboardMarkup speed_markup(const std::string& lang, int64_t chat_id) {
    std::string cid = std::to_string(chat_id);
    return {
        {
            {"🕒 0.5x", "SpeedUP " + cid + "|0.5", "", ""},
            {"🕓 0.75x", "SpeedUP " + cid + "|0.75", "", ""}
        },
        {
            {"🕤 1.0x (طبيعي)", "SpeedUP " + cid + "|1.0", "", ""}
        },
        {
            {"🕤 1.5x", "SpeedUP " + cid + "|1.5", "", ""},
            {"🕛 2.0x", "SpeedUP " + cid + "|2.0", "", ""}
        },
        {
            {strings::tr(lang, "CLOSE_BUTTON"), "close", "", ""}
        }
    };
}

} // namespace AnnieXMedia::utils::inline_markups
