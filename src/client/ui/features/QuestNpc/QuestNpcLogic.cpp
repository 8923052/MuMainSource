#include "ui/features/QuestNpc/QuestNpcLogic.h"
#include "I18N/All.h"
#include "app/AppWindow.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/Quests.h"
#include "domain/Shop.h"
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
#include "support/CoreMath.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/QuestNpc/QuestNpcRender.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"
#include "ui/session/UiSessionRender.h"

//  UIGateKeeper.cpp

CUIGateKeeper::CUIGateKeeper()
{
    m_bPublic = false;
    m_byType = TOUCH_TYPE_NONE;
    m_nEntranceFee = 0;
    m_iAddEntranceFee = 0;
    m_iMaxEnteranceFee = 0;

    m_iViewEntranceFee = m_nEntranceFee;
}

CUIGateKeeper::~CUIGateKeeper()
{
}

void CUIGateKeeper::SendPublicSetting()
{
}

void CUIGateKeeper::SendEnteranceFee()
{
}

void CUIGateKeeper::EnteranceFeeUp()
{
    m_iViewEntranceFee += m_iAddEntranceFee;
    if (m_iViewEntranceFee > m_iMaxEnteranceFee)
    {
        m_iViewEntranceFee = m_iMaxEnteranceFee;
    }
}

void CUIGateKeeper::EnteranceFeeDown()
{
    m_iViewEntranceFee -= m_iAddEntranceFee;
    if (m_iViewEntranceFee < 0)
    {
        m_iViewEntranceFee = 0;
    }
}

void CUIGateKeeper::SendEnter()
{
}

SEASON3B::CNewUIQuestProgressByEtc::CNewUIQuestProgressByEtc(SessionKeeper &keeper)
    : CNewUIQuestProgress(keeper, true)
{
}

using namespace SEASON3B;
CNewUIEmpireGuardianNPC::CNewUIEmpireGuardianNPC(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()),
      panel_(keeper, "empire_guardian_entry.rml", "Events")
{
}
CNewUIEmpireGuardianNPC::~CNewUIEmpireGuardianNPC()
{
    Release();
}
bool CNewUIEmpireGuardianNPC::Create(CNewUIManager *manager, CNewUI3DRenderMng *renderManager, int,
                                     int)
{
    if (!manager || !renderManager || !g_pNewItemMng)
        return false;
    manager_ = manager;
    renderManager_ = renderManager;
    manager_->AddUIObj(INTERFACE_EMPIREGUARDIAN_NPC, this);
    renderManager_->Add3DRenderObj(this, INVENTORY_CAMERA_Z_ORDER);
    Show(false);
    return true;
}
void CNewUIEmpireGuardianNPC::Release()
{
    panel_.Release();
    if (renderManager_)
        renderManager_->Remove3DRenderObj(this);
    if (manager_)
        manager_->RemoveUIObj(this);
    renderManager_ = nullptr;
    manager_ = nullptr;
}

bool CNewUIEmpireGuardianNPC::Update()
{
    if (panel_.TakeFocus() && manager_)
        manager_->BringToFront(this);
    const bool close = panel_.TakeClick("btnClose"), enter = panel_.TakeClick("btnEnter");
    if (IsVisible() && close)
        g_pNewUISystem->Hide(INTERFACE_EMPIREGUARDIAN_NPC);
    if (IsVisible())
    {
        if (enter)
        {
            SocketClient->ToGameServer()->SendEnterEmpireGuardianEvent();
            PlayBuffer(SOUND_INTERFACE01);
        }
        StageContent();
    }
    visible_ = IsVisible();
    return true;
}
bool CNewUIEmpireGuardianNPC::UpdateMouseEvent()
{
    const auto bounds = panel_.Bounds();
    return !CheckMouseIn(bounds.x, bounds.y, bounds.width, bounds.height);
}
bool CNewUIEmpireGuardianNPC::UpdateKeyEvent()
{
    if (!IsVisible() || !IsPress(VK_ESCAPE))
        return true;
    g_pNewUISystem->Hide(INTERFACE_EMPIREGUARDIAN_NPC);
    PlayBuffer(SOUND_CLICK01);
    return false;
}
bool CNewUIEmpireGuardianNPC::IsVisible() const
{
    return CNewUIObj::IsVisible();
}

float CNewUIEmpireGuardianNPC::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}
void CNewUIEmpireGuardianNPC::OpenningProcess()
{
    visible_ = true;
    StageContent();
}
void CNewUIEmpireGuardianNPC::ClosingProcess()
{
    visible_ = false;
}

bool CNewUIEmpireGuardianNPC::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessPanelInput(event);
}

CNewUIEmpireGuardianTimer::CNewUIEmpireGuardianTimer(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper)
{
    m_pNewUIMng = NULL;
    m_Pos.x = m_Pos.y = 0;
    m_dTime = 600000;
    m_iType = 1;
    m_iDay = EG_MONDAY; //EG_DAY_MAP_LIST::EG_MONDAY;
    m_iZone = 1;
    m_iMonsterCount = 0;
}

CNewUIEmpireGuardianTimer::~CNewUIEmpireGuardianTimer()
{
    Release();
}

bool CNewUIEmpireGuardianTimer::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_EMPIREGUARDIAN_TIMER, this);

    SetPos(x, y);

    LoadImages();

    Show(false);

    return true;
}

void CNewUIEmpireGuardianTimer::Release()
{
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool CNewUIEmpireGuardianTimer::UpdateMouseEvent()
{
    if (true == BtnProcess())
        return false;
    return true;
}

bool CNewUIEmpireGuardianTimer::UpdateKeyEvent()
{
    return true;
}

bool CNewUIEmpireGuardianTimer::Update()
{
    if (!IsVisible())
        return true;

    return true;
}

bool CNewUIEmpireGuardianTimer::BtnProcess()
{
    return false;
}

float CNewUIEmpireGuardianTimer::GetLayerDepth()
{
    return 1.2f;
}

void CNewUIEmpireGuardianTimer::OpenningProcess()
{
}

void CNewUIEmpireGuardianTimer::ClosingProcess()
{
}

SEASON3B::CNewUINPCShop::CNewUINPCShop(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderUnit(RendererForConstruction()),
      m_ModernPanel(keeper, "npc_shop.rml")
{
    Init();
}

SEASON3B::CNewUINPCShop::~CNewUINPCShop()
{
    Release();
}

void SEASON3B::CNewUINPCShop::Init()
{
    m_pNewUIMng = NULL;
    m_pNewInventoryCtrl = NULL;
    m_Pos.x = m_Pos.y = 0;
    m_dwShopState = SHOP_STATE_BUYNSELL;
    m_iTaxRate = 0;
    m_bRepairShop = false;
    m_bIsNPCShopOpen = false;
    m_dwStandbyItemKey = 0;
    m_bSellingItem = false;
}

bool SEASON3B::CNewUINPCShop::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng || NULL == g_pNewItemMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_NPCSHOP, this);

    m_pNewInventoryCtrl = renderUnit.CreateInventoryControl();
    if (false ==
        m_pNewInventoryCtrl->Create(GameDataForConstruction().Inventory(InventoryRole::NpcShop),
                                    g_pNewUI3DRenderMng, this, 0, 0))
    {
        SAFE_DELETE(m_pNewInventoryCtrl);
        return false;
    }

    if (m_pNewInventoryCtrl)
    {
        m_pNewInventoryCtrl->SetToolTipType(TOOLTIP_TYPE_NPC_SHOP);
        m_pNewInventoryCtrl->SetOwnerRendered(true);
        m_pNewInventoryCtrl->SetRenderSlotFrame(false);
    }

    SetPos(x, y);

    Show(false);

    return true;
}

void SEASON3B::CNewUINPCShop::Release()
{
    m_ModernPanel.Release();

    SAFE_DELETE(m_pNewInventoryCtrl);

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool SEASON3B::CNewUINPCShop::UpdateMouseEvent()
{
    SyncModernGeometry();
    if (m_pNewInventoryCtrl)
    {
        if (false == m_pNewInventoryCtrl->UpdateMouseEvent())
        {
            return false;
        }

        if (InventoryProcess() == true)
        {
            return false;
        }

        if (m_pNewInventoryCtrl->CheckPtInRect(MouseX, MouseY) == true)
        {
            ITEM *pItem = m_pNewInventoryCtrl->FindItemAtPt(MouseX, MouseY);

            if ((m_bIsNPCShopOpen == true) && (pItem) && (IsRelease(VK_LBUTTON)))
            {
                int iIndex = (pItem->y * m_pNewInventoryCtrl->GetNumberOfColumn()) + pItem->x;
                GambleSystem &_gambleSys = g_GambleSystem;

                if (_gambleSys.IsGambleShop())
                {
                    _gambleSys.SetBuyItemInfo(iIndex, ItemValue(pItem, 0));
                    g_pNPCShop->SetStandbyItemKey(pItem->Key);

                    SEASON3B::CreateGambleBuyMessageBox(SessionOrigin());

                    return false;
                }
                if (BuyCost == 0)
                {
                    SocketClient->ToGameServer()->SendBuyItemFromNpcRequest(iIndex);
                    BuyCost = ItemValue(pItem, 0);
                    g_ConsoleDebug.Write(MCD_SEND, L"0x32 [SendRequestBuy(%d)]", iIndex);
                }

                return false;
            }
            if (IsRelease(VK_LBUTTON))
            {
                m_bIsNPCShopOpen = true;
                return false;
            }
            if (IsPress(VK_LBUTTON))
            {
                return false;
            }
        }
    }

    if (BtnProcess() == true)
    {
        return false;
    }

    if (IsMouseInModernPanel())
    {
        if (IsNone(VK_LBUTTON) == false)
        {
            return false;
        }
    }

    return true;
}

bool SEASON3B::CNewUINPCShop::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_NPCSHOP) == false)
    {
        return true;
    }

    if (m_bRepairShop && IsRepeat(VK_SHIFT) && IsPress('L'))
    {
        SocketClient->ToGameServer()->SendRepairItemRequest(0xFF, 0);
        return false;
    }
    if (IsPress('L'))
    {
        if (m_bRepairShop && g_pMyInventory->GetInventoryCtrl()->GetPickedItem() == NULL)
        {
            ToggleState();
            return false;
        }
    }

    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_NPCSHOP) == true)
    {
        if (IsPress(VK_ESCAPE) == true && m_bSellingItem == false)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_NPCSHOP);
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }
    return true;
}

bool SEASON3B::CNewUINPCShop::Update()
{
    if (m_ModernPanel.TakeFocus() && m_pNewUIMng)
        m_pNewUIMng->BringToFront(this);
    BtnProcess();
    m_ModernPanel.SetVisible(IsVisible());
    if (m_pNewInventoryCtrl && false == m_pNewInventoryCtrl->Update())
    {
        return false;
    }
    if (IsVisible())
    {
        SyncModernGeometry();
        StageModernContent();
    }
    if (m_bRepairShop)
    {
        RepairAllGold();
    }
    return true;
}

float SEASON3B::CNewUINPCShop::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

void SEASON3B::CNewUINPCShop::SetTaxRate(int iTaxRate)
{
    m_iTaxRate = iTaxRate;
}

int SEASON3B::CNewUINPCShop::GetTaxRate()
{
    return m_iTaxRate;
}

bool SEASON3B::CNewUINPCShop::InsertItem(int iIndex, std::span<const BYTE> pbyItemPacket)
{
    if (m_pNewInventoryCtrl)
    {
        return m_pNewInventoryCtrl->AddItem(iIndex, pbyItemPacket);
    }

    return false;
}

bool SEASON3B::CNewUINPCShop::InventoryProcess()
{
    CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();

    if (!m_pNewInventoryCtrl)
        return false;
    if (!pPickedItem)
        return false;
    ITEM *pItem = pPickedItem->GetItem();

    if (IsSellingBan(pItem))
        m_pNewInventoryCtrl->SetSquareColorNormal(1.0f, 0.0f, 0.0f);
    else
        m_pNewInventoryCtrl->SetSquareColorNormal(0.1f, 0.4f, 0.8f);

    if (IsRelease(VK_LBUTTON) == true &&
        m_pNewInventoryCtrl->CheckPtInRect(MouseX, MouseY) == true && m_bSellingItem == false)
    {
        if (CharacterMachine->Gold + ItemValue(pItem) > 2000000000)
        {
            g_pSystemLogBox->AddText(I18N::Game::ExceededMaximumAmountOfZenYouCanPossess,
                                     SEASON3B::TYPE_SYSTEM_MESSAGE);

            return true;
        }

        if (pItem && pItem->Jewel_Of_Harmony_Option != 0)
        {
            g_pSystemLogBox->AddText(I18N::Game::ReinforcedItemCanTBeSold,
                                     SEASON3B::TYPE_ERROR_MESSAGE);

            return true;
        }
        if (pItem && IsSellingBan(pItem) == true)
        {
            g_pSystemLogBox->AddText(I18N::Game::TheseItemsCannotBeTraded,
                                     SEASON3B::TYPE_ERROR_MESSAGE);
            m_pNewInventoryCtrl->BackupPickedItem();

            return true;
        }
        if (pItem && IsHighValueItem(pItem) == true)
        {
            SEASON3B::CreateMessageBox(
                MSGBOX_LAYOUT_CLASS(SEASON3B::CHighValueItemCheckMsgBoxLayout, SessionOrigin()));
            pPickedItem->HidePickedItem();

            return true;
        }

        if (pPickedItem->GetSourceStorageType() == STORAGE_TYPE::INVENTORY)
        {
            const int iSourceIndex = pPickedItem->GetSourceLinealPos();
            if (iSourceIndex >= MAX_EQUIPMENT_INDEX && iSourceIndex < MAX_MY_INVENTORY_EX_INDEX)
            {
                SocketClient->ToGameServer()->SendSellItemToNpcRequest(iSourceIndex);
                g_pNPCShop->SetSellingItem(true);
                return true;
            }
        }
    }

    return false;
}

bool SEASON3B::CNewUINPCShop::BtnProcess()
{
    if (m_ModernPanel.TakeClick("btnClose") && !m_bSellingItem)
    {
        g_pNewUISystem->Hide(INTERFACE_NPCSHOP);
        PlayBuffer(SOUND_CLICK01);
        return true;
    }
    if (!m_bRepairShop)
        return false;
    if (m_ModernPanel.TakeClick("btnRepair"))
    {
        ToggleState();
        return true;
    }
    if (m_ModernPanel.TakeClick("btnRepairAll"))
    {
        SocketClient->ToGameServer()->SendRepairItemRequest(0xFF, 0);
        return true;
    }
    return false;
}

void SEASON3B::CNewUINPCShop::DeleteAllItems()
{
    if (m_pNewInventoryCtrl)
        m_pNewInventoryCtrl->RemoveAllItems();
}

void SEASON3B::CNewUINPCShop::OpenningProcess()
{
    if (IsRepeat(VK_LBUTTON))
    {
        m_bIsNPCShopOpen = false;
    }
    else
    {
        m_bIsNPCShopOpen = true;
    }
}

void SEASON3B::CNewUINPCShop::ClosingProcess()
{
    SocketClient->ToGameServer()->SendCloseNpcRequest();

    m_dwShopState = SHOP_STATE_BUYNSELL;
    m_iTaxRate = 0;
    m_bRepairShop = false;
    m_dwStandbyItemKey = 0;

    m_bIsNPCShopOpen = false;

    if (m_pNewInventoryCtrl)
    {
        m_pNewInventoryCtrl->RemoveAllItems();
    }

    g_GambleSystem.SetGambleShop(false);
    m_bSellingItem = false;
}

void SEASON3B::CNewUINPCShop::SetRepairShop(bool bRepair)
{
    m_bRepairShop = bRepair;
}

bool SEASON3B::CNewUINPCShop::IsRepairShop()
{
    return m_bRepairShop;
}

void SEASON3B::CNewUINPCShop::ToggleState()
{
    if (m_dwShopState == SHOP_STATE_BUYNSELL)
    {
        m_dwShopState = SHOP_STATE_REPAIR;

        g_pMyInventory->SetRepairMode(true);
    }
    else
    {
        m_dwShopState = SHOP_STATE_BUYNSELL;
        g_pMyInventory->SetRepairMode(false);
    }
}

DWORD SEASON3B::CNewUINPCShop::GetShopState()
{
    return m_dwShopState;
}

int SEASON3B::CNewUINPCShop::GetPointedItemIndex()
{
    return m_pNewInventoryCtrl->GetPointedSquareIndex();
}

void SEASON3B::CNewUINPCShop::SetStandbyItemKey(DWORD dwItemKey)
{
    m_dwStandbyItemKey = dwItemKey;
}

DWORD SEASON3B::CNewUINPCShop::GetStandbyItemKey() const
{
    return m_dwStandbyItemKey;
}

int SEASON3B::CNewUINPCShop::GetStandbyItemIndex()
{
    ITEM *pItem = GetStandbyItem();
    if (pItem)
        return pItem->y * m_pNewInventoryCtrl->GetNumberOfColumn() + pItem->x;
    return -1;
}

ITEM *SEASON3B::CNewUINPCShop::GetStandbyItem()
{
    if (m_pNewInventoryCtrl)
        return m_pNewInventoryCtrl->FindItemByKey(m_dwStandbyItemKey);
    return NULL;
}

void SEASON3B::CNewUINPCShop::SetSellingItem(bool bFlag)
{
    m_bSellingItem = bFlag;
}

bool SEASON3B::CNewUINPCShop::IsSellingItem()
{
    return m_bSellingItem;
}

void CNewUINPCShop::SyncModernGeometry()
{
    const auto cell = m_ModernPanel.GridCell();
    if (m_pNewInventoryCtrl && cell.width > 0 && cell.height > 0)
        m_pNewInventoryCtrl->SetOwnerGeometry(cell.x, cell.y, cell.width, cell.height);
}
bool CNewUINPCShop::IsMouseInModernPanel() const
{
    const auto rect = m_ModernPanel.PanelRect();
    return CheckMouseIn(static_cast<int>(std::floor(rect.x)), static_cast<int>(std::floor(rect.y)),
                        static_cast<int>(std::ceil(rect.width)),
                        static_cast<int>(std::ceil(rect.height)));
}

std::optional<bool> CNewUINPCShop::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernPanel.ProcessInput(event);
}

CNewUINPCQuest::CNewUINPCQuest(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()), panel_(keeper)
{
}
CNewUINPCQuest::~CNewUINPCQuest()
{
    Release();
}
bool CNewUINPCQuest::Create(CNewUIManager *manager, CNewUI3DRenderMng *models, int, int)
{
    if (!manager || !models || !g_pNewItemMng)
        return false;
    manager_ = manager;
    models_ = models;
    manager_->AddUIObj(INTERFACE_NPCQUEST, this);
    models_->Add3DRenderObj(this, INVENTORY_CAMERA_Z_ORDER);
    Show(false);
    return true;
}
void CNewUINPCQuest::Release()
{
    panel_.Release();
    content_ = {};
    items_.clear();
    visible_ = pending_ = false;
    if (models_)
    {
        models_->Remove3DRenderObj(this);
        models_ = nullptr;
    }
    if (manager_)
    {
        manager_->RemoveUIObj(this);
        manager_ = nullptr;
    }
}

bool CNewUINPCQuest::UpdateMouseEvent()
{
    return !panel_.ContainsReferencePointer(MouseX, MouseY);
}
bool CNewUINPCQuest::UpdateKeyEvent()
{
    if (!IsVisible() || !IsPress(VK_ESCAPE))
        return true;
    g_pNewUISystem->Hide(INTERFACE_NPCQUEST);
    PlayBuffer(SOUND_CLICK01);
    return false;
}
void CNewUINPCQuest::SendProgress()
{
    pending_ = true;
    SocketClient->ToGameServer()->SendLegacyQuestStateSetRequest(g_csQuest.GetCurrQuestIndex(),
                                                                 LegacyQuestState::Active);
}
void CNewUINPCQuest::Choose(std::size_t choice)
{
    const auto &entry = GameLogic::Quests::Dialog::GetEntry(g_iCurrentDialogScript);
    const auto answer = entry.answers[choice];
    bool error = false;
    switch (answer.returnCode)
    {
    case 1:
        error = g_csQuest.ProcessNextProgress();
        if (!error)
            SendProgress();
        break;
    case 2:
        g_pNewUISystem->Hide(INTERFACE_NPCQUEST);
        break;
    case 3:
        SendProgress();
        break;
    }
    if (answer.link > 0 && !error)
        g_csQuest.ShowDialogText(answer.link);
    PlayBuffer(SOUND_INTERFACE01);
}
bool CNewUINPCQuest::Update()
{
    // Refresh before consuming a queued click: a server reply may have changed its branch.
    if (IsVisible())
        StageContent();
    const auto changes = panel_.TakeChanges();
    if (changes.focus && manager_)
        manager_->BringToFront(this);
    if (IsVisible())
    {
        if (changes.close)
            g_pNewUISystem->Hide(INTERFACE_NPCQUEST);
        else if (!pending_ && changes.revision == revision_)
        {
            if (changes.choice)
                Choose(*changes.choice);
            else if (changes.complete && g_csQuest.BeQuestItem())
            {
                SendProgress();
                PlayBuffer(SOUND_INTERFACE01);
            }
        }
    }
    visible_ = IsVisible();
    if (visible_)
        StageContent();
    return true;
}

bool CNewUINPCQuest::IsVisible() const
{
    return CNewUIObj::IsVisible();
}
float CNewUINPCQuest::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}
void CNewUINPCQuest::ProcessOpening()
{
    pending_ = false;
    visible_ = true;
    g_csQuest.ShowQuestNpcWindow();
    StageContent();
    ++revision_;
}
bool CNewUINPCQuest::ProcessClosing()
{
    visible_ = false;
    items_.clear();
    SocketClient->ToGameServer()->SendCloseNpcRequest();
    return true;
}

bool CNewUINPCQuest::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessInput(event);
}

namespace GatekeeperDetail
{
std::wstring FeeText(int fee, const wchar_t *format)
{
    wchar_t gold[64], text[256];
    ConvertGold(fee, gold);
    mu_swprintf(text, format, gold);
    return text;
}
} // namespace GatekeeperDetail
CNewUIGatemanWindow::CNewUIGatemanWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()), panel_(keeper)
{
}
CNewUIGatemanWindow::~CNewUIGatemanWindow()
{
    Release();
}
bool CNewUIGatemanWindow::Create(CNewUIManager *manager, int, int)
{
    if (!manager)
        return false;
    manager_ = manager;
    manager_->AddUIObj(INTERFACE_GATEKEEPER, this);
    Show(false);
    return true;
}
void CNewUIGatemanWindow::Release()
{
    panel_.Release();
    if (manager_)
        manager_->RemoveUIObj(this);
    manager_ = nullptr;
}

void CNewUIGatemanWindow::Enter()
{
    auto &gate = *g_pUIGateKeeper;
    if (gate.GetType() == TOUCH_TYPE_PERSON)
    {
        if (!gate.IsPublic())
            return;
        if (static_cast<int>(CharacterMachine->Gold) < gate.GetEnteranceFee())
        {
            CreateMessageBox(MSGBOX_LAYOUT_CLASS(CGatemanMoneyMsgBoxLayout, SessionOrigin()));
            return;
        }
    }
    else if (gate.GetType() != TOUCH_TYPE_GUILD_MASTER && gate.GetType() != TOUCH_TYPE_GUILD_STAFF)
        return;
    SocketClient->ToGameServer()->SendCastleSiegeHuntingZoneEnterRequest(
        gate.GetViewEnteranceFee());
    g_pNewUISystem->Hide(INTERFACE_GATEKEEPER);
}
bool CNewUIGatemanWindow::Update()
{
    const auto changes = panel_.TakeChanges();
    if (changes.focus && manager_)
        manager_->BringToFront(this);
    if (IsVisible() && changes.close)
        g_pNewUISystem->Hide(INTERFACE_GATEKEEPER);
    if (IsVisible())
    {
        auto &gate = *g_pUIGateKeeper;
        if (gate.GetType() == TOUCH_TYPE_GUILD_MASTER)
        {
            if (changes.publicAccess)
                SocketClient->ToGameServer()->SendCastleSiegeHuntingZoneEntranceSetting(
                    *changes.publicAccess);
            if (changes.feeIndex)
                gate.SetViewEntranceFee(*changes.feeIndex * gate.GetAddEnteranceFee());
            if (changes.confirm)
            {
                constexpr int HuntingGroundTax = 3;
                SocketClient->ToGameServer()->SendCastleSiegeTaxChangeRequest(
                    HuntingGroundTax, gate.GetViewEnteranceFee());
            }
        }
        if (changes.enter)
            Enter();
        StageContent();
    }
    visible_ = IsVisible();
    return true;
}
bool CNewUIGatemanWindow::UpdateMouseEvent()
{
    return !panel_.ContainsReferencePointer(MouseX, MouseY);
}
bool CNewUIGatemanWindow::UpdateKeyEvent()
{
    if (!IsVisible() || !IsPress(VK_ESCAPE))
        return true;
    g_pNewUISystem->Hide(INTERFACE_GATEKEEPER);
    PlayBuffer(SOUND_CLICK01);
    return false;
}
void CNewUIGatemanWindow::OpeningProcess()
{
    visible_ = true;
    StageContent();
}
void CNewUIGatemanWindow::ClosingProcess()
{
    visible_ = false;
    SocketClient->ToGameServer()->SendCloseNpcRequest();
}
float CNewUIGatemanWindow::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

bool CNewUIGatemanWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessInput(event);
}

namespace QuestInfoDetail
{
const wchar_t *QuestText(const wchar_t *text)
{
    return text ? text : L"";
}
} // namespace QuestInfoDetail
CNewUIMyQuestInfoWindow::CNewUIMyQuestInfoWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()), panel_(keeper)
{
}
CNewUIMyQuestInfoWindow::~CNewUIMyQuestInfoWindow()
{
    Release();
}
bool CNewUIMyQuestInfoWindow::Create(CNewUIManager *manager, int, int)
{
    if (!manager)
        return false;
    manager_ = manager;
    manager_->AddUIObj(INTERFACE_MYQUEST, this);
    Show(false);
    return true;
}
void CNewUIMyQuestInfoWindow::Release()
{
    panel_.Release();
    quests_.clear();
    content_ = {};
    visible_ = false;
    staged_.reset();
    if (!manager_)
        return;
    manager_->RemoveUIObj(this);
    manager_ = nullptr;
}

bool CNewUIMyQuestInfoWindow::UpdateMouseEvent()
{
    return !panel_.ContainsReferencePointer(MouseX, MouseY);
}
bool CNewUIMyQuestInfoWindow::UpdateKeyEvent()
{
    if (!IsVisible() || !IsPress(VK_ESCAPE))
        return true;
    g_pNewUISystem->Hide(INTERFACE_MYQUEST);
    return false;
}
void CNewUIMyQuestInfoWindow::Select(std::size_t index)
{
    if (selected_ == index)
        return;
    selected_ = index;
    canStart_ = g_QuestMng.IsQuestByEtc(quests_[index]);
    canGiveUp_ = true;
    showRewards_ = false;
    const auto quest = quests_[index];
    SocketClient->ToGameServer()->SendQuestStateRequest(static_cast<std::uint16_t>(LOWORD(quest)),
                                                        static_cast<std::uint16_t>(HIWORD(quest)));
    PlayBuffer(SOUND_CLICK01);
}
void CNewUIMyQuestInfoWindow::ProcessChanges(const Panel::Changes &changes)
{
    if (changes.close)
    {
        g_pNewUISystem->Hide(INTERFACE_MYQUEST);
        return;
    }
    if (changes.tab && *changes.tab != tab_)
    {
        tab_ = *changes.tab;
        if (tab_ == Panel::Tab::CastleTemple)
        {
            SocketClient->ToGameServer()->SendMiniGameEventCountRequest(MiniGameType::BloodCastle);
            SocketClient->ToGameServer()->SendMiniGameEventCountRequest(MiniGameType::CursedTemple);
        }
        PlayBuffer(SOUND_CLICK01);
    }
    if (changes.revision != revision_ || tab_ != Panel::Tab::Quests)
        return;
    if (changes.selected)
        Select(*changes.selected);
    if (changes.start && canStart_ && selected_)
    {
        g_pQuestProgressByEtc->SetContents(GetSelQuestIndex());
        g_pNewUISystem->Show(INTERFACE_QUEST_PROGRESS_ETC);
        PlayBuffer(SOUND_CLICK01);
    }
    if (changes.giveUp && canGiveUp_ && selected_)
        CreateMessageBox(MSGBOX_LAYOUT_CLASS(CQuestGiveUpMsgBoxLayout, SessionOrigin()));
}
bool CNewUIMyQuestInfoWindow::Update()
{
    const auto changes = panel_.TakeChanges();
    if (changes.focus && manager_)
        manager_->BringToFront(this);
    if (IsVisible())
        ProcessChanges(changes);
    visible_ = IsVisible();
    if (visible_)
    {
        StageContent();
        StageTooltip();
    }
    return true;
}

float CNewUIMyQuestInfoWindow::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}
void CNewUIMyQuestInfoWindow::OpenningProcess()
{
    g_csQuest.ShowQuestPreviewWindow(-1);
    staged_.reset();
    visible_ = true;
    StageContent();
}
void CNewUIMyQuestInfoWindow::ClosingProcess()
{
    visible_ = false;
    UnselectQuestList();
    SocketClient->ToGameServer()->SendCloseNpcRequest();
    PlayBuffer(SOUND_CLICK01);
}
void CNewUIMyQuestInfoWindow::UnselectQuestList()
{
    selected_.reset();
    canStart_ = canGiveUp_ = showRewards_ = false;
    rewardItems_.fill(nullptr);
    StageContent();
}
void CNewUIMyQuestInfoWindow::SetCurQuestList(DWordList *quests)
{
    quests_.assign(quests->begin(), quests->end());
    ++listRevision_;
    UnselectQuestList();
    ++revision_; // Invalidates a queued selection from the previous list even when text is identical.
}
void CNewUIMyQuestInfoWindow::SetSelQuestSummary()
{
    showRewards_ = false;
    StageContent();
}
void CNewUIMyQuestInfoWindow::SetSelQuestRequestReward()
{
    showRewards_ = true;
    StageContent();
}
void CNewUIMyQuestInfoWindow::QuestOpenBtnEnable(bool enable)
{
    canStart_ = enable;
    StageContent();
}
void CNewUIMyQuestInfoWindow::QuestGiveUpBtnEnable(bool enable)
{
    canGiveUp_ = enable;
    StageContent();
}
DWORD CNewUIMyQuestInfoWindow::GetSelQuestIndex()
{
    return selected_ ? quests_[*selected_] : 0;
}

bool CNewUIMyQuestInfoWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessInput(event);
}

namespace QuestProgressDetail
{
const wchar_t *QuestText(const wchar_t *text)
{
    return text ? text : L"";
}

} // namespace QuestProgressDetail
CNewUIQuestProgress::CNewUIQuestProgress(SessionKeeper &keeper) : CNewUIQuestProgress(keeper, false)
{
}
CNewUIQuestProgress::CNewUIQuestProgress(SessionKeeper &keeper, bool byItem)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()), panel_(keeper, byItem),
      byItem_(byItem)
{
}
CNewUIQuestProgress::~CNewUIQuestProgress()
{
    Release();
}
int CNewUIQuestProgress::InterfaceId() const
{
    return byItem_ ? INTERFACE_QUEST_PROGRESS_ETC : INTERFACE_QUEST_PROGRESS;
}
bool CNewUIQuestProgress::Create(CNewUIManager *manager, int, int)
{
    if (!manager)
        return false;
    manager_ = manager;
    manager_->AddUIObj(InterfaceId(), this);
    Show(false);
    return true;
}
void CNewUIQuestProgress::Release()
{
    panel_.Release();
    content_ = {};
    rewardItems_.fill(nullptr);
    visible_ = pending_ = false;
    staged_.reset();
    if (!manager_)
        return;
    manager_->RemoveUIObj(this);
    manager_ = nullptr;
}

bool CNewUIQuestProgress::UpdateMouseEvent()
{
    return !panel_.ContainsReferencePointer(MouseX, MouseY);
}
bool CNewUIQuestProgress::UpdateKeyEvent()
{
    if (!IsVisible() || !IsPress(VK_ESCAPE))
        return true;
    g_pNewUISystem->Hide(InterfaceId());
    return false;
}
void CNewUIQuestProgress::ProcessChanges(const Panel::Changes &changes)
{
    if (changes.close)
    {
        g_pNewUISystem->Hide(InterfaceId());
        return;
    }
    if (pending_ || changes.revision != revision_)
        return;
    const auto number = static_cast<std::uint16_t>(LOWORD(quest_));
    const auto group = static_cast<std::uint16_t>(HIWORD(quest_));
    if (changes.choice)
    {
        SocketClient->ToGameServer()->SendQuestProceedRequest(
            number, group, static_cast<QuestProceedAction>(*changes.choice + 1));
    }
    else if (changes.complete && content_.complete && completionEnabled_)
    {
        SocketClient->ToGameServer()->SendQuestCompletionRequest(number, group);
    }
    else
        return;
    pending_ = true;
    PlayBuffer(SOUND_CLICK01);
}
bool CNewUIQuestProgress::Update()
{
    if (IsVisible())
        StageContent();
    const auto changes = panel_.TakeChanges();
    if (changes.focus && manager_)
        manager_->BringToFront(this);
    if (IsVisible())
        ProcessChanges(changes);
    visible_ = IsVisible();
    if (visible_)
    {
        if (content_.canChoose == pending_)
        {
            content_.canChoose = !pending_;
            ++revision_;
        }
        StageTooltip();
    }
    return true;
}

bool CNewUIQuestProgress::IsVisible() const
{
    return CNewUIObj::IsVisible();
}
float CNewUIQuestProgress::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}
void CNewUIQuestProgress::ProcessOpening()
{
    visible_ = true;
    PlayBuffer(SOUND_INTERFACE01);
}
bool CNewUIQuestProgress::ProcessClosing()
{
    if (byItem_)
        g_QuestMng.DelQuestIndexByEtcList(quest_);
    quest_ = 0;
    visible_ = false;
    rewardItems_.fill(nullptr);
    SocketClient->ToGameServer()->SendCloseNpcRequest();
    PlayBuffer(SOUND_CLICK01);
    return true;
}
void CNewUIQuestProgress::SetContents(DWORD index)
{
    if (!index)
        return;
    staged_.reset();
    quest_ = index;
    pending_ = false;
    completionEnabled_ = true;
    ++branch_;
    StageContent();
    ++revision_;
}
void CNewUIQuestProgress::EnableCompleteBtn(bool enable)
{
    completionEnabled_ = enable;
    StageContent();
}

bool CNewUIQuestProgress::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessInput(event);
}

namespace
{
const wchar_t *DialogueText(const wchar_t *text)
{
    return text ? text : L"";
}
} // namespace
CNewUINPCDialogue::CNewUINPCDialogue(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()), panel_(keeper)
{
}
CNewUINPCDialogue::~CNewUINPCDialogue()
{
    Release();
}
bool CNewUINPCDialogue::Create(CNewUIManager *manager, int, int)
{
    if (!manager)
        return false;
    m_pNewUIMng = manager;
    manager->AddUIObj(INTERFACE_NPC_DIALOGUE, this);
    Show(false);
    return true;
}
void CNewUINPCDialogue::Release()
{
    panel_.Release();
    content_ = {};
    visible_ = false;
    if (!m_pNewUIMng)
        return;
    m_pNewUIMng->RemoveUIObj(this);
    m_pNewUIMng = nullptr;
}

bool CNewUINPCDialogue::UpdateMouseEvent()
{
    return !panel_.ContainsReferencePointer(MouseX, MouseY);
}
bool CNewUINPCDialogue::UpdateKeyEvent()
{
    if (!IsVisible() || !IsPress(VK_ESCAPE))
        return true;
    g_pNewUISystem->Hide(INTERFACE_NPC_DIALOGUE);
    return false;
}
bool CNewUINPCDialogue::Update()
{
    const auto changes = panel_.TakeChanges();
    if (changes.focus && m_pNewUIMng)
        m_pNewUIMng->BringToFront(this);
    if (IsVisible())
    {
        if (changes.close)
            g_pNewUISystem->Hide(INTERFACE_NPC_DIALOGUE);
        else if (changes.choice && content_.canChoose && changes.revision == content_.revision)
        {
            content_.canChoose = false;
            m_nSelSelText = static_cast<int>(*changes.choice) + 1;
            ProcessSelTextResult();
            PlayBuffer(SOUND_CLICK01);
        }
    }
    visible_ = IsVisible();
    return true;
}

bool CNewUINPCDialogue::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessInput(event);
}
bool CNewUINPCDialogue::IsVisible() const
{
    return CNewUIObj::IsVisible();
}
float CNewUINPCDialogue::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}
void CNewUINPCDialogue::ProcessOpening()
{
    m_bQuestListMode = false;
    visible_ = true;
    SetContents(0);
    PlayBuffer(SOUND_INTERFACE01);
}
bool CNewUINPCDialogue::ProcessClosing()
{
    m_dwCurDlgIndex = m_dwContributePoint = 0;
    m_bQuestListMode = visible_ = false;
    content_.canChoose = false;
    SocketClient->ToGameServer()->SendCloseNpcRequest();
    PlayBuffer(SOUND_CLICK01);
    return true;
}
void CNewUINPCDialogue::SetContents(DWORD index)
{
    m_dwCurDlgIndex = index;
    content_.title = DialogueText(g_QuestMng.GetNPCName());
    content_.dialogue = DialogueText(g_QuestMng.GetNPCDlgNPCWords(index));
    content_.choices.clear();
    for (int i = 0; i < QM_MAX_ND_ANSWER; ++i)
    {
        const auto *answer = g_QuestMng.GetNPCDlgAnswer(index, i);
        if (!answer)
            break;
        content_.choices.push_back(std::to_wstring(i + 1) + L". " + answer);
    }
    m_nSelTextCount = static_cast<int>(content_.choices.size());
    m_nSelSelText = 0;
    content_.canChoose = true;
    StageContribution();
    ++content_.revision;
}

void CNewUINPCDialogue::SetContributePoint(DWORD point)
{
    if (m_dwContributePoint == point)
        return;
    m_dwContributePoint = point;
    StageContribution();
    ++content_.revision;
}
void CNewUINPCDialogue::ProcessQuestListReceive(DWORD *quests, int count)
{
    m_bQuestListMode = true;
    std::copy_n(quests, count, m_adwQuestIndex.begin());
    content_.title = DialogueText(g_QuestMng.GetNPCName());
    content_.dialogue = DialogueText(g_QuestMng.GetWords(count > 0 ? 1501 : 1502));
    content_.choices.clear();
    for (int i = 0; i < count; ++i)
        content_.choices.push_back(std::to_wstring(i + 1) + L". [Q]" +
                                   DialogueText(g_QuestMng.GetSubject(quests[i])));
    content_.choices.push_back(std::to_wstring(count + 1) + L". " +
                               DialogueText(g_QuestMng.GetWords(1007)));
    m_nSelTextCount = count + 1;
    m_nSelSelText = 0;
    content_.canChoose = true;
    ++content_.revision;
}

void CNewUINPCDialogue::ProcessSelTextResult()
{
    if (m_bQuestListMode)
    {
        if (m_nSelSelText == m_nSelTextCount)
        {
            m_bQuestListMode = false;
            SetContents(0);
        }
        else
        {
            const DWORD dwSelectedQuest = m_adwQuestIndex[m_nSelSelText - 1];
            const auto questNumber = static_cast<uint16_t>(LOWORD(dwSelectedQuest));
            const auto questGroup = static_cast<uint16_t>(HIWORD(dwSelectedQuest));
            SocketClient->ToGameServer()->SendQuestSelectRequest(questNumber, questGroup,
                                                                 (BYTE)m_nSelSelText);
        }
    }
    else
    {
        int nAnswerResult = g_QuestMng.GetNPCDlgAnswerResult(m_dwCurDlgIndex, m_nSelSelText - 1);
        if (900 >= nAnswerResult)
        {
            SetContents(nAnswerResult);
        }
        else
        {
            switch (nAnswerResult)
            {
            case 901:
                SocketClient->ToGameServer()->SendAvailableQuestsRequest();
                break;

            case 902:
                SocketClient->ToGameServer()->SendNpcBuffRequest();
                g_pNewUISystem->Hide(SEASON3B::INTERFACE_NPC_DIALOGUE);
                break;
            case 903:
                SocketClient->ToGameServer()->SendGensJoinRequest(GensType::Duprian);
                break;
            case 904:
                SocketClient->ToGameServer()->SendGensJoinRequest(GensType::Vanert);
                break;
            case 905:
                SocketClient->ToGameServer()->SendGensLeaveRequest();
                break;
            case 906:
                SocketClient->ToGameServer()->SendGensRewardRequest(GensType::Duprian);
                break;
            case 907:
                SocketClient->ToGameServer()->SendGensRewardRequest(GensType::Vanert);
                break;

            default:
                SetContents(999);
            }
        }
    }
}

void CNewUINPCDialogue::ProcessGensJoiningReceive(BYTE byResult, BYTE byInfluence)
{
    switch (byResult)
    {
    case NpcDialogueDetail::GJEC_NONE_ERR:
        Hero->m_byGensInfluence = byInfluence;
        SetContents(5);
        break;
    case NpcDialogueDetail::GJEC_REG_GENS_ERR:
        SetContents(9);
        break;
    case NpcDialogueDetail::GJEC_GENS_SECEDE_DAY_ERR:
        SetContents(11);
        break;
    case NpcDialogueDetail::GJEC_REG_GENS_LV_ERR:
        SetContents(8);
        break;
    case NpcDialogueDetail::GJEC_REG_GENS_NOT_EQL_GUILDMA_ERR:
        SetContents(10);
        break;
    case NpcDialogueDetail::GJEC_NONE_REG_GENS_GUILDMA_ERR:
        SetContents(12);
        break;
    case NpcDialogueDetail::GJEC_PARTY:
        SetContents(18);
        break;
    case NpcDialogueDetail::GJEC_GUILD_UNION_MASTER:
        SetContents(19);
        break;
    }
}

void CNewUINPCDialogue::ProcessGensSecessionReceive(BYTE byResult)
{
    switch (byResult)
    {
    case NpcDialogueDetail::GSEC_NONE_ERR:
        Hero->m_byGensInfluence = 0;
        SetContents(16);
        break;
    case NpcDialogueDetail::GSEC_IS_NOT_REG_GENS:
        SetContents(15);
        break;
    case NpcDialogueDetail::GSEC_GUILD_MASTER_CAN_NOT_SECEDE:
        SetContents(14);
        break;
    case NpcDialogueDetail::GSEC_IS_NOT_INFLUENCE_NPC:
        SetContents(17);
        break;
    }
}

void CNewUINPCDialogue::ProcessGensRewardReceive(BYTE byResult)
{
    switch (byResult)
    {
    case NpcDialogueDetail::GENS_REWARD_CALL:
        SetContents(20);
        break;
    case NpcDialogueDetail::GENS_REWARD_TERM:
        SetContents(21);
        break;
    case NpcDialogueDetail::GENS_REWARD_TARGET:
        SetContents(22);
        break;
    case NpcDialogueDetail::GENS_REWARD_SPACE:
        SetContents(23);
        break;
    case NpcDialogueDetail::GENS_REWARD_ALREADY:
        SetContents(24);
        break;
    case NpcDialogueDetail::GENS_REWARD_DIFFERENT:
        SetContents(17);
        break;
    case NpcDialogueDetail::GENS_REWARD_NOT_REG:
        SetContents(25);
        break;
    }
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

CUICurQuestListBox::CUICurQuestListBox(SessionKeeper &keeper)
    : CUITextListBox<SCurQuestItem>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 8;

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
    m_iScrollType = UILISTBOX_SCROLL_DOWNUP;

    SetSize(174, 105);
}

void CUICurQuestListBox::AddText(DWORD dwQuestIndex, const wchar_t *pszText)
{
    SCurQuestItem sCurQuestItem{};
    sCurQuestItem.m_bIsSelected = FALSE;
    sCurQuestItem.m_dwIndex = dwQuestIndex;
    wcsncpy(sCurQuestItem.m_szText, pszText, 64);

    m_TextList.push_front(sCurQuestItem);

    SendUIMessageDirect(UI_MESSAGE_LISTSCRLTOP, 0, 0);
    SLSetSelectLine(0);
}

void CUICurQuestListBox::SetNumRenderLine(int iLine)
{
    if (iLine < m_iNumRenderLine && iLine < GetLineNum())
        ++m_iCurrentRenderEndLine;
    else if (iLine > GetLineNum())
        m_iCurrentRenderEndLine = 0;

    m_iNumRenderLine = iLine;
}

int CUICurQuestListBox::GetRenderLinePos_y(int iLineNumber)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * 13 - iLineNumber * 13 + 3);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * 13 - iLineNumber * 13 + 3);
}

BOOL CUICurQuestListBox::DoLineMouseAction(int iLineNumber)
{
    if (CheckMouseIn(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3,
                     m_iWidth - m_fScrollBarWidth + 1, 13))
    {
        if (MouseLButtonPush) // MouseLButtonDBClick
        {
            MouseLButtonPush = false;

            int nSelLine = m_iCurrentRenderEndLine + iLineNumber + 1;

            if (SLGetSelectLineNum() == nSelLine)
                return TRUE;

            PlayBuffer(SOUND_CLICK01);

            SLSetSelectLine(nSelLine);
            m_TextListIter->m_bIsSelected = (m_TextListIter->m_bIsSelected + 1) % 2; // T/F Reverse

            m_TextListIter = SLGetSelectLine();
            if (g_QuestMng.IsQuestByEtc(m_TextListIter->m_dwIndex))
                g_pMyQuestInfoWindow->QuestOpenBtnEnable(true);
            else
                g_pMyQuestInfoWindow->QuestOpenBtnEnable(false);

            g_pMyQuestInfoWindow->QuestGiveUpBtnEnable(true);
            g_pMyQuestInfoWindow->SetSelQuestSummary();

            const auto questNumber = static_cast<uint16_t>(LOWORD(m_TextListIter->m_dwIndex));
            const auto questGroup = static_cast<uint16_t>(HIWORD(m_TextListIter->m_dwIndex));
            SocketClient->ToGameServer()->SendQuestStateRequest(questNumber, questGroup);
        }
    }

    return TRUE;
}

void CUICurQuestListBox::DeleteText(DWORD dwQuestIndex)
{
    BOOL bFind = FALSE;
    for (m_TextListIter = m_TextList.begin(); m_TextListIter != m_TextList.end(); ++m_TextListIter)
    {
        if (m_TextListIter->m_dwIndex == dwQuestIndex)
        {
            bFind = TRUE;
            break;
        }
    }

    if (bFind == FALSE)
        return;

    if (m_TextList.size() == 1)
    {
        m_TextList.erase(m_TextListIter);
        SLSetSelectLine(0);
        return;
    }

    if (SLGetSelectLineNum() != 1)
        SLSelectNextLine();

    m_TextList.erase(m_TextListIter);
}

CUIQuestContentsListBox::CUIQuestContentsListBox(SessionKeeper &keeper)
    : CUITextListBox<SQuestContents>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 16;

    m_fScrollBarRange_top = 0;
    m_fScrollBarRange_bottom = 0;

    m_fScrollBarPos_y = 0;
    m_fScrollBarWidth = 13;
    m_fScrollBarHeight = 0;

    m_fScrollBarClickPos_y = 0;
    m_bScrollBtnClick = FALSE;
    m_bScrollBarClick = FALSE;

    m_bUseSelectLine = FALSE;
    m_bUseNewUIScrollBar = TRUE;
    m_iScrollType = UILISTBOX_SCROLL_DOWNUP;

    SetSize(174, 208);
}

void CUIQuestContentsListBox::AddText(LegacyFontRole role, DWORD dwColor, int nSort,
                                      const wchar_t *pszText)
{
    SQuestContents sQuestContents{};

    sQuestContents.m_fontRole = role;
    sQuestContents.m_dwColor = dwColor;
    sQuestContents.m_nSort = nSort;
    wcsncpy(sQuestContents.m_szText, pszText, 64);
    sQuestContents.m_dwType = 0;
    sQuestContents.m_wIndex = 0;
    sQuestContents.m_pItem = nullptr;

    m_TextList.push_front(sQuestContents);

    SendUIMessageDirect(UI_MESSAGE_LISTSCRLTOP, 0, 0);
}

void CUIQuestContentsListBox::AddText(SRequestRewardText *pRequestRewardText, int nSort)
{
    SQuestContents sQuestContents{};

    sQuestContents.m_fontRole = pRequestRewardText->m_fontRole;
    sQuestContents.m_dwColor = pRequestRewardText->m_dwColor;
    sQuestContents.m_nSort = nSort;
    wcsncpy(sQuestContents.m_szText, pRequestRewardText->m_szText, 64);
    sQuestContents.m_eRequestReward = pRequestRewardText->m_eRequestReward;
    sQuestContents.m_dwType = pRequestRewardText->m_dwType;
    sQuestContents.m_wIndex = pRequestRewardText->m_wIndex;
    sQuestContents.m_pItem = pRequestRewardText->m_pItem;

    m_TextList.push_front(sQuestContents);

    SendUIMessageDirect(UI_MESSAGE_LISTSCRLTOP, 0, 0);
}

int CUIQuestContentsListBox::GetRenderLinePos_y(int iLineNumber)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * 13 - iLineNumber * 13 + 3);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * 13 - iLineNumber * 13 + 3);
}

void CUIQuestContentsListBox::DoActionSub(BOOL bMessageOnly)
{
    SLSetSelectLine(0);
}

BOOL CUIQuestContentsListBox::DoLineMouseAction(int iLineNumber)
{
    if (CheckMouseIn(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3,
                     m_iWidth - m_fScrollBarWidth + 1, 13))
    {
        int nSelLine = m_iCurrentRenderEndLine + iLineNumber + 1;
        SLSetSelectLine(nSelLine);
    }

    return TRUE;
}
