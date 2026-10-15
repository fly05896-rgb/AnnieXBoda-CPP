#pragma once
#include <string>

namespace AnnieXMedia::utils {
std::string resolve_raw_cookie_url(const std::string& url);
void fetch_and_store_cookies();
} // namespace AnnieXMedia::utils
