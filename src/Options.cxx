#include "Options.h"
#include <string_view>
#include <iostream>
#include <fstream>

constexpr std::size_t MAXARGLEN = 256;       // To allow passing a path as argument.

namespace {

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

void print_usage(char const* argv0)
{
  std::cerr << "Usage: " << argv0 << " [--config <path>] [--socket <path>] [--inetd]\n";
}

bool parse_long_option_with_value(int argc, char* argv[], int* index, std::string* value_out)
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

std::string_view trim(std::string_view in)
{
  while (!in.empty() && (in.front() == ' ' || in.front() == '\t' || in.front() == '\r' || in.front() == '\n'))
    in.remove_prefix(1);

  while (!in.empty() && (in.back() == ' ' || in.back() == '\t' || in.back() == '\r' || in.back() == '\n'))
    in.remove_suffix(1);

  return in;
}

} // namespace

bool Options::parse_args(int argc, char* argv[])
{
  for (int i = 1; i < argc; ++i)
  {
    if (!sane_argument(argv[i]))
      return false;

    std::string_view const arg(argv[i]);
    if (arg == "--help" || arg == "-h")
    {
      print_usage(argv[0]);
      return false;
    }

    if (arg == "--inetd")
    {
      inetd_mode_ = true;
      continue;
    }

    if (arg == "--config")
    {
      std::string value;
      if (!parse_long_option_with_value(argc, argv, &i, &value))
      {
        std::cerr << "Missing value for --config\n";
        return false;
      }
      config_path_ = std::move(value);
      continue;
    }
    if (arg == "--socket")
    {
      std::string value;
      if (!parse_long_option_with_value(argc, argv, &i, &value))
      {
        std::cerr << "Missing value for --socket\n";
        return false;
      }
      socket_override_ = std::move(value);
      continue;
    }

    std::cerr << "Unknown argument: " << arg << "\n";
    return false;
  }

  return true;
}

std::optional<std::string> Options::parse_socket_path_from_config(std::string* error_out) const
{
  std::ifstream config(config_path_);
  if (!config.is_open())
  {
    *error_out = "Unable to open config file '" + config_path_ + "'";
    return std::nullopt;
  }

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
    {
      *error_out = "Config key 'socket' is empty in '" + config_path_ + "'";
      return std::nullopt;
    }

    return std::string(value);
  }

  *error_out = "Config file '" + config_path_ + "' does not define a 'socket' key";
  return std::nullopt;
}

bool Options::get_socket_path(std::string* socket_path_out) const
{
  if (socket_override_.has_value())
    *socket_path_out = *socket_override_;
  else
  {
    std::string error;
    std::optional<std::string> const parsed = parse_socket_path_from_config(&error);
    if (!parsed.has_value())
    {
      std::cerr << "remountd: " << error << "\n";
      return false;
    }
    *socket_path_out = *parsed;
  }
  return true;
}
