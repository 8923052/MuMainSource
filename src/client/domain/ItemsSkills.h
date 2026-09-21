#pragma once
#include "data/CharacterData.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/WorldData.h"
#include "domain/EffectsUpdate.h"
#include "domain/WorldSimulation.h"
#include "render/FrameTape.h"
#include "render/ModelResources.h"
#include "render/Text.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

struct CharacterAfterImagePose;

const std::map<ActionSkillType, ActionSkillType> SKILL_REPLACEMENTS = {
    {AT_SKILL_POISON_STR, AT_SKILL_POISON},
    {AT_SKILL_LIGHTNING_STR, AT_SKILL_LIGHTNING},
    {AT_SKILL_LIGHTNING_STR_MG, AT_SKILL_LIGHTNING},
    {AT_SKILL_FLAME_STR, AT_SKILL_FLAME},
    {AT_SKILL_FLAME_STR_MG, AT_SKILL_FLAME},
    {AT_SKILL_ICE_STR, AT_SKILL_ICE},
    {AT_SKILL_ICE_STR_MG, AT_SKILL_ICE},
    {AT_SKILL_EVIL_SPIRIT_STR, AT_SKILL_EVIL_SPIRIT},
    {AT_SKILL_EVIL_SPIRIT_STR_MG, AT_SKILL_EVIL_SPIRIT},
    {AT_SKILL_HELL_FIRE_STR, AT_SKILL_HELL_FIRE},
    {AT_SKILL_BLAST_STR, AT_SKILL_BLAST},
    {AT_SKILL_BLAST_STR_MG, AT_SKILL_BLAST},
    {AT_SKILL_INFERNO_STR, AT_SKILL_INFERNO},
    {AT_SKILL_INFERNO_STR_MG, AT_SKILL_INFERNO},
    {AT_SKILL_SOUL_BARRIER_STR, AT_SKILL_SOUL_BARRIER},
    {AT_SKILL_SOUL_BARRIER_PROFICIENCY, AT_SKILL_SOUL_BARRIER_STR},
    {AT_SKILL_FALLING_SLASH_STR, AT_SKILL_FALLING_SLASH},
    {AT_SKILL_LUNGE_STR, AT_SKILL_LUNGE},
    {AT_SKILL_CYCLONE_STR, AT_SKILL_CYCLONE},
    {AT_SKILL_CYCLONE_STR_MG, AT_SKILL_CYCLONE},
    {AT_SKILL_SLASH_STR, AT_SKILL_SLASH},
    {AT_SKILL_TRIPLE_SHOT_STR, AT_SKILL_TRIPLE_SHOT},
    {AT_SKILL_TRIPLE_SHOT_MASTERY, AT_SKILL_TRIPLE_SHOT_STR},
    {AT_SKILL_HEALING_STR, AT_SKILL_HEALING},
    {AT_SKILL_DEFENSE_STR, AT_SKILL_DEFENSE},
    {AT_SKILL_DEFENSE_MASTERY, AT_SKILL_DEFENSE_STR},
    {AT_SKILL_ATTACK_STR, AT_SKILL_ATTACK},
    {AT_SKILL_ATTACK_MASTERY, AT_SKILL_ATTACK_STR},
    {AT_SKILL_DECAY_STR, AT_SKILL_DECAY},
    {AT_SKILL_TWISTING_SLASH_STR, AT_SKILL_TWISTING_SLASH},
    {AT_SKILL_TWISTING_SLASH_STR_MG, AT_SKILL_TWISTING_SLASH},
    {AT_SKILL_TWISTING_SLASH_MASTERY, AT_SKILL_TWISTING_SLASH_STR},
    {AT_SKILL_RAGEFUL_BLOW_STR, AT_SKILL_RAGEFUL_BLOW},
    {AT_SKILL_RAGEFUL_BLOW_MASTERY, AT_SKILL_RAGEFUL_BLOW_STR},
    {AT_SKILL_DEATHSTAB_STR, AT_SKILL_DEATHSTAB},
    {AT_SKILL_SWELL_LIFE_STR, AT_SKILL_SWELL_LIFE},
    {AT_SKILL_SWELL_LIFE_PROFICIENCY, AT_SKILL_SWELL_LIFE_STR},
    {AT_SKILL_ICE_ARROW_STR, AT_SKILL_ICE_ARROW},
    {AT_SKILL_PENETRATION_STR, AT_SKILL_PENETRATION},
    {AT_SKILL_FIRE_SLASH_STR, AT_SKILL_FIRE_SLASH},
    {AT_SKILL_POWER_SLASH_STR, AT_SKILL_POWER_SLASH},
    {AT_SKILL_FORCE_WAVE_STR, AT_SKILL_FORCE_WAVE},
    {AT_SKILL_FIREBURST_STR, AT_SKILL_FIREBURST},
    {AT_SKILL_FIREBURST_MASTERY, AT_SKILL_FIREBURST_STR},
    {AT_SKILL_EARTHSHAKE_STR, AT_SKILL_EARTHSHAKE},
    {AT_SKILL_EARTHSHAKE_MASTERY, AT_SKILL_EARTHSHAKE_STR},
    {AT_SKILL_ADD_CRITICAL_STR1, AT_SKILL_ADD_CRITICAL},
    {AT_SKILL_ADD_CRITICAL_STR2, AT_SKILL_ADD_CRITICAL_STR1},
    {AT_SKILL_ADD_CRITICAL_STR3, AT_SKILL_ADD_CRITICAL_STR2},
    {AT_SKILL_INFINITY_ARROW_STR, AT_SKILL_INFINITY_ARROW},
    {AT_SKILL_FIRE_SCREAM_STR, AT_SKILL_FIRE_SCREAM},
    {AT_SKILL_ALICE_DRAINLIFE_STR, AT_SKILL_ALICE_DRAINLIFE},
    {AT_SKILL_ALICE_CHAINLIGHTNING_STR, AT_SKILL_ALICE_CHAINLIGHTNING},
    {AT_SKILL_ALICE_BERSERKER_STR, AT_SKILL_ALICE_BERSERKER},
    {AT_SKILL_ALICE_SLEEP_STR, AT_SKILL_ALICE_SLEEP},
    {AT_SKILL_LIGHTNING_SHOCK_STR, AT_SKILL_LIGHTNING_SHOCK},
    {AT_SKILL_STRIKE_OF_DESTRUCTION_STR, AT_SKILL_STRIKE_OF_DESTRUCTION},
    {AT_SKILL_EXPANSION_OF_WIZARDRY_MASTERY, AT_SKILL_EXPANSION_OF_WIZARDRY_STR},
    {AT_SKILL_EXPANSION_OF_WIZARDRY_STR, AT_SKILL_EXPANSION_OF_WIZARDRY},
    {AT_SKILL_KILLING_BLOW_STR, AT_SKILL_KILLING_BLOW},
    {AT_SKILL_KILLING_BLOW_MASTERY, AT_SKILL_KILLING_BLOW_STR},
    {AT_SKILL_BEAST_UPPERCUT_STR, AT_SKILL_BEAST_UPPERCUT},
    {AT_SKILL_BEAST_UPPERCUT_MASTERY, AT_SKILL_BEAST_UPPERCUT_STR},
    {AT_SKILL_CHAIN_DRIVE_STR, AT_SKILL_CHAIN_DRIVE},
    {AT_SKILL_DARKSIDE_STR, AT_SKILL_DARKSIDE},
    {AT_SKILL_DRAGON_ROAR_STR, AT_SKILL_DRAGON_ROAR},
    {AT_SKILL_HP_UP_OURFORCES_STR, AT_SKILL_HP_UP_OURFORCES},
    {AT_SKILL_DEF_UP_OURFORCES_STR, AT_SKILL_DEF_UP_OURFORCES},
    {AT_SKILL_DEF_UP_OURFORCES_MASTERY, AT_SKILL_DEF_UP_OURFORCES_STR},
};
#define MAX_BUFF_NAME_LENGTH 50
#define MAX_DESCRIPT_LENGTH 100
#define g_ChangeRingMgr (&g_ChangeRingManager)
#define MAXHARMONYJEWELOPTIONTYPE 3
#define MAXHARMONYJEWELOPTIONINDEX 10
#define CHAOS_MIX_LEVEL 10
#define SKILL_SWORD 0
#define SKILL_MACE 2
#define SKILL_BOW 3
#define SKILL_SPEAR 4
#define SKILL_STAFF 5
#define SKILL_CRY 8
#define SKILL_INVINCIBILITY 9
#define SKILL_DUAL_WEAPON 10
#define SKILL_BASH 11
#define SKILL_HEALING 12
#define SKILL_SACRIFICE 13
#define SKILL_STEALTH 16
#define SKILL_TRAP 17
#define SKILL_JAP 19
#define SKILL_TELEPORT 20
#define SKILL_DECOY 21
#define SKILL_MULTIPLE_SHOT 24
#define SKILL_CHARM 25
#define SKILL_CONFUSE 26
#define SKILL_CHANGE_SHAPE 27
#define SKILL_INFRAVISION 28
#define SKILL_PROTECTION 29
#define SKILL_FIRE 32
#define SKILL_COLD 33
#define SKILL_MANA 35
#define SKILL_ANTIDOTE 36
#define SKILL_REFLECTION 37
#define TRADELIMITLEVEL (6)

class CErrorReport;
class SessionKeeper;

struct _BUFFINFO
{
    short s_BuffIndex;
    BYTE s_BuffEffectType;
    BYTE s_ItemType;
    BYTE s_ItemIndex;
    char s_BuffName[MAX_BUFF_NAME_LENGTH];
    BYTE s_BuffClassType;
    BYTE s_NoticeType;
    BYTE s_ClearType;
    char s_BuffDescript[MAX_DESCRIPT_LENGTH];
};

class BuffInfo
{
  public:
    BuffInfo();
    virtual ~BuffInfo();

  public:
    short s_BuffIndex;
    BYTE s_BuffEffectType;
    BYTE s_ItemType;
    BYTE s_ItemIndex;
    wchar_t s_BuffName[MAX_BUFF_NAME_LENGTH + 1];
    BYTE s_BuffClassType;
    BYTE s_NoticeType;
    BYTE s_ClearType;
    wchar_t s_BuffDescript[MAX_DESCRIPT_LENGTH + 1];
    std::list<std::wstring> s_BuffDescriptlist;
};

SmartPointer(BuffScriptLoader);
class BuffScriptLoader : protected SessionLegacyCalls
{
  public:
    static BuffScriptLoaderPtr Make(SessionKeeper &keeper);
    virtual ~BuffScriptLoader();

  private:
    bool Load(const std::wstring &pchFileName);

  public:
    const BuffInfo GetBuffinfo(eBuffState type) const;
    eBuffClass IsBuffClass(eBuffState type) const;

#ifdef KJH_PBG_ADD_INGAMESHOP_SYSTEM
    int GetBuffIndex(int iItemCode);
    int GetBuffType(int iItemCode);
#endif // KJH_PBG_ADD_INGAMESHOP_SYSTEM

  private:
    explicit BuffScriptLoader(SessionKeeper &keeper);

  private:
    typedef std::map<eBuffState, BuffInfo> BuffInfoMap;

  private:
    std::wstring &g_strSelectedML;
    CErrorReport &g_ErrorReport;
    HWND &g_hWnd;
    BuffInfoMap m_Info;
};

class SessionKeeper;

SmartPointer(BuffStateValueControl);

class BuffStateValueControl : protected SessionLegacyCalls
{
  private:
    struct BuffStateValueInfo
    {
        DWORD s_Value1;
        DWORD s_Value2;
        DWORD s_Time;

        BuffStateValueInfo() : s_Value1(0), s_Value2(0), s_Time(0)
        {
        }
    };

  public:
    static BuffStateValueControlPtr Make(SessionKeeper &keeper);
    virtual ~BuffStateValueControl();

  public:
    const BuffStateValueInfo GetValue(eBuffState bufftype);
    void GetBuffInfoString(std::list<std::wstring> &outstr, eBuffState bufftype);
    void GetBuffValueString(std::wstring &outstr, eBuffState bufftype);
    eBuffValueLoadType CheckValue(eBuffState bufftype);

  private:
    void SetValue(eBuffState bufftype, BuffStateValueInfo &valueinfo);
    void Initialize();
    void Destroy();
    explicit BuffStateValueControl(SessionKeeper &keeper);

  private:
    typedef std::map<eBuffState, BuffStateValueInfo> BuffStateValueMap;

  private:
    BuffStateValueMap m_BuffStateValue;
};

class CmuConsoleDebug;
class SessionKeeper;

SmartPointer(BuffTimeControl);

class BuffTimeControl : protected SessionLegacyCalls
{
  public:
    static BuffTimeControlPtr Make(SessionKeeper &keeper);
    virtual ~BuffTimeControl();

  public:
    void RegisterBuffTime(eBuffState bufftype, DWORD curbufftime);
    bool UnRegisterBuffTime(eBuffState bufftype);
    bool IsBuffTime(eBuffTimeType bufftype);

  public:
    void GetBuffStringTime(eBuffState bufftype, std::wstring &timeText);
    void GetBuffStringTime(DWORD type, std::wstring &timeText, bool issecond = true);
    const DWORD GetBuffTime(DWORD type);
    void GetStringTime(DWORD time, std::wstring &timeText, bool isSecond = true);

  private:
    eBuffTimeType CheckBuffTimeType(eBuffState bufftype);
    DWORD GetBuffEventTime(eBuffTimeType bufftimetype);
    DWORD GetBuffMaxTime(eBuffState bufftype, DWORD curbufftime = 0);
    bool CheckBuffTime(DWORD type);
    explicit BuffTimeControl(SessionKeeper &keeper);

  private:
    struct BuffTimeInfo
    {
        eBuffState s_BuffType;
        DWORD s_CurBuffTime;
        DWORD s_EventBuffTime;

        BuffTimeInfo() : s_BuffType(eBuffNone), s_CurBuffTime(0), s_EventBuffTime(0)
        {
        }
    };
    typedef std::map<DWORD, BuffTimeInfo> BuffTimeInfoMap;

  private:
    BuffTimeInfoMap m_BuffTimeList;
    CmuConsoleDebug &g_ConsoleDebug;
};

class Buff;
class SessionKeeper;

SmartPointer(BuffStateSystem);
class BuffStateSystem : protected SessionLegacyCalls
{
  public:
    explicit BuffStateSystem(SessionKeeper &keeper) noexcept;
    virtual ~BuffStateSystem();

  public:
    BuffScriptLoader &GetBuffInfo();
    BuffTimeControl &GetBuffTimeControl();
    BuffStateValueControl &GetBuffStateValueControl();
    bool IsEqualBuffType(Buff &buff, IN int iBuffType, OUT wchar_t *szBuffName);

  private:
    friend class SessionKeeper;
    friend class SessionLegacyCalls;

    bool Initialize();
    void Destroy();
    BuffStateSystem &TheBuffStateSystem();
    BuffScriptLoader &TheBuffInfo();
    BuffTimeControl &TheBuffTimeControl();
    BuffStateValueControl &TheBuffStateValueControl();

  private:
    BuffScriptLoaderPtr m_BuffInfo;
    BuffTimeControlPtr m_BuffTimeControl;
    BuffStateValueControlPtr m_BuffStateValueControl;
};

inline BuffScriptLoader &BuffStateSystem::GetBuffInfo()
{
    assert(m_BuffInfo);
    return *m_BuffInfo;
}

inline BuffTimeControl &BuffStateSystem::GetBuffTimeControl()
{
    assert(m_BuffTimeControl);
    return *m_BuffTimeControl;
}

inline BuffStateValueControl &BuffStateSystem::GetBuffStateValueControl()
{
    assert(m_BuffStateValueControl);
    return *m_BuffStateValueControl;
}

class CErrorReport;
class CHARACTER_MACHINE;

constexpr std::uint8_t MAX_SET_OPTION = 64; // Maximum number of possible ancient sets
constexpr std::uint8_t MASTERY_OPTION = 24; // A mastery option increases a specific skill

// Each item can be part of up to 2 ancient sets. The following are their identifier.
constexpr std::uint8_t EXT_A_SET_OPTION = 1;
constexpr std::uint8_t EXT_B_SET_OPTION = 2;

// Maximum number of equipped sets. This is the maximum number of sets that can be equipped at the same time.
// It's 5, because the number of equippable items is 12, but one is a wing and one is a pet. To make any set, you need at least 2 items.
constexpr std::uint8_t MAX_EQUIPPED_SET_ITEMS = MAX_EQUIPMENT_INDEX - 2;
constexpr std::uint8_t MAX_EQUIPPED_SETS = MAX_EQUIPPED_SET_ITEMS / 2;

struct ITEM_SET_TYPE
{
    std::array<std::uint8_t, MAX_ITEM_SETS_PER_ITEM> byOption{};
    std::array<std::uint8_t, MAX_ITEM_SETS_PER_ITEM> byMixItemLevel{};
};

struct ITEM_SET_OPTION
{
    wchar_t strSetName[MAX_ITEM_SET_NAME];
    std::uint8_t bySetItemCount; // The number of items in the set
    std::uint8_t byOptionCount;  // The total number of options in the set

    // The following arrays are used to store the standard options of the ancient sets.
    // There is typically one option less than the number of items in the set.
    // These 2-element-arrays are somehow strange. Only the first element is actually used, I found no use case for the second one.
    // Theoretically Webzen planned to have 2 options per item, but never implemented it.
    std::array<std::array<std::uint8_t, MAX_ITEM_SET_STANDARD_OPTION_PER_ITEM_COUNT>,
               MAX_ITEM_SET_STANDARD_OPTION_COUNT>
        byStandardOption{};
    std::array<std::array<std::uint8_t, MAX_ITEM_SET_STANDARD_OPTION_PER_ITEM_COUNT>,
               MAX_ITEM_SET_STANDARD_OPTION_COUNT>
        byStandardOptionValue{};

    // There are up to 2 ext options which always apply when there are at least two items of a set equipped.
    // Found no set which uses an ExtOption.
    std::array<std::uint8_t, MAX_ITEM_SET_EXT_OPTION_COUNT> byExtOption{};
    std::array<std::uint8_t, MAX_ITEM_SET_EXT_OPTION_COUNT> byExtOptionValue{};

    std::array<std::uint8_t, MAX_ITEM_SET_FULL_OPTION_COUNT> byFullOption{};
    std::array<std::uint8_t, MAX_ITEM_SET_FULL_OPTION_COUNT> byFullOptionValue{};
    std::array<std::uint8_t, MAX_CLASS> byRequireClass{};
};

struct SET_OPTION
{
    bool IsActive;
    bool IsFullOption;
    bool IsExtOption;
    bool
        FulfillsClassRequirement; // If the option requires a specific class and the character fulfills that. //m_bySetOptionList[x][1]
    std::uint8_t OptionNumber;    // m_bySetOptionList[x][0]
    int Value;                    //m_iSetOptionListValue
};

struct SET_SEARCH_RESULT
{
    std::uint8_t SetNumber;
    std::uint8_t CompleteSetItemCount;
    std::uint8_t ItemCount;
    std::uint8_t SetTypeIndex;
    wchar_t SetName[MAX_ITEM_SET_NAME];
};

struct SET_SEARCH_RESULT_OPT : SET_SEARCH_RESULT
{
    int SetOptionCount;
    SET_OPTION SetOption[MAX_OPTIONS_PER_ITEM_SET];
};

class CSItemOption : protected SessionLegacyCalls
{
  private:
    std::wstring &g_strSelectedML;
    CErrorReport &g_ErrorReport;
    ITEM_SET_TYPE m_ItemSetType[MAX_ITEM];
    ITEM_SET_OPTION m_ItemSetOption[MAX_SET_OPTION];

    int m_SetSearchResultCount;
    SET_SEARCH_RESULT_OPT m_SetSearchResult[MAX_EQUIPPED_SETS];

    bool m_bViewOptionList;
    std::uint8_t m_byRenderOptionList;
    std::uint8_t m_bySelectedItemOption;
    std::uint8_t m_bySameSetItem;

    typedef std::map<int, std::wstring> MAP_EQUIPPEDSETITEMNAME;
    typedef std::map<std::uint8_t, int> MAP_EQUIPPEDSETITEM_SEQUENCE;

    MAP_EQUIPPEDSETITEMNAME m_mapEquippedSetItemName;
    MAP_EQUIPPEDSETITEMNAME::iterator m_iterESIN;

    static bool isClassRequirementFulfilled(const ITEM_SET_OPTION &setOptions, int firstClass,
                                            int secondClass);
    static void TryAddSetOption(std::uint8_t option, int value, int optionIndex,
                                SET_SEARCH_RESULT_OPT &set, const ITEM_SET_OPTION &setOptions,
                                bool isThisSetComplete, bool isFullOption, bool isExtOption,
                                bool fulfillsClassRequirement, int firstClass, int secondClass);

    static bool getExplainText(wchar_t *text, std::uint8_t option, int value);
    std::uint8_t RenderSetOptionList(const SET_SEARCH_RESULT_OPT &set, std::uint8_t textIndex,
                                     bool bIsEquippedItem, bool bShowInactive);

    std::wstring OpenItemSetType(const wchar_t *filename);
    std::wstring OpenItemSetOption(const wchar_t *filename);
    void checkItemType(SET_SEARCH_RESULT *optionList, const int iType,
                       const int ancientDiscriminator) const;
    void calcSetOptionList(const SET_SEARCH_RESULT *optionList);

    void getAllAddState(std::uint16_t *Strength, std::uint16_t *Dexterity, std::uint16_t *Energy,
                        std::uint16_t *Vitality, std::uint16_t *Charisma) const;

    void AddStatsBySetOptions(std::uint16_t *Strength, std::uint16_t *Dexterity,
                              std::uint16_t *Energy, std::uint16_t *Vitality,
                              std::uint16_t *Charisma)
        const; //Adds the stats of the active ancient set options to the given pointers, without bonus options

    int AggregateOptionValue(int optionNumber) const;

  public:
    explicit CSItemOption(SessionKeeper &keeper) noexcept;
    ~CSItemOption(void) {};
    void BindCharacterState(CHARACTER_MACHINE &characterMachine) noexcept;

    void init(void)
    {
        m_bViewOptionList = false;
        m_byRenderOptionList = 0;
        m_bySelectedItemOption = 0;
        m_bySameSetItem = 0;
    }
    std::wstring OpenItemSetScript();

    std::uint8_t IsChangeSetItem(const int Type, const int SubType = -1);
    std::uint16_t GetMixItemLevel(const int Type) const;
    bool GetSetItemName(wchar_t *strName, const int iType, const int setType) const;

    void PlusSpecial(std::uint16_t *Value, int Special) const;
    void PlusSpecialPercent(std::uint16_t *Value, int Special) const;
    void PlusSpecialLevel(std::uint16_t *Value, std::uint16_t SrcValue, int Special) const;
    void PlusMastery(int *Value, std::uint8_t MasteryType) const;

    int GetDefaultOptionValue(ITEM *ip, std::uint16_t *Value) const;
    bool GetDefaultOptionText(const ITEM *ip, wchar_t *Text) const;
    int RenderDefaultOptionText(const ITEM *ip, int TextNum);

    bool IsNonWeaponSkillOrIsSkillEquipped(ActionSkillType skill) const;
    void CheckItemSetOptions(void);
    void MoveSetOptionList(const int StartX, const int StartY);
    bool BuildSetOptionList(std::uint8_t &textCount);

    int RenderSetOptionListInItem(const ITEM *ip, int TextNum, bool bIsEquippedItem = false);

    void ClearOptionHelper(void)
    {
        m_byRenderOptionList = 0;
    }
    void CheckRenderOptionHelper(const wchar_t *FilterName);
    bool BuildOptionHelper(std::uint8_t &textCount);
    std::uint8_t OptionHelperSelection() const
    {
        return m_byRenderOptionList;
    }

    bool IsAncientSetEquipped() const
    {
        return m_SetSearchResultCount > 0;
    }

    void SetViewOptionList(bool bView);
    bool IsViewOptionList();

    void getAllAddOptionStatesbyCompare(std::uint16_t *Strength, std::uint16_t *Dexterity,
                                        std::uint16_t *Energy, std::uint16_t *Vitality,
                                        std::uint16_t *Charisma, std::uint16_t iCompareStrength,
                                        std::uint16_t iCompareDexterity,
                                        std::uint16_t iCompareEnergy,
                                        std::uint16_t iCompareVitality, std::uint16_t iC);

    void getAllAddStateOnlyAddValue(std::uint16_t *AddStrength, std::uint16_t *AddDexterity,
                                    std::uint16_t *AddEnergy, std::uint16_t *AddVitality,
                                    std::uint16_t *AddCharisma)
        const; // Gets only the added stats of the active ancient set options plus bonus options

  private:
    CHARACTER_MACHINE *CharacterMachine = nullptr;
    CHARACTER_ATTRIBUTE *CharacterAttribute = nullptr;
};

class CChangeRingManager
{
  public:
    constexpr CChangeRingManager() noexcept = default;
    bool CheckDarkLordHair(int iType) const;
    bool CheckDarkCloak(CLASS_TYPE iClass, int iType) const;
    bool CheckChangeRing(short RingType) const;
    bool CheckRepair(int iType) const;
    bool CheckMoveMap(short sLeftRingType, short sRightRingType) const;
    bool CheckBanMoveIcarusMap(short sLeftRingType, short sRightRingType) const;
};

inline constexpr CChangeRingManager g_ChangeRingManager;

namespace SEASON3B
{

enum REPAIR_MODE
{
    REPAIR_MODE_OFF = 0,
    REPAIR_MODE_ON,
};

class IInventoryActionContext
{
  public:
    virtual ~IInventoryActionContext() = default;

    virtual REPAIR_MODE GetRepairMode() const = 0;
    virtual bool IsEquipable(int iIndex, ITEM *pItem) const = 0;
    virtual void ResetMouseRButton() = 0;
    virtual void ResetMouseLButton() = 0;
    virtual int FindEmptySlot(ITEM *pItem) const = 0;
    virtual bool IsRepairEnableLevel() const = 0;
};

} // namespace SEASON3B

// Logical containers are distinct even when the wire protocol uses UNDEFINED.
enum class InventoryRole : std::size_t
{
    Player,
    PlayerExtension0,
    PlayerExtension1,
    PlayerExtension2,
    PlayerExtension3,
    Vault,
    VaultExtension,
    TradeOwn,
    TradeOther,
    Crafting,
    LuckyCrafting,
    MyShop,
    OtherShop,
    NpcShop,
    Count
};

class SessionItemStore;
namespace SEASON4A
{
class CSocketItemMgr;
}

class InventoryGrid final : protected SessionLegacyCalls
{
  public:
    InventoryGrid(SessionKeeper &keeper, SessionItemStore &store, InventoryRole role);
    ~InventoryGrid();
    InventoryGrid(const InventoryGrid &) = delete;
    InventoryGrid &operator=(const InventoryGrid &) = delete;

    InventoryRole Role() const noexcept
    {
        return role_;
    }
    STORAGE_TYPE GetStorageType() const noexcept
    {
        return storageType_;
    }
    void SetStorageType(STORAGE_TYPE type) noexcept
    {
        storageType_ = type;
    }
    int GetNumberOfColumn() const noexcept
    {
        return columns_;
    }
    int GetNumberOfRow() const noexcept
    {
        return rows_;
    }
    int IndexOffset() const noexcept
    {
        return offset_;
    }
    int GetIndex(int column, int row) const noexcept
    {
        return offset_ + row * columns_ + column;
    }
    std::span<ITEM *const> Items() const noexcept
    {
        return items_;
    }
    DWORD SlotKey(int slot) const noexcept
    {
        return cells_[slot] ? cells_[slot]->Key : 0;
    }
    size_t GetNumberOfItems() const noexcept
    {
        return items_.size();
    }
    ITEM *GetItem(int index) const;
    ITEM *FindItem(int slot) const;
    ITEM *FindItem(int column, int row) const;
    ITEM *FindItemByKey(DWORD key) const;
    ITEM *FindTypeItem(short type) const;
    bool IsItem(short type) const
    {
        return FindTypeItem(type) != nullptr;
    }
    int GetItemCount(short type, int level = -1) const;
    int FindItemIndex(short type, int level = -1) const;
    int FindItemReverseIndex(short type, int level = -1) const;
    int GetIndexByItem(ITEM *item) const
    {
        return item ? GetIndex(item->x, item->y) : -1;
    }
    short FindItemTypeByPos(int column, int row) const;
    int GetNumItemByKey(DWORD key) const;
    int GetNumItemByType(short type) const;
    int GetEmptySlotCount() const;
    int FindEmptySlot(int width, int height) const;
    bool FindEmptySlot(int width, int height, int &column, int &row) const;
    bool CanMove(int slot, ITEM *item) const;
    bool CanMove(int column, int row, ITEM *item) const;
    bool AddItem(int slot, std::span<const BYTE> packet);
    bool AddItem(int column, int row, std::span<const BYTE> packet);
    bool AddItem(int column, int row, ITEM *item);
    bool AddItem(int column, int row, BYTE type, BYTE subtype, BYTE level = 0,
                 BYTE durability = 255, BYTE option = 0, BYTE ancient = 0, BYTE guardian = 0,
                 BYTE harmony = 0);
    void RemoveItem(ITEM *item);
    bool RemoveItemAt(int slot);
    void RemoveAllItems();
    bool AreItemsStackable(ITEM *source, ITEM *target);
    bool CanUpgradeItem(ITEM *source, ITEM *target);
    bool CanPreviewDrop(ITEM *source, ITEM *target);

  private:
    bool CheckSlot(int column, int row, int width, int height) const;
    bool InsertOwned(int column, int row, ITEM *item);
    void FillItemCells(ITEM &item, ITEM *value);
    SessionItemStore &store_;
    SEASON4A::CSocketItemMgr &g_SocketItemMgr;
    InventoryRole role_;
    STORAGE_TYPE storageType_ = STORAGE_TYPE::UNDEFINED;
    int columns_ = COLUMN_INVENTORY;
    int rows_ = ROW_INVENTORY_EXT;
    int offset_ = 0;
    std::vector<ITEM *> items_;
    std::vector<ITEM *> cells_;
};

inline bool IsMainInventorySlot(const int slot)
{
    return slot >= MAX_EQUIPMENT_INDEX && slot < MAX_MY_INVENTORY_INDEX;
}

inline bool IsInventoryExtensionSlot(const int slot)
{
    return slot >= MAX_MY_INVENTORY_INDEX && slot < MAX_MY_INVENTORY_EX_INDEX;
}

inline bool IsMyShopSlot(const int slot)
{
    return slot >= MAX_MY_INVENTORY_EX_INDEX && slot < MAX_MY_SHOP_INVENTORY_INDEX;
}

inline bool IsPlayerInventorySlot(const int slot)
{
    return IsMainInventorySlot(slot) || IsInventoryExtensionSlot(slot);
}

class CErrorReport;
class SessionKeeper;

typedef struct _ITEM_ADD_OPTION
{
    BYTE m_byOption1;
    WORD m_byValue1;

    BYTE m_byOption2;
    WORD m_byValue2;

    BYTE m_Type;
    DWORD m_Time;

    _ITEM_ADD_OPTION()
    {
        m_byOption1 = 0;
        m_byValue1 = 0;

        m_byOption2 = 0;
        m_byValue2 = 0;

        m_Type = 0;
        m_Time = 0;
    }
} ITEM_ADD_OPTION;

typedef struct _ITEM_ADD_OPTION_RESULT
{
    int m_Width;
    int m_Height;
    int m_Count;
    int m_Type;

    _ITEM_ADD_OPTION_RESULT()
    {
        m_Width = 0;
        m_Height = 0;
        m_Count = 0;
        m_Type = 0;
    }
    bool IsSuccess()
    {
        if (m_Width * m_Height == m_Count && m_Count != 0)
        {
            return true;
        }
        return false;
    }
} ITEM_ADD_OPTION_RESULT;

class ItemAddOptioninfo : protected SessionLegacyCalls
{
  public:
    static ItemAddOptioninfo *MakeInfo(SessionKeeper &keeper);
    virtual ~ItemAddOptioninfo();

  public:
    void GetItemAddOtioninfoText(std::vector<std::wstring> &outtextlist, int type);

  public: //inline
    const ITEM_ADD_OPTION &GetItemAddOtioninfo(int type);

  private:
    const bool OpenItemAddOptionInfoFile(const std::wstring &filename);
    explicit ItemAddOptioninfo(SessionKeeper &keeper);

  private:
    CErrorReport &g_ErrorReport;
    HWND &g_hWnd;
    ITEM_ADD_OPTION m_ItemAddOption[MAX_ITEM];
    ITEM_ADD_OPTION m_Temp;
};

inline const ITEM_ADD_OPTION &ItemAddOptioninfo::GetItemAddOtioninfo(int type)
{
    return m_ItemAddOption[type];
}

struct ItemCreationParams
{
    int Group;
    int Number;
    BYTE Level;
    BYTE Durability;
    bool WithLuck;
    bool WithSkill;

    bool WithOption;
    BYTE OptionLevel;
    BYTE OptionType;

    bool HasExcellentOption;
    BYTE ExcellentFlags;

    bool IsAncient;
    BYTE AncientDiscriminator;
    BYTE AncientBonusOption;

    bool HasHarmonyOption;
    BYTE HarmonyOptionType;
    BYTE HarmonyOptionLevel;

    bool HasGuardianOption;
    BYTE SocketCount;
    BYTE SocketOptions[MAX_SOCKETS];
    BYTE SocketBonusOption;

    bool WithExpiration;
    bool IsExpired;
};

namespace GameLogic::Items
{
std::size_t ItemPacketLength(std::span<const BYTE> packet);
std::optional<ItemCreationParams> ParseItemData(std::span<const BYTE> itemData);
} // namespace GameLogic::Items

class CErrorReport;

namespace SEASON3A
{
class CMixRecipeMgr;

class CMixItem
{
  public:
    CMixItem()
    {
        Reset();
    }
    virtual ~CMixItem()
    {
    }
    CMixItem(ITEM *pItem, int iMixValue)
    {
        SetItem(pItem, iMixValue);
    }

    void Reset();
    void SetItem(ITEM *pItem, DWORD dwMixValue);

    bool IsSameItem(const CMixItem &rhs) const
    {
        return (m_sType == rhs.m_sType && m_iLevel == rhs.m_iLevel &&
                (m_bCanStack || m_iDurability == rhs.m_iDurability) && m_iOption == rhs.m_iOption &&
                m_dwSpecialItem == rhs.m_dwSpecialItem);
    }

    bool operator==(ITEM *rhs) const
    {
        return IsSameItem(CMixItem(rhs, 0));
    }

    short m_sType;
    int m_iLevel;
    int m_iOption;
    int m_iDurability;
    DWORD m_dwSpecialItem;
    int m_iCount;
    int m_iTestCount;
    BOOL m_bMixLuck;
    BOOL m_bIsEquipment;
    BOOL m_bCanStack;
    BOOL m_bIsWing;
    BOOL m_bIsUpgradedWing;
    BOOL m_bIs3rdUpgradedWing;
    DWORD m_dwMixValue;
    BOOL m_bIsCharmItem;
    BOOL m_bIsChaosCharmItem;
    BOOL m_bIsJewelItem;
    BOOL m_b380AddedItem;
    BOOL m_bFenrirAddedItem;
    WORD m_wHarmonyOption;
    WORD m_wHarmonyOptionLevel;
    BYTE m_bySocketCount;
    BYTE m_bySocketSeedID[MAX_SOCKETS];
    BYTE m_bySocketSphereLv[MAX_SOCKETS];
    BYTE m_bySeedSphereID;
};

class CMixItemInventory : protected SessionLegacyCalls
{
  public:
    explicit CMixItemInventory(SessionKeeper &keeper) : SessionLegacyCalls(keeper)
    {
        Reset();
    }
    virtual ~CMixItemInventory()
    {
        Reset();
    }
    void Reset()
    {
        m_iNumMixItems = 0;
    }

    int AddItem(ITEM *pItem);
    int GetNumMixItems()
    {
        return m_iNumMixItems;
    }
    CMixItem *GetMixItems()
    {
        return m_MixItems;
    }

  protected:
    DWORD EvaluateMixItemValue(ITEM *pItem);

  protected:
    CMixItem m_MixItems[32];
    int m_iNumMixItems;
};

enum _SPECIAL_ITEM_RECIPE_
{
    RCP_SP_EXCELLENT = 1,
    RCP_SP_ADD380ITEM = 2,
    RCP_SP_SETITEM = 4,
    RCP_SP_HARMONY = 8,
    RCP_SP_SOCKETITEM = 16,
};

typedef struct _MIX_RECIPE_ITEM
{
    short m_sTypeMin;
    short m_sTypeMax;
    int m_iLevelMin;
    int m_iLevelMax;
    int m_iOptionMin;
    int m_iOptionMax;
    int m_iDurabilityMin;
    int m_iDurabilityMax;
    int m_iCountMin;
    int m_iCountMax;
    DWORD m_dwSpecialItem;
} MIX_RECIPE_ITEM;

enum _MIX_TYPES_
{
    MIXTYPE_GOBLIN_NORMAL,
    MIXTYPE_GOBLIN_CHAOSITEM,
    MIXTYPE_GOBLIN_ADD380,
    MIXTYPE_CASTLE_SENIOR,
    MIXTYPE_TRAINER,
    MIXTYPE_OSBOURNE,
    MIXTYPE_JERRIDON,
    MIXTYPE_ELPIS,
    MIXTYPE_CHAOS_CARD,
    MIXTYPE_CHERRYBLOSSOM,
    MIXTYPE_EXTRACT_SEED,
    MIXTYPE_SEED_SPHERE,
    MIXTYPE_ATTACH_SOCKET,
    MIXTYPE_DETACH_SOCKET,
    MAX_MIX_TYPES
};
#define MAX_MIX_SOURCES 8
#define MAX_MIX_NAMES 3
#define MAX_MIX_DESCRIPTIONS 3
#define MAX_MIX_RATE_TOKEN 32

enum _MIXRATE_OPS
{
    MRCP_NUMBER = 0,
    MRCP_ADD, // +
    MRCP_SUB, // -
    MRCP_MUL, // *
    MRCP_DIV, // /
    MRCP_LP,  // (
    MRCP_RP,  // )
    MRCP_INT, // INT()
    MRCP_MAXRATE = 32,
    MRCP_ITEM,
    MRCP_WING,
    MRCP_EXCELLENT,
    MRCP_EQUIP,
    MRCP_SET,
    MRCP_LEVEL1,
    MRCP_NONJEWELITEM,
    MRCP_LUCKOPT = 64
};

typedef struct _MIXRATE_TOKEN
{
    enum _MIXRATE_OPS op;
    float value;
} MIXRATE_TOKEN;

typedef struct _MIX_RECIPE
{
    int m_iMixIndex;
    int m_iMixID;
    int m_iMixName[MAX_MIX_NAMES];
    int m_iMixDesc[MAX_MIX_DESCRIPTIONS];
    int m_iMixAdvice[MAX_MIX_DESCRIPTIONS];
    int m_iWidth;
    int m_iHeight;
    int m_iRequiredLevel;
    BYTE m_bRequiredZenType;
    DWORD m_dwRequiredZen;
    int m_iNumRateData;
    MIXRATE_TOKEN m_RateToken[MAX_MIX_RATE_TOKEN];
    int m_iSuccessRate;
    BYTE m_bMixOption;
    BYTE m_bCharmOption;
    BYTE m_bChaosCharmOption;
    MIX_RECIPE_ITEM m_MixSources[MAX_MIX_SOURCES];
    int m_iNumMixSoruces;
} MIX_RECIPE;

enum _MIX_SOURCE_STATUS
{
    MIX_SOURCE_ERROR,
    MIX_SOURCE_NO,
    MIX_SOURCE_PARTIALLY,
    MIX_SOURCE_YES
};

class CMixRecipes
{
    friend class CMixRecipeMgr;

  public:
    CMixRecipes()
    {
        Reset();
    }
    virtual ~CMixRecipes()
    {
        Reset();
    }
    void BindOwner(CMixRecipeMgr &owner) noexcept
    {
        owner_ = &owner;
    }

    void Reset();
    void AddRecipe(MIX_RECIPE *pMixRecipe);
    void ClearCheckRecipeResult();
    int CheckRecipe(int iNumMixItems, CMixItem *pMixItems);
    int CheckRecipeSimilarity(int iNumMixItems, CMixItem *pMixItems);

    BOOL IsMixSource(ITEM *pItem);
    MIX_RECIPE *GetCurRecipe();
    int GetCurMixID();
    BOOL GetCurRecipeName(wchar_t *pszNameOut, int iNameLine);
    BOOL GetCurRecipeDesc(wchar_t *pszDescOut, int iDescLine);
    MIX_RECIPE *GetMostSimilarRecipe();
    BOOL GetMostSimilarRecipeName(wchar_t *pszNameOut, int iNameLine);
    BOOL GetRecipeAdvice(wchar_t *pszAdviceOut, int iAdivceLine);
    int GetSourceName(int itemNumber, wchar_t *name, int itemCount, CMixItem *items);
    BOOL IsReadyToMix()
    {
        return (m_iCurMixIndex > 0);
    }
    int GetSuccessRate()
    {
        return m_iSuccessRate;
    }
    DWORD GetReqiredZen()
    {
        return m_dwRequiredZen;
    }
    int GetFirstItemSocketCount()
    {
        return m_byFirstItemSocketCount;
    }
    int GetFirstItemSocketSeedID(int iIndex)
    {
        if (iIndex >= m_byFirstItemSocketCount)
            return SOCKET_EMPTY;
        else
            return m_byFirstItemSocketSeedID[iIndex];
    }
    int GetFirstItemSocketShpereLv(int iIndex)
    {
        if (iIndex >= m_byFirstItemSocketCount)
            return 0;
        else
            return m_byFirstItemSocketSphereLv[iIndex];
    }
    void CalcCharmBonusRate(int iNumMixItems, CMixItem *pMixItems);
    void CalcChaosCharmCount(int iNumMixItems, CMixItem *pMixItems);

#ifdef LJH_MOD_CANNOT_USE_CHARMITEM_AND_CHAOSCHARMITEM_SIMULTANEOUSLY
    WORD GetTotalChaosCharmCount()
    {
        return m_wTotalChaosCharmCount;
    }
    WORD GetTotalCharmCount()
    {
        return m_wTotalCharmBonus;
    }
#endif //LJH_MOD_CANNOT_USE_CHARMITEM_AND_CHAOSCHARMITEM_SIMULTANEOUSLY

  protected:
    bool IsOptionItem(MIX_RECIPE_ITEM &rItem)
    {
        return (rItem.m_iCountMin == 0);
    } // 옵션(안넣어도 되는) 아이템인가
    BOOL CheckRecipeSub(std::vector<MIX_RECIPE *>::iterator iter, int iNumMixItems,
                        CMixItem *pMixItems);
    int CheckRecipeSimilaritySub(std::vector<MIX_RECIPE *>::iterator iter, int iNumMixItems,
                                 CMixItem *pMixItems);         // 유사도 비교
    bool CheckItem(MIX_RECIPE_ITEM &rItem, CMixItem &rSource); // 같은 아이템인지 비교
    void EvaluateMixItems(int iNumMixItems, CMixItem *pMixItems);
    void CalcMixRate(int iNumMixItems, CMixItem *pMixItems);
    void CalcMixReqZen(int iNumMixItems, CMixItem *pMixItems);
    BOOL GetRecipeName(MIX_RECIPE *pRecipe, wchar_t *pszNameOut, int iNameLine,
                       BOOL bSimilarRecipe); // 주어진 조합법의 이름 얻기
    BOOL IsChaosItem(CMixItem &rSource);
    BOOL IsChaosJewel(CMixItem &rSource);
    BOOL Is380AddedItem(CMixItem &rSource);
    BOOL IsFenrirAddedItem(CMixItem &rSource);
    BOOL IsUpgradableItem(CMixItem &rSource);
    BOOL IsSourceOfRefiningStone(CMixItem &rSource);
    BOOL IsCharmItem(CMixItem &rSource);
    BOOL IsChaosCharmItem(CMixItem &rSource);
    BOOL IsJewelItem(CMixItem &rSource);
    BOOL IsSourceOfAttachSeedSphereToWeapon(CMixItem &rSource);
    BOOL IsSourceOfAttachSeedSphereToArmor(CMixItem &rSource);

    float MixrateAddSub();
    float MixrateMulDiv();
    float MixrateFactor();

  protected:
    CMixRecipeMgr *owner_ = nullptr;
    std::vector<MIX_RECIPE *> m_Recipes;
    int m_iCurMixIndex;
    int m_iMostSimilarMixIndex;
    int m_iSuccessRate;
    DWORD m_dwRequiredZen;
    BOOL m_bFindMixLuckItem;
    DWORD m_dwTotalItemValue;
    DWORD m_dwExcellentItemValue;
    DWORD m_dwEquipmentItemValue;
    DWORD m_dwWingItemValue;
    DWORD m_dwSetItemValue;
    DWORD m_iFirstItemLevel;
    int m_iFirstItemType;
    DWORD m_dwTotalNonJewelItemValue;
    BYTE m_byFirstItemSocketCount;
    BYTE m_byFirstItemSocketSeedID[MAX_SOCKETS];
    BYTE m_byFirstItemSocketSphereLv[MAX_SOCKETS];
    WORD m_wTotalCharmBonus;
    WORD m_wTotalChaosCharmCount;
    int m_iMixSourceTest[MAX_MIX_SOURCES];
    int m_iMostSimilarMixSourceTest[MAX_MIX_SOURCES];
    int m_iMixRateIter;
    MIXRATE_TOKEN *m_pMixRates;
};

class CMixRecipeMgr : protected SessionLegacyCalls
{
    friend class CMixRecipes;

  public:
    explicit CMixRecipeMgr(SessionKeeper &keeper);
    virtual ~CMixRecipeMgr()
    {
    }

    void Initialize();

    void SetMixType(int iMixType)
    {
        this->m_iMixType = iMixType;
    }
    int GetMixInventoryType();
    STORAGE_TYPE GetMixInventoryEquipmentIndex();
    void ResetMixItemInventory();
    void AddItemToMixItemInventory(ITEM *pItem);
    void CheckMixInventory();
    void SetMixSubType(int iMixSubType)
    {
        this->m_iMixSubType = iMixSubType;
    }
    int GetMixSubType()
    {
        return this->m_iMixSubType;
    }

    BOOL IsMixSource(ITEM *pItem)
    {
        m_MixRecipe[GetMixInventoryType()].CalcCharmBonusRate(m_MixItemInventory.GetNumMixItems(),
                                                              m_MixItemInventory.GetMixItems());
        m_MixRecipe[GetMixInventoryType()].CalcChaosCharmCount(m_MixItemInventory.GetNumMixItems(),
                                                               m_MixItemInventory.GetMixItems());
        return m_MixRecipe[GetMixInventoryType()].IsMixSource(pItem);
    }
    int CheckRecipe(int iNumMixItems, CMixItem *pMixItems)
    {
        return m_MixRecipe[GetMixInventoryType()].CheckRecipe(iNumMixItems, pMixItems);
    }
    int CheckRecipeSimilarity(int iNumMixItems, CMixItem *pMixItems)
    {
        return m_MixRecipe[GetMixInventoryType()].CheckRecipeSimilarity(iNumMixItems, pMixItems);
    }
    BOOL IsReadyToMix()
    {
        return m_MixRecipe[GetMixInventoryType()].IsReadyToMix();
    }
    MIX_RECIPE *GetCurRecipe()
    {
        return m_MixRecipe[GetMixInventoryType()].GetCurRecipe();
    }
    int GetSuccessRate()
    {
        return m_MixRecipe[GetMixInventoryType()].GetSuccessRate();
    }
    int GetReqiredZen()
    {
        return m_MixRecipe[GetMixInventoryType()].GetReqiredZen();
    }
    int GetCurMixID()
    {
        return m_MixRecipe[GetMixInventoryType()].GetCurMixID();
    }
    BOOL GetCurRecipeName(wchar_t *pszNameOut, int iNameLine)
    {
        return m_MixRecipe[GetMixInventoryType()].GetCurRecipeName(pszNameOut, iNameLine);
    }
    BOOL GetCurRecipeDesc(wchar_t *pszDescOut, int iDescLine)
    {
        return m_MixRecipe[GetMixInventoryType()].GetCurRecipeDesc(pszDescOut, iDescLine);
    }
    MIX_RECIPE *GetMostSimilarRecipe()
    {
        return m_MixRecipe[GetMixInventoryType()].GetMostSimilarRecipe();
    }
    BOOL GetMostSimilarRecipeName(wchar_t *pszNameOut, int iNameLine)
    {
        return m_MixRecipe[GetMixInventoryType()].GetMostSimilarRecipeName(pszNameOut, iNameLine);
    }
    BOOL GetRecipeAdvice(wchar_t *pszAdviceOut, int iAdivceLine)
    {
        return m_MixRecipe[GetMixInventoryType()].GetRecipeAdvice(pszAdviceOut, iAdivceLine);
    }
    int GetSourceName(int iItemNum, wchar_t *pszNameOut)
    {
        return m_MixRecipe[GetMixInventoryType()].GetSourceName(iItemNum, pszNameOut,
                                                                m_MixItemInventory.GetNumMixItems(),
                                                                m_MixItemInventory.GetMixItems());
    }
    void ClearCheckRecipeResult()
    {
        m_MixRecipe[GetMixInventoryType()].ClearCheckRecipeResult();
        SetMixType(0);
    }
    BOOL IsMixInit()
    {
        return m_bIsMixInit;
    }

    void SetPlusChaosRate(BYTE btPlusChaosRate)
    {
        m_btPlusChaosRate = btPlusChaosRate;
    }
    BYTE GetPlusChaosRate()
    {
        return m_btPlusChaosRate;
    }
    int GetFirstItemSocketCount()
    {
        return m_MixRecipe[GetMixInventoryType()].GetFirstItemSocketCount();
    }
    int GetFirstItemSocketSeedID(int iIndex)
    {
        if (iIndex >= GetFirstItemSocketCount())
            return SOCKET_EMPTY;
        else
            return m_MixRecipe[GetMixInventoryType()].GetFirstItemSocketSeedID(iIndex);
    }
    int GetFirstItemSocketShpereLv(int iIndex)
    {
        if (iIndex >= GetFirstItemSocketCount())
            return 0;
        else
            return m_MixRecipe[GetMixInventoryType()].GetFirstItemSocketShpereLv(iIndex);
    }
    int GetSeedSphereID(int iOrder);

#ifdef LJH_MOD_CANNOT_USE_CHARMITEM_AND_CHAOSCHARMITEM_SIMULTANEOUSLY
    WORD GetTotalChaosCharmCount()
    {
        m_MixRecipe[GetMixInventoryType()].CalcChaosCharmCount(m_MixItemInventory.GetNumMixItems(),
                                                               m_MixItemInventory.GetMixItems());
        return m_MixRecipe[GetMixInventoryType()].GetTotalChaosCharmCount();
    }
    WORD GetTotalCharmCount()
    {
        m_MixRecipe[GetMixInventoryType()].CalcCharmBonusRate(m_MixItemInventory.GetNumMixItems(),
                                                              m_MixItemInventory.GetMixItems());
        return m_MixRecipe[GetMixInventoryType()].GetTotalCharmCount();
    }
#endif //LJH_MOD_CANNOT_USE_CHARMITEM_AND_CHAOSCHARMITEM_SIMULTANEOUSLY

  protected:
    void OpenRecipeFile(const wchar_t *szFileName); // mix.bmd

    ItemAddOptioninfo *ItemAddOptionInfoObject() const noexcept
    {
        return g_pItemAddOptioninfo;
    }
    JewelHarmonyInfo *JewelHarmonyInfoObject() const noexcept
    {
        return g_pUIJewelHarmonyinfo;
    }
    int GetMixSourceName(CMixRecipes &recipes, int itemNumber, wchar_t *nameOut, int mixItemCount,
                         CMixItem *mixItems) const;

  protected:
    CErrorReport &g_ErrorReport;
    HWND &g_hWnd;
    CMixRecipes m_MixRecipe[MAX_MIX_TYPES];
    CMixItemInventory m_MixItemInventory;
    int m_iMixType;
    int m_iMixSubType;
    BOOL m_bIsMixInit;
    BYTE m_btPlusChaosRate;
};
} // namespace SEASON3A

class InventoryGrid;
struct tagITEM;

struct PickedInventoryItem final
{
    tagITEM *item = nullptr;
    InventoryGrid *source = nullptr;
    STORAGE_TYPE storage = STORAGE_TYPE::UNDEFINED;
    int sourceSlot = -1;
    tagITEM *GetItem() const noexcept
    {
        return item;
    }
    int GetSourceLinealPos() const noexcept
    {
        return sourceSlot;
    }
    STORAGE_TYPE GetSourceStorageType() const noexcept
    {
        return storage;
    }
};

// A collector retains one admitted drop, even when the viewport reuses its slot.

class SessionKeeper;

class CSkillEffectMgr : protected SessionLegacyCalls
{
  public:
    explicit CSkillEffectMgr(SessionKeeper &keeper);
    virtual ~CSkillEffectMgr();

    int GetSize() const
    {
        return m_SkillEffects.Capacity();
    }
    SessionEffectPool<OBJECT> &Storage() noexcept
    {
        return m_SkillEffects;
    }
    const SessionEffectPool<OBJECT> &Storage() const noexcept
    {
        return m_SkillEffects;
    }
    OBJECT *GetEffect(int iIndex);

    BOOL IsSkillEffect(int Type, vec3_t Position, vec3_t Angle, vec3_t Light, int SubType = 0,
                       OBJECT *Target = NULL, short PKKey = -1, WORD SkillIndex = 0, WORD Skill = 0,
                       WORD SkillSerialNum = 0, float Scale = 0.0f, int sTargetIndex = -1);
    OBJECT *CreateEffect();

    bool DeleteEffect(int Type, OBJECT *Owner, int iSubType = -1);
    void DeleteEffect(int efftype);
    void DeleteAllEffects();

    bool SearchEffect(int iType, OBJECT *pOwner, int iSubType = -1);
    BOOL FindSameEffectOfSameOwner(int iType, OBJECT *pOwner);

    void MoveEffects();

  protected:
    SessionEffectPool<OBJECT> m_SkillEffects;
};

class CMapManager;
class SessionKeeper;

typedef struct DemendConditionInfo
{
    WORD SkillType;
    wchar_t SkillName[100];
    WORD SkillLevel;
    WORD SkillStrength;
    WORD SkillDexterity;
    WORD SkillVitality;
    WORD SkillEnergy;
    WORD SkillCharisma;

    DemendConditionInfo()
        : SkillType(0), SkillLevel(0), SkillStrength(0), SkillDexterity(0), SkillVitality(0),
          SkillEnergy(0), SkillCharisma(0)
    {
        ZeroMemory(SkillName, 100);
    }
    BOOL operator<=(const DemendConditionInfo &rhs) const
    {
        return SkillLevel <= rhs.SkillLevel && SkillStrength <= rhs.SkillStrength &&
               SkillDexterity <= rhs.SkillDexterity && SkillVitality <= rhs.SkillVitality &&
               SkillEnergy <= rhs.SkillEnergy && SkillCharisma <= rhs.SkillCharisma;
    }
} DemendConditionInfo;

class CSkillManager : protected SessionLegacyCalls
{
  public:
    explicit CSkillManager(SessionKeeper &keeper);
    virtual ~CSkillManager();
    bool FindHeroSkill(ActionSkillType eSkillType);
    int GetSkillIndex(int type) const;
    WORD GetHeroPriorSkill() const noexcept
    {
        return priorSkill_;
    }
    void SetHeroPriorSkill(WORD skill) noexcept
    {
        priorSkill_ = skill;
    }
    void SelectHeroSkill(int slot);
    void GetSkillInformation(int iType, int iLevel, wchar_t *lpszName, int *piMana, int *piDistance,
                             int *piSkillMana = NULL);
    void GetSkillInformation_Energy(int iType, int *piEnergy);
    void GetSkillInformation_Charisma(int iType, int *piCharisma);
    float GetSkillDistance(int Index, CHARACTER *c = NULL);
    void GetSkillInformation_Damage(int iType, int *piDamage);
    bool CheckSkillDelay(int SkillIndex);
    void CalcSkillDelay(int time);
    BYTE GetSkillMasteryType(ActionSkillType iType);
    static ActionSkillType MasterSkillToBaseSkillIndex(ActionSkillType masterSkill);
    bool skillVScharactorCheck(const DemendConditionInfo &basicInfo,
                               const DemendConditionInfo &heroInfo);
    bool AreSkillAttributeRequirementsMet(ActionSkillType skilltype);

    void InvalidateSkillAttributeRequirementsCache();
    void InitializeSkillAttributeRequirementsCache();

  private:
    WORD priorSkill_ = 0;
    void RebuildSkillAttributeRequirementsCache();

    CMapManager &gMapManager;
    DemendConditionInfo m_cachedHeroRequirements;
    bool m_cachedEmpireGuardian;
    bool m_bSkillAttributeRequirementsCacheDirty;
    bool m_aSkillAttributeRequirementsMet[MAX_SKILLS];
};

class CErrorReport;
class SessionKeeper;

//#define IS_BUTTON_SORT

struct HarmonyJewelOption
{
    int OptionType;
    wchar_t Name[60];
    int Minlevel;
    int HarmonyJewelLevel[14];
    int Zen[14];

    HarmonyJewelOption() : OptionType(-1), Minlevel(-1)
    {
        memset(Name, 0, sizeof(Name));
        for (int i = 0; i < 14; ++i)
        {
            HarmonyJewelLevel[i] = -1;
            Zen[i] = -1;
        }
    }
};

struct NaturalAbility
{
    int SI_force;
    int SI_activity;
    NaturalAbility() : SI_force(0), SI_activity(0)
    {
    }
};

struct StrikingPower
{
    int SI_minattackpower;
    int SI_maxattackpower;
    int SI_magicalpower;
    int SI_attackpowerRate;
    int SI_skillattackpower;

    StrikingPower()
        : SI_minattackpower(0), SI_maxattackpower(0), SI_magicalpower(0), SI_attackpowerRate(0),
          SI_skillattackpower(0)
    {
    }
};

struct StrengthenDefense
{
    int SI_defense;
    int SI_AG;
    int SI_HP;
    int SI_defenseRate;

    StrengthenDefense() : SI_defense(0), SI_AG(0), SI_HP(0), SI_defenseRate(0)
    {
    }
};

struct StrengthenCapability
{
    NaturalAbility SI_NB;
    bool SI_isNB;

    StrikingPower SI_SP;
    bool SI_isSP;

    StrengthenDefense SI_SD;
    bool SI_isSD;

    StrengthenCapability() : SI_isNB(false), SI_isSP(false), SI_isSD(false)
    {
    }
};

enum StrengthenItem
{
    SI_Weapon = 0,
    SI_Staff,
    SI_Defense,
    SI_None,
};

typedef HarmonyJewelOption HARMONYJEWELOPTION;

class JewelHarmonyInfo : protected SessionLegacyCalls
{
  public:
    virtual ~JewelHarmonyInfo();
    static JewelHarmonyInfo *MakeInfo(SessionKeeper &keeper);

  public:
    const StrengthenItem GetItemType(int type);
    const HARMONYJEWELOPTION &GetHarmonyJewelOptionInfo(int type, int option);
    void GetStrengthenCapability(StrengthenCapability *pitemSC, const ITEM *pitem, const int index);

  public:
    const bool IsHarmonyJewelOption(int type, int option);

  private:
    explicit JewelHarmonyInfo(SessionKeeper &keeper);
    const bool OpenJewelHarmonyInfoFile(const std::wstring &filename);

  private:
    std::wstring &g_strSelectedML;
    CErrorReport &g_ErrorReport;
    HWND &g_hWnd;
    HARMONYJEWELOPTION m_OptionData[MAXHARMONYJEWELOPTIONTYPE][MAXHARMONYJEWELOPTIONINDEX];
};

inline const HARMONYJEWELOPTION &JewelHarmonyInfo::GetHarmonyJewelOptionInfo(int type, int option)
{
    return m_OptionData[type][option - 1];
}

namespace SEASON4A
{
class CSocketItemMgr;
}

class SessionItemStore : protected SessionLegacyCalls
{
    typedef std::list<ITEM *> type_list_item;

    type_list_item m_listItem;
    DWORD m_dwAlternate, m_dwAvailableKeyStream;

  public:
    explicit SessionItemStore(SessionKeeper &keeper);
    virtual ~SessionItemStore();

    ITEM *CreateItem(std::span<const BYTE> itemData);
    ITEM *CreateItemOld(std::span<const BYTE> pbyItemPacket);
    ITEM *CreateItemExtended(std::span<const BYTE> itemData);
    ITEM *CreateItem(BYTE byType, BYTE bySubType, BYTE byLevel = 0, BYTE byDurability = 255,
                     BYTE byOption1 = 0, BYTE ancientByte = 0, BYTE byOption380 = 0,
                     BYTE byOptionHarmony = 0, BYTE *pbySocketOptions = NULL); //. create instance
    ITEM *CreateItemByParameters(const ItemCreationParams *parameters);
    ITEM *CreateItem(ITEM *pItem);    //. refer to the instance already existed
    ITEM *DuplicateItem(ITEM *pItem); //. create instance
    void DeleteItem(ITEM *pItem);
    void DeleteDuplicatedItem(ITEM *pItem);
    void DeleteAllItems();

    bool IsEmpty();

  protected:
    SEASON4A::CSocketItemMgr &g_SocketItemMgr;
    DWORD GenerateItemKey();
    DWORD FindAvailableKeyIndex(DWORD dwSeed);

    WORD ExtractItemType(std::span<const BYTE> pbyItemPacket);
    void SetItemAttr(ITEM *pItem, BYTE byLevel, BYTE byOption1, BYTE ancientDiscriminator);
};

class CErrorReport;
class SessionRenderUnit;

namespace SEASON4A
{
enum _SOCKET_OPTION_TYPE
{
    SOT_SOCKET_ITEM_OPTIONS,
    SOT_MIX_SET_BONUS_OPTIONS,
    SOT_EQUIP_SET_BONUS_OPTIONS,
    MAX_SOCKET_OPTION_TYPES
};

const int MAX_SOCKET_OPTION = 50;
const int MAX_SOCKET_OPTION_NAME_LENGTH = 64;
const int MAX_SPHERE_LEVEL = 5;
const int MAX_SOCKET_TYPES = 6;

enum _SOCKET_OPTION_CATEGORY
{
    SOC_NULL,
    SOC_IMPROVE_ATTACK,
    SOC_IMPROVE_DEFENSE,
    SOC_IMPROVE_WEAPON,
    SOC_IMPROVE_ARMOR,
    SOC_IMPROVE_BATTLE,
    SOC_IMPROVE_STATUS,
    SOC_UNIQUE_OPTION,
    MAX_SOCKET_OPTION_CATEGORY
};

enum _SOCKET_OPTIONS
{
    SOPT_ATTACK_N_MAGIC_DAMAGE_BONUS_BY_LEVEL = 0,
    SOPT_ATTACK_SPEED_BONUS,
    SOPT_ATTACT_N_MAGIC_DAMAGE_MAX_BONUS,
    SOPT_ATTACK_N_MAGIC_DAMAGE_MIN_BONUS,
    SOPT_ATTACK_N_MAGIC_DAMAGE_BONUS,
    SOPT_DECREASE_AG_USE,

    SOPT_DEFENCE_RATE_BONUS = 10,
    SOPT_DEFENCE_BONUS,
    SOPT_SHIELD_DEFENCE_BONUS,
    SOPT_DECREASE_DAMAGE,
    SOPT_REFLECT_DAMAGE,

    SOPT_MONSTER_DEATH_LIFE_BONUS = 16,
    SOPT_MONSTER_DEATH_MANA_BONUS,
    SOPT_SKILL_DAMAGE_BONUS,
    SOPT_ATTACK_RATE_BONUS,
    SOPT_INCREASE_ITEM_DURABILITY,

    SOPT_LIFE_REGENERATION_BONUS = 21,
    SOPT_MAX_LIFE_BONUS,
    SOPT_MAX_MANA_BONUS,
    SOPT_MANA_REGENERATION_BONUS,
    SOPT_MAX_AG_BONUS,
    SOPT_AG_REGENERATION_BONUS,

    SOPT_EXCELLENT_DAMAGE_BONUS = 29,
    SOPT_EXCELLENT_DAMAGE_RATE_BONUS,
    SOPT_CRITICAL_DAMAGE_BONUS,
    SOPT_CRITICAL_DAMAGE_RATE_BONUS,

    SOPT_STRENGTH_BONUS = 34,
    SOPT_DEXTERITY_BONUS,
    SOPT_VITALITY_BONUS,
    SOPT_ENERGY_BONUS,
    SOPT_REQUIRED_STENGTH_BONUS,
    SOPT_REQUIRED_DEXTERITY_BONUS,

    SOPT_UNIQUE01 = 41,
    SOPT_UNIQUE02,
};

enum _SOCKET_BONUS_OPTIONS
{
    SBOPT_ATTACK_DAMAGE_BONUS = 0,
    SBOPT_SKILL_DAMAGE_BONUS,
    SBOPT_MAGIC_POWER_BONUS,
    SBOPT_SKILL_DAMAGE_BONUS_2,
    SBOPT_DEFENCE_BONUS,
    SBOPT_MAX_LIFE_BONUS,
};

typedef struct
{
    int m_iOptionID;
    int m_iOptionCategory;
    wchar_t m_szOptionName[MAX_SOCKET_OPTION_NAME_LENGTH];
    char m_bOptionType;
    int m_iOptionValue[5];
    BYTE m_bySocketCheckInfo[6];
} SOCKET_OPTION_INFO;

typedef struct
{
    int m_iOptionID;
    int m_iOptionCategory;
    char m_szOptionName[MAX_SOCKET_OPTION_NAME_LENGTH];
    char m_bOptionType;
    int m_iOptionValue[5];
    BYTE m_bySocketCheckInfo[6];
} SOCKET_OPTION_INFO_FILE;

typedef struct _SOCKET_OPTION_STATUS_BONUS
{
    int m_iAttackDamageMinBonus;
    int m_iAttackDamageMaxBonus;
    int m_iAttackRateBonus;
    int m_iSkillAttackDamageBonus;
    int m_iAttackSpeedBonus;
#ifdef YDG_FIX_SOCKET_MISSING_MAGIC_POWER_BONUS
    int m_iMagicPowerMinBonus;
    int m_iMagicPowerMaxBonus;
#else  // YDG_FIX_SOCKET_MISSING_MAGIC_POWER_BONUS
    int m_iMagicPowerBonus;
#endif // YDG_FIX_SOCKET_MISSING_MAGIC_POWER_BONUS

    int m_iDefenceBonus;
    float m_fDefenceRateBonus;
    int m_iShieldDefenceBonus;
    int m_iStrengthBonus;
    int m_iDexterityBonus;
    int m_iVitalityBonus;
    int m_iEnergyBonus;
} SOCKET_OPTION_STATUS_BONUS;

class CSocketItemMgr : protected SessionLegacyCalls
{
  public:
    explicit CSocketItemMgr(SessionKeeper &keeper);
    virtual ~CSocketItemMgr();

    BOOL IsSocketItem(const ITEM *pItem);
    BOOL IsSocketItem(const OBJECT *pObject);
    static int GetSeedShpereSeedID(const ITEM *pItem);
    int GetSocketCategory(int iSeedID);

    int AttachToolTipForSocketItem(const ITEM *pItem, int iTextNum);
    int AttachToolTipForSeedSphereItem(const ITEM *pItem, int iTextNum);

    void CheckSocketSetOption();
    BOOL IsSocketSetOptionEnabled();

    void CreateSocketOptionText(wchar_t *pszOptionText, int iSeedID, int iSphereLv);

    __int64 CalcSocketBonusItemValue(const ITEM *pItem, __int64 iOrgGold);

    int GetSocketOptionValue(const ITEM *pItem, int iSocketIndex);

    void CalcSocketStatusBonus();
    SOCKET_OPTION_STATUS_BONUS m_StatusBonus;

    bool OpenSocketItemScript(const wchar_t *szFileName);

  protected:
    friend class ::SessionRenderUnit;
    BOOL IsSocketItem(int iItemType);

    void CalcSocketOptionValueText(wchar_t *pszOptionValueText, int iOptionType,
                                   float fOptionValue);
    int CalcSocketOptionValue(int iOptionType, float fOptionValue);

  protected:
    CErrorReport &g_ErrorReport;
    SOCKET_OPTION_INFO m_SocketOptionInfo[MAX_SOCKET_OPTION_TYPES][MAX_SOCKET_OPTION];
    int m_iNumEquitSetBonusOptions;

    std::deque<DWORD> m_EquipSetBonusList;
};
} // namespace SEASON4A

enum
{
    SKILL_ABILITY_NONE = 0,
    SKILL_ABILITY_FIRE,
    SKILL_ABILITY_THUNDER,
    SKILL_ABILITY_ICE,
    SKILL_ABILITY_POSION,
    SKILL_ABILITY_EARTH,
    SKILL_ABILITY_WIND,
    SKILL_ABILITY_WATER
};

enum
{
    SKILL_USE_TYPE_NONE = 0,
    SKILL_USE_TYPE_MASTER,       // 1
    SKILL_USE_TYPE_BRAND,        // 2
    SKILL_USE_TYPE_MASTERLEVEL,  // 3
    SKILL_USE_TYPE_MASTERACTIVE, // 4
};

namespace COMGEM
{
enum _METHOD
{
    ATTACH = 0,
    DETACH
};

enum eGEMTYPE
{
    eNOGEM = -1,
    eBLESS = 0,
    eBLESS_C,

    eSOUL,
    eSOUL_C,

    eLIFE,
    eLIFE_C,

    eCREATE,
    eCREATE_C,

    ePROTECT,
    ePROTECT_C,

    eGEMSTONE,
    eGEMSTONE_C,

    eHARMONY,
    eHARMONY_C,

    eCHAOS,
    eCHAOS_C,

    eLOW,
    eLOW_C,

    eUPPER,
    eUPPER_C,

    eGEMTYPE_END = 10,
    eGEMTYPE_END_ALL = 20,
};
enum eGEMINDEXTYPE
{
    eGEM_NAME,
    eGEM_INDEX,
    eGEM_END
};

enum _GEMTYPE
{
    NOGEM = -1,
    CELE = 0,
    SOUL = 1,
    COMCELE = 2,
    COMSOUL = 3
};

enum _COMTYPE
{
    NOCOM = -1,
    FIRST = 10,
    SECOND = 20,
    THIRD = 30,
    eCOMTYPE_END = 3
};

enum _STATE
{
    STATE_READY,
    STATE_HOLD,
    STATE_END
};

enum _ERRORTYPE
{
    NOERR = 0,
    POPERROR_NOTALLOWED,
    COMERROR_NOTALLOWED,
    DEERROR_SOMANY,
    DEERROR_NOTALLOWED,
    ERROR_UNKNOWN,
    ERROR_ALL
};
}; // namespace COMGEM

enum eTYPEBOW
{
    BOWTYPE_NONE = 0,
    BOWTYPE_BOW,
    BOWTYPE_CROSSBOW,
};

enum E_WINGMIXCHAR_SEQUENCE
{
    EWS_KNIGHT_1_CHARM = 83,
    EWS_MAGICIAN_1_CHARM,
    EWS_ELF_1_CHARM,
    EWS_SUMMONER_1_CHARM,
    EWS_DARKLORD_1_CHARM,
    EWS_KNIGHT_2_CHARM,
    EWS_MAGICIAN_2_CHARM,
    EWS_ELF_2_CHARM,
    EWS_SUMMONER_2_CHARM,
    EWS_DARKKNIGHT_2_CHARM,
    EWS_BEGIN = 83,
    EWS_END = EWS_BEGIN + 10,
};

class OBJECT;
class CHARACTER;

class SessionItemStore;

struct SessionTexturePropertiesSlot;

typedef struct Script_Skill
{
    int Skill_Num[MAX_MONSTERSKILL_NUM];
    int Slot;
} Script_Skill;

constexpr auto MAX_FENRIR_SKILL_MONSTER_NUM = 10;

/**
 * \brief Types of storage where items can be moved from/to.
 */

// Lucky Set armor constants - verified as indices 62-72 (Phoenix Soul is at 73)
constexpr int LUCKY_SET_ARMOR_START_INDEX = 62;
constexpr int LUCKY_SET_ARMOR_COUNT = 11; // Indices 62-72

// Restricted ITEM_HELPER special items starting at local index 135.
// Exact item-name coverage may vary across older client logic.
constexpr int RESTRICTED_SPECIAL_MISC_START_INDEX = 135;
constexpr int RESTRICTED_SPECIAL_MISC_COUNT = 12; // 135..146

// Restricted ITEM_POTION special jewels starting at local index 160.
// Exact item-name coverage may vary across older client logic.
constexpr int RESTRICTED_SPECIAL_JEWEL_START_INDEX = 160;
constexpr int RESTRICTED_SPECIAL_JEWEL_COUNT = 2; // 160..161

// Item action restriction types for Check_ItemAction
// Index into nItemOption array: [PERSONALSHOP, STORE, TRADE, DROP, SELL, REPAIR]
constexpr int ITEM_ACTION_BLOCK_STORAGE_TRADE = 0; // 0,1,1,0,0,0 - Blocks storage & trade
constexpr int ITEM_ACTION_BLOCK_SELL_ONLY = 1;     // 0,0,0,0,1,0 - Blocks sell only

namespace SEASON4A
{
class CSocketItemMgr;
}
class CSItemOption;

extern ActionSkillType GetSkillByBook(int Type);
extern float CalcDurabilityPercent(BYTE dur, BYTE maxDur, int Level, int excellentFlags,
                                   int ancientDiscriminator = 0);

BOOL IsValidateSkillIdx(INT iSkillIdx);

bool IsCepterItem(int iType);

//  HotKey

// skill.

enum ITEMSETOPTION
{
    eITEM_PERSONALSHOP = 0,
    eITEM_STORE,
    eITEM_TRADE,
    eITEM_DROP,
    eITEM_SELL,
    eITEM_REPAIR,
    eITEM_END
};

struct sItemAct
{
    int s_nItemIndex;
    bool s_bType[eITEM_END];
};

// guild

// text 관련

// party

// inventory

bool CheckEmptyInventory(ITEM *Inv, int InvWidth, int InvHeight);
#ifdef AUTO_CHANGE_ITEM

#endif // AUTO_CHANGE_ITEM

int CompareItem(ITEM item1, ITEM item2);

int64_t CalcSelfRepairCost(int64_t ItemValue, int Durability, int MaxDurability, short Type);
WORD CalcMaxDurability(const ITEM *ip, ITEM_ATTRIBUTE *p, int Level);

//  Party.

bool IsPartChargeItem(ITEM *pItem);
bool IsPersonalShopBan(ITEM *pItem);
bool IsDropBan(ITEM *pItem);
bool IsStoreBan(ITEM *pItem);
bool IsSellingBan(ITEM *pItem);
bool IsWingItem(ITEM *pItem);
bool IsJewelItem(ITEM *pItem);
bool IsExcellentItem(ITEM *pItem);
bool IsAncientItem(ITEM *pItem);
bool IsMoneyItem(ITEM *pItem);

bool Check_ItemAction(ITEM *_pItem, ITEMSETOPTION _eAction, bool _bType = false);
bool Check_LuckyItem(int _nIndex, int _nType = 0);
sItemAct Set_ItemActOption(int _nIndex, int _nOption);

bool IsLuckySetItem(int iType);

class CMapManager;
class SessionKeeper;
struct WorldCharacterVisualState;
struct CharacterDarksideVisual;

class CItemEqualType
{
  private:
    int m_nModelType;
    int m_nSubLeftType;
    int m_nSubRightType;

  public:
    CItemEqualType();
    ~CItemEqualType();

    void SetModelType(int _ModelIndex, int _Left, int _Right);
    const int &GetModelType() const
    {
        return m_nModelType;
    }
    const int &GetSubLeftType() const
    {
        return m_nSubLeftType;
    }
    const int &GetSubRightType() const
    {
        return m_nSubRightType;
    }
};

typedef std::map<int, CItemEqualType> tm_ItemEqualType;
typedef std::list<int> list_ItemType;
typedef std::vector<WORD> vec_DarkIndex;

class CMonkSystem : protected SessionLegacyCalls
{
  private:
    enum
    {
        DS_TARGET_NONE = NUMOFMON,
        MAX_REPEATEDLY = 4,
    };
    struct DamageInfo
    {
        int m_Damage;
        int m_DamageType;
        bool m_Double;
        //int m_ShieldDamage;
    };

    DamageInfo m_arrRepeatedly[MAX_REPEATEDLY];
    int m_nRepeatedlyCnt;

    int m_nTotalCnt;
    tm_ItemEqualType m_mapItemEqualType;
    CItemEqualType m_cItemEqualType;
    list_ItemType m_listGloveformSword;

    int m_nDarksideCnt = 0;
    vec_DarkIndex m_DarksideIndex;

    enum ATTACK_FRAME
    {
        NONE_ATT = 0,
        FRAME_FIRSTATT = 1,
        FRAME_SECONDATT = 2,
    };

    float m_fFirstFrame;
    float m_fSecondFrame;
    BYTE m_btAttState;

    bool m_bUseEffectOnce;
    int m_nLowerEffCnt;

  public:
    explicit CMonkSystem(SessionKeeper &keeper);
    virtual ~CMonkSystem();

    void Init();
    void Destroy();
    void RegistItem();
    int GetSubItemType(int _Type, int _Left = 0);
    int GetModelItemType(int _Type);
    int OrginalTypeCommonItemMonk(int _ModifyType);
    int ModifyTypeCommonItemMonk(int _OrginalType);
    bool IsRagefighterCommonWeapon(CLASS_TYPE _Class, int _Type);
    bool IsSwordformGloves(int _Type);
    void RenderPhoenixGloves(const CharacterDrawInput &character, BYTE slot = 0);
    void RenderSwordformGloves(const CharacterDrawInput &character, int _ModelType, int _Hand,
                               float _Alpha, bool _Translate = false, int _Select = 0);
    int ModifyTypeSwordformGloves(int _ModelType, int _LeftHand);
    int EqualItemModelType(int _Type);
    void LoadModelItem();
    void LoadModelItemTexture();
    void MoveBlurEffect(CHARACTER *pCha, OBJECT *pObj, BMD *pModel, float animationFactor);
    const int GetItemCnt() const
    {
        return m_nTotalCnt;
    }
    void SetSwordformGlovesItemType();
    bool IsSwordformGlovesItemType(int _Type);
    bool RageEquipmentWeapon(int _Index, short _ItemType);
    bool SetRepeatedly(int _Damage, int _DamageType, bool _Double, bool _bEndRepeatedly);
    int GetRepeatedlyDamage(int _index);
    int GetRepeatedlyDamageType(int _index);
    bool GetRepeatedlyDouble(int _index);
    int GetRepeatedlyCnt();
    bool SetRageSkillAni(int _nSkill, CHARACTER &character);
    bool IsRageHalfwaySkillAni(int _nSkill);
    bool IsSecondAttackState() const
    {
        return m_btAttState == FRAME_SECONDATT;
    }
    bool RageFighterEffect(const ObjectDrawInput &draw, int _Type);
    bool SetDarksideTargetIndexState(CHARACTER &character, WORD *targetIndex);
    void SetDarksideCnt(CHARACTER &character, const CHARACTER &target);
    bool SendDarksideAtt(OBJECT *_pObj);
    void InitDarksideTarget();
    bool CalculateDarksideTrans(CharacterAfterImagePose &pose, vec3_t _vPos, float _fAni,
                                float _fNextAni = 0);
    void AdvanceDarksideVisual(CHARACTER &character, WorldCharacterVisualState &visual);
    void SetDummy(CharacterDarksideVisual &visual, vec3_t pos, vec3_t target);
    void InitConsecutiveState(float _fFirstFrame = 0, float _fSecondFrame = 0,
                              BYTE _btAttState = NONE_ATT);
    bool IsConsecutiveAtt(float _fAttFrame);
    void RenderRepeatedly(int _Key, OBJECT *pObj);
    bool IsRideNotUseSkill(int _nSkill, short _Type);
    bool IsSwordformGlovesUseSkill(int _nSkill);
    bool IsChangeringNotUseSkill(short _LType, short _RType, int _LLevel, int _RLevel);
    void InitEffectOnce();
    bool GetSkillUseState();
    bool RageCreateEffect(OBJECT *_pObj, int _nSkill);
    void InitLower();
    int GetLowerEffCnt();
    bool SetLowerEffEct();
};

namespace ItemRulesDetail
{

#pragma pack(push)
#pragma pack()
inline const int DEFAULT_DEVILSQUARELEVEL[6][2] = {{15, 130},  {131, 180}, {181, 230},
                                                   {231, 280}, {281, 330}, {331, 99999}};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const int DARKCLASS_DEVILSQUARELEVEL[6][2] = {{15, 110},  {111, 160}, {161, 210},
                                                     {211, 260}, {261, 310}, {311, 99999}};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const int g_iChaosCastleLevel[12][2] = {{15, 49},   {50, 119},  {120, 179}, {180, 239},
                                               {240, 299}, {300, 999}, {15, 29},   {30, 99},
                                               {100, 159}, {160, 219}, {220, 279}, {280, 999}};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const int g_iChaosCastleZen[6] = {25, 80, 150, 250, 400, 650};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const int iMaxLevel = 15;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const int iMaxColumn = 17;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int SommonTable[] = {2, 7, 14, 8, 9, 41};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr wchar_t ChaosEventName[][100] = {
    L"????? ???", L"???4 ???", L"??????", L"??? ?? ???+??? ??", L"256M ?", L"6???? ???",
    L"?????(??)", L"? ???",    L"? T??",  L"? 10?? ?????"};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const std::unordered_set<int> boldTextItems = {
    MODEL_JEWEL_OF_BLESS,      MODEL_JEWEL_OF_SOUL,
    MODEL_JEWEL_OF_LIFE,       MODEL_JEWEL_OF_CHAOS,
    MODEL_JEWEL_OF_CREATION,   MODEL_JEWEL_OF_GUARDIAN,
    MODEL_LOCHS_FEATHER,       MODEL_GEMSTONE,
    MODEL_JEWEL_OF_HARMONY,    MODEL_LOWER_REFINE_STONE,
    MODEL_HIGHER_REFINE_STONE, MODEL_COMPILED_CELE,
    MODEL_COMPILED_SOUL,       MODEL_DEVILS_EYE,
    MODEL_DEVILS_KEY,          MODEL_DEVILS_INVITATION,
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const std::unordered_set<int> whiteTextItems = {
    MODEL_SCROLL_OF_CHAOTIC_DISEIER,
    MODEL_SCROLL_OF_FIRE_SCREAM,
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const std::unordered_set<int> yellowTextItems = {

    MODEL_ZEN,
    MODEL_JEWEL_OF_BLESS,
    MODEL_JEWEL_OF_SOUL,
    MODEL_JEWEL_OF_LIFE,
    MODEL_JEWEL_OF_CHAOS,
    MODEL_JEWEL_OF_CREATION,
    MODEL_JEWEL_OF_GUARDIAN,
    MODEL_LOCHS_FEATHER,
    MODEL_GEMSTONE,
    MODEL_JEWEL_OF_HARMONY,
    MODEL_LOWER_REFINE_STONE,
    MODEL_HIGHER_REFINE_STONE,
    MODEL_COMPILED_CELE,
    MODEL_COMPILED_SOUL,
    MODEL_DEVILS_EYE,
    MODEL_DEVILS_KEY,
    MODEL_DEVILS_INVITATION,
    MODEL_BOX_OF_LUCK,
    MODEL_FRUITS,
    MODEL_SPIRIT,
    MODEL_EVENT + 16,
    MODEL_EVENT + 5,
    MODEL_OLD_SCROLL,
    MODEL_ILLUSION_SORCERER_COVENANT,
    MODEL_SCROLL_OF_BLOOD,
    MODEL_POTION + 64,
    MODEL_EVENT + 11,
    MODEL_EVENT + 12,
    MODEL_EVENT + 13,
    MODEL_EVENT + 14,
    MODEL_EVENT + 15,
    MODEL_SUSPICIOUS_SCRAP_OF_PAPER,
    MODEL_GAIONS_ORDER,
    MODEL_FIRST_SECROMICON_FRAGMENT,
    MODEL_SECOND_SECROMICON_FRAGMENT,
    MODEL_THIRD_SECROMICON_FRAGMENT,
    MODEL_FOURTH_SECROMICON_FRAGMENT,
    MODEL_FIFTH_SECROMICON_FRAGMENT,
    MODEL_SIXTH_SECROMICON_FRAGMENT,
    MODEL_COMPLETE_SECROMICON,
    MODEL_POTION + 100,
    MODEL_POTION + 111,
    MODEL_POTION + 112,
    MODEL_POTION + 113,
    MODEL_POTION + 120,
    MODEL_POTION + 121,
    MODEL_POTION + 122,
    MODEL_POTION + 123,
    MODEL_POTION + 124,
    MODEL_POTION + 134,
    MODEL_POTION + 135,
    MODEL_POTION + 136,
    MODEL_POTION + 137,
    MODEL_POTION + 138,
    MODEL_POTION + 139,
    MODEL_POTION + 114,
    MODEL_POTION + 115,
    MODEL_POTION + 116,
    MODEL_POTION + 117,
    MODEL_POTION + 118,
    MODEL_POTION + 119,
    MODEL_POTION + 126,
    MODEL_POTION + 127,
    MODEL_POTION + 128,
    MODEL_POTION + 129,
    MODEL_POTION + 130,
    MODEL_POTION + 131,
    MODEL_POTION + 132,
    MODEL_HELPER + 91,
    MODEL_HELPER + 97,
    MODEL_HELPER + 98,
    MODEL_HELPER + 99,
    MODEL_HELPER + 109,
    MODEL_HELPER + 110,
    MODEL_HELPER + 111,
    MODEL_HELPER + 112,
    MODEL_HELPER + 113,
    MODEL_HELPER + 114,
    MODEL_HELPER + 115,
    MODEL_HELPER + 121,
    MODEL_WING + 25,
    MODEL_LOST_MAP,
    MODEL_SYMBOL_OF_KUNDUN,
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const std::unordered_set<int> orangeTextItems = {
    MODEL_CHERRY_BLOSSOM_PLAYBOX,
    MODEL_CHERRY_BLOSSOM_WINE,
    MODEL_CHERRY_BLOSSOM_RICE_CAKE,
    MODEL_CHERRY_BLOSSOM_FLOWER_PETAL,
    MODEL_GOLDEN_CHERRY_BLOSSOM_BRANCH,
    MODEL_DEMON,
    MODEL_SPIRIT_OF_GUARDIAN,
    MODEL_PET_RUDOLF,
    MODEL_PET_PANDA,
    MODEL_PET_UNICORN,
    MODEL_PET_SKELETON,
    MODEL_SNOWMAN_TRANSFORMATION_RING,
    MODEL_PANDA_TRANSFORMATION_RING,
    MODEL_SKELETON_TRANSFORMATION_RING,
    MODEL_PUMPKIN_OF_LUCK,
    MODEL_JACK_OLANTERN_BLESSINGS,
    MODEL_JACK_OLANTERN_WRATH,
    MODEL_JACK_OLANTERN_CRY,
    MODEL_JACK_OLANTERN_FOOD,
    MODEL_JACK_OLANTERN_DRINK,
    MODEL_HELPER + 43,
    MODEL_HELPER + 44,
    MODEL_HELPER + 45,
    MODEL_HELPER + 46,
    MODEL_HELPER + 47,
    MODEL_HELPER + 48,
    MODEL_HELPER + 54,
    MODEL_HELPER + 55,
    MODEL_HELPER + 56,
    MODEL_HELPER + 57,
    MODEL_HELPER + 58,
    MODEL_HELPER + 59,
    MODEL_HELPER + 60,
    MODEL_HELPER + 61,
    MODEL_HELPER + 62,
    MODEL_HELPER + 63,
    MODEL_HELPER + 116,
    MODEL_HELPER + 125,
    MODEL_HELPER + 126,
    MODEL_HELPER + 127,
    MODEL_HELPER + 128,
    MODEL_HELPER + 129,
    MODEL_HELPER + 130,
    MODEL_HELPER + 131,
    MODEL_HELPER + 132,
    MODEL_HELPER + 133,
    MODEL_HELPER + 134,
    MODEL_POTION + 53,
    MODEL_POTION + 54,
    MODEL_POTION + 58,
    MODEL_POTION + 59,
    MODEL_POTION + 60,
    MODEL_POTION + 61,
    MODEL_POTION + 62,
    MODEL_POTION + 70,
    MODEL_POTION + 71,
    MODEL_POTION + 72,
    MODEL_POTION + 73,
    MODEL_POTION + 74,
    MODEL_POTION + 75,
    MODEL_POTION + 76,
    MODEL_POTION + 77,
    MODEL_POTION + 78,
    MODEL_POTION + 79,
    MODEL_POTION + 80,
    MODEL_POTION + 81,
    MODEL_POTION + 82,
    MODEL_POTION + 83,
    MODEL_POTION + 88,
    MODEL_POTION + 89,
    MODEL_POTION + 91,
    MODEL_POTION + 92,
    MODEL_POTION + 93,
    MODEL_POTION + 94,
    MODEL_POTION + 95,
    MODEL_POTION + 96,
    MODEL_POTION + 97,
    MODEL_POTION + 98,
    MODEL_POTION + 141,
    MODEL_POTION + 142,
    MODEL_POTION + 143,
    MODEL_POTION + 144,
    MODEL_POTION + 145,
    MODEL_POTION + 146,
    MODEL_POTION + 147,
    MODEL_POTION + 148,
    MODEL_POTION + 149,
    MODEL_POTION + 150,
    MODEL_WING + 130,
    MODEL_WING + 131,
    MODEL_WING + 132,
    MODEL_WING + 133,
    MODEL_WING + 134,
    MODEL_WING + 135,
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr size_t GROUND_ITEM_LABEL_CACHE_MAX_ENTRIES = 1500;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr DWORD GROUND_ITEM_LABEL_CACHE_MAX_IDLE_MS = 10 * 1000;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
struct GroundItemLabelDescriptor
{
    wchar_t Name[80]{};
    LegacyFontRole Font = LegacyFontRole::Normal;
    DWORD TextColor = 0xFFFFFFFF;
    DWORD BgColor = 0xFF000000;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline DWORD MakeRgba(BYTE red, BYTE green, BYTE blue, BYTE alpha = 255)
{
    return red + (green << 8) + (blue << 16) + (alpha << 24);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
template <size_t BufferSize, typename... Args>
void FormatGroundItemLabelText(wchar_t (&buffer)[BufferSize], const wchar_t *format, Args... args)
{
    _snwprintf_s(buffer, BufferSize, _TRUNCATE, format, args...);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
template <size_t BufferSize>
void CopyGroundItemLabelText(wchar_t (&buffer)[BufferSize], const wchar_t *value)
{
    FormatGroundItemLabelText(buffer, L"%ls", value != nullptr ? value : L"");
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
template <size_t BufferSize, typename... Args>
void AppendGroundItemLabelText(wchar_t (&buffer)[BufferSize], const wchar_t *format, Args... args)
{
    const size_t currentLength = wcslen(buffer);
    if (currentLength >= BufferSize - 1)
    {
        return;
    }

    _snwprintf_s(buffer + currentLength, BufferSize - currentLength, _TRUNCATE, format, args...);
}
#pragma pack(pop)

} // namespace ItemRulesDetail

namespace ItemRulesDetail
{

bool IsDivineArchangelWeaponItem(int itemType);
bool IsDivineArchangelWeaponModel(int modelType);

void SetDescriptorTextColor(ItemRulesDetail::GroundItemLabelDescriptor &descriptor, float red,
                            float green, float blue);
void SetDescriptorYellowTextColor(ItemRulesDetail::GroundItemLabelDescriptor &descriptor);
void SetDescriptorGrayTextColor(ItemRulesDetail::GroundItemLabelDescriptor &descriptor);
void SetDescriptorOrangeTextColor(ItemRulesDetail::GroundItemLabelDescriptor &descriptor);
bool PublishBitmapRevision(SessionBitmapView &bitmaps, std::uint32_t logicalIndex,
                           LogicalRenderAssetRef currentAsset, std::uint32_t width,
                           std::uint32_t height, std::span<const std::byte> rgba8);
} // namespace ItemRulesDetail

void ItemAngle(OBJECT *object);
int64_t CalcRepairCost(int64_t value, int durability, int maximum, short type, bool selfRepair);
