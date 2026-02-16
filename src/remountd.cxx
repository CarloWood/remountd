#include "Remountd.h"
#include "remountd_error.h"

#include <exception>
#include <iostream>
#include <system_error>

int main(int argc, char* argv[])
{
  using namespace remountd;

  try
  {
    Remountd application;
    application.initialize(argc, argv);
    application.run();
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
