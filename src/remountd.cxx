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
    Remountd application(argc, argv);
    application.run();
    return 0;
  }
  catch (std::system_error const& error)
  {
    if (error.code() == errc::help_requested || error.code() == errc::version_requested)
      return 0;
    std::cerr << argv[0] << ": " << error.what() << "\n";
  }
  catch (std::exception const& error)
  {
    std::cerr << argv[0] << ": " << error.what() << "\n";
  }
  return 1;
}
