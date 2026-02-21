#include "sys.h"
#include "RemountCtl.h"
#include "ScopedFd.h"
#include "remountd_error.h"
#include "utils.h"

#include <sys/socket.h>
#include <unistd.h>
#include <sys/un.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "debug.h"

namespace remountd {

namespace {

constexpr std::size_t k_max_reply_length = 4096;

ScopedFd connect_unix_socket(std::filesystem::path const& socket_fs_path)
{
  std::string const socket_native_path = socket_fs_path.string();
  if (socket_native_path.size() >= sizeof(sockaddr_un::sun_path))
    throw_error(errc::socket_path_too_long, "socket path is too long for AF_UNIX: '" + socket_native_path + "'");

  ScopedFd fd(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (!fd.valid())
    throw std::system_error(errno, std::generic_category(), "socket(AF_UNIX) failed");

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::copy(socket_native_path.begin(), socket_native_path.end(), addr.sun_path);
  addr.sun_path[socket_native_path.size()] = '\0';

  if (connect(fd.get(), reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) != 0)
    throw std::system_error(errno, std::generic_category(), "connect('" + socket_native_path + "') failed");

  return fd;
}

std::string receive_reply_line(int fd)
{
  std::string reply;
  bool saw_carriage_return = false;
  char buffer[512];
  for (;;)
  {
    ssize_t const read_ret = read(fd, buffer, sizeof(buffer));
    if (read_ret > 0)
    {
      for (ssize_t i = 0; i < read_ret; ++i)
      {
        char const byte = buffer[i];
        // Skip a \n if that immediately follows a \r.
        if (saw_carriage_return && byte == '\n')
        {
          saw_carriage_return = false;
          continue;
        }
        saw_carriage_return = byte == '\r';

        if (byte == '\r')
        {
          reply.push_back('\n');
          return reply;
        }

        reply.push_back(byte);
        if (byte == '\n')
          return reply;

        if (reply.size() >= k_max_reply_length)
          throw std::system_error(EMSGSIZE, std::generic_category(), "reply line too long");
      }
      continue;
    }

    if (read_ret == 0)
      return reply;

    if (errno == EINTR)
      continue;

    throw std::system_error(errno, std::generic_category(), "read(socket) failed");
  }
}

} // namespace

RemountCtl::RemountCtl(int argc, char* argv[])
{
  Application::initialize(argc, argv);
}

RemountCtl::~RemountCtl() = default;

//virtual
bool RemountCtl::parse_command_line_parameter(std::string_view arg, int /*argc*/, char*[] /*argv*/, int* /*index*/)
{
  if (!arg.empty() && arg[0] == '-')
    return false;

  positional_args_.push_back(std::string(arg));
  return true;
}

void RemountCtl::print_usage_extra(std::ostream& os) const
{
  os << " <command...>";
}

void RemountCtl::mainloop()
{
  DoutEntering(dc::notice, "RemountCtl::mainloop()");

  exit_code_ = 0;

  if (positional_args_.empty())
  {
    std::cerr << "ERROR: missing command.\n";
    exit_code_ = 1;
    return;
  }

  // Special-case: "ro|rw <name>" gets the PID appended when <name> is valid.
  if (positional_args_.size() == 2 && (positional_args_[0] == "ro" || positional_args_[0] == "rw"))
  {
    std::string_view const name(positional_args_[1]);
    if (!find_allowed_path(name).has_value())
    {
      std::cerr << format_unknown_identifier_error(name);
      exit_code_ = 1;
      return;
    }

    positional_args_.push_back(std::to_string(getpid()));
  }

  std::string message;
  for (std::size_t i = 0; i < positional_args_.size(); ++i)
  {
    if (i > 0)
      message.push_back(' ');
    message += positional_args_[i];
  }
  message.push_back('\n');

  ScopedFd fd = connect_unix_socket(socket_path());
  send_text_to_socket(fd.get(), message);

  std::string const reply = receive_reply_line(fd.get());
  if (reply == "OK\n")
    return;

  std::cerr << reply;
  exit_code_ = 1;
}

//virtual
std::u8string RemountCtl::application_name() const
{
  return u8"remountctl";
}

} // namespace remountd
