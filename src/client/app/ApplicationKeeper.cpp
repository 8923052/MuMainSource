#include "app/ApplicationKeeper.h"
#include "app/Application.h"
#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationNetwork.h"
#include "app/ManagedBindingUnit.h"
#include "data/GameData.h"
#include "domain/Quests.h"
#include "render/Sprites.h"
#include "session/SessionRuntime.h"
#include "ui/runtime/UiRuntime.h"

ApplicationSupportCalls::ApplicationSupportCalls(ApplicationKeeper &keeper) noexcept
    : applicationKeeper_(keeper)
{
}

ApplicationLegacyCalls::ApplicationLegacyCalls(ApplicationKeeper &keeper) noexcept
    : ApplicationSupportCalls(keeper), Bitmaps(keeper.BitmapRegistry()),
      KeyState(keeper.PlatformStorageRef().KeyState),
      FontHeight(keeper.PlatformStorageRef().FontHeight), First(keeper.PlatformStorageRef().First),
      FirstTime(keeper.PlatformStorageRef().FirstTime),
      g_dwBKConv(keeper.PlatformStorageRef().g_dwBKConv),
      g_dwBKSent(keeper.PlatformStorageRef().g_dwBKSent),
      g_bForceIMEConv(keeper.PlatformStorageRef().g_bForceIMEConv),
      g_bForceIMESent(keeper.PlatformStorageRef().g_bForceIMESent),
      g_bIMEBlock(keeper.PlatformStorageRef().g_bIMEBlock),
      g_iChatInputType(keeper.LegacyRuntimeStorageRef().g_iChatInputType),
      pMultiLanguage(keeper.LegacyRuntimeStorageRef().pMultiLanguage),
      g_fScreenRate_x(keeper.LegacyRuntimeStorageRef().g_fScreenRate_x),
      g_fScreenRate_y(keeper.LegacyRuntimeStorageRef().g_fScreenRate_y),
      g_bShowPath(keeper.LegacyRuntimeStorageRef().g_bShowPath),
      frameTimerScheduler_(keeper.FrameTimerSchedulerObject()),
      RendomMemoryDump(keeper.LegacyRuntimeStorageRef().RendomMemoryDump),
      ItemAttRibuteMemoryDump(keeper.LegacyRuntimeStorageRef().ItemAttRibuteMemoryDump),
      SkillAttribute(keeper.LegacyRuntimeStorageRef().SkillAttribute),
      ItemAttribute(keeper.LegacyRuntimeStorageRef().ItemAttribute),
      RandomTable(keeper.LegacyRuntimeStorageRef().RandomTable),
      bootstrapServerIp(keeper.LegacyRuntimeStorageRef().bootstrapServerIp),
      bootstrapServerPort(keeper.LegacyRuntimeStorageRef().bootstrapServerPort),
      m_SoundOnOff(keeper.LegacyRuntimeStorageRef().m_SoundOnOff),
      m_MusicOnOff(keeper.LegacyRuntimeStorageRef().m_MusicOnOff),
      g_lpszCmdURL(keeper.LegacyRuntimeStorageRef().g_lpszCmdURL),
      g_LoginSceneOffsetX(keeper.LoginSceneOffsetX()),
      g_LoginSceneOffsetY(keeper.LoginSceneOffsetY()),
      g_LoginSceneOffsetZ(keeper.LoginSceneOffsetZ()),
      g_LoginSceneAnglePitch(keeper.LoginSceneAnglePitch()),
      g_LoginSceneAngleYaw(keeper.LoginSceneAngleYaw())
{
}

ApplicationKeeper::ApplicationKeeper()
    : gameDataStorage_(std::make_unique<ApplicationGameDataStorage>()), graphicsStorage_(*this),
      diagnosticsStorage_(*this)
{
}

ApplicationKeeper::~ApplicationKeeper() = default;

bool ApplicationKeeper::IsOwnerThread() const noexcept
{
    return std::this_thread::get_id() == ownerThread_;
}

bool ApplicationKeeper::RegisterModernUiRuntime(UI::Modern::RmlUiRuntime &runtime) noexcept
{
    return RegisterShortcut(modernUiRuntime_, runtime);
}

UI::Modern::RmlUiRuntime *ApplicationKeeper::ModernUiRuntime() const noexcept
{
    return modernUiRuntime_;
}

void ApplicationKeeper::DeferErrorMessage(const wchar_t *errorMessage, bool forceDestroy) noexcept
{
    std::lock_guard lock(deferredErrorMutex_);
    if (!deferredError_.has_value())
    {
        DeferredErrorMessage error;
        if (errorMessage != nullptr)
        {
            std::wcsncpy(error.text.data(), errorMessage, error.text.size() - 1);
        }
        error.forceDestroy = forceDestroy;
        deferredError_.emplace(std::move(error));
        deferredErrorPending_.store(true, std::memory_order_release);
    }
}

std::optional<ApplicationKeeper::DeferredErrorMessage> ApplicationKeeper::TakeDeferredErrorMessage()
{
    if (!deferredErrorPending_.load(std::memory_order_acquire))
    {
        return std::nullopt;
    }
    std::lock_guard lock(deferredErrorMutex_);
    std::optional<DeferredErrorMessage> result = std::move(deferredError_);
    deferredError_.reset();
    deferredErrorPending_.store(false, std::memory_order_release);
    return result;
}

const QuestScriptData *ApplicationKeeper::QuestScripts() const noexcept
{
    return questScripts_.get();
}

bool ApplicationKeeper::PublishQuestScripts(std::unique_ptr<const QuestScriptData> scripts) noexcept
{
    if (questScripts_ == nullptr)
    {
        questScripts_ = std::move(scripts);
    }
    return questScripts_ != nullptr;
}

bool ApplicationKeeper::IsReady() const noexcept
{
    return state_ == State::Ready;
}

bool ApplicationKeeper::IsShutdown() const noexcept
{
    return state_ == State::Shutdown;
}

bool ApplicationKeeper::CompleteLinks() noexcept
{
    if (state_ != State::Constructing || linkError_ || applicationAudio_ == nullptr ||
        applicationDiagnostics_ == nullptr || applicationFrame_ == nullptr ||
        applicationConfigUnit_ == nullptr || applicationNetwork_ == nullptr ||
        managedBindingUnit_ == nullptr || applicationLoopUnit_ == nullptr ||
        !applicationConfigUnit_->IsLoaded() || applicationSessionRuntime_ == nullptr ||
        appWindow_ == nullptr || newKeyInput_ == nullptr || input_ == nullptr ||
        gameData_ == nullptr || sessionWorkspace_ == nullptr || sessionConfigStore_ == nullptr ||
        !sessionConfigStore_->IsLoaded() || sessionManager_ == nullptr || compositor_ == nullptr)
    {
        return false;
    }

    state_ = State::Ready;
    return true;
}

bool ApplicationKeeper::BeginShutdown() noexcept
{
    if (state_ != State::Ready || activeSessionCount_ != 0)
    {
        return false;
    }

    state_ = State::Shutdown;
    return true;
}

BOOL &ApplicationKeeper::MinimizedEnabled() noexcept
{
    return g_bMinimizedEnabled;
}

const BOOL &ApplicationKeeper::MinimizedEnabled() const noexcept
{
    return g_bMinimizedEnabled;
}

ApplicationConfigValues &ApplicationKeeper::ApplicationConfig() noexcept
{
    return applicationConfig_;
}

const ApplicationConfigValues &ApplicationKeeper::ApplicationConfig() const noexcept
{
    return applicationConfig_;
}

void ApplicationKeeper::CollectIdleAssets(std::uint64_t nowMilliseconds) noexcept
{
    const int idleSeconds = applicationConfig_.sharedAssetIdleSeconds;
    if (idleSeconds == 0 || nowMilliseconds < nextAssetCollectionMilliseconds_)
    {
        return;
    }

    constexpr std::uint64_t CollectionIntervalMilliseconds = 1000;
    nextAssetCollectionMilliseconds_ = nowMilliseconds + CollectionIntervalMilliseconds;
    const std::uint64_t idleTimeoutMilliseconds = static_cast<std::uint64_t>(idleSeconds) * 1000;
    const auto collect = [nowMilliseconds, idleTimeoutMilliseconds](auto &assets,
                                                                    std::size_t &bytes) {
        for (auto current = assets.begin(); current != assets.end();)
        {
            auto &retained = current->second;
            if (retained.asset.use_count() != 1)
            {
                retained.unusedSinceMilliseconds = 0;
                ++current;
                continue;
            }
            if (retained.unusedSinceMilliseconds == 0)
            {
                retained.unusedSinceMilliseconds = nowMilliseconds;
                ++current;
                continue;
            }
            if (nowMilliseconds - retained.unusedSinceMilliseconds < idleTimeoutMilliseconds)
            {
                ++current;
                continue;
            }
            bytes -= retained.bytes;
            current = assets.erase(current);
        }
    };

    collect(modelAssets_, retainedModelAssetBytes_);
    collect(terrainGeometryAssets_, retainedTerrainGeometryBytes_);
    collect(worldTerrainAssets_, retainedWorldTerrainBytes_);
    BitmapRegistry().CollectIdleOwnerAssets(nowMilliseconds, idleTimeoutMilliseconds);
}

const std::wstring &ApplicationKeeper::BootstrapServerIp() const noexcept
{
    return legacyRuntimeStorage_.bootstrapServerIp;
}

WORD ApplicationKeeper::BootstrapServerPort() const noexcept
{
    return legacyRuntimeStorage_.bootstrapServerPort;
}

wchar_t (&ApplicationKeeper::AbuseFilterStorage() noexcept)[MAX_FILTERS][20]
{
    return gameDataStorage_->AbuseFilter;
}

wchar_t (&ApplicationKeeper::AbuseNameFilterStorage() noexcept)[MAX_NAMEFILTERS][20]
{
    return gameDataStorage_->AbuseNameFilter;
}

int &ApplicationKeeper::AbuseFilterCount() noexcept
{
    return gameDataStorage_->AbuseFilterNumber;
}

int &ApplicationKeeper::AbuseNameFilterCount() noexcept
{
    return gameDataStorage_->AbuseNameFilterNumber;
}

GATE_ATTRIBUTE *&ApplicationKeeper::GateAttributes() noexcept
{
    return gameDataStorage_->GateAttribute;
}

MONSTER_SCRIPT (&ApplicationKeeper::MonsterScripts() noexcept)[MAX_MONSTER]
{
    return gameDataStorage_->MonsterScript;
}

Script_Skill (&ApplicationKeeper::MonsterSkills() noexcept)[MODEL_MONSTER_END]
{
    return gameDataStorage_->MonsterSkill;
}

CLASS_ATTRIBUTE (&ApplicationKeeper::ClassAttributes() noexcept)[MAX_CLASS]
{
    return gameDataStorage_->ClassAttribute;
}

int &ApplicationKeeper::EditMonsterCount() noexcept
{
    return gameDataStorage_->EditMonsterNumber;
}

std::wstring &ApplicationKeeper::AssetLanguage() noexcept
{
    return gameDataStorage_->g_strSelectedML;
}

const std::wstring &ApplicationKeeper::AssetLanguage() const noexcept
{
    return gameDataStorage_->g_strSelectedML;
}

float &ApplicationKeeper::LoginSceneOffsetX() noexcept
{
    return cameraStorage_.g_LoginSceneOffsetX;
}

float &ApplicationKeeper::LoginSceneOffsetY() noexcept
{
    return cameraStorage_.g_LoginSceneOffsetY;
}

float &ApplicationKeeper::LoginSceneOffsetZ() noexcept
{
    return cameraStorage_.g_LoginSceneOffsetZ;
}

float &ApplicationKeeper::LoginSceneAnglePitch() noexcept
{
    return cameraStorage_.g_LoginSceneAnglePitch;
}

float &ApplicationKeeper::LoginSceneAngleYaw() noexcept
{
    return cameraStorage_.g_LoginSceneAngleYaw;
}

HWND &ApplicationKeeper::PlatformWindowHandle() noexcept
{
    return platformStorage_.g_hWnd;
}

HDC &ApplicationKeeper::PlatformDeviceContext() noexcept
{
    return platformStorage_.g_hDC;
}

HFONT &ApplicationKeeper::PlatformUiFont() noexcept
{
    return platformStorage_.g_hFont;
}

HFONT &ApplicationKeeper::PlatformBoldFont() noexcept
{
    return platformStorage_.g_hFontBold;
}

HFONT &ApplicationKeeper::PlatformBigFont() noexcept
{
    return platformStorage_.g_hFontBig;
}

HFONT &ApplicationKeeper::PlatformFixedFont() noexcept
{
    return platformStorage_.g_hFixFont;
}

unsigned int &ApplicationKeeper::PlatformWindowWidth() noexcept
{
    return platformStorage_.WindowWidth;
}

unsigned int &ApplicationKeeper::PlatformWindowHeight() noexcept
{
    return platformStorage_.WindowHeight;
}

bool &ApplicationKeeper::PlatformDestroyRequested() noexcept
{
    return platformStorage_.Destroy;
}

CErrorReport &ApplicationKeeper::ErrorReport() noexcept
{
    return diagnosticsStorage_.g_ErrorReport;
}

CmuConsoleDebug &ApplicationKeeper::ConsoleDebug() noexcept
{
    return diagnosticsStorage_.consoleDebug;
}

wchar_t (&ApplicationKeeper::ExecutableVersion() noexcept)[11]
{
    return diagnosticsStorage_.m_ExeVersion;
}

SessionConfigCatalog &ApplicationKeeper::SessionProfiles() noexcept
{
    return sessionProfiles_;
}

const SessionConfigCatalog &ApplicationKeeper::SessionProfiles() const noexcept
{
    return sessionProfiles_;
}

ApplicationAudio *ApplicationKeeper::ApplicationAudioUnit() const noexcept
{
    return state_ == State::Ready ? applicationAudio_ : nullptr;
}

ApplicationAudio &ApplicationKeeper::ApplicationAudioObject() noexcept
{
    if (applicationAudio_ == nullptr)
    {
        std::terminate();
    }
    return *applicationAudio_;
}

ApplicationDiagnostics *ApplicationKeeper::ApplicationDiagnosticsUnit() const noexcept
{
    return state_ == State::Ready ? applicationDiagnostics_ : nullptr;
}

ApplicationFrameUnit *ApplicationKeeper::ApplicationFrameUnitShortcut() const noexcept
{
    return state_ == State::Ready ? applicationFrame_ : nullptr;
}

ApplicationNetwork *ApplicationKeeper::ApplicationNetworkUnit() const noexcept
{
    return state_ == State::Ready ? applicationNetwork_ : nullptr;
}

ApplicationLoopUnit *ApplicationKeeper::ApplicationLoopUnitShortcut() const noexcept
{
    return state_ == State::Ready ? applicationLoopUnit_ : nullptr;
}

ManagedBindingUnit *ApplicationKeeper::ManagedBindingUnitShortcut() const noexcept
{
    return state_ == State::Ready ? managedBindingUnit_ : nullptr;
}

AppWindow *ApplicationKeeper::AppWindowUnit() const noexcept
{
    return state_ == State::Ready ? appWindow_ : nullptr;
}

SEASON3B::CNewKeyInput *ApplicationKeeper::NewKeyInputUnit() const noexcept
{
    return state_ == State::Ready ? newKeyInput_ : nullptr;
}

CInput *ApplicationKeeper::InputUnit() const noexcept
{
    return state_ == State::Ready ? input_ : nullptr;
}

ApplicationSessionRuntime *ApplicationKeeper::ApplicationSessionRuntimeUnit() const noexcept
{
    return state_ == State::Ready ? applicationSessionRuntime_ : nullptr;
}

GameData *ApplicationKeeper::GameDataUnit() const noexcept
{
    return state_ == State::Ready ? gameData_ : nullptr;
}

SessionWorkspace *ApplicationKeeper::SessionWorkspaceUnit() const noexcept
{
    return state_ == State::Ready ? sessionWorkspace_ : nullptr;
}

SessionConfigStore *ApplicationKeeper::SessionConfigStoreUnit() const noexcept
{
    return state_ == State::Ready ? sessionConfigStore_ : nullptr;
}

SessionConfigStore &ApplicationKeeper::SessionConfigStoreObject() noexcept
{
    if (sessionConfigStore_ == nullptr)
    {
        std::terminate();
    }
    return *sessionConfigStore_;
}

SessionManager *ApplicationKeeper::SessionManagerUnit() const noexcept
{
    return state_ == State::Ready ? sessionManager_ : nullptr;
}

Compositor *ApplicationKeeper::CompositorUnit() const noexcept
{
    return state_ == State::Ready ? compositor_ : nullptr;
}

std::size_t ApplicationKeeper::ActiveSessionCount() const noexcept
{
    return activeSessionCount_;
}

SharedCharacterPool &ApplicationKeeper::SharedCharacters() noexcept
{
    return sharedCharacters_;
}

const SharedCharacterPool &ApplicationKeeper::SharedCharacters() const noexcept
{
    return sharedCharacters_;
}

bool ApplicationKeeper::PrepareRenderTapeFrame(const ApplicationFramePlan &plan) noexcept
{
    std::optional<ApplicationRenderTapeStorage::FrameLease> prepared =
        renderTapeStorage_.PrepareFrame(plan);
    if (!prepared.has_value())
    {
        preparedRenderTapeFrame_.reset();
        return false;
    }
    preparedRenderTapeFrame_ = std::move(*prepared);
    return true;
}

std::optional<SessionRenderTapeRecording> ApplicationKeeper::AcquireRenderTapeRecording(
    SessionId id, SessionGeneration generation, std::uint64_t surfaceGeneration,
    std::uint32_t viewportWidth, std::uint32_t viewportHeight) noexcept
{
    if (!preparedRenderTapeFrame_.has_value())
    {
        return std::nullopt;
    }
    return preparedRenderTapeFrame_->AcquireChildBlock(id, generation, surfaceGeneration,
                                                       viewportWidth, viewportHeight);
}

bool ApplicationKeeper::LoadApplicationBitmap(const wchar_t *fileName, std::uint32_t logicalIndex,
                                              LegacyTextureFilter filter,
                                              LegacyTextureWrap wrapMode, bool fullPath)
{
    if (fileName == nullptr || !IsValid(filter) || !IsValid(wrapMode))
    {
        return false;
    }
    wchar_t resolvedPath[256] = {};
    if (fullPath)
    {
        wcscpy_s(resolvedPath, fileName);
    }
    else
    {
        wcscpy_s(resolvedPath, L"Data\\");
        wcscat_s(resolvedPath, fileName);
    }
    return BitmapRegistry().LoadNamedImage(logicalIndex, resolvedPath, ErrorReport(), filter,
                                           wrapMode);
}

void ApplicationKeeper::DeleteApplicationBitmap(std::uint32_t logicalIndex, bool force) noexcept
{
    BitmapRegistry().UnloadImage(logicalIndex, force);
}

bool ApplicationKeeper::RegisterApplicationAudio(ApplicationAudio &audio) noexcept
{
    return RegisterShortcut(applicationAudio_, audio);
}

bool ApplicationKeeper::RegisterApplicationDiagnostics(ApplicationDiagnostics &diagnostics) noexcept
{
    return RegisterShortcut(applicationDiagnostics_, diagnostics);
}

bool ApplicationKeeper::RegisterApplicationFrame(ApplicationFrameUnit &frame) noexcept
{
    return RegisterShortcut(applicationFrame_, frame);
}

bool ApplicationKeeper::RegisterApplicationNetwork(ApplicationNetwork &network) noexcept
{
    return RegisterShortcut(applicationNetwork_, network);
}

bool ApplicationKeeper::RegisterApplicationLoop(ApplicationLoopUnit &loop) noexcept
{
    return RegisterShortcut(applicationLoopUnit_, loop);
}

bool ApplicationKeeper::RegisterManagedBindingUnit(ManagedBindingUnit &bindings) noexcept
{
    return RegisterShortcut(managedBindingUnit_, bindings);
}

bool ApplicationKeeper::RegisterApplicationConfig(ApplicationConfigUnit &config) noexcept
{
    return RegisterShortcut(applicationConfigUnit_, config);
}

bool ApplicationKeeper::RegisterApplicationSessionRuntime(
    ApplicationSessionRuntime &runtime) noexcept
{
    return RegisterShortcut(applicationSessionRuntime_, runtime);
}

bool ApplicationKeeper::RegisterAppWindow(AppWindow &window) noexcept
{
    return RegisterShortcut(appWindow_, window);
}

bool ApplicationKeeper::RegisterNewKeyInput(SEASON3B::CNewKeyInput &input) noexcept
{
    return RegisterShortcut(newKeyInput_, input);
}

bool ApplicationKeeper::RegisterInput(CInput &input) noexcept
{
    return RegisterShortcut(input_, input);
}

bool ApplicationKeeper::RegisterGameData(GameData &gameData) noexcept
{
    return RegisterShortcut(gameData_, gameData);
}

bool ApplicationKeeper::RegisterSessionWorkspace(SessionWorkspace &workspace) noexcept
{
    return RegisterShortcut(sessionWorkspace_, workspace);
}

bool ApplicationKeeper::RegisterSessionConfigStore(SessionConfigStore &store) noexcept
{
    return RegisterShortcut(sessionConfigStore_, store);
}

bool ApplicationKeeper::RegisterSessionManager(SessionManager &sessions) noexcept
{
    return RegisterShortcut(sessionManager_, sessions);
}

bool ApplicationKeeper::RegisterCompositor(Compositor &compositor) noexcept
{
    return RegisterShortcut(compositor_, compositor);
}

void ApplicationKeeper::MarkLinkError() noexcept
{
    linkError_ = true;
}

ApplicationGameDataStorage &ApplicationKeeper::GameDataStorageRef() noexcept
{
    return *gameDataStorage_;
}

const ApplicationGameDataStorage &ApplicationKeeper::GameDataStorageRef() const noexcept
{
    return *gameDataStorage_;
}

ApplicationDiagnosticsStorage &ApplicationKeeper::DiagnosticsStorageRef() noexcept
{
    return diagnosticsStorage_;
}

ManagedBindingStorage &ApplicationKeeper::ManagedBindingsRef() noexcept
{
    return managedBindings_;
}

ApplicationNetworkStorage &ApplicationKeeper::NetworkStorageRef() noexcept
{
    return networkStorage_;
}

ApplicationAudioStorage &ApplicationKeeper::AudioStorageRef() noexcept
{
    return audioStorage_;
}

ApplicationFrameStorage &ApplicationKeeper::FrameStorageRef() noexcept
{
    return frameStorage_;
}

ApplicationPlatformStorage &ApplicationKeeper::PlatformStorageRef() noexcept
{
    return platformStorage_;
}

ApplicationGraphicsStorage &ApplicationKeeper::GraphicsStorageRef() noexcept
{
    return graphicsStorage_;
}

CUIRenderText &ApplicationKeeper::GraphicsRenderText() noexcept
{
    return graphicsStorage_.renderText;
}

ApplicationLegacyRuntimeStorage &ApplicationKeeper::LegacyRuntimeStorageRef() noexcept
{
    return legacyRuntimeStorage_;
}

Core::Time::FrameTimerScheduler &ApplicationKeeper::FrameTimerSchedulerObject() noexcept
{
    return frameTimerScheduler_;
}

CGlobalBitmap &ApplicationKeeper::BitmapRegistry() noexcept
{
    return graphicsStorage_.Bitmaps;
}

ApplicationNetwork &ApplicationKeeper::ApplicationNetworkForConstruction() noexcept
{
    if (applicationNetwork_ == nullptr)
    {
        std::terminate();
    }
    return *applicationNetwork_;
}

ManagedBindingUnit &ApplicationKeeper::ManagedBindingsForConstruction() noexcept
{
    if (managedBindingUnit_ == nullptr)
    {
        std::terminate();
    }
    return *managedBindingUnit_;
}

SessionManager &ApplicationKeeper::SessionManagerForConstruction() noexcept
{
    if (sessionManager_ == nullptr)
    {
        std::terminate();
    }
    return *sessionManager_;
}

ApplicationConfigUnit &ApplicationKeeper::ApplicationConfigForConstruction() noexcept
{
    if (applicationConfigUnit_ == nullptr)
    {
        std::terminate();
    }
    return *applicationConfigUnit_;
}

ApplicationAudio &ApplicationKeeper::ApplicationAudioForConstruction() noexcept
{
    if (applicationAudio_ == nullptr)
    {
        std::terminate();
    }
    return *applicationAudio_;
}

AppWindow &ApplicationKeeper::AppWindowForConstruction() noexcept
{
    if (appWindow_ == nullptr)
    {
        std::terminate();
    }
    return *appWindow_;
}

CInput &ApplicationKeeper::InputForConstruction() noexcept
{
    if (input_ == nullptr)
    {
        std::terminate();
    }
    return *input_;
}

bool ApplicationKeeper::AttachSession() noexcept
{
    if (state_ != State::Ready)
    {
        return false;
    }

    ++activeSessionCount_;
    return true;
}

void ApplicationKeeper::DetachSession() noexcept
{
    if (activeSessionCount_ > 0)
    {
        --activeSessionCount_;
    }
}
