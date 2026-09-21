#pragma once
#include "support/CoreMath.h"

#include "app/Application.h"
#include "session/SessionAudio.h"
#include "session/SessionRuntime.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

class SessionKeeper;

class SessionDisplaySlot;

class SessionDisplayView final
{
  public:
    SessionId Id() const noexcept;
    SessionDisplayRect LocalRect() const noexcept;
    bool IsVisible() const noexcept;
    std::uint64_t SurfaceGeneration() const noexcept;

  private:
    friend class SessionDisplaySlot;
    friend class SessionKeeper;

    explicit SessionDisplayView(SessionDisplaySlot &slot) noexcept;
    int &OpenGlX() noexcept;
    int &OpenGlY() noexcept;
    int &OpenGlWidth() noexcept;
    int &OpenGlHeight() noexcept;
    unsigned int &WindowWidth() noexcept;
    unsigned int &WindowHeight() noexcept;
    float &ScreenRateX() noexcept;
    float &ScreenRateY() noexcept;

    SessionDisplaySlot *slot_;
};

class FullContentSessionLayout final
{
  public:
    bool Assign(SessionId id, SessionDisplayRect contentRect) noexcept;
    bool Remove(SessionId id) noexcept;
    void Resize(SessionDisplayRect contentRect) noexcept;

    std::optional<SessionId> AssignedSession() const noexcept;
    SessionDisplayRect ContentRect() const noexcept;

  private:
    std::optional<SessionId> assignedSession_;
    SessionDisplayRect contentRect_;
};

class RenderSurface final
{
  public:
    explicit RenderSurface(SessionId id) noexcept;

    RenderSurface(const RenderSurface &) = delete;
    RenderSurface &operator=(const RenderSurface &) = delete;
    RenderSurface(RenderSurface &&) = delete;
    RenderSurface &operator=(RenderSurface &&) = delete;

    bool Resize(std::uint32_t width, std::uint32_t height) noexcept;
    std::uint32_t Width() const noexcept;
    std::uint32_t Height() const noexcept;
    std::uint64_t Generation() const noexcept;
    bool IsReady() const noexcept;
    SessionId Id() const noexcept;

  private:
    SessionId id_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint64_t generation_ = 0;
};

class SessionDisplaySlot final
{
  public:
    explicit SessionDisplaySlot(SessionId id) noexcept;

    SessionDisplaySlot(const SessionDisplaySlot &) = delete;
    SessionDisplaySlot &operator=(const SessionDisplaySlot &) = delete;
    SessionDisplaySlot(SessionDisplaySlot &&) = delete;
    SessionDisplaySlot &operator=(SessionDisplaySlot &&) = delete;

    SessionId Id() const noexcept;
    SessionDisplayRect ContentRect() const noexcept;
    bool SetContentRect(SessionDisplayRect contentRect) noexcept;
    bool IsVisible() const noexcept;
    void SetVisible(bool visible) noexcept;
    SessionDisplayView &View() noexcept;
    const SessionDisplayView &View() const noexcept;

  private:
    friend class Compositor;
    friend class SessionDisplayView;

    RenderSurface &Surface() noexcept;

    SessionId id_;
    SessionDisplayRect contentRect_;
    int openGlX_ = 0;
    int openGlY_ = 0;
    int openGlWidth_ = 0;
    int openGlHeight_ = 0;
    unsigned int windowWidth_ = 0;
    unsigned int windowHeight_ = 0;
    float screenRateX_ = 1.0F;
    float screenRateY_ = 1.0F;
    bool visible_ = false;
    RenderSurface surface_;
    SessionDisplayView view_;
};

enum class SessionInputEventKind
{
    Key,
    Pointer,
    Text,
    Window,
    Application,
};

enum class SessionInputAction
{
    None,
    PointerMove,
    PointerButton,
    PointerWheel,
    KeyDown,
    KeyUp,
    TextInput,
    TextEditing,
    WindowResized,
    WindowFocusGained,
    WindowFocusLost,
    ApplicationShortcut,
    Quit,
};

enum class ApplicationShortcutCode : std::int32_t
{
    None = 0,
    ToggleControlBar,
    SelectPickerRow1,
    SelectPickerRow2,
    SelectPickerRow3,
    SelectPickerRow4,
    SelectPickerRow5,
    SelectPickerRow6,
    SelectPickerRow7,
    SelectPickerRow8,
    SelectPickerRow9,
};

struct SessionInputEvent final
{
    SessionInputEventKind kind = SessionInputEventKind::Application;
    std::uint64_t sequence = 0;
    SessionInputAction action = SessionInputAction::None;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::int32_t code = 0;
    std::uint32_t modifiers = 0;
    float wheel = 0.0F;
    bool pressed = false;
    bool repeat = false;
    std::uint8_t clicks = 0;
    std::array<char, 32> text{};

    constexpr bool operator==(const SessionInputEvent &) const noexcept = default;
};

class SessionInputQueue;

class SessionInputView final
{
  public:
    SessionId Id() const noexcept;
    std::size_t PendingCount() const noexcept;
    std::optional<SessionInputEvent> TryPop() noexcept;
    std::int32_t PointerX() const noexcept;
    std::int32_t PointerY() const noexcept;
    bool PointerButtonPressed(std::uint32_t button) const noexcept;
    std::int32_t LastKeyCode() const noexcept;
    bool WindowFocused() const noexcept;

  private:
    friend class SessionInputQueue;

    explicit SessionInputView(SessionInputQueue &queue) noexcept;

    SessionInputQueue *queue_;
};

class SessionInputQueue final
{
  public:
    explicit SessionInputQueue(SessionId id) noexcept;

    SessionInputQueue(const SessionInputQueue &) = delete;
    SessionInputQueue &operator=(const SessionInputQueue &) = delete;
    SessionInputQueue(SessionInputQueue &&) = delete;
    SessionInputQueue &operator=(SessionInputQueue &&) = delete;

    SessionId Id() const noexcept;
    bool Push(const SessionInputEvent &event) noexcept;
    SessionInputView &View() noexcept;
    const SessionInputView &View() const noexcept;

  private:
    friend class SessionInputView;

    std::size_t PendingCount() const noexcept;
    std::optional<SessionInputEvent> TryPop() noexcept;
    std::int32_t PointerX() const noexcept;
    std::int32_t PointerY() const noexcept;
    bool PointerButtonPressed(std::uint32_t button) const noexcept;
    std::int32_t LastKeyCode() const noexcept;
    bool WindowFocused() const noexcept;
    void ApplyLogicalState(const SessionInputEvent &event) noexcept;

    SessionId id_;
    static constexpr std::size_t Capacity = 256;

    std::array<SessionInputEvent, Capacity> events_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::int32_t pointerX_ = 0;
    std::int32_t pointerY_ = 0;
    std::array<bool, 8> pointerButtons_{};
    std::int32_t lastKeyCode_ = 0;
    bool windowFocused_ = false;
    SessionInputView view_;
};

enum class SessionInputRouteResult
{
    Queued,
    ConsumedByApplication,
    DiscardedNoFocus,
    DiscardedOutsideSlots,
};

struct SessionInputRoute final
{
    SessionInputRouteResult result;
    std::optional<SessionId> sessionId;
};

class SessionWorkspace;

class SessionInputRoutingView final
{
  public:
    SessionInputRoute Route(const SessionInputEvent &event);
    SessionInputView *Input(SessionId id) noexcept;
    std::optional<SessionId> FocusedSession() const noexcept;
    std::optional<SessionDisplayRect> SessionRectToWindow(
        SessionId id, SessionDisplayRect localRect) const noexcept;

  private:
    friend class SessionWorkspace;

    explicit SessionInputRoutingView(SessionWorkspace &workspace) noexcept;

    SessionWorkspace *workspace_;
};

enum class SessionWorkspaceMode
{
    Single,
    Grid4,
};

enum class WorkspaceCommandKind
{
    AddDefaultSlot,
    MoveFocusedSlotUp,
    DiscardSession,
};

struct WorkspaceCommand final
{
    WorkspaceCommandKind kind;
    std::optional<SessionId> sessionId;
};

class WorkspaceFocusController final
{
  public:
    void Focus(SessionId id) noexcept;
    void Clear() noexcept;
    bool Remove(SessionId id) noexcept;
    std::optional<SessionId> FocusedSession() const noexcept;

  private:
    std::optional<SessionId> focusedSession_;
};

class ApplicationKeeper;
class ApplicationKeeperTestPeer;

class SessionWorkspace final : protected ApplicationLegacyCalls
{
  public:
    static constexpr std::uint32_t ControlBarHeight = 36;

    SessionWorkspace(ApplicationKeeper &keeper, SessionAudioWorkspaceView &audio) noexcept;

    bool AddSession(SessionId id);
    bool AddSession(SessionId id, SessionSlotId slotId);
    bool RemoveSession(SessionId id) noexcept;
    std::size_t SessionCount() const noexcept;
    std::size_t VisibleSessionCount() const noexcept;

    bool ConfigureWindow(SessionDisplayRect windowRect) noexcept;
    SessionDisplayRect WindowRect() const noexcept;
    std::uint64_t PresentationRevision() const noexcept;
    SessionDisplayRect GameContentRect() const noexcept;
    bool SetControlUiScale(int percent) noexcept;
    int ControlUiScalePercent() const noexcept;
    std::uint32_t ControlBarHeightPixels() const noexcept;
    bool IsControlBarVisible() const noexcept;
    SessionWorkspaceMode Mode() const noexcept;
    bool ToggleMode() noexcept;
    bool SelectPrevious() noexcept;
    bool SelectNext() noexcept;
    bool UpdateSlotId(SessionId id, SessionSlotId slotId) noexcept;
    bool SwapSlotIds(SessionId first, SessionId second) noexcept;
    std::optional<SessionSlotId> SlotId(SessionId id) const noexcept;
    std::optional<SessionId> SessionForSlot(SessionSlotId slotId) const noexcept;
    std::optional<SessionSlotId> GreatestLiveSlot() const noexcept;
    std::vector<SessionId> OrderedSessions() const;
    std::vector<SessionSlotId> VisibleSlots() const;
    bool IsVisible(SessionId id) const noexcept;

    void SetTransactionActive(bool active) noexcept;
    void SetCanAddSession(bool canAdd) noexcept;
    bool TransactionActive() const noexcept;
    std::optional<WorkspaceCommand> TakeCommand() noexcept;
    bool TakeLayoutChanged() noexcept;
    std::wstring AccessibleNameAt(std::int32_t x, std::int32_t y) const;
    std::span<const WorkspaceOverlayRect> OverlayRects() const noexcept;
    std::span<const WorkspaceOverlayLabel> OverlayLabels() const noexcept;

    bool AssignFullContent(SessionId id, SessionDisplayRect contentRect) noexcept;
    bool ResizeFullContent(SessionDisplayRect contentRect) noexcept;
    std::optional<SessionDisplayRect> ContentRect(SessionId id) const noexcept;

    bool Focus(SessionId id) noexcept;
    void ClearFocus() noexcept;
    void ApplyAudioGating() noexcept;
    std::optional<SessionId> FocusedSession() const noexcept;
    std::optional<SessionId> InputFocusedSession() const noexcept;

    bool PushInput(SessionId id, const SessionInputEvent &event) noexcept;
    SessionInputRoutingView &InputRouting() noexcept;
    SessionDisplayView *Display(SessionId id) noexcept;
    const SessionDisplayView *Display(SessionId id) const noexcept;
    SessionInputView *Input(SessionId id) noexcept;
    SessionAudioBusView *Audio(SessionId id) noexcept;

  private:
    friend class ApplicationKeeperTestPeer;
    friend class SessionInputRoutingView;

    using DisplaySlots = std::map<SessionId, std::unique_ptr<SessionDisplaySlot>>;
    using InputQueues = std::map<SessionId, std::unique_ptr<SessionInputQueue>>;
    using SlotAssignments = std::map<SessionId, SessionSlotId>;
    struct ControlMetrics final
    {
        std::int32_t buttonSize;
        std::int32_t buttonGap;
        std::int32_t barPadding;
        std::int32_t pickerWidth;
        std::int32_t pickerRowHeight;
        std::uint32_t barHeight;
        float textScale;
    };
    bool HasSession(SessionId id) const noexcept;
    bool RecalculateLayout() noexcept;
    void RebuildOverlay();
    bool HandleControlBarInput(const SessionInputEvent &event);
    void HandleApplicationShortcut(const SessionInputEvent &event);
    std::int32_t ScaleControlMetric(std::int32_t value) const noexcept;
    ControlMetrics Metrics() const noexcept;
    SessionDisplayRect PickerRect() const noexcept;
    SessionDisplayRect PickerDropDownRect() const;
    std::size_t PickerRowCount() const;
    bool SelectPickerRow(std::size_t row);
    void QueueCommand(WorkspaceCommandKind kind, std::optional<SessionId> id = std::nullopt);
    std::vector<SessionId> OrderedSessionsInGroup(std::uint32_t group) const;
    std::vector<std::uint32_t> NonemptyGroups() const;
    std::uint32_t GroupForSlot(SessionSlotId slotId) const noexcept;
    SessionInputRoute RouteInput(const SessionInputEvent &event);
    std::optional<SessionId> PointerTarget(const SessionInputEvent &event) const noexcept;
    std::optional<SessionDisplayRect> SessionRectToWindow(
        SessionId id, SessionDisplayRect localRect) const noexcept;
    void TranslatePointer(SessionId id, SessionInputEvent &event) const noexcept;

    SessionAudioWorkspaceView &audio_;
    DisplaySlots displaySlots_;
    InputQueues inputQueues_;
    SlotAssignments slotAssignments_;
    FullContentSessionLayout fullContentLayout_;
    WorkspaceFocusController focusController_;
    SessionInputRoutingView inputRouting_;
    std::optional<SessionId> pointerCapture_;
    bool windowHasFocus_ = true;
    SessionWorkspaceMode mode_ = SessionWorkspaceMode::Single;
    SessionDisplayRect windowRect_;
    std::uint32_t visibleGridGroup_ = 0;
    bool transactionActive_ = false;
    bool canAddSession_ = true;
    bool hoveringWorkspaceUi_ = false;
    bool pickerOpen_ = false;
    bool applicationPointerCapture_ = false;
    bool controlBarVisible_ = true;
    int controlUiScalePercent_ = 100;
    bool layoutChanged_ = false;
    std::uint64_t presentationRevision_ = 1;
    std::int32_t hoverX_ = 0;
    std::int32_t hoverY_ = 0;
    std::optional<WorkspaceCommand> pendingCommand_;
    std::vector<WorkspaceOverlayRect> overlayRects_;
    std::vector<WorkspaceOverlayLabel> overlayLabels_;
};

inline constexpr std::array<wchar_t, 45> WorkspaceGlyphOrder{
    L'0', L'1', L'2', L'3', L'4', L'5', L'6', L'7', L'8', L'9', L'A', L'B', L'C', L'D', L'E',
    L'F', L'G', L'H', L'I', L'J', L'K', L'L', L'M', L'N', L'O', L'P', L'Q', L'R', L'S', L'T',
    L'U', L'V', L'W', L'X', L'Y', L'Z', L'[', L']', L'-', L'+', L'<', L'>', L'^', L'#', L'\x2196'};

inline constexpr std::size_t WorkspaceGlyphFallbackSlot = WorkspaceGlyphOrder.size();

constexpr std::array<std::uint8_t, 7> WorkspaceGlyphRows(wchar_t character) noexcept
{
    if (character >= L'a' && character <= L'z')
    {
        character = static_cast<wchar_t>(character - L'a' + L'A');
    }
    switch (character)
    {
    case L'0':
        return {14, 17, 19, 21, 25, 17, 14};
    case L'1':
        return {4, 12, 4, 4, 4, 4, 14};
    case L'2':
        return {14, 17, 1, 2, 4, 8, 31};
    case L'3':
        return {30, 1, 1, 14, 1, 1, 30};
    case L'4':
        return {2, 6, 10, 18, 31, 2, 2};
    case L'5':
        return {31, 16, 16, 30, 1, 1, 30};
    case L'6':
        return {14, 16, 16, 30, 17, 17, 14};
    case L'7':
        return {31, 1, 2, 4, 8, 8, 8};
    case L'8':
        return {14, 17, 17, 14, 17, 17, 14};
    case L'9':
        return {14, 17, 17, 15, 1, 1, 14};
    case L'A':
        return {14, 17, 17, 31, 17, 17, 17};
    case L'B':
        return {30, 17, 17, 30, 17, 17, 30};
    case L'C':
        return {14, 17, 16, 16, 16, 17, 14};
    case L'D':
        return {30, 17, 17, 17, 17, 17, 30};
    case L'E':
        return {31, 16, 16, 30, 16, 16, 31};
    case L'F':
        return {31, 16, 16, 30, 16, 16, 16};
    case L'G':
        return {14, 17, 16, 23, 17, 17, 15};
    case L'H':
        return {17, 17, 17, 31, 17, 17, 17};
    case L'I':
        return {14, 4, 4, 4, 4, 4, 14};
    case L'J':
        return {7, 2, 2, 2, 18, 18, 12};
    case L'K':
        return {17, 18, 20, 24, 20, 18, 17};
    case L'L':
        return {16, 16, 16, 16, 16, 16, 31};
    case L'M':
        return {17, 27, 21, 21, 17, 17, 17};
    case L'N':
        return {17, 25, 21, 19, 17, 17, 17};
    case L'O':
        return {14, 17, 17, 17, 17, 17, 14};
    case L'P':
        return {30, 17, 17, 30, 16, 16, 16};
    case L'Q':
        return {14, 17, 17, 17, 21, 18, 13};
    case L'R':
        return {30, 17, 17, 30, 20, 18, 17};
    case L'S':
        return {15, 16, 16, 14, 1, 1, 30};
    case L'T':
        return {31, 4, 4, 4, 4, 4, 4};
    case L'U':
        return {17, 17, 17, 17, 17, 17, 14};
    case L'V':
        return {17, 17, 17, 17, 17, 10, 4};
    case L'W':
        return {17, 17, 17, 21, 21, 21, 10};
    case L'X':
        return {17, 17, 10, 4, 10, 17, 17};
    case L'Y':
        return {17, 17, 10, 4, 4, 4, 4};
    case L'Z':
        return {31, 1, 2, 4, 8, 16, 31};
    case L'[':
        return {14, 8, 8, 8, 8, 8, 14};
    case L']':
        return {14, 2, 2, 2, 2, 2, 14};
    case L'-':
        return {0, 0, 0, 31, 0, 0, 0};
    case L'+':
        return {0, 4, 4, 31, 4, 4, 0};
    case L'<':
        return {2, 4, 8, 16, 8, 4, 2};
    case L'>':
        return {8, 4, 2, 1, 2, 4, 8};
    case L'^':
        return {4, 10, 17, 0, 0, 0, 0};
    case L'#':
        return {21, 31, 21, 31, 21, 0, 0};
    case L'\x2196':
        return {16, 24, 28, 30, 31, 18, 1};
    default:
        return {};
    }
}

constexpr std::size_t WorkspaceGlyphIndex(wchar_t character) noexcept
{
    if (character >= L'a' && character <= L'z')
    {
        character = static_cast<wchar_t>(character - L'a' + L'A');
    }
    for (std::size_t index = 0; index < WorkspaceGlyphOrder.size(); ++index)
    {
        if (WorkspaceGlyphOrder[index] == character)
        {
            return index;
        }
    }
    return WorkspaceGlyphFallbackSlot;
}
