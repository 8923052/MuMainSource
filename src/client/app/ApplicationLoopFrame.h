#pragma once
#include "support/CoreMath.h"

#include "app/Application.h"
#include "app/ApplicationConfigScheduling.h"
#include "session/SessionRuntime.h"

#include <chrono>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace FrameProfiler
{
enum class Pass : int;
}

class AppWindow;
class ApplicationDiagnostics;
class ApplicationFrameUnit;
class ApplicationKeeper;
class ApplicationNetwork;
class ApplicationRenderFrame;
class Compositor;
class CErrorReport;
class CInput;
class CmuConsoleDebug;
class SessionManager;
class SessionWorkspace;
struct SessionInputEvent;
struct SDL_KeyboardEvent;
union SDL_Event;
struct SDL_Window;
namespace SEASON3B
{
class CNewKeyInput;
}

class ApplicationLoopUnit final : protected ApplicationLegacyCalls
{
  public:
    ApplicationLoopUnit(ApplicationKeeper &keeper, SessionWorkspace &workspace, AppWindow &window,
                        ApplicationDiagnostics &diagnostics, ApplicationNetwork &network,
                        SessionManager &sessions, SEASON3B::CNewKeyInput &keyInput, CInput &input,
                        ApplicationFrameUnit &frame, Compositor &compositor) noexcept;
    ~ApplicationLoopUnit();
    bool ToggleFocusedCameraZoomLock();
    void SendCheatDetectionLogouts();
    void CloseSessionConnections();

  private:
    struct RenderPipelineState;
    friend class Application;
    friend class ApplicationLegacyCalls;
    friend class ApplicationSupportCalls;

    static bool WindowsMessageHookThunk(void *userdata, MSG *message);
    bool RecoverRenderer() noexcept;
    bool TryBeginRenderFrame() noexcept;
    static void ReplayReadyFrameCallback(void *context) noexcept;
    void ReplayReadyFrame() noexcept;
    MSG MainLoop();
    void DestroyWindow();
    LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam); // OMF-00014
    bool Win32MessageHook(void *userdata, MSG *message);                               // OMF-00020
    void HandleMouseMotion(float windowX, float windowY);                              // OMF-00021
    void FeedPortableTextInput(const char *utf8);                                      // OMF-00028
    bool FeedPortableKey(const SDL_KeyboardEvent &key);                                // OMF-00029
    std::optional<SessionInputEvent> TranslateSdlInputEvent(const SDL_Event &event,
                                                            std::uint64_t sequence);
    void HandleMouseButton(const SDL_Event &event);
    void CheckHack();
    bool IsKeyDown(int virtualKey) const; // OMF-00146
    bool GetLogicalMousePosition(int &x, int &y) const;
    bool PressKey(int key); // OMF-00437
    void ApplyLegacySessionInput(SessionId sessionId, const SessionInputEvent &event);
    bool HandleDebugShortcut(const SDL_Event &event);

    SessionWorkspace &sessionWorkspace_;
    AppWindow &appWindow_;
    ApplicationDiagnostics &applicationDiagnostics_;
    ApplicationNetwork &applicationNetwork_;
    SessionManager &sessionManager_;
    SEASON3B::CNewKeyInput &keyInput_;
    CInput &input_;
    ApplicationFrameUnit &applicationFrame_;
    Compositor &compositor_;
    std::bitset<SessionShortcutScancodeCount> pressedShortcutScancodes_;
    // Frame eligible for owner-thread submission during worker preparation.
    std::unique_ptr<ApplicationRenderFrame> readyRenderFrame_;
    // Allocated only when RenderPipeline=1. Disabled mode retains no extra frame.
    std::unique_ptr<RenderPipelineState> renderPipelineState_;
    SDL_Window *&g_sdlWindow;
    HWND &g_hWnd;
    BOOL &g_bUseWindowMode;
    int &g_iNoMouseTime;
    int &rawMouseX_;
    int &rawMouseY_;
    bool &g_bWndActive;
    bool &g_HasInactiveFpsOverride;
    CErrorReport &g_ErrorReport;
    CmuConsoleDebug &g_ConsoleDebug;
};

class FrameTimingState final
{
  private:
    double targetFps = -1.0;
    double msPerFrame = 0.0;

  public:
    void SetTargetFps(double fps) noexcept
    {
        if (fps <= 0.0)
        {
            targetFps = -1.0;
            msPerFrame = 0.0;
        }
        else if (fps > 0.0)
        {
            targetFps = fps;
            msPerFrame = 1000.0 / fps;
        }
    }

    double GetTargetFps() const noexcept
    {
        return targetFps;
    }
    double GetMsPerFrame() const noexcept
    {
        return msPerFrame;
    }

    bool ShouldRenderNextFrame() const noexcept
    {
        return msPerFrame <= 0.0 || currentTickCount - lastRenderTickCount >= msPerFrame;
    }

    void UpdateCurrentTime(double time) noexcept
    {
        currentTickCount = time;
    }
    void MarkFrameRendered() noexcept
    {
        lastRenderTickCount = currentTickCount;
    }

    double GetCurrentFrameTime() const noexcept
    {
        return currentTickCount - lastRenderTickCount;
    }

    double lastRenderTickCount = 0.0;
    double currentTickCount = 0.0;
};

struct ApplicationFrameStorage final
{
    static constexpr std::size_t FrameHistorySize = 300;
    static constexpr std::size_t ProfilerPassCount = 6;

    ApplicationFrameStorage() : g_pTimer(new CTimer()), g_WorldTime(new CTimer())
    {
    }

    ~ApplicationFrameStorage()
    {
        delete g_pTimer;
        delete g_WorldTime;
    }

    ApplicationFrameStorage(const ApplicationFrameStorage &) = delete;
    ApplicationFrameStorage &operator=(const ApplicationFrameStorage &) = delete;

    CTimer *g_pTimer;
    CTimer *g_WorldTime;
    FrameTimingState g_frameTiming;
    double FPS = 0.0;
    double FPS_AVG = 0.0;
    float FPS_ANIMATION_FACTOR = 0.0f;
    double WorldTime = 0.0;
    int timeinit = 0;
    double start = 0.0;
    double last = 0.0;
    int frame = 0;
    bool framePresented = false;
#ifdef _DEBUG
    bool g_bShowDebugInfo = true;
#else
    bool g_bShowDebugInfo = false;
#endif
    bool g_bShowFpsCounter = false;
    float s_frameTimesMs[FrameHistorySize]{};
    int s_frameIndex = 0;
    int s_frameCount = 0;
    double s_lastFrameTime = 0.0;
    double s_highestFps = 0.0;
    float s_avgFps = 0.0f;
    float s_onePercentLow = 0.0f;
    float s_slowestFrameFps = 0.0f;
    double s_lastStatsUpdate = 0.0;
    float profilerAccumulatorMs[ProfilerPassCount]{};
    bool g_bRenderBoundingBox = false;
    std::chrono::steady_clock::time_point timer2StartTickTime;
};

namespace Core::Time
{
struct BounceResult;
}

namespace Core::Time
{

double ReferenceFrames(double milliseconds, int referenceFps) noexcept;
// Join float-rounded representations of the same interval boundary.
float ReferenceStep(float frames, float &intervalRemaining) noexcept;

// Preserve the original per-frame blend over any elapsed number of 25 FPS frames.
float DampedDistance(float damping, float referenceFrames) noexcept;
float Blend(float referenceBlend, float referenceFrames) noexcept;

// Extend the original position-then-velocity step without changing its 25 FPS trajectory.
void Advance(float &position, float &velocity, float acceleration, float referenceFrames) noexcept;
// Position first, then velocity = velocity * damping + acceleration each reference tick.
void AdvanceDamped(float &position, float &velocity, float damping, float acceleration,
                   float referenceFrames) noexcept;
// Constant floor, positive gravity, restitution in (0, 1). Includes impacts,
// settled time, and integrated horizontal damping. Uses Advance's velocity units.
BounceResult AdvanceBouncing(float &position, float &velocity, float floor, float gravity,
                             float restitution, float referenceFrames) noexcept;
int RepeatCount(float &heldFrames, float delay, float referenceFrames) noexcept;
// First authored whole-frame sample at or below a remaining-age clock.
float ReferenceSample(float remaining) noexcept;
bool Reaches(float remaining, float referenceFrames, float threshold) noexcept;
int Periods(float remaining, float referenceFrames, float interval, float phase = 0.f) noexcept;
} // namespace Core::Time

class AppWindow;
class ApplicationKeeper;

class ApplicationFrameUnit final : protected ApplicationLegacyCalls
{
  public:
    ApplicationFrameUnit(ApplicationKeeper &keeper, AppWindow &window) noexcept;
    ~ApplicationFrameUnit();

    ApplicationFrameUnit(const ApplicationFrameUnit &) = delete;
    ApplicationFrameUnit &operator=(const ApplicationFrameUnit &) = delete;

    static constexpr double LegacyReferenceFrameMilliseconds =
        Core::Time::ReferenceFrameMilliseconds;

    static float CalculateAnimationFactor(
        double frameDeltaMilliseconds, int referenceFps = Core::Time::DefaultReferenceFps) noexcept;

    bool ShouldRenderNextFrame() noexcept;
    void WaitForNextActivity(bool usePreciseSleep) const;
    void MarkFrameRendered() noexcept;
    double AdvanceClock() noexcept;
    void UpdateFrameStats() noexcept;
    void RecordPresentedFrame() noexcept;
    void ResetFrameStats() noexcept;
    void ResetFrame() noexcept;
    void SetTargetFps(double targetFps) noexcept;
    void UseUnlimitedTargetFps() noexcept;
    double GetTargetFps() const noexcept;
    void SetShowDebugInfo(bool enabled) noexcept;
    void SetShowFpsCounter(bool enabled) noexcept;
    void SetRenderBoundingBox(bool enabled) noexcept;
    float &AccumulatorMs(FrameProfiler::Pass pass) noexcept;
    std::chrono::steady_clock::time_point &Timer2StartTickTime() noexcept;
    float &AnimationFactor() noexcept;
    double &WorldTimeRef() noexcept;
    void Shutdown() noexcept;

  private:
    friend class ApplicationLegacyCalls;
    friend class ApplicationKeeperTestPeer;

    int GetFPSLimit() const noexcept;
    void RefreshFrameAverages(double now) noexcept;

    AppWindow &window_;
    CTimer *&g_pTimer;
    CTimer *&g_WorldTime;
    FrameTimingState &g_frameTiming;
    bool &framePresented_;
    double &FPS;
    double &FPS_AVG;
    float &FPS_ANIMATION_FACTOR;
    double &WorldTime;
    int &timeinit;
    double &start;
    double &last;
    int &frame;
    bool &g_bShowDebugInfo;
    bool &g_bShowFpsCounter;
    float (&s_frameTimesMs)[ApplicationFrameStorage::FrameHistorySize];
    int &s_frameIndex;
    int &s_frameCount;
    double &s_lastFrameTime;
    double &s_highestFps;
    float &s_avgFps;
    float &s_onePercentLow;
    float &s_slowestFrameFps;
    double &s_lastStatsUpdate;
    float (&profilerAccumulatorMs)[ApplicationFrameStorage::ProfilerPassCount];
    bool &g_bRenderBoundingBox;
    std::chrono::steady_clock::time_point &timer2StartTickTime;
};

// Portable key and logical-mouse behavior is owned by ApplicationLoopUnit.

namespace Core::Time
{
// Portable replacement for Win32 SetTimer/WM_TIMER. Subsystems register a
// repeating timer by id; the main loop calls Tick() once per frame, which
// fires every timer whose interval has elapsed.
// SetRepeating with an id that already exists replaces it (matching
// SetTimer); Kill removes it (matching KillTimer). Timers fire on the main
// thread from the main loop, exactly like the old WM_TIMER dispatch through
// WndProc. The id space is shared, as it was with SetTimer(g_hWnd, id, ...).
class FrameTimerScheduler
{
  public:
    using TimerId = std::uintptr_t;
    using Callback = std::function<void()>;

    FrameTimerScheduler() = default;

    // Register or replace a repeating timer firing every intervalMs.
    void SetRepeating(TimerId id, unsigned intervalMs, Callback callback);
    void SetRepeating(const void *owner, TimerId id, unsigned intervalMs, Callback callback);

    // Remove a timer. No-op if the id is not registered.
    void Kill(TimerId id);
    void Kill(const void *owner, TimerId id);

    // Fire all timers whose interval has elapsed. Call once per frame.
    void Tick();

  private:
    struct Timer
    {
        unsigned intervalMs;
        std::uint64_t nextDueMs;
        Callback callback;
    };

    struct TimerKey
    {
        const void *owner;
        TimerId id;

        bool operator==(const TimerKey &) const = default;
    };

    struct TimerKeyHash
    {
        std::size_t operator()(const TimerKey &key) const noexcept;
    };

    static std::uint64_t NowMs();

    std::unordered_map<TimerKey, Timer, TimerKeyHash> m_timers;
};
} // namespace Core::Time

extern void DestroyWindow();
