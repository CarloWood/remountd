#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

constexpr char const* kDefaultConfigPath = "/etc/remountd/config.yaml";
constexpr int kListenBacklog             = 32;
volatile sig_atomic_t g_stop_requested   = 0;

struct Options
{
  std::string config_path = kDefaultConfigPath;
  std::optional<std::string> socket_override;
  bool inetd_mode = false;
};

void PrintUsage(char const* argv0)
{
  std::cerr << "Usage: " << argv0 << " [--config <path>] [--socket <path>] [--inetd]\n";
}

bool ParseLongOptionWithValue(int argc, char* argv[], int* index, std::string* value_out)
{
  if (*index + 1 >= argc) return false;
  *value_out = argv[*index + 1];
  *index += 1;
  return true;
}

bool ParseArgs(int argc, char* argv[], Options* options)
{
  for (int i = 1; i < argc; ++i)
  {
    std::string_view const arg(argv[i]);
    if (arg == "--help" || arg == "-h")
    {
      PrintUsage(argv[0]);
      return false;
    }
    if (arg == "--inetd")
    {
      options->inetd_mode = true;
      continue;
    }
    if (arg == "--config")
    {
      std::string value;
      if (!ParseLongOptionWithValue(argc, argv, &i, &value))
      {
        std::cerr << "Missing value for --config\n";
        return false;
      }
      options->config_path = std::move(value);
      continue;
    }
    if (arg == "--socket")
    {
      std::string value;
      if (!ParseLongOptionWithValue(argc, argv, &i, &value))
      {
        std::cerr << "Missing value for --socket\n";
        return false;
      }
      options->socket_override = std::move(value);
      continue;
    }

    std::cerr << "Unknown argument: " << arg << "\n";
    return false;
  }
  return true;
}

std::string_view Trim(std::string_view in)
{
  while (!in.empty() && (in.front() == ' ' || in.front() == '\t' || in.front() == '\r' || in.front() == '\n')) in.remove_prefix(1);

  while (!in.empty() && (in.back() == ' ' || in.back() == '\t' || in.back() == '\r' || in.back() == '\n')) in.remove_suffix(1);

  return in;
}

std::optional<std::string> ParseSocketPathFromConfig(std::string const& config_path, std::string* error_out)
{
  std::ifstream config(config_path);
  if (!config.is_open())
  {
    *error_out = "Unable to open config file '" + config_path + "'";
    return std::nullopt;
  }

  std::string line;
  while (std::getline(config, line))
  {
    std::string_view current(line);
    size_t const comment = current.find('#');
    if (comment != std::string_view::npos) current = current.substr(0, comment);
    current = Trim(current);
    if (current.empty()) continue;

    size_t const colon = current.find(':');
    if (colon == std::string_view::npos) { continue; }

    std::string_view const key = Trim(current.substr(0, colon));
    if (key != "socket") continue;

    std::string_view value = Trim(current.substr(colon + 1));
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) value = value.substr(1, value.size() - 2);

    if (value.empty())
    {
      *error_out = "Config key 'socket' is empty in '" + config_path + "'";
      return std::nullopt;
    }

    return std::string(value);
  }

  *error_out = "Config file '" + config_path + "' does not define a 'socket' key";
  return std::nullopt;
}

void OnSignal(int /*signum*/)
{
  g_stop_requested = 1;
}

bool IsSocketFd(int fd)
{
  struct stat st;
  if (fstat(fd, &st) != 0) return false;

  return S_ISSOCK(st.st_mode);
}

enum class SystemdSocketState
{
  kNone,
  kListeningFd,
  kError,
};

struct SystemdSocketResult
{
  SystemdSocketState state = SystemdSocketState::kNone;
  int fd                   = -1;
  std::string error;
};

SystemdSocketResult DetectSystemdSocketActivation()
{
  char const* listen_fds_env = getenv("LISTEN_FDS");
  if (listen_fds_env == nullptr) { return {}; }

  char const* listen_pid_env = getenv("LISTEN_PID");
  if (listen_pid_env == nullptr) { return {SystemdSocketState::kError, -1, "LISTEN_FDS is set but LISTEN_PID is missing"}; }

  char* end             = nullptr;
  long const listen_pid = strtol(listen_pid_env, &end, 10);
  if (end == nullptr || *end != '\0' || listen_pid <= 0) { return {SystemdSocketState::kError, -1, "Invalid LISTEN_PID value"}; }
  if (static_cast<pid_t>(listen_pid) != getpid()) { return {}; }

  end                   = nullptr;
  long const listen_fds = strtol(listen_fds_env, &end, 10);
  if (end == nullptr || *end != '\0' || listen_fds < 1) { return {SystemdSocketState::kError, -1, "LISTEN_FDS must be >= 1 when LISTEN_PID matches"}; }

  int const fd = 3;  // SD_LISTEN_FDS_START
  if (!IsSocketFd(fd)) { return {SystemdSocketState::kError, -1, "Inherited FD 3 is not a socket"}; }

  return {SystemdSocketState::kListeningFd, fd, ""};
}

int CreateStandaloneListener(std::string const& socket_path, std::string* error_out)
{
  if (socket_path.size() >= sizeof(sockaddr_un::sun_path))
  {
    *error_out = "Socket path is too long for AF_UNIX: '" + socket_path + "'";
    return -1;
  }

  struct stat st;
  if (lstat(socket_path.c_str(), &st) == 0)
  {
    if (!S_ISSOCK(st.st_mode))
    {
      *error_out = "Path exists and is not a socket: '" + socket_path + "'";
      return -1;
    }
    if (unlink(socket_path.c_str()) != 0)
    {
      *error_out = "Failed to remove stale socket '" + socket_path + "': " + strerror(errno);
      return -1;
    }
  }
  else if (errno != ENOENT)
  {
    *error_out = "Failed to stat socket path '" + socket_path + "': " + strerror(errno);
    return -1;
  }

  int const fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
  {
    *error_out = "socket(AF_UNIX) failed: " + std::string(strerror(errno));
    return -1;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

  if (bind(fd, reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) != 0)
  {
    *error_out = "bind('" + socket_path + "') failed: " + strerror(errno);
    close(fd);
    return -1;
  }
  if (listen(fd, kListenBacklog) != 0)
  {
    *error_out = "listen('" + socket_path + "') failed: " + strerror(errno);
    close(fd);
    return -1;
  }

  return fd;
}

void RunEventLoop()
{
  while (!g_stop_requested) { pause(); }
}

}  // namespace

int main(int argc, char* argv[])
{
  Options options;
  if (!ParseArgs(argc, argv, &options)) { return 2; }

  struct sigaction sa{};
  sa.sa_handler = OnSignal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);

  if (options.inetd_mode)
  {
    if (!IsSocketFd(STDIN_FILENO))
    {
      std::cerr << "--inetd was specified but stdin is not a socket\n";
      return 1;
    }
    std::cerr << "remountd skeleton running in --inetd mode (protocol handling not implemented yet)\n";
    RunEventLoop();
    return 0;
  }

  SystemdSocketResult const systemd_socket = DetectSystemdSocketActivation();
  if (systemd_socket.state == SystemdSocketState::kError)
  {
    std::cerr << "socket activation error: " << systemd_socket.error << "\n";
    return 1;
  }

  int listener_fd     = -1;
  bool unlink_on_exit = false;
  std::string standalone_socket_path;

  if (systemd_socket.state == SystemdSocketState::kListeningFd)
  {
    listener_fd = systemd_socket.fd;
    std::cerr << "remountd skeleton using systemd-activated listening socket on FD 3\n";
  }
  else
  {
    std::string socket_path;
    if (options.socket_override.has_value()) { socket_path = *options.socket_override; }
    else
    {
      std::string error;
      std::optional<std::string> const parsed = ParseSocketPathFromConfig(options.config_path, &error);
      if (!parsed.has_value())
      {
        std::cerr << error << "\n";
        return 1;
      }
      socket_path = *parsed;
    }

    std::string error;
    listener_fd = CreateStandaloneListener(socket_path, &error);
    if (listener_fd < 0)
    {
      std::cerr << error << "\n";
      return 1;
    }

    standalone_socket_path = std::move(socket_path);
    unlink_on_exit         = true;
    std::cerr << "remountd skeleton listening on " << standalone_socket_path << "\n";
  }

  RunEventLoop();

  if (listener_fd >= 0 && listener_fd != STDIN_FILENO) { close(listener_fd); }
  if (unlink_on_exit && !standalone_socket_path.empty()) { unlink(standalone_socket_path.c_str()); }
}
