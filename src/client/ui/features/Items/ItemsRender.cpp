

#include "ui/features/Items/ItemsRender.h"
#include "I18N/All.h"
#include "app/AppWindow.h"
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
#include "session/SessionRender.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"
#include "ui/session/UiSessionRender.h"

namespace UI::Modern::PC::Inventory
{
class RmlDurabilityLayer::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "Inventory", "item_durability.rml")),
          design_(path_, {"Hud-Size", "Hud-Reference", "Hud-Resize"}),
          host_(keeper, "item-durability-" + std::to_string(keeper.Id().RawValue()), path_)
    {
    }
    ~Impl()
    {
        Release();
    }
    void Release()
    {
        for (auto &cell : cells_)
        {
            cell.button.Unbind();
            cell.element = cell.icon = cell.right = nullptr;
            cell.equipment = -1;
        }
        root_ = ammunition_ = nullptr;
        previous_.reset();
        position_.reset();
        visible_ = dirty_ = false;
        hovered_ = -1;
        physicalWidth_ = 0;
        host_.Release();
    }
    bool Bind()
    {
        auto &document = *host_.Document();
        root_ = document.GetElementById("durability");
        ammunition_ = document.GetElementById("ammunition");
        if (!root_ || !ammunition_)
            return false;
        for (std::size_t i = 0; i < cells_.size(); ++i)
        {
            auto &cell = cells_[i];
            cell.element = document.GetElementById("btItem_" + std::to_string(i));
            if (!cell.element || cell.element->GetNumChildren() != 2)
                return false;
            cell.icon = cell.element->GetChild(0);
            cell.right = cell.element->GetChild(1);
            cell.button.Bind(*cell.element);
        }
        return true;
    }
    void SetCell(std::size_t index, int equipment, const Warning &warning, int rightState = 0)
    {
        auto &cell = cells_[index];
        cell.equipment = equipment;
        cell.element->SetClassNames("mu-button DurabilityButton type-" +
                                    std::to_string(warning.type));
        cell.icon->SetClassNames("icon state-" + std::to_string(warning.state));
        cell.right->SetClassNames("ring-right state-" + std::to_string(rightState));
        cell.button.SetVisible(true);
        cell.button.SyncVisualState();
    }
    void Apply(const Content &content)
    {
        root_->SetProperty("display", content.warningsVisible ? "block" : "none");
        std::size_t count = 0;
        constexpr int Helper = 8, RightRing = 10, LeftRing = 11;
        for (int i = 0; i < RightRing; ++i)
        {
            if (i != Helper && content.equipment[i].state)
                SetCell(count++, i, content.equipment[i]);
        }
        if (content.equipment[RightRing].state || content.equipment[LeftRing].state)
            SetCell(count++, RightRing, {RightRing, content.equipment[LeftRing].state},
                    content.equipment[RightRing].state);
        for (std::size_t i = count; i < cells_.size(); ++i)
        {
            cells_[i].equipment = -1;
            cells_[i].button.SetVisible(false);
            cells_[i].button.SyncVisualState();
        }
        ammunition_->SetInnerRML(
            Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(content.ammunition.c_str())));
        ammunition_->SetProperty("display", content.ammunition.empty() ? "none" : "block");
        previous_ = content;
        hovered_ = -1;
    }
    bool Prepare(int width, int height, bool visible, const Content &content)
    {
        if (!root_ && !visible)
            return true;
        const auto size = design_.Values(0), resize = design_.Values(2);
        const float multiplier = height < resize[0] ? resize[1] : 1.0f;
        if (!host_.Ensure(width, height, size[0], size[1], multiplier))
            return false;
        if (!root_ && !Bind())
        {
            Release();
            return false;
        }
        if (!host_.SetVisible(visible))
            return false;
        physicalWidth_ = width;
        visible_ = visible;
        if (visible && previous_ != content)
        {
            Apply(content);
            dirty_ = true;
        }
        const auto viewport = host_.Viewport();
        const Rml::Vector2f position(viewport.width - size[0], (viewport.height - size[1]) / 2);
        if (position_ != position)
        {
            root_->SetProperty(Rml::PropertyId::Left, Rml::Property(position.x, Rml::Unit::PX));
            root_->SetProperty(Rml::PropertyId::Top, Rml::Property(position.y, Rml::Unit::PX));
            position_ = position;
            dirty_ = true;
        }
        if (!visible)
            hovered_ = -1;
        if (!host_.CaptureIfDirty(dirty_))
            return false;
        dirty_ = false;
        return true;
    }
    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!visible_ || event.kind != SessionInputEventKind::Pointer)
            return false;
        host_.ProcessInput(event);
        hovered_ = -1;
        for (auto &cell : cells_)
        {
            if (cell.equipment < 0 || !cell.button.OwnsPointer(host_.HoverElement()))
                continue;
            hovered_ = cell.equipment;
            if (hovered_ == 10)
            {
                const auto x = float(event.x) * host_.Viewport().width / physicalWidth_ -
                               cell.element->GetAbsoluteOffset().x;
                hovered_ = x < cell.right->GetBox().GetSize().x ? 10 : 11;
            }
            break;
        }
        dirty_ = true;
        return event.kind == SessionInputEventKind::Pointer && hovered_ >= 0;
    }
    struct Cell
    {
        RmlMuButton button;
        Rml::Element *element = nullptr;
        Rml::Element *icon = nullptr;
        Rml::Element *right = nullptr;
        int equipment = -1;
    };
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    std::array<Cell, 10> cells_;
    Rml::Element *root_ = nullptr;
    Rml::Element *ammunition_ = nullptr;
    std::optional<Content> previous_;
    std::optional<Rml::Vector2f> position_;
    int hovered_ = -1;
    int physicalWidth_ = 0;
    bool visible_ = false, dirty_ = false;
};
RmlDurabilityLayer::RmlDurabilityLayer(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper))
{
}
RmlDurabilityLayer::~RmlDurabilityLayer() = default;
void RmlDurabilityLayer::Release()
{
    impl_->Release();
}
bool RmlDurabilityLayer::PrepareOnWorker(int width, int height, bool visible,
                                         const Content &content)
{
    return impl_->Prepare(width, height, visible, content);
}
bool RmlDurabilityLayer::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
int RmlDurabilityLayer::HoveredEquipment() const
{
    return impl_->hovered_;
}
bool RmlDurabilityLayer::Record(LegacyRenderFacade &facade) const
{
    return !impl_->root_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::Inventory

namespace UI::Modern::PC::Inventory
{
namespace
{
enum class InventoryExtensionPanelKey
{
    Width,
    Heights,
    GridX,
    GridY,
    PitchX,
    PitchY,
    Reference,
    Initial
};
const RmlUiDesign &InventoryExtensionPanelDesign()
{
    static const RmlUiDesign design("Data/UI/PC/Inventory/inventory_extension.rml",
                                    {"Extension-Width", "Extension-Heights", "Extension-GridX",
                                     "Extension-GridY", "Extension-PitchX", "Extension-PitchY",
                                     "Extension-Reference", "Extension-InitialPosition"});
    return design;
}
bool Descendant(const Rml::Element *element, const Rml::Element *root)
{
    for (; element; element = element->GetParentNode())
        if (element == root)
            return true;
    return false;
}
} // namespace

class RmlInventoryExtensionPanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : host_(
              keeper, "inventory-extension-" + std::to_string(keeper.Id().RawValue()),
              ResolveUiDocument("Data/UI", UiPlatform::Pc, "Inventory", "inventory_extension.rml"))
    {
    }
    ~Impl()
    {
        Release();
    }

    void Release()
    {
        movable_.Unbind();
        close_.Unbind();
        for (auto &slot : slots_)
            slot.Unbind();
        for (auto &page : pages_)
            page.clear();
        panel_ = title_ = nullptr;
        titleText_.clear();
        changes_ = {};
        bagCount_ = MAX_INVENTORY_EXT_COUNT + 1;
        inputDirty_ = positionSet_ = false;
        visible_ = false;
        left_ = top_ = scaleX_ = scaleY_ = panelHeight_ = 0;
        host_.Release();
        slotFramesDirty_ = true;
    }

    bool Bind()
    {
        auto *document = host_.Document();
        panel_ = document->GetElementById("extension");
        title_ = document->GetElementById("tfTitle");
        auto *drag = document->GetElementById("btnDrag");
        auto *close = document->GetElementById("btnClose");
        if (!panel_ || !title_ || !drag || !close)
            return false;
        for (std::size_t i = 0; i < slots_.size(); ++i)
        {
            auto *slot = document->GetElementById("isSlot" + std::to_string(i));
            if (!slot)
                return false;
            slots_[i].Bind(*slot);
        }
        for (std::size_t i = 0; i < pages_.size(); ++i)
            document->GetElementsByClassName(pages_[i], "bag-" + std::to_string(i));
        close_.Bind(*close);
        movable_.Bind(*panel_, *drag);
        return true;
    }

    bool Prepare(int width, int height, bool visible, std::size_t bags, const std::wstring &title)
    {
        if (!panel_ && !visible)
            return true;
        const auto initial =
            InventoryExtensionPanelDesign().Values(InventoryExtensionPanelKey::Initial);
        if (!host_.Ensure(
                width, height,
                initial[0] +
                    InventoryExtensionPanelDesign().Number(InventoryExtensionPanelKey::Width),
                initial[1] + InventoryExtensionPanelDesign().Values(
                                 InventoryExtensionPanelKey::Heights)[bags ? bags - 1 : 0]))
            return false;
        if (!panel_ && !Bind())
        {
            Release();
            return false;
        }
        if (!host_.SetVisible(visible))
            return false;
        bool dirty = visible_.load() != visible || inputDirty_;
        dirty = ApplyContent(bags, title) || dirty;
        if (slotFramesDirty_)
        {
            for (std::size_t i = 0; i < slots_.size(); ++i)
                slots_[i].SetIconState(slotFrames_[i]);
            slotFramesDirty_ = false;
            dirty = true;
        }
        const auto viewport = host_.Viewport();
        movable_.Configure(
            viewport.width, viewport.height,
            InventoryExtensionPanelDesign().Number(InventoryExtensionPanelKey::Width),
            InventoryExtensionPanelDesign().Values(
                InventoryExtensionPanelKey::Heights)[bags ? bags - 1 : 0]);
        if (!positionSet_)
        {
            movable_.SetPosition(initial[0], initial[1]);
            positionSet_ = true;
            dirty = true;
        }
        if (!visible)
            movable_.CancelDrag();
        dirty = movable_.TakeDirty() || dirty;
        dirty = close_.SyncVisualState() || dirty;
        if (inputDirty_)
            for (auto &slot : slots_)
                dirty = slot.SyncVisualState() || dirty;
        inputDirty_ = false;
        const auto reference =
            InventoryExtensionPanelDesign().Values(InventoryExtensionPanelKey::Reference);
        scaleX_ = reference[0] / viewport.width;
        scaleY_ = reference[1] / viewport.height;
        PublishPosition();
        visible_ = visible;
        return host_.CaptureIfDirty(dirty);
    }

    bool ApplyContent(std::size_t bags, const std::wstring &title)
    {
        bool dirty = false;
        if (bags != bagCount_)
        {
            for (std::size_t i = 0; i < pages_.size(); ++i)
            {
                for (auto *element : pages_[i])
                    element->SetProperty(Rml::PropertyId::Display,
                                         Rml::Property(i < bags ? Rml::Style::Display::Block
                                                                : Rml::Style::Display::None));
            }
            bagCount_ = bags;
            const float height = InventoryExtensionPanelDesign().Values(
                InventoryExtensionPanelKey::Heights)[bags ? bags - 1 : 0];
            panel_->SetProperty(Rml::PropertyId::Height, Rml::Property(height, Rml::Unit::PX));
            panelHeight_ = height;
            dirty = true;
        }
        if (titleText_ != title)
        {
            titleText_ = title;
            title_->SetInnerRML(
                Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(title.c_str())));
            dirty = true;
        }
        return dirty;
    }

    std::optional<bool> ProcessInput(const SessionInputEvent &event)
    {
        if (!visible_)
            return std::nullopt;
        const bool dragging = movable_.IsDragging();
        const bool processed = host_.ProcessInput(event);
        auto *target = host_.HoverElement();
        if (event.kind == SessionInputEventKind::Pointer &&
            event.action == SessionInputAction::PointerButton && event.pressed &&
            Descendant(target, panel_))
            changes_.focus = true;
        changes_.dismiss = close_.IsClick() || changes_.dismiss;
        inputDirty_ = true;
        PublishPosition();
        for (auto *element = target; element && element != panel_;
             element = element->GetParentNode())
            if (element->IsClassSet("mu-item-slot"))
                return false;
        if (event.kind != SessionInputEventKind::Pointer)
            return std::nullopt;
        if (dragging || movable_.IsDragging() || Descendant(target, panel_))
            return true;
        return std::nullopt;
    }

    void PublishPosition()
    {
        const auto position = movable_.Position();
        left_ = position.left;
        top_ = position.top;
    }
    Rect Scaled(float x, float y, float width, float height) const
    {
        return {(left_.load() + x) * scaleX_.load(), (top_.load() + y) * scaleY_.load(),
                width * scaleX_.load(), height * scaleY_.load()};
    }
    Rect GridCell(std::size_t bag) const
    {
        return Scaled(
            InventoryExtensionPanelDesign().Values(InventoryExtensionPanelKey::GridX)[bag],
            InventoryExtensionPanelDesign().Values(InventoryExtensionPanelKey::GridY)[bag],
            InventoryExtensionPanelDesign().Number(InventoryExtensionPanelKey::PitchX),
            InventoryExtensionPanelDesign().Number(InventoryExtensionPanelKey::PitchY));
    }

    RmlDocumentHost host_;
    RmlMuMovablePanel movable_;
    RmlMuButton close_;
    std::array<RmlMuSlot, MAX_INVENTORY_EXT> slots_;
    std::array<int, MAX_INVENTORY_EXT> slotFrames_{};
    bool slotFramesDirty_ = true;
    std::array<Rml::ElementList, MAX_INVENTORY_EXT_COUNT> pages_;
    Rml::Element *panel_ = nullptr;
    Rml::Element *title_ = nullptr;
    std::wstring titleText_;
    Changes changes_;
    std::size_t bagCount_ = MAX_INVENTORY_EXT_COUNT + 1;
    bool inputDirty_ = false;
    bool positionSet_ = false;
    std::atomic<bool> visible_{false};
    std::atomic<float> left_{0}, top_{0}, scaleX_{0}, scaleY_{0}, panelHeight_{0};
};

RmlInventoryExtensionPanel::RmlInventoryExtensionPanel(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper))
{
}
RmlInventoryExtensionPanel::~RmlInventoryExtensionPanel() = default;
void RmlInventoryExtensionPanel::SetSlotFrames(std::span<const int> frames, std::size_t offset)
{
    auto first = impl_->slotFrames_.begin() + offset;
    if (std::equal(frames.begin(), frames.end(), first))
        return;
    std::copy(frames.begin(), frames.end(), first);
    impl_->slotFramesDirty_ = true;
}
void RmlInventoryExtensionPanel::Release()
{
    impl_->Release();
}
bool RmlInventoryExtensionPanel::PrepareOnWorker(int width, int height, bool visible,
                                                 std::size_t bags, const std::wstring &title)
{
    return impl_->Prepare(width, height, visible, bags, title);
}
std::optional<bool> RmlInventoryExtensionPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
RmlInventoryExtensionPanel::Changes RmlInventoryExtensionPanel::TakeChanges()
{
    return std::exchange(impl_->changes_, {});
}
bool RmlInventoryExtensionPanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->panel_ || impl_->host_.Record(facade);
}
RmlInventoryExtensionPanel::Rect RmlInventoryExtensionPanel::ReferenceRect() const
{
    return impl_->Scaled(0, 0,
                         InventoryExtensionPanelDesign().Number(InventoryExtensionPanelKey::Width),
                         impl_->panelHeight_.load());
}
RmlInventoryExtensionPanel::Rect RmlInventoryExtensionPanel::GridCell(std::size_t bag) const
{
    return impl_->GridCell(bag);
}
} // namespace UI::Modern::PC::Inventory

namespace UI::Modern::PC::Inventory
{
namespace
{
enum class InventoryPanelKey
{
    Size,
    Reference,
    Initial,
    Grid,
    Equipment0,
    Button0 = static_cast<int>(Equipment0) + MAX_EQUIPMENT_INDEX
};
const RmlUiDesign &InventoryPanelDesign()
{
    static const RmlUiDesign design(
        "Data/UI/PC/Inventory/inventory.rml",
        {"Inventory-Size",        "Inventory-Reference",  "Inventory-InitialPosition",
         "Inventory-Grid",        "Inventory-Equipment0", "Inventory-Equipment1",
         "Inventory-Equipment2",  "Inventory-Equipment3", "Inventory-Equipment4",
         "Inventory-Equipment5",  "Inventory-Equipment6", "Inventory-Equipment7",
         "Inventory-Equipment8",  "Inventory-Equipment9", "Inventory-Equipment10",
         "Inventory-Equipment11", "Inventory-Button0",    "Inventory-Button1",
         "Inventory-Button2",     "Inventory-Button3",    "Inventory-Button4",
         "Inventory-Button5"});
    return design;
}
constexpr std::array<const char *, RmlInventoryPanel::ButtonCount> ButtonIds{
    "btnClose",        "btnRepair",    "btnPrivateStore",
    "btnExtensionBag", "btnSetOption", "btnSocketOption"};
constexpr std::array<const char *, MAX_EQUIPMENT_INDEX> EquipmentIds{
    "slot_weapon_right", "slot_weapon_left", "slot_helm",       "slot_armor",
    "slot_pants",        "slot_gloves",      "slot_boots",      "slot_wing",
    "slot_helper",       "slot_amulet",      "slot_ring_right", "slot_ring_left"};

} // namespace

class RmlInventoryPanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : host_(keeper, "inventory-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "Inventory", "inventory.rml"))
    {
    }
    ~Impl()
    {
        Release();
    }

    void Release()
    {
        movable_.Unbind();
        for (auto &button : buttons_)
            button.Unbind();
        for (auto &slot : equipment_)
            slot.Unbind();
        for (auto &slot : slots_)
            slot.Unbind();
        buttonElements_.fill(nullptr);
        labels_.fill(nullptr);
        panel_ = title_ = zen_ = helm_ = gloves_ = nullptr;
        current_.reset();
        focus_ = false;
        hovered_ = ButtonCount;
        hoveredEquipment_ = -1;
        inputDirty_ = positionSet_ = false;
        visible_ = false;
        left_ = top_ = scaleX_ = scaleY_ = 0;
        host_.Release();
    }

    bool Bind()
    {
        auto *document = host_.Document();
        panel_ = document->GetElementById("inventory");
        title_ = document->GetElementById("tfTitle");
        zen_ = document->GetElementById("tfZen");
        helm_ = document->GetElementById("mc_helm");
        gloves_ = document->GetElementById("mc_gloves");
        auto *drag = document->GetElementById("btnDrag");
        if (!panel_ || !title_ || !zen_ || !helm_ || !gloves_ || !drag)
            return false;
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            buttonElements_[i] = document->GetElementById(ButtonIds[i]);
            if (!buttonElements_[i])
                return false;
            buttons_[i].Bind(*buttonElements_[i]);
            labels_[i] = document->GetElementById(std::string(ButtonIds[i]) + "-label");
        }
        if (!BindSlots(*document))
            return false;
        movable_.Bind(*panel_, *drag);
        return true;
    }
    bool BindSlots(Rml::ElementDocument &document)
    {
        for (std::size_t i = 0; i < slots_.size(); ++i)
        {
            auto *slot = document.GetElementById("isSlot" + std::to_string(i));
            if (!slot)
                return false;
            slots_[i].Bind(*slot);
        }
        for (std::size_t i = 0; i < equipment_.size(); ++i)
        {
            equipmentElements_[i] = document.GetElementById(EquipmentIds[i]);
            if (!equipmentElements_[i])
                return false;
            equipment_[i].Bind(*equipmentElements_[i]);
        }
        return true;
    }
    bool Prepare(int width, int height, bool visible, const Content &content)
    {
        if (!panel_ && !visible)
            return true;
        const auto initial = InventoryPanelDesign().Values(InventoryPanelKey::Initial);
        const auto size = InventoryPanelDesign().Values(InventoryPanelKey::Size);
        if (!host_.Ensure(width, height, initial[0] + size[0], initial[1] + size[1]))
            return false;
        if (!panel_ && !Bind())
        {
            Release();
            return false;
        }
        if (!host_.SetVisible(visible))
            return false;
        bool dirty = visible_.load() != visible || inputDirty_;
        dirty = ApplyContent(content) || dirty;
        const auto viewport = host_.Viewport();
        movable_.Configure(viewport.width, viewport.height, size[0], size[1]);
        if (!positionSet_)
        {
            movable_.SetPosition(initial[0], initial[1]);
            positionSet_ = true;
            dirty = true;
        }
        if (!visible)
        {
            movable_.CancelDrag();
            hovered_ = ButtonCount;
            hoveredEquipment_ = -1;
        }
        dirty = movable_.TakeDirty() || dirty;
        for (auto &button : buttons_)
            dirty = button.SyncVisualState() || dirty;
        if (inputDirty_)
        {
            for (auto &slot : slots_)
                dirty = slot.SyncVisualState() || dirty;
            for (auto &slot : equipment_)
                dirty = slot.SyncVisualState() || dirty;
        }
        inputDirty_ = false;
        const auto reference = InventoryPanelDesign().Values(InventoryPanelKey::Reference);
        scaleX_ = reference[0] / viewport.width;
        scaleY_ = reference[1] / viewport.height;
        PublishPosition();
        visible_ = visible;
        return host_.CaptureIfDirty(dirty);
    }
    static void Text(Rml::Element &element, const std::wstring &value)
    {
        element.SetInnerRML(
            Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(value.c_str())));
    }
    static void Show(Rml::Element &element, bool shown)
    {
        element.SetProperty(
            Rml::PropertyId::Display,
            Rml::Property(shown ? Rml::Style::Display::Block : Rml::Style::Display::None));
    }
    bool ApplyContent(const Content &next)
    {
        if (current_ && *current_ == next)
            return false;
        if (!current_ || current_->title != next.title)
            Text(*title_, next.title);
        if (!current_ || current_->zen != next.zen)
            Text(*zen_, next.zen);
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            if (labels_[i] && (!current_ || current_->labels[i] != next.labels[i]))
                Text(*labels_[i], next.labels[i]);
            buttons_[i].SetEnable(next.enabled[i]);
            buttons_[i].SetVisible(next.shown[i]);
        }
        Show(*helm_, next.showHelm);
        Show(*equipmentElements_[EQUIPMENT_HELM], next.showHelm);
        Show(*gloves_, next.showGloves);
        Show(*equipmentElements_[EQUIPMENT_GLOVES], next.showGloves);
        for (std::size_t i = 0; i < equipment_.size(); ++i)
            equipment_[i].SetIconState(next.equipmentFrames[i]);
        for (std::size_t i = 0; i < slots_.size(); ++i)
            slots_[i].SetIconState(next.gridFrames[i]);
        current_ = next;
        return true;
    }
    std::optional<bool> ProcessInput(const SessionInputEvent &event)
    {
        if (!visible_)
            return std::nullopt;
        const bool dragging = movable_.IsDragging();
        const bool processed = host_.ProcessInput(event);
        auto *target = host_.HoverElement();
        hovered_ = ButtonCount;
        hoveredEquipment_ = -1;
        for (std::size_t i = 0; i < buttons_.size(); ++i)
            if (Descendant(target, buttonElements_[i]))
                hovered_ = static_cast<Button>(i);
        for (std::size_t i = 0; i < equipment_.size(); ++i)
            if (Descendant(target, equipmentElements_[i]))
                hoveredEquipment_ = static_cast<int>(i);
        if (event.kind == SessionInputEventKind::Pointer &&
            event.action == SessionInputAction::PointerButton && event.pressed &&
            Descendant(target, panel_))
            focus_ = true;
        inputDirty_ = true;
        PublishPosition();
        if (event.kind != SessionInputEventKind::Pointer)
            return std::nullopt;
        if (dragging || movable_.IsDragging())
            return true;
        if (!Descendant(target, panel_))
            return std::nullopt;
        for (auto *element = target; element && element != panel_;
             element = element->GetParentNode())
            if (element->IsClassSet("mu-item-slot"))
                return false;
        return true;
    }

    void PublishPosition()
    {
        const auto position = movable_.Position();
        left_ = position.left;
        top_ = position.top;
    }
    Rect Scaled(float x, float y, float width, float height) const
    {
        return {(left_.load() + x) * scaleX_.load(), (top_.load() + y) * scaleY_.load(),
                width * scaleX_.load(), height * scaleY_.load()};
    }
    RmlDocumentHost host_;
    RmlMuMovablePanel movable_;
    std::array<RmlMuButton, ButtonCount> buttons_;
    std::array<RmlMuSlot, MAX_INVENTORY> slots_;
    std::array<RmlMuSlot, MAX_EQUIPMENT_INDEX> equipment_;
    std::array<Rml::Element *, ButtonCount> buttonElements_{}, labels_{};
    std::array<Rml::Element *, MAX_EQUIPMENT_INDEX> equipmentElements_{};
    Rml::Element *panel_ = nullptr, *title_ = nullptr, *zen_ = nullptr, *helm_ = nullptr,
                 *gloves_ = nullptr;
    std::optional<Content> current_;
    bool inputDirty_ = false, positionSet_ = false;
    std::atomic<bool> visible_{false}, focus_{false};
    std::atomic<Button> hovered_{ButtonCount};
    std::atomic<int> hoveredEquipment_{-1};
    std::atomic<float> left_{0}, top_{0}, scaleX_{0}, scaleY_{0};
};

RmlInventoryPanel::RmlInventoryPanel(SessionKeeper &keeper) : impl_(std::make_unique<Impl>(keeper))
{
}
RmlInventoryPanel::~RmlInventoryPanel() = default;
void RmlInventoryPanel::Release()
{
    impl_->Release();
}
bool RmlInventoryPanel::PrepareOnWorker(int width, int height, bool visible, const Content &content)
{
    return impl_->Prepare(width, height, visible, content);
}
std::optional<bool> RmlInventoryPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
bool RmlInventoryPanel::TakeClick(Button button)
{
    return impl_->buttons_[button].IsClick();
}
bool RmlInventoryPanel::TakeFocus()
{
    return impl_->focus_.exchange(false);
}
bool RmlInventoryPanel::Hovered(Button button) const
{
    return impl_->hovered_ == button;
}
int RmlInventoryPanel::HoveredEquipment() const
{
    return impl_->hoveredEquipment_;
}
bool RmlInventoryPanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->panel_ || impl_->host_.Record(facade);
}
RmlInventoryPanel::Rect RmlInventoryPanel::PanelRect() const
{
    const auto size = InventoryPanelDesign().Values(InventoryPanelKey::Size);
    return impl_->Scaled(0, 0, size[0], size[1]);
}
RmlInventoryPanel::Rect RmlInventoryPanel::GridCell() const
{
    const auto rect = InventoryPanelDesign().Values(InventoryPanelKey::Grid);
    return impl_->Scaled(rect[0], rect[1], rect[2], rect[3]);
}
RmlInventoryPanel::Rect RmlInventoryPanel::EquipmentRect(std::size_t index) const
{
    const auto rect = InventoryPanelDesign().Values(
        static_cast<std::size_t>(InventoryPanelKey::Equipment0) + index);
    return impl_->Scaled(rect[0], rect[1], rect[2], rect[3]);
}
RmlInventoryPanel::Rect RmlInventoryPanel::ButtonRect(Button button) const
{
    const auto rect = InventoryPanelDesign().Values(
        static_cast<std::size_t>(InventoryPanelKey::Button0) + button);
    return impl_->Scaled(rect[0], rect[1], rect[2], rect[3]);
}
} // namespace UI::Modern::PC::Inventory

namespace UI::Modern::PC::Inventory
{
class RmlItemDialogPanel::Impl final
{
  public:
    struct Text
    {
        std::wstring value;
        std::optional<std::uint32_t> color;
        Rml::Element *element = nullptr;
        Rml::ElementFormControlInput *input = nullptr;
        std::unique_ptr<RmlMuTextArea> area;
        bool dirty = true;
    };
    struct Slot
    {
        RmlMuSlot control;
        Rml::Element *element = nullptr;
        Rect bounds{};
    };
    struct Button
    {
        std::string id;
        RmlMuButton control;
        Rml::Element *element = nullptr;
    };
    Impl(SessionKeeper &keeper, const char *document, const char *group)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, group, document)),
          design_(path_, {"Menu-Size", "Menu-Reference"}),
          host_(keeper,
                std::string(document) + "-" + std::to_string(keeper.Id().RawValue()) + "-" +
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
        list_.Unbind();
        movable_.Unbind();
        movableBound_ = false;
        positioned_ = false;
        buttons_.clear();
        slots_.clear();
        for (auto &[id, text] : texts_)
        {
            text.area.reset();
            text.element = nullptr;
            text.input = nullptr;
            text.dirty = true;
        }
        menu_ = nullptr;
        input_ = nullptr;
        inputValue_.clear();
        focus_ = false;
        dirty_ = true;
        host_.Release();
    }
    void SetText(const char *id, std::wstring_view value)
    {
        auto [it, inserted] = texts_.try_emplace(id);
        auto &text = it->second;
        if (!inserted && text.value == value)
            return;
        text.value = value;
        text.dirty = dirty_ = true;
    }
    bool Bind()
    {
        auto *document = host_.Document();
        menu_ = document->GetElementById("menu");
        if (!menu_)
            return false;
        Rml::ElementList elements;
        document->GetElementsByClassName(elements, "mu-button");
        for (auto *element : elements)
        {
            auto &button = buttons_.emplace_back();
            button.id = element->GetId();
            button.element = element;
            if (auto state = visible_.find(button.id); state != visible_.end() && !state->second)
                element->SetProperty("display", "none");
            button.control.Bind(*element);
            button.control.SetEnable(!element->IsClassSet("source-disabled"));
            if (auto state = enabled_.find(button.id); state != enabled_.end())
                button.control.SetEnable(state->second);
        }
        if (auto *drag = document->GetElementById("btnDrag"))
        {
            movable_.Bind(*menu_, *drag);
            movableBound_ = true;
        }
        if (!inputId_.empty())
        {
            input_ = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
                document->GetElementById(inputId_));
            if (!input_)
                return false;
            input_->SetAttribute("maxlength", inputMaxLength_);
            focusInput_ = true;
        }
        elements.clear();
        document->GetElementsByClassName(elements, "mu-item-slot");
        for (auto *element : elements)
        {
            auto &slot = slots_[element->GetId()];
            slot.element = element;
            slot.control.Bind(*element);
        }
        if (!listId_.empty() &&
            !list_.Bind(*document, listId_.c_str(), scrollbarId_.c_str(), rowTemplateId_.c_str()))
            return false;
        return ArrangeKeypad();
    }
    bool ArrangeKeypad()
    {
        std::vector<Rml::Vector2f> positions;
        std::vector<Rml::Element *> digits;
        for (std::size_t i = 0; i < keypadOrder_.size(); ++i)
        {
            auto *digit = host_.Document()->GetElementById("btnNum" + std::to_string(i));
            if (!digit)
                return false;
            digits.push_back(digit);
            positions.push_back(
                {digit->GetProperty<float>("left"), digit->GetProperty<float>("top")});
        }
        for (std::size_t i = 0; i < keypadOrder_.size(); ++i)
        {
            auto *digit = digits[keypadOrder_[i]];
            digit->SetProperty(Rml::PropertyId::Left, Rml::Property(positions[i].x, Rml::Unit::PX));
            digit->SetProperty(Rml::PropertyId::Top, Rml::Property(positions[i].y, Rml::Unit::PX));
        }
        return true;
    }
    void Position()
    {
        if (!movableBound_)
            return;
        const auto size = design_.Values(0);
        const auto viewport = host_.Viewport();
        movable_.Configure(viewport.width, viewport.height, size[0], size[1]);
        if (!positioned_)
        {
            menu_->SetProperty("position", "absolute");
            movable_.SetPosition((viewport.width - size[0]) / 2, (viewport.height - size[1]) / 2);
            positioned_ = true;
        }
        dirty_ = movable_.TakeDirty() || dirty_;
    }
    bool ApplyText()
    {
        for (auto &[id, text] : texts_)
        {
            if (!text.dirty)
                continue;
            if (!text.element)
            {
                text.element = host_.Document()->GetElementById(id);
                text.input = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(text.element);
                if (text.element && text.element->IsClassSet("mu-text-area"))
                {
                    text.area = std::make_unique<RmlMuTextArea>();
                    if (!text.area->Bind(*host_.Document(), id.c_str()))
                        return false;
                }
            }
            if (!text.element)
                return false;
            const auto value = StringUtils::WideToNarrow(text.value.c_str());
            if (text.input)
                text.input->SetValue(value);
            else if (text.area)
                text.area->SetMarkup(Rml::StringUtilities::EncodeRml(value));
            else
                text.element->SetInnerRML(Rml::StringUtilities::EncodeRml(value));
            if (text.color)
            {
                const auto color = *text.color;
                text.element->SetProperty(
                    Rml::PropertyId::Color,
                    Rml::Property(Rml::Colourb(color & 255, (color >> 8) & 255, (color >> 16) & 255,
                                               color >> 24),
                                  Rml::Unit::COLOUR));
            }
            text.dirty = false;
        }
        return true;
    }
    bool Prepare(int width, int height, bool visible)
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
        if (!visible)
            return Hide();
        if (!ApplyText())
            return false;
        if (focusInput_ && input_->Focus(true))
        {
            focusInput_ = false;
            dirty_ = true;
        }
        Position();
        for (auto &button : buttons_)
            dirty_ = button.control.SyncVisualState() || dirty_;
        for (auto &[id, slot] : slots_)
            dirty_ = slot.control.SyncVisualState() || dirty_;
        if (!listId_.empty())
        {
            if (dirty_)
                host_.Document()->GetContext()->Update();
            dirty_ = list_.Apply() || dirty_;
        }
        if (!host_.CaptureIfDirty(dirty_))
            return false;
        dirty_ = false;
        PublishBounds();
        return true;
    }
    void PublishBounds()
    {
        const auto reference = design_.Values(1);
        const auto viewport = host_.Viewport();
        const auto offset = menu_->GetAbsoluteOffset();
        const auto extent = menu_->GetBox().GetSize();
        bounds_ = {
            offset.x * reference[0] / viewport.width, offset.y * reference[1] / viewport.height,
            extent.x * reference[0] / viewport.width, extent.y * reference[1] / viewport.height};
        for (auto &[id, slot] : slots_)
        {
            const auto position = slot.element->GetAbsoluteOffset();
            const auto size = slot.element->GetBox().GetSize();
            slot.bounds = {position.x * reference[0] / viewport.width,
                           position.y * reference[1] / viewport.height,
                           size.x * reference[0] / viewport.width,
                           size.y * reference[1] / viewport.height};
        }
    }
    bool Hide()
    {
        movable_.CancelDrag();
        for (auto &button : buttons_)
            button.control.Reset();
        focus_ = false;
        return host_.CaptureIfDirty(false);
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    RmlMuMovablePanel movable_;
    bool movableBound_ = false, positioned_ = false;
    Rml::Element *menu_ = nullptr;
    std::map<std::string, Text, std::less<>> texts_;
    std::deque<Button> buttons_;
    std::map<std::string, Slot, std::less<>> slots_;
    std::map<std::string, bool, std::less<>> enabled_;
    std::map<std::string, bool, std::less<>> visible_;
    RmlMuScrollingList list_;
    std::string listId_, scrollbarId_, rowTemplateId_;
    std::vector<int> keypadOrder_;
    Rml::ElementFormControlInput *input_ = nullptr;
    std::string inputId_;
    std::wstring inputValue_;
    int inputMaxLength_ = 0;
    bool focusInput_ = false;
    bool dirty_ = true;
    bool focus_ = false;
    Rect bounds_{};
};

RmlItemDialogPanel::RmlItemDialogPanel(SessionKeeper &keeper, const char *document,
                                       const char *group)
    : impl_(std::make_unique<Impl>(keeper, document, group))
{
}
RmlItemDialogPanel::~RmlItemDialogPanel() = default;
void RmlItemDialogPanel::Release()
{
    impl_->Release();
}
void RmlItemDialogPanel::SetText(const char *id, std::wstring_view text)
{
    impl_->SetText(id, text);
}
void RmlItemDialogPanel::SetTextColor(const char *id, std::uint32_t color)
{
    auto &text = impl_->texts_[id];
    if (text.color == color)
        return;
    text.color = color;
    text.dirty = impl_->dirty_ = true;
}
void RmlItemDialogPanel::SetButtonVisible(const char *id, bool visible)
{
    auto [it, inserted] = impl_->visible_.try_emplace(id, visible);
    if (!inserted && it->second == visible)
        return;
    it->second = visible;
    for (auto &button : impl_->buttons_)
        if (button.id == id)
        {
            if (visible)
                button.element->RemoveProperty("display");
            else
            {
                button.element->SetProperty("display", "none");
                button.control.Reset();
            }
        }
    impl_->dirty_ = true;
}
void RmlItemDialogPanel::SetButtonEnabled(const char *id, bool enabled)
{
    auto [it, inserted] = impl_->enabled_.try_emplace(id, enabled);
    if (!inserted && it->second == enabled)
        return;
    it->second = enabled;
    for (auto &button : impl_->buttons_)
        if (button.id == id)
            button.control.SetEnable(enabled);
    impl_->dirty_ = true;
}
void RmlItemDialogPanel::ConfigureList(const char *list, const char *scrollbar,
                                       const char *rowTemplate)
{
    impl_->listId_ = list;
    impl_->scrollbarId_ = scrollbar;
    impl_->rowTemplateId_ = rowTemplate;
}
void RmlItemDialogPanel::SetListData(const RmlMuScrollingList::Data &rows)
{
    impl_->list_.SetData(rows);
}
std::optional<std::size_t> RmlItemDialogPanel::TakeListSelection()
{
    return impl_->list_.TakeSelection();
}
void RmlItemDialogPanel::SetKeypadOrder(std::span<const int> order)
{
    impl_->keypadOrder_.assign(order.begin(), order.end());
}
void RmlItemDialogPanel::ConfigureInput(const char *id, int maxLength)
{
    impl_->inputValue_.clear();
    impl_->focusInput_ = true;
    impl_->inputId_ = id;
    impl_->inputMaxLength_ = maxLength;
    impl_->SetText(id, L"");
}
const std::wstring &RmlItemDialogPanel::InputValue() const
{
    return impl_->inputValue_;
}
std::optional<RmlTextInputArea> RmlItemDialogPanel::TextInputArea() const
{
    return impl_->input_ ? impl_->host_.FocusedTextInputArea() : std::nullopt;
}
bool RmlItemDialogPanel::PrepareOnWorker(int width, int height, bool visible)
{
    return impl_->Prepare(width, height, visible);
}
bool RmlItemDialogPanel::ProcessInput(const SessionInputEvent &event)
{
    if (!impl_->menu_)
        return false;
    const bool processed = impl_->host_.ProcessInput(event);
    if (!impl_->listId_.empty())
        impl_->list_.ProcessInput(event, impl_->host_.HoverElement());
    impl_->dirty_ = true;
    if (impl_->input_)
    {
        impl_->inputValue_ = StringUtils::NarrowToWide(impl_->input_->GetValue().c_str());
        if (event.kind == SessionInputEventKind::Key &&
            (event.code == SDL_SCANCODE_ESCAPE || event.code == SDL_SCANCODE_RETURN ||
             event.code == SDL_SCANCODE_KP_ENTER))
            return false;
        return processed;
    }
    return event.kind == SessionInputEventKind::Pointer;
}
bool RmlItemDialogPanel::TakeClick(const char *id)
{
    for (auto &button : impl_->buttons_)
        if (button.id == id)
            return button.control.IsClick();
    return false;
}
bool RmlItemDialogPanel::ProcessPanelInput(const SessionInputEvent &event)
{
    if (!impl_->menu_)
        return false;
    const bool dragging = impl_->movable_.IsDragging();
    const bool processed = ProcessInput(event);
    bool inside = false;
    for (auto *element = impl_->host_.HoverElement(); element; element = element->GetParentNode())
        if (element == impl_->menu_)
        {
            inside = true;
            break;
        }
    if (inside && event.action == SessionInputAction::PointerButton && event.pressed)
        impl_->focus_ = true;
    return event.kind == SessionInputEventKind::Pointer
               ? inside || dragging || impl_->movable_.IsDragging()
               : processed;
}
bool RmlItemDialogPanel::TakeFocus()
{
    return std::exchange(impl_->focus_, false);
}
bool RmlItemDialogPanel::Record(LegacyRenderFacade &facade) const
{
    return impl_->host_.Record(facade);
}
RmlItemDialogPanel::Rect RmlItemDialogPanel::SlotBounds(const char *id) const
{
    const auto slot = impl_->slots_.find(id);
    return slot == impl_->slots_.end() ? Rect{} : slot->second.bounds;
}
RmlItemDialogPanel::Rect RmlItemDialogPanel::Bounds() const
{
    return impl_->bounds_;
}
} // namespace UI::Modern::PC::Inventory

namespace UI::Modern::PC::Inventory
{
namespace
{
std::string Encoded(const std::wstring &text)
{
    return Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(text.c_str()));
}
void AppendLines(std::string &markup, const std::vector<RmlItemExplanationPanel::Line> &lines)
{
    for (const auto &line : lines)
        markup += "<div class=\"line " + line.style + (line.text.empty() ? " blank" : "") + "\">" +
                  Encoded(line.text) + "</div>";
}
std::string Markup(const RmlItemExplanationPanel::Content &content)
{
    std::string markup;
    AppendLines(markup, content.heading);
    if (!content.columns.empty())
    {
        markup += "<table><tr>";
        for (const auto &column : content.columns)
            markup += "<th>" + Encoded(column) + "</th>";
        markup += "</tr>";
        for (const auto &row : content.rows)
        {
            markup += row.available ? "<tr class=\"white\">" : "<tr class=\"red\">";
            for (const auto &cell : row.cells)
                markup += "<td>" + Encoded(cell) + "</td>";
            markup += "</tr>";
        }
        markup += "</table>";
    }
    AppendLines(markup, content.notes);
    return markup;
}
} // namespace
class RmlItemExplanationPanel::Impl final
{
  public:
    Impl(SessionKeeper &keeper, const char *contextName)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "Inventory", "item_explanation.rml")),
          design_(path_, {"Popup-Minimum", "Popup-Reference"}),
          host_(keeper, std::string(contextName) + "-" + std::to_string(keeper.Id().RawValue()),
                path_)
    {
    }
    ~Impl()
    {
        Release();
    }
    void Release()
    {
        popup_ = information_ = nullptr;
        revision_.reset();
        extent_.reset();
        width_ = height_ = 0;
        visible_ = inputDirty_ = measure_ = false;
        host_.Release();
    }
    bool Prepare(int width, int height, bool visible, const Content &content)
    {
        if (!popup_ && !visible)
            return true;
        const auto minimum = design_.Values(0);
        const auto extent = extent_.value_or(Rml::Vector2f(minimum[0], minimum[1]));
        if (!host_.Ensure(width, height, extent.x, extent.y))
            return false;
        if (!popup_)
        {
            popup_ = host_.Document()->GetElementById("popup");
            information_ = host_.Document()->GetElementById("information");
            if (!popup_ || !information_)
            {
                Release();
                return false;
            }
        }
        if (!host_.SetVisible(visible))
            return false;
        bool dirty = inputDirty_ || revision_ != content.revision || visible_ != visible;
        visible_ = visible;
        if (revision_ != content.revision)
        {
            information_->SetInnerRML(Markup(content));
            revision_ = content.revision;
            measure_ = true;
        }
        if (visible && measure_)
        {
            host_.Document()->GetContext()->Update();
            const auto size = popup_->GetBox().GetSize();
            if (!host_.Ensure(width, height, size.x, size.y))
                return false;
            extent_ = size;
            measure_ = false;
        }
        if (extent_)
        {
            const auto reference = design_.Values(1);
            const auto viewport = host_.Viewport();
            width_ = extent_->x * reference[0] / viewport.width;
            height_ = extent_->y * reference[1] / viewport.height;
        }
        inputDirty_ = false;
        return host_.CaptureIfDirty(dirty);
    }
    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!visible_)
            return false;
        host_.ProcessInput(event);
        inputDirty_ = true;
        for (auto *element = host_.HoverElement(); element; element = element->GetParentNode())
            if (element == popup_)
                return event.kind == SessionInputEventKind::Pointer;
        return false;
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    Rml::Element *popup_ = nullptr;
    Rml::Element *information_ = nullptr;
    std::optional<std::uint64_t> revision_;
    std::optional<Rml::Vector2f> extent_;
    float width_ = 0, height_ = 0;
    bool visible_ = false, inputDirty_ = false, measure_ = false;
};
RmlItemExplanationPanel::RmlItemExplanationPanel(SessionKeeper &keeper, const char *contextName)
    : impl_(std::make_unique<Impl>(keeper, contextName))
{
}
RmlItemExplanationPanel::~RmlItemExplanationPanel() = default;
void RmlItemExplanationPanel::Release()
{
    impl_->Release();
}
bool RmlItemExplanationPanel::PrepareOnWorker(int width, int height, bool visible,
                                              const Content &content)
{
    return impl_->Prepare(width, height, visible, content);
}
bool RmlItemExplanationPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
bool RmlItemExplanationPanel::ContainsReferencePointer(int x, int y) const
{
    return impl_->visible_ && x >= 0 && y >= 0 && x < impl_->width_ && y < impl_->height_;
}
bool RmlItemExplanationPanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->popup_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::Inventory

namespace UI::Modern::PC::Inventory
{
namespace
{
enum class ItemPanelDesignKey
{
    Size,
    Reference,
    Initial,
    Grids
};

} // namespace
class RmlItemPanel::Impl final
{
  public:
    struct Field
    {
        Rml::Element *element = nullptr;
        RmlMuButton *button = nullptr;
        RmlMuSlot *slot = nullptr;
        std::optional<std::wstring> text;
        std::optional<std::string> markup;
        std::optional<bool> shown, enabled;
        int frame = 0, appliedFrame = 0;
        bool dirty = true;
    };
    struct Button
    {
        std::string id;
        RmlMuButton control;
    };
    Impl(SessionKeeper &keeper, const char *document, const char *overlayDocument)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "Inventory", document)),
          design_(path_, {"Panel-Size", "Panel-Reference", "Panel-Initial", "Panel-Grids"}),
          host_(keeper, std::string(document) + "-" + std::to_string(keeper.Id().RawValue()), path_)
    {
        if (overlayDocument)
            overlay_ = std::make_unique<RmlDocumentHost>(
                keeper, std::string(overlayDocument) + "-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "Inventory", overlayDocument));
    }
    ~Impl()
    {
        Release();
    }
    void Release()
    {
        movable_.Unbind();
        scrollBar_.Unbind();
        hasScrollBar_ = false;
        buttons_.clear();
        slots_.clear();
        fields_.clear();
        panel_ = nullptr;
        stagedVisible_ = visible_ = focus_ = false;
        stagedDirty_ = true;
        inputDirty_ = positionSet_ = false;
        left_ = top_ = scaleX_ = scaleY_ = 0;
        host_.Release();
        if (overlay_)
            overlay_->Release();
    }
    Field &GetField(const char *id)
    {
        auto found = fields_.find(id);
        if (found == fields_.end())
            found = fields_.emplace(id, Field{}).first;
        return found->second;
    }
    void Text(const char *id, std::wstring_view value)
    {
        auto &field = GetField(id);
        if (field.text && *field.text == value)
            return;
        field.text = value;
        field.dirty = stagedDirty_ = true;
    }
    void Markup(const char *id, std::string_view value)
    {
        auto &field = GetField(id);
        if (field.markup && *field.markup == value)
            return;
        field.markup = value;
        field.dirty = stagedDirty_ = true;
    }
    void Flag(const char *id, bool value, bool shown)
    {
        auto &field = GetField(id);
        auto &flag = shown ? field.shown : field.enabled;
        if (flag == value)
            return;
        flag = value;
        field.dirty = stagedDirty_ = true;
    }
    void Frame(const char *id, int frame)
    {
        auto &field = GetField(id);
        if (field.frame == frame)
            return;
        field.frame = frame;
        field.dirty = stagedDirty_ = true;
    }
    bool Bind()
    {
        auto *document = host_.Document();
        panel_ = document->GetElementById("panel");
        auto *drag = document->GetElementById("btnDrag");
        if (!panel_ || !drag)
            return false;
        Rml::ElementList elements;
        document->GetElementsByClassName(elements, "mu-button");
        for (auto *element : elements)
        {
            auto &button = buttons_.emplace_back();
            button.id = element->GetId();
            button.control.Bind(*element);
            auto &field = GetField(button.id.c_str());
            field.element = element;
            field.button = &button.control;
        }
        elements.clear();
        document->GetElementsByClassName(elements, "mu-item-slot");
        for (auto *element : elements)
        {
            auto &slot = slots_.emplace_back();
            slot.Bind(*element);
            auto &field = GetField(element->GetId().c_str());
            field.element = element;
            field.slot = &slot;
        }
        movable_.Bind(*panel_, *drag);
        if (document->GetElementById("sbSockList"))
        {
            if (!scrollBar_.Bind(*document, "sbSockList"))
                return false;
            hasScrollBar_ = true;
        }
        return true;
    }
    bool ApplyFields()
    {
        if (!stagedDirty_)
            return true;
        for (auto &[id, field] : fields_)
        {
            if (!field.dirty)
                continue;
            if (!field.element)
                field.element = host_.Document()->GetElementById(id);
            if (!field.element && overlay_)
                field.element = overlay_->Document()->GetElementById(id);
            if (!field.element)
                return false;
            ApplyField(field);
            field.dirty = false;
        }
        stagedDirty_ = false;
        return true;
    }
    static void ApplyField(Field &field)
    {
        auto &element = *field.element;
        if (field.text)
            element.SetInnerRML(
                Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(field.text->c_str())));
        if (field.markup)
            element.SetInnerRML(*field.markup);
        if (field.shown)
        {
            if (field.button)
                field.button->SetVisible(*field.shown);
            else
                element.SetProperty(Rml::PropertyId::Display,
                                    Rml::Property(*field.shown ? Rml::Style::Display::Block
                                                               : Rml::Style::Display::None));
        }
        if (field.button && field.enabled)
            field.button->SetEnable(*field.enabled);
        if (field.frame == field.appliedFrame)
            return;
        if (field.slot)
        {
            field.slot->SetIconState(field.frame);
            field.appliedFrame = field.frame;
            return;
        }
        if (field.appliedFrame)
            element.SetClass("frame-" + std::to_string(field.appliedFrame), false);
        if (field.frame)
            element.SetClass("frame-" + std::to_string(field.frame), true);
        field.appliedFrame = field.frame;
    }
    bool Prepare(int width, int height)
    {
        if (!panel_ && !stagedVisible_)
            return true;
        const auto size = design_.Values(ItemPanelDesignKey::Size),
                   initial = design_.Values(ItemPanelDesignKey::Initial);
        if (!host_.Ensure(width, height, initial[0] + size[0], initial[1] + size[1]))
            return false;
        if (overlay_ &&
            !overlay_->Ensure(width, height, initial[0] + size[0], initial[1] + size[1]))
            return false;
        if (!panel_ && !Bind())
        {
            Release();
            return false;
        }
        if (!host_.SetVisible(stagedVisible_))
            return false;
        if (overlay_ && !overlay_->SetVisible(stagedVisible_))
            return false;
        bool dirty = stagedDirty_ || inputDirty_ || visible_.load() != stagedVisible_;
        if (!ApplyFields())
            return false;
        const auto viewport = host_.Viewport();
        movable_.Configure(viewport.width, viewport.height, size[0], size[1]);
        if (!positionSet_)
        {
            movable_.SetPosition(initial[0], initial[1]);
            positionSet_ = true;
            dirty = true;
        }
        if (!stagedVisible_)
            movable_.CancelDrag();
        dirty = movable_.TakeDirty() || dirty;
        for (auto &button : buttons_)
            dirty = button.control.SyncVisualState() || dirty;
        if (inputDirty_)
            for (auto &slot : slots_)
                dirty = slot.SyncVisualState() || dirty;
        if (hasScrollBar_ && dirty)
            scrollBar_.Apply(RmlMuScrollBarState{});
        inputDirty_ = false;
        const auto reference = design_.Values(ItemPanelDesignKey::Reference);
        scaleX_ = reference[0] / viewport.width;
        scaleY_ = reference[1] / viewport.height;
        PublishPosition();
        visible_ = stagedVisible_;
        if (overlay_)
        {
            const auto position = movable_.Position();
            auto *front = overlay_->Document()->GetElementById("panel");
            front->SetProperty(Rml::PropertyId::Left, Rml::Property(position.left, Rml::Unit::PX));
            front->SetProperty(Rml::PropertyId::Top, Rml::Property(position.top, Rml::Unit::PX));
            if (!overlay_->CaptureIfDirty(dirty))
                return false;
        }
        return host_.CaptureIfDirty(dirty);
    }
    std::optional<bool> Input(const SessionInputEvent &event)
    {
        if (!visible_)
            return std::nullopt;
        const bool dragging = movable_.IsDragging();
        (void)host_.ProcessInput(event);
        inputDirty_ = true;
        auto *target = host_.HoverElement();
        if (event.kind == SessionInputEventKind::Pointer &&
            event.action == SessionInputAction::PointerButton && event.pressed &&
            Descendant(target, panel_))
            focus_ = true;
        PublishPosition();
        if (event.kind != SessionInputEventKind::Pointer)
            return std::nullopt;
        if (dragging || movable_.IsDragging())
            return true;
        if (!Descendant(target, panel_))
            return std::nullopt;
        for (auto *element = target; element != panel_; element = element->GetParentNode())
            if (element->IsClassSet("mu-item-slot"))
                return false;
        return true;
    }
    void PublishPosition()
    {
        const auto position = movable_.Position();
        left_ = position.left;
        top_ = position.top;
    }
    Rect Scaled(float x, float y, float width, float height) const
    {
        return {(left_.load() + x) * scaleX_.load(), (top_.load() + y) * scaleY_.load(),
                width * scaleX_.load(), height * scaleY_.load()};
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    std::unique_ptr<RmlDocumentHost> overlay_;
    RmlMuMovablePanel movable_;
    RmlMuScrollBar scrollBar_;
    bool hasScrollBar_ = false;
    Rml::Element *panel_ = nullptr;
    std::map<std::string, Field, std::less<>> fields_;
    std::deque<Button> buttons_;
    std::deque<RmlMuSlot> slots_;
    bool stagedVisible_ = false, stagedDirty_ = true, inputDirty_ = false, positionSet_ = false;
    std::atomic<bool> visible_{false}, focus_{false};
    std::atomic<float> left_{0}, top_{0}, scaleX_{0}, scaleY_{0};
};
RmlItemPanel::RmlItemPanel(SessionKeeper &keeper, const char *document, const char *overlayDocument)
    : impl_(std::make_unique<Impl>(keeper, document, overlayDocument))
{
}
RmlItemPanel::~RmlItemPanel() = default;
void RmlItemPanel::Release()
{
    impl_->Release();
}
void RmlItemPanel::SetVisible(bool visible)
{
    impl_->stagedVisible_ = visible;
}
void RmlItemPanel::SetText(const char *id, std::wstring_view text)
{
    impl_->Text(id, text);
}
void RmlItemPanel::SetMarkup(const char *id, std::string_view markup)
{
    impl_->Markup(id, markup);
}
void RmlItemPanel::SetShown(const char *id, bool shown)
{
    impl_->Flag(id, shown, true);
}
void RmlItemPanel::SetEnabled(const char *id, bool enabled)
{
    impl_->Flag(id, enabled, false);
}
void RmlItemPanel::SetFrame(const char *id, int frame)
{
    impl_->Frame(id, frame);
}
void RmlItemPanel::SetSlotFrames(std::span<const int> frames, int offset)
{
    for (int frame : frames)
        impl_->Frame(("isSlot" + std::to_string(offset++)).c_str(), frame);
}
bool RmlItemPanel::PrepareOnWorker(int width, int height)
{
    return impl_->Prepare(width, height);
}
std::optional<bool> RmlItemPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->Input(event);
}
bool RmlItemPanel::TakeClick(const char *id)
{
    const auto field = impl_->fields_.find(id);
    return field != impl_->fields_.end() && field->second.button && field->second.button->IsClick();
}
bool RmlItemPanel::TakeFocus()
{
    return impl_->focus_.exchange(false);
}
bool RmlItemPanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->panel_ || impl_->host_.Record(facade);
}
bool RmlItemPanel::RecordOverlay(LegacyRenderFacade &facade) const
{
    return !impl_->overlay_ || !impl_->panel_ || impl_->overlay_->Record(facade);
}
RmlItemPanel::Rect RmlItemPanel::PanelRect() const
{
    const auto size = impl_->design_.Values(ItemPanelDesignKey::Size);
    return impl_->Scaled(0, 0, size[0], size[1]);
}
RmlItemPanel::Rect RmlItemPanel::GridCell(std::size_t grid) const
{
    const auto values = impl_->design_.Values(ItemPanelDesignKey::Grids).subspan(grid * 4, 4);
    return impl_->Scaled(values[0], values[1], values[2], values[3]);
}
} // namespace UI::Modern::PC::Inventory

namespace UI::Modern::PC::Inventory
{
namespace
{
enum class PrivateStorePanelDesignKey
{
    Width,
    Height,
    InitialX,
    InitialY,
    ReferenceWidth,
    ReferenceHeight,
    SellerGridRect,
    BuyerGridRect,
    GridColumns,
    GridRows
};

const RmlUiDesign &PrivateStorePanelDesign()
{
    static const RmlUiDesign design("Data/UI/PC/Inventory/private_store.rml",
                                    {"PrivateStore-Width", "PrivateStore-Height",
                                     "PrivateStore-InitialX", "PrivateStore-InitialY",
                                     "PrivateStore-ReferenceWidth", "PrivateStore-ReferenceHeight",
                                     "PrivateStore-seller-GridRect", "PrivateStore-buyer-GridRect",
                                     "PrivateStore-GridColumns", "PrivateStore-GridRows"});
    return design;
}

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

bool RmlPrivateStoreCloseEnabled(RmlPrivateStoreMode mode, bool shopOpen) noexcept
{
    return mode == RmlPrivateStoreMode::Buyer || shopOpen;
}

float RmlPrivateStorePanel::Width() noexcept
{
    return PrivateStorePanelDesign().Number(PrivateStorePanelDesignKey::Width);
}
float RmlPrivateStorePanel::Height() noexcept
{
    return PrivateStorePanelDesign().Number(PrivateStorePanelDesignKey::Height);
}
std::size_t RmlPrivateStorePanel::GridColumns() noexcept
{
    return PrivateStorePanelDesign().Number<std::size_t>(PrivateStorePanelDesignKey::GridColumns);
}
std::size_t RmlPrivateStorePanel::GridRows() noexcept
{
    return PrivateStorePanelDesign().Number<std::size_t>(PrivateStorePanelDesignKey::GridRows);
}

class RmlPrivateStorePanel::Impl final
{
  public:
    Impl(SessionKeeper &keeper, std::array<RmlMuButton, 3> &buttons,
         std::array<RmlMuSlot, RmlPrivateStoreSlotCount> &slots, RmlPrivateStoreMode mode)
        : buttons_(buttons), slots_(slots), mode_(mode),
          host_(keeper,
                std::string(mode == RmlPrivateStoreMode::Seller ? "private-store-seller-"
                                                                : "private-store-buyer-") +
                    std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "Inventory", "private_store.rml"))
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
        for (RmlMuSlot &slot : slots_)
            slot.Unbind();
        panel_ = nullptr;
        drag_ = nullptr;
        title_ = nullptr;
        shopNameLabel_ = nullptr;
        buyerName_ = nullptr;
        shopNameInput_ = nullptr;
        buttonElements_.fill(nullptr);
        buttonLabels_.fill(nullptr);
        slotElements_.fill(nullptr);
        currentContent_ = {};
        inputValue_.clear();
        changes_ = {};
        viewportWidth_ = 0;
        viewportHeight_ = 0;
        scaleX_ = scaleY_ = 1.0F;
        positionSet_ = false;
        inputDirty_ = false;
        visible_ = false;
        host_.Release();
        PublishGeometry();
    }

    std::optional<bool> ProcessInput(const SessionInputEvent &event)
    {
        if (!publishedVisible_.load(std::memory_order_acquire))
            return std::nullopt;
        const bool wasDragging = movable_.IsDragging();
        const bool processed = host_.ProcessInput(event);
        if (event.action != SessionInputAction::PointerMove)
            ReadChanges();
        inputDirty_ = movable_.TakeDirty() || inputDirty_;
        PublishGeometry();
        if (event.kind != SessionInputEventKind::Pointer)
            return HasTextInputFocus() ? std::optional<bool>(processed) : std::nullopt;
        if (event.action == SessionInputAction::PointerButton && event.pressed &&
            IsDescendantOf(host_.HoverElement(), panel_))
            changes_.focus = true;
        // Rml owns slot clicks; the legacy control still owns item drag and drop.
        if (IsSlotTarget(host_.HoverElement()))
            return false;
        if (wasDragging || movable_.IsDragging() || IsDescendantOf(host_.HoverElement(), panel_))
            return true;
        return std::nullopt;
    }

    RmlPrivateStoreChanges TakeChanges()
    {
        RmlPrivateStoreChanges result = std::move(changes_);
        changes_ = {};
        return result;
    }

    bool Prepare(int viewportWidth, int viewportHeight, bool visible,
                 const RmlPrivateStoreContent &content)
    {
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
        const bool viewportChanged = ApplyViewport(viewportWidth, viewportHeight);
        const bool visibilityChanged = visible_ != visible;
        bool dirty = viewportChanged || visibilityChanged || inputDirty_ || movable_.TakeDirty();
        dirty = ApplyContent(content) || dirty;
        for (RmlMuButton &button : buttons_)
            dirty = button.SyncVisualState() || dirty;
        for (RmlMuSlot &slot : slots_)
            dirty = slot.SyncVisualState() || dirty;

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

    RmlPrivateStoreRect ReferenceRect(int viewportWidth, int viewportHeight) const noexcept
    {
        return ScaledRect(0.0F, 0.0F, Width(), Height(), viewportWidth, viewportHeight);
    }

    RmlPrivateStoreRect InventoryGridRect(int viewportWidth, int viewportHeight) const noexcept
    {
        const auto rect = PrivateStorePanelDesign().Values(
            mode_ == RmlPrivateStoreMode::Buyer ? PrivateStorePanelDesignKey::BuyerGridRect
                                                : PrivateStorePanelDesignKey::SellerGridRect);
        return ScaledRect(rect[0], rect[1], rect[2], rect[3], viewportWidth, viewportHeight);
    }

    std::optional<RmlTextInputArea> TextInputArea() const
    {
        return HasTextInputFocus() ? host_.FocusedTextInputArea() : std::nullopt;
    }

    bool HasTextInputFocus() const noexcept
    {
        return mode_ == RmlPrivateStoreMode::Seller && visible_ && shopNameInput_ != nullptr &&
               shopNameInput_->IsPseudoClassSet("focus");
    }

  private:
    bool EnsureDocument(int viewportWidth, int viewportHeight)
    {
        if (!host_.Ensure(
                viewportWidth, viewportHeight,
                PrivateStorePanelDesign().Number(PrivateStorePanelDesignKey::InitialX) + Width(),
                PrivateStorePanelDesign().Number(PrivateStorePanelDesignKey::InitialY) + Height()))
            return false;
        if (panel_ != nullptr)
            return true;

        Rml::ElementDocument *const document = host_.Document();
        panel_ = document->GetElementById("private-store");
        drag_ = document->GetElementById("private-store-drag");
        title_ = document->GetElementById("private-store-title");
        shopNameLabel_ = document->GetElementById("private-store-name-label");
        buyerName_ = document->GetElementById("private-store-buyer-name");
        shopNameInput_ = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
            document->GetElementById("private-store-name"));
        const std::array<const char *, 3> buttonIds{
            "private-store-open", "private-store-close-shop", "private-store-close"};
        const std::array<const char *, 2> labelIds{"private-store-open-label",
                                                   "private-store-close-shop-label"};
        for (std::size_t index = 0; index < buttonElements_.size(); ++index)
            buttonElements_[index] = document->GetElementById(buttonIds[index]);
        for (std::size_t index = 0; index < buttonLabels_.size(); ++index)
            buttonLabels_[index] = document->GetElementById(labelIds[index]);
        for (std::size_t index = 0; index < slotElements_.size(); ++index)
        {
            slotElements_[index] =
                document->GetElementById("private-store-slot-" + std::to_string(index));
        }

        const auto missing = [](const auto &elements) {
            return std::find(elements.begin(), elements.end(), nullptr) != elements.end();
        };
        if (panel_ == nullptr || buyerName_ == nullptr || drag_ == nullptr || title_ == nullptr ||
            shopNameLabel_ == nullptr || shopNameInput_ == nullptr || missing(buttonElements_) ||
            missing(buttonLabels_) || missing(slotElements_))
        {
            Release();
            return false;
        }

        for (std::size_t index = 0; index < buttons_.size(); ++index)
            buttons_[index].Bind(*buttonElements_[index]);
        for (std::size_t index = 0; index < slots_.size(); ++index)
            slots_[index].Bind(*slotElements_[index]);
        movable_.Bind(*panel_, *drag_);
        ApplyMode();
        viewportWidth_ = 0;
        viewportHeight_ = 0;
        return true;
    }

    void ApplyMode()
    {
        const bool buyer = mode_ == RmlPrivateStoreMode::Buyer;
        panel_->SetClass("buyer", buyer);
        panel_->SetClass("seller", !buyer);
        shopNameInput_->SetDisabled(buyer);
    }

    bool ApplyViewport(int physicalWidth, int physicalHeight)
    {
        const auto viewport = host_.Viewport();
        scaleX_ = static_cast<float>(physicalWidth) / viewport.width;
        scaleY_ = static_cast<float>(physicalHeight) / viewport.height;
        if (viewportWidth_ == viewport.width && viewportHeight_ == viewport.height)
            return false;
        viewportWidth_ = viewport.width;
        viewportHeight_ = viewport.height;
        movable_.Configure(viewport.width, viewport.height, Width(), Height());
        if (!positionSet_)
        {
            movable_.SetPosition(
                PrivateStorePanelDesign().Number(PrivateStorePanelDesignKey::InitialX),
                PrivateStorePanelDesign().Number(PrivateStorePanelDesignKey::InitialY));
            positionSet_ = true;
        }
        return true;
    }

    bool ApplyContent(const RmlPrivateStoreContent &content)
    {
        bool dirty = SetText(*title_, currentContent_.title, content.title);
        dirty =
            SetText(*shopNameLabel_, currentContent_.shopNameLabel, content.shopNameLabel) || dirty;
        dirty = SetText(*buttonLabels_[0], currentContent_.openLabel, content.openLabel) || dirty;
        dirty = SetText(*buttonLabels_[1], currentContent_.closeLabel, content.closeLabel) || dirty;
        if (inputValue_ != content.shopName)
        {
            inputValue_ = content.shopName;
            if (mode_ == RmlPrivateStoreMode::Seller)
                shopNameInput_->SetValue(StringUtils::WideToNarrow(inputValue_.c_str()));
            else
                buyerName_->SetInnerRML(Rml::StringUtilities::EncodeRml(
                    StringUtils::WideToNarrow(inputValue_.c_str())));
            dirty = true;
        }
        buttons_[0].SetVisible(mode_ == RmlPrivateStoreMode::Seller && content.showSellerActions);
        buttons_[1].SetVisible(mode_ == RmlPrivateStoreMode::Buyer || content.showSellerActions);
        buttons_[0].SetEnable(content.openEnabled && !content.shopOpen);
        buttons_[1].SetEnable(RmlPrivateStoreCloseEnabled(mode_, content.shopOpen));
        currentContent_ = content;
        return dirty;
    }

    void ReadChanges()
    {
        changes_.open = buttons_[0].IsClick() || changes_.open;
        const bool closeClicked = buttons_[1].IsClick();
        changes_.close = (mode_ == RmlPrivateStoreMode::Seller && closeClicked) || changes_.close;
        changes_.dismiss = (mode_ == RmlPrivateStoreMode::Buyer && closeClicked) ||
                           buttons_[2].IsClick() || changes_.dismiss;
        for (std::size_t index = 0; index < slots_.size(); ++index)
        {
            if (slots_[index].IsClick() && mode_ == RmlPrivateStoreMode::Buyer)
                changes_.selectSlot = index;
            if (slots_[index].IsClear() && mode_ == RmlPrivateStoreMode::Seller)
                changes_.clearSlot = index;
        }

        if (mode_ == RmlPrivateStoreMode::Buyer)
            return;

        const std::wstring rawValue = StringUtils::NarrowToWide(shopNameInput_->GetValue().c_str());
        std::wstring value = rawValue;
        std::erase(value, L'|');
        if (value != rawValue)
        {
            shopNameInput_->SetValue(StringUtils::WideToNarrow(value.c_str()));
            inputDirty_ = true;
        }
        if (value == inputValue_)
            return;
        inputValue_ = std::move(value);
        changes_.shopName = inputValue_;
        inputDirty_ = true;
    }

    bool IsSlotTarget(const Rml::Element *element) const noexcept
    {
        for (; element != nullptr && element != panel_; element = element->GetParentNode())
            if (element->IsClassSet("mu-item-slot"))
                return true;
        return false;
    }

    RmlPrivateStoreRect ScaledRect(float x, float y, float width, float height, int viewportWidth,
                                   int viewportHeight) const noexcept
    {
        if (viewportWidth <= 0 || viewportHeight <= 0)
            return {};
        const float left = publishedLeft_.load(std::memory_order_acquire);
        const float top = publishedTop_.load(std::memory_order_acquire);
        const float scaleX = publishedScaleX_.load(std::memory_order_acquire);
        const float scaleY = publishedScaleY_.load(std::memory_order_acquire);
        return {(left + x) * scaleX *
                    PrivateStorePanelDesign().Number(PrivateStorePanelDesignKey::ReferenceWidth) /
                    viewportWidth,
                (top + y) * scaleY *
                    PrivateStorePanelDesign().Number(PrivateStorePanelDesignKey::ReferenceHeight) /
                    viewportHeight,
                width * scaleX *
                    PrivateStorePanelDesign().Number(PrivateStorePanelDesignKey::ReferenceWidth) /
                    viewportWidth,
                height * scaleY *
                    PrivateStorePanelDesign().Number(PrivateStorePanelDesignKey::ReferenceHeight) /
                    viewportHeight};
    }

    void PublishGeometry() noexcept
    {
        const RmlMuPanelPosition position = movable_.Position();
        publishedLeft_.store(position.left, std::memory_order_release);
        publishedTop_.store(position.top, std::memory_order_release);
        publishedScaleX_.store(scaleX_, std::memory_order_release);
        publishedScaleY_.store(scaleY_, std::memory_order_release);
        publishedVisible_.store(visible_, std::memory_order_release);
    }

    std::array<RmlMuButton, 3> &buttons_;
    std::array<RmlMuSlot, RmlPrivateStoreSlotCount> &slots_;
    const RmlPrivateStoreMode mode_;
    RmlDocumentHost host_;
    RmlMuMovablePanel movable_;
    Rml::Element *panel_ = nullptr;
    Rml::Element *drag_ = nullptr;
    Rml::Element *title_ = nullptr;
    Rml::Element *shopNameLabel_ = nullptr;
    Rml::Element *buyerName_ = nullptr;
    Rml::ElementFormControlInput *shopNameInput_ = nullptr;
    std::array<Rml::Element *, 3> buttonElements_{};
    std::array<Rml::Element *, 2> buttonLabels_{};
    std::array<Rml::Element *, RmlPrivateStoreSlotCount> slotElements_{};
    RmlPrivateStoreContent currentContent_{};
    std::wstring inputValue_;
    RmlPrivateStoreChanges changes_{};
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
    float scaleX_ = 1.0F;
    float scaleY_ = 1.0F;
    bool positionSet_ = false;
    bool inputDirty_ = false;
    bool visible_ = false;
    std::atomic<float> publishedLeft_{0.0F};
    std::atomic<float> publishedTop_{0.0F};
    std::atomic<float> publishedScaleX_{1.0F};
    std::atomic<float> publishedScaleY_{1.0F};
    std::atomic<bool> publishedVisible_{false};
};

RmlPrivateStorePanel::RmlPrivateStorePanel(SessionKeeper &keeper, RmlPrivateStoreMode mode)
    : impl_(std::make_unique<Impl>(keeper, buttons_, slots_, mode))
{
}

RmlPrivateStorePanel::~RmlPrivateStorePanel() = default;

void RmlPrivateStorePanel::Create()
{
    for (RmlMuButton &button : buttons_)
    {
        button.SetEnable(true);
        button.Reset();
    }
    for (RmlMuSlot &slot : slots_)
    {
        slot.SetEnable(true);
        slot.Reset();
    }
}

void RmlPrivateStorePanel::Release()
{
    impl_->Release();
}

bool RmlPrivateStorePanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event).value_or(false);
}

std::optional<bool> RmlPrivateStorePanel::RouteInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}

RmlPrivateStoreChanges RmlPrivateStorePanel::TakeChanges()
{
    return impl_->TakeChanges();
}

bool RmlPrivateStorePanel::PrepareOnWorker(int viewportWidth, int viewportHeight, bool visible,
                                           const RmlPrivateStoreContent &content)
{
    return impl_->Prepare(viewportWidth, viewportHeight, visible, content);
}

bool RmlPrivateStorePanel::Record(LegacyRenderFacade &facade) const
{
    return impl_->Record(facade);
}

RmlPrivateStoreRect RmlPrivateStorePanel::ReferenceRect(int viewportWidth,
                                                        int viewportHeight) const noexcept
{
    return impl_->ReferenceRect(viewportWidth, viewportHeight);
}

RmlPrivateStoreRect RmlPrivateStorePanel::InventoryGridRect(int viewportWidth,
                                                            int viewportHeight) const noexcept
{
    return impl_->InventoryGridRect(viewportWidth, viewportHeight);
}

std::optional<RmlTextInputArea> RmlPrivateStorePanel::TextInputArea() const
{
    return impl_->TextInputArea();
}
} // namespace UI::Modern::PC::Inventory

void CPersonalShopTitleImp::Draw()
{
    if (SessionRenderUnit *renderer = SessionOrigin().Renderer())
    {
        (void)renderer->RecordPlayerNames();
    }
}

void CPersonalShopTitleImp::StageModernLabels()
{
    modernRequest_.labels.clear();
    if (m_bShow && !m_listShopTitleDrawObj.empty())
    {
        UpdatePosition();
        RevisionPosition();
        modernRequest_.labels.reserve(m_listShopTitleDrawObj.size());
        for (const auto &[player, drawObject] : m_listShopTitleDrawObj)
        {
            UpdateHighlight(*drawObject, player);
            if (!drawObject->IsVisible())
            {
                continue;
            }

            UI::Modern::RmlPlayerName label;
            drawObject->FillModernLabel(label);
            label.storeOpen = true;
            switch (player->PK)
            {
            case PVP_CAUTION:
                label.tone = UI::Modern::RmlPlayerNameTone::Caution;
                break;
            case PVP_MURDERER1:
                label.tone = UI::Modern::RmlPlayerNameTone::Murderer1;
                break;
            case PVP_MURDERER2:
                label.tone = UI::Modern::RmlPlayerNameTone::Murderer2;
                break;
            default:
                break;
            }
            modernRequest_.labels.push_back(label);
        }
    }
    if (SessionRenderUnit *renderer = SessionOrigin().Renderer())
    {
        renderer->StagePlayerNames(modernRequest_);
    }
}

void CPersonalShopTitleImp::CShopTitleDrawObj::FillModernLabel(
    UI::Modern::RmlPlayerName &label) const
{
    std::wcsncpy(label.name.data(), m_fullname.c_str(), label.name.size() - 1);
    std::wcsncpy(label.storeTitleTop.data(), m_topTitle.c_str(), label.storeTitleTop.size() - 1);
    std::wcsncpy(label.storeTitleBottom.data(), m_bottomTitle.c_str(),
                 label.storeTitleBottom.size() - 1);
    const float scale = ModernUiScale();
    label.characterKey = m_key;
    label.anchorX = m_pos.x - static_cast<int>(
                                  std::lround(UI::Modern::RmlPlayerNameLayer::StoreLeft() * scale));
    label.anchorY =
        m_pos.y - static_cast<int>(std::lround(UI::Modern::RmlPlayerNameLayer::StoreTop() * scale));
    label.highlighted = m_bHighlight;
}

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

bool CMsgBoxIGSBuyConfirm::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    RenderMsgBackColor(true);

    RenderFrame();
    RenderTexts();
    RenderButtons();
    DisableAlphaBlend();

    return true;
}

void CMsgBoxIGSBuyConfirm::SetButtonInfo()
{
    m_BtnOk.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_OK_POS_X, GetPos().y + IGS_BTN_POS_Y,
                    IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                    CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnOk.MoveTextPos(0, -1);
    m_BtnOk.SetText(I18N::Game::OK);
    m_BtnCancel.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_CANCEL_POS_X,
                        GetPos().y + IGS_BTN_POS_Y, IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                        CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnCancel.MoveTextPos(0, -1);
    m_BtnCancel.SetText(I18N::Game::Cancel);
}

void CMsgBoxIGSBuyConfirm::RenderFrame()
{
    int iY = GetPos().y;

    RenderImage(IMAGE_IGS_BACK, GetPos().x, iY, IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_FRAME_HEIGHT);
    RenderImage(IMAGE_IGS_UP, GetPos().x, iY, IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_UP_HEIGHT);
    iY += IMAGE_IGS_UP_HEIGHT;
    for (int i = 0; i < m_iMiddleCount; ++i)
    {
        RenderImage(IMAGE_IGS_LEFTLINE, GetPos().x, iY, IMAGE_IGS_LINE_WIDTH,
                    IMAGE_IGS_LINE_HEIGHT);
        RenderImage(IMAGE_IGS_RIGHTLINE, GetPos().x + IMAGE_IGS_FRAME_WIDTH - IMAGE_IGS_LINE_WIDTH,
                    iY, IMAGE_IGS_LINE_WIDTH, IMAGE_IGS_LINE_HEIGHT);
        iY += IMAGE_IGS_LINE_HEIGHT;
    }
    RenderImage(IMAGE_IGS_DOWN, GetPos().x, iY, IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_DOWN_HEIGHT);
    RenderImage(IMAGE_IGS_TEXTBOX, GetPos().x + IGS_TEXTBOX_POS_X, GetPos().y + IGS_TEXTBOX_POS_Y,
                IMAGE_IGS_TEXTBOX_WIDTH, IMAGE_IGS_TEXTBOX_HEIGHT);
}

void CMsgBoxIGSBuyConfirm::RenderTexts()
{
    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_TITLE_POS_Y,
                            I18N::Game::PurchaseConfirmation, IMAGE_IGS_FRAME_WIDTH, 0,
                            RT3_SORT_CENTER);
    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_QUESTION_POS_Y,
                            I18N::Game::DoYouWishToBuyTheFollowingItemS, IMAGE_IGS_FRAME_WIDTH, 0,
                            RT3_SORT_CENTER);
    g_RenderText.SetTextColor(247, 186, 0, 255);
    g_RenderText.RenderText(GetPos().x + IGS_TEXT_ITEM_INFO_POS_X,
                            GetPos().y + IGS_TEXT_ITEM_INFO_NAME_POS_Y, m_szItemName,
                            IGS_TEXT_ITEM_INFO_WIDTH, 0, RT3_SORT_LEFT);
    g_RenderText.RenderText(GetPos().x + IGS_TEXT_ITEM_INFO_POS_X,
                            GetPos().y + IGS_TEXT_ITEM_INFO_PRICE_POS_Y, m_szItemPrice,
                            IGS_TEXT_ITEM_INFO_WIDTH, 0, RT3_SORT_LEFT);
    g_RenderText.RenderText(GetPos().x + IGS_TEXT_ITEM_INFO_POS_X,
                            GetPos().y + IGS_TEXT_ITEM_INFO_PERIOD_POS_Y, m_szItemPeriod,
                            IGS_TEXT_ITEM_INFO_WIDTH, 0, RT3_SORT_LEFT);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    for (int i = 0; i < m_iNumNoticeLine; i++)
    {
        g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_NOTICE_POS_Y + (i * 10),
                                m_szNotice[i], IMAGE_IGS_FRAME_WIDTH, 0, RT3_SORT_CENTER);
    }

#ifdef FOR_WORK
    wchar_t szText[256] = {
        0,
    };
    g_RenderText.SetTextColor(255, 0, 0, 255);
    if (m_wItemCode == 65535)
    {
        mu_swprintf(szText, L"Bad Item index");
    }
    else
    {
        mu_swprintf(szText, L"ItemCode : %d (%d, %d)", m_wItemCode, m_wItemCode / MAX_ITEM_INDEX,
                    m_wItemCode % MAX_ITEM_INDEX);
    }
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 10, szText, 200, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Package Seq : %d", m_iPackageSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 20, szText, 100, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Display Seq : %d", m_iDisplaySeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 30, szText, 100, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Price Seq : %d", m_iPriceSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 40, szText, 100, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"CashType : %d", m_iCashType);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 50, szText, 100, 0,
                            RT3_SORT_LEFT);
#endif // FOR_WORK
}

void CMsgBoxIGSBuyConfirm::RenderButtons()
{
    m_BtnOk.Render();
    m_BtnCancel.Render();
}

void CMsgBoxIGSBuyConfirm::LoadImages()
{
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Bt03.tga", IMAGE_IGS_BUTTON,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_IGS_BACK, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back03.tga", IMAGE_IGS_DOWN, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_top.tga", IMAGE_IGS_UP, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_back06(L).tga", IMAGE_IGS_LEFTLINE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_back06(R).tga", IMAGE_IGS_RIGHTLINE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\ingame_box.tga", IMAGE_IGS_TEXTBOX,
                LegacyTextureFilter::Linear);
}

void CMsgBoxIGSBuyConfirm::UnloadImages()
{
    DeleteBitmap(IMAGE_IGS_BUTTON);
    DeleteBitmap(IMAGE_IGS_BACK);
    DeleteBitmap(IMAGE_IGS_DOWN);
    DeleteBitmap(IMAGE_IGS_UP);
    DeleteBitmap(IMAGE_IGS_LEFTLINE);
    DeleteBitmap(IMAGE_IGS_RIGHTLINE);
    DeleteBitmap(IMAGE_IGS_TEXTBOX);
}

bool CMsgBoxIGSBuyConfirmLayout::SetLayout()
{
    CMsgBoxIGSBuyConfirm *pMsgBox = GetMsgBox();
    if (pMsgBox == nullptr)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

bool CMsgBoxIGSBuyPackageItem::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    RenderMsgBackColor(true);
    RenderFrame();
    RenderTexts();
    RenderButtons();
    RenderListBox();
    DisableAlphaBlend();
    return true;
}

void CMsgBoxIGSBuyPackageItem::RenderFrame()
{
    RenderImage(IMAGE_IGS_FRAME, GetPos().x, GetPos().y, IMAGE_IGS_FRAME_WIDTH,
                IMAGE_IGS_FRAME_HEIGHT);
}

void CMsgBoxIGSBuyPackageItem::SetButtonInfo()
{
    m_BtnBuy.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_BUY_POS_X, GetPos().y + IGS_BTN_POS_Y,
                     IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                     CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnBuy.MoveTextPos(-1, -1);
    m_BtnBuy.SetText(I18N::Game::Buy1124);

    m_BtnPresent.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_PRESENT_POS_X,
                         GetPos().y + IGS_BTN_POS_Y, IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                         CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnPresent.MoveTextPos(-1, -1);
    m_BtnPresent.SetText(I18N::Game::Gift);

    m_BtnCancel.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_CANCEL_POS_X,
                        GetPos().y + IGS_BTN_POS_Y, IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                        CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnCancel.MoveTextPos(-1, -1);
    m_BtnCancel.SetText(I18N::Game::Cancel);
}

void CMsgBoxIGSBuyPackageItem::RenderTexts()
{
    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);

    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_TITLE_POS_Y, I18N::Game::Shop,
                            IMAGE_IGS_FRAME_WIDTH, 0, RT3_SORT_CENTER);

    g_RenderText.SetTextColor(247, 186, 0, 255);

    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_NAME_POS_Y, m_szPackageName,
                            IMAGE_IGS_FRAME_WIDTH, 0, RT3_SORT_CENTER);

    g_RenderText.SetFont(LegacyFontRole::Normal);

    g_RenderText.SetTextColor(255, 238, 161, 255);

    g_RenderText.RenderText(GetPos().x + IGS_TEXT_PRICE_POS_X, GetPos().y + IGS_TEXT_PRICE_POX_Y,
                            m_szPrice, IGS_TEXT_PRICE_WIDTH, 0, RT3_SORT_RIGHT);

#ifdef FOR_WORK
    wchar_t szText[256] = {
        '\0',
    };
    g_RenderText.SetTextColor(255, 0, 0, 255);
    if (m_wItemCode == 65535)
    {
        mu_swprintf(szText, L"Package item information is not available.");
    }
    else
    {
        mu_swprintf(szText, L"ItemCode : %d (%d, %d)", m_wItemCode, m_wItemCode / MAX_ITEM_INDEX,
                    m_wItemCode % MAX_ITEM_INDEX);
    }
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 10, szText, 200, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Package Seq : %d", m_iPackageSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 20, szText, 200, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Display Seq : %d", m_iDisplaySeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 30, szText, 200, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Price Seq : 0");
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 40, szText, 200, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"CashType : %d", m_iCashType);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 50, szText, 200, 0,
                            RT3_SORT_LEFT);
#endif // FOR_WORK
}

void CMsgBoxIGSBuyPackageItem::RenderButtons()
{
    m_BtnBuy.Render();
    m_BtnPresent.Render();
    m_BtnCancel.Render();
}

void CMsgBoxIGSBuyPackageItem::Render3D()
{
    if (m_wItemCode == 65535)
        return;

    RenderItem3D(GetPos().x + IGS_3DITEM_POS_X, GetPos().y + IGS_3DITEM_POS_Y, IGS_3DITEM_WIDTH,
                 IGS_3DITEM_HEIGHT, m_wItemCode, 0, 0, 0, true);
}

void CMsgBoxIGSBuyPackageItem::LoadImages()
{
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_pack_back01.tga", IMAGE_IGS_FRAME,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Bt03.tga", IMAGE_IGS_BUTTON,
                LegacyTextureFilter::Linear);
}

void CMsgBoxIGSBuyPackageItem::UnloadImages()
{
    DeleteBitmap(IMAGE_IGS_FRAME);
    DeleteBitmap(IMAGE_IGS_BUTTON);
}

void CMsgBoxIGSBuyPackageItem::RenderListBox()
{
    if (m_PackageInfo.GetLineNum() != 0)
        m_PackageInfo.Render();
}

bool CMsgBoxBuyPackageItemLayout::SetLayout()
{
    CMsgBoxIGSBuyPackageItem *pMsgBox = GetMsgBox();
    if (pMsgBox == nullptr)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

// Create

bool CMsgBoxIGSBuySelectItem::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    RenderMsgBackColor(true);
    RenderFrame();
    RenderTexts();
    RenderButtons();
    RenderListBox();
    DisableAlphaBlend();
    return true;
}

void CMsgBoxIGSBuySelectItem::RenderFrame()
{
    RenderImage(IMAGE_IGS_MGSBOX_BACK, GetPos().x, GetPos().y, IMAGE_IGS_FRAME_WIDTH,
                IMAGE_IGS_FRAME_HEIGHT);
}

void CMsgBoxIGSBuySelectItem::RenderTexts()
{
    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_TITLE_POS_Y, I18N::Game::Shop,
                            IMAGE_IGS_FRAME_WIDTH, 0, RT3_SORT_CENTER);
    g_RenderText.SetTextColor(255, 255, 0, 255);
    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_NAME_POS_Y, m_szPackageName,
                            IMAGE_IGS_FRAME_WIDTH, 0, RT3_SORT_CENTER);
    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.SetTextColor(255, 255, 255, 255);

    for (int i = 0; i < m_iDescriptionLine; i++)
    {
        g_RenderText.RenderText(GetPos().x + IGS_TEXT_ATTR_POS_X,
                                GetPos().y + IGS_TEXT_ATTR_POS_Y + (i * 10), m_szDescription[i],
                                IGS_TEXT_ATTR_WIDTH, 0, RT3_SORT_LEFT);
    }

    g_RenderText.RenderText(GetPos().x + IGS_TEXT_PRICE_POS_X, GetPos().y + IGS_TEXT_PRICE_POX_Y,
                            m_szPrice, IGS_TEXT_PRICE_WIDTH, 0, RT3_SORT_RIGHT);

#ifdef FOR_WORK
    // debug
    wchar_t szText[256] = {
        0,
    };
    g_RenderText.SetTextColor(255, 0, 0, 255);
    if (m_wItemCode == 65535)
    {
        mu_swprintf(szText, L"Bad item index.");
    }
    else
    {
        mu_swprintf(szText, L"ItemCode : %d (%d, %d)", m_wItemCode, m_wItemCode / MAX_ITEM_INDEX,
                    m_wItemCode % MAX_ITEM_INDEX);
    }
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 10, szText, 200, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Package Seq : %d", m_iPackageSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 20, szText, 200, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Display Seq : %d", m_iDisplaySeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 30, szText, 200, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Price Seq : %d", m_SelectBuyListBox.GetSelectedText()->m_iPriceSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 40, szText, 200, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Cash Type : %d", m_SelectBuyListBox.GetSelectedText()->m_iCashType);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 50, szText, 200, 0,
                            RT3_SORT_LEFT);
#endif // FOR_WORK
}

void CMsgBoxIGSBuySelectItem::RenderButtons()
{
    m_BtnPresent.Render();
    m_BtnBuy.Render();
    m_BtnCancel.Render();
}

void CMsgBoxIGSBuySelectItem::Render3D()
{
    if (m_wItemCode == 65535)
        return;

    RenderItem3D(GetPos().x + IGS_3DITEM_POS_X, GetPos().y + IGS_3DITEM_POS_Y, IGS_3DITEM_WIDTH,
                 IGS_3DITEM_HEIGHT, m_wItemCode, 0, 0, 0, true);
}

void CMsgBoxIGSBuySelectItem::SetButtonInfo()
{
    m_BtnBuy.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_BUY_POS_X, GetPos().y + IGS_BTN_POS_Y,
                     IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                     CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnBuy.MoveTextPos(-1, -1);
    m_BtnBuy.SetText(I18N::Game::Buy1124);

    m_BtnPresent.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_PRESENT_POS_X,
                         GetPos().y + IGS_BTN_POS_Y, IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                         CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnPresent.MoveTextPos(-1, -1);
    m_BtnPresent.SetText(I18N::Game::Gift);

    m_BtnCancel.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_CANCEL_POS_X,
                        GetPos().y + IGS_BTN_POS_Y, IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                        CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnCancel.MoveTextPos(-1, -1);
    m_BtnCancel.SetText(I18N::Game::Cancel);
}

// LoadImages
void CMsgBoxIGSBuySelectItem::LoadImages()
{
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_pack_back03.tga", IMAGE_IGS_MGSBOX_BACK,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Bt03.tga", IMAGE_IGS_BUTTON,
                LegacyTextureFilter::Linear);
}

void CMsgBoxIGSBuySelectItem::UnloadImages()
{
    DeleteBitmap(IMAGE_IGS_MGSBOX_BACK);
    DeleteBitmap(IMAGE_IGS_BUTTON);
}

void CMsgBoxIGSBuySelectItem::RenderListBox()
{
    if (m_SelectBuyListBox.GetLineNum() != 0)
        m_SelectBuyListBox.Render();
}

bool CMsgBoxIGSBuySelectItemLayout::SetLayout()
{
    CMsgBoxIGSBuySelectItem *pMsgBox = GetMsgBox();
    if (pMsgBox == nullptr)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

bool CMsgBoxIGSCommon::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    RenderMsgBackColor(true);
    RenderFrame();
    RenderTexts();
    RenderButtons();
    DisableAlphaBlend();

    return true;
}

void CMsgBoxIGSCommon::SetButtonInfo()
{
    m_BtnOk.SetInfo(
        IMAGE_IGS_BUTTON, GetPos().x + (IMAGE_IGS_FRAME_WIDTH / 2) - (IMAGE_IGS_BTN_WIDTH / 2),
        (GetPos().y + m_iMsgBoxHeight) - (static_cast<int>(IMAGE_IGS_BTN_HEIGHT) + IGS_BTN_POS_Y),
        IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT, CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnOk.MoveTextPos(0, -1);
    m_BtnOk.SetText(I18N::Game::OK);
}

void CMsgBoxIGSCommon::RenderFrame()
{
    int iY = GetPos().y;

    RenderImage(IMAGE_IGS_BACK, GetPos().x, GetPos().y, m_iMsgBoxWidth, m_iMsgBoxHeight);
    RenderImage(IMAGE_IGS_UP, GetPos().x, GetPos().y, IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_UP_HEIGHT);
    iY += IMAGE_IGS_UP_HEIGHT;
    for (int i = 0; i < m_iMiddleCount; ++i)
    {
        RenderImage(IMAGE_IGS_LEFTLINE, GetPos().x, iY, IMAGE_IGS_LINE_WIDTH,
                    IMAGE_IGS_LINE_HEIGHT);
        RenderImage(IMAGE_IGS_RIGHTLINE, GetPos().x + IMAGE_IGS_FRAME_WIDTH - IMAGE_IGS_LINE_WIDTH,
                    iY, IMAGE_IGS_LINE_WIDTH, IMAGE_IGS_LINE_HEIGHT);
        iY += IMAGE_IGS_LINE_HEIGHT;
    }
    RenderImage(IMAGE_IGS_DOWN, GetPos().x, iY, IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_DOWN_HEIGHT);
}

void CMsgBoxIGSCommon::RenderTexts()
{
    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);

    // Title
    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_TITLE_POS_Y, m_szTitle,
                            IMAGE_IGS_FRAME_WIDTH, 0, RT3_SORT_CENTER);

    g_RenderText.SetFont(LegacyFontRole::Normal);

    // Text
    int iY = IGS_TEXT_ITEM_INFO_POS_Y;
    if (m_iNumTextLine <= IGS_NUM_TEXT_LIMIT_RENDER_MIDDLE_LINE)
    {
        iY = iY + ((IGS_NUM_TEXT_LIMIT_RENDER_MIDDLE_LINE - m_iNumTextLine) * 5);
    }

    for (int j = 0; j < m_iNumTextLine; ++j)
    {
        g_RenderText.RenderText(GetPos().x + IGS_TEXT_ITEM_INFO_POS_X, GetPos().y + iY + j * 12,
                                m_szText[j], IGS_TEXT_ITEM_INFO_WIDTH, 0, RT3_SORT_CENTER);
    }
}

void CMsgBoxIGSCommon::RenderButtons()
{
    m_BtnOk.Render();
}

void CMsgBoxIGSCommon::LoadImages()
{
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Bt03.tga", IMAGE_IGS_BUTTON,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_IGS_BACK, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back03.tga", IMAGE_IGS_DOWN, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_top.tga", IMAGE_IGS_UP, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_back06(L).tga", IMAGE_IGS_LEFTLINE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_back06(R).tga", IMAGE_IGS_RIGHTLINE,
                LegacyTextureFilter::Linear);
}

void CMsgBoxIGSCommon::UnloadImages()
{
    DeleteBitmap(IMAGE_IGS_BUTTON);
    DeleteBitmap(IMAGE_IGS_BACK);
    DeleteBitmap(IMAGE_IGS_DOWN);
    DeleteBitmap(IMAGE_IGS_UP);
    DeleteBitmap(IMAGE_IGS_LEFTLINE);
    DeleteBitmap(IMAGE_IGS_RIGHTLINE);
}

bool CMsgBoxIGSCommonLayout::SetLayout()
{
    CMsgBoxIGSCommon *pMsgBox = GetMsgBox();
    if (pMsgBox == nullptr)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

// Construction/Destruction

// Create

// Initialize

// Release

// Update

// Render
bool CMsgBoxIGSDeleteItemConfirm::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    RenderMsgBackColor(true);

    RenderFrame();
    RenderTexts();
    RenderButtons();
    DisableAlphaBlend();
    return true;
}

// LButtonUp

// OKButtonDown

// CancelButtonDown

// SetAddCallbackFunc

// SetButtonInfo
void CMsgBoxIGSDeleteItemConfirm::SetButtonInfo()
{
    // Ȯ�� ��ư
    m_BtnDelete.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_DEL_POS_X,
                        GetPos().y + IGS_BTN_POS_Y, IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT);
    m_BtnDelete.SetText(I18N::Game::Delete);

    // ��� ��ư
    m_BtnCancel.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_CANCEL_POS_X,
                        GetPos().y + IGS_BTN_POS_Y, IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT);
    m_BtnCancel.SetText(I18N::Game::Cancel);
}

// RenderFrame
void CMsgBoxIGSDeleteItemConfirm::RenderFrame()
{
    int iY;

    RenderImage(IMAGE_IGS_BACK, GetPos().x, GetPos().y, IMAGE_IGS_FRAME_WIDTH,
                IMAGE_IGS_FRAME_HEIGHT);
    RenderImage(IMAGE_IGS_UP, GetPos().x, GetPos().y, IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_UP_HEIGHT);

    iY = GetPos().y + IMAGE_IGS_UP_HEIGHT;

    for (int i = 0; i < m_iMiddleCount; ++i)
    {
        RenderImage(IMAGE_IGS_LEFTLINE, GetPos().x, iY, IMAGE_IGS_LINE_WIDTH,
                    IMAGE_IGS_LINE_HEIGHT);
        RenderImage(IMAGE_IGS_RIGHTLINE, GetPos().x + IMAGE_IGS_FRAME_WIDTH - IMAGE_IGS_LINE_WIDTH,
                    iY, IMAGE_IGS_LINE_WIDTH, IMAGE_IGS_LINE_HEIGHT);
        iY += IMAGE_IGS_LINE_HEIGHT;
    }
    RenderImage(IMAGE_IGS_DOWN, GetPos().x, iY, IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_DOWN_HEIGHT);
}

// RenderTexts
void CMsgBoxIGSDeleteItemConfirm::RenderTexts()
{
    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);

    // Title - "������ ����"
    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_TITLE_Y, I18N::Game::DeleteItem,
                            IMAGE_IGS_FRAME_WIDTH, 0, RT3_SORT_CENTER);

    g_RenderText.SetFont(LegacyFontRole::Normal);

    // Decription
    for (int i = 0; i < m_iDesciptionLine; ++i)
    {
        g_RenderText.RenderText(GetPos().x + IGS_TEXT_DESCRIPTION_POS_X,
                                GetPos().y + IGS_TEXT_DESCRIPTION_POS_Y +
                                    (i * IGS_TEXT_DESCRIPTION_INTERVAL),
                                m_szDescription[i], IGS_TEXT_DESCRIPTION_WIDTH, 0, RT3_SORT_LEFT);
    }

#ifdef FOR_WORK
    wchar_t szText[256] = {
        0,
    };
    g_RenderText.SetTextColor(255, 0, 0, 255);
    mu_swprintf(szText, L"m_iStorageSeq : %d", m_iStorageSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 10, szText, 150, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"m_iStorageItemSeq : %d", m_iStorageItemSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 20, szText, 150, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"m_szItemType : %c", m_szItemType);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 30, szText, 150, 0,
                            RT3_SORT_LEFT);
#endif // FOR_WORK
}

// RenderButtons
void CMsgBoxIGSDeleteItemConfirm::RenderButtons()
{
    m_BtnDelete.Render();
    m_BtnCancel.Render();
}

// LoadImages
void CMsgBoxIGSDeleteItemConfirm::LoadImages()
{
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Bt03.tga", IMAGE_IGS_BUTTON,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_IGS_BACK, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back03.tga", IMAGE_IGS_DOWN, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_top.tga", IMAGE_IGS_UP, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_back06(L).tga", IMAGE_IGS_LEFTLINE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_back06(R).tga", IMAGE_IGS_RIGHTLINE,
                LegacyTextureFilter::Linear);
}

// UnloadImages
void CMsgBoxIGSDeleteItemConfirm::UnloadImages()
{
    DeleteBitmap(IMAGE_IGS_BUTTON);
    DeleteBitmap(IMAGE_IGS_BACK);
    DeleteBitmap(IMAGE_IGS_DOWN);
    DeleteBitmap(IMAGE_IGS_UP);
    DeleteBitmap(IMAGE_IGS_LEFTLINE);
    DeleteBitmap(IMAGE_IGS_RIGHTLINE);
}

// LayOut
bool CMsgBoxIGSDeleteItemConfirmLayout::SetLayout()
{
    CMsgBoxIGSDeleteItemConfirm *pMsgBox = GetMsgBox();
    if (pMsgBox == nullptr)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

// Construction/Destruction

// Create

// IsVisible

// Initialize

// Release

// Update

// Render
bool CMsgBoxIGSGiftStorageItemInfo::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    RenderMsgBackColor(true);

    RenderFrame();
    RenderTexts();
    RenderButtons();

    m_MessageInputBox.Render();

    DisableAlphaBlend();
    return true;
}

// Render3D
void CMsgBoxIGSGiftStorageItemInfo::Render3D()
{
    if (m_wItemCode == 65535)
        return;

    RenderItem3D(GetPos().x + IGS_3DITEM_POS_X, GetPos().y + IGS_3DITEM_POS_Y, IGS_3DITEM_WIDTH,
                 IGS_3DITEM_HEIGHT, m_wItemCode, 0, 0, 0, true);
}

// SetAddCallbackFunc

// LButtonUp

// OKButtonDown

// CancelButtonDown

// SetButtonInfo
void CMsgBoxIGSGiftStorageItemInfo::SetButtonInfo()
{
    // Ȯ�� ��ư
    m_BtnUse.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_OK_POS_X,
                     GetPos().y + IGS_BTN_POS_Y + 102, IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                     CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnUse.MoveTextPos(0, -1);
    m_BtnUse.SetText(I18N::Game::OK);

    // ��� ��ư
    m_BtnCancel.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_CANCEL_POS_X,
                        GetPos().y + IGS_BTN_POS_Y + 102, IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                        CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnCancel.MoveTextPos(0, -1);
    m_BtnCancel.SetText(I18N::Game::Cancel);
}

// RenderFrame
void CMsgBoxIGSGiftStorageItemInfo::RenderFrame()
{
    RenderImage(IMAGE_IGS_FRAME, GetPos().x, GetPos().y, IMAGE_IGS_FRAME_WIDTH,
                IMAGE_IGS_FRAME_HEIGHT);
}

// RenderTexts
void CMsgBoxIGSGiftStorageItemInfo::RenderTexts()
{
    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);

    // Title "���� ����â"
    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_TITLE_POS_Y,
                            I18N::Game::GiftInfoWindow, IMAGE_IGS_FRAME_WIDTH, 0, RT3_SORT_CENTER);

    // Item Name
    g_RenderText.SetTextColor(255, 255, 0, 255);
    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_ITEM_NAME_POS_Y, m_szName,
                            IMAGE_IGS_FRAME_WIDTH, 0, RT3_SORT_CENTER);

    // ID Info
    g_RenderText.SetTextColor(0, 0, 0, 255);
    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_ID_INFO_POS_Y, m_szIDInfo,
                            IMAGE_IGS_FRAME_WIDTH, 0, RT3_SORT_CENTER);

    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.SetTextColor(255, 255, 255, 255);

    // Item Info
    g_RenderText.RenderText(GetPos().x + IGS_TEXT_ITEM_INFO_POS_X,
                            GetPos().y + IGS_TEXT_ITEM_INFO_NUM_POS_Y, m_szNum,
                            IGS_TEXT_ITEM_INFO_WIDTH, 0, RT3_SORT_LEFT);
    g_RenderText.RenderText(GetPos().x + IGS_TEXT_ITEM_INFO_POS_X,
                            GetPos().y + IGS_TEXT_ITEM_INFO_PERIOD_POS_Y, m_szPeriod,
                            IGS_TEXT_ITEM_INFO_WIDTH, 0, RT3_SORT_LEFT);

#ifdef FOR_WORK
    // debug
    wchar_t szText[256] = {
        0,
    };
    g_RenderText.SetTextColor(255, 0, 0, 255);
    if (m_wItemCode == 65535)
    {
        mu_swprintf(szText, L"Package item information is not available.");
    }
    else
    {
        mu_swprintf(szText, L"ItemCode : %d (%d, %d)", m_wItemCode, m_wItemCode / MAX_ITEM_INDEX,
                    m_wItemCode % MAX_ITEM_INDEX);
    }
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 10, szText, 150, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Storage Seq : %d", m_iStorageSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 20, szText, 150, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Storage ItemSeq : %d", m_iStorageItemSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 30, szText, 150, 0,
                            RT3_SORT_LEFT);
#endif // FOR_WORK
}

// RenderButtons
void CMsgBoxIGSGiftStorageItemInfo::RenderButtons()
{
    m_BtnUse.Render();
    m_BtnCancel.Render();
}

// LoadImages
void CMsgBoxIGSGiftStorageItemInfo::LoadImages()
{
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Bt03.tga", IMAGE_IGS_BUTTON,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\ingame_Box_List_B.tga", IMAGE_IGS_FRAME,
                LegacyTextureFilter::Linear);
}

// UnloadImages
void CMsgBoxIGSGiftStorageItemInfo::UnloadImages()
{
    DeleteBitmap(IMAGE_IGS_BUTTON);
    DeleteBitmap(IMAGE_IGS_FRAME);
}

// LayOut
bool CMsgBoxIGSGiftStorageItemInfoLayout::SetLayout()
{
    CMsgBoxIGSGiftStorageItemInfo *pMsgBox = GetMsgBox();
    if (pMsgBox == nullptr)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

bool CMsgBoxIGSSendGift::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    RenderMsgBackColor(true);

    RenderFrame();
    RenderTexts();
    RenderButtons();

    m_IDInputBox.Render();
    m_MessageInputBox.Render();

    DisableAlphaBlend();
    return true;
}

void CMsgBoxIGSSendGift::SetButtonInfo()
{
    m_BtnOk.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_OK_POS_X, GetPos().y + IGS_BTN_POS_Y,
                    IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                    CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnOk.MoveTextPos(0, -1);
    m_BtnOk.SetText(I18N::Game::OK);

    m_BtnCancel.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_CANCEL_POS_X,
                        GetPos().y + IGS_BTN_POS_Y, IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                        CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnCancel.MoveTextPos(0, -1);
    m_BtnCancel.SetText(I18N::Game::Cancel);
}

void CMsgBoxIGSSendGift::RenderFrame()
{
    RenderImage(IMAGE_IGS_FRAME, GetPos().x, GetPos().y, IMAGE_IGS_FRAME_WIDTH,
                IMAGE_IGS_FRAME_HEIGHT);
    RenderImage(IMAGE_IGS_DECO, GetPos().x + IMAGE_IGS_DECO_POS_X,
                GetPos().y + IMAGE_IGS_DECO_POS_Y, IMAGE_IGS_DECO_WIDTH, IMAGE_IGS_DECO_HEIGHT);
    RenderImage(IMAGE_IGS_INPUTTEXT, GetPos().x + IMAGE_IGS_ID_INPUT_BOX_POS_X,
                GetPos().y + IMAGE_IGS_ID_INPUT_BOX_POS_Y, IMAGE_IGS_ID_INPUT_BOX_WIDTH,
                IMAGE_IGS_ID_INPUT_BOX_HEIGHT);
}

void CMsgBoxIGSSendGift::RenderTexts()
{
    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);

    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_TITLE_POS_Y,
                            I18N::Game::SendGiftItems, IMAGE_IGS_FRAME_WIDTH, 0, RT3_SORT_CENTER);

    g_RenderText.RenderText(
        GetPos().x + IGS_TEXT_ID_TITLE_POS_X, GetPos().y + IGS_TEXT_ID_TITLE_POS_Y,
        I18N::Game::RecipientSCharacterName, IGS_TEXT_ID_TITLE_WIDTH, 0, RT3_SORT_LEFT);

    g_RenderText.SetTextColor(0, 0, 0, 255);
    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_MESSAGE_TITLE_POS_Y,
                            I18N::Game::MessageToTheRecipient, IMAGE_IGS_FRAME_WIDTH, 0,
                            RT3_SORT_CENTER);

    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.SetTextColor(255, 255, 255, 255);

    g_RenderText.SetTextColor(247, 186, 0, 255);
    g_RenderText.RenderText(GetPos().x + IGS_TEXT_ITEM_INFO_POS_X,
                            GetPos().y + IGS_TEXT_ITEM_INFO_NAME_POS_Y, m_szName,
                            IGS_TEXT_ITEM_INFO_WIDTH, 0, RT3_SORT_LEFT);
    g_RenderText.RenderText(GetPos().x + IGS_TEXT_ITEM_INFO_POS_X,
                            GetPos().y + IGS_TEXT_ITEM_INFO_PRICE_POS_Y, m_szPrice,
                            IGS_TEXT_ITEM_INFO_WIDTH, 0, RT3_SORT_LEFT);
    g_RenderText.RenderText(GetPos().x + IGS_TEXT_ITEM_INFO_POS_X,
                            GetPos().y + IGS_TEXT_ITEM_INFO_PERIOD_POS_Y, m_szPeriod,
                            IGS_TEXT_ITEM_INFO_WIDTH, 0, RT3_SORT_LEFT);

    g_RenderText.SetTextColor(255, 255, 255, 255);
    for (int i = 0; i < m_iNumNoticeLine; i++)
    {
        g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_NOTICE_POS_Y + i * 10,
                                m_szNotice[i], IMAGE_IGS_FRAME_WIDTH, 0, RT3_SORT_CENTER);
    }

#ifdef FOR_WORK
    wchar_t szText[256] = {
        0,
    };
    g_RenderText.SetTextColor(255, 0, 0, 255);
    mu_swprintf(szText, L"Package Seq : %d", m_iPackageSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 10, szText, 200, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Display Seq : %d", m_iDisplaySeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 20, szText, 200, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Price Seq : %d", m_iPriceSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 30, szText, 200, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"ItemCode : %d", m_wItemCode);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 40, szText, 200, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"CashType : %d", m_iCashType);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 50, szText, 200, 0,
                            RT3_SORT_LEFT);
#endif // FOR_WORK
}

void CMsgBoxIGSSendGift::RenderButtons()
{
    m_BtnOk.Render();
    m_BtnCancel.Render();
}

void CMsgBoxIGSSendGift::LoadImages()
{
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Bt03.tga", IMAGE_IGS_BUTTON,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\ingame_gift_back01.tga", IMAGE_IGS_FRAME,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\ingame_gift_icon.tga", IMAGE_IGS_DECO,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\ingame_gift_namebox.tga", IMAGE_IGS_INPUTTEXT,
                LegacyTextureFilter::Linear);
}

void CMsgBoxIGSSendGift::UnloadImages()
{
    DeleteBitmap(IMAGE_IGS_BUTTON);
    DeleteBitmap(IMAGE_IGS_FRAME);
    DeleteBitmap(IMAGE_IGS_DECO);
    DeleteBitmap(IMAGE_IGS_INPUTTEXT);
}

bool CMsgBoxIGSSendGiftLayout::SetLayout()
{
    CMsgBoxIGSSendGift *pMsgBox = GetMsgBox();
    if (pMsgBox == nullptr)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

bool CMsgBoxIGSSendGiftConfirm::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    RenderMsgBackColor(true);

    RenderFrame();
    RenderTexts();
    RenderButtons();
    DisableAlphaBlend();

    return true;
}

void CMsgBoxIGSSendGiftConfirm::SetButtonInfo()
{
    m_BtnOk.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_OK_POS_X, GetPos().y + IGS_BTN_POS_Y,
                    IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                    CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnOk.MoveTextPos(0, -1);
    m_BtnOk.SetText(I18N::Game::OK);

    m_BtnCancel.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_CANCEL_POS_X,
                        GetPos().y + IGS_BTN_POS_Y, IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                        CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnCancel.MoveTextPos(0, -1);
    m_BtnCancel.SetText(I18N::Game::Cancel);
}

void CMsgBoxIGSSendGiftConfirm::RenderFrame()
{
    int iY = GetPos().y;

    RenderImage(IMAGE_IGS_BACK, GetPos().x, iY, IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_FRAME_HEIGHT);
    RenderImage(IMAGE_IGS_UP, GetPos().x, iY, IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_UP_HEIGHT);
    iY += IMAGE_IGS_UP_HEIGHT;
    for (int i = 0; i < m_iMiddleCount; ++i)
    {
        RenderImage(IMAGE_IGS_LEFTLINE, GetPos().x, iY, IMAGE_IGS_LINE_WIDTH,
                    IMAGE_IGS_LINE_HEIGHT);
        RenderImage(IMAGE_IGS_RIGHTLINE, GetPos().x + IMAGE_IGS_FRAME_WIDTH - IMAGE_IGS_LINE_WIDTH,
                    iY, IMAGE_IGS_LINE_WIDTH, IMAGE_IGS_LINE_HEIGHT);
        iY += IMAGE_IGS_LINE_HEIGHT;
    }
    RenderImage(IMAGE_IGS_DOWN, GetPos().x, iY, IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_DOWN_HEIGHT);
    RenderImage(IMAGE_IGS_TEXTBOX, GetPos().x + IGS_TEXTBOX_POS_X, GetPos().y + IGS_TEXTBOX_POS_Y,
                IMAGE_IGS_TEXTBOX_WIDTH, IMAGE_IGS_TEXTBOX_HEIGHT);
}

void CMsgBoxIGSSendGiftConfirm::RenderTexts()
{
    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);

    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_TITLE_POS_Y,
                            I18N::Game::GiftConfirmation, IMAGE_IGS_FRAME_WIDTH, 0,
                            RT3_SORT_CENTER);

    g_RenderText.SetFont(LegacyFontRole::Normal);

    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_QUESTION_POS_Y,
                            I18N::Game::DoYouWantToGiftTheFollowingItemS, IMAGE_IGS_FRAME_WIDTH, 0,
                            RT3_SORT_CENTER);

    g_RenderText.SetTextColor(247, 186, 0, 255);
    g_RenderText.RenderText(GetPos().x + IGS_TEXT_ITEM_INFO_POS_X,
                            GetPos().y + IGS_TEXT_ITEM_INFO_NAME_POS_Y, m_szItemName,
                            IGS_TEXT_ITEM_INFO_WIDTH, 0, RT3_SORT_LEFT);
    g_RenderText.RenderText(GetPos().x + IGS_TEXT_ITEM_INFO_POS_X,
                            GetPos().y + IGS_TEXT_ITEM_INFO_PRICE_POS_Y, m_szItemPrice,
                            IGS_TEXT_ITEM_INFO_WIDTH, 0, RT3_SORT_LEFT);
    g_RenderText.RenderText(GetPos().x + IGS_TEXT_ITEM_INFO_POS_X,
                            GetPos().y + IGS_TEXT_ITEM_INFO_PERIOD_POS_Y, m_szItemPeriod,
                            IGS_TEXT_ITEM_INFO_WIDTH, 0, RT3_SORT_LEFT);

    g_RenderText.SetTextColor(255, 255, 255, 255);
    for (int i = 0; i < m_iNumNoticeLine; i++)
    {
        g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_NOTICE_POS_Y + (i * 10),
                                m_szNotice[i], IMAGE_IGS_FRAME_WIDTH, 0, RT3_SORT_CENTER);
    }

#ifdef FOR_WORK
    wchar_t szText[256] = {
        0,
    };
    g_RenderText.SetTextColor(255, 0, 0, 255);
    mu_swprintf(szText, L"Package Seq : %d", m_iPackageSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 10, szText, 200, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Display Seq : %d", m_iDisplaySeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 20, szText, 200, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Price Seq : %d", m_iPriceSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 30, szText, 200, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"ItemCode : %d", m_wItemCode);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 40, szText, 200, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"CashType : %d", m_iCashType);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 50, szText, 200, 0,
                            RT3_SORT_LEFT);
#endif // FOR_WORK
}

void CMsgBoxIGSSendGiftConfirm::RenderButtons()
{
    m_BtnOk.Render();
    m_BtnCancel.Render();
}

void CMsgBoxIGSSendGiftConfirm::LoadImages()
{
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Bt03.tga", IMAGE_IGS_BUTTON,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_IGS_BACK, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back03.tga", IMAGE_IGS_DOWN, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_top.tga", IMAGE_IGS_UP, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_back06(L).tga", IMAGE_IGS_LEFTLINE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_back06(R).tga", IMAGE_IGS_RIGHTLINE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\ingame_box.tga", IMAGE_IGS_TEXTBOX,
                LegacyTextureFilter::Linear);
}

void CMsgBoxIGSSendGiftConfirm::UnloadImages()
{
    DeleteBitmap(IMAGE_IGS_BUTTON);
    DeleteBitmap(IMAGE_IGS_BACK);
    DeleteBitmap(IMAGE_IGS_DOWN);
    DeleteBitmap(IMAGE_IGS_UP);
    DeleteBitmap(IMAGE_IGS_LEFTLINE);
    DeleteBitmap(IMAGE_IGS_RIGHTLINE);
    DeleteBitmap(IMAGE_IGS_TEXTBOX);
}

bool CMsgBoxIGSSendGiftConfirmLayout::SetLayout()
{
    CMsgBoxIGSSendGiftConfirm *pMsgBox = GetMsgBox();
    if (pMsgBox == nullptr)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

bool CMsgBoxIGSStorageItemInfo::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    RenderMsgBackColor(true);
    RenderFrame();
    RenderTexts();
    RenderButtons();
    DisableAlphaBlend();
    return true;
}

void CMsgBoxIGSStorageItemInfo::Render3D()
{
    if (m_wItemCode == 65535)
        return;

    RenderItem3D(GetPos().x + IGS_3DITEM_POS_X, GetPos().y + IGS_3DITEM_POS_Y, IGS_3DITEM_WIDTH,
                 IGS_3DITEM_HEIGHT, m_wItemCode, 0, 0, 0, true);
}

void CMsgBoxIGSStorageItemInfo::SetButtonInfo()
{
    m_BtnUse.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_OK_POS_X, GetPos().y + IGS_BTN_POS_Y,
                     IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                     CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnUse.MoveTextPos(0, -1);
    m_BtnUse.SetText(I18N::Game::OK);
    m_BtnCancel.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_CANCEL_POS_X,
                        GetPos().y + IGS_BTN_POS_Y, IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                        CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnCancel.MoveTextPos(0, -1);
    m_BtnCancel.SetText(I18N::Game::Cancel);
}

void CMsgBoxIGSStorageItemInfo::RenderFrame()
{
    if (m_wItemCode == 65535)
        return;

    RenderImage(IMAGE_IGS_FRAME, GetPos().x, GetPos().y, IMAGE_IGS_FRAME_WIDTH,
                IMAGE_IGS_FRAME_HEIGHT);
}

void CMsgBoxIGSStorageItemInfo::RenderTexts()
{
    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);

    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_TITLE_POS_Y,
                            I18N::Game::ItemInfoWindow, IMAGE_IGS_FRAME_WIDTH, 0, RT3_SORT_CENTER);

    g_RenderText.SetTextColor(255, 255, 0, 255);
    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_ITEM_NAME_POS_Y, m_szName,
                            IMAGE_IGS_FRAME_WIDTH, 0, RT3_SORT_CENTER);

    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.SetTextColor(255, 255, 255, 255);

    g_RenderText.RenderText(GetPos().x + IGS_TEXT_ITEM_INFO_POS_X,
                            GetPos().y + IGS_TEXT_ITEM_INFO_NUM_POS_Y, m_szNum,
                            IGS_TEXT_ITEM_INFO_WIDTH, 0, RT3_SORT_LEFT);
    g_RenderText.RenderText(GetPos().x + IGS_TEXT_ITEM_INFO_POS_X,
                            GetPos().y + IGS_TEXT_ITEM_INFO_PERIOD_POS_Y, m_szPeriod,
                            IGS_TEXT_ITEM_INFO_WIDTH, 0, RT3_SORT_LEFT);

#ifdef FOR_WORK
    // debug
    wchar_t szText[256] = {
        0,
    };
    g_RenderText.SetTextColor(255, 0, 0, 255);
    if (m_wItemCode == 65535)
    {
        mu_swprintf(szText, L"Bad Item Index");
    }
    else
    {
        mu_swprintf(szText, L"ItemCode : %d (%d, %d)", m_wItemCode, m_wItemCode / MAX_ITEM_INDEX,
                    m_wItemCode % MAX_ITEM_INDEX);
    }
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 10, szText, 150, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Storage Seq : %d", m_iStorageSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 20, szText, 150, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"Storage ItemSeq : %d", m_iStorageItemSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 30, szText, 150, 0,
                            RT3_SORT_LEFT);
#endif // FOR_WORK
}

void CMsgBoxIGSStorageItemInfo::RenderButtons()
{
    m_BtnUse.Render();
    m_BtnCancel.Render();
}

void CMsgBoxIGSStorageItemInfo::LoadImages()
{
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Bt03.tga", IMAGE_IGS_BUTTON,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\ingame_Box_List_A.tga", IMAGE_IGS_FRAME,
                LegacyTextureFilter::Linear);
}

void CMsgBoxIGSStorageItemInfo::UnloadImages()
{
    DeleteBitmap(IMAGE_IGS_BUTTON);
    DeleteBitmap(IMAGE_IGS_FRAME);
}

bool CMsgBoxIGSStorageItemInfoLayout::SetLayout()
{
    CMsgBoxIGSStorageItemInfo *pMsgBox = GetMsgBox();
    if (pMsgBox == nullptr)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

bool CMsgBoxIGSUseBuffConfirm::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    RenderMsgBackColor(true);
    RenderFrame();
    RenderTexts();
    RenderButtons();
    DisableAlphaBlend();
    return true;
}

void CMsgBoxIGSUseBuffConfirm::SetButtonInfo()
{
    m_BtnOk.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_OK_POS_X, GetPos().y + IGS_BTN_POS_Y,
                    IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                    CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnOk.MoveTextPos(0, -1);
    m_BtnOk.SetText(I18N::Game::OK);

    m_BtnCancel.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_CANCEL_POS_X,
                        GetPos().y + IGS_BTN_POS_Y, IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                        CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnCancel.MoveTextPos(0, -1);
    m_BtnCancel.SetText(I18N::Game::Cancel);
}

void CMsgBoxIGSUseBuffConfirm::RenderFrame()
{
    int iY;

    RenderImage(IMAGE_IGS_BACK, GetPos().x, GetPos().y, IMAGE_IGS_FRAME_WIDTH,
                IMAGE_IGS_FRAME_HEIGHT);
    RenderImage(IMAGE_IGS_UP, GetPos().x, GetPos().y, IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_UP_HEIGHT);

    iY = GetPos().y + IMAGE_IGS_UP_HEIGHT;

    for (int i = 0; i < m_iMiddleCount; ++i)
    {
        RenderImage(IMAGE_IGS_LEFTLINE, GetPos().x, iY, IMAGE_IGS_LINE_WIDTH,
                    IMAGE_IGS_LINE_HEIGHT);
        RenderImage(IMAGE_IGS_RIGHTLINE, GetPos().x + IMAGE_IGS_FRAME_WIDTH - IMAGE_IGS_LINE_WIDTH,
                    iY, IMAGE_IGS_LINE_WIDTH, IMAGE_IGS_LINE_HEIGHT);
        iY += IMAGE_IGS_LINE_HEIGHT;
    }

    RenderImage(IMAGE_IGS_DOWN, GetPos().x, iY, IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_DOWN_HEIGHT);
}

void CMsgBoxIGSUseBuffConfirm::RenderTexts()
{
    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);

    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_TITLE_Y,
                            I18N::Game::BuffItemUseConfirmation, IMAGE_IGS_FRAME_WIDTH, 0,
                            RT3_SORT_CENTER);

    g_RenderText.SetFont(LegacyFontRole::Normal);

    // Decription
    for (int i = 0; i < m_iDesciptionLine; ++i)
    {
        g_RenderText.RenderText(GetPos().x + IGS_TEXT_DESCRIPTION_POS_X,
                                GetPos().y + IGS_TEXT_DESCRIPTION_POS_Y +
                                    (i * IGS_TEXT_DESCRIPTION_INTERVAL),
                                m_szDescription[i], IGS_TEXT_DESCRIPTION_WIDTH, 0, RT3_SORT_LEFT);
    }

#ifdef FOR_WORK
    wchar_t szText[256] = {
        0,
    };
    g_RenderText.SetTextColor(255, 0, 0, 255);
    mu_swprintf(szText, L"m_iStorageSeq : %d", m_iStorageSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 10, szText, 150, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"m_iStorageItemSeq : %d", m_iStorageItemSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 20, szText, 150, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"m_wItemCode : %d", m_wItemCode);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 30, szText, 150, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"m_szItemType : %c", m_szItemType);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 40, szText, 150, 0,
                            RT3_SORT_LEFT);
#endif // FOR_WORK
}

void CMsgBoxIGSUseBuffConfirm::RenderButtons()
{
    m_BtnOk.Render();
    m_BtnCancel.Render();
}

void CMsgBoxIGSUseBuffConfirm::LoadImages()
{
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Bt03.tga", IMAGE_IGS_BUTTON,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_IGS_BACK, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back03.tga", IMAGE_IGS_DOWN, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_top.tga", IMAGE_IGS_UP, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_back06(L).tga", IMAGE_IGS_LEFTLINE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_back06(R).tga", IMAGE_IGS_RIGHTLINE,
                LegacyTextureFilter::Linear);
}

void CMsgBoxIGSUseBuffConfirm::UnloadImages()
{
    DeleteBitmap(IMAGE_IGS_BUTTON);
    DeleteBitmap(IMAGE_IGS_BACK);
    DeleteBitmap(IMAGE_IGS_DOWN);
    DeleteBitmap(IMAGE_IGS_UP);
    DeleteBitmap(IMAGE_IGS_LEFTLINE);
    DeleteBitmap(IMAGE_IGS_RIGHTLINE);
}

bool CMsgBoxIGSUseBuffConfirmLayout::SetLayout()
{
    CMsgBoxIGSUseBuffConfirm *pMsgBox = GetMsgBox();
    if (pMsgBox == nullptr)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

// Construction/Destruction

// Create

// Initialize

// Release

// Update

// Render
bool CMsgBoxIGSUseItemConfirm::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    RenderMsgBackColor(true);

    RenderFrame();
    RenderTexts();
    RenderButtons();
    DisableAlphaBlend();
    return true;
}

// LButtonUp

// OKButtonDown

// CancelButtonDown

// SetAddCallbackFunc

// SetButtonInfo
void CMsgBoxIGSUseItemConfirm::SetButtonInfo()
{
    // Ȯ�� ��ư
    m_BtnOk.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_OK_POS_X, GetPos().y + IGS_BTN_POS_Y,
                    IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                    CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnOk.MoveTextPos(0, -1);
    m_BtnOk.SetText(I18N::Game::OK);

    // ��� ��ư
    m_BtnCancel.SetInfo(IMAGE_IGS_BUTTON, GetPos().x + IGS_BTN_CANCEL_POS_X,
                        GetPos().y + IGS_BTN_POS_Y, IMAGE_IGS_BTN_WIDTH, IMAGE_IGS_BTN_HEIGHT,
                        CNewUIMessageBoxButton::MSGBOX_BTN_CUSTOM, true);
    m_BtnCancel.MoveTextPos(0, -1);
    m_BtnCancel.SetText(I18N::Game::Cancel);
}

// RenderFrame
void CMsgBoxIGSUseItemConfirm::RenderFrame()
{
    int iY;

    RenderImage(IMAGE_IGS_BACK, GetPos().x, GetPos().y, IMAGE_IGS_FRAME_WIDTH,
                IMAGE_IGS_FRAME_HEIGHT);
    RenderImage(IMAGE_IGS_UP, GetPos().x, GetPos().y, IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_UP_HEIGHT);

    iY = GetPos().y + IMAGE_IGS_UP_HEIGHT;

    for (int i = 0; i < m_iMiddleCount; ++i)
    {
        RenderImage(IMAGE_IGS_LEFTLINE, GetPos().x, iY, IMAGE_IGS_LINE_WIDTH,
                    IMAGE_IGS_LINE_HEIGHT);
        RenderImage(IMAGE_IGS_RIGHTLINE, GetPos().x + IMAGE_IGS_FRAME_WIDTH - IMAGE_IGS_LINE_WIDTH,
                    iY, IMAGE_IGS_LINE_WIDTH, IMAGE_IGS_LINE_HEIGHT);
        iY += IMAGE_IGS_LINE_HEIGHT;
    }

    RenderImage(IMAGE_IGS_DOWN, GetPos().x, iY, IMAGE_IGS_FRAME_WIDTH, IMAGE_IGS_DOWN_HEIGHT);
}

// RenderTexts
void CMsgBoxIGSUseItemConfirm::RenderTexts()
{
    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);

    // Title - "��� Ȯ��"
    g_RenderText.RenderText(GetPos().x, GetPos().y + IGS_TEXT_TITLE_Y, I18N::Game::UseConfirmation,
                            IMAGE_IGS_FRAME_WIDTH, 0, RT3_SORT_CENTER);

    g_RenderText.SetFont(LegacyFontRole::Normal);

    // Decription
    for (int i = 0; i < m_iDesciptionLine; ++i)
    {
        g_RenderText.RenderText(GetPos().x + IGS_TEXT_DESCRIPTION_POS_X,
                                GetPos().y + IGS_TEXT_DESCRIPTION_POS_Y +
                                    (i * IGS_TEXT_DESCRIPTION_INTERVAL),
                                m_szDescription[i], IGS_TEXT_DESCRIPTION_WIDTH, 0, RT3_SORT_LEFT);
    }

#ifdef FOR_WORK
    wchar_t szText[256] = {
        0,
    };
    g_RenderText.SetTextColor(255, 0, 0, 255);
    mu_swprintf(szText, L"m_iStorageSeq : %d", m_iStorageSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 10, szText, 150, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"m_iStorageItemSeq : %d", m_iStorageItemSeq);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 20, szText, 150, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"m_wItemCode : %d", m_wItemCode);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 30, szText, 150, 0,
                            RT3_SORT_LEFT);
    mu_swprintf(szText, L"m_szItemType : %c", m_szItemType);
    g_RenderText.RenderText(GetPos().x + IMAGE_IGS_FRAME_WIDTH, GetPos().y + 40, szText, 150, 0,
                            RT3_SORT_LEFT);
#endif // FOR_WORK
}

// RenderButtons
void CMsgBoxIGSUseItemConfirm::RenderButtons()
{
    m_BtnOk.Render();
    m_BtnCancel.Render();
}

// LoadImages
void CMsgBoxIGSUseItemConfirm::LoadImages()
{
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Bt03.tga", IMAGE_IGS_BUTTON,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_IGS_BACK, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back03.tga", IMAGE_IGS_DOWN, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_top.tga", IMAGE_IGS_UP, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_back06(L).tga", IMAGE_IGS_LEFTLINE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_option_back06(R).tga", IMAGE_IGS_RIGHTLINE,
                LegacyTextureFilter::Linear);
}

// UnloadImages
void CMsgBoxIGSUseItemConfirm::UnloadImages()
{
    DeleteBitmap(IMAGE_IGS_BUTTON);
    DeleteBitmap(IMAGE_IGS_BACK);
    DeleteBitmap(IMAGE_IGS_DOWN);
    DeleteBitmap(IMAGE_IGS_UP);
    DeleteBitmap(IMAGE_IGS_LEFTLINE);
    DeleteBitmap(IMAGE_IGS_RIGHTLINE);
}

// LayOut
bool CMsgBoxIGSUseItemConfirmLayout::SetLayout()
{
    CMsgBoxIGSUseItemConfirm *pMsgBox = GetMsgBox();
    if (pMsgBox == nullptr)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef PBG_ADD_INGAMESHOP_UI_ITEMSHOP

using namespace SEASON3B;

void CNewUIInGameShop::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool CNewUIInGameShop::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    RenderFrame();
    RenderButtons();
    RenderTexts();
    RenderBanner();
    RenderListBox();
    RenderDisplayItems();
    DisableAlphaBlend();
    return true;
}

void CNewUIInGameShop::RenderFrame()
{
    RenderImage(IMAGE_IGS_BACK, m_Pos.x, m_Pos.y, IMAGE_IGS_BACK_WIDTH, IMAGE_IGS_BACK_HEIGHT);

    int iSizeCategory = g_InGameShopSystem.GetSizeCategoriesAsSelectedZone();

    if (iSizeCategory < 0)
        return;

    // Category Deco Middle Render
    POINT CategoryDecoMiddlePos;
    CategoryDecoMiddlePos.x = m_CategoryButton.GetPos(0).x + (IMAGE_IGS_CATEGORY_BTN_WIDTH / 2) -
                              (IMAGE_IGS_CATEGORY_DECO_MIDDLE_WIDTH / 2);

    for (int i = 0; i < iSizeCategory - 1; i++)
    {
        CategoryDecoMiddlePos.y = m_CategoryButton.GetPos(i).y + IMAGE_IGS_CATEGORY_BTN_HEIGHT - 1;

        RenderImage(IMAGE_IGS_CATEGORY_DECO_MIDDLE, CategoryDecoMiddlePos.x,
                    CategoryDecoMiddlePos.y, IMAGE_IGS_CATEGORY_DECO_MIDDLE_WIDTH,
                    IMAGE_IGS_CATEGORY_DECO_MIDDLE_HEIGHT);
    }

    // Category Deco Down Render
    RenderImage(IMAGE_IGS_CATEGORY_DECO_DOWN, m_Pos.x,
                m_CategoryButton.GetPos(iSizeCategory - 1).y - 10,
                IMAGE_IGS_CATEGORY_DECO_DOWN_WIDTH, IMAGE_IGS_CATEGORY_DECO_DOWN_HEIGHT);

    for (int cnt = g_InGameShopSystem.GetSizePackageAsDisplayPackage();
         cnt < INGAMESHOP_DISPLAY_ITEMLIST_SIZE; cnt++)
    {
        RenderImage(IMAGE_IGS_ITEMBOX_LOGO,
                    m_Pos.x + IMAGE_IGS_ITEMBOX_LOGO_POS_X +
                        ((cnt % IGS_NUM_ITEMS_WIDTH) * IMAGE_IGS_VIEWDETAIL_BTN_DISTANCE_X),
                    m_Pos.y + IMAGE_IGS_ITEMBOX_LOGO_POS_Y +
                        ((cnt / IGS_NUM_ITEMS_HEIGHT) * IMAGE_IGS_VIEWDETAIL_BTN_DISTANCE_Y),
                    IMAGE_IGS_ITEMBOX_LOGO_SIZE, IMAGE_IGS_ITEMBOX_LOGO_SIZE);
    }

    RenderImage(IMAGE_IGS_STORAGE_PAGE, m_Pos.x + IMAGE_IGS_STORAGE_PAGE_POS_X,
                m_Pos.y + IMAGE_IGS_STORAGE_PAGE_POS_Y, IMGAE_IGS_STORAGE_PAGE_WIDTH,
                IMGAE_IGS_STORAGE_PAGE_HEIGHT);
}

void CNewUIInGameShop::RenderTexts()
{
    wchar_t szText[256] = {
        0,
    };
    wchar_t szValue[256] = {
        0,
    };

    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);
    mu_swprintf(szText, Hero->ID);
    g_RenderText.RenderText(m_Pos.x + TEXT_IGS_CHAR_NAME_POS_X, m_Pos.y + TEXT_IGS_CHAR_NAME_POS_Y,
                            szText, TEXT_IGS_CHAR_NAME_WIDTH, 0, RT3_SORT_CENTER);
    g_RenderText.SetFont(LegacyFontRole::Normal);

    // Display Item
    for (int i = 0; i < g_InGameShopSystem.GetSizePackageAsDisplayPackage(); i++)
    {
        CShopPackage *pPackage = g_InGameShopSystem.GetDisplayPackage(i);
        // Package
        g_RenderText.SetTextColor(255, 255, 255, 255);
        g_RenderText.RenderText(
            m_Pos.x + IGS_PACKAGE_NAME_POS_X +
                ((i % IGS_NUM_ITEMS_WIDTH) * IMAGE_IGS_VIEWDETAIL_BTN_DISTANCE_X),
            m_Pos.y + IGS_PACKAGE_NAME_POS_Y +
                ((i / IGS_NUM_ITEMS_HEIGHT) * IMAGE_IGS_VIEWDETAIL_BTN_DISTANCE_Y),
            pPackage->PackageProductName, IGS_PACKAGE_NAME_WIDTH, 0, RT3_SORT_CENTER);
        // Package
        ConvertGold(pPackage->Price, szValue);
        mu_swprintf(szText, L"%ls %ls", szValue, pPackage->PricUnitName);
        g_RenderText.SetTextColor(255, 238, 161, 255);
        g_RenderText.RenderText(
            m_Pos.x + IGS_PACKAGE_NAME_POS_X +
                ((i % IGS_NUM_ITEMS_WIDTH) * IMAGE_IGS_VIEWDETAIL_BTN_DISTANCE_X),
            m_Pos.y + IGS_PACKAGE_PRICE_POS_Y + 53 +
                ((i / IGS_NUM_ITEMS_HEIGHT) * IMAGE_IGS_VIEWDETAIL_BTN_DISTANCE_Y),
            szText, IGS_PACKAGE_NAME_WIDTH, 0, RT3_SORT_CENTER);
    }
    g_RenderText.SetTextColor(255, 238, 161, 255);

    //CreditCard
    ConvertGold(g_InGameShopSystem.GetCashCreditCard(), szValue);
    mu_swprintf(szText, I18N::Game::MyWCoinS, L"");
    g_RenderText.RenderText(m_Pos.x + TEXT_IGS_CASH_POS_X, m_Pos.y + TEXT_IGS_CASH_POS_Y, szText,
                            TEXT_IGS_CASH_WIDTH, 0, RT3_SORT_LEFT);
    g_RenderText.RenderText(m_Pos.x + TEXT_IGS_CASH_POS_X + 50, m_Pos.y + TEXT_IGS_CASH_POS_Y,
                            szValue, TEXT_IGS_CASH_WIDTH - 56, 0, RT3_SORT_RIGHT);

    //Prepaid
    ConvertGold(g_InGameShopSystem.GetCashPrepaid(), szValue);
    mu_swprintf(szText, I18N::Game::MyWCoinPS, L"");
    g_RenderText.RenderText(m_Pos.x + TEXT_IGS_CASH_POS_X, m_Pos.y + TEXT_IGS_MILEAGE_POS_Y, szText,
                            TEXT_IGS_CASH_WIDTH, 0, RT3_SORT_LEFT);
    g_RenderText.RenderText(m_Pos.x + TEXT_IGS_CASH_POS_X + 50, m_Pos.y + TEXT_IGS_MILEAGE_POS_Y,
                            szValue, TEXT_IGS_CASH_WIDTH - 56, 0, RT3_SORT_RIGHT);

    ConvertGold(g_InGameShopSystem.GetTotalMileage(), szValue, 1);
    mu_swprintf(szText, I18N::Game::GoblinPointsS, L"");
    g_RenderText.RenderText(m_Pos.x + TEXT_IGS_CASH_POS_X, m_Pos.y + TEXT_IGS_POINT_POS_Y, szText,
                            TEXT_IGS_CASH_WIDTH, 0, RT3_SORT_LEFT);
    g_RenderText.RenderText(m_Pos.x + TEXT_IGS_CASH_POS_X + 50, m_Pos.y + TEXT_IGS_POINT_POS_Y,
                            szValue, TEXT_IGS_CASH_WIDTH - 56, 0, RT3_SORT_RIGHT);

    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);

    g_RenderText.RenderText(m_Pos.x + TEXT_IGS_STORAGE_NAME_POS_X,
                            m_Pos.y + TEXT_IGS_STORAGE_NAME_POS_Y, I18N::Game::ItemName,
                            TEXT_IGS_STORAGE_NAME_WIDTH, 0, RT3_SORT_CENTER);

    g_RenderText.RenderText(m_Pos.x + TEXT_IGS_STORAGE_TIME_POS_X,
                            m_Pos.y + TEXT_IGS_STORAGE_NAME_POS_Y, I18N::Game::Duration,
                            TEXT_IGS_STORAGE_TIME_WIDTH, 0, RT3_SORT_CENTER);

    // Page Info
    g_RenderText.RenderText(m_Pos.x + TEXT_IGS_PAGE_POS_X + 23, m_Pos.y + TEXT_IGS_PAGE_POS_Y, L"/",
                            10, 0, RT3_SORT_CENTER);

    mu_swprintf(szText, L"%d", g_InGameShopSystem.GetSelectPage());
    g_RenderText.RenderText(m_Pos.x + TEXT_IGS_PAGE_POS_X + 5, m_Pos.y + TEXT_IGS_PAGE_POS_Y,
                            szText, 15, 0, RT3_SORT_RIGHT);

    mu_swprintf(szText, L"%d", g_InGameShopSystem.GetTotalPages());
    g_RenderText.RenderText(m_Pos.x + TEXT_IGS_PAGE_POS_X + 36, m_Pos.y + TEXT_IGS_PAGE_POS_Y,
                            szText, 15, 0, RT3_SORT_LEFT);

    // Storage Page Info
    g_RenderText.RenderText(m_Pos.x + TEXT_IGS_STORAGE_PAGE_INFO_POS_X + 35,
                            m_Pos.y + TEXT_IGS_STORAGE_PAGE_INFO_POS_Y, L"/", 10, 0,
                            RT3_SORT_CENTER);
    mu_swprintf(szText, L"%d", m_iStorageCurrentPage);
    g_RenderText.RenderText(m_Pos.x + TEXT_IGS_STORAGE_PAGE_INFO_POS_X + 12,
                            m_Pos.y + TEXT_IGS_STORAGE_PAGE_INFO_POS_Y, szText, 20, 0,
                            RT3_SORT_RIGHT);
    mu_swprintf(szText, L"%d", m_iStorageTotalPage);
    g_RenderText.RenderText(m_Pos.x + TEXT_IGS_STORAGE_PAGE_INFO_POS_X + 48,
                            m_Pos.y + TEXT_IGS_STORAGE_PAGE_INFO_POS_Y, szText, 20, 0,
                            RT3_SORT_LEFT);

#ifdef KJH_MOD_SHOP_SCRIPT_DOWNLOAD
#ifdef FOR_WORK
    g_RenderText.SetTextColor(210, 180, 230, 255);
    g_RenderText.SetFont(LegacyFontRole::Normal);

    // Script Version Info
    CListVersionInfo ScriptVer;
    ScriptVer = g_InGameShopSystem.GetCurrentScriptVer();
    mu_swprintf(szText, L"Script Ver. %d.%d.%d", ScriptVer.Zone, ScriptVer.year, ScriptVer.yearId);
    g_RenderText.RenderText(m_Pos.x + 12, m_Pos.y + 396, szText, 150, 0, RT3_SORT_LEFT);

    ScriptVer = g_InGameShopSystem.GetCurrentBannerVer();
    mu_swprintf(szText, L"Banner Ver. %d.%d.%d", ScriptVer.Zone, ScriptVer.year, ScriptVer.yearId);
    g_RenderText.RenderText(m_Pos.x + 12, m_Pos.y + 408, szText, 150, 0, RT3_SORT_LEFT);
#endif // FOR_WORK
#endif //KJH_MOD_SHOP_SCRIPT_DOWNLOAD
}

void CNewUIInGameShop::RenderButtons()
{
    m_ZoneButton.Render();
    m_CategoryButton.Render();
    m_ListBoxTabButton.Render();

    for (int i = 0; i < g_InGameShopSystem.GetSizePackageAsDisplayPackage(); i++)
    {
        m_ViewDetailButton[i].Render();
    }

    m_CashGiftButton.Render();
    m_CashChargeButton.Render();
    m_CashRefreshButton.Render();
    m_UseButton.Render();
    m_PrevButton.Render();
    m_NextButton.Render();
    m_StoragePrevButton.Render();
    m_StorageNextButton.Render();
    m_CloseButton.Render();
}

void CNewUIInGameShop::RenderListBox()
{
    m_StorageItemListBox.Render();
}

void CNewUIInGameShop::RenderBanner()
{
    if (m_bLoadBanner == false)
        return;

    RenderImage(IMAGE_IGS_BANNER, IMAGE_IGS_BANNER_POS_X, IMAGE_IGS_BANNER_POS_Y,
                IMAGE_IGS_BANNER_WIDTH, IMAGE_IGS_BANNER_HEIGHT);
}

void CNewUIInGameShop::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_exit_00.tga", IMAGE_IGS_EXIT_BTN, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_shopback.jpg", IMAGE_IGS_BACK,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Bt01.tga", IMAGE_IGS_CATEGORY_BTN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Deco_Center.tga", IMAGE_IGS_CATEGORY_DECO_MIDDLE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Deco_Dn.tga", IMAGE_IGS_CATEGORY_DECO_DOWN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\ingame_Tab01.tga", IMAGE_IGS_LEFT_TAB,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\ingame_Tab02.tga", IMAGE_IGS_RIGHT_TAB,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Tab_Up.tga", IMAGE_IGS_ZONE_BTN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Bt_Gift.tga", IMAGE_IGS_ITEMGIFT_BTN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Bt_Cash.tga", IMAGE_IGS_CASHGIFT_BTN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Bt_Reset.tga", IMAGE_IGS_REFRESH_BTN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Bt03.tga", IMAGE_IGS_VIEWDETAIL_BTN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\Ingame_Itembox_logo.tga", IMAGE_IGS_ITEMBOX_LOGO,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\ingame_Bt_page_L.tga", IMAGE_IGS_PAGE_LEFT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\ingame_Bt_page_R.tga", IMAGE_IGS_PAGE_RIGHT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\IGS_Storage_Page.tga", IMAGE_IGS_STORAGE_PAGE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\IGS_Storage_Page_Left.tga", IMAGE_IGS_STORAGE_PAGE_LEFT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\InGameShop\\IGS_Storage_Page_Right.tga", IMAGE_IGS_STORAGE_PAGE_RIGHT,
                LegacyTextureFilter::Linear);
}

void CNewUIInGameShop::UnloadImages()
{
    DeleteBitmap(IMAGE_IGS_EXIT_BTN);
    DeleteBitmap(IMAGE_IGS_BACK);
    DeleteBitmap(IMAGE_IGS_CATEGORY_BTN);
    DeleteBitmap(IMAGE_IGS_CATEGORY_DECO_MIDDLE);
    DeleteBitmap(IMAGE_IGS_CATEGORY_DECO_DOWN);
    DeleteBitmap(IMAGE_IGS_LEFT_TAB);
    DeleteBitmap(IMAGE_IGS_RIGHT_TAB);
    DeleteBitmap(IMAGE_IGS_ZONE_BTN);
    DeleteBitmap(IMAGE_IGS_ITEMGIFT_BTN);
    DeleteBitmap(IMAGE_IGS_CASHGIFT_BTN);
    DeleteBitmap(IMAGE_IGS_REFRESH_BTN);
    DeleteBitmap(IMAGE_IGS_VIEWDETAIL_BTN);
    DeleteBitmap(IMAGE_IGS_ITEMBOX_LOGO);
    DeleteBitmap(IMAGE_IGS_PAGE_LEFT);
    DeleteBitmap(IMAGE_IGS_PAGE_RIGHT);
    DeleteBitmap(IMAGE_IGS_STORAGE_PAGE);
    DeleteBitmap(IMAGE_IGS_STORAGE_PAGE_LEFT);
    DeleteBitmap(IMAGE_IGS_STORAGE_PAGE_RIGHT);
}

#endif //PBG_ADD_INGAMESHOP_UI_ITEMSHOP

using namespace SEASON4A;

void SessionRenderUnit::RenderToolTipForSocketSetOption(int iPos_x, int iPos_y)
{
    if (g_SocketItemMgr.IsSocketSetOptionEnabled())
    {
        int PosX, PosY;

        PosX = iPos_x;
        PosY = iPos_y;

        BYTE TextNum = 0;
        BYTE SkipNum = 0;
        BYTE setIndex = 0;

        mu_swprintf(TextList[TextNum], L"\n");
        TextListColor[TextNum] = 0;
        TextBold[TextNum] = false;
        TextNum++;
        SkipNum++;
        mu_swprintf(TextList[TextNum], L"\n");
        TextListColor[TextNum] = 0;
        TextBold[TextNum] = false;
        TextNum++;
        SkipNum++;
        mu_swprintf(TextList[TextNum], L"\n");
        TextListColor[TextNum] = 0;
        TextBold[TextNum] = false;
        TextNum++;
        SkipNum++;

        mu_swprintf(TextList[TextNum], I18N::Game::SocketPackageOption);
        TextListColor[TextNum] = TEXT_COLOR_PURPLE;
        TextBold[TextNum] = true;
        TextNum++;

        wchar_t szOptionText[64] = {
            0,
        };
        wchar_t szOptionValueText[16] = {
            0,
        };
        SOCKET_OPTION_INFO *pInfo = NULL;
        for (std::deque<DWORD>::iterator iter = g_SocketItemMgr.m_EquipSetBonusList.begin();
             iter != g_SocketItemMgr.m_EquipSetBonusList.end(); ++iter)
        {
            pInfo = &g_SocketItemMgr.m_SocketOptionInfo[SOT_EQUIP_SET_BONUS_OPTIONS][*iter];
            g_SocketItemMgr.CalcSocketOptionValueText(szOptionValueText, pInfo->m_bOptionType,
                                                      (float)pInfo->m_iOptionValue[0]);
            mu_swprintf(TextList[TextNum], L"%ls %ls", pInfo->m_szOptionName, szOptionValueText);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
        }

        RenderTipTextList(PosX, PosY, TextNum, 140, RT3_SORT_CENTER, STRP_BOTTOMCENTER);
    }
}

using namespace SEASON3B;

void CNewUIInventoryExtension::SetPos(int x, int y)
{
    (void)x;
    (void)y;
}

bool CNewUIInventoryExtension::Render()
{
    bool recorded = m_ModernPanel.Record(renderUnit.LegacyRender());
    for (std::size_t i = 0; i < m_ModernBagCount; ++i)
    {
        m_extensions[i]->Render();
        recorded = m_extensions[i]->RenderOwnerLayer() && recorded;
    }
    return recorded;
}

bool CNewUIInventoryExtension::PrepareModernUiOnWorker(int width, int height)
{
    return m_ModernPanel.PrepareOnWorker(width, height, m_ModernVisible, m_ModernBagCount,
                                         m_ModernTitle);
}

void CNewUIItemExplanationWindow::SetPos(int, int)
{
}

bool CNewUIItemExplanationWindow::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}
bool CNewUIItemExplanationWindow::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_, content_);
}

void CNewUILuckyItemWnd::RenderMixEffect()
{
    if (m_mixEffectTicks <= 0)
    {
        return;
    }

    const auto cell = m_ModernPanel.GridCell();
    EnableAlphaBlend();
    for (int i = 0; i < (int)m_pNewInventoryCtrl->GetNumberOfItems(); ++i)
    {
        int iWidth = ItemAttribute[m_pNewInventoryCtrl->GetItem(i)->Type].Width;
        int iHeight = ItemAttribute[m_pNewInventoryCtrl->GetItem(i)->Type].Height;

        for (int h = 0; h < iHeight; ++h)
        {
            for (int w = 0; w < iWidth; ++w)
            {
                glColor3f((float)(rand() % 6 + 6) * 0.1f, (float)(rand() % 4 + 4) * 0.1f, 0.2f);
                float Rotate = (float)((int)(WorldTime) % 100) * 20.f;
                float Scale = 5.f + (rand() % 10);
                float x =
                    cell.x + (m_pNewInventoryCtrl->GetItem(i)->x + w + float(rand()) / RAND_MAX) *
                                 cell.width;
                float y =
                    cell.y + (m_pNewInventoryCtrl->GetItem(i)->y + h + float(rand()) / RAND_MAX) *
                                 cell.height;
                RenderBitmapRotate(BITMAP_SHINY, x, y, Scale, Scale, 0);
                RenderBitmapRotate(BITMAP_SHINY, x, y, Scale, Scale, Rotate);
                RenderBitmapRotate(BITMAP_SHINY + 1, x, y, Scale * 3.f, Scale * 3.f, Rotate);
                RenderBitmapRotate(BITMAP_LIGHT, x, y, Scale * 6.f, Scale * 6.f, 0);
            }
        }
    }
    DisableAlphaBlend();
}

bool CNewUILuckyItemWnd::Render(void)
{
    bool recorded = m_ModernPanel.Record(renderUnit.LegacyRender());
    if (m_pNewInventoryCtrl)
    {
        m_pNewInventoryCtrl->Render();
        recorded = m_pNewInventoryCtrl->RenderOwnerLayer() && recorded;
    }
    if (m_eEnd == eLuckyItem_End)
        RenderMixEffect();
    return recorded;
}

void CNewUILuckyItemWnd::SetPos(int x, int y)
{
    (void)x;
    (void)y;
    SyncModernGeometry();
}

void CNewUILuckyItemWnd::StageModernContent()
{
    m_ModernPanel.SetSlotFrames(m_pNewInventoryCtrl->SlotIconFrames());
    m_ModernPanel.SetText("tfTitle", m_szSubject);
    m_ModernPanel.SetText("btnMix-label", m_eType == eLuckyItemType_Trade ? I18N::Game::Combining
                                                                          : I18N::Game::Refine);
    m_ModernPanel.SetShown("btnMix", m_eEnd != eLuckyItem_End);
    m_ModernPanel.SetEnabled("btnMix", m_eWndAction != eLuckyItem_Act);
    m_ModernPanel.SetShown("mcScrollList", false);
    wchar_t rate[128];
    mu_swprintf(rate, I18N::Game::SSuccessRateD,
                m_eType == eLuckyItemType_Trade ? I18N::Game::Combining : I18N::Game::Refine,
                GetLuckyItemRate(m_eType));
    m_ModernPanel.SetText("tfMent", m_eEnd == eLuckyItem_End ? L"" : rate);
    std::string guide;
    for (int i = 0; i < m_nTextMaxLine; ++i)
    {
        if (m_sText[i].s_nTextIndex < 0)
            break;
        if (m_sText[i].s_nTextIndex == 0)
            continue;
        const char *tone = m_sText[i].s_dwColor == 0xFF0000FF   ? "warning"
                           : m_sText[i].s_dwColor == 0xFF00FFFF ? "ready"
                                                                : "normal";
        guide += "<span class=\"";
        guide += tone;
        guide += "\">";
        guide += Rml::StringUtilities::EncodeRml(
            StringUtils::WideToNarrow(I18N::Game::Lookup(m_sText[i].s_nTextIndex)));
        guide += "</span><br/>";
    }
    m_ModernPanel.SetMarkup("taGuide01", guide);
}
bool CNewUILuckyItemWnd::PrepareModernUiOnWorker(int width, int height)
{
    return m_ModernPanel.PrepareOnWorker(width, height);
}

void SEASON3B::CNewUIPurchaseShopInventory::SetPos(int x, int y)
{
    (void)x;
    (void)y;
}

bool SEASON3B::CNewUIPurchaseShopInventory::Render()
{
    bool recorded = m_ModernPanel.Record(renderUnit.LegacyRender());
    if (m_pNewInventoryCtrl != nullptr)
    {
        m_pNewInventoryCtrl->Render();
        recorded = m_pNewInventoryCtrl->RenderOwnerLayer() && recorded;
    }
    return recorded;
}

bool SEASON3B::CNewUIPurchaseShopInventory::PrepareModernUiOnWorker(int viewportWidth,
                                                                    int viewportHeight)
{
    return m_ModernPanel.PrepareOnWorker(viewportWidth, viewportHeight, m_ModernVisible,
                                         m_ModernContent);
}

void CNewUISetItemExplanation::SetPos(int, int)
{
}

void CNewUISetItemExplanation::StageContent()
{
    const int selection = g_csItemOption.OptionHelperSelection();
    const std::string locale = I18N::GetCurrentLocale();
    if (selection_ == selection && locale_ == locale)
        return;
    selection_ = selection;
    locale_ = locale;
    const auto revision = content_.revision + 1;
    content_ = {};
    content_.revision = revision;
    std::uint8_t count = 0;
    if (!g_csItemOption.BuildOptionHelper(count))
        return;
    for (int i = 0; i < count; ++i)
    {
        const std::wstring text = TextList[i];
        const bool blank = text.find_first_not_of(L" \r\n\t") == std::wstring::npos;
        const char *style = TextListColor[i] == TEXT_COLOR_BLUE     ? "blue"
                            : TextListColor[i] == TEXT_COLOR_GREEN  ? "green"
                            : TextListColor[i] == TEXT_COLOR_YELLOW ? "yellow"
                                                                    : "white";
        content_.notes.push_back({blank ? L"" : text, style});
    }
}

bool CNewUISetItemExplanation::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}
bool CNewUISetItemExplanation::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_, content_);
}

// Construction/Destruction

void CNewUIStorageInventory::SetPos(int x, int y)
{
    (void)x;
    (void)y;
    SyncModernGeometry();
}

bool CNewUIStorageInventory::Render()
{
    bool recorded = m_ModernPanel.Record(renderUnit.LegacyRender());
    if (m_pNewInventoryCtrl)
    {
        m_pNewInventoryCtrl->Render();
        recorded = m_pNewInventoryCtrl->RenderOwnerLayer() && recorded;
    }
    return recorded;
}

bool CNewUIStorageInventory::PrepareModernUiOnWorker(int width, int height)
{
    return m_ModernPanel.PrepareOnWorker(width, height);
}

void CNewUIStorageInventory::StageModernContent()
{
    m_ModernPanel.SetSlotFrames(m_pNewInventoryCtrl->SlotIconFrames());
    m_ModernPanel.SetShown("btnExit", false);
    m_ModernPanel.SetShown("mcPeriodStorage", false);
    wchar_t text[128];
    mu_swprintf(text, L"%ls (%ls)", I18N::Game::Storage, I18N::Game::Lookup(m_bLock ? 241 : 240));
    m_ModernPanel.SetText("tfTitle", text);
    ConvertGold(CharacterMachine->StorageGold, text);
    m_ModernPanel.SetText("tfKeepZenValue", text);
    m_ModernPanel.SetText("tfKeepZen", I18N::Game::Zen);
    m_ModernPanel.SetText("tfCharge", I18N::Game::StorageFee);
    const auto level = static_cast<double>(CharacterAttribute->Level) + Master_Level_Data.nMLevel;
    int fee = std::max(1, static_cast<int>(level * level * 0.04) +
                              (m_bLock ? static_cast<int>(CharacterAttribute->Level) * 2 : 0));
    if (fee >= 1000)
        fee = fee / 100 * 100;
    else if (fee >= 100)
        fee = fee / 10 * 10;
    ConvertGold(fee, text);
    m_ModernPanel.SetText("tfChargeValue", text);
    m_ModernPanel.SetText("btnDeposit-label", I18N::Game::Deposit);
    m_ModernPanel.SetText("btnWithDraw-label", I18N::Game::Withdraw);
    m_ModernPanel.SetText("btnLift-label", I18N::Game::Lookup(m_bLock ? 241 : 240));
    m_ModernPanel.SetText("btnExtend-label", I18N::Game::InventoryExpand);
    m_ModernPanel.SetEnabled("btnExtend", CharacterAttribute->IsVaultExtended > 0);
}

// Construction/Destruction

void CNewUIStorageInventoryExt::SetPos(int x, int y)
{
    (void)x;
    (void)y;
    SyncModernGeometry();
}

bool CNewUIStorageInventoryExt::Render()
{
    bool recorded = m_ModernPanel.Record(renderUnit.LegacyRender());
    if (m_pNewInventoryCtrl)
    {
        m_pNewInventoryCtrl->Render();
        recorded = m_pNewInventoryCtrl->RenderOwnerLayer() && recorded;
    }
    return recorded;
}

bool CNewUIStorageInventoryExt::PrepareModernUiOnWorker(int width, int height)
{
    return m_ModernPanel.PrepareOnWorker(width, height);
}

void CNewUIStorageInventoryExt::StageModernContent()
{
    m_ModernPanel.SetSlotFrames(m_pNewInventoryCtrl->SlotIconFrames());
    m_ModernPanel.SetShown("btnExit", false);
    m_ModernPanel.SetShown("mcPeriodStorage", false);
    m_ModernPanel.SetText("tfTitle", I18N::Game::ExpandedVault);
    m_ModernPanel.SetShown("mcCharge", false);
}

// Desc: implementation of the CNewUITrade class.

void CNewUITrade::SetPos(int x, int y)
{
    (void)x;
    (void)y;
    SyncModernGeometry();
}

bool CNewUITrade::Render()
{
    bool recorded = m_ModernPanel.Record(renderUnit.LegacyRender());
    for (auto *control : {m_pYourInvenCtrl, m_pMyInvenCtrl})
    {
        control->Render();
        recorded = control->RenderOwnerLayer() && recorded;
    }
    return m_ModernPanel.RecordOverlay(renderUnit.LegacyRender()) && recorded;
}

void CNewUITrade::StageModernContent()
{
    m_ModernPanel.SetText("tfTitle", I18N::Game::Trade);
    m_ModernPanel.SetText("tfTradeOther", I18N::Game::Trade);
    m_ModernPanel.SetText("tfTradeOtherValue", m_szYourID);
    m_ModernPanel.SetText("tfMyName", I18N::Game::Name);
    m_ModernPanel.SetText("tfMyNameValue", Hero->ID);
    m_ModernPanel.SetText("tfGuild", I18N::Game::Guild);
    m_ModernPanel.SetText("tfLevel", I18N::Game::Level);
    std::wstring_view guild;
    for (int i = 0; i < MAX_MARKS; ++i)
    {
        if (GuildMark[i].Key != -1 && GuildMark[i].Key == m_nYourGuildType)
        {
            guild = GuildMark[i].GuildName;
            break;
        }
    }
    m_ModernPanel.SetText("tfGuildValue", guild);
    int level;
    DWORD color;
    ConvertYourLevel(level, color);
    wchar_t text[256];
    if (level == 400)
        mu_swprintf(text, L"%d", level);
    else
        mu_swprintf(text, I18N::Game::AboutD, level);
    m_ModernPanel.SetText("tfLevelValue", text);
    ConvertGold(m_nYourTradeGold, text);
    m_ModernPanel.SetText("tfOtherZen", text);
    ConvertGold(m_nMyTradeGold, text);
    m_ModernPanel.SetText("tfMyZen", text);
    m_ModernPanel.SetText("tfCaution", I18N::Game::Warning);
    mu_swprintf(text, L"%ls %ls %ls", I18N::Game::NoticePleaseCheckOut,
                I18N::Game::TheLevelOfThePlayer, I18N::Game::AndTheItemsBeforeTrading);
    m_ModernPanel.SetText("tfCautionText", text);
    m_ModernPanel.SetText("btnInputZen-label", I18N::Game::ZenTrade);
    m_ModernPanel.SetText("btnTrade-label", I18N::Game::Trade);
    m_ModernPanel.SetText("tfOtherConfirm", I18N::Game::Trade);
    m_ModernPanel.SetText("tfMyConfirm", I18N::Game::Trade);
    m_ModernPanel.SetShown("tfOtherConfirm", m_bYourConfirm);
    m_ModernPanel.SetShown("tfMyConfirm", m_bMyConfirm);
    m_ModernPanel.SetShown("mcOtherInvenBG", m_bYourConfirm);
    m_ModernPanel.SetShown("mcMyInvenBG", m_bMyConfirm);
    m_ModernPanel.SetEnabled("btnTrade", m_nMyTradeWait == 0 && !g_pPickedItem);
    m_ModernPanel.SetSlotFrames(m_pYourInvenCtrl->SlotIconFrames());
    m_ModernPanel.SetSlotFrames(m_pMyInvenCtrl->SlotIconFrames(), MAX_TRADE_INVEN);
}
bool CNewUITrade::PrepareModernUiOnWorker(int width, int height)
{
    return m_ModernPanel.PrepareOnWorker(width, height);
}

void CNewUIUnitedMarketPlaceWindow::SetPos(int, int)
{
}

void CNewUIUnitedMarketPlaceWindow::StageContent()
{
    locale_ = I18N::GetCurrentLocale();
    contentWorld_ = gMapManager.ContextMap();
    content_.title = I18N::Game::Julia;
    content_.okLabel = I18N::Game::OK;
    content_.cancelLabel = I18N::Game::Cancel;
    content_.firstLine.clear();
    const auto add = [&](const wchar_t *line) {
        if (!content_.firstLine.empty())
            content_.firstLine += L"\n";
        content_.firstLine += line;
    };
    if (gMapManager.ContextMap() == WD_79UNITEDMARKETPLACE)
    {
        add(I18N::Game::WillYouBeGoingBackToTownNow);
        add(I18N::Game::HaveAnotherGreatDay);
        add(I18N::Game::AndStayPositiveAtAllTimes);
        add(I18N::Game::WouldYouLikeToGoToTown);
        return;
    }
    add(I18N::Game::IfYouGoToTheMarketInLorencia);
    add(I18N::Game::YouLlFindManyItemsYouNeed);
    add(I18N::Game::AvailableForPurchase);
    add(I18N::Game::IfYouHaveItemsYouWantToSell);
    add(I18N::Game::YouCanSellThem);
    add(I18N::Game::AtTheMarket);
    add(I18N::Game::WouldYouLikeToGoToTheMarket);
}

bool CNewUIUnitedMarketPlaceWindow::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}
bool CNewUIUnitedMarketPlaceWindow::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, content_);
}

namespace
{
using INTBYTEPAIR = std::pair<int, BYTE>;

}

void SessionUiUnit::RenderUnMixList()
{
    unmixGemList_->Render();
}

// OMF-00735
// OMF-00736
// OMF-00737
// OMF-00738
// OMF-00740
// OMF-00741
// OMF-00742
// OMF-00743
// OMF-00744
// OMF-00746
// OMF-00747
// OMF-00745
// OMF-00748
// OMF-00749
// OMF-00750
// OMF-00751
// OMF-00752
// OMF-00753
// OMF-00754
// OMF-00755
// OMF-00756
// OMF-00757
// OMF-00758
// OMF-00759
// OMF-00760
// OMF-00761
// OMF-00762
// OMF-00763
// OMF-00764
// OMF-00765
// OMF-00766
// OMF-00767
// OMF-00768

using InventoryPanel = UI::Modern::PC::Inventory::RmlInventoryPanel;

void CNewUIMyInventory::SetPos(int x, int y)
{
    (void)x;
    (void)y;
    SyncModernGeometry();
}

bool CNewUIMyInventory::Render()
{
    bool recorded = m_ModernPanel.Record(renderUnit_.LegacyRender());
    m_pNewInventoryCtrl->Render();
    recorded = m_pNewInventoryCtrl->RenderOwnerLayer() && recorded;
    recorded = m_pNewUI3DRenderMng->RenderObject(*this, INVENTORY_CAMERA_Z_ORDER) && recorded;
    RenderSetOption();
    RenderSocketOption();
    RenderEquippedItem();
    return recorded;
}

void CNewUIMyInventory::RenderSetOption()
{
    if (m_ModernPanel.Hovered(InventoryPanel::SetOption))
        m_pNewUI3DRenderMng->RenderUI2DEffect(INVENTORY_CAMERA_Z_ORDER, UI2DEffectCallback, this,
                                              -1, ITEM_SET_OPTION);
}

void CNewUIMyInventory::RenderSocketOption()
{
    if (m_ModernPanel.Hovered(InventoryPanel::SocketOption))
        m_pNewUI3DRenderMng->RenderUI2DEffect(INVENTORY_CAMERA_Z_ORDER, UI2DEffectCallback, this,
                                              -1, ITEM_SOCKET_SET_OPTION);
}

void CNewUIMyInventory::Render3D()
{
    for (int i = 0; i < MAX_EQUIPMENT_INDEX; ++i)
    {
        const ITEM &item = CharacterMachine->Equipment[i];
        if (item.Type < 0 || (i == EQUIPMENT_HELM && !m_ModernContent.showHelm) ||
            (i == EQUIPMENT_GLOVES && !m_ModernContent.showGloves))
            continue;
        const auto &slot = m_EquipmentSlots[i];
        const auto cell = m_ModernPanel.GridCell();
        const float scale = cell.height / INVENTORY_SQUARE_HEIGHT;
        glColor4f(1.f, 1.f, 1.f, 1.f);
        RenderItem3D(slot.x, slot.y, slot.width, slot.height, item.Type, item.Level,
                     item.ExcellentFlags, item.AncientDiscriminator, false, scale, true);
    }
}

void CNewUIMyInventory::RenderSetOptionList()
{
    std::uint8_t textCount = 0;
    if (g_csItemOption.BuildSetOptionList(textCount))
    {
        g_RenderText.SetTextColor(255, 255, 255, 255);
        g_RenderText.SetBgColor(100, 0, 0, 0);
        const auto button = m_ModernPanel.ButtonRect(InventoryPanel::SetOption);
        RenderTipTextList(static_cast<int>(button.x + button.width / 2), static_cast<int>(button.y),
                          textCount, 120, RT3_SORT_CENTER, STRP_BOTTOMCENTER);
    }
}

void CNewUIMyInventory::LoadImages() const
{
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_INVENTORY_BACK,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back01.tga", IMAGE_INVENTORY_BACK_TOP,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back04.tga", IMAGE_INVENTORY_BACK_TOP2,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-L.tga", IMAGE_INVENTORY_BACK_LEFT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back02-R.tga", IMAGE_INVENTORY_BACK_RIGHT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_back03.tga", IMAGE_INVENTORY_BACK_BOTTOM,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_boots.tga", IMAGE_INVENTORY_ITEM_BOOT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_cap.tga", IMAGE_INVENTORY_ITEM_HELM,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_fairy.tga", IMAGE_INVENTORY_ITEM_FAIRY,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_wing.tga", IMAGE_INVENTORY_ITEM_WING,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_weapon(L).tga", IMAGE_INVENTORY_ITEM_RIGHT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_weapon(R).tga", IMAGE_INVENTORY_ITEM_LEFT,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_upper.tga", IMAGE_INVENTORY_ITEM_ARMOR,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_gloves.tga", IMAGE_INVENTORY_ITEM_GLOVES,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_lower.tga", IMAGE_INVENTORY_ITEM_PANTS,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_ring.tga", IMAGE_INVENTORY_ITEM_RING,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_necklace.tga", IMAGE_INVENTORY_ITEM_NECKLACE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_item_money.tga", IMAGE_INVENTORY_MONEY,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_exit_00.tga", IMAGE_INVENTORY_EXIT_BTN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_repair_00.tga", IMAGE_INVENTORY_REPAIR_BTN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_expansion_btn.tga", IMAGE_INVENTORY_EXPAND_BTN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_Bt_openshop.tga", IMAGE_INVENTORY_MYSHOP_OPEN_BTN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_Bt_closeshop.tga", IMAGE_INVENTORY_MYSHOP_CLOSE_BTN,
                LegacyTextureFilter::Linear);
}

void CNewUIMyInventory::UnloadImages()
{
    DeleteBitmap(IMAGE_INVENTORY_MYSHOP_CLOSE_BTN);
    DeleteBitmap(IMAGE_INVENTORY_MYSHOP_OPEN_BTN);
    DeleteBitmap(IMAGE_INVENTORY_REPAIR_BTN);
    DeleteBitmap(IMAGE_INVENTORY_EXIT_BTN);
    DeleteBitmap(IMAGE_INVENTORY_MONEY);
    DeleteBitmap(IMAGE_INVENTORY_ITEM_NECKLACE);
    DeleteBitmap(IMAGE_INVENTORY_ITEM_RING);
    DeleteBitmap(IMAGE_INVENTORY_ITEM_PANTS);
    DeleteBitmap(IMAGE_INVENTORY_ITEM_GLOVES);
    DeleteBitmap(IMAGE_INVENTORY_ITEM_ARMOR);
    DeleteBitmap(IMAGE_INVENTORY_ITEM_RIGHT);
    DeleteBitmap(IMAGE_INVENTORY_ITEM_LEFT);
    DeleteBitmap(IMAGE_INVENTORY_ITEM_WING);
    DeleteBitmap(IMAGE_INVENTORY_ITEM_FAIRY);
    DeleteBitmap(IMAGE_INVENTORY_ITEM_HELM);
    DeleteBitmap(IMAGE_INVENTORY_ITEM_BOOT);
    DeleteBitmap(IMAGE_INVENTORY_BACK_BOTTOM);
    DeleteBitmap(IMAGE_INVENTORY_BACK_RIGHT);
    DeleteBitmap(IMAGE_INVENTORY_BACK_LEFT);
    DeleteBitmap(IMAGE_INVENTORY_BACK_TOP2);
    DeleteBitmap(IMAGE_INVENTORY_BACK_TOP);
    DeleteBitmap(IMAGE_INVENTORY_BACK);
    DeleteBitmap(IMAGE_INVENTORY_EXPAND_BTN);
}

void CNewUIMyInventory::RenderEquippedItem()
{
    if (m_iPointedSlot != -1 && m_pNewUI3DRenderMng)
        m_pNewUI3DRenderMng->RenderUI2DEffect(INVENTORY_CAMERA_Z_ORDER, UI2DEffectCallback, this,
                                              m_iPointedSlot, 0);
}

void CNewUIMyInventory::RenderItemToolTip(int iSlotIndex) const
{
    if (m_iPointedSlot != -1)
    {
        ITEM *pEquipmentItemSlot = &CharacterMachine->Equipment[iSlotIndex];
        if (pEquipmentItemSlot->Type != -1)
        {
            const int iTargetX =
                m_EquipmentSlots[iSlotIndex].x + m_EquipmentSlots[iSlotIndex].width / 2;
            const int iTargetY =
                m_EquipmentSlots[iSlotIndex].y + m_EquipmentSlots[iSlotIndex].height / 2;

            if (m_RepairMode == REPAIR_MODE_OFF)
            {
                RenderItemInfo(iTargetX, iTargetY, pEquipmentItemSlot, false);
            }
            else
            {
                RenderRepairInfo(iTargetX, iTargetY, pEquipmentItemSlot, false);
            }
        }
    }
}

void CNewUIMyInventory::StageModernContent()
{
    auto &content = m_ModernContent;
    content.title = I18N::Game::Inventory;
    wchar_t zen[64]{};
    ConvertGold(CharacterMachine->Gold, zen);
    content.zen = zen;
    content.labels[InventoryPanel::Repair] = I18N::Game::Repair;
    content.labels[InventoryPanel::Store] = I18N::Game::InventoryStore;
    content.labels[InventoryPanel::Expand] = I18N::Game::InventoryExpand;
    content.labels[InventoryPanel::SetOption] = I18N::Game::SetOption;
    content.labels[InventoryPanel::SocketOption] = I18N::Game::Socket;
    const bool actions = !g_pNewUISystem->IsVisible(INTERFACE_NPCSHOP) &&
                         !g_pNewUISystem->IsVisible(INTERFACE_TRADE) &&
                         !g_pNewUISystem->IsVisible(INTERFACE_DEVILSQUARE) &&
                         !g_pNewUISystem->IsVisible(INTERFACE_BLOODCASTLE) &&
                         !g_pNewUISystem->IsVisible(INTERFACE_MIXINVENTORY) &&
                         !g_pNewUISystem->IsVisible(INTERFACE_LUCKYITEMWND) &&
                         !g_pNewUISystem->IsVisible(INTERFACE_STORAGE);
    content.shown[InventoryPanel::Repair] = actions && m_bRepairEnableLevel;
    content.shown[InventoryPanel::Store] = actions && m_bMyShopOpen;
    content.enabled[InventoryPanel::Store] = !m_ShopButtonLocked;
    content.enabled[InventoryPanel::SetOption] = g_csItemOption.IsAncientSetEquipped();
    content.enabled[InventoryPanel::SocketOption] = g_SocketItemMgr.IsSocketSetOptionEnabled();
    content.showHelm = gCharacterManager.GetBaseClass(Hero->Class) != CLASS_DARK;
    content.showGloves = gCharacterManager.GetBaseClass(Hero->Class) != CLASS_RAGEFIGHTER;
    const auto frames = m_pNewInventoryCtrl->SlotIconFrames();
    std::copy(frames.begin(), frames.end(), content.gridFrames.begin());
    UpdateEquipmentPresentation();
}

bool CNewUIMyInventory::PrepareModernUiOnWorker(int width, int height)
{
    return m_ModernPanel.PrepareOnWorker(width, height, m_ModernVisible, m_ModernContent);
}

bool SEASON3B::CNewUIPickedItem::RenderOwnerLayer()
{
    return !IsVisible() || (g_pNewUI3DRenderMng != nullptr &&
                            g_pNewUI3DRenderMng->RenderObject(*this, INFORMATION_CAMERA_Z_ORDER));
}

void SEASON3B::CNewUIPickedItem::Render3D()
{
    if (m_pPickedItem && m_pPickedItem->Type >= 0)
    {
        const auto *geometry =
            m_pSrcInventory ? m_pSrcInventory : g_pMyInventory->GetInventoryCtrl();
        const auto &item = ItemAttribute[m_pPickedItem->Type];
        const float width = item.Width * geometry->PresentedSquareWidth();
        const float height = item.Height * geometry->PresentedSquareHeight();
        m_Size.cx = static_cast<int>(std::lround(width));
        m_Size.cy = static_cast<int>(std::lround(height));
        m_Pos.x = MouseX - m_Size.cx / 2;
        m_Pos.y = MouseY - m_Size.cy / 2;
        RenderItem3D(MouseX - width / 2, MouseY - height / 2, width, height, m_pPickedItem->Type,
                     m_pPickedItem->Level, m_pPickedItem->ExcellentFlags,
                     m_pPickedItem->AncientDiscriminator, true, geometry->ItemPresentationScale(),
                     geometry->m_ownerRendered);
    }
}

void SEASON3B::CNewUIInventoryCtrl::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_item_box.tga", IMAGE_ITEM_SQUARE);
    LoadBitmapW(L"Interface\\newui_item_table01(L).tga", IMAGE_ITEM_TABLE_TOP_LEFT);
    LoadBitmapW(L"Interface\\newui_item_table01(R).tga", IMAGE_ITEM_TABLE_TOP_RIGHT);
    LoadBitmapW(L"Interface\\newui_item_table02(L).tga", IMAGE_ITEM_TABLE_BOTTOM_LEFT);
    LoadBitmapW(L"Interface\\newui_item_table02(R).tga", IMAGE_ITEM_TABLE_BOTTOM_RIGHT);
    LoadBitmapW(L"Interface\\newui_item_table03(Up).tga", IMAGE_ITEM_TABLE_TOP_PIXEL);
    LoadBitmapW(L"Interface\\newui_item_table03(Dw).tga", IMAGE_ITEM_TABLE_BOTTOM_PIXEL);
    LoadBitmapW(L"Interface\\newui_item_table03(L).tga", IMAGE_ITEM_TABLE_LEFT_PIXEL);
    LoadBitmapW(L"Interface\\newui_item_table03(R).tga", IMAGE_ITEM_TABLE_RIGHT_PIXEL);

#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
    LoadBitmapW(L"Interface\\newui_inven_usebox_01.tga", IMAGE_ITEM_SQUARE_FOR_1_BY_1);
    LoadBitmapW(L"Interface\\newui_inven_usebox_02.tga", IMAGE_ITEM_SQUARE_TOP_RECT);
    LoadBitmapW(L"Interface\\newui_inven_usebox_03.tga", IMAGE_ITEM_SQUARE_BOTTOM_RECT);
#endif //LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
}

void SEASON3B::CNewUIInventoryCtrl::UnloadImages()
{
#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
    DeleteBitmap(IMAGE_ITEM_SQUARE_BOTTOM_RECT);
    DeleteBitmap(IMAGE_ITEM_SQUARE_TOP_RECT);
    DeleteBitmap(IMAGE_ITEM_SQUARE_FOR_1_BY_1);
#endif //LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY

    DeleteBitmap(IMAGE_ITEM_TABLE_RIGHT_PIXEL);
    DeleteBitmap(IMAGE_ITEM_TABLE_LEFT_PIXEL);
    DeleteBitmap(IMAGE_ITEM_TABLE_BOTTOM_PIXEL);
    DeleteBitmap(IMAGE_ITEM_TABLE_TOP_PIXEL);
    DeleteBitmap(IMAGE_ITEM_TABLE_BOTTOM_RIGHT);
    DeleteBitmap(IMAGE_ITEM_TABLE_BOTTOM_LEFT);
    DeleteBitmap(IMAGE_ITEM_TABLE_TOP_RIGHT);
    DeleteBitmap(IMAGE_ITEM_TABLE_TOP_LEFT);
    DeleteBitmap(IMAGE_ITEM_SQUARE);
}

void SEASON3B::CNewUIInventoryCtrl::PrepareDragPreview()
{
    dragPreview_.clear();
    if (!ms_pPickedItem || !ms_pPickedItem->IsVisible())
        return;
    RECT pickedRect, inventoryRect, overlap;
    ms_pPickedItem->GetRect(pickedRect);
    GetRect(inventoryRect);
    if (!IntersectRect(&overlap, &pickedRect, &inventoryRect))
        return;
    auto *picked = ms_pPickedItem->GetItem();
    const auto &size = ItemAttribute[picked->Type];
    const float squareWidth = PresentedSquareWidth(), squareHeight = PresentedSquareHeight();
    const float x = MouseX - (size.Width - 1) * squareWidth / 2.f;
    const float y = MouseY - (size.Height - 1) * squareHeight / 2.f;
    int column = 0, row = 0;
    if (!GetSquarePosAtPt(x, y, column, row))
    {
        column = static_cast<int>(std::floor((x - PresentedX()) / squareWidth));
        row = static_cast<int>(std::floor((y - PresentedY()) / squareHeight));
    }
    const int left = (std::max)(0, column), top = (std::max)(0, row);
    const int right = (std::min)(grid_->GetNumberOfColumn(), column + size.Width);
    const int bottom = (std::min)(grid_->GetNumberOfRow(), row + size.Height);
    if (left >= right || top >= bottom)
        return;
    if (column != left || row != top || right != column + size.Width || bottom != row + size.Height)
    {
        dragPreview_.push_back({left, top, right - left, bottom - top, {1.f, 0.2f, 0.2f}});
        return;
    }
    for (int rowIndex = top; rowIndex < bottom; ++rowIndex)
        for (int columnIndex = left; columnIndex < right; ++columnIndex)
        {
            const DWORD key = grid_->SlotKey(rowIndex * grid_->GetNumberOfColumn() + columnIndex);
            auto color = std::array{m_afColorStateNormal[0], m_afColorStateNormal[1],
                                    m_afColorStateNormal[2]};
            if (key > 1)
            {
                auto *target = FindItemByKey(key);
                const bool accepted = target && CanPreviewDrop(picked, target);
                color = accepted ? std::array{0.2f, 0.4f, 0.2f} : std::array{1.f, 0.2f, 0.2f};
            }
            dragPreview_.push_back({columnIndex, rowIndex, 1, 1, color});
        }
}

void SEASON3B::CNewUIInventoryCtrl::RenderDragPreview()
{
    if (dragPreview_.empty())
        return;
    EnableAlphaTest();
    for (const auto &cell : dragPreview_)
    {
        glColor4f(cell.color[0], cell.color[1], cell.color[2], 0.4f);
        RenderColor(PresentedX() + cell.column * PresentedSquareWidth(),
                    PresentedY() + cell.row * PresentedSquareHeight(),
                    cell.columns * PresentedSquareWidth(), cell.rows * PresentedSquareHeight());
    }
    EndRenderColor();
}

void SEASON3B::CNewUIInventoryCtrl::Render()
{
    const float presentedX = PresentedX();
    const float presentedY = PresentedY();
    const float presentedSquareWidth = PresentedSquareWidth();
    const float presentedSquareHeight = PresentedSquareHeight();
    int x, y;
    for (y = 0; y < grid_->GetNumberOfRow(); y++)
    {
        for (x = 0; x < grid_->GetNumberOfColumn(); x++)
        {
            const int iCurSquareIndex = y * grid_->GetNumberOfColumn() + x;

            const DWORD slotKey = grid_->SlotKey(iCurSquareIndex);
            if (slotKey > 1 && !m_ownerRendered)
            {
                EnableAlphaTest();

                ITEM *pItem = FindItemByKey(slotKey);

                if (pItem)
                {
                    if (pItem->byColorState == ITEM_COLOR_NORMAL)
                    {
                        glColor4f(0.3f, 0.5f, 0.5f, 0.6f);
                    }
                    else if (pItem->byColorState == ITEM_COLOR_DURABILITY_50)
                    {
                        glColor4f(1.0f, 1.0f, 0.f, 0.4f);
                    }
                    else if (pItem->byColorState == ITEM_COLOR_DURABILITY_70)
                    {
                        glColor4f(1.0f, 0.66f, 0.f, 0.4f);
                    }
                    else if (pItem->byColorState == ITEM_COLOR_DURABILITY_80)
                    {
                        glColor4f(1.0f, 0.33f, 0.f, 0.4f);
                    }
                    else if (pItem->byColorState == ITEM_COLOR_DURABILITY_100)
                    {
                        glColor4f(1.0f, 0.f, 0.f, 0.4f);
                    }
                    else if (pItem->byColorState == ITEM_COLOR_TRADE_WARNING)
                    {
                        glColor4f(1.0f, 0.2f, 0.1f, 0.4f);
                    }
                }
                else
                {
                    glColor4f(0.f, 0.f, 0.f, 0.f);
                }

                RenderColor(presentedX + (x * presentedSquareWidth),
                            presentedY + (y * presentedSquareHeight), presentedSquareWidth,
                            presentedSquareHeight);
                EndRenderColor();
            }

            EnableAlphaTest();
            if (m_renderSlotFrame)
            {
                RenderImage(IMAGE_ITEM_SQUARE, m_Pos.x + (x * m_squareWidth),
                            m_Pos.y + (y * m_squareHeight), m_squareWidth + 1, m_squareHeight + 1);
            }
        }
    }

    if (m_renderSlotFrame)
    {
        EnableAlphaTest();
        RenderImage(IMAGE_ITEM_TABLE_TOP_LEFT, m_Pos.x - WND_LEFT_EDGE, m_Pos.y - WND_TOP_EDGE, 14,
                    14);
        RenderImage(IMAGE_ITEM_TABLE_TOP_RIGHT, m_Pos.x + m_Size.cx - WND_RIGHT_EDGE,
                    m_Pos.y - WND_TOP_EDGE, 14, 14);
        RenderImage(IMAGE_ITEM_TABLE_BOTTOM_LEFT, m_Pos.x - WND_LEFT_EDGE,
                    m_Pos.y + m_Size.cy - WND_BOTTOM_EDGE, 14, 14);
        RenderImage(IMAGE_ITEM_TABLE_BOTTOM_RIGHT, m_Pos.x + m_Size.cx - WND_RIGHT_EDGE,
                    m_Pos.y + m_Size.cy - WND_BOTTOM_EDGE, 14, 14);

        for (x = m_Pos.x - WND_LEFT_EDGE + 14; x < m_Pos.x + m_Size.cx - WND_RIGHT_EDGE; x++)
        {
            RenderImage(IMAGE_ITEM_TABLE_TOP_PIXEL, x, m_Pos.y - WND_TOP_EDGE, 1, 14);
            RenderImage(IMAGE_ITEM_TABLE_BOTTOM_PIXEL, x, m_Pos.y + m_Size.cy - WND_BOTTOM_EDGE, 1,
                        14);
        }
        for (y = m_Pos.y - WND_TOP_EDGE + 14; y < m_Pos.y + m_Size.cy - WND_BOTTOM_EDGE; y++)
        {
            RenderImage(IMAGE_ITEM_TABLE_LEFT_PIXEL, m_Pos.x - WND_LEFT_EDGE, y, 14, 1);
            RenderImage(IMAGE_ITEM_TABLE_RIGHT_PIXEL, m_Pos.x + m_Size.cx - WND_RIGHT_EDGE, y, 14,
                        1);
        }
    }

    RenderDragPreview();

    const bool tooltipvisible = true;

    if (m_pNew3DRenderMng && !m_ownerRendered)
    {
        m_pNew3DRenderMng->RenderUI2DEffect(INVENTORY_CAMERA_Z_ORDER, UI2DEffectCallback, this,
                                            RENDER_NUMBER_OF_ITEM, 0);
        if (m_pToolTipItem && GetPickedItem() == nullptr)
        {
            if (tooltipvisible)
            {
                m_pNew3DRenderMng->RenderUI2DEffect(INVENTORY_CAMERA_Z_ORDER, UI2DEffectCallback,
                                                    this, RENDER_ITEM_TOOLTIP, 0);
            }
        }
    }
}

void SEASON3B::CNewUIInventoryCtrl::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

bool SEASON3B::CNewUIInventoryCtrl::RenderOwnerLayer()
{
    if (!m_ownerRendered || !IsVisible())
        return true;
    if (m_pNew3DRenderMng == nullptr ||
        !m_pNew3DRenderMng->RenderObject(*this, INVENTORY_CAMERA_Z_ORDER))
    {
        return false;
    }
    RenderNumberOfItem();
    if (m_pToolTipItem && GetPickedItem() == nullptr)
        RenderItemToolTip();
    return true;
}
void SEASON3B::CNewUIInventoryCtrl::RenderNumberOfItem()
{
    const float squareWidth = PresentedSquareWidth();
    const float squareHeight = PresentedSquareHeight();
    const float scale = m_ownerRendered ? std::min(squareWidth / INVENTORY_SQUARE_WIDTH,
                                                   squareHeight / INVENTORY_SQUARE_HEIGHT)
                                        : 1.0f;
    // RenderNumber uses a biased scale; convert the cell ratio to that API.
    constexpr float NumberScaleBias = 0.3f;
    const float numberScale = NumberScaleBias + (1.0f - NumberScaleBias) * scale;
    constexpr float NumberRightInset = 6, NumberTopInset = 1;
    EnableAlphaTest();
    glColor3f(1.f, 0.9f, 0.7f);
    for (const ITEM *item : grid_->Items())
    {
        int quantity = InventoryControlDetail::StackedConsumableQuantity(*item);
        if (!quantity && isCompiledGem(item))
            quantity = (item->Level + 1) * COMGEM::FIRST;
        if (!quantity)
            continue;
        const auto &size = ItemAttribute[item->Type];
        const float x = PresentedX() + (item->x + size.Width) * squareWidth;
        const float y = PresentedY() + item->y * squareHeight;
        RenderNumber(x - NumberRightInset * scale, y + NumberTopInset * scale, quantity,
                     numberScale);
    }
    glColor3f(1.f, 1.f, 1.f);
    DisableAlphaBlend();
}

void SEASON3B::CNewUIInventoryCtrl::RenderItemToolTip()
{
    if (m_pToolTipItem)
    {
        const ITEM_ATTRIBUTE *pItemAttr = &ItemAttribute[m_pToolTipItem->Type];
        const float squareWidth = PresentedSquareWidth();
        const float squareHeight = PresentedSquareHeight();
        const int iTargetX =
            static_cast<int>(std::lround(PresentedX() + m_pToolTipItem->x * squareWidth +
                                         pItemAttr->Width * squareWidth / 2.0f));
        int iTargetY =
            static_cast<int>(std::lround(PresentedY() + m_pToolTipItem->y * squareHeight));

        if (pItemAttr->Height == 1)
        {
            iTargetY += static_cast<int>(std::lround(squareHeight / 2.0f));
        }

        if (m_ToolTipType == TOOLTIP_TYPE_INVENTORY)
        {
            RenderItemInfo(iTargetX, iTargetY, m_pToolTipItem, false);
        }
        else if (m_ToolTipType == TOOLTIP_TYPE_REPAIR)
        {
            RenderRepairInfo(iTargetX, iTargetY, m_pToolTipItem, false);
        }
        else if (m_ToolTipType == TOOLTIP_TYPE_NPC_SHOP)
        {
            RenderItemInfo(iTargetX, iTargetY, m_pToolTipItem, true);
        }
        else if (m_ToolTipType == TOOLTIP_TYPE_MY_SHOP)
        {
            RenderItemInfo(iTargetX, iTargetY, m_pToolTipItem, false, m_ToolTipType);
        }
        else if (m_ToolTipType == TOOLTIP_TYPE_PURCHASE_SHOP)
        {
            RenderItemInfo(iTargetX, iTargetY, m_pToolTipItem, false, m_ToolTipType);
        }
    }
}

void SEASON3B::CNewUIInventoryCtrl::Render3D()
{
    const float presentedX = PresentedX();
    const float presentedY = PresentedY();
    const float squareWidth = PresentedSquareWidth();
    const float squareHeight = PresentedSquareHeight();
    auto li = grid_->Items().begin();
    for (; li != grid_->Items().end(); ++li)
    {
        const ITEM *pItem = (*li);
        const ITEM_ATTRIBUTE *pItemAttr = &ItemAttribute[pItem->Type];

        const float x = presentedX + (pItem->x * squareWidth);
        const float y = presentedY + (pItem->y * squareHeight);
        const float width = pItemAttr->Width * squareWidth;
        const float height = pItemAttr->Height * squareHeight;
        glColor4f(1.f, 1.f, 1.f, 1.f);

        RenderItem3D(x, y, width, height, pItem->Type, pItem->Level, pItem->ExcellentFlags,
                     pItem->AncientDiscriminator, false, ItemPresentationScale(), m_ownerRendered);
    }
}

static_assert(MAX_EQUIPMENT == UI::Modern::PC::Inventory::RmlDurabilityLayer::EquipmentCount);
void CNewUIItemEnduranceInfo::SetPos(int x, int y)
{
    m_petFramePos = {x, y};
}
void SEASON3B::CNewUIItemEnduranceInfo::SetPos(int x)
{
    const int logicalHeight = static_cast<int>(ModernUiViewportHeight() / ModernUiScreenRateY());
    const int frameWidth = DurabilityPanelDetail::PetFrameLogicalSize(
        UI::Modern::RmlPetFrameLayer::Width(), ModernUiScreenRateX(), ModernUiScale());
    const int dragHeight = DurabilityPanelDetail::PetFrameLogicalSize(
        UI::Modern::RmlPetFrameLayer::DragHeight(), ModernUiScreenRateY(), ModernUiScale());
    m_petFramePos.x =
        std::clamp<int>(static_cast<int>(m_petFramePos.x), 0, std::max(0, x - frameWidth));
    m_petFramePos.y = std::clamp<int>(static_cast<int>(m_petFramePos.y), 0,
                                      std::max(0, logicalHeight - dragHeight));
}

void CNewUIItemEnduranceInfo::StageAmmunition()
{
    int type = -1, slot = -1;
    if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_ELF)
    {
        const int bow = gameplay_.GetEquipedBowType();
        if (bow == BOWTYPE_BOW)
        {
            type = ITEM_ARROWS;
            slot = EQUIPMENT_WEAPON_RIGHT;
        }
        else if (bow == BOWTYPE_CROSSBOW)
        {
            type = ITEM_BOLT;
            slot = EQUIPMENT_WEAPON_LEFT;
        }
    }
    const int stacks =
        type >= 0
            ? sessionKeeper_.GameData()->Inventory(InventoryRole::Player).GetNumItemByType(type)
            : 0;
    const bool equipped = slot >= 0 && CharacterMachine->Equipment[slot].Type == type;
    const int remaining = equipped ? CharacterMachine->Equipment[slot].Durability : 0;
    const std::array next{type, remaining, stacks, int(equipped)};
    const std::string locale = I18N::GetCurrentLocale();
    if (ammunitionCounts_ == next && ammunitionLocale_ == locale)
        return;
    ammunitionCounts_ = next;
    ammunitionLocale_ = locale;
    content_.ammunition.clear();
    if (type < 0 || (!equipped && stacks == 0))
        return;
    wchar_t text[256];
    mu_swprintf(text, type == ITEM_ARROWS ? I18N::Game::ArrowsDD : I18N::Game::BoltsDD, remaining,
                stacks);
    content_.ammunition = text;
}

bool CNewUIItemEnduranceInfo::PrepareModernUiOnWorker(int width, int height)
{
    return durability_.PrepareOnWorker(width, height, visible_, content_);
}

void CNewUIItemEnduranceInfo::StageWarningTooltip()
{
    const int slot = durability_.HoveredEquipment();
    if (slot < 0 || !content_.warningsVisible || !content_.equipment[slot].state)
        return;
    const auto &item = CharacterMachine->Equipment[slot];
    if (item.Type < 0)
        return;
    auto &attributes = ItemAttribute[item.Type];
    const int maximum = CalcMaxDurability(&item, &attributes, item.Level);
    mu_swprintf(TextList[0], L"%ls (%d/%d)", attributes.Name, item.Durability, maximum);
    constexpr std::array colors{TEXT_COLOR_WHITE, TEXT_COLOR_YELLOW, TEXT_COLOR_ORANGE,
                                TEXT_COLOR_RED, TEXT_COLOR_RED};
    TextListColor[0] = colors[content_.equipment[slot].state];
    TextBold[0] = true;
    renderer_.RenderTipTextList(MouseX, MouseY, 1, 0, RT3_SORT_CENTER, STRP_BOTTOMCENTER);
}
bool CNewUIItemEnduranceInfo::Render()
{
    if (!renderer_.RecordPetFrame() || !durability_.Record(renderer_.LegacyRender()))
        return false;
    StageWarningTooltip();
    return true;
}

void SEASON3B::CNewUIItemEnduranceInfo::StagePetFrame()
{
    UI::Modern::RmlPetFrameRequest request;
    request.x = m_petFramePos.x;
    request.y = m_petFramePos.y;
    request.minimized = m_petFrameMinimized;
    request.buttonState = m_petFrameButtonState;
    if (Hero == nullptr)
    {
        m_petFrameRowCount = 0;
        renderer_.StagePetFrame(request);
        return;
    }

    wchar_t name[UI::Modern::RmlPetFrameRow::NameCapacity]{};
    if (GetEquippedHelperName(name, UI::Modern::RmlPetFrameRow::NameCapacity))
    {
        AppendPetFrameRow(request, name, CharacterMachine->Equipment[EQUIPMENT_HELPER].Durability,
                          255);
    }
    if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK_LORD && Hero->PetCommands.present)
    {
        AppendPetFrameRow(request, I18N::Game::DarkRaven,
                          CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT].Durability, 255);
    }
    if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_ELF && SummonLife > 0)
    {
        AppendPetFrameRow(request, I18N::Game::SummonedMonsterHP, SummonLife, 100);
    }

    m_petFrameRowCount = request.rowCount;
    renderer_.StagePetFrame(request);
}

void CNewUIMixInventory::SetPos(int x, int y)
{
    (void)x;
    (void)y;
    SyncModernGeometry();
}

bool CNewUIMixInventory::Render()
{
    bool recorded = m_ModernPanel.Record(renderUnit_.LegacyRender());
    if (m_pNewInventoryCtrl)
    {
        m_pNewInventoryCtrl->Render();
        recorded = m_pNewInventoryCtrl->RenderOwnerLayer() && recorded;
    }
    if (GetMixState() >= MIX_REQUESTED)
        RenderMixEffect();
    return recorded;
}

// Direction-agnostic core of the right-click moves: pick the item under the
// cursor in srcCtrl, reserve a slot in dstCtrl, and send the same move that
// drag & drop sends.

void CNewUIMixInventory::RenderMixEffect()
{
    if (m_mixEffectTicks <= 0)
    {
        return;
    }
    const auto cell = m_ModernPanel.GridCell();
    EnableAlphaBlend();

    for (int i = 0; i < (int)m_pNewInventoryCtrl->GetNumberOfItems(); ++i)
    {
        int iWidth = ItemAttribute[m_pNewInventoryCtrl->GetItem(i)->Type].Width;
        int iHeight = ItemAttribute[m_pNewInventoryCtrl->GetItem(i)->Type].Height;

        for (int h = 0; h < iHeight; ++h)
        {
            for (int w = 0; w < iWidth; ++w)
            {
                glColor3f((float)(rand() % 6 + 6) * 0.1f, (float)(rand() % 4 + 4) * 0.1f, 0.2f);
                float Rotate = (float)((int)(WorldTime) % 100) * 20.f;
                float Scale = 5.f + (rand() % 10);
                float x =
                    cell.x + (m_pNewInventoryCtrl->GetItem(i)->x + w + float(rand()) / RAND_MAX) *
                                 cell.width;
                float y =
                    cell.y + (m_pNewInventoryCtrl->GetItem(i)->y + h + float(rand()) / RAND_MAX) *
                                 cell.height;
                RenderBitmapRotate(BITMAP_SHINY, x, y, Scale, Scale, 0);
                RenderBitmapRotate(BITMAP_SHINY, x, y, Scale, Scale, Rotate);
                RenderBitmapRotate(BITMAP_SHINY + 1, x, y, Scale * 3.f, Scale * 3.f, Rotate);
                RenderBitmapRotate(BITMAP_LIGHT, x, y, Scale * 6.f, Scale * 6.f, 0);
            }
        }
    }
    DisableAlphaBlend();
}

void CNewUIMixInventory::StageModernContent()
{
    m_ModernPanel.SetSlotFrames(m_pNewInventoryCtrl->SlotIconFrames());
    wchar_t szText[256]{};
    switch (g_MixRecipeMgr.GetMixInventoryType())
    {
    case SEASON3A::MIXTYPE_GOBLIN_NORMAL:
        mu_swprintf(szText, L"%ls", I18N::Game::RegularCombination);
        break;
    case SEASON3A::MIXTYPE_GOBLIN_CHAOSITEM:
        mu_swprintf(szText, L"%ls", I18N::Game::ChaosWeaponCombination);
        break;
    case SEASON3A::MIXTYPE_GOBLIN_ADD380:
        mu_swprintf(szText, L"%ls", I18N::Game::ItemOptionCombination);
        break;
    case SEASON3A::MIXTYPE_CASTLE_SENIOR:
        mu_swprintf(szText, L"%ls", I18N::Game::Store1640);
        break;
    case SEASON3A::MIXTYPE_TRAINER:
        mu_swprintf(szText, L"%ls", I18N::Game::ResurrectSpirit);
        break;
    case SEASON3A::MIXTYPE_OSBOURNE:
        mu_swprintf(szText, L"%ls", I18N::Game::Refine);
        break;
    case SEASON3A::MIXTYPE_JERRIDON:
        mu_swprintf(szText, L"%ls", I18N::Game::Restore);
        break;
    case SEASON3A::MIXTYPE_ELPIS:
        mu_swprintf(szText, L"%ls", I18N::Game::Refine);
        break;
    case SEASON3A::MIXTYPE_CHAOS_CARD:
        mu_swprintf(szText, L"%ls", I18N::Game::ChaosCardCombination);
        break;
    case SEASON3A::MIXTYPE_CHERRYBLOSSOM:
        mu_swprintf(szText, L"%ls", I18N::Game::SpiritOfCherryBlossoms);
        break;
    case SEASON3A::MIXTYPE_EXTRACT_SEED:
        mu_swprintf(szText, L"%ls", I18N::Game::Extraction);
        break;
    case SEASON3A::MIXTYPE_SEED_SPHERE:
        mu_swprintf(szText, L"%ls", I18N::Game::Assembly);
        break;
    case SEASON3A::MIXTYPE_ATTACH_SOCKET:
        mu_swprintf(szText, L"%ls", I18N::Game::Application);
        break;
    case SEASON3A::MIXTYPE_DETACH_SOCKET:
        mu_swprintf(szText, L"%ls", I18N::Game::Destruction);
        break;
    default:
        mu_swprintf(szText, L"%ls", I18N::Game::Chaos);
        break;
    }

    m_ModernPanel.SetText("tfTitle", szText);
    const int type = g_MixRecipeMgr.GetMixInventoryType();
    const wchar_t *action = I18N::Game::Combining;
    switch (type)
    {
    case SEASON3A::MIXTYPE_TRAINER:
        action = I18N::Game::Resurrection;
        break;
    case SEASON3A::MIXTYPE_OSBOURNE:
    case SEASON3A::MIXTYPE_ELPIS:
        action = I18N::Game::Refine;
        break;
    case SEASON3A::MIXTYPE_JERRIDON:
        action = I18N::Game::Restore;
        break;
    case SEASON3A::MIXTYPE_EXTRACT_SEED:
        action = I18N::Game::Extraction;
        break;
    case SEASON3A::MIXTYPE_SEED_SPHERE:
        action = I18N::Game::Assembly;
        break;
    case SEASON3A::MIXTYPE_ATTACH_SOCKET:
        action = I18N::Game::Application;
        break;
    case SEASON3A::MIXTYPE_DETACH_SOCKET:
        action = I18N::Game::Destruction;
        break;
    }
    const bool sockets =
        type == SEASON3A::MIXTYPE_ATTACH_SOCKET || type == SEASON3A::MIXTYPE_DETACH_SOCKET;
    m_ModernPanel.SetText("btnMix-label", action);
    m_ModernPanel.SetShown("btnMix", GetMixState() != MIX_FINISHED);
    m_ModernPanel.SetEnabled("btnMix", GetMixState() == MIX_READY);
    m_ModernPanel.SetShown("mcScrollList", sockets);
    m_ModernPanel.SetFrame("panel", sockets ? 2 : 1);
    m_ModernPanel.SetText("tfSockListTitle", type == SEASON3A::MIXTYPE_ATTACH_SOCKET
                                                 ? I18N::Game::SelectApplicableSocket
                                                 : I18N::Game::SelectDestructibleSocket);
    std::string info, guide;
    if (GetMixState() != MIX_FINISHED)
    {
        if (type == SEASON3A::MIXTYPE_GOBLIN_NORMAL || type == SEASON3A::MIXTYPE_GOBLIN_CHAOSITEM ||
            type == SEASON3A::MIXTYPE_GOBLIN_ADD380 || type == SEASON3A::MIXTYPE_TRAINER)
        {
            mu_swprintf(szText, I18N::Game::TaxRateDChangedInRealTime, g_nChaosTaxRate);
            MixInventoryDetail::AppendMixText(info, szText);
        }
        const bool ready = g_MixRecipeMgr.IsReadyToMix();
        for (int line = 1; line <= 2; ++line)
            if (g_MixRecipeMgr.GetCurRecipeName(szText, line))
                MixInventoryDetail::AppendMixText(info, szText, ready ? "ready" : "missing");
        switch (type)
        {
        case SEASON3A::MIXTYPE_GOBLIN_NORMAL:
        case SEASON3A::MIXTYPE_GOBLIN_CHAOSITEM:
        case SEASON3A::MIXTYPE_GOBLIN_ADD380:
        case SEASON3A::MIXTYPE_TRAINER:
        case SEASON3A::MIXTYPE_OSBOURNE:
        case SEASON3A::MIXTYPE_ELPIS:
        case SEASON3A::MIXTYPE_EXTRACT_SEED:
        case SEASON3A::MIXTYPE_SEED_SPHERE:
            mu_swprintf(szText, I18N::Game::SSuccessRateD, action, g_MixRecipeMgr.GetSuccessRate());
            if (ready && g_MixRecipeMgr.GetPlusChaosRate() > 0 &&
                g_MixRecipeMgr.GetCurRecipe()->m_bMixOption == 'F')
            {
                wchar_t combined[256];
                mu_swprintf(combined, L"%ls + %d%%", szText, g_MixRecipeMgr.GetPlusChaosRate());
                MixInventoryDetail::AppendMixText(info, combined, "ready");
            }
            else
                MixInventoryDetail::AppendMixText(info, szText);
            break;
        }
        if (type != SEASON3A::MIXTYPE_OSBOURNE && type != SEASON3A::MIXTYPE_ELPIS &&
            type != SEASON3A::MIXTYPE_CHAOS_CARD && type != SEASON3A::MIXTYPE_CHERRYBLOSSOM)
        {
            wchar_t gold[32], taxed[32];
            ConvertGold(g_MixRecipeMgr.GetReqiredZen(), gold);
            ConvertChaosTaxGold(g_MixRecipeMgr.GetReqiredZen(), taxed);
            mu_swprintf(szText,
                        ready && g_MixRecipeMgr.GetCurRecipe()->m_bRequiredZenType == 'C'
                            ? I18N::Game::RequiredZenForPotionSS
                            : I18N::Game::RequiredZenSS,
                        taxed, gold);
            MixInventoryDetail::AppendMixText(info, szText);
        }
        if (g_MixRecipeMgr.GetMostSimilarRecipe())
        {
            if (!ready && g_MixRecipeMgr.GetMostSimilarRecipeName(szText, 1))
            {
                wchar_t prediction[256];
                mu_swprintf(prediction, I18N::Game::AssemblyPredictionS, szText);
                MixInventoryDetail::AppendMixText(guide, prediction);
            }
            for (int line = 0; line < 8; ++line)
            {
                const int status = g_MixRecipeMgr.GetSourceName(line, szText);
                if (status == SEASON3A::MIX_SOURCE_ERROR)
                    break;
                MixInventoryDetail::AppendMixText(guide, szText,
                                                  status == SEASON3A::MIX_SOURCE_NO ? "missing"
                                                  : status == SEASON3A::MIX_SOURCE_PARTIALLY
                                                      ? "partial"
                                                      : "ready");
            }
        }
        else
            MixInventoryDetail::AppendMixText(guide,
                                              g_MixRecipeMgr.IsMixInit()
                                                  ? I18N::Game::PleaseUploadTheAssemblyItems
                                                  : I18N::Game::ImproperItemsForCombination,
                                              "missing");
        for (int line = 1; line <= 3; ++line)
        {
            const bool hasText = ready ? g_MixRecipeMgr.GetCurRecipeDesc(szText, line)
                                       : g_MixRecipeMgr.GetMostSimilarRecipe() &&
                                             g_MixRecipeMgr.GetRecipeAdvice(szText, line);
            if (hasText)
                MixInventoryDetail::AppendMixText(guide, szText, "warning");
        }
        BuildMixDescriptions(guide);
    }
    m_ModernPanel.SetMarkup("tfMent", info);
    m_ModernPanel.SetMarkup("taGuide01", guide);
}
bool CNewUIMixInventory::PrepareModernUiOnWorker(int width, int height)
{
    return m_ModernPanel.PrepareOnWorker(width, height);
}

void SEASON3B::CNewUIMyShopInventory::SetPos(int x, int y)
{
    (void)x;
    (void)y;
}

void SEASON3B::CNewUIMyShopInventory::SetTitle(wchar_t *titletext)
{
    if (titletext == nullptr)
    {
        m_ShopTitle.clear();
        return;
    }
    m_ShopTitle.assign(titletext, wcsnlen(titletext, PersonalShopDetail::iMAX_SHOPTITLE_MULTI - 1));
    std::erase(m_ShopTitle, L'|');
}

bool SEASON3B::CNewUIMyShopInventory::Render()
{
    bool recorded = m_ModernPanel.Record(renderUnit.LegacyRender());
    if (m_pNewInventoryCtrl)
    {
        m_pNewInventoryCtrl->Render();
        recorded = m_pNewInventoryCtrl->RenderOwnerLayer() && recorded;
    }
    return recorded;
}

bool SEASON3B::CNewUIMyShopInventory::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight)
{
    return m_ModernPanel.PrepareOnWorker(viewportWidth, viewportHeight, m_ModernVisible,
                                         m_ModernContent);
}

// ?
#ifdef _PVP_ADD_MOVE_SCROLL
extern CMurdererMove g_MurdererMove;
#endif // _PVP_ADD_MOVE_SCROLL
void SessionRenderUnit::RenderTipTextList(const int sx, const int sy, int TextNum, int Tab,
                                          int iSort, int iRenderPoint, BOOL bUseBG)
{
    UI::Modern::RmlTooltipRequest request;
    // Tooltips share the full UI canvas; Grid scales that canvas into each tile.
    request.x =
        static_cast<int>(sx * static_cast<float>(ModernUiLogicalViewportWidth()) / REFERENCE_WIDTH);
    request.y = static_cast<int>(sy * static_cast<float>(ModernUiLogicalViewportHeight()) /
                                 REFERENCE_HEIGHT);
    request.width = Tab > 0 ? Tab * 2 : 0;
    request.bottomAnchored = iRenderPoint == STRP_BOTTOMCENTER;
    request.lineCount = std::min(TextNum, UI::Modern::RmlTooltipRequest::LineCapacity);
    request.alignment = iSort == RT3_SORT_LEFT || iSort == RT3_SORT_LEFT_CLIP
                            ? UI::Modern::RmlTooltipAlignment::Left
                        : iSort == RT3_SORT_RIGHT ? UI::Modern::RmlTooltipAlignment::Right
                                                  : UI::Modern::RmlTooltipAlignment::Center;
    request.framed = bUseBG == TRUE;
    for (int i = 0; i < request.lineCount; i++)
    {
        if (TextList[i][0] == L'\0')
        {
            request.lineCount = i;
            break;
        }
        UI::Modern::RmlTooltipLine &line = request.lines[i];
        wcsncpy(line.text, TextList[i], UI::Modern::RmlTooltipLine::TextCapacity - 1);
        line.text[UI::Modern::RmlTooltipLine::TextCapacity - 1] = L'\0';
        line.color = ItemRulesDetail::TooltipColor(TextListColor[i]);
        line.background = ItemRulesDetail::TooltipBackground(TextListColor[i]);
        line.bold = TextBold[i] != 0;
    }
    if (request.lineCount == 0)
        return;
    tooltipLayer_.Stage(request);
}

bool SessionRenderUnit::RecordTooltips()
{
    return tooltipLayer_.Record(LegacyRender());
}

void SessionRenderUnit::RenderHelpLine(int iColumnType, const wchar_t *pPrintStyle, int &TabSpace,
                                       const wchar_t *pGapText, int Pos_y, int iType)
{
    int iCurrMaxLevel = ItemRulesDetail::iMaxLevel;

    if (iType == 5)
        iCurrMaxLevel = 0;

    for (int Level = 0; Level <= iCurrMaxLevel; ++Level)
    {
        mu_swprintf(TextList[TextNum], pPrintStyle, g_iItemInfo[Level][iColumnType]);
        if (g_iItemInfo[Level][_COLUMN_TYPE_CAN_EQUIP] == TRUE)
        {
            TextListColor[Level] = TEXT_COLOR_WHITE;
        }
        else
        {
            TextListColor[Level] = TEXT_COLOR_RED;
        }
        TextBold[Level] = false;
        ++TextNum;
    }

    SIZE TextSize;
    RenderTipTextList(TabSpace, Pos_y, TextNum, 0, RT3_SORT_CENTER, FALSE);

    if (pGapText == NULL)
    {
        g_RenderText.MeasureText(TextList[TextNum - 1], lstrlen(TextList[TextNum - 1]), &TextSize);
    }
    else
    {
        g_RenderText.MeasureText(pGapText, wcslen(pGapText), &TextSize);
    }
    TabSpace += int(TextSize.cx / g_fScreenRate_x);
    if (iType == 6)
    {
        TabSpace += 5;
    }
    TextNum -= iCurrMaxLevel + 1;
}

void SessionRenderUnit::RenderHelpCategory(int iColumnType, int Pos_x, int Pos_y)
{
    const wchar_t *pText = NULL;

    switch (iColumnType)
    {
    case _COLUMN_TYPE_LEVEL:
        pText = I18N::Game::LV;
        break;
    case _COLUMN_TYPE_ATTMIN:
    case _COLUMN_TYPE_ATTMAX:
        pText = I18N::Game::ATKDmg;
        break;
    case _COLUMN_TYPE_MAGIC:
        pText = I18N::Game::WIZDmg;
        break;
    case _COLUMN_TYPE_CURSE:
        pText = I18N::Game::Curse;
        break;
    case _COLUMN_TYPE_PET_ATTACK:
        pText = I18N::Game::Attack;
        break;
    case _COLUMN_TYPE_DEFENCE:
        pText = I18N::Game::DEF;
        break;
    case _COLUMN_TYPE_DEFRATE:
        pText = I18N::Game::DEFRate;
        break;
    case _COLUMN_TYPE_REQSTR:
        pText = I18N::Game::STR;
        break;
    case _COLUMN_TYPE_REQDEX:
        pText = I18N::Game::AGI;
        break;
    case _COLUMN_TYPE_REQENG:
        pText = I18N::Game::ENG;
        break;
    case _COLUMN_TYPE_REQCHA:
        pText = I18N::Game::Command;
        break;
    case _COLUMN_TYPE_REQVIT:
        pText = I18N::Game::STA;
        break;
    case _COLUMN_TYPE_REQNLV:
        pText = I18N::Game::ReqLV;
        break;
    default:
        break;
    }
    mu_swprintf(TextList[TextNum], pText);
    TextListColor[TextNum] = TEXT_COLOR_BLUE;
    TextNum++;
    RenderTipTextList(Pos_x, Pos_y, TextNum, 0, RT3_SORT_RIGHT, FALSE);
    TextNum = 0;
}

void SessionRenderUnit::ComputeItemInfo(int iHelpItem)
{
    if (g_iCurrentItem == iHelpItem)
        return;
    else
        g_iCurrentItem = iHelpItem;

    ITEM_ATTRIBUTE *p = &ItemAttribute[ItemHelp];

    for (int Level = 0; Level <= ItemRulesDetail::iMaxLevel; Level++)
    {
        int RequireStrength = 0;
        int RequireDexterity = 0;
        int RequireEnergy = 0;
        int RequireCharisma = 0;
        int RequireVitality = 0;
        int RequireLevel = 0;
        int DamageMin = p->DamageMin;
        int DamageMax = p->DamageMax;
        int Defense = p->Defense;
        int Magic = p->MagicPower;
        int Blocking = p->SuccessfulBlocking;

        if (DamageMin > 0)
        {
            DamageMin += (std::min<int>(9, Level) * 3);
            switch (Level - 9)
            {
            case 6:
                DamageMin += 9;
                break; // +15
            case 5:
                DamageMin += 8;
                break; // +14
            case 4:
                DamageMin += 7;
                break; // +13
            case 3:
                DamageMin += 6;
                break; // +12
            case 2:
                DamageMin += 5;
                break; // +11
            case 1:
                DamageMin += 4;
                break; // +10
            default:
                break;
            };
        }
        if (DamageMax > 0)
        {
            DamageMax += (std::min<int>(9, Level) * 3);
            switch (Level - 9)
            {
            case 6:
                DamageMax += 9;
                break; // +15
            case 5:
                DamageMax += 8;
                break; // +14
            case 4:
                DamageMax += 7;
                break; // +13
            case 3:
                DamageMax += 6;
                break; // +12
            case 2:
                DamageMax += 5;
                break; // +11
            case 1:
                DamageMax += 4;
                break; // +10
            default:
                break;
            };
        }

        if (Magic > 0)
        {
            Magic += (std::min<int>(9, Level) * 3); // ~ +9
            switch (Level - 9)
            {
            case 6:
                Magic += 9;
                break; // +15
            case 5:
                Magic += 8;
                break; // +14
            case 4:
                Magic += 7;
                break; // +13
            case 3:
                Magic += 6;
                break; // +12
            case 2:
                Magic += 5;
                break; // +11
            case 1:
                Magic += 4;
                break; // +10
            default:
                break;
            };
            Magic /= 2;

            if (IsCepterItem(ItemHelp) == false)
            {
                Magic += Level * 2;
            }
        }

        if (Defense > 0)
        {
            if (ItemHelp >= ITEM_SHIELD && ItemHelp < ITEM_SHIELD + MAX_ITEM_INDEX)
            {
                Defense += Level;
            }
            else
            {
                Defense += (std::min<int>(9, Level) * 3); // ~ +9
                switch (Level - 9)
                {
                case 6:
                    Defense += 9;
                    break; // +15
                case 5:
                    Defense += 8;
                    break; // +14
                case 4:
                    Defense += 7;
                    break; // +13
                case 3:
                    Defense += 6;
                    break; // +12
                case 2:
                    Defense += 5;
                    break; // +11
                case 1:
                    Defense += 4;
                    break; // +10
                default:
                    break;
                };
            }
        }
        if (Blocking > 0)
        {
            Blocking += (std::min<int>(9, Level) * 3); // ~ +9
            switch (Level - 9)
            {
            case 6:
                Blocking += 9;
                break; // +15
            case 5:
                Blocking += 8;
                break; // +14
            case 4:
                Blocking += 7;
                break; // +13
            case 3:
                Blocking += 6;
                break; // +12
            case 2:
                Blocking += 5;
                break; // +11
            case 1:
                Blocking += 4;
                break; // +10
            default:
                break;
            };
        }

        if (p->RequireLevel)
        {
            RequireLevel = p->RequireLevel;
        }
        else
        {
            RequireLevel = 0;
        }

        if (p->RequireStrength)
        {
            RequireStrength = 20 + p->RequireStrength * (p->Level + Level * 3) * 3 / 100;
        }
        else
        {
            RequireStrength = 0;
        }

        if (p->RequireDexterity)
            RequireDexterity = 20 + p->RequireDexterity * (p->Level + Level * 3) * 3 / 100;
        else
            RequireDexterity = 0;

        if (p->RequireVitality)
            RequireVitality = 20 + p->RequireVitality * (p->Level + Level * 3) * 3 / 100;
        else
            RequireVitality = 0;

        if (p->RequireEnergy)
        {
            if (ItemHelp >= ITEM_BOOK_OF_SAHAMUTT && ItemHelp <= ITEM_STAFF + 29)
            {
                RequireEnergy = 20 + p->RequireEnergy * (p->Level + Level * 1) * 3 / 100;
            }
            else
            {
                if ((p->RequireLevel > 0) &&
                    (ItemHelp >= ITEM_ETC && ItemHelp < ITEM_ETC + MAX_ITEM_INDEX))
                {
                    RequireEnergy = 20 + (p->RequireEnergy) * (p->RequireLevel) * 4 / 100;
                }
                else
                {
                    RequireEnergy = 20 + p->RequireEnergy * (p->Level + Level * 3) * 4 / 100;
                }
            }
        }
        else
        {
            RequireEnergy = 0;
        }

        if (p->RequireCharisma)
            RequireCharisma = 20 + p->RequireCharisma * (p->Level + Level * 3) * 3 / 100;
        else
            RequireCharisma = 0;

        g_iItemInfo[Level][_COLUMN_TYPE_LEVEL] = Level;
        g_iItemInfo[Level][_COLUMN_TYPE_ATTMIN] = DamageMin;
        g_iItemInfo[Level][_COLUMN_TYPE_ATTMAX] = DamageMax;

        if (ItemHelp >= ITEM_BOOK_OF_SAHAMUTT && ItemHelp <= ITEM_STAFF + 29)
        {
            g_iItemInfo[Level][_COLUMN_TYPE_CURSE] = Magic;
        }
        else
        {
            g_iItemInfo[Level][_COLUMN_TYPE_MAGIC] = Magic;
        }

        g_iItemInfo[Level][_COLUMN_TYPE_PET_ATTACK] = Magic;
        g_iItemInfo[Level][_COLUMN_TYPE_DEFENCE] = Defense;
        g_iItemInfo[Level][_COLUMN_TYPE_DEFRATE] = Blocking;
        g_iItemInfo[Level][_COLUMN_TYPE_REQSTR] = RequireStrength;
        g_iItemInfo[Level][_COLUMN_TYPE_REQDEX] = RequireDexterity;
        g_iItemInfo[Level][_COLUMN_TYPE_REQENG] = RequireEnergy;
        g_iItemInfo[Level][_COLUMN_TYPE_REQCHA] = RequireCharisma;
        g_iItemInfo[Level][_COLUMN_TYPE_REQVIT] = RequireVitality;
        g_iItemInfo[Level][_COLUMN_TYPE_REQNLV] = RequireLevel;

        if (IsCepterItem(ItemHelp) == true)

        {
            g_iItemInfo[Level][_COLUMN_TYPE_MAGIC] = 0;
        }
        else
        {
            g_iItemInfo[Level][_COLUMN_TYPE_PET_ATTACK] = 0;
        }

        WORD Strength, Dexterity, Energy, Vitality, Charisma;

        Strength = CharacterAttribute->Strength + CharacterAttribute->AddStrength;
        Dexterity = CharacterAttribute->Dexterity + CharacterAttribute->AddDexterity;
        Energy = CharacterAttribute->Energy + CharacterAttribute->AddEnergy;
        Vitality = CharacterAttribute->Vitality + CharacterAttribute->AddVitality;
        Charisma = CharacterAttribute->Charisma + CharacterAttribute->AddCharisma;

        if (RequireStrength <= Strength && RequireDexterity <= Dexterity &&
            RequireEnergy <= Energy && RequireVitality <= Vitality && RequireCharisma <= Charisma &&
            RequireLevel <= CharacterAttribute->Level)
            g_iItemInfo[Level][_COLUMN_TYPE_CAN_EQUIP] = TRUE;
        else
            g_iItemInfo[Level][_COLUMN_TYPE_CAN_EQUIP] = FALSE;
    }
}

void SessionRenderUnit::GetSpecialOptionText(int Type, wchar_t *Text, WORD Option, BYTE Value,
                                             int iMana)
{
    switch (Option)
    {
    case AT_SKILL_BLOCKING:
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::DefendSkillManaD, iMana);
        break;
    case AT_SKILL_FALLING_SLASH:
    case AT_SKILL_FALLING_SLASH_STR:
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::FallingSlashSkillManaD, iMana);
        break;
    case AT_SKILL_LUNGE:
    case AT_SKILL_LUNGE_STR:
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::LungeSkillManaD, iMana);
        break;
    case AT_SKILL_UPPERCUT:
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::UppercutSkillManaD, iMana);
        break;
    case AT_SKILL_CYCLONE:
    case AT_SKILL_CYCLONE_STR:
    case AT_SKILL_CYCLONE_STR_MG:
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::CycloneCuttingSkillManaD, iMana);
        break;
    case AT_SKILL_SLASH:
    case AT_SKILL_SLASH_STR:
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::SlashingSkillManaD, iMana);
        break;
    case AT_SKILL_TRIPLE_SHOT:
    case AT_SKILL_TRIPLE_SHOT_STR:
    case AT_SKILL_TRIPLE_SHOT_MASTERY:
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::TripleShotSkillManaD, iMana);
        break;
    case AT_SKILL_BLAST_CROSSBOW4:
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::_4ShotSkillManaD, iMana);
        break;
    case AT_SKILL_MULTI_SHOT:
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::_4ShotSkillManaD, iMana);
        break;
    case AT_SKILL_RECOVER:
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::_4ShotSkillManaD, iMana);
        break;
    case AT_SKILL_POWER_SLASH:
    case AT_SKILL_POWER_SLASH_STR:
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::PowerSlashSkillManaD, iMana);
        break;
    case AT_LUCK:
        mu_swprintf(Text, I18N::Game::LuckSuccessRateOfJewelOfSoul25);
        break;
    case AT_IMPROVE_DAMAGE:
        mu_swprintf(Text, I18N::Game::AdditionalDmgD, Value);
        break;
    case AT_IMPROVE_MAGIC:
        mu_swprintf(Text, I18N::Game::AdditionalWizardryDmgD, Value);
        break;
    case AT_IMPROVE_CURSE:
        mu_swprintf(Text, I18N::Game::AdditionalCurseSpellD, Value);
        break;
    case AT_IMPROVE_BLOCKING:
        mu_swprintf(Text, I18N::Game::AdditionalDefenseRateD, Value);
        break;
    case AT_IMPROVE_DEFENSE:
        mu_swprintf(Text, I18N::Game::AdditionalDefenseD, Value);
        break;
    case AT_LIFE_REGENERATION:
        if (!(ITEM_LOCHS_FEATHER <= Type && Type <= ITEM_INVISIBILITY_CLOAK))
        {
            mu_swprintf(Text, I18N::Game::AutomaticHPRecoveryD, Value);
        }
        break;
    case AT_IMPROVE_LIFE:
        mu_swprintf(Text, I18N::Game::IncreaseMaxHP4);
        break;
    case AT_IMPROVE_MANA:
        mu_swprintf(Text, I18N::Game::IncreaseMaxMana4);
        break;
    case AT_DECREASE_DAMAGE:
        mu_swprintf(Text, I18N::Game::DamageDecrease4);
        break;

    case AT_REFLECTION_DAMAGE:
        mu_swprintf(Text, I18N::Game::ReflectDamage5);
        break;
    case AT_IMPROVE_BLOCKING_PERCENT:
        mu_swprintf(Text, I18N::Game::DefenseSuccessRate10);
        break;
    case AT_IMPROVE_GAIN_GOLD:
        mu_swprintf(Text, I18N::Game::IncreasesAcquisitionRateOfZenAfterHuntingMonsters30);
        break;
    case AT_EXCELLENT_DAMAGE:
        mu_swprintf(Text, I18N::Game::ExcellentDamageRate10);
        break;
    case AT_IMPROVE_DAMAGE_LEVEL:
        mu_swprintf(Text, I18N::Game::IncreaseDamageLevel20);
        break;
    case AT_IMPROVE_DAMAGE_PERCENT:
        mu_swprintf(Text, I18N::Game::IncreaseDamageD, Value);
        break;
    case AT_IMPROVE_MAGIC_LEVEL:
        mu_swprintf(Text, I18N::Game::IncreaseWizardryDmgLevel20);
        break;
    case AT_IMPROVE_MAGIC_PERCENT:
        mu_swprintf(Text, I18N::Game::IncreaseWizardryDmgD, Value);
        break;
    case AT_IMPROVE_ATTACK_SPEED:
        mu_swprintf(Text, I18N::Game::IncreaseAttackingWizardrySpeedD, Value);
        break;
    case AT_IMPROVE_GAIN_LIFE:
        mu_swprintf(Text, I18N::Game::IncreasesAcquisitionRateOfLifeAfterHuntingMonstersLife8);
        break;
    case AT_IMPROVE_GAIN_MANA:
        mu_swprintf(Text, I18N::Game::IncreasesAcquisitionRateOfManaAfterHuntingMonstersMana8);
        break;
    case AT_IMPROVE_EVADE:
        mu_swprintf(Text, I18N::Game::Parrying10Increased);
        break;
    case AT_SKILL_RIDER:
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::RaidSkillManaD, iMana);
        break;
    case AT_SKILL_FORCE: //
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::LongSpearSkillManaD, iMana);
        break;
    case AT_SKILL_FORCE_WAVE:
    case AT_SKILL_FORCE_WAVE_STR:
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::ForceWaveSkillManaD, iMana);
        break;
    case AT_SKILL_EARTHSHAKE:
    case AT_SKILL_EARTHSHAKE_STR:
    case AT_SKILL_EARTHSHAKE_MASTERY:
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::EarthShakeSkillManaD, iMana);
        break;
    case AT_SKILL_PLASMA_STORM_FENRIR:
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::PlasmaStormSkillManaD, iMana);
        break;
    case AT_SET_OPTION_IMPROVE_DEFENCE:
        mu_swprintf(Text, I18N::Game::IncreaseDefensiveSkillD, Value);
        break;
    case AT_SET_OPTION_IMPROVE_CHARISMA:
        mu_swprintf(Text, I18N::Game::IncreaseCommandD, Value);
        break;
    case AT_SET_OPTION_IMPROVE_DAMAGE:
        mu_swprintf(Text, I18N::Game::IncreaseDOfDamage, Value);
        break;
    case AT_IMPROVE_HP_MAX:
        mu_swprintf(Text, I18N::Game::HPDIncreased, Value);
        break;
    case AT_IMPROVE_MP_MAX:
        mu_swprintf(Text, I18N::Game::ManaDIncreased, Value);
        break;
    case AT_ONE_PERCENT_DAMAGE:
        mu_swprintf(Text, I18N::Game::IgnorOpponentSDefensivePowerByD, Value);
        break;
    case AT_IMPROVE_AG_MAX:
        mu_swprintf(Text, I18N::Game::MaxAGDIncreased, Value);
        break;
    case AT_DAMAGE_ABSORB:
        mu_swprintf(Text, I18N::Game::AbsorbDAdditionalDamage, Value);
        break;
    case AT_SET_OPTION_IMPROVE_STRENGTH:
        mu_swprintf(Text, I18N::Game::IncreaseStrengthD, Value);
        break;
    case AT_SET_OPTION_IMPROVE_DEXTERITY:
        mu_swprintf(Text, I18N::Game::IncreaseAgilityD, Value);
        break;
    case AT_SET_OPTION_IMPROVE_VITALITY:
        mu_swprintf(Text, I18N::Game::IncreaseStaminaD, Value);
        break;
    case AT_SET_OPTION_IMPROVE_ENERGY:
        mu_swprintf(Text, I18N::Game::IncreaseEnergyD, Value);
        break;
    case AT_IMPROVE_MAX_MANA:
        mu_swprintf(Text, I18N::Game::MaxManaIncreasedByD, Value);
        break;
    case AT_IMPROVE_MAX_AG:
        mu_swprintf(Text, I18N::Game::MaxAGIncreasedByD, Value);
        break;
    case AT_DAMAGE_REFLECTION:
        mu_swprintf(Text, I18N::Game::ReturnSTheEnemySAttackPowerInD, Value);
        break;
    case AT_RECOVER_FULL_LIFE:
        mu_swprintf(Text, I18N::Game::CompleteRecoveryOfLifeInDRate, Value);
        break;
    case AT_RECOVER_FULL_MANA:
        mu_swprintf(Text, I18N::Game::CompleteRecoverOfManaInDRate, Value);
        break;
    case AT_SKILL_SUMMON_EXPLOSION:
        mu_swprintf(Text, I18N::Game::ExplosionSkillManaD, iMana);
        break;
    case AT_SKILL_SUMMON_REQUIEM:
        mu_swprintf(Text, I18N::Game::RequiemManaD, iMana);
        break;
    case AT_SKILL_SUMMON_POLLUTION:
        mu_swprintf(Text, I18N::Game::PollutionSkillManaD, iMana);
        break;
    case AT_SKILL_KILLING_BLOW:
    case AT_SKILL_KILLING_BLOW_STR:
    case AT_SKILL_KILLING_BLOW_MASTERY:
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::KillingBlowManaD, iMana);
        break;
    case AT_SKILL_BEAST_UPPERCUT:
    case AT_SKILL_BEAST_UPPERCUT_STR:
    case AT_SKILL_BEAST_UPPERCUT_MASTERY:
        gSkillManager.GetSkillInformation(Option, 1, NULL, &iMana, NULL);
        mu_swprintf(Text, I18N::Game::BeastUppercutManaD, iMana);
        break;
    }
}

void SessionRenderUnit::RenderItemInfo(int sx, int sy, ITEM *ip, bool Sell, int Inventype,
                                       bool bItemTextListBoxUse)
{
    if (ip->Type == -1)
        return;

    tm *ExpireTime;
    if (ip->bPeriodItem == true && ip->bExpiredPeriod == false)
    {
        _tzset();
        if (ip->lExpireTime == 0)
            return;

        ExpireTime = localtime((time_t *)&(ip->lExpireTime));
    }

    ITEM_ATTRIBUTE *p = &ItemAttribute[ip->Type];
    TextNum = 0;
    SkipNum = 0;

    ZeroMemory(TextListColor, 20 * sizeof(int));
    for (int i = 0; i < 30; i++)
    {
        TextList[i][0] = 0;
    }

    if (!Sell && (ip->Type == ITEM_DARK_HORSE_ITEM || ip->Type == ITEM_DARK_RAVEN_ITEM))
    {
        RenderPetItemInfo(sx, sy, ip, Inventype);
        return;
    }

    mu_swprintf(TextList[TextNum], L"\n");
    TextNum++;
    SkipNum++;

    int Level = ip->Level;
    int Color;

    if (ip->Type == ITEM_JEWEL_OF_BLESS || ip->Type == ITEM_JEWEL_OF_SOUL ||
        ip->Type == ITEM_JEWEL_OF_CHAOS || ip->Type == ITEM_JEWEL_OF_GUARDIAN ||
        (isCompiledGem(ip)) || ip->Type == ITEM_FLAME_OF_DEATH_BEAM_KNIGHT ||
        ip->Type == ITEM_HORN_OF_HELL_MAINE || ip->Type == ITEM_FEATHER_OF_DARK_PHOENIX ||
        ip->Type == ITEM_EYE_OF_ABYSSAL || ip->Type == ITEM_FLAME_OF_CONDOR ||
        ip->Type == ITEM_FEATHER_OF_CONDOR || ip->Type == ITEM_POTION + 100 ||
        (ip->Type >= ITEM_POTION + 141 && ip->Type <= ITEM_POTION + 144) ||
        (ip->Type >= ITEM_HELPER + 135 && ip->Type <= ITEM_HELPER + 145) ||
        (ip->Type == ITEM_POTION + 160 || ip->Type == ITEM_POTION + 161) ||
        ip->Type == ITEM_JEWEL_OF_LIFE || ip->Type == ITEM_JEWEL_OF_CREATION)
    {
        Color = TEXT_COLOR_YELLOW;
    }
    else if (ItemRulesDetail::IsDivineArchangelWeaponItem(ip->Type))
    {
        Color = TEXT_COLOR_PURPLE;
    }
    else if (ip->Type == ITEM_DEVILS_EYE || ip->Type == ITEM_DEVILS_KEY ||
             ip->Type == ITEM_DEVILS_INVITATION)
    {
        Color = TEXT_COLOR_YELLOW;
    }
    else if (ip->Type == ITEM_SCROLL_OF_ARCHANGEL || ip->Type == ITEM_BLOOD_BONE)
    {
        Color = TEXT_COLOR_YELLOW;
    }
    else if (ip->AncientDiscriminator > 0)
    {
        Color = TEXT_COLOR_GREEN_BLUE;
    }
    else if (g_SocketItemMgr.IsSocketItem(ip))
    {
        Color = TEXT_COLOR_VIOLET;
    }
    else if (ip->SpecialNum > 0 && ip->ExcellentFlags > 0)
    {
        Color = TEXT_COLOR_GREEN;
    }
    else if (Level >= 7)
    {
        Color = TEXT_COLOR_YELLOW;
    }
    else
    {
        if (ip->SpecialNum > 0)
        {
            Color = TEXT_COLOR_BLUE;
        }
        else
        {
            Color = TEXT_COLOR_WHITE;
        }
    }

    if ((ip->Type >= ITEM_WINGS_OF_SPIRITS && ip->Type <= ITEM_WINGS_OF_DARKNESS) ||
        ip->Type == ITEM_CAPE_OF_LORD ||
        (ip->Type >= ITEM_WING_OF_STORM && ip->Type <= ITEM_CAPE_OF_EMPEROR) ||
        (ip->Type >= ITEM_WINGS_OF_DESPAIR && ip->Type <= ITEM_WING_OF_DIMENSION) ||
        (ip->Type >= ITEM_CAPE_OF_FIGHTER && ip->Type <= ITEM_CAPE_OF_OVERRULE))
    {
        if (Level >= 7)
        {
            Color = TEXT_COLOR_YELLOW;
        }
        else
        {
            if (ip->SpecialNum > 0)
            {
                Color = TEXT_COLOR_BLUE;
            }
            else
            {
                Color = TEXT_COLOR_WHITE;
            }
        }
    }

    int nGemType = Check_Jewel(ip->Type);
    if (nGemType != COMGEM::NOGEM)
    {
        Color = TEXT_COLOR_YELLOW;
    }

    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_NPCSHOP) && !IsSellingBan(ip))
    {
        wchar_t Text[100];
        {
            if (Sell)
            {
                DWORD dwValue = ItemValue(ip, 0);
                ConvertGold(dwValue, Text);
                wchar_t Text2[100];

                ConvertTaxGold(ItemValue(ip, 0), Text2);
                mu_swprintf(TextList[TextNum], I18N::Game::PurchasingPriceSS, Text2, Text);
            }
            else
            {
                ConvertGold(ItemValue(ip, 1), Text);
                mu_swprintf(TextList[TextNum], I18N::Game::SellingPriceS, Text);
            }

            TextListColor[TextNum] = Color;
            //			TextBold[TextNum] = true;
            TextNum++;
            mu_swprintf(TextList[TextNum], L"\n");
            TextNum++;
            SkipNum++;
        }
    }
    if ((Inventype == SEASON3B::TOOLTIP_TYPE_MY_SHOP ||
         Inventype == SEASON3B::TOOLTIP_TYPE_PURCHASE_SHOP) &&
        !IsPersonalShopBan(ip))
    {
        {
            int price = 0;
            int indexInv = g_pMyShopInventory->GetInventoryCtrl()->GetIndexByItem(ip);
            wchar_t Text[100];

            if (GetPersonalItemPrice(indexInv, price, g_IsPurchaseShop))
            {
                ConvertGold(price, Text);
                mu_swprintf(TextList[TextNum], I18N::Game::SellingPriceS, Text);

                if (price >= 10000000)
                    TextListColor[TextNum] = TEXT_COLOR_RED;
                else if (price >= 1000000)
                    TextListColor[TextNum] = TEXT_COLOR_YELLOW;
                else if (price >= 100000)
                    TextListColor[TextNum] = TEXT_COLOR_GREEN;
                else
                    TextListColor[TextNum] = TEXT_COLOR_WHITE;
                TextBold[TextNum] = true;
                TextNum++;
                mu_swprintf(TextList[TextNum], L"\n");
                TextNum++;
                SkipNum++;

                DWORD gold = CharacterMachine->Gold;

                if ((int)gold < price && g_IsPurchaseShop == PSHOPWNDTYPE_PURCHASE)
                {
                    TextListColor[TextNum] = TEXT_COLOR_RED;
                    TextBold[TextNum] = true;
                    mu_swprintf(TextList[TextNum], I18N::Game::YouAreShortOfZen);
                    TextNum++;
                    mu_swprintf(TextList[TextNum], L"\n");
                    TextNum++;
                    SkipNum++;
                }
            }
            else if (g_IsPurchaseShop == PSHOPWNDTYPE_SALE)
            {
                TextListColor[TextNum] = TEXT_COLOR_RED;
                TextBold[TextNum] = true;
                mu_swprintf(TextList[TextNum], I18N::Game::RightClickForPriceSetting);
                TextNum++;
                mu_swprintf(TextList[TextNum], L"\n");
                TextNum++;
                SkipNum++;
            }
        }
    }

    if (ip->Type >= ITEM_SCROLL_OF_EMPEROR_RING_OF_HONOR && ip->Type <= ITEM_SOUL_SHARD_OF_WIZARD)
    {
        if (ip->Type == ITEM_SCROLL_OF_EMPEROR_RING_OF_HONOR)
        {
            Color = TEXT_COLOR_YELLOW;
            switch (Level)
            {
            case 0:
                mu_swprintf(TextList[TextNum], L"%ls", p->Name);
                break;
            case 1:
                mu_swprintf(TextList[TextNum], I18N::Game::RingOfHonor);
                break;
            }
        }
        else if (ip->Type == ITEM_BROKEN_SWORD_DARK_STONE)
        {
            Color = TEXT_COLOR_YELLOW;
            switch (Level)
            {
            case 0:
                mu_swprintf(TextList[TextNum], L"%ls", p->Name);
                break;
            case 1:
                mu_swprintf(TextList[TextNum], I18N::Game::DarkStone);
                break;
            }
        }
        else
        {
            mu_swprintf(TextList[TextNum], L"%ls", p->Name);
            Color = TEXT_COLOR_YELLOW;
        }
    }
    else if (ip->Type == ITEM_POTION + 12)
    {
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], I18N::Game::Zen);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], I18N::Game::Heart);
            break;
        case 2:
            mu_swprintf(TextList[TextNum], L"%ls", ItemRulesDetail::ChaosEventName[ip->Durability]);
            break;
        }
    }
    else if (ip->Type == ITEM_BOX_OF_LUCK)
    {
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], L"%ls", p->Name);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], I18N::Game::StarOfSacredBirth);
            break;
        case 2:
            mu_swprintf(TextList[TextNum], I18N::Game::Firecracker);
            break;
        case 3:
            mu_swprintf(TextList[TextNum], I18N::Game::HeartOfLove);
            break;
        case 5:
            mu_swprintf(TextList[TextNum], I18N::Game::SilverMedal);
            break;
        case 6:
            mu_swprintf(TextList[TextNum], I18N::Game::GoldMedal);
            break;
        case 7:
            mu_swprintf(TextList[TextNum], I18N::Game::BoxOfHeaven);
            break;
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
            mu_swprintf(TextList[TextNum], L"%ls +%d", I18N::Game::BoxOfKundun, Level - 7);
            break;
        case 13:
            mu_swprintf(TextList[TextNum], I18N::Game::HeartOfDarkLord);
            break;
        case 14:
            mu_swprintf(TextList[TextNum], I18N::Game::BlueLuckyPouch);
            break;
        case 15:
            mu_swprintf(TextList[TextNum], I18N::Game::RedLuckyPouch);
            break;
        }
    }
    else if (ip->Type == ITEM_POTION + 12)
    {
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], I18N::Game::Zen);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], I18N::Game::Heart);
            break;
        case 2:
            mu_swprintf(TextList[TextNum], L"%ls", ItemRulesDetail::ChaosEventName[ip->Durability]);
            break;
        }
    }
    else if (ip->Type == ITEM_FRUITS)
    {
        Color = TEXT_COLOR_YELLOW;
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], L"%ls %ls", I18N::Game::ENG, p->Name);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], L"%ls %ls", I18N::Game::STA, p->Name);
            break;
        case 2:
            mu_swprintf(TextList[TextNum], L"%ls %ls", I18N::Game::AGI, p->Name);
            break;
        case 3:
            mu_swprintf(TextList[TextNum], L"%ls %ls", I18N::Game::STR, p->Name);
            break;
        case 4:
            mu_swprintf(TextList[TextNum], L"%ls %ls", I18N::Game::Command, p->Name);
            break;
        }
    }
    else if (ip->Type == ITEM_LOCHS_FEATHER)
    {
        Color = TEXT_COLOR_YELLOW;
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], L"%ls", p->Name);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], L"%ls", I18N::Game::CrestOfMonarch);
            break;
        }
    }
    else if (ip->Type == ITEM_POTION + 21)
    {
        Color = TEXT_COLOR_YELLOW;
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], L"%ls", p->Name);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], I18N::Game::Stone);
            break;
        case 2:
            mu_swprintf(TextList[TextNum], I18N::Game::StoneOfFriendship);
            break;
        case 3:
            mu_swprintf(TextList[TextNum], I18N::Game::SignOfLord);
            break;
        }
    }
    else if (ip->Type == ITEM_WEAPON_OF_ARCHANGEL)
    {
        Color = TEXT_COLOR_YELLOW;
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], I18N::Game::AbsoluteStaffOfArchangel);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], I18N::Game::AbsoluteSwordOfArchangel);
            break;
        case 2:
            mu_swprintf(TextList[TextNum], I18N::Game::AbsoluteCrossbowOfArchangel);
            break;
        }
    }
    else if (ip->Type == ITEM_WIZARDS_RING)
    {
        Color = TEXT_COLOR_YELLOW;
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], p->Name);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], I18N::Game::RingOfWarrior);
            break;
        case 2:
            mu_swprintf(TextList[TextNum], I18N::Game::RingOfWarrior);
            break;
        case 3:
            mu_swprintf(TextList[TextNum], I18N::Game::RingOfGlory);
            break;
        }
    }
    else if (ip->Type == ITEM_HELPER + 107)
    {
        Color = TEXT_COLOR_YELLOW;
        mu_swprintf(TextList[TextNum], p->Name);
    }
    else if (ip->Type == ITEM_SIEGE_POTION)
    {
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], I18N::Game::PotionOfBless);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], I18N::Game::PotionOfSoul);
            break;
        }
    }
    else if (ip->Type == ITEM_HELPER + 7)
    {
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], I18N::Game::Archer);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], I18N::Game::Spearman);
            break;
        }
    }
    else if (ip->Type == ITEM_LIFE_STONE_ITEM)
    {
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], I18N::Game::ScrollOfGuardian);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], I18N::Game::PlaceLifeStone);
            break;
        }
    }
    else if (ip->Type == ITEM_ALE)
    {
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], L"%ls", p->Name);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], I18N::Game::OliveOfLove);
            break;
        }
    }
    else if (ip->Type == ITEM_ORB_OF_SUMMONING)
    {
        mu_swprintf(TextList[TextNum], L"%ls %ls", SkillAttribute[30 + Level].Name,
                    I18N::Game::Jewel);
    }
    else if (ip->Type == ITEM_TRANSFORMATION_RING)
    {
        for (int i = 0; i < MAX_MONSTER; i++)
        {
            if (ItemRulesDetail::SommonTable[Level] == MonsterScript[i].Type)
            {
                mu_swprintf(TextList[TextNum], L"%ls %ls", MonsterScript[i].Name,
                            I18N::Game::TransformationRing);
                break;
            }
        }
    }
    else if (ip->Type >= ITEM_WINGS_OF_SPIRITS && ip->Type <= ITEM_WINGS_OF_DARKNESS)
    {
        if (Level == 0)
            mu_swprintf(TextList[TextNum], L"%ls", p->Name);
        else
            mu_swprintf(TextList[TextNum], L"%ls +%d", p->Name, Level);
    }
    else if ((ip->Type >= ITEM_WING_OF_STORM && ip->Type <= ITEM_CAPE_OF_EMPEROR) ||
             (ip->Type >= ITEM_WINGS_OF_DESPAIR && ip->Type <= ITEM_WING_OF_DIMENSION) ||
             (ip->Type >= ITEM_CAPE_OF_FIGHTER && ip->Type <= ITEM_CAPE_OF_OVERRULE))
    {
        if (Level == 0)
            mu_swprintf(TextList[TextNum], L"%ls", p->Name);
        else
            mu_swprintf(TextList[TextNum], L"%ls +%d", p->Name, Level);
    }
    else if (ip->Type == ITEM_SPIRIT)
    {
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], L"%ls of %ls", p->Name, I18N::Game::DarkHorse);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], L"%ls of %ls", p->Name, I18N::Game::DarkRaven);
            break;
        }
    }
    else if (ip->Type == ITEM_CAPE_OF_LORD)
    {
        if (Level == 0)
            mu_swprintf(TextList[TextNum], L"%ls", p->Name);
        else
            mu_swprintf(TextList[TextNum], L"%ls +%d", p->Name, Level);
    }
    else if (ip->Type == ITEM_SYMBOL_OF_KUNDUN)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::KundunMarkDLevel, Level);
    }
    else if (ip->Type == ITEM_LOST_MAP)
    {
        Color = TEXT_COLOR_YELLOW;
        mu_swprintf(TextList[TextNum], L"%ls +%d", p->Name, Level);
    }
    else if (ip->Type == ITEM_RED_RIBBON_BOX)
    {
        mu_swprintf(TextList[TextNum], L"%ls", p->Name);
    }
    else if (ip->Type == ITEM_GREEN_RIBBON_BOX)
    {
        mu_swprintf(TextList[TextNum], L"%ls", p->Name);
    }
    else if (ip->Type == ITEM_BLUE_RIBBON_BOX)
    {
        mu_swprintf(TextList[TextNum], L"%ls", p->Name);
    }
    else if (ip->Type == ITEM_SCROLL_OF_FIRE_SCREAM)
    {
        mu_swprintf(TextList[TextNum], L"%ls", p->Name);
    }
    else if (ip->Type >= ITEM_PUMPKIN_OF_LUCK && ip->Type <= ITEM_JACK_OLANTERN_DRINK)
    {
        mu_swprintf(TextList[TextNum], L"%ls", p->Name);
    }
    else if (ip->Type == ITEM_PINK_CHOCOLATE_BOX)
    {
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], L"%ls", p->Name);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], L"%ls", I18N::Game::LilacCandyBox);
            break;
        }
    }
    else if (ip->Type == ITEM_RED_CHOCOLATE_BOX)
    {
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], L"%ls", p->Name);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], L"%ls", I18N::Game::OrangeCandyBox);
            break;
        }
    }
    else if (ip->Type == ITEM_BLUE_CHOCOLATE_BOX)
    {
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], L"%ls", p->Name);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], L"%ls", I18N::Game::NavyCandyBox);
            break;
        }
    }
    else if (ip->Type >= ITEM_SPLINTER_OF_ARMOR && ip->Type <= ITEM_HORN_OF_FENRIR)
    {
        if (ip->Type == ITEM_HORN_OF_FENRIR)
        {
            Color = TEXT_COLOR_BLUE;
            if ((ip->ExcellentFlags & 63) == 0x01)
                mu_swprintf(TextList[TextNum], L"%ls %ls", p->Name, I18N::Game::Destroy);
            else if ((ip->ExcellentFlags & 63) == 0x02)
                mu_swprintf(TextList[TextNum], L"%ls %ls", p->Name, I18N::Game::Protect);
            else if ((ip->ExcellentFlags & 63) == 0x04)
                mu_swprintf(TextList[TextNum], L"%ls %ls", p->Name, I18N::Game::Illusion);
            else
                mu_swprintf(TextList[TextNum], L"%ls", p->Name);
        }
        else
        {
            Color = TEXT_COLOR_WHITE;
            mu_swprintf(TextList[TextNum], L"%ls", p->Name);
        }
    }
    else if (ItemRulesDetail::IsDivineArchangelWeaponItem(ip->Type))
    {
        if (Level == 0)
            mu_swprintf(TextList[TextNum], L"%ls", p->Name);
        else
            mu_swprintf(TextList[TextNum], L"%ls +%d", p->Name, Level);
    }
    else if (nGemType != COMGEM::NOGEM && nGemType % 2 == 1)
    {
        int nGlobalIndex = GetJewelIndex(nGemType, COMGEM::eGEM_NAME);
        mu_swprintf(TextList[TextNum], L"%ls +%d", I18N::Game::Lookup(nGlobalIndex), Level + 1);
    }
    else if (ip->Type == ITEM_GEMSTONE || ip->Type == ITEM_JEWEL_OF_HARMONY ||
             ip->Type == ITEM_LOWER_REFINE_STONE || ip->Type == ITEM_HIGHER_REFINE_STONE ||
             ip->Type == ITEM_MOONSTONE_PENDANT)
    {
        mu_swprintf(TextList[TextNum], L"%ls", p->Name);
        Color = TEXT_COLOR_YELLOW;
    }
    else if ((ip->Type >= ITEM_SEED_FIRE && ip->Type <= ITEM_SEED_EARTH) ||
             (ip->Type >= ITEM_SPHERE_MONO && ip->Type <= ITEM_SPHERE_5) ||
             (ip->Type >= ITEM_SEED_SPHERE_FIRE_1 && ip->Type <= ITEM_SEED_SPHERE_EARTH_5))
    {
        mu_swprintf(TextList[TextNum], L"%ls", p->Name);
        Color = TEXT_COLOR_VIOLET;
    }
    else if (ip->Type == ITEM_POTION + 111)
    {
        Color = TEXT_COLOR_YELLOW;
        mu_swprintf(TextList[TextNum], L"%ls", p->Name);
    }
    else if (ITEM_SUSPICIOUS_SCRAP_OF_PAPER <= ip->Type && ip->Type <= ITEM_COMPLETE_SECROMICON)
    {
        Color = TEXT_COLOR_YELLOW;
        mu_swprintf(TextList[TextNum], L"%ls", p->Name);
    }
    else
    {
        wchar_t TextName[64];
        if (g_csItemOption.GetSetItemName(TextName, ip->Type, ip->AncientDiscriminator))
        {
            wcscat(TextName, p->Name);
        }
        else
        {
            wcscpy(TextName, p->Name);
        }

        if (ip->ExcellentFlags > 0)
        {
            if (Level == 0)
                mu_swprintf(TextList[TextNum], L"%ls %ls", I18N::Game::Excellent, TextName);
            else
                mu_swprintf(TextList[TextNum], L"%ls %ls +%d", I18N::Game::Excellent, TextName,
                            Level);
        }
        else
        {
            if (Level == 0)
                mu_swprintf(TextList[TextNum], L"%ls", TextName);
            else
                mu_swprintf(TextList[TextNum], L"%ls +%d", TextName, Level);
        }
    }

    TextListColor[TextNum] = Color;
    TextBold[TextNum] = true;
    TextNum++;
    mu_swprintf(TextList[TextNum], L"\n");
    TextNum++;
    SkipNum++;

    if (ip->Type == ITEM_WEAPON_OF_ARCHANGEL)
    {
        int iMana;
        int iWeaponSpeed;
        int iNeedStrength;
        int iNeedDex;

        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        mu_swprintf(TextList[TextNum], I18N::Game::QuestItem);
        TextBold[TextNum] = false;
        TextNum++;

        TextListColor[TextNum] = TEXT_COLOR_DARKRED;
        mu_swprintf(TextList[TextNum], I18N::Game::RewardReceivedWhenReturnedToTheArchangel);
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], L"\n");
        TextBold[TextNum] = false;
        TextNum++;
        SkipNum++;

        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], L"%ls: %d ~ %d", I18N::Game::WizardryDamage, 107, 110);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
            iWeaponSpeed = 20;
            iNeedStrength = 132;
            iNeedDex = 32;
            break;
        case 1:
            mu_swprintf(TextList[TextNum], L"%ls: %d ~ %d", I18N::Game::OneHandedDamage, 110, 120);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
            iWeaponSpeed = 35;
            iNeedStrength = 381;
            iNeedDex = 149;
            break;
        case 2:
            mu_swprintf(TextList[TextNum], L"%ls: %d ~ %d", I18N::Game::TwoHandedDamage, 120, 140);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
            iWeaponSpeed = 35;
            iNeedStrength = 140;
            iNeedDex = 350;
            break;
        }

        mu_swprintf(TextList[TextNum], I18N::Game::AttackSpeedD, iWeaponSpeed);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::StrengthRequirementD, iNeedStrength);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::AgilityRequirementD, iNeedDex);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], L"\n");
        TextBold[TextNum] = false;
        TextNum++;
        SkipNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::LuckSuccessRateOfJewelOfSoul25);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::LuckCriticalDamageRate5, 20);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        switch (Level)
        {
        case 0: {
            mu_swprintf(TextList[TextNum], I18N::Game::WizardryDmgDRise, 53);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = true;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::IncreaseWizardryDmgLevel20);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::IncreaseWizardryDmgD, 2);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        case 1: {
            gSkillManager.GetSkillInformation(AT_SKILL_CYCLONE, 1, NULL, &iMana, NULL);
            mu_swprintf(TextList[TextNum], I18N::Game::CycloneCuttingSkillManaD, iMana);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::IncreaseDamageLevel20);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::IncreaseDamageD, 2);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        case 2: {
            gSkillManager.GetSkillInformation(AT_SKILL_TRIPLE_SHOT, 1, NULL, &iMana, NULL);
            mu_swprintf(TextList[TextNum], I18N::Game::TripleShotSkillManaD, iMana);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::IncreaseDamageLevel20);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::IncreaseDamageD, 2);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        }
        mu_swprintf(TextList[TextNum], I18N::Game::ExcellentDamageRate10);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::IncreaseAttackingWizardrySpeedD, 7);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum],
                    I18N::Game::IncreasesAcquisitionRateOfLifeAfterHuntingMonstersLife8);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum],
                    I18N::Game::IncreasesAcquisitionRateOfManaAfterHuntingMonstersMana8);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }

    if (ip->Type >= ITEM_SCROLL_OF_EMPEROR_RING_OF_HONOR && ip->Type <= ITEM_SOUL_SHARD_OF_WIZARD)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::QuestItem);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::CannotStoreInVault);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::CannotBeTraded);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 12)
    {
        if (Level <= 1)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::YouCanRegisterByGivingItToTheNPC);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
    }
    else if (ip->Type >= ITEM_HELPER + 46 && ip->Type <= ITEM_HELPER + 48)
    {
        int iMap = 0;
        if (ip->Type == ITEM_HELPER + 46)
            iMap = 39;
        else if (ip->Type == ITEM_HELPER + 47)
            iMap = 56;
        else if (ip->Type == ITEM_HELPER + 48)
            iMap = 58;

        mu_swprintf(TextList[TextNum], I18N::Game::EnablesEntranceIntoS, I18N::Game::Lookup(iMap));
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::YouWillBeAssignedToAStageAccordingToYourLevel);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], L"\n");
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }

    else if (ip->Type >= ITEM_HELPER + 125 && ip->Type <= ITEM_HELPER + 127)
    {
        int iMap = 0;
        if (ip->Type == ITEM_HELPER + 125)
            iMap = 3057;
        else if (ip->Type == ITEM_HELPER + 126)
            iMap = 2806;
        else if (ip->Type == ITEM_HELPER + 127)
            iMap = 3107;

        mu_swprintf(TextList[TextNum], I18N::Game::EnablesEntranceIntoS, I18N::Game::Lookup(iMap));
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], L"\n");
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 54)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::YouCanAchieveSpecialItemsWithCombinations);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type >= ITEM_POTION + 58 && ip->Type <= ITEM_POTION + 62)
    {
        mu_swprintf(TextList[TextNum],
                    I18N::Game::CongratulationsPleaseContactCSTeamAndChangeItToItem);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 83)
    {
        mu_swprintf(TextList[TextNum],
                    I18N::Game::CongratulationsPleaseContactCSTeamAndChangeItToItem);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type >= ITEM_POTION + 145 && ip->Type <= ITEM_POTION + 150)
    {
        mu_swprintf(TextList[TextNum],
                    I18N::Game::CongratulationsPleaseContactCSTeamAndChangeItToItem);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 53)
    {
        mu_swprintf(TextList[TextNum],
                    I18N::Game::IncreasesTheCombinationRateButOnlyUpToTheMaximumRate);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_HELPER + 43)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::IncreasesExperienceGained);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::WarpCommandWindowAvailable);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::NotApplicableTo);
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::MasterLevelCharacters);
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_HELPER + 44)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::IncreasesExperienceGainedAndItemDropRate);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::WarpCommandWindowAvailable);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::NotApplicableTo);
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::MasterLevelCharacters);
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_HELPER + 45)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::PreventsExperiencesToBeGained);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::WarpCommandWindowAvailable);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum],
                    I18N::Game::MasterLevelEXPCannotBeAchievedDuringTheItemUsage);
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type >= ITEM_POTION + 70 && ip->Type <= ITEM_POTION + 71)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::NumberOfItemsD, ip->Durability);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        int index = ip->Type - (ITEM_POTION + 70);

        mu_swprintf(TextList[TextNum], I18N::Game::Lookup(2500 + index));
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type >= ITEM_POTION + 72 && ip->Type <= ITEM_POTION + 77)
    {
        const ITEM_ADD_OPTION &Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ip->Type);

        mu_swprintf(TextList[TextNum], I18N::Game::Lookup(2503 + (ip->Type - (ITEM_POTION + 72))),
                    Item_data.m_byValue1);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        mu_swprintf(TextList[TextNum], I18N::Game::YouMayContinueToUseTheStrengthenerPower);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_HELPER + 59)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::YouMayFreelyMoveOnward);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type >= ITEM_HELPER + 54 && ip->Type <= ITEM_HELPER + 58)
    {
        DWORD statpoint = 0;
        statpoint = ip->Durability * 10;

        mu_swprintf(TextList[TextNum], I18N::Game::ResetPointD, statpoint);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        mu_swprintf(TextList[TextNum], I18N::Game::ResetsTheStatus);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;

        TextListColor[TextNum] = TEXT_COLOR_DARKRED;
        mu_swprintf(TextList[TextNum], I18N::Game::ItCanBeUsedWithItemRemoved);
        TextNum++;

        if (ip->Type == ITEM_HELPER + 58)
        {
            if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK_LORD)
            {
                TextListColor[TextNum] = TEXT_COLOR_WHITE;
            }
            else
            {
                TextListColor[TextNum] = TEXT_COLOR_DARKRED;
            }

            mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS, I18N::Game::DarkLord);
            TextNum++;
        }
    }
    else if (ip->Type >= ITEM_POTION + 78 && ip->Type <= ITEM_POTION + 82)
    {
        int index = ip->Type - (ITEM_POTION + 78);
        DWORD value = 0;

        mu_swprintf(TextList[TextNum], I18N::Game::NumberOfItemsD, ip->Durability);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        const ITEM_ADD_OPTION &Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ip->Type);
        value = Item_data.m_byValue1;

        mu_swprintf(TextList[TextNum], I18N::Game::Lookup(2512 + index), value);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::StatusForTheSetPeriod);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::ThereSAnIncreaseEffectToIt);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        if (ip->Type == ITEM_POTION + 82)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::DarkLordUseOnly);
            TextListColor[TextNum] = TEXT_COLOR_YELLOW;
            TextBold[TextNum] = false;
            TextNum++;
        }

        std::wstring timetext;
        g_StringTime(Item_data.m_Time, timetext, true);
        mu_swprintf(TextList[TextNum], timetext.c_str());
        TextListColor[TextNum] = TEXT_COLOR_PURPLE;
        TextBold[TextNum] = false;
        TextNum++;

        mu_swprintf(TextList[TextNum], I18N::Game::Available);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_HELPER + 60)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::ItReducesTheKillingRate);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_HELPER + 62)
    {
        const ITEM_ADD_OPTION &Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ip->Type);

        mu_swprintf(TextList[TextNum], I18N::Game::ExperienceRateIsIncreasedD,
                    Item_data.m_byValue1);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::AutomaticLifeRecoverIncrementD,
                    Item_data.m_byValue2);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum],
                    I18N::Game::EXPAchievementAndTheAutomaticLifeRecoveryRateIncreases);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum],
                    I18N::Game::MasterLevelEXPCannotBeAchievedDuringTheItemUsage);
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_HELPER + 63)
    {
        const ITEM_ADD_OPTION &Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ip->Type);

        mu_swprintf(TextList[TextNum], I18N::Game::ItemDropRateIsIncreasedD, Item_data.m_byValue1);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::AutomaticManaRecoveryIncrementInDRate,
                    Item_data.m_byValue2);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum],
                    I18N::Game::ItemAchievementAndTheAutomaticManaRecoveryIncreasesOnward);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 97)
    {
        const ITEM_ADD_OPTION &Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ip->Type);

        mu_swprintf(TextList[TextNum], I18N::Game::IncreasesCriticalDamageBy20);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        mu_swprintf(TextList[TextNum], I18N::Game::YouMayContinueToUseTheStrengthenerPower);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 98)
    {
        const ITEM_ADD_OPTION &Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ip->Type);

        mu_swprintf(TextList[TextNum], I18N::Game::IncresesExcellentDamageBy20);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::YouMayContinueToUseTheStrengthenerPower);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 140)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AutomaticHPRecoveryD, 3);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::MaxHPIncreaseD, 100);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 96)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::Minimum1015LevelItemUpgrade);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::BlocksTheItemDissipation);
        TextListColor[TextNum] = TEXT_COLOR_PURPLE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum],
                    I18N::Game::_1015WhenUpgradingLevelItemPleasePutItInCombinationWindow);
        TextListColor[TextNum] = TEXT_COLOR_PURPLE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_DEMON)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::IncreasesAttackPowerAndWizardryBy40);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::IncreasesAttackSpeedBy10);
        TextListColor[TextNum] = TEXT_COLOR_PURPLE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_SPIRIT_OF_GUARDIAN)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AlleviatesMonsterSDamageBy30);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::IncreasesMaximumLifeBy50);
        TextListColor[TextNum] = TEXT_COLOR_PURPLE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_PET_RUDOLF)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::SurroundingZensAreAutomaticallyCollected);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_PET_SKELETON)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::SurroundingZensAreAutomaticallyCollected);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        mu_swprintf(TextList[TextNum], I18N::Game::IncreasesDamageWizardryAndCurseBy20);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        mu_swprintf(TextList[TextNum], I18N::Game::IncreasesAttackSpeedBy10);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        mu_swprintf(TextList[TextNum], I18N::Game::AndEXPBy30);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_PET_PANDA)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AutoCollectsZenAroundYou);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::EXPRate50Increase);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::IncreaseDefensiveSkill50);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_PET_UNICORN)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AutoCollectsZenAroundYou);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::ZenIncrease50);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::IncreaseDefensiveSkill50);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_HELPER + 107)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::CannotRepair);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_HELPER + 104)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::IncreaseMaxAGLevel);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_HELPER + 105)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::IncreaseMaxSDLevelx10);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_HELPER + 103)
    {
        mu_swprintf(TextList[TextNum],
                    I18N::Game::UpToDEXPGainIncreaseDependingOnTheNumberOfMembersInYourParty, 170);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_HELPER + 69)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::RememberTheLocationOfOneSDeath);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        if (g_PortalMgr.IsRevivePositionSaved())
        {
            mu_swprintf(TextList[TextNum], I18N::Game::MoveByARightMouseClick);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
            g_PortalMgr.GetRevivePositionText(TextList[TextNum]);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
        }
    }
    else if (ip->Type == ITEM_HELPER + 70)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::SaveTheApplicationLocation);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_HELPER + 81)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::ExpAndItemWillBeSecuredWhenCharacterDies);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum++] = false;
        mu_swprintf(TextList[TextNum], I18N::Game::NoPenaltyForDying);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum++] = false;
        mu_swprintf(TextList[TextNum], I18N::Game::RightClickToUse);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum++] = false;
    }
    else if (ip->Type == ITEM_HELPER + 82)
    {
        mu_swprintf(TextList[TextNum],
                    I18N::Game::ItemDurabilityWillNotBeDecreaseForACertainPeriod);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum++] = false;
        mu_swprintf(TextList[TextNum], I18N::Game::KeepsItemDurable);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum++] = false;
        mu_swprintf(TextList[TextNum], I18N::Game::ApplicableToMountableItemsOnlyBesidesPet);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum++] = false;
    }
    else if (ip->Type == ITEM_HELPER + 93)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::IncreasesExperienceGained);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum++] = false;
        mu_swprintf(TextList[TextNum], I18N::Game::WarpCommandWindowAvailable);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum++] = false;
    }
    else if (ip->Type == ITEM_HELPER + 94)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::IncreasesExperienceGainedAndItemDropRate);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum++] = false;
        mu_swprintf(TextList[TextNum], I18N::Game::WarpCommandWindowAvailable);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum++] = false;
    }
    else if (ip->Type == ITEM_HELPER + 61)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::EnablesEntranceIntoS,
                    I18N::Game::IllusionTemple);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::YouWillBeAssignedToAStageAccordingToYourLevel);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], L"\n");
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 91)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::Lookup(2551));
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 92)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::YouCanAchieveSpecialItemsWithCombinations);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::Lookup(2553));
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 93)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::YouCanAchieveSpecialItemsWithCombinations);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::Lookup(2556));
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 95)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::YouCanAchieveSpecialItemsWithCombinations);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::Lookup(2552));
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 94)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::NumberOfItemsD, ip->Durability);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        mu_swprintf(TextList[TextNum], I18N::Game::RestoresHPBy65Immediately);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_CHERRY_BLOSSOM_PLAYBOX)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::DropItToReceiveTheGift);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_CHERRY_BLOSSOM_WINE)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::_700MaximumManaIncrement);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        std::wstring timetext;
        const ITEM_ADD_OPTION &Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ip->Type);
        g_StringTime(Item_data.m_Time, timetext, true);
        mu_swprintf(TextList[TextNum], timetext.c_str());
        TextListColor[TextNum] = TEXT_COLOR_PURPLE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_CHERRY_BLOSSOM_RICE_CAKE)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::_700MaximumLifeIncrement);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        std::wstring timetext;
        const ITEM_ADD_OPTION &Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ip->Type);
        g_StringTime(Item_data.m_Time, timetext, true);
        mu_swprintf(TextList[TextNum], timetext.c_str());
        TextListColor[TextNum] = TEXT_COLOR_PURPLE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_CHERRY_BLOSSOM_FLOWER_PETAL)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AttackPowerIncrement40);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        std::wstring timetext;
        const ITEM_ADD_OPTION &Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ip->Type);
        g_StringTime(Item_data.m_Time, timetext, true);
        mu_swprintf(TextList[TextNum], timetext.c_str());
        TextListColor[TextNum] = TEXT_COLOR_PURPLE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 88)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::NumberOfItemsD, ip->Durability);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum],
                    I18N::Game::CollectCherryBlossomsAndTakeItToTheSpiritForItemCompensation);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::Lookup(2535));
        TextListColor[TextNum] = TEXT_COLOR_DARKYELLOW;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 89)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::NumberOfItemsD, ip->Durability);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum],
                    I18N::Game::CollectCherryBlossomsAndTakeItToTheSpiritForItemCompensation);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::Lookup(2536));
        TextListColor[TextNum] = TEXT_COLOR_DARKYELLOW;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_GOLDEN_CHERRY_BLOSSOM_BRANCH)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::NumberOfItemsD, ip->Durability);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum],
                    I18N::Game::CollectCherryBlossomsAndTakeItToTheSpiritForItemCompensation);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::Lookup(2537));
        TextListColor[TextNum] = TEXT_COLOR_DARKYELLOW;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_OLD_SCROLL)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AssembleTheScrollOfBloodWith);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_ILLUSION_SORCERER_COVENANT)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AssembleTheScrollOfBloodWithTheOldScrolls);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_SCROLL_OF_BLOOD)
    {
        mu_swprintf(TextList[TextNum],
                    I18N::Game::ThisIsAMarkOfIllusionSorceryThisIsRequiredToEnterTheTemple);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 64)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::MobilitySpeedReducesUponAchievement);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type >= ITEM_FLAME_OF_DEATH_BEAM_KNIGHT && ip->Type <= ITEM_EYE_OF_ABYSSAL)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::QuestItem);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::CannotStoreInVault);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::CannotBeTraded);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_FLAME_OF_CONDOR)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::IngredientsForThe3rdWingAssembly);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_FEATHER_OF_CONDOR)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::IngredientsForThe3rdWingAssembly);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if ((ip->Type >= ITEM_SEED_FIRE && ip->Type <= ITEM_SEED_EARTH) ||
             (ip->Type >= ITEM_SPHERE_MONO && ip->Type <= ITEM_SPHERE_5) ||
             (ip->Type >= ITEM_SEED_SPHERE_FIRE_1 && ip->Type <= ITEM_SEED_SPHERE_EARTH_5))
    {
        TextNum = g_SocketItemMgr.AttachToolTipForSeedSphereItem(ip, TextNum);
    }
    else if (ip->Type == ITEM_HELPER + 71 || ip->Type == ITEM_HELPER + 72 ||
             ip->Type == ITEM_HELPER + 73 || ip->Type == ITEM_HELPER + 74 ||
             ip->Type == ITEM_HELPER + 75)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::ItemSkillLuckOptionWillBeRandomlyAdded);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_MOONSTONE_PENDANT)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::CannotRepair);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_WIZARDS_RING)
    {
        switch (Level)
        {
        case 0: {
            mu_swprintf(TextList[TextNum], I18N::Game::CannotRepair);
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
        }
        break;

        case 1: {
            mu_swprintf(TextList[TextNum], I18N::Game::CanBeDroppedAfterLevelD, 40);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::CannotStoreInVault);
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::CannotBeTraded);
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::CannotBeSold);
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        case 2: {
            mu_swprintf(TextList[TextNum], I18N::Game::CanBeDroppedAfterLevelD, 80);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::CannotStoreInVault);
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::CannotBeTraded);
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::CannotBeSold);
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        case 3: {
            mu_swprintf(TextList[TextNum], I18N::Game::CannotRepair);
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        }
    }
    else if (ip->Type >= ITEM_TYPE_CHARM_MIXWING + EWS_BEGIN &&
             ip->Type <= ITEM_TYPE_CHARM_MIXWING + EWS_END)
    {
        const ITEM_ADD_OPTION &Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ip->Type);
        mu_swprintf(TextList[TextNum], I18N::Game::IncreasesYourLuckToCreateWingOfYourWish);
        TextBold[TextNum] = false;
        TextNum++;

        switch (ip->Type)
        {
        case ITEM_TYPE_CHARM_MIXWING + EWS_KNIGHT_1_CHARM: {
            mu_swprintf(TextList[TextNum], I18N::Game::IncreasesYourLuckToCreateWingsOfSatan,
                        Item_data.m_byValue1);
        }
        break;
        case ITEM_TYPE_CHARM_MIXWING + EWS_MAGICIAN_1_CHARM: {
            mu_swprintf(TextList[TextNum], I18N::Game::IncreasesYourLuckToCreateWingsOfHeaven,
                        Item_data.m_byValue1);
        }
        break;
        case ITEM_TYPE_CHARM_MIXWING + EWS_ELF_1_CHARM: {
            mu_swprintf(TextList[TextNum], I18N::Game::IncreasesYourLuckToCreateWingsOfElf,
                        Item_data.m_byValue1);
        }
        break;
        case ITEM_TYPE_CHARM_MIXWING + EWS_SUMMONER_1_CHARM: {
            mu_swprintf(TextList[TextNum], I18N::Game::IncreasesYourLuckToCreateWingOfCurse,
                        Item_data.m_byValue1);
        }
        break;
        case ITEM_TYPE_CHARM_MIXWING + EWS_DARKLORD_1_CHARM: {
            mu_swprintf(TextList[TextNum], I18N::Game::IncreasesYourLuckToCreateCapeOfEmperor,
                        Item_data.m_byValue1);
        }
        break;
        case ITEM_TYPE_CHARM_MIXWING + EWS_KNIGHT_2_CHARM: {
            mu_swprintf(TextList[TextNum], I18N::Game::IncreasesYourLuckToCreateWingsOfDragon,
                        Item_data.m_byValue1);
        }
        break;
        case ITEM_TYPE_CHARM_MIXWING + EWS_MAGICIAN_2_CHARM: {
            mu_swprintf(TextList[TextNum], I18N::Game::IncreasesYourLuckToCreateWingsOfSoul,
                        Item_data.m_byValue1);
        }
        break;
        case ITEM_TYPE_CHARM_MIXWING + EWS_ELF_2_CHARM: {
            mu_swprintf(TextList[TextNum], I18N::Game::IncreasesYourLuckToCreateWingsOfSpirits,
                        Item_data.m_byValue1);
        }
        break;
        case ITEM_TYPE_CHARM_MIXWING + EWS_SUMMONER_2_CHARM: {
            mu_swprintf(TextList[TextNum], I18N::Game::IncreasesYourLuckToCreateWingOfDespair,
                        Item_data.m_byValue1);
        }
        break;
        case ITEM_TYPE_CHARM_MIXWING + EWS_DARKKNIGHT_2_CHARM: {
            mu_swprintf(TextList[TextNum], I18N::Game::IncreasesYourLuckToCreateWingsOfDarkness,
                        Item_data.m_byValue1);
        }
        break;
        }

        mu_swprintf(TextList[TextNum],
                    I18N::Game::Lookup(2732 + (ip->Type - (ITEM_TYPE_CHARM_MIXWING + EWS_BEGIN))),
                    Item_data.m_byValue1);

        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 110)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::ItSASignInfusedWithTracesOfDimensions);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::CollectFiveAndTheSignsWillAutomatically);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::TransformIntoAMirrorOfDimensions);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::DD, ip->Durability, 5);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::YouNeedDMoreToCreateAMirrorOfDimensions,
                    5 - ip->Durability);
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 111)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::ThatSTheOnlyThingThatWillGetLugardToHelpYou);
        TextListColor[TextNum] = TEXT_COLOR_DARKBLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::EnterTheDoppelgangerArea);
        TextListColor[TextNum] = TEXT_COLOR_DARKBLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ITEM_SUSPICIOUS_SCRAP_OF_PAPER <= ip->Type && ip->Type <= ITEM_COMPLETE_SECROMICON)
    {
        switch (ip->Type)
        {
        case ITEM_SUSPICIOUS_SCRAP_OF_PAPER: {
            mu_swprintf(TextList[TextNum], I18N::Game::DD, ip->Durability, 5);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;

            mu_swprintf(TextList[TextNum],
                        I18N::Game::ItSAWornPieceOfPaperContainingIncomprehensibleText);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        case ITEM_GAIONS_ORDER: {
            mu_swprintf(TextList[TextNum],
                        I18N::Game::ItContainsGaionSPlansForTheDestructionOfTheEmpire);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::AndOrdersForTheEmpireGuardians);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::YouMayEnterTheFortressOfEmpireGuardians);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        case ITEM_FIRST_SECROMICON_FRAGMENT:
        case ITEM_SECOND_SECROMICON_FRAGMENT:
        case ITEM_THIRD_SECROMICON_FRAGMENT:
        case ITEM_FOURTH_SECROMICON_FRAGMENT:
        case ITEM_FIFTH_SECROMICON_FRAGMENT:
        case ITEM_SIXTH_SECROMICON_FRAGMENT: {
            mu_swprintf(TextList[TextNum], I18N::Game::ItSPartOfACompleteSecromicon);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        case ITEM_COMPLETE_SECROMICON: {
            mu_swprintf(TextList[TextNum], I18N::Game::IndestructibleMetalSecromicon);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum],
                        I18N::Game::ContainsInformationAboutGrandWizardEtramuLenosResearch);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        }
    }
    else if (ITEM_HELPER + 109 == ip->Type)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AutomaticHPRecoveryD, 3);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::IncreasesMaxMana4, 4);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ITEM_HELPER + 110 == ip->Type)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AutomaticHPRecoveryD, 3);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::IncreaseMaxHP4, 4);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ITEM_HELPER + 111 == ip->Type)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AutomaticHPRecoveryD, 3);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum],
                    I18N::Game::IncreasesAcquisitionRateOfZenAfterHuntingMonsters30, 50);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ITEM_HELPER + 112 == ip->Type)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AutomaticHPRecoveryD, 3);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::DamageDecrease4, 4);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ITEM_HELPER + 113 == ip->Type)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AutomaticHPRecoveryD, 3);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::ExcellentDamageRate10, 10);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ITEM_HELPER + 114 == ip->Type)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AutomaticHPRecoveryD, 3);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::AttackSpeedIncreaseD, 7);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ITEM_HELPER + 115 == ip->Type)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AutomaticHPRecoveryD, 3);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum],
                    I18N::Game::IncreasesAcquisitionRateOfManaAfterHuntingMonstersMana8);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ITEM_POTION + 112 == ip->Type)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::YouCanDoAGoblinCombination2876);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ITEM_POTION + 113 == ip->Type)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::YouCanDoAGoblinCombination);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 120)
    {
        mu_swprintf(TextList[TextNum],
                    I18N::Game::YouCanAcquireGoblinPointsByUsingTheMUItemShopSStorage);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ITEM_POTION + 121 == ip->Type)
    {
        mu_swprintf(TextList[TextNum],
                    I18N::Game::YouCanDoAGoblinCombinationWithAGoldKeyToCreateAGoldenBox);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ITEM_POTION + 122 == ip->Type)
    {
        mu_swprintf(TextList[TextNum],
                    I18N::Game::YouCanDoAGoblinCombinationWithASilverKeyToCreateASilverBox);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ITEM_POTION + 123 == ip->Type)
    {
        mu_swprintf(TextList[TextNum],
                    I18N::Game::YouCanDropItWithAFixedProbabilityOfItTurningIntoARareItem);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ITEM_POTION + 124 == ip->Type)
    {
        mu_swprintf(TextList[TextNum],
                    I18N::Game::YouCanDropItWithAFixedProbabilityOfItTurningIntoARareItem);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ITEM_POTION + 134 <= ip->Type && ITEM_POTION + 139 >= ip->Type)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::ItSABoxContainingVariousItems);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ITEM_HELPER + 116 == ip->Type)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::BoostsTheItemDropRate);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;

        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ITEM_HELPER + 121 == ip->Type)
    {
        int iMap = 57;
        mu_swprintf(TextList[TextNum], I18N::Game::EnablesEntranceIntoS, I18N::Game::Lookup(iMap));
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::YouWillBeAssignedToAStageAccordingToYourLevel);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::UsableDtimes, ip->Durability);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_HELPER + 124)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::YouCanEnterToGoldChannel);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::_7DaysUntilExpiration);
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextBold[TextNum] = true;
        TextNum++;
    }
    else if (ip->Type >= ITEM_POTION + 141 && ip->Type <= ITEM_POTION + 144)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::ThrowItAndYouMayReceiveSomeZenOrItems);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 133)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::NumberOfItemsD, ip->Durability);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        mu_swprintf(TextList[TextNum], I18N::Game::RestoresSDBy65Immediately);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }

    Color = TEXT_COLOR_YELLOW;
    mu_swprintf(TextList[TextNum], L"%ls", p->Name);

    if (ip->Type == ITEM_DEVILS_INVITATION)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::TheRemainingTimeIsShown);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::WhenYouRightClickOnYourMouse);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }

    if (ip->DamageMin)
    {
        int minindex = 0, maxindex = 0, magicalindex = 0;

        if (Level >= ip->Jewel_Of_Harmony_OptionLevel)
        {
            StrengthenCapability SC;
            g_pUIJewelHarmonyinfo->GetStrengthenCapability(&SC, ip, 1);

            if (SC.SI_isSP)
            {
                minindex = SC.SI_SP.SI_minattackpower;
                maxindex = SC.SI_SP.SI_maxattackpower;
                magicalindex = SC.SI_SP.SI_magicalpower;
            }
        }
        int DamageMin = ip->DamageMin;
        int DamageMax = ip->DamageMax;
        if (ip->Type >> 4 == 15)
        {
            mu_swprintf(TextList[TextNum], L"%ls: %d ~ %d", I18N::Game::Lookup(40 + 2), DamageMin,
                        DamageMax);
        }
        else if (ip->Type != ITEM_SCROLL_OF_TELEPORT && ip->Type != ITEM_SCROLL_OF_TELEPORT_ALLY &&
                 ip->Type != ITEM_SCROLL_OF_SOUL_BARRIER)
        {
            if (ip->Type >= ITEM_ETC && ip->Type < ITEM_ETC + MAX_ITEM_INDEX)
            {
                const ActionSkillType skillIndex = GetSkillByBook(ip->Type);
                if (SkillAttribute != nullptr && skillIndex != AT_SKILL_UNDEFINED &&
                    IsValidateSkillIdx(static_cast<INT>(skillIndex)))
                {
                    const SKILL_ATTRIBUTE &skillAtt = SkillAttribute[skillIndex];
                    DamageMin = skillAtt.Damage;
                    DamageMax = skillAtt.Damage + skillAtt.Damage / 2;
                }

                mu_swprintf(TextList[TextNum], L"%ls: %d ~ %d", I18N::Game::WizardryDamage,
                            DamageMin, DamageMax);
            }
            else
            {
                if (DamageMin + minindex >= DamageMax + maxindex)
                    mu_swprintf(TextList[TextNum], L"%ls: %d ~ %d",
                                I18N::Game::Lookup(40 + p->TwoHand), DamageMax + maxindex,
                                DamageMax + maxindex);
                else
                    mu_swprintf(TextList[TextNum], L"%ls: %d ~ %d",
                                I18N::Game::Lookup(40 + p->TwoHand), DamageMin + minindex,
                                DamageMax + maxindex);
            }
        }
        else
        {
            TextNum--;
        }

        if (DamageMin > 0)
        {
            if (minindex != 0 || maxindex != 0)
            {
                TextListColor[TextNum] = TEXT_COLOR_YELLOW;
                TextBold[TextNum] = false;
                TextNum++;
            }
            else
            {
                if (ip->ExcellentFlags > 0)
                    TextListColor[TextNum] = TEXT_COLOR_BLUE;
                else
                    TextListColor[TextNum] = TEXT_COLOR_WHITE;
                TextBold[TextNum] = false;
                TextNum++;
            }
        }
        else
        {
            TextNum--;
        }
    }
    if (ip->Defense)
    {
        int maxdefense = 0;

        if (Level >= ip->Jewel_Of_Harmony_OptionLevel)
        {
            StrengthenCapability SC;
            g_pUIJewelHarmonyinfo->GetStrengthenCapability(&SC, ip, 2);

            if (SC.SI_isSD)
            {
                maxdefense = SC.SI_SD.SI_defense;
            }
        }
        mu_swprintf(TextList[TextNum], I18N::Game::DefenseD, ip->Defense + maxdefense);

        if (maxdefense != 0)
            TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        else
        {
            if (ip->Type >= ITEM_HELM && ip->Type < ITEM_BOOTS + MAX_ITEM_INDEX &&
                ip->ExcellentFlags > 0)
                TextListColor[TextNum] = TEXT_COLOR_BLUE;
            else
                TextListColor[TextNum] = TEXT_COLOR_WHITE;
        }

        TextBold[TextNum] = false;
        TextNum++;
    }
    if (ip->MagicDefense)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::SpellResistanceD, ip->MagicDefense);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    if (p->SuccessfulBlocking)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::DefenseRateD, ip->SuccessfulBlocking);
        if (ip->ExcellentFlags > 0)
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
        else
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    if (p->WeaponSpeed)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AttackSpeedD, p->WeaponSpeed);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    if (p->WalkSpeed)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::MovingSpeedD, p->WalkSpeed);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    if (ip->Type >= ITEM_RED_RIBBON_BOX && ip->Type <= ITEM_BLUE_RIBBON_BOX)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::ThrowItAndYouMayReceiveSomeZenOrItems);
        switch (ip->Type)
        {
        case ITEM_RED_RIBBON_BOX:
            TextListColor[TextNum] = TEXT_COLOR_RED;
            break;
        case ITEM_GREEN_RIBBON_BOX:
            TextListColor[TextNum] = TEXT_COLOR_GREEN;
            break;
        case ITEM_BLUE_RIBBON_BOX:
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            break;
        }
        TextBold[TextNum] = false;
        TextNum++;
    }
    if (ip->Type >= ITEM_PUMPKIN_OF_LUCK && ip->Type <= ITEM_JACK_OLANTERN_DRINK) //Halloween event
    {
        wchar_t Text_data[300];
        ITEM_ADD_OPTION Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ip->Type);

        switch (ip->Type)
        {
        case ITEM_PUMPKIN_OF_LUCK:
            mu_swprintf(TextList[TextNum], I18N::Game::DropItToReceiveTheGift);
            TextListColor[TextNum] = TEXT_COLOR_DARKYELLOW;
            break;
        case ITEM_JACK_OLANTERN_BLESSINGS:
            mu_swprintf(Text_data, I18N::Game::AttackSpeedIncreaseD, Item_data.m_byValue1);
            mu_swprintf(TextList[TextNum], Text_data);
            TextListColor[TextNum] = TEXT_COLOR_DARKYELLOW;
            break;
        case ITEM_JACK_OLANTERN_WRATH:
            mu_swprintf(Text_data, I18N::Game::AttackPowerIncreaseD, Item_data.m_byValue1);
            mu_swprintf(TextList[TextNum], Text_data);
            TextListColor[TextNum] = TEXT_COLOR_DARKYELLOW;
            break;
        case ITEM_JACK_OLANTERN_CRY:
            mu_swprintf(Text_data, I18N::Game::DefensePowerIncreaseD, Item_data.m_byValue1);
            mu_swprintf(TextList[TextNum], Text_data);
            TextListColor[TextNum] = TEXT_COLOR_DARKYELLOW;
            break;
        case ITEM_JACK_OLANTERN_FOOD:
            mu_swprintf(Text_data, I18N::Game::IncreaseMaxLifeD, Item_data.m_byValue1);
            mu_swprintf(TextList[TextNum], Text_data);
            TextListColor[TextNum] = TEXT_COLOR_DARKYELLOW;
            break;
        case ITEM_JACK_OLANTERN_DRINK:
            mu_swprintf(Text_data, I18N::Game::IncreaseMaxManaD, Item_data.m_byValue1);
            mu_swprintf(TextList[TextNum], Text_data);
            TextListColor[TextNum] = TEXT_COLOR_DARKYELLOW;
            break;
        }
        TextBold[TextNum] = false;
        TextNum++;
    }
    if (ip->Type >= ITEM_PINK_CHOCOLATE_BOX && ip->Type <= ITEM_BLUE_CHOCOLATE_BOX)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::DropItToReceiveTheGift);
        switch (ip->Type)
        {
        case ITEM_PINK_CHOCOLATE_BOX:
            if (Level == 0)
                TextListColor[TextNum] = TEXT_COLOR_PURPLE;
            else if (Level == 1)
                TextListColor[TextNum] = TEXT_COLOR_PURPLE;
            break;
        case ITEM_RED_CHOCOLATE_BOX:
            if (Level == 0)
                TextListColor[TextNum] = TEXT_COLOR_RED;
            else if (Level == 1)
                TextListColor[TextNum] = TEXT_COLOR_RED;
            break;
        case ITEM_BLUE_CHOCOLATE_BOX:
            if (Level == 0)
                TextListColor[TextNum] = TEXT_COLOR_BLUE;
            else if (Level == 1)
                TextListColor[TextNum] = TEXT_COLOR_BLUE;
            break;
        }
        TextBold[TextNum] = false;
        TextNum++;
    }
    if (ip->Type == ITEM_BOX_OF_LUCK)
    {
        if (Level == 7)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::WhenYouDropItOnTheGround);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::RenaZenJewelItem);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::YouWillGetOneOfTheAboveItems);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
        else if (Level == 14)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::FreeEntranceToKalima);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::IncreaseStamina);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
        else
        {
            mu_swprintf(TextList[TextNum], I18N::Game::ThrowItAndYouMayReceiveSomeZenOrItems);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
        mu_swprintf(TextList[TextNum], I18N::Game::CannotBeSold);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextBold[TextNum] = false;
        TextNum++;
        if (Level == 13)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::CannotStoreInVault);
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
        }
    }
    char tCount = CalcCompiledCount(ip);
    if (tCount > 0)
    {
        int nJewelIndex = Check_Jewel_Com(ip->Type);
        if (nJewelIndex != COMGEM::NOGEM)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::DSIsCombined, tCount,
                        I18N::Game::Lookup(GetJewelIndex(nJewelIndex, COMGEM::eGEM_NAME)));
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::CanBeUsedAfterDismantling);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
    }
    if (ip->Type == ITEM_JEWEL_OF_BLESS)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::ItIsUsedToIncreaseYourItemLevelUpTo6);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    if (ip->Type == ITEM_JEWEL_OF_SOUL)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::ItIsUsedToIncreaseYourItemLevelUpTo789);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    if (ip->Type == ITEM_JEWEL_OF_LIFE)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::IncreasesItemOptionBy1Level);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    if (ip->Type == ITEM_DEVILS_EYE || ip->Type == ITEM_DEVILS_KEY)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::ItIsUsedToCombineItemsForADevilSquareInvitation);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    if (ip->Type == ITEM_TOWN_PORTAL_SCROLL && Level >= 1 && Level <= 8)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::WarpToTheCorrespondingAreaAfterDSeconds, 3);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    if (ip->Type == ITEM_JEWEL_OF_CHAOS)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::ItIsUsedToCombineChaosItems);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    if (ip->Type == ITEM_JEWEL_OF_CREATION)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::UsedToCreateFruitsThatIncreaseStats);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    if (ip->Type == ITEM_JEWEL_OF_GUARDIAN)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::CreateAndImproveItemsForSiege);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    if (ip->Type == ITEM_GUARDIAN_ANGEL) //
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AbsorbDOfDamage, 20);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::MaxHPDIncreased, 50);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    if (ip->Type == ITEM_IMP)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::Increase30OfAttackingWizardryDmg);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    if ((ip->Type >= ITEM_WING && ip->Type <= ITEM_WINGS_OF_SATAN) ||
        ip->Type == ITEM_WING_OF_CURSE)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::IncreaseDOfDamage, 12 + Level * 2);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::AbsorbDOfDamage, 12 + Level * 2);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::IncreaseSpeed);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_MOONSTONE_PENDANT)
    {
        mu_swprintf(TextList[TextNum],
                    I18N::Game::IDOfKanturChiefScientistYouCanEnterTheRefineryTower);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_GEMSTONE)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::JewelWithImpurities);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_JEWEL_OF_HARMONY)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::JewelForItemReinforcement);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_LOWER_REFINE_STONE)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::GrantActualPowerToReinforcedItem);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_HIGHER_REFINE_STONE)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::GrantActualPowerToReinforcedItem);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 160)
    {
        // ??? ??
        mu_swprintf(TextList[TextNum], I18N::Game::JewelUsedForRepairingALuckyItem);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 161)
    {
        // ??? ??
        mu_swprintf(TextList[TextNum], I18N::Game::JewelForItemReinforcement);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if ((ip->Type >= ITEM_WINGS_OF_SPIRITS && ip->Type <= ITEM_WINGS_OF_DARKNESS) ||
             ip->Type == ITEM_WINGS_OF_DESPAIR) //??
    {
        mu_swprintf(TextList[TextNum], I18N::Game::IncreaseDOfDamage, 32 + Level); //  ??? ?%??.
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::AbsorbDOfDamage, 25 + Level * 2); //  ??? ?%??.
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::IncreaseSpeed); //  ?? ?? ??.
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if ((ip->Type >= ITEM_WING_OF_STORM && ip->Type <= ITEM_CAPE_OF_EMPEROR) ||
             ip->Type == ITEM_WING_OF_DIMENSION || ip->Type == ITEM_CAPE_OF_OVERRULE)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::IncreaseDOfDamage, 39 + Level * 2);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        if (ip->Type == ITEM_CAPE_OF_EMPEROR || ip->Type == ITEM_CAPE_OF_OVERRULE)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::AbsorbDOfDamage, 24 + Level * 2);
        }
        else
        {
            mu_swprintf(TextList[TextNum], I18N::Game::AbsorbDOfDamage, 39 + Level * 2);
        }
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::IncreaseSpeed);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ITEM_WING + 130 <= ip->Type && ip->Type <= ITEM_WING + 135)
    {
        switch (ip->Type)
        {
        case ITEM_WING + 130:
        case ITEM_WING + 135: {
            mu_swprintf(TextList[TextNum], I18N::Game::IncreaseDOfDamage, 20 + Level * 2);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::AbsorbDOfDamage, 20 + Level * 2);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        case ITEM_WING + 131:
        case ITEM_WING + 132:
        case ITEM_WING + 133:
        case ITEM_WING + 134: {
            mu_swprintf(TextList[TextNum], I18N::Game::IncreaseDOfDamage, 12 + Level * 2);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::AbsorbDOfDamage, 12 + Level * 2);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        }
        mu_swprintf(TextList[TextNum], I18N::Game::IncreaseSpeed);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_HORN_OF_DINORANT)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::IncreaseDOfDamage, 15);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::AbsorbDOfDamage, 10);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_SPIRIT)
    {
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], I18N::Game::UsedInDarkHorseResurrection);
            TextNum++;
            break;
        case 1:
            mu_swprintf(TextList[TextNum], I18N::Game::UsedInDarkRavenResurrection);
            TextNum++;
            break;
        }
    }
    else if (ip->Type == ITEM_LOCHS_FEATHER)
    {
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], L"%ls", I18N::Game::UsedToUpgradeWings);
            TextNum++;
            break;
        case 1:
            mu_swprintf(TextList[TextNum], L"%ls",
                        I18N::Game::UsedInCombiningCapeOfLordWarriorSCloak);
            TextNum++;
            break;
        }
    }

    else if (ip->Type == ITEM_FRUITS)
    {
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], L"%ls %ls", I18N::Game::ENG,
                        I18N::Game::Increases13StatPoints);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], L"%ls %ls", I18N::Game::STA,
                        I18N::Game::Increases13StatPoints);
            break;
        case 2:
            mu_swprintf(TextList[TextNum], L"%ls %ls", I18N::Game::AGI,
                        I18N::Game::Increases13StatPoints);
            break;
        case 3:
            mu_swprintf(TextList[TextNum], L"%ls %ls", I18N::Game::STR,
                        I18N::Game::Increases13StatPoints);
            break;
        case 4:
            mu_swprintf(TextList[TextNum], L"%ls %ls", I18N::Game::Command,
                        I18N::Game::Increases13StatPoints);
            break;
        }
        TextNum++;
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], L"%ls %ls", I18N::Game::ENG,
                        I18N::Game::PossibleToDecreaseStat19Point);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], L"%ls %ls", I18N::Game::STA,
                        I18N::Game::PossibleToDecreaseStat19Point);
            break;
        case 2:
            mu_swprintf(TextList[TextNum], L"%ls %ls", I18N::Game::AGI,
                        I18N::Game::PossibleToDecreaseStat19Point);
            break;
        case 3:
            mu_swprintf(TextList[TextNum], L"%ls %ls", I18N::Game::STR,
                        I18N::Game::PossibleToDecreaseStat19Point);
            break;
        case 4:
            mu_swprintf(TextList[TextNum], L"%ls %ls", I18N::Game::Command,
                        I18N::Game::PossibleToDecreaseStat19Point);
            break;
        }
        TextNum++;
        TextListColor[TextNum] = TEXT_COLOR_DARKRED;
        mu_swprintf(TextList[TextNum], I18N::Game::ItCanBeUsedWithItemRemoved);

        if (Level == 4)
        {
            TextNum++;
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            mu_swprintf(TextList[TextNum], I18N::Game::CanBeEquippedByS, I18N::Game::DarkLord);
        }
        TextNum++;
    }
    else if (ip->Type == ITEM_SCROLL_OF_ARCHANGEL)
    {
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        mu_swprintf(TextList[TextNum], I18N::Game::UsedWhenCreatingACloakOfInvisibility);
        TextNum++;
    }
    else if (ip->Type == ITEM_BLOOD_BONE)
    {
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        mu_swprintf(TextList[TextNum], I18N::Game::UsedWhenCreatingACloakOfInvisibility);
        TextNum++;
    }
    else if (ip->Type == ITEM_INVISIBILITY_CLOAK)
    {
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        mu_swprintf(TextList[TextNum], I18N::Game::UsedWhenEnteringBloodCastle);
        TextNum++;
        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::TheRemainingTimeIsShown);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::WhenYouRightClickOnYourMouse);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_SIEGE_POTION)
    {
        switch (Level)
        {
        case 0: {
            mu_swprintf(TextList[TextNum], I18N::Game::Damage20IncreaseEffect);
            TextListColor[TextNum] = TEXT_COLOR_DARKRED;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::Duration60Seconds);
            TextListColor[TextNum] = TEXT_COLOR_DARKRED;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::OnlyApplicableForCastleGateAndStatue);
            TextListColor[TextNum] = TEXT_COLOR_DARKRED;
            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        case 1: {
            mu_swprintf(TextList[TextNum], I18N::Game::Increase8AGRecoverySpeed);
            TextListColor[TextNum] = TEXT_COLOR_DARKRED;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::IncreaseResistanceOfLightningAndIce);
            TextListColor[TextNum] = TEXT_COLOR_DARKRED;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::AttackingSpeedWillIncrease20);
            TextListColor[TextNum] = TEXT_COLOR_DARKRED;
            TextBold[TextNum] = false;
            TextNum++;
        }
        break;
        }
    }
    else if (ip->Type == ITEM_HELPER + 7)
    {
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], I18N::Game::Archer);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], I18N::Game::Spearman);
            break;
        }
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_LIFE_STONE_ITEM)
    {
        switch (Level)
        {
        case 0:
            mu_swprintf(TextList[TextNum], I18N::Game::ScrollOfGuardian);
            break;
        case 1:
            mu_swprintf(TextList[TextNum], I18N::Game::PlaceLifeStone);
            break;
        }
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_ARMOR_OF_GUARDSMAN)
    {
        int startIndex = 0;
        if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK ||
            gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK_LORD ||
            gCharacterManager.GetBaseClass(Hero->Class) == CLASS_RAGEFIGHTER)
        {
            startIndex = 6;
        }

        int HeroLevel = CharacterAttribute->Level;

        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        mu_swprintf(TextList[TextNum], L"%ls %ls    %ls      %ls    ", I18N::Game::ChaosCastle,
                    I18N::Game::Level, I18N::Game::MinLevel, I18N::Game::Cost);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;

        for (int i = 0; i < 6; i++)
        {
            int Zen = ItemRulesDetail::g_iChaosCastleZen[i];

            mu_swprintf(TextList[TextNum], L"        %d             %3d~%3d     %3d,000", i + 1,
                        ItemRulesDetail::g_iChaosCastleLevel[startIndex + i][0],
                        std::min<int>(400, ItemRulesDetail::g_iChaosCastleLevel[startIndex + i][1]),
                        Zen);
            if ((HeroLevel >= ItemRulesDetail::g_iChaosCastleLevel[startIndex + i][0] &&
                 HeroLevel <= ItemRulesDetail::g_iChaosCastleLevel[startIndex + i][1]) &&
                gCharacterManager.IsMasterLevel(Hero->Class) == false)
            {
                TextListColor[TextNum] = TEXT_COLOR_DARKYELLOW;
            }
            else
            {
                TextListColor[TextNum] = TEXT_COLOR_WHITE;
            }
            TextBold[TextNum] = false;
            TextNum++;
        }
        mu_swprintf(TextList[TextNum], L"         %d          %ls   %3d,000", 7,
                    I18N::Game::MasterLevel, 1000);
        if (gCharacterManager.IsMasterLevel(Hero->Class) == true)
        {
            TextListColor[TextNum] = TEXT_COLOR_DARKYELLOW;
        }
        else
        {
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
        }
        TextBold[TextNum] = false;
        TextNum++;

        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::RightClickToEnter);
        TextListColor[TextNum] = TEXT_COLOR_DARKBLUE;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 21)
    {
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        switch (Level)
        {
        case 1:
            mu_swprintf(TextList[TextNum], I18N::Game::UsedInTheOnlineEvent);
            break;
        case 2:
            mu_swprintf(TextList[TextNum], I18N::Game::UsedInTheMyFriendEvent);
            break;
        case 3:
            mu_swprintf(TextList[TextNum], I18N::Game::UseInSiegeRegistration);
            break;
        default:
            break;
        }
        TextNum++;
    }
    else if (ip->Type == ITEM_LOST_MAP || ip->Type == ITEM_SYMBOL_OF_KUNDUN)
    {
        TextNum = RenderHellasItemInfo(ip, TextNum);
    }
    else if (ip->Type == ITEM_CAPE_OF_FIGHTER || ip->Type == ITEM_CAPE_OF_LORD)
    {
        // ?? ?? ????
        mu_swprintf(TextList[TextNum], I18N::Game::IncreaseDOfDamage, 20 + Level * 2); //  ??? ?%??
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        int _iDamage = (ip->Type == ITEM_CAPE_OF_FIGHTER) ? 10 + Level * 2 : 10 + Level;
        mu_swprintf(TextList[TextNum], I18N::Game::AbsorbDOfDamage, _iDamage); //  ??? ?%??
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
    }

    BOOL bDurExist = FALSE;
    if ((p->Durability || p->MagicDur) &&
            ((ip->Type < ITEM_WING || ip->Type >= ITEM_HELPER) && ip->Type < ITEM_POTION) ||
        (ip->Type >= ITEM_WING && ip->Type <= ITEM_WINGS_OF_DARKNESS) ||
        (ip->Type >= ITEM_WING_OF_STORM && ip->Type <= ITEM_WING_OF_DIMENSION) ||
        (ip->Type >= ITEM_CAPE_OF_FIGHTER && ip->Type <= ITEM_CAPE_OF_OVERRULE))
    {
        bDurExist = TRUE;
    }
    if ((bDurExist || ip->Durability) &&
        (ip->Type < ITEM_LOCHS_FEATHER || ip->Type > ITEM_WEAPON_OF_ARCHANGEL) &&
        !(ip->Type == ITEM_WIZARDS_RING && Level == 1) &&
        !(ip->Type == ITEM_WIZARDS_RING && Level == 2) && !(ip->Type == ITEM_ARMOR_OF_GUARDSMAN) &&
        ip->Type != ITEM_SIEGE_POTION && ip->Type != ITEM_HELPER + 7 &&
        ip->Type != ITEM_LIFE_STONE_ITEM && ip->Type != ITEM_FRAGMENT_OF_HORN &&
        !(ip->Type >= ITEM_POTION + 70 && ip->Type <= ITEM_POTION + 71) &&
        !(ip->Type >= ITEM_HELPER + 54 && ip->Type <= ITEM_HELPER + 58) &&
        !(ip->Type >= ITEM_POTION + 78 && ip->Type <= ITEM_POTION + 82) &&
        !(ip->Type == ITEM_HELPER + 66) &&
        !(ip->Type == ITEM_HELPER + 71 || ip->Type == ITEM_HELPER + 72 ||
          ip->Type == ITEM_HELPER + 73 || ip->Type == ITEM_HELPER + 74 ||
          ip->Type == ITEM_HELPER + 75) &&
        !(ip->Type == ITEM_HELPER + 97) && !(ip->Type == ITEM_HELPER + 98) &&
        !(ip->Type == ITEM_POTION + 91) && !(ip->Type == ITEM_HELPER + 99) &&
        !(ip->Type == ITEM_POTION + 133))
    {
        int Success = false;
        int arrow = false;
        if (ip->Type >= ITEM_POTION && ip->Type <= ITEM_ANTIDOTE)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::NumberOfItemsD, ip->Durability);
            Success = true;
        }
        else if (ip->Type == ITEM_POTION + 21 && Level == 3)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::NumberOfItemsD, ip->Durability);
            Success = true;
        }
        else if (ip->Type == ITEM_BOLT || ip->Type == ITEM_ARROWS)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::NumberOfItemsD, ip->Durability);
            Success = true;
            arrow = true;
        }
        else if (ip->Type >= ITEM_SMALL_SHIELD_POTION && ip->Type <= ITEM_LARGE_COMPLEX_POTION)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::NumberOfItemsD, ip->Durability);
            Success = true;
        }
        else if (ip->Type == ITEM_POTION + 133)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::NumberOfItemsD, ip->Durability);
            Success = true;
        }
        else if (ip->Type >= ITEM_JACK_OLANTERN_BLESSINGS && ip->Type <= ITEM_JACK_OLANTERN_DRINK)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::NumberOfItemsD, ip->Durability);
            Success = true;
        }
        else if (ip->Type >= ITEM_POTION + 153 && ip->Type <= ITEM_POTION + 156)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::NumberOfItemsD, ip->Durability);
            Success = true;
        }
        else if (ip->Type >= ITEM_SPLINTER_OF_ARMOR && ip->Type <= ITEM_BLESS_OF_GUARDIAN)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::DD, ip->Durability, 20);
            Success = true;
        }
        else if (ip->Type == ITEM_CLAW_OF_BEAST)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::DD, ip->Durability, 10);
            Success = true;
        }
        else if (ip->Type == ITEM_HORN_OF_FENRIR)
        {
            if (ip->bPeriodItem == false)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::LifeD, ip->Durability);
                Success = true;
            }
        }
        else if (ip->Type >= ITEM_HELPER && ip->Type <= ITEM_HELPER + 7)
        {
            if (ip->bPeriodItem == false)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::LifeD, ip->Durability);
                Success = true;
            }
        }
        else if (ip->Type == ITEM_TRANSFORMATION_RING)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::DurabilityD, ip->Durability);
            Success = true;
        }
        else if (ip->Type == ITEM_DEMON || ip->Type == ITEM_SPIRIT_OF_GUARDIAN)
        {
            if (ip->bPeriodItem == false)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::LifeD, ip->Durability);
                Success = true;
            }
        }
        else if (ip->Type == ITEM_PET_RUDOLF || ip->Type == ITEM_PET_PANDA ||
                 ip->Type == ITEM_PET_UNICORN || ip->Type == ITEM_PET_SKELETON)
        {
            if (ip->bPeriodItem == false)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::LifeD, ip->Durability);
                Success = true;
            }
        }
        else if (ip->Type >= ITEM_HELPER + 46 && ip->Type <= ITEM_HELPER + 48)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::UsableDtimes, ip->Durability);
            Success = true;
        }
        else if (ip->Type >= ITEM_HELPER + 125 && ip->Type <= ITEM_HELPER + 127)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::UsableDtimes, ip->Durability);

            if (ip->Type == ITEM_HELPER + 126)
            {
                TextNum++;
                mu_swprintf(TextList[TextNum], I18N::Game::CanEnterTheMondaySaturdayMap);
            }
            else if (ip->Type == ITEM_HELPER + 127)
            {
                TextNum++;
                mu_swprintf(TextList[TextNum], I18N::Game::CanEnterTheSundayMap);
            }
            Success = true;
        }
        else if (ip->Type == ITEM_POTION + 53)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::DCombinationSuccessRateIncrease,
                        ip->Durability);
            Success = true;
        }
        else if (ip->Type == ITEM_HELPER + 61)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::UsableDtimes, ip->Durability);
            Success = true;
        }
        else if (ip->Type == ITEM_POTION + 100)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::NumberOfItemsD, ip->Durability);
            Success = true;
        }
        else if (ip->Type == ITEM_HELPER + 70)
        {
            if (ip->Durability == 2)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::SavesTheLocationWithTheRightMouseClick);
                TextListColor[TextNum] = TEXT_COLOR_BLUE;
                TextBold[TextNum] = false;
                TextNum++;
            }
            else if (ip->Durability == 1)
            {
                mu_swprintf(TextList[TextNum], I18N::Game::ReturnsToTheSavedLocationByAClick);
                TextListColor[TextNum] = TEXT_COLOR_BLUE;
                TextBold[TextNum] = false;
                TextNum++;
                g_PortalMgr.GetPortalPositionText(TextList[TextNum]);
                TextListColor[TextNum] = TEXT_COLOR_BLUE;
                TextBold[TextNum] = false;
                TextNum++;
            }
        }
        else if (ip->Type >= ITEM_HELPER + 135 && ip->Type <= ITEM_HELPER + 145)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::YouCanAchieveSpecialItemsWithCombinations);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], L"\n");
            TextNum++;
            SkipNum++;
            Success = true;
        }
        else if ((bDurExist) && (ip->bPeriodItem == false))
        {
            int maxDurability = CalcMaxDurability(ip, p, Level);

            mu_swprintf(TextList[TextNum], I18N::Game::DurabilityDD, ip->Durability, maxDurability);
            Success = true;
        }
        else if (ip->Type >= ITEM_TYPE_CHARM_MIXWING + EWS_BEGIN &&
                 ip->Type <= ITEM_TYPE_CHARM_MIXWING + EWS_END)
        {
            mu_swprintf(
                TextList[TextNum], L"%ls",
                I18N::Game::Lookup(2732 + (ip->Type - (ITEM_TYPE_CHARM_MIXWING + EWS_BEGIN))));

            Success = true;
        }
        else if (ip->Type == ITEM_HELPER + 121)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::UsableDtimes, ip->Durability);
            Success = true;
        }

        if (Success)
        {
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
    }
    else
    {
        if (ip->Type == ITEM_TRANSFORMATION_RING)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::DurabilityD, ip->Durability);

            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
    }

    if (ip->Type == ITEM_BOLT || ip->Type == ITEM_ARROWS)
    {
        if (Level >= 1)
        {
            int value = Level * 2 + 1;

            mu_swprintf(TextList[TextNum], I18N::Game::IncreaseDOfDamage, value);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::AdditionalDmgD, 1);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
        }
    }

    for (int i = 0; i < MAX_RESISTANCE; i++)
    {
        if (p->Resistance[i])
        {
            mu_swprintf(TextList[TextNum], I18N::Game::SResistanceD, I18N::Game::Lookup(48 + i),
                        Level + 1);
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
    }

    if (ip->RequireLevel && ip->Type != ITEM_LOCHS_FEATHER)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::MinimumLevelRequirementD, ip->RequireLevel);
        if (CharacterAttribute->Level < ip->RequireLevel)
        {
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::LackingD,
                        ip->RequireLevel - CharacterAttribute->Level);
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
        }
        else
        {
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
    }

    int si_iNeedStrength = 0, si_iNeedDex = 0;
    bool bRequireStat = true;

    if (Check_LuckyItem(ip->Type))
        bRequireStat = false;

    if (Level >= ip->Jewel_Of_Harmony_OptionLevel)
    {
        StrengthenCapability SC;
        g_pUIJewelHarmonyinfo->GetStrengthenCapability(&SC, ip, 0);

        if (SC.SI_isNB)
        {
            si_iNeedStrength = SC.SI_NB.SI_force;
            si_iNeedDex = SC.SI_NB.SI_activity;
        }
    }

    if (ip->SocketCount > 0)
    {
        for (int i = 0; i < ip->SocketCount; ++i)
        {
            if (ip->SocketSeedID[i] == 38)
            {
                int iReqStrengthDown = g_SocketItemMgr.GetSocketOptionValue(ip, i);
                si_iNeedStrength += iReqStrengthDown;
            }
            else if (ip->SocketSeedID[i] == 39)
            {
                int iReqDexterityDown = g_SocketItemMgr.GetSocketOptionValue(ip, i);
                si_iNeedDex += iReqDexterityDown;
            }
        }
    }

    if (ip->RequireStrength && bRequireStat)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::StrengthRequirementD,
                    ip->RequireStrength - si_iNeedStrength);

        WORD Strength;
        Strength = CharacterAttribute->Strength + CharacterAttribute->AddStrength;
        if (Strength < ip->RequireStrength - si_iNeedStrength)
        {
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::LackingD,
                        (ip->RequireStrength - Strength) - si_iNeedStrength);
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
        }
        else
        {
            if (si_iNeedStrength != 0)
            {
                TextListColor[TextNum] = TEXT_COLOR_YELLOW;
            }
            else
            {
                TextListColor[TextNum] = TEXT_COLOR_WHITE;
            }

            TextBold[TextNum] = false;
            TextNum++;
        }
    }
    if (ip->RequireDexterity && bRequireStat)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AgilityRequirementD,
                    ip->RequireDexterity - si_iNeedDex);
        WORD Dexterity;
        Dexterity = CharacterAttribute->Dexterity + CharacterAttribute->AddDexterity;
        if (Dexterity < (ip->RequireDexterity - si_iNeedDex))
        {
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;

            mu_swprintf(TextList[TextNum], I18N::Game::LackingD,
                        (ip->RequireDexterity - Dexterity) - si_iNeedDex);
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
        }
        else
        {
            if (si_iNeedDex != 0)
            {
                TextListColor[TextNum] = TEXT_COLOR_YELLOW;
            }
            else
            {
                TextListColor[TextNum] = TEXT_COLOR_WHITE;
            }
            TextBold[TextNum] = false;
            TextNum++;
        }
    }

    if (ip->RequireVitality && bRequireStat) //  ????.
    {
        mu_swprintf(TextList[TextNum], I18N::Game::StaminaRequirementD, ip->RequireVitality);

        WORD Vitality;
        Vitality = CharacterAttribute->Vitality + CharacterAttribute->AddVitality;
        if (Vitality < ip->RequireVitality)
        {
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::LackingD, ip->RequireVitality - Vitality);
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
        }
        else
        {
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
    }

    if (ip->RequireEnergy && bRequireStat)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::EnergyRequirementD, ip->RequireEnergy);

        WORD Energy;
        Energy = CharacterAttribute->Energy + CharacterAttribute->AddEnergy;

        if (Energy < ip->RequireEnergy)
        {
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::LackingD, ip->RequireEnergy - Energy);
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
        }
        else
        {
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
    }

    if (ip->RequireCharisma && bRequireStat)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::CharismaRequirementD, ip->RequireCharisma);

        WORD Charisma;
        Charisma = CharacterAttribute->Charisma + CharacterAttribute->AddCharisma;
        if (Charisma < ip->RequireCharisma)
        {
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::LackingD, ip->RequireCharisma - Charisma);
            TextListColor[TextNum] = TEXT_COLOR_RED;
            TextBold[TextNum] = false;
            TextNum++;
        }
        else
        {
            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
    }

    if (IsRequireClassRenderItem(ip->Type))
    {
        RequireClass(p);
    }

    if (ip->Type >= MODEL_BOOTS - MODEL_ITEM &&
        ip->Type < MODEL_BOOTS + MAX_ITEM_INDEX - MODEL_ITEM)
    {
        if (Level >= 5)
        {
            mu_swprintf(TextList[TextNum], L"\n");
            TextNum++;
            SkipNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::IncreasesMovingSpeed);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = true;
            TextNum++;
        }
    }

    if (ip->Type >= MODEL_GLOVES - MODEL_ITEM &&
        ip->Type < MODEL_GLOVES + MAX_ITEM_INDEX - MODEL_ITEM)
    {
        if (Level >= 5)
        {
            mu_swprintf(TextList[TextNum], L"\n");
            TextNum++;
            SkipNum++;

            mu_swprintf(TextList[TextNum], I18N::Game::SwimmingSpeedIncrease);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = true;
            TextNum++;
        }
    }
    if ((ip->Type >= MODEL_STAFF - MODEL_ITEM &&
         ip->Type < MODEL_STAFF + MAX_ITEM_INDEX - MODEL_ITEM) ||
        (ip->Type == (static_cast<int>(MODEL_RUNE_BLADE) - MODEL_ITEM)) ||
        (ip->Type == (static_cast<int>(MODEL_EXPLOSION_BLADE) - MODEL_ITEM)) ||
        (ip->Type == (static_cast<int>(MODEL_SWORD_DANCER) - MODEL_ITEM)) ||
        (ip->Type == (static_cast<int>(MODEL_DARK_REIGN_BLADE) - MODEL_ITEM)) ||
        (ip->Type == (static_cast<int>(MODEL_IMPERIAL_SWORD) - MODEL_ITEM)))
    {
        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;

        int nText = ITEM_BOOK_OF_SAHAMUTT <= ip->Type && ip->Type <= ITEM_STAFF + 29 ? 1691 : 79;
        ::mu_swprintf(TextList[TextNum], I18N::Game::Lookup(nText), ip->MagicPower);

        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = true;
        TextNum++;
    }

    if (IsCepterItem(ip->Type) == true)
    {
        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::IncreasePetAttackAsD, ip->MagicPower);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = true;
        TextNum++;
    }

    if (ip->SpecialNum > 0)
    {
        int iModelType = ip->Type;
        int iStartModelType = ITEM_HELPER + 109;
        int iEndModelType = ITEM_HELPER + 115;

        if (!(ITEM_HELPER + 109 <= ip->Type && ITEM_HELPER + 115 >= ip->Type))
        {
            mu_swprintf(TextList[TextNum], L"\n");
            TextNum++;
            SkipNum++;
        }
    }

    if (ip->option_380 != 0)
    {
        std::vector<std::wstring> Text380;

        if (g_pItemAddOptioninfo)
        {
            g_pItemAddOptioninfo->GetItemAddOtioninfoText(Text380, ip->Type);

            mu_swprintf(TextList[TextNum], L"\n");
            TextNum++;
            SkipNum++;

            for (int i = 0; i < (int)Text380.size(); ++i)
            {
                wcsncpy(TextList[TextNum], Text380[i].c_str(), 100);
                TextListColor[TextNum] = TEXT_COLOR_REDPURPLE;
                TextBold[TextNum] = true;
                TextNum++;
            }

            mu_swprintf(TextList[TextNum], L"\n");
            TextNum++;
            SkipNum++;
        }
    }
    //#ifndef PBG_MOD_NEWCHAR_MONK_WING_2
    //#ifdef PBG_MOD_NEWCHAR_MONK_WING
    //	if(ip->Type==ITEM_WING+49)
    //	{
    //		mu_swprintf(TextList[TextNum],I18N::Game::AbsorbDOfDamage,15+Level);
    //		TextListColor[TextNum] = TEXT_COLOR_BLUE;
    //		TextNum++;
    //	}
    //#endif //PBG_MOD_NEWCHAR_MONK_WING
    //#endif //PBG_MOD_NEWCHAR_MONK_WING_2
    if (g_SocketItemMgr.IsSocketItem(ip))
        ;
    else if (ip->Jewel_Of_Harmony_Option != 0)
    {
        StrengthenItem type = g_pUIJewelHarmonyinfo->GetItemType(static_cast<int>(ip->Type));

        if (type < SI_None)
        {
            if (g_pUIJewelHarmonyinfo->IsHarmonyJewelOption(type, ip->Jewel_Of_Harmony_Option))
            {
                mu_swprintf(TextList[TextNum], L"\n");
                TextNum++;
                SkipNum++;

                HARMONYJEWELOPTION harmonyjewel = g_pUIJewelHarmonyinfo->GetHarmonyJewelOptionInfo(
                    type, ip->Jewel_Of_Harmony_Option);

                if (type == SI_Defense && ip->Jewel_Of_Harmony_Option == 7)
                {
                    mu_swprintf(TextList[TextNum], L"%ls +%d%%", harmonyjewel.Name,
                                harmonyjewel.HarmonyJewelLevel[ip->Jewel_Of_Harmony_OptionLevel]);
                }
                else
                {
                    mu_swprintf(TextList[TextNum], L"%ls +%d", harmonyjewel.Name,
                                harmonyjewel.HarmonyJewelLevel[ip->Jewel_Of_Harmony_OptionLevel]);
                }

                if (Level >= ip->Jewel_Of_Harmony_OptionLevel)
                    TextListColor[TextNum] = TEXT_COLOR_YELLOW;
                else
                    TextListColor[TextNum] = TEXT_COLOR_GRAY;

                TextBold[TextNum] = true;
                TextNum++;

                mu_swprintf(TextList[TextNum], L"\n");
                TextNum++;
                SkipNum++;
            }
            else
            {
                mu_swprintf(TextList[TextNum], L"\n");
                TextNum++;
                SkipNum++;

                mu_swprintf(TextList[TextNum], L"%ls : %d %d %d",
                            I18N::Game::ReinforcementOptionError, (int)type,
                            (int)ip->Jewel_Of_Harmony_Option,
                            (int)ip->Jewel_Of_Harmony_OptionLevel);

                TextListColor[TextNum] = TEXT_COLOR_DARKRED;
                TextBold[TextNum] = true;
                TextNum++;

                mu_swprintf(TextList[TextNum], I18N::Game::SendScreenshotsWithTheReport);

                TextListColor[TextNum] = TEXT_COLOR_DARKRED;
                TextBold[TextNum] = true;
                TextNum++;

                mu_swprintf(TextList[TextNum], L"\n");
                TextNum++;
                SkipNum++;
            }
        }
    }

    TextNum = g_csItemOption.RenderDefaultOptionText(ip, TextNum);

    int iMana;
    for (int i = 0; i < ip->SpecialNum; i++)
    {
        if (ITEM_HELPER + 109 <= ip->Type && ITEM_HELPER + 115 >= ip->Type)
        {
            break;
        }

        gSkillManager.GetSkillInformation(ip->Special[i], 1, NULL, &iMana, NULL);
        GetSpecialOptionText(ip->Type, TextList[TextNum], ip->Special[i], ip->SpecialValue[i],
                             iMana);

        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        if (ip->Special[i] == AT_LUCK)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::LuckCriticalDamageRate5,
                        ip->SpecialValue[i]);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
        }
        else if (ip->Special[i] == AT_SKILL_RIDER)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::KnightSpecificSkill);
            TextListColor[TextNum] = TEXT_COLOR_DARKRED;
            TextBold[TextNum] = false;
            TextNum++;
        }
        else if (ip->Special[i] == AT_SKILL_EARTHSHAKE ||
                 ip->Special[i] == AT_SKILL_EARTHSHAKE_STR ||
                 ip->Special[i] == AT_SKILL_EARTHSHAKE_MASTERY)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::DarkLordExclusiveSkill);
            TextListColor[TextNum] = TEXT_COLOR_DARKRED;
            TextBold[TextNum] = false;
            TextNum++;
        }
        else if ((ip->Special[i] == AT_IMPROVE_DAMAGE) &&
                 ((ip->Type == ITEM_RUNE_BLADE) || (ip->Type == ITEM_DARK_REIGN_BLADE) ||
                  (ip->Type == ITEM_EXPLOSION_BLADE) || (ip->Type == ITEM_SWORD_DANCER) ||
                  (ip->Type == ITEM_IMPERIAL_SWORD)))
        {
            mu_swprintf(TextList[TextNum], I18N::Game::AdditionalWizardryDmgD, ip->SpecialValue[i]);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
        }
    }

    mu_swprintf(TextList[TextNum], L"\n");
    TextNum++;
    SkipNum++;

    if (ip->Type == ITEM_SPLINTER_OF_ARMOR || ip->Type == ITEM_BLESS_OF_GUARDIAN)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::FragmentOfHornCanBeMade);
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextNum++;
    }
    else if (ip->Type == ITEM_CLAW_OF_BEAST || ip->Type == ITEM_FRAGMENT_OF_HORN)
    {
        mu_swprintf(TextList[TextNum],
                    I18N::Game::BrokenHornCanBeMadeUsingTheClawOfBeastAndFragmentOfHorn);
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextNum++;
    }
    else if (ip->Type == ITEM_BROKEN_HORN)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::FenrirSHornCanBeMadeThroughItemCombination);
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextNum++;
    }
    else if (ip->Type == ITEM_HORN_OF_FENRIR)
    {
        GetSpecialOptionText(0, TextList[TextNum], AT_SKILL_PLASMA_STORM_FENRIR, 0, 0);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        SkipNum++;
        if (ip->ExcellentFlags == 0x01)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::IncreaseFinalDamageD, 10);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextNum++;

            mu_swprintf(TextList[TextNum], I18N::Game::IncreaseSpeed);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextNum++;
        }
        else if (ip->ExcellentFlags == 0x02)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::AbsorbFinalDamageD, 10);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextNum++;

            mu_swprintf(TextList[TextNum], I18N::Game::IncreaseSpeed);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextNum++;
        }
        else if (ip->ExcellentFlags == 0x04)
        {
            WORD wLevel = CharacterAttribute->Level;

            mu_swprintf(TextList[TextNum], I18N::Game::AddedDOfLife, (wLevel / 2));
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::AddedDOfMana, (wLevel / 2));
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::AddedDAttack, (wLevel / 12));
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::AddedDWizardry, (wLevel / 25));
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextNum++;
            mu_swprintf(TextList[TextNum], L"\n");
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::GoldenFenrir, (Hero->Level / 2));
            TextListColor[TextNum] = TEXT_COLOR_GREEN;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::ExclusiveEditionOnlyGivenToMUHeroes,
                        (Hero->Level / 2));
            TextListColor[TextNum] = TEXT_COLOR_GREEN;
            TextNum++;
        }

        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;

        mu_swprintf(TextList[TextNum], I18N::Game::CanSummonTheFenrirWhenEquipped);
        TextListColor[TextNum] = TEXT_COLOR_YELLOW;
        TextNum++;

        if (ip->ExcellentFlags == 0x00)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::SkillsWillImproveThroughUpgrading);
            TextListColor[TextNum] = TEXT_COLOR_YELLOW;
            TextNum++;
        }
    }
    else if (ip->Type == ITEM_TRANSFORMATION_RING)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::UnableToEquipWithADifferentTransformationRing);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_ELITE_TRANSFER_SKELETON_RING)
    {
        wchar_t strText[100];
        mu_swprintf(strText, I18N::Game::IncreaseDefensiveSkillD, 10);
        mu_swprintf(TextList[TextNum], L"%ls%%", strText);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextNum++;

        WORD wlevel = CharacterAttribute->Level;
        mu_swprintf(TextList[TextNum], I18N::Game::VitalityD2225, wlevel);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextNum++;

        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;

        mu_swprintf(TextList[TextNum], I18N::Game::UnableToEquipWithADifferentTransformationRing);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_FIRECRACKER)
    {
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        mu_swprintf(TextList[TextNum], I18N::Game::FireworksWillAppearOnceThrownInTheField);
        TextNum++;
    }
    else if (ip->Type == ITEM_GM_GIFT)
    {
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        mu_swprintf(TextList[TextNum], I18N::Game::GMHasGiftedThisSpecialBox);
        TextNum++;
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        mu_swprintf(TextList[TextNum], I18N::Game::DropItToReceiveTheGift);
        TextNum++;
    }
    else if (ip->Type == ITEM_JACK_OLANTERN_TRANSFORMATION_RING)
    {
        mu_swprintf(TextList[TextNum], L"%ls", I18N::Game::EnjoyHalloweenFestival);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;

        mu_swprintf(TextList[TextNum], I18N::Game::UnableToEquipWithADifferentTransformationRing);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextBold[TextNum] = false;
        TextNum++;
        TextNum++;

        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;

        mu_swprintf(TextList[TextNum], I18N::Game::UnableToEquipWithADifferentTransformationRing);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_CHRISTMAS_TRANSFORMATION_RING)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::AdditionalDmgD, 20);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::AdditionalWizardryDmgD, 20);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextNum++;
        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;
        mu_swprintf(TextList[TextNum], L"%ls", I18N::Game::MerryChristmas);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextNum++;
        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::UnableToEquipWithADifferentTransformationRing);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_PANDA_TRANSFORMATION_RING)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::TransformIntoPanda);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::ZenIncrease50);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::DamageWizardryCurse30);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::UnableToEquipWithADifferentTransformationRing);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_SKELETON_TRANSFORMATION_RING)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::EquipToTransformIntoASkeletonWarrior);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::DamageWizardryCurse40);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::EquippingAlongWithAPetSkeleton);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::IncreasesEXPBy30);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::UnableToEquipWithADifferentTransformationRing);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_CHRISTMAS_STAR)
    {
        mu_swprintf(TextList[TextNum], L"%ls", I18N::Game::FireworksWillAppearOnceThrownInTheField);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextNum++;
    }
    else if (ip->Type == ITEM_GAME_MASTER_TRANSFORMATION_RING)
    {
        for (int i = 0; i < 7; ++i)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::Lookup(976 + i), 255);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextNum++;
        }

        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::UnableToEquipWithADifferentTransformationRing);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextBold[TextNum] = false;
        TextNum++;
    }
    else if (ip->Type == ITEM_HELPER + 66)
    {
        TextNum--;
        mu_swprintf(TextList[TextNum], I18N::Game::UsableDtimes, ip->Durability);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextNum++;
        mu_swprintf(TextList[TextNum], L"%ls",
                    I18N::Game::RelocateToTheSantaSVillageByTheRightMouseClick);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextNum++;
    }
    else if (ip->Type == ITEM_POTION + 100)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::RegisterWithTheNPCToReceiveVariousGifts,
                    ip->Durability);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextNum++;
    }
    else if (ip->Type == ITEM_PET_SKELETON)
    {
        TextNum--;
        SkipNum--;

        mu_swprintf(TextList[TextNum], I18N::Game::EquippingAlongWithASkeletonTransformationRing);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        mu_swprintf(TextList[TextNum], I18N::Game::IncreasesEXPBy30);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
    else if (g_pMyInventory->IsInvenItem(ip->Type))
    {
        TextNum--;
        SkipNum--;

        if (ip->Durability == 254)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::InUse);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], L"\n");
            TextNum++;
            SkipNum++;
        }

        switch (ip->Type)
        {
#ifdef LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM
        case ITEM_HELPER + 128:
        case ITEM_HELPER + 129:
            mu_swprintf(TextList[TextNum], I18N::Game::FigurineItem);
            break;
        case ITEM_HELPER + 134:
            mu_swprintf(TextList[TextNum], I18N::Game::RelicItem);
            break;
#endif //LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM
#ifdef LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM_PART_2
        case ITEM_HELPER + 130:
        case ITEM_HELPER + 131:
        case ITEM_HELPER + 132:
        case ITEM_HELPER + 133:
            mu_swprintf(TextList[TextNum], I18N::Game::CharmItem);
            break;
#endif //LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM_PART_2

        default:
            break;
        }

        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;

        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;
        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;

        switch (ip->Type)
        {
#ifdef LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM
        case ITEM_HELPER + 128:
            mu_swprintf(TextList[TextNum], I18N::Game::IncreaseCriticalDamageD, 10);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
            break;
        case ITEM_HELPER + 129:
            mu_swprintf(TextList[TextNum], I18N::Game::IncreaseExcellentDamageD, 10);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
            break;
        case ITEM_HELPER + 134:
            mu_swprintf(TextList[TextNum], I18N::Game::ItemDropRateIncreaseD, 20);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
            break;
#endif //LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM
#ifdef LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM_PART_2
        case ITEM_HELPER + 130:
            mu_swprintf(TextList[TextNum], I18N::Game::MaximumHPIncreaseD, 50);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
            break;
        case ITEM_HELPER + 131:
            mu_swprintf(TextList[TextNum], I18N::Game::MaximumMPIncreaseD, 50);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
            break;
        case ITEM_HELPER + 132:
#ifdef LJH_MOD_CHANGED_GOLDEN_OAK_CHARM_STAT
            mu_swprintf(TextList[TextNum], I18N::Game::MaximumHPIncreaseD, 100);
#else  //LJH_MOD_CHANGED_GOLDEN_OAK_CHARM_STAT
            mu_swprintf(TextList[TextNum], I18N::Game::MaximumHPIncreaseD, 150);
#endif //LJH_MOD_CHANGED_GOLDEN_OAK_CHARM_STAT
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
#ifdef LJH_MOD_CHANGED_GOLDEN_OAK_CHARM_STAT
            mu_swprintf(TextList[TextNum], I18N::Game::MaximumSPIncreaseD, 500);
#else  //LJH_MOD_CHANGED_GOLDEN_OAK_CHARM_STAT
            mu_swprintf(TextList[TextNum], I18N::Game::MaximumSPIncreaseD, 50);
#endif //LJH_MOD_CHANGED_GOLDEN_OAK_CHARM_STAT
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
            break;
        case ITEM_HELPER + 133: // ??????
            mu_swprintf(TextList[TextNum], I18N::Game::MaximumMPIncreaseD, 150);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::MaximumAGIncreaseD, 50);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
            TextBold[TextNum] = false;
            TextNum++;
            break;
#endif //LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM_PART_2
        default:
            break;
        }

        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;
        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;

        mu_swprintf(TextList[TextNum], I18N::Game::RightClickOnYourInventoryToUse);
        TextListColor[TextNum] = TEXT_COLOR_BLUE;
        TextBold[TextNum] = false;
        TextNum++;
    }
#endif //LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
    else if (ip->Type == ITEM_SNOWMAN_TRANSFORMATION_RING)
    {
        mu_swprintf(TextList[TextNum], I18N::Game::UnableToEquipWithADifferentTransformationRing);
        TextListColor[TextNum] = TEXT_COLOR_RED;
        TextBold[TextNum] = false;
        TextNum++;
    }

    if (ip->bPeriodItem == true)
    {
        if (ip->bExpiredPeriod == true)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::ExpiredItem);
            TextListColor[TextNum] = TEXT_COLOR_RED;
        }
        else
        {
            mu_swprintf(TextList[TextNum], I18N::Game::ExpirationDay);
            TextListColor[TextNum] = TEXT_COLOR_ORANGE;
            TextNum++;
            SkipNum++;

            mu_swprintf(TextList[TextNum], L"%d-%02d-%02d  %02d:%02d", ExpireTime->tm_year + 1900,
                        ExpireTime->tm_mon + 1, ExpireTime->tm_mday, ExpireTime->tm_hour,
                        ExpireTime->tm_min);
            TextListColor[TextNum] = TEXT_COLOR_BLUE;
        }

        TextNum++;
    }

    if (!bItemTextListBoxUse)
    {
        bool bThisisEquippedItem = false;

        SEASON3B::CNewUIInventoryCtrl *pNewInventoryCtrl = g_pMyInventory->GetInventoryCtrl();
        ITEM *pFindItem = pNewInventoryCtrl->FindItemByKey(ip->Key);
        (pFindItem == NULL) ? bThisisEquippedItem = true : bThisisEquippedItem = false;

        TextNum = g_csItemOption.RenderSetOptionListInItem(ip, TextNum, bThisisEquippedItem);

        TextNum = g_SocketItemMgr.AttachToolTipForSocketItem(ip, TextNum);

        SIZE TextSize = {0, 0};
        float fRateY = g_fScreenRate_y;
        int Height = 0;
        int EmptyLine = 0;
        int TextLine = 0;

        for (int i = 0; i < TextNum; ++i)
        {
            if (TextList[i][0] == '\0')
                break;
            else if (TextList[i][0] == '\n')
                ++EmptyLine;
            else
                ++TextLine;
        }
        fRateY = fRateY / 1.1f;
        g_RenderText.SetFont(LegacyFontRole::Normal);

        g_RenderText.MeasureText(TextList[0], 1, &TextSize);

        Height = (TextLine * TextSize.cy + EmptyLine * TextSize.cy / 2.0f) / fRateY;

        int iScreenHeight = 420;

        int nInvenHeight = p->Height * INVENTORY_SCALE;

        sy += INVENTORY_SCALE;
        if (sy + Height > iScreenHeight)
        {
            sy += iScreenHeight - (sy + Height);
        }
        else if (sy + Height > iScreenHeight)
        {
        }
    }

    bool isrendertooltip = true;

    if (isrendertooltip)
    {
        if (bItemTextListBoxUse)
            RenderTipTextList(sx, sy, TextNum, 0, RT3_SORT_CENTER, STRP_BOTTOMCENTER);
        else
            RenderTipTextList(sx, sy, TextNum, 0);
    }
}

void SessionRenderUnit::RenderRepairInfo(int sx, int sy, ITEM *ip, bool Sell)
{
    if (IsRepairBan(ip) == true)
    {
        return;
    }
    if (ip->Type >= ITEM_TYPE_CHARM_MIXWING + EWS_BEGIN &&
        ip->Type <= ITEM_TYPE_CHARM_MIXWING + EWS_END)
    {
        return;
    }
    if (ip->Type == ITEM_HELPER + 107)
    {
        return;
    }
    if (ip->Type == ITEM_HELPER + 104)
    {
        return;
    }
    if (ip->Type == ITEM_HELPER + 105)
    {
        return;
    }
    if (ip->Type == ITEM_HELPER + 103)
    {
        return;
    }
    if (ip->Type == ITEM_POTION + 133)
    {
        return;
    }
    if (ip->Type == MODEL_HELPER + 109)
    {
        return;
    }
    if (ip->Type == MODEL_HELPER + 110)
    {
        return;
    }
    if (ip->Type == MODEL_HELPER + 111)
    {
        return;
    }
    if (ip->Type == MODEL_HELPER + 112)
    {
        return;
    }
    if (ip->Type == MODEL_HELPER + 113)
    {
        return;
    }
    if (ip->Type == MODEL_HELPER + 114)
    {
        return;
    }
    if (ip->Type == MODEL_HELPER + 115)
    {
        return;
    }
    if (ip->Type == MODEL_POTION + 112)
    {
        return;
    }
    if (ip->Type == MODEL_POTION + 113)
    {
        return;
    }
    if (ip->Type == ITEM_POTION + 120)
    {
        return;
    }
    if (ip->Type == ITEM_POTION + 121)
    {
        return;
    }
    if (ip->Type == ITEM_POTION + 122)
    {
        return;
    }
    if (ITEM_POTION + 123 == ip->Type)
    {
        return;
    }
    if (ITEM_POTION + 124 == ip->Type)
    {
        return;
    }
    if (ITEM_POTION + 134 <= ip->Type && ip->Type <= ITEM_POTION + 139)
    {
        return;
    }

    if (ITEM_WING + 130 <= ip->Type && ip->Type <= ITEM_WING + 135)
    {
        return;
    }

    if (ITEM_POTION + 114 <= ip->Type && ip->Type <= ITEM_POTION + 119)
    {
        return;
    }
    if (ITEM_POTION + 126 <= ip->Type && ip->Type <= ITEM_POTION + 129)
    {
        return;
    }
    if (ITEM_POTION + 130 <= ip->Type && ip->Type <= ITEM_POTION + 132)
    {
        return;
    }
    if (ITEM_HELPER + 121 == ip->Type)
    {
        return;
    }

    ITEM_ATTRIBUTE *p = &ItemAttribute[ip->Type];
    TextNum = 0;
    SkipNum = 0;
    for (int i = 0; i < 30; i++)
    {
        TextList[i][0] = 0;
    }

    int Level = ip->Level;
    int Color;

    if (ip->Type == ITEM_JEWEL_OF_BLESS || ip->Type == ITEM_JEWEL_OF_SOUL ||
        ip->Type == ITEM_JEWEL_OF_CHAOS)
    {
        Color = TEXT_COLOR_YELLOW;
    }
    else if (isCompiledGem(ip))
    {
        Color = TEXT_COLOR_YELLOW;
    }
    else if (ItemRulesDetail::IsDivineArchangelWeaponItem(ip->Type))
    {
        Color = TEXT_COLOR_PURPLE;
    }
    else if (ip->Type == ITEM_DEVILS_EYE || ip->Type == ITEM_DEVILS_KEY ||
             ip->Type == ITEM_DEVILS_INVITATION)
    {
        Color = TEXT_COLOR_YELLOW;
    }
    else if (ip->SpecialNum > 0 && ip->ExcellentFlags > 0)
    {
        Color = TEXT_COLOR_GREEN;
    }
    else if (Level >= 7)
    {
        Color = TEXT_COLOR_YELLOW;
    }
    else
    {
        if (ip->SpecialNum > 0)
        {
            Color = TEXT_COLOR_BLUE;
        }
        else
        {
            Color = TEXT_COLOR_WHITE;
        }
    }

    if ((ip->Type >= ITEM_WINGS_OF_SPIRITS && ip->Type <= ITEM_WINGS_OF_DARKNESS) ||
        ip->Type >= ITEM_CAPE_OF_LORD ||
        (ip->Type >= ITEM_WING_OF_STORM && ip->Type <= ITEM_CAPE_OF_EMPEROR) ||
        (ip->Type >= ITEM_WINGS_OF_DESPAIR && ip->Type <= ITEM_WING_OF_DIMENSION) ||
        (ip->Type >= ITEM_CAPE_OF_FIGHTER && ip->Type <= ITEM_CAPE_OF_OVERRULE))
    {
        if (Level >= 7)
        {
            Color = TEXT_COLOR_YELLOW;
        }
        else
        {
            if (ip->SpecialNum > 0)
            {
                Color = TEXT_COLOR_BLUE;
            }
            else
            {
                Color = TEXT_COLOR_WHITE;
            }
        }
    }

    if (ip->Type < ITEM_POTION)
    {
        int maxDurability;

        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        SkipNum++;

        wchar_t Text[100];

        maxDurability = CalcMaxDurability(ip, p, Level);
        if (ip->Durability < maxDurability)
        {
            RepairEnable = 2;

            int iGold = ItemValue(ip, 2);
            if (iGold == -1)
                return;
            ConvertRepairGold(iGold, ip->Durability, maxDurability, ip->Type, Text);
            mu_swprintf(TextList[TextNum], I18N::Game::RepairingCostS, Text);

            TextListColor[TextNum] = Color;
        }
        else
        {
            RepairEnable = 1;
            mu_swprintf(TextList[TextNum], I18N::Game::RepairingCostS, L"0");
            TextListColor[TextNum] = Color;
        }
        TextBold[TextNum] = true;
        TextNum++;

        //        RepairEnable = 1;
    }
    mu_swprintf(TextList[TextNum], L"\n");
    TextNum++;
    SkipNum++;

    if (ip->Type == ITEM_ORB_OF_SUMMONING)
    {
        mu_swprintf(TextList[TextNum], L"%ls %ls", SkillAttribute[30 + Level].Name,
                    I18N::Game::Jewel);
    }
    else if (ip->Type == ITEM_TRANSFORMATION_RING)
    {
        for (int i = 0; i < MAX_MONSTER; i++)
        {
            if (ItemRulesDetail::SommonTable[Level] == MonsterScript[i].Type)
            {
                mu_swprintf(TextList[TextNum], L"%ls %ls", MonsterScript[i].Name,
                            I18N::Game::TransformationRing);
                break;
            }
        }
    }
    else if ((ip->Type == ITEM_DARK_HORSE_ITEM) || (ip->Type == ITEM_DARK_RAVEN_ITEM))
    {
        mu_swprintf(TextList[TextNum], L"%ls", p->Name);
    }
    else if ((ip->Type >= ITEM_WINGS_OF_SPIRITS && ip->Type <= ITEM_WINGS_OF_DARKNESS) ||
             ip->Type >= ITEM_CAPE_OF_LORD ||
             (ip->Type >= ITEM_WING_OF_STORM && ip->Type <= ITEM_CAPE_OF_EMPEROR) ||
             (ip->Type >= ITEM_WINGS_OF_DESPAIR && ip->Type <= ITEM_WING_OF_DIMENSION) ||
             (ip->Type >= ITEM_CAPE_OF_FIGHTER && ip->Type <= ITEM_CAPE_OF_OVERRULE))
    {
        if (Level == 0)
            mu_swprintf(TextList[TextNum], L"%ls", p->Name);
        else
            mu_swprintf(TextList[TextNum], L"%ls +%d", p->Name, Level);
    }
    else
    {
        if (ip->ExcellentFlags > 0)
        {
            if (Level == 0)
                mu_swprintf(TextList[TextNum], L"%ls %ls", I18N::Game::Excellent, p->Name);
            else
                mu_swprintf(TextList[TextNum], L"%ls %ls +%d", I18N::Game::Excellent, p->Name,
                            Level);
        }
        else
        {
            if (Level == 0)
                mu_swprintf(TextList[TextNum], L"%ls", p->Name);
            else
                mu_swprintf(TextList[TextNum], L"%ls +%d", p->Name, Level);
        }
    }
    TextListColor[TextNum] = Color;
    TextBold[TextNum] = true;
    TextNum++;
    mu_swprintf(TextList[TextNum], L"\n");
    TextNum++;
    SkipNum++;

    if (ip->Type < ITEM_POTION)
    {
        if (ip->bPeriodItem == false)
        {
            int maxDurability = CalcMaxDurability(ip, p, Level);

            mu_swprintf(TextList[TextNum], I18N::Game::DurabilityDD, ip->Durability, maxDurability);

            TextListColor[TextNum] = TEXT_COLOR_WHITE;
            TextBold[TextNum] = false;
            TextNum++;
        }
    }

    mu_swprintf(TextList[TextNum], L"\n");
    TextNum++;
    SkipNum++;

    SIZE TextSize = {0, 0};

    g_RenderText.MeasureText(TextList[0], 1, &TextSize);

    int Height = ((TextNum - SkipNum) * TextSize.cy + SkipNum * TextSize.cy / 2) *
                 REFERENCE_HEIGHT / WindowHeight;
    if (sy - Height >= 0)
        sy -= Height;
    else
        sy += p->Height * INVENTORY_SCALE;

    RenderTipTextList(sx, sy, TextNum, 0);
}
void SessionRenderUnit::PruneGroundItemLabelCache(DWORD currentTick)
{
    for (auto cacheEntryIterator = groundItemLabels_.cache.begin();
         cacheEntryIterator != groundItemLabels_.cache.end();)
    {
        if (currentTick - cacheEntryIterator->second.LastUsedTick >
            ItemRulesDetail::GROUND_ITEM_LABEL_CACHE_MAX_IDLE_MS)
        {
            cacheEntryIterator = groundItemLabels_.cache.erase(cacheEntryIterator);
        }
        else
        {
            ++cacheEntryIterator;
        }
    }

    if (groundItemLabels_.cache.size() > ItemRulesDetail::GROUND_ITEM_LABEL_CACHE_MAX_ENTRIES)
    {
        const size_t entryCountToEvict =
            groundItemLabels_.cache.size() - ItemRulesDetail::GROUND_ITEM_LABEL_CACHE_MAX_ENTRIES;
        std::vector<decltype(groundItemLabels_.cache.begin())> cacheEntryIterators;
        cacheEntryIterators.reserve(groundItemLabels_.cache.size());

        for (auto cacheEntryIterator = groundItemLabels_.cache.begin();
             cacheEntryIterator != groundItemLabels_.cache.end(); ++cacheEntryIterator)
        {
            cacheEntryIterators.push_back(cacheEntryIterator);
        }

        std::nth_element(cacheEntryIterators.begin(),
                         cacheEntryIterators.begin() + entryCountToEvict, cacheEntryIterators.end(),
                         [](const auto &left, const auto &right) {
                             return left->second.LastUsedTick < right->second.LastUsedTick;
                         });

        for (size_t i = 0; i < entryCountToEvict; ++i)
        {
            groundItemLabels_.cache.erase(cacheEntryIterators[i]);
        }
    }
}

void SessionRenderUnit::BuildGroundItemLabelDescriptor(
    OBJECT *o, ITEM *ip, ItemRulesDetail::GroundItemLabelDescriptor &descriptor)
{
    descriptor.Font = LegacyFontRole::Normal;
    auto ItemLevel = ip->Level;
    auto ItemOption = ip->ExcellentFlags;

    descriptor.Font = LegacyFontRole::Normal;
    descriptor.TextColor = ItemRulesDetail::MakeRgba(255, 255, 255, 255);
    descriptor.BgColor = ItemRulesDetail::MakeRgba(0, 0, 0, 255);

    // Use the item name by default, only when o->Type is in MODEL_ITEM range
    // Items with special types (e.g. MODEL_EVENT + N) are handled by overrides below
    if (o->Type >= MODEL_ITEM && o->Type < MODEL_ITEM + MAX_ITEM)
    {
        if (o->Type == MODEL_ZEN) // Zen
        {
            ItemRulesDetail::FormatGroundItemLabelText(
                descriptor.Name, L"%ls %d", ItemAttribute[o->Type - MODEL_ITEM].Name, ItemLevel);
        }
        else if (ItemLevel == 0)
        {
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name,
                                                     ItemAttribute[o->Type - MODEL_ITEM].Name);
        }
        else
        {
            ItemRulesDetail::FormatGroundItemLabelText(
                descriptor.Name, L"%ls +%d", ItemAttribute[o->Type - MODEL_ITEM].Name, ItemLevel);
        }
    }

    if (ItemRulesDetail::boldTextItems.count(o->Type) > 0)
    {
        descriptor.Font = LegacyFontRole::Bold;
    }

    if (ItemRulesDetail::whiteTextItems.count(o->Type) > 0)
    {
        ItemRulesDetail::SetDescriptorTextColor(descriptor, 1.f, 1.f, 1.f);
    }
    else if (ItemRulesDetail::yellowTextItems.count(o->Type) > 0)
    {
        ItemRulesDetail::SetDescriptorYellowTextColor(descriptor);
    }
    else if (ItemRulesDetail::orangeTextItems.count(o->Type) > 0)
    {
        ItemRulesDetail::SetDescriptorOrangeTextColor(descriptor);
    }
    if (o->Type == MODEL_ORB_OF_SUMMONING)
    {
        ItemRulesDetail::SetDescriptorGrayTextColor(descriptor);
        ItemRulesDetail::FormatGroundItemLabelText(
            descriptor.Name, L"%ls %ls", SkillAttribute[30 + ItemLevel].Name, I18N::Game::Jewel);
    }
    else if (COMGEM::NOGEM != Check_Jewel_Com(o->Type, true))
    {
        int iJewelItemIndex = GetJewelIndex(Check_Jewel_Com(o->Type, true), COMGEM::eGEM_NAME);
        descriptor.Font = LegacyFontRole::Bold;
        ItemRulesDetail::SetDescriptorYellowTextColor(descriptor);
        ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name,
                                                 I18N::Game::Lookup(iJewelItemIndex));
    }
    else if (o->Type == MODEL_COMPILED_CELE)
    {
        ItemRulesDetail::CopyGroundItemLabelText(
            descriptor.Name,
            ItemAttribute[static_cast<int>(MODEL_JEWEL_OF_BLESS) - MODEL_ITEM].Name);
    }
    else if (o->Type == MODEL_COMPILED_SOUL)
    {
        ItemRulesDetail::CopyGroundItemLabelText(
            descriptor.Name,
            ItemAttribute[static_cast<int>(MODEL_JEWEL_OF_SOUL) - MODEL_ITEM].Name);
    }
    else if (o->Type == MODEL_BOX_OF_LUCK && ItemLevel == 7)
    {
        ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::BoxOfHeaven);
    }
    else if (o->Type == MODEL_POTION + 12)
    {
        switch (ItemLevel)
        {
        case 0:
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::Zen);
            break;
        case 1:
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::Heart);
            break;
        case 2:
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name,
                                                     I18N::Game::ChaosEventGiftCertificate);
            break;
        }
    }
    else if (o->Type == MODEL_FRUITS)
    {
        switch (ItemLevel)
        {
        case 0:
            ItemRulesDetail::FormatGroundItemLabelText(descriptor.Name, L"%ls %ls", I18N::Game::ENG,
                                                       ItemAttribute[o->Type - MODEL_ITEM].Name);
            break;
        case 1:
            ItemRulesDetail::FormatGroundItemLabelText(descriptor.Name, L"%ls %ls", I18N::Game::STA,
                                                       ItemAttribute[o->Type - MODEL_ITEM].Name);
            break;
        case 2:
            ItemRulesDetail::FormatGroundItemLabelText(descriptor.Name, L"%ls %ls", I18N::Game::AGI,
                                                       ItemAttribute[o->Type - MODEL_ITEM].Name);
            break;
        case 3:
            ItemRulesDetail::FormatGroundItemLabelText(descriptor.Name, L"%ls %ls", I18N::Game::STR,
                                                       ItemAttribute[o->Type - MODEL_ITEM].Name);
            break;
        case 4:
            ItemRulesDetail::FormatGroundItemLabelText(descriptor.Name, L"%ls %ls",
                                                       I18N::Game::Command,
                                                       ItemAttribute[o->Type - MODEL_ITEM].Name);
            break;
        }
    }
    else if (o->Type == MODEL_SPIRIT)
    {
        switch (ItemLevel)
        {
        case 0:
            ItemRulesDetail::FormatGroundItemLabelText(descriptor.Name, L"%ls of %ls",
                                                       ItemAttribute[o->Type - MODEL_ITEM].Name,
                                                       I18N::Game::DarkHorse);
            break;
        case 1:
            ItemRulesDetail::FormatGroundItemLabelText(descriptor.Name, L"%ls of %ls",
                                                       ItemAttribute[o->Type - MODEL_ITEM].Name,
                                                       I18N::Game::DarkRaven);
            break;
        }
    }
    else if (o->Type == MODEL_EVENT + 16)
    {
        ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::CrestOfMonarch);
    }
    else if (o->Type == MODEL_EVENT + 4)
    {
        ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::StarOfSacredBirth);
    }
    else if (o->Type == MODEL_EVENT + 5)
    {
        switch (ItemLevel)
        {
        case 14:
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::BlueLuckyPouch);
            break;

        case 15:
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::RedLuckyPouch);
            break;

        default:
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::Firecracker);
            break;
        }
    }
    else if (o->Type == MODEL_EVENT + 6)
    {
        if (ItemLevel == 13)
        {
            ItemRulesDetail::SetDescriptorYellowTextColor(descriptor);
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::HeartOfDarkLord);
        }
        else
        {
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::HeartOfLove);
        }
    }
    else if (o->Type == MODEL_EVENT + 7)
    {
        ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::OliveOfLove);
    }
    else if (o->Type == MODEL_EVENT + 8)
    {
        ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::SilverMedal);
    }
    else if (o->Type == MODEL_EVENT + 9)
    {
        ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::GoldMedal);
    }
    else if (o->Type == MODEL_EVENT + 10)
    {
        ItemRulesDetail::FormatGroundItemLabelText(descriptor.Name, L"%ls +%d",
                                                   I18N::Game::BoxOfKundun, ItemLevel - 7);
    }
    else if (o->Type == MODEL_RED_RIBBON_BOX)
    {
        ItemRulesDetail::SetDescriptorTextColor(descriptor, 1.f, 0.3f, 0.3f);
    }
    else if (o->Type == MODEL_GREEN_RIBBON_BOX)
    {
        ItemRulesDetail::SetDescriptorTextColor(descriptor, 0.3f, 1.0f, 0.3f);
    }
    else if (o->Type == MODEL_BLUE_RIBBON_BOX)
    {
        ItemRulesDetail::SetDescriptorTextColor(descriptor, 0.3f, 0.3f, 1.f);
    }
    else if (o->Type == MODEL_PINK_CHOCOLATE_BOX)
    {
        if (ItemLevel == 0)
        {
            ItemRulesDetail::SetDescriptorTextColor(descriptor, 1.f, 0.3f, 1.f);
        }
        else if (ItemLevel == 1)
        {
            ItemRulesDetail::SetDescriptorTextColor(descriptor, 1.f, 0.3f, 1.f);
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::LilacCandyBox);
        }
    }
    else if (o->Type == MODEL_RED_CHOCOLATE_BOX)
    {
        if (ItemLevel == 0)
        {
            ItemRulesDetail::SetDescriptorTextColor(descriptor, 1.0f, 0.3f, 0.3f);
        }
        else if (ItemLevel == 1)
        {
            ItemRulesDetail::SetDescriptorTextColor(descriptor, 1.0f, 0.3f, 0.3f);
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::OrangeCandyBox);
        }
    }
    else if (o->Type == MODEL_BLUE_CHOCOLATE_BOX)
    {
        if (ItemLevel == 0)
        {
            ItemRulesDetail::SetDescriptorTextColor(descriptor, 0.3f, 0.3f, 1.f);
        }
        else if (ItemLevel == 1)
        {
            ItemRulesDetail::SetDescriptorTextColor(descriptor, 0.3f, 0.3f, 1.f);
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::NavyCandyBox);
        }
    }
    else if (o->Type == MODEL_EVENT + 21)
    {
        ItemRulesDetail::SetDescriptorTextColor(descriptor, 1.f, 0.3f, 1.f);
        ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::LilacCandyBox);
    }
    else if (o->Type == MODEL_EVENT + 22)
    {
        ItemRulesDetail::SetDescriptorTextColor(descriptor, 1.0f, 0.3f, 0.3f);
        ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::OrangeCandyBox);
    }
    else if (o->Type == MODEL_EVENT + 23)
    {
        ItemRulesDetail::SetDescriptorTextColor(descriptor, 0.3f, 0.3f, 1.f);
        ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::NavyCandyBox);
    }
    else if (o->Type == MODEL_EVENT + 11)
    {
        ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::Stone);
    }
    else if (o->Type == MODEL_EVENT + 12)
    {
        ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::RingOfHonor);
    }
    else if (o->Type == MODEL_EVENT + 13)
    {
        ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::DarkStone);
    }
    else if (o->Type == MODEL_EVENT + 14)
    {
        switch (ItemLevel)
        {
        case 2:
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::RingOfWarrior);
            break;
        case 3:
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::RingOfGlory);
            break;
        default:
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::RingOfWarrior);
            break;
        }
    }
    else if (o->Type == MODEL_EVENT + 15)
    {
        ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::RingOfWizard);
    }
    else if (o->Type == MODEL_TRANSFORMATION_RING)
    {
        for (int i = 0; i < MAX_MONSTER; i++)
        {
            if (ItemRulesDetail::SommonTable[ItemLevel] == MonsterScript[i].Type)
            {
                ItemRulesDetail::FormatGroundItemLabelText(descriptor.Name, L"%ls %ls",
                                                           MonsterScript[i].Name,
                                                           I18N::Game::TransformationRing);
                break;
            }
        }
    }
    else if (o->Type == MODEL_POTION + 21 && ItemLevel == 3)
    {
        ItemRulesDetail::SetDescriptorYellowTextColor(descriptor);
        ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::SignOfLord);
    }
    else if (o->Type == MODEL_SIEGE_POTION)
    {
        switch (ItemLevel)
        {
        case 0:
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::PotionOfBless);
            break;
        case 1:
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::PotionOfSoul);
            break;
        }
    }
    else if (o->Type == MODEL_HELPER + 7)
    {
        switch (ItemLevel)
        {
        case 0:
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::Archer);
            break;
        case 1:
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::Spearman);
            break;
        }
    }
    else if (o->Type == MODEL_LIFE_STONE_ITEM)
    {
        switch (ItemLevel)
        {
        case 0:
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::ScrollOfGuardian);
            break;
        case 1:
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::PlaceLifeStone);
            break;
        }
    }
    else if (o->Type == MODEL_EVENT + 18)
    {
        ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, I18N::Game::PlaceLifeStone);
    }
    else if ((o->Type >= MODEL_SEED_FIRE && o->Type <= MODEL_SEED_EARTH) ||
             (o->Type >= MODEL_SPHERE_MONO && o->Type <= MODEL_SPHERE_5) ||
             (o->Type >= MODEL_SEED_SPHERE_FIRE_1 && o->Type <= MODEL_SEED_SPHERE_EARTH_5))
    {
        ItemRulesDetail::SetDescriptorTextColor(descriptor, 0.7f, 0.4f, 1.0f);
        ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name,
                                                 ItemAttribute[o->Type - MODEL_ITEM].Name);
    }
    else if (o->Type == MODEL_HELPER + 66)
    {
        ItemRulesDetail::SetDescriptorTextColor(descriptor, 0.6f, 0.4f, 1.0f);
    }
    else if (o->Type >= static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_BEGIN &&
             o->Type <= static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_END)
    {
        ItemRulesDetail::SetDescriptorOrangeTextColor(descriptor);
    }
    else if (o->Type >= MODEL_ITEM && o->Type < MODEL_ITEM + MAX_ITEM &&
             (ItemRulesDetail::whiteTextItems.count(o->Type) > 0 ||
              ItemRulesDetail::yellowTextItems.count(o->Type) > 0 ||
              ItemRulesDetail::orangeTextItems.count(o->Type) > 0))
    {
        // Color was already set by Block 1 (white/yellow/orange). No override needed.
    }
    else
    {
        if (ItemRulesDetail::IsDivineArchangelWeaponModel(o->Type))
        {
            ItemRulesDetail::SetDescriptorTextColor(descriptor, 1.f, 0.1f, 1.f);
        }
        else if (g_SocketItemMgr.IsSocketItem(o))
        {
            ItemRulesDetail::SetDescriptorTextColor(descriptor, 0.7f, 0.4f, 1.0f);
        }
        else if ((ItemOption & 63) > 0 &&
                 (o->Type < MODEL_WINGS_OF_SPIRITS || o->Type > MODEL_WINGS_OF_DARKNESS) &&
                 o->Type != MODEL_CAPE_OF_LORD &&
                 (o->Type < MODEL_WING_OF_STORM || o->Type > MODEL_CAPE_OF_EMPEROR) &&
                 (o->Type < MODEL_WINGS_OF_DESPAIR || o->Type > MODEL_WING_OF_DIMENSION) &&
                 !(o->Type >= MODEL_CAPE_OF_FIGHTER && o->Type <= MODEL_CAPE_OF_OVERRULE))
        {
            ItemRulesDetail::SetDescriptorTextColor(descriptor, 0.1f, 1.f, 0.5f);
        }
        else if (ItemLevel >= 7)
        {
            ItemRulesDetail::SetDescriptorYellowTextColor(descriptor);
        }
        else if (ip->HasSkill || ip->HasLuck || ip->OptionLevel > 0)
        {
            ItemRulesDetail::SetDescriptorTextColor(descriptor, 0.4f, 0.7f, 1.f);
        }
        else if (ItemLevel == 0)
        {
            ItemRulesDetail::SetDescriptorGrayTextColor(descriptor);
        }
        else if (ItemLevel < 3)
        {
            ItemRulesDetail::SetDescriptorTextColor(descriptor, 0.9f, 0.9f, 0.9f);
        }
        else if (ItemLevel < 5)
        {
            ItemRulesDetail::SetDescriptorTextColor(descriptor, 1.f, 0.5f, 0.2f);
        }
        else if (ItemLevel < 7)
        {
            ItemRulesDetail::SetDescriptorTextColor(descriptor, 0.4f, 0.7f, 1.f);
        }

        wchar_t SetName[64]{};
        if (g_csItemOption.GetSetItemName(SetName, o->Type - MODEL_ITEM, ip->AncientDiscriminator))
        {
            ItemRulesDetail::SetDescriptorTextColor(descriptor, 0.f, 1.f, 0.f);
            descriptor.Font = LegacyFontRole::Bold;
            descriptor.TextColor = ItemRulesDetail::MakeRgba(0, 255, 0, 255);
            descriptor.BgColor = ItemRulesDetail::MakeRgba(60, 60, 200, 255);

            wchar_t formattedName[_countof(descriptor.Name)]{};
            ItemRulesDetail::FormatGroundItemLabelText(formattedName, L"%ls%ls", SetName,
                                                       descriptor.Name);
            ItemRulesDetail::CopyGroundItemLabelText(descriptor.Name, formattedName);
        }

        if (ip->HasSkill)
        {
            if (o->Type != MODEL_HORN_OF_DINORANT)
            {
                ItemRulesDetail::AppendGroundItemLabelText(descriptor.Name, L"%ls",
                                                           I18N::Game::Skill);
            }
            else
            {
                ItemRulesDetail::AppendGroundItemLabelText(descriptor.Name, L"%ls", L" +");
                ItemRulesDetail::AppendGroundItemLabelText(descriptor.Name, L"%ls",
                                                           I18N::Game::KnightSpecificSkill);
            }
        }
        if (ip->OptionLevel > 0)
        {
            ItemRulesDetail::AppendGroundItemLabelText(descriptor.Name, L"%ls", I18N::Game::Option);
        }
        if (ip->HasLuck)
        {
            ItemRulesDetail::AppendGroundItemLabelText(descriptor.Name, L"%ls", I18N::Game::Luck);
        }
    }
}

void SessionRenderUnit::ApplyGroundItemLabelDescriptor(
    const ItemRulesDetail::GroundItemLabelDescriptor &descriptor)
{
    g_RenderText.SetFont(descriptor.Font);
    g_RenderText.SetTextColor(descriptor.TextColor);
    g_RenderText.SetBgColor(descriptor.BgColor);
}

bool SessionRenderUnit::CreateGroundItemLabelTexture(
    const ItemRulesDetail::GroundItemLabelDescriptor &descriptor,
    GroundItemLabelCacheEntry &cacheEntry)
{
    (void)cacheEntry;
    int textWidth = 0;
    int textHeight = 0;
    return descriptor.Name[0] != L'\0' &&
           g_RenderText.MeasureText(descriptor.Name, textWidth, textHeight);
}

void SessionRenderUnit::RenderGroundItemLabelTexture(OBJECT *o,
                                                     const GroundItemLabelCacheEntry &cacheEntry)
{
    if (cacheEntry.TextureId == 0)
    {
        return;
    }

    // Match RenderText center behavior: subtract integer half-width to avoid half-pixel blur on odd widths.
    float renderX = static_cast<float>(o->ScreenX) * g_fScreenRate_x -
                    static_cast<float>(cacheEntry.TextWidth / 2);
    float renderY = static_cast<float>(o->ScreenY - 15) * g_fScreenRate_y;

    if (cacheEntry.BgColor != 0)
    {
        EnableAlphaTest();
        glColor4ub(GetRed(cacheEntry.BgColor), GetGreen(cacheEntry.BgColor),
                   GetBlue(cacheEntry.BgColor), GetAlpha(cacheEntry.BgColor));
        RenderColor(renderX / g_fScreenRate_x, renderY / g_fScreenRate_y,
                    static_cast<float>(cacheEntry.TextWidth) / g_fScreenRate_x,
                    static_cast<float>(cacheEntry.TextHeight) / g_fScreenRate_y);
        EndRenderColor();
    }

    glColor4f(1.f, 1.f, 1.f, 1.f);
    float textureUWidth =
        (cacheEntry.TextWidth + 0.01f) / static_cast<float>(cacheEntry.TextureWidth);
    float textureVHeight =
        (cacheEntry.TextHeight + 0.01f) / static_cast<float>(cacheEntry.TextureHeight);
    RenderBitmap(-static_cast<int>(cacheEntry.TextureId), renderX, renderY,
                 static_cast<float>(cacheEntry.TextWidth),
                 static_cast<float>(cacheEntry.TextHeight), 0.f, 0.f, textureUWidth, textureVHeight,
                 false, false);
}

bool SessionRenderUnit::RenderGroundItemLabelCached(OBJECT *o, ITEM *ip)
{
    if (o == nullptr || ip == nullptr)
    {
        return false;
    }
    ItemRulesDetail::GroundItemLabelDescriptor descriptor;
    BuildGroundItemLabelDescriptor(o, ip, descriptor);
    ApplyGroundItemLabelDescriptor(descriptor);
    g_RenderText.RenderText(o->ScreenX, o->ScreenY - 15, descriptor.Name, 0, 0, RT3_WRITE_CENTER);
    return true;
}

void SessionRenderUnit::SetGroundItemLabelBuildBudget(int buildBudget)
{
    groundItemLabels_.buildBudgetRemaining = buildBudget > 0 ? buildBudget : 0;

    constexpr DWORD pruneIntervalMs = 250;
    DWORD currentTick = timeGetTime();

    if (!groundItemLabels_.cache.empty() &&
        (groundItemLabels_.cache.size() > ItemRulesDetail::GROUND_ITEM_LABEL_CACHE_MAX_ENTRIES ||
         groundItemLabels_.lastPruneTick == 0 ||
         currentTick - groundItemLabels_.lastPruneTick >= pruneIntervalMs))
    {
        PruneGroundItemLabelCache(currentTick);
        groundItemLabels_.lastPruneTick = currentTick;
    }
}

void SessionRenderUnit::ReleaseGroundItemLabelCache() noexcept
{
    for (const auto &cacheEntry : groundItemLabels_.cache)
    {
    }
    groundItemLabels_.cache.clear();
    groundItemLabels_.buildBudgetRemaining = 0;
    groundItemLabels_.lastPruneTick = 0;
}

void SessionRenderUnit::RenderItemName(int i, OBJECT *o, ITEM *ip, bool Sort)
{
    (void)i;

    if (!Sort)
    {
        ItemRulesDetail::GroundItemLabelDescriptor descriptor;
        BuildGroundItemLabelDescriptor(o, ip, descriptor);
        ApplyGroundItemLabelDescriptor(descriptor);
        g_RenderText.RenderText(MouseX, MouseY - 15, descriptor.Name, 0, 0, RT3_WRITE_CENTER);
    }
    else
    {
        RenderGroundItemLabelCached(o, ip);
    }

    g_RenderText.SetTextColor(255, 230, 200, 255);
    g_RenderText.SetBgColor(0, 0, 0, 255);
}
void SessionRenderUnit::InventoryColor(ITEM *p)
{
    switch (p->Color)
    {
    case 0:
        glColor3f(1.f, 1.f, 1.f);
        break;
    case 1:
        glColor3f(0.8f, 0.8f, 0.8f);
        break;
    case 2:
        glColor3f(0.6f, 0.7f, 1.f);
        break;
    case 3:
        glColor3f(1.f, 0.2f, 0.1f);
        break;
    case 4:
        glColor3f(0.5f, 1.f, 0.6f);
        break;
    case 5:
        glColor4f(0.8f, 0.7f, 0.f, 1.f);
        break;
    case 6:
        glColor4f(0.8f, 0.5f, 0.f, 1.f);
        break;
    case 7:
        glColor4f(0.8f, 0.3f, 0.3f, 1.f);
        break;
    case 8:
        glColor4f(1.0f, 0.f, 0.f, 1.f);
        break;
    case 99:
        glColor3f(1.f, 0.2f, 0.1f);
        break;
    }
}

void SessionRenderUnit::HideKeyPad()
{
    g_iKeyPadEnable = 0;
}

int SessionRenderUnit::CheckMouseOnKeyPad()
{
    int Width, Height, WindowX, WindowY;
    Width = 213;
    Height = 2 * 5 + 6 * 40;
    WindowX = (REFERENCE_WIDTH - Width) / 2;
    WindowY = 60 + 40; //60 220

    int iButtonTop = 50;

    for (int i = 0; i < 11; ++i)
    {
        int xButton = i % 5;
        int yButton = i / 5;

        int xLeft = WindowX + 10 + xButton * 40;
        int yTop = WindowY + iButtonTop + yButton * 40;
        if (xLeft <= MouseX && MouseX < xLeft + 32 && yTop <= MouseY && MouseY < yTop + 32)
        {
            return (i);
        }
    }
    // Ok, Cancel ( 11 - 12)
    int yTop = WindowY + iButtonTop + 2 * 40 + 5;

    for (int i = 0; i < 2; ++i)
    {
        int xLeft = WindowX + 52 + i * 78;
        if (xLeft <= MouseX && MouseX < xLeft + 70 && yTop <= MouseY && MouseY < yTop + 21)
        {
            return (11 + i);
        }
    }

    return (-1);
}

void SessionRenderUnit::RenderInventoryInterface(int StartX, int StartY, int Flag)
{
    float x, y, Width, Height;
    Width = 190.f;
    Height = 256.f;
    x = (float)StartX;
    y = (float)StartY;

    RenderBitmap(BITMAP_INVENTORY, x, y, Width, Height, 0.f, 0.f, Width / 256.f, Height / 256.f);

    Width = 190.f;
    Height = 177.f;
    x = (float)StartX;
    y = (float)StartY + 256;
    RenderBitmap(BITMAP_INVENTORY + 1, x, y, Width, Height, 0.f, 0.f, Width / 256.f,
                 Height / 256.f);

    if (Flag)
    {
        Width = 190.f;
        Height = 10.f;
        x = (float)StartX;
        y = (float)StartY + 225;
        RenderBitmap(BITMAP_INVENTORY + 19, x, y, Width, Height, 0.f, 0.f, Width / 256.f,
                     Height / 16.f);
    }
}
bool SessionRenderUnit::CreateGuildMark(int nMarkIndex, bool blend)
{
    const auto *texture = g_GuildCache.MarkTexture(nMarkIndex, blend);
    if (!texture)
        return false;
    sessionKeeper_.TextureNamespace().SelectPreparedTexture(BITMAP_GUILD, *texture);
    return true;
}

void SessionRenderUnit::CreateCastleMark(int Type, BYTE *buffer, bool blend)
{
    if (buffer == NULL)
        return;

    const SessionBitmapMetadata b = Bitmaps[Type];
    if (!IsValid(b.Asset))
        return;

    int Width, Height;

    Width = (int)b.Width;
    Height = (int)b.Height;
    std::vector<std::byte> pixels(static_cast<std::size_t>(Width) * Height * 4);
    std::byte *Buffer = pixels.data();

    int alpha = 128;
    if (blend)
    {
        alpha = 0;
    }

    for (int i = 0; i < 16; i++)
    {
        switch (i)
        {
        case 0:
            MarkColor[i] = (alpha << 24) + (0 << 16) + (0 << 8) + (0);
            break;
        case 1:
            MarkColor[i] = (255 << 24) + (0 << 16) + (0 << 8) + (0);
            break;
        case 2:
            MarkColor[i] = (255 << 24) + (128 << 16) + (128 << 8) + (128);
            break;
        case 3:
            MarkColor[i] = (255 << 24) + (255 << 16) + (255 << 8) + (255);
            break;
        case 4:
            MarkColor[i] = (255 << 24) + (0 << 16) + (0 << 8) + (255);
            break; //?
        case 5:
            MarkColor[i] = (255 << 24) + (0 << 16) + (128 << 8) + (255);
            break; //
        case 6:
            MarkColor[i] = (255 << 24) + (0 << 16) + (255 << 8) + (255);
            break; //?
        case 7:
            MarkColor[i] = (255 << 24) + (0 << 16) + (255 << 8) + (128);
            break; //
        case 8:
            MarkColor[i] = (255 << 24) + (0 << 16) + (255 << 8) + (0);
            break; //?
        case 9:
            MarkColor[i] = (255 << 24) + (128 << 16) + (255 << 8) + (0);
            break; //
        case 10:
            MarkColor[i] = (255 << 24) + (255 << 16) + (255 << 8) + (0);
            break; //?
        case 11:
            MarkColor[i] = (255 << 24) + (255 << 16) + (128 << 8) + (0);
            break; //
        case 12:
            MarkColor[i] = (255 << 24) + (255 << 16) + (0 << 8) + (0);
            break; //?
        case 13:
            MarkColor[i] = (255 << 24) + (255 << 16) + (0 << 8) + (128);
            break; //
        case 14:
            MarkColor[i] = (255 << 24) + (255 << 16) + (0 << 8) + (255);
            break; //?
        case 15:
            MarkColor[i] = (255 << 24) + (128 << 16) + (0 << 8) + (255);
            break; //
        }
    }
    BYTE MarkBuffer[32 * 32];

    int offset = 0;

    for (int i = 0; i < 32; ++i)
    {
        for (int j = 0; j < 32; ++j)
        {
            offset = (j / 4) + ((i / 4) * 8);
            MarkBuffer[j + (i * 32)] = buffer[offset];
        }
    }

    offset = 0;
    int offset2 = 0;

    for (int i = 0; i < Height; ++i)
    {
        for (int j = 0; j < Width; ++j)
        {
            if (j >= (Width / 2 - 16) && j < (Width / 2 + 16) && i >= (Height / 2 - 16) &&
                i < (Height / 2 + 16))
            {
                *((unsigned int *)(Buffer + offset)) = MarkColor[MarkBuffer[offset2]];
                offset2++;
            }
            else if (j < 3 || j > (Width - 4) || i < 10 || i > (Height - 10))
            {
                *((unsigned int *)(Buffer + offset)) = (255 << 24) + (0 << 16) +
                                                       ((int)(50 + i / 100.f * 160) << 8) +
                                                       (50 + i / 100.f * 255);
            }
            else
            {
                *((unsigned int *)(Buffer + offset)) = (255 << 24) + (i << 16) + (i << 8) + (i);
            }
            offset += 4;
        }
    }
    ItemRulesDetail::PublishBitmapRevision(Bitmaps, static_cast<std::uint32_t>(Type), b.Asset,
                                           static_cast<std::uint32_t>(Width),
                                           static_cast<std::uint32_t>(Height), pixels);
}

void SessionRenderUnit::RenderGuildColor(float x, float y, int SizeX, int SizeY, int Index)
{
    RenderBitmap(BITMAP_INVENTORY + 18, x - 1, y - 1, (float)SizeX + 2, (float)SizeY + 2, 0.f, 0.f,
                 SizeX / 32.f, SizeY / 30.f);

    const SessionBitmapMetadata b = Bitmaps[BITMAP_GUILD];
    if (!IsValid(b.Asset))
        return;

    int Width, Height;

    Width = (int)b.Width;
    Height = (int)b.Height;
    std::vector<std::byte> pixels(static_cast<std::size_t>(Width) * Height * 4);
    std::byte *Buffer = pixels.data();
    unsigned int Color = MarkColor[Index];

    if (Index == 0)
    {
        for (int i = 0; i < Height; i++)
        {
            for (int j = 0; j < Width; j++)
            {
                *((unsigned int *)(Buffer)) = 255 << 24;
                Buffer += 4;
            }
        }
        Color = (255 << 24) + (128 << 16) + (128 << 8) + (128);
        Buffer = pixels.data();
        for (int i = 0; i < 8; i++)
        {
            *((unsigned int *)(Buffer)) = Color;
            Buffer += 8 * 4 + 4;
        }
        Buffer = pixels.data() + 7 * 4;
        for (int i = 0; i < 8; i++)
        {
            *((unsigned int *)(Buffer)) = Color;
            Buffer += 8 * 4 - 4;
        }
    }
    else
    {
        for (int i = 0; i < Height; i++)
        {
            for (int j = 0; j < Width; j++)
            {
                *((unsigned int *)(Buffer)) = Color;
                Buffer += 4;
            }
        }
    }

    ItemRulesDetail::PublishBitmapRevision(Bitmaps, BITMAP_GUILD, b.Asset,
                                           static_cast<std::uint32_t>(Width),
                                           static_cast<std::uint32_t>(Height), pixels);
    RenderBitmap(BITMAP_GUILD, x, y, (float)SizeX, (float)SizeY);
}

void SessionRenderUnit::RenderGuildList(int StartX, int StartY)
{
    GuildListStartX = StartX;
    GuildListStartY = StartY;

    glColor3f(1.f, 1.f, 1.f);

    DisableAlphaBlend();
    float x, y, Width, Height;
    Width = 190.f;
    Height = 256.f;
    x = (float)StartX;
    y = (float)StartY;
    RenderBitmap(BITMAP_INVENTORY, x, y, Width, Height, 0.f, 0.f, Width / 256.f, Height / 256.f);
    Width = 190.f;
    Height = 177.f;
    x = (float)StartX;
    y = (float)StartY + 256;
    RenderBitmap(BITMAP_INVENTORY + 1, x, y, Width, Height, 0.f, 0.f, Width / 256.f,
                 Height / 256.f);

    EnableAlphaTest();

    g_RenderText.SetBgColor(20, 20, 20, 255);
    g_RenderText.SetTextColor(220, 220, 220, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);

    wchar_t Text[100];
    if (Hero->GuildMarkIndex == -1)
        mu_swprintf(Text, I18N::Game::Guild);
    else
        mu_swprintf(Text, L"%ls (Score:%d)", GuildMark[Hero->GuildMarkIndex].GuildName,
                    GuildTotalScore);

    g_RenderText.RenderText(StartX + 95 - 60, StartY + 12, Text,
                            120 * WindowWidth / REFERENCE_WIDTH, true, 3);

    g_RenderText.SetBgColor(0);
    g_RenderText.SetTextColor(230, 230, 230, 255);
    g_RenderText.SetFont(LegacyFontRole::Normal);

    if (g_nGuildMemberCount == 0)
    {
        g_RenderText.RenderText(StartX + 20, StartY + 50, I18N::Game::TypeGuildInFrontOf);
        g_RenderText.RenderText(StartX + 20, StartY + 65, I18N::Game::TheGuildMasterYouWantToJoin);
        g_RenderText.RenderText(StartX + 20, StartY + 80, I18N::Game::AndYouCanJoinTheGuild);
    }
    g_RenderText.SetBgColor(0, 0, 0, 128);
    g_RenderText.SetTextColor(100, 255, 200, 255);
    g_RenderText.RenderText(StartX + (int)Width / 2, StartY + 44, g_GuildNotice[0], 0, 0,
                            RT3_WRITE_CENTER);
    g_RenderText.RenderText(StartX + (int)Width / 2, StartY + 58, g_GuildNotice[1], 0, 0,
                            RT3_WRITE_CENTER);

    int yGuildStart = 72;
    int Number = g_nGuildMemberCount;

    if (g_nGuildMemberCount >= MAX_GUILD_LINE)
        Number = MAX_GUILD_LINE;
}

//#define MAX_LENGTH_CMB	( 26)
#define NUM_LINE_CMB (7)

void SessionRenderUnit::RenderServerDivision()
{
    if (!g_pUIManager->IsOpen(::INTERFACE_SERVERDIVISION))
        return;

    float Width, Height, x, y;

    glColor3f(1.f, 1.f, 1.f);
    EnableAlphaTest();

    InventoryStartX = REFERENCE_WIDTH - 190;
    InventoryStartY = 0;
    Width = 213;
    Height = 40;
    x = (float)InventoryStartX;
    y = (float)InventoryStartY;
    RenderInventoryInterface((int)x, (int)y, 1);

    g_RenderText.SetBgColor(0);
    g_RenderText.SetTextColor(255, 230, 210, 255);

    g_RenderText.SetFont(LegacyFontRole::Bold);
    x = InventoryStartX + (190 / 2.f);
    y = 50;
    for (int i = 462; i < 470; ++i)
    {
        g_RenderText.RenderText(x, y, I18N::Game::Lookup(i), 0, 0, RT3_WRITE_CENTER);
        y += 20;
    }

    g_RenderText.SetFont(LegacyFontRole::Bold);
    Width = 16;
    Height = 16;
    x = (float)InventoryStartX + 25;
    y = 240;
    if (g_bServerDivisionAccept)
    {
        g_RenderText.SetTextColor(212, 150, 0, 255);
        RenderBitmap(BITMAP_INVENTORY_BUTTON + 11, x, y, Width, Height, 0.f, 0.f, 24 / 32.f,
                     24 / 32.f);
    }
    else
    {
        g_RenderText.SetTextColor(223, 191, 103, 255);
        RenderBitmap(BITMAP_INVENTORY_BUTTON + 10, x, y, Width, Height, 0.f, 0.f, 24 / 32.f,
                     24 / 32.f);
    }
    g_RenderText.RenderText((int)(x + Width + 3), (int)(y + 5),
                            I18N::Game::AgreeWithTheAboveAgreement);
    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.SetTextColor(255, 230, 210, 255);

    Width = 120;
    Height = 24;
    x = (float)InventoryStartX + 35;
    y = 350; //(Width/2.f); y = 231;
    RenderBitmap(BITMAP_INTERFACE + 10, (float)x, (float)y, (float)Width, (float)Height, 0.f, 0.f,
                 213.f / 256.f);
    g_RenderText.RenderText((int)(x + (Width / 2)), (int)(y + 5), I18N::Game::Cancel, 0, 0,
                            RT3_WRITE_CENTER);

    Width = 120;
    Height = 24;
    x = (float)InventoryStartX + 35;
    y = 320; //(Width/2.f); y = 231;
    if (g_bServerDivisionAccept)
        glColor3f(1.f, 1.f, 1.f);
    else
        glColor3f(0.5f, 0.5f, 0.5f);
    RenderBitmap(BITMAP_INTERFACE + 10, (float)x, (float)y, (float)Width, (float)Height, 0.f, 0.f,
                 213.f / 256.f);
    g_RenderText.RenderText((int)(x + (Width / 2)), (int)(y + 5), I18N::Game::OK, 0, 0,
                            RT3_WRITE_CENTER);

    glColor3f(1.f, 1.f, 1.f);
}

// OMF-00541
// OMF-00542

/*+++++++++++++++++++++++++++++++++++++
    INCLUDE.
+++++++++++++++++++++++++++++++++++++*/

int CSItemOption::RenderDefaultOptionText(const ITEM *ip, int TextNum)
{
    int TNum = TextNum;
    if (GetDefaultOptionText(ip, TextList[TNum]))
    {
        TextListColor[TNum] = TEXT_COLOR_BLUE;
        TNum++;

        if ((ip->Type >= ITEM_RING_OF_ICE && ip->Type <= ITEM_RING_OF_POISON) ||
            (ip->Type >= ITEM_PENDANT_OF_LIGHTING && ip->Type <= ITEM_PENDANT_OF_FIRE) ||
            (ip->Type >= ITEM_RING_OF_FIRE && ip->Type <= ITEM_PENDANT_OF_WATER))
        {
            mu_swprintf(TextList[TNum],
                        I18N::Game::IncreaseAttributeDamage); // "Increase Attribute Damage"
            TextListColor[TNum] = TEXT_COLOR_BLUE;
            TNum++;
        }
    }

    return TNum;
}

int CSItemOption::RenderSetOptionListInItem(const ITEM *ip, int TextNum, bool bIsEquippedItem)
{
    if (ip->AncientDiscriminator == 0)
    {
        return TextNum;
    }

    const ITEM_SET_TYPE &itemSType = m_ItemSetType[ip->Type];

    m_bySelectedItemOption = itemSType.byOption[ip->AncientDiscriminator - 1];

    if (m_bySelectedItemOption <= 0 || m_bySelectedItemOption == 255)
        return TextNum;

    int TNum = TextNum;

    const ITEM_SET_OPTION &setOption = m_ItemSetOption[m_bySelectedItemOption];
    if (setOption.byOptionCount >= 255)
    {
        m_bySelectedItemOption = 0;
        return TNum;
    }

    mu_swprintf(TextList[TNum], L"\n");
    TNum++;
    mu_swprintf(TextList[TNum], L"%ls %ls", I18N::Game::Set, I18N::Game::ItemOptionInfo);
    TextListColor[TNum] = TEXT_COLOR_YELLOW;
    TNum++;

    mu_swprintf(TextList[TNum], L"\n");
    TNum++;
    mu_swprintf(TextList[TNum], L"\n");
    TNum++;

    for (int i = 0; i < m_SetSearchResultCount; i++)
    {
        const auto &set = m_SetSearchResult[i];
        if (wcscmp(set.SetName, setOption.strSetName) == 0)
        {
            // Set Found.
            TNum = RenderSetOptionList(set, TNum, bIsEquippedItem, true);
            break;
        }
    }

    mu_swprintf(TextList[TNum], L"\n");
    TNum++;
    mu_swprintf(TextList[TNum], L"\n");
    TNum++;

    return TNum;
}

std::uint8_t CSItemOption::RenderSetOptionList(const SET_SEARCH_RESULT_OPT &set,
                                               std::uint8_t textIndex, bool bIsEquippedItem,
                                               bool bShowInactive)
{
    for (int j = 0; j < set.SetOptionCount; j++)
    {
        const auto option = set.SetOption[j];
        if (!bShowInactive && !option.IsActive)
        {
            break;
        }

        if (getExplainText(TextList[textIndex], option.OptionNumber, option.Value))
        {
            if (!bIsEquippedItem || !option.IsActive)
            {
                TextListColor[textIndex] = TEXT_COLOR_GRAY;
            }
            else if (option.OptionNumber >= AT_SET_OPTION_IMPROVE_ATTACK_1 &&
                     !option.FulfillsClassRequirement)
            {
                // Mastery
                TextListColor[textIndex] = TEXT_COLOR_RED;
            }
            else
            {
                TextListColor[textIndex] = option.IsFullOption  ? TEXT_COLOR_YELLOW
                                           : option.IsExtOption ? TEXT_COLOR_GREEN
                                                                : TEXT_COLOR_BLUE;
            }

            TextBold[textIndex] = false;
            textIndex++;
        }
    }

    mu_swprintf(TextList[textIndex], L"\n");
    TextListColor[textIndex] = 0;
    TextBold[textIndex] = false;
    textIndex++;

    return textIndex;
}

namespace ItemRulesDetail
{
#pragma pack(push)
#pragma pack()
UI::Modern::RmlTooltipColor TooltipColor(int legacyColor) noexcept
{
    using Color = UI::Modern::RmlTooltipColor;
    switch (legacyColor)
    {
    case TEXT_COLOR_BLUE:
        return Color::Blue;
    case TEXT_COLOR_GRAY:
        return Color::Gray;
    case TEXT_COLOR_RED:
        return Color::Red;
    case TEXT_COLOR_YELLOW:
        return Color::Yellow;
    case TEXT_COLOR_GREEN:
        return Color::Green;
    case TEXT_COLOR_PURPLE:
        return Color::Purple;
    case TEXT_COLOR_REDPURPLE:
        return Color::RedPurple;
    case TEXT_COLOR_VIOLET:
        return Color::Violet;
    case TEXT_COLOR_ORANGE:
        return Color::Orange;
    default:
        return Color::White;
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
UI::Modern::RmlTooltipBackground TooltipBackground(int legacyColor) noexcept
{
    using Background = UI::Modern::RmlTooltipBackground;
    switch (legacyColor)
    {
    case TEXT_COLOR_DARKRED:
        return Background::DarkRed;
    case TEXT_COLOR_DARKBLUE:
        return Background::DarkBlue;
    case TEXT_COLOR_DARKYELLOW:
        return Background::DarkYellow;
    case TEXT_COLOR_GREEN_BLUE:
        return Background::GreenBlue;
    default:
        return Background::None;
    }
}
#pragma pack(pop)

} // namespace ItemRulesDetail

void CNewUIMyInventory::UI2DEffectCallback(LPVOID pClass, DWORD dwParamA, DWORD dwParamB)
{
    if (pClass)
    {
        auto *pMyInventory = (CNewUIMyInventory *)(pClass);

        if (dwParamB == ITEM_SET_OPTION)
        {
            pMyInventory->RenderSetOptionList();
        }
        else if (dwParamB == ITEM_SOCKET_SET_OPTION)
        {
            pMyInventory->g_RenderText.SetTextColor(255, 255, 255, 255);
            pMyInventory->g_RenderText.SetBgColor(100, 0, 0, 0);
            const auto button =
                pMyInventory->m_ModernPanel.ButtonRect(InventoryPanel::SocketOption);
            pMyInventory->renderUnit_.RenderToolTipForSocketSetOption(
                static_cast<int>(button.x + button.width / 2), static_cast<int>(button.y));
        }
        else
        {
            pMyInventory->RenderItemToolTip(dwParamA);
        }
    }
}

void SEASON3B::CNewUIInventoryCtrl::UI2DEffectCallback(LPVOID pClass, DWORD dwParamA,
                                                       DWORD dwParamB)
{
    if (pClass)
    {
        auto *pInventoryCtrl = static_cast<CNewUIInventoryCtrl *>(pClass);
        if (dwParamA == RENDER_NUMBER_OF_ITEM)
            pInventoryCtrl->RenderNumberOfItem();
        else if (dwParamA == RENDER_ITEM_TOOLTIP)
            pInventoryCtrl->RenderItemToolTip();
    }
}
