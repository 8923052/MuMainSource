#include "app/ApplicationDiagnostics.h"
#include "app/Application.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "app/SdlGpuRenderBackend.h"
#include "domain/ItemsSkills.h"
#include "render/Sprites.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "session/GameSession.h"
#include "session/SessionGameplay.h"
#include "session/SessionRender.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "turbojpeg.h"
#include "ui/runtime/UiControls.h"

#include <fcntl.h>

#ifdef _WIN32
#include <Pdh.h>
#include <PdhMsg.h>
#include <Psapi.h>
#include <ddraw.h>
#include <dinput.h>
#include <dmusicc.h>
#include <eh.h>
#include <imagehlp.h>
#include <io.h>
#else
#include <sys/resource.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#include <sys/sysctl.h>
#endif

#if defined(_DEBUG) && defined(_WIN32)
namespace
{
enum HotspotMemoryOwner : std::size_t
{
    ActiveSessions,
    SessionFixed,
    ModelPool,
    RetainedModelAssets,
    TerrainGeometry,
    DecodedTextures,
    RenderTapePool,
    CompletionPayload,
    GpuUploadArena,
    GpuDownloadArena,
    GpuResidentTextures,
    GpuGeometryBuffers,
    GpuSessionTargets,
    WorldTerrain,
    HotspotMemoryOwnerCount,
};

std::array<std::uint32_t, HotspotMemoryOwnerCount> hotspotMemoryOwners{};
std::chrono::steady_clock::time_point nextHotspotMemoryOwnerSample;
} // namespace

extern "C"
{
    __declspec(dllexport) const std::uint32_t *MuHotspotMemoryOwners = hotspotMemoryOwners.data();
    extern __declspec(dllexport) const std::uint32_t MuHotspotMemoryOwnerCount =
        HotspotMemoryOwnerCount;
}
#endif

namespace
{
std::size_t CurrentProcessMemoryBytes() noexcept
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    return K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))
               ? counters.WorkingSetSize
               : 0;
#else
    return 0;
#endif
}

#if defined(_WIN32)
class ProcessGpuUsage final
{
  public:
    ProcessGpuUsage() : processToken_(L"pid_" + std::to_wstring(GetCurrentProcessId()) + L"_")
    {
        if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS ||
            PdhAddEnglishCounterW(query_, L"\\GPU Engine(*)\\Utilization Percentage", 0,
                                  &counter_) != ERROR_SUCCESS)
        {
            Reset();
            return;
        }
        (void)PdhCollectQueryData(query_);
    }

    ~ProcessGpuUsage()
    {
        Reset();
    }

    double GetUsage() noexcept
    {
        if (counter_ == nullptr || PdhCollectQueryData(query_) != ERROR_SUCCESS)
        {
            return 0.0;
        }
        DWORD bytes = 0;
        DWORD count = 0;
        if (PdhGetFormattedCounterArrayW(counter_, PDH_FMT_DOUBLE, &bytes, &count, nullptr) !=
            PDH_MORE_DATA)
        {
            return 0.0;
        }
        try
        {
            buffer_.resize(bytes);
        }
        catch (...)
        {
            return 0.0;
        }
        auto *const values = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer_.data());
        if (PdhGetFormattedCounterArrayW(counter_, PDH_FMT_DOUBLE, &bytes, &count, values) !=
            ERROR_SUCCESS)
        {
            return 0.0;
        }
        double usage = 0.0;
        for (DWORD index = 0; index < count; ++index)
        {
            const PDH_FMT_COUNTERVALUE_ITEM_W &value = values[index];
            if (value.szName != nullptr && wcsstr(value.szName, processToken_.c_str()) != nullptr)
            {
                usage = (std::max)(usage, value.FmtValue.doubleValue);
            }
        }
        return std::clamp(usage, 0.0, 100.0);
    }

  private:
    void Reset() noexcept
    {
        if (query_ != nullptr)
        {
            PdhCloseQuery(query_);
        }
        query_ = nullptr;
        counter_ = nullptr;
    }

    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER counter_ = nullptr;
    std::wstring processToken_;
    std::vector<std::byte> buffer_;
};
#else
class ProcessGpuUsage final
{
  public:
    double GetUsage() const noexcept
    {
        return 0.0;
    }
};
#endif
} // namespace

#if defined(_DEBUG) && defined(_WIN32)
namespace
{
constexpr int HotspotProfilerMaximumDurationSeconds = 3600;
constexpr int HotspotProfilerSampleIntervalMs = 10;

struct HotspotProfilerPaths final
{
    std::filesystem::path executable;
    std::filesystem::path symbols;
    std::filesystem::path script;
    std::filesystem::path sampler;
    std::filesystem::path outputDirectory;
    std::filesystem::path stopFile;
};

std::string ToUtf8(const std::filesystem::path &path)
{
    char *const text = SDL_iconv_wchar_utf8(path.c_str());
    if (text == nullptr)
    {
        return {};
    }
    const std::string result(text);
    SDL_free(text);
    return result;
}

std::optional<HotspotProfilerPaths> MakeHotspotProfilerPaths()
{
    std::wstring executable(32768, L'\0');
    const DWORD length =
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size())
    {
        return std::nullopt;
    }
    executable.resize(length);

    HotspotProfilerPaths paths;
    paths.executable = executable;
    paths.symbols = paths.executable;
    paths.symbols.replace_extension(L".pdb");
    const std::filesystem::path gameDirectory = paths.executable.parent_path();
    paths.script = gameDirectory / L"tools" / L"profile_hotspots.ps1";
    const wchar_t *const samplerName =
        sizeof(void *) == 8 ? L"hotspot_sampler_x64.exe" : L"hotspot_sampler_x86.exe";
    paths.sampler = gameDirectory / L"tools" / samplerName;
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    paths.outputDirectory = gameDirectory / L"profiles" / (L"hotspots-" + std::to_wstring(stamp));
    paths.stopFile = paths.outputDirectory / L"stop.request";
    return paths;
}

template <std::size_t Size>
SDL_Process *CreateBackgroundProcess(const std::array<std::string, Size> &arguments,
                                     const std::filesystem::path &workingDirectory)
{
    std::array<const char *, Size + 1> pointers{};
    std::transform(arguments.begin(), arguments.end(), pointers.begin(),
                   [](const std::string &value) { return value.c_str(); });
    const std::string workingDirectoryUtf8 = ToUtf8(workingDirectory);

    const SDL_PropertiesID properties = SDL_CreateProperties();
    if (properties == 0)
    {
        return nullptr;
    }
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                           const_cast<void *>(static_cast<const void *>(pointers.data())));
    SDL_SetStringProperty(properties, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,
                          workingDirectoryUtf8.c_str());
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_Process *const process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    return process;
}
} // namespace
#endif

ApplicationDiagnostics::ApplicationDiagnostics(ApplicationKeeper &keeper,
                                               ApplicationFrameUnit &frame) noexcept
    : ApplicationLegacyCalls(keeper), frame_(frame),
      g_bUseWindowMode(keeper.PlatformStorageRef().g_bUseWindowMode),
      g_bUseFullscreenMode(keeper.PlatformStorageRef().g_bUseFullscreenMode),
      g_ErrorReport(keeper.DiagnosticsStorageRef().g_ErrorReport),
      consoleDebug_(keeper.DiagnosticsStorageRef().consoleDebug),
      consoleWindow_(keeper.DiagnosticsStorageRef().consoleWindow),
      cpuUsage_(keeper.DiagnosticsStorageRef().cpuUsage),
      m_ExeVersion(keeper.DiagnosticsStorageRef().m_ExeVersion),
      CPU_AVG(keeper.DiagnosticsStorageRef().CPU_AVG),
      GPU_AVG(keeper.DiagnosticsStorageRef().GPU_AVG),
      processMemoryBytes_(keeper.DiagnosticsStorageRef().processMemoryBytes),
      cpuAverageMutex_(keeper.DiagnosticsStorageRef().cpuAverageMutex),
      stopCpuWorker_(keeper.DiagnosticsStorageRef().stopCpuWorker),
      g_MaxMessagePerCycle(keeper.DiagnosticsStorageRef().g_MaxMessagePerCycle)
{
    consoleDebug_.Bind(*this);
    (void)applicationKeeper_.RegisterApplicationDiagnostics(*this);
#if defined(_DEBUG) && defined(_WIN32)
    if (const char *prefix =
            SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "MU_RENDER_PROFILE_CAPTURE"))
    {
        try
        {
            renderingProfilePrefix_ = prefix;
        }
        catch (...)
        {
            renderingProfilePrefix_.clear();
        }
        renderingProfileCaptureAt_ = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    }
#endif
}

ApplicationDiagnostics::~ApplicationDiagnostics()
{
    StopHotspotProfiler();
    ReleaseHotspotProfiler();
    StopCpuWorker();
    CloseConsoleWindow();
    consoleDebug_.Unbind(*this);
}

void ApplicationDiagnostics::StartCpuWorker()
{
    if (cpuUsageWorker_.joinable())
    {
        return;
    }
    stopCpuWorker_.store(false, std::memory_order_release);
    cpuUsageWorker_ = std::thread([this] { RecordCpuUsage(); });
}

void ApplicationDiagnostics::StopCpuWorker() noexcept
{
    stopCpuWorker_.store(true, std::memory_order_release);
    if (cpuUsageWorker_.joinable())
    {
        cpuUsageWorker_.join();
    }
}

bool ApplicationDiagnostics::IsCpuWorkerRunning() const noexcept
{
    return cpuUsageWorker_.joinable();
}

double ApplicationDiagnostics::CpuAverage() const noexcept
{
    const std::scoped_lock lock(cpuAverageMutex_);
    return CPU_AVG;
}

double ApplicationDiagnostics::GpuUsagePercent() const noexcept
{
    const std::scoped_lock lock(cpuAverageMutex_);
    return GPU_AVG;
}

std::size_t ApplicationDiagnostics::ProcessMemoryBytes() const noexcept
{
    const std::scoped_lock lock(cpuAverageMutex_);
    return processMemoryBytes_;
}

int ApplicationDiagnostics::MessageLimit() const noexcept
{
    return g_MaxMessagePerCycle;
}

void ApplicationDiagnostics::SetMaxMessagePerCycle(int messages) noexcept
{
    constexpr int MinimumCustomLimit = 3;
    g_MaxMessagePerCycle = messages > 0 ? (std::max)(messages, MinimumCustomLimit) : messages;
}

wchar_t (&ApplicationDiagnostics::ExecutableVersion() noexcept)[11]
{
    return m_ExeVersion;
}

CErrorReport &ApplicationDiagnostics::ErrorReport() noexcept
{
    return g_ErrorReport;
}

CmuConsoleDebug &ApplicationDiagnostics::ConsoleDebug() noexcept
{
    return consoleDebug_;
}

ApplicationFrameUnit &ApplicationDiagnostics::Frame() noexcept
{
    return frame_;
}

bool ApplicationDiagnostics::OpenConsoleWindow(const std::wstring &title)
{
    return consoleWindow_.Open(title, frame_.Timer2StartTickTime());
}

void ApplicationDiagnostics::CloseConsoleWindow()
{
    consoleWindow_.Close();
}
bool ApplicationDiagnostics::SetConsoleTitleW(const std::wstring &title)
{
    return consoleWindow_.SetTitle(title);
}
const std::wstring &ApplicationDiagnostics::GetConsoleTitleW()
{
    return consoleWindow_.GetTitle();
}
HWND ApplicationDiagnostics::GetConsoleWndHandle()
{
    return consoleWindow_.GetWndHandle();
}
bool ApplicationDiagnostics::IsConsoleVisible()
{
    return consoleWindow_.IsVisible();
}
void ApplicationDiagnostics::ShowConsole(bool show)
{
    consoleWindow_.Show(show);
}
void ApplicationDiagnostics::ClearConsoleScreen()
{
    consoleWindow_.ClearScreen();
}
WORD ApplicationDiagnostics::GetConsoleTextColorIndex(WORD *background)
{
    return consoleWindow_.GetTextColorIndex(background);
}
void ApplicationDiagnostics::SetConsoleTextColor(WORD text, WORD background)
{
    consoleWindow_.SetTextColor(text, background);
}
void ApplicationDiagnostics::ActivateCloseButton(bool active)
{
    consoleWindow_.ActivateCloseButton(active);
}
bool ApplicationDiagnostics::IsActiveCloseButton()
{
    return consoleWindow_.IsActiveCloseButton();
}
bool ApplicationDiagnostics::SaveConsoleScreenBuffer(const std::wstring &filename)
{
    return consoleWindow_.SaveScreenBuffer(filename);
}

void ApplicationDiagnostics::ToggleHotspotProfiler()
{
    PollHotspotProfiler();
    const HotspotProfilerState state = HotspotProfileState();
    if (state == HotspotProfilerState::Recording)
    {
        StopHotspotProfiler();
        return;
    }
    if (state != HotspotProfilerState::Stopping)
    {
        (void)StartHotspotProfiler();
    }
}

bool ApplicationDiagnostics::StartHotspotProfiler()
{
#if !defined(_DEBUG) || !defined(_WIN32)
    return false;
#else
    const std::optional<HotspotProfilerPaths> paths = MakeHotspotProfilerPaths();
    if (!paths.has_value() || !std::filesystem::exists(paths->script) ||
        !std::filesystem::exists(paths->symbols))
    {
        hotspotProfilerState_.store(HotspotProfilerState::Failed, std::memory_order_release);
        g_ErrorReport.Write(L"Hotspot profiler tools or Main.pdb are missing.\r\n");
        frame_.SetShowDebugInfo(true);
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(paths->outputDirectory, error);
    if (error)
    {
        hotspotProfilerState_.store(HotspotProfilerState::Failed, std::memory_order_release);
        g_ErrorReport.Write(L"Cannot create hotspot profile directory.\r\n");
        frame_.SetShowDebugInfo(true);
        return false;
    }

    const std::array<std::string, 25> arguments{
        "powershell.exe",
        "-NoLogo",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        ToUtf8(paths->script),
        "-ExePath",
        ToUtf8(paths->executable),
        "-PdbPath",
        ToUtf8(paths->symbols),
        "-ProcessId",
        std::to_string(GetCurrentProcessId()),
        "-DurationSeconds",
        std::to_string(HotspotProfilerMaximumDurationSeconds),
        "-SampleIntervalMs",
        std::to_string(HotspotProfilerSampleIntervalMs),
        "-WarmupSeconds",
        "0",
        "-OutputDirectory",
        ToUtf8(paths->outputDirectory),
        "-SamplerPath",
        ToUtf8(paths->sampler),
        "-StopFile",
        ToUtf8(paths->stopFile)};
    hotspotProfilerProcess_ = CreateBackgroundProcess(arguments, paths->executable.parent_path());
    if (hotspotProfilerProcess_ == nullptr)
    {
        hotspotProfilerState_.store(HotspotProfilerState::Failed, std::memory_order_release);
        g_ErrorReport.Write(L"Cannot start hotspot profiler: %hs\r\n", SDL_GetError());
        frame_.SetShowDebugInfo(true);
        return false;
    }

    hotspotProfilerOutputDirectory_ = paths->outputDirectory;
    hotspotProfilerStopFile_ = paths->stopFile;
    hotspotProfilerState_.store(HotspotProfilerState::Recording, std::memory_order_release);
    frame_.SetShowDebugInfo(true);
    g_ErrorReport.Write(L"Hotspot profile started: %ls\r\n",
                        hotspotProfilerOutputDirectory_.c_str());
    return true;
#endif
}

void ApplicationDiagnostics::StopHotspotProfiler()
{
#if defined(_DEBUG) && defined(_WIN32)
    if (HotspotProfileState() != HotspotProfilerState::Recording)
    {
        return;
    }
    std::ofstream stop(hotspotProfilerStopFile_, std::ios::trunc);
    if (!stop)
    {
        hotspotProfilerState_.store(HotspotProfilerState::Failed, std::memory_order_release);
        g_ErrorReport.Write(L"Cannot stop hotspot profiler cleanly.\r\n");
        return;
    }
    stop << "stop\n";
    hotspotProfilerState_.store(HotspotProfilerState::Stopping, std::memory_order_release);
#endif
}

void ApplicationDiagnostics::PollHotspotProfiler() noexcept
{
#if defined(_DEBUG) && defined(_WIN32)
    UpdateHotspotMemoryOwners();
    if (hotspotProfilerProcess_ == nullptr)
    {
        return;
    }
    int exitCode = 0;
    if (!SDL_WaitProcess(hotspotProfilerProcess_, false, &exitCode))
    {
        return;
    }

    ReleaseHotspotProfiler();
    const bool saved =
        exitCode == 0 && std::filesystem::exists(hotspotProfilerOutputDirectory_ / L"hotspots.md");
    hotspotProfilerState_.store(saved ? HotspotProfilerState::Saved : HotspotProfilerState::Failed,
                                std::memory_order_release);
    g_ErrorReport.Write(saved ? L"Hotspot profile saved: %ls\r\n"
                              : L"Hotspot profiler failed; see its output directory: %ls\r\n",
                        hotspotProfilerOutputDirectory_.c_str());
#endif
}

void ApplicationDiagnostics::CaptureRenderingProfile() noexcept
{
#if defined(_DEBUG) && defined(_WIN32)
    if (renderingProfilePrefix_.empty() ||
        std::chrono::steady_clock::now() < renderingProfileCaptureAt_)
        return;
    auto *sessions = applicationKeeper_.SessionManagerUnit();
    if (!sessions || sessions->SessionCount() == 0)
        return;
    try
    {
        bool captured = true;
        for (const auto id : sessions->SessionIds())
            captured =
                sessions->Find(id)->CaptureRenderingProfile(renderingProfilePrefix_) && captured;
        if (captured)
            renderingProfilePrefix_.clear();
    }
    catch (...)
    {
        return;
    }
#endif
}

void ApplicationDiagnostics::UpdateHotspotMemoryOwners() noexcept
{
#if defined(_DEBUG) && defined(_WIN32)
    const auto now = std::chrono::steady_clock::now();
    if (now < nextHotspotMemoryOwnerSample)
    {
        return;
    }
    nextHotspotMemoryOwnerSample = now + std::chrono::seconds(1);
    CaptureRenderingProfile();
    const auto toCounter = [](std::size_t bytes) noexcept {
        return static_cast<std::uint32_t>((std::min)(
            bytes, static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())));
    };

    hotspotMemoryOwners.fill(0);
    SessionManager *const sessions = applicationKeeper_.SessionManagerUnit();
    if (sessions != nullptr)
    {
        const SessionMemoryStats stats = sessions->MemoryStats();
        hotspotMemoryOwners[ActiveSessions] = static_cast<std::uint32_t>(sessions->SessionCount());
        hotspotMemoryOwners[SessionFixed] = toCounter(stats.fixedBytes);
        hotspotMemoryOwners[ModelPool] = toCounter(stats.modelPoolBytes);
    }
    hotspotMemoryOwners[RetainedModelAssets] =
        toCounter(applicationKeeper_.RetainedModelAssetBytes());
    hotspotMemoryOwners[TerrainGeometry] =
        toCounter(applicationKeeper_.RetainedTerrainGeometryBytes());
    hotspotMemoryOwners[HotspotMemoryOwner::WorldTerrain] =
        toCounter(applicationKeeper_.RetainedWorldTerrainBytes());
    hotspotMemoryOwners[DecodedTextures] =
        applicationKeeper_.BitmapRegistry().GetUsedTextureMemory();
    hotspotMemoryOwners[RenderTapePool] =
        toCounter(applicationKeeper_.renderTapeStorage_.StorageBytes());
    if (Compositor *const compositor = applicationKeeper_.CompositorUnit())
    {
        const SdlGpuMemoryStats stats = compositor->MemoryUsage();
        hotspotMemoryOwners[CompletionPayload] = toCounter(stats.completionPayloadBytes);
        hotspotMemoryOwners[GpuUploadArena] = toCounter(stats.uploadArenaBytes);
        hotspotMemoryOwners[GpuDownloadArena] = toCounter(stats.downloadArenaBytes);
        hotspotMemoryOwners[GpuResidentTextures] = toCounter(stats.residentTextureBytes);
        hotspotMemoryOwners[GpuGeometryBuffers] = toCounter(stats.geometryBufferBytes);
        hotspotMemoryOwners[GpuSessionTargets] = toCounter(stats.sessionTargetBytes);
    }
#endif
}

HotspotProfilerState ApplicationDiagnostics::HotspotProfileState() const noexcept
{
    return hotspotProfilerState_.load(std::memory_order_acquire);
}

void ApplicationDiagnostics::ReleaseHotspotProfiler() noexcept
{
#if defined(_DEBUG) && defined(_WIN32)
    if (hotspotProfilerProcess_ != nullptr)
    {
        SDL_DestroyProcess(hotspotProfilerProcess_);
        hotspotProfilerProcess_ = nullptr;
    }
#endif
}

bool ApplicationDiagnostics::ExceptionCallback(_EXCEPTION_POINTERS *exceptionInfo) noexcept
{
    (void)exceptionInfo;
    if (g_bUseWindowMode == FALSE && g_bUseFullscreenMode == TRUE)
    {
        ChangeDisplaySettings(nullptr, 0);
    }
    return true;
}

void ApplicationDiagnostics::RecordCpuUsage()
{
    constexpr std::size_t RecordingCount = 60;
    constexpr auto PublishInterval = std::chrono::milliseconds(250);
    constexpr auto SampleInterval = std::chrono::milliseconds(16);
    std::array<double, RecordingCount> recordings{};
    double sum = 0.0;
    std::size_t index = 0;
    std::size_t filled = 0;
    auto lastPublish = std::chrono::steady_clock::now();
    ProcessGpuUsage gpuUsage;

    while (!stopCpuWorker_.load(std::memory_order_acquire))
    {
        const double usage = std::clamp(cpuUsage_.GetUsage(), 0.0, 100.0);
        sum -= recordings[index];
        sum += usage;
        recordings[index] = usage;
        index = (index + 1) % RecordingCount;
        filled = (std::min)(filled + 1, RecordingCount);

        const auto now = std::chrono::steady_clock::now();
        if (now - lastPublish >= PublishInterval)
        {
            const double gpu = gpuUsage.GetUsage();
            const std::scoped_lock lock(cpuAverageMutex_);
            CPU_AVG = sum / filled;
            GPU_AVG = gpu;
            processMemoryBytes_ = CurrentProcessMemoryBytes();
            lastPublish = now;
        }
        std::this_thread::sleep_for(SampleInterval);
    }
}

bool ApplicationLegacyCalls::ExceptionCallback(_EXCEPTION_POINTERS *exceptionInfo)
{
    return applicationKeeper_.ApplicationDiagnosticsUnit()->ExceptionCallback(exceptionInfo);
}
void ApplicationLegacyCalls::RecordCpuUsage()
{
    applicationKeeper_.ApplicationDiagnosticsUnit()->RecordCpuUsage();
}
void ApplicationLegacyCalls::SetMaxMessagePerCycle(int messages)
{
    applicationKeeper_.ApplicationDiagnosticsUnit()->SetMaxMessagePerCycle(messages);
}
bool ApplicationLegacyCalls::OpenConsoleWindow(const std::wstring &title)
{
    return applicationKeeper_.ApplicationDiagnosticsUnit()->OpenConsoleWindow(title);
}
void ApplicationLegacyCalls::CloseConsoleWindow()
{
    applicationKeeper_.ApplicationDiagnosticsUnit()->CloseConsoleWindow();
}
bool ApplicationLegacyCalls::SetConsoleTitleW(const std::wstring &title)
{
    return applicationKeeper_.ApplicationDiagnosticsUnit()->SetConsoleTitleW(title);
}
const std::wstring &ApplicationLegacyCalls::GetConsoleTitleW()
{
    return applicationKeeper_.ApplicationDiagnosticsUnit()->GetConsoleTitleW();
}
HWND ApplicationLegacyCalls::GetConsoleWndHandle()
{
    return applicationKeeper_.ApplicationDiagnosticsUnit()->GetConsoleWndHandle();
}
bool ApplicationLegacyCalls::IsConsoleVisible()
{
    return applicationKeeper_.ApplicationDiagnosticsUnit()->IsConsoleVisible();
}
void ApplicationLegacyCalls::ShowConsole(bool show)
{
    applicationKeeper_.ApplicationDiagnosticsUnit()->ShowConsole(show);
}
void ApplicationLegacyCalls::ClearConsoleScreen()
{
    applicationKeeper_.ApplicationDiagnosticsUnit()->ClearConsoleScreen();
}
WORD ApplicationLegacyCalls::GetConsoleTextColorIndex(WORD *background)
{
    return applicationKeeper_.ApplicationDiagnosticsUnit()->GetConsoleTextColorIndex(background);
}
void ApplicationLegacyCalls::SetConsoleTextColor(WORD text, WORD background)
{
    applicationKeeper_.ApplicationDiagnosticsUnit()->SetConsoleTextColor(text, background);
}
void ApplicationLegacyCalls::ActivateCloseButton(bool active)
{
    applicationKeeper_.ApplicationDiagnosticsUnit()->ActivateCloseButton(active);
}
bool ApplicationLegacyCalls::IsActiveCloseButton()
{
    return applicationKeeper_.ApplicationDiagnosticsUnit()->IsActiveCloseButton();
}
bool ApplicationLegacyCalls::SaveConsoleScreenBuffer(const std::wstring &filename)
{
    return applicationKeeper_.ApplicationDiagnosticsUnit()->SaveConsoleScreenBuffer(filename);
}

#ifdef _WIN32

// Implementation for Windows
class CpuUsage::Impl
{
  public:
    Impl()
    {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        m_numProcessors = sysInfo.dwNumberOfProcessors;
        m_lastCheckTime = std::chrono::steady_clock::now();
        m_lastProcessTime = ~0ULL;
    }

    double GetUsage()
    {
        // Get the current process times
        FILETIME creationTime, exitTime, kernelTime, userTime;
        if (!GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime))
        {
            return 0.0; // Error
        }

        // Convert process times to ULONGLONG
        ULONGLONG currentKernelTime = FileTimeToULL(kernelTime);
        ULONGLONG currentUserTime = FileTimeToULL(userTime);
        ULONGLONG currentProcessTime = currentKernelTime + currentUserTime;

        // Get the current wall-clock time using std::chrono
        auto now = std::chrono::steady_clock::now();
        auto elapsedWallTime =
            std::chrono::duration_cast<std::chrono::microseconds>(now - m_lastCheckTime).count();

        // First call: Initialize
        if (m_lastProcessTime == ~0ULL)
        {
            m_lastProcessTime = currentProcessTime;
            m_lastCheckTime = now;
            return 0.0; // Not enough data to calculate usage
        }

        // FILETIME uses 100 ns ticks; elapsedWallTime uses microseconds.
        ULONGLONG processTimeElapsed = currentProcessTime - m_lastProcessTime;

        // Update last recorded times
        m_lastProcessTime = currentProcessTime;
        m_lastCheckTime = now;

        // Avoid division by zero
        if (elapsedWallTime == 0 || m_numProcessors == 0)
            return 0.0;

        // Calculate CPU usage as a percentage
        return std::max<double>(0.0,
                                (10.0 * processTimeElapsed) / (elapsedWallTime * m_numProcessors));
    }

  private:
    ULONGLONG m_lastProcessTime;
    DWORD m_numProcessors;
    std::chrono::steady_clock::time_point m_lastCheckTime;

    ULONGLONG FileTimeToULL(const FILETIME &ft)
    {
        return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    }
};

#else // ---- non-Windows ----------------------------------------------------

// POSIX implementation: process CPU time (user + system) from getrusage against
// wall-clock time, scaled by the processor count -- the same ratio the Windows
// path computes from GetProcessTimes.
class CpuUsage::Impl
{
  public:
    Impl()
        : m_numProcessors(std::max(1u, std::thread::hardware_concurrency())),
          m_lastCheckTime(std::chrono::steady_clock::now()),
          m_lastProcessTime(~0ULL) // sentinel: "no baseline yet" (0 is a valid CPU time)
    {
    }

    double GetUsage()
    {
        rusage ru{};
        if (getrusage(RUSAGE_SELF, &ru) != 0)
            return 0.0;

        const unsigned long long currentProcessTime =
            (static_cast<unsigned long long>(ru.ru_utime.tv_sec) +
             static_cast<unsigned long long>(ru.ru_stime.tv_sec)) *
                1000000ULL +
            static_cast<unsigned long long>(ru.ru_utime.tv_usec) +
            static_cast<unsigned long long>(ru.ru_stime.tv_usec);

        const auto now = std::chrono::steady_clock::now();
        const long long elapsedWallTime =
            std::chrono::duration_cast<std::chrono::microseconds>(now - m_lastCheckTime).count();

        // First call: seed the baselines, no usage yet.
        if (m_lastProcessTime == ~0ULL)
        {
            m_lastProcessTime = currentProcessTime;
            m_lastCheckTime = now;
            return 0.0;
        }

        const unsigned long long processTimeElapsed = currentProcessTime - m_lastProcessTime;
        m_lastProcessTime = currentProcessTime;
        m_lastCheckTime = now;

        if (elapsedWallTime <= 0 || m_numProcessors == 0)
            return 0.0;

        return std::max<double>(0.0, (100.0 * processTimeElapsed) /
                                         (static_cast<double>(elapsedWallTime) * m_numProcessors));
    }

  private:
    unsigned int m_numProcessors;
    std::chrono::steady_clock::time_point m_lastCheckTime;
    unsigned long long m_lastProcessTime;
};

#endif // _WIN32

// Constructor
CpuUsage::CpuUsage() : pImpl(std::make_unique<Impl>())
{
}

// Destructor
CpuUsage::~CpuUsage() = default;

// Public methods
double CpuUsage::GetUsage()
{
    return pImpl->GetUsage();
}

namespace FrameProfiler
{
Scope::Scope(ApplicationKeeper &keeper, Pass pass) noexcept
    : ApplicationLegacyCalls(keeper), m_pass(pass), m_t0(std::chrono::steady_clock::now())
{
}

Scope::~Scope()
{
    const auto elapsed = std::chrono::steady_clock::now() - m_t0;
    const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    AccumulatorMs(m_pass) += static_cast<float>(nanoseconds) / 1.0e6F;
}
} // namespace FrameProfiler

// Max UTF-8 bytes for a single log line. Source buffer is wchar_t[1024]; UTF-8 needs
// up to 3 bytes per BMP character (and 4 bytes per surrogate pair), so a 1024-wchar
// input expands to at most ~3072 bytes. 4096 gives comfortable margin.
constexpr int MAX_LOG_LINE_BYTES = 4096;
// Hex-dump line buffer. Output is pure ASCII (hex digits, spaces, colons, CRLF),
// so 1 byte per source wchar is sufficient with margin.
constexpr int MAX_HEX_LINE_BYTES = 512;

void CErrorReport::WriteDebugInfoStr(wchar_t *lpszToWrite)
{
    if (m_hFile == INVALID_HANDLE_VALUE)
        return;

    // Convert UTF-16 wide string to UTF-8 before writing. UTF-8 is portable across
    // locales (unlike CP_ACP, where the file's bytes depend on the writer's system
    // codepage -- Shift-JIS on JP Windows, Windows-1252 on EN, etc. -- making logs
    // collected from different machines ambiguous without knowing each user's locale).
    char narrowBuf[MAX_LOG_LINE_BYTES];
    int len = WideCharToMultiByte(CP_UTF8, 0, lpszToWrite, -1, narrowBuf, sizeof(narrowBuf),
                                  nullptr, nullptr);
    // len includes the null terminator. 0 = conversion failed (e.g. ERROR_INSUFFICIENT_BUFFER);
    // 1 = empty string (only null terminator). Either way, nothing to write.
    if (len <= 1)
        return;

    DWORD dwNumber;
    WriteFile(m_hFile, narrowBuf, len - 1, &dwNumber, NULL);
    if (dwNumber == 0)
    {
        CloseHandle(m_hFile);
        Create(m_lpszFileName);
    }
}

void CErrorReport::Write(const wchar_t *lpszFormat, ...)
{
    wchar_t lpszBuffer[1024] = {
        0,
    };
    va_list va;
    va_start(va, lpszFormat);
    vswprintf(lpszBuffer, 1024, lpszFormat, va);
    va_end(va);

    WriteDebugInfoStr(lpszBuffer);
}

void CErrorReport::HexWrite(void *pBuffer, int iSize)
{
    DWORD dwWritten = 0;
    wchar_t szLine[256] = {
        0,
    };
    char narrowLine[MAX_HEX_LINE_BYTES];
    int offset = 0;
    int len = 0;
    offset += mu_swprintf(szLine, L"0x%00000008X : ", (DWORD *)pBuffer);
    for (int i = 0; i < iSize; i++)
    {
        offset += mu_swprintf(szLine + offset, L"%02X", *((BYTE *)pBuffer + i));
        if (i > 0 && i < iSize - 1)
        {
            if (i % 16 == 15)
            { //. new line
                offset += mu_swprintf(szLine + offset, L"\r\n");
                len = WideCharToMultiByte(CP_UTF8, 0, szLine, -1, narrowLine, sizeof(narrowLine),
                                          nullptr, nullptr);
                if (len > 1)
                    WriteFile(m_hFile, narrowLine, len - 1, &dwWritten, NULL);
                offset = 0;
                offset += mu_swprintf(szLine + offset, L"           : ");
            }
            else if (i % 4 == 3)
            { //. space
                offset += mu_swprintf(szLine + offset, L" ");
            }
        }
    }
    offset += mu_swprintf(szLine + offset, L"\r\n");
    len = WideCharToMultiByte(CP_UTF8, 0, szLine, -1, narrowLine, sizeof(narrowLine), nullptr,
                              nullptr);
    if (len > 1)
        WriteFile(m_hFile, narrowLine, len - 1, &dwWritten, NULL);
}

void CErrorReport::AddSeparator(void)
{
    Write(
        L"-------------------------------------------------------------------------------------\r\n");
}

void CErrorReport::WriteLogBegin(void)
{
    Write(L"###### Log Begin ######\r\n");
}

void CErrorReport::WriteCurrentTime(BOOL bLineShift)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    Write(L"%4d/%02d/%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    if (bLineShift)
    {
        Write(L"\r\n");
    }
}

void CErrorReport::WriteSystemInfo(ER_SystemInfo *si)
{
    Write(L"<System information>\r\n");
    Write(L"OS \t\t\t: %ls\r\n", si->m_lpszOS);
    Write(L"CPU \t\t\t: %ls\r\n", si->m_lpszCPU);
    Write(L"RAM \t\t\t: %dMB\r\n", 1 + (si->m_iMemorySize / 1024 / 1024));
    AddSeparator();
    Write(L"Direct-X \t\t: %ls\r\n", si->m_lpszDxVersion);
}

void CErrorReport::WriteFontInfo(void)
{
    Write(L"<UI font>\r\n");
#ifdef _WIN32
    Write(L"Source\t\t: Win32 GDI (system fonts)\r\n");
#else
    // On non-Windows the font is discovered at runtime (fontconfig + fallbacks);
    // log what was found so a "no UI text" report is diagnosable. Paths are
    // ASCII, written through the %hs narrow conversion.
    const std::string diag = MuFontDiagnostics();
    if (diag.empty())
    {
        Write(L"(no font resolved)\r\n");
    }
    else
    {
        size_t pos = 0;
        while (pos < diag.size())
        {
            const size_t nl = diag.find('\n', pos);
            const std::string line =
                diag.substr(pos, (nl == std::string::npos ? diag.size() : nl) - pos);
            pos = (nl == std::string::npos) ? diag.size() : nl + 1;
            if (!line.empty())
                Write(L"%hs\r\n", line.c_str());
        }
        if (diag.find("NOT FOUND") != std::string::npos)
            Write(L"!! UI text is disabled - no usable font found. Install a "
                  L"sans-serif font or set MU_FONT.\r\n");
    }
#endif
}

// ---- Win32 crash-report system info -----------------------------------------
// IME / sound-card / OS / CPU / DirectX details for the crash log. These pull in
// DirectX and other Win32 APIs and are only invoked from the Windows entry point
// (Winmain), so guard the whole section off on non-Windows (issue #462).
#ifdef _WIN32

void CErrorReport::WriteImeInfo(HWND hWnd)
{
    wchar_t lpszTemp[256];
    Write(L"<IME information>\r\n");

    HIMC hImc = ImmGetContext(hWnd);
    if (hImc)
    {
        HKL hKl = GetKeyboardLayout(0);
        ImmGetDescription(hKl, lpszTemp, 256);
        Write(L"IME Name\t\t: %ls\r\n", lpszTemp);
        ImmGetIMEFileName(hKl, lpszTemp, 256);
        Write(L"IME File Name\t\t: %ls\r\n", lpszTemp);
        ImmReleaseContext(hWnd, hImc);
    }
    GetKeyboardLayoutName(lpszTemp);
    Write(L"Keyboard type\t\t: %ls\r\n", lpszTemp);
}

typedef struct tagER_SOUNDDEVICE
{
    wchar_t szGuid[64];
    wchar_t szDeviceName[128];
    wchar_t szDriverName[128];
} ER_SOUNDDEVICEINFO;

typedef struct tagSOUNDDEVICEENUM
{
    enum
    {
        MAX_DEVICENUM = 20
    };
    tagSOUNDDEVICEENUM()
    {
        nDeivceCount = 0;
    }
    ER_SOUNDDEVICEINFO infoSoundDevice[MAX_DEVICENUM];
    size_t nDeivceCount;

    tagER_SOUNDDEVICE &operator[](size_t p)
    {
        return infoSoundDevice[p];
    }
    tagER_SOUNDDEVICE &GetNextDevice()
    {
        return infoSoundDevice[nDeivceCount];
    }
} ER_SOUNDDEVICEENUMINFO;

INT_PTR CALLBACK DSoundEnumCallback(GUID *pGUID, LPWSTR strDesc, LPWSTR strDrvName, VOID *pContext)
{
    if (pGUID)
    {
        auto *pSoundDeviceEnumInfo = (ER_SOUNDDEVICEENUMINFO *)pContext;
        wcscpy(pSoundDeviceEnumInfo->GetNextDevice().szDeviceName, strDesc);
        wcscpy(pSoundDeviceEnumInfo->GetNextDevice().szDriverName, strDrvName);
        pSoundDeviceEnumInfo->nDeivceCount++;
    }
    return TRUE;
}

BOOL GetFileVersion(wchar_t *lpszFileName, WORD *pwVersion);

void CErrorReport::WriteSoundCardInfo(void)
{
    ER_SOUNDDEVICEENUMINFO sdi;
    DirectSoundEnumerate((LPDSENUMCALLBACK)DSoundEnumCallback, &sdi);

    if (sdi.nDeivceCount > 0)
    {
        Write(L"<Sound card information>\r\n");
    }
    else
    {
        Write(L"No sound card found.\r\n");
        return;
    }

    for (unsigned int i = 0; i < sdi.nDeivceCount; ++i)
    {
        Write(L"Sound Card \t\t: %ls\r\n", sdi.infoSoundDevice[i].szDeviceName);

        wchar_t lpszBuffer[MAX_PATH];
        GetSystemDirectory(lpszBuffer, MAX_PATH);
        wcscat(lpszBuffer, L"\\drivers\\");
        wcscat(lpszBuffer, sdi.infoSoundDevice[i].szDriverName);
        WORD wVersion[4];
        GetFileVersion(lpszBuffer, wVersion);

        Write(L"Sound Card Driver\t: %ls (%d.%d.%d.%d)\r\n", sdi.infoSoundDevice[i].szDriverName,
              wVersion[0], wVersion[1], wVersion[2], wVersion[3]);
    }

    AddSeparator();
}

void GetOSVersion(ER_SystemInfo *si)
{
    const wchar_t *lpszUnknown = L"Unknown";
    wchar_t lpszTemp[256];

    OSVERSIONINFO osiOne;
    osiOne.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    GetVersionEx(&osiOne);

    int iBuildNumberType = 0;
    mu_swprintf(si->m_lpszOS, L"%ls %d.%d ", lpszUnknown, osiOne.dwMajorVersion,
                osiOne.dwMinorVersion);

    switch (osiOne.dwMajorVersion)
    {
    case 3: // NT 3.51
        switch (osiOne.dwMinorVersion)
        {
        case 51:
            wcscpy(si->m_lpszOS, L"Windows NT 3.51");
            break;
        }
        break;
    case 4:
        switch (osiOne.dwMinorVersion)
        {
        case 0:
            switch (osiOne.dwPlatformId)
            {
            case VER_PLATFORM_WIN32_WINDOWS:
                wcscpy(si->m_lpszOS, L"Windows 95 ");
                if (osiOne.szCSDVersion[1] == 'C' || osiOne.szCSDVersion[1] == 'B')
                {
                    wcscat(si->m_lpszOS, L"OSR2");
                }
                iBuildNumberType = 1;
                break;
            case VER_PLATFORM_WIN32_NT:
                wcscpy(si->m_lpszOS, L"Windows NT 4.0 ");
                break;
            }
            break;
        case 10:
            wcscpy(si->m_lpszOS, L"Windows 98 ");
            if (osiOne.szCSDVersion[1] == 'A')
            {
                wcscat(si->m_lpszOS, L"SE ");
            }
            iBuildNumberType = 1;
            break;
        case 90:
            wcscpy(si->m_lpszOS, L"Windows Me ");
            iBuildNumberType = 1;
            break;
        }
        break;
    case 5:
        switch (osiOne.dwMinorVersion)
        {
        case 0:
            wcscpy(si->m_lpszOS, L"Windows 2000 ");
            {
                HKEY hKey;
                DWORD dwBufLen;
                if (ERROR_SUCCESS ==
                    RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                                 L"SYSTEM\\CurrentControlSet\\Control\\ProductOptions", 0,
                                 KEY_QUERY_VALUE, &hKey))
                {
                    if (ERROR_SUCCESS == RegQueryValueEx(hKey, L"ProductType", NULL, NULL,
                                                         (LPBYTE)lpszTemp, &dwBufLen))
                    {
                        if (0 == lstrcmpi(L"WINNT", lpszTemp))
                        {
                            wcscat(si->m_lpszOS, L"Professional ");
                        }
                        if (0 == lstrcmpi(L"LANMANNT", lpszTemp))
                        {
                            wcscat(si->m_lpszOS, L"Server ");
                        }
                        if (0 == lstrcmpi(L"SERVERNT", lpszTemp))
                        {
                            wcscat(si->m_lpszOS, L"Advanced Server ");
                        }
                    }

                    RegCloseKey(hKey);
                }
            }
            break;
        case 1:
            wcscpy(si->m_lpszOS, L"Windows XP ");
            break;
        case 2:
            wcscpy(si->m_lpszOS, L"Windows 2003 family ");
            break;
        }
        break;
    }
    switch (iBuildNumberType)
    {
    case 0:
        mu_swprintf(lpszTemp, L"Build %d ", osiOne.dwBuildNumber);
        break;
    case 1:
        mu_swprintf(lpszTemp, L"Build %d.%d.%d ", HIBYTE(HIWORD(osiOne.dwBuildNumber)),
                    LOBYTE(HIWORD(osiOne.dwBuildNumber)), LOWORD(osiOne.dwBuildNumber));
        break;
    }
    wcscat(si->m_lpszOS, lpszTemp);
    mu_swprintf(lpszTemp, L"(%ls)", osiOne.szCSDVersion);
    wcscat(si->m_lpszOS, lpszTemp);
}

// NOTE:
// The original implementation of GetCPUFrequency and GetCPUInfo relied on
// MSVC inline assembly (cpuid/rdtsc) and 32-bit affinity mask types, which
// are not portable and fail to compile under MinGW/GCC. For the purposes of
// error reporting on this toolchain, a simplified, portable implementation
// is sufficient.

__int64 GetCPUFrequency(unsigned int uiMeasureMSecs)
{
    (void)uiMeasureMSecs; // unused on this platform

    // High‑resolution CPU frequency measurement is handled elsewhere
    // (see Utilities/CpuUsage.cpp). Here we return 0 to indicate that
    // a specific CPU MHz value is not available in this build.
    return 0;
}

void GetCPUInfo(ER_SystemInfo *si)
{
    if (si == nullptr || si->m_lpszCPU == nullptr)
        return;

    wcscpy(si->m_lpszCPU, L"Unknown CPU");
}

typedef HRESULT(WINAPI *DIRECTDRAWCREATE)(GUID *, LPDIRECTDRAW *, IUnknown *);
typedef HRESULT(WINAPI *DIRECTDRAWCREATEEX)(GUID *, VOID **, REFIID, IUnknown *);
typedef HRESULT(WINAPI *DIRECTINPUTCREATE)(HINSTANCE, DWORD, LPDIRECTINPUT *, IUnknown *);

DWORD GetDXVersion()
{
    DIRECTDRAWCREATE DirectDrawCreate = NULL;
    DIRECTDRAWCREATEEX DirectDrawCreateEx = NULL;
    DIRECTINPUTCREATE DirectInputCreate = NULL;
    HINSTANCE hDDrawDLL = NULL;
    HINSTANCE hDInputDLL = NULL;
    HINSTANCE hD3D8DLL = NULL;
    HINSTANCE hD3D9DLL = NULL;
    LPDIRECTDRAW pDDraw = NULL;
    LPDIRECTDRAW2 pDDraw2 = NULL;
    LPDIRECTDRAWSURFACE pSurf = NULL;
    LPDIRECTDRAWSURFACE3 pSurf3 = NULL;
    LPDIRECTDRAWSURFACE4 pSurf4 = NULL;
    DWORD dwDXVersion = 0;
    HRESULT hr;

    // First see if DDRAW.DLL even exists.
    hDDrawDLL = LoadLibrary(L"DDRAW.DLL");
    if (hDDrawDLL == NULL)
    {
        dwDXVersion = 0;
        return dwDXVersion;
    }

    // See if we can create the DirectDraw object.
    DirectDrawCreate = (DIRECTDRAWCREATE)GetProcAddress(hDDrawDLL, "DirectDrawCreate");
    if (DirectDrawCreate == NULL)
    {
        dwDXVersion = 0;
        FreeLibrary(hDDrawDLL);

        __TraceF(TEXT("===> Couldn't LoadLibrary DDraw\r\n"));
        return dwDXVersion;
    }

    hr = DirectDrawCreate(NULL, &pDDraw, NULL);
    if (FAILED(hr))
    {
        dwDXVersion = 0;
        FreeLibrary(hDDrawDLL);
        __TraceF(TEXT("===> Couldn't create DDraw\r\n"));
        return dwDXVersion;
    }

    // So DirectDraw exists.  We are at least DX1.
    dwDXVersion = 0x100;

    // Let's see if IID_IDirectDraw2 exists.
    hr = pDDraw->QueryInterface(IID_IDirectDraw2, (VOID **)&pDDraw2);
    if (FAILED(hr))
    {
        // No IDirectDraw2 exists... must be DX1
        pDDraw->Release();
        FreeLibrary(hDDrawDLL);
        __TraceF(TEXT("===> Couldn't QI DDraw2\r\n"));
        return dwDXVersion;
    }

    // IDirectDraw2 exists. We must be at least DX2
    pDDraw2->Release();
    dwDXVersion = 0x200;

    // DirectX 3.0 Checks

    // DirectInput was added for DX3
    hDInputDLL = LoadLibrary(L"DINPUT.DLL");
    if (hDInputDLL == NULL)
    {
        // No DInput... must not be DX3
        __TraceF(TEXT("===> Couldn't LoadLibrary DInput\r\n"));
        pDDraw->Release();
        return dwDXVersion;
    }

    DirectInputCreate = (DIRECTINPUTCREATE)GetProcAddress(hDInputDLL, "DirectInputCreateA");
    if (DirectInputCreate == NULL)
    {
        // No DInput... must be DX2
        FreeLibrary(hDInputDLL);
        FreeLibrary(hDDrawDLL);
        pDDraw->Release();
        __TraceF(TEXT("===> Couldn't GetProcAddress DInputCreate\r\n"));
        return dwDXVersion;
    }

    // DirectInputCreate exists. We are at least DX3
    dwDXVersion = 0x300;
    FreeLibrary(hDInputDLL);

    // Can do checks for 3a vs 3b here

    // DirectX 5.0 Checks

    // We can tell if DX5 is present by checking for the existence of
    // IDirectDrawSurface3. First, we need a surface to QI off of.
    DDSURFACEDESC ddsd;
    ZeroMemory(&ddsd, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

    hr = pDDraw->SetCooperativeLevel(NULL, DDSCL_NORMAL);
    if (FAILED(hr))
    {
        // Failure. This means DDraw isn't properly installed.
        pDDraw->Release();
        FreeLibrary(hDDrawDLL);
        dwDXVersion = 0;
        __TraceF(TEXT("===> Couldn't Set coop level\r\n"));
        return dwDXVersion;
    }

    hr = pDDraw->CreateSurface(&ddsd, &pSurf, NULL);
    if (FAILED(hr))
    {
        // Failure. This means DDraw isn't properly installed.
        pDDraw->Release();
        FreeLibrary(hDDrawDLL);
        dwDXVersion = 0;
        __TraceF(TEXT("===> Couldn't CreateSurface\r\n"));
        return dwDXVersion;
    }

    // Query for the IDirectDrawSurface3 interface
    if (FAILED(pSurf->QueryInterface(IID_IDirectDrawSurface3, (VOID **)&pSurf3)))
    {
        pDDraw->Release();
        FreeLibrary(hDDrawDLL);
        return dwDXVersion;
    }

    // QI for IDirectDrawSurface3 succeeded. We must be at least DX5
    dwDXVersion = 0x500;

    // DirectX 6.0 Checks

    // The IDirectDrawSurface4 interface was introduced with DX 6.0
    if (FAILED(pSurf->QueryInterface(IID_IDirectDrawSurface4, (VOID **)&pSurf4)))
    {
        pDDraw->Release();
        FreeLibrary(hDDrawDLL);
        return dwDXVersion;
    }

    // IDirectDrawSurface4 was create successfully. We must be at least DX6
    dwDXVersion = 0x600;
    pSurf->Release();
    pDDraw->Release();

    // DirectX 6.1 Checks

    // Check for DMusic, which was introduced with DX6.1
    LPDIRECTMUSIC pDMusic = NULL;
    CoInitialize(NULL);
    hr = CoCreateInstance(CLSID_DirectMusic, NULL, CLSCTX_INPROC_SERVER, IID_IDirectMusic,
                          (VOID **)&pDMusic);
    if (FAILED(hr))
    {
        __TraceF(TEXT("===> Couldn't create CLSID_DirectMusic\r\n"));
        FreeLibrary(hDDrawDLL);
        return dwDXVersion;
    }

    // DirectMusic was created successfully. We must be at least DX6.1
    dwDXVersion = 0x601;
    pDMusic->Release();
    CoUninitialize();

    // DirectX 7.0 Checks

    // Check for DirectX 7 by creating a DDraw7 object
    LPDIRECTDRAW7 pDD7;
    DirectDrawCreateEx = (DIRECTDRAWCREATEEX)GetProcAddress(hDDrawDLL, "DirectDrawCreateEx");
    if (NULL == DirectDrawCreateEx)
    {
        FreeLibrary(hDDrawDLL);
        return dwDXVersion;
    }

    if (FAILED(DirectDrawCreateEx(NULL, (VOID **)&pDD7, IID_IDirectDraw7, NULL)))
    {
        FreeLibrary(hDDrawDLL);
        return dwDXVersion;
    }

    // DDraw7 was created successfully. We must be at least DX7.0
    dwDXVersion = 0x700;
    pDD7->Release();

    // DirectX 8.0 Checks

    // Simply see if D3D8.dll exists.
    hD3D8DLL = LoadLibrary(L"D3D8.DLL");
    if (hD3D8DLL == NULL)
    {
        FreeLibrary(hDDrawDLL);
        return dwDXVersion;
    }

    // D3D8.dll exists. We must be at least DX8.0
    dwDXVersion = 0x800;

    // DirectX 9.0 Checks
    hD3D9DLL = LoadLibrary(L"D3D9.DLL");
    if (hD3D9DLL == NULL)
    {
        FreeLibrary(hDDrawDLL);
        FreeLibrary(hD3D8DLL);
        return dwDXVersion;
    }
    dwDXVersion = 0x900;

    // End of checking for versions of DirectX

    // Close open libraries and return
    FreeLibrary(hDDrawDLL);
    FreeLibrary(hD3D8DLL);
    FreeLibrary(hD3D9DLL);

    return dwDXVersion;
}

void GetSystemInfo(ER_SystemInfo *si)
{
    ZeroMemory(si, sizeof(ER_SystemInfo));

    // CPU
    GetCPUInfo(si);

    // Memory
    MEMORYSTATUS ms;
    ms.dwLength = sizeof(MEMORYSTATUS);
    GlobalMemoryStatus(&ms);
    si->m_iMemorySize = ms.dwTotalPhys;

    // OS
    GetOSVersion(si);

    // DX
    DWORD dwDX = GetDXVersion();
    mu_swprintf(si->m_lpszDxVersion, L"Direct-X %d.%d", dwDX >> 8, dwDX & 0xFF);
}

#else  // ---- non-Windows ----------------------------------------------------

// IME is a Windows input service; there is nothing to report elsewhere.
void CErrorReport::WriteImeInfo(HWND /*hWnd*/)
{
}

// Audio runs through SDL; the DirectSound device enumeration has no equivalent.
void CErrorReport::WriteSoundCardInfo(void)
{
    Write(L"<Sound device information>\r\n");
    Write(L"Description \t\t: SDL audio\r\n");
}

void GetSystemInfo(ER_SystemInfo *si)
{
    ZeroMemory(si, sizeof(ER_SystemInfo));

    // CPU: the first "model name" entry of /proc/cpuinfo.
    mu_swprintf(si->m_lpszCPU, L"Unknown");
    if (FILE *f = std::fopen("/proc/cpuinfo", "r"))
    {
        char line[256];
        while (std::fgets(line, sizeof(line), f))
        {
            if (std::strncmp(line, "model name", 10) != 0)
                continue;
            const char *value = std::strchr(line, ':');
            if (value)
            {
                ++value;
                while (*value == ' ' || *value == '\t')
                    ++value;
                char name[MAX_LENGTH_CPUNAME] = {0};
                std::strncpy(name, value, sizeof(name) - 1);
                if (char *nl = std::strchr(name, '\n'))
                    *nl = '\0';
                MultiByteToWideChar(CP_UTF8, 0, name, -1, si->m_lpszCPU, MAX_LENGTH_CPUNAME);
            }
            break;
        }
        std::fclose(f);
    }

    // Memory: physical RAM in bytes, clamped like the DWORD->int path on Windows.
    const long long pages = ::sysconf(_SC_PHYS_PAGES);
    const long long pageSize = ::sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0)
    {
        const long long bytes = pages * pageSize;
        si->m_iMemorySize = (bytes > INT_MAX) ? INT_MAX : static_cast<int>(bytes);
    }

    // OS: distro (if any) plus the full kernel name and release, e.g.
    // "Ubuntu 24.04.3 LTS (Linux 6.18.33-...)". The distro tells a dev which
    // distribution the report came from; the full release keeps WSL/variant
    // suffixes that uname carries.
    struct utsname un{};
    if (::uname(&un) == 0)
    {
        char kernel[MAX_LENGTH_OSINFO] = {0};
        std::snprintf(kernel, sizeof(kernel), "%s %s", un.sysname, un.release);
        wchar_t kernelW[MAX_LENGTH_OSINFO] = {0};
        MultiByteToWideChar(CP_UTF8, 0, kernel, -1, kernelW, MAX_LENGTH_OSINFO);

        const std::wstring distro = Core::Platform::GetOSDistroName();
        const std::wstring osLine =
            distro.empty() ? std::wstring(kernelW) : distro + L" (" + kernelW + L")";
        wcsncpy(si->m_lpszOS, osLine.c_str(), MAX_LENGTH_OSINFO - 1);
        si->m_lpszOS[MAX_LENGTH_OSINFO - 1] = L'\0';
    }
    else
    {
        mu_swprintf(si->m_lpszOS, L"Unknown");
    }

    // No DirectX off Windows; rendering is OpenGL.
    mu_swprintf(si->m_lpszDxVersion, L"none (OpenGL)");
}
#endif // _WIN32 (Win32 crash-report system info)

CErrorReport::CErrorReport()
{
    Clear();
    Create(L"MuError.log");
}

CErrorReport::~CErrorReport()
{
    Destroy();
}

void CErrorReport::Clear(void)
{
    m_hFile = INVALID_HANDLE_VALUE;
    m_lpszFileName[0] = '\0';
    m_iKey = 0;
}

void CErrorReport::Create(const wchar_t *lpszFileName)
{
    wcscpy(m_lpszFileName, lpszFileName);
    m_iKey = 0;
    m_hFile = CreateFile(m_lpszFileName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    CutHead();
    SetFilePointer(m_hFile, 0, NULL, FILE_END);
}

void CErrorReport::Destroy(void)
{
    CloseHandle(m_hFile);
    Clear();
}

void CErrorReport::CutHead(void)
{
    DWORD dwNumber;
    char lpszBuffer[128 * 1024];
    ReadFile(m_hFile, lpszBuffer, sizeof(lpszBuffer) - 1, &dwNumber, NULL);
    lpszBuffer[dwNumber] = '\0';
    char *lpCut = CheckHeadToCut(lpszBuffer, dwNumber);
    if (dwNumber >= 32 * 1024 - 1)
    {
        lpCut = &lpszBuffer[32 * 1024 - 1];
    }
    if (lpCut != lpszBuffer)
    {
        CloseHandle(m_hFile);
        DeleteFile(m_lpszFileName);
        m_hFile = CreateFile(m_lpszFileName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD dwSize = dwNumber - static_cast<DWORD>(lpCut - lpszBuffer);
        m_iKey = 0;
        WriteFile(m_hFile, lpCut, dwSize, &dwNumber, NULL);
    }
}

char *CErrorReport::CheckHeadToCut(char *lpszBuffer, DWORD dwNumber)
{
    (void)dwNumber;
    const char *lpszBegin = "###### Log Begin ######";
    int iLengthOfBegin = static_cast<int>(strlen(lpszBegin));

    char *lpFoundList[128];
    int iFoundCount = 0;
    for (char *lpFind = lpszBuffer; lpFind && *lpFind;)
    {
        lpFind = strchr(lpFind, '#');
        if (lpFind)
        {
            if (0 == strncmp(lpFind, lpszBegin, iLengthOfBegin))
            {
                lpFoundList[iFoundCount++] = lpFind;
                lpFind += iLengthOfBegin;
            }
            else
            {
                ++lpFind;
            }
        }
    }

    return iFoundCount >= 5 ? lpFoundList[iFoundCount - 4] : lpszBuffer;
}

BOOL CErrorReport::WriteFile(HANDLE hFile, void *lpBuffer, DWORD nNumberOfBytesToWrite,
                             LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)
{
    return ::WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten,
                       lpOverlapped);
}

#ifdef _WIN32

//. class CConsoleWindow
using namespace leaf;

CConsoleWindow::CConsoleWindow()
{
    m_hWnd = NULL;
    m_bActiveCloseButton = false;
    m_started = false;

    m_LimitTimer.SetTimer(12000); //. 12��
}
CConsoleWindow::~CConsoleWindow()
{
}

bool CConsoleWindow::Open(const std::wstring &title, CTimer2::StartTickTime &startTickTime)
{
    Close();
    if (FALSE == ::AllocConsole())
        return false;
    m_started = true;

    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);

    ::SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_EXTENDED_FLAGS | ENABLE_LINE_INPUT |
                                                         ENABLE_ECHO_INPUT |
                                                         ENABLE_PROCESSED_INPUT);
    ::SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE),
                     ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);
    ::SetConsoleMode(GetStdHandle(STD_ERROR_HANDLE),
                     ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);

    while (!GetWndHandle())
    {
        m_LimitTimer.UpdateTime(startTickTime);
        if (m_LimitTimer.IsTime() || FALSE == ::EnumChildWindows(NULL, EnumChildProc, (LPARAM)this))
            break;
        ::Sleep(500);
    }

    m_bActiveCloseButton = true;

    return true;
}
void CConsoleWindow::Close()
{
    if (!m_started)
    {
        m_hWnd = NULL;
        return;
    }

    fclose(stdin);
    fclose(stdout);
    fclose(stderr);

    FreeConsole();

    m_hWnd = NULL;
    m_started = false;
}

bool CConsoleWindow::SetTitle(const std::wstring &title)
{
    if (FALSE == ::SetConsoleTitle(title.c_str()))
        return false;
    m_title = title;
    return true;
}
const std::wstring &CConsoleWindow::GetTitle()
{
    wchar_t szConsoleTile[1024] = {
        0,
    };
    ::GetConsoleTitle(szConsoleTile, 1024);

    m_title = szConsoleTile;
    return m_title;
}

HWND CConsoleWindow::GetWndHandle()
{
    return m_hWnd;
}

bool CConsoleWindow::IsVisible()
{
    return GetWndHandle() && ::IsWindowVisible(GetWndHandle()) ? true : false;
}
void CConsoleWindow::Show(bool bShow)
{
    if (m_hWnd)
    {
        BOOL bResult = FALSE;
        m_LimitTimer.ResetTimer();
        while (!bResult)
        {
            ::SetLastError(0);
            ::ShowWindow(m_hWnd, bShow ? SW_SHOW : SW_HIDE);
            ::UpdateWindow(m_hWnd);
            bResult = ::GetLastError() ? FALSE : TRUE;
            Sleep(10);
        }
    }
}

void CConsoleWindow::ClearScreen()
{
    /***************************************/
    // This code is from one of Microsoft's
    // knowledge base articles, you can find it at
    // http://support.microsoft.com/default.aspx?scid=KB;EN-US;q99261&
    /***************************************/

    COORD coordScreen = {0, 0};

    DWORD cCharsWritten;
    CONSOLE_SCREEN_BUFFER_INFO csbi; /* to get buffer info */
    DWORD dwConSize;

    /* get the number of character cells in the current buffer */
    ::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &csbi);

    dwConSize = csbi.dwSize.X * csbi.dwSize.Y;

    /* fill the entire screen with blanks */
    ::FillConsoleOutputCharacter(::GetStdHandle(STD_OUTPUT_HANDLE), (TCHAR)' ', dwConSize,
                                 coordScreen, &cCharsWritten);

    /* get the current text attribute */
    ::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &csbi);

    /* now set the buffer's attributes accordingly */
    ::FillConsoleOutputAttribute(::GetStdHandle(STD_OUTPUT_HANDLE), csbi.wAttributes, dwConSize,
                                 coordScreen, &cCharsWritten);

    /* put the cursor at (0, 0) */
    ::SetConsoleCursorPosition(::GetStdHandle(STD_OUTPUT_HANDLE), coordScreen);
}

WORD CConsoleWindow::GetTextColorIndex(WORD *pwBgColorIndex)
{
    CONSOLE_SCREEN_BUFFER_INFO ConScreenBufInfo;
    if (FALSE == ::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &ConScreenBufInfo))
        return 0xFFFF;

    WORD wTextColorIndex = ConScreenBufInfo.wAttributes & 0x000F;
    if (pwBgColorIndex)
    {
        *pwBgColorIndex = wTextColorIndex >> 4;
    }
    return wTextColorIndex;
}
void CConsoleWindow::SetTextColor(WORD wTextColorIndex, WORD wBgColorIndex)
{
    WORD wColorAttr = (wBgColorIndex << 4) & 0xF0;
    wColorAttr |= wTextColorIndex;

    ::SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), wColorAttr);
}

void CConsoleWindow::ActivateCloseButton(bool bActive)
{
    if (bActive && (m_hWnd != NULL))
    {
        // enable the [x] button if we found our console
        ::GetSystemMenu(m_hWnd, TRUE);
        ::DrawMenuBar(m_hWnd);
        m_bActiveCloseButton = true;
    }
    else if (m_hWnd != NULL)
    {
        // disable the [x] button if we found our console
        HMENU hMenu = ::GetSystemMenu(m_hWnd, FALSE);
        if (hMenu != NULL)
        {
            ::RemoveMenu(hMenu, SC_CLOSE, MF_BYCOMMAND);
            ::DrawMenuBar(m_hWnd);
            m_bActiveCloseButton = false;
        }
    }
}
bool CConsoleWindow::IsActiveCloseButton() const
{
    return m_bActiveCloseButton;
}

bool CConsoleWindow::SaveScreenBuffer(const std::wstring &filename)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi; /* to get buffer info */

    /* get the number of character cells in the current buffer */
    ::GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);

    COORD BufferSize;
    BufferSize.X = csbi.dwSize.X;
    BufferSize.Y = csbi.dwCursorPosition.Y + 1;

    auto *pbyCharBuffer = new CHAR_INFO[BufferSize.X * BufferSize.Y];

    COORD StartPointToWrite = {0, 0};
    SMALL_RECT RectToRead = {0, 0, BufferSize.X, BufferSize.Y};
    if (ReadConsoleOutput(GetStdHandle(STD_OUTPUT_HANDLE), pbyCharBuffer, BufferSize,
                          StartPointToWrite, &RectToRead))
    {
    }

    delete[] pbyCharBuffer;

    return true;
}

void CConsoleWindow::SetWndHandle(HWND hWnd)
{
    m_hWnd = hWnd;
}
DWORD CConsoleWindow::Get32ColorFromColorIndex(WORD wColorIndex)
{
    DWORD dw32Color = 0;
    if ((wColorIndex & leaf::COLOR_RED) == leaf::COLOR_RED)
        dw32Color |= 0x007F0000;
    if ((wColorIndex & leaf::COLOR_GREEN) == leaf::COLOR_GREEN)
        dw32Color |= 0x00007F00;
    if ((wColorIndex & leaf::COLOR_BLUE) == leaf::COLOR_BLUE)
        dw32Color |= 0x0000007F;
    if ((wColorIndex & leaf::COLOR_INTENSITY) == leaf::COLOR_INTENSITY)
    {
        if ((dw32Color & 0x7F0000) == 0x7F0000)
            dw32Color |= 0x800000;
        if ((dw32Color & 0x007F00) == 0x007F00)
            dw32Color |= 0x008000;
        if ((dw32Color & 0x00007F) == 0x00007F)
            dw32Color |= 0x000080;
    }
    return dw32Color;
}
BOOL CALLBACK CConsoleWindow::EnumChildProc(HWND hWnd, LPARAM lParam)
{
    auto *pConsoleWnd = (CConsoleWindow *)(lParam);
    DWORD dwProcessId = 0;
    if (GetWindowThreadProcessId(hWnd, &dwProcessId) == GetCurrentThreadId() &&
        dwProcessId == GetCurrentProcessId())
    {
        wchar_t szClassName[512] = {
            0,
        };
        GetClassName(hWnd, szClassName, 512);
        if (0 == wcscmp(szClassName, L"ConsoleWindowClass"))
        {
            if (!pConsoleWnd->GetWndHandle())
                pConsoleWnd->SetWndHandle(hWnd);
            return FALSE;
        }
    }
    return TRUE;
}

#else // ---- non-Windows ----------------------------------------------------

namespace leaf
{

CConsoleWindow::CConsoleWindow() : m_hWnd(nullptr), m_bActiveCloseButton(false), m_started(false)
{
}
CConsoleWindow::~CConsoleWindow() = default;
bool CConsoleWindow::Open(const std::wstring &, CTimer2::StartTickTime &)
{
    m_started = true;
    return true;
}
void CConsoleWindow::Close()
{
    m_started = false;
}
bool CConsoleWindow::SetTitle(const std::wstring &title)
{
    m_title = title;
    return true;
}
const std::wstring &CConsoleWindow::GetTitle()
{
    return m_title;
}
HWND CConsoleWindow::GetWndHandle()
{
    return nullptr;
}
bool CConsoleWindow::IsVisible()
{
    return false;
}
void CConsoleWindow::Show(bool)
{
}
void CConsoleWindow::ClearScreen()
{
}
WORD CConsoleWindow::GetTextColorIndex(WORD *background)
{
    if (background != nullptr)
        *background = 0;
    return 0;
}
void CConsoleWindow::SetTextColor(WORD, WORD)
{
}
void CConsoleWindow::ActivateCloseButton(bool active)
{
    m_bActiveCloseButton = active;
}
bool CConsoleWindow::IsActiveCloseButton() const
{
    return m_bActiveCloseButton;
}
bool CConsoleWindow::SaveScreenBuffer(const std::wstring &)
{
    return true;
}
void CConsoleWindow::SetWndHandle(HWND window)
{
    m_hWnd = window;
}
DWORD CConsoleWindow::Get32ColorFromColorIndex(WORD)
{
    return 0;
}
BOOL CALLBACK CConsoleWindow::EnumChildProc(HWND, LPARAM)
{
    return TRUE;
}

} // namespace leaf

#endif // _WIN32

void ApplicationDiagnostics::InitializeConsole()
{
    consoleDebug_.Initialize();
}

void CmuConsoleDebug::Initialize()
{
#ifdef CSK_LH_DEBUG_CONSOLE
    if (diagnostics_->OpenConsoleWindow(L"Mu Debug Console Window"))
    {
        diagnostics_->ActivateCloseButton(false);
        diagnostics_->ShowConsole(true);
        m_bInit = true;

        diagnostics_->ErrorReport().Write(
            L"Mu Debug Console Window Init - completed(Handle:0x%00000008X)\r\n",
            diagnostics_->GetConsoleWndHandle());
    }
#endif
}

void CmuConsoleDebug::UpdateMainScene()
{
#ifdef CSK_LH_DEBUG_CONSOLE
    if (m_bInit)
    {
        if (IsPress(VK_SHIFT) == TRUE)
        {
            if (PressKey(VK_F7))
            {
                diagnostics_->ShowConsole(!diagnostics_->IsConsoleVisible());
            }
        }
    }
#endif
}

bool CmuConsoleDebug::CheckFrameTimingCommand(const std::wstring &command)
{
    if (command == L"$vsync on" || command == L"$vsync off")
    {
        if (command == L"$vsync on")
            EnableVSync();
        else
            DisableVSync();
        ResetFrameStats();
        return true;
    }
    if (!command.starts_with(L"$fps") ||
        (command.size() > 4 && !std::iswspace(command[4])))
        return false;

    const wchar_t *argument = command.c_str() + 4;
    wchar_t *end = nullptr;
    const float fps = std::wcstof(argument, &end);
    const bool parsed = end != argument;
    while (std::iswspace(*end))
        ++end;
    if (!parsed || *end != L'\0' || !std::isfinite(fps) || (fps < 0.0F && fps != -1.0F))
    {
        Write(MCD_ERROR, L"Usage: $fps <positive number>, or $fps 0 for unlimited.");
        return true;
    }
    SetTargetFps(fps);
    ResetFrameStats();
    return true;
}

bool CmuConsoleDebug::CheckCommand(const std::wstring &strCommand)
{
    if (strCommand.compare(L"$fpscounter on") == 0)
    {
        SetShowFpsCounter(true);
        return true;
    }
    else if (strCommand.compare(L"$fpscounter off") == 0)
    {
        SetShowFpsCounter(false);
        return true;
    }
    else if (strCommand.compare(L"$details on") == 0)
    {
        SetShowDebugInfo(true);
        return true;
    }
    else if (strCommand.compare(L"$details off") == 0)
    {
        SetShowDebugInfo(false);
        return true;
    }
    else if (CheckFrameTimingCommand(strCommand))
    {
        return true;
    }
    else if (strCommand.compare(0, 7, L"$winmsg") == 0)
    {
        auto str_limit = strCommand.substr(8);
        auto message_limit = std::stof(str_limit);
        diagnostics_->SetMaxMessagePerCycle(static_cast<int>(message_limit));
        return true;
    }

#ifdef CSK_LH_DEBUG_CONSOLE
    if (!m_bInit)
        return false;

    if (strCommand.compare(L"$open") == NULL)
    {
        diagnostics_->ShowConsole(true);
        return true;
    }
    else if (strCommand.compare(L"$close") == NULL)
    {
        diagnostics_->ShowConsole(false);
        return true;
    }
    else if (strCommand.compare(L"$clear") == NULL)
    {
        diagnostics_->SetConsoleTextColor();
        diagnostics_->ClearConsoleScreen();
        return true;
    }
#ifdef CSK_DEBUG_MAP_PATHFINDING
    else if (strCommand.compare(L"$path on") == NULL)
    {
        g_bShowPath = true;
    }
    else if (strCommand.compare(L"$path off") == NULL)
    {
        g_bShowPath = false;
    }
#endif // CSK_DEBUG_MAP_PATHFINDING
#ifdef CSK_DEBUG_RENDER_BOUNDINGBOX
    else if (strCommand.compare(L"$bb on") == NULL)
    {
        diagnostics_->Frame().SetRenderBoundingBox(true);
    }
    else if (strCommand.compare(L"$bb off") == NULL)
    {
        diagnostics_->Frame().SetRenderBoundingBox(false);
    }
#endif // CSK_DEBUG_RENDER_BOUNDINGBOX
    else if (strCommand.compare(L"$type_test") == NULL)
    {
        Write(MCD_SEND, L"MCD_SEND");
        Write(MCD_RECEIVE, L"MCD_RECEIVE");
        Write(MCD_ERROR, L"MCD_ERROR");
        Write(MCD_NORMAL, L"MCD_NORMAL");
        return true;
    }
    else if (strCommand.compare(L"$texture_info") == NULL)
    {
        Write(MCD_NORMAL, L"Texture Number : %d", Bitmaps.GetNumberOfTexture());
        Write(MCD_NORMAL, L"Texture Memory : %dKB", Bitmaps.GetUsedTextureMemory() / 1024);
        return true;
    }
    else if (strCommand.compare(L"$color_test") == NULL)
    {
        diagnostics_->SetConsoleTextColor(leaf::COLOR_DARKRED);
        std::cout << "color test: dark red" << std::endl;
        diagnostics_->SetConsoleTextColor(leaf::COLOR_DARKGREEN);
        std::cout << "color test: dark green" << std::endl;
        diagnostics_->SetConsoleTextColor(leaf::COLOR_DARKBLUE);
        std::cout << "color test: dark blue" << std::endl;
        diagnostics_->SetConsoleTextColor(leaf::COLOR_RED);
        std::cout << "color test: red" << std::endl;
        diagnostics_->SetConsoleTextColor(leaf::COLOR_GREEN);
        std::cout << "color test: green" << std::endl;
        diagnostics_->SetConsoleTextColor(leaf::COLOR_BLUE);
        std::cout << "color test: blue" << std::endl;
        diagnostics_->SetConsoleTextColor(leaf::COLOR_OLIVE);
        std::cout << "color test: olive" << std::endl;
        diagnostics_->SetConsoleTextColor(leaf::COLOR_PURPLE);
        std::cout << "color test: purple" << std::endl;
        diagnostics_->SetConsoleTextColor(leaf::COLOR_TEAL);
        std::cout << "color test: teal" << std::endl;
        diagnostics_->SetConsoleTextColor(leaf::COLOR_GRAY);
        std::cout << "color test: gray" << std::endl;
        diagnostics_->SetConsoleTextColor(leaf::COLOR_AQUA);
        std::cout << "color test: aqua" << std::endl;
        diagnostics_->SetConsoleTextColor(leaf::COLOR_FUCHSIA);
        std::cout << "color test: fuchsia" << std::endl;
        diagnostics_->SetConsoleTextColor(leaf::COLOR_YELLOW);
        std::cout << "color test: yellow" << std::endl;
        diagnostics_->SetConsoleTextColor(leaf::COLOR_WHITE);
        std::cout << "color test: white" << std::endl;
        return true;
    }
#endif
    return false;
}

void CmuConsoleDebug::Write(int iType, const wchar_t *pStr, ...)
{
    // MCD_ERROR is always logged to MuError.log, regardless of CONSOLE_DEBUG.
    // Other log levels remain debug-only so they don't spam production logs.
    if (iType == MCD_ERROR)
    {
        wchar_t szErrorBuffer[256] = L"";
        va_list pArgsForFile;
        va_start(pArgsForFile, pStr);
        // C99 4-arg vswprintf -- explicit buffer size, bounded write. The
        // 3-arg MS-extension form is unsafe (no size param, can overflow).
        _vsnwprintf(szErrorBuffer, sizeof(szErrorBuffer) / sizeof(szErrorBuffer[0]), pStr,
                    pArgsForFile);
        va_end(pArgsForFile);
        diagnostics_->ErrorReport().Write(L"[MCD_ERROR] %ls\r\n", szErrorBuffer);
    }

#ifdef CONSOLE_DEBUG
    if (m_bInit)
    {
        switch (iType)
        {
        case MCD_SEND:
            diagnostics_->SetConsoleTextColor(leaf::COLOR_OLIVE);
            break;
        case MCD_RECEIVE:
            diagnostics_->SetConsoleTextColor(leaf::COLOR_DARKGREEN);
            break;
        case MCD_ERROR:
            diagnostics_->SetConsoleTextColor(leaf::COLOR_WHITE, leaf::COLOR_DARKRED);
            break;
        case MCD_NORMAL:
            diagnostics_->SetConsoleTextColor(leaf::COLOR_GRAY);
            break;
        }

        wchar_t szBuffer[256] = L"";
        va_list pArguments;

        va_start(pArguments, pStr);
        vswprintf(szBuffer, sizeof(szBuffer) / sizeof(wchar_t), pStr, pArguments);
        va_end(pArguments);

        std::wcout << szBuffer << std::endl;
    }
#endif
}

// The OS name and version are fixed for the process lifetime, so each platform
// computes the string once into a function-local static. GetOSVersionString is
// called every frame from the debug overlay; caching keeps it off the hot path.
// The non-Windows branches build the result with std::wstring concatenation
// rather than swprintf("%s", ...): a narrow-to-wide "%s" conversion in a wide
// printf is non-portable. OS strings are ASCII, so the byte-wise widening via
// the iterator-pair wstring constructor is safe here.

namespace Core::Platform
{

#ifdef _WIN32

std::wstring GetOSVersionString()
{
    static const std::wstring osVersion = []() -> std::wstring {
        // GetVersionEx is intercepted by the application-compatibility manifest
        // and caps at 6.2 (Windows 8) for unmanifested apps. RtlGetVersion
        // bypasses that shim and reports the true OS version, so resolve it
        // dynamically from ntdll.
        using RtlGetVersionPtr = LONG(WINAPI *)(OSVERSIONINFOW *);

        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (hNtdll == nullptr)
            return L"Windows (unknown)";

        auto pRtlGetVersion =
            reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(hNtdll, "RtlGetVersion"));
        if (pRtlGetVersion == nullptr)
            return L"Windows (unknown)";

        OSVERSIONINFOW info = {};
        info.dwOSVersionInfoSize = sizeof(info);
        if (pRtlGetVersion(&info) != 0) // 0 == STATUS_SUCCESS
            return L"Windows (unknown)";

        wchar_t buf[64];
        swprintf(buf, std::size(buf), L"Windows %lu.%lu", info.dwMajorVersion, info.dwMinorVersion);
        return buf;
    }();
    return osVersion;
}

std::wstring GetOSDistroName()
{
    return {};
} // no distro concept on Windows

#elif defined(__ANDROID__)

std::wstring GetOSVersionString()
{
    static const std::wstring osVersion = []() -> std::wstring {
        // ro.build.version.release is the user-facing release string, e.g. "14".
        char release[PROP_VALUE_MAX] = {};
        if (__system_property_get("ro.build.version.release", release) <= 0)
            return L"Android";

        const std::string releaseStr(release);
        return L"Android " + std::wstring(releaseStr.begin(), releaseStr.end());
    }();
    return osVersion;
}

std::wstring GetOSDistroName()
{
    return {};
} // Android reports its own name

#elif defined(__APPLE__)

std::wstring GetOSVersionString()
{
    static const std::wstring osVersion = []() -> std::wstring {
#if TARGET_OS_IPHONE
        const std::string name = "iOS";
#else
        const std::string name = "macOS";
#endif
        const std::wstring nameW(name.begin(), name.end());

        // kern.osproductversion is the marketing version (e.g. "14.5"),
        // available since macOS 10.13.4 / iOS 11.
        char version[64] = {};
        size_t len = sizeof(version);
        if (sysctlbyname("kern.osproductversion", version, &len, nullptr, 0) != 0 ||
            version[0] == '\0')
            return nameW;

        const std::string versionStr(version);
        return nameW + L" " + std::wstring(versionStr.begin(), versionStr.end());
    }();
    return osVersion;
}

std::wstring GetOSDistroName()
{
    return {};
} // macOS/iOS report their own name

#else // ---- generic POSIX (Linux, BSD) ------------------------------------

namespace
{
// The distro PRETTY_NAME (e.g. "Ubuntu 24.04.3 LTS") from /etc/os-release,
// the freedesktop-standard, distro-agnostic source. Empty if unavailable.
std::wstring ReadDistroPrettyName()
{
    FILE *f = std::fopen("/etc/os-release", "r");
    if (!f)
        return {};
    std::string pretty;
    char line[256];
    while (std::fgets(line, sizeof(line), f))
    {
        if (std::strncmp(line, "PRETTY_NAME=", 12) != 0)
            continue;
        std::string value(line + 12);
        while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
            value.pop_back();
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        pretty = value;
        break;
    }
    std::fclose(f);
    return std::wstring(pretty.begin(), pretty.end()); // PRETTY_NAME is ASCII
}
} // namespace

std::wstring GetOSDistroName()
{
    static const std::wstring distro = ReadDistroPrettyName();
    return distro;
}

std::wstring GetOSVersionString()
{
    static const std::wstring osVersion = []() -> std::wstring {
        utsname uts{};
        if (uname(&uts) != 0)
            return L"Unknown OS";

        // sysname is the kernel name ("Linux"); release is like "6.18.33-...".
        // Show only the kernel major.minor to keep the overlay line short.
        const std::string sysName(uts.sysname);
        std::wstring kernel(sysName.begin(), sysName.end());

        int major = 0;
        int minor = 0;
        if (sscanf(uts.release, "%d.%d", &major, &minor) == 2)
            kernel += L" " + std::to_wstring(major) + L"." + std::to_wstring(minor);

        // Prefer "<distro> (<kernel>)" so the overlay shows which distribution
        // it is, not just the kernel.
        const std::wstring distro = GetOSDistroName();
        if (!distro.empty())
            return distro + L" (" + kernel + L")";
        return kernel;
    }();
    return osVersion;
}

#endif // _WIN32

} // namespace Core::Platform

void ApplicationLegacyCalls::DeleteBitmap(unsigned int textureIndex, bool force)
{
    applicationKeeper_.DeleteApplicationBitmap(textureIndex, force);
}
bool ApplicationLegacyCalls::LoadBitmapW(const wchar_t *fileName, std::uint32_t textureIndex,
                                         LegacyTextureFilter filter, LegacyTextureWrap wrapMode,
                                         bool check, bool fullPath)
{
    const bool loaded = applicationKeeper_.LoadApplicationBitmap(fileName, textureIndex, filter,
                                                                 wrapMode, fullPath);
    if (!loaded && check)
    {
        wchar_t message[256] = {};
        mu_swprintf(message, L"LoadBitmap Failed: %ls", fileName);
        PopUpErrorCheckMsgBox(message, true);
    }
    return loaded;
}

void ApplicationLegacyCalls::PopUpErrorCheckMsgBox(const wchar_t *errorMessage, bool forceDestroy)
{
    if (!applicationKeeper_.IsOwnerThread())
    {
        applicationKeeper_.DeferErrorMessage(errorMessage, forceDestroy);
        return;
    }

    if (forceDestroy)
    {
        MessageBox(applicationKeeper_.PlatformWindowHandle(), errorMessage, L"ErrorCheckBox",
                   MB_OK | MB_ICONERROR);
    }
    else if (MessageBox(applicationKeeper_.PlatformWindowHandle(), errorMessage, L"ErrorCheckBox",
                        MB_YESNO | MB_ICONERROR) == IDYES)
    {
        return;
    }
    if (SessionManager *sessions = applicationKeeper_.SessionManagerUnit())
    {
        sessions->DeleteSockets();
    }
    applicationKeeper_.PlatformDestroyRequested() = true;
    DestroyWindow();
    ExitProcess(0);
}
