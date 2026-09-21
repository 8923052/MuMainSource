#include "domain/Quests.h"
#include "app/ApplicationKeeper.h"
#include "support/CoreMath.h"
#include "session/SessionGameplay.h"
#include "domain/ItemsSkills.h"
#include "I18N/All.h"
#include "data/ResourceData.h"
#include "session/SessionKeeper.h"
#include "ui/session/UiSessionLogic.h"
#include "render/Textures.h"
#include "data/Localization.h"
#include "domain/CharacterSystem.h"
#include "render/ModelResources.h"
#include "domain/CharacterPresentation.h"
#include "domain/WorldSimulation.h"
#include "render/World.h"
#include "render/Text.h"
#include "session/SessionRender.h"
#include "support/Scenes.h"
#include "ui/runtime/UiControls.h"
#include "data/ItemData.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "session/SessionNetwork.h"
#include "data/WorldData.h"
#include "render/Terrain.h"
#include "domain/MovementAI.h"
#include "data/GameData.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/MapSimulation.h"
#include "render/ModelGeometry.h"
#include "app/ApplicationAudio.h"
#include "session/SessionPresentation.h"

namespace
{
constexpr std::uint8_t QUEST_STATE_INVALID = 0xFF;
constexpr std::uint8_t QUEST_STATE_MASK = 0x03;
constexpr std::uint8_t QUEST_STATES_PER_ENTRY = 4;
constexpr std::uint8_t QUEST_STATE_BIT_WIDTH = 2;
constexpr int QUEST_MONSTER_SLOT_COUNT = 5;
constexpr int QUEST_MONSTER_INFO_STRIDE = 2;

struct QuestStateSlot
{
    std::size_t byteIndex{};
    std::uint8_t bitShift{};
};

QuestStateSlot MakeQuestStateSlot(int questIndex)
{
    QuestStateSlot slot{};
    if (questIndex < 0)
    {
        return slot;
    }

    slot.byteIndex = static_cast<std::size_t>(questIndex) / QUEST_STATES_PER_ENTRY;
    slot.bitShift =
        static_cast<std::uint8_t>((questIndex % QUEST_STATES_PER_ENTRY) * QUEST_STATE_BIT_WIDTH);
    return slot;
}

std::uint8_t ExtractQuestState(const std::uint8_t *questList, std::size_t storageSize,
                               int questIndex)
{
    if (questList == nullptr)
    {
        return QUEST_STATE_INVALID;
    }

    const auto slot = MakeQuestStateSlot(questIndex);
    if (slot.byteIndex >= storageSize)
    {
        return QUEST_STATE_INVALID;
    }

    return (questList[slot.byteIndex] >> slot.bitShift) & QUEST_STATE_MASK;
}

void StoreQuestState(std::uint8_t *questList, std::size_t storageSize, int questIndex,
                     std::uint8_t state)
{
    if (questList == nullptr)
    {
        return;
    }

    const auto slot = MakeQuestStateSlot(questIndex);
    if (slot.byteIndex >= storageSize)
    {
        return;
    }

    questList[slot.byteIndex] &= ~(QUEST_STATE_MASK << slot.bitShift);
    questList[slot.byteIndex] |= ((state & QUEST_STATE_MASK) << slot.bitShift);
}

std::size_t ComputeQuestListByteCount(int questCount)
{
    if (questCount <= 0)
    {
        return 0;
    }

    return static_cast<std::size_t>((questCount + (QUEST_STATES_PER_ENTRY - 1)) /
                                    QUEST_STATES_PER_ENTRY);
}

struct QuestAttributeFile
{
    short shQuestConditionNum;
    short shQuestRequestNum;
    WORD wNpcType;

    char strQuestName[32];

    QUEST_CLASS_ACT QuestAct[MAX_QUEST_CONDITION];
    QUEST_CLASS_REQUEST QuestRequest[MAX_QUEST_REQUEST];
};
} // namespace

CSQuest::CSQuest(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), m_byClass(255), m_byCurrQuestIndex(0), m_byCurrQuestIndexWnd(0),
      m_byStartQuestList(0), m_shCurrPage(0), m_byViewQuest(QUEST_VIEW_NONE),
      m_byQuestType(TYPE_QUEST), m_iStartX(REFERENCE_WIDTH - 190), m_iStartY(0)
{
    I18N::RegisterLocaleObserver(&CSQuest::RefreshDialogTextOnLocaleChangeCallback, this);
}

CSQuest::~CSQuest(void)
{
    I18N::UnregisterLocaleObserver(&CSQuest::RefreshDialogTextOnLocaleChangeCallback, this);
}

void CSQuest::RefreshDialogTextOnLocaleChangeCallback(void *context) noexcept
{
    static_cast<CSQuest *>(context)->RefreshDialogTextOnLocaleChange();
}

void CSQuest::RefreshDialogTextOnLocaleChange() noexcept
{
    if (g_iCurrentDialogScript < 0)
        return;

    try
    {
        ShowDialogText(g_iCurrentDialogScript);
    }
    catch (...)
    {
    }
}

bool CSQuest::IsInit(void)
{
    if (m_byClass == 255)
    {
        return true;
    }

    return false;
}

void CSQuest::clearQuest(void)
{
    m_byClass = 255;
    m_shCurrPage = 0;
    m_byViewQuest = QUEST_VIEW_NONE;
    m_byStartQuestList = 0;
    m_byCurrQuestIndex = 0;
    m_byCurrQuestIndexWnd = 0;
    m_byQuestList.fill(0);
    m_byEventCount.fill(0);
    std::fill(m_anKillMobType.begin(), m_anKillMobType.end(), -1);
    std::fill(m_anKillMobCount.begin(), m_anKillMobCount.end(), -1);
}

std::uint8_t CSQuest::getCurrQuestState(void)
{
    return CheckQuestState();
}

const wchar_t *CSQuest::GetNPCName(BYTE byQuestIndex)
{
    return getMonsterName(int(m_Quest[byQuestIndex].wNpcType));
}

wchar_t *CSQuest::getQuestTitle()
{
    return m_Quest[m_byCurrQuestIndex].strQuestName;
}

wchar_t *CSQuest::getQuestTitle(BYTE byQuestIndex)
{
    return m_Quest[byQuestIndex].strQuestName;
}

wchar_t *CSQuest::getQuestTitleWindow()
{
    return m_Quest[m_byCurrQuestIndexWnd].strQuestName;
}

void CSQuest::SetEventCount(std::uint8_t type, std::uint8_t count)
{
    if (type >= TYPE_QUEST_END)
    {
        return;
    }

    m_byEventCount[type] = count;
}

int CSQuest::GetEventCount(std::uint8_t byType)
{
    if (byType >= TYPE_QUEST_END)
    {
        return 0;
    }

    return m_byEventCount[byType];
}

bool CSQuest::OpenQuestScript(const wchar_t *filename)
{
    if (filename == nullptr || filename[0] == L'\0')
    {
        return false;
    }

#ifdef _WIN32
    const std::filesystem::path filePath(filename);
#else
    // The script path is Windows-spelled (backslashes, mixed case); resolve it
    // against the case-sensitive filesystem.
    const std::filesystem::path filePath(
        MuResolvePath(std::filesystem::path(filename).string().c_str()));
#endif
    std::ifstream file(filePath, std::ios::binary);
    if (!file)
    {
        wchar_t text[256];
        mu_swprintf_s(text, std::size(text), L"%ls - File not exist.", filename);
        return false;
    }

    m_Quest.fill({});

    const auto recordSize = sizeof(QuestAttributeFile);
    std::vector<std::uint8_t> buffer(recordSize);

    for (auto &quest : m_Quest)
    {
        if (!file.read(reinterpret_cast<char *>(buffer.data()),
                       static_cast<std::streamsize>(recordSize)))
        {
            return false;
        }

        BuxConvert(buffer.data(), static_cast<int>(buffer.size()));
        auto *current = reinterpret_cast<QuestAttributeFile *>(buffer.data());

        quest.shQuestConditionNum = current->shQuestConditionNum;
        quest.shQuestRequestNum = current->shQuestRequestNum;
        quest.wNpcType = current->wNpcType;
        CMultiLanguage::ConvertFromUtf8(quest.strQuestName, current->strQuestName);

        std::memcpy(quest.QuestAct, current->QuestAct, sizeof quest.QuestAct);
        std::memcpy(quest.QuestRequest, current->QuestRequest, sizeof quest.QuestRequest);
    }

    return true;
}

void CSQuest::setQuestLists(const std::uint8_t *byList, int num, CLASS_TYPE Class)
{
    if (Class != -1)
    {
        m_bOnce = true;
        bCheckNPC = false;

        m_byClass = gCharacterManager.GetBaseClass(Class);
    }

    m_byQuestList.fill(0);
    if (byList != nullptr)
    {
        const auto bytesToCopy =
            std::min<std::size_t>(ComputeQuestListByteCount(num), m_byQuestList.size());
        std::memcpy(m_byQuestList.data(), byList, bytesToCopy);
    }

    int i;
    if (CLASS_KNIGHT == m_byClass)
    {
        for (i = 0; i < num; ++i)
        {
            if (getQuestState(i) != QUEST_END)
                break;
        }
    }
    else if (CLASS_DARK == m_byClass || CLASS_DARK_LORD == m_byClass ||
             CLASS_RAGEFIGHTER == m_byClass)
    {
        for (i = 4; i < num; ++i)
        {
            if (getQuestState(i) != QUEST_END)
                break;
        }
    }
    else
    {
        for (i = 0; i < num; ++i)
        {
            if (QUEST_COMBO == i)
                continue;
            if (getQuestState(i) != QUEST_END)
                break;
        }
    }

    m_byCurrQuestIndex = i;
    m_byCurrQuestIndexWnd = i;

    Hero->byExtensionSkill = 0;
    if (getQuestState(QUEST_COMBO) == QUEST_END)
    {
        Hero->byExtensionSkill = 1;
    }
}

void CSQuest::setQuestList(int index, int result)
{
    if (index < 0 || index >= MAX_QUESTS)
    {
        return;
    }

    m_byCurrQuestIndex = static_cast<std::uint8_t>(index);
    m_byCurrQuestIndexWnd =
        std::max<std::uint8_t>(static_cast<std::uint8_t>(index), m_byCurrQuestIndexWnd);

    // Server protocol 0xA1/0xA2: 'result' is the complete byte holding the packed states
    // of all four quests in the same group as 'index'. Store it as-is.
    const auto byteIndex = static_cast<std::size_t>(index) / QUEST_STATES_PER_ENTRY;
    if (byteIndex < m_byQuestList.size())
    {
        m_byQuestList[byteIndex] = static_cast<std::uint8_t>(result);
    }

    Hero->byExtensionSkill = 0;
    if (getQuestState(QUEST_COMBO) == QUEST_END)
    {
        Hero->byExtensionSkill = 1;
    }
}

short CSQuest::FindQuestContext(QUEST_ATTRIBUTE *pQuest, int index)
{
    for (int i = 0; i < pQuest->shQuestConditionNum; ++i)
    {
        if (pQuest->QuestAct[i].byRequestClass[m_byClass] >= 1)
        {
            return pQuest->QuestAct[i].shQuestStartText[index];
        }
    }

    m_byCurrQuestIndex--;
    CheckQuestState();

    return m_shCurrPage;
}

bool CSQuest::CheckRequestCondition(QUEST_ATTRIBUTE *pQuest, bool bLastCheck)
{
    int iRequestType;
    for (int i = 0; i < pQuest->shQuestConditionNum; ++i)
    {
        if (pQuest->QuestAct[i].byRequestClass[m_byClass] != 0)
        {
            iRequestType = pQuest->QuestAct[i].byRequestType;
            for (int j = 0; j < pQuest->shQuestRequestNum; ++j)
            {
                if (pQuest->QuestRequest[j].byType == iRequestType ||
                    pQuest->QuestRequest[j].byType == 255)
                {
                    if (pQuest->QuestRequest[j].wCompleteQuestIndex != 65535)
                    {
                        if (getQuestState2(pQuest->QuestRequest[j].wCompleteQuestIndex) !=
                            QUEST_END)
                        {
                            m_shCurrPage = pQuest->QuestRequest[j].shErrorText;
                            m_byCurrState = QUEST_ERROR;
                            return false;
                        }
                    }
                    if (pQuest->QuestRequest[j].wLevelMin > 0)
                    {
                        WORD level = CharacterAttribute->Level;

                        if (pQuest->QuestRequest[j].wLevelMin > level)
                        {
                            m_shCurrPage = pQuest->QuestRequest[j].shErrorText;
                            m_byCurrState = QUEST_ERROR;
                            return false;
                        }
                    }
                    if (pQuest->QuestRequest[j].wLevelMax > 0)
                    {
                        WORD level = CharacterAttribute->Level;

                        if (pQuest->QuestRequest[j].wLevelMax < level)
                        {
                            m_shCurrPage = pQuest->QuestRequest[j].shErrorText;
                            m_byCurrState = QUEST_ERROR;
                            return false;
                        }
                    }
                    if (pQuest->QuestRequest[j].dwZen > 0)
                    {
                        if (bLastCheck)
                        {
                            DWORD gold = CharacterMachine->Gold;

                            m_dwNeedZen = pQuest->QuestRequest[j].dwZen;
                            if (m_dwNeedZen > gold)
                            {
                                m_shCurrPage = pQuest->QuestRequest[j].shErrorText;
                                m_byCurrState = QUEST_ERROR;
                                return false;
                            }
                        }
                        else
                        {
                            m_dwNeedZen = pQuest->QuestRequest[j].dwZen;
                        }
                    }
                }
            }
        }
    }
    return true;
}

bool CSQuest::CheckActCondition(QUEST_ATTRIBUTE *pQuest)
{
    for (int i = 0; i < pQuest->shQuestConditionNum; ++i)
    {
        if (pQuest->QuestAct[i].byRequestClass[m_byClass] >= 1)
        {
            switch (pQuest->QuestAct[i].byQuestType)
            {
            case QUEST_ITEM: {
                int itemType = (pQuest->QuestAct[i].wItemType * MAX_ITEM_INDEX) +
                               pQuest->QuestAct[i].byItemSubType;
                int itemLevel = -1;
                int itemNum = pQuest->QuestAct[i].byItemNum;

                itemLevel = pQuest->QuestAct[i].byItemLevel;

                if (FindQuestItemsInInven(itemType, itemNum, itemLevel))
                {
                    m_shCurrPage = FindQuestContext(pQuest, 1);
                    return false;
                }
            }
            break;

            case QUEST_MONSTER: {
                bool bFind = false;

                for (int j = 0; j < QUEST_MONSTER_SLOT_COUNT; ++j)
                {
                    if (m_anKillMobType[j] == int(pQuest->QuestAct[i].wItemType) &&
                        m_anKillMobCount[j] >= int(pQuest->QuestAct[i].byItemNum))
                    {
                        bFind = true;
                        break;
                    }
                }

                if (!bFind)
                {
                    m_shCurrPage = FindQuestContext(pQuest, 1);
                    return false;
                }
            }
            break;
            }
        }
    }
    return true;
}

std::uint8_t CSQuest::getQuestState(int questIndex)
{
    const int targetIndex = (questIndex < 0) ? m_byCurrQuestIndex : questIndex;
    const auto state = ExtractQuestState(m_byQuestList.data(), m_byQuestList.size(), targetIndex);
    const std::uint8_t normalizedState = (state == QUEST_STATE_INVALID) ? 0 : state;

    if (questIndex == -1)
    {
        m_byCurrState = normalizedState;
    }

    return normalizedState;
}

std::uint8_t CSQuest::getQuestState2(int questIndex)
{
    const auto state = ExtractQuestState(m_byQuestList.data(), m_byQuestList.size(), questIndex);
    return (state == QUEST_STATE_INVALID) ? 0 : state;
}

std::uint8_t CSQuest::CheckQuestState(std::uint8_t state)
{
    if (Hero->Class != -1)
    {
        if (m_bOnce)
        {
            m_bOnce = false;
            m_byClass = gCharacterManager.GetBaseClass(Hero->Class);
        }
    }

    QUEST_ATTRIBUTE *lpQuest = &m_Quest[m_byCurrQuestIndex];
    //    m_byCurrState = m_byQuestList[m_byCurrQuestIndex];

    if (state == QUEST_STATE_INVALID)
    {
        getQuestState();
    }
    else
    {
        m_byCurrState = state & QUEST_STATE_MASK;
    }
    switch (m_byCurrState)
    {
    case QUEST_NO: {
        if (CheckRequestCondition(lpQuest))
        {
            m_shCurrPage = FindQuestContext(lpQuest, 0);
        }
    }
    break;

    case QUEST_ING: {
        if (CheckActCondition(lpQuest))
        {
            m_shCurrPage = FindQuestContext(lpQuest, 2);
            m_byCurrState = QUEST_ITEM;
        }
    }
    break;

    case QUEST_END: {
        m_shCurrPage = FindQuestContext(lpQuest, 3);
    }
    break;
    }

    return m_byCurrState;
}

void CSQuest::ShowDialogText(int iDialogIndex)
{
    g_iCurrentDialogScript = iDialogIndex;

    const wchar_t *text = I18N::Dialog::Lookup(iDialogIndex);
    g_iNumLineMessageBoxCustom =
        SeparateTextIntoLines(text, g_lpszMessageBoxCustom[0], NUM_LINE_CMB, MAX_LENGTH_CMB);

    wchar_t lpszAnswer[MAX_LENGTH_ANSWER + 8]{};
    g_iNumAnswer = 0;
    std::memset(g_lpszDialogAnswer, 0, sizeof g_lpszDialogAnswer);

    const auto &entry = GameLogic::Quests::Dialog::GetEntry(iDialogIndex);
    int iTextSize = 0;

    for (int i = 0; i < entry.numAnswer; ++i)
    {
        const wchar_t *answerText =
            I18N::Dialog::Lookup(GameLogic::Quests::Dialog::DialogAnswerLegacyId(iDialogIndex, i));
        mu_swprintf_s(lpszAnswer, std::size(lpszAnswer), L"%d) %ls", i + 1, answerText);
        int iNumLine = SeparateTextIntoLines(lpszAnswer, g_lpszDialogAnswer[i][0], NUM_LINE_DA,
                                             MAX_LENGTH_CMB);
        if (iNumLine < NUM_LINE_DA - 1)
        {
            g_lpszDialogAnswer[i][iNumLine][0] = '\0';
        }

        g_iNumAnswer++;
        iTextSize = i;
    }

    if (0 == entry.numAnswer)
    {
        mu_swprintf_s(lpszAnswer, std::size(lpszAnswer), L"%d) %ls", iTextSize + 1,
                      I18N::Game::ConversationIsOver);
        wcscpy_s(g_lpszDialogAnswer[0][0], MAX_LENGTH_CMB, lpszAnswer);
        g_iNumAnswer = 1;
    }
}

void CSQuest::ShowQuestPreviewWindow(int index)
{
    if (index != -1)
    {
        m_byCurrQuestIndex = static_cast<std::uint8_t>(index);
    }

    m_byViewQuest = QUEST_VIEW_PREVIEW;

    const std::uint8_t previousIndex = m_byCurrQuestIndex;
    m_byCurrQuestIndex = m_byCurrQuestIndexWnd;

    CheckQuestState();
    ShowDialogText(m_shCurrPage);

    m_byCurrQuestIndex = previousIndex;
}

void CSQuest::ShowQuestNpcWindow(int index)
{
    if (index != -1)
    {
        m_byCurrQuestIndex = static_cast<std::uint8_t>(index);
    }

    g_bEventChipDialogEnable = EVENT_NONE;

    CheckQuestState();
    ShowDialogText(m_shCurrPage);
}

bool CSQuest::BeQuestItem(void)
{
    bool bCompleteItem = false;

    if (m_byCurrState == QUEST_ING)
    {
        QUEST_ATTRIBUTE *pQuest = &m_Quest[m_byCurrQuestIndex];

        for (int i = 0; i < pQuest->shQuestConditionNum; ++i)
        {
            if (pQuest->QuestAct[i].byRequestClass[m_byClass] > 0)
            {
                switch (pQuest->QuestAct[i].byQuestType)
                {
                case QUEST_ITEM: {
                    bCompleteItem = true;

                    int itemType = (pQuest->QuestAct[i].wItemType * MAX_ITEM_INDEX) +
                                   pQuest->QuestAct[i].byItemSubType;
                    int itemLevel = -1;
                    int itemNum = pQuest->QuestAct[i].byItemNum;

                    itemLevel = pQuest->QuestAct[i].byItemLevel;
                    if (FindQuestItemsInInven(itemType, itemNum, itemLevel))
                    {
                        return false;
                    }
                }
                break;
                case QUEST_MONSTER: {
                    bCompleteItem = true;
                    bool bFind = false;

                    for (int j = 0; j < QUEST_MONSTER_SLOT_COUNT; ++j)
                    {
                        if (m_anKillMobType[j] == int(pQuest->QuestAct[i].wItemType) &&
                            m_anKillMobCount[j] >= int(pQuest->QuestAct[i].byItemNum))
                        {
                            bFind = true;
                            break;
                        }
                    }

                    if (!bFind)
                        return false;
                }
                break;
                }
            }
        }
    }

    return bCompleteItem;
}

int CSQuest::FindQuestItemsInInven(int nType, int nCount, int nLevel)
{
    auto *pInvenCtrl = &sessionKeeper_.GameData()->Inventory(InventoryRole::Player);

    int nItemsInInven = pInvenCtrl->GetNumberOfItems();
    ITEM *pItem = nullptr;
    int nFindItemCount = 0;

    for (int i = 0; i < nItemsInInven; ++i)
    {
        pItem = pInvenCtrl->GetItem(i);
        if (nType == pItem->Type)
        {
            if (nLevel == -1 || nLevel == pItem->Level)
            {
                if (nCount <= ++nFindItemCount)
                    return 0;
            }
        }
    }

    return nCount - nFindItemCount;
}

int CSQuest::GetKillMobCount(int nMobType)
{
    for (int i = 0; i < QUEST_MONSTER_SLOT_COUNT; ++i)
    {
        if (nMobType == m_anKillMobType[i])
            return m_anKillMobCount[i];
    }

    return 0;
}

bool CSQuest::ProcessNextProgress()
{
    QUEST_ATTRIBUTE *lpQuest = &m_Quest[m_byCurrQuestIndex];
    if (!CheckRequestCondition(lpQuest, true))
    {
        ShowDialogText(m_shCurrPage);
        return true;
    }
    else
    {
        return false;
    }
}

void CSQuest::SetKillMobInfo(const int *anKillMobInfo)
{
    if (anKillMobInfo == nullptr)
    {
        std::fill(m_anKillMobType.begin(), m_anKillMobType.end(), -1);
        std::fill(m_anKillMobCount.begin(), m_anKillMobCount.end(), -1);
        return;
    }

    for (int i = 0; i < QUEST_MONSTER_SLOT_COUNT; ++i)
    {
        const int typeIndex = i * QUEST_MONSTER_INFO_STRIDE;
        m_anKillMobType[i] = anKillMobInfo[typeIndex];
        m_anKillMobCount[i] = anKillMobInfo[typeIndex + 1];
    }
}

// Auto-generated by the one-shot DialogImporter tool (removed; see git history).
// Source: Data/Local/Eng/Dialog_eng.bmd (structural fields only)

namespace GameLogic::Quests::Dialog
{
namespace
{

constexpr Entry kDialogEntries[MaxDialog] = {
    {0, {{-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 0
    {0, {{-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 1
    {0, {{-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 2
    {0, {{-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 3
    {0, {{-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 4
    {0, {{-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 5
    {0, {{-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 6
    {0, {{-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 7
    {0, {{-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 8
    {0, {{-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 9
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 10
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 11
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 12
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 13
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 14
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 15
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 16
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 17
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 18
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 19
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 20
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 21
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 22
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 23
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 24
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 25
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 26
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 27
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 28
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 29
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 30
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 31
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 32
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 33
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 34
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 35
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 36
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 37
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 38
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 39
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 40
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 41
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 42
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 43
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 44
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 45
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 46
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 47
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 48
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 49
    {1,
     {{74, -1}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 50
    {1,
     {{51, 2}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 51
    {1,
     {{75, -1}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 52
    {3,
     {{52, -1},
      {54, 1},
      {53, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 53
    {1,
     {{54, 2}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 54
    {1,
     {{55, 3}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 55
    {1,
     {{57, -1}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 56
    {2,
     {{56, -1},
      {80, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 57
    {2,
     {{57, -1},
      {81, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 58
    {2,
     {{58, -1},
      {59, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 59
    {2,
     {{61, 1}, {60, 2}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 60
    {1,
     {{61, 2}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 61
    {1,
     {{76, -1}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 62
    {1,
     {{79, -1}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 63
    {1,
     {{77, -1}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 64
    {1,
     {{65, 2}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 65
    {1,
     {{78, -1}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 66
    {1,
     {{82, -1}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 67
    {1,
     {{68, 2}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 68
    {1,
     {{70, -1}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 69
    {1,
     {{70, 3}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 70
    {1,
     {{71, 2}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 71
    {1,
     {{72, 2}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 72
    {1,
     {{73, 2}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 73
    {1,
     {{74, 2}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 74
    {1,
     {{53, -1}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 75
    {1,
     {{63, -1}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 76
    {2,
     {{65, 1}, {77, 2}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 77
    {1,
     {{78, 3}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 78
    {1,
     {{79, 3}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 79
    {2,
     {{57, -1},
      {58, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 80
    {2,
     {{58, -1},
      {59, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 81
    {2,
     {{68, 1}, {82, 2}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 82
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 83
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 84
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 85
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 86
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 87
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 88
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 89
    {1,
     {{91, -1}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 90
    {2,
     {{92, 1}, {91, 2}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 91
    {1,
     {{92, 2}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 92
    {1,
     {{94, -1}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 93
    {1,
     {{94, 3}, {-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}},  // idx 94
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 95
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 96
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 97
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 98
    {0, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 99
    {1,
     {{101, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 100
    {1,
     {{101, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 101
    {1,
     {{103, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 102
    {1,
     {{104, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 103
    {3,
     {{102, -1},
      {105, 1},
      {104, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 104
    {1,
     {{105, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 105
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 106
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 107
    {1,
     {{109, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 108
    {1,
     {{110, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 109
    {1,
     {{111, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 110
    {1,
     {{112, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 111
    {1,
     {{112, 3},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 112
    {1,
     {{113, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 113
    {1,
     {{114, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 114
    {1,
     {{101, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 115
    {1,
     {{116, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 116
    {1,
     {{117, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 117
    {1,
     {{118, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 118
    {1,
     {{119, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 119
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 120
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 121
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 122
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 123
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 124
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 125
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 126
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 127
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 128
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 129
    {1,
     {{131, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 130
    {1,
     {{132, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 131
    {1,
     {{133, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 132
    {1,
     {{134, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 133
    {1,
     {{134, 3},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 134
    {1,
     {{136, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 135
    {1,
     {{137, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 136
    {3,
     {{135, -1},
      {138, 1},
      {137, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 137
    {1,
     {{138, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 138
    {1,
     {{140, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 139
    {1,
     {{141, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 140
    {1,
     {{141, 3},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 141
    {1,
     {{143, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 142
    {1,
     {{143, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 143
    {1,
     {{144, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 144
    {1,
     {{145, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 145
    {1,
     {{146, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 146
    {1,
     {{148, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 147
    {1,
     {{149, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 148
    {1,
     {{150, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 149
    {1,
     {{151, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 150
    {3,
     {{147, -1},
      {152, 1},
      {151, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 151
    {1,
     {{153, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 152
    {1,
     {{153, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 153
    {1,
     {{155, 3},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 154
    {1,
     {{156, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 155
    {1,
     {{156, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 156
    {1,
     {{157, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 157
    {1,
     {{159, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 158
    {1,
     {{160, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 159
    {1,
     {{161, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 160
    {3,
     {{158, -1},
      {162, 1},
      {161, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 161
    {1,
     {{163, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 162
    {1,
     {{163, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 163
    {1,
     {{165, 3},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 164
    {1,
     {{165, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 165
    {1,
     {{166, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 166
    {1,
     {{167, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 167
    {1,
     {{169, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 168
    {1,
     {{170, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 169
    {1,
     {{171, -1},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 170
    {3,
     {{168, -1},
      {172, 1},
      {171, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 171
    {1,
     {{172, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 172
    {1,
     {{174, 3},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 173
    {1,
     {{174, 2},
      {-1, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0},
      {0, 0}}}, // idx 174
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 175
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 176
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 177
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 178
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 179
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 180
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 181
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 182
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 183
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 184
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 185
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 186
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 187
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 188
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 189
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 190
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 191
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 192
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 193
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 194
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 195
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 196
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 197
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 198
    {0,
     {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, // idx 199
};

constexpr Entry kEmptyEntry = {0, {}};

} // namespace

const Entry &GetEntry(int dialogIndex) noexcept
{
    if (dialogIndex < 0 || dialogIndex >= MaxDialog)
        return kEmptyEntry;
    return kDialogEntries[dialogIndex];
}

} // namespace GameLogic::Quests::Dialog

#define QM_NPCDIALOGUE_FILE L"Data\\Local\\NPCDialogue.bmd"
#define QM_QUESTPROGRESS_FILE L"Data\\Local\\QuestProgress.bmd"

CQuestMng::CQuestMng(SessionKeeper &keeper) noexcept
    : SessionLegacyCalls(keeper), g_strSelectedML(keeper.AssetLanguage()),
      g_ErrorReport(keeper.ErrorReport())
{
    m_nNPCIndex = 0;
    m_szNPCName = nullptr;
}

CQuestMng::~CQuestMng()
{
}

std::vector<std::wstring> CQuestMng::LoadQuestScript()
{
    if (const QuestScriptData *const cached = applicationKeeper_.QuestScripts(); cached != nullptr)
    {
        questScripts_ = cached;
        return cached->failureMessages;
    }

    auto scripts = std::make_unique<QuestScriptData>();
    for (auto failure : {LoadNPCDialogueScript(scripts->npcDialogues),
                         LoadQuestProgressScript(scripts->questProgress),
                         LoadQuestWordsScript(scripts->questWords)})
    {
        if (!failure.empty())
            scripts->failureMessages.push_back(std::move(failure));
    }
    if (!applicationKeeper_.PublishQuestScripts(std::move(scripts)))
    {
        return {};
    }
    questScripts_ = applicationKeeper_.QuestScripts();
    return questScripts_->failureMessages;
}

const QuestScriptData &CQuestMng::QuestScripts() const noexcept
{
    static const QuestScriptData empty;
    return questScripts_ != nullptr ? *questScripts_ : empty;
}

std::wstring CQuestMng::LoadNPCDialogueScript(NPCDialogueMap &dialogues)
{
    FILE *fp = ::_wfopen(QM_NPCDIALOGUE_FILE, L"rb");
    if (fp == NULL)
    {
        wchar_t szMessage[256];
        mu_swprintf(szMessage, L"%ls file not found.\r\n", QM_NPCDIALOGUE_FILE);
        g_ErrorReport.Write(szMessage);
        return szMessage;
    }

    const int nSize = sizeof(DWORD) + sizeof(SNPCDialogue);
    BYTE abyBuffer[nSize];
    DWORD dwIndex = 0;
    SNPCDialogue sNPCDialogue;

    while (0 != ::fread(abyBuffer, nSize, 1, fp))
    {
        ::BuxConvert(abyBuffer, nSize);

        ::memcpy(&dwIndex, abyBuffer, sizeof(DWORD));
        ::memcpy(&sNPCDialogue, abyBuffer + sizeof(DWORD), sizeof(SNPCDialogue));

        dialogues.insert(std::make_pair(dwIndex, sNPCDialogue));
    }

    ::fclose(fp);
    return {};
}

std::wstring CQuestMng::LoadQuestProgressScript(QuestProgressMap &progress)
{
    FILE *fp = ::_wfopen(QM_QUESTPROGRESS_FILE, L"rb");
    if (fp == NULL)
    {
        wchar_t szMessage[256];
        ::mu_swprintf(szMessage, L"%ls file not found.\r\n", QM_QUESTPROGRESS_FILE);
        g_ErrorReport.Write(szMessage);
        return szMessage;
    }

    const int nSize = sizeof(DWORD) + sizeof(SQuestProgress);
    BYTE abyBuffer[nSize];
    DWORD dwIndex = 0;
    SQuestProgress sQuestProgress;

    while (0 != ::fread(abyBuffer, nSize, 1, fp))
    {
        ::BuxConvert(abyBuffer, nSize);

        ::memcpy(&dwIndex, abyBuffer, sizeof(DWORD));
        ::memcpy(&sQuestProgress, abyBuffer + sizeof(DWORD), sizeof(SQuestProgress));

        progress.insert(std::make_pair(dwIndex, sQuestProgress));
    }

    ::fclose(fp);
    return {};
}

std::wstring CQuestMng::LoadQuestWordsScript(QuestWordsMap &words)
{
    const std::wstring questWordsFile =
        L"Data\\Local\\" + g_strSelectedML + L"\\QuestWords_" + g_strSelectedML + L".bmd";
    FILE *fp = ::_wfopen(questWordsFile.c_str(), L"rb");
    if (fp == NULL)
    {
        wchar_t szMessage[256];
        ::mu_swprintf(szMessage, L"%ls file not found.\r\n", questWordsFile.c_str());
        g_ErrorReport.Write(szMessage);
        return szMessage;
    }

#pragma pack(push, 1)
    struct SQuestWordsHeader
    {
        int m_nIndex;
        short m_nWordsLen;
    };
#pragma pack(pop)

    int nSize = sizeof(SQuestWordsHeader);
    SQuestWordsHeader sQuestWordsHeader;
    char rawWords[1024]{};
    wchar_t szWords[1024]{};

    while (0 != ::fread(&sQuestWordsHeader, nSize, 1, fp))
    {
        ::BuxConvert((BYTE *)&sQuestWordsHeader, nSize);

        ::fread(rawWords, sQuestWordsHeader.m_nWordsLen, 1, fp);
        ::BuxConvert((BYTE *)rawWords, sQuestWordsHeader.m_nWordsLen);
        CMultiLanguage::ConvertFromUtf8(szWords, rawWords, 1024);

        std::wstring strWords = szWords;
        words.insert(std::make_pair(sQuestWordsHeader.m_nIndex, strWords));
    }

    ::fclose(fp);
    return {};
}

void CQuestMng::SetQuestRequestReward(const BYTE *pbyRequestRewardPacket)
{
    auto pRequestRewardPacket = (LPPMSG_NPC_QUESTEXP_INFO)pbyRequestRewardPacket;
    DWORD dwQuestIndex = pRequestRewardPacket->m_dwQuestIndex;
    int i;

    const SQuestRequestReward *pOldRequestReward = GetRequestReward(dwQuestIndex);
    if (pOldRequestReward)
    {
        for (i = 0; i < pOldRequestReward->m_byRequestCount; ++i)
            g_pNewItemMng->DeleteItem(pOldRequestReward->m_aRequest[i].m_pItem);
        BYTE byRewardCount =
            pOldRequestReward->m_byGeneralRewardCount + pOldRequestReward->m_byRandRewardCount;
        for (i = 0; i < byRewardCount; ++i)
            g_pNewItemMng->DeleteItem(pOldRequestReward->m_aReward[i].m_pItem);
    }

    SQuestRequestReward sRequestReward;
    ::memset(&sRequestReward, 0, sizeof(SQuestRequestReward));

    auto pRequestPacket =
        (LPNPC_QUESTEXP_REQUEST_INFO)(pbyRequestRewardPacket + sizeof(PMSG_NPC_QUESTEXP_INFO));

    if (pRequestPacket->m_dwType == QUEST_REQUEST_NONE ||
        pRequestRewardPacket->m_byRequestCount == 0)
    {
        sRequestReward.m_byRequestCount = 1;
    }
    else
    {
        sRequestReward.m_byRequestCount = pRequestRewardPacket->m_byRequestCount;
        for (i = 0; i < sRequestReward.m_byRequestCount; ++i)
        {
            sRequestReward.m_aRequest[i].m_dwType = pRequestPacket->m_dwType;
            sRequestReward.m_aRequest[i].m_wIndex = pRequestPacket->m_wIndex;
            sRequestReward.m_aRequest[i].m_dwValue = pRequestPacket->m_dwValue;
#ifdef ASG_ADD_TIME_LIMIT_QUEST
            sRequestReward.m_aRequest[i].m_dwCurValue = pRequestPacket->m_dwCurValue;
#else  // ASG_ADD_TIME_LIMIT_QUEST
            sRequestReward.m_aRequest[i].m_wCurValue = pRequestPacket->m_wCurValue;
#endif // ASG_ADD_TIME_LIMIT_QUEST
            if (pRequestPacket->m_dwType == QUEST_REQUEST_ITEM)
                sRequestReward.m_aRequest[i].m_pItem =
                    g_pNewItemMng->CreateItemOld(pRequestPacket->m_byItemInfo);
            ++pRequestPacket;
        }
    }

    auto pRewardPacket =
        (LPNPC_QUESTEXP_REWARD_INFO)(pbyRequestRewardPacket + sizeof(PMSG_NPC_QUESTEXP_INFO) +
                                     sizeof(NPC_QUESTEXP_REQUEST_INFO) * 5);

    if (pRewardPacket->m_dwType == QUEST_REWARD_NONE || pRequestRewardPacket->m_byRewardCount == 0)
    {
        sRequestReward.m_byGeneralRewardCount = 1;
    }
    else
    {
        sRequestReward.m_byRandGiveCount = pRequestRewardPacket->m_byRandRewardCount;

        BYTE byGeneralCount = 0;
        BYTE byRandCount = 0;
        SQuestReward aTempRandReward[5];
        ::memset(aTempRandReward, 0, sizeof(SQuestReward) * 5);

        for (i = 0; i < pRequestRewardPacket->m_byRewardCount; ++i)
        {
            if (QUEST_REWARD_TYPE(pRewardPacket->m_dwType & 0xFFE0) == QUEST_REWARD_RANDOM)
            {
                aTempRandReward[byRandCount].m_dwType = pRewardPacket->m_dwType & 0x1F;
                aTempRandReward[byRandCount].m_wIndex = pRewardPacket->m_wIndex;
                aTempRandReward[byRandCount].m_dwValue = pRewardPacket->m_dwValue;
                if (aTempRandReward[byRandCount].m_dwType == QUEST_REWARD_ITEM)
                    aTempRandReward[byRandCount].m_pItem =
                        g_pNewItemMng->CreateItemOld(pRewardPacket->m_byItemInfo);
                ++byRandCount;
            }
            else
            {
                sRequestReward.m_aReward[byGeneralCount].m_dwType = pRewardPacket->m_dwType;
                sRequestReward.m_aReward[byGeneralCount].m_wIndex = pRewardPacket->m_wIndex;
                sRequestReward.m_aReward[byGeneralCount].m_dwValue = pRewardPacket->m_dwValue;
                if (pRewardPacket->m_dwType == QUEST_REWARD_ITEM)
                    sRequestReward.m_aReward[byGeneralCount].m_pItem =
                        g_pNewItemMng->CreateItemOld(pRewardPacket->m_byItemInfo);
                ++byGeneralCount;
            }

            ++pRewardPacket;
        }

        sRequestReward.m_byGeneralRewardCount = byGeneralCount;
        sRequestReward.m_byRandRewardCount = byRandCount;

        for (i = 0; i < sRequestReward.m_byRandRewardCount; ++i)
            sRequestReward.m_aReward[byGeneralCount++] = aTempRandReward[i];
    }

    m_mapQuestRequestReward[dwQuestIndex] = sRequestReward;
    ++requestRewardRevision_;
}

const SQuestRequestReward *CQuestMng::GetRequestReward(DWORD dwQuestIndex)
{
    QuestRequestRewardMap::const_iterator iter = m_mapQuestRequestReward.find(dwQuestIndex);
    if (iter == m_mapQuestRequestReward.end())
        return NULL;

    return &(iter->second);
}

int CQuestMng::GetNPCIndex()
{
    return m_nNPCIndex;
}

const wchar_t *CQuestMng::GetNPCName()
{
    return m_szNPCName;
}

void CQuestMng::SetCurQuestProgress(DWORD dwQuestIndex)
{
    AddCurQuestIndexList(dwQuestIndex);

    if (LOWORD(dwQuestIndex) == 0x00FF)
    {
        if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_QUEST_PROGRESS))
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_QUEST_PROGRESS);
        if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_QUEST_PROGRESS_ETC))
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_QUEST_PROGRESS_ETC);

        g_pSystemLogBox->AddText(I18N::Game::YouVeSuccessfullyCompletedTheQuest,
                                 SEASON3B::TYPE_ERROR_MESSAGE);

        return;
    }

    const QuestProgressMap &progress = QuestScripts().questProgress;
    QuestProgressMap::const_iterator iter = progress.find(dwQuestIndex);
    if (iter == progress.end())
    {
        wchar_t szMessage[128];
        ::mu_swprintf(szMessage, L"Quest progress entry missing for index 0x%08X\r\n",
                      dwQuestIndex);
        g_ErrorReport.Write(szMessage);
        return;
    }

    if (0 == iter->second.m_byUIType)
    {
        g_pQuestProgress->SetContents(dwQuestIndex);
        if (!g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_QUEST_PROGRESS))
            g_pNewUISystem->Show(SEASON3B::INTERFACE_QUEST_PROGRESS);
    }
    else
    {
        g_pQuestProgressByEtc->SetContents(dwQuestIndex);
        if (!g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_QUEST_PROGRESS_ETC))
            g_pNewUISystem->Show(SEASON3B::INTERFACE_QUEST_PROGRESS_ETC);
    }
}

const wchar_t *CQuestMng::GetWords(int nWordsIndex)
{
    const QuestWordsMap &words = QuestScripts().questWords;
    QuestWordsMap::const_iterator iter = words.find(nWordsIndex);
    if (iter == words.end())
        return NULL;

    return iter->second.c_str();
}

const wchar_t *CQuestMng::GetNPCDlgNPCWords(DWORD dwDlgState)
{
    if (0 == m_nNPCIndex)
        return NULL;

    DWORD dwNPCDlgIndex = (DWORD)m_nNPCIndex * 0x10000 + dwDlgState;

    const NPCDialogueMap &dialogues = QuestScripts().npcDialogues;
    NPCDialogueMap::const_iterator iter = dialogues.find(dwNPCDlgIndex);
    if (iter == dialogues.end())
        return NULL;

    return GetWords(iter->second.m_nNPCWords);
}

const wchar_t *CQuestMng::GetNPCDlgAnswer(DWORD dwDlgState, int nAnswer)
{
    if (0 == m_nNPCIndex)
        return NULL;

    _ASSERT(0 <= nAnswer || nAnswer < QM_MAX_ND_ANSWER);

    DWORD dwNPCDlgIndex = (DWORD)m_nNPCIndex * 0x10000 + dwDlgState;

    const NPCDialogueMap &dialogues = QuestScripts().npcDialogues;
    NPCDialogueMap::const_iterator iter = dialogues.find(dwNPCDlgIndex);
    if (iter == dialogues.end())
        return NULL;

    DWORD nNowAnswer = iter->second.m_anAnswer[nAnswer * 2];
    if (0 == nNowAnswer)
        return NULL;

    return GetWords(nNowAnswer);
}

int CQuestMng::GetNPCDlgAnswerResult(DWORD dwDlgState, int nAnswer)
{
    if (0 == m_nNPCIndex)
        return 0;

    _ASSERT(0 <= nAnswer || nAnswer < QM_MAX_ND_ANSWER);

    DWORD dwNPCDlgIndex = (DWORD)m_nNPCIndex * 0x10000 + dwDlgState;

    const NPCDialogueMap &dialogues = QuestScripts().npcDialogues;
    NPCDialogueMap::const_iterator iter = dialogues.find(dwNPCDlgIndex);
    if (iter == dialogues.end())
        return -1;

    DWORD nNowAnswer = iter->second.m_anAnswer[nAnswer * 2];
    if (0 == nNowAnswer)
        return -1;

    return iter->second.m_anAnswer[nAnswer * 2 + 1];
}

const wchar_t *CQuestMng::GetNPCWords(DWORD dwQuestIndex)
{
    const QuestProgressMap &progress = QuestScripts().questProgress;
    QuestProgressMap::const_iterator iter = progress.find(dwQuestIndex);
    if (iter == progress.end())
        return NULL;

    return GetWords(iter->second.m_nNPCWords);
}

const wchar_t *CQuestMng::GetPlayerWords(DWORD dwQuestIndex)
{
    const QuestProgressMap &progress = QuestScripts().questProgress;
    QuestProgressMap::const_iterator iter = progress.find(dwQuestIndex);
    if (iter == progress.end())
        return NULL;

    return GetWords(iter->second.m_nPlayerWords);
}

const wchar_t *CQuestMng::GetAnswer(DWORD dwQuestIndex, int nAnswer)
{
    _ASSERT(0 <= nAnswer || nAnswer < QM_MAX_ANSWER);

    const QuestProgressMap &progress = QuestScripts().questProgress;
    QuestProgressMap::const_iterator iter = progress.find(dwQuestIndex);
    if (iter == progress.end())
        return NULL;

    DWORD nNowAnswer = iter->second.m_anAnswer[nAnswer];
    if (0 == nNowAnswer)
        return NULL;

    return GetWords(nNowAnswer);
}

const wchar_t *CQuestMng::GetSubject(DWORD dwQuestIndex)
{
    const QuestProgressMap &progress = QuestScripts().questProgress;
    QuestProgressMap::const_iterator iter = progress.find(dwQuestIndex);
    if (iter == progress.end())
        return NULL;

    return GetWords(iter->second.m_nSubject);
}

const wchar_t *CQuestMng::GetSummary(DWORD dwQuestIndex)
{
    const QuestProgressMap &progress = QuestScripts().questProgress;
    QuestProgressMap::const_iterator iter = progress.find(dwQuestIndex);
    if (iter == progress.end())
        return NULL;

    return GetWords(iter->second.m_nSummary);
}

bool CQuestMng::IsRequestRewardQS(DWORD dwQuestIndex)
{
    const QuestProgressMap &progress = QuestScripts().questProgress;
    QuestProgressMap::const_iterator iter = progress.find(dwQuestIndex);
    _ASSERT(iter != progress.end());

    if (0 == iter->second.m_anAnswer[0])
        return true;
    else
        return false;
}

bool CQuestMng::GetRequestRewardText(SRequestRewardText *aDest, int nDestCount, DWORD dwQuestIndex)
{
    SQuestRequestReward *pRequestReward = &m_mapQuestRequestReward[dwQuestIndex];

    if (pRequestReward->m_byRequestCount + pRequestReward->m_byGeneralRewardCount +
            pRequestReward->m_byRandRewardCount + 3 >
        nDestCount)
        return false;

    ::memset(aDest, 0, sizeof(SRequestRewardText) * nDestCount);

    int nLine = 0;
    bool bRequestComplete = true;
    int i;

    aDest[nLine].m_fontRole = LegacyFontRole::Bold;
    aDest[nLine].m_dwColor = ARGB(255, 179, 230, 77);
    wcscpy(aDest[nLine++].m_szText, I18N::Game::Requirements);

    SQuestRequest *pRequestInfo;
    for (i = 0; i < pRequestReward->m_byRequestCount; ++i, ++nLine)
    {
        pRequestInfo = &pRequestReward->m_aRequest[i];

        aDest[nLine].m_fontRole = LegacyFontRole::Normal;
        aDest[nLine].m_eRequestReward = RRC_REQUEST;
        aDest[nLine].m_dwType = pRequestInfo->m_dwType;
        aDest[nLine].m_wIndex = pRequestInfo->m_wIndex;
        aDest[nLine].m_pItem = pRequestInfo->m_pItem;

        switch (pRequestInfo->m_dwType)
        {
        case QUEST_REQUEST_NONE:
            aDest[nLine].m_dwColor = ARGB(255, 223, 191, 103);
            wcscpy(aDest[nLine].m_szText, I18N::Game::None);
            break;

#ifdef ASG_ADD_TIME_LIMIT_QUEST
        case QUEST_REQUEST_MONSTER:
        case QUEST_REQUEST_ITEM:
        case QUEST_REQUEST_LEVEL:
        case QUEST_REQUEST_ZEN:
        case QUEST_REQUEST_PVP_POINT:
            if (pRequestInfo->m_dwCurValue < pRequestInfo->m_dwValue)
            {
                aDest[nLine].m_dwColor = ARGB(255, 255, 30, 30);
                bRequestComplete = false;
            }
            else
                aDest[nLine].m_dwColor = ARGB(255, 223, 191, 103);

            switch (pRequestInfo->m_dwType)
            {
            case QUEST_REQUEST_MONSTER:
                ::mu_swprintf(aDest[nLine].m_szText, L"Mon.: %ls x %lu/%lu",
                              getMonsterName(int(pRequestInfo->m_wIndex)),
                              MIN(pRequestInfo->m_dwCurValue, pRequestInfo->m_dwValue),
                              pRequestInfo->m_dwValue);
                break;
            case QUEST_REQUEST_ITEM: {
                wchar_t szItemName[32];
                GetItemName((int)pRequestInfo->m_pItem->Type,
                    (pRequestInfo->m_pItem->Level, szItemName);
                ::mu_swprintf(aDest[nLine].m_szText, L"Item: %ls x %lu/%lu", szItemName,
                    MIN(pRequestInfo->m_dwCurValue, pRequestInfo->m_dwValue),
                    pRequestInfo->m_dwValue);
            }
            break;
            case QUEST_REQUEST_LEVEL:
                ::mu_swprintf(aDest[nLine].m_szText, L"Level: %lu %ls", pRequestInfo->m_dwValue,
                              I18N::Game::Minimum);
                break;
            case QUEST_REQUEST_ZEN:
                ::mu_swprintf(aDest[nLine].m_szText, L"Zen : %lu", pRequestInfo->m_dwValue);
                break;
            case QUEST_REQUEST_PVP_POINT:
                mu_swprintf(aDest[nLine].m_szText, I18N::Game::EnemyGensMemberXLuLu,
                            MIN(pRequestInfo->m_dwCurValue, pRequestInfo->m_dwValue),
                            pRequestInfo->m_dwValue);
                break;
            }
            break;
#endif // ASG_ADD_TIME_LIMIT_QUEST

#ifndef ASG_ADD_TIME_LIMIT_QUEST
        case QUEST_REQUEST_MONSTER:
            if ((DWORD)pRequestInfo->m_wCurValue < pRequestInfo->m_dwValue)
            {
                aDest[nLine].m_dwColor = ARGB(255, 255, 30, 30);
                bRequestComplete = false;
            }
            else
            {
                aDest[nLine].m_dwColor = ARGB(255, 223, 191, 103);
            }

            {
                auto text = getMonsterName(int(pRequestInfo->m_wIndex));
                mu_swprintf(aDest[nLine].m_szText, L"Mon.: %ls x %lu/%lu", text,
                            MIN((DWORD)pRequestInfo->m_wCurValue, pRequestInfo->m_dwValue),
                            pRequestInfo->m_dwValue);
            }
            break;
#endif // ASG_ADD_TIME_LIMIT_QUEST

        case QUEST_REQUEST_SKILL:
#ifdef ASG_ADD_TIME_LIMIT_QUEST
            if (0 == pRequestInfo->m_dwCurValue)
#else  // ASG_ADD_TIME_LIMIT_QUEST
            if (0 == pRequestInfo->m_wCurValue)
#endif // ASG_ADD_TIME_LIMIT_QUEST
            {
                aDest[nLine].m_dwColor = ARGB(255, 255, 30, 30);
                bRequestComplete = false;
            }
            else
                aDest[nLine].m_dwColor = ARGB(255, 223, 191, 103);

            ::mu_swprintf(aDest[nLine].m_szText, L"Skill: %ls",
                          SkillAttribute[pRequestInfo->m_wIndex].Name);
            break;

#ifndef ASG_ADD_TIME_LIMIT_QUEST
        case QUEST_REQUEST_ITEM:
            if ((DWORD)pRequestInfo->m_wCurValue < pRequestInfo->m_dwValue)
            {
                aDest[nLine].m_dwColor = ARGB(255, 255, 30, 30);
                bRequestComplete = false;
            }
            else
                aDest[nLine].m_dwColor = ARGB(255, 223, 191, 103);

            wchar_t szItemName[32];
            GetItemName((int)pRequestInfo->m_pItem->Type, pRequestInfo->m_pItem->Level, szItemName);
            ::mu_swprintf(aDest[nLine].m_szText, L"Item: %ls x %lu/%lu", szItemName,
                          MIN((DWORD)pRequestInfo->m_wCurValue, pRequestInfo->m_dwValue),
                          pRequestInfo->m_dwValue);
            break;

        case QUEST_REQUEST_LEVEL:
            if ((DWORD)pRequestInfo->m_wCurValue < pRequestInfo->m_dwValue)
            {
                aDest[nLine].m_dwColor = ARGB(255, 255, 30, 30);
                bRequestComplete = false;
            }
            else
                aDest[nLine].m_dwColor = ARGB(255, 223, 191, 103);

            ::mu_swprintf(aDest[nLine].m_szText, L"Level: %lu %ls", pRequestInfo->m_dwValue,
                          I18N::Game::Minimum);
            break;
#endif // ASG_ADD_TIME_LIMIT_QUEST

        case QUEST_REQUEST_TUTORIAL:
#ifdef ASG_ADD_TIME_LIMIT_QUEST
            if (pRequestInfo->m_dwCurValue == 1)
#else  // ASG_ADD_TIME_LIMIT_QUEST
            if (pRequestInfo->m_wCurValue == 1)
#endif // ASG_ADD_TIME_LIMIT_QUEST
                aDest[nLine].m_dwColor = ARGB(255, 223, 191, 103);
            else
            {
                aDest[nLine].m_dwColor = ARGB(255, 255, 30, 30);
                bRequestComplete = false;
            }

            switch (dwQuestIndex)
            {
            case 0x10009:
                ::mu_swprintf(aDest[nLine].m_szText, L"%ls", I18N::Game::OpenCharacterStatsCWindow);
                break;
            case 0x1000F:
                ::mu_swprintf(aDest[nLine].m_szText, L"%ls", I18N::Game::OpenInventoryIVWindow);
                break;
            }
            break;

        case QUEST_REQUEST_BUFF: {
#ifdef ASG_ADD_TIME_LIMIT_QUEST
            if (pRequestInfo->m_dwCurValue == 0)
#else  // ASG_ADD_TIME_LIMIT_QUEST
            if (pRequestInfo->m_wCurValue == 0)
#endif // ASG_ADD_TIME_LIMIT_QUEST
            {
                aDest[nLine].m_dwColor = ARGB(255, 255, 30, 30);
                bRequestComplete = false;
            }
            else
                aDest[nLine].m_dwColor = ARGB(255, 223, 191, 103);

            const BuffInfo buffinfo = g_BuffInfo((eBuffState)pRequestInfo->m_wIndex);
            ::mu_swprintf(aDest[nLine].m_szText, L"Bonus: %ls", buffinfo.s_BuffName);
        }
        break;

        case QUEST_REQUEST_EVENT_MAP_MON_KILL:
        case QUEST_REQUEST_EVENT_MAP_BLOOD_GATE:
        case QUEST_REQUEST_EVENT_MAP_USER_KILL:
        case QUEST_REQUEST_EVENT_MAP_DEVIL_POINT: {
#ifdef ASG_ADD_TIME_LIMIT_QUEST
            if (pRequestInfo->m_dwCurValue < pRequestInfo->m_dwValue)
#else  // ASG_ADD_TIME_LIMIT_QUEST
            if ((DWORD)pRequestInfo->m_wCurValue < pRequestInfo->m_dwValue)
#endif // ASG_ADD_TIME_LIMIT_QUEST
            {
                aDest[nLine].m_dwColor = ARGB(255, 255, 30, 30);
                bRequestComplete = false;
            }
            else
                aDest[nLine].m_dwColor = ARGB(255, 223, 191, 103);

            int nTextIndex = 0;
            switch (pRequestInfo->m_dwType)
            {
            case QUEST_REQUEST_EVENT_MAP_MON_KILL:
                nTextIndex = 3074;
                break;
            case QUEST_REQUEST_EVENT_MAP_BLOOD_GATE:
                nTextIndex = 3077;
                break;
            case QUEST_REQUEST_EVENT_MAP_USER_KILL:
                nTextIndex = 3075;
                break;
            case QUEST_REQUEST_EVENT_MAP_DEVIL_POINT:
                nTextIndex = 3079;
                break;
            }
#ifdef ASG_ADD_TIME_LIMIT_QUEST
            DWORD curValue = MIN(pRequestInfo->m_dwCurValue, pRequestInfo->m_dwValue);
#else  // ASG_ADD_TIME_LIMIT_QUEST
            DWORD curValue = MIN((DWORD)pRequestInfo->m_wCurValue, pRequestInfo->m_dwValue);
#endif // ASG_ADD_TIME_LIMIT_QUEST
            mu_swprintf(aDest[nLine].m_szText, I18N::Game::Lookup(nTextIndex),
                        pRequestInfo->m_wIndex, curValue, pRequestInfo->m_dwValue);
        }
        break;

        case QUEST_REQUEST_EVENT_MAP_CLEAR_BLOOD:
        case QUEST_REQUEST_EVENT_MAP_CLEAR_CHAOS:
        case QUEST_REQUEST_EVENT_MAP_CLEAR_DEVIL:
        case QUEST_REQUEST_EVENT_MAP_CLEAR_ILLUSION: {
#ifdef ASG_ADD_TIME_LIMIT_QUEST
            if (pRequestInfo->m_dwCurValue == 0)
#else  // ASG_ADD_TIME_LIMIT_QUEST
            if (pRequestInfo->m_wCurValue == 0)
#endif // ASG_ADD_TIME_LIMIT_QUEST
            {
                aDest[nLine].m_dwColor = ARGB(255, 255, 30, 30);
                bRequestComplete = false;
            }
            else
                aDest[nLine].m_dwColor = ARGB(255, 223, 191, 103);

            int nTextIndex = 0;
            switch (pRequestInfo->m_dwType)
            {
            case QUEST_REQUEST_EVENT_MAP_CLEAR_BLOOD:
                nTextIndex = 3078;
                break;
            case QUEST_REQUEST_EVENT_MAP_CLEAR_CHAOS:
                nTextIndex = 3076;
                break;
            case QUEST_REQUEST_EVENT_MAP_CLEAR_DEVIL:
                nTextIndex = 3080;
                break;
            case QUEST_REQUEST_EVENT_MAP_CLEAR_ILLUSION:
                nTextIndex = 3081;
                break;
            }
            mu_swprintf(aDest[nLine].m_szText, I18N::Game::Lookup(nTextIndex),
                        pRequestInfo->m_wIndex);
        }
        break;
        }
        aDest[nLine].m_szText[QM_MAX_REQUEST_REWARD_TEXT_LEN - 1] = 0;
    }

    BYTE byRewardCount;
    SQuestReward *pRewardInfo;
    i = 0;
    int j;
    for (j = 0; j < 2; ++j)
    {
        if (0 == j && pRequestReward->m_byGeneralRewardCount)
            ::wcscpy(aDest[nLine].m_szText, I18N::Game::Reward);
        else if (1 == j && pRequestReward->m_byRandRewardCount)
            mu_swprintf(aDest[nLine].m_szText, I18N::Game::RandomRewardLuDifferentKinds,
                        pRequestReward->m_byRandGiveCount);
        else
            continue;
        aDest[nLine].m_fontRole = LegacyFontRole::Bold;
        aDest[nLine++].m_dwColor = ARGB(255, 179, 230, 77);

        byRewardCount =
            0 == j ? pRequestReward->m_byGeneralRewardCount
                   : pRequestReward->m_byGeneralRewardCount + pRequestReward->m_byRandRewardCount;
        for (; i < byRewardCount; ++i, ++nLine)
        {
            pRewardInfo = &pRequestReward->m_aReward[i];

            aDest[nLine].m_fontRole = LegacyFontRole::Normal;
            aDest[nLine].m_dwColor = 0 == j ? ARGB(255, 223, 191, 103) : ARGB(255, 103, 103, 223);
            aDest[nLine].m_eRequestReward = RRC_REWARD;
            aDest[nLine].m_dwType = pRewardInfo->m_dwType;
            aDest[nLine].m_wIndex = pRewardInfo->m_wIndex;
            aDest[nLine].m_pItem = pRewardInfo->m_pItem;

            switch (pRewardInfo->m_dwType)
            {
            case QUEST_REWARD_NONE:
                ::wcscpy(aDest[nLine].m_szText, I18N::Game::None);
                break;

            case QUEST_REWARD_EXP:
                ::mu_swprintf(aDest[nLine].m_szText, L"Exp.: %lu", pRewardInfo->m_dwValue);
                break;

            case QUEST_REWARD_ZEN:
                ::mu_swprintf(aDest[nLine].m_szText, L"Zen: %lu", pRewardInfo->m_dwValue);
                break;

            case QUEST_REWARD_ITEM:
                wchar_t szItemName[32];
                GetItemName((int)pRewardInfo->m_pItem->Type, pRewardInfo->m_pItem->Level,
                            szItemName);
                ::mu_swprintf(aDest[nLine].m_szText, L"Item: %ls x %lu", szItemName,
                              pRewardInfo->m_dwValue);
                break;

            case QUEST_REWARD_BUFF: {
                const BuffInfo buffinfo = g_BuffInfo((eBuffState)pRewardInfo->m_wIndex);
                ::mu_swprintf(aDest[nLine].m_szText, L"Bonus: %ls x %lu%ls", buffinfo.s_BuffName,
                              pRewardInfo->m_dwValue, I18N::Game::Minute);
            }
            break;

#ifdef ASG_ADD_GENS_SYSTEM
            case QUEST_REWARD_CONTRIBUTE:
                mu_swprintf(aDest[nLine].m_szText, I18N::Game::ContributionLu,
                            pRewardInfo->m_dwValue);
                break;
#endif // ASG_ADD_GENS_SYSTEM
            }
            aDest[nLine].m_szText[QM_MAX_REQUEST_REWARD_TEXT_LEN - 1] = 0;
        }
    }
    return bRequestComplete;
}

void CQuestMng::SetEPRequestRewardState(DWORD dwQuestIndex, bool ProgressState)
{
    m_mapEPRequestRewardState[HIWORD(dwQuestIndex)] = ProgressState;
}

bool CQuestMng::IsEPRequestRewardState(DWORD dwQuestIndex)
{
    WORD wEP = HIWORD(dwQuestIndex);

    std::map<WORD, bool>::const_iterator iter = m_mapEPRequestRewardState.find(wEP);
    if (iter == m_mapEPRequestRewardState.end())
        return false;

    return m_mapEPRequestRewardState[wEP];
}

bool CQuestMng::IsQuestByEtc(DWORD dwQuestIndex)
{
    const QuestProgressMap &progress = QuestScripts().questProgress;
    QuestProgressMap::const_iterator iter = progress.find(dwQuestIndex);
    _ASSERT(iter != progress.end());

    if (iter->second.m_byUIType == 1)
        return true;
    else
        return false;
}

void CQuestMng::SetQuestIndexByEtcList(DWORD *adwSrcQuestIndex, int nIndexCount)
{
    m_listQuestIndexByEtc.clear();

    if (NULL == adwSrcQuestIndex)
        return;

    int i;
    for (i = 0; i < nIndexCount; ++i)
        m_listQuestIndexByEtc.push_back(adwSrcQuestIndex[i]);
}

bool CQuestMng::IsQuestIndexByEtcListEmpty()
{
    return m_listQuestIndexByEtc.empty();
}

bool CQuestMng::GetQuestIndexByEtcSelection(std::uint16_t &questNumber, std::uint16_t &questGroup)
{
    if (IsQuestIndexByEtcListEmpty())
        return false;

    auto iter = m_listQuestIndexByEtc.begin();
    questNumber = static_cast<std::uint16_t>(LOWORD(*iter));
    questGroup = static_cast<std::uint16_t>(HIWORD(*iter));
    return true;
}

void CQuestMng::DelQuestIndexByEtcList(DWORD dwQuestIndex)
{
    if (0x0000 == LOWORD(dwQuestIndex))
        return;

    DWordList::iterator iter;
    for (iter = m_listQuestIndexByEtc.begin(); iter != m_listQuestIndexByEtc.end();
         advance(iter, 1))
    {
        if (HIWORD(*iter) == HIWORD(dwQuestIndex))
        {
            m_listQuestIndexByEtc.erase(iter);
            break;
        }
    }
}

void CQuestMng::SetCurQuestIndexList(DWORD *adwCurQuestIndex, int nIndexCount)
{
    m_listCurQuestIndex.clear();

    int i;
    for (i = 0; i < nIndexCount; ++i)
        if (GetSubject(adwCurQuestIndex[i]) != NULL)
            m_listCurQuestIndex.push_back(adwCurQuestIndex[i]);

    g_pMyQuestInfoWindow->SetCurQuestList(&m_listCurQuestIndex);
}

void CQuestMng::AddCurQuestIndexList(DWORD dwQuestIndex)
{
    WORD wEP = HIWORD(dwQuestIndex);
    WORD wQS = LOWORD(dwQuestIndex);
    bool bNotFound = true;

    DWordList::iterator iter;
    for (iter = m_listCurQuestIndex.begin(); iter != m_listCurQuestIndex.end(); advance(iter, 1))
    {
        if (wEP == HIWORD(*iter))
        {
            if (wQS != 0x0000 && wQS != 0x00ff)
                m_listCurQuestIndex.insert(iter, dwQuestIndex);

            m_listCurQuestIndex.erase(iter);

            bNotFound = false;
            break;
        }
    }

    if (bNotFound)
    {
        if (wQS != 0x0000 && wQS != 0x00ff)
            m_listCurQuestIndex.push_back(dwQuestIndex);
    }

    m_listCurQuestIndex.sort();

    g_pMyQuestInfoWindow->SetCurQuestList(&m_listCurQuestIndex);
}

void CQuestMng::RemoveCurQuestIndexList(DWORD dwQuestIndex)
{
    WORD wEP = HIWORD(dwQuestIndex);

    DWordList::iterator iter;
    for (iter = m_listCurQuestIndex.begin(); iter != m_listCurQuestIndex.end(); advance(iter, 1))
    {
        if (wEP == HIWORD(*iter))
        {
            m_listCurQuestIndex.erase(iter);
            break;
        }
    }

    g_pMyQuestInfoWindow->SetCurQuestList(&m_listCurQuestIndex);
}

bool CQuestMng::IsIndexInCurQuestIndexList(DWORD dwQuestIndex)
{
    DWordList::iterator iter;
    for (iter = m_listCurQuestIndex.begin(); iter != m_listCurQuestIndex.end(); advance(iter, 1))
    {
        if (*iter == dwQuestIndex)
            return true;
    }

    return false;
}

SEASON3A::CGM3rdChangeUp::CGM3rdChangeUp(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      m_nDarkElfAppearance(false), gMapManager(keeper.MapManagerObject()),
      boneManager_(keeper.BoneManagerObject()), g_Camera(keeper.CameraStateObject())
{
}

SEASON3A::CGM3rdChangeUp::~CGM3rdChangeUp()
{
}

std::unique_ptr<SEASON3A::CGM3rdChangeUp> SEASON3A::CGM3rdChangeUp::Make(SessionKeeper &keeper)
{
    return std::unique_ptr<CGM3rdChangeUp>(new CGM3rdChangeUp(keeper));
}

bool SEASON3A::CGM3rdChangeUp::IsBalgasBarrackMap()
{
    return WD_41CHANGEUP3RD_1ST == gMapManager.ContextMap() ? true : false;
}

bool SEASON3A::CGM3rdChangeUp::IsBalgasRefugeMap()
{
    return WD_42CHANGEUP3RD_2ND == gMapManager.ContextMap() ? true : false;
}

bool SEASON3A::CGM3rdChangeUp::MoveObject(OBJECT *pObject)
{
    if (!(IsBalgasBarrackMap() || IsBalgasRefugeMap()))
        return false;

    PlayEffectSound(pObject);

    vec3_t Light;
    float Luminosity;

    switch (pObject->Type)
    {
    case 2:
    case 5:
    case 58:
    case 59:
    case 60:
        pObject->HiddenMesh = -2;
        break;
    case 3:
        Luminosity = (float)(WorldRandom() % 4 + 3) * 0.1f;
        Vector(Luminosity, Luminosity * 0.6f, Luminosity * 0.2f, Light);
        AddTerrainLight(pObject->Position[0], pObject->Position[1], Light, 3, PrimaryTerrainLight);
        pObject->HiddenMesh = -2;
        break;
    case 57:
        pObject->BlendMeshTexCoordV = -(int)WorldTime % 10000 * 0.0002f;
        break;
    case 84: {
        pObject->Position[2] = RequestTerrainHeight(pObject->Position[0], pObject->Position[1]) +
                               sinf(WorldTime * 0.0005f) * 150.f - 100.f;
    }
    break;
    case 78:
        pObject->Alpha = 0.5f;
        break;
    case 85:
    case 86:
    case 87:
    case 88:
    case 89:
    case 90:
    case 91:
    case 92:
    case 93:
        pObject->HiddenMesh = -2;
        break;
    }

    return true;
}

void SEASON3A::CGM3rdChangeUp::PlayEffectSound(OBJECT *o)
{
    switch (o->Type)
    {
    case 74:
    case 75:
        PlayBuffer(static_cast<ESound>(SOUND_3RD_CHANGE_UP_BG_CAGE1 + WorldRandom() % 2));
        break;
    case 79:
        PlayBuffer(SOUND_3RD_CHANGE_UP_BG_VOLCANO);
        break;
    case 92: {
        float fSin = (sinf(WorldTime * 0.0005f) + 1.f) * 0.5f;
        if (fSin > 0.9f)
            PlayBuffer(SOUND_3RD_CHANGE_UP_BG_FIREPILLAR);
    }
    break;
    }
}

CHARACTER *SEASON3A::CGM3rdChangeUp::CreateBalgasBarrackMonster(int iType, int PosX, int PosY,
                                                                int Key)
{
    if (!(IsBalgasBarrackMap() || IsBalgasRefugeMap() || gMapManager.InDevilSquare()))
        return NULL;

    CHARACTER *c = NULL;

    switch (iType)
    {
    case MONSTER_BALRAM_TRAINEE:
    case MONSTER_BALRAM_TRAINEE_SOLDIER: {
        OpenMonsterModel(MONSTER_MODEL_BALRAM);
        c = CreateCharacter(Key, MODEL_BALRAM, PosX, PosY);
        c->Object.Scale = 1.25f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_DEATH_SPIRIT_TRAINEE_SOLDIER: {
        OpenMonsterModel(MONSTER_MODEL_DEATH_SPIRIT);
        c = CreateCharacter(Key, MODEL_DEATH_SPIRIT, PosX, PosY);
        c->Object.Scale = 1.25f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;

        boneManager_.RegisterBone(c, L"Monster94_zx", CharacterSocket::Monster94_zx);
        boneManager_.RegisterBone(c, L"Monster94_zx01", CharacterSocket::Monster94_zx01);
    }
    break;
    case MONSTER_SORAM_TRAINEE:
    case MONSTER_SORAM_TRAINEE_SOLDIER: {
        OpenMonsterModel(MONSTER_MODEL_SORAM);
        c = CreateCharacter(Key, MODEL_SORAM, PosX, PosY);
        c->Object.Scale = 1.3f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_DARK_ELF_TRAINEE_SOLDIER: {
        m_nDarkElfAppearance = true;

        OpenMonsterModel(MONSTER_MODEL_DARK_ELF);
        c = CreateCharacter(Key, MODEL_DARK_ELF, PosX, PosY);
        //			c->Object.Scale = 1.5f;
        c->Object.Scale = 1.7f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        boneManager_.RegisterBone(c, L"Left_Hand", CharacterSocket::Left_Hand);
    }
    break;
    }
    return c;
}

bool SEASON3A::CGM3rdChangeUp::SetCurrentActionBalgasBarrackMonster(CHARACTER *c, OBJECT *o)
{
    if (!(IsBalgasBarrackMap() || IsBalgasRefugeMap()))
        return false;

    switch (c->MonsterIndex)
    {
    case MONSTER_BALRAM_TRAINEE_SOLDIER:
    case MONSTER_DEATH_SPIRIT_TRAINEE_SOLDIER:
    case MONSTER_SORAM_TRAINEE_SOLDIER:
    case MONSTER_DARK_ELF_TRAINEE_SOLDIER:
        return CheckMonsterSkill(c, o);
    }
    return false;
}

bool SEASON3A::CGM3rdChangeUp::AttackEffectBalgasBarrackMonster(CHARACTER *c, OBJECT *o, BMD *b)
{
    if (!(IsBalgasBarrackMap() || IsBalgasRefugeMap()))
        return false;

    switch (o->Type)
    {
    case MODEL_BALRAM:
        if (c->CheckAttackTime(14))
        {
            CreateEffect(MODEL_ARROW_HOLY, o->Position, o->Angle, o->Light, 1, o, o->PKKey);
            c->SetLastAttackEffectTime();
            return true;
        }
        break;
    }

    return false;
}

bool SEASON3A::CGM3rdChangeUp::SetCurrentActionMonster(CHARACTER *character, OBJECT *object)
{
    return SetCurrentActionBalgasBarrackMonster(character, object);
}

bool SEASON3A::CGM3rdChangeUp::AttackEffectMonster(CHARACTER *character, OBJECT *object, BMD *model)
{
    return AttackEffectBalgasBarrackMonster(character, object, model);
}

void SEASON3A::CGM3rdChangeUp::InstallBehavior()
{
    if (IsBalgasBarrackMap())
    {
        LoadWaveFile(SOUND_3RD_CHANGE_UP_BG_CAGE1, L"Data\\Sound\\w42\\cage01.wav", 1);
        LoadWaveFile(SOUND_3RD_CHANGE_UP_BG_CAGE2, L"Data\\Sound\\w42\\cage02.wav", 1);
        LoadWaveFile(SOUND_3RD_CHANGE_UP_BG_VOLCANO, L"Data\\Sound\\w42\\volcano.wav", 1);
        LoadWaveFile(SOUND_3RD_CHANGE_UP_BG_FIREPILLAR, L"Data\\Sound\\w42\\firepillar.wav", 1);
        LoadWaveFile(SOUND_CRY1ST_BALRAM_MOVE1, L"Data\\Sound\\w35\\balram_idle1.wav", 1);
        LoadWaveFile(SOUND_CRY1ST_BALRAM_MOVE2, L"Data\\Sound\\w35\\balram_idle2.wav", 1);
        LoadWaveFile(SOUND_CRY1ST_BALRAM_ATTACK1, L"Data\\Sound\\w35\\balram_attack1.wav", 1);
        LoadWaveFile(SOUND_CRY1ST_BALRAM_ATTACK2, L"Data\\Sound\\w35\\balram_attack2.wav", 1);
        LoadWaveFile(SOUND_CRY1ST_BALRAM_DIE, L"Data\\Sound\\w35\\balram_death.wav", 1);
        LoadWaveFile(SOUND_CRY1ST_DEATHSPIRIT_MOVE1, L"Data\\Sound\\w35\\dths_idle1.wav", 1);
        LoadWaveFile(SOUND_CRY1ST_DEATHSPIRIT_MOVE2, L"Data\\Sound\\w35\\dths_idle2.wav", 1);
        LoadWaveFile(SOUND_CRY1ST_DEATHSPIRIT_ATTACK1, L"Data\\Sound\\w35\\dths_at1.wav", 1);
        LoadWaveFile(SOUND_CRY1ST_DEATHSPIRIT_ATTACK2, L"Data\\Sound\\w35\\dths_at2.wav", 1);
        LoadWaveFile(SOUND_CRY1ST_DEATHSPIRIT_DIE, L"Data\\Sound\\w35\\dths_deat.wav", 1);
        LoadWaveFile(SOUND_CRY1ST_SORAM_MOVE1, L"Data\\Sound\\w35\\soram_idle1.wav", 1);
        LoadWaveFile(SOUND_CRY1ST_SORAM_MOVE2, L"Data\\Sound\\w35\\soram_idle2.wav", 1);
        LoadWaveFile(SOUND_CRY1ST_SORAM_ATTACK1, L"Data\\Sound\\w35\\soram_attack1.wav", 1);
        LoadWaveFile(SOUND_CRY1ST_SORAM_ATTACK2, L"Data\\Sound\\w35\\soram_attack2.wav", 1);
        LoadWaveFile(SOUND_CRY1ST_SORAM_DIE, L"Data\\Sound\\w35\\soram_death.wav", 1);
    }
    else
    {
        LoadWaveFile(SOUND_3RD_CHANGE_UP_BG_CAGE1, L"Data\\Sound\\w42\\cage01.wav", 1);
        LoadWaveFile(SOUND_3RD_CHANGE_UP_BG_CAGE2, L"Data\\Sound\\w42\\cage02.wav", 1);
        LoadWaveFile(SOUND_3RD_CHANGE_UP_BG_VOLCANO, L"Data\\Sound\\w42\\volcano.wav", 1);
        LoadWaveFile(SOUND_3RD_CHANGE_UP_BG_FIREPILLAR, L"Data\\Sound\\w42\\firepillar.wav", 1);
    }
}

void SEASON3A::CGM3rdChangeUp::UpdateMusic()
{

    if (IsBalgasBarrackMap())
    {
        if (IsEndMp3())
            StopMp3(MUSIC_BALGAS_BARRACK);
        PlayMp3(MUSIC_BALGAS_BARRACK);
    }
    else if (IsBalgasRefugeMap())
    {
        if (IsEndMp3())
            StopMp3(MUSIC_BALGAS_REFUGE);
        PlayMp3(MUSIC_BALGAS_REFUGE);
    }
    else
    {
        StopMp3(MUSIC_BALGAS_BARRACK);
        StopMp3(MUSIC_BALGAS_REFUGE);
    }
}

bool SEASON3A::CGM3rdChangeUp::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_BALGAS_BARRACK) == 0 ||
           std::strcmp(track, MUSIC_BALGAS_REFUGE) == 0;
}

bool SEASON3A::CGM3rdChangeUp::CreateObject(OBJECT *object)
{
    if (object->Type != 57 && object->Type != 78 && object->Type != 84)
        return false;
    object->m_bRenderAfterCharacter = true;
    return true;
}
