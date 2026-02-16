#pragma once

#include <optional>
#include <string>

class Options
{
 public:
  static constexpr char const* default_config_path_c = "/etc/remountd/config.yaml";

 public:
  Options(int argc, char* argv[]);

 private:
  std::string config_path_ = default_config_path_c;
  std::optional<std::string> socket_override_;
  bool inetd_mode_ = false;

 private:
  static void print_usage(char const* argv0);
  void parse_args(int argc, char* argv[]);
  std::string parse_socket_path_from_config() const;

 public:
  std::string socket_path() const;

  // Accessors.
  std::string const& config_path() const { return config_path_; }
  std::optional<std::string> const& socket_override() const { return socket_override_; }
  bool inetd_mode() const { return inetd_mode_; }
};
