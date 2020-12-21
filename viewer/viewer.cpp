﻿// viewer.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "viewer.h"

// [http://web.archive.org/web/20100612190451/http://catch22.net/tuts/flicker <- http://web.archive.org/web/20100107165555/http://blogs.msdn.com/larryosterman/archive/2009/09/16/building-a-flicker-free-volume-control.aspx <- https://stackoverflow.com/questions/1842377/double-buffer-common-controls <- google:‘site:stackoverflow.com winapi tab switch flickering’]
class DoubleBufferedDC
{
    HDC target_dc, hdc;
    int width, height;
    HBITMAP hmembmp, holdbmp;

public:
    DoubleBufferedDC(HDC target_dc, int width, int height) : target_dc(target_dc), width(width), height(height)
    {
        hdc = CreateCompatibleDC(target_dc);
        hmembmp = CreateCompatibleBitmap(target_dc, width, height);
        holdbmp = SelectBitmap(hdc, hmembmp);
    }

    ~DoubleBufferedDC()
    {
        BitBlt(target_dc, 0, 0, width, height, hdc, 0, 0, SRCCOPY);
        SelectBitmap(hdc, holdbmp);
        DeleteBitmap(hmembmp);
        DeleteDC(hdc);
    }

    operator HDC() const {return hdc;}
};

class SelectPenAndBrush
{
    HDC hdc;
    HPEN pen, prev_pen;
    HBRUSH brush, prev_brush;

public:
    SelectPenAndBrush(HDC hdc, COLORREF pen_color, COLORREF brush_color, int pen_style = PS_SOLID, int pen_width = 1) : hdc(hdc)
    {
        pen = CreatePen(pen_style, pen_width, pen_color);
        prev_pen = SelectPen(hdc, pen);
        brush = CreateSolidBrush(brush_color);
        prev_brush = SelectBrush(hdc, brush);
    }

    ~SelectPenAndBrush()
    {
        SelectPen(hdc, prev_pen);
        DeletePen(pen);
        SelectBrush(hdc, prev_brush);
        DeleteBrush(brush);
    }
};

template <typename Ty> inline std::string separate_thousands(Ty value)
{
    static std::stringstream ss;
    static bool initialized = false;
    if (!initialized) {
        struct Dotted : std::numpunct<char> // [https://stackoverflow.com/a/31656618/2692494 <- google:‘c++ separate thousands’]
        {
            char do_thousands_sep()   const {return ' ';}  // separate with spaces
            std::string do_grouping() const {return "\3";} // groups of 3 digits
        };
        ss.imbue(std::locale(ss.getloc(), new Dotted));
        ss << std::fixed << std::setprecision(1); // [https://stackoverflow.com/a/14432416/2692494 <- google:‘c++ cout format float digits’]
        initialized = true;
    }
    ss.str(std::string()); // [https://stackoverflow.com/a/20792/2692494 <- google:‘stringstream clear’] (`clear()` just clears state of the stream)
    ss << value;
    return ss.str();
}

// [https://blog.softwareverify.com/how-to-make-your-mfc-or-non-mfc-program-support-high-dpi-monitors-the-easy-way/ <- https://www.codeproject.com/Messages/5452471/Re-create-a-dpi-aware-application.aspx <- google:‘codeproject dpiaware windows 7 site:www.codeproject.com’]
inline int mul_by_system_scaling_factor(int i)
{
    static int logpixelsx = 0;
    if (logpixelsx == 0) {
        HDC hdc = GetDC(NULL);
        logpixelsx = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(NULL, hdc);
    }
    return i * logpixelsx / 96;
}

const int FONT_HEIGHT = mul_by_system_scaling_factor(16);
const int LINE_HEIGHT = FONT_HEIGHT + 2;
const int ICON_SIZE   = mul_by_system_scaling_factor(16);
const int TREEVIEW_PADDING = mul_by_system_scaling_factor(5);
const int TREEVIEW_LEVEL_OFFSET = mul_by_system_scaling_factor(16);
const int LINE_PADDING_TOP = mul_by_system_scaling_factor(1);
const int LINE_PADDING_LEFT  = mul_by_system_scaling_factor(2);
const int LINE_PADDING_RIGHT = mul_by_system_scaling_factor(2);
const int SIZE_COLUMN_WIDTH = mul_by_system_scaling_factor(70);
HFONT font;

HWND main_wnd, treeview_wnd;

LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);

#define RECTARGS(rect) (rect).left, (rect).top, (rect).right-(rect).left, (rect).bottom-(rect).top

RECT calc_treeview_wnd_rect()
{
    RECT cr;
    GetClientRect(main_wnd, &cr);
    RECT r = {
        mul_by_system_scaling_factor(10),
        mul_by_system_scaling_factor(10),
        cr.right  - mul_by_system_scaling_factor(10),
        cr.bottom - mul_by_system_scaling_factor(10),
    };
    return r;
}

FILE *snapshot_file;

template <typename Ty> void rd(Ty &obj)
{
    fread(&obj, sizeof(Ty), 1, snapshot_file);
}
void rd(std::wstring &s)
{
    uint16_t sz;
    rd(sz);
    s.resize(sz);
    fread(&s[0], 2, s.size(), snapshot_file);
}

class DirEntry
{
public:
    DirEntry *parent = nullptr;
    std::map<std::wstring, DirEntry> subdirs;
    std::map<std::wstring, uint64_t> files;
    int64_t size = 0; // total size of files including subdirectories
    bool expanded = false;

    void read()
    {
        rd(size);

        uint16_t files_count;
        rd(files_count);
        while (files_count--) {
            std::wstring name;
            rd(name);
            uint64_t sz;
            rd(sz);
            files[name] = sz;
        }

        uint16_t dirs_count;
        rd(dirs_count);
        while (dirs_count--) {
            std::wstring name;
            rd(name);
            subdirs[name].read();
        }
    }
} root;

struct DirItem
{
    const std::wstring *name;
    DirEntry *d;
    int64_t size;
    int level;
};

void fill_dir_items(std::vector<DirItem> &all_items, DirEntry &d, int level)
{
    std::vector<DirItem> items;
    items.reserve(d.subdirs.size() + d.files.size());
    for (auto &&dir : d.subdirs) {
        DirItem di = {&dir.first, &dir.second, dir.second.size, level};
        items.push_back(di);
    }
    for (auto &&file : d.files) {
        DirItem di = {&file.first, nullptr, file.second, level};
        items.push_back(di);
    }

    std::sort(items.begin(), items.end(), [](const DirItem &a, const DirItem &b) {
        return a.size > b.size;
    });

    for (auto &&di : items) {
        all_items.push_back(di);
        if (di.d != nullptr && di.d->expanded)
            fill_dir_items(all_items, *di.d, level + 1);
    }
}

LRESULT CALLBACK treeview_wnd_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    static POINT pressed_cur_pos;
    static DirItem treeview_hover_dir_item = {0};

    switch (message)
    {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT r;
        GetClientRect(hwnd, &r);
        int width = r.right;
        int height = r.bottom;
        {
        DoubleBufferedDC hdc(BeginPaint(hwnd, &ps), r.right, r.bottom);

        {SelectPenAndBrush spb(hdc, RGB(223, 223, 223), RGB(255, 255, 255));
        Rectangle(hdc, 0, 0, r.right, r.bottom);}

        std::vector<DirItem> dir_items;
        fill_dir_items(dir_items, root, 0);

        int scrollpos = 0;

        // Update hover item
        static int treeview_hover_dir_item_index;
        RECT wnd_rect;
        GetWindowRect(treeview_wnd, &wnd_rect);
        if (/*!popup_menu_is_open*/true) {
            treeview_hover_dir_item.d = nullptr;
            int item_under_mouse;
            POINT cur_pos;
            GetCursorPos(&cur_pos);
            if (cur_pos.x >= wnd_rect.left
             && cur_pos.y >= wnd_rect.top
             && cur_pos.x < wnd_rect.right
             && cur_pos.y < wnd_rect.bottom
             && (GetAsyncKeyState(VK_LBUTTON) >= 0 || (LONGLONG&)cur_pos == (LONGLONG&)pressed_cur_pos)) { // do not show hover rect when left mouse button is pressed (during scrolling or pressing some button)
                item_under_mouse = (cur_pos.y - wnd_rect.top - TREEVIEW_PADDING + scrollpos) / LINE_HEIGHT;
                if (item_under_mouse < (int)dir_items.size()) {
                    treeview_hover_dir_item_index = item_under_mouse;
                    treeview_hover_dir_item = dir_items[item_under_mouse];
                }
            }
        }
        // Draw hover rect
        if (treeview_hover_dir_item.d != nullptr) {
            SelectPenAndBrush spb(hdc, RGB(112, 192, 231), RGB(229, 243, 251)); // colors are taken from Windows Explorer
            Rectangle(hdc, TREEVIEW_PADDING + treeview_hover_dir_item.level * TREEVIEW_LEVEL_OFFSET + ICON_SIZE - 1,
                           TREEVIEW_PADDING - scrollpos +  treeview_hover_dir_item_index    * LINE_HEIGHT, width - TREEVIEW_PADDING,
                           TREEVIEW_PADDING - scrollpos + (treeview_hover_dir_item_index+1) * LINE_HEIGHT);
        }

        // Draw tree view items
        SelectFont(hdc, font);
        SetBkMode(hdc, TRANSPARENT);

        RECT r;
        r.top = TREEVIEW_PADDING + LINE_PADDING_TOP - scrollpos;
        for (auto &&d : dir_items) {
            if (r.top >= height)
                break;

            r.bottom = r.top + FONT_HEIGHT;

            if (r.bottom >= TREEVIEW_PADDING) { // this check is not only for better performance, but is also to avoid artifacts at the top of tree view after scrollbar down button pressed
                r.right = width - TREEVIEW_PADDING - LINE_PADDING_RIGHT;
                r.left = r.right - SIZE_COLUMN_WIDTH;
                DrawTextA(hdc, separate_thousands(d.size / double(1024 * 1024)).c_str(), -1, &r, DT_RIGHT);

                r.right = r.left;
                r.left = TREEVIEW_PADDING + d.level * TREEVIEW_LEVEL_OFFSET;
//                 if (d.d != nullptr && !d.d->subdirs.empty())
//                     DrawIconEx(hdc, r.left, r.top, d.d->expanded ? icon_dir_exp : icon_dir_col, ICON_SIZE, ICON_SIZE, 0, NULL, DI_NORMAL);

                r.left += ICON_SIZE + LINE_PADDING_LEFT;
                DrawText(hdc, d.name->c_str(), -1, &r, DT_END_ELLIPSIS);
            }

            r.top += LINE_HEIGHT;
        }
        }
        EndPaint(hwnd, &ps);
        return 0; }

    case WM_MOUSEMOVE:
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    }

    return DefWindowProc(hwnd, message, wparam, lparam);
}

int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPTSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_VIEWER));
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = CreateSolidBrush(RGB(248, 248, 248)); // GetSysColorBrush(COLOR_3DFACE);
    wcex.lpszMenuName   = MAKEINTRESOURCE(IDC_VIEWER);
    wcex.lpszClassName  = L"occstracker window class";
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
    RegisterClassEx(&wcex);

    LOGFONT lf = {FONT_HEIGHT, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH|FF_SWISS, _T("")};
    font = CreateFontIndirect(&lf);

    // Perform application initialization:
    main_wnd = CreateWindow(wcex.lpszClassName, L"occstracker viewer", WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);
    if (!main_wnd) return FALSE;

    WNDCLASS wc = {0};
    wc.lpfnWndProc = treeview_wnd_proc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"occstracker tree view window class";
    if (!RegisterClass(&wc)) return FALSE;
    RECT treeview_wnd_rect = calc_treeview_wnd_rect();
    treeview_wnd = CreateWindowEx(0, wc.lpszClassName, L"", WS_CHILD|WS_VISIBLE, RECTARGS(treeview_wnd_rect), main_wnd, NULL, hInstance, 0);
    if (!treeview_wnd) return FALSE;

    ShowWindow(main_wnd, nCmdShow);

    _wfopen_s(&snapshot_file, __wargv[1], L"rb");
    root.read();
    fclose(snapshot_file);

    // Main message loop:
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int wmId, wmEvent;
//     PAINTSTRUCT ps;
//     HDC hdc;

    switch (message)
    {
    case WM_COMMAND:
        wmId    = LOWORD(wParam);
        wmEvent = HIWORD(wParam);
        // Parse the menu selections:
        switch (wmId)
        {
        case IDM_ABOUT:
            //DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
//     case WM_PAINT:
//         hdc = BeginPaint(hWnd, &ps);
//         // TODO: Add any drawing code here...
//         EndPaint(hWnd, &ps);
//         break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_SIZE:
        {
        RECT treeview_wnd_rect = calc_treeview_wnd_rect();
        MoveWindow(treeview_wnd, RECTARGS(treeview_wnd_rect), TRUE);
        InvalidateRect(treeview_wnd, NULL, TRUE);
        }
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
