// Copyright (c) 2016 dacci.org

#include "ui/progress_dialog.h"

#include "app/scan_volume.h"
#include "app/volume_scanner.h"

ProgressDialog::ProgressDialog(VolumeScanner* scanner) : scanner_(scanner) {}

BOOL ProgressDialog::OnInitDialog(CWindow /*focus*/, LPARAM /*init_param*/) {
  CenterWindow();

  DoDataExchange(DDX_LOAD);

  HRESULT result = scanner_->Scan(m_hWnd);
  if (FAILED(result))
    EndDialog(IDABORT);

  return TRUE;
}

LRESULT ProgressDialog::OnScanProgress(UINT /*message*/, WPARAM wParam,
                                       LPARAM lParam) {
  switch (wParam) {
    case VolumeScanner::EnumBegin:
      message_ = L"enumerating . . .";
      break;

    case VolumeScanner::EnumEnd:
      message_ = L"enumerated";
      break;

    case VolumeScanner::SizeBegin:
      message_ = L"sizing . . .";
      break;

    case VolumeScanner::SizeEnd:
      message_ = L"sized";
      break;

    case VolumeScanner::ScanEnd:
      EndDialog(SUCCEEDED(lParam) ? IDOK : IDABORT);
      return 0;
  }

  DoDataExchange(DDX_LOAD);

  return 0;
}

void ProgressDialog::OnCancel(UINT /*notify_code*/, int /*id*/,
                              CWindow /*control*/) {
  scanner_->Cancel();
}
