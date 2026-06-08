#include "VoiceRecorderActivity.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <AudioFilePlayer.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalAudio397.h>
#include <HalBoard397.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WavUtil.h>

#include <algorithm>
#include <ctime>

#include "CrossPointSettings.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/MediaTileDraw.h"
#include "util/UnifiedAppLayout.h"

namespace {

constexpr int VOLUME_STEP = 5;
constexpr int kTilePad = 16;
constexpr int kVolStripW = 10;
constexpr int kVolStripH = 80;
constexpr int kStatusRowH = 24;
}  // namespace

namespace {

std::string makeRecordingPath(const std::string& dir) {
  HalBoard397::DateTime dt{};
  if (!board397.readRtc(dt)) {
    dt.year = 2020;
    dt.month = 1;
    dt.day = 1;
    dt.hour = 0;
    dt.minute = 0;
    dt.second = 0;
  }
  char name[64];
  snprintf(name, sizeof(name), "rec_%04u%02u%02u_%02u%02u%02u.wav", dt.year, dt.month, dt.day, dt.hour, dt.minute,
           dt.second);
  std::string path = dir;
  if (path.back() != '/') path += "/";
  path += name;
  return path;
}

void warmPlaybackCodecTask(void*) {
  SETTINGS.applyOutputVolume();
  audio397.begin(HalAudio397::DEFAULT_SAMPLE_RATE);
  vTaskDelete(nullptr);
}

}  // namespace

VoiceRecorderActivity::VoiceRecorderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("VoiceRecorder", renderer, mappedInput) {}

void VoiceRecorderActivity::loadRecordings() {
  recordings.clear();
  if (!Storage.exists(recordDir.c_str())) {
    Storage.mkdir(recordDir.c_str());
  }

  auto dir = Storage.open(recordDir.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  char name[256];
  while (true) {
    auto file = dir.openNextFile();
    if (!file) break;
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      std::string leaf(name);
      const auto slash = leaf.find_last_of('/');
      if (slash != std::string::npos) {
        leaf = leaf.substr(slash + 1);
      }
      if (isWavPath(leaf)) {
        recordings.push_back(leaf);
      }
    }
    file.close();
  }
  dir.close();
  FsHelpers::sortFileList(recordings);
}

void VoiceRecorderActivity::recordTask(void* param) {
  auto* self = static_cast<VoiceRecorderActivity*>(param);
  const std::string path = makeRecordingPath(self->recordDir);

  audioFilePlayer.stopAndWait();
  if (!audio397.begin(HalAudio397::DEFAULT_SAMPLE_RATE)) {
    self->recording = false;
    self->recordTaskHandle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  FsFile file;
  if (!Storage.openFileForWrite("REC", path, file)) {
  audio397.shutdown();
  self->recording = false;
    self->recordTaskHandle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  writeWavHeader(file, HalAudio397::DEFAULT_SAMPLE_RATE, 1, 16, 0);

  auto buf = makeUniqueNoThrow<int16_t[]>(512);
  uint32_t totalSamples = 0;
  if (buf) {
    while (!self->stopRecord.load()) {
      const size_t n = audio397.recordPcm(buf.get(), 512);
      if (n == 0) {
        continue;
      }
      file.write(reinterpret_cast<const uint8_t*>(buf.get()), n * sizeof(int16_t));
      totalSamples += static_cast<uint32_t>(n);
    }
  }

  finalizeWavHeader(file, totalSamples * sizeof(int16_t));
  file.close();
  audio397.shutdown();
  SETTINGS.applyOutputVolume();
  audio397.begin(HalAudio397::DEFAULT_SAMPLE_RATE);

  self->recording = false;
  self->stopRecord.store(false);
  self->recordTaskHandle = nullptr;
  self->statusMessage = path;
  self->loadRecordings();
  self->requestUpdate();
  vTaskDelete(nullptr);
}

void VoiceRecorderActivity::stopPlayback() {
  audioFilePlayer.stopAndWait();
  nowPlaying.clear();
  playStartMs = 0;
  trackDurationSec = 0;
  totalPausedMs = 0;
  pauseStartMs = 0;
}

void VoiceRecorderActivity::startRecording() {
  if (recording) {
    return;
  }
  stopPlayback();
  stopRecord.store(false);
  recording = true;
  recordStartMs = millis();
  lastTileDisplaySec = UINT32_MAX;
  statusMessage = tr(STR_RECORDING);
  requestUpdate();

  if (xTaskCreate(recordTask, "audioRec", 8192, this, 5, &recordTaskHandle) != pdPASS) {
    recording = false;
    statusMessage = tr(STR_FAILED_LOWER);
    requestUpdate();
  }
}

void VoiceRecorderActivity::stopRecording() {
  if (!recording) {
    return;
  }
  stopRecord.store(true);
}

void VoiceRecorderActivity::startPlayback(const std::string& leaf) {
  if (recording || leaf.empty()) {
    return;
  }
  std::string full = recordDir;
  if (full.back() != '/') {
    full += "/";
  }
  full += leaf;
  nowPlaying = leaf;
  trackDurationSec = 0;
  (void)probeTrackDurationSec(full, trackDurationSec);
  playStartMs = millis();
  lastTileDisplaySec = UINT32_MAX;
  totalPausedMs = 0;
  pauseStartMs = 0;
  SETTINGS.applyOutputVolume();
  audioFilePlayer.playFile(full);
  statusMessage = leaf;
  requestUpdate();
}

void VoiceRecorderActivity::handleConfirm() {
  if (bigTileFocused) {
    if (recording) {
      stopRecording();
    } else {
      startRecording();
    }
    return;
  }
  if (selectorIndex >= recordings.size()) {
    return;
  }
  const std::string& leaf = recordings[selectorIndex];
  if (!nowPlaying.empty() && nowPlaying == leaf && audioFilePlayer.isActive()) {
    audioFilePlayer.togglePause();
    syncPauseClock();
    requestUpdate();
    return;
  }
  startPlayback(leaf);
}

void VoiceRecorderActivity::deleteSelected() {
  if (recordings.empty() || selectorIndex >= recordings.size()) {
    return;
  }
  const std::string& leaf = recordings[selectorIndex];
  std::string full = recordDir;
  if (full.back() != '/') full += "/";
  full += leaf;

  auto handler = [this, full, leaf](const ActivityResult& res) {
    if (!res.isCancelled && Storage.remove(full.c_str())) {
      if (nowPlaying == leaf) {
        stopPlayback();
      }
      loadRecordings();
      if (selectorIndex >= recordings.size() && !recordings.empty()) {
        selectorIndex = recordings.size() - 1;
      }
    }
    requestUpdate();
  };

  const std::string heading = std::string(tr(STR_DELETE)) + "? ";
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, leaf, true), handler);
}

bool VoiceRecorderActivity::preventAutoSleep() {
  return recording || (!nowPlaying.empty() && audioFilePlayer.isActive());
}

void VoiceRecorderActivity::onEnter() {
  Activity::onEnter();
  audioFilePlayer.setKeepCodecWarm(true);
  xTaskCreate(warmPlaybackCodecTask, "audioWarm", 4096, nullptr, 4, nullptr);
  loadRecordings();
  selectorIndex = 0;
  bigTileFocused = true;
  requestUpdate();
}

void VoiceRecorderActivity::onExit() {
  audioFilePlayer.setKeepCodecWarm(false);
  stopRecording();
  while (recording) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  stopPlayback();
  if (audio397.isActive()) {
    audio397.shutdown();
  } else {
    audio397.ensureAmplifierOff();
  }
  Activity::onExit();
}

void VoiceRecorderActivity::adjustVolume(const int delta) {
  const int next = static_cast<int>(SETTINGS.outputVolume) + delta;
  SETTINGS.setOutputVolume(static_cast<uint8_t>(std::clamp(next, 0, 100)));
  requestUpdate();
}

void VoiceRecorderActivity::syncPauseClock() {
  const unsigned long now = millis();
  if (audioFilePlayer.isPaused()) {
    if (pauseStartMs == 0) {
      pauseStartMs = now;
    }
  } else if (pauseStartMs != 0) {
    totalPausedMs += now - pauseStartMs;
    pauseStartMs = 0;
  }
}

uint32_t VoiceRecorderActivity::playbackElapsedSec() const {
  if (playStartMs == 0) {
    return 0;
  }
  unsigned long now = millis();
  unsigned long paused = totalPausedMs;
  if (audioFilePlayer.isPaused() && pauseStartMs != 0) {
    paused += now - pauseStartMs;
  }
  if (now < playStartMs + paused) {
    return 0;
  }
  return static_cast<uint32_t>((now - playStartMs - paused) / 1000);
}

uint32_t VoiceRecorderActivity::recordElapsedSec() const {
  if (!recording || recordStartMs == 0) {
    return 0;
  }
  return static_cast<uint32_t>((millis() - recordStartMs) / 1000);
}

void VoiceRecorderActivity::loop() {
  Activity::loop();

  const bool playbackActive = !recording && !nowPlaying.empty() && audioFilePlayer.isActive();

  if (recording) {
    const uint32_t sec = recordElapsedSec();
    if (sec != lastTileDisplaySec) {
      lastTileDisplaySec = sec;
      requestUpdate();
    }
  } else if (playbackActive && audioFilePlayer.isPlaying()) {
    const uint32_t sec = playbackElapsedSec();
    if (sec != lastTileDisplaySec) {
      lastTileDisplaySec = sec;
      requestUpdate();
    }
  }

  if (!recording && recordStartMs != 0) {
    recordStartMs = 0;
  }

  if (playbackActive && audioFilePlayer.isPlaying()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      adjustVolume(VOLUME_STEP);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      adjustVolume(-VOLUME_STEP);
      return;
    }
  }

  if (!nowPlaying.empty() && !audioFilePlayer.isActive()) {
    nowPlaying.clear();
    requestUpdate();
  }

  if (mappedInput.wasBackClicked()) {
    if (recording) {
      stopRecording();
      return;
    }
    stopPlayback();
    activityManager.goHome();
    return;
  }

  if (consumeShakeDeleteRequest() && !recordings.empty() && !recording && nowPlaying.empty()) {
    deleteSelected();
    return;
  }

  if (consumeConfirmClick()) {
    handleConfirm();
    return;
  }

  const int count = static_cast<int>(recordings.size());
  buttonNavigator.onNextRelease([this, count] {
    if (bigTileFocused) {
      if (count > 0) {
        bigTileFocused = false;
        selectorIndex = 0;
        requestUpdate();
      }
      return;
    }
    if (count > 0) {
      selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), count);
      requestUpdate();
    }
  });
  buttonNavigator.onPreviousRelease([this, count] {
    if (bigTileFocused) {
      return;
    }
    if (count > 0 && selectorIndex == 0) {
      bigTileFocused = true;
      requestUpdate();
      return;
    }
    if (count > 0) {
      selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), count);
      requestUpdate();
    }
  });
}

void VoiceRecorderActivity::drawRecorderTile(const Rect& tile) {
  UnifiedAppLayout::drawTileSurface(renderer, tile, bigTileFocused);

  const int volX = tile.x + tile.width - kTilePad - kVolStripW;
  const int volY = tile.y + 28;
  const int contentRight = volX - 10;
  const int innerX = tile.x + kTilePad;
  const int innerW = std::max(0, contentRight - innerX);
  const int lineH12 = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineH10 = renderer.getLineHeight(UI_10_FONT_ID);

  MediaTileDraw::drawVerticalVolume(renderer, volX, volY, kVolStripW, kVolStripH, SETTINGS.outputVolume);
  char volPct[8];
  snprintf(volPct, sizeof(volPct), "%u%%", static_cast<unsigned>(SETTINGS.outputVolume));
  const int volPctW = renderer.getTextWidth(UI_10_FONT_ID, volPct);
  renderer.drawText(UI_10_FONT_ID, volX + (kVolStripW - volPctW) / 2, volY + kVolStripH + 4, volPct, true);

  const int rowY = tile.y + 10;
  const bool playbackActive = !recording && !nowPlaying.empty() && audioFilePlayer.isActive();

  if (recording) {
    renderer.fillRect(innerX, rowY + 4, 8, 8, true);
    renderer.drawText(UI_12_FONT_ID, innerX + 14, rowY, tr(STR_RECORDING), true, EpdFontFamily::BOLD);

    const Rect clockRect{innerX, tile.y + kStatusRowH + 4, innerW, tile.height - kStatusRowH - 12};
    MediaTileDraw::drawHmsClock(renderer, clockRect, recordElapsedSec());
    return;
  }

  if (playbackActive) {
    const bool paused = audioFilePlayer.isPaused();
    const char* transport = paused ? "||" : ">";
    renderer.drawText(UI_12_FONT_ID, innerX, rowY, transport, true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, innerX + 22, rowY + 2, tr(STR_PLAY), true);

    const int titleTop = tile.y + kStatusRowH + 6;
    const int titleBottom = MediaTileDraw::playbackTitleBottom(tile.y, tile.height, 6);
    const int titleMaxH = std::max(lineH12, titleBottom - titleTop);
    auto titleLines = renderer.wrappedText(UI_12_FONT_ID, nowPlaying.c_str(), innerW,
                                           std::max(1, titleMaxH / lineH12), EpdFontFamily::BOLD);
    const int titleBlockH = lineH12 * static_cast<int>(titleLines.size());
    int ty = titleTop + (titleMaxH - titleBlockH) / 2;
    for (const auto& line : titleLines) {
      const int lineW = renderer.getTextWidth(UI_12_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, innerX + (innerW - lineW) / 2, ty, line.c_str(), true, EpdFontFamily::BOLD);
      ty += lineH12;
    }

    MediaTileDraw::drawPlaybackFooter(renderer, innerX, innerW, tile.y, tile.height, playbackElapsedSec(),
                                      trackDurationSec);
    return;
  }

  // Idle — ready to record (volume strip on the right)
  const int titleTop = tile.y + kStatusRowH + 8;
  const int titleBottom = tile.y + tile.height - MediaTileDraw::kPlaybackBottomPad;
  const int titleMaxH = std::max(lineH12, titleBottom - titleTop);
  const int recordW = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_RECORD), EpdFontFamily::BOLD);
  const int tx = innerX + (innerW - recordW) / 2;
  const int ty = titleTop + (titleMaxH - lineH12) / 2;
  renderer.drawText(UI_12_FONT_ID, tx, ty, tr(STR_RECORD), true, EpdFontFamily::BOLD);
}

void VoiceRecorderActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int headerBottom = metrics.topPadding + metrics.headerHeight;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_VOICE_RECORDER));

  const auto layout = UnifiedAppLayout::splitBelowHeader(renderer, headerBottom);
  drawRecorderTile(layout.bigTile);

  if (recordings.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, layout.menu.y + 8, tr(STR_NO_RECORDINGS), true);
  } else {
    const int menuSelector = bigTileFocused ? -1 : static_cast<int>(selectorIndex);
    GUI.drawButtonMenu(renderer, layout.menu, static_cast<int>(recordings.size()), menuSelector,
                       [this](int i) {
                         const std::string& leaf = recordings[static_cast<size_t>(i)];
                         if (leaf == nowPlaying && audioFilePlayer.isActive()) {
                           if (audioFilePlayer.isPaused()) {
                             return std::string("|| ") + leaf;
                           }
                           return std::string("> ") + leaf;
                         }
                         return leaf;
                       },
                       nullptr, UnifiedAppLayout::kMenuVisibleRows);
  }

  std::string labelsBtn2;
  if (bigTileFocused) {
    labelsBtn2 = recording ? tr(STR_STOP) : tr(STR_RECORD);
  } else if (selectorIndex < recordings.size()) {
    const std::string& leaf = recordings[selectorIndex];
    if (leaf == nowPlaying && audioFilePlayer.isActive()) {
      labelsBtn2 = audioFilePlayer.isPaused() ? tr(STR_PLAY) : "||";
    } else {
      labelsBtn2 = tr(STR_PLAY);
    }
  } else {
    labelsBtn2 = tr(STR_PLAY);
  }

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), labelsBtn2.c_str(), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

#endif
