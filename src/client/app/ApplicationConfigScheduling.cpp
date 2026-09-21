#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationKeeper.h"
#include "data/Localization.h"
#include "data/ResourceData.h"
#include "support/CoreMath.h"

#ifdef _WIN32
#include <dpapi.h>
#include <shellapi.h>
#endif

namespace
{
constexpr wchar_t MissingValue[] = L"{8D8EF0F8-7A39-43D2-97B7-126AC90B2458}";
constexpr int MinimumVolume = 0;
constexpr int MaximumVolume = 10;
constexpr int MaximumServerPort = 65535;
constexpr int MinimumUiScalePercent = 100;
constexpr int MaximumUiScalePercent = 200;
constexpr int MaximumSessionWorkerCount = 64;
constexpr int MaximumSharedAssetIdleSeconds = 86400;
constexpr int MaximumSessionInitDelaySeconds = 86400;

std::filesystem::path ExecutableConfigPath(const wchar_t *fileName)
{
    wchar_t executablePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
    const auto directory = std::filesystem::path(executablePath).parent_path();
    const auto path = directory / L"configs" / fileName;
    std::filesystem::create_directories(path.parent_path());
    const auto legacyPath = directory / fileName;
    if (!std::filesystem::exists(path) && std::filesystem::exists(legacyPath))
    {
        std::filesystem::rename(legacyPath, path);
    }
    return path;
}

std::optional<std::wstring> ReadOptionalString(const std::filesystem::path &path,
                                               const wchar_t *section, const wchar_t *key)
{
    if (path.empty())
    {
        return std::nullopt;
    }

    std::vector<wchar_t> buffer(256);
    while (true)
    {
        const DWORD read =
            GetPrivateProfileStringW(section, key, MissingValue, buffer.data(),
                                     static_cast<DWORD>(buffer.size()), path.wstring().c_str());
        if (read < buffer.size() - 1)
        {
            std::wstring value(buffer.data());
            return value == MissingValue ? std::nullopt
                                         : std::optional<std::wstring>(std::move(value));
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::optional<int> ParseInt(const std::optional<std::wstring> &text)
{
    if (!text.has_value() || text->empty())
    {
        return std::nullopt;
    }

    wchar_t *end = nullptr;
    errno = 0;
    const long value = std::wcstol(text->c_str(), &end, 10);
    if (errno == ERANGE || end == text->c_str() || *end != L'\0' ||
        value < (std::numeric_limits<int>::min)() || value > (std::numeric_limits<int>::max)())
    {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

std::optional<float> ParseFloat(const std::optional<std::wstring> &text)
{
    if (!text.has_value() || text->empty())
        return std::nullopt;

    wchar_t *end = nullptr;
    errno = 0;
    const float value = std::wcstof(text->c_str(), &end);
    if (errno == ERANGE || end == text->c_str() || *end != L'\0' || !std::isfinite(value))
    {
        return std::nullopt;
    }
    return value;
}

int ReadRangedInt(const std::filesystem::path &path, const wchar_t *section, const wchar_t *key,
                  int defaultValue, int minimum, int maximum)
{
    const std::optional<int> value = ParseInt(ReadOptionalString(path, section, key));
    return value.has_value() && *value >= minimum && *value <= maximum ? *value : defaultValue;
}

bool ReadBool(const std::filesystem::path &path, const wchar_t *section, const wchar_t *key,
              bool defaultValue)
{
    const std::optional<int> value = ParseInt(ReadOptionalString(path, section, key));
    return value == 0 ? false : value == 1 ? true : defaultValue;
}

bool WriteString(const std::filesystem::path &path, const wchar_t *section, const wchar_t *key,
                 const std::wstring &value)
{
    return path.empty() ||
           WritePrivateProfileStringW(section, key, value.c_str(), path.wstring().c_str()) != FALSE;
}

bool WriteInt(const std::filesystem::path &path, const wchar_t *section, const wchar_t *key,
              int value)
{
    return WriteString(path, section, key, std::to_wstring(value));
}

bool RemoveKey(const std::filesystem::path &path, const wchar_t *section, const wchar_t *key)
{
    return path.empty() ||
           WritePrivateProfileStringW(section, key, nullptr, path.wstring().c_str()) != FALSE;
}

bool RemoveSection(const std::filesystem::path &path, const wchar_t *section)
{
    return path.empty() ||
           WritePrivateProfileStringW(section, nullptr, nullptr, path.wstring().c_str()) != FALSE;
}
} // namespace

ApplicationConfigUnit::ApplicationConfigUnit(ApplicationKeeper &keeper,
                                             SessionConfigStore &sessionConfigStore)
    : ApplicationConfigUnit(keeper, ExecutableConfigPath(L"config.ini"))
{
    (void)Load(sessionConfigStore);
}

ApplicationConfigUnit::ApplicationConfigUnit(ApplicationKeeper &keeper,
                                             std::filesystem::path configPath) noexcept
    : ApplicationLegacyCalls(keeper), values_(keeper.ApplicationConfig()),
      configPath_(std::move(configPath))
{
    (void)applicationKeeper_.RegisterApplicationConfig(*this);
}

bool ApplicationConfigUnit::Load(SessionConfigStore &sessionConfigStore)
{
    LoadAppValues();
    (void)sessionConfigStore.Load();
    (void)Save();

    loaded_ = sessionConfigStore.IsLoaded();
    return loaded_;
}

bool ApplicationConfigUnit::Save() const
{
    const bool wroteValues =
        WriteInt(configPath_, L"Window", L"Width", values_.windowWidth) &&
        WriteInt(configPath_, L"Window", L"Height", values_.windowHeight) &&
        WriteInt(configPath_, L"Window", L"Windowed", values_.windowed ? 1 : 0) &&
        WriteInt(configPath_, L"Audio", L"MasterSoundVolume", values_.masterSoundVolume) &&
        WriteInt(configPath_, L"Audio", L"MasterMusicVolume", values_.masterMusicVolume) &&
        WriteString(configPath_, L"Network", L"DefaultConnectServerIP",
                    values_.defaultConnectServerIp) &&
        WriteInt(configPath_, L"Network", L"DefaultConnectServerPort",
                 values_.defaultConnectServerPort) &&
        WriteString(configPath_, L"Localization", L"AssetLanguage", values_.assetLanguage) &&
        WriteString(configPath_, L"Localization", L"UILocale", values_.uiLocale) &&
        WriteString(configPath_, L"UI", L"Font", values_.font) &&
        WriteInt(configPath_, L"UI", L"RmlScale",
                 static_cast<int>(std::lround(values_.rmlUiScale * 100.0F))) &&
        WriteInt(configPath_, L"UI", L"ControlUIScale", values_.controlUiScalePercent) &&
        SavePerformanceValues();
    return wroteValues && RemoveLegacyAppKeys();
}

bool ApplicationConfigUnit::IsLoaded() const noexcept
{
    return loaded_;
}

ApplicationConfigValues &ApplicationConfigUnit::Values() noexcept
{
    return values_;
}

const ApplicationConfigValues &ApplicationConfigUnit::Values() const noexcept
{
    return values_;
}

void ApplicationConfigUnit::SetWindowSize(int width, int height) noexcept
{
    values_.windowWidth = width;
    values_.windowHeight = height;
}

void ApplicationConfigUnit::SetWindowMode(bool windowed) noexcept
{
    values_.windowed = windowed;
}

void ApplicationConfigUnit::SetMasterSoundVolume(int level) noexcept
{
    values_.masterSoundVolume = level >= MinimumVolume && level <= MaximumVolume ? level : 5;
}

void ApplicationConfigUnit::SetMasterMusicVolume(int level) noexcept
{
    values_.masterMusicVolume = level >= MinimumVolume && level <= MaximumVolume ? level : 5;
}

void ApplicationConfigUnit::SetUiLocale(std::wstring locale)
{
    values_.uiLocale = std::move(locale);
}

void ApplicationConfigUnit::SetFont(std::wstring font)
{
    values_.font = std::move(font);
}

void ApplicationConfigUnit::LoadAppValues()
{
    const ApplicationConfigValues defaults;
    values_.windowWidth = ReadRangedInt(configPath_, L"Window", L"Width", defaults.windowWidth, 1,
                                        (std::numeric_limits<int>::max)());
    values_.windowHeight = ReadRangedInt(configPath_, L"Window", L"Height", defaults.windowHeight,
                                         1, (std::numeric_limits<int>::max)());
    values_.windowed = ReadBool(configPath_, L"Window", L"Windowed", defaults.windowed);

    const std::optional<int> targetSound =
        ParseInt(ReadOptionalString(configPath_, L"Audio", L"MasterSoundVolume"));
    values_.masterSoundVolume =
        targetSound.has_value() && *targetSound >= MinimumVolume && *targetSound <= MaximumVolume
            ? *targetSound
            : ReadRangedInt(configPath_, L"Audio", L"SoundVolume", defaults.masterSoundVolume,
                            MinimumVolume, MaximumVolume);

    const std::optional<int> targetMusic =
        ParseInt(ReadOptionalString(configPath_, L"Audio", L"MasterMusicVolume"));
    values_.masterMusicVolume =
        targetMusic.has_value() && *targetMusic >= MinimumVolume && *targetMusic <= MaximumVolume
            ? *targetMusic
            : ReadRangedInt(configPath_, L"Audio", L"MusicVolume", defaults.masterMusicVolume,
                            MinimumVolume, MaximumVolume);

    values_.defaultConnectServerIp =
        ReadOptionalString(configPath_, L"Network", L"DefaultConnectServerIP")
            .value_or(ReadOptionalString(configPath_, L"CONNECTION SETTINGS", L"ServerIP")
                          .value_or(defaults.defaultConnectServerIp));
    values_.defaultConnectServerPort =
        ReadRangedInt(configPath_, L"Network", L"DefaultConnectServerPort",
                      ReadRangedInt(configPath_, L"CONNECTION SETTINGS", L"ServerPort",
                                    defaults.defaultConnectServerPort, 1, MaximumServerPort),
                      1, MaximumServerPort);

    values_.assetLanguage = ReadOptionalString(configPath_, L"Localization", L"AssetLanguage")
                                .value_or(ReadOptionalString(configPath_, L"LOGIN", L"Language")
                                              .value_or(defaults.assetLanguage));
    LoadUiValues(defaults);
    values_.initialCameraZoom =
        ReadRangedInt(configPath_, L"Camera", L"Zoom", defaults.initialCameraZoom, 600, 3000);
    values_.controlUiScalePercent = ReadRangedInt(
        configPath_, L"UI", L"ControlUIScale",
        ReadRangedInt(configPath_, L"Workspace", L"ControlUIScale", defaults.controlUiScalePercent,
                      MinimumUiScalePercent, MaximumUiScalePercent),
        MinimumUiScalePercent, MaximumUiScalePercent);
    LoadPerformanceValues(defaults);
}

void ApplicationConfigUnit::LoadUiValues(const ApplicationConfigValues &defaults)
{
    values_.uiLocale =
        ReadOptionalString(configPath_, L"Localization", L"UILocale")
            .value_or(
                ReadOptionalString(configPath_, L"UI", L"Locale").value_or(defaults.uiLocale));
    values_.font = ReadOptionalString(configPath_, L"UI", L"Font").value_or(defaults.font);
    const auto configuredScale = ReadOptionalString(configPath_, L"UI", L"RmlScale");
    const auto scalePercent = ParseInt(configuredScale);
    if (scalePercent.has_value() && *scalePercent >= MinimumUiScalePercent &&
        *scalePercent <= MaximumUiScalePercent)
    {
        values_.rmlUiScale = static_cast<float>(*scalePercent) / 100.0F;
    }
    else
    {
        const auto legacyScale = ParseFloat(configuredScale);
        values_.rmlUiScale = legacyScale.has_value() && *legacyScale >= 1.0F && *legacyScale <= 2.0F
                                 ? *legacyScale
                                 : defaults.rmlUiScale;
    }
}

bool ApplicationConfigUnit::RemoveLegacyAppKeys() const
{
    return RemoveKey(configPath_, L"Performance", L"LegacySimulationFps") &&
           RemoveKey(configPath_, L"Audio", L"SoundVolume") &&
           RemoveKey(configPath_, L"Audio", L"MusicVolume") &&
           RemoveKey(configPath_, L"Audio", L"SoundEnabled") &&
           RemoveKey(configPath_, L"Audio", L"MusicEnabled") &&
           RemoveKey(configPath_, L"Audio", L"VolumeLevel") &&
           RemoveSection(configPath_, L"CONNECTION SETTINGS") &&
           RemoveKey(configPath_, L"LOGIN", L"Language") &&
           RemoveKey(configPath_, L"LOGIN", L"Version") &&
           RemoveKey(configPath_, L"LOGIN", L"TestVersion") &&
           RemoveKey(configPath_, L"UI", L"Locale") &&
           RemoveKey(configPath_, L"Graphics", L"RenderTextType") &&
           RemoveKey(configPath_, L"Graphics", L"ColorDepth") &&
           RemoveKey(configPath_, L"Graphics", L"RenderBackend") &&
           RemoveKey(configPath_, L"Graphic", L"RenderBackend") &&
           RemoveSection(configPath_, L"Workspace") && RemoveSection(configPath_, L"Graphics") &&
           RemoveSection(configPath_, L"Graphic") && RemoveSection(configPath_, L"PARTITION");
}

void ApplicationConfigUnit::LoadPerformanceValues(const ApplicationConfigValues &defaults)
{
    values_.rendererBackend =
        ReadOptionalString(configPath_, L"Performance", L"RendererBackend").value_or(defaults.rendererBackend);
    if (values_.rendererBackend != L"auto" && values_.rendererBackend != L"direct3d12" &&
        values_.rendererBackend != L"vulkan" && values_.rendererBackend != L"metal")
        values_.rendererBackend = defaults.rendererBackend;
    values_.vSync = ReadBool(configPath_, L"Performance", L"VSync", defaults.vSync);
    values_.renderPipeline =
        ReadBool(configPath_, L"Performance", L"RenderPipeline", defaults.renderPipeline);
    const auto fpsLimit = ParseFloat(ReadOptionalString(configPath_, L"Performance", L"FpsLimit"));
    values_.fpsLimit = fpsLimit.has_value() && *fpsLimit >= 0.0F ? *fpsLimit : defaults.fpsLimit;
    // values_.legacyReferenceFps =
    //     ReadRangedInt(configPath_, L"Performance", L"LegacyReferenceFps",
    //                   defaults.legacyReferenceFps, 1, (std::numeric_limits<int>::max)());
    values_.sessionWorkerCount =
        ReadRangedInt(configPath_, L"Performance", L"SessionWorkerCount",
                      defaults.sessionWorkerCount, 2, MaximumSessionWorkerCount);
    values_.sharedAssetIdleSeconds =
        ReadRangedInt(configPath_, L"Performance", L"SharedAssetIdleSeconds",
                      defaults.sharedAssetIdleSeconds, 0, MaximumSharedAssetIdleSeconds);
}

bool ApplicationConfigUnit::SavePerformanceValues() const
{
    return WriteString(configPath_, L"Performance", L"RendererBackend", values_.rendererBackend) &&
           WriteInt(configPath_, L"Performance", L"VSync", values_.vSync ? 1 : 0) &&
           WriteInt(configPath_, L"Performance", L"RenderPipeline",
                    values_.renderPipeline ? 1 : 0) &&
           WriteString(configPath_, L"Performance", L"FpsLimit", std::to_wstring(values_.fpsLimit)) &&
           WriteInt(configPath_, L"Performance", L"LegacyReferenceFps",
                    values_.legacyReferenceFps) &&
           WriteInt(configPath_, L"Performance", L"SessionWorkerCount",
                    values_.sessionWorkerCount) &&
           WriteInt(configPath_, L"Performance", L"SharedAssetIdleSeconds",
                    values_.sharedAssetIdleSeconds);
}

namespace
{
constexpr wchar_t MissingSessionConfigValue[] = L"{308B3B61-75CD-4F2E-8E78-94565A486D55}";

std::optional<std::wstring> ReadSessionConfigString(const std::filesystem::path &path,
                                                    const wchar_t *section, const wchar_t *key)
{
    std::vector<wchar_t> buffer(256);
    while (true)
    {
        const DWORD read =
            GetPrivateProfileStringW(section, key, MissingSessionConfigValue, buffer.data(),
                                     static_cast<DWORD>(buffer.size()), path.wstring().c_str());
        if (read < buffer.size() - 1)
        {
            std::wstring value(buffer.data());
            return value == MissingSessionConfigValue
                       ? std::nullopt
                       : std::optional<std::wstring>(std::move(value));
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring SlotSection(SessionSlotId slotId)
{
    return L"Slot." + std::to_wstring(slotId.RawValue());
}

SessionShortcutSettings DefaultSessionShortcuts()
{
    static_assert(SDL_SCANCODE_COUNT == SessionShortcutScancodeCount);
    SessionShortcutSettings shortcuts;
    shortcuts.toggleControlBar.keys = {SessionShortcutModifierShift, SDL_SCANCODE_KP_0};
    for (std::size_t index = 0; index < shortcuts.slotViews.size(); ++index)
    {
        shortcuts.slotViews[index].keys = {
            SessionShortcutModifierShift,
            static_cast<std::int32_t>(SDL_SCANCODE_KP_1) + static_cast<std::int32_t>(index)};
    }
    return shortcuts;
}

std::optional<SessionShortcutBinding> ParseSessionShortcut(const std::wstring &value)
{
    if (value.empty() || value.back() == L',')
    {
        return std::nullopt;
    }

    SessionShortcutBinding binding;
    std::wstringstream stream(value);
    std::wstring token;
    while (std::getline(stream, token, L','))
    {
        const std::size_t first = token.find_first_not_of(L" \t\r\n");
        const std::size_t last = token.find_last_not_of(L" \t\r\n");
        if (first == std::wstring::npos)
        {
            return std::nullopt;
        }
        const std::optional<int> key = ParseInt(token.substr(first, last - first + 1));
        if (!key.has_value())
        {
            return std::nullopt;
        }

        const bool modifier = *key >= SessionShortcutModifierMeta &&
                              *key <= SessionShortcutModifierControl;
        const bool scancode = *key > 0 && static_cast<std::size_t>(*key) < SessionShortcutScancodeCount &&
                              !(*key >= SDL_SCANCODE_LCTRL && *key <= SDL_SCANCODE_RGUI);
        if ((!modifier && !scancode) ||
            std::find(binding.keys.begin(), binding.keys.end(), *key) != binding.keys.end())
        {
            return std::nullopt;
        }
        binding.keys.push_back(*key);
    }

    if (binding.keys.empty() || std::none_of(binding.keys.begin(), binding.keys.end(),
                                             [](std::int32_t key) { return key >= 0; }))
    {
        return std::nullopt;
    }
    std::sort(binding.keys.begin(), binding.keys.end());
    return binding;
}

void ReadSessionShortcut(const std::filesystem::path &path, const wchar_t *key,
                         SessionShortcutBinding &binding)
{
    const std::optional<std::wstring> value = ReadSessionConfigString(path, L"Sessions", key);
    if (!value.has_value())
    {
        return;
    }
    const std::optional<SessionShortcutBinding> parsed = ParseSessionShortcut(*value);
    if (parsed.has_value())
    {
        binding = *parsed;
    }
}
} // namespace

SessionConfigStore::SessionConfigStore(ApplicationKeeper &keeper)
    : SessionConfigStore(keeper,
#ifdef _WIN32
                         PathFromCommandLine(GetCommandLineW()))
#else
                         std::filesystem::path{})
#endif
{
}

SessionConfigStore::SessionConfigStore(ApplicationKeeper &keeper,
                                       std::filesystem::path configPath) noexcept
    : ApplicationLegacyCalls(keeper), profiles_(keeper.SessionProfiles()),
      configPath_(std::move(configPath)), shortcuts_(DefaultSessionShortcuts())
{
    (void)applicationKeeper_.RegisterSessionConfigStore(*this);
}

SessionConfigValues SessionConfigStore::ReadProfile(const std::wstring &section) const
{
    const std::wstring legacySection = L"Session." + section.substr(5);
    const auto read = [&](const wchar_t *key) {
        auto value = ReadSessionConfigString(configPath_, section.c_str(), key);
        return value.has_value()
                   ? *value
                   : ReadSessionConfigString(configPath_, legacySection.c_str(), key).value_or(L"");
    };
    SessionConfigValues values;
    values.displayName = read(L"DisplayName");
    values.username = read(L"Username");
    values.encryptedPassword = read(L"EncryptedPassword");
    values.rememberMe = !values.username.empty();
    values.savePassword = !values.encryptedPassword.empty();
    const auto port = ParseInt(read(L"AutoLoginPort"));
    values.autoLoginPort =
        port.has_value() && *port >= 0 && *port <= 65535 ? static_cast<unsigned short>(*port) : 0;
    values.autoSelectCharacter = read(L"AutoSelectCharacter");
    return values;
}

bool SessionConfigStore::Load()
{
    profiles_.clear();
    initDelay_ = {};
    shortcuts_ = DefaultSessionShortcuts();
    std::error_code error;
    fileExistedAtLoad_ =
        !configPath_.empty() && std::filesystem::exists(configPath_, error) && !error;
    if (!fileExistedAtLoad_)
    {
        loaded_ = true;
        return true;
    }

    const std::wstring slots =
        ReadSessionConfigString(configPath_, L"Sessions", L"Slots").value_or(L"");
    const auto initDelay = ParseInt(ReadSessionConfigString(
        configPath_, L"Sessions", L"InitDelay"));
    if (initDelay.has_value() && *initDelay >= 0 &&
        *initDelay <= MaximumSessionInitDelaySeconds)
    {
        initDelay_ = std::chrono::seconds(*initDelay);
    }
    ReadSessionShortcut(configPath_, L"ControlBarShortcut", shortcuts_.toggleControlBar);
    for (std::size_t index = 0; index < shortcuts_.slotViews.size(); ++index)
    {
        const std::wstring key = L"Slot" + std::to_wstring(index + 1) + L"Shortcut";
        ReadSessionShortcut(configPath_, key.c_str(), shortcuts_.slotViews[index]);
    }
    std::vector<SessionShortcutBinding> loadedShortcuts{shortcuts_.toggleControlBar};
    loadedShortcuts.insert(loadedShortcuts.end(), shortcuts_.slotViews.begin(), shortcuts_.slotViews.end());
    std::sort(loadedShortcuts.begin(), loadedShortcuts.end(),
              [](const SessionShortcutBinding &left, const SessionShortcutBinding &right) {
                  return left.keys < right.keys;
              });
    if (std::adjacent_find(loadedShortcuts.begin(), loadedShortcuts.end()) != loadedShortcuts.end())
    {
        shortcuts_ = DefaultSessionShortcuts();
    }
    std::wstringstream slotStream(slots);
    std::wstring token;
    while (std::getline(slotStream, token, L','))
    {
        const std::size_t first = token.find_first_not_of(L" \t\r\n");
        const std::size_t last = token.find_last_not_of(L" \t\r\n");
        if (first == std::wstring::npos)
        {
            continue;
        }
        token = token.substr(first, last - first + 1);
        if (!std::all_of(token.begin(), token.end(),
                         [](wchar_t character) { return std::iswdigit(character) != 0; }))
        {
            continue;
        }

        wchar_t *end = nullptr;
        errno = 0;
        const unsigned long raw = std::wcstoul(token.c_str(), &end, 10);
        if (errno == ERANGE || end == token.c_str() || *end != L'\0' ||
            raw > (std::numeric_limits<SessionSlotId::ValueType>::max)())
        {
            continue;
        }
        const std::optional<SessionSlotId> slotId =
            SessionSlotId::TryCreate(static_cast<SessionSlotId::ValueType>(raw));
        if (!slotId.has_value() || profiles_.contains(*slotId))
        {
            continue;
        }

        profiles_.emplace(*slotId, ReadProfile(SlotSection(*slotId)));
    }

    loaded_ = true;
    return true;
}

bool SessionConfigStore::IsLoaded() const noexcept
{
    return loaded_;
}

bool SessionConfigStore::FileExistedAtLoad() const noexcept
{
    return fileExistedAtLoad_;
}

std::chrono::seconds SessionConfigStore::InitDelay() const noexcept
{
    return initDelay_;
}

const SessionShortcutSettings &SessionConfigStore::Shortcuts() const noexcept
{
    return shortcuts_;
}

const SessionConfigValues *SessionConfigStore::Find(SessionSlotId slotId) const noexcept
{
    const auto found = profiles_.find(slotId);
    return found == profiles_.end() ? nullptr : &found->second;
}

SessionConfigValues SessionConfigStore::ProfileOrDefault(SessionSlotId slotId) const
{
    const SessionConfigValues *values = Find(slotId);
    return values != nullptr ? *values : SessionConfigValues{};
}

SessionConfigValues SessionConfigStore::TakeStartupProfile(SessionSlotId slotId)
{
    std::scoped_lock lock(transactionMutex_);
    const auto found = profiles_.find(slotId);
    if (found == profiles_.end())
    {
        return {};
    }
    SessionConfigValues values = found->second;
    found->second.autoLoginPort = 0;
    found->second.autoSelectCharacter.clear();
    return values;
}

std::vector<SessionSlotId> SessionConfigStore::ValidSlotsOrFallback() const
{
    std::scoped_lock lock(transactionMutex_);
    std::vector<SessionSlotId> slots;
    slots.reserve(profiles_.empty() ? 1 : profiles_.size());
    for (const auto &[slotId, values] : profiles_)
    {
        (void)values;
        slots.push_back(slotId);
    }
    if (slots.empty())
    {
        slots.push_back(*SessionSlotId::TryCreate(1));
    }
    return slots;
}

std::optional<SessionSlotId> SessionConfigStore::NextAppendSlot(
    std::optional<SessionSlotId> greatestLiveSlot) const noexcept
{
    std::scoped_lock lock(transactionMutex_);
    SessionSlotId::ValueType greatest =
        greatestLiveSlot.has_value() ? greatestLiveSlot->RawValue() : 0;
    if (!profiles_.empty())
    {
        greatest = (std::max)(greatest, profiles_.rbegin()->first.RawValue());
    }
    if (greatest == (std::numeric_limits<SessionSlotId::ValueType>::max)())
    {
        return std::nullopt;
    }
    return SessionSlotId::TryCreate(greatest + 1);
}

bool SessionConfigStore::AppendDefaultSlot(SessionSlotId slotId)
{
    std::scoped_lock lock(transactionMutex_);
    if (profiles_.contains(slotId) || (!profiles_.empty() && slotId < profiles_.rbegin()->first))
    {
        return false;
    }
    SessionConfigCatalog candidate = profiles_;
    if (candidate.empty() && slotId.RawValue() > 1)
    {
        candidate.emplace(*SessionSlotId::TryCreate(1), SessionConfigValues{});
    }
    candidate.emplace(slotId, SessionConfigValues{});
    return CommitCatalog(std::move(candidate));
}

bool SessionConfigStore::DeleteSlot(SessionSlotId slotId)
{
    std::scoped_lock lock(transactionMutex_);
    if (!profiles_.contains(slotId))
    {
        return true;
    }
    SessionConfigCatalog candidate = profiles_;
    candidate.erase(slotId);
    return CommitCatalog(std::move(candidate));
}

bool SessionConfigStore::MoveOrSwapSlot(SessionSlotId source, SessionSlotId destination)
{
    return MoveOrSwapSlot(source, destination, {});
}

bool SessionConfigStore::MoveOrSwapSlot(SessionSlotId source, SessionSlotId destination,
                                        const std::function<void()> &applyLiveCommit)
{
    std::scoped_lock lock(transactionMutex_);
    const auto sourceProfile = profiles_.find(source);
    if (sourceProfile == profiles_.end() || source == destination)
    {
        return false;
    }

    SessionConfigCatalog candidate = profiles_;
    const auto candidateSource = candidate.find(source);
    const auto candidateDestination = candidate.find(destination);
    if (candidateDestination == candidate.end())
    {
        candidate.emplace(destination, std::move(candidateSource->second));
        candidate.erase(candidateSource);
    }
    else
    {
        std::swap(candidateSource->second, candidateDestination->second);
    }
    if (!CommitCatalog(std::move(candidate)))
    {
        return false;
    }
    if (applyLiveCommit)
    {
        applyLiveCommit();
    }
    return true;
}

bool SessionConfigStore::UpdateCredentials(
    const std::function<CredentialSnapshot()> &captureCurrent)
{
    std::scoped_lock lock(transactionMutex_);
    if (!captureCurrent)
    {
        return false;
    }
    CredentialSnapshot snapshot = captureCurrent();
    const SessionSlotId slotId = snapshot.first;
    const SessionConfigValues &live = snapshot.second;
    SessionConfigCatalog candidate = profiles_;
    SessionConfigValues &persisted = candidate[slotId];
    persisted.rememberMe = live.rememberMe;
    persisted.savePassword = live.savePassword;
    persisted.autoLoginPort = live.autoLoginPort;
    persisted.username = live.username;
    persisted.encryptedPassword = live.encryptedPassword;
    return CommitCatalog(std::move(candidate));
}

bool SessionConfigStore::Update(SessionSlotId slotId, const SessionConfigValues &values)
{
    std::scoped_lock lock(transactionMutex_);
    SessionConfigCatalog candidate = profiles_;
    candidate[slotId] = values;
    return CommitCatalog(std::move(candidate));
}

bool SessionConfigStore::ProtectCredentials(SessionConfigValues &values, const wchar_t *username,
                                            const wchar_t *password) const
{
    values.username = username != nullptr ? username : L"";
    values.encryptedPassword = values.savePassword ? ProtectSetting(password) : L"";
    return !values.savePassword || !values.encryptedPassword.empty();
}

void SessionConfigStore::DecryptCredentials(const SessionConfigValues &values, wchar_t *username,
                                            wchar_t *password, std::size_t usernameSize,
                                            std::size_t passwordSize) const
{
    if (username != nullptr && usernameSize > 0)
    {
        username[0] = L'\0';
    }
    if (password != nullptr && passwordSize > 0)
    {
        password[0] = L'\0';
    }

    const std::wstring &unprotectedUsername = values.username;
    if (!unprotectedUsername.empty() && username != nullptr && usernameSize > 0)
    {
        wcsncpy_s(username, usernameSize, unprotectedUsername.c_str(), _TRUNCATE);
    }

    if (!values.savePassword)
    {
        return;
    }
    const std::wstring unprotectedPassword = UnprotectSetting(values.encryptedPassword);
    if (!unprotectedPassword.empty() && password != nullptr && passwordSize > 0)
    {
        wcsncpy_s(password, passwordSize, unprotectedPassword.c_str(), _TRUNCATE);
    }
}

std::filesystem::path SessionConfigStore::PathFromCommandLine(const wchar_t *commandLine)
{
#ifdef _WIN32
    int count = 0;
    const std::unique_ptr<wchar_t *, decltype(&LocalFree)> arguments(
        CommandLineToArgvW(commandLine, &count), LocalFree);
    if (!arguments)
    {
        throw std::runtime_error("Cannot read the client command line.");
    }
    for (int index = 1; index < count; ++index)
    {
        if (std::wstring_view(arguments.get()[index]) != L"--session-config")
        {
            continue;
        }
        if (++index == count || arguments.get()[index][0] == L'\0' ||
            std::wstring_view(arguments.get()[index]).starts_with(L"--"))
        {
            throw std::invalid_argument("--session-config requires a preset file path.");
        }
        const auto path = std::filesystem::absolute(arguments.get()[index]);
        if (!std::filesystem::is_regular_file(path) || !std::ifstream(path))
        {
            throw std::runtime_error("Cannot read the --session-config preset file.");
        }
        return path;
    }
#else
    (void)commandLine;
#endif
    return {};
}

std::wstring SessionConfigStore::ProtectSetting(const wchar_t *input)
{
    if (input == nullptr || *input == L'\0')
    {
        return L"";
    }

    DATA_BLOB inputData;
    DATA_BLOB outputData;
    inputData.cbData = static_cast<DWORD>((std::wcslen(input) + 1) * sizeof(wchar_t));
    inputData.pbData = reinterpret_cast<BYTE *>(const_cast<wchar_t *>(input));
    if (!CryptProtectData(&inputData, nullptr, nullptr, nullptr, nullptr, 0, &outputData))
    {
        return L"";
    }

    std::wstring protectedValue = BinaryToHex(outputData.pbData, outputData.cbData);
    LocalFree(outputData.pbData);
    return protectedValue;
}

std::wstring SessionConfigStore::UnprotectSetting(const std::wstring &input)
{
    const std::vector<BYTE> protectedData = HexToBinary(input);
    if (protectedData.empty())
    {
        return L"";
    }

    DATA_BLOB inputData;
    DATA_BLOB outputData;
    inputData.pbData = const_cast<BYTE *>(protectedData.data());
    inputData.cbData = static_cast<DWORD>(protectedData.size());
    if (!CryptUnprotectData(&inputData, nullptr, nullptr, nullptr, nullptr, 0, &outputData))
    {
        return L"";
    }

    std::wstring value(reinterpret_cast<wchar_t *>(outputData.pbData),
                       outputData.cbData / sizeof(wchar_t));
    LocalFree(outputData.pbData);
    if (!value.empty() && value.back() == L'\0')
    {
        value.pop_back();
    }
    return value;
}

std::wstring SessionConfigStore::BinaryToHex(const BYTE *data, DWORD size)
{
    constexpr wchar_t HexDigits[] = L"0123456789ABCDEF";
    std::wstring value;
    value.reserve(size * 2);
    for (DWORD index = 0; index < size; ++index)
    {
        value += HexDigits[(data[index] >> 4) & 0x0F];
        value += HexDigits[data[index] & 0x0F];
    }
    return value;
}

std::vector<BYTE> SessionConfigStore::HexToBinary(const std::wstring &value)
{
    if (value.empty() || value.size() % 2 != 0)
    {
        return {};
    }

    const auto nibble = [](wchar_t digit) -> BYTE {
        if (digit >= L'0' && digit <= L'9')
        {
            return static_cast<BYTE>(digit - L'0');
        }
        if (digit >= L'a' && digit <= L'f')
        {
            return static_cast<BYTE>(digit - L'a' + 10);
        }
        return static_cast<BYTE>(digit - L'A' + 10);
    };

    std::vector<BYTE> bytes;
    bytes.reserve(value.size() / 2);
    for (std::size_t index = 0; index < value.size(); index += 2)
    {
        if (!std::iswxdigit(value[index]) || !std::iswxdigit(value[index + 1]))
        {
            return {};
        }
        bytes.push_back(static_cast<BYTE>((nibble(value[index]) << 4) | nibble(value[index + 1])));
    }
    return bytes;
}

bool SessionConfigStore::CommitCatalog(SessionConfigCatalog &&candidate)
{
    profiles_.swap(candidate);
    return true;
}

thread_local ApplicationSessionScheduler *ApplicationSessionScheduler::activeScheduler_ = nullptr;

namespace
{
void UpdateMaximum(std::atomic<std::uint64_t> &maximum, std::uint64_t candidate) noexcept
{
    std::uint64_t observed = maximum.load(std::memory_order_relaxed);
    while (observed < candidate &&
           !maximum.compare_exchange_weak(observed, candidate, std::memory_order_relaxed))
    {
    }
}

std::chrono::nanoseconds CurrentThreadCpuTime() noexcept
{
#if defined(_WIN32)
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user))
    {
        return {};
    }
    ULARGE_INTEGER kernelTicks{};
    kernelTicks.LowPart = kernel.dwLowDateTime;
    kernelTicks.HighPart = kernel.dwHighDateTime;
    ULARGE_INTEGER userTicks{};
    userTicks.LowPart = user.dwLowDateTime;
    userTicks.HighPart = user.dwHighDateTime;
    return std::chrono::nanoseconds(static_cast<std::chrono::nanoseconds::rep>(
        (kernelTicks.QuadPart + userTicks.QuadPart) * 100));
#else
    timespec value{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0)
    {
        return {};
    }
    return std::chrono::seconds(value.tv_sec) + std::chrono::nanoseconds(value.tv_nsec);
#endif
}
} // namespace

struct ApplicationSessionScheduler::Lane final
{
    explicit Lane(SessionGeneration value) noexcept : generation(value)
    {
    }

    SessionGeneration generation;
    bool accepting = true;
    bool queued = false;
    bool running = false;
    std::condition_variable idle;
};

struct ApplicationSessionScheduler::FrameGroup final
{
    explicit FrameGroup(std::size_t count) noexcept : remaining(count)
    {
    }

    void Complete() noexcept
    {
        std::lock_guard lock(mutex);
        if (remaining > 0 && --remaining == 0)
        {
            finished.notify_all();
        }
    }

    void Wait() noexcept
    {
        std::unique_lock lock(mutex);
        finished.wait(lock, [this] { return remaining == 0; });
    }

    std::mutex mutex;
    std::condition_variable finished;
    std::size_t remaining;
};

ApplicationSessionScheduler::ApplicationSessionScheduler(std::size_t workerCount,
                                                         std::size_t queueCapacity) noexcept
    : queueCapacity_((std::max)(std::size_t{1}, queueCapacity))
{
    const std::size_t count = workerCount == 0 ? RecommendedWorkerCount() : workerCount;
    try
    {
        workers_.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            workers_.emplace_back(&ApplicationSessionScheduler::WorkerLoop, this);
        }
    }
    catch (...)
    {
        Stop();
        std::lock_guard lock(mutex_);
        stopping_ = false;
    }
}

ApplicationSessionScheduler::~ApplicationSessionScheduler()
{
    Stop();
}

std::optional<SessionGeneration> ApplicationSessionScheduler::RegisterLane(SessionId id) noexcept
{
    std::lock_guard lock(mutex_);
    if (stopping_ || lanes_.contains(id))
    {
        return std::nullopt;
    }

    const auto generation = SessionGeneration::TryCreate(nextGeneration_);
    if (!generation.has_value())
    {
        return std::nullopt;
    }
    nextGeneration_ = nextGeneration_ == (std::numeric_limits<SessionGeneration::ValueType>::max)()
                          ? 1
                          : nextGeneration_ + 1;
    try
    {
        lanes_.emplace(id, std::make_shared<Lane>(*generation));
    }
    catch (...)
    {
        return std::nullopt;
    }
    return generation;
}

bool ApplicationSessionScheduler::QuiesceAndRemoveLane(SessionId id) noexcept
{
    std::unique_lock lock(mutex_);
    const auto found = lanes_.find(id);
    if (found == lanes_.end())
    {
        return false;
    }

    const std::shared_ptr<Lane> lane = found->second;
    lane->accepting = false;
    for (auto queued = queue_.begin(); queued != queue_.end();)
    {
        if (queued->lane != lane)
        {
            ++queued;
            continue;
        }
        CancelQueuedJob(*queued);
        queued = queue_.erase(queued);
    }
    lane->queued = false;
    lane->idle.wait(lock, [&lane] { return !lane->running; });
    lanes_.erase(found);
    return true;
}

bool ApplicationSessionScheduler::ExecuteFrame(const ApplicationFramePlan &plan,
                                               std::span<const SessionAdvanceJob> jobs,
                                               std::span<SessionAdvanceResult> results,
                                               ApplicationOwnerWork ownerWork) noexcept
{
    return ExecuteFrameWithPolicy(plan, jobs, results, false, ownerWork);
}

bool ApplicationSessionScheduler::ExecuteFrameInline(
    const ApplicationFramePlan &plan, std::span<const SessionAdvanceJob> jobs,
    std::span<SessionAdvanceResult> results) noexcept
{
    return ExecuteFrameWithPolicy(plan, jobs, results, true, {});
}

bool ApplicationSessionScheduler::ExecuteFrameWithPolicy(const ApplicationFramePlan &plan,
                                                         std::span<const SessionAdvanceJob> jobs,
                                                         std::span<SessionAdvanceResult> results,
                                                         bool forceInline,
                                                         ApplicationOwnerWork ownerWork) noexcept
{
    if (jobs.size() != plan.SessionCount() || results.size() < jobs.size())
    {
        return false;
    }
    if (jobs.empty())
    {
        ownerWork.Run();
        return true;
    }
    for (std::size_t index = 0; index < jobs.size(); ++index)
    {
        const SessionFrameInput &input = plan.Session(index);
        if (input.sessionId != jobs[index].sessionId ||
            input.generation != jobs[index].generation ||
            plan.FrameSequence() != jobs[index].frameSequence ||
            input.renderRequired != jobs[index].renderRequired ||
            input.surfaceGeneration != jobs[index].surfaceGeneration ||
            results[index].Id() != jobs[index].sessionId ||
            results[index].Generation() != jobs[index].generation ||
            results[index].FrameSequence() != jobs[index].frameSequence)
        {
            return false;
        }
    }
    if (plan.CancellationRequested())
    {
        for (std::size_t index = 0; index < jobs.size(); ++index)
        {
            results[index].status_ = SessionAdvanceStatus::Cancelled;
            results[index].orderedEffects_.Clear();
            ++cancelledJobs_;
        }
        return true;
    }

    FrameGroup group(jobs.size());
    for (std::size_t index = 0; index < jobs.size(); ++index)
    {
        const SessionAdvanceJob &job = jobs[index];
        SessionAdvanceResult &result = results[index];
        std::shared_ptr<Lane> lane;
        bool executeInline = false;
        {
            std::lock_guard lock(mutex_);
            const auto found = lanes_.find(job.sessionId);
            if (stopping_ || found == lanes_.end() || !found->second->accepting ||
                found->second->generation != job.generation)
            {
                result.status_ = SessionAdvanceStatus::StaleGeneration;
                ++staleJobs_;
                group.Complete();
                continue;
            }
            lane = found->second;
            if (lane->queued || lane->running)
            {
                result.status_ = SessionAdvanceStatus::ConcurrentAdvanceRejected;
                ++rejectedConcurrentJobs_;
                group.Complete();
                continue;
            }
            if (forceInline || workers_.empty() || queue_.size() >= queueCapacity_ ||
                activeScheduler_ == this)
            {
                lane->running = true;
                executeInline = true;
                ++inlineJobs_;
            }
            else
            {
                try
                {
                    lane->queued = true;
                    queue_.push_back({
                        job,
                        &result,
                        lane,
                        &group,
                        std::chrono::steady_clock::now(),
                    });
                    ++queuedJobs_;
                    UpdateMaximum(maximumQueueDepth_, queue_.size());
                }
                catch (...)
                {
                    lane->queued = false;
                    lane->running = true;
                    executeInline = true;
                    ++inlineJobs_;
                }
            }
        }
        if (executeInline)
        {
            RunJob(job, result, lane, group, true);
        }
        else
        {
            workAvailable_.notify_one();
        }
    }
    ownerWork.Run();
    const auto barrierStarted = std::chrono::steady_clock::now();
    group.Wait();
    frameBarrierWaitNanoseconds_.fetch_add(
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::steady_clock::now() - barrierStarted)
                                       .count()),
        std::memory_order_relaxed);
    return true;
}

void ApplicationSessionScheduler::Stop() noexcept
{
    {
        std::lock_guard lock(mutex_);
        if (stopping_ && workers_.empty())
        {
            return;
        }
        stopping_ = true;
        for (auto &[id, lane] : lanes_)
        {
            (void)id;
            lane->accepting = false;
        }
        for (QueuedJob &queued : queue_)
        {
            queued.lane->queued = false;
            CancelQueuedJob(queued);
        }
        queue_.clear();
    }
    workAvailable_.notify_all();
    for (std::thread &worker : workers_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    workers_.clear();
    std::lock_guard lock(mutex_);
    lanes_.clear();
}

std::size_t ApplicationSessionScheduler::WorkerCount() const noexcept
{
    std::lock_guard lock(mutex_);
    return workers_.size();
}

std::size_t ApplicationSessionScheduler::QueueCapacity() const noexcept
{
    return queueCapacity_;
}

ApplicationSessionSchedulerMetrics ApplicationSessionScheduler::Metrics() const noexcept
{
    return {
        queuedJobs_.load(),
        inlineJobs_.load(),
        completedJobs_.load(),
        cancelledJobs_.load(),
        staleJobs_.load(),
        rejectedConcurrentJobs_.load(),
        workerWakeups_.load(),
        maximumQueueDepth_.load(),
        maximumActiveWorkers_.load(),
        queueWaitNanoseconds_.load(),
        workerWallNanoseconds_.load(),
        frameBarrierWaitNanoseconds_.load(),
    };
}

std::size_t ApplicationSessionScheduler::RecommendedWorkerCount() noexcept
{
    constexpr std::size_t ReservedHardwareThreads = 2;
    constexpr std::size_t MaximumWorkerThreads = 8;
    const std::size_t hardwareThreads = std::thread::hardware_concurrency();
    if (hardwareThreads <= ReservedHardwareThreads)
    {
        return 1;
    }
    return (std::min)(hardwareThreads - ReservedHardwareThreads, MaximumWorkerThreads);
}

void ApplicationSessionScheduler::WorkerLoop() noexcept
{
    for (;;)
    {
        std::optional<QueuedJob> queued;
        {
            std::unique_lock lock(mutex_);
            workAvailable_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty())
            {
                if (stopping_)
                {
                    return;
                }
                continue;
            }
            queued.emplace(std::move(queue_.front()));
            queue_.pop_front();
            queued->lane->queued = false;
            queued->lane->running = true;
            ++workerWakeups_;
        }
        queueWaitNanoseconds_.fetch_add(
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::steady_clock::now() - queued->queuedAt)
                                           .count()),
            std::memory_order_relaxed);
        RunJob(queued->job, *queued->result, queued->lane, *queued->group, false);
    }
}

void ApplicationSessionScheduler::RunJob(const SessionAdvanceJob &job, SessionAdvanceResult &result,
                                         const std::shared_ptr<Lane> &lane, FrameGroup &group,
                                         bool executedInline) noexcept
{
    const auto started = std::chrono::steady_clock::now();
    const auto cpuStarted = CurrentThreadCpuTime();
    if (!executedInline)
    {
        const std::uint64_t active = activeWorkers_.fetch_add(1, std::memory_order_relaxed) + 1;
        UpdateMaximum(maximumActiveWorkers_, active);
    }
    ApplicationSessionScheduler *const previousScheduler = activeScheduler_;
    activeScheduler_ = this;
    result.orderedEffects_.Clear();
    result.orderedCommitAttempted_ = false;
    result.orderedCommitWallTime_ = {};
    bool succeeded = job.advance != nullptr && job.advance(job.context);
    if (succeeded && job.buildOrderedEffects != nullptr)
    {
        succeeded = job.buildOrderedEffects(job.context, result.orderedEffects_);
    }
    if (!succeeded)
    {
        result.orderedEffects_.Clear();
    }
    activeScheduler_ = previousScheduler;
    result.wallTime_ = std::chrono::steady_clock::now() - started;
    const auto cpuFinished = CurrentThreadCpuTime();
    result.cpuTime_ =
        cpuFinished >= cpuStarted ? cpuFinished - cpuStarted : std::chrono::nanoseconds{};
    result.executedInline_ = executedInline;
    result.workerStageCount_ = 1;
    result.status_ = succeeded ? SessionAdvanceStatus::Succeeded : SessionAdvanceStatus::Failed;
    {
        std::lock_guard lock(mutex_);
        lane->running = false;
        lane->idle.notify_all();
    }
    ++completedJobs_;
    if (!executedInline)
    {
        --activeWorkers_;
        workerWallNanoseconds_.fetch_add(static_cast<std::uint64_t>(result.wallTime_.count()),
                                         std::memory_order_relaxed);
    }
    group.Complete();
}

void ApplicationSessionScheduler::CancelQueuedJob(QueuedJob &queued) noexcept
{
    queued.result->status_ = SessionAdvanceStatus::Cancelled;
    queued.result->orderedEffects_.Clear();
    ++cancelledJobs_;
    queued.group->Complete();
}
// Non-Windows implementation of the .ini profile shim.
// Stores the file as UTF-8 on disk; sections/keys are matched case-insensitively.
#ifndef _WIN32

namespace
{
// Convert with the source's explicit length (no trailing null), so the
// converters never write a terminator one past the string's logical end.
std::string Narrow(LPCWSTR s)
{
    if (!s)
        return std::string();
    const int len = static_cast<int>(wcslen(s));
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return std::string();
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, len, out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Widen(const std::string &s)
{
    if (s.empty())
        return std::wstring();
    const int len = static_cast<int>(s.size());
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), len, nullptr, 0);
    if (n <= 0)
        return std::wstring();
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), len, out.data(), n);
    return out;
}

std::string Trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
        ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
        --b;
    return s.substr(a, b - a);
}

bool IEquals(const std::string &a, const std::string &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

struct Entry
{
    std::string key, value;
};
struct Section
{
    std::string name;
    std::vector<Entry> entries;
};

std::vector<Section> Load(const std::string &path)
{
    std::vector<Section> sections;
    FILE *fp = std::fopen(MuResolvePath(path.c_str()).c_str(), "rb");
    if (!fp)
        return sections;

    std::string content;
    char buf[4096];
    size_t r;
    while ((r = std::fread(buf, 1, sizeof(buf), fp)) > 0)
        content.append(buf, r);
    std::fclose(fp);

    Section *current = nullptr;
    size_t pos = 0;
    while (pos <= content.size())
    {
        size_t nl = content.find('\n', pos);
        const std::string raw =
            content.substr(pos, (nl == std::string::npos ? content.size() : nl) - pos);
        const std::string line = Trim(raw);
        if (nl == std::string::npos && line.empty())
            break;
        pos = (nl == std::string::npos) ? content.size() + 1 : nl + 1;

        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;

        if (line.front() == '[' && line.back() == ']')
        {
            sections.push_back(Section{line.substr(1, line.size() - 2), {}});
            current = &sections.back();
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos || !current)
            continue;
        current->entries.push_back(Entry{Trim(line.substr(0, eq)), Trim(line.substr(eq + 1))});
    }
    return sections;
}

bool Store(const std::string &path, const std::vector<Section> &sections)
{
    FILE *fp = std::fopen(MuResolvePath(path.c_str()).c_str(), "wb");
    if (!fp)
        return false;
    for (const Section &s : sections)
    {
        std::fprintf(fp, "[%s]\n", s.name.c_str());
        for (const Entry &e : s.entries)
            std::fprintf(fp, "%s=%s\n", e.key.c_str(), e.value.c_str());
        std::fputc('\n', fp);
    }
    std::fclose(fp);
    return true;
}

// Returns the value for app/key, or nullptr if absent.
const std::string *Find(const std::vector<Section> &sections, const std::string &app,
                        const std::string &key)
{
    for (const Section &s : sections)
    {
        if (!IEquals(s.name, app))
            continue;
        for (const Entry &e : s.entries)
            if (IEquals(e.key, key))
                return &e.value;
    }
    return nullptr;
}
} // namespace

DWORD GetPrivateProfileStringW(LPCWSTR lpAppName, LPCWSTR lpKeyName, LPCWSTR lpDefault,
                               LPWSTR lpReturnedString, DWORD nSize, LPCWSTR lpFileName)
{
    if (!lpReturnedString || nSize == 0)
        return 0;

    const std::vector<Section> sections = Load(Narrow(lpFileName));
    const std::string *found =
        (lpAppName && lpKeyName) ? Find(sections, Narrow(lpAppName), Narrow(lpKeyName)) : nullptr;

    const std::wstring value =
        found ? Widen(*found) : (lpDefault ? std::wstring(lpDefault) : std::wstring());

    DWORD copied = static_cast<DWORD>(value.size());
    if (copied > nSize - 1)
        copied = nSize - 1; // truncate, leaving room for the null
    std::wmemcpy(lpReturnedString, value.c_str(), copied);
    lpReturnedString[copied] = L'\0';
    return copied;
}

UINT GetPrivateProfileIntW(LPCWSTR lpAppName, LPCWSTR lpKeyName, INT nDefault, LPCWSTR lpFileName)
{
    const std::vector<Section> sections = Load(Narrow(lpFileName));
    const std::string *found =
        (lpAppName && lpKeyName) ? Find(sections, Narrow(lpAppName), Narrow(lpKeyName)) : nullptr;
    if (!found)
        return static_cast<UINT>(nDefault);
    return static_cast<UINT>(std::strtol(found->c_str(), nullptr, 10));
}

BOOL WritePrivateProfileStringW(LPCWSTR lpAppName, LPCWSTR lpKeyName, LPCWSTR lpString,
                                LPCWSTR lpFileName)
{
    if (!lpAppName)
        return FALSE;
    const std::string path = Narrow(lpFileName);
    const std::string app = Narrow(lpAppName);
    std::vector<Section> sections = Load(path);

    // Null key: delete the whole section.
    if (!lpKeyName)
    {
        for (size_t i = 0; i < sections.size(); ++i)
            if (IEquals(sections[i].name, app))
            {
                sections.erase(sections.begin() + i);
                break;
            }
        return Store(path, sections) ? TRUE : FALSE;
    }

    const std::string key = Narrow(lpKeyName);

    Section *sec = nullptr;
    for (Section &s : sections)
        if (IEquals(s.name, app))
        {
            sec = &s;
            break;
        }

    // Null value: delete the key (if its section exists).
    if (!lpString)
    {
        if (sec)
            for (size_t i = 0; i < sec->entries.size(); ++i)
                if (IEquals(sec->entries[i].key, key))
                {
                    sec->entries.erase(sec->entries.begin() + i);
                    break;
                }
        return Store(path, sections) ? TRUE : FALSE;
    }

    if (!sec)
    {
        sections.push_back(Section{app, {}});
        sec = &sections.back();
    }

    const std::string value = Narrow(lpString);
    for (Entry &e : sec->entries)
        if (IEquals(e.key, key))
        {
            e.value = value;
            return Store(path, sections) ? TRUE : FALSE;
        }

    sec->entries.push_back(Entry{key, value});
    return Store(path, sections) ? TRUE : FALSE;
}

#endif // !_WIN32

CTimeCheck::CTimeCheck()
{
}

CTimeCheck::~CTimeCheck()
{
}

int CTimeCheck::CheckIndex(int index)
{
    for (int i = 0; i < (int)stl_Time.size(); i++)
    {
        if (stl_Time[i].iIndex == index)
            return i;
    }

    TimeCheck T;

    T.iIndex = index;
    T.iBackupTime = 0;
    T.bTimeCheck = true;

    stl_Time.push_back(T);

    return stl_Time.size() - 1;
}

bool CTimeCheck::GetTimeCheck(double worldTime, int index, int DelayTime)
{
    int I = CheckIndex(index);

    const auto presentTime = worldTime;

    if (stl_Time[I].bTimeCheck)
    {
        stl_Time[I].iBackupTime = presentTime;
        stl_Time[I].bTimeCheck = false;
    }

    if (stl_Time[I].iBackupTime + DelayTime <= presentTime)
    {
        stl_Time[I].bTimeCheck = true;
        return true;
    }

    return false;
}

void CTimeCheck::DeleteTimeIndex(int index)
{
    for (int i = 0; i < (int)stl_Time.size(); i++)
    {
        if (stl_Time[i].iIndex == index)
        {
            stl_Time.erase(stl_Time.begin() + i);
            break;
        }
    }
}

CTimer::CTimer()
{
    m_startTime = Clock::now();
    m_absStartTime = m_startTime;
}

double CTimer::GetTimeElapsed()
{
    auto now = Clock::now();
    auto elapsed = std::chrono::duration<double, std::milli>(now - m_startTime);
    return elapsed.count(); // Return elapsed time in milliseconds
}

double CTimer::GetAbsTime()
{
    auto now = Clock::now();
    auto elapsed = std::chrono::duration<double, std::milli>(now - m_absStartTime);
    return elapsed.count(); // Return absolute time in milliseconds
}

void CTimer::ResetTimer()
{
    m_startTime = Clock::now(); // Reset start time to now
}

void CTimer2::SetTimer(unsigned int delay)
{
    m_delay = delay;
    m_startTickCount = 0;
}

unsigned int CTimer2::GetDelay() const
{
    return m_delay;
}

void CTimer2::ResetTimer()
{
    m_startTickCount = 0;
}

void CTimer2::UpdateTime(StartTickTime &startTickTime)
{
    using SteadyClock = std::chrono::steady_clock;
    if (m_delay == 0)
    {
        m_timeReached = true;
    }
    else
    {
        m_timeReached = false;
        auto now = SteadyClock::now();

        if (m_startTickCount == 0)
        {
            startTickTime = now;
            m_startTickCount = 1; // Mark initialization
            return;
        }

        if (now - startTickTime > std::chrono::milliseconds(m_delay))
        {
            startTickTime = now;
            m_timeReached = true;
        }
    }
}

bool CTimer2::IsTime() const
{
    return m_timeReached;
}
