// Copyright (c) 2016 dacci.org

#include "app/scan_volume.h"

#include <crtdbg.h>

#include "ui/main_frame.h"

CAppModule _Module;

int __stdcall wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                       wchar_t* /*command_line*/, int show_mode) {
  HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, nullptr, 0);

  SetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE |
                    BASE_SEARCH_PATH_PERMANENT);
  SetDllDirectory(L"");

  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

  HRESULT result;
  result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(result))
    return __LINE__;

  if (!AtlInitCommonControls(0xFFFF))
    return __LINE__;

  result = _Module.Init(nullptr, hInstance);
  if (FAILED(result))
    return __LINE__;

  {
    CMessageLoop message_loop;
    if (!_Module.AddMessageLoop(&message_loop))
      return __LINE__;

    MainFrame frame;
    if (frame.CreateEx()) {
      frame.ShowWindow(show_mode);
      frame.UpdateWindow();
    }

    message_loop.Run();

    _Module.RemoveMessageLoop();
  }

  _Module.Term();
  CoUninitialize();

  return 0;
}

#ifdef _CONSOLE
int wmain(int /*argc*/, wchar_t** /*argv*/) {
  auto command_line = GetCommandLineW();

  if (command_line[0] == L'"')
    command_line = wcschr(command_line + 1, L'"');

  while (*command_line != L' ' && *command_line != L'\0')
    ++command_line;

  while (*command_line == L' ')
    ++command_line;

  return wWinMain(GetModuleHandle(nullptr), NULL, command_line, SW_SHOWDEFAULT);
}
#endif
