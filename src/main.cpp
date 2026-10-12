// RE6 Save Editor - a small Win32 editor for Resident Evil 6 (PC) savedata.bin.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <cwchar>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "fields.h"
#include "save.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr wchar_t kAppName[] = L"RE6 Save Editor";
constexpr UINT WM_APP_COMMIT = WM_APP + 1;  // wParam: 1 = accept, 0 = cancel; lParam: edit generation

enum ControlId : int { IDC_OPEN = 100, IDC_SAVE, IDC_SAVEAS, IDC_UNLOCK, IDC_TREE, IDC_LIST, IDC_STATUS, IDC_EDITOR };

struct App {
    HWND wnd = nullptr;
    HWND tree = nullptr;
    HWND list = nullptr;
    HWND status = nullptr;
    HWND buttons[4] = {};
    HFONT font = nullptr;
    double scale = 1.0;

    HWND editor = nullptr;
    bool editorIsCombo = false;
    int editRow = -1;
    LPARAM editGeneration = 0;

    std::vector<Page> pages;
    int page = -1;
    SaveFile save;
} g;

int S(int px) { return static_cast<int>(px * g.scale + 0.5); }

std::wstring LookupName(const EnumTable& t, int64_t v) {
    for (const auto& e : t)
        if (e.value == v) return e.name;
    return L"Unknown (" + std::to_wstring(v) + L")";
}

std::wstring FormatValue(const Field& f, int64_t v) {
    switch (f.type) {
        case FieldType::Flag8:
        case FieldType::Bits8:
        case FieldType::Bits32: return v ? L"Yes" : L"No";
        case FieldType::Hex32: {
            wchar_t buf[16];
            swprintf_s(buf, L"0x%08llX", static_cast<unsigned long long>(v));
            return buf;
        }
        default: break;
    }
    if (f.names) return LookupName(*f.names, v);
    return std::to_wstring(v);
}

bool IsToggle(const Field& f) {
    return f.type == FieldType::Flag8 || f.type == FieldType::Bits8 || f.type == FieldType::Bits32;
}

void SetStatusMessage(const std::wstring& msg) {
    SendMessageW(g.status, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(msg.c_str()));
}

void UpdateTitleAndStatus() {
    std::wstring title = kAppName;
    std::wstring info = L"No file loaded. Use Open… or drag savedata.bin onto this window.";
    if (g.save.Loaded()) {
        title = std::filesystem::path(g.save.path).filename().wstring() + (g.save.dirty ? L" *" : L"") + L" - " + kAppName;
        info = g.save.path;
        info += g.save.StoredChecksum() == g.save.ComputeChecksum() || g.save.dirty
                    ? (g.save.dirty ? L"   |   Unsaved changes" : L"   |   Checksum OK")
                    : L"   |   Checksum mismatch (fixed on save)";
    }
    SetWindowTextW(g.wnd, title.c_str());
    SendMessageW(g.status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(info.c_str()));
    EnableWindow(g.buttons[1], g.save.Loaded());
    EnableWindow(g.buttons[2], g.save.Loaded());
    EnableWindow(g.buttons[3], g.save.Loaded());
}

void UpdateRow(int row) {
    if (g.page < 0 || !g.save.Loaded()) return;
    const Field& f = g.pages[g.page].fields[row];
    std::wstring v = FormatValue(f, g.save.Get(f));
    if (f.readOnly) v += L"  (read-only)";
    LVITEMW it{};
    it.iSubItem = 1;
    it.pszText = v.data();
    SendMessageW(g.list, LVM_SETITEMTEXTW, row, reinterpret_cast<LPARAM>(&it));
}

void ShowPage(int page) {
    g.page = page;
    SendMessageW(g.list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g.list);
    if (page >= 0) {
        const auto& fields = g.pages[page].fields;
        for (int i = 0; i < static_cast<int>(fields.size()); ++i) {
            LVITEMW it{};
            it.mask = LVIF_TEXT;
            it.iItem = i;
            it.pszText = const_cast<wchar_t*>(fields[i].name.c_str());
            SendMessageW(g.list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&it));
            wchar_t off[32];
            swprintf_s(off, L"0x%05X", fields[i].offset);
            it.iSubItem = 2;
            it.pszText = off;
            SendMessageW(g.list, LVM_SETITEMTEXTW, i, reinterpret_cast<LPARAM>(&it));
            UpdateRow(i);
        }
    }
    SendMessageW(g.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.list, nullptr, TRUE);
}

void RefreshAllRows() {
    if (g.page < 0) return;
    for (int i = 0; i < static_cast<int>(g.pages[g.page].fields.size()); ++i) UpdateRow(i);
}

// ---- in-place editing -------------------------------------------------------

LRESULT CALLBACK EditorSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
    if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        // For an open combo dropdown, let the combo handle the key itself.
        if (!(g.editorIsCombo && SendMessageW(h, CB_GETDROPPEDSTATE, 0, 0))) {
            PostMessageW(g.wnd, WM_APP_COMMIT, wp == VK_RETURN, g.editGeneration);
            return 0;
        }
    }
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;
    return DefSubclassProc(h, msg, wp, lp);
}

void EndEdit(bool accept) {
    if (!g.editor) return;
    HWND ed = g.editor;
    g.editor = nullptr;  // guard against re-entry from focus notifications during destroy
    const Field& f = g.pages[g.page].fields[g.editRow];

    if (accept) {
        bool ok = true;
        int64_t value = 0;
        if (g.editorIsCombo) {
            LRESULT sel = SendMessageW(ed, CB_GETCURSEL, 0, 0);
            ok = sel != CB_ERR;
            if (ok) value = static_cast<int32_t>(SendMessageW(ed, CB_GETITEMDATA, sel, 0));
        } else {
            wchar_t buf[64] = {};
            GetWindowTextW(ed, buf, 64);
            wchar_t* end = nullptr;
            value = std::wcstoll(buf, &end, 0);
            while (end && *end == L' ') ++end;
            ok = end != buf && end && *end == 0;
        }
        int64_t lo, hi;
        FieldRange(f, lo, hi);
        if (!ok || value < lo || value > hi) {
            MessageBeep(MB_ICONWARNING);
            SetStatusMessage(L"Invalid value for \"" + f.name + L"\" - must be a number from " + std::to_wstring(lo) +
                             L" to " + std::to_wstring(hi) + L".");
        } else if (value != g.save.Get(f)) {
            g.save.Set(f, value);
            UpdateRow(g.editRow);
            UpdateTitleAndStatus();
            SetStatusMessage(L"Changed \"" + f.name + L"\". Remember to save.");
        }
    }
    DestroyWindow(ed);
    SetFocus(g.list);
}

void BeginEdit(int row) {
    EndEdit(true);
    if (g.page < 0 || row < 0 || !g.save.Loaded()) return;
    const Field& f = g.pages[g.page].fields[row];
    if (f.readOnly) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    if (IsToggle(f)) {
        g.save.Set(f, !g.save.Get(f));
        UpdateRow(row);
        UpdateTitleAndStatus();
        return;
    }

    ListView_EnsureVisible(g.list, row, FALSE);
    RECT rc{};
    rc.top = 1;  // subitem index
    rc.left = LVIR_BOUNDS;
    SendMessageW(g.list, LVM_GETSUBITEMRECT, row, reinterpret_cast<LPARAM>(&rc));
    MapWindowPoints(g.list, g.wnd, reinterpret_cast<POINT*>(&rc), 2);

    g.editRow = row;
    g.editorIsCombo = f.names != nullptr;
    int64_t current = g.save.Get(f);
    if (g.editorIsCombo) {
        g.editor = CreateWindowExW(0, WC_COMBOBOXW, nullptr,
                                   WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST,
                                   rc.left, rc.top - S(2), rc.right - rc.left, S(320), g.wnd,
                                   reinterpret_cast<HMENU>(IDC_EDITOR), nullptr, nullptr);
        SendMessageW(g.editor, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), FALSE);
        bool found = false;
        for (const auto& e : *f.names) {
            if (e.value == current) found = true;
            std::wstring label = f.type == FieldType::Weapon || f.names->size() > 20
                                     ? std::to_wstring(e.value) + L" - " + e.name
                                     : std::wstring(e.name);
            LRESULT idx = SendMessageW(g.editor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            SendMessageW(g.editor, CB_SETITEMDATA, idx, static_cast<LPARAM>(e.value));
            if (e.value == current) SendMessageW(g.editor, CB_SETCURSEL, idx, 0);
        }
        if (!found) {
            std::wstring label = L"Unknown (" + std::to_wstring(current) + L")";
            SendMessageW(g.editor, CB_INSERTSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            SendMessageW(g.editor, CB_SETITEMDATA, 0, static_cast<LPARAM>(current));
            SendMessageW(g.editor, CB_SETCURSEL, 0, 0);
        }
    } else {
        std::wstring text = std::to_wstring(current);
        g.editor = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, text.c_str(),
                                   WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | ES_AUTOHSCROLL, rc.left, rc.top,
                                   rc.right - rc.left, rc.bottom - rc.top, g.wnd,
                                   reinterpret_cast<HMENU>(IDC_EDITOR), nullptr, nullptr);
        SendMessageW(g.editor, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), FALSE);
        SendMessageW(g.editor, EM_SETSEL, 0, -1);
    }
    ++g.editGeneration;
    SetWindowSubclass(g.editor, EditorSubclass, 1, 0);
    SetWindowPos(g.editor, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetFocus(g.editor);
    if (g.editorIsCombo) SendMessageW(g.editor, CB_SHOWDROPDOWN, TRUE, 0);
}

// ---- file handling ----------------------------------------------------------

std::wstring FindSteamSaveDir() {
    wchar_t steam[MAX_PATH];
    DWORD size = sizeof(steam);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, steam,
                     &size) != ERROR_SUCCESS)
        return {};
    std::error_code ec;
    for (const auto& user : std::filesystem::directory_iterator(std::filesystem::path(steam) / L"userdata", ec)) {
        auto dir = user.path() / L"221040" / L"remote";
        if (std::filesystem::is_directory(dir, ec)) return dir.make_preferred().wstring();
    }
    return {};
}

bool ConfirmDiscard() {
    if (!g.save.dirty) return true;
    int r = MessageBoxW(g.wnd, L"Save changes to the current file first?", kAppName, MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return false;
    if (r == IDYES) {
        std::wstring err;
        if (!g.save.Save(g.save.path, err)) {
            MessageBoxW(g.wnd, err.c_str(), kAppName, MB_ICONERROR);
            return false;
        }
    }
    return true;
}

void OpenPath(const std::wstring& path) {
    EndEdit(false);
    if (!ConfirmDiscard()) return;
    std::wstring err;
    if (!g.save.Load(path, err)) {
        MessageBoxW(g.wnd, (path + L"\n\n" + err).c_str(), kAppName, MB_ICONERROR);
        return;
    }
    RefreshAllRows();
    UpdateTitleAndStatus();
    if (g.save.StoredChecksum() != g.save.ComputeChecksum())
        SetStatusMessage(L"Loaded, but the stored checksum doesn't match. It will be corrected when you save.");
    else
        SetStatusMessage(L"Loaded.");
}

void OpenDialog() {
    wchar_t file[MAX_PATH] = L"savedata.bin";
    std::wstring dir = FindSteamSaveDir();
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = g.wnd;
    ofn.lpstrFilter = L"RE6 save (savedata.bin)\0savedata*.bin\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) OpenPath(file);
}

void SaveTo(std::wstring path) {
    EndEdit(true);
    if (!g.save.Loaded()) return;
    if (path.empty()) {
        wchar_t file[MAX_PATH];
        wcsncpy_s(file, g.save.path.c_str(), _TRUNCATE);
        OPENFILENAMEW ofn{sizeof(ofn)};
        ofn.hwndOwner = g.wnd;
        ofn.lpstrFilter = L"RE6 save (*.bin)\0*.bin\0All files\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"bin";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&ofn)) return;
        path = file;
    }
    std::wstring err;
    if (!g.save.Save(path, err)) {
        MessageBoxW(g.wnd, err.c_str(), kAppName, MB_ICONERROR);
        return;
    }
    UpdateTitleAndStatus();
    SetStatusMessage(L"Saved (checksum updated, backup written to " +
                     std::filesystem::path(path).filename().wstring() + L".bak).");
}

// ---- window -----------------------------------------------------------------

void BuildTree() {
    std::map<std::wstring, HTREEITEM> nodes;
    HTREEITEM first = nullptr;
    for (int i = 0; i < static_cast<int>(g.pages.size()); ++i) {
        const std::wstring& path = g.pages[i].path;
        HTREEITEM parent = TVI_ROOT;
        size_t start = 0;
        while (true) {
            size_t slash = path.find(L'/', start);
            bool leaf = slash == std::wstring::npos;
            std::wstring key = path.substr(0, leaf ? path.size() : slash);
            auto it = nodes.find(key);
            if (it == nodes.end()) {
                std::wstring label = path.substr(start, leaf ? std::wstring::npos : slash - start);
                TVINSERTSTRUCTW ins{};
                ins.hParent = parent;
                ins.hInsertAfter = TVI_LAST;
                ins.item.mask = TVIF_TEXT | TVIF_PARAM;
                ins.item.pszText = label.data();
                ins.item.lParam = leaf ? i : -1;
                HTREEITEM h = reinterpret_cast<HTREEITEM>(
                    SendMessageW(g.tree, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&ins)));
                it = nodes.emplace(key, h).first;
                if (leaf && !first) first = h;
            }
            parent = it->second;
            if (leaf) break;
            start = slash + 1;
        }
    }
    // Expand top-level groups so the main sections are visible at a glance.
    for (const auto& [key, h] : nodes)
        if (key.find(L'/') == std::wstring::npos && key != L"Mercenaries Records" && key != L"Inventory")
            TreeView_Expand(g.tree, h, TVE_EXPAND);
    TreeView_SelectItem(g.tree, first);
}

void Layout() {
    RECT rc;
    GetClientRect(g.wnd, &rc);
    SendMessageW(g.status, WM_SIZE, 0, 0);
    RECT sr;
    GetWindowRect(g.status, &sr);
    int statusH = sr.bottom - sr.top;
    int parts[2] = {std::max(S(200), static_cast<int>(rc.right * 0.6)), -1};
    SendMessageW(g.status, SB_SETPARTS, 2, reinterpret_cast<LPARAM>(parts));

    int m = S(8), bh = S(28), x = m;
    const int widths[4] = {S(90), S(90), S(90), S(150)};
    for (int i = 0; i < 4; ++i) {
        MoveWindow(g.buttons[i], x, m, widths[i], bh, TRUE);
        x += widths[i] + (i == 2 ? S(24) : S(6));
    }
    int top = m + bh + m, treeW = S(240);
    int h = std::max(0, static_cast<int>(rc.bottom) - statusH - top - m);
    MoveWindow(g.tree, m, top, treeW, h, TRUE);
    int listX = m + treeW + m;
    int listW = std::max(0, static_cast<int>(rc.right) - listX - m);
    MoveWindow(g.list, listX, top, listW, h, TRUE);
    int offW = S(80), nameW = std::min(S(260), listW / 2);
    ListView_SetColumnWidth(g.list, 0, nameW);
    ListView_SetColumnWidth(g.list, 1, std::max(S(120), listW - nameW - offW - GetSystemMetrics(SM_CXVSCROLL) - S(4)));
    ListView_SetColumnWidth(g.list, 2, offW);
}

void CreateControls() {
    NONCLIENTMETRICSW ncm{sizeof(ncm)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);

    const wchar_t* labels[4] = {L"Open…", L"Save", L"Save As…", L"Unlock Everything"};
    const int ids[4] = {IDC_OPEN, IDC_SAVE, IDC_SAVEAS, IDC_UNLOCK};
    for (int i = 0; i < 4; ++i)
        g.buttons[i] = CreateWindowExW(0, WC_BUTTONW, labels[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                       0, 0, 0, g.wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ids[i])), nullptr,
                                       nullptr);

    g.tree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, nullptr,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | TVS_HASBUTTONS | TVS_LINESATROOT |
                                 TVS_SHOWSELALWAYS | TVS_FULLROWSELECT | TVS_HASLINES,
                             0, 0, 0, 0, g.wnd, reinterpret_cast<HMENU>(IDC_TREE), nullptr, nullptr);

    g.list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SINGLESEL |
                                 LVS_SHOWSELALWAYS,
                             0, 0, 0, 0, g.wnd, reinterpret_cast<HMENU>(IDC_LIST), nullptr, nullptr);
    ListView_SetExtendedListViewStyle(g.list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    const wchar_t* cols[3] = {L"Field", L"Value  (double-click to edit)", L"Offset"};
    for (int i = 0; i < 3; ++i) {
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.cx = S(100);
        c.pszText = const_cast<wchar_t*>(cols[i]);
        SendMessageW(g.list, LVM_INSERTCOLUMNW, i, reinterpret_cast<LPARAM>(&c));
    }

    g.status = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, g.wnd,
                               reinterpret_cast<HMENU>(IDC_STATUS), nullptr, nullptr);

    for (HWND h : {g.tree, g.list, g.status, g.buttons[0], g.buttons[1], g.buttons[2], g.buttons[3]})
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), FALSE);

    g.pages = BuildPages();
    BuildTree();
    DragAcceptFiles(g.wnd, TRUE);
}

LRESULT OnNotify(NMHDR* nm) {
    if (nm->idFrom == IDC_TREE && nm->code == TVN_SELCHANGEDW) {
        EndEdit(true);
        auto* tv = reinterpret_cast<NMTREEVIEWW*>(nm);
        int page = static_cast<int>(tv->itemNew.lParam);
        if (page < 0) {
            // Group node: jump to its first page.
            HTREEITEM child = TreeView_GetChild(g.tree, tv->itemNew.hItem);
            if (child) {
                TreeView_Expand(g.tree, tv->itemNew.hItem, TVE_EXPAND);
                TreeView_SelectItem(g.tree, child);
                return 0;
            }
        }
        ShowPage(page);
    } else if (nm->idFrom == IDC_LIST) {
        switch (nm->code) {
            case NM_DBLCLK: BeginEdit(reinterpret_cast<NMITEMACTIVATE*>(nm)->iItem); break;
            case NM_RETURN: BeginEdit(ListView_GetNextItem(g.list, -1, LVNI_SELECTED)); break;
            case LVN_KEYDOWN: {
                WORD key = reinterpret_cast<NMLVKEYDOWN*>(nm)->wVKey;
                if (key == VK_SPACE || key == VK_F2) BeginEdit(ListView_GetNextItem(g.list, -1, LVNI_SELECTED));
                break;
            }
            case LVN_BEGINSCROLL: EndEdit(true); break;
        }
    }
    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g.wnd = hwnd;
            CreateControls();
            UpdateTitleAndStatus();
            return 0;
        case WM_SIZE:
            EndEdit(true);
            Layout();
            return 0;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {S(640), S(400)};
            return 0;
        case WM_NOTIFY: return OnNotify(reinterpret_cast<NMHDR*>(lp));
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_OPEN: OpenDialog(); break;
                case IDC_SAVE: SaveTo(g.save.path); break;
                case IDC_SAVEAS: SaveTo({}); break;
                case IDC_UNLOCK:
                    EndEdit(true);
                    if (g.save.Loaded()) {
                        UnlockEverything(g.save);
                        RefreshAllRows();
                        UpdateTitleAndStatus();
                        SetStatusMessage(L"Unlocked all skills, characters, costumes, Mercenaries stages and Serpent "
                                         L"Emblems. Remember to save.");
                    }
                    break;
                case IDOK:  // Enter, routed through IsDialogMessage
                    if (GetFocus() == g.list) BeginEdit(ListView_GetNextItem(g.list, -1, LVNI_SELECTED));
                    break;
                case IDC_EDITOR:
                    if (HIWORD(wp) == CBN_SELENDOK || HIWORD(wp) == CBN_KILLFOCUS || HIWORD(wp) == EN_KILLFOCUS)
                        PostMessageW(hwnd, WM_APP_COMMIT, 1, g.editGeneration);
                    break;
            }
            return 0;
        case WM_APP_COMMIT:
            if (lp == g.editGeneration) EndEdit(wp != 0);
            return 0;
        case WM_DROPFILES: {
            wchar_t file[MAX_PATH];
            if (DragQueryFileW(reinterpret_cast<HDROP>(wp), 0, file, MAX_PATH)) OpenPath(file);
            DragFinish(reinterpret_cast<HDROP>(wp));
            return 0;
        }
        case WM_CLOSE:
            EndEdit(true);
            if (ConfirmDiscard()) DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            DeleteObject(g.font);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    g.scale = GetDpiForSystem() / 96.0;

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES |
                                              ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"RE6SaveEditorWindow";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, kAppName, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                                CW_USEDEFAULT, S(980), S(640), nullptr, nullptr, inst, nullptr);
    ShowWindow(hwnd, show);

    int argc = 0;
    if (wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        if (argc > 1) OpenPath(argv[1]);
        LocalFree(argv);
    }

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(hwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
