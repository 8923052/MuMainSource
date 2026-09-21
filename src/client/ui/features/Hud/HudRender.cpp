#include "ui/features/Hud/HudRender.h"
#include "I18N/All.h"
#include "app/AppWindow.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "data/WorldData.h"
#include "domain/Automation.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/ChatSocial.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Shop.h"
#include "domain/WorldSimulation.h"
#include "render/FrameTape.h"
#include "render/ModelResources.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "render/UiAdapter.h"
#include "render/World.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Hud/HudLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/features/World/WorldLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"
#include "ui/session/UiSessionRender.h"

namespace UI::Modern::PC::Character
{
namespace
{
// Authored design inputs.
enum class CharacterFramePanelDesignKey
{
    PanelWidth,
    PanelHeight,
    ReferenceWidth,
    ReferenceHeight,
    GfxStageWidth,
    InitialX,
    InitialY
};

const RmlUiDesign &CharacterFramePanelDesign()
{
    static const RmlUiDesign design(
        "Data/UI/PC/Character/character_frame.rml",
        {"RmlCharacterFramePanel-PanelWidth", "RmlCharacterFramePanel-PanelHeight",
         "RmlCharacterFramePanel-ReferenceWidth", "RmlCharacterFramePanel-ReferenceHeight",
         "RmlCharacterFramePanel-GfxStageWidth", "RmlCharacterFramePanel-InitialX",
         "RmlCharacterFramePanel-InitialY"});
    return design;
}
// End authored design inputs.

bool SetText(Rml::Element &element, std::wstring &current, const std::wstring &next)
{
    if (current == next)
        return false;
    current = next;
    element.SetInnerRML(Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(next.c_str())));
    return true;
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

} // namespace

float RmlCharacterFramePanel::Width() noexcept
{
    return CharacterFramePanelDesign().Number(CharacterFramePanelDesignKey::PanelWidth);
}

float RmlCharacterFramePanel::Height() noexcept
{
    return CharacterFramePanelDesign().Number(CharacterFramePanelDesignKey::PanelHeight);
}

class RmlCharacterFramePanel::Impl final
{
  public:
    Impl(SessionKeeper &keeper, std::array<RmlMuButton, RmlCharacterFrameStatCount + 3> &buttons)
        : buttons_(buttons),
          host_(keeper, "character-frame-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "Character", "character_frame.rml"))
    {
    }

    ~Impl()
    {
        Release();
    }

    void Release()
    {
        movable_.Unbind();
        for (RmlMuButton &button : buttons_)
            button.Unbind();
        panel_ = nullptr;
        drag_ = nullptr;
        close_ = nullptr;
        title_ = nullptr;
        levelLabel_ = nullptr;
        level_ = nullptr;
        classLabel_ = nullptr;
        characterClass_ = nullptr;
        serverLabel_ = nullptr;
        server_ = nullptr;
        experience_ = nullptr;
        pointLabel_ = nullptr;
        points_ = nullptr;
        charismaSection_ = nullptr;
        charismaField_ = nullptr;
        statLabels_.fill(nullptr);
        statValues_.fill(nullptr);
        details_.fill(nullptr);
        statButtons_.fill(nullptr);
        smallButtons_.fill(nullptr);
        smallButtonLabels_.fill(nullptr);
        currentContent_ = {};
        viewportWidth_ = 0;
        viewportHeight_ = 0;
        scale_ = 1.0F;
        positionSet_ = false;
        inputDirty_ = false;
        visible_ = false;
        host_.Release();
        PublishGeometry();
    }

    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!publishedVisible_.load(std::memory_order_acquire) ||
            event.kind != SessionInputEventKind::Pointer)
        {
            return false;
        }
        const bool wasDragging = movable_.IsDragging();
        (void)host_.ProcessInput(event);
        inputDirty_ = movable_.TakeDirty() || inputDirty_;
        PublishGeometry();
        return wasDragging || movable_.IsDragging() || IsDescendantOf(host_.HoverElement(), panel_);
    }

    bool Prepare(int viewportWidth, int viewportHeight, bool visible,
                 const RmlCharacterFrameContent &content)
    {
        if (panel_ == nullptr && !visible)
        {
            visible_ = false;
            publishedVisible_.store(false, std::memory_order_release);
            return true;
        }
        if (!EnsureDocument(viewportWidth, viewportHeight))
            return false;
        const RmlUiScaledViewport viewport = host_.Viewport();

        const bool viewportChanged =
            viewportWidth_ != viewport.width || viewportHeight_ != viewport.height;
        const bool visibilityChanged = visible_ != visible;
        viewportWidth_ = viewport.width;
        viewportHeight_ = viewport.height;
        scale_ = viewport.scale;
        if (viewportChanged)
        {
            movable_.Configure(viewport.width, viewport.height, Width(), Height());
            if (!positionSet_)
            {
                const float rightMargin =
                    CharacterFramePanelDesign().Number(
                        CharacterFramePanelDesignKey::GfxStageWidth) -
                    CharacterFramePanelDesign().Number(CharacterFramePanelDesignKey::InitialX) -
                    Width();
                movable_.SetPosition(
                    std::max(0.0F, viewport.width - Width() - rightMargin),
                    CharacterFramePanelDesign().Number(CharacterFramePanelDesignKey::InitialY));
                positionSet_ = true;
            }
        }

        bool dirty = viewportChanged || visibilityChanged || inputDirty_ || movable_.TakeDirty();
        dirty = ApplyContent(content) || dirty;
        for (RmlMuButton &button : buttons_)
            dirty = button.SyncVisualState() || dirty;

        if (!host_.SetVisible(visible))
            return false;
        if (!visible && visibilityChanged)
            movable_.CancelDrag();
        visible_ = visible;
        inputDirty_ = false;
        PublishGeometry();
        return host_.CaptureIfDirty(dirty);
    }

    bool Record(LegacyRenderFacade &facade) const
    {
        return panel_ == nullptr || host_.Record(facade);
    }

    RmlCharacterFrameRect ReferenceRect(int viewportWidth, int viewportHeight) const noexcept
    {
        if (viewportWidth <= 0 || viewportHeight <= 0)
            return {};
        const float left = publishedLeft_.load(std::memory_order_acquire);
        const float top = publishedTop_.load(std::memory_order_acquire);
        const float scale = publishedScale_.load(std::memory_order_acquire);
        return {
            left * scale *
                CharacterFramePanelDesign().Number(CharacterFramePanelDesignKey::ReferenceWidth) /
                viewportWidth,
            top * scale *
                CharacterFramePanelDesign().Number(CharacterFramePanelDesignKey::ReferenceHeight) /
                viewportHeight,
            Width() * scale *
                CharacterFramePanelDesign().Number(CharacterFramePanelDesignKey::ReferenceWidth) /
                viewportWidth,
            Height() * scale *
                CharacterFramePanelDesign().Number(CharacterFramePanelDesignKey::ReferenceHeight) /
                viewportHeight};
    }

  private:
    bool EnsureDocument(int viewportWidth, int viewportHeight)
    {
        if (!host_.Ensure(
                viewportWidth, viewportHeight, Width(),
                CharacterFramePanelDesign().Number(CharacterFramePanelDesignKey::InitialY) +
                    Height()))
            return false;
        if (panel_ != nullptr)
            return true;

        Rml::ElementDocument *const document = host_.Document();
        panel_ = document->GetElementById("character-frame");
        drag_ = document->GetElementById("character-drag");
        close_ = document->GetElementById("character-close");
        title_ = document->GetElementById("character-title");
        levelLabel_ = document->GetElementById("character-level-label");
        level_ = document->GetElementById("character-level");
        classLabel_ = document->GetElementById("character-class-label");
        characterClass_ = document->GetElementById("character-class");
        serverLabel_ = document->GetElementById("character-server-label");
        server_ = document->GetElementById("character-server");
        experience_ = document->GetElementById("character-experience");
        pointLabel_ = document->GetElementById("character-point-label");
        points_ = document->GetElementById("character-points");
        charismaSection_ = document->GetElementById("character-section-charisma");
        charismaField_ = document->GetElementById("character-field-stat-4");
        for (std::size_t index = 0; index < statLabels_.size(); ++index)
        {
            const std::string suffix = std::to_string(index);
            statLabels_[index] = document->GetElementById("character-stat-label-" + suffix);
            statValues_[index] = document->GetElementById("character-stat-value-" + suffix);
            statButtons_[index] = document->GetElementById("character-stat-button-" + suffix);
        }
        for (std::size_t index = 0; index < details_.size(); ++index)
        {
            details_[index] = document->GetElementById("character-detail-" + std::to_string(index));
        }
        smallButtons_[0] = document->GetElementById("character-pet");
        smallButtons_[1] = document->GetElementById("character-master");
        smallButtonLabels_[0] = document->GetElementById("character-pet-label");
        smallButtonLabels_[1] = document->GetElementById("character-master-label");

        const std::array<Rml::Element *, 16> fixed{
            panel_,          drag_,       close_,          title_,           levelLabel_,
            level_,          classLabel_, characterClass_, serverLabel_,     server_,
            experience_,     pointLabel_, points_,         charismaSection_, charismaField_,
            smallButtons_[0]};
        const auto missing = [](const auto &elements) {
            return std::find(elements.begin(), elements.end(), nullptr) != elements.end();
        };
        if (missing(fixed) || smallButtons_[1] == nullptr || missing(smallButtonLabels_) ||
            missing(statLabels_) || missing(statValues_) || missing(statButtons_) ||
            missing(details_))
        {
            Release();
            return false;
        }

        for (std::size_t index = 0; index < statButtons_.size(); ++index)
            buttons_[index].Bind(*statButtons_[index]);
        buttons_[RmlCharacterFrameStatCount].Bind(*smallButtons_[0]);
        buttons_[RmlCharacterFrameStatCount + 1].Bind(*smallButtons_[1]);
        buttons_[RmlCharacterFrameStatCount + 2].Bind(*close_);
        movable_.Bind(*panel_, *drag_);
        viewportWidth_ = 0;
        viewportHeight_ = 0;
        return true;
    }

    bool ApplyContent(const RmlCharacterFrameContent &content)
    {
        bool dirty = SetText(*title_, currentContent_.title, content.title);
        dirty = SetText(*levelLabel_, currentContent_.levelLabel, content.levelLabel) || dirty;
        dirty = SetText(*level_, currentContent_.level, content.level) || dirty;
        dirty = SetText(*classLabel_, currentContent_.classLabel, content.classLabel) || dirty;
        dirty = SetText(*characterClass_, currentContent_.characterClass, content.characterClass) ||
                dirty;
        dirty = SetText(*serverLabel_, currentContent_.serverLabel, content.serverLabel) || dirty;
        dirty = SetText(*server_, currentContent_.server, content.server) || dirty;
        dirty = SetText(*experience_, currentContent_.experience, content.experience) || dirty;
        dirty = SetText(*pointLabel_, currentContent_.pointLabel, content.pointLabel) || dirty;
        dirty = SetText(*points_, currentContent_.points, content.points) || dirty;
        dirty = SetText(*smallButtonLabels_[0], currentContent_.pet, content.pet) || dirty;
        dirty = SetText(*smallButtonLabels_[1], currentContent_.masterLevel, content.masterLevel) ||
                dirty;

        for (std::size_t index = 0; index < statLabels_.size(); ++index)
        {
            dirty = SetText(*statLabels_[index], currentContent_.statLabels[index],
                            content.statLabels[index]) ||
                    dirty;
            dirty = SetText(*statValues_[index], currentContent_.statValues[index],
                            content.statValues[index]) ||
                    dirty;
            if (currentContent_.canIncrease[index] != content.canIncrease[index])
            {
                statButtons_[index]->SetProperty("display",
                                                 content.canIncrease[index] ? "block" : "none");
                dirty = true;
            }
            buttons_[index].SetEnable(content.canIncrease[index]);
        }
        for (std::size_t index = 0; index < details_.size(); ++index)
        {
            dirty =
                SetText(*details_[index], currentContent_.details[index], content.details[index]) ||
                dirty;
        }
        if (currentContent_.darkLord != content.darkLord)
        {
            const char *display = content.darkLord ? "block" : "none";
            charismaSection_->SetProperty("display", display);
            charismaField_->SetProperty("display", display);
            statLabels_.back()->SetProperty("display", display);
            statValues_.back()->SetProperty("display", display);
            dirty = true;
        }
        currentContent_ = content;
        return dirty;
    }

    void PublishGeometry() noexcept
    {
        const RmlMuPanelPosition position = movable_.Position();
        publishedLeft_.store(position.left, std::memory_order_release);
        publishedTop_.store(position.top, std::memory_order_release);
        publishedScale_.store(scale_, std::memory_order_release);
        publishedVisible_.store(visible_, std::memory_order_release);
    }

    std::array<RmlMuButton, RmlCharacterFrameStatCount + 3> &buttons_;
    RmlDocumentHost host_;
    RmlMuMovablePanel movable_;
    Rml::Element *panel_ = nullptr;
    Rml::Element *drag_ = nullptr;
    Rml::Element *close_ = nullptr;
    Rml::Element *title_ = nullptr;
    Rml::Element *levelLabel_ = nullptr;
    Rml::Element *level_ = nullptr;
    Rml::Element *classLabel_ = nullptr;
    Rml::Element *characterClass_ = nullptr;
    Rml::Element *serverLabel_ = nullptr;
    Rml::Element *server_ = nullptr;
    Rml::Element *experience_ = nullptr;
    Rml::Element *pointLabel_ = nullptr;
    Rml::Element *points_ = nullptr;
    Rml::Element *charismaSection_ = nullptr;
    Rml::Element *charismaField_ = nullptr;
    std::array<Rml::Element *, RmlCharacterFrameStatCount> statLabels_{};
    std::array<Rml::Element *, RmlCharacterFrameStatCount> statValues_{};
    std::array<Rml::Element *, RmlCharacterFrameStatCount - 1> details_{};
    std::array<Rml::Element *, RmlCharacterFrameStatCount> statButtons_{};
    std::array<Rml::Element *, 2> smallButtons_{};
    std::array<Rml::Element *, 2> smallButtonLabels_{};
    RmlCharacterFrameContent currentContent_{};
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
    float scale_ = 1.0F;
    bool positionSet_ = false;
    bool inputDirty_ = false;
    bool visible_ = false;
    std::atomic<float> publishedLeft_{0.0F};
    std::atomic<float> publishedTop_{0.0F};
    std::atomic<float> publishedScale_{1.0F};
    std::atomic<bool> publishedVisible_{false};
};

RmlCharacterFramePanel::RmlCharacterFramePanel(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper, buttons_))
{
}

RmlCharacterFramePanel::~RmlCharacterFramePanel() = default;

void RmlCharacterFramePanel::Create()
{
    for (RmlMuButton &button : buttons_)
    {
        button.SetEnable(true);
        button.Reset();
    }
}

void RmlCharacterFramePanel::Release()
{
    impl_->Release();
}

bool RmlCharacterFramePanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}

bool RmlCharacterFramePanel::PrepareOnWorker(int viewportWidth, int viewportHeight, bool visible,
                                             const RmlCharacterFrameContent &content)
{
    return impl_->Prepare(viewportWidth, viewportHeight, visible, content);
}

bool RmlCharacterFramePanel::Record(LegacyRenderFacade &facade) const
{
    return impl_->Record(facade);
}

RmlCharacterFrameRect RmlCharacterFramePanel::ReferenceRect(int viewportWidth,
                                                            int viewportHeight) const noexcept
{
    return impl_->ReferenceRect(viewportWidth, viewportHeight);
}
} // namespace UI::Modern::PC::Character

namespace UI::Modern
{
namespace
{
// Authored design inputs.
enum class PetFrameLayerDesignKey
{
    DragWidth,
    DragHeight,
    MemberHeight,
    ReferenceWidth,
    ReferenceHeight,
    MinimizeX,
    MinimizeY,
    MinimizeWidth,
    MinimizeHeight
};

const RmlUiDesign &PetFrameLayerDesign()
{
    static const RmlUiDesign design(
        "Data/UI/PC/Character/pet_frame.rml",
        {"RmlPetFrameLayer-DragWidth", "RmlPetFrameLayer-DragHeight",
         "RmlPetFrameLayer-MemberHeight", "RmlPetFrameLayer-ReferenceWidth",
         "RmlPetFrameLayer-ReferenceHeight", "RmlPetFrameLayer-MinimizeX",
         "RmlPetFrameLayer-MinimizeY", "RmlPetFrameLayer-MinimizeWidth",
         "RmlPetFrameLayer-MinimizeHeight"});
    return design;
}
// End authored design inputs.

bool SameRequest(const RmlPetFrameRequest &left, const RmlPetFrameRequest &right) noexcept
{
    if (left.x != right.x || left.y != right.y || left.rowCount != right.rowCount ||
        left.minimized != right.minimized || left.buttonState != right.buttonState)
    {
        return false;
    }
    for (int index = 0; index < left.rowCount; ++index)
    {
        const RmlPetFrameRow &a = left.rows[index];
        const RmlPetFrameRow &b = right.rows[index];
        if (a.maximum != b.maximum || a.position != b.position || std::wcscmp(a.name, b.name) != 0)
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
} // namespace

float RmlPetFrameLayer::Width() noexcept
{
    return PetFrameLayerDesign().Number(PetFrameLayerDesignKey::DragWidth);
}
float RmlPetFrameLayer::DragHeight() noexcept
{
    return PetFrameLayerDesign().Number(PetFrameLayerDesignKey::DragHeight);
}
float RmlPetFrameLayer::RowHeight() noexcept
{
    return PetFrameLayerDesign().Number(PetFrameLayerDesignKey::MemberHeight);
}
RmlPetFrameRect RmlPetFrameLayer::MinimizeRect() noexcept
{
    return {PetFrameLayerDesign().Number(PetFrameLayerDesignKey::MinimizeX),
            PetFrameLayerDesign().Number(PetFrameLayerDesignKey::MinimizeY),
            PetFrameLayerDesign().Number(PetFrameLayerDesignKey::MinimizeWidth),
            PetFrameLayerDesign().Number(PetFrameLayerDesignKey::MinimizeHeight)};
}

class RmlPetFrameLayer::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : host_(keeper, "pet-frame-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "Character", "pet_frame.rml"))
    {
    }

    void Stage(const RmlPetFrameRequest &request) noexcept
    {
        pending_ = request;
        pending_.rowCount = std::clamp(pending_.rowCount, 0, RmlPetFrameRequest::RowCapacity);
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
        if (!created_ && pending_.rowCount == 0)
        {
            active_ = pending_;
            return true;
        }
        if (!EnsureDocument(viewportWidth, viewportHeight))
        {
            return false;
        }

        const bool viewportChanged =
            viewportWidth_ != host_.Viewport().width || viewportHeight_ != host_.Viewport().height;
        const bool layoutChanged = initialize || viewportChanged;
        const bool dirty = layoutChanged || !SameRequest(active_, pending_);
        viewportWidth_ = host_.Viewport().width;
        viewportHeight_ = host_.Viewport().height;
        if (dirty)
        {
            Apply(pending_, layoutChanged);
        }
        active_ = pending_;
        if (!host_.SetVisible(active_.rowCount > 0))
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
        if (!host_.Ensure(
                viewportWidth, viewportHeight,
                PetFrameLayerDesign().Number(PetFrameLayerDesignKey::DragWidth),
                PetFrameLayerDesign().Number(PetFrameLayerDesignKey::DragHeight) +
                    RmlPetFrameRequest::RowCapacity *
                        PetFrameLayerDesign().Number(PetFrameLayerDesignKey::MemberHeight)))
        {
            return false;
        }
        if (created_)
        {
            return true;
        }

        Rml::ElementDocument *const document = host_.Document();
        frame_ = document->GetElementById("pet-frame");
        dragbar_ = document->GetElementById("pet-dragbar");
        minimize_ = document->GetElementById("pet-minimize");
        for (int index = 0; index < RmlPetFrameRequest::RowCapacity; ++index)
        {
            members_[index] = document->GetElementById(Rml::CreateString("pet-member-%d", index));
            names_[index] = document->GetElementById(Rml::CreateString("pet-name-%d", index));
            hpBars_[index] = document->GetElementById(Rml::CreateString("pet-hp-%d", index));
        }
        if (frame_ == nullptr || dragbar_ == nullptr || minimize_ == nullptr ||
            std::find(members_.begin(), members_.end(), nullptr) != members_.end() ||
            std::find(names_.begin(), names_.end(), nullptr) != names_.end() ||
            std::find(hpBars_.begin(), hpBars_.end(), nullptr) != hpBars_.end())
        {
            host_.Release();
            return false;
        }
        for (std::size_t index = 0; index < hpBars_.size(); ++index)
            progress_[index].Bind(*hpBars_[index]);
        created_ = true;
        return true;
    }

    void ApplyFrame(const RmlPetFrameRequest &request, bool layoutChanged)
    {
        const bool geometryChanged =
            layoutChanged || request.x != active_.x || request.y != active_.y ||
            request.minimized != active_.minimized || request.rowCount != active_.rowCount;
        if (geometryChanged)
        {
            const float width = PetFrameLayerDesign().Number(PetFrameLayerDesignKey::DragWidth);
            const float height =
                PetFrameLayerDesign().Number(PetFrameLayerDesignKey::DragHeight) +
                (request.minimized ? 0.0F
                                   : request.rowCount * PetFrameLayerDesign().Number(
                                                            PetFrameLayerDesignKey::MemberHeight));
            const float positionScaleX =
                viewportWidth_ /
                PetFrameLayerDesign().Number(PetFrameLayerDesignKey::ReferenceWidth);
            const float positionScaleY =
                viewportHeight_ /
                PetFrameLayerDesign().Number(PetFrameLayerDesignKey::ReferenceHeight);
            const float left = std::clamp(request.x * positionScaleX, 0.0F,
                                          std::max(0.0F, viewportWidth_ - width));
            const float top = std::clamp(request.y * positionScaleY, 0.0F,
                                         std::max(0.0F, viewportHeight_ - height));
            SetPixels(*frame_, "left", left);
            SetPixels(*frame_, "top", top);
            SetPixels(*frame_, "width", width);
            SetPixels(*frame_, "height", height);
        }
        if (layoutChanged)
            frame_->SetProperty("display", "block");
        if (layoutChanged || request.minimized != active_.minimized)
        {
            minimize_->SetClass("restore", request.minimized);
        }
        if (layoutChanged || request.buttonState != active_.buttonState)
        {
            ApplyRmlMuButtonVisualState(*minimize_, request.buttonState);
        }
    }

    void ApplyRow(int index, const RmlPetFrameRequest &request, bool layoutChanged)
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
        const RmlPetFrameRow &row = request.rows[index];
        const RmlPetFrameRow &previous = active_.rows[index];
        if (initialize || std::wcscmp(row.name, previous.name) != 0)
        {
            names_[index]->SetInnerRML(
                Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(row.name)));
        }
        if (initialize || row.maximum != previous.maximum || row.position != previous.position)
        {
            progress_[index].SetProgress(row.position, row.maximum);
        }
    }

    void Apply(const RmlPetFrameRequest &request, bool layoutChanged)
    {
        ApplyFrame(request, layoutChanged);
        for (int index = 0; index < RmlPetFrameRequest::RowCapacity; ++index)
        {
            ApplyRow(index, request, layoutChanged);
        }
    }

    RmlDocumentHost host_;
    Rml::Element *frame_ = nullptr;
    Rml::Element *dragbar_ = nullptr;
    Rml::Element *minimize_ = nullptr;
    std::array<Rml::Element *, RmlPetFrameRequest::RowCapacity> members_{};
    std::array<Rml::Element *, RmlPetFrameRequest::RowCapacity> names_{};
    std::array<Rml::Element *, RmlPetFrameRequest::RowCapacity> hpBars_{};
    std::array<RmlMuProgressBar, RmlPetFrameRequest::RowCapacity> progress_;
    RmlPetFrameRequest active_{};
    RmlPetFrameRequest pending_{};
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
    bool created_ = false;
    bool hasPending_ = false;
};

RmlPetFrameLayer::RmlPetFrameLayer(SessionKeeper &keeper) : impl_(std::make_unique<Impl>(keeper))
{
}

RmlPetFrameLayer::~RmlPetFrameLayer() = default;

void RmlPetFrameLayer::Stage(const RmlPetFrameRequest &request) noexcept
{
    impl_->Stage(request);
}

bool RmlPetFrameLayer::PrepareOnWorker(int viewportWidth, int viewportHeight)
{
    return impl_->Prepare(viewportWidth, viewportHeight);
}

bool RmlPetFrameLayer::Record(LegacyRenderFacade &facade) const
{
    return impl_->Record(facade);
}
} // namespace UI::Modern

namespace UI::Modern::PC::Character
{
namespace
{
// Authored design inputs.
enum class PetInfoPanelDesignKey
{
    PanelWidth,
    PanelHeight,
    ReferenceWidth,
    ReferenceHeight,
    GfxStageWidth,
    InitialX,
    InitialY,
    TabX,
    TabY,
    TabWidth,
    TabHeight,
    SkillColumns
};

const RmlUiDesign &PetInfoPanelDesign()
{
    static const RmlUiDesign design(
        "Data/UI/PC/Character/pet_info.rml",
        {"RmlPetInfoPanel-PanelWidth", "RmlPetInfoPanel-PanelHeight",
         "RmlPetInfoPanel-ReferenceWidth", "RmlPetInfoPanel-ReferenceHeight",
         "RmlPetInfoPanel-GfxStageWidth", "RmlPetInfoPanel-InitialX", "RmlPetInfoPanel-InitialY",
         "RmlPetInfoPanel-TabX", "RmlPetInfoPanel-TabY", "RmlPetInfoPanel-TabWidth",
         "RmlPetInfoPanel-TabHeight", "Skill-Columns"});
    return design;
}
// End authored design inputs.

template <std::size_t Size>
bool HasMissing(const std::array<Rml::Element *, Size> &elements) noexcept
{
    return std::find(elements.begin(), elements.end(), nullptr) != elements.end();
}
} // namespace

RmlPetInfoRect RmlPetInfoTabRect(std::size_t index) noexcept
{
    if (index >= RmlPetInfoTabCount)
        return {};
    return {PetInfoPanelDesign().Values(PetInfoPanelDesignKey::TabX)[index],
            PetInfoPanelDesign().Number(PetInfoPanelDesignKey::TabY),
            PetInfoPanelDesign().Number(PetInfoPanelDesignKey::TabWidth),
            PetInfoPanelDesign().Number(PetInfoPanelDesignKey::TabHeight)};
}

float RmlPetInfoScaleFor(int viewportWidth, int viewportHeight, float maximumScale) noexcept
{
    if (viewportWidth <= 0 || viewportHeight <= 0)
        return 0.0F;
    return std::min(
        {maximumScale, viewportWidth / RmlPetInfoPanel::Width(),
         viewportHeight / (PetInfoPanelDesign().Number(PetInfoPanelDesignKey::InitialY) +
                           RmlPetInfoPanel::Height())});
}

float RmlPetInfoPanel::Width() noexcept
{
    return PetInfoPanelDesign().Number(PetInfoPanelDesignKey::PanelWidth);
}

float RmlPetInfoPanel::Height() noexcept
{
    return PetInfoPanelDesign().Number(PetInfoPanelDesignKey::PanelHeight);
}

class RmlPetInfoPanel::Impl final
{
  public:
    Impl(SessionKeeper &keeper, std::array<RmlMuButton, RmlPetInfoTabCount + 1> &buttons)
        : buttons_(buttons),
          host_(keeper, "pet-info-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "Character", "pet_info.rml"))
    {
    }

    ~Impl()
    {
        Release();
    }

    void Release()
    {
        for (auto &icon : commandIcons_)
            icon.Unbind();
        movable_.Unbind();
        progress_.Unbind();
        scrollBar_.Unbind();
        for (RmlMuButton &button : buttons_)
            button.Unbind();
        panel_ = nullptr;
        drag_ = nullptr;
        close_ = nullptr;
        content_ = nullptr;
        commands_ = nullptr;
        missingPet_ = nullptr;
        progressFill_ = nullptr;
        title_ = nullptr;
        tabs_.fill(nullptr);
        tabLabels_.fill(nullptr);
        labels_.fill(nullptr);
        values_.fill(nullptr);
        commandLabel_ = nullptr;
        leadershipLabel_ = nullptr;
        leadership_ = nullptr;
        skills_.fill(nullptr);
        skillSlots_.fill(nullptr);
        current_ = {};
        contentSet_ = false;
        viewportWidth_ = 0;
        viewportHeight_ = 0;
        scale_ = 1.0F;
        positionSet_ = false;
        inputDirty_ = false;
        visible_ = false;
        host_.Release();
        PublishGeometry();
    }

    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!publishedVisible_.load(std::memory_order_acquire) ||
            event.kind != SessionInputEventKind::Pointer)
        {
            return false;
        }
        const bool wasDragging = movable_.IsDragging();
        (void)host_.ProcessInput(event);
        inputDirty_ = movable_.TakeDirty() || inputDirty_;
        PublishGeometry();
        return wasDragging || movable_.IsDragging() || IsDescendantOf(host_.HoverElement(), panel_);
    }

    bool Prepare(int viewportWidth, int viewportHeight, bool visible,
                 const RmlPetInfoContent &content)
    {
        if (panel_ == nullptr && !visible)
        {
            visible_ = false;
            publishedVisible_.store(false, std::memory_order_release);
            return true;
        }
        if (!EnsureDocument(viewportWidth, viewportHeight))
            return false;
        const RmlUiScaledViewport viewport = host_.Viewport();

        bool dirty = ApplyViewport(viewport);
        dirty = ApplyContent(content) || dirty;
        for (RmlMuButton &button : buttons_)
            dirty = button.SyncVisualState() || dirty;
        const bool visibilityChanged = visible_ != visible;
        if (!host_.SetVisible(visible))
            return false;
        if (!visible && visibilityChanged)
            movable_.CancelDrag();
        visible_ = visible;
        if (visible && (dirty || visibilityChanged))
        {
            RmlMuScrollBarState state;
            state.pageSize = RmlPetInfoSkillCount;
            scrollBar_.Apply(state);
        }
        dirty = visibilityChanged || inputDirty_ || movable_.TakeDirty() || dirty;
        inputDirty_ = false;
        PublishGeometry();
        return host_.CaptureIfDirty(dirty);
    }

    bool Record(LegacyRenderFacade &facade) const
    {
        return panel_ == nullptr || host_.Record(facade);
    }

    RmlPetInfoRect ReferenceRect(int viewportWidth, int viewportHeight) const noexcept
    {
        if (viewportWidth <= 0 || viewportHeight <= 0)
            return {};
        const float left = publishedLeft_.load(std::memory_order_acquire);
        const float top = publishedTop_.load(std::memory_order_acquire);
        const float scale = publishedScale_.load(std::memory_order_acquire);
        return {left * scale * PetInfoPanelDesign().Number(PetInfoPanelDesignKey::ReferenceWidth) /
                    viewportWidth,
                top * scale * PetInfoPanelDesign().Number(PetInfoPanelDesignKey::ReferenceHeight) /
                    viewportHeight,
                RmlPetInfoPanel::Width() * scale *
                    PetInfoPanelDesign().Number(PetInfoPanelDesignKey::ReferenceWidth) /
                    viewportWidth,
                RmlPetInfoPanel::Height() * scale *
                    PetInfoPanelDesign().Number(PetInfoPanelDesignKey::ReferenceHeight) /
                    viewportHeight};
    }

  private:
    bool EnsureDocument(int viewportWidth, int viewportHeight)
    {
        if (!host_.Ensure(viewportWidth, viewportHeight, RmlPetInfoPanel::Width(),
                          PetInfoPanelDesign().Number(PetInfoPanelDesignKey::InitialY) +
                              RmlPetInfoPanel::Height()))
            return false;
        if (panel_ != nullptr)
            return true;
        if (!ResolveElements(*host_.Document()))
        {
            Release();
            return false;
        }
        if (!BindWidgets())
        {
            Release();
            return false;
        }
        viewportWidth_ = 0;
        viewportHeight_ = 0;
        return true;
    }

    bool ResolveElements(Rml::ElementDocument &document)
    {
        panel_ = document.GetElementById("pet-info");
        drag_ = document.GetElementById("pet-info-drag");
        close_ = document.GetElementById("pet-info-close");
        content_ = document.GetElementById("pet-info-content");
        commands_ = document.GetElementById("pet-info-commands");
        missingPet_ = document.GetElementById("pet-info-missing");
        progressFill_ = document.GetElementById("pet-info-progress-fill");
        title_ = document.GetElementById("pet-info-title");
        commandLabel_ = document.GetElementById("pet-info-command-label");
        leadershipLabel_ = document.GetElementById("pet-info-leadership-label");
        leadership_ = document.GetElementById("pet-info-leadership");
        for (std::size_t index = 0; index < tabs_.size(); ++index)
        {
            tabs_[index] = document.GetElementById("pet-info-tab-" + std::to_string(index));
            tabLabels_[index] =
                document.GetElementById("pet-info-tab-label-" + std::to_string(index));
        }
        for (std::size_t index = 0; index < labels_.size(); ++index)
        {
            labels_[index] = document.GetElementById("pet-info-label-" + std::to_string(index));
            values_[index] = document.GetElementById("pet-info-value-" + std::to_string(index));
        }
        for (std::size_t index = 0; index < skills_.size(); ++index)
        {
            skills_[index] =
                document.GetElementById("pet-info-skill-label-" + std::to_string(index));
            skillSlots_[index] =
                document.GetElementById("pet-info-skill-slot-" + std::to_string(index));
        }
        const std::array fixed{panel_,      drag_,         close_, content_,      commands_,
                               missingPet_, progressFill_, title_, commandLabel_, leadershipLabel_,
                               leadership_};
        return !HasMissing(fixed) && !HasMissing(tabs_) && !HasMissing(tabLabels_) &&
               !HasMissing(labels_) && !HasMissing(values_) && !HasMissing(skills_) &&
               !HasMissing(skillSlots_);
    }

    bool BindWidgets()
    {
        for (std::size_t index = 0; index < tabs_.size(); ++index)
            buttons_[index].Bind(*tabs_[index]);
        buttons_[RmlPetInfoTabCount].Bind(*close_);
        for (std::size_t i = 0; i < skillSlots_.size(); ++i)
        {
            auto *icon = skillSlots_[i]->GetFirstChild();
            if (!icon || !icon->GetFirstChild())
                return false;
            commandIcons_[i].Bind(
                *icon, PetInfoPanelDesign().Number<int>(PetInfoPanelDesignKey::SkillColumns));
            commandIcons_[i].Set(
                RmlSkillIconState::FromSkill(AT_PET_COMMAND_DEFAULT + static_cast<int>(i), 0, 0));
        }
        progress_.Bind(*progressFill_);
        movable_.Bind(*panel_, *drag_);
        if (!scrollBar_.Bind(*host_.Document(), "pet-info-scroll"))
            return false;
        return true;
    }

    bool ApplyViewport(const RmlUiScaledViewport &viewport)
    {
        scale_ = viewport.scale;
        if (viewportWidth_ == viewport.width && viewportHeight_ == viewport.height)
            return false;
        viewportWidth_ = viewport.width;
        viewportHeight_ = viewport.height;
        movable_.Configure(viewport.width, viewport.height, RmlPetInfoPanel::Width(),
                           RmlPetInfoPanel::Height());
        if (!positionSet_)
        {
            const float rightMargin =
                PetInfoPanelDesign().Number(PetInfoPanelDesignKey::GfxStageWidth) -
                PetInfoPanelDesign().Number(PetInfoPanelDesignKey::InitialX) -
                RmlPetInfoPanel::Width();
            movable_.SetPosition(
                std::max(0.0F, viewport.width - RmlPetInfoPanel::Width() - rightMargin),
                PetInfoPanelDesign().Number(PetInfoPanelDesignKey::InitialY));
            positionSet_ = true;
        }
        return true;
    }

    bool ApplyContent(const RmlPetInfoContent &content)
    {
        bool dirty = ApplyText(content);
        if (!contentSet_ || current_.selectedTab != content.selectedTab)
        {
            const std::size_t selected = static_cast<std::size_t>(
                std::clamp(content.selectedTab, 0, static_cast<int>(tabs_.size() - 1)));
            for (std::size_t index = 0; index < tabs_.size(); ++index)
                tabs_[index]->SetClass("selected", index == selected);
            dirty = true;
        }
        const bool commandsVisible = content.petPresent && content.selectedTab == 1;
        if (current_.petPresent != content.petPresent)
        {
            content_->SetProperty("display", content.petPresent ? "block" : "none");
            missingPet_->SetProperty("display", content.petPresent ? "none" : "block");
            dirty = true;
        }
        const bool previousCommands = current_.petPresent && current_.selectedTab == 1;
        if (previousCommands != commandsVisible)
        {
            commands_->SetProperty("display", commandsVisible ? "block" : "none");
            dirty = true;
        }
        dirty = progress_.SetProgress(content.experience, content.nextExperience) || dirty;
        current_ = content;
        contentSet_ = true;
        return dirty;
    }

    bool ApplyText(const RmlPetInfoContent &content)
    {
        bool dirty = SetText(*title_, current_.title, content.title);
        for (std::size_t index = 0; index < tabs_.size(); ++index)
            dirty =
                SetText(*tabLabels_[index], current_.tabLabels[index], content.tabLabels[index]) ||
                dirty;
        for (std::size_t index = 0; index < labels_.size(); ++index)
        {
            dirty =
                SetText(*labels_[index], current_.labels[index], content.labels[index]) || dirty;
            dirty =
                SetText(*values_[index], current_.values[index], content.values[index]) || dirty;
        }
        dirty = SetText(*commandLabel_, current_.commandLabel, content.commandLabel) || dirty;
        dirty =
            SetText(*leadershipLabel_, current_.leadershipLabel, content.leadershipLabel) || dirty;
        dirty = SetText(*leadership_, current_.leadership, content.leadership) || dirty;
        dirty = SetText(*missingPet_, current_.missingPet, content.missingPet) || dirty;
        for (std::size_t index = 0; index < skills_.size(); ++index)
            dirty =
                SetText(*skills_[index], current_.skills[index], content.skills[index]) || dirty;
        return dirty;
    }

    void PublishGeometry() noexcept
    {
        const RmlMuPanelPosition position = movable_.Position();
        publishedLeft_.store(position.left, std::memory_order_release);
        publishedTop_.store(position.top, std::memory_order_release);
        publishedScale_.store(scale_, std::memory_order_release);
        publishedVisible_.store(visible_, std::memory_order_release);
    }

    std::array<RmlMuButton, RmlPetInfoTabCount + 1> &buttons_;
    RmlDocumentHost host_;
    RmlMuMovablePanel movable_;
    RmlMuProgressBar progress_;
    RmlMuScrollBar scrollBar_;
    Rml::Element *panel_ = nullptr;
    Rml::Element *drag_ = nullptr;
    Rml::Element *close_ = nullptr;
    Rml::Element *content_ = nullptr;
    Rml::Element *commands_ = nullptr;
    Rml::Element *missingPet_ = nullptr;
    Rml::Element *progressFill_ = nullptr;
    Rml::Element *title_ = nullptr;
    std::array<Rml::Element *, RmlPetInfoTabCount> tabs_{};
    std::array<Rml::Element *, RmlPetInfoTabCount> tabLabels_{};
    std::array<Rml::Element *, 5> labels_{};
    std::array<Rml::Element *, 5> values_{};
    Rml::Element *commandLabel_ = nullptr;
    Rml::Element *leadershipLabel_ = nullptr;
    Rml::Element *leadership_ = nullptr;
    std::array<Rml::Element *, RmlPetInfoSkillCount> skills_{};
    std::array<Rml::Element *, RmlPetInfoSkillCount> skillSlots_{};
    RmlPetInfoContent current_{};
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
    float scale_ = 1.0F;
    bool positionSet_ = false;
    bool inputDirty_ = false;
    bool visible_ = false;
    bool contentSet_ = false;
    std::atomic<float> publishedLeft_{0.0F};
    std::atomic<float> publishedTop_{0.0F};
    std::atomic<float> publishedScale_{1.0F};
    std::atomic<bool> publishedVisible_{false};
    std::array<RmlSkillIcon, RmlPetInfoSkillCount> commandIcons_;
};

RmlPetInfoPanel::RmlPetInfoPanel(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper, buttons_))
{
}

RmlPetInfoPanel::~RmlPetInfoPanel() = default;

void RmlPetInfoPanel::Create()
{
    for (RmlMuButton &button : buttons_)
    {
        button.SetEnable(true);
        button.Reset();
    }
}

void RmlPetInfoPanel::Release()
{
    impl_->Release();
}

bool RmlPetInfoPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}

bool RmlPetInfoPanel::PrepareOnWorker(int viewportWidth, int viewportHeight, bool visible,
                                      const RmlPetInfoContent &content)
{
    return impl_->Prepare(viewportWidth, viewportHeight, visible, content);
}

bool RmlPetInfoPanel::Record(LegacyRenderFacade &facade) const
{
    return impl_->Record(facade);
}

RmlPetInfoRect RmlPetInfoPanel::ReferenceRect(int viewportWidth, int viewportHeight) const noexcept
{
    return impl_->ReferenceRect(viewportWidth, viewportHeight);
}

} // namespace UI::Modern::PC::Character

namespace UI::Modern::PC::Command
{
namespace
{
// Authored design inputs.
enum class CommandWindowPanelDesignKey
{
    PanelWidth,
    PanelHeight,
    ReferenceWidth,
    ReferenceHeight,
    ButtonX,
    ButtonY,
    ButtonStep,
    ButtonWidth,
    ButtonHeight
};

const RmlUiDesign &CommandWindowPanelDesign()
{
    static const RmlUiDesign design(
        "Data/UI/PC/Command/command_window.rml",
        {"RmlCommandWindowPanel-PanelWidth", "RmlCommandWindowPanel-PanelHeight",
         "RmlCommandWindowPanel-ReferenceWidth", "RmlCommandWindowPanel-ReferenceHeight",
         "RmlCommandWindowPanel-ButtonX", "RmlCommandWindowPanel-ButtonY",
         "RmlCommandWindowPanel-ButtonStep", "RmlCommandWindowPanel-ButtonWidth",
         "RmlCommandWindowPanel-ButtonHeight"});
    return design;
}
// End authored design inputs.

bool IsDescendantOf(const Rml::Element *element, const Rml::Element *ancestor) noexcept
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        if (element == ancestor)
            return true;
    }
    return false;
}

bool SetText(Rml::Element &element, std::wstring &current, const std::wstring &next)
{
    if (current == next)
        return false;
    current = next;
    element.SetInnerRML(Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(next.c_str())));
    return true;
}
} // namespace

RmlCommandWindowRect RmlCommandWindowButtonRect(std::size_t index) noexcept
{
    if (index >= RmlCommandWindowButtonCount)
        return {};
    return {CommandWindowPanelDesign().Number(CommandWindowPanelDesignKey::ButtonX),
            CommandWindowPanelDesign().Number(CommandWindowPanelDesignKey::ButtonY) +
                CommandWindowPanelDesign().Number(CommandWindowPanelDesignKey::ButtonStep) * index,
            CommandWindowPanelDesign().Number(CommandWindowPanelDesignKey::ButtonWidth),
            CommandWindowPanelDesign().Number(CommandWindowPanelDesignKey::ButtonHeight)};
}

float RmlCommandWindowScaleFor(int viewportWidth, int viewportHeight, float maximumScale) noexcept
{
    if (viewportWidth <= 0 || viewportHeight <= 0)
        return 0.0F;
    return std::min({maximumScale,
                     static_cast<float>(viewportWidth) / RmlCommandWindowPanel::Width(),
                     static_cast<float>(viewportHeight) / RmlCommandWindowPanel::Height()});
}

float RmlCommandWindowPanel::Width() noexcept
{
    return CommandWindowPanelDesign().Number(CommandWindowPanelDesignKey::PanelWidth);
}

float RmlCommandWindowPanel::Height() noexcept
{
    return CommandWindowPanelDesign().Number(CommandWindowPanelDesignKey::PanelHeight);
}

class RmlCommandWindowPanel::Impl final
{
  public:
    Impl(SessionKeeper &keeper,
         SessionBoundArray<RmlMuButton, RmlCommandWindowButtonCount + 1> &buttons)
        : buttons_(buttons),
          host_(keeper, "command-window-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "Command", "command_window.rml"))
    {
    }

    ~Impl()
    {
        Release();
    }

    void Release()
    {
        movable_.Unbind();
        for (RmlMuButton &button : buttons_)
            button.Unbind();
        panel_ = nullptr;
        background_ = nullptr;
        title_ = nullptr;
        drag_ = nullptr;
        buttonElements_.fill(nullptr);
        labels_.fill(nullptr);
        currentContent_ = {};
        viewportWidth_ = 0;
        viewportHeight_ = 0;
        scale_ = host_.ConfiguredScale();
        positionSet_ = false;
        visible_ = false;
        host_.Release();
        PublishGeometry();
    }

    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!publishedVisible_.load(std::memory_order_acquire) ||
            event.kind != SessionInputEventKind::Pointer)
        {
            return false;
        }
        const bool wasDragging = movable_.IsDragging();
        (void)host_.ProcessInput(event);
        inputDirty_ = movable_.TakeDirty() || inputDirty_;
        PublishGeometry();
        return wasDragging || movable_.IsDragging() || IsDescendantOf(host_.HoverElement(), panel_);
    }

    bool Prepare(int viewportWidth, int viewportHeight, bool visible,
                 const RmlCommandWindowContent &content)
    {
        if (panel_ == nullptr && !visible)
        {
            visible_ = false;
            publishedVisible_.store(false, std::memory_order_release);
            return true;
        }
        if (!EnsureDocument(viewportWidth, viewportHeight))
            return false;
        const RmlUiScaledViewport viewport = host_.Viewport();

        const bool viewportChanged =
            viewportWidth_ != viewport.width || viewportHeight_ != viewport.height;
        const bool visibilityChanged = visible_ != visible;
        viewportWidth_ = viewport.width;
        viewportHeight_ = viewport.height;
        scale_ = viewport.scale;
        if (viewportChanged)
        {
            movable_.Configure(viewport.width, viewport.height, RmlCommandWindowPanel::Width(),
                               RmlCommandWindowPanel::Height());
            if (!positionSet_)
            {
                movable_.SetPosition(
                    std::max(0.0F, viewport.width - RmlCommandWindowPanel::Width()), 0.0F);
                positionSet_ = true;
            }
        }

        bool dirty = viewportChanged || visibilityChanged || inputDirty_ || movable_.TakeDirty();
        dirty = ApplyContent(content) || dirty;
        for (RmlMuButton &button : buttons_)
            dirty = button.SyncVisualState() || dirty;

        if (!host_.SetVisible(visible))
            return false;
        if (!visible && visibilityChanged)
        {
            movable_.CancelDrag();
        }
        visible_ = visible;
        inputDirty_ = false;
        PublishGeometry();
        return host_.CaptureIfDirty(dirty);
    }

    bool Record(LegacyRenderFacade &facade) const
    {
        return panel_ == nullptr || host_.Record(facade);
    }

    RmlCommandWindowRect ReferenceRect(int viewportWidth, int viewportHeight) const noexcept
    {
        if (viewportWidth <= 0 || viewportHeight <= 0)
            return {};
        const float left = publishedLeft_.load(std::memory_order_acquire);
        const float top = publishedTop_.load(std::memory_order_acquire);
        const float scale = publishedScale_.load(std::memory_order_acquire);
        return {
            left * scale *
                CommandWindowPanelDesign().Number(CommandWindowPanelDesignKey::ReferenceWidth) /
                viewportWidth,
            top * scale *
                CommandWindowPanelDesign().Number(CommandWindowPanelDesignKey::ReferenceHeight) /
                viewportHeight,
            RmlCommandWindowPanel::Width() * scale *
                CommandWindowPanelDesign().Number(CommandWindowPanelDesignKey::ReferenceWidth) /
                viewportWidth,
            RmlCommandWindowPanel::Height() * scale *
                CommandWindowPanelDesign().Number(CommandWindowPanelDesignKey::ReferenceHeight) /
                viewportHeight};
    }

  private:
    bool EnsureDocument(int viewportWidth, int viewportHeight)
    {
        if (!host_.Ensure(viewportWidth, viewportHeight, RmlCommandWindowPanel::Width(),
                          RmlCommandWindowPanel::Height()))
            return false;
        if (panel_ != nullptr)
            return true;

        Rml::ElementDocument *const document = host_.Document();
        panel_ = document->GetElementById("command-window");
        background_ = document->GetElementById("command-window-back");
        title_ = document->GetElementById("command-window-title");
        drag_ = document->GetElementById("command-window-drag");
        Rml::Element *const close = document->GetElementById("command-window-close");
        for (std::size_t index = 0; index < RmlCommandWindowButtonCount; ++index)
        {
            buttonElements_[index] =
                document->GetElementById("command-button-" + std::to_string(index));
            labels_[index] = document->GetElementById("command-label-" + std::to_string(index));
        }
        const bool missingFixed = panel_ == nullptr || background_ == nullptr ||
                                  title_ == nullptr || drag_ == nullptr || close == nullptr;
        const bool missingButton =
            std::find(buttonElements_.begin(), buttonElements_.end(), nullptr) !=
                buttonElements_.end() ||
            std::find(labels_.begin(), labels_.end(), nullptr) != labels_.end();
        if (missingFixed || missingButton)
        {
            Release();
            return false;
        }

        for (std::size_t index = 0; index < RmlCommandWindowButtonCount; ++index)
        {
            buttons_[index].Bind(*buttonElements_[index]);
        }
        buttons_[RmlCommandWindowButtonCount].Bind(*close);
        close_ = close;
        movable_.Bind(*panel_, *drag_);
        viewportWidth_ = 0;
        viewportHeight_ = 0;
        return true;
    }

    bool ApplyContent(const RmlCommandWindowContent &content)
    {
        bool dirty = SetText(*title_, currentContent_.title, content.title);
        for (std::size_t index = 0; index < RmlCommandWindowButtonCount; ++index)
        {
            dirty =
                SetText(*labels_[index], currentContent_.labels[index], content.labels[index]) ||
                dirty;
            const bool selected = content.selected == static_cast<int>(index);
            const bool wasSelected = currentContent_.selected == static_cast<int>(index);
            if (selected != wasSelected)
            {
                buttonElements_[index]->SetClass("selected", selected);
                dirty = true;
            }
        }
        currentContent_.selected = content.selected;
        return dirty;
    }

    void PublishGeometry() noexcept
    {
        const RmlMuPanelPosition position = movable_.Position();
        publishedLeft_.store(position.left, std::memory_order_release);
        publishedTop_.store(position.top, std::memory_order_release);
        publishedScale_.store(scale_, std::memory_order_release);
        publishedVisible_.store(visible_, std::memory_order_release);
    }

    SessionBoundArray<RmlMuButton, RmlCommandWindowButtonCount + 1> &buttons_;
    RmlDocumentHost host_;
    RmlMuMovablePanel movable_;
    Rml::Element *panel_ = nullptr;
    Rml::Element *background_ = nullptr;
    Rml::Element *title_ = nullptr;
    Rml::Element *drag_ = nullptr;
    Rml::Element *close_ = nullptr;
    std::array<Rml::Element *, RmlCommandWindowButtonCount> buttonElements_{};
    std::array<Rml::Element *, RmlCommandWindowButtonCount> labels_{};
    RmlCommandWindowContent currentContent_{};
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
    float scale_ = 1.0F;
    bool positionSet_ = false;
    bool inputDirty_ = false;
    bool visible_ = false;
    std::atomic<float> publishedLeft_{0.0F};
    std::atomic<float> publishedTop_{0.0F};
    std::atomic<float> publishedScale_{1.0F};
    std::atomic<bool> publishedVisible_{false};
};

RmlCommandWindowPanel::RmlCommandWindowPanel(SessionKeeper &keeper)
    : buttons_(keeper), impl_(std::make_unique<Impl>(keeper, buttons_))
{
}

RmlCommandWindowPanel::~RmlCommandWindowPanel() = default;

void RmlCommandWindowPanel::Create()
{
    for (RmlMuButton &button : buttons_)
    {
        button.SetEnable(true);
        button.Reset();
    }
}

void RmlCommandWindowPanel::Release()
{
    impl_->Release();
}

bool RmlCommandWindowPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}

bool RmlCommandWindowPanel::PrepareOnWorker(int viewportWidth, int viewportHeight, bool visible,
                                            const RmlCommandWindowContent &content)
{
    return impl_->Prepare(viewportWidth, viewportHeight, visible, content);
}

bool RmlCommandWindowPanel::Record(LegacyRenderFacade &facade) const
{
    return impl_->Record(facade);
}

RmlCommandWindowRect RmlCommandWindowPanel::ReferenceRect(int viewportWidth,
                                                          int viewportHeight) const noexcept
{
    return impl_->ReferenceRect(viewportWidth, viewportHeight);
}
} // namespace UI::Modern::PC::Command

namespace UI::Modern::PC::Gens
{
class RmlGensRankingPanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "Gens", "gens_ranking.rml")),
          design_(path_, {"Panel-Size", "Panel-Reference", "Panel-Initial"}),
          host_(keeper, "gens-" + std::to_string(keeper.Id().RawValue()), path_)
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
        movable_.Unbind();
        panel_ = nullptr;
        labels_.fill(nullptr);
        marks_.fill(nullptr);
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
                                 "tfGensTeam",
                                 "tfGensTeamValue",
                                 "tfGensRanking",
                                 "tfGensRankingValue",
                                 "tfRanking",
                                 "tfRankingValue",
                                 "tfGrade",
                                 "tfContribution",
                                 "tfContributionValue",
                                 "tfGensInfo",
                                 "taGensState",
                                 "taGensInfoMent",
                                 "btnClose"};
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            labels_[i] = document.GetElementById(ids[i]);
            if (!labels_[i])
                return false;
        }
        marks_[0] = document.GetElementById("mcMark_D");
        marks_[1] = document.GetElementById("mcMark_V");
        return marks_[0] && marks_[1] && areas_[0].Bind(document, "taGensState") &&
               areas_[1].Bind(document, "taGensInfoMent");
    }
    bool ApplyContent(const Content &content)
    {
        if (revision_ == content.revision)
            return false;
        constexpr std::size_t LabelCount = 11, TextAreaStart = LabelCount;
        for (std::size_t i = 0; i < LabelCount; ++i)
            labels_[i]->SetInnerRML(Rml::StringUtilities::EncodeRml(
                StringUtils::WideToNarrow(content.labels[i].c_str())));
        for (std::size_t i = 0; i < areas_.size(); ++i)
            areas_[i].SetMarkup(Rml::StringUtilities::EncodeRml(
                StringUtils::WideToNarrow(content.labels[TextAreaStart + i].c_str())));
        labels_.back()->SetAttribute("title",
                                     StringUtils::WideToNarrow(content.labels.back().c_str()));
        for (std::size_t i = 0; i < marks_.size(); ++i)
        {
            marks_[i]->SetClassNames("art rank-" + std::to_string(content.rank));
            marks_[i]->SetProperty("display", content.faction == int(i + 1) ? "block" : "none");
        }
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
    RmlMuButton close_;
    std::array<RmlMuTextArea, 2> areas_;
    Rml::Element *panel_ = nullptr;
    std::array<Rml::Element *, 14> labels_{};
    std::array<Rml::Element *, 2> marks_{};
    std::optional<std::uint64_t> revision_;
    Changes changes_;
    float left_ = 0, top_ = 0, width_ = 0, height_ = 0;
    bool visible_ = false, positioned_ = false, inputDirty_ = false;
};
RmlGensRankingPanel::RmlGensRankingPanel(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper))
{
}
RmlGensRankingPanel::~RmlGensRankingPanel() = default;
void RmlGensRankingPanel::Release()
{
    impl_->Release();
}
bool RmlGensRankingPanel::PrepareOnWorker(int width, int height, bool visible,
                                          const Content &content)
{
    return impl_->Prepare(width, height, visible, content);
}
bool RmlGensRankingPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
RmlGensRankingPanel::Changes RmlGensRankingPanel::TakeChanges()
{
    return std::exchange(impl_->changes_, {});
}
bool RmlGensRankingPanel::ContainsReferencePointer(int x, int y) const
{
    return impl_->visible_ && x >= impl_->left_ && y >= impl_->top_ &&
           x < impl_->left_ + impl_->width_ && y < impl_->top_ + impl_->height_;
}
bool RmlGensRankingPanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->panel_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::Gens

namespace UI::Modern
{
class RmlBuffLayer::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "HUD", "buff_list.rml")),
          design_(path_, {"Buff-Size", "Buff-Cell", "Buff-Resize", "Buff-Columns", "Buff-Icons"}),
          host_(keeper, "buff-list-" + std::to_string(keeper.Id().RawValue()), path_)
    {
        for (const auto value : design_.Values(4))
        {
            const auto id = static_cast<std::size_t>(value);
            if (icons_.size() <= id)
                icons_.resize(id + 1);
            icons_[id] = true;
        }
    }
    ~Impl()
    {
        Release();
    }

    void Release()
    {
        cells_.clear();
        root_ = nullptr;
        lists_ = {};
        previous_.reset();
        position_.reset();
        hovered_ = cancel_ = 0;
        visible_ = dirty_ = false;
        host_.Release();
    }
    bool Bind()
    {
        auto &document = *host_.Document();
        root_ = document.GetElementById("buff-list");
        lists_ = {document.GetElementById("_BuffList"), document.GetElementById("_DeBuffList")};
        return root_ && lists_[0] && lists_[1];
    }
    struct Cell
    {
        RmlMuSlot slot;
        Rml::Element *element = nullptr;
        int id = 0;
    };
    void Apply(const std::vector<Entry> &entries)
    {
        cells_.clear();
        for (auto *list : lists_)
            list->SetInnerRML("");
        const auto geometry = design_.Values(1), columns = design_.Values(3);
        std::array<int, 2> count{};
        for (const auto &entry : entries)
        {
            const int group = entry.debuff ? 1 : 0;
            const int index = count[group]++;
            auto cell = std::make_unique<Cell>();
            auto element = host_.Document()->CreateElement("div");
            element->SetClassNames(entry.debuff ? "mu-button deBuffIcon" : "mu-button BuffIcon");
            element->SetInnerRML("<i class=\"buff-image buff-" + std::to_string(entry.id) +
                                 "\"/><i class=\"buff-edge edge-" + std::to_string(entry.border) +
                                 "\"/>");
            const float x = (index % static_cast<int>(columns[group])) *
                            (geometry[0] + geometry[2]) * (entry.debuff ? 1 : -1);
            const float y =
                (index / static_cast<int>(columns[group])) * (geometry[1] + geometry[3]);
            element->SetProperty(Rml::PropertyId::Left, Rml::Property(x, Rml::Unit::PX));
            element->SetProperty(Rml::PropertyId::Top, Rml::Property(y, Rml::Unit::PX));
            cell->element = lists_[group]->AppendChild(std::move(element));
            cell->slot.Bind(*cell->element);
            cell->id = entry.id;
            cells_.push_back(std::move(cell));
        }
        previous_ = entries;
        hovered_ = cancel_ = 0;
    }
    void Position(int height)
    {
        const auto size = design_.Values(0), resize = design_.Values(2);
        const auto viewport = host_.Viewport();
        const bool small = height < resize[0];
        const float sourceScale = small ? resize[1] : 1.0f;
        const Rml::Vector2f position(
            (std::floor((viewport.width * sourceScale - size[0]) / 2) + resize[4]) / sourceScale,
            (small ? resize[2] : resize[3]) / sourceScale);
        if (position_ == position)
            return;
        root_->SetProperty(Rml::PropertyId::Left, Rml::Property(position.x, Rml::Unit::PX));
        root_->SetProperty(Rml::PropertyId::Top, Rml::Property(position.y, Rml::Unit::PX));
        position_ = position;
        dirty_ = true;
    }
    bool Prepare(int width, int height, bool visible, const std::vector<Entry> &entries)
    {
        if (!root_ && !visible)
            return true;
        const auto size = design_.Values(0), resize = design_.Values(2);
        if (!host_.Ensure(width, height, size[0], size[1], height < resize[0] ? resize[1] : 1.0f))
            return false;
        if (!root_ && !Bind())
        {
            Release();
            return false;
        }
        if (!host_.SetVisible(visible))
            return false;
        visible_ = visible;
        physicalWidth_ = width;
        physicalHeight_ = height;
        if (visible && previous_ != entries)
        {
            Apply(entries);
            dirty_ = true;
        }
        Position(height);
        if (!visible)
        {
            hovered_ = cancel_ = 0;
            for (auto &cell : cells_)
                cell->slot.Reset();
        }
        for (auto &cell : cells_)
            dirty_ |= cell->slot.SyncVisualState();
        if (!host_.CaptureIfDirty(dirty_))
            return false;
        if (!host_.HoverElement())
            hovered_ = 0;
        dirty_ = false;
        return true;
    }
    bool Input(const SessionInputEvent &event)
    {
        if (!visible_)
            return false;
        host_.ProcessInput(event);
        hovered_ = 0;
        const auto *target = host_.HoverElement();
        for (auto &cell : cells_)
        {
            if (cell->element == target)
            {
                hovered_ = cell->id;
                const auto offset = cell->element->GetAbsoluteOffset();
                const auto size = cell->element->GetBox().GetSize();
                const auto viewport = host_.Viewport();
                anchorX_ = (offset.x + size.x / 2) * physicalWidth_ / viewport.width;
                anchorY_ = (offset.y + size.y) * physicalHeight_ / viewport.height;
            }
            if (cell->slot.IsClear())
                cancel_ = cell->id;
            dirty_ |= cell->slot.SyncVisualState();
        }
        return event.kind == SessionInputEventKind::Pointer && hovered_ != 0;
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    Rml::Element *root_ = nullptr;
    std::array<Rml::Element *, 2> lists_{};
    std::vector<std::unique_ptr<Cell>> cells_;
    std::vector<bool> icons_;
    std::optional<std::vector<Entry>> previous_;
    std::optional<Rml::Vector2f> position_;
    int hovered_ = 0, cancel_ = 0;
    int physicalWidth_ = 0, physicalHeight_ = 0;
    float anchorX_ = 0, anchorY_ = 0;
    bool visible_ = false, dirty_ = false;
};
RmlBuffLayer::RmlBuffLayer(SessionKeeper &keeper) : impl_(std::make_unique<Impl>(keeper))
{
}
RmlBuffLayer::~RmlBuffLayer() = default;
void RmlBuffLayer::Release()
{
    impl_->Release();
}
bool RmlBuffLayer::PrepareOnWorker(int width, int height, bool visible,
                                   const std::vector<Entry> &entries)
{
    return impl_->Prepare(width, height, visible, entries);
}
bool RmlBuffLayer::ProcessInput(const SessionInputEvent &event)
{
    return impl_->Input(event);
}
int RmlBuffLayer::TakeCancel()
{
    return std::exchange(impl_->cancel_, 0);
}
int RmlBuffLayer::HoveredBuff() const
{
    return impl_->hovered_;
}
bool RmlBuffLayer::HasIcon(int id) const
{
    return static_cast<std::size_t>(id) < impl_->icons_.size() && impl_->icons_[id];
}
bool RmlBuffLayer::HoverAnchor(float &x, float &y) const
{
    if (!impl_->hovered_)
        return false;
    x = impl_->anchorX_;
    y = impl_->anchorY_;
    return true;
}
bool RmlBuffLayer::Record(LegacyRenderFacade &facade) const
{
    return !impl_->root_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern

namespace UI::Modern
{
namespace
{
Rml::Vector2f Position(Rml::Element &element)
{
    return {element.GetProperty(Rml::PropertyId::Left)->Get<float>(),
            element.GetProperty(Rml::PropertyId::Top)->Get<float>()};
}

Rml::Vertex InterpolateTextVertex(const Rml::Vertex &start, const Rml::Vertex &end, float t)
{
    const auto color = [t](Rml::byte a, Rml::byte b) {
        return static_cast<Rml::byte>(std::lround(std::lerp(float(a), float(b), t)));
    };
    return {start.position + (end.position - start.position) * t,
            {color(start.colour.red, end.colour.red),
             color(start.colour.green, end.colour.green),
             color(start.colour.blue, end.colour.blue),
             color(start.colour.alpha, end.colour.alpha)},
            start.tex_coord + (end.tex_coord - start.tex_coord) * t};
}

void ClipTextPolygon(std::vector<Rml::Vertex> &polygon, std::vector<Rml::Vertex> &scratch,
                     int edge, float boundary)
{
    if (polygon.empty())
        return;
    scratch.clear();
    const auto coordinate = [edge](const Rml::Vertex &vertex) {
        return edge < 2 ? vertex.position.x : vertex.position.y;
    };
    const auto inside = [edge, boundary, &coordinate](const Rml::Vertex &vertex) {
        return edge % 2 == 0 ? coordinate(vertex) >= boundary : coordinate(vertex) <= boundary;
    };
    auto start = polygon.back();
    bool startInside = inside(start);
    for (const auto &end : polygon)
    {
        const bool endInside = inside(end);
        if (startInside != endInside)
        {
            const float t = (boundary - coordinate(start)) / (coordinate(end) - coordinate(start));
            scratch.push_back(InterpolateTextVertex(start, end, t));
        }
        if (endInside)
            scratch.push_back(end);
        start = end;
        startInside = endInside;
    }
    polygon.swap(scratch);
}

Rml::Mesh ClipTextMesh(const Rml::Mesh &source, Rml::Vector2f position, Rml::Vector2f size)
{
    Rml::Mesh result;
    std::vector<Rml::Vertex> polygon, scratch;
    for (std::size_t index = 0; index < source.indices.size(); index += 3)
    {
        polygon = {source.vertices[source.indices[index]], source.vertices[source.indices[index + 1]],
                   source.vertices[source.indices[index + 2]]};
        ClipTextPolygon(polygon, scratch, 0, position.x);
        ClipTextPolygon(polygon, scratch, 1, position.x + size.x);
        ClipTextPolygon(polygon, scratch, 2, position.y);
        ClipTextPolygon(polygon, scratch, 3, position.y + size.y);
        if (polygon.size() < 3)
            continue;
        const int base = static_cast<int>(result.vertices.size());
        result.vertices.insert(result.vertices.end(), polygon.begin(), polygon.end());
        for (std::size_t point = 2; point < polygon.size(); ++point)
            result.indices.insert(result.indices.end(), {base, base + static_cast<int>(point - 1),
                                                          base + static_cast<int>(point)});
    }
    return result;
}
} // namespace
void RmlHudMapMarkers::Bind(Rml::ElementDocument &document)
{
    labels_ = {document.GetElementById("npc-label"), document.GetElementById("portal-label")};
    const std::array ids{40, 23, 24, 25, 26};
    for (std::size_t i = 0; i < ids.size(); ++i)
        art_[i] = document.GetElementById("map-art-" + std::to_string(ids[i]));
    for (auto *element : labels_)
        if (!element)
            throw std::runtime_error("Missing map label prototype");
    for (auto *element : art_)
        if (!element)
            throw std::runtime_error("Missing map sprite prototype");
    auto *origin = document.GetElementById("npc-origin");
    if (!origin)
        throw std::runtime_error("Missing NPC label origin");
    origins_ = {origin, document.GetElementById("portal-origin")};
    if (!origins_[1])
        throw std::runtime_error("Missing portal origin");
    const RmlUiDesign labelDesign(document.GetSourceURL(), {"Map-LabelExtra", "Map-PortalGrid"});
    labelExtra_ = labelDesign.Number(0);
    const auto grid = labelDesign.Values(1);
    portalGrid_ = {grid[0], grid[1]};
    const auto path =
        std::filesystem::path(document.GetSourceURL()).parent_path() / "hud_map_npc_label.rml";
    backgroundSource_ = LoadRmlVectorMesh(path);
    const RmlUiDesign design(path, {"Clip-Color"});
    const auto color = design.Values(0);
    backgroundColor_ = Rml::Colourb(
        static_cast<Rml::byte>(color[0] * 255), static_cast<Rml::byte>(color[1] * 255),
        static_cast<Rml::byte>(color[2] * 255), static_cast<Rml::byte>(color[3] * 255));
    opacity_ = -1;
}
void RmlHudMapMarkers::Set(std::span<const WorldMinimapData::Marker> markers)
{
    markers_.clear();
    markers_.reserve(markers.size());
    // Source MapRoot places the portal container above the NPC container.
    for (int kind : {1, 2})
        for (const auto &marker : markers)
            if (marker.kind == kind)
                // Server tile coordinates place actors at the center of their tile.
                markers_.push_back(
                    {{(marker.location[1] + 0.5F) / 256, (marker.location[0] + 0.5F) / 256},
                     marker.kind - 1,
                     StringUtils::WideToNarrow(marker.name.data()),
                     {}});
    opacity_ = -1;
}
RmlHudMapMarkers::Part RmlHudMapMarkers::Sprite(Rml::RenderManager &manager, int index,
                                                Rml::Vector2f offset, float opacity, float width)
{
    const std::array ids{40, 23, 24, 25, 26};
    auto &element = *art_[index];
    const auto *sprite =
        element.GetStyleSheet()->GetSprite("hudMap.Bitmap" + std::to_string(ids[index]));
    if (!sprite)
        throw std::runtime_error("Missing map sprite");
    const auto texture = sprite->sprite_sheet->texture_source.GetTexture(manager);
    const Rml::Vector2f dimensions(texture.GetDimensions());
    auto position = Position(element), size = element.GetBox().GetSize();
    if (index != 0)
    {
        const float original = origins_[1]->GetBox().GetSize().x;
        const auto warp = [&](float x) {
            if (x <= portalGrid_[0])
                return x;
            if (x >= portalGrid_[1])
                return x + width - original;
            return portalGrid_[0] + (x - portalGrid_[0]) *
                                        (width - original + portalGrid_[1] - portalGrid_[0]) /
                                        (portalGrid_[1] - portalGrid_[0]);
        };
        size.x = warp(position.x + size.x) - warp(position.x);
        position.x = warp(position.x);
    }
    Rml::Mesh mesh;
    Rml::MeshUtilities::GenerateQuad(
        mesh, offset + position, size, Rml::Colourb(255).ToPremultiplied(opacity),
        sprite->rectangle.TopLeft() / dimensions, sprite->rectangle.BottomRight() / dimensions);
    return {manager.MakeGeometry(std::move(mesh)), texture};
}
void RmlHudMapMarkers::BuildLabel(Marker &marker, Rml::RenderManager &manager, float opacity)
{
    auto &label = *labels_[marker.kind];
    auto &fonts = *Rml::GetFontEngineInterface();
    const Rml::String language;
    const Rml::TextShapingContext shaping{language};
    const auto font = label.GetFontFaceHandle();
    const float textWidth = fonts.GetStringWidth(font, marker.name, shaping);
    const auto original = origins_[marker.kind]->GetBox().GetSize();
    auto size = label.GetBox().GetSize(), position = Position(label),
         origin = Position(*origins_[marker.kind]);
    const float width = original.x - size.x + textWidth + labelExtra_;
    size.x = textWidth + labelExtra_;
    if (marker.kind == 0)
    {
        position.x += (original.x - width) / 2;
        origin.x += (original.x - width) / 2;
    }
    marker.art.clear();
    if (marker.kind == 1)
    {
        for (int i = 1; i < 5; ++i)
            marker.art.push_back(Sprite(manager, i, origin, opacity, width));
    }
    else
    {
        auto background = backgroundSource_;
        for (auto &vertex : background.vertices)
        {
            vertex.position.x *= width / original.x;
            vertex.position += origin;
            vertex.colour = backgroundColor_.ToPremultiplied(opacity);
        }
        marker.background = manager.MakeGeometry(std::move(background));
    }
    const auto &metrics = fonts.GetFontMetrics(font);
    const float x = marker.kind == 0 ? (size.x - textWidth) / 2 : 0;
    const float baseline = (size.y - metrics.ascent - metrics.descent) / 2 + metrics.ascent;
    Rml::TexturedMeshList meshes;
    fonts.GenerateString(
        manager, font, {}, marker.name, position + Rml::Vector2f(x, baseline),
        label.GetProperty(Rml::PropertyId::Color)->Get<Rml::Colourb>().ToPremultiplied(opacity),
        opacity, shaping, meshes);
    marker.text.clear();
    for (auto &mesh : meshes)
        marker.text.push_back({manager.MakeGeometry(ClipTextMesh(mesh.mesh, position, size)),
                               mesh.texture});
}
void RmlHudMapMarkers::Build(Rml::RenderManager &manager, float opacity)
{
    npcImage_ = Sprite(manager, 0, {}, opacity);
    auto &fonts = *Rml::GetFontEngineInterface();
    const Rml::String language;
    const Rml::TextShapingContext shaping{language};
    for (const auto &marker : markers_)
        fonts.GetStringWidth(labels_[marker.kind]->GetFontFaceHandle(), marker.name, shaping);
    for (auto &marker : markers_)
        BuildLabel(marker, manager, opacity);
    for (int kind = 0; kind < 2; ++kind)
        fontVersions_[kind] = fonts.GetVersion(labels_[kind]->GetFontFaceHandle());
    opacity_ = opacity;
}
void RmlHudMapMarkers::Render(RmlHudMapViewport &viewport, float opacity, bool namesVisible)
{
    if (!labels_[0])
        return;
    auto &manager = *viewport.GetRenderManager();
    auto &fonts = *Rml::GetFontEngineInterface();
    if (opacity_ != opacity ||
        fontVersions_[0] != fonts.GetVersion(labels_[0]->GetFontFaceHandle()) ||
        fontVersions_[1] != fonts.GetVersion(labels_[1]->GetFontFaceHandle()))
        Build(manager, opacity);
    const auto saved = manager.GetState();
    const auto normalized = viewport.MarkerTransform();
    for (auto &marker : markers_)
    {
        const auto position = viewport.GetAbsoluteOffset() + viewport.Project(marker.uv);
        const auto transform =
            saved.transform * Rml::Matrix4f::Translate(position.x, position.y, 0) * normalized;
        manager.SetTransform(&transform);
        if (marker.kind == 0)
            npcImage_.geometry.Render({}, npcImage_.texture);
        else
            for (const auto &part : marker.art)
                part.geometry.Render({}, part.texture);
        if (namesVisible || marker.kind == 1)
        {
            if (marker.kind == 0)
                marker.background.Render({});
            for (const auto &part : marker.text)
                part.geometry.Render({}, part.texture);
        }
    }
    manager.SetState(saved);
}
} // namespace UI::Modern

namespace UI::Modern
{
RmlHudMapViewport::RmlHudMapViewport(const Rml::String &tag) : Rml::Element(tag)
{
}

void RmlHudMapViewport::BindMarkers(Rml::ElementDocument &document, float rotation)
{
    markers_.Bind(document);
    markerRotation_ = rotation;
}
void RmlHudMapViewport::SetMarkers(std::span<const WorldMinimapData::Marker> markers)
{
    markers_.Set(markers);
}
bool RmlHudMapViewport::SetMarkerView(float opacity, bool namesVisible)
{
    const bool changed = markerOpacity_ != opacity || namesVisible_ != namesVisible;
    markerOpacity_ = opacity;
    namesVisible_ = namesVisible;
    return changed;
}
Rml::Matrix4f RmlHudMapViewport::MarkerTransform() const noexcept
{
    const float angle = markerRotation_ * std::numbers::pi_v<float> / 180;
    const float c = std::cos(angle), s = std::sin(angle);
    const auto rotation =
        Rml::Matrix4f::FromRows({rotation_[0], rotation_[2], 0, 0},
                                {rotation_[1], rotation_[3], 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1});
    const auto counter =
        Rml::Matrix4f::FromRows({c, -s, 0, 0}, {s, c, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1});
    return Rml::Matrix4f::Scale(scale_.x, scale_.y, 1) * rotation * counter *
           Rml::Matrix4f::Scale(1 / scale_.x, 1 / scale_.y, 1);
}

void RmlHudMapViewport::ConfigureMask(const std::filesystem::path &path,
                                      const std::array<float, 4> &rotation)
{
    const RmlUiDesign design(path, {"Clip-Size"});
    const auto size = design.Values(0);
    if (size.size() != 2 || !std::isfinite(size[0]) || !std::isfinite(size[1]) || size[0] <= 0 ||
        size[1] <= 0)
        throw std::runtime_error("Invalid map mask size: " + path.string());
    maskSize_ = {size[0], size[1]};
    rotation_ = rotation;
    mask_ = GetRenderManager()->MakeGeometry(LoadRmlVectorMesh(path));
}

bool RmlHudMapViewport::SetImage(const LogicalRenderAssetMetadata &image)
{
    if (asset_ == image.Asset)
        return false;
    asset_ = image.Asset;
    imageSize_ = {image.Width, image.Height};
    texture_ = GetRenderManager()->MakeCallbackTexture(
        [handle = image.BitmapIndex,
         size = Rml::Vector2i(imageSize_)](const Rml::CallbackTextureInterface &target) {
            target.SetTextureHandle(handle, size);
            return true;
        });
    imageDirty_ = true;
    return true;
}

void RmlHudMapViewport::ClearImage()
{
    texture_.Release();
    image_.Release();
    asset_ = {};
    imageSize_ = {};
    imageDirty_ = false;
}

bool RmlHudMapViewport::SetImageOrigin(Rml::Vector2f pixels)
{
    const bool changed = imageOriginPixels_ != pixels;
    imageOriginPixels_ = pixels;
    return changed;
}

bool RmlHudMapViewport::SetView(Rml::Vector2f heroUv, Rml::Vector2f scale, float opacity)
{
    const bool changed = heroUv_ != heroUv || scale_ != scale || opacity_ != opacity;
    imageDirty_ = imageDirty_ || opacity_ != opacity;
    heroUv_ = heroUv;
    scale_ = scale;
    opacity_ = opacity;
    return changed;
}

Rml::Vector2f RmlHudMapViewport::ImagePoint(Rml::Vector2f uv) const noexcept
{
    // Adapt the installed S6 map's image axes to the S16 map plane.
    // The source's +45 degree rotation then preserves the native world orientation.
    return {uv.y * imageSize_.y, (1 - uv.x) * imageSize_.x};
}

Rml::Vector2f RmlHudMapViewport::Project(Rml::Vector2f uv) const noexcept
{
    return maskSize_ / 2 + ProjectDirection(uv - heroUv_);
}

Rml::Vector2f RmlHudMapViewport::ProjectDirection(Rml::Vector2f uv) const noexcept
{
    const Rml::Vector2f point(uv.y * imageSize_.y, -uv.x * imageSize_.x);
    return Rml::Vector2f(scale_.x * (rotation_[0] * point.x + rotation_[2] * point.y),
                         scale_.y * (rotation_[1] * point.x + rotation_[3] * point.y));
}

void RmlHudMapViewport::BuildImage()
{
    Rml::Mesh mesh = image_.Release(Rml::Geometry::ReleaseMode::ClearMesh);
    const auto color = Rml::Colourb(255).ToPremultiplied(opacity_);
    for (const auto uv : {Rml::Vector2f{0, 0}, {1, 0}, {1, 1}, {0, 1}})
        mesh.vertices.push_back({ImagePoint(uv), color, uv});
    mesh.indices = {0, 1, 2, 0, 2, 3};
    image_ = GetRenderManager()->MakeGeometry(std::move(mesh));
    imageDirty_ = false;
}

void RmlHudMapViewport::OnRender()
{
    if (!mask_ || !texture_)
        return;
    if (imageDirty_)
        BuildImage();
    auto &manager = *GetRenderManager();
    const auto saved = manager.GetState();
    const auto offset = GetAbsoluteOffset();
    auto clips = saved.clip_mask_list;
    clips.push_back(
        {clips.empty() ? Rml::ClipMaskOperation::Set : Rml::ClipMaskOperation::Intersect, &mask_,
         offset, &saved.transform});
    manager.SetClipMask(std::move(clips));
    const auto center = offset + maskSize_ / 2;
    const auto hero =
        ImagePoint(heroUv_) + Rml::Vector2f(imageOriginPixels_.y, -imageOriginPixels_.x);
    const auto rotation =
        Rml::Matrix4f::FromRows({rotation_[0], rotation_[2], 0, 0},
                                {rotation_[1], rotation_[3], 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1});
    const auto transform = saved.transform * Rml::Matrix4f::Translate(center.x, center.y, 0) *
                           Rml::Matrix4f::Scale(scale_.x, scale_.y, 1) * rotation *
                           Rml::Matrix4f::Translate(-hero.x, -hero.y, 0);
    manager.SetTransform(&transform);
    image_.Render({}, texture_);
    manager.SetTransform(&saved.transform);
    markers_.Render(*this, markerOpacity_, namesVisible_);
    manager.SetState(saved);
}
} // namespace UI::Modern

namespace UI::Modern
{
namespace
{
enum class MainFrameLayerDesignKey
{
    ResizeStageHeight,
    SmallStageScale,
    ArtWidth,
    ArtHeight,
    HotSelectionX,
    HotSelectionStep,
    MarbleHeight,
    LegacyReferenceWidth,
    LegacyReferenceHeight,
    SkillColumns
};

const RmlUiDesign &MainFrameLayerDesign()
{
    static const RmlUiDesign design(
        "Data/UI/PC/HUD/main_frame.rml",
        {"RmlMainFrameLayer-ResizeStageHeight", "RmlMainFrameLayer-SmallStageScale",
         "RmlMainFrameLayer-ArtWidth", "RmlMainFrameLayer-ArtHeight",
         "RmlMainFrameLayer-HotSelectionX", "RmlMainFrameLayer-HotSelectionStep",
         "RmlMainFrameLayer-MarbleHeight", "RmlMainFrameLayer-LegacyReferenceWidth",
         "RmlMainFrameLayer-LegacyReferenceHeight", "Skill-Columns"});
    return design;
}

float Ratio(int value, int maximum) noexcept
{
    return maximum > 0 ? std::clamp(static_cast<float>(value) / maximum, 0.0F, 1.0F) : 0.0F;
}

bool SameRequest(const RmlMainFrameRequest &left, const RmlMainFrameRequest &right) noexcept
{
    return left.skillIcons == right.skillIcons && left.skillCooldowns == right.skillCooldowns &&
           left.buttons == right.buttons && left.currentExperience == right.currentExperience &&
           left.nextExperience == right.nextExperience && left.life == right.life &&
           left.maximumLife == right.maximumLife && left.mana == right.mana &&
           left.maximumMana == right.maximumMana && left.shield == right.shield &&
           left.maximumShield == right.maximumShield && left.ability == right.ability &&
           left.maximumAbility == right.maximumAbility &&
           left.experiencePage == right.experiencePage &&
           left.selectedHotSkillSlot == right.selectedHotSkillSlot &&
           left.experienceRatio == right.experienceRatio && left.visible == right.visible &&
           left.poisoned == right.poisoned &&
           left.skillSelectionVisible == right.skillSelectionVisible &&
           left.skillSecondPage == right.skillSecondPage &&
           left.skillPageButton == right.skillPageButton &&
           left.experienceStyle == right.experienceStyle;
}

void SetText(Rml::Element &element, std::int64_t value)
{
    element.SetInnerRML(std::to_string(value));
}

} // namespace

RmlMainFrameTransform CalculateRmlMainFrameTransform(int viewportWidth, int viewportHeight,
                                                     float maximumScale) noexcept
{
    const float stageWidth = viewportWidth / maximumScale;
    const float stageHeight = viewportHeight / maximumScale;
    const float stageScale =
        stageHeight < MainFrameLayerDesign().Number(MainFrameLayerDesignKey::ResizeStageHeight)
            ? MainFrameLayerDesign().Number(MainFrameLayerDesignKey::SmallStageScale)
            : 1.0F;
    return {
        std::floor((stageWidth -
                    MainFrameLayerDesign().Number(MainFrameLayerDesignKey::ArtWidth) * stageScale) /
                   2.0F) *
            maximumScale,
        std::floor(stageHeight -
                   MainFrameLayerDesign().Number(MainFrameLayerDesignKey::ArtHeight) * stageScale) *
            maximumScale,
        maximumScale * stageScale};
}

RmlMainFrameRect CalculateRmlMainFrameReferenceRect(const RmlMainFrameTransform &transform,
                                                    int viewportWidth, int viewportHeight, float x,
                                                    float y, float width, float height) noexcept
{
    const float referenceScaleX =
        MainFrameLayerDesign().Number(MainFrameLayerDesignKey::LegacyReferenceWidth) /
        viewportWidth;
    const float referenceScaleY =
        MainFrameLayerDesign().Number(MainFrameLayerDesignKey::LegacyReferenceHeight) /
        viewportHeight;
    return {(transform.left + x * transform.scale) * referenceScaleX,
            (transform.top + y * transform.scale) * referenceScaleY,
            width * transform.scale * referenceScaleX, height * transform.scale * referenceScaleY};
}

class RmlMainFrameLayer::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : host_(keeper, "main-frame-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "HUD", "main_frame.rml"))
    {
    }

    ~Impl()
    {
        for (auto &icon : skillIcons_)
            icon.Unbind();
        for (auto &cooldown : skillCooldowns_)
            cooldown.Unbind();
        hpBar_.Unbind();
        mpBar_.Unbind();
        sdBar_.Unbind();
        agBar_.Unbind();
        expBar_.Unbind();
    }

    void Stage(const RmlMainFrameRequest &request) noexcept
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
    bool BindSkillIcons(Rml::ElementDocument &document)
    {
        for (int i = 0; i < RmlMainFrameRequest::SkillSlotCount; ++i)
        {
            auto *icon = document.GetElementById("main-skill-icon-" + std::to_string(i));
            if (!icon || icon->GetNumChildren() != 2)
                return false;
            skillElements_[i] = icon;
            skillBounds_[i] = {icon->GetProperty("left")->Get<float>(),
                               icon->GetProperty("top")->Get<float>(),
                               icon->GetProperty("width")->Get<float>(),
                               icon->GetProperty("height")->Get<float>()};
            skillIcons_[i].Bind(
                *icon, MainFrameLayerDesign().Number<int>(MainFrameLayerDesignKey::SkillColumns));
            skillCooldowns_[i].Bind(*icon->GetChild(1), RmlMuProgressBar::Axis::Vertical);
        }
        return true;
    }

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
        frame_ = document->GetElementById("main-frame");
        shell_ = document->GetElementById("main-shell");
        hpBackground_ = document->GetElementById("main-hp-background");
        hpClip_ = document->GetElementById("main-hp-clip");
        hp_ = document->GetElementById("main-hp");
        mpBackground_ = document->GetElementById("main-mp-background");
        mpClip_ = document->GetElementById("main-mp-clip");
        mp_ = document->GetElementById("main-mp");
        sdClip_ = document->GetElementById("main-sd-clip");
        sd_ = document->GetElementById("main-sd");
        agClip_ = document->GetElementById("main-ag-clip");
        ag_ = document->GetElementById("main-ag");
        expClip_ = document->GetElementById("main-exp-clip");
        exp_ = document->GetElementById("main-exp");
        expFrame_ = document->GetElementById("main-exp-frame");
        hpText_ = document->GetElementById("main-hp-text");
        mpText_ = document->GetElementById("main-mp-text");
        sdText_ = document->GetElementById("main-sd-text");
        agText_ = document->GetElementById("main-ag-text");
        expCurrent_ = document->GetElementById("main-exp-current");
        expNext_ = document->GetElementById("main-exp-next");
        expSlash_ = document->GetElementById("main-exp-slash");
        expPage_ = document->GetElementById("main-exp-page");
        currentSkillSelection_ = document->GetElementById("main-current-selection");
        hotSkillSelection_ = document->GetElementById("main-hot-selection");
        skillPageLabels_ = document->GetElementById("main-skill-page-labels");
        skillPageToggle_ = document->GetElementById("main-skill-page-toggle");
        constexpr std::array<const char *, RmlMainFrameRequest::ButtonCount> ButtonIds{
            "main-button-shop",  "main-button-character", "main-button-inventory",
            "main-button-quest", "main-button-community", "main-button-system"};
        for (int index = 0; index < RmlMainFrameRequest::ButtonCount; ++index)
        {
            buttons_[index] = document->GetElementById(ButtonIds[index]);
        }

        const std::array<Rml::Element *, 26> required{frame_,
                                                      shell_,
                                                      hpBackground_,
                                                      hpClip_,
                                                      hp_,
                                                      mpBackground_,
                                                      mpClip_,
                                                      mp_,
                                                      sdClip_,
                                                      sd_,
                                                      agClip_,
                                                      ag_,
                                                      expClip_,
                                                      exp_,
                                                      expFrame_,
                                                      hpText_,
                                                      mpText_,
                                                      sdText_,
                                                      agText_,
                                                      expCurrent_,
                                                      expNext_,
                                                      expPage_,
                                                      currentSkillSelection_,
                                                      hotSkillSelection_,
                                                      skillPageLabels_,
                                                      skillPageToggle_};
        if (std::find(required.begin(), required.end(), nullptr) != required.end() ||
            expSlash_ == nullptr ||
            std::find(buttons_.begin(), buttons_.end(), nullptr) != buttons_.end())
        {
            host_.Release();
            return false;
        }
        if (!BindSkillIcons(*document))
            return false;
        hpBar_.Bind(*hpClip_, RmlMuProgressBar::Axis::Vertical);
        mpBar_.Bind(*mpClip_, RmlMuProgressBar::Axis::Vertical);
        sdBar_.Bind(*sd_);
        agBar_.Bind(*ag_);
        expBar_.Bind(*exp_);
        created_ = true;
        return true;
    }

    void ApplyLayout()
    {
        const auto transform = CalculateRmlMainFrameTransform(viewportWidth_, viewportHeight_,
                                                              host_.ConfiguredScale());
        SetPixels(*frame_, "left", transform.left / host_.Viewport().scale);
        SetPixels(*frame_, "top", transform.top / host_.Viewport().scale);
        frame_->SetProperty(
            "transform",
            Rml::CreateString("scale(%.6f)", transform.scale / host_.Viewport().scale));
        // Keep sheet viewports outside the transformed art: the tape backend supports scissor clips.
        const float scale = transform.scale / host_.Viewport().scale;
        for (int i = 0; i < RmlMainFrameRequest::SkillSlotCount; ++i)
        {
            const auto &bounds = skillBounds_[i];
            SetPixels(*skillElements_[i], "left",
                      transform.left / host_.Viewport().scale + bounds.x * scale);
            SetPixels(*skillElements_[i], "top",
                      transform.top / host_.Viewport().scale + bounds.y * scale);
            SetPixels(*skillElements_[i], "width", bounds.width * scale);
            SetPixels(*skillElements_[i], "height", bounds.height * scale);
        }
    }

    void ApplyVitals(const RmlMainFrameRequest &request, bool layoutChanged)
    {
        if (layoutChanged || request.life != active_.life ||
            request.maximumLife != active_.maximumLife)
        {
            hpBar_.SetProgress(
                std::floor(MainFrameLayerDesign().Number(MainFrameLayerDesignKey::MarbleHeight) *
                           Ratio(request.life, request.maximumLife)),
                MainFrameLayerDesign().Number(MainFrameLayerDesignKey::MarbleHeight));
            hpText_->SetInnerRML(std::to_string(request.life) + "/" +
                                 std::to_string(request.maximumLife));
        }
        if (layoutChanged || request.poisoned != active_.poisoned)
        {
            hp_->SetClass("poisoned", request.poisoned);
        }
        if (layoutChanged || request.mana != active_.mana ||
            request.maximumMana != active_.maximumMana)
        {
            mpBar_.SetProgress(
                std::floor(MainFrameLayerDesign().Number(MainFrameLayerDesignKey::MarbleHeight) *
                           Ratio(request.mana, request.maximumMana)),
                MainFrameLayerDesign().Number(MainFrameLayerDesignKey::MarbleHeight));
            mpText_->SetInnerRML(std::to_string(request.mana) + "/" +
                                 std::to_string(request.maximumMana));
        }
        if (layoutChanged || request.shield != active_.shield ||
            request.maximumShield != active_.maximumShield)
        {
            sdBar_.SetProgress(request.shield, request.maximumShield);
            sdText_->SetInnerRML(std::to_string(request.shield) + " / " +
                                 std::to_string(request.maximumShield));
        }
        if (layoutChanged || request.ability != active_.ability ||
            request.maximumAbility != active_.maximumAbility)
        {
            agBar_.SetProgress(request.ability, request.maximumAbility);
            agText_->SetInnerRML(std::to_string(request.ability) + " / " +
                                 std::to_string(request.maximumAbility));
        }
    }

    void ApplyExperience(const RmlMainFrameRequest &request, bool layoutChanged)
    {
        if (layoutChanged || request.experienceRatio != active_.experienceRatio)
        {
            expBar_.SetProgress(request.experienceRatio, 1.0);
        }
        if (layoutChanged || request.experienceStyle != active_.experienceStyle)
        {
            exp_->SetClass("normal",
                           request.experienceStyle == RmlMainFrameExperienceStyle::Normal);
            exp_->SetClass("master",
                           request.experienceStyle == RmlMainFrameExperienceStyle::Master);
            exp_->SetClass("fourth",
                           request.experienceStyle == RmlMainFrameExperienceStyle::Fourth);
        }
        if (layoutChanged || request.currentExperience != active_.currentExperience)
        {
            SetText(*expCurrent_, request.currentExperience);
        }
        if (layoutChanged || request.nextExperience != active_.nextExperience)
        {
            SetText(*expNext_, request.nextExperience);
        }
        if (layoutChanged || request.experiencePage != active_.experiencePage)
        {
            SetText(*expPage_, request.experiencePage);
        }
    }

    void ApplySkillIcons(const RmlMainFrameRequest &request)
    {
        for (int i = 0; i < RmlMainFrameRequest::SkillSlotCount; ++i)
        {
            skillIcons_[i].Set(request.skillIcons[i]);
            skillCooldowns_[i].SetProgress(request.skillCooldowns[i], 1.0);
        }
    }

    void ApplySkills(const RmlMainFrameRequest &request, bool layoutChanged)
    {
        if (layoutChanged || request.skillSelectionVisible != active_.skillSelectionVisible)
        {
            currentSkillSelection_->SetProperty("display",
                                                request.skillSelectionVisible ? "block" : "none");
        }
        if (layoutChanged || request.skillSelectionVisible != active_.skillSelectionVisible ||
            request.selectedHotSkillSlot != active_.selectedHotSkillSlot)
        {
            const bool visible = request.skillSelectionVisible &&
                                 request.selectedHotSkillSlot >= 0 &&
                                 request.selectedHotSkillSlot < 5;
            SetPixels(
                *hotSkillSelection_, "left",
                MainFrameLayerDesign().Number(MainFrameLayerDesignKey::HotSelectionX) +
                    request.selectedHotSkillSlot *
                        MainFrameLayerDesign().Number(MainFrameLayerDesignKey::HotSelectionStep));
            hotSkillSelection_->SetProperty("display", visible ? "block" : "none");
        }
        if (layoutChanged || request.skillSecondPage != active_.skillSecondPage)
        {
            skillPageLabels_->SetClass("page-one", !request.skillSecondPage);
            skillPageLabels_->SetClass("page-two", request.skillSecondPage);
            skillPageToggle_->SetClass("to-first", request.skillSecondPage);
            skillPageToggle_->SetClass("to-second", !request.skillSecondPage);
        }
        if (layoutChanged || request.skillPageButton != active_.skillPageButton)
        {
            ApplyRmlMuButtonVisualState(*skillPageToggle_, request.skillPageButton);
        }
    }

    void ApplyButtons(const RmlMainFrameRequest &request, bool layoutChanged)
    {
        for (int index = 0; index < RmlMainFrameRequest::ButtonCount; ++index)
        {
            if (!layoutChanged && request.buttons[index] == active_.buttons[index])
            {
                continue;
            }
            ApplyRmlMuButtonVisualState(*buttons_[index], request.buttons[index]);
        }
    }

    void Apply(const RmlMainFrameRequest &request, bool layoutChanged)
    {
        if (layoutChanged)
        {
            ApplyLayout();
        }
        if (layoutChanged || request.visible != active_.visible)
        {
            frame_->SetProperty("display", request.visible ? "block" : "none");
        }
        ApplyVitals(request, layoutChanged);
        ApplyExperience(request, layoutChanged);
        ApplySkills(request, layoutChanged);
        ApplySkillIcons(request);
        ApplyButtons(request, layoutChanged);
    }

    std::array<RmlSkillIcon, RmlMainFrameRequest::SkillSlotCount> skillIcons_;
    std::array<Rml::Element *, RmlMainFrameRequest::SkillSlotCount> skillElements_{};
    std::array<RmlMainFrameRect, RmlMainFrameRequest::SkillSlotCount> skillBounds_{};
    std::array<RmlMuProgressBar, RmlMainFrameRequest::SkillSlotCount> skillCooldowns_;
    RmlDocumentHost host_;
    RmlMuProgressBar hpBar_, mpBar_, sdBar_, agBar_, expBar_;
    Rml::Element *frame_ = nullptr;
    Rml::Element *shell_ = nullptr;
    Rml::Element *hpBackground_ = nullptr;
    Rml::Element *hpClip_ = nullptr;
    Rml::Element *hp_ = nullptr;
    Rml::Element *mpBackground_ = nullptr;
    Rml::Element *mpClip_ = nullptr;
    Rml::Element *mp_ = nullptr;
    Rml::Element *sdClip_ = nullptr;
    Rml::Element *sd_ = nullptr;
    Rml::Element *agClip_ = nullptr;
    Rml::Element *ag_ = nullptr;
    Rml::Element *expClip_ = nullptr;
    Rml::Element *exp_ = nullptr;
    Rml::Element *expFrame_ = nullptr;
    Rml::Element *hpText_ = nullptr;
    Rml::Element *mpText_ = nullptr;
    Rml::Element *sdText_ = nullptr;
    Rml::Element *agText_ = nullptr;
    Rml::Element *expCurrent_ = nullptr;
    Rml::Element *expNext_ = nullptr;
    Rml::Element *expSlash_ = nullptr;
    Rml::Element *expPage_ = nullptr;
    Rml::Element *currentSkillSelection_ = nullptr;
    Rml::Element *hotSkillSelection_ = nullptr;
    Rml::Element *skillPageLabels_ = nullptr;
    Rml::Element *skillPageToggle_ = nullptr;
    std::array<Rml::Element *, RmlMainFrameRequest::ButtonCount> buttons_{};
    RmlMainFrameRequest active_{};
    RmlMainFrameRequest pending_{};
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
    float configuredScale_ = 0.0F;
    bool created_ = false;
    bool hasPending_ = false;
};

RmlMainFrameLayer::RmlMainFrameLayer(SessionKeeper &keeper) : impl_(std::make_unique<Impl>(keeper))
{
}

RmlMainFrameLayer::~RmlMainFrameLayer() = default;

void RmlMainFrameLayer::Stage(const RmlMainFrameRequest &request) noexcept
{
    impl_->Stage(request);
}

bool RmlMainFrameLayer::PrepareOnWorker(int viewportWidth, int viewportHeight)
{
    return impl_->Prepare(viewportWidth, viewportHeight);
}

bool RmlMainFrameLayer::Record(LegacyRenderFacade &facade) const
{
    return impl_->Record(facade);
}
} // namespace UI::Modern

namespace UI::Modern
{
class RmlMasterSkillTreePanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : design_(
              "Data/UI/PC/HUD/master_tree.rml",
              {"Panel-Size", "Skill-Columns", "Legacy-ReferenceSize", "Tooltip-BottomThreshold"}),
          host_(keeper, "master-tree-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "HUD", "master_tree.rml"))
    {
    }
    ~Impl()
    {
        Release();
    }
    struct Cell
    {
        RmlMuButton button;
        RmlSkillIcon icon;
        Rml::Element *slot = nullptr;
        Rml::Element *rank = nullptr;
        Rml::Element *hit = nullptr;
    };
    void Release()
    {
        close_.Unbind();
        movable_.Unbind();
        if (cells_)
            for (int i = 0; i < SlotCount; ++i)
            {
                cells_[i].button.Unbind();
                cells_[i].icon.Unbind();
            }
        cells_.reset();
        panel_ = exp_ = nullptr;
        labels_.fill(nullptr);
        previous_.reset();
        visible_ = false;
        ownsPointer_ = false;
        hovered_ = 0;
        dirty_ = false;
        host_.Release();
    }
    bool Bind()
    {
        auto *doc = host_.Document();
        panel_ = doc->GetElementById("master-tree");
        exp_ = doc->GetElementById("btnExp");
        auto *close = doc->GetElementById("btnClose");
        auto *drag = doc->GetElementById("btnDrag");
        if (!panel_ || !exp_ || !close || !drag)
            return false;
        close_.Bind(*close);
        movable_.Bind(*panel_, *drag);
        constexpr std::array names{"tfClass",      "tfLevel",      "tfPoint",     "tfExp",
                                   "tfAttribute1", "tfAttribute2", "tfAttribute3"};
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            labels_[i] = doc->GetElementById(names[i]);
            if (!labels_[i])
                return false;
        }
        cells_ = std::make_unique<Cell[]>(SlotCount);
        for (int i = 0; i < SlotCount; ++i)
        {
            auto &cell = cells_[i];
            cell.slot = doc->GetElementById("mcSkillSlot" + std::to_string(i + 1));
            if (!cell.slot || cell.slot->GetNumChildren() != 11)
                return false;
            cell.hit = cell.slot->GetChild(9);
            cell.rank = cell.slot->GetChild(10);
            cell.hit->SetAttribute("data-master-slot", i + 1);
            cell.button.Bind(*cell.hit);
            cell.icon.Bind(*cell.hit->GetFirstChild(), design_.Number<int>(1));
        }
        return true;
    }
    bool Apply(const State &state)
    {
        if (previous_ == state)
            return false;
        for (std::size_t i = 0; i < labels_.size(); ++i)
            if (!previous_ || previous_->labels[i] != state.labels[i])
                labels_[i]->SetInnerRML(Rml::StringUtilities::EncodeRml(
                    StringUtils::WideToNarrow(state.labels[i].c_str())));
        for (int i = 0; i < SlotCount; ++i)
        {
            const auto &slot = state.slots[i];
            if (previous_ && previous_->slots[i] == slot)
                continue;
            auto &cell = cells_[i];
            cell.slot->SetProperty("display", slot.icon.sheet >= 0 ? "block" : "none");
            cell.button.SetVisible(slot.icon.sheet >= 0);
            cell.button.SyncVisualState();
            cell.slot->SetClassNames("master-slot direction-" + std::to_string(slot.arrow));
            cell.icon.Set(slot.icon);
            cell.rank->SetInnerRML(std::to_string(slot.rank));
        }
        previous_ = state;
        return true;
    }
    void PublishHover()
    {
        int index = 0;
        Rml::Element *target = nullptr;
        bool inside = movable_.IsDragging();
        for (auto *element = host_.HoverElement(); element; element = element->GetParentNode())
        {
            if (element == exp_)
            {
                index = -1;
                target = exp_;
            }
            const int slot = element->GetAttribute<int>("data-master-slot", 0);
            if (slot)
            {
                index = slot;
                target = element;
            }
            if (element == panel_)
            {
                inside = true;
                break;
            }
        }
        if (target)
        {
            const auto position = target->GetAbsoluteOffset();
            const auto size = target->GetBox().GetSize();
            const auto view = host_.Viewport();
            const auto reference = design_.Values(2);
            hoverX_ = position.x * reference[0] / view.width;
            hoverY_ = (position.y + size.y) * reference[1] / view.height;
            above_ = position.y * reference[1] / view.height > design_.Number(3);
        }
        hovered_ = inside ? index : 0;
        ownsPointer_ = inside;
    }
    bool Prepare(int width, int height, bool visible, const State &state)
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
        const auto view = host_.Viewport();
        movable_.Configure(view.width, view.height, size[0], size[1]);
        if (!host_.SetVisible(visible))
            return false;
        const bool wasVisible = visible_;
        visible_ = visible;
        if (wasVisible && !visible)
        {
            close_.Reset();
            for (int i = 0; i < SlotCount; ++i)
                cells_[i].button.Reset();
        }
        if (!visible)
        {
            movable_.CancelDrag();
            host_.ResetInteraction();
            hovered_ = 0;
            ownsPointer_ = false;
        }
        const bool changed = Apply(state);
        const bool moved = movable_.TakeDirty();
        const bool captured = host_.CaptureIfDirty(changed || moved || dirty_);
        dirty_ = false;
        if (visible)
            PublishHover();
        return captured;
    }
    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!visible_)
            return false;
        if (event.kind != SessionInputEventKind::Pointer)
        {
            if (event.action == SessionInputAction::WindowFocusLost)
            {
                host_.ProcessInput(event);
                movable_.CancelDrag();
                hovered_ = 0;
                ownsPointer_ = false;
            }
            return false;
        }
        const bool dragging = movable_.IsDragging();
        (void)host_.ProcessInput(event);
        dirty_ = movable_.TakeDirty() || dirty_;
        PublishHover();
        return dragging || ownsPointer_;
    }
    RmlUiDesign design_;
    RmlDocumentHost host_;
    RmlMuButton close_;
    RmlMuMovablePanel movable_;
    std::unique_ptr<Cell[]> cells_;
    std::array<Rml::Element *, 7> labels_{};
    Rml::Element *panel_ = nullptr;
    Rml::Element *exp_ = nullptr;
    std::optional<State> previous_;
    std::atomic<bool> visible_{false}, ownsPointer_{false}, above_{false};
    std::atomic<int> hovered_{0};
    std::atomic<float> hoverX_{0}, hoverY_{0};
    bool dirty_ = false;
};
RmlMasterSkillTreePanel::RmlMasterSkillTreePanel(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper))
{
}
RmlMasterSkillTreePanel::~RmlMasterSkillTreePanel() = default;
void RmlMasterSkillTreePanel::Release()
{
    impl_->Release();
}
bool RmlMasterSkillTreePanel::PrepareOnWorker(int width, int height, bool visible,
                                              const State &state)
{
    return impl_->Prepare(width, height, visible, state);
}
bool RmlMasterSkillTreePanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
bool RmlMasterSkillTreePanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->panel_ || impl_->host_.Record(facade);
}
bool RmlMasterSkillTreePanel::TakeClose()
{
    return impl_->close_.IsClick();
}
int RmlMasterSkillTreePanel::TakeSkillClick()
{
    if (!impl_->cells_)
        return 0;
    for (int i = 0; i < SlotCount; ++i)
        if (impl_->cells_[i].button.IsClick())
            return i + 1;
    return 0;
}
bool RmlMasterSkillTreePanel::OwnsPointer() const
{
    return impl_->ownsPointer_;
}
RmlMasterSkillTreePanel::Hover RmlMasterSkillTreePanel::Hovered() const
{
    return {impl_->hovered_, impl_->hoverX_, impl_->hoverY_, impl_->above_};
}
} // namespace UI::Modern

namespace UI::Modern
{
namespace
{
void Position(Rml::Element &element, float x, float y)
{
    element.SetProperty(Rml::PropertyId::Left, Rml::Property(x, Rml::Unit::PX));
    element.SetProperty(Rml::PropertyId::Top, Rml::Property(y, Rml::Unit::PX));
}
} // namespace
class RmlMiniMapPanel::Impl final
{
  public:
    enum Design
    {
        Size,
        ScaleX,
        ScaleY,
        Alpha,
        CenterY,
        HelpDelay,
        HelpFade,
        SmallScale,
        Rotation,
        FrameRate,
        AlphaStep,
        AlphaMin,
        AlphaMax,
        ScaleStep,
        ScaleMin,
        ScaleMax,
        ImageAlpha,
        IconAlpha,
        SmallHeight,
        NameScale,
        IconRotation,
        HeroFrames,
        HelpCurve
    };
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "HUD", "minimap.rml")),
          design_(path_, {"Map-MaskSize",         "Map-InitialScaleX",    "Map-InitialScaleY",
                          "Map-InitialAlpha",     "Map-CenterYOffset",    "Map-HelpDelayMs",
                          "Map-HelpFadeSeconds",  "Map-SmallScale",       "Map-RotationMatrix",
                          "Map-FrameRate",        "Map-AlphaStep",        "Map-AlphaMin",
                          "Map-AlphaMax",         "Map-ScaleStep",        "Map-ScaleMin",
                          "Map-ScaleMax",         "Map-ImageAlphaFactor", "Map-IconAlphaOffset",
                          "Map-SmallStageHeight", "Map-NameScale",        "Map-IconRotation",
                          "Map-HeroFrames",       "Map-HelpFadeCurve"}),
          host_(keeper, "minimap-" + std::to_string(keeper.Id().RawValue()), path_),
          zoom_(design_.Number(ScaleY) * 100), alpha_(design_.Number(Alpha))
    {
    }
    ~Impl()
    {
        Release();
    }
    void Release()
    {
        view_ = nullptr;
        hero_ = help_ = nullptr;
        markerRevision_ = 0;
        visible_ = false;
        headingDirection_ = {};
        width_ = height_ = 0;
        frame_ = -1;
        host_.Release();
    }
    bool Bind()
    {
        auto &document = *host_.Document();
        auto *target = document.GetElementById("map-host");
        hero_ = document.GetElementById("map-hero");
        help_ = document.GetElementById("map-help");
        if (!target || !hero_ || !help_)
            return false;
        auto element = instancer_.InstanceElement(&document, "map-view", {});
        element->SetId("map-viewport");
        element->SetInstancer(&instancer_);
        view_ = static_cast<RmlHudMapViewport *>(target->AppendChild(std::move(element)));
        const auto size = design_.Values(Size), rotation = design_.Values(Rotation);
        view_->SetProperty(Rml::PropertyId::Width, Rml::Property(size[0], Rml::Unit::PX));
        view_->SetProperty(Rml::PropertyId::Height, Rml::Property(size[1], Rml::Unit::PX));
        view_->ConfigureMask(path_.parent_path() / "hud_map_mask.rml",
                             {rotation[0], rotation[1], rotation[2], rotation[3]});
        view_->BindMarkers(document, design_.Number(IconRotation));
        return true;
    }
    bool Prepare(int width, int height, bool visible, const LogicalRenderAssetMetadata *image,
                 float heroX, float heroY, float heroHeading,
                 std::span<const WorldMinimapData::Marker> markers, std::uint64_t revision,
                 const std::array<int, 2> &imageOriginPixels)
    {
        visible = visible && image;
        if (!view_ && !visible)
            return true;
        const float small = height < design_.Number(SmallHeight) ? design_.Number(SmallScale) : 1;
        if (!host_.Ensure(width, height, 1, 1, small))
            return false;
        if (!view_ && !Bind())
        {
            Release();
            return false;
        }
        if (!host_.SetVisible(visible))
            return false;
        bool dirty = false;
        if (!visible)
        {
            if (!image)
            {
                view_->ClearImage();
                view_->SetMarkers({});
                markerRevision_ = 0;
            }
            visible_ = false;
            return host_.CaptureIfDirty(false);
        }
        const auto now = std::chrono::steady_clock::now();
        if (!visible_)
        {
            opened_ = now;
            dirty = true;
        }
        visible_ = true;
        dirty |= PositionPanel(width, height, small);
        dirty |= view_->SetImage(*image);
        dirty |= view_->SetImageOrigin(Rml::Vector2f(imageOriginPixels[0], imageOriginPixels[1]));
        if (markerRevision_ != revision)
        {
            view_->SetMarkers(markers);
            markerRevision_ = revision;
            dirty = true;
        }
        const float scaleX = std::floor(zoom_ * design_.Number(ScaleX) / design_.Number(ScaleY));
        dirty |= view_->SetView({heroY / 256, heroX / 256}, {scaleX / 100, zoom_ / 100},
                                std::floor(alpha_ * design_.Number(ImageAlpha)) / 100);
        dirty |= SetHeading(heroHeading);
        dirty |= view_->SetMarkerView((alpha_ + design_.Number(IconAlpha)) / 100,
                                      zoom_ >= design_.Number(NameScale));
        dirty |= Animate(now);
        return host_.CaptureIfDirty(dirty);
    }
    bool SetHeading(float heading)
    {
        const float radians = heading * std::numbers::pi_v<float> / 180;
        // Native heading zero faces -Y. The authored arrow points up.
        const auto direction = view_->ProjectDirection({-std::cos(radians), std::sin(radians)});
        if (direction == headingDirection_)
            return false;
        hero_->SetProperty(Rml::PropertyId::Transform,
                           Rml::Transform::MakeProperty({Rml::Transforms::Rotate2D(
                               std::atan2(direction.x, -direction.y), Rml::Unit::RAD)}));
        headingDirection_ = direction;
        return true;
    }
    bool PositionPanel(int physicalWidth, int physicalHeight, float small)
    {
        const auto viewport = host_.Viewport();
        if (width_ == viewport.width && height_ == viewport.height && small_ == small)
            return false;
        const auto size = design_.Values(Size);
        const float stageWidth = physicalWidth / host_.ConfiguredScale(),
                    stageHeight = physicalHeight / host_.ConfiguredScale();
        const float sourceWidth = small == 1 ? stageWidth : std::floor(stageWidth / small);
        const float sourceHeight = small == 1 ? stageHeight : std::floor(stageHeight / small);
        const float x = std::floor(sourceWidth / 2),
                    y = std::floor(sourceHeight / 2) - design_.Number(CenterY);
        Position(*view_, x - size[0] / 2, y - size[1] / 2);
        Position(*hero_, x, y);
        Position(*help_, stageWidth - help_->GetProperty(Rml::PropertyId::Width)->Get<float>(),
                 stageHeight - help_->GetProperty(Rml::PropertyId::Height)->Get<float>());
        width_ = viewport.width;
        height_ = viewport.height;
        small_ = small;
        return true;
    }
    bool Animate(std::chrono::steady_clock::time_point now)
    {
        const float elapsed = std::chrono::duration<float>(now - opened_).count();
        const int frame =
            static_cast<int>(elapsed * design_.Number(FrameRate)) % design_.Number<int>(HeroFrames);
        bool dirty = false;
        if (frame != frame_)
        {
            hero_->SetClassNames("hero-frame-" + std::to_string(frame));
            frame_ = frame;
            dirty = true;
        }
        const float progress = std::clamp(
            (elapsed - design_.Number(HelpDelay) / 1000) / design_.Number(HelpFade), 0.F, 1.F);
        const auto curve = design_.Values(HelpCurve);
        const float opacity =
            progress == 1 ? 0
                          : std::clamp(1 - curve[0] * (1 - std::pow(curve[1], curve[2] * progress)),
                                       0.F, 1.F);
        if (helpOpacity_ != opacity)
        {
            help_->SetProperty(Rml::PropertyId::Opacity, Rml::Property(opacity, Rml::Unit::NUMBER));
            helpOpacity_ = opacity;
            dirty = true;
        }
        return dirty;
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    Rml::ElementInstancerGeneric<RmlHudMapViewport> instancer_;
    RmlDocumentHost host_;
    RmlHudMapViewport *view_ = nullptr;
    Rml::Element *hero_ = nullptr, *help_ = nullptr;
    std::chrono::steady_clock::time_point opened_{};
    std::uint64_t markerRevision_ = 0;
    int width_ = 0, height_ = 0, frame_ = -1;
    float zoom_, alpha_, small_ = 0, helpOpacity_ = -1;
    Rml::Vector2f headingDirection_;
    bool visible_ = false;
};
RmlMiniMapPanel::RmlMiniMapPanel(SessionKeeper &keeper) : impl_(std::make_unique<Impl>(keeper))
{
}
RmlMiniMapPanel::~RmlMiniMapPanel() = default;
void RmlMiniMapPanel::Release()
{
    impl_->Release();
}
bool RmlMiniMapPanel::PrepareOnWorker(int width, int height, bool visible,
                                      const LogicalRenderAssetMetadata *image, float heroX,
                                      float heroY, float heroHeading,
                                      std::span<const WorldMinimapData::Marker> markers,
                                      std::uint64_t revision,
                                      const std::array<int, 2> &imageOriginPixels)
{
    return impl_->Prepare(width, height, visible, image, heroX, heroY, heroHeading, markers,
                          revision, imageOriginPixels);
}
void RmlMiniMapPanel::AdjustZoom(int direction)
{
    impl_->zoom_ =
        std::clamp(impl_->zoom_ + direction * impl_->design_.Number(Impl::ScaleStep),
                   impl_->design_.Number(Impl::ScaleMin), impl_->design_.Number(Impl::ScaleMax));
}
void RmlMiniMapPanel::AdjustOpacity(int direction)
{
    impl_->alpha_ =
        std::clamp(impl_->alpha_ + direction * impl_->design_.Number(Impl::AlphaStep),
                   impl_->design_.Number(Impl::AlphaMin), impl_->design_.Number(Impl::AlphaMax));
}
bool RmlMiniMapPanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->view_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern

namespace UI::Modern
{
namespace
{
enum class MoveCommandLayerDesignKey
{
    RootY,
    ReferenceWidth,
    ReferenceHeight
};

const RmlUiDesign &MoveCommandLayerDesign()
{
    static const RmlUiDesign design(
        "Data/UI/PC/HUD/move_command.rml",
        {"MoveCommand-RootY", "MoveCommand-ReferenceWidth", "MoveCommand-ReferenceHeight"});
    return design;
}

template <std::size_t Size>
void SetText(Rml::Element &element, const std::array<wchar_t, Size> &value)
{
    element.SetInnerRML(Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(value.data())));
}

RmlMuCheckBoxListRowState CommonRowState(const RmlMoveCommandRow &row)
{
    return {
        {row.strife.data(), row.mapName.data(), row.requiredLevel.data(), row.requiredZen.data()},
        row.visible,
        row.disabled,
        row.over,
        row.down,
        row.selected};
}
} // namespace

RmlMoveCommandTransform CalculateRmlMoveCommandTransform(float maximumScale) noexcept
{
    return {0.0F, MoveCommandLayerDesign().Number(MoveCommandLayerDesignKey::RootY) * maximumScale,
            maximumScale};
}

RmlMoveCommandRect CalculateRmlMoveCommandReferenceRect(const RmlMoveCommandTransform &transform,
                                                        int viewportWidth, int viewportHeight,
                                                        float x, float y, float width,
                                                        float height) noexcept
{
    const float referenceScaleX =
        MoveCommandLayerDesign().Number(MoveCommandLayerDesignKey::ReferenceWidth) / viewportWidth;
    const float referenceScaleY =
        MoveCommandLayerDesign().Number(MoveCommandLayerDesignKey::ReferenceHeight) /
        viewportHeight;
    return {(transform.left + x * transform.scale) * referenceScaleX,
            (transform.top + y * transform.scale) * referenceScaleY,
            width * transform.scale * referenceScaleX, height * transform.scale * referenceScaleY};
}

class RmlMoveCommandLayer::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : host_(keeper, "move-command-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "HUD", "move_command.rml"))
    {
    }

    ~Impl()
    {
        ReleaseBindings();
    }

    void ReleaseBindings()
    {
        for (auto &row : rows_)
            row.Unbind();
        for (auto &row : favorites_)
            row.Unbind();
        scrollBar_.Unbind();
    }

    void Stage(const RmlMoveCommandRequest &request) noexcept
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
        if (!created_ && !pending_.visible)
        {
            active_ = pending_;
            return true;
        }
        if (!EnsureDocument(viewportWidth, viewportHeight))
        {
            return false;
        }

        if (!host_.SetVisible(pending_.visible))
            return false;
        const bool viewportChanged =
            viewportWidth_ != viewportWidth || viewportHeight_ != viewportHeight;
        const bool dirty = viewportChanged || active_ != pending_;
        viewportWidth_ = viewportWidth;
        viewportHeight_ = viewportHeight;
        if (dirty)
        {
            const bool initialize = !layoutApplied_;
            if (initialize)
            {
                ApplyStaticLayout();
                layoutApplied_ = true;
            }
            Apply(pending_, initialize);
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
        frame_ = document->GetElementById("move-command");
        outer_ = document->GetElementById("move-command-outer");
        mainPanel_ = document->GetElementById("move-command-main-panel");
        titleDivider_ = document->GetElementById("move-command-title-divider");
        buttonDivider_ = document->GetElementById("move-command-button-divider");
        favoritePanel_ = document->GetElementById("move-command-favorite-panel");
        favoriteMark_ = document->GetElementById("move-command-favorite-mark");
        inner_ = document->GetElementById("move-command-inner");
        title_ = document->GetElementById("move-command-title");
        strifeLabel_ = document->GetElementById("move-command-strife-label");
        mapLabel_ = document->GetElementById("move-command-map-label");
        levelLabel_ = document->GetElementById("move-command-level-label");
        zenLabel_ = document->GetElementById("move-command-zen-label");
        favoriteLabel_ = document->GetElementById("move-command-favorite-label");
        favoriteTitle_ = document->GetElementById("move-command-favorite-title");
        mainList_ = document->GetElementById("move-command-main-list");
        favoriteList_ = document->GetElementById("move-command-favorite-list");
        showMap_ = document->GetElementById("move-command-show-map");
        showMapLabel_ = document->GetElementById("move-command-show-map-label");
        close_ = document->GetElementById("move-command-close");
        closeLabel_ = document->GetElementById("move-command-close-label");

        const std::array<Rml::Element *, 21> required{
            frame_,         outer_,        mainPanel_, titleDivider_,  buttonDivider_,
            favoritePanel_, favoriteMark_, inner_,     title_,         strifeLabel_,
            mapLabel_,      levelLabel_,   zenLabel_,  favoriteLabel_, favoriteTitle_,
            mainList_,      favoriteList_, showMap_,   showMapLabel_,  close_,
            closeLabel_};
        for (Rml::Element *element : required)
        {
            if (element == nullptr)
            {
                ReleaseBindings();
                host_.Release();
                return false;
            }
        }
        if (!scrollBar_.Bind(*document, "move-command-scroll"))
        {
            ReleaseBindings();
            host_.Release();
            return false;
        }

        for (std::size_t index = 0; index < rows_.size(); ++index)
        {
            const std::string id = "move-command-main-row-" + std::to_string(index);
            if (!rows_[index].Bind(*document, id))
            {
                ReleaseBindings();
                host_.Release();
                return false;
            }
        }
        for (std::size_t index = 0; index < favorites_.size(); ++index)
        {
            const std::string id = "move-command-favorite-row-" + std::to_string(index);
            if (!favorites_[index].Bind(*document, id))
            {
                ReleaseBindings();
                host_.Release();
                return false;
            }
        }
        created_ = true;
        return true;
    }

    void ApplyStaticLayout()
    {
        const auto transform = CalculateRmlMoveCommandTransform(host_.ConfiguredScale());
        SetPixels(*frame_, "left", transform.left / host_.Viewport().scale);
        SetPixels(*frame_, "top", transform.top / host_.Viewport().scale);
        frame_->SetProperty(
            "transform",
            Rml::CreateString("scale(%.6f)", transform.scale / host_.Viewport().scale));
    }

    void Apply(const RmlMoveCommandRequest &request, bool initialize)
    {
        if (initialize || request.visible != active_.visible)
        {
            frame_->SetProperty("display", request.visible ? "block" : "none");
        }

        ApplyTextIfChanged(*title_, request.title, active_.title, initialize);
        ApplyTextIfChanged(*strifeLabel_, request.strifeLabel, active_.strifeLabel, initialize);
        ApplyTextIfChanged(*mapLabel_, request.mapLabel, active_.mapLabel, initialize);
        ApplyTextIfChanged(*levelLabel_, request.levelLabel, active_.levelLabel, initialize);
        ApplyTextIfChanged(*zenLabel_, request.zenLabel, active_.zenLabel, initialize);
        ApplyTextIfChanged(*favoriteLabel_, request.favoriteLabel, active_.favoriteLabel,
                           initialize);
        ApplyTextIfChanged(*favoriteTitle_, request.favoriteLabel, active_.favoriteLabel,
                           initialize);

        for (std::size_t index = 0; index < rows_.size(); ++index)
        {
            if (initialize || request.rows[index] != active_.rows[index])
            {
                const RmlMuCheckBoxListRowState current = CommonRowState(request.rows[index]);
                const RmlMuCheckBoxListRowState previous = CommonRowState(active_.rows[index]);
                rows_[index].Apply(current, initialize ? nullptr : &previous);
            }
        }
        for (std::size_t index = 0; index < favorites_.size(); ++index)
        {
            if (initialize || request.favorites[index] != active_.favorites[index])
            {
                const RmlMuCheckBoxListRowState current = CommonRowState(request.favorites[index]);
                const RmlMuCheckBoxListRowState previous = CommonRowState(active_.favorites[index]);
                favorites_[index].Apply(current, initialize ? nullptr : &previous);
            }
        }

        if (initialize || request.scrollBar != active_.scrollBar ||
            request.visible != active_.visible)
        {
            scrollBar_.Apply(request.scrollBar);
        }

        ApplyButtonChanges(*showMap_, *showMapLabel_, request.showMapLabel, active_.showMapLabel,
                           request.showMap, active_.showMap, initialize);
        ApplyButtonChanges(*close_, *closeLabel_, request.closeLabel, active_.closeLabel,
                           request.close, active_.close, initialize);
    }

    template <std::size_t Size>
    void ApplyTextIfChanged(Rml::Element &element, const std::array<wchar_t, Size> &value,
                            const std::array<wchar_t, Size> &previous, bool initialize)
    {
        if (initialize || value != previous)
        {
            SetText(element, value);
        }
    }

    template <std::size_t Size>
    void ApplyButtonChanges(Rml::Element &button, Rml::Element &label,
                            const std::array<wchar_t, Size> &text,
                            const std::array<wchar_t, Size> &previousText, ButtonVisualState state,
                            ButtonVisualState previousState, bool initialize)
    {
        if (initialize || text != previousText)
        {
            SetText(label, text);
        }
        if (initialize || state != previousState)
        {
            ApplyRmlMuButtonVisualState(button, state);
        }
    }

    RmlDocumentHost host_;
    Rml::Element *frame_ = nullptr;
    Rml::Element *outer_ = nullptr;
    Rml::Element *mainPanel_ = nullptr;
    Rml::Element *titleDivider_ = nullptr;
    Rml::Element *buttonDivider_ = nullptr;
    Rml::Element *favoritePanel_ = nullptr;
    Rml::Element *favoriteMark_ = nullptr;
    Rml::Element *inner_ = nullptr;
    Rml::Element *title_ = nullptr;
    Rml::Element *strifeLabel_ = nullptr;
    Rml::Element *mapLabel_ = nullptr;
    Rml::Element *levelLabel_ = nullptr;
    Rml::Element *zenLabel_ = nullptr;
    Rml::Element *favoriteLabel_ = nullptr;
    Rml::Element *favoriteTitle_ = nullptr;
    Rml::Element *mainList_ = nullptr;
    Rml::Element *favoriteList_ = nullptr;
    Rml::Element *showMap_ = nullptr;
    Rml::Element *showMapLabel_ = nullptr;
    Rml::Element *close_ = nullptr;
    Rml::Element *closeLabel_ = nullptr;
    std::array<RmlMuCheckBoxListRow, RmlMoveCommandVisibleRows> rows_{};
    std::array<RmlMuCheckBoxListRow, RmlMoveCommandFavoriteRows> favorites_{};
    RmlMuScrollBar scrollBar_;
    RmlMoveCommandRequest active_{};
    RmlMoveCommandRequest pending_{};
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
    bool created_ = false;
    bool hasPending_ = false;
    bool layoutApplied_ = false;
};

RmlMoveCommandLayer::RmlMoveCommandLayer(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper))
{
}

RmlMoveCommandLayer::~RmlMoveCommandLayer() = default;

void RmlMoveCommandLayer::Stage(const RmlMoveCommandRequest &request) noexcept
{
    impl_->Stage(request);
}

bool RmlMoveCommandLayer::PrepareOnWorker(int viewportWidth, int viewportHeight)
{
    return impl_->Prepare(viewportWidth, viewportHeight);
}

bool RmlMoveCommandLayer::Record(LegacyRenderFacade &facade) const
{
    return impl_->Record(facade);
}
} // namespace UI::Modern

namespace UI::Modern
{
class RmlSkillListLayer::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : design_("Data/UI/PC/HUD/main_frame.rml",
                  {"Skill-Columns", "RmlMainFrameLayer-LegacyReferenceWidth",
                   "RmlMainFrameLayer-LegacyReferenceHeight"}),
          host_(keeper, "skill-list-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "HUD", "skill_list_icons.rml"))
    {
    }
    ~Impl()
    {
        Release();
    }
    void Release()
    {
        cells_.clear();
        root_ = nullptr;
        previous_.clear();
        viewport_ = {};
        host_.Release();
    }
    struct Cell
    {
        RmlSkillIcon icon;
        RmlMuProgressBar cooldown;
        Rml::Element *element = nullptr;
    };
    void Resize(std::size_t count)
    {
        cells_.clear();
        root_->SetInnerRML("");
        for (std::size_t i = 0; i < count; ++i)
        {
            auto cell = std::make_unique<Cell>();
            auto element = host_.Document()->CreateElement("div");
            element->SetId("skill-list-icon-" + std::to_string(i));
            element->SetClassNames("mu-skill-icon");
            element->SetInnerRML("<div class=\"skill-sheet\"/><div class=\"skill-cooldown\"/>");
            cell->element = root_->AppendChild(std::move(element));
            cell->icon.Bind(*cell->element, design_.Number<int>(0));
            cell->cooldown.Bind(*cell->element->GetChild(1), RmlMuProgressBar::Axis::Vertical);
            cells_.push_back(std::move(cell));
        }
    }
    static void Pixels(Rml::Element &element, Rml::PropertyId property, float value)
    {
        element.SetProperty(property, Rml::Property(value, Rml::Unit::PX));
    }
    void Apply(const std::vector<Entry> &entries, Rml::Vector2i viewport)
    {
        const bool resized = cells_.size() != entries.size();
        if (resized)
            Resize(entries.size());
        const float scaleX = viewport.x / design_.Number(1),
                    scaleY = viewport.y / design_.Number(2);
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const auto &entry = entries[i];
            auto &cell = *cells_[i];
            if (resized || viewport_ != viewport || previous_[i].x != entry.x ||
                previous_[i].y != entry.y || previous_[i].width != entry.width ||
                previous_[i].height != entry.height)
            {
                Pixels(*cell.element, Rml::PropertyId::Left, entry.x * scaleX);
                Pixels(*cell.element, Rml::PropertyId::Top, entry.y * scaleY);
                Pixels(*cell.element, Rml::PropertyId::Width, entry.width * scaleX);
                Pixels(*cell.element, Rml::PropertyId::Height, entry.height * scaleY);
            }
            cell.icon.Set(entry.icon);
            cell.cooldown.SetProgress(entry.cooldown, 1);
        }
        previous_ = entries;
        viewport_ = viewport;
    }
    bool Prepare(int width, int height, bool visible, const std::vector<Entry> &entries)
    {
        if (!root_ && !visible)
            return true;
        if (!host_.Ensure(width, height))
            return false;
        if (!root_)
            root_ = host_.Document()->GetElementById("skill-list-icons");
        if (!root_)
        {
            Release();
            return false;
        }
        if (!host_.SetVisible(visible))
            return false;
        const Rml::Vector2i viewport(host_.Viewport().width, host_.Viewport().height);
        const bool dirty = previous_ != entries || viewport_ != viewport;
        if (dirty)
            Apply(entries, viewport);
        return host_.CaptureIfDirty(dirty);
    }
    RmlUiDesign design_;
    RmlDocumentHost host_;
    Rml::Element *root_ = nullptr;
    std::vector<std::unique_ptr<Cell>> cells_;
    std::vector<Entry> previous_;
    Rml::Vector2i viewport_;
};
RmlSkillListLayer::RmlSkillListLayer(SessionKeeper &keeper) : impl_(std::make_unique<Impl>(keeper))
{
}
RmlSkillListLayer::~RmlSkillListLayer() = default;
void RmlSkillListLayer::Release()
{
    impl_->Release();
}
bool RmlSkillListLayer::PrepareOnWorker(int width, int height, bool visible,
                                        const std::vector<Entry> &entries)
{
    return impl_->Prepare(width, height, visible, entries);
}
bool RmlSkillListLayer::Record(LegacyRenderFacade &facade) const
{
    return !impl_->root_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern

namespace UI::Modern
{
namespace
{
enum class TopMenuLayerDesignKey
{
    ResizeStageHeight,
    SmallStageScale,
    ReferenceWidth,
    ReferenceHeight
};

const RmlUiDesign &TopMenuLayerDesign()
{
    static const RmlUiDesign design("Data/UI/PC/HUD/top_menu.rml",
                                    {"TopMenu-ResizeStageHeight", "TopMenu-SmallStageScale",
                                     "TopMenu-ReferenceWidth", "TopMenu-ReferenceHeight"});
    return design;
}

bool SameRequest(const RmlTopMenuRequest &left, const RmlTopMenuRequest &right) noexcept
{
    return left.positionX == right.positionX && left.positionY == right.positionY &&
           left.visible == right.visible && left.helperActive == right.helperActive &&
           left.optionButton == right.optionButton && left.actionButton == right.actionButton &&
           std::wcscmp(left.mapName, right.mapName) == 0;
}

} // namespace

RmlTopMenuTransform CalculateRmlTopMenuTransform(int viewportWidth, int viewportHeight,
                                                 float maximumScale) noexcept
{
    (void)viewportWidth;
    const float stageHeight = viewportHeight / maximumScale;
    const float stageScale =
        stageHeight < TopMenuLayerDesign().Number(TopMenuLayerDesignKey::ResizeStageHeight)
            ? TopMenuLayerDesign().Number(TopMenuLayerDesignKey::SmallStageScale)
            : 1.0F;
    return {0.0F, 0.0F, maximumScale * stageScale};
}

RmlTopMenuRect CalculateRmlTopMenuReferenceRect(const RmlTopMenuTransform &transform,
                                                int viewportWidth, int viewportHeight, float x,
                                                float y, float width, float height) noexcept
{
    const float referenceScaleX =
        TopMenuLayerDesign().Number(TopMenuLayerDesignKey::ReferenceWidth) / viewportWidth;
    const float referenceScaleY =
        TopMenuLayerDesign().Number(TopMenuLayerDesignKey::ReferenceHeight) / viewportHeight;
    return {(transform.left + x * transform.scale) * referenceScaleX,
            (transform.top + y * transform.scale) * referenceScaleY,
            width * transform.scale * referenceScaleX, height * transform.scale * referenceScaleY};
}

class RmlTopMenuLayer::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : host_(keeper, "top-menu-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "HUD", "top_menu.rml"))
    {
    }

    void Stage(const RmlTopMenuRequest &request) noexcept
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

        if (!host_.SetVisible(pending_.visible))
            return false;
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
            if (layoutChanged)
            {
                ApplyLayout();
            }
            ApplyContent(pending_, layoutChanged);
        }
        active_ = pending_;
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
        frame_ = document->GetElementById("top-menu");
        wingBack_ = document->GetElementById("top-menu-wing-back");
        wingControls_ = document->GetElementById("top-menu-wing-controls");
        mapFrame_ = document->GetElementById("top-menu-map-frame");
        mapName_ = document->GetElementById("top-menu-map-name");
        position_ = document->GetElementById("top-menu-position");
        option_ = document->GetElementById("top-menu-option");
        action_ = document->GetElementById("top-menu-action");

        const std::array<Rml::Element *, 8> required{frame_,   wingBack_, wingControls_, mapFrame_,
                                                     mapName_, position_, option_,       action_};
        if (std::find(required.begin(), required.end(), nullptr) != required.end())
        {
            host_.Release();
            return false;
        }
        created_ = true;
        return true;
    }

    void ApplyLayout()
    {
        const auto transform =
            CalculateRmlTopMenuTransform(viewportWidth_, viewportHeight_, host_.ConfiguredScale());
        SetPixels(*frame_, "left", transform.left / host_.Viewport().scale);
        SetPixels(*frame_, "top", transform.top / host_.Viewport().scale);
        frame_->SetProperty(
            "transform",
            Rml::CreateString("scale(%.6f)", transform.scale / host_.Viewport().scale));
    }

    void ApplyContent(const RmlTopMenuRequest &request, bool initialize)
    {
        if (initialize || request.visible != active_.visible)
        {
            frame_->SetProperty("display", request.visible ? "block" : "none");
        }
        if (initialize || request.visible != active_.visible ||
            std::wcscmp(request.mapName, active_.mapName) != 0)
        {
            SetFittedLabelText(*mapName_, StringUtils::WideToNarrow(request.mapName));
        }
        if (initialize || request.visible != active_.visible ||
            request.positionX != active_.positionX || request.positionY != active_.positionY)
        {
            SetFittedLabelText(*position_, std::to_string(request.positionX) + "," +
                                               std::to_string(request.positionY));
        }
        if (initialize || request.optionButton != active_.optionButton)
        {
            ApplyRmlMuButtonVisualState(*option_, request.optionButton);
        }
        if (initialize || request.helperActive != active_.helperActive)
        {
            action_->SetClass("start", !request.helperActive);
            action_->SetClass("stop", request.helperActive);
        }
        if (initialize || request.actionButton != active_.actionButton)
        {
            ApplyRmlMuButtonVisualState(*action_, request.actionButton);
        }
    }

    RmlDocumentHost host_;
    Rml::Element *frame_ = nullptr;
    Rml::Element *wingBack_ = nullptr;
    Rml::Element *wingControls_ = nullptr;
    Rml::Element *mapFrame_ = nullptr;
    Rml::Element *mapName_ = nullptr;
    Rml::Element *position_ = nullptr;
    Rml::Element *option_ = nullptr;
    Rml::Element *action_ = nullptr;
    RmlTopMenuRequest active_{};
    RmlTopMenuRequest pending_{};
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
    float configuredScale_ = 0.0F;
    bool created_ = false;
    bool hasPending_ = false;
};

RmlTopMenuLayer::RmlTopMenuLayer(SessionKeeper &keeper) : impl_(std::make_unique<Impl>(keeper))
{
}

RmlTopMenuLayer::~RmlTopMenuLayer() = default;

void RmlTopMenuLayer::Stage(const RmlTopMenuRequest &request) noexcept
{
    impl_->Stage(request);
}

bool RmlTopMenuLayer::PrepareOnWorker(int viewportWidth, int viewportHeight)
{
    return impl_->Prepare(viewportWidth, viewportHeight);
}

bool RmlTopMenuLayer::Record(LegacyRenderFacade &facade) const
{
    return impl_->Record(facade);
}
} // namespace UI::Modern

namespace UI::Modern::PC::MuHelper
{
namespace
{
enum class MuHelperPanelDesignKey
{
    Width,
    Height,
    SidePanelWidth,
    SkillColumns
};
const RmlUiDesign &MuHelperPanelDesign()
{
    static const RmlUiDesign design(
        "Data/UI/PC/MuHelper/mu_helper.rml",
        {"MuHelper-Width", "MuHelper-Height", "MuHelper-SidePanelWidth", "Skill-Columns"});
    return design;
}
float CompositeWidth() noexcept
{
    return RmlMuHelperPanel::Width() +
           MuHelperPanelDesign().Number(MuHelperPanelDesignKey::SidePanelWidth);
}

enum class ButtonId : std::size_t
{
    Close,
    Reset,
    Save,
    TabHunt,
    TabItem,
    TabOther,
    PotionSettings,
    PartySettings,
    Skill1Settings,
    Skill2Settings,
    AddItem,
    DeleteItem,
    HuntRange1,
    HuntRange2,
    HuntRange3,
    HuntRange4,
    HuntRange5,
    HuntRange6,
    HuntRange7,
    HuntRange8,
    ObtainRange1,
    ObtainRange2,
    ObtainRange3,
    ObtainRange4,
    ObtainRange5,
    ObtainRange6,
    ObtainRange7,
    ObtainRange8,
    SubClose,
    SubReset,
    SubSave,
    ItemRow0,
    ItemRow1,
    ItemRow2,
    ItemRow3,
    ItemRow4,
    Count,
};

constexpr std::size_t ItemRowCount =
    static_cast<std::size_t>(ButtonId::ItemRow4) - static_cast<std::size_t>(ButtonId::ItemRow0) + 1;

enum class CheckId : std::size_t
{
    FallbackBasicAttack,
    ConcentratedMonsters,
    UseSkillsClosely,
    UsePotion,
    LongRangeCounter,
    ReturnPosition,
    Combo,
    BuffDuration,
    UseDarkRaven,
    SupportParty,
    AutoHeal,
    DrainLife,
    RepairItem,
    PickAll,
    PickSelected,
    PickJewel,
    PickAncient,
    PickZen,
    PickExcellent,
    PickExtra,
    AutoAcceptFriend,
    AutoAcceptGuild,
    SelfDefense,
    PartyHeal,
    PartyBuffDuration,
    SubTimer,
    SubMonsterCondition,
    Count,
};

enum class InputId : std::size_t
{
    ReturnSeconds,
    ManualControlYieldSeconds,
    ItemName,
    PartyBuffInterval,
    SubSkillInterval,
    Count,
};

constexpr std::array<const char *, static_cast<std::size_t>(ButtonId::Count)> ButtonElementIds{
    "helper-close",   "helper-reset",    "helper-save",    "tab-hunt",         "tab-item",
    "tab-other",      "potion-settings", "party-settings", "skill-1-settings", "skill-2-settings",
    "item-add",       "item-delete",     "hunt-range-1",   "hunt-range-2",     "hunt-range-3",
    "hunt-range-4",   "hunt-range-5",    "hunt-range-6",   "hunt-range-7",     "hunt-range-8",
    "obtain-range-1", "obtain-range-2",  "obtain-range-3", "obtain-range-4",   "obtain-range-5",
    "obtain-range-6", "obtain-range-7",  "obtain-range-8", "sub-close",        "sub-reset",
    "sub-save",       "item-row-0",      "item-row-1",     "item-row-2",       "item-row-3",
    "item-row-4",
};

constexpr std::array<const char *, static_cast<std::size_t>(CheckId::Count)> CheckElementIds{
    "fallback-basic",
    "concentrated-monsters",
    "skills-closely",
    "use-potion",
    "long-counter",
    "return-position",
    "combo",
    "buff-duration",
    "use-dark-raven",
    "support-party",
    "auto-heal",
    "drain-life",
    "repair-item",
    "pick-all",
    "pick-selected",
    "pick-jewel",
    "pick-ancient",
    "pick-zen",
    "pick-excellent",
    "pick-extra",
    "accept-friend",
    "accept-guild",
    "self-defense",
    "party-heal",
    "party-buff-duration",
    "sub-skill-timer",
    "sub-monster-condition",
};

constexpr std::array<const char *, static_cast<std::size_t>(InputId::Count)> InputElementIds{
    "return-seconds",
    "manual-yield-seconds",
    "item-name",
    "party-buff-interval",
    "sub-skill-interval",
};

constexpr std::array<const char *, static_cast<std::size_t>(RmlMuHelperTextId::Count)>
    TextElementIds{
        "text-title",
        "text-hunting",
        "text-obtaining",
        "text-other-settings",
        "text-range",
        "text-regular-attack",
        "text-potion",
        "text-long-counter",
        "text-return-position",
        "text-seconds",
        "text-basic-skill",
        "text-activation-1",
        "text-activation-2",
        "text-delay",
        "text-condition",
        "text-setting",
        "text-combo",
        "text-dark-spirits",
        "text-auto-attack",
        "text-cease-attack",
        "text-attack-together",
        "text-party",
        "text-auto-heal",
        "text-drain-life",
        "text-buff-duration",
        "text-repair-item",
        "text-pick-all",
        "text-pick-selected",
        "text-jewel",
        "text-set-item",
        "text-zen",
        "text-excellent",
        "text-extra-item",
        "text-add",
        "text-delete",
        "text-accept-friend",
        "text-accept-guild",
        "text-self-defense",
        "text-manual-yield",
        "text-initialization",
        "text-save",
        "text-auto-recovery",
        "text-auto-potion",
        "text-hp-status",
        "text-activation-skill",
        "text-pre-condition",
        "text-monster-range",
        "text-monster-attacking",
        "text-sub-condition",
        "text-more-two",
        "text-more-three",
        "text-more-four",
        "text-more-five",
        "text-party-heal",
        "text-party-hp",
        "text-buff-support",
        "text-party-buff-duration",
        "text-buff-interval",
        "text-close",
        "copy-range-item",
        "copy-save-sub",
        "copy-condition-sub",
        "copy-seconds-sub",
        "text-concentrated-monsters",
        "text-skills-closely",
    };

constexpr std::array<const char *, 14> TextCopyElementIds{
    "copy-range-item",      "copy-condition-2",        "copy-setting-party", "copy-setting-skill-1",
    "copy-setting-skill-2", "copy-initialization-sub", "copy-save-sub",      "copy-hp-status-heal",
    "copy-party-title",     "copy-condition-sub",      "copy-auto-heal-sub", "copy-drain-life-sub",
    "copy-seconds-sub",    "copy-seconds-manual",
};

constexpr std::array<RmlMuHelperTextId, TextCopyElementIds.size()> TextCopySources{
    RmlMuHelperTextId::LootRange,     RmlMuHelperTextId::Condition,
    RmlMuHelperTextId::Setting,       RmlMuHelperTextId::Setting,
    RmlMuHelperTextId::Setting,       RmlMuHelperTextId::Initialization,
    RmlMuHelperTextId::SaveSetup,     RmlMuHelperTextId::HpStatus,
    RmlMuHelperTextId::Party,         RmlMuHelperTextId::MonsterCondition,
    RmlMuHelperTextId::AutoHeal,      RmlMuHelperTextId::DrainLife,
    RmlMuHelperTextId::SkillInterval, RmlMuHelperTextId::Seconds,
};

std::size_t ToIndex(ButtonId id) noexcept
{
    return static_cast<std::size_t>(id);
}

std::size_t ToIndex(CheckId id) noexcept
{
    return static_cast<std::size_t>(id);
}

std::size_t ToIndex(InputId id) noexcept
{
    return static_cast<std::size_t>(id);
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

bool IsChecked(const Rml::Element &element)
{
    return element.HasAttribute("checked");
}

bool SetChecked(Rml::Element &element, bool checked)
{
    if (IsChecked(element) == checked)
        return false;
    if (checked)
        element.SetAttribute("checked", "");
    else
        element.RemoveAttribute("checked");
    return true;
}

bool SetDisplay(Rml::Element &element, bool visible)
{
    element.SetProperty("display", visible ? "block" : "none");
    return true;
}

bool SetText(Rml::Element &element, std::wstring &current, const std::wstring &next)
{
    if (current == next)
        return false;
    current = next;
    element.SetInnerRML(Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(next.c_str())));
    return true;
}

int ParseNumber(const Rml::ElementFormControlInput &input)
{
    const Rml::String &text = input.GetValue();
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} ? value : 0;
}

template <std::size_t Size>
int CheckedIndex(const std::array<Rml::ElementFormControlInput *, Size> &controls, int fallback)
{
    for (std::size_t index = 0; index < controls.size(); ++index)
    {
        if (IsChecked(*controls[index]))
            return static_cast<int>(index);
    }
    return fallback;
}

template <std::size_t Size>
bool SyncRadio(std::array<Rml::ElementFormControlInput *, Size> &controls, int selection)
{
    bool dirty = false;
    for (std::size_t index = 0; index < controls.size(); ++index)
    {
        dirty = SetChecked(*controls[index], static_cast<int>(index) == selection) || dirty;
    }
    return dirty;
}
} // namespace

float RmlMuHelperPanel::Width() noexcept
{
    return MuHelperPanelDesign().Number(MuHelperPanelDesignKey::Width);
}
float RmlMuHelperPanel::Height() noexcept
{
    return MuHelperPanelDesign().Number(MuHelperPanelDesignKey::Height);
}

class RmlMuHelperPanel::Impl final
{
  public:
    Impl(SessionKeeper &keeper, SessionBoundArray<RmlMuButton, ButtonCount> &buttons,
         SessionBoundArray<RmlMuSlot, RmlMuHelperAssignedSkillCount> &assignedSlots,
         SessionBoundArray<RmlMuSlot, RmlMuHelperAvailableSkillCount> &availableSlots)
        : buttons_(buttons), assignedSlots_(assignedSlots), availableSlots_(availableSlots),
          host_(keeper, "mu-helper-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "MuHelper", "mu_helper.rml"))
    {
    }

    ~Impl()
    {
        Release();
    }

    void Release()
    {
        movable_.Unbind();
        itemScroll_.Unbind();
        for (RmlMuButton &button : buttons_)
            button.Unbind();
        for (auto &icon : assignedIcons_)
            icon.Unbind();
        for (auto &icon : availableIcons_)
            icon.Unbind();
        for (RmlMuSlot &slot : assignedSlots_)
            slot.Unbind();
        for (RmlMuSlot &slot : availableSlots_)
            slot.Unbind();
        ClearPointers();
        currentContent_ = {};
        formValues_.reset();
        changes_ = {};
        viewportWidth_ = 0;
        viewportHeight_ = 0;
        scaleX_ = scaleY_ = 1.0F;
        positionSet_ = false;
        visible_ = false;
        publishedOwnsPointer_.store(false, std::memory_order_release);
        host_.Release();
        publishedVisible_.store(false, std::memory_order_release);
    }

    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!publishedVisible_.load(std::memory_order_acquire))
            return false;
        const bool wasDragging = movable_.IsDragging();
        const bool processed = host_.ProcessInput(event);
        if (event.action != SessionInputAction::PointerMove)
        {
            ReadFormChanges();
            ReadButtonChanges();
            ReadItemScroll();
        }
        inputDirty_ = movable_.TakeDirty() || inputDirty_;
        if (event.kind != SessionInputEventKind::Pointer)
            return processed;
        const bool ownsPointer =
            wasDragging || movable_.IsDragging() || IsDescendantOf(host_.HoverElement(), panel_);
        publishedOwnsPointer_.store(ownsPointer, std::memory_order_release);
        return ownsPointer;
    }

    RmlMuHelperChanges TakeChanges()
    {
        RmlMuHelperChanges result = std::move(changes_);
        changes_ = {};
        return result;
    }

    bool Prepare(int viewportWidth, int viewportHeight, bool visible,
                 const RmlMuHelperContent &content)
    {
        if (!visible)
            publishedOwnsPointer_.store(false, std::memory_order_release);
        if (panel_ == nullptr && !visible)
        {
            visible_ = false;
            publishedVisible_.store(false, std::memory_order_release);
            return true;
        }
        if (!EnsureDocument(viewportWidth, viewportHeight))
            return false;

        if (!host_.SetVisible(visible))
            return false;
        bool dirty = ApplyViewport(viewportWidth, viewportHeight);
        dirty = ApplyContent(content) || dirty;
        if (visible && !visible_)
            ApplyItemScroll(content.extraItems.size());
        dirty = SyncWidgets() || dirty;
        dirty = inputDirty_ || movable_.TakeDirty() || dirty;
        dirty = visible != visible_ || dirty;
        if (!visible && visible_)
            movable_.CancelDrag();
        visible_ = visible;
        inputDirty_ = false;
        const bool captured = host_.CaptureIfDirty(dirty);
        publishedVisible_.store(visible, std::memory_order_release);
        return captured;
    }

    bool Record(LegacyRenderFacade &facade) const
    {
        return panel_ == nullptr || host_.Record(facade);
    }

    std::optional<RmlTextInputArea> TextInputArea() const
    {
        return visible_ ? host_.FocusedTextInputArea() : std::nullopt;
    }

    bool HasTextInputFocus() const noexcept
    {
        return visible_ && std::any_of(inputs_.begin(), inputs_.end(), [](const auto *input) {
                   return input != nullptr && input->IsPseudoClassSet("focus");
               });
    }

    bool OwnsPointer() const noexcept
    {
        return publishedOwnsPointer_.load(std::memory_order_acquire);
    }

  private:
    bool EnsureDocument(int viewportWidth, int viewportHeight)
    {
        if (!host_.Ensure(viewportWidth, viewportHeight, CompositeWidth(), Height()))
            return false;
        if (panel_ != nullptr)
            return true;
        Rml::ElementDocument *const document = host_.Document();
        if (!BindPanels(*document) || !BindText(*document) || !BindButtons(*document) ||
            !BindForms(*document) || !BindSlots(*document))
        {
            Release();
            return false;
        }
        movable_.Bind(*panel_, *drag_);
        viewportWidth_ = 0;
        viewportHeight_ = 0;
        return true;
    }

    bool BindPanels(Rml::ElementDocument &document)
    {
        panel_ = document.GetElementById("mu-helper");
        drag_ = document.GetElementById("helper-drag");
        picker_ = document.GetElementById("skill-picker");
        subWindow_ = document.GetElementById("sub-window");
        itemList_ = document.GetElementById("item-list");
        constexpr std::array<const char *, 3> pageIds{"page-hunt", "page-item", "page-other"};
        constexpr std::array<const char *, 6> classIds{"class-activation-2", "class-combo",
                                                       "class-dark-raven",   "class-party",
                                                       "class-auto-heal",    "class-drain-life"};
        constexpr std::array<const char *, 6> subIds{"sub-recovery",       "sub-recovery-heal",
                                                     "sub-recovery-drain", "sub-skill",
                                                     "sub-party",          "sub-party-heal"};
        for (std::size_t i = 0; i < pages_.size(); ++i)
            pages_[i] = document.GetElementById(pageIds[i]);
        for (std::size_t i = 0; i < classPanels_.size(); ++i)
            classPanels_[i] = document.GetElementById(classIds[i]);
        for (std::size_t i = 0; i < subPanels_.size(); ++i)
            subPanels_[i] = document.GetElementById(subIds[i]);
        return panel_ != nullptr && drag_ != nullptr && picker_ != nullptr &&
               subWindow_ != nullptr && itemList_ != nullptr &&
               itemScroll_.Bind(document, "item-scroll") && AllPresent(pages_) &&
               AllPresent(classPanels_) && AllPresent(subPanels_);
    }

    bool BindText(Rml::ElementDocument &document)
    {
        for (std::size_t i = 0; i < textElements_.size(); ++i)
            textElements_[i] = document.GetElementById(TextElementIds[i]);
        for (std::size_t i = 0; i < textCopies_.size(); ++i)
            textCopies_[i] = document.GetElementById(TextCopyElementIds[i]);
        return AllPresent(textElements_) && AllPresent(textCopies_);
    }

    bool BindButtons(Rml::ElementDocument &document)
    {
        for (std::size_t i = 0; i < ButtonElementIds.size(); ++i)
        {
            Rml::Element *const element = document.GetElementById(ButtonElementIds[i]);
            if (element == nullptr)
                return false;
            buttonElements_[i] = element;
            buttons_[i].Bind(*element);
        }
        return true;
    }

    bool BindForms(Rml::ElementDocument &document)
    {
        for (std::size_t i = 0; i < CheckElementIds.size(); ++i)
        {
            checks_[i] = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
                document.GetElementById(CheckElementIds[i]));
        }
        for (std::size_t i = 0; i < InputElementIds.size(); ++i)
        {
            inputs_[i] = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
                document.GetElementById(InputElementIds[i]));
        }
        BindRadioGroup(document, "dark-mode-", darkMode_);
        BindRadioGroup(document, "skill-pre-", skillPre_);
        BindRadioGroup(document, "skill-mobs-", skillMobs_);
        BindRadioGroup(document, "potion-level-", potionLevels_, 1);
        BindRadioGroup(document, "heal-level-", healLevels_, 1);
        BindRadioGroup(document, "party-heal-level-", partyHealLevels_, 1);
        return AllPresent(checks_) && AllPresent(inputs_) && AllPresent(darkMode_) &&
               AllPresent(skillPre_) && AllPresent(skillMobs_) && AllPresent(potionLevels_) &&
               AllPresent(healLevels_) && AllPresent(partyHealLevels_);
    }

    bool BindSlots(Rml::ElementDocument &document)
    {
        for (std::size_t i = 0; i < RmlMuHelperAssignedSkillCount; ++i)
        {
            assignedSlotElements_[i] =
                document.GetElementById("assigned-skill-" + std::to_string(i));
            if (assignedSlotElements_[i] == nullptr)
                return false;
            assignedSlots_[i].Bind(*assignedSlotElements_[i]);
            if (!assignedSlotElements_[i]->GetFirstChild())
                return false;
            assignedIcons_[i].Bind(
                *assignedSlotElements_[i]->GetFirstChild(),
                MuHelperPanelDesign().Number<int>(MuHelperPanelDesignKey::SkillColumns));
        }
        for (std::size_t i = 0; i < RmlMuHelperAvailableSkillCount; ++i)
        {
            availableSlotElements_[i] =
                document.GetElementById("available-skill-" + std::to_string(i));
            if (availableSlotElements_[i] == nullptr)
                return false;
            availableSlots_[i].Bind(*availableSlotElements_[i]);
            if (!availableSlotElements_[i]->GetFirstChild())
                return false;
            availableIcons_[i].Bind(
                *availableSlotElements_[i]->GetFirstChild(),
                MuHelperPanelDesign().Number<int>(MuHelperPanelDesignKey::SkillColumns));
        }
        return true;
    }

    template <std::size_t Size>
    static bool AllPresent(const std::array<Rml::Element *, Size> &elements)
    {
        return std::find(elements.begin(), elements.end(), nullptr) == elements.end();
    }

    template <std::size_t Size>
    static bool AllPresent(const std::array<Rml::ElementFormControlInput *, Size> &elements)
    {
        return std::find(elements.begin(), elements.end(), nullptr) == elements.end();
    }

    template <std::size_t Size>
    static void BindRadioGroup(Rml::ElementDocument &document, const char *prefix,
                               std::array<Rml::ElementFormControlInput *, Size> &elements,
                               int first = 0)
    {
        for (std::size_t i = 0; i < elements.size(); ++i)
        {
            elements[i] = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
                document.GetElementById(prefix + std::to_string(i + first)));
        }
    }

    bool ApplyViewport(int physicalWidth, int physicalHeight)
    {
        const auto viewport = host_.Viewport();
        const float scaleX = static_cast<float>(physicalWidth) / viewport.width;
        const float scaleY = static_cast<float>(physicalHeight) / viewport.height;
        if (viewportWidth_ == viewport.width && viewportHeight_ == viewport.height &&
            scaleX_ == scaleX && scaleY_ == scaleY)
            return false;
        viewportWidth_ = viewport.width;
        viewportHeight_ = viewport.height;
        scaleX_ = scaleX;
        scaleY_ = scaleY;
        movable_.Configure(viewport.width, viewport.height, CompositeWidth(), Height(),
                           -MuHelperPanelDesign().Number(MuHelperPanelDesignKey::SidePanelWidth),
                           0.0F);
        if (!positionSet_)
        {
            movable_.SetPosition(std::max(0.0F, viewport.width - CompositeWidth()), 0.0F);
            positionSet_ = true;
        }
        ApplyItemScroll(currentContent_.extraItems.size());
        return true;
    }

    bool ApplyContent(const RmlMuHelperContent &content)
    {
        bool dirty = ApplyText(content);
        dirty = ApplyPage(content.tab) || dirty;
        dirty = ApplyClass(content.characterClass) || dirty;
        dirty = ApplySubPage(content.subPage) || dirty;
        dirty = ApplyRanges(content) || dirty;
        dirty = ApplySkills(content) || dirty;
        dirty = ApplyItems(content.extraItems) || dirty;
        if (!formValues_.has_value() || *formValues_ != content.form ||
            currentContent_.subPage != content.subPage)
        {
            formValues_ = content.form;
            dirty = SyncForm(content.form, content.subPage) || dirty;
        }
        currentContent_ = content;
        return dirty;
    }

    bool ApplyText(const RmlMuHelperContent &content)
    {
        if (currentContent_.text == content.text)
            return false;
        bool dirty = false;
        for (std::size_t i = 0; i < textElements_.size(); ++i)
        {
            dirty = SetText(*textElements_[i], currentContent_.text[i], content.text[i]) || dirty;
        }
        for (std::size_t i = 0; i < textCopies_.size(); ++i)
        {
            const std::size_t source = static_cast<std::size_t>(TextCopySources[i]);
            textCopies_[i]->SetInnerRML(Rml::StringUtilities::EncodeRml(
                StringUtils::WideToNarrow(content.text[source].c_str())));
        }
        return dirty;
    }

    bool ApplyPage(int tab)
    {
        if (currentContent_.tab == tab && contentApplied_)
            return false;
        bool dirty = false;
        for (std::size_t i = 0; i < pages_.size(); ++i)
        {
            const bool selected = static_cast<int>(i) == tab;
            dirty = SetDisplay(*pages_[i], selected) || dirty;
            buttonElements_[ToIndex(ButtonId::TabHunt) + i]->SetClass("selected", selected);
            dirty = true;
        }
        ApplyItemScroll(currentContent_.extraItems.size());
        return dirty;
    }

    bool ApplyClass(int characterClass)
    {
        if (currentContent_.characterClass == characterClass && contentApplied_)
            return false;
        const bool isKnight = characterClass == 1;
        const bool isDarkLord = characterClass == 4;
        const bool isElf = characterClass == 2;
        const bool hasParty = characterClass == 0 || isElf;
        const bool isSummoner = characterClass == 5;
        const std::array visible{true, isKnight, isDarkLord, hasParty, isElf, isSummoner};
        bool dirty = false;
        for (std::size_t i = 0; i < classPanels_.size(); ++i)
            dirty = SetDisplay(*classPanels_[i], visible[i]) || dirty;
        return dirty;
    }

    bool ApplySubPage(int subPage)
    {
        if (currentContent_.subPage == subPage && contentApplied_)
            return false;
        bool dirty = SetDisplay(*subWindow_, subPage >= 0);
        subWindow_->SetClass("activation", subPage == 2 || subPage == 3);
        subWindow_->SetClass("drain-recovery", subPage == 5);
        const std::array visible{
            subPage >= 4 && subPage <= 6, subPage == 4 || subPage == 5, subPage == 5,
            subPage == 2 || subPage == 3, subPage == 7 || subPage == 8, subPage == 8};
        for (std::size_t i = 0; i < subPanels_.size(); ++i)
            dirty = SetDisplay(*subPanels_[i], visible[i]) || dirty;
        return dirty;
    }

    bool ApplyRanges(const RmlMuHelperContent &content)
    {
        if (currentContent_.huntingRange == content.huntingRange &&
            currentContent_.obtainingRange == content.obtainingRange && contentApplied_)
            return false;
        for (std::size_t i = 0; i < RmlMuHelperRangeCount; ++i)
        {
            buttonElements_[ToIndex(ButtonId::HuntRange1) + i]->SetClass(
                "selected", static_cast<int>(i) < content.huntingRange);
            buttonElements_[ToIndex(ButtonId::ObtainRange1) + i]->SetClass(
                "selected", static_cast<int>(i) < content.obtainingRange);
        }
        return true;
    }

    bool ApplySkillIcons(const RmlMuHelperContent &content)
    {
        bool dirty = false;
        for (std::size_t i = 0; i < assignedIcons_.size(); ++i)
            dirty = assignedIcons_[i].Set(content.assignedIcons[i]) || dirty;
        for (std::size_t i = 0; i < availableIcons_.size(); ++i)
            dirty = availableIcons_[i].Set(content.availableIcons[i]) || dirty;
        return dirty;
    }

    bool ApplySkills(const RmlMuHelperContent &content)
    {
        bool dirty = ApplySkillIcons(content);
        if (!contentApplied_ || currentContent_.assignedSkills != content.assignedSkills)
        {
            for (std::size_t i = 0; i < assignedSlotElements_.size(); ++i)
            {
                assignedSlotElements_[i]->SetClass("filled", content.assignedSkills[i] > 0);
            }
            dirty = true;
        }
        if (!contentApplied_ || currentContent_.skillPickerVisible != content.skillPickerVisible ||
            currentContent_.availableSkillCount != content.availableSkillCount)
        {
            dirty = SetDisplay(*picker_, content.skillPickerVisible) || dirty;
            for (std::size_t i = 0; i < availableSlotElements_.size(); ++i)
            {
                dirty = SetDisplay(*availableSlotElements_[i], i < content.availableSkillCount) ||
                        dirty;
            }
        }
        return dirty;
    }

    bool ApplyItems(const std::vector<std::wstring> &items)
    {
        if (contentApplied_ && currentContent_.extraItems == items)
            return false;
        itemPosition_ =
            std::min(itemPosition_, items.size() > ItemRowCount ? items.size() - ItemRowCount : 0);
        selectedItem_ = -1;
        ApplyItemRows(items);
        return true;
    }

    void ApplyItemRows(const std::vector<std::wstring> &items)
    {
        for (std::size_t row = 0; row < ItemRowCount; ++row)
        {
            auto &element = *buttonElements_[ToIndex(ButtonId::ItemRow0) + row];
            const auto index = itemPosition_ + row;
            const bool visible = index < items.size();
            SetDisplay(element, visible);
            if (visible)
                element.SetInnerRML(Rml::StringUtilities::EncodeRml(
                    StringUtils::WideToNarrow(items[index].c_str())));
            element.SetClass("selected", static_cast<int>(index) == selectedItem_);
        }
        ApplyItemScroll(items.size());
    }

    void ApplyItemScroll(std::size_t count)
    {
        RmlMuScrollBarState state;
        state.position = itemPosition_;
        state.maximum = count > ItemRowCount ? count - ItemRowCount : 0;
        state.pageSize = ItemRowCount;
        state.up = itemPosition_ > 0 ? ButtonVisualState::Up : ButtonVisualState::Disabled;
        state.down =
            itemPosition_ < state.maximum ? ButtonVisualState::Up : ButtonVisualState::Disabled;
        state.thumb = state.maximum > 0 ? ButtonVisualState::Up : ButtonVisualState::Disabled;
        itemScroll_.Apply(state);
    }

    void ReadItemScroll()
    {
        if (const auto position = itemScroll_.TakeRequestedPosition())
        {
            itemPosition_ = *position;
            ApplyItemRows(currentContent_.extraItems);
            inputDirty_ = true;
        }
    }

    bool SyncForm(const RmlMuHelperFormValues &value, int subPage)
    {
        const std::size_t skillIndex = subPage == 3 ? 1U : 0U;
        const std::array checkValues{value.fallbackBasicAttack,
                                     value.concentratedMonsters,
                                     value.useSkillsClosely,
                                     value.usePotion,
                                     value.longRangeCounter,
                                     value.returnPosition,
                                     value.combo,
                                     value.buffDuration,
                                     value.useDarkRaven,
                                     value.supportParty,
                                     value.autoHeal,
                                     value.drainLife,
                                     value.repairItem,
                                     value.pickAll,
                                     value.pickSelected,
                                     value.pickJewel,
                                     value.pickAncient,
                                     value.pickZen,
                                     value.pickExcellent,
                                     value.pickExtra,
                                     value.autoAcceptFriend,
                                     value.autoAcceptGuild,
                                     value.selfDefense,
                                     value.partyHeal,
                                     value.partyBuffDuration,
                                     value.skillTimer[skillIndex],
                                     value.skillCondition[skillIndex]};
        bool dirty = false;
        for (std::size_t i = 0; i < checks_.size(); ++i)
            dirty = SetChecked(*checks_[i], checkValues[i]) || dirty;
        dirty = SyncRadio(darkMode_, value.darkRavenMode) || dirty;
        dirty = SyncRadio(skillPre_, value.skillPreCondition) || dirty;
        dirty = SyncRadio(skillMobs_, value.skillMobCount) || dirty;
        dirty = SyncThresholds(value) || dirty;
        SyncSkillConditions(value.skillCondition[skillIndex]);
        return SyncTextInputs(value, skillIndex) || dirty;
    }

    bool SyncThresholds(const RmlMuHelperFormValues &value)
    {
        bool dirty = SyncRadio(potionLevels_, value.potionThreshold / 10 - 1);
        dirty = SyncRadio(healLevels_, value.healThreshold / 10 - 1) || dirty;
        dirty = SyncRadio(partyHealLevels_, value.partyHealThreshold / 10 - 1) || dirty;
        ApplySegmentedFill(potionLevels_, value.potionThreshold / 10);
        ApplySegmentedFill(healLevels_, value.healThreshold / 10);
        ApplySegmentedFill(partyHealLevels_, value.partyHealThreshold / 10);
        return dirty;
    }

    void SyncSkillConditions(bool enabled)
    {
        for (auto *input : skillPre_)
            input->SetDisabled(!enabled);
        for (auto *input : skillMobs_)
            input->SetDisabled(!enabled);
    }

    bool SyncTextInputs(const RmlMuHelperFormValues &value, std::size_t skillIndex)
    {
        const std::array values{std::to_string(value.returnSeconds),
                                std::to_string(value.manualControlYieldSeconds),
                                StringUtils::WideToNarrow(value.itemInput.c_str()),
                                std::to_string(value.partyBuffInterval),
                                std::to_string(value.skillInterval[skillIndex])};
        bool dirty = false;
        for (std::size_t i = 0; i < inputs_.size(); ++i)
        {
            if (inputs_[i]->GetValue() == values[i])
                continue;
            inputs_[i]->SetValue(values[i]);
            dirty = true;
        }
        return dirty;
    }

    bool SyncWidgets()
    {
        bool dirty = false;
        for (RmlMuButton &button : buttons_)
            dirty = button.SyncVisualState() || dirty;
        for (RmlMuSlot &slot : assignedSlots_)
            dirty = slot.SyncVisualState() || dirty;
        for (RmlMuSlot &slot : availableSlots_)
            dirty = slot.SyncVisualState() || dirty;
        contentApplied_ = true;
        return dirty;
    }

    void ReadFormChanges()
    {
        if (!formValues_.has_value())
            return;
        RmlMuHelperFormValues next = *formValues_;
        ReadMainChecks(next);
        ReadSubValues(next);
        NormalizeExclusiveChecks(next);
        next.returnSeconds = ParseNumber(*inputs_[ToIndex(InputId::ReturnSeconds)]);
        next.manualControlYieldSeconds =
            ParseNumber(*inputs_[ToIndex(InputId::ManualControlYieldSeconds)]);
        next.partyBuffInterval = ParseNumber(*inputs_[ToIndex(InputId::PartyBuffInterval)]);
        if (currentContent_.subPage == 2 || currentContent_.subPage == 3)
        {
            const std::size_t skillIndex = currentContent_.subPage == 3 ? 1U : 0U;
            next.skillInterval[skillIndex] =
                ParseNumber(*inputs_[ToIndex(InputId::SubSkillInterval)]);
        }
        next.itemInput =
            StringUtils::NarrowToWide(inputs_[ToIndex(InputId::ItemName)]->GetValue().c_str());
        if (next == *formValues_)
            return;
        SyncThresholds(next);
        SyncSkillConditions(next.skillCondition[currentContent_.subPage == 3 ? 1 : 0]);
        inputDirty_ = true;
        formValues_ = next;
        changes_.form = std::move(next);
    }

    void ReadMainChecks(RmlMuHelperFormValues &next) const
    {
        const auto checked = [this](CheckId id) { return IsChecked(*checks_[ToIndex(id)]); };
        next.fallbackBasicAttack = checked(CheckId::FallbackBasicAttack);
        next.concentratedMonsters = checked(CheckId::ConcentratedMonsters);
        next.useSkillsClosely = checked(CheckId::UseSkillsClosely);
        next.usePotion = checked(CheckId::UsePotion);
        next.longRangeCounter = checked(CheckId::LongRangeCounter);
        next.returnPosition = checked(CheckId::ReturnPosition);
        next.combo = checked(CheckId::Combo);
        next.buffDuration = checked(CheckId::BuffDuration);
        next.useDarkRaven = checked(CheckId::UseDarkRaven);
        next.supportParty = checked(CheckId::SupportParty);
        next.autoHeal = checked(CheckId::AutoHeal);
        next.drainLife = checked(CheckId::DrainLife);
        next.repairItem = checked(CheckId::RepairItem);
        next.pickAll = checked(CheckId::PickAll);
        next.pickSelected = checked(CheckId::PickSelected);
        next.pickJewel = checked(CheckId::PickJewel);
        next.pickAncient = checked(CheckId::PickAncient);
        next.pickZen = checked(CheckId::PickZen);
        next.pickExcellent = checked(CheckId::PickExcellent);
        next.pickExtra = checked(CheckId::PickExtra);
        next.autoAcceptFriend = checked(CheckId::AutoAcceptFriend);
        next.autoAcceptGuild = checked(CheckId::AutoAcceptGuild);
        next.selfDefense = checked(CheckId::SelfDefense);
    }

    void NormalizeExclusiveChecks(RmlMuHelperFormValues &next)
    {
        NormalizePair(next.pickAll, next.pickSelected, formValues_->pickAll, CheckId::PickAll,
                      CheckId::PickSelected);
        if (currentContent_.subPage == 2 || currentContent_.subPage == 3)
        {
            const std::size_t skill = currentContent_.subPage == 3 ? 1U : 0U;
            NormalizePair(next.skillTimer[skill], next.skillCondition[skill],
                          formValues_->skillTimer[skill], CheckId::SubTimer,
                          CheckId::SubMonsterCondition);
        }
    }

    void NormalizePair(bool &first, bool &second, bool previousFirst, CheckId firstId,
                       CheckId secondId)
    {
        if (!first || !second)
            return;
        if (first != previousFirst)
        {
            second = false;
            (void)SetChecked(*checks_[ToIndex(secondId)], false);
        }
        else
        {
            first = false;
            (void)SetChecked(*checks_[ToIndex(firstId)], false);
        }
    }

    void ReadSubValues(RmlMuHelperFormValues &next) const
    {
        next.darkRavenMode = CheckedIndex(darkMode_, next.darkRavenMode);
        next.skillPreCondition = CheckedIndex(skillPre_, next.skillPreCondition);
        next.skillMobCount = CheckedIndex(skillMobs_, next.skillMobCount);
        const int potion = CheckedIndex(potionLevels_, -1);
        const int heal = CheckedIndex(healLevels_, -1);
        const int partyHeal = CheckedIndex(partyHealLevels_, -1);
        if (potion >= 0)
            next.potionThreshold = (potion + 1) * 10;
        if (heal >= 0)
            next.healThreshold = (heal + 1) * 10;
        if (partyHeal >= 0)
            next.partyHealThreshold = (partyHeal + 1) * 10;
        next.partyHeal = IsChecked(*checks_[ToIndex(CheckId::PartyHeal)]);
        next.partyBuffDuration = IsChecked(*checks_[ToIndex(CheckId::PartyBuffDuration)]);
        if (currentContent_.subPage == 2 || currentContent_.subPage == 3)
        {
            const std::size_t skillIndex = currentContent_.subPage == 3 ? 1U : 0U;
            next.skillTimer[skillIndex] = IsChecked(*checks_[ToIndex(CheckId::SubTimer)]);
            next.skillCondition[skillIndex] =
                IsChecked(*checks_[ToIndex(CheckId::SubMonsterCondition)]);
        }
    }

    void ReadButtonChanges()
    {
        changes_.close = Clicked(ButtonId::Close) || changes_.close;
        changes_.reset = Clicked(ButtonId::Reset) || changes_.reset;
        changes_.save = Clicked(ButtonId::Save) || changes_.save;
        changes_.addItem = Clicked(ButtonId::AddItem) || changes_.addItem;
        if (Clicked(ButtonId::DeleteItem))
            changes_.removeItemIndex = selectedItem_;
        for (std::size_t row = 0; row < ItemRowCount; ++row)
        {
            if (!buttons_[ToIndex(ButtonId::ItemRow0) + row].IsClick())
                continue;
            selectedItem_ = static_cast<int>(itemPosition_ + row);
            ApplyItemRows(currentContent_.extraItems);
            inputDirty_ = true;
        }
        ReadTabButtons();
        ReadRangeButtons();
        ReadSubButtons();
        ReadSkillSlots();
    }

    bool Clicked(ButtonId id) const
    {
        return buttons_[ToIndex(id)].IsClick();
    }

    void ReadTabButtons()
    {
        for (int tab = 0; tab < 3; ++tab)
        {
            if (buttons_[ToIndex(ButtonId::TabHunt) + tab].IsClick())
                changes_.tab = tab;
        }
    }

    void ReadRangeButtons()
    {
        for (std::size_t i = 0; i < RmlMuHelperRangeCount; ++i)
        {
            if (buttons_[ToIndex(ButtonId::HuntRange1) + i].IsClick())
                changes_.huntingRange = static_cast<int>(i) + 1;
            if (buttons_[ToIndex(ButtonId::ObtainRange1) + i].IsClick())
                changes_.obtainingRange = static_cast<int>(i) + 1;
        }
    }

    void ReadSubButtons()
    {
        if (Clicked(ButtonId::PotionSettings))
            changes_.openSubPage = 0;
        if (Clicked(ButtonId::PartySettings))
            changes_.openSubPage = 1;
        if (Clicked(ButtonId::Skill1Settings))
            changes_.openSubPage = 2;
        if (Clicked(ButtonId::Skill2Settings))
            changes_.openSubPage = 3;
        changes_.closeSubPage = Clicked(ButtonId::SubClose) || changes_.closeSubPage;
        changes_.resetSubPage = Clicked(ButtonId::SubReset) || changes_.resetSubPage;
        changes_.saveSubPage = Clicked(ButtonId::SubSave) || changes_.saveSubPage;
    }

    void ReadSkillSlots()
    {
        for (std::size_t i = 0; i < RmlMuHelperAssignedSkillCount; ++i)
        {
            if (assignedSlots_[i].IsClick())
                changes_.assignedSkillSlot = static_cast<int>(i);
            if (assignedSlots_[i].IsClear())
                changes_.clearSkillSlot = static_cast<int>(i);
        }
        for (std::size_t i = 0; i < RmlMuHelperAvailableSkillCount; ++i)
        {
            if (availableSlots_[i].IsClick())
                changes_.availableSkillSlot = static_cast<int>(i);
        }
    }

    void ClearPointers() noexcept
    {
        panel_ = nullptr;
        drag_ = nullptr;
        picker_ = nullptr;
        subWindow_ = nullptr;
        itemList_ = nullptr;
        itemPosition_ = 0;
        selectedItem_ = -1;
        pages_.fill(nullptr);
        classPanels_.fill(nullptr);
        subPanels_.fill(nullptr);
        textElements_.fill(nullptr);
        textCopies_.fill(nullptr);
        buttonElements_.fill(nullptr);
        checks_.fill(nullptr);
        inputs_.fill(nullptr);
        darkMode_.fill(nullptr);
        skillPre_.fill(nullptr);
        skillMobs_.fill(nullptr);
        potionLevels_.fill(nullptr);
        healLevels_.fill(nullptr);
        partyHealLevels_.fill(nullptr);
        assignedSlotElements_.fill(nullptr);
        availableSlotElements_.fill(nullptr);
        contentApplied_ = false;
    }

    SessionBoundArray<RmlMuButton, ButtonCount> &buttons_;
    SessionBoundArray<RmlMuSlot, RmlMuHelperAssignedSkillCount> &assignedSlots_;
    SessionBoundArray<RmlMuSlot, RmlMuHelperAvailableSkillCount> &availableSlots_;
    RmlDocumentHost host_;
    RmlMuMovablePanel movable_;
    RmlMuHelperContent currentContent_{};
    std::optional<RmlMuHelperFormValues> formValues_;
    RmlMuHelperChanges changes_{};
    Rml::Element *panel_ = nullptr;
    Rml::Element *drag_ = nullptr;
    Rml::Element *picker_ = nullptr;
    Rml::Element *subWindow_ = nullptr;
    Rml::Element *itemList_ = nullptr;
    RmlMuScrollBar itemScroll_;
    std::size_t itemPosition_ = 0;
    int selectedItem_ = -1;
    std::array<Rml::Element *, 3> pages_{};
    std::array<Rml::Element *, 6> classPanels_{};
    std::array<Rml::Element *, 6> subPanels_{};
    std::array<Rml::Element *, static_cast<std::size_t>(RmlMuHelperTextId::Count)> textElements_{};
    std::array<Rml::Element *, TextCopyElementIds.size()> textCopies_{};
    std::array<Rml::Element *, ButtonCount> buttonElements_{};
    std::array<Rml::ElementFormControlInput *, static_cast<std::size_t>(CheckId::Count)> checks_{};
    std::array<Rml::ElementFormControlInput *, static_cast<std::size_t>(InputId::Count)> inputs_{};
    std::array<Rml::ElementFormControlInput *, 3> darkMode_{};
    std::array<Rml::ElementFormControlInput *, 2> skillPre_{};
    std::array<Rml::ElementFormControlInput *, 4> skillMobs_{};
    std::array<Rml::ElementFormControlInput *, 10> potionLevels_{};
    std::array<Rml::ElementFormControlInput *, 10> healLevels_{};
    std::array<Rml::ElementFormControlInput *, 10> partyHealLevels_{};
    std::array<RmlSkillIcon, RmlMuHelperAssignedSkillCount> assignedIcons_;
    std::array<RmlSkillIcon, RmlMuHelperAvailableSkillCount> availableIcons_;
    std::array<Rml::Element *, RmlMuHelperAssignedSkillCount> assignedSlotElements_{};
    std::array<Rml::Element *, RmlMuHelperAvailableSkillCount> availableSlotElements_{};
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
    float scaleX_ = 1.0F;
    float scaleY_ = 1.0F;
    bool positionSet_ = false;
    bool inputDirty_ = false;
    bool contentApplied_ = false;
    bool visible_ = false;
    std::atomic<bool> publishedVisible_{false};
    std::atomic<bool> publishedOwnsPointer_{false};
};

RmlMuHelperPanel::RmlMuHelperPanel(SessionKeeper &keeper)
    : buttons_(keeper), assignedSlots_(keeper), availableSlots_(keeper),
      impl_(std::make_unique<Impl>(keeper, buttons_, assignedSlots_, availableSlots_))
{
}

RmlMuHelperPanel::~RmlMuHelperPanel() = default;

void RmlMuHelperPanel::Create()
{
    for (RmlMuButton &button : buttons_)
    {
        button.SetEnable(true);
        button.Reset();
    }
    for (RmlMuSlot &slot : assignedSlots_)
    {
        slot.SetEnable(true);
        slot.Reset();
    }
    for (RmlMuSlot &slot : availableSlots_)
    {
        slot.SetEnable(true);
        slot.Reset();
    }
}

void RmlMuHelperPanel::Release()
{
    impl_->Release();
}

bool RmlMuHelperPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}

bool RmlMuHelperPanel::HasTextInputFocus() const noexcept
{
    return impl_->HasTextInputFocus();
}

bool RmlMuHelperPanel::OwnsPointer() const noexcept
{
    return impl_->OwnsPointer();
}

RmlMuHelperChanges RmlMuHelperPanel::TakeChanges()
{
    return impl_->TakeChanges();
}

bool RmlMuHelperPanel::PrepareOnWorker(int viewportWidth, int viewportHeight, bool visible,
                                       const RmlMuHelperContent &content)
{
    return impl_->Prepare(viewportWidth, viewportHeight, visible, content);
}

bool RmlMuHelperPanel::Record(LegacyRenderFacade &facade) const
{
    return impl_->Record(facade);
}

std::optional<RmlTextInputArea> RmlMuHelperPanel::TextInputArea() const
{
    return impl_->TextInputArea();
}

} // namespace UI::Modern::PC::MuHelper

namespace UI::Skills::Tooltip
{

namespace
{
// Map the renderer-agnostic LineColor enum to the legacy TextList color
// constants used by RenderTipTextList.
int LegacyColor(LineColor c)
{
    switch (c)
    {
    case LineColor::White:
        return TEXT_COLOR_WHITE;
    case LineColor::Blue:
        return TEXT_COLOR_BLUE;
    case LineColor::Red:
        return TEXT_COLOR_RED;
    case LineColor::DarkRed:
        return TEXT_COLOR_DARKRED;
    }
    return TEXT_COLOR_WHITE;
}
} // namespace

SkillTooltipRendererLegacyCalls::SkillTooltipRendererLegacyCalls(SessionKeeper &keeper,
                                                                 Renderer &owner) noexcept
    : SessionUiLegacyBindings(keeper), owner_(owner)
{
}

Renderer::Renderer(SessionKeeper &keeper) noexcept
    : SkillTooltipRendererLegacyCalls(keeper, *this), modelBuilder_(keeper)
{
}

void Renderer::Render(int sx, int sy, int Type, int /*SkillNum*/, int iRenderPoint /*= STRP_NONE*/)
{
    // Pet command icons get a different UI entirely (delegated to giPetManager).
    if (RenderPetCmdInfo(sx, sy, Type))
        return;

    if (!CharacterAttribute)
        return;

    const int skillType = CharacterAttribute->Skill[Type];

    BuildOptions options;
    options.skillType = skillType;
    options.skillSlotIndex = Type;
    options.includeCharacterSpecific = true;

    Model model;
    modelBuilder_.Build(options, model);

    // Copy the model into the legacy TextList / Color / Bold buffers that
    // RenderTipTextList consumes. Pre-allocated globals, no heap. The legacy
    // TextList row is wchar_t[100] while the model line buffer is wider, so
    // truncate rather than overflow.
    constexpr size_t kLegacyLineCap = 100;
    const int lineCount = (model.count < MAX_TOOLTIP_LINES) ? model.count : MAX_TOOLTIP_LINES;
    for (int i = 0; i < lineCount; ++i)
    {
        const Line &src = model.lines[i];
        wcsncpy(TextList[i], src.text, kLegacyLineCap - 1);
        TextList[i][kLegacyLineCap - 1] = L'\0';
        TextListColor[i] = LegacyColor(src.color);
        TextBold[i] = src.isBold ? 1 : 0;
    }

    SIZE TextSize = {0, 0};
    g_RenderText.MeasureText(TextList[0], 1, &TextSize);

    if (iRenderPoint == STRP_NONE)
    {
        const int Height =
            ((model.count - model.skipCount) * TextSize.cy + model.skipCount * TextSize.cy / 2) /
            g_fScreenRate_y;
        sy -= Height;
    }

    RenderTipTextList(sx, sy, model.count, 0, RT3_SORT_CENTER, iRenderPoint);
}

} // namespace UI::Skills::Tooltip

void UI::Skills::Tooltip::SkillTooltipRendererLegacyCalls::Render(int sx, int sy, int Type,
                                                                  int SkillNum, int iRenderPoint)
{
    return owner_.Render(sx, sy, Type, SkillNum, iRenderPoint);
} // OMF-01942

using namespace SEASON3B;

void SEASON3B::CNewUICharacterInfoWindow::SetPos(int x, int y)
{
    (void)x;
    (void)y;
}

bool SEASON3B::CNewUICharacterInfoWindow::Render()
{
    return m_modernPanel.Record(renderer_.LegacyRender());
}

bool SEASON3B::CNewUICharacterInfoWindow::PrepareModernUiOnWorker(int viewportWidth,
                                                                  int viewportHeight)
{
    return m_modernPanel.PrepareOnWorker(viewportWidth, viewportHeight, modernVisible_,
                                         modernContent_);
}

namespace SEASON3B
{

void CNewUIPetInfoWindow::SetPos(int x, int y)
{
    (void)x;
    (void)y;
}

bool CNewUIPetInfoWindow::Render()
{
    const bool recorded = modernPanel_.Record(renderer_.LegacyRender());
    return recorded;
}

bool CNewUIPetInfoWindow::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight)
{
    return modernPanel_.PrepareOnWorker(viewportWidth, viewportHeight, modernVisible_,
                                        modernContent_);
}

} // namespace SEASON3B

void SEASON3B::CNewUIBuffWindow::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

void SEASON3B::CNewUIBuffWindow::SetPos(int)
{
}

bool SEASON3B::CNewUIBuffWindow::PrepareModernUiOnWorker(int width, int height)
{
    return buffs_.PrepareOnWorker(width, height, visible_, entries_);
}

bool SEASON3B::CNewUIBuffWindow::Render()
{
    if (!buffs_.Record(renderer_.LegacyRender()))
        return false;
    auto buff = static_cast<eBuffState>(buffs_.HoveredBuff());
    float x, y;
    if (buff != eBuffNone && buffs_.HoverAnchor(x, y))
    {
        auto buffClass = g_IsBuffClass(buff);
        RenderBuffTooltip(buffClass, buff, x / ModernUiScreenRateX(), y / ModernUiScreenRateY());
    }
    return true;
}

void SEASON3B::CNewUIBuffWindow::RenderBuffTooltip(eBuffClass &eBuffClassType,
                                                   eBuffState &eBuffType, float x, float y)
{
    int TextNum = 0;
    ::memset(TextList[0], 0, sizeof(char) * 30 * 100);
    ::memset(TextListColor, 0, sizeof(int) * 30);
    ::memset(TextBold, 0, sizeof(int) * 30);

    std::list<std::wstring> tooltipinfo;
    g_BuffToolTipString(tooltipinfo, eBuffType);

    for (std::list<std::wstring>::iterator iter = tooltipinfo.begin(); iter != tooltipinfo.end();
         ++iter)
    {
        std::wstring &temp = *iter;

        mu_swprintf(TextList[TextNum], temp.c_str());

        if (TextNum == 0)
        {
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = true;
        }
        else
        {
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
        }

        TextNum += 1;
    }

    std::wstring bufftime;
    g_BuffStringTime(eBuffType, bufftime);

    if (bufftime.size() != 0)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::DurationPeriodS, bufftime.c_str());
        TextListColor[TextNum] = TEXT_COLOR_PURPLE;
        TextBold[TextNum] = false;
        TextNum += 1;
    }

    SIZE TextSize = {0, 0};
    g_RenderText.MeasureText(TextList[0], 1, &TextSize);
    RenderTipTextList(x, y, TextNum, 0);
}

bool SEASON3B::CNewUICommandWindow::Render()
{
    const bool recorded = m_modernPanel.Record(LegacyRender());
    RenderSelectedCharacter();
    return recorded;
}

void SEASON3B::CNewUICommandWindow::RenderSelectedCharacter()
{
    if (m_iCurMouseCursor != CURSOR_IDSELECT || !m_bSelectedChar)
        return;
    CHARACTER *const character = &CharactersClient[SelectedCharacter];
    if (character == nullptr || character->Object.Kind != KIND_PLAYER || character == Hero ||
        (character->Object.Type != MODEL_PLAYER && !character->Change))
    {
        return;
    }

    static const UI::Modern::RmlUiDesign style(
        "Data/UI/PC/Command/command_window.rml",
        {"CommandTarget-Frame", "CommandTarget-TextY", "CommandTarget-AllowedColor",
         "CommandTarget-DeniedColor", "CommandTarget-BackgroundColor"});
    const auto frame = style.Values(0);
    const auto color = style.Values(m_bCanCommand ? 2 : 3);
    const auto background = style.Values(4);
    EnableAlphaTest();
    glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
    RenderBitmap(BITMAP_COMMAND_WINDOW_BEGIN, MouseX + frame[0], MouseY + frame[1], frame[2],
                 frame[3]);
    g_RenderText.SetFont(LegacyFontRole::Large);
    g_RenderText.SetTextColor(color[0], color[1], color[2], color[3]);
    g_RenderText.SetBgColor(background[0], background[1], background[2], background[3]);
    g_RenderText.RenderText(MouseX + frame[0] + frame[2] / 2, MouseY + style.Number(1),
                            character->ID, 0, 0, RT3_WRITE_CENTER);
    g_RenderText.SetFont(LegacyFontRole::Normal);
    DisableAlphaBlend();
}

void SEASON3B::CNewUICommandWindow::SetPos(int x, int y)
{
    (void)x;
    (void)y;
}

void SEASON3B::CNewUICommandWindow::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_cursorid_wnd.jpg", IMAGE_COMMAND_SELECTID_BG,
                LegacyTextureFilter::Linear);
}

void SEASON3B::CNewUICommandWindow::UnloadImages()
{
    DeleteBitmap(IMAGE_COMMAND_SELECTID_BG);
}

bool SEASON3B::CNewUICommandWindow::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight)
{
    UI::Modern::PC::Command::RmlCommandWindowContent content;
    content.title = I18N::Game::CommandWindow;
    content.labels = {I18N::Game::Trade,
                      I18N::Game::Buy1124,
                      I18N::Game::Party,
                      I18N::Game::Whisper,
                      I18N::Game::Guild,
                      I18N::Game::Alliance,
                      I18N::Game::HostilityGuild,
                      I18N::Game::SuspendHostilities,
                      I18N::Game::AddFriend,
                      I18N::Game::Follow,
                      I18N::Game::Duel};
    content.selected = m_iCurSelectCommand;
    return m_modernPanel.PrepareOnWorker(viewportWidth, viewportHeight, IsVisible(), content);
}

#ifdef PBG_ADD_GENSRANKING

using namespace SEASON3B;

#define TEMP_MAX_TEXT_LENGTH 1024

void CNewUIGensRanking::SetPos(int x, int y)
{
    m_Pos = {x, y};
}

void CNewUIGensRanking::StageContent()
{
    const char *locale = I18N::GetCurrentLocale();
    if (!contentDirty_ && locale_ == locale && stagedRank_ == Hero->GensRanking)
        return;
    locale_ = locale;
    stagedRank_ = Hero->GensRanking;
    content_.labels = {I18N::Game::GensInfoWindow,
                       I18N::Game::Gens,
                       GetGensTeamName(),
                       I18N::Game::Level3095,
                       GetTitleName(Hero->GensRanking),
                       I18N::Game::GensRanking,
                       GetRanking(),
                       L"",
                       I18N::Game::GainContribution,
                       std::to_wstring(GetContribution()),
                       I18N::Game::GensDescription,
                       L"",
                       L"",
                       I18N::Game::Close388};
    if (GetNextContribution() > 0)
    {
        wchar_t text[TEMP_MAX_TEXT_LENGTH];
        mu_swprintf(text, I18N::Game::TheAmountOfContributionNeededForPromotionToTheNextRankIsD,
                    GetNextContribution());
        content_.labels[11] = text;
    }
    content_.labels[12] = std::wstring(I18N::Game::GensRankingRewardsAreGivenOut) + L"\n" +
                          std::wstring(I18N::Game::GensRankingRewardsCanBeClaimed);
    for (auto &text : content_.labels)
        std::replace(text.begin(), text.end(), L'#', L'\n');
    content_.faction = m_byGensInfluence;
    content_.rank = Hero->GensRanking >= TITLENAME_START && Hero->GensRanking <= TITLENAME_END
                        ? Hero->GensRanking
                        : TITLENAME_END;
    ++content_.revision;
    contentDirty_ = false;
}

bool CNewUIGensRanking::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}
bool CNewUIGensRanking::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_, content_);
}

void CNewUIGensRanking::SetTitleName()
{
    wchar_t _szTempText[256] = {
        0,
    };
    mu_swprintf(_szTempText, I18N::Game::GrandDukeDukeMarquisCountViscount);
    DivideStringByPixel(&m_szTitleName[0][0], TITLENAME_END, MAX_TITLELENGTH, _szTempText, 240,
                        true, '#');
}

#endif //PBG_ADD_GENSRANKING

bool SEASON3B::CNewUIHotKey::Render()
{
    return true;
}

void SEASON3B::CNewUIHotKey::SetStateGameOver(bool bGameOver)
{
    m_bStateGameOver = bGameOver;
}

bool SEASON3B::CNewUIMasterLevel::Render()
{
    const bool recorded = modernPanel_.Record(renderer_.LegacyRender());
    RenderToolTip();
    return recorded;
}

bool SEASON3B::CNewUIMasterLevel::PrepareModernUiOnWorker(int width, int height)
{
    if (IsVisible())
        UpdateModernContent();
    return modernPanel_.PrepareOnWorker(width, height, IsVisible(), modernContent_);
}

void SEASON3B::CNewUIMasterLevel::RenderToolTip()
{
    if (IsPress(VK_LBUTTON))
        return;
    const auto hover = modernPanel_.Hovered();
    if (hover.slot == -1)
    {
        TextBold[0] = 0;
        TextListColor[0] = 0;
        mu_swprintf(TextList[0], L"%I64d / %I64d", Master_Level_Data.lMasterLevel_Experince,
                    Master_Level_Data.lNext_MasterLevel_Experince);
        RenderTipTextList(hover.x, hover.y, 1, 0, 3, 0, 1);
        return;
    }
    if (const auto it = map_masterData.find(hover.slot); hover.slot && it != map_masterData.end())
        RenderSkillToolTip(it->second, hover);
}

void SEASON3B::CNewUIMasterLevel::RenderSkillToolTip(
    const _MASTER_SKILLTREE_DATA &data, const UI::Modern::RmlMasterSkillTreePanel::Hover &hover)
{
    const auto group = data.Group;
    const auto Skill = data.Skill;
    const auto *p = &SkillAttribute[Skill];
    auto mtit = this->map_masterSkillToolTip.find(Skill);

    if (mtit == this->map_masterSkillToolTip.end())
    {
        return;
    }

    auto skillInfo = CharacterAttribute->MasterSkillInfo[Skill];
    const auto skillLevel = skillInfo.GetSkillLevel();
    auto skillValue = skillInfo.GetSkillValue();
    const auto skillNextValue = skillInfo.GetSkillNextValue();

    for (int i = 0; i < 30; i++)
    {
        TextList[i][0] = 0;
    }

    memset(TextBold, 0, sizeof(TextBold));

    for (int i = 0; i < 30; i++)
    {
        TextListColor[i] = i == 0 ? TEXT_COLOR_YELLOW : TEXT_COLOR_WHITE;
    }

    int lineCount = 0;

    mu_swprintf(TextList[lineCount], L"%ls", p->Name);

    TextBold[lineCount] = true;

    lineCount++;

    mu_swprintf(TextList[lineCount], mtit->second.Info1, p->SkillRank, skillLevel, data.MaxLevel);

    lineCount++;

    wchar_t buffer[512] = {};

    if (data.DefValue == -1.0f)
    {
        mu_swprintf(buffer, mtit->second.Info2);
    }
    else
    {
        mu_swprintf(buffer, mtit->second.Info2, skillLevel != 0 ? skillValue : data.DefValue);
    }

    lineCount = this->SetDivideString(buffer, 0, lineCount, 0, 0, true);

    if (skillLevel != 0 && skillLevel < data.MaxLevel)
    {
        mu_swprintf(buffer, I18N::Game::NextLevel);

        lineCount = this->SetDivideString(buffer, 0, lineCount, 4, 0, true);

        TextBold[lineCount] = 1;

        mu_swprintf(buffer, mtit->second.Info2, skillNextValue);

        lineCount = this->SetDivideString(buffer, 0, lineCount, 0, 0, true);
    }

    if (skillLevel < data.MaxLevel)
    {
        mu_swprintf(buffer, I18N::Game::Requirements3329);

        lineCount = this->SetDivideString(buffer, 0, lineCount, 1, 0, true);

        TextBold[lineCount] = 1;

        mu_swprintf(buffer, mtit->second.Info3, data.RequiredPoints);

        if (data.RequiredPoints <= Master_Level_Data.nMLevelUpMPoint)
        {
            lineCount = this->SetDivideString(buffer, 0, lineCount, 0, 0, true);
        }
        else
        {
            lineCount = this->SetDivideString(buffer, 0, lineCount, 2, 0, true);
        }
    }

    int iTextColor = this->CheckBeforeSkill(Skill, skillLevel) == true ? 0 : 2;

    mu_swprintf(buffer, mtit->second.Info4);

    lineCount = this->SetDivideString(buffer, 0, lineCount, iTextColor, 0, true);

    if (skillLevel < data.MaxLevel && p->SkillRank != 1)
    {
        iTextColor = this->CheckRankPoint(group, p->SkillRank, skillLevel) == true ? 0 : 2;

        mu_swprintf(buffer, mtit->second.Info5);

        lineCount = this->SetDivideString(buffer, 0, lineCount, iTextColor, 0, true);

        for (int i = 0; i < MAX_MASTER_SKILL_REQUIRES; i++)
        {
            const auto RequireSkill = data.RequireSkill[i];

            if (RequireSkill >= AT_SKILL_MASTER_BEGIN && RequireSkill <= AT_SKILL_MASTER_END)
            {
                auto requiredSkill = CharacterAttribute->MasterSkillInfo[RequireSkill];
                iTextColor = requiredSkill.GetSkillValue() < 10 ? 2 : 0;
                mu_swprintf(buffer, i == 0 ? mtit->second.Info6 : mtit->second.Info7);
                lineCount = this->SetDivideString(buffer, 0, lineCount, iTextColor, 0, true);
            }
        }
    }

    RenderTipTextList(hover.x, hover.y, lineCount, 0, 3, hover.above ? STRP_BOTTOMCENTER : 0, 1);
}

void CNewUIQuickCommandWindow::SetPos(int x, int y)
{
    m_Pos = {x, y};
}

void CNewUIQuickCommandWindow::StageModernContent()
{
    modernContent_.visible = IsVisible();
    if (!modernContent_.visible)
        return;
    modernContent_.title = m_strID;
    modernContent_.x = m_Pos.x;
    modernContent_.y = m_Pos.y;
    constexpr std::array textIds{943, 1124, 944, 948, 949};
    for (std::size_t i = 0; i < textIds.size(); ++i)
        modernContent_.labels[i] = I18N::Game::Lookup(textIds[i]);
}

bool CNewUIQuickCommandWindow::Render()
{
    return modernPanel_.Record(renderer_.LegacyRender());
}
bool CNewUIQuickCommandWindow::PrepareModernUiOnWorker(int width, int height)
{
    return modernPanel_.PrepareOnWorker(width, height, modernContent_);
}

void UI::NoticeBoard::Render()
{
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_INGAMESHOP))
    {
        return;
    }
#endif

    EnableAlphaTest();
    g_RenderText.SetFont(LegacyFontRole::Bold);

    glColor3f(1.f, 1.f, 1.f);
    for (int i = 0; i < MaxNotices; ++i)
    {
        Notice &notice = notices_[i];
        if (notice.color == 0)
        {
            g_RenderText.SetBgColor(0, 0, 0, 128);
            g_RenderText.SetTextColor(
                255, 200, 80, static_cast<BYTE>(blinkPhase_ < BlinkCycleFrames * 0.5f ? 128 : 255));
        }
        else
        {
            g_RenderText.SetTextColor(100, 255, 200, 255);
            g_RenderText.SetBgColor(0, 0, 0, 128);
        }

        g_RenderText.RenderText(320, 300 + i * 13, notice.text, 0, 0, RT3_WRITE_CENTER);
    }

}

// OMF-01936
// OMF-01938

// Create

bool CNewUIHeroPositionInfo::Render()
{
    const bool recorded = renderer_.RecordTopMenu();
    m_BtnConfig.Render();
    muHelper_.IsActive() ? m_BtnStop.Render() : m_BtnStart.Render();
    return recorded;
}

void CNewUIHeroPositionInfo::StageTopMenu()
{
    UI::Modern::RmlTopMenuRequest request;
    request.visible = IsVisible() && Hero != nullptr;
    if (request.visible)
    {
        const wchar_t *const mapName = gMapManager.GetMapName(gMapManager.ContextMap());
        std::wcsncpy(request.mapName, mapName, UI::Modern::RmlTopMenuRequest::MapNameCapacity - 1);
        request.mapName[UI::Modern::RmlTopMenuRequest::MapNameCapacity - 1] = L'\0';
        request.positionX = m_CurHeroPosition.x;
        request.positionY = m_CurHeroPosition.y;
        request.helperActive = muHelper_.IsActive();
        request.optionButton = ToTopMenuButtonState(m_BtnConfig.GetBTState());
        request.actionButton = ToTopMenuButtonState(request.helperActive ? m_BtnStop.GetBTState()
                                                                         : m_BtnStart.GetBTState());
    }
    renderer_.StageTopMenu(request);
}

bool SessionUiUnit::RenderPetCmdInfo(int sx, int sy, int Type)
{
    if (Type < AT_PET_COMMAND_DEFAULT || Type >= AT_PET_COMMAND_END)
        return false;

    int TextNum = 0;
    int SkipNum = 0;

    if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK_LORD)
    {
        int cmdType = Type - AT_PET_COMMAND_DEFAULT;

        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = true;
        mu_swprintf(TextList[TextNum], I18N::Game::Lookup(1219 + cmdType));
        TextNum++;
        SkipNum++;

        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;
        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;
        switch (cmdType)
        {
        case PET_CMD_DEFAULT:
            mu_swprintf(TextList[TextNum], I18N::Game::FollowAroundTheCharacter);
            TextNum++;
            SkipNum++;
            break;
        case PET_CMD_RANDOM:
            mu_swprintf(TextList[TextNum], I18N::Game::AttackAnyMonstersAroundTheCharacter);
            TextNum++;
            SkipNum++;
            break;
        case PET_CMD_OWNER:
            mu_swprintf(TextList[TextNum], I18N::Game::AttackTheMonsterTogetherWithTheCharacter);
            TextNum++;
            SkipNum++;
            break;
        case PET_CMD_TARGET:
            mu_swprintf(TextList[TextNum], I18N::Game::AttackTheMonsterSelectedByTheCharacter);
            TextNum++;
            SkipNum++;
            break;
        }

        SIZE TextSize = {0, 0};
        g_RenderText.MeasureText(TextList[0], 1, &TextSize);
        int Height = (int)(((TextNum - SkipNum) * TextSize.cy + SkipNum * TextSize.cy / 2) /
                           g_fScreenRate_y);
        sy -= Height;

        RenderTipTextList(sx, sy, TextNum, 0);
        return true;
    }
    return false;
}

bool SessionUiUnit::RenderPetItemInfo(int sx, int sy, ITEM *pItem, int iInvenType)
{
    PET_INFO *pPetInfo = GetPetInfo(pItem);

    if (pPetInfo->m_dwPetType == PET_TYPE_NONE)
    {
        return false;
    }

    int TextNum = 0;
    int SkipNum = 0;
    int RequireLevel = 0;
    int RequireCharisma = 0;
    const std::wstring priceFormat =
        PetManagerDetail::SanitizeWideStringFormat(I18N::Game::SellingPriceS);
    const std::wstring ownershipFormat =
        PetManagerDetail::SanitizeWideStringFormat(I18N::Game::CanBeEquippedByS);

    auto appendLine = [&](int color, bool bold, bool countForHeight, const wchar_t *format,
                          auto... args) {
        if (TextNum >= PetManagerDetail::kTooltipLineLimit)
        {
            return;
        }

        TextListColor[TextNum] = color;
        TextBold[TextNum] = bold;
        std::swprintf(TextList[TextNum], PetManagerDetail::kTooltipBufferCapacity, format, args...);
        ++TextNum;

        if (countForHeight)
        {
            ++SkipNum;
        }
    };

    auto appendEmptyLine = [&]() { appendLine(TEXT_COLOR_WHITE, false, true, L"\n"); };

    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_NPCSHOP))
    {
        wchar_t textBuffer[PetManagerDetail::kTooltipBufferCapacity]{};
        std::uint32_t gold = GetPetItemValue(&gs_PetInfo) / 3u;
        gold = (gold / 100u) * 100u;

        ConvertGold(gold, textBuffer);
        appendLine(TEXT_COLOR_WHITE, true, false, priceFormat.c_str(), textBuffer);
        appendEmptyLine();
    }
    else if ((iInvenType == SEASON3B::TOOLTIP_TYPE_MY_SHOP) ||
             (iInvenType == SEASON3B::TOOLTIP_TYPE_PURCHASE_SHOP))
    {
        int price = 0;
        const int indexInv = g_pMyShopInventory->GetInventoryCtrl()->GetIndexByItem(pItem);
        wchar_t textBuffer[PetManagerDetail::kTooltipBufferCapacity]{};

        if (GetPersonalItemPrice(indexInv, price, g_IsPurchaseShop))
        {
            ConvertGold(price, textBuffer);

            int priceColor = TEXT_COLOR_WHITE;
            if (price >= 10000000)
            {
                priceColor = TEXT_COLOR_RED;
            }
            else if (price >= 1000000)
            {
                priceColor = TEXT_COLOR_YELLOW;
            }
            else if (price >= 100000)
            {
                priceColor = TEXT_COLOR_GREEN;
            }

            appendLine(priceColor, true, false, priceFormat.c_str(), textBuffer);
            appendEmptyLine();

            const auto heroGold = CharacterMachine->Gold;
            if ((static_cast<std::int64_t>(heroGold) < static_cast<std::int64_t>(price)) &&
                (g_IsPurchaseShop == PSHOPWNDTYPE_PURCHASE))
            {
                appendLine(TEXT_COLOR_RED, true, false, I18N::Game::YouAreShortOfZen);
                appendEmptyLine();
            }
        }
        else if (g_IsPurchaseShop == PSHOPWNDTYPE_SALE)
        {
            appendLine(TEXT_COLOR_RED, true, false, I18N::Game::RightClickForPriceSetting);
            appendEmptyLine();
        }
    }

    if (pItem->Type == ITEM_DARK_HORSE_ITEM)
    {
        RequireLevel = (218 + (pPetInfo->m_wLevel * 2));
        appendLine(TEXT_COLOR_BLUE, true, true, I18N::Game::DarkHorse);
    }
    else if (pItem->Type == ITEM_DARK_RAVEN_ITEM)
    {
        RequireCharisma = (185 + (pPetInfo->m_wLevel * 15));
        appendLine(TEXT_COLOR_BLUE, true, true, I18N::Game::DarkRaven);
    }

    appendEmptyLine();
    appendEmptyLine();

    appendLine(TEXT_COLOR_WHITE, false, true, I18N::Game::ExpUU, pPetInfo->m_dwExp1,
               pPetInfo->m_dwExp2);
    appendLine(TEXT_COLOR_WHITE, false, true, L"%ls : %d", I18N::Game::Level, pPetInfo->m_wLevel);

    if (pItem->Type == ITEM_DARK_RAVEN_ITEM)
    {
        appendLine(TEXT_COLOR_WHITE, false, true, I18N::Game::DmgRateDDD, pPetInfo->m_wDamageMin,
                   pPetInfo->m_wDamageMax, pPetInfo->m_wAttackSuccess);
        appendLine(TEXT_COLOR_WHITE, false, true, I18N::Game::AttackSpeedD,
                   pPetInfo->m_wAttackSpeed);
    }
    appendLine(TEXT_COLOR_WHITE, false, true, I18N::Game::LifeD, pPetInfo->m_wLife);

    if (pItem->Type == ITEM_DARK_HORSE_ITEM)
    {
        const bool hasLevelRequirement = CharacterAttribute->Level >= RequireLevel;
        const int requirementColor = hasLevelRequirement ? TEXT_COLOR_WHITE : TEXT_COLOR_RED;
        appendLine(requirementColor, false, false, I18N::Game::MinimumLevelRequirementD,
                   RequireLevel);

        if (!hasLevelRequirement)
        {
            appendLine(TEXT_COLOR_RED, false, false, I18N::Game::LackingD,
                       RequireLevel - CharacterAttribute->Level);
        }
    }
    else if (pItem->Type == ITEM_DARK_RAVEN_ITEM)
    {
        const int charismaTotal = CharacterAttribute->Charisma + CharacterAttribute->AddCharisma;
        const bool hasCharisma = charismaTotal >= RequireCharisma;
        const int charismaColor = hasCharisma ? TEXT_COLOR_WHITE : TEXT_COLOR_RED;

        appendLine(charismaColor, false, false, I18N::Game::CharismaRequirementD, RequireCharisma);

        if (!hasCharisma)
        {
            appendLine(TEXT_COLOR_RED, false, false, I18N::Game::LackingD,
                       RequireCharisma - charismaTotal);
        }
    }

    appendEmptyLine();

    const int ownershipColor = (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK_LORD)
                                   ? TEXT_COLOR_WHITE
                                   : TEXT_COLOR_DARKRED;
    appendLine(ownershipColor, false, true, ownershipFormat.c_str(), I18N::Game::DarkLord);

    FormatPetSpecialOptions(*pItem, gs_PetInfo, TextNum, SkipNum);

    if (pItem->Type == ITEM_DARK_HORSE_ITEM)
    {
        appendLine(TEXT_COLOR_BLUE, false, true, I18N::Game::AbsorbDAdditionalDamage,
                   (30 + pPetInfo->m_wLevel) / 2);
        appendLine(TEXT_COLOR_BLUE, false, false, I18N::Game::IncreaseDPossibleAttackDistance, 2);
    }

    SIZE TextSize = {0, 0};
    g_RenderText.MeasureText(TextList[0], 1, &TextSize);
    int Height = static_cast<int>(((TextNum - SkipNum) * TextSize.cy + SkipNum * TextSize.cy / 2) /
                                  g_fScreenRate_y);
    if (sy - Height >= 0)
    {
        sy -= Height;
    }

    RenderTipTextList(sx, sy, TextNum, 0);
    return true;
}

// OMF-00827
// OMF-00834

void SEASON3B::CNewUIMainFrameWindow::SetButtonInfo()
{
    m_BtnCShop.ChangeButtonImgState(true, IMAGE_MENU_BTN_CSHOP, true);
    m_BtnCShop.ChangeToolTipText(&I18N::Game::MUItemShopX, true);

    m_BtnChaInfo.ChangeButtonImgState(true, IMAGE_MENU_BTN_CHAINFO, true);
    m_BtnChaInfo.ChangeToolTipText(&I18N::Game::CharacterC, true);

    m_BtnMyInven.ChangeButtonImgState(true, IMAGE_MENU_BTN_MYINVEN, true);
    m_BtnMyInven.ChangeToolTipText(&I18N::Game::InventoryIV, true);

    m_BtnQuest.ChangeButtonImgState(true, IMAGE_MENU_BTN_FRIEND, true);

    m_BtnFriend.ChangeButtonImgState(true, IMAGE_MENU_BTN_FRIEND, true);
    m_BtnFriend.ChangeToolTipText(&I18N::Game::FriendF, true);

    m_BtnWindow.ChangeButtonImgState(true, IMAGE_MENU_BTN_WINDOW, true);
    m_BtnWindow.ChangeToolTipText(&I18N::Game::MenuU, true);

    UpdateButtonGeometry();
}

bool SEASON3B::CNewUIMainFrameWindow::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    if (!renderer_.RecordMainFrame())
    {
        return false;
    }

    m_pNewUI3DRenderMng->RenderUI2DEffect(ITEMHOTKEYNUMBER_CAMERA_Z_ORDER, UI2DEffectCallback, this,
                                          0, 0);

    DisableAlphaBlend();

    return true;
}

void SEASON3B::CNewUIMainFrameWindow::Render3D()
{
    m_ItemHotKey.RenderItems();
}

void SEASON3B::CNewUIMainFrameWindow::RenderHotKeyItemCount()
{
    m_ItemHotKey.RenderItemCount();
    m_ItemHotKey.RenderItemHotKeyTooltip();
}

void SEASON3B::CNewUIMainFrameWindow::StageMainFrame()
{
    UI::Modern::RmlMainFrameRequest request;
    request.visible = IsVisible() && Hero != nullptr && CharacterAttribute != nullptr;
    if (!request.visible)
    {
        renderer_.StageMainFrame(request);
        return;
    }

    const bool master = gCharacterManager.IsMasterLevel(Hero->Class);
    request.maximumLife =
        std::max<int>(1, master ? Master_Level_Data.wMaxLife : CharacterAttribute->LifeMax);
    request.life = std::clamp<int>(CharacterAttribute->Life, 0, request.maximumLife);
    request.maximumMana =
        std::max<int>(1, master ? Master_Level_Data.wMaxMana : CharacterAttribute->ManaMax);
    request.mana = std::clamp<int>(CharacterAttribute->Mana, 0, request.maximumMana);
    request.maximumShield =
        std::max<int>(1, master ? Master_Level_Data.wMaxShield : CharacterAttribute->ShieldMax);
    request.shield = std::clamp<int>(CharacterAttribute->Shield, 0, request.maximumShield);
    request.maximumAbility =
        std::max<int>(1, master ? Master_Level_Data.wMaxBP : CharacterAttribute->SkillManaMax);
    request.ability = std::clamp<int>(CharacterAttribute->SkillMana, 0, request.maximumAbility);
    request.poisoned = g_isCharacterBuff((&Hero->Object), eDeBuff_Poison);
    request.skillSelectionVisible = CharacterAttribute->SkillNumber > 0;
    request.selectedHotSkillSlot = g_pSkillList->GetSelectedMainFrameHotSlot();
    request.skillSecondPage = g_pSkillList->IsSkillListUp();
    g_pSkillList->FillMainFrameSkills(request);
    FillExperienceRequest(request);

    const bool disabled = g_pNewUIHotKey->IsStateGameOver();
    for (int index = 0; index < UI::Modern::RmlMainFrameRequest::ButtonCount; ++index)
    {
        if (disabled)
        {
            request.buttons[index] = ButtonVisualState::Disabled;
            continue;
        }
        const auto rect = MainFrameDetail::RoundedMainFrameReferenceRect(
            ModernUiViewportWidth(), ModernUiViewportHeight(), ModernUiScale(),
            MainFrameDetail::MainFrameDesign().Values(
                MainFrameDetail::MainFrameDesignKey::MainFrameButtonX)[index],
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameButtonY),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameButtonWidth),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameButtonHeight));
        if (!CheckMouseIn(rect.x, rect.y, rect.width, rect.height))
        {
            request.buttons[index] = ButtonVisualState::Up;
            continue;
        }
        request.buttons[index] = MouseLButton ? ButtonVisualState::Down : ButtonVisualState::Over;
    }
    if (disabled)
    {
        request.skillPageButton = ButtonVisualState::Disabled;
    }
    else
    {
        const auto rect = MainFrameDetail::MainFrameReferenceRect(
            ModernUiViewportWidth(), ModernUiViewportHeight(), ModernUiScale(),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameSkillPageButtonX),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameSkillPageButtonY),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameSkillPageButtonWidth),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameSkillPageButtonHeight));
        if (CheckMouseIn(rect.x, rect.y, rect.width, rect.height))
        {
            request.skillPageButton =
                MouseLButton ? ButtonVisualState::Down : ButtonVisualState::Over;
        }
    }
    renderer_.StageMainFrame(request);
}

void SEASON3B::CNewUIItemHotKey::RenderItems()
{
    for (int i = 0; i < HOTKEY_COUNT; ++i)
    {
        int iIndex = GetHotKeyItemIndex(i);
        if (iIndex != -1)
        {
            ITEM *pItem = sessionKeeper_.GameData()->FindInventoryItemBySlot(iIndex);
            if (pItem)
            {
                const auto rect = MainFrameDetail::MainFrameReferenceRect(
                    ModernUiViewportWidth(), ModernUiViewportHeight(), ModernUiScale(),
                    MainFrameDetail::MainFrameDesign().Number<float>(
                        MainFrameDetail::MainFrameDesignKey::MainFrameItemFirstX) +
                        i * MainFrameDetail::MainFrameDesign().Number<float>(
                                MainFrameDetail::MainFrameDesignKey::MainFrameItemStep),
                    MainFrameDetail::MainFrameDesign().Number<float>(
                        MainFrameDetail::MainFrameDesignKey::MainFrameItemY),
                    MainFrameDetail::MainFrameDesign().Number<float>(
                        MainFrameDetail::MainFrameDesignKey::MainFrameItemWidth),
                    MainFrameDetail::MainFrameDesign().Number<float>(
                        MainFrameDetail::MainFrameDesignKey::MainFrameItemHeight));
                RenderItem3D(rect.x, rect.y, rect.width, rect.height, pItem->Type, pItem->Level, 0,
                             0);
            }
        }
    }
}

void SEASON3B::CNewUIItemHotKey::RenderItemHotKeyTooltip()
{
    if (m_iTooltipHotKey < 0)
    {
        return;
    }

    const int itemIndex = GetHotKeyItemIndex(m_iTooltipHotKey);
    ITEM *const item =
        itemIndex == -1 ? nullptr : sessionKeeper_.GameData()->FindInventoryItemBySlot(itemIndex);
    if (item == nullptr)
    {
        return;
    }

    const auto rect = MainFrameDetail::MainFrameReferenceRect(
        ModernUiViewportWidth(), ModernUiViewportHeight(), ModernUiScale(),
        MainFrameDetail::MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameItemFirstX) +
            m_iTooltipHotKey * MainFrameDetail::MainFrameDesign().Number<float>(
                                   MainFrameDetail::MainFrameDesignKey::MainFrameItemStep),
        MainFrameDetail::MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameItemY),
        MainFrameDetail::MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameItemWidth),
        MainFrameDetail::MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameItemHeight));
    const int tooltipX = static_cast<int>(std::lround(rect.x + rect.width * 0.5F));
    const int tooltipY = static_cast<int>(std::lround(rect.y));
    RenderItemInfo(tooltipX, tooltipY, item, false, 0, true);
}

void SEASON3B::CNewUIItemHotKey::RenderItemCount()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 1.f);

    for (int i = 0; i < HOTKEY_COUNT; ++i)
    {
        int iCount = GetHotKeyItemIndex(i, true);
        if (iCount > 0)
        {
            int digitCount = 1;
            for (int value = iCount;
                 value >= 10 &&
                 digitCount <
                     MainFrameDetail::MainFrameDesign().Number<int>(
                         MainFrameDetail::MainFrameDesignKey::MainFrameItemCountDigitLimit);
                 value /= 10)
            {
                ++digitCount;
            }

            int divisor = 1;
            for (int digit = 1; digit < digitCount; ++digit)
            {
                divisor *= 10;
            }

            const int displayValue = std::min(iCount, 9999);
            for (int digit = 0; digit < digitCount; ++digit)
            {
                const int value = (displayValue / divisor) % 10;
                const float digitX =
                    MainFrameDetail::MainFrameDesign().Number<float>(
                        MainFrameDetail::MainFrameDesignKey::MainFrameItemFirstX) +
                    i * MainFrameDetail::MainFrameDesign().Number<float>(
                            MainFrameDetail::MainFrameDesignKey::MainFrameItemStep) +
                    MainFrameDetail::MainFrameDesign().Number<float>(
                        MainFrameDetail::MainFrameDesignKey::MainFrameItemCountImageX) +
                    (MainFrameDetail::MainFrameDesign().Number<int>(
                         MainFrameDetail::MainFrameDesignKey::MainFrameItemCountDigitLimit) -
                     digitCount + digit) *
                        MainFrameDetail::MainFrameDesign().Number<float>(
                            MainFrameDetail::MainFrameDesignKey::MainFrameItemCountDigitStep) *
                        MainFrameDetail::MainFrameDesign().Number<float>(
                            MainFrameDetail::MainFrameDesignKey::MainFrameItemCountScaleX);
                const auto rect = MainFrameDetail::MainFrameReferenceRect(
                    ModernUiViewportWidth(), ModernUiViewportHeight(), ModernUiScale(), digitX,
                    MainFrameDetail::MainFrameDesign().Number<float>(
                        MainFrameDetail::MainFrameDesignKey::MainFrameItemY) +
                        MainFrameDetail::MainFrameDesign().Number<float>(
                            MainFrameDetail::MainFrameDesignKey::MainFrameItemCountImageY),
                    MainFrameDetail::MainFrameDesign().Number<float>(
                        MainFrameDetail::MainFrameDesignKey::MainFrameItemCountDigitWidth) *
                        MainFrameDetail::MainFrameDesign().Number<float>(
                            MainFrameDetail::MainFrameDesignKey::MainFrameItemCountScaleX),
                    MainFrameDetail::MainFrameDesign().Number<float>(
                        MainFrameDetail::MainFrameDesignKey::MainFrameItemCountDigitHeight) *
                        MainFrameDetail::MainFrameDesign().Number<float>(
                            MainFrameDetail::MainFrameDesignKey::MainFrameItemCountScaleY));
                RenderBitmap(
                    BITMAP_INTERFACE_NEW_NUMBER_BEGIN, rect.x, rect.y, rect.width, rect.height,
                    static_cast<float>(value) *
                        MainFrameDetail::MainFrameDesign().Number(
                            MainFrameDetail::MainFrameDesignKey::MainFrameItemCountDigitWidth) /
                        MainFrameDetail::MainFrameDesign().Number(
                            MainFrameDetail::MainFrameDesignKey::NumberAtlasWidth),
                    0.0F,
                    MainFrameDetail::MainFrameDesign().Number(
                        MainFrameDetail::MainFrameDesignKey::MainFrameItemCountDigitWidth) /
                        MainFrameDetail::MainFrameDesign().Number(
                            MainFrameDetail::MainFrameDesignKey::NumberAtlasWidth),
                    MainFrameDetail::MainFrameDesign().Number(
                        MainFrameDetail::MainFrameDesignKey::MainFrameItemCountDigitHeight) /
                        MainFrameDetail::MainFrameDesign().Number(
                            MainFrameDetail::MainFrameDesignKey::NumberAtlasHeight));
                divisor = std::max(1, divisor / 10);
            }
        }
    }
}

void SEASON3B::CNewUISkillList::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_skill.jpg", IMAGE_SKILL1, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_skill2.jpg", IMAGE_SKILL2, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_command.jpg", IMAGE_COMMAND, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_skillbox.jpg", IMAGE_SKILLBOX, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_skillbox2.jpg", IMAGE_SKILLBOX_USE, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_non_skill.jpg", IMAGE_NON_SKILL1, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_non_skill2.jpg", IMAGE_NON_SKILL2, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_non_command.jpg", IMAGE_NON_COMMAND,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_skill3.jpg", IMAGE_SKILL3, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_non_skill3.jpg", IMAGE_NON_SKILL3, LegacyTextureFilter::Linear);
}

void SEASON3B::CNewUISkillList::UnloadImages()
{
    DeleteBitmap(IMAGE_SKILL1);
    DeleteBitmap(IMAGE_SKILL2);
    DeleteBitmap(IMAGE_COMMAND);
    DeleteBitmap(IMAGE_SKILLBOX);
    DeleteBitmap(IMAGE_SKILLBOX_USE);
    DeleteBitmap(IMAGE_NON_SKILL1);
    DeleteBitmap(IMAGE_NON_SKILL2);
    DeleteBitmap(IMAGE_NON_COMMAND);
    DeleteBitmap(IMAGE_SKILL3);
    DeleteBitmap(IMAGE_NON_SKILL3);
}

bool SEASON3B::CNewUISkillList::PrepareModernUiOnWorker(int width, int height)
{
    skillEntries_.clear();
    const bool visible = IsVisible() && m_bSkillList && CharacterAttribute->SkillNumber > 0;
    if (visible)
    {
        const auto geometry =
            MainFrameDetail::CalculateSkillListGeometry(width, height, ModernUiScale());
        auto append = [&](int index, float x, float y) {
            UI::Modern::RmlMainFrameRequest visual;
            FillMainFrameSkill(visual, 0, index);
            UI::Modern::RmlSkillListLayer::Entry entry;
            entry.icon = visual.skillIcons[0];
            entry.cooldown = visual.skillCooldowns[0];
            entry.skillIndex = index;
            entry.x = x + geometry.iconOffsetX;
            entry.y = y + geometry.iconOffsetY;
            entry.width = geometry.iconWidth;
            entry.height = geometry.iconHeight;
            for (int key = 0; key < SKILLHOTKEY_COUNT; ++key)
                if (m_iHotKeySkillType[key] == index)
                {
                    entry.hotKey = key;
                    break;
                }
            skillEntries_.push_back(entry);
        };
        for (int index = 0; index < MAX_MAGIC; ++index)
        {
            const auto type = CharacterAttribute->Skill[index];
            if (!type || (type >= AT_SKILL_STUN && type <= AT_SKILL_REMOVAL_BUFF))
                continue;
            const auto use = SkillAttribute[type].SkillUseType;
            if (use == SKILL_USE_TYPE_MASTER || use == SKILL_USE_TYPE_MASTERLEVEL)
                continue;
            const auto slot = MainFrameDetail::CalculateSkillListSlot(
                geometry, static_cast<int>(skillEntries_.size()));
            append(index, slot.x, slot.y);
        }
        if (Hero->PetCommands.present)
            for (int index = AT_PET_COMMAND_DEFAULT; index < AT_PET_COMMAND_END; ++index)
                append(index,
                       geometry.petOriginX + (index - AT_PET_COMMAND_DEFAULT) * geometry.slotWidth,
                       geometry.petOriginY);
    }
    return skillImages_.PrepareOnWorker(width, height, visible, skillEntries_);
}

bool SEASON3B::CNewUISkillList::Render()
{
    if (m_bSkillList && CharacterAttribute->SkillNumber > 0)
    {
        const auto geometry = MainFrameDetail::CalculateSkillListGeometry(
            ModernUiViewportWidth(), ModernUiViewportHeight(), ModernUiScale());
        for (const auto &entry : skillEntries_)
            RenderImage(entry.skillIndex == Hero->CurrentSkill ? IMAGE_SKILLBOX_USE
                                                               : IMAGE_SKILLBOX,
                        entry.x - geometry.iconOffsetX, entry.y - geometry.iconOffsetY,
                        geometry.slotWidth, geometry.slotHeight);
        if (!skillImages_.Record(skillRenderer_.LegacyRender()))
            return false;
        for (const auto &entry : skillEntries_)
            if (entry.hotKey >= 0)
                RenderHotKeyNumber(entry.hotKey, entry.x, entry.y, entry.width, entry.height);
    }

    if (m_bRenderSkillInfo == true && m_pNewUI3DRenderMng)
    {
        m_pNewUI3DRenderMng->RenderUI2DEffect(ITEMHOTKEYNUMBER_CAMERA_Z_ORDER, UI2DEffectCallback,
                                              this, 0, 0);

        m_bRenderSkillInfo = false;
    }

    return true;
}

void SEASON3B::CNewUISkillList::RenderSkillInfo()
{
    m_SkillTooltip.Render(m_iRenderSkillInfoPosX, m_iRenderSkillInfoPosY, m_iRenderSkillInfoType);
}

void SEASON3B::CNewUISkillList::RenderHotKeyNumber(int hotKey, float x, float y, float width,
                                                   float height)
{
    const auto &design = MainFrameDetail::MainFrameDesign();
    const auto color = design.Values(MainFrameDetail::MainFrameDesignKey::HotKeyNumberColor);
    glColor3f(color[0], color[1], color[2]);
    const float numberScale =
        design.Number(MainFrameDetail::MainFrameDesignKey::HotKeyNumberBaseScale) +
        design.Number(MainFrameDetail::MainFrameDesignKey::HotKeyNumberWidthScale) * width /
            design.Number(MainFrameDetail::MainFrameDesignKey::SkillIconSourceWidth);
    RenderNumber(x + width,
                 y + height *
                         design.Number(MainFrameDetail::MainFrameDesignKey::HotKeyNumberOffsetY) /
                         design.Number(MainFrameDetail::MainFrameDesignKey::SkillIconSourceHeight),
                 hotKey, numberScale);
    glColor3f(1.f, 1.f, 1.f);
}

void CNewUIMoveCommandWindow::SetPos(int x, int y)
{
    (void)x;
    (void)y;
    const auto &source = g_MoveCommandData.GetMoveCommandDatalist();
    moveEntries_.assign(source.begin(), source.end());
    scrollOffset_ = std::min(scrollOffset_, MaximumScrollOffset());
}

bool CNewUIMoveCommandWindow::Render()
{
    return renderer_.RecordMoveCommand();
}

void CNewUIMoveCommandWindow::StageMoveCommand()
{
    UI::Modern::RmlMoveCommandRequest request;
    request.visible = IsVisible();
    if (!request.visible)
    {
        renderer_.StageMoveCommand(request);
        return;
    }

    MoveCommandDetail::CopyText(request.title, I18N::Game::WarpCommandWindow);
    MoveCommandDetail::CopyText(request.strifeLabel, I18N::Game::BattleZone);
    MoveCommandDetail::CopyText(request.mapLabel, I18N::Game::Map);
    MoveCommandDetail::CopyText(request.levelLabel, I18N::Game::MinLevel);
    MoveCommandDetail::CopyText(request.zenLabel, I18N::Game::Cost);
    MoveCommandDetail::CopyText(request.favoriteLabel, MoveCommandDetail::FavoriteLabel);
    MoveCommandDetail::CopyText(request.showMapLabel, MoveCommandDetail::ShowMapLabel);
    MoveCommandDetail::CopyText(request.closeLabel, I18N::Game::Close388);

    const bool mouseDown = IsRepeat(VK_LBUTTON);
    for (std::size_t index = 0; index < request.rows.size(); ++index)
    {
        const std::size_t mapIndex = scrollOffset_ + index;
        CMoveCommandData::MOVEINFODATA *const moveInfo = MoveAt(mapIndex);
        if (moveInfo == nullptr)
        {
            break;
        }

        auto &row = request.rows[index];
        row.visible = true;
        row.disabled = !moveInfo->_bCanMove;
        row.over = hoveredMainRow_ == static_cast<int>(index);
        row.down = row.over && mouseDown;
        row.selected = IsFavorite(mapIndex);
        if (moveInfo->_bStrife)
        {
            MoveCommandDetail::CopyText(row.strife, I18N::Game::Battle2987);
        }
        MoveCommandDetail::CopyText(row.mapName, moveInfo->_ReqInfo.szMainMapName);
        MoveCommandDetail::CopyNumber(row.requiredLevel, AdjustedRequiredLevel(*moveInfo));
        MoveCommandDetail::CopyNumber(row.requiredZen, moveInfo->_ReqInfo.iReqZen);
    }

    for (std::size_t index = 0; index < request.favorites.size(); ++index)
    {
        if (favoriteIndices_[index] < 0)
        {
            break;
        }

        CMoveCommandData::MOVEINFODATA *const moveInfo =
            MoveAt(static_cast<std::size_t>(favoriteIndices_[index]));
        if (moveInfo == nullptr)
        {
            continue;
        }

        auto &row = request.favorites[index];
        row.visible = true;
        row.disabled = !moveInfo->_bCanMove;
        row.over = hoveredFavoriteRow_ == static_cast<int>(index);
        row.down = row.over && mouseDown;
        row.selected = true;
        if (moveInfo->_bStrife)
        {
            MoveCommandDetail::CopyText(row.strife, I18N::Game::Battle2987);
        }
        MoveCommandDetail::CopyText(row.mapName, moveInfo->_ReqInfo.szMainMapName);
        MoveCommandDetail::CopyNumber(row.requiredLevel, AdjustedRequiredLevel(*moveInfo));
        MoveCommandDetail::CopyNumber(row.requiredZen, moveInfo->_ReqInfo.iReqZen);
    }

    const std::size_t maximumOffset = MaximumScrollOffset();
    request.scrollBar.position = scrollOffset_;
    request.scrollBar.maximum = maximumOffset;
    request.scrollBar.pageSize = UI::Modern::RmlMoveCommandVisibleRows;
    request.scrollBar.up = scrollOffset_ == 0 ? ButtonVisualState::Disabled
                                              : ToButtonState(scrollUpButton_.GetBTState());
    request.scrollBar.down = scrollOffset_ == maximumOffset
                                 ? ButtonVisualState::Disabled
                                 : ToButtonState(scrollDownButton_.GetBTState());
    if (maximumOffset == 0)
    {
        request.scrollBar.thumb = ButtonVisualState::Disabled;
    }
    else if (thumbDragging_)
    {
        request.scrollBar.thumb = ButtonVisualState::Down;
    }
    else
    {
        const auto thumb = MoveCommandDetail::MoveThumbRect(scrollOffset_, maximumOffset);
        const auto thumbRect = ReferenceRect(thumb.x, thumb.y, thumb.width, thumb.height);
        request.scrollBar.thumb =
            MouseIn(thumbRect) ? ButtonVisualState::Over : ButtonVisualState::Up;
    }
    request.showMap = ToButtonState(showMapButton_.GetBTState());
    request.close = ToButtonState(closeButton_.GetBTState());
    renderer_.StageMoveCommand(request);
}

bool CNewUIMuHelper::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight)
{
    if (!IsVisible())
    {
        return m_modernPanel.PrepareOnWorker(viewportWidth, viewportHeight, false, {});
    }
    return m_modernPanel.PrepareOnWorker(viewportWidth, viewportHeight, true, ModernContent());
}
bool CNewUIMuHelper::Render()
{
    const bool recorded = m_modernPanel.Record(LegacyRender());
    return recorded;
}

bool CNewUIMuHelperSkillList::Render()
{
    return true;
}

void CNewUIGensRanking::RanderMark(float x, float y, GENS_TYPE gensType, BYTE rankIndex,
                                   IMAGE_AREA imageArea, float yOffset)
{
    if (gensType == GENSTYPE_NONE)
    {
        return;
    }

    int imageType = (gensType == GENSTYPE_DUPRIAN) ? IMAGE_NEWMARK_DUPRIAN : IMAGE_NEWMARK_BARNERT;

    float _width = GENSMARK_WIDTH;
    float _height = GENSMARK_HEIGHT;
    const auto texture = Bitmaps.GetTextureProperties(imageType);
    if (!texture)
    {
        return;
    }
    float imageWidth = texture->width;
    float imageHeight = texture->height;

    if (imageArea == MARK_BOOLEAN)
    {
        _width = GENSMARK_WIDTH * m_fBooleanSize;
        _height = GENSMARK_HEIGHT * m_fBooleanSize;
        imageWidth = texture->width * m_fBooleanSize;
        imageHeight = texture->height * m_fBooleanSize;
        y = (yOffset - y - _height) / 2 + y;
        x = (float)(x - _width + 1);
    }

    int imageIndex = GetImageIndex(rankIndex);
    float columnIndex = imageIndex % 5;
    float rowIndex = imageIndex / 5;
    float u = columnIndex * _width / imageWidth;
    float v = rowIndex * _height / imageHeight;

    RenderBitmap(imageType, x, y, _width, _height, u, v, _width / imageWidth,
                 _height / imageHeight);
}

void SEASON3B::CNewUIMainFrameWindow::UI2DEffectCallback(LPVOID pClass, DWORD dwParamA,
                                                         DWORD dwParamB)
{
    if (pClass)
    {
        static_cast<CNewUIMainFrameWindow *>(pClass)->RenderHotKeyItemCount();
    }
}

void SEASON3B::CNewUISkillList::UI2DEffectCallback(LPVOID pClass, DWORD dwParamA, DWORD dwParamB)
{
    if (pClass)
    {
        auto *pSkillList = (CNewUISkillList *)(pClass);
        pSkillList->RenderSkillInfo();
    }
}
