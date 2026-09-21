#include "ui/features/Activities/ActivitiesLogic.h"
#include "I18N/All.h"
#include "app/AppWindow.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/WorldSimulation.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
#include "render/ModelResources.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionRender.h"
#include "session/SessionUi.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Hud/HudRender.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"
#include "ui/session/UiSessionRender.h"

//  UIGuardsMan.cpp

CUIGuardsMan::CUIGuardsMan(SessionKeeper &keeper) : SessionLegacyCalls(keeper)
{
    m_dwRegMark = 0;
    m_eRegStatus = REG_STATUS_NONE;
}

CUIGuardsMan::~CUIGuardsMan()
{
}

bool CUIGuardsMan::IsSufficentDeclareLevel()
{
    if (Hero->GuildStatus != G_MASTER)
        return false;

    if (CharacterAttribute->Level >= BC_REQ_LEVEL)
    {
        return true;
    }
    else
    {
        return false;
    }
}

DWORD CUIGuardsMan::GetMyMarkCount()
{
    DWORD dwResult = 0;

    SEASON3B::CNewUIInventoryCtrl *pNewInventoryCtrl = g_pMyInventory->GetInventoryCtrl();
    ITEM *pItem = NULL;

    for (int i = 0; i < (int)pNewInventoryCtrl->GetNumberOfItems(); ++i)
    {
        pItem = pNewInventoryCtrl->GetItem(i);
        int nItemLevel = pItem->Level;
        if (pItem->Type == ITEM_POTION + 21 && nItemLevel == 3)
        {
            dwResult += pItem->Durability;
        }
    }

    return dwResult;
}

int CUIGuardsMan::GetMyMarkSlotIndex()
{
    SEASON3B::CNewUIInventoryCtrl *pNewInventoryCtrl = g_pMyInventory->GetInventoryCtrl();
    ITEM *pItem = NULL;

    for (int i = 0; i < (int)pNewInventoryCtrl->GetNumberOfItems(); ++i)
    {
        pItem = pNewInventoryCtrl->GetItem(i);
        int nItemLevel = pItem->Level;
        if (pItem->Type == ITEM_POTION + 21 && nItemLevel == 3)
        {
            return pItem->y * COLUMN_INVENTORY + pItem->x;
        }
    }

    return -1;
}

//  UIGuardsMan.cpp

SessionSenatusInfoPtr CreateSessionSenatusInfo()
{
    return SessionSenatusInfoPtr(new CSenatusInfo());
}

void SessionSenatusInfoDeleter::operator()(CSenatusInfo *value) const noexcept
{
    delete value;
}

CSenatusInfo::CSenatusInfo()
{
    m_iCurrGate = 0;
    m_iCurrStatue = 0;
    memset(m_GateInfo, 0, sizeof(m_GateInfo));
    memset(m_StatueInfo, 0, sizeof(m_StatueInfo));

    m_iChaosTaxRate = 0;
    m_iNormalTaxRate = 0;
    m_iRealTaxRateChaos = 0;
    m_iRealTaxRateStore = 0;
    m_i64CastleMoney = 0;
}

CSenatusInfo::~CSenatusInfo()
{
}

int CSenatusInfo::GetRepairCost(LPPMSG_NPCDBLIST pInfo)
{
    if (pInfo->iNpcNumber == GATENPC_NUMBER)
        return (pInfo->iNpcMaxHp - pInfo->iNpcHp) * 5 + pInfo->iNpcDfLevel * 1000000;
    else if (pInfo->iNpcNumber == STATUENPC_NUMBER)
        return (pInfo->iNpcMaxHp - pInfo->iNpcHp) * 3 + pInfo->iNpcDfLevel * 1000000;
    else
        return -1;
}

int CSenatusInfo::GetHP(int nType, int nLevel)
{
    if (nType == GATENPC_NUMBER)
    {
        if (nLevel == 0)
            return GATELEVEL_HP_0;
        else if (nLevel == 1)
            return GATELEVEL_HP_1;
        else if (nLevel == 2)
            return GATELEVEL_HP_2;
        else
            return GATELEVEL_HP_3;
    }
    else if (nType == STATUENPC_NUMBER)
    {
        if (nLevel == 0)
            return STATUELEVEL_HP_0;
        else if (nLevel == 1)
            return STATUELEVEL_HP_1;
        else if (nLevel == 2)
            return STATUELEVEL_HP_2;
        else
            return STATUELEVEL_HP_3;
    }
    else
        return -1;
}

int CSenatusInfo::GetHPLevel(LPPMSG_NPCDBLIST pInfo)
{
    if (pInfo->iNpcNumber == GATENPC_NUMBER)
    {
        if (pInfo->iNpcMaxHp <= GATELEVEL_HP_0)
            return 0;
        else if (GATELEVEL_HP_0 < pInfo->iNpcMaxHp && pInfo->iNpcMaxHp <= GATELEVEL_HP_1)
            return 1;
        else if (GATELEVEL_HP_1 < pInfo->iNpcMaxHp && pInfo->iNpcMaxHp <= GATELEVEL_HP_2)
            return 2;
        else
            return 3;
    }
    else if (pInfo->iNpcNumber == STATUENPC_NUMBER)
    {
        if (pInfo->iNpcMaxHp <= STATUELEVEL_HP_0)
            return 0;
        else if (STATUELEVEL_HP_0 < pInfo->iNpcMaxHp && pInfo->iNpcMaxHp <= STATUELEVEL_HP_1)
            return 1;
        else if (STATUELEVEL_HP_1 < pInfo->iNpcMaxHp && pInfo->iNpcMaxHp <= STATUELEVEL_HP_2)
            return 2;
        else
            return 3;
    }
    else
        return -1;
}

int CSenatusInfo::GetNextAddHP(LPPMSG_NPCDBLIST pInfo)
{
    if (pInfo->iNpcNumber == GATENPC_NUMBER)
    {
        if (pInfo->iNpcMaxHp == GATELEVEL_HP_0)
            return GATELEVEL_HP_1 - GATELEVEL_HP_0;
        if (pInfo->iNpcMaxHp == GATELEVEL_HP_1)
            return GATELEVEL_HP_2 - GATELEVEL_HP_1;
        else if (pInfo->iNpcMaxHp == GATELEVEL_HP_2)
            return GATELEVEL_HP_3 - GATELEVEL_HP_2;
        else
            return 0;
    }
    else if (pInfo->iNpcNumber == STATUENPC_NUMBER)
    {
        if (pInfo->iNpcMaxHp == STATUELEVEL_HP_0)
            return STATUELEVEL_HP_1 - STATUELEVEL_HP_0;
        if (pInfo->iNpcMaxHp == STATUELEVEL_HP_1)
            return STATUELEVEL_HP_2 - STATUELEVEL_HP_1;
        else if (pInfo->iNpcMaxHp == STATUELEVEL_HP_2)
            return STATUELEVEL_HP_3 - STATUELEVEL_HP_2;
        else
            return 0;
    }
    else
        return -1;
}

int CSenatusInfo::GetDefense(int nType, int nLevel)
{
    if (nType == GATENPC_NUMBER)
    {
        if (nLevel == 0)
            return GATELEVEL_DEFENSE_0;
        else if (nLevel == 1)
            return GATELEVEL_DEFENSE_1;
        else if (nLevel == 2)
            return GATELEVEL_DEFENSE_2;
        else
            return GATELEVEL_DEFENSE_3;
    }
    else if (nType == STATUENPC_NUMBER)
    {
        if (nLevel == 0)
            return STATUELEVEL_DEFENSE_0;
        else if (nLevel == 1)
            return STATUELEVEL_DEFENSE_1;
        else if (nLevel == 2)
            return STATUELEVEL_DEFENSE_2;
        else
            return STATUELEVEL_DEFENSE_3;
    }
    else
        return -1;
}

int CSenatusInfo::GetDefenseLevel(LPPMSG_NPCDBLIST pInfo)
{
    if (pInfo->iNpcNumber == GATENPC_NUMBER)
        return pInfo->iNpcDfLevel;
    else if (pInfo->iNpcNumber == STATUENPC_NUMBER)
        return pInfo->iNpcDfLevel;
    else
        return -1;
}

int CSenatusInfo::GetNextAddDefense(LPPMSG_NPCDBLIST pInfo)
{
    if (pInfo->iNpcNumber == GATENPC_NUMBER)
    {
        if (pInfo->iNpcDfLevel == 0)
            return GATELEVEL_DEFENSE_1 - GATELEVEL_DEFENSE_0;
        if (pInfo->iNpcDfLevel == 1)
            return GATELEVEL_DEFENSE_2 - GATELEVEL_DEFENSE_1;
        else if (pInfo->iNpcDfLevel == 2)
            return GATELEVEL_DEFENSE_3 - GATELEVEL_DEFENSE_2;
        else
            return 0;
    }
    else if (pInfo->iNpcNumber == STATUENPC_NUMBER)
    {
        if (pInfo->iNpcDfLevel == 0)
            return STATUELEVEL_DEFENSE_1 - STATUELEVEL_DEFENSE_0;
        if (pInfo->iNpcDfLevel == 1)
            return STATUELEVEL_DEFENSE_2 - STATUELEVEL_DEFENSE_1;
        else if (pInfo->iNpcDfLevel == 2)
            return STATUELEVEL_DEFENSE_3 - STATUELEVEL_DEFENSE_2;
        else
            return 0;
    }
    else
        return -1;
}

int CSenatusInfo::GetRecover(int nType, int nLevel)
{
    if (nType == GATENPC_NUMBER)
    {
        return 0;
    }
    else if (nType == STATUENPC_NUMBER)
    {
        if (nLevel == 0)
            return STATUELEVEL_RECOVER_0;
        else if (nLevel == 1)
            return STATUELEVEL_RECOVER_1;
        else if (nLevel == 2)
            return STATUELEVEL_RECOVER_2;
        else
            return STATUELEVEL_RECOVER_3;
    }
    else
        return -1;
}

int CSenatusInfo::GetRecoverLevel(LPPMSG_NPCDBLIST pInfo)
{
    if (pInfo->iNpcNumber == GATENPC_NUMBER)
        return 0;
    else if (pInfo->iNpcNumber == STATUENPC_NUMBER)
        return pInfo->iNpcRgLevel;
    else
        return -1;
}

int CSenatusInfo::GetNextAddRecover(LPPMSG_NPCDBLIST pInfo)
{
    if (pInfo->iNpcNumber == GATENPC_NUMBER)
    {
        return 0;
    }
    else if (pInfo->iNpcNumber == STATUENPC_NUMBER)
    {
        if (pInfo->iNpcRgLevel == STATUELEVEL_RECOVER_0)
            return STATUELEVEL_RECOVER_1 - STATUELEVEL_RECOVER_0;
        if (pInfo->iNpcRgLevel == STATUELEVEL_RECOVER_1)
            return STATUELEVEL_RECOVER_2 - STATUELEVEL_RECOVER_1;
        else if (pInfo->iNpcRgLevel == STATUELEVEL_RECOVER_2)
            return STATUELEVEL_RECOVER_3 - STATUELEVEL_RECOVER_2;
        else
            return 0;
    }
    else
        return -1;
}

void CSenatusInfo::DoGateRepairAction()
{
}

void CSenatusInfo::DoGateUpgradeHPAction()
{
}

void CSenatusInfo::DoGateUpgradeDefenseAction()
{
}

void CSenatusInfo::DoStatueRepairAction()
{
}

void CSenatusInfo::DoStatueUpgradeHPAction()
{
}

void CSenatusInfo::DoStatueUpgradeDefenseAction()
{
}

void CSenatusInfo::DoStatueUpgradeRecoverAction()
{
}

void CSenatusInfo::DoApplyTaxAction()
{
}

void CSenatusInfo::DoWithdrawAction(DWORD dwMoney)
{
}

void CSenatusInfo::SetNPCInfo(LPPMSG_NPCDBLIST pInfo)
{
    if (pInfo->iNpcNumber == GATENPC_NUMBER)
        memcpy(&m_GateInfo[pInfo->iNpcIndex - 1], pInfo, sizeof(PMSG_NPCDBLIST));
    if (pInfo->iNpcNumber == STATUENPC_NUMBER)
        memcpy(&m_StatueInfo[pInfo->iNpcIndex - 1], pInfo, sizeof(PMSG_NPCDBLIST));
}

LPPMSG_NPCDBLIST CSenatusInfo::GetNPCInfo(int iNpcNumber, int iNpcIndex)
{
    if (iNpcNumber == GATENPC_NUMBER)
        return &m_GateInfo[iNpcIndex - 1];
    else if (iNpcNumber == STATUENPC_NUMBER)
        return &m_StatueInfo[iNpcIndex - 1];
    else
        return NULL;
}

void CSenatusInfo::BuyNewNPC(int iNpcNumber, int iNpcIndex)
{
    LPPMSG_NPCDBLIST pNPCInfo = GetNPCInfo(iNpcNumber, iNpcIndex);
    memset(pNPCInfo, 0, sizeof(PMSG_NPCDBLIST));

    pNPCInfo->btNpcLive = 1;
    pNPCInfo->iNpcDfLevel = 0;
    if (iNpcNumber == GATENPC_NUMBER) // gate
        pNPCInfo->iNpcHp = pNPCInfo->iNpcMaxHp = GATELEVEL_HP_0;
    else
        pNPCInfo->iNpcHp = pNPCInfo->iNpcMaxHp = STATUELEVEL_HP_0;
    pNPCInfo->iNpcNumber = iNpcNumber;
    pNPCInfo->iNpcIndex = iNpcIndex;
    pNPCInfo->iNpcDfLevel = 0;
    pNPCInfo->iNpcRgLevel = 0;
}

void CSenatusInfo::SetTaxInfo(LPPMSG_ANS_TAXMONEYINFO pInfo)
{
    m_iChaosTaxRate = m_iRealTaxRateChaos = pInfo->btTaxRateChaos;
    m_iNormalTaxRate = m_iRealTaxRateStore = pInfo->btTaxRateStore;
    BYTE *pMoney = (BYTE *)&m_i64CastleMoney;
    *pMoney++ = pInfo->btMoney8;
    *pMoney++ = pInfo->btMoney7;
    *pMoney++ = pInfo->btMoney6;
    *pMoney++ = pInfo->btMoney5;
    *pMoney++ = pInfo->btMoney4;
    *pMoney++ = pInfo->btMoney3;
    *pMoney++ = pInfo->btMoney2;
    *pMoney++ = pInfo->btMoney1;
}

void CSenatusInfo::ChangeTaxInfo(LPPMSG_ANS_TAXRATECHANGE pInfo)
{
    if (pInfo->btTaxType == 1)
    {
        m_iRealTaxRateChaos = (pInfo->btTaxRate1 << 24) | (pInfo->btTaxRate2 << 16) |
                              (pInfo->btTaxRate3 << 8) | (pInfo->btTaxRate4);
        m_iChaosTaxRate = m_iRealTaxRateChaos;
    }
    else if (pInfo->btTaxType == 2)
    {
        m_iRealTaxRateStore = (pInfo->btTaxRate1 << 24) | (pInfo->btTaxRate2 << 16) |
                              (pInfo->btTaxRate3 << 8) | (pInfo->btTaxRate4);
        m_iNormalTaxRate = m_iRealTaxRateStore;
    }
    else
        ;
}

void CSenatusInfo::ChangeCastleMoney(LPPMSG_ANS_MONEYDRAWOUT pInfo)
{
    BYTE *pMoney = (BYTE *)&m_i64CastleMoney;
    *pMoney++ = pInfo->btMoney8;
    *pMoney++ = pInfo->btMoney7;
    *pMoney++ = pInfo->btMoney6;
    *pMoney++ = pInfo->btMoney5;
    *pMoney++ = pInfo->btMoney4;
    *pMoney++ = pInfo->btMoney3;
    *pMoney++ = pInfo->btMoney2;
    *pMoney++ = pInfo->btMoney1;
}

void CSenatusInfo::PlusChaosTaxRate(int iValue)
{
    m_iChaosTaxRate += iValue;
    if (m_iChaosTaxRate > MAX_TAX_RATE)
        m_iChaosTaxRate = MAX_TAX_RATE;
    else if (m_iChaosTaxRate < MIN_TAX_RATE)
        m_iChaosTaxRate = MIN_TAX_RATE;
}

void CSenatusInfo::PlusNormalTaxRate(int iValue)
{
    m_iNormalTaxRate += iValue;
    if (m_iNormalTaxRate > MAX_NORMAL_TAX_RATE)
        m_iNormalTaxRate = MAX_NORMAL_TAX_RATE;
    else if (m_iNormalTaxRate < MIN_TAX_RATE)
        m_iNormalTaxRate = MIN_TAX_RATE;
}

void CSenatusInfo::RollbackTaxRates()
{
    m_iChaosTaxRate = m_iRealTaxRateChaos;
    m_iNormalTaxRate = m_iRealTaxRateStore;
}

using namespace SEASON3B;

// Construction/Destruction

CNewUICastleWindow::CNewUICastleWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_TabBtn(keeper), m_BtnExit(keeper), m_BtnBuy(keeper),
      m_BtnRepair(keeper), m_BtnUpgradeHP(keeper), m_BtnUpgradeDefense(keeper),
      m_BtnUpgradeRecover(keeper), m_BtnApplyTax(keeper), m_BtnWithdraw(keeper),
      m_BtnChaosTaxUp(keeper), m_BtnChaosTaxDn(keeper), m_BtnNPCTaxUp(keeper), m_BtnNPCTaxDn(keeper)
{
    m_pNewUIMng = NULL;
    m_Pos.x = m_Pos.y = 0;
    m_iNumCurOpenTab = TAB_GATE_MANAGING;
    m_iCurrMsgBoxRequest = CASTLE_MSGREQ_NULL;
}
CNewUICastleWindow::~CNewUICastleWindow()
{
    Release();
}

bool CNewUICastleWindow::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_SENATUS, this);

    SetPos(x, y);

    LoadImages();

    std::list<const wchar_t *const *> ltext;
    ltext.push_back(&I18N::Game::CastleGate);
    ltext.push_back(&I18N::Game::GuardianStatue);
    ltext.push_back(&I18N::Game::Tax);
    ltext.push_back(&I18N::Game::Store1640);

    m_TabBtn.CreateRadioGroup(4, IMAGE_CASTLEWINDOW_TAB_BTN);
    m_TabBtn.ChangeRadioText(ltext);
    m_TabBtn.ChangeRadioButtonInfo(true, m_Pos.x + 12.f, m_Pos.y + 32.f, 40, 22);
    m_TabBtn.ChangeFrame(m_iNumCurOpenTab);

    m_BtnExit.ChangeButtonImgState(true, IMAGE_CASTLEWINDOW_EXIT_BTN, false);
    m_BtnExit.ChangeButtonInfo(m_Pos.x + 13, m_Pos.y + 391, 36, 29);
    m_BtnExit.ChangeToolTipText(&I18N::Game::Close388, true);

    InitButton(&m_BtnBuy, m_Pos.x + INVENTORY_WIDTH / 2 - 27, m_Pos.y + 250, I18N::Game::Buy1124);
    InitButton(&m_BtnRepair, m_Pos.x + 110, m_Pos.y + 260, I18N::Game::Repair);
    InitButton(&m_BtnUpgradeHP, m_Pos.x + 110, m_Pos.y + 310, I18N::Game::Improve);
    InitButton(&m_BtnUpgradeDefense, m_Pos.x + 110, m_Pos.y + 334, I18N::Game::Improve);
    InitButton(&m_BtnUpgradeRecover, m_Pos.x + 110, m_Pos.y + 358, I18N::Game::Improve);
    InitButton(&m_BtnApplyTax, m_Pos.x + 120, m_Pos.y + 133, I18N::Game::Apply);
    InitButton(&m_BtnWithdraw, m_Pos.x + 120, m_Pos.y + 322, I18N::Game::Withdraw);

    m_BtnChaosTaxUp.ChangeButtonImgState(true, IMAGE_CASTLEWINDOW_SCROLL_UP_BTN, true);
    m_BtnChaosTaxDn.ChangeButtonImgState(true, IMAGE_CASTLEWINDOW_SCROLL_DOWN_BTN, true);
    m_BtnNPCTaxUp.ChangeButtonImgState(true, IMAGE_CASTLEWINDOW_SCROLL_UP_BTN, true);
    m_BtnNPCTaxDn.ChangeButtonImgState(true, IMAGE_CASTLEWINDOW_SCROLL_DOWN_BTN, true);
    m_BtnChaosTaxUp.ChangeButtonInfo(m_Pos.x + 158, m_Pos.y + 73, 15, 13);
    m_BtnChaosTaxDn.ChangeButtonInfo(m_Pos.x + 158, m_Pos.y + 86, 15, 13);
    m_BtnNPCTaxUp.ChangeButtonInfo(m_Pos.x + 158, m_Pos.y + 101, 15, 13);
    m_BtnNPCTaxDn.ChangeButtonInfo(m_Pos.x + 158, m_Pos.y + 114, 15, 13);

    Show(false);

    return true;
}

void CNewUICastleWindow::InitButton(CNewUIButton *pNewUIButton, int iPos_x, int iPos_y,
                                    const wchar_t *pCaption)
{
    pNewUIButton->ChangeText(pCaption);
    pNewUIButton->ChangeTextBackColor(RGBA(255, 255, 255, 0));
    pNewUIButton->ChangeButtonImgState(true, IMAGE_CASTLEWINDOW_BUTTON, true);
    pNewUIButton->ChangeButtonInfo(iPos_x, iPos_y, 53, 23);
    pNewUIButton->ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
    pNewUIButton->ChangeImgColor(BUTTON_STATE_DOWN, RGBA(255, 255, 255, 255));
}

void CNewUICastleWindow::Release()
{
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool CNewUICastleWindow::UpdateMouseEvent()
{
    switch (m_iNumCurOpenTab)
    {
    case TAB_GATE_MANAGING:
        UpdateGateManagingTab();
        break;
    case TAB_STATUE_MANAGING:
        UpdateStatueManagingTab();
        break;
    case TAB_TAX_MANAGING:
        UpdateTaxManagingTab();
        break;
    }

    if (true == BtnProcess())
        return false;

    if (CheckMouseIn(m_Pos.x, m_Pos.y, INVENTORY_WIDTH, INVENTORY_HEIGHT))
        return false;

    return true;
}

bool CNewUICastleWindow::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_SENATUS) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_SENATUS);
            PlayBuffer(SOUND_CLICK01);

            return false;
        }
    }
    return true;
}

bool CNewUICastleWindow::Update()
{
    if (IsVisible())
    {
        int iNumCurOpenTab = m_TabBtn.UpdateMouseEvent();

        if (iNumCurOpenTab == RADIOGROUPEVENT_NONE)
            return true;

        m_iNumCurOpenTab = iNumCurOpenTab;

        if (iNumCurOpenTab == TAB_CASTLE_MIX)
        {
            g_MixRecipeMgr.SetMixType(SEASON3A::MIXTYPE_CASTLE_SENIOR);
            //	 		g_pNewUISystem->Hide(SEASON3B::INTERFACE_SENATUS);
            g_pNewUISystem->Show(SEASON3B::INTERFACE_MIXINVENTORY);
        }
    }
    return true;
}

void CNewUICastleWindow::OpeningProcess()
{
    m_iNumCurOpenTab = TAB_GATE_MANAGING;
    m_TabBtn.ChangeFrame(TAB_GATE_MANAGING);

    g_SenatusInfo.SetCurrGate(0);
    g_SenatusInfo.SetCurrStatue(0);

    SocketClient->ToGameServer()->SendCastleSiegeGateListRequest();
    SocketClient->ToGameServer()->SendCastleSiegeStatueListRequest();
    SocketClient->ToGameServer()->SendCastleSiegeTaxInfoRequest();
}

void CNewUICastleWindow::ClosingProcess()
{
    SocketClient->ToGameServer()->SendCraftingDialogCloseRequest();
}

float CNewUICastleWindow::GetLayerDepth()
{
    return 5.0f;
}

bool CNewUICastleWindow::BtnProcess()
{
    // Top-right corner close "X" (shared frame): hides + swallows the click.
    g_pNewUISystem->HandleFrameCornerClose(m_Pos, SEASON3B::INTERFACE_SENATUS);

    if (m_BtnExit.UpdateMouseEvent() == true)
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_SENATUS);
        return true;
    }

    return false;
}

void CNewUICastleWindow::UpdateGateManagingTab()
{
    POINT ptOrigin = {m_Pos.x, m_Pos.y + 55 + 6 + 12};

    if (MouseLButtonPush)
    {
        if (CheckMouseIn(ptOrigin.x + 15, ptOrigin.y, 160.f, 165.f))
        {
            if (CheckMouseIn(ptOrigin.x + 82, ptOrigin.y + 35, 24, 24))
                g_SenatusInfo.SetCurrGate(0);
            else if (CheckMouseIn(ptOrigin.x + 64, ptOrigin.y + 83, 24, 24))
                g_SenatusInfo.SetCurrGate(1);
            else if (CheckMouseIn(ptOrigin.x + 100, ptOrigin.y + 83, 24, 24))
                g_SenatusInfo.SetCurrGate(2);
            else if (CheckMouseIn(ptOrigin.x + 48, ptOrigin.y + 135, 24, 24))
                g_SenatusInfo.SetCurrGate(3);
            else if (CheckMouseIn(ptOrigin.x + 82, ptOrigin.y + 135, 24, 24))
                g_SenatusInfo.SetCurrGate(4);
            else if (CheckMouseIn(ptOrigin.x + 116, ptOrigin.y + 135, 24, 24))
                g_SenatusInfo.SetCurrGate(5);
        }
    }

    SEASON3B::CNewUICommonMessageBox *pMsgBox = NULL;
    wchar_t szText[256] = {
        0,
    };
    if (m_BtnBuy.UpdateMouseEvent() == true)
    {
        SetCurrMsgBoxRequest(CASTLE_MSGREQ_BUY_GATE);
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CCastleMsgBoxLayout, SessionOrigin()), &pMsgBox);
        pMsgBox->AddMsg(I18N::Game::ToPurchaseSelectedCastleGate);
        mu_swprintf(szText, I18N::Game::DZenIsRequired,
                    g_SenatusInfo.GetRepairCost(&g_SenatusInfo.GetCurrGateInfo()));
        InsertComma(szText, g_SenatusInfo.GetRepairCost(&g_SenatusInfo.GetCurrGateInfo()));
        pMsgBox->AddMsg(szText);
        pMsgBox->AddMsg(I18N::Game::WouldYouLikeToPurchase);
    }
    else if (m_BtnRepair.UpdateMouseEvent() == true)
    {
        SetCurrMsgBoxRequest(CASTLE_MSGREQ_REPAIR_GATE);
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CCastleMsgBoxLayout, SessionOrigin()), &pMsgBox);
        pMsgBox->AddMsg(I18N::Game::ToRepairSelectedCastleGate);
        mu_swprintf(szText, I18N::Game::DZenIsRequired,
                    g_SenatusInfo.GetRepairCost(&g_SenatusInfo.GetCurrGateInfo()));
        InsertComma(szText, g_SenatusInfo.GetRepairCost(&g_SenatusInfo.GetCurrGateInfo()));
        pMsgBox->AddMsg(szText);
        pMsgBox->AddMsg(I18N::Game::WouldYouLikeToPurchase);
    }
    else if (m_BtnUpgradeHP.UpdateMouseEvent() == true)
    {
        SetCurrMsgBoxRequest(CASTLE_MSGREQ_UPGRADE_GATE_HP);
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CCastleMsgBoxLayout, SessionOrigin()), &pMsgBox);
        pMsgBox->AddMsg(I18N::Game::UpgradingTheDurabilityOfSelectedCastleGate);

        if (g_SenatusInfo.GetHPLevel(&g_SenatusInfo.GetCurrGateInfo()) == 0)
            mu_swprintf(szText, I18N::Game::DGuardianJewelAndDZenAreRequired, 2, 1000000);
        else if (g_SenatusInfo.GetHPLevel(&g_SenatusInfo.GetCurrGateInfo()) == 1)
            mu_swprintf(szText, I18N::Game::DGuardianJewelAndDZenAreRequired, 3, 1000000);
        else
            mu_swprintf(szText, I18N::Game::DGuardianJewelAndDZenAreRequired, 4, 1000000);
        InsertComma(szText, 1000000);
        pMsgBox->AddMsg(szText);
        pMsgBox->AddMsg(I18N::Game::WouldYouLikeToRepair);
    }
    else if (m_BtnUpgradeDefense.UpdateMouseEvent() == true)
    {
        SetCurrMsgBoxRequest(CASTLE_MSGREQ_UPGRADE_GATE_DEFENSE);
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CCastleMsgBoxLayout, SessionOrigin()), &pMsgBox);
        pMsgBox->AddMsg(I18N::Game::UpgradingTheDefensivePowerOfSelectedCastleGate);
        if (g_SenatusInfo.GetDefenseLevel(&g_SenatusInfo.GetCurrGateInfo()) == 0)
            mu_swprintf(szText, I18N::Game::DGuardianJewelAndDZenAreRequired, 2, 3000000);
        else if (g_SenatusInfo.GetDefenseLevel(&g_SenatusInfo.GetCurrGateInfo()) == 1)
            mu_swprintf(szText, I18N::Game::DGuardianJewelAndDZenAreRequired, 3, 3000000);
        else
            mu_swprintf(szText, I18N::Game::DGuardianJewelAndDZenAreRequired, 4, 3000000);
        InsertComma(szText, 3000000);
        pMsgBox->AddMsg(szText);
        pMsgBox->AddMsg(I18N::Game::WouldYouLikeToRepair);
    }
}

void CNewUICastleWindow::UpdateStatueManagingTab()
{
    POINT ptOrigin = {m_Pos.x, m_Pos.y + 55 + 6 + 12};

    if (MouseLButtonPush)
    {
        if (CheckMouseIn(ptOrigin.x + 15, ptOrigin.y, 160.f, 165.f))
        {
            if (CheckMouseIn(ptOrigin.x + 82, ptOrigin.y + 20, 24, 24))
                g_SenatusInfo.SetCurrStatue(0);
            else if (CheckMouseIn(ptOrigin.x + 82, ptOrigin.y + 65, 24, 24))
                g_SenatusInfo.SetCurrStatue(1);
            else if (CheckMouseIn(ptOrigin.x + 64, ptOrigin.y + 110, 24, 24))
                g_SenatusInfo.SetCurrStatue(2);
            else if (CheckMouseIn(ptOrigin.x + 100, ptOrigin.y + 110, 24, 24))
                g_SenatusInfo.SetCurrStatue(3);
        }
    }

    SEASON3B::CNewUICommonMessageBox *pMsgBox = NULL;
    wchar_t szText[256] = {
        0,
    };
    if (m_BtnBuy.UpdateMouseEvent() == true)
    {
        SetCurrMsgBoxRequest(CASTLE_MSGREQ_BUY_STATUE);
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CCastleMsgBoxLayout, SessionOrigin()), &pMsgBox);
        pMsgBox->AddMsg(I18N::Game::ToPurchaseSelectedStatue);
        mu_swprintf(szText, I18N::Game::DZenIsRequired,
                    g_SenatusInfo.GetRepairCost(&g_SenatusInfo.GetCurrStatueInfo()));
        InsertComma(szText, g_SenatusInfo.GetRepairCost(&g_SenatusInfo.GetCurrStatueInfo()));
        pMsgBox->AddMsg(szText);
        pMsgBox->AddMsg(I18N::Game::WouldYouLikeToPurchase);
    }
    else if (m_BtnRepair.UpdateMouseEvent() == true)
    {
        SetCurrMsgBoxRequest(CASTLE_MSGREQ_REPAIR_STATUE);
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CCastleMsgBoxLayout, SessionOrigin()), &pMsgBox);
        pMsgBox->AddMsg(I18N::Game::ToRepairSelectedStatue);
        mu_swprintf(szText, I18N::Game::DZenIsRequired,
                    g_SenatusInfo.GetRepairCost(&g_SenatusInfo.GetCurrStatueInfo()));
        InsertComma(szText, g_SenatusInfo.GetRepairCost(&g_SenatusInfo.GetCurrStatueInfo()));
        pMsgBox->AddMsg(szText);
        pMsgBox->AddMsg(I18N::Game::WouldYouLikeToRepair);
    }
    else if (m_BtnUpgradeHP.UpdateMouseEvent() == true)
    {
        SetCurrMsgBoxRequest(CASTLE_MSGREQ_UPGRADE_STATUE_HP);
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CCastleMsgBoxLayout, SessionOrigin()), &pMsgBox);
        pMsgBox->AddMsg(I18N::Game::UpgradingDurabilityOfSelectedCastleGate);

        if (g_SenatusInfo.GetHPLevel(&g_SenatusInfo.GetCurrStatueInfo()) == 0)
            mu_swprintf(szText, I18N::Game::DGuardianJewelAndDZenAreRequired, 3, 1000000);
        else if (g_SenatusInfo.GetHPLevel(&g_SenatusInfo.GetCurrStatueInfo()) == 1)
            mu_swprintf(szText, I18N::Game::DGuardianJewelAndDZenAreRequired, 5, 1000000);
        else
            mu_swprintf(szText, I18N::Game::DGuardianJewelAndDZenAreRequired, 7, 1000000);
        InsertComma(szText, 1000000);
        pMsgBox->AddMsg(szText);
        pMsgBox->AddMsg(I18N::Game::WouldYouLikeToRepair);
    }
    else if (m_BtnUpgradeDefense.UpdateMouseEvent() == true)
    {
        SetCurrMsgBoxRequest(CASTLE_MSGREQ_UPGRADE_STATUE_DEFENSE);
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CCastleMsgBoxLayout, SessionOrigin()), &pMsgBox);
        pMsgBox->AddMsg(I18N::Game::UpgradingDefensivePowerOfSelectedStatue);

        if (g_SenatusInfo.GetDefenseLevel(&g_SenatusInfo.GetCurrStatueInfo()) == 0)
            mu_swprintf(szText, I18N::Game::DGuardianJewelAndDZenAreRequired, 3, 3000000);
        else if (g_SenatusInfo.GetDefenseLevel(&g_SenatusInfo.GetCurrStatueInfo()) == 1)
            mu_swprintf(szText, I18N::Game::DGuardianJewelAndDZenAreRequired, 5, 3000000);
        else
            mu_swprintf(szText, I18N::Game::DGuardianJewelAndDZenAreRequired, 7, 3000000);
        InsertComma(szText, 3000000);
        pMsgBox->AddMsg(szText);
        pMsgBox->AddMsg(I18N::Game::WouldYouLikeToRepair);
    }
    else if (m_BtnUpgradeRecover.UpdateMouseEvent() == true)
    {
        SetCurrMsgBoxRequest(CASTLE_MSGREQ_UPGRADE_STATUE_RECOVER);
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CCastleMsgBoxLayout, SessionOrigin()), &pMsgBox);
        pMsgBox->AddMsg(I18N::Game::UpgradingRecoveryPowerOfSelectedStatue);

        if (g_SenatusInfo.GetRecoverLevel(&g_SenatusInfo.GetCurrStatueInfo()) == 0)
            mu_swprintf(szText, I18N::Game::DGuardianJewelAndDZenAreRequired, 3, 5000000);
        else if (g_SenatusInfo.GetRecoverLevel(&g_SenatusInfo.GetCurrStatueInfo()) == 1)
            mu_swprintf(szText, I18N::Game::DGuardianJewelAndDZenAreRequired, 5, 5000000);
        else
            mu_swprintf(szText, I18N::Game::DGuardianJewelAndDZenAreRequired, 7, 5000000);
        InsertComma(szText, 5000000);
        pMsgBox->AddMsg(szText);
        pMsgBox->AddMsg(I18N::Game::WouldYouLikeToRepair);
    }
}

void CNewUICastleWindow::UpdateTaxManagingTab()
{
    SEASON3B::CNewUICommonMessageBox *pMsgBox = NULL;
    wchar_t szText[256] = {
        0,
    };
    if (m_BtnApplyTax.UpdateMouseEvent() == true)
    {
        SetCurrMsgBoxRequest(CASTLE_MSGREQ_APPLY_TAX);
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CCastleMsgBoxLayout, SessionOrigin()), &pMsgBox);
        mu_swprintf(szText, I18N::Game::ChaosCombinationGoblinTaxRateD,
                    g_SenatusInfo.GetChaosTaxRate());
        pMsgBox->AddMsg(szText);
        mu_swprintf(szText, I18N::Game::VariousNPCTaxRateD, g_SenatusInfo.GetNormalTaxRate());
        pMsgBox->AddMsg(szText);
        pMsgBox->AddMsg(I18N::Game::Apply1568);
    }
    else if (m_BtnWithdraw.UpdateMouseEvent() == true)
    {
        SetCurrMsgBoxRequest(CASTLE_MSGREQ_WITHDRAW);
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CCastleWithdrawMsgBoxLayout, SessionOrigin()));
    }
    else if (m_BtnChaosTaxUp.UpdateMouseEvent() == true)
    {
        g_SenatusInfo.PlusChaosTaxRate(1);
    }
    else if (m_BtnChaosTaxDn.UpdateMouseEvent() == true)
    {
        g_SenatusInfo.PlusChaosTaxRate(-1);
    }
    else if (m_BtnNPCTaxUp.UpdateMouseEvent() == true)
    {
        g_SenatusInfo.PlusNormalTaxRate(1);
    }
    else if (m_BtnNPCTaxDn.UpdateMouseEvent() == true)
    {
        g_SenatusInfo.PlusNormalTaxRate(-1);
    }
}

void CNewUICastleWindow::InsertComma(wchar_t *pszText, DWORD dwNumber)
{
    wchar_t szNumber[32];
    mu_swprintf(szNumber, L"%d", dwNumber);

    wchar_t szTemp[256];
    wcscpy_s(szTemp, 256, pszText);
    wchar_t *pszTextBegin = szTemp;
    wchar_t *pszTextFound = wcsstr(szTemp, szNumber);
    wchar_t *pszTextNext = pszTextFound + wcslen(szNumber);
    *pszTextFound = '\0';
    ConvertGold(dwNumber, szNumber);

    mu_swprintf(pszText, L"%ls%ls%ls", pszTextBegin, szNumber, pszTextNext);
}

void CNewUICastleWindow::InsertComma64(wchar_t *pszText, __int64 iNumber)
{
    wchar_t szNumber[32];
    mu_swprintf(szNumber, L"%I64d", iNumber);

    wchar_t szTemp[256];
    wcscpy_s(szTemp, 256, pszText);
    wchar_t *pszTextBegin = szTemp;
    wchar_t *pszTextFound = wcsstr(szTemp, szNumber);
    wchar_t *pszTextNext = pszTextFound + wcslen(szNumber);
    *pszTextFound = '\0';
    ConvertGold64(iNumber, szNumber);

    mu_swprintf(pszText, L"%ls%ls%ls", pszTextBegin, szNumber, pszTextNext);
}

CNewUIDuelWatchMainFrameWindow::CNewUIDuelWatchMainFrameWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_BtnExit(keeper)
{
    m_pNewUIMng = NULL;

    m_bHasHPReceived = FALSE;
    m_fPrevHPRate1 = 0;
    m_fPrevHPRate2 = 0;
    m_fPrevSDRate1 = 0;
    m_fPrevSDRate2 = 0;
    m_fLastHPRate1 = 0;
    m_fLastHPRate2 = 0;
    m_fLastSDRate1 = 0;
    m_fLastSDRate2 = 0;
    m_fReceivedHPRate1 = 0;
    m_fReceivedHPRate2 = 0;
    m_fReceivedSDRate1 = 0;
    m_fReceivedSDRate2 = 0;
}

CNewUIDuelWatchMainFrameWindow::~CNewUIDuelWatchMainFrameWindow()
{
    Release();
}

bool CNewUIDuelWatchMainFrameWindow::Create(CNewUIManager *pNewUIMng,
                                            CNewUI3DRenderMng *pNewUI3DRenderMng)
{
    if (NULL == pNewUIMng || NULL == pNewUI3DRenderMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_DUELWATCH_MAINFRAME, this);

    m_pNewUI3DRenderMng = pNewUI3DRenderMng;
    m_pNewUI3DRenderMng->Add3DRenderObj(this, ITEMHOTKEYNUMBER_CAMERA_Z_ORDER);

    LoadImages();

    m_BtnExit.SetPos(REFERENCE_WIDTH - 36, REFERENCE_HEIGHT - 29);
    m_BtnExit.ChangeButtonImgState(true, IMAGE_INVENTORY_EXIT_BTN, false);
    m_BtnExit.ChangeButtonInfo(REFERENCE_WIDTH - 36, REFERENCE_HEIGHT - 29, 36, 29);
    m_BtnExit.ChangeToolTipText(&I18N::Game::DuelFinished, true);

    Show(false);

    return true;
}

void CNewUIDuelWatchMainFrameWindow::Release()
{
    UnloadImages();

    if (m_pNewUI3DRenderMng)
    {
        m_pNewUI3DRenderMng->Remove3DRenderObj(this);
        m_pNewUI3DRenderMng = NULL;
    }

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool CNewUIDuelWatchMainFrameWindow::UpdateMouseEvent()
{
    if (true == BtnProcess())
        return false;
    return true;
}

bool CNewUIDuelWatchMainFrameWindow::UpdateKeyEvent()
{
    return true;
}

namespace
{
float GaugeStep(float received, float current, float pixels, float factor)
{
    constexpr float DamageGain = 5.f, MinimumPixels = 2.f;
    return (static_cast<int>(std::abs(received - current) * DamageGain) + MinimumPixels) / pixels *
           factor;
}
void AdvanceGauge(float &value, float target, float step)
{
    value += std::clamp(target - value, -step, step);
}
} // namespace

void CNewUIDuelWatchMainFrameWindow::UpdateGaugeHistory()
{
    if (m_bHasHPReceived == FALSE || g_DuelMgr.GetFighterRegenerated())
    {
        m_bHasHPReceived = TRUE;
        g_DuelMgr.SetFighterRegenerated(FALSE);
        m_fPrevHPRate1 = m_fLastHPRate1 = m_fReceivedHPRate1 = g_DuelMgr.GetHP(DUEL_HERO);
        m_fPrevHPRate2 = m_fLastHPRate2 = m_fReceivedHPRate2 = g_DuelMgr.GetHP(DUEL_ENEMY);
        m_fPrevSDRate1 = m_fLastSDRate1 = m_fReceivedSDRate1 = g_DuelMgr.GetSD(DUEL_HERO);
        m_fPrevSDRate2 = m_fLastSDRate2 = m_fReceivedSDRate2 = g_DuelMgr.GetSD(DUEL_ENEMY);
    }

    if (m_fLastHPRate1 != g_DuelMgr.GetHP(DUEL_HERO) ||
        m_fLastHPRate2 != g_DuelMgr.GetHP(DUEL_ENEMY) ||
        m_fLastSDRate1 != g_DuelMgr.GetSD(DUEL_HERO) ||
        m_fLastSDRate2 != g_DuelMgr.GetSD(DUEL_ENEMY))
    {
        m_fLastHPRate1 = g_DuelMgr.GetHP(DUEL_HERO);
        m_fLastHPRate2 = g_DuelMgr.GetHP(DUEL_ENEMY);
        m_fLastSDRate1 = g_DuelMgr.GetSD(DUEL_HERO);
        m_fLastSDRate2 = g_DuelMgr.GetSD(DUEL_ENEMY);
        m_fReceivedHPRate1 = m_fPrevHPRate1;
        m_fReceivedHPRate2 = m_fPrevHPRate2;
        m_fReceivedSDRate1 = m_fPrevSDRate1;
        m_fReceivedSDRate2 = m_fPrevSDRate2;
    }

    constexpr float HpPixels = 236.f, SdPixels = 154.f;
    AdvanceGauge(m_fPrevHPRate1, m_fLastHPRate1,
                 GaugeStep(m_fReceivedHPRate1, m_fLastHPRate1, HpPixels, FPS_ANIMATION_FACTOR));
    AdvanceGauge(m_fPrevHPRate2, m_fLastHPRate2,
                 GaugeStep(m_fReceivedHPRate2, m_fLastHPRate2, HpPixels, FPS_ANIMATION_FACTOR));
    AdvanceGauge(m_fPrevSDRate1, m_fLastSDRate1,
                 GaugeStep(m_fReceivedSDRate1, m_fLastSDRate1, SdPixels, FPS_ANIMATION_FACTOR));
    AdvanceGauge(m_fPrevSDRate2, m_fLastSDRate2,
                 GaugeStep(m_fReceivedSDRate2, m_fLastSDRate1, SdPixels, FPS_ANIMATION_FACTOR));
}

bool CNewUIDuelWatchMainFrameWindow::Update()
{
    if (g_DuelMgr.GetCurrentChannel() != -1)
        UpdateGaugeHistory();
    return true;
}

bool CNewUIDuelWatchMainFrameWindow::IsVisible() const
{
    return CNewUIObj::IsVisible();
}

void CNewUIDuelWatchMainFrameWindow::OpeningProcess()
{
    m_bHasHPReceived = FALSE;
}

void CNewUIDuelWatchMainFrameWindow::ClosingProcess()
{
    m_bHasHPReceived = FALSE;
}

float CNewUIDuelWatchMainFrameWindow::GetLayerDepth()
{
    return 5.0f;
}

bool CNewUIDuelWatchMainFrameWindow::BtnProcess()
{
    if (m_BtnExit.UpdateMouseEvent() == true)
    {
        if (g_DuelMgr.GetCurrentChannel() >= 0)
        {
            SocketClient->ToGameServer()
                ->SendDuelChannelQuitRequest(); //g_DuelMgr.GetCurrentChannel());
        }
        return true;
    }

    return false;
}

CNewUIDuelWatchUserListWindow::CNewUIDuelWatchUserListWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper)
{
}

CNewUIDuelWatchUserListWindow::~CNewUIDuelWatchUserListWindow()
{
    Release();
}

bool CNewUIDuelWatchUserListWindow::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_DUELWATCH_USERLIST, this);

    SetPos(x, y);

    LoadImages();

    Show(false);

    return true;
}

void CNewUIDuelWatchUserListWindow::Release()
{
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool CNewUIDuelWatchUserListWindow::UpdateMouseEvent()
{
    if (true == BtnProcess())
        return false;

    POINT ptSize = {57, 17};
    POINT ptOrigin = {m_Pos.x, m_Pos.y - (ptSize.y + 1) * g_DuelMgr.GetDuelWatchUserCount()};

    if (CheckMouseIn(ptOrigin.x, ptOrigin.y, ptSize.x,
                     (ptSize.y + 1) * g_DuelMgr.GetDuelWatchUserCount() + 10))
        return false;

    return true;
}

bool CNewUIDuelWatchUserListWindow::UpdateKeyEvent()
{
    return true;
}

bool CNewUIDuelWatchUserListWindow::Update()
{
    // 	if(IsVisible())
    // 	{
    // 	}
    return true;
}

void CNewUIDuelWatchUserListWindow::OpeningProcess()
{
}

void CNewUIDuelWatchUserListWindow::ClosingProcess()
{
}

float CNewUIDuelWatchUserListWindow::GetLayerDepth()
{
    return 5.0f;
}

bool CNewUIDuelWatchUserListWindow::BtnProcess()
{
    return false;
}

CNewUIDuelWatchWindow::CNewUIDuelWatchWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()), panel_(keeper)
{
}
CNewUIDuelWatchWindow::~CNewUIDuelWatchWindow()
{
    Release();
}
bool CNewUIDuelWatchWindow::Create(CNewUIManager *manager, int, int)
{
    if (!manager)
        return false;
    manager_ = manager;
    manager_->AddUIObj(INTERFACE_DUELWATCH, this);
    Show(false);
    return true;
}
void CNewUIDuelWatchWindow::Release()
{
    panel_.Release();
    content_ = {};
    locale_.clear();
    players_ = {};
    active_ = {};
    visible_ = false;
    if (!manager_)
        return;
    manager_->RemoveUIObj(this);
    manager_ = nullptr;
}

bool CNewUIDuelWatchWindow::Update()
{
    const auto changes = panel_.TakeChanges();
    if (changes.focus && manager_)
        manager_->BringToFront(this);
    if (IsVisible() && changes.close)
        g_pNewUISystem->Hide(INTERFACE_DUELWATCH);
    if (IsVisible() && changes.channel >= 0 && g_DuelMgr.IsDuelChannelEnabled(changes.channel) &&
        g_DuelMgr.IsDuelChannelJoinable(changes.channel))
        SocketClient->ToGameServer()->SendDuelChannelJoinRequest(
            static_cast<BYTE>(changes.channel));
    if (IsVisible())
        StageContent();
    visible_ = IsVisible();
    return true;
}
bool CNewUIDuelWatchWindow::UpdateMouseEvent()
{
    return !panel_.ContainsReferencePointer(MouseX, MouseY);
}
bool CNewUIDuelWatchWindow::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_DUELWATCH) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_DUELWATCH);
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }
    return true;
}

bool CNewUIDuelWatchWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessInput(event);
}
void CNewUIDuelWatchWindow::OpeningProcess()
{
    visible_ = true;
    StageContent();
}
void CNewUIDuelWatchWindow::ClosingProcess()
{
    visible_ = false;
    SocketClient->ToGameServer()->SendCloseNpcRequest();
}
float CNewUIDuelWatchWindow::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

SEASON3B::CNewUIDuelWindow::CNewUIDuelWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper)
{
    m_pNewUIMng = NULL;
    m_Pos.x = m_Pos.y = 0;
}

SEASON3B::CNewUIDuelWindow::~CNewUIDuelWindow()
{
    Release();
}

bool SEASON3B::CNewUIDuelWindow::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_DUEL_WINDOW, this);

    SetPos(x, y);

    LoadImages();

    Show(false);

    return true;
}

void SEASON3B::CNewUIDuelWindow::Release()
{
    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool SEASON3B::CNewUIDuelWindow::UpdateMouseEvent()
{
    return true;
}

bool SEASON3B::CNewUIDuelWindow::UpdateKeyEvent()
{
    return true;
}

bool SEASON3B::CNewUIDuelWindow::Update()
{
    return true;
}

float SEASON3B::CNewUIDuelWindow::GetLayerDepth()
{
    return 1.1f;
}

CNewUIGuardWindow::CNewUIGuardWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_DeclareGuildListBox(keeper), m_GuildListBox(keeper),
      m_TabBtn(keeper), m_BtnExit(keeper), m_BtnProclaim(keeper), m_BtnRegister(keeper),
      m_BtnGiveUp(keeper)
{
    m_pNewUIMng = NULL;
    m_Pos.x = m_Pos.y = 0;
    m_iNumCurOpenTab = TAB_SIEGE_INFO;
}

CNewUIGuardWindow::~CNewUIGuardWindow()
{
    Release();
}

bool CNewUIGuardWindow::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_GUARDSMAN, this);

    SetPos(x, y);

    LoadImages();

    std::list<const wchar_t *const *> ltext;
    ltext.push_back(&I18N::Game::Status);
    ltext.push_back(&I18N::Game::Register);
    ltext.push_back(&I18N::Game::List);

    m_TabBtn.CreateRadioGroup(3, IMAGE_GUARDWINDOW_TAB_BTN);
    m_TabBtn.ChangeRadioText(ltext);
    m_TabBtn.ChangeRadioButtonInfo(true, m_Pos.x + 12.f, m_Pos.y + 84.f, 56, 22);
    m_TabBtn.ChangeFrame(m_iNumCurOpenTab);

    m_BtnExit.ChangeButtonImgState(true, IMAGE_GUARDWINDOW_EXIT_BTN, false);
    m_BtnExit.ChangeButtonInfo(m_Pos.x + 13, m_Pos.y + 391, 36, 29);
    m_BtnExit.ChangeToolTipText(&I18N::Game::Close388, true);

    InitButton(&m_BtnProclaim, m_Pos.x + INVENTORY_WIDTH / 2 - 27, m_Pos.y + 120,
               &I18N::Game::Announce);
    InitButton(&m_BtnRegister, m_Pos.x + INVENTORY_WIDTH / 2 - 27, m_Pos.y + 200,
               &I18N::Game::Register);
    InitButton(&m_BtnGiveUp, m_Pos.x + INVENTORY_WIDTH / 2 - 27, m_Pos.y + 370,
               &I18N::Game::AbandonCastleSiege);

    Show(false);

    return true;
}

void CNewUIGuardWindow::InitButton(CNewUIButton *pNewUIButton, int iPos_x, int iPos_y,
                                   const wchar_t *const *pCaptionSlot)
{
    pNewUIButton->ChangeText(pCaptionSlot);
    pNewUIButton->ChangeTextBackColor(RGBA(255, 255, 255, 0));
    pNewUIButton->ChangeButtonImgState(true, IMAGE_GUARDWINDOW_BUTTON, true);
    pNewUIButton->ChangeButtonInfo(iPos_x, iPos_y, 53, 23);
    pNewUIButton->ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
    pNewUIButton->ChangeImgColor(BUTTON_STATE_DOWN, RGBA(255, 255, 255, 255));
}

void CNewUIGuardWindow::Release()
{
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool CNewUIGuardWindow::UpdateMouseEvent()
{
    switch (m_iNumCurOpenTab)
    {
    case TAB_SIEGE_INFO:
        UpdateSeigeInfoTab();
        break;
    case TAB_REGISTER:
        UpdateRegisterTab();
        break;
    case TAB_REGISTER_INFO:
        UpdateRegisterInfoTab();
        break;
    }

    if (true == BtnProcess())
        return false;

    if (CheckMouseIn(m_Pos.x, m_Pos.y, INVENTORY_WIDTH, INVENTORY_HEIGHT))
        return false;

    return true;
}

bool CNewUIGuardWindow::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_GUARDSMAN) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_GUARDSMAN);
            PlayBuffer(SOUND_CLICK01);

            return false;
        }
    }
    return true;
}

bool CNewUIGuardWindow::Update()
{
    if (IsVisible())
    {
        int iNumCurOpenTab = m_TabBtn.UpdateMouseEvent();

        if (iNumCurOpenTab == RADIOGROUPEVENT_NONE)
            return true;

        m_iNumCurOpenTab = iNumCurOpenTab;

        if (iNumCurOpenTab == TAB_REGISTER_INFO)
        {
            if (m_eTimeType == CASTLESIEGE_STATE_REGSIEGE ||
                m_eTimeType == CASTLESIEGE_STATE_REGMARK)
            {
                SocketClient->ToGameServer()->SendCastleSiegeRegisteredGuildsListRequest();
            }
            else if (m_eTimeType == CASTLESIEGE_STATE_NOTIFY ||
                     m_eTimeType == CASTLESIEGE_STATE_READYSIEGE)
            {
                SocketClient->ToGameServer()->SendCastleOwnerListRequest();
            }
        }
    }
    return true;
}

void CNewUIGuardWindow::OpeningProcess()
{
    m_iNumCurOpenTab = TAB_SIEGE_INFO;
    m_TabBtn.ChangeFrame(TAB_SIEGE_INFO);

    SocketClient->ToGameServer()->SendCastleSiegeRegistrationStateRequest();
}

void CNewUIGuardWindow::ClosingProcess()
{
    m_DeclareGuildListBox.Clear();
    m_GuildListBox.Clear();

    SocketClient->ToGameServer()->SendCloseNpcRequest();
}

float CNewUIGuardWindow::GetLayerDepth()
{
    return 5.0f;
}

bool CNewUIGuardWindow::BtnProcess()
{
    // Top-right corner close "X" (shared frame): hides + swallows the click.
    g_pNewUISystem->HandleFrameCornerClose(m_Pos, SEASON3B::INTERFACE_GUARDSMAN);

    if (m_BtnExit.UpdateMouseEvent() == true)
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_GUARDSMAN);
        return true;
    }

    return false;
}

void CNewUIGuardWindow::UpdateSeigeInfoTab()
{
}

void CNewUIGuardWindow::UpdateRegisterTab()
{
    switch (m_eTimeType)
    {
    case CASTLESIEGE_STATE_REGSIEGE:
        if (m_BtnProclaim.UpdateMouseEvent() == true)
        {
            if (g_GuardsMan.IsSufficentDeclareLevel())
            {
                SocketClient->ToGameServer()->SendCastleSiegeRegistrationRequest();
            }
            else
            {
                SEASON3B::CreateMessageBox(
                    MSGBOX_LAYOUT_CLASS(SEASON3B::CSiegeLevelMsgBoxLayout, SessionOrigin()));
            }
        }
        break;
    case CASTLESIEGE_STATE_REGMARK:
        if (m_BtnRegister.UpdateMouseEvent() == true)
        {
            int nMarkSlot = g_GuardsMan.GetMyMarkSlotIndex();
            if (nMarkSlot != -1)
            {
                SocketClient->ToGameServer()->SendCastleSiegeMarkRegistration(nMarkSlot);
            }
        }
        break;
    }
}

void CNewUIGuardWindow::UpdateRegisterInfoTab()
{
    if (m_eTimeType == CASTLESIEGE_STATE_REGSIEGE || m_eTimeType == CASTLESIEGE_STATE_REGMARK)
    {
        //g_dwActiveUIID = m_DeclareGuildListBox.GetUIID();
        m_DeclareGuildListBox.DoAction();
        //g_dwActiveUIID = 0;
        if (PressKey(VK_PRIOR))
            m_DeclareGuildListBox.Scrolling(-1 * m_DeclareGuildListBox.GetBoxSize());
        if (PressKey(VK_NEXT))
            m_DeclareGuildListBox.Scrolling(m_DeclareGuildListBox.GetBoxSize());
    }
    else if (m_eTimeType == CASTLESIEGE_STATE_NOTIFY || m_eTimeType == CASTLESIEGE_STATE_READYSIEGE)
    {
        //g_dwActiveUIID = m_DeclareGuildListBox.GetUIID();
        m_GuildListBox.DoAction();
        //g_dwActiveUIID = 0;
        if (PressKey(VK_PRIOR))
            m_GuildListBox.Scrolling(-1 * m_GuildListBox.GetBoxSize());
        if (PressKey(VK_NEXT))
            m_GuildListBox.Scrolling(m_GuildListBox.GetBoxSize());
    }

    if (g_GuardsMan.HasRegistered() && CASTLESIEGE_STATE_REGSIEGE <= m_eTimeType &&
        m_eTimeType <= CASTLESIEGE_STATE_REGMARK && Hero->GuildStatus == G_MASTER)
    {
        if (m_BtnGiveUp.UpdateMouseEvent() == true)
        {
            SEASON3B::CreateMessageBox(
                MSGBOX_LAYOUT_CLASS(SEASON3B::CSiegeGiveUpMsgBoxLayout, SessionOrigin()));
        }
    }
}

void CNewUIGuardWindow::SetData(LPPMSG_ANS_CASTLESIEGESTATE Info)
{
    if (!Info)
        return;

    memset(m_szOwnerGuild, 0, sizeof(char) * 9);
    memset(m_szOwnerGuildMaster, 0, sizeof(char) * 11);

    m_eTimeType = (CASTLESIEGE_STATE)Info->cCastleSiegeState;
    CMultiLanguage::ConvertFromUtf8(m_szOwnerGuild, Info->cOwnerGuild, MAX_GUILDNAME);
    CMultiLanguage::ConvertFromUtf8(m_szOwnerGuildMaster, Info->cOwnerGuildMaster,
                                    MAX_USERNAME_SIZE);

    m_wStartYear = MAKEWORD(Info->btStartYearL, Info->btStartYearH);
    m_byStartMonth = Info->btStartMonth;
    m_byStartDay = Info->btStartDay;
    m_byStartHour = Info->btStartHour;
    m_byStartMinute = Info->btStartMinute;
    m_wEndYear = MAKEWORD(Info->btEndYearL, Info->btEndYearH);
    m_byEndMonth = Info->btEndMonth;
    m_byEndDay = Info->btEndDay;
    m_byEndHour = Info->btEndHour;
    m_byEndMinute = Info->btEndMinute;
    m_wSiegeStartYear = MAKEWORD(Info->btSiegeStartYearL, Info->btSiegeStartYearH);
    m_bySiegeStartMonth = Info->btSiegeStartMonth;
    m_bySiegeStartDay = Info->btSiegeStartDay;
    m_bySiegeStartHour = Info->btSiegeStartHour;
    m_bySiegeStartMinute = Info->btSiegeStartMinute;
    m_dwStateLeftSec = MAKELONG(MAKEWORD(Info->btStateLeftSec4, Info->btStateLeftSec3),
                                MAKEWORD(Info->btStateLeftSec2, Info->btStateLeftSec1));
    //m_dwStateLeftSec = Info->btStateLeftSec1<<24 | Info->btStateLeftSec2<<16 | Info->btStateLeftSec3<<8 | Info->btStateLeftSec4;
}

void CNewUIGuardWindow::AddDeclareGuildList(wchar_t *szGuildName, int nMarkCount, BYTE byIsGiveUP,
                                            BYTE bySeqNum)
{
    m_DeclareGuildListBox.AddText(szGuildName, nMarkCount, byIsGiveUP, bySeqNum);
}

void CNewUIGuardWindow::ClearDeclareGuildList()
{
    m_DeclareGuildListBox.Clear();
}

void CNewUIGuardWindow::SortDeclareGuildList()
{
    m_DeclareGuildListBox.Sort();
}

void CNewUIGuardWindow::AddGuildList(wchar_t *szGuildName, BYTE byCsJoinSide, BYTE byGuildInvolved,
                                     int iGuildScore)
{
    m_GuildListBox.AddText(szGuildName, byCsJoinSide, byGuildInvolved, iGuildScore);
}

void CNewUIGuardWindow::ClearGuildList()
{
    m_GuildListBox.Clear();
}

SEASON3B::CNewUISiegeWarfare::CNewUISiegeWarfare(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper)

{
    m_pNewUIMng = NULL;
    m_pSiegeWarUI = NULL;
    m_iCurSiegeWarType = SIEGEWAR_TYPE_NONE;
    m_byGuildStatus = G_NONE;
    m_sGuildMarkIndex = -1;

    m_iHour = 0;
    m_iMinute = 0;
    m_iSecond = 0;
    m_dwSyncTime = 0;

    m_bCreated = true;

    memset(&m_Pos, 0, sizeof(POINT));
}

SEASON3B::CNewUISiegeWarfare::~CNewUISiegeWarfare()
{
    Release();
}

bool SEASON3B::CNewUISiegeWarfare::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_SIEGEWARFARE, this);

    Show(true);

    SetPos(x, y);

    return true;
}

void SEASON3B::CNewUISiegeWarfare::Release()
{
    if (m_pSiegeWarUI)
    {
        m_pSiegeWarUI->UnLoadImages();
        m_pSiegeWarUI->Release();
        SAFE_DELETE(m_pSiegeWarUI);
    }

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool SEASON3B::CNewUISiegeWarfare::UpdateMouseEvent()
{
    if (m_pSiegeWarUI)
    {
        if (!m_pSiegeWarUI->UpdateMouseEvent())
            return false;
    }

    return true;
}

bool SEASON3B::CNewUISiegeWarfare::UpdateKeyEvent()
{
    if (m_pSiegeWarUI)
    {
        if (!m_pSiegeWarUI->UpdateKeyEvent())
            return false;
    }

    return true;
}

bool SEASON3B::CNewUISiegeWarfare::Update()
{
    if (IsVisible() == false)
        return true;

    if (m_pSiegeWarUI == NULL)
        return true;

    if (gMapManager.InBattleCastle() && IsBattleCastleStart() == true)
    {
        m_iSecond = m_iSecond - (GetTickCount() - m_dwSyncTime);
        if (m_iSecond <= 0)
        {
            if (m_iMinute <= 0)
            {
                if (m_iHour <= 0)
                {
                    m_iSecond = 0;
                    m_iMinute = 0;
                    m_iHour = 0;
                }
                else
                {
                    --m_iHour;
                    m_iMinute = m_iMinute + 60;
                }
            }
            else
            {
                --m_iMinute;
                m_iSecond = m_iSecond + 60000;
            }
        }

        m_dwSyncTime = GetTickCount();

        m_pSiegeWarUI->SetTime(m_iHour, m_iMinute);
    }

    m_pSiegeWarUI->Update();

    return true;
}

float SEASON3B::CNewUISiegeWarfare::GetLayerDepth()
{
    return 1.6f;
}

void SEASON3B::CNewUISiegeWarfare::OpenningProcess()
{
}

void SEASON3B::CNewUISiegeWarfare::ClosingProcess()
{
}

void SEASON3B::CNewUISiegeWarfare::SetGuildData(const CHARACTER *pCharacter)
{
    m_sGuildMarkIndex = pCharacter->GuildMarkIndex;
    m_byGuildStatus = pCharacter->GuildStatus;
}

bool SEASON3B::CNewUISiegeWarfare::CreateMiniMapUI()
{
    if (m_pSiegeWarUI != NULL)
    {
        InitMiniMapUI();
    }

    if (!(Hero->EtcPart == PARTS_ATTACK_TEAM_MARK || Hero->EtcPart == PARTS_ATTACK_TEAM_MARK2 ||
          Hero->EtcPart == PARTS_ATTACK_TEAM_MARK3 || Hero->EtcPart == PARTS_DEFENSE_TEAM_MARK) ||
        Hero->GuildStatus == G_PERSON)
    {
        m_byGuildStatus = G_NONE;
    }

    switch (m_byGuildStatus)
    {
    case G_NONE: {
        m_pSiegeWarUI = new CNewUISiegeWarObserver(SessionOrigin()); // Observer
        m_iCurSiegeWarType = SIEGEWAR_TYPE_OBSERVER;
        m_bCreated = false;
    }
    break;
    case G_MASTER: {
        if (wcscmp(GuildMark[m_sGuildMarkIndex].UnionName, L"") == 0 ||
            wcscmp(GuildMark[m_sGuildMarkIndex].GuildName,
                   GuildMark[m_sGuildMarkIndex].UnionName) == 0)
        {
            m_pSiegeWarUI = new CNewUISiegeWarCommander(SessionOrigin()); // Commander
            m_iCurSiegeWarType = SIEGEWAR_TYPE_COMMANDER;
        }
        else
        {
            m_pSiegeWarUI = new CNewUISiegeWarSoldier(SessionOrigin()); // Soldier
            m_iCurSiegeWarType = SIEGEWAR_TYPE_SOLDIER;
        }
        m_bCreated = true;
    }
    break;
    default: {
        m_pSiegeWarUI = new CNewUISiegeWarSoldier(SessionOrigin()); // Soldier
        m_iCurSiegeWarType = SIEGEWAR_TYPE_SOLDIER;
        m_bCreated = true;
    }
    break;
    }

    m_pSiegeWarUI->LoadImages();
    m_pSiegeWarUI->Create(m_Pos.x, m_Pos.y);
    Show(true);

    return true;
}

void SEASON3B::CNewUISiegeWarfare::ClearGuildMemberLocation(void)
{
    if (m_iCurSiegeWarType == SIEGEWAR_TYPE_COMMANDER)
    {
        ((CNewUISiegeWarCommander *)m_pSiegeWarUI)->ClearGuildMemberLocation();
    }
}

void SEASON3B::CNewUISiegeWarfare::SetGuildMemberLocation(BYTE type, int x, int y)
{
    if (m_iCurSiegeWarType == SIEGEWAR_TYPE_COMMANDER)
    {
        ((CNewUISiegeWarCommander *)m_pSiegeWarUI)->SetGuildMemberLocation(type, x, y);
    }
}

void SEASON3B::CNewUISiegeWarfare::InitMiniMapUI()
{
    if (m_pSiegeWarUI == NULL)
    {
        return;
    }

    m_pSiegeWarUI->UnLoadImages();
    m_pSiegeWarUI->Release();

    SAFE_DELETE(m_pSiegeWarUI);

    m_iCurSiegeWarType = SIEGEWAR_TYPE_NONE;
    m_byGuildStatus = G_NONE;
    m_sGuildMarkIndex = -1;
}

void SEASON3B::CNewUISiegeWarfare::SetTime(BYTE byHour, BYTE byMinute)
{
    m_iHour = (int)byHour;
    m_iMinute = (int)byMinute;
    m_iSecond = 60000;
    m_dwSyncTime = GetTickCount();
}

void SEASON3B::CNewUISiegeWarfare::SetMapInfo(GuildCommander &data)
{
    if (m_pSiegeWarUI == NULL)
    {
        return;
    }

    m_pSiegeWarUI->SetMapInfo(data);
}

void SEASON3B::CNewUISiegeWarfare::InitSkillUI()
{
    if (m_pSiegeWarUI == NULL)
    {
        return;
    }

    m_pSiegeWarUI->InitBattleSkill();
}

void SEASON3B::CNewUISiegeWarfare::ReleaseSkillUI()
{
    if (m_pSiegeWarUI == NULL)
    {
        return;
    }

    m_pSiegeWarUI->ReleaseBattleSkill();
}

SEASON3B::CNewUISiegeWarBase::CNewUISiegeWarBase(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_BtnSkillScroll(keeper), m_BtnAlpha(keeper),
      m_SkillTooltip(keeper)
{
    m_iMiniMapScale = 1;
    m_fMiniMapAlpha = 1.f;

    m_iHour = 0;
    m_iMinute = 0;
    m_bRenderSkillUI = false;
    m_bRenderToolTip = false;

    memset(&m_MiniMapFramePos, 0, sizeof(POINT));
    memset(&m_MiniMapPos, 0, sizeof(POINT));
    memset(&m_TimeUIPos, 0, sizeof(POINT));
    memset(&m_SkillFramePos, 0, sizeof(POINT));
    memset(&m_BtnSkillScrollUpPos, 0, sizeof(POINT));
    memset(&m_BtnSkillScrollDnPos, 0, sizeof(POINT));
    memset(&m_SkillIconPos, 0, sizeof(POINT));
    memset(&m_UseSkillDestKillPos, 0, sizeof(POINT));
    memset(&m_CurKillCountPos, 0, sizeof(POINT));
    memset(&m_BtnAlphaPos, 0, sizeof(POINT));
    memset(&m_SkillTooltipPos, 0, sizeof(POINT));

    memset(&m_HeroPosInWorld, 0, sizeof(POINT));
    memset(&m_HeroPosInMiniMap, 0, sizeof(POINT));
    memset(&m_MiniMapScaleOffset, 0, sizeof(POINT));
}

SEASON3B::CNewUISiegeWarBase::~CNewUISiegeWarBase()
{
}

bool SEASON3B::CNewUISiegeWarBase::Create(int x, int y)
{
    SetPos(x, y);

    if (!OnCreate(x, y))
        return false;

    wchar_t szText[256] = {};
    mu_swprintf(szText, L"%d", (int)(m_fMiniMapAlpha * 100.5f));
    m_BtnAlpha.ChangeText(szText);
    m_BtnAlpha.ChangeButtonImgState(true, IMAGE_BTN_ALPHA, true);
    m_BtnAlpha.ChangeButtonInfo(m_BtnAlphaPos.x, m_BtnAlphaPos.y, BTN_ALPHA_WIDTH,
                                BTN_ALPHA_HEIGHT);

    if (IsBattleCastleStart() == true)
    {
        // 		if( InitBattleSkill() == true )
        // 		{
        // 			m_bRenderSkillUI = true;
        // 		}
        // 		else
        // 		{
        // 			m_bRenderSkillUI = false;
        // 		}

        InitBattleSkill();
    }

    return true;
}

void SEASON3B::CNewUISiegeWarBase::Release()
{
    ReleaseBattleSkill();

    OnRelease();
}

bool SEASON3B::CNewUISiegeWarBase::Update()
{
    for (auto &command : m_CmdBuffer)
        command.byLifeTime = (std::max)(0.f, command.byLifeTime - FPS_ANIMATION_FACTOR);
    UpdateBuffState();
    UpdateHeroPos();

    OnUpdate();

    return true;
}

bool SEASON3B::CNewUISiegeWarBase::InitBattleSkill()
{
    ReleaseBattleSkill();

    if (!(Hero->EtcPart == PARTS_ATTACK_TEAM_MARK || Hero->EtcPart == PARTS_ATTACK_TEAM_MARK2 ||
          Hero->EtcPart == PARTS_ATTACK_TEAM_MARK3 || Hero->EtcPart == PARTS_DEFENSE_TEAM_MARK) ||
        Hero->GuildStatus == G_PERSON)
    {
        return false;
    }

    m_BtnSkillScroll[0].ChangeButtonImgState(true, IMAGE_SKILL_BTN_SCROLL_UP, true);
    m_BtnSkillScroll[0].ChangeButtonInfo(m_BtnSkillScrollUpPos.x, m_BtnSkillScrollUpPos.y,
                                         SKILL_BTN_SCROLL_WIDTH, SKILL_BTN_SCROLL_HEIGHT);
    m_BtnSkillScroll[1].ChangeButtonImgState(true, IMAGE_SKILL_BTN_SCROLL_DN, true);
    m_BtnSkillScroll[1].ChangeButtonInfo(m_BtnSkillScrollDnPos.x, m_BtnSkillScrollDnPos.y,
                                         SKILL_BTN_SCROLL_WIDTH, SKILL_BTN_SCROLL_HEIGHT);

    switch (Hero->GuildStatus)
    {
    case G_MASTER: {
        m_listBattleSkill.push_back(AT_SKILL_INVISIBLE);
        m_listBattleSkill.push_back(AT_SKILL_REMOVAL_INVISIBLE);
        if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK_LORD)
        {
            m_listBattleSkill.push_back(AT_SKILL_REMOVAL_BUFF);
        }
        m_bRenderSkillUI = true;
    }
    break;
    case G_SUB_MASTER: {
        m_listBattleSkill.push_back(AT_SKILL_INVISIBLE);
        m_listBattleSkill.push_back(AT_SKILL_REMOVAL_INVISIBLE);
        m_bRenderSkillUI = true;
    }
    break;
    case G_BATTLE_MASTER: {
        m_listBattleSkill.push_back(AT_SKILL_STUN);
        m_listBattleSkill.push_back(AT_SKILL_REMOVAL_STUN);
        m_listBattleSkill.push_back(AT_SKILL_MANA);
        m_bRenderSkillUI = true;
    }
    break;
    }

    m_iterCurBattleSkill = m_listBattleSkill.begin();

    Hero->GuildSkill = (*m_iterCurBattleSkill);

    return true;
}

void SEASON3B::CNewUISiegeWarBase::ReleaseBattleSkill()
{
    m_listBattleSkill.clear();

    m_bRenderSkillUI = false;
}

void SEASON3B::CNewUISiegeWarBase::SetSkillScrollUp()
{
    if (m_listBattleSkill.begin() == m_iterCurBattleSkill)
        return;

    m_iterCurBattleSkill--;

    Hero->GuildSkill = (*m_iterCurBattleSkill);
}

void SEASON3B::CNewUISiegeWarBase::SetSkillScrollDn()
{
    if (m_listBattleSkill.end() == ++m_iterCurBattleSkill)
    {
        m_iterCurBattleSkill--;
        return;
    }

    Hero->GuildSkill = (*m_iterCurBattleSkill);
}

bool SEASON3B::CNewUISiegeWarBase::UpdateMouseEvent()
{
    if (!OnUpdateMouseEvent())
        return false;

    if (BtnProcess())
        return false;

    if (CheckMouseIn(m_MiniMapFramePos.x, m_MiniMapFramePos.y, MINIMAP_FRAME_WIDTH,
                     MINIMAP_FRAME_HEIGHT) ||
        CheckMouseIn(m_TimeUIPos.x, m_TimeUIPos.y, TIME_FRAME_WIDTH, TIME_FRAME_HEIGHT))
        return false;

    if (m_bRenderSkillUI == true)
    {
        if (CheckMouseIn(m_SkillIconPos.x, m_SkillIconPos.y, SKILL_ICON_WIDTH, SKILL_ICON_HEIGHT))
        {
            m_bRenderToolTip = true;
            return false;
        }
        else
        {
            m_bRenderToolTip = false;
        }

        if (CheckMouseIn(m_SkillFramePos.x, m_SkillFramePos.y, BATTLESKILL_FRAME_WIDTH,
                         BATTLESKILL_FRAME_HEIGHT))
            return false;
    }

    return true;
}

bool SEASON3B::CNewUISiegeWarBase::UpdateKeyEvent()
{
    if (!OnUpdateKeyEvent())
        return false;

    return true;
}

bool SEASON3B::CNewUISiegeWarBase::BtnProcess()
{
    POINT ptScaleBtn = {m_MiniMapFramePos.x + 134, m_MiniMapFramePos.y + 7};

    if (m_BtnAlpha.UpdateMouseEvent())
    {
        if (m_fMiniMapAlpha <= 0.5f)
        {
            m_fMiniMapAlpha = 1.f;
        }
        else
        {
            m_fMiniMapAlpha = m_fMiniMapAlpha - 0.1f;
        }

        wchar_t szText[256] = {};
        mu_swprintf(szText, L"%d", (int)(m_fMiniMapAlpha * 100.5f));
        m_BtnAlpha.ChangeText(szText);
        m_BtnAlpha.ChangeAlpha(m_fMiniMapAlpha);

        return true;
    }

    if (IsPress(VK_LBUTTON) && CheckMouseIn(ptScaleBtn.x, ptScaleBtn.y, 13, 12))
    {
        if (m_iMiniMapScale == 1)
            m_iMiniMapScale = 2;
        else
            m_iMiniMapScale = 1;

        return true;
    }

    if (m_bRenderSkillUI == true)
    {
        if (m_BtnSkillScroll[0].UpdateMouseEvent())
        {
            SetSkillScrollUp();
            return true;
        }
        if (m_BtnSkillScroll[1].UpdateMouseEvent())
        {
            SetSkillScrollDn();
            return true;
        }

        if (MouseWheel > 0)
        {
            SetSkillScrollUp();
            MouseWheel = 0;
            return true;
        }
        if (MouseWheel < 0)
        {
            SetSkillScrollDn();
            MouseWheel = 0;
            return true;
        }
    }

    return false;
}

void SEASON3B::CNewUISiegeWarBase::UpdateBuffState()
{
    DWORD m_dwBuffState = -1;

    OBJECT *o = &Hero->Object;

    if (g_isCharacterBuff(o, eBuff_CastleRegimentAttack1))
    {
        m_dwBuffState = eBuff_CastleRegimentAttack1;
    }
    else if (g_isCharacterBuff(o, eBuff_CastleRegimentAttack2))
    {
        m_dwBuffState = eBuff_CastleRegimentAttack2;
    }
    else if (g_isCharacterBuff(o, eBuff_CastleRegimentAttack3))
    {
        m_dwBuffState = eBuff_CastleRegimentAttack3;
    }
    else if (g_isCharacterBuff(o, eBuff_CastleRegimentDefense))
    {
        m_dwBuffState = eBuff_CastleRegimentDefense;
    }
}

void SEASON3B::CNewUISiegeWarBase::UpdateHeroPos()
{
    m_HeroPosInWorld.x = (Hero->PositionX) / m_iMiniMapScale;
    m_HeroPosInWorld.y = (256 - (Hero->PositionY)) / m_iMiniMapScale;

    m_MiniMapScaleOffset.x = std::max<int>((m_HeroPosInWorld.x - (64 * m_iMiniMapScale)), 0);
    m_MiniMapScaleOffset.y =
        std::min<int>(std::max<int>((m_HeroPosInWorld.y - (64 * m_iMiniMapScale)), 0), 128);

    m_HeroPosInMiniMap.x = m_HeroPosInWorld.x - m_MiniMapScaleOffset.x + m_MiniMapPos.x;
    m_HeroPosInMiniMap.y = m_HeroPosInWorld.y - m_MiniMapScaleOffset.y + m_MiniMapPos.y;

    m_fMiniMapTexU = (float)(m_MiniMapScaleOffset.x) / (256.f / (float)m_iMiniMapScale);
    m_fMiniMapTexV = (float)(m_MiniMapScaleOffset.y) / (256.f / (float)m_iMiniMapScale);
}

// SetPos

void SEASON3B::CNewUISiegeWarBase::SetTime(int iHour, int iMinute)
{
    m_iHour = iHour;
    m_iMinute = iMinute;
}

void SEASON3B::CNewUISiegeWarBase::SetMapInfo(GuildCommander &data)
{
    m_CmdBuffer[data.byTeam].byCmd = data.byCmd;
    m_CmdBuffer[data.byTeam].byTeam = data.byTeam;
    m_CmdBuffer[data.byTeam].byX = data.byX;
    m_CmdBuffer[data.byTeam].byY = data.byY;
    m_CmdBuffer[data.byTeam].byLifeTime = 100;
}

void SEASON3B::CNewUISiegeWarBase::SetRenderSkillUI(bool bRenderSkillUI)
{
    m_bRenderSkillUI = bRenderSkillUI;
}

void SEASON3B::CNewUISiegeWarBase::UnLoadImages()
{
    DeleteBitmap(IMAGE_MINIMAP);
    DeleteBitmap(IMAGE_MINIMAP_FRAME);
    DeleteBitmap(IMAGE_TIME_FRAME);
    DeleteBitmap(IMAGE_COMMAND_ATTACK);
    DeleteBitmap(IMAGE_COMMAND_DEFENCE);
    DeleteBitmap(IMAGE_COMMAND_WAIT);
    DeleteBitmap(IMAGE_BATTLESKILL_FRAME);
    DeleteBitmap(IMAGE_SKILL_BTN_SCROLL_UP);
    DeleteBitmap(IMAGE_SKILL_BTN_SCROLL_DN);
    DeleteBitmap(IMAGE_SKILL_ICON);
    DeleteBitmap(IMAGE_BTN_ALPHA);

    OnUnloadImages();
}

SEASON3B::CNewUISiegeWarCommander::CNewUISiegeWarCommander(SessionKeeper &keeper)
    : CNewUISiegeWarBase(keeper), m_BtnCommandGroup(keeper), m_BtnCommand(keeper)
{
    memset(&m_BtnCommandGroupPos, 0, sizeof(POINT));
    memset(&m_BtnCommandPos, 0, sizeof(POINT));

    m_iCurSelectBtnGroup = -1;
    m_iCurSelectBtnCommand = -1;
    m_bMouseInMiniMap = false;

    m_vGuildMemberLocationBuffer.reserve(1600);
}

SEASON3B::CNewUISiegeWarCommander::~CNewUISiegeWarCommander()
{
}

bool SEASON3B::CNewUISiegeWarCommander::OnCreate(int x, int y)
{
    InitCmdGroupBtn();
    InitCmdBtn();
    return true;
}

void SEASON3B::CNewUISiegeWarCommander::OnRelease()
{
    ClearGuildMemberLocation();
}

bool SEASON3B::CNewUISiegeWarCommander::OnUpdate()
{
    return true;
}

bool SEASON3B::CNewUISiegeWarCommander::OnUpdateMouseEvent()
{
    if (OnBtnProcess())
        return false;

    if (CheckMouseIn(m_MiniMapPos.x, m_MiniMapPos.y, 128, 128))
    {
        if (IsPress(VK_LBUTTON) && m_iCurSelectBtnCommand != -1)
        {
            GuildCommander SelectCmd;
            memset(&SelectCmd, 0, sizeof(GuildCommander));

            SelectCmd.byTeam = m_iCurSelectBtnGroup;
            SelectCmd.byCmd = m_iCurSelectBtnCommand;
            SelectCmd.byX = (MouseX + m_MiniMapScaleOffset.x - m_MiniMapPos.x) * m_iMiniMapScale;
            SelectCmd.byY =
                256 - (MouseY + m_MiniMapScaleOffset.y - m_MiniMapPos.y) * m_iMiniMapScale;
            SelectCmd.byLifeTime = 100;

            SocketClient->ToGameServer()->SendCastleGuildCommand(SelectCmd.byTeam, SelectCmd.byX,
                                                                 SelectCmd.byY, SelectCmd.byCmd);

            m_iCurSelectBtnCommand = -1;

            return false;
        }
        m_bMouseInMiniMap = true;
    }
    else
    {
        m_bMouseInMiniMap = false;
    }

    return true;
}

bool SEASON3B::CNewUISiegeWarCommander::OnUpdateKeyEvent()
{
    return true;
}

bool SEASON3B::CNewUISiegeWarCommander::OnBtnProcess()
{
    for (int i = 0; i < MAX_COMMANDGROUP; i++)
    {
        if (m_BtnCommandGroup[i].UpdateMouseEvent())
        {
            if (m_iCurSelectBtnGroup != -1)
            {
                SetBtnState(m_iCurSelectBtnGroup, false);
            }

            if (m_iCurSelectBtnGroup == i)
            {
                m_iCurSelectBtnGroup = -1;
                SetBtnState(i, false);
            }
            else
            {
                m_iCurSelectBtnGroup = i;
                SetBtnState(i, true);
            }

            m_iCurSelectBtnCommand = -1;

            return true;
        }
    }

    if (m_iCurSelectBtnGroup != -1 && m_iCurSelectBtnCommand == -1)
    {
        for (int j = 0; j < MINIMAP_CMD_MAX; j++)
        {
            if (m_BtnCommand[j].UpdateMouseEvent())
            {
                m_iCurSelectBtnCommand = j;

                return true;
            }
        }
    }

    return false;
}

void SEASON3B::CNewUISiegeWarCommander::OnSetPos(int x, int y)
{
    m_BtnCommandGroupPos.x = x;
    m_BtnCommandGroupPos.y = y + 5;
}

void SEASON3B::CNewUISiegeWarCommander::InitCmdGroupBtn()
{
    int iVal = 0;
    wchar_t sztext[255] = {
        0,
    };

    for (int i = 0; i < MAX_COMMANDGROUP; i++)
    {
        iVal = i * MINIMAP_BTN_GROUP_HEIGHT;
        m_BtnCommandGroup[i].ChangeButtonImgState(true, IMAGE_MINIMAP_BTN_GROUP, true);
        m_BtnCommandGroup[i].ChangeButtonInfo(m_BtnCommandGroupPos.x, m_BtnCommandGroupPos.y + iVal,
                                              MINIMAP_BTN_GROUP_WIDTH, MINIMAP_BTN_GROUP_HEIGHT);
        mu_swprintf(sztext, L"%d", i + 1);
        m_BtnCommandGroup[i].ChangeText(sztext);
    }
}

void SEASON3B::CNewUISiegeWarCommander::InitCmdBtn()
{
    for (int i = 0; i < MINIMAP_CMD_MAX; i++)
    {
        m_BtnCommand[i].ChangeButtonImgState(true, IMAGE_MINIMAP_BTN_COMMAND, true);
    }
}

void SEASON3B::CNewUISiegeWarCommander::SetBtnState(int iBtnType, bool bStateDown)
{
    if (bStateDown)
    {
        m_BtnCommandGroup[iBtnType].UnRegisterButtonState();
        m_BtnCommandGroup[iBtnType].RegisterButtonState(BUTTON_STATE_UP, IMAGE_MINIMAP_BTN_GROUP,
                                                        2);
        m_BtnCommandGroup[iBtnType].RegisterButtonState(BUTTON_STATE_OVER, IMAGE_MINIMAP_BTN_GROUP,
                                                        2);
        m_BtnCommandGroup[iBtnType].RegisterButtonState(BUTTON_STATE_DOWN, IMAGE_MINIMAP_BTN_GROUP,
                                                        2);
        m_BtnCommandGroup[iBtnType].ChangeImgIndex(IMAGE_MINIMAP_BTN_GROUP, 2);
    }
    else
    {
        m_BtnCommandGroup[iBtnType].UnRegisterButtonState();
        m_BtnCommandGroup[iBtnType].RegisterButtonState(BUTTON_STATE_UP, IMAGE_MINIMAP_BTN_GROUP,
                                                        0);
        m_BtnCommandGroup[iBtnType].RegisterButtonState(BUTTON_STATE_OVER, IMAGE_MINIMAP_BTN_GROUP,
                                                        1);
        m_BtnCommandGroup[iBtnType].RegisterButtonState(BUTTON_STATE_DOWN, IMAGE_MINIMAP_BTN_GROUP,
                                                        2);
        m_BtnCommandGroup[iBtnType].ChangeImgIndex(IMAGE_MINIMAP_BTN_GROUP, 0);
    }
}

void SEASON3B::CNewUISiegeWarCommander::ClearGuildMemberLocation(void)
{
    m_vGuildMemberLocationBuffer.clear();
}

void SEASON3B::CNewUISiegeWarCommander::SetGuildMemberLocation(BYTE type, int x, int y)
{
    VisibleUnitLocation vLocation = {type, (BYTE)x, (BYTE)y};

    m_vGuildMemberLocationBuffer.push_back(vLocation);
}

void SEASON3B::CNewUISiegeWarCommander::OnLoadImages()
{
    LoadBitmapW(L"Interface\\newui_SW_Minimap_Bt_group.tga", IMAGE_MINIMAP_BTN_GROUP,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_SW_Minimap_Bt_Command.tga", IMAGE_MINIMAP_BTN_COMMAND,
                LegacyTextureFilter::Linear);
}

void SEASON3B::CNewUISiegeWarCommander::OnUnloadImages()
{
    DeleteBitmap(IMAGE_MINIMAP_BTN_GROUP);
    DeleteBitmap(IMAGE_MINIMAP_BTN_COMMAND);
}

CNewUISiegeWarObserver::CNewUISiegeWarObserver(SessionKeeper &keeper) : CNewUISiegeWarBase(keeper)
{
}

CNewUISiegeWarObserver::~CNewUISiegeWarObserver()
{
}

bool SEASON3B::CNewUISiegeWarObserver::OnCreate(int x, int y)
{
    return true;
}

void SEASON3B::CNewUISiegeWarObserver::OnRelease()
{
}

bool SEASON3B::CNewUISiegeWarObserver::OnUpdate()
{
    return true;
}

void SEASON3B::CNewUISiegeWarObserver::OnSetPos(int x, int y)
{
}

bool SEASON3B::CNewUISiegeWarObserver::OnUpdateMouseEvent()
{
    if (OnBtnProcess())
        return false;

    return true;
}

bool SEASON3B::CNewUISiegeWarObserver::OnUpdateKeyEvent()
{
    return true;
}

bool SEASON3B::CNewUISiegeWarObserver::OnBtnProcess()
{
    return false;
}

void SEASON3B::CNewUISiegeWarObserver::OnLoadImages()
{
}

void SEASON3B::CNewUISiegeWarObserver::OnUnloadImages()
{
}

// Construction/Destruction

CNewUISiegeWarSoldier::CNewUISiegeWarSoldier(SessionKeeper &keeper) : CNewUISiegeWarBase(keeper)
{
}

CNewUISiegeWarSoldier::~CNewUISiegeWarSoldier()
{
}

// OnCreate
bool SEASON3B::CNewUISiegeWarSoldier::OnCreate(int x, int y)
{
    return true;
}

// OnRelease
void SEASON3B::CNewUISiegeWarSoldier::OnRelease()
{
}

// OnUpdate
bool SEASON3B::CNewUISiegeWarSoldier::OnUpdate()
{
    return true;
}

// OnRender

// OnCreate
void SEASON3B::CNewUISiegeWarSoldier::OnSetPos(int x, int y)
{
}

// RenderCharPosInMiniMap
// 미니맵에 모든 캐릭터를 렌더

// OnUpdateMouseEvent
bool SEASON3B::CNewUISiegeWarSoldier::OnUpdateMouseEvent()
{
    if (OnBtnProcess())
        return false;

    return true;
}

// OnUpdateKeyEvent
bool SEASON3B::CNewUISiegeWarSoldier::OnUpdateKeyEvent()
{
    return true;
}
// OnBtnProcess
bool SEASON3B::CNewUISiegeWarSoldier::OnBtnProcess()
{
    return false;
}

// OnLoadImages
void SEASON3B::CNewUISiegeWarSoldier::OnLoadImages()
{
}

// OnUnloadImages
void SEASON3B::CNewUISiegeWarSoldier::OnUnloadImages()
{
}

SEASON3B::CNewUIBattleSoccerScore::CNewUIBattleSoccerScore(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper)
{
    m_pNewUIMng = NULL;
    m_Pos.x = m_Pos.y = 0;
}

SEASON3B::CNewUIBattleSoccerScore::~CNewUIBattleSoccerScore()
{
    Release();
}

bool SEASON3B::CNewUIBattleSoccerScore::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_BATTLE_SOCCER_SCORE, this);

    SetPos(x, y);

    LoadImages();

    Show(false);

    return true;
}

void SEASON3B::CNewUIBattleSoccerScore::Release()
{
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool SEASON3B::CNewUIBattleSoccerScore::UpdateMouseEvent()
{
    return true;
}

bool SEASON3B::CNewUIBattleSoccerScore::UpdateKeyEvent()
{
    return true;
}

bool SEASON3B::CNewUIBattleSoccerScore::Update()
{
    return true;
}

int SEASON3B::CNewUIBattleSoccerScore::FindGuildMark(wchar_t *pszGuildName)
{
    for (int i = 0; i < MARK_EDIT; ++i)
    {
        MARK_t *p = &GuildMark[i];
        if (wcscmp(p->GuildName, pszGuildName) == 0)
        {
            return i;
        }
    }
    return 0;
}

float SEASON3B::CNewUIBattleSoccerScore::GetLayerDepth()
{
    return 1.8f;
}

CNewUIEnterBloodCastle::CNewUIEnterBloodCastle(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()),
      panel_(keeper, "blood_castle_entry.rml", "Events")
{
    m_iBloodCastleLimitLevel[0][0] = 15;
    m_iBloodCastleLimitLevel[0][1] = 80;
    m_iBloodCastleLimitLevel[1][0] = 81;
    m_iBloodCastleLimitLevel[1][1] = 130;
    m_iBloodCastleLimitLevel[2][0] = 131;
    m_iBloodCastleLimitLevel[2][1] = 180;
    m_iBloodCastleLimitLevel[3][0] = 181;
    m_iBloodCastleLimitLevel[3][1] = 230;
    m_iBloodCastleLimitLevel[4][0] = 231;
    m_iBloodCastleLimitLevel[4][1] = 280;
    m_iBloodCastleLimitLevel[5][0] = 281;
    m_iBloodCastleLimitLevel[5][1] = 330;
    m_iBloodCastleLimitLevel[6][0] = 331;
    m_iBloodCastleLimitLevel[6][1] = 400;
    m_iBloodCastleLimitLevel[7][0] = 0;
    m_iBloodCastleLimitLevel[7][1] = 0;

    m_iBloodCastleLimitLevel[8][0] = 10;
    m_iBloodCastleLimitLevel[8][1] = 60;
    m_iBloodCastleLimitLevel[9][0] = 61;
    m_iBloodCastleLimitLevel[9][1] = 110;
    m_iBloodCastleLimitLevel[10][0] = 111;
    m_iBloodCastleLimitLevel[10][1] = 160;
    m_iBloodCastleLimitLevel[11][0] = 161;
    m_iBloodCastleLimitLevel[11][1] = 210;
    m_iBloodCastleLimitLevel[12][0] = 211;
    m_iBloodCastleLimitLevel[12][1] = 260;
    m_iBloodCastleLimitLevel[13][0] = 261;
    m_iBloodCastleLimitLevel[13][1] = 310;
    m_iBloodCastleLimitLevel[14][0] = 311;
    m_iBloodCastleLimitLevel[14][1] = 400;
    m_iBloodCastleLimitLevel[15][0] = 0;
    m_iBloodCastleLimitLevel[15][1] = 0;
}
CNewUIEnterBloodCastle::~CNewUIEnterBloodCastle()
{
    Release();
}
bool CNewUIEnterBloodCastle::Create(CNewUIManager *manager, int, int)
{
    if (!manager)
        return false;
    manager_ = manager;
    manager_->AddUIObj(INTERFACE_BLOODCASTLE, this);
    Show(false);
    return true;
}
void CNewUIEnterBloodCastle::Release()
{
    panel_.Release();
    visible_ = false;
    activeButton_.clear();
    if (!manager_)
        return;
    manager_->RemoveUIObj(this);
    manager_ = nullptr;
}

bool CNewUIEnterBloodCastle::UpdateMouseEvent()
{
    const auto bounds = panel_.Bounds();
    return !IsVisible() || !CheckMouseIn(bounds.x, bounds.y, bounds.width, bounds.height);
}
bool CNewUIEnterBloodCastle::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_BLOODCASTLE) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_BLOODCASTLE);
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }

    return true;
}

int CNewUIEnterBloodCastle::CheckLimitLV(int iIndex)
{
    int iVal = 0;
    int iRet = 0;

    if (iIndex == 1)
    {
        iVal = 8;
    }

    int iLevel = CharacterAttribute->Level;

    if (gCharacterManager.IsMasterLevel(CharacterAttribute->Class) == false)
    {
        for (int iCastleLV = 0; iCastleLV < MAX_ENTER_GRADE - 1; ++iCastleLV)
        {
            if (iLevel >= m_iBloodCastleLimitLevel[iVal + iCastleLV][0] &&
                iLevel <= m_iBloodCastleLimitLevel[iVal + iCastleLV][1])
            {
                iRet = iCastleLV;
                break;
            }
        }
    }
    else
        iRet = MAX_ENTER_GRADE - 1;

    return iRet;
}

bool CNewUIEnterBloodCastle::Update()
{
    if (panel_.TakeFocus() && manager_)
        manager_->BringToFront(this);
    if (IsVisible())
        BtnProcess();
    visible_ = IsVisible();
    return true;
}
bool CNewUIEnterBloodCastle::BtnProcess()
{
    if (panel_.TakeClick("btnClose"))
    {
        g_pNewUISystem->Hide(INTERFACE_BLOODCASTLE);
        return true;
    }
    if (m_iNumActiveBtn < 0 || !panel_.TakeClick(activeButton_.c_str()))
        return false;
    constexpr int AnyTicketSlot = 0xFF;
    SocketClient->ToGameServer()->SendBloodCastleEnterRequest(m_iNumActiveBtn + 1, AnyTicketSlot);
    g_pNewUISystem->Hide(INTERFACE_BLOODCASTLE);
    return true;
}

void CNewUIEnterBloodCastle::OpenningProcess()
{
    SocketClient->ToGameServer()->SendCloseNpcRequest();
    const int baseClass = gCharacterManager.GetBaseClass(Hero->Class);
    const int levelGroup =
        baseClass == CLASS_DARK || baseClass == CLASS_DARK_LORD || baseClass == CLASS_RAGEFIGHTER
            ? 1
            : 0;
    m_iNumActiveBtn = CheckLimitLV(levelGroup);
    activeButton_ = "btnSelect" + std::to_string(m_iNumActiveBtn);
    panel_.SetText("tfTitle", I18N::Game::MessengerOfArchangel);
    std::wstring prompt = I18N::Game::YourWillToHelpTheArchangel;
    std::replace(prompt.begin(), prompt.end(), L'#', L'\n');
    panel_.SetText("taBloodCastleMent", prompt);
    StageGrades(levelGroup);
    visible_ = true;
}
void CNewUIEnterBloodCastle::ClosingProcess()
{
    visible_ = false;
    SocketClient->ToGameServer()->SendCloseNpcRequest();
}

float CNewUIEnterBloodCastle::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

bool CNewUIEnterBloodCastle::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessPanelInput(event);
}

CNewUIBloodCastle::CNewUIBloodCastle(SessionKeeper &keeper) : SessionUiLegacyBindings(keeper)

{
    m_pNewUIMng = NULL;
    m_Pos.x = m_Pos.y = 0;
    m_iTime = 0;
    m_iTimeState = BC_TIME_STATE_NORMAL;
    m_iMaxKillMonster = MAX_KILL_MONSTER;
    m_iKilledMonster = 0;
}

CNewUIBloodCastle::~CNewUIBloodCastle()
{
    Release();
}

bool CNewUIBloodCastle::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_BLOODCASTLE_TIME, this);

    SetPos(x, y);

    LoadImages();

    Show(false);

    return true;
}

void CNewUIBloodCastle::Release()
{
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool CNewUIBloodCastle::UpdateMouseEvent()
{
    if (true == BtnProcess())
        return false;

    if (CheckMouseIn(m_Pos.x, m_Pos.y, BLOODCASTLE_TIME_WINDOW_WIDTH,
                     BLOODCASTLE_TIME_WINDOW_HEIGHT))
        return false;

    return true;
}

bool CNewUIBloodCastle::UpdateKeyEvent()
{
    return true;
}

bool CNewUIBloodCastle::Update()
{
    if (!IsVisible())
        return true;

    if ((g_csMatchInfo == NULL) || (gMapManager.InBloodCastle() == false))
    {
        Show(false);
    }

    return true;
}

bool CNewUIBloodCastle::BtnProcess()
{
    return false;
}

float CNewUIBloodCastle::GetLayerDepth()
{
    return 1.2f;
}

void CNewUIBloodCastle::OpenningProcess()
{
}

void CNewUIBloodCastle::ClosingProcess()
{
}

void CNewUIBloodCastle::SetTime(int iTime)
{
    m_iTime = iTime;

    int iMinute = m_iTime / 60;
    mu_swprintf(m_szTime, L" %.2d:%.2d:%.2d", iMinute, m_iTime % 60, (int)WorldTime % 60);

    if (iMinute < 5)
    {
        m_iTimeState = BC_TIME_STATE_IMMINENCE;
    }
    else
    {
        m_iTimeState = BC_TIME_STATE_NORMAL;
    }
}

void CNewUIBloodCastle::SetKillMonsterStatue(int iKilled, int iMaxKill)
{
    m_iKilledMonster = iKilled;
    m_iMaxKillMonster = iMaxKill;
}

// Construction/Destruction

SEASON3B::CNewUICatapultWindow::CCatapultGroupButton::CCatapultGroupButton(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_Button(keeper)
{
    Initialize();
}

SEASON3B::CNewUICatapultWindow::CCatapultGroupButton::~CCatapultGroupButton()
{
}

void SEASON3B::CNewUICatapultWindow::CCatapultGroupButton::Initialize()
{

    m_iBtnNum = 0;
    m_iType = 0;
    m_iIndex = -1;
}

void SEASON3B::CNewUICatapultWindow::CCatapultGroupButton::Create(int iType, POINT ptWindow)
{
    Initialize();

    m_iType = iType;

    if (iType == CATAPULT_ATTACK)
    {
        m_iBtnNum = 4;
        m_Button[0].ChangeText(&I18N::Game::CastleGate1);
        m_Button[0].ChangeTextBackColor(RGBA(255, 255, 255, 0));
        m_Button[0].ChangeButtonImgState(true, IMAGE_CATAPULT_BTN_SMALL, true);
        m_Button[0].ChangeButtonInfo(ptWindow.x + 22, ptWindow.y + 135, 46, 36);
        m_Button[0].ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
        m_Button[0].ChangeImgColor(BUTTON_STATE_DOWN, RGBA(255, 255, 255, 255));
        m_Button[1].ChangeText(&I18N::Game::CastleGate2);
        m_Button[1].ChangeTextBackColor(RGBA(255, 255, 255, 0));
        m_Button[1].ChangeButtonImgState(true, IMAGE_CATAPULT_BTN_SMALL, true);
        m_Button[1].ChangeButtonInfo(ptWindow.x + 74, ptWindow.y + 135, 46, 36);
        m_Button[1].ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
        m_Button[1].ChangeImgColor(BUTTON_STATE_DOWN, RGBA(255, 255, 255, 255));
        m_Button[2].ChangeText(&I18N::Game::CastleGate3);
        m_Button[2].ChangeTextBackColor(RGBA(255, 255, 255, 0));
        m_Button[2].ChangeButtonImgState(true, IMAGE_CATAPULT_BTN_SMALL, true);
        m_Button[2].ChangeButtonInfo(ptWindow.x + 126, ptWindow.y + 135, 46, 36);
        m_Button[2].ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
        m_Button[2].ChangeImgColor(BUTTON_STATE_DOWN, RGBA(255, 255, 255, 255));
        m_Button[3].ChangeText(&I18N::Game::FrontYard);
        m_Button[3].ChangeTextBackColor(RGBA(255, 255, 255, 0));
        m_Button[3].ChangeButtonImgState(true, IMAGE_CATAPULT_BTN_BIG, true);
        m_Button[3].ChangeButtonInfo(ptWindow.x + 59, ptWindow.y + 182, 77, 47);
        m_Button[3].ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
        m_Button[3].ChangeImgColor(BUTTON_STATE_DOWN, RGBA(255, 255, 255, 255));
    }
    else if (iType == CATAPULT_DEFENSE)
    {
        m_iBtnNum = 3;
        m_Button[0].ChangeText(&I18N::Game::FrontYard1);
        m_Button[0].ChangeTextBackColor(RGBA(255, 255, 255, 0));
        m_Button[0].ChangeButtonImgState(true, IMAGE_CATAPULT_BTN_BIG, true);
        m_Button[0].ChangeButtonInfo(ptWindow.x + 18, ptWindow.y + 125, 77, 47);
        m_Button[0].ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
        m_Button[0].ChangeImgColor(BUTTON_STATE_DOWN, RGBA(255, 255, 255, 255));
        m_Button[1].ChangeText(&I18N::Game::FrontYard2);
        m_Button[1].ChangeTextBackColor(RGBA(255, 255, 255, 0));
        m_Button[1].ChangeButtonImgState(true, IMAGE_CATAPULT_BTN_BIG, true);
        m_Button[1].ChangeButtonInfo(ptWindow.x + 97, ptWindow.y + 125, 77, 47);
        m_Button[1].ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
        m_Button[1].ChangeImgColor(BUTTON_STATE_DOWN, RGBA(255, 255, 255, 255));
        m_Button[2].ChangeText(&I18N::Game::Bridge);
        m_Button[2].ChangeTextBackColor(RGBA(255, 255, 255, 0));
        m_Button[2].ChangeButtonImgState(true, IMAGE_CATAPULT_BTN_BIG, true);
        m_Button[2].ChangeButtonInfo(ptWindow.x + 56, ptWindow.y + 179, 77, 47);
        m_Button[2].ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
        m_Button[2].ChangeImgColor(BUTTON_STATE_DOWN, RGBA(255, 255, 255, 255));
    }
}

void SEASON3B::CNewUICatapultWindow::CCatapultGroupButton::AllUnLock()
{
    for (int i = 0; i < m_iBtnNum; ++i)
    {
        m_Button[i].ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
        m_Button[i].ChangeTextColor(RGBA(255, 255, 255, 255));
        m_Button[i].UnLock();
    }
}

void SEASON3B::CNewUICatapultWindow::CCatapultGroupButton::BtnSelected(int iIndex)
{
    if (iIndex < 0 || iIndex > m_iBtnNum)
    {
        return;
    }

    AllUnLock();

    m_Button[iIndex].ChangeImgColor(BUTTON_STATE_UP, RGBA(100, 100, 100, 255));
    m_Button[iIndex].ChangeTextColor(RGBA(100, 100, 100, 255));
    m_Button[iIndex].Lock();
}

int SEASON3B::CNewUICatapultWindow::CCatapultGroupButton::GetIndex()
{
    return m_iIndex;
}

int SEASON3B::CNewUICatapultWindow::CCatapultGroupButton::UpdateMouseEvent()
{
    int iResult = -1;

    for (int i = 0; i < m_iBtnNum; ++i)
    {
        if (m_Button[i].UpdateMouseEvent() == true)
        {
            BtnSelected(i);
            m_iIndex = i;
            iResult = i;
            break;
        }
    }

    return iResult;
}

SEASON3B::CNewUICatapultWindow::CNewUICatapultWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_BtnExit(keeper), m_BtnChoiceArea(keeper), m_BtnFire(keeper)
{
    m_pNewUIMng = NULL;
    m_Pos.x = m_Pos.y = 0;

    OpenningProcess();
}

SEASON3B::CNewUICatapultWindow::~CNewUICatapultWindow()
{
    Release();
}

bool SEASON3B::CNewUICatapultWindow::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_CATAPULT, this);

    SetPos(x, y);

    LoadImages();

    SetButtonInfo();

    Show(false);

    return true;
}

void SEASON3B::CNewUICatapultWindow::Release()
{
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool SEASON3B::CNewUICatapultWindow::UpdateMouseEvent()
{
    if (BtnProcess() == true)
    {
        return false;
    }

    return true;
}

bool SEASON3B::CNewUICatapultWindow::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_CATAPULT) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_CATAPULT);
            PlayBuffer(SOUND_CLICK01);

            return false;
        }
    }
    return true;
}

bool SEASON3B::CNewUICatapultWindow::Update()
{
    return true;
}

float SEASON3B::CNewUICatapultWindow::GetLayerDepth()
{
    return 5.0f;
}

void SEASON3B::CNewUICatapultWindow::OpenningProcess()
{
    m_iType = 0;
    m_iNpcKey = 0;
    Vector(0.f, 0.f, 0.f, m_vCameraPos);

    m_BtnFire.Lock();
    m_BtnFire.ChangeImgColor(BUTTON_STATE_UP, RGBA(100, 100, 100, 255));
    m_BtnFire.ChangeTextColor(RGBA(100, 100, 100, 255));
}

void SEASON3B::CNewUICatapultWindow::ClosingProcess()
{
    SocketClient->ToGameServer()->SendCloseNpcRequest();
}

void SEASON3B::CNewUICatapultWindow::Init(int iKey, int iType)
{
    m_iNpcKey = iKey;
    m_iType = iType;

    if (m_iType == CATAPULT_ATTACK)
    {
        m_BtnChoiceArea.Create(CATAPULT_ATTACK, m_Pos);
    }
    else if (m_iType == CATAPULT_DEFENSE)
    {
        m_BtnChoiceArea.Create(CATAPULT_DEFENSE, m_Pos);
    }
}

void SEASON3B::CNewUICatapultWindow::DoFire(int iKey, int iResult, int iType, int iPositionX,
                                            int iPositionY)
{
    int iIndex = FindCharacterIndex(iKey);
    CHARACTER *c = &CharactersClient[iIndex];
    OBJECT *o = &c->Object;

    SetAction(o, 1);

    BYTE bySubType = 0;
    BYTE byKeyH = 0;
    BYTE byKeyL = 0;
    if (iResult == 1)
    {
        bySubType = 1;
        byKeyH = (BYTE)(iKey & 0xff);
        byKeyL = (BYTE)(iKey >> 8);
    }
    else if (iResult == 2)
    {
        bySubType = 0;
    }

    vec3_t vPos, vTargetPos;
    Vector(o->Position[0], o->Position[1], 500.f, vPos);
    Vector(iPositionX * TERRAIN_SCALE, iPositionY * TERRAIN_SCALE, 100.f, vTargetPos);
    switch (iType)
    {
    case 1:
        CreateEffect(MODEL_FLY_BIG_STONE1, vPos, vTargetPos, o->Light, bySubType, &Hero->Object, 1,
                     byKeyH, byKeyL);
        break;

    case 2:
        CreateEffect(MODEL_FLY_BIG_STONE2, vPos, vTargetPos, o->Light, bySubType, &Hero->Object, 1,
                     byKeyH, byKeyL);
        break;
    }

    PlayBuffer(SOUND_BC_CATAPULT_ATTACK);
}

void SEASON3B::CNewUICatapultWindow::DoFireFixStartPosition(int iType, int iPositionX,
                                                            int iPositionY)
{
    vec3_t vPos, vTargetPos;

    Vector(iPositionX * TERRAIN_SCALE, iPositionY * TERRAIN_SCALE, 100.f, vTargetPos);

    switch (iType)
    {
    case 1:
        Vector(9200, 3000, 500.f, vPos);
        CreateEffect(MODEL_FLY_BIG_STONE1, vPos, vTargetPos, Hero->Object.Light, 0, &Hero->Object,
                     1);
        break;

    case 2:
        Vector(9400, 19000, 500.f, vPos);
        CreateEffect(MODEL_FLY_BIG_STONE2, vPos, vTargetPos, Hero->Object.Light, 0, &Hero->Object,
                     1);
        break;
    }

    vec3_t vLight = {1.f, 0.3f, 0.1f};
    CreateEffect(BITMAP_SHOCK_WAVE, vTargetPos, Hero->Object.Angle, vLight, 6);

    PlayBuffer(SOUND_BC_CATAPULT_ATTACK);
}

void SEASON3B::CNewUICatapultWindow::SetCameraPos(float x, float y, float z)
{
    Vector(x, y, z, m_vCameraPos);
}

void SEASON3B::CNewUICatapultWindow::GetCameraPos(vec3_t &vPos)
{
    if (m_vCameraPos[0] != 0.f || m_vCameraPos[1] != 0.f || m_vCameraPos[2] != 0.f)
    {
        VectorCopy(m_vCameraPos, vPos);
    }
    else
    {
        VectorCopy(Hero->Object.Position, vPos);
    }
}

bool SEASON3B::CNewUICatapultWindow::BtnProcess()
{
    // Top-right corner close "X" (shared frame). Hides + swallows the click.
    if (g_pNewUISystem->HandleFrameCornerClose(m_Pos, SEASON3B::INTERFACE_CATAPULT))
        return true;

    if (m_BtnExit.UpdateMouseEvent() == true)
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_CATAPULT);
        return true;
    }

    int iIndex = 0;
    iIndex = m_BtnChoiceArea.UpdateMouseEvent();

    if (iIndex > -1)
    {
        m_BtnFire.ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
        m_BtnFire.ChangeTextColor(RGBA(255, 255, 255, 255));
        m_BtnFire.UnLock();
        return true;
    }

    iIndex = m_BtnChoiceArea.GetIndex();
    if (iIndex > -1 && m_BtnFire.UpdateMouseEvent() == true)
    {
        SocketClient->ToGameServer()->SendFireCatapultRequest(m_iNpcKey, iIndex + 1);
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_CATAPULT);
    }

    return false;
}

CNewUIChaosCastleTime::CNewUIChaosCastleTime(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper)

{
    m_pNewUIMng = NULL;
    m_Pos.x = m_Pos.y = 0;
    m_iTime = 0;
    m_iTimeState = CC_TIME_STATE_NORMAL;
    m_iMaxKillMonster = MAX_KILL_MONSTER;
    m_iKilledMonster = 0;
}

CNewUIChaosCastleTime::~CNewUIChaosCastleTime()
{
    Release();
}

bool CNewUIChaosCastleTime::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_CHAOSCASTLE_TIME, this);

    SetPos(x, y);

    LoadImages();

    Show(false);

    return true;
}

void CNewUIChaosCastleTime::Release()
{
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool CNewUIChaosCastleTime::UpdateMouseEvent()
{
    if (true == BtnProcess())
        return false;

    if (CheckMouseIn(m_Pos.x, m_Pos.y, CHAOSCASTLE_TIME_WINDOW_WIDTH,
                     CHAOSCASTLE_TIME_WINDOW_HEIGHT))
        return false;

    return true;
}

bool CNewUIChaosCastleTime::UpdateKeyEvent()
{
    return true;
}

bool CNewUIChaosCastleTime::Update()
{
    if (!IsVisible())
        return true;

    if (gMapManager.InChaosCastle() == false)
    {
        Show(false);
    }

    return true;
}

bool CNewUIChaosCastleTime::BtnProcess()
{
    return false;
}

float CNewUIChaosCastleTime::GetLayerDepth()
{
    return 1.3f;
}

void CNewUIChaosCastleTime::OpenningProcess()
{
}

void CNewUIChaosCastleTime::ClosingProcess()
{
}

void CNewUIChaosCastleTime::SetTime(int iTime)
{
    m_iTime = iTime;

    int iMinute = m_iTime / 60;
    mu_swprintf(m_szTime, L" %.2d:%.2d:%.2d", iMinute, m_iTime % 60, (int)WorldTime % 60);

    if (iMinute < 5)
    {
        m_iTimeState = CC_TIME_STATE_IMMINENCE;
    }
    else
    {
        m_iTimeState = CC_TIME_STATE_NORMAL;
    }
}

void CNewUIChaosCastleTime::SetKillMonsterStatue(int iKilled, int iMaxKill)
{
    m_iKilledMonster = iKilled;
    m_iMaxKillMonster = iMaxKill;
}

SEASON3B::CNewUICryWolf::CNewUICryWolf(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), crywolf_(MapProcessForConstruction().Crywolf1st())
{
    m_pNewUIMng = NULL;
    m_Pos.x = m_Pos.y = 0;

    m_iHour = 0;
    m_iMinute = 0;
    m_iSecond = 0;
    m_dwSyncTime = 0;
    m_icntTime = 0;
    m_bTimeStart = false;
}

SEASON3B::CNewUICryWolf::~CNewUICryWolf()
{
    Release();
}

bool SEASON3B::CNewUICryWolf::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_CRYWOLF, this);

    SetPos(x, y);

    LoadImages();
    return true;
}

void SEASON3B::CNewUICryWolf::ClosingProcess()
{
    SocketClient->ToGameServer()->SendCloseNpcRequest();
}

float SEASON3B::CNewUICryWolf::GetLayerDepth()
{
    return 10.0f;
}

void SEASON3B::CNewUICryWolf::OpenningProcess()
{
    m_iHour = 0;
    m_iMinute = 0;
    m_iSecond = 0;
    m_dwSyncTime = 0;
    m_icntTime = 0;
    m_bTimeStart = false;
}

void SEASON3B::CNewUICryWolf::Release()
{
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool SEASON3B::CNewUICryWolf::UpdateMouseEvent()
{
    if (!crywolf_.IsCyrWolf1st() || !crywolf_.Get_State_Only_Elf())
        return true;
    constexpr int ButtonWidth = 54, ButtonHeight = 30, ButtonY = 250;
    int x = 0;
    if (crywolf_.Message_Box == 1 && crywolf_.Button_Down == 1)
        x = 330;
    else if (crywolf_.Message_Box == 1 && crywolf_.Button_Down == 2)
        x = 250;
    else if (crywolf_.Message_Box == 2 && crywolf_.Button_Down == 3)
        x = 290;
    if (x != 0 && MouseX > x && MouseX < x + ButtonWidth && MouseY > ButtonY &&
        MouseY < ButtonY + ButtonHeight)
    {
        crywolf_.Message_Box = 0;
        crywolf_.Button_Down = 0;
    }
    return true;
}
bool SEASON3B::CNewUICryWolf::UpdateKeyEvent()
{
    return true;
}

void SEASON3B::CNewUICryWolf::AdvanceResultPresentation()
{
    constexpr int ResultHoldTicks = 400, SlideStep = 15, SlideEnd = 479, RankStart = 390;
    constexpr float DecorationEnd = 21.f;
    if (crywolf_.Suc_Or_Fail == 1)
    {
        ++crywolf_.Delay;
        if (crywolf_.Delay >= ResultHoldTicks)
        {
            crywolf_.Delay = 0;
            crywolf_.Suc_Or_Fail = 0;
        }
    }
    if (crywolf_.Suc_Or_Fail >= 0)
        crywolf_.Delay_Add_inter = (std::max)(0, crywolf_.Delay_Add_inter - SlideStep);
    if (crywolf_.Suc_Or_Fail == 0 && crywolf_.Delay * SlideStep <= SlideEnd)
    {
        ++crywolf_.Delay;
        if (crywolf_.Delay * SlideStep > SlideEnd)
        {
            crywolf_.Delay = 0;
            crywolf_.Suc_Or_Fail = -1;
            crywolf_.Delay_Add_inter = RankStart;
            crywolf_.View_End_Result = true;
            SEASON3B::CreateMessageBox(
                MSGBOX_LAYOUT_CLASS(SEASON3B::CCry_Wolf_Result_Set_Temple, SessionOrigin()));
        }
    }
    crywolf_.Deco_Insert = crywolf_.View_Bal ? (std::min)(DecorationEnd, crywolf_.Deco_Insert + 1.f)
                                             : (std::max)(0.f, crywolf_.Deco_Insert - 1.f);
}

void SEASON3B::CNewUICryWolf::UpdateCountdown()
{
    if (!m_bTimeStart || crywolf_.m_CrywolfState != CRYWOLF_STATE_START)
        return;
    const DWORD now = GetTickCount();
    m_iSecond -= static_cast<int>(now - m_dwSyncTime);
    m_dwSyncTime = now;
    if (m_iMinute <= 0 && m_iSecond <= 0)
    {
        m_bTimeStart = false;
        m_iSecond = 0;
        m_icntTime = 0;
    }
}

bool SEASON3B::CNewUICryWolf::Update()
{
    if (!crywolf_.IsCyrWolf1st())
        return true;
    presentationTicks_ += sessionKeeper_.FrameAnimationFactor();
    while (presentationTicks_ >= 1.f)
    {
        AdvanceResultPresentation();
        presentationTicks_ -= 1.f;
    }
    UpdateCountdown();
    crywolf_.AdvanceNotices();
    return true;
}

float SEASON3B::CNewUICryWolf::ConvertX(float x)
{
    return x * (float)WindowWidth / (float)REFERENCE_WIDTH;
}

float SEASON3B::CNewUICryWolf::ConvertY(float y)
{
    return y * (float)WindowHeight / (float)REFERENCE_HEIGHT;
}

void SEASON3B::CNewUICryWolf::SetTime(int iHour, int iMinute)
{
    m_iHour = iHour;
    if (m_iMinute != iMinute)
    {
        m_iSecond = 60000;
        m_icntTime = 1;
        m_bTimeStart = true;
    }
    else if (m_icntTime == 1)
    {
        m_iSecond = 40000;
        m_icntTime++;
    }
    else if (m_icntTime == 2)
    {
        m_iSecond = 20000;
        m_icntTime = 0;
    }
    m_iMinute = iMinute;
}

void SEASON3B::CNewUICryWolf::InitTime()
{
    m_dwSyncTime = GetTickCount();
}

CNewUICursedTempleResult::CNewUICursedTempleResult(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()), panel_(keeper)
{
}
CNewUICursedTempleResult::~CNewUICursedTempleResult()
{
    Destroy();
}
bool CNewUICursedTempleResult::Create(CNewUIManager *manager, int x, int y)
{
    if (!manager)
        return false;
    manager_ = manager;
    manager_->AddUIObj(INTERFACE_CURSEDTEMPLE_RESULT, this);
    SetPos(x, y);
    Show(false);
    return true;
}
void CNewUICursedTempleResult::Destroy()
{
    panel_.Release();
    if (manager_)
        manager_->RemoveUIObj(this);
    manager_ = nullptr;
}
void CNewUICursedTempleResult::ResetGameResultInfo()
{
    m_WinState = 0;
    m_MyTeam = SEASON3A::eTeam_Count;
    m_AlliedTeamGameResult.clear();
    m_IllusionTeamGameResult.clear();
    contentDirty_ = true;
}

void CNewUICursedTempleResult::OpenningProcess()
{
    visible_ = true;
    StageContent();
}
void CNewUICursedTempleResult::ClosingProcess()
{
    visible_ = false;
    SocketClient->ToGameServer()->SendIllusionTempleRewardRequest();
    ResetGameResultInfo();
}
bool CNewUICursedTempleResult::UpdateMouseEvent()
{
    return !panel_.ContainsReferencePointer(MouseX, MouseY);
}
bool CNewUICursedTempleResult::UpdateKeyEvent()
{
    if (!IsVisible() || !IsPress(VK_ESCAPE))
        return true;
    g_pNewUISystem->Hide(INTERFACE_CURSEDTEMPLE_RESULT);
    return false;
}
bool CNewUICursedTempleResult::Update()
{
    if (panel_.TakeClose() && IsVisible())
        g_pNewUISystem->Hide(INTERFACE_CURSEDTEMPLE_RESULT);
    if (IsVisible())
        StageContent();
    visible_ = IsVisible();
    return true;
}

bool CNewUICursedTempleResult::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessInput(event);
}

void SEASON3B::CNewUICursedTempleResult::ReceiveCursedTempleGameResult(const BYTE *ReceiveBuffer)
{
    contentDirty_ = true;
    m_AlliedTeamGameResult.clear();
    m_IllusionTeamGameResult.clear();
    auto data = (LPPMSG_CURSED_TEMPLE_RESULT)ReceiveBuffer;

    int alliedPoint = data->btAlliedPoint;
    int illusionPoint = data->btIllusionPoint;
    int userCount = data->btUserCount;

    if (m_MyTeam == SEASON3A::eTeam_Allied)
    {
        if (illusionPoint < alliedPoint)
            m_WinState = 1;
        else
            m_WinState = 2;

        if (2 > alliedPoint)
            m_WinState = 2;
    }
    else
    {
        if (alliedPoint < illusionPoint)
            m_WinState = 1;
        else
            m_WinState = 2;

        if (2 > illusionPoint)
            m_WinState = 2;
    }

    int Offset = sizeof(PMSG_CURSED_TEMPLE_RESULT);

    for (int i = 0; i < userCount; i++)
    {
        auto data2 = (LPPMSG_CURSED_TEMPLE_USER_ADD_EXP)(ReceiveBuffer + Offset);

        CursedTempleGameResult TempData{};
        CMultiLanguage::ConvertFromUtf8(TempData.s_characterId, data2->GameId, MAX_USERNAME_SIZE);

        TempData.s_mapnumber = (short)data2->byMapNumber;

        TempData.s_team = static_cast<SEASON3A::eCursedTempleTeam>(data2->btTeam);
        TempData.s_class = gCharacterManager.ChangeServerClassTypeToClientClassType(data2->btClass);
        TempData.s_addexp = data2->nAddExp;

        if (TempData.s_team == SEASON3A::eTeam_Allied)
        {
            TempData.s_point = alliedPoint;
            m_AlliedTeamGameResult.push_back(TempData);
        }
        else
        {
            TempData.s_point = illusionPoint;
            m_IllusionTeamGameResult.push_back(TempData);
        }

        Offset += sizeof(PMSG_CURSED_TEMPLE_USER_ADD_EXP);
    }
}

CNewUIDoppelGangerFrame::CNewUIDoppelGangerFrame(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper)

{
    m_pNewUIMng = NULL;
    m_Pos.x = m_Pos.y = 0;
    m_iEnteredMonsters = 0;
    m_iMaxMonsters = 0;
    m_fMonsterGauge = 0.0f;
    m_fMonsterGaugeRcvd = 0.0f;
    m_iTime = 1800;
    m_bStopTimer = FALSE;
    m_bIceWalkerEnabled = FALSE;
    m_fIceWalkerPositionRcvd = 0.0f;
    m_fIceWalkerPosition = 0.0f;
}

CNewUIDoppelGangerFrame::~CNewUIDoppelGangerFrame()
{
    Release();
}

bool CNewUIDoppelGangerFrame::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_DOPPELGANGER_FRAME, this);

    SetPos(x, y);

    LoadImages();

    Show(false);

    return true;
}

void CNewUIDoppelGangerFrame::Release()
{
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool CNewUIDoppelGangerFrame::UpdateMouseEvent()
{
    if (true == BtnProcess())
        return false;
    return true;
}

bool CNewUIDoppelGangerFrame::UpdateKeyEvent()
{
    return true;
}

namespace
{
void AdvanceMarker(float &value, float target, float step)
{
    value += std::clamp(target - value, -step, step);
}
} // namespace

bool CNewUIDoppelGangerFrame::Update()
{
    if (!IsVisible())
        return true;
    constexpr float MarkerStep = 0.01f;
    const float step = MarkerStep * FPS_ANIMATION_FACTOR;
    AdvanceMarker(m_fMonsterGauge, m_fMonsterGaugeRcvd, step);
    if (m_bIceWalkerEnabled)
        AdvanceMarker(m_fIceWalkerPosition, m_fIceWalkerPositionRcvd, step);
    for (auto &[key, marker] : m_PartyPositionMap)
        if (marker.m_fPositionRcvd != -1)
            AdvanceMarker(marker.m_fPosition, marker.m_fPositionRcvd, step);
    return true;
}

bool CNewUIDoppelGangerFrame::BtnProcess()
{
    return false;
}

float CNewUIDoppelGangerFrame::GetLayerDepth()
{
    return 1.2f;
}

void CNewUIDoppelGangerFrame::OpenningProcess()
{
    m_iEnteredMonsters = 0;
    m_iMaxMonsters = 0;
    m_fMonsterGauge = 0.0f;
    m_fMonsterGaugeRcvd = 0.0f;
    m_iTime = 600;
    m_bStopTimer = FALSE;
    ResetPartyMemberInfo();
    m_fIceWalkerPositionRcvd = 0.0f;
    m_fIceWalkerPosition = 0.0f;
    m_bIceWalkerEnabled = FALSE;

    EnabledDoppelGangerEvent(TRUE);
}

void CNewUIDoppelGangerFrame::ClosingProcess()
{
    EnabledDoppelGangerEvent(FALSE);
}

void CNewUIDoppelGangerFrame::SetMonsterGauge(float fValue)
{
    m_fMonsterGaugeRcvd = fValue;
}

void CNewUIDoppelGangerFrame::SetRemainTime(int iSeconds)
{
    m_iTime = iSeconds;
}

void CNewUIDoppelGangerFrame::StopTimer(BOOL bFlag)
{
    m_bStopTimer = bFlag;
}

void CNewUIDoppelGangerFrame::ResetPartyMemberInfo()
{
    m_PartyPositionMap.clear();
}

void CNewUIDoppelGangerFrame::SetPartyMemberRcvd()
{
    for (std::map<WORD, PARTY_POSITION>::iterator iter = m_PartyPositionMap.begin();
         iter != m_PartyPositionMap.end(); ++iter)
    {
        iter->second.m_fPositionRcvd = -1.0f;
    }
}

void CNewUIDoppelGangerFrame::SetPartyMemberInfo(WORD wIndex, float fPosition)
{
    auto iter = m_PartyPositionMap.find(wIndex);
    if (iter == m_PartyPositionMap.end())
    {
        PARTY_POSITION party_pos;
        party_pos.m_fPosition = 0;
        party_pos.m_fPositionRcvd = fPosition;
        m_PartyPositionMap.insert(std::pair<WORD, PARTY_POSITION>(wIndex, party_pos));
    }
    else
    {
        iter->second.m_fPositionRcvd = fPosition;
    }
}

void CNewUIDoppelGangerFrame::SetIceWalkerMap(BOOL bEnable, float fPosition)
{
    if (bEnable == TRUE)
    {
        if (m_bIceWalkerEnabled == TRUE)
        {
            m_fIceWalkerPositionRcvd = fPosition;
        }
        else
        {
            m_fIceWalkerPositionRcvd = fPosition;
            m_fIceWalkerPosition = fPosition;
        }
        m_bIceWalkerEnabled = TRUE;
    }
    else
    {
        m_bIceWalkerEnabled = FALSE;
        m_fIceWalkerPositionRcvd = 0.0f;
    }
}

CNewUIDoppelGangerWindow::CNewUIDoppelGangerWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_BtnEnter(keeper), m_BtnClose(keeper)
{
    m_pNewUIMng = NULL;
    m_pNewUI3DRenderMng = NULL;
    m_Pos.x = m_Pos.y = 0;
    m_iRemainTime = 0;
    m_bIsEnterButtonLocked = FALSE;
}

CNewUIDoppelGangerWindow::~CNewUIDoppelGangerWindow()
{
    Release();
}

bool CNewUIDoppelGangerWindow::Create(CNewUIManager *pNewUIMng,
                                      CNewUI3DRenderMng *pNewUI3DRenderMng, int x, int y)
{
    if (NULL == pNewUIMng || NULL == pNewUI3DRenderMng || NULL == g_pNewItemMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_DOPPELGANGER_NPC, this);

    m_pNewUI3DRenderMng = pNewUI3DRenderMng;
    m_pNewUI3DRenderMng->Add3DRenderObj(this, INVENTORY_CAMERA_Z_ORDER);

    SetPos(x, y);

    LoadImages();

    InitButton(&m_BtnEnter, m_Pos.x + INVENTORY_WIDTH / 2 - 27, m_Pos.y + 190, I18N::Game::Enter);
    InitButton(&m_BtnClose, m_Pos.x + INVENTORY_WIDTH / 2 - 27, m_Pos.y + 360,
               I18N::Game::Close388);

    Show(false);

    return true;
}

void CNewUIDoppelGangerWindow::InitButton(CNewUIButton *pNewUIButton, int iPos_x, int iPos_y,
                                          const wchar_t *pCaption)
{
    pNewUIButton->ChangeText(pCaption);
    pNewUIButton->ChangeTextBackColor(RGBA(255, 255, 255, 0));
    pNewUIButton->ChangeButtonImgState(true, IMAGE_DOPPELGANGERWINDOW_BUTTON, true);
    pNewUIButton->ChangeButtonInfo(iPos_x, iPos_y, 53, 23);
    pNewUIButton->ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
    pNewUIButton->ChangeImgColor(BUTTON_STATE_DOWN, RGBA(255, 255, 255, 255));
}

void CNewUIDoppelGangerWindow::Release()
{
    UnloadImages();

    if (m_pNewUI3DRenderMng)
    {
        m_pNewUI3DRenderMng->Remove3DRenderObj(this);
        m_pNewUI3DRenderMng = NULL;
    }

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool CNewUIDoppelGangerWindow::UpdateMouseEvent()
{
    if (true == BtnProcess())
        return false;

    if (CheckMouseIn(m_Pos.x, m_Pos.y, INVENTORY_WIDTH, INVENTORY_HEIGHT))
        return false;

    return true;
}

bool CNewUIDoppelGangerWindow::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_DOPPELGANGER_NPC) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_DOPPELGANGER_NPC);
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }
    return true;
}

bool CNewUIDoppelGangerWindow::Update()
{
    if (IsVisible())
    {
    }
    return true;
}

bool CNewUIDoppelGangerWindow::IsVisible() const
{
    return CNewUIObj::IsVisible();
}

void CNewUIDoppelGangerWindow::OpeningProcess()
{
    LockEnterButton(FALSE);
}

void CNewUIDoppelGangerWindow::ClosingProcess()
{
    SocketClient->ToGameServer()->SendCloseNpcRequest();
}

float CNewUIDoppelGangerWindow::GetLayerDepth()
{
    return 5.0f;
}

bool CNewUIDoppelGangerWindow::BtnProcess()
{
    // Top-right corner close "X" (shared frame): hides + swallows the click.
    g_pNewUISystem->HandleFrameCornerClose(m_Pos, SEASON3B::INTERFACE_DOPPELGANGER_NPC);

    if (m_BtnEnter.UpdateMouseEvent() == true)
    {
        SocketClient->ToGameServer()->SendDoppelgangerEnterRequest(0xFF);
        return true;
    }

    if (m_BtnClose.UpdateMouseEvent() == true)
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_DOPPELGANGER_NPC);
    }

    return false;
}

void CNewUIDoppelGangerWindow::SetRemainTime(int iTime)
{
    m_iRemainTime = iTime;
    if (iTime != 0)
    {
        LockEnterButton(TRUE);
    }
}

void CNewUIDoppelGangerWindow::LockEnterButton(BOOL bLock)
{
    m_bIsEnterButtonLocked = bLock;
}

CNewUIEnterDevilSquare::CNewUIEnterDevilSquare(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_BtnExit(keeper), m_BtnEnter(keeper)
{
    m_pNewUIMng = NULL;
    memset(&m_Pos, 0, sizeof(POINT));
    memset(&m_EnterUITextPos, 0, sizeof(POINT));

    m_iNumActiveBtn = 1;
    m_BtnEnterStartPos.x = m_BtnEnterStartPos.y = 0;
    m_dwBtnTextColor[0] = RGBA(150, 150, 150, 255);
    m_dwBtnTextColor[1] = RGBA(255, 255, 255, 255);

    m_iDevilSquareLimitLevel[0][0] = 15;
    m_iDevilSquareLimitLevel[0][1] = 130;
    m_iDevilSquareLimitLevel[1][0] = 131;
    m_iDevilSquareLimitLevel[1][1] = 180;
    m_iDevilSquareLimitLevel[2][0] = 181;
    m_iDevilSquareLimitLevel[2][1] = 230;
    m_iDevilSquareLimitLevel[3][0] = 231;
    m_iDevilSquareLimitLevel[3][1] = 280;
    m_iDevilSquareLimitLevel[4][0] = 281;
    m_iDevilSquareLimitLevel[4][1] = 330;
    m_iDevilSquareLimitLevel[5][0] = 331;
    m_iDevilSquareLimitLevel[5][1] = 400;
    m_iDevilSquareLimitLevel[6][0] = 0;
    m_iDevilSquareLimitLevel[6][1] = 0;

    m_iDevilSquareLimitLevel[7][0] = 15;
    m_iDevilSquareLimitLevel[7][1] = 110;
    m_iDevilSquareLimitLevel[8][0] = 111;
    m_iDevilSquareLimitLevel[8][1] = 160;
    m_iDevilSquareLimitLevel[9][0] = 161;
    m_iDevilSquareLimitLevel[9][1] = 210;
    m_iDevilSquareLimitLevel[10][0] = 211;
    m_iDevilSquareLimitLevel[10][1] = 260;
    m_iDevilSquareLimitLevel[11][0] = 261;
    m_iDevilSquareLimitLevel[11][1] = 310;
    m_iDevilSquareLimitLevel[12][0] = 311;
    m_iDevilSquareLimitLevel[12][1] = 400;
    m_iDevilSquareLimitLevel[13][0] = 0;
    m_iDevilSquareLimitLevel[13][1] = 0;
}

CNewUIEnterDevilSquare::~CNewUIEnterDevilSquare()
{
    Release();
}

bool CNewUIEnterDevilSquare::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_DEVILSQUARE, this);

    SetPos(x, y);

    LoadImages();

    // Exit Button
    m_BtnExit.ChangeButtonImgState(true, IMAGE_ENTERDS_BASE_WINDOW_BTN_EXIT, false);
    m_BtnExit.ChangeButtonInfo(m_Pos.x + 13, m_Pos.y + 392, 36, 29);
    m_BtnExit.ChangeToolTipText(&I18N::Game::Close388, true);

    // Enter Button
    int iVal = 0;
    for (int i = 0; i < MAX_ENTER_GRADE; i++)
    {
        iVal = ENTER_BTN_VAL * i;
        m_BtnEnter[i].ChangeButtonImgState(true, IMAGE_ENTERDS_BASE_WINDOW_BTN_ENTER, true);
        m_BtnEnter[i].ChangeButtonInfo(m_BtnEnterStartPos.x, m_BtnEnterStartPos.y + iVal, 180, 29);
    }

    Show(false);

    return true;
}

void CNewUIEnterDevilSquare::Release()
{
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

void CNewUIEnterDevilSquare::SetBtnPos(int x, int y)
{
    m_BtnEnterStartPos.x = x;
    m_BtnEnterStartPos.y = y;
}

bool CNewUIEnterDevilSquare::UpdateMouseEvent()
{
    if (true == BtnProcess())
        return false;

    if (CheckMouseIn(m_Pos.x, m_Pos.y, ENTERDS_BASE_WINDOW_WIDTH, ENTERDS_BASE_WINDOW_HEIGHT))
        return false;

    return true;
}

bool CNewUIEnterDevilSquare::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_DEVILSQUARE) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_DEVILSQUARE);
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }

    return true;
}

bool CNewUIEnterDevilSquare::Update()
{
    if (!IsVisible())
        return true;

    return true;
}

// BtnProcess
bool CNewUIEnterDevilSquare::BtnProcess()
{
    // Top-right corner close "X" (shared frame). Hides + swallows the click.
    if (g_pNewUISystem->HandleFrameCornerClose(m_Pos, SEASON3B::INTERFACE_DEVILSQUARE))
        return true;

    if (m_BtnExit.UpdateMouseEvent() == true)
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_DEVILSQUARE);
        return true;
    }

    if ((m_iNumActiveBtn != -1) && (m_BtnEnter[m_iNumActiveBtn].UpdateMouseEvent() == true))
    {
        SocketClient->ToGameServer()->SendDevilSquareEnterRequest(m_iNumActiveBtn, 0xFF);
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_DEVILSQUARE);
    }

    return false;
}

float CNewUIEnterDevilSquare::GetLayerDepth()
{
    return 4.0f;
}

int CNewUIEnterDevilSquare::CheckLimitLV(int iIndex)
{
    int iVal = 0;
    int iRet = 0;

    if (iIndex == 1)
    {
        iVal = 7;
    }

    int iLevel;
    if (gCharacterManager.IsMasterLevel(CharacterAttribute->Class) == true)
        iLevel = Master_Level_Data.nMLevel;
    else
        iLevel = CharacterAttribute->Level;
    //Master_Level_Data.nMLevel

    if (gCharacterManager.IsMasterLevel(CharacterAttribute->Class) == false)
    {
        for (int iCastleLV = 0; iCastleLV < MAX_ENTER_GRADE - 1; ++iCastleLV)
        {
            if (iLevel >= m_iDevilSquareLimitLevel[iVal + iCastleLV][0] &&
                iLevel <= m_iDevilSquareLimitLevel[iVal + iCastleLV][1])
            {
                iRet = iCastleLV;
                break;
            }
        }
    }
    else
        iRet = MAX_ENTER_GRADE - 1;

    return iRet;
}

void CNewUIEnterDevilSquare::OpenningProcess()
{
    SocketClient->ToGameServer()->SendCloseNpcRequest();

    for (int i = 0; i < MAX_ENTER_GRADE; i++)
    {
        m_BtnEnter[i].ChangeTextColor(m_dwBtnTextColor[ENTERBTN_DISABLE]);
        m_BtnEnter[i].Lock();
    }

    int iLimitLVIndex = 0;
    if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK ||
        gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK_LORD ||
        gCharacterManager.GetBaseClass(Hero->Class) == CLASS_RAGEFIGHTER)
    {
        iLimitLVIndex = 1;
    }

    m_iNumActiveBtn = CheckLimitLV(iLimitLVIndex);

    m_BtnEnter[m_iNumActiveBtn].UnLock();
    m_BtnEnter[m_iNumActiveBtn].ChangeTextColor(m_dwBtnTextColor[ENTERBTN_ENABLE]);

    wchar_t sztext[255] = {
        0,
    };

    for (int i = 0; i < MAX_ENTER_GRADE - 1; i++)
    {
        mu_swprintf(sztext, I18N::Game::TheDSquareDDLevel, i + 1,
                    m_iDevilSquareLimitLevel[(iLimitLVIndex * (MAX_ENTER_GRADE)) + i][0],
                    m_iDevilSquareLimitLevel[(iLimitLVIndex * (MAX_ENTER_GRADE)) + i][1]);
        m_BtnEnter[i].SetFont(LegacyFontRole::Bold);
        m_BtnEnter[i].ChangeText(sztext);
    }

    mu_swprintf(sztext, I18N::Game::SquareNoDMasterLevel, 7);
    m_BtnEnter[MAX_ENTER_GRADE - 1].SetFont(LegacyFontRole::Bold);
    m_BtnEnter[MAX_ENTER_GRADE - 1].ChangeText(sztext);
}

void CNewUIEnterDevilSquare::ClosingProcess()
{
    SocketClient->ToGameServer()->SendCloseNpcRequest();
}

// UnloadImages

CNewUIExchangeLuckyCoin::CNewUIExchangeLuckyCoin(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_BtnExchange(keeper), m_BtnExit(keeper)
{
    m_pNewUIMng = NULL;
    memset(&m_Pos, 0, sizeof(POINT));
    memset(&m_TextPos, 0, sizeof(POINT));
    memset(&m_FirstBtnPos, 0, sizeof(POINT));
}

CNewUIExchangeLuckyCoin::~CNewUIExchangeLuckyCoin()
{
    Release();
}

bool CNewUIExchangeLuckyCoin::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_EXCHANGE_LUCKYCOIN, this);

    SetPos(x, y);

    LoadImages();

    // Exit Button
    m_BtnExit.ChangeButtonImgState(true, IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_BTN_EXIT, true);
    m_BtnExit.ChangeButtonInfo(
        m_Pos.x + ((EXCHANGE_LUCKYCOIN_WINDOW_WIDTH / 2) - (MSGBOX_BTN_EMPTY_SMALL_WIDTH / 2)),
        m_Pos.y + 360, MSGBOX_BTN_EMPTY_SMALL_WIDTH, MSGBOX_BTN_EMPTY_HEIGHT);
    m_BtnExit.ChangeText(&I18N::Game::Close388);

    // Exchange Button
    m_BtnExchange[0].ChangeButtonImgState(true, IMAGE_EXCHANGE_LUCKYCOIN_EXCHANGE_BTN, true);
    m_BtnExchange[0].SetFont(LegacyFontRole::Bold);
    m_BtnExchange[0].ChangeText(&I18N::Game::Exchange10Coins);

    m_BtnExchange[1].ChangeButtonImgState(true, IMAGE_EXCHANGE_LUCKYCOIN_EXCHANGE_BTN, true);
    m_BtnExchange[1].SetFont(LegacyFontRole::Bold);
    m_BtnExchange[1].ChangeText(&I18N::Game::Exchange20Coins);

    m_BtnExchange[2].ChangeButtonImgState(true, IMAGE_EXCHANGE_LUCKYCOIN_EXCHANGE_BTN, true);
    m_BtnExchange[2].SetFont(LegacyFontRole::Bold);
    m_BtnExchange[2].ChangeText(&I18N::Game::Exchange30Coins);

    Show(false);

    return true;
}

void CNewUIExchangeLuckyCoin::Release()
{
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

void CNewUIExchangeLuckyCoin::SetBtnPos(int x, int y)
{
    m_FirstBtnPos.x = x;
    m_FirstBtnPos.y = y;
}

bool CNewUIExchangeLuckyCoin::UpdateMouseEvent()
{
    if (true == BtnProcess())
        return false;

    if (CheckMouseIn(m_Pos.x, m_Pos.y, EXCHANGE_LUCKYCOIN_WINDOW_WIDTH,
                     EXCHANGE_LUCKYCOIN_WINDOW_HEIGHT))
        return false;

    return true;
}

bool CNewUIExchangeLuckyCoin::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_EXCHANGE_LUCKYCOIN) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_EXCHANGE_LUCKYCOIN);
            return false;
        }
    }

    return true;
}

bool CNewUIExchangeLuckyCoin::Update()
{
    if (!IsVisible())
        return true;

    return true;
}

bool CNewUIExchangeLuckyCoin::BtnProcess()
{
    // Top-right corner close "X" (shared frame). Hides + swallows the click.
    if (g_pNewUISystem->HandleFrameCornerClose(m_Pos, SEASON3B::INTERFACE_EXCHANGE_LUCKYCOIN))
        return true;

    if (m_BtnExit.UpdateMouseEvent() == true)
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_EXCHANGE_LUCKYCOIN);
        return true;
    }

    if (m_BtnExchange[0].UpdateMouseEvent() == true)
    {
        LockExchangeBtn();
        SocketClient->ToGameServer()->SendLuckyCoinExchangeRequest(10);
    }

    if (m_BtnExchange[1].UpdateMouseEvent() == true)
    {
        LockExchangeBtn();
        SocketClient->ToGameServer()->SendLuckyCoinExchangeRequest(20);
    }

    if (m_BtnExchange[2].UpdateMouseEvent() == true)
    {
        LockExchangeBtn();
        SocketClient->ToGameServer()->SendLuckyCoinExchangeRequest(30);
    }

    return false;
}

float CNewUIExchangeLuckyCoin::GetLayerDepth()
{
    return 4.2f;
}

void CNewUIExchangeLuckyCoin::OpenningProcess()
{
    g_pNewUISystem->Show(SEASON3B::INTERFACE_INVENTORY);
    UnLockExchangeBtn();
    g_pMyInventory->GetInventoryCtrl()->LockInventory();
    PlayBuffer(SOUND_CLICK01);
}

void CNewUIExchangeLuckyCoin::ClosingProcess()
{
    PlayBuffer(SOUND_CLICK01);
    g_pMyInventory->GetInventoryCtrl()->UnlockInventory();
    SocketClient->ToGameServer()->SendCraftingDialogCloseRequest();
}

void CNewUIExchangeLuckyCoin::LockExchangeBtn()
{
    for (int i = 0; i < 3; i++)
    {
        m_BtnExchange[i].Lock();
        m_BtnExchange[i].ChangeTextColor(0xff808080);
    }
}

void CNewUIExchangeLuckyCoin::UnLockExchangeBtn()
{
    for (int i = 0; i < 3; i++)
    {
        m_BtnExchange[i].UnLock();
        m_BtnExchange[i].ChangeTextColor(0xffffffff);
    }
}

CNewUIGateSwitchWindow::CNewUIGateSwitchWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_BtnExit(keeper), m_BtnOpen(keeper)
{
    m_pNewUIMng = NULL;
    m_Pos.x = m_Pos.y = 0;
}

CNewUIGateSwitchWindow::~CNewUIGateSwitchWindow()
{
    Release();
}

bool CNewUIGateSwitchWindow::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_GATESWITCH, this);

    SetPos(x, y);

    LoadImages();

    m_BtnExit.ChangeButtonImgState(true, IMAGE_GATESWITCHWINDOW_EXIT_BTN, false);
    m_BtnExit.ChangeButtonInfo(m_Pos.x + 13, m_Pos.y + 391, 36, 29);
    m_BtnExit.ChangeToolTipText(&I18N::Game::Close388, true);

    InitButton(&m_BtnOpen, m_Pos.x + 41, m_Pos.y + 320, I18N::Game::Open1107);

    Show(false);

    return true;
}

void CNewUIGateSwitchWindow::InitButton(CNewUIButton *pNewUIButton, int iPos_x, int iPos_y,
                                        const wchar_t *pCaption)
{
    pNewUIButton->ChangeText(pCaption);
    pNewUIButton->ChangeTextBackColor(RGBA(255, 255, 255, 0));
    pNewUIButton->ChangeButtonImgState(true, IMAGE_GATESWITCHWINDOW_BUTTON, true);
    pNewUIButton->ChangeButtonInfo(iPos_x, iPos_y, 108, 29);
    pNewUIButton->ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
    pNewUIButton->ChangeImgColor(BUTTON_STATE_DOWN, RGBA(255, 255, 255, 255));
}

void CNewUIGateSwitchWindow::Release()
{
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool CNewUIGateSwitchWindow::UpdateMouseEvent()
{
    if (m_BtnOpen.UpdateMouseEvent() == true)
    {
        SendToggleGate();
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_GATESWITCH);
    }

    if (true == BtnProcess())
        return false;

    if (CheckMouseIn(m_Pos.x, m_Pos.y, INVENTORY_WIDTH, INVENTORY_HEIGHT))
        return false;

    return true;
}

bool CNewUIGateSwitchWindow::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_GATESWITCH) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_GATESWITCH);
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }
    return true;
}

bool CNewUIGateSwitchWindow::Update()
{
    return true;
}

void CNewUIGateSwitchWindow::OpeningProcess()
{
}

void CNewUIGateSwitchWindow::ClosingProcess()
{
    SocketClient->ToGameServer()->SendCloseNpcRequest();
}

float CNewUIGateSwitchWindow::GetLayerDepth()
{
    return 5.0f;
}

bool CNewUIGateSwitchWindow::BtnProcess()
{
    // Top-right corner close "X" (shared frame): hides + swallows the click.
    g_pNewUISystem->HandleFrameCornerClose(m_Pos, SEASON3B::INTERFACE_GATESWITCH);

    if (m_BtnExit.UpdateMouseEvent() == true)
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_GATESWITCH);
        return true;
    }

    return false;
}

GoldBowmanLenaLegacyCalls::GoldBowmanLenaLegacyCalls(SessionKeeper &keeper,
                                                     CNewUIGoldBowmanLena &owner) noexcept
    : SessionUiLegacyBindings(keeper), owner_(owner)
{
}

CNewUIGoldBowmanLena::CNewUIGoldBowmanLena(SessionKeeper &keeper)
    : GoldBowmanLenaLegacyCalls(keeper, *this), cameraProjection_(keeper.CameraProjectionObject()),
      m_BtnRegister(keeper), m_BtnExit(keeper)
{
}

CNewUIGoldBowmanLena::~CNewUIGoldBowmanLena()
{
    Release();
}

bool CNewUIGoldBowmanLena::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
    {
        return false;
    }

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_GOLD_BOWMAN_LENA, this);

    SetPos(x, y);

    LoadImages();

    // Register Button
    m_BtnRegister.ChangeButtonImgState(true, IMAGE_GBL_BTN_SERIAL, false);
    m_BtnRegister.ChangeButtonInfo(m_Pos.x + 45, m_Pos.y + 285, 108, 29);
    m_BtnRegister.ChangeText(&I18N::Game::RegisteringRena);
    m_BtnRegister.ChangeToolTipText(&I18N::Game::RegisteringRena, true);

    // Exit Button
    m_BtnExit.ChangeButtonImgState(true, IMAGE_GBL_BTN_EXIT, false);
    m_BtnExit.ChangeButtonInfo(m_Pos.x + 13, m_Pos.y + 392, 36, 29);
    m_BtnExit.ChangeToolTipText(&I18N::Game::Close388, true); // 1002 "닫기"

    Show(false);

    return true;
}

void CNewUIGoldBowmanLena::Release()
{
    UnloadImages();
}

void CNewUIGoldBowmanLena::OpeningProcess()
{
}

void CNewUIGoldBowmanLena::ClosingProcess()
{
    g_bEventChipDialogEnable = 0;
    g_shEventChipCount = 0;
    SocketClient->ToGameServer()->SendEventChipExitDialog();
}

bool CNewUIGoldBowmanLena::UpdateMouseEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_GOLD_BOWMAN_LENA) == false)
    {
        return true;
    }

    if (m_BtnRegister.UpdateMouseEvent())
    {
        int registerItem = g_pMyInventory->GetInventoryCtrl()->GetItemCount(ITEM_POTION + 21, 0);

        if (registerItem != 0)
        {
            int index = g_pMyInventory->GetInventoryCtrl()->FindItemIndex(ITEM_POTION + 21, 0);

            if (index != -1)
            {
                SocketClient->ToGameServer()->SendEventChipRegistrationRequest(0, index);
            }
        }
    }

    // Top-right corner close "X" (shared frame): hides + swallows the click.
    if (g_pNewUISystem->HandleFrameCornerClose(m_Pos, SEASON3B::INTERFACE_GOLD_BOWMAN_LENA))
    {
        return false;
    }

    if (m_BtnExit.UpdateMouseEvent())
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_GOLD_BOWMAN_LENA);
        return false;
    }

    if (CheckMouseIn(m_Pos.x, m_Pos.y, INVENTORY_WIDTH, INVENTORY_HEIGHT))
    {
        if (IsPress(VK_RBUTTON))
        {
            MouseRButton = false;
            MouseRButtonPop = false;
            MouseRButtonPush = false;
            return false;
        }

        if (IsNone(VK_LBUTTON) == false)
        {
            return false;
        }
        return false;
    }
    else
    {
        if (IsNone(VK_LBUTTON) == false)
        {
            return false;
        }
        return false;
    }
    return true;
}

bool CNewUIGoldBowmanLena::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_GOLD_BOWMAN_LENA) == false)
    {
        return true;
    }

    if (IsPress(VK_ESCAPE) == true)
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_GOLD_BOWMAN_LENA);
        return false;
    }

    return true;
}

bool CNewUIGoldBowmanLena::Update()
{
    return true;
}

void CNewUIGoldBowmanLena::RendeerButton()
{
    m_BtnRegister.Render();
    m_BtnExit.Render();
}

float CNewUIGoldBowmanLena::GetLayerDepth() // 3.4f
{
    return 3.4f;
}

// OMF-01924

SEASON3B::CNewUIKanturu2ndEnterNpc::CNewUIKanturu2ndEnterNpc(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()),
      panel_(keeper, "kanturu_entry.rml", "Events")
{
    m_pNewUIMng = NULL;
    m_Pos.x = m_Pos.y = 0;
    m_pNpcObject = NULL;
    m_dwRefreshTime = 0;
    m_dwRefreshButtonGapTime = 0;

    Initialize();
}

SEASON3B::CNewUIKanturu2ndEnterNpc::~CNewUIKanturu2ndEnterNpc()
{
    Release();
}

void SEASON3B::CNewUIKanturu2ndEnterNpc::Initialize()
{
    m_bNpcAnimation = false;
    m_bEnterRequest = false;

    m_iStateTextNum = 0;
    ZeroMemory(m_strSubject, sizeof(m_strSubject));
    for (int i = 0; i < KANTURU2ND_STATETEXT_MAX; i++)
    {
        ZeroMemory(m_strStateText[i], sizeof(m_strStateText[i]));
    }
}

bool SEASON3B::CNewUIKanturu2ndEnterNpc::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_KANTURU2ND_ENTERNPC, this);

    SetPos(x, y);

    Show(false);

    return true;
}

void SEASON3B::CNewUIKanturu2ndEnterNpc::Release()
{
    panel_.Release();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool SEASON3B::CNewUIKanturu2ndEnterNpc::UpdateMouseEvent()
{
    const auto bounds = panel_.Bounds();
    return !IsVisible() || !CheckMouseIn(bounds.x, bounds.y, bounds.width, bounds.height);
}

bool SEASON3B::CNewUIKanturu2ndEnterNpc::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_KANTURU2ND_ENTERNPC) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_KANTURU2ND_ENTERNPC);
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }

    return true;
}

bool SEASON3B::CNewUIKanturu2ndEnterNpc::Update()
{
    if (IsVisible())
    {
        BtnProcess();
        if (timeGetTime() - m_dwRefreshTime > KANTURU2ND_REFRESH_GAPTIME)
            SendRequestKanturu3rdInfo();
        StageContent();
    }
    visible_ = IsVisible();
    return true;
}

bool SEASON3B::CNewUIKanturu2ndEnterNpc::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessPanelInput(event);
}
float SEASON3B::CNewUIKanturu2ndEnterNpc::GetLayerDepth()
{
    return 10.1f;
}

void SEASON3B::CNewUIKanturu2ndEnterNpc::SetNpcObject(OBJECT *pObj)
{
    m_pNpcObject = pObj;
}

bool SEASON3B::CNewUIKanturu2ndEnterNpc::IsNpcAnimation()
{
    return m_bNpcAnimation;
}

void SEASON3B::CNewUIKanturu2ndEnterNpc::SetNpcAnimation(bool bValue)
{
    m_bNpcAnimation = bValue;
}

bool SEASON3B::CNewUIKanturu2ndEnterNpc::IsEnterRequest()
{
    return m_bEnterRequest;
}

void SEASON3B::CNewUIKanturu2ndEnterNpc::SetEnterRequest(bool bValue)
{
    m_bEnterRequest = bValue;
}

void SEASON3B::CNewUIKanturu2ndEnterNpc::CreateMessageBox(BYTE btResult)
{
    wchar_t strMessage[256];
    if (btResult == POPUP_FAILED || btResult == POPUP_FAILED2)
    {
        wcscpy(strMessage, I18N::Game::FailedToEnter);
    }
    else if (btResult == POPUP_UNIRIA)
    {
        wcscpy(strMessage, I18N::Game::YouCannotWarpWhileRidingOnAUnicorn);
    }
    else if (btResult == POPUP_CHANGERING)
    {
        wcscpy(strMessage, I18N::Game::YouCanTWarpWearingTheRingOfTransformation);
    }
    else if (btResult == POPUP_NOT_HELPER)
    {
        wcscpy(strMessage, I18N::Game::YouCanOnlyWarpRidingA);
    }
    else
    {
        wcscpy(strMessage, I18N::Game::Lookup(2170 + btResult));
    }

    CreateOkMessageBox(strMessage);
}

void SEASON3B::CNewUIKanturu2ndEnterNpc::ReceiveKanturu3rdInfo(BYTE btState, BYTE btDetailState,
                                                               BYTE btEnter, BYTE btUserCount,
                                                               int iRemainTime)
{
    if (m_pNpcObject && m_pNpcObject->CurrentAction == KANTURU2ND_NPC_ANI_ROT)
    {
        return;
    }

    if (g_MessageBox.IsEmpty() == false)
    {
        return;
    }

    Initialize();

    m_byState = btState;

    if (btEnter == 1)
    {
        canEnter_ = true;
    }
    else
    {
        canEnter_ = false;
    }

    if (btState == KANTURU_STATE_TOWER)
    {
        if (btDetailState == KANTURU_TOWER_REVITALIXATION || btDetailState == KANTURU_TOWER_NOTIFY)
        {
            wcscpy(m_strSubject, I18N::Game::YouMayNowProceedToTheRefineryTower);
            wcscpy(m_strStateText[0], I18N::Game::PathToTheRefineryTowerIsNowOpened);
            mu_swprintf(m_strStateText[1], I18N::Game::PathToTheRefineryTowerWillBeClosedInDHours,
                        iRemainTime / 3600);
            m_iStateTextNum = 2;
        }
        else
        {
            wcscpy(m_strSubject, I18N::Game::YouCanTWarpToTheRefineryTower);
            wcscpy(m_strStateText[0], I18N::Game::DefeatTheNightmareThatControllingThe);
            wcscpy(m_strStateText[1], I18N::Game::EntranceIsRestrictedToEnsureThe);
            m_iStateTextNum = 2;
        }
    }
    else if (btState == KANTURU_STATE_MAYA_BATTLE)
    {
        if (btDetailState != KANTURU_MAYA_DIRECTION_STANBY1 &&
            btDetailState != KANTURU_MAYA_DIRECTION_STANBY2 &&
            btDetailState != KANTURU_MAYA_DIRECTION_STANBY3)
        {
            wcscpy(m_strSubject, I18N::Game::BattleWithMayaIsOngoing);
            mu_swprintf(m_strStateText[0], I18N::Game::DPlayersAreTryingToOpen, btUserCount);
        }
        else
        {
            wcscpy(m_strSubject, I18N::Game::MorePlayersAreNeededToOpenThePathToTheTower);

            if (btDetailState == KANTURU_MAYA_DIRECTION_STANBY1)
            {
                if (btUserCount < 15)
                {
                    wcscpy(m_strStateText[0], I18N::Game::YouMayNowEnter);
                }
                else if (btUserCount == 15)
                {
                    wcscpy(m_strStateText[0], I18N::Game::MoonstonePendantAuthenticationHasFailed);
                }
                else
                {
                    if (canEnter_)
                    {
                        wcscpy(m_strStateText[0], I18N::Game::YouMayNowEnter);
                    }
                    else
                    {
                        wcscpy(m_strStateText[0],
                               I18N::Game::MoonstonePendantAuthenticationHasFailed);
                    }
                }
                m_iStateTextNum = 1;
            }
            else if (btDetailState == KANTURU_MAYA_DIRECTION_STANBY2)
            {
                if (btUserCount < 15)
                {
                    mu_swprintf(m_strStateText[0], I18N::Game::NightmareHasLostTheControlOf,
                                btUserCount);
                    mu_swprintf(m_strStateText[1], I18N::Game::MorePowerFromDPlayersAreNeeded,
                                15 - btUserCount);
                    m_iStateTextNum = 2;
                }
                else if (btUserCount == 15)
                {
                    wcscpy(m_strStateText[0],
                           I18N::Game::NightmareHasLostTheControlOfMayaSLeftHand);
                    m_iStateTextNum = 1;
                }
            }
            else if (btDetailState == KANTURU_MAYA_DIRECTION_STANBY3)
            {
                if (btUserCount < 15)
                {
                    mu_swprintf(m_strStateText[0], I18N::Game::NightmareHasLostTheControlOf2166,
                                btUserCount);
                    mu_swprintf(m_strStateText[1], I18N::Game::MorePowerFromDPlayersAreNeeded,
                                15 - btUserCount);
                    m_iStateTextNum = 2;
                }
                else if (btUserCount == 15)
                {
                    wcscpy(m_strStateText[0],
                           I18N::Game::NightmareHasLostTheControlOfMayaSLeftHand);
                    m_iStateTextNum = 1;
                }
            }
            else
            {
                if (canEnter_)
                {
                    wcscpy(m_strStateText[0], I18N::Game::YouMayNowEnter);

                    m_iStateTextNum = 1;
                }
            }
        }

        if (btDetailState == KANTURU_MAYA_DIRECTION_NOTIFY ||
            btDetailState == KANTURU_MAYA_DIRECTION_MONSTER1 ||
            btDetailState == KANTURU_MAYA_DIRECTION_MAYA1 ||
            btDetailState == KANTURU_MAYA_DIRECTION_END_MAYA1 ||
            btDetailState == KANTURU_MAYA_DIRECTION_ENDCYCLE_MAYA1)
        {
            mu_swprintf(m_strStateText[1],
                        I18N::Game::CurrentlyDPlayersAreInBattleWithMayaSLefeHand, btUserCount);
            m_iStateTextNum = 2;
        }
        else if (btDetailState == KANTURU_MAYA_DIRECTION_MONSTER2 ||
                 btDetailState == KANTURU_MAYA_DIRECTION_MAYA2 ||
                 btDetailState == KANTURU_MAYA_DIRECTION_END_MAYA2 ||
                 btDetailState == KANTURU_MAYA_DIRECTION_ENDCYCLE_MAYA2)
        {
            mu_swprintf(m_strStateText[1],
                        I18N::Game::CurrentlyDPlayersAreInBattleWithMayaSRightHand, btUserCount);
            m_iStateTextNum = 2;
        }
        else if (btDetailState == KANTURU_MAYA_DIRECTION_MONSTER3 ||
                 btDetailState == KANTURU_MAYA_DIRECTION_MAYA3 ||
                 btDetailState == KANTURU_MAYA_DIRECTION_END_MAYA3 ||
                 btDetailState == KANTURU_MAYA_DIRECTION_ENDCYCLE_MAYA3)
        {
            mu_swprintf(m_strStateText[1],
                        I18N::Game::CurrentlyDPlayersAreInBattleWithMayaSBothHands, btUserCount);
            m_iStateTextNum = 2;
        }
        else if (btDetailState == KANTURU_MAYA_DIRECTION_NONE ||
                 btDetailState == KANTURU_MAYA_DIRECTION_END ||
                 btDetailState == KANTURU_MAYA_DIRECTION_ENDCYCLE)
        {
            m_iStateTextNum = 1;
        }
    }
    else if (btState == KANTURU_STATE_NIGHTMARE_BATTLE)
    {
        wcscpy(m_strSubject, I18N::Game::BattleWithMayaIsOngoing);
        mu_swprintf(m_strStateText[0], I18N::Game::DPlayersAreTryingToOpen, btUserCount);
        mu_swprintf(m_strStateText[1], I18N::Game::CurrentlyDPlayersAreInBattleWithNightmare,
                    btUserCount);
        m_iStateTextNum = 2;
    }
    else if (btState == KANTURU_STATE_STANDBY)
    {
        wcscpy(m_strSubject, I18N::Game::BossBattleWillStartSoon);
        if (btDetailState == 1) // STANBY_START
        {
            mu_swprintf(m_strStateText[0], I18N::Game::ForceOfTheNightmareHasInvaded,
                        iRemainTime / 60);
        }
        else // STANBY_NONE || STANBY_NOTIFY || STANBY_END || STANBY_ENDCYCLE
        {
            mu_swprintf(m_strStateText[0], I18N::Game::YouWillBeAbleToApproachMayaShortly);
        }
        mu_swprintf(m_strStateText[1], I18N::Game::DefeatTheNightmareThatControllingThe);
        mu_swprintf(m_strStateText[2], I18N::Game::EntranceIsRestrictedToEnsureThe);
        m_iStateTextNum = 3;
    }
    else
    {
        wcscpy(m_strSubject, I18N::Game::FailedToEnter);
    }

    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_KANTURU2ND_ENTERNPC) == false)
    {
        g_pNewUISystem->Show(SEASON3B::INTERFACE_KANTURU2ND_ENTERNPC);
    }
}

void SEASON3B::CNewUIKanturu2ndEnterNpc::ReceiveKanturu3rdEnter(BYTE btResult)
{
    m_bEnterRequest = false;
    CreateMessageBox(btResult);

    m_pNpcObject->AnimationFrame = 0;
    m_bNpcAnimation = false;
    SetAction(m_pNpcObject, KANTURU2ND_NPC_ANI_STOP);

    DeleteJoint(BITMAP_JOINT_ENERGY, NULL);

    g_pNewUISystem->Hide(SEASON3B::INTERFACE_KANTURU2ND_ENTERNPC);
}

void SEASON3B::CNewUIKanturu2ndEnterNpc::SendRequestKanturu3rdInfo()
{
    SocketClient->ToGameServer()->SendKanturuInfoRequest();
    m_dwRefreshTime = timeGetTime();
}

void SEASON3B::CNewUIKanturu2ndEnterNpc::SendRequestKanturu3rdEnter()
{
    SocketClient->ToGameServer()->SendKanturuEnterRequest();
    m_bEnterRequest = true;
}

bool SEASON3B::CNewUIKanturu2ndEnterNpc::EnterKanturu()
{
    if (m_pNpcObject)
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_KANTURU2ND_ENTERNPC);

        if (m_byState == KANTURU_STATE_TOWER)
        {
            SetAction(m_pNpcObject, KANTURU2ND_NPC_ANI_ROT);
            m_bNpcAnimation = true;
            return true;
        }

        ITEM *pItemHelper, *pItemRingLeft, *pItemRingRight, *pItemWing;
        pItemHelper = &CharacterMachine->Equipment[EQUIPMENT_HELPER];
        pItemRingLeft = &CharacterMachine->Equipment[EQUIPMENT_RING_LEFT];
        pItemRingRight = &CharacterMachine->Equipment[EQUIPMENT_RING_RIGHT];
        pItemWing = &CharacterMachine->Equipment[EQUIPMENT_WING];

        if (pItemHelper->Type == ITEM_HORN_OF_UNIRIA)
        {
            CreateMessageBox(POPUP_UNIRIA);
            return true;
        }

        if (g_ChangeRingMgr->CheckChangeRing(pItemRingLeft->Type) ||
            g_ChangeRingMgr->CheckChangeRing(pItemRingRight->Type))
        {
            CreateMessageBox(POPUP_CHANGERING);
            return true;
        }

        if (!((pItemWing->Type >= ITEM_WINGS_OF_ELF && pItemWing->Type <= ITEM_WINGS_OF_DARKNESS) ||
              (pItemWing->Type >= ITEM_WING_OF_STORM &&
               pItemWing->Type <= ITEM_WING_OF_DIMENSION) ||
              (ITEM_WING + 130 <= pItemWing->Type && pItemWing->Type <= ITEM_WING + 134) ||
              pItemHelper->Type == ITEM_HORN_OF_DINORANT ||
              pItemHelper->Type == ITEM_DARK_HORSE_ITEM || pItemWing->Type == ITEM_CAPE_OF_LORD ||
              pItemHelper->Type == ITEM_HORN_OF_FENRIR ||
              (pItemWing->Type >= ITEM_CAPE_OF_FIGHTER &&
               pItemWing->Type <= ITEM_CAPE_OF_OVERRULE) ||
              (pItemWing->Type == ITEM_WING + 135)))
        {
            CreateMessageBox(POPUP_NOT_HELPER);
            return true;
        }

        if (pItemRingLeft->Type == ITEM_MOONSTONE_PENDANT ||
            pItemRingRight->Type == ITEM_MOONSTONE_PENDANT)
        {
            SetAction(m_pNpcObject, KANTURU2ND_NPC_ANI_ROT);
            m_bNpcAnimation = true;
        }
        else
        {
            CreateMessageBox(POPUP_NOT_MUNSTONE);
            return true;
        }
    }

    return true;
}

bool SEASON3B::CNewUIKanturu2ndEnterNpc::BtnProcess()
{
    if (refreshLocked_)
    {
        if (timeGetTime() - m_dwRefreshButtonGapTime > KANTURU2ND_REFRESHBUTTON_GAPTIME)
        {
            refreshLocked_ = false;
        }
    }
    else if (panel_.TakeClick("btnRefresh"))
    {
        SendRequestKanturu3rdInfo();

        refreshLocked_ = true;

        m_dwRefreshButtonGapTime = timeGetTime();
        return true;
    }

    if (canEnter_ && panel_.TakeClick("btnEnter"))
        return EnterKanturu();

    if (panel_.TakeClick("btnClose"))
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_KANTURU2ND_ENTERNPC);

        return true;
    }
    return false;
}

SEASON3B::CNewUIKanturuInfoWindow::CNewUIKanturuInfoWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), g_Direction(keeper.DirectionObject()),
      renderer_(RendererForConstruction()), panel_(keeper, "kanturu_info.rml", "Events")
{
    m_pNewUIMng = NULL;
    m_Pos.x = m_Pos.y = 0;

    m_iSecond = 0;
    m_dwSyncTime = 0;
}

SEASON3B::CNewUIKanturuInfoWindow::~CNewUIKanturuInfoWindow()
{
    Release();
}

bool SEASON3B::CNewUIKanturuInfoWindow::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_KANTURU_INFO, this);

    SetPos(x, y);

    Show(false);

    return true;
}

void SEASON3B::CNewUIKanturuInfoWindow::Release()
{
    panel_.Release();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool SEASON3B::CNewUIKanturuInfoWindow::UpdateMouseEvent()
{
    return true;
}

bool SEASON3B::CNewUIKanturuInfoWindow::UpdateKeyEvent()
{
    return true;
}

bool SEASON3B::CNewUIKanturuInfoWindow::Update()
{
    if (IsVisible() && !IsInKanturu3rd())
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_KANTURU_INFO);
    if (IsVisible())
        StageContent();
    visible_ = IsVisible();
    return true;
}

float SEASON3B::CNewUIKanturuInfoWindow::GetLayerDepth()
{
    return 1.92f;
}

float SEASON3B::CNewUIKanturuInfoWindow::GetKeyEventOrder()
{
    return 9.1f;
}

void SEASON3B::CNewUIKanturuInfoWindow::SetTime(int iTimeLimit)
{
    m_iSecond = iTimeLimit / 1000;
    m_dwSyncTime = GetTickCount();
}

namespace SEASON3B
{
CNewUIRegistrationLuckyCoin::CNewUIRegistrationLuckyCoin(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), cameraProjection_(keeper.CameraProjectionObject()),
      m_CloseButton(keeper), m_RegistButton(keeper)
{
    m_width = MSGBOX_BTN_EMPTY_SMALL_WIDTH;
    m_height = MSGBOX_BTN_EMPTY_HEIGHT;
    m_RegistCount = 0;
    m_CoinItem = NULL;
    m_ItemAngle = false;
}

CNewUIRegistrationLuckyCoin::~CNewUIRegistrationLuckyCoin()
{
    Release();
}

bool CNewUIRegistrationLuckyCoin::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (pNewUIMng == NULL)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_LUCKYCOIN_REGISTRATION, this);

    SetPos(x, y);
    LoadImages();
    SetBtnInfo();
    Show(false);
    return true;
}

bool CNewUIRegistrationLuckyCoin::BtnProcess()
{
    // Top-right corner close "X" (shared frame): hides + swallows the click.
    if (g_pNewUISystem->HandleFrameCornerClose(GetPos(),
                                               SEASON3B::INTERFACE_LUCKYCOIN_REGISTRATION))
        return false;

    if (m_CloseButton.UpdateMouseEvent() == true)
    {
        if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_LUCKYCOIN_REGISTRATION) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_LUCKYCOIN_REGISTRATION);
            return true;
        }
        return false;
    }

    if (m_RegistButton.UpdateMouseEvent() == true)
    {
        g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();
        SocketClient->ToGameServer()->SendLuckyCoinRegistrationRequest();
        LockLuckyCoinRegBtn();
        return true;
    }
    return false;
}

void CNewUIRegistrationLuckyCoin::SetBtnInfo()
{
    float _x = GetPos().x + LUCKYCOIN_REG_WIDTH / 2.0f - MSGBOX_BTN_EMPTY_SMALL_WIDTH / 2.0f;
    float _y = GetPos().y + LUCKYCOIN_REG_HEIGHT - 220;

    m_RegistButton.ChangeButtonImgState(true, IMAGE_CLOSE_REGIST, true);
    m_RegistButton.ChangeButtonInfo(_x, _y, m_width, m_height);
    m_RegistButton.SetFont(LegacyFontRole::Bold);
    m_RegistButton.ChangeText(&I18N::Game::Register);
    m_CloseButton.ChangeButtonImgState(true, IMAGE_CLOSE_REGIST, true);
    m_CloseButton.ChangeButtonInfo(_x, 360, m_width, m_height);
    m_CloseButton.SetFont(LegacyFontRole::Bold);
    m_CloseButton.ChangeText(&I18N::Game::Close388);
}

bool CNewUIRegistrationLuckyCoin::Update()
{
    return true;
}

bool CNewUIRegistrationLuckyCoin::UpdateMouseEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_LUCKYCOIN_REGISTRATION) == false)
    {
        return true;
    }

    if (BtnProcess() == true)
    {
        return false;
    }

    if (CheckMouseIn(m_Pos.x, m_Pos.y, LUCKYCOIN_REG_WIDTH, LUCKYCOIN_REG_HEIGHT))
    {
        if (IsPress(VK_RBUTTON))
        {
            MouseRButton = false;
            MouseRButtonPop = false;
            MouseRButtonPush = false;
            return false;
        }

        if (IsNone(VK_LBUTTON) == false)
        {
            return false;
        }
    }
    return true;
}

bool CNewUIRegistrationLuckyCoin::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_LUCKYCOIN_REGISTRATION) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_LUCKYCOIN_REGISTRATION);
            return false;
        }
    }
    return true;
}

void CNewUIRegistrationLuckyCoin::OpeningProcess()
{
    g_pMyInventory->GetInventoryCtrl()->LockInventory();

    m_RegistCount = 0;

    UnLockLuckyCoinRegBtn();

    SocketClient->ToGameServer()->SendLuckyCoinCountRequest();

    m_CoinItem = new ITEM;
    if (m_CoinItem == NULL)
        return;
    memset(m_CoinItem, 0, sizeof(ITEM));

    m_CoinItem->Type = ITEM_POTION + 100;
    m_CoinItem->Level = 0;
    m_CoinItem->ExcellentFlags = 0;
    m_CoinItem->AncientDiscriminator = 0;
}

void CNewUIRegistrationLuckyCoin::ClosingProcess()
{
    SAFE_DELETE(m_CoinItem);
    g_pMyInventory->GetInventoryCtrl()->UnlockInventory();
    SocketClient->ToGameServer()->SendCraftingDialogCloseRequest();
}

void CNewUIRegistrationLuckyCoin::Release()
{
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

void CNewUIRegistrationLuckyCoin::LockLuckyCoinRegBtn()
{
    m_RegistButton.Lock();
    m_RegistButton.ChangeTextColor(0xff808080);
}

void CNewUIRegistrationLuckyCoin::UnLockLuckyCoinRegBtn()
{
    m_RegistButton.UnLock();
    m_RegistButton.ChangeTextColor(0xffffffff);
}
} // namespace SEASON3B

namespace
{
constexpr int SerialLength = 12, SerialPartLength = 4;
}
CNewUIGoldBowmanWindow::CNewUIGoldBowmanWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()),
      panel_(keeper, "gold_archer.rml", "Events")
{
}
CNewUIGoldBowmanWindow::~CNewUIGoldBowmanWindow()
{
    Release();
}
bool CNewUIGoldBowmanWindow::Create(CNewUIManager *manager, int x, int y)
{
    if (!manager)
        return false;
    manager_ = manager;
    manager_->AddUIObj(INTERFACE_GOLD_BOWMAN, this);
    SetPos(x, y);
    panel_.ConfigureInput("tiCertifyInput", SerialLength);
    Show(false);
    return true;
}
void CNewUIGoldBowmanWindow::Release()
{
    panel_.Release();
    locale_.clear();
    gift_.clear();
    visible_ = false;
    if (!manager_)
        return;
    manager_->RemoveUIObj(this);
    manager_ = nullptr;
}
void CNewUIGoldBowmanWindow::OpeningProcess()
{
    g_strGiftName[0] = L'\0';
    panel_.ConfigureInput("tiCertifyInput", SerialLength);
    locale_.clear();
    visible_ = true;
    StageContent();
}
void CNewUIGoldBowmanWindow::ClosingProcess()
{
    g_strGiftName[0] = L'\0';
    visible_ = false;
    SocketClient->ToGameServer()->SendEventChipExitDialog();
}

void CNewUIGoldBowmanWindow::SubmitSerial()
{
    constexpr int RewardColumns = 2, RewardRows = 4;
    if (g_pMyInventory->GetInventoryCtrl()->FindEmptySlot(RewardColumns, RewardRows) == -1)
    {
        CreateOkMessageBox(I18N::Game::LeaveAtLeastOneEmptySlotInYourInventory);
        return;
    }
    const auto &serial = panel_.InputValue();
    if (serial.size() != SerialLength)
    {
        CreateOkMessageBox(I18N::Game::EnterThe12DigitLuckyNumber);
        return;
    }
    std::array<std::array<wchar_t, SerialPartLength + 1>, SerialLength / SerialPartLength> parts{};
    for (std::size_t i = 0; i < parts.size(); ++i)
        std::copy_n(serial.data() + i * SerialPartLength, SerialPartLength, parts[i].data());
    SocketClient->ToGameServer()->SendLuckyNumberRequest(parts[0].data(), parts[1].data(),
                                                         parts[2].data());
}
bool CNewUIGoldBowmanWindow::Update()
{
    if (panel_.TakeFocus() && manager_)
        manager_->BringToFront(this);
    if (IsVisible() && panel_.TakeClick("btnClose"))
        g_pNewUISystem->Hide(INTERFACE_GOLD_BOWMAN);
    if (IsVisible())
    {
        if (panel_.TakeClick("btnCertify"))
            SubmitSerial();
        StageContent();
    }
    visible_ = IsVisible();
    return true;
}
bool CNewUIGoldBowmanWindow::UpdateMouseEvent()
{
    const auto bounds = panel_.Bounds();
    return !visible_ || !CheckMouseIn(bounds.x, bounds.y, bounds.width, bounds.height);
}
bool CNewUIGoldBowmanWindow::UpdateKeyEvent()
{
    if (!IsVisible() || !IsPress(VK_ESCAPE))
        return true;
    g_pNewUISystem->Hide(INTERFACE_GOLD_BOWMAN);
    return false;
}

float CNewUIGoldBowmanWindow::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

bool CNewUIGoldBowmanWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessPanelInput(event);
}
std::optional<UI::Modern::RmlTextInputArea> CNewUIGoldBowmanWindow::ModernTextInputArea() const
{
    return IsVisible() ? panel_.TextInputArea() : std::nullopt;
}

// Construction/Destruction

bool SEASON3B::CNewUICursedTempleEnter::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_CURSEDTEMPLE_NPC, this);

    SetPos(x, y);

    SetButtonInfo();

    Show(false);

    return true;
}

SEASON3B::CNewUICursedTempleEnter::CNewUICursedTempleEnter(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_pNewUIMng(NULL), m_EnterTime(0), m_EnterCount(0),
      m_Button(keeper)
{
    Initialize();
}

SEASON3B::CNewUICursedTempleEnter::~CNewUICursedTempleEnter()
{
    Destroy();
}

void SEASON3B::CNewUICursedTempleEnter::Initialize()
{
}

void SEASON3B::CNewUICursedTempleEnter::Destroy()
{
    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool SEASON3B::CNewUICursedTempleEnter::CheckEnterLevel(int &enterlevel)
{
    if (gCharacterManager.IsMasterLevel(Hero->Class) == true)
    {
        enterlevel = 6;
        return true;
    }

    int HeroLevel = CharacterAttribute->Level;

    for (int i = 0; i < TempleEntryDetail::EnterLevelCount; ++i)
    {
        if (HeroLevel >= TempleEntryDetail::EnterMinLevel[i] &&
            HeroLevel <= TempleEntryDetail::EnterMaxLevel[i])
        {
            enterlevel = i + 1;
            return true;
        }
    }

    return false;
}

bool SEASON3B::CNewUICursedTempleEnter::CheckEnterItem(ITEM *p, int enterlevel)
{
    if (p->Type == ITEM_HELPER + 61)
    {
        if (!CheckEnterLevel(enterlevel))
            return false;
    }
    else
    {
        if (p->Type != ITEM_SCROLL_OF_BLOOD)
            return false;

        int itemLevel = p->Level;

        if (itemLevel != enterlevel)
            return false;
    }

    if (p->Durability < 1)
        return false;

    return true;
}

bool SEASON3B::CNewUICursedTempleEnter::CheckInventory(BYTE &itempos, int enterlevel)
{
    int pos = 0;

    if (enterlevel == -1)
    {
        return false;
    }

    pos = g_pMyInventory->GetInventoryCtrl()->FindItemIndex(ITEM_SCROLL_OF_BLOOD, enterlevel);
    if (pos != -1)
    {
        itempos = pos;
        return true;
    }

    pos = g_pMyInventory->GetInventoryCtrl()->FindItemIndex(ITEM_HELPER + 61, -1);
    if (pos != -1)
    {
        itempos = pos;
        return true;
    }
    return false;
}

bool SEASON3B::CNewUICursedTempleEnter::UpdateMouseEvent()
{
    if (m_Button[CURSEDTEMPLEENTER_OPEN].UpdateMouseEvent())
    {
        int EnterLevel = -1;
        bool Result = false;

        // CheckHeroLevl
        Result = CheckEnterLevel(EnterLevel);

        if (Result)
        {
            SocketClient->ToGameServer()->SendIllusionTempleEnterRequest(
                static_cast<BYTE>(EnterLevel), 0xFF);
        }
        else
        {
            g_pSystemLogBox->AddText(I18N::Game::TheAdmissionAndScrollLevelsDoNotMatch,
                                     SEASON3B::TYPE_ERROR_MESSAGE);
        }

        return false;
    }

    if (m_Button[CURSEDTEMPLEENTER_EXIT].UpdateMouseEvent())
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_CURSEDTEMPLE_NPC);
        return false;
    }

    if (CheckMouseIn(m_Pos.x, m_Pos.y, CURSEDTEMPLE_ENTER_WINDOW_WIDTH,
                     CURSEDTEMPLE_ENTER_WINDOW_HEIGHT))
    {
        return false;
    }

    return true;
}

bool SEASON3B::CNewUICursedTempleEnter::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_CURSEDTEMPLE_NPC) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_CURSEDTEMPLE_NPC);
            return false;
        }
    }

    return true;
}

bool SEASON3B::CNewUICursedTempleEnter::Update()
{
    return true;
}

//ServerMessage
void SEASON3B::CNewUICursedTempleEnter::SetCursedTempleEnterInfo(const BYTE *cursedtempleinfo)
{
    m_EnterTime = static_cast<int>(cursedtempleinfo[0]);
    m_EnterCount = static_cast<int>(cursedtempleinfo[1]);
}

void SEASON3B::CNewUICursedTempleEnter::ReceiveCursedTempleEnterInfo(const BYTE *ReceiveBuffer)
{
    auto data = (LPPMSG_CURSED_TEMPLE_USER_COUNT)ReceiveBuffer;

    int enterlevel = -1;

    if (CheckEnterLevel(enterlevel))
    {
        if (enterlevel > 0)
        {
            m_EnterCount = data->btUserCount[enterlevel - 1];
        }
    }
}

namespace TemplePanelDetail
{

//#ifdef _DEBUG

//#endif //_DEBUG

float MiniMapPos(float pointX, float pointY, float scale, int aXis)
{
    float minmapframeposX = 464.f, minmapframeposY = 299.f;

    if (aXis == TemplePanelDetail::AXIS_X)
    {
        float ridY = TemplePanelDetail::posY[6] - pointY;
        return ((pointX - ridY) / scale) + minmapframeposX;
    }
    else
    {
        float ridX = TemplePanelDetail::posX[6] - pointX;
        return (((125 - (pointY + ridX))) / scale) + minmapframeposY;
    }
}

} // namespace TemplePanelDetail

bool SEASON3B::CNewUICursedTempleSystem::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_CURSEDTEMPLE_GAMESYSTEM, this);

    SetPos(x, y);

    SetButtonInfo();

    Show(false);

    return true;
}

SEASON3B::CNewUICursedTempleSystem::CNewUICursedTempleSystem(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()), infoPanel_(keeper),
      scorePanel_(keeper), sessionKeeper_(keeper), gSkillManager(keeper.SkillManagerObject()),
      m_pNewUIMng(NULL), m_Button(keeper)
{
    Initialize();
}

SEASON3B::CNewUICursedTempleSystem::~CNewUICursedTempleSystem()
{
    Destroy();
}

void SEASON3B::CNewUICursedTempleSystem::Initialize()
{
    LoadImages();

    ResetCursedTempleSystemInfo();
}

void SEASON3B::CNewUICursedTempleSystem::Destroy()
{
    infoPanel_.Release();
    scorePanel_.Release();
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

void SEASON3B::CNewUICursedTempleSystem::ResetCursedTempleSystemInfo()
{
    m_EventMapTime = 0;
    m_HolyItemPlayerIndex = 0xffff;
    m_HolyItemPlayerPosX = 0;
    m_HolyItemPlayerPosY = 0;
    m_AlliedPoint = 0;
    m_IllusionPoint = 0;
    m_MyTeam = SEASON3A::eTeam_Count;
    m_CursedTempleMyTeamCount = 0;
    m_Scale = 2.5f;
    m_Alph = 1.0f;
    m_SkillPoint = 0;
    m_IsTutorialStep = false;
    m_TutorialStepState = 0;
    m_TutorialStepTime = 0;

    EndScoreEffect();

    for (int i = 0; i < MAX_PARTYS; ++i)
    {
        m_CursedTempleMyTeam[i].wPartyUserIndex = 0xffff;
        m_CursedTempleMyTeam[i].btX = 0;
        m_CursedTempleMyTeam[i].btY = 0;
        m_CursedTempleMyTeam[i].byMapNumber = 0xff;
    }
}

void SEASON3B::CNewUICursedTempleSystem::StartScoreEffect()
{
    if (m_IsScoreEffect)
        return;
    m_StartScoreEffectTime = timeGetTime();
    m_IsScoreEffect = true;
}

void SEASON3B::CNewUICursedTempleSystem::EndScoreEffect()
{
    m_StartScoreEffectTime = 0;
    m_ScoreEffectAlph = 0.0f;
    m_ScoreEffectState = 0;
    m_IsScoreEffect = false;
}

void SEASON3B::CNewUICursedTempleSystem::StartTutorialStep()
{
    m_IsTutorialStep = true;
    m_TutorialStepState = 0;
    m_TutorialStepTime = timeGetTime();
}

void SEASON3B::CNewUICursedTempleSystem::EndTutorialStep()
{
    m_IsTutorialStep = false;
    m_TutorialStepState = 0;
    m_TutorialStepTime = 0;
}

SEASON3A::eCursedTempleTeam SEASON3B::CNewUICursedTempleSystem::GetMyTeam()
{
    return m_MyTeam;
}

bool SEASON3B::CNewUICursedTempleSystem::CheckTalkProgressNpc(DWORD npcindex, DWORD npckey)
{
    std::list<DWORD> progressnpcindexlist;
    progressnpcindexlist.push_back(TemplePanelDetail::HolyItemNpc);
    progressnpcindexlist.push_back(TemplePanelDetail::AlliedHolyItemBoxNpc);
    progressnpcindexlist.push_back(TemplePanelDetail::IllusionHolyItemBoxNpc);

    for (auto iter = progressnpcindexlist.begin(); iter != progressnpcindexlist.end();)
    {
        auto curiter = iter;
        ++iter;
        DWORD progressnpcindex = *curiter;

        if (progressnpcindex == npcindex)
        {
            if (progressnpcindex == TemplePanelDetail::AlliedHolyItemBoxNpc ||
                progressnpcindex == TemplePanelDetail::IllusionHolyItemBoxNpc)
            {
                if (!(progressnpcindex == TemplePanelDetail::AlliedHolyItemBoxNpc &&
                      SEASON3A::eTeam_Allied == m_MyTeam) &&
                    !(progressnpcindex == TemplePanelDetail::IllusionHolyItemBoxNpc &&
                      SEASON3A::eTeam_Illusion == m_MyTeam))
                {
                    return false;
                }

                if (sessionKeeper_.CursedTempleObject().CheckInventoryHolyItem(Hero))
                {
                    SEASON3B::CCursedTempleProgressMsgBox *pMsgBox = NULL;
                    SEASON3B::CreateMessageBox(
                        MSGBOX_LAYOUT_CLASS(SEASON3B::CCursedTempleHolicItemSaveLayout,
                                            sessionKeeper_),
                        &pMsgBox);
                    if (pMsgBox)
                    {
                        pMsgBox->SetNpcIndex(npckey);
                    }
                }
                else
                {
                    g_pSystemLogBox->AddText(I18N::Game::NoItem, SEASON3B::TYPE_ERROR_MESSAGE);
                }
            }
            else
            {
                SEASON3B::CCursedTempleProgressMsgBox *pMsgBox = NULL;
                SEASON3B::CreateMessageBox(
                    MSGBOX_LAYOUT_CLASS(SEASON3B::CCursedTempleHolicItemGetLayout, sessionKeeper_),
                    &pMsgBox);
                if (pMsgBox)
                {
                    pMsgBox->SetNpcIndex(npckey);
                }
            }

            return true;
        }
    }

    if (npcindex == TemplePanelDetail::AlliedNpc || npcindex == TemplePanelDetail::IllusionNpc)
    {
        if (g_MessageBox.IsEmpty())
        {
            if (npcindex == TemplePanelDetail::AlliedNpc)
                CreateOkMessageBox(I18N::Game::WeHaveEnteredTheHeartOf);

            if (npcindex == TemplePanelDetail::IllusionNpc)
                CreateOkMessageBox(I18N::Game::ListenToThisTheAlliesHave);
        }

        return true;
    }

    SocketClient->ToGameServer()->SendTalkToNpcRequest(npckey);
    return false;
}

bool SEASON3B::CNewUICursedTempleSystem::CheckHeroSkillType(int operatortype)
{
    if (operatortype == 0)
    {
        if (Hero->m_CursedTempleCurSkill >= AT_SKILL_CURSED_TEMPLE_SUBLIMATION)
            return false;
        else
            return true;
    }
    else
    {
        if (Hero->m_CursedTempleCurSkill <= AT_SKILL_CURSED_TEMPLE_PRODECTION)
            return false;
        else
            return true;
    }
}

bool SEASON3B::CNewUICursedTempleSystem::CheckDragonRender()
{
    if (IsVisible())
    {
        return Hero->SafeZone ? false : true;
    }
    else
    {
        return false;
    }
}

bool SEASON3B::CNewUICursedTempleSystem::UpdateMouseEvent()
{
    if (m_Button[CURSEDTEMPLERESULT_ALPH].UpdateMouseEvent())
    {
        if (m_Alph != 1.0f)
        {
            m_Alph = 1.0f;
        }
        else
        {
            m_Alph = 0.51f;
        }

        return false;
    }

    if (MouseWheel >= 1)
    {
        if (CheckHeroSkillType())
        {
            Hero->m_CursedTempleCurSkill += 1;
        }

        MouseWheel = 0;

        return false;
    }

    if (MouseWheel <= -1)
    {
        if (CheckHeroSkillType(1))
        {
            Hero->m_CursedTempleCurSkill -= 1;
        }

        MouseWheel = 0;

        return false;
    }

    if (CheckMouseIn(512, 232, 128, 165))
    {
        return false;
    }

    return true;
}

bool SEASON3B::CNewUICursedTempleSystem::UpdateKeyEvent()
{
    return true;
}

void SEASON3B::CNewUICursedTempleSystem::UpdateScore()
{
    if (m_IsScoreEffect && timeGetTime() - m_StartScoreEffectTime >= scorePanel_.HoldMilliseconds())
        EndScoreEffect();
}

void SEASON3B::CNewUICursedTempleSystem::UpdateTutorialStep()
{
    if (!m_IsTutorialStep)
        return;

    DWORD curTime = timeGetTime();
    if (curTime - m_TutorialStepTime >= 10000)
    {
        m_TutorialStepState += 1;
        m_TutorialStepTime = curTime;

        if (m_TutorialStepState == 3)
        {
            EndTutorialStep();
        }
    }
}

bool SEASON3B::CNewUICursedTempleSystem::Update()
{
    UpdateScore();
    UpdateTutorialStep();
    const auto changes = infoPanel_.TakeChanges();
    if (IsVisible())
    {
        if (changes.selected)
            Hero->m_CursedTempleCurSkill = AT_SKILL_CURSED_TEMPLE_PRODECTION + *changes.selected;
        if (changes.wheel > 0 && CheckHeroSkillType())
            ++Hero->m_CursedTempleCurSkill;
        if (changes.wheel < 0 && CheckHeroSkillType(1))
            --Hero->m_CursedTempleCurSkill;
    }
    if (!gMapManager.IsCursedTemple() && IsVisible())
        g_pNewUISystem->Hide(INTERFACE_CURSEDTEMPLE_GAMESYSTEM);
    modernScoreVisible_ = IsVisible() && m_IsScoreEffect;
    modernVisible_ =
        IsVisible() && (!g_pCharacterInfoWindow->IsVisible() || !g_pMyInventory->IsVisible() ||
                        !g_pGuildInfoWindow->IsVisible() || !g_pWindowMgr->IsVisible() ||
                        !g_pMyQuestInfoWindow->IsVisible());
    if (IsVisible())
        StageModernContent();
    return true;
}

bool SEASON3B::CNewUICursedTempleSystem::ProcessModernUiInput(const SessionInputEvent &event)
{
    if (!IsVisible())
        return false;
    scorePanel_.ProcessInput(event);
    return infoPanel_.ProcessInput(event);
}

void SEASON3B::CNewUICursedTempleSystem::ReceiveCursedTempleInfo(const BYTE *ReceiveBuffer)
{
    auto data = (LPPMSG_CURSED_TAMPLE_STATE)ReceiveBuffer;

    m_EventMapTime = data->wRemainSec;

    if (data->btUserIndex == 0xffff)
    {
        memset(&m_HolyItemPlayerName, 0, sizeof(char));
    }

    m_HolyItemPlayerIndex = data->btUserIndex;
    m_HolyItemPlayerPosX = data->btX;
    m_HolyItemPlayerPosY = data->btY;
    m_MyTeam = static_cast<SEASON3A::eCursedTempleTeam>(data->btMyTeam);

    wchar_t message[200];
    memset(&message, 0, sizeof(char));

    if (m_MyTeam == SEASON3A::eTeam_Allied)
    {
        if (m_AlliedPoint != data->btAlliedPoint)
        {
            PlayBuffer(SOUND_CURSEDTEMPLE_GAMESYSTEM4);
            StartScoreEffect();
            g_pSystemLogBox->AddText(
                I18N::Game::TheAlliesAreAdvancingOnWeAreNotFarFromTheVictoryChargeOn,
                SEASON3B::TYPE_ERROR_MESSAGE);
        }
        else if (m_IllusionPoint != data->btIllusionPoint)
        {
            PlayBuffer(SOUND_CURSEDTEMPLE_GAMESYSTEM4);
            StartScoreEffect();
            g_pSystemLogBox->AddText(I18N::Game::AlthoughWeHaveLostThisBattle,
                                     SEASON3B::TYPE_ERROR_MESSAGE);
        }
    }
    else
    {
        if (m_IllusionPoint != data->btIllusionPoint)
        {
            PlayBuffer(SOUND_CURSEDTEMPLE_GAMESYSTEM4);
            StartScoreEffect();
            g_pSystemLogBox->AddText(I18N::Game::HoorayForTheIllusionSorceryWe,
                                     SEASON3B::TYPE_ERROR_MESSAGE);
        }
        else if (m_AlliedPoint != data->btAlliedPoint)
        {
            PlayBuffer(SOUND_CURSEDTEMPLE_GAMESYSTEM4);
            StartScoreEffect();
            g_pSystemLogBox->AddText(I18N::Game::YouMustNotLoseTheTemple,
                                     SEASON3B::TYPE_ERROR_MESSAGE);
        }
    }

    m_AlliedPoint = data->btAlliedPoint;
    m_IllusionPoint = data->btIllusionPoint;

    m_CursedTempleMyTeamCount = std::min<WORD>(data->btPartyCount, MAX_PARTYS);

    int Offset = sizeof(PMSG_CURSED_TAMPLE_STATE);

    for (int i = 0; i < m_CursedTempleMyTeamCount; i++)
    {
        auto data2 = (LPPMSG_CURSED_TAMPLE_PARTY_POS)(ReceiveBuffer + Offset);

        if (data2->wPartyUserIndex != 0xffff)
        {
            PMSG_CURSED_TAMPLE_PARTY_POS *p = &m_CursedTempleMyTeam[i];

            p->wPartyUserIndex = data2->wPartyUserIndex;
            p->byMapNumber = data2->byMapNumber;
            p->btX = data2->btX;
            p->btY = data2->btY;
        }

        Offset += sizeof(PMSG_CURSED_TAMPLE_PARTY_POS);
    }
}

void SEASON3B::CNewUICursedTempleSystem::ReceiveCursedTempSkillPoint(const BYTE *ReceiveBuffer)
{
    auto data = (LPPMSG_CURSED_TEMPLE_SKILL_POINT)ReceiveBuffer;

    if (m_SkillPoint < data->btSkillPoint)
    {
        wchar_t message[100];
        memset(&message, 0, sizeof(char));
        mu_swprintf(message, I18N::Game::KillPointDAchieved, data->btSkillPoint - m_SkillPoint);
        g_pSystemLogBox->AddText(message, SEASON3B::TYPE_SYSTEM_MESSAGE);
    }

    m_SkillPoint = data->btSkillPoint;
}

void SEASON3B::CNewUICursedTempleSystem::ReceiveCursedTempleHolyItemRelics(
    const BYTE *ReceiveBuffer)
{
    auto data = (LPPMSG_RELICS_GET_USER)ReceiveBuffer;
}

// Symmetric counterpart to GiveFocus(): drops keyboard focus from the focused
// portable text field without hiding or destroying it. GiveFocus() sets both
// s_pFocusedPortable and g_dwKeyFocusUIID, so release both here (clearing the
// key-focus id only while it still points at this field, to avoid stomping
// another widget), letting the field hand focus back to the game window while
// staying visible.
// and wherever a paragraph exceeds the box width (wrapped at the last space, or
// mid-word when a single word is too long). Each span is [start, end) in buffer
// indices; end excludes the wrapped space or newline.

CUIBCDeclareGuildListBox::CUIBCDeclareGuildListBox(SessionKeeper &keeper)
    : CUITextListBox<BCDECLAREGUILD_TEXT>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 18;

    m_fScrollBarRange_top = 0;
    m_fScrollBarRange_bottom = 0;

    m_fScrollBarPos_y = 0;
    m_fScrollBarWidth = 13;
    m_fScrollBarHeight = 0;

    m_fScrollBarClickPos_y = 0;
    m_bScrollBtnClick = FALSE;
    m_bScrollBarClick = FALSE;

    m_bUseSelectLine = TRUE;

    m_bUseNewUIScrollBar = TRUE;

    SetPosition(465, 364);
    SetSize(160, 235);
}

void CUIBCDeclareGuildListBox::AddText(const wchar_t *szGuildName, int nMarkCount, BYTE byIsGiveUp,
                                       BYTE bySeqNum)
{
    if (szGuildName == nullptr || szGuildName[0] == '\0')
        return;

    BCDECLAREGUILD_TEXT text{};
    text.m_bIsSelected = FALSE;
    wcsncpy(text.szName, szGuildName, MAX_GUILDNAME);
    text.szName[MAX_GUILDNAME] = 0;
    text.nCount = nMarkCount;
    text.byIsGiveUp = byIsGiveUp;
    text.bySeqNum = bySeqNum;
    m_TextList.push_front(text);

    RemoveText();
    SLSetSelectLine(0);
    if (GetLineNum() > m_iNumRenderLine)
        ++m_iCurrentRenderEndLine;

    //	if (m_iCurrentRenderEndLine != 0) ++m_iCurrentRenderEndLine;

    if (GetLineNum() < m_iNumRenderLine)
        ;
    else if (GetLineNum() - m_iCurrentRenderEndLine < m_iNumRenderLine)
        m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;

    if (m_TextList.size() == 1)
        SLSetSelectLine(1);

    if (m_TextList.empty() == FALSE)
    {
        SLSetSelectLine(GetLineNum());
        //		m_iCurrentRenderEndLine = 0;
        Scrolling(-10000);
    }
}

bool BCDeclareGuildSortByMark(const BCDECLAREGUILD_TEXT &lhs, const BCDECLAREGUILD_TEXT &rhs)
{
    return (lhs.nCount < rhs.nCount);
}

void CUIBCDeclareGuildListBox::Sort()
{
    sort(m_TextList.begin(), m_TextList.end(), BCDeclareGuildSortByMark);
}

void CUIBCDeclareGuildListBox::SetNumRenderLine(int iLine)
{
    if (iLine < m_iNumRenderLine && iLine < GetLineNum())
        ++m_iCurrentRenderEndLine;
    else if (iLine > GetLineNum())
        m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = iLine;
}

int CUIBCDeclareGuildListBox::GetRenderLinePos_y(int iLineNumber)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * 13 - iLineNumber * 13 + 3);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * 13 - iLineNumber * 13 + 3);
}

BOOL CUIBCDeclareGuildListBox::DoLineMouseAction(int iLineNumber)
{
    if (CheckMouseIn(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3,
                     m_iWidth - m_fScrollBarWidth + 1, 13))
    {
        if (MouseLButtonPush)
        {
            SLSetSelectLine(m_iCurrentRenderEndLine + iLineNumber + 1);
            m_TextListIter->m_bIsSelected = (m_TextListIter->m_bIsSelected + 1) % 2; // T/F Reverse
            MouseLButtonPush = false;
        }
    }
    return TRUE;
}

void CUIBCDeclareGuildListBox::DeleteText(DWORD dwGuildIndex)
{
}

CUIBCGuildListBox::CUIBCGuildListBox(SessionKeeper &keeper) : CUITextListBox<BCGUILD_TEXT>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 15;

    m_fScrollBarRange_top = 0;
    m_fScrollBarRange_bottom = 0;

    m_fScrollBarPos_y = 0;
    m_fScrollBarWidth = 13;
    m_fScrollBarHeight = 0;

    m_fScrollBarClickPos_y = 0;
    m_bScrollBtnClick = FALSE;
    m_bScrollBarClick = FALSE;

    m_bUseSelectLine = TRUE;

    Select_Guild = -1;
    m_bUseNewUIScrollBar = TRUE;

    SetPosition(465, 324);
    SetSize(160, 195);
}

void CUIBCGuildListBox::AddText(const wchar_t *szGuildName, BYTE byJoinSide, BYTE byGuildInvolved,
                                int iGuildScore)
{
    if (szGuildName == nullptr || szGuildName[0] == '\0')
        return;

    BCGUILD_TEXT text{};
    text.m_bIsSelected = FALSE;
    wcsncpy(text.szName, szGuildName, MAX_GUILDNAME);
    text.szName[MAX_GUILDNAME] = 0;
    text.byJoinSide = byJoinSide;
    text.byGuildInvolved = byGuildInvolved;
    text.iGuildScore = iGuildScore;
    m_TextList.push_front(text);

    RemoveText();
    SLSetSelectLine(0);
    if (GetLineNum() > m_iNumRenderLine)
        ++m_iCurrentRenderEndLine;

    //	if (m_iCurrentRenderEndLine != 0) ++m_iCurrentRenderEndLine;

    if (GetLineNum() < m_iNumRenderLine)
        ;
    else if (GetLineNum() - m_iCurrentRenderEndLine < m_iNumRenderLine)
        m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;

    if (m_TextList.size() == 1)
        SLSetSelectLine(1);

    if (m_TextList.empty() == FALSE)
    {
        SLSetSelectLine(GetLineNum());
        //		m_iCurrentRenderEndLine = 0;
        Scrolling(-10000);
    }
}

void CUIBCGuildListBox::SetNumRenderLine(int iLine)
{
    if (iLine < m_iNumRenderLine && iLine < GetLineNum())
        ++m_iCurrentRenderEndLine;
    else if (iLine > GetLineNum())
        m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = iLine;
}

int CUIBCGuildListBox::GetRenderLinePos_y(int iLineNumber)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * 13 - iLineNumber * 13 + 3);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * 13 - iLineNumber * 13 + 3);
}

BOOL CUIBCGuildListBox::DoLineMouseAction(int iLineNumber)
{
    if (CheckMouseIn(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3,
                     m_iWidth - m_fScrollBarWidth + 1, 13))
    {
        if (MouseLButtonPush)
        {
            Select_Guild = iLineNumber;

            SLSetSelectLine(m_iCurrentRenderEndLine + iLineNumber + 1);
            m_TextListIter->m_bIsSelected = (m_TextListIter->m_bIsSelected + 1) % 2; // T/F Reverse
            MouseLButtonPush = false;
        }
    }
    return TRUE;
}

void CUIBCGuildListBox::DeleteText(DWORD dwGuildIndex)
{
}
