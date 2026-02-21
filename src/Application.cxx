#include "sys.h"
#include "Application.h"
#include "remountd_error.h"
#include "utils.h"
#include "version.h"

#include <algorithm>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <system_error>
#include <unistd.h>

#include "debug.h"

namespace {

constexpr std::size_t max_argument_length = 256;

bool sane_argument(char const* arg)
{
  if (!arg)
    return false;

  std::size_t length = 0;
  while (length < max_argument_length && arg[length] != '\0')
    ++length;
  if (length == max_argument_length)
    return false;

  return true;
}

std::optional<std::string> parse_long_option_with_value(int argc, char* argv[], int* index)
{
  int const i = *index + 1;
  if (i >= argc)
    return std::nullopt;
  if (!sane_argument(argv[i]))
    return std::nullopt;

  *index = i;
  return argv[i];
}

} // namespace

namespace remountd {

Application* Application::s_instance_ = nullptr;
int Application::s_signal_write_fd_ = -1;

Application::Application()
{
  // There is only one Application instance.
  s_instance_ = this;
}

Application::~Application()
{
  // Make sure Application::signal_handler is not called after Application was destructed.
  s_signal_write_fd_ = -1;
  uninstall_signal_handlers();
  // Revoke all access to this instance.
  s_instance_ = nullptr;
}

void Application::print_usage() const
{
  std::cerr << "Usage: " << utf8_to_string(application_info_.application_name()) << " [--help] [--version] [--list] [--config <path>] [--socket <path>]";
  print_usage_extra(std::cerr);
  std::cerr << "\n";
}

void Application::print_version() const
{
  auto [major, minor] = application_info_.version();
  std::cout << utf8_to_string(application_info_.application_name()) << ' ' << major << '.' << minor << "\n";
}

//virtual
bool Application::parse_command_line_parameter(std::string_view /*arg*/, int /*argc*/, char*[] /*argv*/, int* /*index*/)
{
  return false;
}

//virtual
void Application::print_usage_extra(std::ostream& /*os*/) const
{
}

void Application::parse_command_line_parameters(int argc, char* argv[])
{
  if (argc <= 0 || argv == nullptr || !sane_argument(argv[0]))
    throw_error(errc::invalid_argument, "invalid process arguments");

  bool list_requested = false;
  for (int i = 1; i < argc; ++i)
  {
    if (!sane_argument(argv[i]))
      throw_error(errc::invalid_argument, "invalid argument at index " + std::to_string(i));

    std::string_view const arg(argv[i]);
    if (arg == "--help" || arg == "-h")
    {
      print_usage();
      throw_error(errc::no_error, "help requested");
    }

    if (arg == "--version")
    {
      print_version();
      throw_error(errc::no_error, "version requested");
    }

    if (arg == "--list")
    {
      list_requested = true;
      continue;
    }

    if (arg == "--config")
    {
      std::optional<std::string> const value = parse_long_option_with_value(argc, argv, &i);
      if (!value.has_value() || value->empty())
        throw_error(errc::missing_option_value, "missing value for --config");
      config_path_ = *value;
      continue;
    }

    if (arg == "--socket")
    {
      std::optional<std::string> const value = parse_long_option_with_value(argc, argv, &i);
      if (!value.has_value() || value->empty())
        throw_error(errc::missing_option_value, "missing value for --socket");
      socket_override_ = *value;
      continue;
    }

    if (parse_command_line_parameter(arg, argc, argv, &i))
      continue;

    throw_error(errc::unknown_argument, "unknown argument: " + std::string(arg));
  }

  if (list_requested)
  {
    load_config();
    std::cout << format_allowed_mount_points(true);
    throw_error(errc::no_error, "list requested");
  }
}

void Application::load_config()
{
  if (config_loaded_)
    return;

  std::ifstream config(config_path_);
  if (!config.is_open())
    throw_error(errc::config_open_failed, "unable to open config file '" + config_path_.native() + "'");

  configured_socket_path_.clear();
  allowed_mount_points_.clear();

  bool in_allow_section = false;
  std::string current_allow_name;
  std::string line;
  while (std::getline(config, line))
  {
    std::string_view current = trim_right(line);
    std::size_t const comment = current.find('#');
    if (comment != std::string_view::npos)
      current = current.substr(0, comment);
    current = trim_right(current);
    if (current.empty())
      continue;

    std::size_t indent = 0;
    while (indent < current.size() && (current[indent] == ' ' || current[indent] == '\t'))
      ++indent;

    std::string_view const content = trim_left(current.substr(indent));
    if (content.empty())
      continue;

    if (indent == 0)
    {
      in_allow_section = false;
      current_allow_name.clear();
    }

    std::size_t const colon = content.find(':');
    if (colon == std::string_view::npos)
      continue;

    std::string_view const key = trim(content.substr(0, colon));
    std::string_view const raw_value = trim(content.substr(colon + 1));

    if (indent == 0)
    {
      if (key == "socket")
      {
        std::string_view const value = unquote(raw_value);
        if (value.empty())
          throw_error(errc::config_socket_empty, "config key 'socket' is empty in '" + config_path_.native() + "'");
        configured_socket_path_ = std::string(value);
        continue;
      }

      if (key == "allow" && raw_value.empty())
      {
        in_allow_section = true;
        continue;
      }

      continue;
    }

    if (!in_allow_section)
      continue;

    if (indent == 2)
    {
      if (!raw_value.empty())
        continue;

      if (!key.empty())
      {
        current_allow_name = std::string(key);
        continue;
      }
    }

    if (indent >= 4 && !current_allow_name.empty() && key == "path")
    {
      std::string_view const value = unquote(raw_value);
      if (value.empty())
        continue;

      allowed_mount_points_.push_back({current_allow_name, std::filesystem::path(value)});
      current_allow_name.clear();
    }
  }

  if (configured_socket_path_.empty())
    throw_error(errc::config_socket_missing, "config file '" + config_path_.native() + "' does not define a 'socket' key");

  config_loaded_ = true;
}

void Application::create_termination_pipe()
{
  int pipe_fds[2] = {-1, -1};
  if (pipe2(pipe_fds, O_NONBLOCK | O_CLOEXEC) != 0)
    throw std::system_error(errno, std::generic_category(), "pipe2 failed");

  terminate_read_fd_.reset(pipe_fds[0]);
  terminate_write_fd_.reset(pipe_fds[1]);
}

//static
void Application::notify_termination_fd(int fd) noexcept
{
  if (fd < 0)
    return;

  unsigned char byte = 1;
  ssize_t const ret = write(fd, &byte, 1);
  (void)ret;
}

//static
void Application::signal_handler(int /*signum*/)
{
  notify_termination_fd(s_signal_write_fd_);
}

//static
void Application::install_signal_handlers()
{
  struct sigaction sa{};
  sa.sa_handler = &Application::signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (sigaction(SIGINT, &sa, nullptr) != 0)
    throw std::system_error(errno, std::generic_category(), "sigaction(SIGINT) failed");
  if (sigaction(SIGTERM, &sa, nullptr) != 0)
    throw std::system_error(errno, std::generic_category(), "sigaction(SIGTERM) failed");
}

//static
void Application::uninstall_signal_handlers()
{
  struct sigaction sa{};
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
}

void Application::initialize(int argc, char** argv)
{
  if (initialized_)
    throw_error(errc::application_already_initialized, "initialize called more than once");

  // Initialize ApplicationInfo first so option handlers can use it.
  application_info_.set_application_name(application_name());
  application_info_.set_application_version(application_version());

  // Parse command line parameters, if any.
  if (argc > 0)
    parse_command_line_parameters(argc, argv);

  // Parse and cache configuration.
  load_config();

  // Set up signal handling.
  create_termination_pipe();
  s_signal_write_fd_ = terminate_write_fd_.get();
  install_signal_handlers();

  // Fully initialized.
  initialized_ = true;
}

void Application::run()
{
  DoutEntering(dc::notice, "Application::run()");

  if (!initialized_)
    throw_error(errc::application_not_initialized, "run called before initialize");

  // Call mainloop of derived class.
  mainloop();
}

void Application::quit()
{
  notify_termination_fd(terminate_write_fd_.get());
}

std::filesystem::path Application::socket_path() const
{
  if (socket_override_.has_value())
    return *socket_override_;

  return configured_socket_path_;
}

std::string Application::format_allowed_mount_points(bool include_header) const
{
  std::ostringstream out;
  if (include_header)
  {
    std::size_t name_width = std::string("NAME").size();
    for (AllowedMountPoint const& allowed_mount_point : allowed_mount_points_)
      name_width = std::max(name_width, allowed_mount_point.name_.size());

    out << std::left << std::setw(static_cast<int>(name_width)) << "NAME" << " PATH\n";
    for (AllowedMountPoint const& allowed_mount_point : allowed_mount_points_)
      out << std::left << std::setw(static_cast<int>(name_width)) << allowed_mount_point.name_ << " " << allowed_mount_point.path_.native() << "\n";
  }
  else
  {
    for (AllowedMountPoint const& allowed_mount_point : allowed_mount_points_)
      out << allowed_mount_point.name_ << " " << allowed_mount_point.path_.native() << "\n";
  }
  return out.str();
}

//virtual
uint32_t Application::application_version() const
{
  // From version.h.
  return application_version_c;
}

} // namespace remountd
