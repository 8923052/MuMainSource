#pragma once
#include "support/CoreMath.h"

#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationNetwork.h"
#include "data/WorldData.h"
#include "domain/Automation.h"
#include "domain/CharacterSystem.h"
#include "domain/ChatSocial.h"
#include "domain/Events.h"
#include "domain/Guild.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Shop.h"
#include "domain/WorldPhysics.h"
#include "render/ModelGeometry.h"
#include "render/ModelResources.h"
#include "render/Sprites.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "session/SessionGameplay.h"
#include "session/SessionNetwork.h"
#include "session/SessionRuntime.h"
#include "session/SessionUi.h"
#include "support/Camera.h"
#include "ui/features/Activities/ActivitiesLogic.h"

#include <array>
#include <atomic>
#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

class GameSession;
class GameSessionTestPeer;
class CTimer;
class Connection;
class CInGameShopSystem;
class CLoadingScene;
class CGlobalBitmap;
class CGaugeBar;
class CSlider;
class CWin;
class CUIFriendMenu;
class CUIGuildInfo;
class CUITextInputBox;
class CUIMercenaryInputBox;
class CSenatusInfo;
class CSBaseMatch;
class CSQuest;
class CQuestMng;
class CameraManager;
class CameraProjection;
class GMKanturu1st;
class SessionAdvanceUnit;
class SessionAudioBusView;
class ApplicationAudio;
class SessionAudioLogicUnit;
class SessionAudioLogicView;
class SessionClock;
class SessionDisplayView;
struct SessionBlurStorage;
struct SessionSpriteStorage;
struct SessionShadowVolumeStorage;
struct SessionLeafStorage;
struct SessionWelfareTempleStorage;
class SessionFrameView;
class SessionGameplayUnit;
class SessionGameDataUnit;
class ReconnectManager;
class SessionInputView;
class SessionInteractionUnit;
class CHARACTER_MACHINE;
class SessionKeeperTestPeer;
class SessionLifecycleObserver;
class SessionLifecycleState;
class SessionNetworkUnit;
class SessionPresentationUnit;
class SessionRandom;
class CPhysicsCloth;
class CPhysicsManager;
class SessionRenderUnit;
class SessionConfigStore;
class SessionUiUnit;
class SessionUiLegacyBindings;
class SessionUiView;
class SessionVisualUnit;
class SessionVisualView;
class World;
class WorldCommandView;
class WorldReadView;
class CNewYearsDayEvent;
class CSPetSystem;
class CSWaterTerrain;
class CGM_PK_Field;
namespace MUHelper
{
class SessionMuHelperUnit;
}
namespace UI::Skills::Tooltip
{
class Builder;
}
namespace SEASON3B
{
class CNewUIMessageBoxBase;
}
namespace SEASON3A
{
class CursedTemple;
class CGM3rdChangeUp;
} // namespace SEASON3A
namespace UI::Login
{
enum class RememberPasswordChoice : int;
}
namespace SEASON3B
{
class CNewUIMessageBoxMng;
class CNewUIPickedItem;
class GMNewTown;
} // namespace SEASON3B

class SessionKeeper final
{
  public:
    // P1.R1 proof shapes; P1.R3 replaces them with classified legacy fields.
    using RepresentativeScalarStorage = std::int32_t;
    using RepresentativeFixedArrayStorage = std::int32_t[4];
    using RepresentativeMatrixStorage = std::int32_t[2][3];
    using RepresentativePointerStorage = std::int32_t *;
    using RepresentativeContainerStorage = std::vector<std::int32_t>;
    using RepresentativeCallbackStorage = void (*)(std::int32_t &) noexcept;
    using RepresentativeAtomicStorage = std::atomic<std::int32_t>;

    ~SessionKeeper();

    SessionKeeper(const SessionKeeper &) = delete;
    SessionKeeper &operator=(const SessionKeeper &) = delete;
    SessionKeeper(SessionKeeper &&) = delete;
    SessionKeeper &operator=(SessionKeeper &&) = delete;

    SessionId Id() const noexcept;
    SessionSlotId SlotId() const noexcept;
    int &CameraZoom() noexcept;
    const int &CameraZoom() const noexcept;
    wchar_t (&Username() noexcept)[MAX_USERNAME_SIZE + 1];
    wchar_t (&Password() noexcept)[MAX_PASSWORD_SIZE + 1];
    int &RememberMe() noexcept;
    void RequestDiscardSession() noexcept;
    SessionConfigValues &Config() noexcept;
    const SessionConfigValues &Config() const noexcept;
    ApplicationKeeper &ApplicationKeeperRef() noexcept;
    const ApplicationKeeper &ApplicationKeeperRef() const noexcept;
    BOOL &MinimizedEnabled() noexcept;
    const BOOL &MinimizedEnabled() const noexcept;
    ApplicationConfigValues &ApplicationConfig() noexcept;
    const ApplicationConfigValues &ApplicationConfig() const noexcept;
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
    float &LoginSceneOffsetX() noexcept;
    float &LoginSceneOffsetY() noexcept;
    float &LoginSceneOffsetZ() noexcept;
    float &LoginSceneAnglePitch() noexcept;
    float &LoginSceneAngleYaw() noexcept;
    HWND &PlatformWindowHandle() noexcept;
    bool &PlatformWindowActive() noexcept;
    int &PlatformNoMouseTime() noexcept;
    bool &PlatformDestroyRequested() noexcept;
    bool &GraphicsFogEnabled() noexcept;
    float (&GraphicsFogColor() noexcept)[4];
    CErrorReport &ErrorReport() noexcept;
    CmuConsoleDebug &ConsoleDebug() noexcept;
    CGlobalBitmap &BitmapRegistry() noexcept;
    double DiagnosticsCpuAverage() const noexcept;
    double DiagnosticsSessionCpuPercent() const noexcept;
    std::size_t DiagnosticsSessionOwnedBytes() const noexcept;
    std::size_t DiagnosticsAppRingOwnedBytes() const noexcept;
    std::size_t DiagnosticsProcessMemoryBytes() const noexcept;
    double DiagnosticsGpuUsagePercent() const noexcept;
    HotspotProfilerState DiagnosticsHotspotProfileState() const noexcept;
    wchar_t (&ExecutableVersion() noexcept)[11];
    SessionModelPool &ModelPoolObject() noexcept;
    SessionModelLoader &ModelLoaderObject() noexcept;
    CBoneManager &BoneManagerObject() noexcept;
    BuffStateSystem &BuffStateSystemObject() noexcept;
    bool &DirectionTimeCheckFlag() noexcept;
    int &DirectionBackupTime() noexcept;
    CDirection &DirectionObject() noexcept;
    CServerListManager &ServerListManagerObject() noexcept;
    LoginCameraState &LoginCameraStateObject() noexcept;
    SessionEffectPool<OBJECT> &EffectsStorage() noexcept;
    SessionEffectPool<PARTICLE> &ParticlesStorage() noexcept;
    OBJECT (&MountsStorage() noexcept)[MAX_MOUNTS];
    OBJECT (&BoidsStorage() noexcept)[MAX_BOIDS];
    OBJECT (&FishsStorage() noexcept)[MAX_FISHS];
    OPERATE (&OperatesStorage() noexcept)[MAX_OPERATES];
    ITEM_t (&ItemsStorage() noexcept)[MAX_ITEMS];
    WORD &LastSkillSerialNumber() noexcept;
    OBJECT_BLOCK (&ObjectBlocks() noexcept)[256];
    int &ActionObjectType() noexcept;
    int &ActionWorld() noexcept;
    float &ActionTime() noexcept;
    float &ActionObjectVelocity() noexcept;
    wchar_t (&MacroTexts() noexcept)[10][256];
    SessionBlurStorage &BlurStorage() noexcept;
    SessionSpriteStorage &SpritesStorage() noexcept;
    SessionShadowVolumeStorage &ShadowVolumeStorage() noexcept;
    float &CollisionDistance() noexcept;
    float (&CollisionPosition() noexcept)[3];
    bool &TerrainSelectFlag() noexcept;
    float &TerrainSelectX() noexcept;
    float &TerrainSelectY() noexcept;
    UI::Login::RememberPasswordChoice &RememberPasswordChoiceStorage() noexcept;
    DWORD &LastUiId() noexcept;
    SEASON3B::CNewUIPickedItem *&PickedItem() noexcept;
    SessionLeafStorage &LeafStorage() noexcept;
    SessionWelfareTempleStorage &WelfareTempleStorage() noexcept;
    int &TaxRate() noexcept;
    int &ChaosTaxRate() noexcept;
    int &GoalEffect() noexcept;
    SessionGroundItemLabelStorage &GroundItemLabelStorage() noexcept;
    SessionItemHelpStorage &ItemHelpStorage() noexcept;
    SessionPersonalItemPriceStorage &PersonalItemPriceStorage() noexcept;
    SessionComGemStorage &ComGemStorage() noexcept;
    SEASON3A::CMixRecipeMgr &MixRecipeManager() noexcept;
    SEASON4A::CSocketItemMgr &SocketItemManager() noexcept;
    CSItemOption &ItemOptionManager() noexcept;
    PATH &PathObject() noexcept;
    CSenatusInfo &SenatusInfoObject() noexcept;
    SessionChaosCastleStorage &ChaosCastleStorage() noexcept;
    CTimeCheck &TimeCheckObject() noexcept;
    GambleSystem &GambleSystemObject() noexcept;
    SEASON3B::CMoveCommandData &MoveCommandDataObject() noexcept;
    SessionQuestDialogStorage &QuestDialogStorage() noexcept;
    SessionPetManagerStorage &PetManagerStorage() noexcept;
    SessionInterfaceStorage &InterfaceStorage() noexcept;
    SessionInventoryStorage &InventoryStorage() noexcept;
    SessionTerrainStorage &TerrainStorage() noexcept;
    bool LoadSessionBitmap(const wchar_t *fileName, std::uint32_t logicalIndex,
                           LegacyTextureFilter filter, LegacyTextureWrap wrapMode, bool check,
                           bool fullPath);
    void DeleteSessionBitmap(std::uint32_t logicalIndex) noexcept;
    SessionTextureNamespace &TextureNamespace() noexcept;
    SessionRenderText &SessionText() noexcept;
    SessionNetworkStorage &NetworkStorage() noexcept;
    SessionBmdStorage &BmdStorage() noexcept;
    SessionPhysicsStorage &PhysicsStorage() noexcept;
    SessionCharacterPopulationStorage &CharacterPopulationStorage() noexcept;
    SessionGuildMasterStorage &GuildMasterStorage() noexcept;
    bool InitializeCharacterPopulation() noexcept;
    SessionCharacterPopulationStorage &CharactersClientStorage() noexcept;
    CHARACTER &CharacterViewStorage() noexcept;
    CHARACTER *&HeroStorage() noexcept;
    CHARACTER_MACHINE *&CharacterMachineStorage() noexcept;
    CHARACTER_ATTRIBUTE *&CharacterAttributeStorage() noexcept;
    CSQuest &QuestObject() noexcept;
    CQuestMng &QuestManagerObject() noexcept;
    SEASON3B::CPartyManager &PartyManagerObject() noexcept;
    CDuelMgr &DuelManagerObject() noexcept;
    CUIGuardsMan &GuardsManObject() noexcept;
    CHARACTER_MACHINE &CharacterMachineObject() noexcept;
    CSBaseMatch *&EventMatch() noexcept;
    int &KeyPadEnable() noexcept;
    std::span<MARK_t> GuildMarks() noexcept;
    int &SelectedGuildMarkColor() noexcept;
    CGuildCache &GuildCacheObject() noexcept;
    SessionEffectPool<JOINT> &JointsStorage() noexcept;
    SessionEffectPool<PARTICLE> &PointsStorage() noexcept;
    SessionEffectPool<PARTICLE> &PointersStorage() noexcept;
    const MapDefinition *WorldContextDefinition() const noexcept;
    WorldBinding &WorldState() noexcept
    {
        return worldBinding_;
    }
    const WorldBinding &WorldState() const noexcept
    {
        return worldBinding_;
    }
    CMapManager &MapManagerObject() noexcept;
    CCameraMove &CameraMoveObject() noexcept;
    CSkillManager &SkillManagerObject() noexcept;
    CSkillEffectMgr &SkillEffectManagerObject() noexcept;
    CSummonSystem &SummonSystemObject() noexcept;
    CMonkSystem &MonkSystemObject() noexcept;
    CPortalMgr &PortalManagerObject() noexcept;
    bool &KanturuSuccessMap() noexcept;
    bool &KanturuSuccessMapBackup() noexcept;
    int &KanturuMayaAction() noexcept;
    bool &KanturuMayaSkill2() noexcept;
    int &KanturuMayaSkill2Counter() noexcept;
    int &KanturuMayaDieCounter() noexcept;
    int &KanturuResult() noexcept;
    float &KanturuResultAlpha() noexcept;
    int &KanturuUserCount() noexcept;
    int &KanturuMonsterCount() noexcept;
    UI::Chat::Storage &ChatStorage() noexcept;
    SEASON3B::CNewUIMessageBoxMng &MessageBoxManagerObject() noexcept;
    CUIFriendMenu &FriendMenuObject() noexcept;
    CInGameShopSystem &InGameShopSystemObject() noexcept;
    CUITextInputBox *&SingleTextInputBox() noexcept;
    CUITextInputBox *&SinglePasswordInputBox() noexcept;
    CUIMercenaryInputBox *&MercenaryInputBox() noexcept;
    CUITextInputBox *&FocusedTextInputBox() noexcept;
    DWORD &KeyFocusUiId() noexcept;
    int &MouseWheelState() noexcept;
    bool InputKeyIsNone(int virtualKey) const noexcept;
    bool InputKeyIsRelease(int virtualKey) const noexcept;
    bool InputKeyIsPress(int virtualKey) const noexcept;
    bool InputKeyIsRepeat(int virtualKey) const noexcept;
    bool InputKeyIsDown(int virtualKey) const noexcept;
    void SetInputKeyState(int virtualKey, int state) noexcept;
    void ApplyInputKeySnapshot(const BYTE *states, bool focused) noexcept;
    PetProcess &PetProcessObject() noexcept;
    SEASON3A::CursedTemple &CursedTempleObject() noexcept;
    SEASON3A::CGM3rdChangeUp &ThirdChangeObject() noexcept;
    double &FrameFpsAverage() noexcept;
    bool &FrameDebugInfoEnabled() noexcept;
    bool &FrameFpsCounterEnabled() noexcept;
    float (&FrameTimesMs() noexcept)[ApplicationFrameStorage::FrameHistorySize];
    int &FrameHistoryIndex() noexcept;
    int &FrameHistoryCount() noexcept;
    double &FrameHighestFps() noexcept;
    float &FrameAverageFps() noexcept;
    float &FrameOnePercentLow() noexcept;
    float &FrameSlowestFps() noexcept;
    float (&FrameProfilerAccumulatorMs() noexcept)[ApplicationFrameStorage::ProfilerPassCount];
    CTimer *&FrameTimer() noexcept;
    float &FrameAnimationFactor() noexcept;
    double &FrameElapsedMilliseconds() noexcept;
    double &FrameWorldTime() noexcept;
    FrameTimingState &FrameTiming() noexcept;
    std::chrono::steady_clock::time_point &FrameTimer2StartTickTime() noexcept;
    bool &FrameRenderBoundingBox() noexcept;
    BOOL &PlatformWindowMode() noexcept;
    int &PlatformOpenglWindowX() noexcept;
    int &PlatformOpenglWindowY() noexcept;
    int &PlatformOpenglWindowWidth() noexcept;
    int &PlatformOpenglWindowHeight() noexcept;
    unsigned int &PlatformWindowWidth() noexcept;
    unsigned int &PlatformWindowHeight() noexcept;
    float &PlatformScreenRateX() noexcept;
    float &PlatformScreenRateY() noexcept;
    CameraState &CameraStateObject() noexcept;
    CameraManager &CameraManagerObject() noexcept;
    CameraManager *CameraManagerUnit() const noexcept;
    CameraProjection &CameraProjectionObject() noexcept;
    SessionConfigStore &SessionConfigStoreObject() noexcept;
    ApplicationAudio &ApplicationAudioObject() noexcept;
    Connection *&NetworkConnection() noexcept;
    BOOL &GameServerConnected() noexcept;

    RepresentativeScalarStorage &RepresentativeScalar() noexcept;
    RepresentativeFixedArrayStorage &RepresentativeFixedArray() noexcept;
    RepresentativeMatrixStorage &RepresentativeMatrix() noexcept;
    RepresentativePointerStorage &RepresentativePointer() noexcept;
    RepresentativeContainerStorage &RepresentativeContainer() noexcept;
    RepresentativeCallbackStorage &RepresentativeCallback() noexcept;
    RepresentativeAtomicStorage &RepresentativeAtomic() noexcept;

    SessionDisplayView *Display() const noexcept;
    SessionInputView *Input() const noexcept;
    SessionAudioBusView *AudioOutput() const noexcept;
    SessionFrameView *Frame() const noexcept;
    SessionClock *Clock() const noexcept;
    SessionRandom *Random() const noexcept;
    SessionLifecycleState *Lifecycle() const noexcept;
    World *WorldUnit() const noexcept;
    SessionUiUnit *Ui() const noexcept;
    SessionInteractionUnit *Interaction() const noexcept;
    SessionPresentationUnit *Presentation() const noexcept;
    SessionGameplayUnit *Gameplay() const noexcept;
    SessionGameDataUnit *GameData() const noexcept;
    MUHelper::SessionMuHelperUnit *MuHelper() const noexcept;
    SessionNetworkUnit *Network() const noexcept;
    SessionVisualUnit *Visual() const noexcept;
    SessionAudioLogicUnit *AudioLogic() const noexcept;
    SessionAdvanceUnit *Advance() const noexcept;
    SessionRenderUnit *Renderer() const noexcept;
    SessionUiView *UiView() const noexcept;
    SessionVisualView *VisualView() const noexcept;
    SessionAudioLogicView *AudioLogicView() const noexcept;
    WorldReadView *WorldRead() const noexcept;
    WorldCommandView *WorldCommands() const noexcept;

  private:
    friend class GameSession;
    friend class GameSessionTestPeer;
    friend class SessionAdvanceUnit;
    friend class SessionAudioLogicUnit;
    friend class SessionClock;
    friend class SessionGameplayUnit;
    friend class SessionGameDataUnit;
    friend class MUHelper::SessionMuHelperUnit;
    friend class SessionNetworkUnit;
    friend class SessionInteractionUnit;
    friend class SessionKeeperTestPeer;
    friend class SessionLifecycleState;
    friend class SessionLegacyCalls;
    friend class SessionPresentationUnit;
    friend class SessionRandom;
    friend class CPhysicsCloth;
    friend class CPhysicsManager;
    friend class SessionRenderUnit;
    friend class SessionUiUnit;
    friend class SessionUiLegacyBindings;
    friend class ReconnectManager;
    friend class CQuestMng;
    friend class CSQuest;
    friend class CWin;
    friend class CUIGuildInfo;
    friend class CUIMng;
    friend class CLoadingScene;
    friend class CGaugeBar;
    friend class CSlider;
    friend class CameraManager;
    friend class SEASON3B::CNewUIMessageBoxBase;
    friend class SessionVisualUnit;
    friend class GameSession;
    friend class SessionManager;
    friend class UI::Skills::Tooltip::Builder;
    friend class World;
    friend class SEASON3B::GMNewTown;
    friend class GMKanturu1st;
    friend class SEASON3A::CMixRecipeMgr;
    friend class CNewYearsDayEvent;
    friend class CSPetSystem;
    friend class PetObject;
    friend class CSWaterTerrain;
    friend class CGM_PK_Field;

    enum class State
    {
        Linking,
        Ready,
        Shutdown,
    };

    explicit SessionKeeper(ApplicationKeeper &applicationKeeper, SessionId id, SessionSlotId slotId,
                           const SessionConfigValues &initialConfig,
                           SessionLifecycleObserver *observer = nullptr);

    template <typename Unit> bool RegisterShortcut(Unit *&destination, Unit &unit) noexcept
    {
        if (state_ != State::Linking || destination != nullptr)
        {
            linkError_ = true;
            return false;
        }

        destination = std::addressof(unit);
        return true;
    }

    template <typename View> View *AccessWhenReady(View *view) const noexcept
    {
        return state_ == State::Ready ? view : nullptr;
    }

    bool RegisterClock(SessionClock &clock) noexcept;
    bool RegisterRandom(SessionRandom &random) noexcept;
    bool RegisterLifecycle(SessionLifecycleState &lifecycle) noexcept;
    bool RegisterWorld(World &world) noexcept;
    bool RegisterUi(SessionUiUnit &ui) noexcept;
    bool InitializeFriendMenuForConstruction();
    CUIFriendMenu *&FriendMenuForConstruction() noexcept;
    bool RegisterInteraction(SessionInteractionUnit &interaction) noexcept;
    bool RegisterPresentation(SessionPresentationUnit &presentation) noexcept;
    bool RegisterGameplay(SessionGameplayUnit &gameplay) noexcept;
    bool RegisterGameData(SessionGameDataUnit &gameData) noexcept;
    bool RegisterMuHelper(MUHelper::SessionMuHelperUnit &muHelper) noexcept;
    bool RegisterNetwork(SessionNetworkUnit &network) noexcept;
    bool RegisterVisual(SessionVisualUnit &visual) noexcept;
    bool RegisterAudioLogic(SessionAudioLogicUnit &audioLogic) noexcept;
    bool RegisterAdvance(SessionAdvanceUnit &advance) noexcept;
    bool RegisterRenderer(SessionRenderUnit &renderer) noexcept;
    SessionGameplayUnit &GameplayForConstruction() noexcept;
    SessionUiUnit &UiForConstruction() noexcept;
    SessionRenderUnit &RendererForConstruction() noexcept;
    SessionRandom &RandomForConstruction() noexcept;
    SessionNetworkUnit &NetworkForConstruction() noexcept;
    SessionGameDataUnit &GameDataForConstruction() noexcept;
    MUHelper::SessionMuHelperUnit &MuHelperForConstruction() noexcept;
    ApplicationNetwork &ApplicationNetworkForConstruction() noexcept;
    ManagedBindingUnit &ManagedBindingsForConstruction() noexcept;
    SessionManager &SessionManagerForConstruction() noexcept;
    ApplicationConfigUnit &ApplicationConfigForConstruction() noexcept;
    ApplicationAudio &ApplicationAudioForConstruction() noexcept;
    AppWindow &AppWindowForConstruction() noexcept;
    CInput &ApplicationInputForConstruction() noexcept;
    CUIMng &LegacyUiManagerForConstruction() noexcept;
    SessionAudioBusView &AudioOutputForConstruction() noexcept;
    bool ResolveApplicationResources() noexcept;
    SessionDisplayView &DisplayForConstruction() noexcept;
    bool CompleteLinks() noexcept;
    bool InitializeBuffStateSystem();
    bool BeginShutdown() noexcept;
    void RekeySlot(SessionSlotId slotId) noexcept;

    ApplicationKeeper &applicationKeeper_;
    BOOL &g_bMinimizedEnabled;
    SessionId id_;
    SessionSlotId slotId_;
    SessionConfigValues config_;
    int cameraZoom_;
    float frameAnimationFactor_ = 0.0f;
    double frameElapsedMilliseconds_ = 0.0;
    wchar_t username_[MAX_USERNAME_SIZE + 1]{};
    wchar_t password_[MAX_PASSWORD_SIZE + 1]{};
    int rememberMe_ = 0;
    SessionLifecycleObserver *observer_;
    Connection *socketClient_ = nullptr;
    BOOL gameServerConnected_ = FALSE;
    CameraState cameraState_;
    RepresentativeScalarStorage representativeScalar_ = 0;
    RepresentativeFixedArrayStorage representativeFixedArray_{};
    RepresentativeMatrixStorage representativeMatrix_{};
    RepresentativePointerStorage representativePointer_ = nullptr;
    RepresentativeContainerStorage representativeContainer_;
    RepresentativeCallbackStorage representativeCallback_ = nullptr;
    RepresentativeAtomicStorage representativeAtomic_{0};
    MUHelper::SessionMuHelperStorage muHelperStorage_;
    UI::Chat::Storage chatStorage_;
    // Model destruction calls DeleteSessionBitmap(), so the texture namespace
    // must be constructed first and destroyed after the complete model pool.
    SessionTextureNamespace textureNamespace_;
    SessionRenderText sessionText_;
    SessionModelPool modelPool_;
    SessionModelLoader modelLoader_;
    CBoneManager boneManager_;
    BuffStateSystem buffStateSystemObject_;
    bool directionTimeCheckFlag_ = false;
    int directionBackupTime_ = 0;
    CDirection directionObject_;
    CServerListManager serverListManagerObject_;
    LoginCameraState loginCameraState_;
    SessionEffectPool<OBJECT> effects_;
    SessionEffectPool<PARTICLE> particles_;
    OBJECT mounts_[MAX_MOUNTS]{};
    OBJECT boids_[MAX_BOIDS]{};
    OBJECT fishs_[MAX_FISHS]{};
    OPERATE operates_[MAX_OPERATES]{};
    ITEM_t items_[MAX_ITEMS]{};
    WORD lastSkillSerialNumber_ = 0;
    OBJECT_BLOCK objectBlocks_[256]{};
    int actionObjectType_ = -1;
    int actionWorld_ = -1;
    float actionTime_ = -1;
    float actionObjectVelocity_ = -1.0f;
    wchar_t macroTexts_[10][256]{};
    std::unique_ptr<SessionBlurStorage> blurStorage_;
    std::unique_ptr<SessionSpriteStorage> spriteStorage_;
    std::unique_ptr<SessionShadowVolumeStorage> shadowVolumeStorage_;
    float collisionDistance_ = 0.f;
    float collisionPosition_[3]{};
    bool terrainSelectFlag_ = false;
    float terrainSelectX_ = 0.f;
    float terrainSelectY_ = 0.f;
    UI::Login::RememberPasswordChoice rememberPasswordChoice_{};
    DWORD lastUiId_ = 0;
    std::unique_ptr<SessionLeafStorage> leafStorage_;
    std::unique_ptr<SessionWelfareTempleStorage> welfareTempleStorage_;
    int taxRate_ = 0;
    int chaosTaxRate_ = 0;
    int goalEffect_ = 0;
    SessionGroundItemLabelStorage groundItemLabelStorage_;
    SessionItemHelpStorage itemHelpStorage_;
    SessionPersonalItemPriceStorage personalItemPriceStorage_;
    SessionComGemStorage comGemStorage_;
    SessionQuestDialogStorage questDialogStorage_;
    SessionPetManagerStorage petManagerStorage_;
    SessionInterfaceStorage interfaceStorage_;
    SessionInventoryStorage inventoryStorage_;
    SessionTerrainStorage terrainStorage_;
    SessionNetworkStorage networkStorage_;
    SessionBmdStorage bmdStorage_;
    SessionPhysicsStorage physicsStorage_;
    SessionCharacterPopulationStorage characterPopulationStorage_{boneManager_};
    SessionGuildMasterStorage guildMasterStorage_;
    SEASON3A::CMixRecipeMgr mixRecipeManager_;
    SEASON4A::CSocketItemMgr socketItemManager_;
    CSItemOption itemOptionManager_;
    PATH pathObject_;
    SessionChaosCastleStorage chaosCastleStorage_;
    CTimeCheck timeCheckObject_;
    GambleSystem gambleSystem_;
    SEASON3B::CMoveCommandData moveCommandData_;
    std::unique_ptr<CHARACTER_MACHINE> characterMachine_;
    std::unique_ptr<CSQuest> questObject_;
    std::unique_ptr<CQuestMng> questManagerObject_;
    SessionSenatusInfoPtr senatusInfoObject_;
    SEASON3B::CPartyManager partyManager_;
    CDuelMgr duelManager_;
    CUIGuardsMan guardsManObject_;
    CSBaseMatch *eventMatch_ = nullptr;
    int keyPadEnable_ = 0;
    MARK_t guildMarks_[GuildMarkConstants::Count]{};
    int selectedGuildMarkColor_ = 0;
    CGuildCache guildCache_;
    SessionEffectPool<JOINT> joints_;
    SessionEffectPool<PARTICLE> points_;
    SessionEffectPool<PARTICLE> pointers_;
    friend class WorldPreviewContext;
    friend class CMapManager;
    friend class GMChaosCastle;
    friend class GMDevilSquare;
    friend class GMLorencia;
    friend class GMIcarus;
    friend class GMDevias;
    friend class GMAtlans;

    bool worldPreviewActive_ = false;
    WorldBinding worldBinding_;
    CCameraMove cameraMoveObject_;
    CMapManager mapManagerObject_;
    CSkillManager skillManagerObject_;
    CSkillEffectMgr skillEffectManagerObject_;
    CSummonSystem summonSystemObject_;
    CMonkSystem monkSystemObject_;
    CPortalMgr portalManagerObject_;
    bool kanturuSuccessMap_ = false;
    bool kanturuSuccessMapBackup_ = false;
    int kanturuMayaAction_ = -1;
    bool kanturuMayaSkill2_ = false;
    int kanturuMayaSkill2Counter_ = 0;
    int kanturuMayaDieCounter_ = 0;
    int kanturuResult_ = -1;
    float kanturuResultAlpha_ = 0.1f;
    int kanturuUserCount_ = 0;
    int kanturuMonsterCount_ = 0;
    SEASON3B::CNewUIMessageBoxMng *messageBoxManagerObject_ = nullptr;
    CUIFriendMenu *friendMenuObject_ = nullptr;
    CInGameShopSystem *inGameShopSystemObject_ = nullptr;
    CUITextInputBox *singleTextInputBox_ = nullptr;
    CUITextInputBox *singlePasswordInputBox_ = nullptr;
    CUIMercenaryInputBox *mercenaryInputBox_ = nullptr;
    CUITextInputBox *focusedTextInputBox_ = nullptr;
    DWORD keyFocusUiId_ = 0;
    int mouseWheel_ = 0;
    std::array<BYTE, 256> inputKeyStates_{};
    PetProcessPtr g_petProcess;
    std::unique_ptr<SEASON3A::CursedTemple> cursedTempleObject_;
    std::unique_ptr<SEASON3A::CGM3rdChangeUp> thirdChangeObject_;
    SessionDisplayView *display_ = nullptr;
    SessionInputView *input_ = nullptr;
    SessionAudioBusView *audioOutput_ = nullptr;
    SessionFrameView *frame_ = nullptr;
    SessionUiView *uiView_ = nullptr;
    SessionVisualView *visualView_ = nullptr;
    SessionAudioLogicView *audioLogicView_ = nullptr;
    State state_ = State::Linking;
    bool linkError_ = false;
    bool attachedToApplication_ = false;
    SessionClock *clock_ = nullptr;
    SessionRandom *random_ = nullptr;
    SessionLifecycleState *lifecycle_ = nullptr;
    World *world_ = nullptr;
    SessionUiUnit *ui_ = nullptr;
    SessionInteractionUnit *interaction_ = nullptr;
    SessionPresentationUnit *presentation_ = nullptr;
    SessionGameplayUnit *gameplay_ = nullptr;
    SessionGameDataUnit *gameData_ = nullptr;
    MUHelper::SessionMuHelperUnit *muHelper_ = nullptr;
    SessionNetworkUnit *network_ = nullptr;
    SessionVisualUnit *visual_ = nullptr;
    SessionAudioLogicUnit *audioLogic_ = nullptr;
    SessionAdvanceUnit *advance_ = nullptr;
    SessionRenderUnit *renderer_ = nullptr;
};

enum class SessionLifecycleEvent
{
    KeeperConstructed,
    ClockConstructed,
    RandomConstructed,
    LifecycleStateConstructed,
    WorldConstructed,
    UiUnitConstructed,
    InteractionUnitConstructed,
    PresentationUnitConstructed,
    GameplayUnitConstructed,
    VisualUnitConstructed,
    AudioLogicUnitConstructed,
    AdvanceUnitConstructed,
    RenderUnitConstructed,
    KeeperLinksCompleted,
    Published,
    KeeperShutdown,
    RenderUnitDestroyed,
    AdvanceUnitDestroyed,
    AudioLogicUnitDestroyed,
    VisualUnitDestroyed,
    GameplayUnitDestroyed,
    PresentationUnitDestroyed,
    InteractionUnitDestroyed,
    UiUnitDestroyed,
    WorldDestroyed,
    LifecycleStateDestroyed,
    RandomDestroyed,
    ClockDestroyed,
    KeeperDestroyed,
};

// Optional observations for the existing observed-session construction path.
// They report execution; they do not select behavior or validate render data.
enum class SessionWorkerStage
{
    VisibleSimulation,
    MainSceneEntities,
    Systems,
};

struct PacketInfo;

class SessionLifecycleObserver
{
  public:
    virtual ~SessionLifecycleObserver() = default;
    virtual void OnSessionLifecycleEvent(SessionLifecycleEvent event) noexcept = 0;
    virtual void OnSessionWorkerStage(SessionWorkerStage) noexcept
    {
    }
    virtual void OnSessionPacket(SessionId, const PacketInfo &)
    {
    }
};

class SessionKeeper;
class SessionLifecycleObserver;
class GameSessionTestPeer;

class SessionLifecycleState final : protected SessionLegacyCalls
{
  public:
    SessionLifecycleState(SessionKeeper &keeper,
                          SessionLifecycleObserver *observer = nullptr) noexcept;
    ~SessionLifecycleState();

    void MarkAdvanced() noexcept;
    void MarkRendered() noexcept;
    void BeginAdvance() noexcept;
    bool BeginRender() noexcept;
    std::uint64_t AdvanceCount() const noexcept;
    std::uint64_t RenderCount() const noexcept;

  private:
    friend class GameSessionTestPeer;

    SessionLifecycleObserver *observer_;
    std::uint64_t advanceCount_ = 0;
    std::uint64_t renderCount_ = 0;
    bool canRender_ = false;
};
