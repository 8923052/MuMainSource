#include "ui/features/QuestNpc/QuestNpcRender.h"
#include "I18N/All.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationNetwork.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "domain/CharacterSystem.h"
#include "domain/ItemsSkills.h"
#include "domain/Quests.h"
#include "domain/Shop.h"
#include "render/Text.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionRender.h"
#include "session/SessionWorkspace.h"
#include "support/CoreMath.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/QuestNpc/QuestNpcLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"
#include "ui/session/UiSessionRender.h"

namespace UI::Modern::PC::NPCs
{
class RmlGatekeeperPanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "NPCs", "gatekeeper.rml")),
          design_(path_, {"Panel-Size", "Panel-Reference", "Panel-Initial"}),
          host_(keeper, "gatekeeper-" + std::to_string(keeper.Id().RawValue()), path_)
    {
    }
    ~Impl()
    {
        Release();
    }
    void Release()
    {
        for (auto &area : areas_)
            area.Unbind();
        close_.Unbind();
        enter_.Unbind();
        confirm_.Unbind();
        stepper_.Unbind();
        checkbox_ = nullptr;
        movable_.Unbind();
        panel_ = nullptr;
        labels_.fill(nullptr);
        management_.fill(nullptr);
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
        close_.Bind(*close);
        constexpr std::array ids{"tfTitle",
                                 "tfEnterLimit",
                                 "taEnterMent",
                                 "cbEnterCheck-label",
                                 "tfEnterFee",
                                 "tfEnterZen",
                                 "tfFeeSetting",
                                 "opZen-label",
                                 "btnSetConfirm-label",
                                 "taEnterFeeMent",
                                 "btnEnter-label",
                                 "btnClose"};
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            labels_[i] = document.GetElementById(ids[i]);
            if (!labels_[i])
                return false;
        }
        auto *enter = document.GetElementById("btnEnter");
        auto *confirm = document.GetElementById("btnSetConfirm");
        auto *stepper = document.GetElementById("opZen");
        auto *previous = document.GetElementById("prevBtn");
        auto *next = document.GetElementById("nextBtn");
        checkbox_ = document.GetElementById("cbEnterCheck-input");
        if (!enter || !confirm || !stepper || !previous || !next || !checkbox_)
            return false;
        enter_.Bind(*enter);
        confirm_.Bind(*confirm);
        stepper_.Bind(*stepper, *previous, *next);
        constexpr std::array management{"cbEnterCheck", "tfFeeSetting", "opZen", "btnSetConfirm",
                                        "taEnterFeeMent"};
        for (std::size_t i = 0; i < management.size(); ++i)
        {
            management_[i] = document.GetElementById(management[i]);
            if (!management_[i])
                return false;
        }
        return areas_[0].Bind(document, "taEnterMent") &&
               areas_[1].Bind(document, "taEnterFeeMent");
    }
    bool ApplyContent(const Content &content)
    {
        if (revision_ == content.revision)
            return false;
        constexpr std::size_t Description = 2, FeeDescription = 9;
        for (std::size_t i = 0; i + 1 < labels_.size(); ++i)
        {
            const auto markup = Rml::StringUtilities::EncodeRml(
                StringUtils::WideToNarrow(content.labels[i].c_str()));
            if (i == Description)
                areas_[0].SetMarkup(markup);
            else if (i == FeeDescription)
                areas_[1].SetMarkup(markup);
            else
                labels_[i]->SetInnerRML(markup);
        }
        observedPublic_ = content.publicAccess;
        if (observedPublic_)
            checkbox_->SetAttribute("checked", "checked");
        else
            checkbox_->RemoveAttribute("checked");
        if (content.canManage)
            checkbox_->RemoveAttribute("disabled");
        else
            checkbox_->SetAttribute("disabled", "disabled");
        for (auto *element : management_)
            element->SetProperty("display", content.canManage ? "block" : "none");
        confirm_.SetEnable(content.canManage);
        enter_.SetEnable(content.canEnter);
        labels_.back()->SetAttribute("title",
                                     StringUtils::WideToNarrow(content.labels.back().c_str()));
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
        dirty = close_.SyncVisualState() || dirty;
        dirty = enter_.SyncVisualState() || dirty;
        dirty = confirm_.SyncVisualState() || dirty;
        dirty = stepper_.Apply(content.feeIndex, content.maximumFeeIndex,
                               visible && content.canManage) ||
                dirty;
        if (dirty)
            host_.Document()->GetContext()->Update();
        for (auto &area : areas_)
            dirty = area.Apply() || dirty;
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
        changes_.close = close_.IsClick() || changes_.close;
        changes_.enter = enter_.IsClick() || changes_.enter;
        changes_.confirm = confirm_.IsClick() || changes_.confirm;
        if (auto index = stepper_.TakeRequestedIndex())
            changes_.feeIndex = index;
        const bool checked = checkbox_->HasAttribute("checked");
        if (checked != observedPublic_)
        {
            changes_.publicAccess = checked;
            observedPublic_ = checked;
        }
        if (inside && event.action == SessionInputAction::PointerButton && event.pressed)
            changes_.focus = true;
        inputDirty_ = true;
        PublishBounds();
        if (event.kind == SessionInputEventKind::Key &&
            (changes_.feeIndex || changes_.publicAccess || changes_.enter || changes_.confirm ||
             changes_.close))
            return true;
        return event.kind == SessionInputEventKind::Pointer &&
               (inside || dragging || movable_.IsDragging());
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    RmlMuMovablePanel movable_;
    RmlMuButton close_, enter_, confirm_;
    RmlMuOptionStepper stepper_;
    Rml::Element *checkbox_ = nullptr;
    bool observedPublic_ = false;
    std::array<RmlMuTextArea, 2> areas_;
    Rml::Element *panel_ = nullptr;
    std::array<Rml::Element *, 12> labels_{};
    std::array<Rml::Element *, 5> management_{};
    std::optional<std::uint64_t> revision_;
    Changes changes_;
    float left_ = 0, top_ = 0, width_ = 0, height_ = 0;
    bool visible_ = false, positioned_ = false, inputDirty_ = false;
};
RmlGatekeeperPanel::RmlGatekeeperPanel(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper))
{
}
RmlGatekeeperPanel::~RmlGatekeeperPanel() = default;
void RmlGatekeeperPanel::Release()
{
    impl_->Release();
}
bool RmlGatekeeperPanel::PrepareOnWorker(int width, int height, bool visible,
                                         const Content &content)
{
    return impl_->Prepare(width, height, visible, content);
}
bool RmlGatekeeperPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
RmlGatekeeperPanel::Changes RmlGatekeeperPanel::TakeChanges()
{
    return std::exchange(impl_->changes_, {});
}
bool RmlGatekeeperPanel::ContainsReferencePointer(int x, int y) const
{
    return impl_->visible_ && x >= impl_->left_ && y >= impl_->top_ &&
           x < impl_->left_ + impl_->width_ && y < impl_->top_ + impl_->height_;
}
bool RmlGatekeeperPanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->panel_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::NPCs

namespace UI::Modern::PC::NPCs
{
namespace
{
constexpr std::size_t ChoicesPerPage = 13; // NpcDialogue.m_iMaxSelectBtn.
enum Navigation
{
    Close,
    Previous,
    Next,
    ChoicePrevious,
    ChoiceNext,
    NavigationCount
};
void SetText(Rml::Element &element, std::wstring_view value)
{
    element.SetInnerRML(
        Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(std::wstring(value).c_str())));
}
} // namespace
class RmlNpcDialoguePanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "NPCs", "npc_dialogue.rml")),
          design_(path_, {"Menu-Size", "Menu-Reference", "Menu-Initial"}),
          host_(keeper, "npc-dialogue-" + std::to_string(keeper.Id().RawValue()), path_)
    {
    }
    ~Impl()
    {
        Release();
    }
    void Release()
    {
        for (auto &button : choices_)
            button.Unbind();
        for (auto &button : navigation_)
            button.Unbind();
        movable_.Unbind();
        menu_ = title_ = dialogue_ = contribution_ = contributionGroup_ = nullptr;
        labels_.fill(nullptr);
        revision_.reset();
        choiceTexts_.clear();
        choicePage_ = 0;
        changes_ = {};
        visible_ = positioned_ = revealed_ = dirty_ = false;
        left_ = top_ = width_ = height_ = 0;
        host_.Release();
    }
    bool Bind()
    {
        auto &document = *host_.Document();
        menu_ = document.GetElementById("menu");
        title_ = document.GetElementById("tfTitle");
        dialogue_ = document.GetElementById("taDialogueMent");
        contribution_ = document.GetElementById("tfContributePoint");
        contributionGroup_ = document.GetElementById("mcContributePoint");
        auto *drag = document.GetElementById("btnDrag");
        if (!menu_ || !title_ || !dialogue_ || !contribution_ || !contributionGroup_ || !drag)
            return false;
        movable_.Bind(*menu_, *drag);
        constexpr std::array ids{"btnClose", "btnPrevious", "btnNext", "btnSelPrevious",
                                 "btnSelNext"};
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            auto *element = document.GetElementById(ids[i]);
            if (!element)
                return false;
            navigation_[i].Bind(*element);
        }
        for (std::size_t i = 0; i < ChoicesPerPage; ++i)
        {
            const auto id = "btnSelect" + std::to_string(i + 1);
            auto *element = document.GetElementById(id);
            labels_[i] = document.GetElementById(id + "-label");
            if (!element || !labels_[i])
                return false;
            choices_[i].Bind(*element);
        }
        return true;
    }
    void ApplyChoices()
    {
        for (std::size_t i = 0; i < ChoicesPerPage; ++i)
        {
            const auto index = choicePage_ * ChoicesPerPage + i;
            SetText(*labels_[i], index < choiceTexts_.size() ? choiceTexts_[index] : L"");
            choices_[i].SetEnable(revealed_ && canChoose_ && index < choiceTexts_.size());
            choices_[i].SetVisible(revealed_);
        }
        navigation_[ChoicePrevious].SetVisible(revealed_ && choicePage_ > 0);
        navigation_[ChoiceNext].SetVisible(revealed_ && (choicePage_ + 1) * ChoicesPerPage <
                                                            choiceTexts_.size());
        dirty_ = true;
    }
    void ApplyContent(const Content &content)
    {
        if (revision_ != content.revision)
        {
            SetText(*title_, content.title);
            SetText(*dialogue_, content.dialogue);
            SetText(*contribution_, content.contribution);
            contributionGroup_->SetProperty("display",
                                            content.contribution.empty() ? "none" : "block");
            choiceTexts_ = content.choices;
            choicePage_ = 0;
            revealed_ = false;
            dialogue_->SetScrollTop(0);
            revision_ = content.revision;
            canChoose_ = content.canChoose;
            for (auto &button : choices_)
                button.Reset();
            ApplyChoices();
        }
        if (canChoose_ != content.canChoose)
        {
            canChoose_ = content.canChoose;
            ApplyChoices();
        }
    }
    void UpdatePaging()
    {
        const auto maximum =
            std::max(0.0f, dialogue_->GetScrollHeight() - dialogue_->GetClientHeight());
        const bool atEnd = dialogue_->GetScrollTop() >= std::round(maximum);
        navigation_[Previous].SetVisible(dialogue_->GetScrollTop() > 0);
        navigation_[Next].SetVisible(!atEnd);
        if (atEnd && !revealed_)
        {
            revealed_ = true;
            ApplyChoices();
        }
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
        if (!menu_ && !visible)
            return true;
        const auto size = design_.Values(0);
        if (!host_.Ensure(width, height, size[0], size[1]))
            return false;
        if (!menu_ && !Bind())
        {
            Release();
            return false;
        }
        if (!host_.SetVisible(visible))
            return false;
        visible_ = visible;
        if (visible)
            ApplyContent(content);
        const auto viewport = host_.Viewport();
        movable_.Configure(viewport.width, viewport.height, size[0], size[1]);
        if (!positioned_)
        {
            const auto initial = design_.Values(2);
            movable_.SetPosition(initial[0], initial[1]);
            positioned_ = true;
        }
        if (!visible)
        {
            movable_.CancelDrag();
            changes_ = {};
        }
        dirty_ = movable_.TakeDirty() || dirty_;
        if (visible && dirty_)
        {
            host_.Document()->GetContext()->Update();
            UpdatePaging();
        }
        for (auto &button : choices_)
            dirty_ = button.SyncVisualState() || dirty_;
        for (auto &button : navigation_)
            dirty_ = button.SyncVisualState() || dirty_;
        PublishBounds();
        if (!host_.CaptureIfDirty(dirty_))
            return false;
        dirty_ = false;
        return true;
    }
    void ProcessButtons()
    {
        changes_.close = navigation_[Close].IsClick() || changes_.close;
        if (navigation_[Previous].IsClick())
            dialogue_->SetScrollTop(dialogue_->GetScrollTop() - dialogue_->GetClientHeight());
        if (navigation_[Next].IsClick())
            dialogue_->SetScrollTop(dialogue_->GetScrollTop() + dialogue_->GetClientHeight());
        if (navigation_[ChoicePrevious].IsClick())
        {
            --choicePage_;
            ApplyChoices();
        }
        if (navigation_[ChoiceNext].IsClick())
        {
            ++choicePage_;
            ApplyChoices();
        }
        for (std::size_t i = 0; i < ChoicesPerPage; ++i)
        {
            if (!choices_[i].IsClick() || changes_.choice)
                continue;
            changes_.choice = choicePage_ * ChoicesPerPage + i;
            changes_.revision = *revision_;
            canChoose_ = false;
            ApplyChoices();
        }
    }
    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!visible_)
            return false;
        const bool dragging = movable_.IsDragging();
        host_.ProcessInput(event);
        ProcessButtons();
        bool inside = false;
        for (auto *element = host_.HoverElement(); element; element = element->GetParentNode())
            if (element == menu_)
            {
                inside = true;
                break;
            }
        if (inside && event.action == SessionInputAction::PointerButton && event.pressed)
            changes_.focus = true;
        dirty_ = true;
        PublishBounds();
        return event.kind == SessionInputEventKind::Pointer &&
               (inside || dragging || movable_.IsDragging());
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    RmlMuMovablePanel movable_;
    std::array<RmlMuButton, NavigationCount> navigation_;
    std::array<RmlMuButton, ChoicesPerPage> choices_;
    std::array<Rml::Element *, ChoicesPerPage> labels_{};
    Rml::Element *menu_ = nullptr;
    Rml::Element *title_ = nullptr;
    Rml::Element *dialogue_ = nullptr;
    Rml::Element *contribution_ = nullptr;
    Rml::Element *contributionGroup_ = nullptr;
    std::optional<std::uint64_t> revision_;
    std::vector<std::wstring> choiceTexts_;
    std::size_t choicePage_ = 0;
    Changes changes_;
    bool visible_ = false, positioned_ = false, revealed_ = false, dirty_ = false,
         canChoose_ = true;
    float left_ = 0, top_ = 0, width_ = 0, height_ = 0;
};
RmlNpcDialoguePanel::RmlNpcDialoguePanel(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper))
{
}
RmlNpcDialoguePanel::~RmlNpcDialoguePanel() = default;
void RmlNpcDialoguePanel::Release()
{
    impl_->Release();
}
bool RmlNpcDialoguePanel::PrepareOnWorker(int width, int height, bool visible,
                                          const Content &content)
{
    return impl_->Prepare(width, height, visible, content);
}
bool RmlNpcDialoguePanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
RmlNpcDialoguePanel::Changes RmlNpcDialoguePanel::TakeChanges()
{
    return std::exchange(impl_->changes_, {});
}
bool RmlNpcDialoguePanel::ContainsReferencePointer(int x, int y) const
{
    return impl_->visible_ && x >= impl_->left_ && y >= impl_->top_ &&
           x < impl_->left_ + impl_->width_ && y < impl_->top_ + impl_->height_;
}
bool RmlNpcDialoguePanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->menu_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::NPCs

namespace UI::Modern::PC::Quests
{
std::string QuestRowsMarkup(const std::vector<QuestTextRow> &rows)
{
    std::string markup;
    for (const auto &row : rows)
        markup += "<div class=\"quest-row " + row.style + "\" data-reward=\"" +
                  std::to_string(row.sourceIndex) + "\">" +
                  Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(row.text.c_str())) +
                  "</div>";
    return markup;
}
} // namespace UI::Modern::PC::Quests

namespace UI::Modern::PC::Quests
{
namespace
{
constexpr std::array JobChangePanelButtonIds{
    "btnClose",       "btnSelect1",      "btnSelect2",          "btnSelect3",
    "btnCompleteMob", "btnCompleteItem", "btnCompleteItemPiece"};
constexpr std::array JobChangePanelGroupIds{"mcRequireZen", "mcRequireMob", "mcRequireItem",
                                            "mcRequireItem3Piece"};
constexpr std::array SlotIds{"isItem", "isItemPiece1", "isItemPiece2", "isItemPiece3"};
constexpr std::array RequirementIds{"tfMob1",       "tfMob2",       "tfMob3",      "tfItemName",
                                    "tfItemPiece1", "tfItemPiece2", "tfItemPiece3"};
} // namespace
class RmlJobChangePanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "Quests", "npc_job_change.rml")),
          design_(path_, {"Panel-Size", "Panel-Reference", "Panel-Initial", "Legacy-ItemHeight"}),
          host_(keeper, "npc-job-change-" + std::to_string(keeper.Id().RawValue()), path_)
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
        for (auto &slot : slots_)
            slot.Unbind();
        movable_.Unbind();
        panel_ = nullptr;
        slotElements_.fill(nullptr);
        revision_.reset();
        changes_ = {};
        itemRects_ = {};
        bounds_ = {};
        visible_ = positioned_ = dirty_ = false;
        host_.Release();
    }
    bool Bind()
    {
        auto &document = *host_.Document();
        panel_ = document.GetElementById("panel");
        auto *drag = document.GetElementById("btnDrag");
        if (!panel_ || !drag)
            return false;
        movable_.Bind(*panel_, *drag);
        for (std::size_t i = 0; i < JobChangePanelButtonIds.size(); ++i)
        {
            auto *element = document.GetElementById(JobChangePanelButtonIds[i]);
            if (!element)
                return false;
            buttons_[i].Bind(*element);
        }
        for (std::size_t i = 0; i < SlotIds.size(); ++i)
        {
            slotElements_[i] = document.GetElementById(SlotIds[i]);
            if (!slotElements_[i])
                return false;
            slots_[i].Bind(*slotElements_[i]);
            slots_[i].SetEnable(false);
        }
        return true;
    }
    void Text(const char *id, std::wstring_view text)
    {
        host_.Document()->GetElementById(id)->SetInnerRML(
            Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(std::wstring(text).c_str())));
    }
    void Requirements(const Content &content)
    {
        for (const auto *id : RequirementIds)
            Text(id, L"");
        const std::size_t first = content.mode == Mode::Monsters ? 0
                                  : content.mode == Mode::Item   ? 3
                                                                 : 4;
        for (std::size_t i = 0; i < content.requirements.size(); ++i)
        {
            const auto *id = RequirementIds[first + i];
            Text(id, content.requirements[i].text);
            auto *element = host_.Document()->GetElementById(id);
            element->SetClass("requirement-complete", content.requirements[i].complete);
            element->SetClass("requirement-missing", !content.requirements[i].complete);
        }
    }
    void Apply(std::uint64_t revision, const Content &content)
    {
        if (revision_ == revision)
            return;
        changes_.choice.reset();
        changes_.complete = false;
        const bool newDialogue = dialogue_ != content.dialogue;
        dialogue_ = content.dialogue;
        Text("tfTitle", content.title);
        Text("tfQuestTitle", content.questTitle);
        Text("taQuestMent", content.dialogue);
        if (newDialogue)
            host_.Document()->GetElementById("taQuestMent")->SetScrollTop(0);
        Text("tfRequireZen", content.costLabel);
        Text("tfRequireZenValue", content.cost);
        for (std::size_t i = 0; i < 3; ++i)
        {
            Text((std::string(JobChangePanelButtonIds[i + 1]) + "-label").c_str(),
                 i < content.choices.size() ? content.choices[i] : L"");
            buttons_[i + 1].SetVisible(i < content.choices.size());
            buttons_[i + 1].SetEnable(content.canChoose);
        }
        for (std::size_t i = 4; i < JobChangePanelButtonIds.size(); ++i)
        {
            Text((std::string(JobChangePanelButtonIds[i]) + "-label").c_str(),
                 content.completeLabel);
            buttons_[i].SetEnable(content.complete && content.canChoose);
        }
        mode_ = content.mode;
        for (std::size_t i = 0; i < JobChangePanelGroupIds.size(); ++i)
            host_.Document()
                ->GetElementById(JobChangePanelGroupIds[i])
                ->SetClass("mu-hidden", static_cast<int>(mode_) != i + 1);
        Requirements(content);
        for (auto &button : buttons_)
            button.Reset();
        revision_ = revision;
        dirty_ = true;
    }
    Rect ReferenceRect(Rml::Element &element) const
    {
        const auto offset = element.GetAbsoluteOffset(), size = element.GetBox().GetSize();
        const auto reference = design_.Values(1);
        const auto viewport = host_.Viewport();
        return {offset.x * reference[0] / viewport.width, offset.y * reference[1] / viewport.height,
                size.x * reference[0] / viewport.width, size.y * reference[1] / viewport.height};
    }
    bool Prepare(int width, int height, bool visible, std::uint64_t revision,
                 const Content &content)
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
        if (visible)
            Apply(revision, content);
        else
        {
            changes_ = {};
            movable_.CancelDrag();
            for (auto &button : buttons_)
                button.Reset();
        }
        const auto viewport = host_.Viewport();
        movable_.Configure(viewport.width, viewport.height, size[0], size[1]);
        if (!positioned_)
        {
            const auto initial = design_.Values(2);
            movable_.SetPosition(initial[0], initial[1]);
            positioned_ = true;
        }
        dirty_ = movable_.TakeDirty() || dirty_;
        for (auto &button : buttons_)
            dirty_ = button.SyncVisualState() || dirty_;
        for (auto &slot : slots_)
            dirty_ = slot.SyncVisualState() || dirty_;
        if (!host_.CaptureIfDirty(dirty_))
            return false;
        bounds_ = ReferenceRect(*panel_);
        itemRects_ = {};
        if (visible && mode_ == Mode::Item)
            itemRects_[0] = ReferenceRect(*slotElements_[0]);
        if (visible && mode_ == Mode::Items)
            for (std::size_t i = 0; i < itemRects_.size(); ++i)
                itemRects_[i] = ReferenceRect(*slotElements_[i + 1]);
        dirty_ = false;
        return true;
    }
    void Buttons()
    {
        changes_.close = buttons_[0].IsClick() || changes_.close;
        for (std::size_t i = 1; i < 4; ++i)
            if (buttons_[i].IsClick())
                changes_.choice = i - 1;
        for (std::size_t i = 4; i < buttons_.size(); ++i)
            changes_.complete = buttons_[i].IsClick() || changes_.complete;
        if (!changes_.choice && !changes_.complete)
            return;
        changes_.revision = *revision_;
        for (std::size_t i = 1; i < buttons_.size(); ++i)
            buttons_[i].SetEnable(false);
    }
    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!visible_)
            return false;
        const bool dragging = movable_.IsDragging();
        host_.ProcessInput(event);
        Buttons();
        bool inside = false;
        for (auto *element = host_.HoverElement(); element; element = element->GetParentNode())
            if (element == panel_)
            {
                inside = true;
                break;
            }
        if (inside && event.action == SessionInputAction::PointerButton && event.pressed)
            changes_.focus = true;
        dirty_ = true;
        return event.kind == SessionInputEventKind::Pointer &&
               (inside || dragging || movable_.IsDragging());
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    RmlMuMovablePanel movable_;
    Rml::Element *panel_ = nullptr;
    std::array<RmlMuButton, JobChangePanelButtonIds.size()> buttons_;
    std::array<RmlMuSlot, SlotIds.size()> slots_;
    std::array<Rml::Element *, SlotIds.size()> slotElements_{};
    std::array<Rect, 3> itemRects_{};
    Rect bounds_{};
    Mode mode_ = Mode::None;
    std::optional<std::uint64_t> revision_;
    std::wstring dialogue_;
    Changes changes_;
    bool visible_ = false, positioned_ = false, dirty_ = false;
};
RmlJobChangePanel::RmlJobChangePanel(SessionKeeper &keeper) : impl_(std::make_unique<Impl>(keeper))
{
}
RmlJobChangePanel::~RmlJobChangePanel() = default;
void RmlJobChangePanel::Release()
{
    impl_->Release();
}
bool RmlJobChangePanel::PrepareOnWorker(int width, int height, bool visible, std::uint64_t revision,
                                        const Content &content)
{
    return impl_->Prepare(width, height, visible, revision, content);
}
bool RmlJobChangePanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
RmlJobChangePanel::Changes RmlJobChangePanel::TakeChanges()
{
    return std::exchange(impl_->changes_, {});
}
bool RmlJobChangePanel::ContainsReferencePointer(int x, int y) const
{
    const auto &rect = impl_->bounds_;
    return impl_->visible_ && x >= rect.x && y >= rect.y && x < rect.x + rect.width &&
           y < rect.y + rect.height;
}
const std::array<RmlJobChangePanel::Rect, 3> &RmlJobChangePanel::ItemRects() const
{
    return impl_->itemRects_;
}
float RmlJobChangePanel::ItemPresentationScale() const
{
    return impl_->itemRects_[0].height / impl_->design_.Values(3)[0];
}
bool RmlJobChangePanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->panel_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::Quests

namespace UI::Modern::PC::Quests
{
namespace
{
constexpr std::array QuestJournalPanelButtonIds{
    "btnClose", "btnMyQuest", "btnJobChange", "btnCastleTemple", "btnQuestOpen", "btnQuestGiveUp"};
constexpr std::array QuestJournalPanelGroupIds{"mcMyQuest", "mcJobChange", "mcCastleTemple"};
std::string Encode(std::wstring_view text)
{
    return Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(std::wstring(text).c_str()));
}
} // namespace
class RmlQuestJournalPanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "Quests", "quest_journal.rml")),
          design_(path_, {"Panel-Size", "Panel-Reference", "Panel-Initial"}),
          host_(keeper, "quest-journal-" + std::to_string(keeper.Id().RawValue()), path_)
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
        for (auto &text : texts_)
            text.Unbind();
        list_.Unbind();
        movable_.Unbind();
        panel_ = nullptr;
        revision_.reset();
        hoveredReward_.reset();
        selected_.reset();
        changes_ = {};
        visible_ = positioned_ = dirty_ = false;
        host_.Release();
    }
    bool Bind()
    {
        auto &document = *host_.Document();
        panel_ = document.GetElementById("panel");
        auto *drag = document.GetElementById("btnDrag");
        if (!panel_ || !drag)
            return false;
        movable_.Bind(*panel_, *drag);
        for (std::size_t i = 0; i < QuestJournalPanelButtonIds.size(); ++i)
        {
            auto *element = document.GetElementById(QuestJournalPanelButtonIds[i]);
            if (!element)
                return false;
            buttons_[i].Bind(*element);
        }
        return list_.Bind(document, "scrollingMyQuest", "sbMyQuest", "quest-row-template") &&
               texts_[0].Bind(document, "taQuestMent", "sbQuestMent") &&
               texts_[1].Bind(document, "taConditionsValue", "sbConditions") &&
               texts_[2].Bind(document, "taRewardsValue", "sbRewards") &&
               texts_[3].Bind(document, "taJobQuestMent") &&
               texts_[4].Bind(document, "taQuestState") && texts_[5].Bind(document, "taCastle") &&
               texts_[6].Bind(document, "taTemple");
    }
    void Text(const char *id, std::wstring_view value)
    {
        host_.Document()->GetElementById(id)->SetInnerRML(Encode(value));
    }
    void Labels(const Content &content)
    {
        Text("tfTitle", content.title);
        Text("btnMyQuest-label", content.questLabel);
        Text("btnJobChange-label", content.jobLabel);
        Text("btnCastleTemple-label", content.eventsLabel);
        Text("btnQuestOpen-label", content.startLabel);
        Text("btnQuestGiveUp-label", content.giveUpLabel);
        Text("tfConditions", content.requirementsLabel);
        Text("tfRewards", content.rewardsLabel);
        Text("tfQuestTitle", content.jobTitle);
        Text("tfBloodCastle", content.castleLabel);
        Text("tfTemple", content.templeLabel);
    }
    void Apply(std::uint64_t revision, const Content &content)
    {
        if (revision_ == revision)
            return;
        changes_.selected.reset();
        changes_.start = changes_.giveUp = false;
        Labels(content);
        RmlMuScrollingList::Data rows;
        for (const auto &quest : content.quests)
            rows.push_back({quest});
        list_.SetData(rows);
        list_.Select(content.selected);
        const bool changedSelection = selected_ != content.selected;
        texts_[0].SetMarkup(Encode(content.summary), changedSelection);
        texts_[1].SetMarkup(QuestRowsMarkup(content.requirements), changedSelection);
        texts_[2].SetMarkup(QuestRowsMarkup(content.rewards), changedSelection);
        texts_[3].SetMarkup(Encode(content.jobDialogue));
        texts_[4].SetMarkup(Encode(content.jobState));
        texts_[5].SetMarkup(Encode(content.castle));
        texts_[6].SetMarkup(Encode(content.temple));
        for (std::size_t i = 0; i < QuestJournalPanelGroupIds.size(); ++i)
            host_.Document()
                ->GetElementById(QuestJournalPanelGroupIds[i])
                ->SetClass("mu-hidden", int(content.tab) != i);
        buttons_[4].SetEnable(content.canStart);
        buttons_[5].SetEnable(content.canGiveUp);
        for (auto &button : buttons_)
            button.Reset();
        selected_ = content.selected;
        revision_ = revision;
        hoveredReward_.reset();
        dirty_ = true;
    }
    bool Prepare(int width, int height, bool visible, std::uint64_t revision,
                 const Content &content)
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
        if (visible)
            Apply(revision, content);
        else
        {
            changes_ = {};
            hoveredReward_.reset();
            movable_.CancelDrag();
            for (auto &button : buttons_)
                button.Reset();
        }
        const auto viewport = host_.Viewport();
        movable_.Configure(viewport.width, viewport.height, size[0], size[1]);
        if (!positioned_)
        {
            const auto initial = design_.Values(2);
            movable_.SetPosition(initial[0], initial[1]);
            positioned_ = true;
        }
        dirty_ = movable_.TakeDirty() || dirty_;
        if (visible && dirty_)
        {
            host_.Document()->GetContext()->Update();
            list_.Apply();
            for (auto &text : texts_)
                text.Apply();
        }
        for (auto &button : buttons_)
            dirty_ = button.SyncVisualState() || dirty_;
        if (!host_.CaptureIfDirty(dirty_))
            return false;
        const auto reference = design_.Values(1);
        const auto position = movable_.Position();
        bounds_ = {position.left * reference[0] / viewport.width,
                   position.top * reference[1] / viewport.height,
                   size[0] * reference[0] / viewport.width,
                   size[1] * reference[1] / viewport.height};
        dirty_ = false;
        return true;
    }
    void Buttons()
    {
        changes_.close = buttons_[0].IsClick() || changes_.close;
        for (std::size_t i = 1; i <= 3; ++i)
            if (buttons_[i].IsClick())
                changes_.tab = Tab(i - 1);
        changes_.start = buttons_[4].IsClick() || changes_.start;
        changes_.giveUp = buttons_[5].IsClick() || changes_.giveUp;
        if (auto selected = list_.TakeSelection())
            changes_.selected = selected;
        changes_.revision = *revision_;
    }
    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!visible_)
            return false;
        bool dragging = movable_.IsDragging() || list_.IsDragging();
        for (const auto &text : texts_)
            dragging = text.IsDragging() || dragging;
        host_.ProcessInput(event);
        list_.ProcessInput(event, host_.HoverElement());
        Buttons();
        for (auto &text : texts_)
            text.Apply();
        bool inside = false;
        hoveredReward_.reset();
        for (auto *element = host_.HoverElement(); element; element = element->GetParentNode())
        {
            if (element->HasAttribute("data-reward"))
                hoveredReward_ = element->GetAttribute<std::size_t>("data-reward", 0);
            if (element == panel_)
            {
                inside = true;
                break;
            }
        }
        hoverRevision_ = *revision_;
        if (inside && event.action == SessionInputAction::PointerButton && event.pressed)
            changes_.focus = true;
        dirty_ = true;
        return event.kind == SessionInputEventKind::Pointer &&
               (inside || dragging || movable_.IsDragging());
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    RmlMuMovablePanel movable_;
    std::array<RmlMuButton, QuestJournalPanelButtonIds.size()> buttons_;
    std::array<RmlMuTextArea, 7> texts_;
    RmlMuScrollingList list_;
    Rml::Element *panel_ = nullptr;
    std::array<float, 4> bounds_{};
    std::optional<std::uint64_t> revision_;
    std::optional<std::size_t> hoveredReward_, selected_;
    std::uint64_t hoverRevision_ = 0;
    Changes changes_;
    bool visible_ = false, positioned_ = false, dirty_ = false;
};
RmlQuestJournalPanel::RmlQuestJournalPanel(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper))
{
}
RmlQuestJournalPanel::~RmlQuestJournalPanel() = default;
void RmlQuestJournalPanel::Release()
{
    impl_->Release();
}
bool RmlQuestJournalPanel::PrepareOnWorker(int width, int height, bool visible,
                                           std::uint64_t revision, const Content &content)
{
    return impl_->Prepare(width, height, visible, revision, content);
}
bool RmlQuestJournalPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
RmlQuestJournalPanel::Changes RmlQuestJournalPanel::TakeChanges()
{
    return std::exchange(impl_->changes_, {});
}
std::optional<std::size_t> RmlQuestJournalPanel::HoveredReward(std::uint64_t revision) const
{
    return revision == impl_->hoverRevision_ ? impl_->hoveredReward_ : std::nullopt;
}
bool RmlQuestJournalPanel::ContainsReferencePointer(int x, int y) const
{
    const auto &rect = impl_->bounds_;
    return impl_->visible_ && x >= rect[0] && y >= rect[1] && x < rect[0] + rect[2] &&
           y < rect[1] + rect[3];
}
bool RmlQuestJournalPanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->panel_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::Quests

namespace UI::Modern::PC::Quests
{
namespace
{
constexpr std::array QuestProgressPanelButtonIds{"btnClose",   "btnPrevious", "btnNext",
                                                 "btnConfirm", "btnSelect1",  "btnSelect2",
                                                 "btnSelect3", "btnSelect4",  "btnSelect5"};
enum Button
{
    Close,
    Previous,
    Next,
    Confirm,
    FirstChoice
};

} // namespace
class RmlQuestProgressPanel::Impl final
{
  public:
    Impl(SessionKeeper &keeper, bool byItem)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "Quests", "npc_quest_progress.rml")),
          design_(path_, {"Panel-Size", "Panel-Reference", "Panel-Initial"}),
          host_(keeper,
                std::string(byItem ? "quest-progress-item-" : "quest-progress-npc-") +
                    std::to_string(keeper.Id().RawValue()),
                path_)
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
        for (auto &text : textAreas_)
            text.Unbind();
        movable_.Unbind();
        panel_ = nullptr;
        revision_.reset();
        branch_.reset();
        hoveredReward_.reset();
        changes_ = {};
        content_ = {};
        visible_ = positioned_ = revealed_ = dirty_ = false;
        host_.Release();
    }
    bool Bind()
    {
        auto &document = *host_.Document();
        panel_ = document.GetElementById("panel");
        auto *drag = document.GetElementById("btnDrag");
        if (!panel_ || !drag)
            return false;
        movable_.Bind(*panel_, *drag);
        for (std::size_t i = 0; i < QuestProgressPanelButtonIds.size(); ++i)
        {
            auto *element = document.GetElementById(QuestProgressPanelButtonIds[i]);
            if (!element)
                return false;
            buttons_[i].Bind(*element);
        }
        return textAreas_[0].Bind(document, "taQuestMent") &&
               textAreas_[1].Bind(document, "taConditions", "sbCondition") &&
               textAreas_[2].Bind(document, "taReward", "sbReward") &&
               textAreas_[3].Bind(document, "taCharMent");
    }
    void Text(const char *id, std::wstring_view text)
    {
        host_.Document()->GetElementById(id)->SetInnerRML(Encode(text));
    }
    void Apply(std::uint64_t revision, std::uint64_t branch, const Content &content)
    {
        if (revision_ == revision)
            return;
        hoveredReward_.reset();
        changes_.choice.reset();
        changes_.complete = false;
        if (branch_ != branch)
        {
            revealed_ = content.choices.empty();
            hoveredReward_.reset();
            host_.Document()->GetElementById("taQuestMent")->SetScrollTop(0);
        }
        Text("tfTitle", content.title);
        Text("tfQuestTitle", content.questTitle);
        Text("tfNpcName", content.npc);
        Text("tfCharName", content.player);
        Text("tfCondition", content.requirementsTitle);
        Text("tfReward", content.rewardsTitle);
        Text("btnConfirm-label", content.confirmLabel);
        textAreas_[0].SetMarkup(Encode(content.dialogue), branch_ != branch);
        textAreas_[1].SetMarkup(QuestRowsMarkup(content.requirements), branch_ != branch);
        textAreas_[2].SetMarkup(QuestRowsMarkup(content.rewards), branch_ != branch);
        textAreas_[3].SetMarkup(Encode(content.playerWords), branch_ != branch);
        for (std::size_t i = 0; i < QuestProgressPanelButtonIds.size() - FirstChoice; ++i)
            Text((std::string(QuestProgressPanelButtonIds[FirstChoice + i]) + "-label").c_str(),
                 i < content.choices.size() ? content.choices[i] : L"");
        host_.Document()
            ->GetElementById("mcNpcQuestState")
            ->SetClass("mu-hidden", !content.choices.empty());
        content_ = content;
        revision_ = revision;
        branch_ = branch;
        for (auto &button : buttons_)
            button.Reset();
        ApplyButtons();
        dirty_ = true;
    }
    void ApplyButtons()
    {
        host_.Document()
            ->GetElementById("mcNpcTalk")
            ->SetClass("mu-hidden", !revealed_ || content_.choices.empty());
        for (std::size_t i = 0; i < QuestProgressPanelButtonIds.size() - FirstChoice; ++i)
        {
            buttons_[FirstChoice + i].SetVisible(i < content_.choices.size());
            buttons_[FirstChoice + i].SetEnable(content_.canChoose);
        }
        buttons_[Confirm].SetEnable(content_.complete && content_.canChoose);
    }
    void Paging()
    {
        buttons_[Previous].SetVisible(!textAreas_[0].AtStart());
        buttons_[Next].SetVisible(!textAreas_[0].AtEnd() || !revealed_);
    }
    bool Prepare(int width, int height, bool visible, std::uint64_t revision, std::uint64_t branch,
                 const Content &content)
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
        if (visible)
            Apply(revision, branch, content);
        else
        {
            changes_ = {};
            hoveredReward_.reset();
            movable_.CancelDrag();
            for (auto &button : buttons_)
                button.Reset();
        }
        const auto viewport = host_.Viewport();
        movable_.Configure(viewport.width, viewport.height, size[0], size[1]);
        if (!positioned_)
        {
            const auto initial = design_.Values(2);
            movable_.SetPosition(initial[0], initial[1]);
            positioned_ = true;
        }
        dirty_ = movable_.TakeDirty() || dirty_;
        if (visible && dirty_)
        {
            host_.Document()->GetContext()->Update();
            for (auto &text : textAreas_)
                text.Apply();
            Paging();
        }
        for (auto &button : buttons_)
            dirty_ = button.SyncVisualState() || dirty_;
        if (!host_.CaptureIfDirty(dirty_))
            return false;
        const auto reference = design_.Values(1);
        const auto position = movable_.Position();
        bounds_ = {position.left * reference[0] / viewport.width,
                   position.top * reference[1] / viewport.height,
                   size[0] * reference[0] / viewport.width,
                   size[1] * reference[1] / viewport.height};
        dirty_ = false;
        return true;
    }
    void Buttons()
    {
        changes_.close = buttons_[Close].IsClick() || changes_.close;
        if (buttons_[Previous].IsClick())
            textAreas_[0].ScrollPage(-1);
        if (buttons_[Next].IsClick())
        {
            if (textAreas_[0].AtEnd())
            {
                revealed_ = true;
                ApplyButtons();
            }
            else
                textAreas_[0].ScrollPage(1);
        }
        changes_.complete = buttons_[Confirm].IsClick() || changes_.complete;
        for (std::size_t i = FirstChoice; i < buttons_.size(); ++i)
            if (buttons_[i].IsClick())
                changes_.choice = i - FirstChoice;
        if (!changes_.choice && !changes_.complete)
            return;
        changes_.revision = *revision_;
        content_.canChoose = false;
        ApplyButtons();
    }
    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!visible_)
            return false;
        bool dragging = movable_.IsDragging();
        for (const auto &text : textAreas_)
            dragging = text.IsDragging() || dragging;
        host_.ProcessInput(event);
        Buttons();
        for (auto &text : textAreas_)
            text.Apply();
        bool inside = false;
        hoveredReward_.reset();
        for (auto *element = host_.HoverElement(); element; element = element->GetParentNode())
        {
            if (element->HasAttribute("data-reward"))
                hoveredReward_ = element->GetAttribute<std::size_t>("data-reward", 0);
            if (element == panel_)
            {
                inside = true;
                break;
            }
        }
        hoverRevision_ = *revision_;
        if (inside && event.action == SessionInputAction::PointerButton && event.pressed)
            changes_.focus = true;
        dirty_ = true;
        return event.kind == SessionInputEventKind::Pointer &&
               (inside || dragging || movable_.IsDragging());
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    RmlMuMovablePanel movable_;
    std::array<RmlMuButton, QuestProgressPanelButtonIds.size()> buttons_;
    std::array<RmlMuTextArea, 4> textAreas_;
    Rml::Element *panel_ = nullptr;
    std::array<float, 4> bounds_{};
    Content content_;
    std::optional<std::uint64_t> revision_, branch_;
    std::uint64_t hoverRevision_ = 0;
    std::optional<std::size_t> hoveredReward_;
    Changes changes_;
    bool visible_ = false, positioned_ = false, revealed_ = false, dirty_ = false;
};
RmlQuestProgressPanel::RmlQuestProgressPanel(SessionKeeper &keeper, bool byItem)
    : impl_(std::make_unique<Impl>(keeper, byItem))
{
}
RmlQuestProgressPanel::~RmlQuestProgressPanel() = default;
void RmlQuestProgressPanel::Release()
{
    impl_->Release();
}
bool RmlQuestProgressPanel::PrepareOnWorker(int width, int height, bool visible,
                                            std::uint64_t revision, std::uint64_t branch,
                                            const Content &content)
{
    return impl_->Prepare(width, height, visible, revision, branch, content);
}
bool RmlQuestProgressPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
RmlQuestProgressPanel::Changes RmlQuestProgressPanel::TakeChanges()
{
    return std::exchange(impl_->changes_, {});
}
std::optional<std::size_t> RmlQuestProgressPanel::HoveredReward(std::uint64_t revision) const
{
    return revision == impl_->hoverRevision_ ? impl_->hoveredReward_ : std::nullopt;
}
bool RmlQuestProgressPanel::ContainsReferencePointer(int x, int y) const
{
    const auto &rect = impl_->bounds_;
    return impl_->visible_ && x >= rect[0] && y >= rect[1] && x < rect[0] + rect[2] &&
           y < rect[1] + rect[3];
}
bool RmlQuestProgressPanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->panel_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::Quests

namespace SEASON3B
{
namespace
{
const char *RewardStyle(const SRequestRewardText &row)
{
    if (row.m_fontRole == LegacyFontRole::Bold)
        return "quest-heading";
    if (row.m_dwColor == ARGB(255, 255, 30, 30))
        return "quest-missing";
    if (row.m_dwColor == ARGB(255, 103, 103, 223))
        return "quest-random";
    return "quest-normal";
}
} // namespace
QuestRewardPresentation BuildQuestRewardPresentation(CQuestMng &quests, std::uint32_t index)
{
    QuestRewardPresentation result;
    const auto *request = quests.GetRequestReward(index);
    if (!request)
        return result;
    std::array<SRequestRewardText, QuestRewardPresentation::MaximumLines> rows{};
    result.complete = quests.GetRequestRewardText(rows.data(), int(rows.size()), index);
    std::size_t end = 1 + request->m_byRequestCount;
    if (request->m_byGeneralRewardCount)
        end += 1 + request->m_byGeneralRewardCount;
    if (request->m_byRandRewardCount)
        end += 1 + request->m_byRandRewardCount;
    for (std::size_t i = 1; i < end; ++i)
    {
        const auto &row = rows[i];
        auto &target = i <= request->m_byRequestCount ? result.requirements : result.rewards;
        target.push_back({row.m_szText, RewardStyle(row), i});
        if (row.m_dwType == QUEST_REQUEST_ITEM || row.m_dwType == QUEST_REWARD_ITEM)
            result.items[i] = row.m_pItem;
    }
    return result;
}
} // namespace SEASON3B

using namespace SEASON3B;

void CNewUIEmpireGuardianNPC::SetPos(int, int)
{
}
void CNewUIEmpireGuardianNPC::StageContent()
{
    const char *locale = I18N::GetCurrentLocale();
    if (locale_ == locale)
        return;
    locale_ = locale;
    panel_.SetText("tfTitle", I18N::Game::JerintTheAssistant);
    panel_.SetText("taGuardianMent", std::wstring(I18N::Game::WithoutGaionSOrder) + L"\n" +
                                         I18N::Game::YouCannotEnterTheFortressOfEmpireGuardians +
                                         L"\n" + I18N::Game::WillYouShowMeTheOrder);
    panel_.SetText("tfItemName", I18N::Game::GaionSOrder);
    panel_.SetText("btnEnter-label", I18N::Game::Enter);
    panel_.SetText("tfCaution", I18N::Game::Warning2223);
    panel_.SetText("taCautionMent", std::wstring(I18N::Game::TheRound7MapSundayCanOnly) + L"\n" +
                                        I18N::Game::BeAccessedIfYouHaveA + L"\n" +
                                        I18N::Game::CompleteSecromicon2837 + L"\n" +
                                        I18N::Game::YouCanOnlyEnterAsAMemberOfAParty);
}

bool CNewUIEmpireGuardianNPC::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}
void CNewUIEmpireGuardianNPC::Render3D()
{
    const auto slot = panel_.SlotBounds("isItem");
    if (slot.width == 0 || slot.height == 0)
        return;
    SessionUiLegacyBindings::RenderItem3D(slot.x, slot.y, slot.width, slot.height,
                                          ITEM_GAIONS_ORDER, 0, 0, 0, false);
}

bool CNewUIEmpireGuardianNPC::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_);
}

void CNewUIEmpireGuardianTimer::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool CNewUIEmpireGuardianTimer::Render()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    RenderImage(IMAGE_EMPIREGUARDIAN_TIMER_WINDOW, m_Pos.x, m_Pos.y, float(TIMER_WINDOW_WIDTH),
                float(TIMER_WINDOW_HEIGHT));

    wchar_t szText[256] = {};
    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.SetBgColor(0);

    mu_swprintf(szText, I18N::Game::RoundDZoneD, m_iDay, m_iZone);
    g_RenderText.RenderText(m_Pos.x + (TIMER_WINDOW_WIDTH / 2) - 55, m_Pos.y + 13, szText, 110, 0,
                            RT3_SORT_CENTER);

    switch (m_iType)
    {
    case 0:
    case 1:
        g_RenderText.SetTextColor(10, 200, 10, 255);
        g_RenderText.RenderText(m_Pos.x + (TIMER_WINDOW_WIDTH / 2) - 55, m_Pos.y + 38,
                                I18N::Game::StandbyTime, 110, 0, RT3_SORT_CENTER);
        break;
    case 2:
        g_RenderText.SetTextColor(255, 150, 0, 255);
        mu_swprintf(szText, L"%ls (%ls)", I18N::Game::TimeLeft, I18N::Game::RemainingMonsters);
        g_RenderText.RenderText(m_Pos.x + (TIMER_WINDOW_WIDTH / 2) - 55, m_Pos.y + 38, szText, 110,
                                0, RT3_SORT_CENTER);
        break;
    }

    int iSecond = m_dTime / 1000;
    int iMinute = iSecond / 60;

    if (2 < iMinute)
    {
        g_RenderText.SetTextColor(255, 150, 0, 255);
    }
    else if (0 < iMinute && iMinute <= 2)
    {
        g_RenderText.SetTextColor(255, 70, 0, 255);
    }
    else if (iMinute == 0)
    {
        g_RenderText.SetTextColor(255, 0, 0, 255);
    }

    mu_swprintf(szText, L"%.2d:%.2d(%d)", iMinute, iSecond % 60, m_iMonsterCount);
    g_RenderText.SetFont(LegacyFontRole::Large);
    g_RenderText.RenderText(m_Pos.x + (TIMER_WINDOW_WIDTH / 2) - 55, m_Pos.y + 50, szText, 110, 0,
                            RT3_SORT_CENTER);

    DisableAlphaBlend();

    return true;
}

void CNewUIEmpireGuardianTimer::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_Figure_blood.tga", IMAGE_EMPIREGUARDIAN_TIMER_WINDOW,
                LegacyTextureFilter::Linear);
}

void CNewUIEmpireGuardianTimer::UnloadImages()
{
    DeleteBitmap(IMAGE_EMPIREGUARDIAN_TIMER_WINDOW);
}

void SEASON3B::CNewUINPCShop::SetPos(int x, int y)
{
    (void)x;
    (void)y;
    SyncModernGeometry();
}

bool SEASON3B::CNewUINPCShop::Render()
{
    bool recorded = m_ModernPanel.Record(renderUnit.LegacyRender());
    if (m_pNewInventoryCtrl)
    {
        m_pNewInventoryCtrl->Render();
        recorded = m_pNewInventoryCtrl->RenderOwnerLayer() && recorded;
    }
    return recorded;
}

void CNewUINPCShop::StageModernContent()
{
    m_ModernPanel.SetSlotFrames(m_pNewInventoryCtrl->SlotIconFrames());
    m_ModernPanel.SetText("tfTitle", I18N::Game::Merchant);
    wchar_t text[256];
    mu_swprintf(text, I18N::Game::TaxRateDChangedInRealTime, m_iTaxRate);
    m_ModernPanel.SetText("tfTax", text);
    ConvertGold(AllRepairGold, text);
    m_ModernPanel.SetText("tfKeepZenValue", text);
    m_ModernPanel.SetText("tfKeepZen", I18N::Game::RepairAll);
    m_ModernPanel.SetText("btnRepair-label", I18N::Game::Repair);
    m_ModernPanel.SetText("btnRepairAll-label", I18N::Game::RepairAll);
    m_ModernPanel.SetShown("mcRepair", m_bRepairShop);
    // This server has no S9 buyback request.
    m_ModernPanel.SetShown("mcRetract", false);
    m_ModernPanel.SetEnabled("btnClose", !m_bSellingItem);
}
bool CNewUINPCShop::PrepareModernUiOnWorker(int width, int height)
{
    return m_ModernPanel.PrepareOnWorker(width, height);
}

void CNewUINPCQuest::SetPos(int, int)
{
}

void CNewUINPCQuest::StageRequirements(Panel::Content &content)
{
    const auto *quest = g_csQuest.GetCurQuestAttribute();
    const int baseClass = gCharacterManager.GetBaseClass(Hero->Class);
    for (int i = 0; i < quest->shQuestConditionNum; ++i)
    {
        const auto &act = quest->QuestAct[i];
        if (!act.byRequestClass[baseClass])
            continue;
        if (act.byQuestType == QUEST_ITEM)
        {
            const int type = act.wItemType * MAX_ITEM_INDEX + act.byItemSubType;
            wchar_t name[128];
            GetItemName(type, act.byItemLevel, name);
            content.requirements.push_back(
                {std::wstring(name) + L" x " + std::to_wstring(act.byItemNum),
                 g_csQuest.FindQuestItemsInInven(type, act.byItemNum, act.byItemLevel) == 0});
            items_.push_back({type, act.byItemLevel});
        }
        else if (act.byQuestType == QUEST_MONSTER)
        {
            const int count =
                std::min(g_csQuest.GetKillMobCount(act.wItemType), int(act.byItemNum));
            content.requirements.push_back({std::wstring(getMonsterName(act.wItemType)) + L" x " +
                                                std::to_wstring(count) + L"/" +
                                                std::to_wstring(act.byItemNum),
                                            count == act.byItemNum});
        }
    }
    content.mode = items_.empty()       ? Panel::Mode::Monsters
                   : items_.size() == 1 ? Panel::Mode::Item
                                        : Panel::Mode::Items;
    content.complete = g_csQuest.BeQuestItem();
}
void CNewUINPCQuest::StageContent()
{
    const auto index = g_csQuest.GetCurrQuestIndex();
    const auto state = g_csQuest.getQuestState2(index);
    const auto branch = std::tuple(int(index), int(state), g_iCurrentDialogScript);
    if (branch_ != branch)
    {
        branch_ = branch;
        pending_ = false;
    }
    Panel::Content next;
    const bool specialNpc = (Hero->Class == CLASS_DARK_LORD || Hero->Class == CLASS_DARK ||
                             Hero->Class == CLASS_RAGEFIGHTER) &&
                            bCheckNPC;
    next.title = g_csQuest.GetNPCName(specialNpc ? 2 : index);
    if (!specialNpc)
        next.questTitle = g_csQuest.getQuestTitle();
    next.dialogue = I18N::Dialog::Lookup(g_iCurrentDialogScript);
    const auto &entry = GameLogic::Quests::Dialog::GetEntry(g_iCurrentDialogScript);
    for (int i = 0; i < entry.numAnswer; ++i)
        next.choices.push_back(std::to_wstring(i + 1) + L") " +
                               I18N::Dialog::Lookup(GameLogic::Quests::Dialog::DialogAnswerLegacyId(
                                   g_iCurrentDialogScript, i)));
    next.canChoose = !pending_;
    next.completeLabel = I18N::Game::ProceedWithQuest;
    items_.clear();
    if (state == QUEST_ING)
        StageRequirements(next);
    else if (state == QUEST_NO)
    {
        next.mode = Panel::Mode::Zen;
        next.costLabel = I18N::Game::Cost;
        wchar_t cost[128];
        ::ConvertGold(g_csQuest.GetNeedZen(), cost);
        next.cost = cost;
    }
    if (next == content_)
        return;
    content_ = std::move(next);
    ++revision_;
}
bool CNewUINPCQuest::Render()
{
    const bool recorded = panel_.Record(renderer_.LegacyRender());
    return items_.empty() ? recorded
                          : models_->RenderObject(*this, INVENTORY_CAMERA_Z_ORDER) && recorded;
}
void CNewUINPCQuest::Render3D()
{
    const auto &rects = panel_.ItemRects();
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        const auto &rect = rects[i];
        if (rect.width == 0)
            continue;
        RenderItem3D(rect.x, rect.y, rect.width, rect.height, items_[i].type, items_[i].level, 0, 0,
                     false, panel_.ItemPresentationScale());
    }
}

bool CNewUINPCQuest::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_, revision_, content_);
}

void CNewUIGatemanWindow::SetPos(int, int)
{
}
void CNewUIGatemanWindow::StageDescriptions(int type, bool publicAccess)
{
    if (type == TOUCH_TYPE_GUILD_MASTER)
        content_.labels[2] = std::wstring(I18N::Game::OnlyTheGuildMembers) + L"\n" +
                             I18N::Game::AreAllowedToEnter + L"\n" + I18N::Game::IsAllowed;
    else if (type == TOUCH_TYPE_GUILD_STAFF)
        content_.labels[2] = I18N::Game::WouldYouLikeToEnter;
    else if (publicAccess)
        content_.labels[2] = std::wstring(I18N::Game::PayEntranceFeeToEnter) + L"\n" +
                             I18N::Game::WouldYouLikeToEnter;
    else
        content_.labels[2] = std::wstring(I18N::Game::EnteringIsNotAllowed) + L"\n" +
                             I18N::Game::ApprovalFromTheLordOfACastleIsRequired + L"\n" +
                             I18N::Game::ForEntering + L"\n" + I18N::Game::PleaseGoBack;
    content_.labels[9] = GatekeeperDetail::FeeText(state_[5], I18N::Game::EntranceFeeRange0SZen) +
                         L"\n" + I18N::Game::ForSetting + L"\n" +
                         GatekeeperDetail::FeeText(state_[4], I18N::Game::IncreaseUnitSZen);
}
void CNewUIGatemanWindow::StageContent()
{
    auto &gate = *g_pUIGateKeeper;
    const std::array<int, 6> state{gate.GetType(),
                                   gate.IsPublic(),
                                   gate.GetEnteranceFee(),
                                   gate.GetViewEnteranceFee(),
                                   gate.GetAddEnteranceFee(),
                                   gate.GetMaxEnteranceFee()};
    const char *locale = I18N::GetCurrentLocale();
    if (content_.revision && state == state_ && locale_ == locale)
        return;
    state_ = state;
    locale_ = locale;
    content_.canManage = state[0] == TOUCH_TYPE_GUILD_MASTER;
    content_.publicAccess = state[1] != 0;
    content_.canEnter = content_.canManage || state[0] == TOUCH_TYPE_GUILD_STAFF ||
                        (state[0] == TOUCH_TYPE_PERSON && content_.publicAccess);
    content_.feeIndex = state[4] > 0 ? state[3] / state[4] : 0;
    content_.maximumFeeIndex = state[4] > 0 ? state[5] / state[4] : 0;
    content_.labels = {I18N::Game::GuardNPC,
                       I18N::Game::EntranceRestriction,
                       L"",
                       I18N::Game::OpenItToNonMembers,
                       GatekeeperDetail::FeeText(state[2], I18N::Game::EntranceFeeSZen),
                       L"",
                       I18N::Game::EntranceFeeSetting,
                       GatekeeperDetail::FeeText(state[3], L"%ls") + L" " + I18N::Game::Zen,
                       I18N::Game::Confirm,
                       L"",
                       I18N::Game::Enter,
                       I18N::Game::Close388};
    StageDescriptions(state[0], content_.publicAccess);
    ++content_.revision;
}

bool CNewUIGatemanWindow::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}
bool CNewUIGatemanWindow::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_, content_);
}

void CNewUIMyQuestInfoWindow::SetPos(int, int)
{
}

void CNewUIMyQuestInfoWindow::StageSelection(Panel::Content &next)
{
    rewardItems_.fill(nullptr);
    if (!selected_)
    {
        if (quests_.empty())
            next.summary = I18N::Game::Lookup(2825);
        return;
    }
    const auto quest = GetSelQuestIndex();
    next.summary = std::wstring(QuestInfoDetail::QuestText(g_QuestMng.GetSubject(quest))) + L"\n" +
                   QuestInfoDetail::QuestText(g_QuestMng.GetSummary(quest));
    if (!showRewards_ || !g_QuestMng.IsRequestRewardQS(quest))
        return;
    auto rewards = BuildQuestRewardPresentation(g_QuestMng, quest);
    next.requirements = std::move(rewards.requirements);
    next.rewards = std::move(rewards.rewards);
    rewardItems_ = rewards.items;
}
void CNewUIMyQuestInfoWindow::StageEvents(Panel::Content &next)
{
    constexpr int DailyEntryLimit = 6; // Native event-entry rule used by this client.
    wchar_t count[256], limit[256];
    mu_swprintf(limit, I18N::Game::YouMayEnterOnlyDTimesPerDay, DailyEntryLimit);
    mu_swprintf(count, I18N::Game::EntranceIsAllowedForDTimes, g_csQuest.GetEventCount(2));
    next.castle = std::wstring(count) + L"\n" + limit;
    mu_swprintf(count, I18N::Game::EntranceIsAllowedForDTimes, g_csQuest.GetEventCount(3));
    next.temple = std::wstring(count) + L"\n" + limit;
}
void CNewUIMyQuestInfoWindow::StageContent()
{
    StageKey key{listRevision_,
                 g_QuestMng.RequestRewardRevision(),
                 GetSelQuestIndex(),
                 tab_,
                 canStart_,
                 canGiveUp_,
                 showRewards_,
                 {},
                 I18N::GetCurrentLocale()};
    if (tab_ == Panel::Tab::JobChange)
        key.nativeState = {g_iCurrentDialogScript,
                           g_csQuest.getQuestState2(g_csQuest.GetCurrQuestIndex()), 0, 0};
    if (tab_ == Panel::Tab::CastleTemple)
        key.nativeState = {0, 0, g_csQuest.GetEventCount(2), g_csQuest.GetEventCount(3)};
    if (staged_ == key)
        return;
    staged_ = key;
    Panel::Content next;
    next.title = next.questLabel = I18N::Game::Quest;
    next.jobLabel = I18N::Game::ChangeClass;
    next.eventsLabel = I18N::Game::CastleTemple;
    next.startLabel = I18N::Game::StartQuest;
    next.giveUpLabel = I18N::Game::GiveUpQuest;
    next.requirementsLabel = I18N::Game::Requirements;
    next.rewardsLabel = I18N::Game::Reward;
    next.castleLabel = I18N::Game::BloodCastle;
    next.templeLabel = I18N::Game::IllusionTemple;
    next.tab = tab_;
    next.selected = selected_;
    next.canStart = canStart_;
    next.canGiveUp = canGiveUp_;
    for (std::size_t i = 0; i < quests_.size(); ++i)
        next.quests.push_back(std::to_wstring(i + 1) + L". " +
                              QuestInfoDetail::QuestText(g_QuestMng.GetSubject(quests_[i])));
    StageSelection(next);
    if (tab_ == Panel::Tab::JobChange)
    {
        next.jobTitle = g_csQuest.getQuestTitleWindow();
        next.jobDialogue = I18N::Dialog::Lookup(g_iCurrentDialogScript);
        const auto state = key.nativeState[1];
        const int text = state == QUEST_ING ? 931 : state == QUEST_END ? 932 : 930;
        next.jobState = I18N::Game::Lookup(text);
    }
    if (tab_ == Panel::Tab::CastleTemple)
        StageEvents(next);
    if (next == content_)
        return;
    content_ = std::move(next);
    ++revision_;
}
void CNewUIMyQuestInfoWindow::StageTooltip()
{
    const auto hover = panel_.HoveredReward(revision_);
    if (hover && rewardItems_[*hover])
        RenderItemInfo(MouseX, MouseY, rewardItems_[*hover], false, 0, true);
}
bool CNewUIMyQuestInfoWindow::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}

bool CNewUIMyQuestInfoWindow::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_, revision_, content_);
}

void CNewUIQuestProgress::SetPos(int, int)
{
}

void CNewUIQuestProgress::StageRequirements(Panel::Content &content)
{
    auto view = BuildQuestRewardPresentation(g_QuestMng, quest_);
    content.requirements = std::move(view.requirements);
    content.rewards = std::move(view.rewards);
    content.complete = view.complete && completionEnabled_;
    rewardItems_ = view.items;
}
void CNewUIQuestProgress::StageContent()
{
    if (!quest_)
        return;
    auto key = std::tuple(quest_, g_QuestMng.RequestRewardRevision(),
                          std::string(I18N::GetCurrentLocale()), pending_, completionEnabled_);
    if (staged_ == key)
        return;
    staged_ = std::move(key);
    Panel::Content next;
    next.title = I18N::Game::Quest;
    next.questTitle = QuestProgressDetail::QuestText(g_QuestMng.GetSubject(quest_));
    next.dialogue = QuestProgressDetail::QuestText(g_QuestMng.GetNPCWords(quest_));
    if (!byItem_)
    {
        next.npc = QuestProgressDetail::QuestText(g_QuestMng.GetNPCName());
        next.player = CharacterAttribute->Name;
        next.playerWords = QuestProgressDetail::QuestText(g_QuestMng.GetPlayerWords(quest_));
    }
    next.requirementsTitle = I18N::Game::Requirements;
    next.rewardsTitle = I18N::Game::Reward;
    next.confirmLabel = I18N::Game::OK;
    next.canChoose = !pending_;
    for (int i = 0; i < QM_MAX_ANSWER; ++i)
    {
        const auto *answer = g_QuestMng.GetAnswer(quest_, i);
        if (!answer)
            break;
        next.choices.push_back(std::to_wstring(i + 1) + L". " + answer);
    }
    if (next.choices.empty())
        StageRequirements(next);
    else
        rewardItems_.fill(nullptr);
    if (next == content_)
        return;
    content_ = std::move(next);
    ++revision_;
}
void CNewUIQuestProgress::StageTooltip()
{
    const auto hovered = panel_.HoveredReward(revision_);
    if (!hovered || !rewardItems_[*hovered])
        return;
    RenderItemInfo(MouseX, MouseY, rewardItems_[*hovered], false, 0, true);
}
bool CNewUIQuestProgress::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}

bool CNewUIQuestProgress::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_, revision_, branch_, content_);
}

void CNewUINPCDialogue::SetPos(int, int)
{
}

bool CNewUINPCDialogue::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}
bool CNewUINPCDialogue::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_, content_);
}

void CNewUINPCDialogue::StageContribution()
{
    content_.contribution.clear();
    constexpr int DuprianNpc = 543, VanertNpc = 544;
    if ((g_QuestMng.GetNPCIndex() == DuprianNpc && Hero->m_byGensInfluence == 1) ||
        (g_QuestMng.GetNPCIndex() == VanertNpc && Hero->m_byGensInfluence == 2))
    {
        wchar_t value[64];
        ::wprintf(value, I18N::Game::GainContributionU, m_dwContributePoint);
        content_.contribution = value;
    }
}
