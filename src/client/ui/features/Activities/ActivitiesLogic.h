#pragma once
#include "data/WorldData.h"
#include "domain/Events.h"
#include "render/UiAdapter.h"
#include "session/SessionNetwork.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include "ui/features/Activities/ActivitiesRender.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Hud/HudLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/QuestNpc/QuestNpcLogic.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"
#include <array>
#include <list>
#include <memory>

#define KANTURU2ND_STATETEXT_MAX 3
#define KANTURU2ND_REFRESH_GAPTIME 2000
#define KANTURU2ND_REFRESHBUTTON_GAPTIME 1000

class CSenatusInfo;

struct SessionSenatusInfoDeleter final
{
    void operator()(CSenatusInfo *value) const noexcept;
};

using SessionSenatusInfoPtr = std::unique_ptr<CSenatusInfo, SessionSenatusInfoDeleter>;

SessionSenatusInfoPtr CreateSessionSenatusInfo();

//  UIGuardsMan.h

class CUIGuardsMan : protected SessionLegacyCalls
{
  public:
    explicit CUIGuardsMan(SessionKeeper &keeper);
    virtual ~CUIGuardsMan();

  protected:
    enum
    {
        BC_REQ_LEVEL = 200,
        BC_REQ_MEMBERCOUNT = 20
    };
    enum REG_STATUS
    {
        REG_STATUS_NONE = 0,
        REG_STATUS_REG = 1
    };
    REG_STATUS m_eRegStatus;
    DWORD m_dwRegMark;

  public:
    bool IsSufficentDeclareLevel();
    bool HasRegistered()
    {
        return (m_eRegStatus == REG_STATUS_REG);
    }
    void SetRegStatus(BYTE byStatus)
    {
        m_eRegStatus = (REG_STATUS)byStatus;
    }

    DWORD GetRegMarkCount()
    {
        return m_dwRegMark;
    }
    void SetMarkCount(DWORD dwMarkCount)
    {
        m_dwRegMark = dwMarkCount;
    }

    DWORD GetMyMarkCount();
    int GetMyMarkSlotIndex();
};

//  UISenatus.h

class CSenatusInfo
{
  private:
    enum NPC_NUMBERS
    {
        GATENPC_NUMBER = 277,
        STATUENPC_NUMBER = 283,
    };
    enum MAX_LEVELS
    {
        GATE_MAX_HP_LEVEL = 3,
        GATE_MAX_DEFENSE_LEVEL = 3,

        STATUE_MAX_HP_LEVEL = 3,
        STATUE_MAX_DEFENSE_LEVEL = 3,
        STATUE_MAX_RECOVER_LEVEL = 3,
    };
    enum LEVEL_VALUES
    {
        GATELEVEL_HP_0 = 1900000,
        GATELEVEL_HP_1 = 2500000,
        GATELEVEL_HP_2 = 3500000,
        GATELEVEL_HP_3 = 5200000,

        GATELEVEL_DEFENSE_0 = 100,
        GATELEVEL_DEFENSE_1 = 180,
        GATELEVEL_DEFENSE_2 = 300,
        GATELEVEL_DEFENSE_3 = 520,

        STATUELEVEL_HP_0 = 1500000,
        STATUELEVEL_HP_1 = 2200000,
        STATUELEVEL_HP_2 = 3400000,
        STATUELEVEL_HP_3 = 5000000,

        STATUELEVEL_DEFENSE_0 = 80,
        STATUELEVEL_DEFENSE_1 = 180,
        STATUELEVEL_DEFENSE_2 = 340,
        STATUELEVEL_DEFENSE_3 = 550,
        STATUELEVEL_RECOVER_0 = 0,
        STATUELEVEL_RECOVER_1 = 1,
        STATUELEVEL_RECOVER_2 = 2,
        STATUELEVEL_RECOVER_3 = 3,
    };
    enum TAX_RATES
    {
        MIN_TAX_RATE = 0,
        MAX_TAX_RATE = 3,
        MAX_NORMAL_TAX_RATE = 3,
    };
    enum NPCUPGRADE_VALUES
    {
        NPCUPGRADE_DEFENSE = 1,
        NPCUPGRADE_RECOVER = 2,
        NPCUPGRADE_HP = 3,
    };

  public:
    CSenatusInfo();
    virtual ~CSenatusInfo();

    int GetRepairCost(LPPMSG_NPCDBLIST pInfo);
    int GetHP(int nType, int nLevel);
    int GetHPLevel(LPPMSG_NPCDBLIST pInfo);
    int GetNextAddHP(LPPMSG_NPCDBLIST pInfo);
    int GetDefense(int nType, int nLevel);
    int GetDefenseLevel(LPPMSG_NPCDBLIST pInfo);
    int GetNextAddDefense(LPPMSG_NPCDBLIST pInfo);
    int GetRecover(int nType, int nLevel);
    int GetRecoverLevel(LPPMSG_NPCDBLIST pInfo);
    int GetNextAddRecover(LPPMSG_NPCDBLIST pInfo);

    void DoGateRepairAction();
    void DoGateUpgradeHPAction();
    void DoGateUpgradeDefenseAction();
    void DoStatueRepairAction();
    void DoStatueUpgradeHPAction();
    void DoStatueUpgradeDefenseAction();
    void DoStatueUpgradeRecoverAction();
    void DoApplyTaxAction();
    void DoWithdrawAction(DWORD dwMoney);

    void SetNPCInfo(LPPMSG_NPCDBLIST pInfo);
    LPPMSG_NPCDBLIST GetNPCInfo(int iNpcNumber, int iNpcIndex);
    void SetTaxInfo(LPPMSG_ANS_TAXMONEYINFO pInfo);
    void ChangeTaxInfo(LPPMSG_ANS_TAXRATECHANGE pInfo);
    void ChangeCastleMoney(LPPMSG_ANS_MONEYDRAWOUT pInfo);
    void BuyNewNPC(int iNpcNumber, int iNpcIndex);

    int GetCurrGate()
    {
        return m_iCurrGate;
    }
    void SetCurrGate(int iCurrGate)
    {
        m_iCurrGate = iCurrGate;
    }
    int GetCurrStatue()
    {
        return m_iCurrStatue;
    }
    void SetCurrStatue(int iCurrStatue)
    {
        m_iCurrStatue = iCurrStatue;
    }

    PMSG_NPCDBLIST &GetGateInfo(int iIndex)
    {
        return m_GateInfo[iIndex];
    }
    PMSG_NPCDBLIST &GetStatueInfo(int iIndex)
    {
        return m_StatueInfo[iIndex];
    }
    PMSG_NPCDBLIST &GetCurrGateInfo()
    {
        return m_GateInfo[m_iCurrGate];
    }
    PMSG_NPCDBLIST &GetCurrStatueInfo()
    {
        return m_StatueInfo[m_iCurrStatue];
    }

    BOOL IsGateRepairable()
    {
        return (GetCurrGateInfo().iNpcHp < GetCurrGateInfo().iNpcMaxHp);
    }
    BOOL IsGateHPUpgradable()
    {
        return (GetHPLevel(&GetCurrGateInfo()) < GATE_MAX_HP_LEVEL);
    }
    BOOL IsGateDefeseUpgradable()
    {
        return (GetDefenseLevel(&GetCurrGateInfo()) < GATE_MAX_DEFENSE_LEVEL);
    }

    BOOL IsStatueRepairable()
    {
        return (GetCurrStatueInfo().iNpcHp < GetCurrStatueInfo().iNpcMaxHp);
    }
    BOOL IsStatueHPUpgradable()
    {
        return (GetHPLevel(&GetCurrStatueInfo()) < STATUE_MAX_HP_LEVEL);
    }
    BOOL IsStatueDefeseUpgradable()
    {
        return (GetDefenseLevel(&GetCurrStatueInfo()) < STATUE_MAX_DEFENSE_LEVEL);
    }
    BOOL IsStatueRecoverUpgradable()
    {
        return (GetRecoverLevel(&GetCurrStatueInfo()) < STATUE_MAX_RECOVER_LEVEL);
    }

    int GetChaosTaxRate()
    {
        return m_iChaosTaxRate;
    }
    void PlusChaosTaxRate(int iValue);
    int GetNormalTaxRate()
    {
        return m_iNormalTaxRate;
    }
    void PlusNormalTaxRate(int iValue);
    int GetRealTaxRateChaos()
    {
        return m_iRealTaxRateChaos;
    }
    int GetRealTaxRateStore()
    {
        return m_iRealTaxRateStore;
    }
    __int64 GetCastleMoney()
    {
        return m_i64CastleMoney;
    }
    void RollbackTaxRates();

    int GetMaxHPLevel()
    {
        return STATUE_MAX_HP_LEVEL;
    }
    int GetMaxDefenseLevel()
    {
        return STATUE_MAX_DEFENSE_LEVEL;
    }
    int GetMaxRecoverLevel()
    {
        return STATUE_MAX_RECOVER_LEVEL;
    }

    BOOL IsGate(LPPMSG_NPCDBLIST pInfo)
    {
        return (pInfo->iNpcNumber == GATENPC_NUMBER);
    }
    BOOL IsStatue(LPPMSG_NPCDBLIST pInfo)
    {
        return (pInfo->iNpcNumber == STATUENPC_NUMBER);
    }

  protected:
    int m_iCurrGate;
    int m_iCurrStatue;
    PMSG_NPCDBLIST m_GateInfo[6];
    PMSG_NPCDBLIST m_StatueInfo[4];

    int m_iChaosTaxRate;
    int m_iNormalTaxRate;
    int m_iRealTaxRateChaos;
    int m_iRealTaxRateStore;
    __int64 m_i64CastleMoney;
};

namespace SEASON3B
{
class CNewUICastleWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_CASTLEWINDOW_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, // Reference
        IMAGE_CASTLEWINDOW_TOP = CNewUIMyInventory::IMAGE_INVENTORY_BACK_TOP,
        IMAGE_CASTLEWINDOW_LEFT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_LEFT,
        IMAGE_CASTLEWINDOW_RIGHT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_RIGHT,
        IMAGE_CASTLEWINDOW_BOTTOM = CNewUIMyInventory::IMAGE_INVENTORY_BACK_BOTTOM,
        IMAGE_CASTLEWINDOW_EXIT_BTN = CNewUIMyInventory::IMAGE_INVENTORY_EXIT_BTN,
        IMAGE_CASTLEWINDOW_TAB_BTN = CNewUIGuildInfoWindow::IMAGE_GUILDINFO_TAB_BUTTON,
        IMAGE_CASTLEWINDOW_LINE = CNewUIMyQuestInfoWindow::IMAGE_MYQUEST_LINE,
        IMAGE_CASTLEWINDOW_BUTTON = CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY_VERY_SMALL,
        IMAGE_CASTLEWINDOW_TABLE_TOP_LEFT =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_TOP_LEFT, //. newui_item_table01(L).tga (14,14)
        IMAGE_CASTLEWINDOW_TABLE_TOP_RIGHT =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_TOP_RIGHT, //. newui_item_table01(R).tga (14,14)
        IMAGE_CASTLEWINDOW_TABLE_BOTTOM_LEFT =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_BOTTOM_LEFT, //. newui_item_table02(L).tga (14,14)
        IMAGE_CASTLEWINDOW_TABLE_BOTTOM_RIGHT = CNewUIInventoryCtrl::
            IMAGE_ITEM_TABLE_BOTTOM_RIGHT, //. newui_item_table02(R).tga (14,14)
        IMAGE_CASTLEWINDOW_TABLE_TOP_PIXEL =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_TOP_PIXEL, //. newui_item_table03(up).tga (1, 14)
        IMAGE_CASTLEWINDOW_TABLE_BOTTOM_PIXEL = CNewUIInventoryCtrl::
            IMAGE_ITEM_TABLE_BOTTOM_PIXEL, //. newui_item_table03(dw).tga (1,14)
        IMAGE_CASTLEWINDOW_TABLE_LEFT_PIXEL =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_LEFT_PIXEL, //. newui_item_table03(L).tga (14,1)
        IMAGE_CASTLEWINDOW_TABLE_RIGHT_PIXEL =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_RIGHT_PIXEL, //. newui_item_table03(R).tga (14,1)
        IMAGE_CASTLEWINDOW_MONEY = CNewUINPCShop::IMAGE_NPCSHOP_REPAIR_MONEY,
        IMAGE_CASTLEWINDOW_SCROLL_UP_BTN = BITMAP_INTERFACE_NEW_CASTLE_WINDOW_BEGIN,
        IMAGE_CASTLEWINDOW_SCROLL_DOWN_BTN,
    };
    enum CASTLE_MSGBOX_REQUEST
    {
        CASTLE_MSGREQ_NULL,
        CASTLE_MSGREQ_BUY_GATE,
        CASTLE_MSGREQ_REPAIR_GATE,
        CASTLE_MSGREQ_UPGRADE_GATE_HP,
        CASTLE_MSGREQ_UPGRADE_GATE_DEFENSE,
        CASTLE_MSGREQ_BUY_STATUE,
        CASTLE_MSGREQ_REPAIR_STATUE,
        CASTLE_MSGREQ_UPGRADE_STATUE_HP,
        CASTLE_MSGREQ_UPGRADE_STATUE_DEFENSE,
        CASTLE_MSGREQ_UPGRADE_STATUE_RECOVER,
        CASTLE_MSGREQ_APPLY_TAX,
        CASTLE_MSGREQ_WITHDRAW,
    };

  private:
    enum
    {
        INVENTORY_WIDTH = 190,
        INVENTORY_HEIGHT = 429,
    };
    enum CURR_OPEN_TAB_BUTTON
    {
        TAB_GATE_MANAGING,
        TAB_STATUE_MANAGING,
        TAB_TAX_MANAGING,
        TAB_CASTLE_MIX
    };

    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;

    CNewUIRadioGroupButton m_TabBtn;
    int m_iNumCurOpenTab;
    int m_iCurrMsgBoxRequest;

    CNewUIButton m_BtnExit;

    CNewUIButton m_BtnBuy;
    CNewUIButton m_BtnRepair;
    CNewUIButton m_BtnUpgradeHP;
    CNewUIButton m_BtnUpgradeDefense;
    CNewUIButton m_BtnUpgradeRecover;
    CNewUIButton m_BtnApplyTax;
    CNewUIButton m_BtnWithdraw;
    CNewUIButton m_BtnChaosTaxUp;
    CNewUIButton m_BtnChaosTaxDn;
    CNewUIButton m_BtnNPCTaxUp;
    CNewUIButton m_BtnNPCTaxDn;

  public:
    explicit CNewUICastleWindow(SessionKeeper &keeper);
    virtual ~CNewUICastleWindow();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    void OpeningProcess();
    void ClosingProcess();

    float GetLayerDepth(); //. 5.0f

    int GetCurrMsgBoxRequest()
    {
        return m_iCurrMsgBoxRequest;
    }

  private:
    void LoadImages();
    void UnloadImages();

    void RenderFrame();
    bool BtnProcess();

    void SetCurrMsgBoxRequest(int iMsgBoxRequest)
    {
        m_iCurrMsgBoxRequest = iMsgBoxRequest;
    }
    void InsertComma(wchar_t *pszText, DWORD dwNumber);
    void InsertComma64(wchar_t *pszText, __int64 iNumber);

    void UpdateGateManagingTab();
    void UpdateStatueManagingTab();
    void UpdateTaxManagingTab();

    void RenderGateManagingTab();
    void RenderStatueManagingTab();
    void RenderTaxManagingTab();

    void RenderCastleItem(int nPosX, int nPosY, LPPMSG_NPCDBLIST pInfo);
    void InitButton(CNewUIButton *pNewUIButton, int iPos_x, int iPos_y, const wchar_t *pCaption);

    void RenderOutlineUpper(float fPos_x, float fPos_y, float fWidth, float fHeight);
    void RenderOutlineLower(float fPos_x, float fPos_y, float fWidth, float fHeight);
};
} // namespace SEASON3B

class PresentationSeparationTestPeer;

namespace SEASON3B
{
class CNewUIDuelWatchMainFrameWindow : public CNewUIObj,
                                       protected SessionUiLegacyBindings,
                                       public INewUI3DRenderObj
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_DUELWATCH_MAINFRAME_BACK1 = BITMAP_BUFFWATCH_MAINFRAME_BEGIN,
        IMAGE_DUELWATCH_MAINFRAME_BACK2,
        IMAGE_DUELWATCH_MAINFRAME_BACK3,
        IMAGE_DUELWATCH_MAINFRAME_SCORE,
        IMAGE_DUELWATCH_MAINFRAME_HP_GAUGE,
        IMAGE_DUELWATCH_MAINFRAME_SD_GAUGE,
        IMAGE_DUELWATCH_MAINFRAME_HP_GAUGE_FX,
        IMAGE_DUELWATCH_MAINFRAME_SD_GAUGE_FX,
        IMAGE_INVENTORY_EXIT_BTN = CNewUIMyInventory::IMAGE_INVENTORY_EXIT_BTN,
    };

  private:
    friend class ::PresentationSeparationTestPeer;
    void UpdateGaugeHistory();
    void RenderGauges();
    CNewUIManager *m_pNewUIMng;
    CNewUI3DRenderMng *m_pNewUI3DRenderMng;

    CNewUIButton m_BtnExit; // �ݱ� ��ư

    BOOL m_bHasHPReceived; // HP �ʱ�����ΰ�
    float m_fPrevHPRate1;
    float m_fPrevHPRate2;
    float m_fPrevSDRate1;
    float m_fPrevSDRate2;
    float m_fLastHPRate1;
    float m_fLastHPRate2;
    float m_fLastSDRate1;
    float m_fLastSDRate2;
    float m_fReceivedHPRate1;
    float m_fReceivedHPRate2;
    float m_fReceivedSDRate1;
    float m_fReceivedSDRate2;

  public:
    explicit CNewUIDuelWatchMainFrameWindow(SessionKeeper &keeper);
    virtual ~CNewUIDuelWatchMainFrameWindow();

    bool Create(CNewUIManager *pNewUIMng, CNewUI3DRenderMng *pNewUI3DRenderMng);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    void Render3D();

    bool IsVisible() const;

    void OpeningProcess();
    void ClosingProcess();

    float GetLayerDepth(); //. 5.0f

  private:
    void LoadImages();
    void UnloadImages();

    void RenderFrame();
    bool BtnProcess();
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIDuelWatchUserListWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_DUELWATCH_USERLIST_BOX = BITMAP_BUFFWATCH_USERLIST_BEGIN,
    };

  private:
    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;

  public:
    explicit CNewUIDuelWatchUserListWindow(SessionKeeper &keeper);
    virtual ~CNewUIDuelWatchUserListWindow();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    bool BtnProcess();

    float GetLayerDepth(); //. 5.4f

    void OpeningProcess();
    void ClosingProcess();

  private:
    void LoadImages();
    void UnloadImages();

    void RenderFrame();
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIDuelWatchWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIDuelWatchWindow(SessionKeeper &keeper);
    ~CNewUIDuelWatchWindow() override;
    bool Create(CNewUIManager *manager, int x, int y);
    void Release();
    void SetPos(int x, int y);
    bool UpdateMouseEvent() override;
    bool UpdateKeyEvent() override;
    bool Update() override;
    bool Render() override;
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    void OpeningProcess();
    void ClosingProcess();
    float GetLayerDepth() override;

  private:
    void StageContent();
    bool StageChannel(int channel, bool languageChanged);
    CNewUIManager *manager_ = nullptr;
    SessionRenderUnit &renderer_;
    UI::Modern::PC::Combat::RmlDuelWatchPanel panel_;
    UI::Modern::PC::Combat::RmlDuelWatchPanel::Content content_;
    std::array<std::array<std::wstring, 2>, 4> players_;
    std::array<bool, 4> active_{};
    std::string locale_;
    bool visible_ = false;
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIDuelWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  private:
    enum IMAGE_LIST
    {
        IMAGE_DUEL_BACK = BITMAP_INTERFACE_NEW_BATTLE_SOCCER_SCORE_BEGIN,
    };
    enum
    {
        DUEL_WND_WIDTH = 131,
        DUEL_WND_HEIGHT = 70,
    };

  public:
    explicit CNewUIDuelWindow(SessionKeeper &keeper);
    virtual ~CNewUIDuelWindow();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    float GetLayerDepth(); //. 1.1f

  private:
    void LoadImages();
    void UnloadImages();

    void RenderFrame();
    void RenderContents();

    CNewUIManager *m_pNewUIMng; // UI �Ŵ���.
    POINT m_Pos;                // â�� ��ġ.
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIGuardWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_GUARDWINDOW_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, // Reference
        IMAGE_GUARDWINDOW_TOP = CNewUIMyInventory::IMAGE_INVENTORY_BACK_TOP,
        IMAGE_GUARDWINDOW_LEFT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_LEFT,
        IMAGE_GUARDWINDOW_RIGHT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_RIGHT,
        IMAGE_GUARDWINDOW_BOTTOM = CNewUIMyInventory::IMAGE_INVENTORY_BACK_BOTTOM,
        IMAGE_GUARDWINDOW_EXIT_BTN = CNewUIMyInventory::IMAGE_INVENTORY_EXIT_BTN,
        IMAGE_GUARDWINDOW_TAB_BTN = CNewUIGuildInfoWindow::IMAGE_GUILDINFO_TAB_BUTTON,
        IMAGE_GUARDWINDOW_BUTTON = CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY_VERY_SMALL,

        IMAGE_GUARDWINDOW_TOP_PIXEL = CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_TOP_PIXEL,
        IMAGE_GUARDWINDOW_BOTTOM_PIXEL = CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_BOTTOM_PIXEL,
        IMAGE_GUARDWINDOW_LEFT_PIXEL = CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_LEFT_PIXEL,
        IMAGE_GUARDWINDOW_RIGHT_PIXEL = CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_RIGHT_PIXEL,

        IMAGE_GUARDWINDOW_SCROLL_TOP = CNewUIChatLogWindow::IMAGE_SCROLL_TOP,
        IMAGE_GUARDWINDOW_SCROLL_MIDDLE = CNewUIChatLogWindow::IMAGE_SCROLL_MIDDLE,
        IMAGE_GUARDWINDOW_SCROLL_BOTTOM = CNewUIChatLogWindow::IMAGE_SCROLL_BOTTOM,
        IMAGE_GUARDWINDOW_SCROLLBAR_ON = CNewUIChatLogWindow::IMAGE_SCROLLBAR_ON,
        IMAGE_GUARDWINDOW_SCROLLBAR_OFF = CNewUIChatLogWindow::IMAGE_SCROLLBAR_OFF,
    };

  private:
    enum
    {
        INVENTORY_WIDTH = 190,
        INVENTORY_HEIGHT = 429,
    };
    enum CURR_OPEN_TAB_BUTTON
    {
        TAB_SIEGE_INFO,
        TAB_REGISTER,
        TAB_REGISTER_INFO
    };

    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;

    CNewUIRadioGroupButton m_TabBtn;
    int m_iNumCurOpenTab; // ���� �����ִ� �ǹ�ư��ȣ

    CNewUIButton m_BtnExit;

    CNewUIButton m_BtnProclaim; // ���� ���� ��ư
    CNewUIButton m_BtnRegister; // ǥ�� ��� ��ư
    CNewUIButton m_BtnGiveUp;   // ���� ���� ��ư

    // ������ ��� ����Ʈ
    CUIBCDeclareGuildListBox m_DeclareGuildListBox;
    // Ȯ���� ��� ����Ʈ
    CUIBCGuildListBox m_GuildListBox;

    // UI ��� ��
    CASTLESIEGE_STATE m_eTimeType;

    wchar_t m_szOwnerGuild[8 + 1];
    wchar_t m_szOwnerGuildMaster[10 + 1];

    WORD m_wStartYear;
    BYTE m_byStartMonth;
    BYTE m_byStartDay;
    BYTE m_byStartHour;
    BYTE m_byStartMinute;
    WORD m_wEndYear;
    BYTE m_byEndMonth;
    BYTE m_byEndDay;
    BYTE m_byEndHour;
    BYTE m_byEndMinute;
    WORD m_wSiegeStartYear;
    BYTE m_bySiegeStartMonth;
    BYTE m_bySiegeStartDay;
    BYTE m_bySiegeStartHour;
    BYTE m_bySiegeStartMinute;
    DWORD m_dwStateLeftSec;

  public:
    explicit CNewUIGuardWindow(SessionKeeper &keeper);
    virtual ~CNewUIGuardWindow();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    void RenderScrollBarFrame(int iPos_x, int iPos_y, int iHeight);
    void RenderScrollBar(int iPos_x, int iPos_y, BOOL bIsClicked);

    void OpeningProcess();
    void ClosingProcess();

    float GetLayerDepth(); //. 5.0f

    void SetData(LPPMSG_ANS_CASTLESIEGESTATE
                     Info); // �������� �޾� ȭ�� ǥ�� ����

    void AddDeclareGuildList(wchar_t *szGuildName, int nMarkCount, BYTE byIsGiveUP, BYTE bySeqNum);
    void ClearDeclareGuildList();
    void SortDeclareGuildList();
    void AddGuildList(wchar_t *szGuildName, BYTE byCsJoinSide, BYTE byGuildInvolved,
                      int iGuildScore);
    void ClearGuildList();

  private:
    void LoadImages();
    void UnloadImages();

    void RenderFrame();
    bool BtnProcess();

    void InitButton(CNewUIButton *pNewUIButton, int iPos_x, int iPos_y,
                    const wchar_t *const *pCaptionSlot);

    void UpdateSeigeInfoTab();
    void UpdateRegisterTab();
    void UpdateRegisterInfoTab();

    void RenderSeigeInfoTab();
    void RenderRegisterTab();
    void RenderRegisterInfoTab();
};
} // namespace SEASON3B

#define MAX_COMMANDGROUP (7)

namespace SEASON3B
{
class CNewUISiegeWarBase : protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        // MiniMap
        IMAGE_SKILL_ICON = CNewUISkillList::IMAGE_SKILL2, // newui_skill2.jpg
        IMAGE_MINIMAP_FRAME = BITMAP_SIEGEWAR_BEGIN,      // newui_SW_Minimap_Frame.tga		(154, 162)
        IMAGE_TIME_FRAME,                                 // newui_SW_Time_Frame.tga			(134, 37)
        IMAGE_MINIMAP,                                    // map1.tga							(256, 256)
        IMAGE_COMMAND_ATTACK,                             // i_attack.tga		(13, 13)
        IMAGE_COMMAND_DEFENCE,                            // i_defense.tga	(18, 15)
        IMAGE_COMMAND_WAIT,                               // i_wait.tga		(11, 12)
        IMAGE_BATTLESKILL_FRAME,   // newui_SW_BattleSkill_Frame.tga	(128, 53)
        IMAGE_SKILL_BTN_SCROLL_UP, // newui_Bt_skill_scroll_up.jpg		(15, 13)
        IMAGE_SKILL_BTN_SCROLL_DN, // newui_Bt_skill_scroll_dn.jpg		(15, 13)
        IMAGE_BTN_ALPHA,           // newui_SW_MiniMap_Bt_clearness.jpg (38, 21)
        IMAGE_SIEGEWAR_BASE_FRAME_END,
    };

    enum FRAME_SIZE
    {
        MINIMAP_FRAME_WIDTH = 154,
        MINIMAP_FRAME_HEIGHT = 162,
        TIME_FRAME_WIDTH = 134,
        TIME_FRAME_HEIGHT = 37,
        MINIMAP_BTN_ALPHA_WIDTH = 30,
        MINIMAP_BTN_ALPHA_HEIGHT = 22,
        COMMAND_ATTACK_WIDTH = 13,
        COMMAND_ATTACK_HEIGHT = 13,
        COMMAND_DEFENCE_WIDTH = 18,
        COMMAND_DEFENCE_HEIGHT = 15,
        COMMAND_WAIT_WIDTH = 11,
        COMMAND_WAIT_HEIGHT = 12,
        BATTLESKILL_FRAME_WIDTH = 128,
        BATTLESKILL_FRAME_HEIGHT = 53,
        SKILL_BTN_SCROLL_WIDTH = 15,
        SKILL_BTN_SCROLL_HEIGHT = 13,
        SKILL_ICON_WIDTH = 20,
        SKILL_ICON_HEIGHT = 28,
        SKILL_TOOLTIP_WIDTH = 128,
        SKILL_TOOLTIP_HEIGHT = 32,
        BTN_ALPHA_WIDTH = 38,
        BTN_ALPHA_HEIGHT = 23,
    };

  protected:
    POINT m_MiniMapFramePos;
    POINT m_MiniMapPos;
    POINT m_TimeUIPos;
    POINT m_SkillFramePos;
    POINT m_BtnSkillScrollUpPos;
    POINT m_BtnSkillScrollDnPos;
    POINT m_SkillIconPos;
    POINT m_UseSkillDestKillPos;
    POINT m_CurKillCountPos;
    POINT m_BtnAlphaPos;
    POINT m_SkillTooltipPos;

    POINT m_HeroPosInWorld;
    POINT m_HeroPosInMiniMap;
    POINT m_MiniMapScaleOffset;

    float m_fMiniMapTexU;
    float m_fMiniMapTexV;

    int m_iMiniMapScale;
    float m_fMiniMapAlpha;
    bool m_bRenderSkillUI;
    bool m_bRenderToolTip;

    int m_iHour;
    int m_iMinute;

    DWORD m_dwBuffState;

    GuildCommander m_CmdBuffer[MAX_COMMANDGROUP]{};
    SessionBoundArray<CNewUIButton, 2> m_BtnSkillScroll;
    CNewUIButton m_BtnAlpha;
    UI::Skills::Tooltip::Renderer m_SkillTooltip;

    std::list<int> m_listBattleSkill;
    std::list<int>::iterator m_iterCurBattleSkill;

  public:
    explicit CNewUISiegeWarBase(SessionKeeper &keeper);
    virtual ~CNewUISiegeWarBase();

  protected:
    virtual bool OnCreate(int x, int y) = 0;
    virtual bool OnUpdate() = 0;
    virtual bool OnRender() = 0;
    virtual void OnRelease() = 0;
    virtual bool OnUpdateMouseEvent() = 0;
    virtual bool OnUpdateKeyEvent() = 0;
    virtual bool OnBtnProcess() = 0;
    virtual void OnLoadImages() = 0;
    virtual void OnUnloadImages() = 0;
    virtual void OnSetPos(int x, int y) = 0;

    void RenderCmdIconInMiniMap();

  public:
    bool Create(int x, int y);
    bool Update();
    bool Render();
    void Release();

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    void LoadImages();
    void UnLoadImages();

    void SetPos(int x, int y);
    void SetTime(int iHour, int iMinute);
    void SetMapInfo(GuildCommander &data);
    void SetRenderSkillUI(bool bRenderSkillUI);

    bool InitBattleSkill();
    void ReleaseBattleSkill();

  private:
    bool BtnProcess();
    void UpdateBuffState();
    void UpdateHeroPos();
    void RenderSkillIcon();

    void SetSkillScrollUp();
    void SetSkillScrollDn();
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUISiegeWarfare : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum SIEGEWAR_TYPE
    {
        SIEGEWAR_TYPE_NONE = -1,
        SIEGEWAR_TYPE_OBSERVER = 0,
        SIEGEWAR_TYPE_COMMANDER,
        SIEGEWAR_TYPE_SOLDIER,
    };

  private:
    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;

    CNewUISiegeWarBase *m_pSiegeWarUI;
    short m_sGuildMarkIndex;
    BYTE m_byGuildStatus;
    int m_iCurSiegeWarType;

    int m_iHour;
    int m_iMinute;
    int m_iSecond;
    DWORD m_dwSyncTime;

    bool m_bCreated;

  public:
    explicit CNewUISiegeWarfare(SessionKeeper &keeper);
    virtual ~CNewUISiegeWarfare();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    bool CreateMiniMapUI();
    void InitMiniMapUI();
    void SetGuildData(const CHARACTER *pCharacter);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    bool BtnProcess();

    float GetLayerDepth(); //. 1.6f

    void OpenningProcess();
    void ClosingProcess();

    void ClearGuildMemberLocation(void);
    void SetGuildMemberLocation(BYTE type, int x, int y);
    void SetTime(BYTE byHour, BYTE byMinute);

    void SetMapInfo(GuildCommander &data);

  public:
    inline CNewUISiegeWarBase *GetBase()
    {
        if (!m_pSiegeWarUI)
            return NULL;

        return m_pSiegeWarUI;
    }

    inline int GetCurSiegeWarType()
    {
        return m_iCurSiegeWarType;
    };
    inline bool IsCreated()
    {
        return m_bCreated;
    };

    void InitSkillUI();
    void ReleaseSkillUI();
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUISiegeWarCommander : public CNewUISiegeWarBase
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_MINIMAP_BTN_GROUP = CNewUISiegeWarBase::
            IMAGE_SIEGEWAR_BASE_FRAME_END, // newui_SW_Minimap_Bt_group.tga	(26, 22)
        IMAGE_MINIMAP_BTN_COMMAND,         // newui_SW_Minimap_Bt_Command.tga	(30, 22)
    };

    enum FRAME_SIZE
    {
        MINIMAP_BTN_GROUP_WIDTH = 26,
        MINIMAP_BTN_GROUP_HEIGHT = 22,
        MINIMAP_BTN_COMMAND_WIDTH = 30,
        MINIMAP_BTN_COMMAND_HEIGHT = 22,
    };

    enum MIMIMAP_COMMAND
    {
        MINIMAP_CMD_ATTACK = 0,
        MINIMAP_CMD_DEFENCE,
        MINIMAP_CMD_FLAG,
        MINIMAP_CMD_MAX,
    };

  private:
    POINT m_BtnCommandGroupPos;
    POINT m_BtnCommandPos;

    SessionBoundArray<CNewUIButton, MAX_COMMANDGROUP> m_BtnCommandGroup;
    SessionBoundArray<CNewUIButton, MINIMAP_CMD_MAX> m_BtnCommand;

    int m_iCurSelectBtnGroup;
    int m_iCurSelectBtnCommand;
    bool m_bMouseInMiniMap;

    std::vector<VisibleUnitLocation> m_vGuildMemberLocationBuffer;

  public:
    explicit CNewUISiegeWarCommander(SessionKeeper &keeper);
    virtual ~CNewUISiegeWarCommander();

  private:
    virtual bool OnCreate(int x, int y);
    virtual bool OnUpdate();
    virtual bool OnRender();
    virtual void OnRelease();

    virtual bool OnUpdateMouseEvent();
    virtual bool OnUpdateKeyEvent();
    virtual bool OnBtnProcess();
    virtual void OnSetPos(int x, int y);

    virtual void OnLoadImages();
    virtual void OnUnloadImages();

    void InitDestKill();
    void InitCmdGroupBtn();
    void InitCmdBtn();
    void RenderCharPosInMiniMap();
    void RenderGuildMemberPosInMiniMap();
    void RenderCmdIconAtMouse();
    void RenderCmdGroupBtn();
    void RenderCmdBtn();

    void SetBtnState(int iBtnType, bool bStateDown);

  public:
    void ClearGuildMemberLocation(void);
    void SetGuildMemberLocation(BYTE type, int x, int y);
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUISiegeWarObserver : public CNewUISiegeWarBase
{
  public:
    explicit CNewUISiegeWarObserver(SessionKeeper &keeper);
    virtual ~CNewUISiegeWarObserver();

  private:
    virtual bool OnCreate(int x, int y);
    virtual bool OnUpdate();
    virtual bool OnRender();
    virtual void OnRelease();

    virtual bool OnUpdateMouseEvent();
    virtual bool OnUpdateKeyEvent();
    virtual bool OnBtnProcess();
    virtual void OnSetPos(int x, int y);

    virtual void OnLoadImages();
    virtual void OnUnloadImages();

    void RenderCharPosInMiniMap();
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUISiegeWarSoldier : public CNewUISiegeWarBase
{
  public:
    explicit CNewUISiegeWarSoldier(SessionKeeper &keeper);
    virtual ~CNewUISiegeWarSoldier();

  private:
    virtual bool OnCreate(int x, int y);
    virtual bool OnUpdate();
    virtual bool OnRender();
    virtual void OnRelease();

    virtual bool OnUpdateMouseEvent();
    virtual bool OnUpdateKeyEvent();
    virtual bool OnBtnProcess();
    virtual void OnSetPos(int x, int y);

    virtual void OnLoadImages();
    virtual void OnUnloadImages();

    void RenderCharPosInMiniMap();
};
} // namespace SEASON3B

// Desc: interface for the CNewUIBattleSoccerScore class.
//		 �����౸ ���� UI Ŭ����.
// producer: Ahn Sang-Kyu

namespace SEASON3B
{
class CNewUIBattleSoccerScore : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_BSS_BACK = BITMAP_INTERFACE_NEW_BATTLE_SOCCER_SCORE_BEGIN,
    };

  private:
    enum
    {
        BSS_WIDTH = 131,
        BSS_HEIGHT = 70,
    };

    CNewUIManager *m_pNewUIMng; // UI �Ŵ���.
    POINT m_Pos;                // â�� ��ġ.

  public:
    explicit CNewUIBattleSoccerScore(SessionKeeper &keeper);
    virtual ~CNewUIBattleSoccerScore();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    float GetLayerDepth(); //. 1.8f

  private:
    void LoadImages();
    void UnloadImages();

    void RenderBackImage();
    void RenderContents();

    int FindGuildMark(wchar_t *pszGuildName);
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIEnterBloodCastle : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIEnterBloodCastle(SessionKeeper &keeper);
    ~CNewUIEnterBloodCastle() override;
    bool Create(CNewUIManager *manager, int x, int y);
    void Release();
    void SetPos(int x, int y);
    bool UpdateMouseEvent() override;
    bool UpdateKeyEvent() override;
    bool Update() override;
    bool Render() override;
    bool BtnProcess();
    float GetLayerDepth() override;
    void OpenningProcess();
    void ClosingProcess();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);

  private:
    static constexpr int MAX_ENTER_GRADE = 8;
    int CheckLimitLV(int index);
    void StageGrades(int levelGroup);
    int m_iBloodCastleLimitLevel[MAX_ENTER_GRADE * 2][2];
    int m_iNumActiveBtn = -1;
    CNewUIManager *manager_ = nullptr;
    SessionRenderUnit &renderer_;
    UI::Modern::PC::Inventory::RmlItemDialogPanel panel_;
    std::string activeButton_;
    bool visible_ = false;
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIBloodCastle : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_BLOODCASTLE_TIME_WINDOW =
            BITMAP_INTERFACE_NEW_BLOODCASTLE_BEGIN // newui_Figure_blood.tga (124, 81)
    };

  private:
    enum BLOODCASTLE_TIME_WINDOW_SIZE
    {
        BLOODCASTLE_TIME_WINDOW_WIDTH = 124,
        BLOODCASTLE_TIME_WINDOW_HEIGHT = 81,
    };

    enum
    {
        BC_TIME_STATE_NORMAL = 1,
        BC_TIME_STATE_IMMINENCE,
    };

    enum
    {
        MAX_KILL_MONSTER = 65535,
    };

  private:
    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;

    wchar_t m_szTime[256]; // �ð�
    int m_iTime;           // �ð�
    int m_iTimeState;      // �ð�����( �⺻, �ӹ� )
    int m_iMaxKillMonster; // �׿����ϴ� ���ͼ���
    int m_iKilledMonster;  // ���� ���� ���ͼ���

  public:
    explicit CNewUIBloodCastle(SessionKeeper &keeper);
    virtual ~CNewUIBloodCastle();

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

  private:
    void LoadImages();
    void UnloadImages();

  public:
    void SetTime(int m_iTime);
    void SetKillMonsterStatue(int iKilled, int iMaxKill);
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUICatapultWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  private:
    class CCatapultGroupButton : protected SessionUiLegacyBindings
    {
      public:
        explicit CCatapultGroupButton(SessionKeeper &keeper);
        virtual ~CCatapultGroupButton();

        void Create(int iType, POINT ptWindow);
        int UpdateMouseEvent();
        void Render();
        int GetIndex();

      private:
        void Initialize();
        void AllUnLock();
        void BtnSelected(int iIndex);

        SessionBoundArray<CNewUIButton, 4> m_Button;

        int m_iBtnNum;
        int m_iType;
        int m_iIndex;
    };

  public:
    enum IMAGE_LIST
    {
        // �⺻â
        IMAGE_CATAPULT_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, // newui_msgbox_back.jpg
        IMAGE_CATAPULT_TOP =
            CNewUIMyInventory::IMAGE_INVENTORY_BACK_TOP, // newui_item_back01.tga	(190,64)
        IMAGE_CATAPULT_LEFT =
            CNewUIMyInventory::IMAGE_INVENTORY_BACK_LEFT, // newui_item_back02-l.tga	(21,320)
        IMAGE_CATAPULT_RIGHT =
            CNewUIMyInventory::IMAGE_INVENTORY_BACK_RIGHT, // newui_item_back02-r.tga	(21,320)
        IMAGE_CATAPULT_BOTTOM =
            CNewUIMyInventory::IMAGE_INVENTORY_BACK_BOTTOM, // newui_item_back03.tga	(190,45)
        IMAGE_CATAPULT_BTN_EXIT = CNewUIMyInventory::IMAGE_INVENTORY_EXIT_BTN, //. newui_exit_00.tga

        // ���̺�
        IMAGE_CATAPULT_TABLE_TOP_LEFT =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_TOP_LEFT, //. newui_item_table01(L).tga (14,14)
        IMAGE_CATAPULT_TABLE_TOP_RIGHT =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_TOP_RIGHT, //. newui_item_table01(R).tga (14,14)
        IMAGE_CATAPULT_TABLE_BOTTOM_LEFT =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_BOTTOM_LEFT, //. newui_item_table02(L).tga (14,14)
        IMAGE_CATAPULT_TABLE_BOTTOM_RIGHT = CNewUIInventoryCtrl::
            IMAGE_ITEM_TABLE_BOTTOM_RIGHT, //. newui_item_table02(R).tga (14,14)
        IMAGE_CATAPULT_TABLE_TOP_PIXEL =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_TOP_PIXEL, //. newui_item_table03(up).tga (1, 14)
        IMAGE_CATAPULT_TABLE_BOTTOM_PIXEL = CNewUIInventoryCtrl::
            IMAGE_ITEM_TABLE_BOTTOM_PIXEL, //. newui_item_table03(dw).tga (1,14)
        IMAGE_CATAPULT_TABLE_LEFT_PIXEL =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_LEFT_PIXEL, //. newui_item_table03(L).tga (14,1)
        IMAGE_CATAPULT_TABLE_RIGHT_PIXEL =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_RIGHT_PIXEL, //. newui_item_table03(R).tga (14,1)

        // ��ư
        IMAGE_CATAPULT_BTN_FIRE = CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY,

        IMAGE_CATAPULT_BTN_SMALL = BITMAP_CATAPULT_BEGIN,
        IMAGE_CATAPULT_BTN_BIG,
    };
    enum CATAPULT_TYPE
    {
        CATAPULT_ATTACK = 1,
        CATAPULT_DEFENSE = 2,
    };

  public:
    explicit CNewUICatapultWindow(SessionKeeper &keeper);
    virtual ~CNewUICatapultWindow();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    float GetLayerDepth(); //. 5.0f

    void OpenningProcess();
    void ClosingProcess();

    void Init(int iKey, int iType);
    void DoFire(int iKey, int iResult, int iType, int iPositionX, int iPositionY);
    void DoFireFixStartPosition(int iType, int iPositionX, int iPositionY);
    void SetCameraPos(float x = 0.f, float y = 0.f, float z = 0.f);
    void GetCameraPos(vec3_t &vPos);

  private:
    void LoadImages();
    void UnloadImages();

    void SetButtonInfo();

    bool BtnProcess();

    void RenderFrame();
    void RenderTexts();
    void RenderButtons();

    void RenderOutlineUpper(float fPos_x, float fPos_y, float fWidth, float fHeight);
    void RenderOutlineLower(float fPos_x, float fPos_y, float fWidth, float fHeight);

  private:
    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;

    CNewUIButton m_BtnExit;
    CCatapultGroupButton m_BtnChoiceArea; // ��������
    CNewUIButton m_BtnFire;               // �߻��ư

    int m_iType;
    int m_iNpcKey;
    vec3_t m_vCameraPos;
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIChaosCastleTime : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_CHAOSCASTLE_TIME_WINDOW =
            CNewUIBloodCastle::IMAGE_BLOODCASTLE_TIME_WINDOW // newui_Figure_blood.tga (124, 81)
    };

  private:
    enum BLOODCASTLE_TIME_WINDOW_SIZE
    {
        CHAOSCASTLE_TIME_WINDOW_WIDTH = 124,
        CHAOSCASTLE_TIME_WINDOW_HEIGHT = 81,
    };

    enum
    {
        CC_TIME_STATE_NORMAL = 1,
        CC_TIME_STATE_IMMINENCE,
    };

    enum
    {
        MAX_KILL_MONSTER = 65535,
    };

  private:
    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;

    wchar_t m_szTime[256]; // �ð�
    int m_iTime;           // �ð�
    int m_iTimeState;      // �ð�����( �⺻, �ӹ� )
    int m_iMaxKillMonster; // �׿����ϴ� ���ͼ���
    int m_iKilledMonster;  // ���� ���� ���ͼ���

  public:
    explicit CNewUIChaosCastleTime(SessionKeeper &keeper);
    virtual ~CNewUIChaosCastleTime();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    bool BtnProcess();

    float GetLayerDepth(); //. 1.3f

    void OpenningProcess();
    void ClosingProcess();

  private:
    void LoadImages();
    void UnloadImages();

  public:
    void SetTime(int m_iTime);
    void SetKillMonsterStatue(int iKilled, int iMaxKill);
};
}; // namespace SEASON3B

class CGMCrywolf1st;

namespace SEASON3B
{
class CNewUICryWolf : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_MVP_INTERFACE = BITMAP_INTERFACE_CRYWOLF_BEGIN,
    };

  private:
    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;
    int m_iHour;
    int m_iMinute;
    int m_iSecond;
    DWORD m_dwSyncTime;
    int m_icntTime;
    bool m_bTimeStart;
    CGMCrywolf1st &crywolf_;
    float presentationTicks_ = 0.f;
    void AdvanceResultPresentation();
    void UpdateCountdown();

  public:
    explicit CNewUICryWolf(SessionKeeper &keeper);
    virtual ~CNewUICryWolf();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool Render(int Posx, int Posy, int nPosx, int nPosy, float u, float v, float su, float sv,
                int Index, bool Scale = false, bool StartScale = false, float Alpha = 1.f);

    float GetLayerDepth(); //. 10.0f

    void OpenningProcess();
    void ClosingProcess();
    float ConvertX(float x);
    float ConvertY(float y);
    void SetTime(int iHour, int iMinute);
    void InitTime();

  private:
    void LoadImages();
    void UnloadImages();
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUICursedTempleEnter : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    static constexpr float CURSEDTEMPLE_ENTER_WINDOW_WIDTH = 230.0f;
    static constexpr float CURSEDTEMPLE_ENTER_WINDOW_HEIGHT = 252.0f;

    enum
    {
        CURSEDTEMPLEENTER_OPEN = 0,
        CURSEDTEMPLEENTER_EXIT,
        CURSEDTEMPLEENTER_MAXBUTTONCOUNT,
    };

  public:
    explicit CNewUICursedTempleEnter(SessionKeeper &keeper);
    virtual ~CNewUICursedTempleEnter();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);

  private:
    void SetButtonInfo();

  public:
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();

  public:
    bool CheckEnterLevel(int &enterlevel);
    bool CheckEnterItem(ITEM *p, int enterlevel);
    bool CheckInventory(BYTE &itempos, int enterlevel);

  public:
    bool Render();

  private:
    void RenderFrame();
    void RenderText();
    void RenderButtons();

  public:
    void SetPos(int x, int y);

  public:
    const POINT &GetPos() const;
    float GetLayerDepth(); //. 5.0f

  public:
    void SetCursedTempleEnterInfo(const BYTE *cursedtempleinfo);
    void ReceiveCursedTempleEnterInfo(const BYTE *cursedtempleinfo);

  private:
    void DrawText(wchar_t *text, int textposx, int textposy, DWORD textcolor, DWORD textbackcolor,
                  int textsort, float fontboxwidth, bool isbold);
    void Initialize();
    void Destroy();

  private:
    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;
    SessionBoundArray<CNewUIButton, CURSEDTEMPLEENTER_MAXBUTTONCOUNT> m_Button;
    int m_EnterTime;
    int m_EnterCount;
};

inline float CNewUICursedTempleEnter::GetLayerDepth()
{
    return 10.3;
}

inline void CNewUICursedTempleEnter::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

inline const POINT &CNewUICursedTempleEnter::GetPos() const
{
    return m_Pos;
}
}; // namespace SEASON3B

namespace SEASON3B
{
class CNewUICursedTempleResult : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    struct CursedTempleGameResult
    {
        wchar_t s_characterId[MAX_USERNAME_SIZE + 1];
        short s_mapnumber;
        SEASON3A::eCursedTempleTeam s_team;
        BYTE s_point;
        CLASS_TYPE s_class;
        DWORD s_addexp;

        CursedTempleGameResult()
            : s_mapnumber(-1), s_team(SEASON3A::eTeam_Count), s_point(0xff),
              s_class(CLASS_UNDEFINED), s_addexp(0xff)
        {
            memset(&s_characterId, 0, sizeof(s_characterId));
        }
    };

    explicit CNewUICursedTempleResult(SessionKeeper &keeper);
    ~CNewUICursedTempleResult();
    bool Create(CNewUIManager *manager, int x, int y);
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    void OpenningProcess();
    void ClosingProcess();
    const POINT &GetPos() const
    {
        return m_Pos;
    }
    float GetLayerDepth()
    {
        return 10.2f;
    }
    void SetPos(int x, int y)
    {
        m_Pos = {x, y};
    }
    void SetMyTeam(SEASON3A::eCursedTempleTeam team)
    {
        m_MyTeam = team;
    }
    void ReceiveCursedTempleGameResult(const BYTE *buffer);
    void ResetGameResultInfo();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);

  private:
    using CT_GameResult_list = std::list<CursedTempleGameResult>;
    void Destroy();
    void StageContent();
    void StageTeam(int team, const CT_GameResult_list &results);
    void RenderDetails();
    CT_GameResult_list m_AlliedTeamGameResult, m_IllusionTeamGameResult;
    int m_WinState = 0;
    SEASON3A::eCursedTempleTeam m_MyTeam = SEASON3A::eTeam_Count;
    POINT m_Pos{};
    CNewUIManager *manager_ = nullptr;
    SessionRenderUnit &renderer_;
    UI::Modern::PC::Events::RmlTempleResultPanel panel_;
    UI::Modern::PC::Events::RmlTempleResultPanel::Content content_;
    std::array<std::vector<std::wstring>, 2> experienceTips_;
    std::string locale_;
    bool visible_ = false, contentDirty_ = true;
};
} // namespace SEASON3B

class CSkillManager;

namespace SEASON3B
{
class CNewUICursedTempleSystem : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_CURSEDTEMPLESYSTEM_TOP = CNewUIMessageBoxMng::IMAGE_MSGBOX_TOP,
        IMAGE_CURSEDTEMPLESYSTEM_MIDDLE = CNewUIMessageBoxMng::IMAGE_MSGBOX_MIDDLE,
        IMAGE_CURSEDTEMPLESYSTEM_BOTTOM = CNewUIMessageBoxMng::IMAGE_MSGBOX_BOTTOM,
        IMAGE_CURSEDTEMPLESYSTEM_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK,
        IMAGE_CURSEDTEMPLESYSTEM_BTN = CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY_VERY_SMALL,

        IMAGE_CURSEDTEMPLESYSTEM_MINIMAPFRAME = BITMAP_CURSEDTEMPLE_BEGIN + 2,
        IMAGE_CURSEDTEMPLESYSTEM_MINIMAP,
        IMAGE_CURSEDTEMPLESYSTEM_MINIMAPALPBTN,
        IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_HOLYITEM_PC,
        IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ILLUSION_HOLYITEM,
        IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ILLUSION_PC,
        IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ILLUSION_NPC,
        IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ALLIED_HOLYITEM,
        IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ALLIED_PC,
        IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ALLIED_NPC,
        IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_HERO,
        IMAGE_CURSEDTEMPLESYSTEM_SKILLFRAME,
        IMAGE_CURSEDTEMPLESYSTEM_SKILLUPBT,
        IMAGE_CURSEDTEMPLESYSTEM_SKILLDOWNBT,
        IMAGE_CURSEDTEMPLESYSTEM_GAMETIME,
        IMAGE_CURSEDTEMPLESYSTEM_SCORE_ALLIED_NUMBER,
        IMAGE_CURSEDTEMPLESYSTEM_SCORE_ILLUSION_NUMBER =
            IMAGE_CURSEDTEMPLESYSTEM_SCORE_ALLIED_NUMBER + 10,
        IMAGE_CURSEDTEMPLESYSTEM_SCORE_VS0 = IMAGE_CURSEDTEMPLESYSTEM_SCORE_ILLUSION_NUMBER + 10,
        IMAGE_CURSEDTEMPLESYSTEM_SCORE_VS1,
        IMAGE_CURSEDTEMPLESYSTEM_SCORE_ALLIED_GAAIL,
        IMAGE_CURSEDTEMPLESYSTEM_SCORE_ILLUSION_GAAIL,
        IMAGE_CURSEDTEMPLESYSTEM_SCORE_LEFT,
        IMAGE_CURSEDTEMPLESYSTEM_SCORE_RIGHT,
        IMAGE_SKILL2 = CNewUISkillList::IMAGE_SKILL2,
        IMAGE_NON_SKILL2 = CNewUISkillList::IMAGE_NON_SKILL2,
    };

    enum
    {
        CURSEDTEMPLERESULT_ALPH = 0,
        CURSEDTEMPLERESULT_SKILLUP,
        CURSEDTEMPLERESULT_SKILLDOWN,
        CURSEDTEMPLERESULT_MAXBUTTONCOUNT,
    };

  public:
    explicit CNewUICursedTempleSystem(SessionKeeper &keeper);
    virtual ~CNewUICursedTempleSystem();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);

  private:
    void LoadImages();
    void UnloadImages();
    void SetButtonInfo();

  public:
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();

  public:
    bool CheckTalkProgressNpc(DWORD npcindex, DWORD npckey);
    bool CheckHeroSkillType(int operatortype = 0);
    bool CheckDragonRender();

  private:
    void UpdateScore();
    void UpdateTutorialStep();

  public:
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);

  private:
    void RenderSkillTooltip();
    void StageModernContent();
    void RenderGameTime();
    void RenderMiniMap();
    void RenderScore();
    void RenderTutorialStep();
    void DrawText(wchar_t *text, int textposx, int textposy, DWORD textcolor, DWORD textbackcolor,
                  int textsort, float fontboxwidth, bool isbold);

  public:
    const POINT &GetPos() const;
    float GetLayerDepth(); //. 1.5f

  public:
    void SetPos(int x, int y);
    void ResetCursedTempleSystemInfo();
    void StartScoreEffect();
    void StartTutorialStep();
    void EndScoreEffect();
    void EndTutorialStep();

    SEASON3A::eCursedTempleTeam GetMyTeam();

  public:
    void ReceiveCursedTempleInfo(const BYTE *ReceiveBuffer);
    void ReceiveCursedTempSkillPoint(const BYTE *ReceiveBuffer);
    void ReceiveCursedTempleHolyItemRelics(const BYTE *ReceiveBuffer);

  private:
    void Initialize();
    void Destroy();

  private:
    SessionRenderUnit &renderer_;
    UI::Modern::PC::Events::RmlTempleInfoPanel infoPanel_;
    UI::Modern::PC::Events::RmlTempleScorePanel scorePanel_;
    UI::Modern::PC::Events::RmlTempleInfoPanel::Content modernContent_;
    std::array<int, 3> stagedScores_{-1, -1, -1};
    std::array<std::wstring, 2> teamLabels_;
    std::string locale_;
    bool modernVisible_ = false;
    bool modernScoreVisible_ = false;
    SessionKeeper &sessionKeeper_;
    CSkillManager &gSkillManager;
    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;
    SessionBoundArray<CNewUIButton, CURSEDTEMPLERESULT_MAXBUTTONCOUNT> m_Button;
    //EventTime
    DWORD m_EventMapTime; // Total event time
    //MiniMap
    WORD m_HolyItemPlayerIndex; // Index of the player holding the Holy Item
    WORD m_HolyItemPlayerPosX;  // Sacred Item X-coordinate
    WORD m_HolyItemPlayerPosY;  // Sacred Item Y-coordinate
    wchar_t m_HolyItemPlayerName[MAX_USERNAME_SIZE];

    float m_Scale;
    float m_Alph;

    //HolyItemCount
    WORD m_AlliedPoint;   // Allied Forces score
    WORD m_IllusionPoint; // Illusion Cult score

    WORD m_CursedTempleMyTeamCount;
    PMSG_CURSED_TAMPLE_PARTY_POS m_CursedTempleMyTeam[MAX_PARTYS];
    //Team
    SEASON3A::eCursedTempleTeam m_MyTeam;
    //skillpoint
    WORD m_SkillPoint;
    //Score
    bool m_IsScoreEffect;
    DWORD m_StartScoreEffectTime;
    WORD m_ScoreEffectState;
    float m_ScoreEffectAlph;
    //Tutorial Step
    bool m_IsTutorialStep;
    WORD m_TutorialStepState;
    DWORD m_TutorialStepTime;
};

inline const POINT &CNewUICursedTempleSystem::GetPos() const
{
    return m_Pos;
}

inline float CNewUICursedTempleSystem::GetLayerDepth() //. 1.5f
{
    return 1.5f;
}

inline void CNewUICursedTempleSystem::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}
}; // namespace SEASON3B

class PresentationSeparationTestPeer;

namespace SEASON3B
{
class CNewUIDoppelGangerFrame : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_DOPPELGANGER_FRAME_WINDOW = BITMAP_DOPPELGANGER_FRAME_BEGIN,
        IMAGE_DOPPELGANGER_GUAGE_RED,
        IMAGE_DOPPELGANGER_GUAGE_ORANGE,
        IMAGE_DOPPELGANGER_GUAGE_YELLOW,
        IMAGE_DOPPELGANGER_GUAGE_PLAYER,
        IMAGE_DOPPELGANGER_GUAGE_PARTY_MEMBER,
        IMAGE_DOPPELGANGER_GUAGE_ICEWALKER,
    };

  private:
    friend class ::PresentationSeparationTestPeer;
    enum DOPPELGANGER_FRAME_WINDOW_SIZE
    {
        DOPPELGANGER_FRAME_WINDOW_WIDTH = 227,
        DOPPELGANGER_FRAME_WINDOW_HEIGHT = 87,
    };

    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;

    typedef struct _PARTY_POSITION
    {
        float m_fPositionRcvd;
        float m_fPosition;
    } PARTY_POSITION;

  public:
    explicit CNewUIDoppelGangerFrame(SessionKeeper &keeper);
    virtual ~CNewUIDoppelGangerFrame();

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

    void SetRemainTime(int iSeconds);
    void StopTimer(BOOL bFlag);

    void SetEnteredMonsters(int iCount)
    {
        m_iEnteredMonsters = iCount;
    }
    void SetMaxMonsters(int iCount)
    {
        m_iMaxMonsters = iCount;
    }
    void SetMonsterGauge(float fValue);
    void ResetPartyMemberInfo();
    void SetPartyMemberRcvd();
    void SetPartyMemberInfo(WORD wIndex, float fPosition);
    void SetIceWalkerMap(BOOL bEnable, float fPosition);

    void EnabledDoppelGangerEvent(BOOL bFlag)
    {
        m_bIsEnabled = bFlag;
    }
    BOOL IsDoppelGangerEnabled()
    {
        return m_bIsEnabled;
    }

  private:
    void LoadImages();
    void UnloadImages();

    int m_iEnteredMonsters;
    int m_iMaxMonsters;
    float m_fMonsterGauge;
    float m_fMonsterGaugeRcvd;

    std::map<WORD, PARTY_POSITION> m_PartyPositionMap;
    int m_iTime;
    BOOL m_bStopTimer;

    BOOL m_bIceWalkerEnabled;
    float m_fIceWalkerPositionRcvd;
    float m_fIceWalkerPosition;

    BOOL m_bIsEnabled;
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIDoppelGangerWindow : public CNewUIObj,
                                 protected SessionUiLegacyBindings,
                                 public INewUI3DRenderObj
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_DOPPELGANGERWINDOW_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, // Reference
        IMAGE_DOPPELGANGERWINDOW_TOP = CNewUIMyInventory::IMAGE_INVENTORY_BACK_TOP,
        IMAGE_DOPPELGANGERWINDOW_LEFT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_LEFT,
        IMAGE_DOPPELGANGERWINDOW_RIGHT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_RIGHT,
        IMAGE_DOPPELGANGERWINDOW_BOTTOM = CNewUIMyInventory::IMAGE_INVENTORY_BACK_BOTTOM,
        IMAGE_DOPPELGANGERWINDOW_BUTTON = CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY_VERY_SMALL,
        IMAGE_DOPPELGANGERWINDOW_LINE = CNewUIMyQuestInfoWindow::IMAGE_MYQUEST_LINE,
    };

  private:
    enum
    {
        INVENTORY_WIDTH = 190,
        INVENTORY_HEIGHT = 429,
    };

    CNewUIManager *m_pNewUIMng;
    CNewUI3DRenderMng *m_pNewUI3DRenderMng;
    POINT m_Pos;

    CNewUIButton m_BtnEnter;
    CNewUIButton m_BtnClose;

  public:
    explicit CNewUIDoppelGangerWindow(SessionKeeper &keeper);
    virtual ~CNewUIDoppelGangerWindow();

    bool Create(CNewUIManager *pNewUIMng, CNewUI3DRenderMng *pNewUI3DRenderMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    void Render3D();

    bool IsVisible() const;

    void OpeningProcess();
    void ClosingProcess();

    float GetLayerDepth(); //. 5.0f

    void SetRemainTime(int iTime);
    void LockEnterButton(BOOL bLock);

  private:
    void LoadImages();
    void UnloadImages();

    void RenderFrame();
    bool BtnProcess();
    using SessionUiLegacyBindings::RenderItem3D;
    void RenderItem3D();

    void InitButton(CNewUIButton *pNewUIButton, int iPos_x, int iPos_y, const wchar_t *pCaption);

    int m_iRemainTime;
    BOOL m_bIsEnterButtonLocked;
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIEnterDevilSquare : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        // Base Window (Reference)
        IMAGE_ENTERDS_BASE_WINDOW_BACK =
            CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, //. newui_msgbox_back.jpg
        IMAGE_ENTERDS_BASE_WINDOW_TOP =
            CNewUIMyInventory::IMAGE_INVENTORY_BACK_TOP, //. newui_item_back01.tga	(190,64)
        IMAGE_ENTERDS_BASE_WINDOW_LEFT =
            CNewUIMyInventory::IMAGE_INVENTORY_BACK_LEFT, //. newui_item_back02-l.tga	(21,320)
        IMAGE_ENTERDS_BASE_WINDOW_RIGHT =
            CNewUIMyInventory::IMAGE_INVENTORY_BACK_RIGHT, //. newui_item_back02-r.tga	(21,320)
        IMAGE_ENTERDS_BASE_WINDOW_BOTTOM =
            CNewUIMyInventory::IMAGE_INVENTORY_BACK_BOTTOM, //. newui_item_back03.tga	(190,45)
        IMAGE_ENTERDS_BASE_WINDOW_BTN_EXIT =
            CNewUIMyInventory::IMAGE_INVENTORY_EXIT_BTN, //. newui_exit_00.tga

        IMAGE_ENTERDS_BASE_WINDOW_BTN_ENTER =
            CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY_BIG //. newui_btn_empty_big.tga	(180, 87)
    };

  private:
    enum ENTERDS_WINDOW_SIZE
    {
        ENTERDS_BASE_WINDOW_WIDTH = 190,
        ENTERDS_BASE_WINDOW_HEIGHT = 429,
    };

    enum ENTERDS_ENTERBTN_STATE
    {
        ENTERBTN_DISABLE = 0,
        ENTERBTN_ENABLE,
    };

    enum
    {
        ENTER_BTN_VAL = 33, // ��ư ������ ����

        MAX_ENTER_GRADE = 7,
    };

  private:
    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;
    POINT m_BtnEnterStartPos;
    POINT m_EnterUITextPos;

    CNewUIButton m_BtnExit;                                      // Exit Button Class
    SessionBoundArray<CNewUIButton, MAX_ENTER_GRADE> m_BtnEnter; // Devil Square Enter Button

    int m_iDevilSquareLimitLevel[MAX_ENTER_GRADE * 2][2];
    int m_iNumActiveBtn;       // Ȱ��ȭ �Ǿ��ִ� ��ư
    DWORD m_dwBtnTextColor[2]; // 0 - Disabled, 1 - Enable

  public:
    explicit CNewUIEnterDevilSquare(SessionKeeper &keeper);
    virtual ~CNewUIEnterDevilSquare();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    bool BtnProcess();

    float GetLayerDepth(); //. 4.0f

    void OpenningProcess();
    void ClosingProcess();

  private:
    void SetBtnPos(int x, int y);
    void LoadImages();
    void UnloadImages();

    int CheckLimitLV(int iIndex);
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIExchangeLuckyCoin : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        // Base Window (Reference)
        IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_BACK =
            CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, //. newui_msgbox_back.jpg
        IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_TOP =
            CNewUIMyInventory::IMAGE_INVENTORY_BACK_TOP2, //. newui_item_back01.tga	(190,64)
        IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_LEFT =
            CNewUIMyInventory::IMAGE_INVENTORY_BACK_LEFT, //. newui_item_back02-l.tga	(21,320)
        IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_RIGHT =
            CNewUIMyInventory::IMAGE_INVENTORY_BACK_RIGHT, //. newui_item_back02-r.tga	(21,320)
        IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_BOTTOM =
            CNewUIMyInventory::IMAGE_INVENTORY_BACK_BOTTOM, //. newui_item_back03.tga	(190,45)
        IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_BTN_EXIT =
            CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY_SMALL, //. newui_btn_empty.tga (64, 87)

        IMAGE_EXCHANGE_LUCKYCOIN_EXCHANGE_BTN =
            CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY //. newui_btn_empty.tga	(108, 87)
    };

  private:
    enum ENTERBC_WINDOW_SIZE
    {
        EXCHANGE_LUCKYCOIN_WINDOW_WIDTH = 190,
        EXCHANGE_LUCKYCOIN_WINDOW_HEIGHT = 429,
    };

  public:
    enum
    {
        MAX_EXCHANGE_BTN = 3,
        EXCHANGE_BTN_VAL = 33,
        EXCHANGE_TEXT_VAL = 14,
    };

  private:
    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;
    POINT m_TextPos;
    POINT m_FirstBtnPos;

    SessionBoundArray<CNewUIButton, MAX_EXCHANGE_BTN> m_BtnExchange;
    CNewUIButton m_BtnExit;

  public:
    explicit CNewUIExchangeLuckyCoin(SessionKeeper &keeper);
    virtual ~CNewUIExchangeLuckyCoin();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    bool BtnProcess();

    float GetLayerDepth(); //. 4.2f

    void OpenningProcess();
    void ClosingProcess();

    void LockExchangeBtn();
    void UnLockExchangeBtn();

  private:
    void SetBtnPos(int x, int y);
    void RenderFrame();
    void RenderTexts();
    void RenderBtn();
    void LoadImages();
    void UnloadImages();
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIGateSwitchWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_GATESWITCHWINDOW_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, // Reference
        IMAGE_GATESWITCHWINDOW_TOP = CNewUIMyInventory::IMAGE_INVENTORY_BACK_TOP,
        IMAGE_GATESWITCHWINDOW_LEFT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_LEFT,
        IMAGE_GATESWITCHWINDOW_RIGHT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_RIGHT,
        IMAGE_GATESWITCHWINDOW_BOTTOM = CNewUIMyInventory::IMAGE_INVENTORY_BACK_BOTTOM,
        IMAGE_GATESWITCHWINDOW_EXIT_BTN = CNewUIMyInventory::IMAGE_INVENTORY_EXIT_BTN,
        IMAGE_GATESWITCHWINDOW_BUTTON = CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY,

        IMAGE_GATESWITCHWINDOW_TABLE_TOP_LEFT =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_TOP_LEFT, //. newui_item_table01(L).tga (14,14)
        IMAGE_GATESWITCHWINDOW_TABLE_TOP_RIGHT =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_TOP_RIGHT, //. newui_item_table01(R).tga (14,14)
        IMAGE_GATESWITCHWINDOW_TABLE_BOTTOM_LEFT =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_BOTTOM_LEFT, //. newui_item_table02(L).tga (14,14)
        IMAGE_GATESWITCHWINDOW_TABLE_BOTTOM_RIGHT = CNewUIInventoryCtrl::
            IMAGE_ITEM_TABLE_BOTTOM_RIGHT, //. newui_item_table02(R).tga (14,14)
        IMAGE_GATESWITCHWINDOW_TABLE_TOP_PIXEL =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_TOP_PIXEL, //. newui_item_table03(up).tga (1, 14)
        IMAGE_GATESWITCHWINDOW_TABLE_BOTTOM_PIXEL = CNewUIInventoryCtrl::
            IMAGE_ITEM_TABLE_BOTTOM_PIXEL, //. newui_item_table03(dw).tga (1,14)
        IMAGE_GATESWITCHWINDOW_TABLE_LEFT_PIXEL =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_LEFT_PIXEL, //. newui_item_table03(L).tga (14,1)
        IMAGE_GATESWITCHWINDOW_TABLE_RIGHT_PIXEL =
            CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_RIGHT_PIXEL, //. newui_item_table03(R).tga (14,1)
    };

  private:
    enum
    {
        INVENTORY_WIDTH = 190,
        INVENTORY_HEIGHT = 429,
    };

    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;

    CNewUIButton m_BtnExit;
    CNewUIButton m_BtnOpen; // ���� ��ư

  public:
    explicit CNewUIGateSwitchWindow(SessionKeeper &keeper);
    virtual ~CNewUIGateSwitchWindow();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    void OpeningProcess();
    void ClosingProcess();

    float GetLayerDepth(); //. 5.0f

  private:
    void LoadImages();
    void UnloadImages();

    void RenderFrame();
    bool BtnProcess();

    void InitButton(CNewUIButton *pNewUIButton, int iPos_x, int iPos_y, const wchar_t *pCaption);

    void RenderOutlineUpper(float fPos_x, float fPos_y, float fWidth, float fHeight);
    void RenderOutlineLower(float fPos_x, float fPos_y, float fWidth, float fHeight);
};
} // namespace SEASON3B

class SessionKeeper;

namespace SEASON3B
{
class CNewUIGoldBowmanLena;

class GoldBowmanLenaLegacyCalls : protected SessionUiLegacyBindings
{
  protected:
    GoldBowmanLenaLegacyCalls(SessionKeeper &keeper, CNewUIGoldBowmanLena &owner) noexcept;

    void RenderText(const wchar_t *text, int x, int y, int sx, int sy, DWORD color, DWORD backcolor,
                    int sort); // OMF-01924

  private:
    CNewUIGoldBowmanLena &owner_;
};

class CNewUIGoldBowmanLena : public CNewUIObj, protected GoldBowmanLenaLegacyCalls
{
    friend class GoldBowmanLenaLegacyCalls;

  public:
    enum IMAGE_LIST
    {
        IMAGE_GBL_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, // Reference
        IMAGE_GBL_TOP = CNewUIMyInventory::IMAGE_INVENTORY_BACK_TOP2,
        IMAGE_GBL_LEFT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_LEFT,
        IMAGE_GBL_RIGHT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_RIGHT,
        IMAGE_GBL_BOTTOM = CNewUIMyInventory::IMAGE_INVENTORY_BACK_BOTTOM,
        IMAGE_GBL_EXCHANGEBTN = CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY,
        IMAGE_GBL_BTN_SERIAL = CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY,
        IMAGE_GBL_BTN_EXIT = CNewUIMyInventory::IMAGE_INVENTORY_EXIT_BTN,
    };

  private:
    enum
    {
        INVENTORY_WIDTH = 190,
        INVENTORY_HEIGHT = 429,
    };

  public:
    CNewUIManager *m_pNewUIMng;
    CNewUIButton m_BtnRegister;
    CNewUIButton m_BtnExit;
    POINT m_Pos;

  public:
    explicit CNewUIGoldBowmanLena(SessionKeeper &keeper);
    virtual ~CNewUIGoldBowmanLena();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);
    const POINT &GetPos();

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    float GetLayerDepth(); // 3.4f

  public:
    void OpeningProcess();
    void ClosingProcess();

  private:
    CameraProjection &cameraProjection_;
    void LoadImages();
    void UnloadImages();
    void RenderFrame();
    void RenderTexts();
    void RenderText(const wchar_t *text, int x, int y, int sx, int sy, DWORD color, DWORD backcolor,
                    int sort);
    void RendeerButton();
    void Render3D();
};

inline void CNewUIGoldBowmanLena::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

inline const POINT &CNewUIGoldBowmanLena::GetPos()
{
    return m_Pos;
}
}; // namespace SEASON3B

namespace SEASON3B
{
class CNewUIGoldBowmanWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIGoldBowmanWindow(SessionKeeper &keeper);
    ~CNewUIGoldBowmanWindow() override;
    bool Create(CNewUIManager *manager, int x, int y);
    void Release();
    void SetPos(int x, int y)
    {
        position_ = {x, y};
    }
    const POINT &GetPos()
    {
        return position_;
    }
    bool UpdateMouseEvent() override;
    bool UpdateKeyEvent() override;
    bool Update() override;
    bool Render() override;
    float GetLayerDepth() override;
    void OpeningProcess();
    void ClosingProcess();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const;

  private:
    void StageContent();
    void SubmitSerial();
    CNewUIManager *manager_ = nullptr;
    SessionRenderUnit &renderer_;
    UI::Modern::PC::Inventory::RmlItemDialogPanel panel_;
    POINT position_{};
    std::string locale_;
    std::wstring gift_;
    bool visible_ = false;
};
} // namespace SEASON3B

class CDirection;

namespace SEASON3B
{
class CNewUIKanturu2ndEnterNpc : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_KANTURU2ND_TOP = CNewUIMessageBoxMng::IMAGE_MSGBOX_TOP,
        IMAGE_KANTURU2ND_MIDDLE = CNewUIMessageBoxMng::IMAGE_MSGBOX_MIDDLE,
        IMAGE_KANTURU2ND_BOTTOM = CNewUIMessageBoxMng::IMAGE_MSGBOX_BOTTOM,
        IMAGE_KANTURU2ND_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK,
        IMAGE_KANTURU2ND_BTN = CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY_VERY_SMALL,
    };

    static constexpr float KANTURU2ND_ENTER_WINDOW_WIDTH = 230.0f;
    static constexpr float KANTURU2ND_ENTER_WINDOW_HEIGHT = 267.0f;

    enum MSGBOX_TYPE
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

  public:
    explicit CNewUIKanturu2ndEnterNpc(SessionKeeper &keeper);
    virtual ~CNewUIKanturu2ndEnterNpc();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);

    float GetLayerDepth(); //. 10.1f

    void SetNpcObject(OBJECT *pObj);
    bool IsNpcAnimation();
    void SetNpcAnimation(bool bValue);
    bool IsEnterRequest();
    void SetEnterRequest(bool bValue);
    void CreateMessageBox(BYTE btResult);

    void ReceiveKanturu3rdInfo(BYTE btState, BYTE btDetailState, BYTE btEnter, BYTE btUserCount,
                               int iRemainTime);
    void ReceiveKanturu3rdEnter(BYTE btResult);
    void SendRequestKanturu3rdInfo();
    void SendRequestKanturu3rdEnter();

  private:
    void Initialize();

    bool BtnProcess();
    bool EnterKanturu();
    void StageContent();

  private:
    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;

    BYTE m_byState;

    bool m_bNpcAnimation;
    OBJECT *m_pNpcObject;

    bool m_bEnterRequest;

    DWORD m_dwRefreshTime;
    DWORD m_dwRefreshButtonGapTime;

    wchar_t m_strSubject[MAX_GLOBAL_TEXT_STRING];
    wchar_t m_strStateText[KANTURU2ND_STATETEXT_MAX][MAX_GLOBAL_TEXT_STRING];
    int m_iStateTextNum;

    SessionRenderUnit &renderer_;
    UI::Modern::PC::Inventory::RmlItemDialogPanel panel_;
    bool canEnter_ = false, refreshLocked_ = false, visible_ = false;
};

class CNewUIKanturuInfoWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_KANTURUINFO_WINDOW = BITMAP_KANTURU_INFO_BEGIN,
    };
    enum
    {
        KANTURUINFO_WINDOW_WIDTH = 99,
        KANTURUINFO_WINDOW_HEIGHT = 78,
    };

  public:
    explicit CNewUIKanturuInfoWindow(SessionKeeper &keeper);
    virtual ~CNewUIKanturuInfoWindow();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);

    float GetLayerDepth();    //. 1.92f
    float GetKeyEventOrder(); //. 9.1f

    void SetTime(int iTimeLimit);

  private:
  private:
    void StageContent();
    CDirection &g_Direction;
    SessionRenderUnit &renderer_;
    UI::Modern::PC::Inventory::RmlItemDialogPanel panel_;
    std::array<int, 4> infoState_{};
    std::string locale_;
    bool visible_ = false;
    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;

    int m_iSecond;
    DWORD m_dwSyncTime;
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIRegistrationLuckyCoin : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK,
        IMAGE_TOP = CNewUIMyInventory::IMAGE_INVENTORY_BACK_TOP2,
        IMAGE_LEFT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_LEFT,
        IMAGE_RIGHT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_RIGHT,
        IMAGE_BOTTOM = CNewUIMyInventory::IMAGE_INVENTORY_BACK_BOTTOM,
        IMAGE_CLOSE_REGIST = CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY_SMALL,
    };

  private:
    static constexpr float LUCKYCOIN_REG_WIDTH = 190.0f;
    static constexpr float LUCKYCOIN_REG_HEIGHT = 429.0f;

  public:
    explicit CNewUIRegistrationLuckyCoin(SessionKeeper &keeper);
    virtual ~CNewUIRegistrationLuckyCoin();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);

    void SetPos(int x, int y);
    const POINT &GetPos()
    {
        return m_Pos;
    }

    bool Render();
    bool Update();
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool BtnProcess();
    void SetBtnInfo();

    float GetLayerDepth()
    {
        return 4.2f;
    }

    const int &GetRegistCount()
    {
        return m_RegistCount;
    }

    void SetRegistCount(int nRegistCount)
    {
        m_RegistCount = nRegistCount;
    }

    bool GetItemRotation()
    {
        return m_ItemAngle;
    }
    void SetItemRotation(bool _bInput)
    {
        m_ItemAngle = _bInput;
    }

    void LockLuckyCoinRegBtn();
    void UnLockLuckyCoinRegBtn();

    void OpeningProcess();
    void ClosingProcess();

    void Release();

  private:
    void LoadImages();
    void UnloadImages();

    void RenderFrame();
    void RenderTexts();
    void RenderButtons();
    void RenderLuckyCoin();

  private:
    CameraProjection &cameraProjection_;
    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;
    ITEM *m_CoinItem;
    bool m_ItemAngle;
    float m_width, m_height;
    int m_RegistCount;
    CNewUIButton m_CloseButton;
    CNewUIButton m_RegistButton;
};
} // namespace SEASON3B

namespace TempleEntryDetail
{
using namespace SEASON3B;
#pragma pack(push)
#pragma pack()
inline const int EnterLevelCount = 5;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const int EnterMinLevel[EnterLevelCount] = {220, 271, 321, 351, 381};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const int EnterMaxLevel[EnterLevelCount] = {270, 320, 350, 380, 400};
#pragma pack(pop)

} // namespace TempleEntryDetail

namespace TemplePanelDetail
{
using namespace SEASON3B;
#pragma pack(push)
#pragma pack()
inline const int HolyItemNpc = 380;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const int AlliedNpc = 381;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const int IllusionNpc = 382;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const int AlliedHolyItemBoxNpc = 383;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const int IllusionHolyItemBoxNpc = 384;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const int AXIS_X = 0;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const int AXIS_Y = 1;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const float PROGRESSTIME = 10000.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const float posX[7] = {
    146.0f, 192.0f, 200.0f, 138.0f, 128.0f, 210.0f, 170.0f,
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const float posY[7] = {
    42.0f, 128.0f, 116.0f, 54.0f, 126.0f, 44.0f, 84.0f,
};
#pragma pack(pop)

} // namespace TemplePanelDetail

namespace TemplePanelDetail
{
using namespace SEASON3B;
float MiniMapPos(float pointX, float pointY, float scale, int aXis);
} // namespace TemplePanelDetail
