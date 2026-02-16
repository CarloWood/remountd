#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <systemd/sd-daemon.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

constexpr char const* kDefaultConfigPath = "/etc/remountd/config.yaml";
constexpr int kListenBacklog             = 32;
volatile sig_atomic_t g_stop_requested   = 0;
constexpr int kSystemdListenFdStart      = SD_LISTEN_FDS_START;

class ScopedFd
{
 public:
  ScopedFd() = default;
  explicit ScopedFd(int fd) : fd_(fd) { }

  ScopedFd(ScopedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) { }
  ScopedFd& operator=(ScopedFd&& other) noexcept
  {
    if (this != &other)
    {
      reset();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }

  ScopedFd(ScopedFd const&) = delete;
  ScopedFd& operator=(ScopedFd const&) = delete;

  ~ScopedFd()
  {
    reset();
  }

  bool valid() const
  {
    return fd_ >= 0;
  }

  int get() const
  {
    return fd_;
  }

  void reset(int fd = -1)
  {
    if (fd_ >= 0)
      close(fd_);
    fd_ = fd;
  }

  int release()
  {
    return std::exchange(fd_, -1);
  }

 private:
  int fd_ = -1;
};

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

constexpr std::size_t MAXARGLEN = 256;       // To allow passing a path as argument.

bool sane_argument(char const* arg)
{
  // Paranoia.
  if (!arg)
    return false;

  // If the length of the argument is larger or equal than MAXARGLEN characters, abort.
  std::size_t length = 0;
  while (length < MAXARGLEN && arg[length] != '\0')
    ++length;
  if (length == MAXARGLEN)
    return false;

  return true;
}

bool ParseLongOptionWithValue(int argc, char* argv[], int* index, std::string* value_out)
{
  // Read the next argument, if any.
  int const i = *index + 1;

  if (i >= argc)
    return false;

  if (!sane_argument(argv[i]))
    return false;

  *value_out = argv[i];
  *index = i;
  return true;
}

bool ParseArgs(int argc, char* argv[], Options* options)
{
  for (int i = 1; i < argc; ++i)
  {
    if (!sane_argument(argv[i]))
      return false;

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
    std::size_t const comment = current.find('#');
    if (comment != std::string_view::npos)
      current = current.substr(0, comment);
    current = Trim(current);
    if (current.empty())
      continue;

    std::size_t const colon = current.find(':');
    if (colon == std::string_view::npos)
      continue;

    std::string_view const key = Trim(current.substr(0, colon));
    if (key != "socket")
      continue;

    std::string_view value = Trim(current.substr(colon + 1));
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')))
      value = value.substr(1, value.size() - 2);

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
  return sd_is_socket_unix(fd, SOCK_STREAM, -1, nullptr, 0) > 0;
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
  int const listen_fds = sd_listen_fds(0);
  if (listen_fds < 0)
  {
    std::error_code const ec(-listen_fds, std::system_category());
    return {SystemdSocketState::kError, -1, "sd_listen_fds failed: " + ec.message()};
  }
  if (listen_fds == 0)
    return {};

  if (listen_fds > 1)
    return {SystemdSocketState::kError, -1, "Expected exactly one socket from systemd"};

  int const fd = kSystemdListenFdStart;
  if (!IsSocketFd(fd))
    return {SystemdSocketState::kError, -1, "Inherited FD 3 is not a UNIX stream socket"};

  return {SystemdSocketState::kListeningFd, fd, ""};
}

int CreateStandaloneListener(std::string const& socket_path, std::string* error_out)
{
  std::filesystem::path const socket_fs_path(socket_path);
  std::string const socket_native_path = socket_fs_path.string();

  if (socket_native_path.size() >= sizeof(sockaddr_un::sun_path))
  {
    *error_out = "Socket path is too long for AF_UNIX: '" + socket_native_path + "'";
    return -1;
  }

  std::error_code ec;
  bool const exists = std::filesystem::exists(socket_fs_path, ec);
  if (ec)
  {
    *error_out = "Failed to inspect socket path '" + socket_native_path + "': " + ec.message();
    return -1;
  }

  if (exists)
  {
    bool const is_socket = std::filesystem::is_socket(socket_fs_path, ec);
    if (ec)
    {
      *error_out = "Failed to inspect socket path '" + socket_native_path + "': " + ec.message();
      return -1;
    }

    if (!is_socket)
    {
      *error_out = "Path exists and is not a socket: '" + socket_native_path + "'";
      return -1;
    }

    if (!std::filesystem::remove(socket_fs_path, ec) || ec)
    {
      *error_out = "Failed to remove stale socket '" + socket_native_path + "': " + ec.message();
      return -1;
    }
  }

  ScopedFd fd(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (!fd.valid())
  {
    *error_out = "socket(AF_UNIX) failed: " + std::error_code(errno, std::generic_category()).message();
    return -1;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::copy(socket_native_path.begin(), socket_native_path.end(), addr.sun_path);
  addr.sun_path[socket_native_path.size()] = '\0';

  if (bind(fd.get(), reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) != 0)
  {
    *error_out = "bind('" + socket_native_path + "') failed: " + std::error_code(errno, std::generic_category()).message();
    return -1;
  }
  if (listen(fd.get(), kListenBacklog) != 0)
  {
    *error_out = "listen('" + socket_native_path + "') failed: " + std::error_code(errno, std::generic_category()).message();
    return -1;
  }

  return fd.release();
}

void RunEventLoop()
{
  while (!g_stop_requested) { pause(); }
}

}  // namespace

int main(int argc, char* argv[])
{
  Options options;
  if (!ParseArgs(argc, argv, &options))
    return 2;

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
      std::cerr << "remountd: --inetd was specified but stdin is not a socket\n";
      return 1;
    }
    std::cerr << "remountd skeleton running in --inetd mode (protocol handling not implemented yet)\n";
    RunEventLoop();
    return 0;
  }

  SystemdSocketResult const systemd_socket = DetectSystemdSocketActivation();
  if (systemd_socket.state == SystemdSocketState::kError)
  {
    std::cerr << "remountd: socket activation error: " << systemd_socket.error << "\n";
    return 1;
  }

  ScopedFd listener_fd;
  bool unlink_on_exit = false;
  std::string standalone_socket_path;

  if (systemd_socket.state == SystemdSocketState::kListeningFd)
  {
    listener_fd.reset(systemd_socket.fd);
    std::cerr << "remountd skeleton using systemd-activated listening socket on FD " << kSystemdListenFdStart << "\n";
  }
  else
  {
    std::string socket_path;
    if (options.socket_override.has_value())
      socket_path = *options.socket_override;
    else
    {
      std::string error;
      std::optional<std::string> const parsed = ParseSocketPathFromConfig(options.config_path, &error);
      if (!parsed.has_value())
      {
        std::cerr << "remountd: " << error << "\n";
        return 1;
      }
      socket_path = *parsed;
    }

    std::string error;
    int const fd = CreateStandaloneListener(socket_path, &error);
    if (fd < 0)
    {
      std::cerr << "remountd: " << error << "\n";
      return 1;
    }
    listener_fd.reset(fd);

    standalone_socket_path = std::move(socket_path);
    unlink_on_exit         = true;
    std::cerr << "remountd skeleton listening on " << standalone_socket_path << "\n";
  }

  RunEventLoop();

  if (listener_fd.get() == STDIN_FILENO)
    listener_fd.release();

  if (unlink_on_exit && !standalone_socket_path.empty())
  {
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(standalone_socket_path), ec);
  }

  return 0;
}
