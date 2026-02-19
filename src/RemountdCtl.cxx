#include "sys.h"
#include "RemountdCtl.h"
#include "SocketServer.h"
#include "ScopedFd.h"

#include <sys/wait.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#include <charconv>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace remountd {

RemountdCtl::RemountdCtl(int argc, char* argv[])
{
  Application::initialize(argc, argv);
}

RemountdCtl::~RemountdCtl() = default;

//virtual
bool RemountdCtl::parse_command_line_parameter(std::string_view arg, int /*argc*/, char*[] /*argv*/, int* /*index*/)
{
  return false;
}

void RemountdCtl::print_usage_extra(std::ostream& os) const
{
}

void RemountdCtl::mainloop()
{
  DoutEntering(dc::notice, "RemountdCtl::mainloop()");
}

//virtual
std::u8string RemountdCtl::application_name() const
{
  return u8"remountdctl";
}

} // namespace remountd
