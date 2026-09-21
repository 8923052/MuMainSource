#pragma once
#include "support/CoreMath.h"

#include "app/Application.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_rect.h>

// Legacy platform constants used by the application compatibility shims.
#define DIRECTINPUT_VERSION 0x0500
#define IME_CONVERSIONMODE 1
#define IME_SENTENCEMODE 2
#define WM_GRAPHNOTIFY WM_USER + 13
#define WM_NPROTECT_EXIT_TWO (WM_USER + 10001)

using AppWindowHandle = void *;
using NativeWindowHandle = void *;
using NativeDeviceContextHandle = void *;

class AppWindowBackend
{
  public:
    virtual ~AppWindowBackend() = default;

    virtual bool InitializeVideo() noexcept = 0;
    virtual AppWindowHandle CreateAppWindow(const char *title, std::int32_t width,
                                            std::int32_t height, bool fullscreen) noexcept = 0;
    virtual void RaiseWindow(AppWindowHandle window) noexcept = 0;
    virtual void DestroyWindow(AppWindowHandle window) noexcept = 0;

    virtual NativeWindowHandle GetNativeWindow(AppWindowHandle window) noexcept = 0;
    virtual NativeDeviceContextHandle AcquireNativeDeviceContext(
        NativeWindowHandle window) noexcept = 0;
    virtual void ReleaseNativeDeviceContext(NativeWindowHandle window,
                                            NativeDeviceContextHandle context) noexcept = 0;
};

class AppPlatformState
{
  public:
    virtual ~AppPlatformState() = default;

    virtual AppWindowHandle Window() const noexcept = 0;
    virtual void SetWindow(AppWindowHandle window) noexcept = 0;
    virtual NativeWindowHandle NativeWindow() const noexcept = 0;
    virtual void SetNativeWindow(NativeWindowHandle window) noexcept = 0;
    virtual NativeDeviceContextHandle NativeDeviceContext() const noexcept = 0;
    virtual void SetNativeDeviceContext(NativeDeviceContextHandle context) noexcept = 0;
};

// Centers/raises the native Win32 MessageBox via a CBT hook. Windows-only and
// currently unused; portable code uses the SDL-backed MessageBox (WinUser.h).
#ifdef _WIN32

namespace leaf
{
class CCBTMessageBox
{
    HWND m_hParentWnd;
    HHOOK m_hCBT;
    bool m_bAlwaysOnTop;

  public:
    CCBTMessageBox();
    ~CCBTMessageBox();

    int OpenMessageBox(HWND hWnd, const wchar_t *lpText, const wchar_t *lpCaption, UINT uType,
                       bool bAlwaysOnTop);

    HWND GetParentWndHandle() const;
    bool IsAlwaysOnTop() const;

    bool HookCBT();
    void UnhookCBT();
    HHOOK GetHookHandle() const;

    static LRESULT CALLBACK CBTProc(INT nCode, WPARAM wParam, LPARAM lParam);
};
} // namespace leaf

#endif // _WIN32

class ApplicationKeeper;
class SdlGpuRenderBackend;

struct FontSizes
{
    int uiFontSize;
    int fixFontSize;
};
class ApplicationKeeperTestPeer;
class ApplicationFrameUnit;

struct AppWindowConfig final
{
    const char *title = nullptr;
    std::int32_t width = 0;
    std::int32_t height = 0;
    bool fullscreen = false;
};

enum class AppWindowCreateResult
{
    Success,
    VideoInitializationFailed,
    WindowCreationFailed,
};

class AppWindow final : protected ApplicationLegacyCalls
{
  public:
    AppWindow(ApplicationKeeper &keeper, AppWindowBackend &backend,
              AppPlatformState &state) noexcept;
    ~AppWindow();

    AppWindowCreateResult Initialize(AppWindowConfig config) noexcept;
    void Raise() noexcept;
    void Shutdown() noexcept;
    bool IsReady() const noexcept;
    BOOL &MinimizedEnabled() noexcept;
    bool &WindowActive() noexcept;
    BOOL &UseWindowMode() noexcept;
    BOOL &UseFullscreenMode() noexcept;
    HWND &NativeWindow() noexcept;
    HFONT &UiFont() noexcept;
    unsigned int &Width() noexcept;
    unsigned int &Height() noexcept;
    void RequestClose() noexcept;
    bool CloseRequested() const noexcept;
    bool SetTextInputActive(bool active) noexcept;

    // Recreates the window once from the startup-frozen configuration after a
    // loss destroyed it. Returns true when a window is ready afterwards.
    bool EnsureCreated() noexcept;
    bool UpdateTextInputArea(SDL_Rect area) noexcept;
    void ReinitializeFonts();
    void HandleWindowResize(int width, int height);
    void HandleFocusChange(bool active);
    void MuApplyWindowResolution(unsigned int width, unsigned int height, bool windowed);
    void UpdateCursorClip() noexcept;
    void RegisterBundledFonts();
    void UnregisterBundledFonts();
    void ShutdownLegacyFonts() noexcept;

  private:
    friend class ApplicationLegacyCalls;
    friend class ApplicationKeeperTestPeer;
    friend class SdlGpuRenderBackend;
    friend class ApplicationFrameUnit;
    AppWindowHandle Handle() const noexcept;
    FontSizes CalculateFontSizes() noexcept;
    HFONT CreateUIFont(int size, int weight, const std::wstring &selectedFont) noexcept;
    HFONT CreateUIFont(int size, int weight) noexcept;
    void CreateNewFonts(FontSizes sizes);
    void ReinitializeTextRenderer();
    void UpdateResolutionDependentSystems();
    void SaveIMEStatus();                // OMF-01869
    void RestoreIMEStatus();             // OMF-01870
    void CheckTextInputBoxIME(int mode); // OMF-01871
    int CBTMessageBox(HWND window, const std::wstring &text, const std::wstring &caption, UINT type,
                      bool alwaysOnTop); // OMF-01992

    AppWindowBackend &backend_;
    AppPlatformState &state_;
    BOOL &g_bMinimizedEnabled;
    SDL_Window *&g_sdlWindow;
    HWND &g_hWnd;
    HINSTANCE &g_hInst;
    HDC &g_hDC;
    HFONT &g_hFont;
    HFONT &g_hFontBold;
    HFONT &g_hFontBig;
    HFONT &g_hFixFont;
    bool &Destroy;
    int &g_iScreenSaverOldValue;
    BOOL &g_bUseWindowMode;
    BOOL &g_bUseFullscreenMode;
    int &g_iNoMouseTime;
    bool &g_bWndActive;
    double &g_TargetFpsBeforeInactive;
    bool &g_HasInactiveFpsOverride;
    int &OpenglWindowX;
    int &OpenglWindowY;
    int &OpenglWindowWidth;
    int &OpenglWindowHeight;
    unsigned int &WindowWidth;
    unsigned int &WindowHeight;
    struct ApplicationPlatformStorage &storage_;
    AppWindowConfig frozenConfig_{};
#ifdef _WIN32
    leaf::CCBTMessageBox cbtMessageBox_;
#endif
};

class LegacyPlatformBridge final : public AppPlatformState
{
  public:
    explicit LegacyPlatformBridge(class ApplicationKeeper &keeper) noexcept;
    AppWindowHandle Window() const noexcept override;
    void SetWindow(AppWindowHandle window) noexcept override;
    NativeWindowHandle NativeWindow() const noexcept override;
    void SetNativeWindow(NativeWindowHandle window) noexcept override;
    NativeDeviceContextHandle NativeDeviceContext() const noexcept override;
    void SetNativeDeviceContext(NativeDeviceContextHandle context) noexcept override;

  private:
    struct ApplicationPlatformStorage &storage_;
};

class SdlAppWindowBackend final : public AppWindowBackend
{
  public:
    bool InitializeVideo() noexcept override;
    AppWindowHandle CreateAppWindow(const char *title, std::int32_t width, std::int32_t height,
                                    bool fullscreen) noexcept override;
    void RaiseWindow(AppWindowHandle window) noexcept override;
    void DestroyWindow(AppWindowHandle window) noexcept override;
    NativeWindowHandle GetNativeWindow(AppWindowHandle window) noexcept override;
    NativeDeviceContextHandle AcquireNativeDeviceContext(
        NativeWindowHandle window) noexcept override;
    void ReleaseNativeDeviceContext(NativeWindowHandle window,
                                    NativeDeviceContextHandle context) noexcept override;
};

extern bool CheckSpecialText(wchar_t *Text);

#ifndef _IEXPLORER_H_
#define _IEXPLORER_H_

namespace leaf
{
// Open a URL in the default browser.
inline bool OpenExplorer(const std::wstring &url)
{
    // SW_SHOW
    INT_PTR result = (INT_PTR)ShellExecute(NULL, L"open", url.c_str(), NULL, NULL, SW_NORMAL);
    if (result > 32)
        return true;
    return false;
}
} // namespace leaf

#endif // _IEXPLORER_H_

// IME placement now belongs to AppWindow and is driven from the focused
// session's routed text-input rectangle in Application::RunLoop.

class ApplicationKeeper;

class CInput final : protected ApplicationLegacyCalls
{
  protected:
    struct PendingButtonInput
    {
        bool pressed = false;
        bool released = false;
        bool doubleClicked = false;
        bool held = false;
        bool changed = false;
    };

    HWND m_hWnd;

    POINT m_ptCursor;
    long m_lDX;
    long m_lDY;
    //    long    m_lDZ;
    bool m_bLBtnDn;
    bool m_bLBtnHeldDn;
    bool m_bLBtnUp;
    bool m_bLBtnDbl;
    bool m_bRBtnDn;
    bool m_bRBtnHeldDn;
    bool m_bRBtnUp;
    bool m_bRBtnDbl;
    bool m_bMBtnDn;
    bool m_bMBtnHeldDn;
    bool m_bMBtnUp;
    bool m_bMBtnDbl;

    long m_lScreenWidth;
    long m_lScreenHeight;
    bool m_bLeftHand;
    bool m_bTextEditMode;

    POINT m_ptFormerCursor;
    double m_dDoubleClickTime;
    double m_dBtn0LastClickTime;
    double m_dBtn1LastClickTime;
    double m_dBtn2LastClickTime;
    bool m_bFormerBtn0Dn;
    bool m_bFormerBtn1Dn;
    bool m_bFormerBtn2Dn;
    PendingButtonInput m_pendingButtons[3]{};

  public:
    explicit CInput(ApplicationKeeper &keeper) noexcept;
    virtual ~CInput();

    bool Create(HWND hWnd, long lScreenWidth, long lScreenHeight);
    void ApplyPointerButton(int virtualKey, bool pressed, bool doubleClicked = false) noexcept;
    void Update();

    bool IsKeyDown(int nVirtualKeyCode)
    {
        if (m_bTextEditMode)
            return false;
        return IsPress(nVirtualKeyCode);
    }
    bool IsKeyHeldDown(int nVirtualKeyCode)
    {
        if (m_bTextEditMode)
            return false;
        return IsRepeat(nVirtualKeyCode);
    }

    POINT GetCursorPos()
    {
        return m_ptCursor;
    }
    long GetCursorX()
    {
        return m_ptCursor.x;
    }
    long GetCursorY()
    {
        return m_ptCursor.y;
    }
    long GetDX()
    {
        return m_lDX;
    }
    long GetDY()
    {
        return m_lDY;
    }
    bool IsLBtnDn()
    {
        return m_bLBtnDn;
    }
    bool IsLBtnHeldDn()
    {
        return m_bLBtnHeldDn;
    }
    bool IsLBtnUp()
    {
        return m_bLBtnUp;
    }
    bool IsLBtnDbl()
    {
        return m_bLBtnDbl;
    }
    bool IsRBtnDn()
    {
        return m_bRBtnDn;
    }
    bool IsRBtnHeldDn()
    {
        return m_bRBtnHeldDn;
    }
    bool IsRBtnUp()
    {
        return m_bRBtnUp;
    }
    bool IsRBtnDbl()
    {
        return m_bRBtnDbl;
    }
    bool IsMBtnDn()
    {
        return m_bMBtnDn;
    }
    bool IsMBtnHeldDn()
    {
        return m_bMBtnHeldDn;
    }
    bool IsMBtnUp()
    {
        return m_bMBtnUp;
    }
    bool IsMBtnDbl()
    {
        return m_bMBtnDbl;
    }

    long GetScreenWidth()
    {
        return m_lScreenWidth;
    }
    long GetScreenHeight()
    {
        return m_lScreenHeight;
    }
    void SetLeftHandMode(bool bLeftHand)
    {
        m_bLeftHand = bLeftHand;
    }
    bool IsLeftHandMode()
    {
        return m_bLeftHand;
    }
    void SetTextEditMode(bool bTextEditMode)
    {
        m_bTextEditMode = bTextEditMode;
    }
    bool IsTextEditMode()
    {
        return m_bTextEditMode;
    }
};

struct BundledFont final
{
    std::string family;
    std::string regular;
    std::string bold;
};

// Reads every TTF in ./fonts and groups regular/bold files by embedded family name.
const std::vector<BundledFont> &GetBundledFonts();
// Portable IMM (Windows IME) shim (issue #462, Phase 3).
// On Windows the IME is driven through <imm.h>. On Linux SDL owns IME (the
// portable text field, #447), so the legacy Win32 IMM calls become no-ops: there
// is no IMM context to fetch, and conversion-mode control belongs to the platform
// IME. These stubs just let the legacy IME code paths compile.

#ifdef _WIN32

#else // ---- non-Windows ----------------------------------------------------

// Composition-window form + conversion-mode flags (imm.h).
#ifndef CFS_POINT
#define CFS_POINT 0x0002
#endif
#ifndef IMC_SETCOMPOSITIONWINDOW
#define IMC_SETCOMPOSITIONWINDOW 0x000B
#endif
#ifndef IME_CMODE_ALPHANUMERIC
#define IME_CMODE_ALPHANUMERIC 0x0000
#endif
#ifndef IME_CMODE_NATIVE
#define IME_CMODE_NATIVE 0x0001
#endif
#ifndef IME_SMODE_NONE
#define IME_SMODE_NONE 0x0000
#endif
#ifndef IME_SMODE_AUTOMATIC
#define IME_SMODE_AUTOMATIC 0x0004
#endif

typedef struct tagCOMPOSITIONFORM
{
    DWORD dwStyle;
    POINT ptCurrentPos;
    RECT rcArea;
} COMPOSITIONFORM, *LPCOMPOSITIONFORM;

// No IMM context exists off Windows; SDL handles composition and conversion.
inline HIMC ImmGetContext(HWND)
{
    return nullptr;
}
inline BOOL ImmReleaseContext(HWND, HIMC)
{
    return TRUE;
}
inline HWND ImmGetDefaultIMEWnd(HWND)
{
    return nullptr;
}

inline BOOL ImmGetConversionStatus(HIMC, LPDWORD lpfdwConversion, LPDWORD lpfdwSentence)
{
    if (lpfdwConversion)
        *lpfdwConversion = 0;
    if (lpfdwSentence)
        *lpfdwSentence = 0;
    return TRUE;
}
inline BOOL ImmSetConversionStatus(HIMC, DWORD, DWORD)
{
    return TRUE;
}

#endif // _WIN32
// Portable MessageBox shim (issue #462, Phase 3).
// On Windows MessageBox comes from <windows.h>; elsewhere we route it through
// SDL's message box so the engine's error/info dialogs work on Linux. Only the
// MessageBox surface the engine actually uses is provided.

#ifdef _WIN32

// Real MessageBox is in <windows.h> (pulled in via the PCH on Windows).

#else // ---- non-Windows ----------------------------------------------------

// MessageBox return values (winuser.h).
#ifndef IDOK
#define IDOK 1
#define IDCANCEL 2
#define IDABORT 3
#define IDRETRY 4
#define IDIGNORE 5
#define IDYES 6
#define IDNO 7
#endif

// Shows a modal dialog and returns one of the ID* values above.
int MessageBoxW(HWND owner, LPCWSTR text, LPCWSTR caption, UINT type);

// The engine builds UNICODE, so unqualified MessageBox maps to the wide form
// (exactly as <winuser.h> does on Windows).
#ifndef MessageBox
#define MessageBox MessageBoxW
#endif

// Window messaging. The engine only ever sends WM_DESTROY/WM_CLOSE (a request to
// quit) and one WM_IME_CONTROL (IME, which SDL owns on Linux). The portable
// versions turn the quit messages into an SDL quit and ignore the rest, so the
// main loop's existing SDL_EVENT_QUIT handling stops the game. Implemented in
// AppWindow.cpp.
LRESULT SendMessage(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL PostMessage(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
void PostQuitMessage(int nExitCode);

// Window / cursor queries, backed by SDL. The engine reads the cursor as
// GetCursorPos followed by ScreenToClient(window); SDL_GetMouseState already
// returns window-relative coordinates, so GetCursorPos yields those directly and
// ScreenToClient is the identity. GetActiveWindow reports the focused window (so
// `== NULL` means the game is unfocused). Implemented in AppWindow.cpp.
BOOL GetCursorPos(LPPOINT lpPoint);
BOOL ScreenToClient(HWND hWnd, LPPOINT lpPoint);
HWND GetActiveWindow();
UINT GetDoubleClickTime();

// The keyboard-focus window, or null when the game is unfocused. The engine
// compares this against the main window (g_hWnd) to ask "is the game focused?",
// so it returns g_hWnd while the SDL window holds focus.
HWND GetFocus();

// ---- Legacy Win32 window / display management --------------------------------
// SDL owns the window, GL context and resolution on Linux, so the engine's
// legacy Win32 window/resolution code is stubbed: these no-ops let it compile
// (and report success) while the real work happens through SDL elsewhere.

// Display settings (winuser.h / wingdi.h). Only the fields the resolution code
// touches are provided; layout is irrelevant since ChangeDisplaySettings is a
// no-op here.
typedef struct _devicemodeW
{
    WCHAR dmDeviceName[32];
    WORD dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
    DWORD dmFields;
    LONG dmPositionX, dmPositionY;
    DWORD dmDisplayOrientation, dmDisplayFixedOutput;
    WORD dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
    WCHAR dmFormName[32];
    WORD dmLogPixels;
    DWORD dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
} DEVMODEW, DEVMODE, *LPDEVMODE, *PDEVMODE;

typedef struct tagMSG
{
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
} MSG, *LPMSG, *PMSG;

inline HWND SetFocus(HWND hWnd)
{
    return hWnd;
}
inline BOOL SetForegroundWindow(HWND)
{
    return TRUE;
}
inline BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT)
{
    return TRUE;
}
inline LONG_PTR SetWindowLongPtr(HWND, int, LONG_PTR)
{
    return 0;
}
inline BOOL SetCursorPos(int, int)
{
    return TRUE;
}
inline BOOL AdjustWindowRect(LPRECT, DWORD, BOOL)
{
    return TRUE;
}
inline BOOL ShowWindow(HWND, int)
{
    return TRUE;
}
inline LONG ChangeDisplaySettings(DEVMODE *, DWORD)
{
    return DISP_CHANGE_SUCCESSFUL;
}

// The legacy code pumps the Win32 queue after a resolution change; SDL drives
// the real loop, so there is never anything to peek here.
inline BOOL PeekMessage(LPMSG, HWND, UINT, UINT, UINT)
{
    return FALSE;
}
inline BOOL TranslateMessage(const MSG *)
{
    return FALSE;
}
inline LRESULT DispatchMessage(const MSG *)
{
    return 0;
}

inline int FillRect(HDC, const RECT *, HBRUSH)
{
    return 1;
}

// Cursor / capture / client-rect helpers -- SDL owns the cursor and input.
// ShowCursor keeps Win32 display-counter semantics (visible iff counter >= 0)
// and drives SDL's cursor to match, so the game's "hide the OS cursor, draw my
// own" calls actually take effect. Defined in AppWindow.cpp (needs SDL).
int ShowCursor(BOOL bShow);
// Re-apply the current cursor visibility to SDL. The engine hides the cursor
// before the SDL video subsystem exists; call this once the window is up so the
// pending state takes effect.
void MuApplyCursorVisibility();
inline HWND SetCapture(HWND)
{
    return nullptr;
}
inline BOOL ReleaseCapture()
{
    return TRUE;
}
inline BOOL ClipCursor(const RECT *)
{
    return TRUE;
}
inline BOOL GetClientRect(HWND, LPRECT lpRect)
{
    if (lpRect)
    {
        lpRect->left = lpRect->top = lpRect->right = lpRect->bottom = 0;
    }
    return TRUE;
}
inline BOOL ClientToScreen(HWND, LPPOINT)
{
    return TRUE;
}
inline HWND FindWindowW(LPCWSTR, LPCWSTR)
{
    return nullptr;
}
#ifndef FindWindow
#define FindWindow FindWindowW
#endif
inline HDC GetDC(HWND)
{
    return nullptr;
}
inline int ReleaseDC(HWND, HDC)
{
    return 1;
}

// Display enumeration (winuser.h). No legacy enumeration off Windows; SDL owns
// the modes. ENUM_CURRENT_SETTINGS-style queries report failure.
#ifndef ENUM_CURRENT_SETTINGS
#define ENUM_CURRENT_SETTINGS (static_cast<DWORD>(-1))
#endif
inline BOOL EnumDisplaySettingsW(LPCWSTR, DWORD, DEVMODE *)
{
    return FALSE;
}
#ifndef EnumDisplaySettings
#define EnumDisplaySettings EnumDisplaySettingsW
#endif
inline LPWSTR GetCommandLineW()
{
    static wchar_t empty[1] = {0};
    return empty;
}
#ifndef GetCommandLine
#define GetCommandLine GetCommandLineW
#endif

// Open a document/URL. Returns a "succeeded" sentinel (> 32, as Win32 does).
inline HINSTANCE ShellExecuteW(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, int)
{
    return reinterpret_cast<HINSTANCE>(33);
}
#ifndef ShellExecute
#define ShellExecute ShellExecuteW
#endif

#endif // _WIN32

// Returns the desktop's current bit depth (queried via EnumDisplaySettings),
// falling back to 32 if the query fails. Used by every fullscreen-mode change
// site so we don't hardcode a value that doesn't match the user's display.
extern DWORD GetDesktopBitsPerPel();
