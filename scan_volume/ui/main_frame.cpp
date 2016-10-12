// Copyright (c) 2016 dacci.org

#include "ui/main_frame.h"

#include <atlstr.h>

#include "ui/drive_dialog.h"

namespace {

struct ItemData {
  FileEntry* entry;
  bool opened;
};

int CALLBACK BySizeDescending(LPARAM left, LPARAM right, LPARAM /*param*/) {
  auto a = reinterpret_cast<ItemData*>(left);
  auto b = reinterpret_cast<ItemData*>(right);

  auto a_dir = a->entry->attributes & FILE_ATTRIBUTE_DIRECTORY;
  auto b_dir = b->entry->attributes & FILE_ATTRIBUTE_DIRECTORY;
  if (a_dir != b_dir)
    return b_dir - a_dir;

  if (a->entry->size.QuadPart < b->entry->size.QuadPart)
    return 1;

  if (a->entry->size.QuadPart > b->entry->size.QuadPart)
    return -1;

  return a->entry->name.compare(b->entry->name);
}

}  // namespace

MainFrame::MainFrame() {}

HTREEITEM MainFrame::InsertItem(HTREEITEM parent, FileEntry* entry) {
  TVINSERTSTRUCT insert{parent};
  auto& new_item = insert.item;
  new_item.mask =
      TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_CHILDREN | TVIF_PARAM;
  new_item.pszText = LPSTR_TEXTCALLBACK;
  new_item.iImage = I_IMAGECALLBACK;
  new_item.iSelectedImage = I_IMAGECALLBACK;
  new_item.cChildren = I_CHILDRENCALLBACK;
  new_item.lParam = reinterpret_cast<LPARAM>(new ItemData{entry, false});

  return tree_.InsertItem(&insert);
}

int MainFrame::OnCreate(CREATESTRUCT* /*create_struct*/) {
  if (!icons_.Create(16, 16, ILC_COLOR32, 2, 0))
    return -1;

  HMODULE shell32 = LoadLibraryEx(L"C:\\Windows\\System32\\imageres.dll", NULL,
                                  LOAD_LIBRARY_AS_IMAGE_RESOURCE);
  if (shell32 == NULL)
    return -1;

  HICON icon = NULL;
  HRESULT result;

  result = LoadIconMetric(shell32, MAKEINTRESOURCE(3), LIM_SMALL, &icon);
  if (FAILED(result))
    return -1;
  icons_.AddIcon(icon);

  result = LoadIconMetric(shell32, MAKEINTRESOURCE(2), LIM_SMALL, &icon);
  if (FAILED(result))
    return -1;
  icons_.AddIcon(icon);

  FreeLibrary(shell32);

  m_hWndClient = tree_.Create(
      m_hWnd, nullptr, nullptr,
      WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
          TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
          TVS_DISABLEDRAGDROP | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT,
      TVS_EX_DOUBLEBUFFER | TVS_EX_NOINDENTSTATE);
  if (!tree_.IsWindow())
    return -1;

  tree_.SetImageList(icons_);

  return 0;
}

LRESULT MainFrame::OnGetDispInfo(NMHDR* header) {
  auto disp_info = reinterpret_cast<NMTVDISPINFO*>(header);
  auto& item = disp_info->item;
  auto data = reinterpret_cast<ItemData*>(item.lParam);

  if (item.mask & LVIF_TEXT) {
    LONGLONG size = data->entry->size.QuadPart;
    if (size < 0) {
      wcscpy_s(item.pszText, item.cchTextMax, data->entry->name.c_str());
    } else {
      int index = -1;
      while (size > 1024) {
        size /= 1024;
        ++index;
      }

      swprintf_s(item.pszText, item.cchTextMax, L"%s (%lld ",
                 data->entry->name.c_str(), size);

      if (index >= 0) {
        auto prefix = L"Ki\0Mi\0Gi\0Ti\0Pi\0Ei\0Zi\0Yi\0" + index * 3;
        wcscat_s(item.pszText, item.cchTextMax, prefix);
      }

      wcscat_s(item.pszText, item.cchTextMax, L"B)");
    }
  }

  if (item.mask & TVIF_IMAGE)
    item.iImage = (data->entry->attributes & FILE_ATTRIBUTE_DIRECTORY) ? 0 : 1;

  if (item.mask & TVIF_SELECTEDIMAGE)
    item.iSelectedImage =
        (data->entry->attributes & FILE_ATTRIBUTE_DIRECTORY) ? 0 : 1;

  if (item.mask & TVIF_CHILDREN)
    item.cChildren = data->entry->children.empty() ? 0 : 1;

  item.mask |= TVIF_DI_SETITEM;

  return 0;
}

LRESULT MainFrame::OnItemExpanding(NMHDR* header) {
  auto tree_view = reinterpret_cast<NMTREEVIEW*>(header);
  auto& item = tree_view->itemNew;
  auto data = reinterpret_cast<ItemData*>(item.lParam);

  if (tree_view->action != TVE_EXPAND || data->opened)
    return 0;

  data->opened = true;

  for (auto& child : data->entry->children)
    InsertItem(tree_view->itemNew.hItem, child.get());

  TVSORTCB sort_cb{item.hItem, BySizeDescending};
  tree_.SortChildrenCB(&sort_cb, FALSE);

  return 0;
}

LRESULT MainFrame::OnDeleteItem(NMHDR* header) {
  auto tree_view = reinterpret_cast<NMTREEVIEW*>(header);

  delete reinterpret_cast<ItemData*>(tree_view->itemOld.lParam);

  return 0;
}

void MainFrame::OnFileOpen(UINT /*notify_code*/, int /*id*/,
                           CWindow /*control*/) {
  DriveDialog dialog;
  if (dialog.DoModal() != IDOK)
    return;

  HRESULT result = scanner_.Scan(dialog.selected_drive());
  if (FAILED(result)) {
    CString message;
    message.Format(L"Scan failed with 0x%08x", result);
    MessageBox(message, nullptr, MB_ICONERROR);
    return;
  }

  tree_.DeleteAllItems();
  InsertItem(TVI_ROOT, scanner_.GetRoot());
}

void MainFrame::OnAppExit(UINT /*notify_code*/, int /*id*/,
                          CWindow /*control*/) {
  PostMessage(WM_CLOSE);
}
