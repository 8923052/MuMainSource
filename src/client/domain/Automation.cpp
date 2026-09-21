#include "domain/Automation.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
#include "support/CoreMath.h"
#include "session/SessionKeeper.h"
#include "session/SessionGameplay.h"
#include "domain/ItemsSkills.h"
#include "domain/Events.h"
#include "domain/MovementAI.h"
#include "domain/CharacterSystem.h"
#include "ui/session/UiSessionLogic.h"
#include "app/ApplicationDiagnostics.h"
#include "domain/ChatSocial.h"
#include "domain/MapSimulation.h"
#include "session/SessionNetwork.h"

MUHelper::SessionMuHelperUnit::SessionMuHelperUnit(SessionKeeper &keeper) noexcept
    : SessionLegacyCalls(keeper), m_config(keeper.muHelperStorage_.m_config),
      uiConfig_(keeper.muHelperStorage_.uiConfig),
      m_posOriginal(keeper.muHelperStorage_.m_posOriginal),
      m_bActive(keeper.muHelperStorage_.m_bActive),
      m_setTargets(keeper.muHelperStorage_.m_setTargets),
      huntingCandidates_(keeper.muHelperStorage_.huntingCandidates),
      huntingDensity_(keeper.muHelperStorage_.huntingDensity),
      m_setTargetsAttacking(keeper.muHelperStorage_.m_setTargetsAttacking),
      m_setItems(keeper.muHelperStorage_.m_setItems),
      targetsLock_(keeper.muHelperStorage_.targetsLock),
      itemsLock_(keeper.muHelperStorage_.itemsLock),
      m_iCurrentItem(keeper.muHelperStorage_.m_iCurrentItem),
      m_iCurrentTarget(keeper.muHelperStorage_.m_iCurrentTarget),
      m_iCurrentBuffIndex(keeper.muHelperStorage_.m_iCurrentBuffIndex),
      m_iCurrentBuffPartyIndex(keeper.muHelperStorage_.m_iCurrentBuffPartyIndex),
      m_iCurrentHealPartyIndex(keeper.muHelperStorage_.m_iCurrentHealPartyIndex),
      m_iComboState(keeper.muHelperStorage_.m_iComboState),
      m_iCurrentSkill(keeper.muHelperStorage_.m_iCurrentSkill),
      m_iHuntingDistance(keeper.muHelperStorage_.m_iHuntingDistance),
      m_iObtainingDistance(keeper.muHelperStorage_.m_iObtainingDistance),
      m_iLoopCounter(keeper.muHelperStorage_.m_iLoopCounter),
      m_iSecondsElapsed(keeper.muHelperStorage_.m_iSecondsElapsed),
      m_iSecondsAway(keeper.muHelperStorage_.m_iSecondsAway),
      m_bTimerActivatedBuffOngoing(keeper.muHelperStorage_.m_bTimerActivatedBuffOngoing),
      m_bPetActivated(keeper.muHelperStorage_.m_bPetActivated),
      m_iTotalCost(keeper.muHelperStorage_.m_iTotalCost),
      chaseProgress_(keeper.muHelperStorage_.chaseProgress),
      manualYieldTicks_(keeper.muHelperStorage_.manualYieldTicks),
      ownsMovement_(keeper.muHelperStorage_.ownsMovement),
      Items(keeper.ItemsStorage()),
      g_ConsoleDebug(keeper.ConsoleDebug()), gSkillManager(keeper.SkillManagerObject()),
      gameplay_(keeper.GameplayForConstruction())
{
    (void)sessionKeeper_.RegisterMuHelper(*this);
}

void MUHelper::SessionMuHelperUnit::Tick()
{
    WorkLoop(0, MUHELPER_TIMER, 0);
}

namespace MUHelper
{
namespace
{
// Client-owned flags occupy the existing echoed byte; packet size is unchanged.
void SerializeClientOptions(const ConfigData &gameData, PRECEIVE_MUHELPER_DATA &netData)
{
    netData.bUseSelfDefense = gameData.bUseSelfDefense;
    netData.bAutoAcceptFriend = gameData.bAutoAcceptFriend;
    netData.bAutoAcceptGuild = gameData.bAutoAcceptGuild;
    netData.bFallbackBasicAttack = gameData.bFallbackBasicAttack;
    netData.bConcentratedMonsters = gameData.bConcentratedMonsters;
    netData.bUseSkillsClosely = gameData.bUseSkillsClosely;
    netData.ManualControlYieldSecondsPlusOne = static_cast<BYTE>(
        std::clamp(gameData.iManualControlYieldSeconds, 0, ManualControlYieldSecondsMaximum) + 1);
}

void DeserializeClientOptions(const PRECEIVE_MUHELPER_DATA &netData, ConfigData &gameData)
{
    gameData.bUseSelfDefense = netData.bUseSelfDefense != 0;
    gameData.bAutoAcceptFriend = netData.bAutoAcceptFriend != 0;
    gameData.bAutoAcceptGuild = netData.bAutoAcceptGuild != 0;
    gameData.bFallbackBasicAttack = netData.bFallbackBasicAttack != 0;
    gameData.bConcentratedMonsters = netData.bConcentratedMonsters != 0;
    gameData.bUseSkillsClosely = netData.bUseSkillsClosely != 0;
    gameData.iManualControlYieldSeconds =
        netData.ManualControlYieldSecondsPlusOne == 0
            ? ManualControlYieldSecondsDefault
            : static_cast<int>(netData.ManualControlYieldSecondsPlusOne) - 1;
}
} // namespace

void ConfigDataSerDe::Serialize(const ConfigData &gameData, PRECEIVE_MUHELPER_DATA &netData)
{
    memset(&netData, 0, sizeof(netData));

    netData.HuntingRange = static_cast<BYTE>(gameData.iHuntingRange & 0x0F);
    netData.DistanceMin = static_cast<BYTE>(gameData.iMaxSecondsAway & 0x0F);
    netData.LongDistanceAttack = gameData.bLongRangeCounterAttack ? 1 : 0;
    netData.OriginalPosition = gameData.bReturnToOriginalPosition ? 1 : 0;

    netData.BasicSkill1 = static_cast<WORD>(gameData.aiSkill[0] & 0xFFFF);
    netData.ActivationSkill1 = static_cast<WORD>(gameData.aiSkill[1] & 0xFFFF);
    netData.ActivationSkill2 = static_cast<WORD>(gameData.aiSkill[2] & 0xFFFF);

    netData.DelayMinSkill1 = static_cast<WORD>(gameData.aiSkillInterval[1] & 0xFFFF);
    netData.DelayMinSkill2 = static_cast<WORD>(gameData.aiSkillInterval[2] & 0xFFFF);

    if (gameData.aiSkillCondition[1] & ON_TIMER)
    {
        netData.Skill1Delay = 1;
    }
    if (gameData.aiSkillCondition[1] & ON_CONDITION)
    {
        netData.Skill1Con = 1;
    }
    if (gameData.aiSkillCondition[1] & ON_MOBS_NEARBY)
    {
        netData.Skill1PreCon = 0;
    }
    else if (gameData.aiSkillCondition[1] & ON_MOBS_ATTACKING)
    {
        netData.Skill1PreCon = 1;
    }

    if (gameData.aiSkillCondition[1] & ON_MORE_THAN_TWO_MOBS)
    {
        netData.Skill1SubCon = 0;
    }
    else if (gameData.aiSkillCondition[1] & ON_MORE_THAN_THREE_MOBS)
    {
        netData.Skill1SubCon = 1;
    }
    else if (gameData.aiSkillCondition[1] & ON_MORE_THAN_FOUR_MOBS)
    {
        netData.Skill1SubCon = 2;
    }
    else if (gameData.aiSkillCondition[1] & ON_MORE_THAN_FIVE_MOBS)
    {
        netData.Skill1SubCon = 3;
    }

    if (gameData.aiSkillCondition[2] & ON_TIMER)
    {
        netData.Skill2Delay = 1;
    }
    if (gameData.aiSkillCondition[2] & ON_CONDITION)
    {
        netData.Skill2Con = 1;
    }
    if (gameData.aiSkillCondition[2] & ON_MOBS_NEARBY)
    {
        netData.Skill2PreCon = 0;
    }
    else if (gameData.aiSkillCondition[2] & ON_MOBS_ATTACKING)
    {
        netData.Skill2PreCon = 1;
    }

    if (gameData.aiSkillCondition[2] & ON_MORE_THAN_TWO_MOBS)
    {
        netData.Skill2SubCon = 0;
    }
    else if (gameData.aiSkillCondition[2] & ON_MORE_THAN_THREE_MOBS)
    {
        netData.Skill2SubCon = 1;
    }
    else if (gameData.aiSkillCondition[2] & ON_MORE_THAN_FOUR_MOBS)
    {
        netData.Skill2SubCon = 2;
    }
    else if (gameData.aiSkillCondition[2] & ON_MORE_THAN_FIVE_MOBS)
    {
        netData.Skill2SubCon = 3;
    }

    netData.Combo = gameData.bUseCombo ? 1 : 0;

    netData.BuffSkill0NumberID = static_cast<WORD>(gameData.aiBuff[0] & 0xFFFF);
    netData.BuffSkill1NumberID = static_cast<WORD>(gameData.aiBuff[1] & 0xFFFF);
    netData.BuffSkill2NumberID = static_cast<WORD>(gameData.aiBuff[2] & 0xFFFF);

    netData.BuffDuration = gameData.bBuffDuration ? 1 : 0;
    netData.BuffDurationforAllPartyMembers = gameData.bBuffDurationParty ? 1 : 0;
    netData.CastingBuffMin = static_cast<WORD>(gameData.iBuffCastInterval);

    netData.AutoHeal = gameData.bAutoHeal ? 1 : 0;
    netData.HPStatusAutoPotion = static_cast<BYTE>((gameData.iPotionThreshold / 10) & 0x0F);
    netData.HPStatusAutoHeal = static_cast<BYTE>((gameData.iHealThreshold / 10) & 0x0F);
    netData.AutoPotion = gameData.bUseHealPotion ? 1 : 0;
    netData.DrainLife = gameData.bUseDrainLife ? 1 : 0;
    netData.Party = gameData.bSupportParty ? 1 : 0;
    netData.PreferenceOfPartyHeal = gameData.bAutoHealParty ? 1 : 0;

    netData.HPStatusOfPartyMembers = static_cast<BYTE>((gameData.iHealPartyThreshold / 10) & 0x0F);
    netData.HPStatusDrainLife = static_cast<BYTE>((gameData.iHealThreshold / 10) & 0x0F);

    netData.UseDarkSpirits = gameData.bUseDarkRaven ? 1 : 0;
    netData.PetAttack = static_cast<BYTE>(gameData.iDarkRavenMode);

    netData.RepairItem = gameData.bRepairItem ? 1 : 0;
    netData.ObtainRange = static_cast<BYTE>(gameData.iObtainingRange & 0x0F);
    netData.PickAllNearItems = gameData.bPickAllItems ? 1 : 0;
    netData.PickSelectedItems = gameData.bPickSelectItems ? 1 : 0;
    netData.Zen = gameData.bPickZen ? 1 : 0;
    netData.JewelOrGem = gameData.bPickJewel ? 1 : 0;
    netData.ExcellentItem = gameData.bPickExcellent ? 1 : 0;
    netData.SetItem = gameData.bPickAncient ? 1 : 0;
    netData.AddExtraItem = gameData.bPickExtraItems ? 1 : 0;

    memset(netData.ExtraItems, 0, sizeof(netData.ExtraItems));
    int iItemIndex = 0;
    for (const auto &wsItem : gameData.aExtraItems)
    {
        if (iItemIndex >= 12)
        {
            break;
        }

        size_t n = wcstombs(netData.ExtraItems[iItemIndex], wsItem.c_str(), 15);
        if (n == (size_t)-1 || n == 15)
        {
            memset(netData.ExtraItems[iItemIndex], 0, 15);
        }
        iItemIndex++;
    }

    SerializeClientOptions(gameData, netData);
}

void ConfigDataSerDe::Deserialize(const PRECEIVE_MUHELPER_DATA &netData, ConfigData &gameData)
{
    gameData.iHuntingRange = static_cast<int>(netData.HuntingRange);

    gameData.iMaxSecondsAway = static_cast<int>(netData.DistanceMin);
    gameData.bLongRangeCounterAttack = (bool)netData.LongDistanceAttack;
    gameData.bReturnToOriginalPosition = (bool)netData.OriginalPosition;

    gameData.aiSkill.fill(0);
    gameData.aiSkill[0] = static_cast<int>(netData.BasicSkill1);
    gameData.aiSkill[1] = static_cast<int>(netData.ActivationSkill1);
    gameData.aiSkill[2] = static_cast<int>(netData.ActivationSkill2);

    gameData.aiSkillInterval.fill(0);
    gameData.aiSkillInterval[1] = static_cast<int>(netData.DelayMinSkill1);
    gameData.aiSkillInterval[2] = static_cast<int>(netData.DelayMinSkill2);

    gameData.aiSkillCondition.fill(0);
    gameData.aiSkillCondition[1] |= netData.Skill1Delay ? ON_TIMER : 0;
    gameData.aiSkillCondition[1] |= netData.Skill1Con ? ON_CONDITION : 0;
    gameData.aiSkillCondition[1] |= netData.Skill1PreCon == 0 ? ON_MOBS_NEARBY : ON_MOBS_ATTACKING;
    gameData.aiSkillCondition[1] |= netData.Skill1SubCon == 0   ? ON_MORE_THAN_TWO_MOBS
                                    : netData.Skill1SubCon == 1 ? ON_MORE_THAN_THREE_MOBS
                                    : netData.Skill1SubCon == 2 ? ON_MORE_THAN_FOUR_MOBS
                                    : netData.Skill1SubCon == 3 ? ON_MORE_THAN_FIVE_MOBS
                                                                : 0;

    gameData.aiSkillCondition[2] |= netData.Skill2Delay ? ON_TIMER : 0;
    gameData.aiSkillCondition[2] |= netData.Skill2Con ? ON_CONDITION : 0;
    gameData.aiSkillCondition[2] |= netData.Skill2PreCon == 0 ? ON_MOBS_NEARBY : ON_MOBS_ATTACKING;
    gameData.aiSkillCondition[2] |= netData.Skill2SubCon == 0   ? ON_MORE_THAN_TWO_MOBS
                                    : netData.Skill2SubCon == 1 ? ON_MORE_THAN_THREE_MOBS
                                    : netData.Skill2SubCon == 2 ? ON_MORE_THAN_FOUR_MOBS
                                    : netData.Skill2SubCon == 3 ? ON_MORE_THAN_FIVE_MOBS
                                                                : 0;
    gameData.bUseCombo = (bool)netData.Combo;

    gameData.aiBuff.fill(0);
    gameData.aiBuff[0] = static_cast<int>(netData.BuffSkill0NumberID);
    gameData.aiBuff[1] = static_cast<int>(netData.BuffSkill1NumberID);
    gameData.aiBuff[2] = static_cast<int>(netData.BuffSkill2NumberID);

    gameData.bBuffDuration = (bool)netData.BuffDuration;
    gameData.bBuffDurationParty = (bool)netData.BuffDurationforAllPartyMembers;
    gameData.iBuffCastInterval = static_cast<int>(netData.CastingBuffMin);

    gameData.bAutoHeal = (bool)netData.AutoHeal;
    gameData.iHealThreshold = static_cast<int>(netData.HPStatusAutoHeal) * 10;
    gameData.bUseDrainLife = static_cast<int>(netData.DrainLife);
    gameData.bUseHealPotion = (bool)netData.AutoPotion;
    gameData.iPotionThreshold = static_cast<int>(netData.HPStatusAutoPotion) * 10;
    gameData.bSupportParty = (bool)netData.Party;
    gameData.bAutoHealParty = (bool)netData.PreferenceOfPartyHeal;
    gameData.iHealPartyThreshold = static_cast<int>(netData.HPStatusOfPartyMembers) * 10;

    gameData.bUseDarkRaven = (bool)netData.UseDarkSpirits;
    gameData.iDarkRavenMode = static_cast<int>(netData.PetAttack);
    gameData.bRepairItem = (bool)netData.RepairItem;

    gameData.iObtainingRange = static_cast<int>(netData.ObtainRange);
    gameData.bPickAllItems = (bool)netData.PickAllNearItems;
    gameData.bPickSelectItems = (bool)netData.PickSelectedItems;
    gameData.bPickZen = (bool)netData.Zen;
    gameData.bPickJewel = (bool)netData.JewelOrGem;
    gameData.bPickExcellent = (bool)netData.ExcellentItem;
    gameData.bPickAncient = (bool)netData.SetItem;
    gameData.bPickExtraItems = (bool)netData.AddExtraItem;

    wchar_t wsExtraItemBuffer[15 + 1];
    for (int i = 0; i < sizeof(netData.ExtraItems) / sizeof(netData.ExtraItems[0]); i++)
    {
        memset(wsExtraItemBuffer, 0, sizeof(wsExtraItemBuffer));

        size_t n = std::mbstowcs(wsExtraItemBuffer, &netData.ExtraItems[i][0], 15);
        if (n > 0 && n <= 15)
        {
            wsExtraItemBuffer[n] = L'\0';
            if (wsExtraItemBuffer[0] != L'\0')
            {
                gameData.aExtraItems.insert(std::wstring(wsExtraItemBuffer));
            }
        }
    }

    DeserializeClientOptions(netData, gameData);
}

} // namespace MUHelper

constexpr int MAX_ACTIONABLE_DISTANCE = 10;
constexpr int DEFAULT_DURABILITY_THRESHOLD = 50;

namespace MUHelper
{
void SessionMuHelperUnit::Save(const ConfigData &config)
{
    m_config = config;

    PRECEIVE_MUHELPER_DATA netData;
    ConfigDataSerDe::Serialize(m_config, netData);

    SocketClient->ToGameServer()->SendMuHelperSaveDataRequest(reinterpret_cast<BYTE *>(&netData),
                                                              sizeof(netData));
}

void SessionMuHelperUnit::Load(const ConfigData &config)
{
    m_config = config;
}

ConfigData SessionMuHelperUnit::GetConfig() const
{
    return m_config;
}

void SessionMuHelperUnit::Toggle()
{
    if (m_bActive)
    {
        TriggerStop();

        // Stop the client-driven bot immediately instead of waiting for the
        // server's status reply. After an auto-reconnect the server's new
        // session doesn't have the helper marked active, so it never replies
        // and the bot would otherwise keep running with no way to stop it.
        Stop();
    }
    else
    {
        TriggerStart();
    }
}

void SessionMuHelperUnit::TriggerStart()
{
    if (!Hero->SafeZone)
        SocketClient->ToGameServer()->SendMuHelperStatusChangeRequest(0);
}

void SessionMuHelperUnit::TriggerStop()
{
    SocketClient->ToGameServer()->SendMuHelperStatusChangeRequest(1);
}

void SessionMuHelperUnit::Start()
{
    if (m_bActive)
    {
        return;
    }

    m_iTotalCost = 0;
    ResetRuntimeState(true);
    m_iCurrentBuffIndex = 0;
    m_iCurrentBuffPartyIndex = 0;
    m_iCurrentHealPartyIndex = 0;
    m_iCurrentSkill = (ActionSkillType)m_config.aiSkill[0];
    m_posOriginal = {Hero->PositionX, Hero->PositionY};

    m_iHuntingDistance = ComputeDistanceByRange(m_config.iHuntingRange);
    m_iObtainingDistance = ComputeDistanceByRange(m_config.iObtainingRange);

    m_iSecondsElapsed = 0;
    m_iSecondsAway = 0;

    m_bTimerActivatedBuffOngoing = false;
    m_bPetActivated = false;

    m_iLoopCounter = 0;

    RefreshAutomationCandidates();
    m_bActive = true;
    g_ConsoleDebug.Write(MCD_NORMAL, L"[MU Helper] Started");
}

void SessionMuHelperUnit::Stop()
{
    m_bActive = false;
    ResetRuntimeState(true);
    g_ConsoleDebug.Write(MCD_NORMAL, L"[MU Helper] Stopped");
}

void SessionMuHelperUnit::YieldToManualControl()
{
    if (!m_bActive)
    {
        return;
    }

    ResetRuntimeState(false);
    manualYieldTicks_ = std::clamp(m_config.iManualControlYieldSeconds, 0,
                                   ManualControlYieldSecondsMaximum) *
                        HelperTicksPerSecond;
    if (manualYieldTicks_ == 0)
    {
        RecoverAutomation();
    }
}

void SessionMuHelperUnit::ResetRuntimeState(bool clearCandidates)
{
    const int helperTargetIndex = FindCharacterIndex(m_iCurrentTarget);
    CancelAutomatedMovement();
    if (ActionTarget == helperTargetIndex)
    {
        ActionTarget = -1;
        Attacking = -1;
    }
    if (clearCandidates && SelectedCharacter == helperTargetIndex)
    {
        SelectedCharacter = -1;
    }

    m_iCurrentTarget = -1;
    m_iCurrentItem = MAX_ITEMS;
    m_iComboState = 0;
    manualYieldTicks_ = 0;
    chaseProgress_.Reset();

    if (clearCandidates)
    {
        {
            std::lock_guard lock(targetsLock_);
            m_setTargets.clear();
            m_setTargetsAttacking.clear();
        }
        {
            std::lock_guard lock(itemsLock_);
            m_setItems.clear();
        }
    }
}

void SessionMuHelperUnit::RefreshAutomationCandidates()
{
    std::set<int> visibleTargets;
    for (int index = 0; index < CharactersClient.Size(); ++index)
    {
        if (!CharactersClient.IsValidIndex(index))
        {
            continue;
        }
        auto sharedCharacterAccess = CharactersClient.AcquireSharedAccess(index);
        CHARACTER *target = &CharactersClient[index];
        if (target != Hero && IsMonster(target) && target->Dead == 0 && target->Object.Live &&
            ComputeDistanceFromTarget(target) <= m_iHuntingDistance)
        {
            visibleTargets.insert(target->Key);
        }
    }

    std::set<int> visibleItems;
    for (int index = 0; index < MAX_ITEMS; ++index)
    {
        if (Items[index].Object.Live)
        {
            visibleItems.insert(index);
        }
    }

    {
        std::lock_guard lock(targetsLock_);
        m_setTargets = std::move(visibleTargets);
        m_setTargetsAttacking.clear();
    }
    {
        std::lock_guard lock(itemsLock_);
        m_setItems = std::move(visibleItems);
    }
}

void SessionMuHelperUnit::RecoverAutomation()
{
    ResetRuntimeState(true);
    RefreshAutomationCandidates();
}

void SessionMuHelperUnit::WorkLoop(UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    if (!m_bActive)
    {
        return;
    }

    if (Hero->SafeZone)
    {
        g_ConsoleDebug.Write(MCD_NORMAL, L"[MU Helper] Entered safezone. Stopping.");
        TriggerStop();
        return;
    }

    if (ownsMovement_ && !Hero->Movement)
    {
        ownsMovement_ = false;
    }

    if (manualYieldTicks_ > 0)
    {
        --manualYieldTicks_;
        if (manualYieldTicks_ > 0)
        {
            return;
        }
        RecoverAutomation();
    }

    Work();

    if (m_iLoopCounter++ == 4)
    {
        m_iSecondsElapsed++;

        if (ComputeDistanceBetween({Hero->PositionX, Hero->PositionY}, m_posOriginal) > 1)
        {
            m_iSecondsAway++;
        }
        else
        {
            m_iSecondsAway = 0;
        }

        m_iLoopCounter = 0;
    }
}

void SessionMuHelperUnit::Work()
{
    try
    {
        if (!ActivatePet())
        {
            return;
        }

        if (!Buff())
        {
            return;
        }

        if (!RecoverHealth())
        {
            return;
        }

        if (!ObtainItem())
        {
            return;
        }

        if (!Regroup())
        {
            return;
        }

        Attack();

        RepairEquipments();
    }
    catch (...)
    {
        g_ConsoleDebug.Write(MCD_NORMAL, L"[MU Helper] Exception occurred. Ignoring...");
    }
}

void SessionMuHelperUnit::AddTarget(int iTargetId, bool bIsAttacking)
{
    if (!m_bActive)
    {
        return;
    }

    CHARACTER *pTarget = FindCharacterByKey(iTargetId);
    if (!pTarget || pTarget == Hero)
    {
        return;
    }

    const bool targetIsMonster = IsMonster(pTarget);
    if (targetIsMonster)
    {
        const int iDistance = ComputeDistanceFromTarget(pTarget);
        if ((iDistance <= m_iHuntingDistance) ||
            (bIsAttacking && m_config.bLongRangeCounterAttack))
        {
            targetsLock_.lock();

            m_setTargets.insert(iTargetId);

            if (bIsAttacking)
            {
                m_setTargetsAttacking.insert(iTargetId);
            }

            targetsLock_.unlock();
        }
    }

    // Self Defense reacts to player attacks only and never displaces a live
    // player target already being pursued.
    if (m_config.bUseSelfDefense && IsPlayer(pTarget) && manualYieldTicks_ == 0)
    {
        CHARACTER *currentTarget = FindCharacterByKey(m_iCurrentTarget);
        const bool targetingLivePlayer = IsPlayer(currentTarget) && currentTarget->Dead == 0 &&
                                         currentTarget->Object.Live;
        if (!targetingLivePlayer)
        {
            m_iCurrentTarget = iTargetId;
        }
    }
}

void SessionMuHelperUnit::DeleteTarget(int iTargetId)
{
    targetsLock_.lock();

    m_setTargets.erase(iTargetId);
    m_setTargetsAttacking.erase(iTargetId);

    targetsLock_.unlock();

    if (iTargetId == m_iCurrentTarget)
    {
        m_iCurrentTarget = -1;
        chaseProgress_.Reset();
    }
}

void SessionMuHelperUnit::DeleteAllTargets()
{
    ResetRuntimeState(true);
}

bool SessionMuHelperUnit::IsSelfDefenseTarget(int targetId) const noexcept
{
    return m_bActive && m_config.bUseSelfDefense && m_iCurrentTarget == targetId;
}

int SessionMuHelperUnit::ComputeDistanceByRange(int iRange)
{
    return ComputeDistanceBetween({0, 0}, {iRange, iRange});
}

int SessionMuHelperUnit::ComputeDistanceFromTarget(CHARACTER *pTarget)
{
    const POINT posHero = {Hero->PositionX, Hero->PositionY};

    const POINT posCurrent = {pTarget->PositionX, pTarget->PositionY};
    const POINT posNext = {pTarget->TargetX, pTarget->TargetY};

    return std::min(ComputeDistanceBetween(posHero, posCurrent),
                    ComputeDistanceBetween(posHero, posNext));
}

int SessionMuHelperUnit::ComputeDistanceBetween(POINT posA, POINT posB)
{
    int iDx = posA.x - posB.x;
    int iDy = posA.y - posB.y;

    return static_cast<int>(std::ceil(std::sqrt(iDx * iDx + iDy * iDy)));
}

int SessionMuHelperUnit::GetHuntingTarget()
{
    huntingCandidates_.clear();
    {
        std::lock_guard lock(targetsLock_);
        for (const int id : m_setTargets)
            huntingCandidates_.push_back({id, 0, 0, 0});
    }
    // Resolve character state after releasing the target-set lock.
    std::size_t count = 0;
    for (const auto candidate : huntingCandidates_)
    {
        CHARACTER *target = FindCharacterByKey(candidate.id);
        if (!IsMonster(target) || target->Dead > 0 || !target->Object.Live)
            continue;
        const int distance = ComputeDistanceFromTarget(target);
        if (distance <= m_iHuntingDistance)
            huntingCandidates_[count++] = {candidate.id, target->PositionX, target->PositionY,
                                           distance};
    }
    huntingCandidates_.resize(count);
    return Combat::SelectTarget(huntingCandidates_, m_config.bConcentratedMonsters,
                                huntingDensity_);
}

int SessionMuHelperUnit::GetFarthestAttackingTarget()
{
    int iFarthestMonsterId = -1;
    int iMaxDistance = -1;

    std::set<int> setTargets;
    {
        targetsLock_.lock();
        setTargets = m_setTargetsAttacking;
        targetsLock_.unlock();
    }

    for (const int &iMonsterId : setTargets)
    {
        int iIndex = FindCharacterIndex(iMonsterId);
        CHARACTER *pTarget = &CharactersClient[iIndex];

        if (!IsMonster(pTarget))
        {
            continue;
        }

        int iDistance = ComputeDistanceFromTarget(pTarget);
        if (iDistance > iMaxDistance)
        {
            iMaxDistance = iDistance;
            iFarthestMonsterId = iMonsterId;
        }
    }

    return iFarthestMonsterId;
}

void SessionMuHelperUnit::CleanupTargets()
{
    std::set<int> setTargets;
    {
        targetsLock_.lock();
        setTargets = m_setTargets;
        targetsLock_.unlock();
    }

    for (const int &iMonsterId : setTargets)
    {
        int iIndex = FindCharacterIndex(iMonsterId);
        if (!CharactersClient.IsValidIndex(iIndex))
        {
            DeleteTarget(iMonsterId);
            continue;
        }

        CHARACTER *pTarget = &CharactersClient[iIndex];
        if (pTarget->Dead > 0 || !pTarget->Object.Live)
        {
            DeleteTarget(iMonsterId);
        }
    }
}

int SessionMuHelperUnit::ActivatePet()
{
    if (!m_config.bUseDarkRaven)
    {
        return 1;
    }

    if (m_bPetActivated)
    {
        return 1;
    }

    if (m_config.iDarkRavenMode == PET_ATTACK_CEASE)
    {
        SocketClient->ToGameServer()->SendPetCommandRequest(PetType::DarkRaven,
                                                            PetCommandMode::Normal, 0xFFFF);
    }
    else if (m_config.iDarkRavenMode == PET_ATTACK_AUTO)
    {
        SocketClient->ToGameServer()->SendPetCommandRequest(PetType::DarkRaven,
                                                            PetCommandMode::AttackRandom, 0xFFFF);
    }
    else if (m_config.iDarkRavenMode == PET_ATTACK_TOGETHER)
    {
        SocketClient->ToGameServer()->SendPetCommandRequest(
            PetType::DarkRaven, PetCommandMode::AttackWithOwner, 0xFFFF);
    }

    m_bPetActivated = true;
    return 1;
}

int SessionMuHelperUnit::Buff()
{
    if (!HasAssignedBuffSkill())
    {
        return 1;
    }

    if (m_config.bSupportParty && g_pPartyManager->IsPartyActive())
    {
        m_iCurrentBuffPartyIndex %= PartyNumber;

        PARTY_t *pMember = &Party[m_iCurrentBuffPartyIndex];
        CHARACTER *pChar = g_pPartyManager->GetPartyMemberChar(pMember);
        int iBuffResult = 1;

        if (pChar != NULL && ComputeDistanceFromTarget(pChar) <= MAX_ACTIONABLE_DISTANCE)
        {
            if (!m_config.bBuffDurationParty && m_config.iBuffCastInterval != 0 &&
                m_iSecondsElapsed % m_config.iBuffCastInterval == 0)
            {
                m_bTimerActivatedBuffOngoing = true;
            }

            iBuffResult = BuffTarget(pChar, (ActionSkillType)m_config.aiBuff[m_iCurrentBuffIndex]);
        }

        m_iCurrentBuffPartyIndex = (m_iCurrentBuffPartyIndex + 1) % PartyNumber;

        if (m_iCurrentBuffPartyIndex == 0)
        {
            m_iCurrentBuffIndex = (m_iCurrentBuffIndex + 1) % m_config.aiBuff.size();

            // Reaching this branch means everyone's been buffed,
            // so we're resetting the timer activated buff flag
            if (m_iCurrentBuffIndex == 0)
            {
                m_bTimerActivatedBuffOngoing = false;
            }
        }

        return iBuffResult;
    }
    else
    {
        m_iCurrentBuffPartyIndex = 0;

        if (!m_config.bBuffDuration && m_config.iBuffCastInterval != 0 &&
            m_iSecondsElapsed % m_config.iBuffCastInterval == 0)
        {
            m_bTimerActivatedBuffOngoing = true;
        }

        if (!BuffTarget(Hero, (ActionSkillType)m_config.aiBuff[m_iCurrentBuffIndex]))
        {
            return 0;
        }
    }

    if (m_iCurrentBuffPartyIndex == 0)
    {
        m_iCurrentBuffIndex = (m_iCurrentBuffIndex + 1) % m_config.aiBuff.size();

        // Reaching this branch means everyone's been buffed,
        // so we're resetting the timer activated buff flag
        if (m_iCurrentBuffIndex == 0)
        {
            m_bTimerActivatedBuffOngoing = false;
        }
    }

    return 1;
}

int SessionMuHelperUnit::BuffTarget(CHARACTER *pTargetChar, ActionSkillType iBuffSkill)
{
    OBJECT *obj = &pTargetChar->Object;

    auto CastIfMissing = [&](bool bBuffActive, bool bTimerRespected, bool bNeedsTarget) -> int {
        if (!bBuffActive || (bTimerRespected && m_bTimerActivatedBuffOngoing))
            return SimulateSkill(iBuffSkill, bNeedsTarget, pTargetChar->Key);
        return 1;
    };

    switch (iBuffSkill)
    {
    case AT_SKILL_ATTACK:
    case AT_SKILL_ATTACK_STR:
        return CastIfMissing(g_isCharacterBuff(obj, eBuff_Attack), true, true);

    case AT_SKILL_DEFENSE:
    case AT_SKILL_DEFENSE_STR:
    case AT_SKILL_DEFENSE_MASTERY:
        return CastIfMissing(g_isCharacterBuff(obj, eBuff_Defense), true, true);

    case AT_SKILL_INFINITY_ARROW:
    case AT_SKILL_INFINITY_ARROW_STR:
        return CastIfMissing(g_isCharacterBuff(obj, eBuff_InfinityArrow), false, false);

    case AT_SKILL_SOUL_BARRIER:
    case AT_SKILL_SOUL_BARRIER_STR:
    case AT_SKILL_SOUL_BARRIER_PROFICIENCY:
        return CastIfMissing(g_isCharacterBuff(obj, eBuff_WizDefense), true, true);

    case AT_SKILL_SWELL_LIFE:
    case AT_SKILL_SWELL_LIFE_STR:
    case AT_SKILL_SWELL_LIFE_PROFICIENCY:
        if (m_iComboState == 2)
        {
            return 1;
        }
        return CastIfMissing(g_isCharacterBuff(obj, eBuff_Life), true, false);

    case AT_SKILL_EXPANSION_OF_WIZARDRY:
    case AT_SKILL_EXPANSION_OF_WIZARDRY_STR:
    case AT_SKILL_EXPANSION_OF_WIZARDRY_MASTERY:
        return CastIfMissing(g_isCharacterBuff(obj, eBuff_SwellOfMagicPower), false, false);

    case AT_SKILL_ADD_CRITICAL:
    case AT_SKILL_ADD_CRITICAL_STR1:
    case AT_SKILL_ADD_CRITICAL_STR2:
    case AT_SKILL_ADD_CRITICAL_STR3:
        return CastIfMissing(g_isCharacterBuff(obj, eBuff_AddCriticalDamage), false, false);

    case AT_SKILL_ALICE_BERSERKER:
    case AT_SKILL_ALICE_BERSERKER_STR:
        return CastIfMissing(g_isCharacterBuff(obj, eBuff_Berserker), false, false);

    case AT_SKILL_ALICE_THORNS:
        return CastIfMissing(g_isCharacterBuff(obj, eBuff_Thorns), false, false);

    // Rage Fighter party buffs — self/party AoE, no explicit target needed.
    case AT_SKILL_ATT_UP_OURFORCES:
        return CastIfMissing(g_isCharacterBuff(obj, eBuff_Att_up_Ourforces), true, false);

    case AT_SKILL_HP_UP_OURFORCES:
    case AT_SKILL_HP_UP_OURFORCES_STR:
        return CastIfMissing(g_isCharacterBuff(obj, eBuff_Hp_up_Ourforces), true, false);

    case AT_SKILL_DEF_UP_OURFORCES:
    case AT_SKILL_DEF_UP_OURFORCES_STR:
    case AT_SKILL_DEF_UP_OURFORCES_MASTERY:
        return CastIfMissing(g_isCharacterBuff(obj, eBuff_Def_up_Ourforces), true, false);

    default:
        return 1;
    }
}

int SessionMuHelperUnit::ConsumePotion()
{
    int64_t iLife = CharacterAttribute->Life;
    int64_t iLifeMax = CharacterAttribute->LifeMax;

    if (m_config.bUseHealPotion && iLifeMax > 0 && iLife > 0)
    {
        int64_t iRemaining = (iLife * 100 + iLifeMax - 1) / iLifeMax;
        if (iRemaining <= m_config.iPotionThreshold)
        {
            int iPotionIndex = sessionKeeper_.GameData()->FindHealingItemIndex();
            if (iPotionIndex != -1)
            {
                SendRequestUse(iPotionIndex, 0);
            }
        }
    }

    return 1;
}

int SessionMuHelperUnit::RecoverHealth()
{
    if (!Heal())
    {
        return 0;
    }

    if (!DrainLife())
    {
        return 0;
    }

    if (!ConsumePotion())
    {
        return 0;
    }

    return 1;
}

int SessionMuHelperUnit::Heal()
{
    if (!m_config.bAutoHeal)
    {
        return 1;
    }

    auto iHealingSkill = GetHealingSkill();
    if (iHealingSkill == AT_SKILL_UNDEFINED)
    {
        return 1;
    }

    if (m_config.bAutoHealParty && g_pPartyManager->IsPartyActive())
    {
        m_iCurrentHealPartyIndex %= PartyNumber;

        PARTY_t *pMember = &Party[m_iCurrentHealPartyIndex];
        CHARACTER *pChar = g_pPartyManager->GetPartyMemberChar(pMember);
        int iHealResult = 1;

        if (pChar != NULL)
        {
            if (pChar == Hero)
            {
                iHealResult = HealSelf(iHealingSkill);
            }
            else if (pMember->stepHP * 10 <= m_config.iHealPartyThreshold &&
                     ComputeDistanceFromTarget(pChar) <= MAX_ACTIONABLE_DISTANCE)
            {
                iHealResult = SimulateSkill(iHealingSkill, true, pChar->Key);
            }
        }

        m_iCurrentHealPartyIndex = (m_iCurrentHealPartyIndex + 1) % PartyNumber;

        return iHealResult;
    }
    else
    {
        m_iCurrentHealPartyIndex = 0;
        return HealSelf(iHealingSkill);
    }

    return 1;
}

int SessionMuHelperUnit::HealSelf(ActionSkillType iHealingSkill)
{
    int64_t iLife = CharacterAttribute->Life;
    int64_t iLifeMax = CharacterAttribute->LifeMax;
    int64_t iRemaining = (iLife * 100 + iLifeMax - 1) / iLifeMax;

    if (iRemaining <= m_config.iHealThreshold)
    {
        return SimulateSkill(iHealingSkill, true, HeroKey);
    }

    return 1;
}

int SessionMuHelperUnit::DrainLife()
{
    if (!m_config.bUseDrainLife)
    {
        return 1;
    }

    auto iDrainLife = GetDrainLifeSkill();
    if (iDrainLife == AT_SKILL_UNDEFINED)
    {
        return 1;
    }

    int64_t iLife = CharacterAttribute->Life;
    int64_t iLifeMax = CharacterAttribute->LifeMax;
    int64_t iRemaining = (iLife * 100 + iLifeMax - 1) / iLifeMax;

    if (iRemaining <= m_config.iHealThreshold)
    {
        if (AcquireAttackTarget())
        {
            return SimulateSkill(iDrainLife, true, m_iCurrentTarget);
        }
    }

    return 1;
}

int SessionMuHelperUnit::RepairEquipments()
{
    if (m_config.bRepairItem)
    {
        for (int i = 0; i < MAX_EQUIPMENT; i++)
        {
            ITEM *pItem = &CharacterMachine->Equipment[i];
            if (!pItem || pItem->Type == -1)
            {
                continue;
            }

            ITEM_ATTRIBUTE *pAttr = &ItemAttribute[pItem->Type];
            if (!pAttr)
            {
                continue;
            }

            int iLevel = pItem->Level;
            int iDurability = pItem->Durability;
            int iMaxDurability = CalcMaxDurability(pItem, pAttr, iLevel);

            int64_t iHealth = (iDurability * 100 + iMaxDurability - 1) / iMaxDurability;

            if (iHealth <= DEFAULT_DURABILITY_THRESHOLD)
            {
                int64_t iGoldCost = CalcSelfRepairCost(ItemValue(pItem, 2), iDurability,
                                                       iMaxDurability, pItem->Type);
                if (iGoldCost <= CharacterMachine->Gold)
                {
                    SocketClient->ToGameServer()->SendRepairItemRequest(i, 1);
                }
            }
        }
    }

    return 1;
}

bool SessionMuHelperUnit::AcquireAttackTarget()
{
    if (m_iCurrentTarget != -1)
    {
        CHARACTER *currentTarget = FindCharacterByKey(m_iCurrentTarget);
        if ((!IsMonster(currentTarget) && !IsPlayer(currentTarget)) || currentTarget->Dead > 0 ||
            !currentTarget->Object.Live)
        {
            DeleteTarget(m_iCurrentTarget);
        }
    }

    if (m_iCurrentTarget == -1)
    {
        if (!m_setTargets.empty())
        {
            CleanupTargets();

            if (m_config.bLongRangeCounterAttack)
            {
                m_iCurrentTarget = GetFarthestAttackingTarget();
            }

            if (m_iCurrentTarget == -1)
            {
                m_iCurrentTarget = GetHuntingTarget();
            }
        }
        else
        {
            m_iComboState = 0;
            return 0;
        }
    }

    return m_iCurrentTarget != -1;
}

int SessionMuHelperUnit::Attack()
{
    if (!AcquireAttackTarget())
        return 0;
    // Chase first. Do not select a skill or check cooldown/mana/conditions
    // until the target is adjacent, including for Combo and ranged skills.
    if (m_config.bUseSkillsClosely &&
        !ApproachAttackTarget(m_iCurrentTarget, Combat::CloseAttackRange))
        return 0;

    if (m_config.bUseCombo)
    {
        return SimulateComboAttack();
    }

    m_iCurrentSkill = SelectAttackSkill();
    if (m_iCurrentSkill > AT_SKILL_UNDEFINED)
    {
        const float fSkillDistance = gSkillManager.GetSkillDistance(m_iCurrentSkill, Hero);
        if (gameplay_.CanExecuteSkill(Hero, m_iCurrentSkill, fSkillDistance))
        {
            return SimulateAttack(m_iCurrentSkill);
        }
    }

    if (m_config.bFallbackBasicAttack)
    {
        if (!Hero->Movement)
        {
            return SimulateBasicAttack(m_iCurrentTarget);
        }
    }

    return 1;
}

ActionSkillType SessionMuHelperUnit::SelectAttackSkill()
{
    const size_t safeSize = std::min({m_config.aiSkill.size(), m_config.aiSkillCondition.size(),
                                      m_config.aiSkillInterval.size()});
    for (int i = 1; i < (int)safeSize; i++)
    {
        const int iSkillId = m_config.aiSkill[i];
        if (iSkillId <= 0 || iSkillId >= MAX_SKILLS)
        {
            continue;
        }

        if ((m_config.aiSkillCondition[i] & ON_TIMER) && m_config.aiSkillInterval[i] != 0 &&
            m_iSecondsElapsed > 0 && m_iSecondsElapsed % m_config.aiSkillInterval[i] == 0)
        {
            return (ActionSkillType)iSkillId;
        }

        if (m_config.aiSkillCondition[i] & ON_CONDITION)
        {
            int iCount = 0;
            if (m_config.aiSkillCondition[i] & ON_MOBS_NEARBY)
            {
                iCount = (int)m_setTargets.size();
            }
            else if (m_config.aiSkillCondition[i] & ON_MOBS_ATTACKING)
            {
                iCount = (int)m_setTargetsAttacking.size();
            }
            else
            {
                continue;
            }

            if (((m_config.aiSkillCondition[i] & ON_MORE_THAN_TWO_MOBS) && iCount >= 2) ||
                ((m_config.aiSkillCondition[i] & ON_MORE_THAN_THREE_MOBS) && iCount >= 3) ||
                ((m_config.aiSkillCondition[i] & ON_MORE_THAN_FOUR_MOBS) && iCount >= 4) ||
                ((m_config.aiSkillCondition[i] & ON_MORE_THAN_FIVE_MOBS) && iCount >= 5))
            {
                return (ActionSkillType)iSkillId;
            }
        }
    }

    if (m_config.aiSkill[0] > 0)
    {
        return (ActionSkillType)m_config.aiSkill[0];
    }

    return AT_SKILL_UNDEFINED;
}

int SessionMuHelperUnit::SimulateComboAttack()
{
    for (int i = 0; i < m_config.aiSkill.size(); i++)
    {
        if (m_config.aiSkill[i] == 0)
        {
            return 0;
        }
    }

    if (SimulateAttack((ActionSkillType)m_config.aiSkill[m_iComboState]))
    {
        m_iComboState = (m_iComboState + 1) % 3;
    }

    return 1;
}

// True while the hero is mid swing; gating helper actions on it makes the
// bot's cadence follow AttackSpeed instead of the fixed helper timer, the
// same way the manual click path gates in MoveHero (ZzzInterface.cpp).
bool SessionMuHelperUnit::IsHeroSwingInProgress() const
{
    const int iAction = Hero->Object.CurrentAction;

    // Outside the swing enum range entirely -> not a swing.
    if (!Engine::Object::IsAttackAction(iAction))
        return false;

    // Several non-swing *stance* animations (mounted idle/walk/run, two-hand-
    // sword stance, ride-horse, rage-fenrir) share the [PLAYER_ATTACK_FIST ..
    // PLAYER_RIDE_SKILL] enum range that IsAttackAction() spans. MoveHero
    // (ZzzInterface.cpp) OR-excludes exactly these four ranges when deciding
    // whether the hero may move; mirror that here. Otherwise a Fenrir-mounted
    // idle character (CurrentAction == PLAYER_FENRIR_STAND, inside the range)
    // reads as a perpetual swing, IsHeroSwingInProgress() never clears, and
    // SimulateSkill()/SimulateAttack() never fire -- the auto-helper is dead
    // for the whole session while Horn of Fenrir (or any mount) is equipped.
    if ((iAction >= PLAYER_STOP_TWO_HAND_SWORD_TWO && iAction <= PLAYER_RUN_TWO_HAND_SWORD_TWO) ||
        (iAction >= PLAYER_DARKLORD_STAND && iAction <= PLAYER_RUN_RIDE_HORSE) ||
        (iAction >= PLAYER_FENRIR_RUN && iAction <= PLAYER_FENRIR_WALK_ONE_LEFT) ||
        (iAction >= PLAYER_RAGE_FENRIR_WALK && iAction <= PLAYER_RAGE_FENRIR_STAND_ONE_LEFT))
        return false;

    // Genuine attack/skill swing -> Fenrir attack/skill actions sit below
    // PLAYER_FENRIR_RUN, so they stay gated and cadence still tracks
    // AttackSpeed when mounted.
    return true;
}

int SessionMuHelperUnit::SimulateAttack(ActionSkillType iSkill)
{
    return SimulateSkill(iSkill, true, m_iCurrentTarget);
}

void SessionMuHelperUnit::AdvanceAttackPath(const PATH_t &path)
{
    constexpr int ChaseStepCount = 2;
    std::lock_guard lock(Hero->Path.Lock);
    const int count = std::min<int>(path.PathNum, ChaseStepCount);
    for (int i = 0; i < count; ++i)
    {
        Hero->Path.PathX[i] = path.PathX[i];
        Hero->Path.PathY[i] = path.PathY[i];
    }
    Hero->Path.PathNum = count;
    Hero->Path.CurrentPath = 0;
    Hero->Path.CurrentPathFloat = 0;
}

void SessionMuHelperUnit::SendAutomatedMove()
{
    ownsMovement_ = true;
    SendMove(Hero, &Hero->Object);
}

void SessionMuHelperUnit::CancelAutomatedMovement()
{
    if (!ownsMovement_ || !Hero)
    {
        ownsMovement_ = false;
        return;
    }

    {
        std::lock_guard lock(Hero->Path.Lock);
        Hero->Path.PathNum = 0;
        Hero->Path.CurrentPath = 0;
        Hero->Path.CurrentPathFloat = 0;
    }
    Hero->Movement = false;
    SetPlayerStop(Hero);
    ownsMovement_ = false;
}

bool SessionMuHelperUnit::ChaseHasStalled(int target)
{
    return chaseProgress_.Record(target, {Hero->PositionX, Hero->PositionY});
}

bool SessionMuHelperUnit::ApproachAttackTarget(int targetId, float distance)
{
    CHARACTER *target = FindCharacterByKey(targetId);
    if (!target || target->Dead > 0 || !target->Object.Live)
    {
        DeleteTarget(targetId);
        return false;
    }
    // Party healing/buffs keep their normal range. A player selected by Self
    // Defense is the current attack target and must be approached normally.
    if (!IsMonster(target) && targetId != m_iCurrentTarget)
        return true;
    SelectedCharacter = FindCharacterIndex(targetId);
    TargetX = static_cast<int>(target->Object.Position[0] / TERRAIN_SCALE);
    TargetY = static_cast<int>(target->Object.Position[1] / TERRAIN_SCALE);
    if (CheckTile(Hero, &Hero->Object, distance))
    {
        chaseProgress_.Reset();
        return true;
    }
    if (IsHeroSwingInProgress())
        return false;

    PATH_t path;
    if (!PathFinding2(Hero->PositionX, Hero->PositionY, TargetX, TargetY, &path,
                      m_iHuntingDistance + distance))
    {
        RecoverAutomation();
        return false;
    }
    if (ChaseHasStalled(targetId))
    {
        RecoverAutomation();
        return false;
    }
    Hero->MovementType = MOVEMENT_MOVE;
    AdvanceAttackPath(path);
    SendAutomatedMove();
    return false;
}

int SessionMuHelperUnit::SimulateSkill(ActionSkillType iSkill, bool bTargetRequired, int iTarget)
{
    // Let the current swing finish before issuing another action, so the
    // cadence tracks AttackSpeed instead of the fixed helper timer.
    if (IsHeroSwingInProgress())
    {
        return 0;
    }

    if (m_config.bUseSkillsClosely && bTargetRequired &&
        !ApproachAttackTarget(iTarget, Combat::CloseAttackRange))
        return 0;

    g_MovementSkill.m_iSkill = iSkill;
    g_MovementSkill.m_bMagic = true;

    const float fSkillDistance = gSkillManager.GetSkillDistance(iSkill, Hero);
    const bool bSelfPositionSkill = IsSelfPositionSkill(iSkill);

    if (bTargetRequired)
    {
        if (bSelfPositionSkill)
        {
            TargetX = Hero->PositionX;
            TargetY = Hero->PositionY;

            g_MovementSkill.m_iTarget = -1;

            // Check if current target is still valid (exists and alive)
            if (iTarget != -1)
            {
                const int iCharIndex = FindCharacterIndex(iTarget);
                if (CharactersClient.IsValidIndex(iCharIndex))
                {
                    CHARACTER *pCurrentTarget = &CharactersClient[iCharIndex];
                    if (pCurrentTarget->Dead > 0 ||
                        (!IsMonster(pCurrentTarget) && !IsPlayer(pCurrentTarget)))
                    {
                        DeleteTarget(iTarget);
                        return 0;
                    }

                    if (!ApproachAttackTarget(iTarget, fSkillDistance))
                        return 0;
                }
                else
                {
                    DeleteTarget(iTarget);
                    return 0;
                }
            }
        }
        else
        {
            if (iTarget == -1)
            {
                return 0;
            }

            const int iCharIndex = FindCharacterIndex(iTarget);
            if (!CharactersClient.IsValidIndex(iCharIndex))
            {
                DeleteTarget(iTarget);
                return 0;
            }

            SelectedCharacter = iCharIndex;

            CHARACTER *pTarget = &CharactersClient[iCharIndex];
            if (pTarget->Dead > 0)
            {
                DeleteTarget(iTarget);
                return 0;
            }

            g_MovementSkill.m_iTarget = iCharIndex;

            TargetX = (int)(pTarget->Object.Position[0] / TERRAIN_SCALE);
            TargetY = (int)(pTarget->Object.Position[1] / TERRAIN_SCALE);

            const bool bTargetNear = CheckTile(Hero, &Hero->Object, fSkillDistance);
            if (bTargetNear && !CheckWall(Hero->PositionX, Hero->PositionY, TargetX, TargetY))
            {
                RecoverAutomation();
                return 0;
            }

            // Target is not yet in range, move closer.
            if (!bTargetNear)
            {
                PATH_t tempPath;
                if (!PathFinding2(Hero->PositionX, Hero->PositionY, TargetX, TargetY, &tempPath,
                                  m_iHuntingDistance + fSkillDistance))
                {
                    RecoverAutomation();
                    return 0;
                }
                if (ChaseHasStalled(iTarget))
                {
                    RecoverAutomation();
                    return 0;
                }
                AdvanceAttackPath(tempPath);
                SendAutomatedMove();
                return 0;
            }
            chaseProgress_.Reset();
        }
    }
    else
    {
        TargetX = Hero->PositionX;
        TargetY = Hero->PositionY;
    }

    int iSkillResult = sessionKeeper_.Gameplay()->ExecuteSkill(Hero, iSkill, fSkillDistance);
    if (iSkillResult == -1 && iTarget != -1)
    {
        DeleteTarget(iTarget);
    }

    return (int)(iSkillResult == 1);
}

int SessionMuHelperUnit::SimulateBasicAttack(int iTarget)
{
    if (iTarget == -1)
    {
        return 0;
    }

    // Let the current swing finish before attacking again, so the cadence
    // tracks AttackSpeed instead of the fixed helper timer.
    if (IsHeroSwingInProgress())
    {
        return 0;
    }

    const int iCharIndex = FindCharacterIndex(iTarget);
    if (!CharactersClient.IsValidIndex(iCharIndex))
    {
        DeleteTarget(iTarget);
        return 0;
    }

    CHARACTER *pTarget = &CharactersClient[iCharIndex];
    if (pTarget->Dead > 0 || (!IsMonster(pTarget) && !IsPlayer(pTarget)))
    {
        DeleteTarget(iTarget);
        return 0;
    }

    constexpr float BASIC_RANGE_DEFAULT = 1.8f;
    constexpr float BASIC_RANGE_SPEAR = 2.2f;
    constexpr float BASIC_RANGE_BOW = 6.0f;

    float fRange = BASIC_RANGE_DEFAULT;
    const int iWeaponRight = CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT].Type;
    if (iWeaponRight >= ITEM_SPEAR && iWeaponRight < ITEM_SPEAR + MAX_ITEM_INDEX)
    {
        fRange = BASIC_RANGE_SPEAR;
    }
    if (gameplay_.GetEquipedBowType() != BOWTYPE_NONE)
    {
        fRange = BASIC_RANGE_BOW;
    }

    SelectedCharacter = iCharIndex;
    TargetX = (int)(pTarget->Object.Position[0] / TERRAIN_SCALE);
    TargetY = (int)(pTarget->Object.Position[1] / TERRAIN_SCALE);

    const bool bTargetNear = CheckTile(Hero, &Hero->Object, fRange);
    if (bTargetNear && !CheckWall(Hero->PositionX, Hero->PositionY, TargetX, TargetY))
    {
        RecoverAutomation();
        return 0;
    }

    // Target is not yet in range, move closer.
    if (!bTargetNear)
    {
        PATH_t tempPath;
        if (!PathFinding2(Hero->PositionX, Hero->PositionY, TargetX, TargetY, &tempPath,
                          m_iHuntingDistance + fRange))
        {
            RecoverAutomation();
            return 0;
        }
        if (ChaseHasStalled(iTarget))
        {
            RecoverAutomation();
            return 0;
        }
        AdvanceAttackPath(tempPath);
        SendAutomatedMove();
        return 0;
    }
    chaseProgress_.Reset();

    if (gameplay_.GetEquipedBowType() != BOWTYPE_NONE && !CheckArrow())
    {
        return 0;
    }

    Hero->MovementType = MOVEMENT_ATTACK;
    ActionTarget = iCharIndex;
    Attacking = 1;
    Action(Hero, &Hero->Object, true);
    return 1;
}

int SessionMuHelperUnit::Regroup()
{
    if (m_config.bReturnToOriginalPosition && m_iSecondsAway > m_config.iMaxSecondsAway)
    {
        if (!SimulateMove(m_posOriginal))
        {
            return 0;
        }

        m_iSecondsAway = 0;
        m_iComboState = 0;
        m_iCurrentTarget = -1;
    }

    return 1;
}

int SessionMuHelperUnit::SimulateMove(POINT posMove)
{
    Hero->MovementType = MOVEMENT_MOVE;
    TargetX = (int)posMove.x;
    TargetY = (int)posMove.y;

    if (!CheckTile(Hero, &Hero->Object, 1.5f))
    {
        if (PathFinding2((Hero->PositionX), (Hero->PositionY), TargetX, TargetY, &Hero->Path))
        {
            SendAutomatedMove();
        }
        return 0;
    }

    return 1;
}

bool SessionMuHelperUnit::HasAssignedBuffSkill()
{
    for (int i = 0; i < m_config.aiBuff.size(); i++)
    {
        if (m_config.aiBuff[i] != 0)
        {
            return true;
        }
    }

    return false;
}

ActionSkillType SessionMuHelperUnit::GetHealingSkill()
{
    std::vector<ActionSkillType> aiHealingSkills = {
        AT_SKILL_HEALING,
        AT_SKILL_HEALING_STR,
    };

    for (int i = 0; i < aiHealingSkills.size(); i++)
    {
        int iSkillIndex = gSkillManager.GetSkillIndex(aiHealingSkills[i]);
        if (iSkillIndex != -1)
        {
            return aiHealingSkills[i];
        }
    }

    return AT_SKILL_UNDEFINED;
}

// Matches AttackWizard() behavior in ZzzInterface.cpp for these skill IDs.
bool SessionMuHelperUnit::IsSelfPositionSkill(ActionSkillType iSkill)
{
    return (iSkill == AT_SKILL_NOVA_BEGIN || iSkill == AT_SKILL_NOVA ||
            iSkill == AT_SKILL_HELL_FIRE || iSkill == AT_SKILL_HELL_FIRE_STR ||
            iSkill == AT_SKILL_INFERNO || iSkill == AT_SKILL_INFERNO_STR ||
            iSkill == AT_SKILL_INFERNO_STR_MG);
}

ActionSkillType SessionMuHelperUnit::GetDrainLifeSkill()
{
    std::vector<ActionSkillType> aiDrainLifeSkills = {AT_SKILL_ALICE_DRAINLIFE,
                                                      AT_SKILL_ALICE_DRAINLIFE_STR};

    for (int i = 0; i < aiDrainLifeSkills.size(); i++)
    {
        int iSkillIndex = gSkillManager.GetSkillIndex(aiDrainLifeSkills[i]);
        if (iSkillIndex != -1)
        {
            return aiDrainLifeSkills[i];
        }
    }

    return AT_SKILL_UNDEFINED;
}

int SessionMuHelperUnit::ObtainItem()
{
    if (m_iCurrentItem == MAX_ITEMS)
    {
        m_iCurrentItem = SelectItemToObtain();
        if (m_iCurrentItem == MAX_ITEMS)
        {
            return 1;
        }
    }

    ITEM_t *pDrop = &Items[m_iCurrentItem];

    if (!pDrop->Object.Live)
    {
        DeleteItem(m_iCurrentItem);
        return 1;
    }

    TargetX = (int)(Items[m_iCurrentItem].Object.Position[0] / TERRAIN_SCALE);
    TargetY = (int)(Items[m_iCurrentItem].Object.Position[1] / TERRAIN_SCALE);

    int iDistance = ComputeDistanceBetween({Hero->PositionX, Hero->PositionY}, {TargetX, TargetY});
    if (iDistance <= m_iObtainingDistance)
    {
        if (!CheckTile(Hero, &Hero->Object, 2.0f))
        {
            if (PathFinding2((Hero->PositionX), (Hero->PositionY), TargetX, TargetY, &Hero->Path))
            {
                SendAutomatedMove();
            }

            return 0;
        }
        else
        {
            if (SendGetItem == -1)
            {
                SendGetItem = m_iCurrentItem;
                SocketClient->ToGameServer()->SendPickupItemRequest(m_iCurrentItem);
                DeleteItem(m_iCurrentItem);
            }
        }
    }

    return 1;
}

bool SessionMuHelperUnit::ShouldObtainItem(int iItemId)
{
    ITEM_t *pDrop = &Items[iItemId];
    ITEM *pItem = &pDrop->Item;

    if ((m_config.bPickZen && IsMoneyItem(pItem)) || (m_config.bPickJewel && IsJewelItem(pItem)) ||
        (m_config.bPickAncient && IsAncientItem(pItem)) ||
        (m_config.bPickExcellent && IsExcellentItem(pItem)))
    {
        return true;
    }

    if (m_config.bPickExtraItems)
    {
        std::wstring strDisplayName = GetItemDisplayName(pItem);

        for (const auto &str : m_config.aExtraItems)
        {
            // Check if the search keyword is in the item's display name
            if (strDisplayName.find(str) != std::wstring::npos)
            {
                return true;
            }
        }
    }

    return m_config.bPickAllItems;
}

void SessionMuHelperUnit::AddItem(int iItemId, POINT posWhere)
{
    itemsLock_.lock();
    m_setItems.insert(iItemId);
    itemsLock_.unlock();
}

void SessionMuHelperUnit::DeleteItem(int iItemId)
{
    itemsLock_.lock();
    m_setItems.erase(iItemId);
    itemsLock_.unlock();

    if (iItemId == m_iCurrentItem)
    {
        m_iCurrentItem = MAX_ITEMS;
    }
}

int SessionMuHelperUnit::SelectItemToObtain()
{
    int iClosestItemId = MAX_ITEMS;
    int iMinDistance = m_config.iObtainingRange;

    std::set<int> setItems;
    {
        itemsLock_.lock();
        setItems = m_setItems;
        itemsLock_.unlock();
    }

    for (const int &iItemId : setItems)
    {
        if (!ShouldObtainItem(iItemId))
        {
            continue;
        }

        int iItemX = (int)(Items[iItemId].Object.Position[0] / TERRAIN_SCALE);
        int iItemY = (int)(Items[iItemId].Object.Position[1] / TERRAIN_SCALE);

        int iDistance =
            ComputeDistanceBetween({Hero->PositionX, Hero->PositionY}, {iItemX, iItemY});
        if (iDistance <= iMinDistance)
        {
            iMinDistance = iDistance;
            iClosestItemId = iItemId;
        }
    }

    return iClosestItemId;
}
} // namespace MUHelper
