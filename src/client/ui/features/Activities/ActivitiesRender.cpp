#include "ui/features/Activities/ActivitiesRender.h"
#include "I18N/All.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationNetwork.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "data/WorldData.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/WorldSimulation.h"
#include "render/ModelResources.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionRender.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Activities/ActivitiesLogic.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Hud/HudRender.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"
#include "ui/session/UiSessionRender.h"

namespace UI::Modern::PC::Combat
{
class RmlDuelWatchPanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "Combat", "duel_watch.rml")),
          design_(path_, {"Panel-Size", "Panel-Reference", "Panel-Initial"}),
          host_(keeper, "duel-watch-" + std::to_string(keeper.Id().RawValue()), path_)
    {
    }
    ~Impl()
    {
        Release();
    }
    void Release()
    {
        for (auto &button : buttons_)
            button.Unbind();
        movable_.Unbind();
        panel_ = nullptr;
        labels_.fill(nullptr);
        revision_.reset();
        visible_ = positioned_ = inputDirty_ = false;
        changes_ = {};
        left_ = top_ = width_ = height_ = 0;
        host_.Release();
    }
    bool Bind()
    {
        auto &document = *host_.Document();
        panel_ = document.GetElementById("panel");
        auto *drag = document.GetElementById("btnDrag");
        auto *close = document.GetElementById("btnClose");
        if (!panel_ || !drag || !close)
            return false;
        movable_.Bind(*panel_, *drag);
        buttons_.back().Bind(*close);
        for (std::size_t i = 0; i < ChannelCount; ++i)
        {
            auto *button = document.GetElementById("btnDuelWatch" + std::to_string(i));
            if (!button)
                return false;
            buttons_[i].Bind(*button);
        }
        constexpr std::array ids{"tfTitle",         "tfExplain",           "tfDuelTitle0",
                                 "tfProgressDuel0", "btnDuelWatch0-label", "tfDuelTitle1",
                                 "tfProgressDuel1", "btnDuelWatch1-label", "tfDuelTitle2",
                                 "tfProgressDuel2", "btnDuelWatch2-label", "tfDuelTitle3",
                                 "tfProgressDuel3", "btnDuelWatch3-label"};
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            labels_[i] = document.GetElementById(ids[i]);
            if (!labels_[i])
                return false;
        }
        return true;
    }
    bool ApplyContent(const Content &content)
    {
        if (revision_ == content.revision)
            return false;
        for (std::size_t i = 0; i < labels_.size(); ++i)
            labels_[i]->SetInnerRML(Rml::StringUtilities::EncodeRml(
                StringUtils::WideToNarrow(content.labels[i].c_str())));
        for (std::size_t i = 0; i < ChannelCount; ++i)
            buttons_[i].SetEnable(content.enabled[i]);
        revision_ = content.revision;
        return true;
    }
    void PublishBounds()
    {
        const auto position = movable_.Position();
        const auto viewport = host_.Viewport();
        const auto reference = design_.Values(1), size = design_.Values(0);
        left_ = position.left * reference[0] / viewport.width;
        top_ = position.top * reference[1] / viewport.height;
        width_ = size[0] * reference[0] / viewport.width;
        height_ = size[1] * reference[1] / viewport.height;
    }
    bool Prepare(int width, int height, bool visible, const Content &content)
    {
        if (!panel_ && !visible)
            return true;
        const auto size = design_.Values(0);
        if (!host_.Ensure(width, height, size[0], size[1]))
            return false;
        if (!panel_ && !Bind())
        {
            Release();
            return false;
        }
        if (!host_.SetVisible(visible))
            return false;
        visible_ = visible;
        bool dirty = ApplyContent(content) || inputDirty_;
        const auto viewport = host_.Viewport();
        movable_.Configure(viewport.width, viewport.height, size[0], size[1]);
        if (!positioned_)
        {
            const auto initial = design_.Values(2);
            movable_.SetPosition(initial[0], initial[1]);
            positioned_ = true;
        }
        if (!visible)
            movable_.CancelDrag();
        dirty = movable_.TakeDirty() || dirty;
        for (auto &button : buttons_)
            dirty = button.SyncVisualState() || dirty;
        if (dirty)
            host_.Document()->GetContext()->Update();
        inputDirty_ = false;
        PublishBounds();
        return host_.CaptureIfDirty(dirty);
    }
    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!visible_)
            return false;
        const bool dragging = movable_.IsDragging();
        host_.ProcessInput(event);
        bool inside = false;
        for (auto *element = host_.HoverElement(); element; element = element->GetParentNode())
            if (element == panel_)
            {
                inside = true;
                break;
            }
        changes_.close = buttons_.back().IsClick() || changes_.close;
        for (std::size_t i = 0; i < ChannelCount; ++i)
            if (buttons_[i].IsClick())
                changes_.channel = int(i);
        if (inside && event.action == SessionInputAction::PointerButton && event.pressed)
            changes_.focus = true;
        inputDirty_ = true;
        PublishBounds();
        return event.kind == SessionInputEventKind::Pointer &&
               (inside || dragging || movable_.IsDragging());
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    RmlMuMovablePanel movable_;
    static constexpr std::size_t ChannelCount = 4;
    std::array<RmlMuButton, ChannelCount + 1> buttons_;
    Rml::Element *panel_ = nullptr;
    std::array<Rml::Element *, 14> labels_{};
    std::optional<std::uint64_t> revision_;
    Changes changes_;
    float left_ = 0, top_ = 0, width_ = 0, height_ = 0;
    bool visible_ = false, positioned_ = false, inputDirty_ = false;
};
RmlDuelWatchPanel::RmlDuelWatchPanel(SessionKeeper &keeper) : impl_(std::make_unique<Impl>(keeper))
{
}
RmlDuelWatchPanel::~RmlDuelWatchPanel() = default;
void RmlDuelWatchPanel::Release()
{
    impl_->Release();
}
bool RmlDuelWatchPanel::PrepareOnWorker(int width, int height, bool visible, const Content &content)
{
    return impl_->Prepare(width, height, visible, content);
}
bool RmlDuelWatchPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
RmlDuelWatchPanel::Changes RmlDuelWatchPanel::TakeChanges()
{
    return std::exchange(impl_->changes_, {});
}
bool RmlDuelWatchPanel::ContainsReferencePointer(int x, int y) const
{
    return impl_->visible_ && x >= impl_->left_ && y >= impl_->top_ &&
           x < impl_->left_ + impl_->width_ && y < impl_->top_ + impl_->height_;
}
bool RmlDuelWatchPanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->panel_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::Combat

namespace UI::Modern::PC::Events
{
class RmlInteractionProgressPanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "Events", "interaction_progress.rml")),
          design_(path_, {"Panel-Size", "Progress-Frames"}),
          host_(keeper,
                "interaction-progress-" + std::to_string(keeper.Id().RawValue()) + "-" +
                    std::to_string(reinterpret_cast<std::uintptr_t>(this)),
                path_)
    {
    }
    ~Impl()
    {
        Release();
    }
    void Release()
    {
        progress_.Unbind();
        text_.Unbind();
        bound_ = false;
        message_.clear();
        host_.Release();
    }
    bool Prepare(int width, int height, const std::wstring &message, unsigned elapsed,
                 unsigned duration)
    {
        const auto size = design_.Values(0);
        if (!host_.Ensure(width, height, size[0], size[1]))
            return false;
        bool dirty = !bound_ || inputDirty_;
        inputDirty_ = false;
        if (!bound_)
        {
            auto *fill = host_.Document()->GetElementById("pbGauge-fill");
            if (!fill || !text_.Bind(*host_.Document(), "taMent-label"))
            {
                Release();
                return false;
            }
            progress_.Bind(*fill);
            bound_ = true;
        }
        if (!host_.SetVisible(true))
            return false;
        if (message_ != message)
        {
            text_.SetMarkup(
                Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(message.c_str())));
            message_ = message;
            dirty = true;
        }
        dirty = progress_.SetProgressFrames(elapsed, duration, design_.Number<int>(1)) || dirty;
        dirty = text_.Apply() || dirty;
        return host_.CaptureIfDirty(dirty);
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    RmlMuProgressBar progress_;
    RmlMuTextArea text_;
    std::wstring message_;
    bool bound_ = false, inputDirty_ = false;
};
RmlInteractionProgressPanel::RmlInteractionProgressPanel(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper))
{
}
RmlInteractionProgressPanel::~RmlInteractionProgressPanel() = default;
void RmlInteractionProgressPanel::Release()
{
    impl_->Release();
}
bool RmlInteractionProgressPanel::PrepareOnWorker(int width, int height,
                                                  const std::wstring &message, unsigned elapsed,
                                                  unsigned duration)
{
    return impl_->Prepare(width, height, message, elapsed, duration);
}
bool RmlInteractionProgressPanel::ProcessInput(const SessionInputEvent &event)
{
    if (!impl_->bound_)
        return false;
    impl_->host_.ProcessInput(event);
    impl_->inputDirty_ = true;
    for (auto *element = impl_->host_.HoverElement(); element; element = element->GetParentNode())
        if (element->GetId() == "panel")
            return event.kind == SessionInputEventKind::Pointer;
    return false;
}
bool RmlInteractionProgressPanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->bound_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::Events

namespace UI::Modern::PC::Events
{
class RmlTempleInfoPanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "Events", "temple_info.rml")),
          design_(path_, {"Panel-Size", "Skill-Columns"}),
          host_(keeper, "temple-info-" + std::to_string(keeper.Id().RawValue()), path_)
    {
    }
    ~Impl()
    {
        Release();
    }
    void Release()
    {
        for (auto &slot : slots_)
            slot.Unbind();
        labels_.fill(nullptr);
        panel_ = nullptr;
        content_.reset();
        visible_ = dirty_ = false;
        changes_ = {};
        hovered_ = -1;
        host_.Release();
    }
    bool Bind()
    {
        auto &document = *host_.Document();
        panel_ = document.GetElementById("panel");
        constexpr std::array ids{"tfAlliedScore-label", "tfIllusionScore-label",
                                 "tfMatchTime-label"};
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            labels_[i] = document.GetElementById(ids[i]);
            if (!labels_[i])
                return false;
        }
        for (int i = 0; i < SkillCount; ++i)
        {
            const auto id = "ssMapSkillSlot" + std::to_string(i);
            auto *root = document.GetElementById(id);
            auto *button = document.GetElementById(id + "-button");
            auto *icon = document.GetElementById(id + "-icon");
            if (!root || !button || !icon)
                return false;
            slots_[i].Bind(*root, *button, *icon, design_.Number<int>(1));
        }
        return panel_ != nullptr;
    }
    bool Prepare(int width, int height, bool visible, const Content &next)
    {
        if (!panel_ && !visible)
            return true;
        const auto size = design_.Values(0);
        if (!host_.Ensure(width, height, size[0], size[1]))
            return false;
        if (!panel_ && !Bind())
        {
            Release();
            return false;
        }
        if (!host_.SetVisible(visible))
            return false;
        visible_ = visible;
        if (!visible)
        {
            changes_ = {};
            hovered_ = -1;
        }
        for (std::size_t i = 0; i < labels_.size(); ++i)
        {
            if (content_ && content_->labels[i] == next.labels[i])
                continue;
            labels_[i]->SetInnerRML(
                Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(next.labels[i].c_str())));
            dirty_ = true;
        }
        constexpr int SelectedSecondary = 2;
        for (int i = 0; i < SkillCount; ++i)
            dirty_ = slots_[i].Apply(next.icons[i], next.selected == i ? SelectedSecondary : 0,
                                     visible) ||
                     dirty_;
        content_ = next;
        const bool captured = host_.CaptureIfDirty(dirty_);
        dirty_ = false;
        return captured;
    }
    bool Input(const SessionInputEvent &event)
    {
        if (!visible_)
            return false;
        host_.ProcessInput(event);
        auto *hover = host_.HoverElement();
        hovered_ = -1;
        for (int i = 0; i < SkillCount; ++i)
        {
            if (slots_[i].TakeClick())
                changes_.selected = i;
            if (slots_[i].OwnsPointer(hover))
                hovered_ = i;
        }
        bool inside = false;
        for (auto *node = hover; node; node = node->GetParentNode())
            if (node == panel_)
            {
                inside = true;
                break;
            }
        if (inside && event.action == SessionInputAction::PointerWheel)
            changes_.wheel += static_cast<int>(event.wheel);
        dirty_ = true;
        return event.kind == SessionInputEventKind::Pointer && inside;
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    Rml::Element *panel_ = nullptr;
    std::array<Rml::Element *, 3> labels_{};
    std::array<RmlMuSkillSlot, SkillCount> slots_;
    std::optional<Content> content_;
    Changes changes_;
    int hovered_ = -1;
    bool visible_ = false, dirty_ = false;
};
RmlTempleInfoPanel::RmlTempleInfoPanel(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper))
{
}
RmlTempleInfoPanel::~RmlTempleInfoPanel() = default;
void RmlTempleInfoPanel::Release()
{
    impl_->Release();
}
bool RmlTempleInfoPanel::PrepareOnWorker(int width, int height, bool visible,
                                         const Content &content)
{
    return impl_->Prepare(width, height, visible, content);
}
bool RmlTempleInfoPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->Input(event);
}
RmlTempleInfoPanel::Changes RmlTempleInfoPanel::TakeChanges()
{
    return std::exchange(impl_->changes_, {});
}
int RmlTempleInfoPanel::HoveredSkill() const
{
    return impl_->hovered_;
}
bool RmlTempleInfoPanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->panel_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::Events

namespace UI::Modern::PC::Events
{
class RmlTempleResultPanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "Events", "temple_result.rml")),
          design_(path_, {"Panel-Size", "Panel-Reference"}),
          host_(keeper, "temple-result-" + std::to_string(keeper.Id().RawValue()), path_)
    {
    }
    ~Impl()
    {
        Release();
    }
    void Release()
    {
        for (auto &list : lists_)
            list.Unbind();
        close_.Unbind();
        labels_.fill(nullptr);
        panel_ = win_ = lose_ = nullptr;
        revision_.reset();
        hovered_.reset();
        visible_ = dirty_ = closeRequested_ = closeHovered_ = false;
        host_.Release();
    }
    bool Bind()
    {
        auto &document = *host_.Document();
        panel_ = document.GetElementById("panel");
        win_ = document.GetElementById("mcWinResult");
        lose_ = document.GetElementById("mcLoseResult");
        auto *close = document.GetElementById("btnClose");
        if (!panel_ || !win_ || !lose_ || !close)
            return false;
        close_.Bind(*close);
        constexpr std::array ids{"tfTitle-label",         "tfAlliedTeam-label",
                                 "tfIllusionTeam-label",  "tfAlliedChararcter-label",
                                 "tfAlliedClass-label",   "tfAlliedPoint-label",
                                 "btnClose-label",        "tfIllusionChararcter-label",
                                 "tfIllusionClass-label", "tfIllusionPoint-label"};
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            labels_[i] = document.GetElementById(ids[i]);
            if (!labels_[i])
                return false;
        }
        return lists_[0].Bind(document, "slAlliedTeamInfo", nullptr, "result-row") &&
               lists_[1].Bind(document, "slIllusionTeamInfo", nullptr, "result-row");
    }
    bool Apply(const Content &content)
    {
        if (revision_ == content.revision)
            return false;
        hovered_.reset();
        constexpr std::array<std::size_t, 10> mapping{0, 1, 2, 3, 4, 5, 6, 3, 4, 5};
        for (std::size_t i = 0; i < mapping.size(); ++i)
            labels_[i]->SetInnerRML(Rml::StringUtilities::EncodeRml(
                StringUtils::WideToNarrow(content.labels[mapping[i]].c_str())));
        win_->SetProperty("display", content.winState == 1 ? "block" : "none");
        lose_->SetProperty("display", content.winState == 2 ? "block" : "none");
        heroRows_ = content.heroRows;
        for (std::size_t i = 0; i < lists_.size(); ++i)
        {
            lists_[i].SetData(content.rows[i]);
            lists_[i].Select(heroRows_[i]);
            lists_[i].ScrollToStart();
        }
        revision_ = content.revision;
        return true;
    }
    bool Prepare(int width, int height, bool visible, const Content &content)
    {
        if (!panel_ && !visible)
            return true;
        const auto size = design_.Values(0);
        if (!host_.Ensure(width, height, size[0], size[1]))
            return false;
        if (!panel_ && !Bind())
        {
            Release();
            return false;
        }
        if (!host_.SetVisible(visible))
            return false;
        visible_ = visible;
        if (!visible)
        {
            close_.Reset();
            closeRequested_ = closeHovered_ = false;
            hovered_.reset();
        }
        dirty_ = Apply(content) || dirty_;
        dirty_ = close_.SyncVisualState() || dirty_;
        if (dirty_)
            host_.Document()->GetContext()->Update();
        for (auto &list : lists_)
            dirty_ = list.Apply() || dirty_;
        const auto viewport = host_.Viewport();
        const auto reference = design_.Values(1);
        const auto offset = panel_->GetAbsoluteOffset();
        bounds_ = {
            offset.x * reference[0] / viewport.width, offset.y * reference[1] / viewport.height,
            size[0] * reference[0] / viewport.width, size[1] * reference[1] / viewport.height};
        const bool captured = host_.CaptureIfDirty(dirty_);
        dirty_ = false;
        return captured;
    }
    bool Input(const SessionInputEvent &event)
    {
        if (!visible_)
            return false;
        host_.ProcessInput(event);
        auto *hover = host_.HoverElement();
        hovered_.reset();
        closeHovered_ = close_.OwnsPointer(hover);
        closeRequested_ = close_.IsClick() || closeRequested_;
        for (int i = 0; i < static_cast<int>(lists_.size()); ++i)
        {
            lists_[i].ProcessInput(event, hover);
            lists_[i].Select(heroRows_[i]);
            if (auto row = lists_[i].Hovered())
                hovered_ = std::pair{i, *row};
        }
        dirty_ = true;
        for (auto *node = hover; node; node = node->GetParentNode())
            if (node == panel_)
                return event.kind == SessionInputEventKind::Pointer;
        return false;
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    std::array<RmlMuScrollingList, 2> lists_;
    std::array<std::optional<std::size_t>, 2> heroRows_;
    RmlMuButton close_;
    Rml::Element *panel_ = nullptr, *win_ = nullptr, *lose_ = nullptr;
    std::array<Rml::Element *, 10> labels_{};
    std::optional<std::uint64_t> revision_;
    std::optional<std::pair<int, std::size_t>> hovered_;
    std::array<float, 4> bounds_{};
    bool visible_ = false, dirty_ = false, closeRequested_ = false, closeHovered_ = false;
};
RmlTempleResultPanel::RmlTempleResultPanel(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper))
{
}
RmlTempleResultPanel::~RmlTempleResultPanel() = default;
void RmlTempleResultPanel::Release()
{
    impl_->Release();
}
bool RmlTempleResultPanel::PrepareOnWorker(int width, int height, bool visible,
                                           const Content &content)
{
    return impl_->Prepare(width, height, visible, content);
}
bool RmlTempleResultPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->Input(event);
}
bool RmlTempleResultPanel::TakeClose()
{
    return std::exchange(impl_->closeRequested_, false);
}
bool RmlTempleResultPanel::CloseHovered() const
{
    return impl_->closeHovered_;
}
std::optional<std::pair<int, std::size_t>> RmlTempleResultPanel::HoveredRow() const
{
    return impl_->hovered_;
}
bool RmlTempleResultPanel::ContainsReferencePointer(int x, int y) const
{
    const auto &b = impl_->bounds_;
    return impl_->visible_ && x >= b[0] && y >= b[1] && x < b[0] + b[2] && y < b[1] + b[3];
}
bool RmlTempleResultPanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->panel_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::Events

namespace UI::Modern::PC::Events
{
class RmlTempleScorePanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "Events", "temple_score.rml")),
          design_(path_, {"Panel-Size", "Score-HoldMilliseconds"}),
          host_(keeper, "temple-score-" + std::to_string(keeper.Id().RawValue()), path_)
    {
    }
    ~Impl()
    {
        Release();
    }
    void Release()
    {
        for (auto &digit : digits_)
            digit.Unbind();
        elements_.fill(nullptr);
        labels_.fill(nullptr);
        scores_.reset();
        teams_ = {};
        bound_ = false;
        host_.Release();
    }
    bool Bind()
    {
        auto &document = *host_.Document();
        constexpr std::array ids{"mcAlliedSingleDigit", "mcAlliedTwoDigit",
                                 "mcAlliedOneDigit",    "mcIllusionSingleDigit",
                                 "mcIllusionTwoDigit",  "mcIllusionOneDigit"};
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            elements_[i] = document.GetElementById(ids[i]);
            if (!elements_[i])
                return false;
            digits_[i].Bind(*elements_[i]);
        }
        labels_ = {document.GetElementById("tfAlliedTeam-label"),
                   document.GetElementById("tfIllusionTeam-label")};
        bound_ = labels_[0] && labels_[1];
        return bound_;
    }
    bool ApplyScores(const std::array<int, 2> &scores)
    {
        if (scores_ == scores)
            return false;
        constexpr int DecimalBase = 10, DigitsPerTeam = 3;
        for (std::size_t i = 0; i < scores.size(); ++i)
        {
            const int tens = scores[i] / DecimalBase, ones = scores[i] % DecimalBase;
            const std::size_t start = i * DigitsPerTeam;
            elements_[start]->SetProperty("display", tens == 0 ? "block" : "none");
            elements_[start + 1]->SetProperty("display", tens == 0 ? "none" : "block");
            elements_[start + 2]->SetProperty("display", tens == 0 ? "none" : "block");
            digits_[start].SetFrame(ones + 1);
            digits_[start + 1].SetFrame(tens + 1);
            digits_[start + 2].SetFrame(ones + 1);
        }
        scores_ = scores;
        return true;
    }
    bool Prepare(int width, int height, bool visible, const std::array<int, 2> &scores,
                 const std::array<std::wstring, 2> &teams)
    {
        if (!bound_ && !visible)
            return true;
        const auto size = design_.Values(0);
        if (!host_.Ensure(width, height, size[0], size[1]))
            return false;
        if (!bound_ && !Bind())
        {
            Release();
            return false;
        }
        if (!host_.SetVisible(visible))
            return false;
        bool dirty = ApplyScores(scores);
        for (std::size_t i = 0; i < teams.size(); ++i)
        {
            if (teams_[i] == teams[i])
                continue;
            labels_[i]->SetInnerRML(
                Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(teams[i].c_str())));
            dirty = true;
        }
        teams_ = teams;
        return host_.CaptureIfDirty(dirty);
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    std::array<RmlMuProgressBar, 6> digits_;
    std::array<Rml::Element *, 6> elements_{};
    std::array<Rml::Element *, 2> labels_{};
    std::optional<std::array<int, 2>> scores_;
    std::array<std::wstring, 2> teams_;
    bool bound_ = false;
};
RmlTempleScorePanel::RmlTempleScorePanel(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper))
{
}
RmlTempleScorePanel::~RmlTempleScorePanel() = default;
void RmlTempleScorePanel::Release()
{
    impl_->Release();
}
unsigned RmlTempleScorePanel::HoldMilliseconds() const
{
    return impl_->design_.Number<unsigned>(1);
}
bool RmlTempleScorePanel::PrepareOnWorker(int width, int height, bool visible,
                                          const std::array<int, 2> &scores,
                                          const std::array<std::wstring, 2> &teams)
{
    return impl_->Prepare(width, height, visible, scores, teams);
}
void RmlTempleScorePanel::ProcessInput(const SessionInputEvent &event)
{
    if (impl_->bound_)
        impl_->host_.ProcessInput(event);
}
bool RmlTempleScorePanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->bound_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::Events

using namespace SEASON3B;

void CNewBloodCastleSystem::RenderMatchResult(void)
{
    int x = REFERENCE_WIDTH / 2;
    int yPos = m_PosResult.y + 40;

    EnableAlphaTest();

    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.SetTextColor(128, 255, 128, 255);
    g_RenderText.SetBgColor(0, 0, 0, 0);

    wchar_t lpszStr[256] = {};

    if (m_iNumResult)
    {
        g_RenderText.RenderText(x, yPos, I18N::Game::CompletedTheBloodCastleQuest, 0, 0,
                                RT3_WRITE_CENTER);
        yPos += 16;
        g_RenderText.RenderText(x, yPos, I18N::Game::CongratulationsYouHaveSuccessfully, 0, 0,
                                RT3_WRITE_CENTER);
    }
    else
    {
        g_RenderText.RenderText(x, yPos, I18N::Game::ToCompleteTheBloodCastleQuest, 0, 0,
                                RT3_WRITE_CENTER);
        yPos += 16;
        g_RenderText.RenderText(x, yPos, I18N::Game::UnfortunatelyYouHaveFailed, 0, 0,
                                RT3_WRITE_CENTER);
    }

    yPos += 30;

    MatchResult *pResult = &m_MatchResult[0];

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetTextColor(210, 255, 210, 255);
    mu_swprintf(lpszStr, I18N::Game::RewardedExpD, pResult->m_dwExp);
    g_RenderText.RenderText(x, yPos, lpszStr, 0, 0, RT3_WRITE_CENTER);
    yPos += 24;

    if (m_iNumResult)
    {
        g_RenderText.SetTextColor(255, 210, 210, 255);
        mu_swprintf(lpszStr, I18N::Game::RewardedZenD, pResult->m_iZen);
        g_RenderText.RenderText(x, yPos, lpszStr, 0, 0, RT3_WRITE_CENTER);
        yPos += 24;
    }

    g_RenderText.SetTextColor(210, 210, 255, 255);
    mu_swprintf(lpszStr, I18N::Game::BloodCastlePointD, pResult->m_iScore);
    g_RenderText.RenderText(x, yPos, lpszStr, 0, 0, RT3_WRITE_CENTER);

    DisableAlphaBlend();
}

void CNewChaosCastleSystem::RenderMatchResult(void)
{
    int x = REFERENCE_WIDTH / 2;
    int yPos = m_PosResult.y + 40;
    wchar_t lpszStr[256] = {};

    EnableAlphaTest();

    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.SetTextColor(128, 255, 128, 255);
    g_RenderText.SetBgColor(0, 0, 0, 0);

    if (m_iNumResult)
    {
        g_RenderText.RenderText(x, yPos, I18N::Game::TheSpiritOfTheGuardHasBeenPurified, 0, 0,
                                RT3_WRITE_CENTER);
        yPos += 16;
        mu_swprintf(lpszStr, L"%ls %ls", I18N::Game::TheQuest,
                    I18N::Game::CongratulationsYouHaveSuccessfully);
        g_RenderText.RenderText(x, yPos, lpszStr, 0, 0, RT3_WRITE_CENTER);
    }
    else
    {
        mu_swprintf(lpszStr, L"%ls %ls", I18N::Game::TheQuest,
                    I18N::Game::UnfortunatelyYouHaveFailed);
        g_RenderText.RenderText(x, yPos, lpszStr, 0, 0, RT3_WRITE_CENTER);
        yPos += 16;
        g_RenderText.RenderText(x, yPos, I18N::Game::TryAgainNextTime, 0, 0, RT3_WRITE_CENTER);
    }
    yPos += 30;

    MatchResult *pResult = &m_MatchResult[0];

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetTextColor(210, 255, 210, 255);

    mu_swprintf(lpszStr, I18N::Game::RewardedExpD, pResult->m_dwExp);
    g_RenderText.RenderText(x, yPos, lpszStr, 0, 0, RT3_WRITE_CENTER);
    yPos += 20;

    mu_swprintf(lpszStr, I18N::Game::MonsterKillCountD, pResult->m_iScore);
    g_RenderText.RenderText(x, yPos, lpszStr, 0, 0, RT3_WRITE_CENTER);
    yPos += 20;

    mu_swprintf(lpszStr, I18N::Game::PlayersKillCountD, pResult->m_iZen);
    g_RenderText.RenderText(x, yPos, lpszStr, 0, 0, RT3_WRITE_CENTER);
    yPos += 24;

    DisableAlphaBlend();
}

// Construction/Destruction

void CNewUICastleWindow::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool CNewUICastleWindow::Render()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    RenderFrame();

    m_TabBtn.Render();

    switch (m_iNumCurOpenTab)
    {
    case TAB_GATE_MANAGING:
        RenderGateManagingTab();
        break;
    case TAB_STATUE_MANAGING:
        RenderStatueManagingTab();
        break;
    case TAB_TAX_MANAGING:
        RenderTaxManagingTab();
        break;
    }

    m_BtnExit.Render();

    DisableAlphaBlend();

    return true;
}

void CNewUICastleWindow::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_CASTLEWINDOW_BACK,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back01.tga", IMAGE_CASTLEWINDOW_TOP,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-L.tga", IMAGE_CASTLEWINDOW_LEFT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-R.tga", IMAGE_CASTLEWINDOW_RIGHT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back03.tga", IMAGE_CASTLEWINDOW_BOTTOM,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_exit_00.tga", IMAGE_CASTLEWINDOW_EXIT_BTN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_guild_tab04.tga", IMAGE_CASTLEWINDOW_TAB_BTN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_myquest_Line.tga", IMAGE_CASTLEWINDOW_LINE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_btn_empty_very_small.tga", IMAGE_CASTLEWINDOW_BUTTON,
                LegacyTextureFilter::Linear);

    LoadBitmapW(L"Interface\\newui_item_table01(L).tga", IMAGE_CASTLEWINDOW_TABLE_TOP_LEFT);
    LoadBitmapW(L"Interface\\newui_item_table01(R).tga", IMAGE_CASTLEWINDOW_TABLE_TOP_RIGHT);
    LoadBitmapW(L"Interface\\newui_item_table02(L).tga", IMAGE_CASTLEWINDOW_TABLE_BOTTOM_LEFT);
    LoadBitmapW(L"Interface\\newui_item_table02(R).tga", IMAGE_CASTLEWINDOW_TABLE_BOTTOM_RIGHT);
    LoadBitmapW(L"Interface\\newui_item_table03(Up).tga", IMAGE_CASTLEWINDOW_TABLE_TOP_PIXEL);
    LoadBitmapW(L"Interface\\newui_item_table03(Dw).tga", IMAGE_CASTLEWINDOW_TABLE_BOTTOM_PIXEL);
    LoadBitmapW(L"Interface\\newui_item_table03(L).tga", IMAGE_CASTLEWINDOW_TABLE_LEFT_PIXEL);
    LoadBitmapW(L"Interface\\newui_item_table03(R).tga", IMAGE_CASTLEWINDOW_TABLE_RIGHT_PIXEL);

    LoadBitmapW(L"Interface\\newui_item_money2.tga", IMAGE_CASTLEWINDOW_MONEY,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_Bt_scroll_up.jpg", IMAGE_CASTLEWINDOW_SCROLL_UP_BTN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_Bt_scroll_dn.jpg", IMAGE_CASTLEWINDOW_SCROLL_DOWN_BTN,
                LegacyTextureFilter::Linear);
}
void CNewUICastleWindow::UnloadImages()
{
    DeleteBitmap(IMAGE_CASTLEWINDOW_BOTTOM);
    DeleteBitmap(IMAGE_CASTLEWINDOW_RIGHT);
    DeleteBitmap(IMAGE_CASTLEWINDOW_LEFT);
    DeleteBitmap(IMAGE_CASTLEWINDOW_TOP);
    DeleteBitmap(IMAGE_CASTLEWINDOW_BACK);
    DeleteBitmap(IMAGE_CASTLEWINDOW_EXIT_BTN);
    DeleteBitmap(IMAGE_CASTLEWINDOW_TAB_BTN);
    DeleteBitmap(IMAGE_CASTLEWINDOW_LINE);
    DeleteBitmap(IMAGE_CASTLEWINDOW_BUTTON);
    DeleteBitmap(IMAGE_CASTLEWINDOW_TABLE_RIGHT_PIXEL);
    DeleteBitmap(IMAGE_CASTLEWINDOW_TABLE_LEFT_PIXEL);
    DeleteBitmap(IMAGE_CASTLEWINDOW_TABLE_BOTTOM_PIXEL);
    DeleteBitmap(IMAGE_CASTLEWINDOW_TABLE_TOP_PIXEL);
    DeleteBitmap(IMAGE_CASTLEWINDOW_TABLE_BOTTOM_RIGHT);
    DeleteBitmap(IMAGE_CASTLEWINDOW_TABLE_BOTTOM_LEFT);
    DeleteBitmap(IMAGE_CASTLEWINDOW_TABLE_TOP_RIGHT);
    DeleteBitmap(IMAGE_CASTLEWINDOW_TABLE_TOP_LEFT);
    DeleteBitmap(IMAGE_CASTLEWINDOW_MONEY);
    DeleteBitmap(IMAGE_CASTLEWINDOW_SCROLL_UP_BTN);
    DeleteBitmap(IMAGE_CASTLEWINDOW_SCROLL_DOWN_BTN);
}

void CNewUICastleWindow::RenderFrame()
{
    RenderImage(IMAGE_CASTLEWINDOW_BACK, m_Pos.x, m_Pos.y, 190.f, 429.f);
    RenderImage(IMAGE_CASTLEWINDOW_TOP, m_Pos.x, m_Pos.y, 190.f, 64.f);
    RenderImage(IMAGE_CASTLEWINDOW_LEFT, m_Pos.x, m_Pos.y + 64, 21.f, 320.f);
    RenderImage(IMAGE_CASTLEWINDOW_RIGHT, m_Pos.x + INVENTORY_WIDTH - 21, m_Pos.y + 64, 21.f,
                320.f);
    RenderImage(IMAGE_CASTLEWINDOW_BOTTOM, m_Pos.x, m_Pos.y + INVENTORY_HEIGHT - 45, 190.f, 45.f);

    wchar_t szText[256] = {
        0,
    };
    float fPos_x = m_Pos.x + 15.0f, fPos_y = m_Pos.y;
    float fLine_y = 13.0f;

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetTextColor(220, 220, 220, 255);
    g_RenderText.SetBgColor(0, 0, 0, 0);

    mu_swprintf(szText, L"%ls", I18N::Game::SeniorNPC);
    g_RenderText.RenderText(fPos_x, fPos_y + fLine_y, szText, 160.0f, 0, RT3_SORT_CENTER);
}

void CNewUICastleWindow::RenderOutlineUpper(float fPos_x, float fPos_y, float fWidth, float fHeight)
{
    POINT ptOrigin = {(long)fPos_x, (long)fPos_y};
    float fBoxWidth = fWidth;

    RenderImage(IMAGE_CASTLEWINDOW_TABLE_TOP_LEFT, ptOrigin.x + 12, ptOrigin.y - 4, 14, 14);
    RenderImage(IMAGE_CASTLEWINDOW_TABLE_TOP_RIGHT, ptOrigin.x + fBoxWidth + 4, ptOrigin.y - 4, 14,
                14);
    RenderImage(IMAGE_CASTLEWINDOW_TABLE_TOP_PIXEL, ptOrigin.x + 25, ptOrigin.y - 4, fBoxWidth - 21,
                14);
    glColor4f(0.0f, 0.0f, 0.0f, 0.3f);
    RenderColor(ptOrigin.x + 15, ptOrigin.y - 3, fBoxWidth - 2, 15);
    EndRenderColor();
}

void CNewUICastleWindow::RenderOutlineLower(float fPos_x, float fPos_y, float fWidth, float fHeight)
{
    POINT ptOrigin = {(long)fPos_x, (long)fPos_y};
    float fBoxWidth = fWidth;
    float fBoxHeight = fHeight;

    RenderImage(IMAGE_CASTLEWINDOW_TABLE_LEFT_PIXEL, ptOrigin.x + 12, ptOrigin.y + 9, 14,
                fBoxHeight);
    RenderImage(IMAGE_CASTLEWINDOW_TABLE_RIGHT_PIXEL, ptOrigin.x + fBoxWidth + 4, ptOrigin.y + 9,
                14, fBoxHeight);
    RenderImage(IMAGE_CASTLEWINDOW_TABLE_BOTTOM_PIXEL, ptOrigin.x + 15, ptOrigin.y + 3,
                fBoxWidth - 2, 14);
    RenderImage(IMAGE_CASTLEWINDOW_TABLE_BOTTOM_LEFT, ptOrigin.x + 12, ptOrigin.y + fBoxHeight + 3,
                14, 14);
    RenderImage(IMAGE_CASTLEWINDOW_TABLE_BOTTOM_RIGHT, ptOrigin.x + fBoxWidth + 4,
                ptOrigin.y + fBoxHeight + 3, 14, 14);
    RenderImage(IMAGE_CASTLEWINDOW_TABLE_BOTTOM_PIXEL, ptOrigin.x + 25, ptOrigin.y + fBoxHeight + 3,
                fBoxWidth - 21, 14);
}

void CNewUICastleWindow::RenderGateManagingTab()
{
    LPPMSG_NPCDBLIST pNPCInfo = &g_SenatusInfo.GetCurrGateInfo();
    POINT ptOrigin = {m_Pos.x, m_Pos.y + 55};

    g_RenderText.SetFont(LegacyFontRole::Bold);

    ptOrigin.y += 6;
    RenderOutlineUpper(ptOrigin.x, ptOrigin.y, 160, 165);
    g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::PurchaseAndRepair, 190, 0,
                            RT3_SORT_CENTER);
    RenderBitmap(BITMAP_INTERFACE_EX + 35, ptOrigin.x + 15, ptOrigin.y + 12, 160.f, 165.f, 0.f, 0.f,
                 160.f / 256.f, 165.f / 256.f);
    RenderOutlineLower(ptOrigin.x, ptOrigin.y, 160, 165);

    ptOrigin.y += 12;
    RenderCastleItem(ptOrigin.x + 82, ptOrigin.y + 35, &g_SenatusInfo.GetGateInfo(0));
    RenderCastleItem(ptOrigin.x + 64, ptOrigin.y + 83, &g_SenatusInfo.GetGateInfo(1));
    RenderCastleItem(ptOrigin.x + 100, ptOrigin.y + 83, &g_SenatusInfo.GetGateInfo(2));
    RenderCastleItem(ptOrigin.x + 48, ptOrigin.y + 135, &g_SenatusInfo.GetGateInfo(3));
    RenderCastleItem(ptOrigin.x + 82, ptOrigin.y + 135, &g_SenatusInfo.GetGateInfo(4));
    RenderCastleItem(ptOrigin.x + 116, ptOrigin.y + 135, &g_SenatusInfo.GetGateInfo(5));

    g_RenderText.SetFont(LegacyFontRole::Normal);

    ptOrigin.y += 173;
    if (pNPCInfo->btNpcLive == 0)
    {
        m_BtnBuy.Render();
    }
    else
    {
        if (g_SenatusInfo.IsGateRepairable())
        {
            m_BtnRepair.UnLock();
            m_BtnRepair.ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
            m_BtnRepair.ChangeTextColor(RGBA(255, 255, 255, 255));
        }
        else
        {
            m_BtnRepair.Lock();
            m_BtnRepair.ChangeImgColor(BUTTON_STATE_UP, RGBA(100, 100, 100, 255));
            m_BtnRepair.ChangeTextColor(RGBA(100, 100, 100, 255));
        }
        if (g_SenatusInfo.IsGateHPUpgradable())
        {
            m_BtnUpgradeHP.UnLock();
            m_BtnUpgradeHP.ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
            m_BtnUpgradeHP.ChangeTextColor(RGBA(255, 255, 255, 255));
        }
        else
        {
            m_BtnUpgradeHP.Lock();
            m_BtnUpgradeHP.ChangeImgColor(BUTTON_STATE_UP, RGBA(100, 100, 100, 255));
            m_BtnUpgradeHP.ChangeTextColor(RGBA(100, 100, 100, 255));
        }
        if (g_SenatusInfo.IsGateDefeseUpgradable())
        {
            m_BtnUpgradeDefense.UnLock();
            m_BtnUpgradeDefense.ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
            m_BtnUpgradeDefense.ChangeTextColor(RGBA(255, 255, 255, 255));
        }
        else
        {
            m_BtnUpgradeDefense.Lock();
            m_BtnUpgradeDefense.ChangeImgColor(BUTTON_STATE_UP, RGBA(100, 100, 100, 255));
            m_BtnUpgradeDefense.ChangeTextColor(RGBA(100, 100, 100, 255));
        }

        m_BtnRepair.Render();
        m_BtnUpgradeHP.Render();
        m_BtnUpgradeDefense.Render();

        g_RenderText.SetBgColor(0x00000000);
        g_RenderText.SetTextColor(0xFFFFFFFF);

        wchar_t szTemp[256];
        mu_swprintf(szTemp, I18N::Game::DURDD, pNPCInfo->iNpcHp, pNPCInfo->iNpcMaxHp);
        InsertComma(szTemp, pNPCInfo->iNpcHp);
        InsertComma(szTemp, pNPCInfo->iNpcMaxHp);
        g_RenderText.RenderText(ptOrigin.x + 20, ptOrigin.y, szTemp);
        ptOrigin.y += 13;
        mu_swprintf(szTemp, I18N::Game::DPD,
                    g_SenatusInfo.GetDefense(pNPCInfo->iNpcNumber, pNPCInfo->iNpcDfLevel));
        g_RenderText.RenderText(ptOrigin.x + 20, ptOrigin.y, szTemp);

        ptOrigin.y += 35;
        RenderOutlineUpper(ptOrigin.x, ptOrigin.y, 160, 78);
        RenderOutlineLower(ptOrigin.x, ptOrigin.y, 160, 78);

        g_RenderText.SetFont(LegacyFontRole::Bold);
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::Improve, 190, 0,
                                RT3_SORT_CENTER);
        g_RenderText.SetFont(LegacyFontRole::Normal);

        ptOrigin.y += 24;
        mu_swprintf(szTemp, I18N::Game::DURD, g_SenatusInfo.GetNextAddHP(pNPCInfo));
        InsertComma(szTemp, g_SenatusInfo.GetNextAddHP(pNPCInfo));
        g_RenderText.RenderText(ptOrigin.x + 30, ptOrigin.y, szTemp);
        ptOrigin.y += 23;
        mu_swprintf(szTemp, I18N::Game::DPD1564, g_SenatusInfo.GetNextAddDefense(pNPCInfo));
        InsertComma(szTemp, g_SenatusInfo.GetNextAddDefense(pNPCInfo));
        g_RenderText.RenderText(ptOrigin.x + 30, ptOrigin.y, szTemp);
    }
}

void CNewUICastleWindow::RenderStatueManagingTab()
{
    POINT ptOrigin = {m_Pos.x, m_Pos.y + 55};
    LPPMSG_NPCDBLIST pNPCInfo = &g_SenatusInfo.GetCurrStatueInfo();

    g_RenderText.SetFont(LegacyFontRole::Bold);

    ptOrigin.y += 6;
    RenderOutlineUpper(ptOrigin.x, ptOrigin.y, 160, 165);
    g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::PurchaseAndRepair, 190, 0,
                            RT3_SORT_CENTER);
    RenderBitmap(BITMAP_INTERFACE_EX + 35, ptOrigin.x + 15, ptOrigin.y + 12, 160.f, 165.f, 0.f, 0.f,
                 160.f / 256.f, 165.f / 256.f);
    RenderOutlineLower(ptOrigin.x, ptOrigin.y, 160, 165);

    ptOrigin.y += 12;
    RenderCastleItem(ptOrigin.x + 82, ptOrigin.y + 20, &g_SenatusInfo.GetStatueInfo(0));
    RenderCastleItem(ptOrigin.x + 82, ptOrigin.y + 65, &g_SenatusInfo.GetStatueInfo(1));
    RenderCastleItem(ptOrigin.x + 64, ptOrigin.y + 110, &g_SenatusInfo.GetStatueInfo(2));
    RenderCastleItem(ptOrigin.x + 100, ptOrigin.y + 110, &g_SenatusInfo.GetStatueInfo(3));

    g_RenderText.SetFont(LegacyFontRole::Normal);

    ptOrigin.y += 173;
    if (pNPCInfo->btNpcLive == 0)
    {
        m_BtnBuy.Render();
    }
    else
    {
        if (g_SenatusInfo.IsStatueRepairable())
        {
            m_BtnRepair.UnLock();
            m_BtnRepair.ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
            m_BtnRepair.ChangeTextColor(RGBA(255, 255, 255, 255));
        }
        else
        {
            m_BtnRepair.Lock();
            m_BtnRepair.ChangeImgColor(BUTTON_STATE_UP, RGBA(100, 100, 100, 255));
            m_BtnRepair.ChangeTextColor(RGBA(100, 100, 100, 255));
        }
        if (g_SenatusInfo.IsStatueHPUpgradable())
        {
            m_BtnUpgradeHP.UnLock();
            m_BtnUpgradeHP.ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
            m_BtnUpgradeHP.ChangeTextColor(RGBA(255, 255, 255, 255));
        }
        else
        {
            m_BtnUpgradeHP.Lock();
            m_BtnUpgradeHP.ChangeImgColor(BUTTON_STATE_UP, RGBA(100, 100, 100, 255));
            m_BtnUpgradeHP.ChangeTextColor(RGBA(100, 100, 100, 255));
        }
        if (g_SenatusInfo.IsStatueDefeseUpgradable())
        {
            m_BtnUpgradeDefense.UnLock();
            m_BtnUpgradeDefense.ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
            m_BtnUpgradeDefense.ChangeTextColor(RGBA(255, 255, 255, 255));
        }
        else
        {
            m_BtnUpgradeDefense.Lock();
            m_BtnUpgradeDefense.ChangeImgColor(BUTTON_STATE_UP, RGBA(100, 100, 100, 255));
            m_BtnUpgradeDefense.ChangeTextColor(RGBA(100, 100, 100, 255));
        }
        if (g_SenatusInfo.IsStatueRecoverUpgradable())
        {
            m_BtnUpgradeRecover.UnLock();
            m_BtnUpgradeRecover.ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
            m_BtnUpgradeRecover.ChangeTextColor(RGBA(255, 255, 255, 255));
        }
        else
        {
            m_BtnUpgradeRecover.Lock();
            m_BtnUpgradeRecover.ChangeImgColor(BUTTON_STATE_UP, RGBA(100, 100, 100, 255));
            m_BtnUpgradeRecover.ChangeTextColor(RGBA(100, 100, 100, 255));
        }

        m_BtnRepair.Render();
        m_BtnUpgradeHP.Render();
        m_BtnUpgradeDefense.Render();
        m_BtnUpgradeRecover.Render();

        g_RenderText.SetTextColor(0xFFFFFFFF);
        g_RenderText.SetBgColor(0);

        wchar_t szTemp[256];
        mu_swprintf(szTemp, I18N::Game::DURDD, pNPCInfo->iNpcHp, pNPCInfo->iNpcMaxHp);
        InsertComma(szTemp, pNPCInfo->iNpcHp);
        InsertComma(szTemp, pNPCInfo->iNpcMaxHp);
        g_RenderText.RenderText(ptOrigin.x + 20, ptOrigin.y, szTemp);
        ptOrigin.y += 13;
        mu_swprintf(szTemp, I18N::Game::DPD,
                    g_SenatusInfo.GetDefense(pNPCInfo->iNpcNumber, pNPCInfo->iNpcDfLevel));
        g_RenderText.RenderText(ptOrigin.x + 20, ptOrigin.y, szTemp);
        ptOrigin.y += 13;
        mu_swprintf(szTemp, I18N::Game::RRD,
                    g_SenatusInfo.GetRecover(pNPCInfo->iNpcNumber, pNPCInfo->iNpcRgLevel));
        g_RenderText.RenderText(ptOrigin.x + 20, ptOrigin.y, szTemp);

        ptOrigin.y += 22;
        RenderOutlineUpper(ptOrigin.x, ptOrigin.y, 160, 78);
        RenderOutlineLower(ptOrigin.x, ptOrigin.y, 160, 78);
        g_RenderText.SetFont(LegacyFontRole::Bold);
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::Improve, 190, 0,
                                RT3_SORT_CENTER);
        g_RenderText.SetFont(LegacyFontRole::Normal);

        ptOrigin.y += 24;
        mu_swprintf(szTemp, I18N::Game::DURD, g_SenatusInfo.GetNextAddHP(pNPCInfo));
        InsertComma(szTemp, g_SenatusInfo.GetNextAddHP(pNPCInfo));
        g_RenderText.RenderText(ptOrigin.x + 30, ptOrigin.y, szTemp);
        ptOrigin.y += 23;
        mu_swprintf(szTemp, I18N::Game::DPD1564, g_SenatusInfo.GetNextAddDefense(pNPCInfo));
        InsertComma(szTemp, g_SenatusInfo.GetNextAddDefense(pNPCInfo));
        g_RenderText.RenderText(ptOrigin.x + 30, ptOrigin.y, szTemp);
        ptOrigin.y += 23;
        mu_swprintf(szTemp, I18N::Game::RRD1565, g_SenatusInfo.GetNextAddRecover(pNPCInfo));
        InsertComma(szTemp, g_SenatusInfo.GetNextAddRecover(pNPCInfo));
        g_RenderText.RenderText(ptOrigin.x + 30, ptOrigin.y, szTemp);
    }
}

void CNewUICastleWindow::RenderTaxManagingTab()
{
    POINT ptOrigin = {m_Pos.x, m_Pos.y + 55};
    g_RenderText.SetTextColor(0xFFFFFFFF);
    g_RenderText.SetBgColor(0);

    ptOrigin.y += 6;

    EnableAlphaTest();
    glColor4f(0.4f, 0.4f, 0.4f, 0.5f);
    RenderColor(ptOrigin.x + 15, ptOrigin.y + 14, 150, 24);
    RenderColor(ptOrigin.x + 15, ptOrigin.y + 42, 150, 24);
    EndRenderColor();

    RenderOutlineUpper(ptOrigin.x, ptOrigin.y, 160, 55);

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::AdjustTaxRate, 190, 0,
                            RT3_SORT_CENTER);

    RenderOutlineLower(ptOrigin.x, ptOrigin.y, 160, 55);
    RenderImage(IMAGE_CASTLEWINDOW_TABLE_BOTTOM_PIXEL, ptOrigin.x + 15, ptOrigin.y + 30, 160 - 2,
                14);

    m_BtnChaosTaxUp.Render();
    m_BtnChaosTaxDn.Render();
    m_BtnNPCTaxUp.Render();
    m_BtnNPCTaxDn.Render();

    wchar_t szTemp[256];
    ptOrigin.y += 23;
    g_RenderText.SetFont(LegacyFontRole::Normal);

    mu_swprintf(szTemp, I18N::Game::ChaosCombinationGoblinDD, g_SenatusInfo.GetRealTaxRateChaos(),
                g_SenatusInfo.GetChaosTaxRate());
    g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, szTemp, 175, 0, RT3_SORT_CENTER);
    ptOrigin.y += 25;

    mu_swprintf(szTemp, I18N::Game::NPCDD, g_SenatusInfo.GetRealTaxRateStore(),
                g_SenatusInfo.GetNormalTaxRate());
    g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, szTemp, 175, 0, RT3_SORT_CENTER);

    m_BtnApplyTax.Render();

    ptOrigin.y += 53;
    g_RenderText.RenderText(ptOrigin.x + 15, ptOrigin.y, I18N::Game::OnlyTheLordOfTheCastle, 160, 0,
                            RT3_SORT_CENTER);
    ptOrigin.y += 13;
    g_RenderText.RenderText(ptOrigin.x + 15, ptOrigin.y, I18N::Game::CanAdjustTheTaxRate, 160, 0,
                            RT3_SORT_CENTER);
    ptOrigin.y += 13;
    g_RenderText.RenderText(ptOrigin.x + 15, ptOrigin.y, I18N::Game::TaxAdjustmentAvailable, 160, 0,
                            RT3_SORT_CENTER);

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetTextColor(0xFF947BBB);

    ptOrigin.y += 20;
    mu_swprintf(szTemp, I18N::Game::DuringTrucePeriod);
    g_RenderText.RenderText(ptOrigin.x + 15, ptOrigin.y, szTemp, 160, 0, RT3_SORT_CENTER);

    ptOrigin.y += 12;
    mu_swprintf(szTemp, I18N::Game::MaximumTaxRates3);
    g_RenderText.RenderText(ptOrigin.x + 15, ptOrigin.y, szTemp, 160, 0, RT3_SORT_CENTER);

    ptOrigin.y += 12;
    g_RenderText.RenderText(ptOrigin.x + 15, ptOrigin.y, I18N::Game::NPCsInclude, 160, 0,
                            RT3_SORT_CENTER);
    ptOrigin.y += 12;
    g_RenderText.RenderText(ptOrigin.x + 15, ptOrigin.y, I18N::Game::ElfLalaPotionGirl, 160, 0,
                            RT3_SORT_CENTER);
    ptOrigin.y += 12;
    g_RenderText.RenderText(ptOrigin.x + 15, ptOrigin.y, I18N::Game::WizardArenaGuard, 160, 0,
                            RT3_SORT_CENTER);
    ptOrigin.y += 12;
    g_RenderText.RenderText(ptOrigin.x + 15, ptOrigin.y, I18N::Game::AndEtc, 160, 0,
                            RT3_SORT_CENTER);

    ptOrigin.y += 10;
    RenderBitmap(IMAGE_CASTLEWINDOW_LINE, ptOrigin.x + 1, ptOrigin.y, 188.f, 21, 0.f, 0.f,
                 188 / 256.f, 21 / 32.f);

    g_RenderText.SetTextColor(0xFFFFFFFF);

    ptOrigin.y += 18;
    RenderImage(IMAGE_CASTLEWINDOW_MONEY, ptOrigin.x + 10, ptOrigin.y, 170.f, 24.f);

    mu_swprintf(szTemp, I18N::Game::Zen);
    g_RenderText.RenderText(ptOrigin.x + 14, ptOrigin.y + 7, szTemp);

    //wchar_t szGoldText[32];
    //ConvertGold(g_SenatusInfo.GetCastleMoney(),szGoldText);

    mu_swprintf(szTemp, L"%I64d", g_SenatusInfo.GetCastleMoney());
    InsertComma64(szTemp, g_SenatusInfo.GetCastleMoney());
    g_RenderText.RenderText(ptOrigin.x + 90, ptOrigin.y + 7, szTemp, 80, 0, RT3_SORT_RIGHT);

    m_BtnWithdraw.Render();

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetTextColor(0xFF947BBB);

    ptOrigin.y += 54;
    g_RenderText.RenderText(ptOrigin.x + 15, ptOrigin.y, I18N::Game::TaxBelongsToTheCastle, 160, 0,
                            RT3_SORT_CENTER);
    ptOrigin.y += 12;
    g_RenderText.RenderText(ptOrigin.x + 15, ptOrigin.y, I18N::Game::AndCanBeUsed, 160, 0,
                            RT3_SORT_CENTER);
    ptOrigin.y += 12;
    g_RenderText.RenderText(ptOrigin.x + 15, ptOrigin.y, I18N::Game::ToOperateTheCastle, 160, 0,
                            RT3_SORT_CENTER);
}

void CNewUICastleWindow::RenderCastleItem(int nPosX, int nPosY, LPPMSG_NPCDBLIST pInfo)
{
    const int nHPBlockSize = 24 / (g_SenatusInfo.GetMaxHPLevel() + 1);
    const int nDefenseBlockSize = 24 / (g_SenatusInfo.GetMaxDefenseLevel() + 1);
    const int nRecoverBlockSize = 24 / (g_SenatusInfo.GetMaxRecoverLevel() + 1);

    if (g_SenatusInfo.IsGate(pInfo)) // ����
    {
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

        if (pInfo->btNpcLive)
        {
            int nHP = g_SenatusInfo.GetHPLevel(pInfo);
            int nDefense = g_SenatusInfo.GetDefenseLevel(pInfo);
            float fHPRate = pInfo->iNpcHp / (float)pInfo->iNpcMaxHp;

            DisableAlphaBlend();

            RenderColor(nPosX, nPosY - 10, nHPBlockSize * (nHP + 1), 3);
            RenderColor(nPosX, nPosY - 5, 24, 3);
            glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
            RenderColor(nPosX, nPosY - 10, (nHPBlockSize * (nHP + 1)) * fHPRate, 3);
            glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
            RenderColor(nPosX, nPosY - 10, 24, 1);
            RenderColor(nPosX, nPosY - 7, 24, 1);
            RenderColor(nPosX, nPosY - 10, 1, 3);
            RenderColor(nPosX + 24, nPosY - 10, 1, 3);
            glColor4f(0.0f, 1.0f, 0.0f, 1.0f);
            RenderColor(nPosX, nPosY - 5, nDefenseBlockSize * (nDefense + 1), 3);
            EndRenderColor();
        }

        if (pInfo->iNpcIndex == g_SenatusInfo.GetCurrGate() + 1)
            RenderBitmap(BITMAP_INTERFACE_EX + 37, nPosX, nPosY, 24.f, 24.f, 0.f, 0.f, 24.f / 32.f,
                         24.f / 32.f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 36, nPosX, nPosY, 24.f, 24.f, 0.f, 0.f, 24.f / 32.f,
                         24.f / 32.f);
    }
    if (g_SenatusInfo.IsStatue(pInfo))
    {
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

        if (pInfo->btNpcLive)
        {
            int nHP = g_SenatusInfo.GetHPLevel(pInfo);
            int nDefense = g_SenatusInfo.GetDefenseLevel(pInfo);
            int nRecover = g_SenatusInfo.GetRecoverLevel(pInfo);
            float fHPRate = pInfo->iNpcHp / (float)pInfo->iNpcMaxHp;

            DisableAlphaBlend();

            RenderColor(nPosX, nPosY - 15, nHPBlockSize * (nHP + 1), 3);
            RenderColor(nPosX, nPosY - 10, 24, 3);
            RenderColor(nPosX, nPosY - 5, 24, 3);
            glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
            RenderColor(nPosX, nPosY - 15, (nHPBlockSize * (nHP + 1)) * fHPRate, 3);
            glColor4f(0.0f, 1.0f, 0.0f, 1.0f);
            RenderColor(nPosX, nPosY - 10, nDefenseBlockSize * (nDefense + 1), 3);
            glColor4f(1.0f, 1.0f, 0.0f, 1.0f);
            RenderColor(nPosX, nPosY - 5, nRecoverBlockSize * (nRecover + 1), 3);
            EndRenderColor();
        }

        if (pInfo->iNpcIndex == g_SenatusInfo.GetCurrStatue() + 1)
            RenderBitmap(BITMAP_INTERFACE_EX + 39, nPosX, nPosY, 24.f, 24.f, 0.f, 0.f, 24.f / 32.f,
                         24.f / 32.f);
        else
            RenderBitmap(BITMAP_INTERFACE_EX + 38, nPosX, nPosY, 24.f, 24.f, 0.f, 0.f, 24.f / 32.f,
                         24.f / 32.f);
    }
    EnableAlphaTest();
}

bool CNewUIDuelWatchMainFrameWindow::Render()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    RenderFrame();

    m_BtnExit.Render();

    if (g_DuelMgr.GetCurrentChannel() == -1)
        return true;

    POINT ptOrigin = {0, (long)((float)REFERENCE_HEIGHT - 51.f)};

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.RenderText(ptOrigin.x + 320 - 80, ptOrigin.y + 36,
                            g_DuelMgr.GetDuelPlayerID(DUEL_HERO), 55, 0, RT3_SORT_CENTER);
    g_RenderText.RenderText(ptOrigin.x + 320 + 25, ptOrigin.y + 36,
                            g_DuelMgr.GetDuelPlayerID(DUEL_ENEMY), 55, 0, RT3_SORT_CENTER);

    int i;
    for (i = 0; i < g_DuelMgr.GetScore(DUEL_HERO); ++i)
    {
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_SCORE, 57 + 17 * i, 460, 16.f, 17.f);
    }
    for (i = 0; i < g_DuelMgr.GetScore(DUEL_ENEMY); ++i)
    {
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_SCORE, REFERENCE_WIDTH - 74 - 17 * i, 460, 16.f,
                    17.f);
    }

    RenderGauges();

    DisableAlphaBlend();

    return true;
}

void CNewUIDuelWatchMainFrameWindow::RenderGauges()
{
    float fHPRate = g_DuelMgr.GetHP(DUEL_HERO);
    if (m_fPrevHPRate1 > fHPRate)
    {
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_HP_GAUGE_FX, 60 + 236.f * (1.f - m_fPrevHPRate1), 440,
                    236.f * m_fPrevHPRate1, 7.f, 235.f / 256.f * m_fPrevHPRate1, 0,
                    -235.f / 256.f * m_fPrevHPRate1, 6.f / 8.f);
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_HP_GAUGE, 60 + 236.f * (1.f - fHPRate), 440,
                    236.f * fHPRate, 7.f, 235.f / 256.f * fHPRate, 0, -235.f / 256.f * fHPRate,
                    6.f / 8.f);
    }
    else if (m_fPrevHPRate1 < fHPRate)
    {
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_HP_GAUGE_FX, 60 + 236.f * (1.f - fHPRate), 440,
                    236.f * fHPRate, 7.f, 235.f / 256.f * fHPRate, 0, -235.f / 256.f * fHPRate,
                    6.f / 8.f);
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_HP_GAUGE, 60 + 236.f * (1.f - m_fPrevHPRate1), 440,
                    236.f * m_fPrevHPRate1, 7.f, 235.f / 256.f * m_fPrevHPRate1, 0,
                    -235.f / 256.f * m_fPrevHPRate1, 6.f / 8.f);
    }
    else
    {
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_HP_GAUGE, 60 + 236.f * (1.f - fHPRate), 440,
                    236.f * fHPRate, 7.f, 235.f / 256.f * fHPRate, 0, -235.f / 256.f * fHPRate,
                    6.f / 8.f);
    }

    fHPRate = g_DuelMgr.GetHP(DUEL_ENEMY);
    if (m_fPrevHPRate2 > fHPRate)
    {
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_HP_GAUGE_FX, 580 - 236, 440, 236.f * m_fPrevHPRate2,
                    7.f);
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_HP_GAUGE, 580 - 236, 440, 236.f * fHPRate, 7.f);
    }
    else if (m_fPrevHPRate2 < fHPRate)
    {
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_HP_GAUGE_FX, 580 - 236, 440, 236.f * fHPRate, 7.f);
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_HP_GAUGE, 580 - 236, 440, 236.f * m_fPrevHPRate2,
                    7.f);
    }
    else
    {
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_HP_GAUGE, 580 - 236, 440, 236.f * fHPRate, 7.f);
    }

    float fSDRate = g_DuelMgr.GetSD(DUEL_HERO);
    if (m_fPrevSDRate1 > fSDRate)
    {
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_SD_GAUGE_FX, 142 + 154.f * (1.f - m_fPrevSDRate1),
                    450, 154.f * m_fPrevSDRate1, 4.f);
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_SD_GAUGE, 142 + 154.f * (1.f - fSDRate), 450,
                    154.f * fSDRate, 4.f);
    }
    else if (m_fPrevSDRate1 < fSDRate)
    {
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_SD_GAUGE_FX, 142 + 154.f * (1.f - fSDRate), 450,
                    154.f * fSDRate, 4.f);
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_SD_GAUGE, 142 + 154.f * (1.f - m_fPrevSDRate1), 450,
                    154.f * m_fPrevSDRate1, 4.f);
    }
    else
    {
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_SD_GAUGE, 142 + 154.f * (1.f - fSDRate), 450,
                    154.f * fSDRate, 4.f);
    }

    fSDRate = g_DuelMgr.GetSD(DUEL_ENEMY);
    if (m_fPrevSDRate2 > fSDRate)
    {
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_SD_GAUGE_FX, 344, 450, 154.f * m_fPrevSDRate2, 4.f);
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_SD_GAUGE, 344, 450, 154.f * fSDRate, 4.f);
    }
    else if (m_fPrevSDRate2 < fSDRate)
    {
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_SD_GAUGE_FX, 344, 450, 154.f * fSDRate, 4.f);
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_SD_GAUGE, 344, 450, 154.f * m_fPrevSDRate2, 4.f);
    }
    else
    {
        RenderImage(IMAGE_DUELWATCH_MAINFRAME_SD_GAUGE, 344, 450, 154.f * fSDRate, 4.f);
    }
}

void CNewUIDuelWatchMainFrameWindow::Render3D()
{
}

void CNewUIDuelWatchMainFrameWindow::LoadImages()
{
    LoadBitmapW(L"Interface\\menu_pk_01.jpg", IMAGE_DUELWATCH_MAINFRAME_BACK1,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\menu_pk_02.jpg", IMAGE_DUELWATCH_MAINFRAME_BACK2,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\menu_pk_03.jpg", IMAGE_DUELWATCH_MAINFRAME_BACK3,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\menu_pk_bt02.tga", IMAGE_DUELWATCH_MAINFRAME_SCORE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\menu_pk_hp03(bar2).jpg", IMAGE_DUELWATCH_MAINFRAME_HP_GAUGE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\menu_pk_sd03(bar2).jpg", IMAGE_DUELWATCH_MAINFRAME_SD_GAUGE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\menu_pk_hp06(bar).jpg", IMAGE_DUELWATCH_MAINFRAME_HP_GAUGE_FX,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\menu_pk_sd05(bar).jpg", IMAGE_DUELWATCH_MAINFRAME_SD_GAUGE_FX,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_exit_00.tga", IMAGE_INVENTORY_EXIT_BTN,
                LegacyTextureFilter::Linear);
}

void CNewUIDuelWatchMainFrameWindow::UnloadImages()
{
    DeleteBitmap(IMAGE_DUELWATCH_MAINFRAME_BACK1);
    DeleteBitmap(IMAGE_DUELWATCH_MAINFRAME_BACK2);
    DeleteBitmap(IMAGE_DUELWATCH_MAINFRAME_BACK3);
    DeleteBitmap(IMAGE_DUELWATCH_MAINFRAME_SCORE);
    DeleteBitmap(IMAGE_DUELWATCH_MAINFRAME_HP_GAUGE);
    DeleteBitmap(IMAGE_DUELWATCH_MAINFRAME_SD_GAUGE);
    DeleteBitmap(IMAGE_DUELWATCH_MAINFRAME_HP_GAUGE_FX);
    DeleteBitmap(IMAGE_DUELWATCH_MAINFRAME_SD_GAUGE_FX);
    DeleteBitmap(IMAGE_INVENTORY_EXIT_BTN);
}

void CNewUIDuelWatchMainFrameWindow::RenderFrame()
{
    float width, height;
    float x, y;

    width = 256.f;
    height = 51.f;
    x = 0.f;
    y = (float)REFERENCE_HEIGHT - height;
    RenderImage(IMAGE_DUELWATCH_MAINFRAME_BACK1, x, y, width, height);
    width = 128.f;
    x = 256.f;
    RenderImage(IMAGE_DUELWATCH_MAINFRAME_BACK2, x, y, width, height);
    width = 256.f;
    x = 256.f + 128.f;
    RenderImage(IMAGE_DUELWATCH_MAINFRAME_BACK3, x, y, width, height);
}

void CNewUIDuelWatchUserListWindow::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool CNewUIDuelWatchUserListWindow::Render()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    RenderFrame();

    SIZE TextSize;

    g_RenderText.MeasureText(L"Q", 1, &TextSize);
    float fFontHeight = TextSize.cy / g_fScreenRate_y;

    POINT ptSize = {57, 17};
    POINT ptOrigin = {m_Pos.x, m_Pos.y - (ptSize.y + 1) * (long)g_DuelMgr.GetDuelWatchUserCount() +
                                   (ptSize.y - (long)fFontHeight) / 2 + 1};

    for (int i = 0; i < g_DuelMgr.GetDuelWatchUserCount(); ++i)
    {
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, g_DuelMgr.GetDuelWatchUser(i), ptSize.x, 0,
                                RT3_SORT_CENTER);
        ptOrigin.y += ptSize.y + 1;
    }

    DisableAlphaBlend();

    return true;
}

void CNewUIDuelWatchUserListWindow::LoadImages()
{
    LoadBitmapW(L"Interface\\Pk_box.tga", IMAGE_DUELWATCH_USERLIST_BOX,
                LegacyTextureFilter::Linear);
}

void CNewUIDuelWatchUserListWindow::UnloadImages()
{
    DeleteBitmap(IMAGE_DUELWATCH_USERLIST_BOX);
}

void CNewUIDuelWatchUserListWindow::RenderFrame()
{
    POINT ptSize = {57, 17};

    int i;
    glColor4f(0.f, 0.f, 0.f, 0.8f);
    for (i = 0; i < g_DuelMgr.GetDuelWatchUserCount(); ++i)
    {
        RenderColor(m_Pos.x, m_Pos.y - (ptSize.y + 1) * (i + 1), ptSize.x, ptSize.y);
    }
    EndRenderColor();

    for (i = 0; i < g_DuelMgr.GetDuelWatchUserCount(); ++i)
    {
        RenderImage(IMAGE_DUELWATCH_USERLIST_BOX, m_Pos.x, m_Pos.y - (ptSize.y + 1) * (i + 1),
                    ptSize.x, ptSize.y);
    }
}

void CNewUIDuelWatchWindow::SetPos(int, int)
{
}
void CNewUIDuelWatchWindow::StageContent()
{
    const char *locale = I18N::GetCurrentLocale();
    const bool languageChanged = locale_ != locale;
    bool dirty = languageChanged;
    if (languageChanged)
    {
        locale_ = locale;
        content_.labels[0] = I18N::Game::DoorkeeperTitus;
        content_.labels[1] = I18N::Game::SelectAnColosseumYouDLikeToWatch;
    }
    for (int i = 0; i < MAX_DUEL_CHANNELS; ++i)
        dirty = StageChannel(i, languageChanged) || dirty;
    if (dirty)
        ++content_.revision;
}
bool CNewUIDuelWatchWindow::StageChannel(int channel, bool languageChanged)
{
    const bool active = g_DuelMgr.IsDuelChannelEnabled(channel) != FALSE;
    const bool enabled = active && g_DuelMgr.IsDuelChannelJoinable(channel);
    const auto *first = g_DuelMgr.GetDuelChannelUserID1(channel);
    const auto *second = g_DuelMgr.GetDuelChannelUserID2(channel);
    if (!languageChanged && active_[channel] == active && content_.enabled[channel] == enabled &&
        players_[channel][0] == first && players_[channel][1] == second)
        return false;
    active_[channel] = active;
    players_[channel] = {first, second};
    content_.enabled[channel] = enabled;
    constexpr int HeaderLabels = 2, LabelsPerChannel = 3, CaptionCapacity = 256;
    const int offset = HeaderLabels + channel * LabelsPerChannel;
    wchar_t title[CaptionCapacity];
    mu_swprintf(title, I18N::Game::ColosseumD, channel + 1);
    content_.labels[offset] = title;
    content_.labels[offset + 1] = active ? players_[channel][0] + L" VS " + players_[channel][1]
                                         : std::wstring(I18N::Game::NoDuelOn);
    content_.labels[offset + 2] = I18N::Game::Watch;
    return true;
}

bool CNewUIDuelWatchWindow::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}
bool CNewUIDuelWatchWindow::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_, content_);
}

void SEASON3B::CNewUIDuelWindow::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool SEASON3B::CNewUIDuelWindow::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    RenderFrame();
    RenderContents();

    DisableAlphaBlend();

    return true;
}

void SEASON3B::CNewUIDuelWindow::RenderFrame()
{
    RenderImage(IMAGE_DUEL_BACK, m_Pos.x, m_Pos.y, 131, 70);
}

void SEASON3B::CNewUIDuelWindow::RenderContents()
{
    wchar_t strMyScore[12];
    wchar_t strDuelScore[12];
    mu_swprintf(strMyScore, L"%d", g_DuelMgr.GetScore(DUEL_HERO));
    mu_swprintf(strDuelScore, L"%d", g_DuelMgr.GetScore(DUEL_ENEMY));

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetTextColor(0, 0, 0, 255);
    g_RenderText.SetBgColor(0);
    g_RenderText.SetTextColor(0, 150, 255, 255);
    g_RenderText.RenderText(m_Pos.x + 55, m_Pos.y + 33, g_DuelMgr.GetDuelPlayerID(DUEL_HERO));
    g_RenderText.RenderText(m_Pos.x + 31, m_Pos.y + 33, strMyScore);
    g_RenderText.SetTextColor(255, 25, 25, 255);
    g_RenderText.RenderText(m_Pos.x + 55, m_Pos.y + 56, g_DuelMgr.GetDuelPlayerID(DUEL_ENEMY));
    g_RenderText.RenderText(m_Pos.x + 31, m_Pos.y + 56, strDuelScore);
}

void SEASON3B::CNewUIDuelWindow::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_Figure_ground.tga", IMAGE_DUEL_BACK,
                LegacyTextureFilter::Linear);
}

void SEASON3B::CNewUIDuelWindow::UnloadImages()
{
    DeleteBitmap(IMAGE_DUEL_BACK);
}

void CNewUIGuardWindow::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool CNewUIGuardWindow::Render()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    RenderFrame();

    std::list<const wchar_t *const *> ltext;
    if (m_eTimeType == CASTLESIEGE_STATE_REGSIEGE)
    {
        ltext.push_back(&I18N::Game::Status);
        ltext.push_back(&I18N::Game::Announce);
        ltext.push_back(&I18N::Game::List);
    }
    else
    {
        ltext.push_back(&I18N::Game::Status);
        ltext.push_back(&I18N::Game::Register);
        ltext.push_back(&I18N::Game::List);
    }
    m_TabBtn.ChangeRadioText(ltext);

    m_TabBtn.Render();

    switch (m_iNumCurOpenTab)
    {
    case TAB_SIEGE_INFO:
        RenderSeigeInfoTab();
        break;
    case TAB_REGISTER:
        RenderRegisterTab();
        break;
    case TAB_REGISTER_INFO:
        RenderRegisterInfoTab();
        break;
    }

    m_BtnExit.Render();
    DisableAlphaBlend();

    return true;
}

void CNewUIGuardWindow::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_GUARDWINDOW_BACK,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back01.tga", IMAGE_GUARDWINDOW_TOP,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-L.tga", IMAGE_GUARDWINDOW_LEFT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-R.tga", IMAGE_GUARDWINDOW_RIGHT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back03.tga", IMAGE_GUARDWINDOW_BOTTOM,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_exit_00.tga", IMAGE_GUARDWINDOW_EXIT_BTN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_guild_tab04.tga", IMAGE_GUARDWINDOW_TAB_BTN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_btn_empty_very_small.tga", IMAGE_GUARDWINDOW_BUTTON,
                LegacyTextureFilter::Linear);

    LoadBitmapW(L"Interface\\newui_item_table03(Up).tga", IMAGE_GUARDWINDOW_TOP_PIXEL,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_table03(Dw).tga", IMAGE_GUARDWINDOW_BOTTOM_PIXEL,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_table03(L).tga", IMAGE_GUARDWINDOW_LEFT_PIXEL,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_table03(R).tga", IMAGE_GUARDWINDOW_RIGHT_PIXEL,
                LegacyTextureFilter::Linear);

    LoadBitmapW(L"Interface\\newui_scrollbar_up.tga", IMAGE_GUARDWINDOW_SCROLL_TOP);
    LoadBitmapW(L"Interface\\newui_scrollbar_m.tga", IMAGE_GUARDWINDOW_SCROLL_MIDDLE);
    LoadBitmapW(L"Interface\\newui_scrollbar_down.tga", IMAGE_GUARDWINDOW_SCROLL_BOTTOM);
    LoadBitmapW(L"Interface\\newui_scroll_on.tga", IMAGE_GUARDWINDOW_SCROLLBAR_ON,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_scroll_off.tga", IMAGE_GUARDWINDOW_SCROLLBAR_OFF,
                LegacyTextureFilter::Linear);
}
void CNewUIGuardWindow::UnloadImages()
{
    DeleteBitmap(IMAGE_GUARDWINDOW_BOTTOM);
    DeleteBitmap(IMAGE_GUARDWINDOW_RIGHT);
    DeleteBitmap(IMAGE_GUARDWINDOW_LEFT);
    DeleteBitmap(IMAGE_GUARDWINDOW_TOP);
    DeleteBitmap(IMAGE_GUARDWINDOW_BACK);
    DeleteBitmap(IMAGE_GUARDWINDOW_EXIT_BTN);
    DeleteBitmap(IMAGE_GUARDWINDOW_TAB_BTN);
    DeleteBitmap(IMAGE_GUARDWINDOW_BUTTON);

    DeleteBitmap(IMAGE_GUARDWINDOW_TOP_PIXEL);
    DeleteBitmap(IMAGE_GUARDWINDOW_BOTTOM_PIXEL);
    DeleteBitmap(IMAGE_GUARDWINDOW_RIGHT_PIXEL);
    DeleteBitmap(IMAGE_GUARDWINDOW_LEFT_PIXEL);

    DeleteBitmap(IMAGE_GUARDWINDOW_SCROLL_TOP);
    DeleteBitmap(IMAGE_GUARDWINDOW_SCROLL_MIDDLE);
    DeleteBitmap(IMAGE_GUARDWINDOW_SCROLL_BOTTOM);
    DeleteBitmap(IMAGE_GUARDWINDOW_SCROLLBAR_ON);
    DeleteBitmap(IMAGE_GUARDWINDOW_SCROLLBAR_OFF);
}

void CNewUIGuardWindow::RenderFrame()
{
    RenderImage(IMAGE_GUARDWINDOW_BACK, m_Pos.x, m_Pos.y, 190.f, 429.f);
    RenderImage(IMAGE_GUARDWINDOW_TOP, m_Pos.x, m_Pos.y, 190.f, 64.f);
    RenderImage(IMAGE_GUARDWINDOW_LEFT, m_Pos.x, m_Pos.y + 64, 21.f, 320.f);
    RenderImage(IMAGE_GUARDWINDOW_RIGHT, m_Pos.x + INVENTORY_WIDTH - 21, m_Pos.y + 64, 21.f, 320.f);
    RenderImage(IMAGE_GUARDWINDOW_BOTTOM, m_Pos.x, m_Pos.y + INVENTORY_HEIGHT - 45, 190.f, 45.f);

    wchar_t szText[256] = {
        0,
    };
    float fPos_x = m_Pos.x + 15.0f, fPos_y = m_Pos.y;
    float fLine_y = 13.0f;

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetTextColor(220, 220, 220, 255);
    g_RenderText.SetBgColor(0, 0, 0, 0);

    mu_swprintf(szText, L"%ls", I18N::Game::GuardNPC);
    g_RenderText.RenderText(fPos_x, fPos_y + fLine_y, szText, 160.0f, 0, RT3_SORT_CENTER);

    POINT ptOrigin = {m_Pos.x, m_Pos.y + 50};
    g_RenderText.SetFont(LegacyFontRole::Bold);

    if (m_szOwnerGuildMaster[0])
    {
        mu_swprintf(szText, I18N::Game::OfficialSealOfKingS, m_szOwnerGuildMaster);
    }
    else
    {
        mu_swprintf(szText, I18N::Game::OfficialSealOfKingS, I18N::Game::None);
    }
    g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, szText, 190, 0, RT3_SORT_CENTER);

    ptOrigin.y += 15;
    if (m_szOwnerGuild[0])
    {
        mu_swprintf(szText, I18N::Game::AffiliatedGuildS, m_szOwnerGuild);
    }
    else
    {
        mu_swprintf(szText, I18N::Game::AffiliatedGuildS, I18N::Game::None);
    }

    g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, szText, 190, 0, RT3_SORT_CENTER);
}

void CNewUIGuardWindow::RenderSeigeInfoTab()
{
    POINT ptOrigin = {m_Pos.x, m_Pos.y + 125};
    wchar_t szTemp[256];

    g_RenderText.SetFont(LegacyFontRole::Normal);
    mu_swprintf(szTemp, I18N::Game::StartingUUUUU, m_wStartYear, m_byStartMonth, m_byStartDay,
                m_byStartHour, m_byStartMinute);
    g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, szTemp, 190, 0, RT3_SORT_CENTER);

    ptOrigin.y += 14;
    mu_swprintf(szTemp, I18N::Game::UntillUUUUU, m_wEndYear, m_byEndMonth, m_byEndDay, m_byEndHour,
                m_byEndMinute);
    g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, szTemp, 190, 0, RT3_SORT_CENTER);

    ptOrigin.y += 14;
    switch (m_eTimeType)
    {
    case CASTLESIEGE_STATE_NONE:
    case CASTLESIEGE_STATE_IDLE_1:
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::SiegePeriodIsOver, 190, 0,
                                RT3_SORT_CENTER);
        break;
    case CASTLESIEGE_STATE_REGSIEGE:
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::SiegeRegistrationPeriod, 190, 0,
                                RT3_SORT_CENTER);
        break;
    case CASTLESIEGE_STATE_IDLE_2:
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y,
                                I18N::Game::StandbyPeriodForSignRegistration, 190, 0,
                                RT3_SORT_CENTER);
        break;
    case CASTLESIEGE_STATE_REGMARK:
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::PeriodForSignRegistration, 190,
                                0, RT3_SORT_CENTER);
        break;
    case CASTLESIEGE_STATE_IDLE_3:
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::StandbyPeriodForAnnouncement,
                                190, 0, RT3_SORT_CENTER);
        break;
    case CASTLESIEGE_STATE_NOTIFY:
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::AnnouncementPeriod, 190, 0,
                                RT3_SORT_CENTER);
        break;
    case CASTLESIEGE_STATE_READYSIEGE:
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::SiegePreparationPeriod, 190, 0,
                                RT3_SORT_CENTER);
        break;
    case CASTLESIEGE_STATE_STARTSIEGE:
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::SiegePeriod, 190, 0,
                                RT3_SORT_CENTER);
        break;
    case CASTLESIEGE_STATE_ENDSIEGE:
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::TrucePeriod, 190, 0,
                                RT3_SORT_CENTER);
        break;
    case CASTLESIEGE_STATE_ENDCYCLE:
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::SiegeIsOver, 190, 0,
                                RT3_SORT_CENTER);
        break;
    }

    if (m_eTimeType < CASTLESIEGE_STATE_STARTSIEGE)
    {
        ptOrigin.y += 35;
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::ExpectedSiegePeriodIs, 190, 0,
                                RT3_SORT_CENTER);

        ptOrigin.y += 14;
        mu_swprintf(szTemp, I18N::Game::UUUUU, m_wSiegeStartYear, m_bySiegeStartMonth,
                    m_bySiegeStartDay, m_bySiegeStartHour, m_bySiegeStartMinute);
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, szTemp, 190, 0, RT3_SORT_CENTER);

        ptOrigin.y += 35;
        mu_swprintf(szTemp, I18N::Game::UUURemainedForTheNextStage, m_dwStateLeftSec / 3600,
                    (m_dwStateLeftSec % 3600) / 60, (m_dwStateLeftSec % 3600) % 60);
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, szTemp, 190, 0, RT3_SORT_CENTER);
    }
}

void CNewUIGuardWindow::RenderRegisterTab()
{
    POINT ptOrigin = {m_Pos.x, m_Pos.y + 125};
    g_RenderText.SetFont(LegacyFontRole::Normal);

    switch (m_eTimeType)
    {
    case CASTLESIEGE_STATE_NONE:
    case CASTLESIEGE_STATE_IDLE_1:
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::SiegePeriodIsOver, 190, 0,
                                RT3_SORT_CENTER);
        break;
    case CASTLESIEGE_STATE_REGSIEGE:
        if (Hero->GuildStatus == G_MASTER)
        {
            if (!g_GuardsMan.HasRegistered())
            {
                if (!wcscmp(GuildMark[Hero->GuildMarkIndex].UnionName, m_szOwnerGuild) ||
                    !wcscmp(GuildMark[Hero->GuildMarkIndex].GuildName, m_szOwnerGuild))
                {
                    m_BtnProclaim.Lock();
                    m_BtnProclaim.ChangeImgColor(BUTTON_STATE_UP, RGBA(100, 100, 100, 255));
                    m_BtnProclaim.ChangeTextColor(RGBA(100, 100, 100, 255));
                }
                else
                {
                    m_BtnProclaim.UnLock();
                    m_BtnProclaim.ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
                    m_BtnProclaim.ChangeTextColor(RGBA(255, 255, 255, 255));
                }
                m_BtnProclaim.Render();
            }
            else
            {
                g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::Announced, 190, 0,
                                        RT3_SORT_CENTER);
            }
        }
        else
        {
            g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::NotAGuildMaster, 190, 0,
                                    RT3_SORT_CENTER);
        }
        break;
    case CASTLESIEGE_STATE_IDLE_2:
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y,
                                I18N::Game::StandbyPeriodForSignRegistration, 190, 0,
                                RT3_SORT_CENTER);
        break;
    case CASTLESIEGE_STATE_REGMARK: {
        if (g_GuardsMan.HasRegistered())
        {
            g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::RegisterTheAcquiredSign,
                                    190, 0, RT3_SORT_CENTER);
            ptOrigin.y += 30;

            int nMarkCount = g_GuardsMan.GetMyMarkCount();
            wchar_t szBuffer[256];
            mu_swprintf(szBuffer, I18N::Game::AcquiredNoOfSignU, nMarkCount);
            g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, szBuffer, 190, 0, RT3_SORT_CENTER);

            ptOrigin.y += 14;
            mu_swprintf(szBuffer, I18N::Game::RegisteredNoOfSignU, g_GuardsMan.GetRegMarkCount());
            g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, szBuffer, 190, 0, RT3_SORT_CENTER);

            if (nMarkCount > 0)
            {
                m_BtnRegister.UnLock();
                m_BtnRegister.ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
                m_BtnRegister.ChangeTextColor(RGBA(255, 255, 255, 255));
            }
            else
            {
                m_BtnRegister.ChangeImgColor(BUTTON_STATE_UP, RGBA(100, 100, 100, 255));
                m_BtnRegister.ChangeTextColor(RGBA(100, 100, 100, 255));
                m_BtnRegister.Lock();
            }
            m_BtnRegister.Render();
        }
        else
        {
            g_RenderText.RenderText(ptOrigin.x, ptOrigin.y,
                                    I18N::Game::ThisGuildIsNotRegisteredInCastleSiege, 190, 0,
                                    RT3_SORT_CENTER);
        }
    }
    break;
    case CASTLESIEGE_STATE_IDLE_3:
        g_RenderText.SetFont(LegacyFontRole::Bold);
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y,
                                I18N::Game::AnnouncementAndRegistrationPeriod, 190, 0,
                                RT3_SORT_CENTER);
        ptOrigin.y += 14;
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::HasEnded, 190, 0,
                                RT3_SORT_CENTER);
        break;
    case CASTLESIEGE_STATE_NOTIFY:
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::AnnouncementPeriod, 190, 0,
                                RT3_SORT_CENTER);
        break;
    case CASTLESIEGE_STATE_READYSIEGE:
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::SiegePreparationPeriod, 190, 0,
                                RT3_SORT_CENTER);
        break;
    case CASTLESIEGE_STATE_STARTSIEGE:
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::SiegePeriod, 190, 0,
                                RT3_SORT_CENTER);
        break;
    case CASTLESIEGE_STATE_ENDSIEGE:
        g_RenderText.SetFont(LegacyFontRole::Bold);
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::TrucePeriod, 190, 0,
                                RT3_SORT_CENTER);
        break;
    case CASTLESIEGE_STATE_ENDCYCLE:
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::SiegeIsOver, 190, 0,
                                RT3_SORT_CENTER);
        break;
    }
}

void CNewUIGuardWindow::RenderRegisterInfoTab()
{
    POINT ptOrigin = {m_Pos.x, m_Pos.y + 125};
    g_RenderText.SetFont(LegacyFontRole::Bold);

    if (m_eTimeType == CASTLESIEGE_STATE_REGSIEGE || m_eTimeType == CASTLESIEGE_STATE_REGMARK)
    {
        EnableAlphaTest();
        RenderImage(IMAGE_GUARDWINDOW_LEFT_PIXEL, ptOrigin.x + 11, ptOrigin.y - 14, 14, 255);
        RenderImage(IMAGE_GUARDWINDOW_RIGHT_PIXEL, ptOrigin.x + 166, ptOrigin.y - 14, 14, 255);
        RenderImage(IMAGE_GUARDWINDOW_TOP_PIXEL, ptOrigin.x + 14, ptOrigin.y - 14, 161, 14);
        RenderImage(IMAGE_GUARDWINDOW_BOTTOM_PIXEL, ptOrigin.x + 14, ptOrigin.y + 232, 161, 14);
        DisableAlphaBlend();

        m_DeclareGuildListBox.Render();
    }
    else if (m_eTimeType == CASTLESIEGE_STATE_NOTIFY || m_eTimeType == CASTLESIEGE_STATE_READYSIEGE)
    {
        EnableAlphaTest();
        RenderImage(IMAGE_GUARDWINDOW_LEFT_PIXEL, ptOrigin.x + 11, ptOrigin.y - 14, 14, 215);
        RenderImage(IMAGE_GUARDWINDOW_RIGHT_PIXEL, ptOrigin.x + 166, ptOrigin.y - 14, 14, 215);
        RenderImage(IMAGE_GUARDWINDOW_TOP_PIXEL, ptOrigin.x + 14, ptOrigin.y - 14, 161, 14);
        RenderImage(IMAGE_GUARDWINDOW_BOTTOM_PIXEL, ptOrigin.x + 14, ptOrigin.y + 192, 161, 14);

        RenderImage(IMAGE_GUARDWINDOW_LEFT_PIXEL, ptOrigin.x + 11, ptOrigin.y + 220, 14, 25);
        RenderImage(IMAGE_GUARDWINDOW_RIGHT_PIXEL, ptOrigin.x + 166, ptOrigin.y + 220, 14, 25);
        RenderImage(IMAGE_GUARDWINDOW_TOP_PIXEL, ptOrigin.x + 14, ptOrigin.y + 220, 161, 14);
        RenderImage(IMAGE_GUARDWINDOW_BOTTOM_PIXEL, ptOrigin.x + 14, ptOrigin.y + 237, 161, 14);
        DisableAlphaBlend();

        m_GuildListBox.Render();
    }
    else if (m_eTimeType == CASTLESIEGE_STATE_NOTIFY)
    {
        wchar_t szBuffer[256];
        mu_swprintf(szBuffer, I18N::Game::OnDD3Pm, 1, 1);
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, szBuffer, 190, 0, RT3_SORT_CENTER);
        ptOrigin.y += 14;
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::CastleSiegeWillStart, 190, 0,
                                RT3_SORT_CENTER);
    }
    else if (m_eTimeType == CASTLESIEGE_STATE_ENDSIEGE)
    {
        g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, I18N::Game::TrucePeriod, 190, 0,
                                RT3_SORT_CENTER);
    }

    if (g_GuardsMan.HasRegistered() && CASTLESIEGE_STATE_REGSIEGE <= m_eTimeType &&
        m_eTimeType <= CASTLESIEGE_STATE_REGMARK && Hero->GuildStatus == G_MASTER)
    {
        m_BtnGiveUp.Render();
    }
}

void CNewUIGuardWindow::RenderScrollBarFrame(int iPos_x, int iPos_y, int iHeight)
{
    RenderImage(IMAGE_GUARDWINDOW_SCROLL_TOP, iPos_x, iPos_y, 7, 3);
#ifdef PBG_ADD_INGAMESHOP_UI_ITEMSHOP

    const SessionBitmapMetadata pImage = Bitmaps[IMAGE_GUARDWINDOW_SCROLL_MIDDLE];
    if (!IsValid(pImage.Asset))
    {
        return;
    }
    float _Temp = pImage.Height - 1;
    float _fMiddle_Cnt = (iHeight - 6) / _Temp;
    int _iMiddle_Cnt = (int)_fMiddle_Cnt;
    float _Middle_rest = _fMiddle_Cnt - _iMiddle_Cnt;

    for (int i = 0; i < _iMiddle_Cnt; i++)
        RenderImage(IMAGE_GUARDWINDOW_SCROLL_MIDDLE, iPos_x, iPos_y + (float)(i * _Temp + 3), 7,
                    _Temp);

    RenderImage(IMAGE_GUARDWINDOW_SCROLL_MIDDLE, iPos_x, iPos_y + (float)(_iMiddle_Cnt * _Temp + 3),
                7, _Temp * _Middle_rest);
#else  //PBG_ADD_INGAMESHOP_UI_ITEMSHOP
    RenderBitmap(IMAGE_GUARDWINDOW_SCROLL_MIDDLE, iPos_x, iPos_y + 3, 7.f, iHeight - 6, 0, 0,
                 7.f / 8.f, 15.f / 16.f);
#endif //PBG_ADD_INGAMESHOP_UI_ITEMSHOP
    RenderImage(IMAGE_GUARDWINDOW_SCROLL_BOTTOM, iPos_x, iPos_y + iHeight - 3, 7, 3);
}

void CNewUIGuardWindow::RenderScrollBar(int iPos_x, int iPos_y, BOOL bIsClicked)
{
    if (bIsClicked)
    {
        glColor3ub(200, 200, 200);
        RenderImage(IMAGE_GUARDWINDOW_SCROLLBAR_ON, iPos_x, iPos_y, 15, 30);
    }
    else
    {
        glColor3ub(255, 255, 255);
        RenderImage(IMAGE_GUARDWINDOW_SCROLLBAR_ON, iPos_x, iPos_y, 15, 30);
    }
    glColor3ub(255, 255, 255);
}

void SEASON3B::CNewUISiegeWarfare::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool SEASON3B::CNewUISiegeWarfare::Render()
{
    if (m_pSiegeWarUI == NULL || gMapManager.InBattleCastle() == false)
    {
        return true;
    }

    m_pSiegeWarUI->Render();

    return true;
}

bool SEASON3B::CNewUISiegeWarBase::Render()
{
    wchar_t szText[256] = {
        0,
    };

    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, m_fMiniMapAlpha);
    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetBgColor(0, 0, 0, 0);

    RenderBitmap(IMAGE_MINIMAP, (float)(m_MiniMapPos.x), (float)(m_MiniMapPos.y), 128.f, 128.f,
                 m_fMiniMapTexU, m_fMiniMapTexV, 0.5f * m_iMiniMapScale, 0.5f * m_iMiniMapScale);

    RenderImage(IMAGE_MINIMAP_FRAME, m_MiniMapFramePos.x, m_MiniMapFramePos.y, MINIMAP_FRAME_WIDTH,
                MINIMAP_FRAME_HEIGHT);
    RenderImage(IMAGE_TIME_FRAME, m_TimeUIPos.x, m_TimeUIPos.y, TIME_FRAME_WIDTH,
                TIME_FRAME_HEIGHT);

    glColor4f(1.f, 1.f, 1.f, m_fMiniMapAlpha);
    if (IsBattleCastleStart())
    {
        g_RenderText.SetFont(LegacyFontRole::Large);

        mu_swprintf(szText, L"%d:%02d", m_iHour, m_iMinute);
        g_RenderText.RenderText(m_TimeUIPos.x, m_TimeUIPos.y + 10, szText, 134, 0, RT3_SORT_CENTER);
    }

    OnRender();

    glColor4f(1.f, 1.f, 0.f, m_fMiniMapAlpha);
    RenderColor((float)(m_HeroPosInMiniMap.x), (float)(m_HeroPosInMiniMap.y), 3, 3);

    DisableAlphaBlend();

    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, m_fMiniMapAlpha);

    if (m_bRenderSkillUI == true)
    {
        RenderImage(IMAGE_BATTLESKILL_FRAME, m_SkillFramePos.x, m_SkillFramePos.y,
                    BATTLESKILL_FRAME_WIDTH, BATTLESKILL_FRAME_HEIGHT);

        RenderSkillIcon();

        m_BtnSkillScroll[0].ChangeAlpha(m_fMiniMapAlpha);
        m_BtnSkillScroll[1].ChangeAlpha(m_fMiniMapAlpha);
        m_BtnSkillScroll[0].Render();
        m_BtnSkillScroll[1].Render();
    }

    m_BtnAlpha.SetFont(LegacyFontRole::Bold);
    m_BtnAlpha.ChangeAlpha(m_fMiniMapAlpha);
    m_BtnAlpha.Render();

    DisableAlphaBlend();

    return true;
}

void SEASON3B::CNewUISiegeWarBase::RenderCmdIconInMiniMap()
{
    int iWidth, iHeight;
    wchar_t szText[256] = {
        0,
    };
    POINT Pos;
    memset(&Pos, 0, sizeof(POINT));

    for (int i = 0; i < MAX_COMMANDGROUP; i++)
    {
        int iBWidth;
        switch (m_CmdBuffer[i].byCmd)
        {
        case 0:
            iWidth = COMMAND_ATTACK_WIDTH;
            iHeight = COMMAND_ATTACK_HEIGHT;
            iBWidth = 16;
            break;
        case 1:
            iWidth = COMMAND_DEFENCE_WIDTH;
            iHeight = COMMAND_DEFENCE_HEIGHT;
            iBWidth = 32;
            break;
        case 2:
            iWidth = COMMAND_WAIT_WIDTH;
            iHeight = COMMAND_WAIT_HEIGHT;
            iBWidth = 16;
            break;
        }

        if (m_CmdBuffer[i].byCmd != 3 && m_CmdBuffer[i].byTeam >= 0 && m_CmdBuffer[i].byTeam <= 6)
        {
            Pos.x =
                (m_CmdBuffer[i].byX) / m_iMiniMapScale - m_MiniMapScaleOffset.x + m_MiniMapPos.x;
            Pos.y = (256 - m_CmdBuffer[i].byY) / m_iMiniMapScale - m_MiniMapScaleOffset.y +
                    m_MiniMapPos.y;

            if (Pos.x < m_MiniMapPos.x || Pos.x > m_MiniMapPos.x + 128 || Pos.y < m_MiniMapPos.y ||
                Pos.y > m_MiniMapPos.y + 128)
                continue;

            if (m_CmdBuffer[i].byLifeTime > 0)
            {
                glColor4f(1.f, 1.f + sinf(m_CmdBuffer[i].byLifeTime * 0.2f),
                          1.f + sinf(m_CmdBuffer[i].byLifeTime * 0.2f), m_fMiniMapAlpha);
            }
            else
            {
                glColor4f(1.f, 1.f, 1.f, m_fMiniMapAlpha);
            }
            mu_swprintf(szText, L"%d", m_CmdBuffer[i].byTeam + 1);
            g_RenderText.RenderText(Pos.x - 12, Pos.y - 5, szText);
            RenderBitmap(IMAGE_COMMAND_ATTACK + m_CmdBuffer[i].byCmd, Pos.x - 7, Pos.y - 7, 11.f,
                         11.f, 0.f, 0.f, ((float)iWidth - 1.f) / (float)iBWidth,
                         ((float)iHeight - 1.f) / 16.f);
        }
    }
}

void SEASON3B::CNewUISiegeWarBase::RenderSkillIcon()
{
    int iUseSkillDestKill;
    int iSelectSkill;
    int iCurKillCount;

    wchar_t szText[256] = {};

    iUseSkillDestKill = SkillAttribute[Hero->GuildSkill].KillCount;

    iSelectSkill = (*m_iterCurBattleSkill);
    iCurKillCount = Hero->GuildMasterKillCount;

    if (Hero->GuildMasterKillCount < iUseSkillDestKill)
    {
        glColor4f(1.f, 0.5f, 0.5f, m_fMiniMapAlpha);
    }

    int src_x, src_y;
    src_x = ((iSelectSkill - 57) % 8) * 20.f;
    src_y = ((iSelectSkill - 57) / 8) * 28.f;

    RenderImage(IMAGE_SKILL_ICON, m_SkillIconPos.x + 1, m_SkillIconPos.y, (float)SKILL_ICON_WIDTH,
                (float)SKILL_ICON_HEIGHT, src_x, src_y);

    glColor4f(1.f, 1.f, 1.f, m_fMiniMapAlpha);

    g_RenderText.SetFont(LegacyFontRole::Large);
    mu_swprintf(szText, L"%d", iUseSkillDestKill);
    g_RenderText.RenderText(m_UseSkillDestKillPos.x, m_UseSkillDestKillPos.y, szText);
    mu_swprintf(szText, L"%d", iCurKillCount);
    g_RenderText.RenderText(m_CurKillCountPos.x, m_CurKillCountPos.y, szText);

    if (m_bRenderToolTip == true)
    {
        m_SkillTooltip.Render(m_SkillTooltipPos.x, m_SkillTooltipPos.y,
                              FindHotKey(Hero->GuildSkill), Hero->GuildSkill, STRP_BOTTOMCENTER);
    }
}

// SetPos
void SEASON3B::CNewUISiegeWarBase::SetPos(int x, int y)
{
    m_MiniMapFramePos.x = x;
    m_MiniMapFramePos.y = y;
    m_MiniMapPos.x = m_MiniMapFramePos.x + 25;
    m_MiniMapPos.y = m_MiniMapFramePos.y + 28;
    m_TimeUIPos.x = m_MiniMapFramePos.x + 20;
    m_TimeUIPos.y = m_MiniMapFramePos.y + MINIMAP_FRAME_HEIGHT - 4;
    m_SkillFramePos.x = x + 26;
    m_SkillFramePos.y = y - BATTLESKILL_FRAME_HEIGHT;
    m_BtnSkillScrollUpPos.x = m_SkillFramePos.x + 48;
    m_BtnSkillScrollUpPos.y = m_SkillFramePos.y + 21;
    m_BtnSkillScrollDnPos.x = m_BtnSkillScrollUpPos.x;
    m_BtnSkillScrollDnPos.y = m_BtnSkillScrollUpPos.y + 15;
    m_SkillIconPos.x = m_SkillFramePos.x + 25;
    m_SkillIconPos.y = m_SkillFramePos.y + 21;
    m_UseSkillDestKillPos.x = m_SkillFramePos.x + 78;
    m_UseSkillDestKillPos.y = m_SkillFramePos.y + 28;
    m_CurKillCountPos.x = m_SkillFramePos.x + 97;
    m_CurKillCountPos.y = m_UseSkillDestKillPos.y;
    m_BtnAlphaPos.x = m_MiniMapFramePos.x + 58;
    m_BtnAlphaPos.y = m_MiniMapFramePos.y + 4;

    m_SkillTooltipPos.y = m_SkillFramePos.y + 16;
    m_SkillTooltipPos.x = m_SkillFramePos.x + 30;

    OnSetPos(x, y);
}

void SEASON3B::CNewUISiegeWarBase::LoadImages()
{
    LoadBitmapW(L"World31\\Map1.jpg", IMAGE_MINIMAP, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_SW_Minimap_Frame.tga", IMAGE_MINIMAP_FRAME,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_SW_Time_Frame.tga", IMAGE_TIME_FRAME,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\i_attack.tga", IMAGE_COMMAND_ATTACK);
    LoadBitmapW(L"Interface\\i_defense.tga", IMAGE_COMMAND_DEFENCE);
    LoadBitmapW(L"Interface\\i_wait.tga", IMAGE_COMMAND_WAIT);
    LoadBitmapW(L"Interface\\newui_SW_BattleSkill_Frame.tga", IMAGE_BATTLESKILL_FRAME,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_Bt_skill_scroll_up.jpg", IMAGE_SKILL_BTN_SCROLL_UP,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_Bt_skill_scroll_dn.jpg", IMAGE_SKILL_BTN_SCROLL_DN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_skill2.jpg", IMAGE_SKILL_ICON, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_SW_MiniMap_Bt_clearness.jpg", IMAGE_BTN_ALPHA,
                LegacyTextureFilter::Linear);

    OnLoadImages();
}

void SEASON3B::CNewUISiegeWarCommander::RenderCharPosInMiniMap()
{
    float fPosX, fPosY;

    for (int i = 0; i < CharactersClient.Size(); ++i)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        CHARACTER *c = &CharactersClient[i];
        if (c != NULL && c->Object.Live && c != Hero &&
            (c->Object.Kind == KIND_PLAYER || c->Object.Kind == KIND_MONSTER ||
             c->Object.Kind == KIND_NPC))
        {
            OBJECT *o = &c->Object;

            if (g_isCharacterBuff(o, static_cast<eBuffState>(m_dwBuffState)))
            {
                continue;
            }
            else
            {
                if (o->Kind == KIND_NPC || o->Kind == KIND_MONSTER && o->Type == MODEL_LIFE_STONE)
                {
                    glColor4f(1.f, 0.f, 1.f, m_fMiniMapAlpha);
                }
                else
                {
                    glColor4f(0.8f, 0.f, 0.f, m_fMiniMapAlpha);
                }
            }

            fPosX = ((c->PositionX)) / m_iMiniMapScale - m_MiniMapScaleOffset.x + m_MiniMapPos.x;
            fPosY =
                (256 - (c->PositionY)) / m_iMiniMapScale - m_MiniMapScaleOffset.y + m_MiniMapPos.y;
            RenderColor(fPosX, fPosY, 3, 3);
        }
    }
}

void SEASON3B::CNewUISiegeWarCommander::RenderGuildMemberPosInMiniMap()
{
    std::vector<VisibleUnitLocation>::iterator UnitIterator;
    POINT Pos;
    memset(&Pos, 0, sizeof(POINT));

    for (UnitIterator = m_vGuildMemberLocationBuffer.begin();
         UnitIterator != m_vGuildMemberLocationBuffer.end(); ++UnitIterator)
    {
        switch (UnitIterator->bIndex)
        {
        case 0:
            glColor4f(0.f, 1.f, 0.f, m_fMiniMapAlpha);
            break;

        case 1:
            glColor4f(0.f, 1.f, 1.f, m_fMiniMapAlpha);
            break;

        case 2:
            glColor4f(1.f, 1.f, 0.f, m_fMiniMapAlpha);
            break;
        }
        Pos.x = (UnitIterator->x) / m_iMiniMapScale - m_MiniMapScaleOffset.x + m_MiniMapPos.x;
        Pos.y = (256 - UnitIterator->y) / m_iMiniMapScale - m_MiniMapScaleOffset.y + m_MiniMapPos.y;

        if (Pos.x < m_MiniMapPos.x || Pos.x > m_MiniMapPos.x + 128 || Pos.y < m_MiniMapPos.y ||
            Pos.y > m_MiniMapPos.y + 128)
        {
            continue;
        }

        RenderColor(Pos.x, Pos.y, 3, 3);
    }
}

void SEASON3B::CNewUISiegeWarCommander::RenderCmdIconAtMouse()
{
    int iWidth, iHeight;
    wchar_t szText[256] = {
        0,
    };

    switch (m_iCurSelectBtnCommand)
    {
    case 0:
        iWidth = COMMAND_ATTACK_WIDTH;
        iHeight = COMMAND_ATTACK_HEIGHT;
        break;
    case 1:
        iWidth = COMMAND_DEFENCE_WIDTH;
        iHeight = COMMAND_DEFENCE_HEIGHT;
        break;
    case 2:
        iWidth = COMMAND_WAIT_WIDTH;
        iHeight = COMMAND_WAIT_HEIGHT;
        break;
    }

    mu_swprintf(szText, L"%d", m_iCurSelectBtnGroup + 1);
    g_RenderText.RenderText(MouseX - 13, MouseY - 6, szText);
    RenderImage(IMAGE_COMMAND_ATTACK + m_iCurSelectBtnCommand, MouseX - 8, MouseY - 8, iWidth,
                iHeight);
}

void SEASON3B::CNewUISiegeWarCommander::RenderCmdGroupBtn()
{
    for (int i = 0; i < MAX_COMMANDGROUP; i++)
    {
        m_BtnCommandGroup[i].SetFont(LegacyFontRole::Bold);
        m_BtnCommandGroup[i].ChangeAlpha(m_fMiniMapAlpha);
        m_BtnCommandGroup[i].Render();
    }
}

void SEASON3B::CNewUISiegeWarCommander::RenderCmdBtn()
{
    if (m_iCurSelectBtnGroup < 5)
    {
        for (int i = 0; i < MINIMAP_CMD_MAX; i++)
        {
            m_BtnCommand[i].ChangeButtonInfo(m_BtnCommandGroupPos.x + MINIMAP_BTN_GROUP_WIDTH,
                                             m_BtnCommandGroupPos.y +
                                                 (m_iCurSelectBtnGroup * MINIMAP_BTN_GROUP_HEIGHT) +
                                                 (i * MINIMAP_BTN_GROUP_HEIGHT),
                                             MINIMAP_BTN_COMMAND_WIDTH, MINIMAP_BTN_COMMAND_HEIGHT);
            m_BtnCommand[i].ChangeAlpha(m_fMiniMapAlpha);
            m_BtnCommand[i].Render();
        }
        RenderImage(IMAGE_COMMAND_ATTACK, m_BtnCommandGroupPos.x + MINIMAP_BTN_GROUP_WIDTH + 8,
                    m_BtnCommandGroupPos.y + (m_iCurSelectBtnGroup * MINIMAP_BTN_GROUP_HEIGHT) + 5,
                    COMMAND_ATTACK_WIDTH, COMMAND_ATTACK_HEIGHT);
        RenderImage(IMAGE_COMMAND_DEFENCE, m_BtnCommandGroupPos.x + MINIMAP_BTN_GROUP_WIDTH + 8,
                    m_BtnCommandGroupPos.y + (m_iCurSelectBtnGroup * MINIMAP_BTN_GROUP_HEIGHT) +
                        MINIMAP_BTN_GROUP_HEIGHT + 3,
                    COMMAND_DEFENCE_WIDTH, COMMAND_DEFENCE_HEIGHT);
        RenderImage(IMAGE_COMMAND_WAIT, m_BtnCommandGroupPos.x + MINIMAP_BTN_GROUP_WIDTH + 10,
                    m_BtnCommandGroupPos.y + (m_iCurSelectBtnGroup * MINIMAP_BTN_GROUP_HEIGHT) +
                        (2 * MINIMAP_BTN_GROUP_HEIGHT) + 5,
                    COMMAND_WAIT_WIDTH, COMMAND_WAIT_HEIGHT);
    }
    else
    {
        for (int i = 0; i < MINIMAP_CMD_MAX; i++)
        {
            m_BtnCommand[i].ChangeButtonInfo(m_BtnCommandGroupPos.x + MINIMAP_BTN_GROUP_WIDTH,
                                             m_BtnCommandGroupPos.y +
                                                 (4 * MINIMAP_BTN_GROUP_HEIGHT) +
                                                 (i * MINIMAP_BTN_GROUP_HEIGHT),
                                             MINIMAP_BTN_COMMAND_WIDTH, MINIMAP_BTN_COMMAND_HEIGHT);
            m_BtnCommand[i].ChangeAlpha(m_fMiniMapAlpha);
            m_BtnCommand[i].Render();
        }
        RenderImage(IMAGE_COMMAND_ATTACK, m_BtnCommandGroupPos.x + MINIMAP_BTN_GROUP_WIDTH + 8,
                    m_BtnCommandGroupPos.y + (4 * MINIMAP_BTN_GROUP_HEIGHT) + 5,
                    COMMAND_ATTACK_WIDTH, COMMAND_ATTACK_HEIGHT);
        RenderImage(IMAGE_COMMAND_DEFENCE, m_BtnCommandGroupPos.x + MINIMAP_BTN_GROUP_WIDTH + 8,
                    m_BtnCommandGroupPos.y + (4 * MINIMAP_BTN_GROUP_HEIGHT) +
                        MINIMAP_BTN_GROUP_HEIGHT + 3,
                    COMMAND_DEFENCE_WIDTH, COMMAND_DEFENCE_HEIGHT);
        RenderImage(IMAGE_COMMAND_WAIT, m_BtnCommandGroupPos.x + MINIMAP_BTN_GROUP_WIDTH + 10,
                    m_BtnCommandGroupPos.y + (4 * MINIMAP_BTN_GROUP_HEIGHT) +
                        (2 * MINIMAP_BTN_GROUP_HEIGHT) + 5,
                    COMMAND_WAIT_WIDTH, COMMAND_WAIT_HEIGHT);
    }
}

void SEASON3B::CNewUISiegeWarObserver::RenderCharPosInMiniMap()
{
    float fPosX, fPosY;

    glColor4f(0.8f, 0.8f, 0.8f, m_fMiniMapAlpha);

    for (int i = 0; i < CharactersClient.Size(); ++i)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        CHARACTER *c = &CharactersClient[i];
        if (c != NULL && c->Object.Live && c != Hero &&
            (c->Object.Kind == KIND_PLAYER || c->Object.Kind == KIND_MONSTER ||
             c->Object.Kind == KIND_NPC))
        {
            fPosX = ((c->PositionX)) / m_iMiniMapScale - m_MiniMapScaleOffset.x + m_MiniMapPos.x;
            fPosY =
                (256 - (c->PositionY)) / m_iMiniMapScale - m_MiniMapScaleOffset.y + m_MiniMapPos.y;
            RenderColor(fPosX, fPosY, 3, 3);
        }
    }
}

// Construction/Destruction

// OnCreate

// OnRelease

// OnUpdate

// OnRender

// OnCreate

// RenderCharPosInMiniMap
// 미니맵에 모든 캐릭터를 렌더
void SEASON3B::CNewUISiegeWarSoldier::RenderCharPosInMiniMap()
{
    float fPosX, fPosY;

    // 미니멥에 플레이어 렌더
    for (int i = 0; i < CharactersClient.Size(); ++i)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        CHARACTER *c = &CharactersClient[i];
        if (c != NULL && c->Object.Live && c != Hero &&
            (c->Object.Kind == KIND_PLAYER || c->Object.Kind == KIND_MONSTER ||
             c->Object.Kind == KIND_NPC))
        {
            OBJECT *o = &c->Object;

            if (g_isCharacterBuff(o, static_cast<eBuffState>(m_dwBuffState)))
            {
                glColor4f(0.f, 1.f, 0.f, m_fMiniMapAlpha);
            }
            else
            {
                glColor4f(0.8f, 0.f, 0.f, m_fMiniMapAlpha);
            }
            if (o->Kind == KIND_NPC)
            {
                glColor4f(1.f, 0.f, 1.f, m_fMiniMapAlpha);
            }
            else if (o->Kind == KIND_MONSTER && o->Type == MODEL_LIFE_STONE)
            {
                glColor4f(1.f, 0.f, 1.f, m_fMiniMapAlpha);
            }

            fPosX = ((c->PositionX)) / m_iMiniMapScale - m_MiniMapScaleOffset.x + m_MiniMapPos.x;
            fPosY =
                (256 - (c->PositionY)) / m_iMiniMapScale - m_MiniMapScaleOffset.y + m_MiniMapPos.y;
            RenderColor(fPosX, fPosY, 3, 3);
        }
    }
}

// OnUpdateMouseEvent

// OnUpdateKeyEvent

// OnBtnProcess

// OnLoadImages

// OnUnloadImages

void SEASON3B::CNewUIBattleSoccerScore::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool SEASON3B::CNewUIBattleSoccerScore::Render()
{
    EnableAlphaTest();

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    RenderBackImage();
    RenderContents();

    DisableAlphaBlend();

    return true;
}

void SEASON3B::CNewUIBattleSoccerScore::RenderBackImage()
{
    RenderImage(IMAGE_BSS_BACK, m_Pos.x, m_Pos.y, float(BSS_WIDTH), float(BSS_HEIGHT));
}

void SEASON3B::CNewUIBattleSoccerScore::RenderContents()
{
    wchar_t szTemp[128];
    int nX = m_Pos.x + 30;
    int nY = m_Pos.y + 33;

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetBgColor(0, 0, 0, 128);

    if (EnableGuildWar && Hero->GuildMarkIndex != -1)
    {
        if (HeroSoccerTeam == 0)
            g_RenderText.SetTextColor(255, 60, 0, 255);
        else
            g_RenderText.SetTextColor(0, 150, 255, 255);

        mu_swprintf(szTemp, L"%d", GuildWarScore[0]);
        g_RenderText.RenderText(nX, nY, szTemp); // ����
        if (CreateGuildMark(Hero->GuildMarkIndex))
            RenderBitmap(BITMAP_GUILD, float(nX + 21), float(nY), 8, 8);                 // ��� ��ũ
        g_RenderText.RenderText(nX + 33, nY, GuildMark[Hero->GuildMarkIndex].GuildName); // ����

        if (HeroSoccerTeam == 0)
            g_RenderText.SetTextColor(0, 150, 255, 255);
        else
            g_RenderText.SetTextColor(255, 60, 0, 255);

        mu_swprintf(szTemp, L"%d", GuildWarScore[1]);
        g_RenderText.RenderText(nX, nY + 22, szTemp); // ����
        if (CreateGuildMark(FindGuildMark(GuildWarName)))
            RenderBitmap(BITMAP_GUILD, float(nX + 21), float(nY + 22), 8, 8); // ��� ��ũ
        g_RenderText.RenderText(nX + 33, nY + 22, GuildWarName);              // ����
    }
    else if (SoccerObserver)
    {
        g_RenderText.SetTextColor(255, 60, 0, 255);
        mu_swprintf(szTemp, L"%d", GuildWarScore[0]);
        g_RenderText.RenderText(nX, nY, szTemp);
        if (CreateGuildMark(FindGuildMark(SoccerTeamName[0])))
            RenderBitmap(BITMAP_GUILD, float(nX + 21), float(nY), 8, 8);
        g_RenderText.RenderText(nX + 33, nY, SoccerTeamName[0]);

        g_RenderText.SetTextColor(0, 150, 255, 255);
        mu_swprintf(szTemp, L"%d", GuildWarScore[1]);
        g_RenderText.RenderText(nX, nY + 22, szTemp);
        if (CreateGuildMark(FindGuildMark(SoccerTeamName[1])))
            RenderBitmap(BITMAP_GUILD, float(nX + 21), float(nY + 22), 8, 8);
        g_RenderText.RenderText(nX + 33, nY + 22, SoccerTeamName[1]);
    }
}

void SEASON3B::CNewUIBattleSoccerScore::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_Figure_ground.tga", IMAGE_BSS_BACK, LegacyTextureFilter::Linear);
}

void SEASON3B::CNewUIBattleSoccerScore::UnloadImages()
{
    DeleteBitmap(IMAGE_BSS_BACK);
}

void CNewUIEnterBloodCastle::SetPos(int, int)
{
}

void CNewUIEnterBloodCastle::StageGrades(int levelGroup)
{
    constexpr int TextCapacity = 255;
    wchar_t text[TextCapacity];
    for (int i = 0; i < MAX_ENTER_GRADE; ++i)
    {
        if (i == MAX_ENTER_GRADE - 1)
            mu_swprintf(text, I18N::Game::CastleNoDMasterLevel, MAX_ENTER_GRADE);
        else
            mu_swprintf(text, I18N::Game::CastleDLevelDD, i + 1,
                        m_iBloodCastleLimitLevel[levelGroup * MAX_ENTER_GRADE + i][0],
                        m_iBloodCastleLimitLevel[levelGroup * MAX_ENTER_GRADE + i][1]);
        const auto id = "btnSelect" + std::to_string(i);
        panel_.SetText((id + "-label").c_str(), text);
        panel_.SetButtonEnabled(id.c_str(), i == m_iNumActiveBtn);
    }
}

bool CNewUIEnterBloodCastle::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}

bool CNewUIEnterBloodCastle::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_);
}

void CNewUIBloodCastle::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool CNewUIBloodCastle::Render()
{
    if (g_csMatchInfo == NULL)
    {
        Show(false);
        return true;
    }

    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    wchar_t szText[256] = {};

    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.SetBgColor(0);
    g_RenderText.SetTextColor(255, 150, 0, 255);

    RenderImage(IMAGE_BLOODCASTLE_TIME_WINDOW, m_Pos.x, m_Pos.y,
                float(BLOODCASTLE_TIME_WINDOW_WIDTH), float(BLOODCASTLE_TIME_WINDOW_HEIGHT));

    if (m_iMaxKillMonster != MAX_KILL_MONSTER)
    {
        if (g_csMatchInfo->GetMatchType() == 5)
        {
            mu_swprintf(szText, I18N::Game::MagicSkeletonDD, m_iKilledMonster, m_iMaxKillMonster);
        }
        else
        {
            mu_swprintf(szText, I18N::Game::MonsterDD, m_iKilledMonster, m_iMaxKillMonster);
        }
        g_RenderText.RenderText(m_Pos.x, m_Pos.y + 13, szText, BLOODCASTLE_TIME_WINDOW_WIDTH, 0,
                                RT3_SORT_CENTER);
    }

    g_RenderText.RenderText(m_Pos.x, m_Pos.y + 38, I18N::Game::TimeLeft,
                            BLOODCASTLE_TIME_WINDOW_WIDTH, 0, RT3_SORT_CENTER);

    if (m_iTimeState == BC_TIME_STATE_IMMINENCE)
        g_RenderText.SetTextColor(255, 32, 32, 255);

    g_RenderText.SetFont(LegacyFontRole::Large);
    g_RenderText.RenderText(m_Pos.x, m_Pos.y + 50, m_szTime, BLOODCASTLE_TIME_WINDOW_WIDTH, 0,
                            RT3_SORT_CENTER);

    DisableAlphaBlend();

    return true;
}

void CNewUIBloodCastle::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_Figure_blood.tga", IMAGE_BLOODCASTLE_TIME_WINDOW,
                LegacyTextureFilter::Linear);
}

void CNewUIBloodCastle::UnloadImages()
{
    DeleteBitmap(IMAGE_BLOODCASTLE_TIME_WINDOW);
}

// Construction/Destruction

void SEASON3B::CNewUICatapultWindow::CCatapultGroupButton::Render()
{
    int iCount = 0;

    if (m_iType == CATAPULT_ATTACK)
    {
        iCount = 4;
    }
    else if (m_iType == CATAPULT_DEFENSE)
    {
        iCount = 3;
    }

    for (int i = 0; i < iCount; ++i)
    {
        m_Button[i].Render();
    }
}

void SEASON3B::CNewUICatapultWindow::SetButtonInfo()
{
    m_BtnExit.ChangeButtonImgState(true, IMAGE_CATAPULT_BTN_EXIT, false);
    m_BtnExit.ChangeButtonInfo(m_Pos.x + 13, m_Pos.y + 392, 36, 29);
    m_BtnExit.ChangeToolTipText(&I18N::Game::Close388, true);
    m_BtnFire.ChangeText(&I18N::Game::Shoot);
    m_BtnFire.ChangeTextBackColor(RGBA(255, 255, 255, 0));
    m_BtnFire.ChangeButtonImgState(true, IMAGE_CATAPULT_BTN_FIRE, true);
    m_BtnFire.ChangeButtonInfo(m_Pos.x + 41, m_Pos.y + 250, 108, 29);
    m_BtnFire.ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
    m_BtnFire.ChangeImgColor(BUTTON_STATE_DOWN, RGBA(255, 255, 255, 255));
}

void SEASON3B::CNewUICatapultWindow::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;

    m_BtnExit.ChangeButtonInfo(m_Pos.x + 13, m_Pos.y + 392, 36, 29);
    m_BtnFire.ChangeButtonInfo(m_Pos.x + 41, m_Pos.y + 250, 108, 29);
}

bool SEASON3B::CNewUICatapultWindow::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    RenderFrame();
    RenderOutlineUpper(m_Pos.x + 0, m_Pos.y + 120, 162, 100);
    RenderOutlineLower(m_Pos.x + 0, m_Pos.y + 120, 162, 100);
    RenderTexts();
    RenderButtons();
    DisableAlphaBlend();
    return true;
}

void SEASON3B::CNewUICatapultWindow::RenderFrame()
{
    RenderImage(IMAGE_CATAPULT_BACK, m_Pos.x, m_Pos.y, 190.f, 429.f);
    RenderImage(IMAGE_CATAPULT_TOP, m_Pos.x, m_Pos.y, 190.f, 64.f);
    RenderImage(IMAGE_CATAPULT_LEFT, m_Pos.x, m_Pos.y + 64, 21.f, 320.f);
    RenderImage(IMAGE_CATAPULT_RIGHT, m_Pos.x + 190 - 21, m_Pos.y + 64, 21.f, 320.f);
    RenderImage(IMAGE_CATAPULT_BOTTOM, m_Pos.x, m_Pos.y + 429 - 45, 190.f, 45.f);
}

void SEASON3B::CNewUICatapultWindow::RenderTexts()
{
    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetTextColor(220, 220, 220, 255);
    g_RenderText.SetBgColor(0);
    if (m_iType == CATAPULT_ATTACK)
    {
        g_RenderText.RenderText(m_Pos.x, m_Pos.y + 13.f, I18N::Game::WeaponForInvadingTeam, 190, 0,
                                RT3_SORT_CENTER);
    }
    else if (m_iType == CATAPULT_DEFENSE)
    {
        g_RenderText.RenderText(m_Pos.x, m_Pos.y + 13.f, I18N::Game::WeaponForDefendingTeam, 190, 0,
                                RT3_SORT_CENTER);
    }

    float fLine = 50.f;
    g_RenderText.RenderText(m_Pos.x, m_Pos.y + fLine, I18N::Game::DesiredAttackingLocation, 190, 0,
                            RT3_SORT_CENTER);
    fLine += 15.f;
    g_RenderText.RenderText(m_Pos.x, m_Pos.y + fLine, I18N::Game::SelectTheButtonAndPress, 190, 0,
                            RT3_SORT_CENTER);
    fLine += 15.f;
    g_RenderText.RenderText(m_Pos.x, m_Pos.y + fLine, I18N::Game::ToShoot, 190, 0, RT3_SORT_CENTER);
}

void SEASON3B::CNewUICatapultWindow::RenderButtons()
{
    m_BtnExit.Render();
    m_BtnFire.Render();

    m_BtnChoiceArea.Render();
}

void SEASON3B::CNewUICatapultWindow::RenderOutlineUpper(float fPos_x, float fPos_y, float fWidth,
                                                        float fHeight)
{
    POINT ptOrigin = {(long)fPos_x, (long)fPos_y};
    float fBoxWidth = fWidth;

    RenderImage(IMAGE_CATAPULT_TABLE_TOP_LEFT, ptOrigin.x + 12, ptOrigin.y - 4, 14, 14);
    RenderImage(IMAGE_CATAPULT_TABLE_TOP_RIGHT, ptOrigin.x + fBoxWidth + 4, ptOrigin.y - 4, 14, 14);
    RenderImage(IMAGE_CATAPULT_TABLE_TOP_PIXEL, ptOrigin.x + 25, ptOrigin.y - 4, fBoxWidth - 21,
                14);
}

void SEASON3B::CNewUICatapultWindow::RenderOutlineLower(float fPos_x, float fPos_y, float fWidth,
                                                        float fHeight)
{
    POINT ptOrigin = {(long)fPos_x, (long)fPos_y};
    float fBoxWidth = fWidth;
    float fBoxHeight = fHeight;

    RenderImage(IMAGE_CATAPULT_TABLE_LEFT_PIXEL, ptOrigin.x + 12, ptOrigin.y + 9, 14, fBoxHeight);
    RenderImage(IMAGE_CATAPULT_TABLE_RIGHT_PIXEL, ptOrigin.x + fBoxWidth + 4, ptOrigin.y + 9, 14,
                fBoxHeight);
    RenderImage(IMAGE_CATAPULT_TABLE_BOTTOM_LEFT, ptOrigin.x + 12, ptOrigin.y + fBoxHeight + 3, 14,
                14);
    RenderImage(IMAGE_CATAPULT_TABLE_BOTTOM_RIGHT, ptOrigin.x + fBoxWidth + 4,
                ptOrigin.y + fBoxHeight + 3, 14, 14);
    RenderImage(IMAGE_CATAPULT_TABLE_BOTTOM_PIXEL, ptOrigin.x + 25, ptOrigin.y + fBoxHeight + 3,
                fBoxWidth - 21, 14);
}

void SEASON3B::CNewUICatapultWindow::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_CATAPULT_BACK,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back01.tga", IMAGE_CATAPULT_TOP,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-L.tga", IMAGE_CATAPULT_LEFT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-R.tga", IMAGE_CATAPULT_RIGHT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back03.tga", IMAGE_CATAPULT_BOTTOM,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_exit_00.tga", IMAGE_CATAPULT_BTN_EXIT,
                LegacyTextureFilter::Linear);

    LoadBitmapW(L"Interface\\newui_btn_empty.tga", IMAGE_CATAPULT_BTN_FIRE,
                LegacyTextureFilter::Linear);

    LoadBitmapW(L"Interface\\newui_Btn_gate.tga", IMAGE_CATAPULT_BTN_SMALL,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_Btn_round.tga", IMAGE_CATAPULT_BTN_BIG,
                LegacyTextureFilter::Linear);

    LoadBitmapW(L"Interface\\newui_item_table01(L).tga", IMAGE_CATAPULT_TABLE_TOP_LEFT);
    LoadBitmapW(L"Interface\\newui_item_table01(R).tga", IMAGE_CATAPULT_TABLE_TOP_RIGHT);
    LoadBitmapW(L"Interface\\newui_item_table02(L).tga", IMAGE_CATAPULT_TABLE_BOTTOM_LEFT);
    LoadBitmapW(L"Interface\\newui_item_table02(R).tga", IMAGE_CATAPULT_TABLE_BOTTOM_RIGHT);
    LoadBitmapW(L"Interface\\newui_item_table03(Up).tga", IMAGE_CATAPULT_TABLE_TOP_PIXEL);
    LoadBitmapW(L"Interface\\newui_item_table03(Dw).tga", IMAGE_CATAPULT_TABLE_BOTTOM_PIXEL);
    LoadBitmapW(L"Interface\\newui_item_table03(L).tga", IMAGE_CATAPULT_TABLE_LEFT_PIXEL);
    LoadBitmapW(L"Interface\\newui_item_table03(R).tga", IMAGE_CATAPULT_TABLE_RIGHT_PIXEL);
}

void SEASON3B::CNewUICatapultWindow::UnloadImages()
{
    DeleteBitmap(IMAGE_CATAPULT_BTN_BIG);
    DeleteBitmap(IMAGE_CATAPULT_BTN_SMALL);
    DeleteBitmap(IMAGE_CATAPULT_BTN_FIRE);

    DeleteBitmap(IMAGE_CATAPULT_TABLE_RIGHT_PIXEL);
    DeleteBitmap(IMAGE_CATAPULT_TABLE_LEFT_PIXEL);
    DeleteBitmap(IMAGE_CATAPULT_TABLE_BOTTOM_PIXEL);
    DeleteBitmap(IMAGE_CATAPULT_TABLE_TOP_PIXEL);
    DeleteBitmap(IMAGE_CATAPULT_TABLE_BOTTOM_RIGHT);
    DeleteBitmap(IMAGE_CATAPULT_TABLE_BOTTOM_LEFT);
    DeleteBitmap(IMAGE_CATAPULT_TABLE_TOP_RIGHT);
    DeleteBitmap(IMAGE_CATAPULT_TABLE_TOP_LEFT);

    DeleteBitmap(IMAGE_CATAPULT_BTN_EXIT);
    DeleteBitmap(IMAGE_CATAPULT_BOTTOM);
    DeleteBitmap(IMAGE_CATAPULT_RIGHT);
    DeleteBitmap(IMAGE_CATAPULT_LEFT);
    DeleteBitmap(IMAGE_CATAPULT_TOP);
    DeleteBitmap(IMAGE_CATAPULT_BACK);
}

void CNewUIChaosCastleTime::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool CNewUIChaosCastleTime::Render()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    wchar_t szText[256] = {};

    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.SetBgColor(0);
    g_RenderText.SetTextColor(255, 150, 0, 255);

    RenderImage(IMAGE_CHAOSCASTLE_TIME_WINDOW, m_Pos.x, m_Pos.y,
                float(CHAOSCASTLE_TIME_WINDOW_WIDTH), float(CHAOSCASTLE_TIME_WINDOW_HEIGHT));

    if (m_iMaxKillMonster != MAX_KILL_MONSTER)
    {
        mu_swprintf(szText, I18N::Game::CharacterDD, m_iKilledMonster, m_iMaxKillMonster);
        g_RenderText.RenderText(m_Pos.x, m_Pos.y + 13, szText, CHAOSCASTLE_TIME_WINDOW_WIDTH, 0,
                                RT3_SORT_CENTER);
    }

    g_RenderText.RenderText(m_Pos.x, m_Pos.y + 38, I18N::Game::TimeLeft,
                            CHAOSCASTLE_TIME_WINDOW_WIDTH, 0, RT3_SORT_CENTER);

    if (m_iTimeState == CC_TIME_STATE_IMMINENCE)
        g_RenderText.SetTextColor(255, 32, 32, 255);

    g_RenderText.SetFont(LegacyFontRole::Large);
    g_RenderText.RenderText(m_Pos.x, m_Pos.y + 50, m_szTime, CHAOSCASTLE_TIME_WINDOW_WIDTH, 0,
                            RT3_SORT_CENTER);

    DisableAlphaBlend();

    return true;
}

void CNewUIChaosCastleTime::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_Figure_blood.tga", IMAGE_CHAOSCASTLE_TIME_WINDOW,
                LegacyTextureFilter::Linear);
}

void CNewUIChaosCastleTime::UnloadImages()
{
    DeleteBitmap(IMAGE_CHAOSCASTLE_TIME_WINDOW);
}

void SEASON3B::CNewUICryWolf::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool SEASON3B::CNewUICryWolf::Render()
{
    if (crywolf_.IsCyrWolf1st() == false)
        return true;

    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    wchar_t Text[300];

    float Main[] = {518.f, 278.f, 122.f, 119.f, 120.f / 128.f, 118.f / 128.f};
    float Number[5][6] = {{565.f, 280.f, 13.f, 13.f, 12.f / 16.f, 12.f / 16.f},
                          {582.f, 282.f, 13.f, 13.f, 12.f / 16.f, 12.f / 16.f},
                          {598.f, 286.f, 13.f, 13.f, 12.f / 16.f, 12.f / 16.f},
                          {613.f, 294.f, 13.f, 13.f, 12.f / 16.f, 12.f / 16.f},
                          {625.f, 306.f, 13.f, 13.f, 12.f / 16.f, 12.f / 16.f}};
    float Dark_Elf_Icon[] = {623.f, 358.f, 15.f, 15.f, 14.f / 16.f, 14.f / 16.f};
    float Val_Icon[] = {623.f, 379.f, 15.f, 15.f, 14.f / 16.f, 14.f / 16.f};

    if (crywolf_.Suc_Or_Fail >= 0)
    {
        float A_Value = 0.f;
        int aa = (crywolf_.Delay * 2) % 140;

        if (aa > 70)
            A_Value = 1.f - ((aa - 70) * 0.01f);
        else
            A_Value = 0.3f + (aa * 0.01f);

        if ((crywolf_.Delay * 15) > 479)
        {
            Render(150, 50, 329, 94, 0.f, 0.f, 328.f / 512.f, 93.f / 128.f, crywolf_.Add_Num, false,
                   false, A_Value);
        }
        else if (crywolf_.Suc_Or_Fail == 0)
        {
            Render(150 + (crywolf_.Delay * 15), 50, 329, 94, 0.f, 0.f, 328.f / 512.f, 93.f / 128.f,
                   crywolf_.Add_Num, false, false, A_Value);
        }
        else
        {
            //Delay_Add_inter
            Render(-329 + (crywolf_.Delay * 15), 50, 329, 94, 0.f, 0.f, 328.f / 512.f, 93.f / 128.f,
                   crywolf_.Add_Num, false, false, A_Value);
        }

        Render(230, 150, 196, 141, 0.f, 0.f, 195.f / 256.f, 140.f / 256.f, 26);
        Render(250 + crywolf_.Delay_Add_inter, 188, 110, 28, 0.f, 0.f, 110.f / 128.f, 27.f / 32.f,
               27);

        if (crywolf_.Delay_Add_inter == 0)
        {
            int Exp_val[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0}, Exp_Dummy = 0, Val = 0;

            Render(250 + 120, 188, 29, 28, 0.f, 0.f, 29.f / 32.f, 27.f / 32.f, 28 + crywolf_.Rank);

            for (int i = 0; i < 9; i++)
            {
                if (crywolf_.Exp >= Val)
                {
                    if (Val > 0)
                    {
                        Exp_Dummy = crywolf_.Exp / Val;
                        Exp_val[8 - i] = Exp_Dummy % 10;
                    }
                    else
                    {
                        Exp_val[8 - i] = crywolf_.Exp % 10;
                        Val = 1;
                    }
                }
                else
                    break;

                Val *= 10;
            }
            int Move_X = 29;

            g_pCryWolfInterface->Render(200 + Move_X, 235, 60, 19, 0.f, 0.f, 60.f / 64.f,
                                        19.f / 32.f, 43);
            g_pCryWolfInterface->Render(250 + 130 + Move_X, 235, 15, 19, 0.f, 0.f, 15.f / 16.f,
                                        19.f / 32.f, 33 + Exp_val[8]);
            g_pCryWolfInterface->Render(250 + 115 + Move_X, 235, 15, 19, 0.f, 0.f, 15.f / 16.f,
                                        19.f / 32.f, 33 + Exp_val[7]);
            g_pCryWolfInterface->Render(250 + 100 + Move_X, 235, 15, 19, 0.f, 0.f, 15.f / 16.f,
                                        19.f / 32.f, 33 + Exp_val[6]);
            g_pCryWolfInterface->Render(250 + 85 + Move_X, 235, 15, 19, 0.f, 0.f, 15.f / 16.f,
                                        19.f / 32.f, 33 + Exp_val[5]);
            g_pCryWolfInterface->Render(250 + 70 + Move_X, 235, 15, 19, 0.f, 0.f, 15.f / 16.f,
                                        19.f / 32.f, 33 + Exp_val[4]);
            g_pCryWolfInterface->Render(250 + 55 + Move_X, 235, 15, 19, 0.f, 0.f, 15.f / 16.f,
                                        19.f / 32.f, 33 + Exp_val[3]);
            g_pCryWolfInterface->Render(250 + 40 + Move_X, 235, 15, 19, 0.f, 0.f, 15.f / 16.f,
                                        19.f / 32.f, 33 + Exp_val[2]);
            g_pCryWolfInterface->Render(250 + 25 + Move_X, 235, 15, 19, 0.f, 0.f, 15.f / 16.f,
                                        19.f / 32.f, 33 + Exp_val[1]);
            g_pCryWolfInterface->Render(250 + 10 + Move_X, 235, 15, 19, 0.f, 0.f, 15.f / 16.f,
                                        19.f / 32.f, 33 + Exp_val[0]);
        }
    }

    if (crywolf_.Get_State_Only_Elf() == false || crywolf_.IsCyrWolf1st() == false)
        return true;

    g_pCryWolfInterface->Render(Main[0], Main[1], Main[2], Main[3], 0.f, 0.f, Main[4], Main[5], 3);

    //23,24;

    for (int ia = 0; ia < 5; ia++)
    {
        BYTE Use = (crywolf_.m_AltarState[ia] & 0xf0) >> 4;
        BYTE State = (crywolf_.m_AltarState[ia] & 0x0f);
        if (Use == CRYWOLF_ALTAR_STATE_CONTRACTED)
        {
            if (State == 1)
            {
                g_pCryWolfInterface->Render(Number[ia][0], Number[ia][1], Number[ia][2],
                                            Number[ia][3], 0.f, 0.f, Number[ia][4], Number[ia][5],
                                            23);
            }
            else if (State == 2)
            {
                g_pCryWolfInterface->Render(Number[ia][0], Number[ia][1], Number[ia][2],
                                            Number[ia][3], 0.f, 0.f, Number[ia][4], Number[ia][5],
                                            24);
            }
            else
            {
                g_pCryWolfInterface->Render(Number[ia][0], Number[ia][1], Number[ia][2],
                                            Number[ia][3], 0.f, 0.f, Number[ia][4], Number[ia][5],
                                            25);
            }
        }
        else
        {
            if (State == 1)
            {
                g_pCryWolfInterface->Render(Number[ia][0], Number[ia][1], Number[ia][2],
                                            Number[ia][3], 0.f, 0.f, Number[ia][4], Number[ia][5],
                                            7);
            }
            else if (State == 2)
            {
                g_pCryWolfInterface->Render(Number[ia][0], Number[ia][1], Number[ia][2],
                                            Number[ia][3], 0.f, 0.f, Number[ia][4], Number[ia][5],
                                            8);
            }
        }
    }

    if (crywolf_.Dark_elf_Num == 0)
    {
        g_pCryWolfInterface->Render(Dark_Elf_Icon[0], Dark_Elf_Icon[1], Dark_Elf_Icon[2],
                                    Dark_Elf_Icon[3], 0.f, 0.f, Dark_Elf_Icon[4], Dark_Elf_Icon[5],
                                    6);
    }
    else
    {
        g_pCryWolfInterface->Render(Dark_Elf_Icon[0], Dark_Elf_Icon[1], Dark_Elf_Icon[2],
                                    Dark_Elf_Icon[3], 0.f, 0.f, Dark_Elf_Icon[4], Dark_Elf_Icon[5],
                                    5);
    }

    g_pCryWolfInterface->Render(538, 392, 104, 37, 0.f, 0.f, 104.f / 128.f, 36.f / 64.f, 12);

    glColor3f(1.f, 0.6f, 0.3f);
    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.SetTextColor(255, 148, 21, 255);
    g_RenderText.SetBgColor(0);
    mu_swprintf(Text, I18N::Game::DarkElfD12, crywolf_.Dark_elf_Num);
    g_RenderText.RenderText(582, 359, Text, 0, 0, RT3_WRITE_CENTER);

    if (crywolf_.View_Bal == true)
    {
        if (crywolf_.Deco_Insert >= 21.f)
        {
            g_pCryWolfInterface->Render(Val_Icon[0], Val_Icon[1], Val_Icon[2], Val_Icon[3], 0.f,
                                        0.f, Val_Icon[4], Val_Icon[5], 4);

            mu_swprintf(Text, I18N::Game::Balgass);
            g_RenderText.RenderText(600, 380, Text, 0, 0, RT3_WRITE_CENTER);

            float Hp = ((67.f / 100.f) * (float)crywolf_.Val_Hp);
            float nx = ((68.f / 100.f) * (float)crywolf_.Val_Hp);

            g_pCryWolfInterface->Render(548, 388, nx, 8, 0.f, 0.f, Hp / 128.f, 8.f / 8.f, 1);
        }
    }

    if (m_bTimeStart == true && crywolf_.m_CrywolfState == CRYWOLF_STATE_START)
    {

        if (crywolf_.View_Bal == false)
        {
            glColor3f(1.f, 1.0f, 1.0f);
        }
        else
        {
            glColor3f(1.f, 0.3f, 0.3f);
        }

        if (m_iMinute < 10)
        {
            RenderNumber2D(510 + 60, 384 + 18, 0, 14, 14);
        }
        RenderNumber2D(510 + 70, 384 + 18, m_iMinute, 14, 14);
        if (m_iSecond / 1000 < 10)
        {
            RenderNumber2D(520 + 77, 384 + 18, 0, 14, 14);
        }
        RenderNumber2D(520 + 87, 384 + 18, m_iSecond / 1000, 14, 14);
    }
    else
    {
        RenderNumber2D(510 + 60, 384 + 18, 0, 14, 14);
        RenderNumber2D(510 + 70, 384 + 18, 0, 14, 14);
        RenderNumber2D(520 + 77, 384 + 18, 0, 14, 14);
        RenderNumber2D(520 + 87, 384 + 18, 0, 14, 14);
    }

    int HpS = 100 - crywolf_.m_StatueHP;
    float Hp = ((88.f / 100.f) * (float)HpS);
    float nx = ((89.f / 100.f) * (float)HpS);
    RenderImage(IMAGE_MVP_INTERFACE + 9, 548 + nx, 323, 89.f - nx, 30, Hp / 128.f, 0.f,
                ((((88.f / 100.f) * (float)(100.f - HpS))) / 128.f), 29.f / 32.f);

    crywolf_.RenderNoticesCryWolf();

    if (crywolf_.Get_State_Only_Elf() == false || crywolf_.IsCyrWolf1st() == false)
        return true;

    int Yes = 250;
    int No = 330;

    g_RenderText.SetTextColor(255, 148, 21, 255);
    g_RenderText.SetBgColor(0x00000000);

    if (crywolf_.Message_Box == 1)
    {
        g_pCryWolfInterface->Render(212, 206, 209, 80, 0.f, 0.f, 206.f / 256.f, 77.f / 128.f, 22);
        if (MouseX > No && MouseX < No + 54 && MouseY > 250 && MouseY < 250 + 30)
        {
            if (crywolf_.Button_Down == 1)
            {
                g_pCryWolfInterface->Render(No, 250, 54, 30, 0.f, 0.f, 53.f / 64.f, 30.f / 32.f,
                                            15);
            }
            else
            {
                g_pCryWolfInterface->Render(No, 250, 54, 30, 0.f, 0.f, 53.f / 64.f, 30.f / 32.f,
                                            14);
            }
        }
        else
        {
            g_pCryWolfInterface->Render(No, 250, 54, 30, 0.f, 0.f, 53.f / 64.f, 30.f / 32.f, 13);
        }

        if (MouseX > Yes && MouseX < Yes + 54 && MouseY > 250 && MouseY < 250 + 30)
        {
            if (crywolf_.Button_Down == 2)
            {
                g_pCryWolfInterface->Render(Yes, 250, 54, 30, 0.f, 0.f, 53.f / 64.f, 30.f / 32.f,
                                            21);
            }
            else
            {
                g_pCryWolfInterface->Render(Yes, 250, 54, 30, 0.f, 0.f, 53.f / 64.f, 30.f / 32.f,
                                            20);
            }
        }
        else
        {
            g_pCryWolfInterface->Render(Yes, 250, 54, 30, 0.f, 0.f, 53.f / 64.f, 30.f / 32.f, 19);
        }

        if (crywolf_.Box_String[0][0] != 0)
        {
            int Y_loc = 239;
            if (crywolf_.Box_String[1][0] != 0)
                Y_loc = 227;
            g_RenderText.RenderText(317, Y_loc, crywolf_.Box_String[0], 0, 0, RT3_WRITE_CENTER);
        }
        if (crywolf_.Box_String[1][0] != 0)
        {
            g_RenderText.RenderText(317, 238, crywolf_.Box_String[1], 0, 0, RT3_WRITE_CENTER);
        }
    }
    else if (crywolf_.Message_Box == 2)
    {
        g_pCryWolfInterface->Render(212, 206, 209, 80, 0.f, 0.f, 206.f / 256.f, 77.f / 128.f, 22);

        if (MouseX > 290 && MouseX < 290 + 54 && MouseY > 250 && MouseY < 250 + 30)
        {
            if (crywolf_.Button_Down == 3)
            {
                g_pCryWolfInterface->Render(290, 250, 54, 30, 0.f, 0.f, 53.f / 64.f, 30.f / 32.f,
                                            18);
            }
            else
            {
                g_pCryWolfInterface->Render(290, 250, 54, 30, 0.f, 0.f, 53.f / 64.f, 30.f / 32.f,
                                            17);
            }
        }
        else
        {
            g_pCryWolfInterface->Render(290, 250, 54, 30, 0.f, 0.f, 53.f / 64.f, 30.f / 32.f, 16);
        }

        if (crywolf_.Box_String[0][0] != 0)
        {
            int Y_loc = 239;
            if (crywolf_.Box_String[1][0] != 0)
                Y_loc = 227;
            g_RenderText.RenderText(317, Y_loc, crywolf_.Box_String[0], 0, 0, RT3_WRITE_CENTER);
        }

        if (crywolf_.Box_String[1][0] != 0)
        {
            g_RenderText.RenderText(317, 238, crywolf_.Box_String[1], 0, 0, RT3_WRITE_CENTER);
        }
    }

    DisableAlphaBlend();

    return true;
}

bool SEASON3B::CNewUICryWolf::Render(int Posx, int Posy, int nPosx, int nPosy, float u, float v,
                                     float su, float sv, int Index, bool Scale, bool StartScale,
                                     float Alpha)
{
    glColor4f(1.f, 1.f, 1.f, Alpha);

    RenderImage(IMAGE_MVP_INTERFACE + Index, Posx, Posy, nPosx, nPosy, u, v, su, sv);

    return true;
}

void SEASON3B::CNewUICryWolf::LoadImages()
{
    LoadBitmapW(L"Interface\\in_bar.tga", IMAGE_MVP_INTERFACE, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\in_bar2.jpg", IMAGE_MVP_INTERFACE + 1, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\in_deco.tga", IMAGE_MVP_INTERFACE + 2, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\in_main-New.tga", IMAGE_MVP_INTERFACE + 3,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\in_main_icon_bal1.tga", IMAGE_MVP_INTERFACE + 4,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\in_main_icon_dl1.tga", IMAGE_MVP_INTERFACE + 5,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\in_main_icon_dl2.tga", IMAGE_MVP_INTERFACE + 6,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\in_main_number1.tga", IMAGE_MVP_INTERFACE + 7,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\in_main_number2.tga", IMAGE_MVP_INTERFACE + 8,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\in_main2-New.tga", IMAGE_MVP_INTERFACE + 9,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_failure.tga", IMAGE_MVP_INTERFACE + 10,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_success.tga", IMAGE_MVP_INTERFACE + 11,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\t_main-New.tga", IMAGE_MVP_INTERFACE + 12,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\m_b_no1.tga", IMAGE_MVP_INTERFACE + 13, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\m_b_no2.tga", IMAGE_MVP_INTERFACE + 14, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\m_b_no3.tga", IMAGE_MVP_INTERFACE + 15, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\m_b_ok1.tga", IMAGE_MVP_INTERFACE + 16, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\m_b_ok2.tga", IMAGE_MVP_INTERFACE + 17, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\m_b_ok3.tga", IMAGE_MVP_INTERFACE + 18, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\m_b_yes1.tga", IMAGE_MVP_INTERFACE + 19, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\m_b_yes2.tga", IMAGE_MVP_INTERFACE + 20, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\m_b_yes3.tga", IMAGE_MVP_INTERFACE + 21, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\m_main.tga", IMAGE_MVP_INTERFACE + 22, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\in_main_number1_1.tga", IMAGE_MVP_INTERFACE + 23,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\in_main_number2_1.tga", IMAGE_MVP_INTERFACE + 24,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\in_main_number0_2.tga", IMAGE_MVP_INTERFACE + 25,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_table.tga", IMAGE_MVP_INTERFACE + 26,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_rank.tga", IMAGE_MVP_INTERFACE + 27,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_D.tga", IMAGE_MVP_INTERFACE + 28,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_C.tga", IMAGE_MVP_INTERFACE + 29,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_B.tga", IMAGE_MVP_INTERFACE + 30,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_A.tga", IMAGE_MVP_INTERFACE + 31,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_S.tga", IMAGE_MVP_INTERFACE + 32,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_0.tga", IMAGE_MVP_INTERFACE + 33,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_1.tga", IMAGE_MVP_INTERFACE + 34,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_2.tga", IMAGE_MVP_INTERFACE + 35,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_3.tga", IMAGE_MVP_INTERFACE + 36,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_4.tga", IMAGE_MVP_INTERFACE + 37,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_5.tga", IMAGE_MVP_INTERFACE + 38,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_6.tga", IMAGE_MVP_INTERFACE + 39,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_7.tga", IMAGE_MVP_INTERFACE + 40,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_8.tga", IMAGE_MVP_INTERFACE + 41,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_9.tga", IMAGE_MVP_INTERFACE + 42,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\icon_Rank_exp.tga", IMAGE_MVP_INTERFACE + 43,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\m_main_rank.tga", IMAGE_MVP_INTERFACE + 44,
                LegacyTextureFilter::Linear);
}

void SEASON3B::CNewUICryWolf::UnloadImages()
{
    DeleteBitmap(IMAGE_MVP_INTERFACE);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 1);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 2);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 3);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 4);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 5);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 6);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 7);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 8);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 9);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 10);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 11);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 12);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 13);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 14);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 15);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 16);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 17);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 18);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 19);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 20);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 21);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 22);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 23);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 24);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 25);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 26);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 27);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 28);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 29);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 30);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 31);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 32);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 33);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 34);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 35);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 36);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 37);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 38);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 39);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 40);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 41);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 42);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 43);
    DeleteBitmap(IMAGE_MVP_INTERFACE + 44);
}

void CNewUICursedTempleResult::StageTeam(int team, const CT_GameResult_list &results)
{
    auto &rows = content_.rows[team];
    auto &tooltips = experienceTips_[team];
    rows.clear();
    tooltips.clear();
    content_.heroRows[team].reset();
    for (const auto &result : results)
    {
        if (wcscmp(result.s_characterId, Hero->ID) == 0)
            content_.heroRows[team] = rows.size();
        rows.push_back({result.s_characterId,
                        gCharacterManager.GetCharacterClassText(result.s_class),
                        std::to_wstring(result.s_point)});
        tooltips.push_back(std::wstring(I18N::Game::EXP) + L": " +
                           std::to_wstring(result.s_addexp));
    }
}
void CNewUICursedTempleResult::StageContent()
{
    const char *locale = I18N::GetCurrentLocale();
    if (!contentDirty_ && locale_ == locale)
        return;
    locale_ = locale;
    content_.labels = {I18N::Game::HeroList,  I18N::Game::MUAlliance, I18N::Game::IllusionSorcery,
                       I18N::Game::Character, I18N::Game::Class,      I18N::Game::Point,
                       I18N::Game::Close388};
    content_.winState = m_WinState;
    StageTeam(0, m_AlliedTeamGameResult);
    StageTeam(1, m_IllusionTeamGameResult);
    ++content_.revision;
    contentDirty_ = false;
}

void CNewUICursedTempleResult::RenderDetails()
{
    const wchar_t *tip = nullptr;
    if (panel_.CloseHovered())
        tip = I18N::Game::YouMayBeCompensatedByClickingOnTheCloseButton;
    else if (auto hovered = panel_.HoveredRow())
        tip = experienceTips_[hovered->first][hovered->second].c_str();
    if (!tip)
        return;
    TextNum = 1;
    mu_swprintf(TextList[0], L"%ls", tip);
    TextListColor[0] = TEXT_COLOR_WHITE;
    RenderTipTextList(MouseX, MouseY, TextNum, 0);
}
bool CNewUICursedTempleResult::Render()
{
    panel_.Record(renderer_.LegacyRender());
    RenderDetails();
    return true;
}
bool CNewUICursedTempleResult::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_, content_);
}

void CNewUIDoppelGangerFrame::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool CNewUIDoppelGangerFrame::Render()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    RenderImage(IMAGE_DOPPELGANGER_FRAME_WINDOW, m_Pos.x, m_Pos.y,
                float(DOPPELGANGER_FRAME_WINDOW_WIDTH), float(DOPPELGANGER_FRAME_WINDOW_HEIGHT));

    wchar_t szText[256] = {};
    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.SetBgColor(0);

    if (m_iEnteredMonsters == 0)
    {
        g_RenderText.SetTextColor(255, 150, 0, 255);
    }
    else if (m_iEnteredMonsters == 1)
    {
        g_RenderText.SetTextColor(255, 70, 0, 255);
    }
    else if (m_iEnteredMonsters >= 2)
    {
        g_RenderText.SetTextColor(255, 0, 0, 255);
    }

    mu_swprintf(szText, I18N::Game::MonstersPassedDD, m_iEnteredMonsters, m_iMaxMonsters);
    g_RenderText.RenderText(m_Pos.x + 117, m_Pos.y + 13, szText, 110, 0, RT3_SORT_CENTER);

    g_RenderText.SetTextColor(255, 150, 0, 255);
    g_RenderText.RenderText(m_Pos.x + 117, m_Pos.y + 38, I18N::Game::TimeLeft, 110, 0,
                            RT3_SORT_CENTER);

    int iMinute = m_iTime / 60;
    int iSecond = 99 - (int)WorldTime % 100;
    if (m_bStopTimer == TRUE)
    {
        iSecond = 0;
    }

    mu_swprintf(szText, L"%.2d:%.2d:%.2d", iMinute, m_iTime % 60, iSecond);
    g_RenderText.SetFont(LegacyFontRole::Large);
    g_RenderText.RenderText(m_Pos.x + 117, m_Pos.y + 50, szText, 110, 0, RT3_SORT_CENTER);

    if (m_iEnteredMonsters == 0)
    {
        RenderImage(IMAGE_DOPPELGANGER_GUAGE_YELLOW,
                    m_Pos.x + 59 + 167.f * (1.0f - m_fMonsterGauge), m_Pos.y + 78,
                    167.f * m_fMonsterGauge, 8.f - 1, 165.f / 256.f * (1.0f - m_fMonsterGauge), 0,
                    165.f / 256.f * m_fMonsterGauge, 6.f / 8.f);
    }
    else if (m_iEnteredMonsters == 1)
    {
        RenderImage(IMAGE_DOPPELGANGER_GUAGE_YELLOW, m_Pos.x + 59, m_Pos.y + 78, 167.f, 8.f - 1, 0,
                    0, 165.f / 256.f, 6.f / 8.f);
        RenderImage(IMAGE_DOPPELGANGER_GUAGE_ORANGE,
                    m_Pos.x + 59 + 167.f * (1.0f - m_fMonsterGauge), m_Pos.y + 78,
                    167.f * m_fMonsterGauge, 8.f - 1, 165.f / 256.f * (1.0f - m_fMonsterGauge), 0,
                    165.f / 256.f * m_fMonsterGauge, 6.f / 8.f);
    }
    else if (m_iEnteredMonsters >= 2)
    {
        RenderImage(IMAGE_DOPPELGANGER_GUAGE_ORANGE, m_Pos.x + 59, m_Pos.y + 78, 167.f, 8.f - 1, 0,
                    0, 165.f / 256.f, 6.f / 8.f);
        RenderImage(IMAGE_DOPPELGANGER_GUAGE_RED, m_Pos.x + 59 + 167.f * (1.0f - m_fMonsterGauge),
                    m_Pos.y + 78, 167.f * m_fMonsterGauge, 8.f - 1,
                    165.f / 256.f * (1.0f - m_fMonsterGauge), 0, 165.f / 256.f * m_fMonsterGauge,
                    6.f / 8.f);
    }

    if (m_bIceWalkerEnabled == TRUE)
    {
        RenderImage(IMAGE_DOPPELGANGER_GUAGE_ICEWALKER,
                    m_Pos.x + 59 - 6.5f + 167 * m_fIceWalkerPosition, m_Pos.y + 78 - 1, 13.0f,
                    7.0f);
    }

    for (std::map<WORD, PARTY_POSITION>::iterator iter = m_PartyPositionMap.begin();
         iter != m_PartyPositionMap.end(); ++iter)
    {
        if (iter->second.m_fPositionRcvd == -1)
            continue;

        if (iter->first == Hero->Key)
        {
            RenderImage(IMAGE_DOPPELGANGER_GUAGE_PLAYER,
                        m_Pos.x + 59 - 4.5f + 167 * iter->second.m_fPosition, m_Pos.y + 78 + 1,
                        9.0f, 8.0f);
        }
        else
        {
            RenderImage(IMAGE_DOPPELGANGER_GUAGE_PARTY_MEMBER,
                        m_Pos.x + 59 - 4.5f + 167 * iter->second.m_fPosition, m_Pos.y + 78 + 1,
                        9.0f, 8.0f);
        }
    }

    DisableAlphaBlend();

    return true;
}

void CNewUIDoppelGangerFrame::LoadImages()
{
    LoadBitmapW(L"Interface\\Double_back.tga", IMAGE_DOPPELGANGER_FRAME_WINDOW,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\Double_bar(R).jpg", IMAGE_DOPPELGANGER_GUAGE_RED,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\Double_bar(O).jpg", IMAGE_DOPPELGANGER_GUAGE_ORANGE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\Double_bar(Y).jpg", IMAGE_DOPPELGANGER_GUAGE_YELLOW,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\Double_Baricon04.tga", IMAGE_DOPPELGANGER_GUAGE_PLAYER,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\Double_Baricon01.tga", IMAGE_DOPPELGANGER_GUAGE_PARTY_MEMBER,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\Double_Micon01.tga", IMAGE_DOPPELGANGER_GUAGE_ICEWALKER,
                LegacyTextureFilter::Linear);
}

void CNewUIDoppelGangerFrame::UnloadImages()
{
    DeleteBitmap(IMAGE_DOPPELGANGER_FRAME_WINDOW);
}

void CNewUIDoppelGangerWindow::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool CNewUIDoppelGangerWindow::Render()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    RenderFrame();

    POINT ptOrigin = {m_Pos.x, m_Pos.y + 50};
    wchar_t szText[256];

    g_RenderText.SetFont(LegacyFontRole::Normal);
    wchar_t szTextOut[2][300];
    CutStr(I18N::Game::OnlyThoseInPossessionOfAMirrorOfDimensions, szTextOut[0], 140, 2, 300);
    g_RenderText.RenderText(ptOrigin.x, ptOrigin.y, szTextOut[0], 190, 0, RT3_SORT_CENTER);
    g_RenderText.RenderText(ptOrigin.x, ptOrigin.y + 15, szTextOut[1], 190, 0, RT3_SORT_CENTER);
    CutStr(I18N::Game::MayPassThroughTheDoppelgangerGate, szTextOut[0], 100, 2, 300);
    g_RenderText.RenderText(ptOrigin.x, ptOrigin.y + 30, szTextOut[0], 190, 0, RT3_SORT_CENTER);
    g_RenderText.RenderText(ptOrigin.x, ptOrigin.y + 45, szTextOut[1], 190, 0, RT3_SORT_CENTER);
    g_RenderText.RenderText(ptOrigin.x, ptOrigin.y + 60, I18N::Game::WillYouShowMeYourMirror, 190,
                            0, RT3_SORT_CENTER);

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.RenderText(ptOrigin.x, ptOrigin.y + 120, I18N::Game::MirrorOfDimensions, 190, 0,
                            RT3_SORT_CENTER);

    if (m_bIsEnterButtonLocked == TRUE)
    {
        m_BtnEnter.Lock();
        m_BtnEnter.ChangeImgColor(BUTTON_STATE_UP, RGBA(100, 100, 100, 255));
        m_BtnEnter.ChangeImgColor(BUTTON_STATE_DOWN, RGBA(100, 100, 100, 255));
        m_BtnEnter.ChangeImgColor(BUTTON_STATE_OVER, RGBA(100, 100, 100, 255));
        m_BtnEnter.ChangeTextColor(RGBA(100, 100, 100, 255));
    }
    else
    {
        m_BtnEnter.UnLock();
        m_BtnEnter.ChangeImgColor(BUTTON_STATE_UP, RGBA(255, 255, 255, 255));
        m_BtnEnter.ChangeImgColor(BUTTON_STATE_DOWN, RGBA(255, 255, 255, 255));
        m_BtnEnter.ChangeImgColor(BUTTON_STATE_OVER, RGBA(255, 255, 255, 255));
        m_BtnEnter.ChangeTextColor(RGBA(255, 255, 255, 255));
    }
    m_BtnEnter.Render();

    RenderImage(IMAGE_DOPPELGANGERWINDOW_LINE, m_Pos.x + 1, m_Pos.y + 130 + 90, 188.f, 21.f);

    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.RenderText(ptOrigin.x, ptOrigin.y + 210, I18N::Game::EntryTime, 190, 0,
                            RT3_SORT_CENTER);
    if (m_iRemainTime == 0)
    {
        mu_swprintf(szText, I18N::Game::YouMayNowEnter);
    }
    else
    {
        mu_swprintf(szText, I18N::Game::EnterAfterDMinutes, m_iRemainTime);
    }
    g_RenderText.RenderText(ptOrigin.x, ptOrigin.y + 230, szText, 190, 0, RT3_SORT_CENTER);

    m_BtnClose.Render();

    DisableAlphaBlend();

    return true;
}

void CNewUIDoppelGangerWindow::Render3D()
{
    RenderItem3D();
}

void CNewUIDoppelGangerWindow::RenderItem3D()
{
    POINT ptOrigin = {m_Pos.x, m_Pos.y + 50};

    int nItemType = (14 * MAX_ITEM_INDEX) + 111;
    int nItemLevel = 0;

    RenderItem3D(ptOrigin.x + (190 - 20) / 2, ptOrigin.y + 75, 20.f, 27, nItemType, nItemLevel, 0,
                 0, false);
}

void CNewUIDoppelGangerWindow::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_DOPPELGANGERWINDOW_BACK,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back01.tga", IMAGE_DOPPELGANGERWINDOW_TOP,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-L.tga", IMAGE_DOPPELGANGERWINDOW_LEFT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-R.tga", IMAGE_DOPPELGANGERWINDOW_RIGHT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back03.tga", IMAGE_DOPPELGANGERWINDOW_BOTTOM,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_btn_empty_very_small.tga", IMAGE_DOPPELGANGERWINDOW_BUTTON,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_myquest_Line.tga", IMAGE_DOPPELGANGERWINDOW_LINE,
                LegacyTextureFilter::Linear);
}

void CNewUIDoppelGangerWindow::UnloadImages()
{
    DeleteBitmap(IMAGE_DOPPELGANGERWINDOW_BOTTOM);
    DeleteBitmap(IMAGE_DOPPELGANGERWINDOW_RIGHT);
    DeleteBitmap(IMAGE_DOPPELGANGERWINDOW_LEFT);
    DeleteBitmap(IMAGE_DOPPELGANGERWINDOW_TOP);
    DeleteBitmap(IMAGE_DOPPELGANGERWINDOW_BACK);
    DeleteBitmap(IMAGE_DOPPELGANGERWINDOW_BUTTON);
    DeleteBitmap(IMAGE_DOPPELGANGERWINDOW_LINE);
}

void CNewUIDoppelGangerWindow::RenderFrame()
{
    RenderImage(IMAGE_DOPPELGANGERWINDOW_BACK, m_Pos.x, m_Pos.y, 190.f, 429.f);
    RenderImage(IMAGE_DOPPELGANGERWINDOW_TOP, m_Pos.x, m_Pos.y, 190.f, 64.f);
    RenderImage(IMAGE_DOPPELGANGERWINDOW_LEFT, m_Pos.x, m_Pos.y + 64, 21.f, 320.f);
    RenderImage(IMAGE_DOPPELGANGERWINDOW_RIGHT, m_Pos.x + INVENTORY_WIDTH - 21, m_Pos.y + 64, 21.f,
                320.f);
    RenderImage(IMAGE_DOPPELGANGERWINDOW_BOTTOM, m_Pos.x, m_Pos.y + INVENTORY_HEIGHT - 45, 190.f,
                45.f);

    wchar_t szText[256] = {
        0,
    };
    float fPos_x = m_Pos.x + 15.0f, fPos_y = m_Pos.y;
    float fLine_y = 13.0f;

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetTextColor(220, 220, 220, 255);
    g_RenderText.SetBgColor(0, 0, 0, 0);

    mu_swprintf(szText, L"%ls", I18N::Game::Lugard);
    g_RenderText.RenderText(fPos_x, fPos_y + fLine_y, szText, 160.0f, 0, RT3_SORT_CENTER);
}

void CNewUIEnterDevilSquare::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
    m_EnterUITextPos.x = m_Pos.x + 3;
    m_EnterUITextPos.y = m_Pos.y + 45;

    SetBtnPos(m_Pos.x + 6, m_Pos.y + 155);

    m_BtnExit.ChangeButtonInfo(m_Pos.x + 13, m_Pos.y + 392, 36, 29);

    for (int i = 0; i < MAX_ENTER_GRADE; i++)
    {
        int iVal = ENTER_BTN_VAL * i;
        m_BtnEnter[i].ChangeButtonInfo(m_BtnEnterStartPos.x, m_BtnEnterStartPos.y + iVal, 180, 29);
    }
}

bool CNewUIEnterDevilSquare::Render()
{
    EnableAlphaTest();

    RenderImage(IMAGE_ENTERDS_BASE_WINDOW_BACK, m_Pos.x, m_Pos.y, float(ENTERDS_BASE_WINDOW_WIDTH),
                float(ENTERDS_BASE_WINDOW_HEIGHT));
    RenderImage(IMAGE_ENTERDS_BASE_WINDOW_TOP, m_Pos.x, m_Pos.y, float(ENTERDS_BASE_WINDOW_WIDTH),
                64.f);
    RenderImage(IMAGE_ENTERDS_BASE_WINDOW_LEFT, m_Pos.x, m_Pos.y + 64.f, 21.f,
                float(ENTERDS_BASE_WINDOW_HEIGHT) - 64.f - 45.f);
    RenderImage(IMAGE_ENTERDS_BASE_WINDOW_RIGHT, m_Pos.x + float(ENTERDS_BASE_WINDOW_WIDTH) - 21.f,
                m_Pos.y + 64.f, 21.f, float(ENTERDS_BASE_WINDOW_HEIGHT) - 64.f - 45.f);
    RenderImage(IMAGE_ENTERDS_BASE_WINDOW_BOTTOM, m_Pos.x,
                m_Pos.y + float(ENTERDS_BASE_WINDOW_HEIGHT) - 45.f,
                float(ENTERDS_BASE_WINDOW_WIDTH), 45.f);

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetTextColor(0xFFFFFFFF);
    g_RenderText.SetBgColor(0x00000000);
    g_RenderText.RenderText(m_Pos.x + 60, m_Pos.y + 12, I18N::Game::DevilSquare, 72, 0,
                            RT3_SORT_CENTER);
    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.RenderText(m_EnterUITextPos.x, m_EnterUITextPos.y,
                            I18N::Game::YouVeBeenGivenAChanceToProveYourBravery, 190, 0,
                            RT3_SORT_CENTER);
    g_RenderText.RenderText(m_EnterUITextPos.x, m_EnterUITextPos.y + 15,
                            I18N::Game::NoOneHasEverEnteredTheDevilSquareYet, 190, 0,
                            RT3_SORT_CENTER);
    g_RenderText.RenderText(m_EnterUITextPos.x, m_EnterUITextPos.y + 30,
                            I18N::Game::NoHumanHasEverGoneThere, 190, 0, RT3_SORT_CENTER);
    g_RenderText.RenderText(m_EnterUITextPos.x, m_EnterUITextPos.y + 45,
                            I18N::Game::DoNotBelieveAnythingYouSeeInThere, 190, 0, RT3_SORT_CENTER);
    g_RenderText.RenderText(m_EnterUITextPos.x, m_EnterUITextPos.y + 60,
                            I18N::Game::OnlyTrustYourBraveryAndStrength, 190, 0, RT3_SORT_CENTER);
    g_RenderText.RenderText(m_EnterUITextPos.x, m_EnterUITextPos.y + 75,
                            I18N::Game::OnlyYourBraveryAndStrengthWillKeepYouAlive, 190, 0,
                            RT3_SORT_CENTER);

    for (int i = 0; i < MAX_ENTER_GRADE; i++)
    {
        m_BtnEnter[i].Render();
    }

    // Exit Button
    m_BtnExit.Render();

    DisableAlphaBlend();

    return true;
}

// BtnProcess

void CNewUIEnterDevilSquare::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_ENTERDS_BASE_WINDOW_BACK,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back01.tga", IMAGE_ENTERDS_BASE_WINDOW_TOP,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-L.tga", IMAGE_ENTERDS_BASE_WINDOW_LEFT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-R.tga", IMAGE_ENTERDS_BASE_WINDOW_RIGHT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back03.tga", IMAGE_ENTERDS_BASE_WINDOW_BOTTOM,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_exit_00.tga", IMAGE_ENTERDS_BASE_WINDOW_BTN_EXIT,
                LegacyTextureFilter::Linear); // Exit Button
    LoadBitmapW(L"Interface\\newui_btn_empty_big.tga", IMAGE_ENTERDS_BASE_WINDOW_BTN_ENTER,
                LegacyTextureFilter::Linear); // Enter Button
}

// UnloadImages
void CNewUIEnterDevilSquare::UnloadImages()
{
    DeleteBitmap(IMAGE_ENTERDS_BASE_WINDOW_BACK);
    DeleteBitmap(IMAGE_ENTERDS_BASE_WINDOW_TOP);
    DeleteBitmap(IMAGE_ENTERDS_BASE_WINDOW_LEFT);
    DeleteBitmap(IMAGE_ENTERDS_BASE_WINDOW_RIGHT);
    DeleteBitmap(IMAGE_ENTERDS_BASE_WINDOW_BOTTOM);
    DeleteBitmap(IMAGE_ENTERDS_BASE_WINDOW_BTN_EXIT);  // Exit Button
    DeleteBitmap(IMAGE_ENTERDS_BASE_WINDOW_BTN_ENTER); // Enter Button
}

void CNewUIExchangeLuckyCoin::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
    m_TextPos.x = m_Pos.x;
    m_TextPos.y = m_Pos.y + 80;

    SetBtnPos(m_Pos.x + ((EXCHANGE_LUCKYCOIN_WINDOW_WIDTH / 2) - (MSGBOX_BTN_EMPTY_WIDTH / 2)),
              m_Pos.y + 220);

    m_BtnExit.ChangeButtonInfo(m_Pos.x + 13, m_Pos.y + 392, 36, 29);

    for (int i = 0; i < MAX_EXCHANGE_BTN; i++)
    {
        int iVal = EXCHANGE_BTN_VAL * i;
        m_BtnExchange[i].ChangeButtonInfo(m_FirstBtnPos.x, m_FirstBtnPos.y + iVal,
                                          MSGBOX_BTN_EMPTY_WIDTH, MSGBOX_BTN_EMPTY_HEIGHT);
    }
}

bool CNewUIExchangeLuckyCoin::Render()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    RenderFrame();
    RenderTexts();
    RenderBtn();

    DisableAlphaBlend();

    return true;
}

void CNewUIExchangeLuckyCoin::RenderFrame()
{
    RenderImage(IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_BACK, m_Pos.x, m_Pos.y,
                float(EXCHANGE_LUCKYCOIN_WINDOW_WIDTH), float(EXCHANGE_LUCKYCOIN_WINDOW_HEIGHT));
    RenderImage(IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_TOP, m_Pos.x, m_Pos.y,
                float(EXCHANGE_LUCKYCOIN_WINDOW_WIDTH), 64.f);
    RenderImage(IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_LEFT, m_Pos.x, m_Pos.y + 64.f, 21.f,
                float(EXCHANGE_LUCKYCOIN_WINDOW_HEIGHT) - 64.f - 45.f);
    RenderImage(IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_RIGHT,
                m_Pos.x + float(EXCHANGE_LUCKYCOIN_WINDOW_WIDTH) - 21.f, m_Pos.y + 64.f, 21.f,
                float(EXCHANGE_LUCKYCOIN_WINDOW_HEIGHT) - 64.f - 45.f);
    RenderImage(IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_BOTTOM, m_Pos.x,
                m_Pos.y + float(EXCHANGE_LUCKYCOIN_WINDOW_HEIGHT) - 45.f,
                float(EXCHANGE_LUCKYCOIN_WINDOW_WIDTH), 45.f);
}

void CNewUIExchangeLuckyCoin::RenderTexts()
{
    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetBgColor(0, 0, 0, 0);

    g_RenderText.RenderText(m_Pos.x, m_Pos.y + 25, I18N::Game::LuckyCoinExchange, 190, 0,
                            RT3_SORT_CENTER);

    g_RenderText.RenderText(m_TextPos.x, m_Pos.y + 200, I18N::Game::Exchange1940,
                            EXCHANGE_LUCKYCOIN_WINDOW_WIDTH, 0, RT3_SORT_CENTER);

    g_RenderText.SetTextColor(255, 255, 0, 255);
    g_RenderText.RenderText(m_TextPos.x, m_TextPos.y, I18N::Game::Warning,
                            EXCHANGE_LUCKYCOIN_WINDOW_WIDTH, 0, RT3_SORT_CENTER);

    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    int iTextPosy = m_TextPos.y + (EXCHANGE_TEXT_VAL * 2);
    g_RenderText.RenderText(m_TextPos.x, iTextPosy, I18N::Game::ExchangedLuckyCoins,
                            EXCHANGE_LUCKYCOIN_WINDOW_WIDTH, 0, RT3_SORT_CENTER);
    iTextPosy += EXCHANGE_TEXT_VAL;
    g_RenderText.RenderText(m_TextPos.x, iTextPosy, I18N::Game::WillNotBeReturned,
                            EXCHANGE_LUCKYCOIN_WINDOW_WIDTH, 0, RT3_SORT_CENTER);
}

void CNewUIExchangeLuckyCoin::RenderBtn()
{
    for (int i = 0; i < MAX_EXCHANGE_BTN; i++)
    {
        m_BtnExchange[i].Render();
    }
    m_BtnExit.Render();
}

void CNewUIExchangeLuckyCoin::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_BACK,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back04.tga", IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_TOP,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-L.tga", IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_LEFT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-R.tga", IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_RIGHT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back03.tga", IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_BOTTOM,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_btn_empty_small.tga", IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_BTN_EXIT,
                LegacyTextureFilter::Linear); // Exit Button
    LoadBitmapW(L"Interface\\newui_btn_empty.tga", IMAGE_EXCHANGE_LUCKYCOIN_EXCHANGE_BTN,
                LegacyTextureFilter::Linear); // Exchange Button
}

void CNewUIExchangeLuckyCoin::UnloadImages()
{
    DeleteBitmap(IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_BACK);
    DeleteBitmap(IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_TOP);
    DeleteBitmap(IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_LEFT);
    DeleteBitmap(IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_RIGHT);
    DeleteBitmap(IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_BOTTOM);
    DeleteBitmap(IMAGE_EXCHANGE_LUCKYCOIN_WINDOW_BTN_EXIT); // Exit Button
    DeleteBitmap(IMAGE_EXCHANGE_LUCKYCOIN_EXCHANGE_BTN);    // Exchange Button
}

void CNewUIGateSwitchWindow::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool CNewUIGateSwitchWindow::Render()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    RenderFrame();

    POINT ptOrigin = {m_Pos.x, m_Pos.y + 50};

    g_RenderText.RenderText(ptOrigin.x + 95, ptOrigin.y, I18N::Game::CanCommandToOpenOrClose, 0, 0,
                            RT3_WRITE_CENTER);
    ptOrigin.y += 17;
    g_RenderText.RenderText(ptOrigin.x + 95, ptOrigin.y, I18N::Game::TheCastleGateInFront, 0, 0,
                            RT3_WRITE_CENTER);
    ptOrigin.y += 17;

    g_RenderText.SetBgColor(160, 0, 0, 255);
    g_RenderText.RenderText(ptOrigin.x + 95, ptOrigin.y,
                            I18N::Game::BeCarefulItMightBeBeneficialToTheEnemy, 0, 0,
                            RT3_WRITE_CENTER);
    ptOrigin.y += 17;
    g_RenderText.SetBgColor(0);

    RenderOutlineUpper(m_Pos.x + 0, m_Pos.y + 120, 162, 159);
    RenderOutlineLower(m_Pos.x + 0, m_Pos.y + 120, 162, 159);
    if (IsGateOpened())
    {
        RenderBitmap(BITMAP_INTERFACE_EX + 41, m_Pos.x + 17.5f, m_Pos.y + 120, 155, 168, 0.f, 0.f,
                     155 / 256.f, 168 / 256.f);
        m_BtnOpen.ChangeText(&I18N::Game::Close388);
    }
    else
    {
        RenderBitmap(BITMAP_INTERFACE_EX + 40, m_Pos.x + 17.5f, m_Pos.y + 120, 155, 168, 0.f, 0.f,
                     155 / 256.f, 168 / 256.f);
        m_BtnOpen.ChangeText(&I18N::Game::Open1107);
    }

    m_BtnOpen.Render();
    m_BtnExit.Render();
    DisableAlphaBlend();
    return true;
}

void CNewUIGateSwitchWindow::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_GATESWITCHWINDOW_BACK,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back01.tga", IMAGE_GATESWITCHWINDOW_TOP,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-L.tga", IMAGE_GATESWITCHWINDOW_LEFT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-R.tga", IMAGE_GATESWITCHWINDOW_RIGHT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back03.tga", IMAGE_GATESWITCHWINDOW_BOTTOM,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_exit_00.tga", IMAGE_GATESWITCHWINDOW_EXIT_BTN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_btn_empty.tga", IMAGE_GATESWITCHWINDOW_BUTTON,
                LegacyTextureFilter::Linear);

    LoadBitmapW(L"Interface\\newui_item_table01(L).tga", IMAGE_GATESWITCHWINDOW_TABLE_TOP_LEFT);
    LoadBitmapW(L"Interface\\newui_item_table01(R).tga", IMAGE_GATESWITCHWINDOW_TABLE_TOP_RIGHT);
    LoadBitmapW(L"Interface\\newui_item_table02(L).tga", IMAGE_GATESWITCHWINDOW_TABLE_BOTTOM_LEFT);
    LoadBitmapW(L"Interface\\newui_item_table02(R).tga", IMAGE_GATESWITCHWINDOW_TABLE_BOTTOM_RIGHT);
    LoadBitmapW(L"Interface\\newui_item_table03(Up).tga", IMAGE_GATESWITCHWINDOW_TABLE_TOP_PIXEL);
    LoadBitmapW(L"Interface\\newui_item_table03(Dw).tga",
                IMAGE_GATESWITCHWINDOW_TABLE_BOTTOM_PIXEL);
    LoadBitmapW(L"Interface\\newui_item_table03(L).tga", IMAGE_GATESWITCHWINDOW_TABLE_LEFT_PIXEL);
    LoadBitmapW(L"Interface\\newui_item_table03(R).tga", IMAGE_GATESWITCHWINDOW_TABLE_RIGHT_PIXEL);
}
void CNewUIGateSwitchWindow::UnloadImages()
{
    DeleteBitmap(IMAGE_GATESWITCHWINDOW_BOTTOM);
    DeleteBitmap(IMAGE_GATESWITCHWINDOW_RIGHT);
    DeleteBitmap(IMAGE_GATESWITCHWINDOW_LEFT);
    DeleteBitmap(IMAGE_GATESWITCHWINDOW_TOP);
    DeleteBitmap(IMAGE_GATESWITCHWINDOW_BACK);
    DeleteBitmap(IMAGE_GATESWITCHWINDOW_EXIT_BTN);
    DeleteBitmap(IMAGE_GATESWITCHWINDOW_BUTTON);
    DeleteBitmap(IMAGE_GATESWITCHWINDOW_TABLE_RIGHT_PIXEL);
    DeleteBitmap(IMAGE_GATESWITCHWINDOW_TABLE_LEFT_PIXEL);
    DeleteBitmap(IMAGE_GATESWITCHWINDOW_TABLE_BOTTOM_PIXEL);
    DeleteBitmap(IMAGE_GATESWITCHWINDOW_TABLE_TOP_PIXEL);
    DeleteBitmap(IMAGE_GATESWITCHWINDOW_TABLE_BOTTOM_RIGHT);
    DeleteBitmap(IMAGE_GATESWITCHWINDOW_TABLE_BOTTOM_LEFT);
    DeleteBitmap(IMAGE_GATESWITCHWINDOW_TABLE_TOP_RIGHT);
    DeleteBitmap(IMAGE_GATESWITCHWINDOW_TABLE_TOP_LEFT);
}

void CNewUIGateSwitchWindow::RenderFrame()
{
    RenderImage(IMAGE_GATESWITCHWINDOW_BACK, m_Pos.x, m_Pos.y, 190.f, 429.f);
    RenderImage(IMAGE_GATESWITCHWINDOW_TOP, m_Pos.x, m_Pos.y, 190.f, 64.f);
    RenderImage(IMAGE_GATESWITCHWINDOW_LEFT, m_Pos.x, m_Pos.y + 64, 21.f, 320.f);
    RenderImage(IMAGE_GATESWITCHWINDOW_RIGHT, m_Pos.x + INVENTORY_WIDTH - 21, m_Pos.y + 64, 21.f,
                320.f);
    RenderImage(IMAGE_GATESWITCHWINDOW_BOTTOM, m_Pos.x, m_Pos.y + INVENTORY_HEIGHT - 45, 190.f,
                45.f);

    wchar_t szText[256] = {
        0,
    };
    float fPos_x = m_Pos.x + 15.0f, fPos_y = m_Pos.y;
    float fLine_y = 13.0f;

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetTextColor(220, 220, 220, 255);
    g_RenderText.SetBgColor(0, 0, 0, 0);

    mu_swprintf(szText, L"%ls", I18N::Game::CastleGateSwitch);
    g_RenderText.RenderText(fPos_x, fPos_y + fLine_y, szText, 160.0f, 0, RT3_SORT_CENTER);
}

void CNewUIGateSwitchWindow::RenderOutlineUpper(float fPos_x, float fPos_y, float fWidth,
                                                float fHeight)
{
    POINT ptOrigin = {(long)fPos_x, (long)fPos_y};
    float fBoxWidth = fWidth;

    RenderImage(IMAGE_GATESWITCHWINDOW_TABLE_TOP_LEFT, ptOrigin.x + 12, ptOrigin.y - 4, 14, 14);
    RenderImage(IMAGE_GATESWITCHWINDOW_TABLE_TOP_RIGHT, ptOrigin.x + fBoxWidth + 4, ptOrigin.y - 4,
                14, 14);
    RenderImage(IMAGE_GATESWITCHWINDOW_TABLE_TOP_PIXEL, ptOrigin.x + 25, ptOrigin.y - 4,
                fBoxWidth - 21, 14);
}

void CNewUIGateSwitchWindow::RenderOutlineLower(float fPos_x, float fPos_y, float fWidth,
                                                float fHeight)
{
    POINT ptOrigin = {(long)fPos_x, (long)fPos_y};
    float fBoxWidth = fWidth;
    float fBoxHeight = fHeight;

    RenderImage(IMAGE_GATESWITCHWINDOW_TABLE_LEFT_PIXEL, ptOrigin.x + 12, ptOrigin.y + 9, 14,
                fBoxHeight);
    RenderImage(IMAGE_GATESWITCHWINDOW_TABLE_RIGHT_PIXEL, ptOrigin.x + fBoxWidth + 4,
                ptOrigin.y + 9, 14, fBoxHeight);
    RenderImage(IMAGE_GATESWITCHWINDOW_TABLE_BOTTOM_LEFT, ptOrigin.x + 12,
                ptOrigin.y + fBoxHeight + 3, 14, 14);
    RenderImage(IMAGE_GATESWITCHWINDOW_TABLE_BOTTOM_RIGHT, ptOrigin.x + fBoxWidth + 4,
                ptOrigin.y + fBoxHeight + 3, 14, 14);
    RenderImage(IMAGE_GATESWITCHWINDOW_TABLE_BOTTOM_PIXEL, ptOrigin.x + 25,
                ptOrigin.y + fBoxHeight + 3, fBoxWidth - 21, 14);
}

void SEASON3B::CNewUIGoldBowmanLena::RenderText(const wchar_t *text, int x, int y, int sx, int sy,
                                                DWORD color, DWORD backcolor, int sort)
{
    g_RenderText.SetFont(LegacyFontRole::Normal);

    DWORD backuptextcolor = g_RenderText.GetTextColor();
    DWORD backuptextbackcolor = g_RenderText.GetBgColor();

    g_RenderText.SetTextColor(color);
    g_RenderText.SetBgColor(backcolor);
    g_RenderText.RenderText(x, y, text, sx, sy, sort);

    g_RenderText.SetTextColor(backuptextcolor);
    g_RenderText.SetBgColor(backuptextbackcolor);
}

void CNewUIGoldBowmanLena::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_GBL_BACK, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back04.tga", IMAGE_GBL_TOP, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-L.tga", IMAGE_GBL_LEFT, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-R.tga", IMAGE_GBL_RIGHT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back03.tga", IMAGE_GBL_BOTTOM, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_btn_empty.tga", IMAGE_GBL_EXCHANGEBTN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_btn_empty.tga", IMAGE_GBL_BTN_SERIAL,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_exit_00.tga", IMAGE_GBL_BTN_EXIT, LegacyTextureFilter::Linear);
}

void CNewUIGoldBowmanLena::UnloadImages()
{
    DeleteBitmap(IMAGE_GBL_BTN_EXIT);
    DeleteBitmap(IMAGE_GBL_BTN_SERIAL);
    DeleteBitmap(IMAGE_GBL_EXCHANGEBTN);
    DeleteBitmap(IMAGE_GBL_BOTTOM);
    DeleteBitmap(IMAGE_GBL_RIGHT);
    DeleteBitmap(IMAGE_GBL_LEFT);
    DeleteBitmap(IMAGE_GBL_TOP);
    DeleteBitmap(IMAGE_GBL_BACK);
}

bool CNewUIGoldBowmanLena::Render()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    RenderFrame();

    RenderTexts();

    RendeerButton();

    DisableAlphaBlend();

    Render3D();

    return true;
}

void CNewUIGoldBowmanLena::RenderFrame()
{
    // frame
    RenderImage(IMAGE_GBL_BACK, m_Pos.x, m_Pos.y, 190.f, 429.f);
    RenderImage(IMAGE_GBL_TOP, m_Pos.x, m_Pos.y, 190.f, 64.f);
    RenderImage(IMAGE_GBL_LEFT, m_Pos.x, m_Pos.y + 64, 21.f, 320.f);
    RenderImage(IMAGE_GBL_RIGHT, m_Pos.x + INVENTORY_WIDTH - 21, m_Pos.y + 64, 21.f, 320.f);
    RenderImage(IMAGE_GBL_BOTTOM, m_Pos.x, m_Pos.y + INVENTORY_HEIGHT - 45, 190.f, 45.f);
}

void CNewUIGoldBowmanLena::RenderTexts()
{
    const wchar_t *name = getMonsterName(236);
    RenderText(name, m_Pos.x, m_Pos.y + 15, 190, 0, 0xFFFFFFFF, 0x00000000, RT3_SORT_CENTER);

    wchar_t Text[100];
    memset(&Text, 0, sizeof(wchar_t) * 100);
    for (int i = 0; i < 3; ++i)
    {
        memset(&Text, 0, sizeof(wchar_t) * 100);
        mu_swprintf(Text, I18N::Game::Lookup(700 + i));
        RenderText(Text, m_Pos.x, m_Pos.y + 100 + (i * 15), 190, 0, 0xFFFFFFFF, 0x00000000,
                   RT3_SORT_CENTER);
    }

    int registerItem = g_pMyInventory->GetInventoryCtrl()->GetItemCount(ITEM_POTION + 21, 0);

    memset(&Text, 0, sizeof(wchar_t) * 100);
    mu_swprintf(Text, L"%ls", I18N::Game::NumberOfRenaYouHaveCollected);
    RenderText(Text, m_Pos.x + 20, m_Pos.y + 180, 190, 0, 0xFF47DFFA, 0x00000000, RT3_SORT_LEFT);

    memset(&Text, 0, sizeof(wchar_t) * 100);
    mu_swprintf(Text, L"    X    %d", registerItem);
    RenderText(Text, m_Pos.x + 5, m_Pos.y + 202, 190, 0, 0xFFFFFFFF, 0x00000000, RT3_SORT_CENTER);

    memset(&Text, 0, sizeof(wchar_t) * 100);
    mu_swprintf(Text, L"%ls", I18N::Game::NumberOfRegisteredRena);
    RenderText(Text, m_Pos.x + 20, m_Pos.y + 225, 190, 0, 0xFF47DFFA, 0x00000000, RT3_SORT_LEFT);

    memset(&Text, 0, sizeof(wchar_t) * 100);
    mu_swprintf(Text, L"    X    %d", g_shEventChipCount);
    RenderText(Text, m_Pos.x + 5, m_Pos.y + 245, 190, 0, 0xFFFFFFFF, 0x00000000, RT3_SORT_CENTER);

    for (int j = 0; j < 2; ++j)
    {
        memset(&Text, 0, sizeof(wchar_t) * 100);
        mu_swprintf(Text, I18N::Game::Lookup(703 + j));
        RenderText(Text, m_Pos.x, m_Pos.y + 350 + (j * 15), 190, 0, 0xFFFA47D6, 0x00000000,
                   RT3_SORT_CENTER);
    }
}

void CNewUIGoldBowmanLena::Render3D()
{
    EndBitmap();

    glMatrixMode(GL_PROJECTION);
    SaveCameraPerspective();
    glPushMatrix();
    glLoadIdentity();
    glViewport2(0, 0, WindowWidth, WindowHeight);
    gluPerspective2(1.f, (float)(WindowWidth) / (float)(WindowHeight), RENDER_ITEMVIEW_NEAR,
                    RENDER_ITEMVIEW_FAR);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    cameraProjection_.GetModelViewMatrix(g_Camera.Matrix);
    EnableDepthTest();
    EnableDepthMask();

    int Type = ITEM_POTION + 21;
    int Level = 0;
    float x = (float)REFERENCE_WIDTH - 120.f;
    float y = 200.f;
    float Width = (float)ItemAttribute[Type].Width * INVENTORY_SCALE;
    float Height = (float)ItemAttribute[Type].Height * INVENTORY_SCALE;
    RenderItem3D(x, y, Width, Height, Type, Level, 0, 0, false);
    RenderItem3D(x, y + 42, Width, Height, Type, Level, 0, 0, false);

    UpdateMousePositionn();

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    RestoreCameraPerspective();
    BeginBitmap();
}

void SEASON3B::GoldBowmanLenaLegacyCalls::RenderText(const wchar_t *text, int x, int y, int sx,
                                                     int sy, DWORD color, DWORD backcolor, int sort)
{
    return owner_.RenderText(text, x, y, sx, sy, color, backcolor, sort);
} // OMF-01924

void SEASON3B::CNewUIKanturu2ndEnterNpc::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

void SEASON3B::CNewUIKanturu2ndEnterNpc::StageContent()
{
    panel_.SetText("tfSubject", m_strSubject);
    panel_.SetText("TextArea1", m_iStateTextNum > 0 ? m_strStateText[0] : L"");
    panel_.SetText("TextArea2", m_iStateTextNum > 1 ? m_strStateText[1] : L"");
    panel_.SetText("TextArea3", m_iStateTextNum > 2 ? m_strStateText[2] : L"");
    panel_.SetText("btnEnter-label", I18N::Game::Enter);
    panel_.SetText("btnRefresh-label", I18N::Game::Refresh);
    panel_.SetText("btnClose-label", I18N::Game::Close388);
    panel_.SetButtonEnabled("btnEnter", canEnter_);
    panel_.SetButtonEnabled("btnRefresh", !refreshLocked_);
}

bool SEASON3B::CNewUIKanturu2ndEnterNpc::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}
bool SEASON3B::CNewUIKanturu2ndEnterNpc::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_);
}

void SEASON3B::CNewUIKanturuInfoWindow::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

void SEASON3B::CNewUIKanturuInfoWindow::StageContent()
{
    constexpr int MillisecondsPerSecond = 1000, SecondsPerMinute = 60, TextCapacity = 256;
    const int seconds =
        std::max(0, m_iSecond - int(DWORD(GetTickCount() - m_dwSyncTime) / MillisecondsPerSecond));
    const int maya = g_Direction.m_CKanturu.m_iMayaState;
    const std::array<int, 4> state{UserCount, MonsterCount, seconds, maya};
    const char *locale = I18N::GetCurrentLocale();
    if (infoState_ == state && locale_ == locale)
        return;
    infoState_ = state;
    locale_ = locale;
    wchar_t text[TextCapacity];
    mu_swprintf(text, I18N::Game::CharacterD, UserCount);
    panel_.SetText("strUserCount", text);
    if (maya == KANTURU_MAYA_DIRECTION_MAYA1 || maya == KANTURU_MAYA_DIRECTION_MAYA2 ||
        maya == KANTURU_MAYA_DIRECTION_MAYA3)
        panel_.SetText("strMonsterCount", I18N::Game::MonsterBoss2182);
    else
    {
        mu_swprintf(text, I18N::Game::MonsterD, MonsterCount);
        panel_.SetText("strMonsterCount", text);
    }
    mu_swprintf(text, L"%02d:%02d", seconds / SecondsPerMinute, seconds % SecondsPerMinute);
    panel_.SetText("strTime", text);
}

bool SEASON3B::CNewUIKanturuInfoWindow::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}
bool SEASON3B::CNewUIKanturuInfoWindow::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_);
}

namespace SEASON3B
{

void CNewUIRegistrationLuckyCoin::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool CNewUIRegistrationLuckyCoin::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    RenderFrame();
    RenderTexts();
    RenderButtons();
    RenderLuckyCoin();
    DisableAlphaBlend();
    return true;
}

void CNewUIRegistrationLuckyCoin::RenderFrame()
{
    RenderImage(IMAGE_BACK, m_Pos.x, m_Pos.y, 190.f, 429.f);
    RenderImage(IMAGE_TOP, m_Pos.x, m_Pos.y, 190.f, 64.f);
    RenderImage(IMAGE_LEFT, m_Pos.x, m_Pos.y + 64, 21.f, 320.f);
    RenderImage(IMAGE_RIGHT, m_Pos.x + LUCKYCOIN_REG_WIDTH - 21, m_Pos.y + 64, 21.f, 320.f);
    RenderImage(IMAGE_BOTTOM, m_Pos.x, m_Pos.y + LUCKYCOIN_REG_HEIGHT - 45, 190.f, 45.f);
}

void CNewUIRegistrationLuckyCoin::RenderTexts()
{
    wchar_t szText[256] = {
        0,
    };
    float _x = GetPos().x;
    float _y = GetPos().y + 25;

    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);
    mu_swprintf(szText, I18N::Game::LuckyCoinRegistration);
    g_RenderText.RenderText(_x, _y, szText, LUCKYCOIN_REG_WIDTH, 0, RT3_SORT_CENTER);

    g_RenderText.SetFont(LegacyFontRole::Normal);
    mu_swprintf(szText, I18N::Game::Register255LuckyCoinsDuringTheEvent);
    g_RenderText.RenderText(_x, _y + 40, szText, LUCKYCOIN_REG_WIDTH, 0, RT3_SORT_CENTER);
    mu_swprintf(szText, I18N::Game::ForAChanceToGet);
    g_RenderText.RenderText(_x, _y + 60, szText, LUCKYCOIN_REG_WIDTH, 0, RT3_SORT_CENTER);
    mu_swprintf(szText, I18N::Game::TheAbsoluteWeapon);
    g_RenderText.RenderText(_x, _y + 80, szText, LUCKYCOIN_REG_WIDTH, 0, RT3_SORT_CENTER);
    mu_swprintf(szText, I18N::Game::PleaseCheckTheWebPageForTheEventDetails);
    g_RenderText.RenderText(_x, _y + 100, szText, LUCKYCOIN_REG_WIDTH, 0, RT3_SORT_CENTER);

    g_RenderText.SetFont(LegacyFontRole::Bold);

    mu_swprintf(szText, I18N::Game::Registered);
    g_RenderText.RenderText(_x, _y + 120, szText, LUCKYCOIN_REG_WIDTH, 0, RT3_SORT_CENTER);

    mu_swprintf(szText, I18N::Game::XDCoins, GetRegistCount());
    g_RenderText.RenderText(_x + 24, _y + 150, szText, LUCKYCOIN_REG_WIDTH, 0, RT3_SORT_CENTER);
}

void CNewUIRegistrationLuckyCoin::RenderLuckyCoin()
{
    float x, y, width, height;

    x = GetPos().x - 20;
    y = GetPos().y + 50;

    width = LUCKYCOIN_REG_WIDTH;
    height = LUCKYCOIN_REG_HEIGHT;

    EndBitmap();

    glMatrixMode(GL_PROJECTION);
    SaveCameraPerspective();
    glPushMatrix();
    glLoadIdentity();
    glViewport2(0, 0, WindowWidth, WindowHeight);
    gluPerspective2(1.f, (float)(WindowWidth) / (float)(WindowHeight), RENDER_ITEMVIEW_NEAR,
                    RENDER_ITEMVIEW_FAR);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    cameraProjection_.GetModelViewMatrix(g_Camera.Matrix);
    EnableDepthTest();
    EnableDepthMask();

    glClear(GL_DEPTH_BUFFER_BIT);

    SetItemRotation(true);
    RenderItem3D(x, y, width, height, m_CoinItem->Type, m_CoinItem->Level, 0, 0, true);
    SetItemRotation(false);

    UpdateMousePositionn();

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    RestoreCameraPerspective();
    BeginBitmap();
}

void CNewUIRegistrationLuckyCoin::RenderButtons()
{
    m_CloseButton.Render();
    m_RegistButton.Render();
}

void CNewUIRegistrationLuckyCoin::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_BACK, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back04.tga", IMAGE_TOP, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-L.tga", IMAGE_LEFT, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-R.tga", IMAGE_RIGHT, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back03.tga", IMAGE_BOTTOM, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_btn_empty_small.tga", IMAGE_CLOSE_REGIST,
                LegacyTextureFilter::Linear);
}

void CNewUIRegistrationLuckyCoin::UnloadImages()
{
    DeleteBitmap(IMAGE_CLOSE_REGIST);
    DeleteBitmap(IMAGE_BOTTOM);
    DeleteBitmap(IMAGE_RIGHT);
    DeleteBitmap(IMAGE_LEFT);
    DeleteBitmap(IMAGE_TOP);
    DeleteBitmap(IMAGE_BACK);
}

} // namespace SEASON3B

void CNewUIGoldBowmanWindow::StageContent()
{
    const char *locale = I18N::GetCurrentLocale();
    if (locale_ == locale && gift_ == g_strGiftName)
        return;
    locale_ = locale;
    gift_ = g_strGiftName;
    constexpr int GoldenArcherNpc = 236;
    panel_.SetText("tfTitle", getMonsterName(GoldenArcherNpc));
    panel_.SetText("tfInput", I18N::Game::EnterTheLuckyNumber);
    panel_.SetText("btnCertify-label", I18N::Game::LuckyNumberRegistered);
    const std::wstring introduction = std::wstring(I18N::Game::EnterThe12DigitLuckyNumber) + L"\n" +
                                      std::wstring(I18N::Game::WrittenOnThe100WinningCard) + L"\n" +
                                      std::wstring(I18N::Game::LuckyNumberRegistrationPeriod) +
                                      L"\n" + std::wstring(I18N::Game::Oct282003Nov30);
    panel_.SetText("taCertifyMent", introduction);
    const std::wstring example =
        std::wstring(I18N::Game::ExAUS919DKL2J9) + L"\n" +
        std::wstring(I18N::Game::PleaseMakeSureToDifferentiate) + L"\n" +
        std::wstring(I18N::Game::AlphabetOAndNumber0AndAlphabetIAndNumber1) +
        (gift_.empty() ? L"" : L"\n" + gift_);
    panel_.SetText("taCertifyExample", example);
}

bool CNewUIGoldBowmanWindow::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}

bool CNewUIGoldBowmanWindow::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_);
}

//  CSEventMatch.cpp

void CSBaseMatch::RenderTime(void)
{
    float x, y;

    if (m_iMatchCountDownType <= TYPE_MATCH_NONE || m_iMatchCountDownType >= TYPE_MATCH_END)
    {
        return;
    }

    const auto now = MatchClock::now();
    const auto elapsedSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(now - m_matchCountDownStart);
    if (elapsedSeconds >= EventMatchDetail::kMatchCountdownDuration)
    {
        return;
    }

    DisableAlphaBlend();
    EnableAlphaTest(false);

    x = 10.0f;
    y = (float)REFERENCE_HEIGHT - 70.0f;
    g_RenderText.SetTextColor(128, 128, 255, 255);
    g_RenderText.SetBgColor(0, 0, 0, 128);

    const int remainingSeconds =
        static_cast<int>((EventMatchDetail::kMatchCountdownDuration - elapsedSeconds).count());
    wchar_t lpszStr[256]{0};

    if (m_iMatchCountDownType >= TYPE_MATCH_CASTLE_ENTER_CLOSE &&
        m_iMatchCountDownType <= TYPE_MATCH_CASTLE_END)
    {
        const int textNum = 824 + m_iMatchCountDownType - TYPE_MATCH_CASTLE_ENTER_CLOSE;
        EventMatchDetail::WriteWide(lpszStr, I18N::Game::Lookup(textNum), I18N::Game::BloodCastle,
                                    remainingSeconds);
    }
    else if (m_iMatchCountDownType >= TYPE_MATCH_CHAOS_ENTER_START &&
             m_iMatchCountDownType <= TYPE_MATCH_CHAOS_END)
    {
        int textNum = 824 + m_iMatchCountDownType - TYPE_MATCH_CHAOS_ENTER_START;
        if (textNum == 825)
        {
            textNum = 828;
        }
        EventMatchDetail::WriteWide(lpszStr, I18N::Game::Lookup(textNum), I18N::Game::ChaosCastle,
                                    remainingSeconds);
    }
    else if (m_iMatchCountDownType == TYPE_MATCH_CURSEDTEMPLE_ENTER_CLOSE ||
             m_iMatchCountDownType == TYPE_MATCH_CURSEDTEMPLE_GAME_START)
    {
        int textNum = (m_iMatchCountDownType == TYPE_MATCH_CURSEDTEMPLE_GAME_START) ? 2386 : 2384;
        EventMatchDetail::WriteWide(lpszStr, I18N::Game::Lookup(textNum), remainingSeconds);
    }
    else if (m_iMatchCountDownType >= TYPE_MATCH_DOPPELGANGER_ENTER_CLOSE &&
             m_iMatchCountDownType <= TYPE_MATCH_DOPPELGANGER_CLOSE)
    {
        const int textNum = 2860 + m_iMatchCountDownType - TYPE_MATCH_DOPPELGANGER_ENTER_CLOSE;
        EventMatchDetail::WriteWide(lpszStr, I18N::Game::Lookup(textNum), remainingSeconds);
    }
    else
    {
        const int textNum = 640 + m_iMatchCountDownType - TYPE_MATCH_DEVIL_ENTER_START;
        EventMatchDetail::WriteWide(lpszStr, I18N::Game::Lookup(textNum), remainingSeconds);
    }

    g_RenderText.RenderText(REFERENCE_WIDTH / 2, static_cast<int>(y), lpszStr, 0, 0,
                            RT3_WRITE_CENTER);
}

void CSBaseMatch::renderOnlyTime(float x, float y, int MatchTime)
{
    wchar_t lpszStr[256]{0};
    const int iMinute = MatchTime / 60;
    const int iSecondTime = MatchTime - (iMinute * 60);

    EventMatchDetail::WriteWide(lpszStr, L" %.2d :", iMinute);

    if (iSecondTime >= 0)
    {
        EventMatchDetail::AppendWide(lpszStr, L" %.2d", iSecondTime);
    }

    if (iMinute < 5)
    {
        g_RenderText.SetTextColor(255, 32, 32, 255);
    }
    if (iMinute < 15)
    {
        EventMatchDetail::AppendWide(lpszStr, L": %.2d", static_cast<int>(WorldTime) % 60);
    }
    g_RenderText.SetFont(LegacyFontRole::Large);
    g_RenderText.RenderText(static_cast<int>(x), static_cast<int>(y), lpszStr, 0, 0,
                            RT3_WRITE_CENTER);
}

void CSDevilSquareMatch::RenderMatchResult(void)
{
    int xPos[6] = {
        m_PosResult.x,
    };
    xPos[1] = xPos[0] + 15;
    xPos[2] = xPos[1] + 15;
    xPos[3] = xPos[2] + 60;
    xPos[4] = xPos[3] + 50;
    xPos[5] = xPos[4] + 38;

    int yPos = m_PosResult.y + 40;

    g_RenderText.SetBgColor(0);

    wchar_t lpszStr[256]{0};

    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.RenderText(xPos[2], yPos, I18N::Game::Congratulations);
    yPos += 16;
    EventMatchDetail::WriteWide(lpszStr, I18N::Game::SYourBraveryIsProvenInDevilSquare, Hero->ID);
    g_RenderText.RenderText((xPos[2]), yPos, lpszStr);
    yPos += 24;

    g_RenderText.SetTextColor(0, 255, 0, 255);
    g_RenderText.RenderText(xPos[2], yPos, I18N::Game::Rank, xPos[3] - xPos[1], RT3_SORT_CENTER);
    g_RenderText.RenderText(xPos[3], yPos, I18N::Game::Point, xPos[4] - xPos[3], RT3_SORT_CENTER);
    g_RenderText.RenderText(xPos[4], yPos, I18N::Game::EXP, xPos[5] - xPos[4], RT3_SORT_CENTER);
    g_RenderText.RenderText(xPos[5], yPos, I18N::Game::Reward,
                            (REFERENCE_WIDTH - 230) / 2 + 210 - xPos[5], RT3_SORT_CENTER);
    yPos += 20;

    int yStartPos = yPos;

    for (int i = 0; i < m_iNumResult; ++i)
    {
        MatchResult *pResult = &m_MatchResult[i];

        // Highlight "my result" with a different color
        if (i == m_iMyResult - 1)
        {
            g_RenderText.SetTextColor(200, 120, 0, 255); // Special color for "my result"
        }
        else
        {
            g_RenderText.SetTextColor(255, 255, 0, 255); // Normal color for others
        }

        // Render rank number (index + 1 for display)
        EventMatchDetail::WriteWide(lpszStr, L"%2d", i + 1);
        g_RenderText.RenderText(xPos[1], yPos, lpszStr);

        // Render player ID
        std::fill(std::begin(lpszStr), std::end(lpszStr), L'\0');
        CMultiLanguage::ConvertFromUtf8(lpszStr, reinterpret_cast<char *>(pResult->m_lpID),
                                        MAX_USERNAME_SIZE);
        g_RenderText.RenderText(xPos[2], yPos, lpszStr);

        // Render score
        EventMatchDetail::WriteWide(lpszStr, L"%10lu", pResult->m_iScore);
        g_RenderText.RenderText(xPos[3], yPos, lpszStr);

        // Render experience
        EventMatchDetail::WriteWide(lpszStr, L"%6lu", pResult->m_dwExp);
        g_RenderText.RenderText(xPos[4], yPos, lpszStr);

        // Render Zen
        EventMatchDetail::WriteWide(lpszStr, L"%6lu", pResult->m_iZen);
        g_RenderText.RenderText(xPos[5], yPos, lpszStr);

        // Increment yPos for the next row
        yPos += 16;
    }

    // Render a special section for "my result"
    if (m_iMyResult > 0 && m_iMyResult <= m_iNumResult)
    {
        int myIndex = m_iMyResult - 1; // Convert to zero-based index
        MatchResult *myResult = &m_MatchResult[myIndex];

        yPos = yStartPos + 16 * 10;                  // Fixed position for the special section
        g_RenderText.SetTextColor(200, 120, 0, 255); // Special color

        // "My Ranking" label
        g_RenderText.RenderText(xPos[0], yPos, I18N::Game::MyInfo, 230, 0, RT3_SORT_CENTER);
        yPos += 20;

        // Render my rank
        EventMatchDetail::WriteWide(lpszStr, L"%2d", m_iMyResult);
        g_RenderText.RenderText(xPos[1], yPos, lpszStr);

        // Render my ID
        std::fill(std::begin(lpszStr), std::end(lpszStr), L'\0');
        CMultiLanguage::ConvertFromUtf8(lpszStr, reinterpret_cast<char *>(myResult->m_lpID),
                                        MAX_USERNAME_SIZE);
        g_RenderText.RenderText(xPos[2], yPos, lpszStr);

        // Render my score
        EventMatchDetail::WriteWide(lpszStr, L"%10lu", myResult->m_iScore);
        g_RenderText.RenderText(xPos[3], yPos, lpszStr);

        // Render my experience
        EventMatchDetail::WriteWide(lpszStr, L"%6lu", myResult->m_dwExp);
        g_RenderText.RenderText(xPos[4], yPos, lpszStr);

        // Render my Zen
        EventMatchDetail::WriteWide(lpszStr, L"%6lu", myResult->m_iZen);
        g_RenderText.RenderText(xPos[5], yPos, lpszStr);
    }
}

// Construction/Destruction

void SEASON3B::CNewUICursedTempleEnter::DrawText(wchar_t *text, int textposx, int textposy,
                                                 DWORD textcolor, DWORD textbackcolor, int textsort,
                                                 float fontboxwidth, bool isbold)
{
    if (isbold)
    {
        g_RenderText.SetFont(LegacyFontRole::Bold);
    }
    else
    {
        g_RenderText.SetFont(LegacyFontRole::Normal);
    }

    DWORD backuptextcolor = g_RenderText.GetTextColor();
    DWORD backuptextbackcolor = g_RenderText.GetBgColor();

    g_RenderText.SetTextColor(textcolor);
    g_RenderText.SetBgColor(textbackcolor);
    g_RenderText.RenderText(textposx, textposy, text, fontboxwidth, 0, textsort);
    g_RenderText.SetTextColor(backuptextcolor);
    g_RenderText.SetBgColor(backuptextbackcolor);
}

void SEASON3B::CNewUICursedTempleEnter::SetButtonInfo()
{
    float x;
    x = m_Pos.x + (((CURSEDTEMPLE_ENTER_WINDOW_WIDTH / 2) - MSGBOX_BTN_WIDTH) / 2);
    m_Button[CURSEDTEMPLEENTER_OPEN].ChangeButtonImgState(
        true, CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY_VERY_SMALL, true);

    m_Button[CURSEDTEMPLEENTER_OPEN].ChangeButtonInfo(x, m_Pos.y + 203, 54, 23);

    // 2147 "입장하기"
    m_Button[CURSEDTEMPLEENTER_OPEN].ChangeText(&I18N::Game::Enter);

    x = m_Pos.x + (CURSEDTEMPLE_ENTER_WINDOW_WIDTH / 2) +
        (((CURSEDTEMPLE_ENTER_WINDOW_WIDTH / 2) - MSGBOX_BTN_WIDTH) / 2);
    m_Button[CURSEDTEMPLEENTER_EXIT].ChangeButtonImgState(
        true, CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY_VERY_SMALL, true);

    m_Button[CURSEDTEMPLEENTER_EXIT].ChangeButtonInfo(x, m_Pos.y + 203, 54, 23);
    // 1002 "닫기"
    m_Button[CURSEDTEMPLEENTER_EXIT].ChangeText(&I18N::Game::Close388);
}

void SEASON3B::CNewUICursedTempleEnter::RenderText()
{
    wchar_t Text[100];

    memset(&Text, 0, sizeof(wchar_t));

    mu_swprintf(Text, I18N::Game::DoYouWishToGoToTheIllusionTemple);
    DrawText(Text, m_Pos.x, m_Pos.y + 13, 0xFF49B0FF, 0x00000000, RT3_SORT_CENTER,
             CURSEDTEMPLE_ENTER_WINDOW_WIDTH, true);

    int enterlevel = -1;

    if (CheckEnterLevel(enterlevel))
    {
        memset(&Text, 0, sizeof(Text));

        mu_swprintf(Text, I18N::Game::TheDIllusionTemple, enterlevel);
        DrawText(Text, m_Pos.x + 3, m_Pos.y + 42, 0xffffffff, 0x00000000, RT3_SORT_CENTER,
                 CURSEDTEMPLE_ENTER_WINDOW_WIDTH - 10, false);

        for (int i = 0; i < TempleEntryDetail::EnterLevelCount + 1; ++i)
        {
            memset(&Text, 0, sizeof(Text));

            if (i == 5)
            {
                wcscpy(Text, I18N::Game::MasterLevel);
            }
            else
            {
                mu_swprintf(Text, I18N::Game::LevelDD, TempleEntryDetail::EnterMinLevel[i],
                            TempleEntryDetail::EnterMaxLevel[i]);
            }

            if (enterlevel == i + 1)
            {
                DisableAlphaBlend();
                mu_swprintf(Text, L"%ls %ls", Text, I18N::Game::EntranceEnabled);
                DrawText(Text, m_Pos.x + 3, m_Pos.y + 67 + (i * 15), 0xffffffff, 0xff0000ff,
                         RT3_SORT_CENTER, CURSEDTEMPLE_ENTER_WINDOW_WIDTH - 10, false);
                EnableAlphaTest();
            }
            else
            {
                mu_swprintf(Text, L"%ls %ls", Text, I18N::Game::EntranceDisabled);
                DrawText(Text, m_Pos.x + 3, m_Pos.y + 67 + (i * 15), 0xffffffff, 0x00000000,
                         RT3_SORT_CENTER, CURSEDTEMPLE_ENTER_WINDOW_WIDTH - 10, false);
            }
        }

        memset(&Text, 0, sizeof(char));
        mu_swprintf(Text, I18N::Game::CurrentMembersD, m_EnterCount);
        DrawText(Text, m_Pos.x + 3, m_Pos.y + 70 + ((TempleEntryDetail::EnterLevelCount + 1) * 15),
                 0xff0000ff, 0x00000000, RT3_SORT_CENTER, CURSEDTEMPLE_ENTER_WINDOW_WIDTH - 10,
                 false);
    }
    else
    {
        memset(&Text, 0, sizeof(char));
        mu_swprintf(Text, I18N::Game::YouMustBeOfTheMinimumLevel220ToEnterTheZone);
        DrawText(Text, m_Pos.x, m_Pos.y + 52, 0xff0000ff, 0x00000000, RT3_SORT_CENTER,
                 CURSEDTEMPLE_ENTER_WINDOW_WIDTH, false);
    }
}

void SEASON3B::CNewUICursedTempleEnter::RenderFrame()
{
    float x, y, width, height;

    x = GetPos().x;
    y = GetPos().y + 2.f, width = CURSEDTEMPLE_ENTER_WINDOW_WIDTH - MSGBOX_BACK_BLANK_WIDTH;
    height = CURSEDTEMPLE_ENTER_WINDOW_HEIGHT - MSGBOX_BACK_BLANK_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, x, y, width, height);

    x = GetPos().x;
    y = GetPos().y, width = MSGBOX_WIDTH;
    height = MSGBOX_TOP_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_TOP_TITLEBAR, x, y, width, height);

    x = GetPos().x;
    y += MSGBOX_TOP_HEIGHT;
    width = MSGBOX_WIDTH;
    height = MSGBOX_MIDDLE_HEIGHT;
    for (int i = 0; i < 9; ++i)
    {
        RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_MIDDLE, x, y, width, height);
        y += height;
    }

    x = GetPos().x;
    width = MSGBOX_WIDTH;
    height = MSGBOX_BOTTOM_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_BOTTOM, x, y, width, height);

    x = GetPos().x;
    y = GetPos().y + CURSEDTEMPLE_ENTER_WINDOW_HEIGHT - 77;
    width = MSGBOX_LINE_WIDTH;
    height = MSGBOX_LINE_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_LINE, x, y, width, height);
}

bool SEASON3B::CNewUICursedTempleEnter::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    RenderFrame();
    RenderText();
    RenderButtons();

    DisableAlphaBlend();

    return true;
}

void SEASON3B::CNewUICursedTempleEnter::RenderButtons()
{
    for (int i = 0; i < CURSEDTEMPLEENTER_MAXBUTTONCOUNT; ++i)
    {
        // 버튼 렌더링
        m_Button[i].Render();
    }
}

//ServerMessage

void SEASON3B::CNewUICursedTempleSystem::DrawText(wchar_t *text, int textposx, int textposy,
                                                  DWORD textcolor, DWORD textbackcolor,
                                                  int textsort, float fontboxwidth, bool isbold)
{
    if (isbold)
    {
        g_RenderText.SetFont(LegacyFontRole::Bold);
    }
    else
    {
        g_RenderText.SetFont(LegacyFontRole::Normal);
    }

    DWORD backuptextcolor = g_RenderText.GetTextColor();
    DWORD backuptextbackcolor = g_RenderText.GetBgColor();

    g_RenderText.SetTextColor(textcolor);
    g_RenderText.SetBgColor(textbackcolor);
    g_RenderText.RenderText(textposx, textposy, text, fontboxwidth, 0, textsort);
    g_RenderText.SetTextColor(backuptextcolor);
    g_RenderText.SetBgColor(backuptextbackcolor);
}

void SEASON3B::CNewUICursedTempleSystem::LoadImages()
{
    //minimap
    LoadBitmapW(L"Interface\\newui_ctminmapframe.tga", IMAGE_CURSEDTEMPLESYSTEM_MINIMAPFRAME,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_ctminmap.jpg", IMAGE_CURSEDTEMPLESYSTEM_MINIMAP,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_Bt_clearness_illusion.jpg",
                IMAGE_CURSEDTEMPLESYSTEM_MINIMAPALPBTN, LegacyTextureFilter::Linear);

    //minimapicon
    LoadBitmapW(L"Interface\\newui_ctminmap_Relic.tga",
                IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_HOLYITEM_PC, LegacyTextureFilter::Linear,
                LegacyTextureWrap::ClampToEdge);
    LoadBitmapW(L"Interface\\newui_ctminmap_TeamA_box.tga",
                IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ILLUSION_HOLYITEM, LegacyTextureFilter::Linear,
                LegacyTextureWrap::ClampToEdge);
    LoadBitmapW(L"Interface\\newui_ctminmap_TeamA_member.tga",
                IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ILLUSION_PC, LegacyTextureFilter::Linear,
                LegacyTextureWrap::ClampToEdge);
    LoadBitmapW(L"Interface\\newui_ctminmap_TeamA_npc.tga",
                IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ILLUSION_NPC, LegacyTextureFilter::Linear,
                LegacyTextureWrap::ClampToEdge);
    LoadBitmapW(L"Interface\\newui_ctminmap_TeamB_box.tga",
                IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ALLIED_HOLYITEM, LegacyTextureFilter::Linear,
                LegacyTextureWrap::ClampToEdge);
    LoadBitmapW(L"Interface\\newui_ctminmap_TeamB_member.tga",
                IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ALLIED_PC, LegacyTextureFilter::Linear,
                LegacyTextureWrap::ClampToEdge);
    LoadBitmapW(L"Interface\\newui_ctminmap_TeamB_npc.tga",
                IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ALLIED_NPC, LegacyTextureFilter::Linear,
                LegacyTextureWrap::ClampToEdge);
    LoadBitmapW(L"Interface\\newui_ctminmap_Hero.tga", IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_HERO,
                LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);

    //prorogress, npctalk
    LoadBitmapW(L"Interface\\newui_msgbox_top.tga", IMAGE_CURSEDTEMPLESYSTEM_TOP,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_msgbox_middle.tga", IMAGE_CURSEDTEMPLESYSTEM_MIDDLE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_msgbox_bottom.tga", IMAGE_CURSEDTEMPLESYSTEM_BOTTOM,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_CURSEDTEMPLESYSTEM_BACK,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_btn_empty_very_small.tga", IMAGE_CURSEDTEMPLESYSTEM_BTN,
                LegacyTextureFilter::Linear);
}

void SEASON3B::CNewUICursedTempleSystem::UnloadImages()
{
    //prorogress, npctalk
    DeleteBitmap(IMAGE_CURSEDTEMPLESYSTEM_BTN);
    DeleteBitmap(IMAGE_CURSEDTEMPLESYSTEM_BACK);
    DeleteBitmap(IMAGE_CURSEDTEMPLESYSTEM_BOTTOM);
    DeleteBitmap(IMAGE_CURSEDTEMPLESYSTEM_MIDDLE);
    DeleteBitmap(IMAGE_CURSEDTEMPLESYSTEM_TOP);

    //minmapicon
    DeleteBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_HERO);
    DeleteBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ALLIED_NPC);
    DeleteBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ALLIED_PC);
    DeleteBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ALLIED_HOLYITEM);
    DeleteBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ILLUSION_NPC);
    DeleteBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ILLUSION_PC);
    DeleteBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ILLUSION_HOLYITEM);
    DeleteBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_HOLYITEM_PC);

    //minimap
    DeleteBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPALPBTN);
    DeleteBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAP);
    DeleteBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPFRAME);
}

void SEASON3B::CNewUICursedTempleSystem::SetButtonInfo()
{
    m_Button[CURSEDTEMPLERESULT_ALPH].ChangeButtonImgState(
        true, IMAGE_CURSEDTEMPLESYSTEM_MINIMAPALPBTN, true);
    m_Button[CURSEDTEMPLERESULT_ALPH].ChangeButtonInfo(513, 238, 38, 24);
}

void SEASON3B::CNewUICursedTempleSystem::StageModernContent()
{
    const char *locale = I18N::GetCurrentLocale();
    const std::array<int, 3> scores{m_AlliedPoint, m_IllusionPoint,
                                    static_cast<int>(m_EventMapTime)};
    if (scores != stagedScores_ || locale_ != locale)
    {
        stagedScores_ = scores;
        locale_ = locale;
        modernContent_.labels[0] = std::to_wstring(m_AlliedPoint);
        modernContent_.labels[1] = std::to_wstring(m_IllusionPoint);
        constexpr int SecondsPerMinute = 60;
        wchar_t time[32];
        mu_swprintf(time, L"%02u:%02u", m_EventMapTime / SecondsPerMinute,
                    m_EventMapTime % SecondsPerMinute);
        modernContent_.labels[2] = time;
        teamLabels_ = {I18N::Game::MUAlliance, I18N::Game::IllusionSorcery};
    }
    modernContent_.selected = Hero->m_CursedTempleCurSkill - AT_SKILL_CURSED_TEMPLE_PRODECTION;
    for (int i = 0; i < UI::Modern::PC::Events::RmlTempleInfoPanel::SkillCount; ++i)
    {
        const int id = AT_SKILL_CURSED_TEMPLE_PRODECTION + i;
        const auto &skill = SkillAttribute[id];
        modernContent_.icons[i] = UI::Modern::RmlSkillIconState::FromSkill(
            id, skill.SkillUseType, skill.Magic_Icon, m_SkillPoint < skill.KillCount);
    }
}
void SEASON3B::CNewUICursedTempleSystem::RenderSkillTooltip()
{
    const int hovered = infoPanel_.HoveredSkill();
    if (hovered < 0)
        return;
    const int id = AT_SKILL_CURSED_TEMPLE_PRODECTION + hovered;
    TextNum = 0;
    mu_swprintf(TextList[TextNum], L"%ls", SkillAttribute[id].Name);
    TextListColor[TextNum++] = TEXT_COLOR_BLUE;
    mu_swprintf(TextList[TextNum], L"%ls", I18N::Game::Lookup(2379 + hovered));
    TextListColor[TextNum++] = TEXT_COLOR_DARKBLUE;
    mu_swprintf(TextList[TextNum], L"%ls: %d", I18N::Game::RequiredKillPoint,
                SkillAttribute[id].KillCount);
    TextListColor[TextNum++] = TEXT_COLOR_WHITE;
    mu_swprintf(TextList[TextNum], L"%ls: %d", I18N::Game::AchievedKillPoint, m_SkillPoint);
    TextListColor[TextNum++] = TEXT_COLOR_WHITE;
    RenderTipTextList(MouseX, MouseY, TextNum, 0);
}
bool SEASON3B::CNewUICursedTempleSystem::PrepareModernUiOnWorker(int width, int height)
{
    return infoPanel_.PrepareOnWorker(width, height, modernVisible_, modernContent_) &&
           scorePanel_.PrepareOnWorker(width, height, modernScoreVisible_,
                                       {m_AlliedPoint, m_IllusionPoint}, teamLabels_);
}

void SEASON3B::CNewUICursedTempleSystem::RenderMiniMap()
{
    float x, y, Width, Height;

    m_Scale = 1.56f;

    EnableAlphaTest();

    glColor4f(1.f, 1.f, 1.f, m_Alph);

    x = 512.f;
    y = 263.f;
    Width = 128.f;
    Height = 128.f;
    RenderBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAP, x, y, Width, Height, 0.f, 0.f, 1.f, 1.f);

    x = 512.f;
    y = 232.f;
    Width = 128.f;
    Height = 165.f;
    RenderBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPFRAME, x, y, Width, Height, 0.f, 0.f,
                 Width / 128.f, Height / 256.f);

    float npc_x = TemplePanelDetail::MiniMapPos(138, 44, m_Scale, TemplePanelDetail::AXIS_X);
    float npc_y = TemplePanelDetail::MiniMapPos(138, 44, m_Scale, TemplePanelDetail::AXIS_Y);
    RenderBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ALLIED_NPC, npc_x, npc_y, 9.0f, 9.0f, 0.f,
                 0.f, 9.f / 16.f, 9.f / 16.f);

    npc_x = TemplePanelDetail::MiniMapPos(138, 58, m_Scale, TemplePanelDetail::AXIS_X);
    npc_y = TemplePanelDetail::MiniMapPos(138, 58, m_Scale, TemplePanelDetail::AXIS_Y);
    RenderBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ALLIED_HOLYITEM, npc_x, npc_y, 9.0f, 8.0f,
                 0.f, 0.f, 9.f / 16.f, 8.f / 8.f);

    npc_x = TemplePanelDetail::MiniMapPos(192, 113, m_Scale, TemplePanelDetail::AXIS_X);
    npc_y = TemplePanelDetail::MiniMapPos(192, 113, m_Scale, TemplePanelDetail::AXIS_Y);
    RenderBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ILLUSION_HOLYITEM, npc_x, npc_y, 9.0f, 8.0f,
                 0.f, 0.f, 9.f / 16.f, 8.f / 8.f);

    npc_x = TemplePanelDetail::MiniMapPos(193, 126, m_Scale, TemplePanelDetail::AXIS_X);
    npc_y = TemplePanelDetail::MiniMapPos(193, 126, m_Scale, TemplePanelDetail::AXIS_Y);
    RenderBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ILLUSION_NPC, npc_x, npc_y, 9.0f, 9.0f, 0.f,
                 0.f, 9.f / 16.f, 9.f / 16.f);

    for (int k = 0; k < m_CursedTempleMyTeamCount; ++k)
    {
        PMSG_CURSED_TAMPLE_PARTY_POS *p = &m_CursedTempleMyTeam[k];

        if (p->wPartyUserIndex == 0xffff)
            continue;

        if (p->wPartyUserIndex != Hero->Key && p->wPartyUserIndex != m_HolyItemPlayerIndex)
        {
            float pcX =
                TemplePanelDetail::MiniMapPos(p->btX, p->btY, m_Scale, TemplePanelDetail::AXIS_X);
            float pcY =
                TemplePanelDetail::MiniMapPos(p->btX, p->btY, m_Scale, TemplePanelDetail::AXIS_Y);

            if (m_MyTeam == SEASON3A::eTeam_Allied)
            {
                RenderBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ALLIED_PC, pcX - 3.f, pcY - 3.f,
                             7.0f, 7.0f, 0.f, 0.f, 7.f / 8.f, 7.f / 8.f);
            }
            else
            {
                RenderBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_ILLUSION_PC, pcX - 3.f, pcY - 3.f,
                             7.0f, 7.0f, 0.f, 0.f, 7.f / 8.f, 7.f / 8.f);
            }
        }
    }

    //  ?? ??
    if (m_HolyItemPlayerIndex != 0xffff && m_HolyItemPlayerIndex != Hero->Key)
    {
        float holypcX = TemplePanelDetail::MiniMapPos(m_HolyItemPlayerPosX, m_HolyItemPlayerPosY,
                                                      m_Scale, TemplePanelDetail::AXIS_X);
        float holypcY = TemplePanelDetail::MiniMapPos(m_HolyItemPlayerPosX, m_HolyItemPlayerPosY,
                                                      m_Scale, TemplePanelDetail::AXIS_Y);
        RenderBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_HOLYITEM_PC, holypcX - 5.f, holypcY - 5.f,
                     14.0f, 14.0f, 0.f, 0.f, 14.f / 16.f, 14.f / 16.f);
    }

    m_Button[CURSEDTEMPLERESULT_ALPH].ChangeAlpha(m_Alph);
    m_Button[CURSEDTEMPLERESULT_ALPH].Render();

    glColor4f(1.f, 1.f, 1.f, m_Alph);

    // ???
    x = (Hero->PositionX);
    y = (Hero->PositionY);
    float hero_x = TemplePanelDetail::MiniMapPos(x, y, m_Scale, TemplePanelDetail::AXIS_X);
    float hero_y = TemplePanelDetail::MiniMapPos(x, y, m_Scale, TemplePanelDetail::AXIS_Y);
    RenderBitmap(IMAGE_CURSEDTEMPLESYSTEM_MINIMAPICON_HERO, hero_x - 4, hero_y - 4, 11.0f, 11.0f,
                 0.f, 0.f, 11.f / 16.f, 11.f / 16.f);

    // ?? ?
    RenderNumber2D(517.f + 15.f, 246.f, static_cast<int>(m_Alph * 100), 8, 8);
    // ??
    RenderNumber2D(517.f + 66.f, 246.f, m_AlliedPoint, 8, 8);
    RenderNumber2D(517.f + 110.f, 246.f, m_IllusionPoint, 8, 8);

    glColor4f(1.f, 1.f, 1.f, 1.f);

    DisableAlphaBlend();

#ifdef _DEBUG
    // ??? ?? ?? ?? ?? ??..? ??...
/*
    for ( int j = 0; j < 7; ++j )
    {
        glColor3f ( 1.f, 0.f, 0.f );
        RenderColor( MiniMapPos( posX[j], posY[j], m_Scale, AXIS_X ),
                     MiniMapPos( posX[j], posY[j], m_Scale, AXIS_Y ), 3, 3 );
        glColor3f ( 1.f, 1.f, 1.f );
    }
    DisableAlphaBlend();
*/
#endif //_DEBUG
}

void SEASON3B::CNewUICursedTempleSystem::RenderTutorialStep()
{
    if (!m_IsTutorialStep)
        return;

    TextNum = 0;
    ZeroMemory(TextListColor, 20 * sizeof(int));
    for (int i = 0; i < 30; i++)
    {
        TextList[i][0] = 0;
    }

    if (m_TutorialStepState == 0)
    {
        wcscpy(TextList[TextNum], I18N::Game::STEP1BattleBegins);
        TextListColor[TextNum] = 0xFF49B0FF;
        ++TextNum;
        mu_swprintf(TextList[TextNum], L"");
        TextListColor[TextNum] = 0xFF000000;
        ++TextNum;
        wcscpy(TextList[TextNum],
               I18N::Game::TheStoneStatueAppearsRandomlyFromOneOfTheTwoLocations);
        TextListColor[TextNum] = 0xFFffffff;
        ++TextNum;
        wcscpy(TextList[TextNum], I18N::Game::TheSacredItemMayBeAchievedByClickingOnTheStoneStatue);
        TextListColor[TextNum] = 0xFFffffff;
        ++TextNum;
        wcscpy(TextList[TextNum], I18N::Game::BeCautiousOfTheFactThat);
        TextListColor[TextNum] = 0xFFffffff;
        ++TextNum;
    }
    else if (m_TutorialStepState == 1)
    {
        wcscpy(TextList[TextNum], I18N::Game::STEP2StorageOfTheSacredItem);
        TextListColor[TextNum] = 0xFF49B0FF;
        ++TextNum;
        mu_swprintf(TextList[TextNum], L"");
        TextListColor[TextNum] = 0xFF000000;
        ++TextNum;
        wcscpy(TextList[TextNum], I18N::Game::ClickOnTheStorageOfThe);
        TextListColor[TextNum] = 0xFFffffff;
        ++TextNum;
        wcscpy(TextList[TextNum],
               I18N::Game::TheGoalIsToAchieveAsManyPointsAsPossibleWithinTheGivenPeriod);
        TextListColor[TextNum] = 0xFFffffff;
        ++TextNum;
        wcscpy(TextList[TextNum],
               I18N::Game::TheStoneStatueReappearsAfterTheStorageLookForTheStatue);
        TextListColor[TextNum] = 0xFFffffff;
        ++TextNum;
    }
    else if (m_TutorialStepState == 2)
    {
        wcscpy(TextList[TextNum], I18N::Game::STEP3OfficialSkills);
        TextListColor[TextNum] = 0xFF49B0FF;
        ++TextNum;
        mu_swprintf(TextList[TextNum], L"");
        TextListColor[TextNum] = 0xFF000000;
        ++TextNum;
        wcscpy(TextList[TextNum], I18N::Game::YouMayAchieveTheKillPoints);
        TextListColor[TextNum] = 0xFFffffff;
        ++TextNum;
        wcscpy(TextList[TextNum],
               I18N::Game::MouseWheelButtonChangeSkillTypesShiftMouseRightClickUse);
        TextListColor[TextNum] = 0xFFffffff;
        ++TextNum;
        wcscpy(TextList[TextNum], I18N::Game::ThereAre4TypesOfSkills);
        TextListColor[TextNum] = 0xFFffffff;
        ++TextNum;
    }

    EnableAlphaTest();
    for (int j = 0; j < TextNum; ++j)
    {
        DrawText(TextList[j], 140, 50 + (j * 14), TextListColor[j], 0x00000000, RT3_SORT_LEFT, 300,
                 false);
    }
    DisableAlphaBlend();
}

bool SEASON3B::CNewUICursedTempleSystem::Render()
{
    if (!gMapManager.IsCursedTemple())
        return true;
    if (modernVisible_)
    {
        RenderMiniMap();
        infoPanel_.Record(renderer_.LegacyRender());
        RenderSkillTooltip();
    }
    scorePanel_.Record(renderer_.LegacyRender());
    RenderTutorialStep();
    return true;
}

bool SEASON3B::CNewUISiegeWarCommander::OnRender()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, m_fMiniMapAlpha);
    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetBgColor(0, 0, 0, 0);
    RenderCharPosInMiniMap();
    RenderGuildMemberPosInMiniMap();
    DisableAlphaBlend();
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, m_fMiniMapAlpha);

    if (m_iCurSelectBtnGroup != -1 && m_iCurSelectBtnCommand != -1 && m_bMouseInMiniMap == true)
    {
        RenderCmdIconAtMouse();
    }

    RenderCmdIconInMiniMap();
    RenderCmdGroupBtn();

    if (m_iCurSelectBtnGroup != -1 && m_iCurSelectBtnCommand == -1)
    {
        RenderCmdBtn();
    }

    DisableAlphaBlend();

    return true;
}

bool SEASON3B::CNewUISiegeWarObserver::OnRender()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, m_fMiniMapAlpha);
    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetBgColor(0, 0, 0, 0);
    RenderCharPosInMiniMap();
    DisableAlphaBlend();
    return true;
}

bool SEASON3B::CNewUISiegeWarSoldier::OnRender()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, m_fMiniMapAlpha);

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetBgColor(0, 0, 0, 0);

    // 모든 캐릭터의 위치
    RenderCharPosInMiniMap();

    DisableAlphaBlend();

    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, m_fMiniMapAlpha);

    // 지도상의 명령 Icon
    RenderCmdIconInMiniMap();

    DisableAlphaBlend();

    return true;
}
