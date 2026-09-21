#pragma once
#include "render/UiAdapter.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include "support/ExternalObject.h"
#include "ui/features/Activities/ActivitiesRender.h"
#include "ui/features/Dialogs/DialogsRender.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"
#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <string>

// Native window controllers grouped by feature.
#pragma pack(push)
#pragma pack()
class CUITextInputWindow : public CUIBaseWindow
{
  public:
    explicit CUITextInputWindow(SessionKeeper &keeper)
        : CUIBaseWindow(keeper), m_dwReturnWindowUIID(0), m_TextInputBox(keeper),
          m_AddButton(keeper), m_CancelButton(keeper), m_ModernPanel(keeper)
    {
    }
    virtual ~CUITextInputWindow()
    {
    }

    virtual void Init(const wchar_t *pszTitle, DWORD dwParentID = 0);
    virtual void Refresh();
    void SetText(const wchar_t *pszText)
    {
        m_TextInputBox.SetText(pszText);
        m_TextInputBox.GiveFocus(TRUE);
    }
    bool PrepareModernUiOnWorker(int, int, bool) override;
    bool ProcessModernUiInput(const SessionInputEvent &) override;
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const override;
    bool RecordModernUi(LegacyRenderFacade &) const override;
    void ApplyModernUiChanges() override;

  protected:
    void InitControls() override;
    virtual void RenderSub();
    virtual BOOL HandleMessage();
    virtual void DoActionSub(BOOL bMessageOnly);
    virtual void DoMouseActionSub();

    void ReturnText();

  protected:
    DWORD m_dwReturnWindowUIID;
    CUITextInputBox m_TextInputBox;
    CUIButton m_AddButton;
    CUIButton m_CancelButton;
    UI::Modern::RmlMessageBoxPanel m_ModernPanel;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
class CUIQuestionWindow : public CUIBaseWindow
{
  public:
    CUIQuestionWindow(SessionKeeper &keeper, int iDialogType = 0)
        : CUIBaseWindow(keeper), m_dwReturnWindowUIID(0), m_iDialogType(iDialogType),
          m_AddButton(keeper), m_CancelButton(keeper), m_ModernPanel(keeper)
    {
    }
    virtual ~CUIQuestionWindow()
    {
    }

    virtual void Init(const wchar_t *pszTitle, DWORD dwParentID = 0);
    virtual void Refresh();

    void SaveID(const wchar_t *pszText);
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

  protected:
    DWORD m_dwReturnWindowUIID;
    int m_iDialogType; // 0: Y/N, 1: OK
    wchar_t m_szCaption[2][MAX_TEXT_LENGTH + 1];
    wchar_t m_szSaveID[MAX_USERNAME_SIZE + 1];
    CUIButton m_AddButton;
    CUIButton m_CancelButton;
    UI::Modern::RmlMessageBoxPanel m_ModernPanel;
};
#pragma pack(pop)

#define SLIDE_LEVEL_MAX 5
#define SLIDE_TEXT_LENGTH 1024

struct SLIDEHELPTEXT
{
    int iLevel;
    int iNumber;
    char szSlideHelpText[32][256];
};

struct SLIDEHELP
{
    int iCreateDelay;
    float fSpeed;
    SLIDEHELPTEXT SlideHelp[SLIDE_LEVEL_MAX];
};

struct SLIDE_QUEUE_DATA
{
    int type;
    std::wstring text;
    DWORD color;
};
using SLIDE_QUEUE = std::multimap<DWORD, SLIDE_QUEUE_DATA>;

class CErrorReport;
class CUISlideHelp : protected SessionUiLegacyBindings
{
  public:
    explicit CUISlideHelp(SessionKeeper &keeper);
    void Init(DWORD displayMilliseconds);
    void Update(bool paused = false);
    BOOL HaveText() const
    {
        return text_.empty();
    }
    const std::wstring &Text() const
    {
        return text_;
    }
    DWORD Color() const
    {
        return color_;
    }
    void AddSlide(int count, int delay, const wchar_t *text, int type, float speed, DWORD color);
    void ManageSlide();

  private:
    friend class PresentationSeparationTestPeer;
    SLIDE_QUEUE queue_;
    std::wstring text_;
    DWORD color_ = 0;
    DWORD displayMilliseconds_ = 0, remainingMilliseconds_ = 0;
    DWORD previousUpdate_ = 0, startedAt_ = 0;
};

class CSlideHelpMgr : protected SessionUiLegacyBindings
{
  public:
    explicit CSlideHelpMgr(SessionKeeper &keeper);
    virtual ~CSlideHelpMgr();

    void Init();
    void Render();

    void CreateSlideText();
    void OpenSlideTextFile(const wchar_t *szFileName);
    void ClearSlideText();
    const wchar_t *GetSlideText(int iLevel);
    void SetCreateDelay(int iDelay)
    {
        m_iCreateDelay = iDelay;
    }

    void AddSlide(int iLoopCount, int iLoopDelay, const wchar_t *pszText, int iType, float fSpeed,
                  DWORD dwTextColor = (255 << 24) + (200 << 16) + (220 << 8) + (230));
    void ManageSlide();
    BOOL IsIdle();
    bool PrepareModernUiOnWorker(int width, int height);

  protected:
    SessionRenderUnit &renderer_;
    UI::Modern::PC::Inventory::RmlItemDialogPanel panel_;
    UI::Modern::RmlUiDesign design_;
    bool visible_ = false;
    CUISlideHelp m_HelpSlide;
    CUISlideHelp m_NoticeSlide;

    int m_iCreateDelay;
    int m_iLevelCap[SLIDE_LEVEL_MAX];
    int m_iTextNumber[SLIDE_LEVEL_MAX];
    std::list<wchar_t *> m_SlideTextList[SLIDE_LEVEL_MAX];
    std::list<wchar_t *>::iterator m_SlideTextListIter;
    float m_fHelpSlideSpeed;
};
#define MW_MSG_LINE_MAX 2
#define MW_MSG_ROW_MAX 52

//  UIPopup.h

#define POPUP_CUSTOM 0
#define POPUP_OK 1
#define POPUP_OKCANCEL 2
#define POPUP_YESNO 4
#define POPUP_TIMEOUT 8
#define POPUP_INPUT 16

typedef int POPUP_RESULT;
#define POPUP_RESULT_NONE 1
#define POPUP_RESULT_OK 2
#define POPUP_RESULT_CANCEL 4
#define POPUP_RESULT_YES 8
#define POPUP_RESULT_NO 16
#define POPUP_RESULT_TIMEOUT 32
#define POPUP_RESULT_ESC 64

enum POPUP_ALIGN
{
    PA_CENTER,
    PA_TOP
};

#define MAX_POPUP_TEXTLINE 10
#define MAX_POPUP_TEXTLENGTH 256

class CUIPopup : protected SessionUiLegacyBindings
{
  public:
    explicit CUIPopup(SessionKeeper &keeper);
    virtual ~CUIPopup();

  protected:
    std::function<int(POPUP_RESULT)> PopupResultFuncPointer;
    std::function<void()> PopupUpdateInputFuncPointer;
    std::function<void()> PopupRenderFuncPointer;

    DWORD m_dwPopupID;
    int m_nPopupTextCount;
    wchar_t m_szPopupText[MAX_POPUP_TEXTLINE][MAX_POPUP_TEXTLENGTH];
    int m_PopupType;
    SIZE m_sizePopup;
    POPUP_ALIGN m_Align;

    wchar_t m_szInputText[1024];
    int m_nInputSize;
    int m_nInputTextLength;
    UIOPTIONS m_InputOptions;

    DWORD m_dwPopupStartTime;
    DWORD m_dwPopupEndTime;
    DWORD m_dwPopupElapseTime;

    CUIButton m_OkButton;
    CUIButton m_CancelButton;
    CUIButton m_YesButton;
    CUIButton m_NoButton;

  protected:
    bool CheckTimeOut();

  public:
    void Init();
    DWORD SetPopup(const wchar_t *pszText, int nLineCount, int nBufferSize, int Type,
                   std::function<int(POPUP_RESULT)> resultFunc, POPUP_ALIGN Align = PA_CENTER);
    void SetPopupExtraFunc(std::function<void()> inputFunc, std::function<void()> renderFunc);
    void SetInputMode(int nSize, int nTextLength, UIOPTIONS Options);
    bool IsInputEnable();
    void SetTimeOut(DWORD dwElapseTime);
    wchar_t *GetInputText();
    DWORD GetPopupID();
    void Close();
    void CancelPopup();
    bool PressKey(int nKey);
    void UpdateInput();
    void Render();
};

class CmuConsoleDebug;

class MapProcess;
class SessionGameplayUnit;
class SessionUiUnit;

#pragma warning(disable : 4786)

class SessionKeeper;
struct SessionInputEvent;
namespace UI::Modern
{
class RmlMessageBoxPanel;
}
namespace UI::Modern::PC::SystemMenu
{
class RmlSystemMenuPanel;
}

namespace SEASON3B
{
enum CALLBACK_RESULT
{
    CALLBACK_CONTINUE = 0,
    CALLBACK_BREAK,
    CALLBACK_EXCEPTION,
    CALLBACK_POP_ALL_EVENTS,
};
using EVENT_CALLBACK =
    std::function<CALLBACK_RESULT(class CNewUIMessageBoxBase *, const leaf::xstreambuf &)>;

enum
{
    MSGBOX_EVENT_NONE = 0,
    MSGBOX_EVENT_DESTROY,
    MSGBOX_EVENT_MOUSE_HOVER,
    MSGBOX_EVENT_MOUSE_LBUTTON_DOWN,
    MSGBOX_EVENT_MOUSE_LBUTTON_UP,
    MSGBOX_EVENT_MOUSE_RBUTTON_DOWN,
    MSGBOX_EVENT_MOUSE_RBUTTON_UP,
    MSGBOX_EVENT_PRESSKEY_ESC,
    MSGBOX_EVENT_PRESSKEY_RETURN,
    MSGBOX_EVENT_USER_DEFINE = 0x0F00,
    // common msgbox
    MSGBOX_EVENT_USER_COMMON_OK,
    MSGBOX_EVENT_USER_COMMON_CANCEL,
    // keypad
    MSGBOX_EVENT_USER_CUSTOM_KEYPAD_INPUT,
    MSGBOX_EVENT_USER_CUSTOM_KEYPAD_DELETE,
    // use fruit
    MSGBOX_EVENT_USER_CUSTOM_USE_FRUIT_ADD,
    MSGBOX_EVENT_USER_CUSTOM_USE_FRUIT_MINUS,
    // gem interation
    MSGBOX_EVENT_USER_CUSTOM_GEM_UNITY,
    MSGBOX_EVENT_USER_CUSTOM_GEM_DISJOINT,
    MSGBOX_EVENT_USER_CUSTOM_GEM_UNITY_BLESSING,
    MSGBOX_EVENT_USER_CUSTOM_GEM_UNITY_SOUL,
    MSGBOX_EVENT_USER_CUSTOM_GEM_UNITY_TEN,
    MSGBOX_EVENT_USER_CUSTOM_GEM_UNITY_TWENTY,
    MSGBOX_EVENT_USER_CUSTOM_GEM_UNITY_THIRTY,
    MSGBOX_EVENT_USER_CUSTOM_GEM_DISJOINT_BLESSING,
    MSGBOX_EVENT_USER_CUSTOM_GEM_DISJOINT_SOUL,
    MSGBOX_EVENT_USER_CUSTOM_GEM_DISJOINT_DISJOINT,
    // system menu
    MSGBOX_EVENT_USER_CUSTOM_SYSTEMMENU_GAMEOVER,
    MSGBOX_EVENT_USER_CUSTOM_SYSTEMMENU_CHOOSESERVER,
    MSGBOX_EVENT_USER_CUSTOM_SYSTEMMENU_CHOOSECHARACTER,
    MSGBOX_EVENT_USER_CUSTOM_SYSTEMMENU_OPTION,
    // mix menu
    MSGBOX_EVENT_USER_CUSTOM_MIXMENU_GENERALMIX,
    MSGBOX_EVENT_USER_CUSTOM_MIXMENU_CHAOSMIX,
    MSGBOX_EVENT_USER_CUSTOM_MIXMENU_MIX380,
    // trainer menu
    MSGBOX_EVENT_USER_CUSTOM_TRAINER_MENU_RECOVER,
    MSGBOX_EVENT_USER_CUSTOM_TRAINER_MENU_REVIVE,
    MSGBOX_EVENT_USER_CUSTOM_TRAINER_MENU_RECOVER_DARKSPRIT,
    MSGBOX_EVENT_USER_CUSTOM_TRAINER_MENU_RECOVER_DARKHORSE,
    // elpis menu
    MSGBOX_EVENT_USER_CUSTOM_ELPIS_ABOUT_REFINARY,
    MSGBOX_EVENT_USER_CUSTOM_ELPIS_ABOUT_JEWELOFHARMONY,
    MSGBOX_EVENT_USER_CUSTOM_ELPIS_REFINE,
    // dialog
    MSGBOX_EVENT_USER_CUSTOM_DIALOG_END,
    // progress
    MSGBOX_EVENT_USER_CUSTOM_PROGRESS_CLOSINGPROCESS,
    MSGBOX_EVENT_USER_CUSTOM_PROGRESS_COMPLETEPROCESS,
    // duel
    MSGBOX_EVENT_USER_CUSTOM_DUEL_OK,
    MSGBOX_EVENT_USER_CUSTOM_DUEL_CANCEL,
    MSGBOX_EVENT_USER_CUSTOM_CB_WHITE,
    MSGBOX_EVENT_USER_CUSTOM_CB_RED,
    MSGBOX_EVENT_USER_CUSTOM_CB_GOLD,
    // seed master menu
    MSGBOX_EVENT_USER_CUSTOM_SEED_MASTER_MENU_EXTRACT_SEED,
    MSGBOX_EVENT_USER_CUSTOM_SEED_MASTER_MENU_SEED_SPHERE,
    // seed investigator menu
    MSGBOX_EVENT_USER_CUSTOM_SEED_INVESTIGATOR_MENU_ATTACH_SOCKET,
    MSGBOX_EVENT_USER_CUSTOM_SEED_INVESTIGATOR_MENU_DETACH_SOCKET,

    MSGBOX_EVENT_USER_CUSTOM_RESET_CHARACTER_POINT,

    MSGBOX_EVENT_USER_CUSTOM_DELGARDO_REGISTRATION_LUCKY_COIN,
    MSGBOX_EVENT_USER_CUSTOM_DELGARDO_EXCHANGE_LUCKY_COIN,

    MSGBOX_EVENT_USER_CUSTOM_INGAMESHOP_PRESENT,

    MSGBOX_EVENT_USER_CUSTOM_LUCKYITEM_TRADE,
    MSGBOX_EVENT_USER_CUSTOM_LUCKYITEM_REFINERY,

    MSGBOX_EVENT_USER_CUSTOM_GEM_SELECTMIX,
    MSGBOX_EVENT_USER_CUSTOM_GEM_SELECT,
};

class CNewUIManager;

class CNewUIMessageBoxBase : protected SessionUiLegacyBindings
{
    typedef std::map<DWORD, EVENT_CALLBACK> type_map_callback;

    POINT m_Pos;
    SIZE m_Size;
    float m_fPriority;
    bool m_bCanMove;

    type_map_callback m_mapCallbacks;

    float m_fOpacityAlpha;
    vec3_t m_vColor;
    MapProcess &mapProcess_;

  protected:
    SessionGameplayUnit &gameplay_;
    SessionUiUnit &sessionUi_;
    template <class _M>
    EVENT_CALLBACK BindSelf(CALLBACK_RESULT (_M::*callback)(CNewUIMessageBoxBase *,
                                                            const leaf::xstreambuf &))
    {
        return
            [this, callback](CNewUIMessageBoxBase *messageBox, const leaf::xstreambuf &parameter) {
                return (static_cast<_M *>(this)->*callback)(messageBox, parameter);
            };
    }

  public:
    explicit CNewUIMessageBoxBase(SessionKeeper &keeper);
    virtual ~CNewUIMessageBoxBase();

    virtual bool Create(int x, int y, int width, int height, float fPriority = 3.f);
    virtual void Release();

    virtual void SetPos(int x, int y);
    void SetSize(int width, int height);
    const POINT &GetPos();
    const SIZE &GetSize();
    void SetCanMove(bool bCanMove);
    bool CanMove();

    float GetPriority() const;

    void AddCallbackFunc(EVENT_CALLBACK pFunc, DWORD dwEvent);
    void RemoveCallbackFunc(DWORD dwEvent);
    void RemoveAllCallbackFuncs();

    EVENT_CALLBACK GetCallbackFunc(DWORD dwEvent);

    virtual bool Update() = 0;
    virtual bool Render() = 0;
    virtual bool PrepareModernUiOnWorker(int, int)
    {
        return true;
    }
    virtual bool ProcessModernUiInput(const SessionInputEvent &)
    {
        return false;
    }
    virtual std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const
    {
        return std::nullopt;
    }

    SessionKeeper &MessageBoxSessionOrigin() const noexcept
    {
        return SessionOrigin();
    }
    MapProcess &MessageBoxMapProcessOrigin() noexcept
    {
        return mapProcess_;
    }
    SessionUiUnit &MessageBoxUiOrigin() noexcept
    {
        return sessionUi_;
    }

    bool ShowOkMessageBox(const std::wstring &message);

    void SendEvent(CNewUIMessageBoxBase *pOwner, DWORD dwEvent);
    void SendEvent(CNewUIMessageBoxBase *pOwner, DWORD dwEvent, const leaf::xstreambuf &xParam);
    void RenderMsgBackColor(bool _bRender = false);
    void SetMsgBackOpacity(float _fAlpha = 0.5f);
    const float &GetMsgBackOpacity() const
    {
        return m_fOpacityAlpha;
    }
    void SetMsgBackColor(vec3_t _vColor = NULL);
    const vec3_t &GetMsgBackColor() const
    {
        return m_vColor;
    }
};

class CNewUIMessageBoxFactory
{
    typedef std::list<CNewUIMessageBoxBase *> type_list_msgbox;
    type_list_msgbox m_listMsgBoxes;

  public:
    CNewUIMessageBoxFactory()
    {
    }
    ~CNewUIMessageBoxFactory()
    {
        DeleteAllMessageBoxes();
    }

    enum INSTANCE_STATE
    {
        INSTANCE_NEW = 1,
        INSTANCE_REFERENCE
    };
    template <class T> class TContainer
    {
        T *m_pObj;
        mutable INSTANCE_STATE m_InstState;

      public:
        explicit TContainer(SessionKeeper &keeper)
            : m_pObj(new T(keeper)), m_InstState(INSTANCE_NEW)
        {
        }
        TContainer(TContainer &_container) : m_pObj(NULL), m_InstState(INSTANCE_REFERENCE)
        {
            if (_container.GetInstance())
                m_pObj = _container.GetInstance(); //. copy instance
        }
        virtual ~TContainer()
        {
            if (m_InstState == INSTANCE_NEW)
                delete m_pObj;
        }

        T *GetInstance() const
        {
            return m_pObj;
        }
        void SetInstState(INSTANCE_STATE InstState) const
        {
            m_InstState = InstState;
        }

        TContainer<T> &operator=(const TContainer<T> &_container)
        {
            if (_container.GetInstance())
            {
                if (m_InstState == INSTANCE_NEW)
                    delete m_pObj;
                m_pObj = _container.GetInstance();
                m_InstState = INSTANCE_REFERENCE;
            }
        }
    };
    template <class T>

    T *NewMessageBox(const TContainer<T> &_container)
    {
        T *pObj = _container.GetInstance();
        m_listMsgBoxes.push_back(pObj);
        _container.SetInstState(INSTANCE_REFERENCE);
        return pObj;
    }

    void DeleteMessageBox(const CNewUIMessageBoxBase *pObj)
    {
        auto li = std::find(m_listMsgBoxes.begin(), m_listMsgBoxes.end(), pObj);
        if (li != m_listMsgBoxes.end())
        {
            delete (*li);
            m_listMsgBoxes.erase(li);
        }
    }
    void DeleteAllMessageBoxes()
    {
        auto li = m_listMsgBoxes.begin();
        for (; li != m_listMsgBoxes.end(); li++)
            delete (*li); //. delete instance
        m_listMsgBoxes.clear();
    }
};

class CNewUIMessageBoxMng : public CNewUIObj, protected SessionLegacyCalls
{
    class CNewUIEvent
    {
        CNewUIMessageBoxBase *m_pOwner;
        DWORD m_dwEvent;
        leaf::xstreambuf m_xParam;

      public:
        CNewUIEvent(CNewUIMessageBoxBase *pOwner, DWORD dwEvent)
            : m_pOwner(pOwner), m_dwEvent(dwEvent)
        {
        }
        CNewUIEvent(CNewUIMessageBoxBase *pOwner, DWORD dwEvent, const leaf::xstreambuf &param)
        {
            m_pOwner = pOwner;
            m_dwEvent = dwEvent;
            m_xParam = param;
        }

        CNewUIMessageBoxBase *GetOwner() const
        {
            return m_pOwner;
        }
        DWORD GetEvent() const
        {
            return m_dwEvent;
        }
        const leaf::xstreambuf &GetParam() const
        {
            return m_xParam;
        }
    };

    enum EVENT_STATE
    {
        EVENT_NONE = 0,
        EVENT_WND_MOUSE_HOVER,
        EVENT_WND_MOUSE_LBUTTON_DOWN,
        EVENT_WND_MOUSE_RBUTTON_DOWN,
    };

    typedef std::vector<CNewUIMessageBoxBase *> type_vector_msgbox;
    typedef std::queue<CNewUIEvent *> type_queue_event;

    CNewUIManager *m_pNewUIMng;
    CNewUIMessageBoxFactory *m_pMsgBoxFactory;
    type_vector_msgbox m_vecMsgBoxes;
    type_queue_event m_queueEvents;
    EVENT_STATE m_EventState;
    std::unique_ptr<UI::Modern::PC::SystemMenu::RmlSystemMenuPanel> m_modernSystemMenu;
    std::unique_ptr<UI::Modern::RmlMessageBoxPanel> m_modernMessageBoxPanel;

  public:
    explicit CNewUIMessageBoxMng(SessionKeeper &keeper);
    enum IMAGE_LIST
    {
        IMAGE_MSGBOX_TOP = BITMAP_INTERFACE_NEW_MESSAGEBOX_BEGIN,
        IMAGE_MSGBOX_MIDDLE,
        IMAGE_MSGBOX_BOTTOM,
        IMAGE_MSGBOX_BACK,
        IMAGE_MSGBOX_BTN_OK,
        IMAGE_MSGBOX_BTN_CANCEL,
        IMAGE_MSGBOX_BTN_CLOSE,
        IMAGE_MSGBOX_BTN_EMPTY,
        IMAGE_MSGBOX_BTN_EMPTY_SMALL,
        IMAGE_MSGBOX_BTN_EMPTY_BIG,
        IMAGE_MSGBOX_BTN_EMPTY_VERY_SMALL,
        IMAGE_MSGBOX_LINE,
        IMAGE_MSGBOX_TOP_TITLEBAR,
        IMAGE_MSGBOX_SEPARATE_LINE,
        IMAGE_MSGBOX_PROGRESS_BG,
        IMAGE_MSGBOX_PROGRESS_BAR,
        IMAGE_MSGBOX_DUEL_BACK,
    };

    virtual ~CNewUIMessageBoxMng();

    bool Create(CNewUIManager *pNewUIMng);
    void Release();

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const;
    UI::Modern::PC::SystemMenu::RmlSystemMenuPanel &ModernSystemMenuPanel() noexcept;
    UI::Modern::RmlMessageBoxPanel &ModernMessageBoxPanel() noexcept;

    float GetLayerDepth();    //. 10.7f
    float GetKeyEventOrder(); //. 10.f

    static bool ComparePriority(CNewUIMessageBoxBase *pObj1, CNewUIMessageBoxBase *pObj2);

    template <class T>

    T *NewMessageBox(const CNewUIMessageBoxFactory::TContainer<T> &container)
    {
        T *pMsgBox = NULL;
        if (m_pMsgBoxFactory)
        {
            pMsgBox = m_pMsgBoxFactory->NewMessageBox(container);
            m_vecMsgBoxes.push_back(pMsgBox);
        }
        return pMsgBox;
    }

    void DeleteMessageBox(const CNewUIMessageBoxBase *pObj);
    void PopMessageBox();
    void PopAllMessageBoxes();

    bool IsEmpty();

    void SendEvent(CNewUIMessageBoxBase *pOwner, DWORD dwEvent);
    void SendEvent(CNewUIMessageBoxBase *pOwner, DWORD dwEvent, const leaf::xstreambuf &xParam);

  protected:
    void PopEvent();
    void PopAllEvents();

  private:
    void LoadImages();
    void UnloadImages();
};

class IMsgBoxLayout
{
  public:
    virtual bool SetLayout() = 0;
};

template <class _M>
class TMsgBoxLayout : public IMsgBoxLayout,
                      public std::enable_shared_from_this<TMsgBoxLayout<_M>>,
                      protected SessionUiLegacyBindings
{
  protected:
    SessionUiUnit &sessionUi_;

  private:
    _M *m_pMsgBox;

  public:
    explicit TMsgBoxLayout(SessionKeeper &keeper)
        : SessionUiLegacyBindings(keeper), sessionUi_(UiForConstruction()), m_pMsgBox(nullptr)
    {
        CNewUIMessageBoxFactory::TContainer<_M> container(keeper);
        m_pMsgBox = g_MessageBox.NewMessageBox(container);
    }
    virtual ~TMsgBoxLayout()
    {
    }

    _M *GetMsgBox() const noexcept
    {
        return m_pMsgBox;
    }

  protected:
    template <class _L>
    EVENT_CALLBACK BindCallback(_L *owner,
                                CALLBACK_RESULT (_L::*callback)(CNewUIMessageBoxBase *,
                                                                const leaf::xstreambuf &))
    {
        auto lifetime = this->shared_from_this();
        return [lifetime, owner, callback](CNewUIMessageBoxBase *messageBox,
                                           const leaf::xstreambuf &parameter) {
            return (owner->*callback)(messageBox, parameter);
        };
    }
};
template <class _L> class TMsgBoxLayoutContainer
{
    mutable std::shared_ptr<_L> m_pMsgBoxLayout;
    std::function<std::shared_ptr<_L>()> createLayout_;

  public:
    explicit TMsgBoxLayoutContainer(SessionKeeper &keeper)
        : createLayout_([&keeper] { return std::make_shared<_L>(keeper); })
    {
    }
    ~TMsgBoxLayoutContainer()
    {
        Release();
    }

    bool Create() const
    {
        if (m_pMsgBoxLayout)
            return false;
        m_pMsgBoxLayout = createLayout_();
        return true;
    }
    void Release() const
    {
        m_pMsgBoxLayout.reset();
    }
    bool SetLayout() const
    {
        if (m_pMsgBoxLayout)
            return m_pMsgBoxLayout->SetLayout();
        return false;
    }
    auto GetMsgBox() const noexcept
    {
        return m_pMsgBoxLayout ? m_pMsgBoxLayout->GetMsgBox() : nullptr;
    }
};

template <class _L> bool CreateMessageBox(const TMsgBoxLayoutContainer<_L> &container)
{
    if (false == container.Create()) //. MessageBox Layout
        return false;

    return container.SetLayout();
}
template <class _L, class _M>
bool CreateMessageBox(const TMsgBoxLayoutContainer<_L> &container, _M **ppMsgBox)
{
    if (false == container.Create()) //. MessageBox Layout
        return false;
    if (ppMsgBox)
        *ppMsgBox = container.GetMsgBox();

    return container.SetLayout();
}
} // namespace SEASON3B

#define MSGBOX_LAYOUT_CLASS(x, ...) SEASON3B::TMsgBoxLayoutContainer<x>(__VA_ARGS__)

namespace SEASON3B
{
enum
{
    MSGBOX_COMMON_TYPE_OK,
    MSGBOX_COMMON_TYPE_OKCANCEL,
};

enum
{
    MSGBOX_FONT_NORMAL,
    MSGBOX_FONT_BOLD,
};

static constexpr float SCREEN_WIDTH = (float)REFERENCE_WIDTH;
static constexpr float SCREEN_HEIGHT = (float)REFERENCE_HEIGHT;

static constexpr float MSGBOX_WIDTH = 230.0f;
static constexpr float MSGBOX_TOP_HEIGHT = 67.0f;
static constexpr float MSGBOX_BOTTOM_HEIGHT = 50.0f;
static constexpr float MSGBOX_MIDDLE_HEIGHT = 15.0f;

static constexpr float MSGBOX_BACK_BLANK_WIDTH = 8.0f;
static constexpr float MSGBOX_BACK_BLANK_HEIGHT = 10.0f;

static constexpr float MSGBOX_TEXT_TOP_BLANK = 35.0f;
static constexpr float MSGBOX_TEXT_MAXWIDTH = 180.0f;

static constexpr float MSGBOX_LINE_WIDTH = 223.0f;
static constexpr float MSGBOX_LINE_HEIGHT = 21.0f;

static constexpr float MSGBOX_SEPARATE_LINE_WIDTH = 205.0f;
static constexpr float MSGBOX_SEPARATE_LINE_HEIGHT = 2.0f;

static constexpr float MSGBOX_BTN_WIDTH = 54.0f;
static constexpr float MSGBOX_BTN_HEIGHT = 30.0f;
static constexpr float MSGBOX_BTN_BOTTOM_BLANK = 20.0f;

static constexpr float MSGBOX_BTN_EMPTY_SMALL_WIDTH = 64.0f;
static constexpr float MSGBOX_BTN_EMPTY_WIDTH = 108.0f;
static constexpr float MSGBOX_BTN_EMPTY_BIG_WIDTH = 180.0f;
static constexpr float MSGBOX_BTN_EMPTY_HEIGHT = 29.0f;

typedef struct _MSGBOX_TEXTDATA
{
    std::wstring strMsg;
    DWORD dwColor;
    BYTE byFontType;

    _MSGBOX_TEXTDATA()
    {
        strMsg = L"";
        dwColor = 0xffffffff;
        byFontType = MSGBOX_FONT_NORMAL;
    }
} MSGBOX_TEXTDATA;

typedef std::vector<MSGBOX_TEXTDATA *> type_vector_msgdata;
typedef std::wstring type_string;

class CNewUIMessageBoxButton : protected SessionUiLegacyBindings
{
  public:
    enum EVENT_STATE
    {
        EVENT_NONE = 0,
        EVENT_BTN_HOVER,
        EVENT_BTN_DOWN,
    };

    enum BTN_SIZE_TYPE
    {
        MSGBOX_BTN_CUSTOM = 0,
        MSGBOX_BTN_SIZE_OK,
        MSGBOX_BTN_SIZE_EMPTY,
        MSGBOX_BTN_SIZE_EMPTY_SMALL,
        MSGBOX_BTN_SIZE_EMPTY_BIG,
    };

    explicit CNewUIMessageBoxButton(SessionKeeper &keeper);
    ~CNewUIMessageBoxButton();

    bool IsMouseIn();
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    void SetInfo(DWORD dwTexType, float x, float y, float width, float height,
                 DWORD dwSizeType = MSGBOX_BTN_CUSTOM, bool bClickEffect = false);
    void MoveTextPos(int iX, int iY);
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
    void SetInfo(DWORD dwTexType, float x, float y, float width, float height,
                 DWORD dwSizeType = MSGBOX_BTN_SIZE_OK);
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
    void SetText(const wchar_t *strText);
    void AddBlank(int iAddLine);

    void SetEnable(bool bEnable)
    {
        m_bEnable = bEnable;
    }

    void SetPos(float x, float y)
    {
        m_x = x;
        m_y = y;
    }
    float GetPosX()
    {
        return m_x;
    }
    float GetPosY()
    {
        return m_y;
    }
    float GetWidth()
    {
        return m_width;
    }
    float GetHeight()
    {
        return m_height;
    }

    void ClearEventState()
    {
        m_EventState = EVENT_NONE;
    }
    EVENT_STATE GetEventState()
    {
        return m_EventState;
    }

    virtual void Update();
    virtual void Render();

  private:
    bool m_bEnable;

    DWORD m_dwTexType;
    DWORD m_dwSizeType;

    std::wstring m_strText;
    float m_x, m_y, m_width, m_height;
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    float m_fButtonWidth;
    float m_fButtonHeight;
    int m_iMoveTextPosX;
    int m_iMoveTextPosY;
    bool m_bClickEffect;
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

    EVENT_STATE m_EventState;
};

class CNewUICommonMessageBox : public CNewUIMessageBoxBase
{
  public:
    using CNewUIMessageBoxBase::CNewUIMessageBoxBase;
    explicit CNewUICommonMessageBox(SessionKeeper &keeper);
    ~CNewUICommonMessageBox();

    DWORD GetType();

    bool Create(DWORD dwType, float fPriority = 3.f);
    bool Create(DWORD dwType, const type_string &strMsg, DWORD dwColor = CLRDW_WHITE,
                BYTE byFontType = MSGBOX_FONT_NORMAL, float fPriority = 3.f);

    void AddMsg(const type_string &strMsg, DWORD dwColor = CLRDW_WHITE,
                BYTE byFontType = MSGBOX_FONT_NORMAL);
    void UseS16Caution(const type_string &title, const type_string &message);

    CALLBACK_RESULT Close(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;

    void LockOkButton();

  private:
    void SetAddCallbackFunc();
    bool UpdateS16Caution();

    DWORD m_dwType;
    type_string m_s16Title;
    type_string m_s16Message;
};

class CNewUI3DItemCommonMsgBox : public CNewUIMessageBoxBase, public INewUI3DRenderObj
{
  public:
    explicit CNewUI3DItemCommonMsgBox(SessionKeeper &keeper);
    ~CNewUI3DItemCommonMsgBox();

    DWORD GetType();

    bool Create(DWORD dwType, float fPriority = 3.f);
    bool Create(DWORD dwType, const type_string &strMsg, DWORD dwColor = CLRDW_WHITE,
                BYTE byFontType = MSGBOX_FONT_NORMAL, float fPriority = 3.f);
    void Release();

    void AddMsg(const type_string &strMsg, DWORD dwColor = CLRDW_WHITE,
                BYTE byFontType = MSGBOX_FONT_NORMAL);

    void Set3DItem(ITEM *pItem);
    void SetItemValue(int iValue);
    int GetItemValue();
    CmuConsoleDebug &ConsoleDebug() const noexcept;

    CALLBACK_RESULT Close(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;
    void Render3D();

    bool IsVisible() const;

  private:
    void SetAddCallbackFunc();

    DWORD m_dwType;
    ITEM m_Item;
    int m_iItemValue;

    UI::Modern::PC::Inventory::RmlItemDialogPanel panel_;
    std::wstring message_;
};

class CFenrirRepairMsgBox : public CNewUICommonMessageBox
{
  public:
    using CNewUICommonMessageBox::CNewUICommonMessageBox;
    void SetSourceIndex(int iIndex);
    void SetTargetIndex(int iIndex);
    int GetSourceIndex();
    int GetTargetIndex();

  private:
    int m_iSourceIndex;
    int m_iTargetIndex;
};

class CServerLostMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    explicit CServerLostMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUICommonMessageBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CGuildRequestMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CGuildFireMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CMapEnterWerwolfMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CMapEnterGateKeeperMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CPartyMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CTradeMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CTradeAlertMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CGuildWarMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CBattleSoccerMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CServerImmigrationErrorMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CPersonalshopCreateMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CFenrirRepairMsgBoxLayout : public TMsgBoxLayout<CFenrirRepairMsgBox>
{
  public:
    using TMsgBoxLayout<CFenrirRepairMsgBox>::TMsgBoxLayout;
    explicit CFenrirRepairMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CFenrirRepairMsgBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CInfinityArrowCancelMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CBuffSwellOfMPCancelMsgBoxLayOut : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CGemIntegrationUnityCheckMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CGemIntegrationUnityResultMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CGemIntegrationDisjointCheckMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CGemIntegrationDisjointResultMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CChaosCastleTimeCheckMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CHarvestEventLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CWhiteAngelEventLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CCanNotUseWordMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CMixCheckMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CUseReviveCharmMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    explicit CUseReviveCharmMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUICommonMessageBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CUsePortalCharmMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    explicit CUsePortalCharmMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUICommonMessageBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};
class CReturnPortalCharmMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    explicit CReturnPortalCharmMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUICommonMessageBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CDuelCreateErrorMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CDuelWatchErrorMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CDoppelGangerMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CGuildRelationShipMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CCastleMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CSiegeLevelMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};
class CSiegeGiveUpMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};
class CGatemanMoneyMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};
class CGatemanFailMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CQuestGiveUpMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    DWORD selectedQuest_ = 0;
};

#ifdef ASG_ADD_TIME_LIMIT_QUEST
class CQuestCountLimitMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};
#endif // ASG_ADD_TIME_LIMIT_QUEST

class CHighValueItemCheckMsgBoxLayout : public TMsgBoxLayout<CNewUI3DItemCommonMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUI3DItemCommonMsgBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CUseFruitMsgBoxLayout : public TMsgBoxLayout<CNewUI3DItemCommonMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUI3DItemCommonMsgBox>::TMsgBoxLayout;
    explicit CUseFruitMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUI3DItemCommonMsgBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CUsePartChargeFruitMsgBoxLayout : public TMsgBoxLayout<CNewUI3DItemCommonMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUI3DItemCommonMsgBox>::TMsgBoxLayout;
    explicit CUsePartChargeFruitMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUI3DItemCommonMsgBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class COsbourneMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CPersonalShopItemValueCheckMsgBoxLayout : public TMsgBoxLayout<CNewUI3DItemCommonMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUI3DItemCommonMsgBox>::TMsgBoxLayout;
    explicit CPersonalShopItemValueCheckMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUI3DItemCommonMsgBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CPersonalShopItemBuyMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CGuildOutPerson : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CGuildBreakMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    explicit CGuildBreakMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUICommonMessageBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    std::wstring target_;
    bool submitted_ = false;
};

class CGuildPerson_Get_Out : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    explicit CGuildPerson_Get_Out(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUICommonMessageBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    std::wstring target_;
    bool submitted_ = false;
};

class CGuildPerson_Cancel_Position_MsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    std::wstring target_;
    bool submitted_ = false;
};

class CUnionGuild_Break_MsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    std::wstring target_;
    bool submitted_ = false;
};

class CUnionGuild_Out_MsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CMaster_Level_Interface : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CCry_Wolf_Get_Temple : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CCry_Wolf_Set_Temple : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CCry_Wolf_Set_Temple1 : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CCry_Wolf_Dont_Set_Temple : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CCry_Wolf_Dont_Set_Temple1 : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CCry_Wolf_Wat_Set_Temple1 : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CCry_Wolf_Destroy_Set_Temple : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CCry_Wolf_Ing_Set_Temple : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CCry_Wolf_Result_Set_Temple : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CUseSantaInvitationMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    explicit CUseSantaInvitationMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUICommonMessageBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CSantaTownLeaveMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CSantaTownSantaMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CUseRegistLuckyCoinMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CRegistOverLuckyCoinMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CExchangeLuckyCoinMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CExchangeLuckyCoinInvenErrMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CGambleBuyMsgBoxLayout : public TMsgBoxLayout<CNewUI3DItemCommonMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUI3DItemCommonMsgBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

bool CreateGambleBuyMessageBox(SessionKeeper &keeper);

class CEmpireGuardianMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CUnitedMarketPlaceMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CLuckyItemMsgBoxLayout : public TMsgBoxLayout<CNewUICommonMessageBox>
{
  public:
    using TMsgBoxLayout<CNewUICommonMessageBox>::TMsgBoxLayout;
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};
} // namespace SEASON3B

class CGMCrywolf1st;

namespace MUHelper
{
class SessionMuHelperUnit;
}
class SessionGameDataUnit;
class SessionUiUnit;
class CErrorReport;
class CmuConsoleDebug;
namespace SEASON3A
{
class CursedTemple;
}

namespace SEASON3B
{
enum
{
    INPUTBOX_TYPE_NUMBER,
    INPUTBOX_TYPE_TEXT,
};

enum KEYPAD_TYPE
{
    KEYPAD_TYPE_MOVE = 1,
    KEYPAD_TYPE_UNLOCK = 2,
    KEYPAD_TYPE_LOCK_FIRST = 3,
    KEYPAD_TYPE_LOCK_SECOND = 4,
    KEYPAD_TYPE_LOCK_FINAL = 5,
};

static constexpr float INPUTBOX_WIDTH = 50.0f;
static constexpr float INPUTBOX_HEIGHT = 12.0f;
static constexpr int INPUTBOX_TEXTLIMIT = 8;

class CNewUITextInputMsgBox : public CNewUIMessageBoxBase
{
    static constexpr float INPUTBOX_TOP_BLANK = 10.0f;

    typedef struct _MSGBOX_TEXTDATA
    {
        std::wstring strMsg;
        DWORD dwColor;
        BYTE byFontType;

        _MSGBOX_TEXTDATA()
        {
            strMsg = L"";
            dwColor = 0xffffffff;
            byFontType = MSGBOX_FONT_NORMAL;
        }
    } MSGBOX_TEXTDATA;

    typedef std::vector<MSGBOX_TEXTDATA *> type_vector_msgdata;
    typedef std::wstring type_string;

  public:
    explicit CNewUITextInputMsgBox(SessionKeeper &keeper);
    virtual ~CNewUITextInputMsgBox();

    bool Create(DWORD dwMsgBoxType, DWORD dwInputType, int iInputBoxWidth = 100,
                int iInputBoxHeight = 14, int iLimitText = 256, bool bIsPassword = false);
    bool CreateModernNumberInput(DWORD dwMsgBoxType);
    bool CreateModernPasswordInput(int maxLength);
    void Release();

    CALLBACK_RESULT LButtonUp(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);

    void AddMsg(const type_string &strMsg, DWORD dwColor = CLRDW_WHITE,
                BYTE byFontType = MSGBOX_FONT_NORMAL);

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const override;

    DWORD GetMsgBoxType();

    void GetInputBoxText(wchar_t *strText);
    void SetInputBoxOption(int iOption);
    void SetInputBoxPosition(int x, int y);
    void SetInputBoxSize(int width, int height);
    SessionKeeper &OriginatingSession() const noexcept
    {
        return SessionOrigin();
    }

  private:
    int SeparateText(const type_string &strMsg, DWORD dwColor, BYTE byFontType);
    void SetButtonInfo();
    void AddButtonBlank(int iAddLine);
    void RenderTexts();
    void RenderButtons();
    bool UpdateModernInput();

    DWORD m_dwMsgBoxType;
    DWORD m_dwInputType;

    CUITextInputBox *m_pInputBox;
    type_vector_msgdata m_MsgTextList;

    CNewUIMessageBoxButton m_BtnOk{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnCancel{SessionOrigin()};
    std::wstring m_ModernMessage;
    bool m_UseModernNumberInput = false;
    bool m_UseModernPasswordInput = false;
    UI::Modern::PC::Inventory::RmlItemDialogPanel m_ModernMenu;

  public:
    void SetPassword(WORD password)
    {
        m_password = password;
    }
    WORD GetPassword()
    {
        return m_password;
    }

  private:
    WORD m_password;
};

class CNewUIKeyPadButton : public CNewUIMessageBoxButton
{
  public:
    using CNewUIMessageBoxButton::CNewUIMessageBoxButton;
    void Render();
};

class CNewUIDeleteKeyPadButton : public CNewUIMessageBoxButton
{
  public:
    using CNewUIMessageBoxButton::CNewUIMessageBoxButton;
    void Render();
};

class CNewUIKeyPadMsgBox : public CNewUIMessageBoxBase
{

    typedef struct _MSGBOX_TEXTDATA
    {
        std::wstring strMsg;
        DWORD dwColor;
        BYTE byFontType;

        _MSGBOX_TEXTDATA()
        {
            strMsg = L"";
            dwColor = 0xffffffff;
            byFontType = MSGBOX_FONT_NORMAL;
        }
    } MSGBOX_TEXTDATA;

    typedef std::vector<MSGBOX_TEXTDATA *> type_vector_msgdata;
    typedef std::wstring type_string;

  public:
    explicit CNewUIKeyPadMsgBox(SessionKeeper &keeper);
    virtual ~CNewUIKeyPadMsgBox();

    SessionKeeper &OriginatingSession() const noexcept
    {
        return SessionOrigin();
    }

    bool Create(DWORD dwType, int iInputLImit = 4);
    void Release();

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;

    void AddMsg(const type_string &strMsg, DWORD dwColor = CLRDW_WHITE,
                BYTE byFontType = MSGBOX_FONT_NORMAL);

    int GetInputLimit();
    int GetInputSize();
    void ClearInput();
    const wchar_t *GetInputText();
    void SetCheckInputText(const wchar_t *strInput);
    bool IsCheckInput();
    bool IsAllSameNumber();

    void SetStoragePassword(WORD wPassword);
    WORD GetStoragePassword();

    CALLBACK_RESULT DeleteBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
    CALLBACK_RESULT KeyPadBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
    CALLBACK_RESULT Close(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);

  private:
    void StageModernContent();
    UI::Modern::PC::Inventory::RmlItemDialogPanel m_ModernMenu;

    void KeyPadInput(int iInput);
    void DeleteKeyPadInput();

    DWORD m_dwType;

    int m_iInputLimit;
    int m_iKeyPadMapping[MAX_KEYPADINPUT];
    wchar_t m_strKeyPadInput[MAX_PASSWORD_SIZE + 1];
    wchar_t m_strCheckKeyPadInput[MAX_PASSWORD_SIZE + 1];

    WORD m_wStoragePassword;
    type_vector_msgdata m_MsgTextList;
};

class CUseFruitCheckMsgBox : public CNewUIMessageBoxBase, public INewUI3DRenderObj
{
  public:
    explicit CUseFruitCheckMsgBox(SessionKeeper &keeper);
    virtual ~CUseFruitCheckMsgBox();

    bool Create(float fPriority = 3.f);
    void Release();

    void Set3DItem(ITEM *pItem);

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;
    void Render3D();

    bool IsVisible() const;

    CALLBACK_RESULT AddBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT MinusBtnDown(class CNewUIMessageBoxBase *pOwner,
                                 const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    void AddMsg(const type_string &strMsg, DWORD dwColor = CLRDW_WHITE,
                BYTE byFontType = MSGBOX_FONT_NORMAL);
    void SetAddCallbackFunc();

    ITEM m_Item;

    UI::Modern::PC::Inventory::RmlItemDialogPanel panel_;
    std::wstring message_;
};

class CGemIntegrationMsgBox : public CNewUIMessageBoxBase
{
  public:
    explicit CGemIntegrationMsgBox(SessionKeeper &keeper);
    virtual ~CGemIntegrationMsgBox();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;

    CALLBACK_RESULT UnityBtnDown(class CNewUIMessageBoxBase *pOwner,
                                 const leaf::xstreambuf &xParam);
    CALLBACK_RESULT DisjointBtnDown(class CNewUIMessageBoxBase *pOwner,
                                    const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();

    UI::Modern::PC::Inventory::RmlItemDialogPanel panel_;
    std::string locale_;
    void StageContent();
};

class CGemIntegrationUnityMsgBox : public CNewUIMessageBoxBase
{
  public:
    explicit CGemIntegrationUnityMsgBox(SessionKeeper &keeper);
    virtual ~CGemIntegrationUnityMsgBox();

    bool Create(float fPriority = 3.f);
    void Release();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;

    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
    CALLBACK_RESULT SelectMixBtnDown(class CNewUIMessageBoxBase *pOwner,
                                     const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();

    UI::Modern::PC::Inventory::RmlItemDialogPanel panel_;
    std::string locale_;
    void StageContent();
    UI::Modern::PC::Inventory::RmlItemDialogPanel unitPanel_;
    UI::Modern::PC::Inventory::RmlItemDialogPanel &ActivePanel();
};

class CGemIntegrationDisjointMsgBox : public CNewUIMessageBoxBase
{
  public:
    explicit CGemIntegrationDisjointMsgBox(SessionKeeper &keeper);
    virtual ~CGemIntegrationDisjointMsgBox();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;

    CALLBACK_RESULT DisjointBtnDown(class CNewUIMessageBoxBase *pOwner,
                                    const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();

    UI::Modern::PC::Inventory::RmlItemDialogPanel panel_;
    std::string locale_;
    void StageContent();
};

class CSystemMenuMsgBox : public CNewUIMessageBoxBase
{
  public:
    using CNewUIMessageBoxBase::CNewUIMessageBoxBase;
    explicit CSystemMenuMsgBox(SessionKeeper &keeper);
    virtual ~CSystemMenuMsgBox();

    bool Create(float fPriority = 3.f);
    void Release();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;
    MUHelper::SessionMuHelperUnit &MuHelper() const noexcept;
    CErrorReport &ErrorReport() const noexcept;
    CmuConsoleDebug &ConsoleDebug() const noexcept;

    CALLBACK_RESULT GameOverBtnDown(class CNewUIMessageBoxBase *pOwner,
                                    const leaf::xstreambuf &xParam);
    CALLBACK_RESULT ChooseServerBtnDown(class CNewUIMessageBoxBase *pOwner,
                                        const leaf::xstreambuf &xParam);
    CALLBACK_RESULT ChooseCharacterBtnDown(class CNewUIMessageBoxBase *pOwner,
                                           const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    CGMCrywolf1st &crywolf_;
    MUHelper::SessionMuHelperUnit &muHelper_;
};

class CBloodCastleResultMsgBox : public CNewUIMessageBoxBase
{
    static constexpr float MIDDLE_COUNT = 6.0f;

  public:
    explicit CBloodCastleResultMsgBox(SessionKeeper &keeper);
    virtual ~CBloodCastleResultMsgBox();

    bool Create(float fPriority = 3.f);

    bool Update();
    bool Render();

    CALLBACK_RESULT LButtonUp(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);

  private:
    void RenderFrame();

    CNewUIMessageBoxButton m_BtnOk{SessionOrigin()};
};

class CDevilSquareRankMsgBox : public CNewUIMessageBoxBase
{
    static constexpr float MIDDLE_COUNT1 = 11.0f;
    static constexpr float MIDDLE_COUNT2 = 3.0f;

  public:
    explicit CDevilSquareRankMsgBox(SessionKeeper &keeper);
    virtual ~CDevilSquareRankMsgBox();

    bool Create(float fPriority = 3.f);

    bool Update();
    bool Render();

    CALLBACK_RESULT LButtonUp(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);

  private:
    void RenderFrame();

    CNewUIMessageBoxButton m_BtnOk{SessionOrigin()};
};

class CChaosCastleResultMsgBox : public CNewUIMessageBoxBase
{
    static constexpr float MIDDLE_COUNT = 6.0f;

  public:
    explicit CChaosCastleResultMsgBox(SessionKeeper &keeper);
    virtual ~CChaosCastleResultMsgBox();

    bool Create(float fPriority = 3.f);

    bool Update();
    bool Render();

    CALLBACK_RESULT LButtonUp(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);

  private:
    void RenderFrame();

    CNewUIMessageBoxButton m_BtnOk{SessionOrigin()};
};

class CChaosMixMenuMsgBox : public CNewUIMessageBoxBase
{
  public:
    explicit CChaosMixMenuMsgBox(SessionKeeper &keeper);
    virtual ~CChaosMixMenuMsgBox();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;

    CALLBACK_RESULT GeneralMixBtnDown(class CNewUIMessageBoxBase *pOwner,
                                      const leaf::xstreambuf &xParam);
    CALLBACK_RESULT ChaosMixBtnDown(class CNewUIMessageBoxBase *pOwner,
                                    const leaf::xstreambuf &xParam);
    CALLBACK_RESULT Mix380BtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void StageModernContent();
    UI::Modern::PC::Inventory::RmlItemDialogPanel m_ModernMenu;
};

class CTrainerMenuMsgBox : public CNewUIMessageBoxBase
{
  public:
    explicit CTrainerMenuMsgBox(SessionKeeper &keeper);
    ~CTrainerMenuMsgBox();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;

    CALLBACK_RESULT RecoverBtnDown(class CNewUIMessageBoxBase *pOwner,
                                   const leaf::xstreambuf &xParam);
    CALLBACK_RESULT ReviveBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
    CALLBACK_RESULT ExitBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void StageModernContent();
    UI::Modern::PC::Inventory::RmlItemDialogPanel m_ModernMenu;
};

class CTrainerRecoverMsgBox : public CNewUIMessageBoxBase
{
  public:
    explicit CTrainerRecoverMsgBox(SessionKeeper &keeper);
    ~CTrainerRecoverMsgBox();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;

    CALLBACK_RESULT RecoverDarkSpiritrBtnDown(class CNewUIMessageBoxBase *pOwner,
                                              const leaf::xstreambuf &xParam);
    CALLBACK_RESULT RecoverDarkHorseBtnDown(class CNewUIMessageBoxBase *pOwner,
                                            const leaf::xstreambuf &xParam);
    CALLBACK_RESULT ExitBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void StageModernContent();
    UI::Modern::PC::Inventory::RmlItemDialogPanel m_ModernMenu;
};

class CElpisMsgBox : public CNewUIMessageBoxBase
{
  public:
    explicit CElpisMsgBox(SessionKeeper &keeper);
    ~CElpisMsgBox();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;

    CALLBACK_RESULT AboutRefinaryBtnDown(class CNewUIMessageBoxBase *pOwner,
                                         const leaf::xstreambuf &xParam);
    CALLBACK_RESULT AboutJewelOfHarmonyBtnDown(class CNewUIMessageBoxBase *pOwner,
                                               const leaf::xstreambuf &xParam);
    CALLBACK_RESULT RefineBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
    CALLBACK_RESULT ExitBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);

    void SetMessageType(int iMessageType)
    {
        m_iMessageType = iMessageType;
    }

  private:
    void SetAddCallbackFunc();
    void StageModernContent();
    UI::Modern::PC::Inventory::RmlItemDialogPanel m_ModernMenu;

    int m_iMessageType = 0;
};

class CDialogMsgBox : public CNewUICommonMessageBox
{
  public:
    using CNewUICommonMessageBox::CNewUICommonMessageBox;
    bool Create(float priority = 3.f)
    {
        return CNewUICommonMessageBox::Create(MSGBOX_COMMON_TYPE_OK, priority);
    }
};

class CProgressMsgBox : public CNewUIMessageBoxBase
{
  public:
    using CNewUIMessageBoxBase::CNewUIMessageBoxBase;
    explicit CProgressMsgBox(SessionKeeper &keeper);
    ~CProgressMsgBox();

    bool Create(DWORD dwElapseTime = 3000, float fPriority = 3.f);
    void Release();

    void AddMsg(const type_string &strMsg, DWORD dwColor = CLRDW_WHITE,
                BYTE byFontType = MSGBOX_FONT_NORMAL);
    void SetElapseTime(DWORD dwElapseTime);

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;

    CALLBACK_RESULT ClosingProcess(class CNewUIMessageBoxBase *pOwner,
                                   const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();

  private:
    UI::Modern::PC::Events::RmlInteractionProgressPanel panel_;
    std::wstring message_;
    DWORD elapsed_ = 0;
    bool finished_ = false;

    DWORD m_dwStartTime;
    DWORD m_dwEndTime;
    DWORD m_dwElapseTime;
};

class CCursedTempleProgressMsgBox : public CNewUIMessageBoxBase
{
  public:
    using CNewUIMessageBoxBase::CNewUIMessageBoxBase;
    explicit CCursedTempleProgressMsgBox(SessionKeeper &keeper);
    ~CCursedTempleProgressMsgBox();

    bool Create(DWORD dwElapseTime = 3000, float fPriority = 3.f);
    void Release();

    void AddMsg(const type_string &strMsg, DWORD dwColor = CLRDW_WHITE,
                BYTE byFontType = MSGBOX_FONT_NORMAL);

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;

    void SetNpcIndex(DWORD dwIndex);
    DWORD GetNpcIndex();

    CALLBACK_RESULT ClosingProcess(class CNewUIMessageBoxBase *pOwner,
                                   const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CompleteProcess(class CNewUIMessageBoxBase *pOwner,
                                    const leaf::xstreambuf &xParam);

  private:
    SEASON3A::CursedTemple &cursedTemple_;
    void SetAddCallbackFunc();

    bool CheckHeroAction();

  private:
    UI::Modern::PC::Events::RmlInteractionProgressPanel panel_;
    std::wstring message_;
    DWORD elapsed_ = 0;
    bool finished_ = false;

    DWORD m_dwStartTime;
    DWORD m_dwEndTime;
    DWORD m_dwElapseTime;

    DWORD m_dwNpcIndex;
};

class CDuelMsgBox : public CNewUIMessageBoxBase
{
  public:
    explicit CDuelMsgBox(SessionKeeper &keeper);
    ~CDuelMsgBox();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;

    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();

    UI::Modern::PC::Inventory::RmlItemDialogPanel panel_;
};

class CDuelResultMsgBox : public CNewUIMessageBoxBase
{
  public:
    explicit CDuelResultMsgBox(SessionKeeper &keeper);
    ~CDuelResultMsgBox();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;

    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);

    void SetIDs(wchar_t *pszWinnerID, wchar_t *pszLoserID);

  private:
    void SetAddCallbackFunc();

    UI::Modern::PC::Inventory::RmlItemDialogPanel panel_;
    wchar_t m_szWinnerID[24];
    wchar_t m_szLoserID[24];
};

class CGuild_ToPerson_Position : public CNewUIMessageBoxBase
{
  public:
    explicit CGuild_ToPerson_Position(SessionKeeper &keeper);
    ~CGuild_ToPerson_Position();
    bool Create(float priority = 3.f);
    void Release();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;
    CALLBACK_RESULT BlessingBtnDown(CNewUIMessageBoxBase *owner, const leaf::xstreambuf &param);
    CALLBACK_RESULT SoulBtnDown(CNewUIMessageBoxBase *owner, const leaf::xstreambuf &param);
    CALLBACK_RESULT OkBtnDown(CNewUIMessageBoxBase *owner, const leaf::xstreambuf &param);
    CALLBACK_RESULT CancelBtnDown(CNewUIMessageBoxBase *owner, const leaf::xstreambuf &param);

  private:
    void StageModernContent();
    UI::Modern::PC::Inventory::RmlItemDialogPanel panel_;
    std::wstring member_;
    char role_ = 64;
    bool submitted_ = false;
};

class CCherryBlossomMsgBox : public CNewUIMessageBoxBase
{
  public:
    using CNewUIMessageBoxBase::CNewUIMessageBoxBase;
    explicit CCherryBlossomMsgBox(SessionKeeper &keeper);
    ~CCherryBlossomMsgBox();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();

    CALLBACK_RESULT LButtonUp(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT WhiteCBBtnDown(class CNewUIMessageBoxBase *pOwner,
                                   const leaf::xstreambuf &xParam);
    CALLBACK_RESULT RedCBBtnDown(class CNewUIMessageBoxBase *pOwner,
                                 const leaf::xstreambuf &xParam);
    CALLBACK_RESULT GodCBBtnDown(class CNewUIMessageBoxBase *pOwner,
                                 const leaf::xstreambuf &xParam);
    CALLBACK_RESULT ExitBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);

  private:
    const SessionGameDataUnit &GameData() const noexcept;
    void SetAddCallbackFunc();
    void SetButtonInfo();

    void RenderFrame();
    void RenderTexts();
    void RenderButtons();

    // buttons
    CNewUIMessageBoxButton m_BtnWhiteCB{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnRedCB{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnGoldCB{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnExit{SessionOrigin()};

    int m_iMiddleCount;
    const SessionGameDataUnit &gameData_;
};

class CLuckyTradeMenuMsgBox : public CNewUIMessageBoxBase
{
  public:
    explicit CLuckyTradeMenuMsgBox(SessionKeeper &keeper);
    ~CLuckyTradeMenuMsgBox();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;

    CALLBACK_RESULT LuckyItemTradeBtnDown(class CNewUIMessageBoxBase *pOwner,
                                          const leaf::xstreambuf &xParam);
    CALLBACK_RESULT LuckyItemRefineryBtnDown(class CNewUIMessageBoxBase *pOwner,
                                             const leaf::xstreambuf &xParam);
    CALLBACK_RESULT ExitBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void StageModernContent();
    UI::Modern::PC::Inventory::RmlItemDialogPanel m_ModernMenu;
};

class CSeedMasterMenuMsgBox : public CNewUIMessageBoxBase
{
  public:
    explicit CSeedMasterMenuMsgBox(SessionKeeper &keeper);
    ~CSeedMasterMenuMsgBox();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;

    CALLBACK_RESULT ExtractSeedBtnDown(class CNewUIMessageBoxBase *pOwner,
                                       const leaf::xstreambuf &xParam);
    CALLBACK_RESULT SeedSphereBtnDown(class CNewUIMessageBoxBase *pOwner,
                                      const leaf::xstreambuf &xParam);
    CALLBACK_RESULT ExitBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void StageModernContent();
    UI::Modern::PC::Inventory::RmlItemDialogPanel m_ModernMenu;
};

class CSeedInvestigatorMenuMsgBox : public CNewUIMessageBoxBase
{
  public:
    explicit CSeedInvestigatorMenuMsgBox(SessionKeeper &keeper);
    ~CSeedInvestigatorMenuMsgBox();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height) override;
    bool ProcessModernUiInput(const SessionInputEvent &event) override;

    CALLBACK_RESULT AttachSocketBtnDown(class CNewUIMessageBoxBase *pOwner,
                                        const leaf::xstreambuf &xParam);
    CALLBACK_RESULT DetachSocketBtnDown(class CNewUIMessageBoxBase *pOwner,
                                        const leaf::xstreambuf &xParam);
    CALLBACK_RESULT ExitBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void StageModernContent();
    UI::Modern::PC::Inventory::RmlItemDialogPanel m_ModernMenu;
};

class CResetCharacterPointMsgBox : public CNewUICommonMessageBox
{
  public:
    using CNewUICommonMessageBox::CNewUICommonMessageBox;
    bool Create(float priority = 3.f);
    CALLBACK_RESULT ResetCharacterPointBtnDown(CNewUIMessageBoxBase *owner,
                                               const leaf::xstreambuf &parameter);
};

class CDelgardoMainMenuMsgBox : public CNewUIMessageBoxBase
{
  public:
    using CNewUIMessageBoxBase::CNewUIMessageBoxBase;
    explicit CDelgardoMainMenuMsgBox(SessionKeeper &keeper);
    ~CDelgardoMainMenuMsgBox();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();

    CALLBACK_RESULT LButtonUp(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT RegBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT ExchangeBtnDown(class CNewUIMessageBoxBase *pOwner,
                                    const leaf::xstreambuf &xParam);
    CALLBACK_RESULT ExitBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void SetButtonInfo();

    void RenderFrame();
    void RenderTexts();
    void RenderButtons();

    // buttons
    CNewUIMessageBoxButton m_BtnReg{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnExchange{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnExit{SessionOrigin()};

    int m_iMiddleCount;
};

class CTradeZenMsgBoxLayout : public TMsgBoxLayout<CNewUITextInputMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUITextInputMsgBox>::TMsgBoxLayout;
    explicit CTradeZenMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUITextInputMsgBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT ReturnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

    CALLBACK_RESULT ProcessOk(class CNewUIMessageBoxBase *pOwner);
};

class CZenReceiptMsgBoxLayout : public TMsgBoxLayout<CNewUITextInputMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUITextInputMsgBox>::TMsgBoxLayout;
    explicit CZenReceiptMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUITextInputMsgBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT ReturnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    CALLBACK_RESULT ProcessOk(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CZenPaymentMsgBoxLayout : public TMsgBoxLayout<CNewUITextInputMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUITextInputMsgBox>::TMsgBoxLayout;
    explicit CZenPaymentMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUITextInputMsgBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT ReturnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    CALLBACK_RESULT ProcessOk(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CPersonalShopItemValueMsgBoxLayout : public TMsgBoxLayout<CNewUITextInputMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUITextInputMsgBox>::TMsgBoxLayout;
    explicit CPersonalShopItemValueMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUITextInputMsgBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT ReturnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    CALLBACK_RESULT ProcessOk(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    void ApplyItemPrice(int itemPrice);
};

class CPersonalShopNameMsgBox final : public CNewUITextInputMsgBox
{
  public:
    explicit CPersonalShopNameMsgBox(SessionKeeper &keeper) : CNewUITextInputMsgBox(keeper)
    {
    }
    SessionUiUnit &SessionUi() const noexcept;
};

class CPersonalShopNameMsgBoxLayout : public TMsgBoxLayout<CPersonalShopNameMsgBox>
{
    static constexpr float INPUT_WIDTH = 130.0f;
    static constexpr float INPUT_HEIGHT = 12.0f;
    static constexpr int INPUT_TEXTLIMIT = 28;

  public:
    explicit CPersonalShopNameMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CPersonalShopNameMsgBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT ReturnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    CALLBACK_RESULT ProcessOk(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

bool CreatePersonalShopNameMessageBox(SessionKeeper &keeper);

class CCastleWithdrawMsgBoxLayout : public TMsgBoxLayout<CNewUITextInputMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUITextInputMsgBox>::TMsgBoxLayout;
    explicit CCastleWithdrawMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUITextInputMsgBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT ReturnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CStorageLockMsgBoxLayout : public TMsgBoxLayout<CNewUITextInputMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUITextInputMsgBox>::TMsgBoxLayout;
    explicit CStorageLockMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUITextInputMsgBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT ReturnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    CALLBACK_RESULT ProcessOk(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CStorageUnlockMsgBoxLayout : public TMsgBoxLayout<CNewUITextInputMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUITextInputMsgBox>::TMsgBoxLayout;
    explicit CStorageUnlockMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUITextInputMsgBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CPasswordKeyPadMsgBoxLayout : public TMsgBoxLayout<CNewUIKeyPadMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUIKeyPadMsgBox>::TMsgBoxLayout;
    explicit CPasswordKeyPadMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUIKeyPadMsgBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CStorageLockKeyPadMsgBoxLayout : public TMsgBoxLayout<CNewUIKeyPadMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUIKeyPadMsgBox>::TMsgBoxLayout;
    explicit CStorageLockKeyPadMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUIKeyPadMsgBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CStorageLockCheckKeyPadMsgBoxLayout : public TMsgBoxLayout<CNewUIKeyPadMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUIKeyPadMsgBox>::TMsgBoxLayout;
    explicit CStorageLockCheckKeyPadMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUIKeyPadMsgBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CStorageLockFinalKeyPadMsgBoxLayout : public TMsgBoxLayout<CNewUIKeyPadMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUIKeyPadMsgBox>::TMsgBoxLayout;
    explicit CStorageLockFinalKeyPadMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUIKeyPadMsgBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CStorageUnlockKeyPadMsgBoxLayout : public TMsgBoxLayout<CNewUIKeyPadMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUIKeyPadMsgBox>::TMsgBoxLayout;
    explicit CStorageUnlockKeyPadMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CNewUIKeyPadMsgBox>(keeper)
    {
    }
    bool SetLayout();
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);
};

class CUseFruitCheckMsgBoxLayout : public TMsgBoxLayout<CUseFruitCheckMsgBox>
{
  public:
    using TMsgBoxLayout<CUseFruitCheckMsgBox>::TMsgBoxLayout;
    explicit CUseFruitCheckMsgBoxLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CUseFruitCheckMsgBox>(keeper)
    {
    }
    bool SetLayout();
};

class CGemIntegrationMsgBoxLayout : public TMsgBoxLayout<CGemIntegrationMsgBox>
{
  public:
    using TMsgBoxLayout<CGemIntegrationMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CGemIntegrationUnityMsgBoxLayout : public TMsgBoxLayout<CGemIntegrationUnityMsgBox>
{
  public:
    using TMsgBoxLayout<CGemIntegrationUnityMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CGemIntegrationDisjointMsgBoxLayout : public TMsgBoxLayout<CGemIntegrationDisjointMsgBox>
{
  public:
    using TMsgBoxLayout<CGemIntegrationDisjointMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CBloodCastleResultMsgBoxLayout : public TMsgBoxLayout<CBloodCastleResultMsgBox>
{
  public:
    using TMsgBoxLayout<CBloodCastleResultMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CDevilSquareRankMsgBoxLayout : public TMsgBoxLayout<CDevilSquareRankMsgBox>
{
  public:
    using TMsgBoxLayout<CDevilSquareRankMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CChaosCastleResultMsgBoxLayout : public TMsgBoxLayout<CChaosCastleResultMsgBox>
{
  public:
    using TMsgBoxLayout<CChaosCastleResultMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CChaosMixMenuMsgBoxLayout : public TMsgBoxLayout<CChaosMixMenuMsgBox>
{
  public:
    using TMsgBoxLayout<CChaosMixMenuMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CTrainerMenuMsgBoxLayout : public TMsgBoxLayout<CTrainerMenuMsgBox>
{
  public:
    using TMsgBoxLayout<CTrainerMenuMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CTrainerRecoverMsgBoxLayout : public TMsgBoxLayout<CTrainerRecoverMsgBox>
{
  public:
    using TMsgBoxLayout<CTrainerRecoverMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CElpisMsgBoxLayout : public TMsgBoxLayout<CElpisMsgBox>
{
  public:
    using TMsgBoxLayout<CElpisMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CDialogMsgBoxLayout : public TMsgBoxLayout<CDialogMsgBox>
{
  public:
    using TMsgBoxLayout<CDialogMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CCrownSwitchPopLayout : public TMsgBoxLayout<CProgressMsgBox>
{
  public:
    using TMsgBoxLayout<CProgressMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CCrownSwitchPushLayout : public TMsgBoxLayout<CProgressMsgBox>
{
  public:
    using TMsgBoxLayout<CProgressMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CCrownSwitchOtherPushLayout : public TMsgBoxLayout<CProgressMsgBox>
{
  public:
    using TMsgBoxLayout<CProgressMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CSealRegisterStartLayout : public TMsgBoxLayout<CProgressMsgBox>
{
  public:
    using TMsgBoxLayout<CProgressMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CSealRegisterSuccessLayout : public TMsgBoxLayout<CProgressMsgBox>
{
  public:
    using TMsgBoxLayout<CProgressMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CSealRegisterFailLayout : public TMsgBoxLayout<CProgressMsgBox>
{
  public:
    using TMsgBoxLayout<CProgressMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CSealRegisterOtherLayout : public TMsgBoxLayout<CProgressMsgBox>
{
  public:
    using TMsgBoxLayout<CProgressMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CSealRegisterOtherCampLayout : public TMsgBoxLayout<CProgressMsgBox>
{
  public:
    using TMsgBoxLayout<CProgressMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CCrownDefenseRemoveLayout : public TMsgBoxLayout<CProgressMsgBox>
{
  public:
    using TMsgBoxLayout<CProgressMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CCrownDefenseCreateLayout : public TMsgBoxLayout<CProgressMsgBox>
{
  public:
    using TMsgBoxLayout<CProgressMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CCursedTempleHolicItemGetLayout : public TMsgBoxLayout<CCursedTempleProgressMsgBox>
{
  public:
    using TMsgBoxLayout<CCursedTempleProgressMsgBox>::TMsgBoxLayout;
    explicit CCursedTempleHolicItemGetLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CCursedTempleProgressMsgBox>(keeper)
    {
    }
    bool SetLayout();
};

class CCursedTempleHolicItemSaveLayout : public TMsgBoxLayout<CCursedTempleProgressMsgBox>
{
  public:
    using TMsgBoxLayout<CCursedTempleProgressMsgBox>::TMsgBoxLayout;
    explicit CCursedTempleHolicItemSaveLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CCursedTempleProgressMsgBox>(keeper)
    {
    }
    bool SetLayout();
};

class CSystemMenuMsgBoxLayout : public TMsgBoxLayout<CSystemMenuMsgBox>
{
  public:
    using TMsgBoxLayout<CSystemMenuMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

bool CreateSystemMenuMessageBox(SessionKeeper &keeper);

class CGuildBreakPasswordMsgBoxLayout : public TMsgBoxLayout<CNewUITextInputMsgBox>
{
  public:
    using TMsgBoxLayout<CNewUITextInputMsgBox>::TMsgBoxLayout;
    explicit CGuildBreakPasswordMsgBoxLayout(SessionKeeper &keeper, std::wstring member = {})
        : TMsgBoxLayout<CNewUITextInputMsgBox>(keeper), member_(std::move(member))
    {
    }
    bool SetLayout();
    CALLBACK_RESULT ReturnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OkBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    std::wstring member_;
    bool submitted_ = false;
    CALLBACK_RESULT ProcessOk(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
};

class CGuild_ToPerson_PositionLayout : public TMsgBoxLayout<CGuild_ToPerson_Position>
{
  public:
    using TMsgBoxLayout<CGuild_ToPerson_Position>::TMsgBoxLayout;
    bool SetLayout();
};

class CDuelMsgBoxLayout : public TMsgBoxLayout<CDuelMsgBox>
{
  public:
    using TMsgBoxLayout<CDuelMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CDuelResultMsgBoxLayout : public TMsgBoxLayout<CDuelResultMsgBox>
{
  public:
    using TMsgBoxLayout<CDuelResultMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CCherryBlossomMsgBoxLayout : public TMsgBoxLayout<CCherryBlossomMsgBox>
{
  public:
    using TMsgBoxLayout<CCherryBlossomMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

bool CreateCherryBlossomMessageBox(SessionKeeper &keeper);

class CSeedMasterMenuMsgBoxLayout : public TMsgBoxLayout<CSeedMasterMenuMsgBox>
{
  public:
    using TMsgBoxLayout<CSeedMasterMenuMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};
class CSeedInvestigatorMenuMsgBoxLayout : public TMsgBoxLayout<CSeedInvestigatorMenuMsgBox>
{
  public:
    using TMsgBoxLayout<CSeedInvestigatorMenuMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CResetCharacterPointMsgBoxLayout : public TMsgBoxLayout<CResetCharacterPointMsgBox>
{
  public:
    using TMsgBoxLayout<CResetCharacterPointMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CDelgardoMainMenuMsgBoxLayout : public TMsgBoxLayout<CDelgardoMainMenuMsgBox>
{
  public:
    using TMsgBoxLayout<CDelgardoMainMenuMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};

class CLuckyTradeMenuMsgBoxLayout : public TMsgBoxLayout<CLuckyTradeMenuMsgBox>
{
  public:
    using TMsgBoxLayout<CLuckyTradeMenuMsgBox>::TMsgBoxLayout;
    bool SetLayout();
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIHelpWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIHelpWindow(SessionKeeper &keeper);
    ~CNewUIHelpWindow();
    bool Create(CNewUIManager *manager, int x, int y);
    void Release();
    void SetPos(int x, int y);
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    float GetLayerDepth();
    float GetKeyEventOrder();
    void OpenningProcess();
    void ClosingProcess();
    void AutoUpdateIndex();

  private:
    void StageContent();
    CNewUIManager *manager_ = nullptr;
    SessionRenderUnit &renderer_;
    UI::Modern::PC::Help::RmlHelpPanel panel_;
    UI::Modern::PC::Help::RmlHelpPanel::Content content_;
    std::string locale_;
    int page_ = 0;
    bool visible_ = false;
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIWindowMenu : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIWindowMenu(SessionKeeper &keeper);
    ~CNewUIWindowMenu();
    bool Create(CNewUIManager *manager, int x, int y);
    void Release();
    void SetPos(int x, int y);
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    float GetLayerDepth();
    float GetKeyEventOrder();
    void OpenningProcess();
    void ClosingProcess();

  private:
    void ExecuteCommand(int command);
    void ToggleMiniMap();
    void StageContent();
    CNewUIManager *manager_ = nullptr;
    SessionRenderUnit &renderer_;
    UI::Modern::RmlContextMenuPanel panel_;
    UI::Modern::RmlContextMenuPanel::Content content_;
};
} // namespace SEASON3B

class ReconnectManager;
class SessionKeeper;

namespace UI
{
class ReconnectDialog;

class ReconnectDialogLegacyCalls : protected SessionUiLegacyBindings
{
  protected:
    ReconnectDialogLegacyCalls(SessionKeeper &keeper, ReconnectDialog &owner) noexcept;

    bool NativeSkinAvailable();                                      // OMF-01908
    void FillWhite(float x, float y, float w, float h, float alpha); // OMF-01909
    void FillBlack(float x, float y, float w, float h, float alpha); // OMF-01910
    void DrawCenteredText(float x, float y, float width, LegacyFontRole role,
                          const wchar_t *text); // OMF-01911

  private:
    ReconnectDialog &owner_;
};

class ReconnectDialog final : protected ReconnectDialogLegacyCalls
{
    friend class ReconnectDialogLegacyCalls;

  public:
    explicit ReconnectDialog(SessionKeeper &keeper) noexcept;
    void Update(ReconnectManager &reconnect);
    void Render(ReconnectManager &reconnect);

  private:
    void FillWhite(float x, float y, float width, float height, float alpha);
    void FillBlack(float x, float y, float width, float height, float alpha);
    void DrawCenteredText(float x, float y, float width, LegacyFontRole role, const wchar_t *text);
    bool CancelHovered() const;
    void DrawBackdrop(ReconnectManager &reconnect);
    float Progress(ReconnectManager &reconnect) const;
    void DrawStatusTexts(ReconnectManager &reconnect);
    bool NativeSkinAvailable() const;
    void DrawNative(ReconnectManager &reconnect, bool cancelHovered);
    void DrawFallback(ReconnectManager &reconnect, bool cancelHovered);
};
} // namespace UI

class SessionKeeper;

namespace SEASON3B
{
class CNewUIMessageBoxMng;
}

SEASON3B::CNewUIMessageBoxMng *CreateSessionMessageBoxManager(SessionKeeper &keeper);
void DestroySessionMessageBoxManager(SEASON3B::CNewUIMessageBoxMng *messageBoxManager) noexcept;

class ApplicationConfigUnit;
class ApplicationAudio;
class AppWindow;
class SessionRenderUnit;
struct SessionInputEvent;

namespace SEASON3B
{
class CNewUIOptionWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
  public:
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
    enum IMAGE_LIST
    {
        IMAGE_OPTION_FRAME_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK,
        IMAGE_OPTION_BTN_CLOSE = CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_CLOSE,
        IMAGE_OPTION_FRAME_DOWN = BITMAP_INVENTORY_BACK_BOTTOM,

        IMAGE_OPTION_FRAME_UP = BITMAP_OPTION_BEGIN,
        IMAGE_OPTION_FRAME_LEFT,
        IMAGE_OPTION_FRAME_RIGHT,
        IMAGE_OPTION_LINE,
        IMAGE_OPTION_POINT,
        IMAGE_OPTION_BTN_CHECK,
        IMAGE_OPTION_EFFECT_BACK,
        IMAGE_OPTION_EFFECT_COLOR,
        IMAGE_OPTION_VOLUME_BACK,
        IMAGE_OPTION_VOLUME_COLOR,
    };

  public:
    explicit CNewUIOptionWindow(SessionKeeper &keeper);
    virtual ~CNewUIOptionWindow();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    float GetLayerDepth();    //. 10.5f
    float GetKeyEventOrder(); // 10.f;

    void OpenningProcess();
    void ClosingProcess();
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight);
    bool ProcessModernUiInput(const SessionInputEvent &event);

    void SetAutoAttack(bool bAuto);
    bool IsAutoAttack();
    void SetWhisperSound(bool bSound);
    bool IsWhisperSound();
    void SetSlideHelp(bool bHelp);
    bool IsSlideHelp();
    void SetVolumeLevel(int iVolume);
    int GetVolumeLevel();
    void SetRenderLevel(int iRender);
    int GetRenderLevel();
    void SetRenderAllEffects(bool bRenderAllEffects);
    bool GetRenderAllEffects();

  private:
    void OnSoundVolumeChanged();
    void OnMusicVolumeChanged();
    void ApplyPendingChanges();
    UI::Modern::PC::Option::RmlOptionValues PanelValues() const;
    UI::Modern::PC::Option::RmlOptionContent PanelContent() const;

  private:
    CNewUIManager *m_pNewUIMng;
    ApplicationConfigUnit &m_applicationConfig;
    ApplicationAudio &m_applicationAudio;
    AppWindow &m_appWindow;
    SessionRenderUnit &m_renderer;
    POINT m_Pos;
    UI::Modern::PC::Option::RmlOptionPanel m_modernPanel;

    bool m_bAutoAttack;
    bool m_bWhisperSound;
    bool m_bNameDisplay;
    bool m_bSlideHelp;
    int m_iVolumeLevel; // Sound volume (0=off, 10=max)
    int m_iMusicLevel;  // Music volume (0=off, 10=max)
    int m_iRenderLevel;
    bool m_bRenderAllEffects;
    int m_iResolutionIndex;
    bool m_bWindowedMode;
    int m_iLanguageIndex;
    int m_iFontIndex;

    void ApplyResolution();
    int FindCurrentResolutionIndex();
    void SyncResolutionComboToWindow();
    void ApplyWindowModeToggle();

    void ApplyLanguage();
    int FindCurrentLanguageIndex();

    void ApplyFont();
    int FindCurrentFontIndex();
};
} // namespace SEASON3B

class CMsgWin : public CWin
{
    friend class CharacterRetirementTestPeer;

  protected:
    enum MSG_WIN_TYPE
    {
        MWT_NON,
        MWT_BTN_CANCEL,
        MWT_BTN_OK,
        MWT_BTN_BOTH,
        MWT_STR_INPUT,
    };

    UI::Modern::RmlMessageBoxPanel m_panel;
    wchar_t m_aszMsg[MW_MSG_LINE_MAX][MW_MSG_ROW_MAX];
    int m_nMsgLine;
    int m_nMsgCode;
    MSG_WIN_TYPE m_eType;
    short m_nGameExit;
    double m_dDeltaTickSum;
    bool enterKeyHeld_ = false;
    bool escapeKeyHeld_ = false;

  public:
    static int Width()
    {
        return UI::Modern::RmlMessageBoxPanel::Width();
    }
    static int Height()
    {
        return UI::Modern::RmlMessageBoxPanel::Height();
    }

    explicit CMsgWin(SessionKeeper &keeper);
    ~CMsgWin() override;
    void Create();
    void SetPosition(int nXCoord, int nYCoord) override;
    void Show(bool bShow) override;
    bool CursorInWin(int nArea) override;
    void PopUp(int nMsgCode, wchar_t *pszMsg = nullptr);
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const;

  protected:
    void PreRelease() override;
    void UpdateWhileActive(double dDeltaTick) override;
    void RenderControls() override;
    void SetMsg(MSG_WIN_TYPE eType, std::wstring lpszMsg, std::wstring lpszMsg2 = L"");
    void ManageOKClick();
    void ManageCancelClick();
    void InitResidentNumInput();
    void RequestDeleteCharacter();

    static UI::Modern::RmlMessageBoxMode PanelMode(MSG_WIN_TYPE type) noexcept;
};

class CServerMsgWin : public CWin
{
  protected:
    UI::Modern::PC::ServerMessage::RmlServerMessagePanel panel_;
    UI::Modern::PC::ServerMessage::RmlServerMessageContent content_;

  public:
    explicit CServerMsgWin(SessionKeeper &keeper);
    ~CServerMsgWin() override;

    void Create();
    void SetPosition(int x, int y) override;
    void Show(bool show) override;
    bool CursorInWin(int nArea) override;

    void AddMsg(wchar_t *pszMsg);
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight);

  protected:
    void PreRelease() override;
    void RenderControls() override;
};

class CSysMenuWin : public CWin
{
  public:
    explicit CSysMenuWin(SessionKeeper &keeper);
    ~CSysMenuWin() override;

    void Create();
    void SetPosition(int x, int y) override;
    void Show(bool show) override;
    bool CursorInWin(int area) override;
    bool ProcessModernUiInput(const SessionInputEvent &event);
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight);

  protected:
    void PreRelease() override;
    void UpdateWhileShow(double deltaTick) override;
    void RenderControls() override;

  private:
    void ExitGame();
    void SelectServer();
    void OpenOptions();

    UI::Modern::PC::SystemMenu::RmlSystemMenuPanel m_modernPanel;
    UI::Modern::PC::SystemMenu::RmlSystemMenuMode mode_ =
        UI::Modern::PC::SystemMenu::RmlSystemMenuMode::Login;
};

namespace HelpPanelDetail
{
using namespace SEASON3B;
std::vector<std::wstring> HelpColumns(std::wstring_view line);
} // namespace HelpPanelDetail

namespace WindowMenuDetail
{
using namespace SEASON3B;
#pragma pack(push)
#pragma pack()
inline constexpr std::array WindowMenuTextIds{1741, 1742, 364, 1743, 3055, 3103};
#pragma pack(pop)

} // namespace WindowMenuDetail
