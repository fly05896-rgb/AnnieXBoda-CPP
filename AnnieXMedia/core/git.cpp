#include "AnnieXMedia/core/git.hpp"
#include "AnnieXMedia/logging.hpp"
#include "config.hpp"
#include <array>
#include <cstdio>
#include <filesystem>

namespace AnnieXMedia::core {

std::tuple<std::string, int> run_shell_command(const std::string& cmd) {
    std::array<char, 4096> buffer{};
    std::string result;
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
    if (!pipe) return {"Failed to spawn shell", -1};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }
    int rc = pclose(pipe);
    return {result, rc};
}

void git() {
    if (std::filesystem::exists(".git")) {
        LOGGER("AnnieXMedia.core.git").info("Git Client Found [VPS DEPLOYER]");
    } else {
        LOGGER("AnnieXMedia.core.git").info("Running in standalone/container deployment mode.");
    }
}

} // namespace AnnieXMedia::core
