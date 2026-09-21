#pragma once
#include "support/CoreMath.h"
#include "data/CharacterData.h"
#include "domain/WorldSimulation.h"
#include "render/ModelGeometry.h"
#include "render/ModelResources.h"
#include "data/WorldData.h"
#include "session/SessionRuntime.h"
#include <cstdint>
#include <map>
#include <string>
#include <memory>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <span>
#include <unordered_set>
#include <algorithm>
#include <array>
#include <type_traits>
#include <cmath>
#include <limits>
#include <cstring>
#include <cwchar>
#include <cwctype>

struct WorldCharacterVisualState;
#define DEFAULT_MAX_POSTMOVEPCOCESS 15
#define SEARCH_LENGTH 300.0f
#define CIRCLE_STAND_RADIAN 50.0f
#define CIRCLE_GETITEM_RADIAN 50.0f
#define PC4_ELF 1
#define PC4_TEST 2
#define PC4_SATAN 3
#define XMAS_RUDOLPH 4
#define UNICORN 6
#define SKELETON 7
#define MAX_MOUNTS 10
#define CHAR_DEL_LIMIT_LV 300
#define ATTACK_FAIL 0
#define ATTACK_SUCCESS 1
#define ATTACK_DIE 2
#define ABILITY_FAST_ATTACK_SPEED 0x01
#define ABILITY_PLUS_DAMAGE 0x02
#define ABILITY_FAST_ATTACK_RING 0x04
#define ABILITY_FAST_ATTACK_SPEED2 0x08
#define CTLCODE_01BLOCKCHAR 0x01
#define CTLCODE_02BLOCKITEM 0x02
#define CTLCODE_04FORTV 0x04
#define CTLCODE_08OPERATOR 0x08
#define CTLCODE_10ACCOUNT_BLOCKITEM 0x10
#define CTLCODE_20OPERATOR 0x20
#define PVP_HERO2 1
#define PVP_HERO1 2
#define PVP_NEUTRAL 3
#define PVP_CAUTION 4
#define PVP_MURDERER1 5
#define PVP_MURDERER2 6

typedef struct _PATH_t
{
    unsigned char CurrentPath;
    unsigned char CurrentPathFloat;
    unsigned char PathNum;
    unsigned char PathX[MAX_PATH_FIND];
    unsigned char PathY[MAX_PATH_FIND];

    bool Success;
    bool Error;
    unsigned char x, y;
    unsigned char Direction;
    unsigned char Run;
    int Count;
    SpinLock Lock;

    _PATH_t()
    {
        Reset();
    }

    void Reset()
    {
        CurrentPath = 0;
        CurrentPathFloat = 0;
        PathNum = 0;

        for (int i = 0; i < MAX_PATH_FIND; ++i)
        {
            PathX[i] = 0;
            PathY[i] = 0;
        }

        Success = 0;
        Error = 0;
        x = 0, y = 0;
        Direction = 0;
        Run = 0;
        Count = 0;
    }
} PATH_t;

struct ST_POSTMOVE_PROCESS
{
    bool bProcessingPostMoveEvent;
    unsigned int uiProcessingCount_PostMoveEvent;

    ST_POSTMOVE_PROCESS()
    {
        bProcessingPostMoveEvent = false;
        uiProcessingCount_PostMoveEvent = 0;
    }

    ~ST_POSTMOVE_PROCESS()
    {
    }
};

class CHARACTER
{
  public:
    std::uint64_t WorldVisualTick = 0;
    float WorldVisualAnimationFactor = 1.f;
    float WorldVisualHalloweenProgress = 0.f;
    bool WorldVisualHalloweenBurst = false;
    bool WorldVisualAppearing = false;
    unsigned int WorldVisualHalloweenSparks = 0;
    float WorldVisualPriorLifeTime = 100.f;
    std::uint64_t WorldVisualGeneration = 0;
    // Published pose identity is independent of appearance packets and tick consumption.
    std::uint64_t WorldVisualPoseRevision = 0;
    AnimationPoseSample WorldVisualPoseSample;
    CharacterTargetBinding TargetBinding;
    std::uint64_t WorldVisualAppearanceRevision = 0;
    // Authored socket facts belong to the character lifetime, never to a session BMD.
    std::map<std::wstring, int, std::less<>> NamedBones;
    std::shared_ptr<CharacterSocketSource> SocketSource;
    int WorldVisualAction = 0;
    float WorldVisualAnimationFrame = 0.f;
    float WorldVisualPriorAnimationFrame = 0.f;
    int WorldVisualPriorAction = 0;
    int WorldVisualAttackTime = 0;
    int WorldVisualPriorAI = 0;
    double WorldVisualAttackFrameTime = -1.0;
    float WorldVisualAttackStart = 0.f;
    float WorldVisualAttackFrames = 0.f;
    int WorldVisualAttackAction = 0;

    CHARACTER();
    virtual ~CHARACTER();

  public:
    void Initialize();
    void Destroy();
    void ResetIdentity();
    void ResetPresentationIdentity();
    void MarkAppearanceChanged() noexcept;
    void InitPetInfo(int iPetType);
    PET_INFO *GetEquipedPetInfo(int iPetType);
    void PostMoveProcess_Active(unsigned int uiLimitCount);
    unsigned int PostMoveProcess_GetCurProcessCount();
    bool PostMoveProcess_IsProcessing() const;
    bool PostMoveProcess_Process();
    bool Blood;
    bool Ride;
    bool SkillSuccess;
    bool NotRotateOnMagicHit;
    bool SafeZone;
    bool Change; // True for transformed players
    bool HideShadow;
    bool m_bIsSelected;
    bool Decoy;
    CLASS_TYPE Class;
    CLASS_SKIN_INDEX SkinIndex;
    BYTE Skin; // What is this good for?
    BYTE CtlCode;
    BYTE ExtendState;
    BYTE EtcPart;
    BYTE GuildStatus;
    BYTE GuildType;
    BYTE GuildRelationShip;
    BYTE GuildSkill;
    BYTE GuildMasterKillCount;
    BYTE BackupCurrentSkill;
    BYTE GuildTeam;
    BYTE m_byGensInfluence;
    BYTE PK;
#ifdef LJH_ADD_MORE_ZEN_FOR_ONE_HAVING_A_PARTY_WITH_MURDERER
    char PKPartyLevel;
#endif //LJH_ADD_MORE_ZEN_FOR_ONE_HAVING_A_PARTY_WITH_MURDERER
    BYTE AttackFlag;

    BYTE TargetAngle;
    float Dead; // Number of reference frames after death
    WORD Skill;
    BYTE SwordCount;
    BYTE byExtensionSkill;
    WORD m_byDieType;
    BYTE TargetX;
    BYTE TargetY;
    BYTE SkillX;
    BYTE SkillY;
    float Appear;
    BYTE CurrentSkill;   // Skill Index
    BYTE CastRenderTime; // unused?
    BYTE m_byFriend;
    WORD MonsterSkill;

    wchar_t ID[MAX_MONSTER_NAME + 1]{};
    char Movement;
    char MovementType;
    char CollisionTime; // unused?

    short GuildMarkIndex;
    SHORT Key;
    short TargetCharacter;

    WORD Level;
    EMonsterType MonsterIndex;
    int Damage;
    int Hit;
    WORD MoveSpeed;
    WORD AttackSpeed;
    WORD MagicSpeed;

    int Action;
    int LongRangeAttack;
    int SelectItem;
    int Item;
    int FreezeType;
    int PriorPositionX;
    int PriorPositionY;
    int PositionX;
    int PositionY;
    int m_iFenrirSkillTarget;
    int LastAttackEffectTime;

    float m_iDeleteTime;
    float LastCritDamageEffect;
    float ExtendStateTime;
    double JumpTime;
    float StormTime;
    float AttackTime;

    float ProtectGuildMarkWorldTime;
    float AttackRange;
    float Freeze;
    float Duplication;
    float Rot;
    float Run;
    double PathAnimationWorldTime = -1.0;
    float PathAnimationFrames = 0.f;
    float HealthStatus;
    float ShieldStatus;

    vec3_t TargetPosition;
    vec3_t Light;

    PART_t BodyPart[MAX_BODYPART];
    PART_t Weapon[2];
    PART_t Wing;
    PART_t Helper;
    PART_t Flag;
    PATH_t Path;

    CharacterPetCommands PetCommands;
    CharacterDarksideState Darkside;
    CharacterMountState MountState;
    bool WorldVisualMountBurst = false;
    bool WorldVisualSnowmanDeath = false;
    bool WorldVisualStructureDeath = false;
    CharacterHelperPetState HelperPetState;
    PET_INFO m_PetInfo[PET_TYPE_END];

    wchar_t OwnerID[32];

  private:
    ST_POSTMOVE_PROCESS *m_pPostMoveProcess;

  public:
    int m_iTempKey;
    WORD m_CursedTempleCurSkill;
    bool m_CursedTempleCurSkillPacket;
    OBJECT Object;
    BYTE GensRanking;
    BYTE GensContributionPoints;
    bool CheckAttackTime(int timeNumber) const
    {
        return static_cast<int>(AttackTime) == timeNumber && LastAttackEffectTime != timeNumber;
    }
    void SetLastAttackEffectTime()
    {
        LastAttackEffectTime = static_cast<int>(AttackTime);
    }
};

class CCharacterManager
{
  public:
    constexpr CCharacterManager() noexcept = default;
    CLASS_TYPE ChangeServerClassTypeToClientClassType(
        const SERVER_CLASS_TYPE byServerClassType) const;
    bool IsSecondClass(const CLASS_TYPE byClass) const;
    bool IsThirdClass(const CLASS_TYPE byClass) const;
    bool IsMasterLevel(const CLASS_TYPE byClass) const;
    bool IsMasterExperienceActive(const CLASS_TYPE byClass, const int level) const;
    CLASS_TYPE GetBaseClass(CLASS_TYPE iClass) const;
    const wchar_t *GetCharacterClassText(const CLASS_TYPE byClass) const;

    int IsFemale(CLASS_TYPE iClass) const
    {
        return (this->GetBaseClass(iClass) == CLASS_ELF ||
                this->GetBaseClass(iClass) == CLASS_SUMMONER);
    }
    CLASS_SKIN_INDEX GetSkinModelIndex(const CLASS_TYPE byClass) const;
    BYTE GetStepClass(const CLASS_TYPE byClass) const;
    int GetEquipedBowType(const CHARACTER *pChar) const;
    int GetEquipedBowType(ITEM *pItem) const;

  public:
};

inline constexpr CCharacterManager gCharacterManager;

namespace Engine::Object
{
// True while a player's action is one of the attack/skill swing animations
// (PLAYER_ATTACK_FIST .. PLAYER_RIDE_SKILL). The swing animation's playback
// speed scales with AttackSpeed (see SetAttackSpeed in ZzzCharacter.cpp), so
// this predicate doubles as the natural attack-cadence gate: hold off the
// next action until the current swing finishes, and the rate follows attack
// speed instead of any fixed timer.
inline bool IsAttackAction(int currentAction)
{
    return currentAction >= PLAYER_ATTACK_FIST && currentAction <= PLAYER_RIDE_SKILL;
}

// True while a player is sitting or holding a pose (PLAYER_SIT1 ..
// PLAYER_POSE_FEMALE1). Used to keep these animations from being reset to the
// stand pose when equipment or class changes.
inline bool IsSitOrPoseAction(int currentAction)
{
    return currentAction >= PLAYER_SIT1 && currentAction <= PLAYER_POSE_FEMALE1;
}
} // namespace Engine::Object

class CHARACTER;

struct SharedCharacterKey final
{
    std::uint64_t worldInstance = 0;
    std::uint16_t serverId = 0;

    bool operator==(const SharedCharacterKey &) const noexcept = default;
};

class SharedCharacterPool final
{
  public:
    class AccessLease final
    {
      public:
        AccessLease() = default;
        AccessLease(AccessLease &&) noexcept = default;
        AccessLease &operator=(AccessLease &&) noexcept = default;

        AccessLease(const AccessLease &) = delete;
        AccessLease &operator=(const AccessLease &) = delete;

        void Release() noexcept
        {
            if (lock_.owns_lock())
                lock_.unlock();
        }

      private:
        friend class SharedCharacterPool;
        explicit AccessLease(std::shared_ptr<std::mutex> mutex);

        std::shared_ptr<std::mutex> mutex_;
        std::unique_lock<std::mutex> lock_;
    };

    struct Observation final
    {
        CHARACTER *character = nullptr;
        bool source = false;
    };

    SharedCharacterPool() = default;
    ~SharedCharacterPool();

    SharedCharacterPool(const SharedCharacterPool &) = delete;
    SharedCharacterPool &operator=(const SharedCharacterPool &) = delete;

    Observation Observe(SharedCharacterKey key, SessionId observer) noexcept;
    std::unique_ptr<CHARACTER> Unobserve(SharedCharacterKey key, SessionId observer) noexcept;
    CHARACTER *Find(SharedCharacterKey key) noexcept;
    bool IsSource(SharedCharacterKey key, SessionId observer) const noexcept;
    std::size_t ObserverCount(SharedCharacterKey key) const noexcept;
    std::size_t Size() const noexcept;
    AccessLease AcquireAccess(SharedCharacterKey key);
    std::optional<std::uint32_t> ResolveWorldRoute(std::wstring_view host, std::int32_t port,
                                                   std::uint32_t serverCode) noexcept;

  private:
    struct KeyHash final
    {
        std::size_t operator()(SharedCharacterKey key) const noexcept;
    };

    struct Entry final
    {
        std::unique_ptr<CHARACTER> character;
        std::vector<SessionId> observers;
        std::shared_ptr<std::mutex> accessMutex = std::make_shared<std::mutex>();
    };

    std::unordered_map<SharedCharacterKey, Entry, KeyHash> entries_;
    // Route IDs outlive observations, so reconnects bind the same world identity.
    std::map<std::tuple<std::wstring, std::int32_t, std::uint32_t>, std::uint32_t> worldRoutes_;
    mutable std::mutex entriesMutex_;
};

class CHARACTER_MACHINE;
class CBoneManager;

struct SessionCharacterPopulationStorage final
{
    struct Acquisition final
    {
        CHARACTER *character = nullptr;
        bool source = false;
    };

    explicit SessionCharacterPopulationStorage(CBoneManager &bones) noexcept;
    ~SessionCharacterPopulationStorage();

    SessionCharacterPopulationStorage(const SessionCharacterPopulationStorage &) = delete;
    SessionCharacterPopulationStorage &operator=(const SessionCharacterPopulationStorage &) =
        delete;

    bool Initialize(SharedCharacterPool &sharedCharacters, SessionId sessionId,
                    CHARACTER_MACHINE &characterMachine,
                    CHARACTER_ATTRIBUTE &characterAttribute) noexcept;

    CHARACTER &operator[](std::ptrdiff_t index) noexcept;
    const CHARACTER &operator[](std::ptrdiff_t index) const noexcept;
    int Size() const noexcept;
    bool IsValidIndex(int index) const noexcept;
    std::span<CHARACTER *const> Pointers() const noexcept;

    CHARACTER *AcquireLocalByKey(int key) noexcept;
    CHARACTER *AcquireLocalAt(int index, int key) noexcept;
    int BindControlled(int key) noexcept;
    Acquisition ObserveRemote(int key) noexcept;
    std::unique_ptr<CHARACTER> RemoveByKey(int key) noexcept;

    int FindIndexByKey(int key) const noexcept;
    int FindIndexByMonsterType(int type) const noexcept;
    bool IsShared(int index) const noexcept;
    bool IsControlled(int index) const noexcept;
    bool IsSource(int index) const noexcept;
    bool IsSourceByKey(int key) const noexcept;
    WorldCharacterVisualState &WorldVisuals(int index) noexcept;
    void SetVisible(int index, bool visible) noexcept;
    bool IsVisible(int index) const noexcept;
    SharedCharacterPool::AccessLease AcquireSharedAccess(int index);
    std::size_t ObserverCount(int index) const noexcept;
    void SetWorldInstance(std::uint64_t worldInstance) noexcept;
    void ClearWorldInstance() noexcept;
    bool HasWorldInstance() const noexcept;

    CHARACTER CharacterView{};
    CHARACTER *Hero = nullptr;
    CHARACTER_MACHINE *CharacterMachine = nullptr;
    CHARACTER_ATTRIBUTE *CharacterAttribute = nullptr;
    float g_fBoneSave[10][3][4]{};
    int EquipmentLevelSet = 0;
    bool g_bAddDefense = false;
    int g_iLimitAttackTime = 15;
    int g_iOldPositionX = 0;
    int g_iOldPositionY = 0;
    float g_fStopTime = 0.f;
    int playerNpcDeviasTextIndex = 904;
    int playerNpcLorenciaTextIndex = 823;
    std::vector<CHARACTER *> activeChars;
    std::vector<int> mapHitTargets;
    std::unique_ptr<OBJECT> g_ItemObject[ITEM_ETC + MAX_ITEM_INDEX]{};
    float swordDancerPosition = 0.f;
    int swordDancerRandom = 0;
    float EarthQuake = 0.f;
    int visibleObject = 0;
    OBJECT g_CloudsLow{};
    float objectTextureAnimation = 0.f;
    float objectMeshLight = 0.5f;
    float objectMeshLightDelta = 0.01f;
    int objectPriorAnimationFrame = 0;
    float RainTarget = 0.f;
    float RainCurrent = 0.f;
    int RainSpeed = 30;
    int RainAngle = 0;
    float RainPosition = 0.f;
    int MonsterKey = 0;

  private:
    CBoneManager &boneManager_;
    enum class SlotKind : std::uint8_t
    {
        Empty,
        Controlled,
        Local,
        Shared,
    };

    int AllocateSlot() noexcept;
    bool EnsureSlot(int index) noexcept;
    void MapKey(int index, int key) noexcept;
    void ClearSlot(int index) noexcept;
    SharedCharacterKey SharedKey(int key) const noexcept;

    SharedCharacterPool *sharedCharacters_ = nullptr;
    std::optional<SessionId> sessionId_;
    std::optional<std::uint64_t> worldInstance_;
    std::unique_ptr<CHARACTER> controlledCharacter_;
    CHARACTER invalidCharacter_{};
    std::vector<CHARACTER *> slots_;
    std::vector<SlotKind> slotKinds_;
    std::vector<std::unique_ptr<CHARACTER>> localCharacters_;
    std::vector<std::optional<SharedCharacterKey>> sharedKeys_;
    std::vector<std::uint8_t> visibility_;
    std::vector<WorldCharacterVisualState> worldVisuals_;
    std::vector<int> freeSlots_;
    std::unordered_map<int, int> indicesByKey_;
};

// Shared server/equipment facts. Session-bound pet AI, poses and effects live in the observer.

class SessionRandom;

class CSPetSystem : protected SessionUiLegacyBindings
{
  protected:
    SessionRandom &Random;
    CHARACTER *m_PetOwner;
    CHARACTER *m_PetTarget;
    std::shared_ptr<CharacterSocketSource> m_targetSource;
    void ClearTarget();
    CHARACTER m_PetCharacter;
    AnimationPoseSample poseSample_;
    PET_TYPE m_PetType;
    std::uint16_t petLevel_ = 0;
    bool effectsRetired_ = false;
    std::uint8_t m_byCommand;
    std::unique_ptr<vec34_t[]> m_BoneTransforms;

  public:
    explicit CSPetSystem(SessionKeeper &keeper);
    virtual ~CSPetSystem();

    PET_TYPE GetPetType(void)
    {
        return m_PetType;
    }
    void SetPetInfo(PET_INFO *info);
    void SetPetLevel(std::uint16_t level)
    {
        petLevel_ = level;
    }
    OBJECT *GetObject()
    {
        return &m_PetCharacter.Object;
    }
    void EffectsRetired() noexcept
    {
        effectsRetired_ = true;
    }

    virtual void MovePet(bool forceRender = false) = 0;
    virtual void AdvancePresentation(bool emit = true, bool forceRender = false) = 0;
    virtual void CalcPetInformation(const PET_INFO &Petinfo) = 0;
    virtual void RenderPetInventory(void) = 0;
    virtual void RenderPet(int PetState = 0) const = 0;

    virtual void Eff_LevelUp(void) = 0;
    virtual void Eff_LevelDown(void) = 0;

    void CreatePetPointer(int Type, unsigned char PositionX, unsigned char PositionY,
                          float Rotation);
    bool PlayAnimation(OBJECT *o, float frames);

    void MoveInventory(void);
    void RenderInventory(void);

    void ForgetTargets(const std::unordered_set<const OBJECT *> &targets);
    void SetAI(int AI);
    void SetCommand(int Key, std::uint8_t cmd);
    void SetAttack(int Key, int attackType);

    int GetObjectType()
    {
        return m_PetCharacter.Object.Type;
    }
};

class CSPetDarkSpirit : public CSPetSystem
{
  private:
    float flightNoiseFrames_ = 0.f;
    float flightTurnRate_ = 0.f;
    void AdvanceMotion(float frames);
    void AdvanceMotionStep(float frames, const vec3_t standTarget, float frameFraction);

  public:
    CSPetDarkSpirit(SessionKeeper &keeper, CHARACTER *c);
    virtual ~CSPetDarkSpirit(void);

    virtual void MovePet(bool forceRender = false);
    virtual void CalcPetInformation(const PET_INFO &Petinfo);
    virtual void RenderPetInventory(void);
    virtual void RenderPet(int PetState = 0) const;

    virtual void Eff_LevelUp(void);
    virtual void Eff_LevelDown(void);

    void AdvancePresentation(bool emit = true, bool forceRender = false) override;
    void AttackEffect(CHARACTER *c, OBJECT *o);
    void RenderCmdType(void);
};

#ifndef __CIPET_MANAGER_H__
#define __CIPET_MANAGER_H__

#endif

struct RootingItem
{
    int itemIndex = -1;
    unsigned long long generation = 0;
    vec3_t position{};

    bool FindZen(const OBJECT &owner, ITEM_t (&items)[MAX_ITEMS], float radius);

    bool CanPickup(const ITEM_t (&items)[MAX_ITEMS]) const
    {
        // Checked only at the actual pickup request; admission supplies the index.
        const auto &item = items[itemIndex];
        return item.Object.Live && item.Generation == generation;
    }
};

class SessionKeeper;

class PetAction : protected SessionLegacyCalls
{
  public:
    explicit PetAction(SessionKeeper &keeper) : SessionLegacyCalls(keeper)
    {
    }
    virtual ~PetAction()
    {
    }

  protected:
    static bool CompTimeControl(double interval, double &previous, double now)
    {
        if (now - previous <= interval)
            return false;
        previous = now;
        return true;
    }

  public:
    virtual bool Model(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick, bool bForceRender)
    {
        return false;
    }
    virtual bool Move(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick, bool bForceRender,
                      float animationFactor)
    {
        return false;
    }
    virtual bool Effect(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                        bool bForceRender, double worldTime)
    {
        return false;
    }
    virtual bool Sound(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick, bool bForceRender)
    {
        return false;
    }
};

class SessionKeeper;

SmartPointer(PetObject);
class PetObject : protected SessionLegacyCalls
{
  public:
    enum ActionType
    {
        eAction_Stand = 0,
        eAction_Move,
        eAction_Attack,
        eAction_Skill,
        eAction_Birth,
        eAction_Dead,
        eAction_End
    };

  public:
    static PetObjectPtr Make(SessionKeeper &keeper);
    virtual ~PetObject();

  public:
    OBJECT *GetObject()
    {
        return m_obj;
    }
    bool IsSameOwner(OBJECT *Owner);
    bool IsSameObject(OBJECT *Owner, int itemType);
    void SetActions(ActionType type, Smart_Ptr(PetAction) action, float speed);
    void SetCommand(int targetKey, ActionType cmdType);

    void SetScale(float scale = 0.0f);
    void SetBlendMesh(int blendMesh = -1);

  public:
    bool Create(int itemType, int modelType, vec3_t Position, CHARACTER *Owner, int SubType,
                int LinkBone);
    void Release();
    void EffectsRetired() noexcept
    {
        effectsRetired_ = true;
    }
    void PreparePresentation(bool forceRender = false);
    void Update(bool bForceRender = false);
    void Render(bool bForceRender = false) const;

  private:
    bool UpdateMove(double tick, float animationFactor, bool bForceRender = false);
    bool UpdateModel(double tick, bool bForceRender = false);
    bool UpdateSound(double tick, bool bForceRender = false);

    bool CreateEffect(double tick, double worldTime, bool bForceRender = false);

  private:
    void Init();
    explicit PetObject(SessionKeeper &keeper);

  public:
    typedef std::map<ActionType, Smart_Ptr(PetAction)> ActionMap;
    typedef std::map<ActionType, float> SpeedMap;

  private:
    const float &FPS_ANIMATION_FACTOR;
    const double &WorldTime;
    ActionMap m_actionMap;
    SpeedMap m_speedMap;

    CHARACTER *m_pOwner;
    OBJECT *m_obj = nullptr;
    std::unique_ptr<vec34_t[]> m_pose;
    AnimationPoseSample poseSample_;
    int m_targetKey;
    int m_itemType;

    double elapsedMilliseconds_ = 0.0;
    bool effectsRetired_ = false;

    ActionType m_moveType;
    ActionType m_oldMoveType;
};

SmartPointer(PetActionCollecter);
class PetActionCollecter : public PetAction
{
  public:
    static PetActionCollecterPtr Make(SessionKeeper &keeper);
    virtual ~PetActionCollecter();

  public:
    virtual bool Model(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                       bool bForceRender);
    virtual bool Move(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick, bool bForceRender,
                      float animationFactor);
    virtual bool Effect(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                        bool bForceRender, double worldTime);
    virtual bool Sound(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                       bool bForceRender);

    //test
    void FindZen(OBJECT *obj);

  public:
    typedef std::list<RootingItem> ItemList;
    enum ActionState
    {
        eAction_Stand = 0,
        eAction_Move = 1,
        eAction_Get = 2,
        eAction_Return = 3,

        eAction_End_NotUse,
    };

  private:
    bool PrepareMove(OBJECT *obj, CHARACTER *owner, int targetKey, double tick, bool forceRender,
                     float animationFactor, const vec3_t ownerPosition, const vec3_t ownerAngle);
    explicit PetActionCollecter(SessionKeeper &keeper);
    ITEM_t (&Items)[MAX_ITEMS];
    RootingItem m_RootItem;
    bool m_isRooting;
    double m_dwSendDelayTime;
    double m_dwRootingTime;
    DWORD m_dwRoundCountDelay;
    ActionState m_state;
    double m_fRadWidthStand;
    double m_fRadWidthGet;
};

#ifdef PJH_ADD_PANDA_PET

SmartPointer(PetActionCollecterAdd);
class PetActionCollecterAdd : public PetAction
{
  public:
    static PetActionCollecterAddPtr Make(SessionKeeper &keeper);
    virtual ~PetActionCollecterAdd();

  public:
    virtual bool Model(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                       bool bForceRender);
    virtual bool Move(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick, bool bForceRender,
                      float animationFactor);
    virtual bool Effect(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                        bool bForceRender, double worldTime);
    virtual bool Sound(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                       bool bForceRender);

    //test
    void FindZen(OBJECT *obj);

  public:
    typedef std::list<RootingItem> ItemList;
    enum ActionState
    {
        eAction_Stand = 0,
        eAction_Move = 1,
        eAction_Get = 2,
        eAction_Return = 3,

        eAction_End_NotUse,
    };

  protected:
    bool PrepareMove(OBJECT *obj, CHARACTER *owner, int targetKey, double tick, bool forceRender,
                     float animationFactor, const vec3_t ownerPosition, const vec3_t ownerAngle);
    explicit PetActionCollecterAdd(SessionKeeper &keeper);
    ITEM_t (&Items)[MAX_ITEMS];

    //test

    //ItemList m_ItemList;
    RootingItem m_RootItem;
    bool m_isRooting;

    double m_dwSendDelayTime;
    double m_dwRootingTime;
    DWORD m_dwRoundCountDelay;
    ActionState m_state;

    double m_fRadWidthStand;
    double m_fRadWidthGet;

    //test
};
#endif //PJH_ADD_PANDA_PET

SmartPointer(PetActionCollecterSkeleton);
class PetActionCollecterSkeleton : public PetActionCollecterAdd
{
  public:
    static PetActionCollecterSkeletonPtr Make(SessionKeeper &keeper);
    virtual ~PetActionCollecterSkeleton();

  public:
    virtual bool Move(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick, bool bForceRender,
                      float animationFactor);
    virtual bool Effect(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                        bool bForceRender, double worldTime);

  protected:
    bool PrepareMove(OBJECT *obj, CHARACTER *owner, int targetKey, double tick, bool forceRender,
                     float animationFactor, const vec3_t ownerPosition, const vec3_t ownerAngle);
    explicit PetActionCollecterSkeleton(SessionKeeper &keeper);

    BOOL m_bIsMoving;
};

SmartPointer(PetActionDemon);
class PetActionDemon : public PetAction
{
  public:
    static PetActionDemonPtr Make(SessionKeeper &keeper);
    virtual ~PetActionDemon();

  public:
    virtual bool Model(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                       bool bForceRender);
    virtual bool Move(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick, bool bForceRender,
                      float animationFactor);
    virtual bool Effect(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                        bool bForceRender, double worldTime);

  private:
    explicit PetActionDemon(SessionKeeper &keeper);
};

SmartPointer(PetActionRound);

class PetActionRound : public PetAction
{
  public:
    static PetActionRoundPtr Make(SessionKeeper &keeper);
    virtual ~PetActionRound();

  public:
    virtual bool Model(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                       bool bForceRender);
    virtual bool Move(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick, bool bForceRender,
                      float animationFactor);
    virtual bool Effect(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                        bool bForceRender, double worldTime);

  private:
    explicit PetActionRound(SessionKeeper &keeper);
};

SmartPointer(PetActionStand);

class PetActionStand : public PetAction
{
  public:
    static PetActionStandPtr Make(SessionKeeper &keeper);
    virtual ~PetActionStand();

  public:
    virtual bool Model(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                       bool bForceRender);
    virtual bool Move(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick, bool bForceRender,
                      float animationFactor);
    virtual bool Effect(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                        bool bForceRender, double worldTime);

  private:
    explicit PetActionStand(SessionKeeper &keeper);
};

SmartPointer(PetActionUnicorn);
class PetActionUnicorn : public PetAction
{
  public:
    static PetActionUnicornPtr Make(SessionKeeper &keeper);
    virtual ~PetActionUnicorn();

  public:
    virtual bool Model(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                       bool bForceRender);
    virtual bool Move(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick, bool bForceRender,
                      float animationFactor);
    virtual bool Effect(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                        bool bForceRender, double worldTime);
    virtual bool Sound(OBJECT *obj, CHARACTER *Owner, int targetKey, double tick,
                       bool bForceRender);

    //test
    void FindZen(OBJECT *obj);

  public:
    typedef std::list<RootingItem> ItemList;
    enum ActionState
    {
        eAction_Stand = 0,
        eAction_Move = 1,
        eAction_Get = 2,
        eAction_Return = 3,

        eAction_End_NotUse,
    };

  private:
    bool PrepareMove(OBJECT *obj, CHARACTER *owner, int targetKey, double tick, bool forceRender,
                     float animationFactor, const vec3_t ownerPosition, const vec3_t ownerAngle);
    explicit PetActionUnicorn(SessionKeeper &keeper);
    ITEM_t (&Items)[MAX_ITEMS];

    //ItemList m_ItemList;
    RootingItem m_RootItem;
    bool m_isRooting;

    double m_dwSendDelayTime;
    double m_dwRootingTime;
    DWORD m_dwRoundCountDelay;
    ActionState m_state;

    float m_fRadWidthStand;
    float m_fRadWidthGet;

    float m_speed;

    //test
};

class CErrorReport;
class SessionKeeper;
class SessionKeeperTestPeer;

#ifdef PJH_ADD_PANDA_PET
#define PANDA 5
#endif //PJH_ADD_PANDA_PET

SmartPointer(PetInfo);
class PetInfo
{
  public:
    static PetInfoPtr Make();
    virtual ~PetInfo();

  private:
    PetInfo();
    void Destroy();

  public:
    float GetScale()
    {
        return m_scale;
    }
    void SetScale(float scale = 0.0f)
    {
        m_scale = scale;
    }

    int GetBlendMesh()
    {
        return m_blendMesh;
    }
    void SetBlendMesh(int blendMesh = -1)
    {
        m_blendMesh = blendMesh;
    }

    float *GetSpeeds()
    {
        return m_speeds;
    }
    int *GetActions()
    {
        return m_actions;
    }
    void SetActions(int count, int *actions, float *speeds);

    int GetActionsCount()
    {
        return m_count;
    }

  private:
    //create
    int m_blendMesh;
    float m_scale;

    //action
    int *m_actions;
    float *m_speeds;

    //ex
    int m_count;
};

SmartPointer(PetProcess);

class PetProcess
{
  public:
    typedef std::map<int, Smart_Ptr(PetAction)> ActionMap; //actionNum, actionClass
    typedef std::map<int, Smart_Ptr(PetInfo)> InfoMap;     //PetType, PetInfo

  public:
    static PetProcessPtr Make(SessionKeeper &keeper);
    virtual ~PetProcess();

  private:
    Smart_Ptr(PetAction) CreateAction(int key);

    void Init();
    void Destroy();
    explicit PetProcess(SessionKeeper &keeper);

  public:
    bool LoadData();
    bool IsPet(int itemType);
    bool CreatePet(int itemType, int modelType, vec3_t Position, CHARACTER *Owner, int SubType = 0,
                   int LinkBone = 0);
    void DeletePet(CHARACTER *Owner, int itemType = -1, bool allDelete = false);
    PetObjectPtr CreatePetObject(CHARACTER &owner);
    void SetCommandPet(CHARACTER *Owner, int targetKey, PetObject::ActionType cmdType);

  private:
    friend class SessionKeeperTestPeer;
    SessionKeeper &sessionKeeper_;
    CErrorReport &g_ErrorReport;
    HWND &g_hWnd;
    InfoMap m_petsInfo;
};

enum MonsterSkillType
{
    ATMON_SKILL_BIGIN = 0,
    ATMON_SKILL_THUNDER = 1,
    ATMON_SKILL_WIND = 2,
    ATMON_SKILL_FIRELAY = 3,
    ATMON_SKILL_CHARM = 4,
    ATMON_SKILL_TOUCH = 5,
    ATMON_SKILL_NUM1 = 6,
    ATMON_SKILL_NUM2 = 7,
    ATMON_SKILL_NUM3 = 8,
    ATMON_SKILL_NUM4 = 9,
    ATMON_SKILL_NUM5 = 10,
    ATMON_SKILL_NUM6 = 11,
    ATMON_SKILL_NUM7 = 12,
    ATMON_SKILL_NUM8 = 13,
    ATMON_SKILL_NUM9 = 14,
    ATMON_SKILL_NUM10 = 15,
    ATMON_SKILL_NUM11 = 16,
    ATMON_SKILL_NUM12 = 17,
    ATMON_SKILL_NORMAL = 18,
    ATMON_SKILL_STORM = 19,
    ATMON_SKILL_SUMMON = 20,
    ATMON_SKILL_HELL = 21,
    ATMON_SKILL_TELEPORT = 22,
    ATMON_SKILL_SHOCK = 23,
    ATMON_SKILL_CRYSTAL = 24,
    ATMON_SKILL_MAYASTORM = 25,
    ATMON_SKILL_MAYACRYSTAL = 26,
    ATMON_SKILL_COURSED_STUN = 27,
    ATMON_SKILL_COURSED_POISON = 28,
    ATMON_SKILL_SERUFAN_POISONGEM = 34,
    ATMON_SKILL_SERUFAN_COOLSTORM = 35,
    ATMON_SKILL_SERUFAN_COOLSHOCK = 36,
    ATMON_SKILL_SERUFAN_FALLING = 37,
    ATMON_SKILL_SERUFAN_SUMMON = 38,
    ATMON_SKILL_SERUFAN_HEAL = 39,
    ATMON_SKILL_SERUFAN_FREEZE = 40,
    ATMON_SKILL_SERUFAN_TELEPORT = 41,
    ATMON_SKILL_SERUFAN_SUPERMAN = 42,
    ATMON_SKILL_EMPIREGUARDIAN_BERSERKER = 59,
    ATMON_SKILL_EMPIREGUARDIAN_GAION_02_BLOODATTACK = 64,
    ATMON_SKILL_EMPIREGUARDIAN_GAION_03_GIGANTIKSTORM = 65,
    ATMON_SKILL_EMPIREGUARDIAN_GAION_04_FLAMEATTACK = 66,
    ATMON_SKILL_EMPIREGUARDIAN_GAION_01_GENERALATTACK = 67,
    ATMON_SKILL_EX_BLOODYGOLUEM_ATTACKSKILL = 68,
    ATMON_SKILL_EX_BLOODYWITCHQUEEN_ATTACKSKILL = 69,
    ATMON_SKILL_EX_BERSERKERWARRIOR_ATTACKSKILL = 70,
    ATMON_SKILL_EX_KENTAURUSWARRIOR_ATTACKSKILL = 14,
    ATMON_SKILL_EX_GENOSIDEWARRIOR_ATTACKSKILL = 71,
    ATMON_SKILL_EX_SAPIQUEEN_ATTACKSKILL = 72,
    ATMON_SKILL_EX_ICENAPIN_ATTACKSKILL = 73,
    ATMON_SKILL_EX_SHADOWMASTER_ATTACKSKILL = 74,
    ATMON_SKILL_EX_DARKMEMUD_ATTACKSKILL = 75,
    ATMON_SKILL_EX_DARKGIANT_ATTACKSKILL = 76,
    ATMON_SKILL_EX_DARKAIONNIGHT_ATTACKSKILL = 32,
    ATMON_SKILL_EX_DARKCOOLERTIN_ATTACKSKILL = 77,
    ATMON_SKILL_END,
};

enum OptionType
{
    AT_ATTACK1 = 120,
    AT_ATTACK2,

    AT_STAND1,
    AT_STAND2,
    AT_MOVE1,
    AT_MOVE2,

    AT_DAMAGE1,
    AT_DIE1,
    AT_SIT1,
    AT_POSE1,
    AT_HEALING1,
    AT_GREETING1,
    AT_GOODBYE1,
    AT_CLAP1,
    AT_GESTURE1,
    AT_DIRECTION1,
    AT_UNKNOWN1,
    AT_CRY1,
    AT_CHEER1,
    AT_AWKWARD1,
    AT_SEE1,
    AT_WIN1,
    AT_SMILE1,
    AT_SLEEP1,
    AT_COLD1,
    AT_AGAIN1,
    AT_RESPECT1,
    AT_SALUTE1,
    AT_RUSH1,
    AT_SCISSORS,
    AT_ROCK,
    AT_PAPER,
    AT_HUSTLE,
    AT_PROVOCATION,
    AT_LOOK_AROUND,
    AT_CHEERS,
    AT_JACK1,
    AT_JACK2,
    AT_SANTA1_1,
    AT_SANTA1_2,
    AT_SANTA1_3,
    AT_SANTA2_1,
    AT_SANTA2_2,
    AT_SANTA2_3,
    AT_RAGEBUFF_1,
    AT_RAGEBUFF_2,
    AT_SET_OPTION_IMPROVE_STRENGTH = AT_RAGEBUFF_2 + 1,
    AT_SET_OPTION_IMPROVE_DEXTERITY,
    AT_SET_OPTION_IMPROVE_ENERGY,
    AT_SET_OPTION_IMPROVE_VITALITY,
    AT_SET_OPTION_IMPROVE_CHARISMA,
    AT_SET_OPTION_IMPROVE_ATTACK_MIN,
    AT_SET_OPTION_IMPROVE_ATTACK_MAX,
    AT_SET_OPTION_IMPROVE_MAGIC_POWER,
    AT_SET_OPTION_IMPROVE_DAMAGE,
    AT_SET_OPTION_IMPROVE_ATTACKING_PERCENT,
    AT_SET_OPTION_IMPROVE_DEFENCE,
    AT_SET_OPTION_IMPROVE_MAX_LIFE,
    AT_SET_OPTION_IMPROVE_MAX_MANA,
    AT_SET_OPTION_IMPROVE_MAX_AG,
    AT_SET_OPTION_IMPROVE_ADD_AG,
    AT_SET_OPTION_IMPROVE_CRITICAL_DAMAGE_PERCENT,
    AT_SET_OPTION_IMPROVE_CRITICAL_DAMAGE,
    AT_SET_OPTION_IMPROVE_EXCELLENT_DAMAGE_PERCENT,
    AT_SET_OPTION_IMPROVE_EXCELLENT_DAMAGE,
    AT_SET_OPTION_IMPROVE_SKILL_ATTACK,
    AT_SET_OPTION_DOUBLE_DAMAGE,
    AT_SET_OPTION_DISABLE_DEFENCE,
    AT_SET_OPTION_IMPROVE_SHIELD_DEFENCE,
    AT_SET_OPTION_TWO_HAND_SWORD_IMPROVE_DAMAGE,

    AT_SET_OPTION_IMPROVE_ATTACK_2,
    AT_SET_OPTION_IMPROVE_ATTACK_1,
    AT_SET_OPTION_IMPROVE_DEFENCE_3,
    AT_SET_OPTION_IMPROVE_DEFENCE_4,
    AT_SET_OPTION_IMPROVE_MAGIC,
    AT_SET_OPTION_ICE_MASTERY,
    AT_SET_OPTION_POSION_MASTERY,
    AT_SET_OPTION_THUNDER_MASTERY,
    AT_SET_OPTION_FIRE_MASTERY,
    AT_SET_OPTION_EARTH_MASTERY,
    AT_SET_OPTION_WIND_MASTERY,
    AT_SET_OPTION_WATER_MASTERY,

    AT_IMPROVE_MAX_MANA,
    AT_IMPROVE_MAX_AG,
};

enum
{
    REVIVAL_SUCCESS = 0,
    REVIVAL_ERROR_ZEN,
    REVIVAL_ERROR_LEVEL,
    REVIVAL_ERROR_END
};

constexpr auto FENRIR_TYPE_BLACK = 0;
constexpr auto FENRIR_TYPE_RED = 1;
constexpr auto FENRIR_TYPE_BLUE = 2;
constexpr auto FENRIR_TYPE_GOLD = 3;

//Character Buff
#define g_isNotCharacterBuff(o) o->m_BuffMap.isBuff()

#define g_isCharacterBuff(o, bufftype) (o)->m_BuffMap.isBuff(bufftype)

#define g_isCharacterBufflist(o, bufftypelist) o->m_BuffMap.isBuff(bufftypelist)

#define g_TokenCharacterBuff(o, bufftype) o->m_BuffMap.TokenBuff(bufftype)

#define g_CharacterBuffCount(o, bufftype) o->m_BuffMap.GetBuffCount(bufftype)

#define g_CharacterBuffSize(o) o->m_BuffMap.GetBuffSize()

#define g_CharacterBuff(o, iterindex) o->m_BuffMap.GetBuff(iterindex)

#define g_CharacterRegisterBuff(o, bufftype) o->m_BuffMap.RegisterBuff(bufftype)

#define g_CharacterRegisterBufflist(o, bufftypelist) o->m_BuffMap.RegisterBuff(bufftypelist)

#define g_CharacterUnRegisterBuff(o, bufftype) o->m_BuffMap.UnRegisterBuff(bufftype)

#define g_CharacterUnRegisterBuffList(o, bufftypelist) o->m_BuffMap.UnRegisterBuff(bufftypelist)

#define g_CharacterCopyBuff(outObj, inObj) (outObj)->m_BuffMap = (inObj)->m_BuffMap

#define g_CharacterClearBuff(o) o->m_BuffMap.ClearBuff()

//TheBuffInfo
#define g_BuffInfo(buff) TheBuffInfo().GetBuffinfo(buff)

#define g_IsBuffClass(buff) TheBuffInfo().IsBuffClass(buff)

//TheBuffTimeControl
#define g_RegisterBuffTime(bufftype, curbufftime)                                                  \
    TheBuffTimeControl().RegisterBuffTime(bufftype, curbufftime)

#define g_UnRegisterBuffTime(bufftype) TheBuffTimeControl().UnRegisterBuffTime(bufftype)

#define g_BuffStringTime(bufftype, timeText)                                                       \
    TheBuffTimeControl().GetBuffStringTime(bufftype, timeText)

#define g_StringTime(time, timeText, issecond)                                                     \
    TheBuffTimeControl().GetStringTime(time, timeText, issecond)

//TheBuffStateValueControl
#define g_BuffStateValue(type) TheBuffStateValueControl().GetValue(type)

#define g_BuffToolTipString(outstr, type) TheBuffStateValueControl().GetBuffInfoString(outstr, type)

#define g_BuffStateValueString(outstr, type)                                                       \
    TheBuffStateValueControl().GetBuffValueString(outstr, type)

bool IsMount(ITEM *pItem);

// JumpTime starts at one; the authored >15 check completed on frame 16.
inline constexpr float CharacterPushEndFrame = 16.f;
DWORD GetGuildRelationShipTextColor(BYTE GuildRelationShip);
DWORD GetGuildRelationShipBGColor(BYTE GuildRelationShip);
void LittleSantaLight(int type, vec3_t light);

void MoveEye(OBJECT *o, BMD *b, int Right, int Left, int Right2 = -1, int Left2 = -1,
             int Right3 = -1, int Left3 = -1);
void DeleteCloth(CHARACTER *c, OBJECT *o = NULL, PART_t *p2 = NULL);

void DeadCharacterBuff(OBJECT *o);
void FallingMonster(CHARACTER *character, OBJECT *object, float animationFactor);

int LevelConvert(BYTE Level);

bool CheckMonsterSkill(CHARACTER *c, OBJECT *o);
bool CharacterAnimation(CHARACTER *c, OBJECT *o);

void MakeElfHelper(CHARACTER *c);
int GetFenrirType(CHARACTER *c);

bool IsPlayer(CHARACTER *c);
bool IsMonster(CHARACTER *c);

namespace SEASON4A
{
class CSocketItemMgr;
}
class CSItemOption;

class CHARACTER_MACHINE : protected SessionLegacyCalls
{
  public:
    explicit CHARACTER_MACHINE(SessionKeeper &keeper) noexcept;
    //input
    CHARACTER_ATTRIBUTE Character{};
    ITEM Equipment[MAX_EQUIPMENT]{};
    DWORD Gold{};
    int StorageGold{};
    MONSTER Enemy{};
    //output
    WORD AttackDamageRight{};
    WORD AttackDamageLeft{};
    WORD CriticalDamage{};
    //final output
    WORD FinalAttackDamageRight{};
    WORD FinalAttackDamageLeft{};
    WORD FinalHitPoint{};
    WORD FinalAttackRating{};
    WORD FinalDefenseRating{};
    bool FinalSuccessAttack{};
    bool FinalSuccessDefense{};
    // packet
    BYTE PacketSerial{};
    BYTE InfinityArrowAdditionalMana{};
    SEASON4A::CSocketItemMgr &g_SocketItemMgr;
    CSItemOption &g_csItemOption;

    void Init();
    void InitAddValue();
    void InputEnemyAttribute(MONSTER *Enemy);
    bool IsZeroDurability();
    void CalculateDamage();
    void CalculateCriticalDamage();
    void CalculateMagicDamage();
    void CalculateCurseDamage();
    void CalculateAttackRating();
    void CalculateSuccessfulBlocking();
    void CalculateAttackRatingPK();
    void CalculateSuccessfulBlockingPK();
    void CalculateDefense();
    void CalculateMagicDefense();
    void CalculateWalkSpeed();
    void CalculateNextExperince();
    void CalulateMasterLevelNextExperience();
    void CalculateAll();
    void CalculateBasicState();
    void getAllAddStateOnlyExValues(int &iAddStrengthExValues, int &iAddDexterityExValues,
                                    int &iAddVitalityExValues, int &iAddEnergyExValues,
                                    int &iAddCharismaExValues);
};

class CMapManager;
class SessionKeeper;

class CSummonSystem : protected SessionLegacyCalls
{
  public:
    explicit CSummonSystem(SessionKeeper &keeper);
    virtual ~CSummonSystem();

    void MoveEquipEffect(CHARACTER *pCharacter, int iItemType, int iItemLevel, int iItemOption1,
                         double worldTime, bool removeAbsent = true);
    void RemoveEquipEffects(CHARACTER *pCharacter);
    void RetireCharacter(CHARACTER *character);
    void ForgetCharacterPhase(const OBJECT *character);
    void RemoveEquipEffect_Summon(CHARACTER *pCharacter);

    void CastSummonSkill(int iSkill, CHARACTER *pCharacter, OBJECT *pObject, int iTargetPos_X,
                         int iTargetPos_Y);

    void CreateDamageOfTimeEffect(int iSkill, OBJECT *pObject);
    void RemoveDamageOfTimeEffect(int iSkill, OBJECT *pObject);
    void RemoveAllDamageOfTimeEffect(OBJECT *pObject);

  protected:
    void CreateEquipEffect_WristRing(CHARACTER *pCharacter, int iItemType, int iItemLevel,
                                     int iItemOption1, double worldTime);
    void RemoveEquipEffect_WristRing(CHARACTER *pCharacter);
    void CreateEquipEffect_Summon(CHARACTER *pCharacter, int iItemType, int iItemLevel,
                                  int iItemOption1, double worldTime);

    void CreateCastingEffect(vec3_t vPosition, vec3_t vAngle, int iSubType);

    void CreateSummonObject(int iSkill, CHARACTER *pCharacter, OBJECT *pObject, float fTargetPos_X,
                            float fTargetPos_Y);
    void SetPlayerSummon(CHARACTER *pCharacter, OBJECT *pObject);

  protected:
    CMapManager &gMapManager;
    std::map<const OBJECT *, BYTE> m_EquipEffectRandom;
};

namespace CharacterRulesDetail
{

#pragma pack(push)
#pragma pack()
struct ClassTextEntry
{
    CLASS_TYPE type;
    int textIndex;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kThirdGenWingMin = ITEM_WING + 130;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kThirdGenWingMax = ITEM_WING + 134;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kSpecialWingType = ITEM_WING + 135;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kDefaultClassTextIndex = 2305;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kMasterExperienceUnlockLevel = 400;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr std::array<ClassTextEntry, 18> kClassTextEntries{{
    {CLASS_WIZARD, 20},
    {CLASS_SOULMASTER, 25},
    {CLASS_GRANDMASTER, 1669},
    {CLASS_KNIGHT, 21},
    {CLASS_BLADEKNIGHT, 26},
    {CLASS_BLADEMASTER, 1668},
    {CLASS_ELF, 22},
    {CLASS_MUSEELF, 27},
    {CLASS_HIGHELF, 1670},
    {CLASS_DARK, 23},
    {CLASS_DUELMASTER, 1671},
    {CLASS_DARK_LORD, 24},
    {CLASS_LORDEMPEROR, 1672},
    {CLASS_SUMMONER, 1687},
    {CLASS_BLOODYSUMMONER, 1688},
    {CLASS_DIMENSIONMASTER, 1689},
    {CLASS_RAGEFIGHTER, 3150},
    {CLASS_TEMPLENIGHT, 3151},
}};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
template <typename T, typename U, typename V>
constexpr bool InRange(const T value, const U minValue, const V maxValue)
{
    using Common = std::common_type_t<T, U, V>;
    const Common commonValue = static_cast<Common>(value);
    return (commonValue >= static_cast<Common>(minValue)) &&
           (commonValue <= static_cast<Common>(maxValue));
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
constexpr bool IsWingType(int type)
{
    return InRange(type, ITEM_WING, ITEM_WINGS_OF_DARKNESS) ||
           InRange(type, ITEM_WING_OF_STORM, ITEM_WING_OF_DIMENSION) || type == ITEM_CAPE_OF_LORD ||
           InRange(type, kThirdGenWingMin, kThirdGenWingMax) ||
           InRange(type, ITEM_CAPE_OF_FIGHTER, ITEM_CAPE_OF_OVERRULE) || type == kSpecialWingType;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
constexpr bool IsBowModel(int type)
{
    return InRange(type, MODEL_BOW, MODEL_CHAOS_NATURE_BOW) || type == MODEL_CELESTIAL_BOW ||
           InRange(type, MODEL_ARROW_VIPER_BOW, MODEL_STINGER_BOW) || type == MODEL_AIR_LYN_BOW;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
constexpr bool IsCrossbowModel(int type)
{
    return InRange(type, MODEL_CROSSBOW, MODEL_AQUAGOLD_CROSSBOW) || type == MODEL_SAINT_CROSSBOW ||
           InRange(type, MODEL_DIVINE_CB_OF_ARCHANGEL, MODEL_GREAT_REIGN_CROSSBOW);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
constexpr bool IsGeneralBowItem(int type)
{
    return InRange(type, ITEM_BOW, ITEM_CHAOS_NATURE_BOW) || type == ITEM_CELESTIAL_BOW ||
           InRange(type, ITEM_ARROW_VIPER_BOW, ITEM_STINGER_BOW) || type == ITEM_AIR_LYN_BOW;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
constexpr bool IsGeneralCrossbowItem(int type)
{
    return InRange(type, ITEM_CROSSBOW, ITEM_AQUAGOLD_CROSSBOW) || type == ITEM_SAINT_CROSSBOW ||
           InRange(type, ITEM_DIVINE_CB_OF_ARCHANGEL, ITEM_GREAT_REIGN_CROSSBOW);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
constexpr bool IsEquippedBowItem(int type)
{
    return IsGeneralBowItem(type);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
constexpr bool IsEquippedCrossbowItem(int type)
{
    return IsGeneralCrossbowItem(type);
}
#pragma pack(pop)

} // namespace CharacterRulesDetail

namespace PetSystemDetail
{

#pragma pack(push)
#pragma pack()
inline constexpr float PetContactTolerance = 0.01f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
template <class Travel>
float PetTargetStep(const OBJECT &pet, const vec3_t target, float radius, float frames,
                    Travel &&travel)
{
    vec3_t difference, forward, local{0.f, -1.f, 0.f};
    float matrix[3][4];
    VectorSubtract(target, pet.Position, difference);
    const float distanceSquared = DotProduct(difference, difference);
    if (distanceSquared <= (radius + PetContactTolerance) * (radius + PetContactTolerance))
        return 0.f;
    AngleMatrix(pet.Angle, matrix);
    VectorRotate(local, matrix, forward);
    const float projection = DotProduct(difference, forward);
    const float perpendicular = (std::max)(0.f, distanceSquared - projection * projection);
    if (projection <= 0.f || perpendicular > radius * radius)
        return frames;
    const float contact = projection - std::sqrt(radius * radius - perpendicular);
    if (travel(frames) < contact)
        return frames;
    float before = 0.f, after = frames;
    for (int iteration = 0; iteration < 20; ++iteration)
    {
        const float middle = (before + after) * 0.5f;
        if (travel(middle) < contact)
            before = middle;
        else
            after = middle;
    }
    return after;
}
#pragma pack(pop)

} // namespace PetSystemDetail

namespace PetManagerDetail

{

#pragma pack(push)
#pragma pack()
inline constexpr int kShiftKeyCode = 0x10;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr std::uint16_t kInvalidTargetKey = 0xFFFF;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr std::size_t kTooltipBufferCapacity = 100;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kPetCommandCount = AT_PET_COMMAND_END - AT_PET_COMMAND_DEFAULT;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kTooltipLineLimit = 50;
#pragma pack(pop)

} // namespace PetManagerDetail

namespace PetSystemDetail
{

void OwnerBonePosition(const OBJECT &owner, int bone, const vec3_t offset, vec3_t position);
void SampleOwnerBonePosition(const CHARACTER &character, BMD &model, int bone, const vec3_t offset,
                             double time, float fraction, vec3_t position);
float PetRushDistance(const OBJECT &pet, float frames);
float AdvancePetRush(OBJECT &pet, float frames);
void RescueDistantPet(OBJECT &pet, const vec3_t ownerPosition, float frames);
} // namespace PetSystemDetail

namespace PetManagerDetail
{

std::wstring SanitizeWideStringFormat(const wchar_t *format);
std::uint32_t ComposeItemIndex(int sx, int sy);
} // namespace PetManagerDetail

void CalcAddPosition(OBJECT *object, float x, float y, float z, vec3_t position);
