#pragma once

#include "Application.h"
#include <memory>
#include <string_view>

namespace remountd {

// Forward declaration.
class SocketServer;

// Remountd
//
// Daemon application entrypoint. It adds remountd-specific CLI options
// and delegates the runtime event loop to SocketServer.
class Remountd final : public Application
{
 private:
  bool inetd_mode_ = false;                       // True when running as one-shot inetd/systemd Accept=yes handler.
  std::unique_ptr<SocketServer> socket_server_;   // Socket server that manages listeners, clients, and protocol I/O.

 protected:
  // Parse remountd-specific command line parameters.
  bool parse_command_line_parameter(std::string_view arg, int argc, char* argv[], int* index) override;

  // Print remountd-specific usage suffix.
  void print_usage_extra(std::ostream& os) const override;

  // Return the application display name.
  std::u8string application_name() const override;

  // Run remountd mainloop by delegating to SocketServer.
  void mainloop() override;

 public:
  // Construct and initialize base application state and create SocketServer.
  Remountd(int argc, char* argv[]);

  // Destroy remountd object and owned SocketServer.
  ~Remountd();
};

} // namespace remountd
