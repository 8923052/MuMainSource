#pragma once
#include "support/CoreMath.h"

#include "data/CharacterData.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "data/WorldData.h"
#include "domain/CharacterSystem.h"
#include "domain/ChatSocial.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Quests.h"
#include "domain/WorldPhysics.h"
#include "domain/WorldSimulation.h"
#include "network/generated/PacketFunctions_ClientToServer_Enums.h"
#include "session/SessionNetwork.h"
#include "session/SessionRuntime.h"
#include "support/Camera.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// Thread-safe random number generation module.

// Production random behavior is owned by SessionRandom. This compatibility
// include remains until named consumers finish replacing their old include.

class SessionKeeper;
class SessionLifecycleObserver;
class GameSessionTestPeer;

class SessionRandom final
{
  public:
    // Persistent visual updates have a stream separate from gameplay.
    class PresentationScope final
    {
      public:
        explicit PresentationScope(SessionRandom &random, bool presentation = true) noexcept
            : random_(random), previous_(random.presentation_)
        {
            random_.presentation_ = previous_ || presentation;
        }
        ~PresentationScope()
        {
            random_.presentation_ = previous_;
        }
        PresentationScope(const PresentationScope &) = delete;
        PresentationScope &operator=(const PresentationScope &) = delete;

      private:
        SessionRandom &random_;
        bool previous_;
    };

    // Drawing cannot consume either persistent stream, including nested scopes.
    class DrawScope final
    {
      public:
        explicit DrawScope(SessionRandom &random) noexcept
            : random_(random), previous_(random.drawing_)
        {
            random_.drawing_ = true;
        }
        ~DrawScope()
        {
            random_.drawing_ = previous_;
        }
        DrawScope(const DrawScope &) = delete;
        DrawScope &operator=(const DrawScope &) = delete;

      private:
        SessionRandom &random_;
        bool previous_;
    };

    SessionRandom(SessionKeeper &keeper, std::uint64_t seed,
                  SessionLifecycleObserver *observer = nullptr) noexcept;
    ~SessionRandom();

    bool IsPresentation() const noexcept
    {
        return presentation_ || drawing_;
    }
    std::uint64_t Next() noexcept;
    std::int32_t RangeInt(std::int32_t minInclusive, std::int32_t maxInclusive);
    float RangeFloat(float minInclusive, float maxInclusive);
    float Unit();
    double UnitDouble();
    bool FpsCheck(std::int32_t referenceFrames, double animationFactor = 1.0);
    int EmissionCount(double animationFactor);

  private:
    friend class GameSessionTestPeer;

    SessionKeeper &sessionKeeper_;
    SessionLifecycleObserver *observer_;
    std::uint64_t state_;
    std::mt19937 engine_;
    std::mt19937 presentationEngine_;
    std::uint64_t presentationState_;
    std::mt19937 drawEngine_;
    std::uint64_t drawState_;
    bool presentation_ = false;
    bool drawing_ = false;
    std::mt19937 &ActiveEngine() noexcept
    {
        return drawing_ ? drawEngine_ : presentation_ ? presentationEngine_ : engine_;
    }
};

enum class GameplayExternalEventKind
{
    ResetInteraction,
    ClearFollow,
    StartArrivalCooldown,
    CancelWorldTransfer,
    ResetCadence,
    ClearAttack,
};

struct GameplayExternalEvent final
{
    GameplayExternalEventKind kind = GameplayExternalEventKind::ResetInteraction;
    std::optional<CharacterId> character;
    std::uint32_t cooldownMilliseconds = 0;
    std::uint32_t cadenceMaximum = 0;

    constexpr auto operator<=>(const GameplayExternalEvent &) const noexcept = default;
};

class GameplayExternalEventBatch final
{
  public:
    static constexpr std::size_t Capacity = 2;

    bool Add(const GameplayExternalEvent &event) noexcept
    {
        if (size_ == events_.size())
        {
            return false;
        }
        events_[size_++] = event;
        return true;
    }

    const GameplayExternalEvent *begin() const noexcept
    {
        return events_.data();
    }

    const GameplayExternalEvent *end() const noexcept
    {
        return events_.data() + size_;
    }

    std::size_t Size() const noexcept
    {
        return size_;
    }

  private:
    std::array<GameplayExternalEvent, Capacity> events_{};
    std::size_t size_ = 0;
};

class CHARACTER;
class OBJECT;

class SessionKeeper;
class SessionLifecycleObserver;
class GameSessionTestPeer;

class SessionClock final : protected SessionLegacyCalls
{
  public:
    SessionClock(SessionKeeper &keeper, SessionLifecycleObserver *observer = nullptr) noexcept;
    ~SessionClock();

    void Advance() noexcept;
    std::uint64_t FrameNumber() const noexcept;

  private:
    friend class GameSessionTestPeer;

    SessionLifecycleObserver *observer_;
    std::uint64_t frameNumber_ = 0;
};

class SessionKeeper;
class SessionItemStore;
class InventoryGrid;
class CErrorReport;
class CMapManager;
class CHARACTER_MACHINE;
class CSQuest;
class CQuestMng;
class CSkillManager;
class CSItemOption;
class CSQuest;
class CQuestMng;
class GameSessionTestPeer;
namespace SEASON4A
{
class CSocketItemMgr;
}

class SessionGameDataUnit final : protected SessionLegacyCalls
{
  public:
    explicit SessionGameDataUnit(SessionKeeper &keeper) noexcept;
    ~SessionGameDataUnit();
    SessionItemStore &Items() noexcept
    {
        return *items_;
    }
    InventoryGrid &Inventory(InventoryRole role) noexcept;
    InventoryGrid *PlayerInventoryForSlot(int slot) const noexcept;
    ITEM *FindInventoryItemBySlot(int slot) const;
    int FindManaItemIndex() const;
    int FindHealingItemIndex() const;
    bool CreatePersonalItemTable();
    void ReleasePersonalItemTable();
    void AddPersonalItemPrice(int index, int price, int type);
    void RemovePersonalItemPrice(int index, int type);
    void RemoveAllPerosnalItemPrice(int type);
    bool GetPersonalItemPrice(int index, int &price, int type);
    void ClearInventoryContainers();
    void ClearPlayerInventory();
    bool InsertInventoryItem(int slot, std::span<const BYTE> packet);
    void DeleteInventoryItem(int slot);
    bool HasInventoryItem(short type, bool includePicked = false) const;
    bool BeginItemMove(InventoryGrid *source, ITEM *item);
    PickedInventoryItem *GetPickedItem() const noexcept;
    void ClearPickedItem();
    void TrackItemMove(STORAGE_TYPE storage, int sourceSlot);
    void CompleteItemMove(bool success);
    bool CompleteLuckyMix(BYTE result, std::span<const BYTE> packet);
    void InitializeCharacter(CHARACTER_MACHINE &characterMachine, CLASS_TYPE characterClass);
    const wchar_t *getMonsterName(int type) const;
    void MonsterConvert(MONSTER *monster, int level);
    void GetItemName(int type, int level, wchar_t *text) const;
    int GetExcellentAddValue(ITEM *item) const;
    void CalcDamageMin(ITEM *item, ITEM_ATTRIBUTE *attribute, int excellentAddValue) const;
    void CalcDamageMax(ITEM *item, ITEM_ATTRIBUTE *attribute, int excellentAddValue) const;
    void CalcMagicPower(ITEM *item, ITEM_ATTRIBUTE *attribute, int excellentAddValue) const;
    void CalcSuccessfulBlocking(ITEM *item, ITEM_ATTRIBUTE *attribute) const;
    void CalcDefense(ITEM *item, ITEM_ATTRIBUTE *attribute) const;
    void CalcRequirements(ITEM *item, ITEM_ATTRIBUTE *attribute) const;
    void CalcWingOptions(ITEM *item) const;
    void CalcExcellentOptions(ITEM *item) const;
    void CalcPartType(ITEM *item) const;
    void SetItemAttributes(ITEM *item) const;
    int64_t ItemValue(ITEM *item, int goldType = 1) const;
    void PrintItem(wchar_t *fileName) const;
    bool IsHighValueItem(ITEM *item) const;
    bool IsRequireEquipItem(ITEM *item);
    void PlusSpecial(WORD *value, int special, ITEM *item);
    void PlusSpecialPercent(WORD *value, int special, ITEM *item, WORD percent);
    void PlusSpecialPercent2(WORD *value, int special, ITEM *item);
    WORD ItemDefense(ITEM *item);
    WORD ItemMagicDefense(ITEM *item);
    WORD ItemWalkSpeed(ITEM *item);
    bool IsTradeBan(ITEM *item);
    bool OpenMonsterModel(EMonsterModelType type);

    void ConfigureMonsterModel(EMonsterModelType type);

    bool PrepareMonsterResources(EMonsterModelType type);

    bool FlushPendingMonsterModelsOnOwner() noexcept;

    void DeleteMonsters();

    void SetMonsterSound(int type, int sound1, int sound2, int sound3, int sound4, int sound5,
                         int sound6 = -1, int sound7 = -1, int sound8 = -1, int sound9 = -1,
                         int sound10 = -1);

  private:
    std::unique_ptr<SessionItemStore> items_;
    std::atomic_bool monsterModelLoadPending_{false};
    std::vector<EMonsterModelType> pendingMonsterModels_;
    std::mutex pendingMonsterModelsMutex_;
    friend class SessionLegacyCalls;
    friend class GameSessionTestPeer;

    bool IsRepairBan(ITEM *item) const;                               // OMF-00562
    std::wstring GetItemDisplayName(ITEM *item);                      // OMF-00568
    BOOL IsCorrectSkillType(INT skillSequence, eTypeSkill skillType); // OMF-00400
    BOOL IsCorrectSkillType_FrendlySkill(INT skillSequence);          // OMF-00401
    BOOL IsCorrectSkillType_Buff(INT skillSequence);                  // OMF-00402
    BOOL IsCorrectSkillType_DeBuff(INT skillSequence);                // OMF-00403
    BOOL IsCorrectSkillType_CommonAttack(INT skillSequence);          // OMF-00404
    wchar_t (&AbuseFilter)[MAX_FILTERS][20];
    wchar_t (&AbuseNameFilter)[MAX_NAMEFILTERS][20];
    int &AbuseFilterNumber;
    int &AbuseNameFilterNumber;
    MONSTER_SCRIPT (&MonsterScript)[MAX_MONSTER];
    CLASS_ATTRIBUTE (&ClassAttribute)[MAX_CLASS];
    int &EditMonsterNumber;
    std::wstring &g_strSelectedML;
    CSkillManager &gSkillManager;
    SEASON4A::CSocketItemMgr &g_SocketItemMgr;
    CSItemOption &g_csItemOption;
    CSQuest &g_csQuest;
    CQuestMng &g_QuestMng;
    CErrorReport &g_ErrorReport;
    CMapManager &gMapManager;
};

struct SessionPersonalItemPriceStorage
{
    std::map<int, int> seller;
    std::map<int, int> buyer;
    bool initialized = false;
};

class SessionKeeper;
class SessionLifecycleObserver;
class GameSessionTestPeer;
class SessionOrderedEffectBatch;

class SessionAdvanceUnit final : protected SessionLegacyCalls
{
  public:
    SessionAdvanceUnit(SessionKeeper &keeper,
                       SessionLifecycleObserver *observer = nullptr) noexcept;
    ~SessionAdvanceUnit();

    bool AdvanceFrame() noexcept;
    std::optional<GameplayInteractionFact> CaptureFrameInputOnOwner(
        bool prepareVisibleScene) noexcept;
    std::optional<SessionVisualAnimationInput> CaptureVisualAnimationInputOnOwner() noexcept;
    std::optional<SessionPhysicsFrameInput> CapturePhysicsFrameInputOnOwner() noexcept;
    bool BeginFrameWorkerSafe() noexcept;
    bool BeginFrameWorkerSafe(double frameDeltaMilliseconds, bool renderRequired,
                              const SessionVisualAnimationInput &visualInput,
                              SessionVisualAnimationResult &visualResult) noexcept;
    bool AdvanceMapObserverTickWorkerSafe(SessionOrderedEffectBatch &orderedEffects,
                                          std::uint64_t &nextStableSequence) noexcept;
    bool AdvanceMainSceneTickWorkerSafe(SessionOrderedEffectBatch &orderedEffects,
                                        std::uint64_t &nextStableSequence) noexcept;
    bool AdvanceMainSceneEntitiesWorkerSafe(double frameDeltaMilliseconds,
                                            SessionOrderedEffectBatch &orderedEffects,
                                            std::uint64_t &nextStableSequence) noexcept;
    bool CompleteFrameOnOwner() noexcept;
    bool CompleteFrameOnOwner(const GameplayInteractionFact &interaction) noexcept;
    bool CompleteFrameOnOwner(const GameplayInteractionFact &interaction,
                              SessionOrderedEffectBatch &orderedEffects) noexcept;
    bool CompleteFrameOnOwner(const GameplayInteractionFact &interaction,
                              SessionOrderedEffectBatch &orderedEffects,
                              const SessionVisualAnimationResult &visualResult) noexcept;
    bool BeginOwnerCompletionBeforeSystems(const GameplayInteractionFact &interaction,
                                           SessionOrderedEffectBatch &orderedEffects,
                                           const SessionVisualAnimationResult &visualResult,
                                           std::uint64_t &nextStableSequence) noexcept;
    bool UpdateSystemsWorkerSafe(const SessionPhysicsFrameInput &input) noexcept;
    bool EndOwnerCompletionAfterSystems(SessionOrderedEffectBatch &orderedEffects,
                                        std::uint64_t &nextStableSequence) noexcept;

  private:
    friend class GameSessionTestPeer;

    bool AdvanceLogicalFrameWorkerSafe(double elapsedMilliseconds) noexcept;
    SessionLifecycleObserver *observer_;
};

struct SessionChaosCastleStorage final
{
    CastleLevel currentLevel = CastleLevel::Invalid;
    bool actionMatch = true;
};

struct SessionComGemStorage final
{
    int iUnMixIndex = -1;
    int iUnMixLevel = -1;
    BOOL m_bType = 0;
    char m_cGemType = -1;
    char m_cComType = -1;
    BYTE m_cCount = 0;
    int m_iValue = 0;
    BYTE m_cPercent = 0;
    char m_cState = 0;
    char m_cErr = 0;
};

class GameSessionTestPeer;
struct CharacterLinkedItemVisual;
struct CharacterDrawInput;
struct CharacterEquipmentSet;
class GameSession;
class GMKanturu1st;
class CMVP1STDirection;
class CSkillManager;
class CSItemOption;
class CSQuest;
class CQuestMng;
class CHARACTER_MACHINE;
class CMonkSystem;
class CSPetSystem;
class CErrorReport;
class CCameraMove;
class CBoneManager;
class CmuConsoleDebug;
class PetProcess;
class CUIFriendMenu;
class CUIMng;
class CUITextInputBox;
class CSBaseMatch;
namespace SEASON4A
{
class CSocketItemMgr;
}
class SessionUiUnit;
class SessionKeeper;
class SessionRandom;
class CSkillEffectMgr;
struct SessionSpriteStorage;
class SessionNetworkUnit;
class SessionUiLegacyBindings;
class SessionRenderUnit;
class CSummonSystem;
class SessionLifecycleObserver;
struct LoginCameraState;
struct tagITEM;
namespace SEASON3A
{
class CursedTemple;
class CGM3rdChangeUp;
} // namespace SEASON3A
namespace SEASON3B
{
class CNewUIMessageBoxMng;
class GMNewTown;
} // namespace SEASON3B
namespace MUHelper
{
class SessionMuHelperUnit;
}

class SessionGameplayUnit final : protected SessionLegacyCalls
{
  public:
    void InitPath(); // OMF-00298
    void Clear();
    void Register(int requiredLevel, wchar_t *targetId);
    bool CheckLevel(int requiredLevel, wchar_t *targetId);
    SessionGameplayUnit(SessionKeeper &keeper, SessionLifecycleObserver *observer) noexcept;
    ~SessionGameplayUnit();

    int GetEquipedBowType() const;
    int GetEquipedBowType_Skill() const;
    bool IsEquipedWing() const;
    int SearchArrow() const;
    int SearchArrowCount() const;
    bool GetAttackDamage(int *minimumDamage, int *maximumDamage) const;
    void GetMagicSkillDamage(int type, int *minimumDamage, int *maximumDamage) const;
    void GetCurseSkillDamage(int type, int *minimumDamage, int *maximumDamage) const;
    void GetSkillDamage(int type, int *minimumDamage, int *maximumDamage) const;

    bool PrepareSceneSimulationOnOwner(bool prepareVisibleScene) noexcept;
    bool PrepareMainSceneUpdate(double frameDeltaMilliseconds) noexcept;
    bool AdvanceMainSceneTickWorkerSafe() noexcept;
    bool AdvanceMapObserverTickWorkerSafe() noexcept;
    bool UpdateMainSceneEntitiesWorkerSafe(double frameDeltaMilliseconds) noexcept;
    bool UpdateVisibleSceneSimulationWorkerSafe(double frameDeltaMilliseconds) noexcept;
    bool CompleteSceneUpdateOnOwner(const GameplayInteractionFact &interaction) noexcept;
    std::optional<SessionPhysicsFrameInput> CapturePhysicsFrameInputOnOwner() const noexcept;
    bool UpdateSystems(const SessionPhysicsFrameInput &input) noexcept;
    bool Apply(const GameplayExternalEvent &event) noexcept;
    void CreateEventMatch(int world);
    void DeleteEventMatch();
    void RenderTime();
    void UpdateMatchState();
    void RenderResult();
    void SetPosition(int x, int y);
    void ClearMatchInfo();
    void StartMatchCountDown(int type);
    void SetMatchGameCommand(const LPPRECEIVE_MATCH_GAME_STATE data);
    void SetMatchResult(int resultCount, int myResult, MatchResult *results, int success);
    void Action(CHARACTER *character, OBJECT *object, bool now);
    void SetPlayerAttack(CHARACTER *character);
    void SetAction(OBJECT *object, int action, bool blending = true);
    void SetActionClass(CHARACTER *character, OBJECT *object, int action, int actionType);
    void PushObject(vec3_t pushPosition, vec3_t position, float power, vec3_t angle); // OMF-00285
    void SetAction_Fenrir_Skill(CHARACTER *character, OBJECT *object);
    void SetAction_Fenrir_Damage(CHARACTER *character, OBJECT *object);
    void SetAction_Fenrir_Run(CHARACTER *character, OBJECT *object);
    void SetAction_Fenrir_Walk(CHARACTER *character, OBJECT *object);
    void SetAttackSpeed();
    void SetPlayerHighBowAttack(CHARACTER *character);
    void SetPlayerBow(CHARACTER *character);
    void SetPlayerHighBow(CHARACTER *character);
    void SetPlayerMagic(CHARACTER *character);
    void SetPlayerTeleport(CHARACTER *character);
    void SetAllAction(int action);
    void UpdateCharactersAnimationParallel(std::span<CHARACTER *const> characters);
    bool CharacterVisibleToObserver(const CHARACTER &character);
    void EmitMeshEffects(BMD &model, int mesh, int type, int subtype = 0, vec3_t angle = nullptr,
                         void *owner = nullptr);
    void ActionObject(OBJECT *object);
    void AddTerrainAttribute(int x, int y, BYTE attribute);
    void AddTerrainAttributeRange(int x, int y, int width, int height, BYTE attribute,
                                  BYTE add = 0);
    void AdvanceMapCharacterTransitions(CHARACTER &character, BMD &model);
    void ClearActionObject();
    void ClearCharacters(int key = -1);
    OBJECT *CreateObject(int type, vec3_t position, vec3_t angle, float scale = 1.0f);
    void CreateShiny(OBJECT *object);
    void DeleteCharacter(CHARACTER *character, OBJECT *object);
    void DeleteCharacter(int key);
    void DetachCharacterBindings(CHARACTER *character);
    void DetachCharacterVisuals(CHARACTER *character);
    void HandleItemFalling(OBJECT *object);
    void HandleItemOnGround(OBJECT *object);
    void ItemObjectAttribute(OBJECT *object);
    void MoveItems();
    bool PrepareCharacterObservation(CHARACTER &character);
    void ReleaseCharacters();
    float RequestTerrainHeight(float x, float y);
    void RequestTerrainLight(float x, float y, vec3_t light);
    void RequestTerrainNormal(float x, float y, vec3_t normal);
    void SetActionObject(int world, int type, int lifeTime, int velocity = -1);
    void SubTerrainAttribute(int x, int y, BYTE attribute);
    void CreateItemDrop(ITEM_t *item, ItemCreationParams params, vec3_t position, bool isFreshDrop);
    void CreateMoneyDrop(ITEM_t *item, int amount, vec3_t position, bool isFreshDrop);
    void CreateOperate(OBJECT *owner);
    void DeleteObjectTile(int x, int y);
    BYTE TERRAIN_ATTRIBUTE(float x, float y);
    bool EquipItem(int iIndex, std::span<const BYTE> pbyItemPacket);
    void UnequipItem(int iIndex);
    void UnequipAllItems();
    void ClearInventoryState();
    bool IsEquipable(int iIndex, ITEM *pItem) const;
    void CreateEquippingEffect(ITEM *pItem);
    void DeleteEquippingEffectBug(ITEM *pItem);
    void DeleteEquippingEffect();
    bool EquipmentSlotAccepts(int index, const ITEM &item) const;
    bool RestorePickedItem();
    void ReceiveCursedTempRegisterSkill(const BYTE *packet);
    void ReceiveCursedTempUnRegisterSkill(const BYTE *packet);
    void SendReqMix();
    void SendReqUnMix();
    void ProcessCSAction();
    int GetUnMixGemLevel() const;
    char CheckOneItem(const ITEM *item) const;
    void CalcGen();
    char CalcCompiledCount(const ITEM *item) const;
    int CalcItemValue(const ITEM *item) const;
    int CalcEmptyInv() const;
    int GetJewelRequireCount(int index) const;
    int Check_Jewel(int jewel, int type = 0, bool model = false) const;
    int GetJewelIndex(int jewel, int type) const;
    void SetMode(BOOL mode);
    void SetGem(char gem);
    void SetComType(char type);
    void SetState(char state);
    void SetError(char error);
    char GetError() const;
    bool isComMode() const;
    int Check_Jewel_Unit(int jewel, bool model = false) const;
    int Check_Jewel_Com(int jewel, bool model = false) const;
    bool isCompiledGem(const ITEM *item) const;
    bool isAble() const;
    bool CheckMyInvValid();
    void Init();
    void GetBack();
    void Exit();
    void RequestInventoryRefresh();
    void UpdateHeroHeight();
    void EmitGolemMeshDebris(vec3_t position, int subtype, vec3_t angle, vec3_t light, int &count);
#ifdef ENABLE_EDIT2
    void UpdateEditorMovement();
#endif
    void SetCharacterTerrainHeight(CHARACTER &character);
    void AdvanceHalloweenFormState(CHARACTER &character);
    void AdvanceCharacterPresentationState(CHARACTER &character);
    void CreatePartsFactory(CHARACTER *character);
    void DeleteParts(CHARACTER *character);
    void PrepareLocalCharacterPose(CHARACTER &character);
    void RefreshPendingCharacterPose(CHARACTER &character);
    void RefreshLocalCharacterPose(CHARACTER &character);
    CharacterEquipmentSet EvaluateCharacterEquipmentSet(const CHARACTER &character);
    bool CheckMonsterSkill(CHARACTER *character, OBJECT *object);
    int ExecuteSkill(CHARACTER *character, ActionSkillType skill, float distance);
    CHARACTER *CreateMonster(EMonsterType type, int positionX, int positionY, int key = 0);
    CHARACTER *CreateHellGate(char *id, int key, EMonsterType index, int x, int y, int createFlag);
    SessionNetworkUnit *Network() const noexcept;
    CNewYearsDayEvent &NewYearsDayEvent() noexcept
    {
        return newYearsDayEvent_;
    }
    CXmasEvent &XmasEvent() noexcept
    {
        return xmasEvent_;
    }
    C09SummerEvent &SummerEvent() noexcept
    {
        return summerEvent_;
    }
    CMapManager &MapManagerObject() noexcept
    {
        return gMapManager;
    }
    CameraManager &CameraManagerObject() noexcept
    {
        return cameraManager_;
    }
    CameraProjection &CameraProjectionObject() noexcept
    {
        return cameraProjection_;
    }
    void UpdateResolutionDependentSystems();
    void CheckChatText(wchar_t *text);
    void RetireCharacterEffectTargets(std::span<OBJECT *const> targets);
    void ClearWorldEffects();

    // Shared birth clock for map, character and effect producers.
    EffectEmissionScope EmissionTime(float remainingFrames)
    {
        return EffectEmissionScope(emissionRemainingFrames_, remainingFrames,
                                   effectFrameFrames_ > 0.f ? effectFrameFrames_
                                                            : FPS_ANIMATION_FACTOR,
                                   effectIntervalTail_);
    }
    EffectUpdateInterval EffectInterval(float duration, float offset = 0.f)
    {
        return EffectUpdateInterval(FPS_ANIMATION_FACTOR, effectIntervalTail_,
                                    emissionRemainingFrames_, duration, offset);
    }
    // Continuous mean counts retain EmissionCount rounding; exact events use EmissionTime.
    EffectEmissionSequence Emissions(double expectedCount);
    EffectEmissionSequence Emissions(double expectedCount, float duration, float offset = 0.f);
    void BeginEffectBirths();
    void FinishEffectBirths();
    std::optional<float> WaterWaveRemainingFrames() const
    {
        if (emissionRemainingFrames_)
            return *emissionRemainingFrames_ + effectIntervalTail_;
        return std::nullopt;
    }

  private:
    float boidSteeringFrames_ = 0.f;
    float fishSteeringFrames_ = 0.f;
    using SessionLegacyCalls::DeleteCharacter;
    friend class SessionLegacyCalls;
    friend class GameSessionTestPeer;
    friend class CDirection;
    friend class CharacterRetirementTestPeer;
    friend class CUIPhotoViewer;
    friend class GameSession;
    friend class SessionUiUnit;
    friend class SessionUiLegacyBindings;

    CUIMng &LegacyUiManager();

    void AdvanceBossLaser(OBJECT &object);
    void AdvanceWaterDebris(OBJECT &object, float frames);
    void FinishScriptedAnimationPhase(OBJECT &object, BMD &model);
    void EmitThunderBursts(OBJECT &object, float frames);
    void AdvanceUmbrellaGold(OBJECT &object, float frames);
    void SlideUmbrellaOnGround(OBJECT &object, float frames);
    void ApplyProjectileImpact(OBJECT &projectile, float range);
    float EffectLuminosity(float lifetime);
    void AdvanceMapEffectVisual(OBJECT &object);
    void AdvanceModelEffectVisual(OBJECT &object);
    bool CheckMovementSkillTarget(CHARACTER *character);
    bool CheckCharacterRange(OBJECT *source, float range, short pkKey, BYTE kind = 0);
    void CheckClientArrow(OBJECT *object);
    void CheckSkull(OBJECT *object);
    void CheckTargetRange(OBJECT *object);
    void ClearAllObjectBlurs();
    void CreateArrow(CHARACTER *character, OBJECT *object, OBJECT *target, WORD skillIndex,
                     WORD skill, WORD skillKey);
    void CreateArrows(CHARACTER *character, OBJECT *object, OBJECT *target, WORD skillIndex = 0,
                      WORD skill = 1, WORD skillKey = 0);
    void CreateBlood(OBJECT *object);
    void CreateBlur(CHARACTER *owner, vec3_t first, vec3_t second, vec3_t light, int type,
                    bool shortBlur = false, int subType = 0);
    void CreateBomb(vec3_t position, bool explode, int subType = 0);
    void CreateBomb2(vec3_t position, bool explode, int subType = 0, float scale = 0.0f);
    void CreateBomb3(vec3_t position, int subType, float scale = 1.0f);
    void CreateBonfire(vec3_t position, vec3_t angle);
    void CreateEffect(int type, vec3_t position, vec3_t angle, vec3_t light, int subType = 0,
                      OBJECT *owner = nullptr, short pkKey = -1, WORD skillIndex = 0,
                      WORD skill = 0, WORD skillSerialNumber = 0, float scale = 0.0f,
                      int targetIndex = -1);
    void CreateEffectFpsChecked(int type, vec3_t position, vec3_t angle, vec3_t light,
                                int subType = 0, OBJECT *owner = nullptr, short pkKey = -1,
                                WORD skillIndex = 0, WORD skill = 0, WORD skillSerialNumber = 0,
                                float scale = 0.0f, int targetIndex = -1);
    void CreateFire(int type, OBJECT *object, float x, float y, float z);
    void CreateForce(OBJECT *object, vec3_t position);
    void CreateHealing(OBJECT *object);
    void CreateInferno(vec3_t position, int subType = 0);
    void CreateJoint(int type, vec3_t position, vec3_t targetPosition, vec3_t angle,
                     int subType = 0, OBJECT *target = nullptr, float scale = 10.0f,
                     short pkKey = -1, WORD skillIndex = 0, WORD skillSerialNumber = 0,
                     int characterIndex = -1, const float *priorColor = nullptr,
                     short targetIndex = -1);
    void CreateJointFpsChecked(int type, vec3_t position, vec3_t targetPosition, vec3_t angle,
                               int subType = 0, OBJECT *target = nullptr, float scale = 10.0f,
                               short pkKey = -1, WORD skillIndex = 0, WORD skillSerialNumber = 0,
                               int characterIndex = -1, const float *priorColor = nullptr,
                               short targetIndex = -1);
    void CreateMagicShiny(CHARACTER *character, int hand = 0);
    void CreateMyGensInfluenceGroundEffect();
    void CreateObjectBlur(OBJECT *owner, vec3_t first, vec3_t second, vec3_t light, int type,
                          bool shortBlur = false, int subType = 0, int limitLifeTime = -1);
    int CreateParticle(int type, vec3_t position, vec3_t angle, vec3_t light, int subType = 0,
                       float scale = 1.0f, OBJECT *owner = nullptr);
    int CreateParticleFpsChecked(int type, vec3_t position, vec3_t angle, vec3_t light,
                                 int subType = 0, float scale = 1.0f, OBJECT *owner = nullptr);
    void CreatePoint(vec3_t position, int value, vec3_t color, float scale = 15.f, bool move = true,
                     bool repeatedly = false);
    void CreatePointer(int type, vec3_t position, float angle, vec3_t light, float scale = 1.f);
    void CreateSpark(int type, CHARACTER *character, vec3_t position, vec3_t angle);
    void CreateTeleportBegin(OBJECT *object);
    void CreateTeleportEnd(OBJECT *object);
    bool DeleteEffect(int type, OBJECT *owner, int subType = -1);
    void DeleteEffect(int effectType);
    void DeleteJoint(int type, OBJECT *target, int subType = -1);
    bool DeleteParticle(int type);
    template <class T> static void RetireBlur(SessionEffectPool<T> &pool, T &blur)
    {
        auto first = std::move(blur.P1);
        auto second = std::move(blur.P2);
        pool.Retire(blur);
        if constexpr (requires { blur.BreakAfter; })
        {
            auto gaps = std::move(blur.BreakAfter);
            blur = {};
            blur.BreakAfter = std::move(gaps);
            blur.BreakAfter.Clear();
        }
        else
            blur = {};
        blur.P1 = std::move(first);
        blur.P2 = std::move(second);
        blur.P1.Clear();
        blur.P2.Clear();
    }
    void RetireEffect(OBJECT *object);
    void EffectDestructor(OBJECT *object);
    BOOL FindSameEffectOfSameOwner(int type, OBJECT *owner);
    void GetMagicScrew(int parameter, vec3_t result, float speedRate = 1.0f);
    void HandPosition(PARTICLE *particle);
    void MoveBlurs();
    void MoveEffect(OBJECT *object, int index);
    void MoveEffects();
    void EmitSwellMagicBirths(OBJECT &effect);
    void EmitSwellHandPair(OBJECT &effect, const EffectEmissionScope &birth);
    void EmitShinyModelCloud(OBJECT &effect);
    void EmitLightningOrbParticles(OBJECT &effect);
    void EmitMagicBoneCloud(OBJECT &effect);
    void MoveEtcLeaf(PARTICLE *particle);
    void MoveAirLeaf(PARTICLE *particle, bool rotate, bool discreteNoise);
    void MoveJoint(JOINT *joint, int index);
    void EmitAttachedEnergyParticles(JOINT &joint);
    void MoveLightningRibbon(JOINT *joint);
    void MoveJoints();
    bool MoveJump(OBJECT *object);
    bool MoveLeaves();
    void MoveObjectBlurs();
    void MoveParticle(OBJECT *object, int turn);
    void MoveParticle(OBJECT *object, vec3_t angle);
    void AdvanceLightningParticle(PARTICLE *particle);
    void AdvanceRaklionCloud(PARTICLE &cloud, int index);
    void AdvanceAgAddition(PARTICLE &particle, int index);
    void AdvanceExplosionRings(PARTICLE &particle);
    void EmitFallingShinyChildren(PARTICLE &particle);
    void MoveParticles();
    void MoveParticleEntry(PARTICLE *particle, int index);
    void MovePointers();
    void MovePointerEntry(PARTICLE *pointer, int index);
    void MovePoints();
    void MovePointEntry(PARTICLE *point, int index);
    void RemoveObjectBlurs(OBJECT *owner, int subType = 0);
    bool SearchEffect(int type, OBJECT *owner, int subType = -1);
    bool SearchJoint(int type, OBJECT *target, int subType = -1);
    void TerminateOwnerEffectObject(int ownerObjectType = -1);

    int FindHotKey(int skill);                                         // OMF-00472
    bool CheckTile(CHARACTER *character, OBJECT *object, float range); // OMF-00444
    bool SkillKeyPush(int skill);                                      // OMF-00467
    void SetCharacterScale(CHARACTER *character);                      // OMF-00371
    bool IsIllegalMovementByUsingMsg(const wchar_t *text);             // OMF-00493
    void ChangeCharacterExt(int key, BYTE *equipment, CHARACTER *character = nullptr,
                            OBJECT *helper = nullptr);
    void ReadEquipmentExtended(int key, BYTE flags, BYTE *equipment, CHARACTER *character = nullptr,
                               OBJECT *helper = nullptr);
    friend class SessionNetworkUnit;
    friend class SessionRenderUnit;
    friend class SessionVisualUnit;
    friend class CMVP1STDirection;
    friend class SEASON3B::GMNewTown;
    friend class GMKanturu1st;
    friend class MUHelper::SessionMuHelperUnit;
    bool TryRequestGateTransfer(int gate);
    bool PauseHeroForWorldTransfer(CHARACTER *character);
    bool UpdateSceneGameplayOnOwner();
    bool UpdateSceneGameplaySystems(const SessionPhysicsFrameInput &input);
    bool UpdateGameplaySceneOnOwner();
    void CheckServerConnection();
    bool UpdateApplicationSceneCompatibility();
    void UpdateMainSceneGameplay();
    void UpdateSwitchState();
    bool RequireLeavesEffect();
    void ManageMainSceneAudio();
    bool PrepareLoginProtocolOnOwner();
    void StartLoginPresentation();
    void InitializeCharacterSceneInput();
    bool CreateLogInScene();
    bool CreateCharacterScene();
    bool PrepareLogInSceneSimulationOnOwner(bool prepareVisibleScene);
    bool PrepareCharacterSceneSimulationOnOwner(bool prepareVisibleScene);
    bool UpdateLogInSceneSimulationWorkerSafe();
    bool UpdateCharacterSceneSimulationWorkerSafe();
    void MoveCharacterCamera(vec_t *origin, vec_t *position, vec_t *angle);
    int GetLoginCameraCount();
    int GetLoginCameraWalkCut();
    void InitializeLoginCamera();
    void CalculateWalkDelta();
    void SelectNextWaypoint();
    void UpdateCameraWaypoint();
    void InterpolateCameraMovement();
    void MoveCamera();
    void NewMoveLogInScene();
    void NewMoveCharacterScene();
    void StartGame();
    void TryAutoSelectCharacter();
    void UpdateUIAndInput();
    void UpdateGameEntities();
    void AdvanceEffectShadow(OBJECT &effect, float previousLifetime);
    void AdvanceEffectAnimation(OBJECT &effect);
    void EmitModelEffectParticles(OBJECT &effect, float frames);
    void AdvanceWeaponEffect(OBJECT &effect);
    void AdvanceDroppedItemVisual(OBJECT &item, int index);
    void RetireGroundItem(ITEM_t &item);
    void EmitStandalonePartVisual(OBJECT &owner, const ObjectDrawInput &draw, int type,
                                  const vec3_t light);
    void EmitWingItemVisual(OBJECT &owner, const ObjectDrawInput &draw, int type,
                            const vec34_t *bones,
                            const CharacterLinkedItemVisual *linkedItem = nullptr,
                            const CharacterDrawInput *parentDraw = nullptr);
    void EmitWingItemParticles(OBJECT &owner, const ObjectDrawInput &draw, int type,
                               const vec34_t *bones, const CharacterLinkedItemVisual *linkedItem,
                               const CharacterDrawInput *parentDraw);
    void EmitDarkWingLinks(OBJECT &owner, const ObjectDrawInput &draw, const vec34_t *bones,
                           double time);
    void EmitStormWingThunder(OBJECT &owner, const ObjectDrawInput &draw, const vec34_t *bones);
    void EmitIllusionWingShiny(const ObjectDrawInput &draw, const vec34_t *bones, double time);
    void EmitRuinWingChrome(const ObjectDrawInput &draw, const vec34_t *bones);
    void EditObjects();
    void EditTerrainMapping();
    void Setting_Monster(CHARACTER *character, EMonsterType type, int positionX, int positionY);
    void MoveHero();
    void SendCharacterMove(unsigned short key, float angle, unsigned char pathNum,
                           unsigned char *pathX, unsigned char *pathY, unsigned char targetX,
                           unsigned char targetY);
    void LetHeroStop(CHARACTER *character = nullptr, BOOL setMovementFalse = FALSE);
    void SetCharacterPos(CHARACTER *character, BYTE positionX, BYTE positionY, vec3_t position);
    void SendRequestAction(OBJECT &object, BYTE action);
    bool PathFinding2(int sx, int sy, int tx, int ty, PATH_t *pathState, float distance = 0.0f,
                      int defaultWall = TW_CHARACTER);
    void MoveMonsterClient(CHARACTER *character, OBJECT *object);
    void MoveCharacterClient(CHARACTER *character);
    void PrepareCharacterOccupancy();
    void MoveCharactersClient();

    void DeleteBoids();
    void EmitEventMeteor(float fraction = 1.f);
    void MoveBoids();
    float MoveHumming(vec3_t position, vec3_t angle, vec3_t targetPosition, float turn);
    void MovePosition(vec3_t position, vec3_t angle, vec3_t speed);
    void MoveBoid(OBJECT *object, int index, OBJECT *boids, int maximum, float frames,
                  bool preparedSteering = false);
    void RefreshFlockSteering(OBJECT *objects, int count);
    void AdvanceBoidStep(OBJECT *object, int index, int heroTile, float frames, double endTime);
    bool rand_fps_check(int referenceFrames);

    void Damage(vec3_t sourcePosition, CHARACTER *target, float attackRange, int attackPoint,
                bool hit);
    void MonsterMoveSandSmoke(OBJECT *object);
    BOOL PlayMonsterSoundGlobal(OBJECT *object);
    void MoveTournamentInterface();
    void MoveBattleSoccerEffect(CHARACTER *character);
    void AdvanceAmbientBoid(OBJECT *object, int index, float frames, float animationSpeed,
                            double endTime = -1.0, bool preparedSteering = false);
    void AdvanceFishMotion(OBJECT *object, int index, float frames, bool preparedSteering = false);
    void AdvanceFlyingMount(OBJECT *object, const vec3_t target, float flyRange, float frames);
    void MoveBat(OBJECT *object, float frames);
    void MoveButterFly(OBJECT *object, float frames, bool refresh);
    float MoveBird(OBJECT *object, float frames, double time, bool refresh);
    void MoveEagle(OBJECT *object, float frames, double time, bool refresh);
    void AdvanceEagleAnimation(OBJECT &object, BMD &model, float frames, float speed, bool refresh);
    void MoveTornado(OBJECT *object, float frames, bool refresh);
    CHARACTER *CharacterForMountOwner(OBJECT *owner);
    void PrepareMountPose(OBJECT &object, AnimationPoseSample *pose = nullptr);
    bool CreateMountSub(int type, vec3_t position, OBJECT *owner, OBJECT *object, int subType = 0,
                        int linkBone = 0);
    void DeleteMount(OBJECT *owner);
    void CreateMount(int type, vec3_t position, OBJECT *owner, int subType = 0, int linkBone = 0);
    bool SynchronizeMount(OBJECT &object);
    bool MoveMount(OBJECT *object, bool forceRender = false, CHARACTER *owner = nullptr,
                   AnimationPoseSample *pose = nullptr);
    void MoveMounts();

    void MoveHeavenBug(OBJECT *object, int index, float frames, double time);
    void MoveBoidGroup(OBJECT *object, int index, float frames, bool refresh,
                       float movementVelocity, bool preparedSteering = false,
                       double endTime = -1.0);
    void MoveFishs();
    bool AdvanceFishAnimation(OBJECT &object);
    bool AdvanceFishAnimation(OBJECT &object, float frames);
    bool AdvanceFishAnimation(OBJECT &object, float frames, bool moving);
    void AdvanceFishStep(OBJECT *object, int index, float frames, double endTime);
    void ClearItems();
    BYTE MakeSkillSerialNumber(BYTE *serialNumber);
    void MoveCharacter(CHARACTER *character, OBJECT *object);
    void AdvanceCharacterEnvironmentState(CHARACTER &character);
    void SetCharacterTarget(CHARACTER &character, int index);
    void BindCharacterTarget(CHARACTER &character);
    void AdvanceAttackEffects(CHARACTER *character, OBJECT *object);
    void AttackEffect(CHARACTER *character);
    void EmitBossMeteors(OBJECT &object);
    void DarkPhoenixAttackPosition(const CHARACTER &character, bool wing, vec3_t position,
                                   float fraction = 1.f);
    void FallingCharacter(CHARACTER *character, OBJECT *object);
    void PushingCharacter(CHARACTER *character, OBJECT *object);
    void AdvanceDefaultCharacterPush(CHARACTER *character, OBJECT *object, float speed);
    void DeadCharacter(CHARACTER *character, OBJECT *object, BMD *model);
    void HeroAttributeCalc(CHARACTER *character);
    void AnimationCharacter(CHARACTER *character, OBJECT *object, BMD *model);
    void CreateWeaponBlur(CHARACTER *character, OBJECT *object, BMD *model);
    void SetPlayerStop(CHARACTER *character);
    bool AttackStage(CHARACTER *character, OBJECT *object);
    void OnlyNpcChatProcess(CHARACTER *character, OBJECT *object);
    void PlayerNpcStopAnimationSetting(CHARACTER *character, OBJECT *object);
    void PlayerStopAnimationSetting(CHARACTER *character, OBJECT *object);
    void PlayWalkSound();
    bool CheckFullSet(CHARACTER *character);
    float CharacterMoveSpeed(CHARACTER *character, float runFrames = -1.f);
    void MoveCharacterPosition(CHARACTER *character);
    void FinishCharacterMovement(CHARACTER *character, float frames);
    bool CanAdvanceCharacterRun(const CHARACTER &character);
    bool AdvanceCharacterPath(CHARACTER *character, bool preserveAction = false);
    void CreateCharacterPointer(CHARACTER *character, int type, unsigned char positionX,
                                unsigned char positionY, float rotation = 0.0f);
    void DeletePet(CHARACTER *character);
    void CreatePetDarkSpirit(CHARACTER *character);
    void CreatePetDarkSpirit_Now(CHARACTER *character);
    void SetCharacterClass(CHARACTER *character);
    CHARACTER *CreateCharacter(int key, int type, unsigned char positionX, unsigned char positionY,
                               float rotation = 0.0f);
    int FindCharacterIndex(int key);
    int FindCharacterIndexByMonsterIndex(int type);
    int HangerBloodCastleQuestItem(int key);
    CHARACTER *FindCharacterByID(wchar_t *name);
    CHARACTER *FindCharacterByKey(int key);
    void SetChangeClass(CHARACTER *character);
    CHARACTER *CreateHero(int key, CLASS_TYPE characterClass, int skin = 0, float x = 0.0f,
                          float y = 0.0f, float rotation = 0.0f);
    float CharacterAnimationSpeed(CHARACTER *character, OBJECT *object, BMD *model, float frame);
    float CharacterAnimationBoundary(CHARACTER *character, OBJECT *object, BMD *model);
    bool CharacterAnimation(CHARACTER *character, OBJECT *object);
    bool CharacterAnimation(CHARACTER *character, OBJECT *object, float frames);
    void EtcStopAnimationSetting(CHARACTER *character, OBJECT *object);
    void SetPlayerWalk(CHARACTER *character, float runFrames = -1.f);
    BOOL PlayMonsterSound(OBJECT *object);
    void SetPlayerShock(CHARACTER *character, int hit);
    void SetPlayerDie(CHARACTER *character);
    void Attack(CHARACTER *character);
    bool HandleHeroPositionSlide(CHARACTER *character);
    void AttackElf(CHARACTER *character, int skill, float distance);
    void AttackKnight(CHARACTER *character, ActionSkillType skill, float distance);
    void AttackWizard(CHARACTER *character, int skill, float distance);
    void AttackRagefighter(CHARACTER *character, int skill, float distance);
    bool CheckTarget(CHARACTER *character);
    bool CanExecuteSkill(CHARACTER *character, ActionSkillType skill, float distance);
    void AttackCommon(CHARACTER *character, int skill, float distance);
    void UseSkillWizard(CHARACTER *character, OBJECT *object);
    void UseSkillSummon(CHARACTER *character, OBJECT *object);
    void UseSkillRagefighter(CHARACTER *character, OBJECT *object);
    bool UseSkillRagePosition(CHARACTER *character);
    bool SendAttackPacket(CHARACTER *character, int moveTarget, int skill);
    void SetDarksideTargetIndex(WORD *targetIndex, ActionSkillType skill);
    BOOL SendRequestSummonSkill(int skill, CHARACTER *character, OBJECT *object);
    bool CheckMana(CHARACTER *character, int skill);
    void SendRequestMagic(int type, int key);
    void SendRequestMagicContinue(int type, int x, int y, int angle, BYTE destination,
                                  BYTE targetPosition, WORD targetKey, BYTE *skillSerial);
    bool CheckCommand(wchar_t *text, bool macroText = false);
    void SendMacroChat(wchar_t *text);
    void MoveInterface();
    bool CheckAttack_Fenrir(CHARACTER *character);
    bool IsVirtualKeyPressed(int virtualKey); // OMF-00818
    bool CheckWall(int sx1, int sy1, int sx2, int sy2);
    bool IsGMCharacter();
    bool IsNonAttackGM();
    BYTE CaculateFreeTicketLevel(int type) const;
    bool CheckAttack();
    bool SendPetCommand(CHARACTER *character, int index);
    void ClearRightMouseInputState();
    CSPetSystem *ResolvePetSystem(CHARACTER *character); // OMF-00819
    void InitPetManager();
    void MovePet(CHARACTER *character);
    bool SelectPetCommand();
    void SetPetCommand(CHARACTER *character, int key, std::uint8_t command);
    void SetAttack(CHARACTER *character, int key, int attackType);
    void InitItemBackup();
    void SetPetInfo(std::uint8_t inventoryType, std::uint8_t inventoryPosition, PET_INFO *petInfo);
    PET_INFO *GetPetInfo(ITEM *item) const;
    void CalcPetInfo(PET_INFO *petInfo);
    std::uint8_t GetPetDefenseBonus(const PET_INFO &pet) const;
    void SetPetItemConvert(ITEM *item, PET_INFO *petInfo) const;
    std::uint32_t GetPetItemValue(PET_INFO *petInfo) const;
    int getTargetCharacterKey(CHARACTER *character, int selected);
    bool IsCanBCSkill(int type);
    bool CheckSkillUseCondition(OBJECT *object, int type);
    void ReloadArrow();
    bool CheckArrow();
    void SendRequestUse(int index, int target, bool addPoints = true);
    bool SendRequestEquipmentItem(STORAGE_TYPE sourceType, int sourceIndex, ITEM *item,
                                  STORAGE_TYPE destinationType, int destinationIndex);
    void SendMove(CHARACTER *character, OBJECT *object);
    bool SkillElf(CHARACTER *character, tagITEM *item);
    void UseSkillElf(CHARACTER *character, OBJECT *object);
    bool CastWarriorSkill(CHARACTER *character, OBJECT *object, tagITEM *item,
                          ActionSkillType skill);
    bool SkillWarrior(CHARACTER *character, tagITEM *item);
    void UseSkillWarrior(CHARACTER *character, OBJECT *object);
    void CheckGate();
    MapProcess &TheMapProcess();
    void DeleteCharacter();

    SessionRandom &Random;
    GameLogic::Effects::Behaviors::MoveBehavior effectMoveBehavior_;
    SessionEffectPool<OBJECT> &Effects;
    SessionEffectPool<PARTICLE> &Particles;
    SessionEffectPool<JOINT> &Joints;
    SessionEffectPool<PARTICLE> &Points;
    SessionEffectPool<PARTICLE> &Pointers;
    SessionEffectPool<Blur> &g_blurs;
    SessionEffectPool<ObjectBlur> &g_objectBlurs;
    SessionSpriteStorage &Sprites;
    SessionLeaves &Leaves;
    CSkillEffectMgr &g_SkillEffects;

    CSBaseMatch *&g_csMatchInfo;
    SEASON4A::CSocketItemMgr &g_SocketItemMgr;
    CSItemOption &g_csItemOption;
    CSQuest &g_csQuest;
    CQuestMng &g_QuestMng;
    CHARACTER_MACHINE *const CharacterMachine;
    CHARACTER_ATTRIBUTE *const CharacterAttribute;
    OBJECT (&Mounts)[MAX_MOUNTS];
    OBJECT (&Boids)[MAX_BOIDS];
    OBJECT (&Fishs)[MAX_FISHS];
    OPERATE (&Operates)[MAX_OPERATES];
    ITEM_t (&Items)[MAX_ITEMS];
    WORD &g_byLastSkillSerialNumber;
    OBJECT_BLOCK (&ObjectBlock)[256];
    wchar_t (&MacroText)[10][256];
    vec3_t &CollisionPosition;
    float &SelectXF;
    float &SelectYF;
    MATCH_RESULT &g_wtMatchResult;
    PMSG_MATCH_TIMEVIEW &g_wtMatchTimeLeft;
    int &g_iGoalEffect;
    CMapManager &gMapManager;
    CCameraMove &cameraMove_;
    LoginCameraState &g_loginCamera;
    CDirection &g_Direction;
    CSkillManager &gSkillManager;
    CSummonSystem &g_SummonSystem;
    CMonkSystem &g_CMonkSystem;
    SEASON3B::CNewUIMessageBoxMng &g_MessageBox;
    PetProcess &g_petProcess;
    CUIFriendMenu *&g_pFriendMenu;
    CUITextInputBox *&g_pSinglePasswdInputBox;
    DWORD &g_dwKeyFocusUIID;
    SEASON3A::CursedTemple &cursedTemple_;
    SEASON3A::CGM3rdChangeUp &thirdChange_;
    CameraProjection cameraProjection_;
    CameraManager cameraManager_;
    CXmasEvent xmasEvent_;
    CNewYearsDayEvent newYearsDayEvent_;
    C09SummerEvent summerEvent_;
    MapProcessPtr g_MapProcess;
    std::int32_t &RepresentativeScalar;
    std::int32_t (&RepresentativeFixedArray)[4];
    std::int32_t (&RepresentativeMatrix)[2][3];
    std::int32_t *&RepresentativePointer;
    std::vector<std::int32_t> &RepresentativeContainer;
    void (*&RepresentativeCallback)(std::int32_t &) noexcept;
    std::atomic<std::int32_t> &RepresentativeAtomic;
    GATE_ATTRIBUTE *&GateAttribute;
    MONSTER_SCRIPT (&MonsterScript)[MAX_MONSTER];
    Script_Skill (&MonsterSkill)[MODEL_MONSTER_END];
    CErrorReport &g_ErrorReport;
    CBoneManager &boneManager_;
    CmuConsoleDebug &g_ConsoleDebug;
    HWND &g_hWnd;
    CameraState &g_Camera;
    using NewEffect = std::variant<OBJECT *, JOINT *, PARTICLE *>;
    enum class BirthParticlePool
    {
        Particles,
        Pointers,
        Points
    };
    struct PendingBirth
    {
        NewEffect effect;
        int index;
        std::uint64_t serial;
        BirthParticlePool particlePool;
    };
    void RegisterEffectBirth(EffectBirthTiming &timing, NewEffect effect, int index,
                             BirthParticlePool particlePool = BirthParticlePool::Particles);
    void EmitLightningArrival(JOINT *joint, float duration, float offset);
    std::optional<float> emissionRemainingFrames_;
    float effectFrameFrames_ = 0.f;
    float effectIntervalTail_ = 0.f;
    bool effectFrameActive_ = false;
    std::vector<PendingBirth> pendingBirths_;
    std::uint64_t nextBirthSerial_ = 0;
    float &FPS_ANIMATION_FACTOR;
    double &WorldTime;
    std::chrono::steady_clock::time_point &g_timer2StartTickTime;
    BOOL &g_bUseWindowMode;
    unsigned int &WindowWidth;
    unsigned int &WindowHeight;
    bool &Destroy;
    bool loginProtocolPrepared_ = false;
    bool loginSceneSimulationEnabled_ = false;
    BOOL serverConnectionClosed_ = FALSE;
    int &g_iActionObjectType;
    int &g_iActionWorld;
    float &g_iActionTime;
    float &g_fActionObjectVelocity;
    SessionLifecycleObserver *observer_;
};

class SessionGameplayView
{
  public:
    virtual ~SessionGameplayView() = default;

    virtual bool UpdateGameplay(const GameplayInteractionFact &interaction) noexcept = 0;
    virtual bool UpdateGameplaySystems() noexcept = 0;
    virtual bool ApplyGameplayExternalEvent(const GameplayExternalEvent &event) noexcept = 0;
    virtual bool UsesSessionOwnedLegacyBehavior() const noexcept
    {
        return false;
    }
};

enum eCurrentMode : int
{
    MODE_NONE,
    MODE_CREATE_GUILD,
    MODE_EDIT_GUILDMARK
};
enum eCurrentStep : int
{
    STEP_MAIN,
    STEP_CREATE_GUILDINFO,
    STEP_EDIT_GUILD_MARK,
    STEP_CONFIRM_GUILDINFO,
};

struct SessionGuildMasterStorage final
{
    eCurrentMode m_nCurrMode = MODE_NONE;
    eCurrentStep m_eCurrStep = STEP_MAIN;
    GuildRelationshipType m_byRelationShipType = GuildRelationshipType::Undefined;
    GuildRequestType m_byRelationShipRequestType = GuildRequestType::Undefined;
    BYTE m_byTargetUserIndexH = 0;
    BYTE m_byTargetUserIndexL = 0;
};

class SessionKeeper;
class CMapManager;
class CameraState;
class SessionLifecycleObserver;
class GameSessionTestPeer;

// Computes the per-frame mouse pick for monsters, players, NPCs, items, and
// other operable objects.
class SessionInteractionUnit final : protected SessionLegacyCalls
{
  public:
    OBJECT *CollisionDetectObjects(OBJECT *pickObject);
    SessionInteractionUnit(SessionKeeper &keeper, SessionLifecycleObserver *observer) noexcept;
    ~SessionInteractionUnit();

    GameplayInteractionFact PickForLogic() noexcept;
    bool PrepareFrame() noexcept;
    int SelectItem();
    int SelectCharacter(BYTE kind);
    int SelectOperate();
    void SelectObjects();
    void BuildCharacterPickOBB(const CHARACTER &character, OBB_t &bounds);
    void BuildCharacterScenePickOBB(const OBJECT *object, OBB_t &bounds);

  private:
    friend class GameSessionTestPeer;
    void ResetCharacterPickLighting(BYTE kind);
    void ResetAutoAttackSelection();

    CMapManager &gMapManager;
    CameraState &g_Camera;
    OPERATE (&Operates)[MAX_OPERATES];
    ITEM_t (&Items)[MAX_ITEMS];
    SessionLifecycleObserver *observer_;
};

class CHARACTER;

class SessionInteractionView
{
  public:
    virtual ~SessionInteractionView() = default;

    virtual GameplayInteractionFact PickForLogic(CHARACTER *charactersClient) noexcept = 0;
    virtual int SelectedItemIndex() const noexcept = 0;
    virtual bool PrepareNextFrame() noexcept = 0;
};

class OBJECT;
class CButton;
class CUIManager;
class CUIGateKeeper;
class CUIPopup;
class JewelHarmonyInfo;
class ItemAddOptioninfo;
namespace SEASON3B
{
class CNewUIPickedItem;
class CNewUISystem;
} // namespace SEASON3B

struct SessionInterfaceStorage final
{
    SessionInterfaceStorage(const std::wstring &serverIp, WORD serverPort)
        : initialServerIp(serverIp), szServerIpAddress(initialServerIp.c_str()),
          g_ServerPort(serverPort)
    {
    }

    MovementSkill g_MovementSkill{};
    DWORD g_dwLatestMagicTick = 0;
    int ItemHelp = 0;
    float MouseUpdateTime = 0;
    int MouseUpdateTimeMax = 6;
    bool s_bIgnoreHeldClickAfterNpcTalk = false;
    bool WhisperEnable = true;
    bool ChatWindowEnable = true;
    int InputFrame = 0;
    int EditFlag = EDIT_NONE;
    wchar_t ColorTable[8][10] = {L"White", L"Black", L"Red",  L"Yellow",
                                 L"Green", L"Cyan",  L"Blue", L"Magenta"};
    int SelectMonster = 0;
    int SelectModel = 0;
    int SelectMapping = 0;
    int SelectColor = 0;
    int SelectWall = 0;
    float SelectMappingAngle = 0.f;
    bool DebugEnable = true;
    int SelectedItem = -1;
    int SelectedNpc = -1;
    int SelectedCharacter = -1;
    int SelectedOperate = -1;
    int Attacking = -1;
    int g_iFollowCharacter = -1;
    bool g_bAutoGetItem = false;
    bool g_bRenderGameCursor = true;
    float LButtonPopTime = 0.f;
    float LButtonPressTime = 0.f;
    float RButtonPopTime = 0.f;
    float RButtonPressTime = 0.f;
    int BrushSize = 0;
    int HeroTile = 0;
    int TargetNpc = -1;
    int TargetType = 0;
    int TargetX = 0;
    int TargetY = 0;
    float TargetAngle = 0.f;
    OBJECT *TradeNpc = nullptr;
    bool DontMove = false;
    bool ServerHide = true;
    bool SkillEnable = false;
    bool MouseOnWindow = false;
    int TerrainWallType[TERRAIN_SIZE * TERRAIN_SIZE]{};
    float TerrainWallAngle[TERRAIN_SIZE * TERRAIN_SIZE]{};
    OBJECT *PickObject = nullptr;
    float PickObjectAngle[3]{};
    float PickObjectHeight = 0.f;
    bool PickObjectLockHeight = false;
    bool EnableRandomObject = false;
    float WallAngle = 0.f;
    bool LockInputStatus = false;
    bool GuildInputEnable = false;
    bool TabInputEnable = false;
    bool GoldInputEnable = false;
    bool InputEnable = true;
    bool g_bScratchTicket = false;
    int InputGold = 0;
    int InputNumber = 2;
    int InputTextWidth = 110;
    int InputIndex = 0;
    int InputResidentNumber = 6;
    int InputTextMax[12] = {
        MAX_USERNAME_SIZE, MAX_USERNAME_SIZE, MAX_USERNAME_SIZE, 30, 30, 10, 14, 20, 40};
    wchar_t InputText[12][256]{};
    wchar_t InputTextIME[12][4]{};
    char InputTextHide[12]{};
    int InputLength[12]{};
    std::uint64_t LastMacroTime = 0;
    int WhisperIDCurrent = 2;
    char WhisperID[MAX_WHISPER_ID][256]{};
    DWORD g_dwOneToOneTick = 0;
    bool g_bGMObservation = false;
    BYTE DebugText[10][256]{};
    int DebugTextLength[10]{};
    char DebugTextCount = 0;
    int ItemKey = 0;
    int ActionTarget = -1;
    float StandTime = 0;
    int HeroAngle = 0;
    bool EnableFastInput = false;
    BOOL g_bWhileMovingZone = FALSE;
    DWORD g_dwLatestZoneMoving = 0;
    int TotalPacketSize = 0;
    int OldTime = static_cast<int>(GetTickCount());
    int g_iWidthEx = 5;
    BOOL g_bUseChatListBox = TRUE;
    DWORD g_dwActiveUIID = 0;
    DWORD g_dwMouseUseUIID = 0;
    DWORD g_dwTopWindow = 0;
    DWORD g_dwCurrentPressedButtonID = 0;
    DWORD g_dwLastLetterID = 0;
    CButton *heldButton = nullptr;
    DWORD lastInventoryRefreshRequestTick = 0;
    int g_iNoticeInverse = 0;
    CUIManager *g_pUIManager = nullptr;
    SEASON3B::CNewUISystem *g_pNewUISystem = nullptr;
    SEASON3B::CNewUIPickedItem *ms_pPickedItem = nullptr;
    float Time_Effect = 0.f;
    bool ashies = false;
    int weather = std::rand() % 3;
    float MousePosition[3]{};
    float MouseTarget[3]{};
    int MouseX = 1024 / 2;
    int MouseY = 768 / 2;
    int g_iMousePopPosition_x = 0;
    int g_iMousePopPosition_y = 0;
    bool MouseLButton = false;
    bool MouseLButtonPop = false;
    bool MouseLButtonPush = false;
    bool MouseRButton = false;
    bool MouseRButtonPop = false;
    bool MouseRButtonPush = false;
    bool MouseLButtonDBClick = false;
    bool MouseMButton = false;
    bool MouseMButtonPop = false;
    bool MouseMButtonPush = false;
    DWORD MouseRButtonPress = 0;
    bool GrabEnable = false;
    wchar_t GrabFileName[MAX_PATH]{};
    int GrabScreen = 0;
    int radioButtonIterIndex = 0;
    CUIGateKeeper *g_pUIGateKeeper = nullptr;
    JewelHarmonyInfo *g_pUIJewelHarmonyinfo = nullptr;
    ItemAddOptioninfo *g_pItemAddOptioninfo = nullptr;
    CUIPopup *g_pUIPopup = nullptr;
    bool HeroInventoryEnable = false;
    bool StorageInventoryEnable = false;
    bool g_bPersonalShopWnd = false;
    bool g_bServerDivisionEnable = false;
    const wchar_t *optionFontLabels[3]{};
    int g_iLetterReadNextPos_x = -1;
    int g_iLetterReadNextPos_y = -1;
    int SelectedHero = -1;
    bool InitLogIn = false;
    bool InitLoading = false;
    bool InitCharacterScene = false;
    bool InitMainScene = false;
    float g_fMULogoAlpha = 0.f;
    short g_shCameraLevel = 0;
    std::wstring initialServerIp;
    const wchar_t *szServerIpAddress;
    WORD g_ServerPort;
    EGameScene SceneFlag = WEBZEN_SCENE;
    int g_iCustomMessageBoxButton[NUM_BUTTON_CMB][NUM_PAR_BUTTON_CMB]{};
    int g_iCustomMessageBoxButton_Cancel[NUM_PAR_BUTTON_CMB]{};
    int g_iCancelSkillTarget = 0;
    int DeleteGuildIndex = -1;
    int ErrorMessage = 0;
    float g_Luminosity = 0.f;
    int EnableEvent = 0;
    int DeleteIndex = 0;
    int AppointStatus = 0;
    wchar_t DeleteID[100]{};
    wchar_t s_szTargetID[MAX_USERNAME_SIZE + 1]{};
    int s_nTargetFireMemberIndex = 0;
    char AppointType = 0;
};

class InventoryGrid;

struct SessionInventoryStorage final
{
    SessionInventoryStorage() noexcept;
    ~SessionInventoryStorage();
    std::array<std::unique_ptr<InventoryGrid>, static_cast<std::size_t>(InventoryRole::Count)>
        grids;
    PickedInventoryItem picked;
    InventoryGrid *pendingMoveSource = nullptr;
    int pendingMoveSlot = -1;
    int luckyMixRequest = 0;
    int purchaseSourceSlot = -1;
    InventoryRole npcInventory = InventoryRole::NpcShop;
    wchar_t g_GuildNotice[3][128]{};
    GUILD_LIST_t GuildList[MAX_GUILDS]{};
    int g_nGuildMemberCount = 0;
    int GuildTotalScore = 0;
    int GuildPlayerKey = 0;
    PARTY_t Party[MAX_PARTYS]{};
    int PartyNumber = 0;
    int PartyKey = 0;
    ITEM PickItem{};
    ITEM TargetItem{};
    ITEM Inventory[MAX_INVENTORY]{};
    ITEM InventoryExt[MAX_INVENTORY_EXT]{};
    ITEM ShopInventory[MAX_SHOP_INVENTORY]{};
    ITEM g_PersonalShopInven[MAX_PERSONALSHOP_INVEN]{};
    ITEM g_PersonalShopBackup[MAX_PERSONALSHOP_INVEN]{};
    bool g_bEnablePersonalShop = false;
    int g_iPShopWndType = PSHOPWNDTYPE_NONE;
    POINT g_ptPersonalShop{0, 0};
    int g_iPersonalShopMsgType = 0;
    wchar_t g_szPersonalShopTitle[MAX_SHOPTITLE + 1]{};
    CHARACTER g_PersonalShopSeller{};
    bool g_bIsTooltipOn = false;
    int CheckSkill = -1;
    ITEM *CheckInventory = nullptr;
    bool EquipmentSuccess = false;
    bool CheckShop = false;
    int CheckX = 0;
    int CheckY = 0;
    ITEM *SrcInventory = nullptr;
    int SrcInventoryIndex = 0;
    int DstInventoryIndex = 0;
    int AllRepairGold = 0;
    int StorageGoldFlag = 0;
    int ListCount = 0;
    int GuildListPage = 0;
    int g_bEventChipDialogEnable = EVENT_NONE;
    int g_shEventChipCount = 0;
    short g_shMutoNumber[3]{-1, -1, -1};
    bool g_bServerDivisionAccept = false;
    wchar_t g_strGiftName[64]{};
    bool RepairShop = false;
    int RepairEnable = 0;
    int AskYesOrNo = 0;
    char OkYesOrNo = -1;
    WORD g_wStoragePassword = 0;
    short g_nKeyPadMapping[10]{};
    wchar_t g_lpszKeyPadInput[2][MAX_KEYPADINPUT + 1]{};
    BYTE BuyItem[4]{};
    wchar_t TextList[50][100]{};
    int TextListColor[50]{};
    int TextBold[50]{};
    SIZE Size[50]{};
    int TextNum = 0;
    int SkipNum = 0;
    int InventoryStartX = 0;
    int InventoryStartY = 0;
    int ShopInventoryStartX = 0;
    int ShopInventoryStartY = 0;
    int TradeInventoryStartX = 0;
    int TradeInventoryStartY = 0;
    int CharacterInfoStartX = 0;
    int CharacterInfoStartY = 0;
    int GuildStartX = 0;
    int GuildStartY = 0;
    int GuildListStartX = 0;
    int GuildListStartY = 0;
    bool EquipmentItem = false;
    OBJECT ObjectSelect{};
    unsigned int MarkColor[16]{};
};

struct SessionPetManagerStorage final
{
    std::uint8_t g_tabBar = 0;
    std::uint32_t g_renderItemIndexBackup = 0;
    ITEM g_renderItemInfoBackup{};
    PET_INFO gs_PetInfo{};
};

struct SessionQuestDialogStorage final
{
    bool bCheckNPC = false;
    int g_iNumLineMessageBoxCustom = 0;
    wchar_t g_lpszMessageBoxCustom[SessionQuestDialogDimensions::MessageLineCount]
                                  [SessionQuestDialogDimensions::MessageLength]{};
    int g_iCurrentDialogScript = -1;
    int g_iNumAnswer = 0;
    wchar_t g_lpszDialogAnswer[SessionQuestDialogDimensions::AnswerCount]
                              [SessionQuestDialogDimensions::AnswerLineCount]
                              [SessionQuestDialogDimensions::MessageLength]{};
};

void MoveHero();

int SearchArrowCount();

bool FindText(const wchar_t *Text, const wchar_t *Token, bool First = false);
bool FindTextABS(const wchar_t *Text, const wchar_t *Token, bool First = false);

void GetTime(DWORD time, std::wstring &timeText, bool isSecond = true);

BYTE GetDestValue(int xPos, int yPos, int xDst, int yDst);

bool GetTimeCheck(int DelayTime);

namespace GameplayInteractionDetail
{

#pragma pack(push)
#pragma pack()
inline const float AutoMouseLimitTime = (1.f * 60.f * 60.f);
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr uint64_t MacroCooldownMs = 4000;
#pragma pack(pop)

} // namespace GameplayInteractionDetail

namespace GameplayInteractionDetail
{

void AdvanceTeleportFade(OBJECT *o, float animationFactor);
bool InsideCastleSwitchArea(const CHARACTER &hero);
} // namespace GameplayInteractionDetail
