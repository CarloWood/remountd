#pragma once

#include "Options.h"
#include "ScopedFd.h"

#include <string>

class SocketServer
{
 public:
  enum class Mode
  {
    k_none,
    k_inetd,
    k_systemd,
    k_standalone
  };

 private:
  ScopedFd listener_fd_;
  bool close_listener_on_cleanup_ = true;
  bool unlink_on_cleanup_ = false;
  std::string standalone_socket_path_;
  Mode mode_ = Mode::k_none;

 private:
  void cleanup();
  bool is_socket_fd(int fd) const;
  bool open_inetd(std::string* error_out);
  bool open_systemd(std::string* error_out);
  bool open_standalone(Options const& options, std::string* error_out);
  bool create_standalone_listener(std::string const& socket_path, std::string* error_out);

 public:
  SocketServer() = default;
  ~SocketServer();

  SocketServer(SocketServer const&) = delete;
  SocketServer& operator=(SocketServer const&) = delete;

  bool initialize(Options const& options, std::string* error_out);

  Mode mode() const { return mode_; }
  int listener_fd() const { return listener_fd_.get(); }
};
