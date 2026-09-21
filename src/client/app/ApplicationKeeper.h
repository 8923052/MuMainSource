#pragma once
#include "support/CoreMath.h"

#include "app/ApplicationAudio.h"
#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "app/AppWindow.h"
#include "app/ManagedBindingUnit.h"
#include "data/CharacterData.h"
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
#include "domain/Guild.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Quests.h"
#include "domain/WorldPresentation.h"
#include "domain/WorldSimulation.h"
#include "render/Assets.h"
#include "render/FrameTape.h"
#include "render/ModelGeometry.h"
#include "render/ModelResources.h"
#include "render/Sprites.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "session/SessionAudio.h"
#include "support/Camera.h"
#include "support/Scenes.h"
#include "ui/features/Activities/ActivitiesLogic.h"
#include "ui/features/Hud/HudLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/QuestNpc/QuestNpcLogic.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/session/UiSessionLogic.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <SDL3/SDL.h>
#include <string>
#include <thread>
#include <unordered_map>

namespace LoginSceneCameraDefaults
{
constexpr float OFFSET_X = -300.0f;
constexpr float OFFSET_Y = 650.0f;
constexpr float OFFSET_Z = 950.0f;
constexpr float ANGLE_PITCH = 40.0f;
constexpr float ANGLE_YAW = -5.0f;

constexpr float RENDER_TERRAIN_DIST = 3995.0f;
constexpr float RENDER_OBJECT_DIST = 5903.0f;
} // namespace LoginSceneCameraDefaults

struct ApplicationCameraStorage
{
    float g_LoginSceneOffsetX = LoginSceneCameraDefaults::OFFSET_X;
    float g_LoginSceneOffsetY = LoginSceneCameraDefaults::OFFSET_Y;
    float g_LoginSceneOffsetZ = LoginSceneCameraDefaults::OFFSET_Z;
    float g_LoginSceneAnglePitch = LoginSceneCameraDefaults::ANGLE_PITCH;
    float g_LoginSceneAngleYaw = LoginSceneCameraDefaults::ANGLE_YAW;
};

struct ApplicationPlatformStorage final
{
    SDL_Window *g_sdlWindow = nullptr;
    HWND g_hWnd = nullptr;
    HINSTANCE g_hInst = nullptr;
    HDC g_hDC = nullptr;
    HFONT g_hFont = nullptr;
    HFONT g_hFontBold = nullptr;
    HFONT g_hFontBig = nullptr;
    HFONT g_hFixFont = nullptr;
    bool Destroy = false;
    int g_iScreenSaverOldValue = 60 * 15;
    BOOL g_bUseWindowMode = TRUE;
    BOOL g_bUseFullscreenMode = FALSE;
    int g_iInactiveTime = 0;
    int g_iNoMouseTime = 0;
    int g_iInactiveWarning = 0;
    bool g_bWndActive = false;
    BOOL g_bInactiveTimeChecked = FALSE;
    double g_TargetFpsBeforeInactive = -1.0;
    bool g_HasInactiveFpsOverride = false;
    int OpenglWindowX = 0;
    int OpenglWindowY = 0;
    int OpenglWindowWidth = 1024;
    int OpenglWindowHeight = 768;
    unsigned int WindowWidth = 1024;
    unsigned int WindowHeight = 768;
    bool textInputActive = false;
    SDL_Rect lastTextInputArea{0, 0, 0, 0};
    BYTE newKeyInputStates[256]{};
    bool enterPressed = false;
    int KeyState[256]{};
    int FontHeight = 0;
    bool First = false;
    int FirstTime = 0;
    DWORD g_dwBKConv = IME_CMODE_ALPHANUMERIC;
    DWORD g_dwBKSent = IME_SMODE_NONE;
    BOOL g_bForceIMEConv = FALSE;
    BOOL g_bForceIMESent = FALSE;
    BOOL g_bIMEBlock = FALSE;
};

struct ApplicationGraphicsStorage final
{
    explicit ApplicationGraphicsStorage(ApplicationKeeper &keeper) noexcept : renderText(keeper)
    {
    }
    ApplicationGraphicsStorage(const ApplicationGraphicsStorage &) = delete;
    ApplicationGraphicsStorage &operator=(const ApplicationGraphicsStorage &) = delete;

    CGlobalBitmap Bitmaps;
    CUIRenderText renderText;
};

class CMultiLanguage;

struct ApplicationLegacyRuntimeStorage final
{
    int g_iChatInputType = 1;
    CMultiLanguage *pMultiLanguage = nullptr;
    float g_fScreenRate_x = 0.f;
    float g_fScreenRate_y = 0.f;
    bool g_bShowPath = false;
    BYTE *RendomMemoryDump = nullptr;
    ITEM_ATTRIBUTE *ItemAttRibuteMemoryDump = nullptr;
    SKILL_ATTRIBUTE *SkillAttribute = nullptr;
    ITEM_ATTRIBUTE *ItemAttribute = nullptr;
    int RandomTable[100]{};
    int rawMouseX = 1024 / 2;
    int rawMouseY = 768 / 2;
    std::wstring bootstrapServerIp = L"127.127.127.127";
    WORD bootstrapServerPort = 44406;
    int m_SoundOnOff = 0;
    int m_MusicOnOff = 0;
    wchar_t g_lpszCmdURL[50]{};
};

class ApplicationAudio;
class Application;
class BMD;
class ApplicationDiagnostics;
class ApplicationFrameUnit;
class ApplicationConfigUnit;
class ApplicationSessionRuntime;
class ApplicationKeeperTestPeer;
class ApplicationLoopUnit;
class ApplicationLegacyCalls;
class ApplicationNetwork;
class AppWindow;
class CGlobalBitmap;
class CUIRenderText;
class CInput;
class LegacyPlatformBridge;
class ManagedBindingUnit;
class GameData;
class SessionKeeper;
class SessionManager;
class SessionConfigStore;
class SessionWorkspace;
class Compositor;
struct BmdSharedAsset;
struct QuestScriptData;
class TerrainGeometryCache;
struct TerrainSharedGeometry;
struct WorldTerrainAsset;
class WorldResources;
namespace SEASON3B
{
class CNewKeyInput;
}
namespace UI::Modern
{
class RmlUiRuntime;
}

// Application-owned game catalog storage.
struct ApplicationGameDataStorage final
{
    ApplicationGameDataStorage() = default;
    ~ApplicationGameDataStorage()
    {
        delete[] GateAttribute;
    }

    ApplicationGameDataStorage(const ApplicationGameDataStorage &) = delete;
    ApplicationGameDataStorage &operator=(const ApplicationGameDataStorage &) = delete;
    ApplicationGameDataStorage(ApplicationGameDataStorage &&) = delete;
    ApplicationGameDataStorage &operator=(ApplicationGameDataStorage &&) = delete;

    // Exact former globals accepted for P1A.4 GameData ownership.
    wchar_t AbuseFilter[MAX_FILTERS][20]{};
    wchar_t AbuseNameFilter[MAX_NAMEFILTERS][20]{};
    int AbuseFilterNumber = 0;
    int AbuseNameFilterNumber = 0;
    GATE_ATTRIBUTE *GateAttribute = nullptr;
    MONSTER_SCRIPT MonsterScript[MAX_MONSTER]{};
    Script_Skill MonsterSkill[MODEL_MONSTER_END]{};
    CLASS_ATTRIBUTE ClassAttribute[MAX_CLASS]{};
    int EditMonsterNumber = 0;
    wchar_t g_aszMLSelection[MAX_LANGUAGE_NAME_LENGTH]{};
    std::wstring g_strSelectedML;
};

class ApplicationKeeper final
{
  public:
    ApplicationKeeper();
    ~ApplicationKeeper();

    ApplicationKeeper(const ApplicationKeeper &) = delete;
    ApplicationKeeper &operator=(const ApplicationKeeper &) = delete;
    ApplicationKeeper(ApplicationKeeper &&) = delete;
    ApplicationKeeper &operator=(ApplicationKeeper &&) = delete;

    bool IsReady() const noexcept;
    bool IsShutdown() const noexcept;
    bool IsOwnerThread() const noexcept;
    bool CompleteLinks() noexcept;
    bool BeginShutdown() noexcept;

    BOOL &MinimizedEnabled() noexcept;
    const BOOL &MinimizedEnabled() const noexcept;
    ApplicationConfigValues &ApplicationConfig() noexcept;
    const ApplicationConfigValues &ApplicationConfig() const noexcept;
    const std::wstring &BootstrapServerIp() const noexcept;
    WORD BootstrapServerPort() const noexcept;
    wchar_t (&AbuseFilterStorage() noexcept)[MAX_FILTERS][20];
    wchar_t (&AbuseNameFilterStorage() noexcept)[MAX_NAMEFILTERS][20];
    int &AbuseFilterCount() noexcept;
    int &AbuseNameFilterCount() noexcept;
    GATE_ATTRIBUTE *&GateAttributes() noexcept;
    MONSTER_SCRIPT (&MonsterScripts() noexcept)[MAX_MONSTER];
    Script_Skill (&MonsterSkills() noexcept)[MODEL_MONSTER_END];
    CLASS_ATTRIBUTE (&ClassAttributes() noexcept)[MAX_CLASS];
    int &EditMonsterCount() noexcept;
    std::wstring &AssetLanguage() noexcept;
    const std::wstring &AssetLanguage() const noexcept;
    float &LoginSceneOffsetX() noexcept;
    float &LoginSceneOffsetY() noexcept;
    float &LoginSceneOffsetZ() noexcept;
    float &LoginSceneAnglePitch() noexcept;
    float &LoginSceneAngleYaw() noexcept;
    HWND &PlatformWindowHandle() noexcept;
    HDC &PlatformDeviceContext() noexcept;
    HFONT &PlatformUiFont() noexcept;
    HFONT &PlatformBoldFont() noexcept;
    HFONT &PlatformBigFont() noexcept;
    HFONT &PlatformFixedFont() noexcept;
    CUIRenderText &GraphicsRenderText() noexcept;
    unsigned int &PlatformWindowWidth() noexcept;
    unsigned int &PlatformWindowHeight() noexcept;
    CGlobalBitmap &BitmapRegistry() noexcept;
    bool &PlatformDestroyRequested() noexcept;
    CErrorReport &ErrorReport() noexcept;
    CmuConsoleDebug &ConsoleDebug() noexcept;
    wchar_t (&ExecutableVersion() noexcept)[11];
    ApplicationDiagnostics *ApplicationDiagnosticsUnit() const noexcept;
    ApplicationFrameUnit *ApplicationFrameUnitShortcut() const noexcept;
    ApplicationNetwork *ApplicationNetworkUnit() const noexcept;
    ApplicationLoopUnit *ApplicationLoopUnitShortcut() const noexcept;
    ManagedBindingUnit *ManagedBindingUnitShortcut() const noexcept;
    AppWindow *AppWindowUnit() const noexcept;
    SEASON3B::CNewKeyInput *NewKeyInputUnit() const noexcept;
    CInput *InputUnit() const noexcept;
    UI::Modern::RmlUiRuntime *ModernUiRuntime() const noexcept;

    ApplicationAudio *ApplicationAudioUnit() const noexcept;
    ApplicationAudio &ApplicationAudioObject() noexcept;
    ApplicationSessionRuntime *ApplicationSessionRuntimeUnit() const noexcept;
    GameData *GameDataUnit() const noexcept;
    SessionWorkspace *SessionWorkspaceUnit() const noexcept;
    SessionConfigStore *SessionConfigStoreUnit() const noexcept;
    SessionConfigStore &SessionConfigStoreObject() noexcept;
    SessionManager *SessionManagerUnit() const noexcept;
    Compositor *CompositorUnit() const noexcept;
    std::size_t ActiveSessionCount() const noexcept;
    SharedCharacterPool &SharedCharacters() noexcept;
    const SharedCharacterPool &SharedCharacters() const noexcept;
    std::size_t RetainedModelAssetBytes() const noexcept
    {
        return retainedModelAssetBytes_;
    }
    std::uint64_t NextTerrainContentRevision() noexcept
    {
        return nextTerrainContentRevision_.fetch_add(1, std::memory_order_relaxed);
    }
    std::size_t RetainedWorldTerrainBytes() const noexcept
    {
        return retainedWorldTerrainBytes_;
    }
    std::size_t RetainedTerrainGeometryBytes() const noexcept
    {
        return retainedTerrainGeometryBytes_;
    }
    bool SelectLoginBackgroundClassic(bool firstChoice) noexcept
    {
        if (!loginBackgroundClassic_.has_value())
            loginBackgroundClassic_ = firstChoice;
        return *loginBackgroundClassic_;
    }
    const QuestScriptData *QuestScripts() const noexcept;
    bool PublishQuestScripts(std::unique_ptr<const QuestScriptData> scripts) noexcept;
    void CollectIdleAssets(std::uint64_t nowMilliseconds) noexcept;

    bool PrepareRenderTapeFrame(const ApplicationFramePlan &plan) noexcept;
    std::optional<SessionRenderTapeRecording> AcquireRenderTapeRecording(
        SessionId id, SessionGeneration generation, std::uint64_t surfaceGeneration,
        std::uint32_t viewportWidth, std::uint32_t viewportHeight) noexcept;

    bool LoadApplicationBitmap(const wchar_t *fileName, std::uint32_t logicalIndex,
                               LegacyTextureFilter filter, LegacyTextureWrap wrapMode,
                               bool fullPath);
    void DeleteApplicationBitmap(std::uint32_t logicalIndex, bool force = false) noexcept;

  private:
    struct DeferredErrorMessage final
    {
        std::array<wchar_t, 256> text{};
        bool forceDestroy = false;
    };

    template <typename Asset> struct RetainedAsset final
    {
        std::shared_ptr<Asset> asset;
        std::size_t bytes = 0;
        std::uint64_t unusedSinceMilliseconds = 0;
    };

    friend class ApplicationKeeperTestPeer;
    friend class Application;
    friend class ApplicationLoopUnit;
    friend class ApplicationLegacyCalls;
    friend class ApplicationSupportCalls;
    friend class ApplicationNetwork;
    friend class ApplicationAudio;
    friend class BMD;
    friend class TerrainGeometryCache;
    friend class WorldResources;
    friend class ApplicationDiagnostics;
    friend class ApplicationFrameUnit;
    friend class ApplicationConfigUnit;
    friend class ApplicationSessionRuntime;
    friend class AppWindow;
    friend class SEASON3B::CNewKeyInput;
    friend class CInput;
    friend class LegacyPlatformBridge;
    friend class ManagedBindingUnit;
    friend class GameData;
    friend class UI::Modern::RmlUiRuntime;
    friend class SessionKeeper;
    friend class SessionManager;
    friend class SessionConfigStore;
    friend class SessionWorkspace;
    friend class Compositor;

    enum class State
    {
        Constructing,
        Ready,
        Shutdown,
    };

    template <typename Unit> bool RegisterShortcut(Unit *&destination, Unit &unit) noexcept
    {
        if (state_ != State::Constructing || destination != nullptr)
        {
            linkError_ = true;
            return false;
        }

        destination = std::addressof(unit);
        return true;
    }

    bool RegisterApplicationAudio(ApplicationAudio &audio) noexcept;
    bool RegisterApplicationDiagnostics(ApplicationDiagnostics &diagnostics) noexcept;
    bool RegisterApplicationFrame(ApplicationFrameUnit &frame) noexcept;
    bool RegisterApplicationNetwork(ApplicationNetwork &network) noexcept;
    bool RegisterApplicationLoop(ApplicationLoopUnit &loop) noexcept;
    bool RegisterManagedBindingUnit(ManagedBindingUnit &bindings) noexcept;
    bool RegisterApplicationConfig(ApplicationConfigUnit &config) noexcept;
    bool RegisterApplicationSessionRuntime(ApplicationSessionRuntime &runtime) noexcept;
    bool RegisterAppWindow(AppWindow &window) noexcept;
    bool RegisterNewKeyInput(SEASON3B::CNewKeyInput &input) noexcept;
    bool RegisterInput(CInput &input) noexcept;
    bool RegisterModernUiRuntime(UI::Modern::RmlUiRuntime &runtime) noexcept;
    bool RegisterGameData(GameData &gameData) noexcept;
    bool RegisterSessionWorkspace(SessionWorkspace &workspace) noexcept;
    bool RegisterSessionConfigStore(SessionConfigStore &store) noexcept;
    bool RegisterSessionManager(SessionManager &sessions) noexcept;
    bool RegisterCompositor(Compositor &compositor) noexcept;
    void DeferErrorMessage(const wchar_t *errorMessage, bool forceDestroy) noexcept;
    std::optional<DeferredErrorMessage> TakeDeferredErrorMessage();
    void MarkLinkError() noexcept;
    bool AttachSession() noexcept;
    void DetachSession() noexcept;
    SessionConfigCatalog &SessionProfiles() noexcept;
    const SessionConfigCatalog &SessionProfiles() const noexcept;
    ApplicationGameDataStorage &GameDataStorageRef() noexcept;
    const ApplicationGameDataStorage &GameDataStorageRef() const noexcept;
    ApplicationDiagnosticsStorage &DiagnosticsStorageRef() noexcept;
    ManagedBindingStorage &ManagedBindingsRef() noexcept;
    ApplicationNetworkStorage &NetworkStorageRef() noexcept;
    ApplicationAudioStorage &AudioStorageRef() noexcept;
    ApplicationFrameStorage &FrameStorageRef() noexcept;
    ApplicationPlatformStorage &PlatformStorageRef() noexcept;
    ApplicationGraphicsStorage &GraphicsStorageRef() noexcept;
    ApplicationLegacyRuntimeStorage &LegacyRuntimeStorageRef() noexcept;
    Core::Time::FrameTimerScheduler &FrameTimerSchedulerObject() noexcept;
    ApplicationNetwork &ApplicationNetworkForConstruction() noexcept;
    ManagedBindingUnit &ManagedBindingsForConstruction() noexcept;
    SessionManager &SessionManagerForConstruction() noexcept;
    ApplicationConfigUnit &ApplicationConfigForConstruction() noexcept;
    ApplicationAudio &ApplicationAudioForConstruction() noexcept;
    AppWindow &AppWindowForConstruction() noexcept;
    CInput &InputForConstruction() noexcept;

    // Exact former global storage from the approved P1A.1 App worklist.
    BOOL g_bMinimizedEnabled = FALSE;
    ApplicationConfigValues applicationConfig_;
    SessionConfigCatalog sessionProfiles_;
    ApplicationCameraStorage cameraStorage_;
    std::unique_ptr<ApplicationGameDataStorage> gameDataStorage_;
    ApplicationPlatformStorage platformStorage_;
    ApplicationGraphicsStorage graphicsStorage_;
    ApplicationRenderTapeStorage renderTapeStorage_;
    std::optional<ApplicationRenderTapeStorage::FrameLease> preparedRenderTapeFrame_;
    ApplicationLegacyRuntimeStorage legacyRuntimeStorage_;
    Core::Time::FrameTimerScheduler frameTimerScheduler_;
    ApplicationFrameStorage frameStorage_;
    ApplicationDiagnosticsStorage diagnosticsStorage_;
    ManagedBindingStorage managedBindings_;
    ApplicationNetworkStorage networkStorage_;
    ApplicationAudioStorage audioStorage_;
    std::unordered_map<std::wstring, RetainedAsset<BmdSharedAsset>, LogicalAssetPath::Hash,
                       LogicalAssetPath::Equal>
        modelAssets_;
    std::unordered_map<std::uint64_t, RetainedAsset<TerrainSharedGeometry>> terrainGeometryAssets_;
    std::unordered_map<std::wstring, RetainedAsset<const WorldTerrainAsset>, LogicalAssetPath::Hash,
                       LogicalAssetPath::Equal>
        worldTerrainAssets_;
    std::size_t retainedWorldTerrainBytes_ = 0;
    std::atomic<std::uint64_t> nextTerrainContentRevision_{1};
    std::size_t retainedModelAssetBytes_ = 0;
    std::size_t retainedTerrainGeometryBytes_ = 0;
    std::uint64_t nextAssetCollectionMilliseconds_ = 0;
    std::optional<bool> loginBackgroundClassic_;
    std::unique_ptr<const QuestScriptData> questScripts_;
    SharedCharacterPool sharedCharacters_;
    const std::thread::id ownerThread_ = std::this_thread::get_id();
    std::mutex deferredErrorMutex_;
    std::optional<DeferredErrorMessage> deferredError_;
    std::atomic_bool deferredErrorPending_{false};
    State state_ = State::Constructing;
    bool linkError_ = false;
    std::size_t activeSessionCount_ = 0;
    ApplicationAudio *applicationAudio_ = nullptr;
    ApplicationDiagnostics *applicationDiagnostics_ = nullptr;
    ApplicationFrameUnit *applicationFrame_ = nullptr;
    ApplicationNetwork *applicationNetwork_ = nullptr;
    ApplicationLoopUnit *applicationLoopUnit_ = nullptr;
    ManagedBindingUnit *managedBindingUnit_ = nullptr;
    ApplicationConfigUnit *applicationConfigUnit_ = nullptr;
    ApplicationSessionRuntime *applicationSessionRuntime_ = nullptr;
    AppWindow *appWindow_ = nullptr;
    SEASON3B::CNewKeyInput *newKeyInput_ = nullptr;
    CInput *input_ = nullptr;
    UI::Modern::RmlUiRuntime *modernUiRuntime_ = nullptr;
    GameData *gameData_ = nullptr;
    SessionWorkspace *sessionWorkspace_ = nullptr;
    SessionConfigStore *sessionConfigStore_ = nullptr;
    SessionManager *sessionManager_ = nullptr;
    Compositor *compositor_ = nullptr;
};
