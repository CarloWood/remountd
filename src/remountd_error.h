#pragma once

#include <string>
#include <system_error>
#include <type_traits>

namespace remountd {

enum class errc
{
  help_requested = 1,
  invalid_argument,
  missing_option_value,
  unknown_argument,
  config_open_failed,
  config_socket_missing,
  config_socket_empty,
  socket_path_too_long,
  socket_path_not_socket,
  inetd_stdin_not_socket,
  systemd_invalid_fd_count,
  systemd_inherited_fd_not_socket
};

std::error_code make_error_code(errc code);
[[noreturn]] void throw_error(errc code, std::string const& context);

} // namespace remountd

namespace std {

template<>
struct is_error_code_enum<remountd::errc> : true_type { };

} // namespace std
