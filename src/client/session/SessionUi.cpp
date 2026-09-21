#include "session/SessionUi.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "app/AppWindow.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "data/WorldData.h"
#include "domain/Automation.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/ChatSocial.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/Guild.h"
#include "domain/ItemsSkills.h"
#include "domain/MapPresentation.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Quests.h"
#include "domain/Shop.h"
#include "domain/WorldPhysics.h"
#include "domain/WorldSimulation.h"
#include "I18N/All.h"
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
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
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

bool LegacySessionUiView::BeginPresentation() noexcept
{
    return true;
}

bool LegacySessionUiView::FinishPresentation() noexcept
{
    return true;
}

bool LegacySessionUiView::UsesSessionOwnedLegacyBehavior() const noexcept
{
    return true;
}

SessionUiLegacyBindings::SessionUiLegacyBindings(SessionKeeper &keeper) noexcept
    : SessionLegacyCalls(keeper), g_strSelectedML(keeper.AssetLanguage()),
      g_RenderText(keeper.SessionText()), g_hWnd(keeper.PlatformWindowHandle()),
      g_bWndActive(keeper.PlatformWindowActive()), WindowWidth(keeper.PlatformWindowWidth()),
      WindowHeight(keeper.PlatformWindowHeight()), g_Camera(keeper.CameraStateObject()),
      FPS_ANIMATION_FACTOR(keeper.FrameAnimationFactor()), WorldTime(keeper.FrameWorldTime()),
      g_ErrorReport(keeper.ErrorReport()), g_ConsoleDebug(keeper.ConsoleDebug()),
      gMapManager(keeper.MapManagerObject()), g_PortalMgr(keeper.PortalManagerObject()),
      UserCount(keeper.KanturuUserCount()), MonsterCount(keeper.KanturuMonsterCount()),
      g_MessageBox(keeper.MessageBoxManagerObject()),
      g_pSingleTextInputBox(keeper.SingleTextInputBox()),
      g_pSinglePasswdInputBox(keeper.SinglePasswordInputBox()),
      g_pMercenaryInputBox(keeper.MercenaryInputBox()),
      focusedTextInputBox_(keeper.FocusedTextInputBox()), g_dwKeyFocusUIID(keeper.KeyFocusUiId()),
      MouseWheel(keeper.MouseWheelState()), g_csMatchInfo(keeper.EventMatch()),
      g_MixRecipeMgr(keeper.MixRecipeManager()), g_SocketItemMgr(keeper.SocketItemManager()),
      g_csItemOption(keeper.ItemOptionManager()), g_csQuest(keeper.QuestObject()),
      g_QuestMng(keeper.QuestManagerObject()), iUnMixIndex(keeper.ComGemStorage().iUnMixIndex),
      iUnMixLevel(keeper.ComGemStorage().iUnMixLevel), m_bType(keeper.ComGemStorage().m_bType),
      m_cGemType(keeper.ComGemStorage().m_cGemType), m_cComType(keeper.ComGemStorage().m_cComType),
      m_cCount(keeper.ComGemStorage().m_cCount), m_iValue(keeper.ComGemStorage().m_iValue),
      m_cPercent(keeper.ComGemStorage().m_cPercent), m_cState(keeper.ComGemStorage().m_cState),
      m_cErr(keeper.ComGemStorage().m_cErr)
{
}

SessionKeeper &SessionUiLegacyBindings::SessionOrigin() const noexcept
{
    return sessionKeeper_;
}

MapProcess &SessionUiLegacyBindings::MapProcessForConstruction() const noexcept
{
    return sessionKeeper_.GameplayForConstruction().TheMapProcess();
}

SessionGameplayUnit &SessionUiLegacyBindings::GameplayForConstruction() const noexcept
{
    return sessionKeeper_.GameplayForConstruction();
}

SessionRenderUnit &SessionUiLegacyBindings::RendererForConstruction() const noexcept
{
    return sessionKeeper_.RendererForConstruction();
}

unsigned int SessionUiLegacyBindings::ModernUiViewportWidth() const noexcept
{
    const unsigned int width = RendererForConstruction().ModernUiLogicalViewportWidth();
    return width == 0 ? WindowWidth : width;
}

unsigned int SessionUiLegacyBindings::ModernUiViewportHeight() const noexcept
{
    const unsigned int height = RendererForConstruction().ModernUiLogicalViewportHeight();
    return height == 0 ? WindowHeight : height;
}

float SessionUiLegacyBindings::ModernUiScale() const noexcept
{
    return sessionKeeper_.ApplicationConfig().rmlUiScale;
}

float SessionUiLegacyBindings::ModernUiScreenRateX() const noexcept
{
    return WindowWidth == 0 ? g_fScreenRate_x
                            : g_fScreenRate_x * ModernUiViewportWidth() / WindowWidth;
}

float SessionUiLegacyBindings::ModernUiScreenRateY() const noexcept
{
    return WindowHeight == 0 ? g_fScreenRate_y
                             : g_fScreenRate_y * ModernUiViewportHeight() / WindowHeight;
}

SessionNetworkUnit &SessionUiLegacyBindings::NetworkForConstruction() const noexcept
{
    return sessionKeeper_.NetworkForConstruction();
}

SessionUiUnit &SessionUiLegacyBindings::UiForConstruction() const noexcept
{
    return sessionKeeper_.UiForConstruction();
}

SessionGameDataUnit &SessionUiLegacyBindings::GameDataForConstruction() const noexcept
{
    return sessionKeeper_.GameDataForConstruction();
}

MUHelper::SessionMuHelperUnit &SessionUiLegacyBindings::MuHelperForConstruction() const noexcept
{
    return sessionKeeper_.MuHelperForConstruction();
}

ApplicationConfigUnit &SessionUiLegacyBindings::ApplicationConfigForConstruction() const noexcept
{
    return sessionKeeper_.ApplicationConfigForConstruction();
}

ApplicationAudio &SessionUiLegacyBindings::ApplicationAudioForConstruction() const noexcept
{
    return sessionKeeper_.ApplicationAudioForConstruction();
}

AppWindow &SessionUiLegacyBindings::AppWindowForConstruction() const noexcept
{
    return sessionKeeper_.AppWindowForConstruction();
}

SessionUiUnit::SessionUiUnit(SessionKeeper &keeper, SessionLifecycleObserver *observer) noexcept
    : SessionUiLegacyBindings(keeper), AbuseFilter(keeper.AbuseFilterStorage()),
      AbuseNameFilter(keeper.AbuseNameFilterStorage()),
      AbuseFilterNumber(keeper.AbuseFilterCount()),
      AbuseNameFilterNumber(keeper.AbuseNameFilterCount()), g_pTimer(keeper.FrameTimer()),
      g_dwLastUIID(keeper.LastUiId()), g_nChaosTaxRate(keeper.ChaosTaxRate()),
      personalShopTitle_(keeper), mapName_(keeper), g_pUIMapName(mapName_),
      reconnectDialog_(keeper), notices_(keeper), observer_(observer)
{
    if (sessionKeeper_.RegisterUi(*this))
    {
        (void)sessionKeeper_.InitializeFriendMenuForConstruction();
    }
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::UiUnitConstructed);
    }
}

bool SessionUiUnit::InitializeConnectedChildren()
{
    if (legacyUiManager_ || newUiSystem_ || unmixGemList_)
    {
        return false;
    }
    legacyUiManager_ = std::make_unique<CUIMng>(sessionKeeper_);
    newUiSystem_ = std::make_unique<SEASON3B::CNewUISystem>(sessionKeeper_);
    unmixGemList_ = std::make_unique<CUIUnmixgemList>(sessionKeeper_);
    return true;
}

SessionUiUnit::~SessionUiUnit()
{
    ShutdownLegacyUi();
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::UiUnitDestroyed);
    }
}

bool SessionUiUnit::RegisterChatConnection(std::int32_t handle, DWORD windowId)
{
    return handle > 0 && windowId != 0 && chatConnections_.emplace(handle, windowId).second;
}

void SessionUiUnit::UnregisterChatConnection(std::int32_t handle) noexcept
{
    chatConnections_.erase(handle);
}

void SessionUiUnit::ProcessChatPacket(std::int32_t handle, const BYTE *receiveBuffer,
                                      std::int32_t size)
{
    const auto connection = chatConnections_.find(handle);
    if (connection != chatConnections_.end())
    {
        TranslateChattingProtocol(connection->second, receiveBuffer, size);
    }
}

bool SessionUiUnit::BeginPresentation() noexcept
{
    SessionUiView *ui = sessionKeeper_.UiView();
    return ui != nullptr && (ui->UsesSessionOwnedLegacyBehavior() ? BeginScenePresentationUpdate()
                                                                  : ui->BeginPresentation());
}

bool SessionUiUnit::FinishPresentation() noexcept
{
    SessionUiView *ui = sessionKeeper_.UiView();
    return ui != nullptr && (ui->UsesSessionOwnedLegacyBehavior() ? FinishScenePresentationUpdate()
                                                                  : ui->FinishPresentation());
}

bool SessionUiUnit::ProcessModernUiInput(const SessionInputEvent &event)
{
    return legacyUiManager_ != nullptr && legacyUiManager_->ProcessModernUiInput(event);
}

std::optional<UI::Modern::RmlTextInputArea> SessionUiUnit::ModernTextInputArea() const
{
    const auto area =
        legacyUiManager_ != nullptr ? legacyUiManager_->ModernTextInputArea() : std::nullopt;
    focusedModernUiObject_.store(area.has_value() && legacyUiManager_ != nullptr
                                     ? legacyUiManager_->FocusedModernUiObject()
                                     : nullptr,
                                 std::memory_order_release);
    return area;
}

SEASON3B::CNewUIObj *SessionUiUnit::FocusedModernUiObject() const noexcept
{
    return focusedModernUiObject_.load(std::memory_order_acquire);
}
int SessionUiUnit::DivideStringByPixel(wchar_t *alpszDst, int nDstRow, int nDstColumn,
                                       const wchar_t *lpszSrc, int nPixelPerLine, bool bSpaceInsert,
                                       const wchar_t szNewlineChar)
{
    if (nullptr == alpszDst || 0 >= nDstRow || 0 >= nDstColumn || nullptr == lpszSrc ||
        16 > nPixelPerLine)
        return 0;

    std::wstring szWorkSrc(lpszSrc); // Convert lpszSrc to std::wstring

    wchar_t szWorkToken[1024];
    int nLine = 0;

    const wchar_t delimiters[] = {szNewlineChar, L'\0'};
    wchar_t *context = nullptr;
    wchar_t *pszToken = wcstok_s(&szWorkSrc[0], delimiters, &context);

    while (pszToken != nullptr)
    {
        if (bSpaceInsert)
        {
            mu_swprintf(szWorkToken, L" %ls", pszToken);
            nLine += CutText3(szWorkToken, alpszDst + nLine * nDstColumn, nPixelPerLine, nDstRow,
                              nDstColumn);
        }
        else
        {
            nLine += CutText3(pszToken, alpszDst + nLine * nDstColumn, nPixelPerLine, nDstRow,
                              nDstColumn);
        }

        pszToken = wcstok_s(nullptr, delimiters, &context);
    }

    return nLine;
}

int SessionLegacyCalls::DivideStringByPixel(wchar_t *output, int outputRows, int outputColumns,
                                            const wchar_t *source, int pixelsPerLine,
                                            bool insertSpace, const wchar_t newlineCharacter)
{
    return sessionKeeper_.Ui()->DivideStringByPixel(output, outputRows, outputColumns, source,
                                                    pixelsPerLine, insertSpace, newlineCharacter);
}

void SessionLegacyCalls::RenderTime()
{
    sessionKeeper_.Gameplay()->RenderTime();
}

void SessionLegacyCalls::RenderResult()
{
    sessionKeeper_.Gameplay()->RenderResult();
}

void SessionLegacyCalls::SetPosition(int x, int y)
{
    sessionKeeper_.Gameplay()->SetPosition(x, y);
}

void SessionGameplayUnit::RenderTime()
{
    if (g_csMatchInfo != nullptr)
    {
        g_csMatchInfo->RenderTime();
    }
}

void SessionGameplayUnit::RenderResult()
{
    if (g_csMatchInfo != nullptr)
    {
        g_csMatchInfo->RenderMatchResult();
    }
}

void SessionGameplayUnit::SetPosition(int x, int y)
{
    if (g_csMatchInfo != nullptr)
    {
        g_csMatchInfo->SetPosition(x, y);
    }
}

bool SessionLegacyCalls::AddShopTitle(int key, CHARACTER *player, const std::wstring &title)
{
    return sessionKeeper_.Ui()->AddShopTitle(key, player, title);
}

void SessionLegacyCalls::RemoveShopTitle(CHARACTER *player)
{
    sessionKeeper_.Ui()->RemoveShopTitle(player);
}
void SessionLegacyCalls::RemoveAllShopTitle()
{
    sessionKeeper_.Ui()->RemoveAllShopTitle();
}
void SessionLegacyCalls::RemoveAllShopTitleExceptHero()
{
    sessionKeeper_.Ui()->RemoveAllShopTitleExceptHero();
}
CHARACTER *SessionLegacyCalls::FindCharacterTagShopTitle(int key)
{
    return sessionKeeper_.Ui()->FindCharacterTagShopTitle(key);
}
void SessionLegacyCalls::ShowShopTitles()
{
    sessionKeeper_.Ui()->ShowShopTitles();
}
void SessionLegacyCalls::HideShopTitles()
{
    sessionKeeper_.Ui()->HideShopTitles();
}
void SessionLegacyCalls::EnableShopTitleDraw(CHARACTER *player)
{
    sessionKeeper_.Ui()->EnableShopTitleDraw(player);
}
void SessionLegacyCalls::DisableShopTitleDraw(CHARACTER *player)
{
    sessionKeeper_.Ui()->DisableShopTitleDraw(player);
}
bool SessionLegacyCalls::IsShopTitleVisible(CHARACTER *player)
{
    return sessionKeeper_.Ui()->IsShopTitleVisible(player);
}
bool SessionLegacyCalls::IsShopInViewport(CHARACTER *player)
{
    return sessionKeeper_.Ui()->IsShopInViewport(player);
}
void SessionLegacyCalls::GetShopTitle(CHARACTER *player, std::wstring &title)
{
    sessionKeeper_.Ui()->GetShopTitle(player, title);
}
void SessionLegacyCalls::GetShopTitleSummary(CHARACTER *player, std::wstring &summary)
{
    sessionKeeper_.Ui()->GetShopTitleSummary(player, summary);
}
void SessionLegacyCalls::UpdatePersonalShopTitleImp()
{
    sessionKeeper_.Ui()->UpdatePersonalShopTitleImp();
}

void SessionLegacyCalls::DrawPersonalShopTitleImp()
{
    sessionKeeper_.Ui()->DrawPersonalShopTitleImp();
}
int SessionLegacyCalls::CalcRecoveryZen(BYTE type, wchar_t *Text)
{
    return sessionKeeper_.Ui()->CalcRecoveryZen(type, Text);
} // OMF-00806
void SessionLegacyCalls::RecoverPet(BYTE type)
{
    return sessionKeeper_.Ui()->RecoverPet(type);
} // OMF-00807

/**
 * @brief Performs one-time initialization when entering the main game scene.
 *
 * This function is called once when transitioning from character selection to the main game.
 * It performs the following tasks:
 * - Sends character selection to the game server
 * - Initializes UI systems (chat, party, guild, etc.)
 * - Sets up camera and input configuration
 * - Clears previous scene state and prepares for gameplay
 *
 * @note This function should only be called once per main scene entry.
 */
void SessionUiUnit::InitializeMainScene()
{
    g_pMainFrame->ResetSkillHotKey();
    const wchar_t *const selectedName = CharacterAttribute->Name;

    g_ConsoleDebug.Write(MCD_NORMAL, L"Join the game with the following character: %ls",
                         selectedName);
    g_ErrorReport.Write(L"> Character selected <%d> \"%ls\"\r\n", SelectedHero + 1, selectedName);

    InitMainScene = true;

    g_ConsoleDebug.Write(MCD_SEND, L"SendRequestJoinMapServer");

    CurrentProtocolState = REQUEST_JOIN_MAP_SERVER;
    SocketClient->ToGameServer()->SendSelectCharacter(selectedName);

    // Remember which character is in play so auto-reconnect can re-select it.
    sessionKeeper_.Network()->Reconnect().CacheCharacter(selectedName);

    LegacyUiManager().CreateMainScene();

    g_Camera.Angle[2] = -45.f;

    ClearInput();
    InputEnable = false;
    TabInputEnable = false;
    InputTextWidth = 256;
    InputTextMax[0] = 42;
    InputTextMax[1] = 10;
    InputNumber = 2;
    for (int i = 0; i < MAX_WHISPER; i++)
    {
        g_pChatListBox->AddText(L"", L"", SEASON3B::TYPE_WHISPER_MESSAGE);
    }

    g_GuildNotice[0][0] = '\0';
    g_GuildNotice[1][0] = '\0';

    g_pPartyManager->Create();

    g_pChatListBox->ClearAll();
    g_pSystemLogBox->ClearAll();

    g_pSlideHelpMgr->Init();
    g_pUIMapName.Init();
    g_pNewUIMuHelper->Reset();

    g_GuildCache.Reset();
    g_PortalMgr.Reset();

    ClearAllObjectBlurs();

    SetFocus(g_hWnd);

    g_ErrorReport.Write(L"> Main Scene init success. ");
    g_ErrorReport.WriteCurrentTime();

    g_ConsoleDebug.Write(MCD_NORMAL, L"MainScene Init Success");
}

/**
 * @brief Resets per-frame state variables at the start of each frame.
 *
 * Initializes frame-dependent state including:
 * - Earthquake effect damping
 * - UI interaction flags (inventory, skill checks, mouse window state)
 *
 * @note Called every frame during the main scene update loop.
 */
void SessionUiUnit::InitializeSceneFrame()
{
    EarthQuake *= 0.2f;

    CheckInventory = NULL;
    CheckSkill = -1;
    MouseOnWindow = false;
}

void SessionUiUnit::ClearInput(BOOL bClearWhisperTarget)
{
    InputIndex = 0;
    InputResidentNumber = -1;
    for (int i = 0; i < 10; i++)
    {
        if (i == 1 && bClearWhisperTarget == FALSE)
            continue;
        for (int j = 0; j < 256; j++)
            InputText[i][j] = 0;
        InputLength[i] = 0;
        InputTextHide[i] = 0;
    }

    if (g_iChatInputType == 1)
    {
        if (g_pSingleTextInputBox != nullptr)
        {
            g_pSingleTextInputBox->SetText(nullptr);
        }
        if (g_pSinglePasswdInputBox != nullptr)
        {
            g_pSinglePasswdInputBox->SetText(nullptr);
        }
    }
}

bool SessionUiUnit::BeginMainScenePresentationUpdate()
{
    if (!InitMainScene)
    {
        InitializeMainScene();
    }

    if (MainSceneReady == false)
    {
        return true;
    }

    InitializeSceneFrame();

    // While the reconnect dialog is up it's modal: block world clicks so they
    // don't move the hero and instead reach the dialog's Cancel button.
    if (sessionKeeper_.Network()->Reconnect().IsActive())
        MouseOnWindow = true;

    UpdateUIAndInput();

    return true;
}

void SessionLegacyCalls::InitializeMainScene()
{
    return sessionKeeper_.Ui()->InitializeMainScene();
} // OMF-01787
void SessionLegacyCalls::InitializeSceneFrame()
{
    return sessionKeeper_.Ui()->InitializeSceneFrame();
} // OMF-01788
void SessionLegacyCalls::ClearInput(BOOL clearWhisperTarget)
{
    sessionKeeper_.Ui()->ClearInput(clearWhisperTarget);
}

bool SessionUiUnit::CheckAbuseFilter(wchar_t *Text, bool bCheckSlash)
{
    if (bCheckSlash == true)
    {
        if (Text[0] == '/')
        {
            return false;
        }
    }

    int icntText = 0;
    wchar_t TmpText[2048];
    int textLength = wcslen(Text);
    for (int i = 0; i < textLength; ++i)
    {
        if (Text[i] != 32)
        {
            TmpText[icntText] = Text[i];
            icntText++;
        }
    }
    TmpText[icntText] = 0;

    for (int i = 0; i < AbuseFilterNumber; i++)
    {
        if (FindText(TmpText, AbuseFilter[i]))
        {
            return true;
        }
    }
    return false;
}

bool SessionUiUnit::CheckAbuseNameFilter(wchar_t *Text)
{
    int icntText = 0;
    wchar_t TmpText[256];
    int textLength = wcslen(Text);
    for (int i = 0; i < textLength; ++i)
    {
        if (Text[i] != 32)
        {
            TmpText[icntText] = Text[i];
            icntText++;
        }
    }
    TmpText[icntText] = 0;

    for (int i = 0; i < AbuseNameFilterNumber; i++)
    {
        if (FindText(TmpText, AbuseNameFilter[i]))
        {
            return true;
        }
    }
    return false;
}

bool SessionUiUnit::CheckName()
{
    if (CheckAbuseNameFilter(InputText[0]) || CheckAbuseFilter(InputText[0]) ||
        FindText(InputText[0], L" ") || FindText(InputText[0], L"　") ||
        FindText(InputText[0], L".") || FindText(InputText[0], L"·") ||
        FindText(InputText[0], L"∼") || FindText(InputText[0], L"Webzen") ||
        FindText(InputText[0], L"WebZen") || FindText(InputText[0], L"webzen") ||
        FindText(InputText[0], L"WEBZEN") || FindText(InputText[0], I18N::Game::Operation) ||
        FindText(InputText[0], I18N::Game::WEBZEN))
        return true;
    return false;
}

// UI Utility Functions

BOOL SessionUiUnit::CheckOptionMouseClick(int iOptionPos_y, BOOL bPlayClickSound)
{
    if (CheckMouseIn((REFERENCE_WIDTH - 120) / 2, 30 + iOptionPos_y, 120, 22) && MouseLButtonPush)
    {
        MouseLButtonPush = false;
        MouseUpdateTime = 0;
        MouseUpdateTimeMax = 6;
        if (bPlayClickSound == TRUE)
            PlayBuffer(SOUND_CLICK01);
        return TRUE;
    }
    return FALSE;
}

BOOL SessionLegacyCalls::CheckOptionMouseClick(int optionPositionY, BOOL playClickSound)
{
    return sessionKeeper_.Ui()->CheckOptionMouseClick(optionPositionY, playClickSound);
}

/**
 * @brief Handles item use confirmation dialogs (fruits, consumables).
 */
void SessionUiUnit::SetupItemUseStateMessage(int num, int index, int message)
{
    wchar_t Name[50] = {
        0,
    };
    if (TargetItem.Type == ITEM_FRUITS)
    {
        switch (TargetItem.Level)
        {
        case 0:
            swprintf_s(Name, 50, L"%ls", I18N::Game::ENG);
            break;
        case 1:
            swprintf_s(Name, 50, L"%ls", I18N::Game::STA);
            break;
        case 2:
            swprintf_s(Name, 50, L"%ls", I18N::Game::AGI);
            break;
        case 3:
            swprintf_s(Name, 50, L"%ls", I18N::Game::STR);
            break;
        case 4:
            swprintf_s(Name, 50, L"%ls", I18N::Game::Command);
            break;
        }
    }

    if (message == MESSAGE_USE_STATE2)
        swprintf_s(g_lpszMessageBoxCustom[0], MAX_LENGTH_CMB, L"( %ls%ls )", Name,
                   I18N::Game::Fruit);
    else
        swprintf_s(g_lpszMessageBoxCustom[0], MAX_LENGTH_CMB, L"( %ls )", Name);

    num++;
    for (int i = 1; i < num; ++i)
    {
        swprintf_s(g_lpszMessageBoxCustom[i], MAX_LENGTH_CMB, I18N::Game::Lookup(index));
    }
    g_iNumLineMessageBoxCustom = num;
}

/**
 * @brief Handles personal shop price confirmation dialog.
 */
void SessionUiUnit::SetupPersonalShopWarningMessage(int num, int index)
{
    wchar_t szGold[256];
    ConvertGold(InputGold, szGold);
    swprintf_s(g_lpszMessageBoxCustom[0], MAX_LENGTH_CMB, I18N::Game::Lookup(index), szGold);

    for (int i = 1; i < num; ++i)
    {
        swprintf_s(g_lpszMessageBoxCustom[i], MAX_LENGTH_CMB, I18N::Game::Lookup(index + i));
    }
    g_iNumLineMessageBoxCustom = num;
}

/**
 * @brief Handles Chaos Castle entry confirmation with text wrapping.
 */
void SessionUiUnit::SetupChaosCastleCheckMessage(int num, int index)
{
    g_iNumLineMessageBoxCustom = 0;
    for (int i = 0; i < num; ++i)
    {
        g_iNumLineMessageBoxCustom += SeparateTextIntoLines(
            I18N::Game::Lookup(index + i), g_lpszMessageBoxCustom[g_iNumLineMessageBoxCustom],
            NUM_LINE_CMB, MAX_LENGTH_CMB);
    }
}

/**
 * @brief Handles gem combination/integration confirmation dialog.
 */
void SessionUiUnit::SetupGemIntegrationMessage()
{
    wchar_t tBuf[MAX_GLOBAL_TEXT_STRING];
    wchar_t tLines[2][30];
    for (int t = 0; t < 2; ++t)
        memset(tLines[t], 0, 20);
    g_iNumLineMessageBoxCustom = 0;

    if (isComMode())
    {
        if (m_cGemType == 0)
            swprintf_s(tBuf, MAX_GLOBAL_TEXT_STRING, I18N::Game::AreYouSureToCombineSXD,
                       I18N::Game::JewelOfBless, m_cCount);
        else
            swprintf_s(tBuf, MAX_GLOBAL_TEXT_STRING, I18N::Game::AreYouSureToCombineSXD,
                       I18N::Game::JewelOfSoul, m_cCount);

        g_iNumLineMessageBoxCustom +=
            SeparateTextIntoLines(tBuf, tLines[g_iNumLineMessageBoxCustom], 2, 30);

        for (int t = 0; t < 2; ++t)
            wcscpy_s(g_lpszMessageBoxCustom[t], MAX_LENGTH_CMB, tLines[t]);

        swprintf_s(g_lpszMessageBoxCustom[g_iNumLineMessageBoxCustom], MAX_LENGTH_CMB,
                   I18N::Game::CombinationCostDZen, m_iValue);
        ++g_iNumLineMessageBoxCustom;
    }
    else
    {
        int t_GemLevel = GetUnMixGemLevel() + 1;
        if (m_cGemType == 0)
            swprintf_s(tBuf, MAX_GLOBAL_TEXT_STRING, I18N::Game::AreYouSureToDisbandSD,
                       I18N::Game::JewelOfBless, t_GemLevel);
        else
            swprintf_s(tBuf, MAX_GLOBAL_TEXT_STRING, I18N::Game::AreYouSureToDisbandSD,
                       I18N::Game::JewelOfSoul, t_GemLevel);

        g_iNumLineMessageBoxCustom +=
            SeparateTextIntoLines(tBuf, tLines[g_iNumLineMessageBoxCustom], 2, 30);

        for (int t = 0; t < 2; ++t)
            wcscpy_s(g_lpszMessageBoxCustom[t], MAX_LENGTH_CMB, tLines[t]);

        swprintf_s(g_lpszMessageBoxCustom[g_iNumLineMessageBoxCustom], MAX_LENGTH_CMB,
                   I18N::Game::DissolvingCostDZen, m_iValue);
        ++g_iNumLineMessageBoxCustom;
    }
}

/**
 * @brief Handles skill cancellation confirmation dialog.
 */
void SessionUiUnit::SetupCancelSkillMessage(int index)
{
    wchar_t tBuf[MAX_GLOBAL_TEXT_STRING];
    swprintf_s(tBuf, MAX_GLOBAL_TEXT_STRING, L"%ls%ls", SkillAttribute[index].Name,
               I18N::Game::WouldYouLikeToCancel);
    g_iNumLineMessageBoxCustom =
        SeparateTextIntoLines(tBuf, g_lpszMessageBoxCustom[0], 2, MAX_LENGTH_CMB);
    g_iCancelSkillTarget = index;
}

/**
 * @brief Handles generic message box with simple text lines.
 */
void SessionUiUnit::SetupGenericMessage(int num, int index)
{
    for (int i = 0; i < num; ++i)
    {
        wcscpy_s(g_lpszMessageBoxCustom[i], MAX_LENGTH_CMB, I18N::Game::Lookup(index + i));
    }
    g_iNumLineMessageBoxCustom = num;
}

/**
 * @brief Configures button layout based on message type.
 */
void SessionUiUnit::ConfigureMessageBoxButtons(int message)
{
    ZeroMemory(g_iCustomMessageBoxButton, NUM_BUTTON_CMB * NUM_PAR_BUTTON_CMB * sizeof(int));

    int iOkButton[5] = {1, 21, 90, 70, 21};
    int iCancelButton[5] = {3, 120, 90, 70, 21};

    if (message == MESSAGE_USE_STATE2)
    {
        iOkButton[1] = 22;
        iOkButton[2] = 92;
        iOkButton[3] = 49;
        iOkButton[4] = 16;

        iCancelButton[1] = 82;
        iCancelButton[2] = 92;
        iCancelButton[3] = 49;
        iCancelButton[4] = 16;

        g_iCustomMessageBoxButton_Cancel[0] = 5;
        g_iCustomMessageBoxButton_Cancel[1] = 142;
        g_iCustomMessageBoxButton_Cancel[2] = 92;
        g_iCustomMessageBoxButton_Cancel[3] = 49;
        g_iCustomMessageBoxButton_Cancel[4] = 16;
    }

    if (message == MESSAGE_CHAOS_CASTLE_CHECK)
    {
        iOkButton[2] = 120;
        iCancelButton[2] = 120;
    }

    memcpy(g_iCustomMessageBoxButton[0], iOkButton, 5 * sizeof(int));
    memcpy(g_iCustomMessageBoxButton[1], iCancelButton, 5 * sizeof(int));
}

/**
 * @brief Displays a custom message box with OK/Cancel buttons based on message type.
 * @param num Number of text lines
 * @param index Text index in GlobalText array
 * @param message Message type (MESSAGE_USE_STATE, MESSAGE_PERSONALSHOP_WARNING, etc.)
 * @return Always returns TRUE
 */
BOOL SessionUiUnit::ShowCheckBox(int num, int index, int message)
{
    if (message == MESSAGE_USE_STATE || message == MESSAGE_USE_STATE2)
    {
        SetupItemUseStateMessage(num, index, message);
    }
    else if (message == MESSAGE_PERSONALSHOP_WARNING)
    {
        SetupPersonalShopWarningMessage(num, index);
    }
    else if (message == MESSAGE_CHAOS_CASTLE_CHECK)
    {
        SetupChaosCastleCheckMessage(num, index);
    }
    else if (message == MESSAGE_GEM_INTEGRATION3)
    {
        SetupGemIntegrationMessage();
    }
    else if (message == MESSAGE_CANCEL_SKILL)
    {
        SetupCancelSkillMessage(index);
    }
    else
    {
        SetupGenericMessage(num, index);
    }

    ConfigureMessageBoxButtons(message);

    return true;
}

void SessionLegacyCalls::SetupItemUseStateMessage(int num, int index, int message)
{
    sessionKeeper_.Ui()->SetupItemUseStateMessage(num, index, message);
}
void SessionLegacyCalls::SetupPersonalShopWarningMessage(int num, int index)
{
    sessionKeeper_.Ui()->SetupPersonalShopWarningMessage(num, index);
}
void SessionLegacyCalls::SetupChaosCastleCheckMessage(int num, int index)
{
    sessionKeeper_.Ui()->SetupChaosCastleCheckMessage(num, index);
}
void SessionLegacyCalls::SetupGemIntegrationMessage()
{
    sessionKeeper_.Ui()->SetupGemIntegrationMessage();
}
void SessionLegacyCalls::SetupCancelSkillMessage(int index)
{
    sessionKeeper_.Ui()->SetupCancelSkillMessage(index);
}
void SessionLegacyCalls::SetupGenericMessage(int num, int index)
{
    sessionKeeper_.Ui()->SetupGenericMessage(num, index);
}
void SessionLegacyCalls::ConfigureMessageBoxButtons(int message)
{
    sessionKeeper_.Ui()->ConfigureMessageBoxButtons(message);
}
BOOL SessionLegacyCalls::ShowCheckBox(int num, int index, int message)
{
    return sessionKeeper_.Ui()->ShowCheckBox(num, index, message);
}

bool SessionLegacyCalls::CheckAbuseFilter(wchar_t *text, bool checkSlash)
{
    return sessionKeeper_.Ui()->CheckAbuseFilter(text, checkSlash);
}
bool SessionLegacyCalls::CheckAbuseNameFilter(wchar_t *text)
{
    return sessionKeeper_.Ui()->CheckAbuseNameFilter(text);
}
bool SessionLegacyCalls::CheckName()
{
    return sessionKeeper_.Ui()->CheckName();
}

void SessionLegacyCalls::ResetWantedList()
{
    sessionKeeper_.Ui()->ResetWantedList();
}
SEASON3B::CNewUIInventoryCtrl *SessionLegacyCalls::GetInventoryCtrl() const
{
    return sessionKeeper_.Ui()->GetInventoryCtrl();
} // OMF-00735
bool SessionLegacyCalls::FindWantedList()
{
    return sessionKeeper_.Ui()->FindWantedList();
} // OMF-00740
void SessionLegacyCalls::SelectFromList(int iIndex, int iLevel)
{
    return sessionKeeper_.Ui()->SelectFromList(iIndex, iLevel);
} // OMF-00741
void SessionLegacyCalls::MoveUnMixList()
{
    return sessionKeeper_.Ui()->MoveUnMixList();
} // OMF-00743
void SessionLegacyCalls::RenderUnMixList()
{
    return sessionKeeper_.Ui()->RenderUnMixList();
} // OMF-00744
bool SessionLegacyCalls::CheckInv()
{
    return sessionKeeper_.Ui()->CheckInv();
} // OMF-00746

namespace
{
constexpr unsigned int LoadingSceneUpdatesBeforeTransition = 2;
}

void SessionUiUnit::InitializeLoadingScene()
{
    CUIMng &rUIMng = LegacyUiManager();
    InitLoading = true;
    loadingSceneUpdates_ = 0;
    LoadBitmapW(L"Interface\\LSBg01.JPG", BITMAP_TITLE, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\LSBg02.JPG", BITMAP_TITLE + 1, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\LSBg03.JPG", BITMAP_TITLE + 2, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\LSBg04.JPG", BITMAP_TITLE + 3, LegacyTextureFilter::Linear);

    StopMp3(MUSIC_LOGIN_THEME);

    rUIMng.m_pLoadingScene = new CLoadingScene(sessionKeeper_);
    rUIMng.m_pLoadingScene->Create();
}

bool SessionUiUnit::AdvanceLoadingSceneOnOwner(bool visible)
{
    if (!InitLoading)
    {
        loadingSceneUpdates_ = 0;
        if (visible)
            InitializeLoadingScene();
        else
            InitLoading = true;
    }
    if (loadingSceneUpdates_++ < LoadingSceneUpdatesBeforeTransition)
        return true;
    auto &manager = LegacyUiManager();
    if (manager.m_pLoadingScene)
    {
        SAFE_DELETE(manager.m_pLoadingScene);
        for (int i = 0; i < 4; ++i)
            DeleteBitmap(BITMAP_TITLE + i);
    }
    SceneFlag = MAIN_SCENE;
    ClearInput();
    return true;
}
void SessionLegacyCalls::LoadingScene()
{
    return sessionKeeper_.Renderer()->LoadingScene();
} // OMF-01771

/**
 * @brief Generates a timestamped filename and message for screenshot capture.
 * @param outFileName Buffer to receive the filename
 * @param outMessage Buffer to receive the log message
 */
void SessionUiUnit::GenerateScreenshotFilename(wchar_t *outFileName, wchar_t *outMessage)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    mu_swprintf(outFileName, L"Screen(%02d_%02d-%02d_%02d)-%04d.jpg", st.wMonth, st.wDay, st.wHour,
                st.wMinute, GrabScreen);
    mu_swprintf(outMessage, I18N::Game::SScreenshotSaved, outFileName);

    wchar_t lpszTemp[64];
    mu_swprintf(lpszTemp, L" [%ls / %ls]", g_ServerListManager.GetSelectServerName(), Hero->ID);
    wcscat(outMessage, lpszTemp);
}

/**
 * @brief Captures the current frame buffer and saves it as a JPEG screenshot.
 */
void SessionUiUnit::CaptureScreenshot()
{

    if (WindowWidth == 0 || WindowHeight == 0 || pendingScreenshot_.has_value() ||
        submittedScreenshot_.has_value())
    {
        return;
    }
    const SessionReplayCompletion *const completed =
        sessionKeeper_.Renderer()->LastCompletedTarget().has_value()
            ? &*sessionKeeper_.Renderer()->LastCompletedTarget()
            : nullptr;
    const SessionDisplayView *const display = sessionKeeper_.Display();
    if (completed == nullptr || display == nullptr ||
        display->SurfaceGeneration() != completed->surfaceGeneration ||
        display->LocalRect().width == 0 || display->LocalRect().height == 0)
    {
        return;
    }

    std::uint64_t requestId = nextScreenshotRequestId_++;
    if (requestId == 0)
    {
        requestId = nextScreenshotRequestId_++;
    }
    ScreenshotRequest request{requestId,
                              completed->frameSequence,
                              completed->sessionId,
                              completed->generation,
                              completed->surfaceGeneration,
                              {0, 0, display->LocalRect().width, display->LocalRect().height},
                              GrabFileName};
    pendingScreenshot_ = std::move(request);

    ++GrabScreen;
    GrabScreen %= 10000;
}

std::optional<SessionUiUnit::ScreenshotRequest> SessionUiUnit::TakeScreenshotRequest() noexcept
{
    if (submittedScreenshot_)
    {
        const auto &completed = sessionKeeper_.Renderer()->LastCompletedTarget();
        // One GPU frame is in flight. A later completion proves the recorded
        // screenshot frame was discarded; its request can be submitted again.
        if (completed && completed->generation == submittedScreenshot_->generation &&
            completed->surfaceGeneration == submittedScreenshot_->sourceSurfaceGeneration &&
            completed->frameSequence > submittedScreenshotFrameSequence_)
        {
            pendingScreenshot_ = std::move(submittedScreenshot_);
            submittedScreenshot_.reset();
            submittedScreenshotFrameSequence_ = 0;
        }
    }
    if (!pendingScreenshot_.has_value() || submittedScreenshot_.has_value())
    {
        return std::nullopt;
    }
    try
    {
        ScreenshotRequest request = *pendingScreenshot_;
        pendingScreenshot_.reset();
        return request;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool SessionUiUnit::SubmitScreenshotRequest(ScreenshotRequest request,
                                            std::uint64_t frameSequence) noexcept
{
    if (frameSequence == 0 || pendingScreenshot_.has_value() || submittedScreenshot_.has_value())
    {
        return false;
    }
    try
    {
        submittedScreenshot_ = std::move(request);
        submittedScreenshotFrameSequence_ = frameSequence;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void SessionUiUnit::RestoreScreenshotRequest(ScreenshotRequest request) noexcept
{
    if (submittedScreenshot_.has_value() || pendingScreenshot_.has_value())
    {
        return;
    }
    try
    {
        pendingScreenshot_ = std::move(request);
        submittedScreenshotFrameSequence_ = 0;
    }
    catch (...)
    {
    }
}

bool SessionUiUnit::CompleteScreenshot(const RenderOwnerRequestCompletion &completion,
                                       std::span<const std::byte> rgba8) noexcept
{
    if (!IsValid(completion) || completion.kind != RenderOwnerRequestKind::DownloadTargetRgba8 ||
        !completion.succeeded || completion.destination.id.value != 0 ||
        !submittedScreenshot_.has_value())
    {
        return false;
    }
    const ScreenshotRequest &request = *submittedScreenshot_;
    if (completion.requestId != request.requestId || completion.sessionId != request.sessionId ||
        completion.generation != request.generation || request.sourceFrameSequence == 0 ||
        request.sourceFrameSequence >= submittedScreenshotFrameSequence_ ||
        completion.frameSequence != submittedScreenshotFrameSequence_ ||
        completion.surfaceGeneration != request.sourceSurfaceGeneration ||
        completion.width != request.rect.width || completion.height != request.rect.height ||
        completion.payloadByteCount != rgba8.size())
    {
        return false;
    }
    const std::size_t pixelCount =
        static_cast<std::size_t>(request.rect.width) * request.rect.height;
    if (pixelCount == 0 || rgba8.size() != pixelCount * 4u)
    {
        return false;
    }
    try
    {
        std::vector<unsigned char> rgb(pixelCount * 3u);
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            rgb[pixel * 3u] = static_cast<unsigned char>(rgba8[pixel * 4u]);
            rgb[pixel * 3u + 1u] = static_cast<unsigned char>(rgba8[pixel * 4u + 1u]);
            rgb[pixel * 3u + 2u] = static_cast<unsigned char>(rgba8[pixel * 4u + 2u]);
        }
        WriteJpeg(request.fileName.c_str(), request.rect.width, request.rect.height, rgb.data(),
                  100);
    }
    catch (...)
    {
        return false;
    }
    submittedScreenshot_.reset();
    submittedScreenshotFrameSequence_ = 0;
    return true;
}

/**
 * @brief Handles screenshot capture toggle and execution.
 */
void SessionUiUnit::HandleScreenshotCapture()
{
    if (PressKey(VK_SNAPSHOT))
    {
        GrabEnable = !GrabEnable;
    }

    if (!GrabEnable)
    {
        return;
    }

    const bool addTimeStampToCapture = !IsKeyDown(VK_SHIFT);
    wchar_t screenshotText[256];

    GenerateScreenshotFilename(GrabFileName, screenshotText);

    if (addTimeStampToCapture)
    {
        g_pSystemLogBox->AddText(screenshotText, SEASON3B::TYPE_SYSTEM_MESSAGE);
    }

    CaptureScreenshot();

    if (!addTimeStampToCapture)
    {
        g_pSystemLogBox->AddText(screenshotText, SEASON3B::TYPE_SYSTEM_MESSAGE);
    }

    GrabEnable = false;
}

void SessionLegacyCalls::GenerateScreenshotFilename(wchar_t *outFileName, wchar_t *outMessage)
{
    sessionKeeper_.Ui()->GenerateScreenshotFilename(outFileName, outMessage);
}
void SessionLegacyCalls::CaptureScreenshot()
{
    sessionKeeper_.Ui()->CaptureScreenshot();
}
void SessionLegacyCalls::HandleScreenshotCapture()
{
    sessionKeeper_.Ui()->HandleScreenshotCapture();
}

/**
 * @brief Updates the active scene based on current scene flag.
 */

bool SessionUiUnit::UpdateLegacyUiState()
{
    if (!SceneTransitionDetail::IsActivePresentationScene(SceneFlag))
    {
        return true;
    }

    if (SceneFlag == LOG_IN_SCENE || SceneFlag == CHARACTER_SCENE)
    {
        UpdateLoginAndCharacterScenes();
    }

    return true;
}

/**
 * @brief Updates scene state, handles input, and manages screenshot capture.
 */
bool SessionUiUnit::BeginScenePresentationUpdate()
{
    g_dwMouseUseUIID = 0;
    reconnectDialog_.Update(sessionKeeper_.Network()->Reconnect());

    return SceneFlag != MAIN_SCENE || BeginMainScenePresentationUpdate();
}

bool SessionUiUnit::FinishScenePresentationUpdate()
{
    notices_.Move(FPS_ANIMATION_FACTOR);
    if (SceneFlag == MAIN_SCENE)
        g_pUIMapName.Update();
    HandleScreenshotCapture();
    return UpdateLegacyUiState();
}

void SessionUiUnit::UpdateLoginAndCharacterScenes()
{
    LegacyUiManager().Update(sessionKeeper_.FrameElapsedMilliseconds());
}

void SessionLegacyCalls::UpdateLoginAndCharacterScenes()
{
    return sessionKeeper_.Ui()->UpdateLoginAndCharacterScenes();
} // OMF-01827
bool SessionLegacyCalls::IsCursorOnLegacyUi()
{
    return sessionKeeper_.Ui()->LegacyUiManager().IsCursorOnUI();
}

void SessionUiUnit::RequestCreateCharacter(CCharMakeWin &window)
{
    if (!window.content_.enabled[window.m_nSelJob])
        return;
    std::wcsncpy(InputText[0], window.panel_.Name().c_str(), MAX_USERNAME_SIZE);
    InputText[0][MAX_USERNAME_SIZE] = L'\0';

    CUIMng &rUIMng = LegacyUiManager();

    const std::wstring characterName = InputText[0];

    // todo: check with regex from server
    if (characterName.length() < CharacterCreationDetail::kMinCharacterNameLength)
        rUIMng.PopUpMsgWin(MESSAGE_MIN_LENGTH);
    else if (CheckName())
        rUIMng.PopUpMsgWin(MESSAGE_ID_SPACE_ERROR);
    else if (CheckSpecialText(InputText[0]))
        rUIMng.PopUpMsgWin(MESSAGE_SPECIAL_NAME);
    else
    {
        const auto classByte =
            static_cast<CharacterClassNumber>((CharacterView.Class << 2) + CharacterView.Skin);
        CurrentProtocolState = REQUEST_CREATE_CHARACTER;
        SocketClient->ToGameServer()->SendCreateCharacter(InputText[0], classByte);
        //SendRequestCreateCharacter(InputText[0], CharacterView.Class, CharacterView.Skin);
        rUIMng.HideWin(&window);
        rUIMng.PopUpMsgWin(MESSAGE_WAIT);
    }
}

bool SessionLegacyCalls::HasAccountBlockedCharacter()
{
    return sessionKeeper_.Ui()->HasAccountBlockedCharacter();
}
bool SessionLegacyCalls::HasEmptyCharacterSlot()
{
    return sessionKeeper_.Ui()->HasEmptyCharacterSlot();
}
bool SessionLegacyCalls::HasLiveCharacter()
{
    return sessionKeeper_.Ui()->HasLiveCharacter();
}
CHARACTER *SessionLegacyCalls::GetSelectedCharacter()
{
    return sessionKeeper_.Ui()->GetSelectedCharacter();
}
void SessionLegacyCalls::RenderAccountBlockMessage()
{
    sessionKeeper_.Ui()->RenderAccountBlockMessage();
}

/**
 * @brief Advances the title scene and transitions to login.
 */

bool SessionUiUnit::AdvanceTitleSceneOnOwner()
{
    auto &renderer = *sessionKeeper_.Renderer();
    if (!titleSceneStarted_)
    {
        if (!renderer.PrepareWebzenSceneOnOwner())
            return false;
        titleSceneStarted_ = true;
        return true;
    }

    if (!renderer.HasPresentedFinalTitleFrame())
    {
        return renderer.AdvanceWebzenLoadingOnOwner();
    }

    renderer.ReleaseTitleSceneOnOwner();
    titleSceneStarted_ = false;
    SceneFlag = LOG_IN_SCENE;
    return true;
}

bool SessionLegacyCalls::IsCanUseItem()
{
    return sessionKeeper_.Ui()->IsCanUseItem();
}

bool SessionLegacyCalls::IsCanTrade()
{
    return sessionKeeper_.Ui()->IsCanTrade();
}

void SessionLegacyCalls::RequireClass(ITEM_ATTRIBUTE *item)
{
    sessionKeeper_.Ui()->RequireClass(item);
}

void SessionLegacyCalls::ConvertTaxGold(DWORD gold, wchar_t *text)
{
    sessionKeeper_.Ui()->ConvertTaxGold(gold, text);
}

int64_t SessionLegacyCalls::ConvertRepairGold(int64_t gold, int durability, int maxDurability,
                                              short type, wchar_t *text)
{
    return sessionKeeper_.Ui()->ConvertRepairGold(gold, durability, maxDurability, type, text);
}

void SessionLegacyCalls::RepairAllGold()
{
    sessionKeeper_.Ui()->RepairAllGold();
}

void SessionLegacyCalls::ClearInventory()
{
    sessionKeeper_.Ui()->ClearInventory();
}

void SessionLegacyCalls::SetItemColor(int index, ITEM *inventory, int color)
{
    sessionKeeper_.Ui()->SetItemColor(index, inventory, color);
}

void SessionLegacyCalls::ConvertChaosTaxGold(DWORD gold, wchar_t *text)
{
    sessionKeeper_.Ui()->ConvertChaosTaxGold(gold, text);
}

void SessionLegacyCalls::GateOpen(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Network()->GateOpen(character, object);
}
void SessionLegacyCalls::GateClose(CHARACTER *character, OBJECT *object)
{
    sessionKeeper_.Network()->GateClose(character, object);
}
void SessionLegacyCalls::DoInterfaceOpen(int key)
{
    sessionKeeper_.Network()->DoInterfaceOpen(key);
}
void SessionLegacyCalls::ProcessState(int key, BYTE gateOnOff, BYTE state)
{
    sessionKeeper_.Network()->ProcessState(key, gateOnOff, state);
}
void SessionLegacyCalls::SendToggleGate()
{
    sessionKeeper_.Network()->SendToggleGate();
}
bool SessionLegacyCalls::IsGateOpened()
{
    return sessionKeeper_.Network()->IsGateOpened();
}

bool SessionLegacyCalls::RenderPetCmdInfo(int sx, int sy, int type)
{
    return sessionKeeper_.Ui()->RenderPetCmdInfo(sx, sy, type);
}

bool SessionLegacyCalls::RenderPetItemInfo(int sx, int sy, ITEM *item, int inventoryType)
{
    return sessionKeeper_.Ui()->RenderPetItemInfo(sx, sy, item, inventoryType);
}
bool SessionLegacyCalls::RequestPetInfo(int sx, int sy, ITEM *pItem)
{
    return sessionKeeper_.Ui()->RequestPetInfo(sx, sy, pItem);
} // OMF-00834

void SessionLegacyCalls::MoveTournamentInterface()
{
    sessionKeeper_.Gameplay()->MoveTournamentInterface();
}
void SessionLegacyCalls::MoveBattleSoccerEffect(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->MoveBattleSoccerEffect(character);
}

void SessionUiUnit::CloseNPCGMWindow()
{
    if (!g_pUIManager->IsOpen(::INTERFACE_NPCGUILDMASTER))
        return;
    g_pUIManager->Close(::INTERFACE_NPCGUILDMASTER);
}

void SessionLegacyCalls::MoveInterface()
{
    sessionKeeper_.Gameplay()->MoveInterface();
}

void SessionLegacyCalls::SaveMacro(const wchar_t *fileName)
{
    sessionKeeper_.Ui()->SaveMacro(fileName);
}

void SessionLegacyCalls::OpenMacro(const wchar_t *fileName)
{
    sessionKeeper_.Ui()->OpenMacro(fileName);
}

void SessionLegacyCalls::SaveOptions()
{
    sessionKeeper_.Ui()->SaveOptions();
}

void SessionUiUnit::SaveMacro(const wchar_t *fileName)
{
    FILE *file = _wfopen(fileName, L"wt");
    if (file == nullptr)
        return;
    for (const auto &text : sessionKeeper_.MacroTexts())
        fwprintf(file, L"%ls\n", text);
    fclose(file);
}

void SessionUiUnit::OpenMacro(const wchar_t *fileName)
{
    FILE *file = _wfopen(fileName, L"rt");
    if (file == nullptr)
        return;
    for (auto &text : sessionKeeper_.MacroTexts())
    {
        if (fgetws(text, static_cast<int>(std::size(text)), file) == nullptr)
            break;
        const std::size_t lineEnd = wcscspn(text, L"\r\n");
        if (text[lineEnd] == L'\0' && !feof(file))
        {
            wint_t next;
            while ((next = fgetwc(file)) != WEOF && next != L'\n')
            {
            }
        }
        text[lineEnd] = L'\0';
    }
    fclose(file);
}

void SessionUiUnit::SaveOptions()
{
    // 0 ~ 19 skill hotkey
    BYTE options[30]{};

    int iSkillType = -1;
    for (int i = 0; i < 10; ++i)
    {
        iSkillType = g_pMainFrame->GetSkillHotKey(i);

        int iIndex = i * 2;
        if (iSkillType != -1)
        {
            options[iIndex] = HIBYTE(CharacterAttribute->Skill[iSkillType]);
            options[iIndex + 1] = LOBYTE(CharacterAttribute->Skill[iSkillType]);
        }
        else
        {
            options[iIndex] = 0xff;
            options[iIndex + 1] = 0xff;
        }
    }

    if (g_pOption->IsAutoAttack())
    {
        options[20] |= AUTOATTACK_ON;
    }
    else
    {
        options[20] |= AUTOATTACK_OFF;
    }

    if (g_pOption->IsWhisperSound())
    {
        options[20] |= WHISPER_SOUND_ON;
    }
    else
    {
        options[20] |= WHISPER_SOUND_OFF;
    }

    if (g_pOption->IsSlideHelp() == false)
    {
        options[20] |= SLIDE_HELP_OFF;
    }

    options[21] =
        static_cast<BYTE>((g_pMainFrame->GetItemHotKey(SEASON3B::HOTKEY_Q) - ITEM_POTION) & 0xFF);
    options[22] =
        static_cast<BYTE>((g_pMainFrame->GetItemHotKey(SEASON3B::HOTKEY_W) - ITEM_POTION) & 0xFF);
    options[23] =
        static_cast<BYTE>((g_pMainFrame->GetItemHotKey(SEASON3B::HOTKEY_E) - ITEM_POTION) & 0xFF);

    BYTE wChatListBoxSize =
        g_pChatListBox->GetNumberOfLines(g_pChatListBox->GetCurrentMsgType()) / 3;
    if (g_bUseChatListBox == FALSE)
        wChatListBoxSize = 0;
    BYTE wChatListBoxBackAlpha = g_pChatListBox->GetBackAlpha() * 10;
    options[24] = (((wChatListBoxSize << 4) & 0xF0) | (wChatListBoxBackAlpha & 0x0F)) & 0xFF;
    options[25] =
        static_cast<BYTE>((g_pMainFrame->GetItemHotKey(SEASON3B::HOTKEY_R) - ITEM_POTION) & 0xFF);

    options[26] = g_pMainFrame->GetItemHotKeyLevel(SEASON3B::HOTKEY_Q);
    options[27] = g_pMainFrame->GetItemHotKeyLevel(SEASON3B::HOTKEY_W);
    options[28] = g_pMainFrame->GetItemHotKeyLevel(SEASON3B::HOTKEY_E);
    options[29] = g_pMainFrame->GetItemHotKeyLevel(SEASON3B::HOTKEY_R);

    SocketClient->ToGameServer()->SendSaveKeyConfiguration(options, sizeof options);
}
