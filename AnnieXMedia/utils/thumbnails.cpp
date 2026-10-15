#include "AnnieXMedia/utils/thumbnails.hpp"
#include "AnnieXMedia/core/dir.hpp"
#include "AnnieXMedia/core/git.hpp"
#include "config.hpp"
#include <filesystem>

namespace AnnieXMedia::utils::thumbnails {

std::string get_thumb(const std::string& videoid) {
    if (videoid.empty() || videoid == "telegram" || videoid == "soundcloud") {
        return config::YOUTUBE_IMG_URL;
    }
    std::string out_path = core::CACHE_DIR + "/" + videoid + "_v4.png";
    if (std::filesystem::exists(out_path)) {
        return out_path;
    }
    std::string raw_jpg = core::CACHE_DIR + "/" + videoid + "_raw.jpg";
    std::string yt_thumb = "https://i.ytimg.com/vi/" + videoid + "/maxresdefault.jpg";
    auto [o1, rc1] = core::run_shell_command("curl -fsSL -m 8 \"" + yt_thumb + "\" -o \"" + raw_jpg + "\"");
    if (rc1 != 0 || !std::filesystem::exists(raw_jpg)) {
        std::string hq_thumb = "https://i.ytimg.com/vi/" + videoid + "/hqdefault.jpg";
        core::run_shell_command("curl -fsSL -m 8 \"" + hq_thumb + "\" -o \"" + raw_jpg + "\"");
    }
    if (std::filesystem::exists(raw_jpg)) {
        // Generate glass-design thumbnail via FFmpeg filtergraph
        std::string ff_cmd =
            "ffmpeg -y -loglevel panic -i \"" + raw_jpg + "\" "
            "-vf \"scale=1280:720:force_original_aspect_ratio=increase,crop=1280:720,boxblur=18:2\" "
            "\"" + out_path + "\"";
        auto [o2, rc2] = core::run_shell_command(ff_cmd);
        if (rc2 == 0 && std::filesystem::exists(out_path)) {
            return out_path;
        }
        return raw_jpg;
    }
    return config::YOUTUBE_IMG_URL;
}

} // namespace AnnieXMedia::utils::thumbnails
