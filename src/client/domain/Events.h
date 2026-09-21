#pragma once
#include "data/GameData.h"
#include "domain/MapSimulation.h"
#include "domain/WorldSimulation.h"
#include "session/SessionNetwork.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <list>
#include <span>
#include <string>
#include <vector>

#define g_TimeController SEASON3A::TimeController::GetInstance()
#define MAX_BLOOD_CASTLE_MEN 10
#define MAX_CHAOS_CASTLE_MEN 100
#define BATTLE_CASTLE_WALL1 61
#define BATTLE_CASTLE_WALL2 62
#define BATTLE_CASTLE_WALL3 63
#define BATTLE_CASTLE_WALL4 64
#define MAX_DEVIL_SQUARE_ENTER 6
#define BLOODCASTLE_QUEST_NUM 3

void ClearChaosCastleHelper(CHARACTER *c);

class SessionKeeper;
class CMapManager;

using MatchClock = std::chrono::steady_clock;

enum MATCH_TYPE
{
    TYPE_MATCH_NONE = 0,
    TYPE_MATCH_DEVIL_ENTER_START,
    TYPE_MATCH_DEVIL_ENTER_CLOSE,
    TYPE_MATCH_DEVIL_CLOSE,
    TYPE_MATCH_CASTLE_ENTER_CLOSE,
    TYPE_MATCH_CASTLE_INFILTRATION,
    TYPE_MATCH_CASTLE_CLOSE,
    TYPE_MATCH_CASTLE_END,
    TYPE_MATCH_CHAOS_ENTER_START = 11,
    TYPE_MATCH_CHAOS_EINFILTRATION,
    TYPE_MATCH_CHAOS_CLOSE,
    TYPE_MATCH_CHAOS_END,
    TYPE_MATCH_CURSEDTEMPLE_ENTER_CLOSE,
    TYPE_MATCH_CURSEDTEMPLE_GAME_START,
    TYPE_MATCH_DOPPELGANGER_ENTER_CLOSE,
    TYPE_MATCH_DOPPELGANGER_GAME_START,
    TYPE_MATCH_DOPPELGANGER_ICEWALKER,
    TYPE_MATCH_DOPPELGANGER_CLOSE,
    TYPE_MATCH_END
};

typedef struct MatchResult
{
    BYTE m_lpID[MAX_USERNAME_SIZE];
    DWORD m_iScore;
    DWORD m_dwExp;
    DWORD m_iZen;
} MatchResult;

struct DirectionMonster
{
    int m_Index;
    int m_iActionCheck;
    bool m_bAngleCheck;
};

namespace SEASON3A
{
enum eCursedTempleState
{
    eCursedTempleState_None = 0,
    eCursedTempleState_Wait,
    eCursedTempleState_Ready,
    eCursedTempleState_Play,
    eCursedTempleState_End,
};
enum eCursedTempleTeam
{
    eTeam_Allied = 0,
    eTeam_Illusion,
    eTeam_Count,
};
} // namespace SEASON3A

class CSBaseMatch : protected SessionUiLegacyBindings
{
  protected:
    std::uint8_t m_byMatchEventType;
    MATCH_TYPE m_iMatchCountDownType;
    MatchClock::time_point m_matchCountDownStart;
    std::uint8_t m_byMatchType;
    int m_iMatchTimeMax;
    int m_iMatchTime;

    int m_iMaxKillMonster;
    int m_iKillMonster;

    int m_iNumResult;
    int m_iMyResult;
    MatchResult m_MatchResult[11];

    POINT m_PosResult;

    bool getEqualMonster(int addV);

    void renderOnlyTime(float x, float y, int MatchTime);

  public:
    explicit CSBaseMatch(SessionKeeper &keeper);
    virtual ~CSBaseMatch() {};

    void clearMatchInfo(void);
    std::uint8_t GetMatchEventType(void)
    {
        return m_byMatchEventType;
    }

    std::uint8_t GetMatchType()
    {
        return m_byMatchType;
    }
    int GetMatchTime()
    {
        return m_iMatchTime;
    }
    int GetMatchMaxTime()
    {
        return m_iMatchTimeMax;
    };
    int GetNumMustKillMonster()
    {
        return m_iMaxKillMonster;
    }
    int GetNumKillMonster()
    {
        return m_iKillMonster;
    }

    void SetPosition(int ix, int iy);
    void StartMatchCountDown(int iType);
    void SetMatchInfo(std::uint8_t byType, int iMaxTime, int iTime, int iMaxMonster = 0,
                      int iKillMonster = 0);

    void Update();
    void UpdateCountdown(MatchClock::time_point now = MatchClock::now());
    void RenderTime(void);
    virtual void UpdateMatchState(void) = 0;

    virtual void SetMatchGameCommand(const LPPRECEIVE_MATCH_GAME_STATE data) = 0;
    virtual void SetMatchResult(const int iNumDevilRank, const int iMyRank,
                                const MatchResult *pMatchResult, const int Success = false) = 0;
    virtual void RenderMatchResult(void) = 0;
};

class CSDevilSquareMatch : public CSBaseMatch
{
  private:
  public:
    explicit CSDevilSquareMatch(SessionKeeper &keeper) : CSBaseMatch(keeper)
    {
    }
    virtual ~CSDevilSquareMatch() {};

    virtual void UpdateMatchState(void);

    virtual void SetMatchGameCommand(const LPPRECEIVE_MATCH_GAME_STATE data);
    virtual void SetMatchResult(const int iNumDevilRank, const int iMyRank,
                                const MatchResult *pMatchResult, const int Success = false);
    virtual void RenderMatchResult(void);
};

class CCursedTempleMatch : public CSBaseMatch
{
  private:
  public:
    explicit CCursedTempleMatch(SessionKeeper &keeper) : CSBaseMatch(keeper)
    {
    }
    virtual ~CCursedTempleMatch() {};

    virtual void UpdateMatchState(void);

    virtual void SetMatchGameCommand(const LPPRECEIVE_MATCH_GAME_STATE data);
    virtual void SetMatchResult(const int iNumDevilRank, const int iMyRank,
                                const MatchResult *pMatchResult, const int Success = false);
    virtual void RenderMatchResult(void);
};

class CDoppelGangerMatch : public CSBaseMatch
{
  private:
  public:
    explicit CDoppelGangerMatch(SessionKeeper &keeper) : CSBaseMatch(keeper)
    {
    }
    virtual ~CDoppelGangerMatch() {};

    virtual void UpdateMatchState(void)
    {
    }

    virtual void SetMatchGameCommand(const LPPRECEIVE_MATCH_GAME_STATE data)
    {
    }
    virtual void SetMatchResult(const int iNumDevilRank, const int iMyRank,
                                const MatchResult *pMatchResult, const int Success = false)
    {
    }
    virtual void RenderMatchResult(void)
    {
    }
};

class CDirection;

class CMVP1STDirection : protected SessionLegacyCalls
{
  private:
    CDirection &g_Direction;
    bool m_bTimerCheck;

    void IsCryWolfDirectionTimer();

    void MoveBeginDirection(double worldTime);
    void BeginDirection0();
    void BeginDirection1(double worldTime);
    void BeginDirection2(double worldTime);
    void BeginDirection3();
    void BeginDirection4(double worldTime);
    void BeginDirection5(double worldTime);

    bool IsCrywolfWorldActive(int world) const;
    bool IsSequenceReady() const;
    void ResetSequence();
    void QueueCameraMove(int x, int y, int z, float speed);

  public:
    int m_iCryWolfState;

    CMVP1STDirection(SessionKeeper &keeper, CDirection &direction);
    virtual ~CMVP1STDirection();

    void Init();
    void GetCryWolfState(std::uint8_t CryWolfState);
    bool IsCryWolfDirection() const;
    void CryWolfDirection(int world, double worldTime);
};

class CDirection;

class CKanturuDirection
{
  public:
    int m_iKanturuState;
    int m_iMayaState;
    int m_iNightmareState;
    bool m_bKanturuDirection;

    explicit CKanturuDirection(CDirection &direction);
    virtual ~CKanturuDirection();

    void Init();
    bool IsKanturuDirection() const;
    bool IsKanturu3rdTimer(int world) const;
    bool IsMayaScene(int world) const;
    void GetKanturuAllState(int world, std::uint8_t State, std::uint8_t DetailState);
    void KanturuAllDirection(int world, double worldTime);
    bool GetMayaExplotion() const;
    void SetMayaExplotion(bool MayaDie);
    bool GetMayaAppear() const;
    void SetMayaAppear(bool MayaDie);

  private:
    CDirection &g_Direction;

    struct DirectionTarget
    {
        int x;
        int y;
        int z;
        float distance;
    };

    bool m_bMayaDie;
    bool m_bMayaAppear;
    bool m_bDirectionEnd;

    void GetKanturuMayaState(int world, std::uint8_t DetailState);
    void GetKanturuNightmareState(int world, std::uint8_t DetailState);

    void KanturuMayaDirection(double worldTime);
    void Move1stDirection(double worldTime);
    void Direction1st0();
    void Direction1st1(double worldTime);
    void Move2ndDirection();
    void Direction2nd0();
    void Direction2nd1();
    void Direction2nd2();

    void KanturuNightmareDirection(double worldTime);
    void Move3rdDirection(double worldTime);
    void Direction3rd0();
    void Direction3rd1(double worldTime);
    void Move4thDirection();
    void Direction4th0();
    void Direction4th1();

    bool IsKanturuWorldActive(int world) const;
    void PrepareCameraFocus(bool adjustViewDistance = true);
    void ActivateDirectionSequence();
    void ResetDirectionState();
    void ApplyDirectionTarget(const DirectionTarget &target);
};

class SessionKeeper;

class CDirection : protected SessionLegacyCalls
{
  private:
    friend class SessionLegacyCalls;

    bool &g_bTimeCheck;
    int &g_iBackupTime;

    vec3_t m_vCameraPosition;
    vec3_t m_v1stPosition;
    vec3_t m_v2ndPosition;
    vec3_t m_vResult;

    bool m_bCameraCheck;
    float m_fCount;
    float m_fLength;
    float m_fCameraSpeed;

    bool m_bTimeCheck;
    double m_iBackupTime;
    bool m_bStateCheck;

    double directionTime_ = 0.0;
    float directionFramesRemaining_ = 0.f;
    float directionActorFrames_ = 0.f;
    bool directionSequenceEnded_ = true;
    bool directionWaitsForActor_ = false;
    void SpendDirectionFrames(float frames);

    CHARACTER *FindLiveCharacterByKey(int key);

  public:
    std::vector<DirectionMonster> stl_Monster;

    bool m_bOrderExit;
    int m_iTimeSchedule;
    int m_CameraLevel;
    float m_fCameraViewFar;
    int m_iCheckTime;
    bool m_bAction;
    bool m_bDownHero;
    float m_AngleY;

    CKanturuDirection m_CKanturu;
    CMVP1STDirection m_CMVP;

    explicit CDirection(SessionKeeper &keeper);
    virtual ~CDirection();

    void Init();
    void CloseAllWindows();
    bool IsDirection(int world) const;
    void CheckDirection(int world, double worldTime);
    template <typename Step> void AdvanceSequence(Step &&step)
    {
        while (!directionSequenceEnded_ && !m_bOrderExit)
        {
            const std::array<int, 4> before{m_iTimeSchedule, m_iCheckTime, m_bAction,
                                            m_bCameraCheck};
            directionActorFrames_ = 0.f;
            directionWaitsForActor_ = false;
            step();
            SpendDirectionFrames(directionActorFrames_);
            const std::array<int, 4> after{m_iTimeSchedule, m_iCheckTime, m_bAction,
                                           m_bCameraCheck};
            if (directionWaitsForActor_ || before == after)
                break;
        }
    }

    void SetCameraPosition();
    int GetCameraPosition(vec3_t GetPosition);
    bool DirectionCameraMove();
    void DeleteMonster();

    float CalculateAngle(CHARACTER *c, int x, int y, float Angle);
    void CameraLevelUp();
    void SetNextDirectionPosition(int x, int y, int z, float Speed);
    bool GetTimeCheck(int DelayTime);
    bool GetTimeCheck(int DelayTime, double worldTime);
    void HeroFallingDownDirection();
    void HeroFallingDownInit();
    void FaillingEffect();

    void SummonCreateMonster(EMonsterType type, int x, int y, float angle, bool nextCheck = true,
                             bool summonAnimation = true, float animationSpeed = -1.0f);
    bool MoveCreatedMonster(int index, int x, int y, float angle, int speed);
    bool ActionCreatedMonster(int Index, int Action, int Count, bool TankerAttack = false,
                              bool NextCheck = false);
};

struct WorldCharacterVisualState;

class SessionKeeper;
class SessionRandom;

class CHARACTER;
class OBJECT;
struct BMD;

constexpr std::chrono::milliseconds XMAS_EVENT_TIME{60000};

inline std::uint32_t GetMillisecondsTimestamp()
{
    return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

class CXmasEvent final : protected SessionLegacyCalls
{
  public:
    explicit CXmasEvent(SessionKeeper &keeper) noexcept;
    ~CXmasEvent();

    void LoadXmasEvent();
    void LoadXmasEventEffect();
    void LoadXmasEventItem();
    void LoadXmasEventSound();

    void CreateXmasEventEffect(CHARACTER *pCha, OBJECT *pObj, int iType);

    void GenID();

  public:
    std::int32_t m_iEffectID;
};

class CNewYearsDayEvent final : protected SessionLegacyCalls
{
  public:
    explicit CNewYearsDayEvent(SessionKeeper &keeper) noexcept;
    ~CNewYearsDayEvent();

    void LoadModel();
    void LoadSound();

    CHARACTER *CreateMonster(int iType, int iPosX, int iPosY, int iKey);
    bool MoveMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b, WorldCharacterVisualState &visual);

  private:
    SessionRandom &Random;
};

class C09SummerEvent final : protected SessionLegacyCalls
{
    void EmitAnimationSounds(OBJECT &object, double worldTime, WorldCharacterVisualState &visual);

  public:
    explicit C09SummerEvent(SessionKeeper &keeper) noexcept;
    ~C09SummerEvent();

    void LoadModel();
    void LoadSound();

    CHARACTER *CreateMonster(int iType, int iPosX, int iPosY, int iKey);
    bool MoveMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b, double worldTime,
                           WorldCharacterVisualState &visual);
};

#ifdef CSK_FIX_BLUELUCKYBAG_MOVECOMMAND
class CBlueLuckyBagEvent
{
  public:
    CBlueLuckyBagEvent();
    ~CBlueLuckyBagEvent();

    static CBlueLuckyBagEvent *GetInstance()
    {
        static CBlueLuckyBagEvent s_Instance;
        return &s_Instance;
    }

    void StartBlueLuckyBag();
    void CheckTime();
    bool IsEnableBlueLuckyBag() const;

  private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point m_lastActivationTick;
    bool m_isActive;
};

#define g_BlueLuckyBagEvent CBlueLuckyBagEvent::GetInstance()
#endif // CSK_FIX_BLUELUCKYBAG_MOVECOMMAND

namespace SEASON3B
{
class CNewChaosCastleSystem : public CSBaseMatch
{
  private:
    int m_iChaosCastleLimitArea1[16];
    int m_iChaosCastleLimitArea2[16];
    int m_iChaosCastleLimitArea3[16];
    BYTE m_byCurrCastleLevel;
    bool m_bActionMatch;

  public:
    explicit CNewChaosCastleSystem(SessionKeeper &keeper);
    virtual ~CNewChaosCastleSystem();

    virtual void UpdateMatchState(void);

    virtual void SetMatchGameCommand(const LPPRECEIVE_MATCH_GAME_STATE data);
    virtual void SetMatchResult(const int iNumDevil, const int iMyRank,
                                const MatchResult *pMatchResult, const int Success = false);
    virtual void RenderMatchResult(void);
};
}; // namespace SEASON3B

namespace SEASON3B
{
class CNewBloodCastleSystem : public CSBaseMatch
{
  public:
    explicit CNewBloodCastleSystem(SessionKeeper &keeper);
    virtual ~CNewBloodCastleSystem();

    virtual void UpdateMatchState(void);
    virtual void SetMatchGameCommand(const LPPRECEIVE_MATCH_GAME_STATE data);
    virtual void SetMatchResult(const int iNumDevil, const int iMyRank,
                                const MatchResult *pMatchResult, const int Success = false);
    virtual void RenderMatchResult(void);
};
}; // namespace SEASON3B

struct WorldCharacterVisualState;

struct WorldCharacterVisualState;

class SessionGameplayUnit;
class SessionKeeper;
class CMapManager;

class BMD;
namespace SEASON3A
{
class CursedTemple : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    void InstallBehavior() override;

    void AdvanceMonsterState(CHARACTER &character, BMD &model) override;
    virtual ~CursedTemple();

  private:
    explicit CursedTemple(SessionKeeper &keeper);
    void Initialize();
    void Destroy();

  public:
    void Process();
    void Draw();

  public:
    bool GetInterfaceState(int type, int subtype = -1);
    void SetInterfaceState(bool state, int subtype = -1);

  public:
    enum class SkillResult
    {
        Ignored,
        InsufficientPoints,
        Sent
    };
    SkillResult TryUseSkill(CHARACTER &caster, DWORD selectedCharacter);
    bool CheckInventoryHolyItem(CHARACTER *character);
    WORD SkillPoints() const noexcept
    {
        return skillPoints_;
    }
    void SetSkillPoints(WORD points) noexcept
    {
        skillPoints_ = points;
    }
    bool IsHolyItemPickState();
    bool IsPartyMember(DWORD selectcharacterindex);
    void ReceiveCursedTempleInfo(const BYTE *ReceiveBuffer);
    void ReceiveCursedTempleState(const eCursedTempleState state);

  public:
    CHARACTER *CreateCharacters(EMonsterType iType, int iPosX, int iPosY, int iKey);

  public:
    bool SetCurrentActionMonster(CHARACTER *c, OBJECT *o);
    bool AttackEffectMonster(CHARACTER *c, OBJECT *o, BMD *b);
    void ResetCursedTemple();

  public:
    bool MoveObject(OBJECT *o);
    void MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b);
    bool MoveMonsterVisual(CHARACTER *, OBJECT *o, BMD *b, WorldCharacterVisualState &visual);
    void MoveMonsterSoundVisual(OBJECT *o, BMD *b);

  public:
    void RenderAfterObjectMesh(const ObjectDrawInput &object, BMD *model,
                               bool extraMonster = false) override;
    bool RenderObject_AfterCharacter(const ObjectDrawInput &draw, BMD *b);
    bool AdvanceObjectVisual(OBJECT *o, BMD *b, float = 0.f);
    bool AdvanceSharedMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                    WorldCharacterVisualState &visual);
    bool RenderObjectMesh(const ObjectDrawInput &draw, BMD *b, bool ExtraMon = 0) override;
    void UpdateTempleSystemMsg(int _Value);
    void SetGaugebarEnabled(bool bFlag);
    void SetGaugebarCloseTimer();
    bool IsGaugebarEnabled();

  private:
    friend class ::SessionKeeper;
    friend class ::SessionGameplayUnit;
    WORD skillPoints_ = 0;
    WORD teamKeys_[MAX_PARTYS]{};
    int teamCount_ = 0;
    void UpdateTeam(const BYTE *packet);
    CMapManager &gMapManager;
    bool m_IsTalkEnterNpc;
    bool m_InterfaceState;
    WORD m_HolyItemPlayerIndex;
    eCursedTempleState m_CursedTempleState;
    std::list<int> m_TerrainWaterIndex;
    WORD m_AlliedPoint;
    WORD m_IllusionPoint;
    bool m_ShowAlliedPointEffect;
    bool m_ShowIllusionPointEffect;
    bool m_bGaugebarEnabled;
    float m_fGaugebarCloseTimer;
};
}; // namespace SEASON3A

namespace MUHelper::Combat
{
inline constexpr int ConcentrationRadius = 2;
// Same adjacent-tile distance used by Helper movement (including diagonals).
inline constexpr float CloseAttackRange = 1.5F;

struct HuntingTarget
{
    int id;
    int x;
    int y;
    int distance;
};

int SelectTarget(std::span<const HuntingTarget> targets, bool concentrated,
                 std::vector<int> &density);
} // namespace MUHelper::Combat

// Welfare Temple packet records are owned by SessionNetwork.h.

class CHARACTER;

class CmuConsoleDebug;

struct DUEL_PLAYER_INFO
{
    short m_sIndex = 0;
    wchar_t m_szID[MAX_USERNAME_SIZE + 1]{};
    int m_iScore = 0;
    float m_fHPRate = 0.0f;
    float m_fSDRate = 0.0f;
};

enum _DUEL_PLAYER_TYPE
{
    DUEL_HERO,
    DUEL_ENEMY,
    MAX_DUEL_PLAYERS
};

struct DUEL_CHANNEL_INFO
{
    BOOL m_bEnable = FALSE;
    BOOL m_bJoinable = FALSE;
    wchar_t m_szID1[MAX_USERNAME_SIZE + 1]{};
    wchar_t m_szID2[MAX_USERNAME_SIZE + 1]{};
};

constexpr int MAX_DUEL_CHANNELS = 4;

class CDuelMgr : protected SessionLegacyCalls
{
  public:
    explicit CDuelMgr(SessionKeeper &keeper);
    virtual ~CDuelMgr();
    void Reset();

  public:
    void EnableDuel(BOOL bEnable);
    BOOL IsDuelEnabled();

    void EnablePetDuel(BOOL bEnable);
    BOOL IsPetDuelEnabled();

    void SetDuelPlayer(int iPlayerNum, short iIndex, const wchar_t *pszID);
    void SetHeroAsDuelPlayer(int iPlayerNum);
    void SetScore(int iPlayerNum, int iScore);
    void SetHP(int iPlayerNum, int iRate);
    void SetSD(int iPlayerNum, int iRate);

    const wchar_t *GetDuelPlayerID(int iPlayerNum) const;
    short GetDuelPlayerIndex(int iPlayerNum) const
    {
        return m_DuelPlayer[iPlayerNum].m_sIndex;
    }
    int GetScore(int iPlayerNum);
    float GetHP(int iPlayerNum);
    float GetSD(int iPlayerNum);

    BOOL IsDuelPlayer(CHARACTER *pCharacter, int iPlayerNum, BOOL bIncludeSummon = TRUE);
    BOOL IsDuelPlayer(WORD wIndex, int iPlayerNum);

  protected:
    BOOL m_bIsDuelEnabled;
    BOOL m_bIsPetDuelEnabled;

  public:
    void SetDuelChannel(int iChannelIndex, BOOL bEnable, BOOL bJoinable, const wchar_t *pszID1,
                        const wchar_t *pszID2);
    BOOL IsDuelChannelEnabled(int iChannelIndex)
    {
        return m_DuelChannels[iChannelIndex].m_bEnable;
    }
    BOOL IsDuelChannelJoinable(int iChannelIndex)
    {
        return m_DuelChannels[iChannelIndex].m_bJoinable;
    }
    const wchar_t *GetDuelChannelUserID1(int iChannelIndex) const
    {
        return m_DuelChannels[iChannelIndex].m_szID1;
    }
    const wchar_t *GetDuelChannelUserID2(int iChannelIndex) const
    {
        return m_DuelChannels[iChannelIndex].m_szID2;
    }

    void SetCurrentChannel(int iChannel = -1)
    {
        m_iCurrentChannel = iChannel;
    }
    int GetCurrentChannel()
    {
        return m_iCurrentChannel;
    }

    void RemoveAllDuelWatchUser();
    void AddDuelWatchUser(const wchar_t *pszUserID);
    void RemoveDuelWatchUser(const wchar_t *pszUserID);
    const wchar_t *GetDuelWatchUser(int iIndex) const;
    int GetDuelWatchUserCount() const
    {
        return static_cast<int>(m_DuelWatchUserList.size());
    }

    BOOL GetFighterRegenerated()
    {
        return m_bRegenerated;
    }
    void SetFighterRegenerated(BOOL bFlag)
    {
        m_bRegenerated = bFlag;
    }

  protected:
    CmuConsoleDebug &g_ConsoleDebug;
    std::array<DUEL_PLAYER_INFO, MAX_DUEL_PLAYERS> m_DuelPlayer{};
    std::array<DUEL_CHANNEL_INFO, MAX_DUEL_CHANNELS> m_DuelChannels{};
    int m_iCurrentChannel;

    BOOL m_bRegenerated;

    std::vector<std::wstring> m_DuelWatchUserList;
};

class CHARACTER;
class OBJECT;
struct tagITEM;
typedef tagITEM ITEM;

class CHARACTER;

// Skill execution dispatcher: validates a skill cast (range, mana, conditions)
// and routes it to the per-class executor.
namespace GameLogic::Combat
{
constexpr bool ShouldStartRightClickSkillCast(bool rightButtonDown, bool leftButtonDown,
                                              bool moving) noexcept
{
    return rightButtonDown && !leftButtonDown && !moving;
}

constexpr bool ShouldPathToSkillTarget(bool hasSelectedCharacter, bool hasClearPath) noexcept
{
    return hasSelectedCharacter && !hasClearPath;
}

bool ExecuteSkillComplete(CHARACTER *c);
bool CanExecuteSkill(CHARACTER *c, ActionSkillType Skill, float Distance);
} // namespace GameLogic::Combat

//  INCLUDE.

enum POPUP_MESSAGE
{
    POPUP_NONE = 0,
    POPUP_USER_OVER,
    POPUP_NOT_MUNSTONE,
    POPUP_FAILED,
    POPUP_FAILED2,
    POPUP_UNIRIA = 5,
    POPUP_CHANGERING,
    POPUP_NOT_HELPER,
};

enum
{
    MATCH_TYPE_NONE_EVENT = 0,
    MATCH_TYPE_DEVIL_SQUARE,
    MATCH_TYPE_BLOOD_CASTLE,
    MATCH_TYPE_END
};

enum CRYWOLF_OCCUPATION_STATE_TYPE
{
    CRYWOLF_OCCUPATION_STATE_PEACE = 0,
    CRYWOLF_OCCUPATION_STATE_OCCUPIED = 1,
    CRYWOLF_OCCUPATION_STATE_WAR = 2,
};

enum CRYWOLF_STATE_TYPE
{
    CRYWOLF_STATE_NONE = 0,
    CRYWOLF_STATE_NOTIFY_1 = 1,
    CRYWOLF_STATE_NOTIFY_2 = 2,
    CRYWOLF_STATE_READY = 3,
    CRYWOLF_STATE_START = 4,
    CRYWOLF_STATE_END = 5,
    CRYWOLF_STATE_ENDCYCLE = 6,
};

enum CRYWOLF_ALTAR_STATE
{
    CRYWOLF_ALTAR_STATE_NONE = 0,
    CRYWOLF_ALTAR_STATE_CONTRACTED = 1,
    CRYWOLF_ALTAR_STATE_APPLYING_CONTRACT = 2,
    CRYWOLF_ALTAR_STATE_EXCEEDING_CONTRACT_COUNT = 3,
    CRYWOLF_ALTAR_STATE_OCCUPYING = 4,
    CRYWOLF_ALTAR_STATE_OCCUPIED = 5,
};

enum KANTURU_STATE_TYPE
{
    KANTURU_STATE_NONE = 0,
    KANTURU_STATE_STANDBY = 1,
    KANTURU_STATE_MAYA_BATTLE = 2,
    KANTURU_STATE_NIGHTMARE_BATTLE = 3,
    KANTURU_STATE_TOWER = 4,
    KANTURU_STATE_END = 5,
};

enum KANTURU_MAYA_DIRECTION_TYPE
{
    KANTURU_MAYA_DIRECTION_NONE = 0,
    KANTURU_MAYA_DIRECTION_STANBY1 = 1,
    KANTURU_MAYA_DIRECTION_NOTIFY = 2,
    KANTURU_MAYA_DIRECTION_MONSTER1 = 3,
    KANTURU_MAYA_DIRECTION_MAYA1 = 4,
    KANTURU_MAYA_DIRECTION_END_MAYA1 = 5,
    KANTURU_MAYA_DIRECTION_ENDCYCLE_MAYA1 = 6,
    KANTURU_MAYA_DIRECTION_STANBY2 = 7,
    KANTURU_MAYA_DIRECTION_MONSTER2 = 8,
    KANTURU_MAYA_DIRECTION_MAYA2 = 9,
    KANTURU_MAYA_DIRECTION_END_MAYA2 = 10,
    KANTURU_MAYA_DIRECTION_ENDCYCLE_MAYA2 = 11,
    KANTURU_MAYA_DIRECTION_STANBY3 = 12,
    KANTURU_MAYA_DIRECTION_MONSTER3 = 13,
    KANTURU_MAYA_DIRECTION_MAYA3 = 14,
    KANTURU_MAYA_DIRECTION_END_MAYA3 = 15,
    KANTURU_MAYA_DIRECTION_ENDCYCLE_MAYA3 = 16,
    KANTURU_MAYA_DIRECTION_END = 17,
    KANTURU_MAYA_DIRECTION_ENDCYCLE = 18,
};

enum KANTURU_NIGHTMARE_DIRECTION_TYPE
{
    KANTURU_NIGHTMARE_DIRECTION_NONE = 0,
    KANTURU_NIGHTMARE_DIRECTION_IDLE = 1,
    KANTURU_NIGHTMARE_DIRECTION_NIGHTMARE = 2,
    KANTURU_NIGHTMARE_DIRECTION_BATTLE = 3,
    KANTURU_NIGHTMARE_DIRECTION_END = 4,
    KANTURU_NIGHTMARE_DIRECTION_ENDCYCLE = 5,
};

enum KANTURU_TOWER_STATE_TYPE
{
    KANTURU_TOWER_NONE = 0,
    KANTURU_TOWER_REVITALIXATION = 1,
    KANTURU_TOWER_NOTIFY = 2,
    KANTURU_TOWER_CLOSE = 3,
    KANTURU_TOWER_NOTIFY2 = 4,
    KANTURU_TOWER_END = 5,
    KANTURU_TOWER_ENDCYCLE = 6,
};

enum
{
    FREETICKET_TYPE_DEVILSQUARE = 0,
    FREETICKET_TYPE_BLOODCASTLE,
    FREETICKET_TYPE_KALIMA,
    FREETICKET_TYPE_CURSEDTEMPLE,
    FREETICKED_TYPE_CHAOSCASTLE,
    FREETICKED_TYPE_DOPPLEGANGGER,
};

class OBJECT;
class CHARACTER;

class SessionItemStore;

struct SessionTexturePropertiesSlot;

struct GuildCommander
{
    BYTE byTeam;
    BYTE byX;
    BYTE byY;
    BYTE byCmd = 3;
    float byLifeTime;
};

/**
 * \brief Types of storage where items can be moved from/to.
 */

// Lucky Set armor constants - verified as indices 62-72 (Phoenix Soul is at 73)

// Indices 62-72

// Restricted ITEM_HELPER special items starting at local index 135.
// Exact item-name coverage may vary across older client logic.

// 135..146

// Restricted ITEM_POTION special jewels starting at local index 160.
// Exact item-name coverage may vary across older client logic.

// 160..161

// Item action restriction types for Check_ItemAction
// Index into nItemOption array: [PERSONALSHOP, STORE, TRADE, DROP, SELL, REPAIR]
// 0,1,1,0,0,0 - Blocks storage & trade
// 0,0,0,0,1,0 - Blocks sell only

namespace EventMatchDetail
{

#pragma pack(push)
#pragma pack()
inline constexpr std::chrono::seconds kMatchCountdownDuration{30};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
template <std::size_t N> void ClearWideBuffer(wchar_t (&buffer)[N])
{
    std::fill(std::begin(buffer), std::end(buffer), L'\0');
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
template <std::size_t N, typename... Args>
void WriteWide(wchar_t (&buffer)[N], const wchar_t *format, Args... args)
{
    if (format == nullptr)
    {
        buffer[0] = L'\0';
        return;
    }

    std::swprintf(buffer, static_cast<std::size_t>(N), format, args...);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
template <std::size_t N, typename... Args>
void AppendWide(wchar_t (&buffer)[N], const wchar_t *format, Args... args)
{
    if (format == nullptr)
    {
        return;
    }

    const std::size_t currentLength = std::wcslen(buffer);
    if (currentLength >= N)
    {
        return;
    }

    std::swprintf(buffer + currentLength, static_cast<std::size_t>(N - currentLength), format,
                  args...);
}
#pragma pack(pop)

} // namespace EventMatchDetail

namespace GateSwitchDetail
{

#pragma pack(push)
#pragma pack()
inline constexpr DWORD GATE_OPEN = static_cast<DWORD>(eBuff_CastleGateIsOpen);
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr DWORD GATE_CLOSE = 0;
#pragma pack(pop)

} // namespace GateSwitchDetail
