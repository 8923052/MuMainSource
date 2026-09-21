#pragma once
#include "support/CoreMath.h"

#include "app/Application.h"

#include "session/SessionRuntime.h"

#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

struct ApplicationConfigValues final
{
    int windowWidth = 1024;
    int windowHeight = 768;
    bool windowed = true;
    int masterSoundVolume = 5;
    int masterMusicVolume = 5;
    std::wstring defaultConnectServerIp = L"127.127.127.127";
    int defaultConnectServerPort = 44406;
    std::wstring assetLanguage = L"Eng";
    std::wstring uiLocale = L"en";
    std::wstring font;
    float rmlUiScale = 1.6F;
    int initialCameraZoom = 1735;
    int controlUiScalePercent = 120;
    int sessionWorkerCount = 8;
    int legacyReferenceFps = Core::Time::DefaultReferenceFps;
    int sharedAssetIdleSeconds = 120;
    bool renderPipeline = false;
    bool vSync = true;
    float fpsLimit = 0.0F;
    std::wstring rendererBackend = L"auto";

    friend bool operator==(const ApplicationConfigValues &,
                           const ApplicationConfigValues &) = default;
};

class SessionConfigStore;

class ApplicationConfigUnit final : protected ApplicationLegacyCalls
{
  public:
    ApplicationConfigUnit(ApplicationKeeper &keeper, SessionConfigStore &sessionConfigStore);
    ApplicationConfigUnit(ApplicationKeeper &keeper, std::filesystem::path configPath) noexcept;

    ApplicationConfigUnit(const ApplicationConfigUnit &) = delete;
    ApplicationConfigUnit &operator=(const ApplicationConfigUnit &) = delete;
    ApplicationConfigUnit(ApplicationConfigUnit &&) = delete;
    ApplicationConfigUnit &operator=(ApplicationConfigUnit &&) = delete;

    bool Load(SessionConfigStore &sessionConfigStore);
    bool Save() const;
    bool IsLoaded() const noexcept;

    ApplicationConfigValues &Values() noexcept;
    const ApplicationConfigValues &Values() const noexcept;

    void SetWindowSize(int width, int height) noexcept;
    void SetWindowMode(bool windowed) noexcept;
    void SetMasterSoundVolume(int level) noexcept;
    void SetMasterMusicVolume(int level) noexcept;
    void SetUiLocale(std::wstring locale);
    void SetFont(std::wstring font);

  private:
    friend class ApplicationKeeperTestPeer;

    void LoadAppValues();
    void LoadUiValues(const ApplicationConfigValues &defaults);
    void LoadPerformanceValues(const ApplicationConfigValues &defaults);
    bool SavePerformanceValues() const;
    bool RemoveLegacyAppKeys() const;

    ApplicationConfigValues &values_;
    std::filesystem::path configPath_;
    bool loaded_ = false;
};

using SessionConfigCatalog = std::map<SessionSlotId, SessionConfigValues>;

inline constexpr std::int32_t SessionShortcutModifierControl = -1;
inline constexpr std::int32_t SessionShortcutModifierShift = -2;
inline constexpr std::int32_t SessionShortcutModifierAlt = -3;
inline constexpr std::int32_t SessionShortcutModifierMeta = -4;
inline constexpr std::size_t SessionShortcutScancodeCount = 512;

struct SessionShortcutBinding final
{
    std::vector<std::int32_t> keys;

    friend bool operator==(const SessionShortcutBinding &, const SessionShortcutBinding &) = default;
};

struct SessionShortcutSettings final
{
    SessionShortcutBinding toggleControlBar;
    std::array<SessionShortcutBinding, 9> slotViews;
};

class SessionConfigStore final : protected ApplicationLegacyCalls
{
  public:
    using CredentialSnapshot = std::pair<SessionSlotId, SessionConfigValues>;
    explicit SessionConfigStore(ApplicationKeeper &keeper);
    SessionConfigStore(ApplicationKeeper &keeper, std::filesystem::path configPath) noexcept;

    SessionConfigStore(const SessionConfigStore &) = delete;
    SessionConfigStore &operator=(const SessionConfigStore &) = delete;
    SessionConfigStore(SessionConfigStore &&) = delete;
    SessionConfigStore &operator=(SessionConfigStore &&) = delete;

    static std::filesystem::path PathFromCommandLine(const wchar_t *commandLine);
    bool Load();
    bool IsLoaded() const noexcept;
    bool FileExistedAtLoad() const noexcept;
    std::chrono::seconds InitDelay() const noexcept;
    const SessionShortcutSettings &Shortcuts() const noexcept;

    const SessionConfigValues *Find(SessionSlotId slotId) const noexcept;
    SessionConfigValues ProfileOrDefault(SessionSlotId slotId) const;
    SessionConfigValues TakeStartupProfile(SessionSlotId slotId);
    std::vector<SessionSlotId> ValidSlotsOrFallback() const;
    std::optional<SessionSlotId> NextAppendSlot(
        std::optional<SessionSlotId> greatestLiveSlot = std::nullopt) const noexcept;
    bool AppendDefaultSlot(SessionSlotId slotId);
    bool DeleteSlot(SessionSlotId slotId);
    bool MoveOrSwapSlot(SessionSlotId source, SessionSlotId destination);
    bool MoveOrSwapSlot(SessionSlotId source, SessionSlotId destination,
                        const std::function<void()> &applyLiveCommit);
    bool UpdateCredentials(const std::function<CredentialSnapshot()> &captureCurrent);
    bool Update(SessionSlotId slotId, const SessionConfigValues &values);

    bool ProtectCredentials(SessionConfigValues &values, const wchar_t *username,
                            const wchar_t *password) const;
    void DecryptCredentials(const SessionConfigValues &values, wchar_t *username, wchar_t *password,
                            std::size_t usernameSize, std::size_t passwordSize) const;

  private:
    friend class ApplicationKeeperTestPeer;

    static std::wstring ProtectSetting(const wchar_t *input);
    static std::wstring UnprotectSetting(const std::wstring &input);
    static std::wstring BinaryToHex(const BYTE *data, DWORD size);
    static std::vector<BYTE> HexToBinary(const std::wstring &hex);

    bool CommitCatalog(SessionConfigCatalog &&candidate);
    SessionConfigValues ReadProfile(const std::wstring &section) const;

    SessionConfigCatalog &profiles_;
    std::filesystem::path configPath_;
    bool loaded_ = false;
    bool fileExistedAtLoad_ = false;
    std::chrono::seconds initDelay_{};
    SessionShortcutSettings shortcuts_;
    mutable std::mutex transactionMutex_;
};

class ApplicationFramePlan final
{
  public:
    static constexpr std::size_t MaximumSessions = 1024;

    static std::optional<ApplicationFramePlan> TryCreate(
        std::uint64_t frameSequence, double frameDeltaMilliseconds,
        std::span<const SessionFrameInput> inputs, bool cancellationRequested = false) noexcept
    {
        if (frameSequence == 0 || !std::isfinite(frameDeltaMilliseconds) ||
            frameDeltaMilliseconds < 0.0 || inputs.size() > MaximumSessions)
        {
            return std::nullopt;
        }
        for (const SessionFrameInput &input : inputs)
        {
            if (input.renderRequired)
            {
                if (input.surfaceGeneration == 0 || input.targetWidth == 0 ||
                    input.targetHeight == 0)
                {
                    return std::nullopt;
                }
            }
            const SessionVisualAnimationInput &visual = input.visualAnimation;
            if (visual.workerSafeLegacyPath &&
                (!std::isfinite(visual.currentTickCount) ||
                 !std::isfinite(visual.previousWaterChange) || visual.previousWaterTexture < 0 ||
                 visual.previousWaterTexture >= 32))
            {
                return std::nullopt;
            }
            if (!SessionPhysicsFrameInput::TryCreate(input.physics.animationFactor,
                                                     input.physics.worldTime)
                     .has_value())
            {
                return std::nullopt;
            }
        }
        try
        {
            return ApplicationFramePlan(
                frameSequence, frameDeltaMilliseconds, cancellationRequested,
                std::vector<SessionFrameInput>(inputs.begin(), inputs.end()));
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::uint64_t FrameSequence() const noexcept
    {
        return frameSequence_;
    }
    double FrameDeltaMilliseconds() const noexcept
    {
        return frameDeltaMilliseconds_;
    }
    bool CancellationRequested() const noexcept
    {
        return cancellationRequested_;
    }
    std::size_t SessionCount() const noexcept
    {
        return inputs_.size();
    }
    const SessionFrameInput &Session(std::size_t index) const noexcept
    {
        return inputs_[index];
    }

  private:
    ApplicationFramePlan(std::uint64_t frameSequence, double frameDeltaMilliseconds,
                         bool cancellationRequested, std::vector<SessionFrameInput> inputs) noexcept
        : frameSequence_(frameSequence), frameDeltaMilliseconds_(frameDeltaMilliseconds),
          cancellationRequested_(cancellationRequested), inputs_(std::move(inputs))
    {
    }

    std::uint64_t frameSequence_;
    double frameDeltaMilliseconds_;
    bool cancellationRequested_;
    std::vector<SessionFrameInput> inputs_;
};

struct SessionAdvanceJob final
{
    using AdvanceFunction = bool (*)(void *) noexcept;
    using BuildOrderedEffectsFunction = bool (*)(void *, SessionOrderedEffectBatch &) noexcept;

    SessionId sessionId;
    SessionGeneration generation;
    std::uint64_t frameSequence;
    bool renderRequired;
    std::uint64_t surfaceGeneration;
    void *context;
    AdvanceFunction advance;
    BuildOrderedEffectsFunction buildOrderedEffects = nullptr;
};

struct ApplicationSessionSchedulerMetrics final
{
    std::uint64_t queuedJobs = 0;
    std::uint64_t inlineJobs = 0;
    std::uint64_t completedJobs = 0;
    std::uint64_t cancelledJobs = 0;
    std::uint64_t staleJobs = 0;
    std::uint64_t rejectedConcurrentJobs = 0;
    std::uint64_t workerWakeups = 0;
    std::uint64_t maximumQueueDepth = 0;
    std::uint64_t maximumActiveWorkers = 0;
    std::uint64_t queueWaitNanoseconds = 0;
    std::uint64_t workerWallNanoseconds = 0;
    std::uint64_t frameBarrierWaitNanoseconds = 0;
};

class ApplicationSessionScheduler final
{
  public:
    static constexpr std::size_t DefaultQueueCapacity = ApplicationFramePlan::MaximumSessions;

    explicit ApplicationSessionScheduler(std::size_t workerCount = 0,
                                         std::size_t queueCapacity = DefaultQueueCapacity) noexcept;
    ~ApplicationSessionScheduler();

    ApplicationSessionScheduler(const ApplicationSessionScheduler &) = delete;
    ApplicationSessionScheduler &operator=(const ApplicationSessionScheduler &) = delete;
    ApplicationSessionScheduler(ApplicationSessionScheduler &&) = delete;
    ApplicationSessionScheduler &operator=(ApplicationSessionScheduler &&) = delete;

    std::optional<SessionGeneration> RegisterLane(SessionId id) noexcept;
    bool QuiesceAndRemoveLane(SessionId id) noexcept;
    bool ExecuteFrame(const ApplicationFramePlan &plan, std::span<const SessionAdvanceJob> jobs,
                      std::span<SessionAdvanceResult> results,
                      ApplicationOwnerWork ownerWork = {}) noexcept;
    bool ExecuteFrameInline(const ApplicationFramePlan &plan,
                            std::span<const SessionAdvanceJob> jobs,
                            std::span<SessionAdvanceResult> results) noexcept;
    void Stop() noexcept;

    std::size_t WorkerCount() const noexcept;
    std::size_t QueueCapacity() const noexcept;
    ApplicationSessionSchedulerMetrics Metrics() const noexcept;

  private:
    struct Lane;
    struct FrameGroup;
    struct QueuedJob final
    {
        SessionAdvanceJob job;
        SessionAdvanceResult *result;
        std::shared_ptr<Lane> lane;
        FrameGroup *group;
        std::chrono::steady_clock::time_point queuedAt;
    };

    static std::size_t RecommendedWorkerCount() noexcept;
    bool ExecuteFrameWithPolicy(const ApplicationFramePlan &plan,
                                std::span<const SessionAdvanceJob> jobs,
                                std::span<SessionAdvanceResult> results, bool forceInline,
                                ApplicationOwnerWork ownerWork) noexcept;
    void WorkerLoop() noexcept;
    void RunJob(const SessionAdvanceJob &job, SessionAdvanceResult &result,
                const std::shared_ptr<Lane> &lane, FrameGroup &group, bool executedInline) noexcept;
    void CancelQueuedJob(QueuedJob &queued) noexcept;

    static thread_local ApplicationSessionScheduler *activeScheduler_;

    const std::size_t queueCapacity_;
    mutable std::mutex mutex_;
    std::condition_variable workAvailable_;
    std::map<SessionId, std::shared_ptr<Lane>> lanes_;
    std::deque<QueuedJob> queue_;
    std::vector<std::thread> workers_;
    SessionGeneration::ValueType nextGeneration_ = 1;
    bool stopping_ = false;
    std::atomic<std::uint64_t> queuedJobs_{0};
    std::atomic<std::uint64_t> inlineJobs_{0};
    std::atomic<std::uint64_t> completedJobs_{0};
    std::atomic<std::uint64_t> cancelledJobs_{0};
    std::atomic<std::uint64_t> staleJobs_{0};
    std::atomic<std::uint64_t> rejectedConcurrentJobs_{0};
    std::atomic<std::uint64_t> workerWakeups_{0};
    std::atomic<std::uint64_t> maximumQueueDepth_{0};
    std::atomic<std::uint64_t> activeWorkers_{0};
    std::atomic<std::uint64_t> maximumActiveWorkers_{0};
    std::atomic<std::uint64_t> queueWaitNanoseconds_{0};
    std::atomic<std::uint64_t> workerWallNanoseconds_{0};
    std::atomic<std::uint64_t> frameBarrierWaitNanoseconds_{0};
};

// Portable .ini profile shim (issue #462, Phase 3).
// The config layer persists settings with the Win32 private-profile API. On
// Windows these come from <windows.h>; elsewhere this provides portable
// implementations that read/write a UTF-8 .ini file with the same
// contract (case-insensitive sections/keys; a null value deletes a key; a null
// key deletes a section; a zero buffer size returns the required length).

#ifdef _WIN32

// GetPrivateProfile*/WritePrivateProfile* come from <windows.h>.

#else // ---- non-Windows ----------------------------------------------------

UINT GetPrivateProfileIntW(LPCWSTR lpAppName, LPCWSTR lpKeyName, INT nDefault, LPCWSTR lpFileName);
DWORD GetPrivateProfileStringW(LPCWSTR lpAppName, LPCWSTR lpKeyName, LPCWSTR lpDefault,
                               LPWSTR lpReturnedString, DWORD nSize, LPCWSTR lpFileName);
BOOL WritePrivateProfileStringW(LPCWSTR lpAppName, LPCWSTR lpKeyName, LPCWSTR lpString,
                                LPCWSTR lpFileName);

#endif // _WIN32

struct TimeCheck
{
    double iBackupTime; // �ð� ���� ���
    int iIndex;         // �ð� ���� ��ȣ
    bool bTimeCheck;    // �ð� ���� üũ
};

class CTimeCheck
{
  public:
    std::vector<TimeCheck> stl_Time;
    std::vector<TimeCheck>::iterator stl_Time_I;

    CTimeCheck();
    virtual ~CTimeCheck();

    int CheckIndex(int index);
    bool GetTimeCheck(double worldTime, int index, int DelayTime);
    void DeleteTimeIndex(int index);
};

class CTimer
{
  public:
    CTimer();
    ~CTimer() = default;

    double GetTimeElapsed(); // Time elapsed since the last reset
    double GetAbsTime();     // Absolute time since timer creation
    void ResetTimer();       // Resets the start time to now

  private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    TimePoint m_startTime;    // Start time for relative measurements
    TimePoint m_absStartTime; // Absolute start time of the timer
};

class CTimer2
{
  public:
    using StartTickTime = std::chrono::steady_clock::time_point;

    CTimer2() : m_startTickCount(0), m_delay(0), m_timeReached(false)
    {
    }
    ~CTimer2() = default;

    void SetTimer(unsigned int delay);
    unsigned int GetDelay() const;
    void ResetTimer();
    void UpdateTime(StartTickTime &startTickTime);
    bool IsTime() const;

  private:
    unsigned int m_startTickCount;
    unsigned int m_delay;
    bool m_timeReached;
};

class DebouncedAction
{
  public:
    explicit DebouncedAction(std::function<void()> callback, int period)
        : interval(std::chrono::milliseconds(period)), lastCallTime(), callback(std::move(callback))
    {
    }

    void invoke()
    {
        auto now = std::chrono::high_resolution_clock::now();

        // Call immediately on the first invocation or on the next interval
        if (lastCallTime == std::chrono::high_resolution_clock::time_point() ||
            now - lastCallTime >= interval)
        {
            callback();
            lastCallTime = now;
        }
    }

  private:
    std::chrono::milliseconds interval;
    std::chrono::high_resolution_clock::time_point lastCallTime;
    std::function<void()> callback;
};
