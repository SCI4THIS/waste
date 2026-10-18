/* Target-only Win32 declarations: no Windows SDK, CRT or WASTE runtime. */
typedef void *HANDLE;
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned long long WPARAM;
typedef long long LPARAM;
typedef long long LRESULT;
typedef LRESULT (__stdcall *WNDPROC)(HANDLE, UINT, WPARAM, LPARAM);

typedef struct {
    UINT style;
    WNDPROC window_proc;
    int class_extra, window_extra;
    HANDLE instance, icon, cursor, background;
    const char *menu_name, *class_name;
} WNDCLASSA;

typedef struct {
    HANDLE window;
    UINT message;
    WPARAM wparam;
    LPARAM lparam;
    DWORD time;
    struct { long x, y; } point;
    DWORD private_data;
} MSG;

_Static_assert(sizeof(HANDLE) == 8, "Windows x64 HANDLE required");
_Static_assert(sizeof(DWORD) == 4 && sizeof(BOOL) == 4, "Windows LLP64 required");
_Static_assert(sizeof(WNDCLASSA) == 72, "Windows x64 WNDCLASSA layout required");
_Static_assert(__builtin_offsetof(WNDCLASSA, window_proc) == 8 &&
               __builtin_offsetof(WNDCLASSA, class_name) == 64, "WNDCLASSA offsets");
_Static_assert(sizeof(MSG) == 48 && __builtin_offsetof(MSG, wparam) == 16 &&
               __builtin_offsetof(MSG, point) == 36, "Windows x64 MSG layout required");

__declspec(dllimport) HANDLE __stdcall GetModuleHandleA(const char *name);
__declspec(dllimport) __declspec(noreturn) void __stdcall ExitProcess(UINT status);
__declspec(dllimport) unsigned short __stdcall RegisterClassA(const WNDCLASSA *wc);
__declspec(dllimport) HANDLE __stdcall LoadCursorA(HANDLE instance, const char *name);
__declspec(dllimport) HANDLE __stdcall CreateWindowExA(DWORD ex_style,
    const char *class_name, const char *title, DWORD style,
    int x, int y, int width, int height, HANDLE parent, HANDLE menu,
    HANDLE instance, void *parameter);
__declspec(dllimport) LRESULT __stdcall DefWindowProcA(HANDLE window,
    UINT message, WPARAM wparam, LPARAM lparam);
__declspec(dllimport) BOOL __stdcall DestroyWindow(HANDLE window);
__declspec(dllimport) void __stdcall PostQuitMessage(int status);
__declspec(dllimport) BOOL __stdcall GetMessageA(MSG *message,
    HANDLE window, UINT minimum, UINT maximum);
__declspec(dllimport) BOOL __stdcall TranslateMessage(const MSG *message);
__declspec(dllimport) LRESULT __stdcall DispatchMessageA(const MSG *message);

enum { WM_DESTROY = 2, WM_COMMAND = 0x111, HELLO_BUTTON = 100 };

static LRESULT __stdcall window_proc(HANDLE window, UINT message,
                                    WPARAM wparam, LPARAM lparam) {
    if (message == WM_COMMAND && (wparam & 0xffff) == HELLO_BUTTON &&
        ((wparam >> 16) & 0xffff) == 0 && lparam != 0) { /* BN_CLICKED */
        if (!DestroyWindow(window)) PostQuitMessage(1);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

__declspec(noreturn) void demo_start(void) {
    /* Static zero-initialization avoids a target memset/CRT dependency. */
    static WNDCLASSA wc;
    static const char class_name[] = "WasteDemoWindow";
    static const char message[] = "Hello from WASTE!";
    HANDLE instance = GetModuleHandleA((void *)0);
    if (!instance) ExitProcess(1);
    wc.window_proc = window_proc;
    wc.instance = instance;
    wc.cursor = LoadCursorA((void *)0, (const char *)32512); /* IDC_ARROW */
    wc.background = (HANDLE)6; /* COLOR_WINDOW + 1: system-owned white brush */
    wc.class_name = class_name;
    if (!wc.cursor || !RegisterClassA(&wc)) ExitProcess(1);

    /* WS_OVERLAPPEDWINDOW | WS_VISIBLE; normal close button uses DefWindowProc. */
    HANDLE window = CreateWindowExA(0, class_name, "WASTE Windows demo", 0x10cf0000,
        (-2147483647 - 1), (-2147483647 - 1), 320, 160,
        (void *)0, (void *)0, instance, (void *)0);
    if (!window) ExitProcess(1);
    /* Built-in controls, with WS_CHILD | WS_VISIBLE. */
    if (!CreateWindowExA(0, "STATIC", message, 0x50000000,
            20, 20, 260, 24, window, (void *)0, instance, (void *)0) ||
        !CreateWindowExA(0, "BUTTON", "Hello", 0x50010001,
            20, 58, 90, 30, window, (HANDLE)HELLO_BUTTON, instance, (void *)0))
        ExitProcess(1);

    MSG event;
    int result;
    while ((result = GetMessageA(&event, (void *)0, 0, 0)) > 0) {
        TranslateMessage(&event);
        DispatchMessageA(&event);
    }
    ExitProcess(result < 0 ? 1 : (UINT)event.wparam);
}
