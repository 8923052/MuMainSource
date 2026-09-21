#include "ui/features/Dialogs/DialogsRender.h"
#include "I18N/All.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "data/GameData.h"
#include "data/Localization.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Quests.h"
#include "domain/Shop.h"
#include "domain/WorldSimulation.h"
#include "render/FrameTape.h"
#include "render/ModelResources.h"
#include "render/Sprites.h"
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
#include "ui/features/Shell/ShellLogic.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"
#include "ui/session/UiSessionRender.h"

namespace UI::Modern
{
namespace
{
bool SetText(Rml::Element &element, std::wstring &current, const std::wstring &value)
{
    if (current == value)
        return false;
    current = value;
    element.SetInnerRML(Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(value.c_str())));
    return true;
}
} // namespace

class RmlContextMenuPanel::Impl final
{
  public:
    Impl(SessionKeeper &keeper, const char *group, const char *document, const char *contextName)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, group, document)),
          design_(path_, {"Menu-Size", "Menu-Reference", "Menu-Anchor"}),
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
        for (auto &button : buttons_)
            button.Unbind();
        panel_ = title_ = nullptr;
        labels_.clear();
        buttons_.clear();
        content_ = {};
        command_ = -1;
        dismiss_ = dirty_ = visible_ = false;
        left_ = top_ = width_ = height_ = 0;
        host_.Release();
    }
    bool Bind()
    {
        auto *document = host_.Document();
        panel_ = document->GetElementById("menu");
        title_ = document->GetElementById("menu-title");
        if (!panel_)
            return false;
        Rml::ElementList elements;
        document->GetElementsByClassName(elements, "mu-button");
        buttons_.resize(elements.size());
        labels_.resize(elements.size());
        content_.labels.resize(elements.size());
        for (auto *element : elements)
        {
            const int index = element->GetAttribute<int>("data-command", -1);
            if (index < 0 || index >= static_cast<int>(elements.size()))
                return false;
            labels_[index] = element->GetChild(0);
            if (!labels_[index])
                return false;
            buttons_[index].Bind(*element);
        }
        dirty_ = true;
        return true;
    }
    void Position(const Content &content)
    {
        const auto size = design_.Values(0);
        const auto reference = design_.Values(1);
        const auto viewport = host_.Viewport();
        const auto anchor = design_.Values(2);
        const float x = anchor[0] ? viewport.width - size[0] - anchor[1]
                                  : content.x * viewport.width / reference[0];
        const float y = anchor[0] ? viewport.height - size[1] - anchor[2]
                                  : content.y * viewport.height / reference[1];
        const float left = std::clamp(x, 0.0f, std::max(0.0f, viewport.width - size[0]));
        const float top = std::clamp(y, 0.0f, std::max(0.0f, viewport.height - size[1]));
        if (panel_->GetProperty<float>("left") != left || panel_->GetProperty<float>("top") != top)
        {
            panel_->SetProperty(Rml::PropertyId::Left, Rml::Property(left, Rml::Unit::PX));
            panel_->SetProperty(Rml::PropertyId::Top, Rml::Property(top, Rml::Unit::PX));
            dirty_ = true;
        }
        left_ = left * reference[0] / viewport.width;
        top_ = top * reference[1] / viewport.height;
        width_ = size[0] * reference[0] / viewport.width;
        height_ = size[1] * reference[1] / viewport.height;
    }
    bool Prepare(int width, int height, const Content &content)
    {
        if (!panel_ && !content.visible)
            return true;
        const auto size = design_.Values(0);
        if (!host_.Ensure(width, height, size[0], size[1]))
            return false;
        if (!panel_ && !Bind())
        {
            Release();
            return false;
        }
        if (!host_.SetVisible(content.visible))
            return false;
        dismissOnOutsideRelease_ = content.dismissOnOutsideRelease;
        if (visible_ != content.visible)
        {
            visible_ = content.visible;
            command_ = -1;
            dismiss_ = false;
            for (auto &button : buttons_)
                button.Reset();
            dirty_ = true;
        }
        if (visible_)
        {
            Position(content);
            if (title_)
                dirty_ = SetText(*title_, content_.title, content.title) || dirty_;
            for (std::size_t i = 0; i < buttons_.size(); ++i)
            {
                dirty_ = SetText(*labels_[i], content_.labels[i], content.labels[i]) || dirty_;
                dirty_ = buttons_[i].SyncVisualState() || dirty_;
            }
        }
        if (!host_.CaptureIfDirty(dirty_))
            return false;
        dirty_ = false;
        return true;
    }
    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!visible_)
            return false;
        host_.ProcessInput(event);
        bool inside = false;
        for (auto *element = host_.HoverElement(); element; element = element->GetParentNode())
        {
            if (element == panel_)
            {
                inside = true;
                break;
            }
        }
        for (std::size_t i = 0; i < buttons_.size(); ++i)
            if (buttons_[i].IsClick())
                command_ = static_cast<int>(i);
        dirty_ = true;
        if (event.kind != SessionInputEventKind::Pointer)
            return false;
        if (!inside && dismissOnOutsideRelease_ &&
            event.action == SessionInputAction::PointerButton && !event.pressed)
        {
            dismiss_ = true;
            return true;
        }
        return inside;
    }
    std::filesystem::path path_;
    RmlUiDesign design_;
    RmlDocumentHost host_;
    Rml::Element *panel_ = nullptr;
    Rml::Element *title_ = nullptr;
    std::vector<Rml::Element *> labels_;
    std::deque<RmlMuButton> buttons_;
    Content content_;
    float left_ = 0, top_ = 0, width_ = 0, height_ = 0;
    bool visible_ = false, dirty_ = false, dismiss_ = false;
    bool dismissOnOutsideRelease_ = true;
    int command_ = -1;
};

RmlContextMenuPanel::RmlContextMenuPanel(SessionKeeper &keeper, const char *group,
                                         const char *document, const char *contextName)
    : impl_(std::make_unique<Impl>(keeper, group, document, contextName))
{
}
RmlContextMenuPanel::~RmlContextMenuPanel() = default;
void RmlContextMenuPanel::Release()
{
    impl_->Release();
}
bool RmlContextMenuPanel::PrepareOnWorker(int width, int height, const Content &content)
{
    return impl_->Prepare(width, height, content);
}
bool RmlContextMenuPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
int RmlContextMenuPanel::TakeCommand()
{
    return std::exchange(impl_->command_, -1);
}
bool RmlContextMenuPanel::TakeDismiss()
{
    return std::exchange(impl_->dismiss_, false);
}
bool RmlContextMenuPanel::ContainsReferencePointer(int x, int y) const
{
    return impl_->visible_ && x >= impl_->left_ && y >= impl_->top_ &&
           x < impl_->left_ + impl_->width_ && y < impl_->top_ + impl_->height_;
}
bool RmlContextMenuPanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->panel_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern

namespace UI::Modern
{
namespace
{
// Authored design inputs.
enum class DesignKey
{
    PanelWidth,
    PanelHeight,
    PanelButtonWidth,
    PanelButtonHeight,
    PanelSingleButtonX,
    PanelOkButtonX,
    PanelCancelButtonX,
    PanelButtonY,
    PanelCautionWidth,
    PanelCautionHeight,
    PanelCautionButtonWidth,
    PanelCautionButtonHeight,
    PanelCautionSingleButtonX,
    PanelCautionOkButtonX,
    PanelCautionCancelButtonX,
    PanelCautionBottomY,
    PanelCautionMessageHeight,
    PasswordMaxLength,
    NumberMaxLength,
    TextMaxLength
};

const RmlUiDesign &Design()
{
    static const RmlUiDesign design("Data/UI/PC/Common/message_box.rml",
                                    {"RmlMessageBoxPanel-PanelWidth",
                                     "RmlMessageBoxPanel-PanelHeight",
                                     "RmlMessageBoxPanel-PanelButtonWidth",
                                     "RmlMessageBoxPanel-PanelButtonHeight",
                                     "RmlMessageBoxPanel-PanelSingleButtonX",
                                     "RmlMessageBoxPanel-PanelOkButtonX",
                                     "RmlMessageBoxPanel-PanelCancelButtonX",
                                     "RmlMessageBoxPanel-PanelButtonY",
                                     "RmlMessageBoxPanel-PanelCautionWidth",
                                     "RmlMessageBoxPanel-PanelCautionHeight",
                                     "RmlMessageBoxPanel-PanelCautionButtonWidth",
                                     "RmlMessageBoxPanel-PanelCautionButtonHeight",
                                     "RmlMessageBoxPanel-PanelCautionSingleButtonX",
                                     "RmlMessageBoxPanel-PanelCautionOkButtonX",
                                     "RmlMessageBoxPanel-PanelCautionCancelButtonX",
                                     "RmlMessageBoxPanel-PanelCautionBottomY",
                                     "RmlMessageBoxPanel-PanelCautionMessageHeight",
                                     "RmlMessageBoxPanel-PasswordMaxLength",
                                     "RmlMessageBoxPanel-NumberMaxLength",
                                     "RmlMessageBoxPanel-TextMaxLength"});
    return design;
}
// End authored design inputs.

Rml::String EncodeMessage(const std::wstring &text)
{
    Rml::String encoded = Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(text.c_str()));
    encoded = Rml::StringUtilities::Replace(std::move(encoded), "\n",
                                            "</div><div class=\"message-line\">");
    return "<div class=\"message-line\">" + encoded + "</div>";
}
} // namespace

std::wstring SanitizeRmlMessageBoxNumber(std::wstring_view value)
{
    std::wstring result(value);
    std::erase_if(result, [](wchar_t character) { return character < L'0' || character > L'9'; });
    return result;
}

int RmlMessageBoxPanel::Width() noexcept
{
    return Design().Number<int>(DesignKey::PanelWidth);
}

int RmlMessageBoxPanel::Height() noexcept
{
    return Design().Number<int>(DesignKey::PanelHeight);
}

int RmlMessageBoxPanel::ButtonWidth() noexcept
{
    return Design().Number<int>(DesignKey::PanelButtonWidth);
}

int RmlMessageBoxPanel::ButtonHeight() noexcept
{
    return Design().Number<int>(DesignKey::PanelButtonHeight);
}

int RmlMessageBoxPanel::SingleButtonX() noexcept
{
    return Design().Number<int>(DesignKey::PanelSingleButtonX);
}

int RmlMessageBoxPanel::OkButtonX() noexcept
{
    return Design().Number<int>(DesignKey::PanelOkButtonX);
}

int RmlMessageBoxPanel::CancelButtonX() noexcept
{
    return Design().Number<int>(DesignKey::PanelCancelButtonX);
}

int RmlMessageBoxPanel::ButtonY() noexcept
{
    return Design().Number<int>(DesignKey::PanelButtonY);
}

int RmlMessageBoxPanel::CautionWidth() noexcept
{
    return Design().Number<int>(DesignKey::PanelCautionWidth);
}

int RmlMessageBoxPanel::CautionHeight() noexcept
{
    return Design().Number<int>(DesignKey::PanelCautionHeight);
}

int RmlMessageBoxPanel::CautionButtonWidth() noexcept
{
    return Design().Number<int>(DesignKey::PanelCautionButtonWidth);
}

int RmlMessageBoxPanel::CautionButtonHeight() noexcept
{
    return Design().Number<int>(DesignKey::PanelCautionButtonHeight);
}

int RmlMessageBoxPanel::CautionSingleButtonX() noexcept
{
    return Design().Number<int>(DesignKey::PanelCautionSingleButtonX);
}

int RmlMessageBoxPanel::CautionOkButtonX() noexcept
{
    return Design().Number<int>(DesignKey::PanelCautionOkButtonX);
}

int RmlMessageBoxPanel::CautionCancelButtonX() noexcept
{
    return Design().Number<int>(DesignKey::PanelCautionCancelButtonX);
}

int RmlMessageBoxPanel::CautionBottomY() noexcept
{
    return Design().Number<int>(DesignKey::PanelCautionBottomY);
}

int RmlMessageBoxPanel::CautionMessageHeight() noexcept
{
    return Design().Number<int>(DesignKey::PanelCautionMessageHeight);
}

class RmlMessageBoxPanel::Impl final
{
  public:
    Impl(SessionKeeper &keeper, const RmlMessageBoxPanel &owner, RmlMuButton &okButton,
         RmlMuButton &cancelButton)
        : okButton_(okButton), cancelButton_(cancelButton),
          host_(keeper,
                "message-box-" + std::to_string(keeper.Id().RawValue()) + "-" +
                    std::to_string(reinterpret_cast<std::uintptr_t>(&owner)),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "Common", "message_box.rml"))
    {
    }

    ~Impl()
    {
        Release();
    }

    void Release()
    {
        okButton_.Unbind();
        cancelButton_.Unbind();
        panel_ = nullptr;
        title_ = nullptr;
        divider_ = nullptr;
        message_ = nullptr;
        messageContent_ = nullptr;
        bottom_ = nullptr;
        input_ = nullptr;
        ok_ = nullptr;
        cancel_ = nullptr;
        okLabel_ = nullptr;
        cancelLabel_ = nullptr;
        currentContent_ = {};
        inputValue_.clear();
        mode_ = RmlMessageBoxMode::Ok;
        x_ = 0;
        y_ = 0;
        visible_ = false;
        s16Caution_ = false;
        positionDirty_ = true;
        layoutDirty_ = true;
        inputDirty_ = true;
        focusInput_ = false;
        cautionGeometryDirty_ = true;
        viewportWidth_ = 0;
        viewportHeight_ = 0;
        panelHeight_ = RmlMessageBoxPanel::CautionHeight();
        host_.Release();
    }

    void SetPosition(int x, int y) noexcept
    {
        if (x_ == x && y_ == y)
        {
            return;
        }
        x_ = x;
        y_ = y;
        positionDirty_ = true;
    }

    void SetMode(RmlMessageBoxMode mode) noexcept
    {
        if (mode_ != mode)
        {
            mode_ = mode;
            layoutDirty_ = true;
        }
    }

    void SetS16Caution(bool enabled) noexcept
    {
        if (s16Caution_ != enabled)
        {
            s16Caution_ = enabled;
            layoutDirty_ = true;
            cautionGeometryDirty_ = true;
        }
    }

    int PositionX() const noexcept
    {
        return x_;
    }
    int PositionY() const noexcept
    {
        return y_;
    }
    int PanelHeight() const noexcept
    {
        return panelHeight_;
    }

    void Show(bool show) noexcept
    {
        if (!show)
        {
            focusInput_ = false;
        }
        visible_ = show;
    }

    void SetInputValue(const std::wstring &value)
    {
        if (inputValue_ == value)
        {
            return;
        }
        inputValue_ = value;
        inputDirty_ = true;
    }

    const std::wstring &InputValue() const noexcept
    {
        return inputValue_;
    }

    void FocusInput() noexcept
    {
        focusInput_ = true;
    }

    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!visible_ || !host_.ProcessInput(event))
        {
            return false;
        }
        if (input_ != nullptr)
        {
            const std::wstring value = StringUtils::NarrowToWide(input_->GetValue().c_str());
            inputValue_ =
                mode_ == RmlMessageBoxMode::Number ? SanitizeRmlMessageBoxNumber(value) : value;
            if (inputValue_ != value)
            {
                input_->SetValue(StringUtils::WideToNarrow(inputValue_.c_str()));
            }
        }
        return true;
    }

    std::optional<RmlTextInputArea> TextInputArea() const
    {
        return visible_ &&
                       (mode_ == RmlMessageBoxMode::Text || mode_ == RmlMessageBoxMode::Password ||
                        mode_ == RmlMessageBoxMode::Number)
                   ? host_.FocusedTextInputArea()
                   : std::nullopt;
    }

    bool Prepare(int viewportWidth, int viewportHeight, const RmlMessageBoxContent &content)
    {
        if (panel_ == nullptr && !visible_)
            return true;
        const int panelWidth =
            s16Caution_ ? RmlMessageBoxPanel::CautionWidth() : RmlMessageBoxPanel::Width();
        const int panelHeight = s16Caution_ ? panelHeight_ : RmlMessageBoxPanel::Height();
        if (!EnsureDocument(viewportWidth, viewportHeight, panelWidth, panelHeight) ||
            !host_.SetVisible(visible_))
        {
            return false;
        }
        const RmlUiScaledViewport viewport = host_.Viewport();

        SetPosition((viewport.width - panelWidth) / 2, (viewport.height - panelHeight) / 2);
        bool dirty = false;
        if (viewportWidth_ != viewport.width || viewportHeight_ != viewport.height)
        {
            viewportWidth_ = viewport.width;
            viewportHeight_ = viewport.height;
            cautionGeometryDirty_ = true;
        }
        if (positionDirty_)
        {
            panel_->SetProperty("left", Rml::CreateString("%dpx", x_));
            panel_->SetProperty("top", Rml::CreateString("%dpx", y_));
            positionDirty_ = false;
            dirty = true;
        }
        if (layoutDirty_)
        {
            ApplyLayout();
            layoutDirty_ = false;
            cautionGeometryDirty_ = true;
            dirty = true;
        }
        dirty = ApplyInputState() || dirty;
        const bool messageDirty = SetMessage(content);
        dirty = messageDirty || dirty;
        dirty = SetText(*title_, currentContent_.title, content.title) || dirty;
        dirty = SetText(*okLabel_, currentContent_.okLabel, content.okLabel) || dirty;
        dirty = SetText(*cancelLabel_, currentContent_.cancelLabel, content.cancelLabel) || dirty;
        if (s16Caution_ && (cautionGeometryDirty_ || messageDirty))
        {
            message_->GetContext()->Update();
            dirty = ApplyCautionGeometry(viewport.width, viewport.height) || dirty;
            cautionGeometryDirty_ = false;
        }
        dirty = okButton_.SyncVisualState() || dirty;
        dirty = cancelButton_.SyncVisualState() || dirty;
        return host_.CaptureIfDirty(dirty);
    }

    bool Record(LegacyRenderFacade &facade) const
    {
        return host_.Record(facade);
    }

  private:
    bool EnsureDocument(int viewportWidth, int viewportHeight, int contentWidth, int contentHeight)
    {
        if (!host_.Ensure(viewportWidth, viewportHeight, static_cast<float>(contentWidth),
                          static_cast<float>(contentHeight)))
        {
            return false;
        }
        if (panel_ != nullptr)
        {
            return true;
        }

        Rml::ElementDocument *const document = host_.Document();
        panel_ = document->GetElementById("message-box");
        title_ = document->GetElementById("message-title");
        divider_ = document->GetElementById("message-divider");
        message_ = document->GetElementById("message");
        messageContent_ = document->GetElementById("message-content");
        bottom_ = document->GetElementById("message-bottom");
        input_ = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
            document->GetElementById("message-input"));
        ok_ = document->GetElementById("ok-button");
        cancel_ = document->GetElementById("cancel-button");
        okLabel_ = document->GetElementById("ok-label");
        cancelLabel_ = document->GetElementById("cancel-label");
        if (panel_ == nullptr || title_ == nullptr || divider_ == nullptr || message_ == nullptr ||
            messageContent_ == nullptr || bottom_ == nullptr || input_ == nullptr ||
            ok_ == nullptr || cancel_ == nullptr || okLabel_ == nullptr || cancelLabel_ == nullptr)
        {
            Release();
            return false;
        }

        okButton_.Bind(*ok_);
        cancelButton_.Bind(*cancel_);
        positionDirty_ = true;
        layoutDirty_ = true;
        return true;
    }

    void ApplyLayout()
    {
        const bool showPassword = mode_ == RmlMessageBoxMode::Password;
        const bool showInput =
            showPassword || mode_ == RmlMessageBoxMode::Number || mode_ == RmlMessageBoxMode::Text;
        const bool showOk =
            mode_ == RmlMessageBoxMode::Ok || mode_ == RmlMessageBoxMode::OkCancel || showInput;
        const bool showCancel =
            mode_ == RmlMessageBoxMode::Cancel || mode_ == RmlMessageBoxMode::OkCancel || showInput;
        const bool twoButtons = showOk && showCancel;

        panel_->SetClass("s16-caution", s16Caution_);
        if (!s16Caution_)
        {
            panel_->RemoveProperty("height");
            message_->RemoveProperty("height");
            bottom_->RemoveProperty("top");
        }
        title_->SetProperty("display", s16Caution_ ? "block" : "none");
        input_->SetAttribute("type", showPassword ? "password" : "text");
        input_->SetAttribute("maxlength", mode_ == RmlMessageBoxMode::Number
                                              ? Design().Number<int>(DesignKey::NumberMaxLength)
                                          : mode_ == RmlMessageBoxMode::Text
                                              ? Design().Number<int>(DesignKey::TextMaxLength)
                                              : Design().Number<int>(DesignKey::PasswordMaxLength));
        input_->SetProperty("display", showInput ? "block" : "none");
        ok_->SetProperty("display", showOk ? "block" : "none");
        cancel_->SetProperty("display", showCancel ? "block" : "none");
        panel_->SetClass("two-buttons", twoButtons);
        panel_->SetClass("message-only", mode_ == RmlMessageBoxMode::MessageOnly);
        if (!showInput)
        {
            input_->Blur();
        }
    }

    bool ApplyInputState()
    {
        bool dirty = false;
        if (inputDirty_)
        {
            input_->SetValue(StringUtils::WideToNarrow(inputValue_.c_str()));
            inputDirty_ = false;
            dirty = true;
        }
        const bool inputMode = mode_ == RmlMessageBoxMode::Text ||
                               mode_ == RmlMessageBoxMode::Password ||
                               mode_ == RmlMessageBoxMode::Number;
        if (visible_ && inputMode && focusInput_ && input_->Focus(true))
        {
            focusInput_ = false;
            dirty = true;
        }
        return dirty;
    }

    bool ApplyCautionGeometry(int viewportWidth, int viewportHeight)
    {
        float contentHeight = 0.0F;
        for (int index = 0; index < messageContent_->GetNumChildren(); ++index)
        {
            contentHeight += messageContent_->GetChild(index)->GetOffsetHeight();
        }
        const int messageHeight = std::max(RmlMessageBoxPanel::CautionMessageHeight(),
                                           static_cast<int>(std::ceil(contentHeight)));
        const int extraHeight = messageHeight - RmlMessageBoxPanel::CautionMessageHeight();
        const int panelHeight = RmlMessageBoxPanel::CautionHeight() + extraHeight;
        const int x = (viewportWidth - RmlMessageBoxPanel::CautionWidth()) / 2;
        const int y = (viewportHeight - panelHeight) / 2;

        panelHeight_ = panelHeight;
        x_ = x;
        y_ = y;
        panel_->SetProperty("left", Rml::CreateString("%dpx", x));
        panel_->SetProperty("top", Rml::CreateString("%dpx", y));
        panel_->SetProperty("height", Rml::CreateString("%dpx", panelHeight));
        message_->SetProperty("height", Rml::CreateString("%dpx", messageHeight));
        bottom_->SetProperty(
            "top", Rml::CreateString("%dpx", RmlMessageBoxPanel::CautionBottomY() + extraHeight));
        return true;
    }

    bool SetMessage(const RmlMessageBoxContent &content)
    {
        if (currentContent_.firstLine == content.firstLine &&
            currentContent_.secondLine == content.secondLine)
        {
            return false;
        }
        currentContent_.firstLine = content.firstLine;
        currentContent_.secondLine = content.secondLine;

        Rml::String encoded = EncodeMessage(content.firstLine);
        if (!content.secondLine.empty())
        {
            encoded += EncodeMessage(content.secondLine);
        }
        messageContent_->SetInnerRML(encoded);
        return true;
    }

    RmlMuButton &okButton_;
    RmlMuButton &cancelButton_;
    RmlDocumentHost host_;
    Rml::Element *panel_ = nullptr;
    Rml::Element *title_ = nullptr;
    Rml::Element *divider_ = nullptr;
    Rml::Element *message_ = nullptr;
    Rml::Element *messageContent_ = nullptr;
    Rml::Element *bottom_ = nullptr;
    Rml::ElementFormControlInput *input_ = nullptr;
    Rml::Element *ok_ = nullptr;
    Rml::Element *cancel_ = nullptr;
    Rml::Element *okLabel_ = nullptr;
    Rml::Element *cancelLabel_ = nullptr;
    RmlMessageBoxContent currentContent_;
    std::wstring inputValue_;
    RmlMessageBoxMode mode_ = RmlMessageBoxMode::Ok;
    int x_ = 0;
    int y_ = 0;
    bool visible_ = false;
    bool s16Caution_ = false;
    bool positionDirty_ = true;
    bool layoutDirty_ = true;
    bool inputDirty_ = true;
    bool focusInput_ = false;
    bool cautionGeometryDirty_ = true;
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
    int panelHeight_ = RmlMessageBoxPanel::CautionHeight();
};

RmlMessageBoxPanel::RmlMessageBoxPanel(SessionKeeper &keeper)
    : buttons_(keeper), impl_(std::make_unique<Impl>(keeper, *this, buttons_[0], buttons_[1]))
{
}

RmlMessageBoxPanel::~RmlMessageBoxPanel() = default;

void RmlMessageBoxPanel::Create()
{
    s16Caution_ = false;
    height_ = Height();
    impl_->SetS16Caution(false);
    for (RmlMuButton &button : buttons_)
    {
        button.SetEnable(true);
        button.Reset();
    }
    Show(false);
}

void RmlMessageBoxPanel::CreateS16Caution()
{
    s16Caution_ = true;
    height_ = CautionHeight();
    impl_->SetS16Caution(true);
    for (RmlMuButton &button : buttons_)
    {
        button.SetEnable(true);
        button.Reset();
    }
    Show(false);
}

void RmlMessageBoxPanel::Release()
{
    s16Caution_ = false;
    x_ = 0;
    y_ = 0;
    height_ = Height();
    impl_->Release();
}

void RmlMessageBoxPanel::SetPosition(int x, int y)
{
    x_ = x;
    y_ = y;
    if (s16Caution_)
    {
        height_ = CautionHeight();
    }
    impl_->SetPosition(x, y);
}

void RmlMessageBoxPanel::SetMode(RmlMessageBoxMode mode)
{
    impl_->SetMode(mode);
}

void RmlMessageBoxPanel::Show(bool show)
{
    if (!show)
    {
        for (RmlMuButton &button : buttons_)
        {
            button.Reset();
        }
    }
    impl_->Show(show);
}

void RmlMessageBoxPanel::SetInputValue(const std::wstring &value)
{
    impl_->SetInputValue(value);
}

const std::wstring &RmlMessageBoxPanel::InputValue() const noexcept
{
    return impl_->InputValue();
}

void RmlMessageBoxPanel::FocusInput()
{
    impl_->FocusInput();
}

void RmlMessageBoxPanel::SetPassword(const std::wstring &password)
{
    SetInputValue(password);
}

const std::wstring &RmlMessageBoxPanel::Password() const noexcept
{
    return InputValue();
}

void RmlMessageBoxPanel::FocusPasswordInput()
{
    FocusInput();
}

bool RmlMessageBoxPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}

std::optional<RmlTextInputArea> RmlMessageBoxPanel::TextInputArea() const
{
    return impl_->TextInputArea();
}

bool RmlMessageBoxPanel::PrepareOnWorker(int viewportWidth, int viewportHeight,
                                         const RmlMessageBoxContent &content)
{
    if (!impl_->Prepare(viewportWidth, viewportHeight, content))
    {
        return false;
    }
    if (s16Caution_)
    {
        x_ = impl_->PositionX();
        y_ = impl_->PositionY();
        height_ = impl_->PanelHeight();
    }
    return true;
}

bool RmlMessageBoxPanel::Record(LegacyRenderFacade &facade) const
{
    return impl_->Record(facade);
}

} // namespace UI::Modern

namespace UI::Modern::PC::Help
{
class RmlHelpPanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : path_(ResolveUiDocument("Data/UI", UiPlatform::Pc, "Help", "help.rml")),
          design_(path_, {"Panel-Size", "Panel-Reference", "Panel-Initial"}),
          host_(keeper, "help-" + std::to_string(keeper.Id().RawValue()), path_)
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
        for (auto &button : buttons_)
            button.Unbind();
        movable_.Unbind();
        panel_ = nullptr;
        pages_.fill(nullptr);
        labels_.fill(nullptr);
        revision_.reset();
        visible_ = positioned_ = inputDirty_ = false;
        changes_ = {};
        tab_ = -1;
        left_ = top_ = width_ = height_ = 0;
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
        constexpr std::array buttons{"btnHotKey", "btnChatHotKey", "btnClose"};
        for (std::size_t i = 0; i < buttons.size(); ++i)
        {
            auto *element = document.GetElementById(buttons[i]);
            if (!element)
                return false;
            buttons_[i].Bind(*element);
        }
        constexpr std::array labels{"tfTitle",           "btnHotKey-label", "btnChatHotKey-label",
                                    "tfFunction",        "tfFunctionMent",  "tfChatFunction",
                                    "tfChatFunctionMent"};
        for (std::size_t i = 0; i < labels.size(); ++i)
        {
            labels_[i] = document.GetElementById(labels[i]);
            if (!labels_[i])
                return false;
        }
        pages_[0] = document.GetElementById("mcHotKey");
        pages_[1] = document.GetElementById("mcChatHotKey");
        return pages_[0] && pages_[1] &&
               lists_[0].Bind(document, "scrollingHotKey", "sbHotKey", "help-row-template") &&
               lists_[1].Bind(document, "scrollingChatKey", "sbChatHotKey", "help-row-template");
    }
    bool ApplyContent(int tab, const Content &content)
    {
        bool dirty = false;
        if (tab_ != tab)
        {
            tab_ = tab;
            for (std::size_t i = 0; i < pages_.size(); ++i)
                pages_[i]->SetProperty("display", i == tab ? "block" : "none");
            dirty = true;
        }
        if (revision_ == content.revision)
            return dirty;
        constexpr std::array mapping{0, 1, 2, 3, 4, 3, 4};
        for (std::size_t i = 0; i < labels_.size(); ++i)
            labels_[i]->SetInnerRML(Rml::StringUtilities::EncodeRml(
                StringUtils::WideToNarrow(content.labels[mapping[i]].c_str())));
        for (std::size_t i = 0; i < lists_.size(); ++i)
            lists_[i].SetData(content.rows[i]);
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
    bool Prepare(int width, int height, bool visible, int tab, const Content &content)
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
        bool dirty = ApplyContent(tab, content) || inputDirty_;
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
        if (visible)
            dirty = lists_[tab].Apply() || dirty;
        inputDirty_ = false;
        PublishBounds();
        return host_.CaptureIfDirty(dirty);
    }
    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!visible_)
            return false;
        const bool dragging = movable_.IsDragging() || lists_[tab_].IsDragging();
        host_.ProcessInput(event);
        auto *hovered = host_.HoverElement();
        lists_[tab_].ProcessInput(event, hovered);
        for (int i = 0; i < 2; ++i)
            if (buttons_[i].IsClick())
                changes_.tab = i;
        changes_.close = buttons_[2].IsClick() || changes_.close;
        bool inside = false;
        for (auto *element = hovered; element; element = element->GetParentNode())
            if (element == panel_)
            {
                inside = true;
                break;
            }
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
    std::array<RmlMuButton, 3> buttons_;
    std::array<RmlMuScrollingList, 2> lists_;
    Rml::Element *panel_ = nullptr;
    std::array<Rml::Element *, 2> pages_{};
    std::array<Rml::Element *, 7> labels_{};
    std::optional<std::uint64_t> revision_;
    Changes changes_;
    int tab_ = -1;
    float left_ = 0, top_ = 0, width_ = 0, height_ = 0;
    bool visible_ = false, positioned_ = false, inputDirty_ = false;
};
RmlHelpPanel::RmlHelpPanel(SessionKeeper &keeper) : impl_(std::make_unique<Impl>(keeper))
{
}
RmlHelpPanel::~RmlHelpPanel() = default;
void RmlHelpPanel::Release()
{
    impl_->Release();
}
bool RmlHelpPanel::PrepareOnWorker(int width, int height, bool visible, int tab,
                                   const Content &content)
{
    return impl_->Prepare(width, height, visible, tab, content);
}
bool RmlHelpPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
RmlHelpPanel::Changes RmlHelpPanel::TakeChanges()
{
    return std::exchange(impl_->changes_, {});
}
bool RmlHelpPanel::ContainsReferencePointer(int x, int y) const
{
    return impl_->visible_ && x >= impl_->left_ && y >= impl_->top_ &&
           x < impl_->left_ + impl_->width_ && y < impl_->top_ + impl_->height_;
}
bool RmlHelpPanel::Record(LegacyRenderFacade &facade) const
{
    return !impl_->panel_ || impl_->host_.Record(facade);
}
} // namespace UI::Modern::PC::Help

namespace UI::Modern::PC::Option
{
namespace
{
enum class DesignKey
{
    Width,
    Height
};
const RmlUiDesign &Design()
{
    static const RmlUiDesign design("Data/UI/PC/Option/option.rml",
                                    {"Option-Width", "Option-Height"});
    return design;
}

bool SetText(Rml::Element &element, std::wstring &current, const std::wstring &next)
{
    if (current == next)
    {
        return false;
    }
    current = next;
    element.SetInnerRML(Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(next.c_str())));
    return true;
}

bool IsChecked(const Rml::Element &element)
{
    return element.HasAttribute("checked");
}

bool SetChecked(Rml::Element &element, bool checked)
{
    if (IsChecked(element) == checked)
    {
        return false;
    }
    if (checked)
    {
        element.SetAttribute("checked", "");
    }
    else
    {
        element.RemoveAttribute("checked");
    }
    return true;
}
} // namespace

int RmlOptionPanel::Width() noexcept
{
    return Design().Number<int>(DesignKey::Width);
}
int RmlOptionPanel::Height() noexcept
{
    return Design().Number<int>(DesignKey::Height);
}

std::array<int, 2> RmlOptionViewportFor(int viewportWidth, int viewportHeight,
                                        float maximumScale) noexcept
{
    const RmlUiScaledViewport viewport =
        CalculateRmlUiScaledViewport(viewportWidth, viewportHeight, maximumScale,
                                     RmlOptionPanel::Width(), RmlOptionPanel::Height());
    return {viewport.width, viewport.height};
}

class RmlOptionPanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : host_(keeper, "option-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "Option", "option.rml"))
    {
    }

    ~Impl()
    {
        Release();
    }

    void Configure(RmlOptionChoices choices)
    {
        choices_ = std::move(choices);
    }

    void Release()
    {
        closeButton_.Unbind();
        panel_ = nullptr;
        close_ = nullptr;
        labels_.fill(nullptr);
        checks_.fill(nullptr);
        sound_.fill(nullptr);
        music_.fill(nullptr);
        effects_.fill(nullptr);
        selects_.fill(nullptr);
        currentContent_ = {};
        values_.reset();
        changes_ = {};
        x_ = 0;
        y_ = 0;
        visible_ = false;
        positionDirty_ = true;
        host_.Release();
    }

    void SetPosition(int x, int y)
    {
        if (x_ == x && y_ == y)
        {
            return;
        }
        x_ = x;
        y_ = y;
        positionDirty_ = true;
    }

    void Show(bool show)
    {
        visible_ = show;
    }

    bool ProcessInput(const SessionInputEvent &event)
    {
        if (!visible_ || !host_.ProcessInput(event))
        {
            return false;
        }
        if (closeButton_.IsClick())
            changes_.close = true;
        if (event.action != SessionInputAction::PointerMove)
            ReadChanges();
        return true;
    }

    RmlOptionChanges TakeChanges()
    {
        RmlOptionChanges result = changes_;
        changes_ = {};
        return result;
    }

    bool Prepare(int viewportWidth, int viewportHeight, const RmlOptionContent &content,
                 const RmlOptionValues &values)
    {
        if (panel_ == nullptr && !visible_)
            return true;
        if (!EnsureDocument(viewportWidth, viewportHeight) || !host_.SetVisible(visible_))
        {
            return false;
        }
        const RmlUiScaledViewport viewport = host_.Viewport();
        SetPosition((viewport.width - RmlOptionPanel::Width()) / 2,
                    (viewport.height - RmlOptionPanel::Height()) / 2);

        bool dirty = ApplyPosition();
        dirty = closeButton_.SyncVisualState() || dirty;

        const std::array<const std::wstring *, 14> next{&content.title,
                                                        &content.automaticAttack,
                                                        &content.whisperSound,
                                                        &content.nameDisplay,
                                                        &content.soundVolume,
                                                        &content.musicVolume,
                                                        &content.slideHelp,
                                                        &content.effectLimitation,
                                                        &content.renderFullEffects,
                                                        &content.font,
                                                        &content.language,
                                                        &content.resolution,
                                                        &content.windowedMode,
                                                        &content.close};
        std::array<std::wstring *, 14> current{&currentContent_.title,
                                               &currentContent_.automaticAttack,
                                               &currentContent_.whisperSound,
                                               &currentContent_.nameDisplay,
                                               &currentContent_.soundVolume,
                                               &currentContent_.musicVolume,
                                               &currentContent_.slideHelp,
                                               &currentContent_.effectLimitation,
                                               &currentContent_.renderFullEffects,
                                               &currentContent_.font,
                                               &currentContent_.language,
                                               &currentContent_.resolution,
                                               &currentContent_.windowedMode,
                                               &currentContent_.close};
        for (std::size_t index = 0; index < labels_.size(); ++index)
        {
            dirty = SetText(*labels_[index], *current[index], *next[index]) || dirty;
        }

        if (!values_.has_value() || *values_ != values)
        {
            values_ = values;
            dirty = SyncValues(values) || dirty;
        }
        return host_.CaptureIfDirty(dirty);
    }

    bool Record(LegacyRenderFacade &facade) const
    {
        return host_.Record(facade);
    }

  private:
    bool ApplyPosition()
    {
        if (!positionDirty_)
        {
            return false;
        }
        panel_->SetProperty("left", Rml::CreateString("%dpx", x_));
        panel_->SetProperty("top", Rml::CreateString("%dpx", y_));
        positionDirty_ = false;
        return true;
    }

    bool EnsureDocument(int viewportWidth, int viewportHeight)
    {
        if (!host_.Ensure(viewportWidth, viewportHeight, RmlOptionPanel::Width(),
                          RmlOptionPanel::Height()))
        {
            return false;
        }
        if (panel_ != nullptr)
        {
            return true;
        }

        Rml::ElementDocument *document = host_.Document();
        panel_ = document->GetElementById("option-panel");
        close_ = document->GetElementById("close-button");
        constexpr std::array<const char *, 14> labelIds{
            "title",          "automatic-label", "whisper-label",  "name-display-label",
            "sound-label",    "music-label",     "slide-label",    "effect-label",
            "render-label",   "font-label",      "language-label", "resolution-label",
            "windowed-label", "close-label"};
        constexpr std::array<const char *, 6> checkIds{"automatic-check",    "whisper-check",
                                                       "name-display-check", "slide-check",
                                                       "render-check",       "windowed-check"};
        constexpr std::array<const char *, 3> selectIds{"font-select", "language-select",
                                                        "resolution-select"};

        for (std::size_t i = 0; i < labels_.size(); ++i)
        {
            labels_[i] = document->GetElementById(labelIds[i]);
        }
        for (std::size_t i = 0; i < checks_.size(); ++i)
        {
            checks_[i] = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
                document->GetElementById(checkIds[i]));
        }
        for (std::size_t i = 0; i < selects_.size(); ++i)
        {
            selects_[i] = rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(
                document->GetElementById(selectIds[i]));
        }
        for (std::size_t i = 0; i < sound_.size(); ++i)
        {
            sound_[i] = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
                document->GetElementById(Rml::CreateString("sound-%d", static_cast<int>(i))));
            music_[i] = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
                document->GetElementById(Rml::CreateString("music-%d", static_cast<int>(i))));
        }
        for (std::size_t i = 0; i < effects_.size(); ++i)
        {
            effects_[i] = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
                document->GetElementById(Rml::CreateString("effect-%d", static_cast<int>(i))));
        }

        const auto allPresent = [](const auto &values) {
            for (const auto *value : values)
            {
                if (value == nullptr)
                    return false;
            }
            return true;
        };
        if (panel_ == nullptr || close_ == nullptr || !allPresent(labels_) ||
            !allPresent(checks_) || !allPresent(sound_) || !allPresent(music_) ||
            !allPresent(effects_) || !allPresent(selects_))
        {
            Release();
            return false;
        }

        const std::array<const std::vector<std::wstring> *, 3> choiceLists{
            &choices_.fonts, &choices_.languages, &choices_.resolutions};
        for (std::size_t list = 0; list < selects_.size(); ++list)
        {
            for (std::size_t index = 0; index < choiceLists[list]->size(); ++index)
            {
                const Rml::String text = Rml::StringUtilities::EncodeRml(
                    StringUtils::WideToNarrow((*choiceLists[list])[index].c_str()));
                selects_[list]->Add(text, std::to_string(index));
            }
        }
        closeButton_.Bind(*close_);
        positionDirty_ = true;
        return true;
    }

    bool SyncValues(const RmlOptionValues &value)
    {
        bool dirty = false;
        const std::array<bool, 6> checkValues{value.automaticAttack,   value.whisperSound,
                                              value.nameDisplay,       value.slideHelp,
                                              value.renderFullEffects, value.windowedMode};
        for (std::size_t i = 0; i < checks_.size(); ++i)
        {
            dirty = SetChecked(*checks_[i], checkValues[i]) || dirty;
        }
        dirty = SyncRadio(sound_, value.soundVolume) || dirty;
        dirty = SyncRadio(music_, value.musicVolume) || dirty;
        dirty = SyncRadio(effects_, value.effectLevel) || dirty;
        const std::array<int, 3> selections{value.font, value.language, value.resolution};
        for (std::size_t i = 0; i < selects_.size(); ++i)
        {
            if (selects_[i]->GetSelection() != selections[i])
            {
                selects_[i]->SetSelection(selections[i]);
                dirty = true;
            }
        }
        ApplyVolumeFill(sound_, value.soundVolume);
        ApplyVolumeFill(music_, value.musicVolume);
        return dirty;
    }

    template <std::size_t Size>
    static bool SyncRadio(std::array<Rml::ElementFormControlInput *, Size> &controls, int selection)
    {
        bool dirty = false;
        for (std::size_t i = 0; i < controls.size(); ++i)
        {
            dirty = SetChecked(*controls[i], static_cast<int>(i) == selection) || dirty;
        }
        return dirty;
    }

    static void ApplyVolumeFill(std::array<Rml::ElementFormControlInput *, 11> &controls, int level)
    {
        ApplySegmentedFill(std::span(controls).subspan(1), level);
    }

    template <std::size_t Size>
    static int CheckedIndex(const std::array<Rml::ElementFormControlInput *, Size> &controls,
                            int fallback)
    {
        for (std::size_t i = 0; i < controls.size(); ++i)
        {
            if (IsChecked(*controls[i]))
                return static_cast<int>(i);
        }
        return fallback;
    }

    void ReadChanges()
    {
        if (!values_.has_value())
            return;
        RmlOptionValues next = *values_;
        next.automaticAttack = IsChecked(*checks_[0]);
        next.whisperSound = IsChecked(*checks_[1]);
        next.nameDisplay = IsChecked(*checks_[2]);
        next.slideHelp = IsChecked(*checks_[3]);
        next.renderFullEffects = IsChecked(*checks_[4]);
        next.windowedMode = IsChecked(*checks_[5]);
        next.soundVolume = CheckedIndex(sound_, next.soundVolume);
        next.musicVolume = CheckedIndex(music_, next.musicVolume);
        next.effectLevel = CheckedIndex(effects_, next.effectLevel);
        next.font = selects_[0]->GetSelection();
        next.language = selects_[1]->GetSelection();
        next.resolution = selects_[2]->GetSelection();

#define CAPTURE_CHANGE(field)                                                                      \
    if (next.field != values_->field)                                                              \
    changes_.field = next.field
        CAPTURE_CHANGE(automaticAttack);
        CAPTURE_CHANGE(whisperSound);
        CAPTURE_CHANGE(nameDisplay);
        CAPTURE_CHANGE(soundVolume);
        CAPTURE_CHANGE(musicVolume);
        CAPTURE_CHANGE(slideHelp);
        CAPTURE_CHANGE(effectLevel);
        CAPTURE_CHANGE(renderFullEffects);
        CAPTURE_CHANGE(font);
        CAPTURE_CHANGE(language);
        CAPTURE_CHANGE(resolution);
        CAPTURE_CHANGE(windowedMode);
#undef CAPTURE_CHANGE
        const bool soundChanged = next.soundVolume != values_->soundVolume;
        const bool musicChanged = next.musicVolume != values_->musicVolume;
        values_ = next;
        if (soundChanged)
            ApplyVolumeFill(sound_, next.soundVolume);
        if (musicChanged)
            ApplyVolumeFill(music_, next.musicVolume);
    }

    RmlDocumentHost host_;
    RmlOptionChoices choices_;
    RmlOptionContent currentContent_;
    std::optional<RmlOptionValues> values_;
    RmlOptionChanges changes_;
    Rml::Element *panel_ = nullptr;
    Rml::Element *close_ = nullptr;
    RmlMuButton closeButton_;
    std::array<Rml::Element *, 14> labels_{};
    std::array<Rml::ElementFormControlInput *, 6> checks_{};
    std::array<Rml::ElementFormControlInput *, 11> sound_{};
    std::array<Rml::ElementFormControlInput *, 11> music_{};
    std::array<Rml::ElementFormControlInput *, 5> effects_{};
    std::array<Rml::ElementFormControlSelect *, 3> selects_{};
    int x_ = 0;
    int y_ = 0;
    bool visible_ = false;
    bool positionDirty_ = true;
};

RmlOptionPanel::RmlOptionPanel(SessionKeeper &keeper) : impl_(std::make_unique<Impl>(keeper))
{
}

RmlOptionPanel::~RmlOptionPanel() = default;
void RmlOptionPanel::Configure(RmlOptionChoices choices)
{
    impl_->Configure(std::move(choices));
}
void RmlOptionPanel::Release()
{
    impl_->Release();
}
void RmlOptionPanel::SetPosition(int x, int y)
{
    impl_->SetPosition(x, y);
}
void RmlOptionPanel::Show(bool show)
{
    impl_->Show(show);
}
bool RmlOptionPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}
RmlOptionChanges RmlOptionPanel::TakeChanges()
{
    return impl_->TakeChanges();
}
bool RmlOptionPanel::PrepareOnWorker(int viewportWidth, int viewportHeight,
                                     const RmlOptionContent &content, const RmlOptionValues &values)
{
    return impl_->Prepare(viewportWidth, viewportHeight, content, values);
}
bool RmlOptionPanel::Record(LegacyRenderFacade &facade)
{
    return impl_->Record(facade);
}
} // namespace UI::Modern::PC::Option

namespace UI::Modern::PC::ServerMessage
{
namespace
{
enum class Metric
{
    Width,
    Height,
    Left,
    TopOffset,
    ReferenceTop,
    ReferenceHeight
};
const RmlUiDesign &Design()
{
    static const RmlUiDesign design(
        "Data/UI/PC/ServerMessage/server_message.rml",
        {"ServerMessage-Width", "ServerMessage-Height", "ServerMessage-Left",
         "ServerMessage-TopOffset", "ServerMessage-ReferenceTop", "ServerMessage-ReferenceHeight"});
    return design;
}
} // namespace

int RmlServerMessagePanel::Width()
{
    return Design().Number<int>(Metric::Width);
}
int RmlServerMessagePanel::Height()
{
    return Design().Number<int>(Metric::Height);
}
int RmlServerMessagePanel::Left()
{
    return Design().Number<int>(Metric::Left);
}
int RmlServerMessagePanel::Top(int viewportHeight)
{
    return static_cast<int>(Design().Number(Metric::ReferenceTop) /
                            Design().Number(Metric::ReferenceHeight) * viewportHeight) +
           Design().Number<int>(Metric::TopOffset);
}

class RmlServerMessagePanel::Impl final
{
  public:
    explicit Impl(SessionKeeper &keeper)
        : host_(keeper, "server-message-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "ServerMessage", "server_message.rml"))
    {
    }

    ~Impl()
    {
        Release();
    }

    void Release()
    {
        panel_ = nullptr;
        text_ = nullptr;
        currentContent_ = {};
        x_ = 0;
        y_ = 0;
        visible_ = false;
        positionDirty_ = true;
        host_.Release();
    }

    void SetPosition(int x, int y) noexcept
    {
        if (x_ == x && y_ == y)
        {
            return;
        }
        x_ = x;
        y_ = y;
        positionDirty_ = true;
    }

    void Show(bool show) noexcept
    {
        visible_ = show;
    }

    bool Prepare(int viewportWidth, int viewportHeight, const RmlServerMessageContent &content)
    {
        if (panel_ == nullptr && !visible_)
            return true;
        const int panelHeight = RmlServerMessagePanel::Height();
        if (!EnsureDocument(viewportWidth, viewportHeight,
                            RmlServerMessagePanel::Width() + (std::max)(0, x_),
                            panelHeight + (std::max)(0, y_)) ||
            !host_.SetVisible(visible_))
        {
            return false;
        }

        bool dirty = false;
        if (positionDirty_)
        {
            panel_->SetProperty("left", Rml::CreateString("%dpx", x_));
            panel_->SetProperty("top", Rml::CreateString("%dpx", y_));
            positionDirty_ = false;
            dirty = true;
        }
        if (currentContent_ != content)
        {
            SetContent(content);
            dirty = true;
        }
        return host_.CaptureIfDirty(dirty);
    }

    bool Record(LegacyRenderFacade &facade) const
    {
        return host_.Record(facade);
    }

  private:
    bool EnsureDocument(int viewportWidth, int viewportHeight, int contentWidth, int contentHeight)
    {
        if (!host_.Ensure(viewportWidth, viewportHeight, contentWidth, contentHeight))
        {
            return false;
        }
        if (panel_ != nullptr)
        {
            return true;
        }
        Rml::ElementDocument *const document = host_.Document();
        panel_ = document->GetElementById("server-message-panel");
        text_ = document->GetElementById("server-message-text");
        if (panel_ == nullptr || text_ == nullptr)
        {
            Release();
            return false;
        }
        panel_->SetProperty("width", Rml::CreateString("%dpx", RmlServerMessagePanel::Width()));
        panel_->SetProperty("height", Rml::CreateString("%dpx", RmlServerMessagePanel::Height()));
        positionDirty_ = true;
        return true;
    }

    void SetContent(const RmlServerMessageContent &content)
    {
        currentContent_ = content;
        const std::size_t count = content.lineCount;
        Rml::String encoded;
        for (std::size_t index = 0; index < count; ++index)
        {
            if (index != 0)
            {
                encoded += "<br/>";
            }
            encoded += Rml::StringUtilities::EncodeRml(
                StringUtils::WideToNarrow(content.lines[index].c_str()));
        }
        text_->SetInnerRML(encoded);
    }

    RmlDocumentHost host_;
    Rml::Element *panel_ = nullptr;
    Rml::Element *text_ = nullptr;
    RmlServerMessageContent currentContent_;
    int x_ = 0;
    int y_ = 0;
    bool visible_ = false;
    bool positionDirty_ = true;
};

RmlServerMessagePanel::RmlServerMessagePanel(SessionKeeper &keeper)
    : impl_(std::make_unique<Impl>(keeper))
{
}

RmlServerMessagePanel::~RmlServerMessagePanel() = default;

void RmlServerMessagePanel::Release()
{
    impl_->Release();
}

void RmlServerMessagePanel::SetPosition(int x, int y)
{
    impl_->SetPosition(x, y);
}

void RmlServerMessagePanel::Show(bool show)
{
    impl_->Show(show);
}

bool RmlServerMessagePanel::PrepareOnWorker(int viewportWidth, int viewportHeight,
                                            const RmlServerMessageContent &content)
{
    return impl_->Prepare(viewportWidth, viewportHeight, content);
}

bool RmlServerMessagePanel::Record(LegacyRenderFacade &facade)
{
    return impl_->Record(facade);
}
} // namespace UI::Modern::PC::ServerMessage

namespace UI::Modern::PC::SystemMenu
{
namespace
{
enum class Metric
{
    Width,
    FullHeight,
    LoginHeight,
    SlotX,
    SlotY,
    DividerY,
    CloseY,
    CompactOffset
};
const RmlUiDesign &Design()
{
    static const RmlUiDesign design("Data/UI/PC/SystemMenu/system_menu.rml",
                                    {"SystemMenu-Width", "SystemMenu-FullHeight",
                                     "SystemMenu-LoginHeight", "SystemMenu-SlotX",
                                     "SystemMenu-SlotY", "SystemMenu-DividerY", "SystemMenu-CloseY",
                                     "SystemMenu-CompactOffset"});
    return design;
}

std::array<bool, 4> PrimaryVisibility(RmlSystemMenuMode mode)
{
    switch (mode)
    {
    case RmlSystemMenuMode::Login:
        return {true, false, false, true};
    case RmlSystemMenuMode::Character:
        return {true, true, false, true};
    case RmlSystemMenuMode::Game:
        return {true, true, true, false};
    }
    return {};
}

bool SetText(Rml::Element &element, std::wstring &current, const std::wstring &next)
{
    if (current == next)
    {
        return false;
    }
    current = next;
    element.SetInnerRML(Rml::StringUtilities::EncodeRml(StringUtils::WideToNarrow(next.c_str())));
    return true;
}
} // namespace

class RmlSystemMenuPanel::Impl final
{
  public:
    Impl(SessionKeeper &keeper, SessionBoundArray<RmlMuButton, 5> &buttons)
        : keeper_(keeper), buttons_(buttons),
          host_(keeper, "system-menu-" + std::to_string(keeper.Id().RawValue()),
                ResolveUiDocument("Data/UI", UiPlatform::Pc, "SystemMenu", "system_menu.rml"))
    {
    }

    ~Impl()
    {
        Release();
    }

    void Release()
    {
        for (RmlMuButton &button : buttons_)
        {
            button.Unbind();
        }
        panel_ = nullptr;
        frame_ = nullptr;
        title_ = nullptr;
        bottomDivider_ = nullptr;
        buttonElements_.fill(nullptr);
        labels_.fill(nullptr);
        currentContent_ = {};
        mode_ = RmlSystemMenuMode::Login;
        x_ = 0;
        y_ = 0;
        visible_ = false;
        positionDirty_ = true;
        layoutDirty_ = true;
        host_.Release();
    }

    void SetMode(RmlSystemMenuMode mode) noexcept
    {
        mode_ = mode;
        host_.SetContextName("system-menu-" + std::to_string(keeper_.Id().RawValue()) + "-" +
                             std::to_string(static_cast<int>(mode)));
        layoutDirty_ = true;
    }

    void SetPosition(int x, int y) noexcept
    {
        if (x_ == x && y_ == y)
        {
            return;
        }
        x_ = x;
        y_ = y;
        positionDirty_ = true;
    }

    void Show(bool show) noexcept
    {
        visible_ = show;
    }

    bool ProcessInput(const SessionInputEvent &event)
    {
        return visible_ && host_.ProcessInput(event);
    }

    bool Prepare(int viewportWidth, int viewportHeight, const RmlSystemMenuContent &content)
    {
        if (panel_ == nullptr && !visible_)
            return true;
        const int panelHeight = RmlSystemMenuPanel::HeightFor(mode_);
        bool dirty = false;
        if (!EnsureDocument(viewportWidth, viewportHeight, panelHeight))
        {
            return false;
        }
        if (!host_.SetVisible(visible_))
        {
            return false;
        }
        const RmlUiScaledViewport viewport = host_.Viewport();
        SetPosition((viewport.width - RmlSystemMenuPanel::Width()) / 2,
                    (viewport.height - panelHeight) / 2);
        if (positionDirty_)
        {
            panel_->SetProperty("left", Rml::CreateString("%dpx", x_));
            panel_->SetProperty("top", Rml::CreateString("%dpx", y_));
            positionDirty_ = false;
            dirty = true;
        }
        if (layoutDirty_)
        {
            ApplyLayout();
            layoutDirty_ = false;
            dirty = true;
        }
        dirty = SetText(*title_, currentContent_.title, content.title) || dirty;
        const std::array<const std::wstring *, 5> nextLabels{
            &content.exit, &content.server, &content.character, &content.option, &content.close};
        std::array<std::wstring *, 5> currentLabels{
            &currentContent_.exit, &currentContent_.server, &currentContent_.character,
            &currentContent_.option, &currentContent_.close};
        for (std::size_t index = 0; index < labels_.size(); ++index)
        {
            dirty = SetText(*labels_[index], *currentLabels[index], *nextLabels[index]) || dirty;
            dirty = buttons_[index].SyncVisualState() || dirty;
        }
        return host_.CaptureIfDirty(dirty);
    }

    bool Record(LegacyRenderFacade &facade) const
    {
        return host_.Record(facade);
    }

  private:
    bool EnsureDocument(int viewportWidth, int viewportHeight, int panelHeight)
    {
        if (!host_.Ensure(viewportWidth, viewportHeight, RmlSystemMenuPanel::Width(), panelHeight))
        {
            return false;
        }
        if (panel_ != nullptr)
        {
            return true;
        }
        Rml::ElementDocument *const document = host_.Document();
        panel_ = document->GetElementById("system-menu");
        frame_ = document->GetElementById("system-frame");
        title_ = document->GetElementById("title");
        bottomDivider_ = document->GetElementById("bottom-divider");
        constexpr std::array<const char *, 5> buttonIds{
            "exit-button", "server-button", "character-button", "option-button", "close-button"};
        constexpr std::array<const char *, 5> labelIds{
            "exit-label", "server-label", "character-label", "option-label", "close-label"};
        for (std::size_t index = 0; index < buttonElements_.size(); ++index)
        {
            buttonElements_[index] = document->GetElementById(buttonIds[index]);
            labels_[index] = document->GetElementById(labelIds[index]);
            if (buttonElements_[index] == nullptr || labels_[index] == nullptr)
            {
                Release();
                return false;
            }
            buttons_[index].Bind(*buttonElements_[index]);
        }
        if (panel_ == nullptr || frame_ == nullptr || title_ == nullptr ||
            bottomDivider_ == nullptr)
        {
            Release();
            return false;
        }
        positionDirty_ = true;
        layoutDirty_ = true;
        return true;
    }

    void ApplyLayout()
    {
        const auto visible = PrimaryVisibility(mode_);
        const auto x = Design().Values(Metric::SlotX);
        const auto y = Design().Values(Metric::SlotY);
        std::size_t slot = 0;
        for (std::size_t index = 0; index < visible.size(); ++index)
        {
            buttonElements_[index]->SetProperty("display", visible[index] ? "block" : "none");
            if (!visible[index])
                continue;
            buttonElements_[index]->SetProperty("left", Rml::CreateString("%.3fpx", x[slot]));
            buttonElements_[index]->SetProperty("top", Rml::CreateString("%.3fpx", y[slot]));
            ++slot;
        }
        const bool compact = mode_ == RmlSystemMenuMode::Login;
        const float offset = compact ? Design().Number(Metric::CompactOffset) : 0.0F;
        frame_->SetClass("login", compact);
        bottomDivider_->SetProperty(
            "top", Rml::CreateString("%.3fpx", Design().Number(Metric::DividerY) - offset));
        buttonElements_[4]->SetProperty(
            "top", Rml::CreateString("%.3fpx", Design().Number(Metric::CloseY) - offset));
        panel_->SetProperty("width", Rml::CreateString("%dpx", RmlSystemMenuPanel::Width()));
        panel_->SetProperty("height",
                            Rml::CreateString("%dpx", RmlSystemMenuPanel::HeightFor(mode_)));
    }

    SessionKeeper &keeper_;
    SessionBoundArray<RmlMuButton, 5> &buttons_;
    RmlDocumentHost host_;
    Rml::Element *panel_ = nullptr;
    Rml::Element *frame_ = nullptr;
    Rml::Element *title_ = nullptr;
    Rml::Element *bottomDivider_ = nullptr;
    std::array<Rml::Element *, 5> buttonElements_{};
    std::array<Rml::Element *, 5> labels_{};
    RmlSystemMenuContent currentContent_;
    RmlSystemMenuMode mode_ = RmlSystemMenuMode::Login;
    int x_ = 0;
    int y_ = 0;
    bool visible_ = false;
    bool positionDirty_ = true;
    bool layoutDirty_ = true;
};

RmlSystemMenuPanel::RmlSystemMenuPanel(SessionKeeper &keeper)
    : buttons_(keeper), impl_(std::make_unique<Impl>(keeper, buttons_))
{
}

RmlSystemMenuPanel::~RmlSystemMenuPanel() = default;

int RmlSystemMenuPanel::Width()
{
    return Design().Number<int>(Metric::Width);
}

int RmlSystemMenuPanel::HeightFor(RmlSystemMenuMode mode) noexcept
{
    return Design().Number<int>(mode == RmlSystemMenuMode::Login ? Metric::LoginHeight
                                                                 : Metric::FullHeight);
}

void RmlSystemMenuPanel::Create(RmlSystemMenuMode mode)
{
    for (RmlMuButton &button : buttons_)
    {
        button.SetEnable(true);
        button.Reset();
    }
    impl_->SetMode(mode);
}

void RmlSystemMenuPanel::Release()
{
    impl_->Release();
}

void RmlSystemMenuPanel::SetPosition(int x, int y)
{
    impl_->SetPosition(x, y);
}

void RmlSystemMenuPanel::Show(bool show)
{
    if (!show)
    {
        for (RmlMuButton &button : buttons_)
        {
            button.Reset();
        }
    }
    impl_->Show(show);
}

bool RmlSystemMenuPanel::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}

bool RmlSystemMenuPanel::PrepareOnWorker(int viewportWidth, int viewportHeight,
                                         const RmlSystemMenuContent &content)
{
    return impl_->Prepare(viewportWidth, viewportHeight, content);
}

bool RmlSystemMenuPanel::Record(LegacyRenderFacade &facade)
{
    return impl_->Record(facade);
}
} // namespace UI::Modern::PC::SystemMenu

//  UIPopup.cpp
//////////////////////////////////////////////////////////////////////////

void CUIPopup::Render()
{
    if (m_dwPopupID == 0)
    {
        return;
    }

    if (PopupRenderFuncPointer)
    {
        PopupRenderFuncPointer();
    }
    else
    {
        POINT pt;
        if (m_Align == PA_CENTER)
            pt = g_pUIManager->RenderWindowBase(m_sizePopup.cy);
        else
            pt = g_pUIManager->RenderWindowBase(m_sizePopup.cy, 10);

        g_RenderText.SetFont(LegacyFontRole::Normal);
        EnableAlphaTest();

        g_RenderText.SetBgColor(0x00000000);
        g_RenderText.SetTextColor(0xFFFFFFFF);

        float fPosY = pt.y + 20;
        for (int i = 0; i < m_nPopupTextCount; ++i)
        {
            SIZE size;
            g_RenderText.MeasureText(m_szPopupText[i], wcslen(m_szPopupText[i]), &size);

            size.cx /= g_fScreenRate_x;
            g_RenderText.RenderText(320 - (size.cx / 2), fPosY, m_szPopupText[i], 0, 0,
                                    RT3_SORT_LEFT);
            fPosY += 15;
        }

        if (m_PopupType & POPUP_INPUT)
        {
            RenderBitmap(BITMAP_INVENTORY + 11, 320 - m_nInputSize / 2, fPosY, m_nInputSize, 18,
                         0.f, 0.f, 113.f / 128.f, 18.f / 32.f);

            fPosY += 7;
            if (g_iChatInputType == 1)
            {
                g_pSingleTextInputBox->SetState(UISTATE_NORMAL);
                g_pSingleTextInputBox->SetOption(m_InputOptions);
                g_pSingleTextInputBox->SetBackColor(0, 0, 0, 0);
                g_pSingleTextInputBox->SetTextLimit(m_nInputTextLength);
                g_pSingleTextInputBox->SetSize(m_nInputSize, 14);
                g_pSingleTextInputBox->SetPosition(320 - m_nInputSize / 2 + 5, fPosY - 2);
                g_pSingleTextInputBox->GiveFocus();
                g_pSingleTextInputBox->Render();
            }
            else if (g_iChatInputType == 0)
            {
                InputTextWidth = 100;
                RenderInputText(pt.x + 213 / 2 - m_nInputSize + 5, fPosY - 2, 0);
                InputTextWidth = 255;
            }
            fPosY += 10;
        }
        if (m_PopupType & POPUP_TIMEOUT)
        {
            fPosY += 7;
            DWORD dwCurrTime = GetTickCount();
            float fProgress = (float)(dwCurrTime - m_dwPopupStartTime) / m_dwPopupElapseTime;

            RenderBitmap(BITMAP_INTERFACE_EX + 42, 320 - 75, fPosY, 150.0f, 12.0f, 0.f, 0.f,
                         200.0f / 256.0f, 16.0f / 16.0f);
            EnableAlphaBlend();
            RenderBitmap(BITMAP_INTERFACE_EX + 43, 320 - 75 - 4, fPosY, 150.0f * fProgress, 12.0f,
                         0.f, 0.f, (150.0f * fProgress) / 256.0f, 16.0f / 16.0f);
            DisableAlphaBlend();
            fPosY += 10;
        }
        fPosY += 9;
        if (m_PopupType & POPUP_OK)
        {
            m_OkButton.SetPosition(320 - 25, fPosY);
            m_OkButton.Render();
        }
        else if (m_PopupType & POPUP_OKCANCEL)
        {
            m_OkButton.SetPosition(320 - 50 - 10, fPosY);
            m_OkButton.Render();
            m_CancelButton.SetPosition(320 + 10, fPosY);
            m_CancelButton.Render();
        }
        else if (m_PopupType & POPUP_YESNO)
        {
            m_YesButton.SetPosition(320 - 50 - 10, fPosY);
            m_YesButton.Render();
            m_NoButton.SetPosition(320 + 10, fPosY);
            m_NoButton.Render();
        }
    }
}

//
//////////////////////////////////////////////////////////////////////

using namespace SEASON3B;

//////////////////////////////////////////////////////////////////////
// CNewUIMessageBoxBase
//////////////////////////////////////////////////////////////////////

void SEASON3B::CNewUIMessageBoxBase::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
}

void SEASON3B::CNewUIMessageBoxBase::SetSize(int width, int height)
{
    m_Size.cx = width;
    m_Size.cy = height;
}

void SEASON3B::CNewUIMessageBoxBase::RenderMsgBackColor(bool _bRender)
{
    float fWidth = (float)REFERENCE_WIDTH, fHeight = (float)REFERENCE_HEIGHT;
    float fPosX = 0.0f, fPosY = 0.0f;
    if (_bRender)
    {
        glEnable(GL_ALPHA_TEST);
        glColor4f(m_vColor[0], m_vColor[1], m_vColor[2], m_fOpacityAlpha);
        RenderColor(fPosX, fPosY, fWidth, fHeight - 50.0f);

        glEnable(GL_TEXTURE_2D);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glDisable(GL_BLEND);
        glEnable(GL_ALPHA_TEST);
    }
}

bool SEASON3B::CNewUIMessageBoxMng::Render()
{
    std::sort(m_vecMsgBoxes.begin(), m_vecMsgBoxes.end(), ComparePriority);
    auto vi = m_vecMsgBoxes.begin();
    if (vi == m_vecMsgBoxes.end())
    {
        return true;
    }
    return (*vi)->Render();
}

bool SEASON3B::CNewUIMessageBoxMng::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight)
{
    std::sort(m_vecMsgBoxes.begin(), m_vecMsgBoxes.end(), ComparePriority);
    if (m_vecMsgBoxes.empty())
    {
        return true;
    }
    return m_vecMsgBoxes.front()->PrepareModernUiOnWorker(viewportWidth, viewportHeight);
}

void SEASON3B::CNewUIMessageBoxMng::LoadImages()
{
    LoadBitmapW(L"Interface\\newui_msgbox_top.tga", IMAGE_MSGBOX_TOP, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_msgbox_middle.tga", IMAGE_MSGBOX_MIDDLE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_msgbox_bottom.tga", IMAGE_MSGBOX_BOTTOM,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_msgbox_back.jpg", IMAGE_MSGBOX_BACK,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_Message_Line.tga", IMAGE_MSGBOX_LINE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_Message_03.tga", IMAGE_MSGBOX_TOP_TITLEBAR,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_separate_line.jpg", IMAGE_MSGBOX_SEPARATE_LINE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_button_ok.tga", IMAGE_MSGBOX_BTN_OK,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_button_cancel.tga", IMAGE_MSGBOX_BTN_CANCEL,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_button_close.tga", IMAGE_MSGBOX_BTN_CLOSE,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_btn_empty.tga", IMAGE_MSGBOX_BTN_EMPTY,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_btn_empty_small.tga", IMAGE_MSGBOX_BTN_EMPTY_SMALL,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_btn_empty_big.tga", IMAGE_MSGBOX_BTN_EMPTY_BIG,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_btn_empty_very_small.tga", IMAGE_MSGBOX_BTN_EMPTY_VERY_SMALL,
                LegacyTextureFilter::Linear);

    LoadBitmapW(L"Interface\\newui_Bar_switch01.jpg", IMAGE_MSGBOX_PROGRESS_BG,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\newui_Bar_switch02.jpg", IMAGE_MSGBOX_PROGRESS_BAR,
                LegacyTextureFilter::Linear);

    LoadBitmapW(L"Interface\\newui_DuelWindow.tga", IMAGE_MSGBOX_DUEL_BACK,
                LegacyTextureFilter::Linear);
}

void SEASON3B::CNewUIMessageBoxMng::UnloadImages()
{
    DeleteBitmap(IMAGE_MSGBOX_DUEL_BACK);

    DeleteBitmap(IMAGE_MSGBOX_PROGRESS_BAR);
    DeleteBitmap(IMAGE_MSGBOX_PROGRESS_BG);

    DeleteBitmap(IMAGE_MSGBOX_TOP);
    DeleteBitmap(IMAGE_MSGBOX_MIDDLE);
    DeleteBitmap(IMAGE_MSGBOX_BOTTOM);
    DeleteBitmap(IMAGE_MSGBOX_BACK);
    DeleteBitmap(IMAGE_MSGBOX_LINE);
    DeleteBitmap(IMAGE_MSGBOX_TOP_TITLEBAR);
    DeleteBitmap(IMAGE_MSGBOX_SEPARATE_LINE);

    DeleteBitmap(IMAGE_MSGBOX_BTN_OK);
    DeleteBitmap(IMAGE_MSGBOX_BTN_CANCEL);
    DeleteBitmap(IMAGE_MSGBOX_BTN_CLOSE);
    DeleteBitmap(IMAGE_MSGBOX_BTN_EMPTY);
    DeleteBitmap(IMAGE_MSGBOX_BTN_EMPTY_SMALL);
    DeleteBitmap(IMAGE_MSGBOX_BTN_EMPTY_BIG);
    DeleteBitmap(IMAGE_MSGBOX_BTN_EMPTY_VERY_SMALL);
}

#define SUBGUILDMASTER 64
#define BATTLEMASTER 32

namespace SystemMenu = UI::Modern::PC::SystemMenu;

void SEASON3B::CNewUITextInputMsgBox::SetButtonInfo()
{
    float x, y, width, height;

    switch (m_dwMsgBoxType)
    {
    case MSGBOX_COMMON_TYPE_OK:
        x = GetPos().x + (GetSize().cx / 2) - (MSGBOX_BTN_WIDTH / 2);
        y = GetPos().y + GetSize().cy - (MSGBOX_BTN_HEIGHT + MSGBOX_BTN_BOTTOM_BLANK);
        width = MSGBOX_BTN_WIDTH;
        height = MSGBOX_BTN_HEIGHT;
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
        m_BtnOk.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_OK, x, y, width, height,
                        CNewUIMessageBoxButton::MSGBOX_BTN_SIZE_OK);
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
        m_BtnOk.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_OK, x, y, width, height);
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
        break;
    case MSGBOX_COMMON_TYPE_OKCANCEL:
        x = GetPos().x + (((GetSize().cx / 2) - MSGBOX_BTN_WIDTH) / 2);
        y = GetPos().y + GetSize().cy - (MSGBOX_BTN_HEIGHT + MSGBOX_BTN_BOTTOM_BLANK);
        width = MSGBOX_BTN_WIDTH;
        height = MSGBOX_BTN_HEIGHT;
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
        m_BtnOk.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_OK, x, y, width, height,
                        CNewUIMessageBoxButton::MSGBOX_BTN_SIZE_OK);
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
        m_BtnOk.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_OK, x, y, width, height);
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

        x = GetPos().x + (GetSize().cx / 2) + (((GetSize().cx / 2) - MSGBOX_BTN_WIDTH) / 2);
        y = GetPos().y + GetSize().cy - (MSGBOX_BTN_HEIGHT + MSGBOX_BTN_BOTTOM_BLANK);
        width = MSGBOX_BTN_WIDTH;
        height = MSGBOX_BTN_HEIGHT;
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
        m_BtnCancel.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_CANCEL, x, y, width, height,
                            CNewUIMessageBoxButton::MSGBOX_BTN_SIZE_OK);
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
        m_BtnCancel.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_CANCEL, x, y, width, height);
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
        break;
    }
}

bool SEASON3B::CNewUITextInputMsgBox::Render()
{
    if (m_UseModernPasswordInput)
        return m_ModernMenu.Record(LegacyRender());
    if (m_UseModernNumberInput)
    {
        return g_MessageBox.ModernMessageBoxPanel().Record(LegacyRender());
    }

    float x, y, width, height;

    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    x = GetPos().x;
    y = GetPos().y + 2.f, width = GetSize().cx - MSGBOX_BACK_BLANK_WIDTH;
    height = GetSize().cy - MSGBOX_BACK_BLANK_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, x, y, width, height);

    x = GetPos().x;
    y = GetPos().y, width = MSGBOX_WIDTH;
    height = MSGBOX_TOP_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_TOP, x, y, width, height);

    x = GetPos().x;
    y += MSGBOX_TOP_HEIGHT;
    width = MSGBOX_WIDTH;
    height = MSGBOX_MIDDLE_HEIGHT;
    int iCount = m_MsgTextList.size();
    for (int i = 0; i < iCount; ++i)
    {
        RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_MIDDLE, x, y, width, height);
        y += height;
    }

    x = GetPos().x;
    width = MSGBOX_WIDTH;
    height = MSGBOX_BOTTOM_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_BOTTOM, x, y, width, height);

    RenderTexts();

    if (m_pInputBox)
    {
        m_pInputBox->Render();
    }

    RenderButtons();

    DisableAlphaBlend();
    return true;
}

bool SEASON3B::CNewUITextInputMsgBox::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight)
{
    if (m_UseModernPasswordInput)
    {
        if (!m_ModernMenu.PrepareOnWorker(viewportWidth, viewportHeight))
            return false;
        const auto bounds = m_ModernMenu.Bounds();
        SetPos(static_cast<int>(bounds.x), static_cast<int>(bounds.y));
        SetSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
        return true;
    }
    if (!m_UseModernNumberInput)
    {
        return true;
    }

    UI::Modern::RmlMessageBoxPanel &panel = g_MessageBox.ModernMessageBoxPanel();
    panel.Show(true);
    const UI::Modern::RmlMessageBoxContent content{m_ModernMessage, L"", I18N::Game::OK,
                                                   I18N::Game::Cancel, L""};
    return panel.PrepareOnWorker(viewportWidth, viewportHeight, content);
}

void SEASON3B::CNewUITextInputMsgBox::RenderTexts()
{

    float x, y;
    x = GetPos().x;
    y = GetPos().y + MSGBOX_TEXT_TOP_BLANK;
    auto vi = m_MsgTextList.begin();
    for (; vi != m_MsgTextList.end(); vi++)
    {
        g_RenderText.SetTextColor((*vi)->dwColor);
        g_RenderText.SetBgColor(0, 0, 0, 0);
        switch ((*vi)->byFontType)
        {
        case MSGBOX_FONT_NORMAL:
            g_RenderText.SetFont(LegacyFontRole::Normal);
            break;
        case MSGBOX_FONT_BOLD:
            g_RenderText.SetFont(LegacyFontRole::Bold);
            break;
        }

        SIZE TextSize;
        size_t TextExtentWidth, TextExtentHeight;

        g_RenderText.MeasureText((*vi)->strMsg.c_str(), (*vi)->strMsg.size(), &TextSize);
        TextExtentWidth = (size_t)(TextSize.cx / g_fScreenRate_x);
        TextExtentHeight = (size_t)(TextSize.cy / g_fScreenRate_y);

        x = GetPos().x + (MSGBOX_WIDTH / 2) - (TextExtentWidth / 2);
        g_RenderText.RenderText((int)x, (int)y, (*vi)->strMsg.c_str());
        y += (TextExtentHeight + 4);
    }
}

void SEASON3B::CNewUITextInputMsgBox::RenderButtons()
{
    switch (m_dwMsgBoxType)
    {
    case MSGBOX_COMMON_TYPE_OK:
        m_BtnOk.Render();
        break;
    case MSGBOX_COMMON_TYPE_OKCANCEL:
        m_BtnOk.Render();
        m_BtnCancel.Render();
        break;
    }
}

void SEASON3B::CNewUIKeyPadButton::Render()
{
    if (GetEventState() == EVENT_BTN_HOVER)
    {
        RenderImage(BITMAP_INVENTORY + 17, GetPosX(), GetPosY(), GetWidth(), GetHeight());
    }
    else if (GetEventState() == EVENT_BTN_DOWN)
    {
        RenderImage(BITMAP_INVENTORY + 18, GetPosX(), GetPosY(), GetWidth(), GetHeight());
    }
    else
    {
        glColor3f(0.80f, 0.80f, 0.80f);
        RenderImage(BITMAP_INVENTORY + 17, GetPosX(), GetPosY(), GetWidth(), GetHeight());
        glColor3f(1.f, 1.f, 1.f);
    }
}

void SEASON3B::CNewUIDeleteKeyPadButton::Render()
{
    if (GetEventState() == EVENT_BTN_HOVER)
    {
        RenderImage(BITMAP_INTERFACE + 25, GetPosX(), GetPosY(), GetWidth(), GetHeight());
    }
    else if (GetEventState() == EVENT_BTN_DOWN)
    {
        RenderImage(BITMAP_INTERFACE + 26, GetPosX(), GetPosY(), GetWidth(), GetHeight());
    }
    else
    {
        RenderImage(BITMAP_INTERFACE + 24, GetPosX(), GetPosY(), GetWidth(), GetHeight());
    }
}

//////////////////////////////////////////////////////////////////////////

bool SEASON3B::CNewUIKeyPadMsgBox::Render()
{
    return m_ModernMenu.Record(LegacyRender());
}

//////////////////////////////////////////////////////////////////////////

bool SEASON3B::CUseFruitCheckMsgBox::Render()
{
    return panel_.Record(LegacyRender());
}

void SEASON3B::CUseFruitCheckMsgBox::Render3D()
{
    const auto slot = panel_.SlotBounds("isItem");
    if (slot.width <= 0 || slot.height <= 0)
        return;
    RenderItem3D(slot.x, slot.y, slot.width, slot.height, m_Item.Type, m_Item.Level,
                 m_Item.ExcellentFlags, m_Item.AncientDiscriminator, true);
}

bool SEASON3B::CUseFruitCheckMsgBox::PrepareModernUiOnWorker(int width, int height)
{
    if (!panel_.PrepareOnWorker(width, height))
        return false;
    const auto bounds = panel_.Bounds();
    SetPos(static_cast<int>(bounds.x), static_cast<int>(bounds.y));
    SetSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    return true;
}

//////////////////////////////////////////////////////////////////////////

bool SEASON3B::CGemIntegrationMsgBox::Render()
{
    return panel_.Record(LegacyRender());
}

bool SEASON3B::CGemIntegrationUnityMsgBox::Render()
{
    return ActivePanel().Record(LegacyRender());
}

bool SEASON3B::CGemIntegrationDisjointMsgBox::Render()
{
    return panel_.Record(LegacyRender());
}

//////////////////////////////////////////////////////////////////////////

bool SEASON3B::CSystemMenuMsgBox::Render()
{
    return g_MessageBox.ModernSystemMenuPanel().Record(LegacyRender());
}

bool SEASON3B::CSystemMenuMsgBox::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight)
{
    SystemMenu::RmlSystemMenuPanel &modernPanel = g_MessageBox.ModernSystemMenuPanel();
    const int width = SystemMenu::RmlSystemMenuPanel::Width();
    const int height =
        SystemMenu::RmlSystemMenuPanel::HeightFor(SystemMenu::RmlSystemMenuMode::Game);
    modernPanel.SetPosition((viewportWidth - width) / 2, (viewportHeight - height) / 2);
    const SystemMenu::RmlSystemMenuContent content{
        I18N::Game::SystemMenu,      I18N::Game::ExitGame,  I18N::Game::SelectServer,
        I18N::Game::SwitchCharacter, I18N::Game::Option385, I18N::Game::Close388};
    return modernPanel.PrepareOnWorker(viewportWidth, viewportHeight, content);
}

bool SEASON3B::CBloodCastleResultMsgBox::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    RenderFrame();
    m_BtnOk.Render();
    EnableAlphaBlend();
    gameplay_.RenderResult();
    DisableAlphaBlend();
    return true;
}

void SEASON3B::CBloodCastleResultMsgBox::RenderFrame()
{
    float x, y, width, height;

    x = GetPos().x;
    y = GetPos().y + 2.f, width = GetSize().cx - MSGBOX_BACK_BLANK_WIDTH;
    height = GetSize().cy - MSGBOX_BACK_BLANK_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, x, y, width, height);

    x = GetPos().x;
    y = GetPos().y, width = MSGBOX_WIDTH;
    height = MSGBOX_TOP_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_TOP, x, y, width, height);

    x = GetPos().x;
    y += MSGBOX_TOP_HEIGHT;
    width = MSGBOX_WIDTH;
    height = MSGBOX_MIDDLE_HEIGHT;
    for (int i = 0; i < MIDDLE_COUNT; ++i)
    {
        RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_MIDDLE, x, y, width, height);
        y += height;
    }

    x = GetPos().x;
    width = MSGBOX_WIDTH;
    height = MSGBOX_BOTTOM_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_BOTTOM, x, y, width, height);
}

bool SEASON3B::CDevilSquareRankMsgBox::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    RenderFrame();
    m_BtnOk.Render();
    EnableAlphaBlend();
    gameplay_.RenderResult();
    DisableAlphaBlend();

    return true;
}

void SEASON3B::CDevilSquareRankMsgBox::RenderFrame()
{
    float x, y, width, height;

    x = GetPos().x;
    y = GetPos().y + 2.f, width = GetSize().cx - MSGBOX_BACK_BLANK_WIDTH;
    height = GetSize().cy - MSGBOX_BACK_BLANK_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, x, y, width, height);

    x = GetPos().x;
    y = GetPos().y, width = MSGBOX_WIDTH;
    height = MSGBOX_TOP_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_TOP, x, y, width, height);

    x = GetPos().x;
    y += MSGBOX_TOP_HEIGHT;
    width = MSGBOX_WIDTH;
    height = MSGBOX_MIDDLE_HEIGHT;
    for (int i = 0; i < MIDDLE_COUNT1; ++i)
    {
        RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_MIDDLE, x, y, width, height);
        y += height;
    }

    x = GetPos().x;
    width = MSGBOX_LINE_WIDTH;
    height = MSGBOX_LINE_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_LINE, x, y, width, height);
    y += height;

    x = GetPos().x;
    width = MSGBOX_WIDTH;
    height = MSGBOX_MIDDLE_HEIGHT;

    for (int i = 0; i < MIDDLE_COUNT2; ++i)
    {
        RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_MIDDLE, x, y, width, height);
        y += height;
    }

    x = GetPos().x;
    width = MSGBOX_WIDTH;
    height = MSGBOX_BOTTOM_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_BOTTOM, x, y, width, height);

    x = GetPos().x + 13;
    y = GetPos().y + 75;
    width = MSGBOX_SEPARATE_LINE_WIDTH;
    height = MSGBOX_SEPARATE_LINE_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_SEPARATE_LINE, x, y, width, height);

    x = GetPos().x + 13;
    y = GetPos().y + 93;
    width = MSGBOX_SEPARATE_LINE_WIDTH;
    height = MSGBOX_SEPARATE_LINE_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_SEPARATE_LINE, x, y, width, height);

    x = GetPos().x + 13;
    y = GetPos().y + 255;
    width = MSGBOX_SEPARATE_LINE_WIDTH;
    height = MSGBOX_SEPARATE_LINE_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_SEPARATE_LINE, x, y, width, height);

    x = GetPos().x + 13;
    y = GetPos().y + 273;
    width = MSGBOX_SEPARATE_LINE_WIDTH;
    height = MSGBOX_SEPARATE_LINE_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_SEPARATE_LINE, x, y, width, height);
}

bool SEASON3B::CChaosCastleResultMsgBox::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    RenderFrame();
    m_BtnOk.Render();
    EnableAlphaBlend();
    gameplay_.RenderResult();
    DisableAlphaBlend();
    return true;
}

void SEASON3B::CChaosCastleResultMsgBox::RenderFrame()
{
    float x, y, width, height;

    x = GetPos().x;
    y = GetPos().y + 2.f, width = GetSize().cx - MSGBOX_BACK_BLANK_WIDTH;
    height = GetSize().cy - MSGBOX_BACK_BLANK_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, x, y, width, height);

    x = GetPos().x;
    y = GetPos().y, width = MSGBOX_WIDTH;
    height = MSGBOX_TOP_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_TOP, x, y, width, height);

    x = GetPos().x;
    y += MSGBOX_TOP_HEIGHT;
    width = MSGBOX_WIDTH;
    height = MSGBOX_MIDDLE_HEIGHT;
    for (int i = 0; i < MIDDLE_COUNT; ++i)
    {
        RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_MIDDLE, x, y, width, height);
        y += height;
    }

    x = GetPos().x;
    width = MSGBOX_WIDTH;
    height = MSGBOX_BOTTOM_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_BOTTOM, x, y, width, height);
}

//////////////////////////////////////////////////////////////////////////

bool SEASON3B::CChaosMixMenuMsgBox::Render()
{
    return m_ModernMenu.Record(LegacyRender());
}

bool SEASON3B::CProgressMsgBox::Render()
{
    return panel_.Record(LegacyRender());
}
bool SEASON3B::CProgressMsgBox::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, message_, elapsed_, m_dwElapseTime);
}

bool SEASON3B::CCursedTempleProgressMsgBox::Render()
{
    return panel_.Record(LegacyRender());
}
bool SEASON3B::CCursedTempleProgressMsgBox::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, message_, elapsed_, m_dwElapseTime);
}

bool SEASON3B::CDuelMsgBox::Render()
{
    return panel_.Record(LegacyRender());
}

bool SEASON3B::CDuelMsgBox::PrepareModernUiOnWorker(int width, int height)
{
    if (!panel_.PrepareOnWorker(width, height))
        return false;
    const auto bounds = panel_.Bounds();
    SetPos(static_cast<int>(bounds.x), static_cast<int>(bounds.y));
    SetSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    return true;
}

bool SEASON3B::CDuelResultMsgBox::Render()
{
    return panel_.Record(LegacyRender());
}

bool SEASON3B::CDuelResultMsgBox::PrepareModernUiOnWorker(int width, int height)
{
    if (!panel_.PrepareOnWorker(width, height))
        return false;
    const auto bounds = panel_.Bounds();
    SetPos(static_cast<int>(bounds.x), static_cast<int>(bounds.y));
    SetSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    return true;
}

bool CCherryBlossomMsgBox::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    RenderFrame();
    RenderTexts();
    RenderButtons();
    DisableAlphaBlend();
    return true;
}

void CCherryBlossomMsgBox::SetButtonInfo()
{
    float x, y, width, height;

    float msgboxhalfwidth = (GetSize().cx / 2.f);
    float btnhalfwidth = MSGBOX_BTN_EMPTY_WIDTH / 2.f;

    width = MSGBOX_BTN_EMPTY_WIDTH + 20;
    height = MSGBOX_BTN_EMPTY_HEIGHT;
    btnhalfwidth = width / 2.f;
    x = GetPos().x + msgboxhalfwidth - btnhalfwidth;
    y = GetPos().y + 50;
    m_BtnWhiteCB.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY, x, y, width, height,
                         CNewUIMessageBoxButton::MSGBOX_BTN_SIZE_EMPTY);
    m_BtnWhiteCB.SetText(I18N::Game::Lookup(2542));

    y = GetPos().y + 100;
    m_BtnRedCB.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY, x, y, width, height,
                       CNewUIMessageBoxButton::MSGBOX_BTN_SIZE_EMPTY);
    m_BtnRedCB.SetText(I18N::Game::Lookup(2543));

    y = GetPos().y + 150;
    m_BtnGoldCB.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY, x, y, width, height,
                        CNewUIMessageBoxButton::MSGBOX_BTN_SIZE_EMPTY);
    m_BtnGoldCB.SetText(I18N::Game::GoldenCherryBlossomsBranches);

    width = MSGBOX_BTN_EMPTY_SMALL_WIDTH;
    btnhalfwidth = width / 2.f;
    x = GetPos().x + msgboxhalfwidth - btnhalfwidth;
    y = GetPos().y + GetSize().cy - (MSGBOX_BTN_EMPTY_HEIGHT + MSGBOX_BTN_BOTTOM_BLANK);
    m_BtnExit.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY_SMALL, x, y, width, height,
                      CNewUIMessageBoxButton::MSGBOX_BTN_SIZE_EMPTY_SMALL);
    // 1002 "??"
    m_BtnExit.SetText(I18N::Game::Close388);
}

void CCherryBlossomMsgBox::RenderFrame()
{
    float x, y, width, height;

    x = GetPos().x;
    y = GetPos().y + 2.f, width = GetSize().cx - MSGBOX_BACK_BLANK_WIDTH;
    height = GetSize().cy - MSGBOX_BACK_BLANK_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, x, y, width, height);

    x = GetPos().x;
    y = GetPos().y, width = MSGBOX_WIDTH;
    height = MSGBOX_TOP_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_TOP_TITLEBAR, x, y, width, height);

    x = GetPos().x;
    y += MSGBOX_TOP_HEIGHT;
    width = MSGBOX_WIDTH;
    height = MSGBOX_MIDDLE_HEIGHT;
    for (int i = 0; i < m_iMiddleCount; ++i)
    {
        RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_MIDDLE, x, y, width, height);
        y += height;
    }

    x = GetPos().x;
    width = MSGBOX_WIDTH;
    height = MSGBOX_BOTTOM_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_BOTTOM, x, y, width, height);
}

void CCherryBlossomMsgBox::RenderTexts()
{
    wchar_t title[256];

    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.SetTextColor(255, 255, 0, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);
    mu_swprintf(title, L"%ls", GameData().getMonsterName(450));
    g_RenderText.RenderText(GetPos().x, GetPos().y + 10, title, MSGBOX_WIDTH, 0, RT3_SORT_CENTER);

    wchar_t titleinfo[256];
    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.SetTextColor(255, 255, 0, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);
    mu_swprintf(titleinfo, L"%ls", I18N::Game::GoldenCherryBlossomsBranches);
    g_RenderText.RenderText(GetPos().x, GetPos().y + 70, titleinfo, MSGBOX_WIDTH, 0,
                            RT3_SORT_CENTER);
}

void CCherryBlossomMsgBox::RenderButtons()
{
    m_BtnWhiteCB.Render();
    m_BtnRedCB.Render();
    m_BtnGoldCB.Render();
    m_BtnExit.Render();
}

bool SEASON3B::CTradeZenMsgBoxLayout::SetLayout()
{
    CNewUITextInputMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL, INPUTBOX_TYPE_NUMBER, INPUTBOX_WIDTH,
                                 INPUTBOX_HEIGHT, INPUTBOX_TEXTLIMIT))
        return false;

    pMsgBox->SetInputBoxOption(UIOPTION_NUMBERONLY | UIOPTION_PAINTBACK);
    pMsgBox->AddMsg(I18N::Game::EnterTheAmountOfZenYouWouldLikeToTrade);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CTradeZenMsgBoxLayout::ReturnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CTradeZenMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CTradeZenMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CTradeZenMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CZenReceiptMsgBoxLayout::SetLayout()
{
    CNewUITextInputMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL, INPUTBOX_TYPE_NUMBER, INPUTBOX_WIDTH,
                                 INPUTBOX_HEIGHT, INPUTBOX_TEXTLIMIT))
        return false;

    pMsgBox->SetInputBoxOption(UIOPTION_NUMBERONLY | UIOPTION_PAINTBACK);
    pMsgBox->AddMsg(I18N::Game::EnterTheAmountOfZenYouWouldLikeToDeposit);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CZenReceiptMsgBoxLayout::ReturnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CZenReceiptMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CZenReceiptMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CZenReceiptMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CZenPaymentMsgBoxLayout::SetLayout()
{
    CNewUITextInputMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL, INPUTBOX_TYPE_NUMBER, INPUTBOX_WIDTH,
                                 INPUTBOX_HEIGHT, INPUTBOX_TEXTLIMIT))
        return false;

    pMsgBox->SetInputBoxOption(UIOPTION_NUMBERONLY | UIOPTION_PAINTBACK);
    pMsgBox->AddMsg(I18N::Game::EnterTheAmountOfZenYouWouldLikeToWithdraw);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CZenPaymentMsgBoxLayout::ReturnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CZenPaymentMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CZenPaymentMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CZenPaymentMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CPersonalShopItemValueMsgBoxLayout::SetLayout()
{
    CNewUITextInputMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (!pMsgBox->CreateModernNumberInput(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->SetInputBoxOption(UIOPTION_NUMBERONLY | UIOPTION_PAINTBACK);
    pMsgBox->AddMsg(I18N::Game::EnterSellingPrice);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPersonalShopItemValueMsgBoxLayout::ReturnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPersonalShopItemValueMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPersonalShopItemValueMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CPersonalShopItemValueMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CPersonalShopNameMsgBoxLayout::SetLayout()
{
    CPersonalShopNameMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL, INPUTBOX_TYPE_TEXT, INPUT_WIDTH,
                                 INPUT_HEIGHT, INPUT_TEXTLIMIT))
        return false;

    pMsgBox->SetInputBoxOption(UIOPTION_PAINTBACK);
    pMsgBox->AddMsg(I18N::Game::EnterStoreName);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPersonalShopNameMsgBoxLayout::ReturnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPersonalShopNameMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPersonalShopNameMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CPersonalShopNameMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CCastleWithdrawMsgBoxLayout::SetLayout()
{
    CNewUITextInputMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL, INPUTBOX_TYPE_NUMBER, INPUTBOX_WIDTH,
                                 INPUTBOX_HEIGHT, INPUTBOX_TEXTLIMIT))
        return false;

    pMsgBox->SetInputBoxOption(UIOPTION_NUMBERONLY | UIOPTION_PAINTBACK);

    pMsgBox->AddMsg(I18N::Game::EnterTheWithdrawalAmount);
    pMsgBox->AddMsg(I18N::Game::Maximum15000000Zen);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CCastleWithdrawMsgBoxLayout::ReturnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CCastleWithdrawMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CCastleWithdrawMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CCastleWithdrawMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CPasswordKeyPadMsgBoxLayout::SetLayout()
{
    CNewUIKeyPadMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(KEYPAD_TYPE_MOVE, 4))
        return false;

    pMsgBox->AddMsg(I18N::Game::PasswordVerification);
    pMsgBox->AddMsg(I18N::Game::Choose4DigitsForPassword);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPasswordKeyPadMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPasswordKeyPadMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPasswordKeyPadMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPasswordKeyPadMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);

    return true;
}

bool SEASON3B::CStorageLockKeyPadMsgBoxLayout::SetLayout()
{
    CNewUIKeyPadMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(KEYPAD_TYPE_LOCK_FIRST, 4))
        return false;

    pMsgBox->AddMsg(I18N::Game::ChooseNewPassword);
    pMsgBox->AddMsg(I18N::Game::Choose4DigitsForPassword);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CStorageLockKeyPadMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CStorageLockKeyPadMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CStorageLockKeyPadMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CStorageLockKeyPadMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CStorageLockCheckKeyPadMsgBoxLayout::SetLayout()
{
    CNewUIKeyPadMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(KEYPAD_TYPE_LOCK_SECOND, 4))
        return false;

    pMsgBox->AddMsg(I18N::Game::VerifyNewPassword);
    pMsgBox->AddMsg(I18N::Game::EnterPasswordAgain);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CStorageLockCheckKeyPadMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CStorageLockCheckKeyPadMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CStorageLockCheckKeyPadMsgBoxLayout::CancelBtnDown),
        MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CStorageLockCheckKeyPadMsgBoxLayout::CancelBtnDown),
        MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CStorageLockMsgBoxLayout::SetLayout()
{
    CNewUITextInputMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (!pMsgBox->CreateModernPasswordInput(g_iLengthAuthorityCode))
        return false;

    pMsgBox->AddMsg(I18N::Game::EnterYourWEBZENCOMPassword);
    pMsgBox->AddCallbackFunc(BindCallback(this, &SEASON3B::CStorageLockMsgBoxLayout::ReturnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &SEASON3B::CStorageLockMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &SEASON3B::CStorageLockMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CStorageLockMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CStorageLockFinalKeyPadMsgBoxLayout::SetLayout()
{
    CNewUIKeyPadMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(KEYPAD_TYPE_LOCK_FINAL, g_iLengthAuthorityCode))
        return false;

    pMsgBox->AddMsg(I18N::Game::EnterYourWEBZENCOMPassword);
    pMsgBox->AddMsg(I18N::Game::EnterYourWEBZENCOMPassword697);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CStorageLockFinalKeyPadMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CStorageLockFinalKeyPadMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CStorageLockFinalKeyPadMsgBoxLayout::CancelBtnDown),
        MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CStorageLockFinalKeyPadMsgBoxLayout::CancelBtnDown),
        MSGBOX_EVENT_PRESSKEY_ESC);

    return true;
}

bool SEASON3B::CStorageUnlockMsgBoxLayout::SetLayout()
{
    CNewUITextInputMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (!pMsgBox->CreateModernPasswordInput(g_iLengthAuthorityCode))
        return false;

    pMsgBox->AddMsg(I18N::Game::WarehouseLockUnlock);
    pMsgBox->AddMsg(I18N::Game::EnterYourWEBZENCOMPassword697);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CStorageUnlockMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CStorageUnlockMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CStorageUnlockMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CStorageUnlockMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CStorageUnlockKeyPadMsgBoxLayout::SetLayout()
{
    CNewUIKeyPadMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(KEYPAD_TYPE_UNLOCK, g_iLengthAuthorityCode))
        return false;

    pMsgBox->AddMsg(I18N::Game::WarehouseLockUnlock);
    pMsgBox->AddMsg(I18N::Game::EnterYourWEBZENCOMPassword697);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CStorageUnlockKeyPadMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CStorageUnlockKeyPadMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CStorageUnlockKeyPadMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CStorageUnlockKeyPadMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);

    return true;
}

bool SEASON3B::CUseFruitCheckMsgBoxLayout::SetLayout()
{
    CUseFruitCheckMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CGemIntegrationMsgBoxLayout::SetLayout()
{
    CGemIntegrationMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CGemIntegrationUnityMsgBoxLayout::SetLayout()
{
    CGemIntegrationUnityMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CGemIntegrationDisjointMsgBoxLayout::SetLayout()
{
    CGemIntegrationDisjointMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CBloodCastleResultMsgBoxLayout::SetLayout()
{
    CBloodCastleResultMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CDevilSquareRankMsgBoxLayout::SetLayout()
{
    CDevilSquareRankMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CChaosCastleResultMsgBoxLayout::SetLayout()
{
    CChaosCastleResultMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CChaosMixMenuMsgBoxLayout::SetLayout()
{
    CChaosMixMenuMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CDialogMsgBoxLayout::SetLayout()
{
    CDialogMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CCrownSwitchPopLayout::SetLayout()
{
    CProgressMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(3000))
        return false;

    pMsgBox->AddMsg(I18N::Game::CrownSwitchHasBeenReleased);

    return true;
}

bool SEASON3B::CCrownSwitchPushLayout::SetLayout()
{
    CProgressMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(3000))
        return false;

    pMsgBox->AddMsg(I18N::Game::CrownSwitchHasBeenActivated);

    return true;
}

bool SEASON3B::CCrownSwitchOtherPushLayout::SetLayout()
{
    CProgressMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(3000))
        return false;

    return true;
}

bool SEASON3B::CSealRegisterStartLayout::SetLayout()
{
    CProgressMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CSealRegisterSuccessLayout::SetLayout()
{
    CProgressMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(3000))
        return false;

    pMsgBox->AddMsg(I18N::Game::OfficialSealRegistrationIsSuccessful);

    return true;
}

bool SEASON3B::CSealRegisterFailLayout::SetLayout()
{
    CProgressMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(3000))
        return false;

    return true;
}

bool SEASON3B::CSealRegisterOtherLayout::SetLayout()
{
    CProgressMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(3000))
        return false;

    pMsgBox->AddMsg(I18N::Game::AnotherCharacterIsRegisteringTheOfficialSeal);

    return true;
}

bool SEASON3B::CSealRegisterOtherCampLayout::SetLayout()
{
    CProgressMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(3000))
        return false;

    pMsgBox->AddMsg(I18N::Game::OtherSiegeTeamIsRunningTheCrownSwitch);

    return true;
}

bool SEASON3B::CCrownDefenseRemoveLayout::SetLayout()
{
    CProgressMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(3000))
        return false;

    pMsgBox->AddMsg(I18N::Game::ShieldOfTheCrownHasBeenRemoved);

    return true;
}

bool SEASON3B::CCrownDefenseCreateLayout::SetLayout()
{
    CProgressMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(3000))
        return false;

    pMsgBox->AddMsg(I18N::Game::ShieldOfTheCrownHasBeenActivated);

    return true;
}

bool SEASON3B::CCursedTempleHolicItemGetLayout::SetLayout()
{
    CCursedTempleProgressMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(10000))
        return false;

    pMsgBox->AddMsg(I18N::Game::YouAreCurrentGainingTheSacredItem);

    return true;
}

bool SEASON3B::CCursedTempleHolicItemSaveLayout::SetLayout()
{
    CCursedTempleProgressMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(10000))
        return false;

    pMsgBox->AddMsg(I18N::Game::YouAreCurrentlyStoringTheSacredItem);

    return true;
}

bool SEASON3B::CTrainerMenuMsgBoxLayout::SetLayout()
{
    CTrainerMenuMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CTrainerRecoverMsgBoxLayout::SetLayout()
{
    CTrainerRecoverMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CElpisMsgBoxLayout::SetLayout()
{
    CElpisMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CSystemMenuMsgBoxLayout::SetLayout()
{
    CSystemMenuMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CDuelMsgBoxLayout::SetLayout()
{
    CDuelMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CDuelResultMsgBoxLayout::SetLayout()
{
    CDuelResultMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool CCherryBlossomMsgBoxLayout::SetLayout()
{
    CCherryBlossomMsgBox *pMsgBox = GetMsgBox();
    if (pMsgBox == nullptr)
    {
        return false;
    }

    return pMsgBox->Create();
}

bool SEASON3B::CSeedMasterMenuMsgBoxLayout::SetLayout()
{
    CSeedMasterMenuMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CSeedInvestigatorMenuMsgBoxLayout::SetLayout()
{
    CSeedInvestigatorMenuMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CResetCharacterPointMsgBoxLayout::SetLayout()
{
    CResetCharacterPointMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CDelgardoMainMenuMsgBoxLayout::SetLayout()
{
    CDelgardoMainMenuMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CLuckyTradeMenuMsgBoxLayout::SetLayout()
{
    CLuckyTradeMenuMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CLuckyTradeMenuMsgBox::Render()
{
    return m_ModernMenu.Record(LegacyRender());
}

//////////////////////////////////////////////////////////////////////////

bool SEASON3B::CTrainerMenuMsgBox::Render()
{
    return m_ModernMenu.Record(LegacyRender());
}

bool SEASON3B::CTrainerRecoverMsgBox::Render()
{
    return m_ModernMenu.Record(LegacyRender());
}

bool SEASON3B::CElpisMsgBox::Render()
{
    return m_ModernMenu.Record(LegacyRender());
}

bool SEASON3B::CSeedMasterMenuMsgBox::Render()
{
    return m_ModernMenu.Record(LegacyRender());
}

bool SEASON3B::CSeedInvestigatorMenuMsgBox::Render()
{
    return m_ModernMenu.Record(LegacyRender());
}

bool SEASON3B::CGuildBreakPasswordMsgBoxLayout::SetLayout()
{
    if (member_.empty())
        member_ = GuildList[DeleteIndex].Name;
    submitted_ = false;
    CNewUITextInputMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (!pMsgBox->CreateModernPasswordInput(g_iLengthAuthorityCode))
        return false;

    pMsgBox->AddMsg(I18N::Game::IfYouWantToLeaveYourGuild);
    pMsgBox->AddMsg(I18N::Game::PleaseEnterYourWEBZENCOMPassword);

    pMsgBox->AddCallbackFunc(
        BindCallback(this, &SEASON3B::CGuildBreakPasswordMsgBoxLayout::ReturnDown),
        MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(
        BindCallback(this, &SEASON3B::CGuildBreakPasswordMsgBoxLayout::OkBtnDown),
        MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(
        BindCallback(this, &SEASON3B::CGuildBreakPasswordMsgBoxLayout::CancelBtnDown),
        MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildBreakPasswordMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

void SEASON3B::CGuild_ToPerson_Position::StageModernContent()
{
    panel_.SetText("tfTitle", I18N::Game::Position);
    panel_.SetText("btnType1-label", I18N::Game::AppointAsAssistantGuildMaster);
    panel_.SetText("btnType2-label", I18N::Game::AppointAsABattleMaster);
    panel_.SetText("btnOk-label", I18N::Game::OK);
    panel_.SetText("btnCancel-label", I18N::Game::Cancel);
    wchar_t text[256]{};
    mu_swprintf(text, I18N::Game::SAsAS, member_.c_str(),
                role_ == G_SUB_MASTER ? I18N::Game::AssistM : I18N::Game::BattleM);
    panel_.SetText("taPosMent", std::wstring(text) + L"\n" + I18N::Game::DoYouWantToAppoint);
}

bool SEASON3B::CGuild_ToPerson_Position::Render()
{
    return panel_.Record(LegacyRender());
}
bool SEASON3B::CGuild_ToPerson_Position::PrepareModernUiOnWorker(int width, int height)
{
    if (!panel_.PrepareOnWorker(width, height))
        return false;
    const auto bounds = panel_.Bounds();
    SetPos(static_cast<int>(bounds.x), static_cast<int>(bounds.y));
    SetSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    return true;
}

bool SEASON3B::CGuild_ToPerson_PositionLayout::SetLayout()
{
    CGuild_ToPerson_Position *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create())
        return false;

    return true;
}

bool SEASON3B::CDelgardoMainMenuMsgBox::Render()
{
    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    RenderFrame();
    RenderTexts();
    RenderButtons();
    DisableAlphaBlend();
    return true;
}

void SEASON3B::CDelgardoMainMenuMsgBox::SetButtonInfo()
{
    float x, y, width, height;

    float msgboxhalfwidth = (GetSize().cx / 2.f);
    float btnhalfwidth = MSGBOX_BTN_EMPTY_WIDTH / 2.f;

    width = MSGBOX_BTN_EMPTY_WIDTH + 20;
    height = MSGBOX_BTN_EMPTY_HEIGHT;
    btnhalfwidth = width / 2.f;
    x = GetPos().x + msgboxhalfwidth - btnhalfwidth;
    y = GetPos().y + 85;
    m_BtnReg.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY, x, y, width, height,
                     CNewUIMessageBoxButton::MSGBOX_BTN_SIZE_EMPTY);
    m_BtnReg.SetText(I18N::Game::LuckyCoinRegistration);

    y = GetPos().y + 120;
    m_BtnExchange.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY, x, y, width, height,
                          CNewUIMessageBoxButton::MSGBOX_BTN_SIZE_EMPTY);
    m_BtnExchange.SetText(I18N::Game::LuckyCoinExchange);

    width = MSGBOX_BTN_EMPTY_SMALL_WIDTH;
    btnhalfwidth = width / 2.f;
    x = GetPos().x + msgboxhalfwidth - btnhalfwidth;
    y = GetPos().y + GetSize().cy - (MSGBOX_BTN_EMPTY_HEIGHT + MSGBOX_BTN_BOTTOM_BLANK);
    m_BtnExit.SetInfo(CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY_SMALL, x, y, width, height,
                      CNewUIMessageBoxButton::MSGBOX_BTN_SIZE_EMPTY_SMALL);
    m_BtnExit.SetText(I18N::Game::Close388);
}

void SEASON3B::CDelgardoMainMenuMsgBox::RenderFrame()
{
    float x, y, width, height;

    x = GetPos().x;
    y = GetPos().y + 2.f, width = GetSize().cx - MSGBOX_BACK_BLANK_WIDTH;
    height = GetSize().cy - MSGBOX_BACK_BLANK_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK, x, y, width, height);

    x = GetPos().x;
    y = GetPos().y, width = MSGBOX_WIDTH;
    height = MSGBOX_TOP_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_TOP_TITLEBAR, x, y, width, height);

    x = GetPos().x;
    y += MSGBOX_TOP_HEIGHT;
    width = MSGBOX_WIDTH;
    height = MSGBOX_MIDDLE_HEIGHT;
    for (int i = 0; i < m_iMiddleCount; ++i)
    {
        RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_MIDDLE, x, y, width, height);
        y += height;
    }

    x = GetPos().x;
    width = MSGBOX_WIDTH;
    height = MSGBOX_BOTTOM_HEIGHT;
    RenderImage(CNewUIMessageBoxMng::IMAGE_MSGBOX_BOTTOM, x, y, width, height);
}

void SEASON3B::CDelgardoMainMenuMsgBox::RenderTexts()
{
    wchar_t szText[256] = {
        0,
    };
    float fPos_x = GetPos().x + 10;
    float fPos_y = GetPos().y + 10;

    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetFont(LegacyFontRole::Bold);
    mu_swprintf(szText, I18N::Game::Delgado);
    g_RenderText.RenderText(fPos_x, fPos_y, szText, MSGBOX_WIDTH - 20.0f, 0, RT3_SORT_CENTER);

    fPos_y += 26;
    g_RenderText.SetFont(LegacyFontRole::Normal);
    mu_swprintf(szText, I18N::Game::RegisterYourLuckyCoinsOr);
    g_RenderText.RenderText(fPos_x, fPos_y + 1 * 12, szText, MSGBOX_WIDTH - 20.0f, 0,
                            RT3_SORT_CENTER);
    mu_swprintf(szText, I18N::Game::UseTheLuckyCoinsYouAlreadyHave);
    g_RenderText.RenderText(fPos_x, fPos_y + 2 * 12, szText, MSGBOX_WIDTH - 20.0f, 0,
                            RT3_SORT_CENTER);
    mu_swprintf(szText, I18N::Game::AndExchangeThemForItems);
    g_RenderText.RenderText(fPos_x, fPos_y + 3 * 12, szText, MSGBOX_WIDTH - 20.0f, 0,
                            RT3_SORT_CENTER);
}

void SEASON3B::CDelgardoMainMenuMsgBox::RenderButtons()
{
    m_BtnReg.Render();
    m_BtnExchange.Render();
    m_BtnExit.Render();
}

void SEASON3B::CChaosMixMenuMsgBox::StageModernContent()
{
    m_ModernMenu.SetText("tfTitle", I18N::Game::SelectMethodOfCombination);
    m_ModernMenu.SetText(
        "taMentType1", std::wstring(I18N::Game::Wings7TypesFruitDevilSInvitation) + L"\n" +
                           std::wstring(I18N::Game::Dinorant1015ItemsCloakOfInvisibility) + L"\n" +
                           std::wstring(I18N::Game::FenrirSHornScrollOfBloodCondorSFeather));
    m_ModernMenu.SetText("taMentType2",
                         std::wstring(I18N::Game::ChaosDragonAxeChaosLightningStaff) + L"\n" +
                             std::wstring(I18N::Game::ChaosNatureBow));
    m_ModernMenu.SetText("taMentType3", I18N::Game::Add380ItemOption);
    m_ModernMenu.SetText("btnType1-label", I18N::Game::RegularCombination);
    m_ModernMenu.SetText("btnType2-label", I18N::Game::ChaosWeaponCombination);
    m_ModernMenu.SetText("btnType3-label", I18N::Game::ItemOptionCombination);
    m_ModernMenu.SetText("btnClose-label", I18N::Game::Close388);
}

bool SEASON3B::CChaosMixMenuMsgBox::PrepareModernUiOnWorker(int width, int height)
{
    if (!m_ModernMenu.PrepareOnWorker(width, height))
        return false;
    const auto bounds = m_ModernMenu.Bounds();
    SetPos(static_cast<int>(bounds.x), static_cast<int>(bounds.y));
    SetSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    return true;
}

void SEASON3B::CTrainerMenuMsgBox::StageModernContent()
{
    m_ModernMenu.SetText("tfTitle", I18N::Game::Trainer);
    m_ModernMenu.SetText("btnRecover-label", I18N::Game::RestoreLifeDurability);
    m_ModernMenu.SetText("btnRevive-label", I18N::Game::ResurrectSpirit);
    m_ModernMenu.SetText("btnClose-label", I18N::Game::Close388);
    wchar_t greeting[256]{};
    mu_swprintf(greeting, I18N::Game::SWhatIsYourCommand, Hero->ID);
    m_ModernMenu.SetText("taMent", std::wstring(I18N::Game::Hi) + L"\n" + greeting);
}

bool SEASON3B::CTrainerMenuMsgBox::PrepareModernUiOnWorker(int width, int height)
{
    if (!m_ModernMenu.PrepareOnWorker(width, height))
        return false;
    const auto bounds = m_ModernMenu.Bounds();
    SetPos(static_cast<int>(bounds.x), static_cast<int>(bounds.y));
    SetSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    return true;
}

void SEASON3B::CTrainerRecoverMsgBox::StageModernContent()
{
    m_ModernMenu.SetText("tfTitle", I18N::Game::Trainer);
    m_ModernMenu.SetText("tfRecoverMent", I18N::Game::SelectThePetToRecoverLife);
    m_ModernMenu.SetText("btnDarkHorseRecover-label", I18N::Game::DarkHorse);
    m_ModernMenu.SetText("btnDarkSpiritRecover-label", I18N::Game::DarkRaven);
    m_ModernMenu.SetText("btnClose-label", I18N::Game::Close388);
    wchar_t fee[256]{};
    sessionUi_.CalcRecoveryZen(REVIVAL_DARKHORSE, fee);
    m_ModernMenu.SetText("tfDarkHorseMent", fee);
    sessionUi_.CalcRecoveryZen(REVIVAL_DARKSPIRIT, fee);
    m_ModernMenu.SetText("tfDarkSpiritMent", fee);
}

bool SEASON3B::CTrainerRecoverMsgBox::PrepareModernUiOnWorker(int width, int height)
{
    if (!m_ModernMenu.PrepareOnWorker(width, height))
        return false;
    const auto bounds = m_ModernMenu.Bounds();
    SetPos(static_cast<int>(bounds.x), static_cast<int>(bounds.y));
    SetSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    return true;
}

void SEASON3B::CSeedMasterMenuMsgBox::StageModernContent()
{
    m_ModernMenu.SetText("tfTitle", I18N::Game::SeedMaster);
    m_ModernMenu.SetText("taMent", std::wstring(I18N::Game::ExtractTheSeedOrTheSeedSphere) + L"\n" +
                                       std::wstring(I18N::Game::YouMayAssemblyThemTogether));
    m_ModernMenu.SetText("btnType1-label", I18N::Game::SeedExtraction);
    m_ModernMenu.SetText("btnType2-label", I18N::Game::SeedSphereAssembly);
    m_ModernMenu.SetText("btnClose-label", I18N::Game::Close388);
}

bool SEASON3B::CSeedMasterMenuMsgBox::PrepareModernUiOnWorker(int width, int height)
{
    if (!m_ModernMenu.PrepareOnWorker(width, height))
        return false;
    const auto bounds = m_ModernMenu.Bounds();
    SetPos(static_cast<int>(bounds.x), static_cast<int>(bounds.y));
    SetSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    return true;
}

void SEASON3B::CSeedInvestigatorMenuMsgBox::StageModernContent()
{
    m_ModernMenu.SetText("tfTitle", I18N::Game::SeedResearcher);
    m_ModernMenu.SetText("taMent", std::wstring(I18N::Game::EitherApplyTheSeedSphere) + L"\n" +
                                       std::wstring(I18N::Game::OrDestroyTheSeedSphereAccordingly));
    m_ModernMenu.SetText("btnType1-label", I18N::Game::SeedSphereApplication);
    m_ModernMenu.SetText("btnType2-label", I18N::Game::SeedSphereDestruction);
    m_ModernMenu.SetText("btnClose-label", I18N::Game::Close388);
}

bool SEASON3B::CSeedInvestigatorMenuMsgBox::PrepareModernUiOnWorker(int width, int height)
{
    if (!m_ModernMenu.PrepareOnWorker(width, height))
        return false;
    const auto bounds = m_ModernMenu.Bounds();
    SetPos(static_cast<int>(bounds.x), static_cast<int>(bounds.y));
    SetSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    return true;
}

void SEASON3B::CLuckyTradeMenuMsgBox::StageModernContent()
{
    m_ModernMenu.SetText("tfTitle", I18N::Game::ExchangeLuckyItem);
    m_ModernMenu.SetText("taMent", I18N::Game::CanExchangeWithALuckyItemOrRefineIt);
    m_ModernMenu.SetText("btnType1-label", I18N::Game::ExchangeLuckyItem);
    m_ModernMenu.SetText("btnType2-label", I18N::Game::RefineLuckyItem);
    m_ModernMenu.SetText("btnClose-label", I18N::Game::Close388);
}

bool SEASON3B::CLuckyTradeMenuMsgBox::PrepareModernUiOnWorker(int width, int height)
{
    if (!m_ModernMenu.PrepareOnWorker(width, height))
        return false;
    const auto bounds = m_ModernMenu.Bounds();
    SetPos(static_cast<int>(bounds.x), static_cast<int>(bounds.y));
    SetSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    return true;
}

void SEASON3B::CElpisMsgBox::StageModernContent()
{
    m_ModernMenu.SetText("tfTitle", I18N::Game::Elpis);
    m_ModernMenu.SetText("btnSelect1-label", I18N::Game::AboutRefinery);
    m_ModernMenu.SetText("btnSelect2-label", I18N::Game::JewelOfHarmony);
    m_ModernMenu.SetText("btnSelect3-label", I18N::Game::RefineGemstone);
    const wchar_t *text = I18N::Game::WhatWouldYouLikeToKnow;
    if (m_iMessageType == MSGBOX_EVENT_USER_CUSTOM_ELPIS_ABOUT_REFINARY)
        text = I18N::Game::GemstoneOfJewelOfHarmonyHas;
    if (m_iMessageType == MSGBOX_EVENT_USER_CUSTOM_ELPIS_ABOUT_JEWELOFHARMONY)
        text = I18N::Game::NewPowerCanBeGrantedTo;
    m_ModernMenu.SetText("taDialogueMent", text);
}

bool SEASON3B::CElpisMsgBox::PrepareModernUiOnWorker(int width, int height)
{
    if (!m_ModernMenu.PrepareOnWorker(width, height))
        return false;
    const auto bounds = m_ModernMenu.Bounds();
    SetPos(static_cast<int>(bounds.x), static_cast<int>(bounds.y));
    SetSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    return true;
}

void SEASON3B::CNewUIKeyPadMsgBox::StageModernContent()
{
    m_ModernMenu.SetText("tfNumTitle", m_MsgTextList.empty() ? L"" : m_MsgTextList.front()->strMsg);
    std::wstring message;
    for (std::size_t i = 1; i < m_MsgTextList.size(); ++i)
    {
        if (!message.empty())
            message += L"\n";
        message += m_MsgTextList[i]->strMsg;
    }
    m_ModernMenu.SetText("taNumMent", message);
    m_ModernMenu.SetText("btnPassOk-label", I18N::Game::OK);
    m_ModernMenu.SetText("btnPassCancel-label", I18N::Game::Cancel);
}

bool SEASON3B::CNewUIKeyPadMsgBox::PrepareModernUiOnWorker(int width, int height)
{
    if (!m_ModernMenu.PrepareOnWorker(width, height))
        return false;
    const auto bounds = m_ModernMenu.Bounds();
    SetPos(static_cast<int>(bounds.x), static_cast<int>(bounds.y));
    SetSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    return true;
}

bool SEASON3B::CGemIntegrationMsgBox::PrepareModernUiOnWorker(int width, int height)
{
    if (!panel_.PrepareOnWorker(width, height))
        return false;
    const auto bounds = panel_.Bounds();
    SetPos(static_cast<int>(bounds.x), static_cast<int>(bounds.y));
    SetSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    return true;
}

bool SEASON3B::CGemIntegrationUnityMsgBox::PrepareModernUiOnWorker(int width, int height)
{
    const bool choosingJewel = m_cGemType == COMGEM::eNOGEM;
    if (!panel_.PrepareOnWorker(width, height, choosingJewel) ||
        !unitPanel_.PrepareOnWorker(width, height, !choosingJewel))
        return false;
    const auto bounds = ActivePanel().Bounds();
    SetPos(static_cast<int>(bounds.x), static_cast<int>(bounds.y));
    SetSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    return true;
}

bool SEASON3B::CGemIntegrationDisjointMsgBox::PrepareModernUiOnWorker(int width, int height)
{
    if (!panel_.PrepareOnWorker(width, height))
        return false;
    const auto bounds = panel_.Bounds();
    SetPos(static_cast<int>(bounds.x), static_cast<int>(bounds.y));
    SetSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    return true;
}

void SEASON3B::CGemIntegrationMsgBox::StageContent()
{
    if (locale_ == I18N::GetCurrentLocale())
        return;
    locale_ = I18N::GetCurrentLocale();
    panel_.SetText("tfSelectTitle-label", I18N::Game::JewelCombination);
    panel_.SetText("taSelectMent-label", std::wstring(I18N::Game::YouCanCombineOrDissolve) + L"\n" +
                                             std::wstring(I18N::Game::VariousJewels));
    panel_.SetText("btnSelectAttach-label", I18N::Game::JewelCombination);
    panel_.SetText("btnSelectDetach-label", I18N::Game::DismantleJewel);
    panel_.SetText("btnSelectClose-label", I18N::Game::Close388);
}

void SEASON3B::CGemIntegrationUnityMsgBox::StageContent()
{
    if (locale_ == I18N::GetCurrentLocale())
        return;
    locale_ = I18N::GetCurrentLocale();
    panel_.SetText("tfAttachTitle-label", I18N::Game::JewelCombination);
    panel_.SetText("taAttachMent-label", I18N::Game::SelectAJewelToCombine);
    panel_.SetText("btnAttachClose-label", I18N::Game::Close388);
    constexpr int names[COMGEM::eGEMTYPE_END] = {1806, 1807, 3312, 3313, 3314,
                                                 2081, 3315, 3316, 3317, 3318};
    for (int i = 0; i < COMGEM::eGEMTYPE_END; ++i)
        panel_.SetText(("btnAttach" + std::to_string(i + 1) + "-label").c_str(),
                       I18N::Game::Lookup(names[i]));
    unitPanel_.SetText("tfUnitTitle-label", I18N::Game::JewelCombination);
    unitPanel_.SetText("tfUnitMent-label", I18N::Game::ChooseANumberButtonToCombine);
    unitPanel_.SetText("btnUnitClose-label", I18N::Game::Close388);
    for (int i = 0; i < COMGEM::eCOMTYPE_END; ++i)
    {
        wchar_t cost[256]{};
        mu_swprintf(cost, I18N::Game::CombinationCostDZen, 500000 * (i + 1));
        unitPanel_.SetText(("tfUnitMent" + std::to_string(i + 1) + "-label").c_str(), cost);
        unitPanel_.SetText(("btnUnit" + std::to_string(i + 1) + "-label").c_str(),
                           std::to_wstring(10 * (i + 1)));
    }
}

void SEASON3B::CGemIntegrationDisjointMsgBox::StageContent()
{
    if (locale_ == I18N::GetCurrentLocale())
        return;
    locale_ = I18N::GetCurrentLocale();
    panel_.SetText("tfDetachTitle-label", I18N::Game::DismantleJewel);
    panel_.SetText("btnDetachRun-label", I18N::Game::DismantleJewel);
    panel_.SetText("btnDetachClose-label", I18N::Game::Close388);
    UI::Modern::RmlMuScrollingList::Data rows;
    for (const auto &gem : sessionUi_.UnmixGemList().Items())
    {
        wchar_t text[MAX_GLOBAL_TEXT_STRING]{};
        if (const ITEM *item = FindInventoryItemBySlot(gem.m_iInvenIdx))
            mu_swprintf(
                text, L"%ls, %d",
                I18N::Game::Lookup(GetJewelIndex(Check_Jewel(item->Type), COMGEM::eGEM_NAME)),
                (gem.m_cLevel + 1) * 10);
        rows.push_back({text});
    }
    panel_.SetListData(rows);
}

//*****************************************************************************
//*****************************************************************************

void CMsgWin::SetPosition(int nXCoord, int nYCoord)
{
    m_panel.SetPosition(nXCoord, nYCoord);
}

void CMsgWin::Show(bool bShow)
{
    CWin::Show(bShow);
    m_panel.Show(bShow);
    enterKeyHeld_ = IsPress(VK_RETURN) || IsRepeat(VK_RETURN);
    escapeKeyHeld_ = IsPress(VK_ESCAPE) || IsRepeat(VK_ESCAPE);
}

void CMsgWin::RenderControls()
{
    (void)m_panel.Record(LegacyRender());
}

bool CMsgWin::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight)
{
    m_panel.SetPosition((viewportWidth - UI::Modern::RmlMessageBoxPanel::Width()) / 2,
                        (viewportHeight - UI::Modern::RmlMessageBoxPanel::Height()) / 2);
    const UI::Modern::RmlMessageBoxContent content{m_nMsgLine > 0 ? m_aszMsg[0] : L"",
                                                   m_nMsgLine > 1 ? m_aszMsg[1] : L"",
                                                   I18N::Game::OK, I18N::Game::Cancel};
    return m_panel.PrepareOnWorker(viewportWidth, viewportHeight, content);
}

//*****************************************************************************
//*****************************************************************************

void CServerMsgWin::SetPosition(int x, int y)
{
    CWin::SetPosition(x, y);
    panel_.SetPosition(x, y);
}

void CServerMsgWin::Show(bool show)
{
    CWin::Show(show);
    panel_.Show(show);
}

bool CServerMsgWin::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight)
{
    return panel_.PrepareOnWorker(viewportWidth, viewportHeight, content_);
}

void CServerMsgWin::RenderControls()
{
    (void)panel_.Record(LegacyRender());
}

//*****************************************************************************
//*****************************************************************************

namespace SystemMenu = UI::Modern::PC::SystemMenu;

void CSysMenuWin::SetPosition(int x, int y)
{
    CWin::SetPosition(x, y);
    m_modernPanel.SetPosition(x, y);
}

void CSysMenuWin::Show(bool show)
{
    CWin::Show(show);
    m_modernPanel.Show(show);
}

bool CSysMenuWin::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight)
{
    m_modernPanel.SetPosition((viewportWidth - GetWidth()) / 2, (viewportHeight - GetHeight()) / 2);
    const SystemMenu::RmlSystemMenuContent content{
        I18N::Game::SystemMenu,      I18N::Game::ExitGame,  I18N::Game::SelectServer,
        I18N::Game::SwitchCharacter, I18N::Game::Option385, I18N::Game::Close388};
    return m_modernPanel.PrepareOnWorker(viewportWidth, viewportHeight, content);
}

void CSysMenuWin::RenderControls()
{
    (void)m_modernPanel.Record(LegacyRender());
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void RenderColor(float x, float y, float Width, float Height, float Alpha, int Flag);

////////////////////////////////////////////////////////////////////////////////////////////////////

extern void MoveCharacter(CHARACTER *c, OBJECT *o);

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
////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////

bool CUITextInputWindow::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight,
                                                 bool visible)
{
    m_ModernPanel.SetPosition(GetPosition_x(), GetPosition_y());
    m_ModernPanel.Show(visible);
    const UI::Modern::RmlMessageBoxContent content{GetTitle(), L"", I18N::Game::OK,
                                                   I18N::Game::Cancel, L""};
    return m_ModernPanel.PrepareOnWorker(viewportWidth, viewportHeight, content);
}

bool CUITextInputWindow::RecordModernUi(LegacyRenderFacade &facade) const
{
    return m_ModernPanel.Record(facade);
}

void CUITextInputWindow::RenderSub()
{
    if (GetState() == UISTATE_MOVE || GetState() == UISTATE_RESIZE)
    {
        m_AddButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_CancelButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_TextInputBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    }

    m_AddButton.Render();
    m_CancelButton.Render();
    EnableAlphaTest();
    m_TextInputBox.Render();
    DisableAlphaBlend();
}

bool CUIQuestionWindow::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight, bool visible)
{
    m_ModernPanel.SetPosition(GetPosition_x(), GetPosition_y());
    m_ModernPanel.Show(visible);
    const UI::Modern::RmlMessageBoxContent content{m_szCaption[0], m_szCaption[1], I18N::Game::OK,
                                                   I18N::Game::Cancel, L""};
    return m_ModernPanel.PrepareOnWorker(viewportWidth, viewportHeight, content);
}

bool CUIQuestionWindow::RecordModernUi(LegacyRenderFacade &facade) const
{
    return m_ModernPanel.Record(facade);
}

void CUIQuestionWindow::RenderSub()
{
    if (GetState() == UISTATE_MOVE || GetState() == UISTATE_RESIZE)
    {
        m_AddButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
        m_CancelButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    }

    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetBgColor(0);

    SIZE TextSize;
    g_RenderText.RenderText(RPos_x(5), RPos_y(8), m_szCaption[0], 0, 0, RT3_SORT_LEFT, &TextSize);
    if (m_szCaption[1][0] != '\0')
        g_RenderText.RenderText(RPos_x(5), RPos_y(8 + TextSize.cy), m_szCaption[1]);

    m_AddButton.Render();
    if (m_iDialogType == 0)
        m_CancelButton.Render();
}

//////////////////////////////////////////////////////////////////////

extern int DoBreakUpGuildAction_New(POPUP_RESULT Result);

void SEASON3B::CNewUIMessageBoxButton::SetText(const wchar_t *strText)
{
    if (wcslen(strText) > 0)
    {
        m_strText = strText;
    }
}

void SEASON3B::CNewUIMessageBoxButton::Render()
{
    if (m_bEnable == false)
    {
        glColor4f(0.8f, 0.8f, 0.8f, 0.9f);
    }
    else
    {
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    }

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
    const SessionBitmapMetadata pImage = Bitmaps[m_dwTexType];
    if (!IsValid(pImage.Asset))
    {
        return;
    }
    RenderBitmap(
        m_dwTexType, m_x, m_y, m_width, m_height, (0.5f / (float)pImage.Width),
        ((static_cast<float>(m_EventState) * m_fButtonHeight + 0.5f) / (float)pImage.Height),
        (m_fButtonWidth - 0.5f) / (float)pImage.Width - (0.5f / (float)pImage.Width),
        (m_fButtonHeight - 0.5f) / (float)pImage.Height - (0.5f / (float)pImage.Height));
#else  //KJH_ADD_INGAMESHOP_UI_SYSTEM
    float fv = 0.f;
    float fBtnOrigWidth = 0.f;
    float fBtnOrigHeight = 0.f;

    if (m_dwSizeType == MSGBOX_BTN_SIZE_OK)
    {
        fBtnOrigWidth = MSGBOX_BTN_WIDTH;
        fBtnOrigHeight = MSGBOX_BTN_HEIGHT;
    }
    else if (m_dwSizeType == MSGBOX_BTN_SIZE_EMPTY)
    {
        fBtnOrigWidth = MSGBOX_BTN_EMPTY_WIDTH;
        fBtnOrigHeight = MSGBOX_BTN_EMPTY_HEIGHT;
    }
    else if (m_dwSizeType == MSGBOX_BTN_SIZE_EMPTY_SMALL)
    {
        fBtnOrigWidth = MSGBOX_BTN_EMPTY_SMALL_WIDTH;
        fBtnOrigHeight = MSGBOX_BTN_EMPTY_HEIGHT;
    }
    else if (m_dwSizeType == MSGBOX_BTN_SIZE_EMPTY_BIG)
    {
        fBtnOrigWidth = MSGBOX_BTN_EMPTY_BIG_WIDTH;
        fBtnOrigHeight = MSGBOX_BTN_EMPTY_HEIGHT;
    }
    if (m_EventState == EVENT_BTN_HOVER)
    {
        fv = m_height * 1.f / 128.f;
    }
    else if (m_EventState == EVENT_BTN_DOWN)
    {
        fv = m_height * 2.f / 128.f;
    }
    else
    {
        fv = 0.f;
    }

    if (m_bEnable == false)
    {
        glColor4f(0.6f, 0.6f, 0.6f, 0.6f);
    }

    if (m_dwSizeType == MSGBOX_BTN_SIZE_OK || m_dwSizeType == MSGBOX_BTN_SIZE_EMPTY_SMALL)
    {
        RenderBitmap(m_dwTexType, m_x, m_y, m_width, m_height, 0.f, fv, fBtnOrigWidth / 64.f,
                     fBtnOrigHeight / 128.f);
    }
    else if (m_dwSizeType == MSGBOX_BTN_SIZE_EMPTY)
    {
        RenderBitmap(m_dwTexType, m_x, m_y, m_width, m_height, 0.f, fv, fBtnOrigWidth / 128.f,
                     fBtnOrigHeight / 128.f);
    }
    else if (m_dwSizeType == MSGBOX_BTN_SIZE_EMPTY_BIG)
    {
        RenderBitmap(m_dwTexType, m_x, m_y, m_width, m_height, 0.f, fv, fBtnOrigWidth / 256.f,
                     fBtnOrigHeight / 128.f);
    }
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

    if (m_strText.size() > 0)
    {
#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM
        SIZE Fontsize;

        g_RenderText.SetFont(LegacyFontRole::Normal);

        if (m_bEnable == false)
        {
            g_RenderText.SetTextColor(128, 128, 128, 255);
        }
        else
        {
            g_RenderText.SetTextColor(255, 255, 255, 255);
        }

        g_RenderText.SetBgColor(0);

        g_RenderText.MeasureText(m_strText.c_str(), m_strText.size(), &Fontsize);

        Fontsize.cx = Fontsize.cx / ((float)WindowWidth / REFERENCE_WIDTH);
        Fontsize.cy = Fontsize.cy / ((float)WindowHeight / REFERENCE_HEIGHT);

        int x = m_x + ((m_width / 2) - (Fontsize.cx / 2));
        int y = m_y + ((m_height / 2) - (Fontsize.cy / 2));

        if ((m_bClickEffect == true) && (m_EventState == EVENT_BTN_DOWN))
        {
            g_RenderText.RenderText(x + m_iMoveTextPosX + 1, y + m_iMoveTextPosY + 1,
                                    m_strText.c_str());
        }
        else
        {
            g_RenderText.RenderText(x + m_iMoveTextPosX, y + m_iMoveTextPosY, m_strText.c_str());
        }
#else  // KJH_ADD_INGAMESHOP_UI_SYSTEM
        int x;
        SIZE TextSize;
        size_t TextExtentWidth;

        g_RenderText.MeasureText(m_strText.c_str(), m_strText.size(), &TextSize);
        TextExtentWidth = (size_t)(TextSize.cx / g_fScreenRate_x);

        g_RenderText.SetFont(LegacyFontRole::Normal);
        g_RenderText.SetTextColor(255, 255, 255, 255);
        g_RenderText.SetBgColor(0);

        x = m_x + (m_width / 2) - (TextExtentWidth / 2);

        g_RenderText.RenderText(x, m_y + 10, m_strText.c_str());
#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM
    }
}

bool SEASON3B::CNewUICommonMessageBox::Render()
{
    return g_MessageBox.ModernMessageBoxPanel().Record(LegacyRender());
}

bool SEASON3B::CNewUICommonMessageBox::PrepareModernUiOnWorker(int viewportWidth,
                                                               int viewportHeight)
{
    UI::Modern::RmlMessageBoxPanel &panel = g_MessageBox.ModernMessageBoxPanel();
    panel.SetPosition(GetPos().x, GetPos().y);
    panel.Show(true);
    const UI::Modern::RmlMessageBoxContent content{m_s16Message, L"", I18N::Game::OK,
                                                   I18N::Game::Cancel, m_s16Title};
    if (!panel.PrepareOnWorker(viewportWidth, viewportHeight, content))
    {
        return false;
    }
    CNewUIMessageBoxBase::SetSize(UI::Modern::RmlMessageBoxPanel::CautionWidth(),
                                  panel.CurrentHeight());
    CNewUIMessageBoxBase::SetPos(panel.PositionX(), panel.PositionY());
    return true;
}

bool SEASON3B::CNewUI3DItemCommonMsgBox::Render()
{
    return panel_.Record(LegacyRender());
}

void SEASON3B::CNewUI3DItemCommonMsgBox::Render3D()
{
    const auto slot = panel_.SlotBounds("isItem");
    if (slot.width <= 0 || slot.height <= 0)
        return;
    RenderItem3D(slot.x, slot.y, slot.width, slot.height, m_Item.Type, m_Item.Level,
                 m_Item.ExcellentFlags, m_Item.AncientDiscriminator, true);
}

bool SEASON3B::CNewUI3DItemCommonMsgBox::PrepareModernUiOnWorker(int width, int height)
{
    if (!panel_.PrepareOnWorker(width, height))
        return false;
    const auto bounds = panel_.Bounds();
    SetPos(static_cast<int>(bounds.x), static_cast<int>(bounds.y));
    SetSize(static_cast<int>(bounds.width), static_cast<int>(bounds.height));
    return true;
}

bool SEASON3B::CServerLostMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK, 10.f))
        return false;

    pMsgBox->AddMsg(I18N::Game::YouAreDisconnectedFromTheServer);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CServerLostMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->RemoveCallbackFunc(MSGBOX_EVENT_PRESSKEY_ESC);

    return true;
}

bool SEASON3B::CGuildRequestMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildRequestMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildRequestMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildRequestMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildRequestMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    return true;
}

bool SEASON3B::CGuildFireMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildFireMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(pMsgBox, &CNewUICommonMessageBox::Close),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildFireMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(pMsgBox, &CNewUICommonMessageBox::Close),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CMapEnterWerwolfMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddMsg(I18N::Game::WerewolfGuardsman, RGBA(254, 176, 72, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddMsg(L" ");
    pMsgBox->AddMsg(I18N::Game::DoYouEvenKnowAboutMe, RGBA(170, 218, 146, 255));
    pMsgBox->AddMsg(L" ");
    pMsgBox->AddMsg(I18N::Game::IfYouHavePassedThroughThe);
    pMsgBox->AddMsg(L" ");
    pMsgBox->AddMsg(I18N::Game::YouMustBeLocatedCloselyTogether);
    pMsgBox->AddMsg(L" ");
    pMsgBox->AddMsg(I18N::Game::InOrderToReceiveHelpFrom);

    BYTE byQuestState = g_csQuest.getQuestState2(QUEST_3RD_CHANGE_UP_2);
    if (QUEST_ING != byQuestState && QUEST_END != byQuestState)
    {
        pMsgBox->LockOkButton();
    }

    pMsgBox->AddCallbackFunc(BindCallback(this, &CMapEnterWerwolfMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CMapEnterWerwolfMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    return true;
}

bool CMapEnterGateKeeperMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddMsg(I18N::Game::Gatekeeper, 0xFF49B0FF, MSGBOX_FONT_BOLD);
    pMsgBox->AddMsg(L" ");
    pMsgBox->AddMsg(I18N::Game::HmmWhoAreYouIMConfusedAreYouEvenApprovedOfBalgass, 0xFF61F191);
    pMsgBox->AddMsg(L" ");
    pMsgBox->AddMsg(I18N::Game::LugadrS12ApostlesAreHelping);
    pMsgBox->AddMsg(L" ");
    pMsgBox->AddMsg(I18N::Game::ApostleDevinSThirdMissionRequest);

    BYTE byQuestState = g_csQuest.getQuestState2(QUEST_3RD_CHANGE_UP_3);
    if (QUEST_ING != byQuestState)
    {
        pMsgBox->LockOkButton();
    }

    pMsgBox->AddCallbackFunc(BindCallback(this, &CMapEnterGateKeeperMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CMapEnterGateKeeperMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    return true;
}

bool SEASON3B::CPartyMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->UseS16Caution(I18N::Game::Party, I18N::Game::SomeoneRequestsYouToJoinTheirAParty);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPartyMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPartyMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPartyMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPartyMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CTradeMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    wchar_t szYourID[MAX_USERNAME_SIZE + 1];
    g_pTrade->GetYourID(szYourID);
    std::wstring message = szYourID;
    message += L" ";
    message += I18N::Game::WouldLikeToTradeWithYou;
    pMsgBox->UseS16Caution(I18N::Game::Trade, message);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CTradeMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CTradeMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CTradeMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CTradeMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CTradeAlertMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    DWORD adwColor[4] = {RGBA(255, 178, 0, 255), RGBA(255, 178, 0, 255), RGBA(255, 178, 0, 255),
                         RGBA(255, 32, 32, 255)};

    for (int i = 0; i < 4; ++i)
        pMsgBox->AddMsg(I18N::Game::Lookup(371 + i), adwColor[i], MSGBOX_FONT_BOLD);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CTradeAlertMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CTradeAlertMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CTradeAlertMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CTradeAlertMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CGuildWarMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL, 15.f))
        return false;

    wchar_t strText[128];
    mu_swprintf(strText, I18N::Game::SGuildChallengesYou, GuildWarName);
    pMsgBox->AddMsg(strText);
    pMsgBox->AddMsg(I18N::Game::ToAGuildWar);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildWarMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildWarMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildWarMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildWarMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CBattleSoccerMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL, 15.f))
        return false;

    wchar_t strText[128];
    mu_swprintf(strText, I18N::Game::SGuildChallengesYou, GuildWarName);
    pMsgBox->AddMsg(strText);
    pMsgBox->AddMsg(I18N::Game::YouHaveBeenChallengedToBattleSoccer);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CBattleSoccerMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CBattleSoccerMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CBattleSoccerMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CBattleSoccerMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CServerImmigrationErrorMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::ThePasswordYouHaveEnteredIsIncorrect);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CServerImmigrationErrorMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);

    return true;
}

bool SEASON3B::CPersonalshopCreateMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddMsg(I18N::Game::DoYouWantToOpenAStore);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CPersonalshopCreateMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPersonalshopCreateMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CPersonalshopCreateMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPersonalshopCreateMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CFenrirRepairMsgBoxLayout::SetLayout()
{
    CFenrirRepairMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddMsg(I18N::Game::WouldYouLikeToRepairFenrirSHorn, RGBA(255, 255, 0, 255),
                    MSGBOX_FONT_BOLD);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CFenrirRepairMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CFenrirRepairMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CFenrirRepairMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CFenrirRepairMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CInfinityArrowCancelMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    wchar_t strText[MAX_GLOBAL_TEXT_STRING];
    mu_swprintf(strText, L"%ls%ls", SkillAttribute[AT_SKILL_INFINITY_ARROW].Name,
                I18N::Game::WouldYouLikeToCancel);
    g_iCancelSkillTarget =
        AT_SKILL_INFINITY_ARROW; // todo: is considering master skill required here?

    pMsgBox->AddMsg(strText, RGBA(255, 255, 0, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CInfinityArrowCancelMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CInfinityArrowCancelMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CInfinityArrowCancelMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CInfinityArrowCancelMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CBuffSwellOfMPCancelMsgBoxLayOut::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    wchar_t strText[MAX_GLOBAL_TEXT_STRING];
    mu_swprintf(strText, L"%ls%ls", SkillAttribute[AT_SKILL_EXPANSION_OF_WIZARDRY].Name,
                I18N::Game::WouldYouLikeToCancel);
    g_iCancelSkillTarget = AT_SKILL_EXPANSION_OF_WIZARDRY;

    pMsgBox->AddMsg(strText, RGBA(255, 255, 0, 255), MSGBOX_FONT_BOLD);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CBuffSwellOfMPCancelMsgBoxLayOut::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CBuffSwellOfMPCancelMsgBoxLayOut::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CBuffSwellOfMPCancelMsgBoxLayOut::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CBuffSwellOfMPCancelMsgBoxLayOut::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CGemIntegrationUnityCheckMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddCallbackFunc(BindCallback(this, &CGemIntegrationUnityCheckMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CGemIntegrationUnityCheckMsgBoxLayout::CancelBtnDown),
        MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CGemIntegrationUnityCheckMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CGemIntegrationUnityCheckMsgBoxLayout::CancelBtnDown),
        MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CGemIntegrationUnityResultMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    wchar_t strText[256] = {
        0,
    };
    mu_swprintf(strText, L"%ls%ls %ls", I18N::Game::JewelCombination, I18N::Game::To1816,
                I18N::Game::CongratulationsYouHaveSuccessfully);
    pMsgBox->AddMsg(strText, RGBA(255, 255, 255, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGemIntegrationUnityResultMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);

    return true;
}

bool SEASON3B::CGemIntegrationDisjointCheckMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CGemIntegrationDisjointCheckMsgBoxLayout::OkBtnDown),
        MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CGemIntegrationDisjointCheckMsgBoxLayout::CancelBtnDown),
        MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CGemIntegrationDisjointCheckMsgBoxLayout::OkBtnDown),
        MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CGemIntegrationDisjointCheckMsgBoxLayout::CancelBtnDown),
        MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CGemIntegrationDisjointResultMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    wchar_t strText[256] = {
        0,
    };
    mu_swprintf(strText, L"%ls%ls %ls", I18N::Game::DismantleJewel, I18N::Game::To1816,
                I18N::Game::CongratulationsYouHaveSuccessfully);
    pMsgBox->AddMsg(strText, RGBA(255, 255, 255, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CGemIntegrationDisjointResultMsgBoxLayout::OkBtnDown),
        MSGBOX_EVENT_USER_COMMON_OK);
    return true;
}

bool SEASON3B::CChaosCastleTimeCheckMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddCallbackFunc(BindCallback(this, &CChaosCastleTimeCheckMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CChaosCastleTimeCheckMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CChaosCastleTimeCheckMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CChaosCastleTimeCheckMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    return true;
}

bool SEASON3B::CHarvestEventLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddMsg(I18N::Game::WouldYouLikeToReceiveTheItem, RGBA(255, 255, 255, 255),
                    MSGBOX_FONT_NORMAL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CHarvestEventLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CHarvestEventLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CHarvestEventLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CHarvestEventLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CWhiteAngelEventLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddMsg(I18N::Game::WouldYouLikeToReceiveTheItem, RGBA(255, 255, 255, 255),
                    MSGBOX_FONT_NORMAL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CWhiteAngelEventLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CWhiteAngelEventLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CWhiteAngelEventLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CWhiteAngelEventLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CLuckyItemMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    // ??? ??
    int nTextIndex[10] = {
        0,
    };
    eLUCKYITEMTYPE eAct = g_pLuckyItemWnd->GetAct();

    switch (eAct)
    {
    case eLuckyItemType_Trade:
        nTextIndex[0] = 3288;
        nTextIndex[1] = 3297;
        nTextIndex[2] = 3298;
        nTextIndex[3] = 3299;
        break;
    case eLuckyItemType_Refinery:
        nTextIndex[0] = 3289;
        nTextIndex[1] = 539;
        break;
    default:
        return false;
        break;
    }

    pMsgBox->AddMsg(I18N::Game::Lookup(nTextIndex[0]), RGBA(255, 255, 0, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddMsg(L" ");
    for (int i = 1; i < 10; i++)
    {
        if (nTextIndex[i] <= 0)
            break;
        pMsgBox->AddMsg(I18N::Game::Lookup(nTextIndex[i]), RGBA(255, 255, 255, 255),
                        MSGBOX_FONT_NORMAL);
    }

    pMsgBox->AddCallbackFunc(BindCallback(this, &CLuckyItemMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CLuckyItemMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CLuckyItemMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CLuckyItemMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CMixCheckMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    wchar_t strText[256];
    if (g_MixRecipeMgr.GetCurRecipe()->m_iMixName[1] == 0)
    {
        mu_swprintf(strText, L"%ls",
                    I18N::Game::Lookup(g_MixRecipeMgr.GetCurRecipe()->m_iMixName[0]));
    }
    else if (g_MixRecipeMgr.GetCurRecipe()->m_iMixName[2] == 0)
    {
        mu_swprintf(strText, L"%ls %ls",
                    I18N::Game::Lookup(g_MixRecipeMgr.GetCurRecipe()->m_iMixName[0]),
                    I18N::Game::Lookup(g_MixRecipeMgr.GetCurRecipe()->m_iMixName[1]));
    }
    else
    {
        mu_swprintf(strText, L"%ls %ls %ls",
                    I18N::Game::Lookup(g_MixRecipeMgr.GetCurRecipe()->m_iMixName[0]),
                    I18N::Game::Lookup(g_MixRecipeMgr.GetCurRecipe()->m_iMixName[1]),
                    I18N::Game::Lookup(g_MixRecipeMgr.GetCurRecipe()->m_iMixName[2]));
    }

    pMsgBox->AddMsg(strText, RGBA(255, 255, 0, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddMsg(L" ");
    pMsgBox->AddMsg(I18N::Game::DoYouWantToCombineYourItems, RGBA(255, 255, 255, 255),
                    MSGBOX_FONT_NORMAL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CMixCheckMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CMixCheckMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CMixCheckMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CMixCheckMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CUseReviveCharmMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddMsg(I18N::Game::WouldYouLikeToSaveTheLocation);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUseReviveCharmMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUseReviveCharmMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUseReviveCharmMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUseReviveCharmMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CUsePortalCharmMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddMsg(I18N::Game::WouldYouLikeToSaveTheLocation);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUsePortalCharmMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUsePortalCharmMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUsePortalCharmMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUsePortalCharmMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CReturnPortalCharmMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddMsg(I18N::Game::WouldYouLikeToSaveTheLocation);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CReturnPortalCharmMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CReturnPortalCharmMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CReturnPortalCharmMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CReturnPortalCharmMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CDuelCreateErrorMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();

    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::ColosseumIsOccupied, RGBA(255, 255, 255, 255), MSGBOX_FONT_NORMAL);
    pMsgBox->AddMsg(I18N::Game::TryItAgainLater, RGBA(255, 255, 255, 255), MSGBOX_FONT_NORMAL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CDuelCreateErrorMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);

    return true;
}

bool SEASON3B::CDuelWatchErrorMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();

    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::NotAvailable, RGBA(255, 255, 128, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddMsg(L" ");
    pMsgBox->AddMsg(I18N::Game::TooManyPeopleInTheColossum, RGBA(255, 255, 255, 255),
                    MSGBOX_FONT_NORMAL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CDuelWatchErrorMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);

    return true;
}

bool SEASON3B::CDoppelGangerMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();

    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddCallbackFunc(BindCallback(this, &CDoppelGangerMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);

    return true;
}

bool SEASON3B::CGuildRelationShipMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();

    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildRelationShipMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildRelationShipMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildRelationShipMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildRelationShipMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    return true;
}

bool SEASON3B::CCastleMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();

    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddCallbackFunc(BindCallback(this, &CCastleMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CCastleMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CCastleMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CCastleMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CSiegeLevelMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();

    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::YouHaveNoAbility, RGBA(255, 255, 255, 255), MSGBOX_FONT_NORMAL);
    pMsgBox->AddMsg(I18N::Game::ToAttackTheCastle, RGBA(255, 255, 255, 255), MSGBOX_FONT_NORMAL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CSiegeLevelMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);

    return true;
}

bool SEASON3B::CSiegeGiveUpMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();

    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddMsg(I18N::Game::AreYouReallyWantToQuitTheSiegeWargare, RGBA(255, 255, 255, 255),
                    MSGBOX_FONT_NORMAL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CSiegeGiveUpMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CSiegeGiveUpMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CSiegeGiveUpMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CSiegeGiveUpMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CGatemanMoneyMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();

    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::EnteringIsNotAllowed, RGBA(255, 255, 255, 255), MSGBOX_FONT_NORMAL);
    pMsgBox->AddMsg(I18N::Game::InsufficientZenForEntering, RGBA(255, 255, 255, 255),
                    MSGBOX_FONT_NORMAL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CGatemanMoneyMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);

    return true;
}

bool SEASON3B::CGatemanFailMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();

    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::UnfortunatelyYouHaveFailed, RGBA(255, 255, 255, 255),
                    MSGBOX_FONT_NORMAL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CGatemanFailMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);

    return true;
}

bool SEASON3B::CQuestGiveUpMsgBoxLayout::SetLayout()
{
    selectedQuest_ = g_pMyQuestInfoWindow->GetSelQuestIndex();
    if (!selectedQuest_)
        return false;
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddMsg(I18N::Game::IfYouGiveUpYouWill);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CQuestGiveUpMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CQuestGiveUpMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CQuestGiveUpMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CQuestGiveUpMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

#ifdef ASG_ADD_TIME_LIMIT_QUEST
bool SEASON3B::CQuestCountLimitMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::YouCannotAcceptAnyMoreQuest);
    pMsgBox->AddMsg(I18N::Game::YouCanProceedMaximum10Quests);
    pMsgBox->AddMsg(I18N::Game::AtTheSameTime);
    pMsgBox->AddMsg(I18N::Game::YouNeedToClearAtLeast1QuestTo);
    pMsgBox->AddMsg(I18N::Game::AcceptThisOne);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CQuestCountLimitMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);

    return true;
}

#endif // ASG_ADD_TIME_LIMIT_QUEST

bool SEASON3B::CCanNotUseWordMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::RestrictedWordsAre, RGBA(255, 255, 255, 255), MSGBOX_FONT_NORMAL);
    pMsgBox->AddMsg(I18N::Game::Included, RGBA(255, 255, 255, 255), MSGBOX_FONT_NORMAL);

    return true;
}

bool SEASON3B::CHighValueItemCheckMsgBoxLayout::SetLayout()
{
    CNewUI3DItemCommonMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();
    ITEM *pItem = NULL;
    if (pPickedItem)
    {
        pItem = pPickedItem->GetItem();
    }
    if (pItem)
    {
        pMsgBox->Set3DItem(pItem);
    }

    pMsgBox->AddMsg(I18N::Game::AnExpensiveItem, RGBA(255, 0, 0, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddMsg(I18N::Game::CheckTheItemPlease, RGBA(255, 178, 0, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddMsg(I18N::Game::AreYouSureYouWantToSellIt, RGBA(255, 178, 0, 255),
                    MSGBOX_FONT_BOLD);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CHighValueItemCheckMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CHighValueItemCheckMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CHighValueItemCheckMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CHighValueItemCheckMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CUseFruitMsgBoxLayout::SetLayout()
{
    CNewUI3DItemCommonMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    ITEM *pItem = g_pMyInventory->GetStandbyItem();
    if (pItem == NULL)
    {
        return false;
    }

    pMsgBox->Set3DItem(pItem);

    wchar_t strName[50] = {
        0,
    };
    if (pItem->Type == ITEM_FRUITS)
    {
        switch (pItem->Level)
        {
        case 0:
            mu_swprintf(strName, L"%ls", I18N::Game::ENG);
            break;
        case 1:
            mu_swprintf(strName, L"%ls", I18N::Game::STA);
            break;
        case 2:
            mu_swprintf(strName, L"%ls", I18N::Game::AGI);
            break;
        case 3:
            mu_swprintf(strName, L"%ls", I18N::Game::STR);
            break;
        case 4:
            mu_swprintf(strName, L"%ls", I18N::Game::Command);
            break;
        }
    }

    pMsgBox->AddMsg(strName, RGBA(255, 255, 0, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddMsg(I18N::Game::DoYouWantToUseTheFruit, RGBA(255, 255, 0, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUseFruitMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUseFruitMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUseFruitMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUseFruitMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CUsePartChargeFruitMsgBoxLayout::SetLayout()
{
    CNewUI3DItemCommonMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    ITEM *pItem = g_pMyInventory->GetStandbyItem();
    if (pItem == NULL)
    {
        return false;
    }

    pMsgBox->Set3DItem(pItem);

    wchar_t strName[50] = {
        0,
    };

    if (pItem->Type == ITEM_HELPER + 54)
    {
        mu_swprintf(strName, L"%ls", I18N::Game::STR);
    }
    else if (pItem->Type == ITEM_HELPER + 55)
    {
        mu_swprintf(strName, L"%ls", I18N::Game::AGI);
    }
    else if (pItem->Type == ITEM_HELPER + 56)
    {
        mu_swprintf(strName, L"%ls", I18N::Game::STA);
    }
    else if (pItem->Type == ITEM_HELPER + 57)
    {
        mu_swprintf(strName, L"%ls", I18N::Game::ENG);
    }
    else if (pItem->Type == ITEM_HELPER + 58)
    {
        mu_swprintf(strName, L"%ls", I18N::Game::Command);
    }

    pMsgBox->AddMsg(strName, RGBA(255, 255, 0, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddMsg(I18N::Game::ThisIsMoreThanTheValueOfYourResettablePoints,
                    RGBA(255, 255, 0, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddMsg(I18N::Game::WouldYouLikeToReset, RGBA(255, 255, 0, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUsePartChargeFruitMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUsePartChargeFruitMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUsePartChargeFruitMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUsePartChargeFruitMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CPersonalShopItemValueCheckMsgBoxLayout::SetLayout()
{
    CNewUI3DItemCommonMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    CNewUIPickedItem *pPickedItem = g_pMyInventory->GetInventoryCtrl()->GetPickedItem();

    ITEM *pItemObj = NULL;
    if (g_pMyShopInventory->GetTargetIndex() == -1)
    {
        pItemObj = g_pMyShopInventory->FindItem(g_pMyShopInventory->GetSourceIndex());
    }
    else
    {
        if (pPickedItem)
        {
            pItemObj = pPickedItem->GetItem();
        }
    }

    if (pItemObj)
    {
        pMsgBox->Set3DItem(pItemObj);
    }

    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CPersonalShopItemValueCheckMsgBoxLayout::OkBtnDown),
        MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CPersonalShopItemValueCheckMsgBoxLayout::CancelBtnDown),
        MSGBOX_EVENT_USER_COMMON_CANCEL);

    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CPersonalShopItemValueCheckMsgBoxLayout::OkBtnDown),
        MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CPersonalShopItemValueCheckMsgBoxLayout::CancelBtnDown),
        MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CPersonalShopItemBuyMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddMsg(I18N::Game::DoYouWantToBuyAnItem);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPersonalShopItemBuyMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPersonalShopItemBuyMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPersonalShopItemBuyMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CPersonalShopItemBuyMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::COsbourneMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::Warning2223, RGBA(255, 0, 0, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddMsg(L" ");
    pMsgBox->AddMsg(I18N::Game::RefineryHasStartedRefineryIsA, RGBA(223, 191, 103, 255),
                    MSGBOX_FONT_BOLD);

    pMsgBox->AddCallbackFunc(BindCallback(this, &COsbourneMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &COsbourneMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);

    return true;
}

bool SEASON3B::CGuildOutPerson::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::AllianceMasterCanTDisbandTheGuild, RGBA(255, 255, 255, 255),
                    MSGBOX_FONT_BOLD);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildOutPerson::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildOutPerson::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);

    return true;
}

bool SEASON3B::CGuildBreakMsgBoxLayout::SetLayout()
{
    target_ = GuildList[DeleteIndex].Name;
    submitted_ = false;
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddMsg(I18N::Game::OnceYouDisbandTheGuild, RGBA(255, 255, 255, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddMsg(I18N::Game::AllTheItemsAndZenInTheGuildVaultWillDisappear,
                    RGBA(255, 255, 255, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddMsg(I18N::Game::AlsoTheGuildRankingInformationWillDisappear,
                    RGBA(255, 255, 255, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddMsg(I18N::Game::WouldYouLikeToDisbandTheGuild, RGBA(255, 255, 255, 255),
                    MSGBOX_FONT_BOLD);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildBreakMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildBreakMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildBreakMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildBreakMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    return true;
}

bool SEASON3B::CGuildPerson_Get_Out::SetLayout()
{
    target_ = GuildList[DeleteIndex].Name;
    submitted_ = false;
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    wchar_t Buff[300];
    mu_swprintf(Buff, I18N::Game::CharacterS, target_.c_str());
    pMsgBox->AddMsg(Buff, RGBA(255, 255, 255, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddMsg(I18N::Game::WouldYouLikeToRelease, RGBA(255, 255, 255, 255), MSGBOX_FONT_BOLD);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildPerson_Get_Out::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildPerson_Get_Out::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildPerson_Get_Out::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGuildPerson_Get_Out::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    return true;
}

bool SEASON3B::CGuildPerson_Cancel_Position_MsgBoxLayout::SetLayout()
{
    target_ = GuildList[DeleteIndex].Name;
    submitted_ = false;
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CGuildPerson_Cancel_Position_MsgBoxLayout::OkBtnDown),
        MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CGuildPerson_Cancel_Position_MsgBoxLayout::CancelBtnDown),
        MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CGuildPerson_Cancel_Position_MsgBoxLayout::CancelBtnDown),
        MSGBOX_EVENT_PRESSKEY_ESC);
    pMsgBox->AddCallbackFunc(
        BindCallback(this, &CGuildPerson_Cancel_Position_MsgBoxLayout::OkBtnDown),
        MSGBOX_EVENT_PRESSKEY_RETURN);
    return true;
}

bool SEASON3B::CCry_Wolf_Result_Set_Temple::SetLayout()
{
    auto &crywolf = TheMapProcess().Crywolf1st();
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    wchar_t Text[300];

    mu_swprintf(Text, L"%ls    %ls    %ls    %ls", I18N::Game::Rank, I18N::Game::Character,
                I18N::Game::Class, I18N::Game::Score);

    int TextColor = (255 << 24) + (21 << 16) + (148 << 8) + (255);
    pMsgBox->AddMsg(Text, TextColor);

    TextColor = (255 << 24) + (255 << 16) + (255 << 8) + (255);

    for (int i = 0; i < 5; i++)
    {
        if (crywolf.HeroScore[i] == -1)
            continue;

        mu_swprintf(Text, L"%d      %ls      %ls      %d", i + 1, crywolf.HeroName[i],
                    gCharacterManager.GetCharacterClassText(crywolf.HeroClass[i]),
                    crywolf.HeroScore[i]);

        pMsgBox->AddMsg(Text, TextColor);
    }

    TextColor = (255 << 24) + (255 << 16) + (0 << 8) + (255);
    pMsgBox->AddMsg(L"    ", TextColor);
    pMsgBox->AddMsg(L"    ", TextColor);
    pMsgBox->AddMsg(L"    ", TextColor);
    pMsgBox->AddMsg(L"    ", TextColor);

    if (crywolf.View_Suc_Or_Fail == 1)
    {
        mu_swprintf(Text, I18N::Game::MonsterStrengthDecreased10);
        pMsgBox->AddMsg(Text, TextColor);
        mu_swprintf(Text, I18N::Game::_5IncreaseInCastleAndArenaInvitationCombineRate);
        pMsgBox->AddMsg(Text, TextColor);
    }
    else
    {
        pMsgBox->AddMsg(I18N::Game::AllNPCsInCrywolfHaveBeenDeleted, TextColor);
    }

    pMsgBox->AddCallbackFunc(BindCallback(this, &CCry_Wolf_Result_Set_Temple::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    return true;
}

bool SEASON3B::CCry_Wolf_Ing_Set_Temple::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::ContractIsOngoingThereforeDualCompactIsNotPossible);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CCry_Wolf_Ing_Set_Temple::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    return true;
}

bool SEASON3B::CCry_Wolf_Destroy_Set_Temple::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::FurtherContractCanTBeDoneSinceTheAltarHasBeenDestroyed);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CCry_Wolf_Destroy_Set_Temple::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    return true;
}

bool SEASON3B::CCry_Wolf_Wat_Set_Temple1::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::PleaseTryAgainInAWhile);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CCry_Wolf_Wat_Set_Temple1::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    return true;
}

bool SEASON3B::CCry_Wolf_Dont_Set_Temple1::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::DisqualifiedForTheContractRequirement);
    pMsgBox->AddMsg(I18N::Game::OnlyLevelAbove350IsAllowedToMakeAContract);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CCry_Wolf_Dont_Set_Temple1::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    return true;
}

bool SEASON3B::CCry_Wolf_Dont_Set_Temple::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::ContractCanTBeMadeWhenYouAreOnAMount);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CCry_Wolf_Dont_Set_Temple::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    return true;
}

bool SEASON3B::CCry_Wolf_Set_Temple1::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::WeNeedAGuardianToProtectTheWolf);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CCry_Wolf_Set_Temple1::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    return true;
}

bool SEASON3B::CCry_Wolf_Set_Temple::SetLayout()
{
    auto &crywolf = TheMapProcess().Crywolf1st();
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::YouHaveBeenRegisteredToBeAGuardianToProtectTheWolf);
    pMsgBox->AddMsg(I18N::Game::YourRoleAsAGuardianWillBeCancelledWhenYouWarp);
    crywolf.BackUp_Key = CharactersClient[TargetNpc].Key;

    pMsgBox->AddCallbackFunc(BindCallback(this, &CCry_Wolf_Set_Temple::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    return true;
}

//CMaster_Level_Interface
bool SEASON3B::CMaster_Level_Interface::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    auto Need_Point = g_pMasterLevelInterface->GetConsumePoint();
    wchar_t szText[256];
    pMsgBox->AddMsg(I18N::Game::WouldYouLikeToStrengthenTheSkill);
    mu_swprintf(szText, I18N::Game::MasterLevelPointRequirementD, Need_Point);
    pMsgBox->AddMsg(szText);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CMaster_Level_Interface::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CMaster_Level_Interface::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CMaster_Level_Interface::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CMaster_Level_Interface::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CCry_Wolf_Get_Temple::SetLayout()
{
    auto &crywolf = TheMapProcess().Crywolf1st();
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    wchar_t szText[256];
    int Num = CharactersClient[TargetNpc].Object.Type - MODEL_CRYWOLF_ALTAR1;
    BYTE State = (crywolf.m_AltarState[Num] & 0x0f);
    mu_swprintf(szText, I18N::Game::ContractCanBeMadeForDTimes, State);
    pMsgBox->AddMsg(szText);
    pMsgBox->AddMsg(I18N::Game::WouldYouLikeToProceedWithTheContract);
    crywolf.BackUp_Key = CharactersClient[TargetNpc].Key;
    pMsgBox->AddCallbackFunc(BindCallback(this, &CCry_Wolf_Get_Temple::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CCry_Wolf_Get_Temple::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CCry_Wolf_Get_Temple::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CCry_Wolf_Get_Temple::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CUnionGuild_Break_MsgBoxLayout::SetLayout()
{
    target_ = DeleteID;
    submitted_ = false;
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;

    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    wchar_t szText[256];
    mu_swprintf(szText, I18N::Game::SGuildFromTheAlliance, target_.c_str());
    pMsgBox->AddMsg(szText);
    pMsgBox->AddMsg(I18N::Game::WouldYouLikeToRelease);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUnionGuild_Break_MsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUnionGuild_Break_MsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUnionGuild_Break_MsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUnionGuild_Break_MsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    return true;
}

bool SEASON3B::CUnionGuild_Out_MsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return false;

    pMsgBox->AddMsg(I18N::Game::AllianceMasterCanTWithdrawTheGuild, RGBA(255, 255, 255, 255),
                    MSGBOX_FONT_BOLD);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUnionGuild_Out_MsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUnionGuild_Out_MsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CUseSantaInvitationMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return FALSE;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return FALSE;

    pMsgBox->AddMsg(I18N::Game::WouldYouLikeToMoveToTheSantaSVillage);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUseSantaInvitationMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUseSantaInvitationMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUseSantaInvitationMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUseSantaInvitationMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return TRUE;
}

bool CSantaTownLeaveMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return FALSE;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return FALSE;

    pMsgBox->AddMsg(I18N::Game::WouldYouLikeToReturnToDevias);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CSantaTownLeaveMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CSantaTownLeaveMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CSantaTownLeaveMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CSantaTownLeaveMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return TRUE;
}

bool CSantaTownSantaMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return FALSE;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return FALSE;

    pMsgBox->AddCallbackFunc(BindCallback(this, &CSantaTownSantaMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CSantaTownSantaMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CSantaTownSantaMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CSantaTownSantaMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return TRUE;
}

bool SEASON3B::CUseRegistLuckyCoinMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return FALSE;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return FALSE;

    wchar_t szText[100] = {
        0,
    };
    mu_swprintf(szText, I18N::Game::YouAreLackOfSItems, I18N::Game::Register);
    pMsgBox->AddMsg(szText);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CUseRegistLuckyCoinMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    return TRUE;
}

bool SEASON3B::CRegistOverLuckyCoinMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return FALSE;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return FALSE;

    pMsgBox->AddMsg(I18N::Game::YouCanOnlyApplyOncePerYourAccount);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CRegistOverLuckyCoinMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);

    return TRUE;
}

bool SEASON3B::CExchangeLuckyCoinMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return FALSE;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return FALSE;

    wchar_t szText[100] = {
        0,
    };
    mu_swprintf(szText, I18N::Game::YouAreLackOfSItems, I18N::Game::Exchange1940);
    pMsgBox->AddMsg(szText);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CExchangeLuckyCoinMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);

    return TRUE;
}

bool SEASON3B::CExchangeLuckyCoinInvenErrMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return FALSE;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return FALSE;

    pMsgBox->AddMsg(I18N::Game::MoreThan2X4SpaceInInventoryIsNeeded);

    pMsgBox->AddCallbackFunc(BindCallback(this, &CExchangeLuckyCoinInvenErrMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);

    return TRUE;
}

bool SEASON3B::CGambleBuyMsgBoxLayout::SetLayout()
{
    CNewUI3DItemCommonMsgBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return false;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OKCANCEL))
        return false;

    ITEM *pItem = g_pNPCShop->GetStandbyItem();
    if (pItem == NULL)
    {
        return false;
    }
    pMsgBox->Set3DItem(pItem);
    pMsgBox->AddMsg(I18N::Game::WouldYouLikeToPurchase);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGambleBuyMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGambleBuyMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_USER_COMMON_CANCEL);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGambleBuyMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_PRESSKEY_RETURN);
    pMsgBox->AddCallbackFunc(BindCallback(this, &CGambleBuyMsgBoxLayout::CancelBtnDown),
                             MSGBOX_EVENT_PRESSKEY_ESC);
    return true;
}

bool SEASON3B::CEmpireGuardianMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return FALSE;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return FALSE;

    pMsgBox->AddCallbackFunc(BindCallback(this, &CEmpireGuardianMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);

    return TRUE;
}

bool SEASON3B::CUnitedMarketPlaceMsgBoxLayout::SetLayout()
{
    CNewUICommonMessageBox *pMsgBox = GetMsgBox();
    if (0 == pMsgBox)
        return FALSE;
    if (false == pMsgBox->Create(MSGBOX_COMMON_TYPE_OK))
        return FALSE;

    pMsgBox->AddCallbackFunc(BindCallback(this, &CUnitedMarketPlaceMsgBoxLayout::OkBtnDown),
                             MSGBOX_EVENT_USER_COMMON_OK);

    return TRUE;
}

using namespace SEASON3B;
void CNewUIHelpWindow::SetPos(int, int)
{
}
void CNewUIHelpWindow::StageContent()
{
    const char *locale = I18N::GetCurrentLocale();
    if (locale_ == locale)
        return;
    locale_ = locale;
    constexpr std::array labels{3415, 3416, 3417, 3416, 3418};
    for (std::size_t i = 0; i < labels.size(); ++i)
        content_.labels[i] = I18N::Game::Lookup(labels[i]);
    for (auto &rows : content_.rows)
        rows.clear();
    constexpr int FirstKey = 121, FunctionKeyCount = 4, KeyCount = 19, RetiredPartyKey = 128;
    const auto add = [&](int id) {
        content_.rows[0].push_back(HelpPanelDetail::HelpColumns(I18N::Game::Lookup(id)));
    };
    for (int i = 0; i < FunctionKeyCount; ++i)
        add(FirstKey + i);
    const wchar_t *extras[]{I18N::Game::F8ToggleMonsterHPBar, I18N::Game::F9Toggle3DCamera,
                            I18N::Game::F10LockUnlockCameraZoom, I18N::Game::F11ResetCameraView,
                            I18N::Game::HomeToggleMUHelper};
    for (const auto *line : extras)
        content_.rows[0].push_back(HelpPanelDetail::HelpColumns(line));
    for (int i = FunctionKeyCount; i < KeyCount; ++i)
        if (FirstKey + i != RetiredPartyKey)
            add(FirstKey + i);
    constexpr int FirstChat = 141, ChatCount = 16;
    for (int i = 0; i < ChatCount; ++i)
        content_.rows[1].push_back(HelpPanelDetail::HelpColumns(I18N::Game::Lookup(FirstChat + i)));
    ++content_.revision;
}

bool CNewUIHelpWindow::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}
bool CNewUIHelpWindow::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_, page_, content_);
}

using namespace SEASON3B;

void CNewUIWindowMenu::SetPos(int, int)
{
}

void CNewUIWindowMenu::StageContent()
{
    content_.visible = IsVisible();
    if (!content_.visible)
        return;
    for (std::size_t i = 0; i < WindowMenuDetail::WindowMenuTextIds.size(); ++i)
        content_.labels[i] = I18N::Game::Lookup(WindowMenuDetail::WindowMenuTextIds[i]);
}

bool CNewUIWindowMenu::Render()
{
    return panel_.Record(renderer_.LegacyRender());
}
bool CNewUIWindowMenu::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, content_);
}

void SEASON3B::CNewUIOptionWindow::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;
    m_modernPanel.SetPosition(x, y);
}

bool SEASON3B::CNewUIOptionWindow::Render()
{
    return m_modernPanel.Record(m_renderer.LegacyRender());
}

bool SEASON3B::CNewUIOptionWindow::PrepareModernUiOnWorker(int viewportWidth, int viewportHeight)
{
    return m_modernPanel.PrepareOnWorker(viewportWidth, viewportHeight, PanelContent(),
                                         PanelValues());
}

void UI::ReconnectDialog::DrawCenteredText(float x, float y, float width, LegacyFontRole role,
                                           const wchar_t *text)
{
    g_RenderText.SetFont(role);
    g_RenderText.SetBgColor(0, 0, 0, 0);
    g_RenderText.SetTextColor(ReconnectDetail::TEXT_R, ReconnectDetail::TEXT_G,
                              ReconnectDetail::TEXT_B, ReconnectDetail::TEXT_A);
    g_RenderText.RenderText(static_cast<int>(x), static_cast<int>(y), text, static_cast<int>(width),
                            0, RT3_SORT_CENTER);
}

void UI::ReconnectDialog::DrawBackdrop(ReconnectManager &reconnect)
{
    // While probing we're still in the main scene: dim the live game lightly
    // and let it show through.
    if (reconnect.GetPhase() == ReconnectManager::Phase::Probing)
    {
        FillBlack(0.0f, 0.0f, REFERENCE_WIDTH, REFERENCE_HEIGHT, ReconnectDetail::DIM_ALPHA);
        return;
    }

    FillBlack(0.0f, 0.0f, REFERENCE_WIDTH, REFERENCE_HEIGHT, ReconnectDetail::OPAQUE_ALPHA);
}

void UI::ReconnectDialog::DrawStatusTexts(ReconnectManager &mgr)
{
    DrawCenteredText(ReconnectDetail::PANEL_X, ReconnectDetail::TITLE_Y, ReconnectDetail::PANEL_W,
                     LegacyFontRole::Bold, I18N::Game::ConnectionLost);
    DrawCenteredText(ReconnectDetail::PANEL_X, ReconnectDetail::STEP_Y, ReconnectDetail::PANEL_W,
                     LegacyFontRole::Normal, ReconnectDetail::StepLabel(mgr.GetPhase()));

    const int seconds = mgr.GetCountdownSeconds();
    if (seconds > 0)
    {
        wchar_t countdown[64];
        mu_swprintf(countdown, I18N::Game::RetryingInSeconds, seconds);
        DrawCenteredText(ReconnectDetail::PANEL_X, ReconnectDetail::COUNTDOWN_Y,
                         ReconnectDetail::PANEL_W, LegacyFontRole::Normal, countdown);
    }
}

// ---- Native (message-box textured) rendering ----------------------------

void UI::ReconnectDialog::DrawNative(ReconnectManager &reconnect, bool cancelHovered)
{
    glEnable(GL_TEXTURE_2D);

    // Inner background, then the top/middle*n/bottom border frame on top.
    RenderImage(ReconnectDetail::MsgBox::IMAGE_MSGBOX_BACK, ReconnectDetail::PANEL_X,
                ReconnectDetail::PANEL_Y + 2.0f,
                ReconnectDetail::MSGBOX_WIDTH - ReconnectDetail::BACK_BLANK_W,
                ReconnectDetail::PANEL_H - ReconnectDetail::BACK_BLANK_H);

    RenderImage(ReconnectDetail::MsgBox::IMAGE_MSGBOX_TOP, ReconnectDetail::PANEL_X,
                ReconnectDetail::PANEL_Y, ReconnectDetail::MSGBOX_WIDTH, ReconnectDetail::TOP_H);
    RenderImage(ReconnectDetail::MsgBox::IMAGE_MSGBOX_MIDDLE, ReconnectDetail::PANEL_X,
                ReconnectDetail::PANEL_Y + ReconnectDetail::TOP_H, ReconnectDetail::MSGBOX_WIDTH,
                ReconnectDetail::MIDDLE_FILL_H, 0.0f, 0.0f, 1.0f, 1.0f); // stretched to fill
    RenderImage(ReconnectDetail::MsgBox::IMAGE_MSGBOX_BOTTOM, ReconnectDetail::PANEL_X,
                ReconnectDetail::PANEL_Y + ReconnectDetail::TOP_H + ReconnectDetail::MIDDLE_FILL_H,
                ReconnectDetail::MSGBOX_WIDTH, ReconnectDetail::BOTTOM_H);

    // Progress bar (native two-texture gauge), fill inset into the trough.
    RenderImage(ReconnectDetail::MsgBox::IMAGE_MSGBOX_PROGRESS_BG, ReconnectDetail::PROG_X,
                ReconnectDetail::PROG_Y, ReconnectDetail::PROG_W, ReconnectDetail::PROG_H);
    const float fraction = Progress(reconnect);
    if (fraction > 0.0f)
    {
        RenderImage(ReconnectDetail::MsgBox::IMAGE_MSGBOX_PROGRESS_BAR, ReconnectDetail::PROG_BAR_X,
                    ReconnectDetail::PROG_BAR_Y, ReconnectDetail::PROG_BAR_MAX_W * fraction,
                    ReconnectDetail::PROG_BAR_H);
    }

    // Cancel button: pick the normal / hover row from the button sheet.
    const float sv =
        cancelHovered ? (ReconnectDetail::CANCEL_H / ReconnectDetail::BTN_SHEET_H) : 0.0f;
    RenderImage(ReconnectDetail::MsgBox::IMAGE_MSGBOX_BTN_CANCEL, ReconnectDetail::CANCEL_X,
                ReconnectDetail::CANCEL_Y, ReconnectDetail::CANCEL_W, ReconnectDetail::CANCEL_H,
                0.0f, sv, ReconnectDetail::CANCEL_W / ReconnectDetail::BTN_SHEET_W,
                ReconnectDetail::CANCEL_H / ReconnectDetail::BTN_SHEET_H);

    DrawStatusTexts(reconnect);
}

// ---- Fallback (flat-colour) rendering, used when the skin is unloaded ----

void UI::ReconnectDialog::DrawFallback(ReconnectManager &reconnect, bool cancelHovered)
{
    FillWhite(ReconnectDetail::PANEL_X - ReconnectDetail::FB_BORDER,
              ReconnectDetail::PANEL_Y - ReconnectDetail::FB_BORDER,
              ReconnectDetail::PANEL_W + 2 * ReconnectDetail::FB_BORDER,
              ReconnectDetail::PANEL_H + 2 * ReconnectDetail::FB_BORDER,
              ReconnectDetail::FB_BORDER_ALPHA);
    FillBlack(ReconnectDetail::PANEL_X, ReconnectDetail::PANEL_Y, ReconnectDetail::PANEL_W,
              ReconnectDetail::PANEL_H, ReconnectDetail::FB_PANEL_ALPHA);

    FillBlack(ReconnectDetail::PROG_X, ReconnectDetail::PROG_Y, ReconnectDetail::PROG_W,
              ReconnectDetail::PROG_H, ReconnectDetail::FB_BAR_BG_ALPHA);
    const float fraction = Progress(reconnect);
    if (fraction > 0.0f)
    {
        FillWhite(ReconnectDetail::PROG_BAR_X, ReconnectDetail::PROG_BAR_Y,
                  ReconnectDetail::PROG_BAR_MAX_W * fraction, ReconnectDetail::PROG_BAR_H,
                  ReconnectDetail::FB_BAR_FILL_ALPHA);
    }

    FillWhite(ReconnectDetail::CANCEL_X, ReconnectDetail::CANCEL_Y, ReconnectDetail::CANCEL_W,
              ReconnectDetail::CANCEL_H,
              cancelHovered ? ReconnectDetail::FB_CANCEL_HOVER_ALPHA
                            : ReconnectDetail::FB_CANCEL_ALPHA);

    EndRenderColor(); // restore texturing for the glyphs

    DrawStatusTexts(reconnect);
    DrawCenteredText(ReconnectDetail::CANCEL_X, ReconnectDetail::CANCEL_Y + 8.0f,
                     ReconnectDetail::CANCEL_W, LegacyFontRole::Normal, I18N::Game::Cancel);
}

void UI::ReconnectDialog::Render(ReconnectManager &mgr)
{
    if (!mgr.IsActive())
    {
        return;
    }

    const bool cancelHovered = CancelHovered();

    BeginBitmap();

    DrawBackdrop(mgr);

    if (NativeSkinAvailable())
    {
        DrawNative(mgr, cancelHovered);
    }
    else
    {
        DrawFallback(mgr, cancelHovered);
    }

    EndBitmap();
}

// OMF-01909
// OMF-01910
void UI::ReconnectDialogLegacyCalls::DrawCenteredText(float x, float y, float width,
                                                      LegacyFontRole role, const wchar_t *text)
{
    return owner_.DrawCenteredText(x, y, width, role, text);
} // OMF-01911

// Feature controller methods consolidated from shared UI buckets.
#pragma pack(push)
#pragma pack()
void CSlideHelpMgr::Render()
{
    panel_.Record(renderer_.LegacyRender());
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CSlideHelpMgr::PrepareModernUiOnWorker(int width, int height)
{
    return panel_.PrepareOnWorker(width, height, visible_);
}
#pragma pack(pop)

void UI::ReconnectDialog::FillWhite(float x, float y, float w, float h, float alpha)
{
    RenderColor(x, y, w, h, alpha, 0);
}

void UI::ReconnectDialog::FillBlack(float x, float y, float w, float h, float alpha)
{
    RenderColor(x, y, w, h, alpha, 1);
}
