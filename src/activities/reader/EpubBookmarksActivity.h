#pragma once

#include <Epub.h>
#include <string>
#include <vector>

#include "BookmarkStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class EpubBookmarksActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::string bookPath;
  std::vector<ReaderBookmark> entries;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;

 public:
  EpubBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Epub> epub,
                      std::string bookPath);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
