#include "ui/runtime/UiControls.h"
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
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"
#include "ui/session/UiSessionRender.h"

// Construction/Destruction

#ifdef UIDEFAULTBASE

using namespace SEASON3A;

CUIDefaultBase::CUIDefaultBase(const string &uiname) : m_IsOpen(false), m_UIName(uiname)
{
}

CUIDefaultBase::~CUIDefaultBase()
{
}

#endif //UIDEFAULTBASE

namespace UI::Modern
{
void ApplySegmentedFill(std::span<Rml::ElementFormControlInput *const> cells, int filledCells)
{
    for (std::size_t i = 0; i < cells.size(); ++i)
        cells[i]->SetClass("filled", static_cast<int>(i) < filledCells);
}

void SetFittedLabelText(Rml::Element &label, const std::string &text)
{
    label.SetInnerRML(Rml::StringUtilities::EncodeRml(text));
    label.RemoveProperty("font-size");
    label.GetOwnerDocument()->UpdateDocument();
    if (!label.IsVisible(true))
        return;
    const float width = label.GetBox().GetSize(Rml::BoxArea::Content).x;
    const int textWidth = Rml::ElementUtilities::GetStringWidth(&label, text);
    if (textWidth <= width)
        return;
    const float fontSize = label.GetProperty<float>("font-size");
    label.SetProperty("font-size", Rml::CreateString("%.3fpx", fontSize * width / textWidth));
}
} // namespace UI::Modern

namespace UI::Modern
{
class RmlMuButton::Listener final : public Rml::EventListener
{
  public:
    explicit Listener(RmlMuButton &owner) noexcept : owner_(owner)
    {
    }

    void ProcessEvent(Rml::Event &) override
    {
        owner_.OnClick();
    }

  private:
    RmlMuButton &owner_;
};

void ApplyRmlMuButtonVisualState(Rml::Element &element, ButtonVisualState state)
{
    element.SetClass("over", state == ButtonVisualState::Over);
    element.SetClass("down", state == ButtonVisualState::Down);
    element.SetClass("disabled", state == ButtonVisualState::Disabled);
}

RmlMuButton::RmlMuButton() : listener_(std::make_unique<Listener>(*this))
{
}

RmlMuButton::RmlMuButton(SessionKeeper &) : RmlMuButton()
{
}

RmlMuButton::~RmlMuButton()
{
    Unbind();
}

void RmlMuButton::Bind(Rml::Element &element)
{
    Unbind();
    element_ = &element;
    element_->AddEventListener("click", listener_.get());
    syncedEnabled_.reset();
    syncedVisible_.reset();
}

void RmlMuButton::Unbind() noexcept
{
    if (element_ != nullptr)
    {
        element_->RemoveEventListener("click", listener_.get());
    }
    element_ = nullptr;
    syncedEnabled_.reset();
    syncedVisible_.reset();
    clicked_.store(false, std::memory_order_release);
}

void RmlMuButton::Reset() noexcept
{
    clicked_.store(false, std::memory_order_release);
}

void RmlMuButton::SetEnable(bool enable)
{
    enabled_.store(enable, std::memory_order_release);
    if (!enable)
    {
        clicked_.store(false, std::memory_order_release);
    }
}

void RmlMuButton::SetVisible(bool visible)
{
    visible_.store(visible, std::memory_order_release);
    if (!visible)
    {
        clicked_.store(false, std::memory_order_release);
    }
}

bool RmlMuButton::IsClick() const
{
    return clicked_.exchange(false, std::memory_order_acq_rel);
}

bool RmlMuButton::OwnsPointer(const Rml::Element *pointerTarget) const noexcept
{
    if (!enabled_.load(std::memory_order_acquire) || !visible_.load(std::memory_order_acquire))
    {
        return false;
    }
    for (const Rml::Element *element = pointerTarget; element != nullptr;
         element = element->GetParentNode())
    {
        if (element == element_)
        {
            return true;
        }
    }
    return false;
}

bool RmlMuButton::SyncVisualState()
{
    if (element_ == nullptr)
    {
        return false;
    }
    bool dirty = false;
    const bool visible = visible_.load(std::memory_order_acquire);
    if (syncedVisible_ != visible)
    {
        element_->SetClass("mu-hidden", !visible);
        syncedVisible_ = visible;
        dirty = true;
    }
    const bool enabled = enabled_.load(std::memory_order_acquire);
    if (syncedEnabled_ != enabled)
    {
        element_->SetClass("disabled", !enabled);
        element_->SetProperty("pointer-events", enabled ? "auto" : "none");
        syncedEnabled_ = enabled;
        dirty = true;
    }
    return dirty;
}

void RmlMuButton::OnClick() noexcept
{
    if (enabled_.load(std::memory_order_acquire) && visible_.load(std::memory_order_acquire))
    {
        clicked_.store(true, std::memory_order_release);
    }
}
} // namespace UI::Modern

namespace UI::Modern
{
void RmlMuButtonGroup::Bind(std::span<Rml::Element *const> elements)
{
    Unbind();
    for (auto *element : elements)
    {
        auto &entry = buttons_.emplace_back();
        entry.element = element;
        entry.button.Bind(*element);
    }
}
void RmlMuButtonGroup::Unbind()
{
    buttons_.clear();
    selected_ = -1;
}
bool RmlMuButtonGroup::Apply(int selected, std::span<const bool> enabled)
{
    bool dirty = selected_ != selected;
    for (std::size_t i = 0; i < buttons_.size(); ++i)
    {
        auto &entry = buttons_[i];
        if (dirty)
            entry.element->SetClass("selected", static_cast<int>(i) == selected);
        entry.button.SetEnable(enabled[i]);
        const bool visualDirty = entry.button.SyncVisualState();
        if (visualDirty)
            dirty = true;
    }
    selected_ = selected;
    return dirty;
}
std::optional<int> RmlMuButtonGroup::TakeSelection() const
{
    std::optional<int> selected;
    for (std::size_t i = 0; i < buttons_.size(); ++i)
        if (buttons_[i].button.IsClick())
            selected = static_cast<int>(i);
    return selected;
}
} // namespace UI::Modern

namespace UI::Modern
{
namespace
{
void SetText(Rml::Element &element, const wchar_t *value)
{
    element.SetInnerRML(Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(value)));
}
} // namespace

bool RmlMuCheckBoxListRow::Bind(Rml::ElementDocument &document, std::string_view id)
{
    row_ = document.GetElementById(std::string(id));
    if (row_ == nullptr || row_->GetNumChildren() != 5)
    {
        return false;
    }

    for (std::size_t index = 0; index < text_.size(); ++index)
    {
        text_[index] = row_->GetChild(static_cast<int>(index + 1));
    }
    return std::all_of(text_.begin(), text_.end(),
                       [](Rml::Element *element) { return element != nullptr; });
}

void RmlMuCheckBoxListRow::Apply(const RmlMuCheckBoxListRowState &state,
                                 const RmlMuCheckBoxListRowState *previous)
{
    if (previous == nullptr || state.visible != previous->visible)
    {
        row_->SetProperty("display", state.visible ? "block" : "none");
    }
    for (std::size_t field = 0; field < text_.size(); ++field)
    {
        if (previous == nullptr || std::wcscmp(state.text[field], previous->text[field]) != 0)
        {
            SetText(*text_[field], state.text[field]);
        }
    }

    if (previous == nullptr || state.checked != previous->checked)
    {
        row_->SetClass("checked", state.checked);
    }
    if (previous == nullptr || state.over != previous->over)
    {
        row_->SetClass("over", state.over);
    }
    if (previous == nullptr || state.down != previous->down)
    {
        row_->SetClass("down", state.down);
    }
    if (previous == nullptr || state.disabled != previous->disabled)
    {
        row_->SetClass("disabled", state.disabled);
    }
}

void RmlMuCheckBoxListRow::Unbind() noexcept
{
    row_ = nullptr;
    text_.fill(nullptr);
}

bool RmlMuCheckBoxListRow::CanToggle(bool checked, bool disabled) noexcept
{
    return checked || !disabled;
}
} // namespace UI::Modern

namespace UI::Modern
{
class RmlMuMovablePanel::Listener final : public Rml::EventListener
{
  public:
    explicit Listener(RmlMuMovablePanel &owner) noexcept : owner_(owner)
    {
    }

    void ProcessEvent(Rml::Event &event) override
    {
        owner_.ProcessEvent(event);
    }

  private:
    RmlMuMovablePanel &owner_;
};

RmlMuMovablePanel::RmlMuMovablePanel() : listener_(std::make_unique<Listener>(*this))
{
}

RmlMuMovablePanel::~RmlMuMovablePanel()
{
    Unbind();
}

void RmlMuMovablePanel::Bind(Rml::Element &panel, Rml::Element &dragHandle)
{
    Unbind();
    panel_ = &panel;
    dragHandle_ = &dragHandle;
    dragHandle_->AddEventListener("dragstart", listener_.get());
    dragHandle_->AddEventListener("drag", listener_.get());
    dragHandle_->AddEventListener("dragend", listener_.get());
}

void RmlMuMovablePanel::Unbind() noexcept
{
    if (dragHandle_ != nullptr)
    {
        dragHandle_->RemoveEventListener("dragstart", listener_.get());
        dragHandle_->RemoveEventListener("drag", listener_.get());
        dragHandle_->RemoveEventListener("dragend", listener_.get());
    }
    panel_ = nullptr;
    dragHandle_ = nullptr;
    viewportWidth_ = 0.0F;
    viewportHeight_ = 0.0F;
    panelWidth_ = 0.0F;
    panelHeight_ = 0.0F;
    originLeft_ = 0.0F;
    originTop_ = 0.0F;
    left_ = 0.0F;
    top_ = 0.0F;
    dragAnchorX_ = 0.0F;
    dragAnchorY_ = 0.0F;
    dragging_ = false;
    dirty_ = false;
}

void RmlMuMovablePanel::Configure(float viewportWidth, float viewportHeight, float panelWidth,
                                  float panelHeight, float originLeft, float originTop)
{
    const bool originChanged = originLeft_ != originLeft || originTop_ != originTop;
    viewportWidth_ = viewportWidth;
    viewportHeight_ = viewportHeight;
    panelWidth_ = panelWidth;
    panelHeight_ = panelHeight;
    originLeft_ = originLeft;
    originTop_ = originTop;
    if (ClampPosition() || originChanged)
    {
        ApplyPosition();
    }
}

void RmlMuMovablePanel::SetPosition(float left, float top)
{
    if (left_ == left && top_ == top)
        return;
    left_ = left;
    top_ = top;
    (void)ClampPosition();
    ApplyPosition();
}

RmlMuPanelPosition RmlMuMovablePanel::Position() const noexcept
{
    return {left_, top_};
}

bool RmlMuMovablePanel::IsDragging() const noexcept
{
    return dragging_;
}

void RmlMuMovablePanel::CancelDrag() noexcept
{
    dragging_ = false;
}

bool RmlMuMovablePanel::TakeDirty() noexcept
{
    return std::exchange(dirty_, false);
}

void RmlMuMovablePanel::ProcessEvent(Rml::Event &event)
{
    const Rml::String &type = event.GetType();
    if (type == "dragstart")
    {
        dragging_ = true;
        dragAnchorX_ = event.GetParameter<float>("mouse_x", 0.0F) - left_;
        dragAnchorY_ = event.GetParameter<float>("mouse_y", 0.0F) - top_;
        return;
    }
    if (type == "drag" && dragging_)
    {
        left_ = event.GetParameter<float>("mouse_x", 0.0F) - dragAnchorX_;
        top_ = event.GetParameter<float>("mouse_y", 0.0F) - dragAnchorY_;
        (void)ClampPosition();
        ApplyPosition();
        return;
    }
    if (type == "dragend")
        dragging_ = false;
}

bool RmlMuMovablePanel::ClampPosition() noexcept
{
    const float nextLeft = std::clamp(left_, 0.0F, std::max(0.0F, viewportWidth_ - panelWidth_));
    const float nextTop = std::clamp(top_, 0.0F, std::max(0.0F, viewportHeight_ - panelHeight_));
    const bool changed = left_ != nextLeft || top_ != nextTop;
    left_ = nextLeft;
    top_ = nextTop;
    return changed;
}

void RmlMuMovablePanel::ApplyPosition()
{
    if (panel_ == nullptr)
        return;
    panel_->SetProperty("left", Rml::CreateString("%.3fpx", left_ - originLeft_));
    panel_->SetProperty("top", Rml::CreateString("%.3fpx", top_ - originTop_));
    dirty_ = true;
}
} // namespace UI::Modern

namespace UI::Modern
{
class RmlMuOptionStepper::Listener final : public Rml::EventListener
{
  public:
    explicit Listener(RmlMuOptionStepper &owner) : owner_(owner)
    {
    }
    void ProcessEvent(Rml::Event &event) override
    {
        owner_.OnKey(event);
    }

  private:
    RmlMuOptionStepper &owner_;
};
RmlMuOptionStepper::RmlMuOptionStepper() : listener_(std::make_unique<Listener>(*this))
{
}
RmlMuOptionStepper::~RmlMuOptionStepper()
{
    Unbind();
}
void RmlMuOptionStepper::Bind(Rml::Element &root, Rml::Element &previous, Rml::Element &next)
{
    Unbind();
    root_ = &root;
    root_->AddEventListener("keydown", listener_.get());
    previous_.Bind(previous);
    next_.Bind(next);
}
void RmlMuOptionStepper::Unbind()
{
    if (root_)
        root_->RemoveEventListener("keydown", listener_.get());
    root_ = nullptr;
    previous_.Unbind();
    next_.Unbind();
    requested_.reset();
    index_ = maximum_ = 0;
    enabled_ = false;
}
bool RmlMuOptionStepper::Apply(int index, int maximum, bool enabled)
{
    const bool changed = index_ != index || maximum_ != maximum || enabled_ != enabled;
    index_ = index;
    maximum_ = maximum;
    enabled_ = enabled;
    previous_.SetEnable(enabled && index > 0);
    next_.SetEnable(enabled && index < maximum);
    if (!enabled)
        requested_.reset();
    const bool previousDirty = previous_.SyncVisualState();
    const bool nextDirty = next_.SyncVisualState();
    return changed || previousDirty || nextDirty;
}
std::optional<int> RmlMuOptionStepper::TakeRequestedIndex()
{
    int index = requested_.value_or(index_);
    const bool previous = previous_.IsClick(), next = next_.IsClick();
    if (enabled_ && (previous || next))
        requested_ = std::clamp(index + int(next) - int(previous), 0, maximum_);
    if (requested_)
        index_ = *requested_;
    return std::exchange(requested_, std::nullopt);
}
void RmlMuOptionStepper::OnKey(Rml::Event &event)
{
    using namespace Rml::Input;
    const auto key = event.GetParameter<int>("key_identifier", KI_UNKNOWN);
    int next = requested_.value_or(index_);
    switch (key)
    {
    case KI_LEFT:
        --next;
        break;
    case KI_RIGHT:
        ++next;
        break;
    case KI_HOME:
        next = 0;
        break;
    case KI_END:
        next = maximum_;
        break;
    case KI_TAB:
        event.StopPropagation();
        return;
    default:
        return;
    }
    if (enabled_)
        requested_ = std::clamp(next, 0, maximum_);
    event.StopPropagation();
}
} // namespace UI::Modern

namespace UI::Modern
{
namespace
{
constexpr int LeftButton = 1, RightButton = 3;
}
bool RmlMuPalette::Bind(Rml::Element &root, bool editable)
{
    Unbind();
    root_ = &root;
    editable_ = editable;
    root.GetElementsByClassName(cells_, "mu-palette-cell");
    if (cells_.size() != PixelCount)
    {
        Unbind();
        return false;
    }
    root.SetClass("read-only", !editable);
    pixels_.fill(0);
    return true;
}
void RmlMuPalette::Unbind()
{
    for (std::size_t i = 0; i < cells_.size(); ++i)
    {
        cells_[i]->SetClass("palette-color-" + std::to_string(pixels_[i]), false);
        cells_[i]->SetClass("palette-color-0", true);
    }
    root_ = nullptr;
    cells_.clear();
    heldButton_ = 0;
    editable_ = changed_ = false;
}
void RmlMuPalette::SetPixels(const Pixels &pixels)
{
    for (std::size_t i = 0; i < PixelCount; ++i)
    {
        if (pixels_[i] == pixels[i])
            continue;
        cells_[i]->SetClass("palette-color-" + std::to_string(pixels_[i]), false);
        cells_[i]->SetClass("palette-color-" + std::to_string(pixels[i]), true);
    }
    pixels_ = pixels;
}
void RmlMuPalette::SetColor(std::uint8_t color)
{
    color_ = color;
}
void RmlMuPalette::CancelPaint()
{
    heldButton_ = 0;
}
bool RmlMuPalette::IsPainting() const
{
    return heldButton_ != 0;
}
void RmlMuPalette::Paint(Rml::Element *hovered)
{
    Rml::Element *cell = nullptr;
    for (auto *element = hovered; element; element = element->GetParentNode())
    {
        if (element->HasAttribute("data-palette-index"))
            cell = element;
        if (element != root_)
            continue;
        if (!cell)
            return;
        const auto index = cell->GetAttribute<std::size_t>("data-palette-index", 0);
        const auto color = heldButton_ == RightButton ? 0 : color_;
        if (pixels_[index] == color)
            return;
        cell->SetClass("palette-color-" + std::to_string(pixels_[index]), false);
        cell->SetClass("palette-color-" + std::to_string(color), true);
        pixels_[index] = static_cast<std::uint8_t>(color);
        changed_ = true;
        return;
    }
}
bool RmlMuPalette::ProcessInput(const SessionInputEvent &event, Rml::Element *hovered)
{
    if (!root_ || !editable_ || !root_->IsVisible())
    {
        CancelPaint();
        return false;
    }
    bool inside = false;
    for (auto *element = hovered; element; element = element->GetParentNode())
        if (element == root_)
        {
            inside = true;
            break;
        }
    const bool painting = IsPainting();
    if (event.action == SessionInputAction::PointerButton)
    {
        if (!event.pressed && event.code == heldButton_)
            CancelPaint();
        else if (event.pressed && inside && (event.code == LeftButton || event.code == RightButton))
            heldButton_ = event.code;
    }
    if (IsPainting())
        Paint(hovered);
    return painting || IsPainting();
}
std::optional<RmlMuPalette::Pixels> RmlMuPalette::TakeChanges()
{
    if (!std::exchange(changed_, false))
        return std::nullopt;
    return pixels_;
}
} // namespace UI::Modern

namespace UI::Modern
{
void RmlMuProgressBar::Bind(Rml::Element &fill, Axis axis) noexcept
{
    fill_ = &fill;
    axis_ = axis;
    ratio_ = -1.0;
    frame_ = -1;
}

void RmlMuProgressBar::Unbind() noexcept
{
    fill_ = nullptr;
    ratio_ = -1.0;
    frame_ = -1;
}

bool RmlMuProgressBar::SetFrame(int frame)
{
    if (!fill_ || frame_ == frame)
        return false;
    if (frame_ >= 0)
        fill_->SetClass("frame-" + std::to_string(frame_), false);
    fill_->SetClass("frame-" + std::to_string(frame), true);
    frame_ = frame;
    return true;
}

bool RmlMuProgressBar::SetProgressFrames(double value, double maximum, int frames)
{
    const double ratio = maximum > 0 ? std::clamp(value / maximum, 0.0, 1.0) : 0;
    return SetFrame(1 + static_cast<int>(ratio * (frames - 1)));
}

bool RmlMuProgressBar::SetProgress(double value, double maximum)
{
    const double next = maximum > 0.0 ? std::clamp(value / maximum, 0.0, 1.0) : 0.0;
    if (fill_ == nullptr || ratio_ == next)
        return false;

    ratio_ = next;
    fill_->SetProperty(axis_ == Axis::Vertical ? "height" : "width",
                       Rml::CreateString("%.3f%%", ratio_ * 100.0));
    return true;
}
} // namespace UI::Modern

namespace UI::Modern
{
namespace
{
const RmlMuScrollBarMetrics &DefaultMetrics()
{
    static const RmlUiDesign design("Data/UI/PC/Common/scrollbar.rml",
                                    {"Width", "Height", "TrackX", "TrackY", "TrackWidth",
                                     "TrackHeight", "ButtonWidth", "ButtonHeight", "DownY",
                                     "ThumbX", "ThumbY", "ThumbWidth", "DefaultThumbHeight",
                                     "DefaultThumbTravel", "MinimumThumbHeight"});
    static const RmlMuScrollBarMetrics metrics{
        design.Number(0),  design.Number(1),  design.Number(2),  design.Number(3),
        design.Number(4),  design.Number(5),  design.Number(6),  design.Number(7),
        design.Number(8),  design.Number(9),  design.Number(10), design.Number(11),
        design.Number(12), design.Number(13), design.Number(14)};
    return metrics;
}

void SetPixels(Rml::Element &element, const char *property, float value)
{
    element.SetProperty(property, Rml::CreateString("%.3fpx", value));
}

void SetBox(Rml::Element &element, float x, float y, float width, float height, float scale)
{
    SetPixels(element, "left", x * scale);
    SetPixels(element, "top", y * scale);
    SetPixels(element, "width", width * scale);
    SetPixels(element, "height", height * scale);
}
} // namespace

float DefaultRmlMuScrollBarMinimumThumbHeight() noexcept
{
    return DefaultMetrics().minimumThumbHeight;
}

class RmlMuScrollBar::Listener final : public Rml::EventListener
{
  public:
    explicit Listener(RmlMuScrollBar &owner) noexcept : owner_(owner)
    {
    }

    void ProcessEvent(Rml::Event &event) override
    {
        owner_.ProcessEvent(event);
    }

  private:
    RmlMuScrollBar &owner_;
};

RmlMuScrollBar::RmlMuScrollBar() : listener_(std::make_unique<Listener>(*this))
{
}

RmlMuScrollBar::~RmlMuScrollBar()
{
    Unbind();
}

bool RmlMuScrollBar::Bind(Rml::ElementDocument &document, std::string_view id)
{
    Unbind();
    root_ = document.GetElementById(std::string(id));
    if (root_ == nullptr || root_->GetNumChildren() != 4)
    {
        return false;
    }

    track_ = root_->GetChild(0);
    down_ = root_->GetChild(1);
    up_ = root_->GetChild(2);
    thumb_ = root_->GetChild(3);
    if (track_ == nullptr || down_ == nullptr || up_ == nullptr || thumb_ == nullptr)
    {
        Unbind();
        return false;
    }
    track_->AddEventListener("click", listener_.get());
    down_->AddEventListener("click", listener_.get());
    up_->AddEventListener("click", listener_.get());
    thumb_->AddEventListener("dragstart", listener_.get());
    thumb_->AddEventListener("drag", listener_.get());
    thumb_->AddEventListener("dragend", listener_.get());
    authoredThumbHeight_ = thumb_->GetProperty<float>("height");
    authoredThumbY_ = thumb_->GetProperty<float>("top");
    return true;
}

void RmlMuScrollBar::Unbind() noexcept
{
    if (track_ != nullptr)
        track_->RemoveEventListener("click", listener_.get());
    if (down_ != nullptr)
        down_->RemoveEventListener("click", listener_.get());
    if (up_ != nullptr)
        up_->RemoveEventListener("click", listener_.get());
    if (thumb_ != nullptr)
    {
        thumb_->RemoveEventListener("dragstart", listener_.get());
        thumb_->RemoveEventListener("drag", listener_.get());
        thumb_->RemoveEventListener("dragend", listener_.get());
    }
    root_ = nullptr;
    track_ = nullptr;
    down_ = nullptr;
    up_ = nullptr;
    thumb_ = nullptr;
    state_ = {};
    metrics_ = {};
    requestedPosition_.reset();
    scale_ = 1.0F;
    dragStartMouseY_ = 0.0F;
    dragStartPosition_ = 0;
    dragging_ = false;
    authoredGeometry_ = false;
}

void RmlMuScrollBar::Apply(const RmlMuScrollBarState &state)
{
    root_->GetOwnerDocument()->UpdateDocument();
    if (!root_->IsVisible(true))
        return;
    RmlMuScrollBarMetrics metrics{};
    metrics.trackHeight = track_->GetOffsetHeight();
    metrics.thumbX = thumb_->GetProperty<float>("left");
    metrics.thumbY = authoredThumbY_;
    metrics.thumbWidth = thumb_->GetProperty<float>("width");
    metrics.thumbHeight = authoredThumbHeight_;
    metrics.minimumThumbHeight = thumb_->GetProperty<float>("min-height");
    metrics.thumbTravel = std::max(0.0F, metrics.trackHeight - metrics.thumbHeight);
    authoredGeometry_ = true;
    Apply(state, metrics, 0.0F, 0.0F, 1.0F, nullptr);
}

void RmlMuScrollBar::Apply(const RmlMuScrollBarState &state, float x, float y, float scale,
                           const RmlMuScrollBarState *previous)
{
    Apply(state, DefaultMetrics(), x, y, scale, previous);
}

void RmlMuScrollBar::Apply(const RmlMuScrollBarState &state, const RmlMuScrollBarMetrics &metrics,
                           float x, float y, float scale, const RmlMuScrollBarState *previous)
{
    state_ = state;
    metrics_ = metrics;
    scale_ = scale;
    if (previous == nullptr && !authoredGeometry_)
    {
        SetBox(*root_, x, y, metrics.width, metrics.height, scale);
        SetBox(*track_, metrics.trackX, metrics.trackY, metrics.trackWidth, metrics.trackHeight,
               scale);
        SetBox(*down_, 0.0F, metrics.downY, metrics.buttonWidth, metrics.buttonHeight, scale);
        SetBox(*up_, 0.0F, 0.0F, metrics.buttonWidth, metrics.buttonHeight, scale);
    }
    if (previous == nullptr || state.position != previous->position ||
        state.maximum != previous->maximum || state.pageSize != previous->pageSize)
    {
        if (authoredGeometry_)
        {
            SetPixels(*thumb_, "top", metrics.thumbY + ThumbOffset(state, metrics));
            SetPixels(*thumb_, "height", ThumbLength(state, metrics));
        }
        else
        {
            SetBox(*thumb_, metrics.thumbX, metrics.thumbY + ThumbOffset(state, metrics),
                   metrics.thumbWidth, ThumbLength(state, metrics), scale);
        }
        thumb_->SetProperty("display", state.maximum == 0 ? "none" : "block");
        ApplyRmlMuButtonVisualState(*track_, state.maximum == 0 ? ButtonVisualState::Disabled
                                                                : ButtonVisualState::Up);
    }
    if (previous == nullptr || state.down != previous->down)
    {
        ApplyRmlMuButtonVisualState(*down_, state.down);
    }
    if (previous == nullptr || state.up != previous->up)
    {
        ApplyRmlMuButtonVisualState(*up_, state.up);
    }
    if (previous == nullptr || state.thumb != previous->thumb)
    {
        ApplyRmlMuButtonVisualState(*thumb_, state.thumb);
    }
}

RmlMuScrollBar::Rect RmlMuScrollBar::UpButtonRect(float x, float y) noexcept
{
    return {x, y, DefaultMetrics().buttonWidth, DefaultMetrics().buttonHeight};
}

RmlMuScrollBar::Rect RmlMuScrollBar::DownButtonRect(float x, float y) noexcept
{
    return {x, y + DefaultMetrics().downY, DefaultMetrics().buttonWidth,
            DefaultMetrics().buttonHeight};
}

RmlMuScrollBar::Rect RmlMuScrollBar::ThumbRect(float x, float y, std::size_t position,
                                               std::size_t maximum) noexcept
{
    return {x + DefaultMetrics().thumbX,
            y + DefaultMetrics().thumbY + ThumbOffset(position, maximum),
            DefaultMetrics().thumbWidth, DefaultMetrics().thumbHeight};
}

float RmlMuScrollBar::ThumbOffset(std::size_t position, std::size_t maximum) noexcept
{
    return maximum == 0 ? 0.0F
                        : DefaultMetrics().thumbTravel * static_cast<float>(position) /
                              static_cast<float>(maximum);
}

float RmlMuScrollBar::ThumbLength(const RmlMuScrollBarState &state,
                                  const RmlMuScrollBarMetrics &metrics) noexcept
{
    if (state.pageSize == 0)
    {
        return metrics.thumbHeight;
    }
    const std::size_t itemCount = state.maximum + state.pageSize;
    const float requested = itemCount == 0
                                ? metrics.trackHeight
                                : static_cast<float>(state.pageSize) /
                                      static_cast<float>(itemCount) * metrics.trackHeight;
    return std::clamp(requested, std::min(metrics.minimumThumbHeight, metrics.trackHeight),
                      metrics.trackHeight);
}

float RmlMuScrollBar::ThumbTravel(const RmlMuScrollBarState &state,
                                  const RmlMuScrollBarMetrics &metrics) noexcept
{
    return state.pageSize == 0 ? metrics.thumbTravel
                               : std::max(0.0F, metrics.trackHeight - ThumbLength(state, metrics));
}

float RmlMuScrollBar::ThumbOffset(const RmlMuScrollBarState &state,
                                  const RmlMuScrollBarMetrics &metrics) noexcept
{
    return state.maximum == 0 ? 0.0F
                              : ThumbTravel(state, metrics) * static_cast<float>(state.position) /
                                    static_cast<float>(state.maximum);
}

std::size_t RmlMuScrollBar::DragPosition(std::size_t startPosition, int mouseDelta,
                                         std::size_t maximum, float displayTravel) noexcept
{
    if (maximum == 0 || displayTravel == 0.0F)
    {
        return startPosition;
    }

    const float positionDelta =
        static_cast<float>(mouseDelta) * static_cast<float>(maximum) / displayTravel;
    const int target =
        static_cast<int>(std::lround(static_cast<float>(startPosition) + positionDelta));
    return static_cast<std::size_t>(std::clamp(target, 0, static_cast<int>(maximum)));
}

std::optional<std::size_t> RmlMuScrollBar::TakeRequestedPosition() noexcept
{
    return std::exchange(requestedPosition_, std::nullopt);
}

bool RmlMuScrollBar::IsDragging() const noexcept
{
    return dragging_;
}

void RmlMuScrollBar::ProcessEvent(Rml::Event &event)
{
    const Rml::String &type = event.GetType();
    Rml::Element *const current = event.GetCurrentElement();
    if (type == "click")
    {
        if (current == up_ && state_.position > 0)
        {
            RequestPosition(state_.position - 1);
        }
        else if (current == down_ && state_.position < state_.maximum)
        {
            RequestPosition(state_.position + 1);
        }
        else if (current == track_ && state_.maximum > 0)
        {
            const float mouseY = event.GetParameter<float>("mouse_y", 0.0F);
            const float thumbCenter =
                thumb_->GetAbsoluteOffset().y + thumb_->GetBox().GetSize().y * 0.5F;
            const std::size_t step =
                state_.trackStep > 0 ? state_.trackStep : std::max<std::size_t>(1, state_.pageSize);
            const int direction = mouseY < thumbCenter ? -1 : 1;
            const int requested =
                static_cast<int>(state_.position) + direction * static_cast<int>(step);
            RequestPosition(static_cast<std::size_t>(
                std::clamp(requested, 0, static_cast<int>(state_.maximum))));
        }
        event.StopPropagation();
        return;
    }
    if (type == "dragstart" && current == thumb_ && state_.maximum > 0)
    {
        dragging_ = true;
        dragStartMouseY_ = event.GetParameter<float>("mouse_y", 0.0F);
        dragStartPosition_ = state_.position;
        event.StopPropagation();
        return;
    }
    if (type == "drag" && dragging_)
    {
        const int delta = static_cast<int>(
            std::lround(event.GetParameter<float>("mouse_y", 0.0F) - dragStartMouseY_));
        RequestPosition(DragPosition(dragStartPosition_, delta, state_.maximum,
                                     ThumbTravel(state_, metrics_) * scale_));
        event.StopPropagation();
        return;
    }
    if (type == "dragend")
    {
        dragging_ = false;
        event.StopPropagation();
    }
}

void RmlMuScrollBar::RequestPosition(std::size_t position) noexcept
{
    requestedPosition_ = std::min(position, state_.maximum);
}
} // namespace UI::Modern

namespace UI::Modern
{
namespace
{
bool Within(const Rml::Element *element, const Rml::Element *parent)
{
    for (; element; element = element->GetParentNode())
        if (element == parent)
            return true;
    return false;
}
} // namespace
RmlMuScrollingList::~RmlMuScrollingList()
{
    Unbind();
}
bool RmlMuScrollingList::Bind(Rml::ElementDocument &document, const char *listId,
                              const char *scrollbarId, const char *templateId)
{
    Unbind();
    list_ = document.GetElementById(listId);
    template_ = document.GetElementById(templateId);
    scrollbarElement_ = scrollbarId ? document.GetElementById(scrollbarId) : nullptr;
    if (!list_ || !template_ || (scrollbarId && !scrollbarElement_))
        return false;
    rowHeight_ = template_->GetProperty<float>("height");
    if (rowHeight_ <= 0 || (scrollbarId && !scrollbar_.Bind(document, scrollbarId)))
        return false;
    dirty_ = true;
    return true;
}
void RmlMuScrollingList::Unbind()
{
    ResizeRows(0);
    scrollbar_.Unbind();
    list_ = scrollbarElement_ = template_ = nullptr;
    first_ = maximum_ = 0;
    wheelRemainder_ = 0;
    selected_.reset();
    requested_.reset();
    hovered_.reset();
    dirty_ = true;
}
void RmlMuScrollingList::SetData(const Data &data)
{
    if (data_ == data)
        return;
    data_ = data;
    if (selected_ && *selected_ >= data_.size())
        selected_.reset();
    dirty_ = true;
}
void RmlMuScrollingList::SetPaletteData(const std::vector<RmlMuPalette::Pixels> &marks)
{
    if (marks_ == marks)
        return;
    marks_ = marks;
    dirty_ = true;
}
void RmlMuScrollingList::Select(std::optional<std::size_t> index)
{
    if (selected_ == index)
        return;
    selected_ = index;
    dirty_ = true;
}
void RmlMuScrollingList::ScrollToStart()
{
    MoveTo(0);
}
bool RmlMuScrollingList::MoveTo(std::size_t position)
{
    position = std::min(position, maximum_);
    if (position == first_)
        return false;
    first_ = position;
    dirty_ = true;
    return true;
}
void RmlMuScrollingList::ResizeRows(std::size_t count)
{
    while (rows_.size() > count)
    {
        auto &row = rows_.back();
        row.palette.Unbind();
        row.button.Unbind();
        list_->RemoveChild(row.element);
        rows_.pop_back();
    }
    while (rows_.size() < count)
    {
        auto element = template_->Clone();
        element->SetClass("mu-button", true);
        element->SetId(list_->GetId() + "-row-" + std::to_string(rows_.size()));
        element->SetProperty(Rml::PropertyId::Top,
                             Rml::Property(rows_.size() * rowHeight_, Rml::Unit::PX));
        auto &row = rows_.emplace_back();
        row.element = list_->AppendChild(std::move(element));
        row.element->GetElementsByClassName(row.cells, "mu-list-cell");
        row.button.Bind(*row.element);
        Rml::ElementList palettes;
        row.element->GetElementsByClassName(palettes, "mu-palette");
        if (!palettes.empty())
            row.hasPalette = row.palette.Bind(*palettes.front(), false);
    }
}
void RmlMuScrollingList::ApplyRows()
{
    for (std::size_t i = 0; i < rows_.size(); ++i)
    {
        auto &row = rows_[i];
        const auto index = first_ + i;
        row.button.SetVisible(index < data_.size());
        row.button.SyncVisualState();
        if (index >= data_.size())
            continue;
        row.element->SetClass("selected", selected_ == index);
        if (row.hasPalette)
            row.palette.SetPixels(index < marks_.size() ? marks_[index] : RmlMuPalette::Pixels{});
        for (std::size_t column = 0; column < row.cells.size(); ++column)
        {
            const auto text = column < data_[index].size()
                                  ? StringUtils::WideToNarrow(data_[index][column].c_str())
                                  : std::string{};
            row.cells[column]->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
        }
    }
}
bool RmlMuScrollingList::Apply()
{
    if (!list_ || !list_->IsVisible())
        return false;
    const auto pageSize = static_cast<std::size_t>(
        std::max(0.0f, std::floor(list_->GetBox().GetSize(Rml::BoxArea::Content).y / rowHeight_)));
    if (rows_.size() != pageSize)
    {
        ResizeRows(pageSize);
        dirty_ = true;
    }
    if (auto position = scrollbar_.TakeRequestedPosition())
        MoveTo(*position);
    if (!dirty_)
        return false;
    maximum_ = data_.size() > pageSize ? data_.size() - pageSize : 0;
    first_ = std::min(first_, maximum_);
    ApplyRows();
    RmlMuScrollBarState state;
    state.position = first_;
    state.maximum = maximum_;
    state.pageSize = state.trackStep = std::min(data_.size(), pageSize);
    state.up = first_ ? ButtonVisualState::Up : ButtonVisualState::Disabled;
    state.down = first_ < maximum_ ? ButtonVisualState::Up : ButtonVisualState::Disabled;
    state.thumb = maximum_ ? ButtonVisualState::Up : ButtonVisualState::Disabled;
    if (scrollbarElement_)
        scrollbar_.Apply(state);
    dirty_ = false;
    return true;
}
void RmlMuScrollingList::ProcessInput(const SessionInputEvent &event, const Rml::Element *hovered)
{
    if (!list_ || !list_->IsVisible())
        return;
    if (auto position = scrollbar_.TakeRequestedPosition())
        MoveTo(*position);
    if (event.action == SessionInputAction::PointerWheel &&
        (Within(hovered, list_) || Within(hovered, scrollbarElement_)))
    {
        wheelRemainder_ += event.wheel;
        const auto rows = static_cast<std::int64_t>(wheelRemainder_);
        wheelRemainder_ -= static_cast<float>(rows);
        const auto next =
            std::clamp<std::int64_t>(static_cast<std::int64_t>(first_) - rows, 0, maximum_);
        MoveTo(static_cast<std::size_t>(next));
    }
    hovered_.reset();
    for (std::size_t i = 0; i < rows_.size(); ++i)
    {
        if (first_ + i < data_.size() && Within(hovered, rows_[i].element))
            hovered_ = first_ + i;
        if (!rows_[i].button.IsClick())
            continue;
        selected_ = requested_ = first_ + i;
        dirty_ = true;
    }
}
std::optional<std::size_t> RmlMuScrollingList::TakeSelection()
{
    return std::exchange(requested_, {});
}
std::optional<std::size_t> RmlMuScrollingList::Hovered() const
{
    return hovered_;
}
bool RmlMuScrollingList::IsDragging() const
{
    return scrollbar_.IsDragging();
}
} // namespace UI::Modern

namespace UI::Modern
{
RmlMuSkillSlot::~RmlMuSkillSlot()
{
    Unbind();
}
void RmlMuSkillSlot::Bind(Rml::Element &root, Rml::Element &button, Rml::Element &icon, int columns)
{
    Unbind();
    root_ = &root;
    button_.Bind(button);
    icon_.Bind(icon, columns);
}
void RmlMuSkillSlot::Unbind()
{
    button_.Unbind();
    icon_.Unbind();
    root_ = nullptr;
    selected_ = -1;
}
bool RmlMuSkillSlot::Apply(const RmlSkillIconState &icon, int selectedState, bool enabled)
{
    bool dirty = icon_.Set(icon);
    if (selected_ != selectedState)
    {
        if (selected_ >= 0)
            root_->SetClass("selected-" + std::to_string(selected_), false);
        root_->SetClass("selected-" + std::to_string(selectedState), true);
        selected_ = selectedState;
        dirty = true;
    }
    button_.SetEnable(enabled);
    return button_.SyncVisualState() || dirty;
}
bool RmlMuSkillSlot::TakeClick() const
{
    return button_.IsClick();
}
bool RmlMuSkillSlot::OwnsPointer(const Rml::Element *target) const
{
    for (; target; target = target->GetParentNode())
        if (target == root_)
            return true;
    return false;
}
} // namespace UI::Modern

namespace UI::Modern
{
class RmlMuSlot::Listener final : public Rml::EventListener
{
  public:
    explicit Listener(RmlMuSlot &owner) noexcept : owner_(owner)
    {
    }

    void ProcessEvent(Rml::Event &event) override
    {
        owner_.ProcessEvent(event);
    }

  private:
    RmlMuSlot &owner_;
};

RmlMuSlot::RmlMuSlot() : listener_(std::make_unique<Listener>(*this))
{
}

RmlMuSlot::RmlMuSlot(SessionKeeper &) : RmlMuSlot()
{
}

RmlMuSlot::~RmlMuSlot()
{
    Unbind();
}

void RmlMuSlot::Bind(Rml::Element &element)
{
    Unbind();
    element_ = &element;
    button_.Bind(element);
    element_->AddEventListener("mouseup", listener_.get());
}

void RmlMuSlot::Unbind() noexcept
{
    if (element_ != nullptr)
    {
        if (iconFrame_ != 0)
            element_->SetClass("icon-frame-" + std::to_string(iconFrame_), false);
        element_->RemoveEventListener("mouseup", listener_.get());
    }
    button_.Unbind();
    element_ = nullptr;
    iconFrame_ = 0;
    cleared_.store(false, std::memory_order_release);
}

void RmlMuSlot::Reset() noexcept
{
    button_.Reset();
    cleared_.store(false, std::memory_order_release);
}

void RmlMuSlot::SetEnable(bool enable)
{
    button_.SetEnable(enable);
    if (!enable)
        cleared_.store(false, std::memory_order_release);
}

bool RmlMuSlot::IsClick() const
{
    return button_.IsClick();
}

bool RmlMuSlot::IsClear()
{
    return cleared_.exchange(false, std::memory_order_acq_rel);
}

bool RmlMuSlot::SyncVisualState()
{
    return button_.SyncVisualState();
}

void RmlMuSlot::SetIconState(int frame)
{
    if (iconFrame_ == frame)
        return;
    if (iconFrame_ != 0)
        element_->SetClass("icon-frame-" + std::to_string(iconFrame_), false);
    iconFrame_ = frame;
    element_->SetClass("icon-frame-" + std::to_string(frame), true);
}

void RmlMuSlot::ProcessEvent(Rml::Event &event) noexcept
{
    constexpr int RightMouseButton = 1;
    if (event.GetParameter<int>("button", 0) == RightMouseButton)
    {
        cleared_.store(true, std::memory_order_release);
    }
}
} // namespace UI::Modern

namespace UI::Modern
{
RmlMuTextArea::~RmlMuTextArea()
{
    Unbind();
}
bool RmlMuTextArea::Bind(Rml::ElementDocument &document, const char *id, const char *scrollbarId)
{
    Unbind();
    element_ = document.GetElementById(id);
    hasScrollbar_ = scrollbarId != nullptr;
    return element_ && (!hasScrollbar_ || scrollbar_.Bind(document, scrollbarId));
}
void RmlMuTextArea::Unbind()
{
    scrollbar_.Unbind();
    element_ = nullptr;
    state_.reset();
    markup_.clear();
    hasScrollbar_ = false;
}
void RmlMuTextArea::SetMarkup(const std::string &markup, bool resetScroll)
{
    if (markup_ == markup)
        return;
    markup_ = markup;
    element_->SetInnerRML(markup);
    if (resetScroll)
        element_->SetScrollTop(0);
    state_.reset();
}
void RmlMuTextArea::ScrollPage(int direction)
{
    element_->SetScrollTop(element_->GetScrollTop() + direction * element_->GetClientHeight());
}
bool RmlMuTextArea::AtEnd() const
{
    // Element::SetScrollTop rounds its final clamped offset to a whole logical pixel.
    return element_->GetScrollTop() >=
           std::round(std::max(0.0f, element_->GetScrollHeight() - element_->GetClientHeight()));
}
bool RmlMuTextArea::AtStart() const
{
    return element_->GetScrollTop() == 0;
}
bool RmlMuTextArea::Apply()
{
    if (!element_ || !element_->IsVisible())
        return false;
    const float lineHeight = element_->GetProperty<float>("line-height");
    if (auto position = scrollbar_.TakeRequestedPosition())
        element_->SetScrollTop(float(*position) * lineHeight);
    if (!hasScrollbar_)
        return false;
    RmlMuScrollBarState next;
    next.position = std::size_t(std::ceil(element_->GetScrollTop() / lineHeight));
    next.maximum = std::size_t(std::max(
        0.0f, std::ceil((element_->GetScrollHeight() - element_->GetClientHeight()) / lineHeight)));
    next.pageSize = next.trackStep =
        std::size_t(std::ceil(element_->GetClientHeight() / lineHeight));
    next.up = next.position ? ButtonVisualState::Up : ButtonVisualState::Disabled;
    next.down = next.position < next.maximum ? ButtonVisualState::Up : ButtonVisualState::Disabled;
    next.thumb = next.maximum ? ButtonVisualState::Up : ButtonVisualState::Disabled;
    if (state_ == next)
        return false;
    scrollbar_.Apply(next);
    state_ = next;
    return true;
}
bool RmlMuTextArea::IsDragging() const
{
    return scrollbar_.IsDragging();
}
} // namespace UI::Modern

namespace UI::Modern
{
void RmlSkillIcon::Bind(Rml::Element &element, int columns)
{
    element_ = &element;
    sheet_ = element.GetFirstChild();
    columns_ = columns;
    state_.reset();
}
void RmlSkillIcon::Unbind()
{
    element_ = sheet_ = nullptr;
    state_.reset();
}
bool RmlSkillIcon::Set(const RmlSkillIconState &state)
{
    if (state_ == state)
        return false;
    element_->SetProperty("display", state.sheet < 0 ? "none" : "block");
    if (state.sheet >= 0)
    {
        sheet_->SetClassNames("skill-sheet sheet-" + std::to_string(state.sheet));
        constexpr float Percent = 100.0f;
        sheet_->SetProperty(Rml::PropertyId::Left,
                            Rml::Property(-(state.icon % columns_) * Percent, Rml::Unit::PERCENT));
        sheet_->SetProperty(Rml::PropertyId::Top,
                            Rml::Property(-(state.icon / columns_) * Percent, Rml::Unit::PERCENT));
    }
    state_ = state;
    return true;
}
} // namespace UI::Modern

namespace UI::Modern
{
RmlSkillIconState RmlSkillIconState::FromSkill(int skillId, int useType, int icon, bool disabled)
{
    // S16 asData.ImageLoader.DrawIcon_Skill sheet indices and SkillUseType values.
    constexpr int Normal = 0, Master = 2, Fourth = 4;
    constexpr int FourthActive = 7, FourthPassive = 8, FourthBrand = 9;
    int sheet = Normal;
    switch (useType)
    {
    case SKILL_USE_TYPE_MASTERLEVEL:
    case SKILL_USE_TYPE_MASTERACTIVE:
        sheet = Master;
        break;
    case FourthActive:
    case FourthPassive:
    case FourthBrand:
        sheet = Fourth;
        break;
    }
    // The shipped S6 BMD leaves normal Magic_Icon unset. S16's normal sheet
    // retains skill-ID cells, including pet commands 120-123 and Fenrir 76.
    if (sheet == Normal && icon == 0)
        icon = skillId;
    return {sheet + static_cast<int>(disabled), icon};
}
} // namespace UI::Modern

// Construction/Destruction

namespace
{
void PointSet(POINT &p, int x, int y)
{
    p.x = x;
    p.y = y;
}

} // namespace

using namespace SEASON3B;

// CNewUIBaseButton

NewUIBaseButtonLegacyCalls::NewUIBaseButtonLegacyCalls(SessionKeeper &keeper,
                                                       CNewUIBaseButton &owner) noexcept
    : SessionUiLegacyBindings(keeper), owner_(owner)
{
}

CNewUIBaseButton::CNewUIBaseButton(SessionKeeper &keeper)
    : NewUIBaseButtonLegacyCalls(keeper, *this), m_Lock(false), m_EventState(BUTTON_STATE_UP)
{
    PointSet(m_Pos, 0, 0);
    PointSet(m_Size, 0, 0);
}

CNewUIBaseButton::~CNewUIBaseButton()
{
}

void CNewUIBaseButton::SetPos(int x, int y)
{
    PointSet(m_Pos, x, y);
}

void CNewUIBaseButton::SetSize(int sx, int sy)
{
    PointSet(m_Size, sx, sy);
}

bool CNewUIBaseButton::RadioProcess()
{
    bool isMousein = CheckMouseIn(m_Pos.x, m_Pos.y, m_Size.x, m_Size.y);

#ifdef KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE
    if (isMousein)
    {
        if (IsPress(VK_LBUTTON))
        {
            if (m_EventState == BUTTON_STATE_OVER)
            {
                m_EventState = BUTTON_STATE_DOWN;
                return true;
            }
        }
        else
        {
            if (m_EventState == BUTTON_STATE_UP)
            {
                m_EventState = BUTTON_STATE_OVER;
            }
        }
    }
    else
    {
        if (m_EventState == BUTTON_STATE_OVER)
        {
            m_EventState = BUTTON_STATE_UP;
        }
    }
#else  // KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE
    if (IsPress(VK_LBUTTON) && isMousein)
    {
        if (m_EventState == BUTTON_STATE_UP)
        {
            m_EventState = BUTTON_STATE_DOWN;
            return true;
        }
        else if (m_EventState == BUTTON_STATE_DOWN)
        {
            m_EventState = BUTTON_STATE_UP;
        }
    }
#endif // KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE

    return false;
}

bool CNewUIBaseButton::Process()
{
    bool isMousein = CheckMouseIn(m_Pos.x, m_Pos.y, m_Size.x, m_Size.y);

    if (IsNone(VK_LBUTTON) && isMousein)
    {
        m_EventState = BUTTON_STATE_OVER;
    }
    else if (IsRepeat(VK_LBUTTON) && isMousein)
    {
        m_EventState = BUTTON_STATE_DOWN;
    }
    else if (IsRelease(VK_LBUTTON) && isMousein)
    {
        m_EventState = BUTTON_STATE_UP;
        return true;
    }
    else
    {
        m_EventState = BUTTON_STATE_UP;
    }

    return false;
}

// CNewUIButton
CNewUIButton::CNewUIButton(SessionKeeper &keeper)
    : CNewUIBaseButton(keeper), m_CurImgIndex(0), m_CurImgState(0), m_ImgWidth(0), m_ImgHeight(0),
      m_NameColor(0xFFFFFFFF), m_NameBackColor(0x00000000), m_CurImgColor(0xFFFFFFFF),
      m_TooltipTextColor(0xFFFFFFFF), m_IsTopPos(false),
#ifndef KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE // #ifndef
      m_IsImgWidth(false),
#endif // KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE
      m_fAlpha(1.0f)
{
    Initialize();
}

CNewUIButton::~CNewUIButton()
{
    Destroy();
}

void SEASON3B::CNewUIButton::Initialize()
{
    m_textFontRole = LegacyFontRole::Normal;
    m_toolTipFontRole = LegacyFontRole::Normal;
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    m_iMoveTextPosX = 0;
    m_iMoveTextPosY = 0;
    m_bClickEffect = false;
    m_iMoveTextTipPosX = 0;
    m_iMoveTextTipPosY = 0;
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
}

void SEASON3B::CNewUIButton::Destroy()
{
    if (m_LocaleObserverRegistered)
    {
        I18N::UnregisterLocaleObserver(&CNewUIButton::OnLocaleChanged, this);
        m_LocaleObserverRegistered = false;
    }

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    UnRegisterButtonState();
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
}

void SEASON3B::CNewUIButton::ChangeText(const wchar_t *const *nameSlot)
{
    m_pNameSlot = nameSlot;
    m_Name = (nameSlot != nullptr && *nameSlot != nullptr) ? *nameSlot : L"";
    EnsureLocaleObserver();
}

void SEASON3B::CNewUIButton::ChangeToolTipText(const wchar_t *const *tooltipSlot, bool istoppos)
{
    m_pTooltipSlot = tooltipSlot;
    m_TooltipText = (tooltipSlot != nullptr && *tooltipSlot != nullptr) ? *tooltipSlot : L"";
    m_IsTopPos = istoppos;
    EnsureLocaleObserver();
}

void SEASON3B::CNewUIButton::EnsureLocaleObserver()
{
    if (m_LocaleObserverRegistered)
        return;
    I18N::RegisterLocaleObserver(&CNewUIButton::OnLocaleChanged, this);
    m_LocaleObserverRegistered = true;
}

void SEASON3B::CNewUIButton::OnLocaleChanged(void *ctx) noexcept
{
    auto *self = static_cast<CNewUIButton *>(ctx);
    if (self->m_pNameSlot != nullptr && *self->m_pNameSlot != nullptr)
    {
        self->m_Name = *self->m_pNameSlot;
    }
    if (self->m_pTooltipSlot != nullptr && *self->m_pTooltipSlot != nullptr)
    {
        self->m_TooltipText = *self->m_pTooltipSlot;
    }
}

#ifdef KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE
void SEASON3B::CNewUIButton::ChangeButtonImgState(bool imgregister, int imgindex,
                                                  bool overflg /* = false */,
                                                  bool bLockImage /* = false */,
                                                  bool bClickEffect /* = false  */)
{
    m_bClickEffect = bClickEffect;

    if (imgregister)
    {
        RegisterButtonState(BUTTON_STATE_UP, imgindex, 0);

        if (overflg)
        {
            RegisterButtonState(BUTTON_STATE_OVER, imgindex, 1);
            RegisterButtonState(BUTTON_STATE_DOWN, imgindex, 2);
        }
        else
        {
            RegisterButtonState(BUTTON_STATE_OVER, imgindex, 0);
            RegisterButtonState(BUTTON_STATE_DOWN, imgindex, 1);
        }

        if (bLockImage)
        {
            RegisterButtonState(BUTTON_STATE_LOCK, imgindex, 3);
        }
        else
        {
            RegisterButtonState(BUTTON_STATE_LOCK, imgindex, 0);
        }

        ChangeImgIndex(imgindex, 0);
    }
}
#else // KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
void SEASON3B::CNewUIButton::ChangeButtonImgState(bool imgregister, int imgindex, bool overflg,
                                                  bool isimgwidth, bool bClickEffect)
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
void SEASON3B::CNewUIButton::ChangeButtonImgState(bool imgregister, int imgindex, bool overflg,
                                                  bool isimgwidth)
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
{
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    m_bClickEffect = bClickEffect;
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

    if (imgregister)
    {
        if (overflg)
        {
            RegisterButtonState(BUTTON_STATE_UP, imgindex, 0);
            RegisterButtonState(BUTTON_STATE_OVER, imgindex, 1);
            RegisterButtonState(BUTTON_STATE_DOWN, imgindex, 2);
        }
        else
        {
            RegisterButtonState(BUTTON_STATE_UP, imgindex, 0);
            RegisterButtonState(BUTTON_STATE_DOWN, imgindex, 1);
        }

        ChangeImgIndex(imgindex, 0);
        ChangeImgWidth(isimgwidth);
    }
}
#endif // KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE

void SEASON3B::CNewUIButton::ChangeButtonInfo(int x, int y, int sx, int sy)
{
    SetPos(x, y);
    SetSize(sx, sy);
}

void SEASON3B::CNewUIButton::RegisterButtonState(BUTTON_STATE eventstate, int imgindex, int btstate)
{
    ButtonInfo btinfo;
    btinfo.s_ImgIndex = imgindex;
    btinfo.s_BTstate = btstate;

    m_ButtonInfo.insert(std::make_pair(eventstate, btinfo));
}

void SEASON3B::CNewUIButton::UnRegisterButtonState()
{
    m_ButtonInfo.clear();
}

void SEASON3B::CNewUIButton::ChangeImgIndex(int imgindex, int curimgstate)
{
    m_CurImgIndex = imgindex;
    m_CurImgState = curimgstate;

    if (m_CurImgIndex != -1)
    {
        const auto b = Bitmaps[m_CurImgIndex];

        m_ImgWidth = b.Width;
        m_ImgHeight = b.Height;
    }
}

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
void SEASON3B::CNewUIButton::ChangeButtonState(BUTTON_STATE eventstate, int iButtonState)
{
    if (m_ButtonInfo.size() != 0)
    {
        auto iter = m_ButtonInfo.find(static_cast<int>(eventstate));

        if (iter != m_ButtonInfo.end())
        {
            ButtonInfo &info = (*iter).second;
            info.s_BTstate = iButtonState;
        }
    }
}

void SEASON3B::CNewUIButton::MoveTextPos(int iX, int iY)
{
    m_iMoveTextPosX = iX;
    m_iMoveTextPosY = iY;
}

void SEASON3B::CNewUIButton::MoveTextTipPos(int iX, int iY)
{
    m_iMoveTextTipPosX = iX;
    m_iMoveTextTipPosY = iY;
}
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

void CNewUIButton::ChangeAlpha(unsigned char fAlpha, bool isfontalph)
{
    m_CurImgColor &= ~(0xff << 24);
    m_CurImgColor |= (fAlpha << 24);

    if (isfontalph)
    {
        m_NameColor &= ~(0xff << 24);
        m_NameColor |= (fAlpha << 24);
    }
}

void CNewUIButton::ChangeAlpha(float fAlpha, bool isfontalph)
{
    m_CurImgColor &= ~(0xff << 24);
    m_CurImgColor |= (static_cast<unsigned char>((float)(0xff) * fAlpha) << 24);

    if (isfontalph)
    {
        m_NameColor &= ~(0xff << 24);
        m_NameColor |= (static_cast<unsigned char>((float)(0xff) * fAlpha) << 24);
    }
}

void SEASON3B::CNewUIButton::ChangeImgColor(BUTTON_STATE eventstate, unsigned int color)
{
    if (m_ButtonInfo.size() != 0)
    {
        auto iter = m_ButtonInfo.find(static_cast<int>(eventstate));

        if (iter != m_ButtonInfo.end())
        {
            ButtonInfo &info = (*iter).second;
            info.s_imgColor = color;
            if (GetBTState() == eventstate)
            {
                m_CurImgColor = color;
            }
        }
    }
}

void SEASON3B::CNewUIButton::ChangeFrame()
{
    if (m_ButtonInfo.size() != 0)
    {
        auto iter = m_ButtonInfo.find(static_cast<int>(GetBTState()));

        if (iter != m_ButtonInfo.end())
        {
            ButtonInfo &info = (*iter).second;

            ChangeImgIndex(info.s_ImgIndex, info.s_BTstate);

            m_CurImgColor = info.s_imgColor;
        }
    }
}

bool SEASON3B::CNewUIButton::UpdateMouseEvent()
{
    if (IsLock())
    {
        return false;
    }

    BUTTON_STATE backevent = GetBTState();

    bool result = Process();

    if (backevent != GetBTState())
    {
        ChangeFrame();
    }

    return result;
}

#ifdef KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE
void SEASON3B::CNewUIButton::Lock()
{
    CNewUIBaseButton::Lock();
    ChangeFrame();
}

void SEASON3B::CNewUIButton::UnLock()
{
    CNewUIBaseButton::UnLock();
    ChangeFrame();
}
#endif // KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE

CNewUIRadioButton::CNewUIRadioButton(SessionKeeper &keeper)
    : CNewUIBaseButton(keeper), m_NameColor(0xffB5B5B5), m_NameBackColor(0x00000000),
      m_CurImgIndex(0), m_CurImgState(0), m_ImgWidth(0), m_ImgHeight(0), m_CurImgColor(0xffffffff)
{
    Initialize();
}

CNewUIRadioButton::~CNewUIRadioButton()
{
    Destroy();
}

void CNewUIRadioButton::Initialize()
{
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    m_textFontRole = LegacyFontRole::Normal;
    m_bClickEffect = false;
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
}

void CNewUIRadioButton::Destroy()
{
    if (m_LocaleObserverRegistered)
    {
        I18N::UnregisterLocaleObserver(&CNewUIRadioButton::OnLocaleChanged, this);
        m_LocaleObserverRegistered = false;
    }

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    UnRegisterButtonState();
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
}

void CNewUIRadioButton::ChangeText(const wchar_t *const *nameSlot)
{
    m_pNameSlot = nameSlot;
    m_Name = (nameSlot != nullptr && *nameSlot != nullptr) ? *nameSlot : L"";
    EnsureLocaleObserver();
}

void CNewUIRadioButton::EnsureLocaleObserver()
{
    if (m_LocaleObserverRegistered)
        return;
    I18N::RegisterLocaleObserver(&CNewUIRadioButton::OnLocaleChanged, this);
    m_LocaleObserverRegistered = true;
}

void CNewUIRadioButton::OnLocaleChanged(void *ctx) noexcept
{
    auto *self = static_cast<CNewUIRadioButton *>(ctx);
    if (self->m_pNameSlot != nullptr && *self->m_pNameSlot != nullptr)
    {
        self->m_Name = *self->m_pNameSlot;
    }
}

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
#ifdef KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE
void CNewUIRadioButton::ChangeRadioButtonImgState(int imgindex, bool bMouseOnImage, bool bLockImage,
                                                  bool bClickEffect)
{
    int btState = 0;

    m_bClickEffect = bClickEffect;
    m_bLockImage = bLockImage;

    RegisterButtonState(BUTTON_STATE_UP, imgindex, btState++);

    if (bMouseOnImage == true)
    {
        RegisterButtonState(BUTTON_STATE_OVER, imgindex, btState++);
    }

    RegisterButtonState(BUTTON_STATE_DOWN, imgindex, btState++);

    if (bLockImage == true)
    {
        RegisterButtonState(BUTTON_STATE_LOCK, imgindex, btState++);
    }
}
#else  // KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE
void CNewUIRadioButton::ChangeRadioButtonImgState(int imgindex, bool isDown, bool bClickEffect)
{
    m_bClickEffect = bClickEffect;
    RegisterButtonState(BUTTON_STATE_UP, imgindex, 0);
    RegisterButtonState(BUTTON_STATE_DOWN, imgindex, 1);

    if (isDown)
    {
        ChangeFrame(BUTTON_STATE_DOWN);
    }
}
#endif // KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
void CNewUIRadioButton::ChangeRadioButtonImgState(int imgindex, bool isDown)
{
    RegisterButtonState(BUTTON_STATE_UP, imgindex, 0);
    RegisterButtonState(BUTTON_STATE_DOWN, imgindex, 1);

    if (isDown)
    {
        ChangeFrame(BUTTON_STATE_DOWN);
    }
}
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

void CNewUIRadioButton::ChangeRadioButtonInfo(int x, int y, int sx, int sy)
{
    SetPos(x, y);
    SetSize(sx, sy);
}

void CNewUIRadioButton::RegisterButtonState(BUTTON_STATE eventstate, int imgindex, int btstate)
{
    ButtonInfo btinfo;
    btinfo.s_ImgIndex = imgindex;
    btinfo.s_BTstate = btstate;

    m_RadioButtonInfo.insert(std::make_pair(eventstate, btinfo));
}

void CNewUIRadioButton::UnRegisterButtonState()
{
    m_RadioButtonInfo.clear();
}

void CNewUIRadioButton::ChangeImgColor(BUTTON_STATE eventstate, unsigned int color)
{
    if (m_RadioButtonInfo.size() != 0)
    {
        auto iter = m_RadioButtonInfo.find(static_cast<int>(eventstate));

        if (iter != m_RadioButtonInfo.end())
        {
            ButtonInfo &info = (*iter).second;
            info.s_imgColor = color;
            if (GetBTState() == eventstate)
            {
                m_CurImgColor = color;
            }
        }
    }
}

void CNewUIRadioButton::ChangeImgIndex(int imgindex, int curimgstate)
{
    m_CurImgIndex = imgindex;
    m_CurImgState = curimgstate;

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    if (m_CurImgIndex != -1 && m_CurImgIndex != BITMAP_UNKNOWN)
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
    if (m_CurImgIndex != -1)
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
    {
        const auto b = Bitmaps[m_CurImgIndex];

        m_ImgWidth = b.Width;
        m_ImgHeight = b.Height;
    }
}

void CNewUIRadioButton::ChangeFrame(BUTTON_STATE eventstate)
{
    m_EventState = eventstate;

    if (m_RadioButtonInfo.size() != 0)
    {
        auto iter = m_RadioButtonInfo.find(static_cast<int>(GetBTState()));

        if (iter != m_RadioButtonInfo.end())
        {
            ButtonInfo &info = (*iter).second;

            ChangeImgIndex(info.s_ImgIndex, info.s_BTstate);

            m_CurImgColor = info.s_imgColor;
        }
    }

    if (m_Name.size() != 0)
    {
        if (GetBTState() == BUTTON_STATE_UP)
            ChangeTextColor(0xffB5B5B5);
        else
            ChangeTextColor(0xffFFFFFF);
    }
}

void CNewUIRadioButton::ChangeFrame()
{
    if (m_RadioButtonInfo.size() != 0)
    {
        auto iter = m_RadioButtonInfo.find(static_cast<int>(GetBTState()));

        if (iter != m_RadioButtonInfo.end())
        {
            ButtonInfo &info = (*iter).second;

            ChangeImgIndex(info.s_ImgIndex, info.s_BTstate);

            m_CurImgColor = info.s_imgColor;
        }
    }

    if (m_Name.size() != 0)
    {
        if (GetBTState() == BUTTON_STATE_UP)
            ChangeTextColor(0xffB5B5B5);
        else
            ChangeTextColor(0xffFFFFFF);
    }
}

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
void CNewUIRadioButton::ChangeButtonState(int iImgIndex, BUTTON_STATE eventstate, int iButtonState)
{
    if (m_RadioButtonInfo.size() != 0)
    {
        auto iter = m_RadioButtonInfo.find(static_cast<int>(eventstate));

        if (iter != m_RadioButtonInfo.end())
        {
            ButtonInfo &info = (*iter).second;
            info.s_ImgIndex = iImgIndex;
            info.s_BTstate = iButtonState;
        }
    }
}

void CNewUIRadioButton::ChangeButtonState(BUTTON_STATE eventstate, int iButtonState)
{
    if (m_RadioButtonInfo.size() != 0)
    {
        auto iter = m_RadioButtonInfo.find(static_cast<int>(eventstate));

        if (iter != m_RadioButtonInfo.end())
        {
            ButtonInfo &info = (*iter).second;
            info.s_BTstate = iButtonState;
        }
    }
}
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

bool CNewUIRadioButton::UpdateMouseEvent(bool isGroupevent)
{
    if (IsLock())
    {
        return false;
    }

    BUTTON_STATE backevent = GetBTState();

    bool result = false;

    if (isGroupevent)
    {
        if (GetBTState() != BUTTON_STATE_DOWN)
        {
            result = RadioProcess();
            if (backevent != GetBTState())
            {
                ChangeFrame();
            }
        }
    }
    else
    {
        result = RadioProcess();
        if (backevent != GetBTState())
        {
            ChangeFrame();
        }
    }

    return result;
}

// CNewUIRadioGroupButton

CNewUIRadioGroupButton::CNewUIRadioGroupButton(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper)
{
    Initialize();
}

CNewUIRadioGroupButton::~CNewUIRadioGroupButton()
{
    Destroy();
}

void CNewUIRadioGroupButton::Initialize()
{
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    m_iButtonDistance = 1;
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
}

void CNewUIRadioGroupButton::Destroy()
{
    UnRegisterRadioButton();
}

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
#ifdef KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE
void CNewUIRadioGroupButton::CreateRadioGroup(int radiocount, int imgindex,
                                              bool bFirstIndexBtnDown /* = true */,
                                              bool bMouseOnImage /* = false */, bool bLockImage,
                                              bool bClickEffect /* = false  */)
{
    for (int i = 0; i < radiocount; ++i)
    {
        CNewUIRadioButton *button = new CNewUIRadioButton(SessionOrigin());

        button->ChangeRadioButtonImgState(imgindex, bMouseOnImage, bLockImage, bClickEffect);
        button->ChangeRadioButtonInfo(0, 0, 0, 0);
        RegisterRadioButton(button);
    }

    int iCurIndex = -1;

    if (bFirstIndexBtnDown == true)
    {
        iCurIndex = 0;
    }

    ChangeFrame(iCurIndex);
    SetCurButtonIndex(iCurIndex);
}
#else  // KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE
void CNewUIRadioGroupButton::CreateRadioGroup(int radiocount, int imgindex, bool bClickEffect)
{
    for (int i = 0; i < radiocount; ++i)
    {
        auto *button = new CNewUIRadioButton(SessionOrigin());

        button->ChangeRadioButtonImgState(imgindex, ((i == 0) ? true : false), bClickEffect);
        button->ChangeRadioButtonInfo(0, 0, 0, 0);
        RegisterRadioButton(button);
    }

    SetCurButtonIndex(0);
}
#endif // KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
void CNewUIRadioGroupButton::CreateRadioGroup(int radiocount, int imgindex)
{
    for (int i = 0; i < radiocount; ++i)
    {
        CNewUIRadioButton *button = new CNewUIRadioButton(SessionOrigin());
        button->ChangeRadioButtonImgState(imgindex, ((i == 0) ? true : false));
        button->ChangeRadioButtonInfo(0, 0, 0, 0);
        RegisterRadioButton(button);
    }

    SetCurButtonIndex(0);
}
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
void CNewUIRadioGroupButton::ChangeRadioButtonInfo(bool iswidth, int x, int y, int sx, int sy,
                                                   int iDistance /* = 1*/)
{
    int i = 0;

    m_iButtonDistance = iDistance;

    for (auto iter = m_RadioList.begin(); iter != m_RadioList.end();)
    {
        auto curiter = iter;
        ++iter;
        CNewUIRadioButton *button = (*curiter);

        if (button)
        {
            if (iswidth)
                button->SetPos(x + ((sx + m_iButtonDistance) * i), y);
            else
                button->SetPos(x, y + ((sy + m_iButtonDistance) * i));
            button->SetSize(sx, sy);
        }

        ++i;
    }
}

void CNewUIRadioGroupButton::ChangeButtonState(BUTTON_STATE eventstate, int iButtonState)
{
    int i = 0;
    for (auto iter = m_RadioList.begin(); iter != m_RadioList.end();)
    {
        auto curiter = iter;
        ++iter;
        CNewUIRadioButton *button = (*curiter);

        button->ChangeButtonState(eventstate, iButtonState);

        ++i;
    }
}

void CNewUIRadioGroupButton::ChangeButtonState(int iBtnIndex, int iImgIndex,
                                               BUTTON_STATE eventstate, int iButtonState)
{
    int i = 0;
    for (auto iter = m_RadioList.begin(); iter != m_RadioList.end();)
    {
        auto curiter = iter;
        ++iter;
        CNewUIRadioButton *button = (*curiter);

        if (i == iBtnIndex)
        {
            button->ChangeButtonState(iImgIndex, eventstate, iButtonState);
            return;
        }

        ++i;
    }
}

#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
void CNewUIRadioGroupButton::ChangeRadioButtonInfo(bool iswidth, int x, int y, int sx, int sy)
{
    int i = 0;

    for (RadioButtonList::iterator iter = m_RadioList.begin(); iter != m_RadioList.end();)
    {
        RadioButtonList::iterator curiter = iter;
        ++iter;
        CNewUIRadioButton *button = (*curiter);

        if (button)
        {
            if (iswidth)
                button->SetPos(x + ((sx + 1) * i), y);
            else
                button->SetPos(x, y + ((sy + 1) * i));
            button->SetSize(sx, sy);
        }

        ++i;
    }
}
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

void CNewUIRadioGroupButton::ChangeRadioText(std::list<std::wstring> &textlist)
{
    auto textiter = textlist.begin();

    for (auto iter = m_RadioList.begin(); iter != m_RadioList.end();)
    {
        auto curiter = iter;
        ++iter;
        CNewUIRadioButton *button = (*curiter);

        auto curtextiter = textiter;
        ++textiter;
        std::wstring text = (*curtextiter);

        button->ChangeText(text);

        if (textiter == textlist.end())
            break;
    }
}

void CNewUIRadioGroupButton::ChangeRadioText(std::list<const wchar_t *const *> &slotList)
{
    auto slotIter = slotList.begin();

    for (auto iter = m_RadioList.begin(); iter != m_RadioList.end(); ++iter)
    {
        if (slotIter == slotList.end())
            break;

        CNewUIRadioButton *button = *iter;
        if (button != nullptr)
            button->ChangeText(*slotIter);

        ++slotIter;
    }
}

void CNewUIRadioGroupButton::ChangeFrame(int buttonIndex)
{
    int i = 0;
    for (auto iter = m_RadioList.begin(); iter != m_RadioList.end();)
    {
        auto curiter = iter;
        ++iter;
        CNewUIRadioButton *button = (*curiter);

        if (i != buttonIndex)
        {
            button->ChangeFrame(BUTTON_STATE_UP);
        }
        else
        {
            SetCurButtonIndex(i);
            button->ChangeFrame(BUTTON_STATE_DOWN);
        }
        ++i;
    }
}

// �߰� : Pruarin(07.09.03)
void CNewUIRadioGroupButton::LockButtonindex(int buttonIndex)
{
    int i = 0;
    for (auto iter = m_RadioList.begin(); iter != m_RadioList.end();)
    {
        auto curiter = iter;
        ++iter;
        CNewUIRadioButton *button = (*curiter);

        if (i == buttonIndex)
        {
            button->Lock();
            break;
        }

        ++i;
    }
}

#ifdef KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE
void CNewUIRadioGroupButton::UnLockButtonIndex(int buttonIndex)
{
    int i = 0;
    for (RadioButtonList::iterator iter = m_RadioList.begin(); iter != m_RadioList.end();)
    {
        RadioButtonList::iterator curiter = iter;
        ++iter;
        CNewUIRadioButton *button = (*curiter);

        if (i == buttonIndex)
        {
            button->UnLock();
            break;
        }

        ++i;
    }
}
#endif // KJH_MOD_RADIOBTN_MOUSE_OVER_IMAGE

void CNewUIRadioGroupButton::RegisterRadioButton(CNewUIRadioButton *button)
{
    m_RadioList.push_back(button);
}

void CNewUIRadioGroupButton::UnRegisterRadioButton()
{
    for (auto iter = m_RadioList.rbegin(); iter != m_RadioList.rend();)
    {
        auto curiter = iter;
        ++iter;
        CNewUIRadioButton *button = (*curiter);

        SAFE_DELETE(button);
    }

    m_RadioList.clear();
}

int CNewUIRadioGroupButton::UpdateMouseEvent()
{
    int i = 0;

    for (auto iter = m_RadioList.begin(); iter != m_RadioList.end();)
    {
        auto curiter = iter;
        ++iter;
        CNewUIRadioButton *button = (*curiter);

        if (button->UpdateMouseEvent(true))
        {
            ChangeFrame(i);
            return GetCurButtonIndex();
        }

        ++i;
    }

    return static_cast<int>(RADIOGROUPEVENT_NONE);
}

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
void CNewUIRadioGroupButton::SetFont(LegacyFontRole role)
{
    for (auto iter = m_RadioList.begin(); iter != m_RadioList.end();)
    {
        auto curiter = iter;
        ++iter;
        CNewUIRadioButton *button = (*curiter);

        button->SetFont(role);
    }
}

void CNewUIRadioGroupButton::SetFont(LegacyFontRole role, int iButtonIndex)
{
    int i = 0;
    for (auto iter = m_RadioList.begin(); iter != m_RadioList.end();)
    {
        auto curiter = iter;
        ++iter;
        CNewUIRadioButton *button = (*curiter);

        if (i == iButtonIndex)
        {
            button->SetFont(role);
            break;
        }

        ++i;
    }
}

POINT CNewUIRadioGroupButton::GetPos(int iButtonIndex)
{
    int i = 0;
    for (auto iter = m_RadioList.begin(); iter != m_RadioList.end();)
    {
        auto curiter = iter;
        ++iter;
        CNewUIRadioButton *button = (*curiter);

        if (i == iButtonIndex)
        {
            return button->GetPos();
        }
        ++i;
    }

    POINT Pos;
    Pos.x = 0;
    Pos.y = 0;

    return Pos;
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

SEASON3B::CNewUICheckBox::CNewUICheckBox(SessionKeeper &keeper) : SessionUiLegacyBindings(keeper)
{
    s_ImgIndex = -1;
    m_Pos.x = 0;
    m_Pos.y = 0;
    m_Size.x = 15;
    m_Size.y = 15;
    m_Name.clear();
    m_textFontRole = LegacyFontRole::Normal;
    m_NameColor = 0xFFFFFFFF;
    m_NameBackColor = 0x00000000;
    m_ImgWidth = 0.0;
    m_ImgHeight = 15.f;
    State = 0;
}

SEASON3B::CNewUICheckBox::~CNewUICheckBox()
{
    if (m_LocaleObserverRegistered)
    {
        I18N::UnregisterLocaleObserver(&CNewUICheckBox::OnLocaleChanged, this);
        m_LocaleObserverRegistered = false;
    }
}

void SEASON3B::CNewUICheckBox::CheckBoxImgState(int imgindex)
{
    s_ImgIndex = imgindex;
}

void SEASON3B::CNewUICheckBox::RegisterBoxState(bool eventstate)
{
    State = eventstate;
}

void SEASON3B::CNewUICheckBox::ChangeText(std::wstring btname)
{
    m_pNameSlot = nullptr;
    m_Name = btname;
}

void SEASON3B::CNewUICheckBox::ChangeText(const wchar_t *const *nameSlot)
{
    m_pNameSlot = nameSlot;
    m_Name = (nameSlot != nullptr && *nameSlot != nullptr) ? *nameSlot : L"";
    EnsureLocaleObserver();
}

void SEASON3B::CNewUICheckBox::EnsureLocaleObserver()
{
    if (m_LocaleObserverRegistered)
        return;
    I18N::RegisterLocaleObserver(&CNewUICheckBox::OnLocaleChanged, this);
    m_LocaleObserverRegistered = true;
}

void SEASON3B::CNewUICheckBox::OnLocaleChanged(void *ctx) noexcept
{
    auto *self = static_cast<CNewUICheckBox *>(ctx);
    if (self->m_pNameSlot != nullptr && *self->m_pNameSlot != nullptr)
    {
        self->m_Name = *self->m_pNameSlot;
    }
}

void SEASON3B::CNewUICheckBox::CheckBoxInfo(int x, int y, int sx, int sy)
{
    m_Pos.x = x;
    m_Pos.y = y;
    m_Size.x = sx;
    m_Size.y = sy;
}

bool SEASON3B::CNewUICheckBox::GetBoxState()
{
    return State;
}

bool SEASON3B::CNewUICheckBox::UpdateMouseEvent()
{
    if (CheckMouseIn(m_Pos.x, m_Pos.y, m_Size.x, m_Size.y))
    {
        if (IsRelease(VK_LBUTTON))
        {
            State = !State;
            return TRUE;
        }
    }
    return 0;
}

// OMF-01988

CNewUIComboBox::CNewUIComboBox(SessionKeeper &keeper) : SessionUiLegacyBindings(keeper)
{
}

void CNewUIComboBox::Setup(int x, int y, int width, int itemHeight, const wchar_t *const *labels,
                           int itemCount, int initialIdx, int maxVisibleItems)
{
    m_X = x;
    m_Y = y;
    m_Width = width;
    m_ItemHeight = itemHeight;
    m_Labels = labels;
    m_ItemCount = itemCount;
    m_MaxVisibleItems = maxVisibleItems;
    m_ScrollOffset = 0;
    SetSelectedIndex(initialIdx);
    m_bOpen = false;
}

void CNewUIComboBox::SetSelectedIndex(int idx)
{
    if (m_ItemCount <= 0)
    {
        m_SelectedIndex = 0;
        return;
    }
    if (idx < 0)
        idx = 0;
    if (idx >= m_ItemCount)
        idx = m_ItemCount - 1;
    m_SelectedIndex = idx;
    ScrollToShowIndex(m_SelectedIndex);
}

int CNewUIComboBox::GetVisibleCount() const
{
    if (m_MaxVisibleItems <= 0 || m_MaxVisibleItems >= m_ItemCount)
        return m_ItemCount;
    return m_MaxVisibleItems;
}

int CNewUIComboBox::GetMaxScrollOffset() const
{
    const int visible = GetVisibleCount();
    return (m_ItemCount > visible) ? (m_ItemCount - visible) : 0;
}

bool CNewUIComboBox::IsScrollable() const
{
    return m_MaxVisibleItems > 0 && m_ItemCount > m_MaxVisibleItems;
}

void CNewUIComboBox::ClampScrollOffset()
{
    const int maxOffset = GetMaxScrollOffset();
    if (m_ScrollOffset < 0)
        m_ScrollOffset = 0;
    if (m_ScrollOffset > maxOffset)
        m_ScrollOffset = maxOffset;
}

void CNewUIComboBox::ScrollToShowIndex(int idx)
{
    if (!IsScrollable())
    {
        m_ScrollOffset = 0;
        return;
    }
    if (idx < m_ScrollOffset)
        m_ScrollOffset = idx;
    else if (idx >= m_ScrollOffset + m_MaxVisibleItems)
        m_ScrollOffset = idx - m_MaxVisibleItems + 1;
    ClampScrollOffset();
}

int CNewUIComboBox::GetItemIndexAtMouse() const
{
    if (!m_bOpen)
        return -1;

    const int listY = GetListY();
    const int visible = GetVisibleCount();
    // Scrollbar eats the right edge when present
    const int rowWidth = IsScrollable() ? (m_Width - LegacyComboStyle::SCROLLBAR_WIDTH) : m_Width;

    for (int row = 0; row < visible; row++)
    {
        const int itemY = listY + row * m_ItemHeight;
        if (CheckMouseIn(m_X, itemY, rowWidth, m_ItemHeight))
            return m_ScrollOffset + row;
    }
    return -1;
}

bool CNewUIComboBox::IsMouseOverWidget() const
{
    if (CheckMouseIn(m_X, m_Y, m_Width, m_ItemHeight))
        return true;
    if (m_bOpen && CheckMouseIn(m_X, GetListY(), m_Width, GetListHeight()))
        return true;
    return false;
}

bool CNewUIComboBox::UpdateMouseEvent()
{
    // Mouse-wheel scrolling inside the open dropdown (consumed so it doesn't
    // leak to other handlers like the volume sliders).
    if (m_bOpen && IsScrollable() && MouseWheel != 0 &&
        CheckMouseIn(m_X, GetListY(), m_Width, GetListHeight()))
    {
        // MouseWheel sign convention matches NewUIOptionWindow sliders:
        // positive = wheel up = scroll toward earlier items.
        if (MouseWheel > 0)
            m_ScrollOffset--;
        else
            m_ScrollOffset++;
        ClampScrollOffset();
        MouseWheel = 0;
    }

    if (!IsPress(VK_LBUTTON))
        return false;

    const bool clickedClosedField = CheckMouseIn(m_X, m_Y, m_Width, m_ItemHeight);

    // Clicking the closed field toggles the dropdown.
    if (clickedClosedField)
    {
        m_bOpen = !m_bOpen;
        if (m_bOpen)
            ScrollToShowIndex(m_SelectedIndex);
        return false;
    }

    if (!m_bOpen)
        return false;

    // Dropdown open: did the click land on an item?
    const int hitIdx = GetItemIndexAtMouse();
    if (hitIdx < 0)
    {
        // Click landed outside the list (or on the scrollbar column) -- close
        // without changing selection.
        m_bOpen = false;
        return false;
    }

    const bool changed = (hitIdx != m_SelectedIndex);
    m_SelectedIndex = hitIdx;
    m_bOpen = false;
    return changed;
}

//	NewUIScrollBar.cpp

CNewUIScrollBar::CNewUIScrollBar(SessionKeeper &keeper) : SessionUiLegacyBindings(keeper)
{
    memset(&m_ptPos, 0, sizeof(POINT));
    memset(&m_ptScrollBtnStartPos, 0, sizeof(POINT));
    memset(&m_ptScrollBtnPos, 0, sizeof(POINT));

    m_iScrollBarPickGap = 0;

    m_iScrollBarMovePixel = 1;
    m_iScrollBarHeightPixel = 0;
    m_iScrollBarMiddleNum = 0;
    m_iScrollBarMiddleRemainderPixel = 0;

    m_iScrollBtnMouseEvent = SCROLLBAR_MOUSEBTN_NORMAL;
    m_bScrollBtnActive = false;

    m_fPercentOfSize = 1.0f;

    m_iBeginPos = 0;
    m_iCurPos = 0;
    m_iMaxPos = 1;
}

CNewUIScrollBar::~CNewUIScrollBar()
{
    Release();
}

bool CNewUIScrollBar::Create(int iX, int iY, int iHeight)
{
    m_iHeight = iHeight;
    SetPos(iX, iY);

    LoadImages();
    Show(true);
    m_bScrollBtnActive = true;

    return true;
}

void CNewUIScrollBar::Release()
{
    UnloadImages();
}

float CNewUIScrollBar::GetLayerDepth()
{
    return 4.4f;
}

void CNewUIScrollBar::SetPos(int x, int y)
{
    m_ptPos.x = x;
    m_ptPos.y = y;

    m_ptScrollBtnStartPos.x = m_ptPos.x - (SCROLLBTN_WIDTH / 2 - SCROLLBAR_TOP_WIDTH / 2);
    m_ptScrollBtnStartPos.y = m_ptPos.y;

    m_ptScrollBtnPos.x = m_ptScrollBtnStartPos.x;
    m_ptScrollBtnPos.y = m_ptScrollBtnStartPos.y;

    m_iScrollBarMovePixel = m_iHeight - SCROLLBTN_HEIGHT;
    if (m_iScrollBarMovePixel < 0)
        m_iScrollBarMovePixel = 1;

    m_iScrollBarHeightPixel = m_iHeight;

    m_iScrollBarMiddleNum =
        (m_iScrollBarHeightPixel - (SCROLLBAR_TOP_HEIGHT * 2)) / SCROLLBAR_MIDDLE_HEIGHT;
    m_iScrollBarMiddleRemainderPixel =
        (m_iScrollBarHeightPixel - (SCROLLBAR_TOP_HEIGHT * 2)) % SCROLLBAR_MIDDLE_HEIGHT;
}

bool CNewUIScrollBar::UpdateBtnEvent()
{
    if (IsRelease(VK_LBUTTON))
    {
        m_iScrollBtnMouseEvent = SCROLLBAR_MOUSEBTN_NORMAL;
        m_iScrollBarPickGap = 0;
        return true;
    }

    if (CheckMouseIn(m_ptScrollBtnPos.x, m_ptScrollBtnPos.y, SCROLLBTN_WIDTH, SCROLLBTN_HEIGHT))
    {
        if (IsPress(VK_LBUTTON) && m_bScrollBtnActive == true)
        {
            m_iScrollBarPickGap = MouseY - m_ptScrollBtnPos.y;
            m_iScrollBtnMouseEvent = SCROLLBAR_MOUSEBTN_CLICKED;
            return false;
        }
    }

    if (CheckMouseIn(m_ptPos.x, m_ptPos.y, SCROLLBAR_TOP_WIDTH, m_iScrollBarHeightPixel))
    {
        if (IsPress(VK_LBUTTON) && m_bScrollBtnActive == true)
        {
            float fPercent = (float)(MouseY - m_ptPos.y) / (float)m_iScrollBarMovePixel;
            SetPercent(fPercent);
            return true;
        }
    }

    return true;
}

bool CNewUIScrollBar::UpdateMouseEvent()
{
    if (UpdateBtnEvent() == true)
        return false;

    return true;
}

bool CNewUIScrollBar::UpdateKeyEvent()
{
    if (!IsVisible())
    {
    }

    return true;
}

bool CNewUIScrollBar::Update()
{
    if (m_iScrollBtnMouseEvent == SCROLLBAR_MOUSEBTN_CLICKED)
    {
        float fPercent =
            (float)(MouseY - m_iScrollBarPickGap - m_ptPos.y) / (float)m_iScrollBarMovePixel;
        SetPercent(fPercent);
    }

    return true;
}

void CNewUIScrollBar::UpdateScrolling()
{
    m_ptScrollBtnPos.y = m_ptScrollBtnStartPos.y + m_fPercentOfSize * m_iScrollBarMovePixel;
}

void CNewUIScrollBar::ScrollUp(int iMoveValue)
{
    SetCurPos(m_iCurPos + iMoveValue);
}

void CNewUIScrollBar::ScrollDown(int iMoveValue)
{
    SetCurPos(m_iCurPos - iMoveValue);
}

void CNewUIScrollBar::SetPercent(float fPercent)
{
    if (fPercent <= 0.0f)
        m_fPercentOfSize = 0.0f;
    else if (fPercent >= 1.0f)
        m_fPercentOfSize = 1.0f;
    else
        m_fPercentOfSize = fPercent;

    m_iCurPos = m_iMaxPos * m_fPercentOfSize;
    UpdateScrolling();
}

void CNewUIScrollBar::SetMaxPos(int iMaxPos)
{
    if (iMaxPos < 1)
        iMaxPos = 1;

    m_iMaxPos = iMaxPos;
}

void CNewUIScrollBar::SetCurPos(int iPosValue)
{
    if (m_iBeginPos >= iPosValue)
        m_iCurPos = m_iBeginPos;
    else if (m_iMaxPos <= iPosValue)
        m_iCurPos = m_iMaxPos;
    else
        m_iCurPos = iPosValue;

    m_fPercentOfSize = (float)m_iCurPos / (float)m_iMaxPos;
    UpdateScrolling();
}

SEASON3B::CNewUISlideWindow::CNewUISlideWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper)
{
    m_pNewUIMng = NULL;
    m_pSlideMgr = NULL;
}

SEASON3B::CNewUISlideWindow::~CNewUISlideWindow()
{
    Release();
}

bool SEASON3B::CNewUISlideWindow::Create(CNewUIManager *pNewUIMng)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_SLIDEWINDOW, this);
    m_pSlideMgr = new CSlideHelpMgr(SessionOrigin());
    std::wstring strFileName =
        L"Data\\Local\\" + g_strSelectedML + L"\\Slide_" + g_strSelectedML + L".bmd";
    m_pSlideMgr->OpenSlideTextFile(strFileName.c_str());

    return true;
}

void SEASON3B::CNewUISlideWindow::Release()
{
    SAFE_DELETE(m_pSlideMgr);

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool SEASON3B::CNewUISlideWindow::UpdateMouseEvent()
{
    return true;
}
bool SEASON3B::CNewUISlideWindow::UpdateKeyEvent()
{
    return true;
}
bool SEASON3B::CNewUISlideWindow::Update()
{
    m_pSlideMgr->ManageSlide();

    return true;
}

float SEASON3B::CNewUISlideWindow::GetLayerDepth()
{
    return 1.91f;
}

//	NewUITextBox.cpp

const int iMAX_TEXT_LINE = 512;
const int iLINE_INTERVAL = 2;

CNewUITextBox::CNewUITextBox(SessionKeeper &keeper) : SessionUiLegacyBindings(keeper)
{
    m_iWidth = 0;
    m_iHeight = 0;
    memset(&m_ptPos, 0, sizeof(POINT));

    m_iTextHeight = 0;
    m_iTextLineHeight = 1;
    m_iLimitLine = 0;

    m_iMaxLine = 0;
    m_iCurLine = 0;
}

CNewUITextBox::~CNewUITextBox()
{
    Release();
}

bool CNewUITextBox::Create(int iX, int iY, int iWidth, int iHeight)
{
    SetPos(iX, iY, iWidth, iHeight);
    Show(true);

    return true;
}

void CNewUITextBox::SetPos(int iX, int iY, int iWidth, int iHeight)
{
    m_ptPos.x = iX;
    m_ptPos.y = iY;
    m_iWidth = iWidth;
    m_iHeight = iHeight;

    SIZE Fontsize;
    g_RenderText.SetFont(LegacyFontRole::Normal);

    std::wstring strTemp = L"A";

    g_RenderText.MeasureText(strTemp.c_str(), strTemp.size(), &Fontsize);

    m_iTextHeight = Fontsize.cy;
    m_iTextLineHeight = m_iTextHeight + iLINE_INTERVAL;

    m_iLimitLine = m_iHeight / m_iTextLineHeight;

    m_iMaxLine = 0;
    m_iCurLine = 0;
}

void CNewUITextBox::Release()
{
}

float CNewUITextBox::GetLayerDepth()
{
    return 4.4f;
}

bool CNewUITextBox::UpdateMouseEvent()
{
    return true;
}

bool CNewUITextBox::UpdateKeyEvent()
{
    return true;
}

bool CNewUITextBox::Update()
{
    return true;
}

void CNewUITextBox::AddText(wchar_t *strText)
{
    wchar_t strTemp[iMAX_TEXT_LINE][iMAX_TEXT_LINE];
    ::memset(strTemp[0], 0, sizeof(char) * iMAX_TEXT_LINE * iMAX_TEXT_LINE);

    int iTextLine = DivideStringByPixel(&strTemp[0][0], iMAX_TEXT_LINE, iMAX_TEXT_LINE, strText,
                                        m_iWidth, true, '#');

    for (int iIndex = 0; iIndex < iTextLine; iIndex++)
    {
        m_vecText.push_back(strTemp[iIndex]);
    }
}

void CNewUITextBox::AddText(const wchar_t *strText)
{
    wchar_t strTempText[iMAX_TEXT_LINE] = {
        0,
    };
    mu_swprintf(strTempText, strText);

    AddText(strTempText);
}

std::wstring CNewUITextBox::GetFullText()
{
    std::wstring strTemp;

    auto vi = m_vecText.begin();
    for (; vi != m_vecText.end(); vi++)
    {
        strTemp += (*vi);
    }

    return strTemp;
}

std::wstring CNewUITextBox::GetLineText(int iLineIndex)
{
    if (0 > iLineIndex || (int)m_vecText.size() <= iLineIndex)
        return L"";

    return m_vecText[iLineIndex];
}

int CNewUITextBox::GetMoveableLine()
{
    int iMoveableLine = m_vecText.size() - m_iLimitLine;
    if (iMoveableLine <= 0)
        iMoveableLine = 0;

    return iMoveableLine;
}

namespace
{
constexpr float kDefaultTextOffset = 0.5f;
constexpr float kPressedTextOffset = 1.5f;

bool IsValidStateIndex(int state)
{
    return state >= 0 && state < BTN_IMG_MAX;
}

int ClampStateIndex(int state)
{
    return IsValidStateIndex(state) ? state : BTN_UP;
}
} // namespace

CButton::CButton(SessionKeeper &keeper)
    : CSprite(keeper), m_pBtnHeld(keeper.InterfaceStorage().heldButton), m_bEnable(true),
      m_bActive(true), m_bClick(false), m_bCheck(false)
{
    m_imageFrames.fill(-1);
}

CButton::~CButton()
{
    Release();
}

void CButton::Release()
{
    ReleaseText();
}

void CButton::Create(int nWidth, int nHeight, int nTexID, int nMaxFrame, int nDownFrame,
                     int nActiveFrame, int nDisableFrame, int nCheckUpFrame, int nCheckDownFrame,
                     int nCheckActiveFrame, int nCheckDisableFrame)
{
    Release();

    const int frameCount = nMaxFrame;
    std::vector<SFrameCoord> frameCoords(static_cast<std::size_t>(frameCount > 0 ? frameCount : 0));
    for (int i = 0; i < frameCount; ++i)
    {
        auto &coord = frameCoords[static_cast<std::size_t>(i)];
        coord.nX = 0;
        coord.nY = nHeight * i;
    }

    CSprite::Create(nWidth, nHeight, nTexID, frameCount, frameCoords.data());
    CSprite::SetAction(0, frameCount - 1);

    m_imageFrames.fill(-1);
    m_imageFrames[BTN_UP] = 0;
    m_imageFrames[BTN_DOWN] = (nDownFrame >= 0) ? nDownFrame : m_imageFrames[BTN_UP];
    m_imageFrames[BTN_ACTIVE] = (nActiveFrame >= 0) ? nActiveFrame : m_imageFrames[BTN_UP];
    m_imageFrames[BTN_DISABLE] = nDisableFrame;

    m_imageFrames[BTN_UP_CHECK] = nCheckUpFrame;
    m_imageFrames[BTN_DOWN_CHECK] =
        (nCheckDownFrame >= 0) ? nCheckDownFrame : m_imageFrames[BTN_UP_CHECK];
    m_imageFrames[BTN_ACTIVE_CHECK] =
        (nCheckActiveFrame >= 0) ? nCheckActiveFrame : m_imageFrames[BTN_UP_CHECK];
    m_imageFrames[BTN_DISABLE_CHECK] = nCheckDisableFrame;

    m_bClick = false;
    m_bCheck = false;
    m_bEnable = true;
    m_bActive = true;
    m_visualState = ButtonVisualState::Up;
}

void CButton::Show(bool bShow)
{
    CSprite::Show(bShow);
    if (!bShow)
    {
        m_bClick = false;
    }
}

BOOL CButton::CursorInObject()
{
    if (!m_bActive)
    {
        return FALSE;
    }

    return CSprite::CursorInObject();
}

void CButton::Update()
{
    if (!CSprite::m_bShow)
    {
        return;
    }

    m_fTextAddYPos = kDefaultTextOffset;

    if (m_bEnable)
    {
        if (CursorInObject() && MouseLButtonPush)
        {
            m_pBtnHeld = this;
        }

        m_bClick = false;

        if (MouseLButtonPop)
        {
            if (CursorInObject() && this == m_pBtnHeld)
            {
                m_bClick = true;
                PlayBuffer(SOUND_CLICK01);

                if (HasCheckVisuals())
                {
                    m_bCheck = !m_bCheck;
                }
            }

            if (this == m_pBtnHeld)
            {
                m_pBtnHeld = nullptr;
            }
        }

        if (CursorInObject() && m_pBtnHeld == nullptr)
        {
            m_visualState = ButtonVisualState::Over;
            ApplyVisualState(m_bCheck ? BTN_ACTIVE_CHECK : BTN_ACTIVE);
        }
        else if (CursorInObject() && this == m_pBtnHeld)
        {
            m_visualState = ButtonVisualState::Down;
            m_fTextAddYPos = kPressedTextOffset;
            ApplyVisualState(m_bCheck ? BTN_DOWN_CHECK : BTN_DOWN);
        }
        else
        {
            m_visualState = ButtonVisualState::Up;
            ApplyVisualState(m_bCheck ? BTN_UP_CHECK : BTN_UP);
        }
    }
    else
    {
        m_bClick = false;
        if (MouseLButtonPop && this == m_pBtnHeld)
        {
            m_pBtnHeld = nullptr;
        }

        m_visualState = ButtonVisualState::Disabled;
        ApplyVisualState(m_bCheck ? BTN_DISABLE_CHECK : BTN_DISABLE);
    }

    // Button animation update is intentionally disabled (legacy behavior kept).
}

void CButton::ReleaseText()
{
    m_text.clear();
    m_textColors.fill(0);
    m_textColorCount = 0;
    m_textColor = 0;
}

void CButton::SetText(const wchar_t *pszText, DWORD *adwColor)
{
    ReleaseText();

    if (pszText == nullptr)
    {
        return;
    }

    m_text.assign(pszText);

    const std::size_t colorCount = HasCheckVisuals() ? BTN_IMG_MAX : BTN_IMG_MAX / 2;
    if (adwColor != nullptr)
    {
        m_textColorCount = colorCount;
        std::copy_n(adwColor, m_textColorCount, m_textColors.begin());
    }

    ApplyTextColorForState(m_bCheck ? BTN_UP_CHECK : BTN_UP);
}

wchar_t *CButton::GetText() const
{
    if (m_text.empty())
    {
        return nullptr;
    }

    m_textBuffer.assign(m_text.begin(), m_text.end());
    m_textBuffer.push_back(L'\0');
    return m_textBuffer.data();
}

void CButton::ApplyVisualState(int frameIndex)
{
    const int clampedState = ClampStateIndex(frameIndex);
    int spriteFrame = m_imageFrames[clampedState];
    if (spriteFrame < 0)
    {
        spriteFrame = m_imageFrames[BTN_UP];
    }

    CSprite::SetNowFrame(spriteFrame);
    ApplyTextColorForState(clampedState);
}

void CButton::ApplyTextColorForState(int colorIndex)
{
    if (m_textColorCount == 0 || m_text.empty())
    {
        return;
    }

    const int resolvedIndex = ResolveColorIndex(colorIndex);
    if (resolvedIndex < 0)
    {
        return;
    }

    const std::size_t index = static_cast<std::size_t>(resolvedIndex);
    m_textColor = m_textColors[index];
}

bool CButton::HasCheckVisuals() const
{
    return m_imageFrames[BTN_UP_CHECK] >= 0;
}

int CButton::ResolveColorIndex(int requestedIndex) const
{
    if (m_textColorCount == BTN_IMG_MAX)
    {
        return ClampStateIndex(requestedIndex);
    }

    if (m_textColorCount == BTN_IMG_MAX / 2)
    {
        return ClampStateIndex(requestedIndex) % (BTN_IMG_MAX / 2);
    }

    if (m_textColorCount == 0)
    {
        return -1;
    }

    const int clamped = ClampStateIndex(requestedIndex);
    return std::min<int>(clamped, static_cast<int>(m_textColorCount - 1));
}

// Desc: implementation of the CGaugeBar class.
// producer: Ahn Sang-Kyu

// Construction/Destruction

CGaugeBar::CGaugeBar(SessionKeeper &keeper)
    : sessionKeeper_(keeper), MouseX(keeper.InterfaceStorage().MouseX),
      MouseY(keeper.InterfaceStorage().MouseY), screenRateX_(keeper.PlatformScreenRateX()),
      screenRateY_(keeper.PlatformScreenRateY()), m_sprGauge(keeper), m_gaugeRect{0, 0, 0, 0}
{
}

CGaugeBar::~CGaugeBar()
{
    Release();
}

void CGaugeBar::Create(int nGaugeWidth, int nGaugeHeight, int nGaugeTexID, const RECT *prcGauge,
                       int nBackWidth, int nBackHeight, int nBackTexID, bool bShortenLeft,
                       float fScaleX, float fScaleY)
{
    Release();

    const int nSizingDatums = bShortenLeft ? SPR_SIZING_DATUMS_LT : SPR_SIZING_DATUMS_RT;

    m_sprGauge.Create(nGaugeWidth, nGaugeHeight, nGaugeTexID, 0, nullptr, 0, 0, true, nSizingDatums,
                      fScaleX, fScaleY);

    if (prcGauge == nullptr)
    {
        m_gaugeRect = Rect{0, 0, nGaugeWidth, nGaugeHeight};
    }
    else
    {
        m_gaugeRect = ConvertRect(*prcGauge);
        m_sprGauge.SetSize(0, m_gaugeRect.bottom - m_gaugeRect.top, Y);
    }

    if (nBackTexID >= 0)
    {
        m_backgroundSprite = std::make_unique<CSprite>(sessionKeeper_);
        m_backgroundSprite->Create(nBackWidth, nBackHeight, nBackTexID, 0, nullptr, 0, 0, false,
                                   SPR_SIZING_DATUMS_LT, fScaleX, fScaleY);
        m_responseSize.reset();
    }
    else if (nBackWidth > 0 && nBackHeight > 0)
    {
        m_responseSize = GaugeSize{nBackWidth, nBackHeight};
        m_backgroundSprite.reset();
    }
    else
    {
        m_responseSize.reset();
        m_backgroundSprite.reset();
    }

    m_nXPos = 0;
    m_nYPos = 0;
}

void CGaugeBar::Release()
{
    m_sprGauge.Release();
    m_backgroundSprite.reset();
    m_responseSize.reset();
}

void CGaugeBar::SetPosition(int nXCoord, int nYCoord)
{
    if (m_backgroundSprite)
    {
        m_backgroundSprite->SetPosition(nXCoord, nYCoord);
    }

    m_sprGauge.SetPosition(nXCoord + m_gaugeRect.left, nYCoord + m_gaugeRect.top);

    m_nXPos = nXCoord;
    m_nYPos = nYCoord;
}

int CGaugeBar::GetWidth() const
{
    if (m_backgroundSprite)
    {
        return m_backgroundSprite->GetWidth();
    }

    if (m_responseSize)
    {
        return m_responseSize->width;
    }

    return m_gaugeRect.right - m_gaugeRect.left;
}

int CGaugeBar::GetHeight() const
{
    if (m_backgroundSprite)
    {
        return m_backgroundSprite->GetHeight();
    }

    if (m_responseSize)
    {
        return m_responseSize->height;
    }

    return m_gaugeRect.bottom - m_gaugeRect.top;
}

void CGaugeBar::SetValue(std::uint32_t dwNow, std::uint32_t dwTotal)
{
    const std::uint32_t safeTotal = std::max<std::uint32_t>(dwTotal, 1u);
    const std::uint32_t clampedNow = std::min<std::uint32_t>(dwNow, safeTotal);
    const int gaugeWidth = m_gaugeRect.right - m_gaugeRect.left;

    int nNowSize =
        static_cast<int>((static_cast<std::uint64_t>(clampedNow) * gaugeWidth) / safeTotal);

    if (IS_SIZING_DATUMS_R(m_sprGauge.GetSizingDatums()))
    {
        nNowSize += m_sprGauge.GetTexWidth() - gaugeWidth;
    }

    m_sprGauge.SetSize(nNowSize, 0, X);
}

bool CGaugeBar::CursorInObject()
{
    if (!IsShow())
    {
        return false;
    }

    if (m_backgroundSprite)
    {
        return m_backgroundSprite->CursorInObject() != FALSE;
    }

    float fScaleX = m_sprGauge.GetScaleX();
    float fScaleY = m_sprGauge.GetScaleY();

    const POINT cursorPos{
        static_cast<LONG>(MouseX * screenRateX_),
        static_cast<LONG>(MouseY * screenRateY_),
    };

    if (m_responseSize)
    {
        Rect scaledRect{0, 0, m_responseSize->width, m_responseSize->height};
        scaledRect = ScaleRect(scaledRect, fScaleX, fScaleY);
        scaledRect = OffsetRect(scaledRect, m_nXPos, m_nYPos);
        return Contains(scaledRect, cursorPos.x, cursorPos.y);
    }

    Rect scaledRect = ScaleRect(m_gaugeRect, fScaleX, fScaleY);
    scaledRect = OffsetRect(scaledRect, m_nXPos, m_nYPos);
    return Contains(scaledRect, cursorPos.x, cursorPos.y);
}

void CGaugeBar::SetAlpha(std::uint8_t byAlpha)
{
    if (m_backgroundSprite)
    {
        m_backgroundSprite->SetAlpha(static_cast<BYTE>(byAlpha));
    }

    m_sprGauge.SetAlpha(static_cast<BYTE>(byAlpha));
}

void CGaugeBar::SetColor(std::uint8_t byRed, std::uint8_t byGreen, std::uint8_t byBlue)
{
    m_sprGauge.SetColor(static_cast<BYTE>(byRed), static_cast<BYTE>(byGreen),
                        static_cast<BYTE>(byBlue));
}

void CGaugeBar::Show(bool bShow)
{
    if (m_backgroundSprite)
    {
        m_backgroundSprite->Show(bShow);
    }
    m_sprGauge.Show(bShow);
}

CGaugeBar::Rect CGaugeBar::ConvertRect(const RECT &source)
{
    return Rect{source.left, source.top, source.right, source.bottom};
}

CGaugeBar::Rect CGaugeBar::ScaleRect(const Rect &rect, float scaleX, float scaleY)
{
    return Rect{static_cast<int>(rect.left * scaleX), static_cast<int>(rect.top * scaleY),
                static_cast<int>(rect.right * scaleX), static_cast<int>(rect.bottom * scaleY)};
}

CGaugeBar::Rect CGaugeBar::OffsetRect(const Rect &rect, int offsetX, int offsetY)
{
    return Rect{rect.left + offsetX, rect.top + offsetY, rect.right + offsetX,
                rect.bottom + offsetY};
}

bool CGaugeBar::Contains(const Rect &rect, long x, long y)
{
    return (x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom);
}

#define SLD_FIRST_SLIDE_DELAY_TIME 500
#define SLD_SLIDE_DELAY_TIME 50

CSlider::CSlider(SessionKeeper &keeper)
    : sessionKeeper_(keeper), MouseX(keeper.InterfaceStorage().MouseX),
      MouseY(keeper.InterfaceStorage().MouseY), screenRateX_(keeper.PlatformScreenRateX()),
      screenRateY_(keeper.PlatformScreenRateY()),
      MouseLButton(keeper.InterfaceStorage().MouseLButton),
      MouseLButtonPop(keeper.InterfaceStorage().MouseLButtonPop), m_btnThumb(keeper),
      m_pGaugeBar(NULL), m_psprBack(NULL)
{
}

CSlider::~CSlider()
{
    SAFE_DELETE(m_pGaugeBar);
    SAFE_DELETE(m_psprBack);
}

void CSlider::Create(SImgInfo *piiThumb, SImgInfo *piiBack, SImgInfo *piiGauge, RECT *prcGauge,
                     bool bVertical)
{
    m_btnThumb.Create(piiThumb->nWidth, piiThumb->nHeight, piiThumb->nTexID);

    SAFE_DELETE(m_pGaugeBar);
    SAFE_DELETE(m_psprBack);

    if (piiGauge)
    {
        m_pGaugeBar = new CGaugeBar(sessionKeeper_);
        m_pGaugeBar->Create(piiGauge->nWidth, piiGauge->nHeight, piiGauge->nTexID, prcGauge,
                            piiBack->nWidth, piiBack->nHeight, piiBack->nTexID);
    }
    else if (-1 < piiBack->nTexID)
    {
        m_psprBack = new CSprite(sessionKeeper_);
        m_psprBack->Create(piiBack);
    }

    m_bVertical = bVertical;
    m_byState = SLD_STATE_IDLE;
    m_nSlidePos = 0;
    m_nSlideRange = 1;
    m_ptPos.x = 0;
    m_ptPos.y = 0;
    m_dThumbMoveTime = 0.0;

    int nThumbRange;
    if (m_bVertical)
    {
        nThumbRange = piiBack->nHeight - m_btnThumb.GetHeight();

        m_Size.cx = m_btnThumb.GetWidth();
        m_Size.cy = piiBack->nHeight;
    }
    else
    {
        nThumbRange = piiBack->nWidth - m_btnThumb.GetWidth();

        m_Size.cx = piiBack->nWidth;
        m_Size.cy = m_btnThumb.GetHeight();
    }
    m_nThumbRange = nThumbRange < 0 ? 0 : nThumbRange;
}

void CSlider::Release()
{
    m_btnThumb.Release();
    SAFE_DELETE(m_pGaugeBar);
    SAFE_DELETE(m_psprBack);
}

void CSlider::SetThumbPosition()
{
    float fThumbPos;
    if (m_bVertical)
    {
        fThumbPos = float(m_ptPos.y) + (float)m_nThumbRange / m_nSlideRange * m_nSlidePos;
        m_btnThumb.SetPosition(m_ptPos.x, (int)fThumbPos);
    }
    else
    {
        fThumbPos = float(m_ptPos.x) + (float)m_nThumbRange / m_nSlideRange * m_nSlidePos;
        m_btnThumb.SetPosition((int)fThumbPos, m_ptPos.y);
        m_pGaugeBar->SetValue(m_nSlidePos, m_nSlideRange);
    }
}

void CSlider::SetPosition(int nXCoord, int nYCoord)
{
    m_ptPos.x = nXCoord;
    m_ptPos.y = nYCoord;

    SetThumbPosition();

    if (m_pGaugeBar)
        m_pGaugeBar->SetPosition(nXCoord, nYCoord);
    else if (m_psprBack)
        m_psprBack->SetPosition(nXCoord, nYCoord);
}

void CSlider::SetSlideRange(int nSlideRange)
{
    m_nSlideRange = MAX(nSlideRange, 1);

    SetThumbPosition();
}

void CSlider::LineUp()
{
    if (m_bVertical)
    {
        float fThumbYPos = float(m_ptPos.y) + (float)m_nThumbRange / m_nSlideRange * --m_nSlidePos;
        m_btnThumb.SetPosition(m_ptPos.x, (int)fThumbYPos);
    }
    else
    {
        float fThumbXPos = float(m_ptPos.x) + (float)m_nThumbRange / m_nSlideRange * --m_nSlidePos;
        m_btnThumb.SetPosition((int)fThumbXPos, m_ptPos.y);
        m_pGaugeBar->SetValue(m_nSlidePos, m_nSlideRange);
    }
}

void CSlider::LineDown()
{
    float fThumbPos;

    if (m_bVertical)
    {
        if (++m_nSlidePos == m_nSlideRange)
            fThumbPos = float(m_ptPos.y + m_nThumbRange);
        else
            fThumbPos = float(m_ptPos.y) + (float)m_nThumbRange / m_nSlideRange * m_nSlidePos;

        m_btnThumb.SetPosition(m_ptPos.x, (int)fThumbPos);
    }
    else
    {
        if (++m_nSlidePos == m_nSlideRange)
            fThumbPos = float(m_ptPos.x + m_nThumbRange);
        else
            fThumbPos = float(m_ptPos.x) + (float)m_nThumbRange / m_nSlideRange * m_nSlidePos;

        m_btnThumb.SetPosition((int)fThumbPos, m_ptPos.y);
        m_pGaugeBar->SetValue(m_nSlidePos, m_nSlideRange);
    }
}

void CSlider::Update(double dDeltaTick)
{
    if (!m_btnThumb.IsShow())
        return;

    m_btnThumb.Update();

    if (!m_btnThumb.IsEnable())
        return;

    if (MouseLButtonPop)
    {
        SetThumbPosition();
        m_byState = SLD_STATE_IDLE;
    }

    RECT rcUpperThumb, rcUnderThumb;
    if (m_bVertical)
    {
        ::SetRect(&rcUpperThumb, m_ptPos.x, m_ptPos.y, m_ptPos.x + m_Size.cx, m_btnThumb.GetYPos());
        ::SetRect(&rcUnderThumb, m_ptPos.x, m_btnThumb.GetYPos() + m_btnThumb.GetHeight(),
                  m_ptPos.x + m_Size.cx, m_ptPos.y + m_Size.cy);
    }
    else
    {
        ::SetRect(&rcUpperThumb, m_ptPos.x, m_ptPos.y, m_btnThumb.GetXPos(), m_ptPos.y + m_Size.cy);
        ::SetRect(&rcUnderThumb, m_btnThumb.GetXPos() + m_btnThumb.GetWidth(), m_ptPos.y,
                  m_ptPos.x + m_Size.cx, m_ptPos.y + m_Size.cy);
    }

    POINT ptCursor{
        static_cast<LONG>(MouseX * screenRateX_),
        static_cast<LONG>(MouseY * screenRateY_),
    };

    if (::PtInRect(&rcUpperThumb, ptCursor))
    {
        m_dThumbMoveStartTime += dDeltaTick;
        if (MouseLButton)
        {
            LineUp();
            m_byState |= SLD_STATE_UP;
            m_dThumbMoveStartTime = 0.0;
        }

        if (m_dThumbMoveStartTime >= SLD_FIRST_SLIDE_DELAY_TIME && m_byState & SLD_STATE_UP)
        {
            m_dThumbMoveTime += dDeltaTick;

            if (m_dThumbMoveTime >= SLD_SLIDE_DELAY_TIME)
            {
                LineUp();
                m_dThumbMoveTime = 0.0;
            }
        }
    }
    else if (::PtInRect(&rcUnderThumb, ptCursor))
    {
        m_dThumbMoveStartTime += dDeltaTick;
        if (MouseLButton)
        {
            LineDown();
            m_byState |= SLD_STATE_DN;
            m_dThumbMoveStartTime = 0.0;
        }

        if (m_dThumbMoveStartTime >= SLD_FIRST_SLIDE_DELAY_TIME && m_byState & SLD_STATE_DN)
        {
            m_dThumbMoveTime += dDeltaTick;

            if (m_dThumbMoveTime >= SLD_SLIDE_DELAY_TIME)
            {
                LineDown();
                m_dThumbMoveTime = 0.0;
            }
        }
    }

    if (m_btnThumb.CursorInObject() && MouseLButton)
    {
        if (m_bVertical)
        {
            m_nCapturePos = ptCursor.y;
            m_nLimitPos = m_ptPos.y + m_nCapturePos - m_btnThumb.GetYPos();
        }
        else
        {
            m_nCapturePos = ptCursor.x;
            m_nLimitPos = m_ptPos.x + m_nCapturePos - m_btnThumb.GetXPos();
        }
        m_byState |= SLD_STATE_THUMB_DRG;
    }

    if (m_byState & SLD_STATE_THUMB_DRG)
    {
        int nThumbPos;
        if (m_bVertical)
        {
            if (m_nLimitPos < ptCursor.y && m_nLimitPos + m_nThumbRange > ptCursor.y)
            {
                nThumbPos = m_btnThumb.GetYPos() + ptCursor.y - m_nCapturePos;
                m_nCapturePos = ptCursor.y;
            }
            else if (m_nLimitPos >= ptCursor.y)
            {
                nThumbPos = m_ptPos.y;
                m_nCapturePos = m_nLimitPos;
            }
            else
            {
                nThumbPos = m_ptPos.y + m_nThumbRange;
                m_nCapturePos = m_nLimitPos + m_nThumbRange;
            }
            m_btnThumb.SetPosition(m_btnThumb.GetXPos(), nThumbPos);
            //			m_btnThumb.SetAction(BTN_HIGHLIGHT_DOWN, BTN_HIGHLIGHT_DOWN);

            // m_nSlidePos ���ϱ�.
            float fPixelPerPos = (float)m_nThumbRange / m_nSlideRange;
            m_nSlidePos =
                int((float(m_btnThumb.GetYPos() - m_ptPos.y) + (fPixelPerPos / 2)) / fPixelPerPos);
        }
        else
        {
            if (m_nLimitPos < ptCursor.x && m_nLimitPos + m_nThumbRange > ptCursor.x)
            {
                nThumbPos = m_btnThumb.GetXPos() + ptCursor.x - m_nCapturePos;
                m_nCapturePos = ptCursor.x;
            }
            else if (m_nLimitPos >= ptCursor.x)
            {
                nThumbPos = m_ptPos.x;
                m_nCapturePos = m_nLimitPos;
            }
            else
            {
                nThumbPos = m_ptPos.x + m_nThumbRange;
                m_nCapturePos = m_nLimitPos + m_nThumbRange;
            }
            m_btnThumb.SetPosition(nThumbPos, m_btnThumb.GetYPos());
            //			m_btnThumb.SetAction(BTN_HIGHLIGHT_DOWN, BTN_HIGHLIGHT_DOWN);

            float fPixelPerPos = (float)m_nThumbRange / m_nSlideRange;
            m_nSlidePos =
                int((float(m_btnThumb.GetXPos() - m_ptPos.x) + (fPixelPerPos / 2)) / fPixelPerPos);
            m_pGaugeBar->SetValue(m_nSlidePos, m_nSlideRange);
        } // if (m_bVertical) else�� ��.
    }
}

void CSlider::SetEnable(bool bEnable)
{
    m_btnThumb.SetEnable(bEnable);

    if (!bEnable)
        m_byState = SLD_STATE_IDLE;
}

void CSlider::Show(bool bShow)
{
    m_btnThumb.Show(bShow);
    if (m_pGaugeBar)
        m_pGaugeBar->Show(bShow);
    else if (m_psprBack)
        m_psprBack->Show(bShow);
}

BOOL CSlider::CursorInObject()
{
    if (m_btnThumb.IsShow())
    {
        RECT rcSlider = {m_ptPos.x, m_ptPos.y, m_ptPos.x + m_Size.cx, m_ptPos.y + m_Size.cy};
        const POINT cursor{
            static_cast<LONG>(MouseX * screenRateX_),
            static_cast<LONG>(MouseY * screenRateY_),
        };
        return ::PtInRect(&rcSlider, cursor);
    }

    return FALSE;
}

void CSlider::SetSlidePos(int nSlidePos)
{
    m_nSlidePos = LIMIT(nSlidePos, 0, m_nSlideRange);
    SetThumbPosition();
}

CWin::CWin(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), sessionKeeper_(keeper),
      legacyUiManager_(keeper.LegacyUiManagerForConstruction()), m_psprBg(NULL)
{
}

CWin::~CWin()
{
    Release();
}

void CWin::Create(int nWidth, int nHeight, int nTexID, bool bTile)
{
    Release();

    if (-2 < nTexID)
    {
        m_ownedBg = std::make_unique<CSprite>(sessionKeeper_);
        m_psprBg = m_ownedBg.get();
        m_psprBg->Create(nWidth, nHeight, nTexID, 0, NULL, 0, 0, bTile);
        if (-1 == nTexID)
        {
            m_psprBg->SetAlpha(128);
            m_psprBg->SetColor(0, 0, 0);
        }
    }

    m_ptPos.x = m_ptPos.y = 0;
    m_ptHeld = m_ptTemp = m_ptPos;
    m_Size.cx = nWidth;
    m_Size.cy = nHeight;
    m_bDocking = m_bActive = m_bShow = false;
    m_nState = WS_NORMAL;
}

void CWin::Release()
{
    PreRelease();

    CButton *pBtn;
    while (m_BtnList.GetCount())
    {
        pBtn = (CButton *)m_BtnList.RemoveHead();
        pBtn->Release();
    }

    m_ownedBg.reset();
    m_psprBg = nullptr;
}

void CWin::SetPosition(int nXCoord, int nYCoord)
{
    m_ptPos.x = nXCoord;
    m_ptPos.y = nYCoord;
    if (m_psprBg)
        m_psprBg->SetPosition(nXCoord, nYCoord);
}

void CWin::SetSize(int nWidth, int nHeight, CHANGE_PRAM eChangedPram)
{
    if (eChangedPram & X)
        m_Size.cx = nWidth;
    if (eChangedPram & Y)
        m_Size.cy = nHeight;

    if (m_psprBg)
        m_psprBg->SetSize(nWidth, nHeight, eChangedPram);
}

bool CWin::CursorInWin(int nArea)
{
    if (!m_bShow)
        return false;

    const POINT cursor{
        static_cast<LONG>(MouseX * g_fScreenRate_x),
        static_cast<LONG>(MouseY * g_fScreenRate_y),
    };
    RECT rc = {0, 0, 0, 0};
    NODE *position;
    CButton *pBtn;

    switch (nArea)
    {
    case WA_ALL:
        ::SetRect(&rc, m_ptPos.x, m_ptPos.y, m_ptPos.x + m_Size.cx, m_ptPos.y + m_Size.cy);
        if (::PtInRect(&rc, cursor))
            return true;
        break;

    case WA_MOVE:
        ::SetRect(&rc, m_ptPos.x, m_ptPos.y, m_ptPos.x + m_Size.cx, m_ptPos.y + 26);
        if (::PtInRect(&rc, cursor))
            return true;
        break;
    case WA_BUTTON:
        position = m_BtnList.GetHeadPosition();
        while (position)
        {
            pBtn = (CButton *)m_BtnList.GetNext(position);
            if (pBtn->CursorInObject())
                return true;
        }
        if (CursorInButtonlike())
            return true;
        break;
    }

    return false;
}

void CWin::ActiveBtns(bool bActive)
{
    CButton *pBtn;
    NODE *position = m_BtnList.GetHeadPosition();
    while (position)
    {
        pBtn = (CButton *)m_BtnList.GetNext(position);
        pBtn->SetActive(bActive);
    }
}

void CWin::Show(bool bShow)
{
    if (m_psprBg)
        m_psprBg->Show(bShow);
    m_bShow = bShow;
    if (!m_bShow)
        m_bActive = false;
}

void CWin::Update(double dDeltaTick)
{
    if (!m_bShow)
        return;

    const POINT cursor{
        static_cast<LONG>(MouseX * g_fScreenRate_x),
        static_cast<LONG>(MouseY * g_fScreenRate_y),
    };

    if (MouseLButtonPop)
        m_nState = WS_NORMAL;

    if (m_nState == WS_NORMAL)
    {
        CButton *pBtn;
        NODE *position = m_BtnList.GetHeadPosition();
        while (position)
        {
            pBtn = (CButton *)m_BtnList.GetNext(position);
            pBtn->Update();
        }
    }

    UpdateWhileShow(dDeltaTick);

    if (!m_bActive)
        return;

    if (MouseLButtonPush)
    {
        if (CursorInWin(WA_MOVE))
        {
            m_ptHeld = cursor;
            m_ptTemp = m_ptPos;
            m_nState = WS_MOVE;
        }
    }

    if (WS_MOVE == m_nState)
    {
        m_ptTemp.x += cursor.x - m_ptHeld.x;
        m_ptTemp.y += cursor.y - m_ptHeld.y;
        if (!m_bDocking)
            SetPosition(m_ptTemp.x, m_ptTemp.y);
        m_ptHeld = cursor;
    }
    CheckAdditionalState();
    UpdateWhileActive(dDeltaTick);
}

void CWin::RegisterButton(CButton *pBtn)
{
    m_BtnList.AddTail(pBtn);
}

#define WE_CENTER_SPR_POS 3

CWinEx::CWinEx(SessionKeeper &keeper) : CWin(keeper), m_backgroundSprites(keeper)
{
}

CWinEx::~CWinEx()
{
    Release();
}

void CWinEx::Create(SImgInfo *aImgInfo, int nBgSideMin, int nBgSideMax)
{
    Release();

    CWin::m_psprBg = m_backgroundSprites.data();

    CWin::m_psprBg[WE_BG_CENTER].Create(aImgInfo, 0, 0, true);
    CWin::m_psprBg[WE_BG_TOP].Create(aImgInfo + 1);
    CWin::m_psprBg[WE_BG_BOTTOM].Create(aImgInfo + 2);
    CWin::m_psprBg[WE_BG_LEFT].Create(aImgInfo + 3, 0, 0, true);
    CWin::m_psprBg[WE_BG_RIGHT].Create(aImgInfo + 4, 0, 0, true);

    CWin::m_psprBg[WE_BG_CENTER].SetSize(
        CWin::m_psprBg[WE_BG_TOP].GetWidth() - WE_CENTER_SPR_POS * 2, 0, X);

    CWin::m_ptPos.x = CWin::m_ptPos.y = 0;
    CWin::m_ptHeld = CWin::m_ptTemp = CWin::m_ptPos;
    CWin::m_bDocking = CWin::m_bActive = CWin::m_bShow = false;
    CWin::m_nState = WS_NORMAL;
    CWin::m_Size.cx = CWin::m_psprBg[WE_BG_TOP].GetWidth();
    CWin::m_Size.cy = CWin::m_psprBg[WE_BG_TOP].GetHeight() +
                      CWin::m_psprBg[WE_BG_BOTTOM].GetHeight() +
                      CWin::m_psprBg[WE_BG_LEFT].GetHeight();

    m_nBgSideNow = m_nBgSideMin = nBgSideMin;
    m_nBgSideMax = nBgSideMax;
}

void CWinEx::Release()
{
    PreRelease();
    CButton *pBtn;
    while (CWin::m_BtnList.GetCount())
    {
        pBtn = (CButton *)CWin::m_BtnList.RemoveHead();
        pBtn->Release();
    }

    for (CSprite &sprite : m_backgroundSprites)
        sprite.Release();
    CWin::m_psprBg = nullptr;
}

void CWinEx::SetPosition(int nXCoord, int nYCoord)
{
    CWin::m_psprBg[WE_BG_TOP].SetPosition(nXCoord, nYCoord);

    CWin::m_psprBg[WE_BG_CENTER].SetPosition(nXCoord + WE_CENTER_SPR_POS,
                                             nYCoord + WE_CENTER_SPR_POS);

    CWin::m_psprBg[WE_BG_LEFT].SetPosition(nXCoord,
                                           nYCoord + CWin::m_psprBg[WE_BG_TOP].GetHeight());

    CWin::m_psprBg[WE_BG_RIGHT].SetPosition(nXCoord + CWin::m_psprBg[WE_BG_TOP].GetWidth() -
                                                CWin::m_psprBg[WE_BG_RIGHT].GetWidth(),
                                            CWin::m_psprBg[WE_BG_LEFT].GetYPos());

    CWin::m_psprBg[WE_BG_BOTTOM].SetPosition(nXCoord, CWin::m_psprBg[WE_BG_LEFT].GetYPos() +
                                                          CWin::m_psprBg[WE_BG_LEFT].GetHeight());

    m_ptPos.x = nXCoord;
    m_ptPos.y = nYCoord;
}

int CWinEx::SetLine(int nLine)
{
    nLine = LIMIT(nLine, m_nBgSideMin, m_nBgSideMax);

    if (m_nBgSideNow == nLine)
        return m_nBgSideNow;

    int nOldLine = m_nBgSideNow;
    m_nBgSideNow = nLine;

    int nBgSideHeight = CWin::m_psprBg[WE_BG_LEFT].GetTexHeight() * m_nBgSideNow;

    CWin::m_psprBg[WE_BG_LEFT].SetSize(0, nBgSideHeight, Y);
    CWin::m_psprBg[WE_BG_RIGHT].SetSize(0, nBgSideHeight, Y);

    CWin::m_psprBg[WE_BG_BOTTOM].SetPosition(
        0, CWin::m_psprBg[WE_BG_LEFT].GetYPos() + CWin::m_psprBg[WE_BG_LEFT].GetHeight(), Y);

    CWin::m_Size.cy = CWin::m_psprBg[WE_BG_TOP].GetHeight() +
                      CWin::m_psprBg[WE_BG_BOTTOM].GetHeight() + nBgSideHeight;

    CWin::m_psprBg[WE_BG_CENTER].SetSize(0, CWin::m_Size.cy - WE_CENTER_SPR_POS * 2, Y);

    return nOldLine;
}

void CWinEx::SetSize(int nHeight)
{
    int nLine = (nHeight - CWin::m_psprBg[WE_BG_TOP].GetHeight() -
                 CWin::m_psprBg[WE_BG_BOTTOM].GetHeight()) /
                CWin::m_psprBg[WE_BG_LEFT].GetTexHeight();

    SetLine(nLine);
}

bool CWinEx::CursorInWin(int nArea)
{
    if (!CWin::m_bShow)
        return false;

    const POINT cursor{
        static_cast<LONG>(MouseX * g_fScreenRate_x),
        static_cast<LONG>(MouseY * g_fScreenRate_y),
    };
    RECT rc = {0, 0, 0, 0};

    switch (nArea)
    {
    case WA_EXTEND_DN:
        ::SetRect(&rc, CWin::m_ptPos.x, CWin::m_ptPos.y + CWin::m_Size.cy - 5,
                  CWin::m_ptPos.x + CWin::m_Size.cx, CWin::m_ptPos.y + CWin::m_Size.cy);
        if (::PtInRect(&rc, cursor))
            return true;
        break;

    case WA_EXTEND_UP:
        ::SetRect(&rc, CWin::m_ptPos.x, CWin::m_ptPos.y, CWin::m_ptPos.x + CWin::m_Size.cx,
                  CWin::m_ptPos.y + 4);
        if (::PtInRect(&rc, cursor))
            return true;
        break;
    }

    return CWin::CursorInWin(nArea);
}

void CWinEx::Show(bool bShow)
{
    for (int i = 0; i < WE_BG_MAX; ++i)
        CWin::m_psprBg[i].Show(bShow);

    CWin::m_bShow = bShow;
    if (!CWin::m_bShow)
        CWin::m_bActive = false;
}

void CWinEx::CheckAdditionalState()
{
    const int cursorY = static_cast<int>(MouseY * g_fScreenRate_y);

    if (MouseLButtonPush)
    {
        if (CursorInWin(WA_EXTEND_UP))
        {
            m_nBasisY = CWin::m_ptPos.y + CWin::m_psprBg[WE_BG_LEFT].GetTexHeight() * m_nBgSideNow;
            CWin::m_nState = WS_EXTEND_UP;
        }

        if (CursorInWin(WA_EXTEND_DN))
        {
            m_nBasisY = CWin::m_ptPos.y + CWin::m_psprBg[WE_BG_TOP].GetHeight() +
                        CWin::m_psprBg[WE_BG_BOTTOM].GetHeight();
            CWin::m_nState = WS_EXTEND_DN;
        }
    }

    int nBgSideHeight;
    switch (CWin::m_nState)
    {
    case WS_EXTEND_UP:
        nBgSideHeight = m_nBasisY - cursorY;
        if (nBgSideHeight < CWin::m_psprBg[WE_BG_LEFT].GetTexHeight() * m_nBgSideMin)
            SetLine(m_nBgSideMin);
        else
            SetLine(nBgSideHeight / CWin::m_psprBg[WE_BG_LEFT].GetTexHeight() + 1);

        SetPosition(CWin::m_ptPos.x,
                    m_nBasisY - CWin::m_psprBg[WE_BG_LEFT].GetTexHeight() * m_nBgSideNow);

        break;

    case WS_EXTEND_DN:
        nBgSideHeight = cursorY - m_nBasisY;
        if (nBgSideHeight < CWin::m_psprBg[WE_BG_LEFT].GetTexHeight() * m_nBgSideMin)
            SetLine(m_nBgSideMin);
        else
            SetLine(nBgSideHeight / CWin::m_psprBg[WE_BG_LEFT].GetTexHeight() + 1);

        break;
    }
}

void RenderColor(float x, float y, float Width, float Height, float Alpha, int Flag);

CUIBaseWindow::CUIBaseWindow(SessionKeeper &keeper) : CUIControl(keeper)
{
    m_WorkMessage = {};
    m_iMouseClickPos_x = 0;
    m_iMouseClickPos_y = 0;
    m_iMinWidth = 100;
    m_iMinHeight = 100;
    m_iMaxWidth = 0;
    m_iMaxHeight = 0;
    SetOption(UIWINDOWSTYLE_NORMAL);
    m_bHaveTextBox = FALSE;
    m_iControlButtonClick = 0;
    m_bIsMaximize = FALSE;
    m_iBackPos_y = 0;
    m_iBackHeight = 0;
    m_iResizeDir = 0;
}

CUIBaseWindow::~CUIBaseWindow()
{
}

void CUIBaseWindow::Init(const wchar_t *pszTitle, DWORD dwParentID)
{
    SetTitle(pszTitle);
    SetParentUIID(dwParentID);

    SetPosition(50, 50);
    SetSize(213, 170);
}

void CUIBaseWindow::SetTitle(const wchar_t *pszTitle)
{
    if (!pszTitle)
        return;

    m_strTitle = std::wstring(pszTitle);
}

BOOL CUIBaseWindow::DoMouseAction()
{
    m_iControlButtonClick = 0;
    if (CheckMouseIn(m_iPos_x, m_iPos_y, m_iWidth, m_iHeight))
    {
        if (CheckOption(UIWINDOWSTYLE_TITLEBAR) &&
            CheckMouseIn(m_iPos_x + m_iWidth - 16, m_iPos_y + 8, 9, 9))
        {
            m_iControlButtonClick = 3;
            if (MouseOnWindow == false && MouseLButtonPop == true)
            {
                if (CloseCheck() == TRUE)
                {
                    g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, GetUIID(), 0);
                }
                MouseLButtonPop = false;
            }
        }
        else if (CheckOption(UIWINDOWSTYLE_MAXBUTTON) &&
                 CheckMouseIn(m_iPos_x + m_iWidth - 27, m_iPos_y + 8, 9, 9))
        {
            m_iControlButtonClick = 2;
            if (MouseOnWindow == false && MouseLButtonPop == true)
            {
                g_pWindowMgr->SendUIMessage(UI_MESSAGE_MAXIMIZE, GetUIID(), 0);
                MouseLButtonPop = false;
            }
        }
        else if (CheckOption(UIWINDOWSTYLE_MINBUTTON) &&
                 CheckMouseIn(m_iPos_x + m_iWidth -
                                  (CheckOption(UIWINDOWSTYLE_MAXBUTTON) ? 38 : 27),
                              m_iPos_y + 8, 9, 9))
        {
            m_iControlButtonClick = 1;
            if (MouseOnWindow == false && MouseLButtonPop == true)
            {
                g_pWindowMgr->SendUIMessage(UI_MESSAGE_HIDE, GetUIID(), 0);
                MouseLButtonPop = false;
            }
        }
        else if (CheckOption(UIWINDOWSTYLE_RESIZEABLE) && CheckMouseIn(m_iPos_x, m_iPos_y, 7, 7))
        {
            if (MouseLButton == true && GetState() != UISTATE_RESIZE && g_dwActiveUIID == 0)
            {
                g_dwActiveUIID = GetUIID();
                SetState(UISTATE_RESIZE);
                m_iResizeDir = 315;
                m_iMouseClickPos_x = MouseX;
                m_iMouseClickPos_y = MouseY;
                m_bIsMaximize = FALSE;
            }
        }
        else if (CheckOption(UIWINDOWSTYLE_MOVEABLE) && CheckOption(UIWINDOWSTYLE_TITLEBAR) &&
                 CheckMouseIn(m_iPos_x, m_iPos_y, m_iWidth, 20))
        {
            if (MouseLButton == true && GetState() != UISTATE_MOVE && g_dwActiveUIID == 0)
            {
                g_dwActiveUIID = GetUIID();
                SetState(UISTATE_MOVE);
                m_iMouseClickPos_x = MouseX;
                m_iMouseClickPos_y = MouseY;
            }
        }
        else if (CheckOption(UIWINDOWSTYLE_RESIZEABLE) &&
                 CheckMouseIn(m_iPos_x + m_iWidth - 10, m_iPos_y + m_iHeight - 10, 10, 10))
        {
            if (MouseLButton == true && GetState() != UISTATE_RESIZE && g_dwActiveUIID == 0)
            {
                g_dwActiveUIID = GetUIID();
                SetState(UISTATE_RESIZE);
                m_iResizeDir = 135;
                m_iMouseClickPos_x = MouseX;
                m_iMouseClickPos_y = MouseY;
                m_bIsMaximize = FALSE;
            }
        }
        else if (g_dwActiveUIID == GetUIID() && GetState() != UISTATE_MOVE &&
                 GetState() != UISTATE_RESIZE)
        {
            MouseLButton = false;
            MouseLButtonPush = false;
        }
        MouseOnWindow = true;
    }
    if (GetState() == UISTATE_MOVE)
    {
        if (MouseLButton == true)
        {
            if (g_dwMouseUseUIID == 0)
                g_dwMouseUseUIID = GetUIID();
            MouseOnWindow = true;

            if (m_iPos_x + MouseX - m_iMouseClickPos_x < 0)
                m_iPos_x = 0;
            else if (m_iPos_x + m_iWidth + MouseX - m_iMouseClickPos_x > REFERENCE_WIDTH)
                m_iPos_x = REFERENCE_WIDTH - m_iWidth;
            else
                m_iPos_x += MouseX - m_iMouseClickPos_x;

            if (m_iPos_y + MouseY - m_iMouseClickPos_y < 0)
            {
                m_iPos_y = 0;
            }
            else if (m_iPos_y + m_iHeight + MouseY - m_iMouseClickPos_y > REFERENCE_HEIGHT)
            {
                m_iPos_y = REFERENCE_HEIGHT - m_iHeight;
            }
            else
            {
                m_iPos_y += MouseY - m_iMouseClickPos_y;
            }

            m_iMouseClickPos_x = MouseX;
            m_iMouseClickPos_y = MouseY;
        }
        else
        {
            SetState(UISTATE_NORMAL);
            if (g_dwActiveUIID == GetUIID())
                g_dwActiveUIID = 0;
        }
    }
    else if (GetState() == UISTATE_RESIZE)
    {
        if (MouseLButton == true)
        {
            if (m_iResizeDir == 135)
            {
                if (g_dwMouseUseUIID == 0)
                    g_dwMouseUseUIID = GetUIID();
                MouseOnWindow = true;

                if (m_iPos_x + m_iWidth + MouseX - m_iMouseClickPos_x > REFERENCE_WIDTH)
                    m_iWidth = REFERENCE_WIDTH - m_iPos_x;
                else
                    m_iWidth += MouseX - m_iMouseClickPos_x;

                if (m_iWidth < m_iMinWidth)
                {
                    m_iWidth = m_iMinWidth;
                }
                else if (m_iMaxWidth > 0 && m_iWidth > m_iMaxWidth)
                {
                    m_iWidth = m_iMaxWidth;
                }
                else
                    m_iMouseClickPos_x = MouseX;

                if (m_iPos_y + m_iHeight + MouseY - m_iMouseClickPos_y > REFERENCE_HEIGHT)
                    m_iHeight = REFERENCE_HEIGHT - m_iPos_y;
                else
                    m_iHeight += MouseY - m_iMouseClickPos_y;

                if (m_iHeight < m_iMinHeight)
                {
                    m_iHeight = m_iMinHeight;
                }
                else if (m_iMaxHeight > 0 && m_iHeight > m_iMaxHeight)
                {
                    m_iHeight = m_iMaxHeight;
                }
                else
                    m_iMouseClickPos_y = MouseY;
            }
            else if (m_iResizeDir == 315)
            {
                if (g_dwMouseUseUIID == 0)
                    g_dwMouseUseUIID = GetUIID();
                MouseOnWindow = true;

                if (m_iWidth + m_iMouseClickPos_x - MouseX >= m_iMinWidth &&
                    m_iPos_x - (m_iMouseClickPos_x - MouseX) >= 0)
                {
                    m_iPos_x -= m_iMouseClickPos_x - MouseX;
                    m_iWidth += m_iMouseClickPos_x - MouseX;
                }
                m_iMouseClickPos_x = MouseX;

                if (m_iHeight + m_iMouseClickPos_y - MouseY >= m_iMinHeight &&
                    m_iPos_y - (m_iMouseClickPos_y - MouseY) >= 0)
                {
                    m_iPos_y -= m_iMouseClickPos_y - MouseY;
                    m_iHeight += m_iMouseClickPos_y - MouseY;
                }
                m_iMouseClickPos_y = MouseY;
            }
        }
        else
        {
            SetState(UISTATE_NORMAL);
            if (g_dwActiveUIID == GetUIID())
                g_dwActiveUIID = 0;
        }
    }
    DoMouseActionSub();

    return TRUE;
}

void CUIBaseWindow::Maximize()
{
    if (m_bIsMaximize == FALSE)
    {
        m_iBackPos_y = m_iPos_y;
        m_iBackHeight = m_iHeight;
        m_iPos_y = 0;
        m_iHeight = REFERENCE_HEIGHT - 48;
        Refresh();
        m_bIsMaximize = TRUE;
    }
    else
    {
        m_iPos_y = m_iBackPos_y;
        m_iHeight = m_iBackHeight;
        Refresh();
        m_bIsMaximize = FALSE;
    }
}

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

CUITextInputBox *CreateSessionTextInputBox(SessionKeeper &keeper)
{
    return new CUITextInputBox(keeper);
}

CUIMercenaryInputBox *CreateSessionMercenaryInputBox(SessionKeeper &keeper)
{
    return new CUIMercenaryInputBox(keeper);
}

int SessionUiUnit::CutStr(const wchar_t *pszSrcText, wchar_t *pTextOut, const int iTargetPixelWidth,
                          const int iMaxOutLine, const int iOutStrLength, const int iFirstLineTab)
{
    if (iFirstLineTab < 0)
    {
        return 0;
    }

    if (pszSrcText == nullptr)
    {
        assert(!"CutStr Error");
        return 0;
    }

    auto tempString = std::wstring(pszSrcText);
    int iCharIndex = 0, iLineIndex = 0;
    const int iScreenRatePixelWidth = iTargetPixelWidth * g_fScreenRate_x - 5;

    const int totalCharacters = tempString.length();
    int processedSourceCharacters = 0;
    SIZE iSize;
    while (!tempString.empty() && iLineIndex < iMaxOutLine)
    {
        g_RenderText.MeasureText(tempString.c_str(), tempString.length(), &iSize);

        if (iLineIndex == 0)
            iSize.cx += iFirstLineTab;

        const auto isTooWideInPixels = iSize.cx >= iScreenRatePixelWidth;
        const auto isTooLongInCharacters = (int)tempString.length() >= iOutStrLength - 1;
        if (isTooWideInPixels || isTooLongInCharacters)
        {
            // then remove the last word/token from the string and try next loop iteration again ...
            const auto iPosLastSpace = tempString.find_last_of(L' ');
            iCharIndex =
                (iPosLastSpace == std::wstring::npos) ? tempString.length() - 1 : iPosLastSpace;
            tempString = tempString.substr(0, iCharIndex);
        }
        else
        {
            // we can copy that to the destination
            tempString.copy(pTextOut, tempString.length(), 0);
            iLineIndex++;
            processedSourceCharacters += tempString.length();

            pTextOut += iOutStrLength; // move destination pointer to the next line
            if (processedSourceCharacters < totalCharacters)
            {
                tempString = std::wstring(pszSrcText + processedSourceCharacters);
            }
            else
            {
                tempString = L"";
                break;
            }
        }
    }

    return iLineIndex;
}

int SessionUiUnit::CutText3(const wchar_t *pszText, wchar_t *pTextOut, const int TargetWidth,
                            const int iMaxOutLine, const int iOutStrLength, const int iFirstLineTab,
                            const BOOL bReverseWrite)
{
    return CutStr(pszText, pTextOut, TargetWidth, iMaxOutLine, iOutStrLength, iFirstLineTab);
}

void CutText4(const wchar_t *pszSource, wchar_t *pszResult1, wchar_t *pszResult2, int iCutCount)
{
    if (pszSource == nullptr || pszSource[0] == '\0')
        return;
    auto sourceString = std::wstring(pszSource);
    int iLength = sourceString.length();
    int iMove = 2; // might be 4, too
    int iTextSize = 0;
    for (int i = 0; i < iLength;)
    {
        if (i + iMove > iCutCount)
            break;
        else
            i += iMove;

        iTextSize = i;
    }
    wcsncpy(pszResult1, pszSource, iTextSize);
    pszResult1[iTextSize] = '\0';
    if (pszResult2 != nullptr)
    {
        wcsncpy(pszResult2, pszSource + iTextSize, iLength - iTextSize);
        pszResult2[iLength - iTextSize] = '\0';
    }
}

void CUIMessage::SendUIMessage(int iMessage, LONG_PTR iParam1, LONG_PTR iParam2, std::wstring text)
{
    UI_MESSAGE tempmsg{};
    tempmsg.m_iMessage = iMessage;
    tempmsg.m_iParam1 = iParam1;
    tempmsg.m_iParam2 = iParam2;
    tempmsg.m_Text = std::move(text);

    m_MessageList.push_back(std::move(tempmsg));
}

void CUIMessage::GetUIMessage()
{
    if (m_MessageList.empty())
        return;

    m_WorkMessage = std::move(m_MessageList.front());
    m_MessageList.pop_front();
}

CUIControl::CUIControl(SessionKeeper &keeper) : SessionUiLegacyBindings(keeper)
{
    m_dwUIID = CreateUIID();
    m_dwParentUIID = 0;
    SetState(0);
    m_iOptions = 0;
    SetPosition(0, 0);
    SetSize(100, 100);
    SetArrangeType();
    SetResizeType();
    m_iCoordType = COORDINATE_TYPE_LEFT_TOP;
}

void CUIControl::SetState(int iState)
{
    m_iState = iState;
}

int CUIControl::GetState()
{
    return m_iState;
}

void CUIControl::SetPosition(int iPos_x, int iPos_y)
{
    m_iPos_x = iPos_x;
    m_iPos_y = iPos_y;
}

void CUIControl::SetSize(int iWidth, int iHeight)
{
    m_iWidth = iWidth;
    m_iHeight = iHeight;
}

void CUIControl::SetArrangeType(int iArrangeType, int iRelativePos_x, int iRelativePos_y)
{
    m_iArrangeType = iArrangeType;
    m_iRelativePos_x = iRelativePos_x;
    m_iRelativePos_y = iRelativePos_y;
}

void CUIControl::SetResizeType(int iResizeType, int iRelativeWidth, int iRelativeHeight)
{
    m_iResizeType = iResizeType;
    m_iRelativeWidth = iRelativeWidth;
    m_iRelativeHeight = iRelativeHeight;
}

void CUIControl::SendUIMessageDirect(int iMessage, int iParam1, int iParam2)
{
    SendUIMessage(iMessage, iParam1, iParam2);
    DoAction(TRUE);
}

BOOL CUIControl::DoAction(BOOL bMessageOnly)
{
    while (m_MessageList.empty() == FALSE)
    {
        GetUIMessage();
        if (HandleMessage() == FALSE)
            DefaultHandleMessage();
    }

    DoActionSub(bMessageOnly);

    if (bMessageOnly == TRUE)
        return 0;

    if (CheckMouseIn(m_iPos_x, m_iPos_y, m_iWidth, m_iHeight, m_iCoordType))
    {
        if (g_dwMouseUseUIID == 0)
        {
            if (g_dwActiveUIID == 0 || g_dwActiveUIID == GetUIID())
            {
                g_dwMouseUseUIID = GetUIID();
            }
            else
            {
                return FALSE;
            }
        }
        if (GetState() == UISTATE_NORMAL)
        {
            if (g_dwMouseUseUIID != GetUIID() && g_dwActiveUIID != GetUIID())
                return FALSE;

            if (MouseLButton == true)
            {
                CUIControl *pRootWindow = nullptr;
                if (GetParentUIID() == 0)
                {
                    pRootWindow = this;
                }
                else
                {
                    pRootWindow = g_pWindowMgr->GetWindow(GetParentUIID());
                }

                while (pRootWindow != nullptr && pRootWindow->GetParentUIID() != 0)
                {
                    pRootWindow = g_pWindowMgr->GetWindow(pRootWindow->GetParentUIID());
                }
                if (pRootWindow != nullptr && g_pWindowMgr != nullptr)
                {
                    if (g_pWindowMgr->IsWindow(pRootWindow->GetUIID()) == TRUE)
                        g_pWindowMgr->SendUIMessage(UI_MESSAGE_SELECT, pRootWindow->GetUIID(), 0);
                }
            }
        }
    }
    return DoMouseAction();
}

void CUIControl::DefaultHandleMessage()
{
    switch (m_WorkMessage.m_iMessage)
    {
    case UI_MESSAGE_P_MOVE:
        if (m_dwParentUIID != 0)
        {
            CUIBaseWindow *pWindow = g_pWindowMgr->GetWindow(m_dwParentUIID);
            if (pWindow != nullptr)
            {
                switch (m_iArrangeType)
                {
                case 0:
                    SetPosition(pWindow->RPos_x(m_iRelativePos_x),
                                pWindow->RPos_y(m_iRelativePos_y));
                    break;
                case 1:
                    SetPosition(pWindow->RPos_x(-1 * m_iRelativePos_x) + pWindow->RWidth(),
                                pWindow->RPos_y(m_iRelativePos_y));
                    break;
                case 2:
                    SetPosition(pWindow->RPos_x(m_iRelativePos_x),
                                pWindow->RPos_y(-1 * m_iRelativePos_y) + pWindow->RHeight());
                    break;
                case 3:
                    SetPosition(pWindow->RPos_x(-1 * m_iRelativePos_x) + pWindow->RWidth(),
                                pWindow->RPos_y(-1 * m_iRelativePos_y) + pWindow->RHeight());
                    break;
                default:
                    break;
                }
            }
        }
        break;
    case UI_MESSAGE_P_RESIZE:
        if (m_dwParentUIID != 0)
        {
            CUIBaseWindow *pWindow = g_pWindowMgr->GetWindow(m_dwParentUIID);
            if (pWindow != nullptr)
            {
                switch (m_iResizeType)
                {
                case 0:
                    if (m_iRelativeWidth == 0 && m_iRelativeHeight == 0)
                        break;
                    else
                        SetSize(m_iRelativeWidth, m_iRelativeHeight);
                    break;
                case 1:
                    SetSize(pWindow->RWidth() + m_iRelativeWidth, m_iRelativeHeight);
                    break;
                case 2:
                    SetSize(m_iRelativeWidth, pWindow->RHeight() + m_iRelativeHeight);
                    break;
                case 3:
                    SetSize(pWindow->RWidth() + m_iRelativeWidth,
                            pWindow->RHeight() + m_iRelativeHeight);
                    break;
                default:
                    break;
                }
            }
        }
        break;
    default:
        break;
    }
}

CUIButton::CUIButton(SessionKeeper &keeper) : CUIControl(keeper)
{
    m_dwButtonID = 0;
    m_pszCaption = nullptr;
    SetSize(70, 20);
    m_bMouseState = FALSE;
}

CUIButton::~CUIButton()
{
    if (m_pszCaption != nullptr)
    {
        delete[] m_pszCaption;
        m_pszCaption = nullptr;
    }
    if (g_dwCurrentPressedButtonID == GetUIID())
        g_dwCurrentPressedButtonID = 0;
}

void CUIButton::Init(DWORD dwButtonID, const wchar_t *pszCaption)
{
    m_dwButtonID = dwButtonID;
    SetCaption(pszCaption);
}

void CUIButton::SetCaption(const wchar_t *pszCaption)
{
    if (pszCaption == nullptr)
        return;
    if (m_pszCaption != nullptr)
    {
        if (wcscmp(pszCaption, m_pszCaption) == 0)
            return;
        delete[] m_pszCaption;
    }
    m_pszCaption = new wchar_t[wcslen(pszCaption) + 1];
    wcsncpy(m_pszCaption, pszCaption, wcslen(pszCaption) + 1);
    //m_pszCaption[wcslen(pszCaption)] = '\0';
}

BOOL CUIButton::DoMouseAction()
{
    if (GetState() == UISTATE_DISABLE)
    {
        return FALSE;
    }

    if (/*MouseOnWindow == false && */ CheckMouseIn(m_iPos_x, m_iPos_y, m_iWidth, m_iHeight) ==
        TRUE)
    {
        MouseOnWindow = true;
        if (MouseLButtonPush && g_dwCurrentPressedButtonID == 0)
        {
            m_bMouseState = TRUE;
            g_dwCurrentPressedButtonID = GetUIID();
            //			MouseLButton = false;
            //			MouseLButtonPush = false;
        }
        if (MouseLButtonPop && g_dwCurrentPressedButtonID == GetUIID())
        {
            g_pWindowMgr->SendUIMessageToWindow(GetParentUIID(), UI_MESSAGE_BTNLCLICK, m_dwButtonID,
                                                0);
            MouseLButtonPop = false;
            PlayBuffer(SOUND_CLICK01);
            m_bMouseState = FALSE;
            g_dwCurrentPressedButtonID = 0;
            return TRUE;
        }
    }
    else if (m_bMouseState == TRUE)
    {
        MouseLButton = false;
        MouseLButtonPush = false;
        MouseLButtonPop = false;
        g_dwCurrentPressedButtonID = 0;
        m_bMouseState = FALSE;
    }

    return FALSE;
}

template <class T> CUITextListBox<T>::CUITextListBox(SessionKeeper &keeper) : CUIControl(keeper)
{
    m_iCoordType = COORDINATE_TYPE_LEFT_DOWN;
    m_iScrollType = UILISTBOX_SCROLL_DOWNUP;
    m_bUseMultiline = FALSE;
    m_bUseSelectLine = FALSE;
    m_bPressCursorKey = 0;
    m_bNewTypeScrollBar = TRUE;
    SLSetSelectLine(0);
    m_bUseNewUIScrollBar = FALSE;
}

template <class T> CUITextListBox<T>::~CUITextListBox()
{
    Clear();
}

template <class T> void CUITextListBox<T>::Clear()
{
    m_TextList.clear();
    ResetCheckedLine();
    SLSetSelectLine(0);
    m_iCurrentRenderEndLine = 0;
}

template <class T> void CUITextListBox<T>::ResetCheckedLine(BOOL bFlag)
{
    typename std::deque<T>::iterator EndIter = m_TextList.end();
    for (m_TextListIter = m_TextList.begin(); m_TextListIter != EndIter; ++m_TextListIter)
    {
        m_TextListIter->m_bIsSelected = bFlag;
    }
}

template <class T> BOOL CUITextListBox<T>::HaveCheckedLine()
{
    typename std::deque<T>::iterator EndIter = m_TextList.end();
    for (m_TextListIter = m_TextList.begin(); m_TextListIter != EndIter; ++m_TextListIter)
    {
        if (m_TextListIter->m_bIsSelected == TRUE)
            return TRUE;
    }
    return FALSE;
}

template <class T> int CUITextListBox<T>::GetCheckedLines(std::deque<T *> *pSelectLineList)
{
    int iSelectLineNum = 0;
    typename std::deque<T>::iterator EndIter = m_TextList.end();
    for (m_TextListIter = m_TextList.begin(); m_TextListIter != EndIter; ++m_TextListIter)
    {
        if (m_TextListIter->m_bIsSelected == TRUE)
        {
            ++iSelectLineNum;
            pSelectLineList->push_back(&(*m_TextListIter));
        }
    }
    return iSelectLineNum;
}

template <class T> void CUITextListBox<T>::SLSetSelectLine(int iLineNum)
{
    m_iSelectLineNum = iLineNum;
    if (m_iSelectLineNum > GetLineNum())
        m_iSelectLineNum = GetLineNum();
}

template <class T> void CUITextListBox<T>::SLSelectPrevLine(int iLineNum)
{
    if (m_TextList.empty())
    {
        m_iSelectLineNum = 0;
        return;
    }
    else if (m_iSelectLineNum == 0)
        return;

    m_iSelectLineNum += iLineNum;
    if (m_iSelectLineNum > GetLineNum())
        m_iSelectLineNum = GetLineNum();
}

template <class T> void CUITextListBox<T>::SLSelectNextLine(int iLineNum)
{
    if (m_TextList.empty())
    {
        m_iSelectLineNum = 0;
        return;
    }
    else if (m_iSelectLineNum == 0)
        return;

    m_iSelectLineNum -= iLineNum;
    if (m_iSelectLineNum < 1)
        m_iSelectLineNum = 1;
}

template <class T> typename std::deque<T>::iterator CUITextListBox<T>::SLGetSelectLine()
{
    if (m_TextList.empty())
    {
        m_iSelectLineNum = 0;
        return m_TextList.end();
    }
    else if (m_iSelectLineNum == 0)
        return m_TextList.end();

    int iLineCount = 1;
    for (typename std::deque<T>::iterator resultIter = m_TextList.begin();
         resultIter != m_TextList.end(); ++resultIter, ++iLineCount)
    {
        if (iLineCount == m_iSelectLineNum)
            return resultIter;
    }
    assert(!"SLGetSelectLine");
    return m_TextList.end();
}

template <class T> void CUITextListBox<T>::RemoveText()
{
    int iSize = m_TextList.size();
    if (iSize >= m_iMaxLineCount)
    {
        for (int i = 0; i < iSize - m_iMaxLineCount; ++i)
        {
            m_TextList.pop_back();
        }
        SLSetSelectLine(0);
    }
}

template <class T> void CUITextListBox<T>::MoveRenderLine()
{
    if (m_bUseMultiline == TRUE)
        m_TextListIter = m_RenderTextList.begin();
    else
        m_TextListIter = m_TextList.begin();
    for (int i = 0; i < m_iCurrentRenderEndLine; ++i, ++m_TextListIter)
        ;
}

template <class T> BOOL CUITextListBox<T>::CheckMouseInBox()
{
    return CheckMouseIn(m_iPos_x, m_iPos_y, m_iWidth, m_iHeight, COORDINATE_TYPE_LEFT_DOWN);
}

template <class T> void CUITextListBox<T>::ComputeScrollBar()
{
    if (m_bUseNewUIScrollBar == TRUE)
    {
        m_fScrollBarRange_top = m_iPos_y - m_iHeight;
        m_fScrollBarRange_bottom = m_iPos_y;
        m_fScrollBarHeight = 20;

        float fPosRate = 0;
        if (GetLineNum() - GetBoxSize() <= 0)
            fPosRate = 1;
        else
            fPosRate =
                (GetLineNum() > 0 ? (float)m_iCurrentRenderEndLine / (GetLineNum() - GetBoxSize())
                                  : 1);
        m_fScrollBarPos_y =
            m_fScrollBarRange_bottom - m_fScrollBarHeight * 1.5f -
            (m_fScrollBarRange_bottom - m_fScrollBarRange_top - m_fScrollBarHeight * 1.5f) *
                fPosRate;
    }
    else
    {
        if (m_bNewTypeScrollBar == TRUE)
        {
            m_fScrollBarRange_top = m_iPos_y - m_iHeight + 12;
            m_fScrollBarRange_bottom = m_iPos_y - 12;
        }
        else
        {
            m_fScrollBarRange_top = m_iPos_y - m_iHeight + 21;
            m_fScrollBarRange_bottom = m_iPos_y - 17;
        }

        float fLineRate = (GetLineNum() > 0 ? (float)m_iNumRenderLine / (float)GetLineNum() : 1);
        m_fScrollBarHeight =
            (m_fScrollBarRange_bottom - m_fScrollBarRange_top) * (fLineRate < 1 ? fLineRate : 1);
        if (m_fScrollBarHeight < 2)
            m_fScrollBarHeight = 2;
        float fPosRate =
            (GetLineNum() > 0 ? (float)m_iCurrentRenderEndLine / (float)GetLineNum() : 0);
        m_fScrollBarPos_y = m_fScrollBarRange_bottom -
                            ((m_fScrollBarRange_bottom - m_fScrollBarRange_top -
                              (m_fScrollBarHeight > 2 ? 0 : m_fScrollBarHeight)) *
                             fPosRate) -
                            m_fScrollBarHeight;
    }
}

template <class T> void CUITextListBox<T>::Scrolling(int iValue)
{
    if (m_bUseMultiline == TRUE)
    {
        if (m_RenderTextList.size() < (size_t)m_iNumRenderLine)
            return;
    }
    else if (m_TextList.size() < (size_t)m_iNumRenderLine)
        return;

    m_iCurrentRenderEndLine += -1 * iValue;
    if (m_iCurrentRenderEndLine < 0)
        m_iCurrentRenderEndLine = 0;
    else if (m_iCurrentRenderEndLine > GetLineNum() - m_iNumRenderLine)
        m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;
}

template <class T> void CUITextListBox<T>::Resize(int iValue)
{
    m_iNumRenderLine += iValue * 3;
    if (m_iNumRenderLine < 3)
        m_iNumRenderLine = 3;
}

template <class T> BOOL CUITextListBox<T>::HandleMessage()
{
    switch (m_WorkMessage.m_iMessage)
    {
    case UI_MESSAGE_P_RESIZE: {
        CalcLineNum();
        if (m_dwParentUIID != 0)
        {
            int iOldNumRenderLine = m_iNumRenderLine;
            SetNumRenderLine(float(m_iHeight) / 13.0f - 0.5f);

            if (m_iScrollType == UILISTBOX_SCROLL_UPDOWN &&
                (int)m_RenderTextList.size() >= m_iNumRenderLine)
            {
                m_iCurrentRenderEndLine += (iOldNumRenderLine - m_iNumRenderLine);
            }
            Scrolling(0);
        }
    }
    break;
    case UI_MESSAGE_LISTSCRLTOP:
        Scrolling(-1000);
        if (m_bUseSelectLine == TRUE && m_TextList.empty() == FALSE)
        {
            SLSetSelectLine(1000);
        }
        break;
    case UI_MESSAGE_LISTSELUP:
    case UI_MESSAGE_LISTSELDOWN:
        if (m_bUseSelectLine == TRUE && m_TextList.size() >= 1)
        {
            if (m_WorkMessage.m_iMessage == UI_MESSAGE_LISTSELUP)
            {
                SLSelectPrevLine();
            }
            else if (m_WorkMessage.m_iMessage == UI_MESSAGE_LISTSELDOWN)
            {
                SLSelectNextLine();
            }

            if ((int)m_TextList.size() <= m_iNumRenderLine)
                break;

            int iLine = SLGetSelectLineNum() - 1;
            int iTarget = 0;
            if (iLine >= m_iCurrentRenderEndLine + m_iNumRenderLine)
            {
                iTarget = iLine - m_iNumRenderLine + 1;
            }
            else if (iLine < m_iCurrentRenderEndLine)
            {
                iTarget = iLine;
            }
            else
                break;
            Scrolling(m_iCurrentRenderEndLine - iTarget);
        }
        break;
    default:
        break;
    }

    return FALSE;
}

template <class T> BOOL CUITextListBox<T>::DoMouseAction()
{
    BOOL bResult = FALSE;
    if (m_bUseSelectLine == TRUE && g_dwKeyFocusUIID == GetUIID())
    {
        BOOL bKeyPress = FALSE;

        if (IsKeyDown(VK_LEFT))
        {
        }
        if (IsKeyDown(VK_RIGHT))
        {
        }
        if (IsKeyDown(VK_UP))
        {
            bKeyPress = TRUE;
            if (m_bPressCursorKey == 0)
            {
                SendUIMessage(UI_MESSAGE_LISTSELUP, 0, 0);
                m_bPressCursorKey = 1;
            }
            else
                for (int repeat =
                         Core::Time::RepeatCount(m_bPressCursorKey, 5.f, FPS_ANIMATION_FACTOR);
                     repeat > 0; --repeat)
                    SendUIMessage(UI_MESSAGE_LISTSELUP, 0, 0);
        }
        if (IsKeyDown(VK_DOWN))
        {
            bKeyPress = TRUE;
            if (m_bPressCursorKey == 0)
            {
                SendUIMessage(UI_MESSAGE_LISTSELDOWN, 0, 0);
                m_bPressCursorKey = 1;
            }
            else
                for (int repeat =
                         Core::Time::RepeatCount(m_bPressCursorKey, 5.f, FPS_ANIMATION_FACTOR);
                     repeat > 0; --repeat)
                    SendUIMessage(UI_MESSAGE_LISTSELDOWN, 0, 0);
        }
        if (bKeyPress == FALSE)
        {
            m_bPressCursorKey = 0;
        }
    }
    if (CheckMouseInBox())
    {
        if (MouseWheel != 0)
        {
            Scrolling(-3 * MouseWheel);
            MouseWheel = 0;
        }
        if (MouseLButtonPush && GetState() == UISTATE_NORMAL)
        {
            g_dwKeyFocusUIID = GetUIID();

            DoSubMouseAction();

            int iNewTypePos_x = 0;

            if (m_bUseNewUIScrollBar == TRUE)
            {
                if (GetLineNum() < m_iNumRenderLine)
                    ;
                else if (CheckMouseIn(m_iPos_x + m_iWidth - m_fScrollBarWidth, m_fScrollBarPos_y,
                                      m_fScrollBarWidth, m_fScrollBarHeight))
                {
                    if (GetState() == UISTATE_NORMAL && g_dwActiveUIID == 0)
                    {
                        g_dwActiveUIID = GetUIID();
                        SetState(UISTATE_SCROLL);
                        m_fScrollBarClickPos_y = MouseY - m_fScrollBarPos_y;
                    }
                }
                else if (CheckMouseIn(m_iPos_x + m_iWidth - m_fScrollBarWidth,
                                      m_fScrollBarRange_top, m_fScrollBarWidth,
                                      m_fScrollBarPos_y - m_fScrollBarRange_top))
                {
                    if (GetParentUIID() > 0 && g_pWindowMgr->IsRenderFrame() == FALSE)
                        ;
                    else if (m_bScrollBarClick == FALSE)
                    {
                        Scrolling(-1 * m_iNumRenderLine);
                        m_bScrollBarClick = TRUE;
                    }
                    else if (GetParentUIID() == 0 || g_pWindowMgr->IsRenderFrame() == TRUE)
                    {
                        for (int repeat = Core::Time::RepeatCount(m_bScrollBarClick, 15.f,
                                                                  FPS_ANIMATION_FACTOR);
                             repeat > 0; --repeat)
                            Scrolling(-1 * m_iNumRenderLine);
                    }
                }
                else if (CheckMouseIn(m_iPos_x + m_iWidth - m_fScrollBarWidth,
                                      m_fScrollBarPos_y + m_fScrollBarHeight, m_fScrollBarWidth,
                                      m_fScrollBarRange_bottom - m_fScrollBarPos_y -
                                          m_fScrollBarHeight))
                {
                    if (GetParentUIID() > 0 && g_pWindowMgr->IsRenderFrame() == FALSE)
                        ;
                    else if (m_bScrollBarClick == FALSE)
                    {
                        Scrolling(m_iNumRenderLine);
                        m_bScrollBarClick = TRUE;
                    }
                    else if (GetParentUIID() == 0 || g_pWindowMgr->IsRenderFrame() == TRUE)
                    {
                        for (int repeat = Core::Time::RepeatCount(m_bScrollBarClick, 15.f,
                                                                  FPS_ANIMATION_FACTOR);
                             repeat > 0; --repeat)
                            Scrolling(m_iNumRenderLine);
                    }
                }
            }
            else
            {
                if (m_bNewTypeScrollBar == TRUE)
                    iNewTypePos_x = 8;

                if (CheckMouseIn(m_iPos_x + m_iWidth - 21 + iNewTypePos_x,
                                 m_iPos_y - m_iHeight + (m_bNewTypeScrollBar == TRUE ? 0 : 8), 13,
                                 13))
                {
                    if (m_bScrollBtnClick == FALSE)
                    {
                        Scrolling(-1);
                        PlayBuffer(SOUND_CLICK01);
                        m_bScrollBtnClick = TRUE;
                    }
                    else if (GetParentUIID() == 0 || g_pWindowMgr->IsRenderFrame() == TRUE)
                    {
                        for (int repeat = Core::Time::RepeatCount(m_bScrollBtnClick, 15.f,
                                                                  FPS_ANIMATION_FACTOR);
                             repeat > 0; --repeat)
                            Scrolling(-1);
                    }
                }
                if (CheckMouseIn(m_iPos_x + m_iWidth - 21 + iNewTypePos_x,
                                 m_iPos_y - (m_bNewTypeScrollBar == TRUE ? 13 : 21), 13, 13))
                {
                    if (m_bScrollBtnClick == FALSE)
                    {
                        Scrolling(1);
                        PlayBuffer(SOUND_CLICK01);
                        m_bScrollBtnClick = TRUE;
                    }
                    else if (GetParentUIID() == 0 || g_pWindowMgr->IsRenderFrame() == TRUE)
                    {
                        for (int repeat = Core::Time::RepeatCount(m_bScrollBtnClick, 15.f,
                                                                  FPS_ANIMATION_FACTOR);
                             repeat > 0; --repeat)
                            Scrolling(1);
                    }
                }
                if (GetLineNum() < m_iNumRenderLine)
                    ;

                else if (CheckMouseIn(m_iPos_x + m_iWidth - 19 + iNewTypePos_x, m_fScrollBarPos_y,
                                      m_fScrollBarWidth, m_fScrollBarHeight))
                {
                    if (GetState() == UISTATE_NORMAL && g_dwActiveUIID == 0)
                    {
                        g_dwActiveUIID = GetUIID();
                        SetState(UISTATE_SCROLL);
                        m_fScrollBarClickPos_y = MouseY - m_fScrollBarPos_y;
                    }
                }

                else if (CheckMouseIn(m_iPos_x + m_iWidth - 19 + iNewTypePos_x,
                                      m_fScrollBarRange_top, m_fScrollBarWidth,
                                      m_fScrollBarPos_y - m_fScrollBarRange_top))
                {
                    if (GetParentUIID() > 0 && g_pWindowMgr->IsRenderFrame() == FALSE)
                        ;
                    else if (m_bScrollBarClick == FALSE)
                    {
                        Scrolling(-1 * m_iNumRenderLine);
                        m_bScrollBarClick = TRUE;
                    }
                    else if (GetParentUIID() == 0 || g_pWindowMgr->IsRenderFrame() == TRUE)
                    {
                        for (int repeat = Core::Time::RepeatCount(m_bScrollBarClick, 15.f,
                                                                  FPS_ANIMATION_FACTOR);
                             repeat > 0; --repeat)
                            Scrolling(-1 * m_iNumRenderLine);
                    }
                }
                else if (CheckMouseIn(m_iPos_x + m_iWidth - 19 + iNewTypePos_x,
                                      m_fScrollBarPos_y + m_fScrollBarHeight, m_fScrollBarWidth,
                                      m_fScrollBarRange_bottom - m_fScrollBarPos_y -
                                          m_fScrollBarHeight))
                {
                    if (GetParentUIID() > 0 && g_pWindowMgr->IsRenderFrame() == FALSE)
                        ;
                    else if (m_bScrollBarClick == FALSE)
                    {
                        Scrolling(m_iNumRenderLine);
                        m_bScrollBarClick = TRUE;
                    }
                    else if (GetParentUIID() == 0 || g_pWindowMgr->IsRenderFrame() == TRUE)
                    {
                        for (int repeat = Core::Time::RepeatCount(m_bScrollBarClick, 15.f,
                                                                  FPS_ANIMATION_FACTOR);
                             repeat > 0; --repeat)
                            Scrolling(m_iNumRenderLine);
                    }
                }
            }
        }
        else
        {
            m_bScrollBtnClick = FALSE;
            m_bScrollBarClick = FALSE;
        }
        MouseOnWindow = true;
        bResult = TRUE;
    }

    if (GetState() == UISTATE_RESIZE)
    {
        if (MouseLButtonPush)
        {
            MouseOnWindow = true;
            m_iNumRenderLine = (m_iPos_y - MouseY + 5) / 40 * 3;
            if (m_iNumRenderLine < 3)
                m_iNumRenderLine = 3;
            else if (m_iNumRenderLine > 30)
                m_iNumRenderLine = 30;

            if (GetLineNum() < m_iNumRenderLine)
                ;
            else if (GetLineNum() - m_iCurrentRenderEndLine < m_iNumRenderLine)
                m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;
        }
        else
        {
            SetState(UISTATE_NORMAL);
            if (g_dwActiveUIID == GetUIID())
                g_dwActiveUIID = 0;
        }
    }
    else if (GetState() == UISTATE_SCROLL)
    {
        if (MouseLButtonPush)
        {
            MouseOnWindow = true;
            if (m_bUseNewUIScrollBar == TRUE)
            {
                m_fScrollBarPos_y = (float)MouseY - m_fScrollBarClickPos_y;
                if (m_fScrollBarPos_y < m_fScrollBarRange_top)
                {
                    m_fScrollBarPos_y = m_fScrollBarRange_top;
                }
                else if (m_fScrollBarPos_y > m_fScrollBarRange_bottom - m_fScrollBarHeight * 1.5f)
                {
                    m_fScrollBarPos_y = m_fScrollBarRange_bottom - m_fScrollBarHeight * 1.5f;
                }

                if (m_fScrollBarPos_y < m_fScrollBarRange_top)
                {
                    m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;
                }
                else if (m_fScrollBarPos_y > m_fScrollBarRange_bottom - m_fScrollBarHeight)
                {
                    m_iCurrentRenderEndLine = 0;
                }
                else
                {
                    float fRate =
                        (m_fScrollBarRange_bottom - m_fScrollBarPos_y - m_fScrollBarHeight * 1.5f) /
                        (m_fScrollBarRange_bottom - m_fScrollBarRange_top -
                         m_fScrollBarHeight * 1.5f);
                    m_iCurrentRenderEndLine = fRate * (float)(GetLineNum() - GetBoxSize()) + 0.5f;
                }
            }
            else
            {
                m_fScrollBarPos_y = (float)MouseY - m_fScrollBarClickPos_y;
                if (m_fScrollBarPos_y < m_fScrollBarRange_top)
                    m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;
                else if (m_fScrollBarPos_y > m_fScrollBarRange_bottom - m_fScrollBarHeight)
                    m_iCurrentRenderEndLine = 0;
                else
                    m_iCurrentRenderEndLine =
                        (m_fScrollBarRange_bottom - m_fScrollBarPos_y - m_fScrollBarHeight + 0.5f) /
                        (m_fScrollBarRange_bottom - m_fScrollBarRange_top -
                         (m_fScrollBarHeight > 2 ? 0 : 1.0f)) *
                        (float)GetLineNum();
            }
        }
        else
        {
            SetState(UISTATE_NORMAL);
            if (g_dwActiveUIID == GetUIID())
                g_dwActiveUIID = 0;
        }
    }

    MoveRenderLine();
    if (g_dwTopWindow == 0)
    {
        for (int i = 0; i < m_iNumRenderLine; ++i)
        {
            m_TextListIter = m_TextList.begin();

            if (m_TextListIter == m_TextList.end())
                break;
            BOOL bResult = DoLineMouseAction(i);
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
    }
    return bResult;
}

template class CUITextListBox<GUILDLIST_TEXT>;
template class CUITextListBox<WHISPER_TEXT>;
template class CUITextListBox<LETTER_TEXT>;
template class CUITextListBox<WINDOWLIST_TEXT>;
template class CUITextListBox<LETTERLIST_TEXT>;
template class CUITextListBox<SOCKETLIST_TEXT>;
template class CUITextListBox<GUILDLOG_TEXT>;
template class CUITextListBox<UNIONGUILD_TEXT>;
template class CUITextListBox<FILTERLIST_TEXT>;
template class CUITextListBox<UNMIX_TEXT>;
template class CUITextListBox<BCDECLAREGUILD_TEXT>;
template class CUITextListBox<BCGUILD_TEXT>;
template class CUITextListBox<MOVECOMMAND_TEXT>;
template class CUITextListBox<SCurQuestItem>;
template class CUITextListBox<SQuestContents>;
template class CUITextListBox<IGS_StorageItem>;
template class CUITextListBox<IGS_BuyList>;
template class CUITextListBox<IGS_SelectBuyItem>;

int SessionLegacyCalls::CutStr(const wchar_t *source, wchar_t *output, const int targetPixelWidth,
                               const int maxOutputLines, const int outputLength,
                               const int firstLineTab)
{
    return sessionKeeper_.Ui()->CutStr(source, output, targetPixelWidth, maxOutputLines,
                                       outputLength, firstLineTab);
}

int SessionLegacyCalls::CutText3(const wchar_t *text, wchar_t *output, const int targetWidth,
                                 const int maxOutputLines, const int outputLength,
                                 const int firstLineTab, const BOOL reverseWrite)
{
    return sessionKeeper_.Ui()->CutText3(text, output, targetWidth, maxOutputLines, outputLength,
                                         firstLineTab, reverseWrite);
}

CUITextInputBox::CUITextInputBox(SessionKeeper &keeper)
    : CUIControl(keeper), g_pFriendMenu(keeper.FriendMenuObject()),
      s_pFocusedPortable(keeper.FocusedTextInputBox())
{
    m_dwTextColor = _ARGB(255, 255, 255, 255);
    m_dwBackColor = _ARGB(255, 0, 0, 0);
    m_dwSelectBackColor = _ARGB(255, 150, 150, 150);

    m_caretTimer.ResetTimer();

    m_bLock = FALSE;
    m_bPasswordInput = FALSE;

    SetPosition(193, 422);

    m_pTabTarget = nullptr;
    m_bUseMultiLine = FALSE;
    m_bScrollBtnClick = FALSE;
    m_bScrollBarClick = FALSE;
    m_iNumLines = 0;
    m_fScrollBarWidth = 0;
    m_fScrollBarRange_top = 0;
    m_fScrollBarRange_bottom = 0;
    m_fScrollBarHeight = 0;
    m_fScrollBarPos_y = 0;
    m_fScrollBarClickPos_y = 0;
}

CUITextInputBox::~CUITextInputBox()
{
    if (s_pFocusedPortable == this)
        s_pFocusedPortable = nullptr;
}

void CUITextInputBox::GetText(wchar_t *pszText, int iGetLength)
{
    if (pszText == nullptr || iGetLength <= 0)
        return;
    // Copy only as many characters as the text actually has (capped at the
    // caller's length), then terminate right after. The previous wcsncpy form
    // zero-padded all the way to iGetLength-1 and wrote the terminator at
    // [iGetLength-1], so it always touched iGetLength wchar_t even for a short
    // string - overflowing every caller whose buffer is smaller than the default
    // iGetLength (MAX_TEXT_LENGTH = 255). That smashed the stack on Linux, where
    // wchar_t is 4 bytes (e.g. guild creation's tempText[100], #462).
    size_t copyLen = m_portableText.size();
    if (copyLen > static_cast<size_t>(iGetLength - 1))
        copyLen = static_cast<size_t>(iGetLength - 1);
    wmemcpy(pszText, m_portableText.c_str(), copyLen);
    pszText[copyLen] = L'\0';
}

void CUITextInputBox::SetText(const wchar_t *pszText)
{
    std::wstring wstrText = (pszText != nullptr) ? std::wstring(pszText) : std::wstring();
    if (wstrText.length() > MAX_TEXT_LENGTH)
        return;

    if (m_iMaxLength > 0 && static_cast<int>(wstrText.length()) > m_iMaxLength)
        wstrText.resize(m_iMaxLength);
    m_portableText = wstrText;
    m_iCaret = static_cast<int>(m_portableText.length());
    m_iSelAnchor = m_iCaret;
    m_iFirstVisible = 0;
}

void CUITextInputBox::SetTextLimit(int iLimit)
{
    m_iMaxLength = iLimit;
}

void CUITextInputBox::SetSize(int iWidth, int iHeight)
{
    if (iWidth == 0 || iHeight == 0)
        return;

    // The field renders through g_RenderText, so size is the only state to keep.
    m_iWidth = iWidth;
    m_iHeight = iHeight;
}

void CUITextInputBox::Init(int iWidth, int iHeight, int iMaxLength, BOOL bIsPassword)
{
    m_bPasswordInput = bIsPassword;
    m_iMaxLength = iMaxLength;
    m_portableText.clear();
    m_iCaret = 0;
    m_iSelAnchor = 0;
    m_iFirstVisible = 0;
    SetSize(iWidth, iHeight);
    m_caretTimer.ResetTimer();
#ifdef PBG_ADD_INGAMESHOPMSGBOX
    m_bUseScrollbarRender = true;
#endif //PBG_ADD_INGAMESHOPMSGBOX
}

void CUITextInputBox::SetState(int iState)
{
    m_iState = iState;
    // A hidden box must not keep keyboard focus, or the SDL loop would keep
    // routing input to an invisible field.
    if (m_iState == UISTATE_HIDE && s_pFocusedPortable == this)
    {
        s_pFocusedPortable = nullptr;
        m_composition.clear();
    }
}

void CUITextInputBox::GiveFocus(BOOL SelectText)
{
    if (m_iState == UISTATE_HIDE || m_iState == UISTATE_DISABLE)
        return;

    s_pFocusedPortable = this;
    g_dwKeyFocusUIID = GetUIID();
    m_composition.clear();
    m_caretTimer.ResetTimer();

    const int iLength = static_cast<int>(m_portableText.length());
    if (SelectText == TRUE)
    {
        m_iSelAnchor = 0;
        m_iCaret = iLength;
    }
    else
    {
        m_iCaret = iLength;
        m_iSelAnchor = iLength;
    }
}

// Symmetric counterpart to GiveFocus(): drops keyboard focus from the focused
// portable text field without hiding or destroying it. GiveFocus() sets both
// s_pFocusedPortable and g_dwKeyFocusUIID, so release both here (clearing the
// key-focus id only while it still points at this field, to avoid stomping
// another widget), letting the field hand focus back to the game window while
// staying visible.
void SessionUiLegacyBindings::ReleaseTextInputFocus() noexcept
{
    if (focusedTextInputBox_ != nullptr)
    {
        if (g_dwKeyFocusUIID == focusedTextInputBox_->GetUIID())
        {
            g_dwKeyFocusUIID = 0;
        }
        focusedTextInputBox_ = nullptr;
    }
}

void CUITextInputBox::SetFont(LegacyFontRole role)
{
    m_fontRole = role;
}

BOOL CUITextInputBox::DoPortableMouse()
{
    g_RenderText.SetFont(CurrentFont());
    const int iLineHeight = LineHeightPx();

    // Thumb drag in progress: track the mouse until the button is released.
    if (m_bUseMultiLine && GetState() == UISTATE_SCROLL)
    {
        if (MouseLButtonPush)
        {
            MouseOnWindow = true;
            std::vector<PortableLine> lines;
            LayoutLines(BuildDisplay(), lines);
            const float range = m_fScrollBarRange_bottom - m_fScrollBarRange_top;
            const float rel = (float)MouseY - m_fScrollBarClickPos_y - m_fScrollBarRange_top;
            int target = (range > 0.f) ? static_cast<int>((rel / range) * lines.size() + 0.5f) : 0;
            const int iMaxScroll = (static_cast<int>(lines.size()) > m_iNumLines)
                                       ? (static_cast<int>(lines.size()) - m_iNumLines)
                                       : 0;
            if (target < 0)
                target = 0;
            if (target > iMaxScroll)
                target = iMaxScroll;
            m_iScrollLine = target;
        }
        else
        {
            SetState(UISTATE_NORMAL);
            if (g_dwActiveUIID == GetUIID())
                g_dwActiveUIID = 0;
        }
        return TRUE;
    }

    if (CheckMouseIn(m_iPos_x, m_iPos_y - 4, m_iWidth, m_iHeight + 8) == FALSE)
        return FALSE;

    MouseOnWindow = true;
    if (GetState() != UISTATE_NORMAL)
        return TRUE;

    // Mouse wheel scrolls a multiline box.
    if (m_bUseMultiLine && MouseWheel != 0)
    {
        m_iScrollLine -= MouseWheel;
        if (m_iScrollLine < 0)
            m_iScrollLine = 0;
    }

    if (!MouseLButtonPush)
        return TRUE;

    // Scrollbar hit testing (multiline only; geometry set during render).
    if (m_bUseMultiLine)
    {
        if (CheckMouseIn(m_iPos_x + m_iWidth - 15, m_iPos_y - 4, 13, 13))
        {
            if (m_iScrollLine > 0)
                --m_iScrollLine;
            return TRUE;
        }
        if (CheckMouseIn(m_iPos_x + m_iWidth - 15, m_iPos_y + m_iHeight - 9, 13, 13))
        {
            ++m_iScrollLine; // clamped on next render
            return TRUE;
        }
        if (CheckMouseIn(m_iPos_x + m_iWidth - 14, m_fScrollBarPos_y, m_fScrollBarWidth,
                         m_fScrollBarHeight))
        {
            if (g_dwActiveUIID == 0)
            {
                g_dwActiveUIID = GetUIID();
                SetState(UISTATE_SCROLL);
                m_fScrollBarClickPos_y = MouseY - m_fScrollBarPos_y;
            }
            return TRUE;
        }
    }

    // Click in the text area: focus and place the caret at the click.
    GiveFocus(FALSE);
    MouseUpdateTime = 0;
    MouseUpdateTimeMax = 6;

    const std::wstring display = BuildDisplay();
    const int targetX = MouseX - m_iPos_x;
    int idx;
    if (m_bUseMultiLine)
    {
        std::vector<PortableLine> lines;
        LayoutLines(display, lines);
        int li = (iLineHeight > 0) ? m_iScrollLine + (MouseY - m_iPos_y) / iLineHeight : 0;
        if (li < 0)
            li = 0;
        if (li >= static_cast<int>(lines.size()))
            li = static_cast<int>(lines.size()) - 1;
        idx = IndexAtLineX(display, lines[li], targetX);
    }
    else
    {
        // Single line scrolls horizontally; map against the visible window.
        const PortableLine line{m_iFirstVisible, static_cast<int>(display.length())};
        idx = IndexAtLineX(display, line, targetX);
    }
    m_iCaret = idx;
    m_iSelAnchor = idx;
    return TRUE;
}

BOOL CUITextInputBox::DoMouseAction()
{
    return DoPortableMouse();
}
int CUITextInputBox::MeasureWidth(const wchar_t *pszText, int iLength) const
{
    if (pszText == nullptr || iLength <= 0)
        return 0;
    g_RenderText.SetFont(CurrentFont());

    SIZE sz = {0, 0};
    if (!g_RenderText.MeasureText(pszText, static_cast<std::size_t>(iLength), &sz))
    {
        return 0;
    }
    return (g_fScreenRate_x > 0.f) ? static_cast<int>(sz.cx / g_fScreenRate_x) : sz.cx;
}

void CUITextInputBox::MoveCaret(int iNewCaret, bool bExtendSelection)
{
    const int iLength = static_cast<int>(m_portableText.length());
    if (iNewCaret < 0)
        iNewCaret = 0;
    if (iNewCaret > iLength)
        iNewCaret = iLength;

    m_iCaret = iNewCaret;
    if (!bExtendSelection)
        m_iSelAnchor = m_iCaret;

    m_caretTimer.ResetTimer();
}

void CUITextInputBox::InsertChar(wchar_t ch)
{
    if (HasSelection())
        DeleteSelection();

    if (m_iMaxLength > 0 && static_cast<int>(m_portableText.length()) >= m_iMaxLength)
        return;

    m_portableText.insert(m_portableText.begin() + m_iCaret, ch);
    ++m_iCaret;
    m_iSelAnchor = m_iCaret;
    m_caretTimer.ResetTimer();
}

void CUITextInputBox::DeleteSelection()
{
    if (!HasSelection())
        return;

    const int iStart = SelectionStart();
    const int iEnd = SelectionEnd();
    m_portableText.erase(m_portableText.begin() + iStart, m_portableText.begin() + iEnd);
    m_iCaret = iStart;
    m_iSelAnchor = iStart;
    if (m_iFirstVisible > static_cast<int>(m_portableText.length()))
        m_iFirstVisible = 0;
}

void CUITextInputBox::SelectAll()
{
    m_iSelAnchor = 0;
    m_iCaret = static_cast<int>(m_portableText.length());
}

std::wstring CUITextInputBox::GetSelectedText() const
{
    if (m_iSelAnchor == m_iCaret)
        return std::wstring();
    const int iStart = (m_iSelAnchor < m_iCaret) ? m_iSelAnchor : m_iCaret;
    const int iEnd = (m_iSelAnchor < m_iCaret) ? m_iCaret : m_iSelAnchor;
    return m_portableText.substr(iStart, iEnd - iStart);
}

void CUITextInputBox::OnTextEditing(const wchar_t *pszText)
{
    // IME preedit: stored for display only, never committed to the buffer.
    m_composition = (pszText != nullptr) ? std::wstring(pszText) : std::wstring();
    m_caretTimer.ResetTimer();
}

bool CUITextInputBox::GetCaretArea(int &x, int &y, int &w, int &h) const
{
    if (s_pFocusedPortable != this || m_iCaretAreaH <= 0)
        return false;
    x = m_iCaretAreaX;
    y = m_iCaretAreaY;
    w = LegacyControlDetail::CARET_WIDTH_PX;
    h = m_iCaretAreaH;
    return true;
}

void CUITextInputBox::OnTextInput(const wchar_t *pszText)
{
    if (pszText == nullptr)
        return;

    // Committed text supersedes any active IME composition.
    m_composition.clear();

    for (const wchar_t *p = pszText; *p != L'\0'; ++p)
    {
        wchar_t ch = static_cast<wchar_t>(g_pMultiLanguage->ConvertFulltoHalfWidthChar(*p));
        if (ch < 0x20)
            continue; // drop control characters

        if (CheckOption(UIOPTION_NUMBERONLY))
        {
            if (ch < L'0' || ch > L'9')
                continue;
        }
        else if (CheckOption(UIOPTION_SERIALNUMBER))
        {
            if (ch >= L'0' && ch <= L'9')
            {
            }
            else if (ch >= L'A' && ch <= L'Z')
            {
            }
            else if (ch >= L'a' && ch <= L'z')
                ch -= 32; // force uppercase
            else
                continue;
        }
#ifdef LJH_ADD_RESTRICTION_ON_ID
        else if (CheckOption(UIOPTION_NOLOCALIZEDCHARACTERS))
        {
            if (ch < 33 || ch > 126)
                continue;
        }
#endif // LJH_ADD_RESTRICTION_ON_ID

        InsertChar(ch);
    }
}

void CUITextInputBox::OnEditKey(int iVirtualKey, bool bCtrl, bool bShift)
{
    m_caretTimer.ResetTimer();

    switch (iVirtualKey)
    {
    case VK_LEFT:
        MoveCaret(m_iCaret - 1, bShift);
        break;
    case VK_RIGHT:
        MoveCaret(m_iCaret + 1, bShift);
        break;
    case VK_HOME:
        if (m_bUseMultiLine)
        {
            std::vector<PortableLine> lines;
            LayoutLines(BuildDisplay(), lines);
            MoveCaret(lines[CaretToLine(lines, m_iCaret)].start, bShift);
        }
        else
        {
            MoveCaret(0, bShift);
        }
        break;
    case VK_END:
        if (m_bUseMultiLine)
        {
            std::vector<PortableLine> lines;
            LayoutLines(BuildDisplay(), lines);
            MoveCaret(lines[CaretToLine(lines, m_iCaret)].end, bShift);
        }
        else
        {
            MoveCaret(static_cast<int>(m_portableText.length()), bShift);
        }
        break;
    case VK_UP:
    case VK_DOWN:
        if (m_bUseMultiLine)
        {
            const std::wstring display = BuildDisplay();
            std::vector<PortableLine> lines;
            LayoutLines(display, lines);
            const int cur = CaretToLine(lines, m_iCaret);
            const int target = cur + (iVirtualKey == VK_UP ? -1 : 1);
            if (target >= 0 && target < static_cast<int>(lines.size()))
            {
                const int caretX =
                    MeasureWidth(display.c_str() + lines[cur].start, m_iCaret - lines[cur].start);
                MoveCaret(IndexAtLineX(display, lines[target], caretX), bShift);
            }
        }
        break;
    case VK_BACK:
        if (HasSelection())
            DeleteSelection();
        else if (m_iCaret > 0)
        {
            m_portableText.erase(m_portableText.begin() + m_iCaret - 1);
            --m_iCaret;
            m_iSelAnchor = m_iCaret;
        }
        break;
    case VK_DELETE:
        if (HasSelection())
            DeleteSelection();
        else if (m_iCaret < static_cast<int>(m_portableText.length()))
        {
            m_portableText.erase(m_portableText.begin() + m_iCaret);
            m_iSelAnchor = m_iCaret;
        }
        break;
    case VK_RETURN:
        if (IsLocked() == TRUE)
            break;
        // A multiline box inserts a hard line break; a single-line, unlocked box
        // notifies its owning UI window so Enter confirms (e.g. submits the
        // dialog). The login screen instead reads Enter from the global key
        // poll, so the parentless single-line case needs nothing here.
        if (UseMultiline() == TRUE)
        {
            InsertChar(L'\n');
            break;
        }
        if (g_pFriendMenu.IsHotkeyEnable() == TRUE)
            break;
        if (GetParentUIID() != 0)
            g_pWindowMgr->SendUIMessageToWindow(GetParentUIID(), UI_MESSAGE_TEXTINPUT, 0, 0);
        break;
    case VK_TAB:
        if (GetTabTarget() != nullptr && GetTabTarget()->GetState() == UISTATE_NORMAL)
            GetTabTarget()->GiveFocus(TRUE);
        break;
    default:
        break;
    }
}

int CUITextInputBox::LineHeightPx() const
{
    SIZE qSize = {0, 0};
    g_RenderText.SetFont(CurrentFont());
    (void)g_RenderText.MeasureText(L"Q", 1, &qSize);
    int iLineHeight =
        (g_fScreenRate_y > 0.f) ? static_cast<int>(qSize.cy / g_fScreenRate_y) : qSize.cy;
    if (iLineHeight <= 0)
        iLineHeight = 1;
    return iLineHeight;
}

int CUITextInputBox::VisibleLineCount(int iLineHeight) const
{
    int n = (iLineHeight > 0) ? (m_iHeight / iLineHeight) : 1;
    return (n < 1) ? 1 : n;
}

// Break the text into displayed lines: a new line starts after every hard '\n'
// and wherever a paragraph exceeds the box width (wrapped at the last space, or
// mid-word when a single word is too long). Each span is [start, end) in buffer
// indices; end excludes the wrapped space or newline.
void CUITextInputBox::LayoutLines(const std::wstring &display,
                                  std::vector<PortableLine> &lines) const
{
    lines.clear();
    const int n = static_cast<int>(display.length());
    const wchar_t *p = display.c_str();

    int paraStart = 0;
    for (int i = 0; i <= n; ++i)
    {
        if (i != n && display[i] != L'\n')
            continue;

        // Wrap the paragraph [paraStart, i).
        int lineStart = paraStart;
        int lastSpace = -1;
        for (int j = paraStart; j < i; ++j)
        {
            if (display[j] == L' ')
                lastSpace = j;
            if (j > lineStart && MeasureWidth(p + lineStart, j + 1 - lineStart) > m_iWidth)
            {
                if (lastSpace > lineStart)
                {
                    lines.push_back({lineStart, lastSpace});
                    lineStart = lastSpace + 1;
                }
                else
                {
                    lines.push_back({lineStart, j});
                    lineStart = j;
                }
                lastSpace = -1;
            }
        }
        lines.push_back({lineStart, i});

        paraStart = i + 1;
    }

    if (lines.empty())
        lines.push_back({0, 0});
}

int CUITextInputBox::CaretToLine(const std::vector<PortableLine> &lines, int iCaret) const
{
    // The caret belongs to the last line whose start is <= caret.
    int result = 0;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i)
    {
        if (lines[i].start <= iCaret)
            result = i;
        else
            break;
    }
    return result;
}

int CUITextInputBox::IndexAtLineX(const std::wstring &display, const PortableLine &line,
                                  int targetX) const
{
    int best = line.start;
    int bestDelta = targetX; // distance from x=0 at line start
    if (bestDelta < 0)
        bestDelta = -bestDelta;

    const wchar_t *p = display.c_str() + line.start;
    for (int k = line.start + 1; k <= line.end; ++k)
    {
        int x = MeasureWidth(p, k - line.start);
        int delta = x - targetX;
        if (delta < 0)
            delta = -delta;
        if (delta < bestDelta)
        {
            bestDelta = delta;
            best = k;
        }
    }
    return best;
}

std::wstring CUITextInputBox::BuildDisplay() const
{
    if (m_bPasswordInput)
        return std::wstring(m_portableText.length(), L'*');
    return m_portableText;
}

std::wstring CUITextInputBox::DisplayTextForRetainedUi() const
{
    std::wstring display = BuildDisplay();
    if (HaveFocus() && !m_composition.empty() && !m_bPasswordInput)
    {
        const std::size_t caret =
            static_cast<std::size_t>((std::clamp)(m_iCaret, 0, static_cast<int>(display.size())));
        display.insert(caret, m_composition);
    }
    return display;
}

int CUITextInputBox::DisplayCaretForRetainedUi() const noexcept
{
    const int baseLength = static_cast<int>(m_portableText.size());
    int caret = (std::clamp)(m_iCaret, 0, baseLength);
    if (HaveFocus() && !m_composition.empty() && !m_bPasswordInput)
    {
        caret += static_cast<int>(m_composition.size());
    }
    return caret;
}

void CUIChatInputBox::Init()
{
    m_TextInputBox.Init(180, 14, 50);
    m_TextInputBox.SetPosition(193, 422);
    m_TextInputBox.SetTextColor(255, 255, 230, 210);
    m_TextInputBox.GiveFocus();

    m_BuddyInputBox.Init(50, 14, 10);
    m_BuddyInputBox.SetPosition(376, 422);
    m_BuddyInputBox.SetTextColor(255, 200, 200, 200);
    m_BuddyInputBox.SetBackColor(0, 25, 25, 25);

    m_CurrentHistoryLine = m_HistoryList.begin();
    m_bHistoryMode = FALSE;
    memset(m_szTempText, 0, MAX_TEXT_LENGTH + 1);
    const HWND window = m_TextInputBox.GetParentHandle();
    SetFocus(window);

    HIMC hIMC = ImmGetContext(window);
    ImmGetConversionStatus(hIMC, &g_dwBKConv, &g_dwBKSent);
    ImmReleaseContext(window, hIMC);
}

void CUIChatInputBox::Reset()
{
    InputIndex = 0;
    TabMove(0);
    ClearTexts();
    RemoveHistory(TRUE);
}

void CUIChatInputBox::TabMove(int iBoxNumber)
{
    if (GetState() == UISTATE_HIDE)
        return;
    if (iBoxNumber == 1)
        m_BuddyInputBox.GiveFocus(TRUE);
    else
        m_TextInputBox.GiveFocus(TRUE);
}

void CUIChatInputBox::SetState(int iState)
{
    m_BuddyInputBox.SetState(iState);
    m_TextInputBox.SetState(iState);
    if (iState == UISTATE_NORMAL)
    {
        m_TextInputBox.GiveFocus();
    }
}

void CUIChatInputBox::SetFont(LegacyFontRole role)
{
    m_TextInputBox.SetFont(role);
    m_BuddyInputBox.SetFont(role);
}

void CUIChatInputBox::SetText(BOOL bSetText, const wchar_t *pText, BOOL bSetBuddyText,
                              const wchar_t *pBuddyText)
{
    if (bSetText == TRUE)
        m_TextInputBox.SetText(pText);
    if (bSetBuddyText == TRUE)
        m_BuddyInputBox.SetText(pBuddyText);
}

BOOL CUIChatInputBox::DoMouseAction()
{
    if (m_TextInputBox.DoAction() == TRUE)
        InputIndex = 0;
    if (m_BuddyInputBox.DoAction() == TRUE)
        InputIndex = 1;
    return TRUE;
}

void CUILoginInputBox::Init()
{
    m_TextInputBox.Init(100, 14, 10);
    m_TextInputBox.SetPosition(294, 300);
    m_TextInputBox.SetTextColor(255, 255, 230, 210);
    m_TextInputBox.SetBackColor(0, 45, 45, 45);
    m_TextInputBox.GiveFocus();

    m_BuddyInputBox.Init(100, 14, 10, TRUE);
    m_BuddyInputBox.SetPosition(294, 350);
    m_BuddyInputBox.SetTextColor(255, 255, 230, 210);
    m_BuddyInputBox.SetBackColor(0, 45, 45, 45);

    SetFocus(m_TextInputBox.GetParentHandle());
}

void CUIMercenaryInputBox::Init()
{
    m_TextInputBox.Init(100, 14, 10);
    m_TextInputBox.SetPosition(294, 300);
    m_TextInputBox.SetTextColor(255, 255, 230, 210);
    m_TextInputBox.SetBackColor(0, 45, 45, 45);
    m_TextInputBox.SetOption(UIOPTION_NUMBERONLY | UIOPTION_ENTERIMECHKOFF);
    m_TextInputBox.GiveFocus();

    m_BuddyInputBox.Init(100, 14, 10);
    m_BuddyInputBox.SetPosition(294, 350);
    m_BuddyInputBox.SetTextColor(255, 255, 230, 210);
    m_BuddyInputBox.SetBackColor(0, 45, 45, 45);
    m_BuddyInputBox.SetOption(UIOPTION_NUMBERONLY | UIOPTION_ENTERIMECHKOFF);

    SetFocus(m_TextInputBox.GetParentHandle());
}

#ifdef PBG_ADD_INGAMESHOP_UI_ITEMSHOP

CRadioButton::CRadioButton()
{
    m_byMouseState = LBTN_DEFAULT;
    m_bCheckState = false;
    m_rtCheckBtn.top = 0;
    m_rtCheckBtn.bottom = BTN_HEIGHT;
    m_rtCheckBtn.left = 0;
    m_rtCheckBtn.right = BTN_WIDTH;
    m_nRadioBtnEnable = 0;
}
CRadioButton::~CRadioButton()
{
    // n/a
}
void CRadioButton::SetCheckState(bool _Value)
{
    m_bCheckState = _Value;
}
void CRadioButton::SetRadioBtnIsEnable(int _Value)
{
    m_nRadioBtnEnable = _Value;
}
bool CRadioButton::UpdateActionCheck(int _nState)
{
    m_bCheckState = _nState;

    if (m_bCheckState)
        m_byMouseState = LBTN_UP;
    else
        m_byMouseState = LBTN_DEFAULT;

    return true;
}

void CRadioButton::SetRadioBtnRect(float _x, float _y, float _width, float _height)
{
    m_rtCheckBtn.top = _y;
    m_rtCheckBtn.bottom = _y + _height;
    m_rtCheckBtn.left = _x;
    m_rtCheckBtn.right = _x + _width;
}

#endif //PBG_ADD_INGAMESHOP_UI_ITEMSHOP

// Common character-preview control.
#pragma pack(push)
#pragma pack()
int CUIPhotoViewer::SelectPhotoPose(int iCurrentAni, int iMoveDir) const
{
    if (m_PhotoHelper.Live == true &&
        (m_PhotoHelper.Type == MODEL_UNICON || m_PhotoHelper.Type == MODEL_PEGASUS ||
         m_PhotoHelper.Type == MODEL_DARK_HORSE ||
         (m_PhotoHelper.Type >= MODEL_FENRIR_BLACK && m_PhotoHelper.Type <= MODEL_FENRIR_GOLD)))
    {
        static const int MAX_POSE_NUM = 3;
        static constexpr int siPose[MAX_POSE_NUM] = {AT_STAND1, AT_MOVE1, AT_ATTACK1};

        int iCurrentAniArray = 0;

        for (int i = 0; i < MAX_POSE_NUM; ++i)
        {
            iCurrentAniArray = i;
            if (iCurrentAni == siPose[i])
                break;
        }

        iCurrentAniArray += iMoveDir;
        if (iCurrentAniArray < 0)
            iCurrentAniArray = MAX_POSE_NUM * 100 + iCurrentAniArray;
        iCurrentAniArray %= MAX_POSE_NUM;
        iCurrentAni = siPose[iCurrentAniArray];
    }
    else
    {
        static const int MAX_POSE_NUM = 24;
        static constexpr int siPose[MAX_POSE_NUM] = {
            AT_STAND1, AT_GREETING1, AT_CLAP1,  AT_GESTURE1, AT_DIRECTION1, AT_AWKWARD1,
            AT_CRY1,   AT_SEE1,      AT_CHEER1, AT_UNKNOWN1, AT_WIN1,       AT_SMILE1,
            AT_SLEEP1, AT_COLD1,     AT_AGAIN1, AT_RESPECT1, AT_SALUTE1,    AT_GOODBYE1,
            AT_MOVE1,  AT_RUSH1,     AT_SIT1,   AT_POSE1,    AT_HEALING1,   AT_ATTACK1};

        int iCurrentAniArray = 0;
        for (int i = 0; i < MAX_POSE_NUM; ++i)
        {
            iCurrentAniArray = i;
            if (iCurrentAni == siPose[i])
                break;
        }

        iCurrentAniArray += iMoveDir;
        if (iCurrentAniArray < 0)
            iCurrentAniArray = MAX_POSE_NUM * 100 + iCurrentAniArray;
        iCurrentAniArray %= MAX_POSE_NUM;
        iCurrentAni = siPose[iCurrentAniArray];
    }

    return iCurrentAni;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
int CUIPhotoViewer::SetPhotoPose(int iCurrentAni, int iMoveDir)
{
    iCurrentAni = SelectPhotoPose(iCurrentAni, iMoveDir);
    CHARACTER *c = &m_PhotoChar;
    OBJECT *o = &c->Object;
    switch (iCurrentAni)
    {
    case AT_STAND1: {
        const WorldPreviewContext preview(sessionKeeper_);
        SetPlayerStop(c);
        break;
    }
    case AT_ATTACK1: {
        const WorldPreviewContext preview(sessionKeeper_);
        SetPlayerAttack(c);
        c->AttackTime = 1;
        c->Object.AnimationFrame = 0;
        SetCharacterTarget(*c, -1);
        break;
    }
    case AT_MOVE1: {
        const WorldPreviewContext preview(sessionKeeper_);
        SetPlayerWalk(c);
        break;
    }
    case AT_SIT1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(&c->Object, PLAYER_SIT1);
        else
            SetAction(&c->Object, PLAYER_SIT_FEMALE1);
        break;
    case AT_POSE1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(&c->Object, PLAYER_POSE1);
        else
            SetAction(&c->Object, PLAYER_POSE_FEMALE1);
        break;
    case AT_HEALING1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(&c->Object, PLAYER_HEALING1);
        else
            SetAction(&c->Object, PLAYER_HEALING_FEMALE1);
        break;
    case AT_GREETING1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(o, PLAYER_GREETING1);
        else
            SetAction(o, PLAYER_GREETING_FEMALE1);
        break;
    case AT_GOODBYE1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(o, PLAYER_GOODBYE1);
        else
            SetAction(o, PLAYER_GOODBYE_FEMALE1);
        break;
    case AT_CLAP1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(o, PLAYER_CLAP1);
        else
            SetAction(o, PLAYER_CLAP_FEMALE1);
        break;
    case AT_GESTURE1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(o, PLAYER_GESTURE1);
        else
            SetAction(o, PLAYER_GESTURE_FEMALE1);
        break;
    case AT_DIRECTION1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(o, PLAYER_DIRECTION1);
        else
            SetAction(o, PLAYER_DIRECTION_FEMALE1);
        break;
    case AT_UNKNOWN1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(o, PLAYER_UNKNOWN1);
        else
            SetAction(o, PLAYER_UNKNOWN_FEMALE1);
        break;
    case AT_CRY1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(o, PLAYER_CRY1);
        else
            SetAction(o, PLAYER_CRY_FEMALE1);
        break;
    case AT_AWKWARD1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(o, PLAYER_AWKWARD1);
        else
            SetAction(o, PLAYER_AWKWARD_FEMALE1);
        break;
    case AT_SEE1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(o, PLAYER_SEE1);
        else
            SetAction(o, PLAYER_SEE_FEMALE1);
        break;
    case AT_CHEER1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(o, PLAYER_CHEER1);
        else
            SetAction(o, PLAYER_CHEER_FEMALE1);
        break;
    case AT_WIN1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(o, PLAYER_WIN1);
        else
            SetAction(o, PLAYER_WIN_FEMALE1);
        break;
    case AT_SMILE1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(o, PLAYER_SMILE1);
        else
            SetAction(o, PLAYER_SMILE_FEMALE1);
        break;
    case AT_SLEEP1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(o, PLAYER_SLEEP1);
        else
            SetAction(o, PLAYER_SLEEP_FEMALE1);
        break;
    case AT_COLD1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(o, PLAYER_COLD1);
        else
            SetAction(o, PLAYER_COLD_FEMALE1);
        break;
    case AT_AGAIN1:
        if (!gCharacterManager.IsFemale(c->Class))
            SetAction(o, PLAYER_AGAIN1);
        else
            SetAction(o, PLAYER_AGAIN_FEMALE1);
        break;
    case AT_RESPECT1:
        SetAction(o, PLAYER_RESPECT1);
        break;
    case AT_SALUTE1:
        SetAction(o, PLAYER_SALUTE1);
        break;
    case AT_RUSH1:
        SetAction(o, PLAYER_RUSH1);
        break;
    default:
        break;
    }
    MoveCharacter(c, o);
    AdvanceCharacterEnvironmentState(*c);
    m_iCurrentAnimation = iCurrentAni;
    return iCurrentAni;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIPhotoViewer::SetAnimation(int iAnimationType)
{
    m_iSettingAnimation = iAnimationType;
    SetPhotoPose(m_iSettingAnimation);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIPhotoViewer::ChangeAnimation(int iMoveDir)
{
    m_iSettingAnimation = SetPhotoPose(m_iSettingAnimation, iMoveDir);
    m_iCurrentFrame = 0;
    m_bActionRepeatCheck = TRUE;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
CUIPhotoViewer::CUIPhotoViewer(SessionKeeper &keeper)
    : CUIControl(keeper), cameraProjection_(keeper.CameraProjectionObject()),
      gameplay_(GameplayForConstruction()), g_SummonSystem(keeper.SummonSystemObject())
{
    m_bIsInitialized = FALSE;
    m_iSettingAnimation = 0;
    m_iCurrentFrame = 0;
    m_bActionRepeatCheck = FALSE;
    m_fSettingAngle = 0;
    m_fCurrentAngle = 0;
    m_fRotateClickPos_x = 0;
    m_fSettingZoom = 1.0f;
    m_fCurrentZoom = 1.0f;
    m_bHelpEnable = FALSE;
    m_bUpdatePlayer = FALSE;
    m_fPhotoHelperScale = 0;
    m_iCurrentAnimation = 0;
    m_bIsWebzenMail = FALSE;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
CUIPhotoViewer::~CUIPhotoViewer()
{
    RetirePhotoMount();
    sessionKeeper_.Visual()->RetireCharacterVisualLifetime(m_PhotoChar, m_PhotoVisual);
    UnregisterBone(&m_PhotoChar);
    DeleteCloth(&m_PhotoChar, &m_PhotoChar.Object);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIPhotoViewer::Init(int iInitType)
{
    if (iInitType < 0)
        return;

    sessionKeeper_.Visual()->RetireCharacterVisualLifetime(m_PhotoChar, m_PhotoVisual);
    m_PhotoVisual.Reset();
    m_photoUpdateTime = WorldTime;
    RetirePhotoMount();
    m_PhotoHelper.Initialize();

    m_PhotoChar.Initialize();

    CreateCharacterPointer(&m_PhotoChar, MODEL_PLAYER, (Hero->PositionX), (Hero->PositionY), 0);

    // 이동
    Vector(-300, -300, -300, m_PhotoChar.Object.Position);

    m_bIsInitialized = TRUE;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIPhotoViewer::CopyPlayer()
{
    if (m_bIsInitialized == FALSE)
        return;

    if (m_PhotoChar.Class != Hero->Class)
    {
        m_PhotoChar.Class = Hero->Class;
        SetChangeClass(&m_PhotoChar);
    }

    int i;
    int maxClass = MAX_CLASS;

    BOOL bChangeArmor = FALSE;
    BOOL bChangeWeapon = FALSE;
    BOOL bChangeWing = FALSE;
    BOOL bChangeHelper = FALSE;
    if (Hero->Change == FALSE)
    {
        for (i = 0; i < MAX_BODYPART; ++i)
        {
            if (CompareItemEqual(&m_PhotoChar.BodyPart[i], &Hero->BodyPart[i]) == FALSE)
            {
                bChangeArmor = TRUE;
                break;
            }
        }
        for (i = 0; i < 2; ++i)
        {
            if (CompareItemEqual(&m_PhotoChar.Weapon[i], &Hero->Weapon[i]) == FALSE)
            {
                bChangeWeapon = TRUE;
                break;
            }
        }
        if (CompareItemEqual(&m_PhotoChar.Wing, &Hero->Wing) == FALSE)
            bChangeWing = TRUE;
        if (CompareItemEqual(&m_PhotoChar.Helper, &Hero->Helper) == FALSE)
            bChangeHelper = TRUE;
    }
    else // 변신 상태
    {
        if (CompareItemEqual(&m_PhotoChar.BodyPart[BODYPART_HELM],
                             &CharacterMachine->Equipment[EQUIPMENT_HELM],
                             static_cast<int>(MODEL_BODY_HELM) + Hero->SkinIndex) == FALSE)
            bChangeArmor = TRUE;
        else if (CompareItemEqual(&m_PhotoChar.BodyPart[BODYPART_ARMOR],
                                  &CharacterMachine->Equipment[EQUIPMENT_ARMOR],
                                  static_cast<int>(MODEL_BODY_ARMOR) + Hero->SkinIndex) == FALSE)
            bChangeArmor = TRUE;
        else if (CompareItemEqual(&m_PhotoChar.BodyPart[BODYPART_PANTS],
                                  &CharacterMachine->Equipment[EQUIPMENT_PANTS],
                                  static_cast<int>(MODEL_BODY_PANTS) + Hero->SkinIndex) == FALSE)
            bChangeArmor = TRUE;
        else if (CompareItemEqual(&m_PhotoChar.BodyPart[BODYPART_GLOVES],
                                  &CharacterMachine->Equipment[EQUIPMENT_GLOVES],
                                  static_cast<int>(MODEL_BODY_GLOVES) + Hero->SkinIndex) == FALSE)
            bChangeArmor = TRUE;
        else if (CompareItemEqual(&m_PhotoChar.BodyPart[BODYPART_BOOTS],
                                  &CharacterMachine->Equipment[EQUIPMENT_BOOTS],
                                  static_cast<int>(MODEL_BODY_BOOTS) + Hero->SkinIndex) == FALSE)
            bChangeArmor = TRUE;

        for (i = 0; i < 2; ++i)
        {
            if (CompareItemEqual(&m_PhotoChar.Weapon[i],
                                 &CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT + i],
                                 -1) == FALSE)
            {
                bChangeWeapon = TRUE;
                break;
            }
        }
        if (CompareItemEqual(&m_PhotoChar.Wing, &CharacterMachine->Equipment[EQUIPMENT_WING], -1) ==
            FALSE)
            bChangeWing = TRUE;
        if (CompareItemEqual(&m_PhotoChar.Helper, &CharacterMachine->Equipment[EQUIPMENT_HELPER],
                             -1) == FALSE)
            bChangeHelper = TRUE;
    }

    if (bChangeArmor == FALSE && bChangeWeapon == FALSE && bChangeWing == FALSE &&
        bChangeHelper == FALSE)
        return;

    if (Hero->Change == FALSE)
    {
        if (bChangeArmor == TRUE)
        {
            DeleteCloth(&m_PhotoChar, NULL, NULL);
            memcpy(&m_PhotoChar.BodyPart, &Hero->BodyPart, sizeof(PART_t) * MAX_BODYPART);
        }
        if (bChangeWeapon == TRUE)
        {
            memcpy(&m_PhotoChar.Weapon, &Hero->Weapon, sizeof(PART_t) * 2);
        }
        if (bChangeWing == TRUE)
        {
            memcpy(&m_PhotoChar.Wing, &Hero->Wing, sizeof(PART_t));
            DeleteCloth(NULL, &m_PhotoChar.Object, NULL);
        }
        if (bChangeHelper == TRUE)
        {
            memcpy(&m_PhotoChar.Helper, &Hero->Helper, sizeof(PART_t));
        }
    }
    else // 변신 상태
    {
        if (bChangeArmor == TRUE)
        {
            DeleteCloth(&m_PhotoChar, NULL, NULL);

            m_PhotoChar.BodyPart[BODYPART_HEAD].Type =
                static_cast<int>(MODEL_BODY_HELM) + Hero->SkinIndex;
            SetItemToPhoto(&m_PhotoChar.BodyPart[BODYPART_HELM],
                           &CharacterMachine->Equipment[EQUIPMENT_HELM],
                           static_cast<int>(MODEL_BODY_HELM) + Hero->SkinIndex);
            SetItemToPhoto(&m_PhotoChar.BodyPart[BODYPART_ARMOR],
                           &CharacterMachine->Equipment[EQUIPMENT_ARMOR],
                           static_cast<int>(MODEL_BODY_ARMOR) + Hero->SkinIndex);
            SetItemToPhoto(&m_PhotoChar.BodyPart[BODYPART_PANTS],
                           &CharacterMachine->Equipment[EQUIPMENT_PANTS],
                           static_cast<int>(MODEL_BODY_PANTS) + Hero->SkinIndex);
            SetItemToPhoto(&m_PhotoChar.BodyPart[BODYPART_GLOVES],
                           &CharacterMachine->Equipment[EQUIPMENT_GLOVES],
                           static_cast<int>(MODEL_BODY_GLOVES) + Hero->SkinIndex);
            SetItemToPhoto(&m_PhotoChar.BodyPart[BODYPART_BOOTS],
                           &CharacterMachine->Equipment[EQUIPMENT_BOOTS],
                           static_cast<int>(MODEL_BODY_BOOTS) + Hero->SkinIndex);
        }
        if (bChangeWeapon == TRUE)
        {
            for (i = 0; i < 2; ++i)
            {
                SetItemToPhoto(&m_PhotoChar.Weapon[i],
                               &CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT + i], -1);
            }
        }
        if (bChangeWing == TRUE)
        {
            SetItemToPhoto(&m_PhotoChar.Wing, &CharacterMachine->Equipment[EQUIPMENT_WING], -1);
        }
        if (bChangeHelper == TRUE)
        {
            SetItemToPhoto(&m_PhotoChar.Helper, &CharacterMachine->Equipment[EQUIPMENT_HELPER], -1);
        }
    }

    if (bChangeWeapon == TRUE)
    {
        auto &pet = m_PhotoChar.PetCommands;
        pet.present = Hero->PetCommands.present;
        pet.level = Hero->PetCommands.level;
        ++pet.generation;
        pet.commandRevision = pet.attackRevision = 0;
    }
    if (bChangeHelper == TRUE)
    {
        auto &pet = m_PhotoChar.HelperPetState;
        const auto &appearance = Hero->HelperPetState;
        pet.itemType = appearance.itemType;
        pet.modelType = appearance.modelType;
        pet.subType = appearance.subType;
        pet.linkBone = appearance.linkBone;
        VectorCopy(m_PhotoChar.Object.Position, pet.position);
        ++pet.generation;
        pet.commandRevision = 0;
        pet.targetKey = -1;
        pet.action = 0;
        SetPhotoPose(AT_STAND1);
        m_iSettingAnimation = AT_STAND1;
    }
    else
    {
        SetPhotoPose(m_iCurrentAnimation);
    }

    if (bChangeHelper == TRUE || bChangeWeapon == TRUE)
    {
        RetirePhotoMount();
        m_PhotoHelper.Live = false;
        switch (m_PhotoChar.Helper.Type - MODEL_HELPER)
        {
        case 0:
            CreateMountSub(MODEL_HELPER, m_PhotoChar.Object.Position, &m_PhotoChar.Object,
                           &m_PhotoHelper);
            break;
        case 2:
            CreateMountSub(MODEL_UNICON, m_PhotoChar.Object.Position, &m_PhotoChar.Object,
                           &m_PhotoHelper);
            break;
        case 3:
            CreateMountSub(MODEL_PEGASUS, m_PhotoChar.Object.Position, &m_PhotoChar.Object,
                           &m_PhotoHelper);
            break;
        case 4:
            CreateMountSub(MODEL_DARK_HORSE, m_PhotoChar.Object.Position, &m_PhotoChar.Object,
                           &m_PhotoHelper);
            break;
        case 37: //^ 펜릴 편지 관련
            if (m_PhotoChar.Helper.ExcellentFlags == 0x01)
            {
                CreateMountSub(MODEL_FENRIR_BLACK, m_PhotoChar.Object.Position, &m_PhotoChar.Object,
                               &m_PhotoHelper);
            }
            else if (m_PhotoChar.Helper.ExcellentFlags == 0x02)
            {
                CreateMountSub(MODEL_FENRIR_BLUE, m_PhotoChar.Object.Position, &m_PhotoChar.Object,
                               &m_PhotoHelper);
            }
            else if (m_PhotoChar.Helper.ExcellentFlags == 0x04)
            {
                CreateMountSub(MODEL_FENRIR_GOLD, m_PhotoChar.Object.Position, &m_PhotoChar.Object,
                               &m_PhotoHelper);
            }
            else
            {
                CreateMountSub(MODEL_FENRIR_RED, m_PhotoChar.Object.Position, &m_PhotoChar.Object,
                               &m_PhotoHelper);
            }
            break;
        }
        m_PhotoHelper.Alpha = 0;
        m_fPhotoHelperScale = m_PhotoHelper.Scale * 0.7f / m_PhotoChar.Object.Scale;
        PreparePhotoMount();
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIPhotoViewer::SetClass(CLASS_TYPE byClass)
{
    if (m_bIsInitialized == FALSE)
        return;
    m_PhotoChar.Class = byClass;
    SetChangeClass(&m_PhotoChar);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIPhotoViewer::SetEquipmentPacket(BYTE *pbyEquip)
{
    if (m_bIsInitialized == FALSE)
        return;
    //CHARACTER *c = CharactersClient;
    //CharactersClient = &m_PhotoChar;

    RetirePhotoMount();
    ReadEquipmentExtended(0, 0, pbyEquip, &m_PhotoChar, &m_PhotoHelper);
    PreparePhotoMount();

    m_fPhotoHelperScale = m_PhotoHelper.Scale * 0.7f / Hero->Object.Scale;

    if (m_PhotoChar.Wing.Type != -1 && m_iSettingAnimation > AT_HEALING1)
        m_PhotoChar.SafeZone = true;
    else
        m_PhotoChar.SafeZone = false;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIPhotoViewer::SetAngle(float fDegree)
{
    if (m_bIsInitialized == FALSE)
        return;
    OBJECT *o = &m_PhotoChar.Object;
    Vector(-20.f, 5.f, 60.f, o->Angle);

    m_fSettingAngle = fDegree;
    m_fCurrentAngle = fDegree;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIPhotoViewer::SetZoom(float fZoom)
{
    m_fSettingZoom = fZoom;
    m_fCurrentZoom = fZoom;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIPhotoViewer::SetID(const wchar_t *pszID)
{
    if (pszID == NULL)
        return;
    mu_swprintf(m_PhotoChar.ID, pszID);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
BOOL CUIPhotoViewer::DoMouseAction()
{
    if (m_bIsWebzenMail == TRUE)
    {
        if (CheckMouseIn(m_iPos_x, m_iPos_y, m_iWidth, m_iHeight) == TRUE)
        {
            MouseOnWindow = true;
        }
        return TRUE;
    }

    if (m_bUpdatePlayer == TRUE && EquipmentSuccess == true)
    {
        CopyPlayer();
    }

    m_PhotoChar.EtcPart = Hero->EtcPart;

    if (CheckOption(UIPHOTOVIEWER_CANCONTROL))
    {
        if (GetState() == UISTATE_NORMAL &&
            CheckMouseIn(m_iPos_x + 1, m_iPos_y + m_iHeight - 17, 16, 16) == TRUE)
        {
            MouseOnWindow = true;
            if (MouseLButtonPush)
            {
                m_bHelpEnable = (m_bHelpEnable + 1) % 2;
                MouseLButtonPush = FALSE;
                MouseLButton = FALSE;
            }
        }
        else if (CheckMouseIn(m_iPos_x, m_iPos_y, m_iWidth, m_iHeight) == TRUE)
        {
            MouseOnWindow = true;
            if (MouseLButtonPush)
            {
                m_bHelpEnable = FALSE;
                if (GetState() == UISTATE_NORMAL && g_dwActiveUIID == 0)
                {
                    g_dwActiveUIID = GetUIID();
                    SetState(UISTATE_SCROLL);
                    m_fRotateClickPos_x = MouseX;
                    SetFocus(g_hWnd);
                }
            }
            else if (MouseRButtonPush)
            {
                m_bHelpEnable = FALSE;
                m_fCurrentAngle = m_fSettingAngle;
                m_fCurrentZoom = m_fSettingZoom;
            }
            else if (MouseWheel != 0)
            {
                m_bHelpEnable = FALSE;
                m_fCurrentZoom += MouseWheel / 50.0f;
                if (m_fCurrentZoom > 1.1f)
                    m_fCurrentZoom = 1.1f;
                else if (m_fCurrentZoom < 0.8f)
                    m_fCurrentZoom = 0.8f;
                MouseWheel = 0;
            }
        }
        if (GetState() == UISTATE_SCROLL)
        {
            if (MouseLButtonPush)
            {
                MouseOnWindow = true;
                m_fCurrentAngle += (MouseX - m_fRotateClickPos_x);
                m_fRotateClickPos_x = MouseX;
            }
            else
            {
                SetState(UISTATE_NORMAL);
                if (g_dwActiveUIID == GetUIID())
                    g_dwActiveUIID = 0;
            }
        }
    }
    else
    {
        if (GetState() == UISTATE_NORMAL &&
            CheckMouseIn(m_iPos_x, m_iPos_y, m_iWidth, m_iHeight) == TRUE)
        {
            MouseOnWindow = true;
            if (MouseLButtonPush)
            {
                MouseLButtonPush = FALSE;
                MouseLButton = FALSE;
            }
        }
    }
    AdvancePhotoCharacter();
    return TRUE;
}
#pragma pack(pop)

void SEASON3B::CNewUISlideWindow::Init()
{
    m_pSlideMgr->Init();
}
void SEASON3B::CNewUISlideWindow::CreateSlideText()
{
    m_pSlideMgr->CreateSlideText();
}
void SEASON3B::CNewUISlideWindow::AddSlide(int iLoopCount, int iLoopDelay, const wchar_t *strText,
                                           int iType, float fSpeed, DWORD dwTextColor)
{
    m_pSlideMgr->AddSlide(iLoopCount, iLoopDelay, strText, iType, fSpeed, dwTextColor);
}

BOOL CompareItemEqual(const PART_t *item1, const PART_t *item2)
{
    return (item1->Type == item2->Type && item1->Level == item2->Level &&
            item1->ExcellentFlags == item2->ExcellentFlags);
}

BOOL CompareItemEqual(const PART_t *item1, const ITEM *item2, int iDefaultValue)
{
    if (item2->Type == -1)
    {
        return (item1->Type == iDefaultValue && item1->Level == item2->Level &&
                item1->ExcellentFlags == item2->ExcellentFlags);
    }
    else
    {
        return (item1->Type == item2->Type + MODEL_ITEM && item1->Level == item2->Level &&
                item1->ExcellentFlags == item2->ExcellentFlags);
    }
}

void SetItemToPhoto(PART_t *itemDest, const ITEM *itemSrc, int iDefaultValue)
{
    if (itemSrc->Type == -1)
    {
        itemDest->Type = iDefaultValue;
        itemDest->Level = itemSrc->Level;
        itemDest->ExcellentFlags = itemSrc->ExcellentFlags;
    }
    else
    {
        itemDest->Type = itemSrc->Type + MODEL_ITEM;
        itemDest->Level = itemSrc->Level;
        itemDest->ExcellentFlags = itemSrc->ExcellentFlags;
    }
}
