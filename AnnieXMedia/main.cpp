// ── 𝚂ᴏᴜʀᴄᴇ ✘ 𝐁ᴏᴅᴀ © 2026 (C++20 Edition - TDLib & Native NTgCalls) ──────────
// Converted from AnnieXMedia/__main__.py & AnnieXMedia/__init__.py
#include "config.hpp"
#include "strings/strings.hpp"
#include "AnnieXMedia/logging.hpp"
#include "AnnieXMedia/misc.hpp"
#include "AnnieXMedia/core/bot.hpp"
#include "AnnieXMedia/core/call.hpp"
#include "AnnieXMedia/core/dir.hpp"
#include "AnnieXMedia/core/git.hpp"
#include "AnnieXMedia/core/mongo.hpp"
#include "AnnieXMedia/core/userbot.hpp"
#include "AnnieXMedia/plugins/plugin_manager.hpp"
#include "AnnieXMedia/utils/cookie_handler.hpp"
#include "AnnieXMedia/utils/database.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>

using namespace AnnieXMedia;

static void signal_handler(int signum) {
    (void)signum;
    LOGGER("AnnieXMedia").info("🛑 Bot process killed by user (SIGINT/SIGTERM). Stopping services...");
    core::app.stop();
    core::userbot.stop();
    std::exit(0);
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (config::getenv_str("DEBUG_NTG", "0") == "1") {
        std::cout << "🚨 تم تفعيل وضع الـ DEBUG لمكتبة ntgcalls C++ بناءً على طلبك من الكونسول 🚨" << std::endl;
    }

    // 1. Load configuration (.env) and language strings
    config::init_config();
    strings::init_strings("strings/langs");

    // 2. Initialize directories, git, MongoDB, in-memory DB, and Heroku checks
    core::StorageManager();
    core::git();
    core::init_mongo();
    dbb();
    heroku();

    LOGGER("AnnieXMedia").info("🚀 Starting Annie Music Bot (C++20 Native TDLib & NTgCalls Engine)...");

    if (config::STRING1.empty() && config::STRING2.empty() && config::STRING3.empty() &&
        config::STRING4.empty() && config::STRING5.empty()) {
        LOGGER("AnnieXMedia").error("❌ Assistant session/phone not filled, please fill STRING_SESSION in .env...");
        return 1;
    }

    // 3. Load YouTube cookies if COOKIE_URL is provided
    try {
        utils::fetch_and_store_cookies();
    } catch (const std::exception& e) {
        LOGGER("AnnieXMedia").warning(std::string("⚠️ Cookie Error: ") + e.what());
    }

    // 4. Load sudoers and banned/gbanned users
    sudo();
    for (auto uid : utils::database::get_gbanned()) {
        config::BANNED_USERS.insert(uid);
    }
    for (auto uid : utils::database::get_banned_users()) {
        config::BANNED_USERS.insert(uid);
    }

    // 5. Start TDLib Bot Client & register all C++ plugins
    core::app.start();
    plugins::register_all_plugins();

    // 6. Start TDLib Assistant Userbots & Native C++ NTgCalls StreamController
    core::userbot.start();
    core::StreamController.start();
    core::StreamController.decorators();

    LOGGER("AnnieXMedia").info("✅ Annie Music Bot (C++20) Started Successfully.");

    // 7. Block until interrupted
    core::app.idle();

    core::app.stop();
    core::userbot.stop();
    LOGGER("AnnieXMedia").info("🛑 Stopping Annie Music Bot...");
    return 0;
}
