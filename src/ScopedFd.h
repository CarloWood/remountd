#pragma once

#include <utility>      // std::exchange
#include <unistd.h>     // close()

// ScopedFd
//
// A RAII filedescriptor wrapper.
//
// Usage:
//
//   ScopedFd scoped_fd(valid_fd);
//   ...use scoped_fd.get()...
//
// Closes valid_fd upon destruction.
//
class ScopedFd
{
 private:
  int fd_ = -1;

 public:
  // Construct an invalid fd.
  ScopedFd() = default;

  // Construct a ScopedFd for `fd`.
  // fd must be a valid filedescriptor (>= 0).
  explicit ScopedFd(int fd) : fd_(fd) { }

  // Move constructor.
  ScopedFd(ScopedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) { }

  // Move assignment operator.
  ScopedFd& operator=(ScopedFd&& other) noexcept
  {
    if (this != &other)
    {
      reset();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }

  // Disallow duplication.
  ScopedFd(ScopedFd const&) = delete;
  ScopedFd& operator=(ScopedFd const&) = delete;

  // The destructor closes the underlying fd (if it is valid).
  ~ScopedFd()
  {
    reset();
  }

  // Returns true iff the underlying fd is valid.
  bool valid() const
  {
    return fd_ >= 0;
  }

  // Return the underlying filedescriptor. Do not close it.
  int get() const
  {
    return fd_;
  }

  // Close the underlying fd and reassign a new value.
  // The new value, if any, must be an open valid filedescriptor;
  // if no value is passed then the current object becomes as-if default constructed (invalid).
  void reset(int fd = -1)
  {
    if (fd_ >= 0)
      close(fd_);
    fd_ = fd;
  }

  // Extract the underlying fd: the current object becomes invalid
  // and will no longer close the fd upon destruction.
  int release()
  {
    return std::exchange(fd_, -1);
  }
};
