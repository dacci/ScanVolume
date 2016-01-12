// Copyright (c) 2016 dacci.org

#ifndef SCAN_VOLUME_APP_VOLUME_SCANNER_H_
#define SCAN_VOLUME_APP_VOLUME_SCANNER_H_

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

struct FileEntry {
  FileEntry() : parent(nullptr), attributes(), size() {}

  FileEntry* parent;
  DWORD attributes;
  std::wstring name;
  LARGE_INTEGER size;
  std::vector<std::unique_ptr<FileEntry>> children;

  FileEntry(const FileEntry&) = delete;
  FileEntry& operator=(const FileEntry&) = delete;
};

class VolumeScanner {
 public:
  VolumeScanner();

  HRESULT Scan(const wchar_t* volume);

  FileEntry* GetRoot() const {
    if (roots_.empty())
      return nullptr;
    else
      return roots_[0].get();
  }

 private:
  static const size_t kBufferSize = 64 * 1024;

  std::vector<std::unique_ptr<FileEntry>> roots_;

  VolumeScanner(const VolumeScanner&) = delete;
  VolumeScanner& operator=(const VolumeScanner&) = delete;
};

#endif  // SCAN_VOLUME_APP_VOLUME_SCANNER_H_
