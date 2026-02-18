#pragma once

#include "ScopedFd.h"
#include "ApplicationInfo.h"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace remountd {

// Application
//
// Base class that provides common command line parsing, signal handling,
// termination signaling through a self-pipe, and a non-virtual run() driver.
// Derived classes implement parse_command_line_parameter() for extra CLI options
// and mainloop() for the main event loop.
class Application
{
 public:
  static constexpr char const* default_config_path_c = "/etc/remountd/config.yaml";
  static Application& instance() { return *s_instance_; }

 private:
  static Application* s_instance_;                              // Singleton application instance for signal dispatch.
  static int s_signal_write_fd_;                                // Write-end FD used directly by the async signal handler.
  ApplicationInfo application_info_;                            // Metadata captured during initialize().
  std::filesystem::path config_path_ = default_config_path_c;   // Path of the YAML config file.
  std::optional<std::string> socket_override_;                  // Optional override for socket path from CLI.
  bool initialized_ = false;                                    // True after successful initialize().
  ScopedFd terminate_read_fd_;                                  // Read-end of termination self-pipe.
  ScopedFd terminate_write_fd_;                                 // Write-end of termination self-pipe.

 private:
  // Parse common command line parameters and delegate unknown options to derived class.
  void parse_command_line_parameters(int argc, char* argv[]);

  // Print common usage text and derived-class usage suffix.
  void print_usage(char const* argv0) const;

  // Print application name and decoded version.
  void print_version() const;

  // Read and parse `socket:` from config_path_.
  std::filesystem::path parse_socket_path_from_config() const;

  // Create the self-pipe used to wake epoll/mainloop on termination.
  void create_termination_pipe();

  // Register process signal handlers.
  static void install_signal_handlers();

  // Restore default process signal handlers.
  static void uninstall_signal_handlers();

  // Async signal handler entrypoint; writes to s_signal_write_fd_.
  static void signal_handler(int signum);

  // Write a wakeup byte to a termination pipe FD.
  static void notify_termination_fd(int fd) noexcept;

 protected:
  // Parse a derived-class specific command line parameter.
  virtual bool parse_command_line_parameter(std::string_view arg, int argc, char* argv[], int* index);

  // Print derived-class specific usage suffix.
  virtual void print_usage_extra(std::ostream& os) const;

  // Return file descriptor that becomes readable when termination is requested.
  int termination_fd() const { return terminate_read_fd_.get(); }

  // Derived class event loop implementation.
  virtual void mainloop() = 0;

 public:
  // Construct a default, uninitialized application.
  Application();

  // Unregister signal handlers and release application singleton.
  virtual ~Application();

  // Parse CLI, initialize metadata, create termination pipe, and install signals.
  void initialize(int argc = 0, char** argv = nullptr);

  // Run the application mainloop until it returns.
  void run();

  // Request application termination by waking termination_fd().
  void quit();

  // Resolve configured socket path from override or config file.
  std::filesystem::path socket_path() const;

  // Access configured config file path.
  std::filesystem::path const& config_path() const { return config_path_; }

  // Access configured socket path override.
  std::optional<std::string> const& socket_override() const { return socket_override_; }

  // Return application display name.
  virtual std::u8string application_name() const = 0;

  // Return encoded application version.
  virtual uint32_t application_version() const;
};

} // namespace remountd
