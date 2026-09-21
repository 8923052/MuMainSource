#pragma once

#include "render/Textures.h"
#include "session/SessionRuntime.h"
#include <RmlUi/Core/Decorator.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace UI::Modern::PC::Common
{
inline constexpr int TextureIndex = BITMAP_INTERFACE_MACROUI_END;
inline constexpr int S16PopupTextureIndex = TextureIndex + 9;
} // namespace UI::Modern::PC::Common

namespace UI::Modern
{
// Applies the authored GFx RGB scale/offset to one unchanged atlas sprite.
class RmlGfxTintDecoratorInstancer final : public Rml::DecoratorInstancer
{
  public:
    RmlGfxTintDecoratorInstancer();
    Rml::SharedPtr<Rml::Decorator> InstanceDecorator(
        const Rml::String &name, const Rml::PropertyDictionary &properties,
        const Rml::DecoratorInstancerInterface &interface) override;

  private:
    class SpriteDecorator;
    Rml::PropertyId spriteId_;
    std::array<Rml::PropertyId, 4> tintIds_;
};
} // namespace UI::Modern

namespace UI::Modern
{
struct RmlUiScaledViewport final
{
    int width = 0;
    int height = 0;
    float scale = 0.0F;
};

RmlUiScaledViewport CalculateRmlUiScaledViewport(int viewportWidth, int viewportHeight,
                                                 float maximumScale, float contentWidth,
                                                 float contentHeight) noexcept;
} // namespace UI::Modern

namespace UI::Modern::PC::Common
{
inline constexpr int TooltipTextureIndex = TextureIndex + 2;
}

namespace UI::Modern::PC::Textures
{
inline constexpr int FrameTextureIndex = Common::TextureIndex + 15;
inline constexpr int ButtonTextureIndex = Common::TextureIndex + 16;
inline constexpr int ScrollTextureIndex = Common::TextureIndex + 17;
inline constexpr int CheckboxTextureIndex = Common::TextureIndex + 18;
inline constexpr int IconTextureIndex = Common::TextureIndex + 19;
} // namespace UI::Modern::PC::Textures

struct SessionInputEvent;

namespace Rml
{
class Context;
}

namespace UI::Modern
{
struct RmlUiPresentation;

struct RmlTextInputArea final
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

bool DispatchRmlUiInput(Rml::Context &context, const SessionInputEvent &event);
bool DispatchRmlUiInput(Rml::Context &context, const SessionInputEvent &physicalEvent,
                        const RmlUiPresentation &presentation);
} // namespace UI::Modern

class LegacyRenderFacade;
class SessionKeeper;
struct SessionInputEvent;

namespace Rml
{
class Element;
class ElementDocument;
} // namespace Rml

namespace UI::Modern
{
class RmlDocumentHost final
{
  public:
    RmlDocumentHost(SessionKeeper &keeper, std::string contextName,
                    std::filesystem::path documentPath);
    ~RmlDocumentHost();

    RmlDocumentHost(const RmlDocumentHost &) = delete;
    RmlDocumentHost &operator=(const RmlDocumentHost &) = delete;

    void SetContextName(std::string contextName);
    bool Ensure(int physicalWidth, int physicalHeight, float contentWidth = 1.0F,
                float contentHeight = 1.0F, float scaleMultiplier = 1.0F);
    bool EnsureAtLogicalSize(int logicalWidth, int logicalHeight);
    RmlUiScaledViewport Viewport() const noexcept;
    float ConfiguredScale() const noexcept;
    bool SetVisible(bool visible);
    Rml::ElementDocument *Document() noexcept;
    Rml::Element *HoverElement() noexcept;
    void ResetInteraction() noexcept;
    bool ProcessInput(const SessionInputEvent &event);
    std::optional<RmlTextInputArea> FocusedTextInputArea() const;
    bool CaptureIfDirty(bool dirty);
    bool Record(LegacyRenderFacade &facade) const;
    void Release();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern

namespace UI::Modern
{
// Immutable RML inputs for calculations needed before a document is opened.
// Construct once; rendering and input only read the decoded values.
class RmlUiDesign final
{
  public:
    RmlUiDesign(const std::filesystem::path &document, std::initializer_list<const char *> names);

    template <typename T = float, typename Key> T Number(Key key) const noexcept
    {
        return static_cast<T>(values_[static_cast<std::size_t>(key)].front());
    }

    template <typename Key> std::span<const float> Values(Key key) const noexcept
    {
        return values_[static_cast<std::size_t>(key)];
    }

  private:
    class Parser;
    std::vector<std::vector<float>> values_;
};
} // namespace UI::Modern

class ApplicationKeeper;
class CGlobalBitmap;
class LegacyRenderFacade;
class SessionRenderUnit;
class SessionTextureNamespace;

namespace Rml
{
class Context;
}

namespace UI::Modern
{
struct RmlUiRenderSnapshot;

struct RmlUiPresentation final
{
    std::uint32_t physicalWidth = 0;
    std::uint32_t physicalHeight = 0;
    std::uint32_t logicalWidth = 0;
    std::uint32_t logicalHeight = 0;
    float scaleX = 1.0F;
    float scaleY = 1.0F;

    int ToLogicalX(int value) const noexcept;
    int ToLogicalY(int value) const noexcept;
    bool MatchesPhysicalViewport(std::uint32_t width, std::uint32_t height) const noexcept;

    bool operator==(const RmlUiPresentation &) const = default;
};

RmlUiPresentation CalculateRmlUiPresentation(std::uint32_t physicalWidth,
                                             std::uint32_t physicalHeight,
                                             std::uint32_t logicalWidth,
                                             std::uint32_t logicalHeight) noexcept;

inline std::array<float, 16> CalculateRmlUiProjection(std::uint32_t physicalWidth,
                                                      std::uint32_t physicalHeight) noexcept
{
    const float width = static_cast<float>(physicalWidth);
    const float height = static_cast<float>(physicalHeight);
    return {
        2.0F / width, 0.0F, 0.0F,  0.0F, 0.0F,  2.0F / height, 0.0F, 0.0F,
        0.0F,         0.0F, -1.0F, 0.0F, -1.0F, -1.0F,         0.0F, 1.0F,
    };
}

class RmlUiRuntime final
{
  public:
    explicit RmlUiRuntime(ApplicationKeeper &keeper) noexcept;
    ~RmlUiRuntime();

    RmlUiRuntime(const RmlUiRuntime &) = delete;
    RmlUiRuntime &operator=(const RmlUiRuntime &) = delete;

    bool IsReady() const noexcept;
    bool IsWorkerThread() const noexcept;
    bool Submit(std::function<void()> work) noexcept;
    bool Execute(std::function<void()> work) noexcept;
    bool WaitUntilIdle() noexcept;
    Rml::Context *CreateContext(const char *name, std::int32_t width, std::int32_t height) noexcept;
    bool RemoveContext(const char *name) noexcept;
    std::shared_ptr<const RmlUiRenderSnapshot> Capture(
        Rml::Context &context, SessionRenderUnit &renderer, CGlobalBitmap &bitmaps,
        SessionTextureNamespace &textures, const RmlUiPresentation &presentation) noexcept;
    bool Record(const RmlUiRenderSnapshot &snapshot, LegacyRenderFacade &facade,
                SessionId sessionId) const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace UI::Modern

namespace UI::Modern
{
enum class UiPlatform
{
    Pc,
    Mobile,
};

std::filesystem::path ResolveUiDocument(const std::filesystem::path &uiRoot, UiPlatform platform,
                                        std::string_view component, std::string_view fileName);
} // namespace UI::Modern
