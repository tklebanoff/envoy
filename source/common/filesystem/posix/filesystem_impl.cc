#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/logger.h"
#include "common/filesystem/filesystem_impl.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Filesystem {

FileImplPosix::~FileImplPosix() {
  if (isOpen()) {
    const Api::IoCallBoolResult result = close();
    ASSERT(result.rc_);
  }
}

void FileImplPosix::openFile() {
  const int flags = O_RDWR | O_APPEND | O_CREAT;
  const int mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

  fd_ = ::open(path_.c_str(), flags, mode);
}

ssize_t FileImplPosix::writeFile(absl::string_view buffer) {
  return ::write(fd_, buffer.data(), buffer.size());
}

bool FileImplPosix::closeFile() { return ::close(fd_) != -1; }

FilePtr InstanceImplPosix::createFile(const std::string& path) {
  return std::make_unique<FileImplPosix>(path);
}

bool InstanceImplPosix::fileExists(const std::string& path) {
  std::ifstream input_file(path);
  return input_file.is_open();
}

bool InstanceImplPosix::directoryExists(const std::string& path) {
  DIR* const dir = ::opendir(path.c_str());
  const bool dir_exists = nullptr != dir;
  if (dir_exists) {
    ::closedir(dir);
  }

  return dir_exists;
}

ssize_t InstanceImplPosix::fileSize(const std::string& path) {
  struct stat info;
  if (::stat(path.c_str(), &info) != 0) {
    return -1;
  }
  return info.st_size;
}

std::string InstanceImplPosix::fileReadToEnd(const std::string& path) {
  if (illegalPath(path)) {
    throw EnvoyException(fmt::format("Invalid path: {}", path));
  }

  std::ios::sync_with_stdio(false);

  std::ifstream file(path);
  if (file.fail()) {
    throw EnvoyException(fmt::format("unable to read file: {}", path));
  }

  std::stringstream file_string;
  file_string << file.rdbuf();

  return file_string.str();
}

bool InstanceImplPosix::illegalPath(const std::string& path) {
  const Api::SysCallStringResult canonical_path = canonicalPath(path);
  if (canonical_path.rc_.empty()) {
    ENVOY_LOG_MISC(debug, "Unable to determine canonical path for {}: {}", path,
                   ::strerror(canonical_path.errno_));
    return true;
  }

  // Platform specific path sanity; we provide a convenience to avoid Envoy
  // instances poking in bad places. We may have to consider conditioning on
  // platform in the future, growing these or relaxing some constraints (e.g.
  // there are valid reasons to go via /proc for file paths).
  // TODO(htuch): Optimize this as a hash lookup if we grow any further.
  if (absl::StartsWith(canonical_path.rc_, "/dev") ||
      absl::StartsWith(canonical_path.rc_, "/sys") ||
      absl::StartsWith(canonical_path.rc_, "/proc")) {
    return true;
  }
  return false;
}

Api::SysCallStringResult InstanceImplPosix::canonicalPath(const std::string& path) {
  // TODO(htuch): When we are using C++17, switch to std::filesystem::canonical.
  char* resolved_path = ::realpath(path.c_str(), nullptr);
  if (resolved_path == nullptr) {
    return {std::string(), errno};
  }
  std::string resolved_path_string{resolved_path};
  ::free(resolved_path);
  return {resolved_path_string, 0};
}

} // namespace Filesystem
} // namespace Envoy
