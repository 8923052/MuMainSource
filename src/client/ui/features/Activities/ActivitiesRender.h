#pragma once
#include "ui/runtime/UiControls.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;
namespace UI::Modern::PC::Combat
{
class RmlDuelWatchPanel final
{
  public:
    struct Content
    {
        std::uint64_t revision = 0;
        std::array<std::wstring, 14> labels;
        std::array<bool, 4> enabled{};
    };
    struct Changes
    {
        int channel = -1;
        bool close = false, focus = false;
    };
    explicit RmlDuelWatchPanel(SessionKeeper &keeper);
    ~RmlDuelWatchPanel();
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
} // namespace UI::Modern::PC::Combat

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;
namespace UI::Modern::PC::Events
{
class RmlInteractionProgressPanel final
{
  public:
    explicit RmlInteractionProgressPanel(SessionKeeper &keeper);
    ~RmlInteractionProgressPanel();
    void Release();
    bool PrepareOnWorker(int width, int height, const std::wstring &message, unsigned elapsed,
                         unsigned duration);
    bool ProcessInput(const SessionInputEvent &event);
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Events

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;
namespace UI::Modern::PC::Events
{
class RmlTempleInfoPanel final
{
  public:
    static constexpr int SkillCount = 4;
    struct Content
    {
        std::array<std::wstring, 3> labels;
        std::array<RmlSkillIconState, SkillCount> icons;
        int selected = 0;
        bool operator==(const Content &) const = default;
    };
    struct Changes
    {
        std::optional<int> selected;
        int wheel = 0;
    };
    explicit RmlTempleInfoPanel(SessionKeeper &keeper);
    ~RmlTempleInfoPanel();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, const Content &content);
    bool ProcessInput(const SessionInputEvent &event);
    Changes TakeChanges();
    int HoveredSkill() const;
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Events

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;
namespace UI::Modern::PC::Events
{
class RmlTempleResultPanel final
{
  public:
    struct Content
    {
        std::uint64_t revision = 0;
        std::array<std::wstring, 7> labels;
        std::array<RmlMuScrollingList::Data, 2> rows;
        std::array<std::optional<std::size_t>, 2> heroRows;
        int winState = 0;
    };
    explicit RmlTempleResultPanel(SessionKeeper &keeper);
    ~RmlTempleResultPanel();
    void Release();
    bool PrepareOnWorker(int width, int height, bool visible, const Content &content);
    bool ProcessInput(const SessionInputEvent &event);
    bool TakeClose();
    bool CloseHovered() const;
    std::optional<std::pair<int, std::size_t>> HoveredRow() const;
    bool ContainsReferencePointer(int x, int y) const;
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Events

class SessionKeeper;
class LegacyRenderFacade;
struct SessionInputEvent;
namespace UI::Modern::PC::Events
{
class RmlTempleScorePanel final
{
  public:
    explicit RmlTempleScorePanel(SessionKeeper &keeper);
    ~RmlTempleScorePanel();
    void Release();
    unsigned HoldMilliseconds() const;
    bool PrepareOnWorker(int width, int height, bool visible, const std::array<int, 2> &scores,
                         const std::array<std::wstring, 2> &teams);
    void ProcessInput(const SessionInputEvent &event);
    bool Record(LegacyRenderFacade &facade) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern::PC::Events
