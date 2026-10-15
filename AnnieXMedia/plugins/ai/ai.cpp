// Authored By Certified Coders © 2026
// Converted from AnnieXMedia/plugins/ai/* to C++20
#include "AnnieXMedia/plugins/plugin_manager.hpp"
#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/core/git.hpp"
#include "AnnieXMedia/misc.hpp"
#include "config.hpp"

#include <atomic>
#include <sstream>

namespace AnnieXMedia::plugins {

static std::atomic<bool> ai_enabled{true};

static std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::uppercase << std::setw(2) << int(c) << std::nouppercase;
        }
    }
    return escaped.str();
}

static std::string ask_ai_engine(const std::string& prompt) {
    if (!ai_enabled.load()) return "خدمة الذكاء الاصطناعي متوقفة حالياً.";
    std::string system_prompt = "أنت AnnieX، مساعد ذكي مصري متطور، تتحدث باللهجة المصرية الراقية أو الفصحى البسيطة بذكاء واختصار.";
    std::string full_q = url_encode(system_prompt + "\nسؤال المستخدم: " + prompt);
    auto [out, rc] = core::run_shell_command("curl -fsSL -m 15 \"https://text.pollinations.ai/" + full_q + "\"");
    if (rc == 0 && !out.empty()) {
        return out;
    }
    return "أهلاً بيك يا صديقي! أنا معاك وجاهز لأي سؤال.";
}

void register_ai_plugins() {
    core::app.on_message([](const core::IncomingMessage& msg) -> bool {
        if (config::BANNED_USERS.count(msg.sender_user_id)) return false;
        auto [cmd, args] = parse_command(msg.text);

        if (cmd == "ai_on" && is_sudo(msg.sender_user_id)) {
            ai_enabled.store(true);
            core::app.send_message(msg.chat_id, "🤖 تم تفعيل محرك الذكاء الاصطناعي (AnnieX AI).", {}, msg.id);
            return true;
        }
        if (cmd == "ai_off" && is_sudo(msg.sender_user_id)) {
            ai_enabled.store(false);
            core::app.send_message(msg.chat_id, "🔕 تم إيقاف محرك الذكاء الاصطناعي.", {}, msg.id);
            return true;
        }

        if (cmd == "ai" || cmd == "سؤال" || cmd == "ذكاء" || cmd == "انني") {
            if (args.empty()) {
                core::app.send_message(msg.chat_id, "🤖 اكتب سؤالك بعد الأمر، مثال: <code>سؤال اشرحلي لغة C++</code>", {}, msg.id);
                return true;
            }
            int64_t wait_id = core::app.send_message(msg.chat_id, "💭 جاري التفكير...", {}, msg.id);
            std::string reply = ask_ai_engine(args);
            core::app.edit_message_text(msg.chat_id, wait_id, reply);
            return true;
        }
        return false;
    });
}

} // namespace AnnieXMedia::plugins
