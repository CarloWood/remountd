#include "sys.h"
#include "Remountd.h"
#include "SocketServer.h"

#include <cerrno>
#include <memory>
#include <string_view>
#include <system_error>

namespace {

// RemountdClient
//
// Concrete client used by remountd. Protocol handling will be added later.
class RemountdClient final : public remountd::SocketServer::Client
{
 public:
  // Construct a remountd client wrapper around a connected socket.
  explicit RemountdClient(int fd) : Client(fd)
  {
    DoutEntering(dc::notice, "RemountdClient::RemountdClient(" << fd << ")");
  }

 protected:
  // Handle one complete newline-terminated message.
  void new_message(std::string_view message) override
  {
    DoutEntering(dc::notice, "RemountdClient::new_message(\"" << message << "\")");
  }
};

} // namespace

namespace remountd {

Remountd::Remountd(int argc, char* argv[])
{
  Application::initialize(argc, argv);
  // The Application base class must be initialized before we can create the SocketServer.
  socket_server_ = std::make_unique<SocketServer>(inetd_mode_);
  socket_server_->set_client_factory(
      [](int client_fd)
      {
        return std::make_unique<RemountdClient>(client_fd);
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
