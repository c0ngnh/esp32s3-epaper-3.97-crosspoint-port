#include "ReaderActivity.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <TxtLimits.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "Epub.h"
#include "EpubReaderActivity.h"
#include "SdCardFontSystem.h"
#include "Txt.h"
#include "TxtReaderActivity.h"
#include "Xtc.h"
#include "XtcReaderActivity.h"
#include "activities/util/BmpViewerActivity.h"
#include "activities/util/ConfirmationActivity.h"

bool ReaderActivity::isXtcFile(const std::string& path) { return FsHelpers::hasXtcExtension(path); }

bool ReaderActivity::isTxtFile(const std::string& path) {
  return FsHelpers::hasTxtExtension(path) ||
         FsHelpers::hasMarkdownExtension(path);  // Treat .md as txt files (until we have a markdown reader)
}

bool ReaderActivity::isBmpFile(const std::string& path) {
  return FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path) || FsHelpers::hasJpgExtension(path);
}

std::unique_ptr<Epub> ReaderActivity::loadEpub(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto epub = std::unique_ptr<Epub>(new Epub(path, "/.crosspoint"));
  if (epub->load(true, SETTINGS.embeddedStyle == 0)) {
    return epub;
  }

  LOG_ERR("READER", "Failed to load epub");
  return nullptr;
}

std::unique_ptr<Xtc> ReaderActivity::loadXtc(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto xtc = std::unique_ptr<Xtc>(new Xtc(path, "/.crosspoint"));
  if (xtc->load()) {
    return xtc;
  }

  LOG_ERR("READER", "Failed to load XTC");
  return nullptr;
}

std::unique_ptr<Txt> ReaderActivity::loadTxt(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto txt = std::unique_ptr<Txt>(new Txt(path, "/.crosspoint"));
  if (txt->load()) {
    return txt;
  }

  LOG_ERR("READER", "Failed to load TXT");
  return nullptr;
}

void ReaderActivity::goToLibrary(const std::string& fromBookPath) {
  // If coming from a book, start in that book's folder; otherwise start from root
  auto initialPath = fromBookPath.empty() ? "/" : FsHelpers::extractFolderPath(fromBookPath);
  activityManager.goToFileBrowser(std::move(initialPath));
}

void ReaderActivity::onGoToEpubReader(std::unique_ptr<Epub> epub) {
  const auto epubPath = epub->getPath();
  currentBookPath = epubPath;
  activityManager.replaceActivity(std::make_unique<EpubReaderActivity>(renderer, mappedInput, std::move(epub)));
}

void ReaderActivity::onGoToBmpViewer(const std::string& path) {
  activityManager.replaceActivity(std::make_unique<BmpViewerActivity>(renderer, mappedInput, path));
}

void ReaderActivity::onGoToXtcReader(std::unique_ptr<Xtc> xtc) {
  const auto xtcPath = xtc->getPath();
  currentBookPath = xtcPath;
  activityManager.replaceActivity(std::make_unique<XtcReaderActivity>(renderer, mappedInput, std::move(xtc)));
}

void ReaderActivity::onGoToTxtReader(std::unique_ptr<Txt> txt) {
  const auto txtPath = txt->getPath();
  currentBookPath = txtPath;
  activityManager.replaceActivity(std::make_unique<TxtReaderActivity>(renderer, mappedInput, std::move(txt)));
}

void ReaderActivity::tryOpenTxt(const std::string& path) {
  auto txt = loadTxt(path);
  if (!txt) {
    onGoBack();
    return;
  }

  const size_t fileSize = txt->getFileSize();
  const auto policy = TxtLimits::openPolicyForSize(fileSize);

  if (policy == TxtLimits::OpenPolicy::Allow) {
    onGoToTxtReader(std::move(txt));
    return;
  }

  txt.reset();

  char fileSizeStr[24];
  char limitStr[24];
  TxtLimits::formatSize(fileSizeStr, sizeof(fileSizeStr), fileSize);

  if (policy == TxtLimits::OpenPolicy::Refuse) {
    TxtLimits::formatSize(limitStr, sizeof(limitStr), TxtLimits::HARD_MAX_BYTES);
    char body[192];
    snprintf(body, sizeof(body), tr(STR_TXT_TOO_LARGE_BODY), fileSizeStr, limitStr);
    startActivityForResult(
        std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_TXT_TOO_LARGE_TITLE), body),
        [path](const ActivityResult& res) {
          if (!res.isCancelled) {
            activityManager.goToFileBrowser(path);
          }
        });
    return;
  }

  TxtLimits::formatSize(limitStr, sizeof(limitStr), TxtLimits::RECOMMENDED_MAX_BYTES);
  char body[192];
  snprintf(body, sizeof(body), tr(STR_TXT_LARGE_BODY), fileSizeStr, limitStr);
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_TXT_LARGE_TITLE), body),
                         [this, path](const ActivityResult& res) {
                           if (res.isCancelled) {
                             activityManager.goToFileBrowser(path);
                             return;
                           }
                           auto reopened = loadTxt(path);
                           if (!reopened) {
                             onGoBack();
                             return;
                           }
                           onGoToTxtReader(std::move(reopened));
                         });
}

void ReaderActivity::onEnter() {
  Activity::onEnter();

  if (initialBookPath.empty()) {
    goToLibrary();  // Start from root when entering via Browse
    return;
  }

  sdFontSystem.ensureLoaded(renderer);

  currentBookPath = initialBookPath;
  if (isBmpFile(initialBookPath)) {
    onGoToBmpViewer(initialBookPath);
  } else if (isXtcFile(initialBookPath)) {
    auto xtc = loadXtc(initialBookPath);
    if (!xtc) {
      onGoBack();
      return;
    }
    onGoToXtcReader(std::move(xtc));
  } else if (isTxtFile(initialBookPath)) {
    tryOpenTxt(initialBookPath);
  } else {
    auto epub = loadEpub(initialBookPath);
    if (!epub) {
      onGoBack();
      return;
    }
    onGoToEpubReader(std::move(epub));
  }
}

void ReaderActivity::onGoBack() {
  if (activityManager.hasStackedActivities()) {
    finish();
    return;
  }
  if (!currentBookPath.empty()) {
    activityManager.goToFileBrowser(currentBookPath);
    return;
  }
  finish();
}
