#pragma once
#include "data/WorldData.h"
#include "render/UiAdapter.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/QuestNpc/QuestNpcRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"
#include <algorithm>
#include <array>
#include <list>
#include <memory>
#include <tuple>

#define NUM_LINE_DA (1)
#define MAX_LENGTH_CMB (38)
//  UIGateKeeper.h
//  ��  �� : ������ �������̽�

enum
{
    TOUCH_TYPE_NONE = 0,
    TOUCH_TYPE_PERSON,
    TOUCH_TYPE_GUILD_STAFF,
    TOUCH_TYPE_GUILD_MASTER
};

class CUIGateKeeper
{
  public:
    CUIGateKeeper();
    virtual ~CUIGateKeeper();

  protected:
    bool m_bPublic;
    BYTE m_byType;
    int m_nEntranceFee;
    int m_iViewEntranceFee;
    int m_iAddEntranceFee;
    int m_iMaxEnteranceFee;

  public:
    void SetPublic(bool bPublic)
    {
        m_bPublic = bPublic;
    }
    void SetEntranceFee(int nEntranceFee)
    {
        m_nEntranceFee = nEntranceFee;
    }
    void SetInfo(BYTE byType, bool bPublic, int iEntranceFee, int iAddEntranceFee,
                 int iMaxEntranceFee)
    {
        m_byType = byType;
        m_bPublic = bPublic;
        m_nEntranceFee = iEntranceFee;
        m_iViewEntranceFee = m_nEntranceFee;
        m_iAddEntranceFee = iAddEntranceFee;
        m_iMaxEnteranceFee = iMaxEntranceFee;
    }
    BYTE GetType()
    {
        return m_byType;
    }
    BOOL IsPublic()
    {
        return m_bPublic;
    }
    int GetEnteranceFee()
    {
        return m_nEntranceFee;
    }
    int GetViewEnteranceFee()
    {
        return m_iViewEntranceFee;
    }
    void SetViewEntranceFee(int fee)
    {
        m_iViewEntranceFee = fee;
    }
    int GetAddEnteranceFee()
    {
        return m_iAddEntranceFee;
    }
    int GetMaxEnteranceFee()
    {
        return m_iMaxEnteranceFee;
    }

    void SendPublicSetting();
    void SendEnteranceFee();
    void SendEnter();
    void EnteranceFeeUp();
    void EnteranceFeeDown();
};

namespace SEASON3B
{
class CNewUIEmpireGuardianNPC : public CNewUIObj,
                                protected SessionUiLegacyBindings,
                                public INewUI3DRenderObj
{
  public:
    explicit CNewUIEmpireGuardianNPC(SessionKeeper &keeper);
    ~CNewUIEmpireGuardianNPC();
    bool Create(CNewUIManager *manager, CNewUI3DRenderMng *renderManager, int x, int y);
    void Release();
    void SetPos(int x, int y);
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    void Render3D();
    float GetLayerDepth();
    bool IsVisible() const;
    void OpenningProcess();
    void ClosingProcess();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);

  private:
    void StageContent();
    CNewUIManager *manager_ = nullptr;
    CNewUI3DRenderMng *renderManager_ = nullptr;
    SessionRenderUnit &renderer_;
    UI::Modern::PC::Inventory::RmlItemDialogPanel panel_;
    std::string locale_;
    bool visible_ = false;
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIEmpireGuardianTimer : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum EG_DAY_MAP_LIST
    {
        EG_MONDAY,
        EG_TUESDAY,
        EG_WEDNESDAY,
        EG_THURSDAY,
        EG_FRIDAY,
        EG_SATURDAY,
        EG_SUNDAY,
    };

    enum IMAGE_LIST
    {
        IMAGE_EMPIREGUARDIAN_TIMER_WINDOW = BITMAP_EMPIREGUARDIAN_TIMER_BEGIN,
    };

  private:
    enum EMPIREGUARDIAN_TIME_WINDOW_SIZE
    {
        TIMER_WINDOW_WIDTH = 124,
        TIMER_WINDOW_HEIGHT = 81,
    };

    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;

  public:
    explicit CNewUIEmpireGuardianTimer(SessionKeeper &keeper);
    virtual ~CNewUIEmpireGuardianTimer();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    bool BtnProcess();

    float GetLayerDepth(); //. 1.2f

    void OpenningProcess();
    void ClosingProcess();

    void SetType(int iType)
    {
        m_iType = iType;
    }
    void SetRemainTime(DWORD tick)
    {
        m_dTime = tick;
    }
    void SetDay(int iDay)
    {
        m_iDay = iDay;
    }
    void SetZone(int iZone)
    {
        m_iZone = iZone;
    }
    void SetMonsterCount(int iMonsterCount)
    {
        m_iMonsterCount = iMonsterCount;
    }

    const int GetDay()
    {
        return m_iDay;
    }
    const int GetZone()
    {
        return m_iZone;
    }

  private:
    void LoadImages();
    void UnloadImages();

    int m_iType;
    DWORD m_dTime;
    int m_iDay;
    int m_iZone;
    int m_iMonsterCount;
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIGatemanWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIGatemanWindow(SessionKeeper &keeper);
    ~CNewUIGatemanWindow();
    bool Create(CNewUIManager *manager, int x, int y);
    void Release();
    void SetPos(int x, int y);
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    void OpeningProcess();
    void ClosingProcess();
    float GetLayerDepth();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);

  private:
    void StageContent();
    void StageDescriptions(int type, bool publicAccess);
    void Enter();
    CNewUIManager *manager_ = nullptr;
    SessionRenderUnit &renderer_;
    UI::Modern::PC::NPCs::RmlGatekeeperPanel panel_;
    UI::Modern::PC::NPCs::RmlGatekeeperPanel::Content content_;
    std::array<int, 6> state_{};
    std::string locale_;
    bool visible_ = false;
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUINPCDialogue : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUINPCDialogue(SessionKeeper &keeper);
    ~CNewUINPCDialogue();
    bool Create(CNewUIManager *manager, int x, int y);
    void Release();
    void SetPos(int x, int y);
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    bool IsVisible() const;
    float GetLayerDepth();
    void ProcessOpening();
    bool ProcessClosing();
    void SetContents(DWORD index);
    void SetContributePoint(DWORD point);
    void ProcessQuestListReceive(DWORD *quests, int count);
    void ProcessGensJoiningReceive(BYTE result, BYTE influence);
    void ProcessGensSecessionReceive(BYTE result);
    void ProcessGensRewardReceive(BYTE result);

  private:
    void ProcessSelTextResult();
    void StageContribution();
    CNewUIManager *m_pNewUIMng = nullptr;
    SessionRenderUnit &renderer_;
    UI::Modern::PC::NPCs::RmlNpcDialoguePanel panel_;
    UI::Modern::PC::NPCs::RmlNpcDialoguePanel::Content content_;
    static constexpr std::size_t MaximumQuestChoices = 20;
    std::array<DWORD, MaximumQuestChoices> m_adwQuestIndex{};
    DWORD m_dwCurDlgIndex = 0, m_dwContributePoint = 0;
    int m_nSelTextCount = 0, m_nSelSelText = 0;
    bool m_bQuestListMode = false, visible_ = false;
};
} // namespace SEASON3B

class CmuConsoleDebug;

class SessionRenderUnit;

namespace SEASON3B
{
class CNewUINPCShop : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_NPCSHOP_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, // Reference
        IMAGE_NPCSHOP_TOP = CNewUIMyInventory::IMAGE_INVENTORY_BACK_TOP2,
        IMAGE_NPCSHOP_LEFT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_LEFT,
        IMAGE_NPCSHOP_RIGHT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_RIGHT,
        IMAGE_NPCSHOP_BOTTOM = CNewUIMyInventory::IMAGE_INVENTORY_BACK_BOTTOM,
        IMAGE_NPCSHOP_BTN_REPAIR = CNewUIMyInventory::IMAGE_INVENTORY_REPAIR_BTN,
        IMAGE_NPCSHOP_REPAIR_MONEY = BITMAP_INTERFACE_NEW_NPCSHOP_BEGIN,
    };

    enum
    {
        NPCSHOP_POS_X = 260,
        NPCSHOP_POS_Y = 0,
        SHOP_STATE_BUYNSELL = 1,
        SHOP_STATE_REPAIR = 2,
    };

  private:
    enum
    {
        NPCSHOP_WIDTH = 190,
        NPCSHOP_HEIGHT = 429,
    };

    CNewUIManager *m_pNewUIMng;
    CNewUIInventoryCtrl *m_pNewInventoryCtrl;
    POINT m_Pos;

    DWORD m_dwShopState;
    int m_iTaxRate;
    bool m_bRepairShop;
    bool m_bIsNPCShopOpen;

    UI::Modern::PC::Inventory::RmlItemPanel m_ModernPanel;

    DWORD m_dwStandbyItemKey;

    bool m_bSellingItem;

  public:
    explicit CNewUINPCShop(SessionKeeper &keeper);
    virtual ~CNewUINPCShop();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);
    std::optional<bool> ProcessModernUiInput(const SessionInputEvent &event);

    float GetLayerDepth(); //. 2.5f

    void SetTaxRate(int iTaxRate);
    int GetTaxRate();

    bool InsertItem(int iIndex, std::span<const BYTE> pbyItemPacket);

    void OpenningProcess();
    void DeleteAllItems();

    void ClosingProcess();
    void SetRepairShop(bool bRepair);
    bool IsRepairShop();
    void ToggleState();
    DWORD GetShopState();

    int GetPointedItemIndex();

    //. Exporting Functions
    void SetStandbyItemKey(DWORD dwItemKey);
    DWORD GetStandbyItemKey() const;
    int GetStandbyItemIndex();
    ITEM *GetStandbyItem();

    void SetSellingItem(bool bFlag);
    bool IsSellingItem();

  private:
    SessionRenderUnit &renderUnit;
    void Init();
    void SyncModernGeometry();
    void StageModernContent();
    bool IsMouseInModernPanel() const;

    bool InventoryProcess();
    bool BtnProcess();
};
} // namespace SEASON3B

using DWordList = std::list<DWORD>;
namespace SEASON3B
{
class CNewUIMyQuestInfoWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    // Reserved shared legacy texture identifier; this owner no longer loads it.
    enum
    {
        IMAGE_MYQUEST_LINE = BITMAP_INTERFACE_MYQUEST_WINDOW_BEGIN
    };
    explicit CNewUIMyQuestInfoWindow(SessionKeeper &keeper);
    ~CNewUIMyQuestInfoWindow();
    bool Create(CNewUIManager *manager, int x, int y);
    void Release();
    void SetPos(int x, int y);
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    float GetLayerDepth();
    void OpenningProcess();
    void ClosingProcess();
    void UnselectQuestList();
    void SetCurQuestList(DWordList *quests);
    void SetSelQuestSummary();
    void SetSelQuestRequestReward();
    void QuestOpenBtnEnable(bool enable);
    void QuestGiveUpBtnEnable(bool enable);
    DWORD GetSelQuestIndex();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);

  private:
    using Panel = UI::Modern::PC::Quests::RmlQuestJournalPanel;
    struct StageKey
    {
        std::uint64_t listRevision, rewardRevision;
        DWORD selected;
        Panel::Tab tab;
        bool canStart, canGiveUp, showRewards;
        std::array<int, 4> nativeState;
        std::string locale;
        bool operator==(const StageKey &) const = default;
    };
    void StageContent();
    void StageSelection(Panel::Content &content);
    void StageEvents(Panel::Content &content);
    void ProcessChanges(const Panel::Changes &changes);
    void Select(std::size_t index);
    void StageTooltip();
    CNewUIManager *manager_ = nullptr;
    SessionRenderUnit &renderer_;
    Panel panel_;
    Panel::Content content_;
    Panel::Tab tab_ = Panel::Tab::Quests;
    std::vector<DWORD> quests_;
    std::optional<std::size_t> selected_;
    std::array<ITEM *, QuestRewardPresentation::MaximumLines> rewardItems_{};
    std::uint64_t revision_ = 0;
    std::uint64_t listRevision_ = 0;
    std::optional<StageKey> staged_;
    bool visible_ = false, canStart_ = false, canGiveUp_ = false, showRewards_ = false;
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUINPCQuest : public CNewUIObj, public INewUI3DRenderObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUINPCQuest(SessionKeeper &keeper);
    ~CNewUINPCQuest();
    bool Create(CNewUIManager *manager, CNewUI3DRenderMng *models, int x, int y);
    void Release();
    void SetPos(int x, int y);
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    void Render3D();
    bool IsOwnerRendered() const override
    {
        return true;
    }
    bool IsVisible() const;
    float GetLayerDepth();
    void ProcessOpening();
    bool ProcessClosing();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);

  private:
    using Panel = UI::Modern::PC::Quests::RmlJobChangePanel;
    struct Item
    {
        int type, level;
    };
    void StageContent();
    void StageRequirements(Panel::Content &content);
    void Choose(std::size_t choice);
    void SendProgress();
    CNewUIManager *manager_ = nullptr;
    CNewUI3DRenderMng *models_ = nullptr;
    SessionRenderUnit &renderer_;
    Panel panel_;
    Panel::Content content_;
    std::vector<Item> items_;
    std::tuple<int, int, int> branch_{-1, -1, -1};
    std::uint64_t revision_ = 0;
    bool visible_ = false, pending_ = false;
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIQuestProgress : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIQuestProgress(SessionKeeper &keeper);
    ~CNewUIQuestProgress();
    bool Create(CNewUIManager *manager, int x, int y);
    void Release();
    void SetPos(int x, int y);
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool IsVisible() const;
    float GetLayerDepth();
    void ProcessOpening();
    bool ProcessClosing();
    void SetContents(DWORD index);
    void EnableCompleteBtn(bool enable);
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);

  protected:
    CNewUIQuestProgress(SessionKeeper &keeper, bool byItem);

  private:
    using Panel = UI::Modern::PC::Quests::RmlQuestProgressPanel;
    void StageContent();
    void StageRequirements(Panel::Content &content);
    void ProcessChanges(const Panel::Changes &changes);
    void StageTooltip();
    int InterfaceId() const;
    CNewUIManager *manager_ = nullptr;
    SessionRenderUnit &renderer_;
    Panel panel_;
    Panel::Content content_;
    static constexpr std::size_t MaximumRewardLines = 13;
    std::array<ITEM *, MaximumRewardLines> rewardItems_{};
    DWORD quest_ = 0;
    std::uint64_t revision_ = 0, branch_ = 0;
    std::optional<std::tuple<DWORD, std::uint64_t, std::string, bool, bool>> staged_;
    bool byItem_ = false, visible_ = false, pending_ = false, completionEnabled_ = true;
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIQuestProgressByEtc final : public CNewUIQuestProgress
{
  public:
    explicit CNewUIQuestProgressByEtc(SessionKeeper &keeper);
};
} // namespace SEASON3B

namespace GatekeeperDetail
{
using namespace SEASON3B;
std::wstring FeeText(int fee, const wchar_t *format);
} // namespace GatekeeperDetail

namespace QuestInfoDetail
{
using namespace SEASON3B;
const wchar_t *QuestText(const wchar_t *text);
} // namespace QuestInfoDetail

namespace QuestProgressDetail
{
using namespace SEASON3B;
const wchar_t *QuestText(const wchar_t *text);
} // namespace QuestProgressDetail

namespace NpcDialogueDetail
{
using namespace SEASON3B;
#pragma pack(push)
#pragma pack()
enum GENS_JOINING_ERR_CODE
{
    GJEC_NONE_ERR = 0,
    GJEC_REG_GENS_ERR,
    GJEC_GENS_SECEDE_DAY_ERR,
    GJEC_REG_GENS_LV_ERR,
    GJEC_REG_GENS_NOT_EQL_GUILDMA_ERR,
    GJEC_NONE_REG_GENS_GUILDMA_ERR,
    GJEC_PARTY,
    GJEC_GUILD_UNION_MASTER
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
enum GENS_SECEDE_ERR_CODE
{
    GSEC_NONE_ERR = 0,
    GSEC_IS_NOT_REG_GENS,
    GSEC_GUILD_MASTER_CAN_NOT_SECEDE,
    GSEC_IS_NOT_INFLUENCE_NPC
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
enum GENS_REWARD_ERR_CODE
{
    GENS_REWARD_CALL = 0,
    GENS_REWARD_TERM,
    GENS_REWARD_TARGET,
    GENS_REWARD_SPACE,
    GENS_REWARD_ALREADY,
    GENS_REWARD_DIFFERENT,
    GENS_REWARD_NOT_REG,
};
#pragma pack(pop)

} // namespace NpcDialogueDetail
