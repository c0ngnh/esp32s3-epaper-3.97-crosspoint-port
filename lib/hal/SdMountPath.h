#pragma once

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <cstring>
#include <string>

// SD_MMC mounted at /sdcard, but SD_MMC.open() prepends the mountpoint internally,
// so callers must pass paths WITHOUT the /sdcard prefix. This helper:
//   - returns "/"  for null/empty/"/" input
//   - strips a leading /sdcard so legacy code that built absolute mount paths still works
//   - collapses any leading duplicate slashes
inline std::string resolveSdMountPath(const char* path) {
  static constexpr char kSdMount[] = "/sdcard";
  if (path == nullptr || path[0] == '\0') {
    return "/";
  }
  const size_t mountLen = sizeof(kSdMount) - 1;
  if (strncmp(path, kSdMount, mountLen) == 0 && (path[mountLen] == '\0' || path[mountLen] == '/')) {
    path += mountLen;
    if (path[0] == '\0') {
      return "/";
    }
  }
  while (path[0] == '/' && path[1] == '/') {
    path++;
  }
  if (path[0] != '/') {
    std::string out;
    out.reserve(strlen(path) + 1);
    out.push_back('/');
    out.append(path);
    return out;
  }
  return std::string(path);
}

#endif
