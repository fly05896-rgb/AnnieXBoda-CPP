#pragma once
#include "AnnieXMedia/core/bot.hpp"
#include <string>

namespace AnnieXMedia::platforms {
class TeleAPI {
public:
    std::string download_media(const core::IncomingMessage& msg);
};
extern TeleAPI Telegram;
} // namespace AnnieXMedia::platforms
