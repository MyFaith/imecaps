#pragma comment(linker, "/subsystem:windows /entry:WinMainCRTStartup")

#include <windows.h>
#include <shellapi.h>
#include <winreg.h>

#define IDM_TOGGLE 1001
#define IDM_EXIT   1002
#define IDM_FULLSCREEN 1003
#define IDM_HIDE_ICON 1004
#define IDM_AUTOSTART 1005
#define WM_TRAYICON (WM_USER + 1)
#define WM_SHOW_TRAY (WM_USER + 2)
#define LONG_PRESS_TIME 5000 // 5秒

NOTIFYICONDATAW nid;
HMENU hPopupMenu = NULL;
HHOOK hKeyboardHook = NULL;
bool isEnabled = true;
bool isFullscreenDisable = true;
bool isTrayVisible = true;
bool isAutoStartEnabled = false;
const wchar_t* AUTO_START_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* APP_NAME = L"IMECaps";
const wchar_t CLASS_NAME[] = L"IMECapsClass";

// 修改全局变量
static DWORD capsLockDownTime = 0;
static bool capsLockHeld = false;
static bool altKeyPressed = false;
static bool ctrlKeyPressed = false;
static bool shiftKeyPressed = false;

// 修改后的全屏检测函数
bool IsFullscreenWindow() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || hwnd == GetShellWindow()) return false;

    // 方法1：检查窗口矩形
    RECT windowRect, monitorRect;
    GetWindowRect(hwnd, &windowRect);
    HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
    GetMonitorInfoW(hMonitor, &monitorInfo);
    monitorRect = monitorInfo.rcMonitor;

    // 允许2像素的误差范围
    const int tolerance = 2;
    bool isFullscreenBySize = 
        (abs(windowRect.left - monitorRect.left) <= tolerance &&
         abs(windowRect.top - monitorRect.top) <= tolerance &&
         abs(windowRect.right - monitorRect.right) <= tolerance &&
         abs(windowRect.bottom - monitorRect.bottom) <= tolerance);

    // 方法2：检查窗口样式
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    bool isFullscreenStyle = !(style & WS_CHILD) && 
                           ((style & WS_POPUP) || !(style & (WS_CAPTION | WS_THICKFRAME)));

    // 方法3：检查全屏独占模式（适用于游戏）
    DWORD flags;
    GetWindowDisplayAffinity(hwnd, &flags);
    bool isFullscreenAffinity = (flags == WDA_EXCLUDEFROMCAPTURE);

    return isFullscreenBySize && (isFullscreenStyle || isFullscreenAffinity);
}

// 修改键盘钩子处理函数
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        
        // 检测Ctrl键
        if (p->vkCode == VK_LCONTROL || p->vkCode == VK_RCONTROL) {
            ctrlKeyPressed = (wParam == WM_KEYDOWN);
            return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
        }

        // 检测Alt键
        if (p->vkCode == VK_LMENU || p->vkCode == VK_RMENU) {
            altKeyPressed = (wParam == WM_KEYDOWN);
            return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
        }

        // 检测Shift键
        if (p->vkCode == VK_LSHIFT || p->vkCode == VK_RSHIFT) {
            shiftKeyPressed = (wParam == WM_KEYDOWN);
            return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
        }

        // 检测C键
        if (p->vkCode == 'C' && wParam == WM_KEYDOWN) {
            if (ctrlKeyPressed && altKeyPressed && shiftKeyPressed && !isTrayVisible) {
                PostMessage(nid.hWnd, WM_SHOW_TRAY, 0, 0);
                return 1; // 阻止C键输入
            }
        }

        if (p->vkCode == VK_CAPITAL) {
            // 长按检测逻辑
            if (wParam == WM_KEYDOWN) {
                if (!capsLockHeld) {
                    capsLockDownTime = GetTickCount();
                    capsLockHeld = true;
                }
            } else if (wParam == WM_KEYUP) {
                DWORD holdTime = GetTickCount() - capsLockDownTime;
                if (holdTime >= LONG_PRESS_TIME && !isTrayVisible) {
                    PostMessage(nid.hWnd, WM_SHOW_TRAY, 0, 0);
                }
                capsLockHeld = false;
            }

            // 输入法切换逻辑
            if (isEnabled && wParam == WM_KEYDOWN) {
                if (isFullscreenDisable && IsFullscreenWindow()) {
                    return 1;
                }

                INPUT input[4] = {0};
                input[0].type = INPUT_KEYBOARD;
                input[0].ki.wVk = VK_LMENU;
                input[1].type = INPUT_KEYBOARD;
                input[1].ki.wVk = VK_LSHIFT;
                input[2].type = INPUT_KEYBOARD;
                input[2].ki.wVk = VK_LSHIFT;
                input[2].ki.dwFlags = KEYEVENTF_KEYUP;
                input[3].type = INPUT_KEYBOARD;
                input[3].ki.wVk = VK_LMENU;
                input[3].ki.dwFlags = KEYEVENTF_KEYUP;

                SendInput(4, input, sizeof(INPUT));
                return 1;
            }

            // 检测Alt+CapsLock组合键
            if (wParam == WM_KEYDOWN && altKeyPressed && !isTrayVisible) {
                PostMessage(nid.hWnd, WM_SHOW_TRAY, 0, 0);
                return 1; // 阻止CapsLock单独触发
            }
        }
    }
    return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
}

// 添加单实例检测
HANDLE hMutex = NULL;

// 添加注册表操作函数
bool SetAutoStart(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTO_START_KEY, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS)
        return false;

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    
    LONG result;
    if (enable) {
        result = RegSetValueExW(hKey, APP_NAME, 0, REG_SZ, 
            (const BYTE*)path, (wcslen(path)+1)*sizeof(wchar_t));
    } else {
        result = RegDeleteValueW(hKey, APP_NAME);
    }
    
    RegCloseKey(hKey);
    return (result == ERROR_SUCCESS);
}

bool CheckAutoStart() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTO_START_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    DWORD type = REG_NONE;
    LONG result = RegQueryValueExW(hKey, APP_NAME, NULL, &type, NULL, NULL);
    RegCloseKey(hKey);
    
    return (result == ERROR_SUCCESS && type == REG_SZ);
}

// 窗口过程函数
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            // 设置键盘钩子
            hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
            break;

        case WM_TRAYICON:
            if (lParam == WM_RBUTTONDOWN) {
                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hWnd);
                TrackPopupMenu(hPopupMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
            }
            break;

        case WM_SHOW_TRAY:
            if (!isTrayVisible) {
                Shell_NotifyIconW(NIM_ADD, &nid);
                isTrayVisible = true;
                // 更新注册表状态
                HKEY hKey;
                if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\IMECaps", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                    DWORD value = 1;
                    RegSetValueExW(hKey, L"TrayVisible", 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
                    RegCloseKey(hKey);
                }
            }
            break;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_AUTOSTART:
                    if (SetAutoStart(!isAutoStartEnabled)) {
                        isAutoStartEnabled = !isAutoStartEnabled;
                        CheckMenuItem(hPopupMenu, IDM_AUTOSTART, 
                            isAutoStartEnabled ? (MF_CHECKED | MF_BYCOMMAND) : (MF_UNCHECKED | MF_BYCOMMAND));
                    } else {
                        MessageBoxW(hWnd, L"修改启动项失败，请尝试以管理员身份运行", L"错误", MB_ICONERROR);
                    }
                    break;
                case IDM_TOGGLE:
                    isEnabled = !isEnabled;
                    CheckMenuItem(hPopupMenu, IDM_TOGGLE, isEnabled ? (MF_CHECKED | MF_BYCOMMAND) : (MF_UNCHECKED | MF_BYCOMMAND));
                    break;
                case IDM_FULLSCREEN:
                    isFullscreenDisable = !isFullscreenDisable;
                    CheckMenuItem(hPopupMenu, IDM_FULLSCREEN, 
                        isFullscreenDisable ? (MF_CHECKED | MF_BYCOMMAND) : (MF_UNCHECKED | MF_BYCOMMAND));
                    break;
                case IDM_HIDE_ICON:
                    isTrayVisible = !isTrayVisible;
                    Shell_NotifyIconW(isTrayVisible ? NIM_ADD : NIM_DELETE, &nid);
                    HKEY hKey;
                    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\IMECaps", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                        DWORD value = isTrayVisible ? 1 : 0;
                        RegSetValueExW(hKey, L"TrayVisible", 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
                        RegCloseKey(hKey);
                    }
                    break;
                case IDM_EXIT:
                    DestroyWindow(hWnd);
                    break;
            }
            break;

        case WM_DESTROY:
            UnhookWindowsHookEx(hKeyboardHook);
            Shell_NotifyIconW(NIM_DELETE, &nid);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 单实例检测
    hMutex = CreateMutexW(NULL, TRUE, CLASS_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hWnd = FindWindowW(CLASS_NAME, NULL);
        if (hWnd) {
            SendMessage(hWnd, WM_SHOW_TRAY, 0, 0);
        }
        return 0;
    }

    // 在单实例检测之后添加启动项检查
    isAutoStartEnabled = CheckAutoStart();

    // 在创建托盘图标前添加
    isTrayVisible = true; // 默认显示托盘图标

    // 注册窗口类
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, L"MAINICON");
    wc.hIconSm = LoadIconW(hInstance, L"MAINICON");
    wc.lpszClassName = L"IMECapsClass";
    RegisterClassExW(&wc);

    // 创建隐藏窗口
    HWND hWnd = CreateWindowExW(0, wc.lpszClassName, L"IMECaps", 0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    ShowWindow(hWnd, SW_HIDE);

    // 创建托盘图标
    nid = { sizeof(nid) };
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(GetModuleHandleW(NULL), L"MAINICON");
    wcscpy_s(nid.szTip, L"IMECaps - 输入法切换工具");
    Shell_NotifyIconW(NIM_ADD, &nid);

    // 创建弹出菜单
    hPopupMenu = CreatePopupMenu();
    AppendMenuW(hPopupMenu, MF_STRING | MF_CHECKED, IDM_TOGGLE, L"启用");
    AppendMenuW(hPopupMenu, MF_STRING | (isFullscreenDisable ? MF_CHECKED : 0), IDM_FULLSCREEN, L"全屏时禁用");
    AppendMenuW(hPopupMenu, MF_STRING | (isAutoStartEnabled ? MF_CHECKED : 0), IDM_AUTOSTART, L"开机自启");
    AppendMenuW(hPopupMenu, MF_STRING, IDM_HIDE_ICON, L"隐藏托盘图标");
    AppendMenuW(hPopupMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hPopupMenu, MF_STRING, IDM_EXIT, L"退出程序");

    // 添加启动时读取状态的代码
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\IMECaps", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD value = 1, size = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"TrayVisible", NULL, NULL, (LPBYTE)&value, &size) == ERROR_SUCCESS) {
            isTrayVisible = (value != 0);
        }
        RegCloseKey(hKey);
    }
    
    // 根据状态显示/隐藏图标
    if (isTrayVisible) {
        Shell_NotifyIconW(NIM_ADD, &nid);
    }

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
