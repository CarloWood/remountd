#include "sys.h"
#include "RemountdCtl.h"
#include "remountd_error.h"

#include <exception>
#include <iostream>
#include <system_error>

#include "debug.h"

int main(int argc, char* argv[])
{
  Debug(NAMESPACE_DEBUG::init());
  Dout(dc::notice, "Entering main()...");

  using namespace remountd;
  int exit_code = 0;
  try
  {
    RemountdCtl application(argc, argv);
    application.run();
  }
  catch (std::system_error const& error)
  {
    if (error.code() != errc::no_error)
    {
      std::cerr << argv[0] << ": " << error.what() << "\n";
      exit_code = 1;
    }
  }
  catch (std::exception const& error)
  {
    std::cerr << argv[0] << ": " << error.what() << "\n";
    exit_code = 1;
  }

  Dout(dc::notice, "Leaving main()...");
  return exit_code;
}
