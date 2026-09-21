#include "session/SessionWorkspace.h"
#include "app/ApplicationKeeper.h"

namespace
{
constexpr std::int32_t BaseButtonSize = 28;
constexpr std::int32_t BaseButtonGap = 4;
constexpr std::int32_t BaseBarPadding = 8;
constexpr std::int32_t BasePickerWidth = 180;
constexpr std::int32_t BasePickerRowHeight = 28;
constexpr std::uint32_t BarColor = 0x161b22ff;
constexpr std::uint32_t ButtonColor = 0x30363dff;
constexpr std::uint32_t SelectedButtonColor = 0x1f6febff;
constexpr std::uint32_t DisabledButtonColor = 0x21262dff;
constexpr std::uint32_t LabelColor = 0xf0f6fcff;
constexpr std::uint32_t MutedLabelColor = 0x6e7681ff;
constexpr std::uint32_t CellLabelColor = 0x0d111dcc;
constexpr std::uint32_t CursorShadowColor = 0x000000ff;
constexpr wchar_t WorkspaceCursorGlyph = L'\x2196';

bool Contains(SessionDisplayRect rect, std::int32_t x, std::int32_t y) noexcept
{
    return x >= rect.x && y >= rect.y && x < static_cast<std::int64_t>(rect.x) + rect.width &&
           y < static_cast<std::int64_t>(rect.y) + rect.height;
}
} // namespace

SessionWorkspace::SessionWorkspace(ApplicationKeeper &keeper,
                                   SessionAudioWorkspaceView &audio) noexcept
    : ApplicationLegacyCalls(keeper), audio_(audio), inputRouting_(*this)
{
    (void)applicationKeeper_.RegisterSessionWorkspace(*this);
}

bool SessionWorkspace::AddSession(SessionId id)
{
    const auto raw = id.RawValue();
    std::optional<SessionSlotId> slot =
        raw <= (std::numeric_limits<SessionSlotId::ValueType>::max)()
            ? SessionSlotId::TryCreate(static_cast<SessionSlotId::ValueType>(raw))
            : std::nullopt;
    if (!slot.has_value() || SessionForSlot(*slot).has_value())
    {
        const auto greatest = GreatestLiveSlot();
        const auto next =
            greatest.has_value() &&
                    greatest->RawValue() < (std::numeric_limits<SessionSlotId::ValueType>::max)()
                ? SessionSlotId::TryCreate(greatest->RawValue() + 1)
                : SessionSlotId::TryCreate(1);
        if (!next.has_value())
        {
            return false;
        }
        slot = next;
    }
    return AddSession(id, *slot);
}

bool SessionWorkspace::AddSession(SessionId id, SessionSlotId slotId)
{
    if (HasSession(id))
    {
        return false;
    }
    if (SessionForSlot(slotId).has_value())
    {
        return false;
    }

    auto displaySlot = std::make_unique<SessionDisplaySlot>(id);
    auto inputQueue = std::make_unique<SessionInputQueue>(id);
    if (!audio_.AddSession(id))
    {
        return false;
    }
    try
    {
        displaySlots_.emplace(id, std::move(displaySlot));
        inputQueues_.emplace(id, std::move(inputQueue));
        slotAssignments_.emplace(id, slotId);
    }
    catch (...)
    {
        displaySlots_.erase(id);
        inputQueues_.erase(id);
        slotAssignments_.erase(id);
        audio_.RemoveSession(id);
        return false;
    }
    if (!RecalculateLayout())
    {
        RemoveSession(id);
        return false;
    }
    return true;
}

bool SessionWorkspace::RemoveSession(SessionId id) noexcept
{
    if (!HasSession(id))
    {
        return false;
    }

    std::optional<SessionId> replacement;
    const std::vector<SessionId> ordered = OrderedSessions();
    const auto removed = std::find(ordered.begin(), ordered.end(), id);
    if (removed != ordered.end())
    {
        const auto next = std::next(removed);
        if (next != ordered.end())
        {
            replacement = *next;
        }
        else if (removed != ordered.begin())
        {
            replacement = *std::prev(removed);
        }
    }

    (void)fullContentLayout_.Remove(id);
    (void)focusController_.Remove(id);
    if (pointerCapture_ == id)
    {
        pointerCapture_.reset();
    }
    displaySlots_.erase(id);
    inputQueues_.erase(id);
    slotAssignments_.erase(id);
    audio_.RemoveSession(id);
    if (replacement.has_value())
    {
        (void)Focus(*replacement);
    }
    else
    {
        ClearFocus();
    }
    (void)RecalculateLayout();
    return true;
}

std::size_t SessionWorkspace::SessionCount() const noexcept
{
    return displaySlots_.size();
}

std::size_t SessionWorkspace::VisibleSessionCount() const noexcept
{
    return static_cast<std::size_t>(
        std::count_if(displaySlots_.begin(), displaySlots_.end(),
                      [](const auto &entry) { return entry.second->IsVisible(); }));
}

bool SessionWorkspace::ConfigureWindow(SessionDisplayRect windowRect) noexcept
{
    windowRect_ = windowRect;
    return RecalculateLayout();
}

SessionDisplayRect SessionWorkspace::WindowRect() const noexcept
{
    return windowRect_;
}

std::uint64_t SessionWorkspace::PresentationRevision() const noexcept
{
    return presentationRevision_;
}

bool SessionWorkspace::SetControlUiScale(int percent) noexcept
{
    if (percent < 50 || percent > 200)
    {
        return false;
    }
    if (controlUiScalePercent_ == percent)
    {
        return true;
    }
    controlUiScalePercent_ = percent;
    pickerOpen_ = false;
    applicationPointerCapture_ = false;
    return RecalculateLayout();
}

int SessionWorkspace::ControlUiScalePercent() const noexcept
{
    return controlUiScalePercent_;
}

std::uint32_t SessionWorkspace::ControlBarHeightPixels() const noexcept
{
    return controlBarVisible_ ? Metrics().barHeight : 0;
}

bool SessionWorkspace::IsControlBarVisible() const noexcept
{
    return controlBarVisible_;
}

SessionDisplayRect SessionWorkspace::GameContentRect() const noexcept
{
    const std::uint32_t barHeight = (std::min)(ControlBarHeightPixels(), windowRect_.height);
    return {
        windowRect_.x,
        windowRect_.y + static_cast<std::int32_t>(barHeight),
        windowRect_.width,
        windowRect_.height - barHeight,
    };
}

std::int32_t SessionWorkspace::ScaleControlMetric(std::int32_t value) const noexcept
{
    return (std::max)(1, (value * controlUiScalePercent_ + 50) / 100);
}

SessionWorkspace::ControlMetrics SessionWorkspace::Metrics() const noexcept
{
    return {
        ScaleControlMetric(BaseButtonSize),
        ScaleControlMetric(BaseButtonGap),
        ScaleControlMetric(BaseBarPadding),
        ScaleControlMetric(BasePickerWidth),
        ScaleControlMetric(BasePickerRowHeight),
        static_cast<std::uint32_t>(ScaleControlMetric(ControlBarHeight)),
        static_cast<float>(controlUiScalePercent_) / 100.0F,
    };
}

SessionWorkspaceMode SessionWorkspace::Mode() const noexcept
{
    return mode_;
}

bool SessionWorkspace::ToggleMode() noexcept
{
    pickerOpen_ = false;
    if (mode_ == SessionWorkspaceMode::Single)
    {
        mode_ = SessionWorkspaceMode::Grid4;
        const auto focused = FocusedSession();
        const auto slot = focused.has_value() ? SlotId(*focused) : std::nullopt;
        if (slot.has_value())
        {
            visibleGridGroup_ = GroupForSlot(*slot);
        }
    }
    else
    {
        const auto inGroup = OrderedSessionsInGroup(visibleGridGroup_);
        if (!FocusedSession().has_value() && !inGroup.empty())
        {
            focusController_.Focus(inGroup.front());
            if (windowHasFocus_)
            {
                audio_.Focus(inGroup.front());
            }
        }
        mode_ = SessionWorkspaceMode::Single;
    }
    return RecalculateLayout();
}

bool SessionWorkspace::SelectPrevious() noexcept
{
    if (mode_ == SessionWorkspaceMode::Grid4)
    {
        const auto groups = NonemptyGroups();
        const auto current = std::find(groups.begin(), groups.end(), visibleGridGroup_);
        if (current == groups.end() || current == groups.begin())
        {
            return false;
        }
        visibleGridGroup_ = *std::prev(current);
        const auto sessions = OrderedSessionsInGroup(visibleGridGroup_);
        if (!sessions.empty())
        {
            Focus(sessions.front());
        }
        return RecalculateLayout();
    }

    const auto ordered = OrderedSessions();
    const auto focused = FocusedSession();
    const auto current =
        focused.has_value() ? std::find(ordered.begin(), ordered.end(), *focused) : ordered.end();
    if (current == ordered.end() || current == ordered.begin())
    {
        return false;
    }
    return Focus(*std::prev(current));
}

bool SessionWorkspace::SelectNext() noexcept
{
    if (mode_ == SessionWorkspaceMode::Grid4)
    {
        const auto groups = NonemptyGroups();
        const auto current = std::find(groups.begin(), groups.end(), visibleGridGroup_);
        if (current == groups.end() || std::next(current) == groups.end())
        {
            return false;
        }
        visibleGridGroup_ = *std::next(current);
        const auto sessions = OrderedSessionsInGroup(visibleGridGroup_);
        if (!sessions.empty())
        {
            Focus(sessions.front());
        }
        return RecalculateLayout();
    }

    const auto ordered = OrderedSessions();
    const auto focused = FocusedSession();
    const auto current =
        focused.has_value() ? std::find(ordered.begin(), ordered.end(), *focused) : ordered.end();
    if (current == ordered.end() || std::next(current) == ordered.end())
    {
        return false;
    }
    return Focus(*std::next(current));
}

bool SessionWorkspace::UpdateSlotId(SessionId id, SessionSlotId slotId) noexcept
{
    const auto assignment = slotAssignments_.find(id);
    const auto existing = SessionForSlot(slotId);
    if (assignment == slotAssignments_.end() || (existing.has_value() && *existing != id))
    {
        return false;
    }
    assignment->second = slotId;
    if (FocusedSession() == id && mode_ == SessionWorkspaceMode::Grid4)
    {
        visibleGridGroup_ = GroupForSlot(slotId);
    }
    (void)RecalculateLayout();
    return true;
}

bool SessionWorkspace::SwapSlotIds(SessionId first, SessionId second) noexcept
{
    const auto firstAssignment = slotAssignments_.find(first);
    const auto secondAssignment = slotAssignments_.find(second);
    if (firstAssignment == slotAssignments_.end() || secondAssignment == slotAssignments_.end())
    {
        return false;
    }
    std::swap(firstAssignment->second, secondAssignment->second);
    if (mode_ == SessionWorkspaceMode::Grid4)
    {
        const auto focused = FocusedSession();
        if (focused.has_value())
        {
            visibleGridGroup_ = GroupForSlot(*SlotId(*focused));
        }
    }
    (void)RecalculateLayout();
    return true;
}

std::optional<SessionSlotId> SessionWorkspace::SlotId(SessionId id) const noexcept
{
    const auto assignment = slotAssignments_.find(id);
    return assignment == slotAssignments_.end() ? std::nullopt
                                                : std::optional<SessionSlotId>(assignment->second);
}

std::optional<SessionId> SessionWorkspace::SessionForSlot(SessionSlotId slotId) const noexcept
{
    for (const auto &[id, candidate] : slotAssignments_)
    {
        if (candidate == slotId)
        {
            return id;
        }
    }
    return std::nullopt;
}

std::optional<SessionSlotId> SessionWorkspace::GreatestLiveSlot() const noexcept
{
    std::optional<SessionSlotId> greatest;
    for (const auto &[id, slot] : slotAssignments_)
    {
        (void)id;
        if (!greatest.has_value() || *greatest < slot)
        {
            greatest = slot;
        }
    }
    return greatest;
}

std::vector<SessionId> SessionWorkspace::OrderedSessions() const
{
    std::vector<std::pair<SessionSlotId, SessionId>> rows;
    rows.reserve(slotAssignments_.size());
    for (const auto &[id, slot] : slotAssignments_)
    {
        rows.emplace_back(slot, id);
    }
    std::sort(rows.begin(), rows.end());
    std::vector<SessionId> result;
    result.reserve(rows.size());
    for (const auto &[slot, id] : rows)
    {
        (void)slot;
        result.push_back(id);
    }
    return result;
}

std::vector<SessionSlotId> SessionWorkspace::VisibleSlots() const
{
    std::vector<SessionSlotId> slots;
    for (SessionId id : OrderedSessions())
    {
        if (IsVisible(id))
        {
            slots.push_back(*SlotId(id));
        }
    }
    return slots;
}

bool SessionWorkspace::IsVisible(SessionId id) const noexcept
{
    const auto slot = displaySlots_.find(id);
    return slot != displaySlots_.end() && slot->second->IsVisible();
}

bool SessionWorkspace::AssignFullContent(SessionId id, SessionDisplayRect contentRect) noexcept
{
    const auto slot = displaySlots_.find(id);
    const auto assigned = fullContentLayout_.AssignedSession();
    if (slot == displaySlots_.end() || (assigned.has_value() && assigned != id))
    {
        return false;
    }
    const SessionDisplayRect previous = slot->second->ContentRect();
    if (!slot->second->SetContentRect(contentRect) || !fullContentLayout_.Assign(id, contentRect))
    {
        return false;
    }
    for (auto &[candidateId, candidate] : displaySlots_)
    {
        candidate->SetVisible(candidateId == id);
    }
    layoutChanged_ = layoutChanged_ || previous.width != contentRect.width ||
                     previous.height != contentRect.height;
    return true;
}

bool SessionWorkspace::ResizeFullContent(SessionDisplayRect contentRect) noexcept
{
    const auto assigned = fullContentLayout_.AssignedSession();
    if (!assigned.has_value())
    {
        return false;
    }

    const auto slot = displaySlots_.find(*assigned);
    const SessionDisplayRect previous =
        slot != displaySlots_.end() ? slot->second->ContentRect() : SessionDisplayRect{};
    if (slot == displaySlots_.end() || !slot->second->SetContentRect(contentRect))
    {
        return false;
    }
    fullContentLayout_.Resize(contentRect);
    layoutChanged_ = layoutChanged_ || previous.width != contentRect.width ||
                     previous.height != contentRect.height;
    return true;
}

std::optional<SessionDisplayRect> SessionWorkspace::ContentRect(SessionId id) const noexcept
{
    const auto slot = displaySlots_.find(id);
    if (slot == displaySlots_.end())
    {
        return std::nullopt;
    }
    return slot->second->ContentRect();
}

bool SessionWorkspace::Focus(SessionId id) noexcept
{
    if (!HasSession(id))
    {
        return false;
    }

    focusController_.Focus(id);
    if (windowHasFocus_)
    {
        audio_.Focus(id);
    }
    else
    {
        audio_.ClearFocus();
    }
    if (mode_ == SessionWorkspaceMode::Grid4)
    {
        visibleGridGroup_ = GroupForSlot(*SlotId(id));
    }
    (void)RecalculateLayout();
    return true;
}

void SessionWorkspace::ClearFocus() noexcept
{
    focusController_.Clear();
    audio_.ClearFocus();
}

void SessionWorkspace::ApplyAudioGating() noexcept
{
    audio_.ApplyGating();
}

std::optional<SessionId> SessionWorkspace::FocusedSession() const noexcept
{
    return focusController_.FocusedSession();
}

std::optional<SessionId> SessionWorkspace::InputFocusedSession() const noexcept
{
    return windowHasFocus_ ? FocusedSession() : std::nullopt;
}

bool SessionWorkspace::PushInput(SessionId id, const SessionInputEvent &event) noexcept
{
    const auto queue = inputQueues_.find(id);
    if (queue == inputQueues_.end())
    {
        return false;
    }

    return queue->second->Push(event);
}

SessionInputRoutingView &SessionWorkspace::InputRouting() noexcept
{
    return inputRouting_;
}

SessionDisplayView *SessionWorkspace::Display(SessionId id) noexcept
{
    const auto slot = displaySlots_.find(id);
    return slot == displaySlots_.end() ? nullptr : &slot->second->View();
}

const SessionDisplayView *SessionWorkspace::Display(SessionId id) const noexcept
{
    const auto slot = displaySlots_.find(id);
    return slot == displaySlots_.end() ? nullptr : &slot->second->View();
}

SessionInputView *SessionWorkspace::Input(SessionId id) noexcept
{
    const auto queue = inputQueues_.find(id);
    return queue == inputQueues_.end() ? nullptr : &queue->second->View();
}

SessionAudioBusView *SessionWorkspace::Audio(SessionId id) noexcept
{
    return audio_.Audio(id);
}

bool SessionWorkspace::HasSession(SessionId id) const noexcept
{
    return displaySlots_.contains(id) && inputQueues_.contains(id) &&
           slotAssignments_.contains(id) && audio_.Audio(id) != nullptr;
}

SessionInputRoute SessionWorkspace::RouteInput(const SessionInputEvent &event)
{
    if (event.action == SessionInputAction::ApplicationShortcut)
    {
        HandleApplicationShortcut(event);
        return {SessionInputRouteResult::ConsumedByApplication, std::nullopt};
    }
    if (event.action == SessionInputAction::Quit)
    {
        return {SessionInputRouteResult::ConsumedByApplication, std::nullopt};
    }

    if (event.action == SessionInputAction::WindowFocusLost)
    {
        windowHasFocus_ = false;
        pointerCapture_.reset();
        applicationPointerCapture_ = false;
        pickerOpen_ = false;
        hoveringWorkspaceUi_ = false;
        audio_.ClearFocus();
        RebuildOverlay();
    }
    else if (event.action == SessionInputAction::WindowFocusGained)
    {
        windowHasFocus_ = true;
        if (const std::optional<SessionId> focused = FocusedSession())
        {
            (void)audio_.Focus(*focused);
        }
    }

    if (event.action == SessionInputAction::WindowResized)
    {
        (void)ConfigureWindow({0, 0, event.width, event.height});
    }

    if (event.kind == SessionInputEventKind::Pointer && !pointerCapture_.has_value() &&
        HandleControlBarInput(event))
    {
        return {SessionInputRouteResult::ConsumedByApplication, std::nullopt};
    }

    SessionInputEvent routedEvent = event;
    const bool isPointer = routedEvent.kind == SessionInputEventKind::Pointer;
    std::optional<SessionId> target =
        isPointer ? PointerTarget(routedEvent) : focusController_.FocusedSession();
    if (!target.has_value())
    {
        return {
            isPointer ? SessionInputRouteResult::DiscardedOutsideSlots
                      : SessionInputRouteResult::DiscardedNoFocus,
            std::nullopt,
        };
    }
    if (!windowHasFocus_ && event.action != SessionInputAction::WindowFocusLost &&
        event.action != SessionInputAction::WindowFocusGained)
    {
        return {SessionInputRouteResult::DiscardedNoFocus, std::nullopt};
    }

    if (isPointer)
    {
        TranslatePointer(*target, routedEvent);
        if (routedEvent.action == SessionInputAction::PointerButton && routedEvent.pressed)
        {
            (void)Focus(*target);
            pointerCapture_ = target;
        }
    }

    if (!PushInput(*target, routedEvent))
    {
        return {SessionInputRouteResult::DiscardedNoFocus, std::nullopt};
    }
    if (isPointer && routedEvent.action == SessionInputAction::PointerButton &&
        !routedEvent.pressed)
    {
        pointerCapture_.reset();
    }
    return {SessionInputRouteResult::Queued, target};
}

std::optional<SessionId> SessionWorkspace::PointerTarget(
    const SessionInputEvent &event) const noexcept
{
    if (pointerCapture_.has_value() && HasSession(*pointerCapture_))
    {
        return pointerCapture_;
    }

    for (const auto &[id, slot] : displaySlots_)
    {
        if (!slot->IsVisible())
        {
            continue;
        }
        const SessionDisplayRect rect = slot->ContentRect();
        const auto right = static_cast<std::int64_t>(rect.x) + rect.width;
        const auto bottom = static_cast<std::int64_t>(rect.y) + rect.height;
        if (event.x >= rect.x && event.y >= rect.y && event.x < right && event.y < bottom)
        {
            return id;
        }
    }
    return std::nullopt;
}

std::optional<SessionDisplayRect> SessionWorkspace::SessionRectToWindow(
    SessionId id, SessionDisplayRect localRect) const noexcept
{
    const auto slot = displaySlots_.find(id);
    if (slot == displaySlots_.end())
    {
        return std::nullopt;
    }

    const SessionDisplayRect content = slot->second->ContentRect();
    localRect.x += content.x;
    localRect.y += content.y;
    return localRect;
}

void SessionWorkspace::TranslatePointer(SessionId id, SessionInputEvent &event) const noexcept
{
    const auto slot = displaySlots_.find(id);
    if (slot == displaySlots_.end())
    {
        return;
    }
    const SessionDisplayRect content = slot->second->ContentRect();
    event.x -= content.x;
    event.y -= content.y;
}

void SessionWorkspace::SetTransactionActive(bool active) noexcept
{
    transactionActive_ = active;
    RebuildOverlay();
}

void SessionWorkspace::SetCanAddSession(bool canAdd) noexcept
{
    canAddSession_ = canAdd;
    RebuildOverlay();
}

bool SessionWorkspace::TransactionActive() const noexcept
{
    return transactionActive_;
}

std::optional<WorkspaceCommand> SessionWorkspace::TakeCommand() noexcept
{
    std::optional<WorkspaceCommand> command = pendingCommand_;
    pendingCommand_.reset();
    return command;
}

bool SessionWorkspace::TakeLayoutChanged() noexcept
{
    const bool changed = layoutChanged_;
    layoutChanged_ = false;
    return changed;
}

std::span<const WorkspaceOverlayRect> SessionWorkspace::OverlayRects() const noexcept
{
    return overlayRects_;
}

std::span<const WorkspaceOverlayLabel> SessionWorkspace::OverlayLabels() const noexcept
{
    return overlayLabels_;
}

std::uint32_t SessionWorkspace::GroupForSlot(SessionSlotId slotId) const noexcept
{
    return (slotId.RawValue() - 1) / 4;
}

std::vector<SessionId> SessionWorkspace::OrderedSessionsInGroup(std::uint32_t group) const
{
    std::vector<SessionId> result;
    for (SessionId id : OrderedSessions())
    {
        if (GroupForSlot(*SlotId(id)) == group)
        {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<std::uint32_t> SessionWorkspace::NonemptyGroups() const
{
    std::vector<std::uint32_t> result;
    for (SessionId id : OrderedSessions())
    {
        const std::uint32_t group = GroupForSlot(*SlotId(id));
        if (result.empty() || result.back() != group)
        {
            result.push_back(group);
        }
    }
    return result;
}

bool SessionWorkspace::RecalculateLayout() noexcept
{
    presentationRevision_ =
        presentationRevision_ == (std::numeric_limits<std::uint64_t>::max)()
            ? 1
            : presentationRevision_ + 1;
    if (displaySlots_.empty())
    {
        ClearFocus();
        RebuildOverlay();
        return true;
    }

    const SessionDisplayRect content = GameContentRect();
    if (content.width == 0 || content.height == 0)
    {
        RebuildOverlay();
        return true;
    }

    bool success = true;
    const std::optional<SessionId> focused = FocusedSession();
    if (mode_ == SessionWorkspaceMode::Single)
    {
        for (auto &[id, slot] : displaySlots_)
        {
            const bool visible = focused == id;
            slot->SetVisible(visible);
            const SessionDisplayRect previous = slot->ContentRect();
            if (!slot->SetContentRect(content))
            {
                slot->SetVisible(false);
                success = false;
            }
            else if (previous.width != content.width || previous.height != content.height)
            {
                layoutChanged_ = true;
            }
        }
    }
    else
    {
        const std::uint32_t leftWidth = content.width / 2;
        const std::uint32_t rightWidth = content.width - leftWidth;
        const std::uint32_t topHeight = content.height / 2;
        const std::uint32_t bottomHeight = content.height - topHeight;
        for (auto &[id, slot] : displaySlots_)
        {
            const SessionSlotId slotId = *SlotId(id);
            const bool visible = GroupForSlot(slotId) == visibleGridGroup_;
            slot->SetVisible(visible);
            const std::uint32_t cell = (slotId.RawValue() - 1) % 4;
            const bool right = cell % 2 != 0;
            const bool bottom = cell / 2 != 0;
            const SessionDisplayRect rect{
                content.x + static_cast<std::int32_t>(right ? leftWidth : 0),
                content.y + static_cast<std::int32_t>(bottom ? topHeight : 0),
                right ? rightWidth : leftWidth,
                bottom ? bottomHeight : topHeight,
            };
            const SessionDisplayRect previous = slot->ContentRect();
            if (rect.width == 0 || rect.height == 0 || !slot->SetContentRect(rect))
            {
                slot->SetVisible(false);
                success = false;
            }
            else if (previous.width != rect.width || previous.height != rect.height)
            {
                layoutChanged_ = true;
            }
        }
    }
    RebuildOverlay();
    return success;
}

void SessionWorkspace::QueueCommand(WorkspaceCommandKind kind, std::optional<SessionId> id)
{
    if (!pendingCommand_.has_value())
    {
        pendingCommand_ = WorkspaceCommand{kind, id};
    }
}

void SessionWorkspace::RebuildOverlay()
{
    overlayRects_.clear();
    overlayLabels_.clear();
    if (windowRect_.width == 0 || windowRect_.height == 0 || !controlBarVisible_)
    {
        return;
    }

    const ControlMetrics metrics = Metrics();
    const auto addControl = [this, metrics](SessionDisplayRect rect, std::wstring text,
                                            bool enabled, bool selected = false) {
        overlayRects_.push_back({
            rect,
            enabled ? (selected ? SelectedButtonColor : ButtonColor) : DisabledButtonColor,
        });
        overlayLabels_.push_back({
            rect.x + ScaleControlMetric(9),
            rect.y + ScaleControlMetric(8),
            std::move(text),
            enabled ? LabelColor : MutedLabelColor,
            metrics.textScale,
        });
    };

    const std::uint32_t barHeight = (std::min)(metrics.barHeight, windowRect_.height);
    overlayRects_.push_back({
        {windowRect_.x, windowRect_.y, windowRect_.width, barHeight},
        BarColor,
    });
    const std::int32_t y = windowRect_.y + ScaleControlMetric(4);
    std::int32_t x = windowRect_.x + metrics.barPadding;
    const SessionDisplayRect addRect{
        x,
        y,
        static_cast<std::uint32_t>(metrics.buttonSize),
        static_cast<std::uint32_t>(metrics.buttonSize),
    };
    addControl(addRect, L"+", !transactionActive_ && canAddSession_);
    x += metrics.buttonSize + metrics.buttonGap;
    const SessionDisplayRect gridRect{
        x,
        y,
        static_cast<std::uint32_t>(metrics.buttonSize),
        static_cast<std::uint32_t>(metrics.buttonSize),
    };
    addControl(gridRect, L"#", true, mode_ == SessionWorkspaceMode::Grid4);
    x += metrics.buttonSize + metrics.buttonGap;
    if (mode_ == SessionWorkspaceMode::Single)
    {
        const auto focused = FocusedSession();
        const auto focusedSlot = focused.has_value() ? SlotId(*focused) : std::nullopt;
        const bool canMove =
            !transactionActive_ && focusedSlot.has_value() && focusedSlot->RawValue() > 1;
        addControl(
            {
                x,
                y,
                static_cast<std::uint32_t>(metrics.buttonSize),
                static_cast<std::uint32_t>(metrics.buttonSize),
            },
            L"^", canMove);
        x += metrics.buttonSize + metrics.buttonGap;
        addControl(
            {
                x,
                y,
                static_cast<std::uint32_t>(metrics.buttonSize),
                static_cast<std::uint32_t>(metrics.buttonSize),
            },
            L"X", !transactionActive_ && focused.has_value());
    }

    const std::int32_t right =
        windowRect_.x + static_cast<std::int32_t>(windowRect_.width) - metrics.barPadding;
    const std::int32_t pickerX =
        (std::max)(windowRect_.x + metrics.barPadding, right - metrics.buttonSize -
                                                           metrics.buttonGap - metrics.pickerWidth -
                                                           metrics.buttonGap - metrics.buttonSize);
    const SessionDisplayRect previousRect{
        pickerX,
        y,
        static_cast<std::uint32_t>(metrics.buttonSize),
        static_cast<std::uint32_t>(metrics.buttonSize),
    };
    const SessionDisplayRect pickerRect{
        pickerX + metrics.buttonSize + metrics.buttonGap,
        y,
        static_cast<std::uint32_t>(metrics.pickerWidth),
        static_cast<std::uint32_t>(metrics.buttonSize),
    };
    const SessionDisplayRect nextRect{
        pickerRect.x + metrics.pickerWidth + metrics.buttonGap,
        y,
        static_cast<std::uint32_t>(metrics.buttonSize),
        static_cast<std::uint32_t>(metrics.buttonSize),
    };
    const bool hasPrevious =
        mode_ == SessionWorkspaceMode::Grid4
            ? [&]() {
                  const auto groups = NonemptyGroups();
                  const auto current = std::find(groups.begin(), groups.end(), visibleGridGroup_);
                  return current != groups.end() && current != groups.begin();
              }()
            : [&]() {
                  const auto ordered = OrderedSessions();
                  const auto current =
                      FocusedSession().has_value()
                          ? std::find(ordered.begin(), ordered.end(), *FocusedSession())
                          : ordered.end();
                  return current != ordered.end() && current != ordered.begin();
              }();
    const bool hasNext =
        mode_ == SessionWorkspaceMode::Grid4
            ? [&]() {
                  const auto groups = NonemptyGroups();
                  const auto current = std::find(groups.begin(), groups.end(), visibleGridGroup_);
                  return current != groups.end() && std::next(current) != groups.end();
              }()
            : [&]() {
                  const auto ordered = OrderedSessions();
                  const auto current =
                      FocusedSession().has_value()
                          ? std::find(ordered.begin(), ordered.end(), *FocusedSession())
                          : ordered.end();
                  return current != ordered.end() && std::next(current) != ordered.end();
              }();
    addControl(previousRect, L"<", hasPrevious);
    addControl(pickerRect, L"", true);
    addControl(nextRect, L">", hasNext);

    std::wstring pickerText = L"[-]";
    if (mode_ == SessionWorkspaceMode::Grid4)
    {
        const std::vector<SessionId> group = OrderedSessionsInGroup(visibleGridGroup_);
        if (!group.empty())
        {
            pickerText.clear();
            for (SessionId id : group)
            {
                if (!pickerText.empty())
                {
                    pickerText += L" ";
                }
                pickerText += L"[" + std::to_wstring(SlotId(id)->RawValue()) + L"]";
            }
        }
    }
    else if (FocusedSession().has_value())
    {
        pickerText = L"[" + std::to_wstring(SlotId(*FocusedSession())->RawValue()) + L"]";
    }
    overlayLabels_.push_back({
        pickerRect.x + ScaleControlMetric(8),
        pickerRect.y + ScaleControlMetric(8),
        std::move(pickerText),
        LabelColor,
        metrics.textScale,
    });

    if (pickerOpen_)
    {
        std::int32_t rowY = windowRect_.y + static_cast<std::int32_t>(metrics.barHeight);
        if (mode_ == SessionWorkspaceMode::Grid4)
        {
            for (const std::uint32_t group : NonemptyGroups())
            {
                std::wstring rowText;
                for (const SessionId id : OrderedSessionsInGroup(group))
                {
                    if (!rowText.empty())
                    {
                        rowText += L" ";
                    }
                    rowText += L"[" + std::to_wstring(SlotId(id)->RawValue()) + L"]";
                }
                addControl(
                    {
                        pickerRect.x,
                        rowY,
                        static_cast<std::uint32_t>(metrics.pickerWidth),
                        static_cast<std::uint32_t>(metrics.pickerRowHeight),
                    },
                    std::move(rowText), true, group == visibleGridGroup_);
                rowY += metrics.pickerRowHeight;
            }
        }
        else
        {
            const std::optional<SessionId> focused = FocusedSession();
            for (const SessionId id : OrderedSessions())
            {
                addControl(
                    {
                        pickerRect.x,
                        rowY,
                        static_cast<std::uint32_t>(metrics.pickerWidth),
                        static_cast<std::uint32_t>(metrics.pickerRowHeight),
                    },
                    L"[" + std::to_wstring(SlotId(id)->RawValue()) + L"]", true, focused == id);
                rowY += metrics.pickerRowHeight;
            }
        }
    }

    if (hoveringWorkspaceUi_ && !pickerOpen_)
    {
        const std::wstring tooltip = AccessibleNameAt(hoverX_, hoverY_);
        if (!tooltip.empty())
        {
            const std::uint32_t preferredWidth = static_cast<std::uint32_t>(
                tooltip.size() * ScaleControlMetric(12) + ScaleControlMetric(12));
            const std::uint32_t width = (std::min)(preferredWidth, windowRect_.width);
            const std::uint32_t tooltipHeight = static_cast<std::uint32_t>(ScaleControlMetric(22));
            const std::int32_t maximumX =
                windowRect_.x + static_cast<std::int32_t>(windowRect_.width - width);
            const std::int32_t tooltipX =
                (std::clamp)(hoverX_ + ScaleControlMetric(12), windowRect_.x, maximumX);
            const std::int32_t maximumY =
                windowRect_.y + static_cast<std::int32_t>(windowRect_.height > tooltipHeight
                                                              ? windowRect_.height - tooltipHeight
                                                              : 0);
            const std::int32_t tooltipY =
                (std::clamp)(hoverY_ + ScaleControlMetric(16), windowRect_.y, maximumY);
            overlayRects_.push_back({
                {tooltipX, tooltipY, width, tooltipHeight},
                CellLabelColor,
            });
            overlayLabels_.push_back({
                tooltipX + ScaleControlMetric(6),
                tooltipY + ScaleControlMetric(4),
                tooltip,
                LabelColor,
                metrics.textScale,
            });
        }
    }

    if (hoveringWorkspaceUi_)
    {
        const std::wstring cursor(1, WorkspaceCursorGlyph);
        overlayLabels_.push_back({
            hoverX_ + ScaleControlMetric(2),
            hoverY_ + ScaleControlMetric(2),
            cursor,
            CursorShadowColor,
            metrics.textScale,
        });
        overlayLabels_.push_back({
            hoverX_,
            hoverY_,
            cursor,
            LabelColor,
            metrics.textScale,
        });
    }
}

SessionDisplayRect SessionWorkspace::PickerRect() const noexcept
{
    const ControlMetrics metrics = Metrics();
    const std::int32_t right =
        windowRect_.x + static_cast<std::int32_t>(windowRect_.width) - metrics.barPadding;
    const std::int32_t pickerX =
        (std::max)(windowRect_.x + metrics.barPadding, right - metrics.buttonSize -
                                                           metrics.buttonGap - metrics.pickerWidth -
                                                           metrics.buttonGap - metrics.buttonSize);
    return {
        pickerX + metrics.buttonSize + metrics.buttonGap,
        windowRect_.y + ScaleControlMetric(4),
        static_cast<std::uint32_t>(metrics.pickerWidth),
        static_cast<std::uint32_t>(metrics.buttonSize),
    };
}

std::size_t SessionWorkspace::PickerRowCount() const
{
    return mode_ == SessionWorkspaceMode::Grid4 ? NonemptyGroups().size()
                                                : OrderedSessions().size();
}

SessionDisplayRect SessionWorkspace::PickerDropDownRect() const
{
    const ControlMetrics metrics = Metrics();
    const SessionDisplayRect picker = PickerRect();
    return {
        picker.x,
        windowRect_.y + static_cast<std::int32_t>(metrics.barHeight),
        picker.width,
        static_cast<std::uint32_t>(PickerRowCount() * metrics.pickerRowHeight),
    };
}

bool SessionWorkspace::SelectPickerRow(std::size_t row)
{
    if (mode_ == SessionWorkspaceMode::Grid4)
    {
        const std::vector<std::uint32_t> groups = NonemptyGroups();
        if (row >= groups.size())
        {
            return false;
        }
        visibleGridGroup_ = groups[row];
        const std::vector<SessionId> sessions = OrderedSessionsInGroup(visibleGridGroup_);
        if (!sessions.empty())
        {
            return Focus(sessions.front());
        }
        return RecalculateLayout();
    }

    const std::vector<SessionId> sessions = OrderedSessions();
    return row < sessions.size() && Focus(sessions[row]);
}

void SessionWorkspace::HandleApplicationShortcut(const SessionInputEvent &event)
{
    if (event.repeat)
    {
        return;
    }
    const auto code = static_cast<ApplicationShortcutCode>(event.code);
    if (code == ApplicationShortcutCode::ToggleControlBar)
    {
        controlBarVisible_ = !controlBarVisible_;
        pickerOpen_ = false;
        applicationPointerCapture_ = false;
        hoveringWorkspaceUi_ = false;
        (void)RecalculateLayout();
        return;
    }

    const std::int32_t first = static_cast<std::int32_t>(ApplicationShortcutCode::SelectPickerRow1);
    const std::int32_t last = static_cast<std::int32_t>(ApplicationShortcutCode::SelectPickerRow9);
    if (event.code < first || event.code > last)
    {
        return;
    }
    pickerOpen_ = false;
    if (!SelectPickerRow(static_cast<std::size_t>(event.code - first)))
    {
        RebuildOverlay();
    }
}

bool SessionWorkspace::HandleControlBarInput(const SessionInputEvent &event)
{
    if (!controlBarVisible_)
    {
        return false;
    }
    const ControlMetrics metrics = Metrics();
    const SessionDisplayRect bar{
        windowRect_.x,
        windowRect_.y,
        windowRect_.width,
        (std::min)(metrics.barHeight, windowRect_.height),
    };
    const bool inBar = Contains(bar, event.x, event.y);
    const bool inDropDown = pickerOpen_ && Contains(PickerDropDownRect(), event.x, event.y);
    const bool inWorkspaceUi = inBar || inDropDown;
    if (inWorkspaceUi)
    {
        const bool changed = !hoveringWorkspaceUi_ || hoverX_ != event.x || hoverY_ != event.y;
        hoveringWorkspaceUi_ = true;
        hoverX_ = event.x;
        hoverY_ = event.y;
        if (changed)
        {
            RebuildOverlay();
        }
    }
    else if (hoveringWorkspaceUi_)
    {
        hoveringWorkspaceUi_ = false;
        RebuildOverlay();
    }

    if (event.action == SessionInputAction::PointerButton && !event.pressed &&
        applicationPointerCapture_)
    {
        applicationPointerCapture_ = false;
        return true;
    }
    if (applicationPointerCapture_)
    {
        return true;
    }
    if (!inWorkspaceUi)
    {
        if (pickerOpen_ && event.action == SessionInputAction::PointerButton && event.pressed)
        {
            pickerOpen_ = false;
            applicationPointerCapture_ = true;
            RebuildOverlay();
            return true;
        }
        return false;
    }
    if (event.action != SessionInputAction::PointerButton || !event.pressed)
    {
        return true;
    }
    applicationPointerCapture_ = true;

    if (inDropDown)
    {
        const SessionDisplayRect dropDown = PickerDropDownRect();
        const std::size_t row =
            static_cast<std::size_t>((event.y - dropDown.y) / metrics.pickerRowHeight);
        pickerOpen_ = false;
        if (!SelectPickerRow(row))
        {
            RebuildOverlay();
        }
        return true;
    }

    const std::int32_t y = windowRect_.y + ScaleControlMetric(4);
    std::int32_t x = windowRect_.x + metrics.barPadding;
    const auto buttonRect = [metrics](std::int32_t left, std::int32_t top) {
        return SessionDisplayRect{
            left,
            top,
            static_cast<std::uint32_t>(metrics.buttonSize),
            static_cast<std::uint32_t>(metrics.buttonSize),
        };
    };
    if (Contains(buttonRect(x, y), event.x, event.y))
    {
        if (!transactionActive_ && canAddSession_)
        {
            QueueCommand(WorkspaceCommandKind::AddDefaultSlot);
        }
        return true;
    }
    x += metrics.buttonSize + metrics.buttonGap;
    if (Contains(buttonRect(x, y), event.x, event.y))
    {
        (void)ToggleMode();
        return true;
    }
    x += metrics.buttonSize + metrics.buttonGap;
    if (mode_ == SessionWorkspaceMode::Single)
    {
        if (Contains(buttonRect(x, y), event.x, event.y))
        {
            const auto focused = FocusedSession();
            const auto slot = focused.has_value() ? SlotId(*focused) : std::nullopt;
            if (!transactionActive_ && slot.has_value() && slot->RawValue() > 1)
            {
                QueueCommand(WorkspaceCommandKind::MoveFocusedSlotUp, focused);
            }
            return true;
        }
        x += metrics.buttonSize + metrics.buttonGap;
        if (Contains(buttonRect(x, y), event.x, event.y))
        {
            const auto focused = FocusedSession();
            if (!transactionActive_ && focused.has_value())
            {
                QueueCommand(WorkspaceCommandKind::DiscardSession, focused);
            }
            return true;
        }
    }

    const std::int32_t right =
        windowRect_.x + static_cast<std::int32_t>(windowRect_.width) - metrics.barPadding;
    const std::int32_t pickerX =
        (std::max)(windowRect_.x + metrics.barPadding, right - metrics.buttonSize -
                                                           metrics.buttonGap - metrics.pickerWidth -
                                                           metrics.buttonGap - metrics.buttonSize);
    const SessionDisplayRect previousRect = buttonRect(pickerX, y);
    const SessionDisplayRect nextRect{
        pickerX + metrics.buttonSize + metrics.buttonGap + metrics.pickerWidth + metrics.buttonGap,
        y,
        static_cast<std::uint32_t>(metrics.buttonSize),
        static_cast<std::uint32_t>(metrics.buttonSize),
    };
    if (Contains(previousRect, event.x, event.y))
    {
        pickerOpen_ = false;
        (void)SelectPrevious();
    }
    else if (Contains(PickerRect(), event.x, event.y))
    {
        pickerOpen_ = !pickerOpen_ && PickerRowCount() > 0;
        RebuildOverlay();
    }
    else if (Contains(nextRect, event.x, event.y))
    {
        pickerOpen_ = false;
        (void)SelectNext();
    }
    return true;
}

std::wstring SessionWorkspace::AccessibleNameAt(std::int32_t x, std::int32_t y) const
{
    if (!controlBarVisible_)
    {
        return {};
    }
    const ControlMetrics metrics = Metrics();
    const auto buttonRect = [metrics](std::int32_t left, std::int32_t top) {
        return SessionDisplayRect{
            left,
            top,
            static_cast<std::uint32_t>(metrics.buttonSize),
            static_cast<std::uint32_t>(metrics.buttonSize),
        };
    };
    const SessionDisplayRect bar{
        windowRect_.x,
        windowRect_.y,
        windowRect_.width,
        (std::min)(metrics.barHeight, windowRect_.height),
    };
    if (!Contains(bar, x, y))
    {
        return {};
    }
    const std::int32_t buttonY = windowRect_.y + ScaleControlMetric(4);
    std::int32_t buttonX = windowRect_.x + metrics.barPadding;
    if (Contains(buttonRect(buttonX, buttonY), x, y))
    {
        return L"Add session";
    }
    buttonX += metrics.buttonSize + metrics.buttonGap;
    if (Contains(buttonRect(buttonX, buttonY), x, y))
    {
        return mode_ == SessionWorkspaceMode::Grid4 ? L"Use single-session view"
                                                    : L"Use four-session grid";
    }
    buttonX += metrics.buttonSize + metrics.buttonGap;
    if (mode_ == SessionWorkspaceMode::Single)
    {
        if (Contains(buttonRect(buttonX, buttonY), x, y))
        {
            return L"Move selected session up";
        }
        buttonX += metrics.buttonSize + metrics.buttonGap;
        if (Contains(buttonRect(buttonX, buttonY), x, y))
        {
            const std::optional<SessionId> focused = FocusedSession();
            const std::optional<SessionSlotId> slot =
                focused.has_value() ? SlotId(*focused) : std::nullopt;
            return slot.has_value()
                       ? L"Discard session slot [" + std::to_wstring(slot->RawValue()) + L"]"
                       : L"Discard session slot";
        }
    }
    const std::int32_t right =
        windowRect_.x + static_cast<std::int32_t>(windowRect_.width) - metrics.barPadding;
    const std::int32_t pickerX =
        (std::max)(windowRect_.x + metrics.barPadding, right - metrics.buttonSize -
                                                           metrics.buttonGap - metrics.pickerWidth -
                                                           metrics.buttonGap - metrics.buttonSize);
    if (Contains(buttonRect(pickerX, buttonY), x, y))
    {
        return L"Previous session or grid";
    }
    if (Contains(
            {
                pickerX + metrics.buttonSize + metrics.buttonGap,
                buttonY,
                static_cast<std::uint32_t>(metrics.pickerWidth),
                static_cast<std::uint32_t>(metrics.buttonSize),
            },
            x, y))
    {
        return mode_ == SessionWorkspaceMode::Grid4 ? L"Selected slot group"
                                                    : L"Selected session slot";
    }
    if (Contains(
            {
                pickerX + metrics.buttonSize + metrics.buttonGap + metrics.pickerWidth +
                    metrics.buttonGap,
                buttonY,
                static_cast<std::uint32_t>(metrics.buttonSize),
                static_cast<std::uint32_t>(metrics.buttonSize),
            },
            x, y))
    {
        return L"Next session or grid";
    }
    return {};
}

bool FullContentSessionLayout::Assign(SessionId id, SessionDisplayRect contentRect) noexcept
{
    if (assignedSession_.has_value() && assignedSession_ != id)
    {
        return false;
    }

    assignedSession_ = id;
    contentRect_ = contentRect;
    return true;
}

bool FullContentSessionLayout::Remove(SessionId id) noexcept
{
    if (assignedSession_ != id)
    {
        return false;
    }

    assignedSession_.reset();
    return true;
}

void FullContentSessionLayout::Resize(SessionDisplayRect contentRect) noexcept
{
    contentRect_ = contentRect;
}

std::optional<SessionId> FullContentSessionLayout::AssignedSession() const noexcept
{
    return assignedSession_;
}

SessionDisplayRect FullContentSessionLayout::ContentRect() const noexcept
{
    return contentRect_;
}

RenderSurface::RenderSurface(SessionId id) noexcept : id_(id)
{
}

bool RenderSurface::Resize(std::uint32_t width, std::uint32_t height) noexcept
{
    if (width == 0 || height == 0)
    {
        return false;
    }
    if (width_ == width && height_ == height)
    {
        return true;
    }
    width_ = width;
    height_ = height;
    ++generation_;
    return true;
}

std::uint32_t RenderSurface::Width() const noexcept
{
    return width_;
}

std::uint32_t RenderSurface::Height() const noexcept
{
    return height_;
}

std::uint64_t RenderSurface::Generation() const noexcept
{
    return generation_;
}

bool RenderSurface::IsReady() const noexcept
{
    return width_ != 0 && height_ != 0 && generation_ != 0;
}

SessionId RenderSurface::Id() const noexcept
{
    return id_;
}

namespace
{
constexpr float ReferenceWidth = 640.0F;
constexpr float ReferenceHeight = 480.0F;
} // namespace

SessionDisplaySlot::SessionDisplaySlot(SessionId id) noexcept : id_(id), surface_(id), view_(*this)
{
}

SessionId SessionDisplaySlot::Id() const noexcept
{
    return id_;
}

SessionDisplayRect SessionDisplaySlot::ContentRect() const noexcept
{
    return contentRect_;
}

bool SessionDisplaySlot::SetContentRect(SessionDisplayRect contentRect) noexcept
{
    if (!surface_.Resize(contentRect.width, contentRect.height))
    {
        return false;
    }

    contentRect_ = contentRect;
    openGlX_ = 0;
    openGlY_ = 0;
    openGlWidth_ = static_cast<int>(contentRect.width);
    openGlHeight_ = static_cast<int>(contentRect.height);
    windowWidth_ = contentRect.width;
    windowHeight_ = contentRect.height;
    screenRateX_ = static_cast<float>(contentRect.width) / ReferenceWidth;
    screenRateY_ = static_cast<float>(contentRect.height) / ReferenceHeight;
    return true;
}

bool SessionDisplaySlot::IsVisible() const noexcept
{
    return visible_;
}

void SessionDisplaySlot::SetVisible(bool visible) noexcept
{
    visible_ = visible;
}

SessionDisplayView &SessionDisplaySlot::View() noexcept
{
    return view_;
}

const SessionDisplayView &SessionDisplaySlot::View() const noexcept
{
    return view_;
}

RenderSurface &SessionDisplaySlot::Surface() noexcept
{
    return surface_;
}

SessionDisplayView::SessionDisplayView(SessionDisplaySlot &slot) noexcept : slot_(&slot)
{
}

SessionId SessionDisplayView::Id() const noexcept
{
    return slot_->Id();
}

SessionDisplayRect SessionDisplayView::LocalRect() const noexcept
{
    const SessionDisplayRect content = slot_->ContentRect();
    return {0, 0, content.width, content.height};
}

bool SessionDisplayView::IsVisible() const noexcept
{
    return slot_->IsVisible();
}

std::uint64_t SessionDisplayView::SurfaceGeneration() const noexcept
{
    return slot_->Surface().Generation();
}

int &SessionDisplayView::OpenGlX() noexcept
{
    return slot_->openGlX_;
}

int &SessionDisplayView::OpenGlY() noexcept
{
    return slot_->openGlY_;
}

int &SessionDisplayView::OpenGlWidth() noexcept
{
    return slot_->openGlWidth_;
}

int &SessionDisplayView::OpenGlHeight() noexcept
{
    return slot_->openGlHeight_;
}

unsigned int &SessionDisplayView::WindowWidth() noexcept
{
    return slot_->windowWidth_;
}

unsigned int &SessionDisplayView::WindowHeight() noexcept
{
    return slot_->windowHeight_;
}

float &SessionDisplayView::ScreenRateX() noexcept
{
    return slot_->screenRateX_;
}

float &SessionDisplayView::ScreenRateY() noexcept
{
    return slot_->screenRateY_;
}

SessionInputQueue::SessionInputQueue(SessionId id) noexcept : id_(id), view_(*this)
{
}

SessionId SessionInputQueue::Id() const noexcept
{
    return id_;
}

bool SessionInputQueue::Push(const SessionInputEvent &event) noexcept
{
    if (size_ == Capacity)
    {
        return false;
    }
    const std::size_t tail = (head_ + size_) % Capacity;
    events_[tail] = event;
    ++size_;
    return true;
}

SessionInputView &SessionInputQueue::View() noexcept
{
    return view_;
}

const SessionInputView &SessionInputQueue::View() const noexcept
{
    return view_;
}

std::size_t SessionInputQueue::PendingCount() const noexcept
{
    return size_;
}

std::optional<SessionInputEvent> SessionInputQueue::TryPop() noexcept
{
    if (size_ == 0)
    {
        return std::nullopt;
    }

    const SessionInputEvent event = events_[head_];
    head_ = (head_ + 1) % Capacity;
    --size_;
    ApplyLogicalState(event);
    return event;
}

std::int32_t SessionInputQueue::PointerX() const noexcept
{
    return pointerX_;
}

std::int32_t SessionInputQueue::PointerY() const noexcept
{
    return pointerY_;
}

bool SessionInputQueue::PointerButtonPressed(std::uint32_t button) const noexcept
{
    return button < pointerButtons_.size() && pointerButtons_[button];
}

std::int32_t SessionInputQueue::LastKeyCode() const noexcept
{
    return lastKeyCode_;
}

bool SessionInputQueue::WindowFocused() const noexcept
{
    return windowFocused_;
}

void SessionInputQueue::ApplyLogicalState(const SessionInputEvent &event) noexcept
{
    switch (event.action)
    {
    case SessionInputAction::PointerMove:
        pointerX_ = event.x;
        pointerY_ = event.y;
        break;
    case SessionInputAction::PointerButton:
        pointerX_ = event.x;
        pointerY_ = event.y;
        if (event.code >= 0 && static_cast<std::size_t>(event.code) < pointerButtons_.size())
        {
            pointerButtons_[static_cast<std::size_t>(event.code)] = event.pressed;
        }
        break;
    case SessionInputAction::KeyDown:
        lastKeyCode_ = event.code;
        break;
    case SessionInputAction::WindowFocusGained:
        windowFocused_ = true;
        break;
    case SessionInputAction::WindowFocusLost:
        windowFocused_ = false;
        break;
    default:
        break;
    }
}

SessionInputRoutingView::SessionInputRoutingView(SessionWorkspace &workspace) noexcept
    : workspace_(&workspace)
{
}

SessionInputRoute SessionInputRoutingView::Route(const SessionInputEvent &event)
{
    return workspace_->RouteInput(event);
}

SessionInputView *SessionInputRoutingView::Input(SessionId id) noexcept
{
    return workspace_->Input(id);
}

std::optional<SessionId> SessionInputRoutingView::FocusedSession() const noexcept
{
    return workspace_->FocusedSession();
}

std::optional<SessionDisplayRect> SessionInputRoutingView::SessionRectToWindow(
    SessionId id, SessionDisplayRect localRect) const noexcept
{
    return workspace_->SessionRectToWindow(id, localRect);
}

SessionInputView::SessionInputView(SessionInputQueue &queue) noexcept : queue_(&queue)
{
}

SessionId SessionInputView::Id() const noexcept
{
    return queue_->Id();
}

std::size_t SessionInputView::PendingCount() const noexcept
{
    return queue_->PendingCount();
}

std::optional<SessionInputEvent> SessionInputView::TryPop() noexcept
{
    return queue_->TryPop();
}

std::int32_t SessionInputView::PointerX() const noexcept
{
    return queue_->PointerX();
}

std::int32_t SessionInputView::PointerY() const noexcept
{
    return queue_->PointerY();
}

bool SessionInputView::PointerButtonPressed(std::uint32_t button) const noexcept
{
    return queue_->PointerButtonPressed(button);
}

std::int32_t SessionInputView::LastKeyCode() const noexcept
{
    return queue_->LastKeyCode();
}

bool SessionInputView::WindowFocused() const noexcept
{
    return queue_->WindowFocused();
}

void WorkspaceFocusController::Focus(SessionId id) noexcept
{
    focusedSession_ = id;
}

void WorkspaceFocusController::Clear() noexcept
{
    focusedSession_.reset();
}

bool WorkspaceFocusController::Remove(SessionId id) noexcept
{
    if (focusedSession_ != id)
    {
        return false;
    }

    Clear();
    return true;
}

std::optional<SessionId> WorkspaceFocusController::FocusedSession() const noexcept
{
    return focusedSession_;
}
