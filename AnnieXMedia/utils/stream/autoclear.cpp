#include "AnnieXMedia/utils/stream/autoclear.hpp"
#include "AnnieXMedia/misc.hpp"
#include <algorithm>
#include <filesystem>

namespace AnnieXMedia::utils::stream {

void auto_clean(const config::QueueItem& popped) {
    std::string file = popped.file;
    if (file.empty() || file.rfind("http", 0) == 0 || file.rfind("vid_", 0) == 0) return;

    int count = 0;
    {
        std::lock_guard<std::mutex> lock(AnnieXMedia::db_mutex);
        for (const auto& [cid, q] : AnnieXMedia::db) {
            for (const auto& item : q) {
                if (item.file == file) ++count;
            }
        }
    }
    if (count == 0) {
        try {
            if (std::filesystem::exists(file)) {
                std::filesystem::remove(file);
            }
        } catch (...) {}
    }
}

} // namespace AnnieXMedia::utils::stream
