#include "sys.h"
#include "SocketServer.h"
#include "Application.h"
#include "remountd_error.h"

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <systemd/sd-daemon.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

#include "debug.h"
#ifdef CWDEBUG
#include "libcwd/buf2str.h"
#endif

namespace remountd {
namespace {

constexpr int k_listen_backlog = 4;
constexpr int k_systemd_listen_fd_start = SD_LISTEN_FDS_START;

// Set the O_NONBLOCK flag on a file descriptor.
void make_nonblocking(int fd)
{
  int const flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0)
    throw std::system_error(errno, std::generic_category(), "fcntl(F_GETFL) failed");

  if ((flags & O_NONBLOCK) != 0)
    return;

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0)
    throw std::system_error(errno, std::generic_category(), "fcntl(F_SETFL) failed");
}

// NullClient
//
// Default client implementation that silently discards complete messages.
class NullClient final : public SocketClient
{
 public:
  // Construct a null client for the given file descriptor.
  explicit NullClient(SocketServer& socket_server, int fd) : SocketClient(socket_server, fd)
  {
  }

 protected:
  // Discard one complete message.
  bool new_message(std::string_view DEBUG_ONLY(message)) override
  {
    DoutEntering(dc::notice, "NullClient::new_message(\"" << message << "\".");
    return true;
  }
};

} // namespace

SocketServer::SocketServer(bool inetd_mode) :
    client_factory_(
        [](SocketServer& socket_server, int client_fd)
        {
          return std::make_unique<NullClient>(socket_server, client_fd);
        })
{
  initialize(inetd_mode);
}

SocketServer::~SocketServer()
{
  cleanup();
}

void SocketServer::cleanup()
{
  DoutEntering(dc::notice, "SocketServer::cleanup()");

  Dout(dc::notice, "Calling clients_.clear()");
  clients_.clear();
  epoll_fd_.reset();

  if (close_listener_on_cleanup_)
    listener_fd_.reset();
  else
    listener_fd_.release();

  if (unlink_on_cleanup_ && !standalone_socket_path_.empty())
  {
    std::error_code ec;
    std::filesystem::remove(standalone_socket_path_, ec);
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
  DoutEntering(dc::notice, "SocketServer::open_inetd()");

  if (!is_socket_fd(STDIN_FILENO))
    throw_error(errc::inetd_stdin_not_socket, "--inetd was specified but stdin is not a socket");

  make_nonblocking(STDIN_FILENO);
  listener_fd_.reset(STDIN_FILENO);
  close_listener_on_cleanup_ = false;
  mode_ = Mode::k_inetd;
}

bool SocketServer::open_systemd()
{
  DoutEntering(dc::notice, "SocketServer::open_systemd()");

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

  make_nonblocking(fd);
  listener_fd_.reset(fd);
  mode_ = Mode::k_systemd;
  return true;
}

void SocketServer::create_standalone_listener(std::filesystem::path const& socket_fs_path)
{
  DoutEntering(dc::notice, "SocketServer::create_standalone_listener(" << socket_fs_path << ")");

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

  ScopedFd fd(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
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
  standalone_socket_path_ = socket_fs_path;
  mode_ = Mode::k_standalone;
}

void SocketServer::open_standalone()
{
  DoutEntering(dc::notice, "SocketServer::open_standalone()");

  create_standalone_listener(Application::instance().socket_path());
}

void SocketServer::initialize(bool inetd_mode)
{
  cleanup();

  if (inetd_mode)
  {
    open_inetd();
    return;
  }

  if (!open_systemd())
    open_standalone();
}

void SocketServer::add_fd_to_epoll(int fd, uint32_t events)
{
  DoutEntering(dc::notice, "SocketServer::add_fd_to_epoll(" << fd << ", " << events << ")");

  if (!epoll_fd_.valid())
    throw std::system_error(EINVAL, std::generic_category(), "epoll instance is not initialized");

  epoll_event event{};
  event.events = events;
  event.data.fd = fd;
  if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, fd, &event) != 0)
    throw std::system_error(errno, std::generic_category(), "epoll_ctl(ADD) failed");
}

void SocketServer::remove_fd_from_epoll(int fd)
{
  DoutEntering(dc::notice, "SocketServer::remove_fd_from_epoll(" << fd << ")");

  if (!epoll_fd_.valid())
    return;

  if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr) == 0)
    return;
  if (errno == ENOENT || errno == EBADF)
    return;
  throw std::system_error(errno, std::generic_category(), "epoll_ctl(DEL) failed");
}

void SocketServer::add_client(int client_fd)
{
  DoutEntering(dc::notice, "SocketServer::add_client(" << client_fd << ")");

  std::unique_ptr<SocketClient> client = create_client(client_fd);
  add_fd_to_epoll(client_fd, EPOLLIN | EPOLLRDHUP);
  Dout(dc::notice, "Adding client with fd " << client->fd() << " to clients_.");
  clients_.emplace(client->fd(), std::move(client));
}

std::unique_ptr<SocketClient> SocketServer::create_client(int client_fd)
{
  DoutEntering(dc::notice, "SocketServer::create_client(" << client_fd << ")");

  if (!client_factory_)
    throw std::system_error(EINVAL, std::generic_category(), "client factory is not configured");

  std::unique_ptr<SocketClient> client = client_factory_(*this, client_fd);
  if (!client)
    throw std::system_error(EINVAL, std::generic_category(), "client factory returned null");

  if (client->fd() != client_fd)
    throw std::system_error(EINVAL, std::generic_category(), "client factory returned mismatched file descriptor");

  return client;
}

void SocketServer::remove_client(int client_fd)
{
  DoutEntering(dc::notice, "SocketServer::remove_client(" << client_fd << ")");
  auto const iter = clients_.find(client_fd);
  if (iter == clients_.end())
    return;

  remove_fd_from_epoll(client_fd);
  Dout(dc::notice, "Erasing client with fd " << client_fd << " from clients_.");
  clients_.erase(iter);
}

void SocketServer::accept_new_clients()
{
  for (;;)
  {
    int const client_fd = accept4(listener_fd_.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd >= 0)
    {
      add_client(client_fd);
      continue;
    }

    if (errno == EINTR)
      continue;

    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return;

    throw std::system_error(errno, std::generic_category(), "accept4 failed");
  }
}

void SocketServer::handle_client_readable(int client_fd)
{
  //DoutEntering(dc::notice, "SocketServer::handle_client_readable(" << client_fd << ")");

  auto iter = clients_.find(client_fd);
  if (iter == clients_.end())
    return;

  bool const keep_client = iter->second->handle_readable();
  if (!keep_client)
    remove_client(client_fd);
}

void SocketServer::drain_termination_fd(int terminate_fd)
{
  char buffer[128];
  for (;;)
  {
    ssize_t const read_ret = read(terminate_fd, buffer, sizeof(buffer));
    if (read_ret > 0)
      continue;

    if (read_ret == 0)
      return;

    if (errno == EINTR)
      continue;

    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return;

    throw std::system_error(errno, std::generic_category(), "read(terminate_fd) failed");
  }
}

void SocketServer::set_client_factory(client_factory_type client_factory)
{
  if (!client_factory)
    throw std::system_error(EINVAL, std::generic_category(), "client factory is empty");

  client_factory_ = std::move(client_factory);
}

void SocketServer::mainloop(int terminate_fd)
{
  DoutEntering(dc::notice, "SocketServer::mainloop(" << terminate_fd << ")");

  if (terminate_fd < 0)
    throw std::system_error(EINVAL, std::generic_category(), "invalid terminate fd");

  if (epoll_fd_.valid())
    throw std::system_error(EALREADY, std::generic_category(), "mainloop already running");

  epoll_fd_.reset(epoll_create1(EPOLL_CLOEXEC));
  if (!epoll_fd_.valid())
    throw std::system_error(errno, std::generic_category(), "epoll_create1 failed");

  struct epoll_reset_guard
  {
    ScopedFd& epoll_fd_;

    ~epoll_reset_guard()
    {
      epoll_fd_.reset();
    }
  } const reset_guard {epoll_fd_};

  add_fd_to_epoll(terminate_fd, EPOLLIN);

  if (mode_ == Mode::k_inetd)
  {
    int const client_fd = listener_fd_.release();
    close_listener_on_cleanup_ = true;
    add_client(client_fd);
  }
  else
    add_fd_to_epoll(listener_fd_.get(), EPOLLIN);

  epoll_event events[32];
  for (;;)
  {
    int const event_count = epoll_wait(epoll_fd_.get(), events, 32, -1);
    if (event_count < 0)
    {
      if (errno == EINTR)
        continue;
      throw std::system_error(errno, std::generic_category(), "epoll_wait failed");
    }

    for (int i = 0; i < event_count; ++i)
    {
      int const fd = events[i].data.fd;
      uint32_t const epoll_events = events[i].events;

      if (fd == terminate_fd)
      {
        drain_termination_fd(terminate_fd);
        return;
      }

      if (mode_ != Mode::k_inetd && fd == listener_fd_.get())
      {
        if ((epoll_events & EPOLLIN) != 0)
          accept_new_clients();
        continue;
      }

      if ((epoll_events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0)
      {
        remove_client(fd);
      }
      else if ((epoll_events & EPOLLIN) != 0)
        handle_client_readable(fd);
    }

    if (mode_ == Mode::k_inetd && clients_.empty())
      return;
  }
}

} // namespace remountd
