#include "sys.h"
#include "SocketClient.h"
#include "SocketServer.h"
#include <syslog.h>
#include <cerrno>
#include <system_error>
#include "debug.h"

namespace remountd {

SocketClient::SocketClient(SocketServer& socket_server, int fd) : socket_server_(socket_server), fd_(fd)
{
  DoutEntering(dc::notice, "SocketClient::SocketClient(" << fd << ") [" << this << "]");
}

SocketClient::~SocketClient()
{
  DoutEntering(dc::notice, "SocketClient::~SocketClient() [" << this << "]");
}

void SocketClient::disconnect() noexcept
{
  fd_.reset();
}

bool SocketClient::handle_readable()
{
  //DoutEntering(dc::notice, "SocketClient::handle_readable()");

  if (!fd_.valid())
    return false;

  char buffer[4096];
  for (;;)
  {
    ssize_t const read_ret = read(fd_.get(), buffer, sizeof(buffer));
    if (read_ret > 0)
    {
      //Dout(dc::notice, "Received " << read_ret << " bytes: '" << libcwd::buf2str(buffer, read_ret) << "'");
      for (ssize_t i = 0; i < read_ret; ++i)
      {
        char const byte = buffer[i];
        // Skip a \n if that immediately follows a \r.
        if (saw_carriage_return_ && byte == '\n')
        {
          saw_carriage_return_ = false;
          continue;
        }
        saw_carriage_return_ = byte == '\r';
        if (byte == '\r' || byte == '\n')
        {
          if (!new_message(partial_message_))
            return false;
          partial_message_.clear();
          if (!fd_.valid())
            return false;
          continue;
        }

        partial_message_.push_back(byte);
        if (partial_message_.size() >= max_message_length_c)
        {
          syslog(LOG_ERR, "Dropping client fd %d: no newline within %zu characters", fd_.get(), max_message_length_c);
          return false;
        }
      }
      continue;
    }

    if (read_ret == 0)
      return false;

    if (errno == EINTR)
      continue;

    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return true;

    throw std::system_error(errno, std::generic_category(), "read(client_fd) failed");
  }
}

} // namespace remountd
