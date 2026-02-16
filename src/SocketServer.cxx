#include "SocketServer.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <systemd/sd-daemon.h>

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <iostream>
#include <system_error>

namespace {

constexpr int k_listen_backlog = 32;
constexpr int k_systemd_listen_fd_start = SD_LISTEN_FDS_START;

} // namespace

SocketServer::~SocketServer()
{
  cleanup();
}

void SocketServer::cleanup()
{
  if (close_listener_on_cleanup_)
    listener_fd_.reset();
  else
    listener_fd_.release();

  if (unlink_on_cleanup_ && !standalone_socket_path_.empty())
  {
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(standalone_socket_path_), ec);
  }

  close_listener_on_cleanup_ = true;
  unlink_on_cleanup_ = false;
  standalone_socket_path_.clear();
  mode_ = Mode::k_none;
}

bool SocketServer::is_socket_fd(int fd) const
{
  return sd_is_socket_unix(fd, SOCK_STREAM, -1, nullptr, 0) > 0;
}

bool SocketServer::open_inetd(std::string* error_out)
{
  if (!is_socket_fd(STDIN_FILENO))
  {
    *error_out = "--inetd was specified but stdin is not a socket";
    return false;
  }

  listener_fd_.reset(STDIN_FILENO);
  close_listener_on_cleanup_ = false;
  mode_ = Mode::k_inetd;
  std::cerr << "remountd skeleton running in --inetd mode (protocol handling not implemented yet)\n";
  return true;
}

bool SocketServer::open_systemd(std::string* error_out)
{
  int const listen_fds = sd_listen_fds(0);
  if (listen_fds < 0)
  {
    std::error_code const ec(-listen_fds, std::system_category());
    *error_out = "socket activation error: sd_listen_fds failed: " + ec.message();
    return false;
  }

  if (listen_fds == 0)
    return false;

  if (listen_fds > 1)
  {
    *error_out = "socket activation error: expected exactly one socket from systemd";
    return false;
  }

  int const fd = k_systemd_listen_fd_start;
  if (!is_socket_fd(fd))
  {
    *error_out = "socket activation error: inherited FD 3 is not a UNIX stream socket";
    return false;
  }

  listener_fd_.reset(fd);
  mode_ = Mode::k_systemd;
  std::cerr << "remountd skeleton using systemd-activated listening socket on FD " << k_systemd_listen_fd_start << "\n";
  return true;
}

bool SocketServer::create_standalone_listener(std::string const& socket_path, std::string* error_out)
{
  std::filesystem::path const socket_fs_path(socket_path);
  std::string const socket_native_path = socket_fs_path.string();

  if (socket_native_path.size() >= sizeof(sockaddr_un::sun_path))
  {
    *error_out = "Socket path is too long for AF_UNIX: '" + socket_native_path + "'";
    return false;
  }

  std::error_code ec;
  bool const exists = std::filesystem::exists(socket_fs_path, ec);
  if (ec)
  {
    *error_out = "Failed to inspect socket path '" + socket_native_path + "': " + ec.message();
    return false;
  }

  if (exists)
  {
    bool const is_socket = std::filesystem::is_socket(socket_fs_path, ec);
    if (ec)
    {
      *error_out = "Failed to inspect socket path '" + socket_native_path + "': " + ec.message();
      return false;
    }

    if (!is_socket)
    {
      *error_out = "Path exists and is not a socket: '" + socket_native_path + "'";
      return false;
    }

    if (!std::filesystem::remove(socket_fs_path, ec) || ec)
    {
      *error_out = "Failed to remove stale socket '" + socket_native_path + "': " + ec.message();
      return false;
    }
  }

  ScopedFd fd(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (!fd.valid())
  {
    *error_out = "socket(AF_UNIX) failed: " + std::error_code(errno, std::generic_category()).message();
    return false;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::copy(socket_native_path.begin(), socket_native_path.end(), addr.sun_path);
  addr.sun_path[socket_native_path.size()] = '\0';

  if (bind(fd.get(), reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) != 0)
  {
    *error_out = "bind('" + socket_native_path + "') failed: " + std::error_code(errno, std::generic_category()).message();
    return false;
  }

  if (listen(fd.get(), k_listen_backlog) != 0)
  {
    *error_out = "listen('" + socket_native_path + "') failed: " + std::error_code(errno, std::generic_category()).message();
    std::filesystem::remove(socket_fs_path, ec);
    return false;
  }

  listener_fd_.reset(fd.release());
  unlink_on_cleanup_ = true;
  standalone_socket_path_ = socket_native_path;
  mode_ = Mode::k_standalone;
  std::cerr << "remountd skeleton listening on " << standalone_socket_path_ << "\n";
  return true;
}

bool SocketServer::open_standalone(Options const& options, std::string* error_out)
{
  std::string socket_path;
  if (!options.get_socket_path(&socket_path))
    return false;

  return create_standalone_listener(socket_path, error_out);
}

bool SocketServer::initialize(Options const& options, std::string* error_out)
{
  error_out->clear();
  cleanup();

  if (options.inetd_mode())
    return open_inetd(error_out);

  if (open_systemd(error_out))
    return true;

  if (!error_out->empty())
    return false;

  return open_standalone(options, error_out);
}
