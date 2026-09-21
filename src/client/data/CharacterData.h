#pragma once
#include "support/CoreMath.h"
#include "data/GameData.h"
#include "data/ItemData.h"

#include <memory>
#include <optional>
#include <array>
#include <cstdint>

#define NUMOFMON 10000
#define MODEL_BODY_NUM 24

enum PetCommandType
{
    AT_PET_COMMAND_DEFAULT = 120,
    AT_PET_COMMAND_RANDOM,
    AT_PET_COMMAND_OWNER,
    AT_PET_COMMAND_TARGET,
    AT_PET_COMMAND_END,
};

class OBJECT;
class CHARACTER;
#define BODYPART_HEAD 0
#define BODYPART_HELM 1
#define BODYPART_ARMOR 2
#define BODYPART_PANTS 3
#define BODYPART_GLOVES 4
#define BODYPART_BOOTS 5
#define MAX_BODYPART 6
#define MAX_PATH_FIND 15

struct CharacterSocketSource final
{
    const OBJECT *object = nullptr;
};

struct CharacterSocketBinding final
{
    std::shared_ptr<const CharacterSocketSource> source;
};

struct CharacterTargetBinding final
{
    int sourceIndex = -1;
    int key = -1;
    std::uint64_t revision = 0;
    std::optional<SessionId> sender;
    CHARACTER *character = nullptr;
    std::shared_ptr<const CharacterSocketSource> source;
    CHARACTER *Resolve() const noexcept
    {
        return source && source->object ? character : nullptr;
    }
};

struct CharacterPetCommands final
{
    bool present = false;
    std::uint64_t generation = 0;
    std::uint64_t sequence = 0;
    std::uint64_t commandRevision = 0;
    std::uint64_t attackRevision = 0;
    std::shared_ptr<const CharacterSocketSource> commandTargetSource;
    std::shared_ptr<const CharacterSocketSource> attackTargetSource;
    int commandTarget = -1;
    int attackTarget = -1;
    std::uint8_t command = 0;
    int attackType = 0;
    std::uint16_t level = 0;
};

typedef struct _PART_t
{
    short Type;
    BYTE Level;
    BYTE ExcellentFlags;       // Excellent
    BYTE AncientDiscriminator; // Ancient
    BYTE LinkBone;
    BYTE CurrentAction;
    unsigned short PriorAction;
    float AnimationFrame;
    float PriorAnimationFrame;
    float PlaySpeed;

    _PART_t()
    {
        Type = 0;
        Level = 0;
        ExcellentFlags = 0;
        AncientDiscriminator = 0;
        LinkBone = 0;
        CurrentAction = 0;
        PriorAction = 0;
        AnimationFrame = 0;
        PriorAnimationFrame = 0;
        PlaySpeed = 0;
    }
} PART_t;

typedef struct tagPET_INFO
{
    DWORD m_dwPetType;
    DWORD m_dwExp1;
    DWORD m_dwExp2;
    WORD m_wLevel;
    WORD m_wLife;
    WORD m_wDamageMin;
    WORD m_wDamageMax;
    WORD m_wAttackSpeed;
    WORD m_wAttackSuccess;
} PET_INFO;

enum
{
    PLAYER_SET,
    PLAYER_STOP_MALE,
    PLAYER_STOP_FEMALE,
    PLAYER_STOP_SUMMONER,
    PLAYER_STOP_SWORD,
    PLAYER_STOP_TWO_HAND_SWORD,
    PLAYER_STOP_SPEAR,
    PLAYER_STOP_SCYTHE,
    PLAYER_STOP_BOW,
    PLAYER_STOP_CROSSBOW,
    PLAYER_STOP_WAND,
    PLAYER_STOP_FLY,
    PLAYER_STOP_FLY_CROSSBOW,
    PLAYER_STOP_RIDE,
    PLAYER_STOP_RIDE_WEAPON,

    PLAYER_WALK_MALE,
    PLAYER_WALK_FEMALE,
    PLAYER_WALK_SWORD,
    PLAYER_WALK_TWO_HAND_SWORD,
    PLAYER_WALK_SPEAR,
    PLAYER_WALK_SCYTHE,
    PLAYER_WALK_BOW,
    PLAYER_WALK_CROSSBOW,
    PLAYER_WALK_WAND,
    PLAYER_WALK_SWIM,
    PLAYER_RUN,
    PLAYER_RUN_SWORD,
    PLAYER_RUN_TWO_SWORD,
    PLAYER_RUN_TWO_HAND_SWORD,
    PLAYER_RUN_SPEAR,
    PLAYER_RUN_BOW,
    PLAYER_RUN_CROSSBOW,
    PLAYER_RUN_WAND,
    PLAYER_RUN_SWIM,
    PLAYER_FLY,
    PLAYER_FLY_CROSSBOW,
    PLAYER_RUN_RIDE,
    PLAYER_RUN_RIDE_WEAPON,

    PLAYER_ATTACK_FIST,
    PLAYER_ATTACK_SWORD_RIGHT1,
    PLAYER_ATTACK_SWORD_RIGHT2,
    PLAYER_ATTACK_SWORD_LEFT1,
    PLAYER_ATTACK_SWORD_LEFT2,
    PLAYER_ATTACK_TWO_HAND_SWORD1,
    PLAYER_ATTACK_TWO_HAND_SWORD2,
    PLAYER_ATTACK_TWO_HAND_SWORD3,
    PLAYER_ATTACK_SPEAR1,
    PLAYER_ATTACK_SCYTHE1,
    PLAYER_ATTACK_SCYTHE2,
    PLAYER_ATTACK_SCYTHE3,
    PLAYER_ATTACK_BOW,
    PLAYER_ATTACK_CROSSBOW,
    PLAYER_ATTACK_FLY_BOW,
    PLAYER_ATTACK_FLY_CROSSBOW,
    PLAYER_ATTACK_RIDE_SWORD,
    PLAYER_ATTACK_RIDE_TWO_HAND_SWORD,
    PLAYER_ATTACK_RIDE_SPEAR,
    PLAYER_ATTACK_RIDE_SCYTHE,
    PLAYER_ATTACK_RIDE_BOW,
    PLAYER_ATTACK_RIDE_CROSSBOW,

    PLAYER_ATTACK_SKILL_SWORD1,
    PLAYER_ATTACK_SKILL_SWORD2,
    PLAYER_ATTACK_SKILL_SWORD3,
    PLAYER_ATTACK_SKILL_SWORD4,
    PLAYER_ATTACK_SKILL_SWORD5,

    PLAYER_ATTACK_SKILL_WHEEL,
    PLAYER_ATTACK_SKILL_FURY_STRIKE,
    PLAYER_SKILL_VITALITY,
    PLAYER_SKILL_RIDER,
    PLAYER_SKILL_RIDER_FLY,
    PLAYER_ATTACK_SKILL_SPEAR,
    PLAYER_ATTACK_DEATHSTAB,
    PLAYER_SKILL_HELL_BEGIN,
    PLAYER_SKILL_HELL_START,

    PLAYER_ATTACK_END,

    PLAYER_FLY_RIDE = PLAYER_ATTACK_END,
    PLAYER_FLY_RIDE_WEAPON,
    PLAYER_DARKLORD_STAND,
    PLAYER_DARKLORD_WALK,
    PLAYER_STOP_RIDE_HORSE,
    PLAYER_RUN_RIDE_HORSE,
    PLAYER_ATTACK_STRIKE,
    PLAYER_ATTACK_TELEPORT,
    PLAYER_ATTACK_RIDE_STRIKE,
    PLAYER_ATTACK_RIDE_TELEPORT,
    PLAYER_ATTACK_RIDE_HORSE_SWORD,
    PLAYER_ATTACK_RIDE_ATTACK_FLASH,
    PLAYER_ATTACK_RIDE_ATTACK_MAGIC,
    PLAYER_ATTACK_DARKHORSE,
    PLAYER_IDLE1_DARKHORSE,
    PLAYER_IDLE2_DARKHORSE,
    PLAYER_FENRIR_ATTACK,
    PLAYER_FENRIR_ATTACK_DARKLORD_AQUA,
    PLAYER_FENRIR_ATTACK_DARKLORD_STRIKE,
    PLAYER_FENRIR_ATTACK_DARKLORD_SWORD,
    PLAYER_FENRIR_ATTACK_DARKLORD_TELEPORT,
    PLAYER_FENRIR_ATTACK_DARKLORD_FLASH,
    PLAYER_FENRIR_ATTACK_TWO_SWORD,
    PLAYER_FENRIR_ATTACK_MAGIC,
    PLAYER_FENRIR_ATTACK_CROSSBOW,
    PLAYER_FENRIR_ATTACK_SPEAR,
    PLAYER_FENRIR_ATTACK_ONE_SWORD,
    PLAYER_FENRIR_ATTACK_BOW,
    PLAYER_FENRIR_SKILL,
    PLAYER_FENRIR_SKILL_TWO_SWORD,
    PLAYER_FENRIR_SKILL_ONE_RIGHT,
    PLAYER_FENRIR_SKILL_ONE_LEFT,
    PLAYER_FENRIR_DAMAGE,
    PLAYER_FENRIR_DAMAGE_TWO_SWORD,
    PLAYER_FENRIR_DAMAGE_ONE_RIGHT,
    PLAYER_FENRIR_DAMAGE_ONE_LEFT,
    PLAYER_FENRIR_RUN,
    PLAYER_FENRIR_RUN_TWO_SWORD,
    PLAYER_FENRIR_RUN_ONE_RIGHT,
    PLAYER_FENRIR_RUN_ONE_LEFT,
    PLAYER_FENRIR_RUN_MAGOM,
    PLAYER_FENRIR_RUN_TWO_SWORD_MAGOM,
    PLAYER_FENRIR_RUN_ONE_RIGHT_MAGOM,
    PLAYER_FENRIR_RUN_ONE_LEFT_MAGOM,
    PLAYER_FENRIR_RUN_ELF,
    PLAYER_FENRIR_RUN_TWO_SWORD_ELF,
    PLAYER_FENRIR_RUN_ONE_RIGHT_ELF,
    PLAYER_FENRIR_RUN_ONE_LEFT_ELF,
    PLAYER_FENRIR_STAND,
    PLAYER_FENRIR_STAND_TWO_SWORD,
    PLAYER_FENRIR_STAND_ONE_RIGHT,
    PLAYER_FENRIR_STAND_ONE_LEFT,
    PLAYER_FENRIR_WALK,
    PLAYER_FENRIR_WALK_TWO_SWORD,
    PLAYER_FENRIR_WALK_ONE_RIGHT,
    PLAYER_FENRIR_WALK_ONE_LEFT,

    PLAYER_ATTACK_BOW_UP,
    PLAYER_ATTACK_CROSSBOW_UP,
    PLAYER_ATTACK_FLY_BOW_UP,
    PLAYER_ATTACK_FLY_CROSSBOW_UP,
    PLAYER_ATTACK_RIDE_BOW_UP,
    PLAYER_ATTACK_RIDE_CROSSBOW_UP,
    PLAYER_ATTACK_ONE_FLASH,
    PLAYER_ATTACK_RUSH,
    PLAYER_ATTACK_DEATH_CANNON,
    PLAYER_ATTACK_REMOVAL,
    PLAYER_ATTACK_STUN,
    PLAYER_HIGH_SHOCK,

    PLAYER_STOP_TWO_HAND_SWORD_TWO,
    PLAYER_WALK_TWO_HAND_SWORD_TWO,
    PLAYER_RUN_TWO_HAND_SWORD_TWO,
    PLAYER_ATTACK_TWO_HAND_SWORD_TWO,

    PLAYER_SKILL_HAND1,
    PLAYER_SKILL_HAND2,
    PLAYER_SKILL_WEAPON1,
    PLAYER_SKILL_WEAPON2,
    PLAYER_SKILL_ELF1,
    PLAYER_SKILL_TELEPORT,
    PLAYER_SKILL_FLASH,
    PLAYER_SKILL_INFERNO,
    PLAYER_SKILL_HELL,
    PLAYER_RIDE_SKILL,
    PLAYER_SKILL_SLEEP,
    PLAYER_SKILL_SLEEP_UNI,
    PLAYER_SKILL_SLEEP_DINO,
    PLAYER_SKILL_SLEEP_FENRIR,
    PLAYER_SKILL_CHAIN_LIGHTNING,
    PLAYER_SKILL_CHAIN_LIGHTNING_UNI,
    PLAYER_SKILL_CHAIN_LIGHTNING_DINO,
    PLAYER_SKILL_CHAIN_LIGHTNING_FENRIR,
    PLAYER_SKILL_LIGHTNING_ORB,
    PLAYER_SKILL_LIGHTNING_ORB_UNI,
    PLAYER_SKILL_LIGHTNING_ORB_DINO,
    PLAYER_SKILL_LIGHTNING_ORB_FENRIR,
    PLAYER_SKILL_DRAIN_LIFE,
    PLAYER_SKILL_DRAIN_LIFE_UNI,
    PLAYER_SKILL_DRAIN_LIFE_DINO,
    PLAYER_SKILL_DRAIN_LIFE_FENRIR,
    PLAYER_SKILL_SUMMON,
    PLAYER_SKILL_SUMMON_UNI,
    PLAYER_SKILL_SUMMON_DINO,
    PLAYER_SKILL_SUMMON_FENRIR,

    PLAYER_SKILL_BLOW_OF_DESTRUCTION,
    PLAYER_SKILL_SWELL_OF_MP,
    PLAYER_SKILL_MULTISHOT_BOW_STAND,
    PLAYER_SKILL_MULTISHOT_BOW_FLYING,
    PLAYER_SKILL_MULTISHOT_CROSSBOW_STAND,
    PLAYER_SKILL_MULTISHOT_CROSSBOW_FLYING,
    PLAYER_SKILL_RECOVERY,
    PLAYER_SKILL_GIGANTICSTORM,
    PLAYER_SKILL_FLAMESTRIKE,
    PLAYER_SKILL_LIGHTNING_SHOCK,

#ifdef YDG_ADD_SKILL_RIDING_ANIMATIONS
    PLAYER_SKILL_GIGANTICSTORM_UNI,
    PLAYER_SKILL_GIGANTICSTORM_DINO,
    PLAYER_SKILL_GIGANTICSTORM_FENRIR,
    PLAYER_ATTACK_SKILL_WHEEL_UNI,
    PLAYER_ATTACK_SKILL_WHEEL_DINO,
    PLAYER_ATTACK_SKILL_WHEEL_FENRIR,
#endif // YDG_ADD_SKILL_RIDING_ANIMATIONS
    PLAYER_DEFENSE1,
    PLAYER_GREETING1,
    PLAYER_GREETING_FEMALE1,
    PLAYER_GOODBYE1,
    PLAYER_GOODBYE_FEMALE1,
    PLAYER_CLAP1,
    PLAYER_CLAP_FEMALE1,
    PLAYER_CHEER1,
    PLAYER_CHEER_FEMALE1,
    PLAYER_DIRECTION1,
    PLAYER_DIRECTION_FEMALE1,
    PLAYER_GESTURE1,
    PLAYER_GESTURE_FEMALE1,
    PLAYER_UNKNOWN1,
    PLAYER_UNKNOWN_FEMALE1,
    PLAYER_CRY1,
    PLAYER_CRY_FEMALE1,
    PLAYER_AWKWARD1,
    PLAYER_AWKWARD_FEMALE1,
    PLAYER_SEE1,
    PLAYER_SEE_FEMALE1,
    PLAYER_WIN1,
    PLAYER_WIN_FEMALE1,
    PLAYER_SMILE1,
    PLAYER_SMILE_FEMALE1,
    PLAYER_SLEEP1,
    PLAYER_SLEEP_FEMALE1,
    PLAYER_COLD1,
    PLAYER_COLD_FEMALE1,
    PLAYER_AGAIN1,
    PLAYER_AGAIN_FEMALE1,
    PLAYER_RESPECT1,
    PLAYER_SALUTE1,
    PLAYER_SCISSORS,
    PLAYER_ROCK,
    PLAYER_PAPER,
    PLAYER_HUSTLE,
    PLAYER_PROVOCATION,
    PLAYER_LOOK_AROUND,
    PLAYER_CHEERS,
    PLAYER_KOREA_HANDCLAP,
    PLAYER_POINT_DANCE,
    PLAYER_RUSH1,
    PLAYER_COME_UP,
    PLAYER_SHOCK,
    PLAYER_DIE1,
    PLAYER_DIE2,
    PLAYER_SIT1,
    PLAYER_SIT2,
    PLAYER_SIT_FEMALE1,
    PLAYER_SIT_FEMALE2,
    PLAYER_HEALING1,
    PLAYER_HEALING_FEMALE1,
    PLAYER_POSE1,
    PLAYER_POSE_FEMALE1,
    PLAYER_JACK_1,
    PLAYER_JACK_2,
    PLAYER_SANTA_1,
    PLAYER_SANTA_2,
    PLAYER_CHANGE_UP,
    PLAYER_RECOVER_SKILL,

    PLAYER_SKILL_THRUST,
    PLAYER_SKILL_STAMP,
    PLAYER_SKILL_GIANTSWING,
    PLAYER_SKILL_DARKSIDE_READY,
    PLAYER_SKILL_DARKSIDE_ATTACK,
    PLAYER_SKILL_DRAGONKICK,
    PLAYER_SKILL_DRAGONLORE,
    PLAYER_SKILL_PHOENIX_SHOT,
    PLAYER_SKILL_ATT_UP_OURFORCES,
    PLAYER_SKILL_HP_UP_OURFORCES,
    // Rage Fighter related animations
    PLAYER_RAGE_UNI_ATTACK,
    PLAYER_RAGE_UNI_ATTACK_ONE_RIGHT,
    PLAYER_RAGE_UNI_RUN,
    PLAYER_RAGE_UNI_RUN_ONE_RIGHT,
    PLAYER_RAGE_UNI_STOP_ONE_RIGHT,
    PLAYER_RAGE_FENRIR,
    PLAYER_RAGE_FENRIR_TWO_SWORD,
    PLAYER_RAGE_FENRIR_ONE_RIGHT,
    PLAYER_RAGE_FENRIR_ONE_LEFT,
    PLAYER_RAGE_FENRIR_WALK,
    PLAYER_RAGE_FENRIR_WALK_ONE_RIGHT,
    PLAYER_RAGE_FENRIR_WALK_ONE_LEFT,
    PLAYER_RAGE_FENRIR_WALK_TWO_SWORD,
    PLAYER_RAGE_FENRIR_RUN,
    PLAYER_RAGE_FENRIR_RUN_TWO_SWORD,
    PLAYER_RAGE_FENRIR_RUN_ONE_RIGHT,
    PLAYER_RAGE_FENRIR_RUN_ONE_LEFT,
    PLAYER_RAGE_FENRIR_STAND,
    PLAYER_RAGE_FENRIR_STAND_TWO_SWORD,
    PLAYER_RAGE_FENRIR_STAND_ONE_RIGHT,
    PLAYER_RAGE_FENRIR_STAND_ONE_LEFT,
    PLAYER_RAGE_FENRIR_DAMAGE,
    PLAYER_RAGE_FENRIR_DAMAGE_TWO_SWORD,
    PLAYER_RAGE_FENRIR_DAMAGE_ONE_RIGHT,
    PLAYER_RAGE_FENRIR_DAMAGE_ONE_LEFT,
    PLAYER_RAGE_FENRIR_ATTACK_RIGHT,
    PLAYER_STOP_RAGEFIGHTER,
    MAX_PLAYER_ACTION,
};

enum PET_TYPE
{
    PET_TYPE_NONE = -1,
    PET_TYPE_DARK_SPIRIT = 0,
    PET_TYPE_DARK_HORSE,
    PET_TYPE_END
};

struct CharacterSocketSource;
#define MAX_MASTER 24
#define MAX_RESISTANCE 7
#define MAX_CLASS 7
#define MAX_CLASS_STAGES 3
#define MAX_MONSTER 1024
#define MAX_SKILLS 650
#define MAX_DUTY_CLASS 3
#define MAX_CHARACTERS_SERVER 10
#define MAX_CHARACTERS_PER_ACCOUNT 5
#define RESISTANCE_COLD 0
#define RESISTANCE_POISON 1
#define RESISTANCE_THUNDER 2
#define RESISTANCE_FIRE 3
#define RESISTANCE_EARTH 4
#define RESISTANCE_WIND 5
#define RESISTANCE_WATER 6

// Server target identities and attack progress, shared by all observers.
struct CharacterDarksideState
{
    struct Target
    {
        int key = -1;
        std::shared_ptr<const CharacterSocketSource> source;
    };
    static constexpr int TargetCount = 5;
    std::array<Target, TargetCount> targets;
    Target primaryTarget;
    std::uint64_t revision = 0;
    int targetCount = 0;
    int attackCount = 0;
};

struct CharacterEquipmentSet final
{
    bool complete = false;
    bool addDefense = true;
    int level = 0;
};

struct CharacterHelperPetState final
{
    int itemType = -1;
    int modelType = -1;
    int subType = 0;
    int linkBone = 0;
    float position[3]{};
    std::uint64_t generation = 0;
    std::uint64_t commandRevision = 0;
    int targetKey = -1;
    int action = 0;
};

struct CharacterMountState final
{
    int type = -1;
    int subType = 0;
    int linkBone = 0;
    float position[3]{};
    std::uint64_t generation = 0;
};

enum CLASS_TYPE : BYTE
{
    CLASS_UNDEFINED = 0xFF,
    CLASS_START = 0,

    CLASS_WIZARD = 0,
    CLASS_KNIGHT,
    CLASS_ELF,
    CLASS_DARK,
    CLASS_DARK_LORD,
    CLASS_SUMMONER,
    CLASS_RAGEFIGHTER,

    CLASS_SOULMASTER,
    CLASS_BLADEKNIGHT,
    CLASS_MUSEELF,
    CLASS_BLOODYSUMMONER,

    CLASS_GRANDMASTER,
    CLASS_BLADEMASTER,
    CLASS_HIGHELF,
    CLASS_DUELMASTER,
    CLASS_LORDEMPEROR,
    CLASS_DIMENSIONMASTER,
    CLASS_TEMPLENIGHT,

    CLASS_END = CLASS_TEMPLENIGHT
};

enum CLASS_SKIN_INDEX : BYTE
{
    SKIN_CLASS_WIZARD = 0,
    SKIN_CLASS_KNIGHT,
    SKIN_CLASS_ELF,
    SKIN_CLASS_DARK,
    SKIN_CLASS_DARK_LORD,
    SKIN_CLASS_SUMMONER,
    SKIN_CLASS_RAGEFIGHTER,
    SKIN_CLASS_SOULMASTER,
    SKIN_CLASS_BLADEKNIGHT,
    SKIN_CLASS_MUSEELF,
    SKIN_CLASS_BLOODYSUMMONER = 12,
    SKIN_CLASS_GRANDMASTER = 14,
    SKIN_CLASS_BLADEMASTER,
    SKIN_CLASS_HIGHELF,
    SKIN_CLASS_DUELMASTER,
    SKIN_CLASS_LORDEMPEROR,
    SKIN_CLASS_DIMENSIONMASTER,
    SKIN_CLASS_TEMPLENIGHT,
};

enum SERVER_CLASS_TYPE : BYTE
{
    DarkWizard = 0,
    SoulMaster = 2,
    GrandMaster = 3,
    DarkKnight = 4,
    BladeKnight = 6,
    BladeMaster = 7,
    FairyElf = 8,
    MuseElf = 10,
    HighElf = 11,
    MagicGladiator = 12,
    DuelMaster = 13,
    DarkLord = 16,
    LordEmperor = 17,
    Summoner = 20,
    BloodySummoner = 22,
    DimensionMaster = 23,
    RageFighter = 24,
    FistMaster = 25,
};

typedef struct _MASTER_LEVEL_DATA
{
    BYTE Width;
    int Ability[8][4];
} MASTER_LEVEL_DATA;

typedef struct
{
    WORD Strength;
    WORD Dexterity;
    WORD Vitality;
    WORD Energy;
    WORD Life;
    WORD Mana;
    WORD Shield;
    BYTE LevelLife;
    BYTE LevelMana;
    BYTE VitalityToLife;
    BYTE EnergyToMana;
    BYTE ClassSkill[MAX_SKILLS];
} CLASS_ATTRIBUTE;

class CSkillTreeInfo
{
  public:
    CSkillTreeInfo()
    {
        this->skillLevel = 0;
        this->skillValue = 0.0f;
        this->skillNextValue = 0.0f;
    }

    CSkillTreeInfo(BYTE point, float value, float nextValue)
    {
        this->skillLevel = point;
        this->skillValue = value;
        this->skillNextValue = nextValue;
    }

    ~CSkillTreeInfo() = default;

    BYTE GetSkillLevel() const
    {
        return this->skillLevel;
    }
    float GetSkillValue() const
    {
        return this->skillValue;
    }
    float GetSkillNextValue() const
    {
        return this->skillNextValue;
    }

  private:
    BYTE skillLevel;
    float skillValue;
    float skillNextValue;
};

typedef struct tagCHARACTER_ATTRIBUTE
{
    wchar_t Name[MAX_USERNAME_SIZE + 1];
    CLASS_TYPE Class;
    BYTE Skin;
    BYTE InventoryExtensions;
    BYTE IsVaultExtended;
    WORD Level;
    DWORD Resets;

    WORD Strength;
    WORD Dexterity;
    WORD Vitality;
    WORD Energy;
    WORD Charisma;
    DWORD Life;
    DWORD Mana;
    DWORD LifeMax;
    DWORD ManaMax;
    DWORD Shield;
    DWORD ShieldMax;
    DWORD SkillMana;
    DWORD SkillManaMax;
    WORD AttackRatingPK;
    WORD SuccessfulBlockingPK;
    WORD AddStrength;
    WORD AddDexterity;
    WORD AddVitality;
    WORD AddEnergy;
    WORD AddLifeMax;
    WORD AddManaMax;
    WORD AddCharisma;
    BYTE Ability;
    float AbilityTime[3]; // Number of reference frames

    short AddPoint;
    short MaxAddPoint;
    WORD wMinusPoint;
    WORD wMaxMinusPoint;
    WORD AttackSpeed;
    WORD MaxAttackSpeed; // Maximum attack speed which can be reached on the server.
    WORD AttackRating;
    WORD AttackDamageMinRight;
    WORD AttackDamageMaxRight;
    WORD AttackDamageMinLeft;
    WORD AttackDamageMaxLeft;
    WORD MagicSpeed;
    WORD MagicDamageMin;
    WORD MagicDamageMax;
    WORD CurseDamageMin;
    WORD CurseDamageMax;
    WORD CriticalDamage;
    WORD SuccessfulBlocking;
    WORD Defense;
    WORD MagicDefense;
    WORD WalkSpeed;
    WORD LevelUpPoint;
    BYTE SkillNumber;
    BYTE SkillMasterNumber;

    uint64_t Experience;
    uint64_t NextExperience;

    ActionSkillType Skill[MAX_SKILLS];
    int SkillDelay[MAX_SKILLS];
    BYTE SkillLevel
        [MAX_SKILLS]; // Do we even need this array when we have the map of CSkillTreeInfo?

    CSkillTreeInfo MasterSkillInfo[AT_SKILL_MASTER_END + 1]; // Index = ActionSkillType

} CHARACTER_ATTRIBUTE;

typedef struct _MASTER_LEVEL_VALUE
{
    short nMLevel;
    __int64 lMasterLevel_Experince;
    __int64 lNext_MasterLevel_Experince;

    short nAddMPoint;
    short nMLevelUpMPoint;
    short nTotalMPoint;
    short nMaxPoint;
    DWORD wMaxLife;
    DWORD wMaxMana;
    DWORD wMaxShield;
    DWORD wMaxBP;
} MASTER_LEVEL_VALUE;
