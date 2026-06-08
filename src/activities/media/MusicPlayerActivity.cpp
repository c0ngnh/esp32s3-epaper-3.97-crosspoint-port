#include "MusicPlayerActivity.h"



#if defined(BOARD_ESP32_S3_EPAPER_397)



#include <esp_task_wdt.h>



#include <algorithm>
#include <cstdio>



#include <AudioFilePlayer.h>

#include <HalTiltSensor.h>

#include <WavUtil.h>

#include <HalAudio397.h>

#include <FsHelpers.h>

#include <GfxRenderer.h>

#include <HalStorage.h>

#include <I18n.h>

#include <Logging.h>



#include "CrossPointSettings.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/MediaTileDraw.h"
#include "util/UnifiedAppLayout.h"



namespace {

constexpr int VOLUME_STEP = 5;
constexpr int kTilePad = 16;
constexpr int kTransportRowH = 22;
constexpr int kVolBarW = 72;
void drawVolumeBar(const GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                   const uint8_t volumePct) {
  renderer.drawRect(x, y, w, h, true);
  const int fillW = std::max(0, (w - 4) * static_cast<int>(volumePct) / 100);
  if (fillW > 0) {
    renderer.fillRect(x + 2, y + 2, fillW, h - 4, true);
  }
}

}  // namespace



MusicPlayerActivity::MusicPlayerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)

    : Activity("MusicPlayer", renderer, mappedInput) {}



void MusicPlayerActivity::loadTracks() {

  tracks.clear();

  if (!Storage.exists(musicDir.c_str())) {

    Storage.mkdir(musicDir.c_str());

  }



  auto dir = Storage.open(musicDir.c_str());

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

      if (isWavPath(leaf) || isMp3Path(leaf)) {

        tracks.push_back(leaf);

      }

    }

    file.close();

  }

  dir.close();

  FsHelpers::sortFileList(tracks);

}



void MusicPlayerActivity::startPlayback(const std::string& path) {

  stopPlayback();

  nowPlaying = path;

  std::string full = musicDir;

  if (full.back() != '/') full += "/";

  full += path;

  SETTINGS.applyOutputVolume();
  trackDurationSec = 0;
  (void)probeTrackDurationSec(full, trackDurationSec);
  audioFilePlayer.playFile(full);
  playStartMs = millis();
  totalPausedMs = 0;
  pauseStartMs = 0;
  lastPlaybackDisplaySec = UINT32_MAX;
  markFullRenderNeeded();
  requestUpdate();

}



void MusicPlayerActivity::stopPlayback() {

  audioFilePlayer.stopAndWait();

  nowPlaying.clear();
  trackDurationSec = 0;
  totalPausedMs = 0;
  pauseStartMs = 0;

}



void MusicPlayerActivity::playRandomTrack() {

  if (tracks.empty()) {

    return;

  }

  if (tracks.size() == 1) {

    startPlayback(tracks[0]);

    selectorIndex = 0;

    return;

  }

  size_t idx = 0;

  for (int attempt = 0; attempt < 8; ++attempt) {

    idx = static_cast<size_t>(random(static_cast<long>(tracks.size())));

    if (tracks[idx] != nowPlaying) {

      break;

    }

  }

  selectorIndex = idx;

  startPlayback(tracks[idx]);

}



void MusicPlayerActivity::adjustVolume(const int delta) {
  const int next = static_cast<int>(SETTINGS.outputVolume) + delta;
  SETTINGS.setOutputVolume(static_cast<uint8_t>(std::clamp(next, 0, 100)));
  markFullRenderNeeded();
  requestUpdate();
}

void MusicPlayerActivity::markFullRenderNeeded() { fullRenderNeeded = true; }

void MusicPlayerActivity::syncPauseClock() {
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

uint32_t MusicPlayerActivity::playbackElapsedSec() const {
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

void MusicPlayerActivity::drawPlayerTile(const Rect& tile, const std::string& title,
                                         const bool playbackActive) const {
  UnifiedAppLayout::drawTileSurface(renderer, tile);
  const int innerX = tile.x + kTilePad;
  const int innerW = tile.width - kTilePad * 2;
  const int lineH12 = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineH10 = renderer.getLineHeight(UI_10_FONT_ID);

  const bool paused = playbackActive && audioFilePlayer.isPaused();
  const bool playing = playbackActive && audioFilePlayer.isPlaying();

  // Transport + volume row
  const int row1Y = tile.y + 10;
  if (playbackActive) {
    const char* transport = paused ? "||" : ">";
    renderer.drawText(UI_12_FONT_ID, innerX, row1Y, transport, true, EpdFontFamily::BOLD);
    if (playing) {
      renderer.drawText(UI_10_FONT_ID, innerX + 28, row1Y + 2, tr(STR_PLAYING), true);
    }
  }

  char volPct[8];
  snprintf(volPct, sizeof(volPct), "%u%%", static_cast<unsigned>(SETTINGS.outputVolume));
  const int volPctW = renderer.getTextWidth(UI_10_FONT_ID, volPct);
  const int volBarX = tile.x + tile.width - kTilePad - kVolBarW;
  const int volPctX = volBarX - 6 - volPctW;
  drawVolumeBar(renderer, volBarX, row1Y + 3, kVolBarW, 8, SETTINGS.outputVolume);
  renderer.drawText(UI_10_FONT_ID, volPctX, row1Y + 2, volPct, true);

  // Title (centered in middle band)
  const int titleTop = tile.y + kTransportRowH + 8;
  const int titleBottom = MediaTileDraw::playbackTitleBottom(tile.y, tile.height, 10);
  const int titleMaxH = std::max(lineH12, titleBottom - titleTop);
  const int maxTitleLines = std::max(1, titleMaxH / lineH12);
  auto titleLines =
      renderer.wrappedText(UI_12_FONT_ID, title.c_str(), innerW, maxTitleLines, EpdFontFamily::BOLD);
  const int titleBlockH = lineH12 * static_cast<int>(titleLines.size());
  int ty = titleTop + (titleMaxH - titleBlockH) / 2;
  for (const auto& line : titleLines) {
    const int lineW = renderer.getTextWidth(UI_12_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, innerX + (innerW - lineW) / 2, ty, line.c_str(), true, EpdFontFamily::BOLD);
    ty += lineH12;
  }

  if (!playbackActive) {
    return;
  }

  MediaTileDraw::drawPlaybackFooter(renderer, innerX, innerW, tile.y, tile.height, playbackElapsedSec(),
                                    trackDurationSec);
}

void MusicPlayerActivity::patchPlaybackFooter(const Rect& tile) const {
  const int innerX = tile.x + kTilePad;
  const int innerW = tile.width - kTilePad * 2;
  MediaTileDraw::erasePlaybackFooterBand(renderer, innerX, innerW, tile.y, tile.height);
  MediaTileDraw::drawPlaybackFooter(renderer, innerX, innerW, tile.y, tile.height, playbackElapsedSec(),
                                    trackDurationSec);
}



void MusicPlayerActivity::onEnter() {

  Activity::onEnter();

  halTiltSensor.clearPendingAppGestures();
  halTiltSensor.setShakeDeleteEnabled(false);
  halTiltSensor.setFlipRelaxed(true);

  audioFilePlayer.setKeepCodecWarm(true);
  loadTracks();

  selectorIndex = 0;
  playStartMs = 0;
  lastPlaybackDisplaySec = UINT32_MAX;
  fullRenderNeeded = true;

  SETTINGS.applyOutputVolume();
  requestUpdate();
}

void MusicPlayerActivity::onExit() {
  audioFilePlayer.setKeepCodecWarm(false);
  stopPlayback();
  halTiltSensor.setShakeDeleteEnabled(true);
  halTiltSensor.setFlipRelaxed(false);
  // playTask already calls audio397.shutdown(); avoid a second pop on exit.
  if (audio397.isActive()) {
    audio397.shutdown();
  }
  Activity::onExit();
}



void MusicPlayerActivity::loopPlaybackControls() {

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {

    adjustVolume(VOLUME_STEP);

    return;

  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {

    adjustVolume(-VOLUME_STEP);

    return;

  }

  if (consumeConfirmClick()) {

    audioFilePlayer.togglePause();
    syncPauseClock();
    markFullRenderNeeded();
    requestUpdate();

    return;

  }

  halTiltSensor.consumeShakeDelete();

  if (halTiltSensor.consumeFlip180Return()) {
    playRandomTrack();
  }

}



void MusicPlayerActivity::deleteSelected() {
  if (tracks.empty() || selectorIndex >= tracks.size()) {
    return;
  }
  const std::string leaf = tracks[selectorIndex];
  std::string full = musicDir;
  if (full.back() != '/') {
    full += "/";
  }
  full += leaf;

  auto handler = [this, full, leaf](const ActivityResult& res) {
    if (!res.isCancelled && Storage.remove(full.c_str())) {
      if (nowPlaying == leaf) {
        stopPlayback();
      }
      loadTracks();
      if (tracks.empty()) {
        selectorIndex = 0;
      } else if (selectorIndex >= tracks.size()) {
        selectorIndex = tracks.size() - 1;
      }
    }
    markFullRenderNeeded();
    requestUpdate();
  };

  const std::string heading = std::string(tr(STR_DELETE)) + "? ";
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, leaf, true), handler);
}

void MusicPlayerActivity::loopListNavigation() {
  halTiltSensor.consumeFlip180Return();

  if (!nowPlaying.empty()) {
    halTiltSensor.consumeShakeDelete();
  } else if (consumeShakeDeleteRequest() && !tracks.empty()) {
    deleteSelected();
    return;
  }

  if (consumeConfirmClick()) {
    if (!tracks.empty()) {
      if (audioFilePlayer.isPaused() && selectorIndex < tracks.size() &&
          tracks[selectorIndex] == nowPlaying) {
        audioFilePlayer.togglePause();
        syncPauseClock();
        markFullRenderNeeded();
        requestUpdate();
      } else {
        startPlayback(tracks[selectorIndex]);
      }
    }
    return;
  }



  const int count = static_cast<int>(tracks.size());

  buttonNavigator.onNextRelease([this, count] {

    if (count > 0) {

      selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), count);
      markFullRenderNeeded();
      requestUpdate();

    }

  });

  buttonNavigator.onPreviousRelease([this, count] {

    if (count > 0) {

      selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), count);
      markFullRenderNeeded();
      requestUpdate();

    }

  });

}



void MusicPlayerActivity::loop() {
  Activity::loop();

  const bool playbackActive = !nowPlaying.empty() && audioFilePlayer.isActive();
  if (playbackActive && audioFilePlayer.isPlaying()) {
    const uint32_t elapsed = playbackElapsedSec();
    if (elapsed != lastPlaybackDisplaySec) {
      lastPlaybackDisplaySec = elapsed;
      requestUpdate();
    }
  }

  if (!nowPlaying.empty() && !audioFilePlayer.isActive()) {
    const auto it = std::find(tracks.begin(), tracks.end(), nowPlaying);
    if (it != tracks.end() && tracks.size() > 1) {
      const size_t idx = static_cast<size_t>(it - tracks.begin());
      const size_t nextIdx = (idx + 1) % tracks.size();
      startPlayback(tracks[nextIdx]);
      return;
    }
    nowPlaying.clear();
    markFullRenderNeeded();
    requestUpdate();
  }



  if (mappedInput.wasBackClicked()) {

    stopPlayback();

    activityManager.goHome();

    return;

  }



  // Up/Down adjust volume only while audio is playing; paused/idle use list navigation.
  if (!nowPlaying.empty() && audioFilePlayer.isPlaying()) {
    loopPlaybackControls();
    return;
  }

  loopListNavigation();

}



bool MusicPlayerActivity::preventAutoSleep() { return true; }



void MusicPlayerActivity::render(RenderLock&&) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int headerBottom = metrics.topPadding + metrics.headerHeight;
  const auto layout = UnifiedAppLayout::splitBelowHeader(renderer, headerBottom);
  const bool playbackActive = !nowPlaying.empty() && audioFilePlayer.isActive();

  if (!fullRenderNeeded && playbackActive && audioFilePlayer.isPlaying()) {
    patchPlaybackFooter(layout.bigTile);
    esp_task_wdt_reset();
    renderer.displayBuffer();
    return;
  }

  fullRenderNeeded = false;
  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MUSIC_PLAYER));

  std::string tileTitle;
  if (playbackActive) {
    tileTitle = nowPlaying;
  } else if (!tracks.empty() && selectorIndex < tracks.size()) {
    tileTitle = tracks[selectorIndex];
  } else {
    tileTitle = tr(STR_NO_AUDIO_FILES);
  }

  drawPlayerTile(layout.bigTile, tileTitle, playbackActive);

  if (tracks.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, layout.menu.y + 8, tr(STR_NO_AUDIO_FILES), true);
  } else {
    GUI.drawButtonMenu(renderer, layout.menu, static_cast<int>(tracks.size()), static_cast<int>(selectorIndex),
                       [this, playbackActive](int i) {
                         if (tracks[static_cast<size_t>(i)] == nowPlaying && playbackActive) {
                           if (audioFilePlayer.isPaused()) {
                             return std::string("|| ") + tracks[static_cast<size_t>(i)];
                           }
                           return std::string("> ") + tracks[static_cast<size_t>(i)];
                         }
                         return tracks[static_cast<size_t>(i)];
                       },
                       nullptr, UnifiedAppLayout::kMenuVisibleRows);
  }

  std::string labelsBtn2;
  std::string labelsBtn3;
  std::string labelsBtn4;
  if (playbackActive) {
    labelsBtn2 = audioFilePlayer.isPaused() ? tr(STR_PLAY) : "||";
    if (audioFilePlayer.isPlaying()) {
      labelsBtn3 = "-";
      labelsBtn4 = "+";
    } else {
      labelsBtn3 = tr(STR_DIR_UP);
      labelsBtn4 = tr(STR_DIR_DOWN);
    }
  } else {
    labelsBtn2 = tr(STR_PLAY);
    labelsBtn3 = tr(STR_DIR_UP);
    labelsBtn4 = tr(STR_DIR_DOWN);
  }

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), labelsBtn2.c_str(), labelsBtn3.c_str(), labelsBtn4.c_str());
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  esp_task_wdt_reset();
  renderer.displayBuffer();
}



#endif

