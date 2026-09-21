#include "app/ApplicationLoopFrame.h"
#include "app/Application.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationNetwork.h"
#include "app/AppWindow.h"
#include "app/platform/resource.h"
#include "app/SdlGpuRenderBackend.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "data/WorldData.h"
#include "domain/Automation.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/ChatSocial.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Quests.h"
#include "domain/Shop.h"
#include "domain/WorldSimulation.h"
#include "I18N/All.h"
#include "render/FrameTape.h"
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
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Hud/HudLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/World/WorldLogic.h" // rozy
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"

#ifdef _WIN32
#include <dpapi.h>
#include <shellapi.h>
#include <io.h>
#include <tlhelp32.h>
#endif

struct ApplicationLoopUnit::RenderPipelineState final
{
    std::unique_ptr<ApplicationRenderFrame> preparedFrame;
};

ApplicationLoopUnit::ApplicationLoopUnit(ApplicationKeeper &keeper, SessionWorkspace &workspace,
                                         AppWindow &window, ApplicationDiagnostics &diagnostics,
                                         ApplicationNetwork &network, SessionManager &sessions,
                                         SEASON3B::CNewKeyInput &keyInput, CInput &input,
                                         ApplicationFrameUnit &frame,
                                         Compositor &compositor) noexcept
    : ApplicationLegacyCalls(keeper), sessionWorkspace_(workspace), appWindow_(window),
      applicationDiagnostics_(diagnostics), applicationNetwork_(network), sessionManager_(sessions),
      keyInput_(keyInput), input_(input), applicationFrame_(frame), compositor_(compositor),
      g_sdlWindow(keeper.PlatformStorageRef().g_sdlWindow),
      g_hWnd(keeper.PlatformStorageRef().g_hWnd),
      g_bUseWindowMode(keeper.PlatformStorageRef().g_bUseWindowMode),
      g_iNoMouseTime(keeper.PlatformStorageRef().g_iNoMouseTime),
      rawMouseX_(keeper.LegacyRuntimeStorageRef().rawMouseX),
      rawMouseY_(keeper.LegacyRuntimeStorageRef().rawMouseY),
      g_bWndActive(keeper.PlatformStorageRef().g_bWndActive),
      g_HasInactiveFpsOverride(keeper.PlatformStorageRef().g_HasInactiveFpsOverride),
      g_ErrorReport(diagnostics.ErrorReport()), g_ConsoleDebug(diagnostics.ConsoleDebug())
{
    (void)applicationKeeper_.RegisterApplicationLoop(*this);
}

ApplicationLoopUnit::~ApplicationLoopUnit() = default;

bool ApplicationLoopUnit::ToggleFocusedCameraZoomLock()
{
    const std::optional<SessionId> focused = sessionWorkspace_.FocusedSession();
    return focused.has_value() && sessionManager_.ToggleCameraZoomLock(*focused);
}

void ApplicationLoopUnit::SendCheatDetectionLogouts()
{
    sessionManager_.SendCheatDetectionLogouts();
}

void ApplicationLoopUnit::CloseSessionConnections()
{
    sessionManager_.DeleteSockets();
}

#ifdef _DEBUG
extern "C"
{
    __declspec(dllexport) const float *MuHotspotFrameTimes = nullptr;
    __declspec(dllexport) const int *MuHotspotFrameIndex = nullptr;
    extern __declspec(dllexport) const std::uint32_t MuHotspotFrameCapacity =
        static_cast<std::uint32_t>(ApplicationFrameStorage::FrameHistorySize);
}
#endif

namespace
{
constexpr double MillisecondsInSecond = 1000.0;
constexpr double MinimumFrameTimeMs = 0.5;
constexpr double StatsUpdateIntervalMs = 500.0;
constexpr int MinimumFramesForStats = 10;
constexpr double FpsAverageWindowMs = 2000.0;
constexpr int FpsAverageFrameCount = 25;
} // namespace

ApplicationFrameUnit::ApplicationFrameUnit(ApplicationKeeper &keeper, AppWindow &window) noexcept
    : ApplicationLegacyCalls(keeper), window_(window), g_pTimer(keeper.FrameStorageRef().g_pTimer),
      g_WorldTime(keeper.FrameStorageRef().g_WorldTime),
      g_frameTiming(keeper.FrameStorageRef().g_frameTiming),
      framePresented_(keeper.FrameStorageRef().framePresented), FPS(keeper.FrameStorageRef().FPS),
      FPS_AVG(keeper.FrameStorageRef().FPS_AVG),
      FPS_ANIMATION_FACTOR(keeper.FrameStorageRef().FPS_ANIMATION_FACTOR),
      WorldTime(keeper.FrameStorageRef().WorldTime), timeinit(keeper.FrameStorageRef().timeinit),
      start(keeper.FrameStorageRef().start), last(keeper.FrameStorageRef().last),
      frame(keeper.FrameStorageRef().frame),
      g_bShowDebugInfo(keeper.FrameStorageRef().g_bShowDebugInfo),
      g_bShowFpsCounter(keeper.FrameStorageRef().g_bShowFpsCounter),
      s_frameTimesMs(keeper.FrameStorageRef().s_frameTimesMs),
      s_frameIndex(keeper.FrameStorageRef().s_frameIndex),
      s_frameCount(keeper.FrameStorageRef().s_frameCount),
      s_lastFrameTime(keeper.FrameStorageRef().s_lastFrameTime),
      s_highestFps(keeper.FrameStorageRef().s_highestFps),
      s_avgFps(keeper.FrameStorageRef().s_avgFps),
      s_onePercentLow(keeper.FrameStorageRef().s_onePercentLow),
      s_slowestFrameFps(keeper.FrameStorageRef().s_slowestFrameFps),
      s_lastStatsUpdate(keeper.FrameStorageRef().s_lastStatsUpdate),
      profilerAccumulatorMs(keeper.FrameStorageRef().profilerAccumulatorMs),
      g_bRenderBoundingBox(keeper.FrameStorageRef().g_bRenderBoundingBox),
      timer2StartTickTime(keeper.FrameStorageRef().timer2StartTickTime)
{
#ifdef _DEBUG
    MuHotspotFrameTimes = s_frameTimesMs;
    MuHotspotFrameIndex = &s_frameIndex;
#endif
    (void)applicationKeeper_.RegisterApplicationFrame(*this);
}

ApplicationFrameUnit::~ApplicationFrameUnit()
{
#ifdef _DEBUG
    MuHotspotFrameTimes = nullptr;
    MuHotspotFrameIndex = nullptr;
#endif
    Shutdown();
}

float ApplicationFrameUnit::CalculateAnimationFactor(double frameDeltaMilliseconds,
                                                     int referenceFps) noexcept
{
    return (std::max)(0.0f, static_cast<float>(
                                Core::Time::ReferenceFrames(frameDeltaMilliseconds, referenceFps)));
}

bool ApplicationFrameUnit::ShouldRenderNextFrame() noexcept
{
    if (g_pTimer == nullptr)
    {
        return false;
    }
    g_frameTiming.UpdateCurrentTime(g_pTimer->GetTimeElapsed());
    return g_frameTiming.ShouldRenderNextFrame();
}

void ApplicationFrameUnit::WaitForNextActivity(bool usePreciseSleep) const
{
    const double currentFrameTimeMs = g_frameTiming.GetCurrentFrameTime();
    const double millisecondsPerFrame = g_frameTiming.GetMsPerFrame();
    if (millisecondsPerFrame <= 0.0 || currentFrameTimeMs <= 0.0 ||
        currentFrameTimeMs >= millisecondsPerFrame)
    {
        std::this_thread::yield();
        return;
    }

    const double sleepThresholdMs = usePreciseSleep ? 4.0 : 16.0;
    const double sleepOffsetMs = usePreciseSleep ? 1.0 : 4.0;
    const double remainingMs = millisecondsPerFrame - currentFrameTimeMs;
    if (remainingMs - sleepOffsetMs <= sleepThresholdMs)
    {
        std::this_thread::yield();
        return;
    }

    constexpr double MaximumSleepMs = 10.0;
    const double sleepMs = (std::min)(remainingMs - sleepOffsetMs, MaximumSleepMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long>(sleepMs)));
}

void ApplicationFrameUnit::MarkFrameRendered() noexcept
{
    g_frameTiming.MarkFrameRendered();
}

double ApplicationFrameUnit::AdvanceClock() noexcept
{
    if (g_WorldTime == nullptr)
    {
        return MinimumFrameTimeMs;
    }
    if (!timeinit)
    {
        start = g_WorldTime->GetTimeElapsed();
        last = start;
        timeinit = 1;
    }

    WorldTime = g_WorldTime->GetTimeElapsed();
    const double differenceMs = WorldTime - last;
    FPS = MillisecondsInSecond / (std::max)(differenceMs, MinimumFrameTimeMs);
    FPS_ANIMATION_FACTOR = CalculateAnimationFactor(
        differenceMs, applicationKeeper_.ApplicationConfig().legacyReferenceFps);

    last = WorldTime;
    return differenceMs;
}

void ApplicationFrameUnit::RecordPresentedFrame() noexcept
{
    // Publish the sample on the next owner tick, before render workers read it.
    framePresented_ = true;
}

void ApplicationFrameUnit::UpdateFrameStats() noexcept
{
    if (!std::exchange(framePresented_, false))
        return;
    const double now = WorldTime;
    ++frame;
    const double sinceStart = now - start;
    if (sinceStart > FpsAverageWindowMs || frame > FpsAverageFrameCount)
    {
        FPS_AVG = (MillisecondsInSecond * frame) / sinceStart;
        start = now;
        frame = 0;
    }
    if (!g_bShowDebugInfo)
    {
        return;
    }

    if (s_lastFrameTime > 0.0)
    {
        const double elapsed = (std::max)(now - s_lastFrameTime, MinimumFrameTimeMs);
        s_frameTimesMs[s_frameIndex] = static_cast<float>(elapsed);
        s_frameIndex =
            (s_frameIndex + 1) % static_cast<int>(ApplicationFrameStorage::FrameHistorySize);
        s_frameCount = (std::min)(s_frameCount + 1,
                                  static_cast<int>(ApplicationFrameStorage::FrameHistorySize));
        s_highestFps = (std::max)(s_highestFps, MillisecondsInSecond / elapsed);
    }
    s_lastFrameTime = now;
    RefreshFrameAverages(now);
}

void ApplicationFrameUnit::RefreshFrameAverages(double now) noexcept
{
    if (now - s_lastStatsUpdate <= StatsUpdateIntervalMs || s_frameCount <= MinimumFramesForStats)
    {
        return;
    }
    s_lastStatsUpdate = now;

    std::array<float, ApplicationFrameStorage::FrameHistorySize> sorted{};
    std::copy_n(s_frameTimesMs, s_frameCount, sorted.begin());
    std::sort(sorted.begin(), sorted.begin() + s_frameCount, std::greater<float>());
    const float total = std::accumulate(sorted.begin(), sorted.begin() + s_frameCount, 0.0f);
    const float averageMs = total / s_frameCount;
    s_avgFps = averageMs > 0.0f ? static_cast<float>(MillisecondsInSecond) / averageMs : 0.0f;

    const int onePercentCount = (std::max)(1, s_frameCount / 100);
    const float onePercentTotal =
        std::accumulate(sorted.begin(), sorted.begin() + onePercentCount, 0.0f);
    const float onePercentAverageMs = onePercentTotal / onePercentCount;
    s_onePercentLow = onePercentAverageMs > 0.0f
                          ? static_cast<float>(MillisecondsInSecond) / onePercentAverageMs
                          : 0.0f;
    s_slowestFrameFps =
        sorted.front() > 0.0f ? static_cast<float>(MillisecondsInSecond) / sorted.front() : 0.0f;
}

void ApplicationFrameUnit::ResetFrameStats() noexcept
{
    framePresented_ = false;
    frame = 0;
    start = WorldTime;
    FPS_AVG = 0.0;
    std::memset(s_frameTimesMs, 0, sizeof(s_frameTimesMs));
    s_frameIndex = 0;
    s_frameCount = 0;
    s_lastFrameTime = 0.0;
    s_highestFps = 0.0;
    s_avgFps = 0.0f;
    s_onePercentLow = 0.0f;
    s_slowestFrameFps = 0.0f;
    s_lastStatsUpdate = 0.0;
}

void ApplicationFrameUnit::ResetFrame() noexcept
{
    for (int index = 0; index < static_cast<int>(FrameProfiler::Pass::Count_); ++index)
    {
        AccumulatorMs(static_cast<FrameProfiler::Pass>(index)) = 0.0f;
    }
}

void ApplicationFrameUnit::SetTargetFps(double targetFps) noexcept
{
    g_frameTiming.SetTargetFps(targetFps);
}

void ApplicationFrameUnit::UseUnlimitedTargetFps() noexcept
{
    SetTargetFps(-1.0);
}

double ApplicationFrameUnit::GetTargetFps() const noexcept
{
    return g_frameTiming.GetTargetFps();
}

void ApplicationFrameUnit::SetShowDebugInfo(bool enabled) noexcept
{
    g_bShowDebugInfo = enabled;
    if (enabled)
    {
        g_bShowFpsCounter = false;
    }
}

void ApplicationFrameUnit::SetShowFpsCounter(bool enabled) noexcept
{
    g_bShowFpsCounter = enabled;
    if (enabled)
    {
        g_bShowDebugInfo = false;
    }
}

void ApplicationFrameUnit::SetRenderBoundingBox(bool enabled) noexcept
{
    g_bRenderBoundingBox = enabled;
}

float &ApplicationFrameUnit::AccumulatorMs(FrameProfiler::Pass pass) noexcept
{
    return profilerAccumulatorMs[static_cast<std::size_t>(pass)];
}

std::chrono::steady_clock::time_point &ApplicationFrameUnit::Timer2StartTickTime() noexcept
{
    return timer2StartTickTime;
}

float &ApplicationFrameUnit::AnimationFactor() noexcept
{
    return FPS_ANIMATION_FACTOR;
}

double &ApplicationFrameUnit::WorldTimeRef() noexcept
{
    return WorldTime;
}

void ApplicationFrameUnit::Shutdown() noexcept
{
    delete g_pTimer;
    g_pTimer = nullptr;
    delete g_WorldTime;
    g_WorldTime = nullptr;
}

int ApplicationFrameUnit::GetFPSLimit() const noexcept
{
    constexpr int DefaultRefreshRate = 60;
    SDL_Window *window = static_cast<SDL_Window *>(window_.Handle());
    if (window == nullptr)
    {
        return DefaultRefreshRate;
    }

    SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    if (display == 0)
    {
        display = SDL_GetPrimaryDisplay();
    }
    if (display == 0)
    {
        return DefaultRefreshRate;
    }

    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(display);
    return mode != nullptr && mode->refresh_rate > 0.0f
               ? static_cast<int>(mode->refresh_rate + 0.5f)
               : DefaultRefreshRate;
}

int ApplicationLegacyCalls::GetFPSLimit() const
{
    return applicationKeeper_.ApplicationFrameUnitShortcut()->GetFPSLimit();
}
float &ApplicationLegacyCalls::AccumulatorMs(FrameProfiler::Pass pass)
{
    return applicationKeeper_.ApplicationFrameUnitShortcut()->AccumulatorMs(pass);
}
void ApplicationLegacyCalls::ResetFrame()
{
    applicationKeeper_.ApplicationFrameUnitShortcut()->ResetFrame();
}
void ApplicationLegacyCalls::SetShowDebugInfo(bool enabled)
{
    applicationKeeper_.ApplicationFrameUnitShortcut()->SetShowDebugInfo(enabled);
}
void ApplicationLegacyCalls::SetShowFpsCounter(bool enabled)
{
    applicationKeeper_.ApplicationFrameUnitShortcut()->SetShowFpsCounter(enabled);
}
void ApplicationLegacyCalls::ResetFrameStats()
{
    applicationKeeper_.ApplicationFrameUnitShortcut()->ResetFrameStats();
}
void ApplicationLegacyCalls::SetTargetFps(double targetFps)
{
    applicationKeeper_.ApplicationFrameUnitShortcut()->SetTargetFps(targetFps);
}
double ApplicationLegacyCalls::GetTargetFps() const
{
    return applicationKeeper_.ApplicationFrameUnitShortcut()->GetTargetFps();
}
void ApplicationLegacyCalls::WaitForNextActivity(bool usePreciseSleep) const
{
    return applicationKeeper_.ApplicationFrameUnitShortcut()->WaitForNextActivity(usePreciseSleep);
} // OMF-01814
void ApplicationLegacyCalls::UpdateFrameStats() noexcept
{
    return applicationKeeper_.ApplicationFrameUnitShortcut()->UpdateFrameStats();
} // OMF-01819

namespace
{
// Map a Win32 virtual-key code (or ASCII letter/digit) to an SDL
// scancode. Returns SDL_SCANCODE_UNKNOWN for keys we don't translate.
SDL_Scancode VkToScancode(int vk)
{
    // ASCII letters and digits: VK codes equal their ASCII values.
    if (vk >= 'A' && vk <= 'Z')
        return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (vk - 'A'));
    if (vk >= '1' && vk <= '9')
        return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (vk - '1'));
    if (vk == '0')
        return SDL_SCANCODE_0;

    switch (vk)
    {
    case VK_UP:
        return SDL_SCANCODE_UP;
    case VK_DOWN:
        return SDL_SCANCODE_DOWN;
    case VK_LEFT:
        return SDL_SCANCODE_LEFT;
    case VK_RIGHT:
        return SDL_SCANCODE_RIGHT;
    case VK_INSERT:
        return SDL_SCANCODE_INSERT;
    case VK_DELETE:
        return SDL_SCANCODE_DELETE;
    case VK_HOME:
        return SDL_SCANCODE_HOME;
    case VK_END:
        return SDL_SCANCODE_END;
    case VK_PRIOR:
        return SDL_SCANCODE_PAGEUP;
    case VK_NEXT:
        return SDL_SCANCODE_PAGEDOWN;
    case VK_SPACE:
        return SDL_SCANCODE_SPACE;
    case VK_RETURN:
        return SDL_SCANCODE_RETURN;
    case VK_ESCAPE:
        return SDL_SCANCODE_ESCAPE;
    case VK_TAB:
        return SDL_SCANCODE_TAB;
    case VK_OEM_MINUS:
        return SDL_SCANCODE_MINUS;
    case VK_OEM_PLUS:
        return SDL_SCANCODE_EQUALS;
    case VK_OEM_4:
        return SDL_SCANCODE_LEFTBRACKET;
    case VK_OEM_6:
        return SDL_SCANCODE_RIGHTBRACKET;
    case VK_BACK:
        return SDL_SCANCODE_BACKSPACE;
    case VK_LSHIFT:
        return SDL_SCANCODE_LSHIFT;
    case VK_RSHIFT:
        return SDL_SCANCODE_RSHIFT;
    case VK_LCONTROL:
        return SDL_SCANCODE_LCTRL;
    case VK_RCONTROL:
        return SDL_SCANCODE_RCTRL;
    case VK_LMENU:
        return SDL_SCANCODE_LALT;
    case VK_RMENU:
        return SDL_SCANCODE_RALT;
    case VK_F1:
        return SDL_SCANCODE_F1;
    case VK_F2:
        return SDL_SCANCODE_F2;
    case VK_F3:
        return SDL_SCANCODE_F3;
    case VK_F4:
        return SDL_SCANCODE_F4;
    case VK_F5:
        return SDL_SCANCODE_F5;
    case VK_F6:
        return SDL_SCANCODE_F6;
    case VK_F7:
        return SDL_SCANCODE_F7;
    case VK_F8:
        return SDL_SCANCODE_F8;
    case VK_F9:
        return SDL_SCANCODE_F9;
    case VK_F10:
        return SDL_SCANCODE_F10;
    case VK_F11:
        return SDL_SCANCODE_F11;
    case VK_F12:
        return SDL_SCANCODE_F12;
    case VK_SNAPSHOT:
        return SDL_SCANCODE_PRINTSCREEN;
    default:
        return SDL_SCANCODE_UNKNOWN;
    }
}
} // namespace

bool ApplicationLoopUnit::IsKeyDown(int virtualKey) const
{
    // Poll live input from SDL on every platform. The Win32 path used
    // GetAsyncKeyState because the old child EDIT controls stole keyboard
    // focus from the SDL window; they were replaced by the portable text
    // field (#447), so SDL input state is authoritative here too. The main
    // loop pumps SDL_PollEvent each frame, so this state stays current.

    // Mouse buttons and modifiers come from the SDL mouse / mod state.
    switch (virtualKey)
    {
    case VK_LBUTTON:
        return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
    case VK_RBUTTON:
        return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) != 0;
    case VK_MBUTTON:
        return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE)) != 0;
    case VK_SHIFT:
        return (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    case VK_CONTROL:
        return (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
    case VK_MENU:
        return (SDL_GetModState() & SDL_KMOD_ALT) != 0;
    default:
        break;
    }

    const SDL_Scancode sc = VkToScancode(virtualKey);
    if (sc == SDL_SCANCODE_UNKNOWN)
        return false;

    const bool *state = SDL_GetKeyboardState(nullptr);
    return state != nullptr && state[sc];
}

bool ApplicationLoopUnit::GetLogicalMousePosition(int &x, int &y) const
{
    float windowX = 0.0f;
    float windowY = 0.0f;
    SDL_GetMouseState(&windowX, &windowY);

    SDL_Window *window = SDL_GetMouseFocus();
    int width = 0;
    int height = 0;
    if (window == nullptr || !SDL_GetWindowSize(window, &width, &height) || width <= 0 ||
        height <= 0)
    {
        return false;
    }

    x = std::clamp(static_cast<int>(windowX * REFERENCE_WIDTH / width), 0, REFERENCE_WIDTH);
    y = std::clamp(static_cast<int>(windowY * REFERENCE_HEIGHT / height), 0, REFERENCE_HEIGHT);
    return true;
}

bool ApplicationLegacyCalls::IsKeyDown(int virtualKey) const
{
    return applicationKeeper_.ApplicationLoopUnitShortcut()->IsKeyDown(virtualKey);
}

bool ApplicationSupportCalls::GetLogicalMousePosition(int &x, int &y) const
{
    return applicationKeeper_.ApplicationLoopUnitShortcut()->GetLogicalMousePosition(x, y);
}

namespace Core::Time
{
namespace
{
constexpr double TickTolerance = 0.0001;
}
double ReferenceFrames(double milliseconds, int referenceFps) noexcept
{
    constexpr double MillisecondsPerSecond = 1000.0;
    return milliseconds * referenceFps / MillisecondsPerSecond;
}

float ReferenceStep(float frames, float &intervalRemaining) noexcept
{
    const float tolerance = 4.f * std::numeric_limits<float>::epsilon() *
                            (std::max)({1.f, std::abs(frames), std::abs(intervalRemaining)});
    if (std::abs(frames - intervalRemaining) <= tolerance)
        intervalRemaining = frames;
    return (std::min)(frames, intervalRemaining);
}

float DampedDistance(float damping, float referenceFrames) noexcept
{
    if (referenceFrames == 0.f)
        return 0.f;
    if (damping == 1.f)
        return referenceFrames;
    return float(std::expm1(std::log(double(damping)) * referenceFrames) / (double(damping) - 1.0));
}

float Blend(float referenceBlend, float referenceFrames) noexcept
{
    return 1.f - std::pow(1.f - referenceBlend, referenceFrames);
}

void Advance(float &position, float &velocity, float acceleration, float referenceFrames) noexcept
{
    position += referenceFrames * (velocity + acceleration * (referenceFrames - 1.f) * 0.5f);
    velocity += acceleration * referenceFrames;
}

void AdvanceDamped(float &position, float &velocity, float damping, float acceleration,
                   float referenceFrames) noexcept
{
    if (referenceFrames <= 0.f)
        return;
    if (damping == 1.f)
    {
        Advance(position, velocity, acceleration, referenceFrames);
        return;
    }
    const float equilibrium = acceleration / (1.f - damping);
    position += equilibrium * referenceFrames +
                (velocity - equilibrium) * DampedDistance(damping, referenceFrames);
    velocity = equilibrium + (velocity - equilibrium) * std::pow(damping, referenceFrames);
}

BounceResult AdvanceBouncing(float &position, float &velocity, float floor, float gravity,
                             float restitution, float referenceFrames) noexcept
{
    BounceResult result;
    if (referenceFrames <= 0.f)
        return result;
    // Advance stores the pre-acceleration sample; reflect its physical derivative.
    double speed = velocity + gravity * 0.5;
    double height = (std::max)(0.0, double(position) - floor);
    double remaining = referenceFrames;
    const double resolution = std::nextafter(floor, INFINITY) - floor;
    while (remaining > 0.0)
    {
        if (height == 0.0 && speed * speed / (2.0 * gravity) <= resolution)
        {
            speed = 0.0;
            result.damping = 0.f;
            result.restingFrames = float(remaining);
            break;
        }
        const double impactSpeed = std::sqrt(speed * speed + 2.0 * gravity * height);
        const double impactTime = speed < 0.0
                                      ? (height == 0.0 ? 0.0 : 2.0 * height / (impactSpeed - speed))
                                      : (speed + impactSpeed) / gravity;
        if (impactTime > remaining)
        {
            result.dampedFrames += result.damping * float(remaining);
            height += remaining * (speed - gravity * remaining * 0.5);
            speed -= gravity * remaining;
            break;
        }
        result.dampedFrames += result.damping * float(impactTime);
        remaining -= impactTime;
        height = 0.0;
        speed = impactSpeed * restitution;
        result.damping *= restitution;
        ++result.impacts;
        // Count every representable rebound; smaller heights are resting contact.
        if (speed * speed / (2.0 * gravity) <= resolution)
        {
            speed = 0.0;
            result.damping = 0.f;
            result.restingFrames = float(remaining);
            break;
        }
    }
    position = floor + float(height);
    velocity = float(speed - gravity * 0.5);
    return result;
}

int RepeatCount(float &heldFrames, float delay, float referenceFrames) noexcept
{
    const float previous = heldFrames;
    heldFrames += referenceFrames;
    return static_cast<int>(std::floor((std::max)(0.f, heldFrames - delay - 1.f) + TickTolerance) -
                            std::floor((std::max)(0.f, previous - delay - 1.f) + TickTolerance));
}

float ReferenceSample(float remaining) noexcept
{
    return float(std::floor(remaining + TickTolerance));
}

bool Reaches(float remaining, float referenceFrames, float threshold) noexcept
{
    return referenceFrames > 0.f && remaining + TickTolerance >= threshold &&
           remaining - referenceFrames + TickTolerance < threshold;
}

int Periods(float remaining, float referenceFrames, float interval, float phase) noexcept
{
    return static_cast<int>(
        std::floor((remaining - phase + TickTolerance) / interval) -
        std::floor((remaining - referenceFrames - phase + TickTolerance) / interval));
}
} // namespace Core::Time

namespace Core::Time
{
std::uint64_t FrameTimerScheduler::NowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void FrameTimerScheduler::SetRepeating(TimerId id, unsigned intervalMs, Callback callback)
{
    SetRepeating(nullptr, id, intervalMs, std::move(callback));
}

void FrameTimerScheduler::SetRepeating(const void *owner, TimerId id, unsigned intervalMs,
                                       Callback callback)
{
    m_timers[{owner, id}] = Timer{intervalMs, NowMs() + intervalMs, std::move(callback)};
}

void FrameTimerScheduler::Kill(TimerId id)
{
    Kill(nullptr, id);
}

void FrameTimerScheduler::Kill(const void *owner, TimerId id)
{
    m_timers.erase({owner, id});
}

std::size_t FrameTimerScheduler::TimerKeyHash::operator()(const TimerKey &key) const noexcept
{
    const std::size_t ownerHash = std::hash<const void *>{}(key.owner);
    const std::size_t idHash = std::hash<TimerId>{}(key.id);
    return ownerHash ^ (idHash + 0x9e3779b9u + (ownerHash << 6) + (ownerHash >> 2));
}

void FrameTimerScheduler::Tick()
{
    const std::uint64_t now = NowMs();

    // Collect the due ids first, then fire. A callback may register or kill
    // timers (e.g. a buff timer kills itself on expiry), so we must not hold
    // an iterator into m_timers across a callback.
    std::vector<TimerKey> due;
    for (const auto &[key, timer] : m_timers)
    {
        if (now >= timer.nextDueMs)
        {
            due.push_back(key);
        }
    }

    for (const TimerKey &key : due)
    {
        auto it = m_timers.find(key);
        if (it == m_timers.end())
        {
            continue; // killed by an earlier callback this tick
        }

        // Reschedule before firing so a callback that re-registers or kills
        // this id wins over the reschedule. No catch-up: the next due time is
        // measured from now, matching WM_TIMER coalescing.
        it->second.nextDueMs = now + it->second.intervalMs;

        Callback callback = it->second.callback;
        callback();
    }
}
} // namespace Core::Time

namespace
{
std::uint64_t MonotonicMilliseconds() noexcept
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}
} // namespace

void ApplicationLoopUnit::CheckHack()
{
    g_ConsoleDebug.Write(MCD_SEND, L"SendCheck");

    const int dwTick = GetTickCount();
    sessionManager_.SendNetworkPings(dwTick);

    if (!First)
    {
        First = true;
        FirstTime = dwTick;
    }
}

void ApplicationLoopUnit::DestroyWindow()
{
    SAFE_DELETE(SkillAttribute);

    DeleteBitmap(BITMAP_INTERFACE_NEW_NUMBER_BEGIN);
    Bitmaps.UnloadAllImages(applicationDiagnostics_.ErrorReport());

    SAFE_DELETE_ARRAY(ItemAttRibuteMemoryDump);
    SAFE_DELETE_ARRAY(RendomMemoryDump);

    SAFE_DELETE(pMultiLanguage);
    applicationDiagnostics_.ErrorReport().Write(L"Destroy");

    HWND shWnd = FindWindow(nullptr, L"MuPlayer");
    if (shWnd)
        SendMessage(shWnd, WM_DESTROY, 0, 0);
}
// The legacy Win32 message handler. SDL owns the event loop on Linux and only
// bridges to this via the Windows-only message hook, so guard it off there.
#ifdef _WIN32
LRESULT CALLBACK ApplicationLoopUnit::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // F10 zoom-lock toggle. Handled before the ImGui forwarder so editor-open
    // sessions still get the toggle (ImGui captures keyboard messages while a
    // window has focus). Bit 30 of lParam = previous key state — skip
    // auto-repeat ticks so a held key only toggles once.
    constexpr LPARAM PREVIOUS_KEY_STATE_MASK = 1 << 30;
    if (msg == WM_SYSKEYDOWN && wParam == VK_F10 && (lParam & PREVIOUS_KEY_STATE_MASK) == 0)
    {
        return 0;
    }

    // ImGui (editor) now consumes input from the SDL event loop via
    // ImGui_ImplSDL3_ProcessEvent, not from Win32 messages (issue #442).

    switch (msg)
    {
    case WM_SYSKEYDOWN: {
        // F10 is handled above (intercepted before ImGui). Other system keys
        // are silenced here — returning 0 prevents the OS menu activation.
        return 0;
    }
    break;
    // WM_ACTIVATE is handled via SDL window focus events (issue #442).
    case WM_NPROTECT_EXIT_TWO:
        // Inform the user, then close. A frame-ticked timer cannot fire while
        // this modal dialog blocks the main loop, so close right after the
        // dialog is dismissed instead of via a timer.
        MessageBox(nullptr, I18N::Game::Error9AHackingToolHasBeen, L"Error", MB_OK);
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        break;
    case WM_ERASEBKGND:
        return TRUE;
        break;
    // WM_SIZE is handled via SDL window resize events (issue #442).
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hDC = BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
    }
        return 0;
        break;
    case WM_DESTROY: {
        // Ask SDL's application loop to perform the orderly teardown. The
        // native callback must not destroy SDL's context or window itself.
        SDL_Event quitEvent{};
        quitEvent.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quitEvent);
    }
    break;
    case WM_SETCURSOR: {
        ShowCursor(false);
    }
    break;
    default:
        break;
    }

    // Mouse input (move/buttons/wheel) is handled via SDL events (issue #442).
    switch (msg)
    {
    case WM_IME_NOTIFY: {
        if (g_iChatInputType == 1)
        {
            switch (wParam)
            {
            case IMN_SETCONVERSIONMODE:
                if (GetFocus() == hwnd)
                {
                    CheckTextInputBoxIME(IME_CONVERSIONMODE);
                }
                break;
            case IMN_SETSENTENCEMODE:
                if (GetFocus() == hwnd)
                {
                    CheckTextInputBoxIME(IME_SENTENCEMODE);
                }
                break;
            default:
                break;
            }
        }
    }
    break;
    case WM_CHAR: {
        switch (wParam)
        {
        case VK_RETURN: {
            SetEnterPressed(true);
        }
        break;
        }
    }
    break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
#endif // _WIN32 (WndProc)

void MoveObject(OBJECT *o);

#ifdef _WIN32
// Transitional bridge (issue #442): SDL owns the window, but the existing
// WndProc still handles input, IME, the legacy Win32 EDIT-control text boxes,
// the cursor, and shutdown. SDL invokes this for every Win32 message it pumps;
// forward to WndProc and return true so SDL continues its own processing (which
// also dispatches the child EDIT controls). Removed once input/IME are
// SDL-native and the legacy text boxes are replaced (issue #447).
bool ApplicationLoopUnit::Win32MessageHook(void *userdata, MSG *msg)
{
    // Let SDL own window close. Forwarding WM_CLOSE to WndProc would reach
    // DefWindowProc, which destroys the window synchronously and out from under
    // SDL. SDL turns the close into SDL_EVENT_QUIT, which the main loop handles.
    if (msg->message == WM_CLOSE)
        return true;

    auto &loop = *static_cast<ApplicationLoopUnit *>(userdata);
    if (msg->message == WM_NPROTECT_EXIT_TWO)
    {
        loop.SendCheatDetectionLogouts();
    }
    else if (msg->message == WM_DESTROY)
    {
        loop.CloseSessionConnections();
    }

    constexpr LPARAM previousKeyStateMask = 1 << 30;
    if (msg->message == WM_SYSKEYDOWN && msg->wParam == VK_F10 &&
        (msg->lParam & previousKeyStateMask) == 0)
    {
        loop.ToggleFocusedCameraZoomLock();
        return true;
    }

    WndProc(msg->hwnd, msg->message, msg->wParam, msg->lParam);
    return true;
}

bool ApplicationLoopUnit::WindowsMessageHookThunk(void *userdata, MSG *message)
{
    auto &loop = *static_cast<ApplicationLoopUnit *>(userdata);
    return loop.Win32MessageHook(userdata, message);
}
#endif

// SDL event translation (issue #442). Mouse and window events are handled from
// the SDL event loop instead of WndProc, feeding the same global input state.
void ApplicationLoopUnit::HandleMouseMotion(float winX, float winY)
{
    rawMouseX_ = static_cast<int>(winX / g_fScreenRate_x);
    rawMouseY_ = static_cast<int>(winY / g_fScreenRate_y);
    if (rawMouseX_ < 0)
        rawMouseX_ = 0;
    if (rawMouseX_ > REFERENCE_WIDTH)
        rawMouseX_ = REFERENCE_WIDTH;
    if (rawMouseY_ < 0)
        rawMouseY_ = 0;
    if (rawMouseY_ > REFERENCE_HEIGHT)
        rawMouseY_ = REFERENCE_HEIGHT;
}

namespace
{
using PressedShortcutScancodes = std::bitset<SessionShortcutScancodeCount>;
static_assert(static_cast<std::size_t>(SDL_SCANCODE_COUNT) == SessionShortcutScancodeCount);

bool IsShortcutKeyPressed(std::int32_t key, const PressedShortcutScancodes &pressed)
{
    switch (key)
    {
    case SessionShortcutModifierControl:
        return pressed.test(SDL_SCANCODE_LCTRL) || pressed.test(SDL_SCANCODE_RCTRL);
    case SessionShortcutModifierShift:
        return pressed.test(SDL_SCANCODE_LSHIFT) || pressed.test(SDL_SCANCODE_RSHIFT);
    case SessionShortcutModifierAlt:
        return pressed.test(SDL_SCANCODE_LALT) || pressed.test(SDL_SCANCODE_RALT);
    case SessionShortcutModifierMeta:
        return pressed.test(SDL_SCANCODE_LGUI) || pressed.test(SDL_SCANCODE_RGUI);
    default:
        return key > SDL_SCANCODE_UNKNOWN && key < SDL_SCANCODE_COUNT &&
               pressed.test(static_cast<std::size_t>(key));
    }
}

std::size_t PressedShortcutKeyCount(const PressedShortcutScancodes &pressed)
{
    std::size_t count = pressed.count();
    count -= pressed.test(SDL_SCANCODE_LCTRL) && pressed.test(SDL_SCANCODE_RCTRL);
    count -= pressed.test(SDL_SCANCODE_LSHIFT) && pressed.test(SDL_SCANCODE_RSHIFT);
    count -= pressed.test(SDL_SCANCODE_LALT) && pressed.test(SDL_SCANCODE_RALT);
    count -= pressed.test(SDL_SCANCODE_LGUI) && pressed.test(SDL_SCANCODE_RGUI);
    return count;
}

bool MatchesSessionShortcut(const SessionShortcutBinding &binding,
                            const PressedShortcutScancodes &pressed)
{
    return !binding.keys.empty() && binding.keys.size() == PressedShortcutKeyCount(pressed) &&
           std::all_of(binding.keys.begin(), binding.keys.end(),
                       [&pressed](std::int32_t key) { return IsShortcutKeyPressed(key, pressed); });
}

// --- Portable text field input routing (issue #447) -------------------
// Map the SDL keys a single-line text field reacts to onto the Win32 VK
// codes the field already understands. Returns 0 for keys it ignores.
int MapScancodeToEditVk(SDL_Scancode sc)
{
    switch (sc)
    {
    case SDL_SCANCODE_LEFT:
        return VK_LEFT;
    case SDL_SCANCODE_RIGHT:
        return VK_RIGHT;
    case SDL_SCANCODE_HOME:
        return VK_HOME;
    case SDL_SCANCODE_END:
        return VK_END;
    case SDL_SCANCODE_BACKSPACE:
        return VK_BACK;
    case SDL_SCANCODE_DELETE:
        return VK_DELETE;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        return VK_RETURN;
    case SDL_SCANCODE_TAB:
        return VK_TAB;
    default:
        return 0;
    }
}

// UTF-8 <-> UTF-16 conversions sized to the input, so text of any length
// (typed, copied or pasted) round-trips without truncation (issue #447).
std::wstring Utf8ToWide(const char *utf8)
{
    if (utf8 == nullptr)
        return std::wstring();
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (needed <= 1)
        return std::wstring();            // <=1 means empty or error
    std::wstring wide(needed - 1, L'\0'); // needed includes the null terminator
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), needed);
    return wide;
}

std::string WideToUtf8(const std::wstring &wide)
{
    if (wide.empty())
        return std::string();
    const int needed =
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1)
        return std::string(); // <=1 means empty or error
    std::string utf8(needed - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), needed, nullptr, nullptr);
    return utf8;
}

} // namespace

void ApplicationLoopUnit::FeedPortableTextInput(const char *utf8)
{
    const std::optional<SessionId> focused = sessionWorkspace_.FocusedSession();
    if (!focused.has_value() || utf8 == nullptr)
        return;
    CUITextInputBox *box = sessionManager_.FocusedTextInputBox(*focused);
    if (box == nullptr)
        return;

    const std::wstring wide = Utf8ToWide(utf8);
    if (!wide.empty())
        box->OnTextInput(wide.c_str());
}

// Handle a key for the focused portable field. Returns true if consumed.
bool ApplicationLoopUnit::FeedPortableKey(const SDL_KeyboardEvent &key)
{
    const std::optional<SessionId> focused = sessionWorkspace_.FocusedSession();
    if (!focused.has_value())
        return false;
    CUITextInputBox *box = sessionManager_.FocusedTextInputBox(*focused);
    if (box == nullptr)
        return false;

    const bool ctrl = (key.mod & SDL_KMOD_CTRL) != 0;
    const bool shift = (key.mod & SDL_KMOD_SHIFT) != 0;
    const auto scancode = key.scancode;

    // Clipboard lives in SDL on this side of the boundary, keeping the text
    // field itself free of SDL; the field only exposes selection helpers.
    if (ctrl)
    {
        switch (scancode)
        {
        case SDL_SCANCODE_A:
            box->SelectAll();
            return true;
        case SDL_SCANCODE_C:
        case SDL_SCANCODE_X: {
            const std::wstring selection = box->GetSelectedText();
            if (!selection.empty())
            {
                const std::string utf8 = WideToUtf8(selection);
                if (!utf8.empty())
                {
                    SDL_SetClipboardText(utf8.c_str());
                    if (scancode == SDL_SCANCODE_X)
                        box->DeleteSelection();
                }
            }
            return true;
        }
        case SDL_SCANCODE_V: {
            char *clip = SDL_GetClipboardText();
            if (clip != nullptr)
            {
                const std::wstring wide = Utf8ToWide(clip);
                if (!wide.empty())
                    box->OnTextInput(wide.c_str());
                SDL_free(clip);
            }
            return true;
        }
        default:
            break;
        }
    }

    const int vk = MapScancodeToEditVk(scancode);
    if (vk == 0)
        return false;

    box->OnEditKey(vk, ctrl, shift);
    return true;
}

std::optional<SessionInputEvent> ApplicationLoopUnit::TranslateSdlInputEvent(const SDL_Event &event,
                                                                             std::uint64_t sequence)
{
    SessionInputEvent translated{SessionInputEventKind::Application, sequence};
    switch (event.type)
    {
    case SDL_EVENT_QUIT:
        translated.action = SessionInputAction::Quit;
        return translated;
    case SDL_EVENT_MOUSE_MOTION:
        translated.kind = SessionInputEventKind::Pointer;
        translated.action = SessionInputAction::PointerMove;
        translated.x = static_cast<std::int32_t>(event.motion.x);
        translated.y = static_cast<std::int32_t>(event.motion.y);
        return translated;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        translated.kind = SessionInputEventKind::Pointer;
        translated.action = SessionInputAction::PointerButton;
        translated.x = static_cast<std::int32_t>(event.button.x);
        translated.y = static_cast<std::int32_t>(event.button.y);
        translated.code = event.button.button;
        translated.pressed = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        translated.clicks = event.button.clicks;
        return translated;
    case SDL_EVENT_MOUSE_WHEEL:
        translated.kind = SessionInputEventKind::Pointer;
        translated.action = SessionInputAction::PointerWheel;
        translated.x = static_cast<std::int32_t>(event.wheel.mouse_x);
        translated.y = static_cast<std::int32_t>(event.wheel.mouse_y);
        translated.wheel =
            event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
        return translated;
    case SDL_EVENT_WINDOW_RESIZED:
        translated.kind = SessionInputEventKind::Window;
        translated.action = SessionInputAction::WindowResized;
        translated.width = static_cast<std::uint32_t>(event.window.data1);
        translated.height = static_cast<std::uint32_t>(event.window.data2);
        return translated;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        translated.kind = SessionInputEventKind::Window;
        translated.action = SessionInputAction::WindowFocusGained;
        return translated;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        pressedShortcutScancodes_.reset();
        translated.kind = SessionInputEventKind::Window;
        translated.action = SessionInputAction::WindowFocusLost;
        return translated;
    case SDL_EVENT_TEXT_INPUT:
        translated.kind = SessionInputEventKind::Text;
        translated.action = SessionInputAction::TextInput;
        std::strncpy(translated.text.data(), event.text.text, translated.text.size() - 1);
        return translated;
    case SDL_EVENT_TEXT_EDITING:
        translated.kind = SessionInputEventKind::Text;
        translated.action = SessionInputAction::TextEditing;
        std::strncpy(translated.text.data(), event.edit.text, translated.text.size() - 1);
        return translated;
    case SDL_EVENT_KEY_DOWN: {
        if (event.key.scancode > SDL_SCANCODE_UNKNOWN && event.key.scancode < SDL_SCANCODE_COUNT)
        {
            pressedShortcutScancodes_.set(static_cast<std::size_t>(event.key.scancode));
        }
        if (!event.key.repeat)
        {
            const SessionShortcutSettings &shortcuts =
                applicationKeeper_.SessionConfigStoreObject().Shortcuts();
            if (MatchesSessionShortcut(shortcuts.toggleControlBar, pressedShortcutScancodes_))
            {
                translated.action = SessionInputAction::ApplicationShortcut;
                translated.code =
                    static_cast<std::int32_t>(ApplicationShortcutCode::ToggleControlBar);
                return translated;
            }
            for (std::size_t index = 0; index < shortcuts.slotViews.size(); ++index)
            {
                if (MatchesSessionShortcut(shortcuts.slotViews[index], pressedShortcutScancodes_))
                {
                    translated.action = SessionInputAction::ApplicationShortcut;
                    translated.code = static_cast<std::int32_t>(
                                          ApplicationShortcutCode::SelectPickerRow1) +
                                      static_cast<std::int32_t>(index);
                    return translated;
                }
            }
        }
        translated.kind = SessionInputEventKind::Key;
        translated.action = SessionInputAction::KeyDown;
        translated.code = event.key.scancode;
        translated.modifiers = event.key.mod;
        translated.repeat = event.key.repeat;
        return translated;
    }
    case SDL_EVENT_KEY_UP:
        if (event.key.scancode > SDL_SCANCODE_UNKNOWN && event.key.scancode < SDL_SCANCODE_COUNT)
        {
            pressedShortcutScancodes_.reset(static_cast<std::size_t>(event.key.scancode));
        }
        translated.kind = SessionInputEventKind::Key;
        translated.action = SessionInputAction::KeyUp;
        translated.code = event.key.scancode;
        translated.modifiers = event.key.mod;
        return translated;
    default:
        return std::nullopt;
    }
}

void ApplicationLoopUnit::HandleMouseButton(const SDL_Event &event)
{
    const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    g_iNoMouseTime = 0;
    switch (event.button.button)
    {
    case SDL_BUTTON_LEFT:
        input_.ApplyPointerButton(VK_LBUTTON, down, event.button.clicks >= 2);
        if (down)
        {
            SetCapture(g_hWnd);
        }
        else
        {
            ReleaseCapture();
        }
        break;
    case SDL_BUTTON_RIGHT:
        input_.ApplyPointerButton(VK_RBUTTON, down, event.button.clicks >= 2);
        if (down)
        {
            SetCapture(g_hWnd);
        }
        else
        {
            ReleaseCapture();
        }
        break;
    case SDL_BUTTON_MIDDLE:
        input_.ApplyPointerButton(VK_MBUTTON, down, event.button.clicks >= 2);
        if (down)
        {
            SetCapture(g_hWnd);
        }
        else
        {
            ReleaseCapture();
        }
        break;
    }
}

void ApplicationLoopUnit::ApplyLegacySessionInput(SessionId sessionId,
                                                  const SessionInputEvent &event)
{
    switch (event.action)
    {
    case SessionInputAction::PointerMove:
        sessionManager_.ApplyPointerMove(sessionId, event.x, event.y);
        break;
    case SessionInputAction::PointerButton:
        sessionManager_.ApplyPointerMove(sessionId, event.x, event.y);
        sessionManager_.ApplyPointerButton(sessionId, event.code, event.pressed, event.clicks);
        break;
    case SessionInputAction::PointerWheel:
        sessionManager_.ApplyPointerMove(sessionId, event.x, event.y);
        sessionManager_.SetMouseWheel(sessionId, static_cast<int>(event.wheel));
        break;
    case SessionInputAction::WindowResized:
        HandleWindowResize(static_cast<int>(event.width), static_cast<int>(event.height));
        break;
    case SessionInputAction::WindowFocusGained:
        HandleFocusChange(true);
        break;
    case SessionInputAction::WindowFocusLost:
        sessionManager_.SetMouseWheel(sessionId, 0);
        sessionManager_.ResetMouseButtons();
        HandleFocusChange(false);
        break;
    case SessionInputAction::TextInput:
        break;
    case SessionInputAction::TextEditing:
        if (auto *box = sessionManager_.FocusedTextInputBox(sessionId))
        {
            box->OnTextEditing(Utf8ToWide(event.text.data()).c_str());
        }
        break;
    case SessionInputAction::KeyDown:
        if (!event.repeat && event.code == SDL_SCANCODE_F9)
        {
            sessionManager_.CycleCameraMode(sessionId);
        }
        if (!event.repeat && event.code == SDL_SCANCODE_F11)
        {
            sessionManager_.ResetCameraView(sessionId);
        }
#ifndef _WIN32
        if (event.code == SDL_SCANCODE_RETURN || event.code == SDL_SCANCODE_KP_ENTER)
        {
            SetEnterPressed(true);
        }
        if (event.code == SDL_SCANCODE_F10 && !event.repeat)
        {
            sessionManager_.ToggleCameraZoomLock(sessionId);
        }
#endif
        break;
    default:
        break;
    }
}

bool ApplicationLoopUnit::HandleDebugShortcut(const SDL_Event &event)
{
#if defined(_DEBUG) && defined(_WIN32)
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        event.key.scancode == SDL_SCANCODE_F12)
    {
        applicationDiagnostics_.ToggleHotspotProfiler();
        return true;
    }
#else
    (void)event;
#endif
    return false;
}

// Resolution change through SDL (issue #462). SDL owns the window on every
// platform, so resize it via SDL rather than the OS. The old Windows path in
// ApplyResolution() drove Win32 SetWindowPos/ChangeDisplaySettings on g_hWnd,
// which fought SDL: it pins the min/max tracking size of a non-resizable
// window, so a raw SetWindowPos was clamped back and the resolution never
// changed unless a windowed/fullscreen toggle reset the style first.
// SDL_SetWindowSize resizes regardless of the resizable flag and drives the
// same HandleWindowResize update synchronously, so callers can Save() config
// right after and see the size the window actually ended up with.

bool ApplicationLoopUnit::RecoverRenderer() noexcept
{
    readyRenderFrame_.reset();
    if (renderPipelineState_ != nullptr)
    {
        renderPipelineState_->preparedFrame.reset();
    }
    if (appWindow_.EnsureCreated() && compositor_.Recover(appWindow_))
    {
        return true;
    }
    applicationDiagnostics_.ErrorReport().Write(L"Renderer recovery failed; closing.\r\n");
    appWindow_.RequestClose();
    return false;
}

void ApplicationLoopUnit::ReplayReadyFrameCallback(void *context) noexcept
{
    static_cast<ApplicationLoopUnit *>(context)->ReplayReadyFrame();
}

void ApplicationLoopUnit::ReplayReadyFrame() noexcept
{
    if (readyRenderFrame_ == nullptr)
    {
        return;
    }
    std::unique_ptr<ApplicationRenderFrame> frame = std::move(readyRenderFrame_);
    if (frame->WorkspaceRevision() != sessionWorkspace_.PresentationRevision())
    {
        return;
    }
    const CompositorFrameResult result = compositor_.ReplayAndPresent(*frame);
    if (result == CompositorFrameResult::Submitted)
    {
        applicationFrame_.RecordPresentedFrame();
    }
    else if (result == CompositorFrameResult::Rejected)
    {
        applicationDiagnostics_.ErrorReport().Write(
            L"Renderer rejected frame %llu.\r\n",
            static_cast<unsigned long long>(frame->FrameSequence()));
    }
    else if (result == CompositorFrameResult::DeviceLost)
    {
        (void)RecoverRenderer();
    }
}

bool ApplicationLoopUnit::TryBeginRenderFrame() noexcept
{
    const CompositorRetireResult result = compositor_.RetireCompletedFrame();
    if (result == CompositorRetireResult::Pending)
    {
        // Keep the prepared frame. Recheck after pumping events instead of
        // spending a full worker cycle on another frame we cannot submit.
        std::this_thread::yield();
        return false;
    }
    if (result == CompositorRetireResult::DeviceLost)
    {
        (void)RecoverRenderer();
        return false;
    }
    sessionManager_.CompleteRenderCompletions(compositor_.RenderCompletions(),
                                              applicationKeeper_.BitmapRegistry());
    compositor_.ClearRenderCompletions();
    return true;
}

MSG ApplicationLoopUnit::MainLoop()
{
    SessionInputRoutingView &inputRouting = sessionWorkspace_.InputRouting();
    const bool renderPipelineEnabled = applicationKeeper_.ApplicationConfig().renderPipeline;
    if (renderPipelineEnabled)
    {
        renderPipelineState_ = std::make_unique<RenderPipelineState>();
    }
    constexpr auto target_resolution = 1;
    auto precise = timeBeginPeriod(target_resolution);
    std::uint64_t inputSequence = 0;

    while (!appWindow_.CloseRequested())
    {
        if (auto error = applicationKeeper_.TakeDeferredErrorMessage())
        {
            PopUpErrorCheckMsgBox(error->text.data(), error->forceDestroy);
        }
#if defined(_DEBUG) && defined(_WIN32)
        applicationDiagnostics_.PollHotspotProfiler();
#endif
        SDL_Event event;
        int messageProcessed = 0;

        // Pumping SDL also drives the Win32 message hook (-> WndProc) for the
        // input still on it (IME, the legacy EDIT text boxes); mouse and window
        // events are handled here, off the hook.
        while (SDL_PollEvent(&event))
        {
            (void)HandleDebugShortcut(event);
            if (event.type == SDL_EVENT_MOUSE_MOTION)
            {
                HandleMouseMotion(event.motion.x, event.motion.y);
            }
            const auto inputEvent = TranslateSdlInputEvent(event, ++inputSequence);
            if (inputEvent.has_value())
            {
                const SessionInputRoute route = inputRouting.Route(*inputEvent);
                if (inputEvent->action == SessionInputAction::Quit)
                {
                    if (g_sdlWindow != nullptr)
                    {
                        SDL_HideWindow(g_sdlWindow);
                    }
                    appWindow_.RequestClose();
                }
                else if (route.result == SessionInputRouteResult::Queued)
                {
                    SessionInputView *input = inputRouting.Input(*route.sessionId);
                    if (input != nullptr)
                    {
                        while (const auto queuedEvent = input->TryPop())
                        {
                            const bool modernUiHandled = sessionManager_.ProcessModernUiInput(
                                *route.sessionId, *queuedEvent);
                            const bool modernPointerHandled =
                                queuedEvent->kind == SessionInputEventKind::Pointer &&
                                modernUiHandled;
                            if (modernPointerHandled)
                            {
                                sessionManager_.ApplyPointerMove(*route.sessionId, queuedEvent->x,
                                                                 queuedEvent->y);
                            }
                            else
                            {
                                if (queuedEvent->action == SessionInputAction::PointerButton)
                                {
                                    HandleMouseButton(event);
                                }
                                ApplyLegacySessionInput(*route.sessionId, *queuedEvent);
                            }
                            if (queuedEvent->action == SessionInputAction::TextInput &&
                                !modernUiHandled)
                            {
                                FeedPortableTextInput(queuedEvent->text.data());
                            }
                            else if (queuedEvent->action == SessionInputAction::KeyDown &&
                                     !modernUiHandled)
                            {
                                FeedPortableKey(event.key);
                            }
                        }
                    }
                }
            }
#ifndef _WIN32
            if (event.type == SDL_EVENT_WINDOW_MOUSE_ENTER)
            {
                // Wayland can deliver the first pointer-enter after the startup
                // cursor-hide, dropping it and leaving the OS cursor over the
                // game's own; re-apply the hide on every enter (issue #462).
                MuApplyCursorVisibility();
            }
#endif

            ++messageProcessed;
            if (applicationDiagnostics_.MessageLimit() > 0 &&
                messageProcessed >= applicationDiagnostics_.MessageLimit())
            {
                break;
            }
        }

        // Start/stop SDL text input as a portable text field gains or loses
        // focus, so SDL only emits SDL_EVENT_TEXT_INPUT while one is active (#447).
        {
            const auto focusedSession = inputRouting.FocusedSession();
            const auto modernArea = focusedSession.has_value()
                                        ? sessionManager_.ModernTextInputArea(*focusedSession)
                                        : std::nullopt;
            auto *focusedField = focusedSession.has_value()
                                     ? sessionManager_.FocusedTextInputBox(*focusedSession)
                                     : nullptr;
            const bool wantTextInput = modernArea.has_value() || focusedField != nullptr;
            if (appWindow_.SetTextInputActive(wantTextInput) && g_sdlWindow != nullptr)
            {
                if (wantTextInput)
                    SDL_StartTextInput(g_sdlWindow);
                else
                    SDL_StopTextInput(g_sdlWindow);
            }

            // Anchor the IME candidate window at the caret (reference px -> window
            // px) so composition UI appears next to the text being typed (#447).
            std::optional<SessionDisplayRect> localArea = modernArea;
            int cx = 0;
            int cy = 0;
            int cw = 0;
            int ch = 0;
            if (!localArea.has_value() && focusedField != nullptr &&
                focusedField->GetCaretArea(cx, cy, cw, ch))
            {
                localArea = SessionDisplayRect{static_cast<int>(cx * g_fScreenRate_x),
                                               static_cast<int>(cy * g_fScreenRate_y),
                                               static_cast<std::uint32_t>(cw * g_fScreenRate_x),
                                               static_cast<std::uint32_t>(ch * g_fScreenRate_y)};
            }
            if (wantTextInput && g_sdlWindow != nullptr && localArea.has_value())
            {
                const auto windowArea =
                    focusedSession.has_value()
                        ? inputRouting.SessionRectToWindow(*focusedSession, *localArea)
                        : std::nullopt;
                if (windowArea.has_value())
                {
                    const SDL_Rect area = {windowArea->x, windowArea->y,
                                           static_cast<int>(windowArea->width),
                                           static_cast<int>(windowArea->height)};
                    // Only push when the caret rect actually moves; resending every
                    // frame is wasteful and can flicker the candidate window.
                    if (appWindow_.UpdateTextInputArea(area))
                    {
                        SDL_SetTextInputArea(g_sdlWindow, &area, 0);
                    }
                }
            }
        }

        if (ApplicationSessionRuntime *runtime = applicationKeeper_.ApplicationSessionRuntimeUnit())
        {
            runtime->ProcessPendingCommands();
        }

        // Resolve every packet through its immutable handle/session/role/
        // generation snapshot. Workspace focus and the legacy SocketClient
        // spelling are absent from inbound target selection.
        (void)applicationNetwork_.DispatchPackets(sessionManager_);

        // Run a pending reconnect teardown between frames (self-guards on its
        // pending flag). Replaces the old WM_START_RECONNECT round-trip.
        sessionManager_.BeginReconnects();

        // Fire any due timers. Replaces the Win32 SetTimer/WM_TIMER dispatch.
        frameTimerScheduler_.Tick();

        if (applicationFrame_.ShouldRenderNextFrame())
        {
            if (g_bUseWindowMode || g_bWndActive || g_HasInactiveFpsOverride)
            {
                if (!TryBeginRenderFrame())
                    continue;
                // Consume input edges only when a worker frame will use them.
                keyInput_.ScanAsyncKeyState();
                input_.Update();
                sessionManager_.RouteKeyboardState(sessionWorkspace_.InputFocusedSession(),
                                                   keyInput_.StateData());
                const double frameDeltaMs = applicationFrame_.AdvanceClock();
                sessionManager_.AdvanceSkillDelays(static_cast<int>(frameDeltaMs));
                applicationFrame_.UpdateFrameStats();

                const ApplicationOwnerWork ownerWork =
                    renderPipelineEnabled
                        ? ApplicationOwnerWork{this,
                                               &ApplicationLoopUnit::ReplayReadyFrameCallback}
                        : ApplicationOwnerWork{};
                sessionManager_.AdvanceSessions(frameDeltaMs, ownerWork);
                sessionManager_.ResetTransientMouseStates();
                if (renderPipelineEnabled)
                {
                    // Early frame exits can bypass the render-worker overlap.
                    // ReplayReadyFrame is idempotent after moving the frame.
                    ReplayReadyFrame();
                }
                sessionManager_.UpdateReconnects();

                const bool sceneCompatibilityReady = sessionManager_.UpdateSceneCompatibility();
                auto nextRenderFrame =
                    sceneCompatibilityReady
                        ? ApplicationRenderFrame::TryCreate(sessionManager_.LastFrameSequence(),
                                                            sessionManager_.LastAdvanceResults(),
                                                            sessionWorkspace_,
                                                            applicationKeeper_.BitmapRegistry())
                        : nullptr;
                applicationFrame_.MarkFrameRendered();
                sessionWorkspace_.ApplyAudioGating();
                if (renderPipelineEnabled)
                {
                    readyRenderFrame_ = std::move(renderPipelineState_->preparedFrame);
                    renderPipelineState_->preparedFrame = std::move(nextRenderFrame);
                }
                else
                {
                    readyRenderFrame_ = std::move(nextRenderFrame);
                    ReplayReadyFrame();
                }
            }
        }
        else
        {
            // SDL_PollEvent above already drained pending events, so just pace
            // the frame.
            WaitForNextActivity(precise == TIMERR_NOERROR);
        }

        applicationKeeper_.CollectIdleAssets(MonotonicMilliseconds());

    } // while (!appWindow_.CloseRequested())

    if (precise == TIMERR_NOERROR)
    {
        timeEndPeriod(target_resolution);
    }

    return MSG{};
}

MSG ApplicationLegacyCalls::MainLoop()
{
    return applicationKeeper_.ApplicationLoopUnitShortcut()->MainLoop();
}
void ApplicationLegacyCalls::DestroyWindow()
{
    applicationKeeper_.ApplicationLoopUnitShortcut()->DestroyWindow();
}
#ifdef _WIN32
LRESULT CALLBACK ApplicationLegacyCalls::WndProc(HWND window, UINT message, WPARAM wParam,
                                                 LPARAM lParam)
{
    return applicationKeeper_.ApplicationLoopUnitShortcut()->WndProc(window, message, wParam,
                                                                     lParam);
}
bool ApplicationLegacyCalls::Win32MessageHook(void *userdata, MSG *message)
{
    return applicationKeeper_.ApplicationLoopUnitShortcut()->Win32MessageHook(userdata, message);
}
#endif
void ApplicationLegacyCalls::HandleMouseMotion(float windowX, float windowY)
{
    applicationKeeper_.ApplicationLoopUnitShortcut()->HandleMouseMotion(windowX, windowY);
}
void ApplicationLegacyCalls::FeedPortableTextInput(const char *utf8)
{
    applicationKeeper_.ApplicationLoopUnitShortcut()->FeedPortableTextInput(utf8);
}
bool ApplicationLegacyCalls::FeedPortableKey(const SDL_KeyboardEvent &key)
{
    return applicationKeeper_.ApplicationLoopUnitShortcut()->FeedPortableKey(key);
}
void ApplicationLegacyCalls::HandleMouseButton(const SDL_Event &event)
{
    applicationKeeper_.ApplicationLoopUnitShortcut()->HandleMouseButton(event);
}
void ApplicationLegacyCalls::CheckHack()
{
    applicationKeeper_.ApplicationLoopUnitShortcut()->CheckHack();
}

#ifdef _PVP_ADD_MOVE_SCROLL
extern CMurdererMove g_MurdererMove;
#endif // _PVP_ADD_MOVE_SCROLL

bool ApplicationLoopUnit::PressKey(int Key)
{
    if (IsKeyDown(Key))
    {
        if (KeyState[Key] == false)
        {
            KeyState[Key] = true;
            return true;
        }
    }
    else
        KeyState[Key] = false;
    return false;
}

bool ApplicationLegacyCalls::PressKey(int key)
{
    return applicationKeeper_.ApplicationLoopUnitShortcut()->PressKey(key);
}
