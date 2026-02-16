#include "Remountd.h"
#include "SocketServer.h"

#include <memory>

namespace remountd {

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

void Remountd::initialize(int argc, char* argv[])
{
  Application::initialize(argc, argv);
  socket_server_ = std::make_unique<SocketServer>(*this, inetd_mode_);
}

//virtual
std::u8string Remountd::application_name() const
{
  return u8"remountd";
}

} // namespace remountd
