#pragma once

#include <string>
#include <cstdint>
#include <utility>

namespace remountd {

// ApplicationInfo
//
// Stores high-level metadata for an application instance.
class ApplicationInfo
{
 private:
  std::u8string application_name_;        // Human-readable application name.
  uint32_t encoded_version_ = 0;          // Encoded application version.

 public:
  // Encode major.minor version.
  static constexpr uint32_t encode_version(uint32_t major, uint32_t minor)
  {
    return (major << 16u) | (minor & 0xffffu);
  }

  // Decoded encoded version.
  static constexpr std::pair<uint32_t, uint32_t> decode_version(uint32_t encoded_version)
  {
    return {encoded_version >> 16u, encoded_version & 0xffffu };
  }

  // Set the application display name.
  void set_application_name(std::u8string const& application_name)
  {
    application_name_ = application_name;
  }

  // Set the encoded application version.
  void set_application_version(uint32_t encoded_version)
  {
    encoded_version_ = encoded_version;
  }

  // Accessors.

  std::u8string const& application_name() const { return application_name_; }
  uint32_t encoded_version() const { return encoded_version_; }

  // Return the decoded application version.
  constexpr std::pair<uint32_t, uint32_t> version() const
  {
    return decode_version(encoded_version_);
  }
};

} // namespace remountd
