#pragma once

#include <string>
#include <optional>

class Options
{
 public:
  static constexpr char const* default_config_path_c = "/etc/remountd/config.yaml";

 private:
  std::string config_path_ = default_config_path_c;
  std::optional<std::string> socket_override_;
  bool inetd_mode_ = false;

 private:
  std::optional<std::string> parse_socket_path_from_config(std::string* error_out) const;

 public:
  bool parse_args(int argc, char* argv[]);
  bool get_socket_path(std::string* socket_path_out) const;

  // Accessors.
  std::string const& config_path() const { return config_path_; }
  std::optional<std::string> const& socket_override() const { return socket_override_; }
  bool inetd_mode() const { return inetd_mode_; }
};
