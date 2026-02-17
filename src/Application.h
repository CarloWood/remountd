#pragma once

#include <csignal>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace remountd {

class ApplicationInfo
{
 private:
  std::u8string application_name_;
  uint32_t encoded_version_ = 0;

 public:
  void set_application_name(std::u8string const& application_name)
  {
    application_name_ = application_name;
  }

  void set_application_version(uint32_t encoded_version)
  {
    encoded_version_ = encoded_version;
  }
};

class Application
{
 public:
  static constexpr char const* default_config_path_c = "/etc/remountd/config.yaml";
  static Application& instance() { return *s_instance_; }

 private:
  static Application* s_instance_;
  ApplicationInfo application_info_;
  std::string config_path_ = default_config_path_c;
  std::optional<std::string> socket_override_;
  bool initialized_ = false;
  volatile sig_atomic_t stop_requested_ = 0;

 private:
  void parse_command_line_parameters(int argc, char* argv[]);
  void print_usage(char const* argv0) const;
  std::string parse_socket_path_from_config() const;

  static void install_signal_handlers();
  static void uninstall_signal_handlers();
  // signal_handler is only be called while `Application` is instantiated,
  // because Application itself registers and unregisters the signal handler.
  static void signal_handler(int signum) { s_instance_->handle_signal(signum); }
  void handle_signal(int signnum) { stop_requested_ = 1; }

 protected:
  virtual bool parse_command_line_parameter(std::string_view arg, int argc, char* argv[], int* index);
  virtual void print_usage_extra(std::ostream& os) const;

 public:
  Application();
  virtual ~Application();

  void initialize(int argc = 0, char** argv = nullptr);
  void run();
  void quit();

  std::string socket_path() const;

  std::string const& config_path() const { return config_path_; }
  std::optional<std::string> const& socket_override() const { return socket_override_; }

  virtual std::u8string application_name() const = 0;
  virtual uint32_t application_version() const;
};

} // namespace remountd
