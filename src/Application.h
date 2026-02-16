#pragma once

#include <string>
#include <cstdint>

namespace remountd {

class Application
{
 public:
  static Application& instance() { return *s_instance; }

 private:
  static Application* s_instance;       // There can only be one instance of Application. Allow global access.

 public:
  Application();
  ~Application();

 public:
  void initialize(int argc = 0, char** argv = nullptr);

  // Cleans up everything - resulting in the termination of the application.
  void quit();

  // Run the main loop. Returns only after application termination.
  void run();

 protected:
  virtual void parse_command_line_parameters(int argc, char* argv[]);

 public:
  // Override this function to change the default ApplicationInfo values.
  virtual std::u8string application_name() const;

  // Override this function to change the default application version.
  virtual uint32_t application_version() const;
};

} // namespace remountd
