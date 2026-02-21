#include "sys.h"
#include "remountd_error.h"

#include <string>

namespace {

struct remountd_error_category final : std::error_category
{
  char const* name() const noexcept override
  {
    return "remountd";
  }

  std::string message(int error_value) const override
  {
    switch (static_cast<remountd::errc>(error_value))
    {
      case remountd::errc::no_error:
        return "no error";
      case remountd::errc::invalid_argument:
        return "invalid argument";
      case remountd::errc::missing_option_value:
        return "missing option value";
      case remountd::errc::unknown_argument:
        return "unknown argument";
      case remountd::errc::config_open_failed:
        return "config open failed";
      case remountd::errc::no_such_socket:
        return "no such socket";
      case remountd::errc::config_socket_missing:
        return "config socket key missing";
      case remountd::errc::config_socket_empty:
        return "config socket key empty";
      case remountd::errc::socket_path_too_long:
        return "socket path too long";
      case remountd::errc::socket_path_not_socket:
        return "socket path exists but is not a socket";
      case remountd::errc::inetd_stdin_not_socket:
        return "stdin is not a socket in inetd mode";
      case remountd::errc::systemd_invalid_fd_count:
        return "invalid systemd LISTEN_FDS count";
      case remountd::errc::systemd_inherited_fd_not_socket:
        return "inherited systemd file descriptor is not a UNIX stream socket";
      case remountd::errc::application_already_initialized:
        return "application is already initialized";
      case remountd::errc::application_not_initialized:
        return "application is not initialized";
      default:
        return "unknown remountd error " + std::to_string(error_value);
    }
  }
};

remountd_error_category const remountd_category{};

} // namespace

std::error_code remountd::make_error_code(errc code)
{
  return {static_cast<int>(code), remountd_category};
}

void remountd::throw_error(errc code, std::string const& context)
{
  throw std::system_error(make_error_code(code), context);
}
