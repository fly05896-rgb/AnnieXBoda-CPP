#pragma once

#include <string>
#include <tuple>

namespace AnnieXMedia::core {

std::tuple<std::string, int> run_shell_command(const std::string& cmd);
void git();

} // namespace AnnieXMedia::core
