#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace strings {

using LangMap = std::unordered_map<std::string, std::string>;

void init_strings(const std::string& langs_dir = "strings/langs");
const LangMap& get_string(const std::string& lang = "ar");
std::string tr(const std::string& lang, const std::string& key);
std::string format_str(std::string tmpl, const std::vector<std::string>& args);

// Helpful Arabic / English command texts
extern const std::unordered_map<std::string, std::string> HELP_SECTIONS_AR;

} // namespace strings
