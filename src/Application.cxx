#include "Application.h"
#include "remountd_error.h"

#include <fstream>
#include <iostream>
#include <unistd.h>

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

std::string_view trim(std::string_view in)
{
  while (!in.empty() && (in.front() == ' ' || in.front() == '\t' || in.front() == '\r' || in.front() == '\n'))
    in.remove_prefix(1);

  while (!in.empty() && (in.back() == ' ' || in.back() == '\t' || in.back() == '\r' || in.back() == '\n'))
    in.remove_suffix(1);

  return in;
}

} // namespace

namespace remountd {

Application* Application::s_instance_ = nullptr;

Application::Application()
{
  // There is only one Application instance.
  s_instance_ = this;
}

Application::~Application()
{
  // Make sure Application::signal_handler is not called after Application was destructed.
  if (initialized_)
    uninstall_signal_handlers();
  // Revoke all access to this instance.
  s_instance_ = nullptr;
}

void Application::print_usage(char const* argv0) const
{
  std::cerr << "Usage: " << argv0 << " [--help] [--config <path>] [--socket <path>]";
  print_usage_extra(std::cerr);
  std::cerr << "\n";
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

  for (int i = 1; i < argc; ++i)
  {
    if (!sane_argument(argv[i]))
      throw_error(errc::invalid_argument, "invalid argument at index " + std::to_string(i));

    std::string_view const arg(argv[i]);
    if (arg == "--help" || arg == "-h")
    {
      print_usage(argv[0]);
      throw_error(errc::help_requested, "help requested");
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
}

std::string Application::parse_socket_path_from_config() const
{
  std::ifstream config(config_path_);
  if (!config.is_open())
    throw_error(errc::config_open_failed, "unable to open config file '" + config_path_ + "'");

  std::string line;
  while (std::getline(config, line))
  {
    std::string_view current(line);
    std::size_t const comment = current.find('#');
    if (comment != std::string_view::npos)
      current = current.substr(0, comment);
    current = trim(current);
    if (current.empty())
      continue;

    std::size_t const colon = current.find(':');
    if (colon == std::string_view::npos)
      continue;

    std::string_view const key = trim(current.substr(0, colon));
    if (key != "socket")
      continue;

    std::string_view value = trim(current.substr(colon + 1));
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')))
      value = value.substr(1, value.size() - 2);

    if (value.empty())
      throw_error(errc::config_socket_empty, "config key 'socket' is empty in '" + config_path_ + "'");

    return std::string(value);
  }

  throw_error(errc::config_socket_missing, "config file '" + config_path_ + "' does not define a 'socket' key");
}

//static
void Application::install_signal_handlers()
{
  struct sigaction sa{};
  sa.sa_handler = &Application::signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
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

  if (argc > 0)
    parse_command_line_parameters(argc, argv);

  application_info_.set_application_name(application_name());
  application_info_.set_application_version(application_version());

  stop_requested_ = 0;
  install_signal_handlers();
  initialized_ = true;
}

void Application::run()
{
  if (!initialized_)
    throw_error(errc::application_not_initialized, "run called before initialize");

  while (!stop_requested_)
    pause();
}

void Application::quit()
{
  stop_requested_ = 1;
}

std::string Application::socket_path() const
{
  if (socket_override_.has_value())
    return *socket_override_;

  return parse_socket_path_from_config();
}

//virtual
uint32_t Application::application_version() const
{
  return 0;
}

} // namespace remountd
