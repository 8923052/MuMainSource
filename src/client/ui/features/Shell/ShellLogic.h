#pragma once
#include "app/ApplicationNetwork.h"
#include "domain/CharacterPresentation.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Shell/ShellRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <deque>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#define SSW_SERVER_G_MAX 21
#define SSW_SERVER_MAX 16
#define SSW_DESC_LINE_MAX 2
#define SSW_DESC_ROW_MAX 83
#define SSW_LEFT_SERVER_G_MAX 10
#define SSW_RIGHT_SERVER_G_MAX 10
#define CSMW_SPR_DECO 0
#define CSMW_SPR_INFO 1
#define CSMW_SPR_MAX 2
#define CSMW_BTN_CREATE 0
#define CSMW_BTN_MENU 1
#define CSMW_BTN_CONNECT 2
#define CSMW_BTN_DELETE 3
#define CSMW_BTN_MAX 4

#define CRW_SPR_PIC_L 0
#define CRW_SPR_PIC_R 1
#define CRW_SPR_DECO 2
#define CRW_SPR_LOGO 3
#define CRW_SPR_TXT_HIDE0 4
#define CRW_SPR_TXT_HIDE1 5
#define CRW_SPR_TXT_HIDE2 6
#define CRW_SPR_MAX 7
#define CRW_ILLUST_MAX 8

#define CRW_NAME_MAX 32
#define CRW_ITEM_MAX 400

#define CRW_INDEX_DEPARTMENT 0
#define CRW_INDEX_TEAM 1
#define CRW_INDEX_NAME 2
#define CRW_INDEX_NAME0 2
#define CRW_INDEX_NAME1 3
#define CRW_INDEX_NAME2 4
#define CRW_INDEX_NAME3 5
#define CRW_INDEX_MAX 6

class CCreditWin : public CWin
{
    friend class CharacterRetirementTestPeer;
    enum SHOW_STATE
    {
        HIDE,
        FADEIN,
        SHOW,
        FADEOUT
    };

    using DurationMs = std::chrono::duration<double, std::milli>;

    struct SCreditItem
    {
        std::uint8_t byClass;
        char szName[CRW_NAME_MAX];
    };

  protected:
    SessionBoundArray<CSprite, CRW_SPR_MAX> m_aSpr;
    CButton m_btnClose;

    SHOW_STATE m_eIllustState;
    DurationMs m_illustElapsed;
    std::uint8_t m_byIllust;
    std::array<std::array<const wchar_t *, 2>, CRW_ILLUST_MAX> m_illustPaths;
    SCreditItem m_aCredit[CRW_ITEM_MAX]{};
    bool textReady_ = false;
    std::array<bool, 2> ownedIllustrations_{};
    int m_nNowIndex;
    int m_nNameCount;
    int m_anTextIndex[CRW_INDEX_MAX];
    SHOW_STATE m_aeTextState[CRW_INDEX_NAME + 1];
    DurationMs m_textElapsed;

  public:
    explicit CCreditWin(SessionKeeper &keeper);
    virtual ~CCreditWin();

    void Create();
    bool PrepareText();
    void ReleaseIllustrations() noexcept;
    void SetPosition();
    void Show(bool bShow);
    bool CursorInWin(int nArea);

  protected:
    void PreRelease();
    void UpdateWhileActive(double dDeltaTick);
    void RenderControls();

    void CloseWin();
    void Init();
    void LoadIllust();
    void AnimationIllust(DurationMs deltaTime);
    bool SetTextIndex();
    bool AdvanceTextPhase();
    void AnimationText(DurationMs deltaTime);
};

class CLoginMainWin : public CWin
{
  protected:
    UI::Modern::PC::Login::RmlLoginSceneButtons m_sceneButtons;

  public:
    explicit CLoginMainWin(SessionKeeper &keeper);
    virtual ~CLoginMainWin();

    void Create();
    void SetPosition(int nXCoord, int nYCoord);
    void Show(bool bShow);
    bool CursorInWin(int nArea);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight);

  protected:
    void PreRelease();
    void UpdateWhileShow(double dDeltaTick);
    void RenderControls();
};

class SessionConfigStore;
class SessionNetworkUnit;
struct SessionInputEvent;
struct SessionConfigValues;
class CLoginWin : public CWin
{
  protected:
    UI::Modern::PC::Login::RmlLoginPanel m_panel;

    // Snapshot of the field contents, used to detect that the player edited the
    // username or password so the stored credentials can be dropped.
    wchar_t m_prevUsername[MAX_USERNAME_SIZE + 1] = {};
    wchar_t m_prevPassword[MAX_PASSWORD_SIZE + 1] = {};
    bool enterKeyHeld_ = false;
    bool escapeKeyHeld_ = false;

  public:
    explicit CLoginWin(SessionKeeper &keeper);
    virtual ~CLoginWin();
    void BindConfiguration(SessionSlotId slotId, SessionConfigValues &values,
                           SessionConfigStore &store) noexcept;
    void UnbindConfiguration() noexcept;
    void Create();
    void SetPosition(int nXCoord, int nYCoord);
    void Show(bool bShow);
    bool CursorInWin(int nArea);

    void ConnectConnectionServer();

    void FocusAccountInput();
    void FocusPasswordInput();
    bool ProcessModernUiInput(const SessionInputEvent &event);
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const;
    bool TryAutoLogin();
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight);

  protected:
    void PreRelease();
    void UpdateWhileActive(double dDeltaTick) override;
    void UpdateWhileShow(double dDeltaTick);
    void RenderControls();
    void RequestLogin();
    void CancelLogin();

    void RevokeSavedCredentialsIfEdited();
    bool UpdateConfiguration();

    SessionConfigValues *m_sessionConfig = nullptr;
    SessionConfigStore *m_sessionConfigStore = nullptr;
    SessionNetworkUnit &m_sessionNetwork;
};

// In-game OK/Cancel confirmation shown before the login password is stored.
// Kept separate from the login window so the message-box layout class lives in
// its own translation unit.

class SessionKeeper;

namespace UI::Login
{
// The player's answer to the "Remember Password" confirmation dialog.
enum class RememberPasswordChoice : int
{
    None,    // nothing to apply
    Pending, // dialog is open, awaiting an answer
    Ok,      // player confirmed
    Cancel,  // player declined
};

// Opens the confirmation dialog (which warns about storing the password on a
// shared machine) and marks the choice Pending.
void OpenRememberPasswordPrompt(SessionKeeper &keeper);

} // namespace UI::Login

class CServerGroup;
class CServerInfo;

class CServerSelWin : public CWin
{
  private:
    enum SERVER_SELECT_WIN
    {
        SERVER_GROUP_BTN_WIDTH = 108,
        SERVER_GROUP_BTN_HEIGHT = 26,
        SERVER_BTN_WIDTH = 193,
        SERVER_BTN_HEIGHT = 26,
    };

  protected:
    SessionBoundArray<CButton, SSW_SERVER_G_MAX> m_aServerGroupBtn;
    SessionBoundArray<CButton, SSW_SERVER_MAX> m_aServerBtn;
    SessionBoundArray<CGaugeBar, SSW_SERVER_MAX> m_aServerGauge;
    SessionBoundArray<CSprite, 2> m_aBtnDeco;
    SessionBoundArray<CSprite, 2> m_aArrowDeco;
    CWinEx m_winDescription;

    int m_icntServerGroup;
    int m_icntLeftServerGroup;
    int m_icntRightServerGroup;
    int m_icntServer;
    bool m_bTestServerBtn;

    int m_iSelectServerBtnIndex;
    CServerGroup *m_pSelectServerGroup;

    wchar_t m_szDescription[SSW_DESC_LINE_MAX][SSW_DESC_ROW_MAX];

  public:
    explicit CServerSelWin(SessionKeeper &keeper);
    virtual ~CServerSelWin();
    void Create();
    void SetPosition(int nXCoord, int nYCoord);
    void UpdateDisplay();
    void Show(bool bShow);
    bool CursorInWin(int nArea);
    bool TryAutoConnect();
    bool AcceptConnectionPort(unsigned short port);
    bool IsAutoConnectPending() const noexcept
    {
        return autoConnectPending_;
    }

  protected:
    void PreRelease();
    void SetServerBtnPosition();
    void SetArrowSpritePosition();
    void ShowServerGBtns();
    void ShowDecoSprite();
    void ShowArrowSprite();
    void ShowServerBtns();
    void UpdateWhileActive(double dDeltaTick);
    void RenderControls();

  private:
    bool autoConnectAttempted_ = false;
    bool autoConnectPending_ = false;
    std::deque<std::pair<std::wstring, CServerInfo>> autoConnectTargets_;
    bool RequestNextAutoConnect();
    bool RequestConnection(wchar_t *groupName, CServerInfo &server);
};

class SessionUiUnit;

class CCharMakeWin : public CWin
{
    friend class SessionUiUnit;

  protected:
    SessionUiUnit &sessionUi;
    UI::Modern::PC::Character::RmlCharacterCreatePanel panel_;
    UI::Modern::PC::Character::RmlCharacterCreatePanel::Content content_;
    CLASS_TYPE m_nSelJob = CLASS_KNIGHT;
    std::string locale_;
    double effectElapsedMilliseconds_ = 0;
    WorldCharacterVisualState m_createVisual;
    vec3_t m_createBasePosition{};

  public:
    explicit CCharMakeWin(SessionKeeper &keeper);
    virtual ~CCharMakeWin();

    void Create();
    void SetPosition(int nXCoord, int nYCoord);
    void Show(bool bShow);
    bool CursorInWin(int nArea);
    void UpdateDisplay();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const;

  protected:
    void PreRelease();
    void UpdateWhileActive(double dDeltaTick) override;
    void RenderControls();

    void RequestCreateCharacter();

    void SelectCreateCharacter();
    void UpdateCreateCharacter(double elapsedMilliseconds);
    void PrepareCreateCharacterAppearance();
    void ReleaseCreateCharacterVisual();
    void RenderCreateCharacter();
};

class CCharSelMainWin : public CWin
{
  protected:
    SessionBoundArray<CSprite, CSMW_SPR_MAX> m_asprBack;
    SessionBoundArray<CButton, CSMW_BTN_MAX> m_aBtn;
    bool m_bAccountBlockItem;

  public:
    explicit CCharSelMainWin(SessionKeeper &keeper);
    virtual ~CCharSelMainWin();

    void Create();
    void SetPosition(int nXCoord, int nYCoord);
    void Show(bool bShow);
    bool CursorInWin(int nArea);
    void UpdateDisplay();

  protected:
    void PreRelease();
    void UpdateWhileActive(double dDeltaTick);
    void RenderControls();
    void DeleteCharacter();
};

namespace CharacterCreationDetail
{

#pragma pack(push)
#pragma pack()
inline constexpr std::array<int, MAX_CLASS> kClassButtonTextIds{20, 21, 22, 23, 24, 1687, 3150};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr std::size_t kMinCharacterNameLength = 4;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kSummonerDescriptionTextId = 1690;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kRageFighterDescriptionTextId = 3152;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kDefaultDescriptionBase = 1705;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kStatLabelBaseId = 1701;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr const wchar_t *kDarkLordLeadershipStatValue = L"25";
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kDarkLordLeadershipTextId = 1738;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
struct ClassStats
{
    std::array<const wchar_t *, 4> values;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr std::array<ClassStats, MAX_CLASS> kClassStatTable{{
    ClassStats{{L"18", L"18", L"15", L"30"}}, // Knight
    ClassStats{{L"28", L"20", L"25", L"10"}}, // Wizard
    ClassStats{{L"22", L"25", L"20", L"15"}}, // Elf
    ClassStats{{L"26", L"26", L"26", L"26"}}, // Magic Gladiator
    ClassStats{{L"26", L"20", L"20", L"15"}}, // Dark Lord
    ClassStats{{L"21", L"21", L"18", L"23"}}, // Summoner
    ClassStats{{L"32", L"27", L"25", L"20"}}, // Rage Fighter
}};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
constexpr int ResolveDescriptionTextId(CLASS_TYPE selectedClass)
{
    if (selectedClass == CLASS_SUMMONER)
        return kSummonerDescriptionTextId;
    if (selectedClass == CLASS_RAGEFIGHTER)
        return kRageFighterDescriptionTextId;
    return kDefaultDescriptionBase + selectedClass;
}
#pragma pack(pop)

} // namespace CharacterCreationDetail

namespace CharacterSelectionDetail
{

#pragma pack(push)
#pragma pack()
inline constexpr int kCharacterSlotCount = 5;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kButtonSpacing = 1;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kInfoSpacing = 2;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kInfoOffsetY = 5;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kStatPanelBaseXOffset = 346;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kStatPanelOffsetY = 24;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kJobButtonsStartY = 131;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kRageFighterButtonsY = 246;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kSummonerRow = 3;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kActionButtonsRowOffsetY = 325;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kCancelButtonOffsetX = 400;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kInputSpriteOffsetY = 317;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kInputTextOffsetX = 78;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kInputTextOffsetY = 21;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kDescriptionSpriteOffsetY = 355;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kDecorOffsetX = 22;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kDecorOffsetY = 59;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kAccountBlockMsgX = 320;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kAccountBlockPrimaryY = 330;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kAccountBlockSecondaryY = 348;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kWindowAlpha = 143;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kInfoSpriteHeight = 21;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
template <typename Characters, typename Predicate>
bool AnyCharacter(Characters &characters, Predicate &&predicate)
{
    for (int index = 0; index < kCharacterSlotCount; ++index)
    {
        if (predicate(characters[index]))
        {
            return true;
        }
    }
    return false;
}
#pragma pack(pop)

} // namespace CharacterSelectionDetail

namespace SplashSceneDetail
{

#pragma pack(push)
#pragma pack()
inline constexpr int BACKGROUND_SELECTION_PERCENTAGE = 100;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int CLASSIC_BACKGROUND_PROBABILITY = 70;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int TITLE_BITMAP_BASE = BITMAP_TITLE;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int TITLE_BITMAP_BACK_02 = BITMAP_TITLE + 1;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int TITLE_BITMAP_LOGO = BITMAP_TITLE + 3;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int TITLE_BITMAP_PATTERN = BITMAP_TITLE + 5;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int TITLE_BITMAP_DYNAMIC_START = BITMAP_TITLE + 6;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int TITLE_BITMAP_DYNAMIC_END = BITMAP_TITLE + 14;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
enum class BackgroundTheme
{
    Classic,
    Season5
};
#pragma pack(pop)

} // namespace SplashSceneDetail

namespace ReconnectDetail
{

#pragma pack(push)
#pragma pack()
using MsgBox = SEASON3B::CNewUIMessageBoxMng;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float MSGBOX_WIDTH = 230.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float TOP_H = 67.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float BOTTOM_H = 50.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float BACK_BLANK_W = 8.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float BACK_BLANK_H = 10.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float PANEL_W = MSGBOX_WIDTH;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float PANEL_H = 122.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float PANEL_X = (REFERENCE_WIDTH - PANEL_W) / 2.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float PANEL_Y = (REFERENCE_HEIGHT - PANEL_H) / 2.0f + 100.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float MIDDLE_FILL_H = PANEL_H - TOP_H - BOTTOM_H;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float TITLE_Y = PANEL_Y + 12.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float STEP_Y = PANEL_Y + 30.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float COUNTDOWN_Y = PANEL_Y + 48.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float PROG_W = 160.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float PROG_H = 18.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float PROG_X = PANEL_X + (PANEL_W - PROG_W) / 2.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float PROG_Y = PANEL_Y + 66.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float PROG_BAR_MAX_W = 150.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float PROG_BAR_H = 8.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float PROG_BAR_INSET = 5.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float PROG_BAR_X = PROG_X + PROG_BAR_INSET;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float PROG_BAR_Y = PROG_Y + PROG_BAR_INSET;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float CANCEL_W = 54.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float CANCEL_H = 30.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float CANCEL_X = PANEL_X + (PANEL_W - CANCEL_W) / 2.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float CANCEL_Y = PANEL_Y + PANEL_H - CANCEL_H - 6.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float BTN_SHEET_W = 64.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float BTN_SHEET_H = 128.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr BYTE TEXT_R = 230;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr BYTE TEXT_G = 220;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr BYTE TEXT_B = 200;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr BYTE TEXT_A = 255;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float DIM_ALPHA = 0.45f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float OPAQUE_ALPHA = 1.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float FB_BORDER_ALPHA = 0.5f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float FB_PANEL_ALPHA = 0.92f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float FB_BORDER = 2.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float FB_BAR_BG_ALPHA = 0.7f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float FB_BAR_FILL_ALPHA = 0.9f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float FB_CANCEL_ALPHA = 0.25f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float FB_CANCEL_HOVER_ALPHA = 0.45f;
#pragma pack(pop)

} // namespace ReconnectDetail

namespace OptionPanelDetail
{

#pragma pack(push)
#pragma pack()
inline constexpr int MaxVolume = 10;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
struct Resolution final
{
    int width;
    int height;
    const wchar_t *label;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr std::array Resolutions{
    Resolution{640, 480, L"640 x 480"},     Resolution{800, 600, L"800 x 600"},
    Resolution{1024, 768, L"1024 x 768"},   Resolution{1280, 720, L"1280 x 720"},
    Resolution{1280, 1024, L"1280 x 1024"}, Resolution{1600, 900, L"1600 x 900"},
    Resolution{1600, 1200, L"1600 x 1200"}, Resolution{1680, 1050, L"1680 x 1050"},
    Resolution{1920, 1080, L"1920 x 1080"}, Resolution{2560, 1440, L"2560 x 1440"},
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int DefaultResolution = 8;
#pragma pack(pop)

} // namespace OptionPanelDetail

namespace CreditsDetail
{

#pragma pack(push)
#pragma pack()
using DurationMs = std::chrono::duration<double, std::milli>;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr DurationMs kIllustFadeDuration{2000.0};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr DurationMs kIllustShowDuration{22000.0};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr DurationMs kTextFadeDuration{1000.0};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr DurationMs kNameShowDuration{2300.0};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr std::string_view kCreditDataPath = "Data\\Local\\credit.bmd";
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr std::array<std::array<const wchar_t *, 2>, CRW_ILLUST_MAX> kIllustPaths = {{
    {L"Interface\\im1_1.jpg", L"Interface\\im1_2.jpg"},
    {L"Interface\\im2_1.jpg", L"Interface\\im2_2.jpg"},
    {L"Interface\\im3_1.jpg", L"Interface\\im3_2.jpg"},
    {L"Interface\\im4_1.jpg", L"Interface\\im4_2.jpg"},
    {L"Interface\\im5_1.jpg", L"Interface\\im5_2.jpg"},
    {L"Interface\\im6_1.jpg", L"Interface\\im6_2.jpg"},
    {L"Interface\\im7_1.jpg", L"Interface\\im7_2.jpg"},
    {L"Interface\\im8_1.jpg", L"Interface\\im8_2.jpg"},
}};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
template <std::size_t N> void CopyNameToWide(const char *source, wchar_t (&destination)[N])
{
    if (source == nullptr)
    {
        destination[0] = L'\0';
        return;
    }

    std::mbstowcs(destination, source, N);
    destination[N - 1] = L'\0';
}
#pragma pack(pop)

} // namespace CreditsDetail

namespace ReconnectDetail
{

const wchar_t *StepLabel(ReconnectManager::Phase phase);
}

namespace RememberPasswordDetail
{
class CRememberPasswordMsgBoxLayout
    : public SEASON3B::TMsgBoxLayout<SEASON3B::CNewUICommonMessageBox>
{
  public:
    explicit CRememberPasswordMsgBoxLayout(SessionKeeper &keeper);
    bool SetLayout() override;
    SEASON3B::CALLBACK_RESULT OnOk(SEASON3B::CNewUIMessageBoxBase *pOwner,
                                   const leaf::xstreambuf &xParam);
    SEASON3B::CALLBACK_RESULT OnCancel(SEASON3B::CNewUIMessageBoxBase *pOwner,
                                       const leaf::xstreambuf &xParam);

  private:
    UI::Login::RememberPasswordChoice &g_Choice;
};
} // namespace RememberPasswordDetail
