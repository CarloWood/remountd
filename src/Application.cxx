#include "Application.h"

namespace remountd {

class ApplicationInfo
{
 private:
  std::u8string application_name_;
  uint32_t encoded_version_;

 public:
  void set_application_name(std::u8string const& application_name)
  {
    application_name_ = application_name;
  }

  void set_application_version(uint32_t encoded_version)
  {
    encoded_version_ = encoded_version;
  }
};

//static
Application* Application::s_instance;

// Construct the base class of the Application object.
//
// Because this is a base class, virtual functions can't be used in the constructor.
// Therefore initialization happens after construction.
Application::Application()
{
  s_instance = this;
}

// This instantiates the destructor of our std::unique_ptr's.
// Because it is here instead of the header we can use forward declarations for the types of certain member variables.
Application::~Application()
{
  // Revoke global access.
  s_instance = nullptr;
}

//virtual
void Application::parse_command_line_parameters(int argc, char* argv[])
{
}

//virtual
std::u8string Application::application_name() const
{
  return u8"remountd";
}

//virtual
uint32_t Application::application_version() const
{
  return 0;     // Should be retrieved from CMakeLists.txt somehow. Use format MAJOR_VERSION * 100 + MINOR_VERSION.
}

// Finish initialization of a default constructed Application.
void Application::initialize(int argc, char** argv)
{
  // Only call initialize once. Calling it twice leads to a nasty dead-lock that was hard to debug ;).
  //ASSERT(!m_event_loop);

  // Parse command line parameters before doing any initialization, so the command line arguments can influence the initialization too.

  // Allow the user to override stuff.
  if (argc > 0)
    parse_command_line_parameters(argc, argv);

  ApplicationInfo application_info;
  application_info.set_application_name(application_name());
  application_info.set_application_version(application_version());
}

void Application::quit()
{
}

// Run the application.
// This function does not return until the program terminated.
void Application::run()
{
}

} // namespace remountd
