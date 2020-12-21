// viewer.cpp : Defines the entry point for the application.
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

LRESULT CALLBACK treeview_wnd_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_PAINT:
        PAINTSTRUCT ps;
        RECT r;
        GetClientRect(hwnd, &r);
        {
        DoubleBufferedDC hdc(BeginPaint(hwnd, &ps), r.right, r.bottom);

        {SelectPenAndBrush spb(hdc, RGB(223, 223, 223), RGB(255, 255, 255));
        Rectangle(hdc, 0, 0, r.right, r.bottom);}

        }
        EndPaint(hwnd, &ps);
        return 0;
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
