#include "SDCardManager.h"

#include "Logging.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)
#include <SD_MMC.h>
#include <SdMountPath.h>
#include <algorithm>
#else
#include <algorithm>
#endif

namespace {

#if defined(BOARD_ESP32_S3_EPAPER_397)
// Waveshare ESP32-S3-ePaper-3.97 TF slot — SDIO (see docs/ESP32-S3-ePaper-3.97 connections.txt)
constexpr int SD_CLK_PIN = 16;
constexpr int SD_CMD_PIN = 17;  // labeled MOSI on schematic
constexpr int SD_D0_PIN = 15;   // labeled MISO on schematic
constexpr int SD_D1_PIN = 7;
constexpr int SD_D2_PIN = 8;
constexpr int SD_D3_PIN = 18;  // labeled CS on schematic
#else
constexpr uint8_t SD_CS = 12;
constexpr uint32_t SPI_FQ = 40000000;
#endif

}  // namespace

SDCardManager SDCardManager::instance;

SDCardManager::SDCardManager() {
#if !defined(BOARD_ESP32_S3_EPAPER_397)
  // sd default-constructed
#endif
}

bool SDCardManager::begin() {
  bool ok = false;
#if defined(BOARD_ESP32_S3_EPAPER_397)
  SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_D1_PIN, SD_D2_PIN, SD_D3_PIN);
  ok = SD_MMC.begin("/sdcard", true);  // 1-bit SDIO, matches Waveshare 05_SD_Test
  if (ok) {
    const uint64_t cardSize = SD_MMC.cardSize();
    LOG_INF("SD", "SDIO mounted, %llu MB", cardSize / (1024ULL * 1024ULL));
  }
#else
  ok = sd.begin(SD_CS, SPI_FQ);
#endif

  if (!ok) {
    LOG_INF("SD", "SD card not detected");
    initialized = false;
  } else {
    LOG_INF("SD", "SD card detected");
    initialized = true;
    File probe = SD_MMC.open("/");
    if (!probe) {
      LOG_ERR("SD", "Mounted but cannot open SD root");
    } else {
      if (!probe.isDirectory()) {
        LOG_ERR("SD", "SD root is not a directory");
      }
      probe.close();
    }
  }

  return initialized;
}

#if defined(BOARD_ESP32_S3_EPAPER_397)
bool SDCardManager::remount() {
  LOG_DBG("SD", "Remounting SDIO...");
  SD_MMC.end();
  initialized = false;
  return begin();
}
#endif

bool SDCardManager::ready() const { return initialized; }

#if defined(BOARD_ESP32_S3_EPAPER_397)

bool SDCardManager::exists(const char* path) { return SD_MMC.exists(resolveSdMountPath(path).c_str()); }

bool SDCardManager::mkdir(const char* path, const bool pFlag) {
  (void)pFlag;
  return SD_MMC.mkdir(resolveSdMountPath(path).c_str());
}

bool SDCardManager::remove(const char* path) { return SD_MMC.remove(resolveSdMountPath(path).c_str()); }

bool SDCardManager::rmdir(const char* path) { return SD_MMC.rmdir(resolveSdMountPath(path).c_str()); }

bool SDCardManager::rename(const char* path, const char* newPath) {
  return SD_MMC.rename(resolveSdMountPath(path).c_str(), resolveSdMountPath(newPath).c_str());
}

std::vector<String> SDCardManager::listFiles(const char* path, const int maxFiles) {
  std::vector<String> ret;
  if (!initialized) {
    return ret;
  }

  File root = SD_MMC.open(resolveSdMountPath(path).c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return ret;
  }

  int count = 0;
  for (File f = root.openNextFile(); f && count < maxFiles; f = root.openNextFile()) {
    if (f.isDirectory()) {
      f.close();
      continue;
    }
    ret.emplace_back(f.name());
    f.close();
    count++;
  }
  root.close();
  return ret;
}

String SDCardManager::readFile(const char* path) {
  if (!initialized) {
    return "";
  }

  File f = SD_MMC.open(resolveSdMountPath(path).c_str(), FILE_READ);
  if (!f) {
    return "";
  }

  String content;
  constexpr size_t maxSize = 50000;
  size_t readSize = 0;
  while (f.available() && readSize < maxSize) {
    content += static_cast<char>(f.read());
    readSize++;
  }
  f.close();
  return content;
}

bool SDCardManager::readFileToStream(const char* path, Print& out, const size_t chunkSize) {
  if (!initialized) {
    return false;
  }

  File f = SD_MMC.open(resolveSdMountPath(path).c_str(), FILE_READ);
  if (!f) {
    return false;
  }

  constexpr size_t localBufSize = 256;
  uint8_t buf[localBufSize];
  const size_t toRead = (chunkSize == 0) ? localBufSize : std::min(chunkSize, localBufSize);

  while (f.available()) {
    const int r = f.read(buf, toRead);
    if (r > 0) {
      out.write(buf, static_cast<size_t>(r));
    } else {
      break;
    }
  }
  f.close();
  return true;
}

size_t SDCardManager::readFileToBuffer(const char* path, char* buffer, const size_t bufferSize,
                                       const size_t maxBytes) {
  if (!buffer || bufferSize == 0) {
    return 0;
  }
  if (!initialized) {
    buffer[0] = '\0';
    return 0;
  }

  File f = SD_MMC.open(resolveSdMountPath(path).c_str(), FILE_READ);
  if (!f) {
    buffer[0] = '\0';
    return 0;
  }

  const size_t maxToRead = (maxBytes == 0) ? (bufferSize - 1) : std::min(maxBytes, bufferSize - 1);
  size_t total = 0;

  while (f.available() && total < maxToRead) {
    constexpr size_t chunk = 64;
    const size_t want = maxToRead - total;
    const size_t readLen = std::min(want, chunk);
    const int r = f.read(reinterpret_cast<uint8_t*>(buffer + total), readLen);
    if (r > 0) {
      total += static_cast<size_t>(r);
    } else {
      break;
    }
  }

  buffer[total] = '\0';
  f.close();
  return total;
}

bool SDCardManager::writeFile(const char* path, const String& content) {
  if (!initialized) {
    return false;
  }

  const std::string resolved = resolveSdMountPath(path);
  if (SD_MMC.exists(resolved.c_str())) {
    SD_MMC.remove(resolved.c_str());
  }

  File f = SD_MMC.open(resolved.c_str(), FILE_WRITE);
  if (!f) {
    return false;
  }

  const size_t written = f.print(content);
  f.close();
  return written == content.length();
}

bool SDCardManager::ensureDirectoryExists(const char* path) {
  if (!initialized) {
    return false;
  }

  const std::string resolved = resolveSdMountPath(path);
  if (SD_MMC.exists(resolved.c_str())) {
    File dir = SD_MMC.open(resolved.c_str());
    if (dir && dir.isDirectory()) {
      dir.close();
      return true;
    }
    if (dir) dir.close();
  }

  return SD_MMC.mkdir(resolved.c_str());
}

bool SDCardManager::openFileForRead(const char* moduleName, const char* path) {
  if (!initialized) {
    LOG_ERR(moduleName, "SD not ready: %s", path);
    return false;
  }
  const std::string resolved = resolveSdMountPath(path);
  if (!SD_MMC.exists(resolved.c_str())) {
    LOG_ERR(moduleName, "File does not exist: %s", resolved.c_str());
    return false;
  }
  return true;
}

bool SDCardManager::openFileForRead(const char* moduleName, const std::string& path) {
  return openFileForRead(moduleName, path.c_str());
}

bool SDCardManager::openFileForRead(const char* moduleName, const String& path) {
  return openFileForRead(moduleName, path.c_str());
}

bool SDCardManager::openFileForWrite(const char* moduleName, const char* path) {
  (void)moduleName;
  (void)path;
  return initialized;
}

bool SDCardManager::openFileForWrite(const char* moduleName, const std::string& path) {
  return openFileForWrite(moduleName, path.c_str());
}

bool SDCardManager::openFileForWrite(const char* moduleName, const String& path) {
  return openFileForWrite(moduleName, path.c_str());
}

bool SDCardManager::removeDir(const char* path) {
  const std::string resolvedRoot = resolveSdMountPath(path);
  File dir = SD_MMC.open(resolvedRoot.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  for (File file = dir.openNextFile(); file; file = dir.openNextFile()) {
    String filePath = resolvedRoot.c_str();
    if (!filePath.endsWith("/")) {
      filePath += "/";
    }
    filePath += file.name();

    if (file.isDirectory()) {
      if (!removeDir(filePath.c_str())) {
        file.close();
        dir.close();
        return false;
      }
    } else if (!SD_MMC.remove(filePath)) {
      file.close();
      dir.close();
      return false;
    }
    file.close();
  }
  dir.close();
  return SD_MMC.rmdir(resolvedRoot.c_str());
}

#else  // Xteink (SdFat SPI)

std::vector<String> SDCardManager::listFiles(const char* path, const int maxFiles) {
  std::vector<String> ret;
  if (!initialized) {
    if (Serial) Serial.printf("[%lu] [SD] not initialized, returning empty list\n", millis());
    return ret;
  }

  auto root = sd.open(path);
  if (!root) {
    if (Serial) Serial.printf("[%lu] [SD] Failed to open directory\n", millis());
    return ret;
  }
  if (!root.isDirectory()) {
    if (Serial) Serial.printf("[%lu] [SD] Path is not a directory\n", millis());
    root.close();
    return ret;
  }

  int count = 0;
  char name[128];
  for (auto f = root.openNextFile(); f && count < maxFiles; f = root.openNextFile()) {
    if (f.isDirectory()) {
      f.close();
      continue;
    }
    f.getName(name, sizeof(name));
    ret.emplace_back(name);
    f.close();
    count++;
  }
  root.close();
  return ret;
}

String SDCardManager::readFile(const char* path) {
  if (!initialized) {
    if (Serial) Serial.printf("[%lu] [SD] not initialized; cannot read file\n", millis());
    return {""};
  }

  FsFile f;
  if (!openFileForRead("SD", path, f)) {
    return {""};
  }

  String content = "";
  constexpr size_t maxSize = 50000;
  size_t readSize = 0;
  while (f.available() && readSize < maxSize) {
    const char c = static_cast<char>(f.read());
    content += c;
    readSize++;
  }
  f.close();
  return content;
}

bool SDCardManager::readFileToStream(const char* path, Print& out, const size_t chunkSize) {
  if (!initialized) {
    if (Serial) Serial.println("SDCardManager: not initialized; cannot read file");
    return false;
  }

  FsFile f;
  if (!openFileForRead("SD", path, f)) {
    return false;
  }

  constexpr size_t localBufSize = 256;
  uint8_t buf[localBufSize];
  const size_t toRead = (chunkSize == 0) ? localBufSize : (chunkSize < localBufSize ? chunkSize : localBufSize);

  while (f.available()) {
    const int r = f.read(buf, toRead);
    if (r > 0) {
      out.write(buf, static_cast<size_t>(r));
    } else {
      break;
    }
  }

  f.close();
  return true;
}

size_t SDCardManager::readFileToBuffer(const char* path, char* buffer, const size_t bufferSize, const size_t maxBytes) {
  if (!buffer || bufferSize == 0) {
    return 0;
  }
  if (!initialized) {
    if (Serial) Serial.println("SDCardManager: not initialized; cannot read file");
    buffer[0] = '\0';
    return 0;
  }

  FsFile f;
  if (!openFileForRead("SD", path, f)) {
    buffer[0] = '\0';
    return 0;
  }

  const size_t maxToRead = (maxBytes == 0) ? (bufferSize - 1) : min(maxBytes, bufferSize - 1);
  size_t total = 0;

  while (f.available() && total < maxToRead) {
    constexpr size_t chunk = 64;
    const size_t want = maxToRead - total;
    const size_t readLen = (want < chunk) ? want : chunk;
    const int r = f.read(buffer + total, readLen);
    if (r > 0) {
      total += static_cast<size_t>(r);
    } else {
      break;
    }
  }

  buffer[total] = '\0';
  f.close();
  return total;
}

bool SDCardManager::writeFile(const char* path, const String& content) {
  if (!initialized) {
    if (Serial) Serial.println("SDCardManager: not initialized; cannot write file");
    return false;
  }

  if (sd.exists(path)) {
    sd.remove(path);
  }

  FsFile f;
  if (!openFileForWrite("SD", path, f)) {
    if (Serial) Serial.printf("Failed to open file for write: %s\n", path);
    return false;
  }

  const size_t written = f.print(content);
  f.close();
  return written == content.length();
}

bool SDCardManager::ensureDirectoryExists(const char* path) {
  if (!initialized) {
    if (Serial) Serial.println("SDCardManager: not initialized; cannot create directory");
    return false;
  }

  if (sd.exists(path)) {
    FsFile dir = sd.open(path);
    if (dir && dir.isDirectory()) {
      dir.close();
      return true;
    }
    dir.close();
  }

  return sd.mkdir(path);
}

bool SDCardManager::openFileForRead(const char* moduleName, const char* path, FsFile& file) {
  if (!sd.exists(path)) {
    if (Serial) Serial.printf("[%lu] [%s] File does not exist: %s\n", millis(), moduleName, path);
    return false;
  }

  file = sd.open(path, O_RDONLY);
  if (!file) {
    if (Serial) Serial.printf("[%lu] [%s] Failed to open file for reading: %s\n", millis(), moduleName, path);
    return false;
  }
  return true;
}

bool SDCardManager::openFileForRead(const char* moduleName, const std::string& path, FsFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool SDCardManager::openFileForRead(const char* moduleName, const String& path, FsFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool SDCardManager::openFileForWrite(const char* moduleName, const char* path, FsFile& file) {
  file = sd.open(path, O_RDWR | O_CREAT | O_TRUNC);
  if (!file) {
    if (Serial) Serial.printf("[%lu] [%s] Failed to open file for writing: %s\n", millis(), moduleName, path);
    return false;
  }
  return true;
}

bool SDCardManager::openFileForWrite(const char* moduleName, const std::string& path, FsFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool SDCardManager::openFileForWrite(const char* moduleName, const String& path, FsFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool SDCardManager::removeDir(const char* path) {
  auto dir = sd.open(path);
  if (!dir) {
    return false;
  }
  if (!dir.isDirectory()) {
    return false;
  }

  auto file = dir.openNextFile();
  char name[128];
  while (file) {
    String filePath = path;
    if (!filePath.endsWith("/")) {
      filePath += "/";
    }
    file.getName(name, sizeof(name));
    filePath += name;

    if (file.isDirectory()) {
      if (!removeDir(filePath.c_str())) {
        return false;
      }
    } else {
      if (!sd.remove(filePath.c_str())) {
        return false;
      }
    }
    file = dir.openNextFile();
  }

  return sd.rmdir(path);
}

#endif  // BOARD_ESP32_S3_EPAPER_397
