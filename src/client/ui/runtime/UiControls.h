#pragma once
inline constexpr int NUM_LINE_CMB = 7;
#include "app/ApplicationConfigScheduling.h"
#include "domain/CharacterPresentation.h"
#include "domain/Guild.h"
#include "domain/Quests.h"
#include "render/Sprites.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include "ui/runtime/UiRuntime.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

class CSlideHelpMgr;
namespace UI::Modern
{
class RmlMuButton;
}
namespace SEASON3B
{
class CNewUIManager;
}

namespace SEASON3B
{
class INewUIBase
{
  public:
    virtual bool Render() = 0;
    virtual bool Update() = 0;
    virtual bool UpdateMouseEvent() = 0;
    virtual bool UpdateKeyEvent() = 0;

    virtual float GetLayerDepth() = 0;
    virtual float GetKeyEventOrder() = 0;

    virtual bool IsVisible() const = 0;
    virtual bool IsEnabled() const = 0;
};

class CNewUIObj : public INewUIBase
{
    HWND m_hRelatedWnd;
    bool m_bRender, m_bUpdate;
    std::uint64_t m_dynamicLayerOrder;

  public:
    CNewUIObj() : m_hRelatedWnd(nullptr), m_bRender(true), m_bUpdate(true), m_dynamicLayerOrder(0)
    {
    }
    virtual ~CNewUIObj()
    {
    }

    void SetRelatedWnd(HWND hWnd)
    {
        m_hRelatedWnd = hWnd;
    }
    HWND GetRelatedWnd() const
    {
        return m_hRelatedWnd;
    }

    void Show(bool bShow)
    {
        m_bRender = bShow;
    }
    void Enable(bool bEnable)
    {
        m_bUpdate = bEnable;
    }

    bool IsVisible() const override
    {
        return m_bRender;
    }
    bool IsEnabled() const override
    {
        return m_bUpdate;
    }

    void SetDynamicLayerOrder(std::uint64_t order)
    {
        m_dynamicLayerOrder = order;
    }
    std::uint64_t GetDynamicLayerOrder() const
    {
        return m_dynamicLayerOrder;
    }

    virtual float GetKeyEventOrder()
    {
        return 3.0f;
    } //. Default
};
} // namespace SEASON3B

#define BTN_UP 0
#define BTN_DOWN 1
#define BTN_ACTIVE 2
#define BTN_DISABLE 3
#define BTN_UP_CHECK 4
#define BTN_DOWN_CHECK 5
#define BTN_ACTIVE_CHECK 6
#define BTN_DISABLE_CHECK 7
#define BTN_IMG_MAX 8
#define SLD_STATE_IDLE 0x00
#define SLD_STATE_UP 0x01
#define SLD_STATE_DN 0x02
#define SLD_STATE_THUMB_DRG 0x10
#define WS_NORMAL 0
#define WS_ETC 1
#define WS_MOVE 2
#define WS_EXTEND_DN 3
#define WS_EXTEND_UP 4
#define WA_ALL 0
#define WA_TOOLTIP 1
#define WA_MOVE 2
#define WA_BUTTON 3
#define WA_EXTEND_DN 4
#define WA_EXTEND_UP 5

// State names follow GFx AS2 gfx.controls.Button's stateMap.
enum class ButtonVisualState : std::uint8_t
{
    Up,
    Over,
    Down,
    Disabled,
};

class CButton : public CSprite
{
  protected:
    CButton *&m_pBtnHeld;

    bool m_bEnable;
    bool m_bActive;
    bool m_bClick;
    bool m_bCheck;

    std::array<int, BTN_IMG_MAX> m_imageFrames{};
    std::wstring m_text;
    mutable std::vector<wchar_t> m_textBuffer;
    std::array<DWORD, BTN_IMG_MAX> m_textColors{};
    std::size_t m_textColorCount{0};
    DWORD m_textColor{0};
    float m_fTextAddYPos{0.0f};
    ButtonVisualState m_visualState = ButtonVisualState::Up;

  public:
    explicit CButton(SessionKeeper &keeper);
    virtual ~CButton();

    void Release();
    void Create(int nWidth, int nHeight, int nTexID, int nMaxFrame = 1, int nDownFrame = -1,
                int nActiveFrame = -1, int nDisableFrame = -1, int nCheckUpFrame = -1,
                int nCheckDownFrame = -1, int nCheckActiveFrame = -1, int nCheckDisableFrame = -1);
    void Update();
    void Render();
    void Show(bool bShow = true);
    BOOL CursorInObject();
    void SetEnable(bool bEnable = true)
    {
        m_bEnable = bEnable;
    }
    bool IsEnable() const
    {
        return m_bEnable;
    }
    void SetActive(bool bActive = true)
    {
        m_bActive = bActive;
    }
    bool IsClick() const
    {
        return m_bClick;
    }
    ButtonVisualState VisualState() const noexcept
    {
        return m_visualState;
    }
    void SetCheck(bool bCheck = true)
    {
        m_bCheck = bCheck;
    }
    bool IsCheck()
    {
        return m_bCheck;
    }
    void SetText(const wchar_t *pszText, DWORD *adwColor);
    wchar_t *GetText() const;

  protected:
    void ReleaseText();
    void ApplyVisualState(int frameIndex);
    void ApplyTextColorForState(int colorIndex);
    bool HasCheckVisuals() const;
    int ResolveColorIndex(int requestedIndex) const;
};

class CGaugeBar
{
  protected:
    struct Rect
    {
        int left{0};
        int top{0};
        int right{0};
        int bottom{0};
    };

    struct GaugeSize
    {
        int width{0};
        int height{0};
    };

    SessionKeeper &sessionKeeper_;
    int &MouseX;
    int &MouseY;
    float &screenRateX_;
    float &screenRateY_;
    CSprite m_sprGauge;
    Rect m_gaugeRect;
    std::unique_ptr<CSprite> m_backgroundSprite;
    std::optional<GaugeSize> m_responseSize;
    int m_nXPos{0};
    int m_nYPos{0};

  public:
    explicit CGaugeBar(SessionKeeper &keeper);
    virtual ~CGaugeBar();

    void Create(int nGaugeWidth, int nGaugeHeight, int nGaugeTexID, const RECT *prcGauge = nullptr,
                int nBackWidth = 0, int nBackHeight = 0, int nBackTexID = -1,
                bool bShortenLeft = true, float fScaleX = 1.0f, float fScaleY = 1.0f);

    void Release();
    void SetPosition(int nXCoord, int nYCoord);
    int GetXPos() const
    {
        return m_nXPos;
    }
    int GetYPos() const
    {
        return m_nYPos;
    }
    int GetWidth() const;
    int GetHeight() const;
    void SetValue(std::uint32_t dwNow, std::uint32_t dwTotal);
    bool CursorInObject();
    void SetAlpha(std::uint8_t dwAlpha);
    void SetColor(std::uint8_t byRed, std::uint8_t byGreen, std::uint8_t byBlue);
    void Show(bool bShow = true);
    bool IsShow() const
    {
        return m_sprGauge.IsShow();
    };
    void Render();

  private:
    static Rect ConvertRect(const RECT &source);
    static Rect ScaleRect(const Rect &rect, float scaleX, float scaleY);
    static Rect OffsetRect(const Rect &rect, int offsetX, int offsetY);
    static bool Contains(const Rect &rect, long x, long y);
};

class CGaugeBar;
class CSlider
{
  protected:
    SessionKeeper &sessionKeeper_;
    int &MouseX;
    int &MouseY;
    float &screenRateX_;
    float &screenRateY_;
    bool &MouseLButton;
    bool &MouseLButtonPop;
    bool m_bVertical;
    BYTE m_byState;

    POINT m_ptPos;
    SIZE m_Size;
    int m_nSlideRange;
    int m_nSlidePos;
    int m_nThumbRange;
    CButton m_btnThumb;
    CGaugeBar *m_pGaugeBar;
    CSprite *m_psprBack;

    double m_dThumbMoveStartTime;
    double m_dThumbMoveTime;
    int m_nCapturePos;
    int m_nLimitPos;

  public:
    explicit CSlider(SessionKeeper &keeper);
    virtual ~CSlider();

    void Create(SImgInfo *piiThumb, SImgInfo *piiBack, SImgInfo *piiGauge = NULL,
                RECT *prcGauge = NULL, bool bVertical = false);
    void Release();
    void SetPosition(int nXCoord, int nYCoord);
    int GetXPos()
    {
        return m_ptPos.x;
    }
    int GetYPos()
    {
        return m_ptPos.y;
    }
    int GetWidth()
    {
        return m_Size.cx;
    }
    int GetHeight()
    {
        return m_Size.cy;
    }
    void SetSlideRange(int nSlideRange);
    void Update(double dDeltaTick);
    void Render();
    void SetEnable(bool bEnable);
    bool IsEnable()
    {
        return m_btnThumb.IsEnable();
    }
    void Show(bool bShow);
    bool IsShow()
    {
        return m_btnThumb.IsShow();
    }
    BOOL CursorInObject();
    void SetSlidePos(int nSlidePos);
    int GetSlidePos()
    {
        return m_nSlidePos;
    }
    BYTE GetState()
    {
        return m_byState;
    }

  protected:
    void SetThumbPosition();
    void LineUp();
    void LineDown();
};

class CSprite;
class CButton;
class CUIMng;
class SessionUiUnit;
class CErrorReport;
class CmuConsoleDebug;
namespace UI::Modern
{
class RmlMuButton;
}

class CWin : protected SessionUiLegacyBindings
{
    friend class CUIMng;
    friend class UI::Modern::RmlMuButton;

  protected:
    SessionKeeper &sessionKeeper_;
    CUIMng &legacyUiManager_;
    POINT m_ptHeld;

    bool m_bShow;
    bool m_bActive;
    bool m_bDocking;
    int m_nState;

    CSprite *m_psprBg;
    std::unique_ptr<CSprite> m_ownedBg;
    POINT m_ptPos;
    SIZE m_Size;
    POINT m_ptTemp;

    CPList m_BtnList;

  public:
    explicit CWin(SessionKeeper &keeper);
    virtual ~CWin();

    void Create(int nWidth, int nHeight, int nTexID = -1, bool bTile = false);
    virtual void Release();
    virtual void SetPosition(int nXCoord, int nYCoord);
    int GetXPos()
    {
        return m_ptPos.x;
    }
    int GetYPos()
    {
        return m_ptPos.y;
    }
    void SetSize(int nWidth, int nHeight, CHANGE_PRAM eChangedPram = XY);
    int GetWidth()
    {
        return m_Size.cx;
    }
    int GetHeight()
    {
        return m_Size.cy;
    }
    int GetTempXPos()
    {
        return m_ptTemp.x;
    }
    int GetTempYPos()
    {
        return m_ptTemp.y;
    }
    virtual bool CursorInWin(int nArea);
    virtual void Show(bool bShow);
    bool IsShow()
    {
        return m_bShow;
    }
    virtual void Active(bool bActive)
    {
        m_bActive = bActive;
    }
    bool IsActive()
    {
        return m_bActive;
    }
    void SetDocking(bool bDocking)
    {
        m_bDocking = bDocking;
    }
    int GetState()
    {
        return m_nState;
    }
    void Update(double dDeltaTick);
    virtual void Render();

    virtual int SetLine(int nLine)
    {
        return 0;
    }
    void ActiveBtns(bool bActive);

    BYTE GetBgAlpha()
    {
        return m_psprBg->GetAlpha();
    }
    void SetBgAlpha(BYTE byAlpha)
    {
        m_psprBg->SetAlpha(byAlpha);
    }
    void SetBgColor(BYTE byRed, BYTE byGreen, BYTE byBlue)
    {
        m_psprBg->SetColor(byRed, byGreen, byBlue);
    }

  protected:
    CUIMng &LegacyUiManager() noexcept
    {
        return legacyUiManager_;
    }
    virtual void PreRelease() {};
    virtual bool CursorInButtonlike()
    {
        return false;
    }
    virtual void UpdateWhileShow(double dDeltaTick) {};
    virtual void CheckAdditionalState() {};
    virtual void UpdateWhileActive(double dDeltaTick) {};
    virtual void RenderControls() {};

    void RegisterButton(CButton *pBtn);
    void RenderButtons();
};

#define WINEX_BGSIDE_HEIGHT 8

#define WE_BG_CENTER 0
#define WE_BG_TOP 1
#define WE_BG_BOTTOM 2
#define WE_BG_LEFT 3
#define WE_BG_RIGHT 4
#define WE_BG_MAX 5

class CWinEx : public CWin
{
  protected:
    int m_nBgSideMax;
    int m_nBgSideMin;
    int m_nBgSideNow;

    int m_nBasisY;

  public:
    explicit CWinEx(SessionKeeper &keeper);
    virtual ~CWinEx();

    void Create(SImgInfo *aImgInfo, int nBgSideMin, int nBgSideMax);
    void Release();
    void SetPosition(int nXCoord, int nYCoord);
    int SetLine(int nLine);
    void SetSize(int nHeight);
    bool CursorInWin(int nArea);
    void Show(bool bShow);
    void Render();

  protected:
    SessionBoundArray<CSprite, WE_BG_MAX> m_backgroundSprites;
    void CheckAdditionalState();
};

namespace Rml
{
class ElementFormControlInput;
class Element;
} // namespace Rml

namespace UI::Modern
{
// Existing Options volume-bar fill, shared with percentage bars.
void ApplySegmentedFill(std::span<Rml::ElementFormControlInput *const> cells, int filledCells);
// GFx textAutoSize="shrink": preserve the authored box and only reduce the font.
// Call again when a hidden label becomes visible.
void SetFittedLabelText(Rml::Element &label, const std::string &text);
} // namespace UI::Modern

class SessionKeeper;

namespace Rml
{
class Element;
}

namespace UI::Modern
{
void ApplyRmlMuButtonVisualState(Rml::Element &element, ButtonVisualState state);

class RmlMuButton final
{
  public:
    RmlMuButton();
    explicit RmlMuButton(SessionKeeper &);
    ~RmlMuButton();

    void Bind(Rml::Element &element);
    void Unbind() noexcept;
    void Reset() noexcept;
    void SetEnable(bool enable);
    void SetVisible(bool visible);
    bool IsClick() const;
    bool OwnsPointer(const Rml::Element *pointerTarget) const noexcept;
    bool SyncVisualState();

  private:
    class Listener;

    void OnClick() noexcept;

    std::unique_ptr<Listener> listener_;
    Rml::Element *element_ = nullptr;
    std::atomic<bool> enabled_ = true;
    std::atomic<bool> visible_ = true;
    mutable std::atomic<bool> clicked_ = false;
    std::optional<bool> syncedEnabled_;
    std::optional<bool> syncedVisible_;
};
} // namespace UI::Modern

namespace UI::Modern
{
class RmlMuButtonGroup final
{
  public:
    void Bind(std::span<Rml::Element *const> elements);
    void Unbind();
    bool Apply(int selected, std::span<const bool> enabled);
    std::optional<int> TakeSelection() const;

  private:
    struct Entry
    {
        Rml::Element *element;
        RmlMuButton button;
    };
    std::deque<Entry> buttons_;
    int selected_ = -1;
};
} // namespace UI::Modern

namespace Rml
{
class Element;
class ElementDocument;
} // namespace Rml

namespace UI::Modern
{
struct RmlMuCheckBoxListRowState final
{
    std::array<const wchar_t *, 4> text{L"", L"", L"", L""};
    bool visible = false;
    bool disabled = false;
    bool over = false;
    bool down = false;
    bool checked = false;
};

class RmlMuCheckBoxListRow final
{
  public:
    bool Bind(Rml::ElementDocument &document, std::string_view id);
    void Apply(const RmlMuCheckBoxListRowState &state,
               const RmlMuCheckBoxListRowState *previous = nullptr);

    void Unbind() noexcept;
    static bool CanToggle(bool checked, bool disabled) noexcept;

  private:
    Rml::Element *row_ = nullptr;
    std::array<Rml::Element *, 4> text_{};
};
} // namespace UI::Modern

namespace Rml
{
class Element;
class Event;
} // namespace Rml

namespace UI::Modern
{
struct RmlMuPanelPosition final
{
    float left = 0.0F;
    float top = 0.0F;
};

class RmlMuMovablePanel final
{
  public:
    RmlMuMovablePanel();
    ~RmlMuMovablePanel();

    RmlMuMovablePanel(const RmlMuMovablePanel &) = delete;
    RmlMuMovablePanel &operator=(const RmlMuMovablePanel &) = delete;

    void Bind(Rml::Element &panel, Rml::Element &dragHandle);
    void Unbind() noexcept;
    void Configure(float viewportWidth, float viewportHeight, float panelWidth, float panelHeight,
                   float originLeft = 0.0F, float originTop = 0.0F);
    void SetPosition(float left, float top);
    RmlMuPanelPosition Position() const noexcept;
    bool IsDragging() const noexcept;
    void CancelDrag() noexcept;
    bool TakeDirty() noexcept;

  private:
    class Listener;

    void ProcessEvent(Rml::Event &event);
    bool ClampPosition() noexcept;
    void ApplyPosition();

    std::unique_ptr<Listener> listener_;
    Rml::Element *panel_ = nullptr;
    Rml::Element *dragHandle_ = nullptr;
    float viewportWidth_ = 0.0F;
    float viewportHeight_ = 0.0F;
    float panelWidth_ = 0.0F;
    float panelHeight_ = 0.0F;
    float originLeft_ = 0.0F;
    float originTop_ = 0.0F;
    float left_ = 0.0F;
    float top_ = 0.0F;
    float dragAnchorX_ = 0.0F;
    float dragAnchorY_ = 0.0F;
    bool dragging_ = false;
    bool dirty_ = false;
};
} // namespace UI::Modern

namespace Rml
{
class Element;
class Event;
} // namespace Rml
namespace UI::Modern
{
class RmlMuOptionStepper final
{
  public:
    RmlMuOptionStepper();
    ~RmlMuOptionStepper();
    void Bind(Rml::Element &root, Rml::Element &previous, Rml::Element &next);
    void Unbind();
    bool Apply(int index, int maximum, bool enabled);
    std::optional<int> TakeRequestedIndex();

  private:
    class Listener;
    void OnKey(Rml::Event &event);
    std::unique_ptr<Listener> listener_;
    Rml::Element *root_ = nullptr;
    RmlMuButton previous_, next_;
    int index_ = 0, maximum_ = 0;
    bool enabled_ = false;
    std::optional<int> requested_;
};
} // namespace UI::Modern

namespace Rml
{
class Element;
}
struct SessionInputEvent;
namespace UI::Modern
{
// Common.Controls.MUPalette; the native guild wire format stores 8x8 four-bit colors.
class RmlMuPalette final
{
  public:
    static constexpr std::size_t PixelCount = 64;
    using Pixels = std::array<std::uint8_t, PixelCount>;
    bool Bind(Rml::Element &root, bool editable);
    void Unbind();
    void SetPixels(const Pixels &pixels);
    void SetColor(std::uint8_t color);
    void CancelPaint();
    bool IsPainting() const;
    bool ProcessInput(const SessionInputEvent &event, Rml::Element *hovered);
    std::optional<Pixels> TakeChanges();

  private:
    void Paint(Rml::Element *hovered);
    Rml::Element *root_ = nullptr;
    std::vector<Rml::Element *> cells_;
    Pixels pixels_{};
    std::uint8_t color_ = 0;
    int heldButton_ = 0;
    bool editable_ = false, changed_ = false;
};
} // namespace UI::Modern

namespace Rml
{
class Element;
}

namespace UI::Modern
{
class RmlMuProgressBar final
{
  public:
    enum class Axis
    {
        Horizontal,
        Vertical
    };
    void Bind(Rml::Element &fill, Axis axis = Axis::Horizontal) noexcept;
    void Unbind() noexcept;
    bool SetFrame(int frame);
    bool SetProgressFrames(double value, double maximum, int frames);
    bool SetProgress(double value, double maximum);

  private:
    Rml::Element *fill_ = nullptr;
    double ratio_ = -1.0;
    int frame_ = -1;
    Axis axis_ = Axis::Horizontal;
};
} // namespace UI::Modern

namespace Rml
{
class Element;
class ElementDocument;
class Event;
} // namespace Rml

namespace UI::Modern
{
struct RmlMuScrollBarState final
{
    std::size_t position = 0;
    std::size_t maximum = 0;
    std::size_t pageSize = 0;
    ButtonVisualState up = ButtonVisualState::Disabled;
    ButtonVisualState down = ButtonVisualState::Disabled;
    ButtonVisualState thumb = ButtonVisualState::Disabled;
    std::size_t trackStep = 0;

    bool operator==(const RmlMuScrollBarState &) const = default;
};

float DefaultRmlMuScrollBarMinimumThumbHeight() noexcept;

struct RmlMuScrollBarMetrics final
{
    float width;
    float height;
    float trackX;
    float trackY;
    float trackWidth;
    float trackHeight;
    float buttonWidth;
    float buttonHeight;
    float downY;
    float thumbX;
    float thumbY;
    float thumbWidth;
    float thumbHeight;
    float thumbTravel;
    float minimumThumbHeight = DefaultRmlMuScrollBarMinimumThumbHeight();
};

class RmlMuScrollBar final
{
  public:
    struct Rect final
    {
        float x;
        float y;
        float width;
        float height;
    };

    RmlMuScrollBar();
    ~RmlMuScrollBar();

    RmlMuScrollBar(const RmlMuScrollBar &) = delete;
    RmlMuScrollBar &operator=(const RmlMuScrollBar &) = delete;

    bool Bind(Rml::ElementDocument &document, std::string_view id);
    void Unbind() noexcept;
    // Reworked skins own all geometry in RCSS; only thumb progress is dynamic.
    void Apply(const RmlMuScrollBarState &state);
    void Apply(const RmlMuScrollBarState &state, float x, float y, float scale,
               const RmlMuScrollBarState *previous = nullptr);
    void Apply(const RmlMuScrollBarState &state, const RmlMuScrollBarMetrics &metrics, float x,
               float y, float scale, const RmlMuScrollBarState *previous = nullptr);

    static Rect UpButtonRect(float x, float y) noexcept;
    static Rect DownButtonRect(float x, float y) noexcept;
    static Rect ThumbRect(float x, float y, std::size_t position, std::size_t maximum) noexcept;
    static float ThumbOffset(std::size_t position, std::size_t maximum) noexcept;
    static float ThumbLength(const RmlMuScrollBarState &state,
                             const RmlMuScrollBarMetrics &metrics) noexcept;
    static float ThumbTravel(const RmlMuScrollBarState &state,
                             const RmlMuScrollBarMetrics &metrics) noexcept;
    static float ThumbOffset(const RmlMuScrollBarState &state,
                             const RmlMuScrollBarMetrics &metrics) noexcept;
    static std::size_t DragPosition(std::size_t startPosition, int mouseDelta, std::size_t maximum,
                                    float displayTravel) noexcept;
    std::optional<std::size_t> TakeRequestedPosition() noexcept;
    bool IsDragging() const noexcept;

  private:
    class Listener;

    void ProcessEvent(Rml::Event &event);
    void RequestPosition(std::size_t position) noexcept;

    std::unique_ptr<Listener> listener_;
    Rml::Element *root_ = nullptr;
    Rml::Element *track_ = nullptr;
    Rml::Element *down_ = nullptr;
    Rml::Element *up_ = nullptr;
    Rml::Element *thumb_ = nullptr;
    RmlMuScrollBarState state_{};
    RmlMuScrollBarMetrics metrics_{};
    float authoredThumbHeight_ = 0.0F;
    float authoredThumbY_ = 0.0F;
    bool authoredGeometry_ = false;
    std::optional<std::size_t> requestedPosition_;
    float scale_ = 1.0F;
    float dragStartMouseY_ = 0.0F;
    std::size_t dragStartPosition_ = 0;
    bool dragging_ = false;
};
} // namespace UI::Modern

namespace Rml
{
class Element;
class ElementDocument;
} // namespace Rml
struct SessionInputEvent;

namespace UI::Modern
{
// Common.Controls.MUScrollingList: source row template, selection and paging.
class RmlMuScrollingList final
{
  public:
    using Data = std::vector<std::vector<std::wstring>>;
    ~RmlMuScrollingList();
    bool Bind(Rml::ElementDocument &document, const char *listId, const char *scrollbarId,
              const char *templateId);
    void Unbind();
    void SetData(const Data &data);
    void SetPaletteData(const std::vector<RmlMuPalette::Pixels> &marks);
    void Select(std::optional<std::size_t> index);
    void ScrollToStart();
    bool Apply();
    void ProcessInput(const SessionInputEvent &event, const Rml::Element *hovered);
    std::optional<std::size_t> TakeSelection();
    bool IsDragging() const;
    std::optional<std::size_t> Hovered() const;

  private:
    struct Row
    {
        RmlMuButton button;
        RmlMuPalette palette;
        bool hasPalette = false;
        Rml::Element *element = nullptr;
        std::vector<Rml::Element *> cells;
    };
    void ResizeRows(std::size_t count);
    void ApplyRows();
    bool MoveTo(std::size_t position);
    Rml::Element *list_ = nullptr;
    Rml::Element *scrollbarElement_ = nullptr;
    Rml::Element *template_ = nullptr;
    RmlMuScrollBar scrollbar_;
    Data data_;
    std::vector<RmlMuPalette::Pixels> marks_;
    std::deque<Row> rows_;
    std::size_t first_ = 0, maximum_ = 0;
    std::optional<std::size_t> selected_, requested_, hovered_;
    float rowHeight_ = 0;
    float wheelRemainder_ = 0;
    bool dirty_ = true;
};
} // namespace UI::Modern

namespace UI::Modern
{
struct RmlSkillIconState final
{
    int sheet = -1;
    int icon = 0;
    static RmlSkillIconState FromSkill(int skillId, int useType, int icon, bool disabled = false);
    bool operator==(const RmlSkillIconState &) const = default;
};
} // namespace UI::Modern

namespace Rml
{
class Element;
}

namespace UI::Modern
{
// The source sheet stays whole. The slot clips the same cell as AS2 DrawImg.
class RmlSkillIcon final
{
  public:
    void Bind(Rml::Element &element, int columns);
    void Unbind();
    bool Set(const RmlSkillIconState &state);

  private:
    Rml::Element *element_ = nullptr;
    Rml::Element *sheet_ = nullptr;
    int columns_ = 0;
    std::optional<RmlSkillIconState> state_;
};
} // namespace UI::Modern
namespace UI::Modern
{
class RmlMuSkillSlot final
{
  public:
    ~RmlMuSkillSlot();
    void Bind(Rml::Element &root, Rml::Element &button, Rml::Element &icon, int columns);
    void Unbind();
    bool Apply(const RmlSkillIconState &icon, int selectedState, bool enabled);
    bool TakeClick() const;
    bool OwnsPointer(const Rml::Element *target) const;

  private:
    Rml::Element *root_ = nullptr;
    RmlMuButton button_;
    RmlSkillIcon icon_;
    int selected_ = -1;
};
} // namespace UI::Modern

class SessionKeeper;

namespace Rml
{
class Element;
class Event;
} // namespace Rml

namespace UI::Modern
{
class RmlMuSlot final
{
  public:
    RmlMuSlot();
    explicit RmlMuSlot(SessionKeeper &);
    ~RmlMuSlot();

    RmlMuSlot(const RmlMuSlot &) = delete;
    RmlMuSlot &operator=(const RmlMuSlot &) = delete;

    void Bind(Rml::Element &element);
    void Unbind() noexcept;
    void Reset() noexcept;
    void SetEnable(bool enable);
    bool IsClick() const;
    bool IsClear();
    bool SyncVisualState();
    void SetIconState(int frame);

  private:
    class Listener;

    void ProcessEvent(Rml::Event &event) noexcept;

    RmlMuButton button_;
    std::unique_ptr<Listener> listener_;
    Rml::Element *element_ = nullptr;
    std::atomic<bool> cleared_ = false;
    int iconFrame_ = 0;
};
} // namespace UI::Modern

namespace Rml
{
class Element;
class ElementDocument;
} // namespace Rml

namespace UI::Modern
{
// Native multiline layout with the source MUScrollBar attached to its scroll range.
class RmlMuTextArea final
{
  public:
    ~RmlMuTextArea();
    bool Bind(Rml::ElementDocument &document, const char *id, const char *scrollbarId = nullptr);
    void Unbind();
    void SetMarkup(const std::string &markup, bool resetScroll = true);
    void ScrollPage(int direction);
    bool AtEnd() const;
    bool AtStart() const;
    bool Apply();
    bool IsDragging() const;

  private:
    Rml::Element *element_ = nullptr;
    RmlMuScrollBar scrollbar_;
    std::optional<RmlMuScrollBarState> state_;
    std::string markup_;
    bool hasScrollbar_ = false;
};
} // namespace UI::Modern

class LegacyRenderFacade;
class SessionKeeper;
namespace Rml
{
class Element;
}

namespace UI::Modern
{
enum class RmlTooltipColor
{
    White,
    Blue,
    Gray,
    Red,
    Yellow,
    Green,
    Purple,
    RedPurple,
    Violet,
    Orange,
};

enum class RmlTooltipBackground
{
    None,
    DarkRed,
    DarkBlue,
    DarkYellow,
    GreenBlue,
};

enum class RmlTooltipAlignment
{
    Left,
    Center,
    Right,
};

struct RmlTooltipLine final
{
    static constexpr int TextCapacity = 100;

    wchar_t text[TextCapacity]{};
    RmlTooltipColor color = RmlTooltipColor::White;
    RmlTooltipBackground background = RmlTooltipBackground::None;
    bool bold = false;
};

struct RmlTooltipRequest final
{
    static constexpr int LineCapacity = 50;

    std::array<RmlTooltipLine, LineCapacity> lines{};
    int lineCount = 0;
    // Physical UI-canvas anchor, before per-session Grid presentation.
    int x = 0;
    int y = 0;
    int width = 0;
    RmlTooltipAlignment alignment = RmlTooltipAlignment::Center;
    bool framed = true;
    bool bottomAnchored = false;
};

struct RmlUiScaledViewport;
void PositionRmlTooltip(Rml::Element &panel, const RmlTooltipRequest &request,
                        const RmlUiScaledViewport &viewport);

class RmlTooltipLayer final
{
  public:
    explicit RmlTooltipLayer(SessionKeeper &keeper);
    ~RmlTooltipLayer();

    // Workers stage this frame's legacy row model after owner preparation.
    // The owner captures it on the next frame; the UI manager records the
    // retained snapshot after panels and deferred effects, without calling RmlUi.
    bool PrepareOnWorker(int viewportWidth, int viewportHeight);
    void Stage(const RmlTooltipRequest &request) noexcept;
    bool Record(LegacyRenderFacade &facade);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern

namespace SEASON3B
{
enum BUTTON_STATE
{
    BUTTON_STATE_UP = 0,
    BUTTON_STATE_DOWN,
    BUTTON_STATE_OVER,
};

enum RADIOGROUPEVENT
{
    RADIOGROUPEVENT_NONE = -1,
};

struct ButtonInfo
{
    int s_ImgIndex;
    int s_BTstate;
    unsigned int s_imgColor;
    ButtonInfo() : s_ImgIndex(0), s_BTstate(0), s_imgColor(0xffffffff)
    {
    }
};

typedef std::map<int, ButtonInfo> ButtonStateMap;

class CNewUIBaseButton;

class NewUIBaseButtonLegacyCalls : protected SessionUiLegacyBindings
{
  protected:
    NewUIBaseButtonLegacyCalls(SessionKeeper &keeper, CNewUIBaseButton &owner) noexcept;

    void RenderText(const wchar_t *text, int x, int y, int sx, int sy, LegacyFontRole role,
                    DWORD color, DWORD backcolor, int sort); // OMF-01988

  private:
    CNewUIBaseButton &owner_;
};

class CNewUIBaseButton : protected NewUIBaseButtonLegacyCalls
{
    friend class NewUIBaseButtonLegacyCalls;

  public:
    explicit CNewUIBaseButton(SessionKeeper &keeper);
    virtual ~CNewUIBaseButton();

  public:
    void SetPos(const POINT &pos);
    void SetSize(const POINT &size);
    void SetPos(int x, int y);
    void SetSize(int sx, int sy);

  public:
    const POINT &GetPos();
    const POINT &GetSize();
    const BUTTON_STATE GetBTState();

  public:
    void Lock();
    void UnLock();
    bool IsLock();

  public:
    bool RadioProcess();
    bool Process();

  protected:
    void RenderText(const wchar_t *text, int x, int y, int sx, int sy, LegacyFontRole role,
                    DWORD color, DWORD backcolor, int sort);

    POINT m_Pos;
    POINT m_Size;
    BUTTON_STATE m_EventState;
    bool m_Lock;
};

inline void CNewUIBaseButton::SetPos(const POINT &pos)
{
    m_Pos = pos;
}

inline void CNewUIBaseButton::SetSize(const POINT &size)
{
    m_Size = size;
}

inline const POINT &CNewUIBaseButton::GetPos()
{
    return m_Pos;
}

inline const POINT &CNewUIBaseButton::GetSize()
{
    return m_Size;
}

inline const BUTTON_STATE CNewUIBaseButton::GetBTState()
{
    return m_EventState;
}

#ifndef KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE // #ifndef
inline void CNewUIBaseButton::Lock()
{
    m_Lock = true;
}

inline void CNewUIBaseButton::UnLock()
{
    m_Lock = false;
}
#endif // KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE

inline bool CNewUIBaseButton::IsLock()
{
    return m_Lock;
}

class CNewUIButton : public CNewUIBaseButton
{
  public:
    explicit CNewUIButton(SessionKeeper &keeper);
    virtual ~CNewUIButton();
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    void ChangeButtonImgState(bool imgregister, int imgindex, bool overflg = false,
                              bool isimgwidth = false, bool bClickEffect = false);

#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
    void ChangeButtonImgState(bool imgregister, int imgindex, bool overflg = false,
                              bool isimgwidth = false);
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
    void ChangeButtonInfo(int x, int y, int sx, int sy);

  private:
    void Initialize();
    void Destroy();

  public:
    void RegisterButtonState(BUTTON_STATE eventstate, int imgindex, int btstate);
    void UnRegisterButtonState();

  public:
    void ChangeImgColor(BUTTON_STATE eventstate, unsigned int color);
    void ChangeText(std::wstring btname);
    // Slot overload: stores a pointer to an I18N::<Group>::<Identifier>
    // variable so the cached label is refreshed automatically when the
    // locale changes. Pass &I18N::Game::SomeKey at the call site.
    void ChangeText(const wchar_t *const *nameSlot);
    void SetFont(LegacyFontRole role);

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    void ChangeButtonState(BUTTON_STATE eventstate, int iButtonState);
    void MoveTextPos(int iX, int iY);
    void MoveTextTipPos(int iX, int iY);
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

    void ChangeTextBackColor(const DWORD bcolor);
    void ChangeTextColor(const DWORD color);

    void ChangeToolTipText(std::wstring tooltiptext, bool istoppos = false);
    // Slot overload — see ChangeText(const wchar_t* const*).
    void ChangeToolTipText(const wchar_t *const *tooltipSlot, bool istoppos = false);
    void ChangeToolTipTextColor(const DWORD color);
    void SetToolTipFont(LegacyFontRole role);

    void ChangeImgWidth(bool isimgwidth);
    void ChangeImgIndex(int imgindex, int curimgstate = 0);
    void ChangeAlpha(unsigned char fAlpha, bool isfontalph = true);
    void ChangeAlpha(float fAlpha, bool isfontalph = true);

  public:
    bool UpdateMouseEvent();

  public:
    bool Render(bool RendOption = false);

  private:
    void ChangeFrame();

  private:
    ButtonStateMap m_ButtonInfo;

  private:
    std::wstring m_Name;
    std::wstring m_TooltipText;
    // Optional I18N indirection: when non-null, the corresponding cached
    // string is refreshed from *m_p*Slot on every locale change. Set by
    // the slot-pointer overloads of ChangeText / ChangeToolTipText.
    const wchar_t *const *m_pNameSlot = nullptr;
    const wchar_t *const *m_pTooltipSlot = nullptr;
    // True once we have called I18N::RegisterLocaleObserver for this
    // instance, so EnsureLocaleObserver is idempotent and the destructor
    // knows whether an Unregister call is owed.
    bool m_LocaleObserverRegistered = false;

    LegacyFontRole m_textFontRole = LegacyFontRole::Normal;
    LegacyFontRole m_toolTipFontRole = LegacyFontRole::Normal;
    DWORD m_NameColor;
    DWORD m_NameBackColor;
    DWORD m_TooltipTextColor;

    int m_CurImgIndex;
    int m_CurImgState;

    WORD m_ImgWidth;
    WORD m_ImgHeight;

    unsigned int m_CurImgColor;
    bool m_IsTopPos;
    bool m_IsImgWidth;

    unsigned char m_fAlpha;

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    bool m_bClickEffect;
    int m_iMoveTextPosX;
    int m_iMoveTextPosY;
    int m_iMoveTextTipPosX;
    int m_iMoveTextTipPosY;
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

  private:
    void EnsureLocaleObserver();
    static void OnLocaleChanged(void *ctx) noexcept;
};

inline void CNewUIButton::ChangeImgWidth(bool isimgwidth)
{
    m_IsImgWidth = isimgwidth;
}

inline void CNewUIButton::ChangeText(std::wstring btname)
{
    // Caller is overriding any prior I18N slot binding with a literal
    // string; drop the slot so the locale observer won't clobber it.
    m_pNameSlot = nullptr;
    m_Name = btname;
}

inline void CNewUIButton::SetFont(LegacyFontRole role)
{
    m_textFontRole = role;
}

inline void CNewUIButton::ChangeTextBackColor(const DWORD bcolor)
{
    m_NameBackColor = bcolor;
}

inline void CNewUIButton::ChangeTextColor(const DWORD color)
{
    m_NameColor = color;
}

inline void CNewUIButton::ChangeToolTipText(std::wstring tooltiptext, bool istoppos)
{
    // See ChangeText(std::wstring): literal overrides drop the slot.
    m_pTooltipSlot = nullptr;
    m_TooltipText = tooltiptext;
    m_IsTopPos = istoppos;
    //m_hToolTipFont = LegacyFontRole::Normal;
}

inline void CNewUIButton::SetToolTipFont(LegacyFontRole role)
{
    m_toolTipFontRole = role;
}

inline void CNewUIButton::ChangeToolTipTextColor(const DWORD color)
{
    m_TooltipTextColor = color;
}

class CNewUIRadioButton : public CNewUIBaseButton
{
  public:
    explicit CNewUIRadioButton(SessionKeeper &keeper);
    virtual ~CNewUIRadioButton();

  public:
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    void ChangeRadioButtonImgState(int imgindex, bool isDown = false, bool bClickEffect = false);
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
    void ChangeRadioButtonImgState(int imgindex, bool isDown = false);
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
    void ChangeRadioButtonInfo(int x, int y, int sx, int sy);
    void ChangeFrame(BUTTON_STATE eventstate);

  public:
    void ChangeImgColor(BUTTON_STATE eventstate, unsigned int color);
    void ChangeText(std::wstring btname);
    // Slot overload — see CNewUIButton::ChangeText(const wchar_t* const*).
    void ChangeText(const wchar_t *const *nameSlot);
    void ChangeTextBackColor(const DWORD bcolor);
    void ChangeTextColor(const DWORD color);
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    void ChangeButtonState(BUTTON_STATE eventstate, int iButtonState);
    void ChangeButtonState(int iImgIndex, BUTTON_STATE eventstate, int iButtonState);
    void SetFont(LegacyFontRole role);
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

  public:
    void RegisterButtonState(BUTTON_STATE eventstate, int imgindex, int btstate);
    void UnRegisterButtonState();

  public:
    bool UpdateMouseEvent(bool isGroupevent = false);

  public:
    bool Render();

  private:
    void ChangeImgIndex(int imgindex, int curimgstate = 0);
    void ChangeFrame();
    void Initialize();
    void Destroy();

  private:
    ButtonStateMap m_RadioButtonInfo;
    std::wstring m_Name;
    // See CNewUIButton::m_pNameSlot — same purpose for radio buttons.
    const wchar_t *const *m_pNameSlot = nullptr;
    bool m_LocaleObserverRegistered = false;

    DWORD m_NameColor;
    DWORD m_NameBackColor;
    DWORD m_CurImgIndex;
    DWORD m_CurImgState;
    DWORD m_ImgWidth;
    DWORD m_ImgHeight;
    DWORD m_CurImgColor;
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    LegacyFontRole m_textFontRole = LegacyFontRole::Normal;
    bool m_bClickEffect;
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
#ifdef KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE
    bool m_bLockImage;
#endif // KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE

  private:
    void EnsureLocaleObserver();
    static void OnLocaleChanged(void *ctx) noexcept;
};

inline void CNewUIRadioButton::ChangeText(std::wstring btname)
{
    m_pNameSlot = nullptr;
    m_Name = btname;
}

inline void CNewUIRadioButton::ChangeTextBackColor(const DWORD bcolor)
{
    m_NameBackColor = bcolor;
}

inline void CNewUIRadioButton::ChangeTextColor(const DWORD color)
{
    m_NameColor = color;
}

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
inline void CNewUIRadioButton::SetFont(LegacyFontRole role)
{
    m_textFontRole = role;
}
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

class CNewUIRadioGroupButton : protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIRadioGroupButton(SessionKeeper &keeper);
    virtual ~CNewUIRadioGroupButton();

  public:
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    void CreateRadioGroup(int radiocount, int imgindex, bool bClickEffect = false);
    void ChangeRadioButtonInfo(bool iswidth, int x, int y, int sx, int sy, int iDistance = 1);
    void MoveTextPos(int iX, int iY);
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
    void CreateRadioGroup(int radiocount, int imgindex);
    void ChangeRadioButtonInfo(bool iswidth, int x, int y, int sx, int sy);
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
    void ChangeRadioText(std::list<std::wstring> &textlist);
    // Slot overload: list entries are pointers to I18N runtime variables
    // (e.g. &I18N::Game::Hunting). Labels refresh on language change.
    void ChangeRadioText(std::list<const wchar_t *const *> &slotList);
    void ChangeFrame(int buttonIndex);
    void LockButtonindex(int buttonIndex);
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    void SetFont(LegacyFontRole role, int iButtonIndex);
    void SetFont(LegacyFontRole role);
    void ChangeButtonState(BUTTON_STATE eventstate, int iButtonState);
    void ChangeButtonState(int iBtnIndex, int iImgIndex, BUTTON_STATE eventstate, int iButtonState);
    POINT GetPos(int iButtonIndex);
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
    void SetFontIndex(int buttonIndex, LegacyFontRole role);
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

  public:
    void RegisterRadioButton(CNewUIRadioButton *button);
    void UnRegisterRadioButton();

  public:
    const int GetCurButtonIndex();

  public:
    int UpdateMouseEvent();

  public:
    bool Render();

  private:
    void SetCurButtonIndex(int index);
    void Initialize();
    void Destroy();

  private:
    typedef std::list<CNewUIRadioButton *> RadioButtonList;

  private:
    RadioButtonList m_RadioList;
    DWORD m_CurButtonIndex;
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    int m_iButtonDistance; // ��ư�� ��ư������ ����
#endif                     // KJH_ADD_INGAMESHOP_UI_SYSTEM
};

inline void CNewUIRadioGroupButton::SetCurButtonIndex(int index)
{
    m_CurButtonIndex = index;
}

inline const int CNewUIRadioGroupButton::GetCurButtonIndex()
{
    return m_CurButtonIndex;
}

class CNewUICheckBox : protected SessionUiLegacyBindings
{
  public:
    explicit CNewUICheckBox(SessionKeeper &keeper);
    virtual ~CNewUICheckBox();
    void CheckBoxImgState(int imgindex);
    void RegisterBoxState(bool eventstate);
    void ChangeText(std::wstring btname);
    // Slot overload — see CNewUIButton::ChangeText(const wchar_t* const*).
    void ChangeText(const wchar_t *const *nameSlot);
    void CheckBoxInfo(int x, int y, int sx, int sy);
    bool GetBoxState();

    void Render();
    bool UpdateMouseEvent();

  private:
    int s_ImgIndex;
    POINT m_Pos;
    POINT m_Size;
    std::wstring m_Name;
    const wchar_t *const *m_pNameSlot = nullptr;
    bool m_LocaleObserverRegistered = false;
    LegacyFontRole m_textFontRole = LegacyFontRole::Normal;
    DWORD m_NameColor;
    DWORD m_NameBackColor;
    float m_ImgWidth;
    float m_ImgHeight;
    bool State;

  private:
    void EnsureLocaleObserver();
    static void OnLocaleChanged(void *ctx) noexcept;
};
}; // namespace SEASON3B

namespace SEASON3B
{
/**
     * @brief Minimal click-to-open dropdown combo box.
     *
     * Renders using the same raw GL / RenderText primitives as the rest of the
     * game UI (no ImGui dependency) so it works in release builds. Designed to
     * be self-contained so the upcoming UI rewrite can drop or replace it
     * without ripple changes elsewhere.
     *
     * Typical usage:
     *   - Call Setup() once after the owning window is placed.
     *   - Each frame, call UpdateMouseEvent() -- returns true if a new item
     *     was selected this frame. Then call Render().
     *   - Owner should also treat IsMouseOverWidget() as "click consumed" so
     *     the expanded dropdown (which can overflow the owner's hit box)
     *     doesn't leak clicks to the game world.
     *
     * The combo does NOT take ownership of the label array -- the caller
     * must keep it alive for the combo's lifetime.
     */
class CNewUIComboBox : protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIComboBox(SessionKeeper &keeper);
    ~CNewUIComboBox() = default;

    /**
         * @brief Configures the combo. Call once after the owning window is placed.
         *
         * @param x,y              Top-left of the closed combo field (UI coordinates)
         * @param width            Combo width
         * @param itemHeight       Row height (used for both the closed field and list items)
         * @param labels           Array of item labels; caller owns the memory
         * @param itemCount        Number of labels
         * @param initialIdx       Initial selected index (clamped to [0, itemCount))
         * @param maxVisibleItems  Max rows shown in the expanded dropdown at once. When
         *                         `itemCount` exceeds this, the list becomes scrollable
         *                         (mouse wheel + scrollbar indicator). Pass 0 (default)
         *                         to always show the full list.
         */
    void Setup(int x, int y, int width, int itemHeight, const wchar_t *const *labels, int itemCount,
               int initialIdx, int maxVisibleItems = 0);

    void SetPos(int x, int y)
    {
        m_X = x;
        m_Y = y;
    }
    void SetSelectedIndex(int idx);
    int GetSelectedIndex() const
    {
        return m_SelectedIndex;
    }

    bool IsOpen() const
    {
        return m_bOpen;
    }
    void Close()
    {
        m_bOpen = false;
    }

    /**
         * @brief True if the mouse is over the closed combo or (when open) over any
         * part of the expanded dropdown list. The owner should consume clicks in
         * this region so the overflowing dropdown doesn't leak to other UI or the
         * game world.
         */
    bool IsMouseOverWidget() const;

    /**
         * @brief Processes one frame of mouse input.
         * @return true if the user picked a new item this frame (different index).
         */
    bool UpdateMouseEvent();

    /**
         * @brief Draws the closed field always, and the dropdown list if open.
         * Call this last in the owner's render flow so the dropdown sits on top.
         */
    void Render();

  private:
    int m_X = 0;
    int m_Y = 0;
    int m_Width = 0;
    int m_ItemHeight = 0;

    const wchar_t *const *m_Labels = nullptr;
    int m_ItemCount = 0;
    int m_SelectedIndex = 0;
    bool m_bOpen = false;

    // Scrolling
    int m_MaxVisibleItems = 0; // 0 = show all, no scrollbar
    int m_ScrollOffset = 0;    // index of the first visible row

    // Geometry helpers -- all use UI-space coordinates.
    int GetVisibleCount() const; // Rows actually rendered in the open list
    int GetMaxScrollOffset() const;
    bool IsScrollable() const;
    int GetListY() const
    {
        return m_Y + m_ItemHeight;
    }
    int GetListHeight() const
    {
        return GetVisibleCount() * m_ItemHeight;
    }
    int GetItemIndexAtMouse() const; // -1 if no visible item hit
    void ClampScrollOffset();
    void ScrollToShowIndex(int idx); // Ensure `idx` is in the visible window
};
} // namespace SEASON3B

//	NewUIScrollBar.h

namespace SEASON3B
{
class CNewUIScrollBar : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum KEnumTypeIMAGE_LIST
    {
        IMAGE_SCROLL_TOP = BITMAP_INTERFACE_NEW_CHATLOGWND_BEGIN, // newui_scrollbar_up.tga (7,3)
        IMAGE_SCROLL_MIDDLE =
            BITMAP_INTERFACE_NEW_CHATLOGWND_BEGIN + 1, // newui_scrollbar_m.tga (7,15)
        IMAGE_SCROLL_BOTTOM =
            BITMAP_INTERFACE_NEW_CHATLOGWND_BEGIN + 2, // newui_scrollbar_down.tga (7,3)
        IMAGE_SCROLLBAR_ON =
            BITMAP_INTERFACE_NEW_CHATLOGWND_BEGIN + 3, // newui_scroll_On.tga (15,30)
        IMAGE_SCROLLBAR_OFF =
            BITMAP_INTERFACE_NEW_CHATLOGWND_BEGIN + 4, // newui_scroll_Off.tga (15,30)
    };

  private:
    enum KEnumTypeSCROLLBTN
    {
        SCROLLBTN_WIDTH = 15,
        SCROLLBTN_HEIGHT = 30,
        SCROLLBAR_TOP_WIDTH = 7,
        SCROLLBAR_TOP_HEIGHT = 3,
        SCROLLBAR_MIDDLE_WIDTH = 7,
        SCROLLBAR_MIDDLE_HEIGHT = 15
    };

    enum KEnumTypeSCROLLBAR_MOUSEBTN
    {
        SCROLLBAR_MOUSEBTN_INVALID = -1,
        SCROLLBAR_MOUSEBTN_FIRST,
        SCROLLBAR_MOUSEBTN_NORMAL = SCROLLBAR_MOUSEBTN_FIRST,
        SCROLLBAR_MOUSEBTN_OVER,
        SCROLLBAR_MOUSEBTN_CLICKED,
        SCROLLBAR_MOUSEBTN_TOTAL,
        SCROLLBAR_MOUSEBTN_LAST = SCROLLBAR_MOUSEBTN_TOTAL - 1,
    };

  protected:
    int m_iHeight;
    POINT m_ptPos;

    POINT m_ptScrollBtnStartPos;
    POINT m_ptScrollBtnPos;

    int m_iScrollBarPickGap;

    int m_iScrollBarMovePixel;
    int m_iScrollBarHeightPixel;
    int m_iScrollBarMiddleNum;
    int m_iScrollBarMiddleRemainderPixel;

    int m_iScrollBtnMouseEvent;
    bool m_bScrollBtnActive;

    float m_fPercentOfSize;

    int m_iBeginPos;
    int m_iCurPos;
    int m_iMaxPos;

  public:
    explicit CNewUIScrollBar(SessionKeeper &keeper);
    virtual ~CNewUIScrollBar();

    bool Create(int iX, int iY, int iHeight);
    void Release();

    float GetLayerDepth();

    void LoadImages();
    void UnloadImages();

    void SetPos(int x, int y);

    bool UpdateBtnEvent();
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();

    bool Update();
    bool Render();

    void UpdateScrolling();
    void ScrollUp(int iMoveValue);
    void ScrollDown(int iMoveValue);

    float GetPercent()
    {
        return m_fPercentOfSize;
    }
    void SetPercent(float fPercent);

    int GetMaxPos()
    {
        return m_iMaxPos;
    }
    void SetMaxPos(int iMaxPos);

    int GetCurPos()
    {
        return m_iCurPos;
    }
    void SetCurPos(int iMoveValue);
};
} // namespace SEASON3B

class SessionKeeper;
class SessionUiUnit;

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
#define UIMAX_TEXT_LINE 150
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

inline DWORD _ARGB(BYTE a, BYTE r, BYTE g, BYTE b)
{
    return (a << 24) + (b << 16) + (g << 8) + (r);
}

void CutText4(const wchar_t *pszSource, wchar_t *pszResult1, wchar_t *pszResult2, int iCutCount);

const int COORDINATE_TYPE_LEFT_TOP = 1;
const int COORDINATE_TYPE_LEFT_DOWN = 2;

#define ID_UICEDIT 0x0001

enum UISTATES
{
    UISTATE_NORMAL = 0,
    UISTATE_RESIZE,
    UISTATE_SCROLL,
    UISTATE_HIDE,
    UISTATE_MOVE,
    UISTATE_READY,
    UISTATE_DISABLE
};

enum UIOPTIONS
{
    UIOPTION_NULL = 0,
    UIOPTION_NUMBERONLY = 1,
    UIOPTION_SERIALNUMBER = 2,
    UIOPTION_ENTERIMECHKOFF = 4,
    UIOPTION_PAINTBACK = 8,
    UIOPTION_NOLOCALIZEDCHARACTERS = 16
};

typedef struct
{
    BOOL m_bIsSelected;
    wchar_t m_szID[MAX_USERNAME_SIZE + 1];
    wchar_t m_szText[MAX_TEXT_LENGTH + 1];
    int m_iType;
    int m_iColor;
    UINT m_uiEmptyLines;
} WHISPER_TEXT;

typedef struct
{
    BOOL m_bIsSelected;
    wchar_t m_szID[MAX_USERNAME_SIZE + 1];
    BYTE m_Number;
    BYTE m_Server;
    BYTE m_GuildStatus;
} GUILDLIST_TEXT;

typedef struct
{
    BOOL m_bIsSelected;
    wchar_t m_szPattern[MAX_ITEM_NAME + 1];
} FILTERLIST_TEXT;

typedef struct
{
    BOOL m_bIsSelected;
    DWORD m_dwUIID;
    wchar_t m_szTitle[64];
    int m_iStatus;
} WINDOWLIST_TEXT;

typedef struct
{
    BOOL m_bIsSelected;
    DWORD m_dwLetterID;
    wchar_t m_szID[MAX_USERNAME_SIZE + 1];
    wchar_t m_szText[MAX_TEXT_LENGTH + 1];
    wchar_t m_szDate[16];
    wchar_t m_szTime[16];
    BOOL m_bIsRead;
} LETTERLIST_TEXT;

typedef struct
{
    BOOL m_bIsSelected;
    wchar_t m_szText[MAX_LETTERTEXT_LENGTH + 1];
} LETTER_TEXT;

typedef struct
{
    BOOL m_bIsSelected;
    wchar_t m_szContent[60];
} GUILDLOG_TEXT;
typedef struct
{
    BOOL m_bIsSelected;
    BYTE GuildMark[64];
    wchar_t szName[GuildConstants::GUILD_NAME_BUFFER_SIZE];
    int nMemberCount;
} UNIONGUILD_TEXT;

typedef struct
{
    BOOL m_bIsSelected;
    wchar_t szName[GuildConstants::GUILD_NAME_BUFFER_SIZE];
    int nCount;
    BYTE byIsGiveUp;
    BYTE bySeqNum;
} BCDECLAREGUILD_TEXT;

typedef struct tagMOVECOMMAND_TEXT
{
    BOOL m_bIsSelected;
    BOOL m_bCanMove;
    wchar_t szMainMapName[32]; //. Main map name
    wchar_t szSubMapName[32];  //. Substitute map name
    int iReqLevel;             //. required level
    int iReqZen;               //. required zen
    int iGateNum;              //. Gate number
} MOVECOMMAND_TEXT;

typedef struct
{
    BOOL m_bIsSelected;
    wchar_t szName[GuildConstants::GUILD_NAME_BUFFER_SIZE];
    BYTE byJoinSide;
    BYTE byGuildInvolved;
    int iGuildScore;
} BCGUILD_TEXT;

typedef struct _UNMIX_TEXT
{
    BOOL m_bIsSelected;
    int m_iInvenIdx;
    char m_cLevel;

    _UNMIX_TEXT() : m_bIsSelected(false), m_iInvenIdx(-1), m_cLevel(-1)
    {
    }
} UNMIX_TEXT;

typedef struct _SOCKETLIST_TEXT
{
    BOOL m_bIsSelected;
    int m_iSocketIndex;
    wchar_t m_szText[64 + 1];
} SOCKETLIST_TEXT;

enum UI_MESSAGE_ENUM
{
    UI_MESSAGE_NULL = 0,
    UI_MESSAGE_SELECT,
    UI_MESSAGE_HIDE,
    UI_MESSAGE_MAXIMIZE,
    UI_MESSAGE_CLOSE,
    UI_MESSAGE_BOTTOM,
    UI_MESSAGE_SELECTED,
    UI_MESSAGE_TEXTINPUT,
    UI_MESSAGE_P_MOVE,
    UI_MESSAGE_P_RESIZE,
    UI_MESSAGE_BTNLCLICK,
    UI_MESSAGE_TXTRETURN,
    UI_MESSAGE_YNRETURN,
    UI_MESSAGE_LISTDBLCLICK,
    UI_MESSAGE_LISTSCRLTOP,
    UI_MESSAGE_LISTSELUP,
    UI_MESSAGE_LISTSELDOWN
};

struct UI_MESSAGE
{
    int m_iMessage;
    LONG_PTR m_iParam1;
    LONG_PTR m_iParam2;
    std::wstring m_Text;
};

class CUIMessage
{
  public:
    CUIMessage()
    {
    }
    virtual ~CUIMessage()
    {
        m_MessageList.clear();
    }

    void SendUIMessage(int iMessage, LONG_PTR iParam1, LONG_PTR iParam2, std::wstring text = {});
    void GetUIMessage();

  protected:
    std::deque<UI_MESSAGE> m_MessageList;
    UI_MESSAGE m_WorkMessage{};
};

class CUIControl : public CUIMessage, protected SessionUiLegacyBindings
{
  public:
    explicit CUIControl(SessionKeeper &keeper);
    virtual ~CUIControl()
    {
    }

    DWORD GetUIID()
    {
        return m_dwUIID;
    }
    void SetParentUIID(DWORD dwParentUIID)
    {
        m_dwParentUIID = dwParentUIID;
    }
    DWORD GetParentUIID()
    {
        return m_dwParentUIID;
    }
    virtual void SetState(int iState);
    int GetState();
    void SetOption(int iOption)
    {
        m_iOptions = iOption;
    }
    BOOL CheckOption(int iOption)
    {
        return m_iOptions & iOption;
    }

    void SendUIMessageDirect(int iMessage, int iParam1, int iParam2);

    void SetPosition(int iPos_x, int iPos_y);
    int GetPosition_x()
    {
        return m_iPos_x;
    }
    int GetPosition_y()
    {
        return m_iPos_y;
    }
    virtual void SetSize(int iWidth, int iHeight);
    int GetWidth()
    {
        return m_iWidth;
    }
    int GetHeight()
    {
        return m_iHeight;
    }
    virtual void SetArrangeType(int iArrangeType = 0, int iRelativePos_x = 0,
                                int iRelativePos_y = 0);
    virtual void SetResizeType(int iResizeType = 0, int iRelativeWidth = 0,
                               int iRelativeHeight = 0);
    virtual void Render()
    {
    }
    virtual BOOL DoAction(BOOL bMessageOnly = FALSE);

  protected:
    virtual void DoActionSub(BOOL bMessageOnly)
    {
    }
    virtual BOOL DoMouseAction()
    {
        return TRUE;
    }
    virtual void DefaultHandleMessage();
    virtual BOOL HandleMessage()
    {
        return FALSE;
    }

  protected:
    DWORD m_dwUIID;
    DWORD m_dwParentUIID;
    int m_iState;
    int m_iOptions;
    int m_iPos_x, m_iPos_y;
    int m_iWidth, m_iHeight;
    int m_iArrangeType;
    int m_iResizeType;
    int m_iRelativePos_x, m_iRelativePos_y;
    int m_iRelativeWidth, m_iRelativeHeight;
    int m_iCoordType;
};

class CUIButton : public CUIControl
{
  public:
    explicit CUIButton(SessionKeeper &keeper);
    virtual ~CUIButton();

    virtual void Init(DWORD dwButtonID, const wchar_t *pszCaption);
    virtual void SetCaption(const wchar_t *pszCaption);
    virtual BOOL DoMouseAction();
    virtual void Render();

  protected:
    DWORD m_dwButtonID;
    wchar_t *m_pszCaption;
    BOOL m_bMouseState;
};

enum UILISTBOX_SCROLL_TYPE
{
    UILISTBOX_SCROLL_DOWNUP = 0,
    UILISTBOX_SCROLL_UPDOWN
};

template <class T> class CUITextListBox : public CUIControl
{
  public:
    explicit CUITextListBox(SessionKeeper &keeper);
    virtual ~CUITextListBox();

    virtual void Clear();
    virtual void AddText()
    {
    }

    virtual void Render();
    virtual void Scrolling(int iValue);

    virtual int GetBoxSize()
    {
        return m_iNumRenderLine;
    }
    virtual void SetBoxSize(int iLineNum)
    {
        m_iNumRenderLine = iLineNum;
    }
    virtual void SetNumRenderLine(int iLine)
    {
        m_iNumRenderLine = iLine;
    }
    virtual void Resize(int iValue);
    virtual BOOL HandleMessage();

    virtual void ResetCheckedLine(BOOL bFlag = FALSE);
    virtual BOOL HaveCheckedLine();
    virtual int GetCheckedLines(std::deque<T *> *pSelectLineList);
    virtual int GetLineNum()
    {
        return (m_bUseMultiline == TRUE ? m_RenderTextList.size() : m_TextList.size());
    }
    const std::deque<T> &Items() const noexcept
    {
        return m_TextList;
    }

    virtual void SLSetSelectLine(int iLineNum);
    virtual void SLSelectPrevLine(int iLineNum = 1);
    virtual void SLSelectNextLine(int iLineNum = 1);
    virtual typename std::deque<T>::iterator SLGetSelectLine();

    virtual int SLGetSelectLineNum() const
    {
        return m_iSelectLineNum;
    }

  protected:
    virtual BOOL DoMouseAction();
    virtual void RemoveText();
    virtual void ComputeScrollBar();
    virtual void MoveRenderLine();
    virtual BOOL CheckMouseInBox();

    virtual void RenderInterface() = 0;
    virtual BOOL RenderDataLine(int iLineNumber) = 0;
    virtual void RenderCoveredInterface()
    {
    }
    virtual BOOL DoLineMouseAction(int iLineNumber) = 0;
    virtual BOOL DoSubMouseAction()
    {
        return TRUE;
    }

    virtual void CalcLineNum()
    {
    }

  protected:
    std::deque<T> m_TextList;
    typename std::deque<T>::iterator m_TextListIter;

    BOOL m_bUseSelectLine;
    float m_bPressCursorKey;
    int m_iSelectLineNum;

    BOOL m_bUseMultiline;
    std::deque<T> m_RenderTextList;

    int m_iMaxLineCount;
    int m_iCurrentRenderEndLine;
    int m_iNumRenderLine;

    float m_fScrollBarRange_top;
    float m_fScrollBarRange_bottom;

    float m_fScrollBarPos_y;
    float m_fScrollBarWidth;
    float m_fScrollBarHeight;

    float m_fScrollBarClickPos_y;
    float m_bScrollBtnClick;
    float m_bScrollBarClick;

    int m_iScrollType;
    BOOL m_bNewTypeScrollBar;

    BOOL m_bUseNewUIScrollBar;
};

class CUIGuildListBox : public CUITextListBox<GUILDLIST_TEXT>
{
  public:
    explicit CUIGuildListBox(SessionKeeper &keeper);
    virtual ~CUIGuildListBox()
    {
    }

    virtual void AddText(const wchar_t *pszID, BYTE Number, BYTE Server);

  protected:
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int iLineNumber);
    virtual BOOL DoLineMouseAction(int iLineNumber)
    {
        return TRUE;
    }
    virtual BOOL DoSubMouseAction();

  protected:
    BOOL m_bIsGuildMaster;
};

class CUISimpleChatListBox : public CUITextListBox<WHISPER_TEXT>
{
  public:
    explicit CUISimpleChatListBox(SessionKeeper &keeper);
    virtual ~CUISimpleChatListBox()
    {
    }

    virtual void Render();
    virtual void AddText(const wchar_t *pszID, const wchar_t *pszText, int iType, int iColor);

  protected:
    virtual void AddTextToRenderList(const wchar_t *pszID, const wchar_t *pszText, int iType,
                                     int iColor);
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int iLineNumber);
    virtual BOOL DoLineMouseAction(int iLineNumber)
    {
        return TRUE;
    }
    void CalcLineNum();
};

class CUILetterTextListBox : public CUITextListBox<LETTER_TEXT>
{
  public:
    explicit CUILetterTextListBox(SessionKeeper &keeper);
    virtual ~CUILetterTextListBox()
    {
    }

    virtual void Render();
    virtual void AddText(const wchar_t *pszText);

  protected:
    virtual void AddTextToRenderList(const wchar_t *pszText);
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int iLineNumber);
    virtual BOOL DoLineMouseAction(int iLineNumber)
    {
        return TRUE;
    }
    void CalcLineNum();
    virtual int GetRenderLinePos_y(int iLineNumber);
};

class CUIChatPalListBox : public CUITextListBox<GUILDLIST_TEXT>
{
  public:
    explicit CUIChatPalListBox(SessionKeeper &keeper);
    virtual ~CUIChatPalListBox()
    {
    }

    virtual void AddText(const wchar_t *pszID, BYTE Number, BYTE Server);
    virtual void DeleteText(const wchar_t *pszID);
    virtual void SetNumRenderLine(int iLine);
    GUILDLIST_TEXT *GetSelectedText()
    {
        return (SLGetSelectLine() == m_TextList.end() ? NULL : &(*SLGetSelectLine()));
    }
    std::deque<GUILDLIST_TEXT> &GetFriendList()
    {
        m_bForceEditList = TRUE;
        return m_TextList;
    }
    void SetLayout(int iType)
    {
        m_iLayoutType = iType;
    }
    const wchar_t *GetNameByNumber(BYTE byNumber);
    void SetColumnWidth(UINT uiColumnNum, int iWidth)
    {
        if (uiColumnNum < 4)
            m_iColumnWidth[uiColumnNum] = iWidth;
    }
    int GetColumnWidth(UINT uiColumnNum)
    {
        return (uiColumnNum < 4 ? m_iColumnWidth[uiColumnNum] : 0);
    }
    int GetColumnPos_x(UINT uiColumnNum)
    {
        int iResult = 0;

        for (unsigned int i = 0; i < uiColumnNum; ++i)
        {
            iResult += m_iColumnWidth[i];
        }
        return iResult;
    }
    void MakeTitleText(wchar_t *pszTitleText);

  protected:
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int iLineNumber);
    virtual BOOL DoLineMouseAction(int iLineNumber);
    virtual int GetRenderLinePos_y(int iLineNumber);

  protected:
    int m_iLayoutType;
    int m_iColumnWidth[4];
    BOOL m_bForceEditList;
};

class CUIWindowListBox : public CUITextListBox<WINDOWLIST_TEXT>
{
  public:
    explicit CUIWindowListBox(SessionKeeper &keeper);
    virtual ~CUIWindowListBox()
    {
    }

    virtual void AddText(DWORD dwUIID, const wchar_t *pszTitle, int iStatus = 0);
    virtual void DeleteText(DWORD dwUIID);
    virtual void SetNumRenderLine(int iLine);
    WINDOWLIST_TEXT *GetSelectedText()
    {
        return (SLGetSelectLine() == m_TextList.end() ? NULL : &(*SLGetSelectLine()));
    }

  protected:
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int iLineNumber);
    virtual BOOL DoLineMouseAction(int iLineNumber);
    virtual int GetRenderLinePos_y(int iLineNumber);
};

class CUILetterListBox : public CUITextListBox<LETTERLIST_TEXT>
{
  public:
    explicit CUILetterListBox(SessionKeeper &keeper);
    virtual ~CUILetterListBox()
    {
    }

    virtual void AddText(const wchar_t *pszID, const wchar_t *pszText, const wchar_t *pszDate,
                         const wchar_t *pszTime, BOOL bIsRead);
    virtual void DeleteText(DWORD dwLetterID);
    virtual void SetNumRenderLine(int iLine);
    LETTERLIST_TEXT *GetSelectedText()
    {
        return (SLGetSelectLine() == m_TextList.end() ? NULL : &(*SLGetSelectLine()));
    }
    std::deque<LETTERLIST_TEXT> &GetLetterList()
    {
        m_bForceEditList = TRUE;
        return m_TextList;
    }

    void SetColumnWidth(UINT uiColumnNum, int iWidth)
    {
        if (uiColumnNum < 4)
            m_iColumnWidth[uiColumnNum] = iWidth;
    }
    int GetColumnWidth(UINT uiColumnNum)
    {
        return (uiColumnNum < 4 ? m_iColumnWidth[uiColumnNum] : 0);
    }
    int GetColumnPos_x(UINT uiColumnNum)
    {
        int iResult = 0;
        for (unsigned int i = 0; i < uiColumnNum; ++i)
        {
            iResult += m_iColumnWidth[i];
        }
        return iResult;
    }

  protected:
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int iLineNumber);
    virtual BOOL DoLineMouseAction(int iLineNumber);
    virtual int GetRenderLinePos_y(int iLineNumber);

  protected:
    int m_iColumnWidth[4];
    BOOL m_bForceEditList;
};

class CUISocketListBox : public CUITextListBox<SOCKETLIST_TEXT>
{
  public:
    explicit CUISocketListBox(SessionKeeper &keeper);
    virtual ~CUISocketListBox()
    {
    }

    virtual void AddText(int iSocketIndex, const wchar_t *pszText);
    virtual void DeleteText(int iSocketIndex);
    virtual void SetNumRenderLine(int iLine);
    SOCKETLIST_TEXT *GetSelectedText()
    {
        return (SLGetSelectLine() == m_TextList.end() ? NULL : &(*SLGetSelectLine()));
    }

  protected:
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int iLineNumber);
    virtual BOOL DoLineMouseAction(int iLineNumber);
    virtual int GetRenderLinePos_y(int iLineNumber);
};

class CUIGuildNoticeListBox : public CUITextListBox<GUILDLOG_TEXT>
{
  public:
    explicit CUIGuildNoticeListBox(SessionKeeper &keeper);
    virtual ~CUIGuildNoticeListBox()
    {
    }

    virtual void AddText(const wchar_t *szContent);
    virtual void DeleteText(DWORD dwIndex);
    virtual void SetNumRenderLine(int nLine);
    GUILDLOG_TEXT *GetSelectedText()
    {
        return (SLGetSelectLine() == m_TextList.end() ? NULL : &(*SLGetSelectLine()));
    }

  protected:
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int iLineNumber);
    virtual BOOL DoLineMouseAction(int iLineNumber);
    virtual int GetRenderLinePos_y(int iLineNumber);
};

class CUINewGuildMemberListBox : public CUITextListBox<GUILDLIST_TEXT>
{
  public:
    explicit CUINewGuildMemberListBox(SessionKeeper &keeper);
    virtual ~CUINewGuildMemberListBox()
    {
    }

    virtual void AddText(const wchar_t *pszID, BYTE Number, BYTE Server, BYTE GuildStatus);
    virtual void DeleteText(DWORD dwUIID);
    virtual void SetNumRenderLine(int iLine);
    GUILDLIST_TEXT *GetSelectedText()
    {
        return (SLGetSelectLine() == m_TextList.end() ? NULL : &(*SLGetSelectLine()));
    }

  protected:
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int iLineNumber);
    virtual BOOL DoLineMouseAction(int iLineNumber);
    virtual int GetRenderLinePos_y(int iLineNumber);

  protected:
    BOOL m_bIsGuildMaster;
};

class CUIUnionGuildListBox : public CUITextListBox<UNIONGUILD_TEXT>

{
  public:
    explicit CUIUnionGuildListBox(SessionKeeper &keeper);
    virtual ~CUIUnionGuildListBox()
    {
    }

    virtual void AddText(BYTE *pGuildMark, const wchar_t *szGuildName, int nMemberCount);
    virtual void DeleteText(DWORD dwGuildIndex);
    virtual int GetTextCount();
    virtual void SetNumRenderLine(int iLine);
    UNIONGUILD_TEXT *GetSelectedText()
    {
        return (SLGetSelectLine() == m_TextList.end() ? NULL : &(*SLGetSelectLine()));
    }

  protected:
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int iLineNumber);
    virtual BOOL DoLineMouseAction(int iLineNumber);
    virtual int GetRenderLinePos_y(int iLineNumber);
};

class CUIExtraItemListBox : public CUITextListBox<FILTERLIST_TEXT>
{
  public:
    explicit CUIExtraItemListBox(SessionKeeper &keeper);
    ~CUIExtraItemListBox() = default;

    virtual void AddText(const wchar_t *pszPattern);
    virtual void DeleteText(const wchar_t *pszPattern);
    virtual void SetNumRenderLine(int iLine);
    FILTERLIST_TEXT *GetSelectedText()
    {
        return (SLGetSelectLine() == m_TextList.end() ? NULL : &(*SLGetSelectLine()));
    }

  protected:
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int iLineNumber);
    virtual BOOL DoLineMouseAction(int iLineNumber);
    virtual int GetRenderLinePos_y(int iLineNumber);
};

class CUIUnmixgemList : public CUITextListBox<UNMIX_TEXT>
{
  public:
    explicit CUIUnmixgemList(SessionKeeper &keeper);
    virtual ~CUIUnmixgemList()
    {
    }
    virtual void SetNumRenderLine(int iLine);
    UNMIX_TEXT *GetSelectedText()
    {
        return (SLGetSelectLine() == m_TextList.end() ? NULL : &(*SLGetSelectLine()));
    }
    virtual void AddText(int iIndex, BYTE cComType);
    void Sort();

    inline bool IsNotified()
    {
        return m_bNotify;
    }
    inline bool IsEmpty()
    {
        return m_TextList.empty();
    }

  protected:
    SessionUiUnit &sessionUi_;
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int iLineNumber);
    virtual BOOL DoLineMouseAction(int iLineNumber);
    virtual int GetRenderLinePos_y(int iLineNumber);

    bool m_bNotify;
};

class CUIBCDeclareGuildListBox : public CUITextListBox<BCDECLAREGUILD_TEXT>

{
  public:
    explicit CUIBCDeclareGuildListBox(SessionKeeper &keeper);
    virtual ~CUIBCDeclareGuildListBox()
    {
    }

    virtual void AddText(const wchar_t *szGuildName, int nMarkCount, BYTE byIsGiveUp,
                         BYTE bySeqNum);
    virtual void DeleteText(DWORD dwGuildIndex);
    virtual void SetNumRenderLine(int iLine);
    BCDECLAREGUILD_TEXT *GetSelectedText()
    {
        return (SLGetSelectLine() == m_TextList.end() ? NULL : &(*SLGetSelectLine()));
    }
    void Sort();

  protected:
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int iLineNumber);
    virtual BOOL DoLineMouseAction(int iLineNumber);
    virtual int GetRenderLinePos_y(int iLineNumber);
};

class CUIBCGuildListBox : public CUITextListBox<BCGUILD_TEXT>

{
  public:
    int Select_Guild;
    explicit CUIBCGuildListBox(SessionKeeper &keeper);
    virtual ~CUIBCGuildListBox()
    {
    }

    virtual void AddText(const wchar_t *szGuildName, BYTE byJoinSide, BYTE byGuildInvolved,
                         int iGuildScore);
    virtual void DeleteText(DWORD dwGuildIndex);
    virtual void SetNumRenderLine(int iLine);
    BCGUILD_TEXT *GetSelectedText()
    {
        return (SLGetSelectLine() == m_TextList.end() ? NULL : &(*SLGetSelectLine()));
    }

  protected:
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int iLineNumber);
    virtual BOOL DoLineMouseAction(int iLineNumber);
    virtual int GetRenderLinePos_y(int iLineNumber);
};

class CUIMoveCommandListBox : public CUITextListBox<MOVECOMMAND_TEXT>
{
  public:
    explicit CUIMoveCommandListBox(SessionKeeper &keeper);
    virtual ~CUIMoveCommandListBox()
    {
    }

    virtual void AddText(int iIndex, const wchar_t *szMapName, const wchar_t *szSubMapName,
                         int iReqLevel, int iReqZen, int iGateNum);
    //virtual void DeleteText(DWORD dwGuildIndex);
    virtual void SetNumRenderLine(int iLine);
    MOVECOMMAND_TEXT *GetSelectedText()
    {
        return (SLGetSelectLine() == m_TextList.end() ? NULL : &(*SLGetSelectLine()));
    }
    //void Sort();

  protected:
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int iLineNumber);
    virtual BOOL DoLineMouseAction(int iLineNumber);
    virtual int GetRenderLinePos_y(int iLineNumber);
};

struct SCurQuestItem
{
    BOOL m_bIsSelected;
    DWORD m_dwIndex;
    wchar_t m_szText[64];
};

class CUICurQuestListBox : public CUITextListBox<SCurQuestItem>
{
  public:
    explicit CUICurQuestListBox(SessionKeeper &keeper);
    virtual ~CUICurQuestListBox()
    {
    }

    virtual void AddText(DWORD dwQuestIndex, const wchar_t *pszText);
    virtual void DeleteText(DWORD dwQuestIndex);
    virtual void SetNumRenderLine(int nLine);
    SCurQuestItem *GetSelectedText()
    {
        return (SLGetSelectLine() == m_TextList.end() ? NULL : &(*SLGetSelectLine()));
    }

  protected:
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int nLine);
    virtual BOOL DoLineMouseAction(int nLine);
    virtual int GetRenderLinePos_y(int nLine);
};

struct SQuestContents
{
    BOOL m_bIsSelected;
    LegacyFontRole m_fontRole = LegacyFontRole::Normal;
    DWORD m_dwColor;
    int m_nSort;
    wchar_t m_szText[64];
    REQUEST_REWARD_CLASSIFY m_eRequestReward;
    DWORD m_dwType;
    WORD m_wIndex;
    ITEM *m_pItem;
};

struct SRequestRewardText;

class CUIQuestContentsListBox : public CUITextListBox<SQuestContents>
{
  public:
    explicit CUIQuestContentsListBox(SessionKeeper &keeper);
    virtual ~CUIQuestContentsListBox()
    {
    }

    virtual void AddText(LegacyFontRole role, DWORD dwColor, int nSort, const wchar_t *pszText);
    virtual void AddText(SRequestRewardText *pRequestRewardText, int nSort);
    /*	virtual void DeleteText(DWORD dwQuestIndex);
        virtual void SetNumRenderLine(int nLine);
        SQuestContents* GetSelectedText()
        { return (SLGetSelectLine() == m_TextList.end() ? NULL : &(*SLGetSelectLine())); }
    */
  protected:
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int nLine);
    virtual void RenderCoveredInterface();
    virtual void DoActionSub(BOOL bMessageOnly);
    virtual BOOL DoLineMouseAction(int nLine);
    virtual int GetRenderLinePos_y(int nLine);
};

struct TEXTCOLOR_DATA
{
    int m_iCharPos;
    int m_iStartPos;
    DWORD m_dwTextColor;
    DWORD m_dwBackColor;
};
const int MAX_COLOR_PER_LINE = 10;

struct RENDER_TEXT_DATA
{
    wchar_t m_szText[MAX_TEXT_LENGTH + 1];
    int m_iWidth;
    int m_iHeight;
    int m_iTab;
    DWORD m_dwUseCount;
    DWORD m_dwTextColor;
    DWORD m_dwBackColor;
    int m_iTextureIndex;
    int m_iTextureIndexEx;
    BOOL m_bUseTextEX;
};

typedef std::multimap<int, RENDER_TEXT_DATA, std::less<int>> RTMap;

#define g_pMultiLanguage pMultiLanguage

class CUITextInputBox : public CUIControl
{
  public:
    explicit CUITextInputBox(SessionKeeper &keeper);
    virtual ~CUITextInputBox();

    virtual void SetSize(int iWidth, int iHeight);

    virtual void Init(int iWidth, int iHeight, int iMaxLength = 50, BOOL bIsPassword = FALSE);
    virtual void Render();
    virtual void GiveFocus(BOOL bSel = FALSE);

    virtual void SetState(int iState);
    virtual void SetFont(LegacyFontRole role);
    virtual void SetMultiline(BOOL bUseFlag)
    {
        m_bUseMultiLine = bUseFlag;
    }

    virtual void SetTextLimit(int iLimit);
    virtual void SetTextColor(BYTE a, BYTE r, BYTE g, BYTE b)
    {
        m_dwTextColor = _ARGB(a, r, g, b);
    }
    virtual void SetBackColor(BYTE a, BYTE r, BYTE g, BYTE b)
    {
        m_dwBackColor = _ARGB(a, r, g, b);
    }
    virtual void SetSelectBackColor(BYTE a, BYTE r, BYTE g, BYTE b)
    {
        m_dwSelectBackColor = _ARGB(a, r, g, b);
    }
    virtual void SetText(const wchar_t *pszText);
    virtual void GetText(wchar_t *pszText, int iGetLength = MAX_TEXT_LENGTH);
    std::wstring DisplayTextForRetainedUi() const;
    int DisplayCaretForRetainedUi() const noexcept;

    // There is no Win32 EDIT child; GetHandle() returns a stable per-instance
    // token used only as a focus identity by the NewUI "related window" routing
    // (compared, never messaged). That lets the routing keep working: a focused
    // field's owning widget is the only one that receives key events, so game
    // hotkeys stay quiet while the player is typing.
    HWND GetHandle()
    {
        return reinterpret_cast<HWND>(this);
    }
    HWND GetParentHandle()
    {
        return g_hWnd;
    }
    BOOL HaveFocus() const
    {
        return s_pFocusedPortable == this;
    }
    BOOL UseMultiline()
    {
        return m_bUseMultiLine;
    }
    virtual void SetTabTarget(CUITextInputBox *pTabTarget)
    {
        m_pTabTarget = pTabTarget;
    }
    CUITextInputBox *GetTabTarget()
    {
        return m_pTabTarget;
    }

    virtual void Lock(BOOL bFlag)
    {
        m_bLock = bFlag;
    }
    virtual BOOL IsLocked()
    {
        return m_bLock;
    }
    BOOL IsPassword()
    {
        return m_bPasswordInput;
    }

#ifdef PBG_ADD_INGAMESHOPMSGBOX
    bool GetUseScrollbar()
    {
        return m_bUseScrollbarRender;
    }
    void SetUseScrollbar(bool _scrollbar = TRUE)
    {
        m_bUseScrollbarRender = _scrollbar;
    }
#endif //PBG_ADD_INGAMESHOPMSGBOX

    // Input fed from the SDL event loop.
    void OnTextInput(const wchar_t *pszText);   // committed characters
    void OnTextEditing(const wchar_t *pszText); // IME composition preview (uncommitted)
    void OnEditKey(int iVirtualKey, bool bCtrl, bool bShift); // navigation/erase
    void SelectAll();
    std::wstring GetSelectedText() const;
    void DeleteSelection();

    // Caret rectangle in reference pixels (screen space), for positioning the IME
    // candidate window. Returns false when this box isn't the focused field.
    // Valid after the box has rendered at least once since gaining focus.
    bool GetCaretArea(int &x, int &y, int &w, int &h) const;

  protected:
    virtual BOOL DoMouseAction();

    // Portable text field implementation (issue #447).
    struct PortableLine
    {
        int start;
        int end;
    }; // [start,end) buffer indices; end excludes a wrapped space/newline

    void RenderPortable();
    BOOL DoPortableMouse();
    std::wstring BuildDisplay() const; // text with the password mask applied
    // The render helpers take the string to draw, the caret index within it, and
    // the IME composition span [compStart, compEnd) to underline (compStart < 0
    // = none). With composition active the caret/scroll follow the preview text.
    void RenderPortableSingleLine(const std::wstring &display, int iCaret, int iLineHeight,
                                  int compStart, int compEnd);
    void RenderPortableMultiline(const std::wstring &display, int iCaret, int iLineHeight,
                                 int compStart, int compEnd);
    void RenderPortableScrollbar(int iTotalLines, int iVisibleLines);
    void LayoutLines(const std::wstring &display, std::vector<PortableLine> &lines) const;
    int CaretToLine(const std::vector<PortableLine> &lines, int iCaret) const;
    // Buffer index on a line whose rendered x (reference px) is nearest targetX.
    int IndexAtLineX(const std::wstring &display, const PortableLine &line, int targetX) const;
    int LineHeightPx() const;
    int VisibleLineCount(int iLineHeight) const;
    void MoveCaret(int iNewCaret, bool bExtendSelection);
    void InsertChar(wchar_t ch);
    bool HasSelection() const
    {
        return m_iSelAnchor != m_iCaret;
    }
    int SelectionStart() const
    {
        return m_iSelAnchor < m_iCaret ? m_iSelAnchor : m_iCaret;
    }
    int SelectionEnd() const
    {
        return m_iSelAnchor < m_iCaret ? m_iCaret : m_iSelAnchor;
    }
    // Width in reference pixels of the first iLength chars rendered in the font.
    int MeasureWidth(const wchar_t *pszText, int iLength) const;
    LegacyFontRole CurrentFont() const noexcept
    {
        return m_fontRole;
    }

  public:
    CTimer m_caretTimer = {};

  protected:
    CUIFriendMenu &g_pFriendMenu;
    // Portable text field state (issue #447).
    std::wstring m_portableText;
    std::wstring m_composition; // IME preedit shown at the caret, not yet committed
    int m_iCaretAreaX = 0;      // last rendered caret rect (reference px, screen space)
    int m_iCaretAreaY = 0;      // for positioning the IME candidate window
    int m_iCaretAreaH = 0;
    int m_iCaret = 0;        // caret index in [0, length]
    int m_iSelAnchor = 0;    // selection anchor; equals caret when no selection
    int m_iFirstVisible = 0; // first rendered character (single-line horizontal scroll)
    int m_iScrollLine = 0;   // first visible wrapped line (multiline vertical scroll)
    int m_iMaxLength = 0;    // text length limit (0 = unlimited)
    LegacyFontRole m_fontRole = LegacyFontRole::Normal;
    CUITextInputBox *&s_pFocusedPortable;

    CUITextInputBox *m_pTabTarget;

    DWORD m_dwTextColor;
    DWORD m_dwBackColor;
    DWORD m_dwSelectBackColor;

    BOOL m_bPasswordInput;
    BOOL m_bLock;

    BOOL m_bUseMultiLine;
    float m_bScrollBtnClick;
    float m_bScrollBarClick;
    int m_iNumLines;
    float m_fScrollBarWidth;
    float m_fScrollBarRange_top;
    float m_fScrollBarRange_bottom;
    float m_fScrollBarHeight;
    float m_fScrollBarPos_y;
    float m_fScrollBarClickPos_y;
#ifdef PBG_ADD_INGAMESHOPMSGBOX
    bool m_bUseScrollbarRender;
#endif //PBG_ADD_INGAMESHOPMSGBOX
};

class CUIChatInputBox : protected SessionUiLegacyBindings
{
  public:
    explicit CUIChatInputBox(SessionKeeper &keeper)
        : SessionUiLegacyBindings(keeper), m_TextInputBox(keeper), m_BuddyInputBox(keeper)
    {
    }
    virtual ~CUIChatInputBox()
    {
        RemoveHistory(TRUE);
    }

    virtual void Init();
    void Reset();
    void Render();
    void TabMove(int iBoxNumber);
    void GetTexts(wchar_t *pText, wchar_t *pBuddyText);
    void ClearTexts();
    void SetText(BOOL bSetText, const wchar_t *pText, BOOL bSetBuddyText,
                 const wchar_t *pBuddyText);
    void SetState(int iState);
    int GetState()
    {
        return m_TextInputBox.GetState();
    }
    void SetFont(LegacyFontRole role);
    void SetTextPosition(int iPos_x, int iPos_y)
    {
        m_TextInputBox.SetPosition(iPos_x, iPos_y);
    }
    void SetBuddyPosition(int iPos_x, int iPos_y)
    {
        m_BuddyInputBox.SetPosition(iPos_x, iPos_y);
    }
    BOOL HaveFocus()
    {
        return (m_TextInputBox.HaveFocus() || m_BuddyInputBox.HaveFocus());
    }
    BOOL DoMouseAction();
    void RestoreFocus()
    {
        m_bFocusLose = TRUE;
    }

  protected:
    CUITextInputBox m_TextInputBox;
    CUITextInputBox m_BuddyInputBox;
    BOOL m_bFocusLose;
    int m_iBackupFocus;

  public:
    virtual void AddHistory(const wchar_t *pszText);
    virtual void MoveHistory(int iDegree);

  private:
    void RemoveHistory(BOOL bClear);
    BOOL m_bHistoryMode;
    wchar_t m_szTempText[MAX_TEXT_LENGTH + 1];
    std::deque<wchar_t *> m_HistoryList;
    std::deque<wchar_t *>::iterator m_CurrentHistoryLine;
    std::deque<wchar_t *>::iterator m_HistoryListIter;
};

class CUILoginInputBox : public CUIChatInputBox
{
  public:
    explicit CUILoginInputBox(SessionKeeper &keeper) : CUIChatInputBox(keeper)
    {
    }
    virtual ~CUILoginInputBox()
    {
    }
    virtual void Init();
    virtual void AddHistory(const wchar_t *pszText)
    {
    }
    virtual void MoveHistory(int iDegree)
    {
    }
};

class CUIMercenaryInputBox : public CUIChatInputBox
{
  public:
    explicit CUIMercenaryInputBox(SessionKeeper &keeper) : CUIChatInputBox(keeper)
    {
    }
    virtual ~CUIMercenaryInputBox()
    {
    }
    virtual void Init();
    virtual void AddHistory(const wchar_t *pszText)
    {
    }
    virtual void MoveHistory(int iDegree)
    {
    }
};

#ifdef PBG_ADD_INGAMESHOP_UI_ITEMSHOP
typedef struct
{
    BOOL m_bIsSelected;

    int m_iStorageSeq;
    int m_iStorageItemSeq;
    int m_iStorageGroupCode;
    int m_iProductSeq;
    int m_iPriceSeq;
    int m_iCashPoint;
    int m_iNum;
    WORD m_wItemCode;

    wchar_t m_szName[MAX_TEXT_LENGTH];
    wchar_t m_szNum[MAX_TEXT_LENGTH];
    wchar_t m_szPeriod[MAX_TEXT_LENGTH];
    wchar_t m_szSendUserName[MAX_USERNAME_SIZE + 1];
    wchar_t m_szMessage[MAX_GIFT_MESSAGE_SIZE];
    wchar_t m_szType;
} IGS_StorageItem;

class CUIInGameShopListBox : public CUITextListBox<IGS_StorageItem>
{
    enum IMAGE_LISTBOX_SIZE
    {
        LISTBOX_WIDTH = 146,
        LISTBOX_HEIGHT = 115,
    };

  public:
    explicit CUIInGameShopListBox(SessionKeeper &keeper);
    virtual ~CUIInGameShopListBox()
    {
    }

    virtual void AddText(IGS_StorageItem &_StorageItem);
    virtual void SetNumRenderLine(int nLine);
    IGS_StorageItem *GetSelectedText()
    {
        return (SLGetSelectLine() == m_TextList.end() ? NULL : &(*SLGetSelectLine()));
    }

  protected:
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int nLine);
    virtual BOOL DoLineMouseAction(int nLine);
    virtual int GetRenderLinePos_y(int nLine);
};

#define LINE_TEXTMAX 64
#define INFO_LINEMAX 10
#define INFO_LINE_CNTMAX 50

struct IGS_BuyList
{
    BOOL m_bIsSelected;
    wchar_t m_pszItemExplanation[LINE_TEXTMAX];
};

class CUIBuyingListBox : public CUITextListBox<IGS_BuyList>
{
    enum IMAGE_LISTBOX_SIZE
    {
        LISTBOX_WIDTH = 175,
        LISTBOX_HEIGHT = 95,
    };

  public:
    explicit CUIBuyingListBox(SessionKeeper &keeper);
    virtual ~CUIBuyingListBox()
    {
    }

    virtual void AddText(const wchar_t *pszExplanationText);
    virtual void SetNumRenderLine(int nLine);
    IGS_BuyList *GetSelectedText()
    {
        return (SLGetSelectLine() == m_TextList.end() ? NULL : &(*SLGetSelectLine()));
    }

    void SetLineColorRender(const bool _LineColor = true);
    const bool &GetLineColorRender() const
    {
        return m_bRenderLineColor;
    }

  protected:
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int nLine);
    virtual BOOL DoLineMouseAction(int nLine);
    virtual int GetRenderLinePos_y(int nLine);
    bool m_bRenderLineColor;
};

class CRadioButton
{
    enum IMAGE_RADIOBUTTON
    {
        IMAGE_CHECKBTN = BITMAP_IGS_CHECK_BUTTON,
    };
    enum SIZE_BTN
    {
        BTN_WIDTH = 14,
        BTN_HEIGHT = 14,
        BTN_SPACE = 1,
    };
    enum STATE_BUTTON
    {
        LBTN_DEFAULT = 0,
        LBTN_UP,
        LBTN_DOWN,
    };

  public:
    CRadioButton();
    ~CRadioButton();
    bool UpdateActionCheck(int _nState);
    void SetRadioBtnRect(float _x = 0, float _y = 0, float _width = BTN_WIDTH,
                         float _height = BTN_HEIGHT);
    const bool &GetCheckBtn() const
    {
        return m_bCheckState;
    }
    int m_nRadioBtnEnable;
    void SetCheckState(bool _Value);
    void SetRadioBtnIsEnable(int _Value);
    int GetRadioBtnIsEnable()
    {
        return m_nRadioBtnEnable;
    }

  private:
    friend class CUIPackCheckBuyingListBox;
    RECT m_rtCheckBtn;
    bool m_bCheckState;
    BYTE m_byMouseState;
};

struct IGS_PackBuyList
{
    BOOL m_bIsSelected;
    wchar_t m_pszItemName[32];
    wchar_t m_nItemInfo[32];
    CRadioButton m_CheckBtn;
};

typedef struct
{
    BOOL m_bIsSelected;
    int m_iPackageSeq;
    int m_iDisplaySeq;
    int m_iPriceSeq;
    WORD m_wItemCode;
    int m_iCashType;

    wchar_t m_szItemName[MAX_TEXT_LENGTH];
    wchar_t m_szItemPrice[MAX_TEXT_LENGTH];
    wchar_t m_szItemPeriod[MAX_TEXT_LENGTH];
    wchar_t m_szAttribute[MAX_TEXT_LENGTH];

    CRadioButton m_RadioBtn;
} IGS_SelectBuyItem;

class CUIPackCheckBuyingListBox : public CUITextListBox<IGS_SelectBuyItem>
{
    static constexpr float LISTBOX_WIDTH = 180.0f;
    static constexpr float LISTBOX_HEIGHT = 100.0f;
    static constexpr float TEXT_HEIGHTSIZE = 33.0f;

  public:
    explicit CUIPackCheckBuyingListBox(SessionKeeper &keeper);
    virtual ~CUIPackCheckBuyingListBox();

    virtual void AddText(IGS_SelectBuyItem &Item);
    virtual void SetNumRenderLine(int iLine);
    IGS_SelectBuyItem *GetSelectedText()
    {
        return (SLGetSelectLine() == m_TextList.end() ? NULL : &(*SLGetSelectLine()));
    }
    BOOL IsChangeLine();

  protected:
    virtual void RenderInterface();
    virtual BOOL RenderDataLine(int nLine);
    virtual BOOL DoLineMouseAction(int nLine);
    virtual int GetRenderLinePos_y(int nLine);

  private:
    void RenderRadioButton(CRadioButton &button);
    void RenderRadioButtonImage(unsigned int imageType, float x, float y, float width, float height,
                                float sourceU, float sourceV);
    int m_iCurrentLine;
};
#endif //PBG_ADD_INGAMESHOP_UI_ITEMSHOP

namespace SEASON3B
{
class CNewUIManager;

class CNewUISlideWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
    CNewUIManager *m_pNewUIMng;

  public:
    explicit CNewUISlideWindow(SessionKeeper &keeper);
    virtual ~CNewUISlideWindow();

    bool Create(CNewUIManager *pNewUIMng);
    void Release();

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);

    float GetLayerDepth(); // 1.91f

    // wrapping
    void Init();
    void CreateSlideText();
    void AddSlide(int iLoopCount, int iLoopDelay, const wchar_t *strText, int iType, float fSpeed,
                  DWORD dwTextColor = (255 << 24) + (200 << 16) + (220 << 8) + (230));

  private:
    CSlideHelpMgr *m_pSlideMgr;
};
} // namespace SEASON3B

//	NewUITextBox.h

namespace SEASON3B
{
class CNewUITextBox : public CNewUIObj, protected SessionUiLegacyBindings
{
    typedef std::vector<std::wstring> type_vector_textbase;
    type_vector_textbase m_vecText;

  protected:
    int m_iWidth;
    int m_iHeight;
    POINT m_ptPos;

    int m_iTextHeight;
    int m_iTextLineHeight;
    int m_iLimitLine;

    int m_iMaxLine;
    int m_iCurLine;

  public:
    explicit CNewUITextBox(SessionKeeper &keeper);
    virtual ~CNewUITextBox();

    bool Create(int iX, int iY, int iWidth, int iHeight);
    void SetPos(int iX, int iY, int iWidth, int iHeight);
    void Release();

    float GetLayerDepth();

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();

    bool Update();
    bool Render();

    void ClearText()
    {
        m_vecText.clear();
    }
    void AddText(wchar_t *strText);
    void AddText(const wchar_t *strText);

    std::wstring GetFullText();
    std::wstring GetLineText(int iLineIndex);

    int GetMoveableLine();
    int GetLimitLine()
    {
        return m_iLimitLine;
    }
    int GetMaxLine()
    {
        return m_vecText.size();
    }
    int GetCurLine()
    {
        return m_iCurLine;
    }
    void SetCurLine(int iLine)
    {
        m_iCurLine = iLine;
    }
};
} // namespace SEASON3B

class CUITextInputBox;
class CUIMercenaryInputBox;
class SessionKeeper;

CUITextInputBox *CreateSessionTextInputBox(SessionKeeper &keeper);
CUIMercenaryInputBox *CreateSessionMercenaryInputBox(SessionKeeper &keeper);

#ifdef UIDEFAULTBASE

#define UIInitializeFunction(classname)                                                            \
  public:                                                                                          \
    classname(const string &uiname);                                                               \
    virtual ~classname();

#define UIDefaultVariable                                                                          \
  private:                                                                                         \
    bool m_IsOpen;                                                                                 \
    string m_UIName;

#define UIDefaultFunction                                                                          \
  public:                                                                                          \
    void Open();                                                                                   \
    void Close();                                                                                  \
    bool IsOpen();                                                                                 \
    UIDefaultVariable;

namespace SEASON3A
{
class CUIDefaultBase
{
    UIInitializeFunction(CUIDefaultBase);
    UIDefaultFunction;
};

inline void CUIDefaultBase::Open()
{
    m_IsOpen = true;
}

inline void CUIDefaultBase::Close()
{
    m_IsOpen = false;
}

inline bool CUIDefaultBase::IsOpen()
{
    return m_IsOpen;
}
}; // namespace SEASON3A

#endif //UIDEFAULTBASE

// Shared legacy list capacity.
namespace LegacyControlDetail
{
inline constexpr int MaxTextLines = 150;
}

namespace LegacyControlDetail
{

#pragma pack(push)
#pragma pack()
inline constexpr int CARET_BLINK_MS = 530;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int CARET_WIDTH_PX = 2;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const int MAX_HISTORY_LINES = 10;
#pragma pack(pop)

} // namespace LegacyControlDetail

namespace LegacyComboStyle
{
inline constexpr float BG_BRIGHTNESS_IDLE = 0.05f;
inline constexpr float BG_BRIGHTNESS_HOVER = 0.15f;
inline constexpr float BG_BRIGHTNESS_OPEN = 0.10f;
inline constexpr float SCROLLBAR_TRACK_BRIGHT = 0.02f;
inline constexpr float SCROLLBAR_THUMB_BRIGHT = 0.35f;
inline constexpr int TEXT_PAD_X = 6;
inline constexpr int TEXT_PAD_Y = 4;
inline constexpr int ARROW_WIDTH = 14;
inline constexpr int SCROLLBAR_WIDTH = 6;
} // namespace LegacyComboStyle

// Common native window and character-preview controls.

const int g_ciWindowFrameThickness = 5;

const int g_ciWindowTitleHeight = 21;

const int UIWND_DEFAULT = -1;

enum UIWINDOWSTYLE
{
    UIWINDOWSTYLE_NULL = 0,
    UIWINDOWSTYLE_TITLEBAR = 1,
    UIWINDOWSTYLE_FRAME = 2,
    UIWINDOWSTYLE_RESIZEABLE = 4,
    UIWINDOWSTYLE_MOVEABLE = 8,
    UIWINDOWSTYLE_MINBUTTON = 16,
    UIWINDOWSTYLE_MAXBUTTON = 32,
    UIWINDOWSTYLE_NORMAL = UIWINDOWSTYLE_TITLEBAR | UIWINDOWSTYLE_FRAME | UIWINDOWSTYLE_RESIZEABLE |
                           UIWINDOWSTYLE_MOVEABLE | UIWINDOWSTYLE_MINBUTTON |
                           UIWINDOWSTYLE_MAXBUTTON,
    UIWINDOWSTYLE_FIXED = UIWINDOWSTYLE_TITLEBAR | UIWINDOWSTYLE_FRAME | UIWINDOWSTYLE_MOVEABLE
};

enum UIWINDOWSTYPE
{
    UIWNDTYPE_EMPTY = 0,
    UIWNDTYPE_CHAT,
    UIWNDTYPE_CHAT_READY,
    UIWNDTYPE_FRIENDMAIN,
    UIWNDTYPE_TEXTINPUT,
    UIWNDTYPE_QUESTION,
    UIWNDTYPE_READLETTER,
    UIWNDTYPE_WRITELETTER,
    UIWNDTYPE_OK,
    UIWNDTYPE_QUESTION_FORCE,
    UIWNDTYPE_OK_FORCE
};

enum UIADDWINDOWOPTION
{
    UIADDWND_NULL = 0,
    UIADDWND_FORCEPOSITION = 1
};

const int UIPHOTOVIEWER_CANCONTROL = 1;

class CUIBaseWindow : public CUIControl
{
  public:
    explicit CUIBaseWindow(SessionKeeper &keeper);
    virtual ~CUIBaseWindow();

    virtual void Init(const wchar_t *pszTitle, DWORD dwParentID = 0);

    virtual void Refresh()
    {
    }
    void SetLimitSize(int iMinWidth, int iMinHeight, int iMaxWidth = 0, int iMaxHeight = 0)
    {
        m_iMinWidth = iMinWidth;
        m_iMinHeight = iMinHeight;
        m_iMaxWidth = iMaxWidth;
        m_iMaxHeight = iMaxHeight;
    }
    virtual void SetTitle(const wchar_t *pszTitle);
    const wchar_t *GetTitle()
    {
        return m_strTitle.c_str();
    }
    virtual void Maximize();

    void Render();
    virtual int RPos_x(int iPos_x)
    {
        return iPos_x + (m_iPos_x + g_ciWindowFrameThickness);
    }
    virtual int RPos_y(int iPos_y)
    {
        return iPos_y + (m_iPos_y + g_ciWindowTitleHeight);
    }
    virtual int RWidth()
    {
        return m_iWidth - g_ciWindowFrameThickness * 2;
    }
    virtual int RHeight()
    {
        return m_iHeight - (g_ciWindowTitleHeight + g_ciWindowFrameThickness);
    }
    BOOL HaveTextBox()
    {
        return m_bHaveTextBox;
    }
    void GetBackPosition(BOOL *pbIsMaximize, int *piBackPos_y, int *piBackHeight)
    {
        *pbIsMaximize = m_bIsMaximize;
        *piBackPos_y = m_iBackPos_y;
        *piBackHeight = m_iBackHeight;
    }
    void SetBackPosition(BOOL bIsMaximize, int iBackPos_y, int iBackHeight)
    {
        m_bIsMaximize = bIsMaximize;
        m_iBackPos_y = iBackPos_y;
        m_iBackHeight = iBackHeight;
    }

    virtual BOOL CloseCheck()
    {
        return TRUE;
    }
    virtual bool PrepareModernUiOnWorker(int, int, bool)
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
    virtual bool RecordModernUi(LegacyRenderFacade &) const
    {
        return true;
    }
    virtual void ApplyModernUiChanges()
    {
    }

  protected:
    BOOL DoMouseAction();

    virtual void InitControls() = 0;

    virtual void RenderSub()
    {
    }
    virtual void RenderOver()
    {
    }
    virtual void DoActionSub(BOOL bMessageOnly)
    {
    }
    virtual void DoMouseActionSub()
    {
    }
    void DrawOutLine(int iPos_x, int iPos_y, int iWidth, int iHeight);
    void SetControlButtonColor(int iSelect);

  protected:
    int m_iMouseClickPos_x, m_iMouseClickPos_y;
    int m_iResizeDir;
    int m_iMinWidth, m_iMinHeight;
    int m_iMaxWidth, m_iMaxHeight;
    std::wstring m_strTitle;
    BOOL m_bHaveTextBox;
    int m_iControlButtonClick;
    BOOL m_bIsMaximize;
    int m_iBackPos_y, m_iBackHeight;

    std::once_flag _controlsInitialized;
};

class CUIPhotoViewer : public CUIControl
{
  public:
    explicit CUIPhotoViewer(SessionKeeper &keeper);
    virtual ~CUIPhotoViewer();

    virtual CHARACTER *GetPhotoChar()
    {
        return &m_PhotoChar;
    }

    virtual void Init(int iInitType);
    virtual void SetClass(CLASS_TYPE byClass);
    virtual void SetEquipmentPacket(BYTE *pbyEquip);
    virtual void CopyPlayer();
    virtual void SetAngle(float fDegree);
    virtual void SetZoom(float fZoom);
    virtual void SetAutoupdatePlayer(BOOL bFlag)
    {
        m_bUpdatePlayer = bFlag;
    }

    virtual void SetAnimation(int iAnimationType);
    virtual void ChangeAnimation(int iMoveDir = 0);

    void SetID(const wchar_t *pszID);
    const wchar_t *GetID()
    {
        return m_PhotoChar.ID;
    }
    float GetCurrentAngle()
    {
        return m_fCurrentAngle;
    }
    int GetCurrentAction()
    {
        return m_iSettingAnimation;
    }
    float GetCurrentZoom()
    {
        return m_fCurrentZoom;
    }
    void SetWebzenMail(BOOL bFlag)
    {
        m_bIsWebzenMail = bFlag;
    }

    virtual BOOL DoMouseAction();
    virtual void Render();

  protected:
    void RenderPhotoCharacter();
    void AdvancePhotoCharacter();
    void AdvancePhotoAnimation();
    void RetirePhotoMount();
    void PreparePhotoMount();
    void UpdatePhotoPose();
    int SelectPhotoPose(int currentAnimation, int moveDirection) const;
    int SetPhotoPose(int iCurrentAni, int iMoveDir = 0);

  protected:
    CameraProjection &cameraProjection_;
    SessionGameplayUnit &gameplay_;
    CSummonSystem &g_SummonSystem;
    CHARACTER m_PhotoChar;
    WorldCharacterVisualState m_PhotoVisual;
    double m_photoUpdateTime = 0.0;
    OBJECT m_PhotoHelper;
    std::unique_ptr<vec34_t[]> m_photoHelperBones;
    float m_fPhotoHelperScale;
    BOOL m_bIsInitialized;
    BOOL m_bHelpEnable;
    BOOL m_bUpdatePlayer;
    BOOL m_bActionRepeatCheck;
    int m_iShowType;
    int m_iCurrentAnimation;
    int m_iSettingAnimation;
    int m_iCurrentFrame;
    float m_fSettingAngle;
    float m_fCurrentAngle;
    float m_fRotateClickPos_x;
    float m_fSettingZoom;
    float m_fCurrentZoom;
    BOOL m_bIsWebzenMail;

  public:
    void SetShowType(int Stype)
    {
        m_iShowType = Stype;
    }
};

// Native window controllers grouped by feature.
#pragma pack(push)
#pragma pack()
class CUIDefaultWindow : public CUIBaseWindow
{
  public:
    explicit CUIDefaultWindow(SessionKeeper &keeper) : CUIBaseWindow(keeper)
    {
    }
    virtual ~CUIDefaultWindow()
    {
    }

    virtual int RPos_x(int iPos_x)
    {
        return iPos_x + (m_iPos_x);
    }
    virtual int RPos_y(int iPos_y)
    {
        return iPos_y + (m_iPos_y);
    }
    virtual int RWidth()
    {
        return m_iWidth;
    }
    virtual int RHeight()
    {
        return m_iHeight;
    }

  protected:
    virtual void InitControls()
    {
    }
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
class CUITabWindow : public CUIBaseWindow
{
  public:
    explicit CUITabWindow(SessionKeeper &keeper) : CUIBaseWindow(keeper)
    {
    }
    virtual ~CUITabWindow()
    {
    }

    virtual int RPos_x(int iPos_x)
    {
        return iPos_x + (m_iPos_x);
    }
    virtual int RPos_y(int iPos_y)
    {
        return iPos_y + (m_iPos_y);
    }
    virtual int RWidth()
    {
        return m_iWidth;
    }
    virtual int RHeight()
    {
        return m_iHeight;
    }

  protected:
    virtual void InitControls()
    {
    }
};
#pragma pack(pop)

BOOL CompareItemEqual(const PART_t *first, const PART_t *second);
BOOL CompareItemEqual(const PART_t *first, const ITEM *second, int defaultValue);
void SetItemToPhoto(PART_t *destination, const ITEM *source, int defaultValue);
