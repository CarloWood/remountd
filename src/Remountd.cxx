#include "sys.h"
#include "Remountd.h"
#include "SocketServer.h"
#include "ScopedFd.h"
#include "utils.h"

#include <sys/wait.h>
#include <unistd.h>

#include <charconv>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace remountd {
namespace {

// Parse pid from token and validate range.
bool parse_pid_token(std::string_view pid_token, pid_t* pid)
{
  if (pid_token.empty())
    return false;

  long long parsed_pid = 0;
  char const* begin = pid_token.data();
  char const* end = pid_token.data() + pid_token.size();
  std::from_chars_result const conversion_result = std::from_chars(begin, end, parsed_pid);
  if (conversion_result.ec != std::errc() || conversion_result.ptr != end)
    return false;

  if (parsed_pid <= 0 || parsed_pid > std::numeric_limits<pid_t>::max())
    return false;

  *pid = static_cast<pid_t>(parsed_pid);
  return true;
}

// Return true when pid identifies a running process.
bool is_running_process(pid_t pid)
{
  if (kill(pid, 0) == 0)
    return true;

  return errno == EPERM;
}

// Return true when `path` starts with `prefix` on path-component boundaries.
bool path_has_prefix(std::filesystem::path const& path, std::filesystem::path const& prefix)
{
  auto path_iter = path.begin();
  auto const path_end = path.end();
  for (auto prefix_iter = prefix.begin(); prefix_iter != prefix.end(); ++prefix_iter, ++path_iter)
  {
    if (path_iter == path_end || *path_iter != *prefix_iter)
      return false;
  }

  return true;
}

// Resolve the configured prefix and requested absolute path to one allowed path.
std::optional<std::filesystem::path> resolve_allowed_path(std::string_view name, std::string_view requested_path, std::string* error_reply)
{
  std::optional<std::filesystem::path> const configured_path = find_allowed_path(name);
  if (!configured_path.has_value())
  {
    *error_reply = format_unknown_identifier_error(name);
    return std::nullopt;
  }

  if (requested_path.empty() || requested_path.front() != '/')
  {
    *error_reply = "ERROR: path must start with '/'.\n";
    return std::nullopt;
  }

  std::filesystem::path const configured_prefix = configured_path->lexically_normal();
  if (!configured_prefix.is_absolute())
  {
    *error_reply = "ERROR: configured path for '" + std::string(name) + "' is not absolute.\n";
    return std::nullopt;
  }

  std::filesystem::path const requested_suffix{std::string(requested_path)};
  std::filesystem::path const resolved_path = (configured_prefix / requested_suffix.relative_path()).lexically_normal();

  if (!path_has_prefix(resolved_path, configured_prefix))
  {
    *error_reply = "ERROR: requested path escapes allowed prefix.\n";
    return std::nullopt;
  }

  return resolved_path;
}

// Execute remount command in mount namespace of pid.
// Returns empty string on success, otherwise a description.
std::string execute_remount_command(pid_t pid, bool read_only, std::filesystem::path const& path)
{
  int stderr_pipe_fds[2];
  if (pipe(stderr_pipe_fds) != 0)
    return "pipe failed: " + std::string(std::strerror(errno));

  ScopedFd read_end(stderr_pipe_fds[0]);
  ScopedFd write_end(stderr_pipe_fds[1]);

  std::string const pid_string = std::to_string(pid);
  std::string const options = read_only ? "remount,ro,bind" : "remount,rw,bind";
  std::string const path_string = path.string();
  char const* args[] = {
      "nsenter",
      "-t",
      pid_string.c_str(),
      "-m",
      "--",
      "mount",
      "-o",
      options.c_str(),
      path_string.c_str(),
      nullptr
  };

  pid_t const child_pid = fork();
  if (child_pid < 0)
    return "fork failed: " + std::string(std::strerror(errno));

  if (child_pid == 0)
  {
    read_end.reset();
    if (dup2(write_end.get(), STDERR_FILENO) < 0)
      _exit(127);
    write_end.reset();

    execvp(args[0], const_cast<char* const*>(args));
    int const exec_errno = errno;
    dprintf(STDERR_FILENO, "execvp(nsenter) failed: %s", std::strerror(exec_errno));
    _exit(127);
  }

  write_end.reset();

  std::string stderr_text;
  char buffer[512];
  for (;;)
  {
    ssize_t const read_ret = read(read_end.get(), buffer, sizeof(buffer));
    if (read_ret > 0)
    {
      stderr_text.append(buffer, static_cast<std::size_t>(read_ret));
      continue;
    }

    if (read_ret == 0)
      break;

    if (errno == EINTR)
      continue;

    break;
  }

  int status = 0;
  while (waitpid(child_pid, &status, 0) < 0)
  {
    if (errno == EINTR)
      continue;

    return "waitpid failed: " + std::string(std::strerror(errno));
  }

  if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    return {};

  trim_right(&stderr_text);
  if (!stderr_text.empty())
    return stderr_text;

  if (WIFEXITED(status))
    return "nsenter/mount failed with exit status " + std::to_string(WEXITSTATUS(status));
  if (WIFSIGNALED(status))
    return "nsenter/mount terminated by signal " + std::to_string(WTERMSIG(status));

  return "nsenter/mount failed";
}

// Remountd:Client
//
// Concrete client used by remountd. Protocol handling will be added later.
class RemountdClient final : public SocketClient
{
 public:
  // Construct a remountd client wrapper around a connected socket.
  RemountdClient(SocketServer& socket_server, int fd) : SocketClient(socket_server, fd)
  {
    DoutEntering(dc::notice, "RemountdClient::RemountdClient(" << fd << ")");
  }

 protected:
  // Handle one complete newline-terminated message.
  bool new_message(std::string_view message) override
  {
    DoutEntering(dc::notice, "RemountdClient::new_message(\"" << message << "\")");

    if (message == "quit")
      return false;

    if (message == "list")
    {
      std::string const reply = Application::instance().format_allowed_mount_points(false);
      send_text_to_socket(fd(), reply);
      return true;
    }

    std::vector<std::string_view> const tokens = split_tokens(message);
    if (tokens.empty())
      return false;

    bool const is_ro = tokens[0] == "ro";
    bool const is_rw = tokens[0] == "rw";

    if (!is_ro && !is_rw)
      return false;

    if (tokens.size() != 4)
    {
      send_text_to_socket(fd(), "ERROR: invalid command format.\n");
      return true;
    }

    std::string_view const name = tokens[1];
    std::string error_reply;
    std::optional<std::filesystem::path> const path = resolve_allowed_path(name, tokens[2], &error_reply);
    if (!path.has_value())
    {
      send_text_to_socket(fd(), error_reply);
      return true;
    }

    pid_t pid = 0;
    if (!parse_pid_token(tokens[3], &pid) || !is_running_process(pid))
    {
      send_text_to_socket(fd(), "ERROR: " + std::string(tokens[3]) + " is not a running process.\n");
      return true;
    }

    std::string const error_description = execute_remount_command(pid, is_ro, *path);
    if (!error_description.empty())
    {
      send_text_to_socket(fd(), "ERROR: " + error_description + "\n");
      return true;
    }

    send_text_to_socket(fd(), "OK\n");
    return true;
  }
};

} // namespace

Remountd::Remountd(int argc, char* argv[])
{
  Application::initialize(argc, argv);
  // The Application base class must be initialized before we can create the SocketServer.
  socket_server_ = std::make_unique<SocketServer>(inetd_mode_);
  socket_server_->set_client_factory(
      [](SocketServer& socket_server, int client_fd)
      {
        return std::make_unique<RemountdClient>(socket_server, client_fd);
      });
}

Remountd::~Remountd() = default;

//virtual
bool Remountd::parse_command_line_parameter(std::string_view arg, int /*argc*/, char*[] /*argv*/, int* /*index*/)
{
  if (arg == "--inetd")
  {
    inetd_mode_ = true;
    return true;
  }

  return false;
}

void Remountd::print_usage_extra(std::ostream& os) const
{
  os << " [--inetd]";
}

void Remountd::mainloop()
{
  DoutEntering(dc::notice, "Remountd::mainloop()");

  if (!socket_server_)
    throw std::system_error(EINVAL, std::generic_category(), "socket server is not initialized");

  socket_server_->mainloop(termination_fd());
}

//virtual
std::u8string Remountd::application_name() const
{
  return u8"remountd";
}

} // namespace remountd
