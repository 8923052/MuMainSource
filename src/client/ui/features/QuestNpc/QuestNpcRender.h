#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;
namespace UI::Modern::PC::NPCs
{
class RmlGatekeeperPanel final
{
  public:
    struct Content
    {
        std::uint64_t revision = 0;
        std::array<std::wstring, 12> labels;
        int feeIndex = 0, maximumFeeIndex = 0;
        bool publicAccess = false, canManage = false, canEnter = false;
    };
    struct Changes
    {
        bool close = false, focus = false, enter = false, confirm = false;
        std::optional<int> feeIndex;
        std::optional<bool> publicAccess;
    };
    explicit RmlGatekeeperPanel(SessionKeeper &keeper);
    ~RmlGatekeeperPanel();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, const Content &content);
    bool ProcessInput(const SessionInputEvent &event);
    Changes TakeChanges();
    bool ContainsReferencePointer(int x, int y) const;
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::NPCs

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;

namespace UI::Modern::PC::NPCs
{
class RmlNpcDialoguePanel final
{
  public:
    struct Content
    {
        std::uint64_t revision = 0;
        std::wstring title, dialogue, contribution;
        std::vector<std::wstring> choices;
        bool canChoose = true;
    };
    struct Changes
    {
        std::optional<std::size_t> choice;
        std::uint64_t revision = 0;
        bool close = false, focus = false;
    };
    explicit RmlNpcDialoguePanel(SessionKeeper &keeper);
    ~RmlNpcDialoguePanel();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, const Content &content);
    bool ProcessInput(const SessionInputEvent &event);
    Changes TakeChanges();
    bool ContainsReferencePointer(int x, int y) const;
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::NPCs

namespace UI::Modern::PC::Quests
{
struct QuestTextRow
{
    std::wstring text;
    std::string style;
    std::size_t sourceIndex = 0;
    bool operator==(const QuestTextRow &) const = default;
};
std::string QuestRowsMarkup(const std::vector<QuestTextRow> &rows);
} // namespace UI::Modern::PC::Quests

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;

namespace UI::Modern::PC::Quests
{
class RmlJobChangePanel final
{
  public:
    enum class Mode
    {
        None,
        Zen,
        Monsters,
        Item,
        Items
    };
    struct Requirement
    {
        std::wstring text;
        bool complete = false;
        bool operator==(const Requirement &) const = default;
    };
    struct Content
    {
        std::wstring title, questTitle, dialogue, costLabel, cost, completeLabel;
        std::vector<std::wstring> choices;
        std::vector<Requirement> requirements;
        Mode mode = Mode::None;
        bool complete = false, canChoose = true;
        bool operator==(const Content &) const = default;
    };
    struct Changes
    {
        std::uint64_t revision = 0;
        std::optional<std::size_t> choice;
        bool complete = false, close = false, focus = false;
    };
    struct Rect
    {
        float x = 0, y = 0, width = 0, height = 0;
    };
    explicit RmlJobChangePanel(SessionKeeper &keeper);
    ~RmlJobChangePanel();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, std::uint64_t revision,
                         const Content &content);
    bool ProcessInput(const SessionInputEvent &event);
    Changes TakeChanges();
    bool ContainsReferencePointer(int x, int y) const;
    const std::array<Rect, 3> &ItemRects() const;
    float ItemPresentationScale() const;
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Quests

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;
namespace UI::Modern::PC::Quests
{
class RmlQuestJournalPanel final
{
  public:
    enum class Tab
    {
        Quests,
        JobChange,
        CastleTemple
    };
    struct Content
    {
        std::wstring title, questLabel, jobLabel, eventsLabel, startLabel, giveUpLabel;
        std::wstring summary, requirementsLabel, rewardsLabel;
        std::wstring jobTitle, jobDialogue, jobState, castleLabel, templeLabel, castle, temple;
        std::vector<std::wstring> quests;
        std::vector<QuestTextRow> requirements, rewards;
        std::optional<std::size_t> selected;
        Tab tab = Tab::Quests;
        bool canStart = false, canGiveUp = false;
        bool operator==(const Content &) const = default;
    };
    struct Changes
    {
        std::uint64_t revision = 0;
        std::optional<std::size_t> selected;
        std::optional<Tab> tab;
        bool start = false, giveUp = false, close = false, focus = false;
    };
    explicit RmlQuestJournalPanel(SessionKeeper &keeper);
    ~RmlQuestJournalPanel();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, std::uint64_t revision,
                         const Content &content);
    bool ProcessInput(const SessionInputEvent &event);
    Changes TakeChanges();
    std::optional<std::size_t> HoveredReward(std::uint64_t revision) const;
    bool ContainsReferencePointer(int x, int y) const;
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Quests

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;

namespace UI::Modern::PC::Quests
{
class RmlQuestProgressPanel final
{
  public:
    using Row = QuestTextRow;
    struct Content
    {
        std::wstring title, questTitle, npc, dialogue, player, playerWords, requirementsTitle,
            rewardsTitle, confirmLabel;
        std::vector<std::wstring> choices;
        std::vector<Row> requirements, rewards;
        bool complete = false, canChoose = true;
        bool operator==(const Content &) const = default;
    };
    struct Changes
    {
        std::uint64_t revision = 0;
        std::optional<std::size_t> choice;
        bool complete = false, close = false, focus = false;
    };
    explicit RmlQuestProgressPanel(SessionKeeper &keeper, bool byItem);
    ~RmlQuestProgressPanel();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, std::uint64_t revision,
                         std::uint64_t branch, const Content &content);
    bool ProcessInput(const SessionInputEvent &event);
    Changes TakeChanges();
    std::optional<std::size_t> HoveredReward(std::uint64_t revision) const;
    bool ContainsReferencePointer(int x, int y) const;
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Quests

class CQuestMng;
struct tagITEM;
namespace SEASON3B
{
struct QuestRewardPresentation
{
    static constexpr std::size_t MaximumLines =
        13; // Five requirements, five rewards, three headings.
    std::vector<UI::Modern::PC::Quests::QuestTextRow> requirements, rewards;
    std::array<tagITEM *, MaximumLines> items{};
    bool complete = false;
};
QuestRewardPresentation BuildQuestRewardPresentation(CQuestMng &quests, std::uint32_t index);
} // namespace SEASON3B
