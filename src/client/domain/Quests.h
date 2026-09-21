#pragma once
#include "data/CharacterData.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/WorldData.h"
#include "domain/MapSimulation.h"
#include "render/Text.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include <array>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#define QM_MAX_ND_ANSWER 10
#define QM_MAX_ANSWER 5
#define QM_MAX_REQUEST_REWARD_TEXT_LEN 64
#define MAX_QUESTS 200
#define MAX_QUEST_TEXT 100

enum
{
    TYPE_QUEST = 0,
    TYPE_DEVIL_SQUARE,
    TYPE_BLOOD_CASTLE,
    TYPE_CURSEDTEMPLE,
    TYPE_QUEST_END
};

typedef struct
{
    BYTE chLive;
    BYTE byQuestType;
    WORD wItemType;
    BYTE byItemSubType;
    BYTE byItemLevel;
    BYTE byItemNum;
    BYTE byRequestType;
    BYTE byRequestClass[MAX_CLASS];
    short shQuestStartText[4];
} QUEST_CLASS_ACT;

typedef struct
{
    BYTE byLive;
    BYTE byType;
    WORD wCompleteQuestIndex;
    WORD wLevelMin;
    WORD wLevelMax;
    WORD wRequestStrength;
    DWORD dwZen;
    short shErrorText;
} QUEST_CLASS_REQUEST;

typedef struct
{
    short shQuestConditionNum;
    short shQuestRequestNum;
    WORD wNpcType;

    wchar_t strQuestName[32];

    QUEST_CLASS_ACT QuestAct[MAX_QUEST_CONDITION];
    QUEST_CLASS_REQUEST QuestRequest[MAX_QUEST_REQUEST];
} QUEST_ATTRIBUTE;

class CSQuest : protected SessionLegacyCalls
{
  public:
    static constexpr std::uint8_t QUEST_STATE_INVALID = 0xFF;
    static constexpr std::uint8_t QUEST_STATE_MASK = 0x03;
    static constexpr std::uint8_t QUEST_STATES_PER_ENTRY = 4;
    static constexpr std::uint8_t QUEST_STATE_BIT_WIDTH = 2;
    static constexpr int QUEST_MONSTER_SLOT_COUNT = 5;

  private:
    using QuestStateList = std::array<std::uint8_t, (MAX_QUESTS + (QUEST_STATES_PER_ENTRY - 1)) /
                                                        QUEST_STATES_PER_ENTRY>;
    using QuestAttributes = std::array<QUEST_ATTRIBUTE, MAX_QUESTS>;
    using QuestEventCounters = std::array<std::uint8_t, TYPE_QUEST_END>;
    using KillTracker = std::array<int, QUEST_MONSTER_SLOT_COUNT>;

    std::uint8_t getQuestState(int questIndex = -1);
    std::uint8_t CheckQuestState(std::uint8_t state = QUEST_STATE_INVALID);
    short FindQuestContext(QUEST_ATTRIBUTE *pQuest, int index);
    bool CheckRequestCondition(QUEST_ATTRIBUTE *pQuest, bool bLastCheck = false);
    bool CheckActCondition(QUEST_ATTRIBUTE *pQuest);

  public:
    explicit CSQuest(SessionKeeper &keeper);
    ~CSQuest(void);

    //  Quest Init Functions
    bool OpenQuestScript(const wchar_t *filename);
    bool IsInit(void);
    void clearQuest(void);

    //  Quest Setting.
    void setQuestLists(const std::uint8_t *byList, int num, CLASS_TYPE Class = CLASS_UNDEFINED);
    void setQuestList(int index, int result);
    std::uint8_t getQuestState2(int questIndex);
    void ShowQuestPreviewWindow(int index = -1);
    void ShowQuestNpcWindow(int index = -1);

    std::uint8_t getCurrQuestState(void);
    const wchar_t *GetNPCName(BYTE questIndex);
    wchar_t *getQuestTitle();
    wchar_t *getQuestTitle(BYTE byQuestIndex);
    wchar_t *getQuestTitleWindow();
    void SetEventCount(std::uint8_t type, std::uint8_t count);
    int GetEventCount(std::uint8_t byType);
    std::uint32_t GetNeedZen()
    {
        return m_dwNeedZen;
    }
    QUEST_ATTRIBUTE *GetCurQuestAttribute()
    {
        return &m_Quest[m_byCurrQuestIndex];
    }

    std::uint8_t GetCurrQuestIndex()
    {
        return m_byCurrQuestIndex;
    }
    void SetKillMobInfo(const int *anKillMobInfo);
    bool ProcessNextProgress();
    void ShowDialogText(int iDialogIndex);
    bool BeQuestItem();
    int FindQuestItemsInInven(int nType, int nCount, int nLevel = -1);
    int GetKillMobCount(int nMobType);

  private:
    static void RefreshDialogTextOnLocaleChangeCallback(void *context) noexcept;
    void RefreshDialogTextOnLocaleChange() noexcept;
    std::uint8_t m_byClass;

    QuestEventCounters m_byEventCount;
    QuestAttributes m_Quest;

    QuestStateList m_byQuestList;
    std::uint8_t m_byCurrQuestIndex;
    std::uint8_t m_byCurrQuestIndexWnd;

    std::uint8_t m_byStartQuestList;

    std::uint8_t m_byViewQuest;
    short m_shCurrPage;
    std::uint8_t m_byCurrState;
    std::uint32_t m_dwNeedZen;

    std::uint8_t m_byQuestType;
    bool m_bOnce;

    KillTracker m_anKillMobType;
    KillTracker m_anKillMobCount;
    std::uint16_t m_wNPCIndex;

    int m_iStartX;
    int m_iStartY;
};

namespace GameLogic::Quests::Dialog
{

inline constexpr int MaxAnswer = 10;
inline constexpr int MaxDialog = 200;

// I18N::Dialog legacy-id encoding:
//   text:           legacy_id = dialogIndex                   (0..199)
//   answer (i, n):  legacy_id = 1000 + dialogIndex*10 + slot  (1000..2999)
inline constexpr int AnswerLegacyIdBase = 1000;
inline constexpr int AnswerLegacyIdStride = 10;
inline constexpr int DialogAnswerLegacyId(int dialogIndex, int answerSlot) noexcept
{
    return AnswerLegacyIdBase + dialogIndex * AnswerLegacyIdStride + answerSlot;
}

// One reply branch in a dialog entry. `link` is the dialog index
// the client jumps to when the player picks this reply; `returnCode`
// is the answer code the client sends back to the server.
struct Branch
{
    int link;
    int returnCode;
};

// The non-localized half of a dialog entry. The localized NPC line
// and reply labels live in I18N::Dialog (resolved by index via
// I18N::Dialog::Lookup / LookupSlot).
struct Entry
{
    int numAnswer;
    Branch answers[MaxAnswer];
};

// Returns the entry for `dialogIndex` (0-based). Out-of-range
// indices return a zero-answer sentinel entry so callers can
// render an empty dialog rather than dereferencing past the table.
const Entry &GetEntry(int dialogIndex) noexcept;

} // namespace GameLogic::Quests::Dialog

struct WorldCharacterVisualState;

struct WorldCharacterVisualState;

class SessionGameplayUnit;
class SessionKeeper;
class CMapManager;
class CameraState;
class CBoneManager;

class BMD;

namespace SEASON3A
{
class CGM3rdChangeUp : public BaseMap, protected SessionLegacyCalls
{
  protected:
    bool m_nDarkElfAppearance;

  protected:
    explicit CGM3rdChangeUp(SessionKeeper &keeper);

  public:
    bool CreateWeather(PARTICLE *particle, int index) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    void InstallBehavior() override;

    bool AdvanceMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                              WorldCharacterVisualState &visual) override;

    virtual ~CGM3rdChangeUp();

    static std::unique_ptr<CGM3rdChangeUp> Make(SessionKeeper &keeper);

    bool IsBalgasBarrackMap();
    bool IsBalgasRefugeMap();

    bool CreateObject(OBJECT *object) override;
    bool MoveObject(OBJECT *pObject);
    bool AdvanceObjectVisual(OBJECT *pObject, BMD *pModel, float = 0.f);
    bool RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b,
                               bool extraMonster = false) override;

    bool CreateFireSnuff(PARTICLE *o);
    void PlayEffectSound(OBJECT *o);
    CHARACTER *CreateBalgasBarrackMonster(int iType, int PosX, int PosY, int Key);
    bool SetCurrentActionMonster(CHARACTER *character, OBJECT *object) override;
    bool SetCurrentActionBalgasBarrackMonster(CHARACTER *c, OBJECT *o);
    bool AttackEffectMonster(CHARACTER *character, OBJECT *object, BMD *model) override;
    bool AttackEffectBalgasBarrackMonster(CHARACTER *c, OBJECT *o, BMD *b);
    bool MoveBalgasBarrackMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                        WorldCharacterVisualState &visual);
    void MoveBalgasBarrackBlurEffect(CHARACTER *c, OBJECT *o, BMD *b);
    bool RenderMonsterObjectMesh(const ObjectDrawInput &input, BMD *b, int ExtraMon);
    bool AdvanceBalgasBarrackMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                           WorldCharacterVisualState &visual);

  private:
    CMapManager &gMapManager;
    CBoneManager &boneManager_;
    CameraState &g_Camera;
};
} // namespace SEASON3A

struct SNPCDialogue
{
    int m_nNPCWords;
    int m_anAnswer[QM_MAX_ND_ANSWER * 2];
};

#pragma pack(push, 1)
struct SQuestProgress
{
    BYTE m_byUIType;
    int m_nNPCWords;
    int m_nPlayerWords;
    int m_anAnswer[QM_MAX_ANSWER];
    int m_nSubject;
    int m_nSummary;
};
#pragma pack(pop)

using NPCDialogueMap = std::map<DWORD, SNPCDialogue>;
using QuestProgressMap = std::map<DWORD, SQuestProgress>;
using QuestWordsMap = std::map<int, std::wstring>;

struct QuestScriptData final
{
    NPCDialogueMap npcDialogues;
    QuestProgressMap questProgress;
    QuestWordsMap questWords;
    std::vector<std::wstring> failureMessages;
};

class SessionNetworkUnit;
class CErrorReport;
struct tagITEM;
typedef struct tagITEM ITEM;

struct SQuestRequest
{
    DWORD m_dwType;
    WORD m_wIndex;
    DWORD m_dwValue;
#ifdef ASG_ADD_TIME_LIMIT_QUEST
    DWORD m_dwCurValue;
#else  // ASG_ADD_TIME_LIMIT_QUEST
    WORD m_wCurValue;
#endif // ASG_ADD_TIME_LIMIT_QUEST
    ITEM *m_pItem;
};

struct SQuestReward
{
    DWORD m_dwType;
    WORD m_wIndex;
    DWORD m_dwValue;
    ITEM *m_pItem;
};

struct SQuestRequestReward
{
    BYTE m_byRequestCount;
    BYTE m_byGeneralRewardCount;
    BYTE m_byRandRewardCount;
    BYTE m_byRandGiveCount;
    SQuestRequest m_aRequest[5];
    SQuestReward m_aReward[5];
};

enum REQUEST_REWARD_CLASSIFY
{
    RRC_NONE = 0,
    RRC_REQUEST = 1,
    RRC_REWARD = 2
};

struct SRequestRewardText
{
    LegacyFontRole m_fontRole = LegacyFontRole::Normal;
    DWORD m_dwColor;
    wchar_t m_szText[QM_MAX_REQUEST_REWARD_TEXT_LEN];
    REQUEST_REWARD_CLASSIFY m_eRequestReward; //
    DWORD m_dwType;
    WORD m_wIndex;
    ITEM *m_pItem;
};

typedef std::map<DWORD, SQuestRequestReward> QuestRequestRewardMap;
typedef std::list<DWORD> DWordList;

class CQuestMng : protected SessionLegacyCalls
{
    friend class SessionNetworkUnit;

  protected:
    std::wstring &g_strSelectedML;
    CErrorReport &g_ErrorReport;
    const QuestScriptData *questScripts_ = nullptr;
    QuestRequestRewardMap m_mapQuestRequestReward;
    std::uint64_t requestRewardRevision_ = 0;

    std::map<WORD, bool> m_mapEPRequestRewardState;

    int m_nNPCIndex;
    const wchar_t *m_szNPCName; // [MAX_MONSTER_NAME] ;

    DWordList m_listQuestIndexByEtc;
    DWordList m_listCurQuestIndex;

  public:
    explicit CQuestMng(SessionKeeper &keeper) noexcept;
    virtual ~CQuestMng();

    std::vector<std::wstring> LoadQuestScript();
    void SetQuestRequestReward(const BYTE *pbyRequestRewardPacket);
    const SQuestRequestReward *GetRequestReward(DWORD dwQuestIndex);
    std::uint64_t RequestRewardRevision() const
    {
        return requestRewardRevision_;
    }

    int GetNPCIndex();
    const wchar_t *GetNPCName();

    void SetCurQuestProgress(DWORD dwQuestIndex);

    const wchar_t *GetWords(int nWordsIndex);
    const wchar_t *GetNPCDlgNPCWords(DWORD dwDlgState);
    const wchar_t *GetNPCDlgAnswer(DWORD dwDlgState, int nAnswer);
    int GetNPCDlgAnswerResult(DWORD dwDlgState, int nAnswer);
    const wchar_t *GetNPCWords(DWORD dwQuestIndex);
    const wchar_t *GetPlayerWords(DWORD dwQuestIndex);
    const wchar_t *GetAnswer(DWORD dwQuestIndex, int nAnswer);
    const wchar_t *GetSubject(DWORD dwQuestIndex);
    const wchar_t *GetSummary(DWORD dwQuestIndex);
    bool IsRequestRewardQS(DWORD dwQuestIndex);
    bool GetRequestRewardText(SRequestRewardText *destination, int destinationCount,
                              DWORD questIndex);
    void SetEPRequestRewardState(DWORD dwQuestIndex, bool ProgressState);
    bool IsEPRequestRewardState(DWORD dwQuestIndex);
    bool IsQuestByEtc(DWORD dwQuestIndex);

    void SetQuestIndexByEtcList(DWORD *adwSrcQuestIndex, int nIndexCount);
    bool IsQuestIndexByEtcListEmpty();
    bool GetQuestIndexByEtcSelection(std::uint16_t &questNumber, std::uint16_t &questGroup);
    void DelQuestIndexByEtcList(DWORD dwQuestIndex);

    void SetCurQuestIndexList(DWORD *adwCurQuestIndex, int nIndexCount);
    void AddCurQuestIndexList(DWORD dwQuestIndex);
    void RemoveCurQuestIndexList(DWORD dwQuestIndex);
    bool IsIndexInCurQuestIndexList(DWORD dwQuestIndex);

  protected:
    const QuestScriptData &QuestScripts() const noexcept;
    std::wstring LoadNPCDialogueScript(NPCDialogueMap &dialogues);
    std::wstring LoadQuestProgressScript(QuestProgressMap &progress);
    std::wstring LoadQuestWordsScript(QuestWordsMap &words);
};

enum
{
    EVENT_NONE = 0,
    EVENT_LENA,
    EVENT_STONE,
    EVENT_SCRATCH_TICKET,
    EVENT_STONE_EXCHANGE,
    EVENT_FRIEND,
};

enum
{
    BREEDER_NONE = 0,
    BREEDER_START,
    BREEDER_RECOVERY,
    BREEDER_REVIVAL,
    BREEDER_END
};

enum
{
    REVIVAL_NONE = 0,
    REVIVAL_DARKHORSE,
    REVIVAL_DARKSPIRIT,
    REVIVAL_END
};

enum
{
    QUEST_VIEW_NONE = 0,
    QUEST_VIEW_NPC,
    QUEST_VIEW_PREVIEW,
    QUEST_VIEW_END
};

enum //
{
    QUEST_ITEM = 1,
    QUEST_MONSTER,
};

enum
{
    QUEST_NONE = 0,
    QUEST_ING = 1,
    QUEST_END,
    QUEST_NO,
    QUEST_READY,
    QUEST_ERROR,
};

enum
{
    QUEST_CHANGE_UP_1 = 0,
    QUEST_CHANGE_UP_2,
    QUEST_CHANGE_UP_3,
    QUEST_COMBO,
    QUEST_3RD_CHANGE_UP_1,
    QUEST_3RD_CHANGE_UP_2,
    QUEST_3RD_CHANGE_UP_3,
    QUEST_LIST_END
};

enum
{
    TYPE_OBSERVER = 0,
    TYPE_GUILD_SOLDIER,
    TYPE_GUILD_COMMANDER
};

enum
{
    SET_OPTION_NONE = 0,
    SET_OPTION_STRENGTH,
    SET_OPTION_DEXTERITY,
    SET_OPTION_ENERGY,
    SET_OPTION_VITALITY,
    SET_OPTION_END
};

class OBJECT;

class CHARACTER;

class SessionItemStore;

struct SessionTexturePropertiesSlot;

typedef struct
{
    wchar_t m_lpID[MAX_USERNAME_SIZE];
    int m_iScore;
    DWORD m_dwExp;
    int m_iZen;
} DevilSquareRank;

typedef struct
{
    BYTE byFlag;
    BYTE byCount;
} QUEST_FLAG_BUFFER;

typedef struct
{
    int iCrastGold;
    int iCrastSilver;
    int iCrastBronze;
} QUEST_CRAST;

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
