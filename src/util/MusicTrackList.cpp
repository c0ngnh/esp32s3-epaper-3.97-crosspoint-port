#include "MusicTrackList.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <FsHelpers.h>
#include <HalStorage.h>
#include <WavUtil.h>

void listMusicTracks(std::vector<std::string>& out, const char* const musicDir) {
  out.clear();
  if (musicDir == nullptr || musicDir[0] == '\0') {
    return;
  }
  if (!Storage.exists(musicDir)) {
    Storage.mkdir(musicDir);
    return;
  }
  auto dir = Storage.open(musicDir);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return;
  }

  char name[256];
  while (true) {
    auto file = dir.openNextFile();
    if (!file) {
      break;
    }
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      std::string leaf(name);
      const auto slash = leaf.find_last_of('/');
      if (slash != std::string::npos) {
        leaf = leaf.substr(slash + 1);
      }
      if (isWavPath(leaf) || isMp3Path(leaf)) {
        out.push_back(leaf);
      }
    }
    file.close();
  }
  dir.close();
  FsHelpers::sortFileList(out);
}

#endif
