#pragma once
#include "domain/ChatSocial.h"
#include "session/SessionNetwork.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include "ui/features/Social/SocialRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

class Connection;
class SessionNetworkUnit;
class SessionUiUnit;

// Native window controllers grouped by feature.
#pragma pack(push)
#pragma pack()
const DWORD g_cdwLetterCost = 10000;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
class CUIChatWindow : public CUIBaseWindow
{
    Connection *_connection;
    SessionNetworkUnit &sessionNetwork_;
    SessionUiUnit &sessionUi_;

  public:
    explicit CUIChatWindow(SessionKeeper &keeper);
    virtual ~CUIChatWindow();
    SessionKeeper &OriginatingSession() const noexcept
    {
        return SessionOrigin();
    }

    virtual void Init(const wchar_t *pszTitle, DWORD dwParentID = 0);
    virtual void Refresh();
    void FocusReset()
    {
        m_TextInputBox.GiveFocus();
    }
    int AddChatPal(const wchar_t *pszID, BYTE Number, BYTE Server);
    void RemoveChatPal(const wchar_t *pszID);
    void AddChatText(BYTE byIndex, const wchar_t *pszText, int iType, int iColor);
    void ConnectToChatServer(const wchar_t *pszIP, DWORD dwRoomNumber, DWORD dwTicket);
    void DisconnectToChatServer();
    Connection *GetCurrentSocket()
    {
        return _connection;
    }
    GUILDLIST_TEXT *GetCurrentInvitePal()
    {
        return m_InvitePalListBox.GetSelectedText();
    }
    void UpdateInvitePalList();
    int GetShowType()
    {
        return m_iShowType;
    }
    const wchar_t *GetChatFriend(int *piResult = NULL);
    int GetUserCount()
    {
        return m_PalListBox.GetLineNum();
    }
    DWORD GetRoomNumber()
    {
        return m_dwRoomNumber;
    }
    void Lock(BOOL bFlag);
    bool PrepareModernUiOnWorker(int, int, bool) override;
    bool ProcessModernUiInput(const SessionInputEvent &) override;
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const override;
    bool RecordModernUi(LegacyRenderFacade &) const override;
    void ApplyModernUiChanges() override;

  protected:
    virtual void RenderSub();
    virtual BOOL HandleMessage();
    virtual void DoActionSub(BOOL bMessageOnly);
    virtual void DoMouseActionSub();
    void InitControls() override;

  protected:
    int m_iShowType;
    CUITextInputBox m_TextInputBox;
    CUISimpleChatListBox m_ChatListBox;
    CUIChatPalListBox m_PalListBox;
    CUIButton m_InviteButton;
    CUIChatPalListBox m_InvitePalListBox;
    CUIButton m_CloseInviteButton;
    DWORD m_dwRoomNumber;
    int m_iPrevWidth;
    wchar_t m_szLastText[MAX_CHATROOM_TEXT_LENGTH]{};
    UI::Modern::PC::Friend::RmlFriendPanel m_ModernPanel;
    std::size_t m_ModernInviteSelection = 0;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
class CUILetterReadWindow : public CUIBaseWindow
{
  public:
    explicit CUILetterReadWindow(SessionKeeper &keeper)
        : CUIBaseWindow(keeper), m_Photo(keeper), m_iShowType(2), m_LetterTextBox(keeper),
          m_ReplyButton(keeper), m_DeleteButton(keeper), m_CloseButton(keeper),
          m_PrevButton(keeper), m_NextButton(keeper),
          m_ModernPanel(keeper, UI::Modern::PC::Friend::RmlFriendPanelMode::ReadLetter)
    {
    }
    virtual ~CUILetterReadWindow();

    virtual void Init(const wchar_t *pszTitle, DWORD dwParentID = 0);
    virtual void InitControls()
    {
    }
    virtual void Refresh();
    void SetLetter(LETTERLIST_TEXT *pLetterHead, const wchar_t *pLetterText);
    bool PrepareModernUiOnWorker(int, int, bool) override;
    bool ProcessModernUiInput(const SessionInputEvent &) override;
    bool RecordModernUi(LegacyRenderFacade &) const override;
    void ApplyModernUiChanges() override;

  protected:
    virtual void RenderSub();
    virtual void RenderOver();
    virtual BOOL HandleMessage();
    virtual void DoActionSub(BOOL bMessageOnly);
    virtual void DoMouseActionSub();

  public:
    CUIPhotoViewer m_Photo;

  protected:
    int m_iShowType;
    LETTERLIST_TEXT m_LetterHead;

    CUILetterTextListBox m_LetterTextBox;
    CUIButton m_ReplyButton;
    CUIButton m_DeleteButton;
    CUIButton m_CloseButton;
    CUIButton m_PrevButton;
    CUIButton m_NextButton;
    UI::Modern::PC::Friend::RmlFriendPanel m_ModernPanel;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
class CUILetterWriteWindow : public CUIBaseWindow
{
  public:
    explicit CUILetterWriteWindow(SessionKeeper &keeper)
        : CUIBaseWindow(keeper), m_Photo(keeper), m_iShowType(0), m_bIsSend(FALSE),
          m_MailtoInputBox(keeper), m_TitleInputBox(keeper), m_TextInputBox(keeper),
          m_SendButton(keeper), m_CloseButton(keeper), m_PrevPoseButton(keeper),
          m_NextPoseButton(keeper),
          m_ModernPanel(keeper, UI::Modern::PC::Friend::RmlFriendPanelMode::WriteLetter)
    {
    }
    virtual ~CUILetterWriteWindow()
    {
    }

    virtual void Init(const wchar_t *pszTitle, DWORD dwParentID = 0);

    virtual void Refresh();
    void SetMailtoText(const wchar_t *pszText);
    void SetMainTitleText(const wchar_t *pszText);
    void SetMailContextText(const wchar_t *pszText);

    void SetSendState(BOOL bFlag)
    {
        m_bIsSend = bFlag;
    }
    bool PrepareModernUiOnWorker(int, int, bool) override;
    bool ProcessModernUiInput(const SessionInputEvent &) override;
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const override;
    bool RecordModernUi(LegacyRenderFacade &) const override;
    void ApplyModernUiChanges() override;

    virtual BOOL CloseCheck();

  protected:
    void InitControls() override;
    virtual void RenderSub();
    virtual void RenderOver();
    virtual BOOL HandleMessage();
    virtual void DoActionSub(BOOL bMessageOnly);
    virtual void DoMouseActionSub();

  protected:
    int m_iShowType;
    CUIPhotoViewer m_Photo;
    BOOL m_bIsSend;
    int m_iLastTabIndex;

    CUITextInputBox m_MailtoInputBox;
    CUITextInputBox m_TitleInputBox;
    CUITextInputBox m_TextInputBox;
    CUIButton m_SendButton;
    CUIButton m_CloseButton;
    CUIButton m_PrevPoseButton;
    CUIButton m_NextPoseButton;
    UI::Modern::PC::Friend::RmlFriendPanel m_ModernPanel;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
class CFriendList
{
  public:
    CFriendList() : m_iCurrentSortType(0)
    {
    }
    ~CFriendList()
    {
        ClearFriendList();
    }
    void AddFriend(const wchar_t *pszID, BYTE Number, BYTE Server);
    void RemoveFriend(const wchar_t *pszID);
    void ClearFriendList();
    int UpdateFriendList(std::deque<GUILDLIST_TEXT> &pDestData, const wchar_t *pszID);
    void UpdateFriendState(const wchar_t *pszID, BYTE Number, BYTE Server);
    void UpdateAllFriendState(BYTE Number, BYTE Server);
    void Sort(int iType = -1);
    int GetCurrentSortType()
    {
        return m_iCurrentSortType;
    }

  private:
    int m_iCurrentSortType;
    std::deque<GUILDLIST_TEXT> m_FriendList;
    std::deque<GUILDLIST_TEXT>::iterator m_FriendListIter;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
class CUIFriendListTabWindow : public CUITabWindow
{
  public:
    explicit CUIFriendListTabWindow(SessionKeeper &keeper)
        : CUITabWindow(keeper), m_PalListBox(keeper), m_AddFriendButton(keeper),
          m_DelFriendButton(keeper), m_TalkButton(keeper), m_LetterButton(keeper)
    {
    }
    virtual ~CUIFriendListTabWindow()
    {
    }

    virtual void Init(const wchar_t *pszTitle, DWORD dwParentID = 0);
    virtual void Refresh();
    const wchar_t *GetCurrentSelectedFriend(BYTE *pNumber = NULL, BYTE *pServer = NULL);
    DWORD GetKeyMoveListUIID()
    {
        return m_PalListBox.GetUIID();
    }
    void RefreshPalList();
    const std::deque<GUILDLIST_TEXT> &ModernRows() const noexcept
    {
        return m_PalListBox.Items();
    }
    std::size_t ModernSelectedRow() const noexcept
    {
        return m_PalListBox.SLGetSelectLineNum() > 0
                   ? static_cast<std::size_t>(m_PalListBox.SLGetSelectLineNum() - 1)
                   : 0;
    }
    void SelectModernRow(std::size_t index)
    {
        m_PalListBox.SLSetSelectLine(static_cast<int>(index + 1));
    }
    void InvokeModernAction(int action)
    {
        SendUIMessageDirect(UI_MESSAGE_BTNLCLICK, action + 1, 0);
    }
    void SortModern(int column);

  protected:
    void AddReturnedFriend();
    virtual void RenderSub();
    virtual BOOL HandleMessage();
    virtual void DoActionSub(BOOL bMessageOnly);
    virtual void DoMouseActionSub();

  protected:
    CUIChatPalListBox m_PalListBox;
    CUIButton m_AddFriendButton;
    CUIButton m_DelFriendButton;
    CUIButton m_TalkButton;
    CUIButton m_LetterButton;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
class CUIChatRoomListTabWindow : public CUITabWindow
{
  public:
    explicit CUIChatRoomListTabWindow(SessionKeeper &keeper)
        : CUITabWindow(keeper), m_WindowListBox(keeper), m_HideAllButton(keeper)
    {
    }
    virtual ~CUIChatRoomListTabWindow()
    {
    }

    virtual void Init(const wchar_t *pszTitle, DWORD dwParentID = 0);
    virtual void Refresh();
    void AddWindow(DWORD dwUIID, const wchar_t *pszTitle);
    void RemoveWindow(DWORD dwUIID);
    DWORD GetCurrentSelectedWindow();
    DWORD GetKeyMoveListUIID()
    {
        return m_WindowListBox.GetUIID();
    }
    void Reset()
    {
        m_WindowListBox.Clear();
    }
    const std::deque<WINDOWLIST_TEXT> &ModernRows() const noexcept
    {
        return m_WindowListBox.Items();
    }
    std::size_t ModernSelectedRow() const noexcept
    {
        return m_WindowListBox.SLGetSelectLineNum() > 0
                   ? static_cast<std::size_t>(m_WindowListBox.SLGetSelectLineNum() - 1)
                   : 0;
    }
    void SelectModernRow(std::size_t index)
    {
        m_WindowListBox.SLSetSelectLine(static_cast<int>(index + 1));
    }
    void ToggleModernRow()
    {
        SendUIMessageDirect(UI_MESSAGE_LISTDBLCLICK, m_WindowListBox.GetUIID(), 0);
    }
    void HideAllModern()
    {
        SendUIMessageDirect(UI_MESSAGE_BTNLCLICK, 1, 0);
    }

  protected:
    virtual void RenderSub();
    virtual BOOL HandleMessage();
    virtual void DoActionSub(BOOL bMessageOnly);
    virtual void DoMouseActionSub();

  public:
    CUIWindowListBox m_WindowListBox;
    CUIButton m_HideAllButton;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
class CLetterList
{
  public:
    CLetterList() : m_iCurrentSortType(0)
    {
    }
    ~CLetterList()
    {
        ClearLetterList();
    }
    void AddLetter(DWORD dwLetterID, const wchar_t *pszID, const wchar_t *pszText,
                   const wchar_t *pszDate, const wchar_t *pszTime, BOOL bIsRead);
    void RemoveLetter(DWORD dwLetterID);
    void ClearLetterList();
    int UpdateLetterList(std::deque<LETTERLIST_TEXT> &pDestData, DWORD dwSelectLineNum);
    void Sort(int iType = -1);
    DWORD GetPrevLetterID(DWORD dwLetterID);
    DWORD GetNextLetterID(DWORD dwLetterID);
    LETTERLIST_TEXT *GetLetter(DWORD dwLetterID);
    void ResetLetterSelect(BOOL bFlag);
    BOOL CheckNoReadLetter();
    int GetCurrentSortType()
    {
        return m_iCurrentSortType;
    }

    void CacheLetterText(DWORD dwIndex, LPFS_LETTER_TEXT pLetterText);
    LPFS_LETTER_TEXT GetLetterText(DWORD dwIndex);
    void RemoveLetterTextCache(DWORD dwIndex);
    void ClearLetterTextCache();

    int GetLineNum(DWORD dwLetterID);
    int GetLetterCount()
    {
        return m_LetterList.size();
    }

  private:
    int m_iCurrentSortType;
    std::deque<LETTERLIST_TEXT> m_LetterList;
    std::deque<LETTERLIST_TEXT>::iterator m_LetterListIter;

    std::map<DWORD, FS_LETTER_TEXT, std::less<DWORD>> m_LetterCache;
    std::map<DWORD, FS_LETTER_TEXT, std::less<DWORD>>::iterator m_LetterCacheIter;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
class CUILetterBoxTabWindow : public CUITabWindow
{
  public:
    explicit CUILetterBoxTabWindow(SessionKeeper &keeper)
        : CUITabWindow(keeper), m_LetterListBox(keeper), m_WriteButton(keeper),
          m_ReadButton(keeper), m_ReplyButton(keeper), m_DeleteButton(keeper),
          m_bCheckAllState(FALSE)
    {
    }
    virtual ~CUILetterBoxTabWindow()
    {
    }

    virtual void Init(const wchar_t *pszTitle, DWORD dwParentID = 0);
    virtual void Refresh();
    LETTERLIST_TEXT *GetCurrentSelectedLetter();
    DWORD GetKeyMoveListUIID()
    {
        return m_LetterListBox.GetUIID();
    }
    void RefreshLetterList();
    void CheckAll(BOOL bCheck);
    void PrevNextCursorMove(int iMove);
    const std::deque<LETTERLIST_TEXT> &ModernRows() const noexcept
    {
        return m_LetterListBox.Items();
    }
    std::size_t ModernSelectedRow() const noexcept
    {
        return m_LetterListBox.SLGetSelectLineNum() > 0
                   ? static_cast<std::size_t>(m_LetterListBox.SLGetSelectLineNum() - 1)
                   : 0;
    }
    void SelectModernRow(std::size_t index)
    {
        m_LetterListBox.SLSetSelectLine(static_cast<int>(index + 1));
    }
    void ToggleModernRow(std::size_t index);
    void InvokeModernAction(int action)
    {
        SendUIMessageDirect(UI_MESSAGE_BTNLCLICK, action + 1, 0);
    }
    void SortModern(int column);
    bool ModernCheckAll() const noexcept
    {
        return m_bCheckAllState == TRUE;
    }

  protected:
    virtual void RenderSub();
    virtual BOOL HandleMessage();
    virtual void DoActionSub(BOOL bMessageOnly);
    virtual void DoMouseActionSub();

  protected:
    CUILetterListBox m_LetterListBox;
    CUIButton m_WriteButton;
    CUIButton m_ReadButton;
    CUIButton m_ReplyButton;
    CUIButton m_DeleteButton;
    BOOL m_bCheckAllState;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
class CUIFriendWindow : public CUIBaseWindow
{
  public:
    explicit CUIFriendWindow(SessionKeeper &keeper)
        : CUIBaseWindow(keeper), m_iTabIndex(0), m_iTabMouseOverIndex(0), m_FriendListWnd(keeper),
          m_ChatRoomListWnd(keeper), m_LetterBoxWnd(keeper),
          m_ModernPanel(keeper, UI::Modern::PC::Friend::RmlFriendPanelMode::Main)
    {
    }
    virtual ~CUIFriendWindow()
    {
    }

    virtual void Init(const wchar_t *pszTitle, DWORD dwParentID = 0);
    virtual void Refresh();
    void Reset();
    void Close();
    void RefreshPalList()
    {
        m_FriendListWnd.RefreshPalList();
    }

    void AddWindow(DWORD dwUIID, const wchar_t *pszTitle)
    {
        m_ChatRoomListWnd.AddWindow(dwUIID, pszTitle);
    }
    void RemoveWindow(DWORD dwUIID)
    {
        m_ChatRoomListWnd.RemoveWindow(dwUIID);
    }
    DWORD GetCurrentSelectedWindow()
    {
        return m_ChatRoomListWnd.GetCurrentSelectedWindow();
    }
    void ResetWindow()
    {
        m_ChatRoomListWnd.Reset();
    }

    void RefreshLetterList()
    {
        m_LetterBoxWnd.RefreshLetterList();
    }
    LETTERLIST_TEXT *GetCurrentSelectedLetter()
    {
        return m_LetterBoxWnd.GetCurrentSelectedLetter();
    }
    void PrevNextCursorMove(int iMove)
    {
        m_LetterBoxWnd.PrevNextCursorMove(iMove);
    }

    void SetTabIndex(int iIndex);
    int GetTabIndex()
    {
        return m_iTabIndex;
    }
    bool PrepareModernUiOnWorker(int, int, bool) override;
    bool ProcessModernUiInput(const SessionInputEvent &) override;
    bool RecordModernUi(LegacyRenderFacade &) const override;
    void ApplyModernUiChanges() override;

  protected:
    virtual void InitControls()
    {
    }
    virtual void RenderSub();
    virtual BOOL HandleMessage();
    virtual void DoActionSub(BOOL bMessageOnly);
    virtual void DoMouseActionSub();

  protected:
    int m_iTabIndex;
    int m_iTabMouseOverIndex;
    CUIFriendListTabWindow m_FriendListWnd;
    CUIChatRoomListTabWindow m_ChatRoomListWnd;
    CUILetterBoxTabWindow m_LetterBoxWnd;
    UI::Modern::PC::Friend::RmlFriendPanel m_ModernPanel;
    std::size_t m_ModernScroll = 0;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
class CUIFriendMenu : public CUIBaseWindow
{
  public:
    explicit CUIFriendMenu(SessionKeeper &keeper) : CUIBaseWindow(keeper)
    {
        Init();
    }
    virtual ~CUIFriendMenu()
    {
        Reset();
    }

    void Reset();
    void Init();
    void AddWindow(DWORD dwUIID, CUIBaseWindow *pWindow);
    void RemoveWindow(DWORD dwUIID);

    void ShowMenu(BOOL bHotKey = FALSE);
    void HideMenu();

    void SetNewChatAlert(DWORD dwAlertWindowID);
    void SetNewChatAlertOff(DWORD dwAlertWindowID);
    BOOL IsNewChatAlert();
    void SetNewMailAlert(BOOL bAlert);
    BOOL IsNewMailAlert()
    {
        return m_bNewMailAlert;
    }

    int GetBlinkTemp();
    void IncreaseBlinkTemp();
    int GetLetterBlink();
    void IncreaseLetterBlink();

    void RenderFriendButton();

    DWORD CheckChatRoomDuplication(const wchar_t *pszTargetName);
    void SendChatRoomConnectCheck();
    void UpdateAllChatWindowInviteList();

    BOOL IsHotkeyEnable()
    {
        return m_bHotKey;
    }

    void AddRequestWindow(const wchar_t *szTargetName);
    BOOL IsRequestWindow(const wchar_t *szTargetName);
    void RemoveRequestWindow(const wchar_t *szTargetName);
    void RemoveAllRequestWindow();

    void CloseAllChatWindow();
    void LockAllChatWindow();

  protected:
    virtual void InitControls()
    {
    }
    virtual void RenderSub();
    virtual BOOL HandleMessage();
    virtual void DoActionSub(BOOL bMessageOnly);
    virtual void DoMouseActionSub();

    void RenderWindowList();

  protected:
    std::deque<DWORD> m_WindowList;
    std::deque<DWORD>::iterator m_WindowListIter;
    std::deque<DWORD>::iterator m_WindowListSelectIter;
    float m_fLineHeight;
    int m_iFriendMenuPos_y;
    int m_iFriendMenuHeight;
    float m_fMenuAlpha;
    float m_fMenuAlphaAdd;
    std::deque<DWORD> m_NewChatWindowList;
    BOOL m_bNewMailAlert;
    int m_iBlinkTemp;
    int m_iLetterBlink;
    BOOL m_bHotKey;
    std::deque<wchar_t *> m_RequestChatWindowList;
    std::deque<wchar_t *>::iterator m_RequestChatWindowListIter;
};
#pragma pack(pop)

#pragma warning(disable : 4786)

class CUITextInputBox;
class SessionGameplayUnit;
class CMapManager;

namespace UI::Modern::PC::Chat
{
class BlockedChatList final
{
  public:
    explicit BlockedChatList(std::filesystem::path path, std::size_t maxNameLength = 10);

    bool Load();
    bool Add(std::wstring name);
    bool Remove(std::wstring_view name);
    bool Contains(std::wstring_view name) const noexcept;

    const std::vector<std::wstring> &Names() const noexcept
    {
        return names_;
    }

  private:
    bool Save() const;

    std::filesystem::path path_;
    std::size_t maxNameLength_ = 10;
    std::vector<std::wstring> names_;
};
} // namespace UI::Modern::PC::Chat

namespace SEASON3B
{
class CNewUIManager;
class CNewUIChatLogWindow;
class CNewUISystemLogWindow;

class CNewUIChatInputBox : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum INPUT_MESSAGE_TYPE
    {
        INPUT_NOTHING = -1,
        INPUT_CHAT_MESSAGE,
        INPUT_PARTY_MESSAGE,
        INPUT_GUILD_MESSAGE,
        INPUT_GENS_MESSAGE,
    };

    enum INPUT_TOOLTIP_TYPE
    {
        INPUT_TOOLTIP_NOTHING = -1,

        INPUT_TOOLTIP_NORMAL,
        INPUT_TOOLTIP_PARTY,
        INPUT_TOOLTIP_GUILD,
        INPUT_TOOLTIP_GENS,

        INPUT_TOOLTIP_WHISPER,
        INPUT_TOOLTIP_SYSTEM,
        INPUT_TOOLTIP_CHAT,

        INPUT_TOOLTIP_FRAME,
        INPUT_TOOLTIP_SIZE,
        INPUT_TOOLTIP_TRANSPARENCY,
    };

    enum IMAGE_LIST
    {
        IMAGE_INPUTBOX_BACK = BITMAP_INTERFACE_NEW_CHATINPUTBOX_BEGIN,
        IMAGE_INPUTBOX_NORMAL_ON,
        IMAGE_INPUTBOX_PARTY_ON,
        IMAGE_INPUTBOX_GUILD_ON,
        IMAGE_INPUTBOX_GENS_ON,
        IMAGE_INPUTBOX_WHISPER_ON,
        IMAGE_INPUTBOX_SYSTEM_ON,
        IMAGE_INPUTBOX_CHATLOG_ON,
        IMAGE_INPUTBOX_FRAME_ON,
        IMAGE_INPUTBOX_BTN_SIZE,
        IMAGE_INPUTBOX_BTN_TRANSPARENCY,
    };

  private:
    enum EVENT_STATE
    {
        EVENT_NONE = 0,
        EVENT_CLIENT_WND_HOVER,
    };

    typedef std::wstring type_string;
    typedef std::vector<type_string> type_vec_history;

    const uint64_t ChatCooldownMs = 1000; // 1 Second
    uint64_t m_lastChatTime = 0;

    CNewUIManager *m_pNewUIMng;
    SessionGameplayUnit &gameplay_;
    CNewUIChatLogWindow *m_pNewUIChatLogWnd;
    CNewUISystemLogWindow *m_pNewUISystemLogWnd;
    POINT m_WndPos{};

    CUITextInputBox *m_pChatInputBox, *m_pWhsprIDInputBox;
    type_vec_history m_vecChatHistory, m_vecWhsprIDHistory;

    int m_iCurChatHistory, m_iCurWhisperIDHistory;

    int m_iTooltipType;
    int m_iInputMsgType;
    bool m_bBlockWhisper;
    bool m_bShowSystemMessages;
    bool m_bShowChatLog;
    bool m_bWhisperSend;
    bool m_bShowMessageElseNormal;
    UI::Modern::PC::Chat::RmlChatState m_modernState;
    UI::Modern::PC::Chat::BlockedChatList m_blockedChatList;

    void Init();

    void SetInputMsgType(int iInputMsgType);
    int GetInputMsgType() const;

    bool UpdateModernActions();
    void SubmitModernChat(const UI::Modern::PC::Chat::RmlChatSubmit &submit);
    void ApplyHistoryDelta(int delta, bool whisper);

  public:
    explicit CNewUIChatInputBox(SessionKeeper &keeper);
    virtual ~CNewUIChatInputBox();

    bool Create(CNewUIManager *pNewUIMng, CNewUIChatLogWindow *pNewUIChatLogWnd,
                CNewUISystemLogWindow *pNewUISystemLogWnd, int x, int y);
    void Release();

    void SetWndPos(int x, int y);

    void SetFont(LegacyFontRole role);

    // Recreate the internal text-input DCs at the current g_fScreenRate.
    bool HaveFocus();

    void AddChatHistory(const type_string &strText);
    void RemoveChatHistory(int index);
    void RemoveAllChatHIstory();

    void AddWhsprIDHistory(const type_string &strWhsprID);
    void RemoveWhsprIDHistory(int index);
    void RemoveAllWhsprIDHIstory();

    bool IsBlockWhisper();
    void SetBlockWhisper(bool bBlockWhisper);
    bool IsBlockedUser(const wchar_t *name) const noexcept;

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const;

    float GetLayerDepth();
    float GetKeyEventOrder();

    void OpenningProcess();
    void ClosingProcess();

    void SetWhsprID(const wchar_t *strWhsprID);

  protected:
    void GetChatText(type_string &strText);
    void GetWhsprID(type_string &strWhsprID);

    void SetTextPosition(int x, int y);
    void SetBuddyPosition(int x, int y);

    void UpdateWhisperTargetFromRightClick();
};
} // namespace SEASON3B

#define g_pFriendList g_pNewUISystem->GetUI_NewFriendWindow()->GetFriendList()
#define g_pLetterList g_pNewUISystem->GetUI_NewFriendWindow()->GetLetterList()
#define MAX_WHISPER 120
#define MAX_WHISPER_LINE 6
#define CHATCONNECT_TIMER 1002

namespace SEASON3B
{
struct ServerMessageInfo
{
    GuildRelationshipType s_byRelationShipType = GuildRelationshipType::Undefined;
    GuildRequestType s_byRelationShipRequestType = GuildRequestType::Undefined;
    BYTE s_byTargetUserIndexH = 0, s_byTargetUserIndexL = 0;
};
class CNewUIGuildInfoWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum
    {
        IMAGE_GUILDINFO_TAB_BUTTON = BITMAP_GUILDINFO_BEGIN
    };
    explicit CNewUIGuildInfoWindow(SessionKeeper &keeper);
    ~CNewUIGuildInfoWindow();
    bool Create(CNewUIManager *manager, int x, int y);
    void Release();
    void SetPos(int x, int y);
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    float GetLayerDepth();
    CNewUIGuildInfoWindow *GetGuildInfo() const;
    void OpenningProcess();
    void ClosingProcess();
    void AddGuildNotice(wchar_t *text);
    void SetRivalGuildName(wchar_t *name);
    void AddGuildMember(GUILD_LIST_t *member);
    void GuildClear();
    void NoticeClear();
    void UnionGuildClear();
    void AddUnionList(const BYTE *decodedMark, wchar_t *name, int count);
    int GetUnionCount();
    const ServerMessageInfo &GetServerMessage()
    {
        return m_MessageInfo;
    }
    void ReceiveGuildRelationShip(GuildRelationshipType relationship, GuildRequestType request,
                                  BYTE high, BYTE low);
    void InvalidateGuildMark();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);

  private:
    using Panel = UI::Modern::PC::Guild::RmlGuildInfoPanel;
    struct Member
    {
        std::wstring name;
        BYTE number, server, role;
    };
    struct Alliance
    {
        std::wstring name;
        int count;
        UI::Modern::RmlMuPalette::Pixels mark;
    };
    using StageKey = std::tuple<std::uint64_t, Panel::Tab, std::optional<std::size_t>,
                                std::optional<std::size_t>, int, int, int, int, int, int, int,
                                std::wstring, std::wstring, std::string>;
    void StageContent();
    void ProcessChanges(const Panel::Changes &changes);
    void Act(Panel::Action action);
    int GetGuildMemberIndex(const wchar_t *name);
    CNewUIManager *manager_ = nullptr;
    SessionRenderUnit &renderer_;
    Panel panel_;
    Panel::Content content_;
    Panel::Tab tab_ = Panel::Tab::Members;
    std::vector<Member> members_;
    std::vector<Alliance> alliance_;
    std::wstring notice_, rival_;
    std::optional<std::size_t> selectedMember_, selectedAlliance_;
    std::optional<StageKey> staged_;
    std::uint64_t dataRevision_ = 0, revision_ = 0;
    ServerMessageInfo m_MessageInfo;
    bool visible_ = false, m_bRequestUnionList = false;
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIGuildMakeWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum
    {
        IMAGE_GUILDMAKE_EDITBOX = BITMAP_GUILDMAKE_BEGIN
    };
    explicit CNewUIGuildMakeWindow(SessionKeeper &keeper);
    ~CNewUIGuildMakeWindow();
    bool Create(CNewUIManager *manager, int x, int y);
    void Release();
    void OpeningProcess();
    void ClosingProcess();
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    void SetPos(int x, int y);
    const POINT &GetPos();
    float GetLayerDepth();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const;

  private:
    using Panel = UI::Modern::PC::Guild::RmlGuildCreatePanel;
    void StageLabels();
    void ApplyEdits(const Panel::Changes &changes);
    void ProcessAction(const Panel::Changes &changes);
    void Advance();
    bool Validate();
    CNewUIManager *manager_ = nullptr;
    SessionRenderUnit &renderer_;
    Panel panel_;
    Panel::Content content_;
    std::string locale_;
    POINT position_{};
    bool visible_ = false;
};
} // namespace SEASON3B

//  UIGuildInfo.h

class CUIManager;
class CUIGuildInfo;
class CUIGuildMaster;
class SessionUiUnit;

enum GUILD_STATUS
{
    G_NONE = (BYTE)-1,
    G_PERSON = 0,
    G_MASTER = 128,
    G_SUB_MASTER = 64,
    G_BATTLE_MASTER = 32
};

enum GUILD_TYPE
{
    GT_NORMAL = 0x00,
    GT_ANGEL = 0x01
};

enum GUILD_RELATIONSHIP
{
    GR_NONE = 0x00,
    GR_UNION = 0x01,
    GR_UNIONMASTER = 0x04,
    GR_RIVAL = 0x02,
    GR_RIVALUNION = 0x08
};

class CUIGuildInfo : protected SessionUiLegacyBindings
{
  public:
    explicit CUIGuildInfo(SessionKeeper &keeper);
    void Open();
    bool IsOpen();
    void Close();
    void SetRivalGuildName(wchar_t *name);
    void AddGuildNotice(wchar_t *text);
    void ClearGuildLog();
    void AddMemberList(GUILD_LIST_t *member);
    void ClearMemberList();
    void AddUnionList(BYTE *mark, wchar_t *name, int count);
    int GetUnionCount();
    void ClearUnionList();
};

// Compatibility entry points use the registered modern owner.
class CUIGuildMaster : protected SessionUiLegacyBindings
{
  public:
    explicit CUIGuildMaster(SessionKeeper &keeper);
    void Open();
    bool IsOpen();
    void Close();
    void ReceiveGuildRelationShip(GuildRelationshipType relationship, GuildRequestType request,
                                  BYTE high, BYTE low);
};

class CUIFriendMenu;
class SessionKeeper;

CUIFriendMenu *CreateSessionFriendMenu(SessionKeeper &keeper);
void DestroySessionFriendMenu(CUIFriendMenu *friendMenu) noexcept;

class SessionRenderText;

#pragma warning(disable : 4786)

namespace SEASON3B
{
class CNewUIManager;

enum MESSAGE_TYPE
{
    TYPE_ALL_MESSAGE = 0,
    TYPE_CHAT_MESSAGE,
    TYPE_WHISPER_MESSAGE,
    TYPE_SYSTEM_MESSAGE,
    TYPE_ERROR_MESSAGE,
    TYPE_PARTY_MESSAGE,
    TYPE_GUILD_MESSAGE,
    TYPE_UNION_MESSAGE,
    TYPE_GENS_MESSAGE,
    TYPE_GM_MESSAGE,

    NUMBER_OF_TYPES,
    TYPE_UNKNOWN = 0xFFFFFFFF
};

template <class T> class TMessageText
{
    typedef std::wstring type_string;

    type_string m_strID, m_strText;
    MESSAGE_TYPE m_MsgType;
    DWORD m_dwIndentSize;

  public:
    TMessageText() : m_MsgType(TYPE_UNKNOWN), m_dwIndentSize(0)
    {
    }
    ~TMessageText()
    {
        Release();
    }

    bool Create(const type_string &strID, const type_string &strText, MESSAGE_TYPE MsgType)
    {
        if (MsgType >= NUMBER_OF_TYPES)
            return false;

        m_strID = strID;
        m_strText = strText;
        m_MsgType = MsgType;

        return true;
    }
    void Release()
    {
        m_strID.resize(0);
        m_strText.resize(0);
        m_MsgType = TYPE_UNKNOWN;
    }

    const type_string &GetID() const
    {
        return m_strID;
    }
    const type_string &GetText() const
    {
        return m_strText;
    }
    MESSAGE_TYPE GetType() const
    {
        return m_MsgType;
    }
};

typedef TMessageText<wchar_t> CMessageText;

class CNewUIChatLogWindow : public CNewUIObj, protected SessionLegacyCalls
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_SCROLL_TOP = CNewUIScrollBar::IMAGE_SCROLL_TOP, //. newui_scrollbar_up.tga
        IMAGE_SCROLL_MIDDLE,                                  //. newui_scrollbar_m.tga
        IMAGE_SCROLL_BOTTOM,                                  //. newui_scrollbar_down.tga
        IMAGE_SCROLLBAR_ON,                                   //. newui_scroll_on.tga
        IMAGE_SCROLLBAR_OFF,                                  //. newui_scroll_off.tga
        IMAGE_DRAG_BTN,                                       //. newui_scrollbar_stretch.tga
    };

  private:
    static constexpr int MAX_CHAT_BUFFER_SIZE = 60;
    static constexpr int MAX_NUMBER_OF_LINES = 200;
    enum EVENT_STATE
    {
        EVENT_NONE = 0,
        EVENT_CLIENT_WND_HOVER,
        EVENT_SCROLL_BTN_DOWN,
        EVENT_RESIZING_BTN_HOVER,
        EVENT_RESIZING_BTN_DOWN,
        EVENT_RESIZING_BTN_UP,
    };

    typedef std::wstring type_string;
    typedef std::vector<CMessageText *> type_vector_msgs;
    typedef std::vector<type_string> type_vector_filters;

    CNewUIManager *m_pNewUIMng;
    UI::Modern::PC::Chat::RmlChatPanel m_modernPanel;
    bool m_blockChatOpen = false;

    type_vector_msgs m_vecAllMsgs;
    type_vector_msgs m_VecChatMsgs;
    type_vector_msgs m_vecWhisperMsgs;
    type_vector_msgs m_vecPartyMsgs;
    type_vector_msgs m_vecGuildMsgs;
    type_vector_msgs m_vecUnionMsgs;
    type_vector_msgs m_vecGensMsgs;
    type_vector_msgs m_VecSystemMsgs;
    type_vector_msgs m_vecErrorMsgs;
    type_vector_msgs m_vecGMMsgs;
    type_vector_filters m_vecFilters;

    POINT m_WndPos, m_ScrollBtnPos;
    SIZE m_WndSize;
    int m_nShowingLines;

    MESSAGE_TYPE m_CurrentRenderMsgType;
    bool m_bShowChatLog;
    int m_iCurrentRenderEndLine;
    int m_iGrapRelativePosY;
    float m_fBackAlpha;

    EVENT_STATE m_EventState;

    bool m_bShowFrame;
    bool m_bPointedMessage;
    int m_iPointedMessageIndex;
    int &MouseWheel;

    void Init();

  public:
    explicit CNewUIChatLogWindow(SessionKeeper &keeper);
    ~CNewUIChatLogWindow() override;

    bool Create(CNewUIManager *pNewUIMng, int x, int y, int nShowingLines = 6);
    void Release();

    void SetPosition(int x, int y);
    void AddText(const type_string &strID, const type_string &strText, MESSAGE_TYPE MsgType,
                 MESSAGE_TYPE ErrMsgType = TYPE_ALL_MESSAGE);
    void RemoveFrontLine(MESSAGE_TYPE MsgType);
    void Clear(MESSAGE_TYPE MsgType);
    void ClearAll();

    UI::Modern::PC::Chat::RmlChatPanel &ModernPanel() noexcept
    {
        return m_modernPanel;
    }
    void SetBlockChatOpen(bool open) noexcept
    {
        m_blockChatOpen = open;
    }

    size_t GetNumberOfLines(MESSAGE_TYPE MsgType);
    MESSAGE_TYPE GetCurrentMsgType() const;

    void ChangeMessage(MESSAGE_TYPE MsgType);

    void ShowChatLog();
    void HideChatLog();

    int GetCurrentRenderEndLine() const;
    void Scrolling(int nRenderEndLine);

    void SetFilterText(const type_string &strFilterText);
    void ResetFilter();

    void SetSizeAuto();
    void SetNumberOfShowingLines(int nShowingLines, OUT LPSIZE lpBoxSize = NULL);
    size_t GetNumberOfShowingLines() const;
    void SetBackAlphaAuto();
    void SetBackAlpha(float fAlpha);
    float GetBackAlpha() const;

    void ShowFrame();
    void HideFrame();
    bool IsShowFrame();

    bool UpdateMouseEvent() override;
    bool UpdateKeyEvent() override;
    bool Update() override;
    bool Render() override;

    float GetLayerDepth() override;    //. 6.1f
    float GetKeyEventOrder() override; //. 8.0f

    void UpdateWndSize();
    void UpdateScrollPos();

  protected:
    SessionRenderText &g_RenderText;
    type_vector_msgs *GetMsgs(MESSAGE_TYPE MsgType);
    void ProcessAddText(const type_string &strID, const type_string &strText, MESSAGE_TYPE MsgType,
                        MESSAGE_TYPE ErrMsgType);

    void SeparateText(IN const type_string &strID, IN const type_string &strText,
                      OUT type_string &strText1, OUT type_string &strText2);

    bool CheckFilterText(const type_string &strTestText);
    void AddFilterWord(const type_string &strWord);
};

class CNewUISystemLogWindow : public CNewUIObj, protected SessionLegacyCalls
{
  private:
    enum
    {
        MAX_MSG_BUFFER_SIZE = 6,
        MAX_NUMBER_OF_LINES = 6,
        WND_WIDTH = 281,
        FONT_LEADING = 4,
        WND_TOP_BOTTOM_EDGE = 2,
        WND_LEFT_RIGHT_EDGE = 4,
        CLIENT_WIDTH = WND_WIDTH - (WND_LEFT_RIGHT_EDGE * 2),
    };

    typedef std::wstring type_string;
    typedef std::vector<CMessageText *> type_vector_msgs;

    CNewUIManager *m_pNewUIMng;
    CNewUIChatLogWindow *m_pChatLogWindow;

    type_vector_msgs m_vecAllMsgs;

    POINT m_WndPos;
    SIZE m_WndSize;
    int m_nShowingLines;

    int m_iCurrentRenderEndLine;
    float m_fBackAlpha;
    bool m_bShowMessages;

    void Init();

    bool RenderMessages();

    void RemoveFrontLine();
    int GetCurrentRenderEndLine() const;

  public:
    explicit CNewUISystemLogWindow(SessionKeeper &keeper);
    ~CNewUISystemLogWindow() override;

    bool Create(CNewUIManager *pNewUIMng, CNewUIChatLogWindow *pChatLogWindow, int x, int y);
    void Release();

    void SetPosition(int x, int y);
    void AddText(const type_string &strText, MESSAGE_TYPE MsgType);

    void ClearAll();
    void ShowMessages()
    {
        m_bShowMessages = true;
    }
    void HideMessages()
    {
        m_bShowMessages = false;
    }

    bool UpdateMouseEvent() override;
    bool UpdateKeyEvent() override;
    bool Update() override;
    bool Render() override;

    float GetLayerDepth() override;
    float GetKeyEventOrder() override;

    bool CheckChatRedundancy(const type_string &strText, int iSearchLine = 1);

  private:
    SessionRenderText &g_RenderText;
};
} // namespace SEASON3B

class SessionGameplayUnit;
class SessionKeeper;
class SessionRenderUnit;
struct SessionInputEvent;

namespace SEASON3B
{
class CNewUIManager;

class CNewUIFriendWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
    CNewUIManager *m_pNewUIMng;
    CUIWindowMgr *m_pFriendWindowMgr;
    CUIFriendMenu &g_pFriendMenu;
    CFriendList friendList_;
    CLetterList letterList_;
    SessionRenderUnit &m_renderer;

  public:
    explicit CNewUIFriendWindow(SessionKeeper &keeper);
    virtual ~CNewUIFriendWindow();

    bool Create(CNewUIManager *pNewUIMng);
    void Release();

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const;

    float GetLayerDepth(); // 6.f

    void Reset();

    void SendUIMessage(int iMessage, int iParam1, int iParam2)
    {
        m_pFriendWindowMgr->SendUIMessage(iMessage, iParam1, iParam2);
    }
    void SendUIMessageToWindow(DWORD dwUIID, int iMessage, int iParam1, int iParam2,
                               std::wstring text = {})
    {
        m_pFriendWindowMgr->SendUIMessageToWindow(dwUIID, iMessage, iParam1, iParam2,
                                                  std::move(text));
    }

    void SetServerEnable(BOOL bFlag)
    {
        m_pFriendWindowMgr->SetServerEnable(bFlag);
    }
    BOOL IsServerEnable()
    {
        return m_pFriendWindowMgr->IsServerEnable();
    }

    DWORD AddWindow(int iType, int x, int y, const wchar_t *strTitle, DWORD dwParentID = 0,
                    int iOption = UIADDWND_NULL)
    {
        return m_pFriendWindowMgr->AddWindow(iType, x, y, strTitle, dwParentID, iOption);
    }
    void AddWindowFinder(CUIBaseWindow *pWindow)
    {
        m_pFriendWindowMgr->AddWindowFinder(pWindow);
    }
    BOOL IsWindow(DWORD dwUIID)
    {
        return m_pFriendWindowMgr->IsWindow(dwUIID);
    }
    CUIBaseWindow *GetWindow(DWORD dwUIID)
    {
        return m_pFriendWindowMgr->GetWindow(dwUIID);
    }
    void SetWindowsEnable(DWORD bEnable)
    {
        m_pFriendWindowMgr->SetWindowsEnable(bEnable);
    }
    void HideAllWindow(BOOL bHide, BOOL bMainClose = FALSE)
    {
        m_pFriendWindowMgr->HideAllWindow(bHide, bMainClose);
    }
    void HideAllWindowClear()
    {
        m_pFriendWindowMgr->HideAllWindowClear();
    }
    void OpenMainWnd(int x, int y)
    {
        m_pFriendWindowMgr->OpenMainWnd(x, y);
    }
    void CloseMainWnd()
    {
        m_pFriendWindowMgr->CloseMainWnd();
    }
    DWORD GetTopWindowUIID()
    {
        return m_pFriendWindowMgr->GetTopWindowUIID();
    }
    DWORD GetTopNotMainWindowUIID()
    {
        return m_pFriendWindowMgr->GetTopNotMainWindowUIID();
    }
    BOOL IsForceTopWindow(DWORD dwWindowUIID)
    {
        return m_pFriendWindowMgr->IsForceTopWindow(dwWindowUIID);
    }
    BOOL HaveForceTopWindow()
    {
        return m_pFriendWindowMgr->HaveForceTopWindow();
    }
    void RefreshMainWndPalList()
    {
        m_pFriendWindowMgr->RefreshMainWndPalList();
    }

    CUIFriendWindow *GetFriendMainWindow()
    {
        return m_pFriendWindowMgr->GetFriendMainWindow();
    }
    void SetAddFriendWindow(DWORD dwAddWindowUIID)
    {
        m_pFriendWindowMgr->SetAddFriendWindow(dwAddWindowUIID);
    }
    DWORD GetAddFriendWindow()
    {
        return m_pFriendWindowMgr->GetAddFriendWindow();
    }

    void RefreshMainWndChatRoomList()
    {
        m_pFriendWindowMgr->RefreshMainWndChatRoomList();
    }
    void SetChatReject(BOOL bChatReject)
    {
        m_pFriendWindowMgr->SetChatReject(bChatReject);
    }
    BOOL GetChatReject()
    {
        return m_pFriendWindowMgr->GetChatReject();
    }

    void RefreshMainWndLetterList()
    {
        m_pFriendWindowMgr->RefreshMainWndLetterList();
    }
    BOOL LetterReadCheck(DWORD dwLetterID)
    {
        return m_pFriendWindowMgr->LetterReadCheck(dwLetterID);
    }
    void CloseLetterRead(DWORD dwLetterID)
    {
        m_pFriendWindowMgr->CloseLetterRead(dwLetterID);
    }
    void SetLetterReadWindow(DWORD dwLetterID, DWORD dwWindowUIID)
    {
        m_pFriendWindowMgr->SetLetterReadWindow(dwLetterID, dwWindowUIID);
    }
    DWORD GetLetterReadWindow(DWORD dwLetterID)
    {
        return m_pFriendWindowMgr->GetLetterReadWindow(dwLetterID);
    }

    BOOL IsRenderFrame()
    {
        return m_pFriendWindowMgr->IsRenderFrame();
    }

    CFriendList *GetFriendList();
    CLetterList *GetLetterList();
    CUIFriendMenu *GetFriendMenu();
};
} // namespace SEASON3B

class SessionRenderUnit;
struct SessionInputEvent;

namespace SEASON3B
{
class CNewUIPartyListWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  private:
    CameraProjection &cameraProjection_;
    SessionRenderUnit &renderer_;
    CNewUIManager *m_pNewUIMng;
    SessionBoundArray<CNewUIButton, MAX_PARTYS> m_BtnPartyExit;
    float m_partyFrameX = 0.0F;
    float m_partyFrameY = 0.0F;
    float m_partyFrameDragOffsetX = 0.0F;
    float m_partyFrameDragOffsetY = 0.0F;
    int m_partyFrameRowCount = 0;
    bool m_partyFrameDragging = false;
    bool m_partyFrameMinimized = false;
    bool m_partyFrameMinimizePressed = false;
    ButtonVisualState m_partyFrameMinimizeState = ButtonVisualState::Up;
    bool m_bActive;
    int m_iSelectedCharacter;

  public:
    explicit CNewUIPartyListWindow(SessionKeeper &keeper);
    virtual ~CNewUIPartyListWindow();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);
    void SetPos(int x);

    bool UpdateMouseEvent();
    bool OwnsModernPointer(const SessionInputEvent &event) const;
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    bool BtnProcess();

    float GetLayerDepth(); //. 5.4f

    void OpenningProcess();
    void ClosingProcess();

    int GetSelectedCharacter();
    void SetListBGColor();

  private:
    void StagePartyFrame();
    void LeaveParty(int index);
    bool UpdatePartyFrameMouse();
    bool IsMouseInPartyBox(float x, float y, float width, float height) const;
    void SyncLeaveButtons();
    bool SelectCharacterInPartyList(PARTY_t *pMember);
    void RenderPartyHPOnHead();
    void DrawWorldPartyHpBar(int x, int y, int stepHp);
};
} // namespace SEASON3B

namespace ChatLogDetail
{
using namespace SEASON3B;
const UI::Modern::RmlUiDesign &ChatCompatibility();
} // namespace ChatLogDetail

namespace PartyListDetail
{
using namespace SEASON3B;
using UI::Modern::PartyFrameDesign;
using UI::Modern::PartyFrameMetric;
int PartyFrameLogicalSize(float gfxPixels, float screenRate, float maximumScale);
ButtonVisualState ToRmlButtonState(BUTTON_STATE state) noexcept;
} // namespace PartyListDetail
