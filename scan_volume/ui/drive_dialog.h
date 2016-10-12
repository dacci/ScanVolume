// Copyright (c) 2016 dacci.org

#ifndef SCAN_VOLUME_UI_DRIVE_DIALOG_H_
#define SCAN_VOLUME_UI_DRIVE_DIALOG_H_

#include <atlbase.h>
#include <atlstr.h>

#include <atlapp.h>
#include <atlcrack.h>
#include <atlctrls.h>

#include "res/resource.h"

class DriveDialog : public CDialogImpl<DriveDialog> {
 public:
  static const UINT IDD = IDD_DRIVE;

  DriveDialog();

  const CString& selected_drive() const {
    return selected_drive_;
  }

 private:
  BEGIN_MSG_MAP(DriveDialog)
    MSG_WM_INITDIALOG(OnInitDialog)

    COMMAND_ID_HANDLER_EX(IDOK, OnAnswer)
    COMMAND_ID_HANDLER_EX(IDCANCEL, OnAnswer)
  END_MSG_MAP()

  BOOL OnInitDialog(CWindow focus, LPARAM init_param);

  void OnAnswer(UINT notify_code, int id, CWindow control);

  CComboBox drives_;
  CString selected_drive_;

  DriveDialog(const DriveDialog&) = delete;
  DriveDialog& operator=(const DriveDialog&) = delete;
};

#endif  // SCAN_VOLUME_UI_DRIVE_DIALOG_H_
