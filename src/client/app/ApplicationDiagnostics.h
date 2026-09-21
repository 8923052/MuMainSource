#pragma once
#include "support/CoreMath.h"
#include "app/Application.h"
#include "app/ApplicationConfigScheduling.h"
#include "session/SessionRuntime.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#define MAX_LENGTH_CPUNAME (128)
#define MAX_LENGTH_OSINFO (128)
#define MAX_DXVERSION (128)
#define HACK_TIMER 1000

class CpuUsage
{
  public:
    CpuUsage();
    ~CpuUsage();
    double GetUsage();

  private:
    class Impl;                  // Forward declaration of the implementation
    std::unique_ptr<Impl> pImpl; // Pointer to implementation
};

typedef struct
{
    wchar_t m_lpszCPU[MAX_LENGTH_CPUNAME];
    wchar_t m_lpszOS[MAX_LENGTH_OSINFO];
    int m_iMemorySize;

    wchar_t m_lpszDxVersion[MAX_DXVERSION];
} ER_SystemInfo;

class CErrorReport
{
  public:
    CErrorReport();
    virtual ~CErrorReport();

    void Clear(void);

  protected:
    HANDLE m_hFile;
    wchar_t m_lpszFileName[MAX_PATH];
    int m_iKey;

  public:
    void Create(const wchar_t *lpszFileName);
    void Destroy(void);

  protected:
    void CutHead(void);
    char *CheckHeadToCut(char *lpszBuffer, DWORD dwNumber);

  protected:
    BOOL WriteFile(HANDLE hFile, void *lpBuffer, DWORD nNumberOfBytesToWrite,
                   LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);

  public:
    void WriteDebugInfoStr(wchar_t *lpszToWrite);
    void Write(const wchar_t *lpszFormat, ...);
    void HexWrite(void *pBuffer, int iSize);

    void AddSeparator(void);
    void WriteLogBegin(void);
    void WriteCurrentTime(BOOL bLineShift = TRUE);

    void WriteSystemInfo(ER_SystemInfo *si);
    void WriteFontInfo(void);
    void WriteImeInfo(HWND hWnd);
    void WriteSoundCardInfo(void);
};

void GetSystemInfo(ER_SystemInfo *si);
#ifndef _WINDOWSCONSOLE_H_
#define _WINDOWSCONSOLE_H_

// Self-contained: this header declares HWND/WORD/FOREGROUND_* in its interface,
// so pull the Win32 types in directly instead of relying on include order.

#pragma warning(disable : 4786)

namespace leaf
{

class CConsoleWindow
{
    CTimer2 m_LimitTimer;
    HWND m_hWnd;
    bool m_bActiveCloseButton;
    bool m_started;
    std::wstring m_title;

  public:
    CConsoleWindow();
    ~CConsoleWindow();

    bool Open(const std::wstring &title, CTimer2::StartTickTime &startTickTime);
    void Close();

    bool SetTitle(const std::wstring &title);
    const std::wstring &GetTitle();

    HWND GetWndHandle();

    bool IsVisible();
    void Show(bool bShow = true);

    void ClearScreen();

    WORD GetTextColorIndex(WORD *pwBgColorIndex = NULL);
    void SetTextColor(WORD wTextColorIndex = COLOR_WHITE, WORD wBgColorIndex = COLOR_BLACK);

    void ActivateCloseButton(bool bActive = true);
    bool IsActiveCloseButton() const;

    bool SaveScreenBuffer(const std::wstring &filename);

  protected:
    void SetWndHandle(HWND hWnd);
    DWORD Get32ColorFromColorIndex(WORD wColorIndex);
    static BOOL CALLBACK EnumChildProc(HWND hWnd, LPARAM lParam);
};
} // namespace leaf

#endif // _WINDOWSCONSOLE_H_

enum MSG_TYPE
{
    MCD_SEND = 0x01,
    MCD_RECEIVE,
    MCD_ERROR,
    MCD_NORMAL
};

class CmuConsoleDebug final : protected ApplicationLegacyCalls
{
  public:
    virtual ~CmuConsoleDebug() = default;
    void Bind(class ApplicationDiagnostics &diagnostics) noexcept
    {
        diagnostics_ = &diagnostics;
    }
    void Unbind(class ApplicationDiagnostics &diagnostics) noexcept
    {
        if (diagnostics_ == &diagnostics)
        {
            diagnostics_ = nullptr;
        }
    }
    void Initialize();

    void UpdateMainScene();
    bool CheckCommand(const std::wstring &strCommand);
    void Write(int iType, const wchar_t *pStr, ...);

  private:
    friend struct ApplicationDiagnosticsStorage;

    bool CheckFrameTimingCommand(const std::wstring &command);

    explicit CmuConsoleDebug(ApplicationKeeper &keeper) noexcept
        : ApplicationLegacyCalls(keeper), m_bInit(false)
    {
    }

    bool m_bInit;
    class ApplicationDiagnostics *diagnostics_ = nullptr;
};

struct ApplicationDiagnosticsStorage final
{
    explicit ApplicationDiagnosticsStorage(ApplicationKeeper &keeper) noexcept
        : consoleDebug(keeper)
    {
    }

    CErrorReport g_ErrorReport;
    CmuConsoleDebug consoleDebug;
    leaf::CConsoleWindow consoleWindow;
    CpuUsage cpuUsage;
    wchar_t m_ExeVersion[11]{};
    double CPU_AVG = 0.0;
    double GPU_AVG = 0.0;
    std::size_t appRingOwnedBytes = 0;
    std::size_t processMemoryBytes = 0;
    std::mutex cpuAverageMutex;
    std::atomic<bool> stopCpuWorker{false};
    int g_MaxMessagePerCycle = -1;
};

enum class HotspotProfilerState : std::uint8_t
{
    Idle,
    Recording,
    Stopping,
    Saved,
    Failed,
};

class ApplicationFrameUnit;
class ApplicationKeeper;
struct SDL_Process;

class ApplicationDiagnostics final : protected ApplicationLegacyCalls
{
  public:
    ApplicationDiagnostics(ApplicationKeeper &keeper, ApplicationFrameUnit &frame) noexcept;
    ~ApplicationDiagnostics();

    ApplicationDiagnostics(const ApplicationDiagnostics &) = delete;
    ApplicationDiagnostics &operator=(const ApplicationDiagnostics &) = delete;

    void StartCpuWorker();
    void InitializeConsole();
    void StopCpuWorker() noexcept;
    bool IsCpuWorkerRunning() const noexcept;
    double CpuAverage() const noexcept;
    double GpuUsagePercent() const noexcept;
    std::size_t ProcessMemoryBytes() const noexcept;
    int MessageLimit() const noexcept;
    void SetMaxMessagePerCycle(int messages) noexcept;
    wchar_t (&ExecutableVersion() noexcept)[11];
    CErrorReport &ErrorReport() noexcept;
    CmuConsoleDebug &ConsoleDebug() noexcept;
    ApplicationFrameUnit &Frame() noexcept;
    bool OpenConsoleWindow(const std::wstring &title);
    void CloseConsoleWindow();
    bool SetConsoleTitleW(const std::wstring &title);
    const std::wstring &GetConsoleTitleW();
    HWND GetConsoleWndHandle();
    bool IsConsoleVisible();
    void ShowConsole(bool show = true);
    void ClearConsoleScreen();
    WORD GetConsoleTextColorIndex(WORD *background = nullptr);
    void SetConsoleTextColor(WORD text = leaf::COLOR_WHITE, WORD background = leaf::COLOR_BLACK);
    void ActivateCloseButton(bool active = true);
    bool IsActiveCloseButton();
    bool SaveConsoleScreenBuffer(const std::wstring &filename);
    void ToggleHotspotProfiler();
    void PollHotspotProfiler() noexcept;
    HotspotProfilerState HotspotProfileState() const noexcept;

    bool ExceptionCallback(_EXCEPTION_POINTERS *exceptionInfo) noexcept;

  private:
    friend class ApplicationLegacyCalls;
    friend class ApplicationKeeperTestPeer;

    void RecordCpuUsage();
    bool StartHotspotProfiler();
    void StopHotspotProfiler();
    void ReleaseHotspotProfiler() noexcept;
    void UpdateHotspotMemoryOwners() noexcept;
    void CaptureRenderingProfile() noexcept;
    ApplicationFrameUnit &frame_;
    BOOL &g_bUseWindowMode;
    BOOL &g_bUseFullscreenMode;
    CErrorReport &g_ErrorReport;
    CmuConsoleDebug &consoleDebug_;
    leaf::CConsoleWindow &consoleWindow_;
    CpuUsage &cpuUsage_;
    wchar_t (&m_ExeVersion)[11];
    double &CPU_AVG;
    double &GPU_AVG;
    std::size_t &processMemoryBytes_;
    std::mutex &cpuAverageMutex_;
    std::atomic<bool> &stopCpuWorker_;
    int &g_MaxMessagePerCycle;
    std::thread cpuUsageWorker_;
#if defined(_DEBUG) && defined(_WIN32)
    SDL_Process *hotspotProfilerProcess_ = nullptr;
    std::filesystem::path hotspotProfilerOutputDirectory_;
    std::filesystem::path hotspotProfilerStopFile_;
    std::filesystem::path renderingProfilePrefix_;
    std::chrono::steady_clock::time_point renderingProfileCaptureAt_;
#endif
    std::atomic<HotspotProfilerState> hotspotProfilerState_{HotspotProfilerState::Idle};
};

// Tiny per-frame timing utility used by the $details overlay to break a frame
// down into a fixed list of named passes. Header-only and lock-free; intended
// for ad-hoc bottleneck hunting on a single thread (the render thread).
// Usage:
//   { FRAME_PROFILE(Terrain); RenderTerrain(false); }
// Then `FrameProfiler::AccumulatorMs(Pass::Terrain)` returns the elapsed ms.
// Call `FrameProfiler::ResetFrame()` once per frame after reading the values.

namespace FrameProfiler
{
// Stable, indexed pass list. Add a slot to extend; keep Count_ last.
enum class Pass : int
{
    Terrain,
    Objects,
    Characters,
    Items,
    Effects,
    Other,
    Count_
};

inline constexpr const char *kPassNames[(int)Pass::Count_] = {"Terrain", "Objects", "Chars",
                                                              "Items",   "Effects", "Other"};

// RAII timer. Constructor stamps the start, destructor accumulates elapsed
// ms into the named pass. Multiple Scopes for the same Pass within a frame
// accumulate (so calling RenderObjects twice per frame sums correctly).
class Scope : protected ApplicationLegacyCalls
{
  public:
    Scope(ApplicationKeeper &keeper, Pass pass) noexcept;
    ~Scope();

    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

  private:
    Pass m_pass;
    std::chrono::steady_clock::time_point m_t0;
};
} // namespace FrameProfiler

#define FRAME_PROFILE(passName)                                                                    \
    FrameProfiler::Scope _frameProf_##__LINE__(applicationKeeper_, FrameProfiler::Pass::passName)

namespace Core::Platform
{
// Human-readable operating system name and version for diagnostic overlays
// and logs, e.g. "Ubuntu 24.04.3 LTS (Linux 6.18)", "Windows 10.0",
// "macOS 14.5", "iOS 17.5", or "Android 14". Resolved once and cached.
std::wstring GetOSVersionString();

// The Linux distribution name, from /etc/os-release PRETTY_NAME, e.g.
// "Ubuntu 24.04.3 LTS". Empty on platforms with no distro concept (Windows,
// macOS, Android) or when it cannot be read. Resolved once and cached.
std::wstring GetOSDistroName();
} // namespace Core::Platform

inline void __TraceF(const TCHAR *, ...)
{
}
