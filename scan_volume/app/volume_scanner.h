// Copyright (c) 2016 dacci.org

#ifndef SCAN_VOLUME_APP_VOLUME_SCANNER_H_
#define SCAN_VOLUME_APP_VOLUME_SCANNER_H_

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include <pshpack8.h>  // NOLINT(build/include_order)

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

#include <poppack.h>  // NOLINT(build/include_order)

class VolumeScanner {
 public:
  enum Messages {
    EnumBegin,
    EnumEnd,
    SizeBegin,
    SizeEnd,
    ScanEnd,
  };

  VolumeScanner();

  HRESULT Scan(HWND hWnd);
  void Cancel();

  const std::wstring& GetTarget() const {
    return target_;
  }

  void SetTarget(const wchar_t* target) {
    target_ = target;
  }

  FileEntry* GetRoot() const {
    if (roots_.empty())
      return nullptr;
    else
      return roots_[0].get();
  }

 private:
  struct Context;

  static const size_t kBufferSize = 64 * 1024;

  static DWORD CALLBACK Run(void* param);
  HRESULT Enumerate(Context* context);
  static DWORD CALLBACK SizeThread(void* param);

  SRWLOCK lock_;
  CONDITION_VARIABLE done_;
  bool cancel_;
  HANDLE thread_;

  std::wstring target_;
  std::vector<std::unique_ptr<FileEntry>> roots_;

  VolumeScanner(const VolumeScanner&) = delete;
  VolumeScanner& operator=(const VolumeScanner&) = delete;
};

#endif  // SCAN_VOLUME_APP_VOLUME_SCANNER_H_
