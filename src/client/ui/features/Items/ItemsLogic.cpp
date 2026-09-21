#include "ui/features/Items/ItemsLogic.h"
#include "I18N/All.h"
#include "app/AppWindow.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "data/WorldData.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Quests.h"
#include "domain/Shop.h"
#include "domain/WorldSimulation.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
#include "render/ModelResources.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/QuestNpc/QuestNpcLogic.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"
#include "ui/session/UiSessionRender.h"

// NewUIInventoryActionController.cpp

namespace SEASON3B
{

CNewUIInventoryActionController::CNewUIInventoryActionController(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_pContext(nullptr)
{
}

void CNewUIInventoryActionController::SetContext(IInventoryActionContext *pContext)
{
    m_pContext = pContext;
}

bool CNewUIInventoryActionController::HandleInventoryActions(
    CNewUIInventoryCtrl *targetControl) const
{
    if (m_pContext == nullptr || targetControl == nullptr)
    {
        return false;
    }

    if (g_pMyInventory->GetInventoryCtrl()->GetPickedItem() && IsRelease(VK_LBUTTON))
    {
        return HandlePickedItemPlacement(targetControl);
    }

    if (m_pContext->GetRepairMode() == REPAIR_MODE_OFF && IsPress(VK_RBUTTON))
    {
        return HandleRightClick(targetControl);
    }

    if (m_pContext->GetRepairMode() == REPAIR_MODE_ON && IsPress(VK_LBUTTON))
    {
        return HandleRepairClick(targetControl);
    }

    return false;
}

bool CNewUIInventoryActionController::HandlePickedItemPlacement(
    CNewUIInventoryCtrl *targetControl) const
{
    CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();
    if (pPickedItem == nullptr)
    {
        return false;
    }

    ITEM *pPickItem = pPickedItem->GetItem();
    if (pPickItem == nullptr)
    {
        return true;
    }

    const int iSourceIndex = pPickedItem->GetSourceLinealPos();
    const int iTargetIndex = pPickedItem->GetTargetLinealPos(targetControl);

    const bool bFromInventorySystem =
        (pPickedItem->GetOwnerInventory() == g_pMyInventory->GetInventoryCtrl()) ||
        (g_pMyInventoryExt != nullptr && g_pMyInventoryExt->GetOwnerOf(pPickedItem) != nullptr);

    if (bFromInventorySystem)
    {
        if (ApplyJewels(targetControl, pPickedItem, pPickItem, iSourceIndex, iTargetIndex))
        {
            return true;
        }

        if (iTargetIndex != -1 &&
            TryStackItems(targetControl, pPickItem, iSourceIndex, iTargetIndex))
        {
            return true;
        }
    }

    if (TryMoveItem(targetControl, pPickedItem, pPickItem, iSourceIndex, iTargetIndex))
    {
        return true;
    }

    return false;
}

bool CNewUIInventoryActionController::TryApplyJewel(CNewUIInventoryCtrl *targetControl,
                                                    CNewUIPickedItem *pPickedItem, ITEM *pPickItem,
                                                    int iSourceIndex, int iTargetIndex) const
{
    return ApplyJewels(targetControl, pPickedItem, pPickItem, iSourceIndex, iTargetIndex);
}

bool CNewUIInventoryActionController::TryStackItem(CNewUIInventoryCtrl *targetControl,
                                                   ITEM *pPickItem, int iSourceIndex,
                                                   int iTargetIndex) const
{
    return TryStackItems(targetControl, pPickItem, iSourceIndex, iTargetIndex);
}

bool CNewUIInventoryActionController::TryMoveItem(CNewUIInventoryCtrl *targetControl,
                                                  CNewUIPickedItem *pPickedItem, ITEM *pPickItem,
                                                  int iSourceIndex, int iTargetIndex) const
{
    if (iTargetIndex < 0 || !targetControl->CanMove(iTargetIndex, pPickItem))
    {
        return false;
    }

    const auto sourceStorageType = pPickedItem->GetSourceStorageType();
    const auto targetStorageType = targetControl->GetStorageType();

    if (iTargetIndex != iSourceIndex)
    {
        return SendRequestEquipmentItem(sourceStorageType, iSourceIndex, pPickItem,
                                        targetStorageType, iTargetIndex);
    }

    g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();
    return false;
}

bool CNewUIInventoryActionController::HandleRepairClick(CNewUIInventoryCtrl *targetControl) const
{
    return RepairItemAtMousePoint(targetControl);
}

bool CNewUIInventoryActionController::HandleRightClick(CNewUIInventoryCtrl *targetControl) const
{
    m_pContext->ResetMouseRButton();

    if (g_pNewUISystem->IsVisible(INTERFACE_STORAGE))
    {
        return HandleStorageAutoMove(targetControl);
    }

    if (g_pNewUISystem->IsVisible(INTERFACE_NPCSHOP) &&
        g_pNewUISystem->IsVisible(INTERFACE_INVENTORY))
    {
        return HandleSellToNPC(targetControl);
    }

    if (g_pNewUISystem->IsVisible(INTERFACE_MIXINVENTORY) &&
        g_pNewUISystem->IsVisible(INTERFACE_INVENTORY))
    {
        return HandleMixAutoMove(targetControl);
    }

    if (g_pNewUISystem->IsVisible(INTERFACE_INVENTORY) &&
        !g_pNewUISystem->IsVisible(INTERFACE_NPCSHOP) &&
        !g_pNewUISystem->IsVisible(INTERFACE_TRADE) &&
        !g_pNewUISystem->IsVisible(INTERFACE_DEVILSQUARE) &&
        !g_pNewUISystem->IsVisible(INTERFACE_BLOODCASTLE) &&
        !g_pNewUISystem->IsVisible(INTERFACE_LUCKYITEMWND) &&
        !g_pNewUISystem->IsVisible(INTERFACE_MIXINVENTORY) &&
        !g_pNewUISystem->IsVisible(INTERFACE_MYSHOP_INVENTORY))
    {
        return HandleInventoryRightClickActions(targetControl);
    }

    return false;
}

bool CNewUIInventoryActionController::HandleStorageAutoMove(
    CNewUIInventoryCtrl *targetControl) const
{
    if (g_pStorageInventory->ProcessMyInvenItemAutoMove(targetControl))
    {
        return true;
    }

    if (g_pNewUISystem->IsVisible(INTERFACE_STORAGE_EXT))
    {
        return g_pStorageInventoryExt->ProcessMyInvenItemAutoMove(targetControl);
    }

    return false;
}

bool CNewUIInventoryActionController::HandleMixAutoMove(CNewUIInventoryCtrl *targetControl) const
{
    // All crafting NPC dialogs (Chaos Machine, Seed Master, Elphis, Osbourne, ...) share the
    // single mix window, so routing the right-click here covers every crafting NPC at once.
    return g_pMixInventory->ProcessMyInvenItemAutoMove(targetControl);
}

bool CNewUIInventoryActionController::HandleSellToNPC(CNewUIInventoryCtrl *targetControl) const
{
    if (g_pMyInventory->GetInventoryCtrl()->GetPickedItem())
    {
        return false;
    }

    if (!targetControl->CheckPtInRect(MouseX, MouseY))
    {
        return false;
    }

    if (targetControl->GetStorageType() != STORAGE_TYPE::INVENTORY)
    {
        return false;
    }

    ITEM *pItem = targetControl->FindItemAtPt(MouseX, MouseY);
    if (pItem == nullptr)
    {
        return false;
    }

    if (g_pNPCShop->IsSellingItem())
    {
        return false;
    }

    if (IsSellingBan(pItem))
    {
        g_pSystemLogBox->AddText(I18N::Game::TheseItemsCannotBeTraded, TYPE_ERROR_MESSAGE);
        return true;
    }

    const int iIndex = targetControl->GetIndexByItem(pItem);
    if (iIndex < 0)
    {
        return false;
    }

    if (!g_pMyInventory->GetInventoryCtrl()->CreatePickedItem(targetControl, pItem))
    {
        return false;
    }

    CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();
    if (pPickedItem == nullptr)
    {
        return false;
    }

    targetControl->RemoveItem(pItem);
    pPickedItem->HidePickedItem();

    const int sourceIndex = pPickedItem->GetSourceLinealPos();
    if (sourceIndex < MAX_EQUIPMENT_INDEX || sourceIndex >= MAX_MY_INVENTORY_EX_INDEX)
    {
        g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();
        return false;
    }

    if (IsHighValueItem(pItem))
    {
        CreateMessageBox(MSGBOX_LAYOUT_CLASS(CHighValueItemCheckMsgBoxLayout, SessionOrigin()));
        return true;
    }

    SocketClient->ToGameServer()->SendSellItemToNpcRequest(sourceIndex);
    g_pNPCShop->SetSellingItem(true);
    return true;
}

bool CNewUIInventoryActionController::HandleInventoryRightClickActions(
    CNewUIInventoryCtrl *targetControl) const
{
    if (g_pNewUISystem->IsVisible(INTERFACE_INVENTORY_EXT))
    {
        return TryTransferBetweenInventorySections(targetControl);
    }

    ITEM *pItem = targetControl->FindItemAtPt(MouseX, MouseY);
    if (pItem == nullptr)
    {
        return false;
    }

    const int iIndex = targetControl->GetIndexByItem(pItem);

    if (iIndex >= 0 && TryConsumeItem(targetControl, pItem, iIndex))
    {
        return true;
    }

#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
    if (g_pMyInventory->IsInvenItem(pItem->Type))
    {
#ifdef LJH_FIX_APP_SHUTDOWN_WEQUIPPING_INVENITEM_WITH_CLICKING_MOUSELBTN
        if (MouseLButton || MouseLButtonPop || MouseLButtonPush)
        {
            return false;
        }
#endif
        if (pItem->Durability == 0)
        {
            return false;
        }

        int iChangeInvenItemStatus = 0;
        (pItem->Durability == 255) ? iChangeInvenItemStatus = 254 : iChangeInvenItemStatus = 255;
        SendRequestEquippingInventoryItem(iIndex, iChangeInvenItemStatus);
        return true;
    }
#endif

    if (!EquipmentItem)
    {
        if (TryEquipItem(targetControl, pItem, iIndex))
        {
            return true;
        }
    }

    if (TryDropItem(targetControl, pItem))
    {
        return true;
    }

    return false;
}

int CNewUIInventoryActionController::FindAlternateEquipSlot(int nOriginalSlot, ITEM *pItem) const
{
    if (nOriginalSlot == EQUIPMENT_WEAPON_RIGHT)
    {
        const auto baseClass = gCharacterManager.GetBaseClass(Hero->Class);
        if (baseClass == CLASS_KNIGHT || baseClass == CLASS_DARK || baseClass == CLASS_RAGEFIGHTER)
        {
            const ITEM_ATTRIBUTE *pItemAttr = &ItemAttribute[pItem->Type];
            if (!pItemAttr->TwoHand)
            {
                return EQUIPMENT_WEAPON_LEFT;
            }
        }
    }
    else if (nOriginalSlot == EQUIPMENT_RING_RIGHT)
    {
        return EQUIPMENT_RING_LEFT;
    }

    return -1;
}

bool CNewUIInventoryActionController::IsSlotOccupied(int nSlot) const
{
    if (nSlot < 0 || nSlot >= MAX_EQUIPMENT_INDEX)
    {
        return true;
    }

    const ITEM *pEquipment = &CharacterMachine->Equipment[nSlot];
    return (pEquipment->Type != -1);
}

bool CNewUIInventoryActionController::TryEquipItem(CNewUIInventoryCtrl *targetControl, ITEM *pItem,
                                                   int iSrcIndex) const
{
    const ITEM_ATTRIBUTE *pItemAttr = &ItemAttribute[pItem->Type];
    int nDstIndex = pItemAttr->m_byItemSlot;

    if (nDstIndex < 0 || nDstIndex >= MAX_EQUIPMENT_INDEX)
    {
        return false;
    }

    if (!m_pContext->IsEquipable(nDstIndex, pItem))
    {
        return true;
    }

    if (IsSlotOccupied(nDstIndex))
    {
        const int nAltSlot = FindAlternateEquipSlot(nDstIndex, pItem);

        if (nAltSlot != -1 && !IsSlotOccupied(nAltSlot))
        {
            nDstIndex = nAltSlot;
        }
        else
        {
            return true;
        }
    }

    if (!m_pContext->IsEquipable(nDstIndex, pItem))
    {
        return true;
    }

    if (!g_pMyInventory->GetInventoryCtrl()->CreatePickedItem(nullptr, pItem))
    {
        return false;
    }

    CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();
    if (pPickedItem == nullptr)
    {
        return false;
    }

    targetControl->RemoveItem(pItem);
    pPickedItem->HidePickedItem();

    SendRequestEquipmentItem(STORAGE_TYPE::INVENTORY, iSrcIndex, pItem, STORAGE_TYPE::INVENTORY,
                             nDstIndex);
    return true;
}

bool CNewUIInventoryActionController::TryDropItem(CNewUIInventoryCtrl *targetControl,
                                                  ITEM *pItem) const
{
    if (Hero->Dead != 0)
    {
        return false;
    }

    if (IsHighValueItem(pItem))
    {
        g_pSystemLogBox->AddText(I18N::Game::YouAreNotAllowedToDropThisExpensiveItem,
                                 TYPE_ERROR_MESSAGE);
        return true;
    }

    if (IsDropBan(pItem))
    {
        g_pSystemLogBox->AddText(I18N::Game::ThisItemCannotBeDropped, TYPE_ERROR_MESSAGE);
        return true;
    }

    if (!g_pMyInventory->GetInventoryCtrl()->CreatePickedItem(targetControl, pItem))
    {
        return false;
    }

    CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();
    if (pPickedItem == nullptr)
    {
        return false;
    }

    targetControl->RemoveItem(pItem);
    pPickedItem->HidePickedItem();

    const int tx = Hero->PositionX;
    const int ty = Hero->PositionY;
    const int sourceIndex = pPickedItem->GetSourceLinealPos();

    SocketClient->ToGameServer()->SendDropItemRequest(tx, ty, sourceIndex);
    SendDropItem = sourceIndex;

    return true;
}

bool CNewUIInventoryActionController::RepairItemAtMousePoint(
    CNewUIInventoryCtrl *targetControl) const
{
    ITEM *pItem = targetControl->FindItemAtPt(MouseX, MouseY);
    if (pItem == nullptr)
    {
        return true;
    }

    if (IsRepairBan(pItem))
    {
        return true;
    }

    const int iIndex = targetControl->GetIndex(pItem->x, pItem->y);

    if (g_pNewUISystem->IsVisible(INTERFACE_NPCSHOP) && g_pNPCShop->IsRepairShop())
    {
        SocketClient->ToGameServer()->SendRepairItemRequest(iIndex, 0);
    }
    else
    {
        SocketClient->ToGameServer()->SendRepairItemRequest(iIndex, 1);
    }

    return true;
}

bool CNewUIInventoryActionController::ApplyJewels(CNewUIInventoryCtrl *targetControl,
                                                  CNewUIPickedItem *pPickedItem, ITEM *pPickItem,
                                                  int iSourceIndex, int iTargetIndex) const
{
    const bool bIsJewelType =
        pPickItem->Type == ITEM_JEWEL_OF_BLESS || pPickItem->Type == ITEM_JEWEL_OF_SOUL ||
        pPickItem->Type == ITEM_JEWEL_OF_LIFE || pPickItem->Type == ITEM_JEWEL_OF_HARMONY ||
        pPickItem->Type == ITEM_LOWER_REFINE_STONE || pPickItem->Type == ITEM_HIGHER_REFINE_STONE ||
        pPickItem->Type == ITEM_POTION + 160 || pPickItem->Type == ITEM_POTION + 161;

    if (!bIsJewelType)
    {
        return false;
    }

    ITEM *pItem = targetControl->FindItem(iTargetIndex);
    if (!pItem)
    {
        return false;
    }

    const int iType = pItem->Type;
    const int iLevel = pItem->Level;
    const int iDurability = pItem->Durability;

    bool bSuccess = true;

    if (iType > ITEM_WINGS_OF_DARKNESS && iType != ITEM_CAPE_OF_LORD &&
        !(iType >= ITEM_WING_OF_STORM && iType <= ITEM_WING_OF_DIMENSION) &&
        !(ITEM_WING + 130 <= iType && iType <= ITEM_WING + 134) &&
        !(iType >= ITEM_CAPE_OF_FIGHTER && iType <= ITEM_CAPE_OF_OVERRULE) &&
        (iType != ITEM_WING + 135))
    {
        bSuccess = false;
    }

    if (iType == ITEM_BOLT || iType == ITEM_ARROWS)
    {
        bSuccess = false;
    }

    if ((pPickItem->Type == ITEM_JEWEL_OF_BLESS && iLevel >= 6) ||
        (pPickItem->Type == ITEM_JEWEL_OF_SOUL && iLevel >= 9))
    {
        bSuccess = false;
    }

    if (pPickItem->Type == ITEM_JEWEL_OF_BLESS && iType == ITEM_HORN_OF_FENRIR &&
        iDurability != 255)
    {
        CFenrirRepairMsgBox *pMsgBox = nullptr;
        CreateMessageBox(MSGBOX_LAYOUT_CLASS(SEASON3B::CFenrirRepairMsgBoxLayout, SessionOrigin()),
                         &pMsgBox);
        pMsgBox->SetSourceIndex(iSourceIndex);

        const int iIndex = targetControl->GetIndex(pItem->x, pItem->y);
        pMsgBox->SetTargetIndex(iIndex);

        pPickedItem->HidePickedItem();
        return true;
    }

    if (pPickItem->Type == ITEM_JEWEL_OF_HARMONY)
    {
        if (g_SocketItemMgr.IsSocketItem(pItem))
        {
            bSuccess = false;
        }
        else if (pItem->Jewel_Of_Harmony_Option != 0)
        {
            bSuccess = false;
        }
        else
        {
            const StrengthenItem strengthitem =
                g_pUIJewelHarmonyinfo->GetItemType(static_cast<int>(pItem->Type));

            if (strengthitem == SI_None)
            {
                bSuccess = false;
            }
        }
    }

    if (pPickItem->Type == ITEM_LOWER_REFINE_STONE || pPickItem->Type == ITEM_HIGHER_REFINE_STONE)
    {
        if (g_SocketItemMgr.IsSocketItem(pItem))
        {
            bSuccess = false;
        }
        else if (pItem->Jewel_Of_Harmony_Option == 0)
        {
            bSuccess = false;
        }
    }

    if (Check_LuckyItem(pItem->Type))
    {
        bSuccess = false;
        if (pPickItem->Type == ITEM_POTION + 161)
        {
            if (pItem->Jewel_Of_Harmony_Option == 0)
                bSuccess = true;
        }
        else if (pPickItem->Type == ITEM_POTION + 160)
        {
            if (pItem->Durability > 0)
                bSuccess = true;
        }
    }

    if (bSuccess)
    {
        const int targetIndex = targetControl->GetIndexByItem(pItem);
        SendRequestUse(iSourceIndex, targetIndex);
        PlayBuffer(SOUND_GET_ITEM01);
        return true;
    }

    return false;
}

bool CNewUIInventoryActionController::TryStackItems(CNewUIInventoryCtrl *targetControl,
                                                    ITEM *pPickItem, int iSourceIndex,
                                                    int iTargetIndex) const
{
    if (ITEM *pItem = targetControl->FindItem(iTargetIndex))
    {
        if (targetControl->AreItemsStackable(pPickItem, pItem))
        {
            SendRequestEquipmentItem(STORAGE_TYPE::INVENTORY, iSourceIndex, pPickItem,
                                     STORAGE_TYPE::INVENTORY, iTargetIndex);
            return true;
        }
    }

    return false;
}

bool CNewUIInventoryActionController::TryConsumeItem(CNewUIInventoryCtrl *targetControl,
                                                     ITEM *pItem, int iIndex) const
{
    if (pItem == nullptr)
    {
        return false;
    }

    if (pItem->Type == ITEM_TOWN_PORTAL_SCROLL)
    {
        SendRequestUse(iIndex, 0);
        return true;
    }

    const auto isApple = pItem->Type == ITEM_APPLE;
    const auto isPotion =
        (pItem->Type >= ITEM_APPLE && pItem->Type <= ITEM_ALE) ||
        (pItem->Type >= ITEM_SMALL_SHIELD_POTION && pItem->Type <= ITEM_LARGE_COMPLEX_POTION);

    if (isApple || isPotion || (pItem->Type == ITEM_POTION + 20 && pItem->Level == 0) ||
        (pItem->Type >= ITEM_JACK_OLANTERN_BLESSINGS && pItem->Type <= ITEM_JACK_OLANTERN_DRINK) ||
        (pItem->Type == ITEM_BOX_OF_LUCK && pItem->Level == 14) ||
        (pItem->Type >= ITEM_POTION + 70 && pItem->Type <= ITEM_POTION + 71) ||
        (pItem->Type >= ITEM_POTION + 72 && pItem->Type <= ITEM_POTION + 77) ||
        pItem->Type == ITEM_HELPER + 60 || pItem->Type == ITEM_POTION + 94 ||
        (pItem->Type >= ITEM_CHERRY_BLOSSOM_WINE &&
         pItem->Type <= ITEM_CHERRY_BLOSSOM_FLOWER_PETAL) ||
        (pItem->Type >= ITEM_POTION + 97 && pItem->Type <= ITEM_POTION + 98) ||
        pItem->Type == ITEM_HELPER + 81 || pItem->Type == ITEM_HELPER + 82 ||
        pItem->Type == ITEM_POTION + 133)
    {
        SendRequestUse(iIndex, 0);
        if (isApple)
        {
            PlayBuffer(SOUND_EAT_APPLE01);
        }
        else if (isPotion)
        {
            PlayBuffer(SOUND_DRINK01);
        }

        return true;
    }

    if (pItem->Type >= ITEM_POTION + 78 && pItem->Type <= ITEM_POTION + 82)
    {
        std::list<eBuffState> secretPotionbufflist;
        secretPotionbufflist.push_back(eBuff_SecretPotion1);
        secretPotionbufflist.push_back(eBuff_SecretPotion2);
        secretPotionbufflist.push_back(eBuff_SecretPotion3);
        secretPotionbufflist.push_back(eBuff_SecretPotion4);
        secretPotionbufflist.push_back(eBuff_SecretPotion5);

        if (g_isCharacterBufflist((&Hero->Object), secretPotionbufflist) == eBuffNone)
        {
            SendRequestUse(iIndex, 0);
            return true;
        }

        CreateOkMessageBox(I18N::Game::YouCannotUseThisItemWhileThePotionEffectsRemainActive,
                           RGBA(255, 30, 0, 255));
        return false;
    }

    if ((pItem->Type >= ITEM_HELPER + 54 && pItem->Type <= ITEM_HELPER + 57) ||
        (pItem->Type == ITEM_HELPER + 58 &&
         gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK_LORD))
    {
        WORD point[5] = {
            0,
        };
        point[0] = CharacterAttribute->Strength + CharacterAttribute->AddStrength;
        point[1] = CharacterAttribute->Dexterity + CharacterAttribute->AddDexterity;
        point[2] = CharacterAttribute->Vitality + CharacterAttribute->AddVitality;
        point[3] = CharacterAttribute->Energy + CharacterAttribute->AddEnergy;
        point[4] = CharacterAttribute->Charisma + CharacterAttribute->AddCharisma;

        const unsigned char nStat[MAX_CLASS][5] = {
            18, 18, 15, 30, 0,  28, 20, 25, 10, 0,  22, 25, 20, 15, 0,  26, 26, 26,
            26, 0,  26, 20, 20, 15, 25, 21, 21, 18, 23, 0,  32, 27, 25, 20, 0,
        };

        const int attributeType = pItem->Type - (ITEM_HELPER + 54);
        const int characterClass = gCharacterManager.GetBaseClass(Hero->Class);
        point[attributeType] -= nStat[characterClass][attributeType];

        if (point[attributeType] < (pItem->Durability * 10))
        {
            g_pMyInventory->SetStandbyItemKey(pItem->Key);
            CreateMessageBox(
                MSGBOX_LAYOUT_CLASS(SEASON3B::CUsePartChargeFruitMsgBoxLayout, SessionOrigin()));
            return false;
        }

        SendRequestUse(iIndex, 0);
        return true;
    }

    if (pItem->Type == ITEM_HELPER + 58 &&
        gCharacterManager.GetBaseClass(Hero->Class) != CLASS_DARK_LORD)
    {
        CreateOkMessageBox(I18N::Game::OnlyDarklordCanUseIt);
        return true;
    }

    if (pItem->Type == ITEM_ARMOR_OF_GUARDSMAN)
    {
        if (IsUnitedMarketPlace())
        {
            wchar_t szOutputText[512];
            mu_swprintf(szOutputText, L"%ls %ls", I18N::Game::YouCannotEnterChaosCastle,
                        I18N::Game::FromTheMarketInLorencia);
            CreateOkMessageBox(szOutputText);
            return true;
        }

        if (Hero->SafeZone == false)
        {
            CreateOkMessageBox(I18N::Game::YouCanOnlyUseThisInASafeZone);
            return false;
        }

        SocketClient->ToGameServer()->SendMiniGameOpeningStateRequest(MiniGameType::ChaosCastle,
                                                                      pItem->Level);
        g_pMyInventory->SetStandbyItemKey(pItem->Key);
        return true;
    }

    if (pItem->Type == ITEM_HELPER + 46)
    {
        const BYTE byPossibleLevel = CaculateFreeTicketLevel(FREETICKET_TYPE_DEVILSQUARE);
        SocketClient->ToGameServer()->SendMiniGameOpeningStateRequest(MiniGameType::DevilSquare,
                                                                      byPossibleLevel);
        return false;
    }

    if (pItem->Type == ITEM_HELPER + 47)
    {
        const BYTE byPossibleLevel = CaculateFreeTicketLevel(FREETICKET_TYPE_BLOODCASTLE);
        SocketClient->ToGameServer()->SendMiniGameOpeningStateRequest(MiniGameType::BloodCastle,
                                                                      byPossibleLevel);
        return false;
    }

    if (pItem->Type == ITEM_HELPER + 48)
    {
        if (Hero->SafeZone || gMapManager.InHellas())
        {
            g_pSystemLogBox->AddText(I18N::Game::CanTBeUsedInTheSafeZone, TYPE_ERROR_MESSAGE);
            return false;
        }

        SendRequestUse(iIndex, 0);
        return true;
    }

    if (pItem->Type == ITEM_HELPER + 61)
    {
        const BYTE byPossibleLevel = CaculateFreeTicketLevel(FREETICKET_TYPE_CURSEDTEMPLE);
        SocketClient->ToGameServer()->SendMiniGameOpeningStateRequest(MiniGameType::CursedTemple,
                                                                      byPossibleLevel);
        return true;
    }

    if (pItem->Type == ITEM_HELPER + 121)
    {
        if (Hero->SafeZone == false)
        {
            CreateOkMessageBox(I18N::Game::YouCanOnlyUseThisInASafeZone);
            return false;
        }

        SocketClient->ToGameServer()->SendMiniGameOpeningStateRequest(MiniGameType::ChaosCastle,
                                                                      pItem->Level);
        g_pMyInventory->SetStandbyItemKey(pItem->Key);
        return true;
    }

    if (pItem->Type == ITEM_SCROLL_OF_BLOOD)
    {
        SocketClient->ToGameServer()->SendMiniGameOpeningStateRequest(MiniGameType::CursedTemple,
                                                                      pItem->Level);
        return true;
    }

    if (pItem->Type == ITEM_DEVILS_INVITATION)
    {
        SocketClient->ToGameServer()->SendMiniGameOpeningStateRequest(MiniGameType::DevilSquare,
                                                                      pItem->Level);
        return true;
    }

    if (pItem->Type == ITEM_INVISIBILITY_CLOAK)
    {
        if (pItem->Level == 0)
        {
            g_pSystemLogBox->AddText(I18N::Game::IncorrectItem, TYPE_ERROR_MESSAGE);
        }
        else
        {
            SocketClient->ToGameServer()->SendMiniGameOpeningStateRequest(MiniGameType::BloodCastle,
                                                                          pItem->Level - 1);
        }

        return true;
    }

    if ((pItem->Type >= ITEM_SCROLL_OF_POISON && pItem->Type < ITEM_ETC + MAX_ITEM_INDEX) ||
        (pItem->Type >= ITEM_ORB_OF_TWISTING_SLASH &&
         pItem->Type <= ITEM_ORB_OF_GREATER_FORTITUDE) ||
        (pItem->Type >= ITEM_ORB_OF_FIRE_SLASH && pItem->Type <= ITEM_ORB_OF_DEATH_STAB) ||
        (pItem->Type == ITEM_WING + 20) ||
        (pItem->Type >= ITEM_SCROLL_OF_FIREBURST && pItem->Type <= ITEM_SCROLL_OF_ELECTRIC_SPARK) ||
        (pItem->Type == ITEM_SCROLL_OF_FIRE_SCREAM) ||
        (pItem->Type == ITEM_CRYSTAL_OF_DESTRUCTION) ||
        (pItem->Type == ITEM_CRYSTAL_OF_FLAME_STRIKE) ||
        (pItem->Type == ITEM_CRYSTAL_OF_RECOVERY) || (pItem->Type == ITEM_CRYSTAL_OF_MULTI_SHOT) ||
        (pItem->Type == ITEM_SCROLL_OF_CHAOTIC_DISEIER) ||
        (pItem->Type == ITEM_SCROLL_OF_GIGANTIC_STORM) ||
        (pItem->Type == ITEM_SCROLL_OF_WIZARDRY_ENHANCE))
    {
        bool bReadBookGem = true;

        if (pItem->Type == ITEM_SCROLL_OF_NOVA || pItem->Type == ITEM_SCROLL_OF_WIZARDRY_ENHANCE ||
            pItem->Type == ITEM_CRYSTAL_OF_MULTI_SHOT || pItem->Type == ITEM_CRYSTAL_OF_RECOVERY ||
            pItem->Type == ITEM_CRYSTAL_OF_DESTRUCTION)
        {
            if (g_csQuest.getQuestState2(QUEST_CHANGE_UP_3) != QUEST_END)
            {
                bReadBookGem = false;
            }
        }

        if (pItem->Type == ITEM_SCROLL_OF_CHAOTIC_DISEIER)
        {
            if (CharacterAttribute->Level < 220)
            {
                bReadBookGem = false;
            }
        }

        if (bReadBookGem)
        {
            const WORD wStrength = CharacterAttribute->Strength + CharacterAttribute->AddStrength;
            const WORD wEnergy = CharacterAttribute->Energy + CharacterAttribute->AddEnergy;

            if (CharacterAttribute->Level >= ItemAttribute[pItem->Type].RequireLevel &&
                wEnergy >= pItem->RequireEnergy && wStrength >= pItem->RequireStrength)
            {
                SendRequestUse(iIndex, 0);
            }

            return true;
        }

        return false;
    }

    if (pItem->Type == ITEM_FRUITS)
    {
        if (CharacterAttribute->Level < 10)
        {
            CreateOkMessageBox(I18N::Game::MustBeOverLevel10ToUseFruits);
            return true;
        }

        bool bEquipmentEmpty = true;
        for (int i = 0; i < MAX_EQUIPMENT; i++)
        {
            if (CharacterMachine->Equipment[i].Type != -1)
            {
                bEquipmentEmpty = false;
                break;
            }
        }

        if (!bEquipmentEmpty)
        {
            CreateOkMessageBox(I18N::Game::ToDecreaseTheFruitWeaponsArmorsAndOthersMustBeRemoved);
            return true;
        }

        if (pItem->Level == 4) // Command Fruit
        {
            if (gCharacterManager.GetBaseClass(CharacterAttribute->Class) != CLASS_DARK_LORD)
            {
                CreateOkMessageBox(I18N::Game::OnlyDarklordCanUseIt);
                return true;
            }
        }

        g_pMyInventory->SetStandbyItemKey(pItem->Key);
        CreateMessageBox(MSGBOX_LAYOUT_CLASS(SEASON3B::CUseFruitMsgBoxLayout, SessionOrigin()));
        return true;
    }

    if (pItem->Type == ITEM_LIFE_STONE_ITEM)
    {
        bool bUse = false;
        switch (pItem->Level)
        {
        case 0:
            bUse = true;
            break;
        case 1:
            if (Hero->GuildStatus != G_MASTER)
                bUse = true;
            break;
        }

        if (bUse)
        {
            SendRequestUse(iIndex, 0);
            return true;
        }

        return false;
    }

    if (pItem->Type == ITEM_HELPER + 69)
    {
        if (g_PortalMgr.IsRevivePositionSaved())
        {
            if (g_PortalMgr.IsPortalUsable())
            {
                g_pMyInventory->SetStandbyItemKey(pItem->Key);
                CreateMessageBox(
                    MSGBOX_LAYOUT_CLASS(SEASON3B::CUseReviveCharmMsgBoxLayout, SessionOrigin()));
            }
            else
            {
                CreateOkMessageBox(I18N::Game::YouCannotUseTheItemAtCertainApplicableLocations);
            }
        }

        return false;
    }

    if (pItem->Type == ITEM_HELPER + 70)
    {
        if (g_PortalMgr.IsPortalUsable())
        {
            if (pItem->Durability == 2)
            {
                if (g_PortalMgr.IsPortalPositionSaved())
                {
                    CreateOkMessageBox(
                        I18N::Game::ThisItemCannotBeUsedAlongWithAnItemThatSAlreadyInUse);
                }
                else
                {
                    g_pMyInventory->SetStandbyItemKey(pItem->Key);
                    CreateMessageBox(MSGBOX_LAYOUT_CLASS(SEASON3B::CUsePortalCharmMsgBoxLayout,
                                                         SessionOrigin()));
                }
            }
            else if (pItem->Durability == 1)
            {
                g_pMyInventory->SetStandbyItemKey(pItem->Key);
                CreateMessageBox(
                    MSGBOX_LAYOUT_CLASS(SEASON3B::CReturnPortalCharmMsgBoxLayout, SessionOrigin()));
            }
        }
        else
        {
            CreateOkMessageBox(I18N::Game::YouCannotUseTheItemAtCertainApplicableLocations);
        }

        return false;
    }

    if (pItem->Type == ITEM_HELPER + 66)
    {
        g_pMyInventory->SetStandbyItemKey(pItem->Key);
        CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CUseSantaInvitationMsgBoxLayout, SessionOrigin()));
    }

    return false;
}

bool CNewUIInventoryActionController::TryTransferBetweenInventorySections(
    CNewUIInventoryCtrl *sourceControl) const
{
    if (sourceControl == nullptr || g_pMyInventoryExt == nullptr)
    {
        return false;
    }

    ITEM *pItem = sourceControl->FindItemAtPt(MouseX, MouseY);
    if (pItem == nullptr)
    {
        return false;
    }

    const int sourceIndex = sourceControl->GetIndexByItem(pItem);
    if (sourceIndex < 0)
    {
        return false;
    }

    const ITEM_ATTRIBUTE *itemAttribute = &ItemAttribute[pItem->Type];
    int destinationIndex = -1;

    if (sourceControl == g_pMyInventory->GetInventoryCtrl())
    {
        destinationIndex =
            g_pMyInventoryExt->FindEmptySlot(itemAttribute->Width, itemAttribute->Height);
    }
    else
    {
        destinationIndex = m_pContext->FindEmptySlot(pItem);
    }

    if (destinationIndex == -1 || destinationIndex == sourceIndex)
    {
        return true;
    }

    if (!g_pMyInventory->GetInventoryCtrl()->CreatePickedItem(sourceControl, pItem))
    {
        return false;
    }

    sourceControl->RemoveItem(pItem);

    CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();
    if (pPickedItem == nullptr || pPickedItem->GetItem() == nullptr)
    {
        g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();
        return false;
    }

    pPickedItem->HidePickedItem();

    if (!SendRequestEquipmentItem(STORAGE_TYPE::INVENTORY, sourceIndex, pPickedItem->GetItem(),
                                  STORAGE_TYPE::INVENTORY, destinationIndex))
    {
        g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();
        return false;
    }

    return true;
}

} // namespace SEASON3B

bool SessionUiUnit::AddShopTitle(int key, CHARACTER *player, const std::wstring &title)
{
    return personalShopTitle_.AddShopTitle(key, player, title);
}

void SessionUiUnit::RemoveShopTitle(CHARACTER *player)
{
    personalShopTitle_.RemoveShopTitle(player);
}
void SessionUiUnit::RemoveAllShopTitle()
{
    personalShopTitle_.RemoveAllShopTitle();
}
void SessionUiUnit::RemoveAllShopTitleExceptHero()
{
    personalShopTitle_.RemoveAllShopTitleExceptHero();
}
CHARACTER *SessionUiUnit::FindCharacterTagShopTitle(int key)
{
    return personalShopTitle_.FindCharacter(key);
}
void SessionUiUnit::ShowShopTitles()
{
    personalShopTitle_.ShowShopTitles();
}
void SessionUiUnit::HideShopTitles()
{
    personalShopTitle_.HideShopTitles();
}
void SessionUiUnit::EnableShopTitleDraw(CHARACTER *player)
{
    personalShopTitle_.EnableShopTitleDraw(player);
}
void SessionUiUnit::DisableShopTitleDraw(CHARACTER *player)
{
    personalShopTitle_.DisableShopTitleDraw(player);
}
bool SessionUiUnit::IsShopTitleVisible(CHARACTER *player)
{
    return personalShopTitle_.IsShopTitleVisible(player);
}
bool SessionUiUnit::IsShopInViewport(CHARACTER *player)
{
    return personalShopTitle_.IsInViewport(player);
}
void SessionUiUnit::GetShopTitle(CHARACTER *player, std::wstring &title)
{
    personalShopTitle_.GetShopTitle(player, title);
}
void SessionUiUnit::GetShopTitleSummary(CHARACTER *player, std::wstring &summary)
{
    personalShopTitle_.GetShopTitleSummary(player, summary);
}

void SessionUiUnit::UpdatePersonalShopTitleImp()
{
    personalShopTitle_.Update();
}

void SessionUiUnit::DrawPersonalShopTitleImp()
{
    personalShopTitle_.Draw();
}

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CMsgBoxIGSBuyConfirm::CMsgBoxIGSBuyConfirm(SessionKeeper &keeper) : CNewUIMessageBoxBase(keeper)
{
    m_iMiddleCount = 7;
    m_wItemCode = 0;
    m_iPackageSeq = 0;
    m_iDisplaySeq = 0;
    m_iPriceSeq = 0;
    m_iCashType = 0;
    m_szItemName[0] = '\0';
    m_szItemPrice[0] = '\0';
    m_szItemPeriod[0] = '\0';

    for (int i = 0; i < NUM_LINE_CMB; i++)
    {
        m_szNotice[i][0] = '\0';
    }

    m_iNumNoticeLine = 0;
}

CMsgBoxIGSBuyConfirm::~CMsgBoxIGSBuyConfirm()
{
    Release();
}

bool CMsgBoxIGSBuyConfirm::Create(float fPriority)
{
    LoadImages();
    SetAddCallbackFunc();
    CNewUIMessageBoxBase::Create((IMAGE_IGS_WINDOW_WIDTH / 2) - (IMAGE_IGS_FRAME_WIDTH / 2),
                                 (IMAGE_IGS_WINDOW_HEIGHT / 2) - (IMAGE_IGS_FRAME_HEIGHT / 2),
                                 IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_FRAME_HEIGHT, fPriority);
    SetButtonInfo();
    SetMsgBackOpacity();
    return true;
}

void CMsgBoxIGSBuyConfirm::Initialize(WORD wItemCode, int iPackageSeq, int iDisplaySeq,
                                      int iPriceSeq, int iCashType, wchar_t *pszName,
                                      wchar_t *pszPrice, wchar_t *pszPeriod)
{
    m_wItemCode = wItemCode;
    m_iPackageSeq = iPackageSeq;
    m_iDisplaySeq = iDisplaySeq;
    m_iPriceSeq = iPriceSeq;
    m_iCashType = iCashType;

    mu_swprintf(m_szItemName, I18N::Game::ItemS, pszName);
    mu_swprintf(m_szItemPrice, I18N::Game::PriceS, pszPrice);
    mu_swprintf(m_szItemPeriod, I18N::Game::DurationS, pszPeriod);

    //int m_iNumNoticeLine = SeparateTextIntoLines( I18N::Game::BoughtItemsUsedOrTakenOutOfStorageCannotBeReturned, txtline[0], NUM_LINE_CMB, MAX_LENGTH_CMB);
    m_iNumNoticeLine = DivideStringByPixel(
        &m_szNotice[0][0], NUM_LINE_CMB, MAX_TEXT_LENGTH,
        I18N::Game::BoughtItemsUsedOrTakenOutOfStorageCannotBeReturned, IGS_TEXT_NOTICE_WIDTH);
}

void CMsgBoxIGSBuyConfirm::Release()
{
    CNewUIMessageBoxBase::Release();

    UnloadImages();
}

bool CMsgBoxIGSBuyConfirm::Update()
{
    m_BtnOk.Update();
    m_BtnCancel.Update();

    return true;
}

void CMsgBoxIGSBuyConfirm::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&CMsgBoxIGSBuyConfirm::LButtonUp), MSGBOX_EVENT_MOUSE_LBUTTON_UP);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSBuyConfirm::OKButtonDown), MSGBOX_EVENT_USER_COMMON_OK);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSBuyConfirm::CancelButtonDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
}

CALLBACK_RESULT CMsgBoxIGSBuyConfirm::LButtonUp(class CNewUIMessageBoxBase *pOwner,
                                                const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSBuyConfirm *>(pOwner);
    if (pOwnMsgBox)
    {
        if (pOwnMsgBox->m_BtnOk.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_OK);
            return CALLBACK_BREAK;
        }

        if (pOwnMsgBox->m_BtnCancel.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_CANCEL);
            return CALLBACK_BREAK;
        }
    }

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT CMsgBoxIGSBuyConfirm::OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                                   const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSBuyConfirm *>(pOwner);
    pOwnMsgBox->SocketClient->ToGameServer()->SendCashShopItemBuyRequest(
        pOwnMsgBox->m_iPackageSeq, pOwnMsgBox->m_iDisplaySeq, pOwnMsgBox->m_iPriceSeq,
        pOwnMsgBox->m_wItemCode, pOwnMsgBox->m_iCashType, 0);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    return CALLBACK_BREAK;
}

CALLBACK_RESULT CMsgBoxIGSBuyConfirm::CancelButtonDown(class CNewUIMessageBoxBase *pOwner,
                                                       const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    return CALLBACK_BREAK;
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CMsgBoxIGSBuyPackageItem::CMsgBoxIGSBuyPackageItem(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), g_InGameShopSystem(keeper.InGameShopSystemObject()),
      m_PackageInfo(keeper)
{
    m_iPackageSeq = 0;
    m_iDisplaySeq = 0;
    m_wItemCode = -1;
    m_iCashType = 0;
    m_szPackageName[0] = '\0';
    m_szPrice[0] = '\0';
    m_szPeriod[0] = '\0';

    for (int i = 0; i < UIMAX_TEXT_LINE; i++)
    {
        m_szDescription[i][0] = '\0';
    }
}

CMsgBoxIGSBuyPackageItem::~CMsgBoxIGSBuyPackageItem()
{
    Release();
}

bool CMsgBoxIGSBuyPackageItem::Create(float fPriority)
{
    LoadImages();

    SetAddCallbackFunc();

    CNewUIMessageBoxBase::Create((IMAGE_IGS_WINDOW_WIDTH / 2) - (IMAGE_IGS_FRAME_WIDTH / 2),
                                 (IMAGE_IGS_WINDOW_HEIGHT / 2) - (IMAGE_IGS_FRAME_HEIGHT / 2),
                                 IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_FRAME_HEIGHT, fPriority);

    if (g_pNewUI3DRenderMng)
    {
        g_pNewUI3DRenderMng->Add3DRenderObj(this);
    }

    CreateListBox();
    SetButtonInfo();
    SetMsgBackOpacity();
    return true;
}

void CMsgBoxIGSBuyPackageItem::Initialize(CShopPackage *pPackage)
{
    int iProductSeq;
    int iValue = 0;
    wchar_t szText[MAX_TEXT_LENGTH] = {
        '\0',
    };

    m_iPackageSeq = pPackage->PackageProductSeq;
    m_iDisplaySeq = pPackage->ProductDisplaySeq;
    m_iCashType = pPackage->CashType;

    if (pPackage->GiftFlag == 184)
    {
        m_BtnPresent.SetEnable(true);
    }
    else
    {
        m_BtnPresent.SetEnable(false);
    }

    wcsncpy(m_szPackageName, pPackage->PackageProductName, MAX_TEXT_LENGTH);
    ConvertGold(pPackage->Price, szText);
    mu_swprintf(m_szPrice, L"%ls %ls", szText, pPackage->PricUnitName);

    // Period
    pPackage->SetProductSeqFirst();
    pPackage->GetProductSeqNext(iProductSeq);

    g_InGameShopSystem.GetProductInfoFromProductSeq(
        iProductSeq, CInGameShopSystem::IGS_PRODUCT_ATT_TYPE_USE_LIMIT_PERIOD, iValue, szText);

    if (iValue > 0)
    {
        mu_swprintf(m_szPeriod, L"%d %ls", iValue, szText);
    }
    else
    {
        mu_swprintf(m_szPeriod, L"-");
    }

    m_wItemCode = _wtoi(pPackage->InGamePackageID);

    ZeroMemory(m_szDescription, sizeof(wchar_t) * UIMAX_TEXT_LINE * MAX_TEXT_LENGTH);

    g_RenderText.SetFont(LegacyFontRole::Normal);
    int nLine = DivideStringByPixel(&m_szDescription[0][0], UIMAX_TEXT_LINE, MAX_TEXT_LENGTH,
                                    pPackage->Description, IGS_LISTBOX_WIDTH, false, '#');

    for (int i = 0; i < nLine; ++i)
    {
        m_PackageInfo.AddText(m_szDescription[i]);
    }
}

void CMsgBoxIGSBuyPackageItem::Release()
{
    CNewUIMessageBoxBase::Release();

    if (g_pNewUI3DRenderMng)
        g_pNewUI3DRenderMng->Remove3DRenderObj(this);

    ReleaseListBox();

    UnloadImages();
}

bool CMsgBoxIGSBuyPackageItem::Update()
{
    m_BtnBuy.Update();
    m_BtnCancel.Update();
    m_BtnPresent.Update();
    ListBoxDoAction();
    return true;
}

bool CMsgBoxIGSBuyPackageItem::IsVisible() const
{
    return true;
}

void CMsgBoxIGSBuyPackageItem::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&CMsgBoxIGSBuyPackageItem::LButtonUp), MSGBOX_EVENT_MOUSE_LBUTTON_UP);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSBuyPackageItem::BuyBtnDown), MSGBOX_EVENT_USER_COMMON_OK);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSBuyPackageItem::PresentBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_INGAMESHOP_PRESENT);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSBuyPackageItem::CancelBtnDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
}

CALLBACK_RESULT CMsgBoxIGSBuyPackageItem::LButtonUp(class CNewUIMessageBoxBase *pOwner,
                                                    const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSBuyPackageItem *>(pOwner);
    if (pOwnMsgBox)
    {
        if (pOwnMsgBox->m_BtnBuy.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_OK);
            return CALLBACK_BREAK;
        }

        if (pOwnMsgBox->m_BtnPresent.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_CUSTOM_INGAMESHOP_PRESENT);
            return CALLBACK_BREAK;
        }

        if (pOwnMsgBox->m_BtnCancel.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_CANCEL);
            return CALLBACK_BREAK;
        }
    }
    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT CMsgBoxIGSBuyPackageItem::BuyBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                     const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSBuyPackageItem *>(pOwner);
    CMsgBoxIGSBuyConfirm *pMsgBox = NULL;
    CreateMessageBox(
        MSGBOX_LAYOUT_CLASS(CMsgBoxIGSBuyConfirmLayout, pOwnMsgBox->OriginatingSession()),
        &pMsgBox);

    pMsgBox->Initialize(pOwnMsgBox->m_wItemCode, pOwnMsgBox->m_iPackageSeq,
                        pOwnMsgBox->m_iDisplaySeq, 0, pOwnMsgBox->m_iCashType,
                        pOwnMsgBox->m_szPackageName, pOwnMsgBox->m_szPrice, pOwnMsgBox->m_szPeriod);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT CMsgBoxIGSBuyPackageItem::PresentBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                         const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSBuyPackageItem *>(pOwner);

    CMsgBoxIGSSendGift *pMsgBox = NULL;
    CreateMessageBox(
        MSGBOX_LAYOUT_CLASS(CMsgBoxIGSSendGiftLayout, pOwnMsgBox->OriginatingSession()), &pMsgBox);

    pMsgBox->Initialize(pOwnMsgBox->m_iPackageSeq, pOwnMsgBox->m_iDisplaySeq, 0,
                        pOwnMsgBox->m_wItemCode, pOwnMsgBox->m_iCashType,
                        pOwnMsgBox->m_szPackageName, pOwnMsgBox->m_szPrice, pOwnMsgBox->m_szPeriod);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    return CALLBACK_BREAK;
}

CALLBACK_RESULT CMsgBoxIGSBuyPackageItem::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                        const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

void CMsgBoxIGSBuyPackageItem::CreateListBox()
{
    m_PackageInfo.SetPosition(GetPos().x + IGS_LISTBOX_POS_X, GetPos().y + IGS_LISTBOX_POS_Y);
    m_PackageInfo.SetLineColorRender(false);
}

void CMsgBoxIGSBuyPackageItem::ListBoxDoAction()
{
    m_PackageInfo.DoAction();
}

void CMsgBoxIGSBuyPackageItem::ReleaseListBox()
{
    m_PackageInfo.Clear();
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CMsgBoxIGSBuySelectItem::CMsgBoxIGSBuySelectItem(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), g_InGameShopSystem(keeper.InGameShopSystemObject()),
      m_SelectBuyListBox(keeper)
{
    m_iPackageSeq = 0;
    m_iDisplaySeq = 0;
    m_wItemCode = -1;

    m_iDescriptionLine = 0;

    m_szPackageName[0] = '\0';
    m_szPrice[0] = '\0';

    for (int i = 0; i < UIMAX_TEXT_LINE; i++)
    {
        m_szDescription[i][0] = '\0';
    }
}

CMsgBoxIGSBuySelectItem::~CMsgBoxIGSBuySelectItem()
{
    Release();
}

// Create
bool CMsgBoxIGSBuySelectItem::Create(float fPriority)
{
    LoadImages();

    SetAddCallbackFunc();

    CNewUIMessageBoxBase::Create((IMAGE_IGS_WINDOW_WIDTH / 2) - (IMAGE_IGS_FRAME_WIDTH / 2),
                                 (IMAGE_IGS_WINDOW_HEIGHT / 2) - (IMAGE_IGS_FRAME_HEIGHT / 2),
                                 IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_FRAME_HEIGHT, fPriority);

    if (g_pNewUI3DRenderMng)
    {
        g_pNewUI3DRenderMng->Add3DRenderObj(this);
    }

    CreateListBox();
    SetButtonInfo();

    SetMsgBackOpacity();
    return true;
}

void CMsgBoxIGSBuySelectItem::Initialize(CShopPackage *pPackage)
{
    int iProductSeq, iPriceSeq;

    m_wItemCode = _wtoi(pPackage->InGamePackageID);

    m_iPackageSeq = pPackage->PackageProductSeq;
    m_iDisplaySeq = pPackage->ProductDisplaySeq;

    if (pPackage->GiftFlag == 184)
    {
        m_BtnPresent.SetEnable(true);
    }
    else
    {
        m_BtnPresent.SetEnable(false);
    }

    wcsncpy(m_szPackageName, pPackage->PackageProductName, MAX_TEXT_LENGTH);

    ZeroMemory(m_szDescription, sizeof(wchar_t) * UIMAX_TEXT_LINE * MAX_TEXT_LENGTH);

    g_RenderText.SetFont(LegacyFontRole::Normal);
    m_iDescriptionLine =
        DivideStringByPixel(&m_szDescription[0][0], UIMAX_TEXT_LINE, MAX_TEXT_LENGTH,
                            pPackage->Description, IGS_TEXT_DISCRIPTION_WIDTH, false, '#');

    pPackage->SetProductSeqFirst();
    if (pPackage->GetProductSeqNext(iProductSeq) == false)
    {
    }

    pPackage->SetPriceSeqFirst();
    while (pPackage->GetPriceSeqNext(iPriceSeq))
    {
        AddData(pPackage->PackageProductSeq, pPackage->ProductDisplaySeq, iPriceSeq, iProductSeq,
                pPackage->PricUnitName, pPackage->CashType);
    }
}

void CMsgBoxIGSBuySelectItem::Release()
{
    CNewUIMessageBoxBase::Release();

    if (g_pNewUI3DRenderMng)
        g_pNewUI3DRenderMng->Remove3DRenderObj(this);

    ReleaseListBox();

    UnloadImages();
}

bool CMsgBoxIGSBuySelectItem::Update()
{
    m_BtnBuy.Update();
    m_BtnCancel.Update();
    m_BtnPresent.Update();
    ListBoxDoAction();
    return true;
}

bool CMsgBoxIGSBuySelectItem::IsVisible() const
{
    return true;
}

void CMsgBoxIGSBuySelectItem::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&CMsgBoxIGSBuySelectItem::LButtonUp), MSGBOX_EVENT_MOUSE_LBUTTON_UP);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSBuySelectItem::BuyBtnDown), MSGBOX_EVENT_USER_COMMON_OK);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSBuySelectItem::PresentBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_INGAMESHOP_PRESENT);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSBuySelectItem::CancelBtnDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
}

CALLBACK_RESULT CMsgBoxIGSBuySelectItem::LButtonUp(class CNewUIMessageBoxBase *pOwner,
                                                   const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSBuySelectItem *>(pOwner);

    if (pOwnMsgBox)
    {
        if (pOwnMsgBox->m_BtnBuy.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_OK);
            return CALLBACK_BREAK;
        }
        if (pOwnMsgBox->m_BtnPresent.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_CUSTOM_INGAMESHOP_PRESENT);
            return CALLBACK_BREAK;
        }
        if (pOwnMsgBox->m_BtnCancel.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_CANCEL);
            return CALLBACK_BREAK;
        }
    }
    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT CMsgBoxIGSBuySelectItem::BuyBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                    const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSBuySelectItem *>(pOwner);
    IGS_SelectBuyItem *pItem = pOwnMsgBox->m_SelectBuyListBox.GetSelectedText();

    CMsgBoxIGSBuyConfirm *pMsgBox = NULL;
    CreateMessageBox(
        MSGBOX_LAYOUT_CLASS(CMsgBoxIGSBuyConfirmLayout, pOwnMsgBox->OriginatingSession()),
        &pMsgBox);
    pMsgBox->Initialize(pOwnMsgBox->m_wItemCode, pOwnMsgBox->m_iPackageSeq,
                        pOwnMsgBox->m_iDisplaySeq, pItem->m_iPriceSeq, pItem->m_iCashType,
                        pItem->m_szItemName, pItem->m_szItemPrice, pItem->m_szItemPeriod);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT CMsgBoxIGSBuySelectItem::PresentBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                        const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSBuySelectItem *>(pOwner);
    IGS_SelectBuyItem *pItem = pOwnMsgBox->m_SelectBuyListBox.GetSelectedText();

    CMsgBoxIGSSendGift *pMsgBox = NULL;
    CreateMessageBox(
        MSGBOX_LAYOUT_CLASS(CMsgBoxIGSSendGiftLayout, pOwnMsgBox->OriginatingSession()), &pMsgBox);
    pMsgBox->Initialize(pOwnMsgBox->m_iPackageSeq, pOwnMsgBox->m_iDisplaySeq, pItem->m_iPriceSeq,
                        pItem->m_wItemCode, pItem->m_iCashType, pOwnMsgBox->m_szPackageName,
                        pItem->m_szItemPrice, pItem->m_szItemPeriod);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT CMsgBoxIGSBuySelectItem::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                       const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSBuySelectItem *>(pOwner);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

// LoadImages

void CMsgBoxIGSBuySelectItem::CreateListBox()
{
    m_SelectBuyListBox.SetPosition(GetPos().x + IGS_LISTBOX_POS_X, GetPos().y + IGS_LISTBOX_POS_Y);
}

void CMsgBoxIGSBuySelectItem::ReleaseListBox()
{
    m_SelectBuyListBox.Clear();
}

void CMsgBoxIGSBuySelectItem::ListBoxDoAction()
{
    m_SelectBuyListBox.DoAction();

    if (m_SelectBuyListBox.IsChangeLine() == TRUE)
    {
        IGS_SelectBuyItem *pItem = m_SelectBuyListBox.GetSelectedText();
        wcscpy(m_szPrice, pItem->m_szItemPrice);
        m_wItemCode = pItem->m_wItemCode;
    }
}

void CMsgBoxIGSBuySelectItem::AddData(int iPackageSeq, int iDisplaySeq, int iPriceSeq,
                                      int iProductSeq, wchar_t *pszPriceUnit, int iCashType)
{
    int iValue;
    wchar_t szText[MAX_TEXT_LENGTH] = {
        '\0',
    };

    IGS_SelectBuyItem Item;
    memset(&Item, 0, sizeof(IGS_SelectBuyItem));

    Item.m_bIsSelected = FALSE;
    Item.m_iPackageSeq = iPackageSeq;
    Item.m_iDisplaySeq = iDisplaySeq;
    Item.m_iPriceSeq = iPriceSeq;
    Item.m_iCashType = iCashType;

    g_InGameShopSystem.GetProductInfoFromPriceSeq(
        iProductSeq, iPriceSeq, CInGameShopSystem::IGS_PRODUCT_ATT_TYPE_ITEMCODE, iValue, szText);
    Item.m_wItemCode = iValue;

    g_InGameShopSystem.GetProductInfoFromPriceSeq(
        iProductSeq, iPriceSeq, CInGameShopSystem::IGS_PRODUCT_ATT_TYPE_ITEMNAME, iValue, szText);
    wcscpy(Item.m_szItemName, szText);

    g_InGameShopSystem.GetProductInfoFromPriceSeq(
        iProductSeq, iPriceSeq, CInGameShopSystem::IGS_PRODUCT_ATT_TYPE_PRICE, iValue, szText);
    mu_swprintf(Item.m_szItemPrice, L"%ls %ls", szText, pszPriceUnit);

    g_InGameShopSystem.GetProductInfoFromPriceSeq(
        iProductSeq, iPriceSeq, CInGameShopSystem::IGS_PRODUCT_ATT_TYPE_USE_LIMIT_PERIOD, iValue,
        szText);
    if (iValue > 0)
    {
        mu_swprintf(Item.m_szItemPeriod, L"%d %ls", iValue, szText);
    }
    else
    {
        mu_swprintf(Item.m_szItemPeriod, L"-");
    }

    g_InGameShopSystem.GetProductInfoFromPriceSeq(
        iProductSeq, iPriceSeq, CInGameShopSystem::IGS_PRODUCT_ATT_TYPE_NUM, iValue, szText);
    if (iValue > 0)
    {
        mu_swprintf(Item.m_szAttribute, I18N::Game::QuantityDDurationS, iValue,
                    Item.m_szItemPeriod);
    }
    else
    {
        mu_swprintf(Item.m_szAttribute, I18N::Game::DurationS, Item.m_szItemPeriod);
    }

    m_SelectBuyListBox.AddText(Item);
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CMsgBoxIGSCommon::CMsgBoxIGSCommon(SessionKeeper &keeper) : CNewUIMessageBoxBase(keeper)
{
    memset(m_szTitle, 0, sizeof(m_szTitle));
    memset(m_szText, 0, sizeof(m_szText));
    m_iMiddleCount = 0;

    m_iMsgBoxWidth = IMAGE_IGS_FRAME_WIDTH;
    m_iMsgBoxHeight = IMAGE_IGS_FRAME_HEIGHT;
}

CMsgBoxIGSCommon::~CMsgBoxIGSCommon()
{
    Release();
}

bool CMsgBoxIGSCommon::Create(float fPriority)
{
    LoadImages();

    SetAddCallbackFunc();

    CNewUIMessageBoxBase::Create((IMAGE_IGS_WINDOW_WIDTH / 2) - (IMAGE_IGS_FRAME_WIDTH / 2),
                                 (IMAGE_IGS_WINDOW_HEIGHT / 2) - (IMAGE_IGS_FRAME_HEIGHT / 2),
                                 m_iMsgBoxWidth, m_iMsgBoxHeight, fPriority);

    SetButtonInfo();

    SetMsgBackOpacity();

    return true;
}

void CMsgBoxIGSCommon::Initialize(const wchar_t *pszTitle, const wchar_t *pszText)
{
    wcscpy(m_szTitle, pszTitle);

    m_iNumTextLine = DivideStringByPixel(&m_szText[0][0], NUM_LINE_CMB, MAX_TEXT_LENGTH, pszText,
                                         IGS_TEXT_ITEM_INFO_WIDTH, true, '#');

    if (m_iNumTextLine > IGS_NUM_TEXT_LIMIT_RENDER_MIDDLE_LINE)
    {
        m_iMiddleCount = m_iNumTextLine - IGS_NUM_TEXT_LIMIT_RENDER_MIDDLE_LINE;
    }

    m_iMsgBoxWidth = IMAGE_IGS_FRAME_WIDTH;
    m_iMsgBoxHeight = IMAGE_IGS_FRAME_HEIGHT + (m_iMiddleCount * IMAGE_IGS_LINE_HEIGHT);

    CNewUIMessageBoxBase::SetSize(m_iMsgBoxWidth, m_iMsgBoxHeight);
    SetButtonInfo();
}

void CMsgBoxIGSCommon::Release()
{
    CNewUIMessageBoxBase::Release();

    UnloadImages();
}

bool CMsgBoxIGSCommon::Update()
{
    m_BtnOk.Update();

    return true;
}

CALLBACK_RESULT CMsgBoxIGSCommon::LButtonUp(class CNewUIMessageBoxBase *pOwner,
                                            const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSCommon *>(pOwner);

    if (pOwnMsgBox)
    {
        if (pOwnMsgBox->m_BtnOk.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_OK);
            return CALLBACK_BREAK;
        }
    }

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT CMsgBoxIGSCommon::OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                               const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSCommon *>(pOwner);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

void CMsgBoxIGSCommon::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&CMsgBoxIGSCommon::LButtonUp), MSGBOX_EVENT_MOUSE_LBUTTON_UP);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSCommon::OKButtonDown), MSGBOX_EVENT_USER_COMMON_OK);
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

// Construction/Destruction
CMsgBoxIGSDeleteItemConfirm::CMsgBoxIGSDeleteItemConfirm(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper)
{
    m_iMiddleCount = 4;

    m_iStorageSeq = 0;     // ������ ����
    m_iStorageItemSeq = 0; // ������ ��ǰ ����
    m_szItemType = '\0';   // ��ǰ���� (C : ĳ��, P : ��ǰ)

    for (int i = 0; i < UIMAX_TEXT_LINE; i++)
    {
        m_szDescription[i][0] = '\0';
    }

    m_iDesciptionLine = 0;
}

CMsgBoxIGSDeleteItemConfirm::~CMsgBoxIGSDeleteItemConfirm()
{
    Release();
}

// Create
bool CMsgBoxIGSDeleteItemConfirm::Create(float fPriority)
{
    LoadImages();

    SetAddCallbackFunc();

    CNewUIMessageBoxBase::Create((IMAGE_IGS_WINDOW_WIDTH / 2) - (IMAGE_IGS_FRAME_WIDTH / 2),
                                 (IMAGE_IGS_WINDOW_HEIGHT / 2) - (IMAGE_IGS_FRAME_HEIGHT / 2),
                                 IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_FRAME_HEIGHT, fPriority);

    SetButtonInfo();

    SetMsgBackOpacity();

    return true;
}

// Initialize
void CMsgBoxIGSDeleteItemConfirm::Initialize(int iStorageSeq, int iStorageItemSeq,
                                             wchar_t szItemType)
{
    m_iStorageSeq = iStorageSeq;
    m_iStorageItemSeq = iStorageItemSeq;
    m_szItemType = szItemType;

    m_iDesciptionLine = DivideStringByPixel(
        &m_szDescription[0][0], UIMAX_TEXT_LINE, MAX_TEXT_LENGTH,
        I18N::Game::ThisWillDeleteTheSelectedItem, IGS_TEXT_DIVIDE_WIDTH, false, '#');
}

// Release
void CMsgBoxIGSDeleteItemConfirm::Release()
{
    CNewUIMessageBoxBase::Release();

    UnloadImages();
}

// Update
bool CMsgBoxIGSDeleteItemConfirm::Update()
{
    m_BtnDelete.Update();
    m_BtnCancel.Update();

    return true;
}

// Render

// LButtonUp
CALLBACK_RESULT CMsgBoxIGSDeleteItemConfirm::LButtonUp(class CNewUIMessageBoxBase *pOwner,
                                                       const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSDeleteItemConfirm *>(pOwner);

    if (pOwnMsgBox)
    {
        if (pOwnMsgBox->m_BtnDelete.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_OK);
            return CALLBACK_BREAK;
        }

        if (pOwnMsgBox->m_BtnCancel.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_CANCEL);
            return CALLBACK_BREAK;
        }
    }

    return CALLBACK_CONTINUE;
}

// OKButtonDown
CALLBACK_RESULT CMsgBoxIGSDeleteItemConfirm::OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                                          const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSDeleteItemConfirm *>(pOwner);

    pOwnMsgBox->SocketClient->ToGameServer()->SendCashShopDeleteStorageItemRequest(
        pOwnMsgBox->m_iStorageSeq, pOwnMsgBox->m_iStorageItemSeq, pOwnMsgBox->m_szItemType);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

// CancelButtonDown
CALLBACK_RESULT CMsgBoxIGSDeleteItemConfirm::CancelButtonDown(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSDeleteItemConfirm *>(pOwner);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

// SetAddCallbackFunc
void CMsgBoxIGSDeleteItemConfirm::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&CMsgBoxIGSDeleteItemConfirm::LButtonUp),
                    MSGBOX_EVENT_MOUSE_LBUTTON_UP);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSDeleteItemConfirm::OKButtonDown),
                    MSGBOX_EVENT_USER_COMMON_OK);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSDeleteItemConfirm::CancelButtonDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
}

// SetButtonInfo

// RenderFrame

// RenderTexts

// RenderButtons

// LoadImages

// UnloadImages

// LayOut

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

// Construction/Destruction
CMsgBoxIGSGiftStorageItemInfo::CMsgBoxIGSGiftStorageItemInfo(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), m_MessageInputBox(keeper)
{
    m_iStorageSeq = 0;     // ������ ����
    m_iStorageItemSeq = 0; // ������ ��ǰ ����
    m_wItemCode = -1;      // ������ �ڵ�

    m_szName[0] = '\0'; // ������ �̸�
    m_szNum[0] = '\0';
    m_szPeriod[0] = '\0';
    m_szItemType = '\0'; // ��ǰ���� (C : ĳ��, P : ��ǰ)

    m_szIDInfo[0] = '\0';  // �������� ĳ����ID
    m_szMessage[0] = '\0'; // �������� �޼���
}

CMsgBoxIGSGiftStorageItemInfo::~CMsgBoxIGSGiftStorageItemInfo()
{
    Release();
}

// Create
bool CMsgBoxIGSGiftStorageItemInfo::Create(float fPriority)
{
    LoadImages();
    SetAddCallbackFunc();

    CNewUIMessageBoxBase::Create((IMAGE_IGS_WINDOW_WIDTH / 2) - (IMAGE_IGS_FRAME_WIDTH / 2),
                                 (IMAGE_IGS_WINDOW_HEIGHT / 2) - (IMAGE_IGS_FRAME_HEIGHT / 2),
                                 IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_FRAME_HEIGHT, fPriority);

    if (g_pNewUI3DRenderMng)
    {
        g_pNewUI3DRenderMng->Add3DRenderObj(this);
    }

    SetButtonInfo();

    SetMsgBackOpacity();

    // �޼��� Input Box
    m_MessageInputBox.SetMultiline(TRUE);
    m_MessageInputBox.Init(IMAGE_IGS_FRAME_WIDTH - 30, 100, 50);
    m_MessageInputBox.SetPosition(GetPos().x + 22, GetPos().y + IGS_MESSAGE_INPUT_TEXT_POS_Y + 96);
    m_MessageInputBox.SetUseScrollbar(FALSE);
    m_MessageInputBox.SetTextLimit(MAX_GIFT_MESSAGE_SIZE);
    m_MessageInputBox.SetFont(LegacyFontRole::Normal);
    //m_MessageInputBox.SetOption(UIOPTION_NULL);
    m_MessageInputBox.SetBackColor(0, 0, 0, 0);
    m_MessageInputBox.SetState(UISTATE_NORMAL);
    m_MessageInputBox.SetTextColor(255, 0, 0, 0);

    return true;
}

// IsVisible
bool CMsgBoxIGSGiftStorageItemInfo::IsVisible() const
{
    return true;
}

// Initialize
void CMsgBoxIGSGiftStorageItemInfo::Initialize(int iStorageSeq, int iStorageItemSeq, WORD wItemCode,
                                               wchar_t szItemType, wchar_t *pszID,
                                               wchar_t *pszMessage, wchar_t *pszName,
                                               wchar_t *pszNum, wchar_t *pszPeriod)
{
    m_iStorageSeq = iStorageSeq;
    m_iStorageItemSeq = iStorageItemSeq;
    m_wItemCode = wItemCode;
    m_szItemType = szItemType;

    // Name
    wcscpy(m_szName, pszName);

    // Num
    mu_swprintf(m_szNum, I18N::Game::QuantityS, pszNum); // "���� : %ls"

    // Period
    mu_swprintf(m_szPeriod, I18N::Game::DurationS, pszPeriod); // "�Ⱓ : %ls"

    // ID Info
    // "\'%ls\' ���� ���� �����Դϴ�."
    mu_swprintf(m_szIDInfo, I18N::Game::ItSAGiftFromS, pszID);

    m_MessageInputBox.SetText(pszMessage);
}

// Release
void CMsgBoxIGSGiftStorageItemInfo::Release()
{
    CNewUIMessageBoxBase::Release();

    if (g_pNewUI3DRenderMng)
        g_pNewUI3DRenderMng->Remove3DRenderObj(this);

    UnloadImages();
}

// Update
bool CMsgBoxIGSGiftStorageItemInfo::Update()
{
    m_BtnUse.Update();
    m_BtnCancel.Update();

    return true;
}

// Render

// Render3D

// SetAddCallbackFunc
void CMsgBoxIGSGiftStorageItemInfo::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&CMsgBoxIGSGiftStorageItemInfo::LButtonUp),
                    MSGBOX_EVENT_MOUSE_LBUTTON_UP);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSGiftStorageItemInfo::OKButtonDown),
                    MSGBOX_EVENT_USER_COMMON_OK);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSGiftStorageItemInfo::CancelButtonDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
}

// LButtonUp
CALLBACK_RESULT CMsgBoxIGSGiftStorageItemInfo::LButtonUp(class CNewUIMessageBoxBase *pOwner,
                                                         const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSGiftStorageItemInfo *>(pOwner);

    if (pOwnMsgBox)
    {
        if (pOwnMsgBox->m_BtnUse.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_OK);
            return CALLBACK_BREAK;
        }

        if (pOwnMsgBox->m_BtnCancel.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_CANCEL);
            return CALLBACK_BREAK;
        }
    }

    return CALLBACK_CONTINUE;
}

// OKButtonDown
CALLBACK_RESULT CMsgBoxIGSGiftStorageItemInfo::OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                                            const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSGiftStorageItemInfo *>(pOwner);

    // ����ϱ� Ȯ�� â
    CMsgBoxIGSUseItemConfirm *pMsgBox = NULL;
    CreateMessageBox(
        MSGBOX_LAYOUT_CLASS(CMsgBoxIGSUseItemConfirmLayout, pOwnMsgBox->MessageBoxSessionOrigin()),
        &pMsgBox);
    pMsgBox->Initialize(pOwnMsgBox->m_iStorageSeq, pOwnMsgBox->m_iStorageItemSeq,
                        pOwnMsgBox->m_wItemCode, pOwnMsgBox->m_szItemType, pOwnMsgBox->m_szName);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

// CancelButtonDown
CALLBACK_RESULT CMsgBoxIGSGiftStorageItemInfo::CancelButtonDown(class CNewUIMessageBoxBase *pOwner,
                                                                const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSGiftStorageItemInfo *>(pOwner);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

// SetButtonInfo

// RenderFrame

// RenderTexts

// RenderButtons

// LoadImages

// UnloadImages

// LayOut

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CMsgBoxIGSSendGift::CMsgBoxIGSSendGift(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), m_IDInputBox(keeper), m_MessageInputBox(keeper)
{
    m_iPackageSeq = 0;
    m_iDisplaySeq = 0;
    m_iPriceSeq = 0;
    m_wItemCode = -1;
    m_iCashType = 0;

    m_szID[0] = '\0';
    m_szMessage[0] = '\0';

    m_szName[0] = '\0';
    m_szPrice[0] = '\0';
    m_szPeriod[0] = '\0';

    for (int i = 0; i < NUM_LINE_CMB; i++)
    {
        m_szNotice[i][0] = '\0';
    }

    m_iNumNoticeLine = 0;
}

CMsgBoxIGSSendGift::~CMsgBoxIGSSendGift()
{
    Release();
}

bool CMsgBoxIGSSendGift::Create(float fPriority)
{
    LoadImages();
    SetAddCallbackFunc();

    CNewUIMessageBoxBase::Create((IMAGE_IGS_WINDOW_WIDTH / 2) - (IMAGE_IGS_FRAME_WIDTH / 2),
                                 (IMAGE_IGS_WINDOW_HEIGHT / 2) - (IMAGE_IGS_FRAME_HEIGHT / 2),
                                 IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_FRAME_HEIGHT, fPriority);

    SetButtonInfo();
    InitInputBox();

    SetMsgBackOpacity();

    return true;
}

void CMsgBoxIGSSendGift::InitInputBox()
{
    m_IDInputBox.Init(IGS_ID_INPUT_TEXT_WIDTH, IGS_ID_INPUT_TEXT_HEIGHT, MAX_USERNAME_SIZE);
    m_IDInputBox.SetPosition(GetPos().x + IGS_ID_INPUT_TEXT_POS_X,
                             GetPos().y + IGS_ID_INPUT_TEXT_POS_Y);
    m_IDInputBox.SetTextColor(255, 0, 0, 0);
    m_IDInputBox.SetBackColor(255, 255, 255, 255);
    m_IDInputBox.SetFont(LegacyFontRole::Normal);
    m_IDInputBox.SetTextLimit(MAX_USERNAME_SIZE);
    m_IDInputBox.SetState(UISTATE_NORMAL);

    m_MessageInputBox.SetMultiline(TRUE);
    m_MessageInputBox.Init(IGS_MESSAGE_INPUT_TEXT_WIDTH, IGS_MESSAGE_INPUT_TEXT_HEIGHT,
                           IGS_MESSAGE_INPUT_TEXT_LINE_HEIGHT);
    m_MessageInputBox.SetPosition(GetPos().x + IGS_MESSAGE_INPUT_TEXT_POS_X,
                                  GetPos().y + IGS_MESSAGE_INPUT_TEXT_POS_Y);
    m_MessageInputBox.SetUseScrollbar(FALSE);
    m_MessageInputBox.SetTextLimit(MAX_GIFT_MESSAGE_SIZE);
    m_MessageInputBox.SetFont(LegacyFontRole::Normal);
    m_MessageInputBox.SetBackColor(0, 0, 0, 0);
    m_MessageInputBox.SetState(UISTATE_NORMAL);
    m_MessageInputBox.SetTextColor(255, 0, 0, 0);

    m_IDInputBox.GiveFocus();
}

void CMsgBoxIGSSendGift::Initialize(int iPackageSeq, int iDisplaySeq, int iPriceSeq,
                                    DWORD wItemCode, int iCashType, wchar_t *pszName,
                                    wchar_t *pszPrice, wchar_t *pszPeriod)
{
    m_iPackageSeq = iPackageSeq;
    m_iDisplaySeq = iDisplaySeq;
    m_iPriceSeq = iPriceSeq;
    m_wItemCode = wItemCode;
    m_iCashType = iCashType;

    mu_swprintf(m_szName, I18N::Game::ItemS, pszName);
    mu_swprintf(m_szPrice, I18N::Game::PriceS, pszPrice);
    mu_swprintf(m_szPeriod, I18N::Game::DurationS, pszPeriod);

    m_iNumNoticeLine = DivideStringByPixel(&m_szNotice[0][0], NUM_LINE_CMB, MAX_TEXT_LENGTH,
                                           I18N::Game::GiftedItemsCannotBeReturnedDeliverTheGiftS,
                                           IGS_TEXT_NOTICE_WIDTH);
}

void CMsgBoxIGSSendGift::Release()
{
    CNewUIMessageBoxBase::Release();
    UnloadImages();
}

bool CMsgBoxIGSSendGift::Update()
{
    m_BtnOk.Update();
    m_BtnCancel.Update();

    m_IDInputBox.DoAction();
    m_MessageInputBox.DoAction();

    if (m_IDInputBox.HaveFocus() == TRUE)
        m_IDInputBox.GetText(m_szID, MAX_USERNAME_SIZE + 1);

    if (m_MessageInputBox.HaveFocus() == TRUE)
        m_MessageInputBox.GetText(m_szMessage, MAX_GIFT_MESSAGE_SIZE);

    if (IsPress(VK_TAB) == true)
    {
        ChangeInputBoxFocus();
    }

    return true;
}

void CMsgBoxIGSSendGift::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&CMsgBoxIGSSendGift::LButtonUp), MSGBOX_EVENT_MOUSE_LBUTTON_UP);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSSendGift::OKButtonDown), MSGBOX_EVENT_USER_COMMON_OK);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSSendGift::CancelButtonDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
}

CALLBACK_RESULT CMsgBoxIGSSendGift::LButtonUp(class CNewUIMessageBoxBase *pOwner,
                                              const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSSendGift *>(pOwner);

    if (pOwnMsgBox)
    {
        if (pOwnMsgBox->m_BtnOk.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_OK);
            return CALLBACK_BREAK;
        }

        if (pOwnMsgBox->m_BtnCancel.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_CANCEL);
            return CALLBACK_BREAK;
        }
    }
    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT CMsgBoxIGSSendGift::OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                                 const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSSendGift *>(pOwner);

    if (pOwnMsgBox->m_szID[0] == '\0')
    {
        CMsgBoxIGSCommon *pMsgBox = NULL;
        CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(CMsgBoxIGSCommonLayout, pOwnMsgBox->MessageBoxSessionOrigin()),
            &pMsgBox);
        pMsgBox->Initialize(I18N::Game::Error, I18N::Game::GiftRecipientSIDIsMissing);
    }
    else if (wcscmp(pOwnMsgBox->m_szID, Hero->ID) == 0)
    {
        CMsgBoxIGSCommon *pMsgBox = NULL;
        CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(CMsgBoxIGSCommonLayout, pOwnMsgBox->MessageBoxSessionOrigin()),
            &pMsgBox);
        pMsgBox->Initialize(I18N::Game::Error, I18N::Game::YouCannotSendAGiftToYourself);
    }
    else
    {
        CMsgBoxIGSSendGiftConfirm *pMsgBox = NULL;
        CreateMessageBox(MSGBOX_LAYOUT_CLASS(CMsgBoxIGSSendGiftConfirmLayout,
                                             pOwnMsgBox->MessageBoxSessionOrigin()),
                         &pMsgBox);
        pMsgBox->Initialize(pOwnMsgBox->m_iPackageSeq, pOwnMsgBox->m_iDisplaySeq,
                            pOwnMsgBox->m_iPriceSeq, pOwnMsgBox->m_wItemCode,
                            pOwnMsgBox->m_iCashType, pOwnMsgBox->m_szID, pOwnMsgBox->m_szMessage,
                            pOwnMsgBox->m_szName, pOwnMsgBox->m_szPrice, pOwnMsgBox->m_szPeriod);
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    return CALLBACK_BREAK;
}

CALLBACK_RESULT CMsgBoxIGSSendGift::CancelButtonDown(class CNewUIMessageBoxBase *pOwner,
                                                     const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSSendGift *>(pOwner);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    return CALLBACK_BREAK;
}

void CMsgBoxIGSSendGift::ChangeInputBoxFocus()
{
    if (m_IDInputBox.HaveFocus() == TRUE)
    {
        m_MessageInputBox.GiveFocus();
    }
    else if (m_MessageInputBox.HaveFocus() == TRUE)
    {
        m_IDInputBox.GiveFocus();
    }
    else
    {
        m_IDInputBox.GiveFocus();
    }
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CMsgBoxIGSSendGiftConfirm::CMsgBoxIGSSendGiftConfirm(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper)
{
    m_iMiddleCount = 7;
    m_iPackageSeq = 0;
    m_iDisplaySeq = 0;
    m_iPriceSeq = 0;
    m_wItemCode = -1;
    m_iCashType = 0;
    m_szItemName[0] = '\0';
    m_szItemPrice[0] = '\0';
    m_szItemPeriod[0] = '\0';

    for (int i = 0; i < NUM_LINE_CMB; i++)
    {
        m_szNotice[i][0] = '\0';
    }

    m_iNumNoticeLine = 0;
}

CMsgBoxIGSSendGiftConfirm::~CMsgBoxIGSSendGiftConfirm()
{
    Release();
}

bool CMsgBoxIGSSendGiftConfirm::Create(float fPriority)
{
    LoadImages();
    SetAddCallbackFunc();

    CNewUIMessageBoxBase::Create((IMAGE_IGS_WINDOW_WIDTH / 2) - (IMAGE_IGS_FRAME_WIDTH / 2),
                                 (IMAGE_IGS_WINDOW_HEIGHT / 2) - (IMAGE_IGS_FRAME_HEIGHT / 2),
                                 IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_FRAME_HEIGHT, fPriority);

    SetButtonInfo();

    SetMsgBackOpacity();

    return true;
}

void CMsgBoxIGSSendGiftConfirm::Initialize(int iPackageSeq, int iDisplaySeq, int iPriceSeq,
                                           DWORD wItemCode, int iCashType, wchar_t *pszID,
                                           wchar_t *pszMessage, wchar_t *pszName, wchar_t *pszPrice,
                                           wchar_t *pszPeriod)
{
    m_iPackageSeq = iPackageSeq;
    m_iDisplaySeq = iDisplaySeq;
    m_iPriceSeq = iPriceSeq;
    m_wItemCode = wItemCode;
    m_iCashType = iCashType;

    wcscpy(m_szID, pszID);
    wcscpy(m_szMessage, pszMessage);

    wcscpy(m_szItemName, pszName);
    wcscpy(m_szItemPrice, pszPrice);
    wcscpy(m_szItemPeriod, pszPeriod);

    m_iNumNoticeLine = DivideStringByPixel(
        &m_szNotice[0][0], NUM_LINE_CMB, MAX_TEXT_LENGTH,
        I18N::Game::BoughtItemsUsedOrTakenOutOfStorageCannotBeReturned, IGS_TEXT_NOTICE_WIDTH);
}

void CMsgBoxIGSSendGiftConfirm::Release()
{
    CNewUIMessageBoxBase::Release();

    UnloadImages();
}

bool CMsgBoxIGSSendGiftConfirm::Update()
{
    m_BtnOk.Update();
    m_BtnCancel.Update();

    return true;
}

void CMsgBoxIGSSendGiftConfirm::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&CMsgBoxIGSSendGiftConfirm::LButtonUp), MSGBOX_EVENT_MOUSE_LBUTTON_UP);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSSendGiftConfirm::OKButtonDown),
                    MSGBOX_EVENT_USER_COMMON_OK);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSSendGiftConfirm::CancelButtonDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
}

CALLBACK_RESULT CMsgBoxIGSSendGiftConfirm::LButtonUp(class CNewUIMessageBoxBase *pOwner,
                                                     const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSSendGiftConfirm *>(pOwner);
    if (pOwnMsgBox)
    {
        if (pOwnMsgBox->m_BtnOk.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_OK);
            return CALLBACK_BREAK;
        }

        if (pOwnMsgBox->m_BtnCancel.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_CANCEL);
            return CALLBACK_BREAK;
        }
    }

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT CMsgBoxIGSSendGiftConfirm::OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                                        const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSSendGiftConfirm *>(pOwner);

    pOwnMsgBox->SocketClient->ToGameServer()->SendCashShopItemGiftRequest(
        pOwnMsgBox->m_iPackageSeq, pOwnMsgBox->m_iDisplaySeq, pOwnMsgBox->m_iPriceSeq,
        pOwnMsgBox->m_wItemCode, pOwnMsgBox->m_iCashType, 0, pOwnMsgBox->m_szID,
        pOwnMsgBox->m_szMessage);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT CMsgBoxIGSSendGiftConfirm::CancelButtonDown(class CNewUIMessageBoxBase *pOwner,
                                                            const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSSendGiftConfirm *>(pOwner);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CMsgBoxIGSStorageItemInfo::CMsgBoxIGSStorageItemInfo(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper)
{
    m_iStorageSeq = 0;
    m_iStorageItemSeq = 0;
    m_wItemCode = -1;

    m_szName[0] = '\0';
    m_szNum[0] = '\0';
    m_szPeriod[0] = '\0';
    m_szItemType = '\0';
}

CMsgBoxIGSStorageItemInfo::~CMsgBoxIGSStorageItemInfo()
{
    Release();
}

bool CMsgBoxIGSStorageItemInfo::Create(float fPriority)
{
    LoadImages();

    SetAddCallbackFunc();

    CNewUIMessageBoxBase::Create((IMAGE_IGS_WINDOW_WIDTH / 2) - (IMAGE_IGS_FRAME_WIDTH / 2),
                                 (IMAGE_IGS_WINDOW_HEIGHT / 2) - (IMAGE_IGS_FRAME_HEIGHT / 2),
                                 IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_FRAME_HEIGHT, fPriority);

    if (g_pNewUI3DRenderMng)
    {
        g_pNewUI3DRenderMng->Add3DRenderObj(this);
    }

    SetButtonInfo();

    SetMsgBackOpacity();

    return true;
}

bool CMsgBoxIGSStorageItemInfo::IsVisible() const
{
    return true;
}

void CMsgBoxIGSStorageItemInfo::Initialize(int iStorageSeq, int iStorageItemSeq, WORD wItemCode,
                                           char szItemType, wchar_t *pszName, wchar_t *pszNum,
                                           wchar_t *pszPeriod)
{
    m_iStorageSeq = iStorageSeq;
    m_iStorageItemSeq = iStorageItemSeq;
    m_wItemCode = wItemCode;
    m_szItemType = szItemType;

    wcscpy(m_szName, pszName);
    mu_swprintf(m_szNum, I18N::Game::QuantityS, pszNum);
    mu_swprintf(m_szPeriod, I18N::Game::DurationS, pszPeriod);
}

void CMsgBoxIGSStorageItemInfo::Release()
{
    CNewUIMessageBoxBase::Release();

    if (g_pNewUI3DRenderMng)
        g_pNewUI3DRenderMng->Remove3DRenderObj(this);

    UnloadImages();
}

bool CMsgBoxIGSStorageItemInfo::Update()
{
    m_BtnUse.Update();
    m_BtnCancel.Update();
    return true;
}

void CMsgBoxIGSStorageItemInfo::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&CMsgBoxIGSStorageItemInfo::LButtonUp), MSGBOX_EVENT_MOUSE_LBUTTON_UP);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSStorageItemInfo::OKButtonDown),
                    MSGBOX_EVENT_USER_COMMON_OK);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSStorageItemInfo::CancelButtonDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
}

CALLBACK_RESULT CMsgBoxIGSStorageItemInfo::LButtonUp(class CNewUIMessageBoxBase *pOwner,
                                                     const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSStorageItemInfo *>(pOwner);

    if (pOwnMsgBox)
    {
        if (pOwnMsgBox->m_BtnUse.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_OK);
            return CALLBACK_BREAK;
        }

        if (pOwnMsgBox->m_BtnCancel.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_CANCEL);
            return CALLBACK_BREAK;
        }
    }

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT CMsgBoxIGSStorageItemInfo::OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                                        const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSStorageItemInfo *>(pOwner);

    CMsgBoxIGSUseItemConfirm *pMsgBox = NULL;
    CreateMessageBox(
        MSGBOX_LAYOUT_CLASS(CMsgBoxIGSUseItemConfirmLayout, pOwnMsgBox->MessageBoxSessionOrigin()),
        &pMsgBox);
    pMsgBox->Initialize(pOwnMsgBox->m_iStorageSeq, pOwnMsgBox->m_iStorageItemSeq,
                        pOwnMsgBox->m_wItemCode, pOwnMsgBox->m_szItemType, pOwnMsgBox->m_szName);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT CMsgBoxIGSStorageItemInfo::CancelButtonDown(class CNewUIMessageBoxBase *pOwner,
                                                            const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSStorageItemInfo *>(pOwner);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

CMsgBoxIGSUseBuffConfirm::CMsgBoxIGSUseBuffConfirm(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper)
{
    m_iMiddleCount = 5;

    for (int i = 0; i < UIMAX_TEXT_LINE; i++)
    {
        m_szDescription[i][0] = '\0';
    }

    m_iStorageSeq = 0;
    m_iStorageItemSeq = 0;
    m_wItemCode = -1;
    m_szItemType = '\0';
}

CMsgBoxIGSUseBuffConfirm::~CMsgBoxIGSUseBuffConfirm()
{
    Release();
}

bool CMsgBoxIGSUseBuffConfirm::Create(float fPriority)
{
    LoadImages();

    SetAddCallbackFunc();

    CNewUIMessageBoxBase::Create((IMAGE_IGS_WINDOW_WIDTH / 2) - (IMAGE_IGS_FRAME_WIDTH / 2),
                                 (IMAGE_IGS_WINDOW_HEIGHT / 2) - (IMAGE_IGS_FRAME_HEIGHT / 2),
                                 IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_FRAME_HEIGHT, fPriority);

    SetButtonInfo();

    SetMsgBackOpacity();

    return true;
}

void CMsgBoxIGSUseBuffConfirm::Initialize(int iStorageSeq, int iStorageItemSeq, WORD wItemCode,
                                          wchar_t szItemType, wchar_t *pszItemName,
                                          wchar_t *pszBuffName)
{
    wchar_t szText[256] = {
        0,
    };

    m_iStorageSeq = iStorageSeq;
    m_iStorageItemSeq = iStorageItemSeq;
    m_wItemCode = wItemCode;
    m_szItemType = szItemType;
    wcscpy(m_szCurrentBuffName, pszBuffName);

    mu_swprintf(szText, I18N::Game::UsingTheSItemWillNegate, pszItemName, pszBuffName, pszItemName);
    m_iDesciptionLine =
        DivideStringByPixel(&m_szDescription[0][0], UIMAX_TEXT_LINE, MAX_TEXT_LENGTH, szText,
                            IGS_TEXT_DIVIDE_WIDTH, false, '#');
}

void CMsgBoxIGSUseBuffConfirm::Release()
{
    CNewUIMessageBoxBase::Release();

    UnloadImages();
}

bool CMsgBoxIGSUseBuffConfirm::Update()
{
    m_BtnOk.Update();
    m_BtnCancel.Update();

    return true;
}

CALLBACK_RESULT CMsgBoxIGSUseBuffConfirm::LButtonUp(class CNewUIMessageBoxBase *pOwner,
                                                    const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSUseBuffConfirm *>(pOwner);

    if (pOwnMsgBox)
    {
        if (pOwnMsgBox->m_BtnOk.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_OK);
            return CALLBACK_BREAK;
        }

        if (pOwnMsgBox->m_BtnCancel.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_CANCEL);
            return CALLBACK_BREAK;
        }
    }

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT CMsgBoxIGSUseBuffConfirm::OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                                       const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSUseBuffConfirm *>(pOwner);

    pOwnMsgBox->SocketClient->ToGameServer()->SendCashShopStorageItemConsumeRequest(
        pOwnMsgBox->m_iStorageSeq, pOwnMsgBox->m_iStorageItemSeq, pOwnMsgBox->m_wItemCode,
        pOwnMsgBox->m_szItemType);
    pOwnMsgBox->SocketClient->ToGameServer()->SendCashShopPointInfoRequest();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT CMsgBoxIGSUseBuffConfirm::CancelButtonDown(class CNewUIMessageBoxBase *pOwner,
                                                           const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSUseBuffConfirm *>(pOwner);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

void CMsgBoxIGSUseBuffConfirm::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&CMsgBoxIGSUseBuffConfirm::LButtonUp), MSGBOX_EVENT_MOUSE_LBUTTON_UP);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSUseBuffConfirm::OKButtonDown), MSGBOX_EVENT_USER_COMMON_OK);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSUseBuffConfirm::CancelButtonDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

// Construction/Destruction
CMsgBoxIGSUseItemConfirm::CMsgBoxIGSUseItemConfirm(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), g_BuffStateSystem(keeper.BuffStateSystemObject())
{
    m_iMiddleCount = 7;

    for (int i = 0; i < UIMAX_TEXT_LINE; i++)
    {
        m_szDescription[i][0] = '\0';
    }

    m_iStorageSeq = 0;     // ������ ����
    m_iStorageItemSeq = 0; // ������ ��ǰ ����
    m_wItemCode = -1;      // ������ �ڵ�
    m_szItemType = '\0';   // ��ǰ���� (C : ĳ��, P : ��ǰ)
}

CMsgBoxIGSUseItemConfirm::~CMsgBoxIGSUseItemConfirm()
{
    Release();
}

// Create
bool CMsgBoxIGSUseItemConfirm::Create(float fPriority)
{
    LoadImages();

    SetAddCallbackFunc();

    CNewUIMessageBoxBase::Create((IMAGE_IGS_WINDOW_WIDTH / 2) - (IMAGE_IGS_FRAME_WIDTH / 2),
                                 (IMAGE_IGS_WINDOW_HEIGHT / 2) - (IMAGE_IGS_FRAME_HEIGHT / 2),
                                 IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_FRAME_HEIGHT, fPriority);

    SetButtonInfo();

    SetMsgBackOpacity();

    return true;
}

// Initialize
void CMsgBoxIGSUseItemConfirm::Initialize(int iStorageSeq, int iStorageItemSeq, WORD wItemCode,
                                          wchar_t szItemType, wchar_t *pszItemName)
{
    wchar_t szText[256] = {
        0,
    };

    m_iStorageSeq = iStorageSeq;
    m_iStorageItemSeq = iStorageItemSeq;
    m_wItemCode = wItemCode;
    m_szItemType = szItemType;

    wcscpy(m_szItemName, pszItemName);

    // Description
    mu_swprintf(szText, I18N::Game::DoYouWishToUseS, pszItemName);
    m_iDesciptionLine =
        DivideStringByPixel(&m_szDescription[0][0], UIMAX_TEXT_LINE, MAX_TEXT_LENGTH, szText,
                            IGS_TEXT_DIVIDE_WIDTH, false, '#');
}

// Release
void CMsgBoxIGSUseItemConfirm::Release()
{
    CNewUIMessageBoxBase::Release();

    UnloadImages();
}

// Update
bool CMsgBoxIGSUseItemConfirm::Update()
{
    m_BtnOk.Update();
    m_BtnCancel.Update();

    return true;
}

// Render

// LButtonUp
CALLBACK_RESULT CMsgBoxIGSUseItemConfirm::LButtonUp(class CNewUIMessageBoxBase *pOwner,
                                                    const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSUseItemConfirm *>(pOwner);

    if (pOwnMsgBox)
    {
        if (pOwnMsgBox->m_BtnOk.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_OK);
            return CALLBACK_BREAK;
        }

        if (pOwnMsgBox->m_BtnCancel.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_CANCEL);
            return CALLBACK_BREAK;
        }
    }

    return CALLBACK_CONTINUE;
}

// OKButtonDown
CALLBACK_RESULT CMsgBoxIGSUseItemConfirm::OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                                       const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSUseItemConfirm *>(pOwner);

    // ��������� ����Ϸ��� ����Ÿ���� ������ ��� �޼��� ó��
    BuffScriptLoader &pBuffInfo = TheBuffInfo();
    int iBuffType = pBuffInfo.GetBuffType(pOwnMsgBox->m_wItemCode);
    wchar_t szBuffName[MAX_TEXT_LENGTH] = {
        '\0',
    };
    bool bEqualBuff =
        g_BuffStateSystem.IsEqualBuffType(Hero->Object.m_BuffMap, iBuffType, szBuffName);

#ifdef LEM_FIX_WARNINNGMSG_DELETE
    bEqualBuff = false;
#endif // LEM_FIX_WARNINNGMSG_DELETE [lem_2010.8.18]

    if (bEqualBuff)
    {
        //  ���� ���â
        CMsgBoxIGSUseBuffConfirm *pMsgBox = NULL;
        CreateMessageBox(MSGBOX_LAYOUT_CLASS(CMsgBoxIGSUseBuffConfirmLayout,
                                             pOwnMsgBox->MessageBoxSessionOrigin()),
                         &pMsgBox);
        pMsgBox->Initialize(pOwnMsgBox->m_iStorageSeq, pOwnMsgBox->m_iStorageItemSeq,
                            pOwnMsgBox->m_wItemCode, pOwnMsgBox->m_szItemType,
                            pOwnMsgBox->m_szItemName, szBuffName);
    }
    else
    {
        pOwnMsgBox->SocketClient->ToGameServer()->SendCashShopStorageItemConsumeRequest(
            pOwnMsgBox->m_iStorageSeq, pOwnMsgBox->m_iStorageItemSeq, pOwnMsgBox->m_wItemCode,
            pOwnMsgBox->m_szItemType);
        pOwnMsgBox->SocketClient->ToGameServer()->SendCashShopPointInfoRequest();
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

// CancelButtonDown
CALLBACK_RESULT CMsgBoxIGSUseItemConfirm::CancelButtonDown(class CNewUIMessageBoxBase *pOwner,
                                                           const leaf::xstreambuf &xParam)
{
    auto *pOwnMsgBox = dynamic_cast<CMsgBoxIGSUseItemConfirm *>(pOwner);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

// SetAddCallbackFunc
void CMsgBoxIGSUseItemConfirm::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&CMsgBoxIGSUseItemConfirm::LButtonUp), MSGBOX_EVENT_MOUSE_LBUTTON_UP);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSUseItemConfirm::OKButtonDown), MSGBOX_EVENT_USER_COMMON_OK);
    AddCallbackFunc(BindSelf(&CMsgBoxIGSUseItemConfirm::CancelButtonDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
}

// SetButtonInfo

// RenderFrame

// RenderTexts

// RenderButtons

// LoadImages

// UnloadImages

// LayOut

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef PBG_ADD_INGAMESHOP_UI_ITEMSHOP

using namespace SEASON3B;

CNewUIInGameShop::CNewUIInGameShop(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), cameraProjection_(keeper.CameraProjectionObject()),
      g_InGameShopSystem(keeper.InGameShopSystemObject()), m_StorageItemListBox(keeper),
      m_ZoneButton(keeper), m_CategoryButton(keeper), m_ListBoxTabButton(keeper),
      m_ViewDetailButton(keeper), m_CashGiftButton(keeper), m_CashChargeButton(keeper),
      m_CashRefreshButton(keeper), m_UseButton(keeper), m_PrevButton(keeper), m_NextButton(keeper),
      m_CloseButton(keeper), m_StoragePrevButton(keeper), m_StorageNextButton(keeper)
{
    Init();
}

CNewUIInGameShop::~CNewUIInGameShop()
{
    Release();
}

void CNewUIInGameShop::LogSystemShow()
{
    g_ConsoleDebug.Write(MCD_NORMAL, L"InGameShopStatue.Txt CallStack - CNewUISystem.Show()\r\n");
}

void CNewUIInGameShop::Init()
{
    m_ItemAngle = false;
    m_bLoadBanner = false;
    m_bBannerLink = false;
    m_iStorageTotalItemCnt = 0;
    m_iStorageCurrentPageItemCnt = 0;
    m_iStorageTotalPage = 0;
    m_iStorageCurrentPage = 0;
    m_iSelectedStorageItemIndex = 0;
    m_iStorageCurrentPageReceiveItemCnt = 0;
    m_bRequestCurrentPage = false;
}

void CNewUIInGameShop::Release()
{
    UnloadImages();

    ReleaseBanner();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }

    ClearAllStorageItem();
}

bool CNewUIInGameShop::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (pNewUIMng == NULL)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_INGAMESHOP, this);

    SetPos(x, y);
    LoadImages();
    SetBtnInfo();
    Show(false); //visible()? flase?

    return true;
}

bool CNewUIInGameShop::IsInGameShopRect(float _x, float _y)
{
    if (!g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_INGAMESHOP))
        return false;

    RECT _TempRT;

    _TempRT.top = 0;
    _TempRT.bottom = IMAGE_IGS_BACK_HEIGHT;
    _TempRT.left = 0;
    _TempRT.right = IMAGE_IGS_BACK_WIDTH;

    if (_x >= _TempRT.left && _x < _TempRT.right && _y < _TempRT.bottom && _y >= _TempRT.top)
        return true;
    else
        return false;

    return false;
}

void CNewUIInGameShop::SetConvertInvenCoord(WORD _ItemType, float _Width, float _Height)
{
    ITEM_ATTRIBUTE *pItemAttr = &ItemAttribute[_ItemType];
    float _TempWidth = pItemAttr->Width * 20.0f;
    float _TempHeight = pItemAttr->Height * 20.0f;
    float _fCoodX = 0, _fCoodY = 0;

    if (_ItemType == ITEM_WING_OF_STORM)
    {
        _fCoodY = 5.0f;
    }

    else if (pItemAttr->Height >= 4)
    {
        _fCoodY = -10.0f;
    }

    m_fRePos.x = (_Width / 2) - (_TempWidth / 2) + _fCoodX;
    m_fRePos.y = (_Height / 2) - (_TempHeight / 2) + _fCoodY;
    m_fReSize.x = _TempWidth;
    m_fReSize.y = _TempHeight;
}
void CNewUIInGameShop::SetRateScale(int _ItemType)
{
    const float _fRate_Value = 0.703f;
    ITEM_ATTRIBUTE *pItemAttr = &ItemAttribute[_ItemType];

    if (_ItemType == ITEM_WING_OF_STORM)
    {
        m_fRate_Scale = _fRate_Value * 0.7f;
    }
    else if (_ItemType == ITEM_DIVINE_STAFF_OF_ARCHANGEL)
    {
        m_fRate_Scale = _fRate_Value * 0.7f;
    }
    else if (_ItemType >= ITEM_HELPER + 117 && _ItemType <= ITEM_HELPER + 120)
    {
        m_fRate_Scale = _fRate_Value * 1.6f;
    }
    else if (pItemAttr->Height >= 4)
    {
        m_fRate_Scale = _fRate_Value * 0.7f;
    }
    else
    {
        m_fRate_Scale = _fRate_Value;
    }
}

bool CNewUIInGameShop::BtnProcess()
{
    if (g_InGameShopSystem.IsRequestEventPackge() == true)
    {
        if (m_ZoneButton.UpdateMouseEvent() != -1)
        {
            g_InGameShopSystem.SelectZone(m_ZoneButton.GetCurButtonIndex());
            InitCategoryBtn();
            g_InGameShopSystem.SelectCategory(m_CategoryButton.GetCurButtonIndex());
            return true;
        }

        if (m_CategoryButton.UpdateMouseEvent() != -1)
        {
            g_InGameShopSystem.SelectCategory(m_CategoryButton.GetCurButtonIndex());
            return true;
        }
    }

    if (m_ListBoxTabButton.UpdateMouseEvent() != -1)
    {
        char szCode = GetCurrentStorageCode();
        m_iSelectedStorageItemIndex = 0;
        m_bRequestCurrentPage = true;
        SocketClient->ToGameServer()->SendCashShopStorageListRequest(1, szCode);
        return true;
    }

    for (int i = 0; i < g_InGameShopSystem.GetSizePackageAsDisplayPackage(); i++)
    {
        if (m_ViewDetailButton[i].UpdateMouseEvent())
        {
            CShopPackage *pPackage = g_InGameShopSystem.GetDisplayPackage(i);

            if (pPackage->PriceCount == 1)
            {
                CMsgBoxIGSBuyPackageItem *pMsgBox = NULL;
                CreateMessageBox(MSGBOX_LAYOUT_CLASS(CMsgBoxBuyPackageItemLayout, SessionOrigin()),
                                 &pMsgBox);
                pMsgBox->Initialize(pPackage);
            }
            else if (pPackage->PriceCount > 1)
            {
                CMsgBoxIGSBuySelectItem *pMsgBox = NULL;
                CreateMessageBox(
                    MSGBOX_LAYOUT_CLASS(CMsgBoxIGSBuySelectItemLayout, SessionOrigin()), &pMsgBox);
                pMsgBox->Initialize(pPackage);
            }

            return true;
        }
    }

    if (m_CashGiftButton.UpdateMouseEvent() == true)
    {
        CMsgBoxIGSCommon *pMsgBox = NULL;
        CreateMessageBox(MSGBOX_LAYOUT_CLASS(CMsgBoxIGSCommonLayout, SessionOrigin()), &pMsgBox);
        pMsgBox->Initialize(I18N::Game::RestrictedFunction,
                            I18N::Game::ThisFunctionIsNotSupportedIn);
        return true;
    }

    if (m_CashChargeButton.UpdateMouseEvent() == true)
    {
        CMsgBoxIGSCommon *pMsgBox = NULL;
        CreateMessageBox(MSGBOX_LAYOUT_CLASS(CMsgBoxIGSCommonLayout, SessionOrigin()), &pMsgBox);
        pMsgBox->Initialize(I18N::Game::RestrictedFunction,
                            I18N::Game::ThisFunctionIsNotSupportedIn);
        return true;
    }

    if (m_CashRefreshButton.UpdateMouseEvent() == true)
    {
        SocketClient->ToGameServer()->SendCashShopPointInfoRequest();

        return true;
    }

    if (m_UseButton.UpdateMouseEvent() == true)
    {
        if (m_StorageItemListBox.GetLineNum() <= 0)
        {
            CMsgBoxIGSCommon *pMsgBox = NULL;
            CreateMessageBox(MSGBOX_LAYOUT_CLASS(CMsgBoxIGSCommonLayout, SessionOrigin()),
                             &pMsgBox);
            pMsgBox->Initialize(I18N::Game::Error, I18N::Game::ThereIsNoUsableItem);
            return true;
        }

        int iStorageIndex = m_ListBoxTabButton.GetCurButtonIndex();

        IGS_StorageItem *pSelectItem = m_StorageItemListBox.GetSelectedText();

        if (iStorageIndex == IGS_SAFEKEEPING_LISTBOX) // ???
        {
            CMsgBoxIGSStorageItemInfo *pMsgBox = NULL;
            CreateMessageBox(MSGBOX_LAYOUT_CLASS(CMsgBoxIGSStorageItemInfoLayout, SessionOrigin()),
                             &pMsgBox);
            pMsgBox->Initialize(pSelectItem->m_iStorageSeq, pSelectItem->m_iStorageItemSeq,
                                pSelectItem->m_wItemCode, pSelectItem->m_szType,
                                pSelectItem->m_szName, pSelectItem->m_szNum,
                                pSelectItem->m_szPeriod);
        }
        else if (iStorageIndex == IGS_PRESENTBOX_LISTBOX) // ?? ???
        {
            CMsgBoxIGSGiftStorageItemInfo *pMsgBox = NULL;
            CreateMessageBox(
                MSGBOX_LAYOUT_CLASS(CMsgBoxIGSGiftStorageItemInfoLayout, SessionOrigin()),
                &pMsgBox);
            pMsgBox->Initialize(pSelectItem->m_iStorageSeq, pSelectItem->m_iStorageItemSeq,
                                pSelectItem->m_wItemCode, pSelectItem->m_szType,
                                pSelectItem->m_szSendUserName, pSelectItem->m_szMessage,
                                pSelectItem->m_szName, pSelectItem->m_szNum,
                                pSelectItem->m_szPeriod);
        }
        return true;
    }

    // Prev Button
    if (m_PrevButton.UpdateMouseEvent())
    {
        g_InGameShopSystem.PrePage();
        return true;
    }

    // Next Button
    if (m_NextButton.UpdateMouseEvent())
    {
        g_InGameShopSystem.NextPage();
        return true;
    }

    // Storage Prev Button
    if (m_StoragePrevButton.UpdateMouseEvent())
    {
        StoragePrevPage();
        return true;
    }

    // Next Button
    if (m_StorageNextButton.UpdateMouseEvent())
    {
        StorageNextPage();
        return true;
    }

    if (m_CloseButton.UpdateMouseEvent() == true)
    {
        if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_INGAMESHOP) == true)
        {
            SocketClient->ToGameServer()->SendCashShopOpenState(1);
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_INGAMESHOP);

            return true;
        }
        return false;
    }

    return false;
}

void CNewUIInGameShop::SetBtnInfo()
{
    m_CloseButton.ChangeButtonImgState(true, IMAGE_IGS_EXIT_BTN, false);
    m_CloseButton.ChangeButtonInfo(m_Pos.x + IMAGE_IGS_EXIT_BTN_POS_X,
                                   m_Pos.y + IMAGE_IGS_EXIT_BTN_POS_Y, IMAGE_IGS_EXIT_BTN_WIDTH,
                                   IMAGE_IGS_EXIT_BTN_HEIGHT);
    m_CloseButton.ChangeToolTipText(&I18N::Game::Close388, true);
    m_ListBoxTabButton.CreateRadioGroup(IGS_TOTAL_LISTBOX, IMAGE_IGS_LEFT_TAB);
    m_ListBoxTabButton.ChangeRadioButtonInfo(
        true, m_Pos.x + IMAGE_IGS_TAB_BTN_POS_X, m_Pos.y + IMAGE_IGS_TAB_BTN_POS_Y,
        IMAGE_IGS_TAB_BTN_WIDTH, IMAGE_IGS_TAB_BTN_HEIGHT, IMAGE_IGS_TAB_BTN_DISTANCE);
    m_ListBoxTabButton.ChangeButtonState(SEASON3B::BUTTON_STATE_DOWN, 0);
    m_ListBoxTabButton.ChangeButtonState(IGS_SAFEKEEPING_LISTBOX, BITMAP_UNKNOWN,
                                         SEASON3B::BUTTON_STATE_UP, 0);
    m_ListBoxTabButton.ChangeButtonState(IGS_PRESENTBOX_LISTBOX, BITMAP_UNKNOWN,
                                         SEASON3B::BUTTON_STATE_UP, 0);
    m_ListBoxTabButton.ChangeButtonState(IGS_PRESENTBOX_LISTBOX, IMAGE_IGS_RIGHT_TAB,
                                         SEASON3B::BUTTON_STATE_DOWN, 0);

    std::wstring strText;
    std::list<std::wstring> TextList;
    strText = I18N::Game::Storage;
    TextList.push_back(strText);
    strText = I18N::Game::GiftInventory;
    TextList.push_back(strText);

    m_ListBoxTabButton.ChangeRadioText(TextList);
    m_ListBoxTabButton.ChangeFrame(IGS_SAFEKEEPING_LISTBOX);

    for (int i = 0; i < INGAMESHOP_DISPLAY_ITEMLIST_SIZE; i++)
    {
        m_ViewDetailButton[i].ChangeButtonImgState(true, IMAGE_IGS_VIEWDETAIL_BTN, true, false,
                                                   true);
        m_ViewDetailButton[i].ChangeButtonInfo(
            IMAGE_IGS_VIEWDETAIL_BTN_POS_X +
                ((i % IGS_NUM_ITEMS_WIDTH) * IMAGE_IGS_VIEWDETAIL_BTN_DISTANCE_X),
            IMAGE_IGS_VIEWDETAIL_BTN_POS_Y +
                ((i / IGS_NUM_ITEMS_HEIGHT) * IMAGE_IGS_VIEWDETAIL_BTN_DISTANCE_Y),
            IMAGE_IGS_VIEWDETAIL_BTN_WIDTH, IMAGE_IGS_VIEWDETAIL_BTN_HEIGHT);
        m_ViewDetailButton[i].MoveTextPos(0, -1);
        m_ViewDetailButton[i].ChangeText(&I18N::Game::Buy1124);
    }

    m_CashGiftButton.ChangeButtonImgState(true, IMAGE_IGS_ITEMGIFT_BTN, true);
    m_CashGiftButton.ChangeButtonInfo(m_Pos.x + IMAGE_IGS_ITEMGIFT_BTN_POS_X,
                                      m_Pos.y + IMAGE_IGS_ICON_BTN_POS_Y, IMAGE_IGS_ICON_BTN_WIDTH,
                                      IMAGE_IGS_ICON_BTN_HEIGHT);
    m_CashGiftButton.ChangeToolTipText(&I18N::Game::SendWCoin);
    m_CashChargeButton.ChangeButtonImgState(true, IMAGE_IGS_CASHGIFT_BTN, true);
    m_CashChargeButton.ChangeButtonInfo(m_Pos.x + IMAGE_IGS_CASHGIFT_BTN_POS_X,
                                        m_Pos.y + IMAGE_IGS_ICON_BTN_POS_Y,
                                        IMAGE_IGS_ICON_BTN_WIDTH, IMAGE_IGS_ICON_BTN_HEIGHT);
    m_CashChargeButton.ChangeToolTipText(&I18N::Game::RechargeWCoin);

    m_CashRefreshButton.ChangeButtonImgState(true, IMAGE_IGS_REFRESH_BTN, true);
    m_CashRefreshButton.ChangeButtonInfo(m_Pos.x + IMAGE_IGS_REFRESH_BTN_POS_X,
                                         m_Pos.y + IMAGE_IGS_ICON_BTN_POS_Y,
                                         IMAGE_IGS_ICON_BTN_WIDTH, IMAGE_IGS_ICON_BTN_HEIGHT);
    m_CashRefreshButton.ChangeToolTipText(&I18N::Game::UpdateInformation);

    m_UseButton.ChangeButtonImgState(true, IMAGE_IGS_VIEWDETAIL_BTN, true, false, true);
    m_UseButton.ChangeButtonInfo(m_Pos.x + IMAGE_IGS_USE_BTN_POS_X,
                                 m_Pos.y + IMAGE_IGS_USE_BTN_POS_Y, IMAGE_IGS_VIEWDETAIL_BTN_WIDTH,
                                 IMAGE_IGS_VIEWDETAIL_BTN_HEIGHT);
    m_UseButton.MoveTextPos(0, -1);
    m_UseButton.ChangeText(&I18N::Game::Use);

    m_PrevButton.ChangeButtonImgState(true, IMAGE_IGS_PAGE_LEFT, true);
    m_PrevButton.ChangeButtonInfo(m_Pos.x + IMAGE_IGS_PAGE_LEFT_POS_X,
                                  m_Pos.y + IMAGE_IGS_PAGE_BUTTON_POS_Y, IMAGE_IGS_PAGE_BTN_WIDTH,
                                  IMAGE_IGS_PAGE_BTN_HEIGHT);

    // next
    m_NextButton.ChangeButtonImgState(true, IMAGE_IGS_PAGE_RIGHT, true);
    m_NextButton.ChangeButtonInfo(m_Pos.x + IMAGE_IGS_PAGE_RIGHT_POS_X,
                                  m_Pos.y + IMAGE_IGS_PAGE_BUTTON_POS_Y, IMAGE_IGS_PAGE_BTN_WIDTH,
                                  IMAGE_IGS_PAGE_BTN_HEIGHT);

    // Storage Page prev
    m_StoragePrevButton.ChangeButtonImgState(true, IMAGE_IGS_STORAGE_PAGE_LEFT, true);
    m_StoragePrevButton.ChangeButtonInfo(m_Pos.x + IMAGE_IGS_STORAGE_PAGE_LEFT_POS_X - 12,
                                         m_Pos.y + IMAGE_IGS_STORAGE_PAGE_BTN_POS_Y - 3,
                                         IMGAE_IGS_STORAGE_PAGE_BTN_WIDTH,
                                         IMGAE_IGS_STORAGE_PAGE_BTN_HEIGHT);

    // Storage Page next
    m_StorageNextButton.ChangeButtonImgState(true, IMAGE_IGS_STORAGE_PAGE_RIGHT, true);
    m_StorageNextButton.ChangeButtonInfo(m_Pos.x + IMAGE_IGS_STORAGE_PAGE_RIGHT_POS_X + 10,
                                         m_Pos.y + IMAGE_IGS_STORAGE_PAGE_BTN_POS_Y - 3,
                                         IMGAE_IGS_STORAGE_PAGE_BTN_WIDTH,
                                         IMGAE_IGS_STORAGE_PAGE_BTN_HEIGHT);
}

bool CNewUIInGameShop::Update()
{
    if (IsVisible() == false)
        return true;

    return true;
}

bool CNewUIInGameShop::UpdateMouseEvent()
{
    if (IsVisible() == false)
        return true;

    if (BtnProcess())
        return false;

    if (UpdateBanner())
        return false;

    if (CheckMouseIn(m_Pos.x, m_Pos.y, IMAGE_IGS_BACK_WIDTH, IMAGE_IGS_BACK_HEIGHT))
    {
        m_StorageItemListBox.DoAction();

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

    return true;
}

bool CNewUIInGameShop::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_INGAMESHOP) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            SocketClient->ToGameServer()->SendCashShopOpenState(1);
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_INGAMESHOP);

            return false;
        }
    }
    return true;
}

bool CNewUIInGameShop::IsInGameShopOpen()
{
    g_ConsoleDebug.Write(MCD_NORMAL,
                         L"InGameShopStatue.Txt CallStack - CNewUIInGameShop::IsInGameShopOpen()");
    if (Hero->Movement)
        return false;

    if (!(Hero->SafeZone) &&
        !(WD_0LORENCIA == gMapManager.ContextMap() && WD_3NORIA == gMapManager.ContextMap() &&
          WD_2DEVIAS == gMapManager.ContextMap() && WD_51HOME_6TH_CHAR == gMapManager.ContextMap()))
    {
        CMsgBoxIGSCommon *pMsgBox = NULL;
        CreateMessageBox(MSGBOX_LAYOUT_CLASS(CMsgBoxIGSCommonLayout, SessionOrigin()), &pMsgBox);
        pMsgBox->Initialize(I18N::Game::Error,
                            I18N::Game::YouCanOnlyOpenMUItemShopInATownOrSafeZone);
        g_ConsoleDebug.Write(MCD_NORMAL, L"InGameShopStatue.Txt Return - false <%ls>",
                             I18N::Game::YouCanOnlyOpenMUItemShopInATownOrSafeZone);
        return false;
    }

    if (g_InGameShopSystem.IsShopOpen() == false)
    {
        CMsgBoxIGSCommon *pMsgBox = NULL;
        CreateMessageBox(MSGBOX_LAYOUT_CLASS(CMsgBoxIGSCommonLayout, SessionOrigin()), &pMsgBox);
        pMsgBox->Initialize(I18N::Game::Error,
                            I18N::Game::CannotOpenMUItemShopPleaseReconnectToTheGame);
        g_ConsoleDebug.Write(MCD_NORMAL, L"InGameShopStatue.Txt Return - false <%ls>",
                             I18N::Game::CannotOpenMUItemShopPleaseReconnectToTheGame);
        return false;
    }
    g_ConsoleDebug.Write(MCD_NORMAL, L"InGameShopStatue.Txt Return - true");
    return true;
}

bool CNewUIInGameShop::IsInGameShop()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_INGAMESHOP))
        return true;
    else
        return false;
}

void CNewUIInGameShop::InitBanner(wchar_t *pszFileName, wchar_t *pszBannerURL)
{
    ReleaseBanner();

    if (pszFileName == NULL)
        return;

    if (pszBannerURL[0] != '#')
    {
        m_bBannerLink = true;
    }

    if (Bitmaps.Convert_Format(pszFileName) == false)
        return;

    if (LoadBitmapW(pszFileName, IMAGE_IGS_BANNER, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Clamp, true, true) == true)
    {
        m_bLoadBanner = true;

        wcscpy(m_szBannerURL, pszBannerURL);
    }
}

bool CNewUIInGameShop::UpdateBanner()
{
    if (m_bLoadBanner == false || m_bBannerLink == false)
        return false;

    if ((IsPress(VK_LBUTTON)) && (CheckMouseIn(IMAGE_IGS_BANNER_POS_X, IMAGE_IGS_BANNER_POS_Y,
                                               IMAGE_IGS_BANNER_WIDTH, IMAGE_IGS_BANNER_HEIGHT)))
    {
        leaf::OpenExplorer(m_szBannerURL);
        return true;
    }
    return false;
}

void CNewUIInGameShop::ReleaseBanner()
{
    if (m_bLoadBanner == false)
        return;

    DeleteBitmap(IMAGE_IGS_BANNER);

    m_bLoadBanner = false;
}

void CNewUIInGameShop::OpeningProcess()
{
    g_ConsoleDebug.Write(MCD_NORMAL,
                         L"InGameShopStatue.Txt CallStack - CNewUIInGameShop::OpeningProcess()");
    PlayBuffer(SOUND_CLICK01);
    g_InGameShopSystem.Initalize();
    g_InGameShopSystem.SelectZone(0);
    InitZoneBtn();
    g_InGameShopSystem.SelectCategory(0);
    InitCategoryBtn();
    g_InGameShopSystem.SetRequestEventPackge();
}

void CNewUIInGameShop::ClosingProcess()
{
    PlayBuffer(SOUND_CLICK01);
    m_ListBoxTabButton.ChangeFrame(IGS_SAFEKEEPING_LISTBOX);
    ClearAllStorageItem();
}

void CNewUIInGameShop::InitZoneBtn()
{
    m_ZoneButton.UnRegisterRadioButton();

    if (g_InGameShopSystem.GetSizeZones() == 0)
        return;

    m_ZoneButton.UnRegisterRadioButton();
    m_ZoneButton.CreateRadioGroup(g_InGameShopSystem.GetSizeZones(), IMAGE_IGS_ZONE_BTN);
    m_ZoneButton.ChangeRadioButtonInfo(true, m_Pos.x + IMAGE_IGS_ZONE_BTN_POS_X,
                                       m_Pos.y + IMAGE_IGS_ZONE_BTN_POS_Y, IMAGE_IGS_ZONE_BTN_WIDTH,
                                       IMAGE_IGS_ZONE_BTN_HEIGHT);
    m_ZoneButton.SetFont(LegacyFontRole::Bold);
    m_ZoneButton.ChangeRadioText(g_InGameShopSystem.GetZoneName());
    m_ZoneButton.ChangeFrame(0);
}

void CNewUIInGameShop::InitCategoryBtn()
{
    m_CategoryButton.UnRegisterRadioButton();

    if (g_InGameShopSystem.GetSizeCategoriesAsSelectedZone() == 0)
        return;

    m_CategoryButton.UnRegisterRadioButton();
    m_CategoryButton.CreateRadioGroup(g_InGameShopSystem.GetSizeCategoriesAsSelectedZone(),
                                      IMAGE_IGS_CATEGORY_BTN, true);
    m_CategoryButton.ChangeRadioButtonInfo(
        false, m_Pos.x + IMAGE_IGS_CATEGORY_BTN_POS_X, m_Pos.y + IMAGE_IGS_CATEGORY_BTN_POS_Y,
        IMAGE_IGS_CATEGORY_BTN_WIDTH, IMAGE_IGS_CATEGORY_BTN_HEIGHT,
        IMAGE_IGS_CATEGORY_BTN_DISTANCE);
    m_CategoryButton.ChangeButtonState(SEASON3B::BUTTON_STATE_DOWN, 2);
    m_CategoryButton.SetFont(LegacyFontRole::Bold);
    m_CategoryButton.ChangeRadioText(g_InGameShopSystem.GetCategoryName());
    m_CategoryButton.ChangeFrame(0);
}

void CNewUIInGameShop::AddStorageItem(int iStorageSeq, int iStorageItemSeq, int iStorageGroupCode,
                                      int iProductSeq, int iPriceSeq, int iCashPoint,
                                      wchar_t chItemType, wchar_t *pszUserName /* = NULL */,
                                      wchar_t *pszMessage /* = NULL */)
{
    int iValue = -1;
    wchar_t szText[MAX_TEXT_LENGTH] = {
        '\0',
    };
    IGS_StorageItem Item;

    Item.m_bIsSelected = FALSE;
    Item.m_iStorageSeq = iStorageSeq;
    Item.m_iStorageItemSeq = iStorageItemSeq;
    Item.m_iStorageGroupCode = iStorageGroupCode;
    Item.m_iProductSeq = iProductSeq;
    Item.m_iPriceSeq = iPriceSeq;
    Item.m_iCashPoint = iCashPoint;
    Item.m_iNum = 1;
    Item.m_szType = chItemType;
    Item.m_wItemCode = -1;

    if (pszUserName == NULL)
    {
        Item.m_szSendUserName[0] = '\0';
    }
    else
    {
        wcscpy(Item.m_szSendUserName, pszUserName);
    }

    if (pszMessage == NULL)
    {
        Item.m_szMessage[0] = '\0';
    }
    else
    {
        wcscpy(Item.m_szMessage, pszMessage);
    }

    if (chItemType == 'C' || chItemType == 'c')
    {
        wchar_t szValue[MAX_TEXT_LENGTH] = {
            '\0',
        };
        ConvertGold(iCashPoint, szValue);
        // Name
        mu_swprintf(Item.m_szName, I18N::Game::WCoinSCoins, szValue);

        // Num
        mu_swprintf(Item.m_szNum, I18N::Game::SWCoin, szValue);
        Item.m_iNum = iCashPoint;

        // Period
        mu_swprintf(Item.m_szPeriod, L"-");
    }
    else if (chItemType == 'P' || chItemType == 'p')
    {
        if (iPriceSeq > 0)
        {
            // Name
            if (g_InGameShopSystem.GetProductInfoFromPriceSeq(
                    iProductSeq, iPriceSeq, CInGameShopSystem::IGS_PRODUCT_ATT_TYPE_ITEMNAME,
                    iValue, Item.m_szName) == false)
            {
                mu_swprintf(Item.m_szName, L"aaa");
            }

            g_InGameShopSystem.GetProductInfoFromPriceSeq(
                iProductSeq, iPriceSeq, CInGameShopSystem::IGS_PRODUCT_ATT_TYPE_NUM, iValue,
                szText);
            if (iValue > 0)
            {
                mu_swprintf(Item.m_szNum, L"%d %ls", iValue, szText);
                Item.m_iNum = iValue;
            }
            else
            {
                mu_swprintf(Item.m_szNum, L"-");
            }

            // Period
            g_InGameShopSystem.GetProductInfoFromPriceSeq(
                iProductSeq, iPriceSeq, CInGameShopSystem::IGS_PRODUCT_ATT_TYPE_USE_LIMIT_PERIOD,
                iValue, szText);
            if (iValue > 0)
            {
                mu_swprintf(Item.m_szPeriod, L"%d %ls", iValue, szText);
            }
            else
            {
                mu_swprintf(Item.m_szPeriod, L"-");
            }

            g_InGameShopSystem.GetProductInfoFromPriceSeq(
                iProductSeq, iPriceSeq, CInGameShopSystem::IGS_PRODUCT_ATT_TYPE_ITEMCODE, iValue,
                szText);
            Item.m_wItemCode = iValue;
        }
        else
        {
            if (g_InGameShopSystem.GetProductInfoFromProductSeq(
                    iProductSeq, CInGameShopSystem::IGS_PRODUCT_ATT_TYPE_ITEMNAME, iValue,
                    Item.m_szName) == false)
                return;

            // Num
            g_InGameShopSystem.GetProductInfoFromProductSeq(
                iProductSeq, CInGameShopSystem::IGS_PRODUCT_ATT_TYPE_NUM, iValue, szText);
            if (iValue > 0)
            {
                mu_swprintf(Item.m_szNum, L"%d %ls", iValue, szText);
                Item.m_iNum = iValue;
            }
            else
            {
                mu_swprintf(Item.m_szNum, L"-");
            }

            // Period
            g_InGameShopSystem.GetProductInfoFromProductSeq(
                iProductSeq, CInGameShopSystem::IGS_PRODUCT_ATT_TYPE_USE_LIMIT_PERIOD, iValue,
                szText);
            if (iValue > 0)
            {
                mu_swprintf(Item.m_szPeriod, L"%d %ls", iValue, szText);
            }
            else
            {
                mu_swprintf(Item.m_szPeriod, L"-");
            }

            g_InGameShopSystem.GetProductInfoFromProductSeq(
                iProductSeq, CInGameShopSystem::IGS_PRODUCT_ATT_TYPE_ITEMCODE, iValue, szText);
            Item.m_wItemCode = iValue;
        }
    }
    else
    {
        return;
    }

    m_iStorageCurrentPageReceiveItemCnt++;

    m_StorageItemListBox.AddText(Item);

    if (m_iStorageCurrentPageReceiveItemCnt >= m_iStorageCurrentPageItemCnt)
    {
        if (m_iSelectedStorageItemIndex > m_iStorageCurrentPageItemCnt)
        {
            m_StorageItemListBox.SLSetSelectLine(m_iStorageCurrentPageItemCnt);
        }
        else
        {
            m_StorageItemListBox.SLSetSelectLine(m_iSelectedStorageItemIndex);
        }
    }
}

void CNewUIInGameShop::ClearAllStorageItem()
{
    m_iStorageTotalItemCnt = 0;
    m_iStorageCurrentPageItemCnt = 0;
    m_iStorageTotalPage = 0;
    m_iStorageCurrentPage = 0;
    m_iStorageCurrentPageReceiveItemCnt = 0;
    m_StorageItemListBox.Clear();
}

void CNewUIInGameShop::InitStorage(int iTotalItemCnt, int iCurrentPageItemCnt, int iTotalPage,
                                   int iCurrentPage)
{
    ClearAllStorageItem();

    m_iStorageTotalItemCnt = iTotalItemCnt;
    m_iStorageCurrentPageItemCnt = iCurrentPageItemCnt;
    m_iStorageTotalPage = iTotalPage;

    if (m_iStorageTotalPage > 0)
    {
        m_iStorageCurrentPage = iCurrentPage;
    }
    else
    {
        m_iStorageCurrentPage = 0;
    }

    if (m_iSelectedStorageItemIndex == 0 || m_bRequestCurrentPage == false)
    {
        m_iSelectedStorageItemIndex = iCurrentPageItemCnt;
    }

    m_bRequestCurrentPage = false;
}

char CNewUIInGameShop::GetCurrentStorageCode()
{
    char szCode;
    switch (m_ListBoxTabButton.GetCurButtonIndex())
    {
    case IGS_SAFEKEEPING_LISTBOX:
        szCode = 'S';
        break;
    case IGS_PRESENTBOX_LISTBOX:
        szCode = 'G';
        break;
    default:
        szCode = 'Z';
        break;
    }
    return szCode;
}

void CNewUIInGameShop::StoragePrevPage()
{
    if (m_iStorageCurrentPage > 1)
    {
        char szCode = GetCurrentStorageCode();
        m_iSelectedStorageItemIndex = 0;
        m_bRequestCurrentPage = true;
        SocketClient->ToGameServer()->SendCashShopStorageListRequest(m_iStorageCurrentPage - 1,
                                                                     szCode);
    }
}

void CNewUIInGameShop::StorageNextPage()
{
    if (m_iStorageCurrentPage < m_iStorageTotalPage)
    {
        char szCode = GetCurrentStorageCode();
        m_iSelectedStorageItemIndex = 0;
        m_bRequestCurrentPage = true;
        SocketClient->ToGameServer()->SendCashShopStorageListRequest(m_iStorageCurrentPage + 1,
                                                                     szCode);
    }
}

void CNewUIInGameShop::UpdateStorageItemList()
{
    char szCode = GetCurrentStorageCode();
    int iSelectLineIndex = m_StorageItemListBox.SLGetSelectLineNum();
    m_bRequestCurrentPage = true;

    if ((m_iStorageCurrentPageItemCnt == 1) && (m_iStorageTotalPage > 1))
    {
        m_iSelectedStorageItemIndex = 1;
        SocketClient->ToGameServer()->SendCashShopStorageListRequest(m_iStorageCurrentPage - 1,
                                                                     szCode);
    }
    else if (iSelectLineIndex == 1)
    {
        m_iSelectedStorageItemIndex = iSelectLineIndex;
        SocketClient->ToGameServer()->SendCashShopStorageListRequest(m_iStorageCurrentPage, szCode);
    }
    else if (m_iStorageCurrentPageItemCnt < IGS_STORAGE_TOTAL_ITEM_PER_PAGE)
    {
        m_iSelectedStorageItemIndex = (iSelectLineIndex - 1);
        SocketClient->ToGameServer()->SendCashShopStorageListRequest(m_iStorageCurrentPage, szCode);
    }
    else
    {
        m_iSelectedStorageItemIndex = iSelectLineIndex;
        SocketClient->ToGameServer()->SendCashShopStorageListRequest(m_iStorageCurrentPage, szCode);
    }
}

#endif //PBG_ADD_INGAMESHOP_UI_ITEMSHOP

using namespace SEASON3B;

CNewUIInventoryExtension::CNewUIInventoryExtension(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderUnit(RendererForConstruction()), m_ModernPanel(keeper)
{
    Init();
}

CNewUIInventoryExtension::~CNewUIInventoryExtension()
{
    Release();
}

void CNewUIInventoryExtension::Init()
{
    m_pNewUIMng = nullptr;
    std::fill(std::begin(m_extensions), std::end(m_extensions), nullptr);
}

bool CNewUIInventoryExtension::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (nullptr == pNewUIMng || nullptr == g_pNewItemMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(INTERFACE_INVENTORY_EXT, this);

    // we have to create all 4 boxes here already, and just handle the ones
    // which are available to the character...
    int i = 0;
    for (auto &m_extension : m_extensions)
    {
        m_extension = renderUnit.CreateInventoryControl();

        const int indexOffset = MAX_MY_INVENTORY_INDEX + i * MAX_INVENTORY_EXT_ONE;
        if (false ==
            m_extension->Create(GameDataForConstruction().Inventory(static_cast<InventoryRole>(
                                    static_cast<int>(InventoryRole::PlayerExtension0) + i)),
                                g_pNewUI3DRenderMng, this, 0, 0))
        {
            SAFE_DELETE(m_extension);
            return false;
        }

        if (m_extension)
        {
            m_extension->SetToolTipType(TOOLTIP_TYPE_INVENTORY);
            m_extension->SetRenderSlotFrame(false);
            m_extension->SetOwnerRendered(true);
        }

        i++;
    }

    SetPos(x, y);
    Show(false);

    return true;
}

void CNewUIInventoryExtension::Release()
{
    m_ModernPanel.Release();
    m_ModernVisible = false;
    m_ModernBagCount = 0;
    m_ModernTitle.clear();

    for (auto &extension : m_extensions)
    {
        if (extension)
        {
            SAFE_DELETE(extension);
        }
    }

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }
}

bool CNewUIInventoryExtension::UpdateMouseEvent()
{
    SyncModernGeometry();
    if (ProcessModernChanges())
        return false;

    for (int i = 0; i < CharacterAttribute->InventoryExtensions; i++)
    {
        if (const auto m_extension = m_extensions[i])
        {
            if (!m_extension->UpdateMouseEvent())
            {
                return false;
            }

            if (InventoryProcess())
            {
                return false;
            }
        }
    }

    if (IsMouseInModernPanel())
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

bool CNewUIInventoryExtension::InventoryProcess()
{
    if (!IsMouseInModernPanel())
    {
        return false;
    }

    for (std::size_t i = 0; i < m_ModernBagCount; ++i)
    {
        auto *extension = m_extensions[i];
        if (extension->CheckPtInRect(MouseX, MouseY))
        {
            return g_pMyInventory->HandleInventoryActions(extension);
        }
    }

    return false;
}

bool CNewUIInventoryExtension::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(INTERFACE_INVENTORY_EXT) == false)
    {
        return true;
    }

    return true;
}

bool CNewUIInventoryExtension::Update()
{
    ProcessModernChanges();
    m_ModernVisible = IsVisible();
    m_ModernBagCount = CharacterAttribute->InventoryExtensions;
    m_ModernTitle = I18N::Game::ExpandedInventory;
    if (m_ModernVisible)
        SyncModernGeometry();
    for (int i = 0; i < CharacterAttribute->InventoryExtensions; i++)
    {
        if (const auto &extension = m_extensions[i])
        {
            if (extension && !extension->Update())
            {
                return false;
            }
            m_ModernPanel.SetSlotFrames(extension->SlotIconFrames(),
                                        i * (MAX_INVENTORY_EXT / MAX_INVENTORY_EXT_COUNT));
        }
    }

    return true;
}

float CNewUIInventoryExtension::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

void CNewUIInventoryExtension::SyncModernGeometry()
{
    for (std::size_t i = 0; i < m_ModernBagCount; ++i)
    {
        const auto cell = m_ModernPanel.GridCell(i);
        if (cell.width > 0 && cell.height > 0)
            m_extensions[i]->SetOwnerGeometry(cell.x, cell.y, cell.width, cell.height);
    }
}

bool CNewUIInventoryExtension::IsMouseInModernPanel() const
{
    const auto rect = m_ModernPanel.ReferenceRect();
    return CheckMouseIn(static_cast<int>(std::floor(rect.x)), static_cast<int>(std::floor(rect.y)),
                        static_cast<int>(std::ceil(rect.width)),
                        static_cast<int>(std::ceil(rect.height)));
}

bool CNewUIInventoryExtension::ProcessModernChanges()
{
    const auto changes = m_ModernPanel.TakeChanges();
    if (changes.focus && m_pNewUIMng)
        m_pNewUIMng->BringToFront(this);
    if (changes.dismiss)
        g_pNewUISystem->Hide(INTERFACE_INVENTORY_EXT);
    return changes.dismiss;
}

std::optional<bool> CNewUIInventoryExtension::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernPanel.ProcessInput(event);
}

CNewUIInventoryCtrl *CNewUIInventoryExtension::TryGetExtensionByInventoryIndex(int iIndex) const
{
    const auto index = iIndex - MAX_MY_INVENTORY_INDEX;
    const auto extensionIndex = index / MAX_INVENTORY_EXT_ONE;
    if (extensionIndex >= 0 && extensionIndex < MAX_INVENTORY_EXT_COUNT)
    {
        return m_extensions[extensionIndex];
    }

    return nullptr;
}

ITEM *CNewUIInventoryExtension::FindItem(int iIndex) const
{
    if (const auto &extension = TryGetExtensionByInventoryIndex(iIndex))
    {
        return extension->FindItem(iIndex);
    }
    return nullptr;
}

bool CNewUIInventoryExtension::InsertItem(int iIndex, std::span<const BYTE> pbyItemPacket) const
{
    return sessionKeeper_.GameData()->InsertInventoryItem(iIndex, pbyItemPacket);
}

void CNewUIInventoryExtension::DeleteItem(int iIndex) const
{
    sessionKeeper_.GameData()->DeleteInventoryItem(iIndex);
}

void CNewUIInventoryExtension::DeleteAllItems() const
{
    for (auto *extension : m_extensions)
    {
        if (extension)
        {
            extension->RemoveAllItems();
        }
    }
}

int CNewUIInventoryExtension::FindEmptySlot(int cx, int cy,
                                            const CNewUIInventoryCtrl *excluded) const
{
    if (CharacterAttribute == nullptr)
    {
        return -1;
    }

    for (int i = 0; i < CharacterAttribute->InventoryExtensions; ++i)
    {
        auto *extension = m_extensions[i];
        if (extension && extension != excluded)
        {
            const int emptySlot = extension->FindEmptySlot(cx, cy);
            if (emptySlot != -1)
            {
                return emptySlot;
            }
        }
    }

    return -1;
}

CNewUIInventoryCtrl *CNewUIInventoryExtension::GetOwnerOf(const CNewUIPickedItem *pPickedItem) const
{
    if (!pPickedItem)
    {
        return nullptr;
    }

    const auto *ownerOfItem = pPickedItem->GetOwnerInventory();

    for (auto *extension : m_extensions)
    {
        if (extension == ownerOfItem)
        {
            return extension;
        }
    }

    return nullptr;
}

CNewUIItemExplanationWindow::CNewUIItemExplanationWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()),
      panel_(keeper, "item-explanation")
{
}
CNewUIItemExplanationWindow::~CNewUIItemExplanationWindow()
{
    Release();
}
bool CNewUIItemExplanationWindow::Create(CNewUIManager *manager, int, int)
{
    if (!manager)
        return false;
    manager_ = manager;
    manager->AddUIObj(INTERFACE_ITEM_EXPLANATION, this);
    Show(false);
    return true;
}
void CNewUIItemExplanationWindow::Release()
{
    panel_.Release();
    content_ = {};
    key_.reset();
    locale_.clear();
    visible_ = false;
    if (!manager_)
        return;
    manager_->RemoveUIObj(this);
    manager_ = nullptr;
}

bool CNewUIItemExplanationWindow::UpdateMouseEvent()
{
    return !panel_.ContainsReferencePointer(MouseX, MouseY);
}
bool CNewUIItemExplanationWindow::UpdateKeyEvent()
{
    if (!IsVisible() || (!IsPress(VK_ESCAPE) && !IsPress(VK_F1)))
        return true;
    g_pNewUISystem->Hide(INTERFACE_ITEM_EXPLANATION);
    PlayBuffer(SOUND_CLICK01);
    return false;
}
void CNewUIItemExplanationWindow::BuildTable(bool singleLevel)
{
    struct Column
    {
        int id;
        const wchar_t *title;
        bool percent = false;
    };
    const std::array candidates{Column{_COLUMN_TYPE_LEVEL, I18N::Game::LV},
                                Column{_COLUMN_TYPE_REQNLV, I18N::Game::ReqLV},
                                Column{_COLUMN_TYPE_ATTMIN, I18N::Game::ATKDmg},
                                Column{_COLUMN_TYPE_MAGIC, I18N::Game::WIZDmg, true},
                                Column{_COLUMN_TYPE_CURSE, I18N::Game::Curse},
                                Column{_COLUMN_TYPE_PET_ATTACK, I18N::Game::Attack, true},
                                Column{_COLUMN_TYPE_DEFENCE, I18N::Game::DEF},
                                Column{_COLUMN_TYPE_DEFRATE, I18N::Game::DEFRate},
                                Column{_COLUMN_TYPE_REQSTR, I18N::Game::STR},
                                Column{_COLUMN_TYPE_REQDEX, I18N::Game::AGI},
                                Column{_COLUMN_TYPE_REQVIT, I18N::Game::STA},
                                Column{_COLUMN_TYPE_REQENG, I18N::Game::ENG},
                                Column{_COLUMN_TYPE_REQCHA, I18N::Game::Command}};
    std::vector<Column> columns;
    for (auto column : candidates)
    {
        const bool shown = column.id == _COLUMN_TYPE_LEVEL ? !singleLevel
                           : column.id == _COLUMN_TYPE_ATTMIN
                               ? !singleLevel && (g_iItemInfo[0][_COLUMN_TYPE_ATTMIN] > 0 ||
                                                  g_iItemInfo[0][_COLUMN_TYPE_ATTMAX] > 0)
                               : g_iItemInfo[0][column.id] > 0 ||
                                     (column.id == _COLUMN_TYPE_REQDEX && ItemHelp < ITEM_ETC);
        if (shown)
        {
            columns.push_back(column);
            content_.columns.push_back(column.title);
        }
    }
    for (int level = 0; level <= (singleLevel ? 0 : ItemRulesDetail::iMaxLevel); ++level)
    {
        auto &row = content_.rows.emplace_back();
        row.available = g_iItemInfo[level][_COLUMN_TYPE_CAN_EQUIP] != 0;
        for (auto column : columns)
        {
            auto value = std::to_wstring(g_iItemInfo[level][column.id]);
            if (column.id == _COLUMN_TYPE_LEVEL)
                value = L"+" + value;
            else if (column.id == _COLUMN_TYPE_ATTMIN)
                value += L" ~ " + std::to_wstring(g_iItemInfo[level][_COLUMN_TYPE_ATTMAX]);
            else if (column.percent)
                value += L"%";
            row.cells.push_back(std::move(value));
        }
    }
}

bool CNewUIItemExplanationWindow::StageContent()
{
    const bool supported = ItemHelp >= ITEM_SWORD && ItemHelp < ITEM_ETC + MAX_ITEM_INDEX &&
                           ItemHelp != ITEM_ARROWS && ItemHelp != ITEM_BOLT;
    if (!supported)
        return false;
    const std::array next{ItemHelp,
                          int(CharacterAttribute->Level),
                          int(Hero->Class),
                          int(CharacterAttribute->Strength + CharacterAttribute->AddStrength),
                          int(CharacterAttribute->Dexterity + CharacterAttribute->AddDexterity),
                          int(CharacterAttribute->Vitality + CharacterAttribute->AddVitality),
                          int(CharacterAttribute->Energy + CharacterAttribute->AddEnergy),
                          int(CharacterAttribute->Charisma + CharacterAttribute->AddCharisma)};
    const std::string locale = I18N::GetCurrentLocale();
    if (key_ == next && locale_ == locale)
        return true;
    key_ = next;
    locale_ = locale;
    const auto revision = content_.revision + 1;
    content_ = {};
    content_.revision = revision;
    g_iCurrentItem = -1;
    memset(g_iItemInfo, 0, sizeof(g_iItemInfo));
    ComputeItemInfo(ItemHelp);
    content_.heading = {{I18N::Game::ItemInfo, "blue bold"},
                        {ItemAttribute[ItemHelp].Name, "white bold"}};
    BuildTable(ItemHelp >= ITEM_ETC);
    TextNum = SkipNum = 0;
    RequireClass(&ItemAttribute[ItemHelp]);
    for (int i = 0; i < TextNum; ++i)
    {
        const std::wstring text = TextList[i];
        const char *style = TextListColor[i] == TEXT_COLOR_DARKRED ? "dark-red"
                            : TextListColor[i] == TEXT_COLOR_RED   ? "red"
                                                                   : "white";
        content_.notes.push_back(
            {text.find_first_not_of(L" \r\n\t") == std::wstring::npos ? L"" : text, style});
    }
    TextNum = SkipNum = 0;
    return true;
}

bool CNewUIItemExplanationWindow::Update()
{
    if (IsVisible() && !StageContent())
        g_pNewUISystem->Hide(INTERFACE_ITEM_EXPLANATION);
    visible_ = IsVisible();
    return true;
}

bool CNewUIItemExplanationWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessInput(event);
}
float CNewUIItemExplanationWindow::GetLayerDepth()
{
    return 6.5f;
}
float CNewUIItemExplanationWindow::GetKeyEventOrder()
{
    return 10.0f;
}
void CNewUIItemExplanationWindow::OpenningProcess()
{
    key_.reset();
}
void CNewUIItemExplanationWindow::ClosingProcess()
{
    visible_ = false;
}

CNewUILuckyItemWnd::CNewUILuckyItemWnd(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderUnit(RendererForConstruction()),
      m_ModernPanel(keeper, "lucky_item.rml")
{
    std::fill(std::begin(m_szSubject), std::end(m_szSubject), L'\0');
    m_pNewUIMng = nullptr;
    m_pNewInventoryCtrl = nullptr;
    m_eWndAction = m_eEnd = eLuckyItem_None;
    m_nTextMaxLine = 0;
    m_eType = eLuckyItemType_None;
    m_mixEffectTicks = 0;
}

CNewUILuckyItemWnd::~CNewUILuckyItemWnd()
{
    Release();
}

int CNewUILuckyItemWnd::GetLuckyItemRate(int _nType)
{
    if (_nType == eLuckyItemType_Trade)
        return 100;
    if (_nType == eLuckyItemType_Refinery)
        return 50;

    return 0;
}

STORAGE_TYPE CNewUILuckyItemWnd::SetMoveAction()
{
    m_eWndAction = eLuckyItem_Move;
    switch (m_eType)
    {
    case eLuckyItemType_Trade:
        return STORAGE_TYPE::LUCKYITEM_TRADE;
    case eLuckyItemType_Refinery:
        return STORAGE_TYPE::LUCKYITEM_REFINERY;
    }

    return STORAGE_TYPE::UNDEFINED;
}

int CNewUILuckyItemWnd::SetActAction()
{
    m_eWndAction = eLuckyItem_Act;
    switch (m_eType)
    {
    case eLuckyItemType_Trade:
        sessionKeeper_.InventoryStorage().luckyMixRequest = 51;
        return 51;
    case eLuckyItemType_Refinery:
        sessionKeeper_.InventoryStorage().luckyMixRequest = 52;
        return 52;
    default:
        return -1;
    }
}

void CNewUILuckyItemWnd::GetResult(BYTE _byResult)
{
    if (m_eWndAction == eLuckyItem_Act)
    {
        if (_byResult == 1)
        {
            PlayBuffer(SOUND_JEWEL01);
            m_mixEffectTicks = 50;
        }
        else if (m_eType == eLuckyItemType_Trade)
        {
            g_pChatListBox->AddText(L"", I18N::Game::Lookup(3303), TYPE_ERROR_MESSAGE);
        }
        m_eEnd = eLuckyItem_End;
        SetFrame_Text(m_eEnd);
    }
    m_eWndAction = eLuckyItem_None;
}

bool CNewUILuckyItemWnd::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng || NULL == g_pNewUI3DRenderMng || NULL == g_pNewItemMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_LUCKYITEMWND, this);

    m_pNewInventoryCtrl = renderUnit.CreateInventoryControl();
    if (false == m_pNewInventoryCtrl->Create(
                     GameDataForConstruction().Inventory(InventoryRole::LuckyCrafting),
                     g_pNewUI3DRenderMng, this, 0, 0))
    {
        SAFE_DELETE(m_pNewInventoryCtrl);
        return false;
    }
    m_pNewInventoryCtrl->GetSquareColorNormal(m_fInvenClr);
    m_pNewInventoryCtrl->GetSquareColorWarning(m_fInvenClrWarning);

    m_pNewInventoryCtrl->SetOwnerRendered(true);
    m_pNewInventoryCtrl->SetRenderSlotFrame(false);
    SetPos(x, y);

    for (int i = 0; i < LUCKYITEMMAXLINE; i++)
    {
        m_sText[i].s_nTextIndex = -1;
        m_sText[i].s_dwColor = 0;
        m_sText[i].s_nLine = false;
    }

    Show(false);

    return true;
}

void CNewUILuckyItemWnd::Release()
{
    m_ModernPanel.Release();
    SAFE_DELETE(m_pNewInventoryCtrl);
    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

void CNewUILuckyItemWnd::OpeningProcess(void)
{
    for (int i = 0; i < LUCKYITEMMAXLINE; i++)
    {
        m_sText[i].s_nTextIndex = -1;
        m_sText[i].s_dwColor = 0;
        m_sText[i].s_nLine = false;
    }
    m_nTextMaxLine = 0;
    m_eEnd = eLuckyItem_None;
    switch (m_eType)
    {
    case eLuckyItemType_Trade:
        mu_swprintf(m_szSubject, L"%ls", I18N::Game::ExchangeLuckyItem);
        AddText(3291, 0xFF0000FF, RT3_SORT_LEFT), AddText(0), AddText(0), AddText(3292),
            AddText(3293), AddText(3294);
        AddText(0), AddText(0);
        AddText(2223, 0xFF00FFFF);
        AddText(0);
        AddText(3295, 0xFF0000FF), AddText(3296, 0xFF0000FF);
        break;
    case eLuckyItemType_Refinery:
        mu_swprintf(m_szSubject, L"%ls", I18N::Game::RefineLuckyItem);
        AddText(2346, 0xFF0000FF, RT3_SORT_LEFT), AddText(0), AddText(0);
        AddText(3300), AddText(3301);
        AddText(0), AddText(0), AddText(0);
        AddText(3302, 0xFF0000FF);
        break;
    }
}

void CNewUILuckyItemWnd::SetFrame_Text(eLUCKYITEM _eType)
{
    switch (_eType)
    {
    case eLuckyItem_End:
        for (int i = 0; i < LUCKYITEMMAXLINE; i++)
        {
            m_sText[i].s_nTextIndex = -1;
            m_sText[i].s_dwColor = 0;
            m_sText[i].s_nLine = false;
        }
        m_nTextMaxLine = 0;
        AddText(0);
        if (m_eType == eLuckyItemType_Trade)
            AddText(1888);
        else if (m_eType == eLuckyItemType_Refinery)
        {
        }
        break;
    }
}

void CNewUILuckyItemWnd::AddText(int _nGlobalTextIndex, DWORD _dwColor, int _nLine)
{
    if (m_nTextMaxLine >= LUCKYITEMMAXLINE)
        return;
    m_sText[m_nTextMaxLine].s_nTextIndex = _nGlobalTextIndex;
    m_sText[m_nTextMaxLine].s_dwColor = _dwColor;
    m_sText[m_nTextMaxLine].s_nLine = _nLine;
    m_nTextMaxLine++;
}

bool CNewUILuckyItemWnd::ClosingProcess(void)
{
    if (GetInventoryCtrl()->GetNumberOfItems() > 0 ||
        g_pMyInventory->GetInventoryCtrl()->GetPickedItem() != NULL)
    {
        g_pChatListBox->AddText(L"", I18N::Game::CloseInventoryAfterMovingYourItemsInTheInventory,
                                SEASON3B::TYPE_ERROR_MESSAGE);
        return false;
    }

    SocketClient->ToGameServer()->SendCraftingDialogCloseRequest();
    m_eType = eLuckyItemType_None;
    return true;
}

bool CNewUILuckyItemWnd::Process_InventoryCtrl_InsertItem(int iIndex,
                                                          std::span<const BYTE> pbyItemPacket)
{
    if (m_pNewInventoryCtrl)
        return m_pNewInventoryCtrl->AddItem(iIndex, pbyItemPacket);
    return false;
}

void CNewUILuckyItemWnd::Process_InventoryCtrl_DeleteItem(int iIndex)
{
    if (m_pNewInventoryCtrl)
    {
        if (iIndex == -1)
        {
            m_pNewInventoryCtrl->RemoveAllItems();
            return;
        }
        ITEM *pItem = m_pNewInventoryCtrl->FindItem(iIndex);
        if (pItem != NULL)
            m_pNewInventoryCtrl->RemoveItem(pItem);
    }
}

bool CNewUILuckyItemWnd::Check_LuckyItem_InWnd(void)
{
    if (GetInventoryCtrl()->GetNumberOfItems() > 0)
        return true;
    return false;
}

bool CNewUILuckyItemWnd::Check_LuckyItem(ITEM *_pItem)
{
    switch (m_eType)
    {
    case eLuckyItemType_Trade:
        if (Check_LuckyItem_Trade(_pItem))
            return true;
        break;
    case eLuckyItemType_Refinery:
        if (Check_LuckyItem_Refinery(_pItem))
            return true;
        break;
    }

    return false;
}

bool CNewUILuckyItemWnd::Check_LuckyItem_Trade(ITEM *_pItem)
{
    if (_pItem->Type >= ITEM_HELPER + 135 && _pItem->Type <= ITEM_HELPER + 145)
        return true;

    return false;
}

bool CNewUILuckyItemWnd::Check_LuckyItem_Refinery(ITEM *_pItem)
{
    if (_pItem->Type >= ITEM_ARMOR + 62 && _pItem->Type <= ITEM_ARMOR + 72)
        return true;
    else if (_pItem->Type >= ITEM_HELM + 62 && _pItem->Type <= ITEM_HELM + 72)
        return true;
    else if (_pItem->Type >= ITEM_BOOTS + 62 && _pItem->Type <= ITEM_BOOTS + 72)
        return true;
    else if (_pItem->Type >= ITEM_GLOVES + 62 && _pItem->Type <= ITEM_GLOVES + 72)
        return true;
    else if (_pItem->Type >= ITEM_PANTS + 62 && _pItem->Type <= ITEM_PANTS + 72)
        return true;

    return false;
}

bool CNewUILuckyItemWnd::Process_InventoryCtrl(void)
{
    CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();
    if (!m_pNewInventoryCtrl)
        return false;
    if (!pPickedItem)
        return false;

    ITEM *pItemObj = pPickedItem->GetItem();
    bool bAct = Check_LuckyItem(pItemObj);

    if (!bAct || Check_LuckyItem_InWnd())
    {
        m_pNewInventoryCtrl->SetSquareColorNormal(m_fInvenClrWarning[0], m_fInvenClrWarning[1],
                                                  m_fInvenClrWarning[2]);
        return false;
    }

    if (pPickedItem->GetSourceStorageType() == STORAGE_TYPE::INVENTORY)
    {
        if (IsPress(VK_LBUTTON))
        {
            int iSourceIndex = pPickedItem->GetSourceLinealPos();
            int iTargetIndex = pPickedItem->GetTargetLinealPos(m_pNewInventoryCtrl);
            if (iTargetIndex != -1 && m_pNewInventoryCtrl->CanMove(iTargetIndex, pItemObj))
            {
                auto nMoveIndex = SetMoveAction();
                if (SendRequestEquipmentItem(STORAGE_TYPE::INVENTORY, iSourceIndex, pItemObj,
                                             nMoveIndex, iTargetIndex))
                    return true;
            }
        }
    }
    else if (pPickedItem->GetOwnerInventory() == m_pNewInventoryCtrl)
    {
        if (IsPress(VK_LBUTTON))
        {
            int iSourceIndex = pPickedItem->GetSourceLinealPos();
            int iTargetIndex = pPickedItem->GetTargetLinealPos(m_pNewInventoryCtrl);
            if (iTargetIndex != -1 && m_pNewInventoryCtrl->CanMove(iTargetIndex, pItemObj))
            {
                auto nMoveIndex = SetMoveAction();
                if (SendRequestEquipmentItem(nMoveIndex, iSourceIndex, pItemObj, nMoveIndex,
                                             iTargetIndex))
                {
                    return true;
                }
            }
        }
    }

    // InventoryCtrl Background Color
    m_pNewInventoryCtrl->SetSquareColorNormal(m_fInvenClr[0], m_fInvenClr[1], m_fInvenClr[2]);
    return false;
}

CNewUIInventoryCtrl *CNewUILuckyItemWnd::GetInventoryCtrl() const
{
    return m_pNewInventoryCtrl;
}

bool CNewUILuckyItemWnd::Process_BTN_Action(void)
{
    if (!m_ModernPanel.TakeClick("btnMix"))
        return false;

    if (m_eEnd == eLuckyItem_End)
        return false;

    if (g_pMyInventory->GetInventoryCtrl()->GetPickedItem())
        return false;

    if (!Check_LuckyItem_InWnd())
    {
        g_pChatListBox->AddText(L"", I18N::Game::ItemsForCombinationSystemIsLacking,
                                SEASON3B::TYPE_ERROR_MESSAGE);
        return false;
    }
    if (!Check_LuckyItem(m_pNewInventoryCtrl->GetItem(0)))
    {
        g_pChatListBox->AddText(L"", I18N::Game::CorrespondingItemIsInappropriate,
                                SEASON3B::TYPE_ERROR_MESSAGE);
        return false;
    }
#ifdef LEM_FIX_LUCKYITEM_SLOTCHECK
    if (g_pMyInventory->FindEmptySlot(4, 4) == -1)
#else  // LEM_FIX_LUCKYITEM_SLOTCHECK
    if (g_pMyInventory->GetInventoryCtrl()->FindEmptySlot(4, 4) == -1)
#endif // LEM_FIX_LUCKYITEM_SLOTCHECK
    {
        g_pChatListBox->AddText(L"", I18N::Game::InventorySpaceIsInsufficient,
                                SEASON3B::TYPE_ERROR_MESSAGE);
        return false;
    }

    SEASON3B::CreateMessageBox(
        MSGBOX_LAYOUT_CLASS(SEASON3B::CLuckyItemMsgBoxLayout, SessionOrigin()));
    return true;
}

bool CNewUILuckyItemWnd::UpdateMouseEvent(void)
{
    SyncModernGeometry();
    if (m_pNewInventoryCtrl && false == m_pNewInventoryCtrl->UpdateMouseEvent())
        return false;
    Process_InventoryCtrl();

    if (m_ModernPanel.TakeClick("btnClose"))
        g_pNewUISystem->Hide(INTERFACE_LUCKYITEMWND);

    Process_BTN_Action();

    if (IsMouseInModernPanel())
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

bool CNewUILuckyItemWnd::UpdateKeyEvent(void)
{
    return true;
}

bool CNewUILuckyItemWnd::Update(void)
{
    m_mixEffectTicks = (std::max)(0.f, m_mixEffectTicks - FPS_ANIMATION_FACTOR);
    if (m_ModernPanel.TakeFocus() && m_pNewUIMng)
        m_pNewUIMng->BringToFront(this);
    if (m_ModernPanel.TakeClick("btnClose"))
        g_pNewUISystem->Hide(INTERFACE_LUCKYITEMWND);
    Process_BTN_Action();
    m_ModernPanel.SetVisible(IsVisible());
    if (m_pNewInventoryCtrl && false == m_pNewInventoryCtrl->Update())
        return false;
    if (IsVisible())
    {
        SyncModernGeometry();
        StageModernContent();
    }

    return true;
}

float CNewUILuckyItemWnd::GetLayerDepth(void)
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

void CNewUILuckyItemWnd::SyncModernGeometry()
{
    const auto cell = m_ModernPanel.GridCell();
    if (m_pNewInventoryCtrl && cell.width > 0 && cell.height > 0)
        m_pNewInventoryCtrl->SetOwnerGeometry(cell.x, cell.y, cell.width, cell.height);
}
bool CNewUILuckyItemWnd::IsMouseInModernPanel() const
{
    const auto rect = m_ModernPanel.PanelRect();
    return CheckMouseIn(static_cast<int>(std::floor(rect.x)), static_cast<int>(std::floor(rect.y)),
                        static_cast<int>(std::ceil(rect.width)),
                        static_cast<int>(std::ceil(rect.height)));
}

std::optional<bool> CNewUILuckyItemWnd::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernPanel.ProcessInput(event);
}

SEASON3B::PurchaseShopInventoryLegacyCalls::PurchaseShopInventoryLegacyCalls(
    SessionKeeper &keeper, CNewUIPurchaseShopInventory &owner) noexcept
    : SessionUiLegacyBindings(keeper), owner_(owner)
{
}

SEASON3B::CNewUIPurchaseShopInventory::CNewUIPurchaseShopInventory(SessionKeeper &keeper)
    : PurchaseShopInventoryLegacyCalls(keeper, *this), renderUnit(RendererForConstruction()),
      m_pNewUIMng(NULL), m_pNewInventoryCtrl(NULL), m_ShopCharacterIndex(-1), m_SourceIndex(-1),
      m_ModernPanel(keeper, UI::Modern::PC::Inventory::RmlPrivateStoreMode::Buyer)
{
}

SEASON3B::CNewUIPurchaseShopInventory::~CNewUIPurchaseShopInventory()
{
    Release();
}

bool SEASON3B::CNewUIPurchaseShopInventory::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng || NULL == g_pNewUI3DRenderMng || NULL == g_pNewItemMng)
    {
        return false;
    }

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_PURCHASESHOP_INVENTORY, this);
    SetPos(x, y);

    m_pNewInventoryCtrl = renderUnit.CreateInventoryControl();
    if (false ==
        m_pNewInventoryCtrl->Create(GameDataForConstruction().Inventory(InventoryRole::OtherShop),
                                    g_pNewUI3DRenderMng, this, 0, 0))
    {
        SAFE_DELETE(m_pNewInventoryCtrl);
        return false;
    }

    m_pNewInventoryCtrl->SetToolTipType(TOOLTIP_TYPE_PURCHASE_SHOP);
    m_pNewInventoryCtrl->LockInventory();
    m_pNewInventoryCtrl->SetRenderSlotFrame(false);
    m_pNewInventoryCtrl->SetOwnerRendered(true);
    m_ModernPanel.Create();
    Show(false);
    return true;
}

void SEASON3B::CNewUIPurchaseShopInventory::Release()
{
    m_ModernPanel.Release();
    SAFE_DELETE(m_pNewInventoryCtrl);

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool SEASON3B::CNewUIPurchaseShopInventory::InsertItem(int iIndex,
                                                       std::span<const BYTE> pbyItemPacket)
{
    return m_pNewInventoryCtrl != nullptr && m_pNewInventoryCtrl->AddItem(iIndex, pbyItemPacket);
}

void SEASON3B::CNewUIPurchaseShopInventory::DeleteItem(int iIndex)
{
    if (m_pNewInventoryCtrl == nullptr)
        return;
    ITEM *const item = m_pNewInventoryCtrl->FindItem(iIndex);
    if (item != nullptr)
        m_pNewInventoryCtrl->RemoveItem(item);
}

ITEM *SEASON3B::CNewUIPurchaseShopInventory::FindItem(int iLinealPos)
{
    return m_pNewInventoryCtrl != nullptr ? m_pNewInventoryCtrl->FindItem(iLinealPos) : nullptr;
}

int SEASON3B::CNewUIPurchaseShopInventory::GetItemInventoryIndex(ITEM *pItem)
{
    return m_pNewInventoryCtrl != nullptr && pItem != nullptr
               ? m_pNewInventoryCtrl->GetIndexByItem(pItem)
               : -1;
}

bool SEASON3B::CNewUIPurchaseShopInventory::UpdateMouseEvent()
{
    if (ProcessModernChanges())
        return false;
    SyncModernInventoryGeometry();
    if (m_pNewInventoryCtrl != nullptr && false == m_pNewInventoryCtrl->UpdateMouseEvent())
    {
        return false;
    }
    return !IsMouseInModernPanel();
}

bool SEASON3B::CNewUIPurchaseShopInventory::UpdateKeyEvent()
{
    return true;
}

bool SEASON3B::CNewUIPurchaseShopInventory::Update()
{
    (void)ProcessModernChanges();
    m_ModernVisible = IsVisible();
    if (m_ModernVisible)
    {
        m_ModernContent = BuildModernContent();
        SyncModernInventoryGeometry();
    }
    return m_pNewInventoryCtrl == nullptr || m_pNewInventoryCtrl->Update();
}

void SEASON3B::CNewUIPurchaseShopInventory::ClosingProcess()
{
    if (m_pNewInventoryCtrl != nullptr)
    {
        m_pNewInventoryCtrl->RemoveAllItems();
        g_ErrorReport.Write(
            L"@ [Notice] CNewUIPurchaseShopInventory::ClosingProcess():m_pNewInventoryCtrl->RemoveAllItems(); )\n");
    }
    m_ShopCharacterIndex = -1;
    g_pMyInventory->ChangeMyShopButtonStateOpen();
}

int SEASON3B::CNewUIPurchaseShopInventory::GetPointedItemIndex()
{
    return m_pNewInventoryCtrl->GetPointedSquareIndex();
}

bool SEASON3B::CNewUIPurchaseShopInventory::ProcessModernChanges()
{
    const auto changes = m_ModernPanel.TakeChanges();
    if (changes.focus && m_pNewUIMng != nullptr)
        m_pNewUIMng->BringToFront(this);
    if (changes.dismiss)
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_PURCHASESHOP_INVENTORY);
    const bool selected = changes.selectSlot.has_value() &&
                          OpenPurchaseForSlot(static_cast<int>(*changes.selectSlot));
    return changes.dismiss || selected;
}

bool SEASON3B::CNewUIPurchaseShopInventory::OpenPurchaseForSlot(int slot)
{
    const int inventorySlot = MAX_MY_INVENTORY_EX_INDEX + slot;
    if (FindItem(inventorySlot) == nullptr)
        return false;
    ChangeSourceIndex(inventorySlot);
    CreateMessageBox(
        MSGBOX_LAYOUT_CLASS(SEASON3B::CPersonalShopItemBuyMsgBoxLayout, SessionOrigin()));
    return true;
}

void SEASON3B::CNewUIPurchaseShopInventory::SyncModernInventoryGeometry()
{
    if (m_pNewInventoryCtrl == nullptr)
        return;
    const auto grid = m_ModernPanel.InventoryGridRect(static_cast<int>(ModernUiViewportWidth()),
                                                      static_cast<int>(ModernUiViewportHeight()));
    if (grid.width <= 0.0f || grid.height <= 0.0f)
        return;
    m_pNewInventoryCtrl->SetOwnerGeometry(
        grid.x, grid.y, grid.width / UI::Modern::PC::Inventory::RmlPrivateStorePanel::GridColumns(),
        grid.height / UI::Modern::PC::Inventory::RmlPrivateStorePanel::GridRows());
}

bool SEASON3B::CNewUIPurchaseShopInventory::IsMouseInModernPanel() const
{
    const auto rect = m_ModernPanel.ReferenceRect(static_cast<int>(ModernUiViewportWidth()),
                                                  static_cast<int>(ModernUiViewportHeight()));
    return CheckMouseIn(static_cast<int>(std::floor(rect.x)), static_cast<int>(std::floor(rect.y)),
                        static_cast<int>(std::ceil(rect.width)),
                        static_cast<int>(std::ceil(rect.height)));
}

UI::Modern::PC::Inventory::RmlPrivateStoreContent SEASON3B::CNewUIPurchaseShopInventory::
    BuildModernContent() const
{
    UI::Modern::PC::Inventory::RmlPrivateStoreContent content;
    content.title = I18N::Game::PurchasingStore;
    content.shopNameLabel = I18N::Game::StoreName;
    content.shopName = m_TitleText;
    content.closeLabel = I18N::Game::Close388;
    content.showSellerActions = false;
    return content;
}

float SEASON3B::CNewUIPurchaseShopInventory::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

std::optional<bool> SEASON3B::CNewUIPurchaseShopInventory::ProcessModernUiInput(
    const SessionInputEvent &event)
{
    return m_ModernPanel.RouteInput(event);
}

CNewUISetItemExplanation::CNewUISetItemExplanation(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()),
      panel_(keeper, "set-explanation")
{
}
CNewUISetItemExplanation::~CNewUISetItemExplanation()
{
    Release();
}
bool CNewUISetItemExplanation::Create(CNewUIManager *manager, int, int)
{
    if (!manager)
        return false;
    manager_ = manager;
    manager->AddUIObj(INTERFACE_SETITEM_EXPLANATION, this);
    Show(false);
    return true;
}
void CNewUISetItemExplanation::Release()
{
    panel_.Release();
    content_ = {};
    selection_ = -1;
    locale_.clear();
    visible_ = false;
    if (!manager_)
        return;
    manager_->RemoveUIObj(this);
    manager_ = nullptr;
}

bool CNewUISetItemExplanation::UpdateMouseEvent()
{
    return !panel_.ContainsReferencePointer(MouseX, MouseY);
}
bool CNewUISetItemExplanation::UpdateKeyEvent()
{
    if (!IsVisible() || (!IsPress(VK_ESCAPE) && !IsPress(VK_F1)))
        return true;
    g_pNewUISystem->Hide(INTERFACE_SETITEM_EXPLANATION);
    PlayBuffer(SOUND_CLICK01);
    return false;
}

bool CNewUISetItemExplanation::Update()
{
    if (IsVisible())
        StageContent();
    visible_ = IsVisible() && !content_.notes.empty();
    return true;
}

bool CNewUISetItemExplanation::ProcessModernUiInput(const SessionInputEvent &event)
{
    return visible_ && panel_.ProcessInput(event);
}
float CNewUISetItemExplanation::GetLayerDepth()
{
    return 6.6f;
}
float CNewUISetItemExplanation::GetKeyEventOrder()
{
    return 10.0f;
}
void CNewUISetItemExplanation::OpenningProcess()
{
    selection_ = -1;
}
void CNewUISetItemExplanation::ClosingProcess()
{
    visible_ = false;
}

// Construction/Destruction
CNewUIStorageInventory::CNewUIStorageInventory(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderUnit(RendererForConstruction()),
      m_ModernPanel(keeper, "storage.rml")
{
    m_pNewUIMng = nullptr;
    m_pNewInventoryCtrl = nullptr;
    m_Pos.x = m_Pos.y = 0;
    m_nBackupSourceInvenIndex = -1;
}

CNewUIStorageInventory::~CNewUIStorageInventory()
{
    Release();
}

bool CNewUIStorageInventory::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (nullptr == pNewUIMng || nullptr == g_pNewUI3DRenderMng || nullptr == g_pNewItemMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(INTERFACE_STORAGE, this);

    m_pNewInventoryCtrl = renderUnit.CreateInventoryControl();
    if (false ==
        m_pNewInventoryCtrl->Create(GameDataForConstruction().Inventory(InventoryRole::Vault),
                                    g_pNewUI3DRenderMng, this, 0, 0))
    {
        SAFE_DELETE(m_pNewInventoryCtrl);
        return false;
    }

    m_pNewInventoryCtrl->SetOwnerRendered(true);
    m_pNewInventoryCtrl->SetRenderSlotFrame(false);
    SetPos(x, y);

    m_bLock = false;
    m_bCorrectPassword = false;
    SetItemAutoMove(false);
    InitBackupItemInfo();

    Show(false);

    return true;
}

void CNewUIStorageInventory::Release()
{
    m_ModernPanel.Release();

    SAFE_DELETE(m_pNewInventoryCtrl);

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }
}

bool CNewUIStorageInventory::UpdateMouseEvent()
{
    SyncModernGeometry();
    if (m_pNewInventoryCtrl && false == m_pNewInventoryCtrl->UpdateMouseEvent())
        return false;

    ProcessInventoryCtrl();

    if (ProcessBtns())
        return false;

    if (IsMouseInModernPanel())
    {
        if (IsPress(VK_RBUTTON))
        {
            MouseRButton = false;
            MouseRButtonPop = false;
            MouseRButtonPush = false;
            return false;
        }

        if (!IsNone(VK_LBUTTON))
        {
            return false;
        }
    }

    return true;
}

bool CNewUIStorageInventory::UpdateKeyEvent()
{
    if (!g_pNewUISystem->IsVisible(INTERFACE_STORAGE))
    {
        return true;
    }

    if (IsPress(VK_ESCAPE) == true)
    {
        g_pNewUISystem->Hide(INTERFACE_STORAGE);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }

    if (CharacterAttribute->IsVaultExtended > 0 && IsPress('H'))
    {
        g_pNewUISystem->Toggle(INTERFACE_STORAGE_EXT);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }
    return true;
}

bool CNewUIStorageInventory::Update()
{
    if (m_ModernPanel.TakeFocus() && m_pNewUIMng)
        m_pNewUIMng->BringToFront(this);
    ProcessBtns();
    m_ModernPanel.SetVisible(IsVisible());
    if (m_pNewInventoryCtrl && !m_pNewInventoryCtrl->Update())
        return false;
    if (IsVisible())
    {
        SyncModernGeometry();
        StageModernContent();
    }
    return true;
}

float CNewUIStorageInventory::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

CNewUIInventoryCtrl *CNewUIStorageInventory::GetInventoryCtrl() const
{
    return m_pNewInventoryCtrl;
}

void CNewUIStorageInventory::LockStorage(bool bLock)
{
    m_bLock = bLock;
}

bool CNewUIStorageInventory::ProcessClosing()
{
    if (EquipmentItem)
        return false;

    g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();
    DeleteAllItems();
    SocketClient->ToGameServer()->SendVaultClosed();
    return true;
}

bool CNewUIStorageInventory::InsertItem(int nIndex, std::span<const BYTE> pbyItemPacket)
{
    if (m_pNewInventoryCtrl)
        return m_pNewInventoryCtrl->AddItem(nIndex, pbyItemPacket);

    return false;
}

void CNewUIStorageInventory::DeleteAllItems()
{
    if (m_pNewInventoryCtrl)
        m_pNewInventoryCtrl->RemoveAllItems();
}

void CNewUIStorageInventory::ProcessInventoryCtrl()
{
    if (nullptr == m_pNewInventoryCtrl)
    {
        return;
    }

    CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();
    if (pPickedItem)
    {
        ITEM *pItemObj = pPickedItem->GetItem();
        if (nullptr == pItemObj)
        {
            return;
        }

        if (IsPress(VK_LBUTTON) || IsRelease(VK_LBUTTON))
        {
            const int nDstIndex = pPickedItem->GetTargetLinealPos(m_pNewInventoryCtrl);

            if (nDstIndex >= 0 && m_pNewInventoryCtrl->CanMove(nDstIndex, pItemObj))
            {
                const int nSrcIndex = pPickedItem->GetSourceLinealPos();
                const auto sourceStorageType = pPickedItem->GetSourceStorageType();
                const auto targetStorageType = m_pNewInventoryCtrl->GetStorageType();
                SendRequestEquipmentItem(sourceStorageType, nSrcIndex, pItemObj, targetStorageType,
                                         nDstIndex);
            }
        }
        else
        {
            if (::IsStoreBan(pItemObj))
            {
                m_pNewInventoryCtrl->SetSquareColorNormal(1.0f, 0.0f, 0.0f);
            }
            else
            {
                m_pNewInventoryCtrl->SetSquareColorNormal(0.1f, 0.4f, 0.8f);
            }
        }
    }
    else if (IsPress(VK_RBUTTON))
    {
        ProcessStorageItemAutoMove();
    }
}

void CNewUIStorageInventory::ProcessStorageItemAutoMove()
{
    if (g_pPickedItem)
        if (g_pPickedItem->GetItem())
            return;

    if (IsItemAutoMove())
        return;

    ITEM *pItemObj = m_pNewInventoryCtrl->FindItemAtPt(MouseX, MouseY);
    if (pItemObj)
    {
        int nDstIndex = g_pMyInventory->FindEmptySlotIncludingExtensions(pItemObj);
        if (-1 != nDstIndex)
        {
            SetItemAutoMove(true);

            int nSrcIndex = pItemObj->y * m_pNewInventoryCtrl->GetNumberOfColumn() + pItemObj->x;
            SendRequestItemToMyInven(pItemObj, nSrcIndex, nDstIndex);

            PlayBuffer(SOUND_GET_ITEM01);
        }
    }
}

bool CNewUIStorageInventory::ProcessMyInvenItemAutoMove(CNewUIInventoryCtrl *sourceCtrl)
{
    if (g_pPickedItem && g_pPickedItem->GetItem())
    {
        return false;
    }

    if (IsItemAutoMove())
    {
        return false;
    }

    if (sourceCtrl == nullptr)
    {
        sourceCtrl = g_pMyInventory->GetInventoryCtrl();
    }

    if (sourceCtrl == nullptr)
    {
        return false;
    }

    if (const auto pItemObj = sourceCtrl->FindItemAtPt(MouseX, MouseY))
    {
        if (pItemObj->Type == ITEM_WIZARDS_RING)
            return false;

        const int emptySlotIndex = FindEmptySlot(pItemObj);
        if (-1 != emptySlotIndex)
        {
            const int nSrcIndex = sourceCtrl->GetIndexByItem(pItemObj);
            if (nSrcIndex < 0)
            {
                return false;
            }

            SetItemAutoMove(true, nSrcIndex);
            SendRequestItemToStorage(pItemObj, nSrcIndex, emptySlotIndex);
            PlayBuffer(SOUND_GET_ITEM01);
            return true;
        }
    }

    return false;
}

void CNewUIStorageInventory::SendRequestItemToMyInven(ITEM *pItemObj, int nStorageIndex,
                                                      int nInvenIndex)
{
    if (!IsStorageLocked() || IsCorrectPassword())
    {
        SendRequestEquipmentItem(STORAGE_TYPE::VAULT, nStorageIndex, pItemObj,
                                 STORAGE_TYPE::INVENTORY, nInvenIndex);
    }
    else
    {
        SetBackupInvenIndex(nInvenIndex);
        if (!IsItemAutoMove())
            g_pPickedItem->HidePickedItem();

        CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CPasswordKeyPadMsgBoxLayout, SessionOrigin()));
    }
}

void CNewUIStorageInventory::SendRequestItemToStorage(ITEM *pItemObj, int nInvenIndex,
                                                      int nStorageIndex)
{
    if (IsStoreBan(pItemObj))
    {
#ifdef KJH_PBG_ADD_INGAMESHOP_SYSTEM
        // MessageBox
        CMsgBoxIGSCommon *pMsgBox = nullptr;
        CreateMessageBox(MSGBOX_LAYOUT_CLASS(CMsgBoxIGSCommonLayout, SessionOrigin()), &pMsgBox);
        pMsgBox->Initialize(I18N::Game::Error, I18N::Game::TheseItemsCannotBeStoredInTheInventory);
#endif // KJH_PBG_ADD_INGAMESHOP_SYSTEM

        g_pSystemLogBox->AddText(I18N::Game::TheseItemsCannotBeStoredInTheInventory,
                                 TYPE_ERROR_MESSAGE);
        g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();

        if (IsItemAutoMove())
            SetItemAutoMove(false);
    }
    else
    {
        SendRequestEquipmentItem(STORAGE_TYPE::INVENTORY, nInvenIndex, pItemObj,
                                 STORAGE_TYPE::VAULT, nStorageIndex);
    }
}

bool CNewUIStorageInventory::ProcessBtns()
{
    if (CharacterAttribute->IsVaultExtended > 0 && m_ModernPanel.TakeClick("btnExtend"))
    {
        g_pNewUISystem->Toggle(INTERFACE_STORAGE_EXT);
        return true;
    }

    if (m_ModernPanel.TakeClick("btnDeposit"))
    {
        CreateMessageBox(MSGBOX_LAYOUT_CLASS(SEASON3B::CZenReceiptMsgBoxLayout, SessionOrigin()));
        return true;
    }

    if (m_ModernPanel.TakeClick("btnWithDraw"))
    {
        CreateMessageBox(MSGBOX_LAYOUT_CLASS(SEASON3B::CZenPaymentMsgBoxLayout, SessionOrigin()));
        return true;
    }

    if (m_ModernPanel.TakeClick("btnLift"))
    {
        if (m_bLock)
        {
            CreateMessageBox(
                MSGBOX_LAYOUT_CLASS(SEASON3B::CStorageUnlockMsgBoxLayout, SessionOrigin()));
        }
        else
        {
            CreateMessageBox(
                MSGBOX_LAYOUT_CLASS(SEASON3B::CStorageLockKeyPadMsgBoxLayout, SessionOrigin()));
        }
        return true;
    }

    if (m_ModernPanel.TakeClick("btnClose"))
    {
        g_pNewUISystem->Hide(INTERFACE_STORAGE);
        return true;
    }

    return false;
}

void CNewUIStorageInventory::SetItemAutoMove(bool bItemAutoMove, int nSourceInvenIndex)
{
    m_bItemAutoMove = bItemAutoMove;

    if (bItemAutoMove)
    {
        m_nBackupMouseX = MouseX;
        m_nBackupMouseY = MouseY;
        m_nBackupSourceInvenIndex = nSourceInvenIndex;
    }
    else
    {
        m_nBackupMouseX = m_nBackupMouseY = 0;
        m_nBackupSourceInvenIndex = -1;
    }
}

void CNewUIStorageInventory::InitBackupItemInfo()
{
    m_bTakeZen = false;
    m_nBackupTakeZen = 0;
    m_nBackupInvenIndex = -1;
    m_nBackupSourceInvenIndex = -1;
}

void CNewUIStorageInventory::SetBackupTakeZen(int nZen)
{
    m_bTakeZen = true;
    m_nBackupTakeZen = nZen;
}

void CNewUIStorageInventory::SetBackupInvenIndex(int nInvenIndex)
{
    m_bTakeZen = false;
    m_nBackupInvenIndex = nInvenIndex;
}

int CNewUIStorageInventory::FindEmptySlot(ITEM *pItemObj)
{
    if (pItemObj == nullptr)
        return -1;

    ITEM_ATTRIBUTE *pItemAttr = &ItemAttribute[pItemObj->Type];

    if (m_pNewInventoryCtrl)
        return m_pNewInventoryCtrl->FindEmptySlot(pItemAttr->Width, pItemAttr->Height);

    return -1;
}

void CNewUIStorageInventory::ProcessToReceiveStorageStatus(BYTE byStatus)
{
    switch (byStatus)
    {
    case 0:
        LockStorage(false);
        SetCorrectPassword(false);
        break;

    case 1:
        LockStorage(true);
        SetCorrectPassword(false);
        break;

    case 10:
        CreateOkMessageBox(I18N::Game::IncorrectPassword);
        g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();
        InitBackupItemInfo();
        SetItemAutoMove(false);
        break;

    case 11:
        CreateOkMessageBox(I18N::Game::InventoryIsAlreadyLocked);
        break;

    case 12:
        if (IsStorageLocked() && !IsCorrectPassword())
        {
            if (m_bTakeZen)
            {
                SocketClient->ToGameServer()->SendVaultMoveMoneyRequest(
                    VaultMoneyMoveDirection::VaultToInventory, GetBackupTakeZen());
                InitBackupItemInfo();
            }
            else
            {
                ITEM *pItemObj;
                int nStorageIndex;

                if (IsItemAutoMove())
                {
                    pItemObj = m_pNewInventoryCtrl->FindItemAtPt(m_nBackupMouseX, m_nBackupMouseY);
                    nStorageIndex =
                        pItemObj->y * m_pNewInventoryCtrl->GetNumberOfColumn() + pItemObj->x;
                }
                else
                {
                    nStorageIndex = g_pPickedItem->GetSourceLinealPos();
                    pItemObj = g_pPickedItem->GetItem();
                }

                SendRequestEquipmentItem(STORAGE_TYPE::VAULT, nStorageIndex, pItemObj,
                                         STORAGE_TYPE::INVENTORY, GetBackupInvenIndex());

                InitBackupItemInfo();
            }
        }
        LockStorage(true);
        SetCorrectPassword(true);
        break;

    case 13:
        CreateOkMessageBox(I18N::Game::ThePasswordYouHaveEnteredIsIncorrect);
        break;
    }
}

int CNewUIStorageInventory::GetPointedItemIndex()
{
    return m_pNewInventoryCtrl->GetPointedSquareIndex();
}

void CNewUIStorageInventory::SyncModernGeometry()
{
    const auto cell = m_ModernPanel.GridCell();
    if (m_pNewInventoryCtrl && cell.width > 0 && cell.height > 0)
        m_pNewInventoryCtrl->SetOwnerGeometry(cell.x, cell.y, cell.width, cell.height);
}
bool CNewUIStorageInventory::IsMouseInModernPanel() const
{
    const auto rect = m_ModernPanel.PanelRect();
    return CheckMouseIn(static_cast<int>(std::floor(rect.x)), static_cast<int>(std::floor(rect.y)),
                        static_cast<int>(std::ceil(rect.width)),
                        static_cast<int>(std::ceil(rect.height)));
}

std::optional<bool> CNewUIStorageInventory::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernPanel.ProcessInput(event);
}

// Construction/Destruction
CNewUIStorageInventoryExt::CNewUIStorageInventoryExt(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderUnit(RendererForConstruction()),
      m_ModernPanel(keeper, "storage_extension.rml")
{
    m_pNewUIMng = nullptr;
    m_pNewInventoryCtrl = nullptr;
    m_Pos.x = m_Pos.y = 0;
    m_nBackupSourceInvenIndex = -1;
}

CNewUIStorageInventoryExt::~CNewUIStorageInventoryExt()
{
    Release();
}

bool CNewUIStorageInventoryExt::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (nullptr == pNewUIMng || nullptr == g_pNewUI3DRenderMng || nullptr == g_pNewItemMng)
    {
        return false;
    }

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(INTERFACE_STORAGE_EXT, this);

    m_pNewInventoryCtrl = renderUnit.CreateInventoryControl();
    if (false == m_pNewInventoryCtrl->Create(
                     GameDataForConstruction().Inventory(InventoryRole::VaultExtension),
                     g_pNewUI3DRenderMng, this, 0, 0))
    {
        SAFE_DELETE(m_pNewInventoryCtrl);
        return false;
    }

    m_pNewInventoryCtrl->SetOwnerRendered(true);
    m_pNewInventoryCtrl->SetRenderSlotFrame(false);
    SetPos(x, y);
    SetItemAutoMove(false);

    Show(false);

    return true;
}

void CNewUIStorageInventoryExt::Release()
{
    m_ModernPanel.Release();

    SAFE_DELETE(m_pNewInventoryCtrl);

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }
}

bool CNewUIStorageInventoryExt::UpdateMouseEvent()
{
    SyncModernGeometry();
    if (m_pNewInventoryCtrl && false == m_pNewInventoryCtrl->UpdateMouseEvent())
        return false;

    ProcessInventoryCtrl();

    if (ProcessBtns())
        return false;

    if (IsMouseInModernPanel())
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

bool CNewUIStorageInventoryExt::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(INTERFACE_STORAGE) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(INTERFACE_STORAGE);
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }
    return true;
}

bool CNewUIStorageInventoryExt::Update()
{
    if (m_ModernPanel.TakeFocus() && m_pNewUIMng)
        m_pNewUIMng->BringToFront(this);
    ProcessBtns();
    m_ModernPanel.SetVisible(IsVisible());
    if (m_pNewInventoryCtrl && false == m_pNewInventoryCtrl->Update())
        return false;
    if (IsVisible())
    {
        SyncModernGeometry();
        StageModernContent();
    }

    return true;
}

float CNewUIStorageInventoryExt::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

CNewUIInventoryCtrl *CNewUIStorageInventoryExt::GetInventoryCtrl() const
{
    return m_pNewInventoryCtrl;
}

bool CNewUIStorageInventoryExt::ProcessClosing() const
{
    if (EquipmentItem)
        return false;

    g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();
    DeleteAllItems();
    SocketClient->ToGameServer()->SendVaultClosed();
    return true;
}

bool CNewUIStorageInventoryExt::InsertItem(int iIndex, std::span<const BYTE> pbyItemPacket) const
{
    if (m_pNewInventoryCtrl)
        return m_pNewInventoryCtrl->AddItem(iIndex, pbyItemPacket);

    return false;
}

void CNewUIStorageInventoryExt::DeleteAllItems() const
{
    if (m_pNewInventoryCtrl)
        m_pNewInventoryCtrl->RemoveAllItems();
}

void CNewUIStorageInventoryExt::ProcessInventoryCtrl()
{
    if (nullptr == m_pNewInventoryCtrl)
    {
        return;
    }

    if (const auto pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem())
    {
        ITEM *pItemObj = pPickedItem->GetItem();
        if (pItemObj == nullptr)
        {
            return;
        }

        if (IsPress(VK_LBUTTON) || IsRelease(VK_LBUTTON))
        {
            const int nDstIndex = pPickedItem->GetTargetLinealPos(m_pNewInventoryCtrl);

            if (nDstIndex >= 0 && m_pNewInventoryCtrl->CanMove(nDstIndex, pItemObj))
            {
                const int nSrcIndex = pPickedItem->GetSourceLinealPos();
                const auto sourceStorageType = pPickedItem->GetSourceStorageType();
                const auto targetStorageType = m_pNewInventoryCtrl->GetStorageType();
                SendRequestEquipmentItem(sourceStorageType, nSrcIndex, pItemObj, targetStorageType,
                                         nDstIndex);
            }
        }
        else
        {
            if (::IsStoreBan(pItemObj))
            {
                m_pNewInventoryCtrl->SetSquareColorNormal(1.0f, 0.0f, 0.0f);
            }
            else
            {
                m_pNewInventoryCtrl->SetSquareColorNormal(0.1f, 0.4f, 0.8f);
            }
        }
    }
    else if (IsPress(VK_RBUTTON))
    {
        ProcessStorageItemAutoMove();
    }
}

void CNewUIStorageInventoryExt::ProcessStorageItemAutoMove()
{
    if (g_pPickedItem)
        if (g_pPickedItem->GetItem())
            return;

    if (IsItemAutoMove())
        return;

    if (const auto pItemObj = m_pNewInventoryCtrl->FindItemAtPt(MouseX, MouseY))
    {
        const int nDstIndex = g_pMyInventory->FindEmptySlotIncludingExtensions(pItemObj);
        if (-1 != nDstIndex)
        {
            SetItemAutoMove(true);

            const int nSrcIndex = m_pNewInventoryCtrl->GetIndexByItem(pItemObj);
            g_pStorageInventory->SendRequestItemToMyInven(pItemObj, nSrcIndex, nDstIndex);

            PlayBuffer(SOUND_GET_ITEM01);
        }
    }
}

bool CNewUIStorageInventoryExt::ProcessMyInvenItemAutoMove(CNewUIInventoryCtrl *sourceCtrl)
{
    if (g_pPickedItem && g_pPickedItem->GetItem())
    {
        return false;
    }

    if (IsItemAutoMove())
    {
        return false;
    }

    if (sourceCtrl == nullptr)
    {
        sourceCtrl = g_pMyInventory->GetInventoryCtrl();
    }

    if (sourceCtrl == nullptr)
    {
        return false;
    }

    if (const auto pItemObj = sourceCtrl->FindItemAtPt(MouseX, MouseY))
    {
        if (pItemObj->Type == ITEM_WIZARDS_RING)
            return false;

        const int emptySlotIndex = FindEmptySlot(pItemObj);
        if (emptySlotIndex != -1)
        {
            const int nSrcIndex = sourceCtrl->GetIndexByItem(pItemObj);
            if (nSrcIndex < 0)
            {
                return false;
            }

            SetItemAutoMove(true, nSrcIndex);
            g_pStorageInventory->SendRequestItemToStorage(pItemObj, nSrcIndex, emptySlotIndex);
            PlayBuffer(SOUND_GET_ITEM01);
            return true;
        }
    }

    return false;
}

bool CNewUIStorageInventoryExt::ProcessBtns()
{
    if (m_ModernPanel.TakeClick("btnClose"))
    {
        g_pNewUISystem->Hide(INTERFACE_STORAGE_EXT);
        return true;
    }
    return false;
}

void CNewUIStorageInventoryExt::SetItemAutoMove(bool bItemAutoMove, int nSourceInvenIndex)
{
    m_bItemAutoMove = bItemAutoMove;

    if (bItemAutoMove)
    {
        m_nBackupMouseX = MouseX;
        m_nBackupMouseY = MouseY;
        m_nBackupSourceInvenIndex = nSourceInvenIndex;
    }
    else
    {
        m_nBackupMouseX = m_nBackupMouseY = 0;
        m_nBackupSourceInvenIndex = -1;
    }
}

int CNewUIStorageInventoryExt::FindEmptySlot(const ITEM *pItemObj) const
{
    if (pItemObj == nullptr)
        return -1;

    const ITEM_ATTRIBUTE *pItemAttr = &ItemAttribute[pItemObj->Type];

    if (m_pNewInventoryCtrl)
        return m_pNewInventoryCtrl->FindEmptySlot(pItemAttr->Width, pItemAttr->Height);

    return -1;
}

int CNewUIStorageInventoryExt::GetPointedItemIndex() const
{
    return m_pNewInventoryCtrl->GetPointedSquareIndex();
}

void CNewUIStorageInventoryExt::SyncModernGeometry()
{
    const auto cell = m_ModernPanel.GridCell();
    if (m_pNewInventoryCtrl && cell.width > 0 && cell.height > 0)
        m_pNewInventoryCtrl->SetOwnerGeometry(cell.x, cell.y, cell.width, cell.height);
}
bool CNewUIStorageInventoryExt::IsMouseInModernPanel() const
{
    const auto rect = m_ModernPanel.PanelRect();
    return CheckMouseIn(static_cast<int>(std::floor(rect.x)), static_cast<int>(std::floor(rect.y)),
                        static_cast<int>(std::ceil(rect.width)),
                        static_cast<int>(std::ceil(rect.height)));
}

std::optional<bool> CNewUIStorageInventoryExt::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernPanel.ProcessInput(event);
}

// Desc: implementation of the CNewUITrade class.

CNewUITrade::CNewUITrade(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderUnit(RendererForConstruction()),
      m_ModernPanel(keeper, "trade.rml", "trade_confirm.rml")
{
    m_pNewUIMng = NULL;
    m_pYourInvenCtrl = m_pMyInvenCtrl = NULL;
    m_Pos.x = m_Pos.y = 0;
}

CNewUITrade::~CNewUITrade()
{
    Release();
}

bool CNewUITrade::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng || NULL == g_pNewUI3DRenderMng || NULL == g_pNewItemMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_TRADE, this);

    m_pYourInvenCtrl = renderUnit.CreateInventoryControl();
    if (false ==
        m_pYourInvenCtrl->Create(GameDataForConstruction().Inventory(InventoryRole::TradeOther),
                                 g_pNewUI3DRenderMng, this, 0, 0))
    {
        SAFE_DELETE(m_pYourInvenCtrl);
        return false;
    }

    m_pMyInvenCtrl = renderUnit.CreateInventoryControl();
    if (false ==
        m_pMyInvenCtrl->Create(GameDataForConstruction().Inventory(InventoryRole::TradeOwn),
                               g_pNewUI3DRenderMng, this, 0, 0))
    {
        SAFE_DELETE(m_pMyInvenCtrl);
        return false;
    }

    SetPos(x, y);

    for (auto *control : {m_pYourInvenCtrl, m_pMyInvenCtrl})
    {
        control->SetOwnerRendered(true);
        control->SetRenderSlotFrame(false);
    }
    ::memset(m_szYourID, 0, sizeof(m_szYourID));
    m_bTradeAlert = false;

    InitTradeInfo();
    InitYourInvenBackUp();

    Show(false);

    return true;
}

void CNewUITrade::InitTradeInfo()
{
    m_nYourLevel = 0;
    m_nYourGuildType = -1;
    m_nYourTradeGold = 0;
    m_nMyTradeGold = 0;
    m_nMyTradeWait = 0;
    m_bYourConfirm = m_bMyConfirm = false;
}

void CNewUITrade::InitYourInvenBackUp()
{
    for (int i = 0; i < MAX_TRADE_INVEN; ++i)
        m_aYourInvenBackUp[i].Type = -1;
}

void CNewUITrade::Release()
{
    m_ModernPanel.Release();

    SAFE_DELETE(m_pMyInvenCtrl);
    SAFE_DELETE(m_pYourInvenCtrl);

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool CNewUITrade::UpdateMouseEvent()
{
    SyncModernGeometry();
    if ((m_pYourInvenCtrl && false == m_pYourInvenCtrl->UpdateMouseEvent()) ||
        (m_pMyInvenCtrl && false == m_pMyInvenCtrl->UpdateMouseEvent()))
    {
        if (IsPress(VK_LBUTTON) && g_pMyInventory->GetInventoryCtrl()->GetPickedItem() &&
            g_pMyInventory->GetInventoryCtrl()->GetPickedItem()->GetOwnerInventory() ==
                m_pMyInvenCtrl &&
            m_bMyConfirm)
        {
            m_bMyConfirm = false;
            SocketClient->ToGameServer()->SendTradeButtonStateChange(TradeButtonState::Unchecked);
        }

        return false;
    }

    ProcessMyInvenCtrl();

    if (ProcessBtns())
        return false;

    if (IsMouseInModernPanel())
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

bool CNewUITrade::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_TRADE) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            SocketClient->ToGameServer()->SendTradeCancel();
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_TRADE);
            PlayBuffer(SOUND_CLICK01);

            return false;
        }
    }
    return true;
}

bool CNewUITrade::Update()
{
    m_nMyTradeWait = (std::max)(0.f, m_nMyTradeWait - FPS_ANIMATION_FACTOR);
    if (m_ModernPanel.TakeFocus() && m_pNewUIMng)
        m_pNewUIMng->BringToFront(this);
    ProcessBtns();
    m_ModernPanel.SetVisible(IsVisible());
    if ((m_pYourInvenCtrl && false == m_pYourInvenCtrl->Update()) ||
        (m_pMyInvenCtrl && false == m_pMyInvenCtrl->Update()))
        return false;
    if (IsVisible())
    {
        SyncModernGeometry();
        StageModernContent();
    }

    return true;
}

void CNewUITrade::ConvertYourLevel(int &rnLevel, DWORD &rdwColor)
{
    if (m_nYourLevel >= 400)
    {
        rnLevel = 400;
        rdwColor = (255 << 24) + (153 << 16) + (153 << 8) + (255);
    }
    else if (m_nYourLevel >= 300)
    {
        rnLevel = 300;
        rdwColor = (255 << 24) + (255 << 16) + (153 << 8) + (255);
    }
    else if (m_nYourLevel >= 200)
    {
        rnLevel = 200;
        rdwColor = (255 << 24) + (255 << 16) + (230 << 8) + (210);
    }
    else if (m_nYourLevel >= 100)
    {
        rnLevel = 100;
        rdwColor = (255 << 24) + (24 << 16) + (201 << 8) + (0);
    }
    else if (m_nYourLevel >= 50)
    {
        rnLevel = 50;
        rdwColor = (255 << 24) + (0 << 16) + (150 << 8) + (255);
    }
    else //  ???.
    {
        rnLevel = 10;
        rdwColor = (255 << 24) + (0 << 16) + (0 << 8) + (255);
    }
}

float CNewUITrade::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

void CNewUITrade::ProcessClosing()
{
    m_pYourInvenCtrl->RemoveAllItems();
    m_pMyInvenCtrl->RemoveAllItems();

    if (m_bTradeAlert)
        InitYourInvenBackUp();
}

void CNewUITrade::ProcessMyInvenCtrl()
{
    if (NULL == m_pMyInvenCtrl)
        return;

    if (IsPress(VK_LBUTTON))
    {
        CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();
        if (NULL == pPickedItem)
            return;

        ITEM *pItemObj = pPickedItem->GetItem();
        if (pPickedItem->GetOwnerInventory() == g_pMyInventory->GetInventoryCtrl())
        {
            int nSrcIndex = pPickedItem->GetSourceLinealPos();
            int nDstIndex = pPickedItem->GetTargetLinealPos(m_pMyInvenCtrl);
            if (nDstIndex != -1 && m_pMyInvenCtrl->CanMove(nDstIndex, pItemObj))
                SendRequestItemToTrade(pItemObj, nSrcIndex, nDstIndex);
        }
        else if (pPickedItem->GetOwnerInventory() == m_pMyInvenCtrl)
        {
            int nSrcIndex = pPickedItem->GetSourceLinealPos();
            int nDstIndex = pPickedItem->GetTargetLinealPos(m_pMyInvenCtrl);
            if (nDstIndex != -1 && m_pMyInvenCtrl->CanMove(nDstIndex, pItemObj))
            {
                SendRequestEquipmentItem(STORAGE_TYPE::TRADE, nSrcIndex, pItemObj,
                                         STORAGE_TYPE::TRADE, nDstIndex);
            }
        }
        else if (pItemObj->ex_src_type == ITEM_EX_SRC_EQUIPMENT)
        {
            int nSrcIndex = pPickedItem->GetSourceLinealPos();
            int nDstIndex = pPickedItem->GetTargetLinealPos(m_pMyInvenCtrl);
            if (nDstIndex != -1 && m_pMyInvenCtrl->CanMove(nDstIndex, pItemObj))
                SendRequestItemToTrade(pItemObj, nSrcIndex, nDstIndex);
        }
    }
}

void CNewUITrade::SendRequestItemToTrade(ITEM *pItemObj, int nInvenIndex, int nTradeIndex)
{
    if (IsTradeBan(pItemObj))
    {
        g_pSystemLogBox->AddText(I18N::Game::TheseItemsCannotBeTraded,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
    }
    else
    {
        m_bMyConfirm = false;
        SocketClient->ToGameServer()->SendTradeButtonStateChange(TradeButtonState::Unchecked);

        SendRequestEquipmentItem(STORAGE_TYPE::INVENTORY, nInvenIndex, pItemObj,
                                 STORAGE_TYPE::TRADE, nTradeIndex);
    }
}

void CNewUITrade::SendRequestItemToMyInven(ITEM *pItemObj, int nTradeIndex, int nInvenIndex)
{
    SendRequestEquipmentItem(STORAGE_TYPE::TRADE, nTradeIndex, pItemObj, STORAGE_TYPE::INVENTORY,
                             nInvenIndex);

    if (m_bMyConfirm)
    {
        AlertTrade();
    }
    m_nMyTradeWait = 150;
}

void CNewUITrade::SendRequestMyGoldInput(int nInputGold)
{
    if (nInputGold <= (int)CharacterMachine->Gold + m_nMyTradeGold)
    {
        if (m_bMyConfirm)
        {
            m_bMyConfirm = false;
            SocketClient->ToGameServer()->SendTradeButtonStateChange(TradeButtonState::Unchecked);
        }

        if (m_nMyTradeGold > 0)
            m_nMyTradeWait = 150;

        m_nTempMyTradeGold = nInputGold;
        SocketClient->ToGameServer()->SendSetTradeMoney(nInputGold);
    }
    else
    {
        CreateOkMessageBox(I18N::Game::YouAreShortOfZen);
    }
}

void CNewUITrade::ProcessCloseBtn()
{
    if (g_pMyInventory->GetInventoryCtrl()->GetPickedItem() == NULL)
    {
        m_bTradeAlert = false;
        SocketClient->ToGameServer()->SendTradeCancel();
    }
}

bool CNewUITrade::ProcessBtns()
{

    if (m_ModernPanel.TakeClick("btnClose"))
    {
        PlayBuffer(SOUND_CLICK01);
        ProcessCloseBtn();
        return true;
    }
    else if (m_ModernPanel.TakeClick("btnInputZen"))
    {
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CTradeZenMsgBoxLayout, SessionOrigin()));
        PlayBuffer(SOUND_CLICK01);
        return true;
    }
    else if (m_ModernPanel.TakeClick("btnTrade"))
    {
        if (0 == m_nMyTradeWait && g_pMyInventory->GetInventoryCtrl()->GetPickedItem() == NULL)
        {
            PlayBuffer(SOUND_CLICK01);

            if (m_bTradeAlert && !m_bMyConfirm)
            {
                SEASON3B::CreateMessageBox(
                    MSGBOX_LAYOUT_CLASS(SEASON3B::CTradeAlertMsgBoxLayout, SessionOrigin()));
            }
            else
            {
                AlertTrade();
            }
        }
        return true;
    }

    return false;
}

void CNewUITrade::AlertTrade()
{
    m_bMyConfirm = !m_bMyConfirm;

    m_bTradeAlert = true;
    SocketClient->ToGameServer()->SendTradeButtonStateChange(
        m_bMyConfirm ? TradeButtonState::Checked : TradeButtonState::Unchecked);
}

void CNewUITrade::GetYourID(wchar_t *pszYourID)
{
    ::wcscpy(pszYourID, m_szYourID);
}

void CNewUITrade::ProcessToReceiveTradeRequest(char *pbyYourID)
{
    if (g_pNewUISystem->IsImpossibleTradeInterface())
    {
        SocketClient->ToGameServer()->SendTradeRequestResponse(false);
        return;
    }

    CMultiLanguage::ConvertFromUtf8(m_szYourID, pbyYourID, MAX_USERNAME_SIZE);

    SEASON3B::CreateMessageBox(MSGBOX_LAYOUT_CLASS(SEASON3B::CTradeMsgBoxLayout, SessionOrigin()));

    g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();
}

void CNewUITrade::ProcessToReceiveTradeResult(LPPTRADE pTradeData)
{
    switch (pTradeData->SubCode)
    {
    case 0:
        g_pSystemLogBox->AddText(I18N::Game::YourTradeHasBeenCanceled,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
        break;

    case 2:
        g_pSystemLogBox->AddText(I18N::Game::YouCannotTradeRightNow, SEASON3B::TYPE_ERROR_MESSAGE);
        break;

    case 1:
        g_pNewUISystem->Show(SEASON3B::INTERFACE_TRADE);

        InitTradeInfo();

        int x = 260 * MouseX / REFERENCE_WIDTH;
        SetCursorPos(x * WindowWidth / REFERENCE_WIDTH, MouseY * WindowHeight / REFERENCE_HEIGHT);

        wchar_t szTempID[MAX_USERNAME_SIZE + 1]{};
        CMultiLanguage::ConvertFromUtf8(szTempID, pTradeData->ID, MAX_USERNAME_SIZE);

        if (!m_bTradeAlert && ::wcscmp(m_szYourID, szTempID))
            InitYourInvenBackUp();

        m_bTradeAlert = false;
        m_nYourGuildType = pTradeData->GuildKey;
        wcsncpy(m_szYourID, szTempID, MAX_USERNAME_SIZE);
        m_nYourLevel = pTradeData->Level; //  ??? ??.
        break;
    }
}

void CNewUITrade::BackUpYourInven(int nYourInvenIndex)
{
    ITEM *pYourItemObj = m_pYourInvenCtrl->FindItem(nYourInvenIndex);
    if (pYourItemObj)
        BackUpYourInven(pYourItemObj);
}

void CNewUITrade::BackUpYourInven(ITEM *pYourItemObj)
{
    if ((pYourItemObj->Type >= ITEM_HELPER && pYourItemObj->Type <= ITEM_DARK_HORSE_ITEM) ||
        (pYourItemObj->Type == ITEM_JEWEL_OF_BLESS || pYourItemObj->Type == ITEM_JEWEL_OF_SOUL ||
         pYourItemObj->Type == ITEM_JEWEL_OF_LIFE) ||
        (pYourItemObj->Type >= ITEM_JEWEL_OF_GUARDIAN) || (isCompiledGem(pYourItemObj)) ||
        (pYourItemObj->Type >= ITEM_WING && pYourItemObj->Type <= ITEM_WINGS_OF_DARKNESS) ||
        (pYourItemObj->Type >= ITEM_CAPE_OF_LORD) ||
        (pYourItemObj->Type >= ITEM_WING_OF_STORM &&
         pYourItemObj->Type <= ITEM_WING_OF_DIMENSION) ||
        (pYourItemObj->Type == ITEM_JEWEL_OF_CHAOS) ||
        (pYourItemObj->Type >= ITEM_CAPE_OF_FIGHTER &&
         pYourItemObj->Type <= ITEM_CAPE_OF_OVERRULE) ||
        ((pYourItemObj->Level > 4 && pYourItemObj->Type < ITEM_WING) ||
         pYourItemObj->ExcellentFlags > 0))
    {
        int nCompareValue;
        bool bSameItem = false;

        for (int i = 0; i < MAX_TRADE_INVEN; ++i)
        {
            if (-1 == m_aYourInvenBackUp[i].Type)
                continue;

            nCompareValue = ::CompareItem(m_aYourInvenBackUp[i], *pYourItemObj);
            if (0 == nCompareValue)
            {
                bSameItem = true;
                break;
            }
            else if (-1 == nCompareValue)
            {
                bSameItem = true;
                m_aYourInvenBackUp[i] = *pYourItemObj;
                break;
            }
            else if (2 != nCompareValue)
            {
                bSameItem = true;
            }
        }

        if (!bSameItem)
        {
            for (int i = 0; i < MAX_TRADE_INVEN; ++i)
            {
                if (-1 == m_aYourInvenBackUp[i].Type)
                {
                    m_aYourInvenBackUp[i] = *pYourItemObj;
                    break;
                }
            }
        }
    }
}

void CNewUITrade::AlertYourTradeInven()
{
    int nCount = 0;
    int nCompareItemType[10];

    m_bTradeAlert = false;

    int nYourItems = m_pYourInvenCtrl->GetNumberOfItems();
    ITEM *pYourItemObj;
    int nCompareValue;

    for (int i = 0; i < nYourItems; ++i)
    {
        pYourItemObj = m_pYourInvenCtrl->GetItem(i);
        for (int j = 0; j < MAX_TRADE_INVEN; ++j)
        {
            if (m_aYourInvenBackUp[j].Type == pYourItemObj->Type)
            {
                nCompareValue = ::CompareItem(m_aYourInvenBackUp[j], *pYourItemObj);
                if (1 == nCompareValue)
                {
                    m_bTradeAlert = true;
                    pYourItemObj->byColorState = ITEM_COLOR_TRADE_WARNING;
                }
                else
                {
                    if (0 == nCompareValue)
                        nCompareItemType[nCount++] = m_aYourInvenBackUp[j].Type;

                    pYourItemObj->byColorState = ITEM_COLOR_NORMAL;
                    break;
                }
            }
        }
    }

    if (nCount > 0)
    {
        m_bTradeAlert = false;
        for (int i = 0; i < nCount; ++i)
        {
            for (int j = 0; j < nYourItems; ++j)
            {
                pYourItemObj = m_pYourInvenCtrl->GetItem(j);
                if (nCompareItemType[i] == pYourItemObj->Type)
                    pYourItemObj->byColorState = ITEM_COLOR_NORMAL;
            }
        }
    }
}

void CNewUITrade::ProcessToReceiveMyTradeGold(BYTE bySuccess)
{
    m_nMyTradeGold = bySuccess ? m_nTempMyTradeGold : 0;
}

void CNewUITrade::ProcessToReceiveYourConfirm(BYTE byState)
{
    switch (byState)
    {
    case 0:
        m_bYourConfirm = false;
        break;
    case 1:
        m_bYourConfirm = true;
        break;
    case 2:
        m_bMyConfirm = false;
        m_bYourConfirm = false;
        m_nMyTradeWait = 150;
        break;
    case 3:
        break;
    }

    PlayBuffer(SOUND_CLICK01);
}

void CNewUITrade::ProcessToReceiveTradeExit(BYTE byState)
{
    switch (byState)
    {
    case 0: {
        g_pSystemLogBox->AddText(I18N::Game::YourTradeHasBeenCanceled,
                                 SEASON3B::TYPE_ERROR_MESSAGE);

        m_bTradeAlert = false;

        int nYourItems = m_pYourInvenCtrl->GetNumberOfItems();
        for (int i = 0; i < nYourItems; ++i)
            BackUpYourInven(m_pYourInvenCtrl->GetItem(i));
    }
    break;

    case 2:
        g_pSystemLogBox->AddText(I18N::Game::YourTradeHasBeenCanceledBecauseYourInventoryIsFull,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
        break;

    case 3:
        g_pSystemLogBox->AddText(I18N::Game::TradeRequestIsCanceled, SEASON3B::TYPE_ERROR_MESSAGE);
        break;

    case 4:
        g_pSystemLogBox->AddText(I18N::Game::ReinforcedItemCanTBeTraded,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
        break;
    }

    g_pMyInventory->GetInventoryCtrl()->DeletePickedItem();

    g_MessageBox.PopMessageBox();

    g_pNewUISystem->Hide(SEASON3B::INTERFACE_TRADE);
}

int SEASON3B::CNewUITrade::GetPointedItemIndexMyInven()
{
    return m_pMyInvenCtrl->GetPointedSquareIndex();
}

int SEASON3B::CNewUITrade::GetPointedItemIndexYourInven()
{
    return m_pYourInvenCtrl->GetPointedSquareIndex();
}

void CNewUITrade::SyncModernGeometry()
{
    const auto other = m_ModernPanel.GridCell(0), mine = m_ModernPanel.GridCell(1);
    if (other.width > 0 && other.height > 0 && m_pYourInvenCtrl)
        m_pYourInvenCtrl->SetOwnerGeometry(other.x, other.y, other.width, other.height);
    if (mine.width > 0 && mine.height > 0 && m_pMyInvenCtrl)
        m_pMyInvenCtrl->SetOwnerGeometry(mine.x, mine.y, mine.width, mine.height);
}
bool CNewUITrade::IsMouseInModernPanel() const
{
    const auto rect = m_ModernPanel.PanelRect();
    return CheckMouseIn(static_cast<int>(std::floor(rect.x)), static_cast<int>(std::floor(rect.y)),
                        static_cast<int>(std::ceil(rect.width)),
                        static_cast<int>(std::ceil(rect.height)));
}

std::optional<bool> CNewUITrade::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernPanel.ProcessInput(event);
}

CNewUIUnitedMarketPlaceWindow::CNewUIUnitedMarketPlaceWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()), panel_(keeper)
{
}
CNewUIUnitedMarketPlaceWindow::~CNewUIUnitedMarketPlaceWindow()
{
    Release();
}
bool CNewUIUnitedMarketPlaceWindow::Create(CNewUIManager *manager, CNewUI3DRenderMng *, int, int)
{
    if (!manager)
        return false;
    manager_ = manager;
    manager_->AddUIObj(INTERFACE_UNITEDMARKETPLACE_NPC_JULIA, this);
    panel_.CreateS16Caution();
    panel_.SetMode(UI::Modern::RmlMessageBoxMode::OkCancel);
    Show(false);
    return true;
}
void CNewUIUnitedMarketPlaceWindow::Release()
{
    panel_.Release();
    content_ = {};
    if (!manager_)
        return;
    manager_->RemoveUIObj(this);
    manager_ = nullptr;
}

void CNewUIUnitedMarketPlaceWindow::RequestTravel()
{
    if (locked_)
        return;
    sessionKeeper_.WorldUnit()->BeginTransfer();
    SocketClient->ToGameServer()->SendEnterMarketPlaceRequest();
    LockEnterButton(TRUE);
}

bool CNewUIUnitedMarketPlaceWindow::Update()
{
    if (IsVisible())
    {
        if (locale_ != I18N::GetCurrentLocale() || contentWorld_ != gMapManager.ContextMap())
            StageContent();
        if (panel_.CancelButton().IsClick())
            g_pNewUISystem->Hide(INTERFACE_UNITEDMARKETPLACE_NPC_JULIA);
        else if (panel_.OkButton().IsClick())
            RequestTravel();
    }
    panel_.Show(IsVisible());
    return true;
}
bool CNewUIUnitedMarketPlaceWindow::UpdateMouseEvent()
{
    return !IsVisible();
}
bool CNewUIUnitedMarketPlaceWindow::UpdateKeyEvent()
{
    if (!IsVisible() || !IsPress(VK_ESCAPE))
        return true;
    g_pNewUISystem->Hide(INTERFACE_UNITEDMARKETPLACE_NPC_JULIA);
    PlayBuffer(SOUND_CLICK01);
    return false;
}

bool CNewUIUnitedMarketPlaceWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessInput(event);
}
void CNewUIUnitedMarketPlaceWindow::OpeningProcess()
{
    LockEnterButton(FALSE);
    panel_.OkButton().Reset();
    panel_.CancelButton().Reset();
    StageContent();
    panel_.Show(true);
}
void CNewUIUnitedMarketPlaceWindow::ClosingProcess()
{
    panel_.Show(false);
    SocketClient->ToGameServer()->SendCloseNpcRequest();
}
float CNewUIUnitedMarketPlaceWindow::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dialog;
}
void CNewUIUnitedMarketPlaceWindow::SetRemainTime(int time)
{
    if (time != 0)
        LockEnterButton(TRUE);
}
void CNewUIUnitedMarketPlaceWindow::LockEnterButton(BOOL lock)
{
    locked_ = lock != FALSE;
    panel_.OkButton().SetEnable(!locked_);
}

namespace
{
using INTBYTEPAIR = std::pair<int, BYTE>;

}

SEASON3B::CNewUIInventoryCtrl *SessionUiUnit::GetInventoryCtrl() const
{
    return (g_pMyInventory != nullptr) ? g_pMyInventory->GetInventoryCtrl() : nullptr;
}

void SessionUiUnit::ResetWantedList()
{
    unmixGemList_->Clear();
}

bool SessionUiUnit::FindWantedList()
{
    bool foundAny = false;
    ResetWantedList();

    for (int slot = MAX_EQUIPMENT_INDEX; slot < MAX_MY_INVENTORY_EX_INDEX; ++slot)
    {
        const ITEM *pItem = FindInventoryItemBySlot(slot);
        if (!pItem)
        {
            continue;
        }

        if (isCompiledGem(pItem))
        {
            INTBYTEPAIR p;
            p.first = slot;
            p.second = pItem->Level;
            unmixGemList_->AddText(p.first, p.second);
            foundAny = true;
        }
    }
    return foundAny;
}

void SessionUiUnit::SelectFromList(int iIndex, int iLevel)
{
    iUnMixIndex = iIndex;
    iUnMixLevel = iLevel;

    if (CheckInv())
    {
    }
}

void SessionUiUnit::MoveUnMixList()
{
    g_dwActiveUIID = unmixGemList_->GetUIID();
    unmixGemList_->DoAction();
    g_dwActiveUIID = 0;
}

bool SessionUiUnit::CheckInv()
{
    if (!CheckMyInvValid())
    {
        switch (GetError())
        {
        case COMGEM::COMERROR_NOTALLOWED:
            g_pSystemLogBox->AddText(I18N::Game::ItemsForCombinationSystemIsLacking,
                                     SEASON3B::TYPE_ERROR_MESSAGE);
            break;
        case COMGEM::DEERROR_NOTALLOWED:
            g_pSystemLogBox->AddText(I18N::Game::CanTBeDismantled, SEASON3B::TYPE_ERROR_MESSAGE);
            break;
        }
        GetBack();
        return false;
    }

    return true;
}

// OMF-00735
// OMF-00736
// OMF-00737
// OMF-00738
// OMF-00740
// OMF-00741
// OMF-00742
// OMF-00743
// OMF-00744
// OMF-00746
// OMF-00747
// OMF-00745
// OMF-00748
// OMF-00749
// OMF-00750
// OMF-00751
// OMF-00752
// OMF-00753
// OMF-00754
// OMF-00755
// OMF-00756
// OMF-00757
// OMF-00758
// OMF-00759
// OMF-00760
// OMF-00761
// OMF-00762
// OMF-00763
// OMF-00764
// OMF-00765
// OMF-00766
// OMF-00767
// OMF-00768

void RenderColor(float x, float y, float Width, float Height, float Alpha, int Flag);

extern void MoveCharacter(CHARACTER *c, OBJECT *o);

/*
void CChatRoomSocketList::ProcessSocketMessage(DWORD dwSocketID, WORD wMessage)
{
    CHATROOM_SOCKET * pChatroomSocket = GetChatRoomSocketData(GetChatRoomSocketID(dwSocketID));
    if (pChatroomSocket == NULL) return;
    Connection* pSocketClient = &pChatroomSocket->m_WSClient;

    if (pSocketClient == NULL)
    {
        return;
    }
    switch(wMessage)
    {
    case FD_CONNECT:
        break;
    case FD_READ :
        // pSocketClient->nRecv();
        break;
    case FD_WRITE :
        // pSocketClient->FDWriteSend();
        break;
    case FD_CLOSE :
        CUIChatWindow * pWindow = (CUIChatWindow *)g_pWindowMgr->GetWindow(pChatroomSocket->m_dwWindowUIID);
        if (pWindow != NULL)
            pWindow->AddChatText(255, I18N::Game::YouAreDisconnectedFromTheServer, 1, 0);
        pSocketClient->Close();
        break;
    }
}

//void CChatRoomSocketList::ProtocolCompile()
//{
//	// TODO: Change that
//	for (m_ChatRoomSocketMapIter = m_ChatRoomSocketMap.begin(); m_ChatRoomSocketMapIter != m_ChatRoomSocketMap.end(); ++m_ChatRoomSocketMapIter)
//	{
//		ProtocolCompiler(&m_ChatRoomSocketMapIter->second->m_WSClient, 1, m_ChatRoomSocketMapIter->second->m_dwWindowUIID);
//	}
//}
*/

using InventoryPanel = UI::Modern::PC::Inventory::RmlInventoryPanel;

CNewUIMyInventory::CNewUIMyInventory(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), g_petProcess(keeper.PetProcessObject()),
      g_Direction(keeper.DirectionObject()), g_CMonkSystem(keeper.MonkSystemObject()),
      gameplay_(GameplayForConstruction()), renderUnit_(RendererForConstruction()),
      CollisionPosition(keeper.CollisionPosition()), terrainSelectX_(keeper.TerrainSelectX()),
      terrainSelectY_(keeper.TerrainSelectY()), m_ActionController(keeper), m_ModernPanel(keeper)
{
    m_pNewUIMng = nullptr;
    m_pNewUI3DRenderMng = nullptr;
    m_pNewInventoryCtrl = nullptr;
    m_Pos.x = m_Pos.y = 0;

    memset(&m_EquipmentSlots, 0, sizeof(EQUIPMENT_ITEM) * MAX_EQUIPMENT_INDEX);
    m_iPointedSlot = -1;

    m_MyShopMode = MYSHOP_MODE_OPEN;
    m_RepairMode = REPAIR_MODE_OFF;
    m_dwStandbyItemKey = 0;

    m_bRepairEnableLevel = false;
    m_bMyShopOpen = false;
}

CNewUIMyInventory::~CNewUIMyInventory()
{
    Release();
}

bool CNewUIMyInventory::Create(CNewUIManager *pNewUIMng, CNewUI3DRenderMng *pNewUI3DRenderMng,
                               int x, int y)
{
    if (nullptr == pNewUIMng || nullptr == pNewUI3DRenderMng || nullptr == g_pNewItemMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(INTERFACE_INVENTORY, this);

    m_pNewUI3DRenderMng = pNewUI3DRenderMng;
    m_pNewUI3DRenderMng->Add3DRenderObj(this, INVENTORY_CAMERA_Z_ORDER);

    m_pNewInventoryCtrl = new CNewUIInventoryCtrl(sessionKeeper_);
    if (false ==
        m_pNewInventoryCtrl->Create(GameDataForConstruction().Inventory(InventoryRole::Player),
                                    m_pNewUI3DRenderMng, this, 0, 0))
    {
        SAFE_DELETE(m_pNewInventoryCtrl);
        return false;
    }

    m_ActionController.SetContext(this);

    SetPos(x, y);
    LoadImages();
    SetEquipmentSlotInfo();
    m_pNewInventoryCtrl->SetRenderSlotFrame(false);
    m_pNewInventoryCtrl->SetOwnerRendered(true);
    Show(false);
    return true;
}

void CNewUIMyInventory::Release()
{
    m_ModernPanel.Release();
    m_ModernVisible = false;
    m_ModernContent = {};
    if (m_pNewUI3DRenderMng)
        m_pNewUI3DRenderMng->DeleteUI2DEffectObject(UI2DEffectCallback);

    UnloadImages();

    SAFE_DELETE(m_pNewInventoryCtrl);

    if (m_pNewUI3DRenderMng)
    {
        m_pNewUI3DRenderMng->Remove3DRenderObj(this);
        m_pNewUI3DRenderMng = nullptr;
    }
    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }
}

bool CNewUIMyInventory::EquipItem(int iIndex, std::span<const BYTE> pbyItemPacket)
{
    return gameplay_.EquipItem(iIndex, pbyItemPacket);
}

void CNewUIMyInventory::UnequipItem(int iIndex)
{
    gameplay_.UnequipItem(iIndex);
}

void CNewUIMyInventory::UnequipAllItems()
{
    gameplay_.UnequipAllItems();
}

bool CNewUIMyInventory::IsEquipable(int iIndex, ITEM *pItem) const
{
    return gameplay_.IsEquipable(iIndex, pItem);
}

bool CNewUIMyInventory::InsertItem(int iIndex, std::span<const BYTE> pbyItemPacket) const
{
    return sessionKeeper_.GameData()->InsertInventoryItem(iIndex, pbyItemPacket);
}

void CNewUIMyInventory::DeleteItem(int iIndex) const
{
    sessionKeeper_.GameData()->DeleteInventoryItem(iIndex);
}

void CNewUIMyInventory::DeleteAllItems() const
{
    if (m_pNewInventoryCtrl)
        m_pNewInventoryCtrl->RemoveAllItems();
}

const POINT &CNewUIMyInventory::GetPos() const
{
    return m_Pos;
}

SEASON3B::REPAIR_MODE CNewUIMyInventory::GetRepairMode() const
{
    return m_RepairMode;
}

void CNewUIMyInventory::SetRepairMode(bool bRepair)
{
    if (bRepair)
    {
        m_RepairMode = REPAIR_MODE_ON;
        if (m_pNewInventoryCtrl)
        {
            m_pNewInventoryCtrl->SetRepairMode(true);
        }
    }
    else
    {
        m_RepairMode = REPAIR_MODE_OFF;
        if (m_pNewInventoryCtrl)
        {
            m_pNewInventoryCtrl->SetRepairMode(false);
        }
    }
}

bool CNewUIMyInventory::UpdateMouseEvent()
{
    SyncModernGeometry();
    m_iPointedSlot = m_ModernPanel.HoveredEquipment();
    if (m_pNewInventoryCtrl && !m_pNewInventoryCtrl->UpdateMouseEvent())
        return false;

    if (true == EquipmentWindowProcess())
        return false;
    if (true == InventoryProcess())
        return false;

    if (true == BtnProcess())
        return false;

    if (HandleWorldItemDrop())
        return false;

    g_csItemOption.SetViewOptionList(false);

    if (m_ModernPanel.Hovered(InventoryPanel::SetOption) == true)
    {
        g_csItemOption.SetViewOptionList(true);
    }

    if (IsMouseInModernPanel())
    {
        if (IsPress(VK_RBUTTON))
        {
            ResetMouseRButton();
            return false;
        }

        if (IsNone(VK_LBUTTON) == false)
        {
            return false;
        }
    }

    return true;
}

bool CNewUIMyInventory::UpdateKeyEvent()
{
    if (!g_pNewUISystem->IsVisible(INTERFACE_INVENTORY))
    {
        return true;
    }

    if (IsPress(VK_ESCAPE) == true)
    {
        if (g_pNPCShop->IsSellingItem() == false)
        {
            g_pNewUISystem->Hide(INTERFACE_INVENTORY);
            PlayBuffer(SOUND_CLICK01);
        }
        return false;
    }

    if (IsPress('L') == true)
    {
        if (m_bRepairEnableLevel == true && g_pNewUISystem->IsVisible(INTERFACE_NPCSHOP) == false &&
            g_pNewUISystem->IsVisible(INTERFACE_MIXINVENTORY) == false &&
            g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_LUCKYITEMWND) == false)
        {
            ToggleRepairMode();

            return false;
        }
    }

    if (CanOpenMyShopInterface() == true && IsPress('S'))
    {
        if (m_bMyShopOpen)
        {
            if (m_MyShopMode == MYSHOP_MODE_OPEN)
            {
                ChangeMyShopButtonStateClose();
            }
            else if (m_MyShopMode == MYSHOP_MODE_CLOSE)
            {
                ChangeMyShopButtonStateOpen();
            }
            g_pNewUISystem->Toggle(INTERFACE_MYSHOP_INVENTORY);
            PlayBuffer(SOUND_CLICK01);
        }
        return false;
    }

    if (IsPress('K'))
    {
        g_pNewUISystem->Toggle(INTERFACE_INVENTORY_EXT);
        PlayBuffer(SOUND_CLICK01);

        return false;
    }

    if (IsMouseInModernPanel() == false)
    {
        return true;
    }

    if (IsRepeat(VK_CONTROL))
    {
        int iHotKey = -1;
        if (IsPress('Q'))
        {
            iHotKey = HOTKEY_Q;
        }
        else if (IsPress('W'))
        {
            iHotKey = HOTKEY_W;
        }
        else if (IsPress('E'))
        {
            iHotKey = HOTKEY_E;
        }
        else if (IsPress('R'))
        {
            iHotKey = HOTKEY_R;
        }

        if (iHotKey != -1)
        {
            const ITEM *pItem = m_pNewInventoryCtrl->FindItemAtPt(MouseX, MouseY);
            if (pItem == nullptr)
            {
                return false;
            }

            if (CanRegisterItemHotKey(pItem->Type) == true)
            {
                const int iItemLevel = pItem->Level;
                g_pMainFrame->SetItemHotKey(iHotKey, pItem->Type, iItemLevel);
                return false;
            }
        }
    }

    return true;
}

bool CNewUIMyInventory::Update()
{
    if (m_ModernPanel.TakeFocus() && m_pNewUIMng)
        m_pNewUIMng->BringToFront(this);
    (void)BtnProcess();
    m_ModernVisible = IsVisible();
    if (m_ModernVisible)
        SyncModernGeometry();
    if (m_pNewInventoryCtrl && !m_pNewInventoryCtrl->Update())
        return false;
    if (m_ModernVisible)
    {
        m_iPointedSlot = m_ModernPanel.HoveredEquipment();
        UpdateEquippedPetInfo();
        StageModernContent();
    }
    else
        m_iPointedSlot = -1;
    return true;
}

void CNewUIMyInventory::UpdateEquippedPetInfo()
{
    if (m_RepairMode != REPAIR_MODE_OFF || m_iPointedSlot < 0)
        return;
    auto &item = CharacterMachine->Equipment[m_iPointedSlot];
    const bool horse = item.Type == ITEM_DARK_HORSE_ITEM && m_iPointedSlot == EQUIPMENT_HELPER;
    const bool raven = item.Type == ITEM_DARK_RAVEN_ITEM && m_iPointedSlot == EQUIPMENT_WEAPON_LEFT;
    if (!horse && !raven)
        return;
    const auto &slot = m_EquipmentSlots[m_iPointedSlot];
    sessionKeeper_.Ui()->RequestPetInfo(slot.x + slot.width / 2, slot.y + slot.height / 2, &item,
                                        true);
}

bool CNewUIMyInventory::IsVisible() const
{
    return CNewUIObj::IsVisible();
}

void CNewUIMyInventory::OpenningProcess()
{
    SetRepairMode(false);

    m_MyShopMode = MYSHOP_MODE_OPEN;
    ChangeMyShopButtonStateOpen();

    const WORD wLevel = CharacterAttribute->Level;

    if (wLevel >= 50)
    {
        m_bRepairEnableLevel = true;
    }
    else
    {
        m_bRepairEnableLevel = false;
    }

    if (wLevel >= 6)
    {
        m_bMyShopOpen = true;
    }
    else
    {
        m_bMyShopOpen = false;
    }

    if (g_QuestMng.IsIndexInCurQuestIndexList(0x1000F))
    {
        if (g_QuestMng.IsEPRequestRewardState(0x1000F))
        {
            SocketClient->ToGameServer()->SendQuestClientActionRequest(1, 0x0F);
            g_QuestMng.SetEPRequestRewardState(0x1000F, false);
        }
    }
}

void CNewUIMyInventory::ClosingProcess()
{
    m_pNewInventoryCtrl->BackupPickedItem();
    RepairEnable = 0;
    SetRepairMode(false);
}

float CNewUIMyInventory::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

CNewUIInventoryCtrl *CNewUIMyInventory::GetInventoryCtrl() const
{
    return m_pNewInventoryCtrl;
}

ITEM *CNewUIMyInventory::FindItem(int iLinealPos) const
{
    if (m_pNewInventoryCtrl)
        return m_pNewInventoryCtrl->FindItem(iLinealPos);
    return nullptr;
}

ITEM *CNewUIMyInventory::FindItemByKey(DWORD dwKey) const
{
    if (m_pNewInventoryCtrl)
        return m_pNewInventoryCtrl->FindItemByKey(dwKey);
    return nullptr;
}

int CNewUIMyInventory::FindItemIndex(short int siType, int iLevel) const
{
    if (m_pNewInventoryCtrl)
        return m_pNewInventoryCtrl->FindItemIndex(siType, iLevel);
    return -1;
}

int CNewUIMyInventory::FindItemReverseIndex(short sType, int iLevel) const
{
    if (m_pNewInventoryCtrl)
    {
        return m_pNewInventoryCtrl->FindItemReverseIndex(sType, iLevel);
    }

    return -1;
}

int CNewUIMyInventory::FindEmptySlot(IN int cx, IN int cy) const
{
    if (m_pNewInventoryCtrl)
        return m_pNewInventoryCtrl->FindEmptySlot(cx, cy);
    return -1;
}

int CNewUIMyInventory::FindEmptySlot(ITEM *pItem) const
{
    if (pItem == nullptr)
    {
        return -1;
    }

    const ITEM_ATTRIBUTE *pItemAttr = &ItemAttribute[pItem->Type];
    if (m_pNewInventoryCtrl)
    {
        return m_pNewInventoryCtrl->FindEmptySlot(pItemAttr->Width, pItemAttr->Height);
    }

    return -1;
}

int CNewUIMyInventory::FindEmptySlotIncludingExtensions(IN int cx, IN int cy) const
{
    const int baseInventorySlot = FindEmptySlot(cx, cy);
    if (baseInventorySlot != -1)
    {
        return baseInventorySlot;
    }

    if (g_pMyInventoryExt != nullptr)
    {
        return g_pMyInventoryExt->FindEmptySlot(cx, cy);
    }

    return -1;
}

int CNewUIMyInventory::FindEmptySlotIncludingExtensions(ITEM *pItem) const
{
    if (pItem == nullptr)
    {
        return -1;
    }

    const ITEM_ATTRIBUTE *pItemAttr = &ItemAttribute[pItem->Type];
    return FindEmptySlotIncludingExtensions(pItemAttr->Width, pItemAttr->Height);
}

void CNewUIMyInventory::SetStandbyItemKey(DWORD dwItemKey)
{
    m_dwStandbyItemKey = dwItemKey;
}

DWORD CNewUIMyInventory::GetStandbyItemKey() const
{
    return m_dwStandbyItemKey;
}

int CNewUIMyInventory::GetStandbyItemIndex() const
{
    if (ITEM *pItem = GetStandbyItem())
    {
        return m_pNewInventoryCtrl->GetIndexByItem(pItem);
    }
    return -1;
}

ITEM *CNewUIMyInventory::GetStandbyItem() const
{
    if (m_pNewInventoryCtrl)
        return m_pNewInventoryCtrl->FindItemByKey(m_dwStandbyItemKey);
    return nullptr;
}

void CNewUIMyInventory::CreateEquippingEffect(ITEM *pItem)
{
    gameplay_.CreateEquippingEffect(pItem);
}

void CNewUIMyInventory::DeleteEquippingEffectBug(ITEM *pItem)
{
    gameplay_.DeleteEquippingEffectBug(pItem);
}

void CNewUIMyInventory::DeleteEquippingEffect()
{
    gameplay_.DeleteEquippingEffect();
}

void CNewUIMyInventory::SetEquipmentSlotInfo()
{
    SyncModernGeometry();
}

bool CNewUIMyInventory::EquipmentWindowProcess()
{
    if (m_iPointedSlot != -1 && IsRelease(VK_LBUTTON))
    {
        if (CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem())
        {
            ITEM *pItemObj = pPickedItem->GetItem();
            const int iSourceIndex = pPickedItem->GetSourceLinealPos();
            const int iTargetIndex = m_iPointedSlot;
            if (pItemObj->bPeriodItem && pItemObj->bExpiredPeriod)
            {
                g_pSystemLogBox->AddText(I18N::Game::CanTWearItem, SEASON3B::TYPE_ERROR_MESSAGE);
                g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();

                ResetMouseLButton();
                return false;
            }

            ITEM *pEquipmentItemSlot = &CharacterMachine->Equipment[iTargetIndex];
            if (pEquipmentItemSlot && pEquipmentItemSlot->Type != -1)
            {
                return true;
            }

            if (g_ChangeRingMgr->CheckChangeRing(pPickedItem->GetItem()->Type))
            {
                ITEM *pItemRingLeft = &CharacterMachine->Equipment[EQUIPMENT_RING_LEFT];
                ITEM *pItemRingRight = &CharacterMachine->Equipment[EQUIPMENT_RING_RIGHT];

                if (g_ChangeRingMgr->CheckChangeRing(pItemRingLeft->Type) ||
                    g_ChangeRingMgr->CheckChangeRing(pItemRingRight->Type))
                {
                    g_pSystemLogBox->AddText(I18N::Game::CanTWearItem, TYPE_ERROR_MESSAGE);
                    g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();

                    ResetMouseLButton();
                    return false;
                }
            }

            if (IsEquipable(iTargetIndex, pItemObj))
            {
                const STORAGE_TYPE sourceType = pPickedItem->GetSourceStorageType();

                if (sourceType == STORAGE_TYPE::INVENTORY && iSourceIndex == iTargetIndex)
                {
                    g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();
                }
                else
                {
                    SendRequestEquipmentItem(sourceType, iSourceIndex, pItemObj,
                                             STORAGE_TYPE::INVENTORY, iTargetIndex);
                    return true;
                }
            }
        }
        else // pPickedItem == NULL
        {
            if (GetRepairMode() == REPAIR_MODE_ON)
            {
                ITEM *pEquippedItem = &CharacterMachine->Equipment[m_iPointedSlot];

                if (pEquippedItem == NULL)
                {
                    return true;
                }

                if (IsRepairBan(pEquippedItem) == true)
                {
                    return true;
                }

                if (g_pNewUISystem->IsVisible(INTERFACE_NPCSHOP) && g_pNPCShop->IsRepairShop())
                {
                    SocketClient->ToGameServer()->SendRepairItemRequest(m_iPointedSlot, 0);
                }
                else if (m_bRepairEnableLevel == true)
                {
                    SocketClient->ToGameServer()->SendRepairItemRequest(m_iPointedSlot, 1);
                }

                return true;
            }

            ITEM *pEquippedItem = &CharacterMachine->Equipment[m_iPointedSlot];
            if (pEquippedItem->Type >= 0)
            {
                if (gMapManager.ContextMap() == WD_10HEAVEN)
                {
                    const ITEM *pEquippedPetItem = &CharacterMachine->Equipment[EQUIPMENT_HELPER];
                    bool bPicked = true;

                    if (m_iPointedSlot == EQUIPMENT_HELPER || m_iPointedSlot == EQUIPMENT_WING)
                    {
                        if (((m_iPointedSlot == EQUIPMENT_HELPER) && !gameplay_.IsEquipedWing()))
                        {
                            bPicked = false;
                        }
                        else if (((m_iPointedSlot == EQUIPMENT_WING) &&
                                  !((pEquippedPetItem->Type == ITEM_HORN_OF_DINORANT) ||
                                    (pEquippedPetItem->Type == ITEM_DARK_HORSE_ITEM) ||
                                    (pEquippedPetItem->Type == ITEM_HORN_OF_FENRIR))))
                        {
                            bPicked = false;
                        }
                    }

                    if (bPicked == true)
                    {
                        if (g_pMyInventory->GetInventoryCtrl()->CreatePickedItem(nullptr,
                                                                                 pEquippedItem))
                        {
                            UnequipItem(m_iPointedSlot);
                        }
                    }
                }
                else
                {
                    if (g_pMyInventory->GetInventoryCtrl()->CreatePickedItem(nullptr,
                                                                             pEquippedItem))
                    {
                        UnequipItem(m_iPointedSlot);
                    }
                }
            }
        }
    }

    if (IsRelease(VK_RBUTTON))
    {
        const CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();

        const int iSourceIndex = m_iPointedSlot;
        if (GetRepairMode() != REPAIR_MODE_ON && EquipmentItem == false && pPickedItem == nullptr &&
            iSourceIndex != -1 &&
            !g_pNewUISystem->IsVisible(INTERFACE_NPCSHOP)) // Don't unequip when NPC shop is open
        {
            ResetMouseRButton();

            ITEM *pEquippedItem = &CharacterMachine->Equipment[iSourceIndex];

            if (pEquippedItem->Type >= 0)
            {
                const int emptySlotIndex = FindEmptySlot(pEquippedItem);

                if (emptySlotIndex != -1)
                {
                    // This code looks tricky... it simulates a pick up and click on the inventory slot.
                    // God knows what happens, when this request to the server goes wrong.
                    if (g_pMyInventory->GetInventoryCtrl()->CreatePickedItem(nullptr,
                                                                             pEquippedItem))
                    {
                        CNewUIPickedItem *pPickedItem =
                            g_pMyInventory->GetInventoryCtrl()->GetPickedItem();
                        UnequipItem(iSourceIndex);
                        pPickedItem->HidePickedItem();
                    }

                    SendRequestEquipmentItem(STORAGE_TYPE::INVENTORY, iSourceIndex, pEquippedItem,
                                             STORAGE_TYPE::INVENTORY, emptySlotIndex);
                    return true;
                }
            }
        }
    }

    return false;
}
bool CNewUIMyInventory::InventoryProcess() const
{
    if (IsMouseInModernPanel() == false)
    {
        return false;
    }

    if (m_pNewInventoryCtrl == nullptr)
    {
        return false;
    }

    return m_ActionController.HandleInventoryActions(m_pNewInventoryCtrl);
}

bool CNewUIMyInventory::BtnProcess()
{
    if (m_ModernPanel.TakeClick(InventoryPanel::Close))
    {
        if (g_pNewUISystem->IsVisible(INTERFACE_MYSHOP_INVENTORY))
        {
            g_pNewUISystem->Hide(INTERFACE_MYSHOP_INVENTORY);
        }
        g_pNewUISystem->Hide(INTERFACE_INVENTORY);
        return true;
    }

    if (m_ModernPanel.TakeClick(InventoryPanel::Expand))
    {
        g_pNewUISystem->Toggle(INTERFACE_INVENTORY_EXT);
        return true;
    }

    if (g_pNewUISystem->IsVisible(INTERFACE_NPCSHOP) == false &&
        g_pNewUISystem->IsVisible(INTERFACE_TRADE) == false &&
        g_pNewUISystem->IsVisible(INTERFACE_DEVILSQUARE) == false &&
        g_pNewUISystem->IsVisible(INTERFACE_BLOODCASTLE) == false &&
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_LUCKYITEMWND) == false &&
        g_pNewUISystem->IsVisible(INTERFACE_MIXINVENTORY) == false &&
        g_pNewUISystem->IsVisible(INTERFACE_STORAGE) == false)
    {
        if (m_bRepairEnableLevel == true && m_ModernPanel.TakeClick(InventoryPanel::Repair) == true)
        {
            ToggleRepairMode();
            return true;
        }

        if (m_bMyShopOpen == true && !m_ShopButtonLocked &&
            m_ModernPanel.TakeClick(InventoryPanel::Store) == true)
        {
            if (m_MyShopMode == MYSHOP_MODE_OPEN)
            {
                ChangeMyShopButtonStateClose();
                g_pNewUISystem->Show(INTERFACE_MYSHOP_INVENTORY);
            }
            else if (m_MyShopMode == MYSHOP_MODE_CLOSE)
            {
                ChangeMyShopButtonStateOpen();
                g_pNewUISystem->Hide(INTERFACE_MYSHOP_INVENTORY);
                g_pNewUISystem->Hide(INTERFACE_PURCHASESHOP_INVENTORY);
            }

            return true;
        }
    }

    return false;
}

bool CNewUIMyInventory::CanRegisterItemHotKey(int iType)
{
    switch (iType)
    {
    case ITEM_APPLE:
    case ITEM_SMALL_HEALING_POTION:
    case ITEM_MEDIUM_HEALING_POTION:
    case ITEM_LARGE_HEALING_POTION:
    case ITEM_SMALL_MANA_POTION:
    case ITEM_MEDIUM_MANA_POTION:
    case ITEM_LARGE_MANA_POTION:
    case ITEM_SIEGE_POTION:
    case ITEM_ANTIDOTE:
    case ITEM_ALE:
    case ITEM_TOWN_PORTAL_SCROLL:
    case ITEM_POTION + 20:
    case ITEM_SMALL_SHIELD_POTION:
    case ITEM_MEDIUM_SHIELD_POTION:
    case ITEM_LARGE_SHIELD_POTION:
    case ITEM_SMALL_COMPLEX_POTION:
    case ITEM_MEDIUM_COMPLEX_POTION:
    case ITEM_LARGE_COMPLEX_POTION:
    case ITEM_JACK_OLANTERN_BLESSINGS:
    case ITEM_JACK_OLANTERN_WRATH:
    case ITEM_JACK_OLANTERN_CRY:
    case ITEM_JACK_OLANTERN_FOOD:
    case ITEM_JACK_OLANTERN_DRINK:
    case ITEM_POTION + 70:
    case ITEM_POTION + 71:
    case ITEM_POTION + 78:
    case ITEM_POTION + 79:
    case ITEM_POTION + 80:
    case ITEM_POTION + 81:
    case ITEM_POTION + 82:
    case ITEM_POTION + 94:
    case ITEM_CHERRY_BLOSSOM_WINE:
    case ITEM_CHERRY_BLOSSOM_RICE_CAKE:
    case ITEM_CHERRY_BLOSSOM_FLOWER_PETAL:
    case ITEM_POTION + 133:
        return true;
    }

    return false;
}

bool CNewUIMyInventory::HandleInventoryActions(CNewUIInventoryCtrl *targetControl)
{
    if (g_pMyInventory)
    {
        return g_pMyInventory->m_ActionController.HandleInventoryActions(targetControl);
    }
    return false;
}

bool CNewUIMyInventory::CanOpenMyShopInterface()
{
    if (g_pNewUISystem->IsVisible(INTERFACE_NPCSHOP) ||
        g_pNewUISystem->IsVisible(INTERFACE_STORAGE) ||
        g_pNewUISystem->IsVisible(INTERFACE_MIXINVENTORY) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_LUCKYITEMWND) ||
        g_pNewUISystem->IsVisible(INTERFACE_TRADE) || gMapManager.IsCursedTemple())
    {
        return false;
    }
    return true;
}

bool CNewUIMyInventory::IsRepairEnableLevel() const
{
    return m_bRepairEnableLevel;
}

void CNewUIMyInventory::SetRepairEnableLevel(bool bOver)
{
    m_bRepairEnableLevel = bOver;
}

void CNewUIMyInventory::ChangeMyShopButtonStateOpen()
{
    m_MyShopMode = MYSHOP_MODE_OPEN;
}

void CNewUIMyInventory::ChangeMyShopButtonStateClose()
{
    m_MyShopMode = MYSHOP_MODE_CLOSE;
}

void CNewUIMyInventory::LockMyShopButtonOpen()
{
    m_ShopButtonLocked = true;
}

void CNewUIMyInventory::UnlockMyShopButtonOpen()
{
    m_ShopButtonLocked = false;
}

void CNewUIMyInventory::ToggleRepairMode()
{
    if (m_RepairMode == REPAIR_MODE_OFF)
    {
        SetRepairMode(true);
    }
    else if (m_RepairMode == REPAIR_MODE_ON)
    {
        SetRepairMode(false);
    }
}

bool CNewUIMyInventory::IsItem(short int siType, bool bcheckPick) const
{
    return sessionKeeper_.GameData()->HasInventoryItem(siType, bcheckPick);
}

int CNewUIMyInventory::GetNumItemByKey(DWORD dwItemKey) const
{
    return m_pNewInventoryCtrl->GetNumItemByKey(dwItemKey);
}

int CNewUIMyInventory::GetNumItemByType(short sItemType) const
{
    return m_pNewInventoryCtrl->GetNumItemByType(sItemType);
}

BYTE CNewUIMyInventory::GetDurabilityPointedItem() const
{
    const ITEM *pItem = nullptr;

    if (m_iPointedSlot != -1)
    {
        pItem = &CharacterMachine->Equipment[m_iPointedSlot];
        const BYTE byDurability = pItem->Durability;

        return byDurability;
    }

    pItem = m_pNewInventoryCtrl->FindItemPointedSquareIndex();
    if (pItem != nullptr)
    {
        const BYTE byDurability = pItem->Durability;
        return byDurability;
    }

    return 0;
}

int CNewUIMyInventory::GetPointedItemIndex() const
{
    if (m_iPointedSlot != -1)
    {
        return m_iPointedSlot;
    }

    return m_pNewInventoryCtrl->GetPointedSquareIndex();
}

int CNewUIMyInventory::FindManaItemIndex() const
{
    for (int i = ITEM_LARGE_MANA_POTION; i >= ITEM_SMALL_MANA_POTION; i--)
    {
        const int iIndex = FindItemReverseIndex(i);
        if (iIndex != -1)
        {
            return iIndex;
        }
    }

    return -1;
}

int CNewUIMyInventory::FindHealingItemIndex() const
{
    for (int i = ITEM_LARGE_HEALING_POTION; i >= ITEM_APPLE; i--)
    {
        const int iIndex = FindItemReverseIndex(i);
        if (iIndex != -1)
        {
            return iIndex;
        }
    }

    return -1;
}

void CNewUIMyInventory::ResetMouseLButton()
{
    MouseLButton = false;
    MouseLButtonPop = false;
    MouseLButtonPush = false;
}

void CNewUIMyInventory::ResetMouseRButton()
{
    MouseRButton = false;
    MouseRButtonPop = false;
    MouseRButtonPush = false;
}

#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
BOOL SEASON3B::CNewUIMyInventory::IsInvenItem(const short sType)
{
    BOOL bInvenItem = FALSE;

    if (FALSE
#ifdef LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM
        || (sType == ITEM_HELPER + 128 || sType == ITEM_HELPER + 129 || sType == ITEM_HELPER + 134)
#endif //LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM
#ifdef LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM_PART_2
        || (sType >= ITEM_HELPER + 130 && sType <= ITEM_HELPER + 133)
#endif //LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM_PART_2
    )
        bInvenItem = TRUE;

    return bInvenItem;
}
#endif //LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY

void CNewUIMyInventory::SyncModernGeometry()
{
    if (!m_pNewInventoryCtrl)
        return;
    const auto cell = m_ModernPanel.GridCell();
    if (cell.width <= 0 || cell.height <= 0)
        return;
    m_pNewInventoryCtrl->SetOwnerGeometry(cell.x, cell.y, cell.width, cell.height);
    const auto panel = m_ModernPanel.PanelRect();
    m_Pos = {static_cast<LONG>(std::lround(panel.x)), static_cast<LONG>(std::lround(panel.y))};
    for (std::size_t i = 0; i < MAX_EQUIPMENT_INDEX; ++i)
    {
        const auto rect = m_ModernPanel.EquipmentRect(i);
        m_EquipmentSlots[i] = {rect.x, rect.y, rect.width, rect.height};
    }
}

bool CNewUIMyInventory::IsMouseInModernPanel() const
{
    const auto rect = m_ModernPanel.PanelRect();
    return CheckMouseIn(static_cast<int>(std::floor(rect.x)), static_cast<int>(std::floor(rect.y)),
                        static_cast<int>(std::ceil(rect.width)),
                        static_cast<int>(std::ceil(rect.height)));
}

void CNewUIMyInventory::UpdateEquipmentPresentation()
{
    enum IconFrame
    {
        Normal = 1,
        Half = 3,
        Low = 4,
        Critical = 5,
        Invalid = 7
    };
    m_ModernContent.equipmentFrames.fill(Normal);
    for (int i = 0; i < MAX_EQUIPMENT_INDEX; ++i)
    {
        ITEM &item = CharacterMachine->Equipment[i];
        if (item.Type < 0 || (item.bPeriodItem && !item.bExpiredPeriod))
            continue;
        if ((i == EQUIPMENT_RING_LEFT || i == EQUIPMENT_RING_RIGHT) &&
            ((item.Type == ITEM_WIZARDS_RING && item.Level == 1) || item.Level == 2))
            continue;
        const auto maximum = CalcMaxDurability(&item, &ItemAttribute[item.Type], item.Level);
        auto &frame = m_ModernContent.equipmentFrames[i];
        if (item.Durability <= maximum * 0.2f)
            frame = Critical;
        else if (item.Durability <= maximum * 0.3f)
            frame = Low;
        else if (item.Durability <= maximum * 0.5f)
            frame = Half;
        else if (!IsEquipable(i, &item))
            frame = Invalid;
    }
    if (auto *picked = m_pNewInventoryCtrl->GetPickedItem(); picked && m_iPointedSlot >= 0)
    {
        auto *item = picked->GetItem();
        if (item && (CharacterMachine->Equipment[m_iPointedSlot].Type >= 0 ||
                     !IsEquipable(m_iPointedSlot, item)))
            m_ModernContent.equipmentFrames[m_iPointedSlot] = Invalid;
    }
}

std::optional<bool> CNewUIMyInventory::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernPanel.ProcessInput(event);
}

bool CNewUIMyInventory::HandleWorldItemDrop()
{
    CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();
    if (pPickedItem && IsPress(VK_LBUTTON) && !IsMouseInModernPanel() &&
        !(g_pMyInventoryExt->IsVisible() && g_pMyInventoryExt->IsMouseInModernPanel()) &&
        CheckMouseIn(0, 0, GetScreenWidth(), 429))
    {
        if (g_pNewUISystem->IsVisible(INTERFACE_NPCSHOP) == true ||
            g_pNewUISystem->IsVisible(INTERFACE_TRADE) == true ||
            g_pNewUISystem->IsVisible(INTERFACE_DEVILSQUARE) == true ||
            g_pNewUISystem->IsVisible(INTERFACE_BLOODCASTLE) == true ||
            g_pNewUISystem->IsVisible(INTERFACE_MIXINVENTORY) == true ||
            g_pNewUISystem->IsVisible(INTERFACE_STORAGE) == true ||
            g_pNewUISystem->IsVisible(INTERFACE_MYSHOP_INVENTORY) == true ||
            g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_LUCKYITEMWND) == true ||
            g_pNewUISystem->IsVisible(INTERFACE_PURCHASESHOP_INVENTORY) == true)
        {
            ResetMouseLButton();
            return true;
        }

        ITEM *pItemObj = pPickedItem->GetItem();
        if (pItemObj && pItemObj->Jewel_Of_Harmony_Option != 0)
        {
            g_pSystemLogBox->AddText(I18N::Game::ReinforcedItemCanTBeDropped, TYPE_ERROR_MESSAGE);

            ResetMouseLButton();
            return true;
        }
        if (pItemObj && IsHighValueItem(pItemObj) == true)
        {
            g_pSystemLogBox->AddText(I18N::Game::YouAreNotAllowedToDropThisExpensiveItem,
                                     TYPE_ERROR_MESSAGE);
            g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();

            ResetMouseLButton();
            return true;
        }
        if (pItemObj && IsDropBan(pItemObj))
        {
            g_pSystemLogBox->AddText(I18N::Game::ThisItemCannotBeDropped, TYPE_ERROR_MESSAGE);
            g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();

            ResetMouseLButton();
            return true;
        }
        if (pItemObj && pItemObj->Type == ITEM_LOST_MAP && gMapManager.IsCursedTemple() == true)
        {
            ResetMouseLButton();
            return true;
        }
        RenderTerrain(true);
        if (SelectFlag)
        {
            const int iSourceIndex = pPickedItem->GetSourceLinealPos();
            const int tx = (int)(CollisionPosition[0] / TERRAIN_SCALE);
            const int ty = (int)(CollisionPosition[1] / TERRAIN_SCALE);
            if (pPickedItem->GetOwnerInventory() == m_pNewInventoryCtrl ||
                g_pMyInventoryExt->GetOwnerOf(pPickedItem) != nullptr)
            {
                if (Hero->Dead == 0)
                {
                    SocketClient->ToGameServer()->SendDropItemRequest(tx, ty, iSourceIndex);
                    SendDropItem = iSourceIndex;
                }
            }
            else if (pItemObj && pItemObj->ex_src_type == ITEM_EX_SRC_EQUIPMENT)
            {
                SocketClient->ToGameServer()->SendDropItemRequest(tx, ty, iSourceIndex);
                SendDropItem = iSourceIndex;
            }
            MouseUpdateTime = 0;
            MouseUpdateTimeMax = 6;

            ResetMouseLButton();
            return true;
        }
    }

    return false;
}

SEASON3B::CNewUIInventoryCtrl *SessionRenderUnit::CreateInventoryControl()
{
    return new SEASON3B::CNewUIInventoryCtrl(sessionKeeper_);
}

SEASON3B::CNewUIPickedItem::CNewUIPickedItem(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_pPickedItem(keeper.InventoryStorage().picked.item)
{
    m_pSrcInventory = nullptr;
    m_bShow = true;
    m_Pos.x = m_Pos.y = 0;
    m_Size.cx = m_Size.cy = 0;
}

SEASON3B::CNewUIPickedItem::~CNewUIPickedItem()
{
    Release();
}

bool SEASON3B::CNewUIPickedItem::Create(SessionItemStore *pNewItemMng, CNewUIInventoryCtrl *pSrc,
                                        ITEM *pItem)
{
    if (g_pNewUI3DRenderMng == nullptr || pNewItemMng == nullptr || pItem == nullptr)
        return false;

    m_pSrcInventory = pSrc;
    if (!sessionKeeper_.GameData()->BeginItemMove(pSrc ? &pSrc->Data() : nullptr, pItem))
    {
        return false;
    }

    g_pNewUI3DRenderMng->Add3DRenderObj(this, INFORMATION_CAMERA_Z_ORDER);

    const auto *geometry = pSrc ? pSrc : g_pMyInventory->GetInventoryCtrl();
    const ITEM_ATTRIBUTE *pItemAttr = &ItemAttribute[m_pPickedItem->Type];
    m_Size.cx = static_cast<int>(std::lround(pItemAttr->Width * geometry->PresentedSquareWidth()));
    m_Size.cy =
        static_cast<int>(std::lround(pItemAttr->Height * geometry->PresentedSquareHeight()));
    m_Pos.x = MouseX - m_Size.cx / 2;
    m_Pos.y = MouseY - m_Size.cy / 2;

    return true;
}

void SEASON3B::CNewUIPickedItem::Release()
{
    if (g_pNewUI3DRenderMng)
        g_pNewUI3DRenderMng->Remove3DRenderObj(this);
    m_pSrcInventory = nullptr;
    m_bShow = true;
}

void SEASON3B::CNewUIPickedItem::DetachInventory(const CNewUIInventoryCtrl *inventory) noexcept
{
    if (m_pSrcInventory == inventory)
        m_pSrcInventory = nullptr;
}

CNewUIInventoryCtrl *SEASON3B::CNewUIPickedItem::GetOwnerInventory() const
{
    return m_pSrcInventory;
}

STORAGE_TYPE CNewUIPickedItem::GetSourceStorageType() const
{
    const auto *picked = sessionKeeper_.GameData()->GetPickedItem();
    return picked ? picked->storage : STORAGE_TYPE::UNDEFINED;
}

ITEM *SEASON3B::CNewUIPickedItem::GetItem() const
{
    return m_pPickedItem;
}

const POINT &SEASON3B::CNewUIPickedItem::GetPos() const
{
    return m_Pos;
}

const SIZE &SEASON3B::CNewUIPickedItem::GetSize() const
{
    return m_Size;
}

void SEASON3B::CNewUIPickedItem::GetRect(RECT &rcBox)
{
    rcBox.left = MouseX - m_Size.cx / 2;
    rcBox.top = MouseY - m_Size.cy / 2;
    rcBox.right = rcBox.left + m_Size.cx;
    rcBox.bottom = rcBox.top + m_Size.cy;
}

int SEASON3B::CNewUIPickedItem::GetSourceLinealPos()
{
    const auto *picked = sessionKeeper_.GameData()->GetPickedItem();
    return picked ? picked->sourceSlot : -1;
}

bool SEASON3B::CNewUIPickedItem::GetTargetPos(CNewUIInventoryCtrl *pDest, int &iTargetColumnX,
                                              int &iTargetRowY)
{
    if (pDest != nullptr)
    {
        const ITEM_ATTRIBUTE *pItemAttr = &ItemAttribute[m_pPickedItem->Type];
        const float iPickedItemX =
            MouseX - ((pItemAttr->Width - 1) * pDest->PresentedSquareWidth() / 2);
        const float iPickedItemY =
            MouseY - ((pItemAttr->Height - 1) * pDest->PresentedSquareHeight() / 2);

        return pDest->GetSquarePosAtPt(iPickedItemX, iPickedItemY, iTargetColumnX, iTargetRowY);
    }
    return false;
}

int SEASON3B::CNewUIPickedItem::GetTargetLinealPos(CNewUIInventoryCtrl *pDest)
{
    int iTargetColumnX, iTargetRowY;
    if (GetTargetPos(pDest, iTargetColumnX, iTargetRowY))
    {
        return pDest->GetIndex(iTargetColumnX, iTargetRowY);
    }
    return -1;
}

bool SEASON3B::CNewUIPickedItem::IsVisible() const
{
    return m_bShow && m_pPickedItem;
}

bool SEASON3B::CNewUIPickedItem::IsOwnerRendered() const
{
    return true;
}

void SEASON3B::CNewUIPickedItem::ShowPickedItem()
{
    m_bShow = true;
}

void SEASON3B::CNewUIPickedItem::HidePickedItem()
{
    m_bShow = false;
}

SEASON3B::CNewUIInventoryCtrl::CNewUIInventoryCtrl(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), ms_pPickedItem(keeper.PickedItem()),
      sessionUi_(UiForConstruction())

{
    Init();
}

SEASON3B::CNewUIInventoryCtrl::~CNewUIInventoryCtrl()
{
    Release();
}

void SEASON3B::CNewUIInventoryCtrl::Init()
{
    m_pNew3DRenderMng = nullptr;
    m_pOwner = nullptr;
    m_Pos.x = m_Pos.y = 0;
    m_Size.cx = m_Size.cy = 0;
    grid_ = nullptr;
    m_EventState = EVENT_NONE;
    m_iPointedSquareIndex = -1;
    m_bShow = true;
    m_bLock = false;
    m_ToolTipType = TOOLTIP_TYPE_INVENTORY;
    m_pToolTipItem = nullptr;
    m_bRepairMode = false;
    m_squareWidth = INVENTORY_SQUARE_WIDTH;
    m_squareHeight = INVENTORY_SQUARE_HEIGHT;
    m_ownerX = 0.0f;
    m_ownerY = 0.0f;
    m_ownerSquareWidth = static_cast<float>(INVENTORY_SQUARE_WIDTH);
    m_ownerSquareHeight = static_cast<float>(INVENTORY_SQUARE_HEIGHT);
    m_renderSlotFrame = true;
    m_ownerRendered = false;
    Vector(0.1f, 0.4f, 0.8f, m_afColorStateNormal);
    Vector(1.f, 0.2f, 0.2f, m_afColorStateWarning);
}

void SEASON3B::CNewUIInventoryCtrl::SetItemColorState(ITEM *pItem)
{
    if (pItem == nullptr)
    {
        return;
    }

    if (pItem->byColorState == ITEM_COLOR_TRADE_WARNING)
    {
        return;
    }

    ITEM_ATTRIBUTE *pItemAttr = &ItemAttribute[pItem->Type];
    const int iLevel = pItem->Level;
    const int iMaxDurability = CalcMaxDurability(pItem, pItemAttr, iLevel);

    if (pItem->Durability <= 0)
    {
        pItem->byColorState = ITEM_COLOR_DURABILITY_100;
    }
    else if (pItem->Durability <= (iMaxDurability * 0.2f))
    {
        pItem->byColorState = ITEM_COLOR_DURABILITY_80;
    }
    else if (pItem->Durability <= (iMaxDurability * 0.3f))
    {
        pItem->byColorState = ITEM_COLOR_DURABILITY_70;
    }
    else if (pItem->Durability <= (iMaxDurability * 0.5f))
    {
        pItem->byColorState = ITEM_COLOR_DURABILITY_50;
    }
    else
    {
        pItem->byColorState = ITEM_COLOR_NORMAL;
    }
}

bool SEASON3B::CNewUIInventoryCtrl::CanChangeItemColorState(ITEM *pItem)
{
    if (pItem == nullptr)
    {
        return false;
    }

    if (pItem->Type < ITEM_WING)
    {
        return true;
    }

    if (pItem->Type == ITEM_BOLT || pItem->Type == ITEM_ARROWS)
    {
        return false;
    }

    if (pItem->Type == ITEM_WIZARDS_RING && (pItem->Level == 1 || pItem->Level == 2))
    {
        return false;
    }

    if (pItem->Type >= ITEM_RING_OF_ICE && pItem->Type <= ITEM_RING_OF_POISON ||
        pItem->Type == ITEM_TRANSFORMATION_RING ||
        pItem->Type >= ITEM_PENDANT_OF_LIGHTING && pItem->Type <= ITEM_PENDANT_OF_FIRE ||
        pItem->Type == ITEM_WIZARDS_RING ||
        pItem->Type >= ITEM_RING_OF_FIRE && pItem->Type <= ITEM_PENDANT_OF_ABILITY ||
        pItem->Type >= ITEM_MOONSTONE_PENDANT && pItem->Type <= ITEM_GAME_MASTER_TRANSFORMATION_RING
#ifdef PJH_ADD_PANDA_CHANGERING
        || pItem->Type == ITEM_PANDA_TRANSFORMATION_RING
#endif //PJH_ADD_PANDA_CHANGERING
        || pItem->Type == ITEM_SKELETON_TRANSFORMATION_RING || pItem->Type == ITEM_PET_PANDA ||
        pItem->Type == ITEM_DEMON || pItem->Type == ITEM_SPIRIT_OF_GUARDIAN ||
        pItem->Type == ITEM_PET_SKELETON || pItem->Type == ITEM_HELPER + 107 ||
        pItem->Type == ITEM_HELPER + 109 || pItem->Type == ITEM_HELPER + 110 ||
        pItem->Type == ITEM_HELPER + 111 || pItem->Type == ITEM_HELPER + 112 ||
        pItem->Type == ITEM_HELPER + 113 || pItem->Type == ITEM_HELPER + 114 ||
        pItem->Type == ITEM_HELPER + 115
#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        || g_pMyInventory->IsInvenItem(pItem->Type)
#endif //LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
    )
    {
        return true;
    }

    if (pItem->Type >= ITEM_HELPER && pItem->Type <= ITEM_DARK_RAVEN_ITEM ||
        pItem->Type == ITEM_HORN_OF_FENRIR || pItem->Type == ITEM_PET_UNICORN)
    {
        return true;
    }

    if (IsWingItem(pItem) == true)
    {
        return true;
    }

    return false;
}

bool SEASON3B::CNewUIInventoryCtrl::Create(InventoryGrid &grid, CNewUI3DRenderMng *renderer,
                                           CNewUIObj *owner, int x, int y)
{
    if (grid_ || !renderer)
        return false;
    UI::Items::ItemSlotTrs::EnsureLoaded();
    grid_ = &grid;
    m_pNew3DRenderMng = renderer;
    renderer->Add3DRenderObj(this, INVENTORY_CAMERA_Z_ORDER);
    m_pOwner = owner;
    m_Pos = {x, y};
    m_Size.cx = grid.GetNumberOfColumn() * m_squareWidth;
    m_Size.cy = grid.GetNumberOfRow() * m_squareHeight;
    dragPreview_.reserve(grid.GetNumberOfColumn() * grid.GetNumberOfRow());
    LoadImages();
    if (grid.GetStorageType() == STORAGE_TYPE::UNDEFINED)
        LockInventory();
    return true;
}

void SEASON3B::CNewUIInventoryCtrl::Release()
{
    if (ms_pPickedItem)
        ms_pPickedItem->DetachInventory(this);
    if (m_pNew3DRenderMng)
        m_pNew3DRenderMng->DeleteUI2DEffectObject(UI2DEffectCallback);

    UnloadImages();

    if (m_pNew3DRenderMng)
        m_pNew3DRenderMng->Remove3DRenderObj(this);

    Init();
}

bool SEASON3B::CNewUIInventoryCtrl::AddItem(int iLinealPos, std::span<const BYTE> itemData)
{
    return grid_->AddItem(iLinealPos, itemData);
}

bool SEASON3B::CNewUIInventoryCtrl::AddItem(int iColumnX, int iRowY, std::span<const BYTE> itemData)
{
    return grid_->AddItem(iColumnX, iRowY, itemData);
}

bool SEASON3B::CNewUIInventoryCtrl::AddItem(int iColumnX, int iRowY, ITEM *pItem)
{
    return grid_->AddItem(iColumnX, iRowY, pItem);
}

bool SEASON3B::CNewUIInventoryCtrl::AddItem(int iColumnX, int iRowY, BYTE byType, BYTE bySubType,
                                            BYTE byLevel, BYTE byDurability, BYTE byOption1,
                                            BYTE byOptionEx, BYTE byOption380, BYTE byOptionHarmony)
{
    return grid_->AddItem(iColumnX, iRowY, byType, bySubType, byLevel, byDurability, byOption1,
                          byOptionEx, byOption380, byOptionHarmony);
}

void SEASON3B::CNewUIInventoryCtrl::RemoveItem(ITEM *pItem)
{
    grid_->RemoveItem(pItem);
}

bool SEASON3B::CNewUIInventoryCtrl::RemoveItemAt(int iLinealPos)
{
    return grid_->RemoveItemAt(iLinealPos);
}

void SEASON3B::CNewUIInventoryCtrl::RequestInventoryRefresh() const
{
    sessionKeeper_.Gameplay()->RequestInventoryRefresh();
}

void SEASON3B::CNewUIInventoryCtrl::RemoveAllItems()
{
    grid_->RemoveAllItems();
}

size_t SEASON3B::CNewUIInventoryCtrl::GetNumberOfItems()
{
    return grid_->GetNumberOfItems();
}

ITEM *SEASON3B::CNewUIInventoryCtrl::GetItem(int iIndex)
{
    return grid_->GetItem(iIndex);
}

void SEASON3B::CNewUIInventoryCtrl::SetSquareColorNormal(float fRed, float fGreen, float fBlue)
{
    Vector(fRed, fGreen, fBlue, m_afColorStateNormal);
}

void SEASON3B::CNewUIInventoryCtrl::GetSquareColorNormal(float *pfParams) const
{
    Vector(m_afColorStateNormal[0], m_afColorStateNormal[1], m_afColorStateNormal[2], pfParams);
}

void SEASON3B::CNewUIInventoryCtrl::SetSquareColorWarning(float fRed, float fGreen, float fBlue)
{
    Vector(fRed, fGreen, fBlue, m_afColorStateWarning);
}

void SEASON3B::CNewUIInventoryCtrl::GetSquareColorWarning(float *pfParams) const
{
    Vector(m_afColorStateWarning[0], m_afColorStateWarning[1], m_afColorStateWarning[2], pfParams);
}

ITEM *SEASON3B::CNewUIInventoryCtrl::FindItem(int iLinealPos)
{
    return grid_->FindItem(iLinealPos);
}

ITEM *SEASON3B::CNewUIInventoryCtrl::FindItem(int iColumnX, int iRowY)
{
    return grid_->FindItem(iColumnX, iRowY);
}

ITEM *SEASON3B::CNewUIInventoryCtrl::FindItemByKey(DWORD dwKey)
{
    return grid_->FindItemByKey(dwKey);
}

ITEM *SEASON3B::CNewUIInventoryCtrl::FindTypeItem(short int siType)
{
    return grid_->FindTypeItem(siType);
}

bool SEASON3B::CNewUIInventoryCtrl::IsItem(short int siType)
{
    return grid_->IsItem(siType);
}

int SEASON3B::CNewUIInventoryCtrl::GetItemCount(short int siType, int iLevel)
{
    return grid_->GetItemCount(siType, iLevel);
}

int SEASON3B::CNewUIInventoryCtrl::FindItemIndex(short int siType, int iLevel)
{
    return grid_->FindItemIndex(siType, iLevel);
}

int SEASON3B::CNewUIInventoryCtrl::FindItemReverseIndex(short sType, int iLevel)
{
    return grid_->FindItemReverseIndex(sType, iLevel);
}

int SEASON3B::CNewUIInventoryCtrl::GetIndexByItem(ITEM *pItem)
{
    return grid_->GetIndexByItem(pItem);
}

ITEM *SEASON3B::CNewUIInventoryCtrl::FindItemPointedSquareIndex()
{
    if (m_iPointedSquareIndex != -1)
    {
        ITEM *pItem = nullptr;
        pItem = FindItemByKey(grid_->SlotKey(m_iPointedSquareIndex - grid_->IndexOffset()));
        return pItem;
    }

    return nullptr;
}

int SEASON3B::CNewUIInventoryCtrl::GetPointedSquareIndex()
{
    return m_iPointedSquareIndex;
}

ITEM *SEASON3B::CNewUIInventoryCtrl::FindItemAtPt(int x, int y)
{
    const int iIndex = GetIndexAtPt(x, y);
    return FindItem(iIndex);
}

int SEASON3B::CNewUIInventoryCtrl::FindEmptySlot(IN int cx, IN int cy)
{
    return grid_->FindEmptySlot(cx, cy);
}
bool SEASON3B::CNewUIInventoryCtrl::FindEmptySlot(IN int cx, IN int cy, OUT int &iColumnX,
                                                  OUT int &iColumnY)
{
    return grid_->FindEmptySlot(cx, cy, iColumnX, iColumnY);
}

int SEASON3B::CNewUIInventoryCtrl::GetNumItemByKey(DWORD dwItemKey)
{
    return grid_->GetNumItemByKey(dwItemKey);
}

int SEASON3B::CNewUIInventoryCtrl::GetNumItemByType(short sItemType)
{
    return grid_->GetNumItemByType(sItemType);
}

int SEASON3B::CNewUIInventoryCtrl::GetEmptySlotCount()
{
    return grid_->GetEmptySlotCount();
}

bool SEASON3B::CNewUIInventoryCtrl::UpdateMouseEvent()
{
    if (m_EventState == EVENT_NONE && IsNone(VK_LBUTTON) && m_iPointedSquareIndex != -1)
    {
        m_EventState = EVENT_HOVER;
    }
    else if (m_EventState == EVENT_HOVER && IsRelease(VK_LBUTTON) && m_iPointedSquareIndex != -1 &&
             nullptr == GetPickedItem() && false == IsLocked() && m_bRepairMode == false)
    {
        m_EventState = EVENT_PICKING;
        ITEM *pItem = this->FindItem(m_iPointedSquareIndex);
        if (pItem)
        {
            if (CreatePickedItem(this, pItem))
            {
                RemoveItem(pItem);
                return false;
            }
        }
    }
    else if (m_EventState == EVENT_HOVER && IsNone(VK_LBUTTON) && m_iPointedSquareIndex != -1 &&
             nullptr == GetPickedItem() &&
             (grid_->SlotKey(m_iPointedSquareIndex - grid_->IndexOffset()) > 1) && g_pNewUIMng)
    {
        ITEM *pItem = this->FindItem(m_iPointedSquareIndex);
        if (pItem != nullptr && pItem != m_pToolTipItem)
        {
            CreateItemToolTip(pItem);

            if ((pItem->Type == ITEM_DARK_HORSE_ITEM) || (pItem->Type == ITEM_DARK_RAVEN_ITEM))
            {
                const ITEM_ATTRIBUTE *pItemAttr = &ItemAttribute[m_pToolTipItem->Type];
                const float squareWidth = PresentedSquareWidth();
                const float squareHeight = PresentedSquareHeight();
                const int iTargetX =
                    static_cast<int>(std::lround(PresentedX() + m_pToolTipItem->x * squareWidth +
                                                 pItemAttr->Width * squareWidth / 2.0f));
                const int iTargetY =
                    static_cast<int>(std::lround(PresentedY() + m_pToolTipItem->y * squareHeight));
                sessionUi_.RequestPetInfo(iTargetX, iTargetY, pItem);
            }
        }
    }
    return true;
}

bool SEASON3B::CNewUIInventoryCtrl::Update()
{
    if (IsVisible())
    {
        UpdateProcess();
        PrepareDragPreview();
        UpdateSlotIconFrames();
    }
    return true;
}

void SEASON3B::CNewUIInventoryCtrl::UpdateSlotIconFrames()
{
    enum SourceIcon
    {
        Empty = 1,
        Occupied = 3,
        Worn = 4,
        Critical = 5,
        Warning = 7
    };
    m_slotIconFrames.assign(grid_->GetNumberOfColumn() * grid_->GetNumberOfRow(), Empty);
    for (auto *item : grid_->Items())
    {
        if (CanChangeItemColorState(item))
            SetItemColorState(item);
        int frame = Occupied;
        switch (item->byColorState)
        {
        case ITEM_COLOR_DURABILITY_50:
        case ITEM_COLOR_DURABILITY_70:
            frame = Worn;
            break;
        case ITEM_COLOR_DURABILITY_80:
        case ITEM_COLOR_DURABILITY_100:
            frame = Critical;
            break;
        case ITEM_COLOR_TRADE_WARNING:
            frame = Warning;
            break;
        }
        const auto &size = ItemAttribute[item->Type];
        for (int row = item->y; row < item->y + size.Height; ++row)
            std::fill_n(m_slotIconFrames.begin() + row * grid_->GetNumberOfColumn() + item->x,
                        size.Width, frame);
    }
}

void SEASON3B::CNewUIInventoryCtrl::UpdateProcess()
{
    if (m_EventState == EVENT_PICKING && !GetPickedItem())
        m_EventState = EVENT_NONE;
    const int iCurSquareIndex = GetIndexAtPt(MouseX, MouseY);
    if (iCurSquareIndex != m_iPointedSquareIndex)
    {
        if ((GetPickedItem() == nullptr) &&
            (g_pMyShopInventory->IsEnableInputValueTextBox() == false))
            InitItemBackup();
        m_iPointedSquareIndex = iCurSquareIndex;
    }

    bool hasValidPointedItem = false;
    if (m_iPointedSquareIndex != -1)
    {
        hasValidPointedItem = this->FindItem(m_iPointedSquareIndex) != nullptr;
    }

    if (m_iPointedSquareIndex == -1 || !hasValidPointedItem)
    {
        m_EventState = EVENT_NONE;
        DeleteItemToolTip();
    }
}

bool SEASON3B::CNewUIInventoryCtrl::CanPreviewDrop(ITEM *pPickItem, ITEM *pTargetItem)
{
    return grid_->CanPreviewDrop(pPickItem, pTargetItem);
}

void SEASON3B::CNewUIInventoryCtrl::SetSquareSize(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }
    m_squareWidth = width;
    m_squareHeight = height;
    m_Size.cx = grid_->GetNumberOfColumn() * m_squareWidth;
    m_Size.cy = grid_->GetNumberOfRow() * m_squareHeight;
}

void SEASON3B::CNewUIInventoryCtrl::SetOwnerGeometry(float x, float y, float squareWidth,
                                                     float squareHeight)
{
    if (squareWidth <= 0.0f || squareHeight <= 0.0f)
        return;

    m_ownerX = x;
    m_ownerY = y;
    m_ownerSquareWidth = squareWidth;
    m_ownerSquareHeight = squareHeight;
    SetSquareSize(std::max(1, static_cast<int>(std::lround(squareWidth))),
                  std::max(1, static_cast<int>(std::lround(squareHeight))));
    SetPos(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
}

float SEASON3B::CNewUIInventoryCtrl::PresentedX() const noexcept
{
    return m_ownerRendered ? m_ownerX : static_cast<float>(m_Pos.x);
}

float SEASON3B::CNewUIInventoryCtrl::PresentedY() const noexcept
{
    return m_ownerRendered ? m_ownerY : static_cast<float>(m_Pos.y);
}

float SEASON3B::CNewUIInventoryCtrl::PresentedSquareWidth() const noexcept
{
    return m_ownerRendered ? m_ownerSquareWidth : static_cast<float>(m_squareWidth);
}

float SEASON3B::CNewUIInventoryCtrl::PresentedSquareHeight() const noexcept
{
    return m_ownerRendered ? m_ownerSquareHeight : static_cast<float>(m_squareHeight);
}

int SEASON3B::CNewUIInventoryCtrl::GetSquareWidth() const
{
    return m_squareWidth;
}

int SEASON3B::CNewUIInventoryCtrl::GetSquareHeight() const
{
    return m_squareHeight;
}

void SEASON3B::CNewUIInventoryCtrl::SetRenderSlotFrame(bool render)
{
    m_renderSlotFrame = render;
}

bool SEASON3B::CNewUIInventoryCtrl::IsOwnerRendered() const
{
    return m_ownerRendered;
}

void SEASON3B::CNewUIInventoryCtrl::SetOwnerRendered(bool ownerRendered)
{
    m_ownerRendered = ownerRendered;
}

const POINT &SEASON3B::CNewUIInventoryCtrl::GetPos() const
{
    return m_Pos;
}

int SEASON3B::CNewUIInventoryCtrl::GetNumberOfColumn() const
{
    return grid_->GetNumberOfColumn();
}

int SEASON3B::CNewUIInventoryCtrl::GetNumberOfRow() const
{
    return grid_->GetNumberOfRow();
}

void SEASON3B::CNewUIInventoryCtrl::GetRect(RECT &rcBox)
{
    const float left = PresentedX();
    const float top = PresentedY();
    rcBox.left = static_cast<LONG>(std::floor(left));
    rcBox.top = static_cast<LONG>(std::floor(top));
    rcBox.right =
        static_cast<LONG>(std::ceil(left + grid_->GetNumberOfColumn() * PresentedSquareWidth()));
    rcBox.bottom =
        static_cast<LONG>(std::ceil(top + grid_->GetNumberOfRow() * PresentedSquareHeight()));
}

CNewUIInventoryCtrl::EVENT_STATE SEASON3B::CNewUIInventoryCtrl::GetEventState()
{
    return m_EventState;
}

CNewUIObj *SEASON3B::CNewUIInventoryCtrl::GetOwner() const
{
    return m_pOwner;
}

bool SEASON3B::CNewUIInventoryCtrl::IsVisible() const
{
    if (m_pOwner)
        return (m_pOwner->IsVisible() && m_bShow);
    return m_bShow;
}

void SEASON3B::CNewUIInventoryCtrl::ShowInventory()
{
    m_bShow = true;
}

void SEASON3B::CNewUIInventoryCtrl::HideInventory()
{
    m_bShow = false;
}

bool SEASON3B::CNewUIInventoryCtrl::IsLocked() const
{
    return m_bLock;
}

void SEASON3B::CNewUIInventoryCtrl::LockInventory()
{
    m_bLock = true;
}

void SEASON3B::CNewUIInventoryCtrl::UnlockInventory()
{
    m_bLock = false;
}

int SEASON3B::CNewUIInventoryCtrl::GetIndexAtPt(int x, int y)
{
    int iColumnX, iRowY;
    if (GetSquarePosAtPt(x, y, iColumnX, iRowY))
        return iRowY * grid_->GetNumberOfColumn() + iColumnX + grid_->IndexOffset();
    return -1;
}

bool SEASON3B::CNewUIInventoryCtrl::GetSquarePosAtPt(float x, float y, int &iColumnX, int &iRowY)
{
    const float left = PresentedX();
    const float top = PresentedY();
    const float squareWidth = PresentedSquareWidth();
    const float squareHeight = PresentedSquareHeight();
    const float right = left + grid_->GetNumberOfColumn() * squareWidth;
    const float bottom = top + grid_->GetNumberOfRow() * squareHeight;

    if (x < left || x >= right || y < top || y >= bottom)
        return false;

    iColumnX = static_cast<int>((x - left) / squareWidth);
    iRowY = static_cast<int>((y - top) / squareHeight);

    return true;
}

int CNewUIInventoryCtrl::GetIndex(int column, int row)
{
    return grid_->GetIndex(column, row);
}

bool SEASON3B::CNewUIInventoryCtrl::CheckPtInRect(int x, int y)
{
    RECT rcSquare;
    GetRect(rcSquare);

    if (x < rcSquare.left || x >= rcSquare.right || y < rcSquare.top || y >= rcSquare.bottom)
        return false;
    return true;
}

bool SEASON3B::CNewUIInventoryCtrl::CheckRectInRect(const RECT &rcBox)
{
    RECT rcSquare;
    GetRect(rcSquare);

    if (rcBox.left >= rcSquare.left && rcBox.right <= rcSquare.right && rcBox.top >= rcSquare.top &&
        rcBox.bottom <= rcSquare.bottom)
        return true;
    return false;
}

bool SEASON3B::CNewUIInventoryCtrl::CanMove(int iLinealPos, ITEM *pItem)
{
    return grid_->CanMove(iLinealPos, pItem);
}

bool SEASON3B::CNewUIInventoryCtrl::CanMove(int iColumnX, int iRowY, ITEM *pItem)
{
    return grid_->CanMove(iColumnX, iRowY, pItem);
}

bool SEASON3B::CNewUIInventoryCtrl::CanMoveToPt(int x, int y, ITEM *pItem)
{
    int iColumnX, iRowY;
    if (GetSquarePosAtPt(x, y, iColumnX, iRowY))
        return CanMove(iColumnX, iRowY, pItem);
    return false;
}

void SEASON3B::CNewUIInventoryCtrl::SetToolTipType(TOOLTIP_TYPE ToolTipType)
{
    m_ToolTipType = ToolTipType;
}

void SEASON3B::CNewUIInventoryCtrl::CreateItemToolTip(ITEM *pItem)
{
    if (m_pToolTipItem)
        DeleteItemToolTip();

    if (g_pNewItemMng)
        m_pToolTipItem = g_pNewItemMng->CreateItem(pItem);
}

void SEASON3B::CNewUIInventoryCtrl::DeleteItemToolTip()
{
    if (m_pToolTipItem && g_pNewItemMng)
    {
        g_pNewItemMng->DeleteItem(m_pToolTipItem);
        m_pToolTipItem = nullptr;
    }
}

void SEASON3B::CNewUIInventoryCtrl::SetRepairMode(bool bRepair)
{
    m_bRepairMode = bRepair;

    if (m_bRepairMode == true)
    {
        SetToolTipType(TOOLTIP_TYPE_REPAIR);
    }
    else
    {
        SetToolTipType(TOOLTIP_TYPE_INVENTORY);
    }
}

bool SEASON3B::CNewUIInventoryCtrl::IsRepairMode()
{
    return m_bRepairMode;
}

namespace InventoryControlDetail
{
int StackedConsumableQuantity(const ITEM &item)
{
    if (item.Durability <= 1)
        return 0;
    const int type = item.Type;
    const bool stacked =
        (type >= ITEM_POTION && type <= ITEM_ANTIDOTE) ||
        (type >= ITEM_JACK_OLANTERN_BLESSINGS && type <= ITEM_JACK_OLANTERN_DRINK) ||
        (type >= ITEM_SMALL_SHIELD_POTION && type <= ITEM_LARGE_COMPLEX_POTION) ||
        (type >= ITEM_POTION + 70 && type <= ITEM_POTION + 71) || type == ITEM_POTION + 94 ||
        (type >= ITEM_POTION + 78 && type <= ITEM_POTION + 82) ||
        (type >= ITEM_CHERRY_BLOSSOM_WINE && type <= ITEM_GOLDEN_CHERRY_BLOSSOM_BRANCH) ||
        type == ITEM_POTION + 133;
    return stacked ? item.Durability : 0;
}
} // namespace InventoryControlDetail

CNewUIPickedItem *SEASON3B::CNewUIInventoryCtrl::GetPickedItem()
{
    return ms_pPickedItem && ms_pPickedItem->GetItem() ? ms_pPickedItem : nullptr;
}

bool SEASON3B::CNewUIInventoryCtrl::CreatePickedItem(CNewUIInventoryCtrl *source, ITEM *item)
{
    if (sessionKeeper_.GameData()->GetPickedItem())
        return false;
    SAFE_DELETE(ms_pPickedItem);
    ms_pPickedItem = new CNewUIPickedItem(SessionOrigin());
    if (ms_pPickedItem->Create(g_pNewItemMng, source, item))
        return true;
    SAFE_DELETE(ms_pPickedItem);
    return false;
}

void SEASON3B::CNewUIInventoryCtrl::DeletePickedItem()
{
    if (ms_pPickedItem)
    {
        CNewUIInventoryCtrl *pOwner = ms_pPickedItem->GetOwnerInventory();
        if (pOwner)
        {
            pOwner->SetEventState(CNewUIInventoryCtrl::EVENT_NONE);
        }
    }

    sessionKeeper_.GameData()->ClearPickedItem();
    SAFE_DELETE(ms_pPickedItem);
}

void SEASON3B::CNewUIInventoryCtrl::BackupPickedItem()
{
    if (sessionKeeper_.Gameplay()->RestorePickedItem())
        DeletePickedItem();
}

void SEASON3B::CNewUIInventoryCtrl::SetEventState(EVENT_STATE es)
{
    m_EventState = es;
}

float SEASON3B::CNewUIInventoryCtrl::ItemPresentationScale() const noexcept
{
    // The item camera has a fixed vertical FOV. Its pixel scale follows height;
    // using the X reference scale here shrinks models in wide viewports.
    return m_ownerRendered ? PresentedSquareHeight() / INVENTORY_SQUARE_HEIGHT : 1.0f;
}

bool SEASON3B::CNewUIInventoryCtrl::AreItemsStackable(ITEM *pSourceItem, ITEM *pTargetItem)
{
    return grid_->AreItemsStackable(pSourceItem, pTargetItem);
}

bool SEASON3B::CNewUIInventoryCtrl::CanUpgradeItem(ITEM *pSourceItem, ITEM *pTargetItem)
{
    return grid_->CanUpgradeItem(pSourceItem, pTargetItem);
}

void SEASON3B::CNewUIInventoryCtrl::ReleasePickedItemView()
{
    SAFE_DELETE(ms_pPickedItem);
}

static_assert(MAX_EQUIPMENT == UI::Modern::PC::Inventory::RmlDurabilityLayer::EquipmentCount);

namespace DurabilityPanelDetail
{
int PetFrameLogicalSize(float gfxPixels, float screenRate, float maximumScale)
{
    return static_cast<int>(std::ceil(gfxPixels * maximumScale / screenRate));
}

int PetFrameLogicalOffset(float gfxPixels, float screenRate, float maximumScale)
{
    return static_cast<int>(std::floor(gfxPixels * maximumScale / screenRate));
}

int PetFrameLogicalSpan(float gfxOffset, float gfxSize, float screenRate, float maximumScale)
{
    const int start = PetFrameLogicalOffset(gfxOffset, screenRate, maximumScale);
    const int end = PetFrameLogicalSize(gfxOffset + gfxSize, screenRate, maximumScale);
    return end - start;
}
} // namespace DurabilityPanelDetail

CNewUIItemEnduranceInfo::CNewUIItemEnduranceInfo(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), gameplay_(GameplayForConstruction()),
      renderer_(RendererForConstruction()), durability_(keeper)
{
}
CNewUIItemEnduranceInfo::~CNewUIItemEnduranceInfo()
{
    Release();
}
bool CNewUIItemEnduranceInfo::Create(CNewUIManager *manager, int x, int y)
{
    if (!manager)
        return false;
    m_pNewUIMng = manager;
    manager->AddUIObj(INTERFACE_ITEM_ENDURANCE_INFO, this);
    SetPos(x, y);
    Show(true);
    return true;
}
void CNewUIItemEnduranceInfo::Release()
{
    durability_.Release();
    content_ = {};
    ammunitionCounts_.fill(-1);
    visible_ = false;
    if (!m_pNewUIMng)
        return;
    m_pNewUIMng->RemoveUIObj(this);
    m_pNewUIMng = nullptr;
}

bool CNewUIItemEnduranceInfo::UpdateMouseEvent()
{
    return UpdatePetFrameMouse() && durability_.HoveredEquipment() < 0;
}
bool CNewUIItemEnduranceInfo::UpdateKeyEvent()
{
    return true;
}
bool CNewUIItemEnduranceInfo::BtnProcess()
{
    return false;
}
float CNewUIItemEnduranceInfo::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::PetHud;
}
void CNewUIItemEnduranceInfo::OpenningProcess()
{
}
void CNewUIItemEnduranceInfo::ClosingProcess()
{
    visible_ = false;
}
int CNewUIItemEnduranceInfo::WarningState(int slot) const
{
    const auto &item = CharacterMachine->Equipment[slot];
    if (slot == EQUIPMENT_HELPER || item.Type < 0 || (item.bPeriodItem && !item.bExpiredPeriod))
        return 0;
    if ((slot == EQUIPMENT_WEAPON_RIGHT && item.Type == ITEM_ARROWS) ||
        (slot == EQUIPMENT_WEAPON_LEFT && item.Type == ITEM_BOLT))
        return 0;
    if ((slot == EQUIPMENT_RING_RIGHT || slot == EQUIPMENT_RING_LEFT) &&
        item.Type == ITEM_WIZARDS_RING && (item.Level == 1 || item.Level == 2))
        return 0;
    const int maximum = CalcMaxDurability(&item, &ItemAttribute[item.Type], item.Level);
    constexpr float WarningFraction = 0.5f, SevereFraction = 0.3f, CriticalFraction = 0.2f;
    if (item.Durability > maximum * WarningFraction)
        return 0;
    if (item.Durability == 0)
        return 4;
    if (item.Durability <= maximum * CriticalFraction)
        return 3;
    if (item.Durability <= maximum * SevereFraction)
        return 2;
    return 1;
}

bool CNewUIItemEnduranceInfo::Update()
{
    visible_ = IsVisible();
    content_.warningsVisible = !g_pNewUISystem->IsVisible(INTERFACE_TRADE);
    if (!IsVisible())
        return true;
    StagePetFrame();
    StageAmmunition();
    for (int slot = 0; slot < MAX_EQUIPMENT; ++slot)
    {
        int type = slot;
        if (slot == EQUIPMENT_WEAPON_LEFT &&
            gCharacterManager.GetEquipedBowType(&CharacterMachine->Equipment[slot]) == BOWTYPE_BOW)
            type = EQUIPMENT_WEAPON_RIGHT;
        content_.equipment[slot] = {type, WarningState(slot)};
    }
    return true;
}

bool CNewUIItemEnduranceInfo::ProcessModernUiInput(const SessionInputEvent &event)
{
    return visible_ && durability_.ProcessInput(event);
}

void SEASON3B::CNewUIItemEnduranceInfo::AppendPetFrameRow(UI::Modern::RmlPetFrameRequest &request,
                                                          const wchar_t *name, int life,
                                                          int maximum)
{
    if (name == nullptr || request.rowCount >= UI::Modern::RmlPetFrameRequest::RowCapacity)
    {
        return;
    }
    UI::Modern::RmlPetFrameRow &row = request.rows[request.rowCount++];
    std::wcsncpy(row.name, name, UI::Modern::RmlPetFrameRow::NameCapacity - 1);
    row.name[UI::Modern::RmlPetFrameRow::NameCapacity - 1] = L'\0';
    row.maximum = maximum;
    row.position = std::clamp(life, 0, maximum);
}

bool SEASON3B::CNewUIItemEnduranceInfo::UpdatePetFrameMouse()
{
    if (m_petFrameRowCount <= 0)
    {
        m_petFrameDragging = false;
        m_petFrameMinimizePressed = false;
        m_petFrameButtonState = ButtonVisualState::Up;
        return true;
    }

    const int frameWidth = DurabilityPanelDetail::PetFrameLogicalSize(
        UI::Modern::RmlPetFrameLayer::Width(), ModernUiScreenRateX(), ModernUiScale());
    const int dragHeight = DurabilityPanelDetail::PetFrameLogicalSize(
        UI::Modern::RmlPetFrameLayer::DragHeight(), ModernUiScreenRateY(), ModernUiScale());
    const float gfxFrameHeight =
        UI::Modern::RmlPetFrameLayer::DragHeight() +
        (m_petFrameMinimized ? 0 : m_petFrameRowCount * UI::Modern::RmlPetFrameLayer::RowHeight());
    const int frameHeight = DurabilityPanelDetail::PetFrameLogicalSize(
        static_cast<float>(gfxFrameHeight), ModernUiScreenRateY(), ModernUiScale());
    const int minimizeX = DurabilityPanelDetail::PetFrameLogicalOffset(
        UI::Modern::RmlPetFrameLayer::MinimizeRect().x, ModernUiScreenRateX(), ModernUiScale());
    const int minimizeY = DurabilityPanelDetail::PetFrameLogicalOffset(
        UI::Modern::RmlPetFrameLayer::MinimizeRect().y, ModernUiScreenRateY(), ModernUiScale());
    const int minimizeWidth = DurabilityPanelDetail::PetFrameLogicalSpan(
        UI::Modern::RmlPetFrameLayer::MinimizeRect().x,
        UI::Modern::RmlPetFrameLayer::MinimizeRect().width, ModernUiScreenRateX(), ModernUiScale());
    const int minimizeHeight = DurabilityPanelDetail::PetFrameLogicalSpan(
        UI::Modern::RmlPetFrameLayer::MinimizeRect().y,
        UI::Modern::RmlPetFrameLayer::MinimizeRect().height, ModernUiScreenRateY(),
        ModernUiScale());
    const int logicalHeight = static_cast<int>(ModernUiViewportHeight() / ModernUiScreenRateY());
    const bool minimizeHovered = CheckMouseIn(
        m_petFramePos.x + minimizeX, m_petFramePos.y + minimizeY, minimizeWidth, minimizeHeight);

    if (m_petFrameDragging)
    {
        if (MouseLButtonPush || IsRepeat(VK_LBUTTON))
        {
            m_petFramePos.x = std::clamp<int>(static_cast<int>(MouseX - m_petFrameDragOffset.x), 0,
                                              std::max(0, GetScreenWidth() - frameWidth));
            m_petFramePos.y = std::clamp<int>(static_cast<int>(MouseY - m_petFrameDragOffset.y), 0,
                                              std::max(0, logicalHeight - frameHeight));
            return false;
        }
        m_petFrameDragging = false;
        return false;
    }

    if (minimizeHovered)
    {
        if (IsPress(VK_LBUTTON))
        {
            m_petFrameMinimizePressed = true;
        }
        m_petFrameButtonState =
            m_petFrameMinimizePressed && (MouseLButtonPush || IsRepeat(VK_LBUTTON))
                ? ButtonVisualState::Down
                : ButtonVisualState::Over;
        if (IsRelease(VK_LBUTTON))
        {
            if (m_petFrameMinimizePressed)
            {
                m_petFrameMinimized = !m_petFrameMinimized;
                const float nextGfxHeight =
                    UI::Modern::RmlPetFrameLayer::DragHeight() +
                    (m_petFrameMinimized
                         ? 0
                         : m_petFrameRowCount * UI::Modern::RmlPetFrameLayer::RowHeight());
                const int nextHeight = DurabilityPanelDetail::PetFrameLogicalSize(
                    static_cast<float>(nextGfxHeight), ModernUiScreenRateY(), ModernUiScale());
                m_petFramePos.y = std::clamp<int>(static_cast<int>(m_petFramePos.y), 0,
                                                  std::max(0, logicalHeight - nextHeight));
            }
            m_petFrameMinimizePressed = false;
        }
        return false;
    }
    if (IsRelease(VK_LBUTTON))
    {
        m_petFrameMinimizePressed = false;
    }
    m_petFrameButtonState = ButtonVisualState::Up;

    if (IsPress(VK_LBUTTON) &&
        CheckMouseIn(m_petFramePos.x, m_petFramePos.y, frameWidth, dragHeight))
    {
        m_petFrameDragging = true;
        m_petFrameDragOffset = {MouseX - m_petFramePos.x, MouseY - m_petFramePos.y};
        return false;
    }

    return !CheckMouseIn(m_petFramePos.x, m_petFramePos.y, frameWidth, frameHeight);
}

bool SEASON3B::CNewUIItemEnduranceInfo::GetEquippedHelperName(wchar_t *name,
                                                              std::size_t capacity) const
{
    if (name == nullptr || capacity == 0)
    {
        return false;
    }
    const int type = Hero->Helper.Type;
    if (!((type >= MODEL_HELPER && type <= MODEL_DARK_HORSE_ITEM) || type == MODEL_DEMON ||
          type == MODEL_SPIRIT_OF_GUARDIAN || type == MODEL_PET_RUDOLF || type == MODEL_PET_PANDA ||
          type == MODEL_PET_UNICORN || type == MODEL_PET_SKELETON || type == MODEL_HORN_OF_FENRIR))
    {
        return false;
    }

    const wchar_t *source = nullptr;
    switch (type)
    {
    case MODEL_HELPER:
        source = I18N::Game::GuardianAngel;
        break;
    case MODEL_IMP:
        source = ItemAttribute[type - MODEL_SWORD].Name;
        break;
    case MODEL_HORN_OF_UNIRIA:
        source = I18N::Game::Uniria;
        break;
    case MODEL_HORN_OF_DINORANT:
        source = I18N::Game::Dinorant;
        break;
    case MODEL_DARK_HORSE_ITEM:
        source = I18N::Game::DarkHorse;
        break;
    case MODEL_HORN_OF_FENRIR:
        source = I18N::Game::Fenrir;
        break;
    case MODEL_DEMON:
        source = ItemAttribute[ITEM_DEMON].Name;
        break;
    case MODEL_SPIRIT_OF_GUARDIAN:
        source = ItemAttribute[ITEM_SPIRIT_OF_GUARDIAN].Name;
        break;
    case MODEL_PET_RUDOLF:
        source = ItemAttribute[ITEM_PET_RUDOLF].Name;
        break;
    case MODEL_PET_PANDA:
        source = ItemAttribute[ITEM_PET_PANDA].Name;
        break;
    case MODEL_PET_UNICORN:
        source = ItemAttribute[ITEM_PET_UNICORN].Name;
        break;
    case MODEL_PET_SKELETON:
        source = ItemAttribute[ITEM_PET_SKELETON].Name;
        break;
    default:
        return false;
    }

    std::wcsncpy(name, source, capacity - 1);
    name[capacity - 1] = L'\0';
    return true;
}

CNewUIMixInventory::CNewUIMixInventory(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), gameData_(GameDataForConstruction()),
      renderUnit_(RendererForConstruction()), g_nChaosTaxRate(keeper.ChaosTaxRate()),
      m_ModernPanel(keeper, "mix.rml")
{
    m_pNewUIMng = NULL;
    m_pNewInventoryCtrl = NULL;
    m_Pos.x = m_Pos.y = 0;
    m_iMixState = MIX_READY;
    m_mixEffectTicks = 0;
}
CNewUIMixInventory::~CNewUIMixInventory()
{
    Release();
}

bool CNewUIMixInventory::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng || NULL == g_pNewUI3DRenderMng || NULL == g_pNewItemMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_MIXINVENTORY, this);

    m_pNewInventoryCtrl = renderUnit_.CreateInventoryControl();
    if (false ==
        m_pNewInventoryCtrl->Create(GameDataForConstruction().Inventory(InventoryRole::Crafting),
                                    g_pNewUI3DRenderMng, this, 0, 0))
    {
        SAFE_DELETE(m_pNewInventoryCtrl);
        return false;
    }

    SetPos(x, y);

    m_pNewInventoryCtrl->SetOwnerRendered(true);
    m_pNewInventoryCtrl->SetRenderSlotFrame(false);

    m_pNewInventoryCtrl->GetSquareColorNormal(m_fInventoryColor);
    m_pNewInventoryCtrl->GetSquareColorWarning(m_fInventoryWarningColor);

    Show(false);

    return true;
}
void CNewUIMixInventory::Release()
{
    m_ModernPanel.Release();

    SAFE_DELETE(m_pNewInventoryCtrl);

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

void CNewUIMixInventory::SetMixState(int iMixState)
{
    m_iMixState = iMixState;

    if (iMixState == MIX_REQUESTED)
    {
        m_mixEffectTicks = 50;
        m_pNewInventoryCtrl->LockInventory();
        g_pMyInventory->GetInventoryCtrl()->LockInventory();
    }
    else
    {
        m_pNewInventoryCtrl->UnlockInventory();
        g_pMyInventory->GetInventoryCtrl()->UnlockInventory();
    }
}

bool CNewUIMixInventory::InsertItem(int iIndex, std::span<const BYTE> pbyItemPacket)
{
    if (m_pNewInventoryCtrl)
        return m_pNewInventoryCtrl->AddItem(iIndex, pbyItemPacket);
    return false;
}

void CNewUIMixInventory::DeleteItem(int iIndex)
{
    if (m_pNewInventoryCtrl)
    {
        ITEM *pItem = m_pNewInventoryCtrl->FindItem(iIndex);
        if (pItem != NULL)
            m_pNewInventoryCtrl->RemoveItem(pItem);
    }
}

void CNewUIMixInventory::DeleteAllItems()
{
    if (m_pNewInventoryCtrl)
        m_pNewInventoryCtrl->RemoveAllItems();
}

void CNewUIMixInventory::OpeningProcess()
{
    g_MixRecipeMgr.SetPlusChaosRate(0);
    SocketClient->ToGameServer()->SendCrywolfChaosRateBenefitRequest();

    SetMixState(SEASON3B::CNewUIMixInventory::MIX_READY);

    if (g_MixRecipeMgr.GetMixInventoryType() == SEASON3A::MIXTYPE_GOBLIN_NORMAL)
    {
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CChaosMixMenuMsgBoxLayout, SessionOrigin()));
    }
}

bool CNewUIMixInventory::ClosingProcess()
{
    if (g_pMixInventory->GetInventoryCtrl()->GetNumberOfItems() > 0 ||
        g_pMyInventory->GetInventoryCtrl()->GetPickedItem() != NULL)
    {
        g_pSystemLogBox->AddText(I18N::Game::CloseInventoryAfterMovingYourItemsInTheInventory,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
        return false;
    }

    switch (g_MixRecipeMgr.GetMixInventoryType())
    {
    case SEASON3A::MIXTYPE_GOBLIN_NORMAL:
    case SEASON3A::MIXTYPE_GOBLIN_CHAOSITEM:
    case SEASON3A::MIXTYPE_GOBLIN_ADD380:
    case SEASON3A::MIXTYPE_CASTLE_SENIOR:
    case SEASON3A::MIXTYPE_OSBOURNE:
    case SEASON3A::MIXTYPE_JERRIDON:
    case SEASON3A::MIXTYPE_ELPIS:
    case SEASON3A::MIXTYPE_CHAOS_CARD:
    case SEASON3A::MIXTYPE_CHERRYBLOSSOM:
    case SEASON3A::MIXTYPE_EXTRACT_SEED:
    case SEASON3A::MIXTYPE_SEED_SPHERE:
        SocketClient->ToGameServer()->SendCraftingDialogCloseRequest();
        break;
    case SEASON3A::MIXTYPE_TRAINER:
        SocketClient->ToGameServer()->SendCloseNpcRequest();
        break;
    case SEASON3A::MIXTYPE_ATTACH_SOCKET:
    case SEASON3A::MIXTYPE_DETACH_SOCKET:
        m_SelectedSocket = -1;
        SocketClient->ToGameServer()->SendCraftingDialogCloseRequest();
        break;
    default:
        break;
    }
    g_pMixInventory->DeleteAllItems();
    g_MixRecipeMgr.ClearCheckRecipeResult();
    return true;
}

bool CNewUIMixInventory::UpdateMouseEvent()
{
    SyncModernGeometry();
    if (m_pNewInventoryCtrl && false == m_pNewInventoryCtrl->UpdateMouseEvent())
        return false;

    if (true == InventoryProcess())
        return false;

    if (true == BtnProcess())
        return false;

    if (IsMouseInModernPanel())
    {
        if (IsPress(VK_RBUTTON))
        {
            // Right-click on a craft-box item sends it back to the inventory (mirror of the
            // inventory -> craft-box right-click move).
            ProcessMixItemAutoMoveToInventory();
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
bool CNewUIMixInventory::UpdateKeyEvent()
{
    return true;
}
bool CNewUIMixInventory::Update()
{
    m_mixEffectTicks = (std::max)(0.f, m_mixEffectTicks - FPS_ANIMATION_FACTOR);
    if (m_ModernPanel.TakeFocus() && m_pNewUIMng)
        m_pNewUIMng->BringToFront(this);
    BtnProcess();
    m_ModernPanel.SetVisible(IsVisible());
    if (m_pNewInventoryCtrl && !m_pNewInventoryCtrl->Update())
        return false;
    if (IsVisible())
    {
        SyncModernGeometry();
        CheckMixInventory();
        UpdateSocketSelection();
        StageModernContent();
    }
    return true;
}

float CNewUIMixInventory::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

CNewUIInventoryCtrl *CNewUIMixInventory::GetInventoryCtrl() const
{
    return m_pNewInventoryCtrl;
}

bool CNewUIMixInventory::BtnProcess()
{
    if (m_ModernPanel.TakeClick("btnClose"))
        g_pNewUISystem->Hide(INTERFACE_MIXINVENTORY);

    if (GetMixState() != MIX_READY)
    {
        return false;
    }

    if (m_ModernPanel.TakeClick("btnMix"))
    {
        Mix();
        return true;
    }

    return false;
}

int CNewUIMixInventory::Rtn_MixRequireZen(int _nMixZen, int _nTax)
{
    if (_nTax)
        _nMixZen += ((LONGLONG)_nMixZen * g_nChaosTaxRate) / 100;
    return _nMixZen;
}

bool CNewUIMixInventory::Mix()
{
    PlayBuffer(SOUND_CLICK01);

    DWORD dwGold = CharacterMachine->Gold;
    int nMixZen = g_MixRecipeMgr.GetReqiredZen();

    nMixZen = Rtn_MixRequireZen(nMixZen, g_nChaosTaxRate);

    if (nMixZen > (int)dwGold)
    {
        g_pSystemLogBox->AddText(I18N::Game::NotEnoughZenToCombineItems,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
        return false;
    }

    if (!g_MixRecipeMgr.IsReadyToMix())
    {
        wchar_t szText[100];
        mu_swprintf(szText, I18N::Game::YouAreLackOfSItems, I18N::Game::Combining);
        g_pSystemLogBox->AddText(szText, SEASON3B::TYPE_ERROR_MESSAGE);
        return false;
    }

    int iLevel = CharacterAttribute->Level;
    if (iLevel < g_MixRecipeMgr.GetCurRecipe()->m_iRequiredLevel)
    {
        wchar_t szText[100];
        wchar_t szText2[100];
        g_MixRecipeMgr.GetCurRecipeName(szText2, 1);
        mu_swprintf(szText, I18N::Game::FromAboveTheLevelDSEnabledAndOn,
                    g_MixRecipeMgr.GetCurRecipe()->m_iRequiredLevel, szText2);
        g_pSystemLogBox->AddText(szText, SEASON3B::TYPE_ERROR_MESSAGE);
        return false;
    }

    if (g_MixRecipeMgr.GetCurRecipe()->m_iWidth != -1 &&
        g_pMyInventory->FindEmptySlot(g_MixRecipeMgr.GetCurRecipe()->m_iWidth,
                                      g_MixRecipeMgr.GetCurRecipe()->m_iHeight) == -1)
    {
        g_pSystemLogBox->AddText(I18N::Game::CombineItemsAfterOrganizingYourInventory,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
        return false;
    }

    if (g_MixRecipeMgr.GetMixInventoryType() == SEASON3A::MIXTYPE_ATTACH_SOCKET)
    {
        int iSelectedLine = m_SelectedSocket;

        for (int i = 0; i < g_MixRecipeMgr.GetFirstItemSocketCount(); ++i)
        {
            BYTE bySocketSeedID = g_MixRecipeMgr.GetFirstItemSocketSeedID(i);
            if (bySocketSeedID != SOCKET_EMPTY)
            {
                BYTE bySeedSphereID = g_MixRecipeMgr.GetSeedSphereID(0);
                if (bySocketSeedID == bySeedSphereID)
                {
                    g_pSystemLogBox->AddText(I18N::Game::YouCannotApplyTheSameTypeOfSphere,
                                             SEASON3B::TYPE_ERROR_MESSAGE);
                    return false;
                }
            }
        }

        if (m_SelectedSocket < 0)
        {
            g_pSystemLogBox->AddText(I18N::Game::YouMustSelectTheSocket,
                                     SEASON3B::TYPE_ERROR_MESSAGE);
            return false;
        }
        else if (iSelectedLine >= g_MixRecipeMgr.GetFirstItemSocketCount() ||
                 g_MixRecipeMgr.GetFirstItemSocketSeedID(iSelectedLine) != SOCKET_EMPTY)
        {
            g_pSystemLogBox->AddText(I18N::Game::ItSAlreadyAppliedOnTheCharacter,
                                     SEASON3B::TYPE_ERROR_MESSAGE);
            return false;
        }

        g_MixRecipeMgr.SetMixSubType(iSelectedLine);
    }
    else if (g_MixRecipeMgr.GetMixInventoryType() == SEASON3A::MIXTYPE_DETACH_SOCKET)
    {
        int iSelectedLine = m_SelectedSocket;
        if (m_SelectedSocket < 0)
        {
            g_pSystemLogBox->AddText(I18N::Game::YouMustSelectTheDestructibleSocket,
                                     SEASON3B::TYPE_ERROR_MESSAGE);
            return false;
        }
        else if (iSelectedLine >= g_MixRecipeMgr.GetFirstItemSocketCount() ||
                 g_MixRecipeMgr.GetFirstItemSocketSeedID(iSelectedLine) == SOCKET_EMPTY)
        {
            g_pSystemLogBox->AddText(I18N::Game::ThereAreNoDestructibleSeedSpheres,
                                     SEASON3B::TYPE_ERROR_MESSAGE);
            return false;
        }
        g_MixRecipeMgr.SetMixSubType(iSelectedLine);
    }

#ifdef LJH_MOD_CANNOT_USE_CHARMITEM_AND_CHAOSCHARMITEM_SIMULTANEOUSLY
    if (g_MixRecipeMgr.GetTotalChaosCharmCount() > 0 && g_MixRecipeMgr.GetTotalCharmCount() > 0)
    {
        g_pSystemLogBox->AddText(I18N::Game::YouCannotUseTheTalismanOf,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
        return FALSE;
    }
#endif //LJH_MOD_CANNOT_USE_CHARMITEM_AND_CHAOSCHARMITEM_SIMULTANEOUSLY

    if (g_pMyInventory->GetInventoryCtrl()->GetPickedItem() == NULL)
    {
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CMixCheckMsgBoxLayout, SessionOrigin()));
        return true;
    }

    return false;
}

bool CNewUIMixInventory::InventoryProcess()
{
    CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();

    if (m_pNewInventoryCtrl && pPickedItem)
    {
        const auto iCurInventory = g_MixRecipeMgr.GetMixInventoryEquipmentIndex();

        ITEM *pItemObj = pPickedItem->GetItem();
        if (GetMixState() == MIX_READY && g_MixRecipeMgr.IsMixSource(pPickedItem->GetItem()) &&
            pPickedItem->GetSourceStorageType() == STORAGE_TYPE::INVENTORY)
        {
            m_pNewInventoryCtrl->SetSquareColorNormal(m_fInventoryColor[0], m_fInventoryColor[1],
                                                      m_fInventoryColor[2]);
            if (IsPress(VK_LBUTTON))
            {
                int iSourceIndex = pPickedItem->GetSourceLinealPos();
                int iTargetIndex = pPickedItem->GetTargetLinealPos(m_pNewInventoryCtrl);
                if (iTargetIndex != -1 && m_pNewInventoryCtrl->CanMove(iTargetIndex, pItemObj))
                {
                    if (SendRequestEquipmentItem(STORAGE_TYPE::INVENTORY, iSourceIndex, pItemObj,
                                                 iCurInventory, iTargetIndex))
                    {
                        return true;
                    }
                }
            }
        }
        else if (pPickedItem->GetOwnerInventory() == m_pNewInventoryCtrl)
        {
            m_pNewInventoryCtrl->SetSquareColorNormal(m_fInventoryColor[0], m_fInventoryColor[1],
                                                      m_fInventoryColor[2]);
            if (IsPress(VK_LBUTTON))
            {
                int iSourceIndex = pPickedItem->GetSourceLinealPos();
                int iTargetIndex = pPickedItem->GetTargetLinealPos(m_pNewInventoryCtrl);
                if (iTargetIndex != -1 && m_pNewInventoryCtrl->CanMove(iTargetIndex, pItemObj))
                {
                    if (SendRequestEquipmentItem(iCurInventory, iSourceIndex, pItemObj,
                                                 iCurInventory, iTargetIndex))
                    {
                        return true;
                    }
                }
            }
        }
        else if (GetMixState() == MIX_READY && g_MixRecipeMgr.IsMixSource(pPickedItem->GetItem()) &&
                 pItemObj->ex_src_type == ITEM_EX_SRC_EQUIPMENT)
        {
            m_pNewInventoryCtrl->SetSquareColorNormal(m_fInventoryColor[0], m_fInventoryColor[1],
                                                      m_fInventoryColor[2]);
            if (IsPress(VK_LBUTTON))
            {
                int iSourceIndex = pPickedItem->GetSourceLinealPos();
                int iTargetIndex = pPickedItem->GetTargetLinealPos(m_pNewInventoryCtrl);
                if (iTargetIndex != -1 && m_pNewInventoryCtrl->CanMove(iTargetIndex, pItemObj))
                {
                    SendRequestEquipmentItem(STORAGE_TYPE::INVENTORY, iSourceIndex, pItemObj,
                                             iCurInventory, iTargetIndex);
                    return true;
                }
            }
        }
        else
        {
            m_pNewInventoryCtrl->SetSquareColorNormal(m_fInventoryWarningColor[0],
                                                      m_fInventoryWarningColor[1],
                                                      m_fInventoryWarningColor[2]);
        }
    }
    return false;
}

// Direction-agnostic core of the right-click moves: pick the item under the
// cursor in srcCtrl, reserve a slot in dstCtrl, and send the same move that
// drag & drop sends.
bool CNewUIMixInventory::AutoMoveItem(CNewUIInventoryCtrl *srcCtrl, STORAGE_TYPE srcType,
                                      CNewUIInventoryCtrl *dstCtrl, STORAGE_TYPE dstType,
                                      bool requireMixSource)
{
    if (g_pMyInventory->GetInventoryCtrl()->GetPickedItem())
        return false;

    if (srcCtrl == nullptr || dstCtrl == nullptr || GetMixState() != MIX_READY)
        return false;

    ITEM *pItemObj = srcCtrl->FindItemAtPt(MouseX, MouseY);
    if (pItemObj == nullptr)
        return false;

    if (requireMixSource && !g_MixRecipeMgr.IsMixSource(pItemObj))
        return false;

    const ITEM_ATTRIBUTE *pItemAttr = &ItemAttribute[pItemObj->Type];
    const int iTargetIndex = dstCtrl->FindEmptySlot(pItemAttr->Width, pItemAttr->Height);
    if (iTargetIndex < 0 || !dstCtrl->CanMove(iTargetIndex, pItemObj))
        return false;

    if (!g_pMyInventory->GetInventoryCtrl()->CreatePickedItem(srcCtrl, pItemObj))
        return false;

    CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();
    if (pPickedItem == nullptr)
        return false;

    srcCtrl->RemoveItem(pItemObj);
    pPickedItem->HidePickedItem();

    if (!SendRequestEquipmentItem(srcType, pPickedItem->GetSourceLinealPos(), pItemObj, dstType,
                                  iTargetIndex))
    {
        g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();
        return false;
    }

    PlayBuffer(SOUND_GET_ITEM01);
    return true;
}

bool CNewUIMixInventory::ProcessMyInvenItemAutoMove(CNewUIInventoryCtrl *sourceCtrl)
{
    if (sourceCtrl == nullptr)
        sourceCtrl = g_pMyInventory ? g_pMyInventory->GetInventoryCtrl() : nullptr;

    if (sourceCtrl == nullptr || sourceCtrl->GetStorageType() != STORAGE_TYPE::INVENTORY)
        return false;

    return AutoMoveItem(sourceCtrl, STORAGE_TYPE::INVENTORY, m_pNewInventoryCtrl,
                        g_MixRecipeMgr.GetMixInventoryEquipmentIndex(),
                        /*requireMixSource*/ true);
}

bool CNewUIMixInventory::ProcessMixItemAutoMoveToInventory()
{
    CNewUIInventoryCtrl *dstCtrl = g_pMyInventory ? g_pMyInventory->GetInventoryCtrl() : nullptr;
    return AutoMoveItem(m_pNewInventoryCtrl, g_MixRecipeMgr.GetMixInventoryEquipmentIndex(),
                        dstCtrl, STORAGE_TYPE::INVENTORY,
                        /*requireMixSource*/ false);
}

void CNewUIMixInventory::CheckMixInventory()
{
    g_MixRecipeMgr.ResetMixItemInventory();
    ITEM *pItem = NULL;
    for (int i = 0; i < (int)m_pNewInventoryCtrl->GetNumberOfItems(); ++i)
    {
        pItem = m_pNewInventoryCtrl->GetItem(i);
        g_MixRecipeMgr.AddItemToMixItemInventory(pItem);
    }
    g_MixRecipeMgr.CheckMixInventory();
}

int SEASON3B::CNewUIMixInventory::GetPointedItemIndex()
{
    return m_pNewInventoryCtrl->GetPointedSquareIndex();
}

void CNewUIMixInventory::BuildMixDescriptions(std::string &guide)
{
    const char *tone = "normal";

    wchar_t szText[256] = {
        0,
    };
    switch (g_MixRecipeMgr.GetMixInventoryType())
    {
    case SEASON3A::MIXTYPE_GOBLIN_NORMAL:
    case SEASON3A::MIXTYPE_GOBLIN_CHAOSITEM:
    case SEASON3A::MIXTYPE_GOBLIN_ADD380:
        break;
    case SEASON3A::MIXTYPE_CASTLE_SENIOR: {
        tone = "normal";
        for (int i = 0; i < 6; ++i)
            MixInventoryDetail::AppendMixText(guide, I18N::Game::Lookup(1644 + i), tone);
    }
    break;
    case SEASON3A::MIXTYPE_TRAINER:
        break;
    case SEASON3A::MIXTYPE_OSBOURNE: {
        tone = "normal";
        mu_swprintf(szText, I18N::Game::RefineTheItemToCreate);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
        mu_swprintf(szText, I18N::Game::TheRefiningStone);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
        mu_swprintf(szText, I18N::Game::SForOnlyS, I18N::Game::Refine,
                    I18N::Game::WeaponsOrShields);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
        mu_swprintf(szText, I18N::Game::Allowed);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
        tone = "warning";
        mu_swprintf(szText, I18N::Game::ItemWillDisappearWhenFailed);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
    }
    break;
    case SEASON3A::MIXTYPE_JERRIDON: {
        tone = "normal";
        mu_swprintf(szText, I18N::Game::RestorationIsDeletingThe);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
        mu_swprintf(szText, I18N::Game::ReinforcementOption);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
        mu_swprintf(szText, I18N::Game::OfTheWeapons);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
        mu_swprintf(szText, I18N::Game::ForRestoringReinforcedItem);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
        mu_swprintf(szText, I18N::Game::ReinforcementOptionHasToBe);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
        mu_swprintf(szText, I18N::Game::DeletedThroughRestoration);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
    }
    break;
    case SEASON3A::MIXTYPE_ELPIS:
        tone = "normal";
        mu_swprintf(szText, I18N::Game::GettingThroughRefiningProcess);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
        mu_swprintf(szText, I18N::Game::OfJewelOfHarmonyOrignal);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
        mu_swprintf(szText, I18N::Game::GemstoneWillGiveMorePower);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
        break;
    case SEASON3A::MIXTYPE_CHAOS_CARD: {

        tone = "warning";
        mu_swprintf(szText, I18N::Game::Warning2223);
        MixInventoryDetail::AppendMixText(guide, szText, tone);

        tone = "normal";
        mu_swprintf(szText, I18N::Game::CombinationsCanBeUsedOnceAtATime);
        MixInventoryDetail::AppendMixText(guide, szText, tone);

        tone = "normal";
        mu_swprintf(szText, I18N::Game::MoreThan2X4SpaceInInventoryIsNeeded);
        MixInventoryDetail::AppendMixText(guide, szText, tone);

        tone = "normal";
        mu_swprintf(szText, I18N::Game::YouCanAchieveSpecialItemsWithCombinations);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
    }
    break;
    case SEASON3A::MIXTYPE_CHERRYBLOSSOM: {

        tone = "warning";
        mu_swprintf(szText, I18N::Game::Warning2223);
        MixInventoryDetail::AppendMixText(guide, szText, tone);

        tone = "normal";
        mu_swprintf(szText, I18N::Game::_255GoldenCherryBlossomBranches);
        MixInventoryDetail::AppendMixText(guide, szText, tone);

        tone = "normal";
        mu_swprintf(szText, I18N::Game::OnlyTheSameTypeOfCherryBlossomsBranchesCanBeUploaded);
        MixInventoryDetail::AppendMixText(guide, szText, tone);

        tone = "normal";
        mu_swprintf(szText, I18N::Game::MoreThan2X4SpaceInInventoryIsNeeded);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
    }
    break;
    case SEASON3A::MIXTYPE_ATTACH_SOCKET:
        tone = "normal";
        mu_swprintf(szText, I18N::Game::SelectApplicableSocket);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
        break;
    case SEASON3A::MIXTYPE_DETACH_SOCKET:
        tone = "normal";
        mu_swprintf(szText, I18N::Game::SelectDestructibleSocket);
        MixInventoryDetail::AppendMixText(guide, szText, tone);
        break;
    default:
        break;
    }
}

void CNewUIMixInventory::SyncModernGeometry()
{
    const auto cell = m_ModernPanel.GridCell();
    if (m_pNewInventoryCtrl && cell.width > 0 && cell.height > 0)
        m_pNewInventoryCtrl->SetOwnerGeometry(cell.x, cell.y, cell.width, cell.height);
}
bool CNewUIMixInventory::IsMouseInModernPanel() const
{
    const auto rect = m_ModernPanel.PanelRect();
    return CheckMouseIn(static_cast<int>(std::floor(rect.x)), static_cast<int>(std::floor(rect.y)),
                        static_cast<int>(std::ceil(rect.width)),
                        static_cast<int>(std::ceil(rect.height)));
}
void CNewUIMixInventory::UpdateSocketSelection()
{
    const int count = g_MixRecipeMgr.GetFirstItemSocketCount();
    if (m_SelectedSocket >= count)
        m_SelectedSocket = -1;
    for (int i = 0; i < MAX_SOCKETS; ++i)
    {
        const auto id = "socket" + std::to_string(i);
        if (m_ModernPanel.TakeClick(id.c_str()) && i < count)
            m_SelectedSocket = i;
        m_ModernPanel.SetShown(id.c_str(), i < count);
        m_ModernPanel.SetFrame(id.c_str(), i == m_SelectedSocket ? 2 : 1);
        if (i >= count)
            continue;
        wchar_t option[128], text[160];
        if (g_MixRecipeMgr.GetFirstItemSocketSeedID(i) == SOCKET_EMPTY)
            mu_swprintf(option, L"%ls", I18N::Game::NoItemApplication);
        else
            g_SocketItemMgr.CreateSocketOptionText(option,
                                                   g_MixRecipeMgr.GetFirstItemSocketSeedID(i),
                                                   g_MixRecipeMgr.GetFirstItemSocketShpereLv(i));
        mu_swprintf(text, L"%d: %ls", i + 1, option);
        m_ModernPanel.SetText((id + "-label").c_str(), text);
    }
}

std::optional<bool> CNewUIMixInventory::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernPanel.ProcessInput(event);
}

SEASON3B::MyShopInventoryLegacyCalls::MyShopInventoryLegacyCalls(
    SessionKeeper &keeper, CNewUIMyShopInventory &owner) noexcept
    : SessionUiLegacyBindings(keeper), owner_(owner)
{
}

SEASON3B::CNewUIMyShopInventory::CNewUIMyShopInventory(SessionKeeper &keeper)
    : MyShopInventoryLegacyCalls(keeper, *this), renderUnit(RendererForConstruction()),
      m_SourceIndex(-1), m_TargetIndex(-1), m_EnablePersonalShop(false), m_OpenButtonLocked(false),
      m_bIsEnableInputValueTextBox(false), m_ModernPanel(keeper)
{
    m_pNewUIMng = NULL;
    m_pNewInventoryCtrl = NULL;
}

SEASON3B::CNewUIMyShopInventory::~CNewUIMyShopInventory()
{
    Release();
}

bool SEASON3B::CNewUIMyShopInventory::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng || NULL == g_pNewUI3DRenderMng || NULL == g_pNewItemMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_MYSHOP_INVENTORY, this);

    SetPos(x, y);

    m_pNewInventoryCtrl = renderUnit.CreateInventoryControl();
    if (false ==
        m_pNewInventoryCtrl->Create(GameDataForConstruction().Inventory(InventoryRole::MyShop),
                                    g_pNewUI3DRenderMng, this, 0, 0))
    {
        SAFE_DELETE(m_pNewInventoryCtrl);
        return false;
    }

    m_pNewInventoryCtrl->SetToolTipType(TOOLTIP_TYPE_MY_SHOP);
    m_pNewInventoryCtrl->SetRenderSlotFrame(false);
    m_pNewInventoryCtrl->SetOwnerRendered(true);
    m_ModernPanel.Create();
    ChangePersonal(m_EnablePersonalShop);

    Show(false);

    return true;
}

void SEASON3B::CNewUIMyShopInventory::Release()
{
    m_ModernPanel.Release();
    SAFE_DELETE(m_pNewInventoryCtrl);

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

void SEASON3B::CNewUIMyShopInventory::GetTitle(wchar_t *titletext)
{
    wcsncpy_s(titletext, PersonalShopDetail::iMAX_SHOPTITLE_MULTI, m_ShopTitle.c_str(), _TRUNCATE);
}

bool SEASON3B::CNewUIMyShopInventory::InsertItem(int iIndex, std::span<const BYTE> pbyItemPacket)
{
    if (m_pNewInventoryCtrl)
    {
        return m_pNewInventoryCtrl->AddItem(iIndex, pbyItemPacket);
    }

    return false;
}

void SEASON3B::CNewUIMyShopInventory::DeleteItem(int iIndex)
{
    if (m_pNewInventoryCtrl)
    {
        ITEM *pItem = m_pNewInventoryCtrl->FindItem(iIndex);
        if (pItem != NULL)
            m_pNewInventoryCtrl->RemoveItem(pItem);
    }
}

void SEASON3B::CNewUIMyShopInventory::DeleteAllItems()
{
    if (m_pNewInventoryCtrl)
    {
        m_pNewInventoryCtrl->RemoveAllItems();
    }
}

ITEM *SEASON3B::CNewUIMyShopInventory::FindItem(int iLinealPos)
{
    if (m_pNewInventoryCtrl)
        return m_pNewInventoryCtrl->FindItem(iLinealPos);
    return NULL;
}

void SEASON3B::CNewUIMyShopInventory::ChangePersonal(bool state)
{
    m_EnablePersonalShop = state;
}

void SEASON3B::CNewUIMyShopInventory::OpenButtonLock()
{
    m_OpenButtonLocked = true;
}

void SEASON3B::CNewUIMyShopInventory::OpenButtonUnLock()
{
    m_OpenButtonLocked = false;
}

void SEASON3B::CNewUIMyShopInventory::UpdateOpenButtonLockForCurrentMap()
{
    if (gMapManager.IsCursedTemple())
    {
        OpenButtonLock();
    }
    else if (!IsEnablePersonalShop())
    {
        OpenButtonUnLock();
    }
}

const bool SEASON3B::CNewUIMyShopInventory::IsEnablePersonalShop() const
{
    return m_EnablePersonalShop;
}

bool SEASON3B::CNewUIMyShopInventory::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_MYSHOP_INVENTORY) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_MYSHOP_INVENTORY);
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }

    return true;
}

bool SEASON3B::CNewUIMyShopInventory::MyShopInventoryProcess()
{
    CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();
    return IsMouseInModernPanel() && m_pNewInventoryCtrl != nullptr && pPickedItem != nullptr &&
           IsRelease(VK_LBUTTON) && HandlePickedItemDrop(*pPickedItem);
}

bool SEASON3B::CNewUIMyShopInventory::HandlePickedItemDrop(CNewUIPickedItem &pickedItem)
{
    ITEM *const item = pickedItem.GetItem();
    const int source = pickedItem.GetSourceLinealPos();
    const int target = pickedItem.GetTargetLinealPos(m_pNewInventoryCtrl);

#ifndef KJH_FIX_CHANGE_ITEM_PRICE_IN_PERSONAL_SHOP
    enum class DropColor
    {
        Allowed,
        Banned
    };
    static const UI::Modern::RmlUiDesign design(
        "Data/UI/PC/Inventory/private_store.rml",
        {"PrivateStore-DropAllowedColor", "PrivateStore-DropBannedColor"});
    const auto color =
        design.Values(IsPersonalShopBan(item) ? DropColor::Banned : DropColor::Allowed);
    m_pNewInventoryCtrl->SetSquareColorNormal(color[0], color[1], color[2]);
#endif

    if (target == -1)
        return true;

    CNewUIInventoryCtrl *const owner = pickedItem.GetOwnerInventory();
    if (owner == g_pMyInventory->GetInventoryCtrl() || owner == nullptr)
    {
        if (IsPersonalShopBan(item))
        {
            g_pSystemLogBox->AddText(I18N::Game::ThisItemIsNotAllowedToUseThePrivateStore,
                                     SEASON3B::TYPE_ERROR_MESSAGE);
            return true;
        }
        if (!m_pNewInventoryCtrl->CanMove(target, item))
            return false;
        ChangeSourceIndex(source);
        ChangeTargetIndex(target);
        CreateMessageBox(MSGBOX_LAYOUT_CLASS(CPersonalShopItemValueMsgBoxLayout, SessionOrigin()));
        SetInputValueTextBox(true);
        pickedItem.HidePickedItem();
        return true;
    }

    if (owner != m_pNewInventoryCtrl || !m_pNewInventoryCtrl->CanMove(target, item))
        return false;
    ChangeSourceIndex(source);
    ChangeTargetIndex(target);
    SendRequestEquipmentItem(STORAGE_TYPE::MYSHOP, source, item, STORAGE_TYPE::MYSHOP, target);
    return true;
}

bool SEASON3B::CNewUIMyShopInventory::UpdateMouseEvent()
{
    if (ProcessModernChanges())
        return false;
    SyncModernInventoryGeometry();
    if (m_pNewInventoryCtrl && false == m_pNewInventoryCtrl->UpdateMouseEvent())
        return false;
    if (MyShopInventoryProcess())
        return false;
    return !IsMouseInModernPanel();
}

bool SEASON3B::CNewUIMyShopInventory::Update()
{
    (void)ProcessModernChanges();
    m_ModernVisible = IsVisible();
    if (m_ModernVisible)
    {
        m_ModernContent = BuildModernContent();
        SyncModernInventoryGeometry();
    }
    if (m_pNewInventoryCtrl && false == m_pNewInventoryCtrl->Update())
        return false;
    return true;
}

bool SEASON3B::CNewUIMyShopInventory::ProcessModernChanges()
{
    const auto changes = m_ModernPanel.TakeChanges();
    if (changes.focus && m_pNewUIMng != nullptr)
        m_pNewUIMng->BringToFront(this);
    if (changes.shopName)
        m_ShopTitle = *changes.shopName;
    if (changes.dismiss)
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_MYSHOP_INVENTORY);
    if (changes.open)
        OpenShop();
    if (changes.close)
        CloseShop();
    if (changes.clearSlot)
    {
        MouseRButton = false;
        MouseRButtonPop = false;
        MouseRButtonPush = false;
        OpenPriceEditorForSlot(static_cast<int>(*changes.clearSlot));
    }
    return changes.dismiss || changes.open || changes.close || changes.clearSlot.has_value();
}

void SEASON3B::CNewUIMyShopInventory::OpenShop()
{
    if (IsExistUndecidedPrice() || m_ShopTitle.empty())
    {
        g_pSystemLogBox->AddText(I18N::Game::ThereSNoStoreNameOrItemPrice,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
        return;
    }
    if (!m_EnablePersonalShop)
    {
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CPersonalshopCreateMsgBoxLayout, SessionOrigin()));
        return;
    }
    wcscpy(g_szPersonalShopTitle, m_ShopTitle.c_str());
    SocketClient->ToGameServer()->SendPlayerShopOpen(m_ShopTitle.c_str());
    CloseShopWindows();
}

void SEASON3B::CNewUIMyShopInventory::CloseShop()
{
    SocketClient->ToGameServer()->SendPlayerShopClose();
    CloseShopWindows();
}

void SEASON3B::CNewUIMyShopInventory::CloseShopWindows()
{
    g_pNewUISystem->Hide(SEASON3B::INTERFACE_MYSHOP_INVENTORY);
    g_pNewUISystem->Hide(SEASON3B::INTERFACE_INVENTORY);
    g_pNewUISystem->Hide(SEASON3B::INTERFACE_INVENTORY_EXT);
}

bool SEASON3B::CNewUIMyShopInventory::OpenPriceEditorForSlot(int slot)
{
    const int inventorySlot = MAX_MY_INVENTORY_EX_INDEX + slot;
    ITEM *const item = FindItem(inventorySlot);
    const int itemIndex = GetItemInventoryIndex(item);
    if (itemIndex < 0)
        return false;
    ChangeSourceIndex(itemIndex);
    ChangeTargetIndex(-1);
    CreateMessageBox(MSGBOX_LAYOUT_CLASS(CPersonalShopItemValueMsgBoxLayout, SessionOrigin()));
    SetInputValueTextBox(true);
    return true;
}

void SEASON3B::CNewUIMyShopInventory::SyncModernInventoryGeometry()
{
    if (m_pNewInventoryCtrl == nullptr)
        return;
    const auto grid = m_ModernPanel.InventoryGridRect(static_cast<int>(ModernUiViewportWidth()),
                                                      static_cast<int>(ModernUiViewportHeight()));
    if (grid.width <= 0.0f || grid.height <= 0.0f)
        return;
    m_pNewInventoryCtrl->SetOwnerGeometry(
        grid.x, grid.y, grid.width / UI::Modern::PC::Inventory::RmlPrivateStorePanel::GridColumns(),
        grid.height / UI::Modern::PC::Inventory::RmlPrivateStorePanel::GridRows());
}

bool SEASON3B::CNewUIMyShopInventory::IsMouseInModernPanel() const
{
    const auto rect = m_ModernPanel.ReferenceRect(static_cast<int>(ModernUiViewportWidth()),
                                                  static_cast<int>(ModernUiViewportHeight()));
    return CheckMouseIn(static_cast<int>(std::floor(rect.x)), static_cast<int>(std::floor(rect.y)),
                        static_cast<int>(std::ceil(rect.width)),
                        static_cast<int>(std::ceil(rect.height)));
}

UI::Modern::PC::Inventory::RmlPrivateStoreContent SEASON3B::CNewUIMyShopInventory::
    BuildModernContent() const
{
    UI::Modern::PC::Inventory::RmlPrivateStoreContent content;
    content.title = I18N::Game::PersonalStore;
    content.shopNameLabel = I18N::Game::StoreName;
    content.shopName = m_ShopTitle;
    content.openLabel = I18N::Game::Open1107;
    content.closeLabel = I18N::Game::Close388;
    content.shopOpen = m_EnablePersonalShop;
    content.openEnabled = !m_OpenButtonLocked;
    return content;
}

float SEASON3B::CNewUIMyShopInventory::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

std::optional<bool> SEASON3B::CNewUIMyShopInventory::ProcessModernUiInput(
    const SessionInputEvent &event)
{
    return m_ModernPanel.RouteInput(event);
}

std::optional<UI::Modern::RmlTextInputArea> SEASON3B::CNewUIMyShopInventory::ModernTextInputArea()
    const
{
    return m_ModernPanel.TextInputArea();
}

void SEASON3B::CNewUIMyShopInventory::ClosingProcess()
{
    g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();
    g_pMyInventory->ChangeMyShopButtonStateOpen();
    SetFocus(g_hWnd);
    ReleaseTextInputFocus();
}

int SEASON3B::CNewUIMyShopInventory::GetPointedItemIndex()
{
    return m_pNewInventoryCtrl->GetPointedSquareIndex();
}

int SEASON3B::CNewUIMyShopInventory::GetItemInventoryIndex(ITEM *pItem)
{
    return m_pNewInventoryCtrl->GetIndexByItem(pItem);
}

void SEASON3B::CNewUIMyShopInventory::ResetSubject()
{
    m_ShopTitle.clear();
}

bool SEASON3B::CNewUIMyShopInventory::IsEnableInputValueTextBox()
{
    return m_bIsEnableInputValueTextBox;
}

void SEASON3B::CNewUIMyShopInventory::SetInputValueTextBox(bool bIsEnable)
{
    m_bIsEnableInputValueTextBox = bIsEnable;
}

// ?

namespace ItemRulesDetail
{
bool IsDivineArchangelWeaponItem(int itemType)
{
    return itemType == ITEM_DIVINE_SWORD_OF_ARCHANGEL || itemType == ITEM_DIVINE_CB_OF_ARCHANGEL ||
           itemType == ITEM_DIVINE_STAFF_OF_ARCHANGEL ||
           itemType == ITEM_DIVINE_STICK_OF_ARCHANGEL ||
           itemType == ITEM_DIVINE_SCEPTER_OF_ARCHANGEL;
}
} // namespace ItemRulesDetail

namespace ItemRulesDetail
{
bool IsDivineArchangelWeaponModel(int modelType)
{
    return modelType == MODEL_DIVINE_STAFF_OF_ARCHANGEL ||
           modelType == MODEL_DIVINE_STICK_OF_ARCHANGEL ||
           modelType == MODEL_DIVINE_SWORD_OF_ARCHANGEL ||
           modelType == MODEL_DIVINE_CB_OF_ARCHANGEL ||
           modelType == MODEL_DIVINE_SCEPTER_OF_ARCHANGEL;
}
} // namespace ItemRulesDetail

#ifdef _PVP_ADD_MOVE_SCROLL
extern CMurdererMove g_MurdererMove;
#endif // _PVP_ADD_MOVE_SCROLL

int getLevelGeneration(int level, unsigned int *color)
{
    int lvl;
    if (level >= 300)
    {
        lvl = 300;
        *color = (255 << 24) + (255 << 16) + (153 << 8) + (255);
    }
    else if (level >= 200)
    {
        lvl = 200;
        *color = (255 << 24) + (255 << 16) + (230 << 8) + (210);
    }
    else if (level >= 100)
    {
        lvl = 100;
        *color = (255 << 24) + (24 << 16) + (201 << 8) + (0);
    }
    else if (level >= 50)
    {
        lvl = 50;
        *color = (255 << 24) + (0 << 16) + (150 << 8) + (255);
    }
    else
    {
        lvl = 10;
        *color = (255 << 24) + (0 << 16) + (0 << 8) + (255);
    }
    return lvl;
}
bool SessionUiUnit::IsCanUseItem()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_STORAGE) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_TRADE))
    {
        return false;
    }
    else
    {
        return true;
    }
}

bool SessionUiUnit::IsCanTrade()
{
    if (g_pUIManager->IsOpen(INTERFACE_PERSONALSHOPSALE) ||
        g_pUIManager->IsOpen(INTERFACE_PERSONALSHOPPURCHASE))
    {
        return false;
    }
    return true;
}

bool IsRequireClassRenderItem(const short sType)
{
    if (sType == ITEM_WEAPON_OF_ARCHANGEL || sType == ITEM_ARMOR_OF_GUARDSMAN ||
        sType == ITEM_WING + 26 ||
        (sType >= ITEM_PACKED_JEWEL_OF_BLESS && sType <= ITEM_PACKED_JEWEL_OF_SOUL) ||
        (sType >= ITEM_HELPER + 43 && sType <= ITEM_HELPER + 45) ||
        sType == ITEM_TRANSFORMATION_RING ||
        (sType >= ITEM_ELITE_TRANSFER_SKELETON_RING &&
         sType <= ITEM_GAME_MASTER_TRANSFORMATION_RING) ||
        sType == ITEM_HORN_OF_FENRIR || sType == ITEM_JEWEL_OF_CHAOS ||
        sType == ITEM_RED_RIBBON_BOX || sType == ITEM_GREEN_RIBBON_BOX ||
        sType == ITEM_BLUE_RIBBON_BOX)
    {
        return false;
    }

    if ((sType >= ITEM_HELPER + 43 && sType <= ITEM_HELPER + 45) ||
        (sType >= ITEM_HELPER + 46 && sType <= ITEM_HELPER + 48) ||
        (sType >= ITEM_HELPER + 125 && sType <= ITEM_HELPER + 127) || (sType == ITEM_POTION + 54) ||
        (sType >= ITEM_POTION + 58 && sType <= ITEM_POTION + 62) || (sType == ITEM_POTION + 53) ||
        (sType >= ITEM_POTION + 70 && sType <= ITEM_POTION + 71) ||
        (sType >= ITEM_POTION + 72 && sType <= ITEM_POTION + 77) ||
        (sType >= ITEM_TYPE_CHARM_MIXWING + EWS_BEGIN &&
         sType <= ITEM_TYPE_CHARM_MIXWING + EWS_END) ||
        (sType == ITEM_HELPER + 59) || (sType >= ITEM_HELPER + 54 && sType <= ITEM_HELPER + 58) ||
        (sType >= ITEM_POTION + 78 && sType <= ITEM_POTION + 82) || (sType == ITEM_HELPER + 60) ||
        (sType == ITEM_HELPER + 61) || (sType == ITEM_POTION + 91) || (sType == ITEM_POTION + 94) ||
        (sType >= ITEM_POTION + 92 && sType <= ITEM_POTION + 93) || (sType == ITEM_POTION + 95) ||
        (sType >= ITEM_HELPER + 62 && sType <= ITEM_HELPER + 63) ||
        (sType >= ITEM_POTION + 97 && sType <= ITEM_POTION + 98) || (sType == ITEM_POTION + 96) ||
        (sType == ITEM_DEMON || sType == ITEM_SPIRIT_OF_GUARDIAN) || (sType == ITEM_PET_RUDOLF) ||
        (sType == ITEM_SNOWMAN_TRANSFORMATION_RING) || (sType == ITEM_PANDA_TRANSFORMATION_RING) ||
        (sType == ITEM_SKELETON_TRANSFORMATION_RING) || (sType == ITEM_HELPER + 69) ||
        (sType == ITEM_HELPER + 70) ||
        (sType == ITEM_HELPER + 71 || sType == ITEM_HELPER + 72 || sType == ITEM_HELPER + 73 ||
         sType == ITEM_HELPER + 74 || sType == ITEM_HELPER + 75) ||
        (sType == ITEM_PET_PANDA) || (sType == ITEM_PET_UNICORN) || sType == ITEM_HELPER + 81 ||
        sType == ITEM_HELPER + 82 || sType == ITEM_HELPER + 93 || sType == ITEM_HELPER + 94 ||
        sType == ITEM_HELPER + 121 || (sType >= ITEM_POTION + 145 && sType <= ITEM_POTION + 150)
#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        || g_pMyInventory->IsInvenItem(sType)
#endif //LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        || (sType == ITEM_POTION + 133))
    {
        return false;
    }

    return true;
}

void SessionUiUnit::RequireClass(ITEM_ATTRIBUTE *pItem)
{
    if (pItem == NULL)
        return;

    BYTE byFirstClass = gCharacterManager.GetBaseClass(Hero->Class);
    BYTE byStepClass = gCharacterManager.GetStepClass(Hero->Class);

    int iTextColor = 0;

    TextListColor[TextNum + 2] = TextListColor[TextNum + 3] = TEXT_COLOR_WHITE;
    mu_swprintf(TextList[TextNum], L"\n");
    TextNum++;
    SkipNum++;

    int iCount = 0;
    for (int i = 0; i < MAX_CLASS; ++i)
    {
        if (pItem->RequireClass[i] == 1)
        {
            iCount++;
        }
    }
    if (iCount == MAX_CLASS)
        return;

    for (int i = 0; i < MAX_CLASS; ++i)
    {
        BYTE byRequireClass = pItem->RequireClass[i];

        if (byRequireClass == 0)
            continue;

        if (i == byFirstClass && byRequireClass <= byStepClass)
        {
            iTextColor = TEXT_COLOR_WHITE;
        }
        else
        {
            iTextColor = TEXT_COLOR_DARKRED;
        }

        switch (i)
        {
        case CLASS_WIZARD: {
            if (byRequireClass == 1)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS,
                            I18N::Game::DarkWizard);
                TextListColor[TextNum] = iTextColor;
            }
            else if (byRequireClass == 2)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS,
                            I18N::Game::SoulMaster);
                TextListColor[TextNum] = iTextColor;
            }
            else if (byRequireClass == 3)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS,
                            I18N::Game::GrandMaster);
                TextListColor[TextNum] = iTextColor;
            }

            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        case CLASS_KNIGHT: {
            if (byRequireClass == 1)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS,
                            I18N::Game::DarkKnight);
                TextListColor[TextNum] = iTextColor;
            }
            else if (byRequireClass == 2)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS,
                            I18N::Game::BladeKnight);
                TextListColor[TextNum] = iTextColor;
            }
            else if (byRequireClass == 3)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS,
                            I18N::Game::BladeMaster);
                TextListColor[TextNum] = iTextColor;
            }

            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        case CLASS_ELF: {
            if (byRequireClass == 1)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS, I18N::Game::Elf);
                TextListColor[TextNum] = iTextColor;
            }
            else if (byRequireClass == 2)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS, I18N::Game::MuseElf);
                TextListColor[TextNum] = iTextColor;
            }
            else if (byRequireClass == 3)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS, I18N::Game::HighElf);
                TextListColor[TextNum] = iTextColor;
            }

            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        case CLASS_DARK: {
            if (byRequireClass == 1)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS,
                            I18N::Game::MagicGladiator);
                TextListColor[TextNum] = iTextColor;
            }
            else if (byRequireClass == 3)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS,
                            I18N::Game::DualMaster);
                TextListColor[TextNum] = iTextColor;
            }

            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        case CLASS_DARK_LORD: {
            if (byRequireClass == 1)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS, I18N::Game::DarkLord);
                TextListColor[TextNum] = iTextColor;
            }
            else if (byRequireClass == 3)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS,
                            I18N::Game::LordEmperor);
                TextListColor[TextNum] = iTextColor;
            }

            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        case CLASS_SUMMONER: {
            if (byRequireClass == 1)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS, I18N::Game::Summoner);
                TextListColor[TextNum] = iTextColor;
            }
            else if (byRequireClass == 2)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS,
                            I18N::Game::BloodySummoner);
                TextListColor[TextNum] = iTextColor;
            }
            else if (byRequireClass == 3)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS,
                            I18N::Game::DimensionMaster);
                TextListColor[TextNum] = iTextColor;
            }

            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        case CLASS_RAGEFIGHTER: {
            if (byRequireClass == 1)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS,
                            I18N::Game::RageFighter);
                TextListColor[TextNum] = iTextColor;
            }
            else if (byRequireClass == 3)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS,
                            I18N::Game::FistMaster);
                TextListColor[TextNum] = iTextColor;
            }
            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        }
    }
}

unsigned int getGoldColor(DWORD Gold)
{
    if (Gold >= 10000000)
    {
        return (255 << 24) + (0 << 16) + (0 << 8) + (255);
    }
    else if (Gold >= 1000000)
    {
        return (255 << 24) + (0 << 16) + (150 << 8) + (255);
    }
    else if (Gold >= 100000)
    {
        return (255 << 24) + (24 << 16) + (201 << 8) + (0);
    }

    return (255 << 24) + (150 << 16) + (220 << 8) + (255);
}

void ConvertGold(double dGold, wchar_t *szText, int iDecimals /*= 0*/)
{
    wchar_t szTemp[256];
    int iCipherCnt = 0;
    auto dwValueTemp = (DWORD)dGold;

    while (dwValueTemp / 1000 > 0)
    {
        iCipherCnt = iCipherCnt + 3;
        dwValueTemp = dwValueTemp / 1000;
    }

    mu_swprintf(szText, L"%d", dwValueTemp);

    while (iCipherCnt > 0)
    {
        dwValueTemp = (DWORD)dGold;
        dwValueTemp = (dwValueTemp % (int)pow(10.f, (float)iCipherCnt)) /
                      (int)pow(10.f, (float)(iCipherCnt - 3));
        mu_swprintf(szTemp, L",%03d", dwValueTemp);
        wcscat(szText, szTemp);
        iCipherCnt = iCipherCnt - 3;
    }

    if (iDecimals > 0)
    {
        dwValueTemp = (int)(dGold * pow(10.f, (float)iDecimals)) % (int)pow(10.f, (float)iDecimals);
        mu_swprintf(szTemp, L".%d", dwValueTemp);
        wcscat(szText, szTemp);
    }
}

void ConvertGold64(__int64 Gold, wchar_t *Text)
{
    int Gold1 = Gold % 1000;
    int Gold2 = Gold % 1000000 / 1000;
    int Gold3 = Gold % 1000000000 / 1000000;
    int Gold4 = Gold % 1000000000000 / 1000000000;
    int Gold5 = Gold % 1000000000000000 / 1000000000000;
    int Gold6 = Gold / 1000000000000000;
    if (Gold >= 1000000000000000)
        mu_swprintf(Text, L"%d,%03d,%03d,%03d,%03d,%03d", Gold6, Gold5, Gold4, Gold3, Gold2, Gold1);
    else if (Gold >= 1000000000000)
        mu_swprintf(Text, L"%d,%03d,%03d,%03d,%03d", Gold5, Gold4, Gold3, Gold2, Gold1);
    else if (Gold >= 1000000000)
        mu_swprintf(Text, L"%d,%03d,%03d,%03d", Gold4, Gold3, Gold2, Gold1);
    else if (Gold >= 1000000)
        mu_swprintf(Text, L"%d,%03d,%03d", Gold3, Gold2, Gold1);
    else if (Gold >= 1000)
        mu_swprintf(Text, L"%d,%03d", Gold2, Gold1);
    else
        mu_swprintf(Text, L"%d", Gold1);
}

void SessionUiUnit::ConvertTaxGold(DWORD Gold, wchar_t *Text)
{
    Gold += ((LONGLONG)Gold * g_pNPCShop->GetTaxRate()) / 100;

    int Gold1 = Gold % 1000;
    int Gold2 = Gold % 1000000 / 1000;
    int Gold3 = Gold % 1000000000 / 1000000;
    int Gold4 = Gold / 1000000000;
    if (Gold >= 1000000000)
        mu_swprintf(Text, L"%d,%03d,%03d,%03d", Gold4, Gold3, Gold2, Gold1);
    else if (Gold >= 1000000)
        mu_swprintf(Text, L"%d,%03d,%03d", Gold3, Gold2, Gold1);
    else if (Gold >= 1000)
        mu_swprintf(Text, L"%d,%03d", Gold2, Gold1);
    else
        mu_swprintf(Text, L"%d", Gold1);
}

void SessionUiUnit::ConvertChaosTaxGold(DWORD Gold, wchar_t *Text)
{
    Gold += ((LONGLONG)Gold * g_nChaosTaxRate) / 100;

    int Gold1 = Gold % 1000;
    int Gold2 = Gold % 1000000 / 1000;
    int Gold3 = Gold % 1000000000 / 1000000;
    int Gold4 = Gold / 1000000000;
    if (Gold >= 1000000000)
        mu_swprintf(Text, L"%d,%03d,%03d,%03d", Gold4, Gold3, Gold2, Gold1);
    else if (Gold >= 1000000)
        mu_swprintf(Text, L"%d,%03d,%03d", Gold3, Gold2, Gold1);
    else if (Gold >= 1000)
        mu_swprintf(Text, L"%d,%03d", Gold2, Gold1);
    else
        mu_swprintf(Text, L"%d", Gold1);
}

int64_t SessionUiUnit::ConvertRepairGold(int64_t Gold, int Durability, int MaxDurability,
                                         short Type, wchar_t *Text)
{
    int64_t repairGold = 0;

    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_NPCSHOP) && g_pNPCShop->IsRepairShop())
    {
        repairGold = CalcRepairCost(Gold, Durability, MaxDurability, Type, false);
    }
    else if (g_pMyInventory->IsVisible() && !g_pNPCShop->IsVisible())
    {
        repairGold = CalcRepairCost(Gold, Durability, MaxDurability, Type, true);
    }

    ConvertGold(repairGold, Text);

    return repairGold;
}

void SessionUiUnit::RepairAllGold()
{
    wchar_t text[100];

    AllRepairGold = 0;

    for (int i = 0; i < MAX_EQUIPMENT; ++i)
    {
        if (CharacterMachine->Equipment[i].Type != -1)
        {
            ITEM *ip = &CharacterMachine->Equipment[i];
            ITEM_ATTRIBUTE *p = &ItemAttribute[ip->Type];

            int Level = ip->Level;
            int maxDurability = CalcMaxDurability(ip, p, Level);

            if (IsRepairBan(ip) == true)
            {
                continue;
            }

            //. check durability
            if (ip->Durability < maxDurability)
            {
                int gold = ConvertRepairGold(ItemValue(ip, 2), ip->Durability, maxDurability,
                                             ip->Type, text);

                if (Check_LuckyItem(ip->Type))
                    gold = 0;
                AllRepairGold += gold;
            }
        }
    }

    ITEM *pItem = NULL;
    for (int i = 0; i < (int)(g_pMyInventory->GetInventoryCtrl()->GetNumberOfItems()); ++i)
    {
        pItem = g_pMyInventory->GetInventoryCtrl()->GetItem(i);

        if (pItem)
        {
            ITEM_ATTRIBUTE *p = &ItemAttribute[pItem->Type];

            int Level = pItem->Level;
            int maxDurability = CalcMaxDurability(pItem, p, Level);

            if (pItem->Type >= ITEM_POTION + 55 && pItem->Type <= ITEM_POTION + 57)
            {
                continue;
            }
            //. item filtering
            if ((pItem->Type >= ITEM_HELPER && pItem->Type <= ITEM_DARK_RAVEN_ITEM) ||
                pItem->Type == ITEM_TRANSFORMATION_RING || pItem->Type == ITEM_SPIRIT)
                continue;
            if (pItem->Type == ITEM_BOLT || pItem->Type == ITEM_ARROWS ||
                pItem->Type >= ITEM_POTION)
                continue;
            if (pItem->Type >= ITEM_ORB_OF_TWISTING_SLASH && pItem->Type <= ITEM_ORB_OF_DEATH_STAB)
                continue;
            if ((pItem->Type >= ITEM_LOCHS_FEATHER && pItem->Type <= ITEM_WEAPON_OF_ARCHANGEL) ||
                pItem->Type == ITEM_POTION + 21)
                continue;
            if (pItem->Type == ITEM_WIZARDS_RING)
                continue;
            if (pItem->Type == ITEM_MOONSTONE_PENDANT)
                continue;

            if (pItem->Type >= ITEM_HELPER + 46 && pItem->Type <= ITEM_HELPER + 48)
            {
                continue;
            }
            if (pItem->Type >= ITEM_HELPER + 125 && pItem->Type <= ITEM_HELPER + 127)
            {
                continue;
            }
            if (pItem->Type >= ITEM_POTION + 145 && pItem->Type <= ITEM_POTION + 150)
            {
                continue;
            }
            if (pItem->Type >= ITEM_POTION + 58 && pItem->Type <= ITEM_POTION + 62)
            {
                continue;
            }
            if (pItem->Type == ITEM_POTION + 53)
            {
                continue;
            }
            if (pItem->Type == ITEM_HELPER + 43 || pItem->Type == ITEM_HELPER + 44 ||
                pItem->Type == ITEM_HELPER + 45)
            {
                continue;
            }
            if (pItem->Type >= ITEM_POTION + 70 && pItem->Type <= ITEM_POTION + 71)
            {
                continue;
            }
            if (pItem->Type >= ITEM_POTION + 72 && pItem->Type <= ITEM_POTION + 77)
            {
                continue;
            }
            if (pItem->Type == ITEM_HELPER + 59)
            {
                continue;
            }
            if (pItem->Type >= ITEM_HELPER + 54 && pItem->Type <= ITEM_HELPER + 58)
            {
                continue;
            }
            if (pItem->Type == ITEM_HELPER + 60)
            {
                continue;
            }
            if (pItem->Type == ITEM_HELPER + 61)
            {
                continue;
            }
            if (pItem->Type == ITEM_POTION + 91)
            {
                continue;
            }
            if (pItem->Type >= ITEM_POTION + 92 && pItem->Type <= ITEM_POTION + 93)
            {
                continue;
            }
            if (pItem->Type == ITEM_POTION + 95)
            {
                continue;
            }
            if (pItem->Type == ITEM_POTION + 95)
            {
                continue;
            }
            if (pItem->Type >= ITEM_HELPER + 62 && pItem->Type <= ITEM_HELPER + 63)
            {
                continue;
            }
            if (pItem->Type >= ITEM_POTION + 97 && pItem->Type <= ITEM_POTION + 98)
            {
                continue;
            }
            if (pItem->Type == ITEM_POTION + 140)
            {
                continue;
            }
            if (pItem->Type == ITEM_POTION + 96)
            {
                continue;
            }
            if (pItem->Type == ITEM_DEMON || pItem->Type == ITEM_SPIRIT_OF_GUARDIAN)
            {
                continue;
            }
            if (pItem->Type == ITEM_PET_RUDOLF)
            {
                continue;
            }
            if (pItem->Type == ITEM_PET_PANDA)
            {
                continue;
            }
            if (pItem->Type == ITEM_PET_UNICORN)
            {
                continue;
            }
            if (pItem->Type == ITEM_PET_SKELETON)
            {
                continue;
            }
            if (pItem->Type == ITEM_SNOWMAN_TRANSFORMATION_RING)
            {
                continue;
            }
            if (pItem->Type == ITEM_PANDA_TRANSFORMATION_RING)
            {
                continue;
            }
            if (pItem->Type == ITEM_SKELETON_TRANSFORMATION_RING)
            {
                continue;
            }
            if (pItem->Type == ITEM_HELPER + 69)
                continue;
            if (pItem->Type == ITEM_HELPER + 70)
                continue;

            if (pItem->Type == ITEM_HORN_OF_FENRIR)
                continue;

            if (pItem->Type == ITEM_HELPER + 66)
                continue;

            if (pItem->Type == ITEM_HELPER + 71 || pItem->Type == ITEM_HELPER + 72 ||
                pItem->Type == ITEM_HELPER + 73 || pItem->Type == ITEM_HELPER + 74 ||
                pItem->Type == ITEM_HELPER + 75)
                continue;

            if (pItem->Type == ITEM_HELPER + 81)
                continue;
            if (pItem->Type == ITEM_HELPER + 82)
                continue;
            if (pItem->Type == ITEM_HELPER + 93)
                continue;
            if (pItem->Type == ITEM_HELPER + 94)
                continue;

            if (pItem->Type >= ITEM_TYPE_CHARM_MIXWING + EWS_BEGIN &&
                pItem->Type <= ITEM_TYPE_CHARM_MIXWING + EWS_END)
            {
                continue;
            }
            if (pItem->Type == ITEM_HELPER + 97 || pItem->Type == ITEM_HELPER + 98 ||
                pItem->Type == ITEM_POTION + 91)
                continue;

            if (pItem->Type == ITEM_HELPER + 121)
                continue;

#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
            if (g_pMyInventory->IsInvenItem(pItem->Type))
                continue;

#endif //LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY

            if (pItem->Type >= ITEM_WING + 130 && pItem->Type <= ITEM_WING + 134)
                continue;
            if (pItem->Type == ITEM_HELPER + 109)
                continue;
            if (pItem->Type == ITEM_HELPER + 110)
                continue;
            if (pItem->Type == ITEM_HELPER + 111)
                continue;
            if (pItem->Type == ITEM_HELPER + 112)
                continue;
            if (pItem->Type == ITEM_HELPER + 113)
                continue;
            if (pItem->Type == ITEM_HELPER + 114)
                continue;
            if (pItem->Type == ITEM_HELPER + 115)
                continue;
            if (pItem->Type == ITEM_HELPER + 107)
                continue;

            if (Check_ItemAction(pItem, eITEM_REPAIR))
                continue;

            //. check durability
            if (pItem->Durability < maxDurability)
            {
                int gold = ConvertRepairGold(ItemValue(pItem, 2), pItem->Durability, maxDurability,
                                             pItem->Type, text);
                if (Check_LuckyItem(pItem->Type))
                    gold = 0;
                AllRepairGold += gold;
            }
        }
    }
}
namespace ItemRulesDetail
{

void SetDescriptorTextColor(ItemRulesDetail::GroundItemLabelDescriptor &descriptor, float red,
                            float green, float blue)
{
    descriptor.TextColor =
        ItemRulesDetail::MakeRgba(static_cast<BYTE>(red * 255.f), static_cast<BYTE>(green * 255.f),
                                  static_cast<BYTE>(blue * 255.f), 255);
}

void SetDescriptorYellowTextColor(ItemRulesDetail::GroundItemLabelDescriptor &descriptor)
{
    SetDescriptorTextColor(descriptor, 1.f, 0.8f, 0.1f);
}

void SetDescriptorGrayTextColor(ItemRulesDetail::GroundItemLabelDescriptor &descriptor)
{
    SetDescriptorTextColor(descriptor, 0.7f, 0.7f, 0.7f);
}

void SetDescriptorOrangeTextColor(ItemRulesDetail::GroundItemLabelDescriptor &descriptor)
{
    SetDescriptorTextColor(descriptor, 0.9f, 0.53f, 0.13f);
}

GroundItemLabelCacheKey BuildGroundItemLabelCacheKey(OBJECT *o, ITEM *ip)
{
    GroundItemLabelCacheKey key;
    key.Type = o->Type;
    key.Level = ip->Level;
    key.ExcellentFlags = static_cast<BYTE>(ip->ExcellentFlags);
    key.AncientDiscriminator = static_cast<BYTE>(ip->AncientDiscriminator);
    key.FeatureFlags = 0;
    if (ip->HasSkill)
    {
        key.FeatureFlags |= 1;
    }
    if (ip->HasLuck)
    {
        key.FeatureFlags |= 2;
    }
    if (ip->OptionLevel > 0)
    {
        key.FeatureFlags |= 4;
    }
    return key;
}

int GetNextPowerOfTwo(int value)
{
    int result = 1;
    while (result < value)
    {
        result <<= 1;
    }

    return result;
}

} // namespace ItemRulesDetail

namespace ItemRulesDetail
{
bool HasSingleColumnLegacyPanel(SEASON3B::CNewUISystem &system)
{
    return system.IsVisible(SEASON3B::INTERFACE_LUCKYCOIN_REGISTRATION) ||
           system.IsVisible(SEASON3B::INTERFACE_GUARDSMAN) ||
           system.IsVisible(SEASON3B::INTERFACE_SENATUS) ||
           system.IsVisible(SEASON3B::INTERFACE_SERVERDIVISION) ||
           system.IsVisible(SEASON3B::INTERFACE_GATESWITCH) ||
           system.IsVisible(SEASON3B::INTERFACE_CATAPULT) ||
           system.IsVisible(SEASON3B::INTERFACE_DEVILSQUARE) ||
           system.IsVisible(SEASON3B::INTERFACE_GOLD_BOWMAN_LENA) ||
           system.IsVisible(SEASON3B::INTERFACE_DOPPELGANGER_NPC);
}
} // namespace ItemRulesDetail

int SessionUiUnit::GetScreenWidth()
{
    // Movable RmlUI windows overlay the world; only retained docked windows
    // reserve a legacy column in the camera viewport.
    constexpr int LegacyPanelWidth = 190;
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_REFINERY))
        return REFERENCE_WIDTH - LegacyPanelWidth * 2;
    return ItemRulesDetail::HasSingleColumnLegacyPanel(*g_pNewUISystem)
               ? REFERENCE_WIDTH - LegacyPanelWidth
               : REFERENCE_WIDTH;
}

void SessionUiUnit::ClearInventory()
{
    for (int i = 0; i < MAX_EQUIPMENT; i++)
    {
        CharacterMachine->Equipment[i].Type = -1;
        CharacterMachine->Equipment[i].Number = 0;
    }
    for (int i = 0; i < MAX_INVENTORY; i++)
    {
        Inventory[i].Type = -1;
        Inventory[i].Number = 0;
    }
    for (int i = 0; i < MAX_INVENTORY_EXT; i++)
    {
        InventoryExt[i].Type = -1;
        InventoryExt[i].Number = 0;
    }
    for (int i = 0; i < MAX_SHOP_INVENTORY; i++)
    {
        ShopInventory[i].Type = -1;
        ShopInventory[i].Number = 0;
    }

    Init();
}

void SessionUiUnit::SetItemColor(int index, ITEM *Inv, int color)
{
    int Width = ItemAttribute[Inv[index].Type].Width;
    int Height = ItemAttribute[Inv[index].Type].Height;

    for (int k = Inv[index].y; k < Inv[index].y + Height; k++)
    {
        for (int l = Inv[index].x; l < Inv[index].x + Width; l++)
        {
            int Number = k * COLUMN_TRADE_INVENTORY + l;
            Inv[Number].Color = color;
        }
    }
}

void SessionUiUnit::InitPartyList()
{
    PartyNumber = 0;
    PartyKey = 0;
}

void SessionUiUnit::MoveServerDivisionInventory()
{
    if (!g_pUIManager->IsOpen(::INTERFACE_SERVERDIVISION))
        return;
    int x = REFERENCE_WIDTH - 190;
    int y = 0;
    int Width, Height;

    if (MouseX >= (int)(x) && MouseX < (int)(x + 190) && MouseY >= (int)(y) &&
        MouseY < (int)(y + 256 + 177))
    {
        MouseOnWindow = true;
    }

    Width = 16;
    Height = 16;
    x = InventoryStartX + 25;
    y = 240;
    if (MouseX >= x && MouseX < x + Width && MouseY >= y && MouseY < y + Height && MouseLButtonPush)
    {
        g_bServerDivisionAccept ^= true;

        MouseLButtonPush = false;
        MouseLButton = false;
    }

    if (g_bServerDivisionAccept)
    {
        Width = 120;
        Height = 24;
        x = (float)InventoryStartX + 35;
        y = 320;
        if (MouseX >= x && MouseX < x + Width && MouseY >= y && MouseY < y + Height &&
            MouseLButtonPush)
        {
            MouseLButtonPush = false;
            MouseLButton = false;
            AskYesOrNo = 4;
            OkYesOrNo = -1;

            ShowCheckBox(1, 448, MESSAGE_CHECK);
        }
    }

    Width = 120;
    Height = 24;
    x = (float)InventoryStartX + 35;
    y = 350;
    if (MouseX >= x && MouseX < x + Width && MouseY >= y && MouseY < y + Height && MouseLButtonPush)
    {
        MouseLButtonPush = false;
        MouseLButton = false;
        MouseUpdateTime = 0;
        MouseUpdateTimeMax = 6;

        SocketClient->ToGameServer()->SendCloseNpcRequest();
        g_pUIManager->CloseAll();
    }

    Width = 24;
    Height = 24;
    x = InventoryStartX + 25;
    y = InventoryStartY + 395;
    if (MouseX >= x && MouseX < x + Width && MouseY >= y && MouseY < y + Height)
    {
        if (MouseLButtonPush)
        {
            MouseLButtonPush = false;
            MouseUpdateTime = 0;
            MouseUpdateTimeMax = 6;

            g_bEventChipDialogEnable = EVENT_NONE;

            SocketClient->ToGameServer()->SendCloseNpcRequest();
            g_pUIManager->CloseAll();
        }
    }
}

void SessionUiUnit::MovePersonalShop()
{
    if ((g_pUIManager->IsOpen(INTERFACE_PERSONALSHOPSALE) ||
         g_pUIManager->IsOpen(INTERFACE_PERSONALSHOPPURCHASE)) &&
        g_iPShopWndType == PSHOPWNDTYPE_SALE)
    {
        if (g_iPersonalShopMsgType == 1)
        {
            if (OkYesOrNo == 1)
            {
                g_iPersonalShopMsgType = 0;
                OkYesOrNo = -1;
            }
            else if (OkYesOrNo == 2)
            {
                g_iPersonalShopMsgType = 0;
                OkYesOrNo = -1;
            }
        }
        g_ptPersonalShop.x = REFERENCE_WIDTH - 190 * 2;
        g_ptPersonalShop.y = 0;

        int Width = 56, Height = 24;
        int ButtonX = g_ptPersonalShop.x + 30, ButtonY = g_ptPersonalShop.y + 396;
        if (MouseX >= ButtonX && MouseX < ButtonX + Width && MouseY >= ButtonY &&
            MouseY < ButtonY + Height && MouseLButtonPush)
        {
            MouseLButtonPush = false;
            if (!IsExistUndecidedPrice() && wcslen(g_szPersonalShopTitle) > 0)
            {
                if (g_bEnablePersonalShop)
                {
                    SocketClient->ToGameServer()->SendPlayerShopOpen(g_szPersonalShopTitle);
                    g_pUIManager->Close(::INTERFACE_INVENTORY);
                }
                else
                {
                    SEASON3B::CreateMessageBox(MSGBOX_LAYOUT_CLASS(
                        SEASON3B::CPersonalshopCreateMsgBoxLayout, SessionOrigin()));
                }
            }
            else
            {
                g_pSystemLogBox->AddText(I18N::Game::ThereSNoStoreNameOrItemPrice,
                                         SEASON3B::TYPE_ERROR_MESSAGE);
            }
        }

        ButtonX = g_ptPersonalShop.x + 105;
        if (MouseX >= ButtonX && MouseX < ButtonX + Width && MouseY >= ButtonY &&
            MouseY < ButtonY + Height && MouseLButtonPush)
        {
            MouseLButtonPush = false;
            if (g_bEnablePersonalShop)
            {
                SocketClient->ToGameServer()->SendPlayerShopClose();
            }
        }

        Width = 150;
        ButtonX = g_ptPersonalShop.x + 20;
        ButtonY = g_ptPersonalShop.y + 65;
        if (MouseX >= ButtonX && MouseX < ButtonX + Width && MouseY >= ButtonY &&
            MouseY < ButtonY + Height && MouseLButtonPush)
        {
            OpenPersonalShopMsgWnd(1);
        }
    }
}

void SessionUiUnit::ClosePersonalShop()
{
    if (g_iPShopWndType == PSHOPWNDTYPE_PURCHASE)
    {
        memcpy(g_PersonalShopInven, g_PersonalShopBackup, sizeof(ITEM) * MAX_PERSONALSHOP_INVEN);
        if (IsShopInViewport(Hero))
        {
            std::wstring title{};
            GetShopTitle(Hero, title);
            wcscpy(g_szPersonalShopTitle, title.c_str());
        }
        else
        {
            g_szPersonalShopTitle[0] = '\0';
        }
        if (g_PersonalShopSeller.Key)
        {
            SocketClient->ToGameServer()->SendPlayerShopCloseOther(g_PersonalShopSeller.Key,
                                                                   g_PersonalShopSeller.ID);
        }
    }

    g_PersonalShopSeller.Initialize();

    g_iPShopWndType = PSHOPWNDTYPE_NONE;
}

void SessionUiUnit::ClearPersonalShop()
{
    g_bEnablePersonalShop = false;
    g_iPShopWndType = PSHOPWNDTYPE_NONE;
    g_iPersonalShopMsgType = 0;
    g_szPersonalShopTitle[0] = '\0';

    RemoveAllShopTitle();
}

bool SessionUiUnit::IsExistUndecidedPrice()
{
    bool bResult = true;

    auto inventoryCtrl = g_pMyShopInventory->GetInventoryCtrl();
    for (int i = 0; i < MAX_PERSONALSHOP_INVEN; ++i)
    {
        int iPrice = 0;
        ITEM *pItem = inventoryCtrl->GetItem(i);
        if (pItem)
        {
            bResult = false;
            int iIndex = inventoryCtrl->GetIndexByItem(pItem);
            if (GetPersonalItemPrice(iIndex, iPrice, g_IsPurchaseShop) == false)
            {
                return true;
            }

            if (iPrice <= 0)
            {
                return true;
            }
        }
        else
        {
            continue;
        }
    }

    return bResult;
}

void SessionUiUnit::OpenPersonalShopMsgWnd(int iMsgType)
{
    if (iMsgType == 1)
    {
        SEASON3B::CreatePersonalShopNameMessageBox(OriginatingSession());
    }
    else if (iMsgType == 2)
    {
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CPersonalShopItemValueMsgBoxLayout, sessionKeeper_));
    }
}
bool SessionUiUnit::IsCorrectShopTitle(const wchar_t *szShopTitle)
{
    int j = 0;
    wchar_t TmpText[2048];
    for (int i = 0; i < (int)wcslen(szShopTitle); ++i)
    {
        if (szShopTitle[i] != 32)
        {
            TmpText[j] = szShopTitle[i];
            j++;
        }
    }
    TmpText[j] = 0;

    for (int i = 0; i < AbuseFilterNumber; i++)
    {
        if (FindText(TmpText, AbuseFilter[i]))
        {
            return false;
        }
    }

    int len = wcslen(szShopTitle);
    int count = 0;

    for (int i = 0; i < len; i++)
    {
        if (szShopTitle[i] == 0x20)
        {
            count++;
            if (i == 1 && count >= 2)
                return false;
        }
        else
        {
            count = 0;
        }
    }
    if (count >= 2)
        return false;
    return true;
}

namespace ItemRulesDetail
{
// Publishes a full-replace RGBA8 repaint of an existing CGlobalBitmap
// entry as a new catalog revision (PLAN_P1R5.1.md section 5.2: "update
// mutation callers ... to build and smooth a local vector before catalog
// publication rather than write through a retained pointer"). Used by
// the guild/castle mark procedural texture paths below.
bool PublishBitmapRevision(SessionBitmapView &bitmaps, std::uint32_t logicalIndex,
                           LogicalRenderAssetRef currentAsset, std::uint32_t width,
                           std::uint32_t height, std::span<const std::byte> rgba8)
{
    const auto currentLease = bitmaps.TryLease(logicalIndex);
    if (!currentLease.has_value())
    {
        return false;
    }
    const LogicalRenderAssetRef nextAsset{currentAsset.id, currentAsset.revision + 1};
    const auto pixels = std::make_shared<const std::vector<std::byte>>(rgba8.begin(), rgba8.end());
    if (!bitmaps.CommitOwnerProducedRevision(logicalIndex, nextAsset, width, height,
                                             currentLease->sampler, pixels))
    {
        return false;
    }
    return true;
}
} // namespace ItemRulesDetail

//#define MAX_LENGTH_CMB	( 26)
#define NUM_LINE_CMB (7)

// OMF-00541
// OMF-00542

/*+++++++++++++++++++++++++++++++++++++
    INCLUDE.
+++++++++++++++++++++++++++++++++++++*/

bool CSItemOption::GetSetItemName(wchar_t *strName, const int iType, const int setType) const
{
    const int setItemType = (setType % 0x04);

    if (setItemType > 0)
    {
        const ITEM_SET_TYPE &itemSType = m_ItemSetType[iType];
        if (itemSType.byOption[setItemType - 1] != 255 && itemSType.byOption[setItemType - 1] != 0)
        {
            const ITEM_SET_OPTION &itemOption =
                m_ItemSetOption[itemSType.byOption[setItemType - 1]];

            memcpy(strName, itemOption.strSetName, sizeof itemOption.strSetName);

            const int length = wcslen(strName);
            strName[length] = ' ';
            strName[length + 1] = 0;
            return true;
        }
    }

    return false;
}

bool CSItemOption::GetDefaultOptionText(const ITEM *ip, wchar_t *Text) const
{
    if (ip->Type > MAX_ITEM)
    {
        return false;
    }

    if (ip->AncientBonusOption <= 0)
    {
        return false;
    }

    switch (ItemAttribute[ip->Type].AttType)
    {
    case SET_OPTION_STRENGTH:
        mu_swprintf(Text, I18N::Game::IncreaseStrengthD, ip->AncientBonusOption * 5);
        break;

    case SET_OPTION_DEXTERITY:
        mu_swprintf(Text, I18N::Game::IncreaseAgilityD, ip->AncientBonusOption * 5);
        break;

    case SET_OPTION_ENERGY:
        mu_swprintf(Text, I18N::Game::IncreaseEnergyD, ip->AncientBonusOption * 5);
        break;

    case SET_OPTION_VITALITY:
        mu_swprintf(Text, I18N::Game::IncreaseStaminaD, ip->AncientBonusOption * 5);
        break;

    default:
        return false;
    }
    return true;
}

void CSItemOption::MoveSetOptionList(const int StartX, const int StartY)
{
    int x, y, Width, Height;

    Width = 162;
    Height = 20;
    x = StartX + 14;
    y = StartY + 22;
    if (MouseX >= x && MouseX < x + Width && MouseY >= y && MouseY < y + Height)
    {
        m_bViewOptionList = true;

        MouseLButtonPush = false;
        MouseUpdateTime = 0;
        MouseUpdateTimeMax = 6;
    }
}

bool CSItemOption::BuildSetOptionList(std::uint8_t &TextNum)
{
    TextNum = 0;
    if (!m_bViewOptionList || m_SetSearchResultCount <= 0)
    {
        return false;
    }

    mu_swprintf(TextList[TextNum], L"\n");
    TextListColor[TextNum] = 0;
    TextBold[TextNum] = false;
    TextNum++;
    mu_swprintf(TextList[TextNum], L"\n");
    TextListColor[TextNum] = 0;
    TextBold[TextNum] = false;
    TextNum++;
    mu_swprintf(TextList[TextNum], L"\n");
    TextListColor[TextNum] = 0;
    TextBold[TextNum] = false;
    TextNum++;

    for (int i = 0; i < m_SetSearchResultCount; i++)
    {
        const auto &set = m_SetSearchResult[i];
        mu_swprintf(TextList[TextNum], L"%ls %ls", set.SetName, I18N::Game::Set);
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextBold[TextNum] = true;
        TextNum++;
        TextNum = RenderSetOptionList(set, TextNum, true, false);
    }

    m_bViewOptionList = false;
    return true;
}

void CSItemOption::CheckRenderOptionHelper(const wchar_t *FilterName)
{
    wchar_t Name[256];

    if (FilterName[0] != '/')
        return;

    const auto Length1 = wcslen(FilterName);
    for (int i = 0; i < MAX_SET_OPTION; ++i)
    {
        ITEM_SET_OPTION &setOption = m_ItemSetOption[i];
        if (setOption.byOptionCount < 255)
        {
            mu_swprintf(Name, L"/%ls", setOption.strSetName);

            const auto Length2 = wcslen(Name);

            m_byRenderOptionList = 0;
            if (wcsncmp(FilterName, Name, Length1) == 0 && wcsncmp(FilterName, Name, Length2) == 0)
            {
                g_pNewUISystem->Hide(SEASON3B::INTERFACE_ITEM_EXPLANATION);
                g_pNewUISystem->Hide(SEASON3B::INTERFACE_HELP);
                g_pNewUISystem->Show(SEASON3B::INTERFACE_SETITEM_EXPLANATION);

                m_byRenderOptionList = static_cast<std::uint8_t>(i + 1);
                return;
            }
        }
    }
}

bool CSItemOption::BuildOptionHelper(std::uint8_t &TextNum)
{
    TextNum = 0;
    if (m_byRenderOptionList == 0)
        return false;

    std::fill(std::begin(TextListColor), std::end(TextListColor), 0);
    for (int i = 0; i < 30; i++)
    {
        TextList[i][0] = L'\0';
    }

    ITEM_SET_OPTION &setOption = m_ItemSetOption[m_byRenderOptionList - 1];
    if (setOption.byOptionCount >= 255)
    {
        m_byRenderOptionList = 0;
        return false;
    }

    mu_swprintf(TextList[TextNum], L"\n");
    TextNum++;
    mu_swprintf(TextList[TextNum], L"%ls %ls %ls", setOption.strSetName, I18N::Game::Set,
                I18N::Game::ItemOptionInfo);
    TextListColor[TextNum] = TEXT_COLOR_YELLOW;
    TextNum++;

    mu_swprintf(TextList[TextNum], L"\n");
    TextNum++;
    mu_swprintf(TextList[TextNum], L"\n");
    TextNum++;

    for (int o = 0; o < MAX_ITEM_SET_STANDARD_OPTION_COUNT; ++o)
    {
        for (int n = 0; n < MAX_ITEM_SET_STANDARD_OPTION_PER_ITEM_COUNT; ++n)
        {
            if (getExplainText(TextList[TextNum], setOption.byStandardOption[o][n],
                               setOption.byStandardOptionValue[o][n]))
            {
                TextListColor[TextNum] = TEXT_COLOR_BLUE;
                TextBold[TextNum] = false;
                TextNum++;
            }
        }
    }

    for (int o = 0; o < MAX_ITEM_SET_EXT_OPTION_COUNT; ++o)
    {
        if (getExplainText(TextList[TextNum], setOption.byExtOption[o],
                           setOption.byExtOptionValue[o]))
        {
            TextListColor[TextNum] = TEXT_COLOR_GREEN;
            TextBold[TextNum] = false;
            TextNum++;
        }
    }

    for (int o = 0; o < MAX_ITEM_SET_FULL_OPTION_COUNT; ++o)
    {
        if (getExplainText(TextList[TextNum], setOption.byFullOption[o],
                           setOption.byFullOptionValue[o]))
        {
            TextListColor[TextNum] = TEXT_COLOR_YELLOW;
            TextBold[TextNum] = false;
            TextNum++;
        }
    }

    mu_swprintf(TextList[TextNum], L"\n");
    TextNum++;
    mu_swprintf(TextList[TextNum], L"\n");
    TextNum++;

    return true;
}

void CSItemOption::SetViewOptionList(bool bView)
{
    m_bViewOptionList = bView;
}

bool CSItemOption::IsViewOptionList()
{
    return m_bViewOptionList;
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

CUIUnmixgemList::CUIUnmixgemList(SessionKeeper &keeper)
    : CUITextListBox<UNMIX_TEXT>(keeper), sessionUi_(UiForConstruction())
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = MAX_LINE_UNMIXLIST;

    m_fScrollBarRange_top = 0;
    m_fScrollBarRange_bottom = 0;

    m_fScrollBarWidth = 13;
    m_fScrollBarHeight = 0;

    m_fScrollBarClickPos_y = 0;
    m_bScrollBtnClick = FALSE;
    m_bScrollBarClick = FALSE;

    m_bUseSelectLine = TRUE;

    SetPosition(0, 0);
    SetSize(180, 109);
}

void CUIUnmixgemList::SetNumRenderLine(int iLine)
{
    if (iLine < m_iNumRenderLine && iLine < GetLineNum())
        ++m_iCurrentRenderEndLine;
    else if (iLine > GetLineNum())
        m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = iLine;
}

bool lessfunc(const UNMIX_TEXT &lhs, const UNMIX_TEXT &rhs)
{
    return (lhs.m_iInvenIdx > rhs.m_iInvenIdx);
}

void CUIUnmixgemList::Sort()
{
    sort(m_TextList.begin(), m_TextList.end(), lessfunc);
}

void CUIUnmixgemList::AddText(int iIndex, BYTE cComType)
{
    if (iIndex < MAX_EQUIPMENT_INDEX || iIndex >= MAX_MY_INVENTORY_EX_INDEX ||
        cComType == COMGEM::NOCOM)
        return;

    for (unsigned int i = 0; i < m_TextList.size(); ++i)
    {
        const UNMIX_TEXT &rt = m_TextList[i];
        if (rt.m_iInvenIdx == iIndex)
            return;
    }

    UNMIX_TEXT t;
    t.m_bIsSelected = FALSE;
    t.m_cLevel = cComType;
    t.m_iInvenIdx = iIndex;
    m_TextList.push_back(t);

    RemoveText();
    SLSetSelectLine(0);

    if (GetLineNum() > m_iNumRenderLine)
        ++m_iCurrentRenderEndLine;

    if (GetLineNum() < m_iNumRenderLine)
        ;
    else if (GetLineNum() - m_iCurrentRenderEndLine < m_iNumRenderLine)
        m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;

    Sort();
}

BOOL CUIUnmixgemList::DoLineMouseAction(int iLineNumber)
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

        if (MouseLButtonDBClick)
        {
            SLSetSelectLine(m_iCurrentRenderEndLine + iLineNumber + 1);
            UNMIX_TEXT *pt = GetSelectedText();

            if (pt->m_cLevel != COMGEM::NOCOM && pt->m_iInvenIdx >= MAX_EQUIPMENT_INDEX &&
                pt->m_iInvenIdx < MAX_MY_INVENTORY_EX_INDEX)
                sessionUi_.SelectFromList(pt->m_iInvenIdx, pt->m_cLevel);

            MouseLButtonDBClick = false;
        }
    }
    return TRUE;
}

int CUIUnmixgemList::GetRenderLinePos_y(int iLineNumber)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * 13 - iLineNumber * 13 + 3);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * 13 - iLineNumber * 13 + 3);
}

#ifdef PBG_ADD_INGAMESHOP_UI_ITEMSHOP
CUIInGameShopListBox::CUIInGameShopListBox(SessionKeeper &keeper)
    : CUITextListBox<IGS_StorageItem>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 9;

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
    m_iScrollType = UILISTBOX_SCROLL_UPDOWN;

    SetSize(LISTBOX_WIDTH, LISTBOX_HEIGHT);
    SetPosition(490, 360);
}

void CUIInGameShopListBox::AddText(IGS_StorageItem &_StorageItem)
{
    IGS_StorageItem sItem{};

    sItem.m_bIsSelected = FALSE;

    sItem.m_iStorageSeq = _StorageItem.m_iStorageSeq;
    sItem.m_iStorageItemSeq = _StorageItem.m_iStorageItemSeq;
    sItem.m_iStorageGroupCode = _StorageItem.m_iStorageGroupCode;
    sItem.m_iProductSeq = _StorageItem.m_iProductSeq;
    sItem.m_iPriceSeq = _StorageItem.m_iPriceSeq;
    sItem.m_iNum = _StorageItem.m_iNum;
    sItem.m_wItemCode = _StorageItem.m_wItemCode;

    wcsncpy(sItem.m_szName, _StorageItem.m_szName, MAX_TEXT_LENGTH);
    wcsncpy(sItem.m_szNum, _StorageItem.m_szNum, MAX_TEXT_LENGTH);
    wcsncpy(sItem.m_szPeriod, _StorageItem.m_szPeriod, MAX_TEXT_LENGTH);
    sItem.m_szType = _StorageItem.m_szType;
    wcsncpy(sItem.m_szSendUserName, _StorageItem.m_szSendUserName, MAX_USERNAME_SIZE + 1);
    wcsncpy(sItem.m_szMessage, _StorageItem.m_szMessage, MAX_GIFT_MESSAGE_SIZE);

    m_TextList.push_front(sItem);

    RemoveText();
    SLSetSelectLine(0);

    if (GetLineNum() > m_iNumRenderLine)
        ++m_iCurrentRenderEndLine;

    if (GetLineNum() < m_iNumRenderLine)
        ;
    else if (GetLineNum() - m_iCurrentRenderEndLine < m_iNumRenderLine)
        m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;

    if (m_TextList.size() == 1)
        SLSetSelectLine(1);

    if (m_TextList.empty() == FALSE)
    {
        SLSetSelectLine(GetLineNum());
        Scrolling(-10000);
    }
}

void CUIInGameShopListBox::SetNumRenderLine(int iLine)
{
    if (iLine < m_iNumRenderLine && iLine < GetLineNum())
        ++m_iCurrentRenderEndLine;
    else if (iLine > GetLineNum())
        m_iCurrentRenderEndLine = 0;

    m_iNumRenderLine = iLine;
}

int CUIInGameShopListBox::GetRenderLinePos_y(int iLineNumber)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * 13 - iLineNumber * 13 + 3);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * 13 - iLineNumber * 13 + 3);
}

BOOL CUIInGameShopListBox::DoLineMouseAction(int iLineNumber)
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

            SLSetSelectLine(nSelLine);
            m_TextListIter->m_bIsSelected = (m_TextListIter->m_bIsSelected + 1) % 2; // T/F Reverse

            m_TextListIter = SLGetSelectLine();
        }
    }
    return TRUE;
}

CUIBuyingListBox::CUIBuyingListBox(SessionKeeper &keeper) : CUITextListBox<IGS_BuyList>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 7;

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

    m_bRenderLineColor = true;

    SetSize(LISTBOX_WIDTH, LISTBOX_HEIGHT);
}

void CUIBuyingListBox::AddText(const wchar_t *pszExplanationText)
{
    if (pszExplanationText == nullptr || pszExplanationText[0] == '\0')
        return;

    IGS_BuyList sIGS_Buying{};
    sIGS_Buying.m_bIsSelected = FALSE;
    wcsncpy(sIGS_Buying.m_pszItemExplanation, pszExplanationText, LINE_TEXTMAX);
    m_TextList.push_front(sIGS_Buying);

    RemoveText();
    SLSetSelectLine(0);

    if (GetLineNum() > m_iNumRenderLine)
        ++m_iCurrentRenderEndLine;

    if (GetLineNum() < m_iNumRenderLine)
        ;
    else if (GetLineNum() - m_iCurrentRenderEndLine < m_iNumRenderLine)
        m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;

    if (m_TextList.size() == 1)
        SLSetSelectLine(1);

    if (m_TextList.empty() == FALSE)
    {
        SLSetSelectLine(GetLineNum());
        Scrolling(-10000);
    }
}
void CUIBuyingListBox::SetNumRenderLine(int iLine)
{
    if (iLine < m_iNumRenderLine && iLine < GetLineNum())
        ++m_iCurrentRenderEndLine;
    else if (iLine > GetLineNum())
        m_iCurrentRenderEndLine = 0;

    m_iNumRenderLine = iLine;
}

int CUIBuyingListBox::GetRenderLinePos_y(int iLineNumber)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * 13 - iLineNumber * 13 + 3);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * 13 - iLineNumber * 13 + 3);
}

BOOL CUIBuyingListBox::DoLineMouseAction(int iLineNumber)
{
    if (CheckMouseIn(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3,
                     m_iWidth - m_fScrollBarWidth + 1, 13))
    {
        if (MouseLButtonPush)
        {
            MouseLButtonPush = false;

            int nSelLine = m_iCurrentRenderEndLine + iLineNumber + 1;

            if (SLGetSelectLineNum() == nSelLine)
                return TRUE;

            SLSetSelectLine(nSelLine);
            m_TextListIter->m_bIsSelected = (m_TextListIter->m_bIsSelected + 1) % 2; // T/F Reverse

            m_TextListIter = SLGetSelectLine();
        }
    }
    return TRUE;
}

CUIPackCheckBuyingListBox::CUIPackCheckBuyingListBox(SessionKeeper &keeper)
    : CUITextListBox<IGS_SelectBuyItem>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 3;

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

    m_iCurrentLine = -1;

    SetSize(LISTBOX_WIDTH, LISTBOX_HEIGHT);
}
CUIPackCheckBuyingListBox::~CUIPackCheckBuyingListBox()
{
    radioButtonIterIndex = 0;
}
void CUIPackCheckBuyingListBox::AddText(IGS_SelectBuyItem &_Item)
{
    IGS_SelectBuyItem sItem{};

    sItem.m_iPackageSeq = _Item.m_iPackageSeq;
    sItem.m_iDisplaySeq = _Item.m_iDisplaySeq;
    sItem.m_iPriceSeq = _Item.m_iPriceSeq;
    sItem.m_wItemCode = _Item.m_wItemCode;
    sItem.m_iCashType = _Item.m_iCashType;

    wcsncpy(sItem.m_szItemName, _Item.m_szItemName, MAX_TEXT_LENGTH);
    wcsncpy(sItem.m_szItemPrice, _Item.m_szItemPrice, MAX_TEXT_LENGTH);
    wcsncpy(sItem.m_szItemPeriod, _Item.m_szItemPeriod, MAX_TEXT_LENGTH);
    wcsncpy(sItem.m_szAttribute, _Item.m_szAttribute, MAX_TEXT_LENGTH);

    radioButtonIterIndex++;
    sItem.m_RadioBtn.SetRadioBtnIsEnable(radioButtonIterIndex);

    m_TextList.push_front(sItem);

    RemoveText();
    SLSetSelectLine(0);

    if (GetLineNum() > m_iNumRenderLine)
        ++m_iCurrentRenderEndLine;

    if (GetLineNum() < m_iNumRenderLine)
        ;
    else if (GetLineNum() - m_iCurrentRenderEndLine < m_iNumRenderLine)
        m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;

    if (m_TextList.size() == 1)
        SLSetSelectLine(1);

    if (m_TextList.empty() == FALSE)
    {
        SLSetSelectLine(GetLineNum());
        Scrolling(-10000);
    }
}
void CUIPackCheckBuyingListBox::SetNumRenderLine(int iLine)
{
    if (iLine < m_iNumRenderLine && iLine < GetLineNum())
        ++m_iCurrentRenderEndLine;
    else if (iLine > GetLineNum())
        m_iCurrentRenderEndLine = 0;

    m_iNumRenderLine = iLine;
}

BOOL CUIPackCheckBuyingListBox::DoLineMouseAction(int nLine)
{
    if (CheckMouseIn(m_iPos_x, GetRenderLinePos_y(nLine) - 3, m_iWidth - m_fScrollBarWidth + 1,
                     TEXT_HEIGHTSIZE))
    {
        if (MouseLButtonPush)
        {
            MouseLButtonPush = false;

            int nSelLine = m_iCurrentRenderEndLine + nLine + 1;

            if (SLGetSelectLineNum() == nSelLine)
                return TRUE;

            SLSetSelectLine(nSelLine);
            m_TextListIter->m_bIsSelected = (m_TextListIter->m_bIsSelected + 1) % 2; // T/F Reverse

            m_TextListIter = SLGetSelectLine();
        }
    }
    return TRUE;
}
int CUIPackCheckBuyingListBox::GetRenderLinePos_y(int nLine)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * TEXT_HEIGHTSIZE -
                nLine * TEXT_HEIGHTSIZE + TEXT_HEIGHTSIZE * 0.1f);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * TEXT_HEIGHTSIZE -
                nLine * TEXT_HEIGHTSIZE + TEXT_HEIGHTSIZE * 0.1f);
}

BOOL CUIPackCheckBuyingListBox::IsChangeLine()
{
    if (m_iCurrentLine != SLGetSelectLineNum())
    {
        m_iCurrentLine = SLGetSelectLineNum();
        return TRUE;
    }

    return FALSE;
}

#endif //PBG_ADD_INGAMESHOP_UI_ITEMSHOP

CUIExtraItemListBox::CUIExtraItemListBox(SessionKeeper &keeper)
    : CUITextListBox<FILTERLIST_TEXT>(keeper)
{
    m_iMaxLineCount = 12;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 5;

    m_fScrollBarRange_top = 0;
    m_fScrollBarRange_bottom = 0;

    m_fScrollBarPos_y = 0;
    m_fScrollBarWidth = 13;
    m_fScrollBarHeight = 0;

    m_fScrollBarClickPos_y = 0;
    m_bScrollBtnClick = FALSE;
    m_bScrollBarClick = FALSE;

    m_bUseSelectLine = TRUE;
}

void CUIExtraItemListBox::AddText(const wchar_t *pszPattern)
{
    if (pszPattern == nullptr || pszPattern[0] == '\0')
        return;

    FILTERLIST_TEXT text{};
    wcsncpy(text.m_szPattern, pszPattern, MAX_ITEM_NAME + 1);
    text.m_bIsSelected = FALSE;
    m_TextList.push_front(text);

    RemoveText();
    SLSetSelectLine(0);
    if (GetLineNum() > m_iNumRenderLine)
        ++m_iCurrentRenderEndLine;

    if (GetLineNum() < m_iNumRenderLine)
        ;
    else if (GetLineNum() - m_iCurrentRenderEndLine < m_iNumRenderLine)
        m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;

    if (m_TextList.size() == 1)
        SLSetSelectLine(1);

    if (m_TextList.empty() == FALSE)
    {
        SLSetSelectLine(GetLineNum());
        Scrolling(-10000);
    }
}

void CUIExtraItemListBox::SetNumRenderLine(int iLine)
{
    if (iLine < m_iNumRenderLine && iLine < GetLineNum())
        ++m_iCurrentRenderEndLine;
    else if (iLine > GetLineNum())
        m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = iLine;
}

int CUIExtraItemListBox::GetRenderLinePos_y(int iLineNumber)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * 13 - iLineNumber * 13 + 3);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * 13 - iLineNumber * 13 + 3);
}

BOOL CUIExtraItemListBox::DoLineMouseAction(int iLineNumber)
{
    if (CheckMouseIn(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3,
                     m_iWidth - m_fScrollBarWidth + 1, 13))
    {
        if (MouseLButtonPush)
        {
            SLSetSelectLine(m_iCurrentRenderEndLine + iLineNumber + 1);
            m_TextListIter->m_bIsSelected = (m_TextListIter->m_bIsSelected + 1) % 2;
            MouseLButtonPush = false;
        }
    }
    return TRUE;
}

void CUIExtraItemListBox::DeleteText(const wchar_t *pszPattern)
{
    if (pszPattern == nullptr || wcslen(pszPattern) == 0)
        return;
    for (m_TextListIter = m_TextList.begin(); m_TextListIter != m_TextList.end(); ++m_TextListIter)
    {
        if (wcsncmp(m_TextListIter->m_szPattern, pszPattern, MAX_ITEM_NAME) == 0)
            break;
    }
    if (m_TextListIter == m_TextList.end())
        return;

    if (SLGetSelectLineNum() != 1)
        SLSelectNextLine();
    m_TextList.erase(m_TextListIter);
}

// Personal store label controls.
#pragma pack(push)
#pragma pack()
CPersonalShopTitleImp::CPersonalShopTitleImp(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), cameraProjection_(keeper.CameraProjectionObject()),
      m_iHighlightFrame(0), m_bShow(true)
{
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
CPersonalShopTitleImp::~CPersonalShopTitleImp()
{
    RemoveAllShopTitle();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CPersonalShopTitleImp::AddShopTitle(int key, CHARACTER *pPlayer, const std::wstring &title)
{
    if (pPlayer == NULL)
        return false;
    if (pPlayer->Object.Kind != KIND_PLAYER)
    {
        g_ErrorReport.Write(L"@ AddShopTitle - there is NOT player object(id : %ls) \n",
                            pPlayer->ID);
        return false;
    }

    std::wstring full_name = pPlayer->ID;
    std::wstring topTitle, bottomTitle;

    auto mi = m_listShopTitleDrawObj.find(pPlayer);
    if (mi != m_listShopTitleDrawObj.end())
    {
        CShopTitleDrawObj *pDrawObj = (*mi).second;
        if (pDrawObj->GetKey() == key)
        {
            pDrawObj->SetBoxContent(full_name, title);
            pDrawObj->SetBoxPos(MakePos(-1, -1));
        }
        else
        {
            g_ErrorReport.Write(L"@ AddShopTitle - player key-value dismatch(id : %ls) \n",
                                pPlayer->ID);
        }
    }
    else
    {
        auto *pDrawObj = new CShopTitleDrawObj(SessionOrigin());
        pDrawObj->Create(key, full_name, title, MakePos(-1, -1));
        m_listShopTitleDrawObj.insert(type_drawobj_map::value_type(pPlayer, pDrawObj));
    }

    return true;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::RemoveShopTitle(CHARACTER *pPlayer)
{
    auto mi = m_listShopTitleDrawObj.find(pPlayer);
    if (mi != m_listShopTitleDrawObj.end())
    {
        delete (*mi).second;
        m_listShopTitleDrawObj.erase(mi);
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::RemoveAllShopTitle()
{
    auto mi = m_listShopTitleDrawObj.begin();
    for (; mi != m_listShopTitleDrawObj.end(); ++mi)
        delete (*mi).second;
    m_listShopTitleDrawObj.clear();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::RemoveAllShopTitleExceptHero()
{
    auto mi = m_listShopTitleDrawObj.begin();
    for (; mi != m_listShopTitleDrawObj.end();)
    {
        if ((*mi).second->GetKey() != Hero->Key)
        {
            delete (*mi).second;
            mi = m_listShopTitleDrawObj.erase(mi);
        }
        else
        {
            mi++;
        }
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
CHARACTER *CPersonalShopTitleImp::FindCharacter(int key) const
{
    auto mi = m_listShopTitleDrawObj.begin();
    for (; mi != m_listShopTitleDrawObj.end(); ++mi)
    {
        if ((*mi).second->GetKey() == key)
        {
            return (*mi).first;
        }
    }
    return NULL;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::ShowShopTitles()
{
    m_bShow = true;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::HideShopTitles()
{
    m_bShow = false;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CPersonalShopTitleImp::IsShowShopTitles() const
{
    return m_bShow;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CPersonalShopTitleImp::IsShopTitleVisible(CHARACTER *pPlayer)
{
    type_drawobj_map::const_iterator mi = m_listShopTitleDrawObj.find(pPlayer);
    if (mi != m_listShopTitleDrawObj.end())
    {
        return (*mi).second->IsVisible();
    }
    return false;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CPersonalShopTitleImp::IsShopTitleHighlight(CHARACTER *pPlayer) const
{
    auto mi = m_listShopTitleDrawObj.find(pPlayer);
    if (mi != m_listShopTitleDrawObj.end())
    {
        return (*mi).second->IsHighlight();
    }
    return false;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CPersonalShopTitleImp::IsInViewport(CHARACTER *pPlayer)
{
    auto mi = m_listShopTitleDrawObj.find(pPlayer);
    if (mi != m_listShopTitleDrawObj.end())
    {
        return true;
    }
    return false;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::GetShopTitle(CHARACTER *pPlayer, std::wstring &title)
{
    auto mi = m_listShopTitleDrawObj.find(pPlayer);
    if (mi != m_listShopTitleDrawObj.end())
    {
        (*mi).second->GetFullTitle(title);
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::GetShopTitleSummary(CHARACTER *pPlayer, std::wstring &summary)
{
    auto mi = m_listShopTitleDrawObj.find(pPlayer);
    if (mi != m_listShopTitleDrawObj.end())
    {
        std::wstring full_title;
        (*mi).second->GetFullTitle(full_title);
        if (full_title.size() > 14)
        {
            int offset = 0;
            for (; offset < 12;)
            {
                if (full_title[offset] & 0x80)
                    offset += 2;
                else
                    offset++;
            }
            summary.assign(full_title, 0, offset);
            summary += L"..";
        }
        else
        {
            summary = full_title;
        }
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::Update()
{
    if (!m_listShopTitleDrawObj.empty())
    {
        CheckKeyIntegrity();
    }
    StageModernLabels();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::UpdatePosition()
{
    auto mi = m_listShopTitleDrawObj.begin();
    for (; mi != m_listShopTitleDrawObj.end(); ++mi)
    {
        CShopTitleDrawObj *pDrawObj = (*mi).second;

        SIZE size;
        pDrawObj->GetBoxSize(size);

        POINT pos;
        CalculateBooleanPos((*mi).first, size, pos); //. real position

        pDrawObj->SetBoxPos(pos);
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::UpdateHighlight(CShopTitleDrawObj &drawObject, CHARACTER *player)
{
    const bool isHighlightTime =
        static_cast<INT64>(WorldTime / UI::Modern::RmlPlayerNameLayer::StoreHighlightPeriod()) %
            2 ==
        0;
    const bool highlighted = SelectedCharacter != -1 && isHighlightTime &&
                             g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_COMMAND) &&
                             g_pCommandWindow->GetCurCommandType() == COMMAND_PURCHASE &&
                             &CharactersClient[SelectedCharacter] == player;
    if (highlighted)
    {
        drawObject.EnableHighlight();
    }
    else
    {
        drawObject.DisableHighlight();
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::RevisionPosition()
{
    //. Fit to screen
    auto mi_x = m_listShopTitleDrawObj.begin();
    for (; mi_x != m_listShopTitleDrawObj.end(); ++mi_x)
    {
        auto mi_y = m_listShopTitleDrawObj.begin();
        for (; mi_y != m_listShopTitleDrawObj.end(); ++mi_y)
        {
            if (mi_x != mi_y)
            {
                RECT rcX, rcY;
                (*mi_x).second->GetBoxRect(rcX);
                (*mi_y).second->GetBoxRect(rcY);

                if (rcX.right > rcY.left && rcX.left < rcY.right && rcX.bottom > rcY.top &&
                    rcX.top < rcY.bottom)
                {
                    POINT pos;
                    if (rcX.bottom < (rcY.top + rcY.bottom) / 2)
                    {
                        pos.x = rcX.left;
                        pos.y = rcY.top - (rcX.bottom - rcX.top);
                    }
                    else
                    {
                        pos.x = rcX.left;
                        pos.y = rcY.bottom;
                    }
                    (*mi_x).second->SetBoxPos(pos);
                }
            }
        }
    }

    for (mi_x = m_listShopTitleDrawObj.begin(); mi_x != m_listShopTitleDrawObj.end(); ++mi_x)
    {
        POINT pos;
        SIZE size;
        (*mi_x).second->GetBoxPos(pos);
        (*mi_x).second->GetBoxSize(size);
        if (pos.x < 0)
            pos.x = 0;
        if ((unsigned int)pos.x >= ModernUiViewportWidth() - (unsigned int)size.cx)
            pos.x = ModernUiViewportWidth() -
                    (size.cx + UI::Modern::RmlPlayerNameLayer::StoreRightMargin());
        if (pos.y < 0)
            pos.y = 0;
        if ((unsigned int)pos.y >= ModernUiViewportHeight() - (unsigned int)size.cy)
            pos.y = ModernUiViewportHeight() - size.cy;
        (*mi_x).second->SetBoxPos(pos);
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::CheckKeyIntegrity()
{
    auto mi = m_listShopTitleDrawObj.begin();

    for (; mi != m_listShopTitleDrawObj.end();)
    {
        CHARACTER *pPlayer = (*mi).first;
        CShopTitleDrawObj *pDrawObj = (*mi).second;

        if (pPlayer->Key != pDrawObj->GetKey())
        {
            delete pDrawObj;
            mi = m_listShopTitleDrawObj.erase(mi);
            g_ErrorReport.Write(
                L"@ CheckKeyIntegrity - player key-value dismatch(id : %ls, server's key : %d, client array's key : %d) \n",
                pPlayer->ID, pDrawObj->GetKey(), pPlayer->Key);
        }
        else if (pPlayer->Object.Kind != KIND_PLAYER)
        {
            delete pDrawObj;
            mi = m_listShopTitleDrawObj.erase(mi);
            g_ErrorReport.Write(
                L"@ CheckKeyIntegrity - player type invalid(id : %ls, server's key : %d, client array's key : %d) \n",
                pPlayer->ID, pDrawObj->GetKey(), pPlayer->Key);
        }
        else
        {
            mi++;
        }
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::CalculateBooleanPos(IN CHARACTER *pPlayer, IN const SIZE &size,
                                                OUT POINT &pos)
{
    (void)size;
    vec3_t posTemp;
    OBJECT *pObject = &pPlayer->Object;
    Vector(pObject->Position[0], pObject->Position[1],
           pObject->Position[2] + pObject->BoundingBoxMax[2] +
               UI::Modern::RmlPlayerNameLayer::WorldRaise(),
           posTemp);

    POINT ptFloating;
    cameraProjection_.WorldToScreen(g_Camera, posTemp, (int *)&ptFloating.x, (int *)&ptFloating.y);

    // S16 mcPlayerName places PlayerStore at (-73, -80) from the
    // projected player-name anchor.
    const float scale = ModernUiScale();
    pos.x = (ptFloating.x * (int)ModernUiViewportWidth() / REFERENCE_WIDTH) +
            static_cast<int>(std::lround(UI::Modern::RmlPlayerNameLayer::StoreLeft() * scale));
    pos.y = (ptFloating.y * (int)ModernUiViewportHeight() / REFERENCE_HEIGHT) +
            static_cast<int>(std::lround(UI::Modern::RmlPlayerNameLayer::StoreTop() * scale));
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::CShopTitleDrawObj::Init()
{
    //. initialize
    m_fullname = L"";
    m_topTitle = L"";
    m_bottomTitle = L"";

    m_key = -1;
    m_bDraw = false;
    m_pos.x = m_pos.y = 0;
    m_size.cx = m_size.cy = 0;
    m_bHighlight = false;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CPersonalShopTitleImp::CShopTitleDrawObj::Create(int key, const std::wstring &name,
                                                      const std::wstring &title, POINT pos)
{
    m_key = key & 0x7FFF;
    SetBoxContent(name, title);
    SetBoxPos(pos);
    EnableDraw();

    return true;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::CShopTitleDrawObj::Release()
{
    Init();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
int CPersonalShopTitleImp::CShopTitleDrawObj::GetKey() const
{
    return m_key;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::CShopTitleDrawObj::SetBoxContent(const std::wstring &name,
                                                             const std::wstring &title)
{
    m_fullname = name;
    m_fulltitle = title;
    SeparateShopTitle(title, m_topTitle, m_bottomTitle);
    CalculateBooleanSize(name, m_topTitle, m_bottomTitle, m_size);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::CShopTitleDrawObj::SetBoxPos(POINT pos)
{
    m_pos = pos;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::CShopTitleDrawObj::GetBoxSize(SIZE &size)
{
    size = m_size;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::CShopTitleDrawObj::GetBoxPos(POINT &pos)
{
    pos = m_pos;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::CShopTitleDrawObj::GetBoxRect(RECT &rect)
{
    rect.left = m_pos.x;
    rect.top = m_pos.y;
    rect.right = m_pos.x + m_size.cx;
    rect.bottom = m_pos.y + m_size.cy;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::CShopTitleDrawObj::GetFullTitle(std::wstring &title)
{
    title = m_fulltitle;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CPersonalShopTitleImp::CShopTitleDrawObj::IsVisible() const
{
    return m_bDraw;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::CShopTitleDrawObj::EnableHighlight()
{
    m_bHighlight = true;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::CShopTitleDrawObj::DisableHighlight()
{
    m_bHighlight = false;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CPersonalShopTitleImp::CShopTitleDrawObj::IsHighlight() const
{
    return m_bHighlight;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::CShopTitleDrawObj::SeparateShopTitle(const std::wstring &title,
                                                                 std::wstring &topTitle,
                                                                 std::wstring &bottomTitle)
{
    wchar_t pszTopTitle[MAX_SHOPTITLE] = {0};
    wchar_t pszBottomTitle[MAX_SHOPTITLE] = {0};
    CutText(title.c_str(), pszTopTitle, pszBottomTitle, MAX_SHOPTITLE);

    topTitle = pszTopTitle;
    bottomTitle = pszBottomTitle;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::CShopTitleDrawObj::CalculateBooleanSize(
    IN const std::wstring &name, IN const std::wstring &topTitle,
    IN const std::wstring &bottomTitle, OUT SIZE &size)
{
    (void)name;
    UI::Modern::RmlPlayerName label;
    if (!topTitle.empty())
        label.storeTitleTop[0] = L'x';
    if (!bottomTitle.empty())
        label.storeTitleBottom[0] = L'x';
    const float scale = ModernUiScale();
    size.cx = static_cast<LONG>(std::lround(UI::Modern::RmlPlayerNameLayer::StoreWidth() * scale));
    size.cy = static_cast<LONG>(
        std::lround(UI::Modern::RmlPlayerNameLayer::StoreHeightFor(label) * scale));
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
int SessionUiUnit::CalcRecoveryZen(BYTE type, wchar_t *Text)
{
    ITEM *ip;

    g_pMyInventory->SetRepairEnableLevel(false);

    switch (type)
    {
    case REVIVAL_DARKHORSE:
        ip = &CharacterMachine->Equipment[EQUIPMENT_HELPER];
        if (ip->Type != ITEM_DARK_HORSE_ITEM)
        {
            mu_swprintf(Text, I18N::Game::PetIsNotEquipped);
            return -1;
        }
        break;

    case REVIVAL_DARKSPIRIT:
        ip = &CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT];
        if (ip->Type != ITEM_DARK_RAVEN_ITEM)
        {
            mu_swprintf(Text, I18N::Game::PetIsNotEquipped);
            return -1;
        }
        break;
    default:
        mu_swprintf(Text, I18N::Game::PetIsNotEquipped);
        return -1;
    }

    ITEM_ATTRIBUTE *p = &ItemAttribute[ip->Type];

    int maxDurability = CalcMaxDurability(ip, p, ip->Level);

    int Gold = 0;
    if (ip->Durability < maxDurability)
    {
        DWORD dwValue = 0;
        dwValue = GetPetItemValue(GetPetInfo(ip));

        Gold = ConvertRepairGold(dwValue, ip->Durability, maxDurability, ip->Type, Text);
    }

    switch (Gold)
    {
    case 0:
        mu_swprintf(Text, I18N::Game::LifeHasBeenRecovered);
        break;

    default: {
        wchar_t Text2[100];
        memset(Text2, 0, sizeof(char) * 100);

        if ((int)CharacterMachine->Gold < Gold)
        {
            ConvertGold((double)Gold - CharacterMachine->Gold, Text);
            mu_swprintf(Text2, I18N::Game::SZenIsLackingToRecoverLife, Text);
        }
        else
        {
            mu_swprintf(Text2, I18N::Game::SZenIsRequiredToRecoverLife, Text);
        }

        int Length = wcslen(Text2);
        memcpy(Text, Text2, sizeof(char) * Length);
        Text[Length] = 0;
    }
    break;
    }

    return Gold;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SessionUiUnit::RecoverPet(BYTE type)
{
    wchar_t Text[100];
    int Gold = CalcRecoveryZen(type, Text);

    if ((int)CharacterMachine->Gold >= Gold && Gold != -1)
    {
        switch (type)
        {
        case REVIVAL_DARKHORSE:
            SocketClient->ToGameServer()->SendRepairItemRequest(EQUIPMENT_HELPER, (BYTE)Gold);
            break;

        case REVIVAL_DARKSPIRIT:
            SocketClient->ToGameServer()->SendRepairItemRequest(EQUIPMENT_WEAPON_LEFT, (BYTE)Gold);
            break;
        }
        InitItemBackup();
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::EnableShopTitleDraw(CHARACTER *pPlayer)
{
    auto mi = m_listShopTitleDrawObj.find(pPlayer);
    if (mi != m_listShopTitleDrawObj.end())
    {
        (*mi).second->EnableDraw();
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::DisableShopTitleDraw(CHARACTER *pPlayer)
{
    auto mi = m_listShopTitleDrawObj.find(pPlayer);
    if (mi != m_listShopTitleDrawObj.end())
    {
        (*mi).second->DisableDraw();
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
CPersonalShopTitleImp::CShopTitleDrawObj::CShopTitleDrawObj(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper)
{
    Init();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
CPersonalShopTitleImp::CShopTitleDrawObj::~CShopTitleDrawObj()
{
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::CShopTitleDrawObj::EnableDraw()
{
    m_bDraw = true;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CPersonalShopTitleImp::CShopTitleDrawObj::DisableDraw()
{
    m_bDraw = false;
}
#pragma pack(pop)
