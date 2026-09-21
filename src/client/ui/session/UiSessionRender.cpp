#include "ui/session/UiSessionRender.h"
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
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Shop.h"
#include "domain/WorldSimulation.h"
#include "render/ModelResources.h"
#include "render/Sprites.h"
#include "render/Textures.h"
#include "render/UiAdapter.h"
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
#include "ui/features/Activities/ActivitiesLogic.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Hud/HudLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/QuestNpc/QuestNpcLogic.h"
#include "ui/features/Shell/ShellLogic.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/features/World/WorldLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"

//  UIManager.cpp
POINT CUIManager::RenderWindowBase(int nHeight, int nOriginY)
{
    int nWidth = 213;

    EnableAlphaBlend3();
    glColor4f(1.0f, 1.0f, 1.0f, 0.8f);

    float fPosX = 320 - nWidth / 2;
    float fPosY;
    if (nOriginY == -1)
    {
        fPosY = 130 - nHeight / 2;
        if (fPosY < 40.0f)
            fPosY = 40.0f;
    }
    else
        fPosY = nOriginY;
    POINT ptPos = {static_cast<LONG>(fPosX), static_cast<LONG>(fPosY)};

    RenderBitmap(BITMAP_INTERFACE + 22, fPosX, fPosY, nWidth, 5, 0.f, 0.f, nWidth / 256.f,
                 5.f / 8.f);
    fPosY += 5;

    int nBodyHeight = nHeight - 10;
    int nPatternCount = nBodyHeight / 40;
    for (int i = 0; i < nPatternCount; ++i)
    {
        RenderBitmap(BITMAP_INTERFACE + 21, fPosX, fPosY, nWidth, 40, 0.f, 0.f, nWidth / 256.f,
                     40.f / 64.f);
        fPosY += 40;
    }

    if (nBodyHeight > nPatternCount * 40)
    {
        float fRate = (float)(nBodyHeight - nPatternCount * 40) / 40.0f;
        RenderBitmap(BITMAP_INTERFACE + 21, fPosX, fPosY, nWidth, 40 * fRate, 0.f, 0.f,
                     nWidth / 256.f, (40.f / 64.f) * fRate);
        fPosY += 40 * fRate;
    }

    RenderBitmap(BITMAP_INTERFACE + 22, fPosX, fPosY, nWidth, 5, 0.f, 0.f, nWidth / 256.f,
                 5.f / 8.f);

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    DisableAlphaBlend();

    return ptPos;
}

void CUIManager::Render()
{
}

#define DOCK_EXTENT 10

//#define	UIM_TS_BG_BLACK		0
#define UIM_TS_BACK0 0
#define UIM_TS_BACK1 1
#define UIM_TS_121518 3
#define UIM_TS_BACK2 5
#define UIM_TS_BACK3 6
#define UIM_TS_BACK4 7
#define UIM_TS_BACK5 8
#define UIM_TS_BACK6 9
#define UIM_TS_BACK7 10
#define UIM_TS_BACK8 11
#define UIM_TS_BACK9 12
#define UIM_TS_MAX 13

void CUIMng::RenderTitleSceneUI(HDC hDC, DWORD dwNow, DWORD dwTotal)
{
    renderer_.BeginOpengl();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderer_.BeginBitmap();

    for (int i = 0; i < UIM_TS_MAX; ++i)
    {
        if (i == 2)
            continue;
        m_asprTitle[i].Render();
    }

    m_pgbLoding->SetValue(dwNow, dwTotal);
    m_pgbLoding->Render();

    EndBitmap();
    EndOpengl();
}

void CUIMng::Render()
{
    if (UIM_SCENE_NONE == m_nScene)
        return;

    m_CharInfoBalloonMng->Render();

    CWin *pWin;
    NODE *position = m_WinList.GetTailPosition();
    while (position)
    {
        pWin = (CWin *)m_WinList.GetPrev(position);
        pWin->Render();
    }
}

bool CUIMng::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight)
{
    if (m_nScene == UIM_SCENE_MAIN)
        return PrepareModernMainUiOnWorker(viewportWidth, viewportHeight);
    if (m_nScene != UIM_SCENE_LOGIN && m_nScene != UIM_SCENE_CHARACTER)
    {
        return true;
    }
    if (m_MsgWin != nullptr && !m_MsgWin->PrepareModernUiOnWorker(viewportWidth, viewportHeight))
    {
        return false;
    }
    if (m_SysMenuWin != nullptr &&
        !m_SysMenuWin->PrepareModernUiOnWorker(viewportWidth, viewportHeight))
    {
        return false;
    }
    if (g_pOption != nullptr && !g_pOption->PrepareModernUiOnWorker(viewportWidth, viewportHeight))
    {
        return false;
    }
    if (m_nScene == UIM_SCENE_CHARACTER)
    {
        return (m_ServerMsgWin == nullptr ||
                m_ServerMsgWin->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
               (m_CharMakeWin == nullptr ||
                m_CharMakeWin->PrepareModernUiOnWorker(viewportWidth, viewportHeight));
    }
    return (m_LoginMainWin == nullptr ||
            m_LoginMainWin->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (m_LoginWin == nullptr ||
            m_LoginWin->PrepareModernUiOnWorker(viewportWidth, viewportHeight));
}

bool CUIMng::PrepareModernMainUiOnWorker(int viewportWidth, int viewportHeight)
{
    return sessionKeeper_.MessageBoxManagerObject().PrepareModernUiOnWorker(viewportWidth,
                                                                            viewportHeight) &&
           (g_pNewUISystem == nullptr || g_pNewUISystem->GetUI_NewFriendWindow() == nullptr ||
            g_pNewUISystem->GetUI_NewFriendWindow()->PrepareModernUiOnWorker(viewportWidth,
                                                                             viewportHeight)) &&
           (g_pOption == nullptr ||
            g_pOption->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || g_pCommandWindow == nullptr ||
            g_pCommandWindow->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           PrepareModernInventoryUiOnWorker(viewportWidth, viewportHeight) &&
           (g_pNewUISystem == nullptr || g_pCharacterInfoWindow == nullptr ||
            g_pCharacterInfoWindow->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || g_pPetInfoWindow == nullptr ||
            g_pPetInfoWindow->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUIMuHelper == nullptr ||
            g_pNewUIMuHelper->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pChatInputBox == nullptr ||
            g_pChatInputBox->PrepareModernUiOnWorker(viewportWidth, viewportHeight));
}

bool CUIMng::PrepareModernEventUiOnWorker(int viewportWidth, int viewportHeight)
{
    if (!sessionKeeper_.Ui()->MapName().PrepareModernUiOnWorker(viewportWidth, viewportHeight))
        return false;
    if (!g_pNewUISystem)
        return true;
#ifdef PBG_ADD_GENSRANKING
    if (g_pNewUIGensRanking &&
        !g_pNewUIGensRanking->PrepareModernUiOnWorker(viewportWidth, viewportHeight))
        return false;
#endif
    return (!g_pDuelWatchWindow ||
            g_pDuelWatchWindow->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (!g_pGoldBowmanInterface ||
            g_pGoldBowmanInterface->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (!g_pKanturu2ndEnterNpc ||
            g_pKanturu2ndEnterNpc->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (!g_pKanturuInfoWindow ||
            g_pKanturuInfoWindow->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (!g_pEnterBloodCastle ||
            g_pEnterBloodCastle->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (!g_pGatemanWindow ||
            g_pGatemanWindow->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (!g_pEmpireGuardianNPC ||
            g_pEmpireGuardianNPC->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (!g_pSlideHelpMgr ||
            g_pSlideHelpMgr->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (!g_pCursedTempleWindow ||
            g_pCursedTempleWindow->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (!g_pCursedTempleResultWindow ||
            g_pCursedTempleResultWindow->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (!g_pNewUISystem->GetUI_NewNameWindow() ||
            g_pNewUISystem->GetUI_NewNameWindow()->PrepareModernUiOnWorker(viewportWidth,
                                                                           viewportHeight));
}

bool CUIMng::PrepareModernInventoryUiOnWorker(int viewportWidth, int viewportHeight)
{
    return PrepareModernEventUiOnWorker(viewportWidth, viewportHeight) &&
           (g_pNewUISystem == nullptr || !g_pHelp ||
            g_pHelp->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || !g_pGuildInfoWindow ||
            g_pGuildInfoWindow->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || !g_pGuildMakeWindow ||
            g_pGuildMakeWindow->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || !g_pMyQuestInfoWindow ||
            g_pMyQuestInfoWindow->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || !g_pQuestProgress ||
            g_pQuestProgress->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || !g_pQuestProgressByEtc ||
            g_pQuestProgressByEtc->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || !g_pNPCQuest ||
            g_pNPCQuest->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || !g_pNPCDialogue ||
            g_pNPCDialogue->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || !g_pItemExplanation ||
            g_pItemExplanation->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || !g_pSetItemExplanation ||
            g_pSetItemExplanation->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || !g_pSkillList ||
            g_pSkillList->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || !g_pMasterLevelInterface ||
            g_pMasterLevelInterface->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || !g_pBuffWindow ||
            g_pBuffWindow->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || !g_pNewUIMiniMap ||
            g_pNewUIMiniMap->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || !g_pItemEnduranceInfo ||
            g_pItemEnduranceInfo->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || !g_pWindowMenu ||
            g_pWindowMenu->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || !g_pNewUISystem->GetUI_pNewUnitedMarketPlaceWindow() ||
            g_pNewUISystem->GetUI_pNewUnitedMarketPlaceWindow()->PrepareModernUiOnWorker(
                viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || g_pQuickCommand == nullptr ||
            g_pQuickCommand->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || g_pMixInventory == nullptr ||
            g_pMixInventory->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || g_pLuckyItemWnd == nullptr ||
            g_pLuckyItemWnd->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || g_pTrade == nullptr ||
            g_pTrade->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || g_pStorageInventory == nullptr ||
            g_pStorageInventory->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || g_pStorageInventoryExt == nullptr ||
            g_pStorageInventoryExt->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || g_pNPCShop == nullptr ||
            g_pNPCShop->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || g_pMyInventory == nullptr ||
            g_pMyInventory->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || g_pMyShopInventory == nullptr ||
            g_pMyShopInventory->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || g_pMyInventoryExt == nullptr ||
            g_pMyInventoryExt->PrepareModernUiOnWorker(viewportWidth, viewportHeight)) &&
           (g_pNewUISystem == nullptr || g_pPurchaseShopInventory == nullptr ||
            g_pPurchaseShopInventory->PrepareModernUiOnWorker(viewportWidth, viewportHeight));
}

void SessionUiUnit::RenderImage(std::uint32_t uiImageType, float x, float y, float width,
                                float height)
{
    const SessionBitmapMetadata pImage = Bitmaps[uiImageType];
    if (!IsValid(pImage.Asset))
    {
        return;
    }

    float u, v, uw, vh;

    u = 0.5f / (float)pImage.Width;
    v = 0.5f / (float)pImage.Height;
    uw = (width - 0.5f) / (float)pImage.Width;
    vh = (height - 0.5f) / (float)pImage.Height;

    RenderBitmap(uiImageType, x, y, width, height, u, v, uw - u, vh - v);
}

void SessionUiUnit::RenderImage(std::uint32_t uiImageType, float x, float y, float width,
                                float height, float su, float sv, float uw, float vh, DWORD color)
{
    RenderColorBitmap(uiImageType, x, y, width, height, su, sv, uw, vh, color);
}

void SessionUiUnit::RenderImageStretch(std::uint32_t uiImageType, float x, float y, float width,
                                       float height, float sx, float sy, float sw, float sh,
                                       DWORD color)
{
    const SessionBitmapMetadata pImage = Bitmaps[uiImageType];
    if (!IsValid(pImage.Asset))
    {
        return;
    }

    // Normalize the source texel region to UV. Extent is independent of the dest
    // size, so GL scales the (sw x sh) source onto the (width x height) quad.
    float u = (sx + 0.5f) / (float)pImage.Width;
    float v = (sy + 0.5f) / (float)pImage.Height;
    float uw = (sw - 1.0f) / (float)pImage.Width;
    float vh = (sh - 1.0f) / (float)pImage.Height;

    RenderColorBitmap(uiImageType, x, y, width, height, u, v, uw, vh, color);
}

void SessionUiUnit::RenderImage(std::uint32_t uiImageType, float x, float y, float width,
                                float height, float su, float sv)
{
    const SessionBitmapMetadata pImage = Bitmaps[uiImageType];
    if (!IsValid(pImage.Asset))
    {
        return;
    }

    float u, v, uw, vh;
    u = ((su + 0.5f) / (float)pImage.Width);
    v = ((sv + 0.5f) / (float)pImage.Height);
    uw = (width - 0.5f) / (float)pImage.Width - (0.5f / (float)pImage.Width);
    vh = (height - 0.5f) / (float)pImage.Height - (0.5f / (float)pImage.Height);

    RenderBitmap(uiImageType, x, y, width, height, u, v, uw, vh);
}

void SessionUiUnit::RenderImage(std::uint32_t uiImageType, float x, float y, float width,
                                float height, float su, float sv, DWORD color)
{
    const SessionBitmapMetadata pImage = Bitmaps[uiImageType];
    if (!IsValid(pImage.Asset))
    {
        return;
    }

    float u, v, uw, vh;
    u = ((su + 0.5f) / (float)pImage.Width);
    v = ((sv + 0.5f) / (float)pImage.Height);
    uw = (width - 0.5f) / (float)pImage.Width - (0.5f / (float)pImage.Width);
    vh = (height - 0.5f) / (float)pImage.Height - (0.5f / (float)pImage.Height);

    RenderColorBitmap(uiImageType, x, y, width, height, u, v, uw, vh, color);
}

void SessionLegacyCalls::RenderImage(std::uint32_t imageType, float x, float y, float width,
                                     float height) const
{
    sessionKeeper_.Ui()->RenderImage(imageType, x, y, width, height);
}

void SessionLegacyCalls::RenderImage(std::uint32_t imageType, float x, float y, float width,
                                     float height, float sourceU, float sourceV) const
{
    sessionKeeper_.Ui()->RenderImage(imageType, x, y, width, height, sourceU, sourceV);
}

void SessionLegacyCalls::RenderImage(std::uint32_t imageType, float x, float y, float width,
                                     float height, float sourceU, float sourceV, DWORD color) const
{
    sessionKeeper_.Ui()->RenderImage(imageType, x, y, width, height, sourceU, sourceV, color);
}

void SessionLegacyCalls::RenderImage(std::uint32_t imageType, float x, float y, float width,
                                     float height, float sourceU, float sourceV, float uWidth,
                                     float vHeight, DWORD color) const
{
    sessionKeeper_.Ui()->RenderImage(imageType, x, y, width, height, sourceU, sourceV, uWidth,
                                     vHeight, color);
}

void SessionLegacyCalls::RenderImageStretch(std::uint32_t imageType, float x, float y, float width,
                                            float height, float sourceX, float sourceY,
                                            float sourceWidth, float sourceHeight,
                                            DWORD color) const
{
    sessionKeeper_.Ui()->RenderImageStretch(imageType, x, y, width, height, sourceX, sourceY,
                                            sourceWidth, sourceHeight, color);
}

float SessionLegacyCalls::RenderNumber(float x, float y, int number, float scale)
{
    return sessionKeeper_.Renderer()->RenderNumber(x, y, number, scale);
}

float SessionRenderUnit::RenderNumber(float x, float y, int number, float scale)
{
    EnableAlphaTest();
    if (scale < 0.3f)
    {
        return x;
    }

    const float width = 12.f * (scale - 0.3f);
    const float height = 16.f * (scale - 0.3f);

    wchar_t text[32];
    _itow(number, text, 10);
    const int length = static_cast<int>(wcslen(text));

    x -= width * length / 2;
    for (int i = 0; i < length; ++i)
    {
        const float u = static_cast<float>(text[i] - L'0') * 12.f / 128.f;
        RenderBitmap(BITMAP_INTERFACE_NEW_NUMBER_BEGIN, x, y, width, height, u, 0.f, 12.f / 128.f,
                     14.f / 16.f);
        x += width * 0.8f;
    }

    return x;
}

//	NewUIGroup.cpp

using namespace SEASON3B;

bool CNewUIGroup::Render()
{
    if (IsVisible() == false)
        return false;

    auto vi = m_vecUI.begin();
    for (; vi != m_vecUI.end(); vi++)
    {
        if ((*vi)->IsVisible() == true)
        {
            (*vi)->Render();
        }
    }

    return true;
}

void RenderColor(float x, float y, float Width, float Height, float Alpha, int Flag);

void CUIWindowMgr::Render()
{
    for (m_WindowArrangeListIter = m_WindowArrangeList.begin();
         m_WindowArrangeListIter != m_WindowArrangeList.end(); ++m_WindowArrangeListIter)
    {
        m_WindowMapIter = m_WindowMap.find(*m_WindowArrangeListIter);
        if (m_WindowMapIter != m_WindowMap.end())
        {
            if (m_WindowMapIter->second->GetState() != UISTATE_HIDE &&
                m_WindowMapIter->second->GetState() != UISTATE_READY)
                m_WindowMapIter->second->Render();
        }
    }
    m_bRenderFrame = TRUE;
}

bool CUIWindowMgr::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight, bool visible)
{
    bool prepared = true;
    for (const auto &[id, window] : m_WindowMap)
    {
        (void)id;
        const bool windowVisible = visible && window != nullptr &&
                                   window->GetState() != UISTATE_HIDE &&
                                   window->GetState() != UISTATE_READY;
        if (window != nullptr)
            prepared =
                window->PrepareModernUiOnWorker(viewportWidth, viewportHeight, windowVisible) &&
                prepared;
    }
    return prepared;
}

bool CUIWindowMgr::RecordModernUi(LegacyRenderFacade &facade) const
{
    bool recorded = true;
    for (const DWORD id : m_WindowArrangeList)
    {
        const auto window = m_WindowMap.find(id);
        if (window != m_WindowMap.end() && window->second != nullptr &&
            window->second->GetState() != UISTATE_HIDE &&
            window->second->GetState() != UISTATE_READY)
        {
            recorded = window->second->RecordModernUi(facade) && recorded;
        }
    }
    return recorded;
}

void SessionUiUnit::RenderWindowVLine(float pos_x, float pos_y, float height)
{
    SetLineColor(2);
    RenderColor(pos_x, pos_y, 1.0f, height);
    RenderColor(pos_x + 4, pos_y, 1.0f, height);
    SetLineColor(1);
    RenderColor(pos_x + 1, pos_y, 3.0f, height);
}

void SessionUiUnit::RenderWindowHLine(float pos_x, float pos_y, float width)
{
    SetLineColor(2);
    RenderColor(pos_x, pos_y, width, 1.0f);
    RenderColor(pos_x, pos_y + 4, width, 1.0f);
    SetLineColor(1);
    RenderColor(pos_x, pos_y + 1, width, 3.0f);
}

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

void SessionUiUnit::RenderTabLine(int iPos_x, int iPos_y, int iTabWidth, int iTabHeight,
                                  int iTabNum, int iSelectNum)
{
    for (int i = 0; i < iTabNum; ++i)
    {
        SetLineColor(2);
        auto fRPos_x = float(iPos_x + i * iTabWidth);
        if (i == iSelectNum)
        {
            RenderColor((float)fRPos_x, (float)iPos_y, (float)iTabWidth, (float)1);
            RenderColor((float)fRPos_x - 1, (float)iPos_y, (float)1, (float)iTabHeight);
            RenderColor((float)fRPos_x + iTabWidth - 1, (float)iPos_y, (float)1, (float)iTabHeight);
            SetLineColor(5);
            RenderColor((float)fRPos_x, (float)iPos_y + 1, (float)iTabWidth - 1,
                        (float)iTabHeight - 1);
        }
        else
        {
            RenderColor((float)fRPos_x, (float)iPos_y + 1, (float)iTabWidth, (float)1);
            RenderColor((float)fRPos_x + iTabWidth - 1, (float)iPos_y + 1, (float)1,
                        (float)iTabHeight - 1);
            RenderColor((float)fRPos_x, (float)iPos_y + iTabHeight - 1, (float)iTabWidth, (float)1);
            SetLineColor(6);
            RenderColor((float)fRPos_x, (float)iPos_y + 2, (float)iTabWidth - 1,
                        (float)iTabHeight - 3);
        }
    }
}

void SessionLegacyCalls::RenderWindowVLine(float x, float y, float height)
{
    sessionKeeper_.Ui()->RenderWindowVLine(x, y, height);
}

void SessionLegacyCalls::RenderWindowHLine(float x, float y, float width)
{
    sessionKeeper_.Ui()->RenderWindowHLine(x, y, width);
}

void SessionLegacyCalls::RenderTabLine(int x, int y, int tabWidth, int tabHeight, int tabCount,
                                       int selectedTab)
{
    sessionKeeper_.Ui()->RenderTabLine(x, y, tabWidth, tabHeight, tabCount, selectedTab);
}

bool SEASON3B::CNewUIManager::Render()
{
    RefreshOrderedUI();
    bool overlayRecorded = false;
    bool recorded = true;

    for (auto vi = m_layerOrderedUI.begin(); vi != m_layerOrderedUI.end(); ++vi)
    {
        CNewUIObj *const ui = *vi;
        // The map is already recorded by the scene, below world labels and panels.
        if (ui && ui->GetLayerDepth() == UI::Modern::MigratedUiRenderLayers::Minimap)
            continue;
        if (ui != nullptr && ui->IsVisible())
        {
            if (!overlayRecorded &&
                UI::Modern::MigratedUiRenderLayers::IsDialog(ui->GetLayerDepth()))
            {
                recorded = RenderPickedItemOverlay();
                overlayRecorded = true;
            }
            ui->Render();
        }
    }

    if (!overlayRecorded)
        recorded = RenderPickedItemOverlay();
    if (g_pNewUI3DRenderMng)
        g_pNewUI3DRenderMng->RenderUI2DEffects();
    return sessionKeeper_.Renderer()->RecordTooltips() && recorded;
}

bool SEASON3B::CNewUIManager::RenderPickedItemOverlay()
{
    if (!g_pNewUISystem || !g_pMyInventory || !g_pMyInventory->GetInventoryCtrl())
        return true;
    auto *picked = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();
    return !picked || picked->RenderOwnerLayer();
}

//bool SortUiObj(const INewUIBase& lhs, const INewUIBase& rhs)
//{
//	return lhs.GetDisplayOrder() > rhs.GetDisplayOrder();
//}

void CNewUISystem::Show(DWORD dwKey)
{
#ifdef PBG_ADD_INGAMESHOP_UI_ITEMSHOP
    if (g_pInGameShop->IsInGameShop())
        return;
#endif //PBG_ADD_INGAMESHOP_UI_ITEMSHOP
    if (!m_pNewUIMng)
    {
        return;
    }

    if (dwKey == INTERFACE_INVENTORY_EXT && CharacterAttribute->InventoryExtensions <= 0)
    {
        g_pSystemLogBox->AddText(I18N::Game::YouMustPurchaseExpandedInventoryFirst,
                                 TYPE_ERROR_MESSAGE);
        return;
    }

    /*
    std::list<INewUIBase*> visiblePages = {};

    for (int i = INTERFACE_LIST::INTERFACE_BEGIN; i < INTERFACE_LIST::INTERFACE_END; i++)
    {
        auto const uiObj = m_pNewUIMng->FindUIObj(i);
        if (uiObj->IsVisible() && uiObj->IsRightSideMenu())
        {
            visiblePages.push_back(uiObj);
        }
    }

    visiblePages.sort(SortUiObj);
    // TODO: Close all above the margin.
    */
    // TODO: Refactor this whole method. How would be a fixed priority order
    // for each window. And a maximum of open windows, depending on resolution

    if (dwKey == INTERFACE_FRIEND)
    {
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_FRIEND, true);

        m_pNewFriendWindow->OpenMainWnd(640 - 250, 432 - 173);
    }
    else if (dwKey == INTERFACE_INVENTORY)
    {
        HideGroupBeforeOpenInterface();

        g_pMainFrame->SetBtnState(MAINFRAME_BTN_MYINVEN, true);

        if (IsVisible(INTERFACE_CHARACTER))
        {
            g_pMyInventory->SetPos(UiSystemDetail::PanelColumnX(2), 0);
        }
        if (IsVisible(INTERFACE_MYQUEST))
        {
            Hide(INTERFACE_MYQUEST);
        }
        g_pMyInventory->OpenningProcess();
    }
    else if (dwKey == INTERFACE_INVENTORY_EXT)
    {
        if (IsVisible(INTERFACE_STORAGE_EXT))
        {
            Hide(INTERFACE_STORAGE_EXT);
        }

        if (IsVisible(INTERFACE_STORAGE))
        {
            g_pStorageInventory->SetPos(UiSystemDetail::PanelColumnX(3), 0);
            Hide(INTERFACE_HERO_POSITION_INFO);
        }

        if (IsVisible(INTERFACE_MYSHOP_INVENTORY))
        {
            g_pMyShopInventory->SetPos(UiSystemDetail::PanelColumnX(3), 0);
            Hide(INTERFACE_HERO_POSITION_INFO);
            if (IsVisible(INTERFACE_MYQUEST))
            {
                Hide(INTERFACE_MYQUEST);
            }

            if (IsVisible(INTERFACE_CHARACTER))
            {
                Hide(INTERFACE_CHARACTER);
            }
        }
        if (IsVisible(INTERFACE_MYQUEST))
        {
            Hide(INTERFACE_MYQUEST);
        }
        if (IsVisible(INTERFACE_CHARACTER))
        {
            Hide(INTERFACE_CHARACTER);
        }
        if (IsVisible(INTERFACE_NPCSHOP))
        {
            g_pNPCShop->SetPos(UiSystemDetail::PanelColumnX(3), 0);
            Hide(INTERFACE_HERO_POSITION_INFO);
        }
        if (IsVisible(INTERFACE_MIXINVENTORY))
        {
            g_pMixInventory->SetPos(UiSystemDetail::PanelColumnX(3), 0);
            Hide(INTERFACE_HERO_POSITION_INFO);
        }
        if (IsVisible(INTERFACE_TRADE))
        {
            g_pTrade->SetPos(UiSystemDetail::PanelColumnX(3), 0);
            Hide(INTERFACE_HERO_POSITION_INFO);
        }
    }
    else if (dwKey == INTERFACE_CHARACTER)
    {
        HideGroupBeforeOpenInterface();

        g_pMainFrame->SetBtnState(MAINFRAME_BTN_CHAINFO, true);

        if (IsVisible(INTERFACE_INVENTORY))
        {
            g_pMyInventory->SetPos(UiSystemDetail::PanelColumnX(2), 0);
            if (IsVisible(INTERFACE_INVENTORY_EXT))
            {
                g_pMyInventory->SetPos(UiSystemDetail::PanelColumnX(3), 0);
                Hide(INTERFACE_HERO_POSITION_INFO);
            }
        }
        else if (IsVisible(INTERFACE_MYQUEST))
        {
            g_pMyQuestInfoWindow->SetPos(UiSystemDetail::PanelColumnX(2), 0);
        }
        g_pCharacterInfoWindow->OpenningProcess();
    }
    else if (dwKey == INTERFACE_PET)
    {
        if (IsVisible(INTERFACE_INVENTORY))
        {
            Hide(INTERFACE_INVENTORY);
        }
        if (IsVisible(INTERFACE_MYQUEST))
        {
            Hide(INTERFACE_MYQUEST);
        }

        HideGroupBeforeOpenInterface();

        m_pNewUIMng->ShowInterface(INTERFACE_CHARACTER);
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_CHAINFO, true);
        m_pNewPetInfoWindow->OpenningProcess();
    }
    else if (dwKey == INTERFACE_MYQUEST)
    {
        HideGroupBeforeOpenInterface();

        if (IsVisible(INTERFACE_CHARACTER))
        {
            g_pMyQuestInfoWindow->SetPos(UiSystemDetail::PanelColumnX(2), 0);
        }
        if (IsVisible(INTERFACE_INVENTORY))
        {
            Hide(INTERFACE_INVENTORY);
        }
        if (IsVisible(INTERFACE_PET))
        {
            Hide(INTERFACE_PET);
        }
        g_pMyQuestInfoWindow->OpenningProcess();
    }
    else if (dwKey == INTERFACE_MIXINVENTORY)
    {
        HideAllGroupA();
        g_pMixInventory->OpeningProcess();
        m_pNewUIMng->ShowInterface(INTERFACE_INVENTORY);
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_MYINVEN, true);
    }
    else if (dwKey == INTERFACE_NPCSHOP)
    {
        HideAllGroupA();
        g_pNPCShop->OpenningProcess();
        m_pNewUIMng->ShowInterface(INTERFACE_INVENTORY);
        g_pNPCShop->SetPos(UiSystemDetail::PanelColumnX(2), 0);
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_MYINVEN, true);
    }
    else if (dwKey == INTERFACE_STORAGE)
    {
        const bool isExtendedInventoryOpen = IsVisible(INTERFACE_INVENTORY_EXT);
        HideAllGroupA();
        m_pNewUIMng->ShowInterface(INTERFACE_INVENTORY);
        if (isExtendedInventoryOpen)
        {
            Show(INTERFACE_INVENTORY_EXT);
            g_pStorageInventory->SetPos(UiSystemDetail::PanelColumnX(3), 0);
            Hide(INTERFACE_HERO_POSITION_INFO);
        }
        else
        {
            g_pStorageInventory->SetPos(UiSystemDetail::PanelColumnX(2), 0);
            Show(INTERFACE_HERO_POSITION_INFO);
        }

        g_pMainFrame->SetBtnState(MAINFRAME_BTN_MYINVEN, true);
    }
    else if (dwKey == INTERFACE_STORAGE_EXT)
    {
        if (IsVisible(INTERFACE_INVENTORY_EXT))
        {
            m_pNewUIMng->ShowInterface(INTERFACE_INVENTORY_EXT, false);
        }

        Hide(INTERFACE_HERO_POSITION_INFO);
        g_pStorageInventory->SetPos(UiSystemDetail::PanelColumnX(2), 0);
        g_pStorageInventoryExt->SetPos(UiSystemDetail::PanelColumnX(3), 0);

        m_pNewUIMng->ShowInterface(INTERFACE_STORAGE_EXT);
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_MYINVEN, true);
    }
    else if (dwKey == INTERFACE_MYSHOP_INVENTORY)
    {
        const bool isExtendedInventoryOpen = IsVisible(INTERFACE_INVENTORY_EXT);
        HideAllGroupA();
        m_pNewUIMng->ShowInterface(INTERFACE_INVENTORY);
        if (isExtendedInventoryOpen)
        {
            Show(INTERFACE_INVENTORY_EXT);
            g_pMyShopInventory->SetPos(UiSystemDetail::PanelColumnX(3), 0);
            Hide(INTERFACE_HERO_POSITION_INFO);
        }
        else
        {
            g_pMyShopInventory->SetPos(UiSystemDetail::PanelColumnX(2), 0);
            Show(INTERFACE_HERO_POSITION_INFO);
        }

        g_pMainFrame->SetBtnState(MAINFRAME_BTN_MYINVEN, true);
        g_pMyShopInventory->UpdateOpenButtonLockForCurrentMap();
    }
    else if (dwKey == INTERFACE_PURCHASESHOP_INVENTORY)
    {
        HideAllGroupA();
    }
    else if (dwKey == INTERFACE_NPCQUEST)
    {
        HideAllGroupA();
        g_pNPCQuest->ProcessOpening();
    }
    else if (dwKey == INTERFACE_TRADE)
    {
        HideAllGroupA();
        m_pNewUIMng->ShowInterface(INTERFACE_INVENTORY);
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_MYINVEN, true);
    }
    else if (dwKey == INTERFACE_BLOODCASTLE)
    {
        HideAllGroupA();
        g_pEnterBloodCastle->OpenningProcess();
    }
    else if (dwKey == INTERFACE_DEVILSQUARE)
    {
        HideAllGroupA();
        g_pEnterDevilSquare->OpenningProcess();
    }
    else if (dwKey == INTERFACE_CATAPULT)
    {
        HideAllGroupA();
        g_pCatapultWindow->OpenningProcess();
    }
    else if (dwKey == INTERFACE_COMMAND)
    {
        HideAllGroupA();
        m_pNewCommandWindow->OpenningProcess();
    }
    else if (dwKey == INTERFACE_GUILDINFO)
    {
        HideAllGroupA();
        g_pGuildInfoWindow->OpenningProcess();
    }
    else if (dwKey == INTERFACE_WINDOW_MENU)
    {
        g_pWindowMenu->OpenningProcess();
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_WINDOW, true);
    }
    else if (dwKey == INTERFACE_SENATUS)
    {
        HideAllGroupA();
        g_pCastleWindow->OpeningProcess();
    }
    else if (dwKey == INTERFACE_GUARDSMAN)
    {
        HideAllGroupA();
        g_pGuardWindow->OpeningProcess();
    }
    else if (dwKey == INTERFACE_GATEKEEPER)
    {
        HideAllGroupA();
        g_pGatemanWindow->OpeningProcess();
    }
    else if (dwKey == INTERFACE_GATESWITCH)
    {
        HideAllGroupA();
        g_pGateSwitchWindow->OpeningProcess();
    }
    else if (dwKey == INTERFACE_NPCGUILDMASTER)
    {
        HideAllGroupA();
        m_pNewGuildMakeWindow->OpeningProcess();
    }
    else if (dwKey == INTERFACE_MASTER_LEVEL)
    {
        HideAllGroupA();
    }
    else if (dwKey == INTERFACE_KANTURU2ND_ENTERNPC)
    {
        HideAllGroupB();
    }
    else if (dwKey == INTERFACE_HERO_POSITION_INFO)
    {
        m_pNewHeroPositionInfo->OpenningProcess();
    }
    else if (dwKey == INTERFACE_CHAOSCASTLE_TIME)
    {
        if (IsVisible(INTERFACE_CHATINPUTBOX))
        {
            Hide(INTERFACE_CHATINPUTBOX);
        }
        m_pNewChaosCastleTime->OpenningProcess();
    }
    else if (dwKey == INTERFACE_BLOODCASTLE_TIME)
    {
        g_pBloodCastle->OpenningProcess();
    }
    else if (dwKey == INTERFACE_OPTION)
    {
        g_pOption->OpenningProcess();
    }
    else if (dwKey == INTERFACE_HELP)
    {
        Hide(INTERFACE_MOVEMAP);
        Hide(INTERFACE_ITEM_EXPLANATION);
        Hide(INTERFACE_SETITEM_EXPLANATION);
        g_pHelp->OpenningProcess();
    }
    else if (dwKey == INTERFACE_ITEM_EXPLANATION)
    {
        Hide(INTERFACE_MOVEMAP);
        Hide(INTERFACE_HELP);
        Hide(INTERFACE_SETITEM_EXPLANATION);
        g_pItemExplanation->OpenningProcess();
    }
    else if (dwKey == INTERFACE_SETITEM_EXPLANATION)
    {
        Hide(INTERFACE_MOVEMAP);
        Hide(INTERFACE_HELP);
        Hide(INTERFACE_ITEM_EXPLANATION);
        g_pSetItemExplanation->OpenningProcess();
    }
    else if (dwKey == INTERFACE_QUICK_COMMAND)
    {
        g_pQuickCommand->OpenningProcess();
    }
    else if (dwKey == INTERFACE_MOVEMAP)
    {
        Hide(INTERFACE_HELP);
        Hide(INTERFACE_ITEM_EXPLANATION);
        Hide(INTERFACE_SETITEM_EXPLANATION);
        m_pNewMoveCommandWindow->OpenningProcess();
    }
    else if (dwKey == INTERFACE_CHATINPUTBOX)
    {
        m_pNewChatInputBox->OpenningProcess();
    }
    else if (dwKey == INTERFACE_SIEGEWARFARE)
    {
        m_pNewSiegeWarfare->OpenningProcess();
    }
    else if (dwKey == INTERFACE_ITEM_ENDURANCE_INFO)
    {
        m_pNewItemEnduranceInfo->OpenningProcess();
    }
    else if (dwKey == INTERFACE_BUFF_WINDOW)
    {
        m_pNewBuffWindow->OpenningProcess();
    }
    else if (dwKey == INTERFACE_CRYWOLF)
    {
        m_pNewCryWolfInterface->OpenningProcess();
    }
    else if (dwKey == INTERFACE_GOLD_BOWMAN)
    {
        m_pNewGoldBowman->OpeningProcess();
    }
    else if (dwKey == INTERFACE_GOLD_BOWMAN_LENA)
    {
        m_pNewGoldBowmanLena->OpeningProcess();
    }
    else if (dwKey == INTERFACE_LUCKYCOIN_REGISTRATION)
    {
        HideAllGroupA();
        g_pLuckyCoinRegistration->OpeningProcess();
        m_pNewUIMng->ShowInterface(INTERFACE_INVENTORY);
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_MYINVEN, true);
    }
    else if (dwKey == INTERFACE_EXCHANGE_LUCKYCOIN)
    {
        HideAllGroupA();
        g_pExchangeLuckyCoinWindow->OpenningProcess();
    }
    else if (dwKey == INTERFACE_DUELWATCH)
    {
        m_pNewDuelWatchWindow->OpeningProcess();
    }
    else if (dwKey == INTERFACE_DUELWATCH_MAINFRAME)
    {
        m_pNewDuelWatchMainFrameWindow->OpeningProcess();
    }
    else if (dwKey == INTERFACE_DUELWATCH_USERLIST)
    {
        m_pNewDuelWatchUserListWindow->OpeningProcess();
    }
#ifdef PBG_ADD_INGAMESHOP_UI_MAINFRAME
    else if (dwKey == INTERFACE_INGAMESHOP)
    {
        g_pInGameShop->LogSystemShow();
        HideAll();
        g_pInGameShop->OpeningProcess();
#ifndef KJH_MOD_SHOP_SCRIPT_DOWNLOAD
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_PARTCHARGE, true);
#endif // KJH_MOD_SHOP_SCRIPT_DOWNLOAD
    }
#endif //PBG_ADD_INGAMESHOP_UI_MAINFRAME
    else if (dwKey == INTERFACE_DOPPELGANGER_NPC)
    {
        m_pNewDoppelGangerWindow->OpeningProcess();
    }
    else if (dwKey == INTERFACE_DOPPELGANGER_FRAME)
    {
        m_pNewDoppelGangerFrame->OpenningProcess();
    }
    else if (dwKey == INTERFACE_NPC_DIALOGUE)
    {
        HideAllGroupA();
        g_pNPCDialogue->ProcessOpening();
    }
    else if (dwKey == INTERFACE_QUEST_PROGRESS)
    {
        HideAllGroupA();
        g_pQuestProgress->ProcessOpening();
    }
    else if (dwKey == INTERFACE_QUEST_PROGRESS_ETC)
    {
        if (IsVisible(INTERFACE_INVENTORY))
            Hide(INTERFACE_INVENTORY);
        if (IsVisible(INTERFACE_MYQUEST))
        {
            Hide(INTERFACE_MYQUEST);
            g_pQuestProgressByEtc->SetPos(UiSystemDetail::PanelColumnX(1), 0);
        }
        if (IsVisible(INTERFACE_CHARACTER))
            g_pQuestProgressByEtc->SetPos(UiSystemDetail::PanelColumnX(2), 0);
        g_pQuestProgressByEtc->ProcessOpening();
    }
    else if (dwKey == INTERFACE_EMPIREGUARDIAN_NPC)
    {
        m_pNewEmpireGuardianNPC->OpenningProcess();
    }
    else if (dwKey == INTERFACE_EMPIREGUARDIAN_TIMER)
    {
        m_pNewEmpireGuardianTimer->OpenningProcess();
    }
    else if (dwKey == INTERFACE_MINI_MAP)
    {
        m_pNewMiniMap->OpenningProcess();
    }
    else if (dwKey == INTERFACE_GENSRANKING)
    {
        HideAllGroupA();
        g_pNewUIGensRanking->OpenningProcess();
        g_pNewUIGensRanking->SetPos(UiSystemDetail::PanelColumnX(1), 0);
    }
    else if (dwKey == INTERFACE_UNITEDMARKETPLACE_NPC_JULIA)
    {
        m_pNewUnitedMarketPlaceWindow->OpeningProcess();
    }
    else if (dwKey == SEASON3B::INTERFACE_LUCKYITEMWND)
    {
        HideAllGroupA();
        g_pLuckyItemWnd->OpeningProcess();
        m_pNewUIMng->ShowInterface(SEASON3B::INTERFACE_INVENTORY);
    }
    else if (dwKey == INTERFACE_MUHELPER)
    {
        HideAllGroupA();
    }

    m_pNewUIMng->ShowInterface(dwKey);

    UpdateHeroPositionInfoVisibilityForLayoutChange(dwKey);

    int iScreenWidth = GetScreenWidth();
    m_pNewItemEnduranceInfo->SetPos(iScreenWidth);
    m_pNewBuffWindow->SetPos(iScreenWidth);
    m_pNewPartyListWindow->SetPos(iScreenWidth);
}

void CNewUISystem::Hide(DWORD dwKey)
{
    if (!m_pNewUIMng)
    {
        return;
    }

    if (dwKey == INTERFACE_FRIEND)
    {
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_FRIEND, false);
        m_pNewFriendWindow->HideAllWindow(TRUE, TRUE);
    }
    else if (dwKey == INTERFACE_CHARACTER)
    {
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_CHAINFO, false);
        if (IsVisible(INTERFACE_MYQUEST))
        {
            g_pMyQuestInfoWindow->SetPos(UiSystemDetail::PanelColumnX(1), 0);
        }
        if (IsVisible((INTERFACE_INVENTORY)))
        {
            g_pMyInventory->SetPos(UiSystemDetail::PanelColumnX(1), 0);
        }
        if (IsVisible((INTERFACE_PET)))
        {
            Hide(INTERFACE_PET);
        }
        if (IsVisible((INTERFACE_QUEST_PROGRESS_ETC)))
        {
            g_pQuestProgressByEtc->SetPos(UiSystemDetail::PanelColumnX(1), 0);
        }
    }
    else if (dwKey == INTERFACE_INVENTORY_EXT)
    {
        constexpr auto secondColumnX = UiSystemDetail::PanelColumnX(2);
        if (IsVisible(INTERFACE_MYSHOP_INVENTORY))
        {
            g_pMyShopInventory->SetPos(secondColumnX, 0);
        }

        if (IsVisible(INTERFACE_TRADE))
        {
            g_pTrade->SetPos(secondColumnX, 0);
        }

        if (IsVisible(INTERFACE_STORAGE))
        {
            g_pStorageInventory->SetPos(secondColumnX, 0);
        }

        if (IsVisible(INTERFACE_NPCSHOP))
        {
            g_pNPCShop->SetPos(secondColumnX, 0);
        }

        if (IsVisible(INTERFACE_MIXINVENTORY))
        {
            g_pMixInventory->SetPos(secondColumnX, 0);
        }

        Show(INTERFACE_HERO_POSITION_INFO);
    }
    else if (dwKey == INTERFACE_INVENTORY)
    {
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_MYINVEN, false);

        if (IsVisible(INTERFACE_INVENTORY_EXT))
        {
            Hide(INTERFACE_INVENTORY_EXT);
        }

        if (IsVisible(INTERFACE_MIXINVENTORY))
        {
            if (g_pMixInventory->ClosingProcess() == false)
                return;
            m_pNewUIMng->ShowInterface(INTERFACE_MIXINVENTORY, false);
        }
        if (IsVisible(SEASON3B::INTERFACE_LUCKYITEMWND))
        {
            if (g_pLuckyItemWnd->ClosingProcess() == false)
                return;
            m_pNewUIMng->ShowInterface(SEASON3B::INTERFACE_LUCKYITEMWND, false);
        }
        if (IsVisible(INTERFACE_NPCSHOP))
        {
            g_pNPCShop->ClosingProcess();
            m_pNewUIMng->ShowInterface(INTERFACE_NPCSHOP, false);
        }
        if (IsVisible(INTERFACE_MYSHOP_INVENTORY))
        {
            m_pNewUIMng->ShowInterface(INTERFACE_MYSHOP_INVENTORY, false);
        }
        if (IsVisible(INTERFACE_PURCHASESHOP_INVENTORY))
        {
            g_pPurchaseShopInventory->ClosingProcess();
            m_pNewUIMng->ShowInterface(INTERFACE_PURCHASESHOP_INVENTORY, false);
        }
        if (IsVisible(INTERFACE_STORAGE))
        {
            g_pStorageInventoryExt->ProcessClosing();
            if (!g_pStorageInventory->ProcessClosing())
                return;

            m_pNewUIMng->ShowInterface(INTERFACE_STORAGE_EXT, false);
            m_pNewUIMng->ShowInterface(INTERFACE_STORAGE, false);
        }
        if (IsVisible(INTERFACE_TRADE))
        {
            g_pTrade->ProcessCloseBtn();
            m_pNewUIMng->ShowInterface(INTERFACE_TRADE, false);
        }

        if (IsVisible(INTERFACE_LUCKYCOIN_REGISTRATION))
        {
            m_pNewLuckyCoinRegistration->ClosingProcess();
            m_pNewUIMng->ShowInterface(INTERFACE_LUCKYCOIN_REGISTRATION, false);
        }
        if (IsVisible(INTERFACE_EXCHANGE_LUCKYCOIN))
        {
            m_pNewExchangeLuckyCoinWindow->ClosingProcess();
            m_pNewUIMng->ShowInterface(INTERFACE_EXCHANGE_LUCKYCOIN, false);
        }

        if (IsVisible(SEASON3B::INTERFACE_LUCKYITEMWND))
        {
            m_pNewUILuckyItemWnd->ClosingProcess();
            m_pNewUIMng->ShowInterface(SEASON3B::INTERFACE_LUCKYITEMWND, false);
        }

        g_pMyInventory->SetPos(UiSystemDetail::PanelColumnX(1), 0);
        g_pMyInventory->ClosingProcess();
    }
    else if (dwKey == INTERFACE_MIXINVENTORY)
    {
        if (g_pMixInventory->ClosingProcess() == false)
        {
            return;
        }
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_MYINVEN, false);
        m_pNewUIMng->ShowInterface(INTERFACE_INVENTORY, false);
        Show(INTERFACE_HERO_POSITION_INFO);
    }
    else if (dwKey == INTERFACE_NPCSHOP)
    {
        if (IsVisible(INTERFACE_INVENTORY_EXT))
        {
            Hide(INTERFACE_INVENTORY_EXT);
        }
        g_pNPCShop->ClosingProcess();
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_MYINVEN, false);
        m_pNewUIMng->ShowInterface(INTERFACE_INVENTORY, false);
    }
    else if (dwKey == INTERFACE_MYSHOP_INVENTORY || dwKey == INTERFACE_PURCHASESHOP_INVENTORY)
    {
        if (dwKey == INTERFACE_MYSHOP_INVENTORY)
        {
            g_pMyShopInventory->ClosingProcess();
        }
        else if (dwKey == INTERFACE_PURCHASESHOP_INVENTORY)
        {
            g_pPurchaseShopInventory->ClosingProcess();
        }
        g_pMyInventory->SetPos(UiSystemDetail::PanelColumnX(1), 0);
        Show(INTERFACE_HERO_POSITION_INFO);
    }
    else if (dwKey == INTERFACE_STORAGE)
    {
        g_pStorageInventoryExt->ProcessClosing();
        if (!g_pStorageInventory->ProcessClosing())
            return;
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_MYINVEN, false);
        if (IsVisible(INTERFACE_INVENTORY_EXT))
        {
            Hide(INTERFACE_INVENTORY_EXT);
        }
        m_pNewUIMng->ShowInterface(INTERFACE_INVENTORY, false);
        Show(INTERFACE_HERO_POSITION_INFO);
    }
    else if (dwKey == INTERFACE_STORAGE_EXT)
    {
        Show(INTERFACE_HERO_POSITION_INFO);
    }
    else if (dwKey == INTERFACE_PET)
    {
        m_pNewPetInfoWindow->ClosingProcess();
    }
    else if (dwKey == INTERFACE_MYQUEST)
    {
        m_pNewMyQuestInfoWindow->ClosingProcess();

        m_pNewMyQuestInfoWindow->SetPos(UiSystemDetail::PanelColumnX(1), 0);
    }
    else if (dwKey == INTERFACE_SENATUS)
    {
        m_pNewCastleWindow->ClosingProcess();
    }
    else if (dwKey == INTERFACE_GUARDSMAN)
    {
        m_pNewGuardWindow->ClosingProcess();
    }
    else if (dwKey == INTERFACE_GATEKEEPER)
    {
        m_pNewGatemanWindow->ClosingProcess();
    }
    else if (dwKey == INTERFACE_GATESWITCH)
    {
        m_pNewGateSwitchWindow->ClosingProcess();
    }
    else if (dwKey == INTERFACE_NPCQUEST)
    {
        m_pNewNPCQuest->ProcessClosing();
    }
    else if (dwKey == INTERFACE_BLOODCASTLE)
    {
        g_pEnterBloodCastle->ClosingProcess();
    }
    else if (dwKey == INTERFACE_DEVILSQUARE)
    {
        g_pEnterDevilSquare->ClosingProcess();
    }
    else if (dwKey == INTERFACE_BLOODCASTLE_TIME)
    {
        g_pBloodCastle->ClosingProcess();
    }
    else if (dwKey == INTERFACE_TRADE)
    {
        if (IsVisible(INTERFACE_INVENTORY_EXT))
        {
            Hide(INTERFACE_INVENTORY_EXT);
        }
        g_pTrade->ProcessClosing();
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_MYINVEN, false);
        m_pNewUIMng->ShowInterface(INTERFACE_INVENTORY, false);
        Show(INTERFACE_HERO_POSITION_INFO);
    }
    else if (dwKey == INTERFACE_CATAPULT)
    {
        g_pCatapultWindow->ClosingProcess();
    }
    else if (dwKey == INTERFACE_CHAOSCASTLE_TIME)
    {
        m_pNewChaosCastleTime->ClosingProcess();
    }
    else if (dwKey == INTERFACE_COMMAND)
    {
        m_pNewCommandWindow->ClosingProcess();
    }
    else if (dwKey == INTERFACE_WINDOW_MENU)
    {
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_WINDOW, false);
    }
    else if (dwKey == INTERFACE_OPTION)
    {
    }
    else if (dwKey == INTERFACE_HERO_POSITION_INFO)
    {
        m_pNewHeroPositionInfo->ClosingProcess();
    }
    else if (dwKey == INTERFACE_HELP)
    {
        g_pHelp->ClosingProcess();
    }
    else if (dwKey == INTERFACE_ITEM_EXPLANATION)
    {
        g_pItemExplanation->ClosingProcess();
    }
    else if (dwKey == INTERFACE_SETITEM_EXPLANATION)
    {
        g_pSetItemExplanation->ClosingProcess();
    }
    else if (dwKey == INTERFACE_QUICK_COMMAND)
    {
        g_pQuickCommand->ClosingProcess();
    }
    else if (dwKey == INTERFACE_MOVEMAP)
    {
        m_pNewCommandWindow->ClosingProcess();
    }
    else if (dwKey == INTERFACE_CHATINPUTBOX)
    {
        m_pNewChatInputBox->ClosingProcess();
    }
    else if (dwKey == INTERFACE_GUILDINFO)
    {
        m_pNewGuildInfoWindow->ClosingProcess();
    }
    else if (dwKey == INTERFACE_NPCGUILDMASTER)
    {
        m_pNewGuildMakeWindow->ClosingProcess();
    }
    else if (dwKey == INTERFACE_SIEGEWARFARE)
    {
        m_pNewSiegeWarfare->ClosingProcess();
    }
    else if (dwKey == INTERFACE_ITEM_ENDURANCE_INFO)
    {
        m_pNewItemEnduranceInfo->ClosingProcess();
    }
    else if (dwKey == INTERFACE_BUFF_WINDOW)
    {
        m_pNewBuffWindow->ClosingProcess();
    }
    else if (dwKey == INTERFACE_CURSEDTEMPLE_RESULT)
    {
        m_pNewCursedTempleResultWindow->ClosingProcess();
    }
    else if (dwKey == INTERFACE_CRYWOLF)
    {
        m_pNewCryWolfInterface->ClosingProcess();
    }
    else if (dwKey == INTERFACE_GOLD_BOWMAN)
    {
        m_pNewGoldBowman->ClosingProcess();
    }
    else if (dwKey == INTERFACE_GOLD_BOWMAN_LENA)
    {
        m_pNewGoldBowmanLena->ClosingProcess();
    }
    else if (dwKey == INTERFACE_LUCKYCOIN_REGISTRATION)
    {
        m_pNewLuckyCoinRegistration->ClosingProcess();

        if (IsVisible(INTERFACE_INVENTORY))
        {
            m_pNewMyInventory->ClosingProcess();
            m_pNewUIMng->ShowInterface(INTERFACE_INVENTORY, false);
        }
    }
    else if (dwKey == INTERFACE_EXCHANGE_LUCKYCOIN)
    {
        m_pNewExchangeLuckyCoinWindow->ClosingProcess();

        if (IsVisible(INTERFACE_INVENTORY))
        {
            m_pNewMyInventory->ClosingProcess();
            m_pNewUIMng->ShowInterface(INTERFACE_INVENTORY, false);
        }
    }
    else if (dwKey == INTERFACE_DUELWATCH)
    {
        m_pNewDuelWatchWindow->ClosingProcess();
    }
    else if (dwKey == INTERFACE_DUELWATCH_MAINFRAME)
    {
        m_pNewDuelWatchMainFrameWindow->ClosingProcess();
    }
    else if (dwKey == INTERFACE_DUELWATCH_USERLIST)
    {
        m_pNewDuelWatchUserListWindow->ClosingProcess();
    }
#ifdef PBG_ADD_INGAMESHOP_UI_MAINFRAME
    else if (dwKey == INTERFACE_INGAMESHOP)
    {
        g_pInGameShop->ClosingProcess();
        g_pMainFrame->SetBtnState(MAINFRAME_BTN_PARTCHARGE, false);
    }
#endif //PBG_ADD_INGAMESHOP_UI_MAINFRAME
    else if (dwKey == INTERFACE_DOPPELGANGER_NPC)
    {
        m_pNewDoppelGangerWindow->ClosingProcess();
    }
    else if (dwKey == INTERFACE_DOPPELGANGER_FRAME)
    {
        m_pNewDoppelGangerFrame->ClosingProcess();
    }
    else if (dwKey == INTERFACE_NPC_DIALOGUE)
    {
        m_pNewNPCDialogue->ProcessClosing();
    }
    else if (dwKey == INTERFACE_QUEST_PROGRESS)
    {
        m_pNewQuestProgress->ProcessClosing();
    }
    else if (dwKey == INTERFACE_QUEST_PROGRESS_ETC)
    {
        m_pNewQuestProgressByEtc->ProcessClosing();
    }
    else if (dwKey == INTERFACE_EMPIREGUARDIAN_NPC)
    {
        m_pNewEmpireGuardianNPC->ClosingProcess();
    }
    else if (dwKey == INTERFACE_EMPIREGUARDIAN_TIMER)
    {
        m_pNewEmpireGuardianTimer->ClosingProcess();
    }
    else if (dwKey == INTERFACE_MINI_MAP)
    {
        m_pNewMiniMap->ClosingProcess();
    }
    else if (dwKey == INTERFACE_GENSRANKING)
    {
        g_pNewUIGensRanking->ClosingProcess();
    }
    else if (dwKey == INTERFACE_UNITEDMARKETPLACE_NPC_JULIA)
    {
        m_pNewUnitedMarketPlaceWindow->ClosingProcess();
    }
    else if (dwKey == SEASON3B::INTERFACE_LUCKYITEMWND)
    {
        if (g_pLuckyItemWnd->ClosingProcess() == false)
            return;
        if (IsVisible(SEASON3B::INTERFACE_INVENTORY))
        {
            m_pNewMyInventory->ClosingProcess();
            m_pNewUIMng->ShowInterface(SEASON3B::INTERFACE_INVENTORY, false);
        }
    }
    else if (dwKey == INTERFACE_MUHELPER)
    {
        m_pNewUIMng->ShowInterface(SEASON3B::INTERFACE_MUHELPER_SKILL_LIST, false);
    }

    m_pNewUIMng->ShowInterface(dwKey, false);

    UpdateHeroPositionInfoVisibilityForLayoutChange(dwKey);

    int iScreenWidth = GetScreenWidth();
    m_pNewItemEnduranceInfo->SetPos(iScreenWidth);
    m_pNewBuffWindow->SetPos(iScreenWidth);
    m_pNewPartyListWindow->SetPos(iScreenWidth);
}

void SessionUiUnit::SetLineColor(int iType, float fAlphaRate)
{
    unsigned char ubWindowAlpha = 255 * fAlphaRate;

    switch (iType)
    {
    case 0:
        glColor4ub(146, 134, 121, ubWindowAlpha);
        break;
    case 1:
        glColor4ub(37, 37, 37, ubWindowAlpha);
        break;
    case 2:
        glColor4ub(106, 97, 88, ubWindowAlpha);
        break;
    case 3:
        glColor4ub(0, 0, 0, 179 * fAlphaRate);
        break;
    case 4:
        glColor4ub(173, 167, 150, ubWindowAlpha);
        break;
    case 5:
        glColor4ub(53, 49, 48, ubWindowAlpha);
        break;
    case 6:
        glColor4ub(26, 22, 21, ubWindowAlpha);
        break;
    case 7:
        glColor4ub(0, 0, 0, 255 * fAlphaRate);
        break;
    case 8:
        glColor4ub(153, 156, 166, ubWindowAlpha);
        break;
    case 9:
        glColor4ub(136, 138, 147, ubWindowAlpha);
        break;
    case 10:
        glColor4ub(83, 85, 93, ubWindowAlpha);
        break;
    case 11:
        glColor4ub(102, 104, 112, ubWindowAlpha);
        break;
    case 12:
        glColor4ub(0, 0, 8, ubWindowAlpha);
        break;
    case 13:
        glColor4ub(0, 0, 0, ubWindowAlpha);
        break;
    case 14:
        glColor4ub(185, 185, 185, ubWindowAlpha);
        break;
    case 15:
        glColor4ub(194, 194, 194, ubWindowAlpha);
        break;
    case 16:
        glColor4ub(194, 194, 194, ubWindowAlpha);
        break;
    case 17:
        glColor4ub(209, 188, 134, ubWindowAlpha);
        break;
    case 18:
        glColor4ub(205, 209, 133, ubWindowAlpha);
        break;
    default:
        break;
    }
}
