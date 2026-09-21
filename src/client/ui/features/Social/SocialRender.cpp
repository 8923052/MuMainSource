#include "ui/features/Social/SocialRender.h"
#include "I18N/All.h"
#include "app/AppWindow.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "data/WorldData.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/ChatSocial.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Quests.h"
#include "domain/Shop.h"
#include "domain/WorldSimulation.h"
#include "render/FrameTape.h"
#include "render/ModelResources.h"
#include "render/Terrain.h"
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
#include "support/Scenes.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/features/World/WorldLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"
#include "ui/session/UiSessionRender.h"

namespace UI::Modern::PC::Chat
{
namespace
{
// Authored design inputs.
enum class DesignKey
{
    ViewHeights,
    ResizeViewportHeight,
    SmallStageScale,
    LargeStageBottom,
    SmallStageBottom,
    ViewWidth,
    RowHeight,
    EditingExtraHeight,
    ViewHeightMargin,
    MenuTop,
    InputTop,
    ScrollDownBottom,
    MarqueeSecondsPerPixel,
    ShadowOffsetX,
    AlphaStep,
    AlphaMaximum,
    AlphaMinimum,
    DefaultAlpha,
    RmlBlockedChatWidth,
    RmlBlockedChatHeight,
    RmlBlockedChatVisibleRows
};

const RmlUiDesign &Design()
{
    static const RmlUiDesign design("Data/UI/PC/Chat/chat.rml",
                                    {"RmlChatPanel-ViewHeights",
                                     "RmlChatPanel-ResizeViewportHeight",
                                     "RmlChatPanel-SmallStageScale",
                                     "RmlChatPanel-LargeStageBottom",
                                     "RmlChatPanel-SmallStageBottom",
                                     "RmlChatPanel-ViewWidth",
                                     "RmlChatPanel-RowHeight",
                                     "RmlChatPanel-EditingExtraHeight",
                                     "RmlChatPanel-ViewHeightMargin",
                                     "RmlChatPanel-MenuTop",
                                     "RmlChatPanel-InputTop",
                                     "RmlChatPanel-ScrollDownBottom",
                                     "RmlChatPanel-MarqueeSecondsPerPixel",
                                     "RmlChatPanel-ShadowOffsetX",
                                     "RmlChatPanel-AlphaStep",
                                     "RmlChatPanel-AlphaMaximum",
                                     "RmlChatPanel-AlphaMinimum",
                                     "RmlChatPanel-DefaultAlpha",
                                     "RmlChatPanel-RmlBlockedChatWidth",
                                     "RmlChatPanel-RmlBlockedChatHeight",
                                     "RmlChatPanel-RmlBlockedChatVisibleRows"});
    return design;
}
// End authored design inputs.

struct ChatMessage final
{
    std::wstring name;
    std::wstring text;
    RmlChatMessageType type = RmlChatMessageType::Normal;
};

bool IsSystemType(RmlChatMessageType type) noexcept
{
    return type == RmlChatMessageType::System || type == RmlChatMessageType::Error;
}

bool IsDescendantOf(const Rml::Element *element, const Rml::Element *ancestor) noexcept
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        if (element == ancestor)
            return true;
    }
    return false;
}

void SetPixels(Rml::Element &element, const char *property, float value)
{
    element.SetProperty(property, Rml::CreateString("%.3fpx", value));
}

void SetText(Rml::Element &element, const std::wstring &text)
{
    element.SetInnerRML(Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(text.c_str())));
}

const char *TypeClass(RmlChatMessageType type) noexcept
{
    switch (type)
    {
    case RmlChatMessageType::Whisper:
        return "whisper";
    case RmlChatMessageType::System:
        return "system";
    case RmlChatMessageType::Error:
        return "error";
    case RmlChatMessageType::Party:
        return "party";
    case RmlChatMessageType::Guild:
        return "guild";
    case RmlChatMessageType::Union:
        return "union";
    case RmlChatMessageType::GameMaster:
        return "gm";
    case RmlChatMessageType::Gens:
        return "gens";
    default:
        return "normal";
    }
}
} // namespace

float RmlBlockedChatWidth() noexcept
{
    return Design().Number(DesignKey::RmlBlockedChatWidth);
}

float RmlBlockedChatHeight() noexcept
{
    return Design().Number(DesignKey::RmlBlockedChatHeight);
}

std::size_t RmlBlockedChatVisibleRows() noexcept
{
    return Design().Number<std::size_t>(DesignKey::RmlBlockedChatVisibleRows);
}

int RmlChatVisibleRowCount(int sizeIndex) noexcept
{
    const auto heights = Design().Values(DesignKey::ViewHeights);
    const float height = heights[std::clamp(sizeIndex, 0, static_cast<int>(heights.size()) - 1)];
    return static_cast<int>(std::floor((height - Design().Number(DesignKey::ViewHeightMargin)) /
                                       Design().Number(DesignKey::RowHeight)));
}

int NextRmlChatViewMode(int viewMode) noexcept
{
    return (std::clamp(viewMode, 0, 3) + 1) % 4;
}

int NextRmlChatSizeIndex(int sizeIndex) noexcept
{
    const int count = static_cast<int>(Design().Values(DesignKey::ViewHeights).size());
    return (std::clamp(sizeIndex, 0, count - 1) + 1) % count;
}

int DefaultRmlChatAlpha() noexcept
{
    return Design().Number<int>(DesignKey::DefaultAlpha);
}

int NextRmlChatAlpha(int alpha) noexcept
{
    alpha += Design().Number<int>(DesignKey::AlphaStep);
    return alpha > Design().Number<int>(DesignKey::AlphaMaximum)
               ? Design().Number<int>(DesignKey::AlphaMinimum)
               : alpha;
}

RmlChatBlockPosition CenterRmlChatBlock(int viewportWidth, int viewportHeight, float scale) noexcept
{
    return {std::max(0.0F, (viewportWidth - RmlBlockedChatWidth() * scale) * 0.5F),
            std::max(0.0F, (viewportHeight - RmlBlockedChatHeight() * scale) * 0.5F)};
}

RmlChatBlockPosition ClampRmlChatBlock(int viewportWidth, int viewportHeight, float scale,
                                       RmlChatBlockPosition position) noexcept
{
    position.left = std::clamp(position.left, 0.0F,
                               std::max(0.0F, viewportWidth - RmlBlockedChatWidth() * scale));
    position.top = std::clamp(position.top, 0.0F,
                              std::max(0.0F, viewportHeight - RmlBlockedChatHeight() * scale));
    return position;
}

bool IsRmlChatMessageVisible(RmlChatMessageType type, const RmlChatState &state) noexcept
{
    if (type == RmlChatMessageType::Whisper)
        return state.showWhisper;
    if (IsSystemType(type))
        return state.showSystem;
    if (state.viewMode == 1 || state.viewMode == 2)
        return false;
    switch (type)
    {
    case RmlChatMessageType::Normal:
        return state.showNormal;
    case RmlChatMessageType::Party:
        return state.showParty;
    case RmlChatMessageType::Guild:
        return state.showGuild;
    case RmlChatMessageType::Gens:
        return state.showGens;
    default:
        return true;
    }
}

void ToggleRmlChatMessageFilter(RmlChatMessageType type, RmlChatState &state) noexcept
{
    switch (type)
    {
    case RmlChatMessageType::Normal:
        state.showNormal = !state.showNormal;
        break;
    case RmlChatMessageType::Whisper:
        state.showWhisper = !state.showWhisper;
        break;
    case RmlChatMessageType::System:
    case RmlChatMessageType::Error:
        state.showSystem = !state.showSystem;
        break;
    case RmlChatMessageType::Party: {
        const bool enabled = !state.showParty;
        state.showParty = enabled;
        state.showGuild = false;
        state.showGens = false;
        state.inputType = enabled ? RmlChatInputType::Party : RmlChatInputType::Normal;
        break;
    }
    case RmlChatMessageType::Guild:
    case RmlChatMessageType::Union: {
        const bool enabled = !state.showGuild;
        state.showParty = false;
        state.showGuild = enabled;
        state.showGens = false;
        state.inputType = enabled ? RmlChatInputType::Guild : RmlChatInputType::Normal;
        break;
    }
    case RmlChatMessageType::Gens: {
        const bool enabled = !state.showGens;
        state.showParty = false;
        state.showGuild = false;
        state.showGens = enabled;
        state.inputType = enabled ? RmlChatInputType::Gens : RmlChatInputType::Normal;
        break;
    }
    default:
        break;
    }
}

RmlChatSendBlockReason RmlChatSendBlockReasonFor(RmlChatInputType type, bool inParty, bool inGuild,
                                                 bool inGens) noexcept
{
    if (type == RmlChatInputType::Party && !inParty)
        return RmlChatSendBlockReason::PartyRequired;
    if (type == RmlChatInputType::Guild && !inGuild)
        return RmlChatSendBlockReason::GuildRequired;
    if (type == RmlChatInputType::Gens && !inGens)
        return RmlChatSendBlockReason::GensRequired;
    return RmlChatSendBlockReason::None;
}

bool HandleRmlChatEnter(const RmlChatState &state, const Rml::ElementFormControlInput &mainInput,
                        const Rml::ElementFormControlInput &whisperInput,
                        const Rml::ElementFormControlInput &blockInput, RmlChatActions &actions)
{
    if (state.blockWindowOpen && blockInput.IsPseudoClassSet("focus"))
    {
        actions.registerBlockedUser = StringUtils::NarrowToWide(blockInput.GetValue().c_str());
        return true;
    }
    if (!state.editing)
    {
        actions.open = true;
        return true;
    }
    if (!mainInput.IsPseudoClassSet("focus") && !whisperInput.IsPseudoClassSet("focus"))
    {
        actions.close = true;
        return true;
    }
    actions.submit = RmlChatSubmit{StringUtils::NarrowToWide(mainInput.GetValue().c_str()),
                                   StringUtils::NarrowToWide(whisperInput.GetValue().c_str())};
    return true;
}

class RmlChatPanel::Impl final : public Rml::EventListener
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : host_(keeper, "chat-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "Chat", "chat.rml"))
    {
    }

    ~Impl() override
    {
        Release();
    }

    void AddMessage(std::wstring name, std::wstring text, RmlChatMessageType type)
    {
        std::lock_guard lock(dataMutex_);
        if (history_.size() >= RmlChatHistoryLimit)
        {
            history_.erase(history_.begin());
        }
        history_.push_back({std::move(name), std::move(text), type});
        ++historyRevision_;
        if (IsSystemType(type))
            forceBottomRevision_ = historyRevision_;
    }

    void ClearMessages()
    {
        std::lock_guard lock(dataMutex_);
        history_.clear();
        ++historyRevision_;
        forceBottomRevision_ = historyRevision_;
    }

    void Stage(const RmlChatState &state, const RmlChatLabels &labels)
    {
        std::lock_guard lock(stateMutex_);
        pendingState_ = state;
        pendingLabels_ = labels;
        hasPendingState_ = true;
    }

    void SetMainText(std::wstring text)
    {
        std::lock_guard lock(stateMutex_);
        requestedMainText_ = std::move(text);
    }

    void SetWhisperTarget(std::wstring name)
    {
        std::lock_guard lock(stateMutex_);
        requestedWhisper_ = std::move(name);
        focusMain_ = true;
    }

    void SetBlockedUsers(std::vector<std::wstring> names)
    {
        std::lock_guard lock(stateMutex_);
        requestedBlockedUsers_ = std::move(names);
    }

    RmlChatActions TakeActions()
    {
        std::lock_guard lock(actionMutex_);
        RmlChatActions result = std::move(actions_);
        actions_ = {};
        return result;
    }

    bool Prepare(int viewportWidth, int viewportHeight)
    {
        RmlChatState nextState;
        RmlChatLabels nextLabels;
        std::optional<std::wstring> mainText;
        std::optional<std::wstring> whisper;
        std::optional<std::vector<std::wstring>> blockedUsers;
        bool focusMain = false;
        {
            std::lock_guard lock(stateMutex_);
            if (hasPendingState_)
            {
                nextState = pendingState_;
                nextLabels = pendingLabels_;
                hasPendingState_ = false;
            }
            else
            {
                nextState = activeState_;
                nextLabels = activeLabels_;
            }
            mainText = std::move(requestedMainText_);
            whisper = std::move(requestedWhisper_);
            requestedMainText_.reset();
            requestedWhisper_.reset();
            blockedUsers = std::move(requestedBlockedUsers_);
            requestedBlockedUsers_.reset();
            focusMain = std::exchange(focusMain_, false);
        }

        if (!EnsureDocument(viewportWidth, viewportHeight) || !host_.SetVisible(nextState.visible))
            return false;
        const RmlUiScaledViewport viewport = host_.Viewport();

        std::uint64_t revision = 0;
        std::uint64_t forceBottom = 0;
        {
            std::lock_guard lock(dataMutex_);
            revision = historyRevision_;
            forceBottom = forceBottomRevision_;
            if (revision != renderedHistoryRevision_)
            {
                cachedHistory_ = history_;
            }
        }

        const bool viewportChanged = viewportWidth_ != viewport.width ||
                                     viewportHeight_ != viewport.height ||
                                     physicalViewportHeight_ != viewportHeight;
        const bool stateChanged = activeState_ != nextState;
        const bool labelsChanged = activeLabels_ != nextLabels;
        const bool historyChanged = revision != renderedHistoryRevision_;
        const bool forceBottomChanged = forceBottom > renderedHistoryRevision_;
        bool dirty = viewportChanged || stateChanged || labelsChanged || historyChanged ||
                     std::exchange(inputDirty_, false);
        dirty = dirty || GetTickCount64() < marqueeUntilMs_;

        viewportWidth_ = viewport.width;
        viewportHeight_ = viewport.height;
        physicalViewportHeight_ = viewportHeight;

        if (viewportChanged || stateChanged)
        {
            ApplyState(nextState);
            ApplyLayout(viewport.width, viewport.height, nextState);
        }
        if (labelsChanged)
            ApplyLabels(nextLabels);
        activeLabels_ = nextLabels;

        if (blockedUsers.has_value())
        {
            cachedBlockedUsers_ = std::move(*blockedUsers);
            selectedBlockedUser_ = -1;
            blockedUserOffset_ = 0;
            blockedUsersDirty_ = true;
        }
        if (blockedUsersDirty_)
        {
            RenderBlockedUsers();
            blockedUsersDirty_ = false;
            dirty = true;
        }
        dirty = blockRegisterButton_.SyncVisualState() || dirty;
        dirty = blockDeleteButton_.SyncVisualState() || dirty;
        dirty = blockCloseButton_.SyncVisualState() || dirty;
        for (RmlMuButton &button : chatButtons_)
            dirty = button.SyncVisualState() || dirty;

        const int previousVisibleCount = static_cast<int>(visibleMessages_.size());
        bool preserveScroll = historyChanged && bottomOffset_ > 0 && !forceBottomChanged;
        if (historyChanged && forceBottomChanged)
            bottomOffset_ = 0;
        const bool filterChanged =
            stateChanged && (activeState_.viewMode != nextState.viewMode ||
                             activeState_.sizeIndex != nextState.sizeIndex ||
                             activeState_.showNormal != nextState.showNormal ||
                             activeState_.showWhisper != nextState.showWhisper ||
                             activeState_.showSystem != nextState.showSystem ||
                             activeState_.showParty != nextState.showParty ||
                             activeState_.showGuild != nextState.showGuild ||
                             activeState_.showGens != nextState.showGens);
        if (filterChanged)
        {
            bottomOffset_ = 0;
            preserveScroll = false;
        }
        if (historyChanged || filterChanged)
        {
            RebuildVisible(nextState);
            if (preserveScroll)
            {
                bottomOffset_ +=
                    std::max(0, static_cast<int>(visibleMessages_.size()) - previousVisibleCount);
                ClampBottomOffset(nextState);
            }
            RenderRows(nextState);
            dirty = true;
        }

        if (mainText.has_value())
        {
            mainInput_->SetValue(StringUtils::WideToNarrow(mainText->c_str()));
            dirty = true;
        }
        if (whisper.has_value())
        {
            whisperInput_->SetValue(StringUtils::WideToNarrow(whisper->c_str()));
            dirty = true;
        }
        if (nextState.editing && (!activeState_.editing || focusMain))
        {
            mainInput_->Focus();
            dirty = true;
        }
        if (!nextState.editing && activeState_.editing)
        {
            mainInput_->SetValue("");
            host_.ResetInteraction();
            dirty = true;
        }

        activeState_ = nextState;
        renderedHistoryRevision_ = revision;
        return host_.CaptureIfDirty(dirty);
    }

    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!activeState_.visible || panel_ == nullptr)
            return false;
        if (event.action == SessionInputAction::KeyDown && !event.repeat &&
            ProcessChatKey(event.code))
        {
            return true;
        }

        if (event.kind == SessionInputEventKind::Key || event.kind == SessionInputEventKind::Text)
        {
            return HasTextInputFocus() && host_.ProcessInput(event);
        }

        if (event.kind != SessionInputEventKind::Pointer)
            return false;
        const bool wasBlockDragging = blockMovable_.IsDragging();
        const bool wasChatThumbDragging = chatScrollBar_.IsDragging();
        const bool wasBlockThumbDragging = blockScrollBar_.IsDragging();
        (void)host_.ProcessInput(event);
        ApplyScrollBarActions();
        inputDirty_ = blockMovable_.TakeDirty() || inputDirty_;
        CollectChatButtonActions();
        CollectBlockButtonActions();
        if (wasBlockDragging || blockMovable_.IsDragging() || wasChatThumbDragging ||
            chatScrollBar_.IsDragging() || wasBlockThumbDragging || blockScrollBar_.IsDragging())
        {
            return true;
        }
        const bool ownsPointer = IsDescendantOf(host_.HoverElement(), panel_);
        if (!ownsPointer)
            return false;
        if (event.action == SessionInputAction::PointerWheel)
        {
            Rml::Element *const hover = host_.HoverElement();
            if (activeState_.blockWindowOpen &&
                (IsDescendantOf(hover, blockList_) || IsDescendantOf(hover, blockScrollbar_)))
            {
                ScrollBlockedUsers(event.wheel > 0.0F ? -1 : 1);
            }
            else
            {
                ScrollRows(event.wheel > 0.0F ? 1 : -1);
            }
        }
        return true;
    }

    std::optional<RmlTextInputArea> TextInputArea() const
    {
        return HasTextInputFocus() ? host_.FocusedTextInputArea() : std::nullopt;
    }

    bool HasTextInputFocus() const noexcept
    {
        return (mainInput_ != nullptr && mainInput_->IsPseudoClassSet("focus")) ||
               (whisperInput_ != nullptr && whisperInput_->IsPseudoClassSet("focus")) ||
               (blockInput_ != nullptr && blockInput_->IsPseudoClassSet("focus"));
    }

    bool Record(LegacyRenderFacade &facade) const
    {
        return panel_ == nullptr || host_.Record(facade);
    }

    void Release()
    {
        for (RmlMuButton &button : chatButtons_)
            button.Unbind();
        blockRegisterButton_.Unbind();
        blockDeleteButton_.Unbind();
        blockCloseButton_.Unbind();
        blockMovable_.Unbind();
        chatScrollBar_.Unbind();
        blockScrollBar_.Unbind();
        if (panel_ != nullptr)
        {
            panel_->RemoveEventListener("click", this);
        }
        if (messages_ != nullptr)
        {
            messages_->RemoveEventListener("mouseover", this);
            messages_->RemoveEventListener("mouseout", this);
        }
        panel_ = nullptr;
        view_ = nullptr;
        title_ = nullptr;
        background_ = nullptr;
        messages_ = nullptr;
        scrollbar_ = nullptr;
        scrollDown_ = nullptr;
        menu_ = nullptr;
        menuButtons_ = nullptr;
        inputArea_ = nullptr;
        mainInput_ = nullptr;
        whisperInput_ = nullptr;
        blockPanel_ = nullptr;
        blockDrag_ = nullptr;
        blockList_ = nullptr;
        blockScrollbar_ = nullptr;
        blockInput_ = nullptr;
        labels_.fill(nullptr);
        inputButtons_.fill(nullptr);
        cachedHistory_.clear();
        visibleMessages_.clear();
        visibleNames_.clear();
        renderedHistoryRevision_ = 0;
        bottomOffset_ = 0;
        viewportWidth_ = 0;
        viewportHeight_ = 0;
        physicalViewportHeight_ = 0;
        blockedUserOffset_ = 0;
        blockPositionSet_ = false;
        host_.Release();
    }

    void ProcessEvent(Rml::Event &event) override
    {
        Rml::Element *element = event.GetTargetElement();
        if (event.GetType() == "mouseover" || event.GetType() == "mouseout")
        {
            ProcessMarquee(*element, event.GetType() == "mouseover");
            return;
        }
        while (element != nullptr && element != panel_)
        {
            const Rml::String id = element->GetId();
            if (!id.empty())
            {
                if (ProcessClick(id, *element, event))
                    return;
            }
            element = element->GetParentNode();
        }
    }

    void ProcessMarquee(Rml::Element &target, bool enter)
    {
        Rml::Element *row = &target;
        while (row != nullptr && row != messages_ && !row->IsClassSet("chat-row"))
        {
            row = row->GetParentNode();
        }
        if (row == nullptr || row == messages_ || row->GetNumChildren() < 2)
            return;
        Rml::Element *const shadow = row->GetChild(0);
        Rml::Element *const line = row->GetChild(1);
        if (!enter)
        {
            shadow->SetProperty("transition", "none");
            line->SetProperty("transition", "none");
            shadow->RemoveProperty("left");
            line->RemoveProperty("left");
            inputDirty_ = true;
            marqueeUntilMs_ = 0;
            return;
        }
        const float overflow = line->GetScrollWidth() - row->GetClientWidth();
        if (overflow <= 0.0F)
            return;
        const float duration = Design().Number(DesignKey::MarqueeSecondsPerPixel) * overflow;
        const Rml::String transition = Rml::CreateString("left %.3fs linear", duration);
        shadow->SetProperty("transition", transition);
        line->SetProperty("transition", transition);
        SetPixels(*shadow, "left", Design().Number(DesignKey::ShadowOffsetX) - overflow);
        SetPixels(*line, "left", -overflow);
        inputDirty_ = true;
        marqueeUntilMs_ = GetTickCount64() + static_cast<std::uint64_t>(duration * 1000.0F);
    }

  private:
    bool EnsureDocument(int viewportWidth, int viewportHeight)
    {
        const float stageScale = viewportHeight < Design().Number(DesignKey::ResizeViewportHeight)
                                     ? Design().Number(DesignKey::SmallStageScale)
                                     : 1.0F;
        if (!host_.Ensure(viewportWidth, viewportHeight, Design().Number(DesignKey::ViewWidth),
                          RmlBlockedChatHeight(), stageScale))
            return false;
        if (panel_ != nullptr)
            return true;

        Rml::ElementDocument *document = host_.Document();
        panel_ = document->GetElementById("chat-panel");
        view_ = document->GetElementById("chat-view");
        title_ = document->GetElementById("chat-title");
        background_ = document->GetElementById("chat-background");
        messages_ = document->GetElementById("chat-messages");
        scrollbar_ = document->GetElementById("chat-scrollbar");
        scrollDown_ = document->GetElementById("chat-scroll-down");
        menu_ = document->GetElementById("chat-menu");
        menuButtons_ = document->GetElementById("chat-menu-buttons");
        inputArea_ = document->GetElementById("chat-input-area");
        mainInput_ = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
            document->GetElementById("chat-input"));
        whisperInput_ = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
            document->GetElementById("chat-whisper"));
        blockPanel_ = document->GetElementById("blocked-chat");
        blockDrag_ = document->GetElementById("blocked-chat-drag");
        blockList_ = document->GetElementById("blocked-chat-list");
        blockScrollbar_ = document->GetElementById("blocked-chat-scrollbar");
        blockInput_ = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
            document->GetElementById("blocked-chat-input"));
        Rml::Element *const blockRegister = document->GetElementById("blocked-chat-register");
        Rml::Element *const blockDelete = document->GetElementById("blocked-chat-delete");
        Rml::Element *const blockClose = document->GetElementById("blocked-chat-close");
        constexpr std::array<const char *, 7> LabelIds{
            "normal-label",  "party-label",  "guild-label", "gens-label",
            "whisper-label", "system-label", "block-label"};
        constexpr std::array<const char *, 4> InputButtonIds{"type-normal", "type-party",
                                                             "type-guild", "type-gens"};
        constexpr std::array<const char *, 13> ChatButtonIds{
            "chat-config",    "type-normal",      "type-party",    "type-guild",     "type-gens",
            "filter-whisper", "filter-system",    "block-whisper", "chat-view-mode", "chat-size",
            "chat-alpha",     "chat-scroll-down", "chat-title"};
        std::array<Rml::Element *, 13> chatButtonElements{};
        for (std::size_t i = 0; i < labels_.size(); ++i)
        {
            labels_[i] = document->GetElementById(LabelIds[i]);
        }
        for (std::size_t i = 0; i < inputButtons_.size(); ++i)
        {
            inputButtons_[i] = document->GetElementById(InputButtonIds[i]);
        }
        for (std::size_t i = 0; i < chatButtonElements.size(); ++i)
        {
            chatButtonElements[i] = document->GetElementById(ChatButtonIds[i]);
        }
        const bool valid =
            panel_ != nullptr && view_ != nullptr && title_ != nullptr && background_ != nullptr &&
            messages_ != nullptr && scrollbar_ != nullptr && scrollDown_ != nullptr &&
            menu_ != nullptr && menuButtons_ != nullptr && inputArea_ != nullptr &&
            mainInput_ != nullptr && whisperInput_ != nullptr && blockPanel_ != nullptr &&
            blockDrag_ != nullptr && blockList_ != nullptr && blockScrollbar_ != nullptr &&
            blockInput_ != nullptr && blockRegister != nullptr && blockDelete != nullptr &&
            blockClose != nullptr &&
            std::all_of(labels_.begin(), labels_.end(),
                        [](const auto *value) { return value != nullptr; }) &&
            std::all_of(inputButtons_.begin(), inputButtons_.end(),
                        [](const auto *value) { return value != nullptr; }) &&
            std::all_of(chatButtonElements.begin(), chatButtonElements.end(),
                        [](const auto *value) { return value != nullptr; });
        if (!valid || !chatScrollBar_.Bind(*document, "chat-scrollbar") ||
            !blockScrollBar_.Bind(*document, "blocked-chat-scrollbar"))
        {
            Release();
            return false;
        }
        mainInput_->SetAttribute("maxlength", std::to_string(RmlChatInputLimit));
        whisperInput_->SetAttribute("maxlength", MAX_USERNAME_SIZE);
        blockInput_->SetAttribute("maxlength", MAX_USERNAME_SIZE);
        blockRegisterButton_.Bind(*blockRegister);
        blockDeleteButton_.Bind(*blockDelete);
        blockCloseButton_.Bind(*blockClose);
        for (std::size_t i = 0; i < chatButtons_.size(); ++i)
            chatButtons_[i].Bind(*chatButtonElements[i]);
        blockMovable_.Bind(*blockPanel_, *blockDrag_);
        panel_->AddEventListener("click", this);
        messages_->AddEventListener("mouseover", this);
        messages_->AddEventListener("mouseout", this);
        blockedUsersDirty_ = true;
        return true;
    }

    void ApplyLayout(int viewportWidth, int viewportHeight, const RmlChatState &state)
    {
        const bool smallViewport =
            physicalViewportHeight_ < Design().Number(DesignKey::ResizeViewportHeight);
        const float authoredBottom = smallViewport ? Design().Number(DesignKey::SmallStageBottom) /
                                                         Design().Number(DesignKey::SmallStageScale)
                                                   : Design().Number(DesignKey::LargeStageBottom);
        const auto heights = Design().Values(DesignKey::ViewHeights);
        const float viewHeight =
            heights[std::clamp(state.sizeIndex, 0, static_cast<int>(heights.size()) - 1)];
        const float shift = viewHeight - heights.front();
        panelTop_ = viewportHeight - authoredBottom - shift;
        SetPixels(*panel_, "top", panelTop_);
        SetPixels(*panel_, "height",
                  viewHeight +
                      (state.editing ? Design().Number(DesignKey::EditingExtraHeight) : 0.0F));
        SetPixels(*view_, "height", viewHeight);
        SetPixels(*background_, "height", viewHeight - background_->GetProperty<float>("top"));
        SetPixels(*messages_, "height", viewHeight - Design().Number(DesignKey::ViewHeightMargin));
        SetPixels(*menu_, "top", Design().Number(DesignKey::MenuTop) + shift);
        SetPixels(*inputArea_, "top", Design().Number(DesignKey::InputTop) + shift);
        SetPixels(*scrollDown_, "top", viewHeight - Design().Number(DesignKey::ScrollDownBottom));
        ApplyChatScrollBar(state);

        blockMovable_.Configure(viewportWidth, viewportHeight, RmlBlockedChatWidth(),
                                RmlBlockedChatHeight(), 0.0F, panelTop_);
        if (!blockPositionSet_)
        {
            const auto center = CenterRmlChatBlock(viewportWidth, viewportHeight, 1.0F);
            blockMovable_.SetPosition(center.left, center.top);
            blockPositionSet_ = true;
        }
        ApplyBlockedUserScrollBar();
    }

    void ApplyState(const RmlChatState &state)
    {
        const bool backgroundVisible = state.viewMode == 0 || state.viewMode == 1;
        background_->SetProperty("display", backgroundVisible ? "block" : "none");
        title_->SetProperty("display", backgroundVisible && state.editing ? "block" : "none");
        background_->SetProperty("opacity", Rml::CreateString("%.2f", state.alpha / 100.0F));
        title_->SetProperty("opacity", Rml::CreateString("%.2f", state.alpha / 100.0F));
        menu_->SetProperty("display", state.editing ? "block" : "none");
        inputArea_->SetProperty("display", state.editing ? "block" : "none");
        scrollbar_->SetProperty("display", state.editing ? "block" : "none");
        menuButtons_->SetProperty("display", state.menuExpanded ? "block" : "none");
        const std::array<bool, 4> channelFilters{state.showNormal, state.showParty, state.showGuild,
                                                 state.showGens};
        for (std::size_t i = 0; i < inputButtons_.size(); ++i)
            inputButtons_[i]->SetClass("selected", channelFilters[i]);
        documentElement("filter-whisper").SetClass("selected", state.showWhisper);
        documentElement("filter-system").SetClass("selected", state.showSystem);
        documentElement("block-whisper").SetClass("selected", state.blockWindowOpen);
        blockPanel_->SetProperty("display", state.blockWindowOpen ? "block" : "none");
        if (!state.blockWindowOpen)
            blockMovable_.CancelDrag();
        if (!state.blockWindowOpen && activeState_.blockWindowOpen && state.editing)
            mainInput_->Focus();
    }

    void ApplyLabels(const RmlChatLabels &labels)
    {
        const std::array<const std::wstring *, 7> next{
            &labels.normal,  &labels.party,  &labels.guild, &labels.gens,
            &labels.whisper, &labels.system, &labels.block};
        for (std::size_t i = 0; i < labels_.size(); ++i)
        {
            SetText(*labels_[i], *next[i]);
        }
    }

    Rml::Element &documentElement(const char *id)
    {
        return *host_.Document()->GetElementById(id);
    }

    void RebuildVisible(const RmlChatState &state)
    {
        visibleMessages_.clear();
        for (const ChatMessage &message : cachedHistory_)
        {
            if (IsRmlChatMessageVisible(message.type, state))
                visibleMessages_.push_back(&message);
        }
        ClampBottomOffset(state);
    }

    void RenderBlockedUsers()
    {
        if (blockList_ == nullptr)
            return;
        blockedUserOffset_ = std::min(blockedUserOffset_, BlockedUserMaximumOffset());
        std::string rml;
        const std::size_t count = std::min<std::size_t>(
            cachedBlockedUsers_.size() - blockedUserOffset_, RmlBlockedChatVisibleRows());
        for (std::size_t row = 0; row < count; ++row)
        {
            const std::size_t index = blockedUserOffset_ + row;
            const std::string name = Rml::StringUtilities::EncodeRml(
                StringUtils::WideToNarrow(cachedBlockedUsers_[index].c_str()));
            rml += "<div id='blocked-user-" + std::to_string(index) + "' class='blocked-chat-user";
            if (static_cast<int>(index) == selectedBlockedUser_)
                rml += " selected";
            rml += "'>" + name + "</div>";
        }
        blockList_->SetInnerRML(rml);
        ApplyBlockedUserScrollBar();
    }

    std::size_t BlockedUserMaximumOffset() const noexcept
    {
        return cachedBlockedUsers_.size() > RmlBlockedChatVisibleRows()
                   ? cachedBlockedUsers_.size() - RmlBlockedChatVisibleRows()
                   : 0;
    }

    RmlMuScrollBarState BlockedUserScrollBarState() const noexcept
    {
        const std::size_t maximum = BlockedUserMaximumOffset();
        return {blockedUserOffset_,
                maximum,
                RmlBlockedChatVisibleRows(),
                maximum > 0 && blockedUserOffset_ > 0 ? ButtonVisualState::Up
                                                      : ButtonVisualState::Disabled,
                maximum > 0 && blockedUserOffset_ < maximum ? ButtonVisualState::Up
                                                            : ButtonVisualState::Disabled,
                maximum == 0 ? ButtonVisualState::Disabled
                             : (blockScrollBar_.IsDragging() ? ButtonVisualState::Down
                                                             : ButtonVisualState::Up),
                RmlBlockedChatVisibleRows()};
    }

    void ApplyBlockedUserScrollBar()
    {
        if (blockScrollbar_ != nullptr)
            blockScrollBar_.Apply(BlockedUserScrollBarState());
    }

    void ScrollBlockedUsers(int delta)
    {
        const int maximum = static_cast<int>(BlockedUserMaximumOffset());
        const int next = std::clamp(static_cast<int>(blockedUserOffset_) + delta, 0, maximum);
        if (next == static_cast<int>(blockedUserOffset_))
            return;
        blockedUserOffset_ = static_cast<std::size_t>(next);
        RenderBlockedUsers();
        inputDirty_ = true;
    }

    void ApplyScrollBarActions()
    {
        if (const auto position = blockScrollBar_.TakeRequestedPosition())
        {
            blockedUserOffset_ = *position;
            RenderBlockedUsers();
            inputDirty_ = true;
        }
        if (const auto position = chatScrollBar_.TakeRequestedPosition())
        {
            const RmlMuScrollBarState state = ChatScrollBarState(activeState_);
            bottomOffset_ = static_cast<int>(state.maximum - *position);
            RenderRows(activeState_);
            inputDirty_ = true;
        }
    }

    void CollectChatButtonActions()
    {
        std::array<bool, 13> clicked{};
        bool any = false;
        for (std::size_t i = 0; i < chatButtons_.size(); ++i)
        {
            clicked[i] = chatButtons_[i].IsClick();
            any = any || clicked[i];
        }
        if (!any)
            return;
        if (clicked[11])
        {
            bottomOffset_ = 0;
            RenderRows(activeState_);
            inputDirty_ = true;
        }

        std::lock_guard lock(actionMutex_);
        actions_.toggleMenu = actions_.toggleMenu || clicked[0];
        actions_.toggleNormal = actions_.toggleNormal || clicked[1];
        actions_.toggleParty = actions_.toggleParty || clicked[2];
        actions_.toggleGuild = actions_.toggleGuild || clicked[3];
        actions_.toggleGens = actions_.toggleGens || clicked[4];
        actions_.toggleWhisper = actions_.toggleWhisper || clicked[5];
        actions_.toggleSystem = actions_.toggleSystem || clicked[6];
        actions_.toggleBlock = actions_.toggleBlock || clicked[7];
        actions_.cycleViewMode = actions_.cycleViewMode || clicked[8];
        actions_.cycleSize = actions_.cycleSize || clicked[9] || clicked[12];
        actions_.cycleAlpha = actions_.cycleAlpha || clicked[10];
    }

    void CollectBlockButtonActions()
    {
        const bool registerClick = blockRegisterButton_.IsClick();
        const bool deleteClick = blockDeleteButton_.IsClick();
        const bool closeClick = blockCloseButton_.IsClick();
        if (!registerClick && !deleteClick && !closeClick)
            return;

        std::lock_guard lock(actionMutex_);
        if (registerClick)
        {
            actions_.registerBlockedUser =
                StringUtils::NarrowToWide(blockInput_->GetValue().c_str());
            blockInput_->SetValue("");
        }
        if (deleteClick && selectedBlockedUser_ >= 0 &&
            selectedBlockedUser_ < static_cast<int>(cachedBlockedUsers_.size()))
        {
            actions_.deleteBlockedUser = cachedBlockedUsers_[selectedBlockedUser_];
        }
        if (closeClick)
            actions_.closeBlock = true;
    }

    int VisibleRowCount(const RmlChatState &state) const noexcept
    {
        return RmlChatVisibleRowCount(state.sizeIndex);
    }

    void ClampBottomOffset(const RmlChatState &state)
    {
        const int maximum =
            std::max(0, static_cast<int>(visibleMessages_.size()) - VisibleRowCount(state));
        bottomOffset_ = std::clamp(bottomOffset_, 0, maximum);
    }

    std::wstring FormattedText(const ChatMessage &message, const RmlChatLabels &labels) const
    {
        std::wstring prefix;
        switch (message.type)
        {
        case RmlChatMessageType::Whisper:
            prefix = labels.whisper;
            break;
        case RmlChatMessageType::Party:
            prefix = labels.party;
            break;
        case RmlChatMessageType::Guild:
            prefix = labels.guild;
            break;
        case RmlChatMessageType::Union:
            prefix = L"Union";
            break;
        case RmlChatMessageType::Gens:
            prefix = labels.gens;
            break;
        default:
            break;
        }
        std::wstring result;
        if (!prefix.empty())
            result = L"[" + prefix + L"]";
        if (!message.name.empty())
            result += message.name + L" :";
        result += message.text;
        return result;
    }

    void RenderRows(const RmlChatState &state)
    {
        ClampBottomOffset(state);
        const int rowCount = VisibleRowCount(state);
        const int end = std::max(0, static_cast<int>(visibleMessages_.size()) - bottomOffset_);
        const int begin = std::max(0, end - rowCount);
        std::string rml;
        visibleNames_.clear();
        for (int index = begin; index < end; ++index)
        {
            const ChatMessage &message = *visibleMessages_[index];
            const std::wstring text = FormattedText(message, activeLabels_);
            const std::string encoded =
                Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(text.c_str()));
            const std::size_t row = visibleNames_.size();
            visibleNames_.push_back(message.name);
            rml += "<div class='chat-row " + std::string(TypeClass(message.type)) +
                   "' id='chat-row-" + std::to_string(row) + "'>" + "<div class='chat-shadow'>" +
                   encoded + "</div>" + "<div class='chat-line'>" + encoded + "</div></div>";
        }
        messages_->SetInnerRML(rml);
        scrollDown_->SetProperty("display", bottomOffset_ > 0 ? "block" : "none");
        ApplyChatScrollBar(state);
    }

    RmlMuScrollBarState ChatScrollBarState(const RmlChatState &state) const noexcept
    {
        const std::size_t pageSize = static_cast<std::size_t>(VisibleRowCount(state));
        const std::size_t maximum =
            visibleMessages_.size() > pageSize ? visibleMessages_.size() - pageSize : 0;
        const std::size_t position =
            maximum -
            static_cast<std::size_t>(std::clamp(bottomOffset_, 0, static_cast<int>(maximum)));
        return {
            position,
            maximum,
            pageSize,
            maximum > 0 && position > 0 ? ButtonVisualState::Up : ButtonVisualState::Disabled,
            maximum > 0 && position < maximum ? ButtonVisualState::Up : ButtonVisualState::Disabled,
            maximum == 0
                ? ButtonVisualState::Disabled
                : (chatScrollBar_.IsDragging() ? ButtonVisualState::Down : ButtonVisualState::Up)};
    }

    void ApplyChatScrollBar(const RmlChatState &state)
    {
        if (scrollbar_ != nullptr)
            chatScrollBar_.Apply(ChatScrollBarState(state));
    }

    float CurrentScale() const noexcept
    {
        return 1.0F;
    }

    void ScrollRows(int delta)
    {
        bottomOffset_ += delta;
        ClampBottomOffset(activeState_);
        RenderRows(activeState_);
        inputDirty_ = true;
    }

    void ToggleAction(bool RmlChatActions::*member)
    {
        std::lock_guard lock(actionMutex_);
        actions_.*member = true;
    }

    bool ProcessChatKey(std::int32_t code)
    {
        if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER)
        {
            std::lock_guard lock(actionMutex_);
            return HandleRmlChatEnter(activeState_, *mainInput_, *whisperInput_, *blockInput_,
                                      actions_);
        }
        if (activeState_.blockWindowOpen)
        {
            if (code == SDL_SCANCODE_ESCAPE)
            {
                ToggleAction(&RmlChatActions::closeBlock);
                return true;
            }
        }

        switch (code)
        {
        case SDL_SCANCODE_F2:
            ToggleAction(&RmlChatActions::toggleBlock);
            return true;
        case SDL_SCANCODE_F5:
            ToggleAction(&RmlChatActions::cycleViewMode);
            return true;
        case SDL_SCANCODE_F9:
            ToggleAction(&RmlChatActions::toggleGens);
            return true;
        case SDL_SCANCODE_F10:
            ToggleAction(&RmlChatActions::toggleNormal);
            return true;
        case SDL_SCANCODE_F11:
            ToggleAction(&RmlChatActions::toggleParty);
            return true;
        case SDL_SCANCODE_F12:
            ToggleAction(&RmlChatActions::toggleGuild);
            return true;
        default:
            break;
        }
        if (!activeState_.editing)
        {
            return false;
        }
        if (code == SDL_SCANCODE_F3)
        {
            ToggleAction(&RmlChatActions::toggleWhisper);
            return true;
        }
        if (code == SDL_SCANCODE_F4)
        {
            ToggleAction(&RmlChatActions::cycleSize);
            return true;
        }
        if (code == SDL_SCANCODE_ESCAPE)
        {
            ToggleAction(&RmlChatActions::close);
            return true;
        }
        if (!HasTextInputFocus())
            return false;
        if (code == SDL_SCANCODE_UP || code == SDL_SCANCODE_DOWN)
        {
            const int delta = code == SDL_SCANCODE_UP ? -1 : 1;
            std::lock_guard lock(actionMutex_);
            if (whisperInput_->IsPseudoClassSet("focus"))
                actions_.whisperHistoryDelta = delta;
            else
                actions_.chatHistoryDelta = delta;
            return true;
        }
        return false;
    }

    bool ProcessClick(const Rml::String &id, Rml::Element &element, Rml::Event &event)
    {
        std::lock_guard lock(actionMutex_);
        if (id.rfind("blocked-user-", 0) == 0)
        {
            const int row = std::atoi(id.c_str() + 13);
            if (row >= 0 && row < static_cast<int>(cachedBlockedUsers_.size()))
            {
                selectedBlockedUser_ = row;
                RenderBlockedUsers();
                inputDirty_ = true;
            }
        }
        else if (id.rfind("chat-row-", 0) == 0)
        {
            const int row = std::atoi(id.c_str() + 9);
            if (row >= 0 && row < static_cast<int>(visibleNames_.size()) &&
                !visibleNames_[row].empty())
            {
                actions_.whisperTarget = visibleNames_[row];
            }
        }
        else
        {
            (void)element;
            (void)event;
            return false;
        }
        return true;
    }

    RmlDocumentHost host_;
    mutable std::mutex dataMutex_;
    std::vector<ChatMessage> history_;
    std::uint64_t historyRevision_ = 0;
    std::uint64_t forceBottomRevision_ = 0;

    mutable std::mutex stateMutex_;
    RmlChatState pendingState_{};
    RmlChatLabels pendingLabels_{};
    bool hasPendingState_ = false;
    std::optional<std::wstring> requestedMainText_;
    std::optional<std::wstring> requestedWhisper_;
    std::optional<std::vector<std::wstring>> requestedBlockedUsers_;
    bool focusMain_ = false;

    mutable std::mutex actionMutex_;
    RmlChatActions actions_{};

    RmlChatState activeState_{};
    RmlChatLabels activeLabels_{};
    std::vector<ChatMessage> cachedHistory_;
    std::vector<const ChatMessage *> visibleMessages_;
    std::vector<std::wstring> visibleNames_;
    std::vector<std::wstring> cachedBlockedUsers_;
    std::uint64_t renderedHistoryRevision_ = 0;
    int bottomOffset_ = 0;
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
    int physicalViewportHeight_ = 0;
    float chatScale_ = 1.0F;
    float panelTop_ = 0.0F;
    bool blockPositionSet_ = false;
    std::size_t blockedUserOffset_ = 0;
    bool inputDirty_ = false;
    bool blockedUsersDirty_ = false;
    int selectedBlockedUser_ = -1;
    std::uint64_t marqueeUntilMs_ = 0;

    Rml::Element *panel_ = nullptr;
    Rml::Element *view_ = nullptr;
    Rml::Element *title_ = nullptr;
    Rml::Element *background_ = nullptr;
    Rml::Element *messages_ = nullptr;
    Rml::Element *scrollbar_ = nullptr;
    Rml::Element *scrollDown_ = nullptr;
    Rml::Element *menu_ = nullptr;
    Rml::Element *menuButtons_ = nullptr;
    Rml::Element *inputArea_ = nullptr;
    Rml::ElementFormControlInput *mainInput_ = nullptr;
    Rml::ElementFormControlInput *whisperInput_ = nullptr;
    Rml::Element *blockPanel_ = nullptr;
    Rml::Element *blockDrag_ = nullptr;
    Rml::Element *blockList_ = nullptr;
    Rml::Element *blockScrollbar_ = nullptr;
    Rml::ElementFormControlInput *blockInput_ = nullptr;
    std::array<RmlMuButton, 13> chatButtons_{};
    RmlMuButton blockRegisterButton_;
    RmlMuButton blockDeleteButton_;
    RmlMuButton blockCloseButton_;
    RmlMuScrollBar chatScrollBar_;
    RmlMuScrollBar blockScrollBar_;
    RmlMuMovablePanel blockMovable_;
    std::array<Rml::Element *, 7> labels_{};
    std::array<Rml::Element *, 4> inputButtons_{};
};

RmlChatPanel::RmlChatPanel(SessionKeeper &keeper) : impl_(std::make_unique<Impl>(keeper))
{
}
RmlChatPanel::~RmlChatPanel() = default;
void RmlChatPanel::AddMessage(std::wstring name, std::wstring text, RmlChatMessageType type)
{
    impl_->AddMessage(std::move(name), std::move(text), type);
}
void RmlChatPanel::ClearMessages()
{
    impl_->ClearMessages();
}
void RmlChatPanel::Stage(const RmlChatState &state, const RmlChatLabels &labels)
{
    impl_->Stage(state, labels);
}
void RmlChatPanel::SetMainText(std::wstring text)
{
    impl_->SetMainText(std::move(text));
}
void RmlChatPanel::SetWhisperTarget(std::wstring name)
{
    impl_->SetWhisperTarget(std::move(name));
}
void RmlChatPanel::SetBlockedUsers(std::vector<std::wstring> names)
{
    impl_->SetBlockedUsers(std::move(names));
}
RmlChatActions RmlChatPanel::TakeActions()
{
    return impl_->TakeActions();
}
bool RmlChatPanel::PrepareOnWorker(int width, int height)
{
    return impl_->Prepare(width, height);
}
bool RmlChatPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
bool RmlChatPanel::HasTextInputFocus() const noexcept
{
    return impl_->HasTextInputFocus();
}
std::optional<RmlTextInputArea> RmlChatPanel::TextInputArea() const
{
    return impl_->TextInputArea();
}
bool RmlChatPanel::Record(LegacyRenderFacade &facade) const
{
    return impl_->Record(facade);
}
void RmlChatPanel::Release()
{
    impl_->Release();
}
} // namespace UI::Modern::PC::Chat

namespace UI::Modern::PC::Friend
{
namespace
{
// Authored design inputs.
enum class DesignKey
{
    PanelMainWidth,
    PanelMainHeight,
    PanelChatWidth,
    PanelChatCollapsedWidth,
    PanelChatHeight,
    PanelWriteWidth,
    PanelWriteHeight,
    PanelReadWidth,
    PanelReadHeight,
    PanelMainVisibleRows,
    PanelVisibleRows,
    InitialX,
    InitialY,
    ChatVisibleLines
};

const RmlUiDesign &Design()
{
    static const RmlUiDesign design(
        "Data/UI/PC/Friend/friend.rml",
        {"RmlFriendPanel-PanelMainWidth", "RmlFriendPanel-PanelMainHeight",
         "RmlFriendPanel-PanelChatWidth", "RmlFriendPanel-PanelChatCollapsedWidth",
         "RmlFriendPanel-PanelChatHeight", "RmlFriendPanel-PanelWriteWidth",
         "RmlFriendPanel-PanelWriteHeight", "RmlFriendPanel-PanelReadWidth",
         "RmlFriendPanel-PanelReadHeight", "RmlFriendPanel-PanelMainVisibleRows",
         "RmlFriendPanel-PanelVisibleRows", "RmlFriendPanel-InitialX", "RmlFriendPanel-InitialY",
         "RmlFriendPanel-ChatVisibleLines"});
    return design;
}
// End authored design inputs.

void SetText(Rml::Element &element, const std::wstring &text)
{
    element.SetInnerRML(Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(text.c_str())));
}

void SetChecked(Rml::Element &element, bool checked)
{
    if (checked)
        element.SetAttribute("checked", "");
    else
        element.RemoveAttribute("checked");
}

void SetLines(Rml::Element &element, const std::vector<std::wstring> &lines)
{
    Rml::String rml;
    for (const std::wstring &line : lines)
    {
        if (!rml.empty())
            rml += "<br/>";
        rml += Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(line.c_str()));
    }
    element.SetInnerRML(rml);
}

bool IsDescendantOf(const Rml::Element *element, const Rml::Element *ancestor) noexcept
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        if (element == ancestor)
            return true;
    }
    return false;
}

std::pair<int, int> PanelSize(RmlFriendPanelMode mode, bool inviteOpen) noexcept
{
    switch (mode)
    {
    case RmlFriendPanelMode::Main:
        return {RmlFriendPanel::MainWidth(), RmlFriendPanel::MainHeight()};
    case RmlFriendPanelMode::Chat:
        return {inviteOpen ? RmlFriendPanel::ChatWidth() : RmlFriendPanel::ChatCollapsedWidth(),
                RmlFriendPanel::ChatHeight()};
    case RmlFriendPanelMode::WriteLetter:
        return {RmlFriendPanel::WriteWidth(), RmlFriendPanel::WriteHeight()};
    case RmlFriendPanelMode::ReadLetter:
        return {RmlFriendPanel::ReadWidth(), RmlFriendPanel::ReadHeight()};
    }
    return {};
}
} // namespace

int RmlFriendPanel::MainWidth() noexcept
{
    return Design().Number<int>(DesignKey::PanelMainWidth);
}

int RmlFriendPanel::MainHeight() noexcept
{
    return Design().Number<int>(DesignKey::PanelMainHeight);
}

int RmlFriendPanel::ChatWidth() noexcept
{
    return Design().Number<int>(DesignKey::PanelChatWidth);
}

int RmlFriendPanel::ChatCollapsedWidth() noexcept
{
    return Design().Number<int>(DesignKey::PanelChatCollapsedWidth);
}

int RmlFriendPanel::ChatHeight() noexcept
{
    return Design().Number<int>(DesignKey::PanelChatHeight);
}

int RmlFriendPanel::WriteWidth() noexcept
{
    return Design().Number<int>(DesignKey::PanelWriteWidth);
}

int RmlFriendPanel::WriteHeight() noexcept
{
    return Design().Number<int>(DesignKey::PanelWriteHeight);
}

int RmlFriendPanel::ReadWidth() noexcept
{
    return Design().Number<int>(DesignKey::PanelReadWidth);
}

int RmlFriendPanel::ReadHeight() noexcept
{
    return Design().Number<int>(DesignKey::PanelReadHeight);
}

std::size_t RmlFriendPanel::MainVisibleRows() noexcept
{
    return Design().Number<std::size_t>(DesignKey::PanelMainVisibleRows);
}

std::size_t RmlFriendPanel::VisibleRows() noexcept
{
    return Design().Number<std::size_t>(DesignKey::PanelVisibleRows);
}

int RmlFriendInitialX() noexcept
{
    return Design().Number<int>(DesignKey::InitialX);
}
int RmlFriendInitialY() noexcept
{
    return Design().Number<int>(DesignKey::InitialY);
}

std::size_t RmlFriendPanel::ChatVisibleLines() noexcept
{
    return Design().Number<std::size_t>(DesignKey::ChatVisibleLines);
}

class RmlFriendPanel::Impl final
{
  public:
    Impl(SessionKeeper &keeper, RmlFriendPanelMode mode, const RmlFriendPanel &owner,
         std::array<RmlMuButton, 14> &buttons, std::vector<RmlMuButton> &rows,
         RmlMuScrollBar &scrollBar)
        : mode_(mode), buttons_(buttons), rows_(rows), scrollBar_(scrollBar),
          host_(keeper,
                "friend-panel-" + std::to_string(keeper.Id().RawValue()) + "-" +
                    std::to_string(static_cast<int>(mode)) + "-" +
                    std::to_string(reinterpret_cast<std::uintptr_t>(&owner)),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "Friend", "friend.rml"))
    {
    }

    ~Impl()
    {
        Release();
    }

    void Release()
    {
        movable_.Unbind();
        scrollBar_.Unbind();
        for (RmlMuButton &button : buttons_)
            button.Unbind();
        for (RmlMuButton &row : rows_)
            row.Unbind();
        root_ = nullptr;
        drag_ = nullptr;
        title_ = nullptr;
        rowContainer_ = nullptr;
        gridTitle_ = nullptr;
        std::fill(rowElements_.begin(), rowElements_.end(), nullptr);
        std::fill(rowMarks_.begin(), rowMarks_.end(), nullptr);
        std::fill(rowChecks_.begin(), rowChecks_.end(), nullptr);
        std::fill(rowMailIcons_.begin(), rowMailIcons_.end(), nullptr);
        labels_.fill(nullptr);
        headers_.fill(nullptr);
        inputs_.fill(nullptr);
        messageList_ = nullptr;
        memberList_ = nullptr;
        senderLabel_ = nullptr;
        sender_ = nullptr;
        readBody_ = nullptr;
        currentContent_ = {};
        changes_ = {};
        inputValues_.fill(std::wstring{});
        visible_ = false;
        positionSet_ = false;
        viewportWidth_ = 0;
        viewportHeight_ = 0;
        publishedX_.store(0, std::memory_order_release);
        publishedY_.store(0, std::memory_order_release);
        positionDirty_.store(false, std::memory_order_release);
        host_.Release();
    }

    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!visible_)
            return false;
        const bool textInputFocused = host_.FocusedTextInputArea().has_value();
        if ((event.kind == SessionInputEventKind::Key ||
             event.kind == SessionInputEventKind::Text) &&
            !textInputFocused)
        {
            return false;
        }
        const bool wasMoving = movable_.IsDragging();
        const bool wasScrolling = scrollBar_.IsDragging();
        const bool processed = host_.ProcessInput(event);
        if (event.kind == SessionInputEventKind::Window)
            return false;
        if (!processed)
            return false;

        ReadInputChanges();
        ReadButtonChanges();
        if (movable_.TakeDirty())
        {
            const RmlMuPanelPosition position = movable_.Position();
            publishedX_.store(static_cast<int>(std::lround(position.left)),
                              std::memory_order_release);
            publishedY_.store(static_cast<int>(std::lround(position.top)),
                              std::memory_order_release);
            positionDirty_.store(true, std::memory_order_release);
        }

        if (mode_ == RmlFriendPanelMode::Chat && event.action == SessionInputAction::KeyDown &&
            (event.code == SDL_SCANCODE_RETURN || event.code == SDL_SCANCODE_KP_ENTER) &&
            inputs_[0] != nullptr && inputs_[0]->IsPseudoClassSet("focus") &&
            !currentContent_.inputLocked && !inputValues_[0].empty())
        {
            changes_.sendChat = true;
        }

        if (event.kind == SessionInputEventKind::Key || event.kind == SessionInputEventKind::Text)
        {
            return true;
        }

        if (event.kind != SessionInputEventKind::Pointer)
            return false;
        const bool ownsPointer = IsDescendantOf(host_.HoverElement(), root_);
        if (mode_ == RmlFriendPanelMode::Main && ownsPointer &&
            event.action == SessionInputAction::PointerWheel &&
            currentContent_.rows.size() > RmlFriendPanel::MainVisibleRows())
        {
            const std::size_t maximum =
                currentContent_.rows.size() - RmlFriendPanel::MainVisibleRows();
            std::size_t scroll = currentContent_.scroll;
            if (event.wheel > 0.0F && scroll > 0)
                --scroll;
            else if (event.wheel < 0.0F && scroll < maximum)
                ++scroll;
            changes_.scroll = scroll;
        }

        if (mode_ == RmlFriendPanelMode::Main &&
            event.action == SessionInputAction::PointerButton && !event.pressed &&
            event.clicks >= 2)
        {
            for (std::size_t index = 0; index < rows_.size(); ++index)
            {
                if (!rows_[index].OwnsPointer(host_.HoverElement()))
                    continue;
                const std::size_t row = currentContent_.scroll + index;
                if (row >= currentContent_.rows.size())
                    break;
                changes_.selectedRow = row;
                changes_.action = currentContent_.tab == 0 ? 2 : currentContent_.tab == 1 ? 1 : 4;
                break;
            }
        }
        return ownsPointer || wasMoving || movable_.IsDragging() || wasScrolling ||
               scrollBar_.IsDragging();
    }

    RmlFriendPanelChanges TakeChanges()
    {
        if (positionDirty_.exchange(false, std::memory_order_acq_rel))
        {
            changes_.x = publishedX_.load(std::memory_order_acquire);
            changes_.y = publishedY_.load(std::memory_order_acquire);
        }
        return std::exchange(changes_, {});
    }

    std::optional<RmlTextInputArea> TextInputArea() const
    {
        return visible_ ? host_.FocusedTextInputArea() : std::nullopt;
    }

    bool Prepare(int viewportWidth, int viewportHeight, bool visible,
                 const RmlFriendPanelContent &content)
    {
        if (root_ == nullptr && !visible)
            return true;
        const auto [width, height] = PanelSize(mode_, content.inviteOpen);
        if (!EnsureDocument(viewportWidth, viewportHeight, width, height) ||
            !host_.SetVisible(visible))
        {
            return false;
        }
        const RmlUiScaledViewport viewport = host_.Viewport();
        visible_ = visible;
        if (!visible)
            return host_.CaptureIfDirty(false);

        bool dirty = viewportWidth_ != viewport.width || viewportHeight_ != viewport.height;
        viewportWidth_ = viewport.width;
        viewportHeight_ = viewport.height;
        root_->SetProperty("width", Rml::CreateString("%dpx", width));
        root_->SetProperty("height", Rml::CreateString("%dpx", height));
        movable_.Configure(static_cast<float>(viewport.width), static_cast<float>(viewport.height),
                           static_cast<float>(width), static_cast<float>(height));
        if (!positionSet_)
        {
            movable_.SetPosition(static_cast<float>(content.x), static_cast<float>(content.y));
            positionSet_ = true;
            lastRequestedX_ = content.x;
            lastRequestedY_ = content.y;
            (void)movable_.TakeDirty();
            dirty = true;
        }
        else if (!movable_.IsDragging() &&
                 (content.x != lastRequestedX_ || content.y != lastRequestedY_))
        {
            movable_.SetPosition(static_cast<float>(content.x), static_cast<float>(content.y));
            lastRequestedX_ = content.x;
            lastRequestedY_ = content.y;
            dirty = true;
        }

        if (!(content == currentContent_))
        {
            ApplyContent(content);
            currentContent_ = content;
            dirty = true;
        }
        for (RmlMuButton &button : buttons_)
            dirty = button.SyncVisualState() || dirty;
        for (RmlMuButton &row : rows_)
            dirty = row.SyncVisualState() || dirty;
        return host_.CaptureIfDirty(dirty || movable_.TakeDirty());
    }

    bool Record(LegacyRenderFacade &facade) const
    {
        return host_.Record(facade);
    }

  private:
    bool EnsureDocument(int viewportWidth, int viewportHeight, int contentWidth, int contentHeight)
    {
        if (!host_.Ensure(viewportWidth, viewportHeight, contentWidth, contentHeight))
            return false;
        if (root_ != nullptr)
            return true;

        Rml::ElementDocument &document = *host_.Document();
        const char *rootId = mode_ == RmlFriendPanelMode::Main          ? "friend-main"
                             : mode_ == RmlFriendPanelMode::Chat        ? "friend-chat"
                             : mode_ == RmlFriendPanelMode::WriteLetter ? "friend-write"
                                                                        : "friend-read";
        const char *dragId = mode_ == RmlFriendPanelMode::Main          ? "friend-main-drag"
                             : mode_ == RmlFriendPanelMode::Chat        ? "friend-chat-drag"
                             : mode_ == RmlFriendPanelMode::WriteLetter ? "friend-write-drag"
                                                                        : "friend-read-drag";
        const char *titleId = mode_ == RmlFriendPanelMode::Main          ? "friend-main-title"
                              : mode_ == RmlFriendPanelMode::Chat        ? "friend-chat-title"
                              : mode_ == RmlFriendPanelMode::WriteLetter ? "friend-write-title"
                                                                         : "friend-read-title";
        root_ = document.GetElementById(rootId);
        drag_ = document.GetElementById(dragId);
        title_ = document.GetElementById(titleId);
        if (root_ == nullptr || drag_ == nullptr || title_ == nullptr)
        {
            Release();
            return false;
        }
        root_->SetProperty("display", "block");
        movable_.Bind(*root_, *drag_);

        const bool bound = mode_ == RmlFriendPanelMode::Main          ? BindMain(document)
                           : mode_ == RmlFriendPanelMode::Chat        ? BindChat(document)
                           : mode_ == RmlFriendPanelMode::WriteLetter ? BindWrite(document)
                                                                      : BindRead(document);
        if (!bound)
        {
            Release();
            return false;
        }
        return true;
    }

    bool BindButtons(Rml::ElementDocument &document, std::initializer_list<const char *> ids)
    {
        std::size_t index = 0;
        for (const char *id : ids)
        {
            Rml::Element *const element = document.GetElementById(id);
            if (element == nullptr)
                return false;
            buttons_[index++].Bind(*element);
        }
        return true;
    }

    bool BindMain(Rml::ElementDocument &document)
    {
        if (!BindButtons(document,
                         {"friend-main-close", "friend-tab-0", "friend-tab-1", "friend-tab-2",
                          "friend-refuse", "friend-action-0", "friend-action-1", "friend-action-2",
                          "friend-action-3", "friend-header-0", "friend-header-1",
                          "friend-header-2", "friend-header-3", "friend-header-check"}))
        {
            return false;
        }
        for (std::size_t index = 0; index < 4; ++index)
        {
            labels_[index] =
                document.GetElementById("friend-action-label-" + std::to_string(index));
            headers_[index] = document.GetElementById("friend-header-" + std::to_string(index));
        }
        for (std::size_t index = 0; index < 3; ++index)
        {
            tabLabels_[index] =
                document.GetElementById("friend-tab-label-" + std::to_string(index));
        }
        refuseLabel_ = document.GetElementById("friend-refuse-label");
        refuseMark_ = document.GetElementById("friend-refuse-mark");
        headerCheck_ = document.GetElementById("friend-header-check");
        headerCheckMark_ = document.GetElementById("friend-header-check-mark");
        gridTitle_ = document.GetElementById("friend-grid-title");
        rowContainer_ = document.GetElementById("friend-rows");
        if (refuseLabel_ == nullptr || refuseMark_ == nullptr || headerCheck_ == nullptr ||
            headerCheckMark_ == nullptr || gridTitle_ == nullptr || rowContainer_ == nullptr ||
            !scrollBar_.Bind(document, "friend-scroll"))
        {
            return false;
        }
        for (std::size_t index = 0; index < rows_.size(); ++index)
        {
            Rml::Element *const row =
                document.GetElementById("friend-row-" + std::to_string(index));
            if (row == nullptr || row->GetNumChildren() != 9)
                return false;
            rowElements_[index] = row;
            rowMarks_[index] = row->GetChild(8);
            if (rowMarks_[index]->GetNumChildren() != 2)
                return false;
            rowChecks_[index] = rowMarks_[index]->GetChild(0);
            rowMailIcons_[index] = rowMarks_[index]->GetChild(1);
            rows_[index].Bind(*row);
        }
        return std::all_of(labels_.begin(), labels_.begin() + 4,
                           [](auto *element) { return element != nullptr; }) &&
               std::all_of(headers_.begin(), headers_.end(),
                           [](auto *element) { return element != nullptr; }) &&
               std::all_of(tabLabels_.begin(), tabLabels_.end(),
                           [](auto *element) { return element != nullptr; });
    }

    bool BindChat(Rml::ElementDocument &document)
    {
        if (!BindButtons(document, {"friend-chat-minimize", "friend-chat-close",
                                    "friend-chat-toggle", "friend-chat-invite"}))
        {
            return false;
        }
        labels_[0] = document.GetElementById("friend-chat-toggle-label");
        labels_[1] = document.GetElementById("friend-chat-invite-label");
        messageList_ = document.GetElementById("friend-chat-messages");
        memberList_ = document.GetElementById("friend-chat-members");
        inputs_[0] = rmlui_dynamic_cast<Rml::ElementFormControl *>(
            document.GetElementById("friend-chat-input"));
        for (std::size_t index = 0; index < rows_.size(); ++index)
        {
            rowElements_[index] =
                document.GetElementById("friend-chat-invite-row-" + std::to_string(index));
            if (rowElements_[index] == nullptr)
                return false;
            rows_[index].Bind(*rowElements_[index]);
        }
        return labels_[0] != nullptr && labels_[1] != nullptr && messageList_ != nullptr &&
               memberList_ != nullptr && inputs_[0] != nullptr;
    }

    bool BindWrite(Rml::ElementDocument &document)
    {
        if (!BindButtons(document, {"friend-write-minimize", "friend-write-close-top",
                                    "friend-write-send", "friend-write-close"}))
        {
            return false;
        }
        senderLabel_ = document.GetElementById("friend-write-receiver-label");
        headers_[0] = document.GetElementById("friend-write-subject-label");
        labels_[0] = document.GetElementById("friend-write-send-label");
        labels_[1] = document.GetElementById("friend-write-close-label");
        inputs_[0] = rmlui_dynamic_cast<Rml::ElementFormControl *>(
            document.GetElementById("friend-write-receiver"));
        inputs_[1] = rmlui_dynamic_cast<Rml::ElementFormControl *>(
            document.GetElementById("friend-write-subject"));
        inputs_[2] = rmlui_dynamic_cast<Rml::ElementFormControl *>(
            document.GetElementById("friend-write-body"));
        return senderLabel_ != nullptr && headers_[0] != nullptr && labels_[0] != nullptr &&
               labels_[1] != nullptr &&
               std::all_of(inputs_.begin(), inputs_.end(),
                           [](auto *input) { return input != nullptr; });
    }

    bool BindRead(Rml::ElementDocument &document)
    {
        if (!BindButtons(document,
                         {"friend-read-minimize", "friend-read-close-top", "friend-read-action-0",
                          "friend-read-action-1", "friend-read-action-2", "friend-read-action-3",
                          "friend-read-action-4"}))
        {
            return false;
        }
        senderLabel_ = document.GetElementById("friend-read-sender-label");
        sender_ = document.GetElementById("friend-read-sender");
        readBody_ = document.GetElementById("friend-read-body");
        for (std::size_t index = 0; index < 5; ++index)
        {
            Rml::Element *const action =
                document.GetElementById("friend-read-action-" + std::to_string(index));
            if (action == nullptr || action->GetNumChildren() == 0)
                return false;
            labels_[index] = action->GetChild(0);
        }
        return senderLabel_ != nullptr && sender_ != nullptr && readBody_ != nullptr;
    }

    void ApplyContent(const RmlFriendPanelContent &content)
    {
        SetText(*title_, content.title);
        if (mode_ == RmlFriendPanelMode::Main)
            ApplyMain(content);
        else if (mode_ == RmlFriendPanelMode::Chat)
            ApplyChat(content);
        else if (mode_ == RmlFriendPanelMode::WriteLetter)
            ApplyWrite(content);
        else
            ApplyRead(content);
    }

    void ApplyMain(const RmlFriendPanelContent &content)
    {
        root_->SetClass("mail", content.mailRows);
        root_->SetClass("window-list", content.tab == 2);
        gridTitle_->SetClass("mcFriendTitle", content.tab == 0);
        gridTitle_->SetClass("mcMailTitle", content.tab == 1);
        gridTitle_->SetClass("mcWindowTitle", content.tab == 2);
        for (std::size_t index = 0; index < 3; ++index)
        {
            SetText(*tabLabels_[index], content.tabs[index]);
            buttons_[index + 1].SetVisible(true);
            Rml::Element *const tab = tabLabels_[index]->GetParentNode();
            tab->SetClass("selected", content.tab == static_cast<int>(index));
        }
        SetText(*refuseLabel_, content.refuseLabel);
        SetChecked(*refuseMark_, content.refuseChat);
        buttons_[4].SetVisible(true);
        refuseLabel_->GetParentNode()->SetClass("selected", content.refuseChat);

        for (std::size_t index = 0; index < headers_.size(); ++index)
        {
            SetText(*headers_[index], content.headers[index]);
            buttons_[index + 9].SetVisible(!content.headers[index].empty());
        }
        headerCheck_->SetProperty("display", content.mailRows ? "block" : "none");
        SetChecked(*headerCheckMark_, content.checkAll);
        buttons_[13].SetVisible(content.mailRows);

        for (std::size_t index = 0; index < 4; ++index)
        {
            SetText(*labels_[index], content.labels[index]);
            const bool show = content.tab != 2 || index == 3;
            buttons_[index + 5].SetVisible(show);
        }
        ApplyRows(content);

        RmlMuScrollBarState state;
        state.position = content.scroll;
        state.maximum = content.rows.size() > RmlFriendPanel::MainVisibleRows()
                            ? content.rows.size() - RmlFriendPanel::MainVisibleRows()
                            : 0;
        state.pageSize = std::min(content.rows.size(), RmlFriendPanel::MainVisibleRows());
        state.up = content.scroll == 0 ? ButtonVisualState::Disabled : ButtonVisualState::Up;
        state.down =
            content.scroll >= state.maximum ? ButtonVisualState::Disabled : ButtonVisualState::Up;
        state.thumb = state.maximum == 0 ? ButtonVisualState::Disabled : ButtonVisualState::Up;
        state.trackStep = state.pageSize;
        scrollBar_.Apply(state);
    }

    void ApplyRows(const RmlFriendPanelContent &content)
    {
        for (std::size_t visibleIndex = 0; visibleIndex < rows_.size(); ++visibleIndex)
        {
            if (visibleIndex >= RmlFriendPanel::MainVisibleRows())
            {
                rows_[visibleIndex].SetVisible(false);
                continue;
            }
            const std::size_t index = content.scroll + visibleIndex;
            const bool visible = index < content.rows.size();
            rows_[visibleIndex].SetVisible(true);
            rows_[visibleIndex].SetEnable(visible);
            Rml::Element &row = *rowElements_[visibleIndex];
            row.SetClass("mail", content.mailRows);
            row.SetClass("dataGridItem_Server", content.tab == 0);
            row.SetClass("dataGridItem_Mail_renewal_co", content.tab == 1);
            row.SetClass("dataGridItem_Title", content.tab == 2);
            row.SetClass("empty", !visible);
            row.SetClass("selected", visible && index == content.selectedRow);
            if (!visible)
            {
                row.SetClass("checked", false);
                row.SetClass("read", false);
                row.SetClass("unread", false);
                for (std::size_t field = 0; field < 4; ++field)
                    SetText(*row.GetChild(static_cast<int>(field + 4)), L"");
                continue;
            }
            const RmlFriendRow &data = content.rows[index];
            row.SetClass("checked", data.checked);
            row.SetClass("read", data.read);
            row.SetClass("unread", !data.read);
            for (std::size_t field = 0; field < data.text.size(); ++field)
                SetText(*row.GetChild(static_cast<int>(field + 4)), data.text[field]);
            if (rowMarks_[visibleIndex] != nullptr)
            {
                SetChecked(*rowChecks_[visibleIndex], data.checked);
                rowMailIcons_[visibleIndex]->SetClass("read", data.read);
            }
        }
    }

    void ApplyChat(const RmlFriendPanelContent &content)
    {
        SetText(*labels_[0], content.labels[0]);
        SetText(*labels_[1], content.labels[1]);
        SetLines(*messageList_, content.messages);
        SetLines(*memberList_, content.members);
        root_->SetClass("invite-open", content.inviteOpen);
        buttons_[3].SetVisible(content.inviteOpen);
        buttons_[2].SetEnable(true);
        SyncInput(0, content.body);
        inputs_[0]->SetDisabled(content.inputLocked);
        for (std::size_t index = 0; index < rows_.size(); ++index)
        {
            const bool visible = content.inviteOpen && index < content.invitees.size();
            rows_[index].SetVisible(visible);
            if (!visible)
                continue;
            SetText(*rowElements_[index], content.invitees[index]);
            rowElements_[index]->SetClass("selected", index == content.selectedInvitee);
        }
    }

    void ApplyWrite(const RmlFriendPanelContent &content)
    {
        SetText(*senderLabel_, content.senderLabel);
        SetText(*headers_[0], content.headers[0]);
        SetText(*labels_[0], content.labels[0]);
        SetText(*labels_[1], content.labels[1]);
        SyncInput(0, content.receiver);
        SyncInput(1, content.subject);
        SyncInput(2, content.body);
    }

    void ApplyRead(const RmlFriendPanelContent &content)
    {
        SetText(*senderLabel_, content.senderLabel);
        SetText(*sender_, content.sender);
        SetText(*readBody_, content.body);
        for (std::size_t index = 0; index < 5; ++index)
            SetText(*labels_[index], content.labels[index]);
    }

    void SyncInput(std::size_t index, const std::wstring &value)
    {
        if (inputValues_[index] == value)
            return;
        inputValues_[index] = value;
        inputs_[index]->SetValue(StringUtils::WideToNarrow(value.c_str()));
    }

    void ReadInputChanges()
    {
        if (mode_ == RmlFriendPanelMode::Chat)
        {
            ReadInput(0, changes_.body);
            return;
        }
        if (mode_ != RmlFriendPanelMode::WriteLetter)
            return;
        ReadInput(0, changes_.receiver);
        ReadInput(1, changes_.subject);
        ReadInput(2, changes_.body);
    }

    void ReadInput(std::size_t index, std::optional<std::wstring> &changedValue)
    {
        const std::wstring value = StringUtils::NarrowToWide(inputs_[index]->GetValue().c_str());
        if (inputValues_[index] == value)
            return;
        inputValues_[index] = value;
        changedValue = value;
    }

    void ReadButtonChanges()
    {
        if (mode_ == RmlFriendPanelMode::Main)
            ReadMainButtons();
        else if (mode_ == RmlFriendPanelMode::Chat)
            ReadChatButtons();
        else if (mode_ == RmlFriendPanelMode::WriteLetter)
            ReadWriteButtons();
        else
            ReadReadButtons();
    }

    void ReadMainButtons()
    {
        changes_.close = buttons_[0].IsClick() || changes_.close;
        for (std::size_t index = 0; index < 3; ++index)
            if (buttons_[index + 1].IsClick())
                changes_.tab = static_cast<int>(index);
        changes_.toggleRefuseChat = buttons_[4].IsClick() || changes_.toggleRefuseChat;
        for (std::size_t index = 0; index < 4; ++index)
            if (buttons_[index + 5].IsClick())
                changes_.action = static_cast<int>(index);
        for (std::size_t index = 0; index < 4; ++index)
            if (buttons_[index + 9].IsClick())
                changes_.sortColumn = static_cast<int>(index);
        changes_.toggleCheckAll = buttons_[13].IsClick() || changes_.toggleCheckAll;

        for (std::size_t index = 0; index < rows_.size(); ++index)
        {
            if (!rows_[index].IsClick())
                continue;
            const std::size_t row = currentContent_.scroll + index;
            if (row >= currentContent_.rows.size())
                continue;
            changes_.selectedRow = row;
            if (currentContent_.mailRows && IsDescendantOf(host_.HoverElement(), rowChecks_[index]))
            {
                changes_.checkedRow = row;
            }
            if (currentContent_.tab == 2)
                changes_.action = 4;
        }
        if (const auto scroll = scrollBar_.TakeRequestedPosition())
            changes_.scroll = *scroll;
    }

    void ReadChatButtons()
    {
        changes_.minimize = buttons_[0].IsClick() || changes_.minimize;
        changes_.close = buttons_[1].IsClick() || changes_.close;
        if (buttons_[2].IsClick())
            changes_.action = 0;
        if (buttons_[3].IsClick())
            changes_.action = 1;
        for (std::size_t index = 0; index < rows_.size(); ++index)
        {
            if (!rows_[index].IsClick())
                continue;
            changes_.selectedInvitee = index;
        }
    }

    void ReadWriteButtons()
    {
        changes_.minimize = buttons_[0].IsClick() || changes_.minimize;
        changes_.close = buttons_[1].IsClick() || buttons_[3].IsClick() || changes_.close;
        if (buttons_[2].IsClick())
            changes_.action = 0;
    }

    void ReadReadButtons()
    {
        changes_.minimize = buttons_[0].IsClick() || changes_.minimize;
        changes_.close = buttons_[1].IsClick() || changes_.close;
        for (std::size_t index = 0; index < 5; ++index)
            if (buttons_[index + 2].IsClick())
                changes_.action = static_cast<int>(index);
    }

    const RmlFriendPanelMode mode_;
    std::array<RmlMuButton, 14> &buttons_;
    std::vector<RmlMuButton> &rows_;
    RmlMuScrollBar &scrollBar_;
    RmlDocumentHost host_;
    RmlMuMovablePanel movable_;
    Rml::Element *root_ = nullptr;
    Rml::Element *drag_ = nullptr;
    Rml::Element *title_ = nullptr;
    Rml::Element *rowContainer_ = nullptr;
    Rml::Element *gridTitle_ = nullptr;
    std::vector<Rml::Element *> rowElements_ = std::vector<Rml::Element *>(VisibleRows());
    std::vector<Rml::Element *> rowMarks_ = std::vector<Rml::Element *>(VisibleRows());
    std::vector<Rml::Element *> rowChecks_ = std::vector<Rml::Element *>(VisibleRows());
    std::vector<Rml::Element *> rowMailIcons_ = std::vector<Rml::Element *>(VisibleRows());
    std::array<Rml::Element *, 5> labels_{};
    std::array<Rml::Element *, 4> headers_{};
    std::array<Rml::Element *, 3> tabLabels_{};
    std::array<Rml::ElementFormControl *, 3> inputs_{};
    Rml::Element *refuseLabel_ = nullptr;
    Rml::Element *refuseMark_ = nullptr;
    Rml::Element *headerCheck_ = nullptr;
    Rml::Element *headerCheckMark_ = nullptr;
    Rml::Element *messageList_ = nullptr;
    Rml::Element *memberList_ = nullptr;
    Rml::Element *senderLabel_ = nullptr;
    Rml::Element *sender_ = nullptr;
    Rml::Element *readBody_ = nullptr;
    RmlFriendPanelContent currentContent_{};
    RmlFriendPanelChanges changes_{};
    std::array<std::wstring, 3> inputValues_{};
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
    int lastRequestedX_ = 0;
    int lastRequestedY_ = 0;
    bool visible_ = false;
    bool positionSet_ = false;
    std::atomic<int> publishedX_{0};
    std::atomic<int> publishedY_{0};
    std::atomic<bool> positionDirty_{false};
};

RmlFriendPanel::RmlFriendPanel(SessionKeeper &keeper, RmlFriendPanelMode mode)
    : rows_(VisibleRows()),
      impl_(std::make_unique<Impl>(keeper, mode, *this, buttons_, rows_, scrollBar_))
{
}

RmlFriendPanel::~RmlFriendPanel() = default;

void RmlFriendPanel::Create()
{
    for (RmlMuButton &button : buttons_)
    {
        button.SetEnable(true);
        button.SetVisible(true);
        button.Reset();
    }
    for (RmlMuButton &row : rows_)
    {
        row.SetEnable(true);
        row.SetVisible(false);
        row.Reset();
    }
}

void RmlFriendPanel::Release()
{
    impl_->Release();
}

bool RmlFriendPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}

RmlFriendPanelChanges RmlFriendPanel::TakeChanges()
{
    return impl_->TakeChanges();
}

std::optional<RmlTextInputArea> RmlFriendPanel::TextInputArea() const
{
    return impl_->TextInputArea();
}

bool RmlFriendPanel::PrepareOnWorker(int viewportWidth, int viewportHeight, bool visible,
                                     const RmlFriendPanelContent &content)
{
    return impl_->Prepare(viewportWidth, viewportHeight, visible, content);
}

bool RmlFriendPanel::Record(LegacyRenderFacade &facade) const
{
    return impl_->Record(facade);
}
} // namespace UI::Modern::PC::Friend

namespace UI::Modern::PC::Guild
{
namespace
{
constexpr std::array GuildCreatePanelButtonIds{"btnClose", "btnConfirm", "btnPrev", "btnNext"};
constexpr std::array LabelIds{
    "tfTitle",          "taInfoMent",    "tfGuildName",   "tfGuildMark",      "tfGuildMarkColor",
    "btnConfirm-label", "btnPrev-label", "btnNext-label", "tfCheckGuildName", "tfCheckGuildMaster"};
constexpr std::array GuildCreatePanelGroupIds{"mcInfo", "mcGuildMark", "mcGuildCheck"};
constexpr std::size_t ColorCount = 16;
constexpr int GuildNameLength = 8;
} // namespace
class RmlGuildCreatePanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "Guild", "guild_create.rml")),
          design_(path_, {"Panel-Size", "Panel-Reference", "Panel-Initial"}),
          host_(keeper, "guild-create-" + std::to_string(keeper.Id().RawValue()), path_)
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
        for (auto &color : colors_)
            color.Unbind();
        editor_.Unbind();
        preview_.Unbind();
        movable_.Unbind();
        panel_ = nullptr;
        input_ = nullptr;
        labels_.reset();
        revision_.reset();
        name_.clear();
        previewName_.clear();
        mark_.reset();
        changes_ = {};
        visible_ = positioned_ = focusInput_ = dirty_ = false;
        step_.reset();
        color_.reset();
        host_.Release();
    }
    bool Bind()
    {
        auto &document = *host_.Document();
        panel_ = document.GetElementById("panel");
        auto *drag = document.GetElementById("btnDrag");
        input_ = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
            document.GetElementById("tiGuildNameValue"));
        if (!panel_ || !drag || !input_)
            return false;
        movable_.Bind(*panel_, *drag);
        input_->SetAttribute("maxlength", GuildNameLength);
        for (std::size_t i = 0; i < GuildCreatePanelButtonIds.size(); ++i)
        {
            auto *element = document.GetElementById(GuildCreatePanelButtonIds[i]);
            if (!element)
                return false;
            buttons_[i].Bind(*element);
        }
        for (std::size_t i = 0; i < ColorCount; ++i)
        {
            colorElements_[i] = document.GetElementById("btnColor" + std::to_string(i + 1));
            if (!colorElements_[i])
                return false;
            colors_[i].Bind(*colorElements_[i]);
        }
        auto *editor = document.GetElementById("mcGuildMarkDraw");
        auto *preview = document.GetElementById("mcCheckGuildMark");
        return editor && preview && editor_.Bind(*editor, true) && preview_.Bind(*preview, false);
    }
    void Text(const char *id, const std::wstring &text)
    {
        host_.Document()->GetElementById(id)->SetInnerRML(
            Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(text.c_str())));
    }
    void Color(std::uint8_t color)
    {
        if (color_ == color)
            return;
        if (color_)
            colorElements_[*color_]->SetClass("selected", false);
        colorElements_[color]->SetClass("selected", true);
        editor_.SetColor(color);
        color_ = color;
        dirty_ = true;
    }
    void Apply(const Content &content)
    {
        if (revision_ == content.revision)
            return;
        revision_ = content.revision;
        if (labels_ != content.labels)
        {
            for (std::size_t i = 0; i < LabelIds.size(); ++i)
                Text(LabelIds[i], content.labels[i]);
            labels_ = content.labels;
            dirty_ = true;
        }
        if (name_ != content.name)
        {
            input_->SetValue(StringUtils::WideToNarrow(content.name.c_str()));
            name_ = content.name;
            dirty_ = true;
        }
        if (previewName_ != content.name)
        {
            Text("tfCheckGuildNameValue", content.name);
            previewName_ = content.name;
            dirty_ = true;
        }
        if (mark_ != content.mark)
        {
            editor_.SetPixels(content.mark);
            preview_.SetPixels(content.mark);
            mark_ = content.mark;
            dirty_ = true;
        }
        Color(content.color);
        if (step_ == content.step)
            return;
        step_ = content.step;
        for (std::size_t i = 0; i < GuildCreatePanelGroupIds.size(); ++i)
            host_.Document()
                ->GetElementById(GuildCreatePanelGroupIds[i])
                ->SetClass("mu-hidden", int(content.step) != i);
        buttons_[2].SetVisible(content.step != Step::Info);
        buttons_[3].SetVisible(content.step != Step::Info);
        if (content.step != Step::Edit)
        {
            input_->Blur();
            editor_.CancelPaint();
        }
        focusInput_ = content.step == Step::Edit;
        changes_.create = changes_.previous = changes_.next = false;
        dirty_ = true;
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
        if (visible)
            Apply(content);
        else
        {
            changes_ = {};
            movable_.CancelDrag();
            editor_.CancelPaint();
        }
        const auto viewport = host_.Viewport();
        movable_.Configure(viewport.width, viewport.height, size[0], size[1]);
        if (!positioned_)
        {
            const auto initial = design_.Values(2);
            movable_.SetPosition(initial[0], initial[1]);
            positioned_ = true;
        }
        if (focusInput_ && input_->Focus(true))
        {
            focusInput_ = false;
            dirty_ = true;
        }
        dirty_ = movable_.TakeDirty() || dirty_;
        for (auto &button : buttons_)
            dirty_ = button.SyncVisualState() || dirty_;
        for (auto &button : colors_)
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
    void ReadInput()
    {
        auto value = input_->GetValue();
        // Preserve the native UIOPTION_NOLOCALIZEDCHARACTERS guild-name field.
        const auto previous = value;
        std::erase_if(value, [](unsigned char c) { return c >= 128; });
        if (value != previous)
            input_->SetValue(value);
        auto name = StringUtils::NarrowToWide(value.c_str());
        if (name == name_)
            return;
        name_ = std::move(name);
        changes_.name = name_;
    }
    void Buttons()
    {
        changes_.close = buttons_[0].IsClick() || changes_.close;
        changes_.create = buttons_[1].IsClick() || changes_.create;
        changes_.previous = buttons_[2].IsClick() || changes_.previous;
        changes_.next = buttons_[3].IsClick() || changes_.next;
        changes_.step = *step_;
        for (std::size_t i = 0; i < ColorCount; ++i)
            if (colors_[i].IsClick())
            {
                changes_.color = static_cast<std::uint8_t>(i);
                Color(*changes_.color);
            }
    }
    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!visible_)
            return false;
        const bool dragging = movable_.IsDragging();
        const bool handled = host_.ProcessInput(event);
        const bool painting = editor_.ProcessInput(event, host_.HoverElement());
        if (auto mark = editor_.TakeChanges())
        {
            changes_.mark = mark;
            mark_ = *mark;
            preview_.SetPixels(*mark);
        }
        ReadInput();
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
        if (event.kind == SessionInputEventKind::Key && event.code == SDL_SCANCODE_ESCAPE)
            return false;
        if (event.kind != SessionInputEventKind::Pointer)
            return handled;
        return inside || dragging || movable_.IsDragging() || painting;
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    RmlMuMovablePanel movable_;
    RmlMuPalette editor_, preview_;
    std::array<RmlMuButton, GuildCreatePanelButtonIds.size()> buttons_;
    std::array<RmlMuButton, ColorCount> colors_;
    std::array<Rml::Element *, ColorCount> colorElements_{};
    Rml::Element *panel_ = nullptr;
    Rml::ElementFormControlInput *input_ = nullptr;
    std::array<float, 4> bounds_{};
    std::optional<std::array<std::wstring, LabelIds.size()>> labels_;
    std::wstring name_, previewName_;
    std::optional<RmlMuPalette::Pixels> mark_;
    std::optional<std::uint64_t> revision_;
    std::optional<Step> step_;
    std::optional<std::uint8_t> color_;
    Changes changes_;
    bool visible_ = false, positioned_ = false, focusInput_ = false, dirty_ = false;
};
RmlGuildCreatePanel::RmlGuildCreatePanel(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper))
{
}
RmlGuildCreatePanel::~RmlGuildCreatePanel() = default;
void RmlGuildCreatePanel::Release()
{
    impl_->Release();
}
bool RmlGuildCreatePanel::PrepareOnWorker(int width, int height, bool visible,
                                          const Content &content)
{
    return impl_->Prepare(width, height, visible, content);
}
bool RmlGuildCreatePanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
RmlGuildCreatePanel::Changes RmlGuildCreatePanel::TakeChanges()
{
    return std::exchange(impl_->changes_, {});
}
std::optional<RmlTextInputArea> RmlGuildCreatePanel::TextInputArea() const
{
    return impl_->host_.FocusedTextInputArea();
}
bool RmlGuildCreatePanel::ContainsReferencePointer(int x, int y) const
{
    const auto &rect = impl_->bounds_;
    return impl_->visible_ && x >= rect[0] && y >= rect[1] && x < rect[0] + rect[2] &&
           y < rect[1] + rect[3];
}
bool RmlGuildCreatePanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->panel_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::Guild

namespace UI::Modern::PC::Guild
{
namespace
{
constexpr std::array GuildInfoPanelButtonIds{
    "btnClose",       "btnToggleInfo",        "btnToggleMember",      "btnToggleUnion",
    "btnToggleRival", "btnDisband",           "btnPosition",          "btnClear",
    "btnFire",        "btnUnionDisbandGuild", "btnUnionDisbandUnion", "btnRivalAdd",
    "btnRivalDelete"};
constexpr std::array GuildInfoPanelGroupIds{"mcInfo", "mcMember", "mcUnion", "mcRival",
                                            "mcUnionNone"};
std::string Encode(std::wstring_view text)
{
    return Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(std::wstring(text).c_str()));
}
} // namespace
class RmlGuildInfoPanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "Guild", "guild_info.rml")),
          design_(path_, {"Panel-Size", "Panel-Reference", "Panel-Initial"}),
          host_(keeper, "guild-info-" + std::to_string(keeper.Id().RawValue()), path_)
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
        for (auto &list : lists_)
            list.Unbind();
        palette_.Unbind();
        movable_.Unbind();
        panel_ = nullptr;
        revision_.reset();
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
        for (std::size_t i = 0; i < GuildInfoPanelButtonIds.size(); ++i)
        {
            auto *element = document.GetElementById(GuildInfoPanelButtonIds[i]);
            if (!element)
                return false;
            buttons_[i].Bind(*element);
        }
        return lists_[0].Bind(document, "slMember", "sbMember", "member-row-template") &&
               lists_[1].Bind(document, "slUnion", "sbUnion", "guild-row-template") &&
               lists_[2].Bind(document, "slRival", "sbRival", "rival-row-template") &&
               texts_[0].Bind(document, "taGuildNotice", "sbGuildNotice") &&
               texts_[1].Bind(document, "taUnionMent") && texts_[2].Bind(document, "taGuildInfo") &&
               palette_.Bind(*document.GetElementById("mcGuildMark"), false);
    }
    void Apply(std::uint64_t revision, const Content &content)
    {
        if (revision_ == revision)
            return;
        changes_ = {};
        for (const auto &[id, value] : content.text)
        {
            if (id == "taGuildNotice")
                texts_[0].SetMarkup(Encode(value));
            else if (id == "taUnionMent")
                texts_[1].SetMarkup(Encode(value));
            else if (id == "taGuildInfo")
                texts_[2].SetMarkup(Encode(value));
            else
                host_.Document()->GetElementById(id)->SetInnerRML(Encode(value));
        }
        lists_[0].SetData(content.members);
        lists_[1].SetData(content.alliance);
        lists_[2].SetData(content.rivals);
        lists_[0].Select(content.selectedMember);
        lists_[1].Select(content.selectedAlliance);
        lists_[1].SetPaletteData(content.allianceMarks);
        palette_.SetPixels(content.mark);
        for (std::size_t i = 0; i < GuildInfoPanelGroupIds.size(); ++i)
        {
            bool shown =
                content.hasGuild && (i == 4 ? content.tab == Tab::Alliance && !content.hasAlliance
                                            : int(content.tab) == i);
            if (i == 2)
                shown = shown && content.hasAlliance;
            host_.Document()
                ->GetElementById(GuildInfoPanelGroupIds[i])
                ->SetClass("mu-hidden", !shown);
        }
        for (std::size_t i = 1; i <= 4; ++i)
        {
            buttons_[i].SetVisible(content.hasGuild);
            host_.Document()
                ->GetElementById(GuildInfoPanelButtonIds[i])
                ->SetClass("selected", int(content.tab) == i - 1);
        }
        for (std::size_t i = 5; i < buttons_.size(); ++i)
        {
            buttons_[i].SetEnable(content.enabled[i - 5]);
            buttons_[i].SetVisible(i == 5 || content.master);
        }
        for (auto &button : buttons_)
            button.Reset();
        revision_ = revision;
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
            for (auto &list : lists_)
                list.Apply();
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
    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!visible_)
            return false;
        bool dragging = movable_.IsDragging();
        for (const auto &list : lists_)
            dragging = list.IsDragging() || dragging;
        for (const auto &text : texts_)
            dragging = text.IsDragging() || dragging;
        host_.ProcessInput(event);
        for (auto &list : lists_)
            list.ProcessInput(event, host_.HoverElement());
        changes_.close = buttons_[0].IsClick() || changes_.close;
        for (std::size_t i = 1; i <= 4; ++i)
            if (buttons_[i].IsClick())
                changes_.tab = Tab(i - 1);
        for (std::size_t i = 5; i < buttons_.size(); ++i)
            if (buttons_[i].IsClick())
                changes_.action = Action(i - 5);
        if (auto member = lists_[0].TakeSelection())
            changes_.member = member;
        if (auto alliance = lists_[1].TakeSelection())
            changes_.alliance = alliance;
        changes_.revision = *revision_;
        for (auto &text : texts_)
            text.Apply();
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
    std::array<RmlMuButton, GuildInfoPanelButtonIds.size()> buttons_;
    std::array<RmlMuTextArea, 3> texts_;
    std::array<RmlMuScrollingList, 3> lists_;
    RmlMuPalette palette_;
    Rml::Element *panel_ = nullptr;
    std::array<float, 4> bounds_{};
    std::optional<std::uint64_t> revision_;
    Changes changes_;
    bool visible_ = false, positioned_ = false, dirty_ = false;
};
RmlGuildInfoPanel::RmlGuildInfoPanel(SessionKeeper &keeper) : impl_(std::make_unique<Impl>(keeper))
{
}
RmlGuildInfoPanel::~RmlGuildInfoPanel() = default;
void RmlGuildInfoPanel::Release()
{
    impl_->Release();
}
bool RmlGuildInfoPanel::PrepareOnWorker(int width, int height, bool visible, std::uint64_t revision,
                                        const Content &content)
{
    return impl_->Prepare(width, height, visible, revision, content);
}
bool RmlGuildInfoPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
RmlGuildInfoPanel::Changes RmlGuildInfoPanel::TakeChanges()
{
    return std::exchange(impl_->changes_, {});
}
bool RmlGuildInfoPanel::ContainsReferencePointer(int x, int y) const
{
    const auto &rect = impl_->bounds_;
    return impl_->visible_ && x >= rect[0] && y >= rect[1] && x < rect[0] + rect[2] &&
           y < rect[1] + rect[3];
}
bool RmlGuildInfoPanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->panel_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::Guild

namespace UI::Modern
{
const RmlUiDesign &PartyFrameDesign()
{
    static const RmlUiDesign design("Data/UI/PC/Party/party_frame.rml",
                                    {
                                        "PartyFrame-ReferenceWidth",
                                        "PartyFrame-ReferenceHeight",
                                        "PartyFrame-HeaderWidth",
                                        "PartyFrame-HeaderHeight",
                                        "PartyFrame-MemberX",
                                        "PartyFrame-MemberWidth",
                                        "PartyFrame-MemberHeight",
                                        "PartyFrame-FirstRowY",
                                        "PartyFrame-RowStep",
                                        "PartyFrame-MinimizeX",
                                        "PartyFrame-MinimizeY",
                                        "PartyFrame-MinimizeSize",
                                        "PartyFrame-HpX",
                                        "PartyFrame-HpY",
                                        "PartyFrame-HpWidth",
                                        "PartyFrame-HpHeight",
                                        "PartyFrame-MpX",
                                        "PartyFrame-MpY",
                                        "PartyFrame-MpWidth",
                                        "PartyFrame-MpHeight",
                                        "PartyFrame-OverlayX",
                                        "PartyFrame-OverlayY",
                                        "PartyFrame-OverlayWidth",
                                        "PartyFrame-OverlayHeight",
                                        "PartyFrame-CrownX",
                                        "PartyFrame-CrownY",
                                        "PartyFrame-CrownWidth",
                                        "PartyFrame-CrownHeight",
                                        "PartyFrame-LeaveX",
                                        "PartyFrame-LeaveY",
                                        "PartyFrame-LeaveSize",
                                        "PartyFrame-BackgroundHeight",
                                        "PartyFrame-InitialY",
                                        "PartyFrame-WorldHpBarWidth",
                                        "PartyFrame-WorldHpRaise",
                                        "PartyFrame-WorldHpHoverY",
                                        "PartyFrame-WorldHpTextY",
                                        "PartyFrame-WorldHpTextColor",
                                        "PartyFrame-WorldHpShadowRect",
                                        "PartyFrame-WorldHpFrameRect",
                                        "PartyFrame-WorldHpTrackRect",
                                        "PartyFrame-WorldHpStepRect",
                                        "PartyFrame-WorldHpStepStride",
                                        "PartyFrame-WorldHpShadowColor",
                                        "PartyFrame-WorldHpFrameColor",
                                        "PartyFrame-WorldHpTrackColor",
                                        "PartyFrame-WorldHpStepColor",
                                    });
    return design;
}

namespace
{

bool SameRow(const RmlPartyFrameRow &left, const RmlPartyFrameRow &right) noexcept
{
    return left.currentHp == right.currentHp && left.maximumHp == right.maximumHp &&
           left.outOfViewport == right.outOfViewport && left.leader == right.leader &&
           left.canLeave == right.canLeave && left.leaveButton == right.leaveButton &&
           left.memberButton == right.memberButton && std::wcscmp(left.name, right.name) == 0;
}

bool SameRequest(const RmlPartyFrameRequest &left, const RmlPartyFrameRequest &right) noexcept
{
    if (left.x != right.x || left.y != right.y || left.rowCount != right.rowCount ||
        left.visible != right.visible || left.minimized != right.minimized ||
        left.minimizeButton != right.minimizeButton)
    {
        return false;
    }
    for (int index = 0; index < left.rowCount; ++index)
    {
        if (!SameRow(left.rows[index], right.rows[index]))
        {
            return false;
        }
    }
    return true;
}

void SetPixels(Rml::Element &element, const char *property, float value)
{
    element.SetProperty(property, Rml::CreateString("%.3fpx", value));
}

void SetBox(Rml::Element &element, float x, float y, float width, float height)
{
    SetPixels(element, "left", x);
    SetPixels(element, "top", y);
    SetPixels(element, "width", width);
    SetPixels(element, "height", height);
}

} // namespace

class RmlPartyFrameLayer::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : host_(keeper, "party-frame-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "Party", "party_frame.rml"))
    {
    }

    void Stage(const RmlPartyFrameRequest &request) noexcept
    {
        pending_ = request;
        hasPending_ = true;
    }

    bool Prepare(int viewportWidth, int viewportHeight)
    {
        if (!hasPending_)
        {
            return true;
        }
        hasPending_ = false;
        const bool initialize = !created_;
        if (!created_ && !pending_.visible)
        {
            active_ = pending_;
            return true;
        }
        if (!EnsureDocument(viewportWidth, viewportHeight))
        {
            return false;
        }

        const bool viewportChanged = viewportWidth_ != viewportWidth ||
                                     viewportHeight_ != viewportHeight ||
                                     configuredScale_ != host_.ConfiguredScale();
        const bool layoutChanged = initialize || viewportChanged;
        const bool dirty = layoutChanged || !SameRequest(active_, pending_);
        viewportWidth_ = viewportWidth;
        viewportHeight_ = viewportHeight;
        configuredScale_ = host_.ConfiguredScale();
        if (dirty)
        {
            Apply(pending_, layoutChanged);
        }
        active_ = pending_;
        if (!host_.SetVisible(active_.visible))
        {
            return false;
        }
        return host_.CaptureIfDirty(dirty);
    }

    bool Record(LegacyRenderFacade &facade) const
    {
        return !created_ || host_.Record(facade);
    }

  private:
    bool EnsureDocument(int viewportWidth, int viewportHeight)
    {
        if (!host_.Ensure(viewportWidth, viewportHeight))
        {
            return false;
        }
        if (created_)
        {
            return true;
        }

        Rml::ElementDocument *const document = host_.Document();
        frame_ = document->GetElementById("party-frame");
        dragbar_ = document->GetElementById("party-dragbar");
        minimize_ = document->GetElementById("party-minimize");
        for (int index = 0; index < RmlPartyFrameRequest::RowCapacity; ++index)
        {
            const auto id = [index](const char *name) {
                return Rml::CreateString("party-%s-%d", name, index);
            };
            members_[index] = document->GetElementById(id("member"));
            backgrounds_[index] = document->GetElementById(id("background"));
            channels_[index] = document->GetElementById(id("channel"));
            names_[index] = document->GetElementById(id("name"));
            hpClips_[index] = document->GetElementById(id("hp-clip"));
            hpBars_[index] = document->GetElementById(id("hp"));
            mpClips_[index] = document->GetElementById(id("mp-clip"));
            mpBars_[index] = document->GetElementById(id("mp"));
            overlays_[index] = document->GetElementById(id("overlay"));
            crowns_[index] = document->GetElementById(id("crown"));
            leaves_[index] = document->GetElementById(id("leave"));
        }

        const std::array<Rml::Element *, 3> fixed{frame_, dragbar_, minimize_};
        if (std::find(fixed.begin(), fixed.end(), nullptr) != fixed.end() || HasMissingRowElement())
        {
            host_.Release();
            return false;
        }
        for (int index = 0; index < RmlPartyFrameRequest::RowCapacity; ++index)
        {
            hpProgress_[index].Bind(*hpBars_[index]);
            mpProgress_[index].Bind(*mpBars_[index]);
        }
        created_ = true;
        return true;
    }

    bool HasMissingRowElement() const noexcept
    {
        for (const auto *const collection :
             {&members_, &backgrounds_, &channels_, &names_, &hpClips_, &hpBars_, &mpClips_,
              &mpBars_, &overlays_, &crowns_, &leaves_})
        {
            if (std::find(collection->begin(), collection->end(), nullptr) != collection->end())
            {
                return true;
            }
        }
        return false;
    }

    void ApplyFrame(const RmlPartyFrameRequest &request, bool layoutChanged)
    {
        const bool geometryChanged =
            layoutChanged || request.x != active_.x || request.y != active_.y ||
            request.minimized != active_.minimized || request.rowCount != active_.rowCount;
        if (geometryChanged)
        {
            const float documentWidth = viewportWidth_ / host_.Viewport().scale;
            const float documentHeight = viewportHeight_ / host_.Viewport().scale;
            const float positionScaleX =
                documentWidth / PartyFrameDesign().Number(PartyFrameMetric::ReferenceWidth);
            const float positionScaleY =
                documentHeight / PartyFrameDesign().Number(PartyFrameMetric::ReferenceHeight);
            const float width = request.minimized
                                    ? PartyFrameDesign().Number(PartyFrameMetric::HeaderWidth)
                                    : PartyFrameDesign().Number(PartyFrameMetric::MemberWidth);
            const float height =
                request.minimized
                    ? PartyFrameDesign().Number(PartyFrameMetric::HeaderHeight)
                    : PartyFrameDesign().Number(PartyFrameMetric::FirstRowY) +
                          request.rowCount * PartyFrameDesign().Number(PartyFrameMetric::RowStep);
            const float left =
                std::clamp(request.x * positionScaleX, 0.0F, std::max(0.0F, documentWidth - width));
            const float top = std::clamp(request.y * positionScaleY, 0.0F,
                                         std::max(0.0F, documentHeight - height));
            SetPixels(*frame_, "left", left);
            SetPixels(*frame_, "top", top);
            SetPixels(*frame_, "width", width);
            SetPixels(*frame_, "height", height);
        }
        if (layoutChanged)
        {
            SetBox(*dragbar_, 0.0F, 0.0F, PartyFrameDesign().Number(PartyFrameMetric::HeaderWidth),
                   PartyFrameDesign().Number(PartyFrameMetric::HeaderHeight));
            SetBox(*minimize_, PartyFrameDesign().Number(PartyFrameMetric::MinimizeX),
                   PartyFrameDesign().Number(PartyFrameMetric::MinimizeY),
                   PartyFrameDesign().Number(PartyFrameMetric::MinimizeSize),
                   PartyFrameDesign().Number(PartyFrameMetric::MinimizeSize));
        }
        if (layoutChanged || request.visible != active_.visible)
        {
            frame_->SetProperty("display", request.visible ? "block" : "none");
        }
        if (layoutChanged || request.minimized != active_.minimized)
        {
            minimize_->SetClass("restore", request.minimized);
        }
        if (layoutChanged || request.minimizeButton != active_.minimizeButton)
        {
            ApplyRmlMuButtonVisualState(*minimize_, request.minimizeButton);
        }
    }

    void LayoutRow(int index)
    {
        SetBox(*members_[index], PartyFrameDesign().Number(PartyFrameMetric::MemberX),
               PartyFrameDesign().Number(PartyFrameMetric::FirstRowY) +
                   index * PartyFrameDesign().Number(PartyFrameMetric::RowStep),
               PartyFrameDesign().Number(PartyFrameMetric::MemberWidth),
               PartyFrameDesign().Number(PartyFrameMetric::MemberHeight));
        SetBox(*backgrounds_[index], 0.0F, 0.0F,
               PartyFrameDesign().Number(PartyFrameMetric::MemberWidth),
               PartyFrameDesign().Number(PartyFrameMetric::BackgroundHeight));
        channels_[index]->SetInnerRML("");
        SetBox(*hpClips_[index], PartyFrameDesign().Number(PartyFrameMetric::HpX),
               PartyFrameDesign().Number(PartyFrameMetric::HpY),
               PartyFrameDesign().Number(PartyFrameMetric::HpWidth),
               PartyFrameDesign().Number(PartyFrameMetric::HpHeight));
        SetBox(*mpClips_[index], PartyFrameDesign().Number(PartyFrameMetric::MpX),
               PartyFrameDesign().Number(PartyFrameMetric::MpY),
               PartyFrameDesign().Number(PartyFrameMetric::MpWidth),
               PartyFrameDesign().Number(PartyFrameMetric::MpHeight));
        mpProgress_[index].SetProgress(1, 1);
        SetBox(*overlays_[index], PartyFrameDesign().Number(PartyFrameMetric::OverlayX),
               PartyFrameDesign().Number(PartyFrameMetric::OverlayY),
               PartyFrameDesign().Number(PartyFrameMetric::OverlayWidth),
               PartyFrameDesign().Number(PartyFrameMetric::OverlayHeight));
        SetBox(*crowns_[index], PartyFrameDesign().Number(PartyFrameMetric::CrownX),
               PartyFrameDesign().Number(PartyFrameMetric::CrownY),
               PartyFrameDesign().Number(PartyFrameMetric::CrownWidth),
               PartyFrameDesign().Number(PartyFrameMetric::CrownHeight));
        SetBox(*leaves_[index], PartyFrameDesign().Number(PartyFrameMetric::LeaveX),
               PartyFrameDesign().Number(PartyFrameMetric::LeaveY),
               PartyFrameDesign().Number(PartyFrameMetric::LeaveSize),
               PartyFrameDesign().Number(PartyFrameMetric::LeaveSize));
    }

    void ApplyRow(int index, const RmlPartyFrameRequest &request, bool layoutChanged)
    {
        const bool visible = !request.minimized && index < request.rowCount;
        const bool wasVisible = !active_.minimized && index < active_.rowCount;
        const bool initialize = layoutChanged || !wasVisible;
        if (layoutChanged || visible != wasVisible)
        {
            members_[index]->SetProperty("display", visible ? "block" : "none");
        }
        if (!visible)
        {
            return;
        }

        const RmlPartyFrameRow &row = request.rows[index];
        const RmlPartyFrameRow &previous = active_.rows[index];
        if (initialize || row.memberButton != previous.memberButton)
            ApplyRmlMuButtonVisualState(*members_[index], row.memberButton);
        if (initialize)
        {
            LayoutRow(index);
        }
        if (initialize || std::wcscmp(row.name, previous.name) != 0)
        {
            names_[index]->SetInnerRML(
                Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(row.name)));
        }
        if (initialize || row.currentHp != previous.currentHp ||
            row.maximumHp != previous.maximumHp)
        {
            hpProgress_[index].SetProgress(row.currentHp, row.maximumHp);
        }
        if (initialize || row.outOfViewport != previous.outOfViewport)
        {
            overlays_[index]->SetClass("distant", row.outOfViewport);
            overlays_[index]->SetProperty("display", row.outOfViewport ? "block" : "none");
        }
        if (initialize || row.leader != previous.leader)
        {
            crowns_[index]->SetClass("leader", row.leader);
        }
        if (initialize || row.canLeave != previous.canLeave)
        {
            leaves_[index]->SetProperty("display", row.canLeave ? "block" : "none");
        }
        if (initialize || row.leaveButton != previous.leaveButton)
        {
            ApplyRmlMuButtonVisualState(*leaves_[index], row.leaveButton);
        }
    }

    void Apply(const RmlPartyFrameRequest &request, bool layoutChanged)
    {
        ApplyFrame(request, layoutChanged);
        for (int index = 0; index < RmlPartyFrameRequest::RowCapacity; ++index)
        {
            ApplyRow(index, request, layoutChanged);
        }
    }

    std::array<RmlMuProgressBar, RmlPartyFrameRequest::RowCapacity> hpProgress_{};
    std::array<RmlMuProgressBar, RmlPartyFrameRequest::RowCapacity> mpProgress_{};
    RmlDocumentHost host_;
    Rml::Element *frame_ = nullptr;
    Rml::Element *dragbar_ = nullptr;
    Rml::Element *minimize_ = nullptr;
    std::array<Rml::Element *, RmlPartyFrameRequest::RowCapacity> members_{};
    std::array<Rml::Element *, RmlPartyFrameRequest::RowCapacity> backgrounds_{};
    std::array<Rml::Element *, RmlPartyFrameRequest::RowCapacity> channels_{};
    std::array<Rml::Element *, RmlPartyFrameRequest::RowCapacity> names_{};
    std::array<Rml::Element *, RmlPartyFrameRequest::RowCapacity> hpClips_{};
    std::array<Rml::Element *, RmlPartyFrameRequest::RowCapacity> hpBars_{};
    std::array<Rml::Element *, RmlPartyFrameRequest::RowCapacity> mpClips_{};
    std::array<Rml::Element *, RmlPartyFrameRequest::RowCapacity> mpBars_{};
    std::array<Rml::Element *, RmlPartyFrameRequest::RowCapacity> overlays_{};
    std::array<Rml::Element *, RmlPartyFrameRequest::RowCapacity> crowns_{};
    std::array<Rml::Element *, RmlPartyFrameRequest::RowCapacity> leaves_{};
    RmlPartyFrameRequest active_{};
    RmlPartyFrameRequest pending_{};
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
    float configuredScale_ = 0.0F;
    bool created_ = false;
    bool hasPending_ = false;
};

RmlPartyFrameLayer::RmlPartyFrameLayer(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper))
{
}

RmlPartyFrameLayer::~RmlPartyFrameLayer() = default;

void RmlPartyFrameLayer::Stage(const RmlPartyFrameRequest &request) noexcept
{
    impl_->Stage(request);
}

bool RmlPartyFrameLayer::PrepareOnWorker(int viewportWidth, int viewportHeight)
{
    return impl_->Prepare(viewportWidth, viewportHeight);
}

bool RmlPartyFrameLayer::Record(LegacyRenderFacade &facade) const
{
    return impl_->Record(facade);
}
} // namespace UI::Modern

using namespace SEASON3B;

bool CPartyManager::Render()
{
    return true;
}

void CNewUIGuildInfoWindow::SetPos(int, int)
{
}

void CNewUIGuildInfoWindow::StageContent()
{
    const bool hasGuild = Hero->GuildStatus != G_NONE && Hero->GuildMarkIndex >= 0;
    const std::wstring guild = hasGuild ? GuildMark[Hero->GuildMarkIndex].GuildName : L"";
    const std::wstring unionName = hasGuild ? GuildMark[Hero->GuildMarkIndex].UnionName : L"";
    StageKey key{dataRevision_,
                 tab_,
                 selectedMember_,
                 selectedAlliance_,
                 Hero->GuildMarkIndex,
                 Hero->GuildStatus,
                 GuildTotalScore,
                 g_nGuildMemberCount,
                 CharacterAttribute->Level,
                 CharacterAttribute->Charisma,
                 CharacterAttribute->Class,
                 guild,
                 unionName,
                 I18N::GetCurrentLocale()};
    if (staged_ == key)
        return;
    staged_ = std::move(key);
    Panel::Content next;
    next.tab = tab_;
    next.hasGuild = hasGuild;
    next.hasAlliance = !unionName.empty();
    next.master = hasGuild && Hero->GuildStatus == G_MASTER;
    next.selectedMember = selectedMember_;
    next.selectedAlliance = selectedAlliance_;
    auto &text = next.text;
    text = {{"tfTitle", I18N::Game::Guild},
            {"btnToggleInfo-label", I18N::Game::Guild},
            {"btnToggleMember-label", I18N::Game::Members},
            {"btnToggleUnion-label", I18N::Game::Alliance},
            {"btnToggleRival-label", I18N::Game::HostilityGuild},
            {"btnPosition-label", I18N::Game::Position},
            {"btnClear-label", I18N::Game::Dissolve},
            {"btnFire-label", I18N::Game::Release},
            {"btnDisband-label", next.master ? I18N::Game::Disband : I18N::Game::Leave},
            {"btnUnionDisbandGuild-label", I18N::Game::DisbandAlliance},
            {"btnUnionDisbandUnion-label", I18N::Game::DisbandGuildAlliance},
            {"btnRivalAdd-label", I18N::Game::HostilityGuild},
            {"btnRivalDelete-label", I18N::Game::Cancel},
            {"tfMemberName", I18N::Game::Name},
            {"tfMemberPosition", I18N::Game::Position},
            {"tfMemberServer", I18N::Game::Server},
            {"tfUnionName", I18N::Game::Guild},
            {"tfUnionMemNum", I18N::Game::Members},
            {"tfRivalName", I18N::Game::HostilityGuild},
            {"tfNotice", I18N::Game::GuildAnnouncement},
            {"tfGuildName", guild},
            {"tfGuildPoint", I18N::Game::Score},
            {"tfGuildPointValue", std::to_wstring(GuildTotalScore)},
            {"tfGuildMemberNum", I18N::Game::Members},
            {"tfRivalGuild", I18N::Game::HostilityGuild},
            {"tfRivalGuildValue", rival_.empty() ? std::wstring(I18N::Game::None) : rival_},
            {"taGuildNotice", notice_}};
    text["tfGuildMemberNumValue"] = std::to_wstring(g_nGuildMemberCount);
    if (next.master)
    {
        int capacity = CharacterAttribute->Level / 10;
        if (gCharacterManager.GetBaseClass(CharacterAttribute->Class) == CLASS_DARK_LORD)
            capacity = std::min(80, capacity + CharacterAttribute->Charisma / 10);
        text["tfGuildMemberNumValue"] += L" / " + std::to_wstring(capacity);
    }
    text["taGuildInfo"] = hasGuild ? guild
                                   : std::wstring(I18N::Game::TypeGuildInFrontOf) + L"\n" +
                                         I18N::Game::TheGuildMasterYouWantToJoin + L"\n" +
                                         I18N::Game::AndYouCanJoinTheGuild;
    text["taUnionMent"] =
        std::wstring(I18N::Game::ToMakeTheAlliance) + L"\n" + I18N::Game::FaceTheGuildMaster +
        L"\n" + I18N::Game::OfDesiredGuildForGuildAlliance + L"\n" +
        I18N::Game::EnterAllianceOrGuildAlliance + L"\n" + I18N::Game::ButtonInCommandWindow +
        L"\n\n" + I18N::Game::IfTheOppositeIsNotAGuild + L"\n" +
        I18N::Game::AllianceOppositeAllianceShould + L"\n" +
        I18N::Game::BeTheMainAllianceForCreating + L"\n" + I18N::Game::GuildAllianceRequestThe +
        L"\n" + I18N::Game::RegistrationToOppositeAlliance + L"\n" +
        I18N::Game::IfTheOppositeIsGuildAlliance;
    if (hasGuild)
        std::copy_n(GuildMark[Hero->GuildMarkIndex].Mark, next.mark.size(), next.mark.begin());
    for (const auto &member : members_)
    {
        const wchar_t *rank = member.role == G_MASTER          ? I18N::Game::Master
                              : member.role == G_SUB_MASTER    ? I18N::Game::AssistM
                              : member.role == G_BATTLE_MASTER ? I18N::Game::BattleM
                                                               : L"";
        next.members.push_back(
            {member.name, rank, member.server == 255 ? L"" : std::to_wstring(member.server + 1)});
    }
    for (const auto &entry : alliance_)
    {
        next.alliance.push_back({entry.name, std::to_wstring(entry.count)});
        next.allianceMarks.push_back(entry.mark);
    }
    if (!rival_.empty())
        next.rivals.push_back({rival_});
    const bool selected = selectedMember_ && members_[*selectedMember_].role != G_MASTER;
    const bool canLeave =
        hasGuild && std::any_of(members_.begin(), members_.end(),
                                [&](const auto &member) { return member.name == Hero->ID; });
    next.enabled = {canLeave,
                    next.master && selected,
                    next.master && selected &&
                        (members_[*selectedMember_].role == G_SUB_MASTER ||
                         members_[*selectedMember_].role == G_BATTLE_MASTER),
                    next.master && selected,
                    next.master && selectedAlliance_ && alliance_[*selectedAlliance_].name != guild,
                    next.master && next.hasAlliance,
                    next.master,
                    next.master && !rival_.empty()};
    content_ = std::move(next);
    ++revision_;
}

bool CNewUIGuildInfoWindow::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}

bool CNewUIGuildInfoWindow::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_, revision_, content_);
}

void CNewUIGuildMakeWindow::SetPos(int x, int y)
{
    position_ = {x, y};
}

void CNewUIGuildMakeWindow::StageLabels()
{
    const std::string locale = I18N::GetCurrentLocale();
    if (locale_ == locale)
        return;
    content_.labels = {I18N::Game::Guild,       I18N::Game::DoYouWishToBeTheGuildMaster,
                       I18N::Game::NAME,        I18N::Game::GuildMark,
                       I18N::Game::ChooseColor, I18N::Game::CreateGuild,
                       I18N::Game::Back,        I18N::Game::Next,
                       I18N::Game::NAME,        Hero->ID};
    locale_ = locale;
    ++content_.revision;
}

void CNewUIGuildMakeWindow::ApplyEdits(const Panel::Changes &changes)
{
    if (changes.name || changes.mark || changes.color)
        ++content_.revision;
    if (changes.name)
    {
        content_.name = *changes.name;
        wcsncpy_s(GuildMark[MARK_EDIT].GuildName, content_.name.c_str(), _TRUNCATE);
    }
    if (changes.mark)
    {
        content_.mark = *changes.mark;
        std::copy(content_.mark.begin(), content_.mark.end(), GuildMark[MARK_EDIT].Mark);
        g_GuildCache.PrepareMark(MARK_EDIT);
    }
    if (changes.color)
        SelectMarkColor = content_.color = *changes.color;
}

bool CNewUIGuildMakeWindow::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}

bool CNewUIGuildMakeWindow::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_, content_);
}

using UI::Chat::WHISPER_ID_SLOTS;

void SessionRenderUnit::RenderList()
{
    for (int i = 0; i < WHISPER_ID_SLOTS; ++i)
    {
        g_RenderText.SetTextColor(255, 255, 255, 255);
        g_RenderText.SetBgColor(0, 0, 0, 255);
        g_RenderText.RenderText(100, 100 + (i * 10), WhisperRegistID[i]);
    }
}

void SessionLegacyCalls::RenderList()
{
    sessionKeeper_.Renderer()->RenderList();
}

// Construction/Destruction

bool SEASON3B::CNewUIFriendWindow::Render()
{
    return m_pFriendWindowMgr == nullptr ||
           m_pFriendWindowMgr->RecordModernUi(m_renderer.LegacyRender());
}

bool SEASON3B::CNewUIFriendWindow::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight)
{
    const bool visible = IsVisible();
    return m_pFriendWindowMgr == nullptr ||
           m_pFriendWindowMgr->PrepareModernUiOnWorker(viewportWidth, viewportHeight, visible);
}

void RenderColor(float x, float y, float Width, float Height, float Alpha, int Flag);

bool CUIChatWindow::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight, bool visible)
{
    using namespace UI::Modern::PC::Friend;
    RmlFriendPanelContent content;
    content.x = GetPosition_x();
    content.y = GetPosition_y();
    content.title = GetTitle();
    content.labels[0] = m_iShowType >= 2 ? I18N::Game::CloseInvitation : I18N::Game::Invite;
    content.labels[1] = I18N::Game::Invite;
    content.inviteOpen = m_iShowType >= 2;
    content.inputLocked = m_TextInputBox.IsLocked() == TRUE;
    content.body = m_TextInputBox.DisplayTextForRetainedUi();
    content.selectedInvitee = m_ModernInviteSelection;
    const auto &chatLines = m_ChatListBox.Items();
    const std::size_t visibleChatLines = RmlFriendPanel::ChatVisibleLines();
    const std::size_t firstChatLine =
        chatLines.size() > visibleChatLines ? chatLines.size() - visibleChatLines : 0;
    for (std::size_t i = firstChatLine; i < chatLines.size(); ++i)
    {
        const WHISPER_TEXT &line = chatLines[i];
        std::wstring text;
        if (line.m_szID[0] != L'\0')
            text = std::wstring(line.m_szID) + L": ";
        text += line.m_szText;
        content.messages.emplace_back(std::move(text));
    }
    for (const GUILDLIST_TEXT &member : m_PalListBox.Items())
        content.members.emplace_back(member.m_szID);
    for (const GUILDLIST_TEXT &invitee : m_InvitePalListBox.Items())
        content.invitees.emplace_back(invitee.m_szID);
    return m_ModernPanel.PrepareOnWorker(viewportWidth, viewportHeight, visible, content);
}

bool CUIChatWindow::RecordModernUi(LegacyRenderFacade &facade) const
{
    return m_ModernPanel.Record(facade);
}

void CUIChatWindow::RenderSub()
{
    EnableAlphaTest();
    if (GetState() == UISTATE_MOVE || GetState() == UISTATE_RESIZE)
    {
        m_ChatListBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_PalListBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_InvitePalListBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_InviteButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_CloseInviteButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_TextInputBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);

        if (GetState() == UISTATE_RESIZE)
        {
            m_ChatListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
            m_PalListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
            m_InvitePalListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
        }
    }

    m_ChatListBox.Render();
    if (m_PalListBox.GetLineNum() > 2 || m_iShowType >= 2)
    {
        m_PalListBox.Render();
    }
    if (m_iShowType >= 2)
    {
        m_InvitePalListBox.Render();
        EnableAlphaTest();
        RenderWindowVLine((float)(RPos_x(0) + RWidth() - 160), (float)RPos_y(0),
                          (float)RHeight() - 16);
    }
    if (m_PalListBox.GetLineNum() > 2 || m_iShowType >= 2)
    {
        RenderWindowVLine((float)(RPos_x(0) + RWidth() - 80), (float)RPos_y(0),
                          (float)RHeight() - 16);
    }

    EnableAlphaTest();
    SetLineColor(2);
    RenderColor((float)RPos_x(0), (float)RPos_y(0) + RHeight() - 16, (float)RWidth(), 1.0f);
    SetLineColor(7);
    RenderColor((float)RPos_x(0), (float)RPos_y(0) + RHeight() - 15, (float)RWidth(), 15);
    EndRenderColor();

    m_InviteButton.Render();
    m_TextInputBox.Render();
    if (m_iShowType >= 2)
        m_CloseInviteButton.Render();
    DisableAlphaBlend();
}

extern void MoveCharacter(CHARACTER *c, OBJECT *o);

void CUIFriendListTabWindow::RenderSub()
{
    if (GetState() == UISTATE_MOVE || GetState() == UISTATE_RESIZE)
    {
        m_PalListBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);

        m_AddFriendButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_DelFriendButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_TalkButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_LetterButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);

        if (GetState() == UISTATE_RESIZE)
        {
            m_PalListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
        }
    }

    EnableAlphaTest();
    SetLineColor(7);
    RenderColor((float)RPos_x(0), (float)RPos_y(18 + m_PalListBox.GetHeight()), (float)RWidth(),
                (float)RHeight() - m_PalListBox.GetHeight() - 18);
    EndRenderColor();
    DisableAlphaBlend();

    m_PalListBox.Render();

    m_AddFriendButton.Render();
    m_DelFriendButton.Render();
    m_TalkButton.Render();
    m_LetterButton.Render();

    EnableAlphaTest();
    SetLineColor(2);
    RenderColor((float)RPos_x(0), (float)RPos_y(16), (float)RWidth(), 1.0f);
    SetLineColor(5);
    RenderColor((float)RPos_x(0), (float)RPos_y(0), (float)RWidth(), 16);
    SetLineColor(2);
    RenderColor((float)RPos_x(0), (float)RPos_y(17 + m_PalListBox.GetHeight()), (float)RWidth(),
                1.0f);
    RenderColor((float)RPos_x(0) + m_PalListBox.GetColumnPos_x(1), (float)RPos_y(17), 1.0f,
                (float)RHeight() - 22 - 17);
    SetLineColor(14);
    RenderColor((float)RPos_x(0) + m_PalListBox.GetColumnPos_x(1), (float)RPos_y(3), 1.0f, 10);
    EndRenderColor();

    g_RenderText.SetBgColor(0);

    if (CheckMouseIn(RPos_x(0) + m_PalListBox.GetColumnPos_x(0), RPos_y(0),
                     m_PalListBox.GetColumnWidth(0), 19) == TRUE ||
        g_pFriendList->GetCurrentSortType() == 0)
    {
        g_RenderText.SetTextColor(255, 255, 255, 255);
        g_RenderText.RenderText(RPos_x(4) + m_PalListBox.GetColumnPos_x(0), RPos_y(3),
                                I18N::Game::FriendSName);
        g_RenderText.SetTextColor(230, 220, 200, 255);
    }
    else
        g_RenderText.RenderText(RPos_x(4) + m_PalListBox.GetColumnPos_x(0), RPos_y(3),
                                I18N::Game::FriendSName);

    if (CheckMouseIn(RPos_x(0) + m_PalListBox.GetColumnPos_x(1), RPos_y(0),
                     m_PalListBox.GetColumnWidth(1), 19) == TRUE ||
        g_pFriendList->GetCurrentSortType() == 1)
    {
        g_RenderText.SetTextColor(255, 255, 255, 255);
        g_RenderText.RenderText(RPos_x(4) + m_PalListBox.GetColumnPos_x(1), RPos_y(3),
                                I18N::Game::Server);
        g_RenderText.SetTextColor(230, 220, 200, 255);
    }
    else
        g_RenderText.RenderText(RPos_x(4) + m_PalListBox.GetColumnPos_x(1), RPos_y(3),
                                I18N::Game::Server);

    DisableAlphaBlend();
}

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

void CUIChatRoomListTabWindow::RenderSub()
{
    if (GetState() == UISTATE_MOVE || GetState() == UISTATE_RESIZE)
    {
        m_WindowListBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_HideAllButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);

        if (GetState() == UISTATE_RESIZE)
        {
            m_WindowListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
        }
    }

    EnableAlphaTest();
    SetLineColor(7);
    RenderColor((float)RPos_x(0), (float)RPos_y(18 + m_WindowListBox.GetHeight()), (float)RWidth(),
                (float)RHeight() - m_WindowListBox.GetHeight() - 18);
    EndRenderColor();

    m_WindowListBox.Render();
    m_HideAllButton.Render();

    EnableAlphaTest();
    SetLineColor(2);
    RenderColor((float)RPos_x(0), (float)RPos_y(0) + 16, (float)RWidth(), 1.0f);
    SetLineColor(5);
    RenderColor((float)RPos_x(0), (float)RPos_y(0), (float)RWidth(), 16);
    SetLineColor(2);
    RenderColor((float)RPos_x(0), (float)RPos_y(17 + m_WindowListBox.GetHeight()), (float)RWidth(),
                1.0f);
    EndRenderColor();

    EnableAlphaTest();

    g_RenderText.SetTextColor(230, 220, 200, 255);
    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.RenderText(RPos_x(8), RPos_y(3), I18N::Game::WindowTitle);
    DisableAlphaBlend();
}

bool CUIFriendWindow::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight, bool visible)
{
    using namespace UI::Modern::PC::Friend;
    RmlFriendPanelContent content;
    content.x = GetPosition_x();
    content.y = GetPosition_y();
    content.title = GetTitle();
    content.tabs = {I18N::Game::FriendsList, I18N::Game::LetterBox, I18N::Game::WindowList};
    content.refuseLabel = I18N::Game::RefuseChat;
    content.tab = m_iTabIndex;
    content.refuseChat = g_pWindowMgr->GetChatReject() == TRUE;
    content.scroll = m_ModernScroll;

    if (m_iTabIndex == 0)
    {
        content.labels = {I18N::Game::AddFriend, I18N::Game::DeleteFriend, I18N::Game::Chat,
                          I18N::Game::Write, L""};
        content.headers[0] = I18N::Game::FriendSName;
        content.headers[1] = I18N::Game::Server;
        content.selectedRow = m_FriendListWnd.ModernSelectedRow();
        for (const GUILDLIST_TEXT &friendRow : m_FriendListWnd.ModernRows())
        {
            RmlFriendRow row;
            row.text[0] = friendRow.m_szID;
            wchar_t server[MAX_TEXT_LENGTH + 1]{};
            if (friendRow.m_Server == 0xFD)
                mu_swprintf(server, I18N::Game::CannotUse);
            else if (friendRow.m_Server >= 0xFC)
                mu_swprintf(server, I18N::Game::Offline1039);
            else
                mu_swprintf(server, I18N::Game::_2dServer, friendRow.m_Server + 1);
            row.text[1] = server;
            content.rows.emplace_back(std::move(row));
        }
    }
    else if (m_iTabIndex == 1)
    {
        content.labels = {I18N::Game::Write, I18N::Game::Read, I18N::Game::Reply,
                          I18N::Game::Delete, L""};
        content.headers = {L"", I18N::Game::Sender, I18N::Game::DateRcvd, I18N::Game::Title1030};
        content.mailRows = true;
        content.checkAll = m_LetterBoxWnd.ModernCheckAll();
        content.selectedRow = m_LetterBoxWnd.ModernSelectedRow();
        for (const LETTERLIST_TEXT &letter : m_LetterBoxWnd.ModernRows())
        {
            RmlFriendRow row;
            row.text[1] = letter.m_szID;
            row.text[2] = letter.m_szDate;
            row.text[3] = letter.m_szText;
            row.checked = letter.m_bIsSelected == TRUE;
            row.read = letter.m_bIsRead == TRUE;
            content.rows.emplace_back(std::move(row));
        }
    }
    else
    {
        content.labels[3] = I18N::Game::HideAll;
        content.headers[0] = I18N::Game::WindowTitle;
        content.selectedRow = m_ChatRoomListWnd.ModernSelectedRow();
        for (const WINDOWLIST_TEXT &window : m_ChatRoomListWnd.ModernRows())
        {
            RmlFriendRow row;
            row.text[0] = window.m_szTitle;
            content.rows.emplace_back(std::move(row));
        }
    }

    const std::size_t maximumScroll = content.rows.size() > RmlFriendPanel::MainVisibleRows()
                                          ? content.rows.size() - RmlFriendPanel::MainVisibleRows()
                                          : 0;
    content.scroll = std::min(content.scroll, maximumScroll);
    return m_ModernPanel.PrepareOnWorker(viewportWidth, viewportHeight, visible, content);
}

bool CUIFriendWindow::RecordModernUi(LegacyRenderFacade &facade) const
{
    return m_ModernPanel.Record(facade);
}

void CUIFriendWindow::RenderSub()
{
    if (GetState() == UISTATE_MOVE || GetState() == UISTATE_RESIZE)
    {
        m_FriendListWnd.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_ChatRoomListWnd.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_LetterBoxWnd.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_FriendListWnd.SetState(UISTATE_MOVE);
        m_ChatRoomListWnd.SetState(UISTATE_MOVE);
        m_LetterBoxWnd.SetState(UISTATE_MOVE);

        if (GetState() == UISTATE_RESIZE)
        {
            m_FriendListWnd.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
            m_ChatRoomListWnd.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
            m_LetterBoxWnd.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
            m_FriendListWnd.SetState(UISTATE_RESIZE);
            m_ChatRoomListWnd.SetState(UISTATE_RESIZE);
            m_LetterBoxWnd.SetState(UISTATE_RESIZE);
        }
    }

    switch (m_iTabIndex)
    {
    case 0:
        m_FriendListWnd.Render();
        break;
    case 1:
        m_LetterBoxWnd.Render();
        break;
    case 2:
        m_ChatRoomListWnd.Render();
        break;
    default:
        break;
    }

    EnableAlphaTest();

    SetLineColor(7);
    RenderColor((float)RPos_x(0), (float)RPos_y(0), (float)RWidth(), (float)20);
    RenderTabLine(RPos_x(0), RPos_y(2), 53, 19, 3, m_iTabIndex);
    SetLineColor(2);
    RenderColor(float(RPos_x(53 * 3)), float(RPos_y(2) + 19 - 1), float(RWidth() - 53 * 3),
                (float)1);
    EndRenderColor();

    SIZE TextSize;
    int TextLen;
    g_RenderText.SetBgColor(0);

    if (m_FriendListWnd.GetTitle() != NULL)
    {
        if (m_iTabIndex == 0 || m_iTabMouseOverIndex == 0)
        {
            g_RenderText.SetTextColor(255, 255, 255, 255);
        }
        else
        {
            g_RenderText.SetTextColor(230, 220, 200, 255);
        }

        TextLen = lstrlen(m_FriendListWnd.GetTitle());

        g_RenderText.MeasureText(m_FriendListWnd.GetTitle(), TextLen, &TextSize);
        g_RenderText.RenderText(RPos_x(0) + (52 - (float)TextSize.cx / g_fScreenRate_x + 0.5f) / 2,
                                RPos_y(0) + (24 - (float)TextSize.cy / g_fScreenRate_y + 0.5f) / 2,
                                m_FriendListWnd.GetTitle());
    }
    if (m_LetterBoxWnd.GetTitle() != NULL)
    {
        if (m_iTabIndex == 1 || m_iTabMouseOverIndex == 1)
        {
            g_RenderText.SetTextColor(255, 255, 255, 255);
        }
        else
        {
            g_RenderText.SetTextColor(230, 220, 200, 255);
        }
        TextLen = lstrlen(m_LetterBoxWnd.GetTitle());

        g_RenderText.MeasureText(m_LetterBoxWnd.GetTitle(), TextLen, &TextSize);
        g_RenderText.RenderText(RPos_x(54) + (52 - (float)TextSize.cx / g_fScreenRate_x + 0.5f) / 2,
                                RPos_y(0) + (24 - (float)TextSize.cy / g_fScreenRate_y + 0.5f) / 2,
                                m_LetterBoxWnd.GetTitle());
    }
    if (m_ChatRoomListWnd.GetTitle() != NULL)
    {
        if (m_iTabIndex == 2 || m_iTabMouseOverIndex == 2)
        {
            g_RenderText.SetTextColor(255, 255, 255, 255);
        }
        else
        {
            g_RenderText.SetTextColor(230, 220, 200, 255);
        }
        TextLen = lstrlen(m_ChatRoomListWnd.GetTitle());

        g_RenderText.MeasureText(m_ChatRoomListWnd.GetTitle(), TextLen, &TextSize);
        g_RenderText.RenderText(RPos_x(107) +
                                    (52 - (float)TextSize.cx / g_fScreenRate_x + 0.5f) / 2,
                                RPos_y(0) + (24 - (float)TextSize.cy / g_fScreenRate_y + 0.5f) / 2,
                                m_ChatRoomListWnd.GetTitle());
    }

    g_RenderText.SetTextColor(230, 220, 200, 255);
    g_RenderText.MeasureText(I18N::Game::RefuseChat, wcslen(I18N::Game::RefuseChat), &TextSize);
    g_RenderText.RenderText(RPos_x(0) + RWidth() - (float)TextSize.cx / g_fScreenRate_x - 2,
                            RPos_y(0) + (24 - (float)TextSize.cy / g_fScreenRate_y + 0.5f) / 2,
                            I18N::Game::RefuseChat);

    float fCheckBoxPos_x = RPos_x(0) + RWidth() - (float)TextSize.cx / g_fScreenRate_x - 2 - 14;
    float fCheckBoxPos_y = RPos_y(0) + (24 - (float)TextSize.cy / g_fScreenRate_y + 0.5f) / 2;

    RenderCheckBox(fCheckBoxPos_x - 1, fCheckBoxPos_y - 1, g_pWindowMgr->GetChatReject());
}

void CUIFriendMenu::RenderFriendButton()
{
    BOOL bIsAlertTime = (m_iBlinkTemp % 24 < 12);
    if (g_pWindowMgr->GetFriendMainWindow() != NULL)
    {
        RenderBitmap(BITMAP_INTERFACE_EX + 18, m_iPos_x, m_iFriendMenuPos_y, m_iWidth,
                     m_iFriendMenuHeight, 0.f, 0.f, m_iWidth / 64.f, m_iFriendMenuHeight / 32.f);

        if (SessionOrigin().FriendMenuObject().IsNewChatAlert() && bIsAlertTime)
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 17, m_iPos_x + 7, m_iFriendMenuPos_y + 3, 25.f, 14.f,
                         0.f, 14.f / 32.f, 25.f / 32.f, 14.f / 32.f);
        }
        if (SessionOrigin().FriendMenuObject().IsNewMailAlert())
        {
            if (g_pLetterList->CheckNoReadLetter() == FALSE)
                SessionOrigin().FriendMenuObject().SetNewMailAlert(FALSE);

            if (bIsAlertTime)
            {
                RenderBitmap(BITMAP_INTERFACE_EX + 16, m_iPos_x + 36, m_iFriendMenuPos_y + 7, 15.f,
                             9.f, 0.f, 0.f, 15.f / 16.f, 9.f / 16.f);
                if (m_iBlinkTemp % 24 == 11)
                    ++m_iLetterBlink;
                if (m_iLetterBlink > 5)
                {
                    m_iLetterBlink = 0;
                    SessionOrigin().FriendMenuObject().SetNewMailAlert(FALSE);
                }
            }
        }
        else if (g_pLetterList->CheckNoReadLetter())
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 16, m_iPos_x + 36, m_iFriendMenuPos_y + 7, 15.f, 9.f,
                         0.f, 0.f, 15.f / 16.f, 9.f / 16.f);
        }
    }
    else
    {
        if (SessionOrigin().FriendMenuObject().IsNewChatAlert() && bIsAlertTime)
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 17, m_iPos_x + 4, m_iFriendMenuPos_y + 2, 25.f, 14.f,
                         0.f, 0.f / 32.f, 25.f / 32.f, 14.f / 32.f);
        }
        if (SessionOrigin().FriendMenuObject().IsNewMailAlert())
        {
            if (bIsAlertTime)
            {
                RenderBitmap(BITMAP_INTERFACE_EX + 16, m_iPos_x + 33, m_iFriendMenuPos_y + 5, 15.f,
                             9.f, 0.f, 0.f, 15.f / 16.f, 9.f / 16.f);
                if (m_iBlinkTemp % 24 == 11)
                    ++m_iLetterBlink;
                if (m_iLetterBlink > 5)
                {
                    m_iLetterBlink = 0;
                    SessionOrigin().FriendMenuObject().SetNewMailAlert(FALSE);
                }
            }
        }
        else if (g_pLetterList->CheckNoReadLetter())
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 16, m_iPos_x + 33, m_iFriendMenuPos_y + 5, 15.f, 9.f,
                         0.f, 0.f, 15.f / 16.f, 9.f / 16.f);
        }
    }
    ++m_iBlinkTemp;
}

void CUIFriendMenu::RenderSub()
{
    if (m_fLineHeight == 0)
    {
        SIZE TextSize;
        g_RenderText.MeasureText(L"0", 1, &TextSize);

        m_fLineHeight = TextSize.cy / g_fScreenRate_y;
    }
    m_fMenuAlpha += m_fMenuAlphaAdd;
    if (m_fMenuAlpha < 0.0f)
    {
        m_fMenuAlphaAdd = 0;
        m_fMenuAlpha = 0;
    }
    else if (m_fMenuAlpha > 1.0f)
    {
        m_fMenuAlphaAdd = 0;
        m_fMenuAlpha = 1.0f;
    }

    EnableAlphaTest();

    if (m_WindowList.empty() == false && GetState() == UISTATE_NORMAL)
    {
        int iSize = 0;
        for (m_WindowListIter = m_WindowList.begin(); m_WindowListIter != m_WindowList.end();
             ++m_WindowListIter, ++iSize)
        {
            assert(g_pWindowMgr->GetWindow(*m_WindowListIter) != NULL);
            if (g_pWindowMgr->GetWindow(*m_WindowListIter)->GetState() == UISTATE_READY)
            {
                --iSize;
                continue;
            }
        }
        if (iSize != 0)
        {
            int iLineHeight = (m_fLineHeight + 4) * iSize;

            SetLineColor(3, m_fMenuAlpha);
            RenderColor(m_iPos_x, m_iFriendMenuPos_y - iLineHeight, m_iWidth, iLineHeight);
            SetLineColor(15, m_fMenuAlpha);
            RenderColor(m_iPos_x, m_iFriendMenuPos_y - iLineHeight, m_iWidth, 1);
            RenderColor(m_iPos_x, m_iFriendMenuPos_y - iLineHeight, 1, iLineHeight);
            RenderColor(m_iPos_x + m_iWidth - 1, m_iFriendMenuPos_y - iLineHeight, 1, iLineHeight);
            RenderColor(m_iPos_x, m_iFriendMenuPos_y, m_iWidth, 1);
            EndRenderColor();

            RenderWindowList();
        }
    }
    DisableAlphaBlend();
}

void CUIFriendMenu::RenderWindowList()
{
    int i = 1;
    for (m_WindowListIter = m_WindowList.begin(); m_WindowListIter != m_WindowList.end();
         ++m_WindowListIter, ++i)
    {
        BOOL bAlert = FALSE;
        if (g_pWindowMgr->GetWindow(*m_WindowListIter)->GetState() == UISTATE_READY)
        {
            --i;
            continue;
        }
        BYTE bAlpha = m_fMenuAlpha * 255;

        for (std::deque<DWORD>::iterator iter = m_NewChatWindowList.begin();
             iter != m_NewChatWindowList.end(); ++iter)
        {
            if (*iter == *m_WindowListIter)
            {
                bAlert = TRUE;
            }
        }

        if (m_WindowListSelectIter == m_WindowListIter || bAlert == TRUE)
        {
            EnableAlphaTest();
            if (m_WindowListSelectIter == m_WindowListIter)
                SetLineColor(16, m_fMenuAlpha);
            else
                SetLineColor(17, m_fMenuAlpha);
            RenderColor(m_iPos_x + 1, m_iFriendMenuPos_y - (m_fLineHeight + 4) * i + 1,
                        m_iWidth - 2, m_fLineHeight + 3);
            EndRenderColor();
            g_RenderText.SetTextColor(0, 0, 0, bAlpha);
        }
        else
        {
            g_RenderText.SetTextColor(230, 220, 200, bAlpha);
        }

        wchar_t szText[64];
        g_RenderText.SetBgColor(0);
        const wchar_t *pszChatTitleOriginal =
            g_pWindowMgr->GetWindow(*m_WindowListIter)->GetTitle();

        wchar_t temp[MAX_TEXT_LENGTH + 1] = {0};
        wcsncpy(temp, pszChatTitleOriginal, MAX_TEXT_LENGTH + 1);
        wchar_t *context = nullptr;
        wchar_t *pszChatTitle = wcstok_s(temp, L",", &context);

        if (wcslen(pszChatTitle) > wcslen(I18N::Game::Talking))
        {
            if (wcsncmp(pszChatTitle, I18N::Game::Offline, wcslen(I18N::Game::Offline)) == 0)
            {
                CutText3(pszChatTitle + wcslen(I18N::Game::Offline) + wcslen(I18N::Game::Talking),
                         szText, m_iWidth - 8, 1, 64);
            }
            else
            {
                CutText3(pszChatTitle + wcslen(I18N::Game::Talking), szText, m_iWidth - 8, 1, 64);
            }
        }
        else
        {
            wcscpy(szText, I18N::Game::Offline);
        }
        g_RenderText.RenderText(m_iPos_x + 2, m_iFriendMenuPos_y - (m_fLineHeight + 4) * i + 3,
                                szText);

        auto *pWindow = (CUIChatWindow *)g_pWindowMgr->GetWindow(*m_WindowListIter);
        if (pWindow != NULL && pWindow->GetUserCount() > 2)
        {
            glColor3f(255, 0, 0);
            RenderBitmap(BITMAP_INTERFACE_EX + 15, (float)m_iPos_x + m_iWidth - 7,
                         (float)m_iFriendMenuPos_y - (m_fLineHeight + 4) * i + 5, (float)4,
                         (float)6, 0.f, 0.f, 4.f / 8.f, 6.f / 8.f);
            glColor3f(255, 255, 255);
        }
    }
}

void SEASON3B::CNewUIChatLogWindow::SetPosition(int x, int y)
{
    m_WndPos.x = x;
    m_WndPos.y = y;
}

void SEASON3B::CNewUIChatLogWindow::SetSizeAuto()
{
    SetNumberOfShowingLines(GetNumberOfShowingLines() +
                            ChatLogDetail::ChatCompatibility().Number<int>(5));
}

bool SEASON3B::CNewUIChatLogWindow::Render()
{
    return m_modernPanel.Record(LegacyRender());
}

bool SEASON3B::CNewUISystemLogWindow::RenderMessages()
{
    if (!m_bShowMessages)
    {
        return true;
    }

    int fRenderPosX = m_WndPos.x + FONT_LEADING;
    int fRenderPosY = m_WndPos.y - m_WndSize.cy;

    if (m_vecAllMsgs.empty())
    {
        return true;
    }

    int iRenderStartLine = 0;
    if (GetCurrentRenderEndLine() >= m_nShowingLines)
    {
        iRenderStartLine = GetCurrentRenderEndLine() - m_nShowingLines + 1;
    }
    else
    {
        fRenderPosY += FONT_LEADING;
    }

    const auto rowHeight = static_cast<int>(FontHeight * 1.2 / g_fScreenRate_y);

    EnableAlphaTest();
    for (int i = iRenderStartLine; i <= GetCurrentRenderEndLine(); i++)
    {
        if (i < 0 && i >= static_cast<int>(m_vecAllMsgs.size()))
            break;

        g_RenderText.SetFont(LegacyFontRole::Normal);

        auto const message = m_vecAllMsgs[i];
        if (message->GetType() == TYPE_SYSTEM_MESSAGE)
        {
            g_RenderText.SetBgColor(0, 0, 0, 100);
            g_RenderText.SetTextColor(100, 150, 255, 255);
        }
        else
        {
            g_RenderText.SetBgColor(0, 0, 0, 100);
            g_RenderText.SetTextColor(255, 30, 0, 255);
        }

        g_RenderText.RenderText(fRenderPosX, fRenderPosY + rowHeight * i,
                                message->GetText().c_str());
    }

    DisableAlphaBlend();

    return true;
}

void SEASON3B::CNewUISystemLogWindow::SetPosition(int x, int y)
{
    m_WndPos.x = x;
    m_WndPos.y = y;
}

bool SEASON3B::CNewUISystemLogWindow::Render()
{
    return SceneFlag == MAIN_SCENE || RenderMessages();
}

namespace PartyListDetail
{
using UI::Modern::PartyFrameDesign;
using UI::Modern::PartyFrameMetric;

} // namespace PartyListDetail

void CNewUIPartyListWindow::SetPos(int x, int y)
{
    m_partyFrameX = static_cast<float>(x);
    m_partyFrameY = static_cast<float>(y);
    SyncLeaveButtons();
}

void CNewUIPartyListWindow::SetPos(int x)
{
    const int frameWidth = PartyListDetail::PartyFrameLogicalSize(
        PartyListDetail::PartyFrameDesign().Number(PartyListDetail::PartyFrameMetric::MemberWidth),
        ModernUiScreenRateX(), ModernUiScale());
    m_partyFrameX = static_cast<float>(std::max(0, x - frameWidth));
    SyncLeaveButtons();
}

bool CNewUIPartyListWindow::Render()
{
    return !m_bActive || renderer_.RecordPartyFrame();
}

void CNewUIPartyListWindow::StagePartyFrame()
{
    UI::Modern::RmlPartyFrameRequest request;
    request.x = m_partyFrameX;
    request.y = m_partyFrameY;
    request.rowCount = m_partyFrameRowCount;
    request.visible = m_bActive;
    request.minimized = m_partyFrameMinimized;
    request.minimizeButton = m_partyFrameMinimizeState;

    for (int i = 0; i < request.rowCount; ++i)
    {
        UI::Modern::RmlPartyFrameRow &row = request.rows[i];
        std::wcsncpy(row.name, Party[i].Name, UI::Modern::RmlPartyFrameRow::NameCapacity - 1);
        row.name[UI::Modern::RmlPartyFrameRow::NameCapacity - 1] = L'\0';
        row.currentHp = Party[i].currHP;
        row.maximumHp = Party[i].maxHP;
        row.outOfViewport = Party[i].index == -1;
        row.leader = i == 0;
        row.canLeave = Hero != nullptr && (std::wcscmp(Party[0].Name, Hero->ID) == 0 ||
                                           std::wcscmp(Party[i].Name, Hero->ID) == 0);
        row.leaveButton = PartyListDetail::ToRmlButtonState(m_BtnPartyExit[i].GetBTState());
        if (m_iSelectedCharacter == i)
            row.memberButton = MouseLButtonPush || IsRepeat(VK_LBUTTON) ? ButtonVisualState::Down
                                                                        : ButtonVisualState::Over;
    }
    renderer_.StagePartyFrame(request);
}

void CNewUIPartyListWindow::DrawWorldPartyHpBar(int x, int y, int stepHp)
{
    const auto draw = [&](PartyListDetail::PartyFrameMetric metric) {
        const auto rect = PartyListDetail::PartyFrameDesign().Values(metric);
        RenderColor(x + rect[0], y + rect[1], rect[2], rect[3]);
    };
    EnableAlphaTest();
    glColor4fv(PartyListDetail::PartyFrameDesign()
                   .Values(PartyListDetail::PartyFrameMetric::WorldHpShadowColor)
                   .data());
    draw(PartyListDetail::PartyFrameMetric::WorldHpShadowRect);
    EnableAlphaBlend();
    glColor3fv(PartyListDetail::PartyFrameDesign()
                   .Values(PartyListDetail::PartyFrameMetric::WorldHpFrameColor)
                   .data());
    draw(PartyListDetail::PartyFrameMetric::WorldHpFrameRect);
    glColor3fv(PartyListDetail::PartyFrameDesign()
                   .Values(PartyListDetail::PartyFrameMetric::WorldHpTrackColor)
                   .data());
    draw(PartyListDetail::PartyFrameMetric::WorldHpTrackRect);
    glColor3fv(PartyListDetail::PartyFrameDesign()
                   .Values(PartyListDetail::PartyFrameMetric::WorldHpStepColor)
                   .data());
    const auto rect = PartyListDetail::PartyFrameDesign().Values(
        PartyListDetail::PartyFrameMetric::WorldHpStepRect);
    const float stride = PartyListDetail::PartyFrameDesign().Number(
        PartyListDetail::PartyFrameMetric::WorldHpStepStride);
    // The network HP step is a decile, independent of authored bar geometry.
    const int steps = std::clamp(stepHp, 0, 10);
    for (int step = 0; step < steps; ++step)
        RenderColor(x + rect[0] + step * stride, y + rect[1], rect[2], rect[3]);
    DisableAlphaBlend();
}

void CNewUIPartyListWindow::RenderPartyHPOnHead()
{
    if (PartyNumber <= 0)
    {
        return;
    }

    wchar_t text[100];
    const auto hoverY = PartyListDetail::PartyFrameDesign().Values(
        PartyListDetail::PartyFrameMetric::WorldHpHoverY);
    const auto textColor = PartyListDetail::PartyFrameDesign().Values(
        PartyListDetail::PartyFrameMetric::WorldHpTextColor);
    for (int j = 0; j < PartyNumber; ++j)
    {
        PARTY_t *const member = &Party[j];
        if (member->index <= -1)
        {
            continue;
        }

        CHARACTER *const character = &CharactersClient[member->index];
        OBJECT *const object = &character->Object;
        vec3_t position;
        int screenX = 0;
        int screenY = 0;
        Vector(object->Position[0], object->Position[1],
               object->Position[2] + object->BoundingBoxMax[2] +
                   PartyListDetail::PartyFrameDesign().Number(
                       PartyListDetail::PartyFrameMetric::WorldHpRaise),
               position);

        BeginOpengl();
        cameraProjection_.WorldToScreen(g_Camera, position, &screenX, &screenY);
        EndOpengl();
        screenX -= static_cast<int>(PartyListDetail::PartyFrameDesign().Number(
                                        PartyListDetail::PartyFrameMetric::WorldHpBarWidth) /
                                    2.0F);

        if (MouseX >= screenX &&
            MouseX < screenX + PartyListDetail::PartyFrameDesign().Number(
                                   PartyListDetail::PartyFrameMetric::WorldHpBarWidth) &&
            MouseY >= screenY + hoverY[0] && MouseY < screenY + hoverY[1])
        {
            mu_swprintf(text, L"HP : %d0%%", member->stepHP);
            g_RenderText.SetTextColor(
                static_cast<BYTE>(textColor[0]), static_cast<BYTE>(textColor[1]),
                static_cast<BYTE>(textColor[2]), static_cast<BYTE>(textColor[3]));
            g_RenderText.RenderText(screenX,
                                    screenY + PartyListDetail::PartyFrameDesign().Number<int>(
                                                  PartyListDetail::PartyFrameMetric::WorldHpTextY),
                                    text);
        }

        DrawWorldPartyHpBar(screenX, screenY, member->stepHP);
    }
    DisableAlphaBlend();
    glColor3f(1.0F, 1.0F, 1.0F);
}

// Includes mirror ZzzInterface.cpp, the unit these were extracted from.

void SessionRenderUnit::SetBooleanPosition(ChatTextDetail::CHAT *c)
{
    BOOL bResult[5];
    SIZE Size[5];
    memset(&Size[0], 0, sizeof(SIZE) * 5);

    if (g_isCharacterBuff((&c->Owner->Object), eBuff_GMEffect) ||
        (c->Owner->CtlCode == CTLCODE_20OPERATOR) || (c->Owner->CtlCode == CTLCODE_08OPERATOR))
    {
        g_RenderText.SetFont(LegacyFontRole::Bold);
        bResult[0] = g_RenderText.MeasureText(c->ID, lstrlen(c->ID), &Size[0]);
        g_RenderText.SetFont(LegacyFontRole::Normal);
    }
    else
    {
        bResult[0] = g_RenderText.MeasureText(c->ID, lstrlen(c->ID), &Size[0]);
    }

    bResult[1] = g_RenderText.MeasureText(c->Text[0], lstrlen(c->Text[0]), &Size[1]);
    bResult[2] = g_RenderText.MeasureText(c->Text[1], lstrlen(c->Text[1]), &Size[2]);
    bResult[3] = g_RenderText.MeasureText(c->Union, lstrlen(c->Union), &Size[3]);
    bResult[4] = g_RenderText.MeasureText(c->Guild, lstrlen(c->Guild), &Size[4]);

    Size[0].cx += 3;

    if (c->LifeTime[1] > 0)
        c->Width = std::max<int>(std::max<int>(std::max<int>(Size[0].cx, Size[1].cx),
                                               std::max<int>(Size[2].cx, Size[3].cx)),
                                 Size[4].cx);

    else if (c->LifeTime[0] > 0)
        c->Width = std::max<int>(std::max<int>(Size[0].cx, Size[1].cx),
                                 std::max<int>(Size[3].cx, Size[4].cx));
    else
        c->Width = std::max<int>(std::max<int>(Size[0].cx, Size[3].cx), Size[4].cx);
    c->Height = FontHeight * (bResult[0] + bResult[1] + bResult[2] + bResult[3] + bResult[4]);

    if (lstrlen(c->szShopTitle) > 0)
    {
        SIZE sizeT[2];
        g_RenderText.SetFont(LegacyFontRole::Bold);

        if (g_RenderText.MeasureText(c->szShopTitle, lstrlen(c->szShopTitle), &sizeT[0]) &&
            g_RenderText.MeasureText(I18N::Game::Store, wcslen(I18N::Game::Store), &sizeT[1]))
        {
            if (c->Width < sizeT[0].cx + sizeT[1].cx)
                c->Width = sizeT[0].cx + sizeT[1].cx;
            c->Height += std::max<int>(sizeT[0].cy, sizeT[1].cy);
        }
        g_RenderText.SetFont(LegacyFontRole::Normal);
    }
    c->Width /= g_fScreenRate_x;
    c->Height /= g_fScreenRate_y;
}

void SessionRenderUnit::SetPlayerColor(BYTE PK)
{
    switch (PK)
    {
    case 0:
        g_RenderText.SetTextColor(150, 255, 240, 255);
        break; //npc
    case 1:
        g_RenderText.SetTextColor(100, 120, 255, 255);
        break;
    case 2:
        g_RenderText.SetTextColor(140, 180, 255, 255);
        break;
    case 3:
        g_RenderText.SetTextColor(200, 220, 255, 255);
        break; //normal
    case 4:
        g_RenderText.SetTextColor(255, 150, 60, 255);
        break; //pk1
    case 5:
        g_RenderText.SetTextColor(255, 80, 30, 255);
        break; //pk2
    default:
        g_RenderText.SetTextColor(255, 0, 0, 255);
        break; //pk3
    }
}

// ※

void SessionRenderUnit::RenderBoolean(int x, int y, ChatTextDetail::CHAT *c)
{
    if (g_isCharacterBuff((&c->Owner->Object), eBuff_CrywolfNPCHide))
    {
        return;
    }

    if (c->Owner != Hero && IsBattleCastleStart() == true &&
        g_isCharacterBuff((&c->Owner->Object), eBuff_Cloaking))
    {
        if ((Hero->EtcPart == PARTS_ATTACK_KING_TEAM_MARK ||
             Hero->EtcPart == PARTS_ATTACK_TEAM_MARK))
        {
            if (!(c->Owner->EtcPart == PARTS_ATTACK_KING_TEAM_MARK ||
                  c->Owner->EtcPart == PARTS_ATTACK_TEAM_MARK))
            {
                return;
            }
        }
        else if ((Hero->EtcPart == PARTS_ATTACK_KING_TEAM_MARK2 ||
                  Hero->EtcPart == PARTS_ATTACK_TEAM_MARK2))
        {
            if (!(c->Owner->EtcPart == PARTS_ATTACK_KING_TEAM_MARK2 ||
                  c->Owner->EtcPart == PARTS_ATTACK_TEAM_MARK2))
            {
                return;
            }
        }
        else if ((Hero->EtcPart == PARTS_ATTACK_KING_TEAM_MARK3 ||
                  Hero->EtcPart == PARTS_ATTACK_TEAM_MARK3))
        {
            if (!(c->Owner->EtcPart == PARTS_ATTACK_KING_TEAM_MARK3 ||
                  c->Owner->EtcPart == PARTS_ATTACK_TEAM_MARK3))
            {
                return;
            }
        }
        else if ((Hero->EtcPart == PARTS_DEFENSE_KING_TEAM_MARK ||
                  Hero->EtcPart == PARTS_DEFENSE_TEAM_MARK))
        {
            if (!(c->Owner->EtcPart == PARTS_DEFENSE_KING_TEAM_MARK ||
                  c->Owner->EtcPart == PARTS_DEFENSE_TEAM_MARK))
            {
                return;
            }
        }
    }

    EnableAlphaTest();
    glColor3f(1.f, 1.f, 1.f);

    if (FontHeight > 32)
        FontHeight = 32;

    POINT RenderPos = {x, y};
    SIZE RenderBoxSize = {c->Width, c->Height};
    int iLineHeight = FontHeight / g_fScreenRate_y;

    if (IsStrifeMap(gMapManager.ContextMap()) &&
        Hero->m_byGensInfluence != c->Owner->m_byGensInfluence && !IsGMCharacter())
    {
        if (!c->Owner->GensContributionPoints)
            return;

        if (KIND_PLAYER == c->Owner->Object.Kind && MODEL_PLAYER == c->Owner->Object.Type)
        {
            int tempX = (int)(c->x + c->Width * 0.5f + 20.0f);
            switch (c->Owner->m_byGensInfluence)
            {
            case 1:
                g_pNewUIGensRanking->RanderMark(
                    tempX, y, (SEASON3B::CNewUIGensRanking::GENS_TYPE)c->Owner->m_byGensInfluence,
                    c->Owner->GensRanking, SEASON3B::CNewUIGensRanking::MARK_BOOLEAN,
                    (float)RenderPos.y);
                return;
            case 2:
                g_pNewUIGensRanking->RanderMark(
                    tempX, y, (SEASON3B::CNewUIGensRanking::GENS_TYPE)c->Owner->m_byGensInfluence,
                    c->Owner->GensRanking, SEASON3B::CNewUIGensRanking::MARK_BOOLEAN,
                    (float)RenderPos.y);
                return;
            default:
                break;
            }
        }
    }

    bool bGmMode = false;

    if (g_isCharacterBuff((&c->Owner->Object), eBuff_GMEffect) ||
        (c->Owner->CtlCode == CTLCODE_20OPERATOR) || (c->Owner->CtlCode == CTLCODE_08OPERATOR))
    {
        bGmMode = true;
        g_RenderText.SetBgColor(30, 30, 30, 200);
        g_RenderText.SetTextColor(200, 255, 255, 255);
    }

    if (c->Owner == Hero)
    {
        g_RenderText.SetBgColor(60, 100, 0, 150);
        g_RenderText.SetTextColor(200, 255, 0, 255);
    }
    else if (c->Owner->GuildMarkIndex == Hero->GuildMarkIndex)
    {
        g_RenderText.SetBgColor(GetGuildRelationShipBGColor(GR_UNION));
        g_RenderText.SetTextColor(GetGuildRelationShipTextColor(GR_UNION));
    }
    else
    {
        g_RenderText.SetBgColor(GetGuildRelationShipBGColor(c->Owner->GuildRelationShip));
        g_RenderText.SetTextColor(GetGuildRelationShipTextColor(c->Owner->GuildRelationShip));
    }

    if (c->Union && c->Union[0])
    {
        g_RenderText.RenderText(RenderPos.x, RenderPos.y, c->Union, RenderBoxSize.cx, iLineHeight,
                                RT3_SORT_LEFT);
        RenderPos.y += iLineHeight;
    }
    if (c->Guild && c->Guild[0])
    {
        g_RenderText.RenderText(RenderPos.x, RenderPos.y, c->Guild, RenderBoxSize.cx, iLineHeight,
                                RT3_SORT_LEFT);
        RenderPos.y += iLineHeight;
    }

    if (bGmMode)
    {
        g_RenderText.SetTextColor(100, 250, 250, 255);
    }
    else
    {
        SetPlayerColor(c->Color);
    }

    if (c->x <= MouseX && MouseX < (int)(c->x + c->Width * REFERENCE_WIDTH / WindowWidth) &&
        c->y <= MouseY && MouseY < (int)(c->y + c->Height * REFERENCE_HEIGHT / WindowHeight) &&
        InputEnable && Hero->SafeZone && wcscmp(c->ID, Hero->ID) != 0 && (DWORD)WorldTime % 24 < 12)
    {
        unsigned int Temp = g_RenderText.GetBgColor();
        g_RenderText.SetBgColor(g_RenderText.GetTextColor());
        g_RenderText.SetTextColor(Temp);
    }

    if (bGmMode)
    {
        g_RenderText.SetFont(LegacyFontRole::Bold);
        g_RenderText.RenderText(RenderPos.x, RenderPos.y, c->ID, RenderBoxSize.cx, iLineHeight,
                                RT3_SORT_LEFT);
        RenderPos.y += iLineHeight;
        g_RenderText.SetFont(LegacyFontRole::Normal);
    }
    else
    {
        g_RenderText.RenderText(RenderPos.x, RenderPos.y, c->ID, RenderBoxSize.cx, iLineHeight,
                                RT3_SORT_LEFT);
        RenderPos.y += iLineHeight;
    }

    if (c->GuildColor == 0)
        g_RenderText.SetBgColor(10, 30, 50, 150);
    else if (c->GuildColor == 1)
        g_RenderText.SetBgColor(30, 50, 0, 150);
    else if (bGmMode)
        g_RenderText.SetBgColor(30, 30, 30, 200);
    else
        g_RenderText.SetBgColor(50, 0, 0, 150);

    DWORD dwTextColor[2];
    BYTE byAlpha[2] = {255, 255};
    if ((c->LifeTime[0] > 0 && c->LifeTime[0] < 10))
        byAlpha[0] = 128;
    if ((c->LifeTime[1] > 0 && c->LifeTime[1] < 10))
        byAlpha[1] = 128;

    if (bGmMode)
    {
        dwTextColor[0] = RGBA(250, 200, 50, byAlpha[0]);
        dwTextColor[1] = RGBA(250, 200, 50, byAlpha[1]);
    }
    else
    {
        dwTextColor[0] = RGBA(230, 220, 200, byAlpha[0]);
        dwTextColor[1] = RGBA(230, 220, 200, byAlpha[1]);
    }

    if (c->LifeTime[1] > 0)
    {
        g_RenderText.SetTextColor(dwTextColor[1]);
        g_RenderText.RenderText(RenderPos.x, RenderPos.y, c->Text[1], RenderBoxSize.cx, iLineHeight,
                                RT3_SORT_LEFT);
        RenderPos.y += iLineHeight;

        g_RenderText.SetTextColor(dwTextColor[0]);
        g_RenderText.RenderText(RenderPos.x, RenderPos.y, c->Text[0], RenderBoxSize.cx,
                                iLineHeight);
    }
    else if (c->LifeTime[0] > 0)
    {
        g_RenderText.SetTextColor(dwTextColor[0]);
        g_RenderText.RenderText(RenderPos.x, RenderPos.y, c->Text[0], RenderBoxSize.cx,
                                iLineHeight);
    }

    if (KIND_PLAYER == c->Owner->Object.Kind && MODEL_PLAYER == c->Owner->Object.Type)
    {
        const int nGensMarkHeight = 18;
        int nGensMarkPosY = (RenderPos.y - y - nGensMarkHeight) / 2 + y;

        if (c->LifeTime[1] > 0)
            RenderPos.y -= iLineHeight;

        if (1 == c->Owner->m_byGensInfluence)
            g_pNewUIGensRanking->RanderMark(
                x, y, (SEASON3B::CNewUIGensRanking::GENS_TYPE)c->Owner->m_byGensInfluence,
                c->Owner->GensRanking, SEASON3B::CNewUIGensRanking::MARK_BOOLEAN,
                (float)RenderPos.y);
        else if (2 == c->Owner->m_byGensInfluence)
            g_pNewUIGensRanking->RanderMark(
                x, y, (SEASON3B::CNewUIGensRanking::GENS_TYPE)c->Owner->m_byGensInfluence,
                c->Owner->GensRanking, SEASON3B::CNewUIGensRanking::MARK_BOOLEAN,
                (float)RenderPos.y);
    }
}

void SessionRenderUnit::RenderBooleans()
{
    g_RenderText.SetFont(LegacyFontRole::Normal);

    for (int i = 0; i < ChatTextDetail::MAX_CHAT; i++)
    {
        ChatTextDetail::CHAT *ci = &Chat[i];
        if (ci->IDLifeTime > 0 || ci->LifeTime[0] > 0)
        {
            vec3_t Position;
            int ScreenX, ScreenY;
            if (ci->Owner != NULL)
            {
                OBJECT *o = &ci->Owner->Object;
                Vector(o->Position[0], o->Position[1], o->Position[2] + o->BoundingBoxMax[2] + 60.f,
                       Position);

                if (o->Type >= MODEL_LITTLESANTA && o->Type <= MODEL_LITTLESANTA_END)
                {
                    Vector(o->Position[0], o->Position[1],
                           o->Position[2] + o->BoundingBoxMax[2] + 20.f, Position);
                }
                if (o->Type == MODEL_MERCHANT_MAN)
                {
                    Vector(o->Position[0], o->Position[1],
                           o->Position[2] + o->BoundingBoxMax[2] + 20.f, Position);
                }
                cameraProjection_.WorldToScreen(g_Camera, Position, &ScreenX, &ScreenY);
            }
            else
            {
                cameraProjection_.WorldToScreen(g_Camera, ci->Position, &ScreenX, &ScreenY);
            }
            SetBooleanPosition(ci);
            ci->x = ScreenX - (ci->Width / 2);
            ci->y = ScreenY - ci->Height;
        }
    }

    for (int i = 0; i < ChatTextDetail::MAX_CHAT; i++) //. Bubble sorting
    {
        ChatTextDetail::CHAT *ci = &Chat[i];
        if (ci->IDLifeTime > 0 || ci->LifeTime[0] > 0)
        {
            for (int j = 0; j < ChatTextDetail::MAX_CHAT; j++)
            {
                ChatTextDetail::CHAT *cj = &Chat[j];
                if (i != j && (cj->IDLifeTime > 0 || cj->LifeTime[0] > 0))
                {
                    if (ci->x + ci->Width > cj->x && ci->x < cj->x + cj->Width &&
                        ci->y + ci->Height > cj->y && ci->y < cj->y + cj->Height)
                    {
                        if (ci->y < cj->y + cj->Height / 2)
                            ci->y = cj->y - ci->Height;
                        else
                            ci->y = cj->y + cj->Height;
                    }
                }
            }
        }
    }

    for (int i = 0; i < ChatTextDetail::MAX_CHAT; i++)
    {
        ChatTextDetail::CHAT *ci = &Chat[i];
        if (ci->IDLifeTime > 0 || ci->LifeTime[0] > 0)
        {
            //. Fit to screen
            if (ci->x < 0)
                ci->x = 0;
            if (ci->x >= (int)WindowWidth - ci->Width)
                ci->x = WindowWidth - ci->Width;
            if (ci->y < 0)
                ci->y = 0;
            if (ci->y >= (int)WindowHeight - ci->Height)
                ci->y = WindowHeight - ci->Height;
            if (ci->Owner == nullptr || ci->Owner->Object.Kind != KIND_PLAYER)
            {
                RenderBoolean(ci->x, ci->y, ci);
            }
        }
    }
}

void SessionLegacyCalls::SetBooleanPosition(UI::Chat::CHAT *chat)
{
    sessionKeeper_.Renderer()->SetBooleanPosition(chat);
}

void SessionLegacyCalls::SetPlayerColor(BYTE playerKillLevel)
{
    sessionKeeper_.Renderer()->SetPlayerColor(playerKillLevel);
}

void SessionLegacyCalls::RenderBoolean(int x, int y, UI::Chat::CHAT *chat)
{
    sessionKeeper_.Renderer()->RenderBoolean(x, y, chat);
}

void SessionLegacyCalls::RenderBooleans()
{
    sessionKeeper_.Renderer()->RenderBooleans();
}

// Feature controller methods consolidated from shared UI buckets.
#pragma pack(push)
#pragma pack()
bool SEASON3B::CNewUIChatInputBox::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight)
{
    if (m_pNewUIChatLogWnd == nullptr)
        return true;
    m_modernState.visible = true;
    m_modernState.editing = IsVisible();
    const UI::Modern::PC::Chat::RmlChatLabels labels{
        I18N::Game::General, I18N::Game::Party,  I18N::Game::Guild, I18N::Game::Gens,
        I18N::Game::Whisper, I18N::Game::System, L"Block"};
    auto &panel = m_pNewUIChatLogWnd->ModernPanel();
    panel.Stage(m_modernState, labels);
    return panel.PrepareOnWorker(viewportWidth, viewportHeight);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool SEASON3B::CNewUIChatInputBox::Render()
{
    return true;
}
#pragma pack(pop)

// Native feature window methods.
#pragma pack(push)
#pragma pack()
bool CUILetterWriteWindow::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight,
                                                   bool visible)
{
    using namespace UI::Modern::PC::Friend;
    RmlFriendPanelContent content;
    content.x = GetPosition_x();
    content.y = GetPosition_y();
    content.title = GetTitle();
    content.senderLabel = I18N::Game::Receiver;
    content.headers[0] = I18N::Game::Title1030;
    content.labels[0] = I18N::Game::Send;
    content.labels[1] = I18N::Game::Close388;
    content.receiver = m_MailtoInputBox.DisplayTextForRetainedUi();
    content.subject = m_TitleInputBox.DisplayTextForRetainedUi();
    content.body = m_TextInputBox.DisplayTextForRetainedUi();
    return m_ModernPanel.PrepareOnWorker(viewportWidth, viewportHeight, visible, content);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CUILetterWriteWindow::RecordModernUi(LegacyRenderFacade &facade) const
{
    return m_ModernPanel.Record(facade);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterWriteWindow::RenderSub()
{
    EnableAlphaTest();
    if (GetState() == UISTATE_MOVE || GetState() == UISTATE_RESIZE)
    {
        m_SendButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_CloseButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_MailtoInputBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_TitleInputBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_TextInputBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        //m_PhotoShowButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_PrevPoseButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_NextPoseButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_Photo.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);

        //		if (GetState() == UISTATE_RESIZE)
        //		{
        //			m_TextInputBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
        //		}
    }

    SetLineColor(7);
    RenderColor((float)RPos_x(0), (float)RPos_y(0), (float)RWidth(), 29.0f);
    SetLineColor(2);
    RenderColor((float)RPos_x(0), (float)RPos_y(14), (float)RWidth(), 1.0f);
    RenderColor((float)RPos_x(0), (float)RPos_y(29), (float)RWidth(), 1.0f);

    SetLineColor(7);
    RenderColor((float)RPos_x(0), (float)RPos_y(0) + RHeight() - 19, (float)RWidth(), 19.0f);
    SetLineColor(2);
    RenderColor((float)RPos_x(0), (float)RPos_y(0) + RHeight() - 19, (float)RWidth(), 1.0f);
    EndRenderColor();

    SIZE size;

    g_RenderText.SetTextColor(230, 220, 200, 255);
    g_RenderText.MeasureText(I18N::Game::Receiver, wcslen(I18N::Game::Receiver), &size);
    g_RenderText.RenderText(RPos_x(3), RPos_y(3), I18N::Game::Receiver, size.cx / g_fScreenRate_x,
                            0, RT3_SORT_RIGHT);
    g_RenderText.RenderText(RPos_x(3), RPos_y(18), I18N::Game::Title, size.cx / g_fScreenRate_x, 0,
                            RT3_SORT_RIGHT);

    m_MailtoInputBox.Render();
    m_TitleInputBox.Render();
    m_TextInputBox.Render();
    DisableAlphaBlend();

    m_SendButton.Render();
    m_CloseButton.Render();
    //m_PhotoShowButton.Render();
    if (m_iShowType == 1)
    {
        m_PrevPoseButton.Render();
        m_NextPoseButton.Render();
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterWriteWindow::RenderOver()
{
    if (m_iShowType == 1)
    {
        m_Photo.Render();
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CUILetterReadWindow::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight,
                                                  bool visible)
{
    using namespace UI::Modern::PC::Friend;
    RmlFriendPanelContent content;
    content.x = GetPosition_x();
    content.y = GetPosition_y();
    content.title = GetTitle();
    content.senderLabel = I18N::Game::Sender;
    wchar_t sender[MAX_TEXT_LENGTH + 1]{};
    mu_swprintf(sender, I18N::Game::SenderSSS, m_LetterHead.m_szID, m_LetterHead.m_szDate,
                m_LetterHead.m_szTime);
    content.sender = sender;
    for (auto line = m_LetterTextBox.Items().rbegin(); line != m_LetterTextBox.Items().rend();
         ++line)
    {
        if (!content.body.empty())
            content.body += L'\n';
        content.body += line->m_szText;
    }
    content.labels = {I18N::Game::Reply, I18N::Game::Delete, I18N::Game::Previous, I18N::Game::Next,
                      I18N::Game::Close388};
    return m_ModernPanel.PrepareOnWorker(viewportWidth, viewportHeight, visible, content);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CUILetterReadWindow::RecordModernUi(LegacyRenderFacade &facade) const
{
    return m_ModernPanel.Record(facade);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterReadWindow::RenderSub()
{
    EnableAlphaTest();
    if (GetState() == UISTATE_MOVE || GetState() == UISTATE_RESIZE)
    {
        m_LetterTextBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_ReplyButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_DeleteButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_CloseButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_PrevButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_NextButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_Photo.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);

        if (GetState() == UISTATE_RESIZE)
        {
            m_LetterTextBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
            m_Photo.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
        }
    }

    m_LetterTextBox.Render();

    if (m_iShowType >= 2)
    {
        SetLineColor(2);
        RenderColor((float)(RPos_x(0) + RWidth() - 120), (float)RPos_y(0) + RHeight() - 19, 1, 19);
        RenderColor((float)RPos_x(0), (float)RPos_y(0) + RHeight() - 20, (float)RWidth() - 120,
                    1.0f);
        SetLineColor(7);
        RenderColor((float)RPos_x(0), (float)RPos_y(0) + RHeight() - 19, (float)RWidth() - 120,
                    18.0f);
    }
    else
    {
        SetLineColor(2);
        RenderColor((float)RPos_x(0), (float)RPos_y(0) + RHeight() - 20, (float)RWidth(), 1.0f);
        SetLineColor(7);
        RenderColor((float)RPos_x(0), (float)RPos_y(0) + RHeight() - 19, (float)RWidth(), 18.0f);
    }

    SetLineColor(2);
    RenderColor((float)RPos_x(0), (float)RPos_y(14), (float)RWidth(), 1.0f);
    SetLineColor(7);
    RenderColor((float)RPos_x(0), (float)RPos_y(0), (float)RWidth(), 14.0f);
    EndRenderColor();

    wchar_t szMailFrom[256] = {0};
    mu_swprintf(szMailFrom, I18N::Game::SenderSSS, m_LetterHead.m_szID, m_LetterHead.m_szDate,
                m_LetterHead.m_szTime);
    g_RenderText.RenderText(RPos_x(3), RPos_y(3), szMailFrom);

    m_ReplyButton.Render();
    m_DeleteButton.Render();
    m_CloseButton.Render();
    m_PrevButton.Render();
    m_NextButton.Render();

    DisableAlphaBlend();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterReadWindow::RenderOver()
{
    if (m_iShowType >= 2)
    {
        m_Photo.Render();
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterBoxTabWindow::RenderSub()
{
    if (GetState() == UISTATE_MOVE || GetState() == UISTATE_RESIZE)
    {
        m_LetterListBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);

        m_WriteButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_ReadButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_ReplyButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_DeleteButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        //		m_DeliveryButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);

        if (GetState() == UISTATE_RESIZE)
        {
            m_LetterListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
        }
    }

    EnableAlphaTest();
    SetLineColor(7);
    RenderColor((float)RPos_x(0), (float)RPos_y(18 + m_LetterListBox.GetHeight()), (float)RWidth(),
                (float)RHeight() - m_LetterListBox.GetHeight() - 18);
    EndRenderColor();
    DisableAlphaBlend();

    m_LetterListBox.Render();

    EnableAlphaTest();
    SetLineColor(2);
    RenderColor((float)RPos_x(0), (float)RPos_y(16), (float)RWidth(), 1.0f);
    SetLineColor(5);
    RenderColor((float)RPos_x(0), (float)RPos_y(0), (float)RWidth(), 16);
    SetLineColor(2);
    RenderColor((float)RPos_x(0), (float)RPos_y(17 + m_LetterListBox.GetHeight()), (float)RWidth(),
                1.0f);

    RenderColor((float)RPos_x(0) + m_LetterListBox.GetColumnPos_x(1), (float)RPos_y(17), 1.0f,
                (float)RHeight() - 22 - 17);
    RenderColor((float)RPos_x(0) + m_LetterListBox.GetColumnPos_x(2), (float)RPos_y(17), 1.0f,
                (float)RHeight() - 22 - 17);
    RenderColor((float)RPos_x(0) + m_LetterListBox.GetColumnPos_x(3), (float)RPos_y(17), 1.0f,
                (float)RHeight() - 22 - 17);
    SetLineColor(14);
    RenderColor((float)RPos_x(0) + m_LetterListBox.GetColumnPos_x(1), (float)RPos_y(3), 1.0f, 10);
    RenderColor((float)RPos_x(0) + m_LetterListBox.GetColumnPos_x(2), (float)RPos_y(3), 1.0f, 10);
    RenderColor((float)RPos_x(0) + m_LetterListBox.GetColumnPos_x(3), (float)RPos_y(3), 1.0f, 10);
    EndRenderColor();

    g_RenderText.SetTextColor(230, 220, 200, 255);
    g_RenderText.SetBgColor(0, 0, 0, 0);

    RenderCheckBox(RPos_x(1), RPos_y(3), m_bCheckAllState);
    RenderBitmap(BITMAP_INTERFACE_EX + 14, RPos_x(1 + 10), RPos_y(3), 13.0f, 9.0f, 0.f, 0.f,
                 13.f / 16.f, 9.f / 32.f);

    if (CheckMouseIn(RPos_x(0) + m_LetterListBox.GetColumnPos_x(1), RPos_y(0),
                     m_LetterListBox.GetColumnWidth(1), 19) == TRUE ||
        g_pLetterList->GetCurrentSortType() == 1)
    {
        g_RenderText.SetTextColor(255, 255, 255, 255);
        g_RenderText.RenderText(RPos_x(4) + m_LetterListBox.GetColumnPos_x(1), RPos_y(3),
                                I18N::Game::Sender);
        g_RenderText.SetTextColor(230, 220, 200, 255);
    }
    else
    {
        g_RenderText.RenderText(RPos_x(4) + m_LetterListBox.GetColumnPos_x(1), RPos_y(3),
                                I18N::Game::Sender);
    }

    if (CheckMouseIn(RPos_x(0) + m_LetterListBox.GetColumnPos_x(2), RPos_y(0),
                     m_LetterListBox.GetColumnWidth(2), 19) == TRUE ||
        g_pLetterList->GetCurrentSortType() == 2)
    {
        g_RenderText.SetTextColor(255, 255, 255, 255);
        g_RenderText.RenderText(RPos_x(4) + m_LetterListBox.GetColumnPos_x(2), RPos_y(3),
                                I18N::Game::DateRcvd);
        g_RenderText.SetTextColor(230, 220, 200, 255);
    }
    else
    {
        g_RenderText.RenderText(RPos_x(4) + m_LetterListBox.GetColumnPos_x(2), RPos_y(3),
                                I18N::Game::DateRcvd);
    }

    if (CheckMouseIn(RPos_x(0) + m_LetterListBox.GetColumnPos_x(3), RPos_y(0),
                     m_LetterListBox.GetColumnWidth(3), 19) == TRUE ||
        g_pLetterList->GetCurrentSortType() == 3)
    {
        g_RenderText.SetTextColor(255, 255, 255, 255);
        g_RenderText.RenderText(RPos_x(4) + m_LetterListBox.GetColumnPos_x(3), RPos_y(3),
                                I18N::Game::Title1030);
        g_RenderText.SetTextColor(230, 220, 200, 255);
    }
    else
    {
        g_RenderText.RenderText(RPos_x(4) + m_LetterListBox.GetColumnPos_x(3), RPos_y(3),
                                I18N::Game::Title1030);
    }

    DisableAlphaBlend();

    m_WriteButton.Render();
    m_ReadButton.Render();
    m_ReplyButton.Render();
    m_DeleteButton.Render();
    //	m_DeliveryButton.Render();
}
#pragma pack(pop)
