// Copyright (c) 2016 dacci.org

#include "app/volume_scanner.h"

#include <winioctl.h>

#include <list>
#include <map>

namespace {

template <typename Record>
void ProcessRecord(const Record& record,
                   std::map<FileId, FileEntry*>* entries) {
  FileId id(record.FileReferenceNumber);
  FileId parent_id(record.ParentFileReferenceNumber);

  auto pointer = reinterpret_cast<const wchar_t*>(
      reinterpret_cast<const char*>(&record) + record.FileNameOffset);
  auto length = record.FileNameLength / sizeof(wchar_t);

  auto& parent = (*entries)[parent_id];
  if (parent == nullptr)
    parent = new FileEntry();

  auto& entry = (*entries)[id];
  if (entry == nullptr)
    entry = new FileEntry();

  entry->parent = parent;
  entry->attributes = record.FileAttributes;
  entry->name.assign(pointer, length);

  parent->children.push_back(std::unique_ptr<FileEntry>(entry));
}

class SizeCalculator {
 public:
  SizeCalculator() {}

  void Calculate(FileEntry* root) {
    void* old_value = nullptr;
    Wow64DisableWow64FsRedirection(&old_value);

    CalculateImpl(root);

    Wow64RevertWow64FsRedirection(old_value);
  }

 private:
  void CalculateImpl(FileEntry* root) {
    tree_path_.push_back(root);

    for (auto& child : root->children) {
      if (child->attributes & FILE_ATTRIBUTE_DIRECTORY) {
        CalculateImpl(child.get());
      } else {
        child->size.QuadPart = -1;

        std::wstring path;
        for (auto& node : tree_path_)
          path.append(node->name).push_back(L'\\');
        path.append(child->name);

        HANDLE file = CreateFile(
            path.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (file == INVALID_HANDLE_VALUE)
          continue;

        if (GetFileSizeEx(file, &child->size)) {
          for (auto& node : tree_path_)
            node->size.QuadPart += child->size.QuadPart;
        }

        CloseHandle(file);
      }
    }

    tree_path_.pop_back();
  }

  std::list<FileEntry*> tree_path_;

  SizeCalculator(const SizeCalculator&) = delete;
  SizeCalculator& operator=(const SizeCalculator&) = delete;
};

}  // namespace

bool FileId::operator<(const FileId& other) const {
  auto a = rbegin(), b = other.rbegin();

  for (size_t i = 0; i < size(); ++i) {
    if (*a != *b)
      return *a < *b;

    ++a;
    ++b;
  }

  return false;
}

VolumeScanner::VolumeScanner() {}

HRESULT VolumeScanner::Scan(const wchar_t* volume) {
  auto path = std::wstring(L"\\\\.\\").append(volume);
  HANDLE handle =
      CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (handle == INVALID_HANDLE_VALUE)
    return HRESULT_FROM_WIN32(GetLastError());

  std::map<FileId, FileEntry*> entries_;

  // Step 1: Scan files.

  MFT_ENUM_DATA_V1 enum_query{0, 0, MAXLONGLONG, 2, 3};
  char buffer[kBufferSize];
  DWORD bytes = 0;
  DWORD error = ERROR_SUCCESS;

  for (;;) {
    BOOL succeeded = DeviceIoControl(handle, FSCTL_ENUM_USN_DATA, &enum_query,
                                     sizeof(enum_query), buffer, kBufferSize,
                                     &bytes, nullptr);
    if (!succeeded) {
      error = GetLastError();
      break;
    }

    for (auto cursor = buffer + 8; bytes > 8;) {
      auto record = reinterpret_cast<USN_RECORD_UNION*>(cursor);

      switch (record->Header.MajorVersion) {
        case 2:
          ProcessRecord(record->V2, &entries_);
          break;

        case 3:
          ProcessRecord(record->V3, &entries_);
          break;
      }

      cursor += record->Header.RecordLength;
      bytes -= record->Header.RecordLength;
    }

    enum_query.StartFileReferenceNumber = *reinterpret_cast<DWORDLONG*>(buffer);
  }

  CloseHandle(handle);
  handle = INVALID_HANDLE_VALUE;

  if (error != ERROR_HANDLE_EOF)
    return HRESULT_FROM_WIN32(error);

  roots_.clear();

  if (entries_.empty())
    return S_FALSE;

  // Step 2: Find root entry.

  for (auto& pair : entries_) {
    if (pair.second->parent == nullptr) {
      std::unique_ptr<FileEntry> root(pair.second);
      root->attributes = FILE_ATTRIBUTE_DIRECTORY;
      root->name = volume;
      roots_.push_back(std::move(root));
    }
  }

  // Step 3: Calcurate size.

  SizeCalculator calculator;
  for (auto& root : roots_)
    calculator.Calculate(root.get());

  return S_OK;
}
