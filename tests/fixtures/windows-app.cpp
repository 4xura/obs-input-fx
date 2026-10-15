#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT area;
        GetClientRect(window, &area);
        auto brush = CreateSolidBrush(RGB(70, 78, 92));
        FillRect(dc, &area, brush);
        DeleteObject(brush);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(245, 245, 245));
        RECT title{30, 30, 600, 80};
        DrawTextW(dc, L"InputFX capture verification", -1, &title, DT_LEFT | DT_TOP);
        auto line = CreatePen(PS_SOLID, 1, RGB(100, 110, 125));
        auto old = SelectObject(dc, line);
        MoveToEx(dc, 320, 100, nullptr);
        LineTo(dc, 320, 300);
        MoveToEx(dc, 220, 200, nullptr);
        LineTo(dc, 420, 200);
        SelectObject(dc, old);
        DeleteObject(line);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW cls{};
    cls.lpfnWndProc = proc;
    cls.hInstance = instance;
    cls.lpszClassName = L"InputFXFixture";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&cls);
    RECT rect{0, 0, 640, 400};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window =
        CreateWindowW(cls.lpszClassName, L"InputFX test application", WS_OVERLAPPEDWINDOW, 100, 100,
                      rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
