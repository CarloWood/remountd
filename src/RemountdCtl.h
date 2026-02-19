#pragma once

#include "Application.h"
#include <memory>
#include <string_view>

namespace remountd {

// RemountdCtl
//
class RemountdCtl final : public Application
{
 private:

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
  RemountdCtl(int argc, char* argv[]);

  // Destroy remountd object and owned SocketServer.
  ~RemountdCtl();
};

} // namespace remountd
