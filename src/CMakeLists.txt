find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

add_executable(remountd
  Application.cxx
  Options.cxx
  SocketServer.cxx
  remountd_error.cxx
  remountd.cxx
)

target_link_libraries(remountd
  PRIVATE
    PkgConfig::LIBSYSTEMD
)
