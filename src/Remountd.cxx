#include "sys.h"
#include "Remountd.h"
#include "SocketServer.h"

#include <sys/socket.h>
#include <syslog.h>

#include <cerrno>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

namespace remountd {
namespace {

// Send text to a connected client socket.
void send_text_to_client(int fd, std::string_view text)
{
  std::size_t sent_total = 0;
  while (sent_total < text.size())
  {
    ssize_t const sent = send(fd, text.data() + sent_total, text.size() - sent_total, MSG_NOSIGNAL);
    if (sent > 0)
    {
      sent_total += static_cast<std::size_t>(sent);
      continue;
    }

    if (sent < 0 && errno == EINTR)
      continue;

    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
      syslog(LOG_WARNING, "Partial reply sent to client fd %d", fd);
      return;
    }

    if (sent < 0)
      syslog(LOG_ERR, "send failed for client fd %d: %m", fd);
    return;
  }
}

// Remountd:Client
//
// Concrete client used by remountd. Protocol handling will be added later.
class RemountdClient final : public SocketClient
{
 public:
  // Construct a remountd client wrapper around a connected socket.
  RemountdClient(SocketServer& socket_server, int fd) : SocketClient(socket_server, fd)
  {
    DoutEntering(dc::notice, "RemountdClient::RemountdClient(" << fd << ")");
  }

 protected:
  // Handle one complete newline-terminated message.
  bool new_message(std::string_view message) override
  {
    DoutEntering(dc::notice, "RemountdClient::new_message(\"" << message << "\")");

    if (message == "quit")
      return false;

    if (message == "list")
    {
      std::string const reply = Application::instance().format_allowed_mount_points(false);
      send_text_to_client(fd(), reply);
    }

    return true;
  }
};

} // namespace

Remountd::Remountd(int argc, char* argv[])
{
  Application::initialize(argc, argv);
  // The Application base class must be initialized before we can create the SocketServer.
  socket_server_ = std::make_unique<SocketServer>(inetd_mode_);
  socket_server_->set_client_factory(
      [](SocketServer& socket_server, int client_fd)
      {
        return std::make_unique<RemountdClient>(socket_server, client_fd);
      });
}

Remountd::~Remountd() = default;

//virtual
bool Remountd::parse_command_line_parameter(std::string_view arg, int /*argc*/, char*[] /*argv*/, int* /*index*/)
{
  if (arg == "--inetd")
  {
    inetd_mode_ = true;
    return true;
  }

  return false;
}

void Remountd::print_usage_extra(std::ostream& os) const
{
  os << " [--inetd]";
}

void Remountd::mainloop()
{
  DoutEntering(dc::notice, "Remountd::mainloop()");

  if (!socket_server_)
    throw std::system_error(EINVAL, std::generic_category(), "socket server is not initialized");

  socket_server_->mainloop(termination_fd());
}

//virtual
std::u8string Remountd::application_name() const
{
  return u8"remountd";
}

} // namespace remountd
