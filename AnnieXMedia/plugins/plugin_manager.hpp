#pragma once
#include "AnnieXMedia/core/bot.hpp"
#include <string>
#include <utility>
#include <vector>

namespace AnnieXMedia::plugins {

// Parse command name and argument string from message text (supports /, !, ., # and prefixless Arabic commands)
std::pair<std::string, std::string> parse_command(const std::string& text);
std::string normalize_arabic(std::string s);

void register_play_plugins();
void register_admin_plugins();
void register_bot_plugins();
void register_sudo_plugins();
void register_azan_plugins();
void register_security_plugins();
void register_ai_plugins();
void register_manager_plugins();
void register_tools_misc_kishu_plugins();

void register_all_plugins();

} // namespace AnnieXMedia::plugins
