#include "Options.h"
#include "SocketServer.h"

#include <signal.h>
#include <unistd.h>

#include <iostream>
#include <string>

namespace {

volatile sig_atomic_t g_stop_requested = 0;

void on_signal(int /*signum*/)
{
  g_stop_requested = 1;
}

void install_signal_handlers()
{
  struct sigaction sa{};
  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
}

void run_event_loop()
{
  while (!g_stop_requested)
    pause();
}

}  // namespace

int main(int argc, char* argv[])
{
  Options options;
  if (!options.parse_args(argc, argv))
    return 2;

  install_signal_handlers();

  SocketServer socket_server;
  std::string error;
  if (!socket_server.initialize(options, &error))
  {
    if (!error.empty())
      std::cerr << "remountd: " << error << "\n";
    return 1;
  }

  run_event_loop();

  return 0;
}
