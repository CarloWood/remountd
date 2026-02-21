#pragma once

#include <string_view>
#include <vector>
#include <optional>
#include <filesystem>
#include <string>

namespace remountd {

void send_text_to_socket(int fd, std::string_view text);
std::string format_unknown_identifier_error(std::string_view name);
std::vector<std::string_view> split_tokens(std::string_view message);
std::optional<std::filesystem::path> find_allowed_path(std::string_view allowed_name);
void trim_right(std::string* text);
std::string_view trim(std::string_view in);
std::string_view trim_left(std::string_view in);
std::string_view trim_right(std::string_view in);
std::string_view unquote(std::string_view in);
std::string utf8_to_string(std::u8string const& text);

} // namespace remountd
