#include "SocketServer.h"
#include "remountd_error.h"

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

namespace remountd {

SocketServer::SocketServer(Application const& application, bool inetd_mode)
{
  initialize(application, inetd_mode);
}

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

void SocketServer::open_inetd()
{
  if (!is_socket_fd(STDIN_FILENO))
    throw_error(errc::inetd_stdin_not_socket, "--inetd was specified but stdin is not a socket");

  listener_fd_.reset(STDIN_FILENO);
  close_listener_on_cleanup_ = false;
  mode_ = Mode::k_inetd;
}

bool SocketServer::open_systemd()
{
  int const listen_fds = sd_listen_fds(0);
  if (listen_fds < 0)
    throw std::system_error(-listen_fds, std::system_category(), "sd_listen_fds failed");

  if (listen_fds == 0)
    return false;

  if (listen_fds > 1)
    throw_error(errc::systemd_invalid_fd_count, "expected exactly one socket from systemd");

  int const fd = k_systemd_listen_fd_start;
  if (!is_socket_fd(fd))
    throw_error(errc::systemd_inherited_fd_not_socket, "inherited FD " + std::to_string(fd) + " is not a UNIX stream socket");

  listener_fd_.reset(fd);
  mode_ = Mode::k_systemd;
  std::cerr << "remountd skeleton using systemd-activated listening socket on FD " << k_systemd_listen_fd_start << "\n";
  return true;
}

void SocketServer::create_standalone_listener(std::string const& socket_path)
{
  std::filesystem::path const socket_fs_path(socket_path);
  std::string const socket_native_path = socket_fs_path.string();

  if (socket_native_path.size() >= sizeof(sockaddr_un::sun_path))
    throw_error(errc::socket_path_too_long, "socket path is too long for AF_UNIX: '" + socket_native_path + "'");

  std::error_code ec;
  bool const exists = std::filesystem::exists(socket_fs_path, ec);
  if (ec)
    throw std::system_error(ec, "failed to inspect socket path '" + socket_native_path + "'");

  if (exists)
  {
    bool const is_socket = std::filesystem::is_socket(socket_fs_path, ec);
    if (ec)
      throw std::system_error(ec, "failed to inspect socket path '" + socket_native_path + "'");

    if (!is_socket)
      throw_error(errc::socket_path_not_socket, "path exists and is not a socket: '" + socket_native_path + "'");
  }

  ScopedFd fd(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (!fd.valid())
    throw std::system_error(errno, std::generic_category(), "socket(AF_UNIX) failed");

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::copy(socket_native_path.begin(), socket_native_path.end(), addr.sun_path);
  addr.sun_path[socket_native_path.size()] = '\0';

  if (bind(fd.get(), reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) != 0)
  {
    int const bind_errno = errno;
    if (bind_errno == EADDRINUSE)
      throw std::system_error(bind_errno, std::generic_category(), "socket path already exists: '" + socket_native_path + "'");
    throw std::system_error(bind_errno, std::generic_category(), "bind('" + socket_native_path + "') failed");
  }

  if (listen(fd.get(), k_listen_backlog) != 0)
  {
    int const err = errno;
    std::filesystem::remove(socket_fs_path, ec);
    throw std::system_error(err, std::generic_category(), "listen('" + socket_native_path + "') failed");
  }

  listener_fd_.reset(fd.release());
  unlink_on_cleanup_ = true;
  standalone_socket_path_ = socket_native_path;
  mode_ = Mode::k_standalone;
}

void SocketServer::open_standalone(Application const& application)
{
  create_standalone_listener(application.socket_path());
}

void SocketServer::initialize(Application const& application, bool inetd_mode)
{
  cleanup();

  if (inetd_mode)
  {
    open_inetd();
    return;
  }

  if (!open_systemd())
    open_standalone(application);
}

} // namespace remountd
