#pragma once

#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace UI::Modern::PC::Chat
{
inline constexpr int TextureIndex = PC::Common::TextureIndex + 8;
}

class LegacyRenderFacade;
class SessionKeeper;
struct SessionInputEvent;
namespace Rml
{
class ElementFormControlInput;
}

namespace UI::Modern
{
struct RmlMuScrollBarMetrics;
}

namespace UI::Modern::PC::Chat
{
inline constexpr std::size_t RmlChatHistoryLimit = 201;
inline constexpr std::size_t RmlChatInputLimit = 89;
std::size_t RmlBlockedChatVisibleRows() noexcept;
float RmlBlockedChatWidth() noexcept;
float RmlBlockedChatHeight() noexcept;

struct RmlChatBlockPosition final
{
    float left = 0.0F;
    float top = 0.0F;
};

enum class RmlChatMessageType : std::uint8_t
{
    Normal,
    Whisper,
    System,
    Error,
    Party,
    Guild,
    Union,
    GameMaster,
    Gens,
};

enum class RmlChatInputType : std::uint8_t
{
    Normal,
    Party,
    Guild,
    Gens,
};

enum class RmlChatSendBlockReason : std::uint8_t
{
    None,
    PartyRequired,
    GuildRequired,
    GensRequired,
};

struct RmlChatLabels final
{
    std::wstring normal;
    std::wstring party;
    std::wstring guild;
    std::wstring gens;
    std::wstring whisper;
    std::wstring system;
    std::wstring block;

    bool operator==(const RmlChatLabels &) const = default;
};

int DefaultRmlChatAlpha() noexcept;

struct RmlChatState final
{
    RmlChatInputType inputType = RmlChatInputType::Normal;
    int viewMode = 0;
    int sizeIndex = 0;
    int alpha = DefaultRmlChatAlpha();
    bool visible = true;
    bool editing = false;
    bool menuExpanded = true;
    bool blockWindowOpen = false;
    bool showNormal = true;
    bool showWhisper = true;
    bool showSystem = true;
    bool showParty = false;
    bool showGuild = false;
    bool showGens = false;

    bool operator==(const RmlChatState &) const = default;
};

int RmlChatVisibleRowCount(int sizeIndex) noexcept;
int NextRmlChatViewMode(int viewMode) noexcept;
int NextRmlChatSizeIndex(int sizeIndex) noexcept;
int NextRmlChatAlpha(int alpha) noexcept;
bool IsRmlChatMessageVisible(RmlChatMessageType type, const RmlChatState &state) noexcept;
void ToggleRmlChatMessageFilter(RmlChatMessageType type, RmlChatState &state) noexcept;
RmlChatBlockPosition CenterRmlChatBlock(int viewportWidth, int viewportHeight,
                                        float scale) noexcept;
RmlChatBlockPosition ClampRmlChatBlock(int viewportWidth, int viewportHeight, float scale,
                                       RmlChatBlockPosition position) noexcept;
RmlChatSendBlockReason RmlChatSendBlockReasonFor(RmlChatInputType type, bool inParty, bool inGuild,
                                                 bool inGens) noexcept;

struct RmlChatSubmit final
{
    std::wstring text;
    std::wstring whisper;
};

struct RmlChatActions final
{
    std::optional<RmlChatSubmit> submit;
    std::optional<std::wstring> whisperTarget;
    int chatHistoryDelta = 0;
    int whisperHistoryDelta = 0;
    bool open = false;
    bool close = false;
    bool toggleMenu = false;
    bool toggleBlock = false;
    bool closeBlock = false;
    bool toggleWhisper = false;
    bool toggleSystem = false;
    bool toggleNormal = false;
    bool toggleParty = false;
    bool toggleGuild = false;
    bool toggleGens = false;
    bool cycleViewMode = false;
    bool cycleSize = false;
    bool cycleAlpha = false;
    std::optional<std::wstring> registerBlockedUser;
    std::optional<std::wstring> deleteBlockedUser;
};

bool HandleRmlChatEnter(const RmlChatState &state, const Rml::ElementFormControlInput &mainInput,
                        const Rml::ElementFormControlInput &whisperInput,
                        const Rml::ElementFormControlInput &blockInput, RmlChatActions &actions);

class RmlChatPanel final
{
  public:
    explicit RmlChatPanel(SessionKeeper &keeper);
    ~RmlChatPanel();

    RmlChatPanel(const RmlChatPanel &) = delete;
    RmlChatPanel &operator=(const RmlChatPanel &) = delete;

    void AddMessage(std::wstring name, std::wstring text, RmlChatMessageType type);
    void ClearMessages();
    void Stage(const RmlChatState &state, const RmlChatLabels &labels);
    void SetMainText(std::wstring text);
    void SetWhisperTarget(std::wstring name);
    void SetBlockedUsers(std::vector<std::wstring> names);
    RmlChatActions TakeActions();

    bool PrepareOnWorker(int viewportWidth, int viewportHeight);
    bool ProcessInput(const SessionInputEvent &event);
    bool HasTextInputFocus() const noexcept;
    std::optional<RmlTextInputArea> TextInputArea() const;
    bool Record(LegacyRenderFacade &facade) const;
    void Release();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Chat

class LegacyRenderFacade;
class SessionKeeper;
struct SessionInputEvent;

namespace UI::Modern::PC::Friend
{
enum class RmlFriendPanelMode
{
    Main,
    Chat,
    WriteLetter,
    ReadLetter,
};

struct RmlFriendRow final
{
    std::array<std::wstring, 4> text;
    bool checked = false;
    bool read = false;

    bool operator==(const RmlFriendRow &) const = default;
};

int RmlFriendInitialX() noexcept;
int RmlFriendInitialY() noexcept;

struct RmlFriendPanelContent final
{
    int x = RmlFriendInitialX();
    int y = RmlFriendInitialY();
    std::wstring title;
    std::array<std::wstring, 3> tabs;
    std::wstring refuseLabel;
    std::array<std::wstring, 5> labels;
    std::array<std::wstring, 4> headers;
    std::vector<RmlFriendRow> rows;
    std::size_t selectedRow = 0;
    std::size_t scroll = 0;
    int tab = 0;
    bool refuseChat = false;
    bool checkAll = false;
    bool mailRows = false;
    bool inviteOpen = false;
    bool inputLocked = false;
    std::vector<std::wstring> messages;
    std::vector<std::wstring> members;
    std::vector<std::wstring> invitees;
    std::size_t selectedInvitee = 0;
    std::wstring receiver;
    std::wstring subject;
    std::wstring body;
    std::wstring senderLabel;
    std::wstring sender;

    bool operator==(const RmlFriendPanelContent &) const = default;
};

struct RmlFriendPanelChanges final
{
    std::optional<int> tab;
    std::optional<int> action;
    std::optional<int> sortColumn;
    std::optional<std::size_t> selectedRow;
    std::optional<std::size_t> checkedRow;
    std::optional<std::size_t> scroll;
    std::optional<std::size_t> selectedInvitee;
    std::optional<std::wstring> receiver;
    std::optional<std::wstring> subject;
    std::optional<std::wstring> body;
    std::optional<int> x;
    std::optional<int> y;
    bool close = false;
    bool minimize = false;
    bool toggleRefuseChat = false;
    bool toggleCheckAll = false;
    bool sendChat = false;
};

class RmlFriendPanel final
{
  public:
    static std::size_t ChatVisibleLines() noexcept;
    static std::size_t MainVisibleRows() noexcept;
    static std::size_t VisibleRows() noexcept;
    static int MainWidth() noexcept;
    static int MainHeight() noexcept;
    static int ChatWidth() noexcept;
    static int ChatCollapsedWidth() noexcept;
    static int ChatHeight() noexcept;
    static int WriteWidth() noexcept;
    static int WriteHeight() noexcept;
    static int ReadWidth() noexcept;
    static int ReadHeight() noexcept;

    RmlFriendPanel(SessionKeeper &keeper, RmlFriendPanelMode mode);
    ~RmlFriendPanel();

    RmlFriendPanel(const RmlFriendPanel &) = delete;
    RmlFriendPanel &operator=(const RmlFriendPanel &) = delete;

    void Create();
    void Release();
    bool ProcessInput(const SessionInputEvent &event);
    RmlFriendPanelChanges TakeChanges();
    std::optional<RmlTextInputArea> TextInputArea() const;
    bool PrepareOnWorker(int viewportWidth, int viewportHeight, bool visible,
                         const RmlFriendPanelContent &content);
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;

    std::array<RmlMuButton, 14> buttons_;
    std::vector<RmlMuButton> rows_;
    RmlMuScrollBar scrollBar_;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Friend

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;
namespace UI::Modern::PC::Guild
{
class RmlGuildCreatePanel final
{
  public:
    enum class Step
    {
        Info,
        Edit,
        Review
    };
    struct Content
    {
        std::uint64_t revision = 0;
        std::array<std::wstring, 10> labels;
        std::wstring name;
        RmlMuPalette::Pixels mark{};
        Step step = Step::Info;
        std::uint8_t color = 0;
    };
    struct Changes
    {
        std::optional<std::wstring> name;
        std::optional<RmlMuPalette::Pixels> mark;
        std::optional<std::uint8_t> color;
        Step step = Step::Info;
        bool create = false, previous = false, next = false, close = false, focus = false;
    };
    explicit RmlGuildCreatePanel(SessionKeeper &keeper);
    ~RmlGuildCreatePanel();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, const Content &content);
    bool ProcessInput(const SessionInputEvent &event);
    Changes TakeChanges();
    std::optional<RmlTextInputArea> TextInputArea() const;
    bool ContainsReferencePointer(int x, int y) const;
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Guild

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;
namespace UI::Modern::PC::Guild
{
class RmlGuildInfoPanel final
{
  public:
    enum class Tab
    {
        Info,
        Members,
        Alliance,
        Rival
    };
    enum class Action
    {
        Leave,
        Appoint,
        Demote,
        Kick,
        RemoveAllianceGuild,
        LeaveAlliance,
        AddRival,
        RemoveRival
    };
    struct Content
    {
        std::map<std::string, std::wstring> text;
        RmlMuScrollingList::Data members, alliance, rivals;
        std::vector<RmlMuPalette::Pixels> allianceMarks;
        RmlMuPalette::Pixels mark{};
        std::optional<std::size_t> selectedMember, selectedAlliance;
        Tab tab = Tab::Members;
        bool hasGuild = false, hasAlliance = false, master = false;
        std::array<bool, 8> enabled{};
        bool operator==(const Content &) const = default;
    };
    struct Changes
    {
        std::optional<Tab> tab;
        std::optional<Action> action;
        std::optional<std::size_t> member, alliance;
        std::uint64_t revision = 0;
        bool close = false, focus = false;
    };
    explicit RmlGuildInfoPanel(SessionKeeper &keeper);
    ~RmlGuildInfoPanel();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, std::uint64_t revision,
                         const Content &content);
    bool ProcessInput(const SessionInputEvent &event);
    Changes TakeChanges();
    bool ContainsReferencePointer(int x, int y) const;
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Guild

namespace UI::Modern
{
enum class PartyFrameMetric
{
    ReferenceWidth,
    ReferenceHeight,
    HeaderWidth,
    HeaderHeight,
    MemberX,
    MemberWidth,
    MemberHeight,
    FirstRowY,
    RowStep,
    MinimizeX,
    MinimizeY,
    MinimizeSize,
    HpX,
    HpY,
    HpWidth,
    HpHeight,
    MpX,
    MpY,
    MpWidth,
    MpHeight,
    OverlayX,
    OverlayY,
    OverlayWidth,
    OverlayHeight,
    CrownX,
    CrownY,
    CrownWidth,
    CrownHeight,
    LeaveX,
    LeaveY,
    LeaveSize,
    BackgroundHeight,
    InitialY,
    WorldHpBarWidth,
    WorldHpRaise,
    WorldHpHoverY,
    WorldHpTextY,
    WorldHpTextColor,
    WorldHpShadowRect,
    WorldHpFrameRect,
    WorldHpTrackRect,
    WorldHpStepRect,
    WorldHpStepStride,
    WorldHpShadowColor,
    WorldHpFrameColor,
    WorldHpTrackColor,
    WorldHpStepColor,
};

const RmlUiDesign &PartyFrameDesign();
} // namespace UI::Modern

namespace UI::Modern::PC::Party
{
inline constexpr int PartyFrameTextureIndex = PC::Common::TextureIndex + 5;
}

class LegacyRenderFacade;
class SessionKeeper;

namespace UI::Modern
{
struct RmlPartyFrameRow final
{
    static constexpr std::size_t NameCapacity = 64;

    wchar_t name[NameCapacity]{};
    int currentHp = 0;
    int maximumHp = 0;
    bool outOfViewport = false;
    bool leader = false;
    bool canLeave = false;
    ButtonVisualState leaveButton = ButtonVisualState::Up;
    ButtonVisualState memberButton = ButtonVisualState::Up;
};

struct RmlPartyFrameRequest final
{
    static constexpr int RowCapacity = 5;

    std::array<RmlPartyFrameRow, RowCapacity> rows{};
    float x = 0.0F;
    float y = 0.0F;
    int rowCount = 0;
    bool visible = false;
    bool minimized = false;
    ButtonVisualState minimizeButton = ButtonVisualState::Up;
};

class RmlPartyFrameLayer final
{
  public:
    explicit RmlPartyFrameLayer(SessionKeeper &keeper);
    ~RmlPartyFrameLayer();

    RmlPartyFrameLayer(const RmlPartyFrameLayer &) = delete;
    RmlPartyFrameLayer &operator=(const RmlPartyFrameLayer &) = delete;

    void Stage(const RmlPartyFrameRequest &request) noexcept;
    bool PrepareOnWorker(int viewportWidth, int viewportHeight);
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern
