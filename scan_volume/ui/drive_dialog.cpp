// Copyright (c) 2016 dacci.org

#include "ui/drive_dialog.h"

DriveDialog::DriveDialog() {}

BOOL DriveDialog::OnInitDialog(CWindow /*focus*/, LPARAM /*init_param*/) {
  CenterWindow();

  drives_ = GetDlgItem(IDC_DRIVE_COMBO);

  CString text;
  wchar_t letter = L'A';
  for (auto bits = GetLogicalDrives(); bits; bits >>= 1, ++letter) {
    if ((bits & 1) == 0)
      continue;

    text.Format(L"%c:\\", letter);

    auto type = GetDriveType(text);
    if (type != DRIVE_FIXED)
      continue;

    CString format;
    BOOL succeeded = GetVolumeInformation(text, nullptr, 0, nullptr, nullptr,
                                          nullptr, format.GetBuffer(32), 32);
    format.ReleaseBuffer();
    if (!succeeded || format.CompareNoCase(L"NTFS") != 0)
      continue;

    text.Delete(2);
    drives_.AddString(text);
  }

  drives_.SetCurSel(0);

  return TRUE;
}

void DriveDialog::OnAnswer(UINT /*notify_code*/, int id, CWindow /*control*/) {
  if (id == IDOK)
    GetDlgItemText(IDC_DRIVE_COMBO, selected_drive_);

  EndDialog(id);
}
