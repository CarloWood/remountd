#pragma once

#include "Application.h"
#include "SocketServer.h"

#include <memory>
#include <string_view>

namespace remountd {

class Remountd final : public Application
{
 private:
  bool inetd_mode_ = false;
  std::unique_ptr<SocketServer> socket_server_;

 protected:
  bool parse_command_line_parameter(std::string_view arg, int argc, char* argv[], int* index) override;
  void print_usage_extra(std::ostream& os) const override;
  std::u8string application_name() const override;

 public:
  Remountd() = default;
  ~Remountd();

 public:
  void initialize(int argc, char* argv[]);
};

} // namespace remountd
