#include "ui/features/Shell/ShellLogic.h"
#include "I18N/All.h"
#include "app/AppWindow.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "data/GameData.h"
#include "data/Localization.h"
#include "data/ResourceData.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/ItemsSkills.h"
#include "domain/MovementAI.h"
#include "domain/WorldSimulation.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
#include "network/generated/PacketFunctions_ConnectServer.h"
#include "render/ModelResources.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "session/SessionUi.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"

//*****************************************************************************
//*****************************************************************************

//=============================================================================
// Global Variables
//=============================================================================

//=============================================================================
// Constructor / Destructor
//=============================================================================

CLoginMainWin::CLoginMainWin(SessionKeeper &keeper) : CWin(keeper), m_sceneButtons(keeper)
{
}

CLoginMainWin::~CLoginMainWin()
{
}

//=============================================================================
// Public Methods
//=============================================================================

void CLoginMainWin::Create()
{
    CWin::Create(static_cast<int>(ModernUiViewportWidth()) -
                     UI::Modern::PC::Login::RmlLoginSceneButtons::HorizontalMargin() * 2,
                 UI::Modern::PC::Login::RmlLoginSceneButtons::ButtonHeight(), -2);
    m_sceneButtons.Create();
}

void CLoginMainWin::PreRelease()
{
    m_sceneButtons.Release();
}

bool CLoginMainWin::CursorInWin(int nArea)
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

void CLoginMainWin::UpdateWhileShow(double dDeltaTick)
{
    CUIMng &rUIMng = LegacyUiManager();

    if (m_sceneButtons.MenuButton().IsClick())
    {
        rUIMng.ShowWin(rUIMng.m_SysMenuWin.get());
        rUIMng.SetSysMenuWinShow(true);
    }
    else if (m_sceneButtons.CreditButton().IsClick())
    {
        SocketClient->ToConnectServer()->SendServerListRequest();

        rUIMng.ShowWin(rUIMng.m_CreditWin.get());

        StopMp3(MUSIC_MAIN_THEME);
        PlayMp3(MUSIC_MUTHEME);
    }
}

bool CLoginMainWin::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsShow() && m_sceneButtons.ProcessInput(event);
}

//*****************************************************************************
// Desc: implementation of the CServerSelWin class.
//*****************************************************************************

#define SSW_GAP_WIDTH 28
#define SSW_GAP_HEIGHT 5
#define SSW_GB_POS_X 16
#define SSW_GB_POS_Y 19

using namespace SEASON3A;

CServerSelWin::CServerSelWin(SessionKeeper &keeper)
    : CWin(keeper), m_aServerGroupBtn(keeper), m_aServerBtn(keeper), m_aServerGauge(keeper),
      m_aBtnDeco(keeper), m_aArrowDeco(keeper), m_winDescription(keeper)
{
}

CServerSelWin::~CServerSelWin()
{
}

void CServerSelWin::Create()
{
    CWin::Create(0, 0, -2);

    m_iSelectServerBtnIndex = -1;

    int i;

    for (i = 0; i < SSW_SERVER_G_MAX; ++i)
    {
        m_aServerGroupBtn[i].Create(SERVER_GROUP_BTN_WIDTH, SERVER_GROUP_BTN_HEIGHT, BITMAP_LOG_IN,
                                    4, 2, 1, -1, 3);
        CWin::RegisterButton(&m_aServerGroupBtn[i]);
    }

    for (i = 0; i < SSW_SERVER_MAX; ++i)
    {
        m_aServerBtn[i].Create(SERVER_BTN_WIDTH, SERVER_BTN_HEIGHT, BITMAP_LOG_IN + 1, 3, 2, 1);
        CWin::RegisterButton(&m_aServerBtn[i]);
        m_aServerGauge[i].Create(160, 4, BITMAP_LOG_IN + 2);
    }

    SImgInfo aiiDeco[2] = {{BITMAP_LOG_IN + 3, 0, 0, 68, 95}, {BITMAP_LOG_IN + 3, 68, 0, 68, 95}};
    m_aBtnDeco[0].Create(&aiiDeco[0], 8, 19);
    m_aBtnDeco[1].Create(&aiiDeco[1], 60, 19);

    SImgInfo aiiArrow[2] = {{BITMAP_LOG_IN + 3, 136, 0, 23, 29},
                            {BITMAP_LOG_IN + 3, 136, 30, 23, 29}};
    m_aArrowDeco[0].Create(&aiiArrow[0], 1, 2);
    m_aArrowDeco[1].Create(&aiiArrow[1], 23, 2);

    SImgInfo aiiDescBg[WE_BG_MAX] = {{BITMAP_LOG_IN + 11, 0, 0, 4, 4},
                                     {BITMAP_LOG_IN + 12, 0, 0, 512, 6},
                                     {BITMAP_LOG_IN + 12, 0, 6, 512, 6},
                                     {BITMAP_LOG_IN + 13, 0, 0, 3, 4},
                                     {BITMAP_LOG_IN + 13, 3, 0, 3, 4}};
    m_winDescription.Create(aiiDescBg, 1, 10);
    m_winDescription.SetLine(10);

    CWin::SetSize((SERVER_GROUP_BTN_WIDTH + SSW_GAP_WIDTH) * 2 + SERVER_BTN_WIDTH,
                  SERVER_BTN_HEIGHT * SSW_SERVER_MAX + SSW_GAP_HEIGHT * 2 +
                      SERVER_GROUP_BTN_HEIGHT + m_winDescription.GetHeight());
}

void CServerSelWin::PreRelease()
{
    int i;

    for (i = 0; i < SSW_SERVER_MAX; ++i)
    {
        m_aServerGauge[i].Release();
    }

    for (i = 0; i < 2; ++i)
    {
        m_aBtnDeco[i].Release();
        m_aArrowDeco[i].Release();
    }

    m_winDescription.Release();
}

void CServerSelWin::SetServerBtnPosition()
{
    if (m_iSelectServerBtnIndex == -1)
        return;

    int nServerBtnPosX =
        m_aServerGroupBtn[1].GetXPos() + m_aServerGroupBtn[0].GetWidth() + SSW_GAP_WIDTH;

    int nServerBtnHeight = m_aServerBtn[0].GetHeight();

    int nLServerGBtnHeightSum = m_aServerGroupBtn[1].GetHeight() * 10;

    int nServerBtnHeightSum = nServerBtnHeight * m_icntServer;

    int nLServerGBtnTop = m_aServerGroupBtn[1].GetYPos();

    int nServerBtnBasePosY = nLServerGBtnHeightSum > nServerBtnHeightSum
                                 ? nLServerGBtnTop
                                 : nLServerGBtnTop - (nServerBtnHeightSum - nLServerGBtnHeightSum);

    for (int i = 0; i < m_pSelectServerGroup->GetServerSize(); i++)
    {
        m_aServerBtn[i].SetPosition(nServerBtnPosX, nServerBtnBasePosY + nServerBtnHeight * i);
        m_aServerGauge[i].SetPosition(m_aServerBtn[i].GetXPos() + SSW_GB_POS_X,
                                      m_aServerBtn[i].GetYPos() + SSW_GB_POS_Y);
    }
}

void CServerSelWin::SetArrowSpritePosition()
{
    if (m_iSelectServerBtnIndex == -1)
        return;

    if ((m_iSelectServerBtnIndex >= 0) && (m_iSelectServerBtnIndex <= SSW_LEFT_SERVER_G_MAX))
    {
        m_aArrowDeco[0].SetPosition(m_aServerGroupBtn[m_iSelectServerBtnIndex].GetXPos() +
                                        SERVER_GROUP_BTN_WIDTH,
                                    m_aServerGroupBtn[m_iSelectServerBtnIndex].GetYPos());
    }
    else if ((m_iSelectServerBtnIndex > SSW_LEFT_SERVER_G_MAX) &&
             (m_iSelectServerBtnIndex < SSW_SERVER_G_MAX))
    {
        m_aArrowDeco[1].SetPosition(m_aServerGroupBtn[m_iSelectServerBtnIndex].GetXPos(),
                                    m_aServerGroupBtn[m_iSelectServerBtnIndex].GetYPos());
    }
}

void CServerSelWin::UpdateDisplay()
{
    m_pSelectServerGroup = NULL;
    m_icntServerGroup = 0;
    m_icntServer = 0;
    m_icntLeftServerGroup = 0;
    m_icntRightServerGroup = 0;
    m_bTestServerBtn = false;

    DWORD adwServerGBtnClr[BTN_IMG_MAX] = {CLRDW_BR_GRAY, CLRDW_BR_GRAY, CLRDW_WHITE, 0,
                                           CLRDW_BR_GRAY, CLRDW_BR_GRAY, CLRDW_WHITE, 0};

    DWORD adwServerBtnClr[4][4] = {
        {CLRDW_BR_GRAY, CLRDW_BR_GRAY, CLRDW_WHITE, 0},
        {CLRDW_YELLOW, CLRDW_YELLOW, CLRDW_BR_YELLOW, 0},
        {CLRDW_ORANGE, CLRDW_ORANGE, CLRDW_BR_ORANGE, 0},
        {CLRDW_ORANGE, CLRDW_ORANGE, CLRDW_BR_ORANGE, 0},
    };

    m_icntServerGroup = g_ServerListManager.GetServerGroupSize();

    if (m_icntServerGroup < 1)
        return;

    CServerGroup *pServerGroup = NULL;

    g_ServerListManager.SetFirst();

    while (g_ServerListManager.GetNext(pServerGroup))
    {
        if (pServerGroup->m_iWidthPos == CServerGroup::SBP_CENTER)
        {
            if (m_bTestServerBtn == true)
                continue;

            m_aServerGroupBtn[0].SetText(pServerGroup->m_szName, adwServerGBtnClr);
            pServerGroup->m_iBtnPos = 0;
            m_bTestServerBtn = true;
        }
        else if (pServerGroup->m_iWidthPos == CServerGroup::SBP_LEFT)
        {
            if (m_icntLeftServerGroup >= SSW_LEFT_SERVER_G_MAX)
                continue;

            m_aServerGroupBtn[m_icntLeftServerGroup + 1].SetText(pServerGroup->m_szName,
                                                                 adwServerGBtnClr);
            pServerGroup->m_iBtnPos = m_icntLeftServerGroup + 1;

            m_icntLeftServerGroup++;
        }
        else if (pServerGroup->m_iWidthPos == CServerGroup::SBP_RIGHT)
        {
            if (m_icntRightServerGroup >= SSW_RIGHT_SERVER_G_MAX)
                continue;

            m_aServerGroupBtn[SSW_LEFT_SERVER_G_MAX + m_icntRightServerGroup + 1].SetText(
                pServerGroup->m_szName, adwServerGBtnClr);
            pServerGroup->m_iBtnPos = SSW_LEFT_SERVER_G_MAX + m_icntRightServerGroup + 1;

            m_icntRightServerGroup++;
        }
    }

    ShowServerGBtns();
    ShowDecoSprite();

    memset(m_szDescription, 0, sizeof(char) * SSW_DESC_LINE_MAX * SSW_DESC_ROW_MAX);

    if (m_iSelectServerBtnIndex != -1)
    {
        m_pSelectServerGroup = g_ServerListManager.GetServerGroupByBtnPos(m_iSelectServerBtnIndex);
    }

    if (m_pSelectServerGroup == NULL)
        return;

    m_icntServer = m_pSelectServerGroup->GetServerSize();

    if (m_icntServer < 1)
        return;

    CServerInfo *pServerInfo = NULL;

    m_pSelectServerGroup->SetFirst();

    int icntServer = 0;
    while (m_pSelectServerGroup->GetNext(pServerInfo))
    {
        m_aServerBtn[icntServer].SetText(pServerInfo->m_bName,
                                         adwServerBtnClr[pServerInfo->m_byNonPvP]);
        m_aServerGauge[icntServer].SetValue(pServerInfo->m_iPercent, 100);
        icntServer++;
    }

    ::SeparateTextIntoLines(m_pSelectServerGroup->m_szDescription, m_szDescription[0],
                            SSW_DESC_LINE_MAX, SSW_DESC_ROW_MAX);

    SetArrowSpritePosition();
    SetServerBtnPosition();
    ShowArrowSprite();
    ShowServerBtns();
}

void CServerSelWin::ShowServerGBtns()
{
    int i;

    if (m_bTestServerBtn == true)
    {
        m_aServerGroupBtn[0].Show(CWin::m_bShow);
    }
    else
    {
        m_aServerGroupBtn[0].Show(false);
    }

    for (i = 1; i < m_icntLeftServerGroup + 1; i++)
    {
        m_aServerGroupBtn[i].Show(CWin::m_bShow);
    }
    for (; i < SSW_LEFT_SERVER_G_MAX; ++i)
    {
        m_aServerGroupBtn[i].Show(false);
    }

    for (i = SSW_LEFT_SERVER_G_MAX + 1; i < SSW_RIGHT_SERVER_G_MAX + 1 + m_icntRightServerGroup;
         i++)
    {
        m_aServerGroupBtn[i].Show(CWin::m_bShow);
    }
    for (; i < SSW_SERVER_G_MAX; i++)
    {
        m_aServerGroupBtn[i].Show(false);
    }
}

void CServerSelWin::ShowDecoSprite()
{
    if (m_icntLeftServerGroup > 0)
    {
        m_aBtnDeco[0].Show(CWin::m_bShow);
    }
    else
    {
        m_aBtnDeco[0].Show(false);
    }

    if (m_icntRightServerGroup > 0)
    {
        m_aBtnDeco[1].Show(CWin::m_bShow);
    }
    else
    {
        m_aBtnDeco[1].Show(false);
    }
}

void CServerSelWin::ShowArrowSprite()
{
    if ((m_iSelectServerBtnIndex >= 0) && (m_iSelectServerBtnIndex <= SSW_LEFT_SERVER_G_MAX))
    {
        m_aArrowDeco[0].Show(CWin::m_bShow);
        m_aArrowDeco[1].Show(false);
    }
    else if ((m_iSelectServerBtnIndex > SSW_LEFT_SERVER_G_MAX) &&
             (m_iSelectServerBtnIndex < SSW_SERVER_G_MAX))
    {
        m_aArrowDeco[0].Show(false);
        m_aArrowDeco[1].Show(CWin::m_bShow);
    }
    else
    {
        m_aArrowDeco[0].Show(false);
        m_aArrowDeco[1].Show(false);
    }
}

void CServerSelWin::ShowServerBtns()
{
    if (m_iSelectServerBtnIndex == -1)
    {
        m_winDescription.Show(false);
        return;
    }

    int i;
    for (i = 0; i < m_icntServer; i++)
    {
        m_aServerBtn[i].Show(CWin::m_bShow);
        m_aServerGauge[i].Show(CWin::m_bShow);
    }
    for (; i < SSW_SERVER_MAX; i++)
    {
        m_aServerBtn[i].Show(false);
        m_aServerGauge[i].Show(false);
    }

    m_winDescription.Show(CWin::m_bShow);
}

bool CServerSelWin::CursorInWin(int nArea)
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

bool CServerSelWin::RequestConnection(wchar_t *groupName, CServerInfo &server)
{
    if (server.m_iPercent >= 100)
    {
        return false;
    }
    LegacyUiManager().HideWin(this);
    SocketClient->ToConnectServer()->SendConnectionInfoRequest(
        static_cast<uint16_t>(server.m_iConnectIndex));
    g_pSystemLogBox->AddText(I18N::Game::ConnectingToTheServer, SEASON3B::TYPE_SYSTEM_MESSAGE);
    g_pSystemLogBox->AddText(I18N::Game::PleaseWait, SEASON3B::TYPE_SYSTEM_MESSAGE);
    g_ServerListManager.SetSelectServerInfo(groupName, server.m_iIndex, server.m_byNonPvP);
    return true;
}

bool CServerSelWin::TryAutoConnect()
{
    autoConnectTargets_.clear();
    autoConnectPending_ = false;
    SessionConfigValues &config = sessionKeeper_.Config();
    if (autoConnectAttempted_)
    {
        // A refreshed list (including a busy response) returns to manual selection.
        config.autoLoginPort = 0;
        return false;
    }
    if (!config.autoLoginPort || !config.rememberMe || !config.savePassword ||
        config.username.empty() || config.encryptedPassword.empty())
    {
        return false;
    }

    autoConnectAttempted_ = true;
    CServerGroup *group = nullptr;
    g_ServerListManager.SetFirst();
    while (g_ServerListManager.GetNext(group))
    {
        CServerInfo *server = nullptr;
        group->SetFirst();
        while (group->GetNext(server))
        {
            if (server->m_iPercent < 100)
            {
                autoConnectTargets_.emplace_back(group->m_szName, *server);
            }
        }
    }
    return RequestNextAutoConnect();
}

bool CServerSelWin::RequestNextAutoConnect()
{
    if (autoConnectTargets_.empty())
    {
        autoConnectPending_ = false;
        sessionKeeper_.Config().autoLoginPort = 0;
        LegacyUiManager().ShowWin(this);
        return false;
    }
    auto target = std::move(autoConnectTargets_.front());
    autoConnectTargets_.pop_front();
    autoConnectPending_ = true;
    return RequestConnection(target.first.data(), target.second);
}

bool CServerSelWin::AcceptConnectionPort(unsigned short port)
{
    if (!autoConnectPending_)
        return true;
    if (sessionKeeper_.Config().autoLoginPort != port)
    {
        (void)RequestNextAutoConnect();
        return false;
    }
    autoConnectTargets_.clear();
    autoConnectPending_ = false;
    return true;
}

void CServerSelWin::UpdateWhileActive(double dDeltaTick)
{
    int i;

    for (i = 0; i < SSW_SERVER_G_MAX; i++)
    {
        if (m_aServerGroupBtn[i].IsClick())
        {
            if (m_iSelectServerBtnIndex != -1)
            {
                m_aServerGroupBtn[m_iSelectServerBtnIndex].SetCheck(false);
            }

            m_aServerGroupBtn[i].SetCheck(true);
            m_iSelectServerBtnIndex = i;

            SocketClient->ToConnectServer()->SendServerListRequest();
        }
    }

    if (m_pSelectServerGroup == NULL)
        return;

    CServerInfo *pServerInfo = NULL;
    for (i = 0; i < m_icntServer; i++)
    {
        if (m_aServerBtn[i].IsClick())
        {
            pServerInfo = m_pSelectServerGroup->GetServerInfo(i);

            if (pServerInfo == NULL)
                return;

            if (pServerInfo->m_iPercent < 100)
            {
                (void)RequestConnection(m_pSelectServerGroup->m_szName, *pServerInfo);
                break;
            }
            else if (pServerInfo->m_iPercent < 128)
            {
                LegacyUiManager().PopUpMsgWin(MESSAGE_SERVER_BUSY);
            }
        }
    }
}

CLoginWin::CLoginWin(SessionKeeper &keeper)
    : CWin(keeper), m_sessionNetwork(NetworkForConstruction()), m_panel(keeper)
{
}

CLoginWin::~CLoginWin() = default;

void CLoginWin::UnbindConfiguration() noexcept
{
    m_sessionConfig = nullptr;
    m_sessionConfigStore = nullptr;
}

void CLoginWin::Create()
{
    m_RememberMe = m_sessionConfig != nullptr && m_sessionConfig->rememberMe;
    if (m_RememberMe)
    {
        m_sessionConfigStore->DecryptCredentials(*m_sessionConfig, m_Username, m_Password,
                                                 _countof(m_Username), _countof(m_Password));
    }
    else
    {
        // Ensure they are empty if RememberMe is off
        m_Username[0] = L'\0';
        m_Password[0] = L'\0';
    }

    CWin::Create(UI::Modern::PC::Login::RmlLoginPanel::Width(),
                 UI::Modern::PC::Login::RmlLoginPanel::Height(), -2);
    m_panel.Create();
    m_panel.SetCredentials(m_Username, m_Password);
    m_panel.FocusInitialInput();
    enterKeyHeld_ = IsPress(VK_RETURN) || IsRepeat(VK_RETURN);
    escapeKeyHeld_ = IsPress(VK_ESCAPE) || IsRepeat(VK_ESCAPE);

    // Seed the edit-detection snapshot with what we just loaded so filling the
    // boxes here is not mistaken for the player editing them.
    wcscpy_s(m_prevUsername, _countof(m_prevUsername), m_Username);
    wcscpy_s(m_prevPassword, _countof(m_prevPassword), m_Password);
}

void CLoginWin::PreRelease()
{
    m_panel.Release();
}

bool CLoginWin::CursorInWin(int nArea)
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

void CLoginWin::UpdateWhileActive(double)
{
    const bool enterDown = IsPress(VK_RETURN) || IsRepeat(VK_RETURN);
    const bool escapeDown = IsPress(VK_ESCAPE) || IsRepeat(VK_ESCAPE);
    const bool enterPressed = enterDown && !enterKeyHeld_;
    const bool escapePressed = escapeDown && !escapeKeyHeld_;
    enterKeyHeld_ = enterDown;
    escapeKeyHeld_ = escapeDown;

    if (m_panel.OkButton().IsClick() || enterPressed)
    {
        PlayBuffer(SOUND_CLICK01);
        RequestLogin();
        return;
    }

    if (m_panel.CancelButton().IsClick() || escapePressed)
    {
        PlayBuffer(SOUND_CLICK01);
        CancelLogin();
        LegacyUiManager().SetSysMenuWinShow(false);
        return;
    }
}

void CLoginWin::UpdateWhileShow(double dDeltaTick)
{
    (void)dDeltaTick;
    RevokeSavedCredentialsIfEdited();
}

void CLoginWin::FocusAccountInput()
{
    m_panel.FocusAccountInput();
}

void CLoginWin::FocusPasswordInput()
{
    m_panel.FocusPasswordInput();
}

bool CLoginWin::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsShow() && m_panel.ProcessInput(event);
}

std::optional<UI::Modern::RmlTextInputArea> CLoginWin::ModernTextInputArea() const
{
    return CWin::m_bShow ? m_panel.TextInputArea() : std::nullopt;
}

void CLoginWin::RevokeSavedCredentialsIfEdited()
{
    // Editing credentials clears the live copy only. Startup profiles stay unchanged.
    const std::wstring &curUser = m_panel.Account();
    const std::wstring &curPass = m_panel.Password();

    if (curUser == m_prevUsername && curPass == m_prevPassword)
        return;

    if (m_sessionConfig == nullptr)
        return;
    SessionConfigValues &config = *m_sessionConfig;
    const bool bHadStored = !config.username.empty() || !config.encryptedPassword.empty();
    if (bHadStored)
    {
        config.username.clear();
        config.encryptedPassword.clear();
        config.savePassword = false;
        config.autoLoginPort = 0;
        (void)UpdateConfiguration();
    }

    wcsncpy_s(m_prevUsername, _countof(m_prevUsername), curUser.c_str(), _TRUNCATE);
    wcsncpy_s(m_prevPassword, _countof(m_prevPassword), curPass.c_str(), _TRUNCATE);
}

void CLoginWin::RequestLogin()
{
    if (CurrentProtocolState == REQUEST_JOIN_SERVER)
        return;

    LegacyUiManager().HideWin(this);

    wcsncpy_s(m_Username, _countof(m_Username), m_panel.Account().c_str(), _TRUNCATE);
    wcsncpy_s(m_Password, _countof(m_Password), m_panel.Password().c_str(), _TRUNCATE);

    if (wcslen(m_Username) <= 0)
        LegacyUiManager().PopUpMsgWin(MESSAGE_INPUT_ID);
    else if (wcslen(m_Password) <= 0)
        LegacyUiManager().PopUpMsgWin(MESSAGE_INPUT_PASSWORD);
    else
    {
        if (CurrentProtocolState == RECEIVE_JOIN_SERVER_SUCCESS)
        {
            g_ConsoleDebug.Write(MCD_NORMAL, L"Login with the following account: %ls", m_Username);

            g_ErrorReport.Write(L"> Login Request.\r\n");
            g_ErrorReport.Write(L"> Try to Login \"%ls\"\r\n", m_Username);

            LogIn = 1;
            wcscpy(LogInID, (m_Username));
            CurrentProtocolState = REQUEST_LOG_IN;

            SocketClient->ToGameServer()->SendLogin(m_Username, m_Password, Version, Serial);

            // Keep the credentials in memory so auto-reconnect can re-login
            // without prompting after an in-game disconnect.
            m_sessionNetwork.Reconnect().CacheCredentials(m_Username, m_Password);

            g_pSystemLogBox->AddText(I18N::Game::VerifyingYourAccount,
                                     SEASON3B::TYPE_SYSTEM_MESSAGE);
            g_pSystemLogBox->AddText(I18N::Game::PleaseWait, SEASON3B::TYPE_SYSTEM_MESSAGE);
        }
    }
}

bool CLoginWin::TryAutoLogin()
{
    if (m_sessionConfig == nullptr || !m_sessionConfig->autoLoginPort ||
        !m_sessionConfig->rememberMe || !m_sessionConfig->savePassword ||
        m_sessionConfig->username.empty() || m_sessionConfig->encryptedPassword.empty() ||
        CurrentProtocolState != RECEIVE_JOIN_SERVER_SUCCESS)
    {
        return false;
    }
    m_sessionConfig->autoLoginPort = 0;
    RequestLogin();
    return CurrentProtocolState == REQUEST_LOG_IN;
}

bool CLoginWin::UpdateConfiguration()
{
    return m_sessionConfig != nullptr && m_sessionConfigStore != nullptr &&
           m_sessionConfigStore->UpdateCredentials([this] {
               return SessionConfigStore::CredentialSnapshot{sessionKeeper_.SlotId(),
                                                             *m_sessionConfig};
           });
}

void CLoginWin::CancelLogin()
{
    if (m_sessionConfig != nullptr)
    {
        m_sessionConfig->autoLoginPort = 0;
    }
    ConnectConnectionServer();
    LegacyUiManager().HideWin(this);
}

void CLoginWin::ConnectConnectionServer()
{
    LogIn = 0;
    CurrentProtocolState = REQUEST_JOIN_SERVER;
    CreateSocket(szServerIpAddress, g_ServerPort);
}

//*****************************************************************************
//*****************************************************************************

CCharMakeWin::CCharMakeWin(SessionKeeper &keeper)
    : CWin(keeper), sessionUi(UiForConstruction()), panel_(keeper)
{
}

CCharMakeWin::~CCharMakeWin()
{
    ReleaseCreateCharacterVisual();
}

void CCharMakeWin::Create()
{
    CWin::Create(WindowWidth, WindowHeight, -2);
    m_nSelJob = CLASS_KNIGHT;
    UpdateDisplay();
}
void CCharMakeWin::PreRelease()
{
    panel_.Release();
    ReleaseCreateCharacterVisual();
}
void CCharMakeWin::SetPosition(int, int)
{
}
void CCharMakeWin::Show(bool shown)
{
    CWin::Show(shown);
    if (shown)
    {
        ClearInput();
        InputEnable = false;
        panel_.ClearName();
        UpdateDisplay();
    }
}
bool CCharMakeWin::CursorInWin(int area)
{
    return m_bShow && area != WA_MOVE;
}
void CCharMakeWin::UpdateDisplay()
{
    content_.enabled.fill(true);
#ifdef PBG_ADD_CHARACTERCARD
    for (int i = 0; i < CLASS_CHARACTERCARD_TOTALCNT; ++i)
        content_.enabled[i + CLASS_DARK] = g_CharCardEnable.bCharacterEnable[i];
#endif
    for (int i = 0; i < MAX_CLASS; ++i)
        content_.classes[i] = I18N::Game::Lookup(CharacterCreationDetail::kClassButtonTextIds[i]);
    const auto &stats =
        CharacterCreationDetail::kClassStatTable[static_cast<std::size_t>(m_nSelJob)];
    for (std::size_t i = 0; i < stats.values.size(); ++i)
    {
        content_.stats[i] = stats.values[i];
        content_.statTitles[i] =
            I18N::Game::Lookup(CharacterCreationDetail::kStatLabelBaseId + static_cast<int>(i));
    }
    content_.stats.back() = CharacterCreationDetail::kDarkLordLeadershipStatValue;
    content_.statTitles.back() =
        I18N::Game::Lookup(CharacterCreationDetail::kDarkLordLeadershipTextId);
    content_.selected = m_nSelJob;
    content_.showCharisma = m_nSelJob == CLASS_DARK_LORD;
    content_.classTitle = content_.classes[m_nSelJob];
    content_.nameTitle = I18N::Game::Name;
    content_.description =
        I18N::Game::Lookup(CharacterCreationDetail::ResolveDescriptionTextId(m_nSelJob));
    std::replace(content_.description.begin(), content_.description.end(), L'#', L'\n');
    content_.ok = I18N::Game::OK;
    content_.cancel = I18N::Game::Cancel;
    locale_ = I18N::GetCurrentLocale();
    ++content_.revision;
    effectElapsedMilliseconds_ = 0;
    SelectCreateCharacter();
}
void CCharMakeWin::UpdateWhileActive(double elapsedMilliseconds)
{
    const auto changes = panel_.TakeChanges();
    if (changes.selected && content_.enabled[*changes.selected] && m_nSelJob != *changes.selected)
    {
        m_nSelJob = static_cast<CLASS_TYPE>(*changes.selected);
        UpdateDisplay();
    }
    else if (locale_ != I18N::GetCurrentLocale())
        UpdateDisplay();
    if (changes.ok || IsPress(VK_RETURN))
    {
        if (content_.enabled[m_nSelJob])
        {
            PlayBuffer(SOUND_CLICK01);
            RequestCreateCharacter();
        }
    }
    else if (changes.cancel || IsPress(VK_ESCAPE))
    {
        PlayBuffer(SOUND_CLICK01);
        LegacyUiManager().HideWin(this);
        LegacyUiManager().SetSysMenuWinShow(false);
    }
    effectElapsedMilliseconds_ += elapsedMilliseconds;
    UpdateCreateCharacter(elapsedMilliseconds);
}
bool CCharMakeWin::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, m_bShow, effectElapsedMilliseconds_, content_);
}
bool CCharMakeWin::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_bShow && panel_.ProcessInput(event);
}
std::optional<UI::Modern::RmlTextInputArea> CCharMakeWin::ModernTextInputArea() const
{
    return m_bShow ? panel_.TextInputArea() : std::nullopt;
}

void CCharMakeWin::RequestCreateCharacter()
{
    sessionUi.RequestCreateCharacter(*this);
}

void CCharMakeWin::SelectCreateCharacter()
{
    ReleaseCreateCharacterVisual();
    CharacterView.Class = m_nSelJob;
    CreateCharacterPointer(&CharacterView, static_cast<int>(MODEL_FACE) + CharacterView.Class, 0,
                           0);
    CharacterView.Object.Kind = 0;
    SetAction(&CharacterView.Object, 1);
    VectorCopy(CharacterView.Object.Position, m_createBasePosition);
    PrepareCreateCharacterAppearance();
}

void CCharMakeWin::UpdateCreateCharacter(double elapsedMilliseconds)
{
    PrepareCreateCharacterAppearance();
    const bool advance = elapsedMilliseconds > 0.0;
    struct FactorScope final
    {
        float &factor;
        float previous;
        ~FactorScope()
        {
            factor = previous;
        }
    } scope{FPS_ANIMATION_FACTOR, FPS_ANIMATION_FACTOR};
    FPS_ANIMATION_FACTOR = ApplicationFrameUnit::CalculateAnimationFactor(
        elapsedMilliseconds, sessionKeeper_.ApplicationConfig().legacyReferenceFps);
    if (advance)
    {
        if (!CharacterAnimation(&CharacterView, &CharacterView.Object))
            SetAction(&CharacterView.Object, 0);
        sessionKeeper_.Gameplay()->PrepareLocalCharacterPose(CharacterView);
        sessionKeeper_.Visual()->AdvanceLocalCharacterVisual(CharacterView, m_createVisual);
    }
    if (CharacterView.WorldVisualPoseRevision == 0)
        sessionKeeper_.Gameplay()->RefreshLocalCharacterPose(CharacterView);
    if (!advance)
        sessionKeeper_.Visual()->AdvanceLocalCharacterVisual(CharacterView, m_createVisual);
}

void CCharMakeWin::ReleaseCreateCharacterVisual()
{
    if (m_createVisual.generation == 0)
        return;
    sessionKeeper_.Visual()->RetireCharacterVisualLifetime(CharacterView, m_createVisual);
    m_createVisual.Reset();
}

//*****************************************************************************
//*****************************************************************************

bool SessionUiUnit::HasAccountBlockedCharacter()
{
    return CharacterSelectionDetail::AnyCharacter(CharactersClient, [](const CHARACTER &character) {
        return character.Object.Live != 0 && (character.CtlCode & CTLCODE_10ACCOUNT_BLOCKITEM);
    });
}

bool SessionUiUnit::HasEmptyCharacterSlot()
{
    return CharacterSelectionDetail::AnyCharacter(
        CharactersClient, [](const CHARACTER &character) { return character.Object.Live == 0; });
}

bool SessionUiUnit::HasLiveCharacter()
{
    return CharacterSelectionDetail::AnyCharacter(
        CharactersClient, [](const CHARACTER &character) { return character.Object.Live != 0; });
}

CHARACTER *SessionUiUnit::GetSelectedCharacter()
{
    if (SelectedHero < 0 || SelectedHero >= CharacterSelectionDetail::kCharacterSlotCount)
        return nullptr;
    return &CharactersClient[SelectedHero];
}

CCharSelMainWin::CCharSelMainWin(SessionKeeper &keeper)
    : CWin(keeper), m_asprBack(keeper), m_aBtn(keeper)
{
}

CCharSelMainWin::~CCharSelMainWin()
{
}

void CCharSelMainWin::Create()
{
    m_asprBack[CSMW_SPR_DECO].Create(189, 103, BITMAP_LOG_IN + 2);
    m_asprBack[CSMW_SPR_INFO].Create(static_cast<int>(WindowWidth) - 266,
                                     CharacterSelectionDetail::kInfoSpriteHeight);
    m_asprBack[CSMW_SPR_INFO].SetColor(0, 0, 0);
    m_asprBack[CSMW_SPR_INFO].SetAlpha(CharacterSelectionDetail::kWindowAlpha);

    m_aBtn[CSMW_BTN_CREATE].Create(54, 30, BITMAP_LOG_IN + 3, 4, 2, 1, 3);
    m_aBtn[CSMW_BTN_MENU].Create(54, 30, BITMAP_LOG_IN + 4, 3, 2, 1);
    m_aBtn[CSMW_BTN_CONNECT].Create(54, 30, BITMAP_LOG_IN + 5, 4, 2, 1, 3);
    m_aBtn[CSMW_BTN_DELETE].Create(54, 30, BITMAP_LOG_IN + 6, 4, 2, 1, 3);

    CWin::Create(m_aBtn[0].GetWidth() * CSMW_BTN_MAX + m_asprBack[CSMW_SPR_INFO].GetWidth() + 6,
                 m_aBtn[0].GetHeight(), -2);

    for (int i = 0; i < CSMW_BTN_MAX; ++i)
        CWin::RegisterButton(&m_aBtn[i]);

    m_bAccountBlockItem = HasAccountBlockedCharacter();
}

void CCharSelMainWin::PreRelease()
{
    for (int i = 0; i < CSMW_SPR_MAX; ++i)
        m_asprBack[i].Release();
}

void CCharSelMainWin::SetPosition(int nXCoord, int nYCoord)
{
    CWin::SetPosition(nXCoord, nYCoord);

    const int buttonWidth = m_aBtn[0].GetWidth();

    m_aBtn[CSMW_BTN_CREATE].SetPosition(nXCoord, nYCoord);
    m_aBtn[CSMW_BTN_MENU].SetPosition(
        nXCoord + buttonWidth + CharacterSelectionDetail::kButtonSpacing, nYCoord);

    const int infoX =
        m_aBtn[CSMW_BTN_MENU].GetXPos() + buttonWidth + CharacterSelectionDetail::kInfoSpacing;
    m_asprBack[CSMW_SPR_INFO].SetPosition(infoX, nYCoord + CharacterSelectionDetail::kInfoOffsetY);

    const int windowRightX = nXCoord + CWin::GetWidth();
    m_asprBack[CSMW_SPR_DECO].SetPosition(windowRightX - (m_asprBack[CSMW_SPR_DECO].GetWidth() -
                                                          CharacterSelectionDetail::kDecorOffsetX),
                                          nYCoord - CharacterSelectionDetail::kDecorOffsetY);

    m_aBtn[CSMW_BTN_DELETE].SetPosition(windowRightX - buttonWidth, nYCoord);
    m_aBtn[CSMW_BTN_CONNECT].SetPosition(
        windowRightX - (buttonWidth * 2 + CharacterSelectionDetail::kButtonSpacing), nYCoord);
}

void CCharSelMainWin::Show(bool bShow)
{
    CWin::Show(bShow);

    for (auto &sprite : m_asprBack)
        sprite.Show(bShow);
    for (auto &button : m_aBtn)
        button.Show(bShow);
}

bool CCharSelMainWin::CursorInWin(int nArea)
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

void CCharSelMainWin::UpdateDisplay()
{
    m_aBtn[CSMW_BTN_CREATE].SetEnable(HasEmptyCharacterSlot());

    const bool hasSelection = (SelectedHero > -1);
    m_aBtn[CSMW_BTN_CONNECT].SetEnable(hasSelection);
    m_aBtn[CSMW_BTN_DELETE].SetEnable(hasSelection);

    if (!HasLiveCharacter())
    {
        CUIMng &rUIMng = LegacyUiManager();
        rUIMng.ShowWin(rUIMng.m_CharMakeWin.get());
    }
}

void CCharSelMainWin::UpdateWhileActive(double dDeltaTick)
{
    CUIMng &uiManager = LegacyUiManager();

    if (m_aBtn[CSMW_BTN_CONNECT].IsClick())
    {
        StartGame();
    }
    else if (m_aBtn[CSMW_BTN_MENU].IsClick())
    {
        uiManager.ShowWin(uiManager.m_SysMenuWin.get());
        uiManager.SetSysMenuWinShow(true);
    }
    else if (m_aBtn[CSMW_BTN_CREATE].IsClick())
    {
        uiManager.ShowWin(uiManager.m_CharMakeWin.get());
    }
    else if (m_aBtn[CSMW_BTN_DELETE].IsClick())
    {
        DeleteCharacter();
    }
}

void CCharSelMainWin::DeleteCharacter()
{
    CHARACTER *selected = GetSelectedCharacter();
    if (selected == nullptr)
        return;

    CUIMng &uiManager = LegacyUiManager();

    if (selected->GuildStatus != G_NONE)
    {
        uiManager.PopUpMsgWin(MESSAGE_DELETE_CHARACTER_GUILDWARNING);
    }
    else if (selected->CtlCode & (CTLCODE_02BLOCKITEM | CTLCODE_10ACCOUNT_BLOCKITEM))
    {
        uiManager.PopUpMsgWin(MESSAGE_DELETE_CHARACTER_ID_BLOCK);
    }
    else
    {
        uiManager.PopUpMsgWin(MESSAGE_DELETE_CHARACTER_CONFIRM);
    }
}

//*****************************************************************************
//*****************************************************************************

CCreditWin::CCreditWin(SessionKeeper &keeper)
    : CWin(keeper), m_aSpr(keeper), m_btnClose(keeper), m_eIllustState(HIDE),
      m_illustElapsed(CreditsDetail::DurationMs::zero()), m_byIllust(0),
      m_illustPaths(CreditsDetail::kIllustPaths), m_nNowIndex(0), m_nNameCount(0), m_anTextIndex{},
      m_aeTextState{}, m_textElapsed(CreditsDetail::DurationMs::zero())
{
}

CCreditWin::~CCreditWin()
{
    ReleaseIllustrations();
}

void CCreditWin::Create()
{
    CWin::Create(WindowWidth, WindowHeight);
    CWin::SetBgAlpha(255);

    float fScaleX = static_cast<float>(WindowWidth) / 800.0f;
    float fScaleY = static_cast<float>(WindowHeight) / 600.0f;

    m_aSpr[CRW_SPR_DECO].Create(189, 103, BITMAP_LOG_IN + 6);
    m_aSpr[CRW_SPR_LOGO].Create(290, 41, BITMAP_LOG_IN + 14, 0, NULL, 0, 0, false,
                                SPR_SIZING_DATUMS_LT, fScaleX, fScaleY);

    for (int i = CRW_SPR_TXT_HIDE0; i <= CRW_SPR_TXT_HIDE2; ++i)
    {
        m_aSpr[i].Create(800, 42, -1, 0, NULL, 0, 0, false, SPR_SIZING_DATUMS_LT, fScaleX, fScaleY);
        m_aSpr[i].SetColor(0, 0, 0);
    }

    m_btnClose.Create(54, 30, BITMAP_BUTTON + 2, 3, 2, 1);
    CWin::RegisterButton(&m_btnClose);
    SetPosition();
}

void CCreditWin::PreRelease()
{
    ReleaseIllustrations();
    for (int i = 0; i < CRW_SPR_MAX; ++i)
        m_aSpr[i].Release();
}

bool CCreditWin::CursorInWin(int nArea)
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

void CCreditWin::UpdateWhileActive(double deltaMilliseconds)
{
    const CreditsDetail::DurationMs deltaTime{deltaMilliseconds};

    if (m_btnClose.IsClick())
    {
        CloseWin();
        return;
    }
    else if (IsPress(VK_ESCAPE))
    {
        PlayBuffer(SOUND_CLICK01);
        CloseWin();
        LegacyUiManager().SetSysMenuWinShow(false);
        return;
    }

    AnimationText(deltaTime);
    if (m_aeTextState[CRW_INDEX_NAME] == HIDE)
        return;
    AnimationIllust(deltaTime);
}

void CCreditWin::CloseWin()
{
    LegacyUiManager().HideWin(this);

    SocketClient->ToConnectServer()->SendServerListRequest();

    StopMp3(MUSIC_MUTHEME);
    PlayMp3(MUSIC_MAIN_THEME);
}

void CCreditWin::Init()
{
    m_eIllustState = FADEIN;
    m_illustElapsed = CreditsDetail::DurationMs::zero();
    m_byIllust = 0;
    LoadIllust();

    for (int i = 0; i <= CRW_INDEX_NAME; ++i)
    {
        m_aeTextState[i] = FADEIN;
        m_aSpr[CRW_SPR_TXT_HIDE0 + i].SetAlpha(255);
    }
    m_textElapsed = CreditsDetail::DurationMs::zero();
    m_nNowIndex = 0;
    m_nNameCount = 0;
    SetTextIndex();
}

bool CCreditWin::SetTextIndex()
{
    if (0 == m_aCredit[m_nNowIndex].byClass)
    {
        PlayBuffer(SOUND_CLICK01);
        CloseWin();
        return false;
    }

    if (1 == m_aCredit[m_nNowIndex].byClass)
    {
        m_anTextIndex[CRW_INDEX_DEPARTMENT] = m_nNowIndex;
        ++m_nNowIndex;
    }
    if (2 == m_aCredit[m_nNowIndex].byClass)
    {
        m_anTextIndex[CRW_INDEX_TEAM] = m_nNowIndex;
        ++m_nNowIndex;
    }

    int iNameCnt = 0;
    for (int i = 0; i < 4; ++i)
    {
        iNameCnt = i;
        if (3 == m_aCredit[m_nNowIndex].byClass)
        {
            m_anTextIndex[CRW_INDEX_NAME0 + i] = m_nNowIndex;
            ++m_nNowIndex;
        }
        else
            break;
    }
    m_nNameCount = iNameCnt;
    return true;
}

void CCreditWin::LoadIllust()
{
    float fScaleX = static_cast<float>(WindowWidth) / 800.0f;
    float fScaleY = static_cast<float>(WindowHeight) / 600.0f;

    for (int i = 0; i < 2; ++i)
    {
        const auto &illustPath = m_illustPaths[m_byIllust][i];
        if (ownedIllustrations_[i])
            DeleteBitmap(BITMAP_TEMP + i);
        ownedIllustrations_[i] =
            LoadBitmapW(illustPath, BITMAP_TEMP + i, LegacyTextureFilter::Linear);

        m_aSpr[i].Create(400, 400, BITMAP_TEMP + i, 0, NULL, 0, 0, false, SPR_SIZING_DATUMS_LT,
                         fScaleX, fScaleY);
        m_aSpr[i].SetAlpha(0);
        m_aSpr[i].Show(true);
    }

    m_aSpr[CRW_SPR_PIC_L].SetPosition(0, 126);
    m_aSpr[CRW_SPR_PIC_R].SetPosition(400, 126);
}

bool CCreditWin::AdvanceTextPhase()
{
    const auto state = m_aeTextState[CRW_INDEX_NAME];
    if (state == FADEIN)
    {
        for (auto &phase : m_aeTextState)
            if (phase == FADEIN)
                phase = SHOW;
    }
    else if (state == SHOW)
    {
        m_aeTextState[CRW_INDEX_NAME] = FADEOUT;
        if (m_aCredit[m_nNowIndex].byClass != 3)
        {
            m_aeTextState[CRW_INDEX_TEAM] = FADEOUT;
            if (m_aCredit[m_nNowIndex].byClass != 2)
                m_aeTextState[CRW_INDEX_DEPARTMENT] = FADEOUT;
        }
    }
    else
    {
        for (auto &phase : m_aeTextState)
            if (phase == FADEOUT)
                phase = FADEIN;
        if (!SetTextIndex())
        {
            m_aeTextState[CRW_INDEX_NAME] = HIDE;
            return false;
        }
    }
    return true;
}

void CCreditWin::AnimationText(CreditsDetail::DurationMs deltaTime)
{
    while (deltaTime > DurationMs::zero() && m_aeTextState[CRW_INDEX_NAME] != HIDE)
    {
        const auto duration = m_aeTextState[CRW_INDEX_NAME] == SHOW
                                  ? CreditsDetail::kNameShowDuration
                                  : CreditsDetail::kTextFadeDuration;
        const auto step = std::min(deltaTime, duration - m_textElapsed);
        m_textElapsed += step;
        deltaTime -= step;
        const double progress = m_textElapsed / CreditsDetail::kTextFadeDuration;
        constexpr double opaqueAlpha = 255.0;
        for (int index = 0; index <= CRW_INDEX_NAME; ++index)
        {
            const auto state = m_aeTextState[index];
            const double alpha = state == FADEIN    ? opaqueAlpha * (1.0 - progress)
                                 : state == FADEOUT ? opaqueAlpha * progress
                                                    : 0.0;
            m_aSpr[CRW_SPR_TXT_HIDE0 + index].SetAlpha(static_cast<BYTE>(std::lround(alpha)));
        }
        if (m_textElapsed >= duration)
        {
            m_textElapsed = DurationMs::zero();
            if (!AdvanceTextPhase())
                return;
        }
    }
}

void CCreditWin::AnimationIllust(CreditsDetail::DurationMs deltaTime)
{
    if (deltaTime <= DurationMs::zero() || m_eIllustState == HIDE)
        return;
    const auto previousIllustration = m_byIllust;
    while (deltaTime > DurationMs::zero())
    {
        const auto duration = m_eIllustState == SHOW ? CreditsDetail::kIllustShowDuration
                                                     : CreditsDetail::kIllustFadeDuration;
        const auto step = std::min(deltaTime, duration - m_illustElapsed);
        m_illustElapsed += step;
        deltaTime -= step;
        if (m_illustElapsed < duration)
            break;
        m_illustElapsed = DurationMs::zero();
        if (m_eIllustState == FADEIN)
            m_eIllustState = SHOW;
        else if (m_eIllustState == SHOW)
            m_eIllustState = FADEOUT;
        else
        {
            m_eIllustState = FADEIN;
            m_byIllust = (m_byIllust + 1) % CRW_ILLUST_MAX;
        }
    }
    if (previousIllustration != m_byIllust)
        LoadIllust();
    const double progress = m_illustElapsed / CreditsDetail::kIllustFadeDuration;
    constexpr double opaqueAlpha = 255.0;
    const double alpha = m_eIllustState == FADEIN    ? opaqueAlpha * progress
                         : m_eIllustState == FADEOUT ? opaqueAlpha * (1.0 - progress)
                                                     : opaqueAlpha;
    m_aSpr[CRW_SPR_PIC_L].SetAlpha(static_cast<BYTE>(std::lround(alpha)));
    m_aSpr[CRW_SPR_PIC_R].SetAlpha(static_cast<BYTE>(std::lround(alpha)));
}

// Two-button confirmation, modelled on the game's other OK/Cancel dialogs (e.g.
// the guild-request box). A bold yellow "WARNING!!!" header sits above the
// orange message body.

SEASON3B::CALLBACK_RESULT RememberPasswordDetail::CRememberPasswordMsgBoxLayout::OnOk(
    SEASON3B::CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &)
{
    g_Choice = UI::Login::RememberPasswordChoice::Ok;
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, SEASON3B::MSGBOX_EVENT_DESTROY);
    return SEASON3B::CALLBACK_BREAK;
}

SEASON3B::CALLBACK_RESULT RememberPasswordDetail::CRememberPasswordMsgBoxLayout::OnCancel(
    SEASON3B::CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &)
{
    g_Choice = UI::Login::RememberPasswordChoice::Cancel;
    PlayBuffer(SOUND_CLICK01);
    pOwner->SendEvent(pOwner, SEASON3B::MSGBOX_EVENT_DESTROY);
    return SEASON3B::CALLBACK_BREAK;
}
// namespace

namespace UI::Login
{
void OpenRememberPasswordPrompt(SessionKeeper &keeper)
{
    keeper.RememberPasswordChoiceStorage() = RememberPasswordChoice::Pending;
    SEASON3B::CreateMessageBox(
        MSGBOX_LAYOUT_CLASS(RememberPasswordDetail::CRememberPasswordMsgBoxLayout, keeper));
}
} // namespace UI::Login

RememberPasswordDetail::CRememberPasswordMsgBoxLayout::CRememberPasswordMsgBoxLayout(
    SessionKeeper &keeper)
    : SEASON3B::TMsgBoxLayout<SEASON3B::CNewUICommonMessageBox>(keeper),
      g_Choice(keeper.RememberPasswordChoiceStorage())
{
}
