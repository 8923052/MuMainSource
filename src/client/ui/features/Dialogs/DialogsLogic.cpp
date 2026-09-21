#include "ui/features/Dialogs/DialogsLogic.h"
#include "I18N/All.h"
#include "app/AppWindow.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Quests.h"
#include "domain/Shop.h"
#include "domain/WorldSimulation.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
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
#include "support/Scenes.h"
#include "ui/features/Dialogs/DialogsRender.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/Shell/ShellLogic.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"
#include "ui/session/UiSessionRender.h"

//  UIPopup.cpp
//////////////////////////////////////////////////////////////////////////

CUIPopup::CUIPopup(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_OkButton(keeper), m_CancelButton(keeper),
      m_YesButton(keeper), m_NoButton(keeper)
{
    int nButtonID = 0;
    m_OkButton.Init(nButtonID++, I18N::Game::OK);
    m_OkButton.SetParentUIID(0);
    m_OkButton.SetSize(50, 18);
    m_CancelButton.Init(nButtonID++, I18N::Game::Cancel);
    m_CancelButton.SetParentUIID(0);
    m_CancelButton.SetSize(50, 18);
    m_YesButton.Init(nButtonID++, I18N::Game::Yes);
    m_YesButton.SetParentUIID(0);
    m_YesButton.SetSize(50, 18);
    m_NoButton.Init(nButtonID++, I18N::Game::No);
    m_NoButton.SetParentUIID(0);
    m_NoButton.SetSize(50, 18);

    m_dwPopupStartTime = 0;
    m_dwPopupEndTime = 0;
    m_dwPopupElapseTime = 5000;

    Init();
}

CUIPopup::~CUIPopup()
{
}

void CUIPopup::Init()
{
    PopupResultFuncPointer = NULL;
    PopupUpdateInputFuncPointer = NULL;
    PopupRenderFuncPointer = NULL;

    m_dwPopupID = 0;
    m_nPopupTextCount = 0;
    m_PopupType = 0;
    ZeroMemory(m_szPopupText, sizeof(char) * MAX_POPUP_TEXTLINE * MAX_POPUP_TEXTLENGTH);
    ZeroMemory(m_szInputText, sizeof(char) * 1024);
}

DWORD CUIPopup::SetPopup(const wchar_t *pszText, int nLineCount, int nBufferSize, int Type,
                         std::function<int(POPUP_RESULT)> resultFunc, POPUP_ALIGN Align)
{
    if (nLineCount > MAX_POPUP_TEXTLINE)
    {
        __TraceF(TEXT("CUIPopup::SetPopup\n"));
        return 0;
    }
    if (m_dwPopupID != 0)
    {
        __TraceF(TEXT("CUIPopup::SetPopup\n"));
        g_pSystemLogBox->AddText(L"SetPopup", SEASON3B::TYPE_SYSTEM_MESSAGE);
        return 0;
    }

    Init();
    m_Align = Align;
    m_sizePopup.cy = 0;
    SIZE sizeText;
    for (int i = 0; i < nLineCount; ++i)
    {
        if (pszText[i * nBufferSize])
        {
            wcscpy(m_szPopupText[i], &pszText[i * nBufferSize]);
            g_RenderText.MeasureText(m_szPopupText[i], wcslen(m_szPopupText[i]), &sizeText);
            m_sizePopup.cy += sizeText.cy + 2;
        }
    }
    m_nPopupTextCount = nLineCount;
    m_PopupType = Type;
    PopupResultFuncPointer = std::move(resultFunc);

    m_sizePopup.cx = 213;
    m_sizePopup.cy += 43;
    if (m_PopupType & POPUP_OK || m_PopupType & POPUP_OKCANCEL || m_PopupType & POPUP_YESNO)
        m_sizePopup.cy += 20;
    if (m_PopupType & POPUP_INPUT)
        m_sizePopup.cy += 17;
    if (m_PopupType & POPUP_TIMEOUT)
        m_sizePopup.cy += 17;

    m_dwPopupStartTime = GetTickCount();
    m_dwPopupEndTime = m_dwPopupStartTime + m_dwPopupElapseTime;
    m_dwPopupID = m_dwPopupStartTime;

    return m_dwPopupID;
}

void CUIPopup::SetInputMode(int nInputSize, int nTextLength, UIOPTIONS Options)
{
    m_nInputSize = nInputSize;
    m_nInputTextLength = nTextLength;
    m_InputOptions = Options;
}

bool CUIPopup::IsInputEnable()
{
    if (m_dwPopupID != 0 && m_PopupType & POPUP_INPUT)
        return true;
    else
        return false;
}

void CUIPopup::SetTimeOut(DWORD dwElapseTime)
{
    m_dwPopupElapseTime = dwElapseTime;
}

void CUIPopup::SetPopupExtraFunc(std::function<void()> inputFunc, std::function<void()> renderFunc)
{
    PopupUpdateInputFuncPointer = std::move(inputFunc);
    PopupRenderFuncPointer = std::move(renderFunc);
}

wchar_t *CUIPopup::GetInputText()
{
    if (g_iChatInputType == 1)
        g_pSingleTextInputBox->GetText(m_szInputText, 1024);
    else
    {
        if (InputText[0])
            wcscpy(m_szInputText, InputText[0]);
        else
            m_szInputText[0] = 0;
    }

    return m_szInputText;
}

DWORD CUIPopup::GetPopupID()
{
    return m_dwPopupID;
}

void CUIPopup::Close()
{
    if (m_dwPopupID == 0)
        return;

    if (m_PopupType & POPUP_INPUT)
    {
        if (g_iChatInputType == 1)
        {
            g_pSingleTextInputBox->SetText(NULL);
            SaveIMEStatus();
            g_pSingleTextInputBox->SetState(UISTATE_HIDE);
        }
        else
            memset(InputText[0], 0, MAX_USERNAME_SIZE);

        InputLength[0] = 0;
        InputTextMax[0] = MAX_USERNAME_SIZE;
    }

    m_dwPopupID = 0;
}

void CUIPopup::CancelPopup()
{
    if (m_dwPopupID == 0)
        return;

    if (PopupResultFuncPointer)
    {
        if (m_PopupType & POPUP_CUSTOM)
        {
        }
        else if (m_PopupType & POPUP_OK)
        {
            PopupResultFuncPointer(POPUP_RESULT_OK);
            PopupResultFuncPointer = NULL;
        }
        else if (m_PopupType & POPUP_OKCANCEL)
        {
            PopupResultFuncPointer(POPUP_RESULT_CANCEL);
            PopupResultFuncPointer = NULL;
        }
        else if (m_PopupType & POPUP_YESNO)
        {
            PopupResultFuncPointer(POPUP_RESULT_NO);
            PopupResultFuncPointer = NULL;
        }
        else
        {
            PopupResultFuncPointer(POPUP_RESULT_NONE);
            PopupResultFuncPointer = NULL;
        }
    }

    PopupUpdateInputFuncPointer = NULL;
    PopupRenderFuncPointer = NULL;
    Close();
}

bool CUIPopup::PressKey(int nKey)
{
    if (m_dwPopupID == 0)
        return false;

    if (m_PopupType & POPUP_CUSTOM)
    {
        if (nKey == VK_ESCAPE)
        {
            Close();
            return true;
        }
    }
    else if (m_PopupType & POPUP_OK)
    {
        if (nKey == VK_ESCAPE)
        {
            if (PopupResultFuncPointer)
            {
                if (PopupResultFuncPointer(POPUP_RESULT_OK | POPUP_RESULT_ESC) == 0)
                    return true;
                PopupResultFuncPointer = NULL;
            }
            Close();
            return true;
        }
    }
    else if (m_PopupType & POPUP_OKCANCEL)
    {
        if (nKey == VK_ESCAPE)
        {
            if (PopupResultFuncPointer)
            {
                if (PopupResultFuncPointer(POPUP_RESULT_CANCEL | POPUP_RESULT_ESC) == 0)
                    return true;
                PopupResultFuncPointer = NULL;
            }
            Close();
            return true;
        }
    }
    else if (m_PopupType & POPUP_YESNO)
    {
        if (nKey == VK_ESCAPE)
        {
            if (PopupResultFuncPointer)
            {
                if (PopupResultFuncPointer(POPUP_RESULT_NO | POPUP_RESULT_ESC) == 0)
                    return true;
                PopupResultFuncPointer = NULL;
            }
            Close();
            return true;
        }
    }
    else
    {
        if (nKey == VK_ESCAPE)
        {
            if (PopupResultFuncPointer)
            {
                if (PopupResultFuncPointer(POPUP_RESULT_NONE | POPUP_RESULT_ESC) == 0)
                    return true;
                PopupResultFuncPointer = NULL;
            }
            Close();
            return true;
        }
    }

    if (nKey == VK_RETURN && m_PopupType & POPUP_INPUT)
    {
        if (m_PopupType & POPUP_OK)
        {
            if (PopupResultFuncPointer)
            {
                if (PopupResultFuncPointer(POPUP_RESULT_OK) == 0)
                    return true;
                PopupResultFuncPointer = NULL;
            }
            Close();
            return true;
        }
        else if (m_PopupType & POPUP_OKCANCEL)
        {
            if (PopupResultFuncPointer)
            {
                if (PopupResultFuncPointer(POPUP_RESULT_OK) == 0)
                    return true;
                PopupResultFuncPointer = NULL;
            }
            Close();
            return true;
        }
        else if (m_PopupType & POPUP_YESNO)
        {
            if (PopupResultFuncPointer)
            {
                if (PopupResultFuncPointer(POPUP_RESULT_YES) == 0)
                    return true;
                PopupResultFuncPointer = NULL;
            }
            Close();
            return true;
        }
        else
        {
            if (PopupResultFuncPointer)
            {
                if (PopupResultFuncPointer(POPUP_RESULT_NONE) == 0)
                    return true;
                PopupResultFuncPointer = NULL;
            }
            Close();
            return true;
        }
    }

    return false;
}

bool CUIPopup::CheckTimeOut()
{
    if (!(m_PopupType & POPUP_TIMEOUT))
        return false;

    DWORD dwCurrTime = GetTickCount();
    if (dwCurrTime >= m_dwPopupEndTime)
        return true;
    else
        return false;
}

void CUIPopup::UpdateInput()
{
    if (m_dwPopupID == 0)
        return;

    float fSubWinPos_x = 320 - m_sizePopup.cx / 2;
    float fSubWinPos_y = 130 - m_sizePopup.cy / 2;
    if (CheckMouseIn(fSubWinPos_x, fSubWinPos_y, m_sizePopup.cx, m_sizePopup.cy))
        MouseOnWindow = TRUE;

    if (PopupUpdateInputFuncPointer)
    {
        PopupUpdateInputFuncPointer();
    }
    else
    {
        bool bTimeOut = CheckTimeOut();

        if (m_PopupType & POPUP_OK)
        {
            if (bTimeOut)
            {
                if (PopupResultFuncPointer)
                {
                    if (PopupResultFuncPointer(POPUP_RESULT_TIMEOUT) == 0)
                        return;
                    PopupResultFuncPointer = NULL;
                }
                Close();
            }
            else if (m_OkButton.DoMouseAction())
            {
                if (PopupResultFuncPointer)
                {
                    if (PopupResultFuncPointer(POPUP_RESULT_OK) == 0)
                        return;
                    PopupResultFuncPointer = NULL;
                }
                Close();
            }
        }
        else if (m_PopupType & POPUP_OKCANCEL)
        {
            if (bTimeOut)
            {
                if (PopupResultFuncPointer)
                {
                    if (PopupResultFuncPointer(POPUP_RESULT_TIMEOUT) == 0)
                        return;
                    PopupResultFuncPointer = NULL;
                }
                Close();
            }
            else if (m_OkButton.DoMouseAction())
            {
                if (PopupResultFuncPointer)
                {
                    if (PopupResultFuncPointer(POPUP_RESULT_OK) == 0)
                        return;
                    PopupResultFuncPointer = NULL;
                }
                Close();
            }
            else if (m_CancelButton.DoMouseAction())
            {
                if (PopupResultFuncPointer)
                {
                    if (PopupResultFuncPointer(POPUP_RESULT_CANCEL) == 0)
                        return;
                    PopupResultFuncPointer = NULL;
                }
                Close();
            }
        }
        else if (m_PopupType & POPUP_YESNO)
        {
            if (bTimeOut)
            {
                if (PopupResultFuncPointer)
                {
                    if (PopupResultFuncPointer(POPUP_RESULT_TIMEOUT) == 0)
                        return;
                    PopupResultFuncPointer = NULL;
                }
                Close();
            }
            else if (m_YesButton.DoMouseAction())
            {
                if (PopupResultFuncPointer)
                {
                    if (PopupResultFuncPointer(POPUP_RESULT_YES) == 0)
                        return;
                    PopupResultFuncPointer = NULL;
                }
                Close();
            }
            else if (m_NoButton.DoMouseAction())
            {
                if (PopupResultFuncPointer)
                {
                    if (PopupResultFuncPointer(POPUP_RESULT_NO) == 0)
                        return;
                    PopupResultFuncPointer = NULL;
                }
                Close();
            }
        }
        else
        {
            if (bTimeOut)
            {
                if (PopupResultFuncPointer)
                {
                    if (PopupResultFuncPointer(POPUP_RESULT_TIMEOUT) == 0)
                        return;
                    PopupResultFuncPointer = NULL;
                }
                Close();
            }
        }
    }
}

//
//////////////////////////////////////////////////////////////////////

using namespace SEASON3B;

//////////////////////////////////////////////////////////////////////
// CNewUIMessageBoxBase
//////////////////////////////////////////////////////////////////////

SEASON3B::CNewUIMessageBoxBase::CNewUIMessageBoxBase(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), mapProcess_(MapProcessForConstruction()),
      gameplay_(GameplayForConstruction()), sessionUi_(keeper.UiForConstruction())
{
    Release();
}

SEASON3B::CNewUIMessageBoxBase::~CNewUIMessageBoxBase()
{
    Release();
}

bool SEASON3B::CNewUIMessageBoxBase::ShowOkMessageBox(const std::wstring &message)
{
    return CreateOkMessageBox(message);
}

bool SEASON3B::CNewUIMessageBoxBase::Create(int x, int y, int width, int height,
                                            float fPriority /* = 3.f*/)
{
    SetPos(x, y);
    SetSize(width, height);
    m_fPriority = fPriority;
    m_fOpacityAlpha = 0.5f;
    Vector(0.0f, 0.0f, 0.0f, m_vColor);
    return true;
}

void SEASON3B::CNewUIMessageBoxBase::Release()
{
    m_Pos.x = m_Pos.y = 0;
    m_Size.cx = m_Size.cy = 0;
    m_fPriority = 0.f;
    m_bCanMove = false;

    RemoveAllCallbackFuncs();
}

const POINT &SEASON3B::CNewUIMessageBoxBase::GetPos()
{
    return m_Pos;
}

const SIZE &SEASON3B::CNewUIMessageBoxBase::GetSize()
{
    return m_Size;
}

float SEASON3B::CNewUIMessageBoxBase::GetPriority() const
{
    return 8.f;
}

void SEASON3B::CNewUIMessageBoxBase::SetCanMove(bool bCanMove)
{
    m_bCanMove = bCanMove;
}

bool SEASON3B::CNewUIMessageBoxBase::CanMove()
{
    return m_bCanMove;
}

void SEASON3B::CNewUIMessageBoxBase::AddCallbackFunc(EVENT_CALLBACK pFunc, DWORD dwEvent)
{
    auto mi = m_mapCallbacks.find(dwEvent);
    if (mi != m_mapCallbacks.end())
        m_mapCallbacks.erase(mi);
    m_mapCallbacks.insert(type_map_callback::value_type(dwEvent, pFunc));
}

void SEASON3B::CNewUIMessageBoxBase::RemoveCallbackFunc(DWORD dwEvent)
{
    auto mi = m_mapCallbacks.find(dwEvent);
    if (mi != m_mapCallbacks.end())
        m_mapCallbacks.erase(mi);
}

void SEASON3B::CNewUIMessageBoxBase::RemoveAllCallbackFuncs()
{
    m_mapCallbacks.clear();
}

EVENT_CALLBACK SEASON3B::CNewUIMessageBoxBase::GetCallbackFunc(DWORD dwEvent)
{
    auto mi = m_mapCallbacks.find(dwEvent);
    if (mi != m_mapCallbacks.end())
        return (*mi).second;
    return NULL;
}

void SEASON3B::CNewUIMessageBoxBase::SendEvent(CNewUIMessageBoxBase *pOwner, DWORD dwEvent)
{
    g_MessageBox.SendEvent(pOwner, dwEvent);
}

void SEASON3B::CNewUIMessageBoxBase::SendEvent(CNewUIMessageBoxBase *pOwner, DWORD dwEvent,
                                               const leaf::xstreambuf &xParam)
{
    g_MessageBox.SendEvent(pOwner, dwEvent, xParam);
}

void SEASON3B::CNewUIMessageBoxBase::SetMsgBackOpacity(float _fAlpha)
{
    m_fOpacityAlpha = _fAlpha;
}

void SEASON3B::CNewUIMessageBoxBase::SetMsgBackColor(vec3_t _vColor)
{
    if (!_vColor)
    {
        Vector(0.0f, 0.0f, 0.0f, m_vColor);
    }
    else
    {
        VectorCopy(_vColor, m_vColor);
    }
}

SEASON3B::CNewUIMessageBoxMng::CNewUIMessageBoxMng(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), m_pNewUIMng(NULL), m_pMsgBoxFactory(NULL),
      m_EventState(EVENT_NONE),
      m_modernSystemMenu(std::make_unique<UI::Modern::PC::SystemMenu::RmlSystemMenuPanel>(keeper)),
      m_modernMessageBoxPanel(std::make_unique<UI::Modern::RmlMessageBoxPanel>(keeper))
{
}

SEASON3B::CNewUIMessageBoxMng::~CNewUIMessageBoxMng()
{
    Release();
}

bool SEASON3B::CNewUIMessageBoxMng::Create(CNewUIManager *pNewUIMng)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_MESSAGEBOX, this);

    m_pMsgBoxFactory = new CNewUIMessageBoxFactory;
    m_modernMessageBoxPanel->CreateS16Caution();

    LoadImages();

    return true;
}

void SEASON3B::CNewUIMessageBoxMng::Release()
{
    m_modernSystemMenu->Release();
    m_modernMessageBoxPanel->Release();
    if (m_pMsgBoxFactory == nullptr && m_pNewUIMng == nullptr)
    {
        PopAllEvents();
        return;
    }

    UnloadImages();

    PopAllEvents();
    PopAllMessageBoxes();

    SAFE_DELETE(m_pMsgBoxFactory);

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool SEASON3B::CNewUIMessageBoxMng::UpdateMouseEvent()
{
    std::sort(m_vecMsgBoxes.begin(), m_vecMsgBoxes.end(), ComparePriority);
    auto vi = m_vecMsgBoxes.begin();
    if (vi == m_vecMsgBoxes.end())
        return true;

    CNewUIMessageBoxBase *pCurMsgBox = (*vi);

    if (m_EventState == EVENT_NONE && false == MouseLButtonPush &&
        CheckMouseIn(pCurMsgBox->GetPos().x, pCurMsgBox->GetPos().y, pCurMsgBox->GetSize().cx,
                     pCurMsgBox->GetSize().cy))
    {
        SendEvent(pCurMsgBox, MSGBOX_EVENT_MOUSE_HOVER);
        m_EventState = EVENT_WND_MOUSE_HOVER;
    }
    else if (m_EventState == EVENT_WND_MOUSE_HOVER && false == MouseLButtonPush &&
             false == CheckMouseIn(pCurMsgBox->GetPos().x, pCurMsgBox->GetPos().y,
                                   pCurMsgBox->GetSize().cx, pCurMsgBox->GetSize().cy))
    {
        m_EventState = EVENT_NONE;
    }
    else if (m_EventState == EVENT_WND_MOUSE_HOVER && MouseLButtonPush &&
             CheckMouseIn(pCurMsgBox->GetPos().x, pCurMsgBox->GetPos().y, pCurMsgBox->GetSize().cx,
                          pCurMsgBox->GetSize().cy))
    {
        SendEvent(pCurMsgBox, MSGBOX_EVENT_MOUSE_LBUTTON_DOWN);
        m_EventState = EVENT_WND_MOUSE_LBUTTON_DOWN;

        return false;
    }
    else if (m_EventState == EVENT_WND_MOUSE_LBUTTON_DOWN)
    {
        if (false == MouseLButtonPush &&
            CheckMouseIn(pCurMsgBox->GetPos().x, pCurMsgBox->GetPos().y, pCurMsgBox->GetSize().cx,
                         pCurMsgBox->GetSize().cy))
        {
            SendEvent(pCurMsgBox, MSGBOX_EVENT_MOUSE_LBUTTON_UP);
            m_EventState = EVENT_NONE;

            return false;
        }
        else if (false == MouseLButtonPush || true == MouseLButtonPop)
        {
            m_EventState = EVENT_NONE;
        }
    }
    else if (m_EventState == EVENT_WND_MOUSE_HOVER && MouseRButtonPush &&
             CheckMouseIn(pCurMsgBox->GetPos().x, pCurMsgBox->GetPos().y, pCurMsgBox->GetSize().cx,
                          pCurMsgBox->GetSize().cy))
    {
        SendEvent(pCurMsgBox, MSGBOX_EVENT_MOUSE_RBUTTON_DOWN);
        m_EventState = EVENT_WND_MOUSE_RBUTTON_DOWN;

        return false;
    }
    else if (m_EventState == EVENT_WND_MOUSE_RBUTTON_DOWN)
    {
        if (false == MouseRButtonPush &&
            CheckMouseIn(pCurMsgBox->GetPos().x, pCurMsgBox->GetPos().y, pCurMsgBox->GetSize().cx,
                         pCurMsgBox->GetSize().cy))
        {
            SendEvent(pCurMsgBox, MSGBOX_EVENT_MOUSE_RBUTTON_UP);
            m_EventState = EVENT_NONE;

            return false;
        }
        else if (false == MouseRButtonPush || true == MouseRButtonPop)
        {
            m_EventState = EVENT_NONE;
        }
    }

    if (pCurMsgBox->CanMove() == false)
    {
        return false;
    }

    return true;
}

bool SEASON3B::CNewUIMessageBoxMng::UpdateKeyEvent()
{
    std::sort(m_vecMsgBoxes.begin(), m_vecMsgBoxes.end(), ComparePriority);
    auto vi = m_vecMsgBoxes.begin();
    if (vi == m_vecMsgBoxes.end())
        return true;

    CNewUIMessageBoxBase *pCurMsgBox = (*vi);
    if (IsPress(VK_ESCAPE))
    {
        SendEvent(pCurMsgBox, MSGBOX_EVENT_PRESSKEY_ESC);
    }
    if (IsPress(VK_RETURN))
    {
        SendEvent(pCurMsgBox, MSGBOX_EVENT_PRESSKEY_RETURN);
    }

    if (!IsEmpty())
    {
        return false;
    }

    return true;
}

bool SEASON3B::CNewUIMessageBoxMng::Update()
{
    //. Update
    std::sort(m_vecMsgBoxes.begin(), m_vecMsgBoxes.end(), ComparePriority);
    auto vi = m_vecMsgBoxes.begin();
    if (vi == m_vecMsgBoxes.end())
    {
        return true;
    }
    CNewUIMessageBoxBase *pCurMsgBox = (*vi);
    bool bResult = pCurMsgBox->Update();

    //. Event Processing
    while (!m_queueEvents.empty())
    {
        CNewUIEvent *pEvent = m_queueEvents.front();
        if (pEvent->GetOwner() == pCurMsgBox)
        {
            //. function call
            EVENT_CALLBACK pCallback = pCurMsgBox->GetCallbackFunc(pEvent->GetEvent());
            if (pCallback)
            {
                CALLBACK_RESULT Result = pCallback(pCurMsgBox, pEvent->GetParam());
                if (CALLBACK_BREAK == Result)
                {
                    PopEvent();
                    break;
                }
                if (CALLBACK_POP_ALL_EVENTS == Result)
                {
                    PopAllEvents();
                    break;
                }
            }
            if (pEvent->GetEvent() == MSGBOX_EVENT_DESTROY)
            {
                DeleteMessageBox(pCurMsgBox);
                PopAllEvents();
                break;
            }
        }
        PopEvent();
    }
    return bResult;
}

bool SEASON3B::CNewUIMessageBoxMng::ProcessModernUiInput(const SessionInputEvent &event)
{
    const bool handled =
        !m_vecMsgBoxes.empty() && m_vecMsgBoxes.front()->ProcessModernUiInput(event);
    if (handled && event.action == SessionInputAction::PointerButton && event.pressed &&
        m_pNewUIMng != nullptr)
    {
        m_pNewUIMng->BringToFront(this);
    }
    return handled;
}

std::optional<UI::Modern::RmlTextInputArea> SEASON3B::CNewUIMessageBoxMng::ModernTextInputArea()
    const
{
    return m_vecMsgBoxes.empty() ? std::nullopt : m_vecMsgBoxes.front()->ModernTextInputArea();
}

UI::Modern::PC::SystemMenu::RmlSystemMenuPanel &SEASON3B::CNewUIMessageBoxMng::
    ModernSystemMenuPanel() noexcept
{
    return *m_modernSystemMenu;
}

UI::Modern::RmlMessageBoxPanel &SEASON3B::CNewUIMessageBoxMng::ModernMessageBoxPanel() noexcept
{
    return *m_modernMessageBoxPanel;
}

float SEASON3B::CNewUIMessageBoxMng::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dialog;
}

float SEASON3B::CNewUIMessageBoxMng::GetKeyEventOrder()
{
    return 10.f;
}

bool SEASON3B::CNewUIMessageBoxMng::ComparePriority(CNewUIMessageBoxBase *pObj1,
                                                    CNewUIMessageBoxBase *pObj2)
{
    return pObj1->GetPriority() < pObj2->GetPriority();
}

void SEASON3B::CNewUIMessageBoxMng::DeleteMessageBox(const CNewUIMessageBoxBase *pObj)
{
    if (m_pMsgBoxFactory)
        m_pMsgBoxFactory->DeleteMessageBox(pObj);
    auto vi = m_vecMsgBoxes.begin();
    for (; vi != m_vecMsgBoxes.end(); vi++)
    {
        if ((*vi) == pObj)
        {
            m_vecMsgBoxes.erase(vi);
            break;
        }
    }
}

void SEASON3B::CNewUIMessageBoxMng::PopMessageBox()
{
    if (m_vecMsgBoxes.empty() == false)
    {
        std::sort(m_vecMsgBoxes.begin(), m_vecMsgBoxes.end(), ComparePriority);
        auto vi = m_vecMsgBoxes.begin();
        m_pMsgBoxFactory->DeleteMessageBox((*vi));
        m_vecMsgBoxes.erase(vi);
    }
}

void SEASON3B::CNewUIMessageBoxMng::PopAllMessageBoxes()
{
    m_pMsgBoxFactory->DeleteAllMessageBoxes();
    m_vecMsgBoxes.clear();
}

bool SEASON3B::CNewUIMessageBoxMng::IsEmpty()
{
    return m_vecMsgBoxes.empty();
}

void SEASON3B::CNewUIMessageBoxMng::SendEvent(CNewUIMessageBoxBase *pOwner, DWORD dwEvent)
{
    auto *pEvent = new CNewUIEvent(pOwner, dwEvent);
    m_queueEvents.push(pEvent);
}

void SEASON3B::CNewUIMessageBoxMng::SendEvent(CNewUIMessageBoxBase *pOwner, DWORD dwEvent,
                                              const leaf::xstreambuf &xParam)
{
    auto *pEvent = new CNewUIEvent(pOwner, dwEvent, xParam);
    m_queueEvents.push(pEvent);
}

void SEASON3B::CNewUIMessageBoxMng::PopEvent()
{
    if (!m_queueEvents.empty())
    {
        delete m_queueEvents.front();
        m_queueEvents.pop();
    }
}

void SEASON3B::CNewUIMessageBoxMng::PopAllEvents()
{
    while (!m_queueEvents.empty())
    {
        delete m_queueEvents.front();
        m_queueEvents.pop();
    }
}

SEASON3B::CNewUIMessageBoxMng *CreateSessionMessageBoxManager(SessionKeeper &keeper)
{
    return new SEASON3B::CNewUIMessageBoxMng(keeper);
}

void DestroySessionMessageBoxManager(SEASON3B::CNewUIMessageBoxMng *messageBoxManager) noexcept
{
    delete messageBoxManager;
}

#define SUBGUILDMASTER 64
#define BATTLEMASTER 32

namespace SystemMenu = UI::Modern::PC::SystemMenu;

SEASON3B::CNewUITextInputMsgBox::CNewUITextInputMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), m_ModernMenu(keeper, "storage_password.rml")
{
    m_pInputBox = NULL;
}

SEASON3B::CNewUITextInputMsgBox::~CNewUITextInputMsgBox()
{
    Release();
}

bool SEASON3B::CNewUITextInputMsgBox::Create(DWORD dwMsgBoxType, DWORD dwInputType,
                                             int iInputBoxWidth, int iInputBoxHeight,
                                             int iTextLimit, bool bIsPassword)
{
    m_dwMsgBoxType = dwMsgBoxType;
    m_dwInputType = dwInputType;

    AddCallbackFunc(BindSelf(&SEASON3B::CNewUITextInputMsgBox::LButtonUp),
                    MSGBOX_EVENT_MOUSE_LBUTTON_UP);

    int x, y, width, height;

    x = (SCREEN_WIDTH / 2) - (MSGBOX_WIDTH / 2);
    y = 100;
    width = MSGBOX_WIDTH;
    height = MSGBOX_TOP_HEIGHT + MSGBOX_MIDDLE_HEIGHT + MSGBOX_BOTTOM_HEIGHT;
    if (CNewUIMessageBoxBase::Create(x, y, width, height) == false)
    {
        return false;
    }

    m_pInputBox = new CUITextInputBox(SessionOrigin());

    if (m_pInputBox)
    {
        m_pInputBox->Init(iInputBoxWidth, iInputBoxHeight, iTextLimit, bIsPassword);
        x = GetPos().x + (MSGBOX_WIDTH / 2) - (iInputBoxWidth / 2);
        y = GetPos().y + MSGBOX_TOP_HEIGHT - INPUTBOX_TOP_BLANK;
        m_pInputBox->SetPosition(x, y);
        m_pInputBox->SetTextColor(255, 255, 230, 210);
        m_pInputBox->SetBackColor(255, 0, 0, 0);
        m_pInputBox->SetFont(LegacyFontRole::Normal);
        m_pInputBox->SetState(UISTATE_NORMAL);
        m_pInputBox->GiveFocus();
    }

    SetButtonInfo();

    return true;
}

bool SEASON3B::CNewUITextInputMsgBox::CreateModernNumberInput(DWORD dwMsgBoxType)
{
    m_dwMsgBoxType = dwMsgBoxType;
    m_dwInputType = INPUTBOX_TYPE_NUMBER;
    m_ModernMessage.clear();
    m_UseModernNumberInput = true;

    const int width = UI::Modern::RmlMessageBoxPanel::Width();
    const int height = UI::Modern::RmlMessageBoxPanel::Height();
    const int x = (static_cast<int>(ModernUiViewportWidth()) - width) / 2;
    const int y = (static_cast<int>(ModernUiViewportHeight()) - height) / 2;
    if (!CNewUIMessageBoxBase::Create(x, y, width, height))
    {
        m_UseModernNumberInput = false;
        return false;
    }

    UI::Modern::RmlMessageBoxPanel &panel = g_MessageBox.ModernMessageBoxPanel();
    panel.Create();
    panel.SetMode(UI::Modern::RmlMessageBoxMode::Number);
    panel.SetInputValue(L"");
    panel.FocusInput();
    return true;
}

bool SEASON3B::CNewUITextInputMsgBox::CreateModernPasswordInput(int maxLength)
{
    m_dwMsgBoxType = MSGBOX_COMMON_TYPE_OKCANCEL;
    m_UseModernPasswordInput = true;
    m_ModernMessage.clear();
    m_ModernMenu.ConfigureInput("tiInput", maxLength);
    m_ModernMenu.SetText("btnInputOk-label", I18N::Game::OK);
    m_ModernMenu.SetText("btnInputCancel-label", I18N::Game::Cancel);
    return CNewUIMessageBoxBase::Create(0, 0, 0, 0);
}

void SEASON3B::CNewUITextInputMsgBox::Release()
{
    if (m_UseModernPasswordInput)
    {
        m_ModernMenu.Release();
        m_UseModernPasswordInput = false;
    }
    if (m_UseModernNumberInput)
    {
        g_MessageBox.ModernMessageBoxPanel().Show(false);
        m_ModernMessage.clear();
        m_UseModernNumberInput = false;
    }
    CNewUIMessageBoxBase::Release();

    auto vi = m_MsgTextList.begin();
    for (; vi != m_MsgTextList.end(); vi++)
    {
        SAFE_DELETE(*vi);
    }
    m_MsgTextList.clear();

    SAFE_DELETE(m_pInputBox);

    g_MessageBox.SetRelatedWnd(g_hWnd);
    SetFocus(g_hWnd);
}

CALLBACK_RESULT SEASON3B::CNewUITextInputMsgBox::LButtonUp(class CNewUIMessageBoxBase *pOwner,
                                                           const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CNewUITextInputMsgBox *>(pOwner);
    if (pMsgBox)
    {
        switch (pMsgBox->GetMsgBoxType())
        {
        case MSGBOX_COMMON_TYPE_OK:
            if (pMsgBox->m_BtnOk.IsMouseIn() == true)
            {
                pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_OK);
                return CALLBACK_BREAK;
            }
            break;
        case MSGBOX_COMMON_TYPE_OKCANCEL:
            if (pMsgBox->m_BtnOk.IsMouseIn() == true)
            {
                pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_OK);
                return CALLBACK_BREAK;
            }
            if (pMsgBox->m_BtnCancel.IsMouseIn() == true)
            {
                pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_CANCEL);
                return CALLBACK_BREAK;
            }
            break;
        }
    }

    return CALLBACK_CONTINUE;
}

void SEASON3B::CNewUITextInputMsgBox::AddMsg(const type_string &strMsg, DWORD dwColor,
                                             BYTE byFontType)
{
    if (m_UseModernNumberInput || m_UseModernPasswordInput)
    {
        if (!m_ModernMessage.empty())
        {
            m_ModernMessage += L'\n';
        }
        m_ModernMessage += strMsg;
        if (m_UseModernPasswordInput)
            m_ModernMenu.SetText("taInputMent", m_ModernMessage);
        return;
    }

    int iOrigSize = m_MsgTextList.size();
    int iLine = SeparateText(strMsg, dwColor, byFontType);
    int iSize = m_MsgTextList.size();

    if (iSize > 1)
    {
        float height = GetSize().cy;

        if (iOrigSize < 1)
        {
            iLine = iLine + iOrigSize - 1;
        }

        height += (MSGBOX_MIDDLE_HEIGHT * iLine);
        SetSize(GetSize().cx, height);
        AddButtonBlank(iLine);

        if (m_pInputBox)
        {
            m_pInputBox->SetPosition(m_pInputBox->GetPosition_x(),
                                     m_pInputBox->GetPosition_y() + (iLine * MSGBOX_MIDDLE_HEIGHT));
        }
    }
}

void SEASON3B::CNewUITextInputMsgBox::AddButtonBlank(int iAddLine)
{
    switch (m_dwMsgBoxType)
    {
    case MSGBOX_COMMON_TYPE_OK:
        m_BtnOk.AddBlank(iAddLine * MSGBOX_MIDDLE_HEIGHT);
        break;
    case MSGBOX_COMMON_TYPE_OKCANCEL:
        m_BtnOk.AddBlank(iAddLine * MSGBOX_MIDDLE_HEIGHT);
        m_BtnCancel.AddBlank(iAddLine * MSGBOX_MIDDLE_HEIGHT);
        break;
    }
}

int SEASON3B::CNewUITextInputMsgBox::SeparateText(const type_string &strMsg, DWORD dwColor,
                                                  BYTE byFontType)
{

    SIZE TextSize;
    size_t TextExtentWidth;
    int iLine = 0;

    g_RenderText.MeasureText(strMsg.c_str(), strMsg.size(), &TextSize);
    TextExtentWidth = (size_t)(TextSize.cx / g_fScreenRate_x);

    if (TextExtentWidth <= MSGBOX_TEXT_MAXWIDTH)
    {
        auto *pMsg = new MSGBOX_TEXTDATA;
        pMsg->strMsg = strMsg;
        pMsg->dwColor = dwColor;
        pMsg->byFontType = byFontType;
        m_MsgTextList.push_back(pMsg);

        iLine = 1;
        return iLine;
    }

    type_string strCutText, strRemainText;
    strRemainText = strMsg;

    bool bLoop = true;
    while (bLoop)
    {
        int prev_offset = 0;
        for (int cur_offset = 0; cur_offset < (int)strRemainText.size();)
        {
            prev_offset = cur_offset;
            size_t offset = _mbclen((const unsigned char *)(strRemainText.c_str() + cur_offset));
            cur_offset += offset;

            type_string strTemp(strRemainText, 0, cur_offset /* size */);
            g_RenderText.MeasureText(strTemp.c_str(), strTemp.size(), &TextSize);
            TextExtentWidth = (size_t)(TextSize.cx / g_fScreenRate_x);

            if (TextExtentWidth > MSGBOX_TEXT_MAXWIDTH && cur_offset != 0)
            {
                strCutText = type_string(strRemainText, 0, prev_offset /* size */);
                strRemainText = type_string(strRemainText, prev_offset,
                                            strRemainText.size() - prev_offset /* size */);

                auto *pMsg = new MSGBOX_TEXTDATA;
                pMsg->strMsg = strCutText;
                pMsg->dwColor = dwColor;
                pMsg->byFontType = byFontType;
                m_MsgTextList.push_back(pMsg);
                iLine++;

                g_RenderText.MeasureText(strRemainText.c_str(), strRemainText.size(), &TextSize);
                TextExtentWidth = (size_t)(TextSize.cx / g_fScreenRate_x);

                if (TextExtentWidth <= MSGBOX_TEXT_MAXWIDTH)
                {
                    auto *pMsg = new MSGBOX_TEXTDATA;
                    pMsg->strMsg = strRemainText;
                    pMsg->dwColor = dwColor;
                    pMsg->byFontType = byFontType;
                    m_MsgTextList.push_back(pMsg);
                    iLine++;

                    bLoop = false;
                    break;
                }

                break;
            }
        }
    }

    return iLine;
}

DWORD SEASON3B::CNewUITextInputMsgBox::GetMsgBoxType()
{
    return m_dwMsgBoxType;
}

bool SEASON3B::CNewUITextInputMsgBox::Update()
{
    if (m_UseModernPasswordInput)
    {
        if (m_ModernMenu.TakeClick("btnInputOk"))
            SendEvent(this, MSGBOX_EVENT_USER_COMMON_OK);
        if (m_ModernMenu.TakeClick("btnInputCancel"))
            SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
        return true;
    }
    if (m_UseModernNumberInput)
    {
        return UpdateModernInput();
    }

    switch (m_dwMsgBoxType)
    {
    case MSGBOX_COMMON_TYPE_OK:
        m_BtnOk.Update();
        break;
    case MSGBOX_COMMON_TYPE_OKCANCEL:
        m_BtnOk.Update();
        m_BtnCancel.Update();
        break;
    }

    if (m_pInputBox)
    {
        m_pInputBox->DoAction();

        if (m_pInputBox->HaveFocus() && g_MessageBox.GetRelatedWnd() != m_pInputBox->GetHandle())
        {
            g_MessageBox.SetRelatedWnd(m_pInputBox->GetHandle());
        }
        if (false == m_pInputBox->HaveFocus() && g_MessageBox.GetRelatedWnd() != g_hWnd)
        {
            g_MessageBox.SetRelatedWnd(g_hWnd);
        }
    }

    return true;
}

bool SEASON3B::CNewUITextInputMsgBox::UpdateModernInput()
{
    UI::Modern::RmlMessageBoxPanel &panel = g_MessageBox.ModernMessageBoxPanel();
    if (panel.OkButton().IsClick())
    {
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_OK);
    }
    else if (panel.CancelButton().IsClick())
    {
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
    }
    return true;
}

bool SEASON3B::CNewUITextInputMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    if (m_UseModernPasswordInput)
        return m_ModernMenu.ProcessInput(event);
    return m_UseModernNumberInput && g_MessageBox.ModernMessageBoxPanel().ProcessInput(event);
}

std::optional<UI::Modern::RmlTextInputArea> SEASON3B::CNewUITextInputMsgBox::ModernTextInputArea()
    const
{
    if (m_UseModernPasswordInput)
        return m_ModernMenu.TextInputArea();
    return m_UseModernNumberInput ? g_MessageBox.ModernMessageBoxPanel().TextInputArea()
                                  : std::nullopt;
}

void SEASON3B::CNewUITextInputMsgBox::GetInputBoxText(wchar_t *strText)
{
    if (m_UseModernPasswordInput)
    {
        wcsncpy_s(strText, MAX_TEXT_LENGTH, m_ModernMenu.InputValue().c_str(), _TRUNCATE);
        return;
    }
    if (m_UseModernNumberInput)
    {
        wcsncpy_s(strText, MAX_TEXT_LENGTH,
                  g_MessageBox.ModernMessageBoxPanel().InputValue().c_str(), _TRUNCATE);
        return;
    }
    if (m_pInputBox)
    {
        m_pInputBox->GetText(strText);
    }
}

void SEASON3B::CNewUITextInputMsgBox::SetInputBoxOption(int iOption)
{
    if (m_UseModernNumberInput)
    {
        return;
    }
    if (m_pInputBox)
    {
        m_pInputBox->SetOption(iOption);
    }
}

void SEASON3B::CNewUITextInputMsgBox::SetInputBoxPosition(int x, int y)
{
    if (m_pInputBox)
    {
        m_pInputBox->SetPosition(x, y);
    }
}

void SEASON3B::CNewUITextInputMsgBox::SetInputBoxSize(int width, int height)
{
    if (m_pInputBox)
    {
        m_pInputBox->SetSize(width, height);
    }
}

//////////////////////////////////////////////////////////////////////////

SEASON3B::CNewUIKeyPadMsgBox::CNewUIKeyPadMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), m_ModernMenu(keeper, "storage_keypad.rml")
{
    ClearInput();

    ZeroMemory(m_strCheckKeyPadInput, sizeof(m_strCheckKeyPadInput));

    m_iInputLimit = 0;
}

SEASON3B::CNewUIKeyPadMsgBox::~CNewUIKeyPadMsgBox()
{
    Release();
}

bool SEASON3B::CNewUIKeyPadMsgBox::Create(DWORD dwType, int iInputLimit)
{
    m_iInputLimit = iInputLimit;
    m_dwType = dwType;
    AddCallbackFunc(BindSelf(&CNewUIKeyPadMsgBox::KeyPadBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_KEYPAD_INPUT);
    AddCallbackFunc(BindSelf(&CNewUIKeyPadMsgBox::DeleteBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_KEYPAD_DELETE);
    for (int i = 0; i < MAX_KEYPADINPUT; ++i)
        m_iKeyPadMapping[i] = i;
    // Caution.as MixButtonPlace swaps two authored key positions per iteration.
    for (int i = 0; i < MAX_KEYPADINPUT * 2; ++i)
        std::swap(m_iKeyPadMapping[rand() % MAX_KEYPADINPUT],
                  m_iKeyPadMapping[rand() % MAX_KEYPADINPUT]);
    m_ModernMenu.SetKeypadOrder(m_iKeyPadMapping);
    ClearInput();
    StageModernContent();
    return CNewUIMessageBoxBase::Create(0, 0, 0, 0);
}

void SEASON3B::CNewUIKeyPadMsgBox::Release()
{
    m_ModernMenu.Release();
    CNewUIMessageBoxBase::Release();

    auto vi = m_MsgTextList.begin();
    for (; vi != m_MsgTextList.end(); vi++)
    {
        SAFE_DELETE(*vi);
    }
    m_MsgTextList.clear();
}

CALLBACK_RESULT SEASON3B::CNewUIKeyPadMsgBox::KeyPadBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                            const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CNewUIKeyPadMsgBox *>(pOwner);

    if (pMsgBox)
    {
        if (pMsgBox->GetInputSize() < pMsgBox->GetInputLimit())
        {
            int *pInputNumber = (int *)xParam.data();
            pMsgBox->KeyPadInput(*pInputNumber);
        }
    }

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT SEASON3B::CNewUIKeyPadMsgBox::DeleteBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                            const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CNewUIKeyPadMsgBox *>(pOwner);

    if (pMsgBox)
    {
        pMsgBox->DeleteKeyPadInput();
    }

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT SEASON3B::CNewUIKeyPadMsgBox::Close(class CNewUIMessageBoxBase *pOwner,
                                                    const leaf::xstreambuf &xParam)
{
    return CALLBACK_CONTINUE;
}

void SEASON3B::CNewUIKeyPadMsgBox::AddMsg(const type_string &strMsg, DWORD dwColor, BYTE byFontType)
{
    auto *pMsg = new MSGBOX_TEXTDATA;
    pMsg->strMsg = strMsg;
    pMsg->dwColor = dwColor;
    pMsg->byFontType = byFontType;
    m_MsgTextList.push_back(pMsg);
    StageModernContent();
}

int SEASON3B::CNewUIKeyPadMsgBox::GetInputLimit()
{
    return m_iInputLimit;
}

int SEASON3B::CNewUIKeyPadMsgBox::GetInputSize()
{
    return wcslen(m_strKeyPadInput);
}

void SEASON3B::CNewUIKeyPadMsgBox::ClearInput()
{
    ZeroMemory(m_strKeyPadInput, sizeof(m_strKeyPadInput));
    m_ModernMenu.SetText("tfInput", m_strKeyPadInput);
}

const wchar_t *SEASON3B::CNewUIKeyPadMsgBox::GetInputText()
{
    return m_strKeyPadInput;
}

void SEASON3B::CNewUIKeyPadMsgBox::SetCheckInputText(const wchar_t *strInput)
{
    wcsncpy_s(m_strCheckKeyPadInput, strInput, _TRUNCATE);
}

bool SEASON3B::CNewUIKeyPadMsgBox::IsCheckInput()
{
    return GetInputSize() == m_iInputLimit &&
           wmemcmp(m_strCheckKeyPadInput, m_strKeyPadInput, m_iInputLimit) == 0;
}

void SEASON3B::CNewUIKeyPadMsgBox::SetStoragePassword(WORD wPassword)
{
    m_wStoragePassword = wPassword;
}

WORD SEASON3B::CNewUIKeyPadMsgBox::GetStoragePassword()
{
    return m_wStoragePassword;
}

bool SEASON3B::CNewUIKeyPadMsgBox::IsAllSameNumber()
{
    for (int i = 0; i < m_iInputLimit - 1; ++i)
    {
        if (m_strKeyPadInput[i] != m_strKeyPadInput[i + 1])
        {
            return false;
        }
    }

    return true;
}

void SEASON3B::CNewUIKeyPadMsgBox::KeyPadInput(int iInput)
{
    wchar_t strInput[4] = {
        0,
    };
    mu_swprintf(strInput, L"%d", iInput);
    wcscat(m_strKeyPadInput, strInput);
    m_ModernMenu.SetText("tfInput", m_strKeyPadInput);
}

void SEASON3B::CNewUIKeyPadMsgBox::DeleteKeyPadInput()
{
    int iSize = wcslen(m_strKeyPadInput);
    if (iSize > 0)
    {
        m_strKeyPadInput[iSize - 1] = '\0';
    }
    m_ModernMenu.SetText("tfInput", m_strKeyPadInput);
}

bool SEASON3B::CNewUIKeyPadMsgBox::Update()
{
    for (int digit = 0; digit < MAX_KEYPADINPUT; ++digit)
    {
        if (!m_ModernMenu.TakeClick(("btnNum" + std::to_string(digit)).c_str()))
            continue;
        leaf::xstreambuf parameter;
        parameter << digit;
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_KEYPAD_INPUT, parameter);
    }
    if (m_ModernMenu.TakeClick("btnBackSpace"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_KEYPAD_DELETE);
    if (m_ModernMenu.TakeClick("btnPassOk"))
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_OK);
    if (m_ModernMenu.TakeClick("btnPassCancel"))
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
    return true;
}

//////////////////////////////////////////////////////////////////////////

SEASON3B::CUseFruitCheckMsgBox::CUseFruitCheckMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), panel_(keeper, "fruit_choice.rml")
{
    ZeroMemory(&m_Item, sizeof(m_Item));
}

SEASON3B::CUseFruitCheckMsgBox::~CUseFruitCheckMsgBox()
{
    Release();
}

bool SEASON3B::CUseFruitCheckMsgBox::Create(float fPriority)
{
    SetAddCallbackFunc();
    message_.clear();
    if (!CNewUIMessageBoxBase::Create(0, 0, 0, 0, fPriority))
        return false;
    ITEM *pItem = g_pMyInventory->GetStandbyItem();
    if (pItem == NULL)
    {
        return false;
    }

    Set3DItem(pItem);
    if (g_pNewUI3DRenderMng)
        g_pNewUI3DRenderMng->Add3DRenderObj(this);

    wchar_t strName[50] = {
        0,
    };
    if (pItem->Type == ITEM_FRUITS)
    {
        switch (pItem->Level)
        {
        case 0:
            mu_swprintf(strName, L"%ls", I18N::Game::ENG);
            break;
        case 1:
            mu_swprintf(strName, L"%ls", I18N::Game::STA);
            break;
        case 2:
            mu_swprintf(strName, L"%ls", I18N::Game::AGI);
            break;
        case 3:
            mu_swprintf(strName, L"%ls", I18N::Game::STR);
            break;
        case 4:
            mu_swprintf(strName, L"%ls", I18N::Game::Command);
            break;
        }
    }

    wchar_t strText[128] = {
        0,
    };
    mu_swprintf(strText, L"( %ls%ls )", strName, I18N::Game::Fruit);
    AddMsg(strText, RGBA(255, 255, 0, 255), MSGBOX_FONT_BOLD);
    AddMsg(I18N::Game::Choose, RGBA(255, 255, 0, 255), MSGBOX_FONT_BOLD);

    return true;
}

void SEASON3B::CUseFruitCheckMsgBox::Release()
{
    if (g_pNewUI3DRenderMng)
        g_pNewUI3DRenderMng->Remove3DRenderObj(this);
    panel_.Release();
    message_.clear();
    CNewUIMessageBoxBase::Release();
}

void SEASON3B::CUseFruitCheckMsgBox::AddMsg(const type_string &strMsg, DWORD dwColor,
                                            BYTE byFontType)
{
    if (!message_.empty())
        message_ += L'\n';
    message_ += strMsg;
    panel_.SetText("taFruitMent-label", message_);
}

void SEASON3B::CUseFruitCheckMsgBox::Set3DItem(ITEM *pItem)
{
    if (pItem)
    {
        memcpy(&m_Item, pItem, sizeof(ITEM));
    }
}

bool SEASON3B::CUseFruitCheckMsgBox::Update()
{
    panel_.SetText("btnStatUp-label", I18N::Game::Create);
    panel_.SetText("btnStatDown-label", I18N::Game::Decrease);
    panel_.SetText("btnStatCancel-label", I18N::Game::Cancel);
    if (panel_.TakeClick("btnStatUp"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_USE_FRUIT_ADD);
    else if (panel_.TakeClick("btnStatDown"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_USE_FRUIT_MINUS);
    else if (panel_.TakeClick("btnStatCancel"))
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
    return true;
}

bool SEASON3B::CUseFruitCheckMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return panel_.ProcessInput(event);
}

bool SEASON3B::CUseFruitCheckMsgBox::IsVisible() const
{
    return true;
}

CALLBACK_RESULT SEASON3B::CUseFruitCheckMsgBox::AddBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                           const leaf::xstreambuf &xParam)
{
    BYTE byIndex = g_pMyInventory->GetStandbyItemIndex();
    SendRequestUse(byIndex, 0, true);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CUseFruitCheckMsgBox::MinusBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    BYTE byIndex = g_pMyInventory->GetStandbyItemIndex();
    SendRequestUse(byIndex, 0, false);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CUseFruitCheckMsgBox::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

void SEASON3B::CUseFruitCheckMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(
        [this](CNewUIMessageBoxBase *owner, const leaf::xstreambuf &parameter) {
            return AddBtnDown(owner, parameter);
        },
        MSGBOX_EVENT_USER_CUSTOM_USE_FRUIT_ADD);
    AddCallbackFunc(
        [this](CNewUIMessageBoxBase *owner, const leaf::xstreambuf &parameter) {
            return MinusBtnDown(owner, parameter);
        },
        MSGBOX_EVENT_USER_CUSTOM_USE_FRUIT_MINUS);
    AddCallbackFunc(BindSelf(&SEASON3B::CUseFruitCheckMsgBox::CancelBtnDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
}

//////////////////////////////////////////////////////////////////////////

SEASON3B::CGemIntegrationMsgBox::CGemIntegrationMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), panel_(keeper, "gem_menu.rml") {};

SEASON3B::CGemIntegrationMsgBox::~CGemIntegrationMsgBox()
{
    Release();
};

bool SEASON3B::CGemIntegrationMsgBox::Create(float fPriority)
{
    SetAddCallbackFunc();
    locale_.clear();
    StageContent();
    return CNewUIMessageBoxBase::Create(0, 0, 0, 0, fPriority);
}

void SEASON3B::CGemIntegrationMsgBox::Release()
{
    panel_.Release();
    CNewUIMessageBoxBase::Release();
}

bool SEASON3B::CGemIntegrationMsgBox::Update()
{
    StageContent();
    if (panel_.TakeClick("btnSelectAttach"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_GEM_UNITY);
    else if (panel_.TakeClick("btnSelectDetach"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_GEM_DISJOINT);
    else if (panel_.TakeClick("btnSelectClose"))
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
    return true;
}

void SEASON3B::CGemIntegrationMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&SEASON3B::CGemIntegrationMsgBox::UnityBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_GEM_UNITY);
    AddCallbackFunc(BindSelf(&SEASON3B::CGemIntegrationMsgBox::DisjointBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_GEM_DISJOINT);
    AddCallbackFunc(BindSelf(&SEASON3B::CGemIntegrationMsgBox::CancelBtnDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
}

CALLBACK_RESULT SEASON3B::CGemIntegrationMsgBox::UnityBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    SetMode(COMGEM::ATTACH);

    SEASON3B::CreateMessageBox(
        MSGBOX_LAYOUT_CLASS(CGemIntegrationUnityMsgBoxLayout, pOwner->MessageBoxSessionOrigin()));

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGemIntegrationMsgBox::DisjointBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                 const leaf::xstreambuf &xParam)
{
    SetMode(COMGEM::DETACH);

    if (!pOwner->MessageBoxUiOrigin().FindWantedList())
    {
        g_pSystemLogBox->AddText(I18N::Game::CanTBeDismantled, SEASON3B::TYPE_ERROR_MESSAGE);
        return CALLBACK_BREAK;
    }

    SEASON3B::CreateMessageBox(MSGBOX_LAYOUT_CLASS(CGemIntegrationDisjointMsgBoxLayout,
                                                   pOwner->MessageBoxSessionOrigin()));

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGemIntegrationMsgBox::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                               const leaf::xstreambuf &xParam)
{
    SessionOrigin().Gameplay()->Exit();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

SEASON3B::CGemIntegrationUnityMsgBox::CGemIntegrationUnityMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), panel_(keeper, "gem_attach.rml"),
      unitPanel_(keeper, "gem_unit.rml")
{
}

SEASON3B::CGemIntegrationUnityMsgBox::~CGemIntegrationUnityMsgBox()
{
    Release();
}

bool SEASON3B::CGemIntegrationUnityMsgBox::Create(float fPriority)
{
    SetAddCallbackFunc();
    locale_.clear();
    StageContent();
    return CNewUIMessageBoxBase::Create(0, 0, 0, 0, fPriority);
}

void SEASON3B::CGemIntegrationUnityMsgBox::Release()
{
    panel_.Release();
    unitPanel_.Release();
    CNewUIMessageBoxBase::Release();
}

bool SEASON3B::CGemIntegrationUnityMsgBox::Update()
{
    StageContent();
    if (m_cGemType == COMGEM::eNOGEM)
    {
        if (panel_.TakeClick("btnAttachClose"))
        {
            SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
            return true;
        }
        for (int i = 0; i < COMGEM::eGEMTYPE_END; ++i)
        {
            if (!panel_.TakeClick(("btnAttach" + std::to_string(i + 1)).c_str()))
                continue;
            SetGem(i * 2);
            break;
        }
    }
    else
    {
        if (unitPanel_.TakeClick("btnUnitClose"))
        {
            SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
            return true;
        }
        for (int i = 0; i < COMGEM::eCOMTYPE_END; ++i)
        {
            if (!unitPanel_.TakeClick(("btnUnit" + std::to_string(i + 1)).c_str()))
                continue;
            m_cComType = GetJewelRequireCount(i);
            SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_GEM_SELECTMIX);
            break;
        }
    }
    return true;
}

void SEASON3B::CGemIntegrationUnityMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&SEASON3B::CGemIntegrationUnityMsgBox::CancelBtnDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
    AddCallbackFunc(BindSelf(&SEASON3B::CGemIntegrationUnityMsgBox::SelectMixBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_GEM_SELECTMIX);
}

CALLBACK_RESULT SEASON3B::CGemIntegrationUnityMsgBox::SelectMixBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    if (!sessionUi_.CheckInv())
    {
        return CALLBACK_BREAK;
    }

    SEASON3B::CNewUICommonMessageBox *pMsgBox = NULL;
    SEASON3B::CreateMessageBox(MSGBOX_LAYOUT_CLASS(SEASON3B::CGemIntegrationUnityCheckMsgBoxLayout,
                                                   pOwner->MessageBoxSessionOrigin()),
                               &pMsgBox);
    if (pMsgBox)
    {
        wchar_t strText[256] = {
            0,
        };
        mu_swprintf(strText, I18N::Game::Lookup(GetJewelIndex(m_cGemType, 0)),
                    I18N::Game::JewelOfSoul, m_cCount);
        pMsgBox->AddMsg(strText, CLRDW_YELLOW, MSGBOX_FONT_BOLD);

        mu_swprintf(strText, I18N::Game::CombinationCostDZen, m_iValue);
        pMsgBox->AddMsg(strText, CLRDW_YELLOW, MSGBOX_FONT_BOLD);
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGemIntegrationUnityMsgBox::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    SessionOrigin().Gameplay()->Exit();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

SEASON3B::CGemIntegrationDisjointMsgBox::CGemIntegrationDisjointMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), panel_(keeper, "gem_detach.rml")
{
    panel_.ConfigureList("scrollList", "sbList", "gem-row");
}

SEASON3B::CGemIntegrationDisjointMsgBox::~CGemIntegrationDisjointMsgBox()
{
    Release();
}

bool SEASON3B::CGemIntegrationDisjointMsgBox::Create(float fPriority)
{
    SetAddCallbackFunc();
    locale_.clear();
    StageContent();
    return CNewUIMessageBoxBase::Create(0, 0, 0, 0, fPriority);
}

void SEASON3B::CGemIntegrationDisjointMsgBox::Release()
{
    panel_.Release();
    CNewUIMessageBoxBase::Release();
}

bool SEASON3B::CGemIntegrationDisjointMsgBox::Update()
{
    StageContent();
    if (auto selected = panel_.TakeListSelection())
        sessionUi_.UnmixGemList().SLSetSelectLine(static_cast<int>(*selected) + 1);
    panel_.SetButtonEnabled("btnDetachRun", sessionUi_.UnmixGemList().GetSelectedText() != nullptr);
    if (panel_.TakeClick("btnDetachClose"))
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
    else if (panel_.TakeClick("btnDetachRun"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_GEM_DISJOINT_DISJOINT);
    return true;
}

void SEASON3B::CGemIntegrationDisjointMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&SEASON3B::CGemIntegrationDisjointMsgBox::DisjointBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_GEM_DISJOINT_DISJOINT);
    AddCallbackFunc(BindSelf(&SEASON3B::CGemIntegrationDisjointMsgBox::CancelBtnDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
}

CALLBACK_RESULT SEASON3B::CGemIntegrationDisjointMsgBox::DisjointBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    UNMIX_TEXT *pUT = pOwner->MessageBoxUiOrigin().UnmixGemList().GetSelectedText();
    if (pUT)
    {
        const ITEM *pItem = FindInventoryItemBySlot(pUT->m_iInvenIdx);
        if (pItem == nullptr)
        {
            return CALLBACK_BREAK;
        }

        sessionUi_.SelectFromList(pUT->m_iInvenIdx, pUT->m_cLevel);

        SEASON3B::CNewUICommonMessageBox *pMsgBox = NULL;
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CGemIntegrationDisjointCheckMsgBoxLayout,
                                pOwner->MessageBoxSessionOrigin()),
            &pMsgBox);

        if (pMsgBox)
        {
            wchar_t strText[256] = {
                0,
            };
            int iGemLevel = GetUnMixGemLevel() + 1;
            int nIdx = Check_Jewel(pItem->Type);
            SetGem(nIdx);
            mu_swprintf(strText, I18N::Game::AreYouSureToDisbandSD,
                        I18N::Game::Lookup(GetJewelIndex(nIdx, COMGEM::eGEM_NAME)), iGemLevel);

            pMsgBox->AddMsg(strText, CLRDW_DARKYELLOW, MSGBOX_FONT_BOLD);
            mu_swprintf(strText, I18N::Game::DissolvingCostDZen, m_iValue);
            pMsgBox->AddMsg(strText, CLRDW_DARKYELLOW, MSGBOX_FONT_BOLD);
        }
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGemIntegrationDisjointMsgBox::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    SessionOrigin().Gameplay()->Exit();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

//////////////////////////////////////////////////////////////////////////

SEASON3B::CSystemMenuMsgBox::CSystemMenuMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), crywolf_(MessageBoxMapProcessOrigin().Crywolf1st()),
      muHelper_(MuHelperForConstruction())
{
}

SEASON3B::CSystemMenuMsgBox::~CSystemMenuMsgBox()
{
    Release();
}

MUHelper::SessionMuHelperUnit &SEASON3B::CSystemMenuMsgBox::MuHelper() const noexcept
{
    return muHelper_;
}

CErrorReport &SEASON3B::CSystemMenuMsgBox::ErrorReport() const noexcept
{
    return g_ErrorReport;
}

CmuConsoleDebug &SEASON3B::CSystemMenuMsgBox::ConsoleDebug() const noexcept
{
    return g_ConsoleDebug;
}

bool SEASON3B::CSystemMenuMsgBox::Create(float fPriority)
{
    SetAddCallbackFunc();
    SystemMenu::RmlSystemMenuPanel &modernPanel = g_MessageBox.ModernSystemMenuPanel();
    modernPanel.Create(SystemMenu::RmlSystemMenuMode::Game);
    const int width = SystemMenu::RmlSystemMenuPanel::Width();
    const int height =
        SystemMenu::RmlSystemMenuPanel::HeightFor(SystemMenu::RmlSystemMenuMode::Game);
    const int x = (static_cast<int>(ModernUiViewportWidth()) - width) / 2;
    const int y = (static_cast<int>(ModernUiViewportHeight()) - height) / 2;
    CNewUIMessageBoxBase::Create(x, y, width, height, fPriority);
    modernPanel.SetPosition(x, y);
    modernPanel.Show(true);
    return true;
}

void SEASON3B::CSystemMenuMsgBox::Release()
{
    g_MessageBox.ModernSystemMenuPanel().Show(false);
    CNewUIMessageBoxBase::Release();
}

bool SEASON3B::CSystemMenuMsgBox::Update()
{
    SystemMenu::RmlSystemMenuPanel &modernPanel = g_MessageBox.ModernSystemMenuPanel();
    if (modernPanel.ExitButton().IsClick())
    {
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_SYSTEMMENU_GAMEOVER);
    }
    else if (modernPanel.ServerButton().IsClick())
    {
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_SYSTEMMENU_CHOOSESERVER);
    }
    else if (modernPanel.CharacterButton().IsClick())
    {
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_SYSTEMMENU_CHOOSECHARACTER);
    }
    else if (modernPanel.CloseButton().IsClick())
    {
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
    }
    return true;
}

bool SEASON3B::CSystemMenuMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return g_MessageBox.ModernSystemMenuPanel().ProcessInput(event);
}

void SEASON3B::CSystemMenuMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&SEASON3B::CSystemMenuMsgBox::GameOverBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_SYSTEMMENU_GAMEOVER);
    AddCallbackFunc(BindSelf(&SEASON3B::CSystemMenuMsgBox::ChooseServerBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_SYSTEMMENU_CHOOSESERVER);
    AddCallbackFunc(BindSelf(&SEASON3B::CSystemMenuMsgBox::ChooseCharacterBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_SYSTEMMENU_CHOOSECHARACTER);
    AddCallbackFunc(BindSelf(&SEASON3B::CSystemMenuMsgBox::CancelBtnDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
    AddCallbackFunc(BindSelf(&SEASON3B::CSystemMenuMsgBox::CancelBtnDown),
                    MSGBOX_EVENT_PRESSKEY_ESC);
}

CALLBACK_RESULT SEASON3B::CSystemMenuMsgBox::GameOverBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    auto *messageBox = static_cast<CSystemMenuMsgBox *>(pOwner);
    messageBox->ErrorReport().Write(L"> Menu - Exit game. ");
    messageBox->ErrorReport().WriteCurrentTime();

    SaveOptions();
    SaveMacro(L"Data\\Macro.txt");

    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_MIXINVENTORY))
    {
        g_pSystemLogBox->AddText(I18N::Game::ExitGameAfterClosingTheChaosInterface,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
    }
    else
    {
        messageBox->MuHelper().TriggerStop();
        LogOut = true;
        SocketClient->ToGameServer()->SendLogOut(LogOutType::CloseGame);
        messageBox->ConsoleDebug().Write(MCD_SEND, L"0xF1 [SendRequestLogOut] 0");
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CSystemMenuMsgBox::ChooseServerBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                 const leaf::xstreambuf &xParam)
{
    auto *messageBox = static_cast<CSystemMenuMsgBox *>(pOwner);
    messageBox->crywolf_.View_End_Result = false;
    messageBox->crywolf_.Suc_Or_Fail = -1;
    messageBox->crywolf_.CryWolfMVPInit();

    messageBox->ErrorReport().Write(L"> Menu - Join another server. ");
    messageBox->ErrorReport().WriteCurrentTime();

    SaveOptions();
    SaveMacro(L"Data\\Macro.txt");

    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_MIXINVENTORY))
    {
        g_pSystemLogBox->AddText(I18N::Game::ExitGameAfterClosingTheChaosInterface,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
    }
    else
    {
        messageBox->MuHelper().TriggerStop();
        g_pNewUIMng->ResetActiveUIObj();
        LogOut = true;
        SocketClient->ToGameServer()->SendLogOut(LogOutType::BackToServerSelection);
        messageBox->ConsoleDebug().Write(MCD_SEND, L"0xF1 [SendRequestLogOut] 2");
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CSystemMenuMsgBox::ChooseCharacterBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    auto *messageBox = static_cast<CSystemMenuMsgBox *>(pOwner);
    messageBox->crywolf_.View_End_Result = false;
    messageBox->crywolf_.Suc_Or_Fail = -1;
    messageBox->crywolf_.CryWolfMVPInit();

    messageBox->ErrorReport().Write(L"> Menu - Join with another character. ");
    messageBox->ErrorReport().WriteCurrentTime();

    //  ????? ??? ??? ??.
    SaveOptions();
    SaveMacro(L"Data\\Macro.txt");

    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_MIXINVENTORY))
    {
        g_pSystemLogBox->AddText(I18N::Game::ExitGameAfterClosingTheChaosInterface,
                                 SEASON3B::TYPE_SYSTEM_MESSAGE);
    }
    else
    {
        messageBox->MuHelper().TriggerStop();
        g_pNewUIMng->ResetActiveUIObj();
        LogOut = true;
        SocketClient->ToGameServer()->SendLogOut(LogOutType::BackToCharacterSelection);
        messageBox->ConsoleDebug().Write(MCD_SEND, L"0xF1 [SendRequestLogOut] 1");
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CSystemMenuMsgBox::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                           const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

SEASON3B::CBloodCastleResultMsgBox::CBloodCastleResultMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper)
{
}

SEASON3B::CBloodCastleResultMsgBox::~CBloodCastleResultMsgBox()
{
}

bool SEASON3B::CBloodCastleResultMsgBox::Create(float fPriority)
{
    int x, y, width, height;

    AddCallbackFunc(BindSelf(&SEASON3B::CBloodCastleResultMsgBox::LButtonUp),
                    MSGBOX_EVENT_MOUSE_LBUTTON_UP);
    AddCallbackFunc(BindSelf(&SEASON3B::CBloodCastleResultMsgBox::OkBtnDown),
                    MSGBOX_EVENT_USER_COMMON_OK);

    x = (SCREEN_WIDTH / 2) - (MSGBOX_WIDTH / 2);
    y = 100;
    width = MSGBOX_WIDTH;
    height = MSGBOX_TOP_HEIGHT + (MIDDLE_COUNT * MSGBOX_MIDDLE_HEIGHT) + MSGBOX_BOTTOM_HEIGHT;

    CNewUIMessageBoxBase::Create(x, y, width, height, fPriority);

    x = GetPos().x + (GetSize().cx / 2) - (MSGBOX_BTN_WIDTH / 2);
    y = GetPos().y + GetSize().cy - (MSGBOX_BTN_HEIGHT + MSGBOX_BTN_BOTTOM_BLANK);
    width = MSGBOX_BTN_WIDTH;
    height = MSGBOX_BTN_HEIGHT;
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    m_BtnOk.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_OK, x, y, width, height,
                    CNewUIMessageBoxButton::MSGBOX_BTN_SIZE_OK);
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
    m_BtnOk.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_OK, x, y, width, height);
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

    return true;
}

bool SEASON3B::CBloodCastleResultMsgBox::Update()
{
    m_BtnOk.Update();

    return true;
}

CALLBACK_RESULT SEASON3B::CBloodCastleResultMsgBox::LButtonUp(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CBloodCastleResultMsgBox *>(pOwner);
    if (pMsgBox)
    {
        if (pMsgBox->m_BtnOk.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_OK);
            return CALLBACK_BREAK;
        }
    }

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT SEASON3B::CBloodCastleResultMsgBox::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

SEASON3B::CDevilSquareRankMsgBox::CDevilSquareRankMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper)
{
}

SEASON3B::CDevilSquareRankMsgBox::~CDevilSquareRankMsgBox()
{
}

bool SEASON3B::CDevilSquareRankMsgBox::Create(float fPriority)
{
    int x, y, width, height;

    AddCallbackFunc(BindSelf(&SEASON3B::CDevilSquareRankMsgBox::LButtonUp),
                    MSGBOX_EVENT_MOUSE_LBUTTON_UP);
    AddCallbackFunc(BindSelf(&SEASON3B::CDevilSquareRankMsgBox::OkBtnDown),
                    MSGBOX_EVENT_USER_COMMON_OK);

    x = (SCREEN_WIDTH / 2) - (MSGBOX_WIDTH / 2);
    y = 60;
    width = MSGBOX_WIDTH;
    height = MSGBOX_TOP_HEIGHT + (MIDDLE_COUNT1 * MSGBOX_MIDDLE_HEIGHT) +
             (MIDDLE_COUNT2 * MSGBOX_MIDDLE_HEIGHT) + MSGBOX_LINE_HEIGHT + MSGBOX_BOTTOM_HEIGHT;

    CNewUIMessageBoxBase::Create(x, y, width, height, fPriority);

    x = GetPos().x + (GetSize().cx / 2) - (MSGBOX_BTN_WIDTH / 2);
    y = GetPos().y + GetSize().cy - (MSGBOX_BTN_HEIGHT + MSGBOX_BTN_BOTTOM_BLANK);
    width = MSGBOX_BTN_WIDTH;
    height = MSGBOX_BTN_HEIGHT;
    m_BtnOk.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_OK, x, y, width, height);

    return true;
}

bool SEASON3B::CDevilSquareRankMsgBox::Update()
{
    m_BtnOk.Update();
    gameplay_.SetPosition(GetPos().x, GetPos().y);

    return true;
}

CALLBACK_RESULT SEASON3B::CDevilSquareRankMsgBox::LButtonUp(class CNewUIMessageBoxBase *pOwner,
                                                            const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CDevilSquareRankMsgBox *>(pOwner);
    if (pMsgBox)
    {
        if (pMsgBox->m_BtnOk.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_OK);
            return CALLBACK_BREAK;
        }
    }

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT SEASON3B::CDevilSquareRankMsgBox::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                            const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

SEASON3B::CChaosCastleResultMsgBox::CChaosCastleResultMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper)
{
}

SEASON3B::CChaosCastleResultMsgBox::~CChaosCastleResultMsgBox()
{
}

bool SEASON3B::CChaosCastleResultMsgBox::Create(float fPriority)
{
    int x, y, width, height;

    AddCallbackFunc(BindSelf(&SEASON3B::CChaosCastleResultMsgBox::LButtonUp),
                    MSGBOX_EVENT_MOUSE_LBUTTON_UP);
    AddCallbackFunc(BindSelf(&SEASON3B::CChaosCastleResultMsgBox::OkBtnDown),
                    MSGBOX_EVENT_USER_COMMON_OK);

    x = (SCREEN_WIDTH / 2) - (MSGBOX_WIDTH / 2);
    y = 100;
    width = MSGBOX_WIDTH;
    height = MSGBOX_TOP_HEIGHT + (MIDDLE_COUNT * MSGBOX_MIDDLE_HEIGHT) + MSGBOX_BOTTOM_HEIGHT;

    CNewUIMessageBoxBase::Create(x, y, width, height, fPriority);

    x = GetPos().x + (GetSize().cx / 2) - (MSGBOX_BTN_WIDTH / 2);
    y = GetPos().y + GetSize().cy - (MSGBOX_BTN_HEIGHT + MSGBOX_BTN_BOTTOM_BLANK);
    width = MSGBOX_BTN_WIDTH;
    height = MSGBOX_BTN_HEIGHT;
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    m_BtnOk.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_OK, x, y, width, height,
                    CNewUIMessageBoxButton::MSGBOX_BTN_SIZE_OK);
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
    m_BtnOk.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_OK, x, y, width, height);
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

    return true;
}

bool SEASON3B::CChaosCastleResultMsgBox::Update()
{
    m_BtnOk.Update();

    return true;
}

CALLBACK_RESULT SEASON3B::CChaosCastleResultMsgBox::LButtonUp(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CChaosCastleResultMsgBox *>(pOwner);
    if (pMsgBox)
    {
        if (pMsgBox->m_BtnOk.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_OK);
            return CALLBACK_BREAK;
        }
    }

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT SEASON3B::CChaosCastleResultMsgBox::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    return CALLBACK_BREAK;
}

//////////////////////////////////////////////////////////////////////////

SEASON3B::CChaosMixMenuMsgBox::CChaosMixMenuMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), m_ModernMenu(keeper, "chaos_menu.rml")
{
}

SEASON3B::CChaosMixMenuMsgBox::~CChaosMixMenuMsgBox()
{
    Release();
}

bool SEASON3B::CChaosMixMenuMsgBox::Create(float fPriority)
{
    SetAddCallbackFunc();
    StageModernContent();
    return CNewUIMessageBoxBase::Create(0, 0, 0, 0, fPriority);
}

void SEASON3B::CChaosMixMenuMsgBox::Release()
{
    m_ModernMenu.Release();
    CNewUIMessageBoxBase::Release();
}

bool SEASON3B::CChaosMixMenuMsgBox::Update()
{
    StageModernContent();
    if (m_ModernMenu.TakeClick("btnType1"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_MIXMENU_GENERALMIX);
    if (m_ModernMenu.TakeClick("btnType2"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_MIXMENU_CHAOSMIX);
    if (m_ModernMenu.TakeClick("btnType3"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_MIXMENU_MIX380);
    if (m_ModernMenu.TakeClick("btnClose"))
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
    return true;
}

CALLBACK_RESULT SEASON3B::CChaosMixMenuMsgBox::GeneralMixBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                 const leaf::xstreambuf &xParam)
{
    g_MixRecipeMgr.SetMixType(0);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CChaosMixMenuMsgBox::ChaosMixBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                               const leaf::xstreambuf &xParam)
{
    g_MixRecipeMgr.SetMixType(1);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CChaosMixMenuMsgBox::Mix380BtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    g_MixRecipeMgr.SetMixType(2);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CChaosMixMenuMsgBox::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    g_MixRecipeMgr.ClearCheckRecipeResult();
    g_pNewUISystem->Hide(SEASON3B::INTERFACE_MIXINVENTORY);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

void SEASON3B::CChaosMixMenuMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&SEASON3B::CChaosMixMenuMsgBox::GeneralMixBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_MIXMENU_GENERALMIX);
    AddCallbackFunc(BindSelf(&SEASON3B::CChaosMixMenuMsgBox::ChaosMixBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_MIXMENU_CHAOSMIX);
    AddCallbackFunc(BindSelf(&SEASON3B::CChaosMixMenuMsgBox::Mix380BtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_MIXMENU_MIX380);
    AddCallbackFunc(BindSelf(&SEASON3B::CChaosMixMenuMsgBox::CancelBtnDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
    AddCallbackFunc(BindSelf(&SEASON3B::CChaosMixMenuMsgBox::CancelBtnDown),
                    MSGBOX_EVENT_PRESSKEY_ESC);
}

SEASON3B::CProgressMsgBox::CProgressMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), panel_(keeper)
{
    m_dwStartTime = 0;
    m_dwEndTime = 0;
    m_dwElapseTime = 0;
}

SEASON3B::CProgressMsgBox::~CProgressMsgBox()
{
    Release();
}

bool SEASON3B::CProgressMsgBox::Create(DWORD duration, float priority)
{
    SetAddCallbackFunc();
    m_dwElapseTime = duration;
    m_dwStartTime = timeGetTime();
    m_dwEndTime = m_dwStartTime + duration;
    elapsed_ = 0;
    finished_ = false;
    message_.clear();
    SetCanMove(true);
    return CNewUIMessageBoxBase::Create(0, 0, 0, 0, priority);
}

void SEASON3B::CProgressMsgBox::Release()
{
    panel_.Release();
    message_.clear();
    CNewUIMessageBoxBase::Release();
}

void SEASON3B::CProgressMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&SEASON3B::CProgressMsgBox::ClosingProcess),
                    MSGBOX_EVENT_USER_CUSTOM_PROGRESS_CLOSINGPROCESS);
}

void SEASON3B::CProgressMsgBox::AddMsg(const type_string &message, DWORD, BYTE)
{
    if (!message_.empty())
        message_ += L'\n';
    message_ += message;
}

void SEASON3B::CProgressMsgBox::SetElapseTime(DWORD dwElapseTime)
{
    elapsed_ = 0;
    finished_ = false;
    m_dwElapseTime = dwElapseTime;
    m_dwStartTime = timeGetTime();
    m_dwEndTime = m_dwStartTime + m_dwElapseTime;
}

bool SEASON3B::CProgressMsgBox::Update()
{
    g_pMainFrame->UpdateItemHotKey();
    elapsed_ = timeGetTime() - m_dwStartTime;
    if (!finished_ && elapsed_ >= m_dwElapseTime)
    {
        finished_ = true;
        g_MessageBox.SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_PROGRESS_CLOSINGPROCESS);
    }
    return true;
}

bool SEASON3B::CProgressMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return panel_.ProcessInput(event);
}

CALLBACK_RESULT SEASON3B::CProgressMsgBox::ClosingProcess(class CNewUIMessageBoxBase *pOwner,
                                                          const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_CONTINUE;
}

SEASON3B::CCursedTempleProgressMsgBox::CCursedTempleProgressMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), panel_(keeper), cursedTemple_(keeper.CursedTempleObject())
{
    m_dwStartTime = 0;
    m_dwEndTime = 0;
    m_dwElapseTime = 0;
}

SEASON3B::CCursedTempleProgressMsgBox::~CCursedTempleProgressMsgBox()
{
    Release();
}

bool SEASON3B::CCursedTempleProgressMsgBox::Create(DWORD duration, float priority)
{
    SetAddCallbackFunc();
    m_dwElapseTime = duration;
    m_dwStartTime = timeGetTime();
    m_dwEndTime = m_dwStartTime + duration;
    elapsed_ = 0;
    finished_ = false;
    message_.clear();
    SetCanMove(true);
    return CNewUIMessageBoxBase::Create(0, 0, 0, 0, priority);
}

void SEASON3B::CCursedTempleProgressMsgBox::Release()
{
    panel_.Release();
    message_.clear();
    CNewUIMessageBoxBase::Release();
}

void SEASON3B::CCursedTempleProgressMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&SEASON3B::CCursedTempleProgressMsgBox::ClosingProcess),
                    MSGBOX_EVENT_USER_CUSTOM_PROGRESS_CLOSINGPROCESS);
    AddCallbackFunc(BindSelf(&SEASON3B::CCursedTempleProgressMsgBox::CompleteProcess),
                    MSGBOX_EVENT_USER_CUSTOM_PROGRESS_COMPLETEPROCESS);
}

void SEASON3B::CCursedTempleProgressMsgBox::AddMsg(const type_string &message, DWORD, BYTE)
{
    if (!message_.empty())
        message_ += L'\n';
    message_ += message;
}

bool SEASON3B::CCursedTempleProgressMsgBox::Update()
{
    g_pMainFrame->UpdateItemHotKey();
    elapsed_ = timeGetTime() - m_dwStartTime;
    if (finished_)
        return true;
    if (!CheckHeroAction() || !g_pNewUISystem->IsVisible(INTERFACE_CURSEDTEMPLE_GAMESYSTEM))
    {
        finished_ = true;
        g_MessageBox.SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_PROGRESS_CLOSINGPROCESS);
    }
    else if (elapsed_ >= m_dwElapseTime)
    {
        finished_ = true;
        g_MessageBox.SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_PROGRESS_COMPLETEPROCESS);
    }
    return true;
}

void SEASON3B::CCursedTempleProgressMsgBox::SetNpcIndex(DWORD dwIndex)
{
    m_dwNpcIndex = dwIndex;
}

DWORD SEASON3B::CCursedTempleProgressMsgBox::GetNpcIndex()
{
    return m_dwNpcIndex;
}

CALLBACK_RESULT SEASON3B::CCursedTempleProgressMsgBox::ClosingProcess(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CCursedTempleProgressMsgBox *>(pOwner);
    if (pMsgBox == nullptr)
    {
        return CALLBACK_CONTINUE;
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    pMsgBox->cursedTemple_.SetGaugebarEnabled(false);
    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT SEASON3B::CCursedTempleProgressMsgBox::CompleteProcess(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CCursedTempleProgressMsgBox *>(pOwner);
    if (pMsgBox == nullptr)
    {
        return CALLBACK_CONTINUE;
    }

    SocketClient->ToGameServer()->SendTalkToNpcRequest(pMsgBox->GetNpcIndex());

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    pMsgBox->cursedTemple_.SetGaugebarCloseTimer();
    return CALLBACK_CONTINUE;
}

bool SEASON3B::CCursedTempleProgressMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return panel_.ProcessInput(event);
}

bool SEASON3B::CCursedTempleProgressMsgBox::CheckHeroAction()
{
    if (g_isCharacterBuff((&Hero->Object), eDeBuff_Harden) ||
        g_isCharacterBuff((&Hero->Object), eDeBuff_Stun) ||
        g_isCharacterBuff((&Hero->Object), eDeBuff_CursedTempleRestraint) ||
        g_isCharacterBuff((&Hero->Object), eDeBuff_Sleep))
    {
        return false;
    }

    int action = Hero->Object.CurrentAction;

    if (!(action >= PLAYER_SET && action <= PLAYER_STOP_RIDE_WEAPON) && !(action == PLAYER_SHOCK) &&
        !(action == PLAYER_FENRIR_STAND) && !(action == PLAYER_FENRIR_STAND_TWO_SWORD) &&
        !(action == PLAYER_FENRIR_STAND_ONE_RIGHT) && !(action == PLAYER_FENRIR_STAND_ONE_LEFT) &&
        !(action == PLAYER_DARKLORD_STAND) && !(action == PLAYER_STOP_RIDE_HORSE) &&
        !(action == PLAYER_ATTACK_STRIKE) && !(action == PLAYER_STOP_TWO_HAND_SWORD_TWO) &&
        !(action >= PLAYER_RAGE_FENRIR_STAND && action <= PLAYER_RAGE_FENRIR_STAND_ONE_LEFT) &&
        !(action == PLAYER_RAGE_UNI_STOP_ONE_RIGHT))
    {
        return false;
    }

    return true;
}

SEASON3B::CDuelMsgBox::CDuelMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), panel_(keeper, "duel_confirm.rml", "Combat")
{
}

SEASON3B::CDuelMsgBox::~CDuelMsgBox()
{
    Release();
}

bool SEASON3B::CDuelMsgBox::Create(float fPriority)
{
    SetAddCallbackFunc();
    return CNewUIMessageBoxBase::Create(0, 0, 0, 0, fPriority);
}

void SEASON3B::CDuelMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&SEASON3B::CDuelMsgBox::OkBtnDown), MSGBOX_EVENT_USER_CUSTOM_DUEL_OK);
    AddCallbackFunc(BindSelf(&SEASON3B::CDuelMsgBox::CancelBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_DUEL_CANCEL);
    AddCallbackFunc(BindSelf(&SEASON3B::CDuelMsgBox::CancelBtnDown), MSGBOX_EVENT_PRESSKEY_ESC);
}

void SEASON3B::CDuelMsgBox::Release()
{
    panel_.Release();
    CNewUIMessageBoxBase::Release();
}

bool SEASON3B::CDuelMsgBox::Update()
{
    wchar_t challenger[256]{};
    mu_swprintf(challenger, L"[%ls]", g_DuelMgr.GetDuelPlayerID(DUEL_ENEMY));
    panel_.SetText("tfTitle-label", challenger);
    panel_.SetText("taDuelInfo-label",
                   std::wstring(I18N::Game::YouAreChallengedToADuel) + L"\n" +
                       std::wstring(I18N::Game::WouldYouLikeToAcceptTheChallenge));
    panel_.SetText("btnOk-label", I18N::Game::OK);
    panel_.SetText("btnCancel-label", I18N::Game::Cancel);
    if (panel_.TakeClick("btnOk"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_DUEL_OK);
    else if (panel_.TakeClick("btnCancel"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_DUEL_CANCEL);
    return true;
}

bool SEASON3B::CDuelMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return panel_.ProcessInput(event);
}
CALLBACK_RESULT SEASON3B::CDuelMsgBox::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                 const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendDuelStartResponse(
        TRUE, g_DuelMgr.GetDuelPlayerIndex(DUEL_ENEMY), g_DuelMgr.GetDuelPlayerID(DUEL_ENEMY));
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    PlayBuffer(SOUND_CLICK01);
    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT SEASON3B::CDuelMsgBox::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                     const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendDuelStartResponse(
        FALSE, g_DuelMgr.GetDuelPlayerIndex(DUEL_ENEMY), g_DuelMgr.GetDuelPlayerID(DUEL_ENEMY));
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    PlayBuffer(SOUND_CLICK01);
    return CALLBACK_CONTINUE;
}

SEASON3B::CDuelResultMsgBox::CDuelResultMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), panel_(keeper, "duel_result.rml", "Combat")
{
    m_szWinnerID[0] = '\0';
    m_szLoserID[0] = '\0';
}

SEASON3B::CDuelResultMsgBox::~CDuelResultMsgBox()
{
    Release();
}

bool SEASON3B::CDuelResultMsgBox::Create(float fPriority)
{
    SetAddCallbackFunc();
    return CNewUIMessageBoxBase::Create(0, 0, 0, 0, fPriority);
}

void SEASON3B::CDuelResultMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&SEASON3B::CDuelResultMsgBox::OkBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_DUEL_OK);
    AddCallbackFunc(BindSelf(&SEASON3B::CDuelResultMsgBox::OkBtnDown), MSGBOX_EVENT_PRESSKEY_ESC);
}

void SEASON3B::CDuelResultMsgBox::Release()
{
    panel_.Release();
    CNewUIMessageBoxBase::Release();
}

bool SEASON3B::CDuelResultMsgBox::Update()
{
    wchar_t winner[256]{}, loser[256]{};
    mu_swprintf(winner, I18N::Game::SHasJustWon, m_szWinnerID);
    mu_swprintf(loser, I18N::Game::TheDuelWithS, m_szLoserID);
    panel_.SetText("tfTitle-label", I18N::Game::DuelFinished);
    panel_.SetText("taDuelInfo-label", std::wstring(winner) + L"\n" + loser + L"\n" +
                                           std::wstring(I18N::Game::Lookup(2697)));
    panel_.SetText("btnEnd-label", I18N::Game::OK);
    if (panel_.TakeClick("btnEnd"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_DUEL_OK);
    return true;
}

bool SEASON3B::CDuelResultMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return panel_.ProcessInput(event);
}
CALLBACK_RESULT SEASON3B::CDuelResultMsgBox::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                       const leaf::xstreambuf &xParam)
{
    // 	SendRequestDuelOk(1, g_iDuelPlayerIndex, g_szDuelPlayerID);

    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    PlayBuffer(SOUND_CLICK01);

    return CALLBACK_CONTINUE;
}

void CDuelResultMsgBox::SetIDs(wchar_t *pszWinnerID, wchar_t *pszLoserID)
{
    wcsncpy(m_szWinnerID, pszWinnerID, MAX_USERNAME_SIZE);
    m_szWinnerID[MAX_USERNAME_SIZE] = '\0';
    wcsncpy(m_szLoserID, pszLoserID, MAX_USERNAME_SIZE);
    m_szLoserID[MAX_USERNAME_SIZE] = '\0';
}

CCherryBlossomMsgBox::CCherryBlossomMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), gameData_(GameDataForConstruction())
{
    m_iMiddleCount = 8;
}

CCherryBlossomMsgBox::~CCherryBlossomMsgBox()
{
    Release();
}

bool CCherryBlossomMsgBox::Create(float fPriority)
{
    SetAddCallbackFunc();

    int x, y, width, height;
    x = (SCREEN_WIDTH / 2) - (MSGBOX_WIDTH / 2);
    y = 60;
    width = MSGBOX_WIDTH;
    height = MSGBOX_TOP_HEIGHT + (m_iMiddleCount * MSGBOX_MIDDLE_HEIGHT) + MSGBOX_BOTTOM_HEIGHT;

    CNewUIMessageBoxBase::Create(x, y, width, height, fPriority);

    SetButtonInfo();

    return true;
}

void CCherryBlossomMsgBox::Release()
{
    CNewUIMessageBoxBase::Release();
}

bool CCherryBlossomMsgBox::Update()
{
    m_BtnWhiteCB.Update();
    m_BtnRedCB.Update();
    m_BtnGoldCB.Update();
    m_BtnExit.Update();

    return true;
}

CALLBACK_RESULT CCherryBlossomMsgBox::LButtonUp(class CNewUIMessageBoxBase *pOwner,
                                                const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CCherryBlossomMsgBox *>(pOwner);
    if (pMsgBox)
    {
        if (pMsgBox->m_BtnWhiteCB.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_CUSTOM_CB_WHITE);
            return CALLBACK_BREAK;
        }
        if (pMsgBox->m_BtnRedCB.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_CUSTOM_CB_RED);
            return CALLBACK_BREAK;
        }
        if (pMsgBox->m_BtnGoldCB.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_CUSTOM_CB_GOLD);
            return CALLBACK_BREAK;
        }
        if (pMsgBox->m_BtnExit.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_CANCEL);
            return CALLBACK_BREAK;
        }
    }

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT CCherryBlossomMsgBox::WhiteCBBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                     const leaf::xstreambuf &xParam)
{
    //	g_pNewUISystem->Show(SEASON3B::INTERFACE_CHERRYBLOSSOM_WINDOW);
    //	g_pCherryBlossom->SetType(SEASON3B::CNewUICherryBlossom::CB_WHITE);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT CCherryBlossomMsgBox::RedCBBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                   const leaf::xstreambuf &xParam)
{
    //	g_pNewUISystem->Show(SEASON3B::INTERFACE_CHERRYBLOSSOM_WINDOW);
    //	g_pCherryBlossom->SetType(SEASON3B::CNewUICherryBlossom::CB_RED);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT CCherryBlossomMsgBox::GodCBBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                   const leaf::xstreambuf &xParam)
{
    //	g_pNewUISystem->Show(SEASON3B::INTERFACE_CHERRYBLOSSOM_WINDOW);
    //	g_pCherryBlossom->SetType(SEASON3B::CNewUICherryBlossom::CB_GOLD);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT CCherryBlossomMsgBox::ExitBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                  const leaf::xstreambuf &xParam)
{
    //	SocketClient->ToGameServer()->SendCraftingDialogCloseRequest();
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    return CALLBACK_CONTINUE;
}

void CCherryBlossomMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&SEASON3B::CCherryBlossomMsgBox::LButtonUp),
                    MSGBOX_EVENT_MOUSE_LBUTTON_UP);
    AddCallbackFunc(BindSelf(&SEASON3B::CCherryBlossomMsgBox::WhiteCBBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_CB_WHITE);
    AddCallbackFunc(BindSelf(&SEASON3B::CCherryBlossomMsgBox::RedCBBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_CB_RED);
    AddCallbackFunc(BindSelf(&SEASON3B::CCherryBlossomMsgBox::GodCBBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_CB_GOLD);
    AddCallbackFunc(BindSelf(&SEASON3B::CCherryBlossomMsgBox::ExitBtnDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
}

const SessionGameDataUnit &CCherryBlossomMsgBox::GameData() const noexcept
{
    return gameData_;
}

CALLBACK_RESULT SEASON3B::CTradeZenMsgBoxLayout::ProcessOk(class CNewUIMessageBoxBase *pOwner)
{
    auto *pMsgBox = dynamic_cast<CNewUITextInputMsgBox *>(pOwner);
    wchar_t strText[MAX_TEXT_LENGTH] = {
        0,
    };
    pMsgBox->GetInputBoxText(strText);
    if (wcslen(strText) == 0)
        return CALLBACK_CONTINUE;

    int iInputZen = _wtoi(strText);
    if (iInputZen == 0)
        return CALLBACK_CONTINUE;

    g_pTrade->SendRequestMyGoldInput(iInputZen);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CTradeZenMsgBoxLayout::ReturnDown(class CNewUIMessageBoxBase *pOwner,
                                                            const leaf::xstreambuf &xParam)
{
    return ProcessOk(pOwner);
}

CALLBACK_RESULT SEASON3B::CTradeZenMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                           const leaf::xstreambuf &xParam)
{
    return ProcessOk(pOwner);
}

CALLBACK_RESULT SEASON3B::CTradeZenMsgBoxLayout::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                               const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CZenReceiptMsgBoxLayout::ReturnDown(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    return ProcessOk(pOwner, xParam);
}

CALLBACK_RESULT SEASON3B::CZenReceiptMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    return ProcessOk(pOwner, xParam);
}

CALLBACK_RESULT SEASON3B::CZenReceiptMsgBoxLayout::ProcessOk(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CNewUITextInputMsgBox *>(pOwner);
    wchar_t strText[MAX_TEXT_LENGTH] = {
        0,
    };
    pMsgBox->GetInputBoxText(strText);
    if (wcslen(strText) == 0)
    {
        return CALLBACK_CONTINUE;
    }

    int iInputZen = _wtoi(strText);
    if (iInputZen == 0)
    {
        return CALLBACK_CONTINUE;
    }

    if (iInputZen <= (int)CharacterMachine->Gold)
    {
        SocketClient->ToGameServer()->SendVaultMoveMoneyRequest(
            VaultMoneyMoveDirection::InventoryToVault, iInputZen);
    }
    else
    {
        pOwner->ShowOkMessageBox(I18N::Game::YouAreShortOfZen);
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CZenReceiptMsgBoxLayout::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                 const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CZenPaymentMsgBoxLayout::ReturnDown(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    return ProcessOk(pOwner, xParam);
}

CALLBACK_RESULT SEASON3B::CZenPaymentMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    return ProcessOk(pOwner, xParam);
}

CALLBACK_RESULT SEASON3B::CZenPaymentMsgBoxLayout::ProcessOk(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CNewUITextInputMsgBox *>(pOwner);
    wchar_t strText[MAX_TEXT_LENGTH] = {
        0,
    };
    pMsgBox->GetInputBoxText(strText);
    if (wcslen(strText) == 0)
    {
        return CALLBACK_CONTINUE;
    }

    int iInputZen = _wtoi(strText);
    if (iInputZen == 0)
    {
        return CALLBACK_CONTINUE;
    }

    if (iInputZen <= CharacterMachine->StorageGold &&
        CharacterMachine->Gold + iInputZen <= 2000000000)
    {
        if (!g_pStorageInventory->IsStorageLocked() || g_pStorageInventory->IsCorrectPassword())
        {
            SocketClient->ToGameServer()->SendVaultMoveMoneyRequest(
                VaultMoneyMoveDirection::VaultToInventory, iInputZen);
        }
        else
        {
            g_pStorageInventory->SetBackupTakeZen(iInputZen);
            SEASON3B::CreateMessageBox(
                MSGBOX_LAYOUT_CLASS(SEASON3B::CPasswordKeyPadMsgBoxLayout, SessionOrigin()));
        }
    }
    else if (CharacterMachine->Gold + iInputZen > 2000000000)
    {
    }
    else
    {
        CreateOkMessageBox(I18N::Game::YouAreShortOfZen);
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CZenPaymentMsgBoxLayout::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                 const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CPersonalShopItemValueMsgBoxLayout::ProcessOk(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CNewUITextInputMsgBox *>(pOwner);

    wchar_t strText[MAX_TEXT_LENGTH] = {
        0,
    };

    pMsgBox->GetInputBoxText(strText);

    if (wcslen(strText) == 0)
    {
        return CALLBACK_CONTINUE;
    }

    int iInputZen = _wtoi(strText);
    if (iInputZen == 0)
    {
        return CALLBACK_CONTINUE;
    }

    ApplyItemPrice(iInputZen);
    g_pMyShopInventory->SetInputValueTextBox(false);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    return CALLBACK_BREAK;
}

void SEASON3B::CPersonalShopItemValueMsgBoxLayout::ApplyItemPrice(int itemPrice)
{
    if (g_pMyShopInventory->IsEnablePersonalShop())
        SocketClient->ToGameServer()->SendPlayerShopClose();

    CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();
    if (pPickedItem != nullptr)
    {
        ITEM *const item = pPickedItem->GetItem();
        const int sourceIndex = pPickedItem->GetSourceLinealPos();
        const int targetIndex = g_pMyShopInventory->GetTargetIndex();
        CNewUIInventoryCtrl *const owner = pPickedItem->GetOwnerInventory();
        if (owner == g_pMyInventory->GetInventoryCtrl() || owner == nullptr)
        {
            SocketClient->ToGameServer()->SendPlayerShopSetItemPrice(sourceIndex, itemPrice);
            SendRequestEquipmentItem(STORAGE_TYPE::INVENTORY, sourceIndex, item,
                                     STORAGE_TYPE::MYSHOP, targetIndex);
        }
        else if (owner == g_pMyShopInventory->GetInventoryCtrl())
        {
            SocketClient->ToGameServer()->SendPlayerShopSetItemPrice(sourceIndex, itemPrice);
            SendRequestEquipmentItem(STORAGE_TYPE::MYSHOP, sourceIndex, item, STORAGE_TYPE::MYSHOP,
                                     targetIndex);
        }
        AddPersonalItemPrice(targetIndex, itemPrice, g_IsPurchaseShop);
        return;
    }

    const int sourceIndex = g_pMyShopInventory->GetSourceIndex();
    SocketClient->ToGameServer()->SendPlayerShopSetItemPrice(sourceIndex, itemPrice);
    AddPersonalItemPrice(sourceIndex, itemPrice, g_IsPurchaseShop);
}

CALLBACK_RESULT SEASON3B::CPersonalShopItemValueMsgBoxLayout::ReturnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    return ProcessOk(pOwner, xParam);
}

CALLBACK_RESULT SEASON3B::CPersonalShopItemValueMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    return ProcessOk(pOwner, xParam);
}

CALLBACK_RESULT SEASON3B::CPersonalShopItemValueMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();
    g_pMyShopInventory->SetInputValueTextBox(false);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    return CALLBACK_BREAK;
}

SessionUiUnit &SEASON3B::CPersonalShopNameMsgBox::SessionUi() const noexcept
{
    return sessionUi_;
}

CALLBACK_RESULT SEASON3B::CPersonalShopNameMsgBoxLayout::ProcessOk(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CPersonalShopNameMsgBox *>(pOwner);
    wchar_t strText[MAX_TEXT_LENGTH] = {
        0,
    };
    pMsgBox->GetInputBoxText(strText);
    if (wcslen(strText) == 0)
    {
        return CALLBACK_CONTINUE;
    }

    pMsgBox->SessionUi().SetPersonalShopTitleIfValid(strText);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

void SessionUiUnit::SetPersonalShopTitleIfValid(const wchar_t *title)
{
    if (IsCorrectShopTitle(title))
    {
        wcscpy(g_szPersonalShopTitle, title);
    }
    else
    {
        g_pSystemLogBox->AddText(I18N::Game::WrongStoreName, SEASON3B::TYPE_SYSTEM_MESSAGE);
    }
}

bool SEASON3B::CreatePersonalShopNameMessageBox(SessionKeeper &keeper)
{
    TMsgBoxLayoutContainer<CPersonalShopNameMsgBoxLayout> container(keeper);
    if (!container.Create())
    {
        return false;
    }

    return container.SetLayout();
}

CALLBACK_RESULT SEASON3B::CPersonalShopNameMsgBoxLayout::ReturnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    return ProcessOk(pOwner, xParam);
}

CALLBACK_RESULT SEASON3B::CPersonalShopNameMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    return ProcessOk(pOwner, xParam);
}

CALLBACK_RESULT SEASON3B::CPersonalShopNameMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CCastleWithdrawMsgBoxLayout::ReturnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CNewUITextInputMsgBox *>(pOwner);
    wchar_t strText[MAX_TEXT_LENGTH] = {
        0,
    };
    pMsgBox->GetInputBoxText(strText);
    if (wcslen(strText) == 0)
    {
        return CALLBACK_CONTINUE;
    }

    DWORD dwInputZen = _wtoi(strText);
    if (dwInputZen == 0)
    {
        return CALLBACK_CONTINUE;
    }

    SocketClient->ToGameServer()->SendCastleSiegeTaxMoneyWithdraw(dwInputZen);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CCastleWithdrawMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                 const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CNewUITextInputMsgBox *>(pOwner);
    wchar_t strText[MAX_TEXT_LENGTH] = {
        0,
    };
    pMsgBox->GetInputBoxText(strText);
    if (wcslen(strText) == 0)
    {
        return CALLBACK_CONTINUE;
    }

    DWORD dwInputZen = _wtoi(strText);
    if (dwInputZen == 0)
    {
        return CALLBACK_CONTINUE;
    }

    SocketClient->ToGameServer()->SendCastleSiegeTaxMoneyWithdraw(dwInputZen);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CCastleWithdrawMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CPasswordKeyPadMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                 const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CNewUIKeyPadMsgBox *>(pOwner);

    if (pMsgBox == nullptr)
    {
        return CALLBACK_CONTINUE;
    }

    if (pMsgBox->GetInputSize() == pMsgBox->GetInputLimit())
    {
        WORD wInputNumber = (WORD)_wtoi(pMsgBox->GetInputText());
        SocketClient->ToGameServer()->SendUnlockVault(wInputNumber);
    }
    else
    {
        return CALLBACK_CONTINUE;
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CPasswordKeyPadMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    if (g_pPickedItem)
        g_pPickedItem->ShowPickedItem();

    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_STORAGE))
        g_pStorageInventory->SetItemAutoMove(false);

    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CStorageLockKeyPadMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CNewUIKeyPadMsgBox *>(pOwner);

    if (pMsgBox == nullptr)
    {
        return CALLBACK_CONTINUE;
    }

    PlayBuffer(SOUND_CLICK01);

    if (pMsgBox->GetInputSize() == pMsgBox->GetInputLimit())
    {
        if (pMsgBox->IsAllSameNumber() == true)
        {
            pMsgBox->ClearInput();
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
            pOwner->ShowOkMessageBox(I18N::Game::ItIsNotAllowedToUseSame4Numbers);
            return CALLBACK_BREAK;
        }

        CNewUIKeyPadMsgBox *pKeyPadMsgBox = NULL;
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CStorageLockCheckKeyPadMsgBoxLayout,
                                pMsgBox->OriginatingSession()),
            &pKeyPadMsgBox);
        if (pKeyPadMsgBox)
        {
            pKeyPadMsgBox->SetCheckInputText(pMsgBox->GetInputText());
        }
        pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
        return CALLBACK_BREAK;
    }

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT SEASON3B::CStorageLockKeyPadMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CStorageLockCheckKeyPadMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CNewUIKeyPadMsgBox *>(pOwner);

    if (pMsgBox == nullptr)
    {
        return CALLBACK_CONTINUE;
    }

    PlayBuffer(SOUND_CLICK01);

    if (pMsgBox->GetInputSize() == pMsgBox->GetInputLimit())
    {
        if (pMsgBox->IsAllSameNumber() == true)
        {
            pMsgBox->ClearInput();
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
            pOwner->ShowOkMessageBox(I18N::Game::ItIsNotAllowedToUseSame4Numbers);
            return CALLBACK_BREAK;
        }

        if (pMsgBox->IsCheckInput() == true)
        {
            WORD wInputNumber = (WORD)_wtoi(pMsgBox->GetInputText());

            CNewUITextInputMsgBox *pPassword = NULL;
            SEASON3B::CreateMessageBox(MSGBOX_LAYOUT_CLASS(SEASON3B::CStorageLockMsgBoxLayout,
                                                           pMsgBox->OriginatingSession()),
                                       &pPassword);
            if (pPassword)
            {
                pPassword->SetPassword(wInputNumber);
            }

            pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
            return CALLBACK_BREAK;
        }
        else
        {
            pMsgBox->ClearInput();
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
            pOwner->ShowOkMessageBox(I18N::Game::PasswordIsIncorrect);
            return CALLBACK_BREAK;
        }
    }

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT SEASON3B::CStorageLockCheckKeyPadMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CStorageLockMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    return ProcessOk(pOwner, xParam);
}

CALLBACK_RESULT SEASON3B::CStorageLockMsgBoxLayout::ReturnDown(class CNewUIMessageBoxBase *pOwner,
                                                               const leaf::xstreambuf &xParam)
{
    return ProcessOk(pOwner, xParam);
}

CALLBACK_RESULT SEASON3B::CStorageLockMsgBoxLayout::ProcessOk(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CNewUITextInputMsgBox *>(pOwner);

    if (pMsgBox == nullptr)
    {
        return CALLBACK_CONTINUE;
    }

    // Sized like the other GetInputBoxText callers (MAX_TEXT_LENGTH): GetText
    // fills up to its default length, so the old [20] buffer overflowed the
    // stack on Linux when entering the guild security code / break password.
    wchar_t strText[MAX_TEXT_LENGTH] = {
        0,
    };

    pMsgBox->GetInputBoxText(strText);
    int iInputTextSize = wcslen(strText);

    if (iInputTextSize > 0)
    {
        SocketClient->ToGameServer()->SendSetVaultPin(pMsgBox->GetPassword(), strText);
    }
    else
    {
        return CALLBACK_CONTINUE;
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CStorageLockMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CStorageLockFinalKeyPadMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CNewUIKeyPadMsgBox *>(pOwner);

    if (pMsgBox == nullptr)
    {
        return CALLBACK_CONTINUE;
    }

    PlayBuffer(SOUND_CLICK01);

    if (pMsgBox->GetInputSize() == pMsgBox->GetInputLimit())
    {
        if (pMsgBox->GetStoragePassword() != 0)
        {
            SocketClient->ToGameServer()->SendSetVaultPin(pMsgBox->GetStoragePassword(),
                                                          pMsgBox->GetInputText());
        }
        pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

        return CALLBACK_BREAK;
    }

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT SEASON3B::CStorageLockFinalKeyPadMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CStorageUnlockMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CNewUITextInputMsgBox *>(pOwner);

    if (pMsgBox == nullptr)
    {
        return CALLBACK_CONTINUE;
    }

    // Sized like the other GetInputBoxText callers (MAX_TEXT_LENGTH): GetText
    // fills up to its default length, so the old [20] buffer overflowed the
    // stack on Linux when entering the guild security code / break password.
    wchar_t strText[MAX_TEXT_LENGTH] = {
        0,
    };

    pMsgBox->GetInputBoxText(strText);
    int iInputTextSize = wcslen(strText);

    if (iInputTextSize > 0)
    {
        SocketClient->ToGameServer()->SendRemoveVaultPin(strText);
    }
    else
    {
        return CALLBACK_CONTINUE;
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CStorageUnlockMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CStorageUnlockKeyPadMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CNewUIKeyPadMsgBox *>(pOwner);

    if (pMsgBox == nullptr)
    {
        return CALLBACK_CONTINUE;
    }

    if (pMsgBox->GetInputSize() == pMsgBox->GetInputLimit())
    {
        SocketClient->ToGameServer()->SendRemoveVaultPin(pMsgBox->GetInputText());
    }
    else
    {
        return CALLBACK_CONTINUE;
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CStorageUnlockKeyPadMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

bool SEASON3B::CreateSystemMenuMessageBox(SessionKeeper &keeper)
{
    TMsgBoxLayoutContainer<CSystemMenuMsgBoxLayout> container(keeper);
    if (!container.Create())
    {
        return false;
    }

    return container.SetLayout();
}

bool SEASON3B::CreateCherryBlossomMessageBox(SessionKeeper &keeper)
{
    TMsgBoxLayoutContainer<CCherryBlossomMsgBoxLayout> container(keeper);
    if (!container.Create())
    {
        return false;
    }

    return container.SetLayout();
}

SEASON3B::CLuckyTradeMenuMsgBox::CLuckyTradeMenuMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), m_ModernMenu(keeper, "lucky_menu.rml")
{
}

SEASON3B::CLuckyTradeMenuMsgBox::~CLuckyTradeMenuMsgBox()
{
    Release();
}

void SEASON3B::CLuckyTradeMenuMsgBox::Release()
{
    m_ModernMenu.Release();
    CNewUIMessageBoxBase::Release();
}

bool SEASON3B::CLuckyTradeMenuMsgBox::Update()
{
    StageModernContent();
    if (m_ModernMenu.TakeClick("btnType1"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_LUCKYITEM_TRADE);
    if (m_ModernMenu.TakeClick("btnType2"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_LUCKYITEM_REFINERY);
    if (m_ModernMenu.TakeClick("btnClose"))
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
    return true;
}

bool SEASON3B::CLuckyTradeMenuMsgBox::Create(float fPriority)
{
    SetAddCallbackFunc();
    StageModernContent();
    return CNewUIMessageBoxBase::Create(0, 0, 0, 0, fPriority);
}

CALLBACK_RESULT SEASON3B::CLuckyTradeMenuMsgBox::LuckyItemTradeBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    g_pLuckyItemWnd->SetAct(eLuckyItemType_Trade);
    g_pNewUISystem->Show(SEASON3B::INTERFACE_LUCKYITEMWND);
    //g_pNewUISystem->Show(SEASON3B::INTERFACE_MIXINVENTORY);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CLuckyTradeMenuMsgBox::LuckyItemRefineryBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    g_pLuckyItemWnd->SetAct(eLuckyItemType_Refinery);
    g_pNewUISystem->Show(SEASON3B::INTERFACE_LUCKYITEMWND);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CLuckyTradeMenuMsgBox::ExitBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendCraftingDialogCloseRequest();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

void SEASON3B::CLuckyTradeMenuMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&SEASON3B::CLuckyTradeMenuMsgBox::LuckyItemTradeBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_LUCKYITEM_TRADE);
    AddCallbackFunc(BindSelf(&SEASON3B::CLuckyTradeMenuMsgBox::LuckyItemRefineryBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_LUCKYITEM_REFINERY);
    AddCallbackFunc(BindSelf(&SEASON3B::CLuckyTradeMenuMsgBox::ExitBtnDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
    AddCallbackFunc(BindSelf(&SEASON3B::CLuckyTradeMenuMsgBox::ExitBtnDown),
                    MSGBOX_EVENT_PRESSKEY_ESC);
}

//////////////////////////////////////////////////////////////////////////

SEASON3B::CTrainerMenuMsgBox::CTrainerMenuMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), m_ModernMenu(keeper, "trainer_menu.rml")
{
}

SEASON3B::CTrainerMenuMsgBox::~CTrainerMenuMsgBox()
{
    Release();
}

bool SEASON3B::CTrainerMenuMsgBox::Create(float fPriority)
{
    SetAddCallbackFunc();
    StageModernContent();
    return CNewUIMessageBoxBase::Create(0, 0, 0, 0, fPriority);
}

void SEASON3B::CTrainerMenuMsgBox::Release()
{
    m_ModernMenu.Release();
    CNewUIMessageBoxBase::Release();
}

bool SEASON3B::CTrainerMenuMsgBox::Update()
{
    StageModernContent();
    if (m_ModernMenu.TakeClick("btnRecover"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_TRAINER_MENU_RECOVER);
    if (m_ModernMenu.TakeClick("btnRevive"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_TRAINER_MENU_REVIVE);
    if (m_ModernMenu.TakeClick("btnClose"))
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
    return true;
}

CALLBACK_RESULT SEASON3B::CTrainerMenuMsgBox::RecoverBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    SEASON3B::CreateMessageBox(MSGBOX_LAYOUT_CLASS(SEASON3B::CTrainerRecoverMsgBoxLayout,
                                                   pOwner->MessageBoxSessionOrigin()));

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CTrainerMenuMsgBox::ReviveBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                            const leaf::xstreambuf &xParam)
{
    g_MixRecipeMgr.SetMixType(SEASON3A::MIXTYPE_TRAINER);
    g_pNewUISystem->Show(SEASON3B::INTERFACE_MIXINVENTORY);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CTrainerMenuMsgBox::ExitBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                          const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendCloseNpcRequest();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

void SEASON3B::CTrainerMenuMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&SEASON3B::CTrainerMenuMsgBox::RecoverBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_TRAINER_MENU_RECOVER);
    AddCallbackFunc(BindSelf(&SEASON3B::CTrainerMenuMsgBox::ReviveBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_TRAINER_MENU_REVIVE);
    AddCallbackFunc(BindSelf(&SEASON3B::CTrainerMenuMsgBox::ExitBtnDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
    AddCallbackFunc(BindSelf(&SEASON3B::CTrainerMenuMsgBox::ExitBtnDown),
                    MSGBOX_EVENT_PRESSKEY_ESC);
}

SEASON3B::CTrainerRecoverMsgBox::CTrainerRecoverMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), m_ModernMenu(keeper, "trainer_recover.rml")
{
}

SEASON3B::CTrainerRecoverMsgBox::~CTrainerRecoverMsgBox()
{
    Release();
}

bool SEASON3B::CTrainerRecoverMsgBox::Create(float fPriority)
{
    SetAddCallbackFunc();
    StageModernContent();
    return CNewUIMessageBoxBase::Create(0, 0, 0, 0, fPriority);
}

void SEASON3B::CTrainerRecoverMsgBox::Release()
{
    m_ModernMenu.Release();
    CNewUIMessageBoxBase::Release();
}

bool SEASON3B::CTrainerRecoverMsgBox::Update()
{
    StageModernContent();
    if (m_ModernMenu.TakeClick("btnDarkHorseRecover"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_TRAINER_MENU_RECOVER_DARKHORSE);
    if (m_ModernMenu.TakeClick("btnDarkSpiritRecover"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_TRAINER_MENU_RECOVER_DARKSPRIT);
    if (m_ModernMenu.TakeClick("btnClose"))
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
    return true;
}

CALLBACK_RESULT SEASON3B::CTrainerRecoverMsgBox::RecoverDarkSpiritrBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    sessionUi_.RecoverPet(REVIVAL_DARKSPIRIT);
    SocketClient->ToGameServer()->SendCloseNpcRequest();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CTrainerRecoverMsgBox::RecoverDarkHorseBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    sessionUi_.RecoverPet(REVIVAL_DARKHORSE);
    SocketClient->ToGameServer()->SendCloseNpcRequest();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CTrainerRecoverMsgBox::ExitBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendCloseNpcRequest();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

void SEASON3B::CTrainerRecoverMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&SEASON3B::CTrainerRecoverMsgBox::RecoverDarkSpiritrBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_TRAINER_MENU_RECOVER_DARKSPRIT);
    AddCallbackFunc(BindSelf(&SEASON3B::CTrainerRecoverMsgBox::RecoverDarkHorseBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_TRAINER_MENU_RECOVER_DARKHORSE);
    AddCallbackFunc(BindSelf(&SEASON3B::CTrainerRecoverMsgBox::ExitBtnDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
    AddCallbackFunc(BindSelf(&SEASON3B::CTrainerRecoverMsgBox::ExitBtnDown),
                    MSGBOX_EVENT_PRESSKEY_ESC);
}

SEASON3B::CElpisMsgBox::CElpisMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), m_ModernMenu(keeper, "elpis_menu.rml")
{
}

SEASON3B::CElpisMsgBox::~CElpisMsgBox()
{
    Release();
}

bool SEASON3B::CElpisMsgBox::Create(float fPriority)
{
    SetAddCallbackFunc();
    StageModernContent();
    return CNewUIMessageBoxBase::Create(0, 0, 0, 0, fPriority);
}

void SEASON3B::CElpisMsgBox::Release()
{
    m_ModernMenu.Release();
    CNewUIMessageBoxBase::Release();
}

bool SEASON3B::CElpisMsgBox::Update()
{
    StageModernContent();
    if (m_ModernMenu.TakeClick("btnSelect1"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_ELPIS_ABOUT_REFINARY);
    if (m_ModernMenu.TakeClick("btnSelect2"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_ELPIS_ABOUT_JEWELOFHARMONY);
    if (m_ModernMenu.TakeClick("btnSelect3"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_ELPIS_REFINE);
    if (m_ModernMenu.TakeClick("btnClose"))
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
    return true;
}

CALLBACK_RESULT SEASON3B::CElpisMsgBox::AboutRefinaryBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CElpisMsgBox *>(pOwner);
    if (pMsgBox)
    {
        pMsgBox->SetMessageType(MSGBOX_EVENT_USER_CUSTOM_ELPIS_ABOUT_REFINARY);
    }

    PlayBuffer(SOUND_CLICK01);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CElpisMsgBox::AboutJewelOfHarmonyBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CElpisMsgBox *>(pOwner);
    if (pMsgBox)
    {
        pMsgBox->SetMessageType(MSGBOX_EVENT_USER_CUSTOM_ELPIS_ABOUT_JEWELOFHARMONY);
    }

    PlayBuffer(SOUND_CLICK01);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CElpisMsgBox::RefineBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                      const leaf::xstreambuf &xParam)
{
    g_MixRecipeMgr.SetMixType(SEASON3A::MIXTYPE_ELPIS);
    g_pNewUISystem->Show(SEASON3B::INTERFACE_MIXINVENTORY);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CElpisMsgBox::ExitBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                    const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendCraftingDialogCloseRequest();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

void SEASON3B::CElpisMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&SEASON3B::CElpisMsgBox::AboutRefinaryBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_ELPIS_ABOUT_REFINARY);
    AddCallbackFunc(BindSelf(&SEASON3B::CElpisMsgBox::AboutJewelOfHarmonyBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_ELPIS_ABOUT_JEWELOFHARMONY);
    AddCallbackFunc(BindSelf(&SEASON3B::CElpisMsgBox::RefineBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_ELPIS_REFINE);
    AddCallbackFunc(BindSelf(&SEASON3B::CElpisMsgBox::ExitBtnDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
    AddCallbackFunc(BindSelf(&SEASON3B::CElpisMsgBox::ExitBtnDown), MSGBOX_EVENT_PRESSKEY_ESC);
}

SEASON3B::CSeedMasterMenuMsgBox::CSeedMasterMenuMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), m_ModernMenu(keeper, "seed_master_menu.rml")
{
}

SEASON3B::CSeedMasterMenuMsgBox::~CSeedMasterMenuMsgBox()
{
    Release();
}

bool SEASON3B::CSeedMasterMenuMsgBox::Create(float fPriority)
{
    SetAddCallbackFunc();
    StageModernContent();
    return CNewUIMessageBoxBase::Create(0, 0, 0, 0, fPriority);
}

void SEASON3B::CSeedMasterMenuMsgBox::Release()
{
    m_ModernMenu.Release();
    CNewUIMessageBoxBase::Release();
}

bool SEASON3B::CSeedMasterMenuMsgBox::Update()
{
    StageModernContent();
    if (m_ModernMenu.TakeClick("btnType1"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_SEED_MASTER_MENU_EXTRACT_SEED);
    if (m_ModernMenu.TakeClick("btnType2"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_SEED_MASTER_MENU_SEED_SPHERE);
    if (m_ModernMenu.TakeClick("btnClose"))
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
    return true;
}

CALLBACK_RESULT SEASON3B::CSeedMasterMenuMsgBox::ExtractSeedBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    g_MixRecipeMgr.SetMixType(SEASON3A::MIXTYPE_EXTRACT_SEED);
    g_pNewUISystem->Show(SEASON3B::INTERFACE_MIXINVENTORY);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CSeedMasterMenuMsgBox::SeedSphereBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    g_MixRecipeMgr.SetMixType(SEASON3A::MIXTYPE_SEED_SPHERE);
    g_pNewUISystem->Show(SEASON3B::INTERFACE_MIXINVENTORY);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CSeedMasterMenuMsgBox::ExitBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendCraftingDialogCloseRequest();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

void SEASON3B::CSeedMasterMenuMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&SEASON3B::CSeedMasterMenuMsgBox::ExtractSeedBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_SEED_MASTER_MENU_EXTRACT_SEED);
    AddCallbackFunc(BindSelf(&SEASON3B::CSeedMasterMenuMsgBox::SeedSphereBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_SEED_MASTER_MENU_SEED_SPHERE);
    AddCallbackFunc(BindSelf(&SEASON3B::CSeedMasterMenuMsgBox::ExitBtnDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
    AddCallbackFunc(BindSelf(&SEASON3B::CSeedMasterMenuMsgBox::ExitBtnDown),
                    MSGBOX_EVENT_PRESSKEY_ESC);
}

SEASON3B::CSeedInvestigatorMenuMsgBox::CSeedInvestigatorMenuMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), m_ModernMenu(keeper, "seed_investigator_menu.rml")
{
}

SEASON3B::CSeedInvestigatorMenuMsgBox::~CSeedInvestigatorMenuMsgBox()
{
    Release();
}

bool SEASON3B::CSeedInvestigatorMenuMsgBox::Create(float fPriority)
{
    SetAddCallbackFunc();
    StageModernContent();
    return CNewUIMessageBoxBase::Create(0, 0, 0, 0, fPriority);
}

void SEASON3B::CSeedInvestigatorMenuMsgBox::Release()
{
    m_ModernMenu.Release();
    CNewUIMessageBoxBase::Release();
}

bool SEASON3B::CSeedInvestigatorMenuMsgBox::Update()
{
    StageModernContent();
    if (m_ModernMenu.TakeClick("btnType1"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_SEED_INVESTIGATOR_MENU_ATTACH_SOCKET);
    if (m_ModernMenu.TakeClick("btnType2"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_SEED_INVESTIGATOR_MENU_DETACH_SOCKET);
    if (m_ModernMenu.TakeClick("btnClose"))
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
    return true;
}

CALLBACK_RESULT SEASON3B::CSeedInvestigatorMenuMsgBox::AttachSocketBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    g_MixRecipeMgr.SetMixType(SEASON3A::MIXTYPE_ATTACH_SOCKET);
    g_pNewUISystem->Show(SEASON3B::INTERFACE_MIXINVENTORY);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CSeedInvestigatorMenuMsgBox::DetachSocketBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    g_MixRecipeMgr.SetMixType(SEASON3A::MIXTYPE_DETACH_SOCKET);
    g_pNewUISystem->Show(SEASON3B::INTERFACE_MIXINVENTORY);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CSeedInvestigatorMenuMsgBox::ExitBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendCraftingDialogCloseRequest();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

void SEASON3B::CSeedInvestigatorMenuMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&SEASON3B::CSeedInvestigatorMenuMsgBox::AttachSocketBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_SEED_INVESTIGATOR_MENU_ATTACH_SOCKET);
    AddCallbackFunc(BindSelf(&SEASON3B::CSeedInvestigatorMenuMsgBox::DetachSocketBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_SEED_INVESTIGATOR_MENU_DETACH_SOCKET);
    AddCallbackFunc(BindSelf(&SEASON3B::CSeedInvestigatorMenuMsgBox::ExitBtnDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
    AddCallbackFunc(BindSelf(&SEASON3B::CSeedInvestigatorMenuMsgBox::ExitBtnDown),
                    MSGBOX_EVENT_PRESSKEY_ESC);
}

bool SEASON3B::CResetCharacterPointMsgBox::Create(float priority)
{
    if (!CNewUICommonMessageBox::Create(MSGBOX_COMMON_TYPE_OKCANCEL, priority))
        return false;
    UseS16Caution(I18N::Game::ReInitializationHelper,
                  I18N::Game::ClickOnTheButtonToReinitializeAllStatPoints);
    AddCallbackFunc(BindSelf(&CResetCharacterPointMsgBox::ResetCharacterPointBtnDown),
                    MSGBOX_EVENT_USER_COMMON_OK);
    return true;
}

CALLBACK_RESULT SEASON3B::CResetCharacterPointMsgBox::ResetCharacterPointBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);

    for (int i = 0; i < MAX_EQUIPMENT; i++)
    {
        if (CharacterMachine->Equipment[i].Type != -1)
        {
            g_pSystemLogBox->AddText(I18N::Game::TheAppliedEquipmentsCannotBeReset,
                                     SEASON3B::TYPE_ERROR_MESSAGE);
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
            return CALLBACK_BREAK;
        }
    }

    SocketClient->ToGameServer()->SendResetCharacterPointRequest();

    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGuildBreakPasswordMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    return ProcessOk(pOwner, xParam);
}

CALLBACK_RESULT SEASON3B::CGuildBreakPasswordMsgBoxLayout::ReturnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    return ProcessOk(pOwner, xParam);
}

CALLBACK_RESULT SEASON3B::CGuildBreakPasswordMsgBoxLayout::ProcessOk(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    if (submitted_)
        return CALLBACK_BREAK;
    submitted_ = true;
    auto *pMsgBox = dynamic_cast<CNewUITextInputMsgBox *>(pOwner);

    if (pMsgBox == nullptr)
    {
        return CALLBACK_CONTINUE;
    }

    // Sized like the other GetInputBoxText callers (MAX_TEXT_LENGTH): GetText
    // fills up to its default length, so the old [20] buffer overflowed the
    // stack on Linux when entering the guild security code / break password.
    wchar_t strText[MAX_TEXT_LENGTH] = {
        0,
    };

    pMsgBox->GetInputBoxText(strText);
    int iInputTextSize = wcslen(strText);

    if (iInputTextSize > 0)
    {
        SocketClient->ToGameServer()->SendGuildKickPlayerRequest(member_.c_str(), strText);
    }
    else
    {
        g_pSystemLogBox->AddText(I18N::Game::ThePasswordYouHaveEnteredIsIncorrect,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGuildBreakPasswordMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

SEASON3B::CGuild_ToPerson_Position::CGuild_ToPerson_Position(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), panel_(keeper, "guild_position.rml", "Guild")
{
}
SEASON3B::CGuild_ToPerson_Position::~CGuild_ToPerson_Position()
{
    Release();
}
bool SEASON3B::CGuild_ToPerson_Position::Create(float priority)
{
    member_ = GuildList[DeleteIndex].Name;
    role_ = AppointType = G_SUB_MASTER;
    submitted_ = false;
    AddCallbackFunc(BindSelf(&CGuild_ToPerson_Position::BlessingBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_GEM_UNITY_BLESSING);
    AddCallbackFunc(BindSelf(&CGuild_ToPerson_Position::SoulBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_GEM_UNITY_SOUL);
    AddCallbackFunc(BindSelf(&CGuild_ToPerson_Position::OkBtnDown), MSGBOX_EVENT_USER_COMMON_OK);
    AddCallbackFunc(BindSelf(&CGuild_ToPerson_Position::CancelBtnDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
    AddCallbackFunc(BindSelf(&CGuild_ToPerson_Position::CancelBtnDown), MSGBOX_EVENT_PRESSKEY_ESC);
    AddCallbackFunc(BindSelf(&CGuild_ToPerson_Position::OkBtnDown), MSGBOX_EVENT_PRESSKEY_RETURN);
    StageModernContent();
    return CNewUIMessageBoxBase::Create(0, 0, 0, 0, priority);
}
void SEASON3B::CGuild_ToPerson_Position::Release()
{
    panel_.Release();
    CNewUIMessageBoxBase::Release();
}

bool SEASON3B::CGuild_ToPerson_Position::Update()
{
    if (panel_.TakeClick("btnType1"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_GEM_UNITY_BLESSING);
    if (panel_.TakeClick("btnType2"))
        SendEvent(this, MSGBOX_EVENT_USER_CUSTOM_GEM_UNITY_SOUL);
    StageModernContent();
    if (panel_.TakeClick("btnOk"))
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_OK);
    else if (panel_.TakeClick("btnCancel"))
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
    return true;
}

bool SEASON3B::CGuild_ToPerson_Position::ProcessModernUiInput(const SessionInputEvent &event)
{
    return panel_.ProcessInput(event);
}
CALLBACK_RESULT SEASON3B::CGuild_ToPerson_Position::BlessingBtnDown(CNewUIMessageBoxBase *,
                                                                    const leaf::xstreambuf &)
{
    role_ = AppointType = G_SUB_MASTER;
    StageModernContent();
    return CALLBACK_CONTINUE;
}
CALLBACK_RESULT SEASON3B::CGuild_ToPerson_Position::SoulBtnDown(CNewUIMessageBoxBase *,
                                                                const leaf::xstreambuf &)
{
    role_ = AppointType = G_BATTLE_MASTER;
    StageModernContent();
    return CALLBACK_CONTINUE;
}
CALLBACK_RESULT SEASON3B::CGuild_ToPerson_Position::OkBtnDown(CNewUIMessageBoxBase *owner,
                                                              const leaf::xstreambuf &)
{
    if (submitted_)
        return CALLBACK_BREAK;
    submitted_ = true;
    SessionOrigin().Gameplay()->Exit();
    if (Hero->GuildStatus == G_MASTER)
    {
        SocketClient->ToGameServer()->SendGuildRoleAssignRequest(role_, member_.c_str(), 0x02);
        SocketClient->ToGameServer()->SendGuildListRequest();
    }
    PlayBuffer(SOUND_CLICK01);
    owner->SendEvent(owner, MSGBOX_EVENT_DESTROY);
    return CALLBACK_BREAK;
}
CALLBACK_RESULT SEASON3B::CGuild_ToPerson_Position::CancelBtnDown(CNewUIMessageBoxBase *owner,
                                                                  const leaf::xstreambuf &)
{
    SessionOrigin().Gameplay()->Exit();
    PlayBuffer(SOUND_CLICK01);
    owner->SendEvent(owner, MSGBOX_EVENT_DESTROY);
    return CALLBACK_BREAK;
}

SEASON3B::CDelgardoMainMenuMsgBox::CDelgardoMainMenuMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper)
{
    m_iMiddleCount = 7;
}

SEASON3B::CDelgardoMainMenuMsgBox::~CDelgardoMainMenuMsgBox()
{
    Release();
}

bool SEASON3B::CDelgardoMainMenuMsgBox::Create(float fPriority)
{
    SetAddCallbackFunc();

    int x, y, width, height;
    x = (SCREEN_WIDTH / 2) - (MSGBOX_WIDTH / 2);
    y = 60;
    width = MSGBOX_WIDTH;
    height = MSGBOX_TOP_HEIGHT + (m_iMiddleCount * MSGBOX_MIDDLE_HEIGHT) + MSGBOX_BOTTOM_HEIGHT;

    CNewUIMessageBoxBase::Create(x, y, width, height, fPriority);
    SetButtonInfo();

    return true;
}

void SEASON3B::CDelgardoMainMenuMsgBox::Release()
{
    CNewUIMessageBoxBase::Release();
}

bool SEASON3B::CDelgardoMainMenuMsgBox::Update()
{
    m_BtnReg.Update();
    m_BtnExchange.Update();
    m_BtnExit.Update();

    return true;
}

CALLBACK_RESULT SEASON3B::CDelgardoMainMenuMsgBox::LButtonUp(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CDelgardoMainMenuMsgBox *>(pOwner);
    if (pMsgBox)
    {
        if (pMsgBox->m_BtnReg.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_CUSTOM_DELGARDO_REGISTRATION_LUCKY_COIN);
            return CALLBACK_BREAK;
        }
        if (pMsgBox->m_BtnExchange.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_CUSTOM_DELGARDO_EXCHANGE_LUCKY_COIN);
            return CALLBACK_BREAK;
        }
        if (pMsgBox->m_BtnExit.IsMouseIn() == true)
        {
            pOwner->SendEvent(pOwner, MSGBOX_EVENT_USER_COMMON_CANCEL);
            return CALLBACK_BREAK;
        }
    }

    return CALLBACK_CONTINUE;
}

CALLBACK_RESULT SEASON3B::CDelgardoMainMenuMsgBox::RegBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    g_pNewUISystem->Show(SEASON3B::INTERFACE_LUCKYCOIN_REGISTRATION);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CDelgardoMainMenuMsgBox::ExchangeBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    g_pNewUISystem->Show(SEASON3B::INTERFACE_EXCHANGE_LUCKYCOIN);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CDelgardoMainMenuMsgBox::ExitBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                               const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendCraftingDialogCloseRequest();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

void SEASON3B::CDelgardoMainMenuMsgBox::SetAddCallbackFunc()
{
    AddCallbackFunc(BindSelf(&SEASON3B::CDelgardoMainMenuMsgBox::LButtonUp),
                    MSGBOX_EVENT_MOUSE_LBUTTON_UP);
    AddCallbackFunc(BindSelf(&SEASON3B::CDelgardoMainMenuMsgBox::RegBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_DELGARDO_REGISTRATION_LUCKY_COIN);
    AddCallbackFunc(BindSelf(&SEASON3B::CDelgardoMainMenuMsgBox::ExchangeBtnDown),
                    MSGBOX_EVENT_USER_CUSTOM_DELGARDO_EXCHANGE_LUCKY_COIN);
    AddCallbackFunc(BindSelf(&SEASON3B::CDelgardoMainMenuMsgBox::ExitBtnDown),
                    MSGBOX_EVENT_USER_COMMON_CANCEL);
}

bool SEASON3B::CChaosMixMenuMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernMenu.ProcessInput(event);
}

bool SEASON3B::CTrainerMenuMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernMenu.ProcessInput(event);
}

bool SEASON3B::CTrainerRecoverMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernMenu.ProcessInput(event);
}

bool SEASON3B::CSeedMasterMenuMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernMenu.ProcessInput(event);
}

bool SEASON3B::CSeedInvestigatorMenuMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernMenu.ProcessInput(event);
}

bool SEASON3B::CLuckyTradeMenuMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernMenu.ProcessInput(event);
}

bool SEASON3B::CElpisMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernMenu.ProcessInput(event);
}

bool SEASON3B::CNewUIKeyPadMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernMenu.ProcessInput(event);
}

bool SEASON3B::CGemIntegrationMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return panel_.ProcessInput(event);
}

bool SEASON3B::CGemIntegrationUnityMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return ActivePanel().ProcessInput(event);
}

bool SEASON3B::CGemIntegrationDisjointMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return panel_.ProcessInput(event);
}

UI::Modern::PC::Inventory::RmlItemDialogPanel &SEASON3B::CGemIntegrationUnityMsgBox::ActivePanel()
{
    return m_cGemType == COMGEM::eNOGEM ? panel_ : unitPanel_;
}

//*****************************************************************************
//*****************************************************************************

CMsgWin::CMsgWin(SessionKeeper &keeper) : CWin(keeper), m_panel(keeper)
{
}

CMsgWin::~CMsgWin() = default;

void CMsgWin::Create()
{
    CWin::Create(static_cast<int>(ModernUiViewportWidth()),
                 static_cast<int>(ModernUiViewportHeight()));
    m_panel.Create();
    m_panel.SetMode(UI::Modern::RmlMessageBoxMode::Ok);

    memset(m_aszMsg, 0, sizeof(m_aszMsg));

    m_eType = MWT_NON;
    m_nMsgLine = 0;
    m_nMsgCode = -1;
    m_nGameExit = -1;
    m_dDeltaTickSum = 0.0;
}

void CMsgWin::PreRelease()
{
    m_panel.Release();
}

bool CMsgWin::CursorInWin(int nArea)
{
    if (!CWin::m_bShow)
        return false;

    switch (nArea)
    {
    case WA_MOVE:
        return false;
    }

    return CWin::CursorInWin(nArea);
}

void CMsgWin::UpdateWhileActive(double dDeltaTick)
{
    const bool enterDown = IsPress(VK_RETURN) || IsRepeat(VK_RETURN);
    const bool escapeDown = IsPress(VK_ESCAPE) || IsRepeat(VK_ESCAPE);
    const bool enterPressed = enterDown && !enterKeyHeld_;
    const bool escapePressed = escapeDown && !escapeKeyHeld_;
    enterKeyHeld_ = enterDown;
    escapeKeyHeld_ = escapeDown;

    if (enterPressed)
    {
        if (m_eType > MWT_BTN_CANCEL)
        {
            PlayBuffer(SOUND_CLICK01);
            ManageOKClick();
        }
        else if (m_eType == MWT_BTN_CANCEL)
        {
            PlayBuffer(SOUND_CLICK01);
            ManageCancelClick();
        }
    }
    else if (escapePressed)
    {
        if (m_eType == MWT_BTN_OK)
        {
            PlayBuffer(SOUND_CLICK01);
            ManageOKClick();
        }
        else if (m_eType > MWT_NON)
        {
            PlayBuffer(SOUND_CLICK01);
            ManageCancelClick();
        }
        LegacyUiManager().SetSysMenuWinShow(false);
    }
    else if (m_panel.OkButton().IsClick())
        ManageOKClick();
    else if (m_panel.CancelButton().IsClick())
        ManageCancelClick();
    else if (m_nMsgCode == MESSAGE_GAME_END_COUNTDOWN)
    {
        if (m_nGameExit != -1)
        {
            m_dDeltaTickSum += dDeltaTick;
            if (m_dDeltaTickSum >= 1000.0)
            {
                const int seconds = static_cast<int>(m_dDeltaTickSum / 1000.0);
                m_dDeltaTickSum -= seconds * 1000.0;
                m_nGameExit = (std::max)(0, m_nGameExit - seconds);
                if (m_nGameExit == 0)
                {
                    g_ErrorReport.Write(L"> Menu - Exit game.");
                    g_ErrorReport.WriteCurrentTime();
                    sessionKeeper_.RequestDiscardSession();
                }
                else
                {
                    wchar_t szMsg[64]{};
                    mu_swprintf(szMsg, I18N::Game::YouWillExitGameInDSeconds, m_nGameExit);
                    SetMsg(m_eType, szMsg, L"");
                }
            }
        }
    }
}

bool CMsgWin::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsShow() && m_panel.ProcessInput(event);
}

std::optional<UI::Modern::RmlTextInputArea> CMsgWin::ModernTextInputArea() const
{
    return CWin::m_bShow ? m_panel.TextInputArea() : std::nullopt;
}

void CMsgWin::SetMsg(MSG_WIN_TYPE eType, std::wstring lpszMsg, std::wstring lpszMsg2)
{
    m_eType = eType;
    m_panel.SetMode(PanelMode(eType));
    memset(m_aszMsg, 0, sizeof(m_aszMsg));

    if (lpszMsg2.empty())
    {
        m_nMsgLine =
            ::SeparateTextIntoLines(lpszMsg.c_str(), m_aszMsg[0], MW_MSG_LINE_MAX, MW_MSG_ROW_MAX);
    }
    else
    {
        wcsncpy_s(m_aszMsg[0], lpszMsg.c_str(), _TRUNCATE);
        wcsncpy_s(m_aszMsg[1], lpszMsg2.c_str(), _TRUNCATE);
        m_nMsgLine = 2;
    }
}

UI::Modern::RmlMessageBoxMode CMsgWin::PanelMode(MSG_WIN_TYPE type) noexcept
{
    switch (type)
    {
    case MWT_NON:
        return UI::Modern::RmlMessageBoxMode::MessageOnly;
    case MWT_BTN_CANCEL:
        return UI::Modern::RmlMessageBoxMode::Cancel;
    case MWT_BTN_BOTH:
        return UI::Modern::RmlMessageBoxMode::OkCancel;
    case MWT_STR_INPUT:
        return UI::Modern::RmlMessageBoxMode::Password;
    case MWT_BTN_OK:
    default:
        return UI::Modern::RmlMessageBoxMode::Ok;
    }
}

void CMsgWin::PopUp(int nMsgCode, wchar_t *pszMsg)
{
    CUIMng &rUIMng = LegacyUiManager();
    std::wstring lpszMsg = L"";
    std::wstring lpszMsg2 = L"";
    MSG_WIN_TYPE eType = MWT_BTN_OK;
    m_nMsgCode = nMsgCode;
    wchar_t szTempMsg[128];

    switch (m_nMsgCode)
    {
    case MESSAGE_FREE_MSG_NOT_BTN:
        lpszMsg = pszMsg;
        eType = MWT_NON;
        break;
    case MESSAGE_GAME_END_COUNTDOWN:
        m_nGameExit = 5;
        mu_swprintf(szTempMsg, I18N::Game::YouWillExitGameInDSeconds, m_nGameExit);
        lpszMsg = szTempMsg;
        eType = MWT_NON;
        break;
    case MESSAGE_WAIT:
        lpszMsg = I18N::Game::PleaseWait;
        eType = MWT_NON;
        break;
    case MESSAGE_SERVER_BUSY:
    case RECEIVE_LOG_IN_FAIL_SERVER_BUSY:
        lpszMsg = I18N::Game::TheServerIsFull;
        break;
    case RECEIVE_JOIN_SERVER_WAITING:
        rUIMng.ShowWin(rUIMng.m_ServerSelWin.get());
        lpszMsg = I18N::Game::TheServerIsFull;
        break;
    case MESSAGE_SERVER_LOST:
        lpszMsg = I18N::Game::YouAreDisconnectedFromTheServer;
        break;
    case MESSAGE_VERSION:
    case RECEIVE_LOG_IN_FAIL_VERSION:
        lpszMsg = I18N::Game::NewVersionOfGameIsRequired;
        lpszMsg2 = I18N::Game::PleaseDownloadTheNewVersion;
        break;
    case MESSAGE_INPUT_ID:
        lpszMsg = I18N::Game::EnterYourAccount;
        break;
    case MESSAGE_INPUT_PASSWORD:
        lpszMsg = I18N::Game::EnterYourPassword;
        break;
    case RECEIVE_LOG_IN_FAIL_ID:
        lpszMsg = I18N::Game::YourAccountIsInvalid;
        break;
    case RECEIVE_LOG_IN_FAIL_PASSWORD:
        lpszMsg = I18N::Game::PasswordIsIncorrect;
        break;
    case RECEIVE_LOG_IN_FAIL_ID_CONNECTED:
        lpszMsg = I18N::Game::YourAccountIsAlreadyConnected;
        break;
    case RECEIVE_LOG_IN_FAIL_ID_BLOCK:
    case MESSAGE_DELETE_CHARACTER_ID_BLOCK:
        lpszMsg = I18N::Game::ThisAccountIsBlocked;
        break;
    case RECEIVE_LOG_IN_FAIL_CONNECT:
        lpszMsg = I18N::Game::ConnectionError;
        break;
    case RECEIVE_LOG_IN_FAIL_ERROR:
        lpszMsg = I18N::Game::ConnectionClosedDueTo3FailedAttempts;
        break;
    case RECEIVE_LOG_IN_FAIL_NO_PAYMENT_INFO:
        lpszMsg = I18N::Game::NoChargeInfo;
        break;
    case RECEIVE_LOG_IN_FAIL_USER_TIME1:
        lpszMsg = I18N::Game::YourIndividualSubscriptionTermIsOver;
        break;
    case RECEIVE_LOG_IN_FAIL_USER_TIME2:
        lpszMsg = I18N::Game::YourIndividualSubscriptionTimeIsOver;
        break;
    case RECEIVE_LOG_IN_FAIL_PC_TIME1:
        lpszMsg = I18N::Game::SubscriptionTermIsOverOnYourIP;
        break;
    case RECEIVE_LOG_IN_FAIL_PC_TIME2:
        lpszMsg = I18N::Game::SubscriptionTimeIsOverOnYourIP;
        break;
    case RECEIVE_LOG_IN_FAIL_ONLY_OVER_15:
        lpszMsg = I18N::Game::OnlyPlayersAge18AndOverArePermittedToConnectToThisServer;
        break;
    case RECEIVE_LOG_IN_FAIL_CHARGED_CHANNEL:
        lpszMsg = I18N::Game::PleasePurchaseGoldChannelTicketToEnter;
        break;
    case RECEIVE_LOG_IN_FAIL_POINT_DATE:
        lpszMsg = I18N::Game::PointNoMoreDates;
        break;
    case RECEIVE_LOG_IN_FAIL_POINT_HOUR:
        lpszMsg = I18N::Game::PointNoMorePointsLeft;
        break;
    case RECEIVE_LOG_IN_FAIL_INVALID_IP:
        lpszMsg = I18N::Game::YourIPIsNotAllowedToConnect;
        break;
    case MESSAGE_DELETE_CHARACTER_GUILDWARNING:
        lpszMsg = I18N::Game::YouCanTDeleteTheCharacterThatBelongsToTheGuild;
        break;
    case MESSAGE_DELETE_CHARACTER_WARNING:
        mu_swprintf(szTempMsg, I18N::Game::CharacterLevelAboveDCannotBeDeleted, CHAR_DEL_LIMIT_LV);
        lpszMsg = szTempMsg;
        break;
    case MESSAGE_DELETE_CHARACTER_CONFIRM:
        mu_swprintf(szTempMsg, I18N::Game::WouldYouLikeToDeleteSCharacter,
                    CharactersClient[SelectedHero].ID);
        lpszMsg = szTempMsg;
        eType = MWT_BTN_BOTH;
        break;
    case MESSAGE_DELETE_CHARACTER_RESIDENT:
        lpszMsg = I18N::Game::PleaseEnterYourWEBZENCOMPassword;
        eType = MWT_STR_INPUT;
        InitResidentNumInput();
        break;
    case MESSAGE_DELETE_CHARACTER_ITEM_BLOCK:
        lpszMsg = I18N::Game::TheCharacterIsItemBlocked;
        break;
    case MESSAGE_STORAGE_RESIDENTWRONG:
        lpszMsg = I18N::Game::ThePasswordYouHaveEnteredIsIncorrect;
        break;
    case MESSAGE_DELETE_CHARACTER_SUCCESS:
        CharactersClient[SelectedHero].Object.Live = false;
        DeleteMount(&CharactersClient[SelectedHero].Object);
        SelectedHero = -1;
        rUIMng.m_CharSelMainWin->UpdateDisplay();
        rUIMng.m_CharInfoBalloonMng->UpdateDisplay();
        lpszMsg = I18N::Game::CharacterWasDeletedSuccessfully;
        break;
    case MESSAGE_BLOCKED_CHARACTER:
        lpszMsg = I18N::Game::ThisIsABlockedCharacter;
        break;
    case MESSAGE_MIN_LENGTH:
        lpszMsg = I18N::Game::TypeMoreThan4Letters;
        break;
    case MESSAGE_ID_SPACE_ERROR:
        lpszMsg = I18N::Game::ItContainsProhibitedWords;
        break;
    case MESSAGE_SPECIAL_NAME:
        lpszMsg = I18N::Game::CannotUseSymbols;
        break;
    case RECEIVE_CREATE_CHARACTER_FAIL:
        rUIMng.ShowWin(rUIMng.m_CharMakeWin.get());
        lpszMsg = I18N::Game::IncorrectCharacterNameWasEnteredOrSameCharacterNameExists;
        break;
    case RECEIVE_CREATE_CHARACTER_FAIL2:
        rUIMng.ShowWin(rUIMng.m_CharMakeWin.get());
        lpszMsg = I18N::Game::NoMoreCharactersCanBeCreated;
        break;
    default:
        m_nMsgCode = -1;
        return;
    }

    SetMsg(eType, lpszMsg, lpszMsg2);
    rUIMng.ShowWin(this);
}

void CMsgWin::ManageOKClick()
{
    CUIMng &rUIMng = LegacyUiManager();
    rUIMng.HideWin(this);

    switch (m_nMsgCode)
    {
    case MESSAGE_VERSION:
    case MESSAGE_SERVER_LOST:
    case RECEIVE_LOG_IN_FAIL_VERSION:
    case RECEIVE_LOG_IN_FAIL_ERROR:
    case MESSAGE_INPUT_ID:
    case RECEIVE_LOG_IN_FAIL_ID:
    case RECEIVE_LOG_IN_FAIL_ID_CONNECTED:
    case RECEIVE_LOG_IN_FAIL_SERVER_BUSY:
    case RECEIVE_LOG_IN_FAIL_ID_BLOCK:
    case RECEIVE_LOG_IN_FAIL_CONNECT:
    case RECEIVE_LOG_IN_FAIL_NO_PAYMENT_INFO:
    case RECEIVE_LOG_IN_FAIL_USER_TIME1:
    case RECEIVE_LOG_IN_FAIL_USER_TIME2:
    case RECEIVE_LOG_IN_FAIL_PC_TIME1:
    case RECEIVE_LOG_IN_FAIL_PC_TIME2:
    case RECEIVE_LOG_IN_FAIL_ONLY_OVER_15:
    case RECEIVE_LOG_IN_FAIL_POINT_DATE:
    case RECEIVE_LOG_IN_FAIL_POINT_HOUR:
    case RECEIVE_LOG_IN_FAIL_INVALID_IP:
    case RECEIVE_LOG_IN_FAIL_CHARGED_CHANNEL:
        rUIMng.ShowWin(rUIMng.m_LoginWin.get());
        LegacyUiManager().m_LoginWin->FocusAccountInput();
        CurrentProtocolState = RECEIVE_JOIN_SERVER_SUCCESS;
        break;
    case MESSAGE_INPUT_PASSWORD:
    case RECEIVE_LOG_IN_FAIL_PASSWORD:
        rUIMng.ShowWin(rUIMng.m_LoginWin.get());
        LegacyUiManager().m_LoginWin->FocusPasswordInput();
        CurrentProtocolState = RECEIVE_JOIN_SERVER_SUCCESS;
        break;
    case MESSAGE_DELETE_CHARACTER_CONFIRM:
        PopUp(MESSAGE_DELETE_CHARACTER_RESIDENT);
        break;
    case MESSAGE_DELETE_CHARACTER_RESIDENT:
        RequestDeleteCharacter();
        PopUp(MESSAGE_WAIT);
        break;
    }
}

void CMsgWin::ManageCancelClick()
{
    CUIMng &rUIMng = LegacyUiManager();
    m_nMsgCode = -1;
    rUIMng.HideWin(this);
}

void CMsgWin::InitResidentNumInput()
{
    ClearInput();
    m_panel.SetPassword(L"");
    m_panel.FocusPasswordInput();
}

void CMsgWin::RequestDeleteCharacter()
{
    wcsncpy_s(InputText[0], _countof(InputText[0]), m_panel.Password().c_str(), _TRUNCATE);
    m_panel.SetPassword(L"");
    InputEnable = false;
    CurrentProtocolState = REQUEST_DELETE_CHARACTER;
    SocketClient->ToGameServer()->SendDeleteCharacter(CharactersClient[SelectedHero].ID,
                                                      InputText[0]);
}

//*****************************************************************************
//*****************************************************************************

CServerMsgWin::CServerMsgWin(SessionKeeper &keeper) : CWin(keeper), panel_(keeper)
{
}

CServerMsgWin::~CServerMsgWin() = default;

void CServerMsgWin::Create()
{
    CWin::Create(UI::Modern::PC::ServerMessage::RmlServerMessagePanel::Width(),
                 UI::Modern::PC::ServerMessage::RmlServerMessagePanel::Height(), -2);
    content_ = {};
    panel_.Show(false);
}

bool CServerMsgWin::CursorInWin(int nArea)
{
    if (!CWin::m_bShow)
        return false;

    return nArea != WA_ALL && CWin::CursorInWin(nArea);
}

void CServerMsgWin::AddMsg(wchar_t *pszMsg)
{
    if (content_.lineCount == content_.lines.size())
    {
        std::rotate(content_.lines.begin(), content_.lines.begin() + 1, content_.lines.end());
    }
    else
    {
        ++content_.lineCount;
    }

    content_.lines[content_.lineCount - 1] = pszMsg != nullptr ? pszMsg : L"";
    Show(true);
}

void CServerMsgWin::PreRelease()
{
    panel_.Release();
}

//*****************************************************************************
//*****************************************************************************

namespace SystemMenu = UI::Modern::PC::SystemMenu;

CSysMenuWin::CSysMenuWin(SessionKeeper &keeper) : CWin(keeper), m_modernPanel(keeper)
{
}

CSysMenuWin::~CSysMenuWin() = default;

void CSysMenuWin::Create()
{
    mode_ = SceneFlag == LOG_IN_SCENE ? SystemMenu::RmlSystemMenuMode::Login
                                      : SystemMenu::RmlSystemMenuMode::Character;
    CWin::Create(SystemMenu::RmlSystemMenuPanel::Width(),
                 SystemMenu::RmlSystemMenuPanel::HeightFor(mode_), -2);
    m_modernPanel.Create(mode_);
    SetPosition((static_cast<int>(ModernUiViewportWidth()) - GetWidth()) / 2,
                (static_cast<int>(ModernUiViewportHeight()) - GetHeight()) / 2);
}

void CSysMenuWin::PreRelease()
{
    m_modernPanel.Release();
}

bool CSysMenuWin::CursorInWin(int area)
{
    if (!CWin::m_bShow)
    {
        return false;
    }
    return area != WA_MOVE && CWin::CursorInWin(area);
}

bool CSysMenuWin::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsShow() && m_modernPanel.ProcessInput(event);
}

void CSysMenuWin::UpdateWhileShow(double)
{
    if (m_modernPanel.ExitButton().IsClick())
    {
        ExitGame();
    }
    else if (m_modernPanel.ServerButton().IsClick())
    {
        SelectServer();
    }
    else if (m_modernPanel.OptionButton().IsClick())
    {
        OpenOptions();
    }
    else if (m_modernPanel.CloseButton().IsClick())
    {
        LegacyUiManager().HideWin(this);
    }
    // ESC toggling is owned by CUIMng::Update().
}

void CSysMenuWin::ExitGame()
{
    g_ErrorReport.Write(L"> Menu - Exit game.");
    g_ErrorReport.WriteCurrentTime();
    LegacyUiManager().HideWin(this);
    sessionKeeper_.RequestDiscardSession();
}

void CSysMenuWin::SelectServer()
{
    g_ErrorReport.Write(L"> Menu - Join another server.");
    g_ErrorReport.WriteCurrentTime();
    LogOut = true;
    SocketClient->ToGameServer()->SendLogOut(LogOutType::BackToServerSelection);
    g_ConsoleDebug.Write(MCD_SEND, L"0xF1 [SendRequestLogOut] 2");

    CUIMng &ui = LegacyUiManager();
    ui.HideWin(this);
    ui.HideWin(ui.m_CharSelMainWin.get());
}

void CSysMenuWin::OpenOptions()
{
    LegacyUiManager().HideWin(this);
    g_pNewUISystem->Show(SEASON3B::INTERFACE_OPTION);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void RenderColor(float x, float y, float Width, float Height, float Alpha, int Flag);

////////////////////////////////////////////////////////////////////////////////////////////////////

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
////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////

void CUITextInputWindow::InitControls()
{
    m_TextInputBox.Init(80, 14, 10);
    m_TextInputBox.SetParentUIID(m_dwUIID);
    m_TextInputBox.SetFont(LegacyFontRole::Normal);
    m_TextInputBox.SetOption(UIOPTION_PAINTBACK);
    m_TextInputBox.SetBackColor(0, 0, 0, 0);
    m_TextInputBox.SetParentUIID(GetUIID());
    m_TextInputBox.SetArrangeType(0, 30, 14);
    m_TextInputBox.SetState(UISTATE_NORMAL);

    m_AddButton.Init(1, I18N::Game::OK);
    m_AddButton.SetParentUIID(GetUIID());
    m_AddButton.SetArrangeType(0, 18, 40);
    m_AddButton.SetSize(50, 20);

    m_CancelButton.Init(2, I18N::Game::Cancel);
    m_CancelButton.SetParentUIID(GetUIID());
    m_CancelButton.SetArrangeType(0, 73, 40);
    m_CancelButton.SetSize(50, 20);
    Refresh();
}

void CUITextInputWindow::Init(const wchar_t *pszTitle, DWORD dwParentID)
{
    SetTitle(pszTitle);
    SetParentUIID(0);
    m_dwReturnWindowUIID = dwParentID;

    SetPosition(50, 50);
    //SetSize(213, 170);
    SetSize(150, 100);
    SetLimitSize(150, 100);
    SetOption(UIWINDOWSTYLE_FIXED);
    m_ModernPanel.CreateS16Caution();
    m_ModernPanel.SetMode(UI::Modern::RmlMessageBoxMode::Text);
    m_ModernPanel.SetInputValue(L"");
    m_ModernPanel.Show(true);
    m_ModernPanel.FocusInput();
}

bool CUITextInputWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    const bool consumed = m_ModernPanel.ProcessInput(event);
    if (consumed && event.action == SessionInputAction::KeyDown &&
        (event.code == SDL_SCANCODE_RETURN || event.code == SDL_SCANCODE_KP_ENTER) &&
        m_ModernPanel.TextInputArea())
    {
        m_TextInputBox.SetText(m_ModernPanel.InputValue().c_str());
        SendUIMessageDirect(UI_MESSAGE_TEXTINPUT, 0, 0);
    }
    return consumed;
}

std::optional<UI::Modern::RmlTextInputArea> CUITextInputWindow::ModernTextInputArea() const
{
    return m_ModernPanel.TextInputArea();
}

void CUITextInputWindow::Refresh()
{
    m_AddButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_CancelButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_TextInputBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);

    m_TextInputBox.GiveFocus();
    m_bHaveTextBox = TRUE;
}

void CUITextInputWindow::ReturnText()
{
    wchar_t returnText[MAX_TEXT_LENGTH + 1]{};
    m_TextInputBox.GetText(returnText, MAX_TEXT_LENGTH + 1);
    m_TextInputBox.SetText(NULL);
    if (returnText[0] == L'\0')
        return;

    g_pWindowMgr->SendUIMessageToWindow(m_dwReturnWindowUIID, UI_MESSAGE_TXTRETURN, GetUIID(), 0,
                                        returnText);
    g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, GetUIID(), 0);
}

BOOL CUITextInputWindow::HandleMessage()
{
    switch (m_WorkMessage.m_iMessage)
    {
    case UI_MESSAGE_SELECTED:
        m_TextInputBox.GiveFocus();
        break;
    case UI_MESSAGE_TEXTINPUT: {
        ReturnText();
    }
    break;
    case UI_MESSAGE_BTNLCLICK: {
        switch (m_WorkMessage.m_iParam1)
        {
        case 1:
            ReturnText();
            break;
        case 2:
            g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, GetUIID(), 0);
            break;
        default:
            break;
        }
    }
    break;
    default:
        break;
    }
    return FALSE;
}

void CUITextInputWindow::DoActionSub(BOOL bMessageOnly)
{
    m_AddButton.DoAction(bMessageOnly);
    m_CancelButton.DoAction(bMessageOnly);
    m_TextInputBox.DoAction(bMessageOnly);
}

void CUITextInputWindow::DoMouseActionSub()
{
    //	if (g_dwMouseUseUIID == GetUIID() && MouseLButton == true)
    //	{
    //		m_TextInputBox.GiveFocus();
    //	}
}

void CUIQuestionWindow::Init(const wchar_t *pszTitle, DWORD dwParentID)
{
    if (m_iDialogType == 0)
        SetTitle(I18N::Game::Question);
    else if (m_iDialogType == 1)
        SetTitle(I18N::Game::OK);
    SetParentUIID(0);
    m_dwReturnWindowUIID = dwParentID;
    memset(m_szCaption, 0, sizeof(m_szCaption));
    memset(m_szSaveID, 0, sizeof(m_szSaveID));
    CutText3(pszTitle, m_szCaption[0], 125, 2, 256);

    SetPosition(50, 50);
    //SetSize(213, 170);
    SetSize(150, 100);
    SetLimitSize(150, 100);
    SetOption(UIWINDOWSTYLE_FIXED);

    if (m_iDialogType == 0)
    {
        m_AddButton.Init(1, I18N::Game::Yes);
        m_AddButton.SetParentUIID(GetUIID());
        m_AddButton.SetArrangeType(0, 18, 40);
        m_AddButton.SetSize(50, 20);

        m_CancelButton.Init(2, I18N::Game::No);
        m_CancelButton.SetParentUIID(GetUIID());
        m_CancelButton.SetArrangeType(0, 73, 40);
        m_CancelButton.SetSize(50, 20);
    }
    else if (m_iDialogType == 1)
    {
        m_AddButton.Init(1, I18N::Game::OK);
        m_AddButton.SetParentUIID(GetUIID());
        m_AddButton.SetArrangeType(0, 45, 40);
        m_AddButton.SetSize(50, 20);
    }
    m_ModernPanel.CreateS16Caution();
    m_ModernPanel.SetMode(m_iDialogType == 0 ? UI::Modern::RmlMessageBoxMode::OkCancel
                                             : UI::Modern::RmlMessageBoxMode::Ok);
    m_ModernPanel.Show(true);
}

bool CUIQuestionWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernPanel.ProcessInput(event);
}

void CUIQuestionWindow::Refresh()
{
    m_AddButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_CancelButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);

    KeyState[VK_RETURN] = true;
}

BOOL CUIQuestionWindow::HandleMessage()
{
    switch (m_WorkMessage.m_iMessage)
    {
    case UI_MESSAGE_SELECTED:
        break;
    case UI_MESSAGE_BTNLCLICK: {
        switch (m_WorkMessage.m_iParam1)
        {
        case 1:
            if (m_dwReturnWindowUIID == -1)
            {
                SocketClient->ToGameServer()->SendFriendAddResponse(0x01, m_szSaveID);
            }
            else if (m_dwReturnWindowUIID != 0)
            {
                g_pWindowMgr->SendUIMessageToWindow(m_dwReturnWindowUIID, UI_MESSAGE_YNRETURN,
                                                    GetUIID(), 1);
            }
            g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, GetUIID(), 0);
            break;
        case 2:
            if (m_iDialogType != 0)
                break;
            if (m_dwReturnWindowUIID == -1)
            {
                SocketClient->ToGameServer()->SendFriendAddResponse(0x00, m_szSaveID);
            }
            else if (m_dwReturnWindowUIID != 0)
            {
                g_pWindowMgr->SendUIMessageToWindow(m_dwReturnWindowUIID, UI_MESSAGE_YNRETURN,
                                                    GetUIID(), 0);
            }
            g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, GetUIID(), 0);
            break;
        default:
            break;
        }
    }
    break;
    default:
        break;
    }
    return FALSE;
}

void CUIQuestionWindow::DoActionSub(BOOL bMessageOnly)
{
    if (g_pWindowMgr->GetTopWindowUIID() == GetUIID() && PressKey(VK_RETURN))
    {
        SendUIMessage(UI_MESSAGE_BTNLCLICK, 1, 0);
    }

    m_AddButton.DoAction(bMessageOnly);
    m_CancelButton.DoAction(bMessageOnly);
}

void CUIQuestionWindow::SaveID(const wchar_t *pszText)
{
    if (pszText[0] != '\0')
    {
        wcsncpy(m_szSaveID, pszText, MAX_USERNAME_SIZE);
        m_szSaveID[MAX_USERNAME_SIZE] = '\0';
    }
    else
        m_szSaveID[0] = '\0';
}

//////////////////////////////////////////////////////////////////////

extern int DoBreakUpGuildAction_New(POPUP_RESULT Result);

SEASON3B::CNewUIMessageBoxButton::CNewUIMessageBoxButton(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper)
{
    m_bEnable = true;

    m_dwTexType = 0;
    m_x = m_y = m_width = m_height = 0.f;

    m_EventState = EVENT_NONE;
    m_dwSizeType = MSGBOX_BTN_SIZE_OK;

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    m_iMoveTextPosX = 0;
    m_iMoveTextPosY = 0;
    m_bClickEffect = false;
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
}

SEASON3B::CNewUIMessageBoxButton::~CNewUIMessageBoxButton()
{
}

bool SEASON3B::CNewUIMessageBoxButton::IsMouseIn()
{
    if (m_bEnable == false)
        return false;

    return CheckMouseIn(m_x, m_y, m_width, m_height);
}

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
void SEASON3B::CNewUIMessageBoxButton::MoveTextPos(int iX, int iY)
{
    m_iMoveTextPosX = iX;
    m_iMoveTextPosY = iY;
}

void SEASON3B::CNewUIMessageBoxButton::SetInfo(DWORD dwTexType, float x, float y, float width,
                                               float height, DWORD dwSizeType, bool bClickEffect)
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
void SEASON3B::CNewUIMessageBoxButton::SetInfo(DWORD dwTexType, float x, float y, float width,
                                               float height, DWORD dwSizeType)
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
{
    m_dwTexType = dwTexType;
    m_dwSizeType = dwSizeType;
    m_x = x;
    m_y = y;
    m_width = width;
    m_height = height;
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    m_bClickEffect = bClickEffect;

    if (m_dwSizeType == MSGBOX_BTN_SIZE_OK)
    {
        m_fButtonWidth = MSGBOX_BTN_WIDTH;
        m_fButtonHeight = MSGBOX_BTN_HEIGHT;
    }
    else if (m_dwSizeType == MSGBOX_BTN_SIZE_EMPTY)
    {
        m_fButtonWidth = MSGBOX_BTN_EMPTY_WIDTH;
        m_fButtonHeight = MSGBOX_BTN_EMPTY_HEIGHT;
    }
    else if (m_dwSizeType == MSGBOX_BTN_SIZE_EMPTY_SMALL)
    {
        m_fButtonWidth = MSGBOX_BTN_EMPTY_SMALL_WIDTH;
        m_fButtonHeight = MSGBOX_BTN_EMPTY_HEIGHT;
    }
    else if (m_dwSizeType == MSGBOX_BTN_SIZE_EMPTY_BIG)
    {
        m_fButtonWidth = MSGBOX_BTN_EMPTY_BIG_WIDTH;
        m_fButtonHeight = MSGBOX_BTN_EMPTY_HEIGHT;
    }
    else
    {
        m_fButtonWidth = width;
        m_fButtonHeight = height;
    }
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
}

void SEASON3B::CNewUIMessageBoxButton::AddBlank(int iAddBlank)
{
    m_y += iAddBlank;
}

void SEASON3B::CNewUIMessageBoxButton::Update()
{
    if (m_bEnable == false)
        return;

    if (m_EventState == EVENT_NONE && MouseLButtonPush == false && IsMouseIn() == true)
    {
        m_EventState = EVENT_BTN_HOVER;
        return;
    }
    if (m_EventState == EVENT_BTN_HOVER && MouseLButtonPush == false && IsMouseIn() == false)
    {
        m_EventState = EVENT_NONE;
        return;
    }
    if (m_EventState == EVENT_BTN_HOVER)
    {
        if (MouseLButtonPush == true)
        {
            if (IsMouseIn() == true)
            {
                m_EventState = EVENT_BTN_DOWN;
                return;
            }
        }
    }
    if (m_EventState == EVENT_BTN_DOWN && MouseLButtonPush == false)
    {
        m_EventState = EVENT_NONE;
        return;
    }
}

SEASON3B::CNewUICommonMessageBox::CNewUICommonMessageBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper)
{
}

SEASON3B::CNewUICommonMessageBox::~CNewUICommonMessageBox() = default;

DWORD SEASON3B::CNewUICommonMessageBox::GetType()
{
    return m_dwType;
}

bool SEASON3B::CNewUICommonMessageBox::Create(DWORD dwType, float fPriority)
{
    m_dwType = dwType;
    m_s16Title.clear();
    m_s16Message.clear();

    SetAddCallbackFunc();

    const int width = UI::Modern::RmlMessageBoxPanel::CautionWidth();
    const int height = UI::Modern::RmlMessageBoxPanel::CautionHeight();
    const int x = (static_cast<int>(ModernUiViewportWidth()) - width) / 2;
    const int y = (static_cast<int>(ModernUiViewportHeight()) - height) / 2;

    if (!CNewUIMessageBoxBase::Create(x, y, width, height, fPriority))
        return false;

    UI::Modern::RmlMessageBoxPanel &panel = g_MessageBox.ModernMessageBoxPanel();
    panel.CreateS16Caution();
    panel.SetPosition(x, y);
    panel.SetMode(m_dwType == MSGBOX_COMMON_TYPE_OK ? UI::Modern::RmlMessageBoxMode::Ok
                                                    : UI::Modern::RmlMessageBoxMode::OkCancel);
    panel.OkButton().SetEnable(true);
    panel.CancelButton().SetEnable(true);

    return true;
}

bool SEASON3B::CNewUICommonMessageBox::Create(DWORD dwType, const type_string &strMsg,
                                              DWORD dwColor, BYTE byFontType, float fPriority)
{
    if (!Create(dwType, fPriority))
        return false;
    AddMsg(strMsg, dwColor, byFontType);
    return true;
}

void SEASON3B::CNewUICommonMessageBox::UseS16Caution(const type_string &title,
                                                     const type_string &message)
{
    m_s16Title = title;
    m_s16Message = message;
}

void SEASON3B::CNewUICommonMessageBox::SetAddCallbackFunc()
{
    switch (m_dwType)
    {
    case MSGBOX_COMMON_TYPE_OK:
        AddCallbackFunc(BindSelf(&SEASON3B::CNewUICommonMessageBox::Close),
                        MSGBOX_EVENT_USER_COMMON_OK);
        AddCallbackFunc(BindSelf(&SEASON3B::CNewUICommonMessageBox::Close),
                        MSGBOX_EVENT_PRESSKEY_ESC);
        //AddCallbackFunc(BindSelf(&SEASON3B::CNewUICommonMessageBox::Close), MSGBOX_EVENT_PRESSKEY_RETURN);
        break;
    case MSGBOX_COMMON_TYPE_OKCANCEL:
        AddCallbackFunc(BindSelf(&SEASON3B::CNewUICommonMessageBox::Close),
                        MSGBOX_EVENT_USER_COMMON_OK);
        AddCallbackFunc(BindSelf(&SEASON3B::CNewUICommonMessageBox::Close),
                        MSGBOX_EVENT_USER_COMMON_CANCEL);
        //AddCallbackFunc(BindSelf(&SEASON3B::CNewUICommonMessageBox::Close), MSGBOX_EVENT_PRESSKEY_ESC);
        //AddCallbackFunc(BindSelf(&SEASON3B::CNewUICommonMessageBox::Close), MSGBOX_EVENT_PRESSKEY_RETURN);
        break;
    }
}

void SEASON3B::CNewUICommonMessageBox::AddMsg(const type_string &strMsg, DWORD, BYTE)
{
    if (!m_s16Message.empty())
    {
        m_s16Message += L'\n';
    }
    m_s16Message += strMsg;
}

bool SEASON3B::CNewUICommonMessageBox::Update()
{
    return UpdateS16Caution();
}

bool SEASON3B::CNewUICommonMessageBox::UpdateS16Caution()
{
    UI::Modern::RmlMessageBoxPanel &panel = g_MessageBox.ModernMessageBoxPanel();
    if (panel.OkButton().IsClick())
    {
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_OK);
    }
    else if (panel.CancelButton().IsClick())
    {
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
    }
    return true;
}

bool SEASON3B::CNewUICommonMessageBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return g_MessageBox.ModernMessageBoxPanel().ProcessInput(event);
}

void SEASON3B::CNewUICommonMessageBox::LockOkButton()
{
    g_MessageBox.ModernMessageBoxPanel().OkButton().SetEnable(false);
}

CALLBACK_RESULT SEASON3B::CNewUICommonMessageBox::Close(class CNewUIMessageBoxBase *pOwner,
                                                        const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

SEASON3B::CNewUI3DItemCommonMsgBox::CNewUI3DItemCommonMsgBox(SessionKeeper &keeper)
    : CNewUIMessageBoxBase(keeper), panel_(keeper, "item_confirmation.rml")
{
    ZeroMemory(&m_Item, sizeof(m_Item));
    m_iItemValue = 0;
}

SEASON3B::CNewUI3DItemCommonMsgBox::~CNewUI3DItemCommonMsgBox()
{
    Release();
}

DWORD SEASON3B::CNewUI3DItemCommonMsgBox::GetType()
{
    return m_dwType;
}

bool SEASON3B::CNewUI3DItemCommonMsgBox::Create(DWORD dwType, float fPriority)
{
    m_dwType = dwType;
    message_.clear();
    SetAddCallbackFunc();
    panel_.SetButtonVisible("btnItemCancel", dwType == MSGBOX_COMMON_TYPE_OKCANCEL);
    if (!CNewUIMessageBoxBase::Create(0, 0, 0, 0, fPriority))
        return false;
    if (g_pNewUI3DRenderMng)
        g_pNewUI3DRenderMng->Add3DRenderObj(this);
    return true;
}

bool SEASON3B::CNewUI3DItemCommonMsgBox::Create(DWORD dwType, const type_string &strMsg,
                                                DWORD dwColor, BYTE byFontType, float fPriority)
{
    if (!Create(dwType, fPriority))
        return false;
    AddMsg(strMsg, dwColor, byFontType);
    return true;
}
void SEASON3B::CNewUI3DItemCommonMsgBox::Release()
{
    if (g_pNewUI3DRenderMng)
        g_pNewUI3DRenderMng->Remove3DRenderObj(this);
    panel_.Release();
    message_.clear();
    CNewUIMessageBoxBase::Release();
}

void SEASON3B::CNewUI3DItemCommonMsgBox::Set3DItem(ITEM *pItem)
{
    if (pItem)
    {
        memcpy(&m_Item, pItem, sizeof(ITEM));
    }
}

void SEASON3B::CNewUI3DItemCommonMsgBox::SetItemValue(int iValue)
{
    m_iItemValue = iValue;
}

int SEASON3B::CNewUI3DItemCommonMsgBox::GetItemValue()
{
    return m_iItemValue;
}

void SEASON3B::CNewUI3DItemCommonMsgBox::AddMsg(const type_string &strMsg, DWORD dwColor,
                                                BYTE byFontType)
{
    if (!message_.empty())
        message_ += L'\n';
    message_ += strMsg;
    panel_.SetText("taItemMent-label", message_);
}

CALLBACK_RESULT SEASON3B::CNewUI3DItemCommonMsgBox::Close(class CNewUIMessageBoxBase *pOwner,
                                                          const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

bool SEASON3B::CNewUI3DItemCommonMsgBox::Update()
{
    panel_.SetText("btnItemOk-label", I18N::Game::OK);
    panel_.SetText("btnItemCancel-label", I18N::Game::Cancel);
    if (panel_.TakeClick("btnItemOk"))
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_OK);
    else if (panel_.TakeClick("btnItemCancel") && m_dwType == MSGBOX_COMMON_TYPE_OKCANCEL)
        SendEvent(this, MSGBOX_EVENT_USER_COMMON_CANCEL);
    return true;
}

void SEASON3B::CNewUI3DItemCommonMsgBox::SetAddCallbackFunc()
{
    switch (m_dwType)
    {
    case MSGBOX_COMMON_TYPE_OK:
        AddCallbackFunc(BindSelf(&SEASON3B::CNewUI3DItemCommonMsgBox::Close),
                        MSGBOX_EVENT_USER_COMMON_OK);
        AddCallbackFunc(BindSelf(&SEASON3B::CNewUI3DItemCommonMsgBox::Close),
                        MSGBOX_EVENT_PRESSKEY_ESC);
        //AddCallbackFunc(BindSelf(&SEASON3B::CNewUICommonMessageBox::Close), MSGBOX_EVENT_PRESSKEY_RETURN);
        break;
    case MSGBOX_COMMON_TYPE_OKCANCEL:
        AddCallbackFunc(BindSelf(&SEASON3B::CNewUI3DItemCommonMsgBox::Close),
                        MSGBOX_EVENT_USER_COMMON_OK);
        AddCallbackFunc(BindSelf(&SEASON3B::CNewUI3DItemCommonMsgBox::Close),
                        MSGBOX_EVENT_USER_COMMON_CANCEL);
        //AddCallbackFunc(BindSelf(&SEASON3B::CNewUICommonMessageBox::Close), MSGBOX_EVENT_PRESSKEY_ESC);
        //AddCallbackFunc(BindSelf(&SEASON3B::CNewUICommonMessageBox::Close), MSGBOX_EVENT_PRESSKEY_RETURN);
        break;
    }
}

bool SEASON3B::CNewUI3DItemCommonMsgBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    return panel_.ProcessInput(event);
}

bool SEASON3B::CNewUI3DItemCommonMsgBox::IsVisible() const
{
    return true;
}

CALLBACK_RESULT SEASON3B::CServerLostMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    SendMessage(g_hWnd, WM_DESTROY, 0, 0);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_POP_ALL_EVENTS;
}

CALLBACK_RESULT SEASON3B::CGuildRequestMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                               const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendGuildJoinResponse(true, GuildPlayerKey);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGuildRequestMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendGuildJoinResponse(false, GuildPlayerKey);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGuildFireMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                            const leaf::xstreambuf &xParam)
{
    DeleteGuildIndex = s_nTargetFireMemberIndex;
    PlayBuffer(SOUND_CLICK01);
    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CMapEnterWerwolfMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    DWORD dwGold = CharacterMachine->Gold;

    if (dwGold >= 3000000)
    {
        SocketClient->ToGameServer()->SendEnterOnWerewolfRequest();
    }
    else
    {
        g_pSystemLogBox->AddText(I18N::Game::YouAreShortOfZen, SEASON3B::TYPE_ERROR_MESSAGE);
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT CMapEnterGateKeeperMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                           const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendEnterOnGatekeeperRequest();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CPartyMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                        const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendPartyInviteResponse(true, PartyKey);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CPartyMsgBoxLayout::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                            const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendPartyInviteResponse(false, PartyKey);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CTradeMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                        const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendTradeRequestResponse(true);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CTradeMsgBoxLayout::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                            const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendTradeRequestResponse(false);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CTradeAlertMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    g_pTrade->AlertTrade();
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CTradeAlertMsgBoxLayout::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                 const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGuildWarMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                           const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendGuildWarResponse(true);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGuildWarMsgBoxLayout::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                               const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendGuildWarResponse(false);
    InitGuildWar();
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CBattleSoccerMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                               const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendGuildWarResponse(true);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CBattleSoccerMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendGuildWarResponse(false);
    InitGuildWar();
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CServerImmigrationErrorMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CPersonalshopCreateMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    wchar_t shopTitle[MAX_SHOPTITLE]{};
    g_pMyShopInventory->GetTitle(shopTitle);
    wcscpy(g_szPersonalShopTitle, shopTitle);
    SocketClient->ToGameServer()->SendPlayerShopOpen(shopTitle);

    g_pNewUISystem->Hide(SEASON3B::INTERFACE_MYSHOP_INVENTORY);
    g_pNewUISystem->Hide(SEASON3B::INTERFACE_INVENTORY);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CPersonalshopCreateMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CFenrirRepairMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                               const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CFenrirRepairMsgBox *>(pOwner);
    if (pMsgBox == nullptr)
    {
        return CALLBACK_CONTINUE;
    }

    SendRequestUse(pMsgBox->GetSourceIndex(), pMsgBox->GetTargetIndex());
    g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CFenrirRepairMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

void SEASON3B::CFenrirRepairMsgBox::SetSourceIndex(int iIndex)
{
    m_iSourceIndex = iIndex;
}

void SEASON3B::CFenrirRepairMsgBox::SetTargetIndex(int iIndex)
{
    m_iTargetIndex = iIndex;
}

int SEASON3B::CFenrirRepairMsgBox::GetSourceIndex()
{
    return m_iSourceIndex;
}

int SEASON3B::CFenrirRepairMsgBox::GetTargetIndex()
{
    return m_iTargetIndex;
}

CALLBACK_RESULT SEASON3B::CInfinityArrowCancelMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendMagicEffectCancelRequest(g_iCancelSkillTarget, HeroKey);
    g_iCancelSkillTarget = 0;

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CInfinityArrowCancelMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    g_iCancelSkillTarget = 0;

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CBuffSwellOfMPCancelMsgBoxLayOut::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendMagicEffectCancelRequest(g_iCancelSkillTarget, HeroKey);
    g_iCancelSkillTarget = 0;

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CBuffSwellOfMPCancelMsgBoxLayOut::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    g_iCancelSkillTarget = 0;

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGemIntegrationUnityCheckMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    SessionOrigin().Gameplay()->ProcessCSAction();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    SessionOrigin().Gameplay()->Exit();

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGemIntegrationUnityCheckMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    SessionOrigin().Gameplay()->GetBack();

    SEASON3B::CreateMessageBox(
        MSGBOX_LAYOUT_CLASS(CGemIntegrationUnityMsgBoxLayout, pOwner->MessageBoxSessionOrigin()));

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGemIntegrationUnityResultMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    SessionOrigin().Gameplay()->Exit();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGemIntegrationDisjointCheckMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    SessionOrigin().Gameplay()->ProcessCSAction();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    SessionOrigin().Gameplay()->Exit();

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGemIntegrationDisjointCheckMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    SessionOrigin().Gameplay()->GetBack();

    SEASON3B::CreateMessageBox(MSGBOX_LAYOUT_CLASS(CGemIntegrationDisjointMsgBoxLayout,
                                                   pOwner->MessageBoxSessionOrigin()));

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGemIntegrationDisjointResultMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    SessionOrigin().Gameplay()->Exit();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CChaosCastleTimeCheckMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    ITEM *pItem = g_pMyInventory->GetStandbyItem();
    if (pItem)
    {
        int iSrcIndex = g_pMyInventory->GetStandbyItemIndex();
        SocketClient->ToGameServer()->SendChaosCastleEnterRequest(pItem->Level, iSrcIndex);
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CChaosCastleTimeCheckMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CHarvestEventLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                         const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendLeoHelperItemRequest();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CHarvestEventLayout::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CWhiteAngelEventLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                            const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendWhiteAngelItemRequest();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CWhiteAngelEventLayout::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CLuckyItemMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                            const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendChaosMachineMixRequest(
        static_cast<ChaosMachineMixType>(g_pLuckyItemWnd->SetActAction()), 0);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CLuckyItemMsgBoxLayout::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CMixCheckMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                           const leaf::xstreambuf &xParam)
{
    g_pMixInventory->SetMixState(SEASON3B::CNewUIMixInventory::MIX_REQUESTED);
    SocketClient->ToGameServer()->SendChaosMachineMixRequest(
        static_cast<ChaosMachineMixType>(g_MixRecipeMgr.GetCurMixID()),
        g_MixRecipeMgr.GetMixSubType());

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CMixCheckMsgBoxLayout::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                               const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CUseReviveCharmMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                 const leaf::xstreambuf &xParam)
{
    BYTE srcIndex = g_pMyInventory->GetStandbyItemIndex();
    SendRequestUse(srcIndex, 0);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CUseReviveCharmMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CUsePortalCharmMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                 const leaf::xstreambuf &xParam)
{
    g_PortalMgr.SavePortalPosition();
    BYTE srcIndex = g_pMyInventory->GetStandbyItemIndex();
    SendRequestUse(srcIndex, 0);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CUsePortalCharmMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CReturnPortalCharmMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    BYTE srcIndex = g_pMyInventory->GetStandbyItemIndex();
    SendRequestUse(srcIndex, 0);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CReturnPortalCharmMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CDuelCreateErrorMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CDuelWatchErrorMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                 const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CDoppelGangerMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                               const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGuildRelationShipMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    const SEASON3B::ServerMessageInfo info = g_pGuildInfoWindow->GetServerMessage();

    SocketClient->ToGameServer()->SendGuildRelationshipChangeResponse(
        info.s_byRelationShipType, info.s_byRelationShipRequestType, 0x01,
        MAKEWORD(info.s_byTargetUserIndexL, info.s_byTargetUserIndexH));

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGuildRelationShipMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    const SEASON3B::ServerMessageInfo info = g_pGuildInfoWindow->GetServerMessage();

    SocketClient->ToGameServer()->SendGuildRelationshipChangeResponse(
        info.s_byRelationShipType, info.s_byRelationShipRequestType, 0x00,
        MAKEWORD(info.s_byTargetUserIndexL, info.s_byTargetUserIndexH));

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CCastleMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                         const leaf::xstreambuf &xParam)
{
    switch (g_pCastleWindow->GetCurrMsgBoxRequest())
    {
    case SEASON3B::CNewUICastleWindow::CASTLE_MSGREQ_BUY_GATE:
    case SEASON3B::CNewUICastleWindow::CASTLE_MSGREQ_REPAIR_GATE: {
        PMSG_NPCDBLIST &info = g_SenatusInfo.GetCurrGateInfo();
        if (info.btNpcLive == 0)
            SocketClient->ToGameServer()->SendCastleSiegeDefenseBuyRequest(info.iNpcNumber,
                                                                           info.iNpcIndex);
        else
            SocketClient->ToGameServer()->SendCastleSiegeDefenseRepairRequest(info.iNpcNumber,
                                                                              info.iNpcIndex);
        break;
    }
    case SEASON3B::CNewUICastleWindow::CASTLE_MSGREQ_UPGRADE_GATE_HP: {
        PMSG_NPCDBLIST &info = g_SenatusInfo.GetCurrGateInfo();
        const int next = g_SenatusInfo.GetHP(info.iNpcNumber, g_SenatusInfo.GetHPLevel(&info) + 1);
        SocketClient->ToGameServer()->SendCastleSiegeDefenseUpgradeRequest(info.iNpcNumber,
                                                                           info.iNpcIndex, 3, next);
        break;
    }
    case SEASON3B::CNewUICastleWindow::CASTLE_MSGREQ_UPGRADE_GATE_DEFENSE: {
        PMSG_NPCDBLIST &info = g_SenatusInfo.GetCurrGateInfo();
        SocketClient->ToGameServer()->SendCastleSiegeDefenseUpgradeRequest(
            info.iNpcNumber, info.iNpcIndex, 1, g_SenatusInfo.GetDefenseLevel(&info) + 1);
        break;
    }
    case SEASON3B::CNewUICastleWindow::CASTLE_MSGREQ_BUY_STATUE:
    case SEASON3B::CNewUICastleWindow::CASTLE_MSGREQ_REPAIR_STATUE: {
        PMSG_NPCDBLIST &info = g_SenatusInfo.GetCurrStatueInfo();
        if (info.btNpcLive == 0)
            SocketClient->ToGameServer()->SendCastleSiegeDefenseBuyRequest(info.iNpcNumber,
                                                                           info.iNpcIndex);
        else
            SocketClient->ToGameServer()->SendCastleSiegeDefenseRepairRequest(info.iNpcNumber,
                                                                              info.iNpcIndex);
        break;
    }
    case SEASON3B::CNewUICastleWindow::CASTLE_MSGREQ_UPGRADE_STATUE_HP: {
        PMSG_NPCDBLIST &info = g_SenatusInfo.GetCurrStatueInfo();
        const int next = g_SenatusInfo.GetHP(info.iNpcNumber, g_SenatusInfo.GetHPLevel(&info) + 1);
        SocketClient->ToGameServer()->SendCastleSiegeDefenseUpgradeRequest(info.iNpcNumber,
                                                                           info.iNpcIndex, 3, next);
        break;
    }
    case SEASON3B::CNewUICastleWindow::CASTLE_MSGREQ_UPGRADE_STATUE_DEFENSE: {
        PMSG_NPCDBLIST &info = g_SenatusInfo.GetCurrStatueInfo();
        SocketClient->ToGameServer()->SendCastleSiegeDefenseUpgradeRequest(
            info.iNpcNumber, info.iNpcIndex, 1, g_SenatusInfo.GetDefenseLevel(&info) + 1);
        break;
    }
    case SEASON3B::CNewUICastleWindow::CASTLE_MSGREQ_UPGRADE_STATUE_RECOVER: {
        PMSG_NPCDBLIST &info = g_SenatusInfo.GetCurrStatueInfo();
        SocketClient->ToGameServer()->SendCastleSiegeDefenseUpgradeRequest(
            info.iNpcNumber, info.iNpcIndex, 2, g_SenatusInfo.GetRecoverLevel(&info) + 1);
        break;
    }
    case SEASON3B::CNewUICastleWindow::CASTLE_MSGREQ_APPLY_TAX:
        SocketClient->ToGameServer()->SendCastleSiegeTaxChangeRequest(
            1, g_SenatusInfo.GetChaosTaxRate());
        SocketClient->ToGameServer()->SendCastleSiegeTaxChangeRequest(
            2, g_SenatusInfo.GetNormalTaxRate());
        break;
    default:
        break;
    };

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CCastleMsgBoxLayout::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CSiegeLevelMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CSiegeGiveUpMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendCastleSiegeUnregisterRequest();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CSiegeGiveUpMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGatemanMoneyMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                               const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGatemanFailMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CQuestGiveUpMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    const DWORD dwSelectedQuest = selectedQuest_;
    const auto questNumber = static_cast<uint16_t>(LOWORD(dwSelectedQuest));
    const auto questGroup = static_cast<uint16_t>(HIWORD(dwSelectedQuest));
    SocketClient->ToGameServer()->SendQuestCancelRequest(questNumber, questGroup);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CQuestGiveUpMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

#ifdef ASG_ADD_TIME_LIMIT_QUEST

CALLBACK_RESULT SEASON3B::CQuestCountLimitMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}
#endif // ASG_ADD_TIME_LIMIT_QUEST

CALLBACK_RESULT SEASON3B::CHighValueItemCheckMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();

    int iSourceIndex = -1;

    if (pPickedItem)
    {
        if (pPickedItem->GetOwnerInventory() == g_pMyInventory->GetInventoryCtrl())
        {
            iSourceIndex = pPickedItem->GetSourceLinealPos();
        }
        else
        {
            iSourceIndex = pPickedItem->GetSourceLinealPos();
        }
    }

    if (iSourceIndex >= MAX_EQUIPMENT_INDEX && iSourceIndex < MAX_MY_INVENTORY_EX_INDEX)
    {
        SocketClient->ToGameServer()->SendSellItemToNpcRequest(iSourceIndex);
        g_pNPCShop->SetSellingItem(true);
        // Note: picked item will be cleaned up by ReceiveSell when server responds
    }
    else
    {
        // If no valid item, restore it
        g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CHighValueItemCheckMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CUseFruitMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                           const leaf::xstreambuf &xParam)
{
    SEASON3B::CreateMessageBox(
        MSGBOX_LAYOUT_CLASS(SEASON3B::CUseFruitCheckMsgBoxLayout, SessionOrigin()));

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CUseFruitMsgBoxLayout::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                               const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CUsePartChargeFruitMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    BYTE srcIndex = g_pMyInventory->GetStandbyItemIndex();
    SendRequestUse(srcIndex, 0);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CUsePartChargeFruitMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CPersonalShopItemValueCheckMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    auto *pMsgBox = dynamic_cast<CNewUI3DItemCommonMsgBox *>(pOwner);
    if (pMsgBox == nullptr)
    {
        return CALLBACK_CONTINUE;
    }

    if (g_pMyShopInventory->IsEnablePersonalShop() == true)
    {
        SocketClient->ToGameServer()->SendPlayerShopClose();
    }

    CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();

    int iSourceIndex = -1, iTargetIndex = -1;

    if (pPickedItem)
    {
        ITEM *pItemObj = pPickedItem->GetItem();
        iSourceIndex = pPickedItem->GetSourceLinealPos();
        iTargetIndex = g_pMyShopInventory->GetTargetIndex();

        if (pPickedItem->GetOwnerInventory() == g_pMyInventory->GetInventoryCtrl())
        {
            int iItemPrice = pMsgBox->GetItemValue();
            SocketClient->ToGameServer()->SendPlayerShopSetItemPrice(iSourceIndex, iItemPrice);
            SendRequestEquipmentItem(STORAGE_TYPE::INVENTORY, iSourceIndex, pItemObj,
                                     STORAGE_TYPE::MYSHOP, iTargetIndex);
        }
        else if (pPickedItem->GetOwnerInventory() == NULL)
        {
            int iItemPrice = pMsgBox->GetItemValue();
            BYTE byIndex = iSourceIndex;
            SocketClient->ToGameServer()->SendPlayerShopSetItemPrice(iSourceIndex, iItemPrice);

            SendRequestEquipmentItem(STORAGE_TYPE::INVENTORY, iSourceIndex, pItemObj,
                                     STORAGE_TYPE::MYSHOP, iTargetIndex);
        }
        else if (pPickedItem->GetOwnerInventory() == g_pMyShopInventory->GetInventoryCtrl())
        {
            int iItemPrice = pMsgBox->GetItemValue();
            BYTE byIndex = MAX_MY_INVENTORY_EX_INDEX + iSourceIndex;
            SocketClient->ToGameServer()->SendPlayerShopSetItemPrice(iSourceIndex, iItemPrice);

            SendRequestEquipmentItem(STORAGE_TYPE::MYSHOP, iSourceIndex, pItemObj,
                                     STORAGE_TYPE::MYSHOP, iTargetIndex);
        }

        AddPersonalItemPrice(iTargetIndex, pMsgBox->GetItemValue(), g_IsPurchaseShop);
    }
    else
    {
        ITEM *pItem = g_pMyShopInventory->FindItem(g_pMyShopInventory->GetSourceIndex());
        if (pItem)
        {
            iSourceIndex = g_pMyShopInventory->GetItemInventoryIndex(pItem);
            if (iSourceIndex >= 0)
            {
                int iItemPrice = pMsgBox->GetItemValue();
                SocketClient->ToGameServer()->SendPlayerShopSetItemPrice(iSourceIndex, iItemPrice);
                AddPersonalItemPrice(iSourceIndex, iItemPrice, g_IsPurchaseShop);
            }
        }
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CPersonalShopItemValueCheckMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CPersonalShopItemBuyMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    ITEM *pItem = g_pPurchaseShopInventory->FindItem(g_pPurchaseShopInventory->GetSourceIndex());
    CHARACTER *pCha = &CharactersClient[g_pPurchaseShopInventory->GetShopCharacterIndex()];

    if (pItem && pCha)
    {
        int sourceIndex = g_pPurchaseShopInventory->GetItemInventoryIndex(pItem);
        if (sourceIndex >= 0)
        {
            sessionKeeper_.InventoryStorage().purchaseSourceSlot = sourceIndex;
            SocketClient->ToGameServer()->SendPlayerShopItemBuyRequest(pCha->Key, pCha->ID,
                                                                       sourceIndex);
        }
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CPersonalShopItemBuyMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::COsbourneMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                           const leaf::xstreambuf &xParam)
{
    g_MixRecipeMgr.SetMixType(SEASON3A::MIXTYPE_OSBOURNE);
    g_pNewUISystem->Show(SEASON3B::INTERFACE_MIXINVENTORY);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGuildOutPerson::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                     const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGuildBreakMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    if (submitted_)
        return CALLBACK_BREAK;
    submitted_ = true;
    std::make_shared<CGuildBreakPasswordMsgBoxLayout>(SessionOrigin(), target_)->SetLayout();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGuildBreakMsgBoxLayout::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                 const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGuildPerson_Get_Out::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                          const leaf::xstreambuf &xParam)
{
    if (submitted_)
        return CALLBACK_BREAK;
    submitted_ = true;
    std::make_shared<CGuildBreakPasswordMsgBoxLayout>(SessionOrigin(), target_)->SetLayout();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGuildPerson_Get_Out::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);
    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGuildPerson_Cancel_Position_MsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    if (submitted_)
        return CALLBACK_BREAK;
    submitted_ = true;
    if (Hero->GuildStatus == G_MASTER)
        SocketClient->ToGameServer()->SendGuildRoleAssignRequest(G_PERSON, target_.c_str(), 0x03);

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGuildPerson_Cancel_Position_MsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CCry_Wolf_Result_Set_Temple::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                 const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CCry_Wolf_Ing_Set_Temple::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CCry_Wolf_Destroy_Set_Temple::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CCry_Wolf_Wat_Set_Temple1::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                               const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CCry_Wolf_Dont_Set_Temple1::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CCry_Wolf_Dont_Set_Temple::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                               const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CCry_Wolf_Set_Temple1::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                           const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CCry_Wolf_Set_Temple::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                          const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}
//CMaster_Level_Interface

CALLBACK_RESULT SEASON3B::CMaster_Level_Interface::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                 const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    MouseLButton = false;
    MouseLButtonPop = false;
    MouseLButtonPush = false;
    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CMaster_Level_Interface::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                             const leaf::xstreambuf &xParam)
{
    auto In_Skill = g_pMasterLevelInterface->GetCurSkillID();
    SocketClient->ToGameServer()->SendAddMasterSkillPoint(In_Skill);
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    MouseLButton = false;
    MouseLButtonPop = false;
    MouseLButtonPush = false;
    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CCry_Wolf_Get_Temple::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                              const leaf::xstreambuf &xParam)
{
    auto &crywolf = pOwner->MessageBoxMapProcessOrigin().Crywolf1st();
    crywolf.Button_Down = 1;
    SEASON3B::CreateMessageBox(
        MSGBOX_LAYOUT_CLASS(SEASON3B::CCry_Wolf_Set_Temple1, pOwner->MessageBoxSessionOrigin()));
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CCry_Wolf_Get_Temple::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                          const leaf::xstreambuf &xParam)
{
    auto &crywolf = pOwner->MessageBoxMapProcessOrigin().Crywolf1st();
    if (Hero->Helper.Type == MODEL_HORN_OF_UNIRIA || Hero->Helper.Type == MODEL_HORN_OF_DINORANT ||
        Hero->Helper.Type == MODEL_HORN_OF_FENRIR)
    {
        SEASON3B::CreateMessageBox(MSGBOX_LAYOUT_CLASS(SEASON3B::CCry_Wolf_Dont_Set_Temple,
                                                       pOwner->MessageBoxSessionOrigin()));
    }
    else
    {
        crywolf.Button_Down = 2;
        SocketClient->ToGameServer()->SendCrywolfContractRequest(crywolf.BackUp_Key);
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CUnionGuild_Break_MsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CUnionGuild_Break_MsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    if (submitted_)
        return CALLBACK_BREAK;
    submitted_ = true;
    if (Hero->GuildStatus == G_MASTER)
        SocketClient->ToGameServer()->SendRemoveAllianceGuildRequest(target_.c_str());

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CUnionGuild_Out_MsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CUseSantaInvitationMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    ITEM *pItem = g_pMyInventory->GetStandbyItem();

    if (pItem)
    {
        int iSrcIndex = g_pMyInventory->GetStandbyItemIndex();
        SendRequestUse(iSrcIndex, 0);
    }
    else
    {
        //N/A
    }

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CUseSantaInvitationMsgBoxLayout::CancelBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT CSantaTownLeaveMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                       const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendMoveToDeviasBySnowmanRequest();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT CSantaTownLeaveMsgBoxLayout::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                           const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT CSantaTownSantaMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                       const leaf::xstreambuf &xParam)
{
    SocketClient->ToGameServer()->SendSantaClausItemRequest();

    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT CSantaTownSantaMsgBoxLayout::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                           const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CUseRegistLuckyCoinMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CRegistOverLuckyCoinMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CExchangeLuckyCoinMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CExchangeLuckyCoinInvenErrMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CmuConsoleDebug &SEASON3B::CNewUI3DItemCommonMsgBox::ConsoleDebug() const noexcept
{
    return g_ConsoleDebug;
}

CALLBACK_RESULT SEASON3B::CGambleBuyMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                            const leaf::xstreambuf &xParam)
{
    GambleSystem &gambleSys = g_GambleSystem;
    if (gambleSys.IsGambleShop() && BuyCost != 0)
    {
        const auto &itemInfo = gambleSys.GetBuyItemInfoConst();
        SocketClient->ToGameServer()->SendBuyItemFromNpcRequest(itemInfo.ItemIndex);
        BuyCost = itemInfo.ItemCost;
        static_cast<CNewUI3DItemCommonMsgBox *>(pOwner)->ConsoleDebug().Write(
            MCD_SEND, L"0x32 [SendRequestBuy(%d)]", itemInfo.ItemIndex);
    }
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CGambleBuyMsgBoxLayout::CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

bool SEASON3B::CreateGambleBuyMessageBox(SessionKeeper &keeper)
{
    TMsgBoxLayoutContainer<CGambleBuyMsgBoxLayout> container(keeper);
    if (!container.Create())
    {
        return false;
    }

    return container.SetLayout();
}

CALLBACK_RESULT SEASON3B::CEmpireGuardianMsgBoxLayout::OkBtnDown(class CNewUIMessageBoxBase *pOwner,
                                                                 const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

CALLBACK_RESULT SEASON3B::CUnitedMarketPlaceMsgBoxLayout::OkBtnDown(
    class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam)
{
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, MSGBOX_EVENT_DESTROY);

    return CALLBACK_BREAK;
}

namespace HelpPanelDetail
{
constexpr int HelpPageCount = 2;
std::vector<std::wstring> HelpColumns(std::wstring_view line)
{
    auto separator = line.find(L':');
    std::size_t separatorLength = 1;
    if (separator == std::wstring_view::npos)
    {
        separator = line.find(L" - ");
        separatorLength = 3;
    }
    if (separator == std::wstring_view::npos)
        return {L"", std::wstring(line)};
    auto key = line.substr(0, separator);
    auto text = line.substr(separator + separatorLength);
    while (!key.empty() && key.back() == L' ')
        key.remove_suffix(1);
    while (!text.empty() && text.front() == L' ')
        text.remove_prefix(1);
    return {std::wstring(key), std::wstring(text)};
}
} // namespace HelpPanelDetail
CNewUIHelpWindow::CNewUIHelpWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()), panel_(keeper)
{
}
CNewUIHelpWindow::~CNewUIHelpWindow()
{
    Release();
}
bool CNewUIHelpWindow::Create(CNewUIManager *manager, int, int)
{
    if (!manager)
        return false;
    manager_ = manager;
    manager_->AddUIObj(INTERFACE_HELP, this);
    Show(false);
    return true;
}
void CNewUIHelpWindow::Release()
{
    panel_.Release();
    content_ = {};
    locale_.clear();
    visible_ = false;
    page_ = 0;
    if (!manager_)
        return;
    manager_->RemoveUIObj(this);
    manager_ = nullptr;
}

bool CNewUIHelpWindow::Update()
{
    const auto changes = panel_.TakeChanges();
    if (changes.focus && manager_)
        manager_->BringToFront(this);
    if (IsVisible())
    {
        if (changes.close)
            g_pNewUISystem->Hide(INTERFACE_HELP);
        else if (changes.tab >= 0)
            page_ = changes.tab;
        StageContent();
    }
    visible_ = IsVisible();
    return true;
}
bool CNewUIHelpWindow::UpdateMouseEvent()
{
    return !panel_.ContainsReferencePointer(MouseX, MouseY);
}
bool CNewUIHelpWindow::UpdateKeyEvent()
{
    if (!IsVisible())
        return true;
    if (IsPress(VK_F1))
    {
        AutoUpdateIndex();
        return false;
    }
    if (!IsPress(VK_ESCAPE))
        return true;
    g_pNewUISystem->Hide(INTERFACE_HELP);
    PlayBuffer(SOUND_CLICK01);
    return false;
}

bool CNewUIHelpWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessInput(event);
}
float CNewUIHelpWindow::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}
float CNewUIHelpWindow::GetKeyEventOrder()
{
    return 10.0f;
}
void CNewUIHelpWindow::OpenningProcess()
{
    page_ = 0;
    visible_ = true;
    StageContent();
}
void CNewUIHelpWindow::ClosingProcess()
{
    visible_ = false;
}
void CNewUIHelpWindow::AutoUpdateIndex()
{
    if (++page_ < HelpPanelDetail::HelpPageCount)
        return;
    g_pNewUISystem->Hide(INTERFACE_HELP);
    PlayBuffer(SOUND_CLICK01);
}

CNewUIWindowMenu::CNewUIWindowMenu(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()),
      panel_(keeper, "SystemMenu", "window_menu.rml", "window-menu")
{
}
CNewUIWindowMenu::~CNewUIWindowMenu()
{
    Release();
}
bool CNewUIWindowMenu::Create(CNewUIManager *manager, int, int)
{
    if (!manager)
        return false;
    manager_ = manager;
    manager_->AddUIObj(INTERFACE_WINDOW_MENU, this);
    content_.labels.resize(WindowMenuDetail::WindowMenuTextIds.size());
    content_.dismissOnOutsideRelease = false;
    Show(false);
    return true;
}
void CNewUIWindowMenu::Release()
{
    panel_.Release();
    content_ = {};
    if (!manager_)
        return;
    manager_->RemoveUIObj(this);
    manager_ = nullptr;
}

bool CNewUIWindowMenu::UpdateMouseEvent()
{
    return !panel_.ContainsReferencePointer(MouseX, MouseY);
}
bool CNewUIWindowMenu::UpdateKeyEvent()
{
    if (!IsVisible() || !IsPress(VK_ESCAPE))
        return true;
    g_pNewUISystem->Hide(INTERFACE_WINDOW_MENU);
    PlayBuffer(SOUND_CLICK01);
    return false;
}
void CNewUIWindowMenu::ToggleMiniMap()
{
    g_pNewUISystem->Hide(INTERFACE_WINDOW_MENU);
    if (!g_pNewUIMiniMap->m_bSuccess)
        g_pNewUISystem->Hide(INTERFACE_MINI_MAP);
    else
        g_pNewUISystem->Toggle(INTERFACE_MINI_MAP);
}
void CNewUIWindowMenu::ExecuteCommand(int command)
{
    switch (command)
    {
    case 0:
        g_pNewUISystem->Hide(INTERFACE_WINDOW_MENU);
        CreateSystemMenuMessageBox(SessionOrigin());
        break;
    case 1:
        if (g_pNewUISystem->IsVisible(INTERFACE_HELP))
            g_pHelp->AutoUpdateIndex();
        else
            g_pNewUISystem->Show(INTERFACE_HELP);
        break;
    case 2:
        g_pNewUISystem->Show(INTERFACE_GUILDINFO);
        g_pNewUISystem->Hide(INTERFACE_WINDOW_MENU);
        break;
    case 3:
        g_pNewUISystem->Toggle(INTERFACE_MOVEMAP);
        break;
    case 4:
        ToggleMiniMap();
        break;
    case 5:
        if (g_pNewUIGensRanking->SetGensInfo())
        {
            g_pNewUISystem->Show(INTERFACE_GENSRANKING);
            g_pNewUISystem->Hide(INTERFACE_WINDOW_MENU);
        }
        break;
    }
}

bool CNewUIWindowMenu::Update()
{
    const int command = panel_.TakeCommand();
    if (IsVisible() && command >= 0)
        ExecuteCommand(command);
    StageContent();
    return true;
}

bool CNewUIWindowMenu::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessInput(event);
}
float CNewUIWindowMenu::GetLayerDepth()
{
    return 10.4f;
}
float CNewUIWindowMenu::GetKeyEventOrder()
{
    return 10.0f;
}
void CNewUIWindowMenu::OpenningProcess()
{
    StageContent();
}
void CNewUIWindowMenu::ClosingProcess()
{
    content_.visible = false;
}

namespace
{

int FindListedResolution(int width, int height)
{
    for (std::size_t index = 0; index < OptionPanelDetail::Resolutions.size(); ++index)
    {
        if (OptionPanelDetail::Resolutions[index].width == width &&
            OptionPanelDetail::Resolutions[index].height == height)
        {
            return static_cast<int>(index);
        }
    }
    return -1;
}
} // namespace

SEASON3B::CNewUIOptionWindow::CNewUIOptionWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_applicationConfig(ApplicationConfigForConstruction()),
      m_applicationAudio(ApplicationAudioForConstruction()),
      m_appWindow(AppWindowForConstruction()), m_renderer(RendererForConstruction()),
      m_modernPanel(keeper)
{
    const ApplicationConfigValues &values = m_applicationConfig.Values();
    m_pNewUIMng = nullptr;
    m_Pos = {};
    m_bAutoAttack = true;
    m_bWhisperSound = false;
    m_bNameDisplay = true;
    m_bSlideHelp = true;
    m_iVolumeLevel = std::clamp(values.masterSoundVolume, 0, OptionPanelDetail::MaxVolume);
    m_iMusicLevel = std::clamp(values.masterMusicVolume, 0, OptionPanelDetail::MaxVolume);
    m_iRenderLevel = 4;
    m_bRenderAllEffects = true;
    const int resolution =
        FindListedResolution(static_cast<int>(WindowWidth), static_cast<int>(WindowHeight));
    m_iResolutionIndex = resolution >= 0 ? resolution : OptionPanelDetail::DefaultResolution;
    m_bWindowedMode = keeper.PlatformWindowMode() == TRUE;
    m_iLanguageIndex = FindCurrentLanguageIndex();
    m_iFontIndex = FindCurrentFontIndex();
}

SEASON3B::CNewUIOptionWindow::~CNewUIOptionWindow()
{
    Release();
}

bool SEASON3B::CNewUIOptionWindow::Create(CNewUIManager *manager, int x, int y)
{
    if (manager == nullptr)
    {
        return false;
    }
    m_pNewUIMng = manager;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_OPTION, this);

    UI::Modern::PC::Option::RmlOptionChoices choices;
    choices.fonts.emplace_back(L"Default");
    for (const BundledFont &font : GetBundledFonts())
        choices.fonts.emplace_back(StringUtils::NarrowToWide(font.family.c_str()));
    for (const char *locale : I18N::GetAvailableLocales())
        choices.languages.emplace_back(
            StringUtils::NarrowToWide(I18N::GetLanguageDisplayName(locale)));
    for (const OptionPanelDetail::Resolution &resolution : OptionPanelDetail::Resolutions)
        choices.resolutions.emplace_back(resolution.label);
    m_modernPanel.Configure(std::move(choices));
    SetPos(x, y);
    m_modernPanel.Show(false);
    Show(false);
    return true;
}

void SEASON3B::CNewUIOptionWindow::Release()
{
    m_modernPanel.Release();
    if (m_pNewUIMng != nullptr)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }
}

bool SEASON3B::CNewUIOptionWindow::UpdateMouseEvent()
{
    return !g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_OPTION);
}

bool SEASON3B::CNewUIOptionWindow::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_OPTION) && IsPress(VK_ESCAPE))
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_OPTION);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }
    return true;
}

void SEASON3B::CNewUIOptionWindow::ApplyPendingChanges()
{
    const UI::Modern::PC::Option::RmlOptionChanges changes = m_modernPanel.TakeChanges();
    if (changes.automaticAttack)
        m_bAutoAttack = *changes.automaticAttack;
    if (changes.whisperSound)
        m_bWhisperSound = *changes.whisperSound;
    if (changes.nameDisplay)
    {
        m_bNameDisplay = *changes.nameDisplay;
        m_renderer.SetPlayerNameDisplay(m_bNameDisplay);
    }
    if (changes.slideHelp)
        m_bSlideHelp = *changes.slideHelp;
    if (changes.effectLevel)
        m_iRenderLevel = *changes.effectLevel;
    if (changes.renderFullEffects)
        m_bRenderAllEffects = *changes.renderFullEffects;
    if (changes.soundVolume)
    {
        m_iVolumeLevel = std::clamp(*changes.soundVolume, 0, OptionPanelDetail::MaxVolume);
        OnSoundVolumeChanged();
    }
    if (changes.musicVolume)
    {
        m_iMusicLevel = std::clamp(*changes.musicVolume, 0, OptionPanelDetail::MaxVolume);
        OnMusicVolumeChanged();
    }
    if (changes.font)
    {
        m_iFontIndex = *changes.font;
        ApplyFont();
    }
    if (changes.language)
    {
        m_iLanguageIndex = *changes.language;
        ApplyLanguage();
    }
    if (changes.resolution)
    {
        m_iResolutionIndex = *changes.resolution;
        ApplyResolution();
    }
    if (changes.windowedMode)
    {
        m_bWindowedMode = *changes.windowedMode;
        ApplyWindowModeToggle();
    }
    if (changes.close)
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_OPTION);
        PlayBuffer(SOUND_CLICK01);
    }
}

void SEASON3B::CNewUIOptionWindow::ApplyLanguage()
{
    const auto languages = I18N::GetAvailableLocales();
    if (m_iLanguageIndex < 0 || m_iLanguageIndex >= static_cast<int>(languages.size()))
        return;
    const char *code = languages[m_iLanguageIndex];
    const std::wstring wide = StringUtils::NarrowToWide(code);
    if (m_applicationConfig.Values().uiLocale == wide)
        return;
    I18N::SetLocale(code);
    m_applicationConfig.SetUiLocale(wide);
    (void)m_applicationConfig.Save();
}

void SEASON3B::CNewUIOptionWindow::ApplyFont()
{
    const auto &fonts = GetBundledFonts();
    if (m_iFontIndex < 0 || m_iFontIndex > static_cast<int>(fonts.size()))
        return;
    const std::wstring selected =
        m_iFontIndex == 0 ? L"" : StringUtils::NarrowToWide(fonts[m_iFontIndex - 1].family.c_str());
    if (m_applicationConfig.Values().font == selected)
        return;
    m_applicationConfig.SetFont(selected);
    (void)m_applicationConfig.Save();
    m_appWindow.ReinitializeFonts();
}

void SEASON3B::CNewUIOptionWindow::ApplyResolution()
{
    if (m_iResolutionIndex < 0 ||
        m_iResolutionIndex >= static_cast<int>(OptionPanelDetail::Resolutions.size()))
        return;
    const OptionPanelDetail::Resolution &resolution =
        OptionPanelDetail::Resolutions[m_iResolutionIndex];
    m_appWindow.MuApplyWindowResolution(resolution.width, resolution.height,
                                        m_appWindow.UseWindowMode() != FALSE);
    SyncResolutionComboToWindow();
    m_applicationConfig.SetWindowSize(m_appWindow.Width(), m_appWindow.Height());
    (void)m_applicationConfig.Save();
    SetPos((static_cast<int>(ModernUiViewportWidth()) -
            UI::Modern::PC::Option::RmlOptionPanel::Width()) /
               2,
           std::max(0, (static_cast<int>(ModernUiViewportHeight()) -
                        UI::Modern::PC::Option::RmlOptionPanel::Height()) /
                           2));
}

void SEASON3B::CNewUIOptionWindow::ApplyWindowModeToggle()
{
    m_appWindow.UseWindowMode() = m_bWindowedMode ? TRUE : FALSE;
    m_appWindow.UseFullscreenMode() = !m_appWindow.UseWindowMode();
    m_applicationConfig.SetWindowMode(m_bWindowedMode);
    m_appWindow.MuApplyWindowResolution(m_appWindow.Width(), m_appWindow.Height(), m_bWindowedMode);
    SyncResolutionComboToWindow();
    m_applicationConfig.SetWindowSize(m_appWindow.Width(), m_appWindow.Height());
    (void)m_applicationConfig.Save();
}

bool SEASON3B::CNewUIOptionWindow::Update()
{
    ApplyPendingChanges();
    return true;
}

float SEASON3B::CNewUIOptionWindow::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Option;
}
float SEASON3B::CNewUIOptionWindow::GetKeyEventOrder()
{
    return 10.0F;
}

void SEASON3B::CNewUIOptionWindow::OpenningProcess()
{
    m_iResolutionIndex = FindCurrentResolutionIndex();
    m_iLanguageIndex = FindCurrentLanguageIndex();
    m_iFontIndex = FindCurrentFontIndex();
    m_bWindowedMode = m_appWindow.UseWindowMode() == TRUE;
    SetPos((static_cast<int>(ModernUiViewportWidth()) -
            UI::Modern::PC::Option::RmlOptionPanel::Width()) /
               2,
           std::max(0, (static_cast<int>(ModernUiViewportHeight()) -
                        UI::Modern::PC::Option::RmlOptionPanel::Height()) /
                           2));
    m_modernPanel.Show(true);
}

void SEASON3B::CNewUIOptionWindow::ClosingProcess()
{
    m_modernPanel.Show(false);
}

bool SEASON3B::CNewUIOptionWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_modernPanel.ProcessInput(event);
}

UI::Modern::PC::Option::RmlOptionContent SEASON3B::CNewUIOptionWindow::PanelContent() const
{
    return {
        I18N::Game::Option385,    I18N::Game::AutomaticAttack,  I18N::Game::BeepSoundForWhispering,
        I18N::Game::NameDisplay,  I18N::Game::SoundVolume,      I18N::Game::MusicVolume,
        I18N::Game::SlideHelp,    I18N::Game::EffectLimitation, I18N::Game::RenderFullEffects,
        I18N::Game::Font,         I18N::Game::Language,         I18N::Game::Resolution,
        I18N::Game::WindowedMode, I18N::Game::Close388};
}

UI::Modern::PC::Option::RmlOptionValues SEASON3B::CNewUIOptionWindow::PanelValues() const
{
    return {m_bAutoAttack, m_bWhisperSound,  m_bNameDisplay,     m_iVolumeLevel,
            m_iMusicLevel, m_bSlideHelp,     m_iRenderLevel,     m_bRenderAllEffects,
            m_iFontIndex,  m_iLanguageIndex, m_iResolutionIndex, m_bWindowedMode};
}

void SEASON3B::CNewUIOptionWindow::OnSoundVolumeChanged()
{
    m_SoundOnOff = m_iVolumeLevel > 0 ? 1 : 0;
    SetEffectVolumeLevel(m_iVolumeLevel);
    m_applicationConfig.SetMasterSoundVolume(m_iVolumeLevel);
    (void)m_applicationConfig.Save();
}

void SEASON3B::CNewUIOptionWindow::OnMusicVolumeChanged()
{
    m_MusicOnOff = m_iMusicLevel > 0 ? 1 : 0;
    m_applicationAudio.SetMasterMusicVolume(m_iMusicLevel);
    m_applicationConfig.SetMasterMusicVolume(m_iMusicLevel);
    (void)m_applicationConfig.Save();
}

void SEASON3B::CNewUIOptionWindow::SetAutoAttack(bool value)
{
    m_bAutoAttack = value;
}
bool SEASON3B::CNewUIOptionWindow::IsAutoAttack()
{
    return m_bAutoAttack;
}
void SEASON3B::CNewUIOptionWindow::SetWhisperSound(bool value)
{
    m_bWhisperSound = value;
}
bool SEASON3B::CNewUIOptionWindow::IsWhisperSound()
{
    return m_bWhisperSound;
}
void SEASON3B::CNewUIOptionWindow::SetSlideHelp(bool value)
{
    m_bSlideHelp = value;
}
bool SEASON3B::CNewUIOptionWindow::IsSlideHelp()
{
    return m_bSlideHelp;
}
void SEASON3B::CNewUIOptionWindow::SetVolumeLevel(int value)
{
    m_iVolumeLevel = std::clamp(value, 0, OptionPanelDetail::MaxVolume);
}
int SEASON3B::CNewUIOptionWindow::GetVolumeLevel()
{
    return m_iVolumeLevel;
}
void SEASON3B::CNewUIOptionWindow::SetRenderLevel(int value)
{
    m_iRenderLevel = std::clamp(value, 0, 4);
}
int SEASON3B::CNewUIOptionWindow::GetRenderLevel()
{
    return m_iRenderLevel;
}
void SEASON3B::CNewUIOptionWindow::SetRenderAllEffects(bool value)
{
    m_bRenderAllEffects = value;
}
bool SEASON3B::CNewUIOptionWindow::GetRenderAllEffects()
{
    return m_bRenderAllEffects;
}

int SEASON3B::CNewUIOptionWindow::FindCurrentResolutionIndex()
{
    const int index = FindListedResolution(static_cast<int>(m_appWindow.Width()),
                                           static_cast<int>(m_appWindow.Height()));
    return index >= 0 ? index : OptionPanelDetail::DefaultResolution;
}

int SEASON3B::CNewUIOptionWindow::FindCurrentLanguageIndex()
{
    const char *current = I18N::GetCurrentLocale();
    if (current == nullptr)
        return 0;
    const auto languages = I18N::GetAvailableLocales();
    for (std::size_t index = 0; index < languages.size(); ++index)
    {
        if (std::string_view(languages[index]) == current)
            return static_cast<int>(index);
    }
    return 0;
}

int SEASON3B::CNewUIOptionWindow::FindCurrentFontIndex()
{
    const std::wstring &current = m_applicationConfig.Values().font;
    if (current.empty())
        return 0;
    const auto &fonts = GetBundledFonts();
    for (std::size_t index = 0; index < fonts.size(); ++index)
    {
        if (current == StringUtils::NarrowToWide(fonts[index].family.c_str()))
            return static_cast<int>(index + 1);
    }
    return 0;
}

void SEASON3B::CNewUIOptionWindow::SyncResolutionComboToWindow()
{
    const int index = FindListedResolution(static_cast<int>(m_appWindow.Width()),
                                           static_cast<int>(m_appWindow.Height()));
    if (index >= 0)
        m_iResolutionIndex = index;
}

namespace ReconnectDetail
{

// Native message-box frame slice sizes (match CNewUICommonMessageBox).

// Layout in the 640x480 reference space the 2D render helpers use. The
// frame's top/bottom slices are fixed; the middle slice is stretched to make
// up the rest, so the panel height can be set freely.

// The fill bar is smaller than the trough; inset it so it sits centred.

// native cancel button size

// The cancel button texture stacks three 30px states in a 64x128 sheet.

// Text colour (light parchment, matching the in-game message boxes).

// Backdrop / fallback-panel opacities.
// dim over the live game / snapshot
// full cover when no frame is shown
// fallback panel border
// fallback panel fill
// fallback panel border thickness
// fallback progress trough
// fallback progress fill
// fallback cancel button

const wchar_t *StepLabel(ReconnectManager::Phase phase)
{
    using Phase = ReconnectManager::Phase;
    switch (phase)
    {
    case Phase::Probing:
        return I18N::Game::CheckingServerAvailability;
    case Phase::Retrying:
        return I18N::Game::ConnectingToTheServer;
    case Phase::Connecting:
        return I18N::Game::ConnectingToTheServer;
    case Phase::LoggingIn:
        return I18N::Game::LoggingIn;
    case Phase::SelectingChar:
        return I18N::Game::LoadingCharacterList;
    case Phase::Joining:
        return I18N::Game::EnteringTheGame;
    default:
        return L"";
    }
}

} // namespace ReconnectDetail

UI::ReconnectDialogLegacyCalls::ReconnectDialogLegacyCalls(SessionKeeper &keeper,
                                                           ReconnectDialog &owner) noexcept
    : SessionUiLegacyBindings(keeper), owner_(owner)
{
}

UI::ReconnectDialog::ReconnectDialog(SessionKeeper &keeper) noexcept
    : ReconnectDialogLegacyCalls(keeper, *this)
{
}

bool UI::ReconnectDialog::NativeSkinAvailable() const
{
    return IsValid(Bitmaps[ReconnectDetail::MsgBox::IMAGE_MSGBOX_TOP].Asset) &&
           IsValid(Bitmaps[ReconnectDetail::MsgBox::IMAGE_MSGBOX_PROGRESS_BG].Asset) &&
           IsValid(Bitmaps[ReconnectDetail::MsgBox::IMAGE_MSGBOX_BTN_CANCEL].Asset);
}

bool UI::ReconnectDialogLegacyCalls::NativeSkinAvailable()
{
    return owner_.NativeSkinAvailable();
}

float UI::ReconnectDialog::Progress(ReconnectManager &reconnect) const
{
    const int steps = ReconnectManager::GetStepCount();
    const int current = reconnect.GetStepIndex();
    if (steps <= 0 || current <= 0)
    {
        return 0.0f;
    }
    return static_cast<float>(current) / static_cast<float>(steps);
}

// ---- Native (message-box textured) rendering ----------------------------

// ---- Fallback (flat-colour) rendering, used when the skin is unloaded ----

bool UI::ReconnectDialog::CancelHovered() const
{
    return CheckMouseIn(static_cast<int>(ReconnectDetail::CANCEL_X),
                        static_cast<int>(ReconnectDetail::CANCEL_Y),
                        static_cast<int>(ReconnectDetail::CANCEL_W),
                        static_cast<int>(ReconnectDetail::CANCEL_H)) == TRUE;
}

void UI::ReconnectDialog::Update(ReconnectManager &mgr)
{
    if (mgr.IsActive() && CancelHovered() && MouseLButtonPush)
        mgr.RequestCancel();
}

void UI::ReconnectDialogLegacyCalls::FillWhite(float x, float y, float w, float h, float alpha)
{
    return owner_.FillWhite(x, y, w, h, alpha);
} // OMF-01909
void UI::ReconnectDialogLegacyCalls::FillBlack(float x, float y, float w, float h, float alpha)
{
    return owner_.FillBlack(x, y, w, h, alpha);
} // OMF-01910
// OMF-01911

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// Symmetric counterpart to GiveFocus(): drops keyboard focus from the focused
// portable text field without hiding or destroying it. GiveFocus() sets both
// s_pFocusedPortable and g_dwKeyFocusUIID, so release both here (clearing the
// key-focus id only while it still points at this field, to avoid stomping
// another widget), letting the field hand focus back to the game window while
// staying visible.
// and wherever a paragraph exceeds the box width (wrapped at the last space, or
// mid-word when a single word is too long). Each span is [start, end) in buffer
// indices; end excludes the wrapped space or newline.

CUISlideHelp::CUISlideHelp(SessionKeeper &keeper) : SessionUiLegacyBindings(keeper)
{
}
void CUISlideHelp::Init(DWORD displayMilliseconds)
{
    queue_.clear();
    text_.clear();
    displayMilliseconds_ = displayMilliseconds;
    remainingMilliseconds_ = 0;
    previousUpdate_ = startedAt_ = timeGetTime();
}
void CUISlideHelp::Update(bool paused)
{
    const DWORD now = timeGetTime();
    const DWORD elapsed = now - previousUpdate_;
    previousUpdate_ = now;
    if (paused)
        return;
    if (elapsed >= remainingMilliseconds_)
    {
        remainingMilliseconds_ = 0;
        text_.clear();
    }
    else
        remainingMilliseconds_ -= elapsed;
}
void CUISlideHelp::AddSlide(int count, int delay, const wchar_t *text, int type, float, DWORD color)
{
    constexpr int MaximumProtocolLoops = 30;
    constexpr DWORD MillisecondsPerSecond = 1000;
    if (SceneFlag != MAIN_SCENE || !text || !*text || count > MaximumProtocolLoops)
        return;
    const DWORD currentSecond = (timeGetTime() - startedAt_) / MillisecondsPerSecond;
    for (int i = 0; i < count; ++i)
        queue_.emplace(type == 1 ? 0 : currentSecond + i * delay,
                       SLIDE_QUEUE_DATA{type, text, color});
}
void CUISlideHelp::ManageSlide()
{
    if (!HaveText())
        return;
    constexpr DWORD MillisecondsPerSecond = 1000, ExpiredHelpSeconds = 60;
    const DWORD now = timeGetTime();
    const DWORD currentSecond = (now - startedAt_) / MillisecondsPerSecond;
    while (!queue_.empty() && queue_.begin()->first <= currentSecond)
    {
        auto entry = queue_.extract(queue_.begin());
        if (entry.mapped().type == -1 && entry.key() + ExpiredHelpSeconds < currentSecond)
            continue;
        text_ = std::move(entry.mapped().text);
        color_ = entry.mapped().color;
        remainingMilliseconds_ = displayMilliseconds_;
        previousUpdate_ = now;
        break;
    }
}

CSlideHelpMgr::CSlideHelpMgr(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()),
      panel_(keeper, "long_notice.rml", "Help"),
      design_(UI::Modern::ResolveUiDocument("Data/UI", UI::Modern::UiPlatform::Pc, "Help",
                                            "long_notice.rml"),
              {"Notice-HoldMilliseconds"}),
      m_HelpSlide(keeper), m_NoticeSlide(keeper)
{
    m_iCreateDelay = 10;
    m_fHelpSlideSpeed = 2.5f;
}

CSlideHelpMgr::~CSlideHelpMgr()
{
    // The slide-help timer's callback captures this; kill it so it cannot fire
    // on a destroyed instance.
    frameTimerScheduler_.Kill(SLIDEHELP_TIMER);
    ClearSlideText();
}

void CSlideHelpMgr::Init()
{
    const auto displayMilliseconds = static_cast<DWORD>(design_.Number(0));
    m_HelpSlide.Init(displayMilliseconds);
    m_NoticeSlide.Init(displayMilliseconds);

    frameTimerScheduler_.SetRepeating(SLIDEHELP_TIMER, m_iCreateDelay * 1000, [this] {
        if (g_bWndActive)
            CreateSlideText();
    });
    CreateSlideText();
}

void CSlideHelpMgr::CreateSlideText()
{
    if (SceneFlag != MAIN_SCENE)
        return;
    if (g_pOption->IsSlideHelp() == false)
    {
        return;
    }
    if (!m_HelpSlide.HaveText())
        return;

    int iLevel = CharacterMachine->Character.Level;

    const wchar_t *pszNewText = GetSlideText(iLevel);

    AddSlide(1, 0, pszNewText, 1, m_fHelpSlideSpeed);
}

void CSlideHelpMgr::OpenSlideTextFile(const wchar_t *szFileName)
{
    for (int i = 0; i < SLIDE_LEVEL_MAX; ++i)
    {
        if (!m_SlideTextList[i].empty())
        {
            ClearSlideText();
            break;
        }
    }

    FILE *fp = _wfopen(szFileName, L"rb");
    if (fp == nullptr)
    {
        wchar_t Text[256];
        mu_swprintf(Text, L"%ls - File not exist.", szFileName);
        g_ErrorReport.Write(Text);
        MessageBox(g_hWnd, Text, nullptr, MB_OK);
        SendMessage(g_hWnd, WM_DESTROY, 0, 0);
        return;
    }

    SLIDEHELP SlideHelp;
    fread(&SlideHelp, sizeof(SLIDEHELP), 1, fp);
    BuxConvert((BYTE *)&SlideHelp, sizeof(SLIDEHELP));
    fclose(fp);

    SetCreateDelay(SlideHelp.iCreateDelay);
    m_fHelpSlideSpeed = SlideHelp.fSpeed;

    for (int i = 0; i < SLIDE_LEVEL_MAX; ++i)
    {
        m_iLevelCap[i] = SlideHelp.SlideHelp[i].iLevel;
        m_iTextNumber[i] = SlideHelp.SlideHelp[i].iNumber;
        for (int j = 0; j < m_iTextNumber[i]; ++j)
        {
            auto charText = SlideHelp.SlideHelp[i].szSlideHelpText[j];
            int iLength = MultiByteToWideChar(CP_UTF8, 0, charText, -1, 0, 0);
            auto pszText = new wchar_t[iLength + 1];
            MultiByteToWideChar(CP_UTF8, 0, charText, -1, pszText, iLength);
            m_SlideTextList[i].push_back(pszText);
        }
    }
}

void CSlideHelpMgr::ClearSlideText()
{
    for (int i = 0; i < SLIDE_LEVEL_MAX; ++i)
    {
        m_iLevelCap[i] = 0;
        m_iTextNumber[i] = 0;
        for (m_SlideTextListIter = m_SlideTextList[i].begin();
             m_SlideTextListIter != m_SlideTextList[i].end(); ++m_SlideTextListIter)
        {
            if (*m_SlideTextListIter != nullptr)
            {
                delete[] *m_SlideTextListIter;
                *m_SlideTextListIter = nullptr;
            }
        }
        m_SlideTextList[i].clear();
    }
}

const wchar_t *CSlideHelpMgr::GetSlideText(int iLevel)
{
    int iHelpType = -1;
    for (int i = 0; i < SLIDE_LEVEL_MAX; ++i)
    {
        if (iLevel <= m_iLevelCap[i])
        {
            iHelpType = i;
            break;
        }
    }
    if (iHelpType == -1)
        return nullptr;
    if (m_iTextNumber[iHelpType] == 0)
        return nullptr;

    int iRandom = rand() % m_iTextNumber[iHelpType];

    if ((unsigned int)iRandom >= m_SlideTextList[iHelpType].size())
        return nullptr;

    m_SlideTextListIter = m_SlideTextList[iHelpType].begin();
    for (int i = 0; i < iRandom; ++i)
        ++m_SlideTextListIter;

    return *m_SlideTextListIter;
}

void CSlideHelpMgr::AddSlide(int iLoopCount, int iLoopDelay, const wchar_t *pszText, int iType,
                             float fSpeed, DWORD dwTextColor)
{
    if (SceneFlag != MAIN_SCENE)
        return;
    if (pszText == nullptr || pszText[0] == '\0')
        return;
    if (iLoopCount > 30)
        return;

    switch (iType)
    {
    case 2:
    case 1:
    case 0:
        m_HelpSlide.AddSlide(iLoopCount, iLoopDelay, pszText, iType - 1, fSpeed, dwTextColor);
        break;
    case 5:
    case 4:
    case 3:
        m_NoticeSlide.AddSlide(iLoopCount, iLoopDelay, pszText, iType - 4, fSpeed, dwTextColor);
        break;
    default:
        break;
    };
}

void CSlideHelpMgr::ManageSlide()
{
    const bool enabled = g_pOption->IsSlideHelp();
    m_NoticeSlide.Update(!enabled);
    m_NoticeSlide.ManageSlide();
    m_HelpSlide.Update(!enabled || !m_NoticeSlide.HaveText());
    if (m_NoticeSlide.HaveText())
        m_HelpSlide.ManageSlide();
    const auto &active = m_NoticeSlide.HaveText() ? m_HelpSlide : m_NoticeSlide;
    visible_ = enabled && !active.HaveText();
    if (!visible_)
        return;
    panel_.SetText("notice-text", active.Text());
    panel_.SetTextColor("notice-text", active.Color());
}

BOOL CSlideHelpMgr::IsIdle()
{
    return (m_NoticeSlide.HaveText() && m_HelpSlide.HaveText());
}

// Native feature window methods.
#pragma pack(push)
#pragma pack()
void CUITextInputWindow::ApplyModernUiChanges()
{
    if (m_ModernPanel.OkButton().IsClick())
    {
        m_TextInputBox.SetText(m_ModernPanel.InputValue().c_str());
        ReturnText();
    }
    if (m_ModernPanel.CancelButton().IsClick())
        g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, GetUIID(), 0);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIQuestionWindow::ApplyModernUiChanges()
{
    if (m_ModernPanel.OkButton().IsClick())
        SendUIMessageDirect(UI_MESSAGE_BTNLCLICK, 1, 0);
    if (m_iDialogType == 0 && m_ModernPanel.CancelButton().IsClick())
        SendUIMessageDirect(UI_MESSAGE_BTNLCLICK, 2, 0);
}
#pragma pack(pop)
