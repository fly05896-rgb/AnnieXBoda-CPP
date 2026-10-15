#pragma once
#include "AnnieXMedia/core/bot.hpp"
#include <cstdint>
#include <string>

namespace AnnieXMedia::utils::inline_markups {

core::InlineKeyboardMarkup stream_markup(const std::string& lang, int64_t chat_id);
core::InlineKeyboardMarkup stream_markup_timer(const std::string& lang, int64_t chat_id, const std::string& played, const std::string& dur);
core::InlineKeyboardMarkup aq_markup(const std::string& lang, int64_t chat_id);
core::InlineKeyboardMarkup close_markup(const std::string& lang);
core::InlineKeyboardMarkup start_panel(const std::string& lang);
core::InlineKeyboardMarkup private_panel(const std::string& lang);
core::InlineKeyboardMarkup help_pannel(const std::string& lang, bool is_callback = false);
core::InlineKeyboardMarkup setting_markup(const std::string& lang);
core::InlineKeyboardMarkup speed_markup(const std::string& lang, int64_t chat_id);

} // namespace AnnieXMedia::utils::inline_markups
