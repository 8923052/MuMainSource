#include "app/Application.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "app/AppWindow.h"
#include "app/ManagedBindingUnit.h"
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
#include "render/Terrain.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/GameSession.h"
#include "session/SessionAudio.h"
#include "session/SessionGameplay.h"
#include "session/SessionNetwork.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "session/SessionRuntime.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/Shell/ShellLogic.h"
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

struct SessionInputEvent;
class SdlGpuRenderBackend;

class ApplicationKeeperLifetime
{
  protected:
    ApplicationKeeperLifetime() = default;
    ~ApplicationKeeperLifetime() = default;

    ApplicationKeeper applicationKeeperStorage_;
};

class Application final : private ApplicationKeeperLifetime, protected ApplicationLegacyCalls
{
  public:
    explicit Application(HINSTANCE instance);
    ~Application();

    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;
    Application(Application &&) = delete;
    Application &operator=(Application &&) = delete;

    int Run();

  private:
    static SdlGpuRenderBackendPtr CreateSdlGpuRenderBackend();
    bool InitializeRenderer();
    void InitializeFramePacing();
    void WriteRendererDiagnostics();
    void ShutdownRenderer() noexcept;
    void ShutdownLegacyWindow() noexcept;
    void Shutdown() noexcept;

    SDL_Window *&g_sdlWindow;
    HWND &g_hWnd;
    HDC &g_hDC;
    HINSTANCE &g_hInst;
    HFONT &g_hFont;
    HFONT &g_hFontBold;
    HFONT &g_hFontBig;
    HFONT &g_hFixFont;
    int &g_iScreenSaverOldValue;
    BOOL &g_bUseWindowMode;
    BOOL &g_bUseFullscreenMode;
    int &OpenglWindowWidth;
    int &OpenglWindowHeight;
    unsigned int &WindowWidth;
    unsigned int &WindowHeight;
    HINSTANCE instance_;
    SessionConfigStore sessionConfigStore_;
    ApplicationConfigUnit applicationConfigUnit_;
    GameData gameData_;
    SdlAppWindowBackend platformBackend_;
    LegacyPlatformBridge legacyPlatformState_;
    AppWindow appWindow_;
    SEASON3B::CNewKeyInput keyInput_;
    CInput input_;
    UI::Modern::RmlUiRuntime modernUiRuntime_;
    SdlGpuRenderBackendPtr sdlGpuRenderBackend_;
    ApplicationFrameUnit applicationFrame_;
    ApplicationDiagnostics applicationDiagnostics_;
    ApplicationAudio applicationAudio_;
    SessionWorkspace sessionWorkspace_;
    ManagedBindingUnit managedBindings_;
    ApplicationNetwork applicationNetwork_;
    ApplicationSessionScheduler sessionScheduler_;
    SessionManager sessionManager_;
    Compositor compositor_;
    SessionFrameView sessionFrame_;
    LegacySessionUiView sessionUi_;
    LegacySessionVisualView sessionVisual_;
    LegacySessionAudioLogicView sessionAudioLogic_;
    SessionIdGenerator sessionIds_;
    ApplicationSessionRuntime sessionRuntime_;
    ApplicationLoopUnit applicationLoop_;
    bool legacyStateInitialized_ = false;
    bool shutdown_ = false;
};

SdlGpuRenderBackendPtr Application::CreateSdlGpuRenderBackend()
{
    return SdlGpuRenderBackendPtr(new SdlGpuRenderBackend());
}

bool Application::InitializeRenderer()
{
    CErrorReport &errorReport = applicationDiagnostics_.ErrorReport();
    const SdlGpuBackendInitializeResult result = sdlGpuRenderBackend_->Initialize(appWindow_);
    if (result == SdlGpuBackendInitializeResult::Success)
    {
        return true;
    }
    errorReport.Write(L"SDL GPU initialization failed (%d).\r\n", static_cast<int>(result));
    MessageBox(nullptr, L"SDL GPU initialization error!", L"Application Error", MB_ICONERROR);
    return false;
}

void Application::InitializeFramePacing()
{
    const ApplicationConfigValues &config = applicationConfigUnit_.Values();
    InitVSync();
    if (!config.vSync)
        DisableVSync();
    applicationFrame_.SetTargetFps(config.fpsLimit);
}

void Application::WriteRendererDiagnostics()
{
    CErrorReport &errorReport = applicationDiagnostics_.ErrorReport();
    errorReport.Write(L"> SDL GPU init success.\r\n");
    errorReport.Write(L"> SDL GPU driver = %ls.\r\n", sdlGpuRenderBackend_->DriverName());
    errorReport.Write(L"> SDL GPU shader artifact = %ls.\r\n",
                      sdlGpuRenderBackend_->ShaderArtifactName());
    errorReport.Write(L"> SDL GPU depth/stencil format = %ls.\r\n",
                      sdlGpuRenderBackend_->DepthStencilFormatName());
    errorReport.Write(L"> SDL GPU swapchain format = %ls.\r\n",
                      sdlGpuRenderBackend_->SwapchainFormatName());
}

void Application::ShutdownRenderer() noexcept
{
    sdlGpuRenderBackend_->Shutdown();
}

BOOL GetFileNameOfFilePath(wchar_t *lpszFile, wchar_t *lpszPath)
{
    auto iFind = (int)'\\';
    wchar_t *lpFound = lpszPath;
    wchar_t *lpOld = lpFound;
    while (lpFound)
    {
        lpOld = lpFound;
        lpFound = wcschr(lpFound + 1, iFind);
    }

    if (wcschr(lpszPath, iFind))
    {
        wcscpy(lpszFile, lpOld + 1);
    }
    else
    {
        wcscpy(lpszFile, lpOld);
    }

    BOOL bCheck = TRUE;
    for (wchar_t *lpTemp = lpszFile; bCheck; ++lpTemp)
    {
        switch (*lpTemp)
        {
        case '\"':
        case '\\':
        case '/':
        case ' ':
            *lpTemp = '\0';
        case '\0':
            bCheck = FALSE;
            break;
        }
    }

    return (TRUE);
}

BOOL GetFileVersion(wchar_t *lpszFileName, WORD *pwVersion)
{
#ifndef _WIN32
    // File version-info is a Win32 crash-report detail; report "unknown".
    (void)lpszFileName;
    (void)pwVersion;
    return FALSE;
#else
    DWORD dwHandle;
    DWORD dwLen = GetFileVersionInfoSize(lpszFileName, &dwHandle);
    if (dwLen <= 0)
    {
        return (FALSE);
    }

    auto *pbyData = new BYTE[dwLen];
    if (!GetFileVersionInfo(lpszFileName, dwHandle, dwLen, pbyData))
    {
        delete[] pbyData;
        return (FALSE);
    }

    VS_FIXEDFILEINFO *pffi;
    UINT uLen;
    if (!VerQueryValue(pbyData, L"\\", (LPVOID *)&pffi, &uLen))
    {
        delete[] pbyData;
        return (FALSE);
    }

    pwVersion[0] = HIWORD(pffi->dwFileVersionMS);
    pwVersion[1] = LOWORD(pffi->dwFileVersionMS);
    pwVersion[2] = HIWORD(pffi->dwFileVersionLS);
    pwVersion[3] = LOWORD(pffi->dwFileVersionLS);

    delete[] pbyData;
    return (TRUE);
#endif
}

namespace
{
#ifdef _WIN32
BOOL ReadCommandLineOption(const std::wstring &commandLine, wchar_t option, std::wstring &value)
{
    int count = 0;
    const std::unique_ptr<wchar_t *, decltype(&LocalFree)> arguments(
        CommandLineToArgvW(commandLine.c_str(), &count), LocalFree);
    if (!arguments)
    {
        return FALSE;
    }
    for (int index = 1; index < count; ++index)
    {
        const std::wstring_view argument(arguments.get()[index]);
        if (argument == L"--session-config")
        {
            ++index;
            continue;
        }
        if (argument.size() >= 2 && argument[0] == L'/' &&
            std::towlower(argument[1]) == std::towlower(option))
        {
            value = argument.substr(2);
            return TRUE;
        }
    }
    return FALSE;
}
#endif
} // namespace

BOOL Util_CheckOption(std::wstring lpszCommandLine, wchar_t cOption, std::wstring &lpszString)
{
#ifdef _WIN32
    return ReadCommandLineOption(lpszCommandLine, cOption, lpszString);
#else
    if (lpszCommandLine.empty())
    {
        return FALSE;
    }

    // Create both lowercase and uppercase variants of the option character
    std::wstring cOptionLower = L"/";
    cOptionLower += static_cast<wchar_t>(towlower(static_cast<wint_t>(cOption)));
    auto foundIndex = lpszCommandLine.find(cOptionLower);
    if (foundIndex == std::wstring::npos)
    {
        std::wstring cOptionUpper = L"/";
        cOptionUpper += static_cast<wchar_t>(towupper(static_cast<wint_t>(cOption)));
        foundIndex = lpszCommandLine.find(cOptionUpper);
    }

    if (foundIndex == std::wstring::npos)
    {
        return FALSE;
    }

    auto endIndex = lpszCommandLine.find(L' ', foundIndex);
    if (endIndex == std::wstring::npos)
    {
        endIndex = lpszCommandLine.length();
    }

    lpszString = lpszCommandLine.substr(foundIndex + 2, endIndex - foundIndex - 2);
    return TRUE;
#endif
}

BOOL GetConnectServerInfo(wchar_t *szCmdLine, wchar_t *lpszURL, WORD *pwPort)
{
    std::wstring lpszTemp = {
        0,
    };

    if (!Util_CheckOption(szCmdLine, L'u', lpszTemp))
    {
        return FALSE;
    }

    wcscpy(lpszURL, lpszTemp.c_str());
    if (!Util_CheckOption(szCmdLine, L'p', lpszTemp))
    {
        return FALSE;
    }

    *pwPort = static_cast<WORD>(std::stoi(lpszTemp));

    return TRUE;
}

void MoveObject(OBJECT *o);

namespace
{
SessionSlotId DefaultFallbackSlot()
{
    return *SessionSlotId::TryCreate(1);
}
} // namespace

Application::Application(HINSTANCE instance)
    : ApplicationKeeperLifetime(), ApplicationLegacyCalls(applicationKeeperStorage_),
      g_sdlWindow(applicationKeeperStorage_.PlatformStorageRef().g_sdlWindow),
      g_hWnd(applicationKeeperStorage_.PlatformStorageRef().g_hWnd),
      g_hDC(applicationKeeperStorage_.PlatformStorageRef().g_hDC),
      g_hInst(applicationKeeperStorage_.PlatformStorageRef().g_hInst),
      g_hFont(applicationKeeperStorage_.PlatformStorageRef().g_hFont),
      g_hFontBold(applicationKeeperStorage_.PlatformStorageRef().g_hFontBold),
      g_hFontBig(applicationKeeperStorage_.PlatformStorageRef().g_hFontBig),
      g_hFixFont(applicationKeeperStorage_.PlatformStorageRef().g_hFixFont),
      g_iScreenSaverOldValue(applicationKeeperStorage_.PlatformStorageRef().g_iScreenSaverOldValue),
      g_bUseWindowMode(applicationKeeperStorage_.PlatformStorageRef().g_bUseWindowMode),
      g_bUseFullscreenMode(applicationKeeperStorage_.PlatformStorageRef().g_bUseFullscreenMode),
      OpenglWindowWidth(applicationKeeperStorage_.PlatformStorageRef().OpenglWindowWidth),
      OpenglWindowHeight(applicationKeeperStorage_.PlatformStorageRef().OpenglWindowHeight),
      WindowWidth(applicationKeeperStorage_.PlatformStorageRef().WindowWidth),
      WindowHeight(applicationKeeperStorage_.PlatformStorageRef().WindowHeight),
      instance_(instance), sessionConfigStore_(applicationKeeper_),
      applicationConfigUnit_(applicationKeeper_, sessionConfigStore_),
      gameData_(applicationKeeper_, ReportLegacyGameDataLoadError),
      legacyPlatformState_(applicationKeeper_),
      appWindow_(applicationKeeper_, platformBackend_, legacyPlatformState_),
      keyInput_(applicationKeeper_), input_(applicationKeeper_),
      modernUiRuntime_(applicationKeeper_), sdlGpuRenderBackend_(CreateSdlGpuRenderBackend()),
      applicationFrame_(applicationKeeper_, appWindow_),
      applicationDiagnostics_(applicationKeeper_, applicationFrame_),
      applicationAudio_(applicationKeeper_,
                        std::make_unique<LegacyApplicationAudioDevice>(
                            applicationKeeperStorage_.AudioStorageRef(),
                            applicationKeeper_.ErrorReport(), applicationKeeper_.ConsoleDebug())),
      sessionWorkspace_(applicationKeeper_, applicationAudio_.WorkspaceView()),
      managedBindings_(applicationKeeper_), applicationNetwork_(applicationKeeper_),
      sessionScheduler_(
          static_cast<std::size_t>(applicationConfigUnit_.Values().sessionWorkerCount)),
      sessionManager_(applicationKeeper_, sessionScheduler_),
      compositor_(applicationKeeper_, *sdlGpuRenderBackend_), sessionFrame_(nullptr),
      sessionRuntime_(applicationKeeper_, sessionWorkspace_, sessionManager_, sessionFrame_,
                      sessionUi_, sessionVisual_, sessionAudioLogic_, sessionIds_,
                      DefaultFallbackSlot(),
                      sessionConfigStore_.ProfileOrDefault(DefaultFallbackSlot())),
      applicationLoop_(applicationKeeper_, sessionWorkspace_, appWindow_, applicationDiagnostics_,
                       applicationNetwork_, sessionManager_, keyInput_, input_, applicationFrame_,
                       compositor_)
{
    applicationKeeper_.DiagnosticsStorageRef().appRingOwnedBytes = sizeof(*this);
    (void)applicationKeeper_.CompleteLinks();
}

Application::~Application()
{
    Shutdown();
}

int Application::Run()
{
    if (!applicationKeeper_.IsReady())
    {
        return 0;
    }

    applicationDiagnostics_.InitializeConsole();
    CErrorReport &g_ErrorReport = applicationDiagnostics_.ErrorReport();
    CmuConsoleDebug &g_ConsoleDebug = applicationDiagnostics_.ConsoleDebug();
    wchar_t lpszExeVersion[256] = L"unknown";

    wchar_t *lpszCommandLine = GetCommandLine();
    wchar_t lpszFile[MAX_PATH];
    WORD wVersion[4] = {
        0,
    };
    if (GetFileNameOfFilePath(lpszFile, lpszCommandLine))
    {
        if (GetFileVersion(lpszFile, wVersion))
        {
            mu_swprintf(lpszExeVersion, L"%d.%02d", wVersion[0], wVersion[1]);
            if (wVersion[2] > 0)
            {
                wchar_t lpszMinorVersion[2] = L"a";
                lpszMinorVersion[0] += (wVersion[2] - 1);
                wcscat(lpszExeVersion, lpszMinorVersion);
            }
        }
    }

    wcsncpy(applicationDiagnostics_.ExecutableVersion(), lpszExeVersion,
            _countof(applicationDiagnostics_.ExecutableVersion()) - 1);

    g_ErrorReport.Write(L"\r\n");
    g_ErrorReport.WriteLogBegin();
    g_ErrorReport.AddSeparator();
    g_ErrorReport.Write(L"Mu online %ls (%ls) executed. (%d.%d.%d.%d)\r\n", lpszExeVersion, L"Eng",
                        wVersion[0], wVersion[1], wVersion[2], wVersion[3]);

    g_ConsoleDebug.Write(MCD_NORMAL, L"Mu Online (Version: %d.%d.%d.%d)", wVersion[0], wVersion[1],
                         wVersion[2], wVersion[3]);

    g_ErrorReport.WriteCurrentTime();
    ER_SystemInfo si;
    ZeroMemory(&si, sizeof(ER_SystemInfo));
    GetSystemInfo(&si);
    g_ErrorReport.AddSeparator();
    g_ErrorReport.WriteSystemInfo(&si);
    g_ErrorReport.AddSeparator();

    g_ErrorReport.Write(L"> To read config.ini.\r\n");

    ApplicationConfigValues &appConfig = applicationConfigUnit_.Values();
    if (!applicationConfigUnit_.IsLoaded())
    {
        g_ErrorReport.Write(L"> Application configuration is invalid.\r\n");
        return 0;
    }
    // Check for command line server override
    WORD wPortNumber;
    if (GetConnectServerInfo(GetCommandLine(), g_lpszCmdURL, &wPortNumber))
    {
        bootstrapServerIp = g_lpszCmdURL;
        bootstrapServerPort = wPortNumber;
    }
    else
    {
        // Use config.ini settings if no command line override
        bootstrapServerIp = appConfig.defaultConnectServerIp;
        bootstrapServerPort = appConfig.defaultConnectServerPort;
    }

    //#ifdef _DEBUG

    g_iChatInputType = 1;

    // Apply window settings from INI
    WindowWidth = appConfig.windowWidth;
    WindowHeight = appConfig.windowHeight;
    g_bUseWindowMode = appConfig.windowed ? TRUE : FALSE;
    g_bUseFullscreenMode = !g_bUseWindowMode;

    // Apply audio settings from INI — volume 0 = off, >0 = on

    if (!gameData_.SelectLanguage(appConfig.assetLanguage))
    {
        return 0;
    }

    g_fScreenRate_x = (float)WindowWidth / (float)REFERENCE_WIDTH;
    g_fScreenRate_y = (float)WindowHeight / (float)REFERENCE_HEIGHT;

    pMultiLanguage = new CMultiLanguage(gameData_.AssetLanguage());

    if (g_iChatInputType == 1)
        ShowCursor(FALSE);

    // Fullscreen is requested via an SDL window flag below; SDL handles the
    // display-mode change and restores it on teardown.
    g_ErrorReport.Write(L"> Screen size = %d x %d.\r\n", static_cast<unsigned int>(WindowWidth),
                        static_cast<unsigned int>(WindowHeight));

    g_hInst = instance_;

    const AppWindowCreateResult windowResult = appWindow_.Initialize({
        "MU Online",
        static_cast<std::int32_t>(WindowWidth),
        static_cast<std::int32_t>(WindowHeight),
        g_bUseWindowMode != TRUE,
    });
    if (windowResult == AppWindowCreateResult::VideoInitializationFailed)
    {
        g_ErrorReport.Write(L"> SDL video init failed.\r\n");
        MessageBox(nullptr, L"Windows aplication error!", L"Aplication Error", MB_ICONERROR);
        return 0;
    }
    if (windowResult == AppWindowCreateResult::WindowCreationFailed)
    {
        g_ErrorReport.Write(L"> SDL_CreateWindow failed.\r\n");
        MessageBox(nullptr, L"Windows aplication error!", L"Aplication Error", MB_ICONERROR);
        return 0;
    }

    g_ErrorReport.Write(L"> Start window success.\r\n");

    // Initialize OpenGL viewport dimensions to match window dimensions
    // This ensures they're correct even if WM_SIZE hasn't fired yet or sent wrong values
    OpenglWindowWidth = WindowWidth;
    OpenglWindowHeight = WindowHeight;

    if (!InitializeRenderer())
    {
        return 0;
    }

#ifdef _WIN32
    // Bridge SDL's native handles so the remaining Win32 code (IME, DirectSound,
    // cursor, the legacy EDIT-control text boxes) keeps working.
    // Drive the existing WndProc from SDL's Win32 messages (transitional, #442).
    SDL_SetWindowsMessageHook(ApplicationLoopUnit::WindowsMessageHookThunk, &applicationLoop_);
#endif // _WIN32

    appWindow_.Raise();
    SetFocus(g_hWnd);

#ifndef _WIN32
    // The engine hid the OS cursor (it draws its own) before the SDL video
    // subsystem existed, so that call could not reach SDL. Apply the pending
    // state now that the window is up. On Windows WM_SETCURSOR keeps doing this.
    MuApplyCursorVisibility();
#endif

    WriteRendererDiagnostics();
    g_ErrorReport.AddSeparator();
    g_ErrorReport.WriteSoundCardInfo();

    if (!applicationAudio_.InitializeLegacyAudio(g_hWnd))
    {
        g_ErrorReport.Write(L"> Audio device initialization failed.\r\n");
        return 0;
    }

    // SDL_CreateWindow already shows the window.

    // Initialize translations with the saved UI locale (defaults to "en").
    // The editor still restores its own MuEditorConfig language preference
    // later in its init, which feeds through to I18N::SetLocale as well.
    {
        std::wstring uiLocaleW = appConfig.uiLocale;
        std::string uiLocale(uiLocaleW.begin(), uiLocaleW.end());
        I18N::SetLocale(uiLocale.c_str());
    }

    g_ErrorReport.WriteImeInfo(g_hWnd);
    g_ErrorReport.AddSeparator();

    InitializeFramePacing();

    // Make the bundled ./fonts faces resolvable by GDI before the first CreateFont,
    // so a chosen curated font works even without a system-wide install.
    RegisterBundledFonts();
    CreateNewFonts(CalculateFontSizes());

    // Log which UI font was resolved now that the fonts have been created (the
    // discovery is lazy on first CreateFont). Helps diagnose "no UI text".
    g_ErrorReport.AddSeparator();
    g_ErrorReport.WriteFontInfo();
    g_ErrorReport.AddSeparator();

    setlocale(LC_ALL, "english");

    Input().Create(g_hWnd, WindowWidth, WindowHeight);

    if (!gameData_.Load())
    {
        return 0;
    }

    {
        int value = AudioPlayer::ClampVolume(appConfig.masterSoundVolume);
        SetEffectVolumeLevel(value);
    }

    auto &timers = frameTimerScheduler_;
    timers.SetRepeating(HACK_TIMER, 20 * 1000, [this] { CheckHack(); });
    timers.SetRepeating(MUHELPER_TIMER, 250 /* ms */, [this] { sessionManager_.TickMuHelpers(); });

    srand((unsigned)time(nullptr));

    for (int &i : RandomTable)
        i = rand() % 360;

    RendomMemoryDump = new BYTE[rand() % 100 + 1];
    SkillAttribute = new SKILL_ATTRIBUTE[MAX_SKILLS]{};
    ItemAttRibuteMemoryDump = new ITEM_ATTRIBUTE[MAX_ITEM + 1024]{};
    ItemAttribute = ((ITEM_ATTRIBUTE *)ItemAttRibuteMemoryDump) + rand() % 1024;
    memset(ItemAttribute, 0, sizeof(ITEM_ATTRIBUTE) * (MAX_ITEM));
    memset(SkillAttribute, 0, sizeof(SKILL_ATTRIBUTE) * (MAX_SKILLS));

    sessionRuntime_.SetSessionInitializer([this](GameSession &session) {
        if (!session.InitializeCharacterPopulationForBootstrap())
        {
            return false;
        }
        session.CharacterMachineForBootstrap().Init();
        if (!session.InitializeLegacyUi())
        {
            return false;
        }
        CUIMng &legacyUiManager = session.LegacyUiManagerForBootstrap();
        legacyUiManager.m_LoginWin->BindConfiguration(session.SlotId(), session.Config(),
                                                      sessionConfigStore_);
        legacyUiManager.Create();
        return g_iChatInputType != 1 || session.InitializeLegacyTextInputs();
    });
    sessionRuntime_.SetSessionFinalizer([](GameSession &session) {
        session.LegacyUiManagerForBootstrap().m_LoginWin->UnbindConfiguration();
    });
    legacyStateInitialized_ = true;
    (void)sessionWorkspace_.SetControlUiScale(appConfig.controlUiScalePercent);
    if (sessionRuntime_.StartConfiguredSessions({
            0,
            0,
            static_cast<std::uint32_t>(WindowWidth),
            static_cast<std::uint32_t>(WindowHeight),
        }) == 0)
    {
        return 0;
    }

    if (g_iChatInputType == 1)
    {
        g_bIMEBlock = FALSE;
        HIMC hIMC = ImmGetContext(g_hWnd);
        ImmSetConversionStatus(hIMC, IME_CMODE_ALPHANUMERIC, IME_SMODE_NONE);
        ImmReleaseContext(g_hWnd, hIMC);
        SaveIMEStatus();
        g_bIMEBlock = TRUE;
    }

#ifdef _WIN32
    if (g_bUseWindowMode == FALSE)
    {
        int nOldVal;
        SystemParametersInfo(SPI_SCREENSAVERRUNNING, 1, &nOldVal, 0);
        SystemParametersInfo(SPI_GETSCREENSAVETIMEOUT, 0, &g_iScreenSaverOldValue, 0);
        SystemParametersInfo(SPI_SETSCREENSAVETIMEOUT, 300 * 60, nullptr, 0);
    }
#endif // _WIN32

    applicationDiagnostics_.StartCpuWorker();
    const MSG msg = MainLoop();

    Shutdown();

    return msg.wParam;
}

void Application::ShutdownLegacyWindow() noexcept
{
    UnregisterBundledFonts();
    appWindow_.ShutdownLegacyFonts();
    if (!legacyStateInitialized_)
    {
        SAFE_DELETE(pMultiLanguage);
        return;
    }
    DestroyWindow();
    legacyStateInitialized_ = false;
}

void Application::Shutdown() noexcept
{
    if (shutdown_)
    {
        return;
    }
    shutdown_ = true;
    appWindow_.RequestClose();

    sessionManager_.StopScheduling();
    sessionWorkspace_.ClearFocus();
    frameTimerScheduler_.Kill(HACK_TIMER);
    frameTimerScheduler_.Kill(MUHELPER_TIMER);
#ifdef _WIN32
    SDL_SetWindowsMessageHook(nullptr, nullptr);
#endif
    applicationDiagnostics_.StopCpuWorker();
    sessionManager_.DeleteSockets();
    sessionRuntime_.Shutdown();
    applicationNetwork_.BeginShutdown();
    (void)applicationConfigUnit_.Save();
    applicationAudio_.ShutdownLegacyAudio();
    ShutdownLegacyWindow();
    (void)applicationKeeper_.BeginShutdown();
    gameData_.Shutdown();
    ShutdownRenderer();
    appWindow_.Shutdown();
    SDL_Quit();
}

int RunApplication(HINSTANCE instance)
{
    Application application(instance);
    return application.Run();
}
