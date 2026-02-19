#pragma once

#include "SocketClient.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace remountd {

// SocketServer
//
// Encapsulates socket setup and runtime I/O multiplexing for remountd.
// The server supports inetd mode (already connected socket) and listener
// mode (standalone or systemd-activated listening socket). Runtime I/O
// is handled by mainloop() using a single epoll instance.
class SocketServer
{
 public:
  using client_factory_type = std::function<std::unique_ptr<SocketClient>(SocketServer&, int)>;   // Creates one client object for an accepted fd.

 public:
  // Runtime mode selected during initialization.
  enum class Mode
  {
    k_none,
    k_inetd,
    k_systemd,
    k_standalone
  };

 private:
  ScopedFd listener_fd_;                                                // Listener socket (or connected inetd socket) initialized by initialize().
  ScopedFd epoll_fd_;                                                   // epoll instance used by mainloop.
  Mode mode_ = Mode::k_none;                                            // Active socket server mode.
  bool close_listener_on_cleanup_ = true;                               // Close listener_fd_ when cleanup() is called.
  std::filesystem::path standalone_socket_path_;                        // Path to standalone socket file for cleanup.
  bool unlink_on_cleanup_ = false;                                      // Remove standalone_socket_path_ during cleanup().
  client_factory_type client_factory_;                                  // Factory that creates concrete SocketClient instances for accepted fds.
  std::unordered_map<int, std::unique_ptr<SocketClient>> clients_;      // Active clients keyed by file descriptor.

 private:
  // Release all runtime resources and restore default state.
  void cleanup();

  // Return true if `fd` is a UNIX stream socket.
  bool is_socket_fd(int fd) const;

  // Configure inetd mode using stdin as connected client socket.
  void open_inetd();

  // Configure systemd socket activation; returns false if no activation socket exists.
  bool open_systemd();

  // Configure standalone listening socket from Application configuration.
  void open_standalone();

  // Create, bind, and listen on a standalone UNIX socket path.
  void create_standalone_listener(std::filesystem::path const& socket_fs_path);

  // Initialize socket mode and base listener file descriptor.
  void initialize(bool inetd_mode);

  // Add `fd` to epoll with given event mask.
  void add_fd_to_epoll(int fd, uint32_t events);

  // Remove `fd` from epoll.
  void remove_fd_from_epoll(int fd);

  // Accept all currently pending connections from listener_fd_.
  void accept_new_clients();

  // Register a newly accepted client with epoll and client map.
  void add_client(int client_fd);

  // Construct one concrete client object for the given connected fd.
  std::unique_ptr<SocketClient> create_client(int client_fd);

  // Disconnect client and erase it from client map.
  void remove_client(int client_fd);

  // Invoke readable handler for a client and remove it if connection is closed.
  void handle_client_readable(int client_fd);

  // Drain all bytes currently available from termination fd.
  void drain_termination_fd(int terminate_fd);

 public:
  // Construct and initialize SocketServer.
  SocketServer(bool inetd_mode);

  // Destroy SocketServer and cleanup resources.
  ~SocketServer();

  SocketServer(SocketServer const&) = delete;
  SocketServer& operator=(SocketServer const&) = delete;

  // Set or replace the concrete client factory.
  void set_client_factory(client_factory_type client_factory);

  // Run epoll loop until termination fd becomes readable.
  void mainloop(int terminate_fd);

  // Return current mode.
  Mode mode() const { return mode_; }

  // Return listener or inetd-connected FD used at initialization.
  int listener_fd() const { return listener_fd_.get(); }
};

} // namespace remountd
