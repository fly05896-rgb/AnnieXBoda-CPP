#include "AnnieXMedia/core/dir.hpp"
#include "AnnieXMedia/logging.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace AnnieXMedia::core {

std::string BASE_DIR = fs::current_path().string();
std::string DOWNLOAD_DIR = fs::exists("/dev/shm") ? "/dev/shm/AnnieDownloads" : (BASE_DIR + "/downloads");
std::string CACHE_DIR = BASE_DIR + "/cache";
std::string COUPLE_DIR = BASE_DIR + "/couples";
std::string BACKUP_DIR = BASE_DIR + "/AnnieXMediaBackup";

void StorageManager() {
    if (fs::exists("/dev/shm") && DOWNLOAD_DIR.find("AnnieDownloads") != std::string::npos) {
        try {
            if (fs::exists(DOWNLOAD_DIR)) {
                fs::remove_all(DOWNLOAD_DIR);
                LOGGER("AnnieXMedia.core.dir").info("🧹 RAM Disk Cleaned Successfully.");
            }
        } catch (const std::exception& e) {
            LOGGER("AnnieXMedia.core.dir").warning(std::string("Failed to clean RAM Disk: ") + e.what());
        }
    }

    for (const auto& path : {DOWNLOAD_DIR, CACHE_DIR, COUPLE_DIR, BACKUP_DIR}) {
        try {
            fs::create_directories(path);
        } catch (const std::exception& e) {
            LOGGER("AnnieXMedia.core.dir").error(std::string("Failed creating dir ") + path + ": " + e.what());
        }
    }
    LOGGER("AnnieXMedia.core.dir").info("Directories Updated. Downloads pointed to: " + DOWNLOAD_DIR);
}

} // namespace AnnieXMedia::core
