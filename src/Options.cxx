#include "Options.h"
#include "remountd_error.h"

#include <fstream>
#include <iostream>
#include <string_view>

constexpr std::size_t max_argument_length = 256;

namespace {

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

std::string parse_long_option_with_value(int argc, char* argv[], int* index)
{
  int const i = *index + 1;
  if (i >= argc)
    return {};

  if (!sane_argument(argv[i]))
    return {};

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

Options::Options(int argc, char* argv[])
{
  parse_args(argc, argv);
}

//static
void Options::print_usage(char const* argv0)
{
  std::cerr << "Usage: " << argv0 << " [--config <path>] [--socket <path>] [--inetd]\n";
}

void Options::parse_args(int argc, char* argv[])
{
  using namespace remountd;

  if (argc <= 0 || argv == nullptr || !sane_argument(argv[0]))
    throw_error(errc::invalid_argument, "invalid process arguments");

  for (int i = 1; i < argc; ++i)
  {
    if (!sane_argument(argv[i]))
      throw_error(errc::invalid_argument, "invalid argument at index " + std::to_string(i));

    std::string_view const arg(argv[i]);
    if (arg == "--help" || arg == "-h")
    {
      Options::print_usage(argv[0]);
      throw_error(errc::help_requested, "help requested");
    }

    if (arg == "--inetd")
    {
      inetd_mode_ = true;
      continue;
    }

    if (arg == "--config")
    {
      std::string const value = parse_long_option_with_value(argc, argv, &i);
      if (value.empty())
        throw_error(errc::missing_option_value, "missing value for --config");
      config_path_ = std::move(value);
      continue;
    }
    if (arg == "--socket")
    {
      std::string const value = parse_long_option_with_value(argc, argv, &i);
      if (value.empty())
        throw_error(errc::missing_option_value, "missing value for --socket");
      socket_override_ = std::move(value);
      continue;
    }

    throw_error(errc::unknown_argument, "unknown argument: " + std::string(arg));
  }
}

std::string Options::parse_socket_path_from_config() const
{
  using namespace remountd;

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

    return std::string{value};
  }

  throw_error(errc::config_socket_missing, "config file '" + config_path_ + "' does not define a 'socket' key");
}

std::string Options::socket_path() const
{
  if (socket_override_.has_value())
    return *socket_override_;

  return parse_socket_path_from_config();
}
