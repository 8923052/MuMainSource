#include "render/UiAdapter.h"
#include "I18N/All.h"
#include "app/AppWindow.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/ChatSocial.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/WorldSimulation.h"
#include "render/Assets.h"
#include "render/FrameTape.h"
#include "render/ModelResources.h"
#include "render/Sprites.h"
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
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"
#include "ui/session/UiSessionRender.h"

namespace UI::Modern
{
Rml::Mesh LoadRmlVectorMesh(const std::filesystem::path &path)
{
    const RmlUiDesign design(path, {"Clip-Vertices", "Clip-Indices"});
    const auto points = design.Values(0);
    const auto indices = design.Values(1);
    const auto count = points.size() / 2;
    if (points.size() % 2 || count < 3 || count > std::numeric_limits<int>::max() ||
        indices.size() % 3 || indices.size() < 3)
        throw std::runtime_error("Invalid vector mesh dimensions: " + path.string());
    Rml::Mesh mesh;
    mesh.vertices.reserve(count);
    mesh.indices.reserve(indices.size());
    for (std::size_t i = 0; i < points.size(); i += 2)
    {
        if (!std::isfinite(points[i]) || !std::isfinite(points[i + 1]))
            throw std::runtime_error("Invalid vector mesh vertex: " + path.string());
        mesh.vertices.push_back({{points[i], points[i + 1]}, Rml::ColourbPremultiplied(255), {}});
    }
    for (const float index : indices)
    {
        if (!std::isfinite(index) || index < 0 || index >= count || std::trunc(index) != index)
            throw std::runtime_error("Invalid vector mesh index: " + path.string());
        mesh.indices.push_back(static_cast<int>(index));
    }
    return mesh;
}
} // namespace UI::Modern

using namespace SEASON3B;

// Construction/Destruction

void SEASON3B::CNewUI3DCamera::Begin3D()
{
    EndBitmap();
    glMatrixMode(GL_PROJECTION);
    SaveCameraPerspective();
    glPushMatrix();
    glLoadIdentity();
    glViewport2(0, 0, m_uiWidth, m_uiHeight);
    gluPerspective2(1.f, (float)(m_uiWidth) / (float)(m_uiHeight), RENDER_ITEMVIEW_NEAR,
                    RENDER_ITEMVIEW_FAR);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    cameraProjection_.GetModelViewMatrix(g_Camera.Matrix);
    EnableDepthTest();
    EnableDepthMask();
    glClear(GL_DEPTH_BUFFER_BIT);
}

void SEASON3B::CNewUI3DCamera::End3D()
{
    UpdateMousePositionn();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    BeginBitmap();
    RestoreCameraPerspective();
}

bool SEASON3B::CNewUI3DCamera::Render()
{
    if (m_list3DObjs.empty())
        return true;

    Begin3D();
    auto li = m_list3DObjs.begin();
    for (; li != m_list3DObjs.end(); li++)
    {
        if ((*li)->IsVisible() && !(*li)->IsOwnerRendered())
        {
            (*li)->Render3D();
        }
    }
    End3D();
    return true;
}

bool SEASON3B::CNewUI3DCamera::RenderObject(INewUI3DRenderObj &object)
{
    Begin3D();
    object.Render3D();
    End3D();
    return true;
}

bool SEASON3B::CNewUI3DRenderMng::RenderObject(INewUI3DRenderObj &object, float fZOrder)
{
    CNewUI3DCamera *const camera = FindCamera(fZOrder);
    return camera != nullptr && camera->RenderObject(object);
}

namespace UI::Modern
{

namespace
{
bool RecordDrawPlan(const RmlUiRenderSnapshot &snapshot, LegacyRenderFacade &facade) noexcept
{
    const std::span<const TrustedGeometryDraw> draws(snapshot.drawPlan);
    std::size_t first = 0;
    for (const auto &reset : snapshot.clipResets)
    {
        if (!facade.AppendTrustedGeometryDrawBatch(
                draws.subspan(first, reset.beforeDraw - first)) ||
            !facade.ClearStencil(reset.value))
            return false;
        first = reset.beforeDraw;
    }
    return facade.AppendTrustedGeometryDrawBatch(draws.subspan(first));
}
} // namespace

bool RmlUiRuntime::Record(const RmlUiRenderSnapshot &snapshot, LegacyRenderFacade &facade,
                          SessionId sessionId) const noexcept
{
    if (!facade.IsRecording() || !snapshot.valid ||
        !facade.MatchesViewport(snapshot.presentation.physicalWidth,
                                snapshot.presentation.physicalHeight))
    {
        return false;
    }
    if (!snapshot.drawPlan.empty() &&
        (snapshot.drawPlan.front().geometry->asset.sessionId != sessionId.RawValue() ||
         snapshot.drawPlan.front().geometry->asset.generation != facade.Generation().RawValue()))
    {
        return false;
    }
    return RecordDrawPlan(snapshot, facade);
}
} // namespace UI::Modern

void CUIButton::Render()
{
    EnableAlphaTest();

    if (GetState() == UISTATE_DISABLE)
    {
        glColor4f(1.0f, 0.4f, 0.4f, 1.0f);
        RenderBitmap(BITMAP_INTERFACE_EX + 9, m_iPos_x, m_iPos_y, (float)m_iWidth, (float)m_iHeight,
                     0.f, 0.f, 49.f / 64.f, 16.f / 16.f);

        if (m_pszCaption != nullptr)
        {
            SIZE TextSize;
            const int TextLen = lstrlen(m_pszCaption);
            g_RenderText.MeasureText(m_pszCaption, TextLen, &TextSize);
            g_RenderText.SetTextColor(230, 220, 200, 255);
            g_RenderText.SetBgColor(0);
            g_RenderText.RenderText(
                m_iPos_x + (m_iWidth - (float)TextSize.cx / g_fScreenRate_x + 0.5f) / 2,
                m_iPos_y + 1 + (m_iHeight - (float)TextSize.cy / g_fScreenRate_y + 0.5f) / 2,
                m_pszCaption);
        }
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }

    if (CheckMouseIn(m_iPos_x, m_iPos_y, m_iWidth, m_iHeight) == TRUE)
    {
        if (m_bMouseState == TRUE)
            glColor4f(0.6f, 0.6f, 0.6f, 1.0f);
        else
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    }
    else
    {
        glColor4f(0.8f, 0.8f, 0.8f, 1.0f);
    }

    if (m_bMouseState == TRUE)
        RenderBitmap(BITMAP_INTERFACE_EX + 9, m_iPos_x + 1, m_iPos_y + 1, (float)m_iWidth - 1,
                     (float)m_iHeight - 1, 0.f, 0.f, 48.f / 64.f, 15.f / 16.f);
    else
        RenderBitmap(BITMAP_INTERFACE_EX + 9, m_iPos_x, m_iPos_y, (float)m_iWidth, (float)m_iHeight,
                     0.f, 0.f, 49.f / 64.f, 16.f / 16.f);

    if (m_pszCaption != nullptr)
    {
        SIZE TextSize;
        const int TextLen = lstrlen(m_pszCaption);

        g_RenderText.MeasureText(m_pszCaption, TextLen, &TextSize);
        g_RenderText.SetTextColor(230, 220, 200, 255);
        g_RenderText.SetBgColor(0, 0, 0, 0);

        if (m_bMouseState == TRUE)
        {
            g_RenderText.RenderText(
                m_iPos_x + 1 + (m_iWidth - (float)TextSize.cx / g_fScreenRate_x + 0.5f) / 2,
                m_iPos_y + 2 + (m_iHeight - (float)TextSize.cy / g_fScreenRate_y + 0.5f) / 2,
                m_pszCaption);
        }
        else
        {
            g_RenderText.RenderText(
                m_iPos_x + (m_iWidth - (float)TextSize.cx / g_fScreenRate_x + 0.5f) / 2,
                m_iPos_y + 1 + (m_iHeight - (float)TextSize.cy / g_fScreenRate_y + 0.5f) / 2,
                m_pszCaption);
        }
    }
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    DisableAlphaBlend();
}

template <class T> void CUITextListBox<T>::Render()
{
    RenderInterface();

    MoveRenderLine();

    glColor3f(1.f, 1.f, 1.f);

    g_RenderText.SetFont(LegacyFontRole::Normal);

    for (int i = 0; i < m_iNumRenderLine; ++i)
    {
        if (m_TextListIter == m_TextList.end())
            break;
        BOOL bResult = RenderDataLine(i);
        if (bResult < 0)
        {
            i -= bResult;
        }
        else if (bResult == FALSE)
        {
            --i;
        }
        ++m_TextListIter;
    }
    RenderCoveredInterface();
}

template void CUITextListBox<GUILDLIST_TEXT>::Render();
template void CUITextListBox<WHISPER_TEXT>::Render();
template void CUITextListBox<LETTER_TEXT>::Render();
template void CUITextListBox<WINDOWLIST_TEXT>::Render();
template void CUITextListBox<LETTERLIST_TEXT>::Render();
template void CUITextListBox<SOCKETLIST_TEXT>::Render();
template void CUITextListBox<GUILDLOG_TEXT>::Render();
template void CUITextListBox<UNIONGUILD_TEXT>::Render();
template void CUITextListBox<FILTERLIST_TEXT>::Render();
template void CUITextListBox<UNMIX_TEXT>::Render();
template void CUITextListBox<BCDECLAREGUILD_TEXT>::Render();
template void CUITextListBox<BCGUILD_TEXT>::Render();
template void CUITextListBox<MOVECOMMAND_TEXT>::Render();
template void CUITextListBox<SCurQuestItem>::Render();
template void CUITextListBox<SQuestContents>::Render();
template void CUITextListBox<IGS_StorageItem>::Render();
template void CUITextListBox<IGS_BuyList>::Render();
template void CUITextListBox<IGS_SelectBuyItem>::Render();

void CUIGuildListBox::RenderInterface()
{
    const int iFillImageHeight = 40;
    const int iFrameImageHeight = 5;

    int iBlockHeight = m_iNumRenderLine / 3;
    m_iHeight = iFillImageHeight * iBlockHeight + iFrameImageHeight * 2;

    ComputeScrollBar();

    if (GetLineNum() >= m_iNumRenderLine)
    {
        RenderBitmap(BITMAP_INTERFACE_EX + 4, (float)m_iPos_x + m_iWidth - 19,
                     (float)m_iPos_y - m_iHeight + 8, 13.0f, 13.0f, 0.0f, 0.0f, 13.0f / 16.0f,
                     13.0f / 16.0f);
        EnableAlphaTest();
        RenderBitmap(BITMAP_INTERFACE_EX + 4, (float)m_iPos_x + m_iWidth - 19, (float)m_iPos_y - 4,
                     13.0f, -13.0f, 0.0f, 0.0f, 13.0f / 16.0f, 13.0f / 16.0f);
        DisableAlphaBlend();

        RenderBitmap(BITMAP_INTERFACE_EX + 3, (float)m_iPos_x + m_iWidth - m_fScrollBarWidth - 6,
                     m_fScrollBarRange_top, m_fScrollBarWidth,
                     m_fScrollBarRange_bottom - m_fScrollBarRange_top, 0.0f, 0.0f, 13.0f / 16.0f,
                     13.0f / 16.0f);

        RenderBitmap(BITMAP_INTERFACE_EX + 2, (float)m_iPos_x + m_iWidth - m_fScrollBarWidth - 5,
                     m_fScrollBarPos_y, m_fScrollBarWidth - 2, m_fScrollBarHeight, 0.0f, 0.0f,
                     11.0f / 16.0f, 11.0f / 16.0f);
    }
}

BOOL CUIGuildListBox::RenderDataLine(int iLineNumber)
{
    EnableAlphaTest();
    int iPos_x = m_iPos_x + 8;
    int iPos_y;
    if (GetLineNum() > m_iNumRenderLine)
        iPos_y = m_iPos_y - 16 - iLineNumber * 13;
    else
        iPos_y = m_iPos_y - 16 - (iLineNumber - GetLineNum() + m_iNumRenderLine) * 13;

    g_RenderText.SetFont(LegacyFontRole::Bold);

    if (++m_TextListIter == m_TextList.end())
    {
        --m_TextListIter;
        g_RenderText.SetBgColor(40, 40, 150, 255);
        if (m_TextListIter->m_Server != 255)
            g_RenderText.SetTextColor(0xFFFFFFFF);
        else
            g_RenderText.SetTextColor(255, 196, 196, 196);

        if (CreateGuildMark(Hero->GuildMarkIndex))
            RenderBitmap(BITMAP_GUILD, (float)iPos_x, (float)iPos_y, 8, 8);
        iPos_x += 13;
    }
    else
    {
        --m_TextListIter;
        g_RenderText.SetBgColor(0x00000000);
        if (m_TextListIter->m_Server != 255)
            g_RenderText.SetTextColor(210, 230, 255, 255);
        else
            g_RenderText.SetTextColor(210, 196, 196, 196);
    }

    wchar_t Text[MAX_TEXT_LENGTH + 1] = {0};
    mu_swprintf(Text, L"%ls", m_TextListIter->m_szID);
    g_RenderText.RenderText(iPos_x, iPos_y, Text);

    if (m_TextListIter->m_Server != 255 /* && m_TextListIter->m_Number != 0*/)
    {
        g_RenderText.SetBgColor(255, 196, 0, 255);
        g_RenderText.SetTextColor(0x00000000);
        mu_swprintf(Text, L"(%d)", m_TextListIter->m_Server + 1);
        g_RenderText.RenderText(m_iPos_x + m_iWidth - 60, iPos_y, Text);
    }

    if (m_bIsGuildMaster == TRUE || wcscmp(m_TextListIter->m_szID, Hero->ID) == 0)
    {
        float fWidth = 13;
        float fHeight = 11;
        float x = (float)m_iPos_x + m_iWidth - 22 - fWidth;
        float y = (float)iPos_y - 1;
        RenderBitmap(BITMAP_INVENTORY_BUTTON, x, y, fWidth, fHeight, 0.f, 0.f, 24.f / 32.f,
                     24.f / 32.f);
        if (MouseX >= x && MouseX < x + fWidth && MouseY >= y && MouseY < y + fHeight)
        {
            if (MouseLButton)
            {
                RenderBitmap(BITMAP_INVENTORY_BUTTON + 1, x, y, fWidth, fHeight, 0.f, 0.f,
                             24.f / 32.f, 24.f / 32.f);
            }

            g_RenderText.SetTextColor(255, 255, 255, 255);
            g_RenderText.SetBgColor(0, 0, 0, 255);

            if (wcscmp(m_TextListIter->m_szID, Hero->ID) == 0 &&
                wcscmp(GuildList[0].Name, Hero->ID) == 0)
                RenderTipText((int)x - 20, (int)y, I18N::Game::Disband);
            else
                RenderTipText((int)x - 20, (int)y, I18N::Game::Leave);
        }
    }

    g_RenderText.SetFont(LegacyFontRole::Normal);
    DisableAlphaBlend();
    return TRUE;
}

void CUISimpleChatListBox::Render()
{
    RenderInterface();
    MoveRenderLine();

    glColor3f(1.f, 1.f, 1.f);
    g_RenderText.SetFont(LegacyFontRole::Normal);

    for (int i = 0; i < m_iNumRenderLine; ++i)
    {
        if (m_TextListIter == m_RenderTextList.end())
            break;
        BOOL bResult = RenderDataLine(i);
        if (bResult < 0)
        {
            i -= bResult;
        }
        else if (bResult == FALSE)
        {
            --i;
        }
        ++m_TextListIter;
    }

    RenderCoveredInterface();
}

void CUISimpleChatListBox::RenderInterface()
{
    ComputeScrollBar();

    //if (GetLineNum() >= m_iNumRenderLine)
    {
        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - m_iHeight - 1, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 13.0f / 16.0f,
                         29.0f / 32.0f, -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 0.0f, 3.0f / 32.0f,
                         13.0f / 16.0f, 13.0f / 32.0f);

        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - 12, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 13.0f / 16.0f, 16.0f / 32.0f,
                         -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 0.0f, 16.0f / 32.0f, 13.0f / 16.0f,
                         13.0f / 32.0f);

        SetLineColor(2);
        RenderColor((float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 1, m_fScrollBarRange_top,
                    (float)1, (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        RenderColor((float)m_iPos_x + m_iWidth, m_fScrollBarRange_top, (float)1,
                    (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        EndRenderColor();

        if (GetLineNum() >= m_iNumRenderLine)
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, m_fScrollBarHeight, 0.0f, 1.0f / 32.0f,
                         11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarPos_y + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1, 0.0f,
                         2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
        else
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, m_fScrollBarRange_bottom - m_fScrollBarRange_top,
                         0.0f, 1.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarRange_top + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1,
                         0.0f, 2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
    }
}

BOOL CUISimpleChatListBox::RenderDataLine(int iLineNumber)
{
    wchar_t Text[MAX_TEXT_LENGTH + 1] = {0};

    SIZE TextSize = {0, 0};
    // 이름
    if (m_TextListIter->m_szID[0] != 0)
    {
        switch (m_TextListIter->m_iType)
        {
        case 0:
            g_RenderText.SetTextColor(0, 0, 0, 255);
            g_RenderText.SetBgColor(255, 200, 50, 0);
            break;
        case 1:
            g_RenderText.SetTextColor(100, 150, 255, 255);
            g_RenderText.SetBgColor(0, 0, 0, 0);
            break;
        case 2:
            g_RenderText.SetTextColor(255, 30, 0, 255);
            g_RenderText.SetBgColor(0, 0, 0, 0);
            break;
        case 3:
            g_RenderText.SetTextColor(239, 220, 205, 255);
            g_RenderText.SetBgColor(0, 0, 0, 0);
            break;
        case 4:
            g_RenderText.SetTextColor(0, 0, 0, 255);
            g_RenderText.SetBgColor(0, 200, 255, 0);
            break;
        case 5:
            g_RenderText.SetTextColor(0, 0, 0, 255);
            g_RenderText.SetBgColor(0, 255, 150, 0);
            break;
        }

        EnableAlphaTest();
        mu_swprintf(Text, L"%ls: ", m_TextListIter->m_szID);
        g_RenderText.RenderText(m_iPos_x + 8, m_iPos_y - 16 - iLineNumber * 13, Text, 0, 0,
                                RT3_SORT_LEFT, &TextSize);
        DisableAlphaBlend();
    }

    switch (m_TextListIter->m_iType)
    {
    case 0:
        g_RenderText.SetTextColor(0, 0, 0, 255);
        g_RenderText.SetBgColor(255, 200, 50, 0);
        break;
    case 1:
        g_RenderText.SetTextColor(70, 165, 210, 255);
        g_RenderText.SetBgColor(0, 0, 0, 0);
        break;
    case 2:
        g_RenderText.SetTextColor(255, 30, 0, 255);
        g_RenderText.SetBgColor(0, 0, 0, 0);
        break;
    case 3:
        g_RenderText.SetTextColor(230, 220, 200, 255);
        g_RenderText.SetBgColor(0, 0, 0, 0);
        break;
    case 4:
        g_RenderText.SetTextColor(0, 0, 0, 255);
        g_RenderText.SetBgColor(0, 200, 255, 0);
        break;
    case 5:
        g_RenderText.SetTextColor(0, 0, 0, 255);
        g_RenderText.SetBgColor(0, 255, 150, 0);
        break;
    }

    EnableAlphaTest();
    mu_swprintf(Text, L"%ls", m_TextListIter->m_szText);
    g_RenderText.RenderText(m_iPos_x + 8 + TextSize.cx, m_iPos_y - 16 - iLineNumber * 13, Text);
    DisableAlphaBlend();

    return TRUE;
}

void CUIChatPalListBox::RenderInterface()
{
    ComputeScrollBar();

    //if (GetLineNum() >= m_iNumRenderLine)
    {
        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - m_iHeight - 1, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 13.0f / 16.0f,
                         29.0f / 32.0f, -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 0.0f, 3.0f / 32.0f,
                         13.0f / 16.0f, 13.0f / 32.0f); // ▲

        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - 12, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 13.0f / 16.0f, 16.0f / 32.0f,
                         -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 0.0f, 16.0f / 32.0f, 13.0f / 16.0f,
                         13.0f / 32.0f); // ▼

        SetLineColor(2);
        RenderColor((float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 1, m_fScrollBarRange_top,
                    (float)1, (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        RenderColor((float)m_iPos_x + m_iWidth, m_fScrollBarRange_top, (float)1,
                    (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        EndRenderColor();

        if (GetLineNum() >= m_iNumRenderLine)
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, m_fScrollBarHeight, 0.0f, 1.0f / 32.0f,
                         11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarPos_y + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1, 0.0f,
                         2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
        else
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, m_fScrollBarRange_bottom - m_fScrollBarRange_top,
                         0.0f, 1.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarRange_top + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1,
                         0.0f, 2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
    }
}

BOOL CUIChatPalListBox::RenderDataLine(int iLineNumber)
{
    EnableAlphaTest();

    if (SLGetSelectLineNum() == m_iCurrentRenderEndLine + iLineNumber + 1)
    {
        if (g_dwKeyFocusUIID == GetUIID())
            glColor4f(1.0f, 1.0f, 1.0f, 0.7f);
        else
            glColor4f(1.0f, 1.0f, 1.0f, 0.3f);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 1,
                    13);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glEnable(GL_TEXTURE_2D);
        g_RenderText.SetTextColor(0, 0, 0, 255);
    }
    else
    {
        g_RenderText.SetTextColor(230, 220, 200, 255);
    }

    g_RenderText.SetBgColor(0);

    int iPos_x = m_iPos_x + 3;
    int iPos_y = GetRenderLinePos_y(iLineNumber);

    wchar_t Text[MAX_TEXT_LENGTH + 1] = {0};
    mu_swprintf(Text, L"%ls", m_TextListIter->m_szID);
    g_RenderText.RenderText(iPos_x + GetColumnPos_x(0), iPos_y, Text);

    if (m_iLayoutType == 1)
    {
        if (m_TextListIter->m_Server == 0xFF)
        {
            mu_swprintf(Text, I18N::Game::Offline1039);
        }
        else if (m_TextListIter->m_Server == 0xFE)
        {
            mu_swprintf(Text, I18N::Game::Offline1039);
        }
        else if (m_TextListIter->m_Server == 0xFD)
        {
            mu_swprintf(Text, I18N::Game::CannotUse);
        }
        else if (m_TextListIter->m_Server == 0xFC)
        {
            mu_swprintf(Text, I18N::Game::Offline1039);
        }
        //		else if (m_TextListIter->m_Server == 0xFB)
        //		{
        //			mu_swprintf(Text,I18N::Game::Waiting);
        //		}
        else
        {
            mu_swprintf(Text, I18N::Game::_2dServer, m_TextListIter->m_Server + 1);
        }
        g_RenderText.RenderText(iPos_x + 4 + GetColumnPos_x(1), iPos_y, Text);
    }
    DisableAlphaBlend();

    return TRUE;
}

void CUIWindowListBox::RenderInterface()
{
    ComputeScrollBar();

    //if (GetLineNum() >= m_iNumRenderLine)
    {
        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - m_iHeight - 1, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 13.0f / 16.0f,
                         29.0f / 32.0f, -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 0.0f, 3.0f / 32.0f,
                         13.0f / 16.0f, 13.0f / 32.0f);

        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - 12, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 13.0f / 16.0f, 16.0f / 32.0f,
                         -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 0.0f, 16.0f / 32.0f, 13.0f / 16.0f,
                         13.0f / 32.0f);

        SetLineColor(2);
        RenderColor((float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 1, m_fScrollBarRange_top,
                    (float)1, (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        RenderColor((float)m_iPos_x + m_iWidth, m_fScrollBarRange_top, (float)1,
                    (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        EndRenderColor();

        if (GetLineNum() >= m_iNumRenderLine)
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, m_fScrollBarHeight, 0.0f, 1.0f / 32.0f,
                         11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarPos_y + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1, 0.0f,
                         2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
        else
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, m_fScrollBarRange_bottom - m_fScrollBarRange_top,
                         0.0f, 1.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarRange_top + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1,
                         0.0f, 2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
    }
}

BOOL CUIWindowListBox::RenderDataLine(int iLineNumber)
{
    EnableAlphaTest();

    if (SLGetSelectLineNum() == m_iCurrentRenderEndLine + iLineNumber + 1)
    {
        if (g_dwKeyFocusUIID == GetUIID())
            glColor4f(1.0f, 1.0f, 1.0f, 0.7f);
        else
            glColor4f(1.0f, 1.0f, 1.0f, 0.3f);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 1,
                    13);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glEnable(GL_TEXTURE_2D);
        g_RenderText.SetTextColor(0, 0, 0, 255);
    }
    else
    {
        g_RenderText.SetTextColor(230, 220, 200, 255);
    }

    g_RenderText.SetBgColor(0);

    int iPos_x = m_iPos_x + 8;
    int iPos_y = GetRenderLinePos_y(iLineNumber);

    wchar_t Text[MAX_TEXT_LENGTH + 1] = {0};
    mu_swprintf(Text, L"%ls", m_TextListIter->m_szTitle);
    g_RenderText.RenderText(iPos_x, iPos_y, Text);

    DisableAlphaBlend();

    return TRUE;
}

void SessionUiUnit::RenderCheckBox(int iPos_x, int iPos_y, BOOL bFlag)
{
    DisableAlphaBlend();
    SetLineColor(0);
    RenderColor(iPos_x, iPos_y, 9, 1);
    RenderColor(iPos_x, iPos_y + 8, 9, 1);
    RenderColor(iPos_x, iPos_y, 1, 9);
    RenderColor(iPos_x + 8, iPos_y, 1, 9);
    EndRenderColor();
    EnableAlphaTest();
    if (bFlag == TRUE)
        RenderBitmap(BITMAP_INTERFACE_EX + 13, iPos_x + 2, iPos_y + 2, 5.0f, 5.0f, 0.f, 0.f,
                     5.f / 8.f, 5.f / 8.f);
}

void SessionLegacyCalls::RenderCheckBox(int x, int y, BOOL selected)
{
    sessionKeeper_.Ui()->RenderCheckBox(x, y, selected);
}

void CUILetterListBox::RenderInterface()
{
    ComputeScrollBar();

    //if (GetLineNum() >= m_iNumRenderLine)
    {
        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - m_iHeight - 1, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 13.0f / 16.0f,
                         29.0f / 32.0f, -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 0.0f, 3.0f / 32.0f,
                         13.0f / 16.0f, 13.0f / 32.0f);

        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - 12, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 13.0f / 16.0f, 16.0f / 32.0f,
                         -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 0.0f, 16.0f / 32.0f, 13.0f / 16.0f,
                         13.0f / 32.0f);

        SetLineColor(2);
        RenderColor((float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 1, m_fScrollBarRange_top,
                    (float)1, (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        RenderColor((float)m_iPos_x + m_iWidth, m_fScrollBarRange_top, (float)1,
                    (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        EndRenderColor();

        if (GetLineNum() >= m_iNumRenderLine)
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, m_fScrollBarHeight, 0.0f, 1.0f / 32.0f,
                         11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarPos_y + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1, 0.0f,
                         2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
        else
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, m_fScrollBarRange_bottom - m_fScrollBarRange_top,
                         0.0f, 1.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarRange_top + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1,
                         0.0f, 2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
    }
}

BOOL CUILetterListBox::RenderDataLine(int iLineNumber)
{
    EnableAlphaTest();

    if (SLGetSelectLineNum() == m_iCurrentRenderEndLine + iLineNumber + 1)
    {
        if (g_dwKeyFocusUIID == GetUIID())
            glColor4f(1.0f, 1.0f, 1.0f, 0.7f);
        else
            glColor4f(1.0f, 1.0f, 1.0f, 0.3f);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 1,
                    13);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glEnable(GL_TEXTURE_2D);
        g_RenderText.SetTextColor(0, 0, 0, 255);
    }
    else
    {
        g_RenderText.SetTextColor(230, 220, 200, 255);
    }

    g_RenderText.SetBgColor(0);

    int iPos_x = m_iPos_x;
    int iPos_y = GetRenderLinePos_y(iLineNumber);

    wchar_t Text[MAX_TEXT_LENGTH + 1] = {0};
    RenderCheckBox(iPos_x + 1, iPos_y - 1, m_TextListIter->m_bIsSelected);
    if (m_TextListIter->m_bIsRead == TRUE)
        RenderBitmap(BITMAP_INTERFACE_EX + 14, iPos_x + 1 + 10, iPos_y - 2, 13.0f, 10.0f, 0.f,
                     9.f / 32.f, 13.f / 16.f, 10.f / 32.f);
    else
        RenderBitmap(BITMAP_INTERFACE_EX + 14, iPos_x + 1 + 10, iPos_y - 1, 13.0f, 9.0f, 0.f, 0.f,
                     13.f / 16.f, 9.f / 32.f);

    wcsncpy(Text, m_TextListIter->m_szID, MAX_TEXT_LENGTH);
    g_RenderText.RenderText(iPos_x + 4 + GetColumnPos_x(1), iPos_y, Text, GetColumnWidth(1) - 4, 0,
                            RT3_SORT_LEFT_CLIP);
    wcsncpy(Text, m_TextListIter->m_szDate, MAX_TEXT_LENGTH);
    g_RenderText.RenderText(iPos_x + GetColumnPos_x(2), iPos_y, Text, GetColumnWidth(2), 0,
                            RT3_SORT_CENTER);
    int iMaxWidth = m_iWidth - m_fScrollBarWidth - GetColumnPos_x(3) - 4;
    g_RenderText.RenderText(iPos_x + 4 + GetColumnPos_x(3), iPos_y, m_TextListIter->m_szText,
                            iMaxWidth, 0, RT3_SORT_LEFT_CLIP);
    DisableAlphaBlend();

    return TRUE;
}

void CUILetterTextListBox::Render()
{
    RenderInterface();
    MoveRenderLine();

    glColor3f(1.f, 1.f, 1.f);
    g_RenderText.SetFont(LegacyFontRole::Normal);

    for (int i = 0; i < m_iNumRenderLine; ++i)
    {
        if (m_TextListIter == m_RenderTextList.end())
            break;
        BOOL bResult = RenderDataLine(i);
        if (bResult < 0)
        {
            i -= bResult;
        }
        else if (bResult == FALSE)
        {
            --i;
        }
        ++m_TextListIter;
    }

    RenderCoveredInterface();
}

void CUILetterTextListBox::RenderInterface()
{
    ComputeScrollBar();

    {
        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - m_iHeight - 1, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 13.0f / 16.0f,
                         29.0f / 32.0f, -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 0.0f, 3.0f / 32.0f,
                         13.0f / 16.0f, 13.0f / 32.0f);

        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - 12, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 13.0f / 16.0f, 16.0f / 32.0f,
                         -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 0.0f, 16.0f / 32.0f, 13.0f / 16.0f,
                         13.0f / 32.0f);

        SetLineColor(2);
        RenderColor((float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 1, m_fScrollBarRange_top,
                    (float)1, (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        RenderColor((float)m_iPos_x + m_iWidth, m_fScrollBarRange_top, (float)1,
                    (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        EndRenderColor();

        if (GetLineNum() >= m_iNumRenderLine)
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, m_fScrollBarHeight, 0.0f, 1.0f / 32.0f,
                         11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarPos_y + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1, 0.0f,
                         2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
        else
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, m_fScrollBarRange_bottom - m_fScrollBarRange_top,
                         0.0f, 1.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarRange_top + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1,
                         0.0f, 2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
    }
}

BOOL CUILetterTextListBox::RenderDataLine(int iLineNumber)
{
    EnableAlphaTest();
    wchar_t Text[MAX_TEXT_LENGTH + 1] = {0};
    // 내용
    g_RenderText.SetTextColor(230, 220, 200, 255);
    g_RenderText.SetBgColor(0, 0, 0, 0);

    mu_swprintf(Text, L"%ls", m_TextListIter->m_szText);
    int iPos_x = m_iPos_x + 10;
    int iPos_y = GetRenderLinePos_y(iLineNumber);
    g_RenderText.RenderText(iPos_x, iPos_y, Text);

    DisableAlphaBlend();

    return TRUE;
}

void CUISocketListBox::RenderInterface()
{
    EnableAlphaTest();
    SetLineColor(7, 0.4f);
    RenderColor(m_iPos_x - 1, m_iPos_y - m_iHeight - 1, m_iWidth + 1, m_iHeight + 2);
    EndRenderColor();

    if (GetState() != UISTATE_SCROLL)
        ComputeScrollBar();

    g_pGuardWindow->RenderScrollBarFrame(m_iPos_x + m_iWidth - 8, m_fScrollBarRange_top,
                                         m_fScrollBarRange_bottom - m_fScrollBarRange_top);
    g_pGuardWindow->RenderScrollBar(m_iPos_x + m_iWidth - 12, m_fScrollBarPos_y,
                                    (GetState() == UISTATE_SCROLL && MouseLButtonPush));
}

BOOL CUISocketListBox::RenderDataLine(int iLineNumber)
{
    EnableAlphaTest();

    if (SLGetSelectLineNum() == m_iCurrentRenderEndLine + iLineNumber + 1)
    {
        if (g_dwKeyFocusUIID == GetUIID())
            glColor4f(1.0f, 1.0f, 1.0f, 0.7f);
        else
            glColor4f(1.0f, 1.0f, 1.0f, 0.3f);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 1,
                    13);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glEnable(GL_TEXTURE_2D);
        g_RenderText.SetTextColor(0, 0, 0, 255);
    }
    else
    {
        g_RenderText.SetTextColor(230, 220, 200, 255);
    }

    g_RenderText.SetBgColor(0);

    int iPos_x = m_iPos_x + 8;
    int iPos_y = GetRenderLinePos_y(iLineNumber);

    wchar_t Text[MAX_TEXT_LENGTH + 1] = {0};
    mu_swprintf(Text, L"%ls", m_TextListIter->m_szText);
    g_RenderText.RenderText(iPos_x, iPos_y, Text);

    DisableAlphaBlend();

    return TRUE;
}

// Symmetric counterpart to GiveFocus(): drops keyboard focus from the focused
// portable text field without hiding or destroying it. GiveFocus() sets both
// s_pFocusedPortable and g_dwKeyFocusUIID, so release both here (clearing the
// key-focus id only while it still points at this field, to avoid stomping
// another widget), letting the field hand focus back to the game window while
// staying visible.

void CUITextInputBox::Render()
{
    if (m_iState == UISTATE_HIDE)
        return;
    RenderPortable();
}

void CUITextInputBox::RenderPortableScrollbar(int iTotalLines, int iVisibleLines)
{
    const float fLineNum = (iTotalLines > 0) ? static_cast<float>(iTotalLines) : 1.0f;
    const float fLineRate = static_cast<float>(iVisibleLines) / fLineNum;
    const float fPosRate = static_cast<float>(m_iScrollLine) / fLineNum;

    m_fScrollBarWidth = 13;
    m_fScrollBarRange_top = m_iPos_y + 9;
    m_fScrollBarRange_bottom = m_iPos_y + m_iHeight - 9;
    m_fScrollBarHeight =
        (m_fScrollBarRange_bottom - m_fScrollBarRange_top) * (fLineRate > 1 ? 1 : fLineRate);
    m_fScrollBarPos_y = m_fScrollBarRange_top + (m_fScrollBarRange_bottom - m_fScrollBarRange_top) *
                                                    (fPosRate > 1 ? 1 : fPosRate);

    if (MouseLButtonPush &&
        CheckMouseIn(m_iPos_x + m_iWidth - m_fScrollBarWidth, m_iPos_y - 4, 13.0f, 13.0f) == TRUE)
        RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - m_fScrollBarWidth,
                     (float)m_iPos_y - 4, 13.0f, 13.0f, 13.0f / 16.0f, 29.0f / 32.0f,
                     -13.0f / 16.0f, -13.0f / 32.0f);
    else
        RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - m_fScrollBarWidth,
                     (float)m_iPos_y - 4, 13.0f, 13.0f, 0.0f, 3.0f / 32.0f, 13.0f / 16.0f,
                     13.0f / 32.0f); // up

    if (MouseLButtonPush && CheckMouseIn(m_iPos_x + m_iWidth - m_fScrollBarWidth,
                                         m_iPos_y + m_iHeight - 9, 13.0f, 13.0f) == TRUE)
        RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - m_fScrollBarWidth,
                     (float)m_iPos_y + m_iHeight - 9, 13.0f, 13.0f, 13.0f / 16.0f, 16.0f / 32.0f,
                     -13.0f / 16.0f, -13.0f / 32.0f);
    else
        RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - m_fScrollBarWidth,
                     (float)m_iPos_y + m_iHeight - 9, 13.0f, 13.0f, 0.0f, 16.0f / 32.0f,
                     13.0f / 16.0f, 13.0f / 32.0f); // down

    EnableAlphaTest();
    SetLineColor(2);
    RenderColor((float)m_iPos_x + m_iWidth - m_fScrollBarWidth, m_fScrollBarRange_top, 1.0f,
                (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
    RenderColor((float)m_iPos_x + m_iWidth - 1, m_fScrollBarRange_top, 1.0f,
                (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
    EndRenderColor();
    DisableAlphaBlend();

    RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 1,
                 m_fScrollBarPos_y, m_fScrollBarWidth - 1, m_fScrollBarHeight, 0.0f, 1.0f / 32.0f,
                 11.0f / 16.0f, 1.0f / 32.0f);
    RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 1,
                 m_fScrollBarPos_y, m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f,
                 1.0f / 32.0f);
    RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 1,
                 m_fScrollBarPos_y + m_fScrollBarHeight, m_fScrollBarWidth - 2, 1, 0.0f,
                 2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
}
// and wherever a paragraph exceeds the box width (wrapped at the last space, or
// mid-word when a single word is too long). Each span is [start, end) in buffer
// indices; end excludes the wrapped space or newline.

void CUITextInputBox::RenderPortable()
{
    g_RenderText.SetFont(CurrentFont());

    const std::wstring base = BuildDisplay();
    const int iBaseLen = static_cast<int>(base.length());
    // Clamp both ends before the substr splice below: a negative index would
    // wrap to a huge size_t and throw std::out_of_range.
    if (m_iCaret < 0)
        m_iCaret = 0;
    else if (m_iCaret > iBaseLen)
        m_iCaret = iBaseLen;
    if (m_iSelAnchor < 0)
        m_iSelAnchor = 0;
    else if (m_iSelAnchor > iBaseLen)
        m_iSelAnchor = iBaseLen;

    // Splice the IME composition (if any) into the displayed text at the caret.
    // It is not committed to m_portableText; the caret and scroll follow its end,
    // and the preview span is underlined. Password fields don't preview.
    const bool bFocused = (s_pFocusedPortable == this);
    std::wstring display = base;
    int iCaret = m_iCaret;
    int compStart = -1, compEnd = -1;
    if (bFocused && !m_composition.empty() && !m_bPasswordInput)
    {
        display = base.substr(0, m_iCaret) + m_composition + base.substr(m_iCaret);
        compStart = m_iCaret;
        compEnd = m_iCaret + static_cast<int>(m_composition.length());
        iCaret = compEnd;
    }

    int iLineHeight = LineHeightPx();
    if (!m_bUseMultiLine && iLineHeight > m_iHeight)
        iLineHeight = m_iHeight;

    // Background fill (shared by both layouts).
    if (CheckOption(UIOPTION_PAINTBACK))
    {
        EnableAlphaTest();
        glColor4f(0.f, 0.f, 0.f, 1.f);
        RenderColor(m_iPos_x, m_iPos_y, m_iWidth, m_iHeight);
        EndRenderColor();
    }
    else if (GetAlpha(m_dwBackColor) > 0)
    {
        EnableAlphaTest();
        glColor4ub(GetRed(m_dwBackColor), GetGreen(m_dwBackColor), GetBlue(m_dwBackColor),
                   GetAlpha(m_dwBackColor));
        RenderColor(m_iPos_x, m_iPos_y, m_iWidth, m_iHeight);
        EndRenderColor();
    }

    if (m_bUseMultiLine)
        RenderPortableMultiline(display, iCaret, iLineHeight, compStart, compEnd);
    else
        RenderPortableSingleLine(display, iCaret, iLineHeight, compStart, compEnd);
}

void CUITextInputBox::RenderPortableSingleLine(const std::wstring &display, int iCaret,
                                               int iLineHeight, int compStart, int compEnd)
{
    const int iLength = static_cast<int>(display.length());
    if (iCaret < 0)
        iCaret = 0;
    else if (iCaret > iLength)
        iCaret = iLength;

    // Horizontal scroll: never start past the caret, advance until it fits, then
    // recede left so deleting / moving left brings hidden text back into view.
    if (m_iFirstVisible > iCaret)
        m_iFirstVisible = iCaret;
    if (m_iFirstVisible < 0)
        m_iFirstVisible = 0;
    while (m_iFirstVisible < iCaret &&
           MeasureWidth(display.c_str() + m_iFirstVisible, iCaret - m_iFirstVisible) > m_iWidth)
    {
        ++m_iFirstVisible;
    }
    while (m_iFirstVisible > 0 && MeasureWidth(display.c_str() + m_iFirstVisible - 1,
                                               iLength - (m_iFirstVisible - 1)) <= m_iWidth)
    {
        --m_iFirstVisible;
    }

    // Selection highlight (suppressed while an IME composition is active).
    if (compStart < 0 && HasSelection())
    {
        const int iSelStart = SelectionStart();
        const int iSelEnd = SelectionEnd();
        if (iSelEnd > m_iFirstVisible)
        {
            const int iVisStart = (iSelStart > m_iFirstVisible) ? iSelStart : m_iFirstVisible;
            int x0 = MeasureWidth(display.c_str() + m_iFirstVisible, iVisStart - m_iFirstVisible);
            int x1 = MeasureWidth(display.c_str() + m_iFirstVisible, iSelEnd - m_iFirstVisible);
            if (x0 < 0)
                x0 = 0;
            if (x1 > m_iWidth)
                x1 = m_iWidth;
            if (x1 > x0)
            {
                EnableAlphaTest();
                glColor4ub(GetRed(m_dwSelectBackColor), GetGreen(m_dwSelectBackColor),
                           GetBlue(m_dwSelectBackColor), GetAlpha(m_dwSelectBackColor));
                RenderColor(m_iPos_x + x0, m_iPos_y, x1 - x0, iLineHeight);
                EndRenderColor();
            }
        }
    }

    // Visible text.
    const int iVisibleLen = iLength - m_iFirstVisible;
    if (iVisibleLen > 0)
    {
        g_RenderText.SetBgColor(0);
        g_RenderText.SetTextColor(m_dwTextColor);
        g_RenderText.RenderText(m_iPos_x, m_iPos_y, display.c_str() + m_iFirstVisible, m_iWidth,
                                m_iHeight, RT3_SORT_LEFT);
    }

    // IME composition underline.
    if (compStart >= 0)
    {
        const int a = (compStart > m_iFirstVisible) ? compStart : m_iFirstVisible;
        int x0 = MeasureWidth(display.c_str() + m_iFirstVisible, a - m_iFirstVisible);
        int x1 = MeasureWidth(display.c_str() + m_iFirstVisible, compEnd - m_iFirstVisible);
        if (x1 > m_iWidth)
            x1 = m_iWidth;
        if (x1 > x0)
        {
            EnableAlphaTest();
            glColor4ub(GetRed(m_dwTextColor), GetGreen(m_dwTextColor), GetBlue(m_dwTextColor), 255);
            RenderColor(m_iPos_x + x0, m_iPos_y + iLineHeight - 1, x1 - x0, 1);
            EndRenderColor();
        }
    }

    // Caret: store its rect for IME positioning, draw the bar on the blink.
    int iCaretX = MeasureWidth(display.c_str() + m_iFirstVisible, iCaret - m_iFirstVisible);
    if (iCaretX > m_iWidth)
        iCaretX = m_iWidth;
    m_iCaretAreaX = m_iPos_x + iCaretX;
    m_iCaretAreaY = m_iPos_y;
    m_iCaretAreaH = iLineHeight;

    const bool bFocused = (s_pFocusedPortable == this);
    const bool bBlinkOn =
        (static_cast<int>(m_caretTimer.GetTimeElapsed()) / LegacyControlDetail::CARET_BLINK_MS) %
            2 ==
        0;
    if (bFocused && bBlinkOn)
    {
        EnableAlphaTest();
        glColor4ub(GetRed(m_dwTextColor), GetGreen(m_dwTextColor), GetBlue(m_dwTextColor), 255);
        RenderColor(m_iPos_x + iCaretX, m_iPos_y, LegacyControlDetail::CARET_WIDTH_PX, iLineHeight);
        EndRenderColor();
    }
}

void CUITextInputBox::RenderPortableMultiline(const std::wstring &display, int iCaret,
                                              int iLineHeight, int compStart, int compEnd)
{
    std::vector<PortableLine> lines;
    LayoutLines(display, lines);

    const int iTotalLines = static_cast<int>(lines.size());
    const int iVisibleLines = VisibleLineCount(iLineHeight);
    m_iNumLines = iVisibleLines;

    // Vertical scroll: keep the caret line within the visible window.
    const int iCaretLine = CaretToLine(lines, iCaret);
    if (iCaretLine < m_iScrollLine)
        m_iScrollLine = iCaretLine;
    if (iCaretLine >= m_iScrollLine + iVisibleLines)
        m_iScrollLine = iCaretLine - iVisibleLines + 1;
    const int iMaxScroll = (iTotalLines > iVisibleLines) ? (iTotalLines - iVisibleLines) : 0;
    if (m_iScrollLine > iMaxScroll)
        m_iScrollLine = iMaxScroll;
    if (m_iScrollLine < 0)
        m_iScrollLine = 0;

    const bool bFocused = (s_pFocusedPortable == this);
    const bool bBlinkOn =
        (static_cast<int>(m_caretTimer.GetTimeElapsed()) / LegacyControlDetail::CARET_BLINK_MS) %
            2 ==
        0;
    const bool bSelection = (compStart < 0) && HasSelection();
    const int iSelStart = SelectionStart();
    const int iSelEnd = SelectionEnd();

    g_RenderText.SetBgColor(0);

    const int iLast = m_iScrollLine + iVisibleLines;
    for (int li = m_iScrollLine; li < iLast && li < iTotalLines; ++li)
    {
        const PortableLine &line = lines[li];
        const int y = m_iPos_y + (li - m_iScrollLine) * iLineHeight;
        const wchar_t *lineText = display.c_str() + line.start;
        const int lineLen = line.end - line.start;

        // Selection highlight for the part of this line inside the selection.
        if (bSelection && iSelEnd > line.start && iSelStart <= line.end)
        {
            const int a = (iSelStart > line.start) ? iSelStart : line.start;
            const int b = (iSelEnd < line.end) ? iSelEnd : line.end;
            if (b >= a)
            {
                int x0 = MeasureWidth(lineText, a - line.start);
                int x1 = MeasureWidth(lineText, b - line.start);
                if (x1 > m_iWidth)
                    x1 = m_iWidth;
                if (x1 > x0)
                {
                    EnableAlphaTest();
                    glColor4ub(GetRed(m_dwSelectBackColor), GetGreen(m_dwSelectBackColor),
                               GetBlue(m_dwSelectBackColor), GetAlpha(m_dwSelectBackColor));
                    RenderColor(m_iPos_x + x0, y, x1 - x0, iLineHeight);
                    EndRenderColor();
                }
            }
        }

        if (lineLen > 0)
        {
            const std::wstring lineStr(lineText, lineLen);
            g_RenderText.SetTextColor(m_dwTextColor);
            g_RenderText.RenderText(m_iPos_x, y, lineStr.c_str(), m_iWidth, iLineHeight,
                                    RT3_SORT_LEFT);
        }

        // IME composition underline for the part of the preview on this line.
        if (compStart >= 0 && compEnd > line.start && compStart < line.end)
        {
            const int a = (compStart > line.start) ? compStart : line.start;
            const int b = (compEnd < line.end) ? compEnd : line.end;
            int x0 = MeasureWidth(lineText, a - line.start);
            int x1 = MeasureWidth(lineText, b - line.start);
            if (x1 > m_iWidth)
                x1 = m_iWidth;
            if (x1 > x0)
            {
                EnableAlphaTest();
                glColor4ub(GetRed(m_dwTextColor), GetGreen(m_dwTextColor), GetBlue(m_dwTextColor),
                           255);
                RenderColor(m_iPos_x + x0, y + iLineHeight - 1, x1 - x0, 1);
                EndRenderColor();
            }
        }

        // Caret on this line: store its rect for IME positioning, draw on blink.
        if (iCaretLine == li && iCaret >= line.start)
        {
            int iCaretX = MeasureWidth(lineText, iCaret - line.start);
            if (iCaretX > m_iWidth)
                iCaretX = m_iWidth;
            m_iCaretAreaX = m_iPos_x + iCaretX;
            m_iCaretAreaY = y;
            m_iCaretAreaH = iLineHeight;
            if (bFocused && bBlinkOn)
            {
                EnableAlphaTest();
                glColor4ub(GetRed(m_dwTextColor), GetGreen(m_dwTextColor), GetBlue(m_dwTextColor),
                           255);
                RenderColor(m_iPos_x + iCaretX, y, LegacyControlDetail::CARET_WIDTH_PX,
                            iLineHeight);
                EndRenderColor();
            }
        }
    }

    if (iTotalLines > iVisibleLines)
        RenderPortableScrollbar(iTotalLines, iVisibleLines);
}

void CUIChatInputBox::Render()
{
    m_TextInputBox.Render();
    m_BuddyInputBox.Render();
}

void CUIGuildNoticeListBox::RenderInterface()
{
    return;
    EnableAlphaTest();
    SetLineColor(7, 0.4f);
    RenderColor(m_iPos_x - 1, m_iPos_y - m_iHeight - 1, m_iWidth + 1, m_iHeight + 2);
    EndRenderColor();
    ComputeScrollBar();

    //if (GetLineNum() >= m_iNumRenderLine)
    {
        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - m_iHeight - 1, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 13.0f / 16.0f,
                         29.0f / 32.0f, -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 0.0f, 3.0f / 32.0f,
                         13.0f / 16.0f, 13.0f / 32.0f); // ▲

        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - 12, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 13.0f / 16.0f, 16.0f / 32.0f,
                         -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 0.0f, 16.0f / 32.0f, 13.0f / 16.0f,
                         13.0f / 32.0f); // ▼

        EnableAlphaTest();
        SetLineColor(2);
        RenderColor((float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 1, m_fScrollBarRange_top,
                    (float)1, (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        RenderColor((float)m_iPos_x + m_iWidth, m_fScrollBarRange_top, (float)1,
                    (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        EndRenderColor();

        if (GetLineNum() >= m_iNumRenderLine)
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, m_fScrollBarHeight, 0.0f, 1.0f / 32.0f,
                         11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarPos_y + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1, 0.0f,
                         2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
        else
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, m_fScrollBarRange_bottom - m_fScrollBarRange_top,
                         0.0f, 1.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarRange_top + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1,
                         0.0f, 2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
    }
}

BOOL CUIGuildNoticeListBox::RenderDataLine(int iLineNumber)
{
    EnableAlphaTest();

    if (SLGetSelectLineNum() == m_iCurrentRenderEndLine + iLineNumber + 1)
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 1,
                    13);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        g_RenderText.SetTextColor(0, 0, 0, 255);
    }
    else
    {
        g_RenderText.SetTextColor(230, 220, 200, 255);
    }
    glEnable(GL_TEXTURE_2D);
    g_RenderText.SetBgColor(0);

    int iPos_x = m_iPos_x + 4;
    int iPos_y = GetRenderLinePos_y(iLineNumber);

    g_RenderText.RenderText(iPos_x, iPos_y, m_TextListIter->m_szContent, 0, 0, RT3_SORT_LEFT);

    DisableAlphaBlend();

    return TRUE;
}

void CUINewGuildMemberListBox::RenderInterface()
{
    return;
    EnableAlphaTest();
    SetLineColor(7, 0.4f);
    RenderColor(m_iPos_x - 1, m_iPos_y - m_iHeight - 1, m_iWidth + 1, m_iHeight + 2);
    EndRenderColor();
    ComputeScrollBar();

    //if (GetLineNum() >= m_iNumRenderLine)
    {
        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - m_iHeight - 1, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 13.0f / 16.0f,
                         29.0f / 32.0f, -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 0.0f, 3.0f / 32.0f,
                         13.0f / 16.0f, 13.0f / 32.0f); // ▲

        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - 12, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 13.0f / 16.0f, 16.0f / 32.0f,
                         -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 0.0f, 16.0f / 32.0f, 13.0f / 16.0f,
                         13.0f / 32.0f); // ▼

        EnableAlphaTest();
        SetLineColor(2);
        RenderColor((float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 1, m_fScrollBarRange_top,
                    (float)1, (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        RenderColor((float)m_iPos_x + m_iWidth, m_fScrollBarRange_top, (float)1,
                    (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        EndRenderColor();

        if (GetLineNum() >= m_iNumRenderLine)
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, m_fScrollBarHeight, 0.0f, 1.0f / 32.0f,
                         11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarPos_y + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1, 0.0f,
                         2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
        else
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, m_fScrollBarRange_bottom - m_fScrollBarRange_top,
                         0.0f, 1.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarRange_top + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1,
                         0.0f, 2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
    }

    g_RenderText.RenderText(m_iPos_x + 14, m_iPos_y - m_iHeight - 12, I18N::Game::Name);
    g_RenderText.RenderText(m_iPos_x + 65, m_iPos_y - m_iHeight - 12, I18N::Game::Position);
    g_RenderText.RenderText(m_iPos_x + 106, m_iPos_y - m_iHeight - 12, I18N::Game::Server);
}

BOOL CUINewGuildMemberListBox::RenderDataLine(int iLineNumber)
{
    EnableAlphaTest();

    int iCharacterLevel;
    if (m_TextListIter->m_GuildStatus == 128)
        iCharacterLevel = 0;
    else if (m_TextListIter->m_GuildStatus == 64)
        iCharacterLevel = 1;
    else if (m_TextListIter->m_GuildStatus == 32)
        iCharacterLevel = 2;
    else
        iCharacterLevel = 3;

    if (iCharacterLevel == 0)
    {
        glColor4ub(255, 100, 50, 127);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 1,
                    13);
        glColor4f(255, 255, 255, 255);
    }
    else if (iCharacterLevel == 1)
    {
        glColor4ub(255, 150, 80, 127);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 1,
                    13);
        glColor4f(255, 255, 255, 255);
    }
    else if (iCharacterLevel == 2)
    {
        glColor4ub(255, 200, 100, 127);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 1,
                    13);
        glColor4f(255, 255, 255, 255);
    }

    if (SLGetSelectLineNum() == m_iCurrentRenderEndLine + iLineNumber + 1)
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 1,
                    13);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        g_RenderText.SetTextColor(0, 0, 0, 255);
    }
    else
    {
        g_RenderText.SetTextColor(230, 220, 200, 255);
    }
    glEnable(GL_TEXTURE_2D);
    g_RenderText.SetBgColor(0);

    int iPos_x = m_iPos_x + 8;
    int iPos_y = GetRenderLinePos_y(iLineNumber);

    wchar_t Text[MAX_TEXT_LENGTH + 1] = {0};
    mu_swprintf(Text, L"%ls", m_TextListIter->m_szID);
    g_RenderText.RenderText(iPos_x, iPos_y, Text);

    if (iCharacterLevel == 0)
        g_RenderText.RenderText(iPos_x + 45, iPos_y, I18N::Game::Master, 70, 0, RT3_SORT_CENTER);
    else if (iCharacterLevel == 1)
        g_RenderText.RenderText(iPos_x + 45, iPos_y, I18N::Game::AssistM, 70, 0, RT3_SORT_CENTER);
    else if (iCharacterLevel == 2)
        g_RenderText.RenderText(iPos_x + 45, iPos_y, I18N::Game::BattleM, 70, 0, RT3_SORT_CENTER);

    if (m_TextListIter->m_Server != 255 /* && m_TextListIter->m_Number != 0*/)
    {
        g_RenderText.SetBgColor(0);
        g_RenderText.SetTextColor(255, 196, 0, 255);

        mu_swprintf(Text, L"%d", m_TextListIter->m_Server + 1);
        g_RenderText.RenderText(m_iPos_x + m_iWidth - 30, iPos_y, Text);
    }

    DisableAlphaBlend();

    return TRUE;
}

void CUIUnionGuildListBox::RenderInterface()
{
    return;

    EnableAlphaTest();
    SetLineColor(7, 0.4f);
    RenderColor(m_iPos_x - 1, m_iPos_y - m_iHeight - 1, m_iWidth + 1, m_iHeight + 2);

    EndRenderColor();

    ComputeScrollBar();

    //if (GetLineNum() >= m_iNumRenderLine)
    {
        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - m_iHeight - 1, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 13.0f / 16.0f,
                         29.0f / 32.0f, -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 0.0f, 3.0f / 32.0f,
                         13.0f / 16.0f, 13.0f / 32.0f); // ▲

        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - 12, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 13.0f / 16.0f, 16.0f / 32.0f,
                         -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 0.0f, 16.0f / 32.0f, 13.0f / 16.0f,
                         13.0f / 32.0f); // ▼

        EnableAlphaTest();
        SetLineColor(2);
        RenderColor((float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 1, m_fScrollBarRange_top,
                    (float)1, (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        RenderColor((float)m_iPos_x + m_iWidth, m_fScrollBarRange_top, (float)1,
                    (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        EndRenderColor();

        if (GetLineNum() >= m_iNumRenderLine)
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, m_fScrollBarHeight, 0.0f, 1.0f / 32.0f,
                         11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarPos_y + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1, 0.0f,
                         2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
        else
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, m_fScrollBarRange_bottom - m_fScrollBarRange_top,
                         0.0f, 1.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarRange_top + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1,
                         0.0f, 2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
    }

    g_RenderText.RenderText(m_iPos_x + 15, m_iPos_y - m_iHeight - 12, I18N::Game::NAME);
    g_RenderText.RenderText(m_iPos_x + 113, m_iPos_y - m_iHeight - 12, I18N::Game::Members);
}

BOOL CUIUnionGuildListBox::RenderDataLine(int iLineNumber)
{
    EnableAlphaTest();

    if (SLGetSelectLineNum() == m_iCurrentRenderEndLine + iLineNumber + 1)
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 1,
                    13);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        g_RenderText.SetTextColor(0, 0, 0, 255);
    }
    else
    {
        g_RenderText.SetTextColor(230, 220, 220, 255);
    }
    glEnable(GL_TEXTURE_2D);
    g_RenderText.SetBgColor(0);

    int iPos_x = m_iPos_x + 4;
    int iPos_y = GetRenderLinePos_y(iLineNumber);

    memcpy(GuildMark[MARK_EDIT].Mark, m_TextListIter->GuildMark, sizeof(BYTE) * 64);
    if (CreateGuildMark(MARK_EDIT))
        RenderBitmap(BITMAP_GUILD, (float)iPos_x, (float)iPos_y, 8, 8);
    if (Hero->GuildMarkIndex >= 0)
        memcpy(GuildMark[MARK_EDIT].Mark, GuildMark[Hero->GuildMarkIndex].Mark, sizeof(BYTE) * 64);
    else
        memset(GuildMark[MARK_EDIT].Mark, 0, 64);

    wchar_t Text[MAX_TEXT_LENGTH + 1] = {0};
    mu_swprintf(Text, L"%ls", m_TextListIter->szName);
    g_RenderText.RenderText(iPos_x + 12, iPos_y, Text);

    mu_swprintf(Text, L"%d", m_TextListIter->nMemberCount);
    g_RenderText.RenderText(iPos_x + 138, iPos_y, Text, 0, 0, RT3_WRITE_RIGHT_TO_LEFT);

    DisableAlphaBlend();

    return TRUE;
}

void CUIUnmixgemList::RenderInterface()
{
    EnableAlphaTest();
    SetLineColor(7, 0.4f);
    RenderColor(m_iPos_x - 1, m_iPos_y - m_iHeight - 1, m_iWidth + 1, m_iHeight + 2);

    EndRenderColor();

    ComputeScrollBar();

    //if (GetLineNum() >= m_iNumRenderLine)
    {
        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - m_iHeight - 1, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 13.0f / 16.0f,
                         29.0f / 32.0f, -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - m_iHeight - 1, 13.0f, 13.0f, 0.0f, 3.0f / 32.0f,
                         13.0f / 16.0f, 13.0f / 32.0f);

        if (MouseLButtonPush &&
            CheckMouseIn(m_iPos_x + m_iWidth - 12, m_iPos_y - 12, 13.0f, 13.0f) == TRUE)
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 13.0f / 16.0f, 16.0f / 32.0f,
                         -13.0f / 16.0f, -13.0f / 32.0f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 12, (float)m_iPos_x + m_iWidth - 12,
                         (float)m_iPos_y - 12, 13.0f, 13.0f, 0.0f, 16.0f / 32.0f, 13.0f / 16.0f,
                         13.0f / 32.0f);

        EnableAlphaTest();
        SetLineColor(2);
        RenderColor((float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 1, m_fScrollBarRange_top,
                    (float)1, (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        RenderColor((float)m_iPos_x + m_iWidth, m_fScrollBarRange_top, (float)1,
                    (float)m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        EndRenderColor();

        if (GetLineNum() >= m_iNumRenderLine)
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, m_fScrollBarHeight, 0.0f, 1.0f / 32.0f,
                         11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarPos_y,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarPos_y + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1, 0.0f,
                         2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
        else
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, m_fScrollBarRange_bottom - m_fScrollBarRange_top,
                         0.0f, 1.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2, m_fScrollBarRange_top,
                         m_fScrollBarWidth - 2, 1, 0.0f, 0.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 12,
                         (float)m_iPos_x + m_iWidth - m_fScrollBarWidth + 2,
                         m_fScrollBarRange_top + m_fScrollBarHeight - 1, m_fScrollBarWidth - 2, 1,
                         0.0f, 2.0f / 32.0f, 11.0f / 16.0f, 1.0f / 32.0f);
        }
    }
}

BOOL CUIUnmixgemList::RenderDataLine(int iLineNumber)
{
    EnableAlphaTest();

    if (SLGetSelectLineNum() == m_iCurrentRenderEndLine + iLineNumber + 1)
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 1,
                    13);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        g_RenderText.SetTextColor(0, 0, 0, 255);
    }
    else
    {
        g_RenderText.SetTextColor(230, 220, 200, 255);
    }
    glEnable(GL_TEXTURE_2D);
    g_RenderText.SetBgColor(0);
    int iPos_x = m_iPos_x + 4;
    int iPos_y = GetRenderLinePos_y(iLineNumber);

    wchar_t oText[MAX_GLOBAL_TEXT_STRING] = {
        0,
    };

    const ITEM *pItem = FindInventoryItemBySlot(m_TextListIter->m_iInvenIdx);
    if (pItem)
    {
        int nIdx = Check_Jewel(pItem->Type);
        mu_swprintf(oText, L"%ls,  %d", I18N::Game::Lookup(GetJewelIndex(nIdx, COMGEM::eGEM_NAME)),
                    (m_TextListIter->m_cLevel + 1) * 10);
    }

    g_RenderText.RenderText(iPos_x + 2, iPos_y, oText);

    DisableAlphaBlend();
    return TRUE;
}

void CUIBCDeclareGuildListBox::RenderInterface()
{
    EnableAlphaTest();
    SetLineColor(7, 0.4f);
    RenderColor(m_iPos_x - 1, m_iPos_y - m_iHeight - 1, m_iWidth + 1, m_iHeight + 2);
    EndRenderColor();

    if (GetState() != UISTATE_SCROLL)
        ComputeScrollBar();

    g_pGuardWindow->RenderScrollBarFrame(m_iPos_x + m_iWidth - 8, m_fScrollBarRange_top,
                                         m_fScrollBarRange_bottom - m_fScrollBarRange_top);
    g_pGuardWindow->RenderScrollBar(m_iPos_x + m_iWidth - 12, m_fScrollBarPos_y,
                                    (GetState() == UISTATE_SCROLL && MouseLButtonPush));
    g_RenderText.RenderText(m_iPos_x + 5, m_iPos_y - m_iHeight - 12, I18N::Game::NAME);
    g_RenderText.RenderText(m_iPos_x + 50, m_iPos_y - m_iHeight - 12, I18N::Game::NoReg);
    g_RenderText.RenderText(m_iPos_x + 98, m_iPos_y - m_iHeight - 12, I18N::Game::Stat);
    g_RenderText.RenderText(m_iPos_x + 123, m_iPos_y - m_iHeight - 12, I18N::Game::Order);
}

BOOL CUIBCDeclareGuildListBox::RenderDataLine(int iLineNumber)
{
    wchar_t Text[MAX_TEXT_LENGTH + 1] = {0};

    mu_swprintf(Text, L"%ls", m_TextListIter->szName);

    if ((wcscmp(GuildMark[Hero->GuildMarkIndex].UnionName, Text) != 0 &&
         wcscmp(GuildMark[Hero->GuildMarkIndex].GuildName, Text) != 0))
    {
        return FALSE;
    }

    EnableAlphaTest();

    if (SLGetSelectLineNum() == m_iCurrentRenderEndLine + iLineNumber + 1)
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 1,
                    13);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        g_RenderText.SetTextColor(0, 0, 0, 255);
    }
    else
    {
        g_RenderText.SetTextColor(230, 220, 200, 255);
    }
    glEnable(GL_TEXTURE_2D);
    g_RenderText.SetBgColor(0);

    int iPos_x = m_iPos_x + 4;
    int iPos_y = GetRenderLinePos_y(iLineNumber);

    g_RenderText.RenderText(iPos_x + 2, iPos_y, Text);

    mu_swprintf(Text, L"%d", m_TextListIter->nCount);
    g_RenderText.RenderText(iPos_x + 70, iPos_y, Text, 0, 0, RT3_WRITE_RIGHT_TO_LEFT);

    if (m_TextListIter->byIsGiveUp)
        mu_swprintf(Text, L"%ls", I18N::Game::Failed);
    else
        mu_swprintf(Text, L"%ls", I18N::Game::Processing);
    g_RenderText.RenderText(iPos_x + 120, iPos_y, Text, 0, 0, RT3_WRITE_RIGHT_TO_LEFT);

    mu_swprintf(Text, L"%u", m_TextListIter->bySeqNum);
    g_RenderText.RenderText(iPos_x + 140, iPos_y, Text, 0, 0, RT3_WRITE_RIGHT_TO_LEFT);

    DisableAlphaBlend();

    return TRUE;
}

void CUIBCGuildListBox::RenderInterface()
{
    EnableAlphaTest();
    SetLineColor(7, 0.4f);
    RenderColor(m_iPos_x - 1, m_iPos_y - m_iHeight - 1, m_iWidth + 1, m_iHeight + 2);
    SetLineColor(0, 0.4f);
    RenderColor(m_iPos_x - 1, m_iPos_y + 25 - 1, m_iWidth - 100 + 1, 20);

    SetLineColor(7, 0.4f);
    RenderColor(m_iPos_x - 1 + m_iWidth - 100 + 1, m_iPos_y + 25 - 1, m_iWidth - 60, 20);
    EndRenderColor();

    if (GetState() != UISTATE_SCROLL)
        ComputeScrollBar();

    g_pGuardWindow->RenderScrollBarFrame(m_iPos_x + m_iWidth - 8, m_fScrollBarRange_top,
                                         m_fScrollBarRange_bottom - m_fScrollBarRange_top);
    g_pGuardWindow->RenderScrollBar(m_iPos_x + m_iWidth - 12, m_fScrollBarPos_y,
                                    (GetState() == UISTATE_SCROLL && MouseLButtonPush));
    g_RenderText.RenderText(m_iPos_x + 5, m_iPos_y - m_iHeight - 12, I18N::Game::NAME);
    g_RenderText.RenderText(m_iPos_x + 80, m_iPos_y - m_iHeight - 12, I18N::Game::Camp);
    g_RenderText.RenderText(m_iPos_x + 120, m_iPos_y - m_iHeight - 12, I18N::Game::Maintain);
    g_RenderText.RenderText(m_iPos_x + 18, m_iPos_y + 31 - 1, I18N::Game::Score);
}

BOOL CUIBCGuildListBox::RenderDataLine(int iLineNumber)
{
    EnableAlphaTest();

    if (SLGetSelectLineNum() == m_iCurrentRenderEndLine + iLineNumber + 1)
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 1,
                    13);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        g_RenderText.SetTextColor(0, 0, 0, 255);
    }
    else if (m_TextListIter->byJoinSide == 1)
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.2f);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 1,
                    13);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        g_RenderText.SetTextColor(230, 220, 200, 255);
    }
    else
    {
        g_RenderText.SetTextColor(230, 220, 200, 255);
    }
    glEnable(GL_TEXTURE_2D);
    g_RenderText.SetBgColor(0);

    int iPos_x = m_iPos_x + 4;
    int iPos_y = GetRenderLinePos_y(iLineNumber);

    wchar_t Text[MAX_TEXT_LENGTH + 1] = {0};
    mu_swprintf(Text, L"%ls", m_TextListIter->szName);
    g_RenderText.RenderText(iPos_x + 2, iPos_y, Text);

    if (m_TextListIter->byJoinSide == 1)
        mu_swprintf(Text, L"%ls", I18N::Game::DefendingTeam);
    else
        mu_swprintf(Text, L"%ls", I18N::Game::InvadingTeam);
    g_RenderText.RenderText(iPos_x + 100, iPos_y, Text, 0, 0, RT3_WRITE_RIGHT_TO_LEFT);

    if (m_TextListIter->byGuildInvolved == 1)
        mu_swprintf(Text, L"%ls", I18N::Game::Maintain);
    else
        mu_swprintf(Text, L"%ls", I18N::Game::Assist);

    g_RenderText.RenderText(iPos_x + 137, iPos_y, Text, 0, 0, RT3_WRITE_RIGHT_TO_LEFT);

    g_RenderText.SetTextColor(230, 220, 200, 255);
    wchar_t Dummy[300];
    if (Select_Guild == iLineNumber)
    {
        if (m_TextListIter->byJoinSide == 1)
            mu_swprintf(Dummy, L"--");
        else
            mu_swprintf(Dummy, L"%ls :     %d", m_TextListIter->szName,
                        m_TextListIter->iGuildScore);
        g_RenderText.RenderText(m_iPos_x + 60, m_iPos_y + 31 - 1, Dummy);
    }

    DisableAlphaBlend();

    return TRUE;
}

void SessionUiUnit::RenderGoldRect(float fPos_x, float fPos_y, float fWidth, float fHeight,
                                   int iFillType)
{
    switch (iFillType)
    {
    case 1:
        glColor4ub(146, 144, 141, 200);
        RenderColor(fPos_x, fPos_y, fWidth, fHeight);
        EndRenderColor();
        break;
    default:
        break;
    };

    RenderBitmap(BITMAP_INVENTORY + 19, fPos_x, fPos_y, fWidth, 2, 10 / 256.f, 5 / 16.f,
                 170.f / 256.f, 2.f / 16.f);
    RenderBitmap(BITMAP_INVENTORY + 19, fPos_x, fPos_y + fHeight, fWidth + 1, 2, 10 / 256.f,
                 5 / 16.f, 170.f / 256.f, 2.f / 16.f);
    RenderBitmap(BITMAP_INVENTORY, fPos_x, fPos_y, 2, fHeight, 1.f / 256.f, 5 / 16.f, 2.f / 256.f,
                 125.f / 256.f);
    RenderBitmap(BITMAP_INVENTORY, fPos_x + fWidth, fPos_y, 2, fHeight, 1.f / 256.f, 5 / 16.f,
                 2.f / 256.f, 125.f / 256.f);
}

void SessionLegacyCalls::RenderGoldRect(float x, float y, float width, float height, int fillType)
{
    sessionKeeper_.Ui()->RenderGoldRect(x, y, width, height, fillType);
}

void CUIMoveCommandListBox::RenderInterface()
{
}

BOOL CUIMoveCommandListBox::RenderDataLine(int iLineNumber)
{
    return TRUE;
}

void CUICurQuestListBox::RenderInterface()
{
    if (GetState() != UISTATE_SCROLL)
        ComputeScrollBar();

    if (GetLineNum() <= m_iNumRenderLine)
        return;

    g_pGuardWindow->RenderScrollBarFrame(m_iPos_x + m_iWidth - 8, m_fScrollBarRange_top,
                                         m_fScrollBarRange_bottom - m_fScrollBarRange_top);
    g_pGuardWindow->RenderScrollBar(m_iPos_x + m_iWidth - 12, m_fScrollBarPos_y,
                                    (GetState() == UISTATE_SCROLL && MouseLButtonPush));
}

BOOL CUICurQuestListBox::RenderDataLine(int iLineNumber)
{
    EnableAlphaTest();

    if (SLGetSelectLineNum() == m_iCurrentRenderEndLine + iLineNumber + 1)
    {
        glColor4f(0.5f, 0.7f, 0.3f, 0.5f);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 1,
                    13);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glEnable(GL_TEXTURE_2D);
    }

    g_RenderText.SetTextColor(255, 230, 210, 255);
    g_RenderText.SetBgColor(0);

    int iPos_x = m_iPos_x + 5;
    int iPos_y = GetRenderLinePos_y(iLineNumber);

    wchar_t Text[MAX_TEXT_LENGTH + 1] = {0};
    mu_swprintf(Text, L"%ls", m_TextListIter->m_szText);
    g_RenderText.RenderText(iPos_x, iPos_y, Text);

    return TRUE;
}

void CUIQuestContentsListBox::RenderInterface()
{
    if (GetState() != UISTATE_SCROLL)
        ComputeScrollBar();

    if (GetLineNum() > m_iNumRenderLine)
    {
        g_pGuardWindow->RenderScrollBarFrame(m_iPos_x + m_iWidth - 8, m_fScrollBarRange_top,
                                             m_fScrollBarRange_bottom - m_fScrollBarRange_top);
        g_pGuardWindow->RenderScrollBar(m_iPos_x + m_iWidth - 12, m_fScrollBarPos_y,
                                        (GetState() == UISTATE_SCROLL && MouseLButtonPush));
    }
}

BOOL CUIQuestContentsListBox::RenderDataLine(int iLineNumber)
{
    EnableAlphaTest();

    g_RenderText.SetFont(m_TextListIter->m_fontRole);
    g_RenderText.SetTextColor(m_TextListIter->m_dwColor);
    g_RenderText.SetBgColor(0);

    int iPos_x = m_iPos_x + 5;
    int iPos_y = GetRenderLinePos_y(iLineNumber);

    wchar_t Text[MAX_TEXT_LENGTH + 1] = {0};
    mu_swprintf(Text, L"%ls", m_TextListIter->m_szText);
    g_RenderText.RenderText(iPos_x, iPos_y, Text, GetWidth() - 18, 0, m_TextListIter->m_nSort);

    return TRUE;
}

void CUIQuestContentsListBox::RenderCoveredInterface()
{
    m_TextListIter = SLGetSelectLine();

    if (SLGetSelectLine() == m_TextList.end())
        return;

    // 아이템인가?
    if (QUEST_REQUEST_ITEM == m_TextListIter->m_dwType ||
        QUEST_REWARD_ITEM == m_TextListIter->m_dwType)
    {
        int nX = m_iPos_x + GetWidth() / 2;
        int nY = GetRenderLinePos_y(SLGetSelectLineNum());
        RenderItemInfo(nX, nY, m_TextListIter->m_pItem, false, 0, true);
    }
}

#ifdef PBG_ADD_INGAMESHOP_UI_ITEMSHOP

void CUIInGameShopListBox::RenderInterface()
{
    if (GetState() != UISTATE_SCROLL)
        ComputeScrollBar();
}

BOOL CUIInGameShopListBox::RenderDataLine(int iLineNumber)
{
    EnableAlphaTest();

    if (SLGetSelectLineNum() == m_iCurrentRenderEndLine + iLineNumber + 1)
    {
        glColor4f(0.15f, 0.3f, 0.4f, 0.5f);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 4,
                    13);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glEnable(GL_TEXTURE_2D);
    }

    g_RenderText.SetTextColor(255, 230, 210, 255);
    g_RenderText.SetBgColor(0);

    int iPos_x = m_iPos_x;
    int iPos_y = GetRenderLinePos_y(iLineNumber);

    wchar_t szItemName[MAX_TEXT_LENGTH];
    if (m_TextListIter->m_iNum > 1)
    {
        mu_swprintf(szItemName, L"%ls(%d)", m_TextListIter->m_szName, m_TextListIter->m_iNum);
        g_RenderText.RenderText(iPos_x, iPos_y, szItemName, 98, 0, RT3_SORT_LEFT);
    }
    else
    {
        g_RenderText.RenderText(iPos_x, iPos_y, m_TextListIter->m_szName, 98, 0, RT3_SORT_LEFT);
    }

    g_RenderText.RenderText(iPos_x + 102, iPos_y, m_TextListIter->m_szPeriod, 33, 0,
                            RT3_SORT_RIGHT);

    DisableAlphaBlend();

    return TRUE;
}

void CUIBuyingListBox::RenderInterface()
{
    EnableAlphaTest();
    if (GetState() != UISTATE_SCROLL)
        ComputeScrollBar();

    if (GetLineNum() <= m_iNumRenderLine)
        return;

    g_pGuardWindow->RenderScrollBarFrame(m_iPos_x + m_iWidth - 8, m_fScrollBarRange_top,
                                         m_fScrollBarRange_bottom - m_fScrollBarRange_top);
    g_pGuardWindow->RenderScrollBar(m_iPos_x + m_iWidth - 12, m_fScrollBarPos_y,
                                    (GetState() == UISTATE_SCROLL && MouseLButtonPush));
    DisableAlphaBlend();
}

BOOL CUIBuyingListBox::RenderDataLine(int iLineNumber)
{
    EnableAlphaTest();

    if (SLGetSelectLineNum() == m_iCurrentRenderEndLine + iLineNumber + 1 && GetLineColorRender())
    {
        glColor4f(0.15f, 0.3f, 0.4f, 0.5f);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 1,
                    13);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glEnable(GL_TEXTURE_2D);
    }

    g_RenderText.SetTextColor(255, 230, 210, 255);
    g_RenderText.SetBgColor(0);

    int iPos_x = m_iPos_x + 3;
    int iPos_y = GetRenderLinePos_y(iLineNumber);

    wchar_t Text[MAX_TEXT_LENGTH + 1] = {0};
    if (m_TextListIter->m_pszItemExplanation != nullptr)
    {
        mu_swprintf(Text, L"%ls", m_TextListIter->m_pszItemExplanation);
        g_RenderText.RenderText(iPos_x, iPos_y, Text);
    }
    DisableAlphaBlend();

    return TRUE;
}
void CUIBuyingListBox::SetLineColorRender(const bool _LineColor)
{
    m_bRenderLineColor = _LineColor;
}

void CUIPackCheckBuyingListBox::RenderInterface()
{
    EnableAlphaTest();
    if (GetState() != UISTATE_SCROLL)
        ComputeScrollBar();

    if (GetLineNum() <= m_iNumRenderLine)
        return;

    g_pGuardWindow->RenderScrollBarFrame(m_iPos_x + m_iWidth - 8, m_fScrollBarRange_top,
                                         m_fScrollBarRange_bottom - m_fScrollBarRange_top);
    g_pGuardWindow->RenderScrollBar(m_iPos_x + m_iWidth - 12, m_fScrollBarPos_y,
                                    (GetState() == UISTATE_SCROLL && MouseLButtonPush));
    DisableAlphaBlend();
}
BOOL CUIPackCheckBuyingListBox::RenderDataLine(int nLine)
{
    EnableAlphaTest();

    if (SLGetSelectLineNum() == m_iCurrentRenderEndLine + nLine + 1)
    {
        glColor4f(0.07f, 0.31f, 0.31f, 0.5f);
        RenderColor(m_iPos_x + 3, GetRenderLinePos_y(nLine) + 1, m_iWidth - m_fScrollBarWidth + 1,
                    TEXT_HEIGHTSIZE - 6);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glEnable(GL_TEXTURE_2D);
    }

    g_RenderText.SetTextColor(255, 230, 210, 255);
    g_RenderText.SetBgColor(0);

    int iPos_x = m_iPos_x + 3;
    int iPos_y = GetRenderLinePos_y(nLine);

    wchar_t Text[MAX_TEXT_LENGTH + 1] = {0};

    if (m_TextListIter->m_szItemName != nullptr)
    {
        if (SLGetSelectLineNum() == m_iCurrentRenderEndLine + nLine + 1)
            m_TextListIter->m_RadioBtn.UpdateActionCheck(true);
        else
            m_TextListIter->m_RadioBtn.UpdateActionCheck(false);

        m_TextListIter->m_RadioBtn.SetRadioBtnRect(iPos_x + 1, iPos_y + TEXT_HEIGHTSIZE * 0.2f);
        RenderRadioButton(m_TextListIter->m_RadioBtn);

        g_RenderText.SetFont(LegacyFontRole::Bold);
        mu_swprintf(Text, L"%ls", m_TextListIter->m_szItemName);
        g_RenderText.RenderText(iPos_x + 20, iPos_y + 5, Text);

        g_RenderText.SetFont(LegacyFontRole::Normal);
        mu_swprintf(Text, L"%ls", m_TextListIter->m_szAttribute);
        g_RenderText.RenderText(iPos_x + 20, iPos_y + 17, Text);
    }
    DisableAlphaBlend();

    return TRUE;
}

void CUIPackCheckBuyingListBox::RenderRadioButton(CRadioButton &button)
{
    switch (button.m_byMouseState)
    {
    case CRadioButton::LBTN_DEFAULT:
        RenderRadioButtonImage(CRadioButton::IMAGE_CHECKBTN, button.m_rtCheckBtn.left,
                               button.m_rtCheckBtn.top, CRadioButton::BTN_WIDTH,
                               CRadioButton::BTN_HEIGHT, 0, 0);
        break;
    case CRadioButton::LBTN_UP:
        RenderRadioButtonImage(
            CRadioButton::IMAGE_CHECKBTN, button.m_rtCheckBtn.left, button.m_rtCheckBtn.top,
            CRadioButton::BTN_WIDTH, CRadioButton::BTN_HEIGHT, 0,
            button.m_bCheckState ? CRadioButton::BTN_HEIGHT + CRadioButton::BTN_SPACE : 0);
        break;
    case CRadioButton::LBTN_DOWN:
        RenderRadioButtonImage(CRadioButton::IMAGE_CHECKBTN, button.m_rtCheckBtn.left,
                               button.m_rtCheckBtn.top, CRadioButton::BTN_WIDTH,
                               CRadioButton::BTN_HEIGHT, 0,
                               (CRadioButton::BTN_HEIGHT + CRadioButton::BTN_SPACE) * 2.0f);
        break;
    }
}
void CUIPackCheckBuyingListBox::RenderRadioButtonImage(unsigned int uiImageType, float x, float y,
                                                       float width, float height, float su,
                                                       float sv)
{
    const auto pImage = Bitmaps.GetTextureProperties(uiImageType);
    if (!pImage)
    {
        return;
    }

    float u, v, uw, vh;
    u = ((su + 0.1f) / pImage->width);
    v = ((sv + 0.1f) / pImage->height);
    uw = (width - 0.1f) / pImage->width - (0.1f / pImage->width);
    vh = (height - 0.1f) / pImage->height - (0.1f / pImage->height);

    RenderBitmap(uiImageType, x, y, width, height, u, v, uw, vh);
}
#endif //PBG_ADD_INGAMESHOP_UI_ITEMSHOP

void CUIExtraItemListBox::RenderInterface()
{
    return;
}

BOOL CUIExtraItemListBox::RenderDataLine(int iLineNumber)
{
    EnableAlphaTest();

    if (SLGetSelectLineNum() == m_iCurrentRenderEndLine + iLineNumber + 1)
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
        RenderColor(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, m_iWidth - m_fScrollBarWidth + 1,
                    13);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        g_RenderText.SetTextColor(0, 0, 0, 255);
    }
    else
    {
        g_RenderText.SetTextColor(230, 220, 200, 255);
    }

    glEnable(GL_TEXTURE_2D);
    g_RenderText.SetBgColor(0);

    int iPos_x = m_iPos_x + 8;
    int iPos_y = GetRenderLinePos_y(iLineNumber);

    wchar_t Text[MAX_TEXT_LENGTH + 1] = {0};
    mu_swprintf(Text, L"%ls", m_TextListIter->m_szPattern);
    g_RenderText.RenderText(iPos_x, iPos_y, Text);

    DisableAlphaBlend();

    return TRUE;
}

namespace UI::Modern
{
namespace
{
// Authored design inputs.
enum class DesignKey
{
    TooltipCapacity,
    MinimumContentSize
};

const RmlUiDesign &Design()
{
    static const RmlUiDesign design(
        "Data/UI/PC/Common/tooltip.rml",
        {"RmlTooltipLayer-TooltipCapacity", "RmlTooltipLayer-MinimumContentSize"});
    return design;
}
// End authored design inputs.

const char *ColorClass(RmlTooltipColor color) noexcept
{
    switch (color)
    {
    case RmlTooltipColor::Blue:
        return " blue";
    case RmlTooltipColor::Gray:
        return " gray";
    case RmlTooltipColor::Red:
        return " red";
    case RmlTooltipColor::Yellow:
        return " yellow";
    case RmlTooltipColor::Green:
        return " green";
    case RmlTooltipColor::Purple:
        return " purple";
    case RmlTooltipColor::RedPurple:
        return " red-purple";
    case RmlTooltipColor::Violet:
        return " violet";
    case RmlTooltipColor::Orange:
        return " orange";
    default:
        return "";
    }
}

const char *BackgroundClass(RmlTooltipBackground background) noexcept
{
    switch (background)
    {
    case RmlTooltipBackground::DarkRed:
        return " dark-red";
    case RmlTooltipBackground::DarkBlue:
        return " dark-blue";
    case RmlTooltipBackground::DarkYellow:
        return " dark-yellow";
    case RmlTooltipBackground::GreenBlue:
        return " green-blue";
    default:
        return "";
    }
}

bool SameContent(const RmlTooltipRequest &left, const RmlTooltipRequest &right) noexcept
{
    if (left.lineCount != right.lineCount)
    {
        return false;
    }
    for (int index = 0; index < left.lineCount; ++index)
    {
        const RmlTooltipLine &a = left.lines[index];
        const RmlTooltipLine &b = right.lines[index];
        if (a.color != b.color || a.background != b.background || a.bold != b.bold ||
            std::wcscmp(a.text, b.text) != 0)
        {
            return false;
        }
    }
    return true;
}
} // namespace

void PositionRmlTooltip(Rml::Element &panel, const RmlTooltipRequest &request,
                        const RmlUiScaledViewport &viewport)
{
    const float width = panel.GetOffsetWidth();
    const float height = panel.GetOffsetHeight();
    const float left = std::clamp(request.x / viewport.scale - width / 2, 0.0F,
                                  std::max(0.0F, viewport.width - width));
    const float top =
        std::clamp(request.y / viewport.scale - (request.bottomAnchored ? height : 0.0F), 0.0F,
                   std::max(0.0F, viewport.height - height));
    panel.SetProperty("left", Rml::CreateString("%fpx", left));
    panel.SetProperty("top", Rml::CreateString("%fpx", top));
}

class RmlTooltipLayer::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : host_(keeper, "tooltip-layer-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "Common", "tooltip.rml"))
    {
    }

    bool Prepare(int viewportWidth, int viewportHeight)
    {
        int nextCount = 0;
        {
            std::lock_guard lock(pendingMutex_);
            nextCount = pendingCount_;
            std::copy_n(pending_.begin(), nextCount, captured_.begin());
            pendingCount_ = 0;
        }
        recorded_ = false;
        if (!created_ && nextCount == 0)
        {
            return true;
        }
        if (!EnsureDocument(viewportWidth, viewportHeight))
        {
            return false;
        }
        const RmlUiScaledViewport viewport = host_.Viewport();

        if (!host_.SetVisible(nextCount > 0))
            return false;
        const bool viewportChanged = activeScale_ != viewport.scale ||
                                     activeViewportWidth_ != viewport.width ||
                                     activeViewportHeight_ != viewport.height;
        bool dirty = activeCount_ != nextCount || viewportChanged;
        bool layoutChanged = dirty;
        for (int index = 0; index < static_cast<int>(panels_.size()); ++index)
        {
            const bool visible = index < nextCount;
            if (!visible)
            {
                if (index < activeCount_)
                    panels_[index]->SetProperty("display", "none");
                continue;
            }
            const RmlTooltipRequest &next = captured_[index];
            const RmlTooltipRequest *previous =
                !viewportChanged && index < activeCount_ ? &active_[index] : nullptr;
            if (previous == nullptr)
                panels_[index]->SetProperty("display", "block");
            const bool presentationChanged =
                ApplyPresentation(*panels_[index], next, previous, viewport.scale);
            layoutChanged = layoutChanged || presentationChanged;
            dirty = dirty || presentationChanged || previous == nullptr || previous->x != next.x ||
                    previous->y != next.y || previous->bottomAnchored != next.bottomAnchored;
            if (previous == nullptr || !SameContent(*previous, next))
            {
                ApplyContent(*panels_[index], next);
                dirty = true;
                layoutChanged = true;
            }
            active_[index] = next;
        }
        activeCount_ = nextCount;
        activeScale_ = viewport.scale;
        activeViewportWidth_ = viewport.width;
        activeViewportHeight_ = viewport.height;
        if (dirty && nextCount > 0)
        {
            if (layoutChanged)
                host_.Document()->UpdateDocument();
            for (int index = 0; index < nextCount; ++index)
                PositionRmlTooltip(*panels_[index], active_[index], viewport);
        }
        return host_.CaptureIfDirty(dirty);
    }

    void Stage(const RmlTooltipRequest &request) noexcept
    {
        std::lock_guard lock(pendingMutex_);
        if (pendingCount_ < Design().Number<int>(DesignKey::TooltipCapacity))
        {
            pending_[pendingCount_++] = request;
        }
    }

    bool Record(LegacyRenderFacade &facade)
    {
        if (recorded_)
        {
            return true;
        }
        recorded_ = true;
        return !created_ || host_.Record(facade);
    }

  private:
    bool EnsureDocument(int viewportWidth, int viewportHeight)
    {
        if (!host_.Ensure(viewportWidth, viewportHeight,
                          Design().Number(DesignKey::MinimumContentSize),
                          Design().Number(DesignKey::MinimumContentSize)))
        {
            return false;
        }
        if (created_)
        {
            return true;
        }
        Rml::ElementDocument *const document = host_.Document();
        for (int index = 0; index < Design().Number<int>(DesignKey::TooltipCapacity); ++index)
        {
            panels_[index] = document->GetElementById(Rml::CreateString("tooltip-%d", index));
            if (panels_[index] == nullptr)
            {
                host_.Release();
                return false;
            }
        }
        created_ = true;
        return true;
    }

    static bool ApplyPresentation(Rml::Element &panel, const RmlTooltipRequest &request,
                                  const RmlTooltipRequest *previous, float scale)
    {
        bool dirty = false;
        if (previous == nullptr || previous->width != request.width)
        {
            panel.SetProperty("min-width", Rml::CreateString("%fpx", request.width / scale));
            dirty = true;
        }
        if (previous == nullptr || previous->alignment != request.alignment)
        {
            panel.SetProperty("text-align", request.alignment == RmlTooltipAlignment::Left ? "left"
                                            : request.alignment == RmlTooltipAlignment::Right
                                                ? "right"
                                                : "center");
            dirty = true;
        }
        if (previous == nullptr || previous->framed != request.framed)
        {
            panel.SetClass("framed", request.framed);
            dirty = true;
        }
        return dirty;
    }

    static void ApplyContent(Rml::Element &panel, const RmlTooltipRequest &request)
    {
        Rml::String content;
        for (int index = 0; index < request.lineCount; ++index)
        {
            const RmlTooltipLine &line = request.lines[index];
            const bool blank =
                line.text[0] == L'\n' || (line.text[0] == L' ' && line.text[1] == L'\0');
            content += "<div class=\"tooltip-line";
            content += blank ? " blank" : ColorClass(line.color);
            content += BackgroundClass(line.background);
            content += line.bold ? " bold\">" : "\">";
            if (!blank)
            {
                content += Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(line.text));
            }
            content += "</div>";
        }
        panel.SetInnerRML(content);
    }

    RmlDocumentHost host_;
    std::vector<Rml::Element *> panels_ =
        std::vector<Rml::Element *>(Design().Number<int>(DesignKey::TooltipCapacity));
    std::vector<RmlTooltipRequest> active_ =
        std::vector<RmlTooltipRequest>(Design().Number<int>(DesignKey::TooltipCapacity));
    std::vector<RmlTooltipRequest> pending_ =
        std::vector<RmlTooltipRequest>(Design().Number<int>(DesignKey::TooltipCapacity));
    std::vector<RmlTooltipRequest> captured_ =
        std::vector<RmlTooltipRequest>(Design().Number<int>(DesignKey::TooltipCapacity));
    std::mutex pendingMutex_;
    int activeCount_ = 0;
    int pendingCount_ = 0;
    float activeScale_ = 0.0F;
    int activeViewportWidth_ = 0;
    int activeViewportHeight_ = 0;
    bool created_ = false;
    bool recorded_ = false;
};

RmlTooltipLayer::RmlTooltipLayer(SessionKeeper &keeper) : impl_(std::make_unique<Impl>(keeper))
{
}

RmlTooltipLayer::~RmlTooltipLayer() = default;

bool RmlTooltipLayer::PrepareOnWorker(int viewportWidth, int viewportHeight)
{
    return impl_->Prepare(viewportWidth, viewportHeight);
}

void RmlTooltipLayer::Stage(const RmlTooltipRequest &request) noexcept
{
    impl_->Stage(request);
}

bool RmlTooltipLayer::Record(LegacyRenderFacade &facade)
{
    return impl_->Record(facade);
}
} // namespace UI::Modern

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIBaseButton::RenderText(const wchar_t *text, int x, int y, int sx, int sy,
                                            LegacyFontRole role, DWORD color, DWORD backcolor,
                                            int sort)
{
    g_RenderText.SetFont(role);

    DWORD backuptextcolor = g_RenderText.GetTextColor();
    DWORD backuptextbackcolor = g_RenderText.GetBgColor();

    g_RenderText.SetTextColor(color);
    g_RenderText.SetBgColor(backcolor);
    g_RenderText.RenderText(x, y, text, sx, sy, sort);

    g_RenderText.SetTextColor(backuptextcolor);
    g_RenderText.SetBgColor(backuptextbackcolor);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool SEASON3B::CNewUIButton::Render(bool RendOption)
{
    if (!m_ButtonInfo.empty())
    {
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
        if (RendOption == true)
            RenderImage(m_CurImgIndex, m_Pos.x, m_Pos.y, m_Size.x, m_Size.y, 0.f,
                        m_CurImgState * m_Size.y, 36.f / 64.f, (29.f / 32.f) / 2.f);
        else
            RenderImage(m_CurImgIndex, m_Pos.x, m_Pos.y, m_Size.x, m_Size.y, 0.0f,
                        m_CurImgState * m_Size.y, m_CurImgColor);
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
        if (m_IsImgWidth)
        {
            RenderImage(m_CurImgIndex, m_Pos.x, m_Pos.y, m_Size.x, m_Size.y,
                        m_CurImgState * m_Size.x, 0.0f, m_CurImgColor);
        }
        else
        {
            RenderImage(m_CurImgIndex, m_Pos.x, m_Pos.y, m_Size.x, m_Size.y, 0.0f,
                        m_CurImgState * m_Size.y, m_CurImgColor);
        }
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
    }

    if (m_Name.size() != 0)
    {
        SIZE Fontsize;
        g_RenderText.SetFont(m_textFontRole);
        g_RenderText.MeasureText(m_Name.c_str(), m_Name.size(), &Fontsize);

        Fontsize.cx = Fontsize.cx / ((float)WindowWidth / REFERENCE_WIDTH);
        Fontsize.cy = Fontsize.cy / ((float)WindowHeight / REFERENCE_HEIGHT);

        int x = m_Pos.x + ((m_Size.x / 2) - (Fontsize.cx / 2));
        int y = m_Pos.y + ((m_Size.y / 2) - (Fontsize.cy / 2));

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
        if ((m_bClickEffect == true) && (GetBTState() == BUTTON_STATE_DOWN))
        {
            RenderText(m_Name.c_str(), x + m_iMoveTextPosX + 1, y + m_iMoveTextPosY + 1, m_Size.x,
                       0, m_textFontRole, m_NameColor, m_NameBackColor, RT3_SORT_LEFT);
        }
        else
        {
            RenderText(m_Name.c_str(), x + m_iMoveTextPosX, y + m_iMoveTextPosY, m_Size.x, 0,
                       m_textFontRole, m_NameColor, m_NameBackColor, RT3_SORT_LEFT);
        }
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
        RenderText(m_Name.c_str(), x, y, m_Size.x, 0, m_textFontRole, m_NameColor, m_NameBackColor,
                   RT3_SORT_LEFT);
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
    }

    if (m_TooltipText.size() != 0)
    {
        if (CheckMouseIn(m_Pos.x, m_Pos.y, m_Size.x, m_Size.y))
        {
            SIZE Fontsize;
            g_RenderText.SetFont(m_toolTipFontRole);
            g_RenderText.MeasureText(m_TooltipText.c_str(), m_TooltipText.size(), &Fontsize);

            Fontsize.cx = Fontsize.cx / ((float)WindowWidth / REFERENCE_WIDTH);
            Fontsize.cy = Fontsize.cy / ((float)WindowHeight / REFERENCE_HEIGHT);

            int x = m_Pos.x + ((m_Size.x / 2) - (Fontsize.cx / 2));
            int y = m_Pos.y + m_Size.y + 2;

            int _iTempWidth = x + Fontsize.cx + 6;
            x = (_iTempWidth > REFERENCE_WIDTH) ? (x - (_iTempWidth - REFERENCE_WIDTH)) : x;

            if (m_IsTopPos)
                y = m_Pos.y - (Fontsize.cy + 2);

            RenderText(m_TooltipText.c_str(), x + m_iMoveTextTipPosX, y + m_iMoveTextTipPosY,
                       Fontsize.cx + 6, 0, m_toolTipFontRole, m_TooltipTextColor,
                       RGBA(0, 0, 0, 180), RT3_SORT_CENTER);
            //RenderText( m_TooltipText.c_str(), x, y, Fontsize.cx+6, 0, m_hToolTipFont, m_TooltipTextColor, RGBA(0, 0, 0, 180), RT3_SORT_CENTER );
        }
    }

    return true;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CNewUIRadioButton::Render()
{
    if (m_RadioButtonInfo.size() != 0)
    {
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
        if (m_CurImgIndex != BITMAP_UNKNOWN)
        {
            RenderImage(m_CurImgIndex, m_Pos.x, m_Pos.y, m_Size.x, m_Size.y, 0.0f,
                        m_CurImgState * m_Size.y, m_CurImgColor);
        }
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
        if (m_ImgWidth < m_ImgHeight)
        {
            RenderImage(m_CurImgIndex, m_Pos.x, m_Pos.y, m_Size.x, m_Size.y,
                        m_CurImgState * m_Size.x, 0.0f, m_CurImgColor);
        }
        else
        {
            RenderImage(m_CurImgIndex, m_Pos.x, m_Pos.y, m_Size.x, m_Size.y, 0.0f,
                        m_CurImgState * m_Size.y, m_CurImgColor);
        }
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
    }

    if (m_Name.size() != 0)
    {
        SIZE Fontsize;

        g_RenderText.SetFont(m_textFontRole);

        g_RenderText.MeasureText(m_Name.c_str(), m_Name.size(), &Fontsize);

        Fontsize.cx = Fontsize.cx / ((float)WindowWidth / REFERENCE_WIDTH);
        Fontsize.cy = Fontsize.cy / ((float)WindowHeight / REFERENCE_HEIGHT);

        int x = m_Pos.x + ((m_Size.x / 2) - (Fontsize.cx / 2));
        int y = m_Pos.y + ((m_Size.y / 2) - (Fontsize.cy / 2));

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
        if ((m_bClickEffect == true) && GetBTState() == BUTTON_STATE_DOWN)
        {
            RenderText(m_Name.c_str(), x + 1, y + 1, m_Size.x, 0, m_textFontRole, m_NameColor,
                       m_NameBackColor, RT3_SORT_LEFT);
        }
        else
        {
            RenderText(m_Name.c_str(), x, y, m_Size.x, 0, m_textFontRole, m_NameColor,
                       m_NameBackColor, RT3_SORT_LEFT);
        }
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
        RenderText(m_Name.c_str(), x, y, m_Size.x, 0, m_textFontRole, m_NameColor, m_NameBackColor,
                   RT3_SORT_LEFT);
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
    }

    return true;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CNewUIRadioGroupButton::Render()
{
    for (auto iter = m_RadioList.begin(); iter != m_RadioList.end();)
    {
        auto curiter = iter;
        ++iter;
        CNewUIRadioButton *button = (*curiter);

        button->Render();
    }

    return true;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUICheckBox::Render()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    RenderImage(s_ImgIndex, m_Pos.x, m_Pos.y, m_Size.x, m_Size.y, 0.0, (State) ? 0.0 : m_Size.y);

    if (State)
    {
        RenderImage(s_ImgIndex, m_Pos.x, m_Pos.y, m_Size.x, m_Size.y, 0, 0);
    }
    else
    {
        RenderImage(s_ImgIndex, m_Pos.x, m_Pos.y, m_Size.x, m_Size.y, 0, m_Size.y);
    }

    g_RenderText.SetFont(m_textFontRole);
    g_RenderText.SetTextColor(m_NameColor);
    g_RenderText.SetBgColor(m_NameBackColor);
    g_RenderText.RenderText(m_Pos.x + m_Size.x + 1, m_Pos.y + 4, m_Name.c_str(), 0, 0);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::NewUIBaseButtonLegacyCalls::RenderText(const wchar_t *text, int x, int y, int sx,
                                                      int sy, LegacyFontRole role, DWORD color,
                                                      DWORD backcolor, int sort)
{
    return owner_.RenderText(text, x, y, sx, sy, role, color, backcolor, sort);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()

#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CNewUIComboBox::Render()
{
    if (m_ItemCount <= 0 || m_Labels == nullptr)
        return;

    // --- Closed field ---
    const bool hoverClosed = CheckMouseIn(m_X, m_Y, m_Width, m_ItemHeight);
    const float closedBrightness = m_bOpen ? LegacyComboStyle::BG_BRIGHTNESS_OPEN
                                           : (hoverClosed ? LegacyComboStyle::BG_BRIGHTNESS_HOVER
                                                          : LegacyComboStyle::BG_BRIGHTNESS_IDLE);
    DrawSolidRect(m_X, m_Y, m_Width, m_ItemHeight, closedBrightness);

    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.SetBgColor(0);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.RenderText(m_X + LegacyComboStyle::TEXT_PAD_X, m_Y + LegacyComboStyle::TEXT_PAD_Y,
                            m_Labels[m_SelectedIndex]);

    // Dropdown arrow glyph (plain text -- no asset needed)
    g_RenderText.SetTextColor(255, 230, 200, 255);
    g_RenderText.RenderText(m_X + m_Width - LegacyComboStyle::ARROW_WIDTH,
                            m_Y + LegacyComboStyle::TEXT_PAD_Y, m_bOpen ? L"^" : L"v");

    // --- Expanded list (drawn on top of anything below) ---
    if (!m_bOpen)
        return;

    const int listY = GetListY();
    const int visible = GetVisibleCount();
    const bool hasScrollbar = IsScrollable();
    const int rowWidth = hasScrollbar ? (m_Width - LegacyComboStyle::SCROLLBAR_WIDTH) : m_Width;

    g_RenderText.SetTextColor(255, 255, 255, 255);

    for (int row = 0; row < visible; row++)
    {
        const int absIdx = m_ScrollOffset + row;
        const int itemY = listY + row * m_ItemHeight;
        const bool hoverItem = CheckMouseIn(m_X, itemY, rowWidth, m_ItemHeight);
        const bool isSelected = (absIdx == m_SelectedIndex);

        float bright = LegacyComboStyle::BG_BRIGHTNESS_IDLE;
        if (hoverItem)
            bright = LegacyComboStyle::BG_BRIGHTNESS_HOVER;
        else if (isSelected)
            bright = LegacyComboStyle::BG_BRIGHTNESS_OPEN;

        DrawSolidRect(m_X, itemY, rowWidth, m_ItemHeight, bright);
        g_RenderText.RenderText(m_X + LegacyComboStyle::TEXT_PAD_X,
                                itemY + LegacyComboStyle::TEXT_PAD_Y, m_Labels[absIdx]);
    }

    // --- Scrollbar (track + proportional thumb) ---
    if (hasScrollbar)
    {
        const int listHeight = GetListHeight();
        const int barX = m_X + m_Width - LegacyComboStyle::SCROLLBAR_WIDTH;

        // Track
        DrawSolidRect(barX, listY, LegacyComboStyle::SCROLLBAR_WIDTH, listHeight,
                      LegacyComboStyle::SCROLLBAR_TRACK_BRIGHT);

        // Thumb: size proportional to (visible / total), position to (offset / maxOffset).
        int thumbHeight = (listHeight * visible) / m_ItemCount;
        if (thumbHeight < m_ItemHeight / 2)
            thumbHeight = m_ItemHeight / 2;

        const int maxOffset = GetMaxScrollOffset();
        const int thumbTravel = listHeight - thumbHeight;
        const int thumbY =
            listY + ((maxOffset > 0) ? (thumbTravel * m_ScrollOffset / maxOffset) : 0);

        DrawSolidRect(barX, thumbY, LegacyComboStyle::SCROLLBAR_WIDTH, thumbHeight,
                      LegacyComboStyle::SCROLLBAR_THUMB_BRIGHT);
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CNewUIScrollBar::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_scrollbar_up.tga", IMAGE_SCROLL_TOP);
    LoadBitmapW(L"Interface\\newui_scrollbar_m.tga", IMAGE_SCROLL_MIDDLE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_scrollbar_down.tga", IMAGE_SCROLL_BOTTOM);
    LoadBitmapW(L"Interface\\newui_scroll_on.tga", IMAGE_SCROLLBAR_ON, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_scroll_off.tga", IMAGE_SCROLLBAR_OFF,
                LegacyTextureFilter::Linear);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CNewUIScrollBar::UnloadImages()
{
    DeleteBitmap(IMAGE_SCROLL_TOP);
    DeleteBitmap(IMAGE_SCROLL_MIDDLE);
    DeleteBitmap(IMAGE_SCROLL_BOTTOM);
    DeleteBitmap(IMAGE_SCROLLBAR_ON);
    DeleteBitmap(IMAGE_SCROLLBAR_OFF);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CNewUIScrollBar::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    RenderImage(IMAGE_SCROLL_TOP, m_ptPos.x, m_ptPos.y, SCROLLBAR_TOP_WIDTH, SCROLLBAR_TOP_HEIGHT);

    for (int i = 0; i < m_iScrollBarMiddleNum; i++)
    {
        RenderImage(IMAGE_SCROLL_MIDDLE, m_ptPos.x,
                    m_ptPos.y + SCROLLBAR_TOP_HEIGHT + (i * SCROLLBAR_MIDDLE_HEIGHT),
                    SCROLLBAR_TOP_WIDTH, SCROLLBAR_MIDDLE_HEIGHT);
    }

    if (m_iScrollBarMiddleRemainderPixel > 0)
    {
        RenderImage(IMAGE_SCROLL_MIDDLE, m_ptPos.x,
                    m_ptPos.y + SCROLLBAR_TOP_HEIGHT +
                        (m_iScrollBarMiddleNum * SCROLLBAR_MIDDLE_HEIGHT),
                    SCROLLBAR_TOP_WIDTH, m_iScrollBarMiddleRemainderPixel);
    }

    RenderImage(IMAGE_SCROLL_BOTTOM, m_ptPos.x, m_ptPos.y + m_iHeight - SCROLLBAR_TOP_HEIGHT,
                SCROLLBAR_TOP_WIDTH, SCROLLBAR_TOP_HEIGHT);

    if (m_bScrollBtnActive == true)
    {
        if (m_iScrollBtnMouseEvent == SCROLLBAR_MOUSEBTN_CLICKED)
        {
            glColor4f(0.7f, 0.7f, 0.7f, 1.0f);
        }
        RenderImage(IMAGE_SCROLLBAR_ON, m_ptScrollBtnPos.x, m_ptScrollBtnPos.y, SCROLLBTN_WIDTH,
                    SCROLLBTN_HEIGHT);
    }
    else
    {
        RenderImage(IMAGE_SCROLLBAR_OFF, m_ptScrollBtnPos.x, m_ptScrollBtnPos.y, SCROLLBTN_WIDTH,
                    SCROLLBTN_HEIGHT);
    }
    DisableAlphaBlend();
    return true;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool SEASON3B::CNewUISlideWindow::Render()
{
    m_pSlideMgr->Render();

    return true;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CNewUITextBox::Render()
{
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    for (int iIndex = 0; iIndex < m_iLimitLine; iIndex++)
    {
        int iLineIndex = m_iCurLine + iIndex;

        if (GetLineText(iLineIndex).empty() == false)
        {
            g_RenderText.SetFont(LegacyFontRole::Normal);
            //g_RenderText.SetBgColor( 0, 0, 0, 0 );
            g_RenderText.SetTextColor(255, 255, 255, 255);
            g_RenderText.RenderText(m_ptPos.x, m_ptPos.y + iIndex * m_iTextLineHeight,
                                    GetLineText(iLineIndex).c_str(), m_iWidth, 0, RT3_SORT_LEFT);
        }
    }

    return true;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CButton::Render()
{
    if (!CSprite::m_bShow)
    {
        return;
    }

    if (!m_bEnable)
    {
        if (m_bCheck && m_imageFrames[BTN_DISABLE_CHECK] < 0)
        {
            return;
        }
        if (!m_bCheck && m_imageFrames[BTN_DISABLE] < 0)
        {
            return;
        }
    }

    CSprite::Render();

    if (m_text.empty())
    {
        return;
    }

    g_RenderText.SetTextColor(m_textColor);
    g_RenderText.SetBgColor(0);
    g_RenderText.SetFont(LegacyFontRole::Fixed);

    SIZE size{};
    const int textLength = static_cast<int>(m_text.length());
    g_RenderText.MeasureText(m_text.c_str(), textLength, &size);

    const float textRelativeYPos = (static_cast<float>(CSprite::GetHeight()) - size.cy) * 0.5f;
    g_RenderText.RenderText(
        static_cast<int>(CSprite::GetXPos() / g_fScreenRate_x),
        static_cast<int>((static_cast<float>(CSprite::GetYPos()) + textRelativeYPos) /
                             g_fScreenRate_y +
                         m_fTextAddYPos),
        m_text.c_str(), CSprite::GetWidth() / g_fScreenRate_x, 0, RT3_SORT_CENTER);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CGaugeBar::Render()
{
    if (m_backgroundSprite)
    {
        m_backgroundSprite->Render();
    }
    m_sprGauge.Render();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CSlider::Render()
{
    if (!m_btnThumb.IsShow())
        return;

    if (m_pGaugeBar)
        m_pGaugeBar->Render();
    else if (m_psprBack)
        m_psprBack->Render();
    m_btnThumb.Render();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CWin::Render()
{
    if (m_bShow)
    {
        if (m_psprBg)
            m_psprBg->Render();

        RenderControls();
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CWin::RenderButtons()
{
    CButton *pBtn;
    NODE *position = m_BtnList.GetHeadPosition();
    while (position)
    {
        pBtn = (CButton *)m_BtnList.GetNext(position);
        pBtn->Render();
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CWinEx::Render()
{
    if (CWin::m_bShow)
    {
        for (int i = 0; i < WE_BG_MAX; ++i)
            CWin::m_psprBg[i].Render();

        RenderControls();
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIBaseWindow::DrawOutLine(int iPos_x, int iPos_y, int iWidth, int iHeight)
{
    SetLineColor(0);
    RenderColor((float)iPos_x, (float)iPos_y, (float)iWidth, (float)1);
    SetLineColor(1);
    RenderColor((float)iPos_x, (float)iPos_y + 1, (float)iWidth, (float)3);
    SetLineColor(2);
    RenderColor((float)iPos_x, (float)iPos_y + 4, (float)iWidth, (float)1);

    SetLineColor(2);
    RenderColor((float)iPos_x, (float)iPos_y + iHeight - 5, (float)iWidth, (float)1);
    SetLineColor(1);
    RenderColor((float)iPos_x, (float)iPos_y + iHeight - 4, (float)iWidth, (float)3);
    SetLineColor(0);
    RenderColor((float)iPos_x, (float)iPos_y + iHeight - 1, (float)iWidth, (float)1);

    SetLineColor(0);
    RenderColor((float)iPos_x, (float)iPos_y, (float)1, (float)iHeight);
    SetLineColor(1);
    RenderColor((float)iPos_x + 1, (float)iPos_y + 5, (float)3, (float)iHeight - 10);
    SetLineColor(2);
    RenderColor((float)iPos_x + 4, (float)iPos_y + 1, (float)1, (float)iHeight - 2);

    SetLineColor(2);
    RenderColor((float)iPos_x + iWidth - 5, (float)iPos_y + 1, (float)1, (float)iHeight - 2);
    SetLineColor(1);
    RenderColor((float)iPos_x + iWidth - 4, (float)iPos_y + 5, (float)3, (float)iHeight - 10);
    SetLineColor(0);
    RenderColor((float)iPos_x + iWidth - 1, (float)iPos_y, (float)1, (float)iHeight);

    SetLineColor(4);
    RenderColor((float)iPos_x + 2, (float)iPos_y + 2, (float)1, (float)1);
    RenderColor((float)iPos_x + iWidth - 3, (float)iPos_y + 2, (float)1, (float)1);
    RenderColor((float)iPos_x + 2, (float)iPos_y + iHeight - 3, (float)1, (float)1);
    if (!CheckOption(UIWINDOWSTYLE_RESIZEABLE))
        RenderColor((float)iPos_x + iWidth - 3, (float)iPos_y + iHeight - 3, (float)1, (float)1);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIBaseWindow::SetControlButtonColor(int iSelect)
{
    if (m_iControlButtonClick == iSelect &&
        CheckMouseIn(m_iPos_x + m_iWidth - 38, m_iPos_y + 8, 38, 9) == TRUE)
    {
        if (MouseLButtonPush == true)
            glColor4f(0.6f, 0.6f, 0.6f, 1.0f);
        else
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    }
    else
    {
        glColor4f(0.8f, 0.8f, 0.8f, 1.0f);
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIBaseWindow::Render()
{
    std::call_once(_controlsInitialized, [this]() { InitControls(); });

    EnableAlphaTest();

    if (m_iOptions == UIWINDOWSTYLE_NULL)
        ;
    else if (CheckOption(UIWINDOWSTYLE_FRAME))
    {
        SetLineColor(3);
        RenderColor((float)m_iPos_x, (float)m_iPos_y + 5, (float)m_iWidth, (float)m_iHeight - 10);
    }
    else
    {
        SetLineColor(3);
        RenderColor((float)m_iPos_x, (float)m_iPos_y + 5, (float)m_iWidth, (float)m_iHeight);
    }
    EndRenderColor();

    g_RenderText.SetFont(LegacyFontRole::Normal);

    RenderSub();

    BOOL bBackWindow = FALSE;
    if (g_pWindowMgr->GetTopWindowUIID() != GetUIID())
    {
        bBackWindow = TRUE;
    }
    if (bBackWindow == TRUE)
        glColor4f(0.5f, 0.5f, 0.5f, 1.0f);
    if (CheckOption(UIWINDOWSTYLE_FRAME))
    {
        if (CheckOption(UIWINDOWSTYLE_TITLEBAR))
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 8, (float)m_iPos_x + 6, (float)m_iPos_y + 5,
                         (float)m_iWidth - 12, (float)15, 0.f, 0.f, 4.f / 4.f, 15.f / 16.f);
            if (bBackWindow == TRUE)
                SetLineColor(10);
            else
                SetLineColor(8);
            RenderColor((float)m_iPos_x + 5, (float)m_iPos_y + 5, (float)1, (float)1);
            if (bBackWindow == TRUE)
                SetLineColor(10);
            else
                SetLineColor(9);
            RenderColor((float)m_iPos_x + 5, (float)m_iPos_y + 6, (float)1, (float)13);
            SetLineColor(10);
            RenderColor((float)m_iPos_x + 5, (float)m_iPos_y + 19, (float)1, (float)1);
            SetLineColor(11);
            RenderColor((float)m_iPos_x + m_iWidth - 6, (float)m_iPos_y + 5, (float)1, (float)1);
            SetLineColor(12);
            RenderColor((float)m_iPos_x + m_iWidth - 6, (float)m_iPos_y + 6, (float)1, (float)13);
            SetLineColor(13);
            RenderColor((float)m_iPos_x + m_iWidth - 6, (float)m_iPos_y + 19, (float)1, (float)1);
        }

        DrawOutLine(m_iPos_x, m_iPos_y, m_iWidth, m_iHeight);
        EndRenderColor();
    }
    if (CheckOption(UIWINDOWSTYLE_TITLEBAR))
    {
        EnableAlphaTest();
        SetLineColor(2);
        RenderColor((float)m_iPos_x + 5, (float)m_iPos_y + 20, (float)m_iWidth - 10, (float)1);
        EndRenderColor();

        g_RenderText.SetFont(LegacyFontRole::Bold);
        if (bBackWindow == FALSE)
        {
            g_RenderText.SetTextColor(230, 220, 200, 255);
        }
        else
        {
            g_RenderText.SetTextColor(115, 110, 100, 255);
        }
        g_RenderText.SetBgColor(0);

        wchar_t szTempTitle[256] = {0};
        CutText3(m_strTitle.c_str(), szTempTitle, m_iWidth - 50, 1, 256);
        g_RenderText.RenderText(m_iPos_x + 9, m_iPos_y + 8, szTempTitle);
        if (CheckOption(UIWINDOWSTYLE_MINBUTTON))
        {
            SetControlButtonColor(1);
            RenderBitmap(BITMAP_INTERFACE_EX + 10,
                         (float)m_iPos_x + m_iWidth -
                             (CheckOption(UIWINDOWSTYLE_MAXBUTTON) ? 38 : 27),
                         (float)m_iPos_y + 8, (float)9, (float)9, 0.f, 0.f, 9.f / 32.f, 9.f / 32.f);
        }
        if (CheckOption(UIWINDOWSTYLE_MAXBUTTON))
        {
            SetControlButtonColor(2);
            if (m_bIsMaximize == FALSE)
                RenderBitmap(BITMAP_INTERFACE_EX + 10, (float)m_iPos_x + m_iWidth - 27,
                             (float)m_iPos_y + 8, (float)9, (float)9, 9.f / 32.f, 0.f, 9.f / 32.f,
                             9.f / 32.f);
            else
                RenderBitmap(BITMAP_INTERFACE_EX + 10, (float)m_iPos_x + m_iWidth - 27,
                             (float)m_iPos_y + 8, (float)9, (float)9, 9.f / 32.f, 9.f / 32.f,
                             9.f / 32.f, 9.f / 32.f);
        }
        SetControlButtonColor(3);
        RenderBitmap(BITMAP_INTERFACE_EX + 10, (float)m_iPos_x + m_iWidth - 16, (float)m_iPos_y + 8,
                     (float)9, (float)9, 0.f, 9.f / 32.f, 9.f / 32.f, 9.f / 32.f);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        g_RenderText.SetFont(LegacyFontRole::Normal);
    }
    if (CheckOption(UIWINDOWSTYLE_RESIZEABLE))
    {
        RenderBitmap(BITMAP_INTERFACE_EX + 11, (float)m_iPos_x + m_iWidth - 10,
                     (float)m_iPos_y + m_iHeight - 10, (float)9, (float)9, 0.f, 0.f, 9.f / 16.f,
                     9.f / 16.f);
    }
    if (g_pWindowMgr->GetTopWindowUIID() != GetUIID() || !g_pUIManager->IsOpen(::INTERFACE_FRIEND))
    {
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        DisableAlphaBlend();
    }

    RenderOver();
}
#pragma pack(pop)

// Common character-preview control.
#pragma pack(push)
#pragma pack()
void CUIPhotoViewer::RenderPhotoCharacter()
{
    float fPos_x = m_iPos_x * 1.2f + m_iWidth / 2 - 50;
    float fPos_y = m_iPos_y * 1.2f + m_iHeight * 1.2f - 62;

    const CHARACTER *c = &m_PhotoChar;
    const OBJECT *o = &c->Object;

    glMatrixMode(GL_PROJECTION);
    SaveCameraPerspective();
    glPushMatrix();
    glLoadIdentity();
    glViewport2(m_iPos_x * g_fScreenRate_x, m_iPos_y * g_fScreenRate_y, m_iWidth * g_fScreenRate_x,
                141 * g_fScreenRate_y);
    gluPerspective2(1.f, (float)(m_iWidth * g_fScreenRate_x) / (float)(141 * g_fScreenRate_y), 2000,
                    20000); //g_Camera.ViewNear,g_Camera.ViewFar);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    cameraProjection_.GetModelViewMatrix(g_Camera.Matrix);
    EnableDepthTest();
    EnableDepthMask();

    glRotatef(-90.0f, 1.f, 0.f, 0.f);
    glRotatef(-90.0f, 0.f, 0.f, 1.f);
    glTranslatef(-10000.0f, 0.0f, -75.f);

    if (c->Helper.Type == MODEL_DARK_HORSE_ITEM)
        glTranslatef(-o->Position[0], -o->Position[1], -o->Position[2] - 50.0f);
    else
        glTranslatef(-o->Position[0], -o->Position[1], -o->Position[2]);

    glDisable(GL_ALPHA_TEST);
    glEnable(GL_TEXTURE_2D);
    EnableDepthTest();
    EnableCullFace();
    EnableDepthMask();
    TextureEnable = true;
    AlphaTestEnable = false;
    glDepthFunc(GL_LEQUAL);
    glAlphaFunc(GL_GREATER, 0.25f);
    glDisable(GL_FOG);
    glClear(GL_DEPTH_BUFFER_BIT);
    ObjectDrawInput helperDraw(&m_PhotoHelper);
    helperDraw.preparedPose = &m_PhotoVisual.localMountPose;
    helperDraw.stableBones = true;
    if (c->Helper.Type == MODEL_HORN_OF_UNIRIA)
        helperDraw.position[2] += 10.f;
    else if (c->Helper.Type == MODEL_HORN_OF_DINORANT)
        helperDraw.position[2] += 25.f;
    if (m_photoHelperBones)
        RenderMount(helperDraw, TRUE);
    sessionKeeper_.Renderer()->RenderCharacter(c, o, 0, &m_PhotoVisual);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    const SessionDisplayRect renderRect = SessionOrigin().Display()->LocalRect();
    glViewport2(0, 0, static_cast<int>(renderRect.width), static_cast<int>(renderRect.height));
    RestoreCameraPerspective();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIPhotoViewer::Render()
{
    if (m_bIsWebzenMail == TRUE)
    {
        glColor4f(0.f, 0.f, 0.f, 1.0f);
        RenderColor(m_iPos_x, m_iPos_y, 119.f, 141.f);
        EndRenderColor();
        RenderBitmap(BITMAP_INTERFACE_EX + 22, m_iPos_x + 20, m_iPos_y + 38, 80.f, 62.f, 0.f, 0.f,
                     256.f / 256.f, 195.f / 256.f);
        return;
    }

    RenderPhotoCharacter();

    if (CheckOption(UIPHOTOVIEWER_CANCONTROL))
    {
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        DisableAlphaBlend();
        if (m_bHelpEnable == FALSE)
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 20, m_iPos_x + 1, m_iPos_y + m_iHeight - 17, 16.0f,
                         16.0f, 0.f, 0.f, 16.f / 16.f, 16.f / 16.f);
        }
        else
        {
            glColor4f(0.6f, 0.6f, 0.6f, 1.0f);
            RenderBitmap(BITMAP_INTERFACE_EX + 20, m_iPos_x + 2, m_iPos_y + m_iHeight - 16, 15.0f,
                         15.0f, 0.f, 0.f, 15.f / 16.f, 15.f / 16.f);
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

            TextNum = 0;
            mu_swprintf(TextList[TextNum], I18N::Game::WheelButtonZoomInOut);
            TextListColor[TextNum] = 0;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::LeftClickRotation);
            TextListColor[TextNum] = 0;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::RightClickDefault);
            TextListColor[TextNum] = 0;
            TextBold[TextNum] = false;
            TextNum++;
            SIZE TextSize;
            g_RenderText.MeasureText(L"Z", 1, &TextSize);
            TextSize.cy /= g_fScreenRate_y;
            RenderTipTextList(m_iPos_x + m_iWidth / 2,
                              m_iPos_y + m_iHeight - TextNum * (TextSize.cy + 2), TextNum, 0,
                              RT3_SORT_LEFT);
        }
    }
}
#pragma pack(pop)

namespace UI::Modern
{
Rml::CompiledGeometryHandle TapeRenderInterface::CompileGeometry(
    Rml::Span<const Rml::Vertex> sourceVertices, Rml::Span<const int> sourceIndices)
{
    if (sourceVertices.empty() || sourceIndices.empty() || revisionOwner_ == nullptr)
    {
        return 0;
    }
    const std::uint64_t revision = revisionOwner_->AllocateModernUiGeometryRevision();
    if (revision == 0)
    {
        return 0;
    }

    auto vertices = std::make_shared<std::vector<RenderTapeVertex>>();
    auto indices = std::make_shared<std::vector<std::uint32_t>>();
    vertices->reserve(sourceVertices.size());
    indices->reserve(sourceIndices.size());
    for (const Rml::Vertex &source : sourceVertices)
    {
        const float colorScale = source.colour.alpha ? 1.0F / source.colour.alpha : 0.0F;
        vertices->push_back(
            RenderTapeVertex{{source.position.x, source.position.y, 0.0F, 1.0F},
                             {source.tex_coord.x, source.tex_coord.y},
                             {source.colour.red * colorScale, source.colour.green * colorScale,
                              source.colour.blue * colorScale, source.colour.alpha / 255.0F},
                             {0.0F, 0.0F, 1.0F}});
    }
    for (const int source : sourceIndices)
    {
        indices->push_back(static_cast<std::uint32_t>(source));
    }

    auto geometry = std::make_shared<CompiledGeometry>(
        CompiledGeometry{revision, std::move(vertices), std::move(indices)});
    auto *handle = new std::shared_ptr<const CompiledGeometry>(std::move(geometry));
    return reinterpret_cast<Rml::CompiledGeometryHandle>(handle);
}

void TapeRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle handle,
                                         Rml::Vector2f translation, Rml::TextureHandle texture)
{
    CaptureGeometry(handle, translation, texture, nullptr);
}

void TapeRenderInterface::CaptureGeometry(Rml::CompiledGeometryHandle handle,
                                          Rml::Vector2f translation, Rml::TextureHandle texture,
                                          const std::array<float, 4> *tint)
{
    if (capture_ == nullptr || handle == 0)
    {
        return;
    }
    const auto *geometry =
        reinterpret_cast<const std::shared_ptr<const CompiledGeometry> *>(handle);
    const auto resolvedTexture = CaptureTexture(texture);
    if (!resolvedTexture.has_value())
    {
        return;
    }
    const LogicalRenderAssetRef asset = *resolvedTexture;
    RenderTapeConstants constants;
    const RmlUiPresentation &presentation = capture_->presentation;
    const RenderTapeRect fullViewport{0, 0, presentation.physicalWidth,
                                      presentation.physicalHeight};
    constants.viewport = fullViewport;
    constants.scissor = fullViewport;
    constants.depthTestEnable = false;
    constants.depthWriteEnable = false;
    constants.cullEnable = false;
    constants.blendEnable = true;
    constants.blendSrc = RenderBlendFactor::SrcAlpha;
    constants.blendDst = RenderBlendFactor::OneMinusSrcAlpha;
    constants.alphaTestEnable = false;
    constants.fogEnable = false;
    constants.lightingEnable = false;
    ApplyClipState(constants);
    constants.textureEnable = IsValid(asset);
    constants.textureEnvironment = RenderTextureEnvironment::Modulate;
    if (tint)
    {
        constants.textureEnvironment = RenderTextureEnvironment::GfxTint;
        constants.textureTint = *tint;
    }
    constants.projection =
        CalculateRmlUiProjection(presentation.physicalWidth, presentation.physicalHeight);
    if (scissorEnabled_)
    {
        const int left = static_cast<int>(std::floor(scissor_.Left() * presentation.scaleX));
        const int right = static_cast<int>(std::ceil(scissor_.Right() * presentation.scaleX));
        const int top = static_cast<int>(std::floor(scissor_.Top() * presentation.scaleY));
        const int lower = static_cast<int>(std::ceil(scissor_.Bottom() * presentation.scaleY));
        const int width = right - left;
        const int height = lower - top;
        const int bottom = static_cast<int>(presentation.physicalHeight) - lower;
        if (width <= 0 || height <= 0 || bottom < 0)
        {
            capture_->valid = false;
            return;
        }
        constants.scissorEnable = true;
        constants.scissor = {left, bottom, static_cast<std::uint32_t>(width),
                             static_cast<std::uint32_t>(height)};
    }

    ApplyGeometryTransform(constants, presentation, translation);

    capture_->geometryLeases.push_back(LogicalGeometryAssetLease{
        revisionOwner_->MakeModernUiGeometryAssetRef((*geometry)->revision), (*geometry)->vertices,
        (*geometry)->indices, nullptr});
    const LogicalGeometryAssetLease &lease = capture_->geometryLeases.back();
    capture_->drawPlan.push_back(TrustedGeometryDraw{
        &lease, 0, static_cast<std::uint32_t>(lease.vertices->size()), 0,
        static_cast<std::uint32_t>(lease.indices->size()), RenderIndexTopology::Triangles,
        RenderPipelineKey{}, constants, asset, 0});
}

Rml::CompiledShaderHandle TapeRenderInterface::CompileShader(const Rml::String &name,
                                                             const Rml::Dictionary &parameters)
{
    if (name != "gfx-tint")
        return 0;
    const auto found = parameters.find("tint");
    if (found == parameters.end())
        return 0;
    const auto value = found->second.Get<Rml::Vector4f>();
    const std::array<float, 4> tint{value.x, value.y, value.z, value.w};
    for (float channel : tint)
        if (!std::isfinite(channel))
            return 0;
    return reinterpret_cast<Rml::CompiledShaderHandle>(new std::array<float, 4>(tint));
}

void TapeRenderInterface::ReleaseShader(Rml::CompiledShaderHandle handle)
{
    delete reinterpret_cast<std::array<float, 4> *>(handle);
}

void TapeRenderInterface::RenderShader(Rml::CompiledShaderHandle shader,
                                       Rml::CompiledGeometryHandle geometry,
                                       Rml::Vector2f translation, Rml::TextureHandle texture)
{
    CaptureGeometry(geometry, translation, texture,
                    reinterpret_cast<const std::array<float, 4> *>(shader));
}

void TapeRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle handle)
{
    delete reinterpret_cast<std::shared_ptr<const CompiledGeometry> *>(handle);
}

Rml::TextureHandle TapeRenderInterface::LoadTexture(Rml::Vector2i &dimensions,
                                                    const Rml::String &source)
{
    const auto asset = ResolveUiAsset(bitmaps_, source);
    if (!asset.has_value() || !RetainLoadedTexture(asset->BitmapIndex, *asset))
    {
        return 0;
    }
    dimensions = {static_cast<int>(asset->Width), static_cast<int>(asset->Height)};
    return asset->BitmapIndex;
}

Rml::TextureHandle TapeRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                        Rml::Vector2i dimensions)
{
    if (dimensions.x <= 0 || dimensions.y <= 0 ||
        source.size() !=
            static_cast<std::size_t>(dimensions.x) * static_cast<std::size_t>(dimensions.y) * 4U)
    {
        return 0;
    }
    const auto id = bitmaps_.AllocateDynamicIdentity();
    if (!id.has_value())
    {
        return 0;
    }
    std::shared_ptr<std::vector<std::byte>> pixels;
    try
    {
        pixels = std::make_shared<std::vector<std::byte>>(source.size());
        CopyStraightAlphaPixels(source, *pixels);
    }
    catch (...)
    {
        return 0;
    }
    const LogicalRenderAssetRef asset{*id, 1};
    const RenderSamplerIntent sampler{LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge};
    if (!bitmaps_.CommitOwnerProducedRevision(asset, static_cast<std::uint32_t>(dimensions.x),
                                              static_cast<std::uint32_t>(dimensions.y), sampler,
                                              std::move(pixels)))
    {
        (void)bitmaps_.RetireLogicalAsset(asset);
        return 0;
    }
    const Rml::TextureHandle handle = nextGeneratedTexture_++;
    try
    {
        generatedTextures_.emplace(
            handle, std::shared_ptr<const LogicalRenderAssetRef>(
                        new LogicalRenderAssetRef(asset),
                        [bitmaps = &bitmaps_](const LogicalRenderAssetRef *retained) {
                            (void)bitmaps->RetireLogicalAsset(*retained);
                            delete retained;
                        }));
    }
    catch (...)
    {
        (void)bitmaps_.RetireLogicalAsset(asset);
        return 0;
    }
    return handle;
}

void TapeRenderInterface::ReleaseTexture(Rml::TextureHandle handle)
{
    generatedTextures_.erase(handle);
    loadedTextures_.erase(handle);
}

void TapeRenderInterface::EnableScissorRegion(bool enable)
{
    scissorEnabled_ = enable;
}

void TapeRenderInterface::EnableClipMask(bool enable)
{
    clipEnabled_ = enable;
}

void TapeRenderInterface::RenderToClipMask(Rml::ClipMaskOperation operation,
                                           Rml::CompiledGeometryHandle geometry,
                                           Rml::Vector2f translation)
{
    if (!capture_)
        return;
    const bool inverse = operation == Rml::ClipMaskOperation::SetInverse;
    if (operation != Rml::ClipMaskOperation::Intersect)
    {
        capture_->clipResets.push_back({capture_->drawPlan.size(), inverse ? 1U : 0U});
        clipReference_ = inverse ? 1U : 0U;
    }
    else if (clipReference_ == 255U)
    {
        // The native target has eight stencil bits. Reject at state change.
        capture_->valid = false;
        return;
    }
    clipWriteOperation_ = inverse ? RenderStencilOperation::Decr : RenderStencilOperation::Incr;
    CaptureGeometry(geometry, translation, 0, nullptr);
    clipWriteOperation_ = RenderStencilOperation::Keep;
    clipReference_ = inverse ? 1U : clipReference_ + 1U;
}

void TapeRenderInterface::ApplyClipState(RenderTapeConstants &constants) const noexcept
{
    const bool writing = clipWriteOperation_ != RenderStencilOperation::Keep;
    if (!writing && !clipEnabled_)
        return;
    constants.stencilEnable = true;
    constants.stencilCompare = RenderCompareFunction::Equal;
    constants.stencilReference = clipReference_;
    constants.stencilPassOp = clipWriteOperation_;
    if (writing)
        constants.colorWriteMask = {false, false, false, false};
}

void TapeRenderInterface::SetScissorRegion(Rml::Rectanglei region)
{
    scissor_ = region;
}

void TapeRenderInterface::SetTransform(const Rml::Matrix4f *transform)
{
    transform_ = transform ? *transform : Rml::Matrix4f::Identity();
}

void TapeRenderInterface::ApplyGeometryTransform(RenderTapeConstants &constants,
                                                 const RmlUiPresentation &presentation,
                                                 Rml::Vector2f translation) const
{
    const Rml::ColumnMajorMatrix4f modelView =
        Rml::Matrix4f::Translate(0, static_cast<float>(presentation.physicalHeight), 0) *
        Rml::Matrix4f::Scale(presentation.scaleX, -presentation.scaleY, 1) * transform_ *
        Rml::Matrix4f::Translate(translation.x, translation.y, 0);
    std::copy_n(modelView.data(), constants.modelView.size(), constants.modelView.begin());
}

void TapeRenderInterface::BeginCapture(RmlUiRenderSnapshot &capture, SessionRenderUnit &renderer,
                                       CGlobalBitmap &bitmaps,
                                       SessionTextureNamespace &textures) noexcept
{
    capture_ = &capture;
    revisionOwner_ = &renderer;
    captureBitmaps_ = &bitmaps;
    captureTextures_ = &textures;
    scissorEnabled_ = false;
    scissor_ = {};
    clipEnabled_ = false;
    clipReference_ = 0;
    clipWriteOperation_ = RenderStencilOperation::Keep;
    transform_ = Rml::Matrix4f::Identity();
}

void TapeRenderInterface::EndCapture() noexcept
{
    capture_ = nullptr;
    revisionOwner_ = nullptr;
    captureBitmaps_ = nullptr;
    captureTextures_ = nullptr;
}

std::optional<LogicalRenderAssetRef> TapeRenderInterface::CaptureTexture(Rml::TextureHandle texture)
{
    LogicalRenderAssetRef asset;
    if (texture != 0)
    {
        const auto generated = generatedTextures_.find(texture);
        if (generated != generatedTextures_.end())
        {
            asset = *generated->second;
            // Rml may rebuild the font atlas while other panels retain
            // snapshots that still draw from this exact texture.
            capture_->textureLeases.push_back(generated->second);
        }
        else
        {
            auto loaded = loadedTextures_.find(texture);
            if (loaded == loadedTextures_.end() && captureBitmaps_ != nullptr &&
                captureTextures_ != nullptr)
            {
                auto metadata = captureBitmaps_->TryDescribe(static_cast<std::uint32_t>(texture));
                if (!metadata.has_value())
                {
                    metadata = captureTextures_->TryDescribe(static_cast<std::uint32_t>(texture));
                }
                if (metadata.has_value() && IsValid(metadata->Asset))
                {
                    if (!RetainLoadedTexture(texture, *metadata))
                    {
                        return std::nullopt;
                    }
                    loaded = loadedTextures_.find(texture);
                }
            }
            if (loaded == loadedTextures_.end())
            {
                return std::nullopt;
            }
            asset = loaded->second->asset;
            capture_->textureLeases.emplace_back(loaded->second, &loaded->second->asset);
        }
    }
    return asset;
}

bool TapeRenderInterface::RetainLoadedTexture(Rml::TextureHandle handle,
                                              const LogicalRenderAssetMetadata &metadata)
{
    const auto existing = loadedTextures_.find(handle);
    if (existing != loadedTextures_.end())
    {
        return existing->second->asset == metadata.Asset;
    }
    try
    {
        auto retained = std::make_shared<LoadedTexture>();
        retained->asset = metadata.Asset;
        retained->bitmapIndex = metadata.BitmapIndex;
        if (!bitmaps_.RetainImage(metadata.BitmapIndex))
            return false;
        retained->owner = &bitmaps_;
        loadedTextures_.emplace(handle, std::move(retained));
        return true;
    }
    catch (...)
    {
        return false;
    }
}
} // namespace UI::Modern

bool SEASON3B::CNewUISlideWindow::PrepareModernUiOnWorker(int width, int height)
{
    return m_pSlideMgr->PrepareModernUiOnWorker(width, height);
}
