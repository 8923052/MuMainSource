#pragma once
#include "support/CoreMath.h"
#include "app/Application.h"
#include "data/ItemData.h"
#include "data/Localization.h"

#include <stdio.h>
#include <mutex>
#include <string>
#include <list>
#include <map>
#include <vector>
#include <cstddef>
#include <sstream>
#include <span>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#pragma pack(push)
#pragma pack()
#define MAX_LETTER_TITLE_LENGTH 60

#pragma pack(pop)

#pragma pack(push)
#pragma pack()
#define MAX_LETTER_DATE_LENGTH 10

#pragma pack(pop)

#pragma pack(push)
#pragma pack()
#define MAX_LETTER_TIME_LENGTH 8

#pragma pack(pop)

#pragma pack(push)
#pragma pack()
#define MAX_LETTERTEXT_LENGTH 1000

#pragma pack(pop)

#pragma pack(push)
#pragma pack()
#define MAX_CHATROOM_TEXT_LENGTH 150

#pragma pack(pop)

#pragma pack(push)
#pragma pack()
#define DARKSIDE_TARGET_MAX 5

#pragma pack(pop)

#pragma pack(push)
#pragma pack()
enum ExperienceType
{
    eExperienceType_Undefined = 0,
    eExperienceType_Normal = 1,
    eExperienceType_Master = 2,
    eExperienceType_MaxLevelReached = 0x10,
    eExperienceType_MaxMasterLevelReached = 0x20,
    eExperienceType_MonsterLevelTooLowForMasterExperience = 0x21,
};
#pragma pack(pop)

// Skill identities shared by data and gameplay.
enum ActionSkillType : int
{
    AT_SKILL_UNDEFINED = 0,
    AT_SKILL_POISON = 1,
    AT_SKILL_METEO = 2,
    AT_SKILL_LIGHTNING = 3,
    AT_SKILL_FIREBALL = 4,
    AT_SKILL_FLAME = 5,
    AT_SKILL_TELEPORT = 6,
    AT_SKILL_ICE = 7,
    AT_SKILL_STORM = 8,
    AT_SKILL_EVIL_SPIRIT = 9,
    AT_SKILL_HELL_FIRE = 10,
    AT_SKILL_POWERWAVE = 11,
    AT_SKILL_FLASH = 12,
    AT_SKILL_BLAST = 13,
    AT_SKILL_INFERNO = 14,
    AT_SKILL_TELEPORT_ALLY = 15,
    AT_SKILL_SOUL_BARRIER = 16,
    AT_SKILL_ENERGYBALL = 17,
    AT_SKILL_DECAY = 38,
    AT_SKILL_ICE_STORM = 39,
    AT_SKILL_NOVA = 40,
    AT_SKILL_NOVA_BEGIN = 58,
    AT_SKILL_BLOCKING = 18,
    AT_SKILL_FALLING_SLASH = 19,
    AT_SKILL_LUNGE = 20,
    AT_SKILL_UPPERCUT = 21,
    AT_SKILL_CYCLONE = 22,
    AT_SKILL_SLASH = 23,
    AT_SKILL_IMPALE = 47,
    AT_SKILL_SWELL_LIFE = 48,
    AT_SKILL_RIDER = 49,
    AT_SKILL_TRIPLE_SHOT = 24,
    AT_SKILL_BOW = 25,
    AT_SKILL_HEALING = 26,
    AT_SKILL_DEFENSE = 27,
    AT_SKILL_ATTACK = 28,
    AT_SKILL_SUMMON = 30,
    AT_SKILL_ICE_ARROW = 51,
    AT_SKILL_PENETRATION = 52,
    AT_SKILL_IMPROVE_AG = 53,
    AT_SKILL_BLAST_CROSSBOW4 = 54,
    AT_SKILL_FIRE_SLASH = 55,
    AT_SKILL_POWER_SLASH = 56,
    AT_SKILL_TWISTING_SLASH = 41,
    AT_SKILL_RAGEFUL_BLOW = 42,
    AT_SKILL_DEATHSTAB = 43,
    AT_SKILL_RUSH = 44,
    AT_SKILL_JAVELIN = 45,
    AT_SKILL_DEEPIMPACT = 46,
    AT_SKILL_SPIRAL_SLASH = 57,
    AT_SKILL_BOSS = 50,
    AT_SKILL_COMBO = 59,
    AT_SKILL_FORCE = 60,
    AT_SKILL_FIREBURST,
    AT_SKILL_EARTHSHAKE,
    AT_SKILL_PARTY_TELEPORT,
    AT_SKILL_ADD_CRITICAL,
    AT_SKILL_THUNDER_STRIKE,
    AT_SKILL_FORCE_WAVE,
    AT_SKILL_STUN,
    AT_SKILL_REMOVAL_STUN,
    AT_SKILL_MANA,
    AT_SKILL_INVISIBLE,
    AT_SKILL_REMOVAL_INVISIBLE,
    AT_SKILL_REMOVAL_BUFF,
    AT_SKILL_DEATH_CANNON,
    AT_SKILL_SPACE_SPLIT,
    AT_SKILL_BRAND_OF_SKILL,
    AT_SKILL_PLASMA_STORM_FENRIR = 76,
    AT_SKILL_INFINITY_ARROW,
    AT_SKILL_FIRE_SCREAM,
    AT_SKILL_EXPLODE,
    AT_IMPROVE_DAMAGE,
    AT_IMPROVE_MAGIC,
    AT_IMPROVE_CURSE,
    AT_IMPROVE_BLOCKING,
    AT_IMPROVE_DEFENSE,
    AT_LUCK,
    AT_LIFE_REGENERATION,
    AT_IMPROVE_LIFE,
    AT_IMPROVE_MANA,
    AT_DECREASE_DAMAGE,
    AT_REFLECTION_DAMAGE,
    AT_IMPROVE_BLOCKING_PERCENT,
    AT_IMPROVE_GAIN_GOLD,
    AT_EXCELLENT_DAMAGE,
    AT_IMPROVE_DAMAGE_LEVEL,
    AT_IMPROVE_DAMAGE_PERCENT,
    AT_IMPROVE_MAGIC_LEVEL,
    AT_IMPROVE_MAGIC_PERCENT,
    AT_IMPROVE_ATTACK_SPEED,
    AT_IMPROVE_GAIN_LIFE,
    AT_IMPROVE_GAIN_MANA,
    AT_IMPROVE_HP_MAX,
    AT_IMPROVE_MP_MAX,
    AT_ONE_PERCENT_DAMAGE,
    AT_IMPROVE_AG_MAX,
    AT_DAMAGE_ABSORB,
    AT_DAMAGE_REFLECTION,
    AT_RECOVER_FULL_LIFE,
    AT_RECOVER_FULL_MANA,
    AT_IMPROVE_CHARISMA,
    AT_IMPROVE_EVADE = 110,
    AT_SKILL_MONSTER_SUMMON = 200,
    AT_SKILL_MONSTER_MAGIC_DEF,
    AT_SKILL_MONSTER_PHY_DEF,
    AT_SKILL_HELLOWIN_EVENT_1 = 205,
    AT_SKILL_HELLOWIN_EVENT_2 = 206,
    AT_SKILL_HELLOWIN_EVENT_3 = 207,
    AT_SKILL_HELLOWIN_EVENT_4 = 208,
    AT_SKILL_HELLOWIN_EVENT_5 = 209,
    AT_SKILL_CURSED_TEMPLE_PRODECTION = 210,
    AT_SKILL_CURSED_TEMPLE_RESTRAINT = 211,
    AT_SKILL_CURSED_TEMPLE_TELEPORT = 212,
    //	AT_SKILL_CURSED_TEMPLE_QUICKNESS   = 213,
    AT_SKILL_CURSED_TEMPLE_SUBLIMATION = 213,
    AT_SKILL_ALICE_DRAINLIFE = 214,
    AT_SKILL_ALICE_CHAINLIGHTNING = 215,
    AT_SKILL_ALICE_LIGHTNINGORB = 216,
    AT_SKILL_ALICE_THORNS = 217,
    AT_SKILL_ALICE_BERSERKER = 218,
    AT_SKILL_ALICE_SLEEP = 219,
    AT_SKILL_ALICE_BLIND = 220,
    AT_SKILL_ALICE_WEAKNESS = 221,
    AT_SKILL_ALICE_ENERVATION = 222,
    AT_SKILL_SUMMON_EXPLOSION = 223,
    AT_SKILL_SUMMON_REQUIEM,
    AT_SKILL_SUMMON_POLLUTION = 225,
    AT_SKILL_LIGHTNING_SHOCK = 230,
    AT_SKILL_STRIKE_OF_DESTRUCTION = 232,
    AT_SKILL_EXPANSION_OF_WIZARDRY = 233,
    AT_SKILL_FLAME_STRIKE = 236,
    AT_SKILL_GIGANTIC_STORM = 237,
    AT_SKILL_RECOVER = 234,
    AT_SKILL_MULTI_SHOT = 235,
    AT_SKILL_CHAOTIC_DISEIER = 238,
    AT_SKILL_DOPPELGANGER_SELFDESTRUCTION = 239,
    AT_SKILL_KILLING_BLOW = 260,     // Rage fighter
    AT_SKILL_BEAST_UPPERCUT = 261,   // Rage fighter
    AT_SKILL_CHAIN_DRIVE = 262,      // Rage fighter
    AT_SKILL_DARKSIDE = 263,         // Rage fighter
    AT_SKILL_DRAGON_ROAR = 264,      // Rage fighter
    AT_SKILL_DRAGON_KICK = 265,      // Rage fighter
    AT_SKILL_ATT_UP_OURFORCES = 266, // Rage fighter
    AT_SKILL_HP_UP_OURFORCES = 267,  // Rage fighter
    AT_SKILL_DEF_UP_OURFORCES = 268, // Rage fighter
    AT_SKILL_OCCUPY = 269,           // Rage fighter
    AT_SKILL_PHOENIX_SHOT = 270,     // Rage fighter

    // Master skills:
    AT_SKILL_MASTER_BEGIN = 300,
    AT_SKILL_DurabilityReduction1 = 300,
    AT_SKILL_PvPDefenceRateInc = 301,
    AT_SKILL_MaximumSDincrease = 302,
    AT_SKILL_AutomaticManaRecInc = 303,
    AT_SKILL_PoisonResistanceInc = 304,
    AT_SKILL_DurabilityReduction2 = 305,
    AT_SKILL_SdRecoverySpeedInc = 306,
    AT_SKILL_AutomaticHpRecInc = 307,
    AT_SKILL_LightningResistanceInc = 308,
    AT_SKILL_DefenseIncrease = 309,
    AT_SKILL_AutomaticAgRecInc = 310,
    AT_SKILL_IceResistanceIncrease = 311,
    AT_SKILL_DurabilityReduction3 = 312,
    AT_SKILL_DefenseSuccessRateInc = 313,
    AT_SKILL_AttackSuccRateInc = 325,
    AT_SKILL_CYCLONE_STR = 326,
    AT_SKILL_SLASH_STR = 327,
    AT_SKILL_FALLING_SLASH_STR = 328,
    AT_SKILL_LUNGE_STR = 329,
    AT_SKILL_TWISTING_SLASH_STR = 330,
    AT_SKILL_RAGEFUL_BLOW_STR = 331,
    AT_SKILL_TWISTING_SLASH_MASTERY = 332,
    AT_SKILL_RAGEFUL_BLOW_MASTERY = 333,
    AT_SKILL_MaximumLifeIncrease = 334,
    AT_SKILL_WeaponMasteryBladeMaster = 335,
    AT_SKILL_DEATHSTAB_STR = 336,
    AT_SKILL_STRIKE_OF_DESTRUCTION_STR = 337,
    AT_SKILL_MaximumManaIncrease = 338,
    AT_SKILL_PvPAttackRate = 347,
    AT_SKILL_TwoHandedSwordStrengthener = 348,
    AT_SKILL_OneHandedSwordStrengthener = 349,
    AT_SKILL_MaceStrengthener = 350,
    AT_SKILL_SpearStrengthener = 351,
    AT_SKILL_TwoHandedSwordMaster = 352,
    AT_SKILL_OneHandedSwordMaster = 353,
    AT_SKILL_MaceMastery = 354,
    AT_SKILL_SpearMastery = 355,
    AT_SKILL_SWELL_LIFE_STR = 356,
    AT_SKILL_ManaReduction = 357,
    AT_SKILL_MonsterAttackSdInc = 358,
    AT_SKILL_MonsterAttackLifeInc = 359,
    AT_SKILL_SWELL_LIFE_PROFICIENCY = 360,
    AT_SKILL_MinimumAttackPowerInc = 361,
    AT_SKILL_MonsterAttackManaInc = 362,
    AT_SKILL_FLAME_STR = 378,
    AT_SKILL_LIGHTNING_STR = 379,
    AT_SKILL_EXPANSION_OF_WIZARDRY_STR = 380,
    AT_SKILL_INFERNO_STR = 381,
    AT_SKILL_BLAST_STR = 382,
    AT_SKILL_EXPANSION_OF_WIZARDRY_MASTERY = 383,
    AT_SKILL_POISON_STR = 384,
    AT_SKILL_EVIL_SPIRIT_STR = 385,
    AT_SKILL_MagicMasteryGrandMaster = 386,
    AT_SKILL_DECAY_STR = 387,
    AT_SKILL_HELL_FIRE_STR = 388,
    AT_SKILL_ICE_STR = 389,
    AT_SKILL_OneHandedStaffStrengthener = 397,
    AT_SKILL_TwoHandedStaffStrengthener = 398,
    AT_SKILL_ShieldStrengthenerGrandMaster = 399,
    AT_SKILL_OneHandedStaffMaster = 400,
    AT_SKILL_TwoHandedStaffMaster = 401,
    AT_SKILL_ShieldMasteryGrandMaster = 402,
    AT_SKILL_SOUL_BARRIER_STR = 403,
    AT_SKILL_SOUL_BARRIER_PROFICIENCY = 404,
    AT_SKILL_MinimumWizardryInc = 405,
    AT_SKILL_HEALING_STR = 413,
    AT_SKILL_TRIPLE_SHOT_STR = 414,
    AT_SKILL_SummonedMonsterStr1 = 415,
    AT_SKILL_PENETRATION_STR = 416,
    AT_SKILL_DEFENSE_STR = 417,
    AT_SKILL_TRIPLE_SHOT_MASTERY = 418,
    AT_SKILL_SummonedMonsterStr2 = 419,
    AT_SKILL_ATTACK_STR = 420,
    AT_SKILL_WeaponMasteryHighElf = 421,
    AT_SKILL_ATTACK_MASTERY = 422,
    AT_SKILL_DEFENSE_MASTERY = 423,
    AT_SKILL_ICE_ARROW_STR = 424,
    AT_SKILL_BowStrengthener = 435,
    AT_SKILL_CrossbowStrengthener = 436,
    AT_SKILL_ShieldStrengthenerHighElf = 437,
    AT_SKILL_BowMastery = 438,
    AT_SKILL_CrossbowMastery = 439,
    AT_SKILL_ShieldMasteryHighElf = 440,
    AT_SKILL_INFINITY_ARROW_STR = 441,
    AT_SKILL_MinimumAttPowerInc = 442,
    AT_SKILL_FireTomeStrengthener = 448,
    AT_SKILL_WindTomeStrengthener = 449,
    AT_SKILL_LightningTomeStren = 450,
    AT_SKILL_FireTomeMastery = 451,
    AT_SKILL_WindTomeMastery = 452,
    AT_SKILL_LightningTomeMastery = 453,
    AT_SKILL_ALICE_SLEEP_STR = 454,
    AT_SKILL_ALICE_CHAINLIGHTNING_STR = 455,
    AT_SKILL_LIGHTNING_SHOCK_STR = 456,
    AT_SKILL_MagicMasterySummoner = 457,
    AT_SKILL_ALICE_DRAINLIFE_STR = 458,
    AT_SKILL_StickStrengthener = 465,
    AT_SKILL_OtherWorldTomeStreng = 466,
    AT_SKILL_StickMastery = 467,
    AT_SKILL_OtherWorldTomeMastery = 468,
    AT_SKILL_ALICE_BERSERKER_STR = 469,
    AT_SKILL_BerserkerProficiency = 470,
    AT_SKILL_MinimumWizCurseInc = 471,
    AT_SKILL_CYCLONE_STR_MG = 479,
    AT_SKILL_LIGHTNING_STR_MG = 480,
    AT_SKILL_TWISTING_SLASH_STR_MG = 481,
    AT_SKILL_POWER_SLASH_STR = 482,
    AT_SKILL_FLAME_STR_MG = 483,
    AT_SKILL_BLAST_STR_MG = 484,
    AT_SKILL_WeaponMasteryDuelMaster = 485,
    AT_SKILL_INFERNO_STR_MG = 486,
    AT_SKILL_EVIL_SPIRIT_STR_MG = 487,
    AT_SKILL_MagicMasteryDuelMaster = 488,
    AT_SKILL_ICE_STR_MG = 489,
    AT_SKILL_FIRE_SLASH_STR = 490,
    AT_SKILL_FIREBURST_STR = 508,
    AT_SKILL_FORCE_WAVE_STR = 509,
    AT_SKILL_DarkHorseStreng1 = 510,
    AT_SKILL_ADD_CRITICAL_STR1 = 511,
    AT_SKILL_EARTHSHAKE_STR = 512,
    AT_SKILL_WeaponMasteryLordEmperor = 513,
    AT_SKILL_FIREBURST_MASTERY = 514,
    AT_SKILL_ADD_CRITICAL_STR2 = 515,
    AT_SKILL_EARTHSHAKE_MASTERY = 516,
    AT_SKILL_ADD_CRITICAL_STR3 = 517,
    AT_SKILL_FIRE_SCREAM_STR = 518,
    AT_SKILL_DarkSpiritStr = 526,
    AT_SKILL_ScepterStrengthener = 527,
    AT_SKILL_ShieldStrengthenerLordEmperor = 528,
    AT_SKILL_UseScepterPetStr = 529,
    AT_SKILL_DarkSpiritStr2 = 530,
    AT_SKILL_ScepterMastery = 531,
    AT_SKILL_ShieldMastery = 532,
    AT_SKILL_CommandAttackInc = 533,
    AT_SKILL_DarkSpiritStr3 = 534,
    AT_SKILL_PetDurabilityStr = 535,
    AT_SKILL_KILLING_BLOW_STR = 551,
    AT_SKILL_BEAST_UPPERCUT_STR = 552,
    AT_SKILL_KILLING_BLOW_MASTERY = 554,
    AT_SKILL_BEAST_UPPERCUT_MASTERY = 555,
    AT_SKILL_WeaponMasteryFistMaster = 557,
    AT_SKILL_CHAIN_DRIVE_STR = 558,
    AT_SKILL_DARKSIDE_STR = 559,
    AT_SKILL_DRAGON_ROAR_STR = 560,
    AT_SKILL_EquippedWeaponStrengthener = 568,
    AT_SKILL_DEF_UP_OURFORCES_STR = 569,
    AT_SKILL_EquippedWeaponMastery = 571,
    AT_SKILL_DEF_UP_OURFORCES_MASTERY = 572,
    AT_SKILL_HP_UP_OURFORCES_STR = 573,
    AT_SKILL_DurabilityReduction1FistMaster = 578,
    AT_SKILL_IncreasePvPDefenseRate = 579,
    AT_SKILL_IncreaseMaximumSd = 580,
    AT_SKILL_IncreaseManaRecoveryRate = 581,
    AT_SKILL_IncreasePoisonResistance = 582,
    AT_SKILL_DurabilityReduction2FistMaster = 583,
    AT_SKILL_IncreaseSdRecoveryRate = 584,
    AT_SKILL_IncreaseHpRecoveryRate = 585,
    AT_SKILL_IncreaseLightningResistance = 586,
    AT_SKILL_IncreasesDefense = 587,
    AT_SKILL_IncreasesAgRecoveryRate = 588,
    AT_SKILL_IncreaseIceResistance = 589,
    AT_SKILL_DurabilityReduction3FistMaster = 590,
    AT_SKILL_IncreaseDefenseSuccessRate = 591,
    AT_SKILL_IncreaseAttackSuccessRate = 599,
    AT_SKILL_IncreaseMaximumHp = 600,
    AT_SKILL_IncreaseMaximumMana = 601,
    AT_SKILL_IncreasePvPAttackRate = 603,
    AT_SKILL_DecreaseMana = 604,
    AT_SKILL_RecoverSDfromMonsterKills = 605,
    AT_SKILL_RecoverHPfromMonsterKills = 606,
    AT_SKILL_IncreaseMinimumAttackPower = 607,
    AT_SKILL_RecoverManaMonsterKills = 608,
    AT_SKILL_MASTER_END = 608,
};

#define SKILL_FIELD_TYPE_Bool bool
#define SKILL_FIELD_TYPE_Byte BYTE
#define SKILL_FIELD_TYPE_Word WORD
#define SKILL_FIELD_TYPE_Int int
#define SKILL_FIELD_TYPE_DWord DWORD
#define g_SkillDataHandler CSkillDataHandler::GetInstance()

enum class GameDataLoadError
{
    MissingFile,
    CorruptFile,
};

class CErrorReport;

using GameDataLoadErrorReporter = void (*)(CErrorReport &errorReport, HWND window,
                                           const wchar_t *fileName, GameDataLoadError error,
                                           bool closeWindow) noexcept;

void ReportLegacyGameDataLoadError(CErrorReport &errorReport, HWND window, const wchar_t *fileName,
                                   GameDataLoadError error, bool closeWindow) noexcept;

// Forward declarations for constants
#ifndef MAX_CLASS
#define MAX_CLASS 7
#endif

#ifndef MAX_DUTY_CLASS
#define MAX_DUTY_CLASS 3
#endif

#ifndef MAX_SKILL_NAME
#define MAX_SKILL_NAME 50
#endif

// Include X-macro field definitions

// X-Macro definition for all SKILL_ATTRIBUTE fields (except Name which is special)
// Format: X(FieldName, TypeEnum, ArraySize, DefaultColumnWidth, I18nMetadataName)
// I18nMetadataName is the bare identifier inside namespace I18N::Metadata that
// holds the localized display label for this field.
// This is the SINGLE SOURCE OF TRUTH for skill field definitions.

#define SKILL_FIELDS_SIMPLE(X)                                                                     \
    X(Level, Word, 1, 60.0f, Level)                                                                \
    X(Damage, Word, 1, 70.0f, Damage)                                                              \
    X(Mana, Word, 1, 70.0f, Mana)                                                                  \
    X(AbilityGuage, Word, 1, 80.0f, AGCost)                                                        \
    X(Distance, DWord, 1, 80.0f, Range)                                                            \
    X(Delay, Int, 1, 70.0f, Cooldown)                                                              \
    X(Energy, Int, 1, 70.0f, ReqEnergy)                                                            \
    X(Charisma, Word, 1, 80.0f, ReqLeadership)                                                     \
    X(MasteryType, Byte, 1, 80.0f, MasteryType)                                                    \
    X(SkillUseType, Byte, 1, 80.0f, UseType)                                                       \
    X(SkillBrand, DWord, 1, 90.0f, Brand)                                                          \
    X(KillCount, Byte, 1, 80.0f, KillCount)

#define SKILL_FIELDS_ARRAYS(X)                                                                     \
    X(RequireDutyClass[0], RequireDutyClass, 0, Byte, 80.0f, Duty0)                                \
    X(RequireDutyClass[1], RequireDutyClass, 1, Byte, 80.0f, Duty1)                                \
    X(RequireDutyClass[2], RequireDutyClass, 2, Byte, 80.0f, Duty2)                                \
    X(RequireClass[0], RequireClass, 0, Byte, 60.0f, DWSM)                                         \
    X(RequireClass[1], RequireClass, 1, Byte, 60.0f, DKBK)                                         \
    X(RequireClass[2], RequireClass, 2, Byte, 65.0f, ELFME)                                        \
    X(RequireClass[3], RequireClass, 3, Byte, 60.0f, MGDM)                                         \
    X(RequireClass[4], RequireClass, 4, Byte, 60.0f, DLLE)                                         \
    X(RequireClass[5], RequireClass, 5, Byte, 65.0f, SUMBS)                                        \
    X(RequireClass[6], RequireClass, 6, Byte, 60.0f, RFFM)

// Helper macro to convert TypeEnum to actual C++ type (same as ItemFieldDefs.h)

// Generate struct field declarations from X-macro. The i18nName parameter
// only matters for descriptor generation; the struct layout ignores it.
#define DECLARE_SKILL_FIELD(name, type, arraySize, width, i18nName) SKILL_FIELD_TYPE_##type name;

// Fields that come after the arrays (must be in correct order for binary compatibility)
#define SKILL_FIELDS_AFTER_ARRAYS(X)                                                               \
    X(SkillRank, Byte, 1, 70.0f, Rank)                                                             \
    X(Magic_Icon, Word, 1, 80.0f, Icon)                                                            \
    X(TypeSkill, Byte, 1, 80.0f, Type)                                                             \
    X(Strength, Int, 1, 80.0f, ReqStrength)                                                        \
    X(Dexterity, Int, 1, 80.0f, ReqDexterity)                                                      \
    X(ItemSkill, Byte, 1, 80.0f, ItemSkill)                                                        \
    X(IsDamage, Byte, 1, 70.0f, IsDamage)                                                          \
    X(Effect, Word, 1, 70.0f, Effect)

// Macro to generate all fields for struct definition (excludes Name)
// IMPORTANT: Field order must match original binary format!
#define SKILL_ATTRIBUTE_FIELDS                                                                     \
    SKILL_FIELDS_SIMPLE(DECLARE_SKILL_FIELD)                                                       \
    BYTE RequireDutyClass[MAX_DUTY_CLASS];                                                         \
    BYTE RequireClass[MAX_CLASS];                                                                  \
    SKILL_FIELDS_AFTER_ARRAYS(DECLARE_SKILL_FIELD)

// Note: WCharArray for Name field is handled specially in SkillFieldMetadata.h

// Legacy file format structure (32-byte name)
// Used for backward compatibility with old BMD files
typedef struct
{
    char Name[32]; // Legacy format used 32 bytes
    SKILL_ATTRIBUTE_FIELDS
} SKILL_ATTRIBUTE_FILE_LEGACY;

// Current file format structure with MAX_SKILL_NAME byte name
// Used for reading/writing BMD files
typedef struct
{
    char Name[MAX_SKILL_NAME];
    SKILL_ATTRIBUTE_FIELDS
} SKILL_ATTRIBUTE_FILE;

// Runtime structure with wide-character name
// Used in-memory during gameplay/editor
typedef struct SKILL_ATTRIBUTE
{
    wchar_t Name[MAX_SKILL_NAME];
    SKILL_ATTRIBUTE_FIELDS
} SKILL_ATTRIBUTE;

// COPY HELPERS (mirrors ItemStructs.h pattern)

// Generate field copy statements from X-macro
#define COPY_SKILL_FIELD(name, type, arraySize, width, i18nName) (dest).name = (source).name;

// Macro to copy all non-name fields from source to dest
#define COPY_SKILL_ATTRIBUTE_FIELDS(dest, source)                                                  \
    do                                                                                             \
    {                                                                                              \
        SKILL_FIELDS_SIMPLE(COPY_SKILL_FIELD)                                                      \
        memcpy((dest).RequireDutyClass, (source).RequireDutyClass,                                 \
               sizeof((source).RequireDutyClass));                                                 \
        memcpy((dest).RequireClass, (source).RequireClass, sizeof((source).RequireClass));         \
        SKILL_FIELDS_AFTER_ARRAYS(COPY_SKILL_FIELD)                                                \
    } while (0)

// Helper template to copy from file structure to runtime structure
// Requires: #include "Data/Translation/MultiLanguage.h"
template <typename TSource>
inline void CopySkillAttributeFromSource(SKILL_ATTRIBUTE &dest, const TSource &source)
{
    CMultiLanguage::ConvertFromUtf8(dest.Name, source.Name, MAX_SKILL_NAME);
    COPY_SKILL_ATTRIBUTE_FIELDS(dest, source);
}

// Helper template to copy from runtime structure to file structure
// Requires: #include "Data/Translation/MultiLanguage.h"
template <typename TDest>
inline void CopySkillAttributeToDestination(TDest &dest, const SKILL_ATTRIBUTE &source)
{
    CMultiLanguage::ConvertToUtf8(dest.Name, source.Name, sizeof(dest.Name));
    COPY_SKILL_ATTRIBUTE_FIELDS(dest, source);
}

class CErrorReport;

// Skill Data Loading Operations
// Loads skill data from encrypted BMD files with checksum verification
// This is the single source of truth for skill data loading (used in ALL builds)
class SkillDataLoader
{
  public:
    explicit SkillDataLoader(SKILL_ATTRIBUTE *&skillAttribute) noexcept
        : SkillAttribute(skillAttribute)
    {
    }

    // Load skill data from file
    // Returns true on success, false on failure
    bool Load(wchar_t *fileName, CErrorReport &errorReport, HWND window);

  private:
    // Generic template method for loading any skill format
    template <typename TFileFormat>
    bool LoadFormat(FILE *fp, const wchar_t *formatName, CErrorReport &errorReport, HWND window);

    // Load legacy format (32-byte names)
    bool LoadLegacyFormat(FILE *fp, long fileSize, CErrorReport &errorReport, HWND window);

    // Load new format (MAX_SKILL_NAME-byte names)
    bool LoadNewFormat(FILE *fp, long fileSize, CErrorReport &errorReport, HWND window);

    SKILL_ATTRIBUTE *&SkillAttribute;
};

class ApplicationKeeper;
class Application;
struct ApplicationGameDataStorage;
class GameDataTestPeer;

class GameData final : protected ApplicationLegacyCalls
{
  public:
    GameData(ApplicationKeeper &keeper, GameDataLoadErrorReporter loadErrorReporter) noexcept;
    ~GameData() noexcept;

    GameData(const GameData &) = delete;
    GameData &operator=(const GameData &) = delete;
    GameData(GameData &&) = delete;
    GameData &operator=(GameData &&) = delete;

    bool IsLoaded() const noexcept;

  private:
    friend class ApplicationLegacyCalls;
    friend class ApplicationSupportCalls;
    friend class Application;
    friend class GameDataTestPeer;

    bool SelectLanguage(const std::wstring &assetLanguage) noexcept;
    const std::wstring &AssetLanguage() const noexcept;
    bool Load();
    void Shutdown() noexcept;

    void OpenFilterFile(const wchar_t *fileName);
    void OpenNameFilterFile(const wchar_t *fileName);
    void OpenGateScript(const wchar_t *fileName);
    void OpenMonsterSkillScript(const wchar_t *fileName);
    void OpenMonsterScript(wchar_t *fileName);
    void CreateClassAttribute(int characterClass, int strength, int dexterity, int vitality,
                              int energy, int life, int mana, int levelLife, int levelMana,
                              int vitalityToLife, int energyToMana);
    void CreateClassAttributes();
    bool LoadItemDataFile(wchar_t *fileName, CErrorReport &errorReport, HWND window);
    bool LoadSkillDataFile(wchar_t *fileName, CErrorReport &errorReport, HWND window);
    void RecordLoadFailure() noexcept;
    void ReportLoadError(const wchar_t *fileName, GameDataLoadError error,
                         bool closeWindow) noexcept;
    void ResetStorage() noexcept;

    ApplicationGameDataStorage &storage_;
    CErrorReport &errorReport_;
    HWND &g_hWnd;
    GameDataLoadErrorReporter loadErrorReporter_;
    ItemDataLoader itemDataLoader_;
    SkillDataLoader skillDataLoader_;
    const bool registered_;
    mutable std::mutex loadMutex_;
    bool loading_ = false;
    bool loaded_ = false;
    bool loadFailed_ = false;
};

/**
 * Field metadata system - replaces X-macros with simple, readable arrays
 *
 * This is MUCH easier to understand and maintain than X-macros!
 */

enum class FieldType
{
    Bool,
    Byte,
    Word,
    Int,
    DWord,
    Float
};

struct FieldInfo
{
    const char *name; // Field name for display
    FieldType type;   // Field type
    size_t offset;    // Offset in struct (use offsetof)
    size_t size;      // Size of field (use sizeof)
    int arrayIndex;   // -1 for simple fields, >= 0 for array elements
};

/**
 * Compare a single field between two structs
 *
 * Uses field metadata to determine how to compare and format the output.
 * No macros needed - just a simple function!
 */
template <typename T>
inline void CompareFieldByMetadata(const FieldInfo &field, const T &oldStruct, const T &newStruct,
                                   std::stringstream &changes, bool &hasChanged)
{
    // Get pointers to the actual field data
    const BYTE *oldPtr = (const BYTE *)&oldStruct + field.offset;
    const BYTE *newPtr = (const BYTE *)&newStruct + field.offset;

    bool fieldChanged = false;
    std::stringstream fieldChange;

    switch (field.type)
    {
    case FieldType::Bool: {
        bool oldVal, newVal;
        memcpy(&oldVal, oldPtr, sizeof(bool));
        memcpy(&newVal, newPtr, sizeof(bool));
        if (oldVal != newVal)
        {
            fieldChanged = true;
            fieldChange << "  " << field.name << ": " << (oldVal ? "true" : "false") << " -> "
                        << (newVal ? "true" : "false") << "\n";
        }
        break;
    }

    case FieldType::Byte: {
        BYTE oldVal, newVal;
        memcpy(&oldVal, oldPtr, sizeof(BYTE));
        memcpy(&newVal, newPtr, sizeof(BYTE));
        if (oldVal != newVal)
        {
            fieldChanged = true;
            fieldChange << "  " << field.name << ": " << (int)oldVal << " -> " << (int)newVal
                        << "\n";
        }
        break;
    }

    case FieldType::Word: {
        WORD oldVal, newVal;
        memcpy(&oldVal, oldPtr, sizeof(WORD));
        memcpy(&newVal, newPtr, sizeof(WORD));
        if (oldVal != newVal)
        {
            fieldChanged = true;
            fieldChange << "  " << field.name << ": " << oldVal << " -> " << newVal << "\n";
        }
        break;
    }

    case FieldType::Int: {
        int oldVal, newVal;
        memcpy(&oldVal, oldPtr, sizeof(int));
        memcpy(&newVal, newPtr, sizeof(int));
        if (oldVal != newVal)
        {
            fieldChanged = true;
            fieldChange << "  " << field.name << ": " << oldVal << " -> " << newVal << "\n";
        }
        break;
    }

    case FieldType::DWord: {
        DWORD oldVal, newVal;
        memcpy(&oldVal, oldPtr, sizeof(DWORD));
        memcpy(&newVal, newPtr, sizeof(DWORD));
        if (oldVal != newVal)
        {
            fieldChanged = true;
            fieldChange << "  " << field.name << ": " << oldVal << " -> " << newVal << "\n";
        }
        break;
    }

    case FieldType::Float: {
        float oldVal, newVal;
        memcpy(&oldVal, oldPtr, sizeof(float));
        memcpy(&newVal, newPtr, sizeof(float));
        if (oldVal != newVal)
        {
            fieldChanged = true;
            fieldChange << "  " << field.name << ": " << oldVal << " -> " << newVal << "\n";
        }
        break;
    }
    }

    if (fieldChanged)
    {
        changes << fieldChange.str();
        hasChanged = true;
    }
}

/**
 * Compare all fields using metadata array
 * Simple loop - no macros!
 */
template <typename T>
inline void CompareAllFieldsByMetadata(const T &oldStruct, const T &newStruct,
                                       std::span<const FieldInfo> fields,
                                       std::stringstream &changes, bool &hasChanged)
{
    for (const auto &field : fields)
    {
        CompareFieldByMetadata(field, oldStruct, newStruct, changes, hasChanged);
    }
}

class CErrorReport;

// Skill Data Handler - Singleton Facade
// Provides centralized access to skill data operations
class CSkillDataHandler
{
  public:
    static CSkillDataHandler &GetInstance();

    // Data Operations - delegates to specialized classes
    bool Load(wchar_t *fileName, CErrorReport &errorReport, HWND window);

    // Data Access
    SKILL_ATTRIBUTE *GetSkillAttributes();
    SKILL_ATTRIBUTE *GetSkillAttribute(int index);
    int GetSkillCount() const;

  private:
    CSkillDataHandler();
    ~CSkillDataHandler() = default;

    // Prevent copying
    CSkillDataHandler(const CSkillDataHandler &) = delete;
    CSkillDataHandler &operator=(const CSkillDataHandler &) = delete;
};

namespace info
{
enum InfoTextType
{
    eInfo_Text_File = 0,
    eInfo_Item_File,
    eInfo_Skill_File,
    eInfo_Slide_File,
    eInfo_Dialog_File,
    eInfo_Credit_File,
    eInfo_Filter_File,
    eInfo_FilterName_File,
    eInfo_MonsterSkill_File,
    eInfo_Movereq_File,
    eInfo_Quest_File,
    eInfo_ItemSetType_File,
    eInfo_ItemSetOption_File,
    eInfo_NpcName_File,
    eInfo_JewelOfHarmonyOption_File,
    eInfo_JewelOfHarmonySmelt_File,
    eInfo_ItemAddOption_File,
    eInfo_File_Count,
};
}; // namespace info

namespace GameDataDecodeDetail
{
enum class GameDataToken
{
    Name,
    Number,
    End,
    Command,
    LeftBracket,
    RightBracket,
    Comma,
    Semicolon,
    Error,
};

class GameDataScriptParser final
{
  public:
    explicit GameDataScriptParser(FILE &file) noexcept : file_(file)
    {
    }

    GameDataToken Next() noexcept
    {
        tokenString_[0] = '\0';

        int ch = 0;
        do
        {
            ch = fgetc(&file_);
            if (ch == EOF)
            {
                return GameDataToken::End;
            }

            if (ch == '/')
            {
                const int next = fgetc(&file_);
                if (next == '/')
                {
                    do
                    {
                        ch = fgetc(&file_);
                    } while (ch != '\n' && ch != EOF);

                    if (ch == EOF)
                    {
                        return GameDataToken::End;
                    }
                }
                else if (next != EOF)
                {
                    ungetc(next, &file_);
                }
            }
        } while (isspace(static_cast<unsigned char>(ch)) != 0);

        switch (ch)
        {
        case '#':
            return GameDataToken::Command;
        case ';':
            return GameDataToken::Semicolon;
        case ',':
            return GameDataToken::Comma;
        case '{':
            return GameDataToken::LeftBracket;
        case '}':
            return GameDataToken::RightBracket;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case '.':
        case '-':
            ungetc(ch, &file_);
            return ReadNumber();
        case '"':
            return ReadQuotedName();
        default:
            if (isalpha(static_cast<unsigned char>(ch)) != 0)
            {
                return ReadName(ch);
            }
            return GameDataToken::Error;
        }
    }

    float Number() const noexcept
    {
        return tokenNumber_;
    }

    const char *String() const noexcept
    {
        return tokenString_;
    }

  private:
    GameDataToken ReadNumber() noexcept
    {
        char text[100]{};
        std::size_t length = 0;
        int ch = fgetc(&file_);
        while (ch != EOF &&
               (ch == '.' || ch == '-' || isdigit(static_cast<unsigned char>(ch)) != 0))
        {
            if (length + 1 < sizeof(text))
            {
                text[length++] = static_cast<char>(ch);
            }
            ch = fgetc(&file_);
        }
        if (ch != EOF)
        {
            ungetc(ch, &file_);
        }
        tokenNumber_ = static_cast<float>(atof(text));
        return GameDataToken::Number;
    }

    GameDataToken ReadQuotedName() noexcept
    {
        std::size_t length = 0;
        int ch = fgetc(&file_);
        while (ch != EOF && ch != '"')
        {
            if (length + 1 < sizeof(tokenString_))
            {
                tokenString_[length++] = static_cast<char>(ch);
            }
            ch = fgetc(&file_);
        }
        tokenString_[length] = '\0';
        return GameDataToken::Name;
    }

    GameDataToken ReadName(int first) noexcept
    {
        std::size_t length = 0;
        tokenString_[length++] = static_cast<char>(first);
        int ch = fgetc(&file_);
        while (ch != EOF &&
               (ch == '.' || ch == '_' || isalnum(static_cast<unsigned char>(ch)) != 0))
        {
            if (length + 1 < sizeof(tokenString_))
            {
                tokenString_[length++] = static_cast<char>(ch);
            }
            ch = fgetc(&file_);
        }
        if (ch != EOF)
        {
            ungetc(ch, &file_);
        }
        tokenString_[length] = '\0';
        return GameDataToken::Name;
    }

    FILE &file_;
    float tokenNumber_ = 0.0f;
    char tokenString_[256]{};
};
} // namespace GameDataDecodeDetail
