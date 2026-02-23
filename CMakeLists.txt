cmake_minimum_required(VERSION 3.16...4.2.3)

project(remountd
  VERSION 0.1
  LANGUAGES CXX
  DESCRIPTION "Socket-activated daemon to remount preconfigured mount points ro/rw; controlled by an unprivileged client.")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#==============================================================================
# Install / uninstall.
#
# Note: `DESTDIR` is honored by CMake's install target automatically. For
# install-time scripts we explicitly prefix with $ENV{DESTDIR}.

set(REMOUNTD_CONFDIR "/etc/remountd"
  CACHE PATH "Configuration directory (installed configs are never overwritten).")
set(REMOUNTD_SBINDIR "/usr/sbin"
  CACHE PATH "Directory to install remountd (privileged daemon) into.")
set(REMOUNTD_BINDIR "/usr/bin"
  CACHE PATH "Directory to install remountctl (unprivileged client) into.")
set(REMOUNTD_RUNDIR "/run/remountd"
  CACHE PATH "Runtime directory (used by the systemd socket).")
set(REMOUNTD_SYSTEMD_UNITDIR "/usr/lib/systemd/system"
  CACHE PATH "Directory to install systemd unit files into.")
set(REMOUNTD_DATADIR "/usr/share/remountd"
  CACHE PATH "Directory to install helper scripts into.")

set(_remountd_install_configs_script "${CMAKE_CURRENT_BINARY_DIR}/remountd_install_configs.cmake")
configure_file("${CMAKE_SOURCE_DIR}/cmake/remountd_install_configs.cmake.in"
  "${_remountd_install_configs_script}" @ONLY)

set(_remountd_uninstall_script "${CMAKE_CURRENT_BINARY_DIR}/remountd_uninstall.cmake")
configure_file("${CMAKE_SOURCE_DIR}/cmake/remountd_uninstall.cmake.in"
  "${_remountd_uninstall_script}" @ONLY)

add_custom_target(uninstall
  COMMAND "${CMAKE_COMMAND}" -P "${_remountd_uninstall_script}"
  COMMENT "Uninstall remountd from the system")

#==============================================================================
# Begin of gitache configuration.
set(GITACHE_PACKAGES libcwd_r)
include(cwm4/cmake/StableGitache)
# End of gitache configuration.

#==============================================================================
# This project uses aicxx modules.
include(cwm4/cmake/AICxxProject NO_POLICY_SCOPE)

include(AICxxSubmodules)

# Exit if someone tries to contaminates the source directory with an in-source build.
if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
  message(FATAL_ERROR "Please do out-of-source builds.\nCleanup: \"rm -rf CMake CMakeCache.txt CMakeFiles/\"")
endif()

add_subdirectory(src)

install(TARGETS remountd
  RUNTIME DESTINATION "${REMOUNTD_SBINDIR}")

install(TARGETS remountctl
  RUNTIME DESTINATION "${REMOUNTD_BINDIR}")

install(FILES services/remountd.service services/remountd.socket
  DESTINATION "${REMOUNTD_SYSTEMD_UNITDIR}")

install(PROGRAMS scripts/post-install.sh
  DESTINATION "${REMOUNTD_DATADIR}")

install(SCRIPT "${_remountd_install_configs_script}")
