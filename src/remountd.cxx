#include "Options.h"
#include "ScopedFd.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <systemd/sd-daemon.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

constexpr int kListenBacklog             = 32;
volatile sig_atomic_t g_stop_requested   = 0;
constexpr int kSystemdListenFdStart      = SD_LISTEN_FDS_START;

void OnSignal(int /*signum*/)
{
  g_stop_requested = 1;
}

bool IsSocketFd(int fd)
{
  return sd_is_socket_unix(fd, SOCK_STREAM, -1, nullptr, 0) > 0;
}

enum class SystemdSocketState
{
  kNone,
  kListeningFd,
  kError,
};

struct SystemdSocketResult
{
  SystemdSocketState state = SystemdSocketState::kNone;
  int fd                   = -1;
  std::string error;
};

SystemdSocketResult DetectSystemdSocketActivation()
{
  int const listen_fds = sd_listen_fds(0);
  if (listen_fds < 0)
  {
    std::error_code const ec(-listen_fds, std::system_category());
    return {SystemdSocketState::kError, -1, "sd_listen_fds failed: " + ec.message()};
  }
  if (listen_fds == 0)
    return {};

  if (listen_fds > 1)
    return {SystemdSocketState::kError, -1, "Expected exactly one socket from systemd"};

  int const fd = kSystemdListenFdStart;
  if (!IsSocketFd(fd))
    return {SystemdSocketState::kError, -1, "Inherited FD 3 is not a UNIX stream socket"};

  return {SystemdSocketState::kListeningFd, fd, ""};
}

int CreateStandaloneListener(std::string const& socket_path, std::string* error_out)
{
  std::filesystem::path const socket_fs_path(socket_path);
  std::string const socket_native_path = socket_fs_path.string();

  if (socket_native_path.size() >= sizeof(sockaddr_un::sun_path))
  {
    *error_out = "Socket path is too long for AF_UNIX: '" + socket_native_path + "'";
    return -1;
  }

  std::error_code ec;
  bool const exists = std::filesystem::exists(socket_fs_path, ec);
  if (ec)
  {
    *error_out = "Failed to inspect socket path '" + socket_native_path + "': " + ec.message();
    return -1;
  }

  if (exists)
  {
    bool const is_socket = std::filesystem::is_socket(socket_fs_path, ec);
    if (ec)
    {
      *error_out = "Failed to inspect socket path '" + socket_native_path + "': " + ec.message();
      return -1;
    }

    if (!is_socket)
    {
      *error_out = "Path exists and is not a socket: '" + socket_native_path + "'";
      return -1;
    }

    if (!std::filesystem::remove(socket_fs_path, ec) || ec)
    {
      *error_out = "Failed to remove stale socket '" + socket_native_path + "': " + ec.message();
      return -1;
    }
  }

  ScopedFd fd(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (!fd.valid())
  {
    *error_out = "socket(AF_UNIX) failed: " + std::error_code(errno, std::generic_category()).message();
    return -1;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::copy(socket_native_path.begin(), socket_native_path.end(), addr.sun_path);
  addr.sun_path[socket_native_path.size()] = '\0';

  if (bind(fd.get(), reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) != 0)
  {
    *error_out = "bind('" + socket_native_path + "') failed: " + std::error_code(errno, std::generic_category()).message();
    return -1;
  }
  if (listen(fd.get(), kListenBacklog) != 0)
  {
    *error_out = "listen('" + socket_native_path + "') failed: " + std::error_code(errno, std::generic_category()).message();
    return -1;
  }

  return fd.release();
}

void RunEventLoop()
{
  while (!g_stop_requested) { pause(); }
}

}  // namespace

int main(int argc, char* argv[])
{
  Options options;
  if (!options.parse_args(argc, argv))
    return 2;

  struct sigaction sa{};
  sa.sa_handler = OnSignal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);

  if (options.inetd_mode())
  {
    if (!IsSocketFd(STDIN_FILENO))
    {
      std::cerr << "remountd: --inetd was specified but stdin is not a socket\n";
      return 1;
    }
    std::cerr << "remountd skeleton running in --inetd mode (protocol handling not implemented yet)\n";
    RunEventLoop();
    return 0;
  }

  SystemdSocketResult const systemd_socket = DetectSystemdSocketActivation();
  if (systemd_socket.state == SystemdSocketState::kError)
  {
    std::cerr << "remountd: socket activation error: " << systemd_socket.error << "\n";
    return 1;
  }

  ScopedFd listener_fd;
  bool unlink_on_exit = false;
  std::string standalone_socket_path;

  if (systemd_socket.state == SystemdSocketState::kListeningFd)
  {
    listener_fd.reset(systemd_socket.fd);
    std::cerr << "remountd skeleton using systemd-activated listening socket on FD " << kSystemdListenFdStart << "\n";
  }
  else
  {
    std::string socket_path;

    if (!options.get_socket_path(&socket_path))
      return 1;

    std::string error;
    int const fd = CreateStandaloneListener(socket_path, &error);
    if (fd < 0)
    {
      std::cerr << "remountd: " << error << "\n";
      return 1;
    }
    listener_fd.reset(fd);

    standalone_socket_path = std::move(socket_path);
    unlink_on_exit         = true;
    std::cerr << "remountd skeleton listening on " << standalone_socket_path << "\n";
  }

  RunEventLoop();

  if (listener_fd.get() == STDIN_FILENO)
    listener_fd.release();

  if (unlink_on_exit && !standalone_socket_path.empty())
  {
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(standalone_socket_path), ec);
  }

  return 0;
}
