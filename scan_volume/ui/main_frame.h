// Copyright (c) 2016 dacci.org

#ifndef SCAN_VOLUME_UI_MAIN_FRAME_H_
#define SCAN_VOLUME_UI_MAIN_FRAME_H_

#include <atlbase.h>

#include <atlapp.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <atlframe.h>

#include "app/volume_scanner.h"
#include "res/resource.h"

class MainFrame : public CFrameWindowImpl<MainFrame> {
 public:
  MainFrame();

  DECLARE_FRAME_WND_CLASS(nullptr, IDR_MAIN)

 private:
  struct ItemData;

  BEGIN_MSG_MAP(MainFrame)
    MSG_WM_CREATE(OnCreate)

    NOTIFY_HANDLER_EX(0, TVN_GETDISPINFO, OnGetDispInfo)
    NOTIFY_HANDLER_EX(0, TVN_ITEMEXPANDING, OnItemExpanding)
    NOTIFY_HANDLER_EX(0, TVN_DELETEITEM, OnDeleteItem)

    COMMAND_ID_HANDLER_EX(ID_FILE_OPEN, OnFileOpen)
    COMMAND_ID_HANDLER_EX(ID_APP_EXIT, OnAppExit)

    CHAIN_MSG_MAP(CFrameWindowImpl)
  END_MSG_MAP()

  HTREEITEM InsertItem(HTREEITEM parent, FileEntry* entry);
  static int CALLBACK SortChildren(LPARAM left, LPARAM right, LPARAM param);

  int OnCreate(CREATESTRUCT* create_struct);

  LRESULT OnGetDispInfo(NMHDR* header);
  LRESULT OnItemExpanding(NMHDR* header);
  LRESULT OnDeleteItem(NMHDR* header);

  void OnFileOpen(UINT notify_code, int id, CWindow control);
  void OnAppExit(UINT notify_code, int id, CWindow control);

  VolumeScanner scanner_;
  CImageList icons_;
  CTreeViewCtrl tree_;

  MainFrame(const MainFrame&) = delete;
  MainFrame& operator=(const MainFrame&) = delete;
};

#endif  // SCAN_VOLUME_UI_MAIN_FRAME_H_
