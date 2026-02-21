#pragma once

#include "Application.h"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace remountd {

// RemountCtl
//
// Client-side control utility for remountd.
//
// Parses positional arguments (a command) and sends it to remountd over the
// configured UNIX-domain socket.
class RemountCtl final : public Application
{
 private:
  std::vector<std::string> positional_args_;   // Positional, non-option arguments (the command to send).
  int exit_code_ = 0;                          // Exit code set by mainloop().

 protected:
  // Parse remountctl-specific command line parameters.
  bool parse_command_line_parameter(std::string_view arg, int argc, char* argv[], int* index) override;

  // Print remountctl-specific usage suffix.
  void print_usage_extra(std::ostream& os) const override;

  // Return the application display name.
  std::u8string application_name() const override;

  // Send command and wait for one reply line.
  void mainloop() override;

 public:
  // Construct and initialize base application state and parse command line.
  RemountCtl(int argc, char* argv[]);

  // Destroy application state and uninstall signal handlers.
  ~RemountCtl();

  // Return exit code determined during mainloop().
  int exit_code() const { return exit_code_; }
};

} // namespace remountd
