#include "Options.h"
#include "SocketServer.h"
#include "remountd_error.h"

#include <signal.h>
#include <unistd.h>

#include <exception>
#include <iostream>
#include <system_error>

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
  using namespace remountd;

  try
  {
    Options const options(argc, argv);
    install_signal_handlers();
    SocketServer socket_server(options);
    run_event_loop();
    return 0;
  }
  catch (std::system_error const& error)
  {
    if (error.code() == errc::help_requested)
      return 0;
    std::cerr << "remountd: " << error.what() << "\n";
  }
  catch (std::exception const& error)
  {
    std::cerr << "remountd: " << error.what() << "\n";
  }
  return 1;
}
