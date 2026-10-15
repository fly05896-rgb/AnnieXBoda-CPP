#include "AnnieXMedia/platforms/Telegram.hpp"

namespace AnnieXMedia::platforms {
TeleAPI Telegram;

std::string TeleAPI::download_media(const core::IncomingMessage& msg) {
    if (!msg.audio_file_id.empty()) return core::app.download_file(msg.audio_file_id);
    if (!msg.video_file_id.empty()) return core::app.download_file(msg.video_file_id);
    if (!msg.voice_file_id.empty()) return core::app.download_file(msg.voice_file_id);
    return "";
}
} // namespace AnnieXMedia::platforms
