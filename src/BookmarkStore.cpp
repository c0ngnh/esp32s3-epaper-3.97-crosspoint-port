#include "BookmarkStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

BookmarkStore BookmarkStore::instance;

bool BookmarkStore::loadFromFile() {
  bookmarks_.clear();
  if (!Storage.exists(FILE_PATH)) {
    return true;
  }
  const String json = Storage.readFile(FILE_PATH);
  if (json.isEmpty()) {
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    LOG_ERR("BMK", "Failed to parse bookmarks.json");
    return false;
  }
  for (JsonObject o : doc["bookmarks"].as<JsonArray>()) {
    ReaderBookmark b;
    b.bookPath = o["path"] | "";
    b.bookTitle = o["title"] | "";
    b.spineIndex = o["spine"] | 0;
    b.pageIndex = o["page"] | 0;
    b.percent = o["percent"] | 0;
    b.createdMs = o["ms"] | 0;
    if (!b.bookPath.empty()) {
      bookmarks_.push_back(std::move(b));
    }
  }
  return true;
}

bool BookmarkStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  JsonDocument doc;
  JsonArray arr = doc["bookmarks"].to<JsonArray>();
  for (const auto& b : bookmarks_) {
    JsonObject o = arr.add<JsonObject>();
    o["path"] = b.bookPath;
    o["title"] = b.bookTitle;
    o["spine"] = b.spineIndex;
    o["page"] = b.pageIndex;
    o["percent"] = b.percent;
    o["ms"] = b.createdMs;
  }
  String out;
  serializeJson(doc, out);
  return Storage.writeFile(FILE_PATH, out);
}

bool BookmarkStore::addBookmark(const ReaderBookmark& entry) {
  for (const auto& b : bookmarks_) {
    if (b.bookPath == entry.bookPath && b.spineIndex == entry.spineIndex && b.pageIndex == entry.pageIndex) {
      return saveToFile();
    }
  }
  bookmarks_.insert(bookmarks_.begin(), entry);
  while (bookmarks_.size() > 64) {
    bookmarks_.pop_back();
  }
  return saveToFile();
}

bool BookmarkStore::removeAt(const size_t index) {
  if (index >= bookmarks_.size()) {
    return false;
  }
  bookmarks_.erase(bookmarks_.begin() + static_cast<ptrdiff_t>(index));
  return saveToFile();
}

std::vector<ReaderBookmark> BookmarkStore::forBook(const std::string& path) const {
  std::vector<ReaderBookmark> out;
  for (const auto& b : bookmarks_) {
    if (b.bookPath == path) {
      out.push_back(b);
    }
  }
  return out;
}
