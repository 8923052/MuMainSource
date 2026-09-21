

#include "ui/runtime/UiRuntime.h"
#include "app/AppWindow.h"
#include "app/ApplicationKeeper.h"
#include "render/Assets.h"
#include "render/FrameTape.h"
#include "render/Sprites.h"
#include "render/Textures.h"
#include "render/UiAdapter.h"
#include "session/SessionKeeper.h"
#include "session/SessionRender.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "ui/session/UiSessionLogic.h"

namespace UI::Modern
{
class RmlGfxTintDecoratorInstancer::SpriteDecorator final : public Rml::Decorator
{
  public:
    SpriteDecorator(const Rml::Sprite &sprite, Rml::Texture texture, std::array<float, 4> tint)
        : rectangle_(sprite.rectangle), tint_(tint)
    {
        AddTexture(texture);
    }
    Rml::DecoratorDataHandle GenerateElementData(Rml::Element *element,
                                                 Rml::BoxArea area) const override
    {
        auto *manager = element->GetRenderManager();
        const auto dimensions = GetTexture().GetDimensions();
        if (!manager || dimensions.x <= 0 || dimensions.y <= 0)
            return INVALID_DECORATORDATAHANDLE;
        auto shader = manager->CompileShader(
            "gfx-tint",
            {{"tint", Rml::Variant(Rml::Vector4f(tint_[0], tint_[1], tint_[2], tint_[3]))}});
        if (!shader)
            return INVALID_DECORATORDATAHANDLE;
        const auto box = element->GetRenderBox(area);
        const Rml::Vector2f textureSize(dimensions);
        const auto alpha = static_cast<Rml::byte>(element->GetComputedValues().opacity() * 255);
        Rml::Mesh mesh;
        Rml::MeshUtilities::GenerateQuad(
            mesh, box.GetFillOffset(), box.GetFillSize(), Rml::ColourbPremultiplied(alpha, alpha),
            rectangle_.TopLeft() / textureSize, rectangle_.BottomRight() / textureSize);
        return reinterpret_cast<Rml::DecoratorDataHandle>(
            new Data{manager->MakeGeometry(std::move(mesh)), std::move(shader)});
    }
    void ReleaseElementData(Rml::DecoratorDataHandle handle) const override
    {
        delete reinterpret_cast<Data *>(handle);
    }
    void RenderElement(Rml::Element *element, Rml::DecoratorDataHandle handle) const override
    {
        const auto *data = reinterpret_cast<Data *>(handle);
        data->geometry.Render(element->GetAbsoluteOffset(Rml::BoxArea::Border), GetTexture(),
                              data->shader);
    }

  private:
    struct Data
    {
        Rml::Geometry geometry;
        Rml::CompiledShader shader;
    };
    Rml::Rectanglef rectangle_;
    std::array<float, 4> tint_;
};
RmlGfxTintDecoratorInstancer::RmlGfxTintDecoratorInstancer()
{
    spriteId_ = RegisterProperty("sprite", "").AddParser("string").GetId();
    constexpr std::array names{"scale", "red", "green", "blue"};
    for (std::size_t i = 0; i < names.size(); ++i)
        tintIds_[i] = RegisterProperty(names[i], i == 0 ? "1" : "0").AddParser("number").GetId();
    RegisterShorthand("decorator", "sprite, scale, red, green, blue",
                      Rml::ShorthandType::FallThrough);
}
Rml::SharedPtr<Rml::Decorator> RmlGfxTintDecoratorInstancer::InstanceDecorator(
    const Rml::String &, const Rml::PropertyDictionary &properties,
    const Rml::DecoratorInstancerInterface &interface)
{
    const auto *sprite = interface.GetSprite(properties.GetProperty(spriteId_)->Get<Rml::String>());
    if (!sprite)
        return nullptr;
    std::array<float, 4> tint;
    for (std::size_t i = 0; i < tint.size(); ++i)
        tint[i] = properties.GetProperty(tintIds_[i])->Get<float>();
    constexpr float ColorChannelMaximum = 255;
    for (std::size_t i = 1; i < tint.size(); ++i)
        tint[i] /= ColorChannelMaximum;
    const auto texture =
        sprite->sprite_sheet->texture_source.GetTexture(interface.GetRenderManager());
    return Rml::MakeShared<SpriteDecorator>(*sprite, texture, tint);
}
} // namespace UI::Modern

namespace UI::Modern
{
RmlUiScaledViewport CalculateRmlUiScaledViewport(int viewportWidth, int viewportHeight,
                                                 float maximumScale, float contentWidth,
                                                 float contentHeight) noexcept
{
    if (viewportWidth <= 0 || viewportHeight <= 0 || contentWidth <= 0.0F || contentHeight <= 0.0F)
    {
        return {};
    }
    const float scale =
        std::min({maximumScale, viewportWidth / contentWidth, viewportHeight / contentHeight});
    return {static_cast<int>(std::ceil(viewportWidth / scale)),
            static_cast<int>(std::ceil(viewportHeight / scale)), scale};
}
} // namespace UI::Modern

namespace UI::Modern
{
class RmlDocumentHost::Impl final
{
  public:
    Impl(SessionKeeper &keeper, std::string contextName, std::filesystem::path documentPath)
        : keeper_(keeper), runtime_(keeper.ApplicationKeeperRef().ModernUiRuntime()),
          contextName_(std::move(contextName)), documentPath_(std::move(documentPath))
    {
    }

    ~Impl()
    {
        Release();
    }

    void SetContextName(std::string contextName)
    {
        if (context_ == nullptr)
        {
            contextName_ = std::move(contextName);
        }
    }

    bool Ensure(int physicalWidth, int physicalHeight, float contentWidth, float contentHeight,
                float scaleMultiplier)
    {
        viewport_ = CalculateRmlUiScaledViewport(physicalWidth, physicalHeight,
                                                 ConfiguredScale() * scaleMultiplier, contentWidth,
                                                 contentHeight);
        return EnsureAtLogicalSize(viewport_.width, viewport_.height);
    }

    bool EnsureAtLogicalSize(int logicalWidth, int logicalHeight)
    {
        SessionRenderUnit *const renderer = keeper_.Renderer();
        if (runtime_ == nullptr || !runtime_->IsWorkerThread() || !runtime_->IsReady() ||
            renderer == nullptr || logicalWidth <= 0 || logicalHeight <= 0)
        {
            return false;
        }
        const RmlUiPresentation nextPresentation = CalculateRmlUiPresentation(
            renderer->ModernUiPhysicalViewportWidth(), renderer->ModernUiPhysicalViewportHeight(),
            static_cast<std::uint32_t>(logicalWidth), static_cast<std::uint32_t>(logicalHeight));
        if (presentation_ != nextPresentation)
        {
            presentation_ = nextPresentation;
            ResetInteraction();
            dirty_ = true;
        }
        if (context_ == nullptr && !Load(logicalWidth, logicalHeight))
        {
            return false;
        }
        if (viewportWidth_ != logicalWidth || viewportHeight_ != logicalHeight)
        {
            viewportWidth_ = logicalWidth;
            viewportHeight_ = logicalHeight;
            context_->SetDimensions({logicalWidth, logicalHeight});
            dirty_ = true;
        }
        ApplyPendingInteractionReset();
        return document_ != nullptr;
    }

    RmlUiScaledViewport Viewport() const noexcept
    {
        return viewport_;
    }

    float ConfiguredScale() const noexcept
    {
        return keeper_.ApplicationConfig().rmlUiScale;
    }

    Rml::ElementDocument *Document() noexcept
    {
        return runtime_ != nullptr && runtime_->IsWorkerThread() ? document_ : nullptr;
    }

    Rml::Element *HoverElement() noexcept
    {
        return runtime_ != nullptr && runtime_->IsWorkerThread() && context_ != nullptr
                   ? context_->GetHoverElement()
                   : nullptr;
    }

    void ResetInteraction() noexcept
    {
        interactionResetRequested_.store(true, std::memory_order_release);
        if (runtime_ != nullptr && runtime_->IsWorkerThread())
        {
            ApplyPendingInteractionReset();
        }
    }

    bool SetVisible(bool visible)
    {
        if (runtime_ == nullptr || !runtime_->IsWorkerThread() || document_ == nullptr)
        {
            return false;
        }
        if (visible != documentVisible_)
        {
            if (!visible)
            {
                ResetInteraction();
            }
            visible ? document_->Show() : document_->Hide();
            documentVisible_ = visible;
            dirty_ = true;
            if (!visible)
            {
                pointerButtonPressed_ = false;
            }
        }
        return true;
    }

    bool ProcessInput(const SessionInputEvent &event)
    {
        if (runtime_ == nullptr || !runtime_->IsWorkerThread() || !documentVisible_ ||
            context_ == nullptr)
        {
            return false;
        }
        ApplyPendingInteractionReset();
        Rml::Element *const previousHover =
            event.action == SessionInputAction::PointerMove ? context_->GetHoverElement() : nullptr;
        const bool processed = DispatchRmlUiInput(*context_, event, presentation_);
        bool visualChanged = processed;
        if (event.action == SessionInputAction::PointerMove && !pointerButtonPressed_)
        {
            visualChanged = processed && previousHover != context_->GetHoverElement();
        }
        else if (event.action == SessionInputAction::PointerButton)
        {
            pointerButtonPressed_ = event.pressed;
        }
        else if (event.action == SessionInputAction::WindowFocusLost)
        {
            pointerButtonPressed_ = false;
        }
        dirty_ = dirty_ || visualChanged;
        return processed;
    }

    std::optional<RmlTextInputArea> FocusedTextInputArea() const
    {
        if (runtime_ == nullptr || !runtime_->IsWorkerThread() || !documentVisible_ ||
            context_ == nullptr)
        {
            return std::nullopt;
        }
        Rml::Element *const focus = context_->GetFocusElement();
        if (rmlui_dynamic_cast<Rml::ElementFormControl *>(focus) == nullptr)
        {
            return std::nullopt;
        }
        const Rml::Vector2f position = focus->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f size = focus->GetBox().GetSize(Rml::BoxArea::Border);
        return RmlTextInputArea{static_cast<int>(std::lround(position.x * presentation_.scaleX)),
                                static_cast<int>(std::lround(position.y * presentation_.scaleY)),
                                static_cast<int>(std::lround(size.x * presentation_.scaleX)),
                                static_cast<int>(std::lround(size.y * presentation_.scaleY))};
    }

    bool CaptureIfDirty(bool dirty)
    {
        if (runtime_ == nullptr || !runtime_->IsWorkerThread() || context_ == nullptr ||
            document_ == nullptr)
        {
            return false;
        }
        dirty_ = ApplyFontFamily() || dirty_ || dirty || context_->GetNextUpdateDelay() <= 0.0;
        if (!documentVisible_)
        {
            return true;
        }
        bool overflowLabelChanged = false;
        if (!dirty_ && snapshot_ != nullptr)
        {
            overflowLabelChanged = UpdateOverflowLabel();
            if (!overflowLabelChanged)
            {
                return true;
            }
            dirty_ = true;
        }

        context_->Update();
        if (!overflowLabelChanged)
        {
            dirty_ = UpdateOverflowLabel() || dirty_;
        }
        auto next = runtime_->Capture(*context_, *keeper_.Renderer(), keeper_.BitmapRegistry(),
                                      keeper_.TextureNamespace(), presentation_);
        if (next == nullptr)
        {
            return false;
        }
        snapshot_ = std::move(next);
        dirty_ = false;
        return true;
    }

    bool Record(LegacyRenderFacade &facade) const
    {
        const SessionDisplayView *const display = keeper_.Display();
        return display == nullptr || !display->IsVisible() || !documentVisible_ ||
               (snapshot_ != nullptr && runtime_->Record(*snapshot_, facade, keeper_.Id()));
    }

    void Release()
    {
        if (runtime_ != nullptr && runtime_->IsWorkerThread())
        {
            ReleaseOnWorker();
            return;
        }
        if (runtime_ != nullptr && runtime_->Execute([this]() noexcept { ReleaseOnWorker(); }))
        {
            return;
        }
        ReleaseState();
    }

  private:
    void ReleaseOnWorker()
    {
        ResetInteraction();
        if (context_ != nullptr && runtime_ != nullptr)
        {
            (void)runtime_->RemoveContext(contextName_.c_str());
        }
        ReleaseState();
    }

    void ReleaseState() noexcept
    {
        snapshot_.reset();
        document_ = nullptr;
        context_ = nullptr;
        viewportWidth_ = 0;
        viewportHeight_ = 0;
        viewport_ = {};
        presentation_ = {};
        documentVisible_ = false;
        dirty_ = true;
        pointerButtonPressed_ = false;
        appliedFont_.clear();
        overflowLabel_ = nullptr;
        interactionResetRequested_.store(false, std::memory_order_release);
    }

    bool UpdateOverflowLabel()
    {
        Rml::Element *next = context_->GetHoverElement();
        while (next != nullptr && next != document_ && !next->IsClassSet("mu-overflow-label"))
        {
            next = next->GetParentNode();
        }
        if (next == document_)
        {
            next = nullptr;
        }

        const auto now = std::chrono::steady_clock::now();
        bool changed = false;
        if (next != overflowLabel_)
        {
            if (overflowLabel_ != nullptr && overflowLabel_->GetScrollLeft() != 0.0F)
            {
                overflowLabel_->SetScrollLeft(0.0F);
                changed = true;
            }
            overflowLabel_ = next;
            overflowHoverStarted_ = now;
        }
        if (overflowLabel_ == nullptr)
        {
            return changed;
        }

        const float overflow = overflowLabel_->GetScrollWidth() - overflowLabel_->GetClientWidth();
        if (overflow <= 0.0F)
        {
            return changed;
        }

        static const RmlUiDesign animation("Data/UI/PC/Common/common.rml",
                                           {"OverflowPauseSeconds", "OverflowPixelsPerSecond"});
        const float PauseSeconds = animation.Number(0);
        const float PixelsPerSecond = animation.Number(1);
        const float travelSeconds = overflow / PixelsPerSecond;
        const float cycleSeconds = 2.0F * (PauseSeconds + travelSeconds);
        const float elapsedSeconds =
            std::chrono::duration<float>(now - overflowHoverStarted_).count();
        const float phase = std::fmod(elapsedSeconds, cycleSeconds);

        float scrollLeft = 0.0F;
        if (phase > PauseSeconds && phase <= PauseSeconds + travelSeconds)
        {
            scrollLeft = (phase - PauseSeconds) * PixelsPerSecond;
        }
        else if (phase <= 2.0F * PauseSeconds + travelSeconds &&
                 phase > PauseSeconds + travelSeconds)
        {
            scrollLeft = overflow;
        }
        else if (phase > 2.0F * PauseSeconds + travelSeconds)
        {
            scrollLeft = overflow - (phase - 2.0F * PauseSeconds - travelSeconds) * PixelsPerSecond;
        }

        const float previous = overflowLabel_->GetScrollLeft();
        overflowLabel_->SetScrollLeft(scrollLeft);
        return changed || overflowLabel_->GetScrollLeft() != previous;
    }

    bool ApplyFontFamily()
    {
        const std::wstring &configured = keeper_.ApplicationKeeperRef().ApplicationConfig().font;
        const std::string family =
            configured.empty() ? "Arial" : StringUtils::WideToNarrow(configured.c_str());
        if (family == appliedFont_)
            return false;
        document_->SetProperty("font-family", family);
        appliedFont_ = family;
        return true;
    }

    void ApplyPendingInteractionReset() noexcept
    {
        if (context_ == nullptr ||
            !interactionResetRequested_.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }
        if (Rml::Element *const focus = context_->GetFocusElement())
        {
            focus->Blur();
        }
        (void)context_->ProcessMouseLeave();
        pointerButtonPressed_ = false;
        dirty_ = true;
    }

    bool Load(int viewportWidth, int viewportHeight)
    {
        context_ = runtime_->CreateContext(contextName_.c_str(), viewportWidth, viewportHeight);
        if (context_ == nullptr)
        {
            return false;
        }
        document_ = context_->LoadDocument(documentPath_.string());
        if (document_ == nullptr)
        {
            Release();
            return false;
        }
        viewportWidth_ = viewportWidth;
        viewportHeight_ = viewportHeight;
        dirty_ = true;
        return true;
    }

    SessionKeeper &keeper_;
    RmlUiRuntime *runtime_ = nullptr;
    std::string contextName_;
    std::filesystem::path documentPath_;
    Rml::Context *context_ = nullptr;
    Rml::ElementDocument *document_ = nullptr;
    std::shared_ptr<const RmlUiRenderSnapshot> snapshot_;
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
    RmlUiScaledViewport viewport_{};
    RmlUiPresentation presentation_{};
    bool documentVisible_ = false;
    bool dirty_ = true;
    bool pointerButtonPressed_ = false;
    std::string appliedFont_;
    Rml::Element *overflowLabel_ = nullptr;
    std::chrono::steady_clock::time_point overflowHoverStarted_{};
    std::atomic<bool> interactionResetRequested_ = false;
};

RmlDocumentHost::RmlDocumentHost(SessionKeeper &keeper, std::string contextName,
                                 std::filesystem::path documentPath)
    : impl_(std::make_unique<Impl>(keeper, std::move(contextName), std::move(documentPath)))
{
}

RmlDocumentHost::~RmlDocumentHost() = default;

void RmlDocumentHost::SetContextName(std::string contextName)
{
    impl_->SetContextName(std::move(contextName));
}

bool RmlDocumentHost::Ensure(int physicalWidth, int physicalHeight, float contentWidth,
                             float contentHeight, float scaleMultiplier)
{
    return impl_->Ensure(physicalWidth, physicalHeight, contentWidth, contentHeight,
                         scaleMultiplier);
}

bool RmlDocumentHost::EnsureAtLogicalSize(int logicalWidth, int logicalHeight)
{
    return impl_->EnsureAtLogicalSize(logicalWidth, logicalHeight);
}

RmlUiScaledViewport RmlDocumentHost::Viewport() const noexcept
{
    return impl_->Viewport();
}

float RmlDocumentHost::ConfiguredScale() const noexcept
{
    return impl_->ConfiguredScale();
}

bool RmlDocumentHost::SetVisible(bool visible)
{
    return impl_->SetVisible(visible);
}

Rml::ElementDocument *RmlDocumentHost::Document() noexcept
{
    return impl_->Document();
}

Rml::Element *RmlDocumentHost::HoverElement() noexcept
{
    return impl_->HoverElement();
}

void RmlDocumentHost::ResetInteraction() noexcept
{
    impl_->ResetInteraction();
}

bool RmlDocumentHost::ProcessInput(const SessionInputEvent &event)
{
    return impl_->ProcessInput(event);
}

std::optional<RmlTextInputArea> RmlDocumentHost::FocusedTextInputArea() const
{
    return impl_->FocusedTextInputArea();
}

bool RmlDocumentHost::CaptureIfDirty(bool dirty)
{
    return impl_->CaptureIfDirty(dirty);
}

bool RmlDocumentHost::Record(LegacyRenderFacade &facade) const
{
    return impl_->Record(facade);
}

void RmlDocumentHost::Release()
{
    impl_->Release();
}
} // namespace UI::Modern

namespace UI::Modern
{
class RmlUiDesign::Parser final : public Rml::BaseXMLParser
{
  public:
    void HandleElementStart(const Rml::String &tag, const Rml::XMLAttributes &attributes) override
    {
        if (tag != "meta")
            return;
        const auto name = Rml::Get<Rml::String>(attributes, "name", "");
        if (!name.starts_with("mu-design-"))
            return;
        entries.emplace(name.substr(std::string_view("mu-design-").size()),
                        Rml::Get<Rml::String>(attributes, "content", ""));
    }

    std::unordered_map<std::string, std::string> entries;
};

RmlUiDesign::RmlUiDesign(const std::filesystem::path &document,
                         std::initializer_list<const char *> names)
{
    std::ifstream file(document, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot read UI design: " + document.string());
    const std::string source{std::istreambuf_iterator<char>(file), {}};
    Rml::StreamMemory stream(reinterpret_cast<const Rml::byte *>(source.data()), source.size());
    Parser parser;
    parser.RegisterCDATATag("style");
    parser.RegisterCDATATag("script");
    parser.Parse(&stream);
    values_.reserve(names.size());
    for (const char *name : names)
    {
        const auto entry = parser.entries.find(name);
        if (entry == parser.entries.end())
            throw std::runtime_error("Missing UI design " + std::string(name) + " in " +
                                     document.string());
        std::istringstream input(entry->second);
        input.imbue(std::locale::classic());
        values_.emplace_back(std::istream_iterator<float>(input), std::istream_iterator<float>());
        if (values_.back().empty() || !input.eof())
            throw std::runtime_error("Invalid UI design " + std::string(name) + " in " +
                                     document.string());
    }
}
} // namespace UI::Modern

namespace UI::Modern
{
namespace
{
Rml::Input::KeyIdentifier ConvertKey(std::int32_t scancode)
{
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
    {
        return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_A + scancode - SDL_SCANCODE_A);
    }
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
    {
        return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_1 + scancode - SDL_SCANCODE_1);
    }
    switch (scancode)
    {
    case SDL_SCANCODE_0:
        return Rml::Input::KI_0;
    case SDL_SCANCODE_SPACE:
        return Rml::Input::KI_SPACE;
    case SDL_SCANCODE_BACKSPACE:
        return Rml::Input::KI_BACK;
    case SDL_SCANCODE_TAB:
        return Rml::Input::KI_TAB;
    case SDL_SCANCODE_RETURN:
        return Rml::Input::KI_RETURN;
    case SDL_SCANCODE_KP_ENTER:
        return Rml::Input::KI_NUMPADENTER;
    case SDL_SCANCODE_ESCAPE:
        return Rml::Input::KI_ESCAPE;
    case SDL_SCANCODE_PAGEUP:
        return Rml::Input::KI_PRIOR;
    case SDL_SCANCODE_PAGEDOWN:
        return Rml::Input::KI_NEXT;
    case SDL_SCANCODE_END:
        return Rml::Input::KI_END;
    case SDL_SCANCODE_HOME:
        return Rml::Input::KI_HOME;
    case SDL_SCANCODE_LEFT:
        return Rml::Input::KI_LEFT;
    case SDL_SCANCODE_UP:
        return Rml::Input::KI_UP;
    case SDL_SCANCODE_RIGHT:
        return Rml::Input::KI_RIGHT;
    case SDL_SCANCODE_DOWN:
        return Rml::Input::KI_DOWN;
    case SDL_SCANCODE_INSERT:
        return Rml::Input::KI_INSERT;
    case SDL_SCANCODE_DELETE:
        return Rml::Input::KI_DELETE;
    case SDL_SCANCODE_LSHIFT:
        return Rml::Input::KI_LSHIFT;
    case SDL_SCANCODE_RSHIFT:
        return Rml::Input::KI_RSHIFT;
    case SDL_SCANCODE_LCTRL:
        return Rml::Input::KI_LCONTROL;
    case SDL_SCANCODE_RCTRL:
        return Rml::Input::KI_RCONTROL;
    case SDL_SCANCODE_LALT:
        return Rml::Input::KI_LMENU;
    case SDL_SCANCODE_RALT:
        return Rml::Input::KI_RMENU;
    default:
        return Rml::Input::KI_UNKNOWN;
    }
}

int ConvertModifiers(std::uint32_t modifiers)
{
    int result = 0;
    if ((modifiers & SDL_KMOD_CTRL) != 0)
        result |= Rml::Input::KM_CTRL;
    if ((modifiers & SDL_KMOD_SHIFT) != 0)
        result |= Rml::Input::KM_SHIFT;
    if ((modifiers & SDL_KMOD_ALT) != 0)
        result |= Rml::Input::KM_ALT;
    if ((modifiers & SDL_KMOD_GUI) != 0)
        result |= Rml::Input::KM_META;
    if ((modifiers & SDL_KMOD_CAPS) != 0)
        result |= Rml::Input::KM_CAPSLOCK;
    if ((modifiers & SDL_KMOD_NUM) != 0)
        result |= Rml::Input::KM_NUMLOCK;
    return result;
}

int ConvertMouseButton(std::int32_t button)
{
    switch (button)
    {
    case SDL_BUTTON_LEFT:
        return 0;
    case SDL_BUTTON_RIGHT:
        return 1;
    case SDL_BUTTON_MIDDLE:
        return 2;
    default:
        return 3;
    }
}
} // namespace

bool DispatchRmlUiInput(Rml::Context &context, const SessionInputEvent &event)
{
    const int modifiers = ConvertModifiers(event.modifiers);
    switch (event.action)
    {
    case SessionInputAction::PointerMove:
        (void)context.ProcessMouseMove(event.x, event.y, modifiers);
        return true;
    case SessionInputAction::PointerButton:
        if (event.pressed)
        {
            (void)context.ProcessMouseButtonDown(ConvertMouseButton(event.code), modifiers);
        }
        else
        {
            (void)context.ProcessMouseButtonUp(ConvertMouseButton(event.code), modifiers);
        }
        return true;
    case SessionInputAction::PointerWheel:
        (void)context.ProcessMouseWheel(Rml::Vector2f{0.0F, -event.wheel}, modifiers);
        return true;
    case SessionInputAction::KeyDown:
    case SessionInputAction::KeyUp: {
        const Rml::Input::KeyIdentifier key = ConvertKey(event.code);
        if (key == Rml::Input::KI_UNKNOWN)
        {
            return false;
        }
        if (event.action == SessionInputAction::KeyDown)
        {
            (void)context.ProcessKeyDown(key, modifiers);
        }
        else
        {
            (void)context.ProcessKeyUp(key, modifiers);
        }
        return true;
    }
    case SessionInputAction::TextInput:
        (void)context.ProcessTextInput(Rml::String(event.text.data()));
        return true;
    case SessionInputAction::WindowFocusLost:
        if (Rml::Element *const focus = context.GetFocusElement())
        {
            focus->Blur();
        }
        (void)context.ProcessMouseLeave();
        return true;
    default:
        return false;
    }
}

bool DispatchRmlUiInput(Rml::Context &context, const SessionInputEvent &physicalEvent,
                        const RmlUiPresentation &presentation)
{
    SessionInputEvent logicalEvent = physicalEvent;
    if (logicalEvent.kind == SessionInputEventKind::Pointer)
    {
        logicalEvent.x = presentation.ToLogicalX(logicalEvent.x);
        logicalEvent.y = presentation.ToLogicalY(logicalEvent.y);
    }
    return DispatchRmlUiInput(context, logicalEvent);
}
} // namespace UI::Modern

namespace UI::Modern
{
std::filesystem::path ResolveUiDocument(const std::filesystem::path &uiRoot, UiPlatform platform,
                                        std::string_view component, std::string_view fileName)
{
    const std::filesystem::path pc = uiRoot / "PC" / component / fileName;
    if (platform == UiPlatform::Mobile)
    {
        const std::filesystem::path mobile = uiRoot / "Mobile" / component / fileName;
        if (std::filesystem::exists(mobile))
        {
            return mobile;
        }
    }
    return pc;
}
} // namespace UI::Modern

using namespace SEASON3B;

// Construction/Destruction

SEASON3B::CNewUI3DCamera::CNewUI3DCamera(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), cameraProjection_(keeper.CameraProjectionObject())
{
}

SEASON3B::CNewUI3DCamera::~CNewUI3DCamera()
{
    Release();
}

bool SEASON3B::CNewUI3DCamera::Create(int iCameraIndex, UINT uiWidth, UINT uiHeight, float fZOrder)
{
    Release();

    m_iCameraIndex = iCameraIndex;
    m_uiWidth = uiWidth;
    m_uiHeight = uiHeight;
    m_fZOrder = fZOrder;

    return true;
}

void SEASON3B::CNewUI3DCamera::Release()
{
    RemoveAll3DRenderObjs();
    m_deque2DEffects.clear();
}

void SEASON3B::CNewUI3DCamera::UpdateDimensions(UINT uiWidth, UINT uiHeight)
{
    m_uiWidth = uiWidth;
    m_uiHeight = uiHeight;
}

bool SEASON3B::CNewUI3DCamera::IsEmpty()
{
    return m_list3DObjs.empty();
}

void SEASON3B::CNewUI3DCamera::Add3DRenderObj(INewUI3DRenderObj *pObj)
{
    if (std::find(m_list3DObjs.begin(), m_list3DObjs.end(), pObj) == m_list3DObjs.end())
    {
        m_list3DObjs.push_back(pObj);
    }
}

void SEASON3B::CNewUI3DCamera::Remove3DRenderObj(INewUI3DRenderObj *pObj)
{
    auto vi = std::find(m_list3DObjs.begin(), m_list3DObjs.end(), pObj);
    if (vi != m_list3DObjs.end())
    {
        m_list3DObjs.erase(vi);
    }
}

void SEASON3B::CNewUI3DCamera::RemoveAll3DRenderObjs()
{
    m_list3DObjs.clear();
}

void SEASON3B::CNewUI3DCamera::RenderUI2DEffect(UI_2DEFFECT_CALLBACK pCallbackFunc, LPVOID pClass,
                                                DWORD dwParamA, DWORD dwParamB)
{
    UI_2DEFFECT_INFO UI2DEffectInfo;
    UI2DEffectInfo.pCallbackFunc = pCallbackFunc;
    UI2DEffectInfo.pClass = pClass;
    UI2DEffectInfo.dwParamA = dwParamA;
    UI2DEffectInfo.dwParamB = dwParamB;

    m_deque2DEffects.push_back(UI2DEffectInfo);
}

void SEASON3B::CNewUI3DCamera::DeleteUI2DEffectObject(UI_2DEFFECT_CALLBACK pCallbackFunc)
{
    auto di = m_deque2DEffects.begin();
    for (; di != m_deque2DEffects.end(); di++)
    {
        if ((*di).pCallbackFunc == pCallbackFunc)
        {
            m_deque2DEffects.erase(di);
            break;
        }
    }
}

int SEASON3B::CNewUI3DCamera::GetCameraIndex() const
{
    return m_iCameraIndex;
}

float SEASON3B::CNewUI3DCamera::GetLayerDepth()
{
    //. fZOrder == fLayerDepth
    return m_fZOrder;
}

void SEASON3B::CNewUI3DCamera::RenderUI2DEffects()
{
    while (!m_deque2DEffects.empty())
    {
        UI_2DEFFECT_INFO &UI2DEffectInfo = m_deque2DEffects.front();
        if (UI2DEffectInfo.pCallbackFunc)
        {
            (*UI2DEffectInfo.pCallbackFunc)(UI2DEffectInfo.pClass, UI2DEffectInfo.dwParamA,
                                            UI2DEffectInfo.dwParamB);
        }
        m_deque2DEffects.pop_front();
    }
}

bool SEASON3B::CNewUI3DCamera::Update()
{
    //. DOING NOTHING
    return true;
}

bool SEASON3B::CNewUI3DCamera::UpdateMouseEvent()
{
    //. DOING NOTHING
    return true;
}

bool SEASON3B::CNewUI3DCamera::UpdateKeyEvent()
{
    //. DOING NOTHING
    return true;
}

SEASON3B::CNewUI3DRenderMng::CNewUI3DRenderMng(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper)
{
}

SEASON3B::CNewUI3DRenderMng::~CNewUI3DRenderMng()
{
    Release();
}

bool SEASON3B::CNewUI3DRenderMng::Create(CNewUIManager *pNewUIMng)
{
    m_pNewUIMng = pNewUIMng;
    return true;
}

void SEASON3B::CNewUI3DRenderMng::Release()
{
    RemoveAll3DRenderObjs();
}

void SEASON3B::CNewUI3DRenderMng::UpdateAllCameraDimensions(UINT uiWidth, UINT uiHeight)
{
    for (auto it = m_listCamera.begin(); it != m_listCamera.end(); ++it)
    {
        if (*it)
        {
            (*it)->UpdateDimensions(uiWidth, uiHeight);
        }
    }
}

void SEASON3B::CNewUI3DRenderMng::Add3DRenderObj(INewUI3DRenderObj *pObj,
                                                 float fZOrder /* = INFORMATION_CAMERA_Z_ORDER*/)
{
    CNewUI3DCamera *pCamera = FindCamera(fZOrder);
    if (NULL == pCamera)
    {
        int iAvailableCameraIndex = FindAvailableCameraIndex();
        if (-1 != iAvailableCameraIndex)
        {
            pCamera = new CNewUI3DCamera(SessionOrigin());
            pCamera->Create(iAvailableCameraIndex, WindowWidth, WindowHeight, fZOrder);
            pCamera->Add3DRenderObj(pObj);
            m_pNewUIMng->AddUIObj(iAvailableCameraIndex, pCamera);
            m_listCamera.push_back(pCamera);
        }
        else
        {
#ifdef _DEBUG
            MU_DEBUG_BREAK();
#endif // _DEBUG
        }
    }
    else
    {
        pCamera->Add3DRenderObj(pObj);
    }
}
void SEASON3B::CNewUI3DRenderMng::Remove3DRenderObj(INewUI3DRenderObj *pObj)
{
    auto li = m_listCamera.begin();
    for (; li != m_listCamera.end(); li++)
    {
        (*li)->Remove3DRenderObj(pObj);
        if ((*li)->IsEmpty())
        {
            m_pNewUIMng->RemoveUIObj(*li);
            delete (*li);
            m_listCamera.erase(li);
            break;
        }
    }
}

void SEASON3B::CNewUI3DRenderMng::RemoveAll3DRenderObjs()
{
    auto li = m_listCamera.begin();
    for (; li != m_listCamera.end(); li++)
    {
        delete (*li);
        m_pNewUIMng->RemoveUIObj(*li);
    }
    m_listCamera.clear();
}

void SEASON3B::CNewUI3DRenderMng::RenderUI2DEffect(float fZOrder,
                                                   UI_2DEFFECT_CALLBACK pCallbackFunc,
                                                   LPVOID pClass, DWORD dwParamA, DWORD dwParamB)
{
    CNewUI3DCamera *pCamera = FindCamera(fZOrder);
    if (pCamera)
        pCamera->RenderUI2DEffect(pCallbackFunc, pClass, dwParamA, dwParamB);
}

void SEASON3B::CNewUI3DRenderMng::DeleteUI2DEffectObject(UI_2DEFFECT_CALLBACK pCallbackFunc)
{
    auto li = m_listCamera.begin();
    for (; li != m_listCamera.end(); li++)
        (*li)->DeleteUI2DEffectObject(pCallbackFunc);
}

void SEASON3B::CNewUI3DRenderMng::RenderUI2DEffects()
{
    for (auto *camera : m_listCamera)
        camera->RenderUI2DEffects();
}

CNewUI3DCamera *SEASON3B::CNewUI3DRenderMng::FindCamera(float fZOrder)
{
    auto li = m_listCamera.begin();
    for (; li != m_listCamera.end(); li++)
        if ((*li)->GetLayerDepth() == fZOrder)
            return (*li);
    return NULL;
}

int SEASON3B::CNewUI3DRenderMng::FindAvailableCameraIndex()
{
    for (int iIndex = INTERFACE_3DRENDERING_CAMERA_BEGIN; iIndex < INTERFACE_3DRENDERING_CAMERA_END;
         iIndex++)
    {
        auto li = m_listCamera.begin();
        for (; li != m_listCamera.end(); li++)
        {
            if ((*li)->GetCameraIndex() == iIndex)
                break;
        }
        if (li == m_listCamera.end())
            return iIndex;
    }
    return -1;
}

namespace UI::Modern
{
namespace
{

class MuSystemInterface final : public Rml::SystemInterface
{
  public:
    void SetClipboardText(const Rml::String &text) override
    {
        (void)SDL_SetClipboardText(text.c_str());
    }

    void GetClipboardText(Rml::String &text) override
    {
        char *const value = SDL_GetClipboardText();
        text = value != nullptr ? value : "";
        SDL_free(value);
    }
};
} // namespace

class RmlUiRuntime::Impl final
{
  public:
    explicit Impl(ApplicationKeeper &keeper) noexcept
        : keeper_(keeper), textures_(keeper.BitmapRegistry()), renderer_(keeper.BitmapRegistry())
    {
        try
        {
            if (!textures_.Load("Data/UI/PC/ui_assets.rcss", keeper.ErrorReport()))
                return;
            worker_ = std::thread([this]() noexcept { Run(); });
        }
        catch (...)
        {
            return;
        }

        std::unique_lock lock(mutex_);
        idle_.wait(lock, [this]() noexcept { return started_; });
        if (ready_)
        {
            ready_ = keeper_.BitmapRegistry().SetTrustedAssetProducerThread(workerThread_);
        }
    }

    ~Impl()
    {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        available_.notify_one();
        if (worker_.joinable())
        {
            worker_.join();
        }
        (void)keeper_.BitmapRegistry().SetTrustedAssetProducerThread({});
    }

    bool Submit(std::function<void()> work) noexcept
    {
        if (!work || !ready_.load(std::memory_order_acquire))
        {
            return false;
        }
        try
        {
            std::lock_guard lock(mutex_);
            if (stopping_)
            {
                return false;
            }
            work_.push_back(std::move(work));
        }
        catch (...)
        {
            return false;
        }
        available_.notify_one();
        return true;
    }

    bool Execute(std::function<void()> work) noexcept
    {
        if (!work)
        {
            return false;
        }
        if (IsWorkerThread())
        {
            try
            {
                work();
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        std::mutex completionMutex;
        std::condition_variable completionReady;
        bool completed = false;
        bool succeeded = false;
        if (!Submit([&]() noexcept {
                try
                {
                    work();
                    succeeded = true;
                }
                catch (...)
                {
                    succeeded = false;
                }
                {
                    std::lock_guard lock(completionMutex);
                    completed = true;
                    // Execute owns this condition variable on its stack.
                    completionReady.notify_one();
                }
            }))
        {
            return false;
        }
        std::unique_lock lock(completionMutex);
        completionReady.wait(lock, [&]() noexcept { return completed; });
        return succeeded;
    }

    bool WaitUntilIdle() noexcept
    {
        if (IsWorkerThread())
        {
            return false;
        }
        std::unique_lock lock(mutex_);
        idle_.wait(lock, [this]() noexcept { return (work_.empty() && !working_) || stopping_; });
        return ready_.load(std::memory_order_acquire) && !stopping_;
    }

    bool IsWorkerThread() const noexcept
    {
        return std::this_thread::get_id() == workerThread_;
    }

  private:
    bool LoadFonts()
    {
        bool loaded =
            Rml::LoadFontFace("C:/Windows/Fonts/arial.ttf") &&
            Rml::LoadFontFace("C:/Windows/Fonts/arialbd.ttf", false, Rml::Style::FontWeight::Bold);
        for (const BundledFont &font : GetBundledFonts())
        {
            loaded = Rml::LoadFontFace(font.regular, font.family, Rml::Style::FontStyle::Normal,
                                       Rml::Style::FontWeight::Normal, true) &&
                     Rml::LoadFontFace(font.bold, font.family, Rml::Style::FontStyle::Normal,
                                       Rml::Style::FontWeight::Bold, true) &&
                     loaded;
        }
        return loaded;
    }

    void Run() noexcept
    {
        workerThread_ = std::this_thread::get_id();
        Rml::SetSystemInterface(&system_);
        Rml::SetRenderInterface(&renderer_);
        initialized_ = Rml::Initialise() && LoadFonts();
        if (initialized_)
        {
            tintDecorator_ = std::make_unique<RmlGfxTintDecoratorInstancer>();
            Rml::Factory::RegisterDecoratorInstancer("gfx-tint", tintDecorator_.get());
        }
        ready_.store(initialized_, std::memory_order_release);
        {
            std::lock_guard lock(mutex_);
            started_ = true;
        }
        idle_.notify_all();

        for (;;)
        {
            std::function<void()> work;
            {
                std::unique_lock lock(mutex_);
                available_.wait(lock, [this]() noexcept { return stopping_ || !work_.empty(); });
                if (stopping_ && work_.empty())
                {
                    break;
                }
                work = std::move(work_.front());
                work_.pop_front();
                working_ = true;
            }
            try
            {
                work();
            }
            catch (...)
            {
            }
            {
                std::lock_guard lock(mutex_);
                working_ = false;
            }
            idle_.notify_all();
        }

        ready_.store(false, std::memory_order_release);
        if (initialized_)
        {
            Rml::Shutdown();
        }
        idle_.notify_all();
    }

  public:
    ApplicationKeeper &keeper_;
    MuSystemInterface system_;
    UiTextureLibrary textures_;
    TapeRenderInterface renderer_;
    std::unique_ptr<RmlGfxTintDecoratorInstancer> tintDecorator_;
    std::thread worker_;
    std::thread::id workerThread_;
    std::mutex mutex_;
    std::condition_variable available_;
    std::condition_variable idle_;
    std::deque<std::function<void()>> work_;
    std::atomic<bool> ready_ = false;
    bool initialized_ = false;
    bool started_ = false;
    bool stopping_ = false;
    bool working_ = false;
};

int RmlUiPresentation::ToLogicalX(int value) const noexcept
{
    return static_cast<int>(std::lround(value / scaleX));
}

int RmlUiPresentation::ToLogicalY(int value) const noexcept
{
    return static_cast<int>(std::lround(value / scaleY));
}

bool RmlUiPresentation::MatchesPhysicalViewport(std::uint32_t width,
                                                std::uint32_t height) const noexcept
{
    return physicalWidth == width && physicalHeight == height;
}

RmlUiPresentation CalculateRmlUiPresentation(std::uint32_t physicalWidth,
                                             std::uint32_t physicalHeight,
                                             std::uint32_t logicalWidth,
                                             std::uint32_t logicalHeight) noexcept
{
    return {
        physicalWidth,
        physicalHeight,
        logicalWidth,
        logicalHeight,
        static_cast<float>(physicalWidth) / logicalWidth,
        static_cast<float>(physicalHeight) / logicalHeight,
    };
}

RmlUiRuntime::RmlUiRuntime(ApplicationKeeper &keeper) noexcept
    : impl_(std::make_unique<Impl>(keeper))
{
    (void)keeper.RegisterModernUiRuntime(*this);
}

RmlUiRuntime::~RmlUiRuntime() = default;

bool RmlUiRuntime::IsReady() const noexcept
{
    return impl_ != nullptr && impl_->ready_.load(std::memory_order_acquire);
}

bool RmlUiRuntime::IsWorkerThread() const noexcept
{
    return impl_ != nullptr && impl_->IsWorkerThread();
}

bool RmlUiRuntime::Submit(std::function<void()> work) noexcept
{
    return impl_ != nullptr && impl_->Submit(std::move(work));
}

bool RmlUiRuntime::Execute(std::function<void()> work) noexcept
{
    return impl_ != nullptr && impl_->Execute(std::move(work));
}

bool RmlUiRuntime::WaitUntilIdle() noexcept
{
    return impl_ != nullptr && impl_->WaitUntilIdle();
}

Rml::Context *RmlUiRuntime::CreateContext(const char *name, std::int32_t width,
                                          std::int32_t height) noexcept
{
    if (!IsReady() || !IsWorkerThread() || name == nullptr || width <= 0 || height <= 0)
    {
        return nullptr;
    }
    return Rml::CreateContext(name, {width, height});
}

bool RmlUiRuntime::RemoveContext(const char *name) noexcept
{
    return IsReady() && IsWorkerThread() && name != nullptr && Rml::RemoveContext(name);
}

std::shared_ptr<const RmlUiRenderSnapshot> RmlUiRuntime::Capture(
    Rml::Context &context, SessionRenderUnit &renderer, CGlobalBitmap &bitmaps,
    SessionTextureNamespace &textures, const RmlUiPresentation &presentation) noexcept
{
    if (!IsReady() || !IsWorkerThread())
    {
        return nullptr;
    }
    try
    {
        auto snapshot = std::make_shared<RmlUiRenderSnapshot>();
        snapshot->presentation = presentation;
        impl_->renderer_.BeginCapture(*snapshot, renderer, bitmaps, textures);
        context.Render();
        impl_->renderer_.EndCapture();
        return snapshot;
    }
    catch (...)
    {
        impl_->renderer_.EndCapture();
        return nullptr;
    }
}

} // namespace UI::Modern
