// Copyright (c) 2016 dacci.org

#include "app/volume_scanner.h"

#include <winioctl.h>

#include <algorithm>
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
  Context(VolumeScanner* instance, HWND hWnd) : instance(instance), hWnd(hWnd) {
    InitializeSRWLock(&queue_lock);
    InitializeConditionVariable(&queue_available);
  }

  VolumeScanner* const instance;
  const HWND hWnd;
  std::map<FileId, FileEntry*> entries;
  std::vector<std::unique_ptr<FileEntry>> roots;
  std::list<FileEntry*> queue;
  SRWLOCK queue_lock;
  CONDITION_VARIABLE queue_available;
};

VolumeScanner::VolumeScanner() : cancel_(false), thread_(NULL) {
  InitializeSRWLock(&lock_);
  InitializeConditionVariable(&done_);
}

HRESULT VolumeScanner::Scan(HWND hWnd) {
  HRESULT result = E_FAIL;

  AcquireSRWLockExclusive(&lock_);

  if (thread_ == NULL) {
    auto context = std::make_unique<Context>(this, hWnd);
    if (context != nullptr) {
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

  if (SUCCEEDED(result) && result != S_FALSE) {
    for (auto& pair : context->entries) {
      if (pair.second->parent != nullptr)
        continue;

      std::unique_ptr<FileEntry> root(pair.second);
      root->attributes = FILE_ATTRIBUTE_DIRECTORY;
      root->name = context->instance->target_.c_str();
      context->roots.push_back(std::move(root));
    }

    PostMessage(context->hWnd, WM_USER, SizeBegin, 0);

    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);

    DWORD concurrency =
        std::min<DWORD>(MAXIMUM_WAIT_OBJECTS, system_info.dwNumberOfProcessors);
    concurrency = concurrency * 3 / 2;

    std::vector<HANDLE> threads;
    for (DWORD i = 0; i < concurrency; ++i) {
      HANDLE thread = CreateThread(nullptr, 0, SizeThread, context, 0, nullptr);
      if (thread == NULL)
        break;

      threads.push_back(thread);
    }

    if (threads.empty()) {
      result = HRESULT_FROM_WIN32(GetLastError());
    } else {
      for (auto& pair : context->entries) {
        AcquireSRWLockShared(&context->instance->lock_);
        bool cancel = context->instance->cancel_;
        ReleaseSRWLockShared(&context->instance->lock_);
        if (cancel) {
          result = E_ABORT;
          break;
        }

        AcquireSRWLockExclusive(&context->queue_lock);

        if (!(pair.second->attributes & FILE_ATTRIBUTE_DIRECTORY)) {
          context->queue.push_front(pair.second);
          WakeAllConditionVariable(&context->queue_available);
        }

        ReleaseSRWLockExclusive(&context->queue_lock);
      }

      AcquireSRWLockExclusive(&context->queue_lock);
      context->queue.push_front(nullptr);
      WakeAllConditionVariable(&context->queue_available);
      ReleaseSRWLockExclusive(&context->queue_lock);

      WaitForMultipleObjects(static_cast<DWORD>(threads.size()), &threads[0],
                             TRUE, INFINITE);
      std::for_each(threads.begin(), threads.end(), CloseHandle);
      threads.clear();
    }

    PostMessage(context->hWnd, WM_USER, SizeEnd, result);
  }

  if (SUCCEEDED(result)) {
    AcquireSRWLockExclusive(&context->instance->lock_);
    context->instance->roots_.clear();
    context->instance->roots_ = std::move(context->roots);
    ReleaseSRWLockExclusive(&context->instance->lock_);
  } else {
    for (auto& pair : context->entries) {
      for (auto& child : pair.second->children)
        child.release();

      delete pair.second;
    }
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

  auto& entries = context->entries;

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

  if (HRESULT_CODE(error) != ERROR_HANDLE_EOF)
    return error;

  return entries.empty() ? S_FALSE : S_OK;
}

DWORD CALLBACK VolumeScanner::SizeThread(void* param) {
  auto context = static_cast<Context*>(param);

  BOOL wow64 = FALSE;
  void* redirection = nullptr;
  if (IsWow64Process(GetCurrentProcess(), &wow64) && wow64)
    Wow64DisableWow64FsRedirection(&redirection);

  for (bool cancel = false; !cancel;) {
    AcquireSRWLockShared(&context->instance->lock_);
    cancel = context->instance->cancel_;
    ReleaseSRWLockShared(&context->instance->lock_);
    if (cancel)
      break;

    AcquireSRWLockExclusive(&context->queue_lock);

    while (context->queue.empty())
      SleepConditionVariableSRW(&context->queue_available, &context->queue_lock,
                                INFINITE, 0);

    auto entry = context->queue.back();
    if (entry != nullptr)
      context->queue.pop_back();
    ReleaseSRWLockExclusive(&context->queue_lock);
    if (entry == nullptr)
      break;

    std::list<FileEntry*> tree_path;
    for (auto cursor = entry; cursor != nullptr; cursor = cursor->parent)
      tree_path.push_front(cursor);

    std::wstring path(L"\\\\?");
    path.reserve(MAX_PATH);
    for (auto cursor : tree_path) {
      path.push_back(L'\\');
      path.append(cursor->name);
    }

    if (GetFileSize(path, &entry->size)) {
      for (auto cursor : tree_path) {
        if (cursor != entry)
          InterlockedAdd64(&cursor->size.QuadPart, entry->size.QuadPart);
      }
    } else {
      entry->size.QuadPart = -1;
    }
  }

  if (wow64)
    Wow64RevertWow64FsRedirection(&redirection);

  return 0;
}
