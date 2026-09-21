#pragma once
#include "domain/ItemsSkills.h"
#include "domain/Shop.h"
#include "render/UiAdapter.h"
#include "session/SessionNetwork.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Hud/HudRender.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/World/WorldRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <vector>

// - ����

class CErrorReport;
class SessionUiUnit;

inline POINT MakePos(long x, long y)
{
    POINT pos = {x, y};
    return pos;
}
inline SIZE MakeSize(int cx, int cy)
{
    SIZE size = {cx, cy};
    return size;
}

//. CPersonalShopTitleImp
class CPersonalShopTitleImp : protected SessionUiLegacyBindings
{
    class CShopTitleDrawObj : protected SessionUiLegacyBindings
    {
      public:
        enum
        {
            EXTRA_SPACE = 25
        };

        explicit CShopTitleDrawObj(SessionKeeper &keeper);
        ~CShopTitleDrawObj();

        bool Create(int key, const std::wstring &name, const std::wstring &title, POINT pos);
        void Release();

        int GetKey() const;

        void SetBoxContent(const std::wstring &name, const std::wstring &title);
        void SetBoxPos(POINT pos);
        void GetFullTitle(std::wstring &title);

        void GetBoxSize(SIZE &size);
        void GetBoxPos(POINT &pos);
        void GetBoxRect(RECT &rect);

        void EnableDraw();
        void DisableDraw();
        bool IsVisible() const;

        void EnableHighlight();
        void DisableHighlight();
        bool IsHighlight() const;
        void FillModernLabel(UI::Modern::RmlPlayerName &label) const;

      private:
        void Init();
        void SeparateShopTitle(IN const std::wstring &title, OUT std::wstring &topTitle,
                               OUT std::wstring &bottomTitle);
        void CalculateBooleanSize(IN const std::wstring &name, IN const std::wstring &topTitle,
                                  IN const std::wstring &bottomTitle, OUT SIZE &size);

      private:
        std::wstring m_fullname;
        std::wstring m_fulltitle;
        std::wstring m_topTitle;
        std::wstring m_bottomTitle;

        int m_key;
        bool m_bDraw;
        POINT m_pos;
        SIZE m_size;
        bool m_bHighlight;
    };

  public:
    ~CPersonalShopTitleImp();

    bool AddShopTitle(int key, CHARACTER *pPlayer, const std::wstring &title);
    void RemoveShopTitle(CHARACTER *pPlayer);
    void RemoveAllShopTitle();
    void RemoveAllShopTitleExceptHero();

    CHARACTER *FindCharacter(int key) const;

    void ShowShopTitles();
    void HideShopTitles();
    bool IsShowShopTitles() const;

    void EnableShopTitleDraw(CHARACTER *pPlayer);
    void DisableShopTitleDraw(CHARACTER *pPlayer);
    bool IsShopTitleVisible(CHARACTER *pPlayer);

    bool IsShopTitleHighlight(CHARACTER *pPlayer) const;

    bool IsInViewport(CHARACTER *pPlayer);
    void GetShopTitle(CHARACTER *pPlayer, std::wstring &title);
    void GetShopTitleSummary(CHARACTER *pPlayer, std::wstring &summary);

    void Update();
    void Draw();

  protected:
    friend class SessionUiUnit;

    explicit CPersonalShopTitleImp(SessionKeeper &keeper);

    void UpdatePosition();
    void RevisionPosition();
    void CheckKeyIntegrity();
    void StageModernLabels();
    void UpdateHighlight(CShopTitleDrawObj &drawObject, CHARACTER *player);

    void CalculateBooleanPos(IN CHARACTER *pPlayer, IN const SIZE &size, OUT POINT &pos);

  private:
    CameraProjection &cameraProjection_;
    typedef std::map<CHARACTER *, CShopTitleDrawObj *> type_drawobj_map;
    type_drawobj_map m_listShopTitleDrawObj;
    UI::Modern::RmlPlayerNameRequest modernRequest_;

    int m_iHighlightFrame;
    bool m_bShow;
};

inline bool CheckPriceIntegrity(const wchar_t *szZen, int size)
{
    if (size > 255)
        return false;
    for (int i = 0; i < size; i++)
    {
        if (szZen[i] == '\0')
            break;
        if (szZen[i] & 0x80)
        {
            return false;
        }
    }
    return true;
}

#define INDEX_COMPILED_CELE ITEM_WING + 30
#define INDEX_COMPILED_SOUL ITEM_WING + 31
#define MAX_LINE_UNMIXLIST 8
#define INDEX_NPC_LAHAP 9

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

using namespace SEASON3B;

class CMsgBoxIGSBuyConfirm : public CNewUIMessageBoxBase
{
  public:
    using CNewUIMessageBoxBase::CNewUIMessageBoxBase;
    enum IMAGE_IGS_BUY_CONFIRM
    {
        IMAGE_IGS_BUTTON = BITMAP_IGS_MSGBOX_BUTTON,
        IMAGE_IGS_BACK = CNewUIOptionWindow::IMAGE_OPTION_FRAME_BACK,
        IMAGE_IGS_UP = CNewUIOptionWindow::IMAGE_OPTION_FRAME_UP,
        IMAGE_IGS_DOWN = CNewUIOptionWindow::IMAGE_OPTION_FRAME_DOWN,
        IMAGE_IGS_LEFTLINE = CNewUIOptionWindow::IMAGE_OPTION_FRAME_LEFT,
        IMAGE_IGS_RIGHTLINE = CNewUIOptionWindow::IMAGE_OPTION_FRAME_RIGHT,
        IMAGE_IGS_TEXTBOX = BITMAP_IGS_MGSBOX_BUY_CONFIRM_TEXT_BOX,
    };

    enum IMAGESIZE_IGS_BUY_CONFIRM
    {
        IMAGE_IGS_WINDOW_WIDTH = 640,
        IMAGE_IGS_WINDOW_HEIGHT = 429,
        IMAGE_IGS_FRAME_WIDTH = 190,
        IMAGE_IGS_FRAME_HEIGHT = 179,
        IMAGE_IGS_TEXTBOX_WIDTH = 160,
        IMAGE_IGS_TEXTBOX_HEIGHT = 41,
        IMAGE_IGS_UP_HEIGHT = 64,
        IMAGE_IGS_DOWN_HEIGHT = 45,
        IMAGE_IGS_LINE_WIDTH = 21,
        IMAGE_IGS_LINE_HEIGHT = 10,
        IMAGE_IGS_BTN_WIDTH = 52,
        IMAGE_IGS_BTN_HEIGHT = 26,
    };

    enum IGS_BUY_CONFIRM_POS
    {
        IGS_BTN_OK_POS_X = 35,
        IGS_BTN_CANCEL_POS_X = 105,
        IGS_BTN_POS_Y = 140,
        IGS_TEXTBOX_POS_X = 15,
        IGS_TEXTBOX_POS_Y = 58,
        IGS_TEXT_TITLE_POS_Y = 10,
        IGS_TEXT_QUESTION_POS_Y = 42,
        IGS_TEXT_NOTICE_POS_Y = 105,
        IGS_TEXT_NOTICE_WIDTH = 150,
        IGS_TEXT_ITEM_INFO_POS_X = 25,
        IGS_TEXT_ITEM_INFO_NAME_POS_Y = 64,
        IGS_TEXT_ITEM_INFO_PRICE_POS_Y = 75,
        IGS_TEXT_ITEM_INFO_PERIOD_POS_Y = 86,
        IGS_TEXT_ITEM_INFO_WIDTH = 143,
    };

  public:
    explicit CMsgBoxIGSBuyConfirm(SessionKeeper &keeper);
    ~CMsgBoxIGSBuyConfirm();
    bool Create(float fPriority = 3.f);
    void Release();
    bool Update();
    bool Render();

    void Initialize(WORD wItemCode, int iPackageSeq, int iDisplaySeq, int iPriceSeq, int iCashType,
                    wchar_t *pszName, wchar_t *pszPrice, wchar_t *pszPeriod);
    CALLBACK_RESULT LButtonUp(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                 const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelButtonDown(class CNewUIMessageBoxBase *pOwner,
                                     const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void SetButtonInfo();
    void RenderFrame();
    void RenderTexts();
    void RenderButtons();
    void LoadImages();
    void UnloadImages();

  private:
    CNewUIMessageBoxButton m_BtnOk{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnCancel{SessionOrigin()};

    int m_iMiddleCount;
    WORD m_wItemCode;
    int m_iPackageSeq;
    int m_iDisplaySeq;
    int m_iPriceSeq;
    int m_iCashType;

    wchar_t m_szItemName[MAX_TEXT_LENGTH];
    wchar_t m_szItemPrice[MAX_TEXT_LENGTH];
    wchar_t m_szItemPeriod[MAX_TEXT_LENGTH];
    wchar_t m_szNotice[NUM_LINE_CMB][MAX_TEXT_LENGTH];

    int m_iNumNoticeLine;
};

class CMsgBoxIGSBuyConfirmLayout : public TMsgBoxLayout<CMsgBoxIGSBuyConfirm>
{
  public:
    using TMsgBoxLayout<CMsgBoxIGSBuyConfirm>::TMsgBoxLayout;
    bool SetLayout();
};

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

using namespace SEASON3B;

class CMsgBoxIGSBuyPackageItem : public CNewUIMessageBoxBase, public INewUI3DRenderObj
{
  public:
    using CNewUIMessageBoxBase::CNewUIMessageBoxBase;
    enum IMAGE_IGS_BUY_PACKAGE_ITEM
    {
        IMAGE_IGS_FRAME = BITMAP_IGS_MSGBOX_BUY_PACKAGE_ITEM,
        IMAGE_IGS_BUTTON = BITMAP_IGS_MSGBOX_BUTTON,
    };

    enum IMAGESIZE_IGS_BUY_PACKAGE_ITEM
    {
        IMAGE_IGS_WINDOW_WIDTH = 640,
        IMAGE_IGS_WINDOW_HEIGHT = 429,
        IMAGE_IGS_FRAME_WIDTH = 198,
        IMAGE_IGS_FRAME_HEIGHT = 291,
        IMAGE_IGS_BTN_WIDTH = 52,
        IMAGE_IGS_BTN_HEIGHT = 26,
    };

    enum IGS_BUY_PACKAGE_ITEM_POS
    {
        IGS_BTN_BUY_POS_X = 18,
        IGS_BTN_PRESENT_POS_X = 74,
        IGS_BTN_CANCEL_POS_X = 130,
        IGS_BTN_POS_Y = 253,
        IGS_TEXT_TITLE_POS_Y = 10,
        IGS_TEXT_NAME_POS_X = 5,
        IGS_TEXT_NAME_POS_Y = 100,
        IGS_TEXT_NAME_WIDTH = 196,
        IGS_TEXT_PRICE_POS_X = 118,
        IGS_TEXT_PRICE_POX_Y = 229,
        IGS_TEXT_PRICE_WIDTH = 66,
        IGS_LISTBOX_POS_X = 14,
        IGS_LISTBOX_POS_Y = 216,
        IGS_LISTBOX_WIDTH = 158,
        IGS_3DITEM_POS_X = 50,
        IGS_3DITEM_POS_Y = 34,
        IGS_3DITEM_WIDTH = 96,
        IGS_3DITEM_HEIGHT = 60,
    };

  public:
    explicit CMsgBoxIGSBuyPackageItem(SessionKeeper &keeper);
    virtual ~CMsgBoxIGSBuyPackageItem();

    bool Create(float fPriority = 3.f);
    void Release();
    bool Update();
    bool Render();

    bool IsVisible() const;

    void Render3D();

    void Initialize(CShopPackage *pPackage);
    SessionKeeper &OriginatingSession() const noexcept
    {
        return SessionOrigin();
    }

    CALLBACK_RESULT LButtonUp(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT BuyBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT PresentBtnDown(class CNewUIMessageBoxBase *pOwner,
                                   const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void SetButtonInfo();
    void RenderFrame();
    void RenderTexts();
    void RenderButtons();

    void CreateListBox();
    void RenderListBox();
    void ListBoxDoAction();
    void ReleaseListBox();

#ifdef LEM_FIX_WARNINGMSG_BUYITEM
    bool Add_WarningMsgBuyItem(int _nItemIndex);
#endif // LEM_FIX_WARNINGMSG_BUYITEM

    void LoadImages();
    void UnloadImages();

  private:
    CInGameShopSystem &g_InGameShopSystem;
    CNewUIMessageBoxButton m_BtnBuy{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnPresent{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnCancel{SessionOrigin()};
    CUIBuyingListBox m_PackageInfo;

    int m_iPackageSeq;
    int m_iDisplaySeq;
    WORD m_wItemCode;
    int m_iCashType;

    wchar_t m_szPackageName[MAX_TEXT_LENGTH];
    wchar_t m_szPrice[MAX_TEXT_LENGTH];
    wchar_t m_szPeriod[MAX_TEXT_LENGTH];
    wchar_t m_szDescription[UIMAX_TEXT_LINE][MAX_TEXT_LENGTH];
};

class CMsgBoxBuyPackageItemLayout : public TMsgBoxLayout<CMsgBoxIGSBuyPackageItem>
{
  public:
    using TMsgBoxLayout<CMsgBoxIGSBuyPackageItem>::TMsgBoxLayout;
    explicit CMsgBoxBuyPackageItemLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CMsgBoxIGSBuyPackageItem>(keeper)
    {
    }
    bool SetLayout();
};

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

using namespace SEASON3B;

class CMsgBoxIGSBuySelectItem : public CNewUIMessageBoxBase, public INewUI3DRenderObj
{
  public:
    using CNewUIMessageBoxBase::CNewUIMessageBoxBase;
    explicit CMsgBoxIGSBuySelectItem(SessionKeeper &keeper);
    virtual ~CMsgBoxIGSBuySelectItem();

  private:
    enum IMAGE_IGS_BUY_SELECT_ITEM
    {
        IMAGE_IGS_MGSBOX_BACK = BITMAP_IGS_MSGBOX_BUY_SELECT_ITEM,
        IMAGE_IGS_BUTTON = BITMAP_IGS_MSGBOX_BUTTON,
    };

    enum IMAGESIZE_IGS_BUY_SELECT_ITEM
    {
        IMAGE_IGS_WINDOW_WIDTH = 640,
        IMAGE_IGS_WINDOW_HEIGHT = 429,
        IMAGE_IGS_FRAME_WIDTH = 215,
        IMAGE_IGS_FRAME_HEIGHT = 346,
        IMAGE_IGS_BTN_WIDTH = 52,
        IMAGE_IGS_BTN_HEIGHT = 26,
    };

    enum IGS_BUY_SELECT_ITEM
    {
        IGS_BTN_BUY_POS_X = 26,
        IGS_BTN_PRESENT_POS_X = 82,
        IGS_BTN_CANCEL_POS_X = 138,
        IGS_BTN_POS_Y = 309,
        IGS_TEXT_TITLE_POS_Y = 10,
        IGS_TEXT_NAME_POS_X = 5,
        IGS_TEXT_NAME_POS_Y = 100,
        IGS_TEXT_ATTR_POS_X = 15,
        IGS_TEXT_ATTR_POS_Y = 118,
        IGS_TEXT_ATTR_WIDTH = 194,
        IGS_TEXT_PRICE_POS_X = 129,
        IGS_TEXT_PRICE_POX_Y = 288,
        IGS_TEXT_PRICE_WIDTH = 66,
        IGS_TEXT_DISCRIPTION_WIDTH = 185,
        IGS_LISTBOX_POS_X = 17,
        IGS_LISTBOX_POS_Y = 277,
        IGS_3DITEM_POS_X = 60,
        IGS_3DITEM_POS_Y = 33,
        IGS_3DITEM_WIDTH = 96,
        IGS_3DITEM_HEIGHT = 60,
    };

  public:
    bool Create(float fPriority = 3.f);
    void Release();
    bool Update();
    bool Render();

    void Initialize(CShopPackage *pPackage);
    SessionKeeper &OriginatingSession() const noexcept
    {
        return SessionOrigin();
    }

    void Render3D();
    bool IsVisible() const;

    CALLBACK_RESULT LButtonUp(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT BuyBtnDown(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT PresentBtnDown(class CNewUIMessageBoxBase *pOwner,
                                   const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelBtnDown(class CNewUIMessageBoxBase *pOwner,
                                  const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void SetButtonInfo();
    void RenderFrame();
    void RenderTexts();
    void RenderButtons();
    void LoadImages();
    void UnloadImages();
    void CreateListBox();
    void RenderListBox();
    void ReleaseListBox();
    void ListBoxDoAction();
    void AddData(int iPackageSeq, int iDisplaySeq, int iPriceSeq, int iProductSeq,
                 wchar_t *pszPriceUnit, int iCashType);

  private:
    CInGameShopSystem &g_InGameShopSystem;
    CNewUIMessageBoxButton m_BtnBuy{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnPresent{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnCancel{SessionOrigin()};
    CUIPackCheckBuyingListBox m_SelectBuyListBox;

    int m_iPackageSeq;
    int m_iDisplaySeq;
    WORD m_wItemCode;
    int m_iDescriptionLine;

    wchar_t m_szPackageName[MAX_TEXT_LENGTH];
    wchar_t m_szPrice[MAX_TEXT_LENGTH];
    wchar_t m_szDescription[UIMAX_TEXT_LINE][MAX_TEXT_LENGTH];
};

class CMsgBoxIGSBuySelectItemLayout : public TMsgBoxLayout<CMsgBoxIGSBuySelectItem>
{
  public:
    using TMsgBoxLayout<CMsgBoxIGSBuySelectItem>::TMsgBoxLayout;
    explicit CMsgBoxIGSBuySelectItemLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CMsgBoxIGSBuySelectItem>(keeper)
    {
    }
    bool SetLayout();
};

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

using namespace SEASON3B;

class CMsgBoxIGSCommon : public CNewUIMessageBoxBase
{
  public:
    using CNewUIMessageBoxBase::CNewUIMessageBoxBase;
    enum IMAGE_IGS_COMMON
    {
        IMAGE_IGS_BUTTON = BITMAP_IGS_MSGBOX_BUTTON,
        IMAGE_IGS_BACK = CNewUIOptionWindow::IMAGE_OPTION_FRAME_BACK,
        IMAGE_IGS_UP = CNewUIOptionWindow::IMAGE_OPTION_FRAME_UP,
        IMAGE_IGS_DOWN = CNewUIOptionWindow::IMAGE_OPTION_FRAME_DOWN,
        IMAGE_IGS_LEFTLINE = CNewUIOptionWindow::IMAGE_OPTION_FRAME_LEFT,
        IMAGE_IGS_RIGHTLINE = CNewUIOptionWindow::IMAGE_OPTION_FRAME_RIGHT,
    };

    enum IMAGESIZE_IGS_COMMON
    {
        IMAGE_IGS_WINDOW_WIDTH = 640,
        IMAGE_IGS_WINDOW_HEIGHT = 429,
        IMAGE_IGS_FRAME_WIDTH = 190,
        IMAGE_IGS_FRAME_HEIGHT = 109,
        IMAGE_IGS_UP_HEIGHT = 64,
        IMAGE_IGS_DOWN_HEIGHT = 45,
        IMAGE_IGS_LINE_WIDTH = 21,
        IMAGE_IGS_LINE_HEIGHT = 10,
        IMAGE_IGS_BTN_WIDTH = 52,
        IMAGE_IGS_BTN_HEIGHT = 26,
    };

    enum IGS_COMMON_POS
    {
        IGS_TEXT_TITLE_POS_Y = 10,
        IGS_TEXT_ITEM_INFO_POS_X = 10,
        IGS_TEXT_ITEM_INFO_POS_Y = 40,
        IGS_TEXT_ITEM_INFO_WIDTH = 170,
        IGS_BTN_POS_Y = 5,
    };

    enum IGS_COMMON_ETC
    {
        IGS_NUM_TEXT_LIMIT_RENDER_MIDDLE_LINE = 3,
    };

  public:
    explicit CMsgBoxIGSCommon(SessionKeeper &keeper);
    ~CMsgBoxIGSCommon();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();

    void Initialize(const wchar_t *pszTitle, const wchar_t *pszText);

    CALLBACK_RESULT LButtonUp(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                 const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelButtonDown(class CNewUIMessageBoxBase *pOwner,
                                     const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void SetButtonInfo();

    void RenderFrame();
    void RenderTexts();
    void RenderButtons();

    void LoadImages();
    void UnloadImages();

  private:
    CNewUIMessageBoxButton m_BtnOk{SessionOrigin()};

    wchar_t m_szTitle[MAX_TEXT_LENGTH];
    wchar_t m_szText[NUM_LINE_CMB][MAX_TEXT_LENGTH];

    int m_iMsgBoxWidth;
    int m_iMsgBoxHeight;
    int m_iMiddleCount;
    int m_iNumTextLine;

  public:
};

class CMsgBoxIGSCommonLayout : public TMsgBoxLayout<CMsgBoxIGSCommon>
{
  public:
    using TMsgBoxLayout<CMsgBoxIGSCommon>::TMsgBoxLayout;
    bool SetLayout();
};

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

using namespace SEASON3B;

class CMsgBoxIGSDeleteItemConfirm : public CNewUIMessageBoxBase
{
  public:
    using CNewUIMessageBoxBase::CNewUIMessageBoxBase;
    enum IMAGE_IGS_DEL_ITEM_CONFIRM
    {
        IMAGE_IGS_BUTTON = BITMAP_IGS_MSGBOX_BUTTON,
        IMAGE_IGS_BACK = CNewUIOptionWindow::IMAGE_OPTION_FRAME_BACK,
        IMAGE_IGS_UP = CNewUIOptionWindow::IMAGE_OPTION_FRAME_UP,
        IMAGE_IGS_DOWN = CNewUIOptionWindow::IMAGE_OPTION_FRAME_DOWN,
        IMAGE_IGS_LEFTLINE = CNewUIOptionWindow::IMAGE_OPTION_FRAME_LEFT,
        IMAGE_IGS_RIGHTLINE = CNewUIOptionWindow::IMAGE_OPTION_FRAME_RIGHT,
    };

    enum IMAGESIZE_IGS_DEL_ITEM_CONFIRM
    {
        IMAGE_IGS_WINDOW_WIDTH = 640,
        IMAGE_IGS_WINDOW_HEIGHT = 429,
        IMAGE_IGS_FRAME_WIDTH = 190,
        IMAGE_IGS_FRAME_HEIGHT = 149,
        IMAGE_IGS_UP_HEIGHT = 64,
        IMAGE_IGS_DOWN_HEIGHT = 45,
        IMAGE_IGS_LINE_WIDTH = 21,
        IMAGE_IGS_LINE_HEIGHT = 10,
        IMAGE_IGS_BTN_WIDTH = 52,
        IMAGE_IGS_BTN_HEIGHT = 26,
    };

    enum IGS_DEL_ITEM_CONFIRM_POS
    {
        IGS_BTN_DEL_POS_X = 33,
        IGS_BTN_CANCEL_POS_X = 105,
        IGS_BTN_POS_Y = 110,
        IGS_TEXT_TITLE_Y = 10,
        IGS_TEXT_DIVIDE_WIDTH = 150,
        IGS_TEXT_DESCRIPTION_POS_X = 20,
        IGS_TEXT_DESCRIPTION_POS_Y = 50,
        IGS_TEXT_DESCRIPTION_INTERVAL = 12,
        IGS_TEXT_DESCRIPTION_WIDTH = 170,
    };

  public:
    explicit CMsgBoxIGSDeleteItemConfirm(SessionKeeper &keeper);
    ~CMsgBoxIGSDeleteItemConfirm();
    bool Create(float fPriority = 3.f);
    void Release();
    bool Update();
    bool Render();
    void Initialize(int iStorageSeq, int iStorageItemSeq, wchar_t szItemType);
    CALLBACK_RESULT LButtonUp(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                 const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelButtonDown(class CNewUIMessageBoxBase *pOwner,
                                     const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void SetButtonInfo();
    void RenderFrame();
    void RenderTexts();
    void RenderButtons();
    void LoadImages();
    void UnloadImages();

  private:
    CNewUIMessageBoxButton m_BtnDelete{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnCancel{SessionOrigin()};
    int m_iMiddleCount;
    int m_iStorageSeq;
    int m_iStorageItemSeq;

    wchar_t m_szItemType;
    wchar_t m_szDescription[UIMAX_TEXT_LINE][MAX_TEXT_LENGTH];

    int m_iDesciptionLine;
};

class CMsgBoxIGSDeleteItemConfirmLayout : public TMsgBoxLayout<CMsgBoxIGSDeleteItemConfirm>
{
  public:
    using TMsgBoxLayout<CMsgBoxIGSDeleteItemConfirm>::TMsgBoxLayout;
    bool SetLayout();
};

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

using namespace SEASON3B;

class CMsgBoxIGSGiftStorageItemInfo : public CNewUIMessageBoxBase, public INewUI3DRenderObj
{
  public:
    using CNewUIMessageBoxBase::CNewUIMessageBoxBase;
    enum IMAGE_IGS_GIFT_STORAGE_ITEM_INFO
    {
        IMAGE_IGS_BUTTON = BITMAP_IGS_MSGBOX_BUTTON,
        IMAGE_IGS_FRAME = BITMAP_IGS_MSGBOX_GIFT_STORAGE_ITEM,
    };

    enum IMAGESIZE_IGS_GIFT_STORAGE_ITEM_INFO
    {
        IMAGE_IGS_WINDOW_WIDTH = 640,
        IMAGE_IGS_WINDOW_HEIGHT = 429,
        IMAGE_IGS_FRAME_WIDTH = 210,
        IMAGE_IGS_FRAME_HEIGHT = 306,
        IMAGE_IGS_BTN_WIDTH = 52,
        IMAGE_IGS_BTN_HEIGHT = 26,
    };

    enum IGS_GIFT_STORAGE_ITEM_INFO_POS
    {
        IGS_BTN_OK_POS_X = 43,
        IGS_BTN_CANCEL_POS_X = 115,
        IGS_BTN_POS_Y = 168,
        IGS_TEXT_TITLE_POS_Y = 10,
        IGS_TEXT_ITEM_NAME_POS_Y = 100,
        IGS_TEXT_ITEM_INFO_POS_X = 30,
        IGS_TEXT_ITEM_INFO_NUM_POS_Y = 124,
        IGS_TEXT_ITEM_INFO_PERIOD_POS_Y = 140,
        IGS_TEXT_ITEM_INFO_WIDTH = 150,
        IGS_TEXT_ID_INFO_POS_X = 30,
        IGS_TEXT_ID_INFO_POS_Y = 177,
        IGS_TEXT_ID_INFO_WIDTH = 150,
        IGS_MESSAGE_INPUT_TEXT_POS_X = 126,
        IGS_MESSAGE_INPUT_TEXT_POS_Y = 96,
        IGS_3DITEM_POS_X = 56,
        IGS_3DITEM_POS_Y = 34,
        IGS_3DITEM_WIDTH = 97,
        IGS_3DITEM_HEIGHT = 60,
    };

  public:
    explicit CMsgBoxIGSGiftStorageItemInfo(SessionKeeper &keeper);
    ~CMsgBoxIGSGiftStorageItemInfo();
    bool Create(float fPriority = 3.f);
    void Release();
    bool Update();
    bool Render();
    bool IsVisible() const;
    void Render3D();
    void Initialize(int iStorageSeq, int iStorageItemSeq, WORD wItemCode, wchar_t szItemType,
                    wchar_t *pszID, wchar_t *pszMessage, wchar_t *pszName, wchar_t *pszNum,
                    wchar_t *pszPeriod);
    CALLBACK_RESULT LButtonUp(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                 const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelButtonDown(class CNewUIMessageBoxBase *pOwner,
                                     const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void SetButtonInfo();
    void RenderFrame();
    void RenderTexts();
    void RenderButtons();
    void LoadImages();
    void UnloadImages();

  private:
    CNewUIMessageBoxButton m_BtnUse{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnCancel{SessionOrigin()};
    CUITextInputBox m_MessageInputBox;

    int m_iStorageSeq;
    int m_iStorageItemSeq;
    WORD m_wItemCode;

    wchar_t m_szName[MAX_TEXT_LENGTH];
    wchar_t m_szNum[MAX_TEXT_LENGTH];
    wchar_t m_szPeriod[MAX_TEXT_LENGTH];
    char m_szItemType;
    wchar_t m_szIDInfo[MAX_TEXT_LENGTH];
    wchar_t m_szMessage[MAX_GIFT_MESSAGE_SIZE];
};

class CMsgBoxIGSGiftStorageItemInfoLayout : public TMsgBoxLayout<CMsgBoxIGSGiftStorageItemInfo>
{
  public:
    using TMsgBoxLayout<CMsgBoxIGSGiftStorageItemInfo>::TMsgBoxLayout;
    explicit CMsgBoxIGSGiftStorageItemInfoLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CMsgBoxIGSGiftStorageItemInfo>(keeper)
    {
    }
    bool SetLayout();
};

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

using namespace SEASON3B;

class CMsgBoxIGSSendGift : public CNewUIMessageBoxBase
{
  public:
    using CNewUIMessageBoxBase::CNewUIMessageBoxBase;
    enum IMAGE_IGS_SEND_GIFT
    {
        IMAGE_IGS_BUTTON = BITMAP_IGS_MSGBOX_BUTTON,
        IMAGE_IGS_FRAME = BITMAP_IGS_MSGBOX_SEND_GIFT_FRAME,         // Frame
        IMAGE_IGS_DECO = BITMAP_IGS_MSGBOX_SEND_GIFT_DECO,           // Deco
        IMAGE_IGS_INPUTTEXT = BITMAP_IGS_MSGBOX_SEND_GIFT_INPUTTEXT, // Input TextBox
    };

    enum IMAGESIZE_IGS_SEND_GIFT
    {
        IMAGE_IGS_WINDOW_WIDTH = 640, // In-game shop background size
        IMAGE_IGS_WINDOW_HEIGHT = 429,
        IMAGE_IGS_FRAME_WIDTH = 210, // Message box Size
        IMAGE_IGS_FRAME_HEIGHT = 267,
        IMAGE_IGS_DECO_WIDTH = 17, // Deco
        IMAGE_IGS_DECO_HEIGHT = 19,
        IMAGE_IGS_ID_INPUT_BOX_WIDTH = 76, // Input TextBox
        IMAGE_IGS_ID_INPUT_BOX_HEIGHT = 17,
        IMAGE_IGS_BTN_WIDTH = 52, // Button Size
        IMAGE_IGS_BTN_HEIGHT = 26,
    };

    // Relative coordinates on the message box
    enum IGS_SEND_GIFT_POS
    {
        IMAGE_IGS_DECO_POS_X = 10, // Deco
        IMAGE_IGS_DECO_POS_Y = 82,
        IMAGE_IGS_ID_INPUT_BOX_POS_X = 118, // ID Input Box
        IMAGE_IGS_ID_INPUT_BOX_POS_Y = 82,
        IGS_ID_INPUT_TEXT_POS_X = 120, // ID Input Text
        IGS_ID_INPUT_TEXT_POS_Y = 87,
        IGS_ID_INPUT_TEXT_WIDTH = 200,
        IGS_ID_INPUT_TEXT_HEIGHT = 14,
        IGS_MESSAGE_INPUT_TEXT_POS_X = 22, // Message Input Text
        IGS_MESSAGE_INPUT_TEXT_POS_Y = 126,
        IGS_MESSAGE_INPUT_TEXT_WIDTH = 170,
        IGS_MESSAGE_INPUT_TEXT_HEIGHT = 65,
        IGS_MESSAGE_INPUT_TEXT_LINE_HEIGHT = 50,
        IGS_BTN_OK_POS_X = 35,
        IGS_BTN_CANCEL_POS_X = 122,
        IGS_BTN_POS_Y = 230,
        IGS_TEXT_TITLE_POS_Y = 10,    // Title
        IGS_TEXT_ID_TITLE_POS_X = 28, // ID Title
        IGS_TEXT_ID_TITLE_POS_Y = 87,
        IGS_TEXT_ID_TITLE_WIDTH = 100,
        IGS_TEXT_MESSAGE_TITLE_POS_Y = 110, // Message Title
        IGS_TEXT_ITEM_INFO_POS_X = 30,      // ItemInfo
        IGS_TEXT_ITEM_INFO_NAME_POS_Y = 38,
        IGS_TEXT_ITEM_INFO_PRICE_POS_Y = 50,
        IGS_TEXT_ITEM_INFO_PERIOD_POS_Y = 62,
        IGS_TEXT_ITEM_INFO_WIDTH = 143,
        IGS_TEXT_NOTICE_POS_Y = 205, // Notice
        IGS_TEXT_NOTICE_WIDTH = 170,
    };

  public:
    explicit CMsgBoxIGSSendGift(SessionKeeper &keeper);
    ~CMsgBoxIGSSendGift();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();

    void Initialize(int iPackageSeq, int iDisplaySeq, int iPriceSeq, DWORD wItemCode, int iCashType,
                    wchar_t *pszName, wchar_t *pszPrice, wchar_t *pszPeriod);

    CALLBACK_RESULT LButtonUp(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                 const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelButtonDown(class CNewUIMessageBoxBase *pOwner,
                                     const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void SetButtonInfo();

    void RenderFrame();
    void RenderTexts();
    void RenderButtons();

    void ChangeInputBoxFocus();

    void LoadImages();
    void UnloadImages();

    void InitInputBox();

  private:
    // buttons
    CNewUIMessageBoxButton m_BtnOk{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnCancel{SessionOrigin()};

    CUITextInputBox m_IDInputBox;
    CUITextInputBox m_MessageInputBox;

    int m_iPackageSeq;
    int m_iDisplaySeq;
    int m_iPriceSeq;
    DWORD m_wItemCode;
    int m_iCashType;

    wchar_t m_szID[MAX_USERNAME_SIZE + 1];
    wchar_t m_szMessage[MAX_GIFT_MESSAGE_SIZE];

    wchar_t m_szName[MAX_TEXT_LENGTH];
    wchar_t m_szPrice[MAX_TEXT_LENGTH];
    wchar_t m_szPeriod[MAX_TEXT_LENGTH];

    wchar_t m_szNotice[NUM_LINE_CMB][MAX_TEXT_LENGTH];

    int m_iNumNoticeLine;
};

class CMsgBoxIGSSendGiftLayout : public TMsgBoxLayout<CMsgBoxIGSSendGift>
{
  public:
    using TMsgBoxLayout<CMsgBoxIGSSendGift>::TMsgBoxLayout;
    explicit CMsgBoxIGSSendGiftLayout(SessionKeeper &keeper)
        : TMsgBoxLayout<CMsgBoxIGSSendGift>(keeper)
    {
    }
    bool SetLayout();
};

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

using namespace SEASON3B;

class CMsgBoxIGSSendGiftConfirm : public CNewUIMessageBoxBase
{
  public:
    using CNewUIMessageBoxBase::CNewUIMessageBoxBase;
    enum IMAGE_IGS_SEND_GIFT_CONFIRM
    {
        IMAGE_IGS_BUTTON = BITMAP_IGS_MSGBOX_BUTTON,
        IMAGE_IGS_BACK = CNewUIOptionWindow::IMAGE_OPTION_FRAME_BACK,
        IMAGE_IGS_UP = CNewUIOptionWindow::IMAGE_OPTION_FRAME_UP,
        IMAGE_IGS_DOWN = CNewUIOptionWindow::IMAGE_OPTION_FRAME_DOWN,
        IMAGE_IGS_LEFTLINE = CNewUIOptionWindow::IMAGE_OPTION_FRAME_LEFT,
        IMAGE_IGS_RIGHTLINE = CNewUIOptionWindow::IMAGE_OPTION_FRAME_RIGHT,
        IMAGE_IGS_TEXTBOX = BITMAP_IGS_MGSBOX_BUY_CONFIRM_TEXT_BOX,
    };

    enum IMAGESIZE_IGS_SEND_GIFT_CONFIRM
    {
        IMAGE_IGS_WINDOW_WIDTH = 640,
        IMAGE_IGS_WINDOW_HEIGHT = 429,
        IMAGE_IGS_FRAME_WIDTH = 190,
        IMAGE_IGS_FRAME_HEIGHT = 179,
        IMAGE_IGS_TEXTBOX_WIDTH = 160,
        IMAGE_IGS_TEXTBOX_HEIGHT = 41,
        IMAGE_IGS_UP_HEIGHT = 64,
        IMAGE_IGS_DOWN_HEIGHT = 45,
        IMAGE_IGS_LINE_WIDTH = 21,
        IMAGE_IGS_LINE_HEIGHT = 10,
        IMAGE_IGS_BTN_WIDTH = 52,
        IMAGE_IGS_BTN_HEIGHT = 26,
    };

    enum IGS_SEND_GIFT_CONFIRM_POS
    {
        IGS_BTN_OK_POS_X = 35,
        IGS_BTN_CANCEL_POS_X = 105,
        IGS_BTN_POS_Y = 140,
        IGS_TEXTBOX_POS_X = 15,
        IGS_TEXTBOX_POS_Y = 58,
        IGS_TEXT_TITLE_POS_Y = 10,
        IGS_TEXT_QUESTION_POS_Y = 42,
        IGS_TEXT_NOTICE_POS_Y = 105,
        IGS_TEXT_NOTICE_WIDTH = 150,
        IGS_TEXT_ITEM_INFO_POS_X = 25,
        IGS_TEXT_ITEM_INFO_NAME_POS_Y = 64,
        IGS_TEXT_ITEM_INFO_PRICE_POS_Y = 75,
        IGS_TEXT_ITEM_INFO_PERIOD_POS_Y = 86,
        IGS_TEXT_ITEM_INFO_WIDTH = 143,
    };

  public:
    explicit CMsgBoxIGSSendGiftConfirm(SessionKeeper &keeper);
    ~CMsgBoxIGSSendGiftConfirm();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();

    void Initialize(int iPackageSeq, int iDisplaySeq, int iPriceSeq, DWORD wItemCode, int iCashType,
                    wchar_t *pszID, wchar_t *pszMessage, wchar_t *pszName, wchar_t *pszPrice,
                    wchar_t *pszPeriod);

    CALLBACK_RESULT LButtonUp(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                 const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelButtonDown(class CNewUIMessageBoxBase *pOwner,
                                     const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void SetButtonInfo();

    void RenderFrame();
    void RenderTexts();
    void RenderButtons();

    void LoadImages();
    void UnloadImages();

  private:
    // buttons
    CNewUIMessageBoxButton m_BtnOk{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnCancel{SessionOrigin()};

    int m_iMiddleCount;

    int m_iPackageSeq;
    int m_iDisplaySeq;
    int m_iPriceSeq;
    DWORD m_wItemCode;
    int m_iCashType;

    wchar_t m_szID[MAX_USERNAME_SIZE + 1];
    wchar_t m_szMessage[MAX_GIFT_MESSAGE_SIZE];

    wchar_t m_szItemName[MAX_TEXT_LENGTH];
    wchar_t m_szItemPrice[MAX_TEXT_LENGTH];
    wchar_t m_szItemPeriod[MAX_TEXT_LENGTH];

    wchar_t m_szNotice[NUM_LINE_CMB][MAX_TEXT_LENGTH];

    int m_iNumNoticeLine;
};

class CMsgBoxIGSSendGiftConfirmLayout : public TMsgBoxLayout<CMsgBoxIGSSendGiftConfirm>
{
  public:
    using TMsgBoxLayout<CMsgBoxIGSSendGiftConfirm>::TMsgBoxLayout;
    bool SetLayout();
};

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

using namespace SEASON3B;

class CMsgBoxIGSStorageItemInfo : public CNewUIMessageBoxBase, public INewUI3DRenderObj
{
  public:
    using CNewUIMessageBoxBase::CNewUIMessageBoxBase;
    enum IMAGE_IGS_STORAGE_ITEM_INFO
    {
        IMAGE_IGS_BUTTON = BITMAP_IGS_MSGBOX_BUTTON,      // �ΰ��Ӽ� ��ư
        IMAGE_IGS_FRAME = BITMAP_IGS_MSGBOX_STORAGE_ITEM, // Main Frame
    };

    enum IMAGESIZE_IGS_STORAGE_ITEM_INFO
    {
        IMAGE_IGS_WINDOW_WIDTH = 640, // �ΰ��Ӽ� ��� ������
        IMAGE_IGS_WINDOW_HEIGHT = 429,
        IMAGE_IGS_FRAME_WIDTH = 210, // �޼����ڽ� Size
        IMAGE_IGS_FRAME_HEIGHT = 202,
        IMAGE_IGS_BTN_WIDTH = 52, // ��ư Size
        IMAGE_IGS_BTN_HEIGHT = 26,
    };

    // �޼����ڽ����� �����ǥ
    enum IGS_STORAGE_ITEM_INFO_POS
    {
        IGS_BTN_OK_POS_X = 43, // ��ư
        IGS_BTN_CANCEL_POS_X = 115,
        IGS_BTN_POS_Y = 168,
        IGS_TEXT_TITLE_POS_Y = 10,      // Title
        IGS_TEXT_ITEM_NAME_POS_Y = 100, // ItemName
        IGS_TEXT_ITEM_INFO_POS_X = 30,  // ItemInfo
        IGS_TEXT_ITEM_INFO_NUM_POS_Y = 124,
        IGS_TEXT_ITEM_INFO_PERIOD_POS_Y = 140,
        IGS_TEXT_ITEM_INFO_WIDTH = 150,
        IGS_3DITEM_POS_X = 56, // 3D ������ ���� ���� ��ġ(BOX ����)
        IGS_3DITEM_POS_Y = 34,
        IGS_3DITEM_WIDTH = 97, // 3D ������ ���� ��������
        IGS_3DITEM_HEIGHT = 60,
    };

  public:
    explicit CMsgBoxIGSStorageItemInfo(SessionKeeper &keeper);
    ~CMsgBoxIGSStorageItemInfo();

    bool Create(float fPriority = 3.f);
    void Release();
    bool Update();
    bool Render();

    bool IsVisible() const;

    void Render3D();

    void Initialize(int iStorageSeq, int iStorageItemSeq, WORD wItemCode, char szItemType,
                    wchar_t *pszName, wchar_t *pszNum, wchar_t *pszPeriod);

    CALLBACK_RESULT LButtonUp(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                 const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelButtonDown(class CNewUIMessageBoxBase *pOwner,
                                     const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void SetButtonInfo();

    void RenderFrame();
    void RenderTexts();
    void RenderButtons();

    void LoadImages();
    void UnloadImages();

  private:
    // buttons
    CNewUIMessageBoxButton m_BtnUse{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnCancel{SessionOrigin()};

    int m_iStorageSeq;     // ������ ����
    int m_iStorageItemSeq; // ������ ��ǰ ����
    WORD m_wItemCode;      // ������ �ڵ�

    wchar_t m_szName[MAX_TEXT_LENGTH]; // ������ �̸�
    wchar_t m_szNum[MAX_TEXT_LENGTH];
    wchar_t m_szPeriod[MAX_TEXT_LENGTH];
    char m_szItemType; // ��ǰ���� (C : ĳ��, P : ��ǰ)
};

// LayOut
class CMsgBoxIGSStorageItemInfoLayout : public TMsgBoxLayout<CMsgBoxIGSStorageItemInfo>
{
  public:
    using TMsgBoxLayout<CMsgBoxIGSStorageItemInfo>::TMsgBoxLayout;
    bool SetLayout();
};

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

using namespace SEASON3B;

class CMsgBoxIGSUseBuffConfirm : public CNewUIMessageBoxBase
{
  public:
    using CNewUIMessageBoxBase::CNewUIMessageBoxBase;
    enum IMAGE_IGS_USEBUFF_CONFIRM
    {
        IMAGE_IGS_BUTTON = BITMAP_IGS_MSGBOX_BUTTON,
        IMAGE_IGS_BACK = CNewUIOptionWindow::IMAGE_OPTION_FRAME_BACK,
        IMAGE_IGS_UP = CNewUIOptionWindow::IMAGE_OPTION_FRAME_UP,
        IMAGE_IGS_DOWN = CNewUIOptionWindow::IMAGE_OPTION_FRAME_DOWN,
        IMAGE_IGS_LEFTLINE = CNewUIOptionWindow::IMAGE_OPTION_FRAME_LEFT,
        IMAGE_IGS_RIGHTLINE = CNewUIOptionWindow::IMAGE_OPTION_FRAME_RIGHT,
    };

    enum IMAGE_IGS_USEBUFF_CONFIRM_SIZE
    {
        IMAGE_IGS_WINDOW_WIDTH = 640,
        IMAGE_IGS_WINDOW_HEIGHT = 429,
        IMAGE_IGS_FRAME_WIDTH = 190,
        IMAGE_IGS_FRAME_HEIGHT = 159,
        IMAGE_IGS_UP_HEIGHT = 64,
        IMAGE_IGS_DOWN_HEIGHT = 45,
        IMAGE_IGS_LINE_WIDTH = 21,
        IMAGE_IGS_LINE_HEIGHT = 10,
        IMAGE_IGS_BTN_WIDTH = 52,
        IMAGE_IGS_BTN_HEIGHT = 26,
    };

    enum IGS_USEBUFF_CONFIRM_POS
    {
        IGS_BTN_OK_POS_X = 33,
        IGS_BTN_CANCEL_POS_X = 105,
        IGS_BTN_POS_Y = 120,
        IGS_TEXT_TITLE_Y = 10,
        IGS_TEXT_DIVIDE_WIDTH = 150,
        IGS_TEXT_DESCRIPTION_POS_X = 20,
        IGS_TEXT_DESCRIPTION_POS_Y = 50,
        IGS_TEXT_DESCRIPTION_INTERVAL = 12,
        IGS_TEXT_DESCRIPTION_WIDTH = 170,
    };

  public:
    explicit CMsgBoxIGSUseBuffConfirm(SessionKeeper &keeper);
    ~CMsgBoxIGSUseBuffConfirm();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();

    void Initialize(int iStorageSeq, int iStorageItemSeq, WORD wItemCode, wchar_t szItemType,
                    wchar_t *pszItemName, wchar_t *pszBuffName);

    CALLBACK_RESULT LButtonUp(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                 const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelButtonDown(class CNewUIMessageBoxBase *pOwner,
                                     const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void SetButtonInfo();

    void RenderFrame();
    void RenderTexts();
    void RenderButtons();

    void LoadImages();
    void UnloadImages();

  private:
    // buttons
    CNewUIMessageBoxButton m_BtnOk{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnCancel{SessionOrigin()};

    int m_iMiddleCount;

    wchar_t m_szDescription[UIMAX_TEXT_LINE][MAX_TEXT_LENGTH];

    int m_iDesciptionLine;

    int m_iStorageSeq;
    int m_iStorageItemSeq;
    WORD m_wItemCode;
    char m_szItemType;
    wchar_t m_szCurrentBuffName[MAX_TEXT_LENGTH];
};

class CMsgBoxIGSUseBuffConfirmLayout : public TMsgBoxLayout<CMsgBoxIGSUseBuffConfirm>
{
  public:
    using TMsgBoxLayout<CMsgBoxIGSUseBuffConfirm>::TMsgBoxLayout;
    bool SetLayout();
};

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

using namespace SEASON3B;

class BuffStateSystem;

class CMsgBoxIGSUseItemConfirm : public CNewUIMessageBoxBase
{
  public:
    using CNewUIMessageBoxBase::CNewUIMessageBoxBase;
    enum IMAGE_IGS_USEITEM_CONFIRM
    {
        IMAGE_IGS_BUTTON = BITMAP_IGS_MSGBOX_BUTTON, // �ΰ��Ӽ� ��ư
        IMAGE_IGS_BACK = CNewUIOptionWindow::IMAGE_OPTION_FRAME_BACK,
        IMAGE_IGS_UP = CNewUIOptionWindow::IMAGE_OPTION_FRAME_UP,
        IMAGE_IGS_DOWN = CNewUIOptionWindow::IMAGE_OPTION_FRAME_DOWN,
        IMAGE_IGS_LEFTLINE = CNewUIOptionWindow::IMAGE_OPTION_FRAME_LEFT,
        IMAGE_IGS_RIGHTLINE = CNewUIOptionWindow::IMAGE_OPTION_FRAME_RIGHT,
    };

    enum IMAGE_IGS_USEITEM_CONFIRM_SIZE
    {
        IMAGE_IGS_WINDOW_WIDTH = 640, // �ΰ��Ӽ� ��� ������
        IMAGE_IGS_WINDOW_HEIGHT = 429,
        IMAGE_IGS_FRAME_WIDTH = 190, // �޼����ڽ� Size
        IMAGE_IGS_FRAME_HEIGHT = 179,
        IMAGE_IGS_UP_HEIGHT = 64,
        IMAGE_IGS_DOWN_HEIGHT = 45,
        IMAGE_IGS_LINE_WIDTH = 21,
        IMAGE_IGS_LINE_HEIGHT = 10,
        IMAGE_IGS_BTN_WIDTH = 52, // ��ư Size
        IMAGE_IGS_BTN_HEIGHT = 26,
    };

    // �޼����ڽ����� �����ǥ
    enum IGS_USEITEM_CONFIRM_POS
    {
        IGS_BTN_OK_POS_X = 33, // ��ư Pos
        IGS_BTN_CANCEL_POS_X = 105,
        IGS_BTN_POS_Y = 140,
        IGS_TEXT_TITLE_Y = 10, // Title
        IGS_TEXT_DIVIDE_WIDTH = 150,
        IGS_TEXT_DESCRIPTION_POS_X = 20,
        IGS_TEXT_DESCRIPTION_POS_Y = 50,
        IGS_TEXT_DESCRIPTION_INTERVAL = 12,
        IGS_TEXT_DESCRIPTION_WIDTH = 170,
    };

  public:
    explicit CMsgBoxIGSUseItemConfirm(SessionKeeper &keeper);
    ~CMsgBoxIGSUseItemConfirm();

    bool Create(float fPriority = 3.f);
    void Release();

    bool Update();
    bool Render();

    void Initialize(int iStorageSeq, int iStorageItemSeq, WORD wItemCode, wchar_t szItemType,
                    wchar_t *pszItemName);

    CALLBACK_RESULT LButtonUp(class CNewUIMessageBoxBase *pOwner, const leaf::xstreambuf &xParam);
    CALLBACK_RESULT OKButtonDown(class CNewUIMessageBoxBase *pOwner,
                                 const leaf::xstreambuf &xParam);
    CALLBACK_RESULT CancelButtonDown(class CNewUIMessageBoxBase *pOwner,
                                     const leaf::xstreambuf &xParam);

  private:
    void SetAddCallbackFunc();
    void SetButtonInfo();

    void RenderFrame();
    void RenderTexts();
    void RenderButtons();

    void LoadImages();
    void UnloadImages();

  private:
    BuffStateSystem &g_BuffStateSystem;

    // buttons
    CNewUIMessageBoxButton m_BtnOk{SessionOrigin()};
    CNewUIMessageBoxButton m_BtnCancel{SessionOrigin()};

    int m_iMiddleCount;

    wchar_t m_szDescription[UIMAX_TEXT_LINE][MAX_TEXT_LENGTH];

    int m_iDesciptionLine;

    int m_iStorageSeq;                     // ������ ����
    int m_iStorageItemSeq;                 // ������ ��ǰ ����
    WORD m_wItemCode;                      // ������ �ڵ�
    wchar_t m_szItemName[MAX_TEXT_LENGTH]; // �������̸�
    char m_szItemType;                     // ��ǰ���� (C : ĳ��, P : ��ǰ)
};

// LayOut
class CMsgBoxIGSUseItemConfirmLayout : public TMsgBoxLayout<CMsgBoxIGSUseItemConfirm>
{
  public:
    using TMsgBoxLayout<CMsgBoxIGSUseItemConfirm>::TMsgBoxLayout;
    bool SetLayout();
};

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef PBG_ADD_INGAMESHOP_UI_ITEMSHOP

#pragma warning(disable : 4786)

class SessionKeeper;
class SessionItemStore;
class SessionUiUnit;

class PresentationSeparationTestPeer;

namespace SEASON3B
{
class CNewUIInventoryCtrl;

enum
{
    INVENTORY_SQUARE_WIDTH = 20,
    INVENTORY_SQUARE_HEIGHT = 20,
};
enum TOOLTIP_TYPE
{
    UNKNOWN_TOOLTIP_TYPE = 0,
    TOOLTIP_TYPE_INVENTORY,
    TOOLTIP_TYPE_REPAIR,
    TOOLTIP_TYPE_NPC_SHOP,
    TOOLTIP_TYPE_MY_SHOP,
    TOOLTIP_TYPE_PURCHASE_SHOP,
};
enum SQUARE_COLOR_STATE
{
    UNKNOWN_COLOR_STATE = 0,
    COLOR_STATE_NORMAL,
    COLOR_STATE_WARNING,
};

class CNewUIPickedItem : public INewUI3DRenderObj, protected SessionUiLegacyBindings
{
    CNewUIInventoryCtrl *m_pSrcInventory;
    ITEM *&m_pPickedItem;

    bool m_bShow;
    POINT m_Pos;
    SIZE m_Size;

  public:
    explicit CNewUIPickedItem(SessionKeeper &keeper);
    virtual ~CNewUIPickedItem();

    bool Create(SessionItemStore *pNewItemMng, CNewUIInventoryCtrl *pSrc, ITEM *pItem);
    void Release();

    CNewUIInventoryCtrl *GetOwnerInventory() const;
    void DetachInventory(const CNewUIInventoryCtrl *inventory) noexcept;
    STORAGE_TYPE GetSourceStorageType() const;
    ITEM *GetItem() const;

    const POINT &GetPos() const;
    const SIZE &GetSize() const;
    void GetRect(RECT &rcBox);

    int GetSourceLinealPos();
    bool GetTargetPos(CNewUIInventoryCtrl *pDest, int &iTargetColumnX, int &iTargetRowY);
    int GetTargetLinealPos(CNewUIInventoryCtrl *pDest);

    bool IsVisible() const;
    bool IsOwnerRendered() const;
    bool RenderOwnerLayer();
    void ShowPickedItem();
    void HidePickedItem();

    void Render3D();
};

class CNewUIInventoryCtrl : public INewUI3DRenderObj, protected SessionUiLegacyBindings
{
  public:
    enum EVENT_STATE
    {
        EVENT_NONE = 0,
        EVENT_HOVER,
        EVENT_PICKING,
    };
    enum IMAGE_LIST
    {
        IMAGE_ITEM_SQUARE = BITMAP_INTERFACE_NEW_INVENTORY_BASE_BEGIN, //. newui_item_box.tga
        IMAGE_ITEM_TABLE_TOP_LEFT,                                     //. newui_item_table01(L).tga
        IMAGE_ITEM_TABLE_TOP_RIGHT,                                    //. newui_item_table01(R).tga
        IMAGE_ITEM_TABLE_BOTTOM_LEFT,                                  //. newui_item_table02(L).tga
        IMAGE_ITEM_TABLE_BOTTOM_RIGHT,                                 //. newui_item_table02(R).tga
        IMAGE_ITEM_TABLE_TOP_PIXEL,    //. newui_item_table03(Up).tga
        IMAGE_ITEM_TABLE_BOTTOM_PIXEL, //. newui_item_table03(Dw).tga
        IMAGE_ITEM_TABLE_LEFT_PIXEL,   //. newui_item_table03(L).tga
        IMAGE_ITEM_TABLE_RIGHT_PIXEL,  //. newui_item_table03(R).tga

#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        IMAGE_ITEM_SQUARE_FOR_1_BY_1,  //. newui_inven_usebox_01.tga
        IMAGE_ITEM_SQUARE_TOP_RECT,    //. newui_inven_usebox_02.tga
        IMAGE_ITEM_SQUARE_BOTTOM_RECT, //. newui_inven_usebox_03.tga
#endif                                 //LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
    };

  private:
    friend class ::PresentationSeparationTestPeer;
    friend class CNewUIPickedItem;
    enum
    {
        ITEM_SQUARE_WITH = 20,
        ITEM_SQUARE_HEIGHT = 20,
        WND_TOP_EDGE = 3,
        WND_LEFT_EDGE = 4,
        WND_BOTTOM_EDGE = 8,
        WND_RIGHT_EDGE = 9,

        RENDER_NUMBER_OF_ITEM = 1,
        RENDER_ITEM_TOOLTIP = 2,
    };

    CNewUIPickedItem *&ms_pPickedItem;

    SessionUiUnit &sessionUi_;
    CNewUI3DRenderMng *m_pNew3DRenderMng;
    CNewUIObj *m_pOwner;

    InventoryGrid *grid_ = nullptr;
    POINT m_Pos;
    SIZE m_Size;
    /**
         * \brief The index of the first slot for this control.
         * For example, the box of an inventory starts at 12.
         */
    EVENT_STATE m_EventState;
    int m_iPointedSquareIndex; // has m_nIndexOffset included
    bool m_bShow, m_bLock;

    TOOLTIP_TYPE m_ToolTipType;
    ITEM *m_pToolTipItem;

    float m_afColorStateNormal[3], m_afColorStateWarning[3];

    bool m_bRepairMode;

    struct DragPreviewCell
    {
        int column, row, columns, rows;
        std::array<float, 3> color;
    };
    std::vector<DragPreviewCell> dragPreview_;
    bool CanPreviewDrop(ITEM *source, ITEM *target);
    void PrepareDragPreview();
    void RenderDragPreview();

    int m_squareWidth;
    int m_squareHeight;
    float m_ownerX;
    float m_ownerY;
    float m_ownerSquareWidth;
    float m_ownerSquareHeight;
    bool m_renderSlotFrame;
    bool m_ownerRendered;
    std::vector<int> m_slotIconFrames;
    void UpdateSlotIconFrames();

    void Init();

    void LoadImages();
    void UnloadImages();

    void SetItemColorState(ITEM *pItem);
    bool CanChangeItemColorState(ITEM *pItem);

    void UpdateProcess();

    void RequestInventoryRefresh() const;

    float PresentedX() const noexcept;
    float PresentedY() const noexcept;
    float PresentedSquareWidth() const noexcept;
    float PresentedSquareHeight() const noexcept;
    float ItemPresentationScale() const noexcept;

  public:
    explicit CNewUIInventoryCtrl(SessionKeeper &keeper);
    virtual ~CNewUIInventoryCtrl();

    bool Create(InventoryGrid &grid, CNewUI3DRenderMng *renderer, CNewUIObj *owner, int x, int y);
    InventoryGrid &Data() const noexcept
    {
        return *grid_;
    }
    void Release();

    bool AddItem(int iLinealPos, std::span<const BYTE> pbyItemPacket);
    bool AddItem(int iColumnX, int iRowY, std::span<const BYTE> pbyItemPacket);
    bool AddItem(int iColumnX, int iRowY, ITEM *pItem);
    bool AddItem(int iColumnX, int iRowY, BYTE byType, BYTE bySubType, BYTE byLevel = 0,
                 BYTE byDurability = 255, BYTE byOption1 = 0, BYTE byOptionEx = 0,
                 BYTE byOption380 = 0, BYTE byOptionHarmony = 0);
    void RemoveItem(ITEM *pItem);
    bool RemoveItemAt(int iLinealPos);
    void RemoveAllItems();

    size_t GetNumberOfItems();

    bool IsItem(short int siType);
    int GetItemCount(short int siType, int iLevel = -1);

    ITEM *GetItem(int iIndex /* 0 <= iIndex < GetNumberOfItems() */);

    ITEM *FindItem(int iLinealPos);
    ITEM *FindItem(int iColumnX, int iRowY);
    ITEM *FindItemByKey(DWORD dwKey);
    ITEM *FindItemAtPt(int x, int y);
    ITEM *FindTypeItem(short int siType);
    int FindItemIndex(short int siType, int iLevel);
    int FindItemReverseIndex(short sType, int iLevel);
    int GetIndexByItem(ITEM *pItem);
    short int FindItemTypeByPos(int iColumnX, int iRowY);

    ITEM *FindItemPointedSquareIndex();
    int GetPointedSquareIndex();

    int GetNumItemByKey(DWORD dwItemKey);
    int GetNumItemByType(short sItemType);

    void SetEventState(EVENT_STATE es);

    int FindEmptySlot(IN int cx, IN int cy); //. return lineal position
    bool FindEmptySlot(IN int cx, IN int cy, OUT int &iColumnX, OUT int &iColumnY);
    int GetEmptySlotCount();

    bool UpdateMouseEvent();
    bool Update();
    std::span<const int> SlotIconFrames() const noexcept
    {
        return m_slotIconFrames;
    }

    void Render();

    void SetPos(int x, int y);
    void SetSquareSize(int width, int height);
    void SetOwnerGeometry(float x, float y, float squareWidth, float squareHeight);
    int GetSquareWidth() const;
    int GetSquareHeight() const;
    void SetRenderSlotFrame(bool render);
    bool IsOwnerRendered() const;
    void SetOwnerRendered(bool ownerRendered);
    bool RenderOwnerLayer();
    const POINT &GetPos() const;
    int GetNumberOfColumn() const;
    int GetNumberOfRow() const;
    void GetRect(RECT &rcBox);

    STORAGE_TYPE GetStorageType() const
    {
        return grid_->GetStorageType();
    }
    void SetStorageType(STORAGE_TYPE type)
    {
        grid_->SetStorageType(type);
    }

    void SetSquareColorNormal(float fRed, float fGreen, float fBlue);
    void GetSquareColorNormal(float *pfParams) const;
    void SetSquareColorWarning(float fRed, float fGreen, float fBlue);
    void GetSquareColorWarning(float *pfParams) const;

    EVENT_STATE GetEventState();

    CNewUIObj *GetOwner() const;
    bool IsVisible() const;
    void ShowInventory();
    void HideInventory();

    bool IsLocked() const;
    void LockInventory();
    void UnlockInventory();

    //. Check Functions
    /* Caution: It's square index, not list index */
    bool GetSquarePosAtPt(float x, float y, int &iColumnX, int &iRowY);

    bool CheckPtInRect(int x, int y);
    bool CheckRectInRect(const RECT &rcBox);
    int GetIndexAtPt(int x, int y);

    int GetIndex(int column, int row);
    bool CanMove(int iLinealPos, ITEM *pItem);
    bool CanMove(int iColumnX, int iRowY, ITEM *pItem);
    bool CanMoveToPt(int x, int y, ITEM *pItem);

    void SetToolTipType(TOOLTIP_TYPE ToolTipType);
    void CreateItemToolTip(ITEM *pItem);
    void DeleteItemToolTip();

    void SetRepairMode(bool bRepair);
    bool IsRepairMode();

    bool AreItemsStackable(ITEM *pSourceItem, ITEM *pTargetItem);
    bool CanUpgradeItem(ITEM *pSourceItem, ITEM *pTargetItem);

    static void UI2DEffectCallback(LPVOID pClass, DWORD dwParamA, DWORD dwParamB);

    //. PickedItem Control Functions
    CNewUIPickedItem *GetPickedItem();
    bool CreatePickedItem(CNewUIInventoryCtrl *pSrc, ITEM *pItem);
    void DeletePickedItem();
    void ReleasePickedItemView();
    void BackupPickedItem();

    //protected:
    void Render3D();
    void RenderNumberOfItem();
    void RenderItemToolTip();
};
} // namespace SEASON3B

#define g_pPickedItem g_pMyInventory->GetInventoryCtrl()->GetPickedItem()

// NewUIInventoryActionController.h

namespace SEASON3B
{

class CNewUIInventoryActionController : protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIInventoryActionController(SessionKeeper &keeper);
    ~CNewUIInventoryActionController() = default;

    void SetContext(IInventoryActionContext *pContext);
    bool HandleInventoryActions(CNewUIInventoryCtrl *targetControl) const;

  private:
    bool HandlePickedItemPlacement(CNewUIInventoryCtrl *targetControl) const;
    bool TryApplyJewel(CNewUIInventoryCtrl *targetControl, CNewUIPickedItem *pPickedItem,
                       ITEM *pPickItem, int iSourceIndex, int iTargetIndex) const;
    bool TryStackItem(CNewUIInventoryCtrl *targetControl, ITEM *pPickItem, int iSourceIndex,
                      int iTargetIndex) const;
    bool TryMoveItem(CNewUIInventoryCtrl *targetControl, CNewUIPickedItem *pPickedItem,
                     ITEM *pPickItem, int iSourceIndex, int iTargetIndex) const;

    bool HandleRepairClick(CNewUIInventoryCtrl *targetControl) const;

    bool HandleRightClick(CNewUIInventoryCtrl *targetControl) const;
    bool HandleStorageAutoMove(CNewUIInventoryCtrl *targetControl) const;
    bool HandleMixAutoMove(CNewUIInventoryCtrl *targetControl) const;
    bool HandleSellToNPC(CNewUIInventoryCtrl *targetControl) const;
    bool HandleInventoryRightClickActions(CNewUIInventoryCtrl *targetControl) const;
    bool TryEquipItem(CNewUIInventoryCtrl *targetControl, ITEM *pItem, int iSrcIndex) const;
    bool TryDropItem(CNewUIInventoryCtrl *targetControl, ITEM *pItem) const;

    int FindAlternateEquipSlot(int nOriginalSlot, ITEM *pItem) const;
    bool IsSlotOccupied(int nSlot) const;

    bool ApplyJewels(CNewUIInventoryCtrl *targetControl, CNewUIPickedItem *pPickedItem,
                     ITEM *pPickItem, int iSourceIndex, int iTargetIndex) const;
    bool TryStackItems(CNewUIInventoryCtrl *targetControl, ITEM *pPickItem, int iSourceIndex,
                       int iTargetIndex) const;
    bool RepairItemAtMousePoint(CNewUIInventoryCtrl *targetControl) const;
    bool TryConsumeItem(CNewUIInventoryCtrl *targetControl, ITEM *pItem, int iIndex) const;
    bool TryTransferBetweenInventorySections(CNewUIInventoryCtrl *sourceControl) const;

    IInventoryActionContext *m_pContext;
};

} // namespace SEASON3B

class SessionRenderUnit;
class SessionGameplayUnit;
class SessionKeeper;
class PetProcess;
class CDirection;
class CMonkSystem;

namespace SEASON3B
{
class CNewUIMyInventory : public CNewUIObj,
                          public INewUI3DRenderObj,
                          public IInventoryActionContext,
                          protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_INVENTORY_MYSHOP_OPEN_BTN = BITMAP_MYSHOPINTERFACE_NEW_PERSONALINVENTORY_BEGIN + 1,
        IMAGE_INVENTORY_MYSHOP_CLOSE_BTN = BITMAP_MYSHOPINTERFACE_NEW_PERSONALINVENTORY_BEGIN + 2,
        IMAGE_INVENTORY_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK,
        IMAGE_INVENTORY_BACK_TOP =
            BITMAP_INTERFACE_NEW_PERSONALINVENTORY_BEGIN,           //"newui_item_back01.tga"
        IMAGE_INVENTORY_BACK_TOP2,                                  //"newui_item_back04.tga"
        IMAGE_INVENTORY_BACK_LEFT,                                  //"newui_item_back02-L.tga"
        IMAGE_INVENTORY_BACK_RIGHT,                                 //"newui_item_back02-R.tga"
        IMAGE_INVENTORY_BACK_BOTTOM = BITMAP_INVENTORY_BACK_BOTTOM, //"newui_item_back03.tga"
        IMAGE_INVENTORY_ITEM_BOOT,                                  //"newui_item_boots.tga"
        IMAGE_INVENTORY_ITEM_HELM,                                  //"newui_item_cap.tga"
        IMAGE_INVENTORY_ITEM_FAIRY,                                 //"newui_item_fairy.tga"
        IMAGE_INVENTORY_ITEM_WING,                                  //"newui_item_wing.tga"
        IMAGE_INVENTORY_ITEM_RIGHT,                                 //"newui_item_weapon(L).tga"
        IMAGE_INVENTORY_ITEM_LEFT,                                  //"newui_item_weapon(R).tga"
        IMAGE_INVENTORY_ITEM_ARMOR,                                 //"newui_item_upper.tga"
        IMAGE_INVENTORY_ITEM_GLOVES,                                //"newui_item_gloves.tga"
        IMAGE_INVENTORY_ITEM_PANTS,                                 //"newui_item_lower.tga"
        IMAGE_INVENTORY_ITEM_RING,                                  //"newui_item_ring.tga"
        IMAGE_INVENTORY_ITEM_NECKLACE,                              //"newui_item_necklace.tga"
        IMAGE_INVENTORY_MONEY,                                      //"newui_item_money.tga"
        IMAGE_INVENTORY_EXIT_BTN,                                   //"newui_exit_00.tga"
        IMAGE_INVENTORY_REPAIR_BTN,                                 //"newui_repair_00.tga"
        IMAGE_INVENTORY_EXPAND_BTN,                                 //"newui_expansion_btn.tga"
    };

    enum MYSHOP_MODE
    {
        MYSHOP_MODE_OPEN = 0,
        MYSHOP_MODE_CLOSE,
    };

  private:
    enum ITEM_OPTION
    {
        ITEM_SET_OPTION = 1,
        ITEM_SOCKET_SET_OPTION = 2,
    };

    static constexpr float INVENTORY_WIDTH = 190.0f;
    static constexpr float INVENTORY_HEIGHT = 429.0f;

    typedef struct tagEQUIPMENT_ITEM
    {
        float x, y;
        float width, height;
    } EQUIPMENT_ITEM;

    CNewUIManager *m_pNewUIMng;
    CNewUI3DRenderMng *m_pNewUI3DRenderMng;
    CNewUIInventoryCtrl *m_pNewInventoryCtrl;
    PetProcess &g_petProcess;
    CDirection &g_Direction;
    CMonkSystem &g_CMonkSystem;
    SessionGameplayUnit &gameplay_;
    SessionRenderUnit &renderUnit_;
    vec3_t &CollisionPosition;
    float &terrainSelectX_;
    float &terrainSelectY_;
    CNewUIInventoryActionController m_ActionController;
    POINT m_Pos;

    EQUIPMENT_ITEM m_EquipmentSlots[MAX_EQUIPMENT_INDEX];
    int m_iPointedSlot;

    UI::Modern::PC::Inventory::RmlInventoryPanel m_ModernPanel;
    UI::Modern::PC::Inventory::RmlInventoryPanel::Content m_ModernContent;
    bool m_ModernVisible = false;
    bool m_ShopButtonLocked = false;

    MYSHOP_MODE m_MyShopMode;
    SEASON3B::REPAIR_MODE m_RepairMode;
    DWORD m_dwStandbyItemKey;

    bool m_bRepairEnableLevel;
    bool m_bMyShopOpen;

  public:
    explicit CNewUIMyInventory(SessionKeeper &keeper);
    virtual ~CNewUIMyInventory();

    bool Create(CNewUIManager *pNewUIMng, CNewUI3DRenderMng *pNewUI3DRenderMng, int x, int y);
    void Release();

    bool EquipItem(int iIndex, std::span<const BYTE> pbyItemPacket);
    void UnequipItem(int iIndex);
    void UnequipAllItems();

    SEASON3B::REPAIR_MODE GetRepairMode() const override;
    bool IsEquipable(int iIndex, ITEM *pItem) const override;
    void ResetMouseRButton() override;
    void ResetMouseLButton() override;
    int FindEmptySlot(ITEM *pItem) const override;
    bool IsRepairEnableLevel() const override;

    bool InsertItem(int iIndex, std::span<const BYTE> pbyItemPacket) const;
    void DeleteItem(int iIndex) const;
    void DeleteAllItems() const;

    void SetPos(int x, int y);
    const POINT &GetPos() const;

    void SetRepairMode(bool bRepair);

#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
    BOOL IsInvenItem(const short sType);
#endif

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    void Render3D();
    bool IsOwnerRendered() const override
    {
        return true;
    }
    bool PrepareModernUiOnWorker(int width, int height);
    std::optional<bool> ProcessModernUiInput(const SessionInputEvent &event);

    bool IsVisible() const;

    void OpenningProcess();
    void ClosingProcess();

    float GetLayerDepth();

    CNewUIInventoryCtrl *GetInventoryCtrl() const;

    ITEM *FindItem(int iLinealPos) const;
    ITEM *FindItemByKey(DWORD dwKey) const;
    int FindItemIndex(short int siType, int iLevel = -1) const;
    int FindItemReverseIndex(short sType, int iLevel = -1) const;
    int FindEmptySlot(IN int cx, IN int cy) const;
    int FindEmptySlotIncludingExtensions(IN int cx, IN int cy) const;
    int FindEmptySlotIncludingExtensions(ITEM *pItem) const;
    bool IsItem(short int siType, bool bcheckPick = false) const;
    int GetNumItemByKey(DWORD dwItemKey) const;
    int GetNumItemByType(short sItemType) const;
    BYTE GetDurabilityPointedItem() const;
    int GetPointedItemIndex() const;
    int FindManaItemIndex() const;
    int FindHealingItemIndex() const;

    static void UI2DEffectCallback(LPVOID pClass, DWORD dwParamA, DWORD dwParamB);

    void SetStandbyItemKey(DWORD dwItemKey);
    DWORD GetStandbyItemKey() const;
    int GetStandbyItemIndex() const;
    ITEM *GetStandbyItem() const;

    void SetRepairEnableLevel(bool bOver);

    void ChangeMyShopButtonStateOpen();
    void ChangeMyShopButtonStateClose();
    void LockMyShopButtonOpen();
    void UnlockMyShopButtonOpen();

    void CreateEquippingEffect(ITEM *pItem);

    static bool CanRegisterItemHotKey(int iType);
    bool HandleInventoryActions(CNewUIInventoryCtrl *targetControl);

    // Public to allow refresh when resolution changes
    void SetEquipmentSlotInfo();

  protected:
    void DeleteEquippingEffect();
    void DeleteEquippingEffectBug(ITEM *pItem);

  private:
    void LoadImages() const;
    void UnloadImages();

    void RenderSetOption();
    void RenderSetOptionList();
    void RenderSocketOption();
    void RenderEquippedItem();
    void StageModernContent();
    void UpdateEquippedPetInfo();
    void UpdateEquipmentPresentation();
    void SyncModernGeometry();
    bool IsMouseInModernPanel() const;
    bool HandleWorldItemDrop();

    bool EquipmentWindowProcess();
    bool InventoryProcess() const;
    bool BtnProcess();

    void RenderItemToolTip(int iSlotIndex) const;
    bool CanOpenMyShopInterface();
    void ToggleRepairMode();
};

} // namespace SEASON3B

class CmuConsoleDebug;

namespace SEASON3B
{
class CNewUIInGameShop : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum LISTBOX_INDEX
    {
        IGS_SAFEKEEPING_LISTBOX = 0,
        IGS_PRESENTBOX_LISTBOX,
        IGS_TOTAL_LISTBOX,
    };

    enum IMAGE_LIST
    {
        IMAGE_IGS_EXIT_BTN =
            CNewUIMyInventory::IMAGE_INVENTORY_EXIT_BTN, // newui_exit_00.tga (36, 58) - 2BtState
        IMAGE_IGS_BACK = BITMAP_INGAMESHOP_FRAME,        // Ingame_shopback.jpg (640, 429)
        IMAGE_IGS_CATEGORY_BTN,                          // Ingame_Bt01.tga (73, 81) - 3BtState
        IMAGE_IGS_CATEGORY_DECO_MIDDLE,                  // Ingame_Deco_Center.tga (6, 8)
        IMAGE_IGS_CATEGORY_DECO_DOWN,                    // Ingame_Deco_Dn.tga (47, 100)
        IMAGE_IGS_LEFT_TAB,                              // Ingame_Tab01.tga (49, 21)
        IMAGE_IGS_RIGHT_TAB,                             // Ingame_Tab02.tga (49, 21)
        IMAGE_IGS_ZONE_BTN,                              // Ingame_Tab_Up.tga (76, 46) - 2BtState
        IMAGE_IGS_ITEMGIFT_BTN,                          // Ingame_Bt_Gift.tga (25, 75) - 3BtState
        IMAGE_IGS_CASHGIFT_BTN,                          // Ingame_Bt_Cash.tga (25, 75) - 3BtState
        IMAGE_IGS_REFRESH_BTN,                           // Ingame_Bt_Reset.tga (25, 75) - 3BtState
        IMAGE_IGS_VIEWDETAIL_BTN,                        // Ingame_Bt_Bt03.tga (52, 78) - 3BtState
        IMAGE_IGS_ITEMBOX_LOGO,                          // Ingame_Itembox_logo.tga (57, 57)
        IMAGE_IGS_PAGE_LEFT,                             // Ingame_Bt_page_L.tga (20, 69) - 3BtState
        IMAGE_IGS_PAGE_RIGHT,                            // Ingame_Bt_page_R.tga (20, 69) - 3BtState
        IMAGE_IGS_STORAGE_PAGE,                          // IGS_Storage_Page.tga (80, 30)
        IMAGE_IGS_STORAGE_PAGE_LEFT,  // IGS_Storage_Page_Left.tga (20, 22) - 3BtState
        IMAGE_IGS_STORAGE_PAGE_RIGHT, // IGS_Storage_Page_Right.tga (20, 22) - 3BtState
        IMAGE_IGS_BANNER = BITMAP_INGAMESHOP_BANNER
    };

  private:
    enum INGAMESHOP_TEXT_INFO
    {
        TEXT_IGS_CHAR_NAME_POS_X = 498,
        TEXT_IGS_CHAR_NAME_POS_Y = 23,
        TEXT_IGS_CHAR_NAME_WIDTH = 122,
        TEXT_IGS_CASH_POS_X = 498,
        TEXT_IGS_CASH_POS_Y = 50,
        TEXT_IGS_CASH_WIDTH = 130,
        TEXT_IGS_MILEAGE_POS_Y = 65,
        TEXT_IGS_POINT_POS_Y = 80,
        TEXT_IGS_STORAGE_NAME_POS_X = 492,
        TEXT_IGS_STORAGE_NAME_POS_Y = 233,
        TEXT_IGS_STORAGE_NAME_WIDTH = 96,
        TEXT_IGS_STORAGE_TIME_POS_X = 592,
        TEXT_IGS_STORAGE_TIME_WIDTH = 34,
        TEXT_IGS_PAGE_POS_X = 251,
        TEXT_IGS_PAGE_POS_Y = 404,
        TEXT_IGS_STORAGE_PAGE_INFO_POS_X = 518,
        TEXT_IGS_STORAGE_PAGE_INFO_POS_Y = 376,
    };

    enum INGAMESHOP_IMAGES_POS
    {
        IMAGE_IGS_EXIT_BTN_POS_X = 484, // Exit Button
        IMAGE_IGS_EXIT_BTN_POS_Y = 392,
        IMAGE_IGS_BACK_POS_X = 0, // InGameShop Back
        IMAGE_IGS_BACK_POS_Y = 0,
        IMAGE_IGS_CATEGORY_BTN_POS_X = 13, // Category Button
        IMAGE_IGS_CATEGORY_BTN_POS_Y = 31,
        IMAGE_IGS_CATEGORY_BTN_DISTANCE = 6,
        IMAGE_IGS_TAB_BTN_POS_X = 486, // Tab Button
        IMAGE_IGS_TAB_BTN_POS_Y = 208,
        IMAGE_IGS_TAB_BTN_DISTANCE = -2,
        IMAGE_IGS_ZONE_BTN_POS_X = 95, // Zone Button
        IMAGE_IGS_ZONE_BTN_POS_Y = 0,
        IMAGE_IGS_VIEWDETAIL_BTN_POS_X = 162, // View Detail Button
        IMAGE_IGS_VIEWDETAIL_BTN_POS_Y = 126,
        IMAGE_IGS_VIEWDETAIL_BTN_DISTANCE_X = 122,
        IMAGE_IGS_VIEWDETAIL_BTN_DISTANCE_Y = 121,
        IMAGE_IGS_ITEMGIFT_BTN_POS_X = 519, // Item Gift Button
        IMAGE_IGS_CASHGIFT_BTN_POS_X = 546, // Cash Gift Button
        IMAGE_IGS_REFRESH_BTN_POS_X = 573,  // Refresh Button
        IMAGE_IGS_ICON_BTN_POS_Y = 94,
        IMAGE_IGS_USE_BTN_POS_X = 572,
        IMAGE_IGS_USE_BTN_POS_Y = 396,
        IMAGE_IGS_ITEMBOX_LOGO_POS_X = 128,
        IMAGE_IGS_ITEMBOX_LOGO_POS_Y = 52,
        IMAGE_IGS_PAGE_LEFT_POS_X = 231,  // Page Left Button
        IMAGE_IGS_PAGE_RIGHT_POS_X = 307, // Page Right Button
        IMAGE_IGS_PAGE_BUTTON_POS_Y = 397,
        IMAGE_IGS_BANNER_POS_X = 482, // Banner
        IMAGE_IGS_BANNER_POS_Y = 133,
        IMAGE_IGS_STORAGE_PAGE_POS_X = 518, // Storage Page
        IMAGE_IGS_STORAGE_PAGE_POS_Y = 366,
        IMAGE_IGS_STORAGE_PAGE_LEFT_POS_X = 512,  // Storage Page Left
        IMAGE_IGS_STORAGE_PAGE_RIGHT_POS_X = 586, // Storage Page Right
        IMAGE_IGS_STORAGE_PAGE_BTN_POS_Y = 372,
    };

    enum INGAMESHOP_IMAGES_SIZE
    {
        IMAGE_IGS_EXIT_BTN_WIDTH = 36, // Exit Button
        IMAGE_IGS_EXIT_BTN_HEIGHT = 29,
        IMAGE_IGS_BACK_WIDTH = 640, // InGameShop Back
        IMAGE_IGS_BACK_HEIGHT = 429,
        IMAGE_IGS_CATEGORY_BTN_WIDTH = 73, // Category Button
        IMAGE_IGS_CATEGORY_BTN_HEIGHT = 27,
        IMAGE_IGS_CATEGORY_DECO_MIDDLE_WIDTH = 4, // Category Deco Middle
        IMAGE_IGS_CATEGORY_DECO_MIDDLE_HEIGHT = 8,
        IMAGE_IGS_CATEGORY_DECO_DOWN_WIDTH = 47, // Category Deco Down
        IMAGE_IGS_CATEGORY_DECO_DOWN_HEIGHT = 100,
        IMAGE_IGS_TAB_BTN_WIDTH = 49, // Tab Button
        IMAGE_IGS_TAB_BTN_HEIGHT = 20,
        IMAGE_IGS_ZONE_BTN_WIDTH = 76, // Zone Button
        IMAGE_IGS_ZONE_BTN_HEIGHT = 23,
        IMAGE_IGS_VIEWDETAIL_BTN_WIDTH = 52, // View Detail Button
        IMAGE_IGS_VIEWDETAIL_BTN_HEIGHT = 26,
        IMAGE_IGS_ICON_BTN_WIDTH = 25,
        IMAGE_IGS_ICON_BTN_HEIGHT = 25,
        IMAGE_IGS_ITEMBOX_LOGO_SIZE = 57,
        IMAGE_IGS_PAGE_BTN_WIDTH = 20, // Page
        IMAGE_IGS_PAGE_BTN_HEIGHT = 23,
        IMAGE_IGS_BANNER_WIDTH = 153, // Banner
        IMAGE_IGS_BANNER_HEIGHT = 63,
        IMGAE_IGS_STORAGE_PAGE_WIDTH = 80, // Storage Page
        IMGAE_IGS_STORAGE_PAGE_HEIGHT = 30,
        IMGAE_IGS_STORAGE_PAGE_BTN_WIDTH = 20, // Storage Page Btn
        IMGAE_IGS_STORAGE_PAGE_BTN_HEIGHT = 22,
    };

    enum INGAMESHOP_DISPLAY_ITEMS
    {
        IGS_WIDTH_POS_X = 129,
        IGS_HEIGHT_POS_Y = 59,
        IGS_SIZE_WIDTH = 60,
        IGS_SIZE_HEIGHT = 49,
        IGS_NUM_ITEMS_WIDTH = 3,
        IGS_NUM_ITEMS_HEIGHT = 3,
        IGS_PACKAGE_NAME_POS_X = 105,
        IGS_PACKAGE_NAME_POS_Y = 40,
        IGS_PACKAGE_NAME_WIDTH = 104,
        IGS_PACKAGE_PRICE_POS_Y = 60,
        IGS_ITEMRENDER_POS_X_STANDAD = 102,
        IGS_ITEMRENDER_POS_WIDTH = 108,
        IGS_ITEMRENDER_POS_Y_STANDAD = 51,
        IGS_ITEMRENDER_POS_HEIGHT = 58,
        IGS_STORAGE_TOTAL_ITEM_PER_PAGE = 9,
    };

  public:
    explicit CNewUIInGameShop(SessionKeeper &keeper);
    virtual ~CNewUIInGameShop();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void LogSystemShow();

    void SetPos(int x, int y);
    const POINT &GetPos()
    {
        return m_Pos;
    }

    bool Render();
    bool Update();
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool BtnProcess();
    void SetBtnInfo();
    float GetLayerDepth()
    {
        return 10.08f;
    }

    bool GetItemRotation()
    {
        return m_ItemAngle;
    }
    void SetItemRotation(bool _bInput)
    {
        m_ItemAngle = _bInput;
    }

    void OpeningProcess();
    void ClosingProcess();

    void Release();

    bool IsInGameShopOpen();
    bool IsInGameShop();

    void InitZoneBtn();
    void InitCategoryBtn();

    void AddStorageItem(int iStorageSeq, int iStorageItemSeq, int iStorageGroupCode,
                        int iProductSeq, int iPriceSeq, int iCashPoint, wchar_t chItemType,
                        wchar_t *pszUserName = NULL, wchar_t *pszMessage = NULL);

    void ClearAllStorageItem();

    void InitStorage(int iTotalItemCnt, int iCurrentPageItemCnt, int iTotalPage, int iCurrentPage);
    char GetCurrentStorageCode();

    void StoragePrevPage();
    void StorageNextPage();
    void UpdateStorageItemList();

    void InitBanner(wchar_t *pszFileName, wchar_t *pszBannerURL);
    void ReleaseBanner();

    void SetRateScale(int _ItemType);
    float GetRateScale()
    {
        return m_fRate_Scale;
    }
    void SetConvertInvenCoord(WORD _ItemType, float _Width, float _Height);
    POINT GetConvertPos()
    {
        return m_fRePos;
    }
    POINT GetConvertSize()
    {
        return m_fReSize;
    }
    bool IsInGameShopRect(float _x, float _y);

  private:
    float m_fRate_Scale;
    POINT m_fRePos;
    POINT m_fReSize;

  private:
    void Init();
    void LoadImages();
    void UnloadImages();
    void RenderFrame();
    void RenderTexts();
    void RenderButtons();
    void RenderListBox();
    void RenderDisplayItems();

    void RenderBanner();
    bool UpdateBanner();

  private:
    CameraProjection &cameraProjection_;
    CInGameShopSystem &g_InGameShopSystem;
    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;
    bool m_ItemAngle;

    CNewUIRadioGroupButton m_ZoneButton;
    CNewUIRadioGroupButton m_CategoryButton;
    CNewUIRadioGroupButton m_ListBoxTabButton;
    SessionBoundArray<CNewUIButton, INGAMESHOP_DISPLAY_ITEMLIST_SIZE> m_ViewDetailButton;
    CNewUIButton m_CashGiftButton;
    CNewUIButton m_CashChargeButton;
    CNewUIButton m_CashRefreshButton;
    CNewUIButton m_UseButton;
    CNewUIButton m_PrevButton;
    CNewUIButton m_NextButton;
    CNewUIButton m_CloseButton;
    CNewUIButton m_StoragePrevButton;
    CNewUIButton m_StorageNextButton;

    bool m_bLoadBanner;
    bool m_bBannerLink;
    wchar_t m_szBannerURL[INTERNET_MAX_URL_LENGTH];

    int m_iStorageTotalItemCnt;
    int m_iStorageCurrentPageItemCnt;
    int m_iStorageTotalPage;
    int m_iStorageCurrentPage;
    int m_iSelectedStorageItemIndex;

    int m_iStorageCurrentPageReceiveItemCnt;
    bool m_bRequestCurrentPage;

    CUIInGameShopListBox m_StorageItemListBox;
};
} // namespace SEASON3B

#endif //PBG_ADD_INGAMESHOP_UI_ITEMSHOP

class SessionRenderUnit;

namespace SEASON3B
{
class CNewUIInventoryExtension : public CNewUIObj, protected SessionUiLegacyBindings
{
  private:
    CNewUIManager *m_pNewUIMng;
    CNewUIInventoryCtrl *m_extensions[MAX_INVENTORY_EXT_COUNT];

    UI::Modern::PC::Inventory::RmlInventoryExtensionPanel m_ModernPanel;
    bool m_ModernVisible = false;
    std::size_t m_ModernBagCount = 0;
    std::wstring m_ModernTitle;

  public:
    explicit CNewUIInventoryExtension(SessionKeeper &keeper);
    virtual ~CNewUIInventoryExtension();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool IsMouseInModernPanel() const;
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);
    std::optional<bool> ProcessModernUiInput(const SessionInputEvent &event);

    float GetLayerDepth(); //. 2.5f
    ITEM *FindItem(int iIndex) const;
    bool InsertItem(int iIndex, std::span<const BYTE> pbyItemPacket) const;
    void DeleteItem(int iIndex) const;
    void DeleteAllItems() const;
    int FindEmptySlot(int cx, int cy, const CNewUIInventoryCtrl *excluded = nullptr) const;
    CNewUIInventoryCtrl *GetOwnerOf(const CNewUIPickedItem *pPickedItem) const;

  private:
    SessionRenderUnit &renderUnit;
    void Init();

    CNewUIInventoryCtrl *TryGetExtensionByInventoryIndex(int iIndex) const;

    bool InventoryProcess();

    void SyncModernGeometry();
    bool ProcessModernChanges();
};
} // namespace SEASON3B

class SessionGameplayUnit;
namespace SEASON3B
{
class CNewUIItemEnduranceInfo : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIItemEnduranceInfo(SessionKeeper &keeper);
    ~CNewUIItemEnduranceInfo();
    bool Create(CNewUIManager *manager, int x, int y);
    void Release();
    void SetPos(int x, int y);
    void SetPos(int x);
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool BtnProcess();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    float GetLayerDepth();
    void OpenningProcess();
    void ClosingProcess();

  private:
    int WarningState(int slot) const;
    void StageAmmunition();
    void StageWarningTooltip();
    void StagePetFrame();
    bool UpdatePetFrameMouse();
    void AppendPetFrameRow(UI::Modern::RmlPetFrameRequest &request, const wchar_t *name, int life,
                           int maximum);
    bool GetEquippedHelperName(wchar_t *name, std::size_t capacity) const;
    SessionGameplayUnit &gameplay_;
    SessionRenderUnit &renderer_;
    CNewUIManager *m_pNewUIMng = nullptr;
    UI::Modern::PC::Inventory::RmlDurabilityLayer durability_;
    UI::Modern::PC::Inventory::RmlDurabilityLayer::Content content_;
    std::array<int, 4> ammunitionCounts_{-1, -1, -1, -1};
    std::string ammunitionLocale_;
    bool visible_ = false;
    POINT m_petFramePos{};
    int m_petFrameRowCount = 0;
    POINT m_petFrameDragOffset{};
    bool m_petFrameDragging = false;
    bool m_petFrameMinimized = false;
    bool m_petFrameMinimizePressed = false;
    ButtonVisualState m_petFrameButtonState = ButtonVisualState::Up;
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIItemExplanationWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIItemExplanationWindow(SessionKeeper &keeper);
    ~CNewUIItemExplanationWindow();
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
    bool StageContent();
    void BuildTable(bool singleLevel);
    CNewUIManager *manager_ = nullptr;
    SessionRenderUnit &renderer_;
    UI::Modern::PC::Inventory::RmlItemExplanationPanel panel_;
    UI::Modern::PC::Inventory::RmlItemExplanationPanel::Content content_;
    std::optional<std::array<int, 8>> key_;
    std::string locale_;
    bool visible_ = false;
};
} // namespace SEASON3B

class SessionRenderUnit;

class PresentationSeparationTestPeer;

namespace SEASON3B
{
#define LUCKYITEMMAXLINE 20
enum eNEWUIFRAME
{
    eFrame_BG,
    eFrame_T,
    eFrame_L,
    eFrame_R,
    eFrame_B,
    eFrame_END
};
enum eIMGLIST
{
    eImgList_MixBtn = eFrame_END,
    eImgList_END
};
enum eLUCKYITEMTYPE
{
    eLuckyItemType_None = 0,
    eLuckyItemType_Trade,
    eLuckyItemType_Refinery,
    eLuckyItemAct_End
};
enum eLUCKYITEM
{
    eLuckyItem_None = 0,
    eLuckyItem_Move,
    eLuckyItem_Act,
    eLuckyITem_Result,
    eLuckyItem_End
};

struct sImgList
{
    float s_fWid;
    float s_fHgt;
    int s_nImgIndex;
    void Set(int _nIndex, float _fWid, float _fHgt)
    {
        s_nImgIndex = _nIndex;
        s_fWid = _fWid;
        s_fHgt = _fHgt;
    }
};
struct sImgFrame
{
    sImgList s_Img;
    POINT s_ptPos;
};
struct sText
{
    int s_nTextIndex; // 글로벌 텍스트 인덱스
    DWORD s_dwColor;  // 텍스트 색깔
    int s_nLine;      // 텍스트 정렬
};

class CNewUILuckyItemWnd : public CNewUIObj, protected SessionUiLegacyBindings
{
  private:
    friend class ::PresentationSeparationTestPeer;
    CNewUIManager *m_pNewUIMng;
    SessionRenderUnit &renderUnit;
    UI::Modern::PC::Inventory::RmlItemPanel m_ModernPanel;
    void SyncModernGeometry();
    void StageModernContent();
    bool IsMouseInModernPanel() const;
    CNewUIInventoryCtrl *m_pNewInventoryCtrl;
    float m_fInvenClr[3];
    float m_fInvenClrWarning[3];
    wchar_t m_szSubject[255];
    sText m_sText[LUCKYITEMMAXLINE];
    int m_nTextMaxLine;
    int m_nResult;
    float m_mixEffectTicks;
    eLUCKYITEMTYPE m_eType;
    eLUCKYITEM m_eWndAction;
    eLUCKYITEM m_eEnd;

  private:
    void SetFrame_Text(eLUCKYITEM _eType);
    bool Process_InventoryCtrl(void);

    int GetLuckyItemRate(int _nType);
    void RenderMixEffect(void);
    void Reset(void);
    void AddText(int _nGlobalTextIndex, DWORD _dwColor = 0xFFFFFFFF, int _bLine = RT3_SORT_CENTER);

    bool Check_LuckyItem_Trade(ITEM *_pItem);
    bool Check_LuckyItem_Refinery(ITEM *_pItem);

  public:
    CNewUIInventoryCtrl *GetInventoryCtrl() const;

    int SetActAction();
    STORAGE_TYPE SetMoveAction();
    void GetResult(BYTE result);
    bool Process_BTN_Action(void);
    bool Process_InventoryCtrl_InsertItem(int iIndex, std::span<const BYTE> pbyItemPacket);
    void Process_InventoryCtrl_DeleteItem(int iIndex);
    bool Check_LuckyItem(ITEM *_pItem);
    bool Check_LuckyItem_InWnd(void);

    //-Virtual Function [lem_2010.9.1]
    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release(void);
    void OpeningProcess(void);
    bool ClosingProcess(void);
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);
    std::optional<bool> ProcessModernUiInput(const SessionInputEvent &event);
    float GetLayerDepth(); //. 3.4f
    //- Virtual Function End

    void SetAct(eLUCKYITEMTYPE type)
    {
        m_eType = type;
        if (m_pNewInventoryCtrl)
            m_pNewInventoryCtrl->SetStorageType(type == eLuckyItemType_Refinery
                                                    ? STORAGE_TYPE::LUCKYITEM_REFINERY
                                                    : STORAGE_TYPE::LUCKYITEM_TRADE);
    }
    void SetPos(int x, int y);

    __inline eLUCKYITEMTYPE GetAct(void)
    {
        return m_eType;
    }
    explicit CNewUILuckyItemWnd(SessionKeeper &keeper);
    virtual ~CNewUILuckyItemWnd();
};
} // namespace SEASON3B

class SessionGameDataUnit;
class SessionRenderUnit;

class PresentationSeparationTestPeer;

namespace SEASON3B
{
class CNewUIMixInventory : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_MIXINVENTORY_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, // Reference
        IMAGE_MIXINVENTORY_TOP = CNewUIMyInventory::IMAGE_INVENTORY_BACK_TOP2,
        IMAGE_MIXINVENTORY_LEFT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_LEFT,
        IMAGE_MIXINVENTORY_RIGHT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_RIGHT,
        IMAGE_MIXINVENTORY_BOTTOM = CNewUIMyInventory::IMAGE_INVENTORY_BACK_BOTTOM,
        IMAGE_MIXINVENTORY_MIXBTN = BITMAP_INTERFACE_NEW_MIXINVENTORY_BEGIN
    };
    enum MIX_STATE
    {
        MIX_READY = 0,
        MIX_REQUESTED,
        MIX_FINISHED
    };

  private:
    friend class ::PresentationSeparationTestPeer;
    static constexpr float INVENTORY_WIDTH = 190.0f;
    static constexpr float INVENTORY_HEIGHT = 429.0f;

    const SessionGameDataUnit &gameData_;
    SessionRenderUnit &renderUnit_;
    int &g_nChaosTaxRate;

    CNewUIManager *m_pNewUIMng;
    CNewUIInventoryCtrl *m_pNewInventoryCtrl;
    POINT m_Pos;

    UI::Modern::PC::Inventory::RmlItemPanel m_ModernPanel;

    int m_iMixState;
    float m_mixEffectTicks;
    float m_fInventoryColor[3];
    float m_fInventoryWarningColor[3];

    int m_SelectedSocket = -1;

  public:
    explicit CNewUIMixInventory(SessionKeeper &keeper);
    virtual ~CNewUIMixInventory();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    bool InsertItem(int iIndex, std::span<const BYTE> pbyItemPacket);
    bool ProcessMyInvenItemAutoMove(CNewUIInventoryCtrl *sourceCtrl = nullptr);
    bool ProcessMixItemAutoMoveToInventory();
    void DeleteItem(int iIndex);
    void DeleteAllItems();

    void OpeningProcess();
    bool ClosingProcess();

    void SetMixState(int iMixState);
    int GetMixState()
    {
        return m_iMixState;
    }

    int GetPointedItemIndex();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);
    std::optional<bool> ProcessModernUiInput(const SessionInputEvent &event);

    float GetLayerDepth(); //. 3.4f

    CNewUIInventoryCtrl *GetInventoryCtrl() const;

  private:
    bool InventoryProcess();
    bool BtnProcess();

    bool AutoMoveItem(CNewUIInventoryCtrl *srcCtrl, STORAGE_TYPE srcType,
                      CNewUIInventoryCtrl *dstCtrl, STORAGE_TYPE dstType, bool requireMixSource);

    void BuildMixDescriptions(std::string &guide);
    void SyncModernGeometry();
    bool IsMouseInModernPanel() const;
    void StageModernContent();
    void UpdateSocketSelection();

    void CheckMixInventory();
    bool Mix();
    void RenderMixEffect();

    int Rtn_MixRequireZen(int _nMixZen, int _nTax);
};
} // namespace SEASON3B

class SessionRenderUnit;

namespace SEASON3B
{
class CNewUIMyShopInventory;

class MyShopInventoryLegacyCalls : protected SessionUiLegacyBindings
{
  protected:
    MyShopInventoryLegacyCalls(SessionKeeper &keeper, CNewUIMyShopInventory &owner) noexcept;

  private:
    CNewUIMyShopInventory &owner_;
};

class CNewUIMyShopInventory : public CNewUIObj, protected MyShopInventoryLegacyCalls
{
    friend class MyShopInventoryLegacyCalls;

  public:
    enum IMAGE_LIST
    {
        IMAGE_MYSHOPINVENTORY_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, // Reference
        IMAGE_MYSHOPINVENTORY_TOP = CNewUIMyInventory::IMAGE_INVENTORY_BACK_TOP,
        IMAGE_MYSHOPINVENTORY_LEFT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_LEFT,
        IMAGE_MYSHOPINVENTORY_RIGHT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_RIGHT,
        IMAGE_MYSHOPINVENTORY_BOTTOM = CNewUIMyInventory::IMAGE_INVENTORY_BACK_BOTTOM,
        IMAGE_MYSHOPINVENTORY_EXIT_BTN = CNewUIMyInventory::IMAGE_INVENTORY_EXIT_BTN,

        IMAGE_MYSHOPINVENTORY_EDIT = BITMAP_MYSHOPINTERFACE_NEW_PERSONALINVENTORY_BEGIN,
        IMAGE_MYSHOPINVENTORY_OPEN,
        IMAGE_MYSHOPINVENTORY_CLOSE,
    };

    enum SHOPTYEP
    {
        PERSONALSHOPSALE = 0,
        PERSONALSHOPPURCHASE,
    };

    CNewUIManager *m_pNewUIMng;
    CNewUIInventoryCtrl *m_pNewInventoryCtrl;

  public:
    explicit CNewUIMyShopInventory(SessionKeeper &keeper);
    virtual ~CNewUIMyShopInventory();
    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();
    void SetPos(int x, int y);
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    void ClosingProcess();
    float GetLayerDepth(); //. 3.2f
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight);
    std::optional<bool> ProcessModernUiInput(const SessionInputEvent &event);
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const;

    CNewUIInventoryCtrl *GetInventoryCtrl() const;

  public:
    void ChangeSourceIndex(int sindex);
    const int GetSourceIndex();
    void ChangeTargetIndex(int tindex);
    const int GetTargetIndex();
    ITEM *FindItem(int iLinealPos);
    int GetItemInventoryIndex(ITEM *pItem);
    void ChangeTitle(wchar_t *titletext);
    void GetTitle(wchar_t *titletext);
    void SetTitle(wchar_t *titletext);
    void ChangePersonal(bool state);
    const bool IsEnablePersonalShop() const;
    void OpenButtonLock();
    void OpenButtonUnLock();
    void UpdateOpenButtonLockForCurrentMap();
    int GetPointedItemIndex();
    void ResetSubject();
    bool IsEnableInputValueTextBox();
    void SetInputValueTextBox(bool bIsEnable);

  public:
    bool InsertItem(int iIndex, std::span<const BYTE> pbyItemPacket);
    void DeleteItem(int iIndex);
    void DeleteAllItems();

  private:
    bool MyShopInventoryProcess();
    bool HandlePickedItemDrop(CNewUIPickedItem &pickedItem);
    bool OpenPriceEditorForSlot(int slot);
    bool ProcessModernChanges();
    void OpenShop();
    void CloseShop();
    void CloseShopWindows();
    void SyncModernInventoryGeometry();
    bool IsMouseInModernPanel() const;
    UI::Modern::PC::Inventory::RmlPrivateStoreContent BuildModernContent() const;

  private:
    SessionRenderUnit &renderUnit;
    int m_TargetIndex;
    int m_SourceIndex;
    bool m_EnablePersonalShop;
    bool m_OpenButtonLocked;
    bool m_bIsEnableInputValueTextBox;
    std::wstring m_ShopTitle;
    UI::Modern::PC::Inventory::RmlPrivateStorePanel m_ModernPanel;
    UI::Modern::PC::Inventory::RmlPrivateStoreContent m_ModernContent;
    bool m_ModernVisible = false;
};

inline void CNewUIMyShopInventory::ChangeTitle(wchar_t *titletext)
{
    SetTitle(titletext);
}

inline void CNewUIMyShopInventory::ChangeSourceIndex(int sindex)
{
    m_SourceIndex = sindex;
}

inline void CNewUIMyShopInventory::ChangeTargetIndex(int tindex)
{
    m_TargetIndex = tindex;
}

inline CNewUIInventoryCtrl *CNewUIMyShopInventory::GetInventoryCtrl() const
{
    return m_pNewInventoryCtrl;
}

inline const int CNewUIMyShopInventory::GetSourceIndex()
{
    return m_SourceIndex;
}

inline const int CNewUIMyShopInventory::GetTargetIndex()
{
    return m_TargetIndex;
}
} // namespace SEASON3B

class CErrorReport;

class SessionRenderUnit;

namespace SEASON3B
{
class CNewUIPurchaseShopInventory;

class PurchaseShopInventoryLegacyCalls : protected SessionUiLegacyBindings
{
  protected:
    PurchaseShopInventoryLegacyCalls(SessionKeeper &keeper,
                                     CNewUIPurchaseShopInventory &owner) noexcept;

  private:
    CNewUIPurchaseShopInventory &owner_;
};

class CNewUIPurchaseShopInventory : public CNewUIObj, protected PurchaseShopInventoryLegacyCalls
{
    friend class PurchaseShopInventoryLegacyCalls;

  public:
    explicit CNewUIPurchaseShopInventory(SessionKeeper &keeper);
    virtual ~CNewUIPurchaseShopInventory();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();
    void SetPos(int x, int y);
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight);
    std::optional<bool> ProcessModernUiInput(const SessionInputEvent &event);

    void ClosingProcess();

  public:
    float GetLayerDepth(); //. 3.2f
    CNewUIInventoryCtrl *GetInventoryCtrl() const;
    const int GetShopCharacterIndex();
    const std::wstring &GetTitleText();
    const int GetSourceIndex();
    int GetPointedItemIndex();

  public:
    void ChangeShopCharacterIndex(int index);
    void ChangeTitleText(wchar_t *text);
    void ChangeSourceIndex(int sindex);

  public:
    bool InsertItem(int iIndex, std::span<const BYTE> pbyItemPacket);
    void DeleteItem(int iIndex);
    ITEM *FindItem(int iLinealPos);
    int GetItemInventoryIndex(ITEM *pItem);

  private:
    bool ProcessModernChanges();
    bool OpenPurchaseForSlot(int slot);
    void SyncModernInventoryGeometry();
    bool IsMouseInModernPanel() const;
    UI::Modern::PC::Inventory::RmlPrivateStoreContent BuildModernContent() const;

  private:
    SessionRenderUnit &renderUnit;
    CNewUIManager *m_pNewUIMng;
    CNewUIInventoryCtrl *m_pNewInventoryCtrl;
    int m_ShopCharacterIndex;
    std::wstring m_TitleText;
    int m_SourceIndex;
    UI::Modern::PC::Inventory::RmlPrivateStorePanel m_ModernPanel;
    UI::Modern::PC::Inventory::RmlPrivateStoreContent m_ModernContent;
    bool m_ModernVisible = false;
};

inline void CNewUIPurchaseShopInventory::ChangeShopCharacterIndex(int index)
{
    m_ShopCharacterIndex = index;
}

inline void CNewUIPurchaseShopInventory::ChangeTitleText(wchar_t *text)
{
    m_TitleText = text;
}

inline void CNewUIPurchaseShopInventory::ChangeSourceIndex(int sindex)
{
    m_SourceIndex = sindex;
}

inline CNewUIInventoryCtrl *CNewUIPurchaseShopInventory::GetInventoryCtrl() const
{
    return m_pNewInventoryCtrl;
}

inline const int CNewUIPurchaseShopInventory::GetShopCharacterIndex()
{
    return m_ShopCharacterIndex;
}

inline const std::wstring &CNewUIPurchaseShopInventory::GetTitleText()
{
    return m_TitleText;
}

inline const int CNewUIPurchaseShopInventory::GetSourceIndex()
{
    return m_SourceIndex;
}
}; // namespace SEASON3B

namespace SEASON3B
{
class CNewUISetItemExplanation : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUISetItemExplanation(SessionKeeper &keeper);
    ~CNewUISetItemExplanation();
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
    void StageContent();
    CNewUIManager *manager_ = nullptr;
    SessionRenderUnit &renderer_;
    UI::Modern::PC::Inventory::RmlItemExplanationPanel panel_;
    UI::Modern::PC::Inventory::RmlItemExplanationPanel::Content content_;
    int selection_ = -1;
    std::string locale_;
    bool visible_ = false;
};
} // namespace SEASON3B

class SessionRenderUnit;

namespace SEASON3B
{
class CNewUIStorageInventory : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_STORAGE_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, // Reference
        IMAGE_STORAGE_TOP = CNewUIMyInventory::IMAGE_INVENTORY_BACK_TOP,
        IMAGE_STORAGE_LEFT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_LEFT,
        IMAGE_STORAGE_RIGHT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_RIGHT,
        IMAGE_STORAGE_BOTTOM = CNewUIMyInventory::IMAGE_INVENTORY_BACK_BOTTOM,
        IMAGE_STORAGE_EXPAND_BTN = CNewUIMyInventory::IMAGE_INVENTORY_EXPAND_BTN,

        IMAGE_STORAGE_BTN_INSERT_ZEN = BITMAP_INTERFACE_NEW_STORAGE_BEGIN,
        IMAGE_STORAGE_BTN_TAKE_ZEN = BITMAP_INTERFACE_NEW_STORAGE_BEGIN + 1,
        IMAGE_STORAGE_BTN_UNLOCK = BITMAP_INTERFACE_NEW_STORAGE_BEGIN + 2,
        IMAGE_STORAGE_BTN_LOCK = BITMAP_INTERFACE_NEW_STORAGE_BEGIN + 3,

        IMAGE_STORAGE_MONEY = BITMAP_INTERFACE_NEW_STORAGE_BEGIN + 4,
    };

  private:
    static constexpr float STORAGE_WIDTH = 190.0f;
    static constexpr float STORAGE_HEIGHT = 429.0f;

    enum STORAGE_BUTTON
    {
        BTN_INSERT_ZEN = 0,
        BTN_TAKE_ZEN,
        BTN_LOCK,
        MAX_BTN
    };

    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;
    CNewUIInventoryCtrl *m_pNewInventoryCtrl;

    bool m_bLock;
    bool m_bCorrectPassword;

    bool m_bItemAutoMove;
    int m_nBackupMouseX;
    int m_nBackupMouseY;

    bool m_bTakeZen;
    int m_nBackupTakeZen;
    int m_nBackupInvenIndex;
    int m_nBackupSourceInvenIndex;

  public:
    explicit CNewUIStorageInventory(SessionKeeper &keeper);
    virtual ~CNewUIStorageInventory();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);
    std::optional<bool> ProcessModernUiInput(const SessionInputEvent &event);

    float GetLayerDepth(); //. 2.2f

    CNewUIInventoryCtrl *GetInventoryCtrl() const;

    bool ProcessClosing();
    bool InsertItem(int iIndex, std::span<const BYTE> pbyItemPacket);
    int FindEmptySlot(ITEM *pItemObj);

    bool IsStorageLocked()
    {
        return m_bLock;
    }
    bool IsCorrectPassword()
    {
        return m_bCorrectPassword;
    }
    bool IsItemAutoMove()
    {
        return m_bItemAutoMove;
    }

    void SetBackupTakeZen(int nZen);

    bool ProcessMyInvenItemAutoMove(CNewUIInventoryCtrl *sourceCtrl = nullptr);

    void SendRequestItemToMyInven(ITEM *pItemObj, int nStorageIndex, int nInvenIndex);

    void ProcessToReceiveStorageStatus(BYTE byStatus);

    int GetPointedItemIndex();

    void SetItemAutoMove(bool bItemAutoMove, int nSourceInvenIndex = -1);
    void SendRequestItemToStorage(ITEM *pItemObj, int nInvenIndex, int nStorageIndex);

  private:
    SessionRenderUnit &renderUnit;
    UI::Modern::PC::Inventory::RmlItemPanel m_ModernPanel;
    void SyncModernGeometry();
    void StageModernContent();
    bool IsMouseInModernPanel() const;

    void DeleteAllItems();

    void LockStorage(bool bLock);
    void SetCorrectPassword(bool bCorrectPassword)
    {
        m_bCorrectPassword = bCorrectPassword;
    }

    void InitBackupItemInfo();
    int GetBackupTakeZen()
    {
        return m_nBackupTakeZen;
    }
    void SetBackupInvenIndex(int nInvenIndex);
    int GetBackupInvenIndex()
    {
        return m_nBackupInvenIndex;
    }

    void ProcessInventoryCtrl();
    bool ProcessBtns();
    void ProcessStorageItemAutoMove();
};
} // namespace SEASON3B

class SessionRenderUnit;

namespace SEASON3B
{
class CNewUIStorageInventoryExt : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_STORAGE_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, // Reference
        IMAGE_STORAGE_TOP = CNewUIMyInventory::IMAGE_INVENTORY_BACK_TOP,
        IMAGE_STORAGE_LEFT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_LEFT,
        IMAGE_STORAGE_RIGHT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_RIGHT,
        IMAGE_STORAGE_BOTTOM = CNewUIMyInventory::IMAGE_INVENTORY_BACK_BOTTOM,
        IMAGE_INVENTORY_EXIT_BTN = CNewUIMyInventory::IMAGE_INVENTORY_EXIT_BTN,
    };

  private:
    static constexpr float STORAGE_WIDTH = 190.0f;
    static constexpr float STORAGE_HEIGHT = 429.0f;

    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;

    CNewUIInventoryCtrl *m_pNewInventoryCtrl;

    bool m_bItemAutoMove;
    int m_nBackupMouseX;
    int m_nBackupMouseY;
    int m_nBackupSourceInvenIndex;

  public:
    explicit CNewUIStorageInventoryExt(SessionKeeper &keeper);
    ~CNewUIStorageInventoryExt() override;

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent() override;
    bool UpdateKeyEvent() override;
    bool Update() override;
    bool Render() override;
    bool PrepareModernUiOnWorker(int width, int height);
    std::optional<bool> ProcessModernUiInput(const SessionInputEvent &event);

    float GetLayerDepth() override; //. 2.2f

    CNewUIInventoryCtrl *GetInventoryCtrl() const;

    bool ProcessClosing() const;
    bool InsertItem(int iIndex, std::span<const BYTE> pbyItemPacket) const;
    int FindEmptySlot(const ITEM *pItemObj) const;
    bool ProcessMyInvenItemAutoMove(CNewUIInventoryCtrl *sourceCtrl = nullptr);

    bool IsItemAutoMove() const
    {
        return m_bItemAutoMove;
    }

    int GetPointedItemIndex() const;

    void SetItemAutoMove(bool bItemAutoMove, int nSourceInvenIndex = -1);

  private:
    SessionRenderUnit &renderUnit;
    UI::Modern::PC::Inventory::RmlItemPanel m_ModernPanel;
    void SyncModernGeometry();
    void StageModernContent();
    bool IsMouseInModernPanel() const;

    void DeleteAllItems() const;

    void ProcessInventoryCtrl();
    bool ProcessBtns();
    void ProcessStorageItemAutoMove();
};
} // namespace SEASON3B

// Desc: interface for the CNewUITrade class.
//       Trade Window class.
// producer: Ahn Sang-Kyu

class SessionRenderUnit;

namespace SEASON3B
{
class CNewUITrade : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_TRADE_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, // Reference
        IMAGE_TRADE_TOP = CNewUIMyInventory::IMAGE_INVENTORY_BACK_TOP,
        IMAGE_TRADE_LEFT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_LEFT,
        IMAGE_TRADE_RIGHT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_RIGHT,
        IMAGE_TRADE_BOTTOM = CNewUIMyInventory::IMAGE_INVENTORY_BACK_BOTTOM,

        IMAGE_TRADE_LINE = BITMAP_INTERFACE_MYQUEST_WINDOW_BEGIN,
        IMAGE_TRADE_NICK_BACK = BITMAP_INTERFACE_NEW_TRADE_BEGIN,
        IMAGE_TRADE_MONEY = CNewUIMyInventory::IMAGE_INVENTORY_MONEY,
        IMAGE_TRADE_CONFIRM = BITMAP_INTERFACE_NEW_TRADE_BEGIN + 1,
        IMAGE_TRADE_WARNING_ARROW = BITMAP_CURSOR + 7,

        IMAGE_TRADE_BTN_CLOSE = CNewUIMyInventory::IMAGE_INVENTORY_EXIT_BTN,
        IMAGE_TRADE_BTN_ZEN_INPUT = CNewUIStorageInventory::IMAGE_STORAGE_BTN_INSERT_ZEN,
    };

  private:
    enum
    {
        TRADE_WIDTH = 190,
        TRADE_HEIGHT = 429,
        CONFIRM_WIDTH = 36,
        CONFIRM_HEIGHT = 29,
        COLUMN_TRADE_INVEN = 8,
        ROW_TRADE_INVEN = 4,
        MAX_TRADE_INVEN = COLUMN_TRADE_INVEN * ROW_TRADE_INVEN,
    };

    enum TRADE_BUTTON
    {
        BTN_CLOSE = 0, // Close window
        BTN_ZEN_INPUT, // Zen input
        MAX_BTN
    };

    CNewUIManager *m_pNewUIMng;               // UI Manager
    POINT m_Pos;                              // Window position
    CNewUIInventoryCtrl *m_pYourInvenCtrl;    // Other player's item control
    CNewUIInventoryCtrl *m_pMyInvenCtrl;      // My item control
    ITEM m_aYourInvenBackUp[MAX_TRADE_INVEN]; // Other player's item backup

    wchar_t m_szYourID[MAX_USERNAME_SIZE + 1]; // Other player's ID
    int m_nYourLevel;                          // Other player's level
    int m_nYourGuildType;                      // Other player's guild type
    int m_nYourTradeGold;                      // Other player's trade gold
    int m_nMyTradeGold;                        // My trade gold
    int m_nTempMyTradeGold;                    // Temporary buffer for my trade gold
    bool m_bYourConfirm;                       // Other player's confirmation status
    bool m_bMyConfirm;                         // My confirmation status
    float m_nMyTradeWait;                      // Delay to prevent spamming my confirm button
    bool m_bTradeAlert;                        // Trade warning alert

  public:
    explicit CNewUITrade(SessionKeeper &keeper);
    virtual ~CNewUITrade();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);
    std::optional<bool> ProcessModernUiInput(const SessionInputEvent &event);

    float GetLayerDepth(); //. 2.1f

    // Returns the other player's (grid-based) trade inventory control.
    CNewUIInventoryCtrl *GetYourInvenCtrl() const
    {
        return m_pYourInvenCtrl;
    }
    // Returns the local player's (grid-based) trade inventory control.
    CNewUIInventoryCtrl *GetMyInvenCtrl() const
    {
        return m_pMyInvenCtrl;
    }

    void ProcessCloseBtn();
    void ProcessClosing();

    void GetYourID(wchar_t *pszYourID);
    void SetYourTradeGold(int nGold)
    {
        m_nYourTradeGold = nGold;
    }

    void SendRequestMyGoldInput(int nInputGold);
    void SendRequestItemToMyInven(ITEM *pItemObj, int nTradeIndex, int nInvenIndex);

    void ProcessToReceiveTradeRequest(char *pbyYourID);
    void ProcessToReceiveTradeResult(LPPTRADE pTradeData);
    void ProcessToReceiveMyTradeGold(BYTE bySuccess);
    void ProcessToReceiveYourConfirm(BYTE byState);
    void ProcessToReceiveTradeExit(BYTE byState);

    void AlertTrade();
    void BackUpYourInven(int nYourInvenIndex);
    void AlertYourTradeInven();

    int GetPointedItemIndexMyInven();
    int GetPointedItemIndexYourInven();

  private:
    SessionRenderUnit &renderUnit;
    UI::Modern::PC::Inventory::RmlItemPanel m_ModernPanel;
    void SyncModernGeometry();
    void StageModernContent();
    bool IsMouseInModernPanel() const;

    void ProcessMyInvenCtrl();
    bool ProcessBtns();

    void ConvertYourLevel(int &rnLevel, DWORD &rdwColor);

    void InitTradeInfo();
    void InitYourInvenBackUp();
    void BackUpYourInven(ITEM *pYourItemObj);

    void SendRequestItemToTrade(ITEM *pItemObj, int nInvenIndex, int nTradeIndex);
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIManager;
class CNewUI3DRenderMng;
class CNewUIUnitedMarketPlaceWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIUnitedMarketPlaceWindow(SessionKeeper &keeper);
    ~CNewUIUnitedMarketPlaceWindow();
    bool Create(CNewUIManager *manager, CNewUI3DRenderMng *, int x, int y);
    void Release();
    void SetPos(int x, int y);
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    void OpeningProcess();
    void ClosingProcess();
    float GetLayerDepth();
    void SetRemainTime(int time);
    void LockEnterButton(BOOL lock);

  private:
    void RequestTravel();
    void StageContent();
    CNewUIManager *manager_ = nullptr;
    SessionRenderUnit &renderer_;
    UI::Modern::RmlMessageBoxPanel panel_;
    UI::Modern::RmlMessageBoxContent content_;
    std::string locale_;
    int contentWorld_ = -1;
    bool locked_ = false;
};
} // namespace SEASON3B

// guild

// text 관련

// party

// inventory

#ifdef AUTO_CHANGE_ITEM
void AutoEquipmentChange(int sx, int sy, ITEM *Inv, int InvWidth, int InvHeight);
#endif // AUTO_CHANGE_ITEM

void ConvertGold(double dGold, wchar_t *szText, int iDecimals = 0);
void ConvertGold64(__int64 Gold, wchar_t *Text);

//  Party.
void OpenPersonalShop(int iType);
void OpenPersonalShopMsgWnd(int iMsgType);
bool IsCorrectShopTitle(const wchar_t *szShopTitle);

bool IsRequireClassRenderItem(const short sType);
unsigned int getGoldColor(DWORD Gold);

namespace InventoryControlDetail
{
using namespace SEASON3B;
int StackedConsumableQuantity(const ITEM &item);
} // namespace InventoryControlDetail

namespace DurabilityPanelDetail
{
using namespace SEASON3B;
int PetFrameLogicalSize(float gfxPixels, float screenRate, float maximumScale);
} // namespace DurabilityPanelDetail

namespace MixInventoryDetail
{
using namespace SEASON3B;
#pragma pack(push)
#pragma pack()
inline void AppendMixText(std::string &target, const wchar_t *text, const char *tone = "normal")
{
    if (!text || !*text)
        return;
    target += "<span class=\"";
    target += tone;
    target += "\">";
    target += Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(text));
    target += "</span><br/>";
}
#pragma pack(pop)

} // namespace MixInventoryDetail

namespace PersonalShopDetail
{
using namespace SEASON3B;
#pragma pack(push)
#pragma pack()
inline const int iMAX_SHOPTITLE_MULTI = 26;
#pragma pack(pop)

} // namespace PersonalShopDetail
