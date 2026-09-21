#include "app/AppWindow.h"
#include "app/Application.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "app/platform/resource.h"
#include "app/SdlGpuRenderBackend.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "data/WorldData.h"
#include "domain/Automation.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Shop.h"
#include "domain/WorldSimulation.h"
#include "I18N/All.h"
#include "render/ModelResources.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/GameSession.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionRender.h"
#include "session/SessionRuntime.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/World/WorldLogic.h" // rozy
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"

#ifdef _WIN32
#include <dpapi.h>
#include <shellapi.h>
#include <io.h>
#include <tlhelp32.h>
#endif

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

AppWindow::AppWindow(ApplicationKeeper &keeper, AppWindowBackend &backend,
                     AppPlatformState &state) noexcept
    : ApplicationLegacyCalls(keeper), backend_(backend), state_(state),
      g_bMinimizedEnabled(keeper.MinimizedEnabled()),
      g_sdlWindow(keeper.PlatformStorageRef().g_sdlWindow),
      g_hWnd(keeper.PlatformStorageRef().g_hWnd), g_hInst(keeper.PlatformStorageRef().g_hInst),
      g_hDC(keeper.PlatformStorageRef().g_hDC), g_hFont(keeper.PlatformStorageRef().g_hFont),
      g_hFontBold(keeper.PlatformStorageRef().g_hFontBold),
      g_hFontBig(keeper.PlatformStorageRef().g_hFontBig),
      g_hFixFont(keeper.PlatformStorageRef().g_hFixFont),
      Destroy(keeper.PlatformStorageRef().Destroy),
      g_iScreenSaverOldValue(keeper.PlatformStorageRef().g_iScreenSaverOldValue),
      g_bUseWindowMode(keeper.PlatformStorageRef().g_bUseWindowMode),
      g_bUseFullscreenMode(keeper.PlatformStorageRef().g_bUseFullscreenMode),
      g_iNoMouseTime(keeper.PlatformStorageRef().g_iNoMouseTime),
      g_bWndActive(keeper.PlatformStorageRef().g_bWndActive),
      g_TargetFpsBeforeInactive(keeper.PlatformStorageRef().g_TargetFpsBeforeInactive),
      g_HasInactiveFpsOverride(keeper.PlatformStorageRef().g_HasInactiveFpsOverride),
      OpenglWindowX(keeper.PlatformStorageRef().OpenglWindowX),
      OpenglWindowY(keeper.PlatformStorageRef().OpenglWindowY),
      OpenglWindowWidth(keeper.PlatformStorageRef().OpenglWindowWidth),
      OpenglWindowHeight(keeper.PlatformStorageRef().OpenglWindowHeight),
      WindowWidth(keeper.PlatformStorageRef().WindowWidth),
      WindowHeight(keeper.PlatformStorageRef().WindowHeight), storage_(keeper.PlatformStorageRef())
{
    (void)applicationKeeper_.RegisterAppWindow(*this);
}

AppWindow::~AppWindow()
{
    Shutdown();
}

AppWindowCreateResult AppWindow::Initialize(AppWindowConfig config) noexcept
{
    if (state_.Window() != nullptr)
    {
        return AppWindowCreateResult::Success;
    }
    if (!backend_.InitializeVideo())
    {
        return AppWindowCreateResult::VideoInitializationFailed;
    }
    state_.SetWindow(
        backend_.CreateAppWindow(config.title, config.width, config.height, config.fullscreen));
    if (state_.Window() == nullptr)
    {
        return AppWindowCreateResult::WindowCreationFailed;
    }

    state_.SetNativeWindow(backend_.GetNativeWindow(state_.Window()));
    state_.SetNativeDeviceContext(backend_.AcquireNativeDeviceContext(state_.NativeWindow()));
    g_hWnd = static_cast<HWND>(state_.NativeWindow());
    g_hDC = static_cast<HDC>(state_.NativeDeviceContext());
    frozenConfig_ = config;
    return AppWindowCreateResult::Success;
}

bool AppWindow::EnsureCreated() noexcept
{
    if (state_.Window() != nullptr)
    {
        return true;
    }
    return Initialize(frozenConfig_) == AppWindowCreateResult::Success;
}

void AppWindow::Raise() noexcept
{
    if (state_.Window() != nullptr)
    {
        backend_.RaiseWindow(state_.Window());
    }
}

void AppWindow::Shutdown() noexcept
{
    if (state_.Window() == nullptr)
    {
        return;
    }

    if (state_.NativeDeviceContext() != nullptr)
    {
        backend_.ReleaseNativeDeviceContext(state_.NativeWindow(), state_.NativeDeviceContext());
        state_.SetNativeDeviceContext(nullptr);
        g_hDC = nullptr;
    }
    state_.SetNativeWindow(nullptr);
    g_hWnd = nullptr;
    backend_.DestroyWindow(state_.Window());
    state_.SetWindow(nullptr);
}

bool AppWindow::IsReady() const noexcept
{
    return state_.Window() != nullptr;
}

BOOL &AppWindow::MinimizedEnabled() noexcept
{
    return g_bMinimizedEnabled;
}

bool &AppWindow::WindowActive() noexcept
{
    return g_bWndActive;
}

BOOL &AppWindow::UseWindowMode() noexcept
{
    return g_bUseWindowMode;
}

BOOL &AppWindow::UseFullscreenMode() noexcept
{
    return g_bUseFullscreenMode;
}

HWND &AppWindow::NativeWindow() noexcept
{
    return g_hWnd;
}

HFONT &AppWindow::UiFont() noexcept
{
    return g_hFont;
}

unsigned int &AppWindow::Width() noexcept
{
    return WindowWidth;
}

unsigned int &AppWindow::Height() noexcept
{
    return WindowHeight;
}

void AppWindow::RequestClose() noexcept
{
    storage_.Destroy = true;
}
bool AppWindow::CloseRequested() const noexcept
{
    return storage_.Destroy;
}

bool AppWindow::SetTextInputActive(bool active) noexcept
{
    if (storage_.textInputActive == active)
    {
        return false;
    }
    storage_.textInputActive = active;
    return true;
}

bool AppWindow::UpdateTextInputArea(SDL_Rect area) noexcept
{
    const SDL_Rect &previous = storage_.lastTextInputArea;
    if (area.x == previous.x && area.y == previous.y && area.w == previous.w &&
        area.h == previous.h)
    {
        return false;
    }
    storage_.lastTextInputArea = area;
    return true;
}

void AppWindow::ShutdownLegacyFonts() noexcept
{
    HFONT *fonts[] = {
        &storage_.g_hFont,
        &storage_.g_hFontBold,
        &storage_.g_hFontBig,
        &storage_.g_hFixFont,
    };
    for (HFONT *font : fonts)
    {
        if (*font != nullptr)
        {
            DeleteObject(static_cast<HGDIOBJ>(*font));
            *font = nullptr;
        }
    }
}

AppWindowHandle AppWindow::Handle() const noexcept
{
    return state_.Window();
}

LegacyPlatformBridge::LegacyPlatformBridge(ApplicationKeeper &keeper) noexcept
    : storage_(keeper.PlatformStorageRef())
{
}

AppWindowHandle LegacyPlatformBridge::Window() const noexcept
{
    return storage_.g_sdlWindow;
}

void LegacyPlatformBridge::SetWindow(AppWindowHandle window) noexcept
{
    storage_.g_sdlWindow = static_cast<SDL_Window *>(window);
}

NativeWindowHandle LegacyPlatformBridge::NativeWindow() const noexcept
{
    return storage_.g_hWnd;
}

void LegacyPlatformBridge::SetNativeWindow(NativeWindowHandle window) noexcept
{
    storage_.g_hWnd = static_cast<HWND>(window);
}

NativeDeviceContextHandle LegacyPlatformBridge::NativeDeviceContext() const noexcept
{
    return storage_.g_hDC;
}

void LegacyPlatformBridge::SetNativeDeviceContext(NativeDeviceContextHandle context) noexcept
{
    storage_.g_hDC = static_cast<HDC>(context);
}

bool SdlAppWindowBackend::InitializeVideo() noexcept
{
    return SDL_InitSubSystem(SDL_INIT_VIDEO);
}

AppWindowHandle SdlAppWindowBackend::CreateAppWindow(const char *title, std::int32_t width,
                                                     std::int32_t height, bool fullscreen) noexcept
{
    SDL_WindowFlags flags = 0;
    if (fullscreen)
    {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    return SDL_CreateWindow(title, width, height, flags);
}

void SdlAppWindowBackend::RaiseWindow(AppWindowHandle window) noexcept
{
    SDL_RaiseWindow(static_cast<SDL_Window *>(window));
}

void SdlAppWindowBackend::DestroyWindow(AppWindowHandle window) noexcept
{
    SDL_DestroyWindow(static_cast<SDL_Window *>(window));
}

NativeWindowHandle SdlAppWindowBackend::GetNativeWindow(AppWindowHandle window) noexcept
{
#ifdef _WIN32
    SDL_Window *sdlWindow = static_cast<SDL_Window *>(window);
    return SDL_GetPointerProperty(SDL_GetWindowProperties(sdlWindow),
                                  SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    (void)window;
    return nullptr;
#endif
}

NativeDeviceContextHandle SdlAppWindowBackend::AcquireNativeDeviceContext(
    NativeWindowHandle window) noexcept
{
#ifdef _WIN32
    return GetDC(static_cast<HWND>(window));
#else
    (void)window;
    return nullptr;
#endif
}

void SdlAppWindowBackend::ReleaseNativeDeviceContext(NativeWindowHandle window,
                                                     NativeDeviceContextHandle context) noexcept
{
#ifdef _WIN32
    ReleaseDC(static_cast<HWND>(window), static_cast<HDC>(context));
#else
    (void)window;
    (void)context;
#endif
}

bool CheckSpecialText(wchar_t *Text)
{
    for (auto *lpszCheck = (wchar_t *)Text; *lpszCheck; ++lpszCheck)
    {
        if (!(48 <= *lpszCheck && *lpszCheck < 58) && !(65 <= *lpszCheck && *lpszCheck < 91) &&
            !(97 <= *lpszCheck && *lpszCheck < 123))
        {
            return true;
        }
    }

    return false;
}

CInput::CInput(ApplicationKeeper &keeper) noexcept : ApplicationLegacyCalls(keeper)
{
    (void)applicationKeeper_.RegisterInput(*this);
}

CInput::~CInput()
{
}

bool CInput::Create(HWND hWnd, long lScreenWidth, long lScreenHeight)
{
#ifdef _WIN32
    // A null window handle is a genuine error on Windows. On the SDL path
    // (issue #462) there is no Win32 HWND, so hWnd is legitimately null; bailing
    // here would leave the screen size unset (0), which makes every centered UI
    // window compute a negative position and pile up in the top-left corner.
    if (hWnd == NULL)
        return false;
#endif

    m_hWnd = hWnd;
    m_lScreenWidth = lScreenWidth;
    m_lScreenHeight = lScreenHeight;

    ::GetCursorPos(&m_ptCursor);
    ::ScreenToClient(m_hWnd, &m_ptCursor);
    m_ptFormerCursor = m_ptCursor;

    m_bLeftHand = false;

    m_dDoubleClickTime = static_cast<double>(::GetDoubleClickTime());

    m_dBtn0LastClickTime = 0.0;
    m_dBtn1LastClickTime = 0.0;
    m_dBtn2LastClickTime = 0.0;

    m_bFormerBtn0Dn = false;
    m_bFormerBtn1Dn = false;
    m_bFormerBtn2Dn = false;

    m_bLBtnHeldDn = false;
    m_bRBtnHeldDn = false;
    m_bMBtnHeldDn = false;

    m_bTextEditMode = false;

    return true;
}

void CInput::ApplyPointerButton(int virtualKey, bool pressed, bool doubleClicked) noexcept
{
    if (m_bLeftHand)
    {
        if (virtualKey == VK_LBUTTON)
            virtualKey = VK_RBUTTON;
        else if (virtualKey == VK_RBUTTON)
            virtualKey = VK_LBUTTON;
    }

    int buttonIndex = -1;
    if (virtualKey == VK_LBUTTON)
        buttonIndex = 0;
    else if (virtualKey == VK_RBUTTON)
        buttonIndex = 1;
    else if (virtualKey == VK_MBUTTON)
        buttonIndex = 2;
    if (buttonIndex < 0)
        return;

    PendingButtonInput &pending = m_pendingButtons[buttonIndex];
    pending.pressed |= pressed;
    pending.released |= !pressed;
    pending.doubleClicked |= doubleClicked;
    pending.held = pressed;
    pending.changed = true;
}

void CInput::Update()
{
    PendingButtonInput pendingButtons[3] = {m_pendingButtons[0], m_pendingButtons[1],
                                            m_pendingButtons[2]};
    for (PendingButtonInput &pending : m_pendingButtons)
        pending = {};

    m_lDX = m_lDY = 0L;
    m_bLBtnUp = m_bRBtnUp = m_bMBtnUp = false;
    m_bLBtnDn = m_bRBtnDn = m_bMBtnDn = false;
    m_bLBtnDbl = m_bRBtnDbl = m_bMBtnDbl = false;

    ::GetCursorPos(&m_ptCursor);
    ::ScreenToClient(m_hWnd, &m_ptCursor);

    m_ptCursor.x = LIMIT(m_ptCursor.x, 0, m_lScreenWidth - 1);
    m_ptCursor.y = LIMIT(m_ptCursor.y, 0, m_lScreenHeight - 1);

    m_lDX = m_ptCursor.x - m_ptFormerCursor.x;
    m_lDY = m_ptCursor.y - m_ptFormerCursor.y;

    if (m_lDX != 0 || m_lDY != 0)
        m_bFormerBtn0Dn = m_bFormerBtn1Dn = m_bFormerBtn2Dn = false;

    m_ptFormerCursor = m_ptCursor;

    const double currentTime = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count();

    if (m_bLeftHand)
    {
        if (IsPress(VK_LBUTTON))
        {
            const double dAbsTime = currentTime;
            if (dAbsTime - m_dBtn0LastClickTime <= m_dDoubleClickTime && m_bFormerBtn0Dn)
            {
                m_bRBtnDn = m_bRBtnHeldDn = m_bRBtnDbl = true;
                m_bRBtnUp = false;
                m_bFormerBtn0Dn = false;
            }
            else
            {
                m_bRBtnDn = m_bRBtnHeldDn = true;
                m_bRBtnUp = m_bRBtnDbl = false;
                m_bFormerBtn0Dn = true;
            }
            m_dBtn0LastClickTime = dAbsTime;
        }
        else if (IsNone(VK_LBUTTON) && m_bRBtnHeldDn)
        {
            m_bRBtnDn = m_bRBtnHeldDn = m_bRBtnDbl = false;
            m_bRBtnUp = true;
        }

        if (IsPress(VK_RBUTTON))
        {
            const double dAbsTime = currentTime;
            if (dAbsTime - m_dBtn1LastClickTime <= m_dDoubleClickTime && m_bFormerBtn1Dn)
            {
                m_bLBtnDn = m_bLBtnHeldDn = m_bLBtnDbl = true;
                m_bLBtnUp = false;
                m_bFormerBtn1Dn = false;
            }
            else
            {
                m_bLBtnDn = m_bLBtnHeldDn = true;
                m_bLBtnUp = m_bLBtnDbl = false;
                m_bFormerBtn1Dn = true;
            }
            m_dBtn1LastClickTime = dAbsTime;
        }
        else if (IsNone(VK_RBUTTON) && m_bLBtnHeldDn)
        {
            m_bLBtnDn = m_bLBtnHeldDn = m_bLBtnDbl = false;
            m_bLBtnUp = true;
        }
    }
    else
    {
        if (IsPress(VK_LBUTTON))
        {
            const double dAbsTime = currentTime;
            if (dAbsTime - m_dBtn0LastClickTime <= m_dDoubleClickTime && m_bFormerBtn0Dn)
            {
                m_bLBtnDn = m_bLBtnHeldDn = m_bLBtnDbl = true;
                m_bLBtnUp = false;
                m_bFormerBtn0Dn = false;
            }
            else
            {
                m_bLBtnDn = m_bLBtnHeldDn = true;
                m_bLBtnUp = m_bLBtnDbl = false;
                m_bFormerBtn0Dn = true;
            }
            m_dBtn0LastClickTime = dAbsTime;
        }
        else if (IsNone(VK_LBUTTON) && m_bLBtnHeldDn)
        {
            m_bLBtnDn = m_bLBtnHeldDn = m_bLBtnDbl = false;
            m_bLBtnUp = true;
        }

        if (IsPress(VK_RBUTTON))
        {
            const double dAbsTime = currentTime;
            if (dAbsTime - m_dBtn1LastClickTime <= m_dDoubleClickTime && m_bFormerBtn1Dn)
            {
                m_bRBtnDn = m_bRBtnHeldDn = m_bRBtnDbl = true;
                m_bRBtnUp = false;
                m_bFormerBtn1Dn = false;
            }
            else
            {
                m_bRBtnDn = m_bRBtnHeldDn = true;
                m_bRBtnUp = m_bRBtnDbl = false;
                m_bFormerBtn1Dn = true;
            }
            m_dBtn1LastClickTime = dAbsTime;
        }
        else if (IsNone(VK_RBUTTON) && m_bRBtnHeldDn)
        {
            m_bRBtnDn = m_bRBtnHeldDn = m_bRBtnDbl = false;
            m_bRBtnUp = true;
        }
    }

    if (IsPress(VK_MBUTTON))
    {
        const double dAbsTime = currentTime;
        if (dAbsTime - m_dBtn2LastClickTime <= m_dDoubleClickTime && m_bFormerBtn2Dn)
        {
            m_bMBtnDbl = true;
            m_bFormerBtn2Dn = false;
        }
        else
        {
            m_bFormerBtn2Dn = true;
            m_bMBtnDbl = false;
        }
        m_bMBtnDn = m_bMBtnHeldDn = true;
        m_bMBtnUp = false;
        m_dBtn2LastClickTime = dAbsTime;
    }
    else if (IsNone(VK_MBUTTON) && m_bMBtnHeldDn)
    {
        m_bMBtnDn = m_bMBtnHeldDn = m_bMBtnDbl = false;
        m_bMBtnUp = true;
    }

    m_bLBtnDn |= pendingButtons[0].pressed;
    m_bLBtnUp |= pendingButtons[0].released;
    m_bLBtnDbl |= pendingButtons[0].doubleClicked;
    if (pendingButtons[0].changed)
        m_bLBtnHeldDn = pendingButtons[0].held;

    m_bRBtnDn |= pendingButtons[1].pressed;
    m_bRBtnUp |= pendingButtons[1].released;
    m_bRBtnDbl |= pendingButtons[1].doubleClicked;
    if (pendingButtons[1].changed)
        m_bRBtnHeldDn = pendingButtons[1].held;

    m_bMBtnDn |= pendingButtons[2].pressed;
    m_bMBtnUp |= pendingButtons[2].released;
    m_bMBtnDbl |= pendingButtons[2].doubleClicked;
    if (pendingButtons[2].changed)
        m_bMBtnHeldDn = pendingButtons[2].held;

    if (GetActiveWindow() == NULL)
    {
        m_lDX = m_lDY = 0L;
        m_bLBtnUp = m_bRBtnUp = m_bMBtnUp = false;
        m_bLBtnDn = m_bRBtnDn = m_bMBtnDn = false;
        m_bLBtnDbl = m_bRBtnDbl = m_bMBtnDbl = false;
        m_ptCursor.x = m_ptCursor.y = 0;
    }
}
namespace
{
std::string FontFamily(const std::vector<unsigned char> &data)
{
    stbtt_fontinfo info{};
    if (data.empty() || !stbtt_InitFont(&info, data.data(), 0))
        return {};
    int length = 0;
    const char *text = stbtt_GetFontNameString(&info, &length, 3, 1, 0x0409, 1);
    if (text == nullptr)
        text = stbtt_GetFontNameString(&info, &length, 3, 1, 0, 1);
    if (text == nullptr || length < 2)
        return {};
    std::wstring wide;
    wide.reserve(static_cast<std::size_t>(length / 2));
    for (int index = 0; index + 1 < length; index += 2)
        wide.push_back(static_cast<wchar_t>((static_cast<unsigned char>(text[index]) << 8) |
                                            static_cast<unsigned char>(text[index + 1])));
    return StringUtils::WideToNarrow(wide.c_str());
}

std::vector<BundledFont> ScanFonts()
{
    std::map<std::string, BundledFont> families;
    std::error_code error;
    for (const auto &file : std::filesystem::directory_iterator("fonts", error))
    {
        std::string extension = file.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (!file.is_regular_file(error) || extension != ".ttf")
            continue;
        std::ifstream stream(file.path(), std::ios::binary);
        const std::vector<unsigned char> data(std::istreambuf_iterator<char>(stream), {});
        const std::string family = FontFamily(data);
        if (family.empty())
            continue;
        const std::string filename = file.path().filename().string();
        const bool regular = filename == family + "-Regular.ttf";
        const bool bold = filename == family + "-Bold.ttf";
        if (!regular && !bold)
            continue;
        BundledFont &font = families[family];
        font.family = family;
        const std::string relative = file.path().generic_string();
        if (bold)
            font.bold = relative;
        else
            font.regular = relative;
    }
    std::vector<BundledFont> result;
    result.reserve(families.size());
    for (auto &[family, font] : families)
    {
        if (font.regular.empty())
            continue;
        if (font.bold.empty())
            font.bold = font.regular;
        result.push_back(std::move(font));
    }
    return result;
}
} // namespace

const std::vector<BundledFont> &GetBundledFonts()
{
    static const std::vector<BundledFont> fonts = ScanFonts();
    return fonts;
}
// Non-Windows implementation of the MessageBox shim.
// Routes the engine's error/info dialogs through SDL's message box.
#ifndef _WIN32

namespace
{
// wchar_t is UTF-32 on Linux; SDL wants UTF-8. Encode directly so non-ASCII
// (e.g. Korean) text survives instead of relying on the C locale.
std::string ToUtf8(LPCWSTR s)
{
    std::string out;
    if (s == nullptr)
        return out;
    for (const wchar_t *p = s; *p; ++p)
    {
        unsigned int c = static_cast<unsigned int>(*p);
        // Map surrogates and out-of-range values to U+FFFD so we never emit
        // invalid UTF-8 to SDL / the font renderer.
        if ((c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF)
            c = 0xFFFD;

        if (c < 0x80)
        {
            out.push_back(static_cast<char>(c));
        }
        else if (c < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
        else if (c < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

Uint32 IconFlags(UINT type)
{
    if (type & (MB_ICONERROR | MB_ICONSTOP))
        return SDL_MESSAGEBOX_ERROR;
    if (type & MB_ICONEXCLAMATION)
        return SDL_MESSAGEBOX_WARNING;
    return SDL_MESSAGEBOX_INFORMATION;
}
} // namespace

int MessageBoxW(HWND /*owner*/, LPCWSTR text, LPCWSTR caption, UINT type)
{
    const std::string message = ToUtf8(text);
    const std::string title = caption ? ToUtf8(caption) : std::string("MU");

    // Parent the dialog to the focused window so it stays in front and modal to
    // the game; a null parent can spawn behind the window on some Linux WMs.
    SDL_Window *parent = SDL_GetKeyboardFocus();

    if ((type & MB_YESNO) == MB_YESNO)
    {
        const SDL_MessageBoxButtonData buttons[2] = {
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, IDYES, "Yes"},
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, IDNO, "No"},
        };
        const SDL_MessageBoxData data = {IconFlags(type), parent, title.c_str(), message.c_str(), 2,
                                         buttons,         nullptr};
        int buttonId = IDNO;
        if (!SDL_ShowMessageBox(&data, &buttonId))
            return IDNO; // dialog failed/dismissed
        return (buttonId == IDYES) ? IDYES : IDNO;
    }

    SDL_ShowSimpleMessageBox(IconFlags(type), title.c_str(), message.c_str(), parent);
    return IDOK;
}

namespace
{
bool IsQuitMessage(UINT msg)
{
    return msg == WM_DESTROY || msg == WM_CLOSE || msg == WM_QUIT;
}

bool RequestQuit()
{
    SDL_Event quit;
    SDL_zero(quit);
    quit.type = SDL_EVENT_QUIT;
    if (!SDL_PushEvent(&quit))
    {
        SDL_Log("Failed to push quit event: %s", SDL_GetError());
        return false;
    }
    return true;
}
} // namespace

LRESULT SendMessage(HWND /*hWnd*/, UINT Msg, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    if (IsQuitMessage(Msg))
        RequestQuit();
    return 0;
}

BOOL PostMessage(HWND /*hWnd*/, UINT Msg, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    if (IsQuitMessage(Msg))
        return RequestQuit() ? TRUE : FALSE;
    return TRUE;
}

void PostQuitMessage(int /*nExitCode*/)
{
    RequestQuit();
}

BOOL GetCursorPos(LPPOINT lpPoint)
{
    if (!lpPoint)
        return FALSE;
    float x = 0.0f, y = 0.0f;
    SDL_GetMouseState(&x, &y); // already window-relative
    lpPoint->x = static_cast<LONG>(x);
    lpPoint->y = static_cast<LONG>(y);
    return TRUE;
}

// GetCursorPos already returns client-space coordinates, so this is the identity.
BOOL ScreenToClient(HWND /*hWnd*/, LPPOINT lpPoint)
{
    return lpPoint ? TRUE : FALSE;
}

HWND GetActiveWindow()
{
    return reinterpret_cast<HWND>(SDL_GetKeyboardFocus()); // null when unfocused
}

UINT GetDoubleClickTime()
{
    return 500; // the Win32 default
}

HWND GetFocus()
{
    // The UI asks "is the game focused?" with `GetFocus() == g_hWnd`, and
    // compares the result against widget handles that derive from g_hWnd. On the
    // SDL path g_hWnd is null (the Win32 HWND bridge is Windows-only), and the
    // portable text fields (#447) don't take Win32 focus, so the main window is
    // always treated as focused: returning null keeps every `== g_hWnd`
    // comparison consistent without the platform layer depending on g_hWnd.
    // (Note: this can't be GetActiveWindow()/SDL_GetKeyboardFocus(), which return
    // the SDL window rather than g_hWnd and would invert the comparison.)
    return nullptr;
}

namespace
{
// Win32 keeps a process-wide cursor display counter; the cursor is shown
// while it is >= 0. The game and the editor toggle it to hide the OS cursor
// (they draw their own) and to reveal it over editor UI. Start at 0 to match
// the Win32 default (cursor visible).
int g_cursorDisplayCount = 0;

// Some Wayland compositors (seen on KDE Plasma) do not reliably drop the
// pointer for SDL_HideCursor() alone, leaving the OS cursor composited over
// the game's own cursor sprite (issue #462). Committing an explicit blank
// (1x1 transparent) cursor in addition to hiding is honoured everywhere, so
// keep one around and set it whenever the cursor should be hidden.
SDL_Cursor *BlankCursor()
{
    static SDL_Cursor *blank = nullptr;
    if (blank == nullptr)
    {
        Uint32 transparent = 0; // single fully-transparent ARGB pixel
        SDL_Surface *surface = SDL_CreateSurfaceFrom(1, 1, SDL_PIXELFORMAT_ARGB8888, &transparent,
                                                     sizeof(transparent));
        if (surface != nullptr)
        {
            blank = SDL_CreateColorCursor(surface, 0, 0);
            SDL_DestroySurface(surface); // cursor keeps its own copy
        }
    }
    return blank;
}

void ApplyCursorVisibility()
{
    // No-op until the SDL video subsystem is up; MuApplyCursorVisibility()
    // re-applies the pending state once the window exists.
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
        return;
    if (g_cursorDisplayCount >= 0)
    {
        // Restore a real cursor first: a prior hide may have left the blank
        // one active, so SDL_ShowCursor() alone would show nothing.
        SDL_SetCursor(SDL_GetDefaultCursor());
        SDL_ShowCursor();
    }
    else
    {
        SDL_HideCursor();
        if (SDL_Cursor *blank = BlankCursor())
            SDL_SetCursor(blank);
    }
}
} // namespace

int ShowCursor(BOOL bShow)
{
    g_cursorDisplayCount += bShow ? 1 : -1;
    ApplyCursorVisibility();
    return g_cursorDisplayCount;
}

void MuApplyCursorVisibility()
{
    ApplyCursorVisibility();
}

#endif // !_WIN32

// Win32 CBT-hook implementation; non-Windows builds use the SDL MessageBox.
#ifdef _WIN32

int AppWindow::CBTMessageBox(HWND hWnd, const std::wstring &text, const std::wstring &caption,
                             UINT uType, bool bAlwaysOnTop)
{
    return cbtMessageBox_.OpenMessageBox(hWnd, text.c_str(), caption.c_str(), uType, bAlwaysOnTop);
}

int ApplicationLegacyCalls::CBTMessageBox(HWND hWnd, const std::wstring &text,
                                          const std::wstring &caption, UINT uType,
                                          bool bAlwaysOnTop)
{
    return applicationKeeper_.AppWindowUnit()->CBTMessageBox(hWnd, text, caption, uType,
                                                             bAlwaysOnTop);
}

namespace
{
constexpr wchar_t kOwnerPropertyName[] = L"MuTwo.CBTMessageBox.Owner";

int ClampInt(int value, int minValue, int maxValue)
{
    return std::min<int>(std::max<int>(value, minValue), maxValue);
}

POINT ComputeCenteredTopLeft(const RECT &parentRect, const RECT &childRect, const RECT &desktopRect)
{
    const int width = (childRect.right - childRect.left);
    const int height = (childRect.bottom - childRect.top);

    const int centerX = parentRect.left + ((parentRect.right - parentRect.left) / 2);
    const int centerY = parentRect.top + ((parentRect.bottom - parentRect.top) / 2);

    POINT result{};
    result.x = centerX - (width / 2);
    result.y = centerY - (height / 2);

    const int desktopRight = static_cast<int>(desktopRect.right);
    const int desktopBottom = static_cast<int>(desktopRect.bottom);
    const int maxX = std::max<int>(0, desktopRight - width);
    const int maxY = std::max<int>(0, desktopBottom - height);

    result.x = ClampInt(result.x, 0, maxX);
    result.y = ClampInt(result.y, 0, maxY);
    return result;
}

struct ScopedCbtUnhook
{
    leaf::CCBTMessageBox *owner{nullptr};

    explicit ScopedCbtUnhook(leaf::CCBTMessageBox *p) : owner(p)
    {
    }

    ScopedCbtUnhook(const ScopedCbtUnhook &) = delete;
    ScopedCbtUnhook &operator=(const ScopedCbtUnhook &) = delete;
    ScopedCbtUnhook(ScopedCbtUnhook &&) = delete;
    ScopedCbtUnhook &operator=(ScopedCbtUnhook &&) = delete;
    ~ScopedCbtUnhook()
    {
        if (owner)
        {
            owner->UnhookCBT();
        }
    }
};
} // namespace

leaf::CCBTMessageBox::CCBTMessageBox()
    : m_hParentWnd(nullptr), m_hCBT(nullptr), m_bAlwaysOnTop(false)
{
}

leaf::CCBTMessageBox::~CCBTMessageBox()
{
    UnhookCBT();
}

int leaf::CCBTMessageBox::OpenMessageBox(HWND hWnd, const wchar_t *lpText, const wchar_t *lpCaption,
                                         UINT uType, bool bAlwaysOnTop)
{
    m_hParentWnd = (hWnd != nullptr) ? hWnd : GetDesktopWindow();
    m_bAlwaysOnTop = bAlwaysOnTop;

    if (!HookCBT())
    {
        return 0;
    }

    ScopedCbtUnhook unhookGuard{this};
    return MessageBox(m_hParentWnd, lpText, lpCaption, uType);
}

HWND leaf::CCBTMessageBox::GetParentWndHandle() const
{
    return m_hParentWnd;
}
bool leaf::CCBTMessageBox::IsAlwaysOnTop() const
{
    return m_bAlwaysOnTop;
}

HHOOK leaf::CCBTMessageBox::GetHookHandle() const
{
    return m_hCBT;
}
bool leaf::CCBTMessageBox::HookCBT()
{
    if (m_hCBT)
        return false;
    if (!SetPropW(m_hParentWnd, kOwnerPropertyName, reinterpret_cast<HANDLE>(this)))
        return false;
    m_hCBT = SetWindowsHookEx(WH_CBT, &CBTProc, 0, GetCurrentThreadId());
    if (m_hCBT == nullptr)
    {
        RemovePropW(m_hParentWnd, kOwnerPropertyName);
        return false;
    }
    return true;
}
void leaf::CCBTMessageBox::UnhookCBT()
{
    if (m_hCBT)
    {
        UnhookWindowsHookEx(m_hCBT);
        m_hCBT = nullptr;
    }
    if (m_hParentWnd != nullptr &&
        GetPropW(m_hParentWnd, kOwnerPropertyName) == reinterpret_cast<HANDLE>(this))
    {
        RemovePropW(m_hParentWnd, kOwnerPropertyName);
    }
}

LRESULT CALLBACK leaf::CCBTMessageBox::CBTProc(INT nCode, WPARAM wParam, LPARAM lParam)
{
    HWND hChildWnd = reinterpret_cast<HWND>(wParam);
    HWND hParentWnd = hChildWnd != nullptr ? GetWindow(hChildWnd, GW_OWNER) : nullptr;
    auto *owner = hParentWnd != nullptr
                      ? reinterpret_cast<CCBTMessageBox *>(GetPropW(hParentWnd, kOwnerPropertyName))
                      : nullptr;
    HHOOK hook = owner != nullptr ? owner->GetHookHandle() : nullptr;

    // notification that a window is about to be activated
    // window handle is wParam
    if (nCode == HCBT_ACTIVATE && hook != nullptr)
    {
        RECT rParent{};
        RECT rChild{};
        RECT rDesktop{};

        // exit CBT hook
        owner->UnhookCBT();

        if ((hParentWnd != nullptr) && (hChildWnd != nullptr) &&
            (GetWindowRect(GetDesktopWindow(), &rDesktop) != 0) &&
            (GetWindowRect(hParentWnd, &rParent) != 0) && (GetWindowRect(hChildWnd, &rChild) != 0))
        {
            const POINT pStart = ComputeCenteredTopLeft(rParent, rChild, rDesktop);

            // move message box
            HWND hWndInsertAfter = nullptr;
            if (owner->IsAlwaysOnTop())
                hWndInsertAfter = HWND_TOPMOST;
            SetWindowPos(hChildWnd, hWndInsertAfter, pStart.x, pStart.y, 0, 0,
                         SWP_HIDEWINDOW | SWP_NOSIZE);
        }
    }
    else if (nCode == HCBT_DESTROYWND && owner != nullptr)
    {
        // exit CBT hook
        owner->UnhookCBT();
    }
    // otherwise, continue with any possible chained hooks
    else
        CallNextHookEx(hook, nCode, wParam, lParam);

    return 0;
}
#endif // _WIN32

void MoveObject(OBJECT *o);

// Resolution change through SDL (issue #462). SDL owns the window on every
// platform, so resize it via SDL rather than the OS. The old Windows path in
// ApplyResolution() drove Win32 SetWindowPos/ChangeDisplaySettings on g_hWnd,
// which fought SDL: it pins the min/max tracking size of a non-resizable
// window, so a raw SetWindowPos was clamped back and the resolution never
// changed unless a windowed/fullscreen toggle reset the style first.
// SDL_SetWindowSize resizes regardless of the resizable flag and drives the
// same HandleWindowResize update synchronously, so callers can Save() config
// right after and see the size the window actually ended up with.
void AppWindow::MuApplyWindowResolution(unsigned int width, unsigned int height, bool windowed)
{
    if (!g_sdlWindow || width == 0 || height == 0)
        return;
    const int w = static_cast<int>(width);
    const int h = static_cast<int>(height);

    if (windowed)
    {
        SDL_SetWindowFullscreen(g_sdlWindow, false);
        SDL_SetWindowSize(g_sdlWindow, w, h);
        SDL_SetWindowPosition(g_sdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    else
    {
        // Pick the closest real fullscreen mode so the monitor switches
        // resolution; fall back to borderless desktop if none matches.
        SDL_DisplayMode mode;
        const SDL_DisplayID display = SDL_GetDisplayForWindow(g_sdlWindow);
        if (SDL_GetClosestFullscreenDisplayMode(display, w, h, 0.0f, false, &mode))
            SDL_SetWindowFullscreenMode(g_sdlWindow, &mode);
        else
            SDL_SetWindowFullscreenMode(g_sdlWindow, nullptr);
        SDL_SetWindowSize(g_sdlWindow, w, h);
        SDL_SetWindowFullscreen(g_sdlWindow, true);
    }

    // The request is not a guarantee: the closest fullscreen mode can differ
    // from what was asked, the borderless fallback is desktop-sized, and mode
    // switches are asynchronous on some window managers. Settle the request,
    // then resize the game to the size the window really got - callers persist
    // WindowWidth/Height, and config must record what happened, not what was
    // asked for. Logical size, matching what SDL_EVENT_WINDOW_RESIZED carries.
    SDL_SyncWindow(g_sdlWindow);
    int actualW = w, actualH = h;
    SDL_GetWindowSize(g_sdlWindow, &actualW, &actualH);
    HandleWindowResize(actualW, actualH);
}

namespace
{
// Tahoma font size scales with window height; these are the tuned base values.
constexpr int BASE_FONT_HEIGHT = 12;
constexpr float FONT_HEIGHT_GROWTH_PER_PIXEL = 1.f / 200.f;
constexpr int FIX_FONT_HEIGHT_SMALL = 14; // used when WindowHeight <= 600
constexpr int FIX_FONT_HEIGHT_LARGE = 15;
constexpr int SMALL_WINDOW_HEIGHT_THRESHOLD = 600;

#ifdef _WIN32
// Absolute path of a bundled font file (relative to ./fonts) next to the exe.
std::wstring BundledFontFullPath(const char *relative)
{
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring path(exePath);
    path.resize(path.find_last_of(L"\\/") + 1); // keep the directory + separator
    while (*relative)
        path.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*relative++)));
    return path;
}
#endif

} // namespace

FontSizes AppWindow::CalculateFontSizes() noexcept
{
    FontHeight = static_cast<int>(std::ceil(BASE_FONT_HEIGHT + (WindowHeight - REFERENCE_HEIGHT) *
                                                                   FONT_HEIGHT_GROWTH_PER_PIXEL));
    const int fixFontHeight = WindowHeight <= SMALL_WINDOW_HEIGHT_THRESHOLD ? FIX_FONT_HEIGHT_SMALL
                                                                            : FIX_FONT_HEIGHT_LARGE;
    return {FontHeight - 1, fixFontHeight - 1};
}

void AppWindow::RegisterBundledFonts()
{
#ifdef _WIN32
    for (const auto &font : GetBundledFonts())
    {
        AddFontResourceExW(BundledFontFullPath(font.regular.c_str()).c_str(), FR_PRIVATE, nullptr);
        if (font.bold != font.regular)
            AddFontResourceExW(BundledFontFullPath(font.bold.c_str()).c_str(), FR_PRIVATE, nullptr);
    }
#endif
}

void AppWindow::UnregisterBundledFonts()
{
#ifdef _WIN32
    for (const auto &font : GetBundledFonts())
    {
        RemoveFontResourceExW(BundledFontFullPath(font.regular.c_str()).c_str(), FR_PRIVATE,
                              nullptr);
        if (font.bold != font.regular)
            RemoveFontResourceExW(BundledFontFullPath(font.bold.c_str()).c_str(), FR_PRIVATE,
                                  nullptr);
    }
#endif
}

HFONT AppWindow::CreateUIFont(int size, int weight, const std::wstring &selectedFont) noexcept
{
    const wchar_t *face = selectedFont.empty() ? L"Tahoma" : selectedFont.c_str();
    return CreateFont(size, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                      CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                      face);
}

HFONT AppWindow::CreateUIFont(int size, int weight) noexcept
{
    return CreateUIFont(size, weight, applicationKeeper_.ApplicationConfig().font);
}

void AppWindow::CreateNewFonts(FontSizes sizes)
{
    g_hFont = CreateUIFont(sizes.uiFontSize, FW_NORMAL);
    g_hFontBold = CreateUIFont(sizes.uiFontSize, FW_SEMIBOLD);
    g_hFontBig = CreateUIFont(sizes.uiFontSize * 2, FW_SEMIBOLD);
    g_hFixFont = CreateUIFont(sizes.fixFontSize, FW_NORMAL);
}

void AppWindow::ReinitializeTextRenderer()
{
    CUIRenderText &renderText = applicationKeeper_.GraphicsStorageRef().renderText;
    renderText.Release();
    renderText.Create();
}

// Reinitialize fonts when window resolution changes
void AppWindow::ReinitializeFonts()
{
    // Save old font handles so we can delete them after the renderer has switched over
    HFONT hOldFont = g_hFont;
    HFONT hOldFontBold = g_hFontBold;
    HFONT hOldFontBig = g_hFontBig;
    HFONT hOldFixFont = g_hFixFont;

    FontSizes sizes = CalculateFontSizes();
    CreateNewFonts(sizes);
    ReinitializeTextRenderer();

    if (hOldFont)
        DeleteObject(hOldFont);
    if (hOldFontBold)
        DeleteObject(hOldFontBold);
    if (hOldFontBig)
        DeleteObject(hOldFontBig);
    if (hOldFixFont)
        DeleteObject(hOldFixFont);

    Input().Create(g_hWnd, WindowWidth, WindowHeight);
    // Text fields render through g_RenderText and resolve their font by kind
    // each frame, so they need no per-control rebuild after a resolution change.
}

void AppWindow::HandleWindowResize(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }
    WindowWidth = static_cast<unsigned int>(width);
    WindowHeight = static_cast<unsigned int>(height);
    g_fScreenRate_x = static_cast<float>(WindowWidth) / static_cast<float>(REFERENCE_WIDTH);
    g_fScreenRate_y = static_cast<float>(WindowHeight) / static_cast<float>(REFERENCE_HEIGHT);
    OpenglWindowWidth = width;
    OpenglWindowHeight = height;
    ReinitializeFonts();
    UpdateResolutionDependentSystems();
    UpdateCursorClip();
}

void AppWindow::HandleFocusChange(bool active)
{
    if (!active)
    {
#ifdef ACTIVE_FOCUS_OUT
        if (g_bUseWindowMode == FALSE)
#endif
            g_bWndActive = false;
        ClipCursor(nullptr);

        if (g_bUseWindowMode == FALSE && !g_HasInactiveFpsOverride)
        {
            g_TargetFpsBeforeInactive = GetTargetFps();
            SetTargetFps(REFERENCE_FPS);
            g_HasInactiveFpsOverride = true;
        }
        return;
    }

    g_bWndActive = true;
    if (g_HasInactiveFpsOverride)
    {
        SetTargetFps(g_TargetFpsBeforeInactive);
        g_HasInactiveFpsOverride = false;
    }
    UpdateCursorClip();
}

DWORD GetDesktopBitsPerPel()
{
    DEVMODE dm = {};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &dm))
        return dm.dmBitsPerPel;
    return 32;
}

void AppWindow::UpdateCursorClip() noexcept
{
    // Confine cursor in fullscreen + active only. In windowed mode the user
    // must be able to move the cursor to other windows; when deactivated we
    // must also release so Windows can focus other apps.
    if (!g_hWnd || g_bUseWindowMode || !g_bWndActive)
    {
        ClipCursor(nullptr);
        return;
    }
    RECT client;
    if (!GetClientRect(g_hWnd, &client))
        return;
    POINT tl = {client.left, client.top};
    POINT br = {client.right, client.bottom};
    ClientToScreen(g_hWnd, &tl);
    ClientToScreen(g_hWnd, &br);
    RECT clip = {tl.x, tl.y, br.x, br.y};
    ClipCursor(&clip);
}

// Update camera state when window resolution changes
void AppWindow::UpdateResolutionDependentSystems()
{
    if (ApplicationSessionRuntime *runtime = applicationKeeper_.ApplicationSessionRuntimeUnit())
    {
        runtime->UpdateResolutionDependentSystems();
    }
}

FontSizes ApplicationLegacyCalls::CalculateFontSizes()
{
    return applicationKeeper_.AppWindowUnit()->CalculateFontSizes();
}
HFONT ApplicationLegacyCalls::CreateUIFont(int size, int weight)
{
    return applicationKeeper_.AppWindowUnit()->CreateUIFont(size, weight);
}
void ApplicationLegacyCalls::CreateNewFonts(FontSizes sizes)
{
    applicationKeeper_.AppWindowUnit()->CreateNewFonts(sizes);
}
void ApplicationLegacyCalls::ReinitializeTextRenderer()
{
    applicationKeeper_.AppWindowUnit()->ReinitializeTextRenderer();
}
void ApplicationLegacyCalls::HandleWindowResize(int width, int height)
{
    applicationKeeper_.AppWindowUnit()->HandleWindowResize(width, height);
}
void ApplicationLegacyCalls::HandleFocusChange(bool active)
{
    applicationKeeper_.AppWindowUnit()->HandleFocusChange(active);
}
void ApplicationLegacyCalls::MuApplyWindowResolution(unsigned int width, unsigned int height,
                                                     bool windowed)
{
    applicationKeeper_.AppWindowUnit()->MuApplyWindowResolution(width, height, windowed);
}
void ApplicationLegacyCalls::RegisterBundledFonts()
{
    applicationKeeper_.AppWindowUnit()->RegisterBundledFonts();
}
void ApplicationLegacyCalls::UnregisterBundledFonts()
{
    applicationKeeper_.AppWindowUnit()->UnregisterBundledFonts();
}
void ApplicationLegacyCalls::ReinitializeFonts()
{
    applicationKeeper_.AppWindowUnit()->ReinitializeFonts();
}
void ApplicationLegacyCalls::UpdateResolutionDependentSystems()
{
    applicationKeeper_.AppWindowUnit()->UpdateResolutionDependentSystems();
}
void ApplicationLegacyCalls::UpdateCursorClip()
{
    applicationKeeper_.AppWindowUnit()->UpdateCursorClip();
}

void AppWindow::SaveIMEStatus()
{
    HIMC hIMC = ImmGetContext(g_hWnd);
    ImmGetConversionStatus(hIMC, &g_dwBKConv, &g_dwBKSent);
    ImmSetConversionStatus(hIMC, IME_CMODE_ALPHANUMERIC, IME_SMODE_NONE);
    ImmReleaseContext(g_hWnd, hIMC);
}

void AppWindow::RestoreIMEStatus()
{
    HIMC hIMC = ImmGetContext(g_hWnd);
    ImmSetConversionStatus(hIMC, g_dwBKConv, g_dwBKSent);
    ImmReleaseContext(g_hWnd, hIMC);
}

void AppWindow::CheckTextInputBoxIME(int iMode)
{
    if (g_bIMEBlock == FALSE)
        return;
    if (iMode & IME_CONVERSIONMODE)
    {
        if (/*InputEnable == false && */ g_bForceIMEConv == TRUE)
        {
            g_bForceIMEConv = FALSE;
            return;
        }

        HIMC hIMC = ImmGetContext(g_hWnd);

        DWORD dwConv, dwSent;
        ImmGetConversionStatus(hIMC, &dwConv, &dwSent);
        if (dwConv != IME_CMODE_ALPHANUMERIC)
        {
            g_dwBKConv = dwConv;
            g_bForceIMEConv = TRUE;
            ImmSetConversionStatus(hIMC, IME_CMODE_ALPHANUMERIC, IME_SMODE_NONE);
        }

        ImmReleaseContext(g_hWnd, hIMC);
        //		g_bForceIMEConv = FALSE;
    }
    if (iMode & IME_SENTENCEMODE)
    {
        if (/*InputEnable == false && */ g_bForceIMESent == TRUE)
        {
            g_bForceIMESent = FALSE;
            return;
        }

        HIMC hIMC = ImmGetContext(g_hWnd);
        DWORD dwConv, dwSent;
        ImmGetConversionStatus(hIMC, &dwConv, &dwSent);
        if (dwSent != IME_SMODE_NONE)
        {
            g_dwBKSent = dwSent;
            g_bForceIMESent = TRUE;
            ImmSetConversionStatus(hIMC, IME_CMODE_ALPHANUMERIC, IME_SMODE_NONE);
        }
        ImmReleaseContext(g_hWnd, hIMC);
        //		g_bForceIMESent = FALSE;
    }
}

void ApplicationLegacyCalls::SaveIMEStatus()
{
    applicationKeeper_.AppWindowUnit()->SaveIMEStatus();
}

void ApplicationLegacyCalls::RestoreIMEStatus()
{
    applicationKeeper_.AppWindowUnit()->RestoreIMEStatus();
}

void ApplicationLegacyCalls::CheckTextInputBoxIME(int mode)
{
    applicationKeeper_.AppWindowUnit()->CheckTextInputBoxIME(mode);
}
