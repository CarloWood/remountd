#include "sys.h"
#include "SocketServer.h"
#include "Application.h"
#include "remountd_error.h"

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>
#include <systemd/sd-daemon.h>

#include <algorithm>
#include <cerrno>
#include <string_view>
#include <system_error>
#include <utility>
#include <iostream>

#include "debug.h"
#ifdef CWDEBUG
#include "libcwd/buf2str.h"
#endif

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
class NullClient final : public remountd::SocketServer::Client
{
 public:
  // Construct a null client for the given file descriptor.
  explicit NullClient(int fd) : Client(fd)
  {
  }

 protected:
  // Discard one complete message.
  void new_message(std::string_view DEBUG_ONLY(message)) override
  {
    DoutEntering(dc::notice, "NullClient::new_message(\"" << message << "\".");
  }
};

} // namespace

namespace remountd {
SocketServer::Client::Client(int fd) : fd_(fd)
{
  DoutEntering(dc::notice, "SocketServer::Client::Client(" << fd << ") [" << this << "]");
}

SocketServer::Client::~Client()
{
  DoutEntering(dc::notice, "SocketServer::Client::~Client() [" << this << "]");
}

bool SocketServer::Client::handle_readable()
{
  DoutEntering(dc::notice, "SocketServer::Client::handle_readable()");

  char buffer[4096];
  for (;;)
  {
    ssize_t const read_ret = read(fd_.get(), buffer, sizeof(buffer));
    if (read_ret > 0)
    {
      Dout(dc::notice, "Received " << read_ret << " bytes: '" << libcwd::buf2str(buffer, read_ret) << "'");
      for (ssize_t i = 0; i < read_ret; ++i)
      {
        char const byte = buffer[i];
        // Skip a \n if that immediately follows a \r.
        if (saw_carriage_return_ && byte == '\n')
        {
          saw_carriage_return_ = false;
          continue;
        }
        saw_carriage_return_ = byte == '\r';
        if (byte == '\r' || byte == '\n')
        {
          new_message(partial_message_);
          partial_message_.clear();
          continue;
        }

        partial_message_.push_back(byte);
        if (partial_message_.size() >= max_message_length_c)
        {
          syslog(LOG_ERR, "Dropping client fd %d: no newline within %zu characters", fd_.get(), max_message_length_c);
          return false;
        }
      }
      continue;
    }

    if (read_ret == 0)
      return false;

    if (errno == EINTR)
      continue;

    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return true;

    throw std::system_error(errno, std::generic_category(), "read(client_fd) failed");
  }
}

SocketServer::SocketServer(bool inetd_mode) :
    client_factory_(
        [](int fd)
        {
          return std::make_unique<NullClient>(fd);
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
  clients_.clear();

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

void SocketServer::add_fd_to_epoll(int epoll_fd, int fd, uint32_t events)
{
  DoutEntering(dc::notice, "SocketServer::add_fd_to_epoll(" << epoll_fd << ", " << fd << ", " << events << ")");

  epoll_event event{};
  event.events = events;
  event.data.fd = fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) != 0)
    throw std::system_error(errno, std::generic_category(), "epoll_ctl(ADD) failed");
}

void SocketServer::remove_fd_from_epoll(int epoll_fd, int fd)
{
  DoutEntering(dc::notice, "SocketServer::remove_fd_from_epoll(" << epoll_fd << ", " << fd << ")");

  if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == 0)
    return;
  if (errno == ENOENT || errno == EBADF)
    return;
  throw std::system_error(errno, std::generic_category(), "epoll_ctl(DEL) failed");
}

void SocketServer::add_client(int epoll_fd, int client_fd)
{
  DoutEntering(dc::notice, "SocketServer::add_client(" << epoll_fd << ", " << client_fd << ")");

  std::unique_ptr<Client> client = create_client(client_fd);
  add_fd_to_epoll(epoll_fd, client->fd(), EPOLLIN | EPOLLRDHUP);
  clients_.emplace(client->fd(), std::move(client));
}

std::unique_ptr<SocketServer::Client> SocketServer::create_client(int client_fd)
{
  DoutEntering(dc::notice, "SocketServer::create_client(" << client_fd << ")");

  if (!client_factory_)
    throw std::system_error(EINVAL, std::generic_category(), "client factory is not configured");

  std::unique_ptr<Client> client = client_factory_(client_fd);
  if (!client)
    throw std::system_error(EINVAL, std::generic_category(), "client factory returned null");

  if (client->fd() != client_fd)
    throw std::system_error(EINVAL, std::generic_category(), "client factory returned mismatched file descriptor");

  return client;
}

void SocketServer::remove_client(int epoll_fd, int client_fd)
{
  DoutEntering(dc::notice, "SocketServer::remove_client(" << epoll_fd << ", " << client_fd << ")");

  auto const iter = clients_.find(client_fd);
  if (iter == clients_.end())
    return;

  remove_fd_from_epoll(epoll_fd, client_fd);
  clients_.erase(iter);
}

void SocketServer::accept_new_clients(int epoll_fd)
{
  for (;;)
  {
    int const client_fd = accept4(listener_fd_.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd >= 0)
    {
      add_client(epoll_fd, client_fd);
      continue;
    }

    if (errno == EINTR)
      continue;

    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return;

    throw std::system_error(errno, std::generic_category(), "accept4 failed");
  }
}

void SocketServer::handle_client_readable(int epoll_fd, int client_fd)
{
  DoutEntering(dc::notice, "SocketServer::handle_client_readable(" << epoll_fd << ", " << client_fd << ")");

  auto iter = clients_.find(client_fd);
  if (iter == clients_.end())
    return;

  bool const keep_client = iter->second->handle_readable();
  if (!keep_client)
    remove_client(epoll_fd, client_fd);
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

  ScopedFd epoll_fd(epoll_create1(EPOLL_CLOEXEC));
  if (!epoll_fd.valid())
    throw std::system_error(errno, std::generic_category(), "epoll_create1 failed");

  add_fd_to_epoll(epoll_fd.get(), terminate_fd, EPOLLIN);

  if (mode_ == Mode::k_inetd)
  {
    int const client_fd = listener_fd_.release();
    close_listener_on_cleanup_ = true;
    add_client(epoll_fd.get(), client_fd);
  }
  else
    add_fd_to_epoll(epoll_fd.get(), listener_fd_.get(), EPOLLIN);

  epoll_event events[32];
  for (;;)
  {
    int const event_count = epoll_wait(epoll_fd.get(), events, 32, -1);
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
          accept_new_clients(epoll_fd.get());
        continue;
      }

      if ((epoll_events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0)
      {
        remove_client(epoll_fd.get(), fd);
      }
      else if ((epoll_events & EPOLLIN) != 0)
        handle_client_readable(epoll_fd.get(), fd);
    }

    if (mode_ == Mode::k_inetd && clients_.empty())
      return;
  }
}

} // namespace remountd
