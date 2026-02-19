#include "sys.h"
#include "utils.h"
#include "Application.h"
#include <syslog.h>
#include <sys/socket.h>

namespace remountd {

// Send text to a connected client socket.
void send_text_to_client(int fd, std::string_view text)
{
  std::size_t sent_total = 0;
  while (sent_total < text.size())
  {
    ssize_t const sent = send(fd, text.data() + sent_total, text.size() - sent_total, MSG_NOSIGNAL);
    if (sent > 0)
    {
      sent_total += static_cast<std::size_t>(sent);
      continue;
    }

    if (sent < 0 && errno == EINTR)
      continue;

    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
      syslog(LOG_WARNING, "Partial reply sent to client fd %d", fd);
      return;
    }

    if (sent < 0)
      syslog(LOG_ERR, "send failed for client fd %d: %m", fd);
    return;
  }
}

// Split one command line into whitespace-separated tokens.
std::vector<std::string_view> split_tokens(std::string_view message)
{
  std::vector<std::string_view> tokens;
  std::size_t position = 0;
  while (position < message.size())
  {
    while (position < message.size() && (message[position] == ' ' || message[position] == '\t'))
      ++position;
    if (position >= message.size())
      break;

    std::size_t token_end = position;
    while (token_end < message.size() && message[token_end] != ' ' && message[token_end] != '\t')
      ++token_end;

    tokens.push_back(message.substr(position, token_end - position));
    position = token_end;
  }

  return tokens;
}

// Find path for allowed identifier.
std::optional<std::filesystem::path> find_allowed_path(std::string_view allowed_name)
{
  for (Application::AllowedMountPoint const& allowed_mount_point : Application::instance().allowed_mount_points())
  {
    if (allowed_mount_point.name_ == allowed_name)
      return allowed_mount_point.path_;
  }

  return std::nullopt;
}

// Trim trailing whitespace/newlines.
void trim_right(std::string* text)
{
  while (!text->empty())
  {
    char const last = text->back();
    if (last != ' ' && last != '\t' && last != '\r' && last != '\n')
      break;
    text->pop_back();
  }
}

std::string_view trim(std::string_view in)
{
  while (!in.empty() && (in.front() == ' ' || in.front() == '\t' || in.front() == '\r' || in.front() == '\n'))
    in.remove_prefix(1);

  while (!in.empty() && (in.back() == ' ' || in.back() == '\t' || in.back() == '\r' || in.back() == '\n'))
    in.remove_suffix(1);

  return in;
}

std::string_view trim_left(std::string_view in)
{
  while (!in.empty() && (in.front() == ' ' || in.front() == '\t' || in.front() == '\r' || in.front() == '\n'))
    in.remove_prefix(1);

  return in;
}

std::string_view trim_right(std::string_view in)
{
  while (!in.empty() && (in.back() == ' ' || in.back() == '\t' || in.back() == '\r' || in.back() == '\n'))
    in.remove_suffix(1);

  return in;
}

std::string_view unquote(std::string_view in)
{
  if (in.size() >= 2 && ((in.front() == '"' && in.back() == '"') || (in.front() == '\'' && in.back() == '\'')))
    return in.substr(1, in.size() - 2);

  return in;
}

std::string utf8_to_string(std::u8string const& text)
{
  return std::string(reinterpret_cast<char const*>(text.data()), text.size());
}

} // namespace remountd
