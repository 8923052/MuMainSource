#pragma once
#include "app/Application.h"
#include "app/ApplicationConfigScheduling.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include "ui/runtime/UiControls.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class CMsgWin;
class CSysMenuWin;
class CLoginMainWin;
class CServerSelWin;
class CLoginWin;
class CCreditWin;
class CCharSelMainWin;
class CCharMakeWin;
class CCharInfoBalloonMng;
class CServerMsgWin;
class CUIFriendWindow;
class SessionItemStore;
namespace SEASON3B
{
class CNewUI3DRenderMng;
class CNewUIBattleSoccerScore;
class CNewUIBloodCastle;
class CNewUIBuffWindow;
class CNewUICastleWindow;
class CNewUICatapultWindow;
class CNewUIChaosCastleTime;
class CNewUICharacterInfoWindow;
class CNewUIChatInputBox;
class CNewUIChatLogWindow;
class CNewUICommandWindow;
class CNewUICryWolf;
class CNewUICursedTempleEnter;
class CNewUICursedTempleResult;
class CNewUICursedTempleSystem;
class CNewUIDoppelGangerFrame;
class CNewUIDoppelGangerWindow;
class CNewUIDuelWatchMainFrameWindow;
class CNewUIDuelWatchUserListWindow;
class CNewUIDuelWatchWindow;
class CNewUIDuelWindow;
class CNewUIEmpireGuardianNPC;
class CNewUIEmpireGuardianTimer;
class CNewUIEnterBloodCastle;
class CNewUIEnterDevilSquare;
class CNewUIExchangeLuckyCoin;
class CNewUIFriendWindow;
class CNewUIGateSwitchWindow;
class CNewUIGatemanWindow;
class CNewUIGensRanking;
class CNewUIGoldBowmanLena;
class CNewUIGoldBowmanWindow;
class CNewUIGuardWindow;
class CNewUIGuildInfoWindow;
class CNewUIGuildMakeWindow;
class CNewUIHelpWindow;
class CNewUIHeroPositionInfo;
class CNewUIHotKey;
class CNewUIInGameShop;
class CNewUIInventoryExtension;
class CNewUIItemEnduranceInfo;
class CNewUIItemExplanationWindow;
class CNewUIKanturu2ndEnterNpc;
class CNewUIKanturuInfoWindow;
class CNewUILuckyItemWnd;
class CNewUIMainFrameWindow;
class CNewUIManager;
class CNewUIMasterLevel;
class CNewUIMiniMap;
class CNewUIMixInventory;
class CNewUIMoveCommandWindow;
class CNewUIMuHelper;
class CNewUIMuHelperSkillList;
class CNewUIMyInventory;
class CNewUIMyQuestInfoWindow;
class CNewUIMyShopInventory;
class CNewUINPCDialogue;
class CNewUINPCQuest;
class CNewUINPCShop;
class CNewUINameWindow;
class CNewUIOptionWindow;
class CNewUIPartyListWindow;
class CNewUIPetInfoWindow;
class CNewUIPurchaseShopInventory;
class CNewUIQuestProgress;
class CNewUIQuestProgressByEtc;
class CNewUIQuickCommandWindow;
class CNewUIRegistrationLuckyCoin;
class CNewUISetItemExplanation;
class CNewUISiegeWarfare;
class CNewUISkillList;
class CNewUISlideWindow;
class CNewUIStorageInventory;
class CNewUIStorageInventoryExt;
class CNewUISystemLogWindow;
class CNewUITrade;
class CNewUIUnitedMarketPlaceWindow;
class CNewUIWindowMenu;
} // namespace SEASON3B
#define UIM_SCENE_NONE 0
#define UIM_SCENE_TITLE 1
#define UIM_SCENE_LOGIN 2
#define UIM_SCENE_LOADING 3
#define UIM_SCENE_CHARACTER 4
#define UIM_SCENE_MAIN 5
#define WM_CHATROOMMSG_BEGIN (WM_USER + 0x100)
#define WM_CHATROOMMSG_END (WM_USER + 0x200)
#define SLIDE_HELP_OFF 0x10
#define SLIDEHELP_TIMER 1003
#define WARNING_TIMER 1004

class CErrorReport;

class CUIManager;
class CUITextInputBox;

enum
{
    INTERFACE_NONE = 0,
    INTERFACE_FRIEND,
    INTERFACE_MOVEMAP,
    INTERFACE_PARTY,
    INTERFACE_QUEST,
    INTERFACE_GUILDINFO,
    INTERFACE_TRADE,
    INTERFACE_STORAGE,
    INTERFACE_GUILDSTORAGE,
    INTERFACE_MIXINVENTORY,
    INTERFACE_COMMAND,
    INTERFACE_PET,
    INTERFACE_PERSONALSHOPSALE,
    INTERFACE_DEVILSQUARE,
    INTERFACE_SERVERDIVISION,
    INTERFACE_BLOODCASTLE,
    INTERFACE_NPCBREEDER,
    INTERFACE_NPCSHOP,
    INTERFACE_PERSONALSHOPPURCHASE,
    INTERFACE_NPCGUILDMASTER,
    INTERFACE_GUARDSMAN,
    INTERFACE_SENATUS,
    INTERFACE_GATEKEEPER,
    INTERFACE_CATAPULTATTACK,
    INTERFACE_CATAPULTDEFENSE,
    INTERFACE_GATESWITCH,
    INTERFACE_CHARACTER,
    INTERFACE_INVENTORY,
    INTERFACE_REFINERY,
    INTERFACE_REFINERYINFO,
    INTERFACE_KANTURU2ND_ENTERNPC,
    INTERFACE_MAP_ENTRANCE,
    INTERFACE_MAX_COUNT,
};

class CUIManager : protected SessionLegacyCalls
{
  public:
    explicit CUIManager(SessionKeeper &keeper);
    virtual ~CUIManager();

  protected:
    bool IsCanOpen(DWORD dwInterfaceFlag);
    bool CloseInterface(std::list<DWORD> &dwInterfaceFlag, DWORD dwExtraData = 0);

  public:
    void Init();
    POINT RenderWindowBase(int nHeight, int nOriginY = -1);
    bool PressKey(int nKey);
    bool IsInputEnable();
    void UpdateInput();
    void Render();
    void CloseAll();
    bool IsOpen(DWORD dwInterface);
    bool Open(DWORD dwInterface, DWORD dwExtraData = 0);
    bool Close(DWORD dwInterface, DWORD dwExtraData = 0);
    void GetInterfaceAll(std::list<DWORD> &outflag);
    void GetInsertInterface(std::list<DWORD> &outflag, DWORD insertflag);
    void GetDeleteInterface(std::list<DWORD> &outflag, DWORD deleteflag);

  private:
    HWND &g_hWnd;
    CErrorReport &g_ErrorReport;
    CUITextInputBox *&focusedTextInputBox_;
};

class CSprite;
class CGaugeBar;
class CWin;
class CLoadingScene;
class SessionUiUnit;
class SessionRenderUnit;
class CErrorReport;
class CmuConsoleDebug;
class SessionKeeper;
class CInput;
struct SessionInputEvent;
namespace SEASON3B
{
class CNewUIObj;
}

class CUIMng : protected SessionLegacyCalls
{
  public:
    std::unique_ptr<CMsgWin> m_MsgWin;
    std::unique_ptr<CSysMenuWin> m_SysMenuWin;
    std::unique_ptr<CLoginMainWin> m_LoginMainWin;
    std::unique_ptr<CServerSelWin> m_ServerSelWin;
    std::unique_ptr<CLoginWin> m_LoginWin;
    std::unique_ptr<CCreditWin> m_CreditWin;
    std::unique_ptr<CCharSelMainWin> m_CharSelMainWin;
    std::unique_ptr<CCharMakeWin> m_CharMakeWin;
    std::unique_ptr<CCharInfoBalloonMng> m_CharInfoBalloonMng;
    std::unique_ptr<CServerMsgWin> m_ServerMsgWin;
    CLoadingScene *m_pLoadingScene;

  protected:
    CSprite *m_asprTitle;
    std::unique_ptr<SessionBoundArray<CSprite, 13>> titleSprites_;
    CGaugeBar *m_pgbLoding;
    SessionKeeper &sessionKeeper_;
    SessionRenderUnit &renderer_;
    CInput &input_;
    unsigned int &WindowWidth;
    unsigned int &WindowHeight;
    EGameScene &SceneFlag;
    CPList m_WinList;
    bool m_bCursorOnUI;
    bool m_bBlockCharMove;
    int m_nScene;
    bool m_bWinActive;
    bool m_bSysMenuWinShow;
    bool escapeKeyHeld_ = false;
    mutable SEASON3B::CNewUIObj *focusedModernUiObject_ = nullptr;

  public:
    explicit CUIMng(SessionKeeper &keeper);
    virtual ~CUIMng();

    void CreateTitleSceneUI();
    void ReleaseTitleSceneUI();
    void RenderTitleSceneUI(HDC hDC, DWORD dwNow, DWORD dwTotal);
    void Create();
    void Release();
    void CreateLoginScene();
    void CreateCharacterScene();
    void CreateMainScene();

    /**
     * @brief Re-layouts the current scene's UI for the current WindowWidth/
     * Height. Call after a runtime resolution change so info boxes, menus,
     * etc. don't end up anchored to the old screen size.
     *
     * Only affects the old-style CWin-based windows owned by CUIMng; the
     * new-style CNewUI* windows are driven by g_pNewUISystem separately.
     */
    void RepositionSceneUI();

    void Update(double dDeltaTick);
    void Render();
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const;

    void ShowWin(CWin *pWin);
    void HideWin(CWin *pWin);

    bool IsCursorOnUI() const;
    void PopUpMsgWin(int nMsgCode, wchar_t *pszMsg = NULL);
    void AddServerMsg(wchar_t *pszMsg);
    void CloseMsgWin();
    void SetSysMenuWinShow(bool bShow)
    {
        m_bSysMenuWinShow = bShow;
    }
    bool IsSysMenuWinShow()
    {
        return m_bSysMenuWinShow;
    };

  protected:
    friend class SessionUiUnit;

    void InitializeSession();
    void ShutdownSession() noexcept;
    bool PrepareModernMainUiOnWorker(int viewportWidth, int viewportHeight);
    std::optional<UI::Modern::RmlTextInputArea> ModernMainTextInputArea() const;
    std::optional<UI::Modern::RmlTextInputArea> ModernEventTextInputArea() const;
    bool PrepareModernInventoryUiOnWorker(int viewportWidth, int viewportHeight);
    bool PrepareModernEventUiOnWorker(int viewportWidth, int viewportHeight);
    std::optional<bool> ProcessModernInventoryPanelsInput(const SessionInputEvent &event);
    std::optional<bool> ProcessModernCharacterPanelsInput(const SessionInputEvent &event);
    SEASON3B::CNewUIObj *FocusedModernUiObject() const noexcept;

    void RemoveWinList();
    CWin *SetActiveWin(CWin *pWin);
    void CheckDockWin();
    bool SetDockWinPosition(CWin *pMoveWin, int nDockX, int nDockY);
};

class SessionGameplayUnit;
class SessionNetworkUnit;
class SessionUiUnit;
class SessionKeeper;
class CMapManager;
class CSummonSystem;
class LegacyRenderFacade;
struct SessionInputEvent;

typedef std::map<DWORD, CUIBaseWindow *, std::less<DWORD>> WndMap;

class CUIWindowMgr : public CUIMessage, protected SessionUiLegacyBindings
{
  public:
    explicit CUIWindowMgr(SessionKeeper &keeper);
    virtual ~CUIWindowMgr();

    void Reset();
    DWORD AddWindow(int iWindowType, int iPos_x, int iPos_y, const wchar_t *pszTitle,
                    DWORD dwParentID = 0, int iOption = UIADDWND_NULL);
    void RemoveWindow(DWORD dwUIID);
    void Render();
    void DoAction();
    void ShowHideWindow(DWORD dwUIID, BOOL bShowWindow);
    void HideAllWindow(BOOL bHide, BOOL bMainClose = FALSE);
    void HideAllWindowClear();
    CUIBaseWindow *GetWindow(DWORD dwUIID);
    BOOL IsWindow(DWORD dwUIID);
    CUIFriendWindow *GetFriendMainWindow()
    {
        return (CUIFriendWindow *)GetWindow(m_dwMainWindowUIID);
    }
    void SetWindowsEnable(DWORD bWindowsEnable)
    {
        m_bWindowsEnable = bWindowsEnable;
    }
    BOOL GetWindowsEnable()
    {
        return m_bWindowsEnable;
    }
    DWORD GetTopWindowUIID()
    {
        return (m_WindowArrangeList.empty() == TRUE ? 0 : *m_WindowArrangeList.rbegin());
    }
    DWORD GetTopNotMainWindowUIID();

    void AddWindowFinder(CUIBaseWindow *pWindow);
    void RemoveWindowFinder(DWORD dwUIID);
    void SendUIMessageToWindow(DWORD dwUIID, int iMessage, LONG_PTR iParam1, LONG_PTR iParam2,
                               std::wstring text = {});

    void OpenMainWnd(int iPos_x, int iPos_y);
    void CloseMainWnd();
    void RefreshMainWndPalList();
    void RefreshMainWndLetterList();
    void RefreshMainWndChatRoomList();

    void SetChatReject(BOOL bChatReject)
    {
        m_bChatReject = bChatReject;
    }
    BOOL GetChatReject()
    {
        return m_bChatReject;
    }

    BOOL LetterReadCheck(DWORD dwLetterID);
    void CloseLetterRead(DWORD dwLetterID);
    void SetLetterReadWindow(DWORD dwLetterID, DWORD dwWindowUIID);
    DWORD GetLetterReadWindow(DWORD dwLetterID);

    void SetServerEnable(BOOL bFlag);
    BOOL IsServerEnable()
    {
        return m_bServerEnable;
    }

    void SetAddFriendWindow(DWORD dwAddWindowUIID)
    {
        m_dwAddWindowUIID = dwAddWindowUIID;
    }
    DWORD GetAddFriendWindow()
    {
        return m_dwAddWindowUIID;
    }

    void AddForceTopWindowList(DWORD dwWindowUIID);
    void RemoveForceTopWindowList(DWORD dwWindowUIID);
    BOOL IsForceTopWindow(DWORD dwWindowUIID);
    BOOL HaveForceTopWindow()
    {
        return !m_ForceTopWindowList.empty();
    }

    BOOL IsRenderFrame()
    {
        return m_bRenderFrame;
    }
    bool PrepareModernUiOnWorker(int, int, bool visible);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const;
    bool RecordModernUi(LegacyRenderFacade &facade) const;
    void ApplyModernUiChanges();

  protected:
    void HandleMessage();

  public:
    BOOL m_bRenderFrame;

  protected:
    SessionGameplayUnit &gameplay_;
    BOOL m_bWindowsEnable;
    DWORD m_dwMainWindowUIID;
    WndMap m_WindowMap;
    WndMap m_WindowFindMap;
    WndMap m_WindowReadyMap;
    WndMap::iterator m_WindowMapIter;
    std::list<DWORD> m_WindowArrangeList;
    std::list<DWORD>::iterator m_WindowArrangeListIter;
    std::list<DWORD>::reverse_iterator m_WindowReverseArrangeListIter;
    std::map<DWORD, DWORD, std::less<DWORD>> m_LetterReadMap;
    std::map<DWORD, DWORD, std::less<DWORD>>::iterator m_LetterReadMapIter;
    BOOL m_bCurrentHideWindowState;
    std::list<DWORD> m_HideWindowList;
    std::list<DWORD> m_ForceTopWindowList;

    int m_iMainWindowPos_x, m_iMainWindowPos_y;
    int m_iMainWindowWidth, m_iMainWindowHeight;
    int m_iMainWindowBackPos_y, m_iMainWindowBackHeight;
    BOOL m_bIsMainWindowMaximize;
    BOOL m_bChatReject;
    int m_iLastFriendWindowTabIndex;

    BOOL m_bServerEnable;
    int m_iFriendMainWindowTitleNumber;
    DWORD m_dwAddWindowUIID;
};

class ApplicationKeeper;

namespace SEASON3B
{
#define g_IsPurchaseShop IsPurchaseShop()

// Scaled sprite blit: maps a source region (sx,sy,sw,sh, in texels) onto the
// dest rect (x,y,width,height, in pixels). Unlike RenderImage above — where
// width/height also set the sampled texel extent (1:1, so a smaller size just
// crops) — this lets a fixed-size sprite be drawn larger or smaller.
class CNewKeyInput final : protected ApplicationLegacyCalls
{
    BYTE (&m_pInputInfo)[256];
    bool &g_bEnterPressed;

#ifndef ASG_FIX_ACTIVATE_APP_INPUT
    void Init();
#endif

  public:
    enum KEY_STATE
    {
        KEY_NONE = 0,
        KEY_RELEASE,
        KEY_PRESS,
        KEY_REPEAT
    };
    explicit CNewKeyInput(ApplicationKeeper &keeper) noexcept;
    ~CNewKeyInput();

#ifdef ASG_FIX_ACTIVATE_APP_INPUT
    void Init();
#endif
    void ScanAsyncKeyState();
    void SetKeyState(int iVirtKey, KEY_STATE KeyState);
    const BYTE *StateData() const noexcept;

  private:
    friend class ::ApplicationLegacyCalls;
    friend class ::ApplicationSupportCalls;
    bool IsNone(int virtualKey) const;       // OMF-01977
    bool IsRelease(int virtualKey) const;    // OMF-01978
    bool IsPress(int virtualKey) const;      // OMF-01979
    bool IsRepeat(int virtualKey) const;     // OMF-01980
    bool IsEnterPressed();                   // OMF-01812
    void SetEnterPressed(bool enterPressed); // OMF-01813
};
} // namespace SEASON3B

//	NewUIGroup.h

namespace SEASON3B
{
class CNewUIGroup : public CNewUIObj, protected SessionUiLegacyBindings
{
    typedef std::vector<CNewUIObj *> type_vector_uibase;
    type_vector_uibase m_vecUI; //. for rendering and updating
  private:
    float m_fLayerDepth;
    float m_fKeyEventOrder;

  public:
    explicit CNewUIGroup(SessionKeeper &keeper);
    virtual ~CNewUIGroup();

    void AddUIObj(CNewUIObj *pUIObj);

    virtual bool Render();
    virtual bool Update();
    virtual bool UpdateMouseEvent();
    virtual bool UpdateKeyEvent();

    virtual void Release();

    void SetKeyEventOrder(float fOrder)
    {
        m_fKeyEventOrder = fOrder;
    }
    float GetKeyEventOrder()
    {
        return m_fKeyEventOrder;
    }

    void SetLayerDepth(float fDepth)
    {
        m_fLayerDepth = fDepth;
    }
    float GetLayerDepth()
    {
        return m_fLayerDepth;
    }
};
} // namespace SEASON3B

#pragma warning(disable : 4786)

class SessionGameplayUnit;
class SessionUiUnit;
namespace MUHelper
{
class SessionMuHelperUnit;
}

namespace SEASON3B
{
class CNewUIHotKey;
class CNewUIWindowMenu;

class CNewUIManager : protected SessionUiLegacyBindings
{
    typedef std::vector<CNewUIObj *> type_vector_uibase;
    typedef std::map<DWORD, CNewUIObj *> type_map_uibase;

    type_vector_uibase m_vecUI; //. for rendering and updating
    type_map_uibase m_mapUI;    //. for managing
    type_vector_uibase m_layerOrderedUI;
    type_vector_uibase m_keyOrderedUI;
    bool m_orderDirty = true;
    std::uint64_t m_nextDynamicLayerOrder = 0;
    SessionUiUnit &sessionUi;

    CNewUIObj *m_pActiveMouseUIObj, *m_pActiveKeyUIObj;
#ifdef PBG_MOD_STAMINA_UI
    int m_nShowUICnt;
#endif //PBG_MOD_STAMINA_UI

    void MarkOrderDirty(CNewUIObj *removed = nullptr) noexcept;
    void RefreshOrderedUI();
    bool RenderPickedItemOverlay();

  public:
    explicit CNewUIManager(SessionKeeper &keeper);
    ~CNewUIManager();

    void AddUIObj(DWORD dwKey, CNewUIObj *pUIObj);
    void RemoveUIObj(DWORD dwKey);
    void RemoveUIObj(CNewUIObj *pUIObj);
    void RemoveAllUIObjs();

    void ReleaseAllUIObj();

    CNewUIObj *FindUIObj(DWORD dwKey);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent(CNewUIHotKey &hotKey);
    bool Update();
    bool Render();

    CNewUIObj *GetActiveMouseUIObj();
    CNewUIObj *GetActiveKeyUIObj();
    void ResetActiveUIObj();
    void BringToFront(CNewUIObj *ui);

    bool IsInterfaceVisible(DWORD dwKey);
    bool IsInterfaceEnabled(DWORD dwKey);

    void ShowInterface(DWORD dwKey, bool bShow = true);

    void EnableInterface(DWORD dwKey, bool bEnable = true);
    void ShowAllInterfaces(bool bShow = true);
    void EnableAllInterfaces(bool bEnable = true);

#ifdef PBG_MOD_STAMINA_UI
    int GetShowUICnt();
#endif //PBG_MOD_STAMINA_UI

  protected:
    bool CompareLayerDepth(CNewUIObj *left, CNewUIObj *right) const;
    static bool CompareKeyEventOrder(INewUIBase *pObj1, INewUIBase *pObj2);
};
} // namespace SEASON3B

class AppWindow;
class ApplicationAudio;
class SessionGameplayUnit;
class SessionGameDataUnit;
class SessionKeeper;
class SessionRenderUnit;
namespace MUHelper
{
class SessionMuHelperUnit;
}

namespace SEASON3B
{

class CNewUISystem : protected SessionUiLegacyBindings
{
    CNewUIManager *m_pNewUIMng;
    CNewUI3DRenderMng *m_pNewUI3DRenderMng;
    CNewUIHotKey *m_pNewUIHotKey;

  public:
    explicit CNewUISystem(SessionKeeper &keeper);
    ~CNewUISystem();

    bool Create();
    void Release();

    bool LoadMainSceneInterface();
    void UnloadMainSceneInterface();

    bool IsVisible(DWORD dwKey);
    void Show(DWORD dwKey);
    void Hide(DWORD dwKey);
    void Toggle(DWORD dwKey); //. Show <-> Hide
    void HideAll();

    // Shared handler for the top-right "X" close glyph baked into the common
    // item-frame (newui_item_back04.tga). Hides `dwKey` when the corner is
    // left-clicked and swallows the mouse so the click can't fall through to
    // the world (which would walk the character). Returns true if handled.
    // Replaces the ptExitBtn1 block that was copy-pasted across the windows.
    bool HandleFrameCornerClose(const POINT &winPos, DWORD dwKey);

    void Enable(DWORD dwKey);
    void Disable(DWORD dwKey);

    bool CheckMouseUse();
    bool CheckKeyUse();

    bool Update(CTimer2::StartTickTime &timer2StartTickTime);

    CNewUIManager *GetNewUIManager() const;
    CNewUI3DRenderMng *GetNewUI3DRenderMng() const;
    CNewUIHotKey *GetNewUIHotKey() const;

    bool IsImpossibleSendMoveInterface();
    void UpdateSendMoveInterface();
    bool IsImpossibleTradeInterface();
    bool IsImpossibleDuelInterface();
    bool IsImpossibleHideInterface(DWORD dwKey);

  protected:
    void HideAllGroupA();
    void HideAllGroupB();
    void HideGroupBeforeOpenInterface();
    void UpdateHeroPositionInfoVisibilityForLayoutChange(DWORD dwKey);
    void SyncHeroPositionInfoVisibility();
    bool ShouldHideHeroPositionInfo();

    /* Interface classes */
  private:
    CNewUIChatInputBox *m_pNewChatInputBox;
    CNewUIChatLogWindow *m_pNewChatLogWindow;
    CNewUISystemLogWindow *m_pNewSystemLogWindow;
    CNewUISlideWindow *m_pNewSlideWindow;
    CNewUIFriendWindow *m_pNewFriendWindow;
    CNewUIMainFrameWindow *m_pNewMainFrameWindow;
    CNewUISkillList *m_pNewSkillList;
    SessionItemStore *m_pNewItemMng;
    CNewUIMyInventory *m_pNewMyInventory;
    CNewUIInventoryExtension *m_pNewMyInventoryExt;
    CNewUINPCShop *m_pNewNPCShop;
    CNewUIPetInfoWindow *m_pNewPetInfoWindow;
    CNewUIMixInventory *m_pNewMixInventory;
    CNewUICastleWindow *m_pNewCastleWindow;
    CNewUIGuardWindow *m_pNewGuardWindow;
    CNewUIGatemanWindow *m_pNewGatemanWindow;
    CNewUIGateSwitchWindow *m_pNewGateSwitchWindow;
    CNewUIStorageInventory *m_pNewStorageInventory;
    CNewUIStorageInventoryExt *m_pNewStorageInventoryExt;
    CNewUIGuildMakeWindow *m_pNewGuildMakeWindow;
    CNewUIGuildInfoWindow *m_pNewGuildInfoWindow;
    CNewUIMyShopInventory *m_pNewMyShopInventory;
    CNewUIPurchaseShopInventory *m_pNewPurchaseShopInventory;
    CNewUICharacterInfoWindow *m_pNewCharacterInfoWindow;
    CNewUIMyQuestInfoWindow *m_pNewMyQuestInfoWindow;
    CNewUIPartyListWindow *m_pNewPartyListWindow;
    CNewUINPCQuest *m_pNewNPCQuest;
    CNewUIEnterBloodCastle *m_pNewEnterBloodCastle;
    CNewUIEnterDevilSquare *m_pNewEnterDevilSquare;
    CNewUIBloodCastle *m_pNewBloodCastle;
    CNewUITrade *m_pNewTrade;
    CNewUIKanturu2ndEnterNpc *m_pNewKanturu2ndEnterNpc;
    CNewUIKanturuInfoWindow *m_pNewKanturuInfoWindow;
    CNewUICatapultWindow *m_pNewCatapultWindow;
    CNewUIChaosCastleTime *m_pNewChaosCastleTime;
    CNewUIBattleSoccerScore *m_pNewBattleSoccerScore;
    CNewUICommandWindow *m_pNewCommandWindow;
    CNewUIHeroPositionInfo *m_pNewHeroPositionInfo;
    CNewUIWindowMenu *m_pNewWindowMenu;
    CNewUIOptionWindow *m_pNewOptionWindow;
    CNewUIHelpWindow *m_pNewHelpWindow;
    CNewUIItemExplanationWindow *m_pNewItemExplanationWindow;
    CNewUISetItemExplanation *m_pNewSetItemExplanation;
    CNewUIQuickCommandWindow *m_pNewQuickCommandWindow;
    CNewUIMoveCommandWindow *m_pNewMoveCommandWindow;
    CNewUIDuelWindow *m_pNewDuelWindow;
    CNewUINameWindow *m_pNewNameWindow;
    CNewUISiegeWarfare *m_pNewSiegeWarfare;
    CNewUIItemEnduranceInfo *m_pNewItemEnduranceInfo;
    CNewUIBuffWindow *m_pNewBuffWindow;
    CNewUICursedTempleEnter *m_pNewCursedTempleEnterWindow;
    CNewUICursedTempleSystem *m_pNewCursedTempleWindow;
    CNewUICursedTempleResult *m_pNewCursedTempleResultWindow;
    CNewUICryWolf *m_pNewCryWolfInterface;
    CNewUIMasterLevel *m_pNewMaster_Level_Interface;
    CNewUIGoldBowmanWindow *m_pNewGoldBowman;
    CNewUIGoldBowmanLena *m_pNewGoldBowmanLena;
    CNewUIRegistrationLuckyCoin *m_pNewLuckyCoinRegistration;
    CNewUIExchangeLuckyCoin *m_pNewExchangeLuckyCoinWindow;
    CNewUIDuelWatchWindow *m_pNewDuelWatchWindow;
    CNewUIDuelWatchMainFrameWindow *m_pNewDuelWatchMainFrameWindow;
    CNewUIDuelWatchUserListWindow *m_pNewDuelWatchUserListWindow;
#ifdef PBG_ADD_INGAMESHOP_UI_MAINFRAME
    CNewUIInGameShop *m_pNewInGameShop;
#endif //PBG_ADD_INGAMESHOP_UI_MAINFRAME
    CNewUIDoppelGangerWindow *m_pNewDoppelGangerWindow;
    CNewUIDoppelGangerFrame *m_pNewDoppelGangerFrame;
    CNewUINPCDialogue *m_pNewNPCDialogue;
    CNewUIQuestProgress *m_pNewQuestProgress;
    CNewUIQuestProgressByEtc *m_pNewQuestProgressByEtc;
    CNewUIEmpireGuardianNPC *m_pNewEmpireGuardianNPC;
    CNewUIEmpireGuardianTimer *m_pNewEmpireGuardianTimer;
    CNewUIMiniMap *m_pNewMiniMap;
    CNewUIGensRanking *m_pNewGensRanking;
    CNewUIUnitedMarketPlaceWindow *m_pNewUnitedMarketPlaceWindow;
    CNewUILuckyItemWnd *m_pNewUILuckyItemWnd;
    CNewUIMuHelper *m_pNewUIMuHelper;
    CNewUIMuHelperSkillList *m_pNewUIMuHelperSkillList;
    bool CreatePersonalItemTable();
    void ReleasePersonalItemTable();

  public:
    CNewUIChatInputBox *GetUI_NewChatInputBox() const;
    CNewUIChatLogWindow *GetUI_NewChatLogWindow() const;
    CNewUISystemLogWindow *GetUI_NewSystemLogWindow() const;
    CNewUISlideWindow *GetUI_NewSlideWindow() const;
    CNewUIGuildMakeWindow *GetUI_NewGuildMakeWindow() const;
    CNewUIFriendWindow *GetUI_NewFriendWindow() const;
    CNewUIMainFrameWindow *GetUI_NewMainFrameWindow() const;
    CNewUISkillList *GetUI_NewSkillList() const;
    SessionItemStore *GetUI_NewItemMng() const;
    CNewUIMyInventory *GetUI_NewMyInventory() const;
    CNewUIInventoryExtension *GetUI_NewMyInventoryExt() const;
    CNewUINPCShop *GetUI_NewNpcShop() const;
    CNewUIPetInfoWindow *GetUI_NewPetInfoWindow() const;
    CNewUIMixInventory *GetUI_NewMixInventory() const;
    CNewUICastleWindow *GetUI_NewCastleWindow() const;
    CNewUIGuardWindow *GetUI_NewGuardWindow() const;
    CNewUIGatemanWindow *GetUI_NewGatemanWindow() const;
    CNewUIGateSwitchWindow *GetUI_NewGateSwitchWindow() const;
    CNewUIStorageInventory *GetUI_NewStorageInventory() const;
    CNewUIStorageInventoryExt *GetUI_NewStorageInventoryExt() const;
    CNewUIGuildInfoWindow *GetUI_NewGuildInfoWindow() const;
    CNewUIMyShopInventory *GetUI_NewMyShopInventory() const;
    CNewUIPurchaseShopInventory *GetUI_NewPurchaseShopInventory() const;
    CNewUICharacterInfoWindow *GetUI_NewCharacterInfoWindow() const;
    CNewUIMyQuestInfoWindow *GetUI_NewMyQuestInfoWindow() const;
    CNewUIPartyListWindow *GetUI_NewPartyListWindow() const;
    CNewUINPCQuest *GetUI_NewNPCQuest() const;
    CNewUIEnterBloodCastle *GetUI_NewEnterBloodCastle() const;
    CNewUIEnterDevilSquare *GetUI_NewEnterDevilSquare() const;
    CNewUIBloodCastle *GetUI_NewBloodCastle() const;
    CNewUITrade *GetUI_NewTrade() const;
    CNewUIKanturu2ndEnterNpc *GetUI_NewKanturu2ndEnterNpc() const;
    CNewUIKanturuInfoWindow *GetUI_NewKanturuInfoWindow() const;
    CNewUICatapultWindow *GetUI_NewCatapultWindow() const;
    CNewUIChaosCastleTime *GetUI_NewChaosCastleTime() const;
    CNewUIBattleSoccerScore *GetUI_NewBattleSoccerScore() const;
    CNewUICommandWindow *GetUI_NewCommandWindow() const;
    CNewUIHeroPositionInfo *GetUI_NewHeroPositionInfo() const;
    CNewUIWindowMenu *GetUI_NewWindowMenu() const;
    CNewUIOptionWindow *GetUI_NewOptionWindow() const;
    CNewUIHelpWindow *GetUI_NewHelpWindow() const;
    CNewUIItemExplanationWindow *GetUI_NewItemExplanationWindow() const;
    CNewUISetItemExplanation *GetUI_NewSetItemExplanation() const;
    CNewUIQuickCommandWindow *GetUI_NewQuickCommandWindow() const;
    CNewUIMoveCommandWindow *GetUI_NewMoveCommandWindow() const;
    CNewUIDuelWindow *GetUI_NewDuelWindow() const;
    CNewUISiegeWarfare *GetUI_NewSiegeWarfare() const;
    CNewUIItemEnduranceInfo *GetUI_NewItemEnduranceInfo() const;
    CNewUIBuffWindow *GetUI_NewBuffWindow() const;
    CNewUICursedTempleEnter *GetUI_NewCursedTempleEnterWindow() const;
    CNewUICursedTempleSystem *GetUI_NewCursedTempleWindow() const;
    CNewUICursedTempleResult *GetUI_NewCursedTempleResultWindow() const;
    CNewUICryWolf *GetUI_NewCryWolfInterface() const;
    CNewUIMasterLevel *GetUI_NewMasterLevelInterface() const;
    CNewUIGoldBowmanWindow *GetUI_pNewGoldBowman() const;
    CNewUIGoldBowmanLena *GetUI_pNewGoldBowmanLena() const;
    CNewUIRegistrationLuckyCoin *GetUI_pNewLuckyCoinRegistration() const;
    CNewUIExchangeLuckyCoin *GetUI_pNewExchangeLuckyCoin() const;
    CNewUIDuelWatchWindow *GetUI_pNewDuelWatch() const;
    CNewUIDuelWatchMainFrameWindow *GetUI_pNewDuelWatchMainFrame() const;
    CNewUIDuelWatchUserListWindow *GetUI_pNewDuelWatchUserList() const;
#ifdef PBG_ADD_INGAMESHOP_UI_MAINFRAME
    CNewUIInGameShop *GetUI_pNewInGameShop() const;
#endif //PBG_ADD_INGAMESHOP_UI_MAINFRAME
    CNewUIDoppelGangerWindow *GetUI_pNewDoppelGangerWindow() const;
    CNewUIDoppelGangerFrame *GetUI_pNewDoppelGangerFrame() const;
    CNewUINPCDialogue *GetUI_NewNPCDialogue() const;
    CNewUIQuestProgress *GetUI_NewQuestProgress() const;
    CNewUIQuestProgressByEtc *GetUI_NewQuestProgressByEtc() const;
    CNewUINameWindow *GetUI_NewNameWindow() const
    {
        return m_pNewNameWindow;
    }
    CNewUIEmpireGuardianNPC *GetUI_pNewEmpireGuardianNPC() const;
    CNewUIEmpireGuardianTimer *GetUI_pNewEmpireGuardianTimer() const;
    CNewUIMiniMap *GetUI_pNewUIMiniMap() const;
    CNewUIGensRanking *GetUI_NewGensRanking() const;
    CNewUIUnitedMarketPlaceWindow *GetUI_pNewUnitedMarketPlaceWindow() const;
    //CNewUIUnitedMarketPlaceWindow*	GetUI_pNewUnitedMarketPlaceFrame() const;
    CNewUILuckyItemWnd *Get_pNewUILuckyItemWnd() const;
    CNewUIMuHelper *Get_pNewUIMuHelper() const;
    CNewUIMuHelperSkillList *Get_pNewUIMuHelperSkillList() const;
};
} // namespace SEASON3B

#define g_pNewUIMng g_pNewUISystem->GetNewUIManager()
#define g_pNewUI3DRenderMng g_pNewUISystem->GetNewUI3DRenderMng()
#define g_pNewUIHotKey g_pNewUISystem->GetNewUIHotKey()
#define g_pNewItemMng ItemStore()
#define g_pChatInputBox g_pNewUISystem->GetUI_NewChatInputBox()
#define g_pChatListBox g_pNewUISystem->GetUI_NewChatLogWindow()
#define g_pSystemLogBox g_pNewUISystem->GetUI_NewSystemLogWindow()
#define g_pSlideHelpMgr g_pNewUISystem->GetUI_NewSlideWindow()
#define g_pWindowMgr g_pNewUISystem->GetUI_NewFriendWindow()
#define g_pMainFrame g_pNewUISystem->GetUI_NewMainFrameWindow()
#define g_pSkillList g_pNewUISystem->GetUI_NewSkillList()
#define g_pMyInventory g_pNewUISystem->GetUI_NewMyInventory()
#define g_pMyInventoryExt g_pNewUISystem->GetUI_NewMyInventoryExt()
#define g_pNPCShop g_pNewUISystem->GetUI_NewNpcShop()
#define g_pPetInfoWindow g_pNewUISystem->GetUI_NewPetInfoWindow()
#define g_pMixInventory g_pNewUISystem->GetUI_NewMixInventory()
#define g_pCastleWindow g_pNewUISystem->GetUI_NewCastleWindow()
#define g_pGuardWindow g_pNewUISystem->GetUI_NewGuardWindow()
#define g_pGatemanWindow g_pNewUISystem->GetUI_NewGatemanWindow()
#define g_pGateSwitchWindow g_pNewUISystem->GetUI_NewGateSwitchWindow()
#define g_pStorageInventory g_pNewUISystem->GetUI_NewStorageInventory()
#define g_pStorageInventoryExt g_pNewUISystem->GetUI_NewStorageInventoryExt()
#define g_pGuildMakeWindow g_pNewUISystem->GetUI_NewGuildMakeWindow()
#define g_pGuildInfoWindow g_pNewUISystem->GetUI_NewGuildInfoWindow()
#define g_pMyShopInventory g_pNewUISystem->GetUI_NewMyShopInventory()
#define g_pPurchaseShopInventory g_pNewUISystem->GetUI_NewPurchaseShopInventory()
#define g_pCharacterInfoWindow g_pNewUISystem->GetUI_NewCharacterInfoWindow()
#define g_pMyQuestInfoWindow g_pNewUISystem->GetUI_NewMyQuestInfoWindow()
#define g_pPartyListWindow g_pNewUISystem->GetUI_NewPartyListWindow()
#define g_pNPCQuest g_pNewUISystem->GetUI_NewNPCQuest()
#define g_pEnterBloodCastle g_pNewUISystem->GetUI_NewEnterBloodCastle()
#define g_pEnterDevilSquare g_pNewUISystem->GetUI_NewEnterDevilSquare()
#define g_pBloodCastle g_pNewUISystem->GetUI_NewBloodCastle()
#define g_pTrade g_pNewUISystem->GetUI_NewTrade()
#define g_pKanturu2ndEnterNpc g_pNewUISystem->GetUI_NewKanturu2ndEnterNpc()
#define g_pKanturuInfoWindow g_pNewUISystem->GetUI_NewKanturuInfoWindow()
#define g_pCatapultWindow g_pNewUISystem->GetUI_NewCatapultWindow()
#define g_pChaosCastleTime g_pNewUISystem->GetUI_NewChaosCastleTime()
#define g_pBattleSoccerScore g_pNewUISystem->GetUI_NewBattleSoccerScore()
#define g_pCommandWindow g_pNewUISystem->GetUI_NewCommandWindow()
#define g_pWindowMenu g_pNewUISystem->GetUI_NewWindowMenu()
#define g_pOption g_pNewUISystem->GetUI_NewOptionWindow()
#define g_pHeroPositionInfo g_pNewUISystem->GetUI_NewHeroPositionInfo()
#define g_pHelp g_pNewUISystem->GetUI_NewHelpWindow()
#define g_pItemExplanation g_pNewUISystem->GetUI_NewItemExplanationWindow()
#define g_pSetItemExplanation g_pNewUISystem->GetUI_NewSetItemExplanation()
#define g_pQuickCommand g_pNewUISystem->GetUI_NewQuickCommandWindow()
#define g_pMoveCommandWindow g_pNewUISystem->GetUI_NewMoveCommandWindow()
#define g_pDuelWindow g_pNewUISystem->GetUI_NewDeulWindow()
#define g_pSiegeWarfare g_pNewUISystem->GetUI_NewSiegeWarfare()
#define g_pItemEnduranceInfo g_pNewUISystem->GetUI_NewItemEnduranceInfo()
#define g_pBuffWindow g_pNewUISystem->GetUI_NewBuffWindow()
#define g_pCursedTempleEnterWindow g_pNewUISystem->GetUI_NewCursedTempleEnterWindow()
#define g_pCursedTempleWindow g_pNewUISystem->GetUI_NewCursedTempleWindow()
#define g_pCursedTempleResultWindow g_pNewUISystem->GetUI_NewCursedTempleResultWindow()
#define g_pCryWolfInterface g_pNewUISystem->GetUI_NewCryWolfInterface()
#define g_pMasterLevelInterface g_pNewUISystem->GetUI_NewMasterLevelInterface()
#define g_pGoldBowmanInterface g_pNewUISystem->GetUI_pNewGoldBowman()
#define g_pGoldBowmanLenaInterface g_pNewUISystem->GetUI_pNewGoldBowmanLena()
#define g_pLuckyCoinRegistration g_pNewUISystem->GetUI_pNewLuckyCoinRegistration()
#define g_pExchangeLuckyCoinWindow g_pNewUISystem->GetUI_pNewExchangeLuckyCoin()
#define g_pDuelWatchWindow g_pNewUISystem->GetUI_pNewDuelWatch()
#define g_pDuelWatchMainFrameWindow g_pNewUISystem->GetUI_pNewDuelWatchMainFrame()
#define g_pDuelWatchUserList g_pNewUISystem->GetUI_pNewDuelWatchUserList()
#ifdef PBG_ADD_INGAMESHOP_UI_MAINFRAME
#define g_pInGameShop g_pNewUISystem->GetUI_pNewInGameShop()
#endif //PBG_ADD_INGAMESHOP_UI_MAINFRAME
#define g_pDoppelGangerWindow g_pNewUISystem->GetUI_pNewDoppelGangerWindow()
#define g_pDoppelGangerFrame g_pNewUISystem->GetUI_pNewDoppelGangerFrame()
#define g_pNPCDialogue g_pNewUISystem->GetUI_NewNPCDialogue()
#define g_pQuestProgress g_pNewUISystem->GetUI_NewQuestProgress()
#define g_pQuestProgressByEtc g_pNewUISystem->GetUI_NewQuestProgressByEtc()
#define g_pEmpireGuardianNPC g_pNewUISystem->GetUI_pNewEmpireGuardianNPC()
#define g_pEmpireGuardianTimer g_pNewUISystem->GetUI_pNewEmpireGuardianTimer()
#define g_pNewUIMiniMap g_pNewUISystem->GetUI_pNewUIMiniMap()
#ifdef PBG_MOD_STAMINA_UI
#define g_pNewUIStamina g_pNewUISystem->GetUI_pNewUIStamina()
#endif //PBG_MOD_STAMINA_UI
#define g_pNewUIGensRanking g_pNewUISystem->GetUI_NewGensRanking()
#define g_pLuckyItemWnd g_pNewUISystem->Get_pNewUILuckyItemWnd()
#define g_pNewUIMuHelper g_pNewUISystem->Get_pNewUIMuHelper()
#define g_pNewUIMuHelperSkillList g_pNewUISystem->Get_pNewUIMuHelperSkillList()

namespace SEASON3B
{
enum INTERFACE_LIST
{
    INTERFACE_BEGIN = 0x00,
    INTERFACE_FRIEND,
    INTERFACE_MOVEMAP,
    INTERFACE_PARTY,
    INTERFACE_MYQUEST,
    INTERFACE_NPCQUEST,
    INTERFACE_GUILDINFO,
    INTERFACE_TRADE,
    INTERFACE_STORAGE,
    INTERFACE_STORAGE_EXT,
    INTERFACE_MIXINVENTORY,
    INTERFACE_COMMAND,
    INTERFACE_PET,
    INTERFACE_NPCSHOP,
    INTERFACE_INVENTORY,
    INTERFACE_INVENTORY_EXT,
    INTERFACE_MYSHOP_INVENTORY,
    INTERFACE_PURCHASESHOP_INVENTORY,
    INTERFACE_CHARACTER,
    INTERFACE_NPCBREEDER,
    INTERFACE_SERVERDIVISION,
    INTERFACE_DEVILSQUARE,
    INTERFACE_BLOODCASTLE,
    INTERFACE_NPCGUILDMASTER,
    INTERFACE_GUARDSMAN,
    INTERFACE_SENATUS,
    INTERFACE_GATEKEEPER,
    INTERFACE_GATESWITCH,
    INTERFACE_CATAPULT,
    INTERFACE_REFINERY,
    INTERFACE_REFINERYINFO,
    INTERFACE_KANTURU2ND_ENTERNPC,
    INTERFACE_CURSEDTEMPLE_NPC,
    INTERFACE_CURSEDTEMPLE_GAMESYSTEM,
    INTERFACE_CURSEDTEMPLE_RESULT,
    INTERFACE_CHATINPUTBOX,
    INTERFACE_WINDOW_MENU,
    INTERFACE_OPTION,
    INTERFACE_HELP,
    INTERFACE_ITEM_EXPLANATION,
    INTERFACE_SETITEM_EXPLANATION,
    INTERFACE_QUICK_COMMAND,
    INTERFACE_KANTURU_INFO,
    INTERFACE_CHATLOGWINDOW,
    INTERFACE_PARTY_INFO_WINDOW,
    INTERFACE_BLOODCASTLE_TIME,
    INTERFACE_CHAOSCASTLE_TIME,
    INTERFACE_BATTLE_SOCCER_SCORE,
    INTERFACE_SLIDEWINDOW,
    INTERFACE_HERO_POSITION_INFO,
    INTERFACE_MESSAGEBOX,
    INTERFACE_DUEL_WINDOW,
    INTERFACE_CRYWOLF,
    INTERFACE_NAME_WINDOW,
    INTERFACE_SIEGEWARFARE,
    INTERFACE_MAINFRAME,
    INTERFACE_SKILL_LIST,
    INTERFACE_ITEM_ENDURANCE_INFO,
    INTERFACE_BUFF_WINDOW,
    INTERFACE_MASTER_LEVEL,
    INTERFACE_GOLD_BOWMAN,
    INTERFACE_GOLD_BOWMAN_LENA,
    INTERFACE_LUCKYCOIN_REGISTRATION,
    INTERFACE_EXCHANGE_LUCKYCOIN,
    INTERFACE_DUELWATCH,
    INTERFACE_DUELWATCH_MAINFRAME,
    INTERFACE_DUELWATCH_USERLIST,
    INTERFACE_INGAMESHOP,
    INTERFACE_DOPPELGANGER_NPC,
    INTERFACE_DOPPELGANGER_FRAME,
    INTERFACE_QUEST_PROGRESS,
    INTERFACE_QUEST_PROGRESS_ETC,
    INTERFACE_EMPIREGUARDIAN_NPC,
    INTERFACE_EMPIREGUARDIAN_TIMER,
    INTERFACE_MINI_MAP,
    INTERFACE_NPC_DIALOGUE,
    INTERFACE_GENSRANKING,
    INTERFACE_UNITEDMARKETPLACE_NPC_JULIA,
    INTERFACE_LUCKYITEMWND,
    INTERFACE_HOTKEY,
    INTERFACE_3DRENDERING_CAMERA_BEGIN,
    INTERFACE_3DRENDERING_CAMERA_END = INTERFACE_3DRENDERING_CAMERA_BEGIN + 24,
    INTERFACE_ITEM_TOOLTIP,
    INTERFACE_MUHELPER,
    INTERFACE_MUHELPER_EXT,
    INTERFACE_MUHELPER_SKILL_LIST,
    INTERFACE_SYSTEMLOGWINDOW,
    INTERFACE_END,
    INTERFACE_COUNT = INTERFACE_END - 2,
};
} // namespace SEASON3B

enum
{
    MESSAGE_FREE_MSG_NOT_BTN = 91,
    MESSAGE_GAME_END_COUNTDOWN,
    MESSAGE_WAIT = 93,
    MESSAGE_DELETE_CHARACTER_CONFIRM,
    MESSAGE_DELETE_CHARACTER_RESIDENT,
    MESSAGE_DELETE_CHARACTER_SUCCESS,
    MESSAGE_DELETE_CHARACTER_ID_BLOCK,
    MESSAGE_DELETE_CHARACTER_ITEM_BLOCK,
    MESSAGE_SERVER_BUSY,
    MESSAGE_INPUT_ID = 100,
    MESSAGE_INPUT_PASSWORD,
    MESSAGE_INPUT_CONFIRM,
    MESSAGE_INPUT_QUIZ,
    MESSAGE_INPUT_ANSWER,
    MESSAGE_INPUT_NAME,
    MESSAGE_INPUT_RESIDENT_NUMBER,
    MESSAGE_INPUT_PHONE,
    MESSAGE_INPUT_EMAIL,
    MESSAGE_PASSWORD,
    MESSAGE_LOG_OUT,
    MESSAGE_VERSION,
    MESSAGE_SERVER_LOST,
    MESSAGE_DELETE_CHARACTER,
    MESSAGE_ID_SPACE_ERROR,
    MESSAGE_INPUT_GOLD,
    MESSAGE_NOT_ENOUGH_GOLD,
    MESSAGE_OVERFLOW_GOLD,
    MESSAGE_GUILD,
    MESSAGE_PARTY,
    MESSAGE_TRADE,
    MESSAGE_MIN_LENGTH,
    MESSAGE_SPECIAL_NAME,
    MESSAGE_RESIDENT_NUMBER_ERROR,
    MESSAGE_INPUT_GUILD,
    MESSAGE_DELETE_GUILD,
    MESSAGE_NO_GUILD_MARK,
    MESSAGE_GUILD_WAR,
    MESSAGE_HOMEPAGE1,
    MESSAGE_HOMEPAGE2,
    MESSAGE_HOMEPAGE3,
    MESSAGE_DELETE_CHARACTER_WARNING,
    MESSAGE_BLOCKED_CHARACTER,
    MESSAGE_STORAGE_PASSWORDWRONG,
    MESSAGE_STORAGE_ALREADYLOCKED,
    MESSAGE_STORAGE_PASSWORDTOOSIMPLE,
    MESSAGE_STORAGE_PASSWORDNOTMATCH,
    MESSAGE_STORAGE_RESIDENTWRONG,
    MESSAGE_CUSTOM,
    MESSAGE_DEVILSQUARERANK,
    MESSAGE_SEMI_QUEST,
    MESSAGE_DIALOG,
    MESSAGE_SELECT_CHAOS,
    MESSAGE_SERVER_IMMIGR_ERROR_RESIDENT,
    MESSAGE_SERVER_IMMIGR_ERROR_DB,

    MESSAGE_PERSONALSHOP,
    MESSAGE_DELETE_CHARACTER_GUILDWARNING,

    MESSAGE_OPTIONS,
    MESSAGE_TRADE_CHECK,
    MESSAGE_LOCK_STORAGE,
    MESSAGE_USE_STATE,
    MESSAGE_BLOODCASTLE_RESULT,
    MESSAGE_CHECK,
    MESSAGE_PERSONALSHOP_WARNING,

    MESSAGE_CHAOS_CASTLE_CHECK,
    MESSAGE_CHAOS_CASTLE_RESULT,
    MESSAGE_CUSTOM_MESSAGEBOX,
    MESSAGE_GEM_INTEGRATION,
    MESSAGE_GEM_INTEGRATION2,
    MESSAGE_GEM_INTEGRATION3,
    MESSAGE_GEM_INTEGRATION4,
    MESSAGE_GEM_INTEGRATION5,
    MESSAGE_GEM_INTEGRATION6,
    MESSAGE_WT_MATCH_RESULT,
    MESSAGE_USE_STATE2,
    MESSAGE_FENRIR_REPAIR,
    MESSAGE_CANCEL_SKILL,
    MESSAGE_PCROOM_EVENT,
    MESSAGE_WHITEANGEL_EVENT,
    MESSAGE_3COLORHARVEST_EVENT,
};

namespace UiSystemDetail
{
using namespace SEASON3B;
#pragma pack(push)
#pragma pack()
inline constexpr int kLayoutBaseX = 640;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kLayoutPanelWidth = 190;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
constexpr int PanelColumnX(int columns)
{
    return kLayoutBaseX - (kLayoutPanelWidth * columns);
}
#pragma pack(pop)

} // namespace UiSystemDetail

BOOL ShowCheckBox(int num, int index, int message = MESSAGE_TRADE_CHECK);
