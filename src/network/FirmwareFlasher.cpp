#include "FirmwareFlasher.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include <spi_flash_mmap.h>
#if defined(BOARD_ESP32_S3_EPAPER_397)
#include <WiFi.h>
#endif

#include <algorithm>
#include <cstring>
#include <memory>

#include "OtaBootSwitch.h"

namespace firmware_flash {

namespace {
constexpr uint8_t ESP_IMAGE_MAGIC = 0xE9;
constexpr size_t MIN_FIRMWARE_SIZE = 64 * 1024;
constexpr size_t SEC = SPI_FLASH_SEC_SIZE;  // 4 KiB
constexpr size_t BLK = 64 * 1024;           // 64 KiB block-erase granularity
constexpr size_t CHUNK = 4096;
constexpr size_t SHA_TRAILER = 32;
constexpr uint8_t CHECKSUM_SEED = 0xEF;
constexpr size_t HEADER_SIZE = 24;
constexpr size_t SEG_HEADER_SIZE = 8;
constexpr size_t PARTITION_TABLE_OFFSET = 0x8000;
constexpr size_t MERGED_APP_OFFSET = 0x10000;
constexpr size_t PARTITION_ENTRY_SIZE = 32;
constexpr uint8_t ESP_PARTITION_TYPE_DATA = 0x01;
constexpr uint8_t ESP_PARTITION_SUBTYPE_DATA_NVS = 0x02;
}  // namespace

namespace {
bool readAtOffset(HalFile& file, size_t offset, void* dest, size_t len) {
  if (!file.seek(offset)) {
    return false;
  }
  return file.read(static_cast<uint8_t*>(dest), len) == static_cast<int>(len);
}

bool looksLikeNvsPartitionEntry(const uint8_t entry[PARTITION_ENTRY_SIZE]) {
  if (std::memcmp(entry, "nvs", 3) != 0) {
    return false;
  }
  if (entry[16] != ESP_PARTITION_TYPE_DATA || entry[17] != ESP_PARTITION_SUBTYPE_DATA_NVS) {
    return false;
  }
  uint32_t offset = 0;
  uint32_t size = 0;
  std::memcpy(&offset, entry + 18, sizeof(offset));
  std::memcpy(&size, entry + 22, sizeof(size));
  return offset == 0x9000 && size == 0x5000;
}

bool looksLikeMergedFactoryImage(HalFile& file, size_t fileSize) {
  if (fileSize < MERGED_APP_OFFSET + 1) {
    return false;
  }

  uint8_t partitionEntry[PARTITION_ENTRY_SIZE] = {};
  uint8_t appMagic = 0;
  if (!readAtOffset(file, PARTITION_TABLE_OFFSET, partitionEntry, sizeof(partitionEntry))) {
    return false;
  }
  if (!readAtOffset(file, MERGED_APP_OFFSET, &appMagic, sizeof(appMagic))) {
    return false;
  }

  return looksLikeNvsPartitionEntry(partitionEntry) && appMagic == ESP_IMAGE_MAGIC;
}

bool looksLikeAppImageHeader(const uint8_t header[HEADER_SIZE]) {
  if (header[0] != ESP_IMAGE_MAGIC) {
    return false;
  }
  const uint8_t segCount = header[1];
  return segCount >= 1 && segCount <= 16;
}
}  // namespace

const char* resultName(Result r) {
  switch (r) {
    case Result::OK:
      return "OK";
    case Result::OPEN_FAIL:
      return "OPEN_FAIL";
    case Result::TOO_SMALL:
      return "TOO_SMALL";
    case Result::TOO_LARGE:
      return "TOO_LARGE";
    case Result::BAD_MAGIC:
      return "BAD_MAGIC";
    case Result::BAD_SEGMENTS:
      return "BAD_SEGMENTS";
    case Result::BAD_CHECKSUM:
      return "BAD_CHECKSUM";
    case Result::BAD_SHA:
      return "BAD_SHA";
    case Result::BAD_SIZE:
      return "BAD_SIZE";
    case Result::WRONG_IMAGE_TYPE:
      return "WRONG_IMAGE_TYPE";
    case Result::NO_PARTITION:
      return "NO_PARTITION";
    case Result::INPLACE_NOT_SUPPORTED:
      return "INPLACE_NOT_SUPPORTED";
    case Result::OOM:
      return "OOM";
    case Result::READ_FAIL:
      return "READ_FAIL";
    case Result::ERASE_FAIL:
      return "ERASE_FAIL";
    case Result::WRITE_FAIL:
      return "WRITE_FAIL";
    case Result::OTADATA_FAIL:
      return "OTADATA_FAIL";
  }
  return "?";
}

namespace {
// Stream `length` bytes from `file` starting at the current read offset, feeding them through
// both the XOR-checksum and SHA256 accumulators. Used by validateImageFile so the whole image
// is verified end-to-end without holding it in RAM (ESP32-C3 only has ~380 KB).
Result feedHashAndChecksum(HalFile& file, size_t length, uint8_t* xorAccum, mbedtls_sha256_context* sha, uint8_t* buf) {
  size_t remaining = length;
  while (remaining > 0) {
    const size_t want = std::min<size_t>(CHUNK, remaining);
    const int got = file.read(buf, want);
    if (got <= 0 || static_cast<size_t>(got) != want) return Result::READ_FAIL;
    if (sha) mbedtls_sha256_update(sha, buf, want);
    if (xorAccum) {
      uint8_t acc = *xorAccum;
      for (size_t i = 0; i < want; i++) acc ^= buf[i];
      *xorAccum = acc;
    }
    remaining -= want;
  }
  return Result::OK;
}
}  // namespace

Result validateImageFile(const char* sdPath, size_t partitionSize) {
  HalFile file;
  if (!Storage.openFileForRead("FLASH", sdPath, file) || !file) {
    LOG_ERR("FLASH", "validate: open failed: %s", sdPath);
    return Result::OPEN_FAIL;
  }

  const size_t fileSize = file.fileSize();
  if (fileSize < MIN_FIRMWARE_SIZE) {
    LOG_ERR("FLASH", "validate: too small: %u", static_cast<unsigned>(fileSize));
    file.close();
    return Result::TOO_SMALL;
  }
  if (partitionSize > 0 && fileSize > partitionSize) {
    LOG_ERR("FLASH", "validate: too large: %u > %u", static_cast<unsigned>(fileSize),
            static_cast<unsigned>(partitionSize));
    file.close();
    return Result::TOO_LARGE;
  }

  uint8_t header[HEADER_SIZE];
  if (file.read(header, HEADER_SIZE) != static_cast<int>(HEADER_SIZE)) {
    LOG_ERR("FLASH", "validate: header read failed");
    file.close();
    return Result::READ_FAIL;
  }
  if (header[0] != ESP_IMAGE_MAGIC) {
    LOG_ERR("FLASH", "validate: bad magic 0x%02X", header[0]);
    file.close();
    return Result::BAD_MAGIC;
  }
  const uint8_t segCount = header[1];
  const bool hashAppended = header[23] != 0;

  auto buf = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[CHUNK]);
  if (!buf) {
    file.close();
    return Result::OOM;
  }

  mbedtls_sha256_context shaCtx;
  mbedtls_sha256_init(&shaCtx);
  mbedtls_sha256_starts(&shaCtx, /*is224=*/0);
  mbedtls_sha256_update(&shaCtx, header, HEADER_SIZE);

  uint8_t xorAccum = CHECKSUM_SEED;
  size_t pos = HEADER_SIZE;

  for (uint8_t i = 0; i < segCount; i++) {
    if (pos + SEG_HEADER_SIZE > fileSize) {
      LOG_ERR("FLASH", "validate: seg %u header overruns EOF at %u", i, static_cast<unsigned>(pos));
      mbedtls_sha256_free(&shaCtx);
      file.close();
      return Result::BAD_SEGMENTS;
    }
    uint8_t segHdr[SEG_HEADER_SIZE];
    if (file.read(segHdr, SEG_HEADER_SIZE) != static_cast<int>(SEG_HEADER_SIZE)) {
      mbedtls_sha256_free(&shaCtx);
      file.close();
      return Result::READ_FAIL;
    }
    mbedtls_sha256_update(&shaCtx, segHdr, SEG_HEADER_SIZE);
    pos += SEG_HEADER_SIZE;

    uint32_t dataLen;
    std::memcpy(&dataLen, segHdr + 4, sizeof(dataLen));
    if (pos + dataLen > fileSize) {
      LOG_ERR("FLASH", "validate: seg %u data overruns EOF (%u + %u > %u)", i, static_cast<unsigned>(pos),
              static_cast<unsigned>(dataLen), static_cast<unsigned>(fileSize));
      mbedtls_sha256_free(&shaCtx);
      file.close();
      return Result::BAD_SEGMENTS;
    }

    const Result feedRes = feedHashAndChecksum(file, dataLen, &xorAccum, &shaCtx, buf.get());
    if (feedRes != Result::OK) {
      mbedtls_sha256_free(&shaCtx);
      file.close();
      return feedRes;
    }
    pos += dataLen;
  }

  // pad_end is the 16-byte aligned offset at which the checksum byte sits at pad_end - 1.
  const size_t padEnd = (pos + 16) & ~static_cast<size_t>(15);
  const size_t expectedTotal = padEnd + (hashAppended ? SHA_TRAILER : 0);
  if (expectedTotal != fileSize) {
    LOG_ERR("FLASH", "validate: size mismatch body+pad=%u sha=%u expected=%u actual=%u", static_cast<unsigned>(padEnd),
            static_cast<unsigned>(hashAppended ? SHA_TRAILER : 0), static_cast<unsigned>(expectedTotal),
            static_cast<unsigned>(fileSize));
    mbedtls_sha256_free(&shaCtx);
    file.close();
    return Result::BAD_SIZE;
  }

  // Read the padding bytes (which include the stored checksum at the last byte) into the SHA stream.
  const size_t padLen = padEnd - pos;
  uint8_t padBuf[16];
  if (padLen > sizeof(padBuf)) {
    mbedtls_sha256_free(&shaCtx);
    file.close();
    return Result::BAD_SIZE;
  }
  if (padLen > 0 && file.read(padBuf, padLen) != static_cast<int>(padLen)) {
    mbedtls_sha256_free(&shaCtx);
    file.close();
    return Result::READ_FAIL;
  }
  mbedtls_sha256_update(&shaCtx, padBuf, padLen);

  const uint8_t storedChecksum = padBuf[padLen - 1];
  if ((xorAccum & 0xFF) != storedChecksum) {
    LOG_ERR("FLASH", "validate: checksum mismatch computed=0x%02X stored=0x%02X", xorAccum, storedChecksum);
    mbedtls_sha256_free(&shaCtx);
    file.close();
    return Result::BAD_CHECKSUM;
  }

  if (hashAppended) {
    uint8_t computed[SHA_TRAILER];
    mbedtls_sha256_finish(&shaCtx, computed);
    uint8_t stored[SHA_TRAILER];
    if (file.read(stored, SHA_TRAILER) != static_cast<int>(SHA_TRAILER)) {
      mbedtls_sha256_free(&shaCtx);
      file.close();
      return Result::READ_FAIL;
    }
    if (std::memcmp(computed, stored, SHA_TRAILER) != 0) {
      LOG_ERR("FLASH", "validate: SHA256 mismatch");
      mbedtls_sha256_free(&shaCtx);
      file.close();
      return Result::BAD_SHA;
    }
  }

  mbedtls_sha256_free(&shaCtx);
  file.close();
  return Result::OK;
}

ImageKind classifyFirmwareFile(const char* sdPath) {
  HalFile file;
  if (!Storage.openFileForRead("FLASH", sdPath, file) || !file) {
    LOG_ERR("FLASH", "classify: open failed: %s", sdPath);
    return ImageKind::INVALID;
  }

  const size_t fileSize = file.fileSize();
  if (fileSize < MIN_FIRMWARE_SIZE) {
    file.close();
    return ImageKind::INVALID;
  }

  if (looksLikeMergedFactoryImage(file, fileSize)) {
    LOG_ERR("FLASH", "classify: merged USB full-flash image (partition table @0x8000)");
    file.close();
    return ImageKind::MERGED_FACTORY;
  }

  uint8_t header[HEADER_SIZE] = {};
  if (!readAtOffset(file, 0, header, sizeof(header))) {
    file.close();
    return ImageKind::INVALID;
  }

  if (!looksLikeAppImageHeader(header)) {
    LOG_ERR("FLASH", "classify: not an app update image (magic=0x%02X)", header[0]);
    file.close();
    return ImageKind::INVALID;
  }

  file.close();
  return ImageKind::APP_UPDATE;
}

bool isSdUpdateImage(const char* sdPath) {
  return classifyFirmwareFile(sdPath) == ImageKind::APP_UPDATE;
}

Result validateSdUpdateImage(const char* sdPath, size_t partitionSize) {
  switch (classifyFirmwareFile(sdPath)) {
    case ImageKind::MERGED_FACTORY:
      return Result::WRONG_IMAGE_TYPE;
    case ImageKind::INVALID:
      return Result::BAD_MAGIC;
    case ImageKind::APP_UPDATE:
      break;
  }
  return validateImageFile(sdPath, partitionSize);
}

const esp_partition_t* getUpdatePartition() {
  if (!hasDualOtaAppPartitions()) {
    LOG_ERR("FLASH", "dual OTA app partitions required for SD update");
    return nullptr;
  }

  const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr);
  if (!dest) {
    LOG_ERR("FLASH", "no inactive OTA app partition");
    return nullptr;
  }

  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running && dest->address == running->address) {
    LOG_ERR("FLASH", "refusing in-place SD update of running partition %s", dest->label);
    return nullptr;
  }
  return dest;
}

bool hasDualOtaAppPartitions() {
  const esp_partition_t* ota0 = esp_partition_find_first(static_cast<esp_partition_type_t>(ESP_PARTITION_TYPE_APP),
                                                         static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0),
                                                         nullptr);
  const esp_partition_t* ota1 = esp_partition_find_first(static_cast<esp_partition_type_t>(ESP_PARTITION_TYPE_APP),
                                                         static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_1),
                                                         nullptr);
  return ota0 && ota1;
}

void prepareForFlashWrite() {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  WiFi.disconnect(true);
  delay(50);
  WiFi.mode(WIFI_OFF);
  delay(50);
#endif
}

namespace {
bool isRunningPartition(const esp_partition_t* dest) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  return running && dest && dest->address == running->address;
}

Result flashForward(HalFile& file, const esp_partition_t* dest, size_t firmwareSize, uint8_t* buffer,
                    ProgressCb onProgress, void* ctx) {
  size_t streamPos = 0;
  size_t erasedUpto = 0;
  while (streamPos < firmwareSize) {
    if (streamPos >= erasedUpto) {
      size_t eraseLen = std::min<size_t>(BLK, dest->size - streamPos);
      eraseLen = (eraseLen + SEC - 1) & ~(SEC - 1);
      eraseLen = std::min<size_t>(eraseLen, dest->size - streamPos);
      if (esp_partition_erase_range(dest, streamPos, eraseLen) != ESP_OK) {
        LOG_ERR("FLASH", "erase @%u (len=%u) failed", static_cast<unsigned>(streamPos),
                static_cast<unsigned>(eraseLen));
        return Result::ERASE_FAIL;
      }
      erasedUpto = streamPos + eraseLen;
    }

    const size_t want = std::min<size_t>(CHUNK, firmwareSize - streamPos);
    const int read = file.read(buffer, want);
    if (read <= 0 || static_cast<size_t>(read) != want) {
      LOG_ERR("FLASH", "read @%u: got=%d want=%u", static_cast<unsigned>(streamPos), read, static_cast<unsigned>(want));
      return Result::READ_FAIL;
    }
    if (esp_partition_write(dest, streamPos, buffer, want) != ESP_OK) {
      LOG_ERR("FLASH", "write @%u failed", static_cast<unsigned>(streamPos));
      return Result::WRITE_FAIL;
    }
    streamPos += want;
    if (onProgress) onProgress(streamPos, firmwareSize, ctx);
    delay(1);
  }
  return Result::OK;
}
}  // namespace

namespace {
bool hasOtadataPartition() {
  return esp_partition_find_first(static_cast<esp_partition_type_t>(ESP_PARTITION_TYPE_DATA),
                                  static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_DATA_OTA),
                                  nullptr) != nullptr;
}
}  // namespace

Result flashFromSdPath(const char* sdPath, ProgressCb onProgress, void* ctx, bool alreadyValidated) {
  // Resolve destination first so we can size-check during validation. The full image-integrity
  // pass below verifies header, segment table, XOR checksum and SHA256 trailer end-to-end before
  // we touch otadata, so a truncated/corrupted .bin can never become the next boot target.
  const esp_partition_t* dest = getUpdatePartition();
  if (!dest) {
    LOG_ERR("FLASH", "no next-update partition");
    return hasDualOtaAppPartitions() ? Result::NO_PARTITION : Result::INPLACE_NOT_SUPPORTED;
  }
  if (isRunningPartition(dest)) {
    LOG_ERR("FLASH", "SD update cannot overwrite the running app partition");
    return Result::INPLACE_NOT_SUPPORTED;
  }

  prepareForFlashWrite();

  // When the caller already ran validateImageFile() against this same partition
  // size (e.g. SdFirmwareUpdateActivity validates before the confirmation
  // prompt), skip the redundant integrity scan. We still keep the partition
  // lookup so the rest of the flashing path stays unchanged.
  if (!alreadyValidated) {
    const Result validateRes = validateSdUpdateImage(sdPath, dest->size);
    if (validateRes != Result::OK) {
      LOG_ERR("FLASH", "image validation failed: %s", resultName(validateRes));
      return validateRes;
    }
  }

  HalFile file;
  if (!Storage.openFileForRead("FLASH", sdPath, file) || !file) {
    LOG_ERR("FLASH", "open failed: %s", sdPath);
    return Result::OPEN_FAIL;
  }

  const size_t firmwareSize = file.fileSize();
  LOG_INF("FLASH", "src=%s size=%u dest=%s @0x%x partsize=%u", sdPath, static_cast<unsigned>(firmwareSize), dest->label,
          static_cast<unsigned>(dest->address), static_cast<unsigned>(dest->size));

  auto buffer = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[CHUNK]);
  if (!buffer) {
    LOG_ERR("FLASH", "OOM");
    file.close();
    return Result::OOM;
  }

  const Result flashRes = flashForward(file, dest, firmwareSize, buffer.get(), onProgress, ctx);
  file.close();
  if (flashRes != Result::OK) {
    return flashRes;
  }

  if (hasOtadataPartition()) {
    if (!ota_boot::switchTo(dest)) {
      LOG_ERR("FLASH", "otadata switch failed");
      return Result::OTADATA_FAIL;
    }
  } else {
    LOG_ERR("FLASH", "otadata partition missing after dual-bank flash");
    return Result::OTADATA_FAIL;
  }
  return Result::OK;
}

}  // namespace firmware_flash
