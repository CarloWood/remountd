find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

configure_file(version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

add_executable(remountd
  Application.cxx
  Remountd.cxx
  SocketClient.cxx
  SocketServer.cxx
  remountd_error.cxx
  remountd.cxx
  utils.cxx
)

target_link_libraries(remountd
  PRIVATE
    ${AICXX_OBJECTS_LIST}
    PkgConfig::LIBSYSTEMD
)

target_include_directories(remountd
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}
)

add_executable(remountdctl
  Application.cxx
  RemountdCtl.cxx
  remountd_error.cxx
  remountdctl.cxx
  utils.cxx
)

target_link_libraries(remountdctl
  PRIVATE
    ${AICXX_OBJECTS_LIST}
)

target_include_directories(remountdctl
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}
)
