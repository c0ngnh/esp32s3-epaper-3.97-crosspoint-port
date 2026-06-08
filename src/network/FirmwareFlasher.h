#pragma once

#include <esp_partition.h>

#include <cstddef>
#include <cstdint>

// Flash a firmware image from an SD-card path into the next OTA app
// partition, then switch otadata so the X3/X4 stock bootloader picks it up
// on next boot. Mirrors the web flasher: raw esp_partition_erase_range +
// esp_partition_write + ota_boot::switchTo (no Arduino Update class, no
// esp_image_verify — those reject our patched image on X4 silicon).
//
// Both the SD update activity and the OTA path land here. OTA first
// downloads the firmware to an SD-card cache file, then calls this.

namespace firmware_flash {

enum class ImageKind {
  APP_UPDATE,      // app-only image suitable for SD / OTA partition write
  MERGED_FACTORY,  // USB full-flash image (bootloader + partitions + app)
  INVALID,         // unreadable or not a CrossPoint/ESP firmware image
};

enum class Result {
  OK,
  OPEN_FAIL,
  TOO_SMALL,
  TOO_LARGE,
  BAD_MAGIC,
  BAD_SEGMENTS,  // segment table malformed or runs past EOF
  BAD_CHECKSUM,  // ESP image XOR checksum mismatch
  BAD_SHA,       // SHA256 trailer mismatch (hash_appended images)
  BAD_SIZE,      // body+pad+sha length doesn't match file size
  WRONG_IMAGE_TYPE,  // merged USB full-flash image, not SD app update
  NO_PARTITION,
  INPLACE_NOT_SUPPORTED,  // single-bank layout — USB full flash required first
  OOM,
  READ_FAIL,
  ERASE_FAIL,
  WRITE_FAIL,
  OTADATA_FAIL,
};

// Progress callback: called after every chunk write. `written`/`total` are bytes.
using ProgressCb = void (*)(size_t written, size_t total, void* ctx);

// Open `sdPath`, validate it looks like an ESP32 image, then stream it into the
// update app partition with interleaved 64 KiB erase + sector writes. On success
// switches otadata when present (dual-bank). Single-bank layouts are rejected —
// SD update must write the inactive OTA slot, not the running app partition.
// ESP.restart() afterwards.
//
// `alreadyValidated` lets callers that have just run `validateImageFile()`
// themselves (e.g. SdFirmwareUpdateActivity, which validates before showing
// the user the confirmation prompt) skip the redundant second pass. Defaults
// to false so callers without prior validation (any future entry point) keep
// the defense-in-depth check.
Result flashFromSdPath(const char* sdPath, ProgressCb onProgress, void* ctx, bool alreadyValidated = false);

// Full-image integrity check that mirrors the bootloader's verification:
// header magic, segment table walk, XOR checksum, and SHA256 trailer (when
// hash_appended == 1). Run this before flashing a candidate firmware so a
// truncated/corrupted .bin never reaches otadata.
//
// `partitionSize` is the size of the destination OTA partition; pass 0 to
// skip the size-fits-partition check (e.g. when validating ahead of partition
// lookup). Streams the file in CHUNK-sized reads; the file is rewound on
// success so the caller can immediately reread it for flashing.
Result validateImageFile(const char* sdPath, size_t partitionSize);

// Classify a .bin before SD update: rejects merged USB full-flash images.
ImageKind classifyFirmwareFile(const char* sdPath);

// True when classifyFirmwareFile() reports APP_UPDATE.
bool isSdUpdateImage(const char* sdPath);

// classifyFirmwareFile + validateImageFile for SD-card update entry points.
Result validateSdUpdateImage(const char* sdPath, size_t partitionSize);

// Destination for SD/OTA writes: inactive OTA slot (dual-bank). Returns nullptr
// when only a single app partition is present — SD update cannot safely overwrite
// the running firmware in that layout.
const esp_partition_t* getUpdatePartition();

// True when app0 and app1 OTA slots are both present in the partition table.
bool hasDualOtaAppPartitions();

// Prepare hardware for raw flash writes (WiFi off, etc.). Call before flashFromSdPath.
void prepareForFlashWrite();

const char* resultName(Result r);

}  // namespace firmware_flash
