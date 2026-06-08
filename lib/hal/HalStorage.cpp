#define HAL_STORAGE_IMPL
#include "HalStorage.h"

#include <FS.h>  // need to be included before SdFat.h for compatibility with FS.h's File class
#include <Logging.h>
#include <SDCardManager.h>

#include <cassert>
#include <cstring>

#if defined(BOARD_ESP32_S3_EPAPER_397)
#include <SD_MMC.h>
#include <SdMountPath.h>
#endif

#define SDCard SDCardManager::getInstance()

HalStorage HalStorage::instance;

HalStorage::HalStorage() {
  storageMutex = xSemaphoreCreateMutex();
  assert(storageMutex != nullptr);
}

bool HalStorage::begin() { return SDCard.begin(); }

bool HalStorage::ready() const { return SDCard.ready(); }

class HalStorage::StorageLock {
 public:
  StorageLock() { xSemaphoreTake(HalStorage::getInstance().storageMutex, portMAX_DELAY); }
  ~StorageLock() { xSemaphoreGive(HalStorage::getInstance().storageMutex); }
};

#define HAL_STORAGE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;               \
  return SDCard.method(__VA_ARGS__);

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  HAL_STORAGE_WRAPPED_CALL(listFiles, path, maxFiles);
}

String HalStorage::readFile(const char* path) { HAL_STORAGE_WRAPPED_CALL(readFile, path); }

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  HAL_STORAGE_WRAPPED_CALL(readFileToStream, path, out, chunkSize);
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  HAL_STORAGE_WRAPPED_CALL(readFileToBuffer, path, buffer, bufferSize, maxBytes);
}

bool HalStorage::writeFile(const char* path, const String& content) {
  HAL_STORAGE_WRAPPED_CALL(writeFile, path, content);
}

bool HalStorage::ensureDirectoryExists(const char* path) { HAL_STORAGE_WRAPPED_CALL(ensureDirectoryExists, path); }

bool HalStorage::mkdir(const char* path, const bool pFlag) { HAL_STORAGE_WRAPPED_CALL(mkdir, path, pFlag); }

bool HalStorage::exists(const char* path) { HAL_STORAGE_WRAPPED_CALL(exists, path); }

bool HalStorage::remove(const char* path) { HAL_STORAGE_WRAPPED_CALL(remove, path); }

bool HalStorage::rename(const char* oldPath, const char* newPath) {
  HAL_STORAGE_WRAPPED_CALL(rename, oldPath, newPath);
}

bool HalStorage::rmdir(const char* path) { HAL_STORAGE_WRAPPED_CALL(rmdir, path); }

bool HalStorage::removeDir(const char* path) { HAL_STORAGE_WRAPPED_CALL(removeDir, path); }

#if defined(BOARD_ESP32_S3_EPAPER_397)

namespace {

const char* openModeFromOfalg(const oflag_t oflag) {
  if (oflag & O_APPEND) {
    return FILE_APPEND;
  }
  if ((oflag & O_RDWR) == O_RDWR || (oflag & O_WRONLY)) {
    return FILE_WRITE;
  }
  return FILE_READ;
}

}  // namespace

class HalFile::Impl {
 public:
  File file;
  String path;

  Impl() = default;
  explicit Impl(File&& f, String p = "") : file(std::move(f)), path(std::move(p)) {}
};

HalFile::HalFile() = default;

HalFile::HalFile(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}

HalFile::~HalFile() = default;

HalFile::HalFile(HalFile&&) = default;

HalFile& HalFile::operator=(HalFile&&) = default;

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  StorageLock lock;
  const std::string resolved = resolveSdMountPath(path);
  // SDIO VFS: FILE_READ open fails on directories; modeless open works (see SDCardManager::listFiles).
  const bool writeIntent = (oflag & O_APPEND) != 0 || (oflag & O_RDWR) == O_RDWR || (oflag & O_WRONLY) != 0;
  File f = writeIntent ? SD_MMC.open(resolved.c_str(), openModeFromOfalg(oflag)) : SD_MMC.open(resolved.c_str());
  return HalFile(std::make_unique<HalFile::Impl>(std::move(f), resolved.c_str()));
}

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;
  if (!SDCard.openFileForRead(moduleName, path)) {
    return false;
  }
  const std::string resolved = resolveSdMountPath(path);
  File f = SD_MMC.open(resolved.c_str(), FILE_READ);
  if (!f) {
    LOG_ERR("HAL", "[%s] Failed to open file for reading: %s", moduleName, resolved.c_str());
    return false;
  }
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(f), resolved.c_str()));
  return true;
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;
  if (!SDCard.openFileForWrite(moduleName, path)) {
    return false;
  }
  const std::string resolved = resolveSdMountPath(path);
  File f = SD_MMC.open(resolved.c_str(), FILE_WRITE);
  if (!f) {
    LOG_ERR("HAL", "[%s] Failed to open file for writing: %s", moduleName, resolved.c_str());
    return false;
  }
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(f), resolved.c_str()));
  return true;
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

void HalFile::flush() {
  HalStorage::StorageLock lock;
  if (impl && impl->file) {
    impl->file.flush();
  }
}

size_t HalFile::getName(char* name, const size_t len) {
  if (!impl || !impl->file || !name || len == 0) {
    return 0;
  }
  const char* n = impl->file.name();
  if (!n) {
    name[0] = '\0';
    return 0;
  }
  strncpy(name, n, len - 1);
  name[len - 1] = '\0';
  return strlen(name);
}

size_t HalFile::size() {
  if (!impl || !impl->file) {
    return 0;
  }
  return impl->file.size();
}

size_t HalFile::fileSize() { return size(); }

uint64_t HalFile::fileSize64() { return size(); }

bool HalFile::seek(const size_t pos) { return seekSet(pos); }

bool HalFile::seek64(const uint64_t pos) { return seekSet(static_cast<size_t>(pos)); }

bool HalFile::seekCur(const int64_t offset) {
  HalStorage::StorageLock lock;
  if (!impl || !impl->file) {
    return false;
  }
  return impl->file.seek(static_cast<uint32_t>(offset), SeekCur);
}

bool HalFile::seekSet(const size_t offset) {
  HalStorage::StorageLock lock;
  if (!impl || !impl->file) {
    return false;
  }
  return impl->file.seek(offset, SeekSet);
}

int HalFile::available() const {
  HalStorage::StorageLock lock;
  if (!impl || !impl->file) {
    return 0;
  }
  return impl->file.available();
}

size_t HalFile::position() const {
  HalStorage::StorageLock lock;
  if (!impl || !impl->file) {
    return 0;
  }
  return impl->file.position();
}

int HalFile::read(void* buf, const size_t count) {
  HalStorage::StorageLock lock;
  if (!impl || !impl->file || !buf) {
    return 0;
  }
  return impl->file.read(static_cast<uint8_t*>(buf), count);
}

int HalFile::read() {
  HalStorage::StorageLock lock;
  if (!impl || !impl->file) {
    return -1;
  }
  return impl->file.read();
}

size_t HalFile::write(const void* buf, const size_t count) {
  HalStorage::StorageLock lock;
  if (!impl || !impl->file || !buf) {
    return 0;
  }
  return impl->file.write(static_cast<const uint8_t*>(buf), count);
}

size_t HalFile::write(const uint8_t b) {
  HalStorage::StorageLock lock;
  if (!impl || !impl->file) {
    return 0;
  }
  return impl->file.write(b);
}

bool HalFile::rename(const char* newPath) {
  HalStorage::StorageLock lock;
  if (!impl || impl->path.isEmpty()) {
    return false;
  }
  const String oldPath = impl->path;
  close();
  return SD_MMC.rename(resolveSdMountPath(oldPath.c_str()).c_str(), resolveSdMountPath(newPath).c_str());
}

bool HalFile::isDirectory() const {
  if (!impl || !impl->file) {
    return false;
  }
  return impl->file.isDirectory();
}

void HalFile::rewindDirectory() {
  HalStorage::StorageLock lock;
  if (impl && impl->file) {
    impl->file.rewindDirectory();
  }
}

bool HalFile::close() {
  HalStorage::StorageLock lock;
  if (!impl || !impl->file) {
    return true;
  }
  impl->file.close();
  impl->path = "";
  return true;
}

HalFile HalFile::openNextFile() {
  HalStorage::StorageLock lock;
  if (!impl || !impl->file) {
    return HalFile();
  }
  File nf = impl->file.openNextFile();
  return HalFile(std::make_unique<Impl>(std::move(nf)));
}

bool HalFile::isOpen() const { return impl != nullptr && static_cast<bool>(impl->file); }

HalFile::operator bool() const { return isOpen(); }

#else  // SdFat / Xteink

class HalFile::Impl {
 public:
  Impl(FsFile&& fsFile) : file(std::move(fsFile)) {}
  FsFile file;
};

HalFile::HalFile() = default;

HalFile::HalFile(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}

HalFile::~HalFile() = default;

HalFile::HalFile(HalFile&&) = default;

HalFile& HalFile::operator=(HalFile&&) = default;

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  StorageLock lock;
  return HalFile(std::make_unique<HalFile::Impl>(SDCard.open(path, oflag)));
}

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;
  FsFile fsFile;
  const bool ok = SDCard.openFileForRead(moduleName, path, fsFile);
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile)));
  return ok;
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;
  FsFile fsFile;
  const bool ok = SDCard.openFileForWrite(moduleName, path, fsFile);
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile)));
  return ok;
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

#define HAL_FILE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;            \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

#define HAL_FILE_FORWARD_CALL(method, ...) \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

void HalFile::flush() { HAL_FILE_WRAPPED_CALL(flush, ); }
size_t HalFile::getName(char* name, size_t len) { HAL_FILE_WRAPPED_CALL(getName, name, len); }
size_t HalFile::size() { HAL_FILE_FORWARD_CALL(size, ); }
size_t HalFile::fileSize() { HAL_FILE_FORWARD_CALL(fileSize, ); }
uint64_t HalFile::fileSize64() { HAL_FILE_FORWARD_CALL(fileSize, ); }
bool HalFile::seek(size_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seek64(uint64_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seekCur(int64_t offset) { HAL_FILE_WRAPPED_CALL(seekCur, offset); }
bool HalFile::seekSet(size_t offset) { HAL_FILE_WRAPPED_CALL(seekSet, offset); }
int HalFile::available() const { HAL_FILE_WRAPPED_CALL(available, ); }
size_t HalFile::position() const { HAL_FILE_WRAPPED_CALL(position, ); }
int HalFile::read(void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(read, buf, count); }
int HalFile::read() { HAL_FILE_WRAPPED_CALL(read, ); }
size_t HalFile::write(const void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(write, buf, count); }
size_t HalFile::write(uint8_t b) { HAL_FILE_WRAPPED_CALL(write, b); }
bool HalFile::rename(const char* newPath) { HAL_FILE_WRAPPED_CALL(rename, newPath); }
bool HalFile::isDirectory() const { HAL_FILE_FORWARD_CALL(isDirectory, ); }
void HalFile::rewindDirectory() { HAL_FILE_WRAPPED_CALL(rewindDirectory, ); }
bool HalFile::close() { HAL_FILE_WRAPPED_CALL(close, ); }
HalFile HalFile::openNextFile() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  return HalFile(std::make_unique<Impl>(impl->file.openNextFile()));
}
bool HalFile::isOpen() const { return impl != nullptr && impl->file.isOpen(); }
HalFile::operator bool() const { return isOpen(); }

#endif  // BOARD_ESP32_S3_EPAPER_397
