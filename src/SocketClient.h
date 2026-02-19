#pragma once

#include "ScopedFd.h"
#include <string>
#include <string_view>

namespace remountd {

class SocketServer;

// Client
//
// Represents a connected client socket and receives complete protocol
// messages. Messages are ASCII/UTF-8 text lines terminated by '\n'.
class SocketClient
{
 private:
  static constexpr std::size_t max_message_length_c = 64;     // Maximum number of non-newline characters per message.
  SocketServer& socket_server_;                               // Owning socket server instance.
  ScopedFd fd_;                                               // Owned connected client socket.
  std::string partial_message_;                               // Bytes of the current not-yet-terminated message.
  bool saw_carriage_return_ = false;                          // True if the last character received was a carriage-return ('\r').

 protected:
  // Handle one complete message (without trailing newline).
  // The client should be removed if this returns false.
  virtual bool new_message(std::string_view message) = 0;

 public:
  // Take ownership of the connected client file descriptor.
  SocketClient(SocketServer& socket_server, int fd);

  // Virtual destructor for polymorphic derived clients.
  virtual ~SocketClient();

  SocketClient(SocketClient const&) = delete;
  SocketClient& operator=(SocketClient const&) = delete;

  // Return the owned client file descriptor.
  int fd() const { return fd_.get(); }

  // Cleanly disconnect this client: remove from epoll and close fd.
  void disconnect() noexcept;

  // Consume currently available input data and dispatch complete messages.
  // Returns false when the connection must be closed.
  bool handle_readable();
};

} // namespace remountd
