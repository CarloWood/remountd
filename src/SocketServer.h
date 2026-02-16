#pragma once

#include "Application.h"
#include "ScopedFd.h"

#include <string>

namespace remountd {

class SocketServer
{
 public:
  SocketServer(Application const& application, bool inetd_mode);
  ~SocketServer();

  SocketServer(SocketServer const&) = delete;
  SocketServer& operator=(SocketServer const&) = delete;

 public:
  enum class Mode
  {
    k_none,
    k_inetd,
    k_systemd,
    k_standalone
  };

 private:
  ScopedFd listener_fd_;                        // The listen socket; initialized by initialize().
  Mode mode_ = Mode::k_none;                    // The mode that the SocketServer is running in.
  bool close_listener_on_cleanup_ = true;       // Close listener_fd_ upon destruction (cleanup).
  std::string standalone_socket_path_;          // Only valid if mode_ == Mode::k_standalone.
  bool unlink_on_cleanup_ = false;              // Remove standalone_socket_path_ upon destruction (cleanup).

 private:
  void cleanup();
  bool is_socket_fd(int fd) const;
  void open_inetd();
  bool open_systemd();
  void open_standalone(Application const& application);
  void create_standalone_listener(std::string const& socket_path);
  void initialize(Application const& application, bool inetd_mode);

 public:
  Mode mode() const { return mode_; }
  int listener_fd() const { return listener_fd_.get(); }
};

} // namespace remountd
