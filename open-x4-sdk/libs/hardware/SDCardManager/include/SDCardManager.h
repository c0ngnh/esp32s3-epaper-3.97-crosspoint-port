#pragma once

#include <Print.h>
#include <WString.h>
#include <vector>
#include <string>

#if !defined(BOARD_ESP32_S3_EPAPER_397)
#include <SdFat.h>
#endif

class SDCardManager {
 public:
  SDCardManager();
  bool begin();
#if defined(BOARD_ESP32_S3_EPAPER_397)
  bool remount();
#endif
  bool ready() const;

  std::vector<String> listFiles(const char* path = "/", int maxFiles = 200);
  String readFile(const char* path);
  bool readFileToStream(const char* path, Print& out, size_t chunkSize = 256);
  size_t readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes = 0);
  bool writeFile(const char* path, const String& content);
  bool ensureDirectoryExists(const char* path);

#if !defined(BOARD_ESP32_S3_EPAPER_397)
  FsFile open(const char* path, const oflag_t oflag = O_RDONLY) { return sd.open(path, oflag); }
  bool mkdir(const char* path, const bool pFlag = true) { return sd.mkdir(path, pFlag); }
  bool exists(const char* path) { return sd.exists(path); }
  bool remove(const char* path) { return sd.remove(path); }
  bool rmdir(const char* path) { return sd.rmdir(path); }
  bool rename(const char* path, const char* newPath) { return sd.rename(path, newPath); }
#endif

#if defined(BOARD_ESP32_S3_EPAPER_397)
  bool mkdir(const char* path, const bool pFlag = true);
  bool exists(const char* path);
  bool remove(const char* path);
  bool rmdir(const char* path);
  bool rename(const char* path, const char* newPath);
#endif

#if defined(BOARD_ESP32_S3_EPAPER_397)
  bool openFileForRead(const char* moduleName, const char* path);
  bool openFileForRead(const char* moduleName, const std::string& path);
  bool openFileForRead(const char* moduleName, const String& path);
  bool openFileForWrite(const char* moduleName, const char* path);
  bool openFileForWrite(const char* moduleName, const std::string& path);
  bool openFileForWrite(const char* moduleName, const String& path);
#else
  bool openFileForRead(const char* moduleName, const char* path, FsFile& file);
  bool openFileForRead(const char* moduleName, const std::string& path, FsFile& file);
  bool openFileForRead(const char* moduleName, const String& path, FsFile& file);
  bool openFileForWrite(const char* moduleName, const char* path, FsFile& file);
  bool openFileForWrite(const char* moduleName, const std::string& path, FsFile& file);
  bool openFileForWrite(const char* moduleName, const String& path, FsFile& file);
#endif
  bool removeDir(const char* path);

  static SDCardManager& getInstance() { return instance; }

 private:
  static SDCardManager instance;

  bool initialized = false;
#if !defined(BOARD_ESP32_S3_EPAPER_397)
  SdFat sd;
#endif
};

#define SdMan SDCardManager::getInstance()
