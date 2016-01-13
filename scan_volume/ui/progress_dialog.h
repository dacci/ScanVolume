// Copyright (c) 2016 dacci.org

#ifndef SCAN_VOLUME_UI_PROGRESS_DIALOG_H_
#define SCAN_VOLUME_UI_PROGRESS_DIALOG_H_

#include <atlbase.h>
#include <atlstr.h>
#include <atlwin.h>

#include <atlapp.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <atlddx.h>

#include "res/resource.h"

class VolumeScanner;

class ProgressDialog : public CDialogImpl<ProgressDialog>,
                       public CWinDataExchange<ProgressDialog> {
 public:
  static const UINT IDD = IDD_PROGRESS;

  explicit ProgressDialog(VolumeScanner* scanner);

 private:
  BEGIN_MSG_MAP(ProgressDialog)
    MSG_WM_INITDIALOG(OnInitDialog)
    MESSAGE_HANDLER_EX(WM_USER, OnScanProgress)

    COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
  END_MSG_MAP()

  BEGIN_DDX_MAP(ProgressDialog)
    DDX_TEXT(IDC_MESSAGE, message_)
    DDX_CONTROL_HANDLE(IDC_PROGRESS, progress_)
  END_DDX_MAP()

  BOOL OnInitDialog(CWindow focus, LPARAM init_param);
  LRESULT OnScanProgress(UINT message, WPARAM wParam, LPARAM lParam);

  void OnCancel(UINT notify_code, int id, CWindow control);

  VolumeScanner* const scanner_;
  CString message_;
  CProgressBarCtrl progress_;

  ProgressDialog(const ProgressDialog&) = delete;
  ProgressDialog& operator=(const ProgressDialog&) = delete;
};

#endif  // SCAN_VOLUME_UI_PROGRESS_DIALOG_H_
