#pragma once
#include "support/CoreMath.h"
#include "session/SessionRuntime.h"
#include "session/SessionNetwork.h"
#include "data/GameData.h"
#include "data/WorldData.h"
#include "domain/WorldSimulation.h"
#include "domain/CharacterSystem.h"
#include "domain/Events.h"

#define AUTOATTACK_ON 0x01
#define AUTOATTACK_OFF 0x02
#define MUHELPER_TIMER 1005

#include <array>
#include <atomic>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace MUHelper
{
inline constexpr int ManualControlYieldSecondsDefault = 2;
inline constexpr int ManualControlYieldSecondsMaximum = 254;
inline constexpr int HelperTicksPerSecond = 4;

enum ESkillActivationBase : uint32_t
{
    ALWAYS = 0x00000000,
    ON_TIMER = 0x00000001,
    ON_CONDITION = 0x00000002,
};

enum ESkillActivationPreCon : uint32_t
{
    ON_MOBS_NEARBY = 0x00000004,
    ON_MOBS_ATTACKING = 0x00000008,
};

enum ESkillActivationSubCon : uint32_t
{
    ON_MORE_THAN_TWO_MOBS = 0x00000010,
    ON_MORE_THAN_THREE_MOBS = 0x00000020,
    ON_MORE_THAN_FOUR_MOBS = 0x00000040,
    ON_MORE_THAN_FIVE_MOBS = 0x00000080,
};

DEFINE_ENUM_FLAG_OPERATORS(ESkillActivationBase);
DEFINE_ENUM_FLAG_OPERATORS(ESkillActivationPreCon);
DEFINE_ENUM_FLAG_OPERATORS(ESkillActivationSubCon);

constexpr uint32_t MUHELPER_SKILL_PRECON_CLEAR =
    ~(static_cast<uint32_t>(ON_MOBS_NEARBY) | static_cast<uint32_t>(ON_MOBS_ATTACKING));

constexpr uint32_t MUHELPER_SKILL_SUBCON_CLEAR = ~(
    static_cast<uint32_t>(ON_MORE_THAN_TWO_MOBS) | static_cast<uint32_t>(ON_MORE_THAN_THREE_MOBS) |
    static_cast<uint32_t>(ON_MORE_THAN_FOUR_MOBS) | static_cast<uint32_t>(ON_MORE_THAN_FIVE_MOBS));

enum EPetAttackMode : BYTE
{
    PET_ATTACK_CEASE = 0x00,
    PET_ATTACK_AUTO = 0x01,
    PET_ATTACK_TOGETHER = 0x02,
};

struct ConfigData
{
    int iHuntingRange = 0;
    bool bLongRangeCounterAttack = false;
    bool bReturnToOriginalPosition = false;
    int iMaxSecondsAway = 0;
    std::array<uint32_t, 3> aiSkill = {0, 0, 0};
    std::array<uint32_t, 3> aiSkillCondition = {0, 0, 0};
    std::array<uint32_t, 3> aiSkillInterval = {0, 0, 0};
    bool bUseCombo = false;
    std::array<uint32_t, 3> aiBuff = {0, 0, 0};
    bool bBuffDuration = false;
    bool bBuffDurationParty = false;
    int iBuffCastInterval = 0;
    bool bAutoHeal = false;
    int iHealThreshold = 0;
    bool bSupportParty = false;
    bool bAutoHealParty = false;
    int iHealPartyThreshold = 0;
    bool bUseHealPotion = false;
    int iPotionThreshold = 0;
    bool bUseDrainLife = false;
    bool bUseDarkRaven = false;
    int iDarkRavenMode = 0;
    bool bRepairItem = false;
    int iObtainingRange = 0;
    bool bPickAllItems = false;
    bool bPickSelectItems = false;
    bool bPickJewel = false;
    bool bPickZen = false;
    bool bPickAncient = false;
    bool bPickExcellent = false;
    bool bPickExtraItems = false;
    std::set<std::wstring> aExtraItems;
    bool bUseSelfDefense = false;
    bool bAutoAcceptFriend = false;
    bool bAutoAcceptGuild = false;
    bool bFallbackBasicAttack = true;
    bool bConcentratedMonsters = false;
    bool bUseSkillsClosely = false;
    int iManualControlYieldSeconds = ManualControlYieldSecondsDefault;
};
} // namespace MUHelper

namespace MUHelper::Combat
{
inline constexpr int ChaseStallAttemptCount = 12;

struct ChaseProgress
{
    int target = -1;
    POINT position{};
    int unchangedAttempts = 0;

    bool Record(int targetId, POINT heroPosition) noexcept
    {
        if (target != targetId || position.x != heroPosition.x || position.y != heroPosition.y)
        {
            target = targetId;
            position = heroPosition;
            unchangedAttempts = 1;
            return false;
        }
        return ++unchangedAttempts >= ChaseStallAttemptCount;
    }

    void Reset() noexcept
    {
        target = -1;
        position = {};
        unchangedAttempts = 0;
    }
};
} // namespace MUHelper::Combat

namespace MUHelper
{
class ConfigDataSerDe
{
  public:
    static void Serialize(const ConfigData &domain, PRECEIVE_MUHELPER_DATA &packet);
    static void Deserialize(const PRECEIVE_MUHELPER_DATA &packet, ConfigData &domain);
};
} // namespace MUHelper

namespace MUHelper
{
struct SessionMuHelperStorage
{
    ConfigData m_config{};
    ConfigData uiConfig{};
    POINT m_posOriginal{};
    std::atomic<bool> m_bActive{false};
    std::set<int> m_setTargets;
    std::vector<Combat::HuntingTarget> huntingCandidates;
    std::vector<int> huntingDensity;
    std::set<int> m_setTargetsAttacking;
    std::set<int> m_setItems;
    SpinLock targetsLock;
    SpinLock itemsLock;
    int m_iCurrentItem = MAX_ITEMS;
    int m_iCurrentTarget = -1;
    int m_iCurrentBuffIndex = 0;
    int m_iCurrentBuffPartyIndex = 0;
    int m_iCurrentHealPartyIndex = 0;
    int m_iComboState = 0;
    ActionSkillType m_iCurrentSkill{};
    int m_iHuntingDistance = 0;
    int m_iObtainingDistance = 0;
    int m_iLoopCounter = 0;
    int m_iSecondsElapsed = 0;
    int m_iSecondsAway = 0;
    bool m_bTimerActivatedBuffOngoing = false;
    bool m_bPetActivated = false;
    int m_iTotalCost = 0;
    Combat::ChaseProgress chaseProgress{};
    int manualYieldTicks = 0;
    bool ownsMovement = false;
};
} // namespace MUHelper

class SessionKeeper;
class CHARACTER;
class CmuConsoleDebug;
class CSkillManager;
class SessionGameplayUnit;

namespace MUHelper
{
class SessionMuHelperUnit final : protected SessionLegacyCalls
{
  public:
    explicit SessionMuHelperUnit(SessionKeeper &keeper) noexcept;
    void Tick();
    ConfigData GetConfig() const;
    void Save(const ConfigData &config);
    void Load(const ConfigData &config);
    void Start();
    void Stop();
    void Toggle();
    void TriggerStart();
    void TriggerStop();
    void YieldToManualControl();
    bool IsActive() const noexcept
    {
        return m_bActive;
    }
    void AddCost(int cost) noexcept
    {
        m_iTotalCost += cost;
    }
    int GetTotalCost() const noexcept
    {
        return m_iTotalCost;
    }
    ConfigData &UiConfiguration() noexcept
    {
        return uiConfig_;
    }
    void AddTarget(int targetId, bool isAttacking);
    void DeleteTarget(int targetId);
    void DeleteAllTargets();
    bool IsSelfDefenseTarget(int targetId) const noexcept;
    void AddItem(int itemId, POINT droppedPosition);
    void DeleteItem(int itemId);

  private:
    friend class SessionLegacyCalls;

    bool IsHeroSwingInProgress() const;
    void WorkLoop(UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
    void Work();
    int Buff();
    int BuffTarget(CHARACTER *target, ActionSkillType skill);
    int RecoverHealth();
    int Heal();
    int HealSelf(ActionSkillType skill);
    int DrainLife();
    int Attack();
    bool AcquireAttackTarget();
    bool ApproachAttackTarget(int target, float distance);
    void AdvanceAttackPath(const PATH_t &path);
    void SendAutomatedMove();
    void CancelAutomatedMovement();
    void ResetRuntimeState(bool clearCandidates);
    void RefreshAutomationCandidates();
    void RecoverAutomation();
    bool ChaseHasStalled(int target);
    ActionSkillType SelectAttackSkill();
    int SimulateComboAttack();
    int SimulateAttack(ActionSkillType skill);
    int SimulateSkill(ActionSkillType skill, bool targetRequired, int target);
    int SimulateBasicAttack(int target);
    int ComputeDistanceByRange(int range);
    int ComputeDistanceBetween(POINT first, POINT second);
    int ActivatePet();
    int ObtainItem();
    int Regroup();
    int RepairEquipments();
    bool HasAssignedBuffSkill();
    int ComputeDistanceFromTarget(CHARACTER *target);
    int ConsumePotion();
    ActionSkillType GetHealingSkill();
    ActionSkillType GetDrainLifeSkill();
    int GetHuntingTarget();
    void CleanupTargets();
    int GetFarthestAttackingTarget();
    bool IsSelfPositionSkill(ActionSkillType skill);
    int SimulateMove(POINT position);
    int SelectItemToObtain();
    bool ShouldObtainItem(int itemId);

    ConfigData &m_config;
    ConfigData &uiConfig_;
    POINT &m_posOriginal;
    std::atomic<bool> &m_bActive;
    std::set<int> &m_setTargets;
    std::vector<Combat::HuntingTarget> &huntingCandidates_;
    std::vector<int> &huntingDensity_;
    std::set<int> &m_setTargetsAttacking;
    std::set<int> &m_setItems;
    SpinLock &targetsLock_;
    SpinLock &itemsLock_;
    int &m_iCurrentItem;
    int &m_iCurrentTarget;
    int &m_iCurrentBuffIndex;
    int &m_iCurrentBuffPartyIndex;
    int &m_iCurrentHealPartyIndex;
    int &m_iComboState;
    ActionSkillType &m_iCurrentSkill;
    int &m_iHuntingDistance;
    int &m_iObtainingDistance;
    int &m_iLoopCounter;
    int &m_iSecondsElapsed;
    int &m_iSecondsAway;
    bool &m_bTimerActivatedBuffOngoing;
    bool &m_bPetActivated;
    int &m_iTotalCost;
    Combat::ChaseProgress &chaseProgress_;
    int &manualYieldTicks_;
    bool &ownsMovement_;
    ITEM_t (&Items)[MAX_ITEMS];
    CmuConsoleDebug &g_ConsoleDebug;
    CSkillManager &gSkillManager;
    SessionGameplayUnit &gameplay_;
};
} // namespace MUHelper
