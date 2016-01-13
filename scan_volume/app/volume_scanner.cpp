// Copyright (c) 2016 dacci.org

#include "app/volume_scanner.h"

#include <winioctl.h>

#include <array>
#include <list>
#include <map>
#include <vector>

namespace {

class FileId : public std::array<BYTE, 16> {
 public:
  FileId() {
    fill(static_cast<value_type>(-1));
  }

  explicit FileId(const FILE_ID_128& id) {
    operator=(id);
  }

  explicit FileId(const DWORDLONG& id) {
    operator=(id);
  }

  FileId& operator=(const FILE_ID_128& id) {
    memcpy(data(), id.Identifier, size());
    return *this;
  }

  FileId& operator=(const DWORDLONG& id) {
    fill(0);
    memcpy(data(), &id, sizeof(id));
    return *this;
  }

  bool operator==(const FileId& other) const {
    return memcmp(data(), other.data(), size()) == 0;
  }

  bool operator<(const FileId& other) const {
    auto a = rbegin(), b = other.rbegin();

    for (size_t i = 0; i < size(); ++i) {
      if (*a != *b)
        return *a < *b;

      ++a;
      ++b;
    }

    return false;
  }
};

bool GetFileSize(const std::wstring& path, LARGE_INTEGER* size) {
  bool succeeded = false;

  HANDLE handle = CreateFileW(
      path.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
      NULL);
  if (handle != INVALID_HANDLE_VALUE) {
    if (GetFileSizeEx(handle, size))
      succeeded = true;

    CloseHandle(handle);
    handle = INVALID_HANDLE_VALUE;
  }

  if (!succeeded) {
    WIN32_FIND_DATAW find_data;
    handle = FindFirstFileW(path.c_str(), &find_data);
    if (handle != INVALID_HANDLE_VALUE) {
      size->LowPart = find_data.nFileSizeLow;
      size->HighPart = find_data.nFileSizeHigh;
      succeeded = true;

      FindClose(handle);
      handle = INVALID_HANDLE_VALUE;
    }
  }

  return succeeded;
}

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

}  // namespace

struct VolumeScanner::Context {
  VolumeScanner* instance;
  HWND hWnd;
  std::vector<std::unique_ptr<FileEntry>> roots;
  std::list<FileEntry*> tree_path;
};

VolumeScanner::VolumeScanner() : cancel_(false), thread_(NULL) {
  InitializeSRWLock(&lock_);
  InitializeConditionVariable(&done_);
}

HRESULT VolumeScanner::Scan(HWND hWnd) {
  HRESULT result = E_FAIL;

  AcquireSRWLockExclusive(&lock_);

  if (thread_ == NULL) {
    auto context = std::make_unique<Context>();
    if (context != nullptr) {
      context->instance = this;
      context->hWnd = hWnd;

      cancel_ = false;

      thread_ = CreateThread(nullptr, 0, Run, context.get(), 0, nullptr);
      if (thread_ != NULL) {
        context.release();
        result = S_OK;
      } else {
        result = HRESULT_FROM_WIN32(GetLastError());
      }
    } else {
      result = E_OUTOFMEMORY;
    }
  }

  ReleaseSRWLockExclusive(&lock_);

  return result;
}

void VolumeScanner::Cancel() {
  AcquireSRWLockExclusive(&lock_);

  cancel_ = true;

  while (thread_ != NULL) {
    if (!SleepConditionVariableSRW(&done_, &lock_, INFINITE, 0))
      break;
  }

  ReleaseSRWLockExclusive(&lock_);
}

DWORD CALLBACK VolumeScanner::Run(void* param) {
  auto context = static_cast<Context*>(param);
  HRESULT result;

  PostMessage(context->hWnd, WM_USER, EnumBegin, 0);
  result = context->instance->Enumerate(context);
  PostMessage(context->hWnd, WM_USER, EnumEnd, result);

  if (SUCCEEDED(result)) {
    PostMessage(context->hWnd, WM_USER, SizeBegin, 0);
    result = context->instance->Size(context);
    PostMessage(context->hWnd, WM_USER, SizeEnd, result);
  }

  if (SUCCEEDED(result)) {
    AcquireSRWLockExclusive(&context->instance->lock_);
    context->instance->roots_.clear();
    context->instance->roots_ = std::move(context->roots);
    ReleaseSRWLockExclusive(&context->instance->lock_);
  }

  PostMessage(context->hWnd, WM_USER, ScanEnd, result);

  AcquireSRWLockExclusive(&context->instance->lock_);

  CloseHandle(context->instance->thread_);
  context->instance->thread_ = NULL;
  WakeAllConditionVariable(&context->instance->done_);

  ReleaseSRWLockExclusive(&context->instance->lock_);

  delete context;

  return 0;
}

HRESULT VolumeScanner::Enumerate(Context* context) {
  auto path = std::wstring(L"\\\\.\\").append(target_);
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (handle == INVALID_HANDLE_VALUE)
    return HRESULT_FROM_WIN32(GetLastError());

  std::map<FileId, FileEntry*> entries;

  // Step 1: Scan files.

  MFT_ENUM_DATA_V1 enum_query{0, 0, MAXLONGLONG, 2, 3};
  char buffer[kBufferSize];
  DWORD bytes = 0;
  HRESULT error = S_OK;

  for (;;) {
    AcquireSRWLockShared(&lock_);
    if (cancel_)
      error = E_ABORT;
    ReleaseSRWLockShared(&lock_);
    if (FAILED(error))
      break;

    BOOL succeeded = DeviceIoControl(handle, FSCTL_ENUM_USN_DATA, &enum_query,
                                     sizeof(enum_query), buffer, kBufferSize,
                                     &bytes, nullptr);
    if (!succeeded) {
      error = HRESULT_FROM_WIN32(GetLastError());
      break;
    }

    for (auto cursor = buffer + 8; bytes > 8 && !cancel_;) {
      AcquireSRWLockShared(&lock_);
      if (cancel_)
        error = E_ABORT;
      ReleaseSRWLockShared(&lock_);
      if (FAILED(error))
        break;

      auto record = reinterpret_cast<USN_RECORD_UNION*>(cursor);

      switch (record->Header.MajorVersion) {
        case 2:
          ProcessRecord(record->V2, &entries);
          break;

        case 3:
          ProcessRecord(record->V3, &entries);
          break;
      }

      cursor += record->Header.RecordLength;
      bytes -= record->Header.RecordLength;
    }

    enum_query.StartFileReferenceNumber = *reinterpret_cast<DWORDLONG*>(buffer);
  }

  CloseHandle(handle);
  handle = INVALID_HANDLE_VALUE;

  if (HRESULT_CODE(error) != ERROR_HANDLE_EOF) {
    for (auto& pair : entries) {
      for (auto& child : pair.second->children)
        child.release();

      delete pair.second;
    }

    return error;
  }

  if (entries.empty())
    return S_FALSE;

  // Step 2: Find root entry.

  for (auto& pair : entries) {
    if (pair.second->parent != nullptr)
      continue;

    std::unique_ptr<FileEntry> root(pair.second);
    root->attributes = FILE_ATTRIBUTE_DIRECTORY;
    root->name = target_.c_str();
    context->roots.push_back(std::move(root));
  }

  return S_OK;
}

HRESULT VolumeScanner::Size(Context* context) {
  void* old_value = nullptr;
  Wow64DisableWow64FsRedirection(&old_value);

  HRESULT result = S_OK;
  for (auto& root : context->roots) {
    result = Size(context, root.get());
    if (FAILED(result))
      break;
  }

  Wow64RevertWow64FsRedirection(old_value);

  return result;
}

HRESULT VolumeScanner::Size(Context* context, FileEntry* entry) {
  HRESULT result = S_OK;

  context->tree_path.push_back(entry);

  for (auto& child : entry->children) {
    AcquireSRWLockShared(&lock_);
    if (cancel_)
      result = E_ABORT;
    ReleaseSRWLockShared(&lock_);
    if (FAILED(result))
      break;

    if (child->attributes & FILE_ATTRIBUTE_DIRECTORY) {
      result = Size(context, child.get());
      if (FAILED(result))
        break;
    } else {
      child->size.QuadPart = -1;

      std::wstring path(L"\\\\?\\");
      for (auto& node : context->tree_path)
        path.append(node->name).push_back(L'\\');
      path.append(child->name);

      if (GetFileSize(path, &child->size)) {
        for (auto& node : context->tree_path)
          node->size.QuadPart += child->size.QuadPart;
      }
    }
  }

  context->tree_path.pop_back();

  return result;
}
