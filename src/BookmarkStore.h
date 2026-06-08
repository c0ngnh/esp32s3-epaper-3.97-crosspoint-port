#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ReaderBookmark {
  std::string bookPath;
  std::string bookTitle;
  int spineIndex = 0;
  int pageIndex = 0;
  int percent = 0;
  uint32_t createdMs = 0;
};

class BookmarkStore {
  static BookmarkStore instance;
  std::vector<ReaderBookmark> bookmarks_;

 public:
  static BookmarkStore& getInstance() { return instance; }
  static constexpr const char* FILE_PATH = "/.crosspoint/bookmarks.json";

  bool loadFromFile();
  bool saveToFile() const;

  const std::vector<ReaderBookmark>& getBookmarks() const { return bookmarks_; }
  bool addBookmark(const ReaderBookmark& entry);
  bool removeAt(size_t index);
  std::vector<ReaderBookmark> forBook(const std::string& path) const;
};

#define BOOKMARK_STORE BookmarkStore::getInstance()
