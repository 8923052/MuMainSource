#include "session/SessionRender.h"
#include "app/Application.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "app/AppWindow.h"
#include "data/CharacterData.h"
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
#include "domain/Guild.h"
#include "domain/ItemsSkills.h"
#include "domain/MapPresentation.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Quests.h"
#include "domain/Shop.h"
#include "domain/WorldPhysics.h"
#include "domain/WorldSimulation.h"
#include "I18N/All.h"
#include "render/FrameTape.h"
#include "render/ModelGeometry.h"
#include "render/ModelResources.h"
#include "render/Sprites.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionAudio.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionPresentation.h"
#include "session/SessionRuntime.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "turbojpeg.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Dialogs/DialogsRender.h"
#include "ui/features/Hud/HudLogic.h"
#include "ui/features/Hud/HudRender.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/Shell/ShellLogic.h"
#include "ui/features/Social/SocialRender.h"
#include "ui/features/World/WorldLogic.h"
#include "ui/features/World/WorldRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"

#ifdef _WIN32
#include <eh.h>
#endif

SessionFrameView::SessionFrameView(RenderFunction render) noexcept : render_(render)
{
}

bool SessionFrameView::HasRenderFunction() const noexcept
{
    return render_ != nullptr;
}

bool SessionFrameView::Render() const noexcept
{
    if (render_ == nullptr)
    {
        return false;
    }

    return render_();
}

namespace
{
constexpr float ReferenceWidth = 640.0F;
constexpr float ReferenceHeight = 480.0F;
constexpr std::uint32_t StartupLoadStepCount = 11;
constexpr std::uint32_t BasicDataLoadStepCount = StartupLoadStepCount - 1;

LogicalRenderAssetRef ResolveAsset(SessionKeeper &keeper, int texture) noexcept
{
    if (texture < 0)
    {
        return {};
    }
    const auto asset = keeper.TextureNamespace().Resolve(static_cast<std::uint32_t>(texture));
    return asset.value_or(LogicalRenderAssetRef{});
}

std::array<float, 4> DecodeColor(unsigned int color) noexcept
{
    return {
        static_cast<float>(color & 0xffU) / 255.0F,
        static_cast<float>((color >> 8) & 0xffU) / 255.0F,
        static_cast<float>((color >> 16) & 0xffU) / 255.0F,
        static_cast<float>((color >> 24) & 0xffU) / 255.0F,
    };
}

bool EmitQuad(LegacyRenderFacade &facade, float x, float y, float width, float height, float u,
              float v, float uWidth, float vHeight, const std::array<float, 4> &color,
              float rotation = 0.0F) noexcept
{
    const RenderTapeRect viewport = facade.Viewport();
    const float centerX = x + width * 0.5F;
    const float centerY = y + height * 0.5F;
    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);
    const std::array<std::array<float, 2>, 4> local{
        std::array<float, 2>{-width * 0.5F, -height * 0.5F},
        std::array<float, 2>{-width * 0.5F, height * 0.5F},
        std::array<float, 2>{width * 0.5F, height * 0.5F},
        std::array<float, 2>{width * 0.5F, -height * 0.5F},
    };
    const std::array<std::array<float, 2>, 4> uv{
        std::array<float, 2>{u, v},
        std::array<float, 2>{u, v + vHeight},
        std::array<float, 2>{u + uWidth, v + vHeight},
        std::array<float, 2>{u + uWidth, v},
    };

    if (!facade.Begin(LegacyPrimitive::Quads))
    {
        return false;
    }
    for (std::size_t index = 0; index < local.size(); ++index)
    {
        const float rotatedX = local[index][0] * cosine - local[index][1] * sine + centerX;
        const float rotatedY = local[index][0] * sine + local[index][1] * cosine + centerY;
        if (!facade.Color4(color[0], color[1], color[2], color[3]) ||
            !facade.TexCoord2(uv[index][0], uv[index][1]) ||
            !facade.Vertex2(rotatedX, static_cast<float>(viewport.height) - rotatedY))
        {
            return false;
        }
    }
    return facade.End();
}

float ScaleX(const LegacyRenderFacade &facade, float value) noexcept
{
    return value * static_cast<float>(facade.Viewport().width) / ReferenceWidth;
}

float ScaleY(const LegacyRenderFacade &facade, float value) noexcept
{
    return value * static_cast<float>(facade.Viewport().height) / ReferenceHeight;
}

std::optional<LegacyPrimitive> PrimitiveFor(unsigned int mode) noexcept
{
    switch (mode)
    {
    case GL_POINTS:
        return LegacyPrimitive::Points;
    case GL_LINES:
        return LegacyPrimitive::Lines;
    case GL_LINE_STRIP:
        return LegacyPrimitive::LineStrip;
    case GL_LINE_LOOP:
        return LegacyPrimitive::LineLoop;
    case GL_TRIANGLES:
        return LegacyPrimitive::Triangles;
    case GL_TRIANGLE_STRIP:
        return LegacyPrimitive::TriangleStrip;
    case GL_TRIANGLE_FAN:
        return LegacyPrimitive::TriangleFan;
    case GL_QUADS:
        return LegacyPrimitive::Quads;
    case GL_POLYGON:
        return LegacyPrimitive::Polygon;
    default:
        return std::nullopt;
    }
}

std::optional<LegacyMatrixMode> MatrixModeFor(unsigned int mode) noexcept
{
    switch (mode)
    {
    case GL_MODELVIEW:
        return LegacyMatrixMode::ModelView;
    case GL_PROJECTION:
        return LegacyMatrixMode::Projection;
    case GL_TEXTURE:
        return LegacyMatrixMode::Texture;
    default:
        return std::nullopt;
    }
}

std::optional<RenderCompareFunction> CompareFor(unsigned int function) noexcept
{
    switch (function)
    {
    case GL_LESS:
        return RenderCompareFunction::Less;
    case GL_LEQUAL:
        return RenderCompareFunction::LessOrEqual;
    case GL_GREATER:
        return RenderCompareFunction::Greater;
    case GL_ALWAYS:
        return RenderCompareFunction::Always;
    default:
        return std::nullopt;
    }
}

std::optional<RenderBlendFactor> BlendFor(unsigned int factor) noexcept
{
    switch (factor)
    {
    case GL_ZERO:
        return RenderBlendFactor::Zero;
    case GL_ONE:
        return RenderBlendFactor::One;
    case GL_SRC_ALPHA:
        return RenderBlendFactor::SrcAlpha;
    case GL_ONE_MINUS_SRC_ALPHA:
        return RenderBlendFactor::OneMinusSrcAlpha;
    case GL_SRC_COLOR:
        return RenderBlendFactor::SrcColor;
    case GL_ONE_MINUS_SRC_COLOR:
        return RenderBlendFactor::OneMinusSrcColor;
    default:
        return std::nullopt;
    }
}

std::optional<RenderCullFace> CullFaceFor(unsigned int face) noexcept
{
    switch (face)
    {
    case GL_FRONT:
        return RenderCullFace::Front;
    case GL_BACK:
        return RenderCullFace::Back;
    default:
        return std::nullopt;
    }
}

std::optional<RenderFrontFace> FrontFaceFor(unsigned int face) noexcept
{
    switch (face)
    {
    case GL_CCW:
        return RenderFrontFace::CounterClockwise;
    case GL_CW:
        return RenderFrontFace::Clockwise;
    default:
        return std::nullopt;
    }
}

std::optional<RenderStencilOperation> StencilOperationFor(unsigned int operation) noexcept
{
    switch (operation)
    {
    case GL_KEEP:
        return RenderStencilOperation::Keep;
    case GL_INCR:
        return RenderStencilOperation::Incr;
    case GL_DECR:
        return RenderStencilOperation::Decr;
    default:
        return std::nullopt;
    }
}

std::optional<RenderClientArraySemantic> ClientArrayFor(unsigned int array) noexcept
{
    switch (array)
    {
    case GL_VERTEX_ARRAY:
        return RenderClientArraySemantic::Position;
    case GL_COLOR_ARRAY:
        return RenderClientArraySemantic::Color;
    case GL_TEXTURE_COORD_ARRAY:
        return RenderClientArraySemantic::TextureCoordinate;
    case GL_NORMAL_ARRAY:
        return RenderClientArraySemantic::Normal;
    default:
        return std::nullopt;
    }
}

std::optional<RenderTextureEnvironment> TextureEnvironmentFor(int value) noexcept
{
    switch (value)
    {
    case GL_MODULATE:
        return RenderTextureEnvironment::Modulate;
    case GL_ADD:
        return RenderTextureEnvironment::Add;
    default:
        return std::nullopt;
    }
}

std::optional<RenderPolygonMode> PolygonModeFor(unsigned int mode) noexcept
{
    switch (mode)
    {
    case GL_FILL:
        return RenderPolygonMode::Fill;
    case GL_LINE:
        return RenderPolygonMode::Line;
    default:
        return std::nullopt;
    }
}

std::array<float, 16> MatrixFrom(const float *matrix) noexcept
{
    std::array<float, 16> result{};
    if (matrix != nullptr)
    {
        std::copy_n(matrix, result.size(), result.begin());
    }
    return result;
}
} // namespace

LegacyRenderFacade &SessionLegacyCalls::LegacyRender() const noexcept
{
    return sessionKeeper_.Renderer()->LegacyRender();
}

void SessionLegacyCalls::glBegin(unsigned int mode) const
{
    const auto primitive = PrimitiveFor(mode);
    if (!primitive.has_value())
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().Begin(*primitive);
}

void SessionLegacyCalls::glEnd() const
{
    (void)LegacyRender().End();
}

void SessionLegacyCalls::glVertex2f(float x, float y) const
{
    (void)LegacyRender().Vertex2(x, y);
}

void SessionLegacyCalls::glVertex3f(float x, float y, float z) const
{
    (void)LegacyRender().Vertex3(x, y, z);
}

void SessionLegacyCalls::glVertex3fv(const float *value) const
{
    if (value == nullptr)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().Vertex3(value[0], value[1], value[2]);
}

void SessionLegacyCalls::glColor3f(float red, float green, float blue) const
{
    (void)LegacyRender().Color3(red, green, blue);
}

void SessionLegacyCalls::glColor3fv(const float *value) const
{
    if (value == nullptr)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().Color3(value[0], value[1], value[2]);
}

void SessionLegacyCalls::glColor3ub(unsigned char red, unsigned char green,
                                    unsigned char blue) const
{
    (void)LegacyRender().Color3Ub(red, green, blue);
}

void SessionLegacyCalls::glColor4f(float red, float green, float blue, float alpha) const
{
    (void)LegacyRender().Color4(red, green, blue, alpha);
}

void SessionLegacyCalls::glColor4fv(const float *value) const
{
    if (value == nullptr)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().Color4(value[0], value[1], value[2], value[3]);
}

void SessionLegacyCalls::glColor4ub(unsigned char red, unsigned char green, unsigned char blue,
                                    unsigned char alpha) const
{
    (void)LegacyRender().Color4Ub(red, green, blue, alpha);
}

void SessionLegacyCalls::glNormal3f(float x, float y, float z) const
{
    (void)LegacyRender().Normal3(x, y, z);
}

void SessionLegacyCalls::glNormal3fv(const float *value) const
{
    if (value == nullptr)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().Normal3(value[0], value[1], value[2]);
}

void SessionLegacyCalls::glTexCoord2f(float u, float v) const
{
    (void)LegacyRender().TexCoord2(u, v);
}

void SessionLegacyCalls::glTexCoord2fv(const float *value) const
{
    if (value == nullptr)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().TexCoord2(value[0], value[1]);
}

void SessionLegacyCalls::glEnable(unsigned int capability) const
{
    bool accepted = true;
    switch (capability)
    {
    case GL_TEXTURE_2D:
        accepted = LegacyRender().SetTextureEnable(true);
        break;
    case GL_DEPTH_TEST:
        accepted = LegacyRender().SetDepthTestEnable(true);
        break;
    case GL_CULL_FACE:
        accepted = LegacyRender().SetCullEnable(true);
        break;
    case GL_BLEND:
        accepted = LegacyRender().SetBlendEnable(true);
        break;
    case GL_ALPHA_TEST:
        accepted = LegacyRender().SetAlphaTestEnable(true);
        break;
    case GL_FOG:
        accepted = LegacyRender().SetFogEnable(true);
        break;
    case GL_LIGHTING:
        accepted = LegacyRender().SetLightingEnable(true);
        break;
    case GL_STENCIL_TEST:
        accepted = LegacyRender().SetStencilEnable(true);
        break;
    default:
        accepted = LegacyRender().RejectUnsupported();
        break;
    }
    (void)accepted;
}

void SessionLegacyCalls::glDisable(unsigned int capability) const
{
    bool accepted = true;
    switch (capability)
    {
    case GL_TEXTURE_2D:
        accepted = LegacyRender().SetTextureEnable(false);
        break;
    case GL_DEPTH_TEST:
        accepted = LegacyRender().SetDepthTestEnable(false);
        break;
    case GL_CULL_FACE:
        accepted = LegacyRender().SetCullEnable(false);
        break;
    case GL_BLEND:
        accepted = LegacyRender().SetBlendEnable(false);
        break;
    case GL_ALPHA_TEST:
        accepted = LegacyRender().SetAlphaTestEnable(false);
        break;
    case GL_FOG:
        accepted = LegacyRender().SetFogEnable(false);
        break;
    case GL_LIGHTING:
        accepted = LegacyRender().SetLightingEnable(false);
        break;
    case GL_STENCIL_TEST:
        accepted = LegacyRender().SetStencilEnable(false);
        break;
    default:
        accepted = LegacyRender().RejectUnsupported();
        break;
    }
    (void)accepted;
}

void SessionLegacyCalls::glDepthMask(unsigned char enabled) const
{
    (void)LegacyRender().SetDepthWriteEnable(enabled != 0);
}

void SessionLegacyCalls::glDepthFunc(unsigned int function) const
{
    const auto compare = CompareFor(function);
    if (!compare.has_value())
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().SetDepthFunc(*compare);
}

void SessionLegacyCalls::glBlendFunc(unsigned int source, unsigned int destination) const
{
    const auto sourceFactor = BlendFor(source);
    const auto destinationFactor = BlendFor(destination);
    if (!sourceFactor.has_value() || !destinationFactor.has_value())
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().SetBlendFunc(*sourceFactor, *destinationFactor);
}

void SessionLegacyCalls::glAlphaFunc(unsigned int function, float reference) const
{
    const auto compare = CompareFor(function);
    if (!compare.has_value())
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().SetAlphaFunc(*compare, reference);
}

void SessionLegacyCalls::glCullFace(unsigned int face) const
{
    const auto cullFace = CullFaceFor(face);
    if (!cullFace.has_value())
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().SetCullFace(*cullFace);
}

void SessionLegacyCalls::glFrontFace(unsigned int face) const
{
    const auto frontFace = FrontFaceFor(face);
    if (!frontFace.has_value())
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().SetFrontFace(*frontFace);
}

void SessionLegacyCalls::glPolygonMode(unsigned int face, unsigned int mode) const
{
    const auto cullFace = CullFaceFor(face);
    const auto polygonMode = PolygonModeFor(mode);
    if (!cullFace.has_value() || !polygonMode.has_value())
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().SetPolygonMode(*cullFace, *polygonMode);
}

void SessionLegacyCalls::glLineWidth(float width) const
{
    (void)LegacyRender().SetLineWidth(width);
}

void SessionLegacyCalls::glMatrixMode(unsigned int mode) const
{
    const auto matrixMode = MatrixModeFor(mode);
    if (!matrixMode.has_value())
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().MatrixMode(*matrixMode);
}

void SessionLegacyCalls::glLoadIdentity() const
{
    (void)LegacyRender().LoadIdentity();
}

void SessionLegacyCalls::glLoadMatrixf(const float *matrix) const
{
    if (matrix == nullptr)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().LoadMatrix(MatrixFrom(matrix));
}

void SessionLegacyCalls::glMultMatrixf(const float *matrix) const
{
    if (matrix == nullptr)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().MultMatrix(MatrixFrom(matrix));
}

void SessionLegacyCalls::glTranslatef(float x, float y, float z) const
{
    (void)LegacyRender().Translate(x, y, z);
}

void SessionLegacyCalls::glRotatef(float angle, float x, float y, float z) const
{
    (void)LegacyRender().Rotate(angle, x, y, z);
}

void SessionLegacyCalls::glScalef(float x, float y, float z) const
{
    (void)LegacyRender().Scale(x, y, z);
}

void SessionLegacyCalls::glPushMatrix() const
{
    (void)LegacyRender().PushMatrix();
}

void SessionLegacyCalls::glPopMatrix() const
{
    (void)LegacyRender().PopMatrix();
}

void SessionLegacyCalls::glPushAttrib(unsigned int mask) const
{
    if (mask != GL_ALL_ATTRIB_BITS)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().PushAttrib();
}

void SessionLegacyCalls::glPopAttrib() const
{
    (void)LegacyRender().PopAttrib();
}

void SessionLegacyCalls::glPushClientAttrib(unsigned int mask) const
{
    if (mask != GL_CLIENT_ALL_ATTRIB_BITS)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().PushClientAttrib();
}

void SessionLegacyCalls::glPopClientAttrib() const
{
    (void)LegacyRender().PopClientAttrib();
}

void SessionLegacyCalls::glViewport(int x, int y, int width, int height) const
{
    if (x < 0 || y < 0 || width < 0 || height < 0)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().SetViewport(
        {x, y, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)});
}

void SessionLegacyCalls::glScissor(int x, int y, int width, int height) const
{
    if (x < 0 || y < 0 || width < 0 || height < 0)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().SetScissor(
        {x, y, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)});
}

void SessionLegacyCalls::glClearColor(float red, float green, float blue, float alpha) const
{
    (void)LegacyRender().SetClearColor({red, green, blue, alpha});
}

void SessionLegacyCalls::glClear(unsigned int mask) const
{
    const unsigned int known = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    if ((mask & ~known) != 0)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().Clear((mask & GL_COLOR_BUFFER_BIT) != 0, (mask & GL_DEPTH_BUFFER_BIT) != 0,
                               (mask & GL_STENCIL_BUFFER_BIT) != 0);
}

void SessionLegacyCalls::glClearDepth(double depth) const
{
    (void)LegacyRender().SetClearDepth(static_cast<float>(depth));
}

void SessionLegacyCalls::glClearStencil(int stencil) const
{
    (void)LegacyRender().SetClearStencilValue(static_cast<std::uint32_t>(stencil));
}

void SessionLegacyCalls::glColorMask(unsigned char red, unsigned char green, unsigned char blue,
                                     unsigned char alpha) const
{
    (void)LegacyRender().SetColorMask(red != 0, green != 0, blue != 0, alpha != 0);
}

void SessionLegacyCalls::glStencilFunc(unsigned int function, int reference,
                                       unsigned int mask) const
{
    const auto compare = CompareFor(function);
    if (!compare.has_value() || reference < 0)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().SetStencilFunc(*compare, static_cast<std::uint32_t>(reference), mask);
}

void SessionLegacyCalls::glStencilOp(unsigned int onFail, unsigned int onDepthFail,
                                     unsigned int onPass) const
{
    const auto fail = StencilOperationFor(onFail);
    const auto depthFail = StencilOperationFor(onDepthFail);
    const auto pass = StencilOperationFor(onPass);
    if (!fail.has_value() || !depthFail.has_value() || !pass.has_value())
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().SetStencilOp(*fail, *depthFail, *pass);
}

void SessionLegacyCalls::glTexEnvi(unsigned int target, unsigned int parameter, int value) const
{
    if (target != GL_TEXTURE_ENV || parameter != GL_TEXTURE_ENV_MODE)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    const auto environment = TextureEnvironmentFor(value);
    if (!environment.has_value())
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().SetTextureEnvironment(*environment);
}

void SessionLegacyCalls::glTexEnvf(unsigned int target, unsigned int parameter, float value) const
{
    if (target != GL_TEXTURE_ENV || parameter != GL_TEXTURE_ENV_MODE)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    const auto environment = TextureEnvironmentFor(static_cast<int>(value));
    if (!environment.has_value())
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().SetTextureEnvironment(*environment);
}

void SessionLegacyCalls::glFogf(unsigned int parameter, float value) const
{
    if (parameter == GL_FOG_DENSITY)
    {
        (void)LegacyRender().SetFogDensity(value);
        return;
    }
    (void)LegacyRender().RejectUnsupported();
}

void SessionLegacyCalls::glFogfv(unsigned int parameter, const float *value) const
{
    if (parameter != GL_FOG_COLOR || value == nullptr)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().SetFogColor({value[0], value[1], value[2], value[3]});
}

void SessionLegacyCalls::glFogi(unsigned int parameter, int value) const
{
    if (parameter != GL_FOG_MODE || value != GL_LINEAR)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().SetFogMode(RenderFogMode::Linear);
}

void SessionLegacyCalls::glShadeModel(unsigned int mode) const
{
    if (mode != GL_SMOOTH)
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().SetShadeMode(RenderShadeMode::Smooth);
}

void SessionLegacyCalls::glEnableClientState(unsigned int array) const
{
    const auto semantic = ClientArrayFor(array);
    if (!semantic.has_value())
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    LegacyRender().EnableClientArray(*semantic);
}

void SessionLegacyCalls::glDisableClientState(unsigned int array) const
{
    const auto semantic = ClientArrayFor(array);
    if (!semantic.has_value())
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    LegacyRender().DisableClientArray(*semantic);
}

void SessionLegacyCalls::glDrawArrays(unsigned int mode, int first, int count) const
{
    const auto primitive = PrimitiveFor(mode);
    if (!primitive.has_value())
    {
        (void)LegacyRender().RejectUnsupported();
        return;
    }
    (void)LegacyRender().DrawArrays(*primitive, first, count);
}

void SessionLegacyCalls::glVertexPointer(int size, unsigned int type, int stride,
                                         const void *pointer) const
{
    (void)LegacyRender().RejectUnsupported();
}

void SessionLegacyCalls::glColorPointer(int size, unsigned int type, int stride,
                                        const void *pointer) const
{
    (void)LegacyRender().RejectUnsupported();
}

void SessionLegacyCalls::glTexCoordPointer(int size, unsigned int type, int stride,
                                           const void *pointer) const
{
    (void)LegacyRender().RejectUnsupported();
}

void SessionLegacyCalls::BindTexture(int texture) const
{
    LegacyRender().BindTexture(ResolveAsset(sessionKeeper_, texture));
}

void SessionLegacyCalls::BindTextureStream(int texture) const
{
    BindTexture(texture);
}

void SessionLegacyCalls::EndTextureStream() const
{
}

void SessionLegacyCalls::EnableDepthTest() const
{
    (void)LegacyRender().SetDepthTestEnable(true);
}

void SessionLegacyCalls::DisableDepthTest() const
{
    (void)LegacyRender().SetDepthTestEnable(false);
}

void SessionLegacyCalls::EnableDepthMask() const
{
    (void)LegacyRender().SetDepthWriteEnable(true);
}

void SessionLegacyCalls::DisableDepthMask() const
{
    (void)LegacyRender().SetDepthWriteEnable(false);
}

void SessionLegacyCalls::EnableCullFace() const
{
    (void)LegacyRender().SetCullEnable(true);
}

void SessionLegacyCalls::DisableCullFace() const
{
    (void)LegacyRender().SetCullEnable(false);
}

void SessionLegacyCalls::DisableTexture(bool alphaTest) const
{
    (void)LegacyRender().SetDepthWriteEnable(true);
    (void)LegacyRender().SetAlphaTestEnable(alphaTest);
    (void)LegacyRender().SetTextureEnable(false);
}

void SessionLegacyCalls::DisableAlphaBlend() const
{
    (void)LegacyRender().SetOpaqueState();
}

void SessionLegacyCalls::EnableAlphaTest(bool depthMask) const
{
    (void)LegacyRender().SetAlphaTestState(depthMask);
}

void SessionLegacyCalls::EnableAlphaBlend() const
{
    (void)LegacyRender().SetAdditiveState();
}

void SessionLegacyCalls::EnableAlphaBlendMinus() const
{
    (void)LegacyRender().SetBlendEnable(true);
    (void)LegacyRender().SetBlendFunc(RenderBlendFactor::Zero, RenderBlendFactor::OneMinusSrcColor);
    (void)LegacyRender().SetCullEnable(false);
    (void)LegacyRender().SetDepthWriteEnable(false);
    (void)LegacyRender().SetAlphaTestEnable(false);
    (void)LegacyRender().SetTextureEnable(true);
    (void)LegacyRender().SetFogEnable(true);
}

void SessionLegacyCalls::EnableAlphaBlend2() const
{
    (void)LegacyRender().SetBlendEnable(true);
    (void)LegacyRender().SetBlendFunc(RenderBlendFactor::OneMinusSrcColor, RenderBlendFactor::One);
    (void)LegacyRender().SetCullEnable(false);
    (void)LegacyRender().SetDepthWriteEnable(false);
    (void)LegacyRender().SetAlphaTestEnable(false);
    (void)LegacyRender().SetTextureEnable(true);
    (void)LegacyRender().SetFogEnable(true);
}

void SessionLegacyCalls::EnableAlphaBlend3() const
{
    (void)LegacyRender().SetBlendEnable(true);
    (void)LegacyRender().SetBlendFunc(RenderBlendFactor::SrcAlpha,
                                      RenderBlendFactor::OneMinusSrcAlpha);
    (void)LegacyRender().SetCullEnable(false);
    (void)LegacyRender().SetDepthWriteEnable(false);
    (void)LegacyRender().SetAlphaTestEnable(false);
    (void)LegacyRender().SetTextureEnable(true);
    (void)LegacyRender().SetFogEnable(true);
}

void SessionLegacyCalls::EnableAlphaBlend4() const
{
    (void)LegacyRender().SetBlendEnable(true);
    (void)LegacyRender().SetBlendFunc(RenderBlendFactor::One, RenderBlendFactor::OneMinusSrcColor);
    (void)LegacyRender().SetCullEnable(false);
    (void)LegacyRender().SetDepthWriteEnable(false);
    (void)LegacyRender().SetAlphaTestEnable(false);
    (void)LegacyRender().SetTextureEnable(true);
    (void)LegacyRender().SetFogEnable(true);
}

void SessionLegacyCalls::EnableLightMap() const
{
    (void)LegacyRender().SetBlendEnable(true);
    (void)LegacyRender().SetBlendFunc(RenderBlendFactor::Zero, RenderBlendFactor::SrcColor);
    (void)LegacyRender().SetCullEnable(true);
    (void)LegacyRender().SetDepthWriteEnable(true);
    (void)LegacyRender().SetAlphaTestEnable(false);
    (void)LegacyRender().SetTextureEnable(true);
    (void)LegacyRender().SetFogEnable(true);
}

float SessionLegacyCalls::ConvertX(float value) const
{
    return ScaleX(LegacyRender(), value);
}

float SessionLegacyCalls::ConvertY(float value) const
{
    return ScaleY(LegacyRender(), value);
}

void SessionLegacyCalls::DrawSolidRect(int x, int y, int width, int height, float brightness) const
{
    (void)LegacyRender().SetTextureEnable(false);
    const std::array<float, 4> color{brightness, brightness, brightness, 1.0F};
    (void)EmitQuad(LegacyRender(), ConvertX(static_cast<float>(x)), ConvertY(static_cast<float>(y)),
                   ConvertX(static_cast<float>(width)), ConvertY(static_cast<float>(height)), 0.0F,
                   0.0F, 0.0F, 0.0F, color);
    (void)LegacyRender().SetTextureEnable(true);
}

void SessionLegacyCalls::RenderColor(float x, float y, float width, float height, float alpha,
                                     int flag) const
{
    (void)LegacyRender().SetTextureEnable(false);
    std::array<float, 4> color = LegacyRender().CurrentColor();
    if (alpha > 0.0F)
    {
        const float brightness = flag == 1 ? 0.0F : 1.0F;
        color = {brightness, brightness, brightness, alpha};
    }
    (void)EmitQuad(LegacyRender(), ConvertX(x), ConvertY(y), ConvertX(width), ConvertY(height),
                   0.0F, 0.0F, 0.0F, 0.0F, color);
    if (alpha > 0.0F)
    {
        (void)LegacyRender().Color4(1.0F, 1.0F, 1.0F, 1.0F);
    }
}

void SessionLegacyCalls::EndRenderColor() const
{
    (void)LegacyRender().Color4(1.0F, 1.0F, 1.0F, 1.0F);
    (void)LegacyRender().SetTextureEnable(true);
}

void SessionLegacyCalls::RenderColorBitmap(int texture, float x, float y, float width, float height,
                                           float u, float v, float uWidth, float vHeight,
                                           unsigned int color) const
{
    BindTexture(texture);
    (void)EmitQuad(LegacyRender(), ConvertX(x), ConvertY(y), ConvertX(width), ConvertY(height), u,
                   v, uWidth, vHeight, DecodeColor(color));
    (void)LegacyRender().Color4(1.0F, 1.0F, 1.0F, 1.0F);
}

void SessionLegacyCalls::RenderBitmap(int texture, float x, float y, float width, float height,
                                      float u, float v, float uWidth, float vHeight, bool scale,
                                      bool startScale, float alpha) const
{
    BindTexture(texture);
    const float effectiveX = startScale ? ConvertX(x) : x;
    const float effectiveY = startScale ? ConvertY(y) : y;
    const float effectiveWidth = scale ? ConvertX(width) : width;
    const float effectiveHeight = scale ? ConvertY(height) : height;
    const std::array<float, 4> color = alpha > 0.0F ? std::array<float, 4>{1.0F, 1.0F, 1.0F, alpha}
                                                    : LegacyRender().CurrentColor();
    (void)EmitQuad(LegacyRender(), effectiveX, effectiveY, effectiveWidth, effectiveHeight, u, v,
                   uWidth, vHeight, color);
    if (alpha > 0.0F)
    {
        (void)LegacyRender().Color4(1.0F, 1.0F, 1.0F, 1.0F);
    }
}

void SessionLegacyCalls::RenderBitmapRotate(int texture, float x, float y, float width,
                                            float height, float rotate, float u, float v,
                                            float uWidth, float vHeight) const
{
    BindTexture(texture);
    (void)EmitQuad(LegacyRender(), ConvertX(x - width * 0.5F), ConvertY(y - height * 0.5F),
                   ConvertX(width), ConvertY(height), u, v, uWidth, vHeight,
                   LegacyRender().CurrentColor(), -rotate * std::acos(-1.0F) / 180.0F);
}

void SessionLegacyCalls::RenderBitRotate(int texture, float x, float y, float width, float height,
                                         float rotate) const
{
    //RenderBitmapRotate(texture, x, y, width, height, rotate);
    x = ConvertX(x);
    y = ConvertY(y);
    width = ConvertX(width);
    height = ConvertY(height);

    BindTexture(texture);

    vec3_t p[4], p2[4];

    y = height - y;

    float cx = (width / 2.f) - (width - x);
    float cy = (height / 2.f) - (height - y);

    float ax = (-width * 0.5f) + cx;
    float bx = (width * 0.5f) + cx;
    float ay = (-height * 0.5f) + cy;
    float by = (height * 0.5f) + cy;

    Vector(ax, by, 0.f, p[0]);
    Vector(ax, ay, 0.f, p[1]);
    Vector(bx, ay, 0.f, p[2]);
    Vector(bx, by, 0.f, p[3]);

    vec3_t Angle;
    Vector(0.f, 0.f, rotate, Angle);
    float Matrix[3][4];
    AngleMatrix(Angle, Matrix);

    float c[4][2];
    c[0][0] = c[0][1] = 0.f;
    c[3][0] = 1.f;
    c[3][1] = 0.f;
    c[2][0] = c[2][1] = 1.f;
    c[1][0] = 0.f;
    c[1][1] = 1.f;

    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i < 4; i++)
    {
        glTexCoord2f(c[i][0], c[i][1]);
        VectorRotate(p[i], Matrix, p2[i]);
        glVertex2f(p2[i][0] + (LegacyRender().Viewport().width / 2.f),
                   p2[i][1] + (LegacyRender().Viewport().height / 2.f));
    }
    glEnd();
}

void SessionLegacyCalls::RenderBitmapLocalRotate(int texture, float x, float y, float width,
                                                 float height, float rotate, float u, float v,
                                                 float uWidth, float vHeight) const
{
    RenderBitmapRotate(texture, x, y, width, height, rotate, u, v, uWidth, vHeight);
}

void SessionLegacyCalls::RenderBitmapAlpha(int texture, float sx, float sy, float width,
                                           float height) const
{
    BindTexture(texture);
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
            if (column == 0 || column == 3)
            {
                color[3] = 0.0F;
            }
            if (row == 0 || row == 3)
            {
                color[3] = 0.0F;
            }
            (void)EmitQuad(LegacyRender(), ConvertX(sx + column * width * 0.25F),
                           ConvertY(sy + row * height * 0.25F), ConvertX(width * 0.25F),
                           ConvertY(height * 0.25F), column * 0.25F, row * 0.25F, 0.25F, 0.25F,
                           color);
        }
    }
}

void SessionLegacyCalls::RenderBitmapUV(int texture, float x, float y, float width, float height,
                                        float u, float v, float uWidth, float vHeight) const
{
    BindTexture(texture);
    (void)EmitQuad(LegacyRender(), ConvertX(x), ConvertY(y), ConvertX(width), ConvertY(height), u,
                   v + vHeight * 0.25F, uWidth, vHeight * 0.75F, LegacyRender().CurrentColor());
}

void SessionLegacyCalls::RenderDebugWindow()
{
    sessionKeeper_.Renderer()->RenderDebugWindow();
}

void SessionLegacyCalls::RenderDebugSphere(const vec_t *center, float radius, float red,
                                           float green, float blue)
{
    sessionKeeper_.Renderer()->RenderDebugSphere(center, radius, red, green, blue);
}

void SessionLegacyCalls::RenderDebugBox(const vec_t *origin, float sizeX, float sizeY, float sizeZ,
                                        float red, float green, float blue)
{
    sessionKeeper_.Renderer()->RenderDebugBox(origin, sizeX, sizeY, sizeZ, red, green, blue);
}

namespace
{
bool CommitCatalogUploadsOnOwner(const SessionRenderTape &tape, CGlobalBitmap &assets) noexcept
{
    std::vector<LogicalRenderAssetRevisionInput> inputs;
    try
    {
        inputs.reserve(tape.OwnerRequests().size());
        const std::span<const std::byte> payload = tape.PayloadBytes();
        for (const RenderOwnerRequest &request : tape.OwnerRequests())
        {
            const auto *upload = std::get_if<UploadLogicalAssetRgba8Request>(&request);
            if (upload == nullptr || upload->retention != RenderAssetRetention::Catalog)
            {
                continue;
            }
            inputs.push_back({upload->destination, upload->width, upload->height, upload->sampler,
                              payload.subspan(upload->payloadOffset, upload->payloadByteCount)});
        }
    }
    catch (...)
    {
        return false;
    }
    return inputs.empty() || assets.CommitRecordedRevisions(inputs);
}
} // namespace

CUIMng &SessionRenderUnit::LegacyUiManager()
{
    return sessionKeeper_.Ui()->LegacyUiManager();
}

SessionRenderUnit::SessionRenderUnit(SessionKeeper &keeper,
                                     SessionLifecycleObserver *observer) noexcept
    : SessionLegacyCalls(keeper), Random(keeper.RandomForConstruction()),
      gameplay_(keeper.GameplayForConstruction()),
      g_MapProcess(keeper.GameplayForConstruction().TheMapProcess()),
      cameraManager_(keeper.CameraManagerObject()),
      cameraProjection_(keeper.CameraProjectionObject()), g_Camera(keeper.CameraStateObject()),
      g_petProcess(keeper.PetProcessObject()), cursedTemple_(keeper.CursedTempleObject()),
      thirdChange_(keeper.ThirdChangeObject()), g_hWnd(keeper.PlatformWindowHandle()),
      g_RenderText(keeper.SessionText()), g_pUIMapName(keeper.UiForConstruction().MapName()),
      Chat(keeper.ChatStorage().Chat), WhisperRegistID(keeper.ChatStorage().WhisperRegistID),
      g_iNoMouseTime(keeper.PlatformNoMouseTime()), Destroy(keeper.PlatformDestroyRequested()),
      OpenglWindowX(keeper.PlatformOpenglWindowX()), OpenglWindowY(keeper.PlatformOpenglWindowY()),
      OpenglWindowWidth(keeper.PlatformOpenglWindowWidth()),
      OpenglWindowHeight(keeper.PlatformOpenglWindowHeight()),
      WindowWidth(keeper.PlatformWindowWidth()), WindowHeight(keeper.PlatformWindowHeight()),
      MonsterScript(keeper.MonsterScripts()), EditMonsterNumber(keeper.EditMonsterCount()),
      g_strSelectedML(keeper.AssetLanguage()), g_ErrorReport(keeper.ErrorReport()),
      modelPool_(keeper.ModelPoolObject()), boneManager_(keeper.BoneManagerObject()),
      Effects(keeper.EffectsStorage()), Particles(keeper.ParticlesStorage()),
      Joints(keeper.JointsStorage()), Points(keeper.PointsStorage()),
      Pointers(keeper.PointersStorage()), Mounts(keeper.MountsStorage()),
      Boids(keeper.BoidsStorage()), Fishs(keeper.FishsStorage()),
      Operates(keeper.OperatesStorage()), Items(keeper.ItemsStorage()),
      groundItemLabels_(keeper.GroundItemLabelStorage()), g_MixRecipeMgr(keeper.MixRecipeManager()),
      g_SocketItemMgr(keeper.SocketItemManager()), g_csItemOption(keeper.ItemOptionManager()),
      g_csQuest(keeper.QuestObject()), g_QuestMng(keeper.QuestManagerObject()),
      ObjectBlock(keeper.ObjectBlocks()), g_iActionObjectType(keeper.ActionObjectType()),
      g_iActionWorld(keeper.ActionWorld()), g_iActionTime(keeper.ActionTime()),
      g_fActionObjectVelocity(keeper.ActionObjectVelocity())

      ,
      g_blurs(keeper.BlurStorage().blurs), g_objectBlurs(keeper.BlurStorage().objectBlurs),
      Sprites(keeper.SpritesStorage()), Leaves(keeper.LeafStorage().Leaves),
      m_qSV(keeper.ShadowVolumeStorage().m_qSV), Distance(keeper.CollisionDistance()),
      CollisionPosition(keeper.CollisionPosition()), SelectXF(keeper.TerrainSelectX()),
      SelectYF(keeper.TerrainSelectY()),
      g_wtMatchResult(keeper.WelfareTempleStorage().g_wtMatchResult),
      g_wtMatchTimeLeft(keeper.WelfareTempleStorage().g_wtMatchTimeLeft),
      gMapManager(keeper.MapManagerObject()), cameraMove_(keeper.CameraMoveObject()),
      g_Direction(keeper.DirectionObject()), gSkillManager(keeper.SkillManagerObject()),
      g_SkillEffects(keeper.SkillEffectManagerObject()),
      g_SummonSystem(keeper.SummonSystemObject()), g_CMonkSystem(keeper.MonkSystemObject()),
      g_pSingleTextInputBox(keeper.SingleTextInputBox()),
      g_pSinglePasswdInputBox(keeper.SinglePasswordInputBox()),
      g_MessageBox(keeper.MessageBoxManagerObject()), g_PortalMgr(keeper.PortalManagerObject()),
      g_ConsoleDebug(keeper.ConsoleDebug()), g_pTimer(keeper.FrameTimer()),
      g_timer2StartTickTime(keeper.FrameTimer2StartTickTime()),
      g_bRenderBoundingBox(keeper.FrameRenderBoundingBox()),
      FPS_ANIMATION_FACTOR(keeper.FrameAnimationFactor()), WorldTime(keeper.FrameWorldTime()),
      m_ExeVersion(keeper.ExecutableVersion()), FPS_AVG(keeper.FrameFpsAverage()),
      g_bShowDebugInfo(keeper.FrameDebugInfoEnabled()),
      g_bShowFpsCounter(keeper.FrameFpsCounterEnabled()), s_frameTimesMs(keeper.FrameTimesMs()),
      s_frameIndex(keeper.FrameHistoryIndex()), s_frameCount(keeper.FrameHistoryCount()),
      s_highestFps(keeper.FrameHighestFps()), s_avgFps(keeper.FrameAverageFps()),
      s_onePercentLow(keeper.FrameOnePercentLow()), s_slowestFrameFps(keeper.FrameSlowestFps()),
      g_frameProfilerAccumulatorMs(keeper.FrameProfilerAccumulatorMs()), facade_(keeper.Id()),
      tooltipLayer_(keeper), petFrameLayer_(keeper), partyFrameLayer_(keeper),
      mainFrameLayer_(keeper), topMenuLayer_(keeper), moveCommandLayer_(keeper),
      playerNameLayer_(keeper), mainSceneFramePresentable_(false), observer_(observer)
{
    (void)InitializeEffectQuadGeometry();
    (void)sessionKeeper_.RegisterRenderer(*this);
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::RenderUnitConstructed);
    }
}

bool SessionRenderUnit::InitializeEffectQuadGeometry() noexcept
{
    constexpr std::size_t CornerCount = 4;
    try
    {
        auto vertices = std::make_shared<std::vector<RenderTapeVertex>>(CornerCount);
        for (std::uint32_t corner = 0; corner < CornerCount; ++corner)
        {
            (*vertices)[corner].bmdSource[3] = corner;
        }
        auto indices = std::make_shared<std::vector<std::uint32_t>>(
            std::initializer_list<std::uint32_t>{0, 1, 2, 0, 2, 3});
        const std::uint64_t revision = AllocateGeometryRevision();
        if (revision == 0)
        {
            return false;
        }
        effectQuadGeometry_ = {
            {sessionKeeper_.Id().RawValue(), 1, revision}, std::move(vertices), std::move(indices)};
        return IsValid(effectQuadGeometry_);
    }
    catch (...)
    {
        return false;
    }
}

SessionRenderUnit::~SessionRenderUnit()
{
    (void)WaitForModernUiPreparation();
    lastCompletedTarget_.reset();
    pendingTargetCopies_.clear();
    ReleaseGroundItemLabelCache();
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::RenderUnitDestroyed);
    }
}

bool SessionRenderUnit::PrepareFrameOnOwner(SessionGeneration generation,
                                            std::uint64_t frameSequence,
                                            std::uint64_t surfaceGeneration,
                                            std::uint32_t viewportWidth,
                                            std::uint32_t viewportHeight,
                                            SessionRenderTapeRecording recording) noexcept
{
    SessionDisplayView *const display = sessionKeeper_.Display();
    SessionLifecycleState *const lifecycle = sessionKeeper_.Lifecycle();
    const World *const world = sessionKeeper_.WorldUnit();
    if (world == nullptr || world->BlocksFrameAdmission() || preparedRecording_.has_value() ||
        preparedTape_.has_value() || display == nullptr || lifecycle == nullptr ||
        (!display->IsVisible() && SceneFlag != WEBZEN_SCENE) ||
        display->SurfaceGeneration() != surfaceGeneration ||
        display->LocalRect().width != viewportWidth ||
        display->LocalRect().height != viewportHeight || !lifecycle->BeginRender())
    {
        return false;
    }

    bool targetStateSurfaceChanged = lastCompletedTarget_.has_value() &&
                                     lastCompletedTarget_->surfaceGeneration != surfaceGeneration;
    for (const auto &[destination, pending] : pendingTargetCopies_)
    {
        (void)destination;
        if (targetStateSurfaceChanged)
        {
            break;
        }
        targetStateSurfaceChanged = pending.surfaceGeneration != surfaceGeneration;
    }
    if (targetStateSurfaceChanged)
    {
        lastCompletedTarget_.reset();
        pendingTargetCopies_.clear();
    }

    if (!PrepareWorldTerrainOnOwner(generation))
    {
        return false;
    }

    PrepareCharacterReaders();
    preparedGeneration_ = generation;
    preparedFrameSequence_ = frameSequence;
    preparedSurfaceGeneration_ = surfaceGeneration;
    preparedViewportWidth_ = viewportWidth;
    preparedViewportHeight_ = viewportHeight;
    const SessionWorkspace *const workspace =
        sessionKeeper_.ApplicationKeeperRef().SessionWorkspaceUnit();
    const SessionDisplayRect modernUiViewport =
        workspace == nullptr ? display->LocalRect() : workspace->GameContentRect();
    preparedModernUiViewportWidth_ = modernUiViewport.width;
    preparedModernUiViewportHeight_ = modernUiViewport.height;
    preparedRecording_.emplace(std::move(recording));
    preparedDiagnostics_ = {};
    if (SessionUiUnit *const ui = sessionKeeper_.Ui())
    {
        preparedScreenshot_ = ui->TakeScreenshotRequest();
    }
    return true;
}

bool SessionRenderUnit::SubmitModernUiPreparation(UI::Modern::RmlUiRuntime &runtime) noexcept
{
    const SessionDisplayView *const display = sessionKeeper_.Display();
    if (!preparedGeneration_.has_value() || display == nullptr)
    {
        return false;
    }
    if (!display->IsVisible())
    {
        std::lock_guard lock(modernUiPreparationMutex_);
        modernUiPreparationState_ = ModernUiPreparationState::NotRequired;
        return true;
    }

    {
        std::lock_guard lock(modernUiPreparationMutex_);
        if (modernUiPreparationState_ != ModernUiPreparationState::NotRequired)
        {
            return false;
        }
        modernUiPreparationState_ = ModernUiPreparationState::Queued;
    }
    if (runtime.Submit([this]() noexcept {
            const bool succeeded = PrepareModernUiOnWorker();
            {
                std::lock_guard lock(modernUiPreparationMutex_);
                modernUiPreparationState_ = succeeded ? ModernUiPreparationState::Succeeded
                                                      : ModernUiPreparationState::Failed;
            }
            modernUiPreparationReady_.notify_all();
        }))
    {
        return true;
    }

    {
        std::lock_guard lock(modernUiPreparationMutex_);
        modernUiPreparationState_ = ModernUiPreparationState::Failed;
    }
    modernUiPreparationReady_.notify_all();
    return false;
}

LogicalGeometryAssetRef SessionRenderUnit::MakeModernUiGeometryAssetRef(
    std::uint64_t revision) const noexcept
{
    return {sessionKeeper_.Id().RawValue(), preparedGeneration_->RawValue(), revision};
}

bool SessionRenderUnit::PrepareModernUiOnWorker() noexcept
{
    const int width = static_cast<int>(preparedModernUiViewportWidth_);
    const int height = static_cast<int>(preparedModernUiViewportHeight_);
    return tooltipLayer_.PrepareOnWorker(width, height) &&
           petFrameLayer_.PrepareOnWorker(width, height) &&
           partyFrameLayer_.PrepareOnWorker(width, height) &&
           mainFrameLayer_.PrepareOnWorker(width, height) &&
           topMenuLayer_.PrepareOnWorker(width, height) &&
           moveCommandLayer_.PrepareOnWorker(width, height) &&
           playerNameLayer_.PrepareOnWorker(width, height) &&
           (sessionKeeper_.Ui() == nullptr ||
            sessionKeeper_.Ui()->LegacyUiManager().PrepareModernUiOnWorker(width, height));
}

bool SessionRenderUnit::WaitForModernUiPreparation() noexcept
{
    std::unique_lock lock(modernUiPreparationMutex_);
    modernUiPreparationReady_.wait(lock, [this]() noexcept {
        return modernUiPreparationState_ != ModernUiPreparationState::Queued;
    });
    return modernUiPreparationState_ != ModernUiPreparationState::Failed;
}

bool SessionRenderUnit::RecordPreparedFrameWorkerSafe() noexcept
{
    if (!preparedGeneration_.has_value() || !preparedRecording_.has_value())
    {
        return false;
    }
    const SessionGeneration generation = *preparedGeneration_;
    const std::uint64_t frameSequence = preparedFrameSequence_;
    if (effectQuadGeometry_.asset.generation != generation.RawValue())
    {
        effectQuadGeometry_.asset.generation = generation.RawValue();
        if (!IsValid(effectQuadGeometry_))
        {
            return false;
        }
    }
    if (!facade_.BeginFrame(sessionKeeper_.Id(), generation, frameSequence,
                            preparedSurfaceGeneration_, preparedViewportWidth_,
                            preparedViewportHeight_, std::move(*preparedRecording_)))
    {
        preparedRecording_.reset();
        return false;
    }
    preparedRecording_.reset();
    activeRenderTapePass_.reset();
    recordingSucceeded_ = true;
    drawPoses_.BeginFrame();

    const auto abortRecording = [&]() noexcept {
        preparedDiagnostics_ = facade_.Diagnostics();
        facade_.Abort();
        activeRenderTapePass_.reset();
    };

    if (!BeginRenderTapePass(RenderTapePass::Terrain))
    {
        abortRecording();
        return false;
    }

    const bool screenshotRecorded =
        !preparedScreenshot_.has_value() ||
        facade_.DownloadTargetRgba8(
            preparedScreenshot_->sessionId, preparedScreenshot_->generation,
            preparedScreenshot_->sourceSurfaceGeneration, preparedScreenshot_->sourceFrameSequence,
            preparedScreenshot_->rect, true, preparedScreenshot_->requestId);
    if (!screenshotRecorded || !facade_.Clear(true, true, false))
    {
        abortRecording();
        return false;
    }

    SessionFrameView *const frame = sessionKeeper_.Frame();
    const SessionRandom::DrawScope drawRandom(sessionKeeper_.RandomForConstruction());
    const bool rendered =
        frame != nullptr && frame->HasRenderFunction() ? frame->Render() : RenderScene();
    if (!rendered || !EndRenderTapePass())
    {
        abortRecording();
        return false;
    }

    preparedTape_ = facade_.Finalize();
    if (!preparedTape_.has_value())
    {
        abortRecording();
        return false;
    }
    return true;
}

std::optional<SessionRenderTape> SessionRenderUnit::CompletePreparedFrameOnOwner(
    bool accept) noexcept
{
    accept = WaitForModernUiPreparation() && accept;
    preparedCharacters_.clear();
    (void)g_RenderText.PublishQueuedGlyphsOnOwner();
    if (accept && preparedTape_.has_value())
    {
        accept = CommitCatalogUploadsOnOwner(*preparedTape_, sessionKeeper_.BitmapRegistry());
    }
    if (!accept || !preparedTape_.has_value())
    {
        if (preparedDiagnostics_.failure != RenderTapeFailure::None &&
            preparedDiagnostics_.failure != lastReportedFailure_)
        {
            const char *const format =
                "[RenderTape] failure=%u pass=%u source-row=%u order=%llu frame=%llu\n";
            std::fprintf(stderr, format, static_cast<unsigned int>(preparedDiagnostics_.failure),
                         static_cast<unsigned int>(preparedDiagnostics_.pass),
                         preparedDiagnostics_.sourceInventoryRowId,
                         static_cast<unsigned long long>(preparedDiagnostics_.stableOrder),
                         static_cast<unsigned long long>(preparedFrameSequence_));
            FILE *failureLog = nullptr;
            if (fopen_s(&failureLog, "render-tape-failures.log", "a") == 0)
            {
                std::fprintf(failureLog, format,
                             static_cast<unsigned int>(preparedDiagnostics_.failure),
                             static_cast<unsigned int>(preparedDiagnostics_.pass),
                             preparedDiagnostics_.sourceInventoryRowId,
                             static_cast<unsigned long long>(preparedDiagnostics_.stableOrder),
                             static_cast<unsigned long long>(preparedFrameSequence_));
                std::fclose(failureLog);
            }
        }
        lastReportedFailure_ = preparedDiagnostics_.failure;
        lastRenderTapeFailureDiagnostics_ = preparedDiagnostics_;
        if (preparedScreenshot_.has_value())
        {
            if (SessionUiUnit *const ui = sessionKeeper_.Ui())
            {
                ui->RestoreScreenshotRequest(std::move(*preparedScreenshot_));
            }
            preparedScreenshot_.reset();
        }
        preparedTape_.reset();
        preparedRecording_.reset();
        preparedGeneration_.reset();
        {
            std::lock_guard lock(modernUiPreparationMutex_);
            modernUiPreparationState_ = ModernUiPreparationState::NotRequired;
        }
        return std::nullopt;
    }

    CaptureRenderTapeDebugStats(*preparedTape_);
    TrackPendingTargetCopies(*preparedTape_);
    if (preparedScreenshot_.has_value())
    {
        if (SessionUiUnit *const ui = sessionKeeper_.Ui())
        {
            (void)ui->SubmitScreenshotRequest(std::move(*preparedScreenshot_),
                                              preparedTape_->FrameSequence());
        }
        preparedScreenshot_.reset();
    }
    if (SessionLifecycleState *const lifecycle = sessionKeeper_.Lifecycle())
    {
        lifecycle->MarkRendered();
    }
    lastReportedFailure_ = RenderTapeFailure::None;
    lastRenderTapeFailureDiagnostics_ = {};
    preparedGeneration_.reset();
    {
        std::lock_guard lock(modernUiPreparationMutex_);
        modernUiPreparationState_ = ModernUiPreparationState::NotRequired;
    }
    return std::exchange(preparedTape_, std::nullopt);
}

bool SessionRenderUnit::BeginRenderTapePass(RenderTapePass pass) noexcept
{
    if (pass == RenderTapePass::UserInterface && !WaitForModernUiPreparation())
    {
        recordingSucceeded_ = false;
        return false;
    }
    if (!facade_.IsRecording())
    {
        return true;
    }
    if (activeRenderTapePass_ == pass)
    {
        return true;
    }

    const auto fog = SessionFogPassConstants::TryCreate(
        FogEnable, sessionKeeper_.CameraStateObject().ViewFar, FogColor);
    if (!fog.has_value())
    {
        facade_.LatchFacadeFailure(RenderTapeFailure::NonFiniteValue);
        recordingSucceeded_ = false;
        return false;
    }
    if ((activeRenderTapePass_.has_value() && !facade_.EndPass()) || !facade_.BeginPass(pass, *fog))
    {
        recordingSucceeded_ = false;
        return false;
    }
    activeRenderTapePass_ = pass;
    return true;
}

bool SessionRenderUnit::EndRenderTapePass() noexcept
{
    if (!activeRenderTapePass_.has_value())
    {
        return false;
    }
    const bool ended = facade_.EndPass();
    activeRenderTapePass_.reset();
    return ended;
}

bool SessionRenderUnit::CompleteRender(const SessionReplayCompletion &completion) noexcept
{
    const SessionDisplayView *const display = sessionKeeper_.Display();
    if (!completion.succeeded || completion.sessionId != sessionKeeper_.Id() ||
        !IsValid(completion) || display == nullptr ||
        display->SurfaceGeneration() != completion.surfaceGeneration)
    {
        return false;
    }
    if (lastCompletedTarget_.has_value() &&
        (completion.generation < lastCompletedTarget_->generation ||
         (completion.generation == lastCompletedTarget_->generation &&
          completion.frameSequence <= lastCompletedTarget_->frameSequence)))
    {
        return false;
    }
    lastCompletedTarget_ = completion;
    return true;
}

bool SessionRenderUnit::CompleteRenderRequest(const RenderOwnerRequestCompletion &completion,
                                              std::span<const std::byte> payload) noexcept
{
    const SessionDisplayView *const display = sessionKeeper_.Display();
    if (!IsValid(completion) || completion.sessionId != sessionKeeper_.Id() ||
        !completion.succeeded || display == nullptr ||
        display->SurfaceGeneration() != completion.surfaceGeneration ||
        completion.payloadByteCount != payload.size())
    {
        return false;
    }
    if (completion.kind == RenderOwnerRequestKind::CopyTargetToLogicalTexture)
    {
        return CompletePendingTargetCopy(completion, payload);
    }
    if (completion.kind == RenderOwnerRequestKind::DownloadTargetRgba8)
    {
        return sessionKeeper_.Ui() != nullptr &&
               sessionKeeper_.Ui()->CompleteScreenshot(completion, payload);
    }
    return false;
}

void SessionRenderUnit::TrackPendingTargetCopies(const SessionRenderTape &tape) noexcept
{
    for (const RenderOwnerRequest &request : tape.OwnerRequests())
    {
        const auto *const copy = std::get_if<CopyTargetToLogicalTextureRequest>(&request);
        if (copy == nullptr)
        {
            continue;
        }
        const RenderOwnerRequestCompletion expected{
            RenderOwnerRequestKind::CopyTargetToLogicalTexture,
            0,
            tape.Id(),
            tape.Generation(),
            tape.FrameSequence(),
            tape.SurfaceGeneration(),
            true,
            copy->destination,
            0,
            0,
            copy->sourceRect.width,
            copy->sourceRect.height,
            copy->sampler};
        try
        {
            pendingTargetCopies_.insert_or_assign(copy->destination, expected);
        }
        catch (...)
        {
            return;
        }
    }
}

void SessionRenderUnit::CaptureRenderTapeDebugStats(const SessionRenderTape &tape) noexcept
{
    renderTapeDebugStats_.frameSequence = tape.FrameSequence();
    renderTapeDebugStats_.generation = tape.Generation().RawValue();
    renderTapeDebugStats_.surfaceGeneration = tape.SurfaceGeneration();
    renderTapeDebugStats_.viewportWidth = tape.ViewportWidth();
    renderTapeDebugStats_.viewportHeight = tape.ViewportHeight();
    renderTapeDebugStats_.entries = tape.Entries().size();
    renderTapeDebugStats_.clears = tape.Clears().size();
    renderTapeDebugStats_.draws = tape.Draws().size();
    renderTapeDebugStats_.vertices = tape.Vertices().size();
    renderTapeDebugStats_.indices = tape.Indices().size();
    renderTapeDebugStats_.quadInstances = tape.QuadInstances().size();
    renderTapeDebugStats_.particleInstances = tape.ParticleInstances().size();
    renderTapeDebugStats_.trailSamples = tape.TrailSamples().size();
    renderTapeDebugStats_.trailSegments = tape.TrailInstances().size();
    renderTapeDebugStats_.rigidInstances = tape.RigidInstances().size();
    renderTapeDebugStats_.constants = tape.Constants().size();
    renderTapeDebugStats_.assets = tape.LogicalAssets().size();
    renderTapeDebugStats_.geometryAssets = tape.GeometryAssets().size();
    renderTapeDebugStats_.geometryBytes = tape.GeometryAssetBytes();
    renderTapeDebugStats_.ownerRequests = tape.OwnerRequests().size();
    renderTapeDebugStats_.payloadBytes = tape.PayloadBytes().size();
    renderTapeDebugStats_.storageBytes = tape.StorageBytes();
}

bool SessionRenderUnit::CompletePendingTargetCopy(const RenderOwnerRequestCompletion &completion,
                                                  std::span<const std::byte> payload) noexcept
{
    const auto found = pendingTargetCopies_.find(completion.destination);
    if (found == pendingTargetCopies_.end())
    {
        return false;
    }
    const RenderOwnerRequestCompletion &pending = found->second;
    if (pending.requestId != completion.requestId || pending.sessionId != completion.sessionId ||
        pending.generation != completion.generation ||
        pending.frameSequence != completion.frameSequence ||
        pending.surfaceGeneration != completion.surfaceGeneration ||
        pending.sampler != completion.sampler || pending.width != completion.width ||
        pending.height != completion.height)
    {
        return false;
    }
    try
    {
        if (!sessionKeeper_.TextureNamespace().CommitOwnerProducedRevision(
                completion.destination, completion.width, completion.height, completion.sampler,
                std::make_shared<const std::vector<std::byte>>(payload.begin(), payload.end())))
        {
            return false;
        }
    }
    catch (...)
    {
        return false;
    }
    pendingTargetCopies_.erase(found);
    return true;
}

void SessionLegacyCalls::AdvanceDarkHorseSkill(OBJECT *object, BMD *model, bool emit)
{
    sessionKeeper_.Visual()->AdvanceDarkHorseSkill(object, model, emit);
}
void SessionLegacyCalls::AdvanceSkillEarthQuake(CHARACTER *character, OBJECT *object, BMD *model,
                                                WorldCharacterVisualState &visual, int maxSkill)
{
    sessionKeeper_.Visual()->AdvanceSkillEarthQuake(character, object, model, visual, maxSkill);
}

void SessionLegacyCalls::RenderEffectShadows()
{
    sessionKeeper_.Renderer()->RenderEffectShadows();
}

void SessionLegacyCalls::RenderJoints(BYTE bRenderOneMore)
{
    sessionKeeper_.Renderer()->RenderJoints(bRenderOneMore);
}

void SessionLegacyCalls::RenderEffects(bool bRenderBlendMesh)
{
    sessionKeeper_.Renderer()->RenderEffects(bRenderBlendMesh);
}

void SessionLegacyCalls::RenderAfterEffects(bool renderBlendMesh)
{
    sessionKeeper_.Renderer()->RenderAfterEffects(renderBlendMesh);
}

void SessionLegacyCalls::RenderParticles(BYTE byRenderOneMore)
{
    sessionKeeper_.Renderer()->RenderParticles(byRenderOneMore);
}

void SessionLegacyCalls::RenderBlurs()
{
    sessionKeeper_.Renderer()->RenderBlurs();
}
void SessionLegacyCalls::RenderObjectBlurs()
{
    sessionKeeper_.Renderer()->RenderObjectBlurs();
}
// 3D Ư��ȿ�� ���� �Լ�
// *** �Լ� ����: 3

void SessionLegacyCalls::RenderLeaves()
{
    sessionKeeper_.Renderer()->RenderLeaves();
}

void SessionLegacyCalls::RenderCircle(int type, const vec3_t objectPosition, float scaleBottom,
                                      float scaleTop, float height, float rotation, float lightTop,
                                      float textureV)
{
    sessionKeeper_.Renderer()->RenderCircle(type, objectPosition, scaleBottom, scaleTop, height,
                                            rotation, lightTop, textureV);
}

void SessionLegacyCalls::RenderCircle2D(int type, vec_t *screenPosition, float scaleBottom,
                                        float scaleTop, float height, float rotation,
                                        float textureV, float textureVScale)
{
    sessionKeeper_.Renderer()->RenderCircle2D(type, screenPosition, scaleBottom, scaleTop, height,
                                              rotation, textureV, textureVScale);
}

void SessionLegacyCalls::RenderNumberPoints(const vec3_t position, int number, const vec3_t color,
                                            float alpha, float scale)
{
    sessionKeeper_.Renderer()->RenderNumberPoints(position, number, color, alpha, scale);
}

void SessionLegacyCalls::RenderPoints(BYTE byRenderOneMore)
{
    sessionKeeper_.Renderer()->RenderPoints(byRenderOneMore);
}

void SessionLegacyCalls::RenderPointers()
{
    sessionKeeper_.Renderer()->RenderPointers();
}

void SessionLegacyCalls::RenderSprites(BYTE byRenderOneMore)
{
    sessionKeeper_.Renderer()->RenderSprites(byRenderOneMore);
}

void SessionLegacyCalls::RenderSprite(const OBJECT *object, const OBJECT *owner)
{
    sessionKeeper_.Renderer()->RenderSprite(object, owner);
}

void SessionLegacyCalls::SMD2BMDModel(int id, int actions)
{
    sessionKeeper_.Renderer()->SMD2BMDModel(id, actions);
}
void SessionLegacyCalls::SMD2BMDAnimation(int id, bool lockPosition)
{
    sessionKeeper_.Renderer()->SMD2BMDAnimation(id, lockPosition);
}
void SessionLegacyCalls::FixupSMD()
{
    sessionKeeper_.Renderer()->FixupSMD();
}

void SessionLegacyCalls::InsertShadowVolume(CShadowVolume *shadowVolume)
{
    sessionKeeper_.Renderer()->InsertShadowVolume(shadowVolume);
}

void SessionLegacyCalls::RenderShadowVolumesAsFrame()
{
    sessionKeeper_.Renderer()->RenderShadowVolumesAsFrame();
}

void SessionLegacyCalls::ShadeWithShadowVolumes()
{
    sessionKeeper_.Renderer()->ShadeWithShadowVolumes();
}

void SessionLegacyCalls::RenderShadowToScreen()
{
    sessionKeeper_.Renderer()->RenderShadowToScreen();
}

void SessionLegacyCalls::glViewport2(int x, int y, int width, int height)
{
    sessionKeeper_.Renderer()->glViewport2(x, y, width, height);
}

// Perspective setup for item/3D-UI rendering. Sets GL perspective AND updates
// g_Camera perspective cache so item rendering can compute screen positions.
// Callers should wrap the entire item-rendering block in SaveCameraPerspective /
// RestoreCameraPerspective to avoid leaking FOV=1 values to ScreenToWorldRay.
void SessionLegacyCalls::gluPerspective2(float fov, float aspect, float zNear, float zFar)
{
    sessionKeeper_.Renderer()->gluPerspective2(fov, aspect, zNear, zFar);
}

// Saved camera state for save/restore around item rendering blocks.
// Item rendering calls gluPerspective2 (corrupts PerspectiveX/Y/ScreenCenter)
// and GetOpenGLMatrix(g_Camera.Matrix) (corrupts the camera matrix). Both must
// be restored so ScreenToWorldRay reads correct values for click detection.
void SessionLegacyCalls::SaveCameraPerspective()
{
    sessionKeeper_.Renderer()->SaveCameraPerspective();
}
void SessionLegacyCalls::RestoreCameraPerspective()
{
    sessionKeeper_.Renderer()->RestoreCameraPerspective();
}
void SessionLegacyCalls::UpdateMousePositionn()
{
    sessionKeeper_.Renderer()->UpdateMousePositionn();
}

void SessionLegacyCalls::InitCollisionDetectLineToFace()
{
    sessionKeeper_.Renderer()->InitCollisionDetectLineToFace();
}

bool SessionLegacyCalls::CollisionDetectLineToFace(vec_t *position, vec_t *target, int polygon,
                                                   float *vertex1, float *vertex2, float *vertex3,
                                                   float *vertex4, vec_t *normal, bool collision)
{
    return sessionKeeper_.Renderer()->CollisionDetectLineToFace(
        position, target, polygon, vertex1, vertex2, vertex3, vertex4, normal, collision);
}

void SessionLegacyCalls::BeginOpengl(int x, int y, int width, int height)
{
    sessionKeeper_.Renderer()->BeginOpengl(x, y, width, height);
}

void SessionLegacyCalls::RenderSprite(int texture, const vec3_t position, float width, float height,
                                      const vec3_t light, float rotation, float u, float v,
                                      float uWidth, float vHeight)
{
    sessionKeeper_.Renderer()->RenderSprite(texture, position, width, height, light, rotation, u, v,
                                            uWidth, vHeight);
}

void SessionLegacyCalls::RenderSpriteUV(int texture, vec3_t position, float width, float height,
                                        float (*uv)[2], vec3_t light[4], float alpha)
{
    sessionKeeper_.Renderer()->RenderSpriteUV(texture, position, width, height, uv, light, alpha);
}

void SessionLegacyCalls::RenderNumber(const vec3_t position, int number, const vec3_t color,
                                      float alpha, float scale)
{
    sessionKeeper_.Renderer()->RenderNumber(position, number, color, alpha, scale);
}

float SessionLegacyCalls::RenderNumber2D(float x, float y, int number, float width, float height)
{
    return sessionKeeper_.Renderer()->RenderNumber2D(x, y, number, width, height);
}

void SessionLegacyCalls::BeginBitmap()
{
    sessionKeeper_.Renderer()->BeginBitmap();
}

void SessionLegacyCalls::EndOpengl()
{
    sessionKeeper_.Renderer()->EndOpengl();
}
void SessionLegacyCalls::EndBitmap()
{
    sessionKeeper_.Renderer()->EndBitmap();
}
void SessionLegacyCalls::BeginSprite()
{
    sessionKeeper_.Renderer()->BeginSprite();
}
void SessionLegacyCalls::EndSprite()
{
    sessionKeeper_.Renderer()->EndSprite();
}
void SessionLegacyCalls::RenderBox(float matrix[3][4])
{
    sessionKeeper_.Renderer()->RenderBox(matrix);
}
void SessionLegacyCalls::RenderPlane3D(float width, float height, float matrix[3][4])
{
    sessionKeeper_.Renderer()->RenderPlane3D(width, height, matrix);
}
void SessionLegacyCalls::RenderPointRotate(int texture, float ix, float iy, float iWidth,
                                           float iHeight, float x, float y, float width,
                                           float height, float rotate, float rotateLocation,
                                           float uWidth, float vHeight, int number)
{
    sessionKeeper_.Renderer()->RenderPointRotate(texture, ix, iy, iWidth, iHeight, x, y, width,
                                                 height, rotate, rotateLocation, uWidth, vHeight,
                                                 number);
}

bool SessionLegacyCalls::LoadBitmapW(const wchar_t *fileName, std::uint32_t textureIndex,
                                     LegacyTextureFilter filter, LegacyTextureWrap wrapMode,
                                     bool check, bool fullPath) const
{
    return sessionKeeper_.LoadSessionBitmap(fileName, textureIndex, filter, wrapMode, check,
                                            fullPath);
}

void SessionLegacyCalls::DeleteBitmap(std::uint32_t textureIndex, bool)
{
    sessionKeeper_.DeleteSessionBitmap(textureIndex);
}

/**
 * @brief Updates login scene camera animation along predefined waypoint path.
 */

bool SessionRenderUnit::NewRenderLogInScene()
{
    sessionKeeper_.SpritesStorage().BeginDraw();
    if (!InitLogIn)
        return false;

    FogEnable = true;
    const SessionDisplayRect renderRect = sessionKeeper_.Display()->LocalRect();
    const int renderWidth = static_cast<int>(renderRect.width);

    vec3_t pos;
    VectorCopy(g_Camera.Position, pos);
    if (cameraMove_.IsCameraMove())
    {
        VectorCopy(g_Camera.Position, pos);
    }

    int Width, Height;

    glColor3f(1.f, 1.f, 1.f);

    Height = REFERENCE_HEIGHT;
    Width = GetScreenWidth();
    glClearColor(0.f, 0.f, 0.f, 1.f);

    // Set ViewFar BEFORE BeginOpengl so the projection matrix covers the full render distance
    g_Camera.ViewFar = LoginSceneCameraDefaults::RENDER_TERRAIN_DIST;
    g_Camera.ViewNear = 100.f; // Push near plane out to preserve z-buffer precision

    if (!BeginRenderTapePass(RenderTapePass::Terrain))
        return false;
    BeginOpengl(0, 0, Width, Height);

    const auto beginWorldPass = [&](RenderTapePass pass) {
        return SwitchWorldRenderTapePass(pass, 0, 0, Width, Height);
    };

    // Build the terrain hull from the actual login-camera matrix. The default
    // camera path supports the tour angles and avoids recording the full map.
    CreateFrustrum2D(g_Camera.Position);

    if (!LegacyUiManager().m_CreditWin->IsShow())
    {
        RenderTerrain(false);
        if (!beginWorldPass(RenderTapePass::Characters))
            return false;
        RenderCharactersClient();
        RenderCharacterAttachments();
        if (!beginWorldPass(RenderTapePass::Objects))
            return false;
        RenderObjects();
        if (!beginWorldPass(RenderTapePass::Effects))
            return false;
        RenderJoints();
        RenderEffects();
        if (!beginWorldPass(RenderTapePass::Sprites))
            return false;
        if (!beginWorldPass(RenderTapePass::Effects))
            return false;
        RenderLeaves();
        RenderBoids();
        if (!beginWorldPass(RenderTapePass::Objects))
            return false;
        RenderObjects_AfterCharacter();
    }

    if (!beginWorldPass(RenderTapePass::Sprites))
    {
        return false;
    }
    BeginSprite();
    RenderSprites();
    RenderParticles();
    EndSprite();
    EndOpengl();
    if (!BeginRenderTapePass(RenderTapePass::UserInterface))
    {
        return false;
    }
    BeginBitmap();

    if (cameraMove_.IsTourMode())
    {
        EnableAlphaBlend();
        glColor4f(g_fMULogoAlpha - 0.3f, g_fMULogoAlpha - 0.3f, g_fMULogoAlpha - 0.3f,
                  g_fMULogoAlpha - 0.3f);
        RenderBitmap(BITMAP_LOG_IN + 17, 320.0f - 128.0f * 0.8f, 25.0f, 256.0f * 0.8f,
                     128.0f * 0.8f);
        EnableAlphaTest();
        glColor4f(g_fMULogoAlpha, g_fMULogoAlpha, g_fMULogoAlpha, g_fMULogoAlpha);
        RenderBitmap(BITMAP_LOG_IN + 16, 320.0f - 128.0f * 0.8f, 25.0f, 256.0f * 0.8f,
                     128.0f * 0.8f);
    }

    SIZE Size;
    wchar_t Text[100];

    g_RenderText.SetFont(LegacyFontRole::Normal);

    InputTextWidth = 256;
    glColor3f(0.8f, 0.7f, 0.6f);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetBgColor(0, 0, 0, 128);

    wcscpy_s(Text, 100, I18N::Game::CCopyright2001Webzen);
    g_RenderText.MeasureText(Text, lstrlen(Text), &Size);
    g_RenderText.RenderText(335 - Size.cx * REFERENCE_WIDTH / renderWidth,
                            REFERENCE_HEIGHT - Size.cy * REFERENCE_WIDTH / renderWidth - 1, Text);

    wcscpy_s(Text, 100, I18N::Game::AllRightsReserved);

    g_RenderText.MeasureText(Text, lstrlen(Text), &Size);
    g_RenderText.RenderText(335, REFERENCE_HEIGHT - Size.cy * REFERENCE_WIDTH / renderWidth - 1,
                            Text);

    swprintf_s(Text, 100, I18N::Game::VerS, m_ExeVersion);

    g_RenderText.MeasureText(Text, lstrlen(Text), &Size);
    g_RenderText.RenderText(0, REFERENCE_HEIGHT - Size.cy * REFERENCE_WIDTH / renderWidth - 1,
                            Text);

    RenderInfomation();

#ifdef ENABLE_EDIT
    RenderDebugWindow();
#endif

    // The login scene skips the full NewUI render.
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_OPTION))
    {
        g_pOption->UpdateMouseEvent();
        g_pOption->UpdateKeyEvent();
        g_pOption->Update();
        g_pOption->Render();
    }

    // Drive the NewUI message box here too (same reason as the option window):
    // the login scene skips the full NewUI update, so a confirmation dialog such
    // as the "Remember Password" prompt would otherwise never update or draw.
    if (!g_MessageBox.IsEmpty())
    {
        g_MessageBox.UpdateMouseEvent();
        g_MessageBox.UpdateKeyEvent();
        g_MessageBox.Update();
        g_MessageBox.Render();
    }

    RenderCursor();

    EndBitmap();

    return true;
}
bool SessionLegacyCalls::NewRenderLogInScene()
{
    return sessionKeeper_.Renderer()->NewRenderLogInScene();
} // OMF-01784

/**
 * @brief Sets up OpenGL viewport and clear color for main scene.
 *
 * @param outWidth Output screen width
 * @param outHeight Output screen height
 * @param outByWaterMap Output water map flag (0=normal, 1=hellas water, 2=water terrain)
 * @param cameraPos Camera position for frustum
 */
void SessionRenderUnit::SetupMainSceneViewport(int &outWidth, int &outHeight, BYTE &outByWaterMap,
                                               vec_t *cameraPos)
{
    outByWaterMap = 0;

    outHeight = REFERENCE_HEIGHT;

    outWidth = GetScreenWidth();

    // NOTE: Clear color is set by SceneManager::SetWorldClearColor() before this function is called
    // All background colors are now centralized in SceneManager.cpp

    BeginOpengl(0, REFERENCE_HEIGHT - outHeight, outWidth, outHeight);

    TheMapProcess().BeginSceneRender();
}

void SessionLegacyCalls::SetupMainSceneViewport(int &outWidth, int &outHeight, BYTE &outByWaterMap,
                                                vec_t *cameraPos)
{
    sessionKeeper_.Renderer()->SetupMainSceneViewport(outWidth, outHeight, outByWaterMap,
                                                      cameraPos);
}

/**
 * @brief Renders all 3D game entities (terrain, objects, characters, effects).
 *
 * @param byWaterMap Water map mode flag (passed by reference, may be modified)
 * @param width Screen width for water terrain rendering
 * @param height Screen height for water terrain rendering
 */
void SessionRenderUnit::RenderGameWorld(BYTE &byWaterMap, int width, int height)
{
    bool renderTerrain = true;
    bool renderStatic = true;
    bool renderEffects = true;
    bool renderDroppedItems = true;
    bool renderWeatherEffects = true;
    const int viewportY = REFERENCE_HEIGHT - height;
    const auto beginWorldPass = [this, width, height, viewportY](RenderTapePass pass) {
        return SwitchWorldRenderTapePass(pass, 0, viewportY, width, height);
    };

    if (!RenderMainTerrainAndObjects(width, height))
        return;

    if (renderEffects)
    {
        if (!beginWorldPass(RenderTapePass::Effects))
            return;
        RenderEffectShadows();
        RenderBoids();
    }

    if (!beginWorldPass(RenderTapePass::Characters))
    {
        return;
    }
    {
        FRAME_PROFILE(Characters);
        RenderCharactersClient();
    }

    if (EditFlag != EDIT_NONE && renderTerrain)
    {
        if (!beginWorldPass(RenderTapePass::Terrain))
            return;
        FRAME_PROFILE(Terrain);
        RenderTerrain(true);
    }
    if (!beginWorldPass(RenderTapePass::Items))
    {
        return;
    }
    if (renderDroppedItems)
    {
        FRAME_PROFILE(Items);
        RenderItems();
    }

    if (!beginWorldPass(RenderTapePass::Characters))
        return;
    RenderFishs();
    RenderCharacterAttachments();

    if (renderWeatherEffects)
    {
        if (!beginWorldPass(RenderTapePass::Effects))
            return;
        RenderLeaves();
    }

    if (renderEffects)
    {
        if (!beginWorldPass(RenderTapePass::Effects))
            return;
        RenderBoids(true);
    }

    if (renderStatic)
    {
        if (!beginWorldPass(RenderTapePass::Objects))
            return;
        FRAME_PROFILE(Objects);
        RenderObjects_AfterCharacter();
    }

    if (!beginWorldPass(RenderTapePass::Effects))
    {
        return;
    }
    RenderJoints(byWaterMap);

    if (renderEffects)
    {
        FRAME_PROFILE(Effects);
        RenderEffects();
        RenderBlurs();
    }
    if (!beginWorldPass(RenderTapePass::Sprites))
    {
        return;
    }
    BeginSprite();

    if (ShouldRenderLeaves())
    {
        RenderLeaves();
    }

    RenderSprites();
    RenderParticles();

    if (IsWaterTerrain() == false)
    {
        RenderPoints(byWaterMap);
    }

    EndSprite();

    if (!beginWorldPass(RenderTapePass::Effects))
        return;
    RenderAfterEffects();

    if (IsWaterTerrain() == true)
    {
        byWaterMap = 2;

        if (!beginWorldPass(RenderTapePass::Terrain))
            return;
        RenderWaterTerrain();
        if (!beginWorldPass(RenderTapePass::Effects))
            return;
        RenderJoints(byWaterMap);
        RenderEffects(true);
        RenderBlurs();
        if (!beginWorldPass(RenderTapePass::Sprites))
            return;
        BeginSprite();

        if (TheMapProcess().WeatherWaterPass())
            RenderLeaves();

        RenderSprites(byWaterMap);
        RenderParticles(byWaterMap);
        RenderPoints(byWaterMap);

        EndSprite();
    }

    TheMapProcess().EndSceneRender();
}

bool SessionRenderUnit::CanRenderMainScene() const noexcept
{
    return MainSceneReady && !sessionKeeper_.WorldUnit()->BlocksInteraction();
}

bool SessionRenderUnit::RenderMainScene()
{
    sessionKeeper_.SpritesStorage().BeginDraw();
    if (!CanRenderMainScene())
    {
        return false;
    }

    // Per-camera fog default: Orbital uses fog (noticeable at longer view distances),
    // Default camera's fog zone sits at/beyond its far clip and reads as visual noise,
    // so fog is off by default for Default. DevEditor can override either below.
    if (ICamera *active = cameraManager_.GetActiveCamera())
    {
        const char *name = active->GetName();
        if (strcmp(name, "Default") == 0)
            FogEnable = false;
        else if (strcmp(name, "Orbital") == 0)
            FogEnable = true;
    }

    vec3_t cameraPos;
    int width, height;
    BYTE byWaterMap;

    // Determine camera position
    if (sessionKeeper_.TerrainStorage().mainCameraLocked)
    {
        VectorCopy(Hero->Object.StartPosition, cameraPos);
    }
    else
    {
        g_pCatapultWindow->GetCameraPos(cameraPos);

        if (g_Direction.IsDirection(gMapManager.ContextMap()) && g_Direction.m_bDownHero == false)
        {
            g_Direction.GetCameraPosition(cameraPos);
        }
    }

    SetupMainSceneViewport(width, height, byWaterMap, cameraPos);
    RenderGameWorld(byWaterMap, width, height);
    if (!recordingSucceeded_)
    {
        return false;
    }
    EndOpengl();

    RenderMainSceneUI();
    if (!recordingSucceeded_)
    {
        return false;
    }

    return true;
}
bool SessionLegacyCalls::ShouldRenderLeaves()
{
    return sessionKeeper_.Renderer()->ShouldRenderLeaves();
} // OMF-01786
void SessionLegacyCalls::RenderGameWorld(BYTE &byWaterMap, int width, int height)
{
    return sessionKeeper_.Renderer()->RenderGameWorld(byWaterMap, width, height);
} // OMF-01793
bool SessionLegacyCalls::RenderMainScene()
{
    return sessionKeeper_.Renderer()->RenderMainScene();
} // OMF-01795

bool SessionLegacyCalls::OpenSMDFile(wchar_t *fileName, int type, bool flip)
{
    return sessionKeeper_.Renderer()->OpenSMDFile(fileName, type, flip);
}
void SessionLegacyCalls::ParseNodes()
{
    sessionKeeper_.Renderer()->ParseNodes();
}
void SessionLegacyCalls::ParseSkeleton()
{
    sessionKeeper_.Renderer()->ParseSkeleton();
}
void SessionLegacyCalls::ParseTriangles(bool flip)
{
    sessionKeeper_.Renderer()->ParseTriangles(flip);
}
bool SessionLegacyCalls::OpenSMDModel(int id, wchar_t *fileName, int actions, bool flip)
{
    return sessionKeeper_.Renderer()->OpenSMDModel(id, fileName, actions, flip);
}
bool SessionLegacyCalls::OpenSMDAnimation(int id, wchar_t *fileName, bool lockPosition)
{
    return sessionKeeper_.Renderer()->OpenSMDAnimation(id, fileName, lockPosition);
}

/**
 * @brief Sets up viewport and character positioning for character selection scene.
 *
 * @param outWidth Output screen width
 * @param outHeight Output screen height
 */
void SessionRenderUnit::SetupCharacterSceneViewport(int &outWidth, int &outHeight)
{
    vec3_t pos;
    Vector(9758.0f, 18913.0f, 675.0f, pos);

    glColor3f(1.f, 1.f, 1.f);
    outHeight = REFERENCE_HEIGHT;
    outWidth = GetScreenWidth();

    glClearColor(0.f, 0.f, 0.f, 1.f);
    BeginOpengl(0, 0, outWidth, outHeight);
}

void SessionLegacyCalls::SetupCharacterSceneViewport(int &outWidth, int &outHeight)
{
    sessionKeeper_.Renderer()->SetupCharacterSceneViewport(outWidth, outHeight);
}

bool SessionRenderUnit::CanRenderCharacterScene() const noexcept
{
    return InitCharacterScene && CurrentProtocolState >= RECEIVE_CHARACTERS_LIST;
}

bool SessionRenderUnit::NewRenderCharacterScene()
{
    sessionKeeper_.SpritesStorage().BeginDraw();
    if (!CanRenderCharacterScene())
    {
        return false;
    }

    FogEnable = true;

    int width, height;
    SetupCharacterSceneViewport(width, height);

    RenderCharacterScene3D();
    if (!recordingSucceeded_)
    {
        return false;
    }

    RenderCharacterSceneUI();
    if (!recordingSucceeded_)
    {
        return false;
    }

    return true;
}
bool SessionLegacyCalls::NewRenderCharacterScene()
{
    return sessionKeeper_.Renderer()->NewRenderCharacterScene();
} // OMF-01770

void SessionRenderUnit::LoadingScene()
{
    CUIMng &rUIMng = LegacyUiManager();
    if (!rUIMng.m_pLoadingScene)
        return;

    FogEnable = true;
    BeginOpengl();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    EndOpengl();
    if (!BeginRenderTapePass(RenderTapePass::UserInterface))
    {
        return;
    }
    BeginBitmap();

    // The loading artwork is the only frame rendered between two complete
    // legacy scenes. Do not inherit culling, fog, stencil, or color-write state
    // from the character scene through the offscreen session surface.
    glDisable(GL_FOG);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColor4f(1.f, 1.f, 1.f, 1.f);

    rUIMng.m_pLoadingScene->Render();

    EndBitmap();
    sessionKeeper_.Ui()->ReconnectOverlay().Render(sessionKeeper_.Network()->Reconnect());
}
void SessionLegacyCalls::RenderCharacter(const CHARACTER *character, const OBJECT *object,
                                         int select)
{
    sessionKeeper_.Renderer()->RenderCharacter(character, object, select);
}
void SessionLegacyCalls::RenderGuild(const ObjectDrawInput &object, int type, vec3_t position)
{
    sessionKeeper_.Renderer()->RenderGuild(object, type, position);
}
const vec34_t *SessionLegacyCalls::RenderLinkObject(float x, float y, float z,
                                                    const CharacterDrawInput &character,
                                                    const PART_t *part, int type, int level,
                                                    int option1, bool link, bool translate,
                                                    int renderType, bool rightHandItem, int slot)
{
    return sessionKeeper_.Renderer()->RenderLinkObject(x, y, z, character, part, type, level,
                                                       option1, link, translate, renderType,
                                                       rightHandItem, slot);
}
bool SessionLegacyCalls::RenderCharacterBackItem(const CharacterDrawInput &character,
                                                 bool translate)
{
    return sessionKeeper_.Renderer()->RenderCharacterBackItem(character, translate);
}
void SessionLegacyCalls::RenderCharactersClient()
{
    sessionKeeper_.Renderer()->RenderCharactersClient();
}
void SessionLegacyCalls::RenderLight(const ObjectDrawInput &object, int texture, float scale,
                                     int bone, float x, float y, float z)
{
    sessionKeeper_.Renderer()->RenderLight(object, texture, scale, bone, x, y, z);
}
void SessionLegacyCalls::RenderEye(const ObjectDrawInput &object, int left, int right, float size)
{
    sessionKeeper_.Renderer()->RenderEye(object, left, right, size);
}

void SessionLegacyCalls::RenderBrightEffect(BMD *model, int bitmap, int link, float scale,
                                            vec_t *light, OBJECT *object)
{
    sessionKeeper_.Visual()->RenderBrightEffect(model, bitmap, link, scale, light, object);
}

void SessionLegacyCalls::OpenModel(int type, wchar_t *directory, wchar_t *modelFileName, ...)
{
    va_list animationFiles;
    va_start(animationFiles, modelFileName);
    sessionKeeper_.Renderer()->OpenModel(type, directory, modelFileName, animationFiles);
    va_end(animationFiles);
}
void SessionLegacyCalls::OpenModels(int model, wchar_t *fileName, int index)
{
    sessionKeeper_.Renderer()->OpenModels(model, fileName, index);
}
void SessionLegacyCalls::OpenPlayers()
{
    sessionKeeper_.Renderer()->OpenPlayers();
}

void SessionLegacyCalls::OpenPlayerTextures()
{
    sessionKeeper_.Renderer()->OpenPlayerTextures();
}

void SessionLegacyCalls::OpenItems()
{
    sessionKeeper_.Renderer()->OpenItems();
}

void SessionLegacyCalls::OpenItemTextures()
{
    sessionKeeper_.Renderer()->OpenItemTextures();
}

void SessionLegacyCalls::OpenNpc(int type)
{
    sessionKeeper_.Renderer()->OpenNpc(type);
}

bool SessionLegacyCalls::OpenMonsterModel(EMonsterModelType type)
{
    return sessionKeeper_.GameData()->OpenMonsterModel(type);
}

void SessionLegacyCalls::OpenSkills()
{
    sessionKeeper_.Renderer()->OpenSkills();
}

bool SessionLegacyCalls::OpenFont()
{
    return sessionKeeper_.Renderer()->OpenFont();
}

void SessionLegacyCalls::OpenSounds()
{
    sessionKeeper_.Renderer()->OpenSounds();
}

void SessionLegacyCalls::ConfigureCharacterSceneModels()
{
    sessionKeeper_.Renderer()->ConfigureCharacterSceneModels();
}

void SessionLegacyCalls::OpenImages()
{
    sessionKeeper_.Renderer()->OpenImages();
}
void SessionLegacyCalls::ReleaseCharacterSceneData()
{
    sessionKeeper_.Renderer()->ReleaseCharacterSceneData();
}

void SessionLegacyCalls::OpenBasicData()
{
    sessionKeeper_.Renderer()->OpenBasicData();
}

// light map

void SessionLegacyCalls::CalcShadowPosition(vec3_t *position, const vec3_t origin,
                                            const float scaleX, const float scaleY) const
{
    sessionKeeper_.Renderer()->CalcShadowPosition(position, origin, scaleX, scaleY);
}
void SessionLegacyCalls::GetClothShadowPosition(vec3_t *target, const CPhysicsCloth *cloth,
                                                const int index, const vec3_t origin,
                                                const float scaleX, const float scaleY) const
{
    sessionKeeper_.Renderer()->GetClothShadowPosition(target, cloth, index, origin, scaleX, scaleY);
}

//#endif //USE_SHADOWVOLUME

// Frame Statistics Tracker

static constexpr float GRAPH_MAX_MS = 33.3f;        // graph Y-axis scale (30fps)
static constexpr float THRESHOLD_60FPS_MS = 16.67f; // 60 FPS threshold
static constexpr float THRESHOLD_40FPS_MS = 25.0f;  // 40 FPS threshold
static constexpr float DEBUG_TEXT_X = 10.0f;        // debug overlay X position
static constexpr int DEBUG_TEXT_Y_START = 26;       // debug overlay Y start
static constexpr int DEBUG_TEXT_LINE_HEIGHT = 10;   // line spacing
static constexpr float DEBUG_GRAPH_WIDTH = 200.0f;  // frame graph width
static constexpr float DEBUG_GRAPH_HEIGHT = 40.0f;  // frame graph height
static constexpr float DEBUG_GRAPH_Y_OFFSET = 2.0f; // gap between text and graph
static constexpr std::size_t DEBUG_BYTES_PER_KIB = 1024;
static constexpr double DEBUG_BYTES_PER_MIB = 1024.0 * 1024.0;

/**
 * @brief Sets both the OpenGL clear color and the global fog color to the same RGB.
 *
 * Every world uses the fog color as its clear color, so this keeps them in sync
 * and avoids duplicating the two assignments at every call site.
 */
void SessionRenderUnit::SetClearAndFogColor(float r, float g, float b)
{
    glClearColor(r, g, b, 1.f);
    FogColor[0] = r;
    FogColor[1] = g;
    FogColor[2] = b;
    FogColor[3] = 1.f;
}

void SessionLegacyCalls::SetClearAndFogColor(float r, float g, float b)
{
    sessionKeeper_.Renderer()->SetClearAndFogColor(r, g, b);
}

/**
 * @brief Sets the OpenGL clear color based on the current world/map.
 *
 * Different maps have different background colors for visual atmosphere.
 */
void SessionRenderUnit::SetWorldClearColor()
{
    const auto &color = TheMapProcess().Presentation().clearColor;
    SetClearAndFogColor(color[0], color[1], color[2]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void SessionLegacyCalls::SetWorldClearColor()
{
    sessionKeeper_.Renderer()->SetWorldClearColor();
}

/**
 * @brief Renders the appropriate scene based on current SceneFlag.
 *
 * @param hDC Device context for rendering
 * @return true if rendering succeeded, false otherwise
 */
bool SessionRenderUnit::RenderCurrentScene()
{
    bool Success = false;

    if (SceneFlag == LOG_IN_SCENE)
    {
        Success = NewRenderLogInScene();
    }
    else if (SceneFlag == CHARACTER_SCENE)
    {
        Success = NewRenderCharacterScene();
    }
    else if (SceneFlag == MAIN_SCENE)
    {
        Success = RenderMainScene();
    }

    RenderPhysicsCloths();
    return Success;
}

/**
 * @brief Renders a frame time graph using raw OpenGL quads.
 *
 * Draws a bar chart of recent frame times inside BeginBitmap's 2D ortho projection.
 * Coordinates are in virtual 640x480 space, converted to window pixels.
 */
void SessionRenderUnit::RenderFrameGraph(float graphX, float graphY, float graphW, float graphH)
{
    if (s_frameCount < 2)
        return;

    const SessionDisplayRect renderRect = sessionKeeper_.Display()->LocalRect();

    // Convert virtual 640x480 coords to this session's render-pass pixels.
    float gx = graphX * static_cast<float>(renderRect.width) / (float)REFERENCE_WIDTH;
    float gy = graphY * static_cast<float>(renderRect.height) / (float)REFERENCE_HEIGHT;
    float gw = graphW * static_cast<float>(renderRect.width) / (float)REFERENCE_WIDTH;
    float gh = graphH * static_cast<float>(renderRect.height) / (float)REFERENCE_HEIGHT;

    // Flip Y for OpenGL (origin bottom-left)
    float glBottom = static_cast<float>(renderRect.height) - gy - gh;
    float glTop = static_cast<float>(renderRect.height) - gy;

    // Background
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glColor4f(0.0f, 0.0f, 0.0f, 0.5f);
    glBegin(GL_QUADS);
    glVertex2f(gx, glBottom);
    glVertex2f(gx + gw, glBottom);
    glVertex2f(gx + gw, glTop);
    glVertex2f(gx, glTop);
    glEnd();

    // Target line at 16.67ms (60fps)
    float target60 = THRESHOLD_60FPS_MS / GRAPH_MAX_MS;
    float lineY = glBottom + target60 * gh;
    glColor4f(0.3f, 0.8f, 0.3f, 0.5f);
    glBegin(GL_LINES);
    glVertex2f(gx, lineY);
    glVertex2f(gx + gw, lineY);
    glEnd();

    // Frame bars
    constexpr int FrameHistorySize = static_cast<int>(ApplicationFrameStorage::FrameHistorySize);
    float barW = gw / FrameHistorySize;
    int oldest = (s_frameCount < FrameHistorySize) ? 0 : s_frameIndex;

    glBegin(GL_QUADS);
    for (int i = 0; i < s_frameCount; i++)
    {
        int idx = (oldest + i) % FrameHistorySize;
        float ms = s_frameTimesMs[idx];
        float norm = (std::min)(ms / GRAPH_MAX_MS, 1.0f);
        float barH = norm * gh;

        // Color: green < 16.67ms, yellow < 25ms, red >= 25ms
        if (ms < THRESHOLD_60FPS_MS)
            glColor4f(0.2f, 0.9f, 0.2f, 0.8f);
        else if (ms < THRESHOLD_40FPS_MS)
            glColor4f(0.9f, 0.9f, 0.2f, 0.8f);
        else
            glColor4f(0.9f, 0.2f, 0.2f, 0.8f);

        float bx = gx + i * barW;
        glVertex2f(bx, glBottom);
        glVertex2f(bx + barW, glBottom);
        glVertex2f(bx + barW, glBottom + barH);
        glVertex2f(bx, glBottom + barH);
    }
    glEnd();

    glEnable(GL_TEXTURE_2D);
}

/**
 * @brief Renders debug information overlay.
 *
 * Shows compact runtime, render-tape, profiler, and frame graph diagnostics.
 */
void SessionRenderUnit::RenderDebugInfo()
{
    if (!g_bShowDebugInfo)
        return;

    BeginBitmap();

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetBgColor(0, 0, 0, 100);
    g_RenderText.SetTextColor(255, 255, 255, 200);

    int y = RenderRuntimeDebugInfo(DEBUG_TEXT_Y_START);

    // Per-pass frame timing (ms) — accumulated by FRAME_PROFILE scopes around the
    // major render passes in MainScene. Reset just below so next frame starts fresh.
    using FP = FrameProfiler::Pass;
#if 0 // Hidden to keep the compact overlay short; retain for quick diagnostics.
    y = RenderTapeDebugInfo(y);
    y = RenderHotspotProfilerDebugInfo(y);

    // Active camera mode (cycled with F9).
    mu_swprintf(szLine, L"Camera: %hs", CameraModeToString(cameraManager_.GetCurrentMode()));
    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, szLine); y += DEBUG_TEXT_LINE_HEIGHT;

    mu_swprintf(szLine, L"Frame ms  T:%5.2f  O:%5.2f  C:%5.2f  I:%5.2f  E:%5.2f",
             g_frameProfilerAccumulatorMs[static_cast<int>(FP::Terrain)],
             g_frameProfilerAccumulatorMs[static_cast<int>(FP::Objects)],
             g_frameProfilerAccumulatorMs[static_cast<int>(FP::Characters)],
             g_frameProfilerAccumulatorMs[static_cast<int>(FP::Items)],
             g_frameProfilerAccumulatorMs[static_cast<int>(FP::Effects)]);
    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, szLine); y += DEBUG_TEXT_LINE_HEIGHT;
#endif
    for (int index = 0; index < static_cast<int>(FP::Count_); ++index)
        g_frameProfilerAccumulatorMs[index] = 0.0f;

    // Frame time graph below text
    RenderFrameGraph(DEBUG_TEXT_X, (float)y + DEBUG_GRAPH_Y_OFFSET, DEBUG_GRAPH_WIDTH,
                     DEBUG_GRAPH_HEIGHT);

    g_RenderText.SetFont(LegacyFontRole::Normal);
    EndBitmap();
}

int SessionRenderUnit::RenderRuntimeDebugInfo(int y)
{
    wchar_t line[256];
    mu_swprintf(line, L"FPS: %.1f  Avg: %.1f  Max: %.1f  Min: %.1f  Frame: %.2fms",
                static_cast<double>(FPS_AVG), static_cast<double>(s_avgFps),
                static_cast<double>(s_highestFps), static_cast<double>(s_slowestFrameFps), 
                s_avgFps > 0.0f ? 1000.0 / s_avgFps : 0.0);
    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, line);
    y += DEBUG_TEXT_LINE_HEIGHT;

    mu_swprintf(line, L"CPU: %.1f%%  GPU: %.1f%%  Memory: %.1f MB  Vsync: %d",
                sessionKeeper_.DiagnosticsCpuAverage(),
                sessionKeeper_.DiagnosticsGpuUsagePercent(),
                sessionKeeper_.DiagnosticsProcessMemoryBytes() / DEBUG_BYTES_PER_MIB,
                IsVSyncEnabled());
    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, line);
    y += DEBUG_TEXT_LINE_HEIGHT;

    mu_swprintf(line, L"MousePos: %d %d %d", MouseX, MouseY, MouseLButtonPush);
    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, line);
    y += DEBUG_TEXT_LINE_HEIGHT;

    mu_swprintf(line, L"Camera3D: %.1f %.1f:%.1f:%.1f", g_Camera.FOV,
        g_Camera.Angle[0], g_Camera.Angle[1], g_Camera.Angle[2]);
    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, line);
    y += DEBUG_TEXT_LINE_HEIGHT;

    mu_swprintf(line, L"OS: %ls Backend: %ls", Core::Platform::GetOSVersionString().c_str(), SdlGpuDriverName());
    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, line);
    y += DEBUG_TEXT_LINE_HEIGHT;

    constexpr const char* kBuildType =
#if defined(_DEBUG) || defined(DEBUG)
        "Debug";
#else
        "Release";
#endif
    constexpr const char* kCompiler =
#if defined(__MINGW32__) || defined(__MINGW64__)
        "MinGW";
#elif defined(__clang__)
        "Clang";
#elif defined(_MSC_VER)
        "MSVC";
#elif defined(__GNUC__)
        "GCC";
#else
        "Unknown";
#endif
    constexpr const char* kArch =
#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
        "x64";
#else
        "x86";
#endif
    mu_swprintf(line, L"Build: %hs %hs %hs  %hs %hs",
        kBuildType, kCompiler, kArch, __DATE__, __TIME__);
    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, line);
    y += DEBUG_TEXT_LINE_HEIGHT;

#if 0 // Hidden to keep the compact overlay short; retain for quick diagnostics.
    mu_swprintf(line, L"CPU: Session %.1f%%  App ring %.1f%%",
                sessionKeeper_.DiagnosticsSessionCpuPercent(),
                sessionKeeper_.DiagnosticsCpuAverage());

    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, line);
    y += DEBUG_TEXT_LINE_HEIGHT;

    mu_swprintf(line, L"Memory MiB: Session fixed %.2f  App ring fixed %.2f  Total %.1f",
                sessionKeeper_.DiagnosticsSessionOwnedBytes() / DEBUG_BYTES_PER_MIB,
                sessionKeeper_.DiagnosticsAppRingOwnedBytes() / DEBUG_BYTES_PER_MIB,
                sessionKeeper_.DiagnosticsProcessMemoryBytes() / DEBUG_BYTES_PER_MIB);
    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, line);
    y += DEBUG_TEXT_LINE_HEIGHT;

    SessionModelPool &modelPool = sessionKeeper_.ModelPoolObject();
    mu_swprintf(line, L"Models: %zu loaded", modelPool.LoadedModelCount());
    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, line);
    y += DEBUG_TEXT_LINE_HEIGHT;

    mu_swprintf(line, L"GPU: %.1f%%  Vsync: %d", sessionKeeper_.DiagnosticsGpuUsagePercent(),
                IsVSyncEnabled());
    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, line);
    y += DEBUG_TEXT_LINE_HEIGHT;
#endif

    return y;
}

int SessionRenderUnit::RenderTapeDebugInfo(int y)
{
    wchar_t line[160];
    const unsigned long long sessionId =
        static_cast<unsigned long long>(sessionKeeper_.Id().RawValue());
    if (renderTapeDebugStats_.frameSequence == 0)
    {
        mu_swprintf(line, L"Render: SDL GPU  Session:%llu  Last tape: waiting", sessionId);
        g_RenderText.RenderText((int)DEBUG_TEXT_X, y, line);
        return RenderTapeFailureDebugInfo(y + DEBUG_TEXT_LINE_HEIGHT);
    }

    const RenderTapeDebugStats &tape = renderTapeDebugStats_;
    mu_swprintf(line, L"Render: SDL GPU  Session:%llu/%llu  Surface:%llu %ux%u", sessionId,
                static_cast<unsigned long long>(tape.generation),
                static_cast<unsigned long long>(tape.surfaceGeneration), tape.viewportWidth,
                tape.viewportHeight);
    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, line);
    y += DEBUG_TEXT_LINE_HEIGHT;

    mu_swprintf(line, L"Last tape F:%llu  E:%llu C:%llu D:%llu R:%llu",
                static_cast<unsigned long long>(tape.frameSequence),
                static_cast<unsigned long long>(tape.entries),
                static_cast<unsigned long long>(tape.clears),
                static_cast<unsigned long long>(tape.draws),
                static_cast<unsigned long long>(tape.ownerRequests));
    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, line);
    y += DEBUG_TEXT_LINE_HEIGHT;

    mu_swprintf(line,
                L"Tape data V:%llu I:%llu Q:%llu K:%llu A:%llu G:%llu/%lluKiB Payload:%lluKiB",
                static_cast<unsigned long long>(tape.vertices),
                static_cast<unsigned long long>(tape.indices),
                static_cast<unsigned long long>(tape.quadInstances),
                static_cast<unsigned long long>(tape.constants),
                static_cast<unsigned long long>(tape.assets),
                static_cast<unsigned long long>(tape.geometryAssets),
                static_cast<unsigned long long>(tape.geometryBytes / DEBUG_BYTES_PER_KIB),
                static_cast<unsigned long long>(tape.payloadBytes / DEBUG_BYTES_PER_KIB));
    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, line);
    y += DEBUG_TEXT_LINE_HEIGHT;

    const std::uint64_t presentedFrame =
        lastCompletedTarget_.has_value() ? lastCompletedTarget_->frameSequence : 0;
    mu_swprintf(line, L"Tape alloc:%lluKiB  Pending:%llu  Presented:%llu",
                static_cast<unsigned long long>(tape.storageBytes / DEBUG_BYTES_PER_KIB),
                static_cast<unsigned long long>(pendingTargetCopies_.size()),
                static_cast<unsigned long long>(presentedFrame));
    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, line);
    y += DEBUG_TEXT_LINE_HEIGHT;

    return RenderTapeFailureDebugInfo(y);
}

int SessionRenderUnit::RenderTapeFailureDebugInfo(int y)
{
    if (lastRenderTapeFailureDiagnostics_.failure == RenderTapeFailure::None)
    {
        return y;
    }

    const RenderTapeFailureDiagnostics &failure = lastRenderTapeFailureDiagnostics_;
    wchar_t line[128];
    mu_swprintf(line, L"Tape failure:%u Pass:%u Row:%u Order:%llu",
                static_cast<unsigned int>(failure.failure), static_cast<unsigned int>(failure.pass),
                failure.sourceInventoryRowId, static_cast<unsigned long long>(failure.stableOrder));
    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, line);
    return y + DEBUG_TEXT_LINE_HEIGHT;
}

int SessionRenderUnit::RenderHotspotProfilerDebugInfo(int y)
{
#if !defined(_DEBUG) || !defined(_WIN32)
    return y;
#else
    const HotspotProfilerState state = sessionKeeper_.DiagnosticsHotspotProfileState();
    const wchar_t *text = nullptr;
    switch (state)
    {
    case HotspotProfilerState::Idle:
        return y;
    case HotspotProfilerState::Recording:
        text = L"Hot profile: RECORDING - F12 stops";
        break;
    case HotspotProfilerState::Stopping:
        text = L"Hot profile: saving...";
        break;
    case HotspotProfilerState::Saved:
        text = L"Hot profile: saved in game/profiles - F12 starts";
        break;
    case HotspotProfilerState::Failed:
        text = L"Hot profile: failed - check MuError.log";
        break;
    }
    g_RenderText.RenderText((int)DEBUG_TEXT_X, y, text);
    return y + DEBUG_TEXT_LINE_HEIGHT;
#endif
}

/**
 * @brief Renders a simple FPS counter overlay showing only current FPS.
 */
void SessionRenderUnit::RenderFpsCounter()
{
    if (!g_bShowFpsCounter)
        return;

    BeginBitmap();

    wchar_t szLine[64];
    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetBgColor(0, 0, 0, 100);
    g_RenderText.SetTextColor(255, 255, 255, 200);

    mu_swprintf(szLine, L"FPS: %.1f", static_cast<double>(FPS_AVG));
    g_RenderText.RenderText((int)DEBUG_TEXT_X, DEBUG_TEXT_Y_START, szLine);

    g_RenderText.SetFont(LegacyFontRole::Normal);
    EndBitmap();
}

void SessionRenderUnit::RenderMainSceneOverlays(bool sceneRendered)
{
    RenderDebugInfo();
    RenderFpsCounter();
    sessionKeeper_.Ui()->ReconnectOverlay().Render(sessionKeeper_.Network()->Reconnect());

    if (!sceneRendered)
    {
        return;
    }
}

void SessionLegacyCalls::RenderFrameGraph(float graphX, float graphY, float graphW, float graphH)
{
    sessionKeeper_.Renderer()->RenderFrameGraph(graphX, graphY, graphW, graphH);
}
void SessionLegacyCalls::RenderDebugInfo()
{
    sessionKeeper_.Renderer()->RenderDebugInfo();
}
void SessionLegacyCalls::RenderFpsCounter()
{
    sessionKeeper_.Renderer()->RenderFpsCounter();
}

void SessionRenderUnit::MainScene()
{
    mainSceneFramePresentable_ = false;
    if (Destroy)
    {
        return;
    }

    SetWorldClearColor();

    bool Success = false;

    try
    {
        Success = RenderCurrentScene();
        RenderMainSceneOverlays(Success);
        mainSceneFramePresentable_ = Success;
    }
    catch (const std::exception &e)
    {
        // Log exception in MainScene
        char errorMsg[256];
        sprintf_s(errorMsg, sizeof(errorMsg), "Exception in MainScene: %s", e.what());
        OutputDebugStringA(errorMsg);
    }
}

void SessionLegacyCalls::MainScene()
{
    sessionKeeper_.Renderer()->MainScene();
}

bool SessionRenderUnit::RenderScene()
{
    try
    {
        g_Luminosity = sinf(WorldTime * 0.004f) * 0.15f + 0.6f;
        bool framePresentable = false;
        switch (SceneFlag)
        {
        case WEBZEN_SCENE:
            WebzenScene();
            framePresentable = preparedWebzenScene_;
            break;
        case LOADING_SCENE:
            LoadingScene();
            framePresentable = LegacyUiManager().m_pLoadingScene != nullptr;
            break;
        case LOG_IN_SCENE:
        case CHARACTER_SCENE:
        case MAIN_SCENE:
            MainScene();
            framePresentable = mainSceneFramePresentable_;
            break;
        }

        return framePresentable;
    }
    catch (const std::exception &e)
    {
        // Log exception in RenderScene
        char errorMsg[256];
        sprintf_s(errorMsg, sizeof(errorMsg), "Exception in RenderScene: %s", e.what());
        OutputDebugStringA(errorMsg);
    }
    return false;
}
bool SessionLegacyCalls::RenderCurrentScene()
{
    return sessionKeeper_.Renderer()->RenderCurrentScene();
} // OMF-01832
//  CSParts.cpp

void SessionLegacyCalls::RenderParts(const CHARACTER *character)
{
    sessionKeeper_.Renderer()->RenderParts(character);
}

bool SessionRenderUnit::EnsureVisualDataOnOwner(bool keepTitleScene)
try
{
    if (visualDataReady_)
    {
        return true;
    }

    SessionGameDataUnit *gameData = sessionKeeper_.GameData();
    if (gameData == nullptr)
    {
        return false;
    }

    if (keepTitleScene)
    {
        return InitializeTitlePresentationOnOwner();
    }

    if (!InitializeTitlePresentationOnOwner())
    {
        return false;
    }
    OpenBasicData();
    InitializeMainSceneInterfaceOnOwner();
    startupLoadStep_ = StartupLoadStepCount;
    visualDataReady_ = true;
    ReleaseTitleSceneOnOwner();
    return true;
}

catch (...)
{
    return sessionKeeper_.WorldUnit()->FinishLoad(false);
}

bool SessionRenderUnit::PrepareWebzenSceneOnOwner()
{
    return EnsureVisualDataOnOwner(true);
}

bool SessionRenderUnit::InitializeTitlePresentationOnOwner()
{
    if (preparedWebzenScene_)
    {
        return true;
    }
    if (!OpenFont())
    {
        return false;
    }

    ClearInput();
    LoadCommonTitleBitmaps();
    LoadBackgroundTheme(SelectBackgroundTheme());
    LegacyUiManager().CreateTitleSceneUI();
    FogEnable = true;
    EnableAlphaTest();

    startupLoadStep_ = 0;
    firstTitleFrameSequence_ = 0;
    firstTitleFrameGeneration_.reset();
    firstTitleFrameSurfaceGeneration_ = 0;
    finalTitleFrameSequence_ = 0;
    finalTitleFrameGeneration_.reset();
    finalTitleFrameSurfaceGeneration_ = 0;
    preparedWebzenScene_ = true;
    return true;
}

bool SessionRenderUnit::AdvanceWebzenLoadingOnOwner()
{
    if (!preparedWebzenScene_)
    {
        return false;
    }
    if (visualDataReady_ || !HasPresentedFirstTitleFrame())
    {
        return true;
    }

    if (startupLoadStep_ < BasicDataLoadStepCount)
    {
        OpenBasicDataStep(startupLoadStep_ + 1);
        ++startupLoadStep_;
        return true;
    }

    InitializeMainSceneInterfaceOnOwner();
    startupLoadStep_ = StartupLoadStepCount;
    visualDataReady_ = true;
    return true;
}

bool SessionRenderUnit::HasPresentedTitleFrame(std::uint64_t frameSequence,
                                               SessionGeneration generation,
                                               std::uint64_t surfaceGeneration) const noexcept
{
    if (frameSequence == 0 || surfaceGeneration == 0 || !lastCompletedTarget_.has_value())
    {
        return false;
    }

    const SessionReplayCompletion &completed = *lastCompletedTarget_;
    return completed.succeeded && completed.generation == generation &&
           completed.surfaceGeneration == surfaceGeneration &&
           completed.frameSequence >= frameSequence;
}

bool SessionRenderUnit::HasPresentedFirstTitleFrame() const noexcept
{
    if (!firstTitleFrameGeneration_.has_value())
    {
        return false;
    }
    return HasPresentedTitleFrame(firstTitleFrameSequence_, *firstTitleFrameGeneration_,
                                  firstTitleFrameSurfaceGeneration_);
}

bool SessionRenderUnit::HasPresentedFinalTitleFrame() const noexcept
{
    if (!finalTitleFrameGeneration_.has_value())
    {
        return false;
    }
    return HasPresentedTitleFrame(finalTitleFrameSequence_, *finalTitleFrameGeneration_,
                                  finalTitleFrameSurfaceGeneration_);
}

void SessionRenderUnit::InitializeMainSceneInterfaceOnOwner()
{
    g_MessageBox.Show(true);
    (void)g_pNewUISystem->LoadMainSceneInterface();
}

void SessionRenderUnit::WebzenScene()
{
    if (!preparedWebzenScene_)
    {
        return;
    }

    if (preparedGeneration_.has_value())
    {
        if (firstTitleFrameSequence_ == 0 || !firstTitleFrameGeneration_.has_value() ||
            *firstTitleFrameGeneration_ != *preparedGeneration_ ||
            firstTitleFrameSurfaceGeneration_ != preparedSurfaceGeneration_)
        {
            firstTitleFrameSequence_ = preparedFrameSequence_;
            firstTitleFrameGeneration_ = *preparedGeneration_;
            firstTitleFrameSurfaceGeneration_ = preparedSurfaceGeneration_;
        }

        if (startupLoadStep_ == StartupLoadStepCount &&
            (finalTitleFrameSequence_ == 0 ||
             !finalTitleFrameGeneration_.has_value() ||
             *finalTitleFrameGeneration_ != *preparedGeneration_ ||
             finalTitleFrameSurfaceGeneration_ != preparedSurfaceGeneration_))
        {
            finalTitleFrameSequence_ = preparedFrameSequence_;
            finalTitleFrameGeneration_ = *preparedGeneration_;
            finalTitleFrameSurfaceGeneration_ = preparedSurfaceGeneration_;
        }
    }

    LegacyUiManager().RenderTitleSceneUI(nullptr, startupLoadStep_, StartupLoadStepCount);
}

void SessionRenderUnit::ReleaseTitleSceneOnOwner() noexcept
{
    if (!preparedWebzenScene_)
    {
        return;
    }

    LegacyUiManager().ReleaseTitleSceneUI();
    UnloadTitleBitmaps();

    g_ErrorReport.Write(L"> Loading ok.\r\n");

    preparedWebzenScene_ = false;
}

SplashSceneDetail::BackgroundTheme SessionLegacyCalls::SelectBackgroundTheme()
{
    return sessionKeeper_.Renderer()->SelectBackgroundTheme();
}
void SessionLegacyCalls::LoadBackgroundTheme(SplashSceneDetail::BackgroundTheme theme)
{
    sessionKeeper_.Renderer()->LoadBackgroundTheme(theme);
}
void SessionLegacyCalls::LoadCommonTitleBitmaps()
{
    sessionKeeper_.Renderer()->LoadCommonTitleBitmaps();
}
void SessionLegacyCalls::UnloadTitleBitmaps()
{
    sessionKeeper_.Renderer()->UnloadTitleBitmaps();
}
void SessionLegacyCalls::WebzenScene()
{
    sessionKeeper_.Renderer()->WebzenScene();
}

void SessionLegacyCalls::RenderAurora(int type, int renderType, float x, float y, float sizeX,
                                      float sizeY, const vec3_t light)
{
    sessionKeeper_.Gameplay()->TheMapProcess().BattleCastle().RenderAurora(type, renderType, x, y,
                                                                           sizeX, sizeY, light);
}

bool SessionLegacyCalls::CreateWaterTerrain(int mapIndex)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Hellas().CreateWaterTerrain(mapIndex);
}

bool SessionLegacyCalls::IsWaterTerrain()
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Hellas().IsWaterTerrain();
}

void SessionLegacyCalls::MoveWaterTerrain()
{
    sessionKeeper_.Gameplay()->TheMapProcess().Hellas().MoveWaterTerrain();
}
bool SessionLegacyCalls::RenderWaterTerrain()
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Hellas().RenderWaterTerrain();
}
void SessionLegacyCalls::DeleteWaterTerrain()
{
    sessionKeeper_.Gameplay()->TheMapProcess().Hellas().DeleteWaterTerrain();
}
float SessionLegacyCalls::GetWaterTerrain(float x, float y)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Hellas().GetWaterTerrain(x, y);
}
void SessionLegacyCalls::RenderWaterTerrain(int texture, float x, float y, float sizeX, float sizeY,
                                            const vec3_t light, float rotation, float alpha,
                                            float height)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Hellas().RenderWaterTerrain(
        texture, x, y, sizeX, sizeY, light, rotation, alpha, height);
}

int SessionLegacyCalls::RenderHellasItemInfo(ITEM *item, int textNumber)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Hellas().RenderHellasItemInfo(item,
                                                                                    textNumber);
}

void SessionLegacyCalls::RenderObjectDescription()
{
    sessionKeeper_.Gameplay()->TheMapProcess().Hellas().RenderObjectDescription();
}

void SessionLegacyCalls::AddObjectDescription(wchar_t *text, vec3_t position)
{
    sessionKeeper_.Gameplay()->TheMapProcess().Hellas().AddObjectDescription(text, position);
}

void SessionLegacyCalls::GetItemName(int type, int level, wchar_t *text) const
{
    sessionKeeper_.GameData()->GetItemName(type, level, text);
}
void SessionLegacyCalls::RenderItemInfo(int x, int y, ITEM *item, bool sell, int inventoryType,
                                        bool itemTextListBoxUse) const
{
    sessionKeeper_.Renderer()->RenderItemInfo(x, y, item, sell, inventoryType, itemTextListBoxUse);
}
void SessionLegacyCalls::GetSpecialOptionText(int type, wchar_t *text, WORD option, BYTE value,
                                              int mana)
{
    sessionKeeper_.Renderer()->GetSpecialOptionText(type, text, option, value, mana);
}
void SessionLegacyCalls::RenderTipTextList(const int x, const int y, int textCount, int tab,
                                           int sort, int renderPoint, BOOL useBackground)
{
    sessionKeeper_.Renderer()->RenderTipTextList(x, y, textCount, tab, sort, renderPoint,
                                                 useBackground);
}

void SessionLegacyCalls::RenderHelpLine(int columnType, const wchar_t *printStyle, int &tabSpace,
                                        const wchar_t *gapText, int positionY, int type)
{
    sessionKeeper_.Renderer()->RenderHelpLine(columnType, printStyle, tabSpace, gapText, positionY,
                                              type);
}
void SessionLegacyCalls::RenderRepairInfo(int x, int y, ITEM *item, bool sell) const
{
    sessionKeeper_.Renderer()->RenderRepairInfo(x, y, item, sell);
}

void SessionLegacyCalls::RenderGroundItemLabelTexture(OBJECT *object,
                                                      const GroundItemLabelCacheEntry &cacheEntry)
{
    sessionKeeper_.Renderer()->RenderGroundItemLabelTexture(object, cacheEntry);
}
bool SessionLegacyCalls::RenderGroundItemLabelCached(OBJECT *object, ITEM *item)
{
    return sessionKeeper_.Renderer()->RenderGroundItemLabelCached(object, item);
}
void SessionLegacyCalls::RenderObjectScreen(int type, int itemLevel, int excellentFlags,
                                            int ancientDiscriminator, vec3_t target, int select,
                                            bool pickUp)
{
    sessionKeeper_.Renderer()->RenderObjectScreen(type, itemLevel, excellentFlags,
                                                  ancientDiscriminator, target, select, pickUp);
}
void SessionLegacyCalls::RenderItem3D(float x, float y, float width, float height, int type,
                                      int level, int excellentFlags, int ancientDiscriminator,
                                      bool pickUp, float presentationScale, bool useSlotTrs)
{
    sessionKeeper_.Renderer()->RenderItem3D(x, y, width, height, type, level, excellentFlags,
                                            ancientDiscriminator, pickUp, presentationScale,
                                            useSlotTrs);
}
void SessionLegacyCalls::RenderEqiupmentBox()
{
    sessionKeeper_.Renderer()->RenderEqiupmentBox();
}
void SessionLegacyCalls::RenderGuildList(int startX, int startY)
{
    sessionKeeper_.Renderer()->RenderGuildList(startX, startY);
}
void SessionLegacyCalls::RenderServerDivision()
{
    sessionKeeper_.Renderer()->RenderServerDivision();
}
void SessionLegacyCalls::RenderInventoryInterface(int startX, int startY, int flag)
{
    sessionKeeper_.Renderer()->RenderInventoryInterface(startX, startY, flag);
}
void SessionLegacyCalls::RenderGuildColor(float x, float y, int sizeX, int sizeY, int index)
{
    sessionKeeper_.Renderer()->RenderGuildColor(x, y, sizeX, sizeY, index);
}
void SessionLegacyCalls::RenderItemName(int index, OBJECT *object, ITEM *item, bool sort)
{
    sessionKeeper_.Renderer()->RenderItemName(index, object, item, sort);
}
void SessionLegacyCalls::RenderHelpCategory(int columnType, int positionX, int positionY)
{
    sessionKeeper_.Renderer()->RenderHelpCategory(columnType, positionX, positionY);
}
void SessionLegacyCalls::RenderEqiupmentPart3D(int index, float x, float y, float width,
                                               float height)
{
    sessionKeeper_.Renderer()->RenderEqiupmentPart3D(index, x, y, width, height);
}
void SessionLegacyCalls::RenderEqiupment3D()
{
    sessionKeeper_.Renderer()->RenderEqiupment3D();
}

void SessionLegacyCalls::BodyLight(const ObjectDrawInput &object, BMD *model)
{
    sessionKeeper_.Visual()->BodyLight(object, model);
}

void SessionLegacyCalls::Draw_RenderObject(const ObjectDrawInput &object, bool translate,
                                           int select, int extraMonster)
{
    sessionKeeper_.Renderer()->Draw_RenderObject(object, translate, select, extraMonster);
}
const vec34_t *SessionLegacyCalls::RenderObject(const ObjectDrawInput &object, bool translate,
                                                int select, int extraMonster) const
{
    return sessionKeeper_.Renderer()->RenderObject(object, translate, select, extraMonster);
}
const vec34_t *SessionLegacyCalls::RenderObject_AfterImage(const ObjectDrawInput &object,
                                                           bool translate, int select,
                                                           int extraMonster)
{
    return sessionKeeper_.Renderer()->RenderObject_AfterImage(object, translate, select,
                                                              extraMonster);
}
void SessionLegacyCalls::RenderCharacter_AfterImage(const CharacterDrawInput &character,
                                                    const PART_t *part, bool translate, int select,
                                                    float animationInterval1,
                                                    float animationInterval2)
{
    sessionKeeper_.Renderer()->RenderCharacter_AfterImage(character, part, translate, select,
                                                          animationInterval1, animationInterval2);
}
void SessionLegacyCalls::RenderObject_AfterCharacter(const ObjectDrawInput &object, bool translate,
                                                     int select, int extraMonster)
{
    sessionKeeper_.Renderer()->RenderObject_AfterCharacter(object, translate, select, extraMonster);
}
void SessionLegacyCalls::Draw_RenderObject_AfterCharacter(const ObjectDrawInput &object,
                                                          bool translate, int select,
                                                          int extraMonster)
{
    sessionKeeper_.Renderer()->Draw_RenderObject_AfterCharacter(object, translate, select,
                                                                extraMonster);
}
void SessionLegacyCalls::RenderObjects_AfterCharacter()
{
    sessionKeeper_.Renderer()->RenderObjects_AfterCharacter();
}

void SessionLegacyCalls::SaveTrapObjects(wchar_t *fileName)
{
    sessionKeeper_.Renderer()->SaveTrapObjects(fileName);
}
void SessionLegacyCalls::RenderCloudLowLevel(int index, int type)
{
    sessionKeeper_.Renderer()->RenderCloudLowLevel(index, type);
}
void SessionLegacyCalls::RenderItems()
{
    sessionKeeper_.Renderer()->RenderItems();
}
void SessionLegacyCalls::NextGradeObjectRender(const CharacterDrawInput &character)
{
    sessionKeeper_.Renderer()->NextGradeObjectRender(character);
}
void SessionLegacyCalls::RenderBoundingBox(const OBJECT *object)
{
#ifdef CSK_DEBUG_RENDER_BOUNDINGBOX
    sessionKeeper_.Renderer()->RenderBoundingBox(object);
#else
    (void)object;
#endif
}
void SessionLegacyCalls::RenderPartObjectEffect(const ObjectDrawInput &object, int type,
                                                const vec3_t light, float alpha, int level,
                                                int excellentFlags, int ancientDiscriminator,
                                                int select, int renderType)
{
    sessionKeeper_.Renderer()->RenderPartObjectEffect(object, type, light, alpha, level,
                                                      excellentFlags, ancientDiscriminator, select,
                                                      renderType);
}
void SessionLegacyCalls::RenderPartObjectBody(BMD *model, const ObjectDrawInput &object, int type,
                                              float alpha, int renderType)
{
    sessionKeeper_.Renderer()->RenderPartObjectBody(model, object, type, alpha, renderType);
}
void SessionLegacyCalls::RenderPartObjectBodyColor(BMD *model, const ObjectDrawInput &object,
                                                   int type, float alpha, int renderType,
                                                   float bright, int texture, int monsterIndex)
{
    sessionKeeper_.Renderer()->RenderPartObjectBodyColor(model, object, type, alpha, renderType,
                                                         bright, texture, monsterIndex);
}
void SessionLegacyCalls::RenderPartObjectBodyColor2(BMD *model, const ObjectDrawInput &object,
                                                    int type, float alpha, int renderType,
                                                    float bright, int texture)
{
    sessionKeeper_.Renderer()->RenderPartObjectBodyColor2(model, object, type, alpha, renderType,
                                                          bright, texture);
}
void SessionLegacyCalls::PartObjectColor(int type, float alpha, float bright, vec3_t light,
                                         bool extraMonster)
{
    sessionKeeper_.Visual()->PartObjectColor(type, alpha, bright, light, extraMonster);
}
void SessionLegacyCalls::PartObjectColor2(int type, float alpha, float bright, vec3_t light,
                                          bool extraMonster)
{
    sessionKeeper_.Renderer()->PartObjectColor2(type, alpha, bright, light, extraMonster);
}
void SessionLegacyCalls::RenderPartObjectEdgeLight(BMD *model, const ObjectDrawInput &object,
                                                   int flags, bool translate, float scale)
{
    sessionKeeper_.Renderer()->RenderPartObjectEdgeLight(model, object, flags, translate, scale);
}
void SessionLegacyCalls::RenderPartObjectEdge(BMD *model, const ObjectDrawInput &object, int flags,
                                              bool translate, float scale)
{
    sessionKeeper_.Renderer()->RenderPartObjectEdge(model, object, flags, translate, scale);
}
void SessionLegacyCalls::RenderPartObjectEdge2(BMD *model, const ObjectDrawInput &object, int flags,
                                               bool translate, float scale, OBB_t *obb)
{
    sessionKeeper_.Renderer()->RenderPartObjectEdge2(model, object, flags, translate, scale, obb);
}
void SessionLegacyCalls::RenderPartObject(const ObjectDrawInput &object, int type,
                                          const PART_t *data, const vec3_t light, float alpha,
                                          int level, int excellentFlags, int ancientDiscriminator,
                                          bool globalTransform, bool hideSkin, bool translate,
                                          int select, int renderType)
{
    sessionKeeper_.Renderer()->RenderPartObject(
        object, type, data, light, alpha, level, excellentFlags, ancientDiscriminator,
        globalTransform, hideSkin, translate, select, renderType);
}

void SessionLegacyCalls::RenderZen(int itemIndex, ITEM_t *item, vec_t *light)
{
    sessionKeeper_.Renderer()->RenderZen(itemIndex, item, light);
}

void SessionLegacyCalls::SortInBlockByType()
{
    sessionKeeper_.Renderer()->SortInBlockByType();
}

bool SessionLegacyCalls::SaveObjects(wchar_t *fileName, int mapNumber)
{
    return sessionKeeper_.Renderer()->SaveObjects(fileName, mapNumber);
}

void SessionLegacyCalls::RenderObjects()
{
    return sessionKeeper_.Renderer()->RenderObjects();
} // OMF-00606
void SessionLegacyCalls::InstallWorldPlacements(const WorldFileData &data)
{
    sessionKeeper_.Renderer()->InstallWorldPlacements(data);
}

void SessionLegacyCalls::RenderPet(CHARACTER *character)
{
    sessionKeeper_.Renderer()->RenderPet(character);
}

// Compute FrustrumBound{Min,Max}{X,Y} from the current FrustrumX/Y/Count hull.
// Snaps to a TERRAIN_ITERATION_TILE grid and clamps to valid terrain range.

// Build a CW convex hull from points projected to tile-space XY, storing into
// FrustrumX/Y/Count, then compute iteration bounds.

// Expand CW convex hull outward by `offset` tiles.
// Compensates for TestFrustrum2D only checking tile centers — tiles at the hull
// boundary whose centers are just outside would otherwise be culled even though
// part of the tile is visible. Expanding by ~1 tile ensures full coverage.

void SessionLegacyCalls::CreateFrustrum2D(vec_t *position)
{
    sessionKeeper_.Visual()->CreateFrustrum2D(position);
}
void SessionLegacyCalls::CreateFrustrum(float xAspect, float yAspect, vec_t *position)
{
    sessionKeeper_.Visual()->CreateFrustrum(xAspect, yAspect, position);
}

/**
 * @brief Renders a wireframe sphere for debugging culling volumes
 * @param center Center position of the sphere in world space
 * @param radius Radius of the sphere
 * @param r Red color component (0-1)
 * @param g Green color component (0-1)
 * @param b Blue color component (0-1)
 */

void SessionLegacyCalls::CacheActiveFrustum()
{
    sessionKeeper_.Visual()->CacheActiveFrustum();
}

extern void RenderCharactersClient();

void SessionLegacyCalls::RenderTerrain(bool editFlag)
{
    sessionKeeper_.Renderer()->RenderTerrain(editFlag);
}
void SessionLegacyCalls::RenderTerrainBitmapTile(float x, float y, float lodFactor, int lod,
                                                 vec3_t coordinates[4], bool lightEnable,
                                                 float alpha, float height)
{
    sessionKeeper_.Renderer()->RenderTerrainBitmapTile(x, y, lodFactor, lod, coordinates,
                                                       lightEnable, alpha, height);
}
void SessionLegacyCalls::RenderTerrainBitmap(int texture, int mapX, int mapY, float rotation)
{
    sessionKeeper_.Renderer()->RenderTerrainBitmap(texture, mapX, mapY, rotation);
}
void SessionLegacyCalls::RenderTerrainAlphaBitmap(int texture, float x, float y, float sizeX,
                                                  float sizeY, const vec3_t light, float rotation,
                                                  float alpha, float height)
{
    sessionKeeper_.Renderer()->RenderTerrainAlphaBitmap(texture, x, y, sizeX, sizeY, light,
                                                        rotation, alpha, height);
}

void SessionLegacyCalls::Vertex0()
{
    sessionKeeper_.Renderer()->Vertex0();
}
void SessionLegacyCalls::Vertex1()
{
    sessionKeeper_.Renderer()->Vertex1();
}
void SessionLegacyCalls::Vertex2()
{
    sessionKeeper_.Renderer()->Vertex2();
}
void SessionLegacyCalls::Vertex3()
{
    sessionKeeper_.Renderer()->Vertex3();
}
void SessionLegacyCalls::Vertex01()
{
    sessionKeeper_.Renderer()->Vertex01();
}
void SessionLegacyCalls::Vertex12()
{
    sessionKeeper_.Renderer()->Vertex12();
}
void SessionLegacyCalls::Vertex23()
{
    sessionKeeper_.Renderer()->Vertex23();
}
void SessionLegacyCalls::Vertex30()
{
    sessionKeeper_.Renderer()->Vertex30();
}
void SessionLegacyCalls::Vertex02()
{
    sessionKeeper_.Renderer()->Vertex02();
}
void SessionLegacyCalls::VertexAlpha0()
{
    sessionKeeper_.Renderer()->VertexAlpha0();
}
void SessionLegacyCalls::VertexAlpha1()
{
    sessionKeeper_.Renderer()->VertexAlpha1();
}
void SessionLegacyCalls::VertexAlpha2()
{
    sessionKeeper_.Renderer()->VertexAlpha2();
}
void SessionLegacyCalls::VertexAlpha3()
{
    sessionKeeper_.Renderer()->VertexAlpha3();
}
void SessionLegacyCalls::VertexAlpha01()
{
    sessionKeeper_.Renderer()->VertexAlpha01();
}
void SessionLegacyCalls::VertexAlpha12()
{
    sessionKeeper_.Renderer()->VertexAlpha12();
}
void SessionLegacyCalls::VertexAlpha23()
{
    sessionKeeper_.Renderer()->VertexAlpha23();
}
void SessionLegacyCalls::VertexAlpha30()
{
    sessionKeeper_.Renderer()->VertexAlpha30();
}
void SessionLegacyCalls::VertexAlpha02()
{
    sessionKeeper_.Renderer()->VertexAlpha02();
}
void SessionLegacyCalls::VertexBlend0()
{
    sessionKeeper_.Renderer()->VertexBlend0();
}
void SessionLegacyCalls::VertexBlend1()
{
    sessionKeeper_.Renderer()->VertexBlend1();
}
void SessionLegacyCalls::VertexBlend2()
{
    sessionKeeper_.Renderer()->VertexBlend2();
}
void SessionLegacyCalls::VertexBlend3()
{
    sessionKeeper_.Renderer()->VertexBlend3();
}

void SessionLegacyCalls::BuildHull2DAndBounds(const float *pointsX, const float *pointsY,
                                              int pointCount)
{
    sessionKeeper_.Visual()->BuildHull2DAndBounds(pointsX, pointsY, pointCount);
}
void SessionLegacyCalls::ExpandHullOutward(float offset)
{
    sessionKeeper_.Visual()->ExpandHullOutward(offset);
}
void SessionLegacyCalls::ResetFrustrumBoundsFullTerrain()
{
    sessionKeeper_.Visual()->ResetFrustrumBoundsFullTerrain();
}
bool SessionLegacyCalls::TestFrustrum2D(float x, float y, float range)
{
    return sessionKeeper_.Visual()->TestFrustrum2D(x, y, range);
}
bool SessionLegacyCalls::TestFrustrum(const vec3_t position, float range)
{
    return sessionKeeper_.Visual()->TestFrustrum(position, range);
}

void SessionLegacyCalls::FaceTexture(int texture, float x, float y, bool water, bool scale)
{
    sessionKeeper_.Renderer()->FaceTexture(texture, x, y, water, scale);
}

void SessionLegacyCalls::RenderFace(int texture, int mapX, int mapY)
{
    sessionKeeper_.Renderer()->RenderFace(texture, mapX, mapY);
}
void SessionLegacyCalls::RenderFace_After(int texture, int mapX, int mapY)
{
    sessionKeeper_.Renderer()->RenderFace_After(texture, mapX, mapY);
}
void SessionLegacyCalls::RenderFaceAlpha(int texture, int mapX, int mapY)
{
    sessionKeeper_.Renderer()->RenderFaceAlpha(texture, mapX, mapY);
}
void SessionLegacyCalls::RenderFaceBlend(int texture, int mapX, int mapY)
{
    sessionKeeper_.Renderer()->RenderFaceBlend(texture, mapX, mapY);
}
void SessionLegacyCalls::RenderTerrainFace(float x, float y, int mapX, int mapY, float lodFactor)
{
    sessionKeeper_.Renderer()->RenderTerrainFace(x, y, mapX, mapY, lodFactor);
}
void SessionLegacyCalls::RenderTerrainFace_After(float x, float y, int mapX, int mapY,
                                                 float lodFactor)
{
    sessionKeeper_.Renderer()->RenderTerrainFace_After(x, y, mapX, mapY, lodFactor);
}
bool SessionLegacyCalls::RenderTerrainTile(float x, float y, int mapX, int mapY, float lodFactor,
                                           int lod, bool flag)
{
    return sessionKeeper_.Renderer()->RenderTerrainTile(x, y, mapX, mapY, lodFactor, lod, flag);
}
void SessionLegacyCalls::RenderTerrainTile_After(float x, float y, int mapX, int mapY,
                                                 float lodFactor, int lod, bool flag)
{
    sessionKeeper_.Renderer()->RenderTerrainTile_After(x, y, mapX, mapY, lodFactor, lod, flag);
}
void SessionLegacyCalls::RenderTerrainBlock(float x, float y, int mapX, int mapY, bool editFlag)
{
    sessionKeeper_.Renderer()->RenderTerrainBlock(x, y, mapX, mapY, editFlag);
}
void SessionLegacyCalls::RenderTerrainFrustrum(bool editFlag)
{
    sessionKeeper_.Renderer()->RenderTerrainFrustrum(editFlag);
}
void SessionLegacyCalls::RenderTerrainBlock_After(float x, float y, int mapX, int mapY,
                                                  bool editFlag)
{
    sessionKeeper_.Renderer()->RenderTerrainBlock_After(x, y, mapX, mapY, editFlag);
}
void SessionLegacyCalls::RenderTerrainFrustrum_After(bool editFlag)
{
    sessionKeeper_.Renderer()->RenderTerrainFrustrum_After(editFlag);
}
void SessionLegacyCalls::RenderTerrain_After(bool editFlag)
{
    sessionKeeper_.Renderer()->RenderTerrain_After(editFlag);
}
void SessionLegacyCalls::RenderSun()
{
    sessionKeeper_.Renderer()->RenderSun();
}
void SessionLegacyCalls::RenderSky()
{
    sessionKeeper_.Renderer()->RenderSky();
}

void SessionLegacyCalls::RenderBoids(bool afterCharacter)
{
    sessionKeeper_.Renderer()->RenderBoids(afterCharacter);
}
void SessionLegacyCalls::RenderFishs()
{
    sessionKeeper_.Renderer()->RenderFishs();
}
bool SessionLegacyCalls::RenderMount(const ObjectDrawInput &object, bool forceRender)
{
    return sessionKeeper_.Renderer()->RenderMount(object, forceRender);
}
void SessionLegacyCalls::RenderCharacterAttachments()
{
    sessionKeeper_.Renderer()->RenderCharacterAttachments();
}

void SessionRenderUnit::RenderInputText(int x, int y, int Index, int Gold)
{
    if (g_iChatInputType == 1)
    {
        return;
    }
    else if (g_iChatInputType == 0)
    {
        g_RenderText.SetTextColor(255, 230, 210, 255);
        g_RenderText.SetBgColor(0);

        SIZE *Size;
        wchar_t Text[256];
        if (InputTextHide[Index] == 1)
        {
            int iTextSize = 0;
            for (unsigned int i = 0; i < wcslen(InputText[Index]); i++)
            {
                Text[i] = '*';
                iTextSize = i;
            }
            Text[iTextSize] = 0;
        }
        else if (InputTextHide[Index] == 2)
        {
            int iTextSize = 0;
            for (unsigned int i = 0; i < 7; i++)
            {
                Text[i] = InputText[Index][i];
                iTextSize = i;
            }
            for (unsigned int i = 7; i < wcslen(InputText[Index]); i++)
            {
                Text[i] = '*';
                iTextSize = i;
            }
            Text[iTextSize] = 0;
        }
        else
        {
            wcscpy(Text, InputText[Index]);
        }
        SIZE TextSize;
        g_RenderText.RenderText(x, y, Text, InputTextWidth, 0, RT3_SORT_LEFT, &TextSize);
        Size = &TextSize;

        if (Index == InputIndex && (InputFrame++) % 2 == 0)
        {
            EnableAlphaTest();
            if (wcslen(InputTextIME[Index]) > 0)
            {
                if (InputTextHide[Index] == 1)
                    g_RenderText.RenderText(x + Size->cx, y, L"**");
                else
                    g_RenderText.RenderText(x + Size->cx, y, InputTextIME[Index]);
            }
            else
                g_RenderText.RenderText(x + Size->cx, y, L"_");
        }
    }
}

void SessionRenderUnit::RenderTipText(int sx, int sy, const wchar_t *Text)
{
    SIZE TextSize = {0, 0};
    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.MeasureText(Text, lstrlen(Text), &TextSize);

    const SessionDisplayRect renderRect = sessionKeeper_.Display()->LocalRect();
    const float screenRateX =
        static_cast<float>(renderRect.width) / static_cast<float>(REFERENCE_WIDTH);
    const float screenRateY =
        static_cast<float>(renderRect.height) / static_cast<float>(REFERENCE_HEIGHT);
    int BackupAlphaBlendType = AlphaBlendType;
    EnableAlphaTest();
    glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
    RenderColor((float)sx - 2, (float)sy - 3, (float)TextSize.cx / screenRateX + 4, (float)1); // ?
    RenderColor((float)sx - 2, (float)sy - 3, (float)1, (float)TextSize.cy / screenRateY + 4); // ?
    RenderColor((float)sx - 2 + TextSize.cx / screenRateX + 3, (float)sy - 3, (float)1,
                (float)TextSize.cy / screenRateY + 4);
    RenderColor((float)sx - 2, (float)sy - 3 + TextSize.cy / screenRateY + 3,
                (float)TextSize.cx / screenRateX + 4, (float)1);

    glColor4f(0.0f, 0.0f, 0.0f, 0.8f);
    RenderColor((float)sx - 1, (float)sy - 2, (float)TextSize.cx / screenRateX + 2,
                (float)TextSize.cy / screenRateY + 2);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetBgColor(0);
    g_RenderText.RenderText(sx, sy, Text);
    switch (BackupAlphaBlendType)
    {
    case 0:
        DisableAlphaBlend();
        break;
    case 1:
        EnableLightMap();
        break;
    case 2:
        EnableAlphaTest();
        break;
    case 3:
        EnableAlphaBlend();
        break;
    case 4:
        EnableAlphaBlendMinus();
        break;
    case 5:
        EnableAlphaBlend2();
        break;
    default:
        DisableAlphaBlend();
        break;
    }
}

void SessionLegacyCalls::RenderTipText(int x, int y, const wchar_t *text)
{
    sessionKeeper_.Renderer()->RenderTipText(x, y, text);
}

int SessionRenderUnit::RenderDebugText(int y)
{
    wchar_t Text[100];
    int Width = 16;
    for (int i = 0; i < std::min<int>(DebugTextCount, 10); i++)
    {
        int Type = 0;
        int Count = 0;
        int x = 0;
        bool Hex = true;
        int SizeByte = 1;
        for (int j = 0; j < DebugTextLength[i]; j++)
        {
            glColor3f(0.6f, 0.6f, 0.6f);
            if (j == 0)
            {
                if (DebugText[i][j] == 0xc2)
                    SizeByte = 2;
            }
            if (j == 2)
            {
                if (SizeByte == 1)
                {
                    Type = DebugText[i][j];
                    glColor3f(1.f, 1.f, 1.f);
                    if (DebugText[i][j] == 0x00)
                    {
                        x = Width * 4;
                    }
                }
            }
            if (j == 3)
            {
                if (SizeByte == 2)
                {
                    Type = DebugText[i][j];
                    glColor3f(1.f, 1.f, 1.f);
                }
            }

            SIZE TextSize;
            if (Hex)
            {
                mu_swprintf(Text, L"%0.2x", DebugText[i][j]);
                g_RenderText.RenderText(x, y, Text, 0, 0, RT3_SORT_CENTER, &TextSize);
            }
            else
            {
                mu_swprintf(Text, L"%c", DebugText[i][j]);
                g_RenderText.RenderText(x, y, Text, 0, 0, RT3_SORT_CENTER, &TextSize);
            }
            if (Hex)
            {
                x += Width;
            }
            else
            {
                x += TextSize.cx;
            }
            Count++;
        }
        y += 12;
    }
    return y;
}

int SessionLegacyCalls::RenderDebugText(int y)
{
    return sessionKeeper_.Renderer()->RenderDebugText(y);
}

//bool IsWebzenCharacter()
//{
//    const std::wstring character_name = std::wstring(Hero->ID);
//    return character_name.find(L"webzen") >= 0;
//}

// While the hero slides to a server-set position (the basic weapon skills reposition the
// hero on every cast), MoveHero would freeze all input until the slide ended, which capped
// those skills' auto-attack cadence below the player's attack speed (issue #350). Keep the
// auto-attack re-cast running through the slide; the slide still animates smoothly and only
// manual move/click input stays suppressed. Returns true when the hero is mid-slide, in
// which case MoveHero should stop after this.

void SessionRenderUnit::RenderBar(float x, float y, float Width, float Height, float Bar,
                                  bool Disabled, bool clipping)
{
    if (clipping)
    {
        if (x < 0)
            x = 0;
        if (y < 0)
            y = 0;
        if (x + Width + 4 > REFERENCE_WIDTH)
            x = REFERENCE_WIDTH - (Width + 1 + 4);
        if (y + Height + 4 > REFERENCE_HEIGHT - 47)
            y = REFERENCE_HEIGHT - 47 - (Height + 1 + 4);
    }

    EnableAlphaTest();
    glColor4f(0.f, 0.f, 0.f, 0.5f);
    RenderColor(x + 1, y + 1, Width + 4, Height + 4);

    EnableAlphaBlend();
    if (Disabled)
        glColor3f(0.2f, 0.0f, 0.0f);
    else
        glColor3f(0.f, 0.2f, 0.2f);
    RenderColor(x, y, Width + 4, Height + 4);
    if (Disabled)
        glColor3f(50.f / 255.f, 10 / 255.f, 0.f);
    else
        glColor3f(0.f / 255.f, 50 / 255.f, 50.f / 255.f);
    RenderColor(x + 2, y + 2, Width, Height);
    if (Disabled)
        glColor3f(200.f / 255.f, 50 / 255.f, 0.f);
    else
        glColor3f(0.f / 255.f, 200 / 255.f, 50.f / 255.f);
    RenderColor(x + 2, y + 2, Bar, Height);

    DisableAlphaBlend();
}

void SessionRenderUnit::RenderSwichState()
{
    wchar_t Buff[300];

    if (Switch_Info == NULL)
        return;

    if (!GameplayInteractionDetail::InsideCastleSwitchArea(*Hero))
        return;

    for (int i = 0; i < 2; i++)
    {
        if (Switch_Info[i].m_bySwitchState > 0)
        {
            mu_swprintf(Buff, L"%ls%d / %ls / %ls", I18N::Game::CrownSwitch, i + 1,
                        Switch_Info[i].m_szGuildName, Switch_Info[i].m_szUserName);
            g_RenderText.SetFont(LegacyFontRole::Normal);
            g_RenderText.SetTextColor(255, 255, 255, 255);
            g_RenderText.SetBgColor(0);
            g_RenderText.RenderText(0, REFERENCE_HEIGHT - 85 + (i * 15), Buff, REFERENCE_WIDTH, 0,
                                    RT3_SORT_CENTER);
        }
    }
}

void SessionRenderUnit::RenderInterface(bool Render)
{
    g_RenderText.SetTextColor(255, 255, 255, 255);

    RenderOutSides();
    RenderPartyHP();

    RenderSwichState();
    TheMapProcess().RenderMapInterface();

    g_pUIMapName.Render(); // rozy
}

void SessionRenderUnit::RenderOutSides()
{
    TheMapProcess().RenderAtmosphere();
    glColor3f(1.f, 1.f, 1.f);
}

void SessionRenderUnit::RenderTournamentInterface()
{
    int Width = 300, Height = 2 * 5 + 6 * 30;
    int WindowX = (REFERENCE_WIDTH - Width) / 2;
    int WindowY = 120 + 0;
    float x = 0.0f, y = 0.0f;
    wchar_t t_Str[20];
    wcscpy(t_Str, L"");

    if (g_wtMatchTimeLeft.m_Time)
    {
        int t_valueSec = g_wtMatchTimeLeft.m_Time % 60;
        int t_valueMin = g_wtMatchTimeLeft.m_Time / 60;
        if (t_valueMin <= 10)
        {
            g_RenderText.SetFont(LegacyFontRole::Large);
            g_RenderText.SetTextColor(255, 10, 10, 255);
            g_RenderText.SetBgColor(0);

            if (g_wtMatchTimeLeft.m_Type == 3)
            {
                g_RenderText.SetTextColor(255, 255, 10, 255);
                mu_swprintf(t_Str, I18N::Game::ItWillStartAfterDSeconds, t_valueSec);
            }
            else
            {
                if (g_wtMatchTimeLeft.m_Time < 60)
                {
                    g_RenderText.SetTextColor(255, 255, 10, 255);
                }
                if (t_valueSec < 10)
                {
                    mu_swprintf(t_Str, I18N::Game::RemainingHoursD0D, t_valueMin, t_valueSec);
                }
                else
                {
                    mu_swprintf(t_Str, I18N::Game::RemainingSecondsDD, t_valueMin, t_valueSec);
                }
            }
            x += (float)GetScreenWidth() / 2;
            y += 350;
            g_RenderText.RenderText((int)x, (int)y, t_Str, 0, 0, RT3_WRITE_CENTER);
            x++;
            y++;

            g_RenderText.SetTextColor(0xffffffff);
            g_RenderText.RenderText((int)x, (int)y, t_Str, 0, 0, RT3_WRITE_CENTER);

            g_RenderText.SetFont(LegacyFontRole::Normal);
            g_RenderText.SetTextColor(255, 255, 255, 255);
        }
    }

    if (!wcscmp(g_wtMatchResult.m_MatchTeamName1, L""))
    {
        return;
    }

    Width = 300;
    Height = 2 * 5 + 5 * 40;
    WindowX = (REFERENCE_WIDTH - Width) / 2;
    WindowY = 120 + 0;
    int yPos = WindowY;
    RenderBitmap(BITMAP_INTERFACE + 22, (float)WindowX, (float)yPos, (float)Width, (float)5, 0.f,
                 0.f, Width / 512.f, 5.f / 8.f);
    yPos += 5;

    for (int i = 0; i < 5; ++i)
    {
        RenderBitmap(BITMAP_INTERFACE + 21, WindowX, (float)yPos, Width, 40.f, 0.f, 0.0f,
                     213.f / 256.f, 40.f / 64.f);
        yPos += 40.f;
    }
    RenderBitmap(BITMAP_INTERFACE + 22, (float)WindowX, (float)yPos, (float)Width, (float)5, 0.f,
                 0.f, Width / 512.f, 5.f / 8.f);

    EnableAlphaBlend();
    glColor4f(1.f, 1.f, 1.f, 1.f);
    g_RenderText.SetFont(LegacyFontRole::Large);
    g_RenderText.SetTextColor(200, 240, 255, 255);
    mu_swprintf(t_Str, I18N::Game::TournamentResult);
    g_RenderText.RenderText(WindowX + Width / 2 - 50, WindowY + 20, t_Str);
    g_RenderText.SetTextColor(255, 255, 255, 255);

    mu_swprintf(t_Str, I18N::Game::VS);
    g_RenderText.SetTextColor(255, 255, 10, 255);
    g_RenderText.RenderText(WindowX + Width / 2 - 13, WindowY + 50, t_Str);
    g_RenderText.SetTextColor(255, 255, 255, 255);

    float t_temp = 0.0f;
    mu_swprintf(t_Str, L"%ls", g_wtMatchResult.m_MatchTeamName1);
    t_temp = (MAX_USERNAME_SIZE - wcslen(t_Str)) * 5;
    g_RenderText.RenderText(WindowX + 10 + t_temp, WindowY + 50, t_Str);
    mu_swprintf(t_Str, L"%ls", g_wtMatchResult.m_MatchTeamName2);
    t_temp = (MAX_USERNAME_SIZE - wcslen(t_Str)) * 5;
    g_RenderText.RenderText(WindowX + Width - 120 + t_temp, WindowY + 50, t_Str);

    mu_swprintf(t_Str, L"(%d)", g_wtMatchResult.m_Score1);
    g_RenderText.RenderText(WindowX + 45, WindowY + 75, t_Str);
    mu_swprintf(t_Str, L"(%d)", g_wtMatchResult.m_Score2);
    g_RenderText.RenderText(WindowX + Width - 85, WindowY + 75, t_Str);

    if (g_wtMatchResult.m_Score1 == g_wtMatchResult.m_Score2)
    {
        g_RenderText.SetFont(LegacyFontRole::Large);
        g_RenderText.SetTextColor(255, 255, 10, 255);
        mu_swprintf(t_Str, I18N::Game::Tie);
        g_RenderText.RenderText(WindowX + Width / 2 - 35, WindowY + 115, t_Str);
        g_RenderText.SetFont(LegacyFontRole::Normal);
        g_RenderText.SetTextColor(255, 255, 255, 255);
    }
    else if (g_wtMatchResult.m_Score1 > g_wtMatchResult.m_Score2)
    {
        g_RenderText.SetFont(LegacyFontRole::Large);
        g_RenderText.SetTextColor(255, 255, 10, 10);
        mu_swprintf(t_Str, I18N::Game::Win);
        g_RenderText.RenderText(WindowX + 47, WindowY + 115, t_Str);
        g_RenderText.SetTextColor(255, 10, 10, 255);
        mu_swprintf(t_Str, I18N::Game::Lose);
        g_RenderText.RenderText(WindowX + Width - 82, WindowY + 115, t_Str);
        g_RenderText.SetFont(LegacyFontRole::Normal);
    }
    else
    {
        g_RenderText.SetFont(LegacyFontRole::Large);
        g_RenderText.SetTextColor(255, 255, 10, 10);
        mu_swprintf(t_Str, I18N::Game::Lose);
        g_RenderText.RenderText(WindowX + 47, WindowY + 115, t_Str);
        g_RenderText.SetTextColor(255, 10, 10, 255);
        mu_swprintf(t_Str, I18N::Game::Win);
        g_RenderText.RenderText(WindowX + Width - 82, WindowY + 115, t_Str);
        g_RenderText.SetFont(LegacyFontRole::Normal);
    }
    g_RenderText.SetFont(LegacyFontRole::Normal);

    Width = 70;
    Height = 20;
    x = (REFERENCE_WIDTH - Width) / 2;
    y = (REFERENCE_HEIGHT - Height) / 2 + 50;
    if (MouseX >= x && MouseX < x + Width && MouseY >= y && MouseY < y + Height)
    {
        RenderBitmap(BITMAP_INTERFACE + 12, (float)x, (float)y, (float)Width, (float)Height, 0.f,
                     0.f, Width / 128.f, Height / 32.f);
    }
    else
    {
        RenderBitmap(BITMAP_INTERFACE + 11, (float)x, (float)y, (float)Width, (float)Height, 0.f,
                     0.f, Width / 128.f, Height / 32.f);
    }

    glColor3f(1.f, 1.f, 1.f);
    DisableAlphaBlend();
}

void SessionRenderUnit::RenderPartyHP()
{
    if (PartyNumber <= 0)
        return;

    float Width = 38.f;
    wchar_t Text[100];

    for (int j = 0; j < PartyNumber; ++j)
    {
        PARTY_t *p = &Party[j];

        if (p->index <= -1)
            continue;

        CHARACTER *c = &CharactersClient[p->index];
        OBJECT *o = &c->Object;
        vec3_t Position;
        int ScreenX, ScreenY;

        Vector(o->Position[0], o->Position[1], o->Position[2] + o->BoundingBoxMax[2] + 100.f,
               Position);
        cameraProjection_.WorldToScreen(g_Camera, Position, &ScreenX, &ScreenY);
        ScreenX -= (int)(Width / 2);

        if ((MouseX >= ScreenX && MouseX < ScreenX + Width && MouseY >= ScreenY - 2 &&
             MouseY < ScreenY + 6))
        {
            mu_swprintf(Text, L"HP : %d0%%", p->stepHP);
            g_RenderText.SetTextColor(255, 230, 210, 255);
            g_RenderText.RenderText(ScreenX, ScreenY - 6, Text);
        }

        EnableAlphaTest();
        glColor4f(0.f, 0.f, 0.f, 0.5f);
        RenderColor((float)(ScreenX + 1), (float)(ScreenY + 1), Width + 4.f, 5.f);

        EnableAlphaBlend();
        glColor3f(0.2f, 0.0f, 0.0f);
        RenderColor((float)ScreenX, (float)ScreenY, Width + 4.f, 5.f);

        glColor3f(50.f / 255.f, 10 / 255.f, 0.f);
        RenderColor((float)(ScreenX + 2), (float)(ScreenY + 2), Width, 1.f);

        int stepHP = std::min<int>(10, p->stepHP);

        glColor3f(250.f / 255.f, 10 / 255.f, 0.f);
        for (int k = 0; k < stepHP; ++k)
        {
            RenderColor((float)(ScreenX + 2 + (k * 4)), (float)(ScreenY + 2), 3.f, 2.f);
        }
        DisableAlphaBlend();
    }
    DisableAlphaBlend();
    glColor3f(1.f, 1.f, 1.f);
}

void SessionRenderUnit::RenderTimes()
{
    const uint64_t currentTickCount = GetTickCount64();
    if (LastMacroTime > currentTickCount - GameplayInteractionDetail::MacroCooldownMs)
    {
        constexpr float width = 50;
        constexpr float height = 2;
        constexpr int y = REFERENCE_HEIGHT - 48 - 40;
        const float x = (static_cast<float>(GetScreenWidth()) - width) / 2.0f;

        const uint64_t remainingMacroCooldownTime =
            GameplayInteractionDetail::MacroCooldownMs - (currentTickCount - LastMacroTime);
        const float progressValue = static_cast<float>(remainingMacroCooldownTime) /
                                    GameplayInteractionDetail::MacroCooldownMs * width;

        EnableAlphaTest();
        g_RenderText.RenderText(static_cast<int>(x), y, L"Macro Time");
        RenderBar(x, y + 12, width, height, (float)progressValue);
    }

    glColor3f(1.f, 1.f, 1.f);

    gameplay_.RenderTime();
}

void SessionRenderUnit::RenderCursor()
{
    if (!g_bRenderGameCursor)
        return;

    EnableAlphaTest();
    glColor3f(1.f, 1.f, 1.f);

    float u = 0.f;
    float v = 0.f;
    int Frame = (int)(WorldTime * 0.01f) % 6;
    if (Frame == 1 || Frame == 3 || Frame == 5)
        u = 0.5f;
    if (Frame == 2 || Frame == 3 || Frame == 4)
        v = 0.5f;
    if (g_iKeyPadEnable || ErrorMessage)
    {
        RenderBitmap(BITMAP_CURSOR, (float)MouseX - 2.f, (float)MouseY - 2.f, 24.f, 24.f);
    }
    else if (SelectedItem != -1)
    {
        RenderBitmap(BITMAP_CURSOR + 3, (float)MouseX - 2.f, (float)MouseY - 2.f, 24.f, 24.f);
    }
    else if (SelectedNpc != -1)
    {
        if (Is_Kanturu2nd())
        {
            RenderBitmap(BITMAP_CURSOR2, (float)MouseX - 2.f, (float)MouseY - 2.f, 24.f, 24.f);
        }
        else
        {
            RenderBitmap(BITMAP_CURSOR + 4, (float)MouseX - 2.f, (float)MouseY - 2.f, 24.f, 24.f, u,
                         v, 0.5f, 0.5f);
        }
    }
    else if (SelectedOperate != -1)
    {
        if ((gMapManager.ContextMap() == WD_0LORENCIA &&
             Operates[SelectedOperate].Owner->Type == MODEL_POSE_BOX) ||
            (gMapManager.ContextMap() == WD_1DUNGEON &&
             Operates[SelectedOperate].Owner->Type == 60) ||
            (gMapManager.ContextMap() == WD_2DEVIAS &&
             Operates[SelectedOperate].Owner->Type == 91) ||
            (gMapManager.ContextMap() == WD_3NORIA && Operates[SelectedOperate].Owner->Type == 38))
            RenderBitmap(BITMAP_CURSOR + 6, (float)MouseX - 2.f, (float)MouseY - 2.f, 24.f, 24.f);
        else
            RenderBitmap(BITMAP_CURSOR + 7, (float)MouseX - 2.f, (float)MouseY - 2.f, 24.f, 24.f);
    }
    else if ((!Hero->SafeZone /*||EnableEdit*/) && SelectedCharacter != -1)
    {
        if (CheckAttack() && !MouseOnWindow)
        {
            if (gMapManager.InBattleCastle())
            {
                RenderBitmap(BITMAP_CURSOR2, (float)MouseX - 2.f, (float)MouseY - 2.f, 24.f, 24.f);
            }
            else
            {
                RenderBitmap(BITMAP_CURSOR + 2, (float)MouseX - 2.f, (float)MouseY - 2.f, 24.f,
                             24.f);
            }
        }
        else
            RenderBitmap(BITMAP_CURSOR, (float)MouseX - 2.f, (float)MouseY - 2.f, 24.f, 24.f);
    }
    else if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_COMMAND))
    {
        if (g_pCommandWindow->GetMouseCursor() == CURSOR_IDSELECT)
        {
            RenderBitmap(BITMAP_INTERFACE_EX + 29, (float)MouseX - 2.f, (float)MouseY - 2.f, 24.f,
                         24.f);
        }
        else if (g_pCommandWindow->GetMouseCursor() == CURSOR_NORMAL)
        {
            RenderBitmap(BITMAP_CURSOR, (float)MouseX - 2.f, (float)MouseY - 2.f, 24.f, 24.f);
        }
        else if (g_pCommandWindow->GetMouseCursor() == CURSOR_PUSH)
        {
            RenderBitmap(BITMAP_CURSOR + 1, (float)MouseX - 2.f, (float)MouseY - 2.f, 24.f, 24.f);
        }
    }
    else if (((g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_INVENTORY) ||
               g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_INVENTORY_EXT)) &&
              g_pMyInventory->GetRepairMode() == SEASON3B::REPAIR_MODE_ON) ||
             (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_NPCSHOP) &&
              g_pNPCShop->GetShopState() == SEASON3B::CNewUINPCShop::SHOP_STATE_REPAIR))
    {
        if (MouseLButton == false)
        {
            RenderBitmap(BITMAP_CURSOR + 5, (float)MouseX - 2.f, (float)MouseY - 2.f, 24.f, 24.f);
        }
        else
        {
            RenderBitmapRotate(BITMAP_CURSOR + 5, (float)MouseX + 5.f, (float)MouseY + 18.f, 24.f,
                               24.f, 45.f);
        }
    }
    else if (RepairEnable == 2)
    {
        if (sin(WorldTime * 0.02f) > 0)
        {
            RenderBitmapRotate(BITMAP_CURSOR + 5, (float)MouseX + 10.f, (float)MouseY + 10.f, 24.f,
                               24.f, 0.f);
        }
        else
        {
            RenderBitmapRotate(BITMAP_CURSOR + 5, (float)MouseX + 5.f, (float)MouseY + 18.f, 24.f,
                               24.f, 45.f);
        }
    }
    else
    {
        if (!MouseLButton)
            RenderBitmap(BITMAP_CURSOR, (float)MouseX - 2.f, (float)MouseY - 2.f, 24.f, 24.f);
        else
        {
            if (DontMove)
                RenderBitmap(BITMAP_CURSOR + 8, (float)MouseX - 2.f, (float)MouseY - 2.f, 24.f,
                             24.f);
            else
                RenderBitmap(BITMAP_CURSOR + 1, (float)MouseX - 2.f, (float)MouseY - 2.f, 24.f,
                             24.f);
        }
    }
}

void SessionLegacyCalls::RenderInterface(bool render)
{
    sessionKeeper_.Renderer()->RenderInterface(render);
}
void SessionLegacyCalls::RenderOutSides()
{
    sessionKeeper_.Renderer()->RenderOutSides();
}
void SessionLegacyCalls::RenderTimes()
{
    sessionKeeper_.Renderer()->RenderTimes();
}
void SessionLegacyCalls::RenderCursor()
{
    sessionKeeper_.Renderer()->RenderCursor();
}
void SessionLegacyCalls::RenderInputText(int x, int y, int index, int gold)
{
    sessionKeeper_.Renderer()->RenderInputText(x, y, index, gold);
}
void SessionLegacyCalls::RenderBar(float x, float y, float width, float height, float bar,
                                   bool disabled, bool clipping)
{
    sessionKeeper_.Renderer()->RenderBar(x, y, width, height, bar, disabled, clipping);
}
void SessionLegacyCalls::RenderSwichState()
{
    sessionKeeper_.Renderer()->RenderSwichState();
}
void SessionLegacyCalls::RenderTournamentInterface()
{
    sessionKeeper_.Renderer()->RenderTournamentInterface();
}
void SessionLegacyCalls::RenderPartyHP()
{
    sessionKeeper_.Renderer()->RenderPartyHP();
}

void SessionRenderUnit::BackSelectModel()
{
    for (int i = 1; i < 20; i++)
    {
        if (SelectModel - i < 0)
            break;
        if (Models[SelectModel - i].NumMeshs > 0)
        {
            SelectModel -= i;
            break;
        }
    }
}

void SessionRenderUnit::ForwardSelectModel()
{
    for (int i = 1; i < 20; i++)
    {
        if (Models[SelectModel + i].NumMeshs > 0)
        {
            SelectModel += i;
            break;
        }
    }
}

void SessionRenderUnit::RenderDebugWindow()
{
    g_RenderText.SetFont(LegacyFontRole::Normal);
    g_RenderText.SetTextColor(255, 255, 255, 255);
    g_RenderText.SetBgColor(0, 0, 0, 255);

    if (timeGetTime() - OldTime >= 1000)
    {
        OldTime = timeGetTime();
        TotalPacketSize = 0;
    }

#ifdef ENABLE_EDIT
    wchar_t Text[256]{};
    if (EditFlag == EDIT_MAPPING)
    {
        int sx = REFERENCE_WIDTH - 30;
        int sy = 0;
        for (int i = 0; i < 14; i++)
        {
            if (i == SelectMapping)
                glColor3f(1.f, 1.f, 1.f);
            else
                glColor3f(0.8f, 0.8f, 0.8f);
            RenderBitmap(BITMAP_MAPTILE + i, (float)(sx), (float)(sy + i * 30), 30.f, 30.f);
        }
        if (CurrentLayer == 0)
            g_RenderText.RenderText(REFERENCE_WIDTH - 100, sy, L"Background");
        else
            g_RenderText.RenderText(REFERENCE_WIDTH - 100, sy, L"Layer1");
        mu_swprintf(Text, L"Brush Size: %d", BrushSize * 2 + 1);
        g_RenderText.RenderText(REFERENCE_WIDTH - 100, sy + 11, Text);
    }
    glColor3f(1.f, 1.f, 1.f);
    if (EditFlag == EDIT_OBJECT)
    {
        g_RenderText.RenderText(REFERENCE_WIDTH - 100, 0, L"Garbage");
        CMultiLanguage::ConvertFromUtf8(Text, Models[SelectModel].Name,
                                        sizeof Models[SelectModel].Name);
        g_RenderText.RenderText(0, 0, Text);
    }
    if (EditFlag == EDIT_MONSTER)
    {
        for (int i = 0; i < EditMonsterNumber; i++)
        {
            if (i == SelectMonster)
                glColor3f(1.f, 0.8f, 0.f);
            else
                glColor3f(1.f, 1.f, 1.f);

            mu_swprintf(Text, L"%2d: %ls", MonsterScript[i].Type, MonsterScript[i].Name);
            g_RenderText.RenderText(REFERENCE_WIDTH - 100, i * 10, Text);
        }
    }
    if (EditFlag == EDIT_LIGHT)
    {
        for (int i = 0; i < 8; i++)
        {
            if (i == SelectColor)
                glColor3f(1.f, 0.8f, 0.f);
            else
                glColor3f(1.f, 1.f, 1.f);

            g_RenderText.RenderText(REFERENCE_WIDTH - 64, i * 10, ColorTable[i]);
        }
    }
#endif //ENABLE_EDIT
}

void MoveCharacter(CHARACTER *c, OBJECT *o);

void SessionLegacyCalls::RenderWheelWeapon(OBJECT *object)
{
    sessionKeeper_.Renderer()->RenderWheelWeapon(object);
}
void SessionLegacyCalls::RenderFuryStrike(OBJECT *object)
{
    sessionKeeper_.Renderer()->RenderFuryStrike(object);
}
void SessionLegacyCalls::RenderSkillSpear(OBJECT *object)
{
    sessionKeeper_.Renderer()->RenderSkillSpear(object);
}
