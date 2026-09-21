#pragma once
#include "app/ApplicationConfigScheduling.h"
#include "render/Assets.h"
#include "session/SessionRuntime.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <source_location>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

struct TerrainLightSnapshot;
class SessionFogPassConstants;

// Four GPU float4 rows. The run supplies the camera; the vertex shader expands
// world-space centers and evaluates rotation. UV/color retain authored values.
struct RenderTapeParticleInstance final
{
    std::array<float, 4> centerHalfWidth;
    std::array<float, 4> halfHeightRotation;
    std::array<float, 4> uv;
    std::array<float, 4> color;
};

static_assert(sizeof(RenderTapeParticleInstance) == 64);

// One authored pair of edges, shared by the segments on either side.
struct RenderTapeTrailSample final
{
    std::array<float, 3> edge0{};
    std::array<float, 3> edge1{};
    std::array<float, 2> padding{};
};
static_assert(sizeof(RenderTapeTrailSample) == 32);
static_assert(offsetof(RenderTapeTrailSample, edge1) == 12);

// Joint: both endpoint U values and the first V edge (the other is 1-V).
// Blur: sample count, fade flag, logical segment index. Full RGBA is retained.
struct RenderTapeTrailInstance final
{
    std::array<float, 3> parameters{};
    std::uint32_t sampleIndex = 0;
    std::array<float, 4> color{};
};
static_assert(sizeof(RenderTapeTrailInstance) == 32);
static_assert(offsetof(RenderTapeTrailInstance, sampleIndex) == 12);
static_assert(offsetof(RenderTapeTrailInstance, color) == 16);

// Six float4 rows in the shared native instance buffer. Unlike quad instances,
// these rows describe a rigid mesh placement and its changing material inputs.
struct RenderTapeRigidInstance final
{
    std::array<float, 4> row0{};
    std::array<float, 4> row1{};
    std::array<float, 4> row2{};
    std::array<float, 4> bodyLight{1.f, 1.f, 1.f, 1.f};
    std::array<float, 4> baseColor{1.f, 1.f, 1.f, 1.f};
    std::array<float, 4> uvOffset{};
};

inline bool IsValid(const RenderTapeRigidInstance &value) noexcept
{
    for (const auto *row : {&value.row0, &value.row1, &value.row2, &value.bodyLight,
                            &value.baseColor, &value.uvOffset})
        for (float component : *row)
            if (!std::isfinite(component))
                return false;
    return true;
}

// Portable value types for the P1.R5.1 session render tape (PLAN_P1R5.1.md
// section 5.1). This header must never include an OpenGL, SDL, Windows,
// render-backend, or native-window header, and must never declare a native
// handle, mutable asset pointer, or pointer-derived identity.

enum class RenderTapeEntryKind : std::uint8_t
{
    Clear,
    Draw,
    OwnerRequest,
};

constexpr bool IsValid(RenderTapeEntryKind value) noexcept
{
    switch (value)
    {
    case RenderTapeEntryKind::Clear:
    case RenderTapeEntryKind::Draw:
    case RenderTapeEntryKind::OwnerRequest:
        return true;
    }
    return false;
}

enum class LegacyPrimitive : std::uint8_t
{
    Points,
    Lines,
    LineStrip,
    LineLoop,
    Triangles,
    TriangleStrip,
    TriangleFan,
    Quads,
    Polygon,
};

constexpr bool IsValid(LegacyPrimitive value) noexcept
{
    switch (value)
    {
    case LegacyPrimitive::Points:
    case LegacyPrimitive::Lines:
    case LegacyPrimitive::LineStrip:
    case LegacyPrimitive::LineLoop:
    case LegacyPrimitive::Triangles:
    case LegacyPrimitive::TriangleStrip:
    case LegacyPrimitive::TriangleFan:
    case LegacyPrimitive::Quads:
    case LegacyPrimitive::Polygon:
        return true;
    }
    return false;
}

enum class RenderIndexTopology : std::uint8_t
{
    Points,
    Lines,
    Triangles,
};

constexpr bool IsValid(RenderIndexTopology value) noexcept
{
    switch (value)
    {
    case RenderIndexTopology::Points:
    case RenderIndexTopology::Lines:
    case RenderIndexTopology::Triangles:
        return true;
    }
    return false;
}

enum class RenderPositionSpace : std::uint8_t
{
    Object,
    Clip,
};

constexpr bool IsValid(RenderPositionSpace value) noexcept
{
    switch (value)
    {
    case RenderPositionSpace::Object:
    case RenderPositionSpace::Clip:
        return true;
    }
    return false;
}

enum class RenderGeometryMode : std::uint8_t
{
    Vertices,
    QuadInstances,
    SpriteInstances,
    Grass,
    Terrain,
    RigidInstances,
    ParticleInstances,
    TrailInstances,
    BlurInstances,
};

constexpr bool IsValid(RenderGeometryMode value) noexcept
{
    switch (value)
    {
    case RenderGeometryMode::Vertices:
    case RenderGeometryMode::QuadInstances:
    case RenderGeometryMode::SpriteInstances:
    case RenderGeometryMode::Grass:
    case RenderGeometryMode::Terrain:
    case RenderGeometryMode::RigidInstances:
    case RenderGeometryMode::ParticleInstances:
    case RenderGeometryMode::TrailInstances:
    case RenderGeometryMode::BlurInstances:
        return true;
    }
    return false;
}

constexpr bool UsesQuadInstanceStorage(RenderGeometryMode value) noexcept
{
    return value == RenderGeometryMode::QuadInstances ||
           value == RenderGeometryMode::SpriteInstances;
}

constexpr bool UsesTrailInstanceStorage(RenderGeometryMode value) noexcept
{
    return value == RenderGeometryMode::TrailInstances ||
           value == RenderGeometryMode::BlurInstances;
}

enum class LegacyMatrixMode : std::uint8_t
{
    ModelView,
    Projection,
    Texture,
};

constexpr bool IsValid(LegacyMatrixMode value) noexcept
{
    switch (value)
    {
    case LegacyMatrixMode::ModelView:
    case LegacyMatrixMode::Projection:
    case LegacyMatrixMode::Texture:
        return true;
    }
    return false;
}

enum class RenderOwnerRequestKind : std::uint8_t
{
    UploadLogicalAssetRgba8,
    CopyTargetToLogicalTexture,
    DownloadTargetRgba8,
};

constexpr bool IsValid(RenderOwnerRequestKind value) noexcept
{
    switch (value)
    {
    case RenderOwnerRequestKind::UploadLogicalAssetRgba8:
    case RenderOwnerRequestKind::CopyTargetToLogicalTexture:
    case RenderOwnerRequestKind::DownloadTargetRgba8:
        return true;
    }
    return false;
}

enum class RenderAssetRetention : std::uint8_t
{
    FrameOnly,
    Catalog,
};

constexpr bool IsValid(RenderAssetRetention value) noexcept
{
    switch (value)
    {
    case RenderAssetRetention::FrameOnly:
    case RenderAssetRetention::Catalog:
        return true;
    }
    return false;
}

enum class RenderTapeFailure : std::uint8_t
{
    None,
    InvalidIdentity,
    InvalidArgument,
    NonFiniteValue,
    InvalidOperation,
    UnsupportedSemantic,
    UnbalancedPrimitive,
    UnbalancedMatrixStack,
    UnbalancedAttributeStack,
    EntryCapacity,
    VertexCapacity,
    IndexCapacity,
    ConstantsCapacity,
    BonePaletteCapacity,
    QuadInstanceCapacity,
    LogicalAssetCapacity,
    LogicalGeometryCapacity,
    OwnerRequestCapacity,
    PayloadCapacity,
    RigidInstanceCapacity,
    ParticleInstanceCapacity,
    TrailCapacity,
};

constexpr bool IsValid(RenderTapeFailure value) noexcept
{
    switch (value)
    {
    case RenderTapeFailure::None:
    case RenderTapeFailure::InvalidIdentity:
    case RenderTapeFailure::InvalidArgument:
    case RenderTapeFailure::NonFiniteValue:
    case RenderTapeFailure::InvalidOperation:
    case RenderTapeFailure::UnsupportedSemantic:
    case RenderTapeFailure::UnbalancedPrimitive:
    case RenderTapeFailure::UnbalancedMatrixStack:
    case RenderTapeFailure::UnbalancedAttributeStack:
    case RenderTapeFailure::EntryCapacity:
    case RenderTapeFailure::VertexCapacity:
    case RenderTapeFailure::IndexCapacity:
    case RenderTapeFailure::ConstantsCapacity:
    case RenderTapeFailure::BonePaletteCapacity:
    case RenderTapeFailure::QuadInstanceCapacity:
    case RenderTapeFailure::LogicalAssetCapacity:
    case RenderTapeFailure::LogicalGeometryCapacity:
    case RenderTapeFailure::OwnerRequestCapacity:
    case RenderTapeFailure::PayloadCapacity:
    case RenderTapeFailure::RigidInstanceCapacity:
    case RenderTapeFailure::ParticleInstanceCapacity:
    case RenderTapeFailure::TrailCapacity:
        return true;
    }
    return false;
}

// Remaining finite state-axis enums (PLAN_P1R5.1.md section 5.1: "Add finite
// value enums for every state axis proved by the inventory"). Each
// enumerator set is exactly the values the R5.0/R5.1 graphics inventory
// proves today; extend it only alongside new inventory evidence, never
// speculatively.
// alongside new inventory evidence, never speculatively.

enum class RenderCompareFunction : std::uint8_t
{
    Less,
    LessOrEqual,
    Greater,
    Always,
    Equal, // RmlUI clip-mask intersection and readback.
};

constexpr bool IsValid(RenderCompareFunction value) noexcept
{
    switch (value)
    {
    case RenderCompareFunction::Less:
    case RenderCompareFunction::LessOrEqual:
    case RenderCompareFunction::Greater:
    case RenderCompareFunction::Always:
    case RenderCompareFunction::Equal:
        return true;
    }
    return false;
}

enum class RenderBlendFactor : std::uint8_t
{
    Zero,
    One,
    SrcAlpha,
    OneMinusSrcAlpha,
    SrcColor,
    OneMinusSrcColor,
};

constexpr bool IsValid(RenderBlendFactor value) noexcept
{
    switch (value)
    {
    case RenderBlendFactor::Zero:
    case RenderBlendFactor::One:
    case RenderBlendFactor::SrcAlpha:
    case RenderBlendFactor::OneMinusSrcAlpha:
    case RenderBlendFactor::SrcColor:
    case RenderBlendFactor::OneMinusSrcColor:
        return true;
    }
    return false;
}

enum class RenderCullFace : std::uint8_t
{
    Front,
    Back,
};

constexpr bool IsValid(RenderCullFace value) noexcept
{
    switch (value)
    {
    case RenderCullFace::Front:
    case RenderCullFace::Back:
        return true;
    }
    return false;
}

enum class RenderFrontFace : std::uint8_t
{
    CounterClockwise,
    Clockwise,
};

constexpr bool IsValid(RenderFrontFace value) noexcept
{
    switch (value)
    {
    case RenderFrontFace::CounterClockwise:
    case RenderFrontFace::Clockwise:
        return true;
    }
    return false;
}

enum class RenderFogMode : std::uint8_t
{
    Linear,
};

constexpr bool IsValid(RenderFogMode value) noexcept
{
    switch (value)
    {
    case RenderFogMode::Linear:
        return true;
    }
    return false;
}

enum class RenderShadeMode : std::uint8_t
{
    Smooth,
};

constexpr bool IsValid(RenderShadeMode value) noexcept
{
    switch (value)
    {
    case RenderShadeMode::Smooth:
        return true;
    }
    return false;
}

enum class RenderTextureEnvironment : std::uint8_t
{
    Modulate,
    Add,
    GfxTint,
};

constexpr bool IsValid(RenderTextureEnvironment value) noexcept
{
    switch (value)
    {
    case RenderTextureEnvironment::Modulate:
    case RenderTextureEnvironment::Add:
    case RenderTextureEnvironment::GfxTint:
        return true;
    }
    return false;
}

enum class LegacyPixelFormat : std::uint8_t
{
    Rgb,
    Rgba,
};

constexpr bool IsValid(LegacyPixelFormat value) noexcept
{
    switch (value)
    {
    case LegacyPixelFormat::Rgb:
    case LegacyPixelFormat::Rgba:
        return true;
    }
    return false;
}

enum class RenderClientArrayScalarType : std::uint8_t
{
    Float,
};

constexpr bool IsValid(RenderClientArrayScalarType value) noexcept
{
    switch (value)
    {
    case RenderClientArrayScalarType::Float:
        return true;
    }
    return false;
}

enum class RenderClientArraySemantic : std::uint8_t
{
    Position,
    Color,
    TextureCoordinate,
    Normal,
};

constexpr bool IsValid(RenderClientArraySemantic value) noexcept
{
    switch (value)
    {
    case RenderClientArraySemantic::Position:
    case RenderClientArraySemantic::Color:
    case RenderClientArraySemantic::TextureCoordinate:
    case RenderClientArraySemantic::Normal:
        return true;
    }
    return false;
}

// The current finite legacy draw-pass set (matches the existing
// Legacy pass values from the retired transition renderer,
// which R5.1 section 7.4 deletes). Named RenderTapePass here, not
// SessionDrawPass, so this portable header never redefines the
// still-present transition enum while both exist during the migration.
enum class RenderTapePass : std::uint8_t
{
    Terrain,
    Objects,
    Characters,
    Items,
    Effects,
    Sprites,
    UserInterface,
};

constexpr bool IsValid(RenderTapePass value) noexcept
{
    switch (value)
    {
    case RenderTapePass::Terrain:
    case RenderTapePass::Objects:
    case RenderTapePass::Characters:
    case RenderTapePass::Items:
    case RenderTapePass::Effects:
    case RenderTapePass::Sprites:
    case RenderTapePass::UserInterface:
        return true;
    }
    return false;
}

// Two more finite state-axis enums, needed only once RenderTapeConstants
// (below) exists to hold them; evidence-grounded exactly like the section
// 5.1 axes above (glStencilOp fail/zfail is always GL_KEEP, zpass observed
// over {GL_KEEP, GL_INCR, GL_DECR}; glPolygonMode observed only over
// front-line/front-fill per section 5.3).

enum class RenderStencilOperation : std::uint8_t
{
    Keep,
    Incr,
    Decr,
};

constexpr bool IsValid(RenderStencilOperation value) noexcept
{
    switch (value)
    {
    case RenderStencilOperation::Keep:
    case RenderStencilOperation::Incr:
    case RenderStencilOperation::Decr:
        return true;
    }
    return false;
}

enum class RenderPolygonMode : std::uint8_t
{
    Fill,
    Line,
};

constexpr bool IsValid(RenderPolygonMode value) noexcept
{
    switch (value)
    {
    case RenderPolygonMode::Fill:
    case RenderPolygonMode::Line:
        return true;
    }
    return false;
}

// A portable (x, y, width, height) rectangle, shared by viewport, scissor,
// and owner-request source/target rectangles (section 5.4, 5.5).
struct RenderTapeRect final
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    friend bool operator==(const RenderTapeRect &, const RenderTapeRect &) = default;
};

// The one canonical vertex (section 5.3). All four attributes are present on
// every vertex; the recorder is responsible for filling missing attributes
// from the pass seed/current latched value, never from geometry.
struct RenderTapeVertex final
{
    std::array<float, 4> position;
    std::array<float, 2> textureCoordinate;
    std::array<float, 4> color;
    std::array<float, 3> normal;
    // Decoded BMD vertices carry trusted position/normal bone indices and the
    // original position index. Dynamic tape geometry leaves these at zero.
    std::array<std::uint32_t, 4> bmdSource{};
};

// One CPU-produced BMD bone matrix. The shader reads this exact 3x4 row-major
// layout from a frame-sized storage buffer.
struct RenderTapeBoneMatrix final
{
    std::array<float, 4> row0;
    std::array<float, 4> row1;
    std::array<float, 4> row2;
};

// Portable identity for immutable vertex/index data owned by one exact
// session generation. Native GPU buffers remain backend-private.
inline bool IsValid(const RenderTapeBoneMatrix &matrix) noexcept
{
    for (const auto *row : {&matrix.row0, &matrix.row1, &matrix.row2})
        for (const float value : *row)
            if (!std::isfinite(value))
                return false;
    return true;
}

struct LogicalGeometryAssetRef final
{
    std::uint64_t sessionId = 0;
    std::uint64_t generation = 0;
    std::uint64_t revision = 0;

    friend bool operator==(const LogicalGeometryAssetRef &,
                           const LogicalGeometryAssetRef &) = default;
};

constexpr bool IsValid(LogicalGeometryAssetRef value) noexcept
{
    return value.sessionId != 0 && value.generation != 0 && value.revision != 0;
}

struct LogicalGeometryAssetRefHash final
{
    std::size_t operator()(LogicalGeometryAssetRef value) const noexcept
    {
        return std::hash<std::uint64_t>{}(value.sessionId ^ std::rotl(value.generation, 17) ^
                                          std::rotl(value.revision, 33));
    }
};

struct RenderTapeTerrainCell final
{
    float height = 0.0F;
    std::uint32_t wall = 0;
    float alpha = 0.0F;
    float padding = 0.0F;
};

struct RenderTapeTerrainInstance final
{
    std::uint32_t tileIndex = 0;
    float grassU = 0.0F;
    float grassHeight = 0.0F;
    float padding = 0.0F;
};

// Compact parameters selected by RenderGeometryMode. QuadInstances stores four
// object-space corners with U in each W plus four V values. SpriteInstances
// stores view-space center/half-width, half-height/sin/cos, and one UV rect in
// corners 0-2. Both modes share one color and the immutable unit quad.
struct RenderTapeQuadInstance final
{
    std::array<std::array<float, 4>, 4> corners;
    std::array<float, 4> v;
    std::array<float, 4> color;
};

inline bool IsValid(const RenderTapeParticleInstance &instance) noexcept
{
    for (const auto *row :
         {&instance.centerHalfWidth, &instance.halfHeightRotation, &instance.uv, &instance.color})
        for (const float value : *row)
            if (!std::isfinite(value))
                return false;
    return true;
}

inline bool IsValid(const RenderTapeQuadInstance &instance) noexcept
{
    for (const auto &corner : instance.corners)
    {
        for (float value : corner)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
    }
    for (float value : instance.v)
    {
        if (!std::isfinite(value))
        {
            return false;
        }
    }
    for (float value : instance.color)
    {
        if (!std::isfinite(value))
        {
            return false;
        }
    }
    return true;
}

enum class RenderBmdUvMode : std::uint32_t
{
    Mesh,
    Chrome,
    Chrome2,
    Chrome3,
    Chrome4,
    Chrome5,
    Chrome6,
    Chrome7,
    Metal,
    Oil,
};

struct RenderTapeBmdConstants final
{
    std::uint32_t rigidInstanceOffset = 0;
    std::uint32_t rigidInstanceCount = 0;
    std::uint32_t paletteOffset = 0;
    std::uint32_t paletteCount = 0;
    RenderBmdUvMode uvMode = RenderBmdUvMode::Mesh;
    bool enabled = false;
    bool translate = false;
    bool lighting = false;
    bool uvScroll = false;
    bool wave = false;
    bool scaledBone = false;
    bool shadow = false;
    bool rigid = false;
    RenderTapeBoneMatrix rigidTransform{};

    float positionScale = 1.0F;
    float boneScale = 1.0F;
    float bodyScale = 1.0F;
    float waveTime = 0.0F;
    std::array<float, 3> bodyOrigin{};
    float alpha = 1.0F;
    std::array<float, 3> bodyLight{1.0F, 1.0F, 1.0F};
    float wavePhase = 0.0F;
    std::array<float, 4> baseColor{1.0F, 1.0F, 1.0F, 1.0F};
    std::array<float, 3> lightPosition{};
    float chromeWave = 0.0F;
    std::array<float, 2> uvOffset{};
    float chromeTime = 0.0F;
    float padding = 0.0F;
    std::array<float, 3> chromeLight{};
    std::array<float, 3> legacyLight{};
    float shadowScaleX = 0.0F;
    float shadowScaleY = 0.0F;
    float terrainSpecialHeight = 0.0F;
    LogicalGeometryAssetRef shadowTerrainGeometry;
};

struct LogicalGeometryAssetLease final
{
    LogicalGeometryAssetRef asset;
    std::shared_ptr<const std::vector<RenderTapeVertex>> vertices;
    std::shared_ptr<const std::vector<std::uint32_t>> indices;
    std::shared_ptr<const std::vector<RenderTapeTerrainCell>> terrainCells;
    std::shared_ptr<const std::vector<RenderTapeTerrainInstance>> terrainInstances;
    std::shared_ptr<const TerrainLightSnapshot> terrainLight;
};

inline bool IsValid(const LogicalGeometryAssetLease &value) noexcept
{
    return IsValid(value.asset) && value.vertices != nullptr && value.indices != nullptr &&
           !value.vertices->empty() && !value.indices->empty() &&
           (value.terrainCells == nullptr || value.terrainCells->size() == 256 * 256) &&
           (value.terrainInstances == nullptr || !value.terrainInstances->empty()) &&
           value.vertices->size() <= (std::numeric_limits<std::uint32_t>::max)() &&
           value.indices->size() <= (std::numeric_limits<std::uint32_t>::max)() &&
           (value.terrainCells == nullptr ||
            value.terrainCells->size() <= (std::numeric_limits<std::uint32_t>::max)());
}

// RenderPipelineKey contains RenderPositionSpace (section 5.3); ordinary
// geometry is Object, and only the explicit CPU-expanded point path is Clip.
struct RenderPipelineKey final
{
    RenderPositionSpace positionSpace = RenderPositionSpace::Object;
    RenderGeometryMode geometryMode = RenderGeometryMode::Vertices;

    friend bool operator==(const RenderPipelineKey &, const RenderPipelineKey &) = default;
};

struct RenderTapeQuadConstants final
{
    std::uint64_t runId = 0;
    std::uint32_t instanceOffset = 0;
    std::uint32_t instanceCount = 0;

    friend bool operator==(const RenderTapeQuadConstants &,
                           const RenderTapeQuadConstants &) = default;
};

struct RenderTapeTrailConstants final
{
    std::uint32_t sampleOffset = 0;
    std::uint32_t sampleCount = 0;
    std::uint32_t faceMask = 0;
    float secondFaceUOffset = 0.f;

    friend bool operator==(const RenderTapeTrailConstants &,
                           const RenderTapeTrailConstants &) = default;
};

struct RenderTapeGrassConstants final
{
    std::uint64_t runId = 0;
    float windSpeed = 0.0F;
    float windScale = 0.0F;
    float windFrequency = 0.0F;
    float specialHeight = 0.0F;

    friend bool operator==(const RenderTapeGrassConstants &,
                           const RenderTapeGrassConstants &) = default;
};

enum RenderTapeTerrainFlags : std::uint32_t
{
    RenderTapeTerrainAlpha = 1U << 0U,
    RenderTapeTerrainOceanBlend = 1U << 1U,
    RenderTapeTerrainWater = 1U << 2U,
};

struct RenderTapeTerrainConstants final
{
    std::array<float, 2> uvScale{};
    float waterMove = 0.0F;
    float waterWindScale = 0.0F;
    float windSpeed = 0.0F;
    float windScale = 0.0F;
    float windFrequency = 0.0F;
    float specialHeight = 0.0F;
    std::uint32_t flags = 0;

    friend bool operator==(const RenderTapeTerrainConstants &,
                           const RenderTapeTerrainConstants &) = default;
};

// Slice offsets/counts into the tape's flat vertex/index arrays, plus a
// constants-array index and a sampled asset (section 5.3). The semantic
// optional asset uses LogicalRenderAssetRef's zero identity/revision pair as
// its no-asset encoding, preserving the generated 64-byte bound.
struct RenderTapeDraw final
{
    std::uint32_t vertexOffset = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexOffset = 0;
    std::uint32_t indexCount = 0;
    RenderIndexTopology topology = RenderIndexTopology::Triangles;
    RenderPipelineKey pipeline;
    std::uint32_t constantsIndex = 0;
    LogicalRenderAssetRef asset;
    LogicalGeometryAssetRef geometry;

    friend bool operator==(const RenderTapeDraw &, const RenderTapeDraw &) = default;
};

inline constexpr std::array<float, 16> RenderTapeIdentityMatrix4x4{
    1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
};

// One complete immutable constants value per draw (section 5.4). Default
// member values match the R5.1 pass seed exactly, except viewport/scissor
// rectangles and fog values, which the recorder must always set explicitly
// from the exact session's surface/fog state.
struct RenderTapeConstants final
{
    std::array<float, 16> modelView = RenderTapeIdentityMatrix4x4;
    std::array<float, 16> projection = RenderTapeIdentityMatrix4x4;
    std::array<float, 16> textureMatrix = RenderTapeIdentityMatrix4x4;

    RenderTapeRect viewport;
    bool scissorEnable = false;
    RenderTapeRect scissor;

    std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
    std::array<float, 3> normal{0.0F, 0.0F, 1.0F};

    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    RenderCompareFunction depthCompare = RenderCompareFunction::LessOrEqual;
    float depthRangeNear = 0.0F;
    float depthRangeFar = 1.0F;

    bool cullEnable = true;
    RenderCullFace cullFace = RenderCullFace::Back;
    RenderFrontFace frontFace = RenderFrontFace::CounterClockwise;

    bool blendEnable = false;
    RenderBlendFactor blendSrc = RenderBlendFactor::One;
    RenderBlendFactor blendDst = RenderBlendFactor::Zero;

    bool alphaTestEnable = false;
    RenderCompareFunction alphaCompare = RenderCompareFunction::Greater;
    float alphaReference = 0.25F;

    bool fogEnable = false;
    RenderFogMode fogMode = RenderFogMode::Linear;
    std::array<float, 4> fogColor{0.0F, 0.0F, 0.0F, 0.0F};
    float fogDensity = 0.0F;
    float fogStart = 0.0F;
    float fogEnd = 0.0F;

    bool lightingEnable = false;
    bool textureEnable = true;
    RenderTextureEnvironment textureEnvironment = RenderTextureEnvironment::Modulate;
    // GFx RGB scale followed by RGB offsets, with source alpha unchanged.
    std::array<float, 4> textureTint{1.0F, 0.0F, 0.0F, 0.0F};

    bool stencilEnable = false;
    RenderCompareFunction stencilCompare = RenderCompareFunction::Always;
    std::uint32_t stencilReference = 0;
    std::uint32_t stencilReadMask = 0xFFFFFFFFu;
    RenderStencilOperation stencilFailOp = RenderStencilOperation::Keep;
    RenderStencilOperation stencilDepthFailOp = RenderStencilOperation::Keep;
    RenderStencilOperation stencilPassOp = RenderStencilOperation::Keep;

    std::array<bool, 4> colorWriteMask{true, true, true, true};

    RenderPolygonMode polygonFrontMode = RenderPolygonMode::Fill;
    float lineWidth = 1.0F;

    RenderShadeMode shadeMode = RenderShadeMode::Smooth;

    RenderTapeBmdConstants bmd;
    RenderTapeQuadConstants quad;
    RenderTapeTrailConstants trail;
    RenderTapeGrassConstants grass;
    RenderTapeTerrainConstants terrain;

    friend bool operator==(const RenderTapeConstants &, const RenderTapeConstants &) = default;
};

inline bool IsValid(const RenderTapeConstants &constants) noexcept
{
    const auto allFinite = [](std::span<const float> values) noexcept {
        for (float value : values)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
        return true;
    };
    const std::array<float, 19> scalars{constants.depthRangeNear,  constants.depthRangeFar,
                                        constants.alphaReference,  constants.fogDensity,
                                        constants.fogStart,        constants.fogEnd,
                                        constants.lineWidth,       constants.grass.windSpeed,
                                        constants.grass.windScale, constants.grass.windFrequency,
                                        constants.grass.specialHeight,
                                        constants.terrain.uvScale[0],
                                        constants.terrain.uvScale[1],
                                        constants.terrain.waterMove,
                                        constants.terrain.waterWindScale,
                                        constants.terrain.windSpeed,
                                        constants.terrain.windScale,
                                        constants.terrain.windFrequency,
                                        constants.terrain.specialHeight};
    return IsValid(constants.depthCompare) && IsValid(constants.cullFace) &&
           IsValid(constants.frontFace) && IsValid(constants.blendSrc) &&
           IsValid(constants.blendDst) && IsValid(constants.alphaCompare) &&
           IsValid(constants.fogMode) && IsValid(constants.textureEnvironment) &&
           IsValid(constants.stencilCompare) && IsValid(constants.stencilFailOp) &&
           IsValid(constants.stencilDepthFailOp) && IsValid(constants.stencilPassOp) &&
           IsValid(constants.polygonFrontMode) && IsValid(constants.shadeMode) &&
           allFinite(constants.modelView) && allFinite(constants.projection) &&
           allFinite(constants.textureMatrix) && allFinite(constants.color) &&
           allFinite(constants.normal) && allFinite(scalars) && allFinite(constants.fogColor) &&
           allFinite(constants.textureTint);
}

inline bool IsValid(const RenderTapeVertex &vertex) noexcept
{
    const auto allFinite = [](std::span<const float> values) noexcept {
        for (float value : values)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
        return true;
    };
    return allFinite(vertex.position) && allFinite(vertex.textureCoordinate) &&
           allFinite(vertex.color) && allFinite(vertex.normal);
}

// One ordered entry in the tape's single entry stream (section 5.5). `index`
// points into Clears()/Draws()/OwnerRequests() depending on `kind`. Field
// order (stableOrder first) packs this into the section 5.8 16-byte bound;
// declaring the uint64 before the two 1-byte enums avoids the tail padding
// that keeping the plan's prose order (kind, pass, stableOrder) would add.
struct RenderTapeEntry final
{
    std::uint64_t stableOrder = 0;
    RenderTapeEntryKind kind = RenderTapeEntryKind::Clear;
    RenderTapePass pass = RenderTapePass::Terrain;
    std::uint32_t index = 0;

    friend bool operator==(const RenderTapeEntry &, const RenderTapeEntry &) = default;
};

// Portable color/depth/stencil mask bits and the exact clear values
// (section 5.5). A clear is not a draw and keeps its source position in the
// entry stream via its own RenderTapeEntry.
struct RenderTapeClear final
{
    bool clearColor = false;
    bool clearDepth = false;
    bool clearStencil = false;
    std::array<float, 4> color{0.0F, 0.0F, 0.0F, 0.0F};
    float depth = 1.0F;
    std::uint32_t stencil = 0;
    bool scissorEnable = false;
    RenderTapeRect scissor;
    std::array<bool, 4> colorWriteMask{true, true, true, true};
    bool depthWriteEnable = true;
};

inline bool IsValid(const RenderTapeClear &clear) noexcept
{
    return (!clear.scissorEnable || (clear.scissor.width != 0 && clear.scissor.height != 0)) &&
           std::isfinite(clear.depth) &&
           std::all_of(clear.color.begin(), clear.color.end(),
                       [](float value) noexcept { return std::isfinite(value); });
}

// The three owner-only request payloads (section 5.5). RenderOwnerRequest is
// a closed union of exactly these three value types; there is no fourth
// alternative and no generic/open request shape.

struct UploadLogicalAssetRgba8Request final
{
    LogicalRenderAssetRef destination;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t payloadOffset = 0;
    std::uint32_t payloadByteCount = 0;
    RenderAssetRetention retention = RenderAssetRetention::FrameOnly;
    RenderSamplerIntent sampler;
};

struct CopyTargetToLogicalTextureRequest final
{
    SessionId sourceSession;
    SessionGeneration sourceGeneration;
    std::uint64_t sourceSurfaceGeneration = 0;
    RenderTapeRect sourceRect;
    LogicalRenderAssetRef destination;
    RenderSamplerIntent sampler;
};

struct DownloadTargetRgba8Request final
{
    SessionId sourceSession;
    SessionGeneration sourceGeneration;
    std::uint64_t sourceSurfaceGeneration = 0;
    std::uint64_t sourceFrameSequence = 0;
    RenderTapeRect rect;
    bool verticallyFlipped = false;
    std::uint64_t requestId = 0;
};

using RenderOwnerRequest =
    std::variant<UploadLogicalAssetRgba8Request, CopyTargetToLogicalTextureRequest,
                 DownloadTargetRgba8Request>;

// Value-only results retained by the application owner until the matching
// frame fence retires. A zero destination reference means that the completion
// has no logical-asset destination.
struct SessionReplayCompletion final
{
    SessionId sessionId;
    SessionGeneration generation;
    std::uint64_t frameSequence = 0;
    std::uint64_t surfaceGeneration = 0;
    bool succeeded = false;
};

constexpr bool IsValid(const SessionReplayCompletion &completion) noexcept
{
    return completion.sessionId.RawValue() != 0 && completion.generation.RawValue() != 0 &&
           completion.frameSequence != 0 && completion.surfaceGeneration != 0;
}

struct RenderOwnerRequestCompletion final
{
    RenderOwnerRequestKind kind = RenderOwnerRequestKind::DownloadTargetRgba8;
    std::uint64_t requestId = 0;
    SessionId sessionId;
    SessionGeneration generation;
    std::uint64_t frameSequence = 0;
    std::uint64_t surfaceGeneration = 0;
    bool succeeded = false;
    LogicalRenderAssetRef destination;
    std::uint32_t payloadOffset = 0;
    std::uint32_t payloadByteCount = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    RenderSamplerIntent sampler;
};

constexpr bool IsValid(const RenderOwnerRequestCompletion &completion) noexcept
{
    if (completion.kind == RenderOwnerRequestKind::CopyTargetToLogicalTexture &&
        !IsValid(completion.sampler))
    {
        return false;
    }
    return IsValid(completion.kind) && completion.sessionId.RawValue() != 0 &&
           completion.generation.RawValue() != 0 && completion.frameSequence != 0 &&
           completion.surfaceGeneration != 0 &&
           (completion.destination.id.value == 0 ? completion.destination.revision == 0
                                                 : IsValid(completion.destination));
}

class SdlGpuRenderBackend;

// Application-owned completion storage. Capacity grows from actual replay
// work and stays at its high watermark for later frames.
class ApplicationRenderCompletionBatch final
{
  public:
    static constexpr std::optional<std::size_t> CheckedMultiply(std::size_t left,
                                                                std::size_t right) noexcept
    {
        if (right != 0 && left > (std::numeric_limits<std::size_t>::max)() / right)
        {
            return std::nullopt;
        }
        return left * right;
    }

    ApplicationRenderCompletionBatch() noexcept = default;
    ApplicationRenderCompletionBatch(ApplicationRenderCompletionBatch &&) noexcept = default;
    ApplicationRenderCompletionBatch &operator=(ApplicationRenderCompletionBatch &&) noexcept =
        default;
    ApplicationRenderCompletionBatch(const ApplicationRenderCompletionBatch &) = delete;
    ApplicationRenderCompletionBatch &operator=(const ApplicationRenderCompletionBatch &) = delete;

    // Must succeed before replay starts. The caller supplies the exact,
    // preflighted total of completion bytes for this frame. A zero total
    // remains ready without allocating an arena.
    bool Prepare(std::size_t payloadByteCapacity) noexcept
    {
        if (Ready() && payloadByteCapacity <= payloadByteCapacity_)
        {
            return true;
        }
        if (!sessionCompletions_.empty() || !requestCompletions_.empty())
        {
            return false;
        }

        try
        {
            if (payloadByteCapacity > payload_.size())
            {
                payload_.resize(payloadByteCapacity);
            }
        }
        catch (...)
        {
            return false;
        }
        payloadByteCapacity_ = payload_.size();
        prepared_ = true;
        return true;
    }

    bool Ready() const noexcept
    {
        return prepared_;
    }

    std::size_t PayloadCapacityBytes() const noexcept
    {
        return payloadByteCapacity_;
    }

    bool TryAppendSession(SessionReplayCompletion completion) noexcept
    {
        if (!Ready() || !IsValid(completion))
        {
            return false;
        }
        try
        {
            sessionCompletions_.push_back(std::move(completion));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool TryAppendRequest(RenderOwnerRequestCompletion completion,
                          std::span<const std::byte> payload) noexcept
    {
        if (!Ready() || !IsValid(completion) ||
            payload.size() > payloadByteCapacity_ - payloadBytesUsed_ ||
            payload.size() > (std::numeric_limits<std::uint32_t>::max)())
        {
            return false;
        }

        const std::uint32_t offset = static_cast<std::uint32_t>(payloadBytesUsed_);
        const std::uint32_t byteCount = static_cast<std::uint32_t>(payload.size());
        if (!payload.empty())
        {
            std::copy(payload.begin(), payload.end(), payload_.data() + payloadBytesUsed_);
        }
        completion.payloadOffset = offset;
        completion.payloadByteCount = byteCount;
        try
        {
            requestCompletions_.push_back(std::move(completion));
            payloadBytesUsed_ += payload.size();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::size_t SessionCompletionCount() const noexcept
    {
        return sessionCompletions_.size();
    }

    std::size_t RequestCompletionCount() const noexcept
    {
        return requestCompletions_.size();
    }

    const SessionReplayCompletion *SessionCompletionAt(std::size_t index) const noexcept
    {
        if (index >= sessionCompletions_.size())
        {
            return nullptr;
        }
        return &sessionCompletions_[index];
    }

    const RenderOwnerRequestCompletion *RequestCompletionAt(std::size_t index) const noexcept
    {
        if (index >= requestCompletions_.size())
        {
            return nullptr;
        }
        return &requestCompletions_[index];
    }

    std::span<const std::byte> Payload(
        const RenderOwnerRequestCompletion &completion) const noexcept
    {
        if (!Ready() || completion.payloadOffset > payloadBytesUsed_ ||
            completion.payloadByteCount > payloadBytesUsed_ - completion.payloadOffset)
        {
            return {};
        }
        if (completion.payloadByteCount == 0)
        {
            return {};
        }
        return {payload_.data() + completion.payloadOffset, completion.payloadByteCount};
    }

    std::size_t PayloadBytes() const noexcept
    {
        return payloadBytesUsed_;
    }

    void Clear() noexcept
    {
        sessionCompletions_.clear();
        requestCompletions_.clear();
        payloadBytesUsed_ = 0;
    }

  private:
    friend class SdlGpuRenderBackend;

    struct Checkpoint final
    {
        std::size_t sessionCount = 0;
        std::size_t requestCount = 0;
        std::size_t payloadBytesUsed = 0;
    };

    Checkpoint SaveCheckpoint() const noexcept
    {
        return {sessionCompletions_.size(), requestCompletions_.size(), payloadBytesUsed_};
    }

    void Rollback(Checkpoint checkpoint) noexcept
    {
        sessionCompletions_.erase(sessionCompletions_.begin() +
                                      static_cast<std::ptrdiff_t>(checkpoint.sessionCount),
                                  sessionCompletions_.end());
        requestCompletions_.erase(requestCompletions_.begin() +
                                      static_cast<std::ptrdiff_t>(checkpoint.requestCount),
                                  requestCompletions_.end());
        payloadBytesUsed_ = checkpoint.payloadBytesUsed;
    }

    std::vector<SessionReplayCompletion> sessionCompletions_;
    std::vector<RenderOwnerRequestCompletion> requestCompletions_;
    std::vector<std::byte> payload_;
    std::size_t payloadByteCapacity_ = 0;
    std::size_t payloadBytesUsed_ = 0;
    bool prepared_ = false;
};

class ApplicationRenderFrameTestPeer;
class LegacyRenderFacade;

// Reusable storage for one in-flight tape. Recording grows these vectors on
// demand and PrepareForLease keeps the allocated memory for the next frame.
// There is no content-dependent fixed ceiling: only allocation failure and
// the tape format's uint32 offsets can reject growth.
struct RenderTapeBlock final
{
    struct AssetMembershipSlot final
    {
        std::uint64_t logicalRevision = 0;
        std::uint64_t logicalStamp = 0;
        std::uint64_t uploadRevision = 0;
        std::uint64_t uploadStamp = 0;
    };

    struct GeometryMembershipSlot final
    {
        std::uint64_t stamp = 0;
        std::uint32_t index = 0;
    };

    std::vector<RenderTapeEntry> entries;
    std::vector<RenderTapeClear> clears;
    std::vector<RenderTapeDraw> draws;
    std::vector<std::uint64_t> drawMergeKeys;
    std::vector<RenderTapeVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<RenderTapeConstants> constants;
    std::vector<RenderTapeBoneMatrix> boneMatrices;
    std::vector<RenderTapeQuadInstance> quadInstances;
    std::vector<RenderTapeParticleInstance> particleInstances;
    std::vector<RenderTapeTrailSample> trailSamples;
    std::vector<RenderTapeTrailInstance> trailInstances;
    std::vector<RenderTapeRigidInstance> rigidInstances;
    std::vector<LogicalRenderAssetRef> logicalAssets;
    std::vector<AssetMembershipSlot> assetMembership;
    std::vector<LogicalGeometryAssetLease> geometryAssets;
    std::vector<GeometryMembershipSlot> geometryMembership;
    std::vector<RenderOwnerRequest> ownerRequests;
    std::vector<std::byte> payload;

    std::size_t entryCount = 0;
    std::size_t clearCount = 0;
    std::size_t drawCount = 0;
    std::size_t vertexCount = 0;
    std::size_t indexCount = 0;
    std::size_t constantsCount = 0;
    std::size_t boneMatrixCount = 0;
    std::size_t quadInstanceCount = 0;
    std::size_t particleInstanceCount = 0;
    std::size_t trailSampleCount = 0;
    std::size_t trailInstanceCount = 0;
    std::size_t rigidInstanceCount = 0;
    std::size_t logicalAssetCount = 0;
    std::size_t geometryAssetCount = 0;
    std::size_t geometryAssetBytes = 0;
    std::size_t ownerRequestCount = 0;
    std::size_t payloadBytesUsed = 0;
    std::uint64_t membershipStamp = 1;

    bool ContainsLogicalAsset(LogicalRenderAssetRef asset) const noexcept
    {
        return asset.id.value < assetMembership.size() &&
               assetMembership[asset.id.value].logicalStamp == membershipStamp &&
               assetMembership[asset.id.value].logicalRevision == asset.revision;
    }

    bool ContainsUploadDestination(LogicalRenderAssetRef asset) const noexcept
    {
        return asset.id.value < assetMembership.size() &&
               assetMembership[asset.id.value].uploadStamp == membershipStamp &&
               assetMembership[asset.id.value].uploadRevision == asset.revision;
    }

    bool InsertLogicalAsset(LogicalRenderAssetRef asset) noexcept
    {
        AssetMembershipSlot *const slot = PrepareAssetMembership(asset);
        if (slot == nullptr || ContainsLogicalAsset(asset))
        {
            return false;
        }
        slot->logicalRevision = asset.revision;
        slot->logicalStamp = membershipStamp;
        return true;
    }

    bool InsertUploadDestination(LogicalRenderAssetRef asset) noexcept
    {
        AssetMembershipSlot *const slot = PrepareAssetMembership(asset);
        if (slot == nullptr || ContainsUploadDestination(asset))
        {
            return false;
        }
        slot->uploadRevision = asset.revision;
        slot->uploadStamp = membershipStamp;
        return true;
    }

    void EraseLogicalAsset(LogicalRenderAssetRef asset) noexcept
    {
        if (ContainsLogicalAsset(asset))
        {
            assetMembership[asset.id.value].logicalStamp = 0;
        }
    }

    void EraseUploadDestination(LogicalRenderAssetRef asset) noexcept
    {
        if (ContainsUploadDestination(asset))
        {
            assetMembership[asset.id.value].uploadStamp = 0;
        }
    }

    bool ContainsGeometryAsset(LogicalGeometryAssetRef asset) const noexcept
    {
        return asset.revision < geometryMembership.size() &&
               geometryMembership[asset.revision].stamp == membershipStamp;
    }

    bool InsertGeometryAsset(LogicalGeometryAssetRef asset, std::uint32_t index) noexcept
    {
        GeometryMembershipSlot *const slot = PrepareGeometryMembership(asset);
        if (slot == nullptr || slot->stamp == membershipStamp)
        {
            return false;
        }
        slot->index = index;
        slot->stamp = membershipStamp;
        return true;
    }

    void EraseGeometryAsset(LogicalGeometryAssetRef asset) noexcept
    {
        if (ContainsGeometryAsset(asset))
        {
            geometryMembership[asset.revision].stamp = 0;
        }
    }

    std::uint32_t GeometryAssetIndex(LogicalGeometryAssetRef asset) const noexcept
    {
        return geometryMembership[asset.revision].index;
    }

    void PrepareForLease() noexcept
    {
        entryCount = 0;
        clearCount = 0;
        drawCount = 0;
        vertexCount = 0;
        indexCount = 0;
        constantsCount = 0;
        boneMatrixCount = 0;
        quadInstanceCount = 0;
        particleInstanceCount = 0;
        trailSampleCount = trailInstanceCount = 0;
        rigidInstanceCount = 0;
        logicalAssetCount = 0;
        geometryAssetCount = 0;
        geometryAssetBytes = 0;
        ownerRequestCount = 0;
        payloadBytesUsed = 0;
        ++membershipStamp;
        if (membershipStamp == 0)
        {
            for (AssetMembershipSlot &slot : assetMembership)
            {
                slot = {};
            }
            for (GeometryMembershipSlot &slot : geometryMembership)
            {
                slot = {};
            }
            membershipStamp = 1;
        }
    }

    std::size_t StorageBytes() const noexcept
    {
        return sizeof(*this) + entries.capacity() * sizeof(entries[0]) +
               clears.capacity() * sizeof(clears[0]) + draws.capacity() * sizeof(draws[0]) +
               drawMergeKeys.capacity() * sizeof(drawMergeKeys[0]) +
               vertices.capacity() * sizeof(vertices[0]) + indices.capacity() * sizeof(indices[0]) +
               constants.capacity() * sizeof(constants[0]) +
               boneMatrices.capacity() * sizeof(boneMatrices[0]) +
               quadInstances.capacity() * sizeof(quadInstances[0]) +
               particleInstances.capacity() * sizeof(particleInstances[0]) +
               trailSamples.capacity() * sizeof(trailSamples[0]) +
               trailInstances.capacity() * sizeof(trailInstances[0]) +
               rigidInstances.capacity() * sizeof(rigidInstances[0]) +
               logicalAssets.capacity() * sizeof(logicalAssets[0]) +
               assetMembership.capacity() * sizeof(assetMembership[0]) +
               geometryAssets.capacity() * sizeof(geometryAssets[0]) +
               geometryMembership.capacity() * sizeof(geometryMembership[0]) +
               ownerRequests.capacity() * sizeof(ownerRequests[0]) +
               payload.capacity() * sizeof(payload[0]);
    }

  private:
    AssetMembershipSlot *PrepareAssetMembership(LogicalRenderAssetRef asset) noexcept
    {
        if (!IsValid(asset) || asset.id.value >= (std::numeric_limits<std::size_t>::max)())
        {
            return nullptr;
        }
        const std::size_t index = static_cast<std::size_t>(asset.id.value);
        try
        {
            if (index >= assetMembership.size())
            {
                assetMembership.resize(index + 1);
            }
        }
        catch (...)
        {
            return nullptr;
        }
        return &assetMembership[index];
    }

    GeometryMembershipSlot *PrepareGeometryMembership(LogicalGeometryAssetRef asset) noexcept
    {
        if (!IsValid(asset) || asset.revision >= (std::numeric_limits<std::size_t>::max)())
        {
            return nullptr;
        }
        const std::size_t index = static_cast<std::size_t>(asset.revision);
        try
        {
            if (index >= geometryMembership.size())
            {
                geometryMembership.resize(index + 1);
            }
        }
        catch (...)
        {
            return nullptr;
        }
        return &geometryMembership[index];
    }
};

// A trusted, immutable internal draw-plan entry. The caller retains the
// geometry lease until AppendTrustedGeometryDrawBatch returns. Validation
// belongs at geometry creation/ownership changes, not in this hot path.
struct TrustedGeometryDraw final
{
    const LogicalGeometryAssetLease *geometry = nullptr;
    std::uint32_t vertexOffset = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexOffset = 0;
    std::uint32_t indexCount = 0;
    RenderIndexTopology topology = RenderIndexTopology::Triangles;
    RenderPipelineKey pipeline;
    RenderTapeConstants constants;
    LogicalRenderAssetRef asset;
    std::uint64_t mergeKey = 0;
};

// One complete, immutable, exact-session tape (PLAN_P1R5.1.md section 5.6).
// Moveable; owns a shared const lease of one application-pooled
// RenderTapeBlock, so the block stays out of the free pool until every
// SessionRenderTape referencing it is gone.
class SessionRenderTape final
{
  public:
    SessionRenderTape(std::shared_ptr<const RenderTapeBlock> block, SessionId id,
                      SessionGeneration generation, std::uint64_t frameSequence,
                      std::uint64_t surfaceGeneration, std::uint32_t viewportWidth,
                      std::uint32_t viewportHeight) noexcept
        : block_(std::move(block)), id_(id), generation_(generation), frameSequence_(frameSequence),
          surfaceGeneration_(surfaceGeneration), viewportWidth_(viewportWidth),
          viewportHeight_(viewportHeight)
    {
    }

    SessionId Id() const noexcept
    {
        return id_;
    }
    SessionGeneration Generation() const noexcept
    {
        return generation_;
    }
    std::uint64_t FrameSequence() const noexcept
    {
        return frameSequence_;
    }
    std::uint64_t SurfaceGeneration() const noexcept
    {
        return surfaceGeneration_;
    }
    std::uint32_t ViewportWidth() const noexcept
    {
        return viewportWidth_;
    }
    std::uint32_t ViewportHeight() const noexcept
    {
        return viewportHeight_;
    }
    bool WasValidatedByRecording() const noexcept
    {
        return validatedByRecording_;
    }

    bool HasValidStorageCounts() const noexcept
    {
        return block_ != nullptr && block_->entryCount <= block_->entries.size() &&
               block_->clearCount <= block_->clears.size() &&
               block_->drawCount <= block_->draws.size() &&
               block_->vertexCount <= block_->vertices.size() &&
               block_->indexCount <= block_->indices.size() &&
               block_->constantsCount <= block_->constants.size() &&
               block_->boneMatrixCount <= block_->boneMatrices.size() &&
               block_->quadInstanceCount <= block_->quadInstances.size() &&
               block_->particleInstanceCount <= block_->particleInstances.size() &&
               block_->trailSampleCount <= block_->trailSamples.size() &&
               block_->trailInstanceCount <= block_->trailInstances.size() &&
               block_->rigidInstanceCount <= block_->rigidInstances.size() &&
               block_->logicalAssetCount <= block_->logicalAssets.size() &&
               block_->geometryAssetCount <= block_->geometryAssets.size() &&
               block_->ownerRequestCount <= block_->ownerRequests.size() &&
               block_->payloadBytesUsed <= block_->payload.size();
    }

    std::span<const RenderTapeEntry> Entries() const noexcept
    {
        return {block_->entries.data(), block_->entryCount};
    }
    std::span<const RenderTapeClear> Clears() const noexcept
    {
        return {block_->clears.data(), block_->clearCount};
    }
    std::span<const RenderTapeDraw> Draws() const noexcept
    {
        return {block_->draws.data(), block_->drawCount};
    }
    std::span<const RenderTapeVertex> Vertices() const noexcept
    {
        return {block_->vertices.data(), block_->vertexCount};
    }
    std::span<const std::uint32_t> Indices() const noexcept
    {
        return {block_->indices.data(), block_->indexCount};
    }
    std::span<const RenderTapeConstants> Constants() const noexcept
    {
        return {block_->constants.data(), block_->constantsCount};
    }
    std::span<const RenderTapeBoneMatrix> BoneMatrices() const noexcept
    {
        return {block_->boneMatrices.data(), block_->boneMatrixCount};
    }
    std::span<const RenderTapeQuadInstance> QuadInstances() const noexcept
    {
        return {block_->quadInstances.data(), block_->quadInstanceCount};
    }
    std::span<const RenderTapeParticleInstance> ParticleInstances() const noexcept
    {
        return {block_->particleInstances.data(), block_->particleInstanceCount};
    }
    std::span<const RenderTapeTrailSample> TrailSamples() const noexcept
    {
        return {block_->trailSamples.data(), block_->trailSampleCount};
    }
    std::span<const RenderTapeTrailInstance> TrailInstances() const noexcept
    {
        return {block_->trailInstances.data(), block_->trailInstanceCount};
    }
    // Only for externally supplied, untrusted tape admission.
    bool HasValidExternalTrailDraw(RenderGeometryMode mode,
                                   const RenderTapeConstants &constants) const noexcept;
    std::span<const RenderTapeRigidInstance> RigidInstances() const noexcept
    {
        return {block_->rigidInstances.data(), block_->rigidInstanceCount};
    }
    std::span<const LogicalRenderAssetRef> LogicalAssets() const noexcept
    {
        return {block_->logicalAssets.data(), block_->logicalAssetCount};
    }
    std::span<const LogicalGeometryAssetLease> GeometryAssets() const noexcept
    {
        return {block_->geometryAssets.data(), block_->geometryAssetCount};
    }
    const LogicalGeometryAssetLease *FindGeometryAsset(LogicalGeometryAssetRef asset) const noexcept
    {
        return block_->ContainsGeometryAsset(asset)
                   ? &block_->geometryAssets[block_->GeometryAssetIndex(asset)]
                   : nullptr;
    }
    std::uint32_t GeometryAssetIndex(LogicalGeometryAssetRef asset) const noexcept
    {
        return block_->GeometryAssetIndex(asset);
    }
    std::size_t GeometryAssetBytes() const noexcept
    {
        return block_->geometryAssetBytes;
    }
    std::span<const RenderOwnerRequest> OwnerRequests() const noexcept
    {
        return {block_->ownerRequests.data(), block_->ownerRequestCount};
    }
    std::span<const std::byte> PayloadBytes() const noexcept
    {
        return {block_->payload.data(), block_->payloadBytesUsed};
    }
    std::size_t StorageBytes() const noexcept
    {
        return block_ == nullptr ? 0 : block_->StorageBytes();
    }

  private:
    friend class ApplicationRenderFrameTestPeer;
    friend class SessionRenderTapeRecording;

    struct RecordingValidatedTag final
    {
    };

    SessionRenderTape(std::shared_ptr<const RenderTapeBlock> block, SessionId id,
                      SessionGeneration generation, std::uint64_t frameSequence,
                      std::uint64_t surfaceGeneration, std::uint32_t viewportWidth,
                      std::uint32_t viewportHeight, RecordingValidatedTag) noexcept
        : SessionRenderTape(std::move(block), id, generation, frameSequence, surfaceGeneration,
                            viewportWidth, viewportHeight)
    {
        validatedByRecording_ = true;
    }

    std::shared_ptr<const RenderTapeBlock> block_;
    SessionId id_;
    SessionGeneration generation_;
    std::uint64_t frameSequence_;
    std::uint64_t surfaceGeneration_;
    std::uint32_t viewportWidth_;
    std::uint32_t viewportHeight_;
    bool validatedByRecording_ = false;
};

class SessionAdvanceResult final
{
  public:
    SessionAdvanceResult(SessionId sessionId, SessionGeneration generation,
                         std::uint64_t frameSequence) noexcept
        : sessionId_(sessionId), generation_(generation), frameSequence_(frameSequence)
    {
    }

    SessionId Id() const noexcept
    {
        return sessionId_;
    }
    SessionGeneration Generation() const noexcept
    {
        return generation_;
    }
    std::uint64_t FrameSequence() const noexcept
    {
        return frameSequence_;
    }
    SessionAdvanceStatus Status() const noexcept
    {
        return status_;
    }
    bool Succeeded() const noexcept
    {
        return status_ == SessionAdvanceStatus::Succeeded;
    }
    bool ExecutedInline() const noexcept
    {
        return executedInline_;
    }
    std::size_t WorkerStageCount() const noexcept
    {
        return workerStageCount_;
    }
    std::chrono::nanoseconds WallTime() const noexcept
    {
        return wallTime_ + orderedCommitWallTime_ + ownerWallTime_;
    }
    std::chrono::nanoseconds WorkerWallTime() const noexcept
    {
        return wallTime_;
    }
    std::chrono::nanoseconds OwnerWallTime() const noexcept
    {
        return ownerWallTime_;
    }
    std::chrono::nanoseconds OrderedCommitWallTime() const noexcept
    {
        return orderedCommitWallTime_;
    }
    std::chrono::nanoseconds CpuTime() const noexcept
    {
        return cpuTime_;
    }
    std::chrono::nanoseconds RenderTapeBuildWallTime() const noexcept
    {
        return renderTapeBuildWallTime_;
    }
    std::size_t RenderTapeBytes() const noexcept
    {
        return renderTape_.has_value() ? renderTape_->StorageBytes() : 0;
    }
    std::size_t RenderTapeItems() const noexcept
    {
        return renderTape_.has_value() ? renderTape_->Entries().size() : 0;
    }
    const SessionOrderedEffectBatch &OrderedEffects() const noexcept
    {
        return orderedEffects_;
    }
    bool OrderedCommitCompleted() const noexcept
    {
        return orderedCommitAttempted_ && orderedCommitSucceeded_;
    }
    const std::optional<SessionRenderTape> &RenderTape() const noexcept
    {
        return renderTape_;
    }

  private:
    friend class ApplicationRenderFrame;
    friend class ApplicationRenderFrameTestPeer;
    friend class ApplicationSessionScheduler;
    friend class SessionManager;
    friend bool CommitSessionOrderedEffects(std::span<SessionAdvanceResult>,
                                            SessionOrderedEffectSink *) noexcept;

    void CompleteOwnerStage(bool succeeded, std::chrono::nanoseconds wallTime,
                            std::chrono::nanoseconds renderTapeBuildWallTime,
                            std::optional<SessionRenderTape> renderTape) noexcept
    {
        ownerWallTime_ = wallTime;
        renderTapeBuildWallTime_ = renderTapeBuildWallTime;
        if (status_ != SessionAdvanceStatus::Succeeded || !succeeded)
        {
            status_ = SessionAdvanceStatus::Failed;
            renderTape_.reset();
            return;
        }
        renderTape_ = std::move(renderTape);
    }

    void CompleteRenderTapeStage(bool succeeded, std::chrono::nanoseconds renderTapeBuildWallTime,
                                 std::optional<SessionRenderTape> renderTape) noexcept
    {
        renderTapeBuildWallTime_ = renderTapeBuildWallTime;
        if (status_ != SessionAdvanceStatus::Succeeded || !succeeded)
        {
            status_ = SessionAdvanceStatus::Failed;
            renderTape_.reset();
            return;
        }
        renderTape_ = std::move(renderTape);
    }

    void AccumulateWorkerStage(const SessionAdvanceResult &stage) noexcept
    {
        wallTime_ += stage.wallTime_;
        cpuTime_ += stage.cpuTime_;
        executedInline_ = executedInline_ || stage.executedInline_;
        workerStageCount_ += stage.workerStageCount_;
    }

    SessionId sessionId_;
    SessionGeneration generation_;
    std::uint64_t frameSequence_;
    SessionAdvanceStatus status_ = SessionAdvanceStatus::Pending;
    bool executedInline_ = false;
    std::size_t workerStageCount_ = 0;
    std::chrono::nanoseconds wallTime_{};
    std::chrono::nanoseconds ownerWallTime_{};
    std::chrono::nanoseconds orderedCommitWallTime_{};
    std::chrono::nanoseconds renderTapeBuildWallTime_{};
    std::chrono::nanoseconds cpuTime_{};
    SessionOrderedEffectBatch orderedEffects_;
    bool orderedCommitAttempted_ = false;
    bool orderedCommitSucceeded_ = false;
    std::optional<SessionRenderTape> renderTape_;
};

// Bounded, string-free failure diagnostics (section 5.7): the first error
// only, latched once per recording.
struct RenderTapeFailureDiagnostics final
{
    RenderTapeFailure failure = RenderTapeFailure::None;
    std::uint32_t sourceInventoryRowId = 0;
    RenderTapePass pass = RenderTapePass::Terrain;
    std::uint64_t stableOrder = 0;
};

// The mutable in-progress recording that produces a SessionRenderTape
// (PLAN_P1R5.1.md section 5.7's state machine, minus the GL-specific
// pass/primitive/matrix semantics that the section 5.7 LegacyRenderFacade
// layers on top in Task 4 -- this type owns only the generic, atomic,
// capacity-checked append/finalize/abort primitives every one of the
// facade's typed operations ultimately goes through). Moveable,
// single-owner, spent after Finalize or Abort.
// There is no separate "Idle" state object: not holding one of these at all
// IS Idle (a session acquires one fresh from ApplicationRenderTapeStorage
// each frame, matching "BeginFrame" in the plan's state diagram); merely
// holding one IS Recording; Failed is the internal `Failed()` flag; and
// Finalize's successful return is the diagram's momentary Sealed step,
// immediately followed by Idle (the caller simply stops holding this
// recording afterward).
class SessionRenderTapeRecording final
{
  public:
    struct ReservedDrawRequest final
    {
        std::size_t indexCount = 0;
        RenderIndexTopology topology = RenderIndexTopology::Triangles;
        RenderPipelineKey pipeline;
        RenderTapeConstants constants;
        LogicalRenderAssetRef asset;
        std::uint64_t mergeKey = 0;
    };

    struct DrawReservation final
    {
        std::span<RenderTapeVertex> vertices;
        std::span<std::uint32_t> indices;
        std::uint32_t vertexOffset = 0;
        std::uint32_t indexOffset = 0;
        std::size_t vertexCount = 0;
        std::size_t indexCount = 0;
        std::size_t requestCount = 0;
        RenderTapePass pass = RenderTapePass::Terrain;
        std::uint32_t sourceInventoryRowId = 0;
        bool mergesLastDraw = false;
        bool addsLogicalAsset = false;
    };

    SessionRenderTapeRecording(std::shared_ptr<RenderTapeBlock> block, SessionId id,
                               SessionGeneration generation, std::uint64_t frameSequence,
                               std::uint64_t surfaceGeneration, std::uint32_t viewportWidth,
                               std::uint32_t viewportHeight) noexcept;

    SessionRenderTapeRecording(SessionRenderTapeRecording &&) noexcept = default;
    SessionRenderTapeRecording &operator=(SessionRenderTapeRecording &&) noexcept = default;
    SessionRenderTapeRecording(const SessionRenderTapeRecording &) = delete;
    SessionRenderTapeRecording &operator=(const SessionRenderTapeRecording &) = delete;

    SessionId Id() const noexcept
    {
        return id_;
    }
    SessionGeneration Generation() const noexcept
    {
        return generation_;
    }
    std::uint64_t FrameSequence() const noexcept
    {
        return frameSequence_;
    }
    std::uint64_t SurfaceGeneration() const noexcept
    {
        return surfaceGeneration_;
    }
    std::uint32_t ViewportWidth() const noexcept
    {
        return viewportWidth_;
    }
    std::uint32_t ViewportHeight() const noexcept
    {
        return viewportHeight_;
    }

    bool Spent() const noexcept
    {
        return blockOwner_ == nullptr;
    }
    bool Failed() const noexcept
    {
        return diagnostics_.failure != RenderTapeFailure::None;
    }
    const RenderTapeFailureDiagnostics &Diagnostics() const noexcept
    {
        return diagnostics_;
    }

    // Any caller (the Task 4 facade, once it exists) latches a semantic
    // failure it detects itself. A no-op once already failed or spent, per
    // the first-failure-latch contract: later calls never overwrite it.
    void LatchFailure(RenderTapeFailure failure, std::uint32_t sourceInventoryRowId,
                      RenderTapePass pass) noexcept;

    // Every Append* call is all-or-nothing: on failure it latches (if this
    // is the first failure) and leaves every count/cursor exactly as it was
    // before the call. `asset` is { id = 0, revision = 0 } when this draw
    // samples no texture; any nonzero asset must have both values valid.
    bool AppendClear(RenderTapePass pass, const RenderTapeClear &clear,
                     std::uint32_t sourceInventoryRowId = 0) noexcept;
    bool AppendDraw(RenderTapePass pass, std::span<const RenderTapeVertex> vertices,
                    std::span<const std::uint32_t> indices, RenderIndexTopology topology,
                    RenderPipelineKey pipeline, const RenderTapeConstants &constants,
                    LogicalRenderAssetRef asset, std::uint32_t sourceInventoryRowId = 0) noexcept;
    // Prepared rigid geometry and one compatible mesh/material/pass. Shadow or
    // order-dependent passes stay on their existing path. Copies instance values
    // into this recording; callers may reuse their preparation storage immediately.
    bool AppendRigidInstances(RenderTapePass pass, const LogicalGeometryAssetLease &geometry,
                              std::uint32_t vertexOffset, std::uint32_t vertexCount,
                              std::uint32_t indexOffset, std::uint32_t indexCount,
                              std::span<const RenderTapeRigidInstance> instances,
                              const RenderTapeConstants &constants,
                              LogicalRenderAssetRef asset) noexcept;
    bool AppendGeometryDraw(RenderTapePass pass, const LogicalGeometryAssetLease &geometry,
                            std::uint32_t vertexOffset, std::uint32_t vertexCount,
                            std::uint32_t indexOffset, std::uint32_t indexCount,
                            RenderIndexTopology topology, RenderPipelineKey pipeline,
                            const RenderTapeConstants &constants, LogicalRenderAssetRef asset,
                            std::uint32_t sourceInventoryRowId = 0,
                            const LogicalGeometryAssetLease *auxiliaryGeometry = nullptr,
                            std::uint64_t mergeKey = 0) noexcept;
    bool AppendGeometryDraw(RenderTapePass pass, const LogicalGeometryAssetLease &geometry,
                            std::uint32_t indexOffset, std::uint32_t indexCount,
                            RenderIndexTopology topology, RenderPipelineKey pipeline,
                            const RenderTapeConstants &constants, LogicalRenderAssetRef asset,
                            std::uint32_t sourceInventoryRowId = 0,
                            std::uint64_t mergeKey = 0) noexcept
    {
        return AppendGeometryDraw(pass, geometry, 0,
                                  static_cast<std::uint32_t>(geometry.vertices->size()),
                                  indexOffset, indexCount, topology, pipeline, constants, asset,
                                  sourceInventoryRowId, nullptr, mergeKey);
    }
    bool AppendTrustedGeometryDrawBatch(RenderTapePass pass,
                                        std::span<const TrustedGeometryDraw> draws) noexcept;
    std::optional<std::uint32_t> AppendBoneMatrices(
        RenderTapePass pass, std::span<const RenderTapeBoneMatrix> matrices,
        std::uint32_t sourceInventoryRowId = 0) noexcept;
    bool AppendQuadInstance(RenderTapePass pass, const LogicalGeometryAssetLease &geometry,
                            const RenderTapeQuadInstance &instance, std::uint64_t runId,
                            RenderPipelineKey pipeline, const RenderTapeConstants &constants,
                            LogicalRenderAssetRef asset,
                            std::uint32_t sourceInventoryRowId = 0) noexcept;
    bool AppendParticleInstance(RenderTapePass pass, const LogicalGeometryAssetLease &geometry,
                                const RenderTapeParticleInstance &instance, std::uint64_t runId,
                                RenderPipelineKey pipeline, const RenderTapeConstants &constants,
                                LogicalRenderAssetRef asset,
                                std::uint32_t sourceInventoryRowId = 0) noexcept;
    struct TrailSampleReservation final
    {
        std::uint32_t offset = 0;
        std::span<RenderTapeTrailSample> samples;
    };
    // The producer fills this span before another reservation or finalization.
    std::optional<TrailSampleReservation> ReserveTrailSamples(std::size_t count) noexcept;
    bool AppendTrailInstance(RenderTapePass pass, const LogicalGeometryAssetLease &geometry,
                             const RenderTapeTrailInstance &instance, std::uint64_t runId,
                             RenderPipelineKey pipeline, const RenderTapeConstants &constants,
                             LogicalRenderAssetRef asset,
                             std::uint32_t sourceInventoryRowId = 0) noexcept;
    bool AppendOwnerRequest(RenderTapePass pass, const RenderOwnerRequest &request,
                            std::uint32_t sourceInventoryRowId = 0) noexcept;

    // Appends one complete upload atomically. Entry, request, and payload
    // capacity are checked before any byte or counter is changed.
    bool AppendUploadLogicalAssetRgba8(RenderTapePass pass, LogicalRenderAssetRef destination,
                                       std::uint32_t width, std::uint32_t height,
                                       std::span<const std::byte> rgba8,
                                       RenderAssetRetention retention, RenderSamplerIntent sampler,
                                       std::uint32_t sourceInventoryRowId = 0) noexcept;

    // Replaces one rectangle in an upload already recorded for this frame.
    // This lets many text draws share one atlas upload without adding a GPU
    // copy pass for every label.
    bool PatchUploadedLogicalAssetRgba8(RenderTapePass pass, LogicalRenderAssetRef destination,
                                        std::uint32_t x, std::uint32_t y, std::uint32_t width,
                                        std::uint32_t height, std::span<const std::byte> rgba8,
                                        std::uint32_t sourceInventoryRowId = 0) noexcept;

    // Appends one FrameOnly RGBA8 upload and its dependent draw as one
    // source operation. Capacity and value validation happen before either
    // part changes tape storage, so a failed text upload/draw publishes no
    // partial upload, draw, payload bytes, or stable-order cursor.
    bool AppendUploadAndDraw(RenderTapePass pass, LogicalRenderAssetRef destination,
                             std::uint32_t width, std::uint32_t height,
                             std::span<const std::byte> rgba8, RenderAssetRetention retention,
                             RenderSamplerIntent sampler,
                             std::span<const RenderTapeVertex> vertices,
                             std::span<const std::uint32_t> indices, RenderIndexTopology topology,
                             RenderPipelineKey pipeline, const RenderTapeConstants &constants,
                             LogicalRenderAssetRef asset,
                             std::uint32_t sourceInventoryRowId = 0) noexcept;

    std::span<const RenderOwnerRequest> OwnerRequests() const noexcept
    {
        return blockOwner_ == nullptr
                   ? std::span<const RenderOwnerRequest>{}
                   : std::span<const RenderOwnerRequest>(block_->ownerRequests.data(),
                                                         block_->ownerRequestCount);
    }
    std::span<const std::byte> PayloadBytes() const noexcept
    {
        return blockOwner_ == nullptr
                   ? std::span<const std::byte>{}
                   : std::span<const std::byte>(block_->payload.data(), block_->payloadBytesUsed);
    }
    // Same atomic reservation as the RGBA8 path, with conversion performed
    // directly into the tape payload after any required growth.
    bool AppendUploadLogicalAssetRgb8(RenderTapePass pass, LogicalRenderAssetRef destination,
                                      std::uint32_t width, std::uint32_t height,
                                      std::span<const std::byte> rgb8,
                                      RenderAssetRetention retention, RenderSamplerIntent sampler,
                                      std::uint32_t sourceInventoryRowId = 0) noexcept;

    // Consumes this recording. Spent (Failed or already Finalized/Aborted)
    // returns no tape. Either way this recording is Spent() afterward.
    std::optional<SessionRenderTape> Finalize() noexcept;

    // Releases the child lease without producing a tape, returning the
    // block to the free pool once every other reference to it drops. Spent
    // afterward, same as Finalize.
    void Abort() noexcept;

  private:
    template <class Instance>
    bool AppendUnitQuadInstance(RenderTapePass pass, const LogicalGeometryAssetLease &geometry,
                                const Instance &instance, std::uint64_t runId,
                                RenderPipelineKey pipeline, const RenderTapeConstants &constants,
                                LogicalRenderAssetRef asset, std::uint32_t sourceInventoryRowId,
                                std::vector<Instance> &instances, std::size_t &instanceCount,
                                RenderTapeFailure allocationFailure) noexcept;

    friend class LegacyRenderFacade;

    // Internal storage reservation. Public AppendDraw and LegacyRenderFacade
    // validate their inputs before reaching this point.
    std::span<RenderTapeVertex> ReserveVertices(std::size_t vertexCount) noexcept;
    std::optional<DrawReservation> ReserveDraws(RenderTapePass pass, std::size_t vertexCount,
                                                std::span<const ReservedDrawRequest> requests,
                                                std::uint32_t sourceInventoryRowId) noexcept;
    std::optional<DrawReservation> ReserveSingleDraw(RenderTapePass pass, std::size_t vertexCount,
                                                     const ReservedDrawRequest &request,
                                                     std::uint32_t sourceInventoryRowId) noexcept;
    bool CommitDraws(const DrawReservation &reservation,
                     std::span<const ReservedDrawRequest> requests) noexcept;
    bool CommitSingleDraw(const DrawReservation &reservation,
                          const ReservedDrawRequest &request) noexcept;
    bool HasEntryCapacity() const noexcept
    {
        return blockOwner_ != nullptr &&
               block_->entryCount < (std::numeric_limits<std::uint32_t>::max)();
    }
    bool CanRecord() const noexcept
    {
        return blockOwner_ != nullptr && !Failed();
    }
    std::optional<std::uint64_t> NextStableOrder() noexcept;
    bool AppendUpload(RenderTapePass pass, LogicalRenderAssetRef destination, std::uint32_t width,
                      std::uint32_t height, std::span<const std::byte> source,
                      RenderAssetRetention retention, RenderSamplerIntent sampler, bool sourceIsRgb,
                      std::uint32_t sourceInventoryRowId) noexcept;

    // Ownership is checked at operation boundaries; the raw pointer avoids
    // shared_ptr accessors in every trusted per-draw write.
    std::shared_ptr<RenderTapeBlock> blockOwner_;
    RenderTapeBlock *block_ = nullptr;
    SessionId id_;
    SessionGeneration generation_;
    std::uint64_t frameSequence_;
    std::uint64_t surfaceGeneration_;
    std::uint32_t viewportWidth_;
    std::uint32_t viewportHeight_;
    std::uint64_t nextStableOrder_ = 1;
    RenderTapeFailureDiagnostics diagnostics_;
};

class CGlobalBitmap;
class SdlGpuRenderBackendTestPeer;
class SessionAdvanceResult;
class SessionWorkspace;

// Immutable application-owner input to the renderer. Construction is the
// only place where session tapes, workspace order, and catalog leases are
// joined.
class ApplicationRenderFrame final
{
  public:
    struct SessionRecord final
    {
        SessionId id;
        SessionGeneration generation;
        SessionDisplayRect destination;
        SessionRenderTape tape;
    };

    struct CompositionRecord final
    {
        SessionId id;
        SessionGeneration generation;
        SessionDisplayRect destination;
        std::uint64_t surfaceGeneration = 0;
    };

    static std::unique_ptr<ApplicationRenderFrame> TryCreate(
        std::uint64_t frameSequence, std::span<SessionAdvanceResult> results,
        const SessionWorkspace &workspace, const CGlobalBitmap &assets) noexcept;

    std::uint64_t FrameSequence() const noexcept
    {
        return frameSequence_;
    }
    std::uint64_t WorkspaceRevision() const noexcept
    {
        return workspaceRevision_;
    }
    bool WasValidatedByApplication() const noexcept
    {
        return validatedByApplication_;
    }
    SessionDisplayRect WindowRect() const noexcept
    {
        return windowRect_;
    }
    std::size_t SessionCount() const noexcept
    {
        return sessions_.size();
    }
    const SessionRecord *Session(std::size_t index) const noexcept;
    std::size_t CompositionCount() const noexcept
    {
        return compositions_.size();
    }
    const CompositionRecord *Composition(std::size_t index) const noexcept;
    std::span<const LogicalRenderAssetLease> Assets() const noexcept
    {
        return assets_;
    }
    std::span<const WorkspaceOverlayRect> OverlayRects() const noexcept
    {
        return overlayRects_;
    }
    std::span<const WorkspaceOverlayLabel> OverlayLabels() const noexcept
    {
        return overlayLabels_;
    }

  private:
    friend class SdlGpuRenderBackendTestPeer;

    std::uint64_t frameSequence_ = 0;
    std::uint64_t workspaceRevision_ = 0;
    SessionDisplayRect windowRect_;
    std::vector<SessionRecord> sessions_;
    std::vector<CompositionRecord> compositions_;
    std::vector<LogicalRenderAssetLease> assets_;
    std::vector<WorkspaceOverlayRect> overlayRects_;
    std::vector<WorkspaceOverlayLabel> overlayLabels_;
    bool validatedByApplication_ = false;
};

static_assert(std::is_trivially_copyable_v<RenderTapeVertex>);
static_assert(std::is_trivially_copyable_v<RenderTapeTerrainCell>);
static_assert(sizeof(RenderTapeTerrainCell) == 16);
static_assert(std::is_trivially_copyable_v<RenderTapeTerrainInstance>);
static_assert(sizeof(RenderTapeTerrainInstance) == 16);
static_assert(std::is_trivially_copyable_v<RenderTapeQuadInstance>);
static_assert(sizeof(RenderTapeQuadInstance) == 96);
static_assert(std::is_trivially_copyable_v<RenderTapeParticleInstance>);
static_assert(sizeof(RenderTapeParticleInstance) == 64);
static_assert(std::is_trivially_copyable_v<RenderTapeTrailSample>);
static_assert(std::is_trivially_copyable_v<RenderTapeTrailInstance>);
static_assert(sizeof(RenderTapeTrailSample) == 32);
static_assert(sizeof(RenderTapeTrailInstance) == 32);
static_assert(sizeof(std::uint32_t) <= 4);
static_assert(sizeof(RenderTapeEntry) <= 16);
static_assert(sizeof(RenderTapeDraw) <= 64);
static_assert(sizeof(RenderTapeClear) <= 56);
static_assert(std::is_trivially_copyable_v<RenderTapeConstants>);
static_assert(sizeof(LogicalRenderAssetRef) <= 16);
static_assert(sizeof(RenderOwnerRequest) <= 72);
// Application-owned pool of RenderTapeBlock storage (PLAN_P1R5.1.md sections
// 5.6, 5.8). Blocks exist only for rendered/in-flight tapes, and each block
// retains its dynamically grown storage for reuse by a later frame.
class ApplicationRenderTapeStorage final
{
  private:
    struct PreparedSessionIdentity final
    {
        SessionId id;
        SessionGeneration generation;
        std::uint64_t surfaceGeneration = 0;
        std::uint32_t viewportWidth = 0;
        std::uint32_t viewportHeight = 0;
    };

  public:
    class FrameLease final
    {
      public:
        FrameLease(FrameLease &&) noexcept = default;
        FrameLease &operator=(FrameLease &&) noexcept = default;
        FrameLease(const FrameLease &) = delete;
        FrameLease &operator=(const FrameLease &) = delete;

        // Acquires one child block lease for one render-required session,
        // tagging it with the exact identity/surface given. Fails (returns
        // nullopt) once every block reserved by PrepareFrame for this frame
        // has already been acquired, on zero dimensions, or when the exact
        // identity/viewport was not prepared by the application frame plan.
        std::optional<SessionRenderTapeRecording> AcquireChildBlock(
            SessionId id, SessionGeneration generation, std::uint64_t surfaceGeneration,
            std::uint32_t viewportWidth, std::uint32_t viewportHeight) noexcept;

      private:
        friend class ApplicationRenderTapeStorage;

        FrameLease(std::vector<std::shared_ptr<RenderTapeBlock>> reserved,
                   std::vector<PreparedSessionIdentity> preparedIdentities,
                   std::uint64_t frameSequence) noexcept
            : reserved_(std::move(reserved)), preparedIdentities_(std::move(preparedIdentities)),
              frameSequence_(frameSequence)
        {
        }

        std::vector<std::shared_ptr<RenderTapeBlock>> reserved_;
        std::vector<PreparedSessionIdentity> preparedIdentities_;
        std::uint64_t frameSequence_;
        std::size_t nextIndex_ = 0;
    };

    ApplicationRenderTapeStorage() noexcept = default;
    ApplicationRenderTapeStorage(const ApplicationRenderTapeStorage &) = delete;
    ApplicationRenderTapeStorage &operator=(const ApplicationRenderTapeStorage &) = delete;

    // Copies the exact render-required identities from the validated plan
    // into fixed frame-lease slots, materializes every block before recording,
    // and returns a lease that can only vend those prepared identities.
    std::optional<FrameLease> PrepareFrame(const ApplicationFramePlan &plan) noexcept;

    std::size_t ResidentBlockCount() const noexcept
    {
        return blocks_.size();
    }
    std::size_t StorageBytes() const noexcept
    {
        std::size_t bytes = blocks_.capacity() * sizeof(blocks_[0]);
        for (const auto &block : blocks_)
        {
            bytes += block->StorageBytes();
        }
        return bytes;
    }

  private:
    friend class FrameLease;

    static bool PrepareIdentities(const ApplicationFramePlan &plan,
                                  std::vector<PreparedSessionIdentity> &identities) noexcept;

    std::vector<std::shared_ptr<RenderTapeBlock>> blocks_;
    std::optional<std::uint64_t> lastPreparedFrameSequence_;
};

enum class CompositorFrameResult
{
    Submitted,
    DroppedNoDrawable,
    Rejected,
    DeviceLost,
};

enum class CompositorRetireResult
{
    Ready,
    Pending,
    DeviceLost,
};

enum class RenderTimingPass : std::uint8_t
{
    Terrain,
    Objects,
    Characters,
    Items,
    Effects,
    Other,
    SessionTarget,
    Composition,
    TotalFrame,
    Upload,
    Count,
};

struct RenderTimingToken final
{
    static constexpr std::uint32_t InvalidSlot = (std::numeric_limits<std::uint32_t>::max)();

    std::uint32_t slot = InvalidSlot;
    std::uint32_t generation = 0;

    bool IsValid() const noexcept
    {
        return slot != InvalidSlot && generation != 0;
    }
};

struct RenderTimingOriginMetrics final
{
    std::uint64_t origin = 0;
    std::array<std::uint64_t, static_cast<std::size_t>(RenderTimingPass::Count)> nanoseconds{};
    std::array<std::uint64_t, static_cast<std::size_t>(RenderTimingPass::Count)> samples{};
};

struct RenderTimingMetrics final
{
    static constexpr std::size_t MaximumOrigins = 64;

    bool supported = false;
    std::array<std::uint64_t, static_cast<std::size_t>(RenderTimingPass::Count)> nanoseconds{};
    std::array<std::uint64_t, static_cast<std::size_t>(RenderTimingPass::Count)> samples{};
    std::array<RenderTimingOriginMetrics, MaximumOrigins> origins{};
    std::uint64_t droppedQueries = 0;
    std::uint64_t invalidTokens = 0;
    std::uint64_t wrongThreadCalls = 0;
};

// Numeric source-language values accepted by the exact-session tape recorder.
// These are data tags only; no native graphics API is included or called.
inline constexpr unsigned int GL_ZERO = 0;
inline constexpr unsigned int GL_FALSE = 0;
inline constexpr unsigned int GL_POINTS = 0x0000;
inline constexpr unsigned int GL_TRUE = 1;
inline constexpr unsigned int GL_ONE = 1;
inline constexpr unsigned int GL_LINES = 0x0001;
inline constexpr unsigned int GL_LINE_LOOP = 0x0002;
inline constexpr unsigned int GL_LINE_STRIP = 0x0003;
inline constexpr unsigned int GL_TRIANGLES = 0x0004;
inline constexpr unsigned int GL_TRIANGLE_STRIP = 0x0005;
inline constexpr unsigned int GL_TRIANGLE_FAN = 0x0006;
inline constexpr unsigned int GL_QUADS = 0x0007;
inline constexpr unsigned int GL_POLYGON = 0x0009;
inline constexpr unsigned int GL_DEPTH_BUFFER_BIT = 0x00000100;
inline constexpr unsigned int GL_ADD = 0x0104;
inline constexpr unsigned int GL_LESS = 0x0201;
inline constexpr unsigned int GL_LEQUAL = 0x0203;
inline constexpr unsigned int GL_GREATER = 0x0204;
inline constexpr unsigned int GL_ALWAYS = 0x0207;
inline constexpr unsigned int GL_SRC_COLOR = 0x0300;
inline constexpr unsigned int GL_ONE_MINUS_SRC_COLOR = 0x0301;
inline constexpr unsigned int GL_SRC_ALPHA = 0x0302;
inline constexpr unsigned int GL_ONE_MINUS_SRC_ALPHA = 0x0303;
inline constexpr unsigned int GL_STENCIL_BUFFER_BIT = 0x00000400;
inline constexpr unsigned int GL_FRONT = 0x0404;
inline constexpr unsigned int GL_BACK = 0x0405;
inline constexpr unsigned int GL_CW = 0x0900;
inline constexpr unsigned int GL_CCW = 0x0901;
inline constexpr unsigned int GL_CULL_FACE = 0x0B44;
inline constexpr unsigned int GL_LIGHTING = 0x0B50;
inline constexpr unsigned int GL_FOG = 0x0B60;
inline constexpr unsigned int GL_FOG_DENSITY = 0x0B62;
inline constexpr unsigned int GL_FOG_START = 0x0B63;
inline constexpr unsigned int GL_FOG_END = 0x0B64;
inline constexpr unsigned int GL_FOG_MODE = 0x0B65;
inline constexpr unsigned int GL_FOG_COLOR = 0x0B66;
inline constexpr unsigned int GL_DEPTH_TEST = 0x0B71;
inline constexpr unsigned int GL_STENCIL_TEST = 0x0B90;
inline constexpr unsigned int GL_MODELVIEW_MATRIX = 0x0BA6;
inline constexpr unsigned int GL_ALPHA_TEST = 0x0BC0;
inline constexpr unsigned int GL_BLEND = 0x0BE2;
inline constexpr unsigned int GL_TEXTURE_2D = 0x0DE1;
inline constexpr unsigned int GL_UNSIGNED_BYTE = 0x1401;
inline constexpr unsigned int GL_MODELVIEW = 0x1700;
inline constexpr unsigned int GL_PROJECTION = 0x1701;
inline constexpr unsigned int GL_TEXTURE = 0x1702;
inline constexpr unsigned int GL_RGB = 0x1907;
inline constexpr unsigned int GL_LINE = 0x1B01;
inline constexpr unsigned int GL_FILL = 0x1B02;
inline constexpr unsigned int GL_SMOOTH = 0x1D01;
inline constexpr unsigned int GL_KEEP = 0x1E00;
inline constexpr unsigned int GL_INCR = 0x1E02;
inline constexpr unsigned int GL_DECR = 0x1E03;
inline constexpr unsigned int GL_MODULATE = 0x2100;
inline constexpr unsigned int GL_TEXTURE_ENV_MODE = 0x2200;
inline constexpr unsigned int GL_TEXTURE_ENV = 0x2300;
inline constexpr unsigned int GL_NEAREST = 0x2600;
inline constexpr unsigned int GL_LINEAR = 0x2601;
inline constexpr unsigned int GL_TEXTURE_MAG_FILTER = 0x2800;
inline constexpr unsigned int GL_TEXTURE_MIN_FILTER = 0x2801;
inline constexpr unsigned int GL_TEXTURE_WRAP_S = 0x2802;
inline constexpr unsigned int GL_TEXTURE_WRAP_T = 0x2803;
inline constexpr unsigned int GL_CLAMP = 0x2900;
inline constexpr unsigned int GL_REPEAT = 0x2901;
inline constexpr unsigned int GL_COLOR_BUFFER_BIT = 0x00004000;
inline constexpr unsigned int GL_VERTEX_ARRAY = 0x8074;
inline constexpr unsigned int GL_NORMAL_ARRAY = 0x8075;
inline constexpr unsigned int GL_COLOR_ARRAY = 0x8076;
inline constexpr unsigned int GL_TEXTURE_COORD_ARRAY = 0x8078;
inline constexpr unsigned int GL_ALL_ATTRIB_BITS = 0x000fffff;
inline constexpr unsigned int GL_CLIENT_ALL_ATTRIB_BITS = 0xffffffffU;
inline constexpr unsigned int GL_CLAMP_TO_EDGE = 0x812F;

// The exact-session CPU recorder (PLAN_P1R5.1.md section 5.7). One instance
// is owned directly by each SessionRenderUnit -- no global, TLS, focus, slot,
// lookup, macro, or service locator. It never calls OpenGL, WGL, or SDL GPU;
// every typed method below only shapes CPU values and appends them to the
// SessionRenderTapeRecording acquired for the current frame.
// State machine (section 5.7): not holding a recording IS Idle; BeginFrame
// moves to Recording by taking ownership of the caller's exact child block
// lease; Failed is SessionRenderTapeRecording::Failed(); Finalize's
// successful return is the diagram's momentary Sealed step, immediately
// followed by Idle. Failure diagnostics live entirely on the held recording
// (SessionRenderTapeRecording::Diagnostics()), so this class does not
// duplicate a second failure-latch.
class LegacyRenderFacade final
{
    friend class SessionRenderUnit;

  public:
    explicit LegacyRenderFacade(SessionId ownerId) noexcept
        : ownerId_(ownerId), generation_(SessionGeneration::TryCreate(1).value())
    {
    }

    // ---- Frame / pass lifecycle -------------------------------------

    // Takes ownership of `recording` only when its own identity/surface
    // exactly match the caller's; a mismatch is a caller bug and leaves this
    // facade Idle (the mismatched recording is destroyed, releasing its
    // block). Resets every per-frame flag; matrix/attribute stacks and
    // latched attributes are authoritatively reset by the first BeginPass.
    bool BeginFrame(SessionId id, SessionGeneration generation, std::uint64_t frameSequence,
                    std::uint64_t surfaceGeneration, std::uint32_t viewportWidth,
                    std::uint32_t viewportHeight, SessionRenderTapeRecording recording) noexcept;

    // Seeds matrices, constants, and latched attributes to the exact R5.1
    // pass seed (section 5.4), copying fog from `fog`. Fails if not
    // Recording, already Failed, or a pass is already open.
    bool BeginPass(RenderTapePass pass, const SessionFogPassConstants &fog) noexcept;

    // Requires no open primitive and every matrix/attribute stack back at
    // its pass-start depth.
    bool EndPass() noexcept;
    std::size_t PaletteAppendCount() const noexcept
    {
        return paletteAppends_;
    }
    std::size_t PaletteReuseCount() const noexcept
    {
        return paletteReuses_;
    }

    // Consumes this facade's recording. Requires at least one completed
    // pass, no open pass, and every tape invariant to hold. Idle afterward
    // either way.
    std::optional<SessionRenderTape> Finalize() noexcept;

    // Releases the child lease without producing a tape. Idle afterward.
    void Abort() noexcept;

    bool IsRecording() const noexcept
    {
        return recording_.has_value();
    }
    bool MatchesViewport(std::uint32_t width, std::uint32_t height) const noexcept
    {
        return IsRecording() && viewportWidth_ == width && viewportHeight_ == height;
    }
    std::uint64_t FrameSequence() const noexcept
    {
        return frameSequence_;
    }
    SessionGeneration Generation() const noexcept
    {
        return generation_;
    }
    bool Failed() const noexcept
    {
        return recording_.has_value() && recording_->Failed();
    }
    RenderTapeFailureDiagnostics Diagnostics() const noexcept
    {
        return recording_.has_value() ? recording_->Diagnostics() : RenderTapeFailureDiagnostics{};
    }

    // A legacy operation with no portable tape semantic fails the complete
    // recording before any native fallback can occur.
    bool RejectUnsupported(std::source_location location = std::source_location::current()) noexcept
    {
        LatchFacadeFailure(RenderTapeFailure::UnsupportedSemantic, location);
        return false;
    }

    // ---- Immediate mode (section 5.3, 5.9) ---------------------------

    bool Begin(LegacyPrimitive primitive) noexcept;
    bool End() noexcept;
    bool Vertex2(float x, float y) noexcept;
    bool Vertex3(float x, float y, float z) noexcept;
    bool Color3(float r, float g, float b) noexcept;
    bool Color4(float r, float g, float b, float a) noexcept;
    bool Color3Ub(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept;
    bool Color4Ub(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) noexcept;
    bool Normal3(float x, float y, float z) noexcept;
    bool TexCoord2(float u, float v) noexcept;

    // ---- Typed client arrays (section 5.7, 5.9) ----------------------

    bool SetClientArray(RenderClientArraySemantic semantic, std::span<const std::byte> storage,
                        std::uint32_t componentCount, RenderClientArrayScalarType scalarType,
                        std::int32_t stride, bool normalized) noexcept;
    void EnableClientArray(RenderClientArraySemantic semantic) noexcept;
    void DisableClientArray(RenderClientArraySemantic semantic) noexcept;
    bool DrawArrays(LegacyPrimitive primitive, std::int32_t first, std::int32_t count) noexcept;
    template <typename Writer> bool WriteTriangles(std::size_t vertexCount, Writer writer) noexcept
    {
        if (!CanMutateState())
        {
            LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
            return false;
        }
        if (vertexCount % 3 != 0)
        {
            LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
            return false;
        }
        if (vertexCount == 0)
        {
            return true;
        }
        if (!recording_->HasEntryCapacity())
        {
            LatchFacadeFailure(RenderTapeFailure::EntryCapacity);
            return false;
        }
        const std::span<RenderTapeVertex> vertices = recording_->ReserveVertices(vertexCount);
        if (vertices.size() != vertexCount)
        {
            LatchFacadeFailure(RenderTapeFailure::VertexCapacity);
            return false;
        }
        writer(vertices);
        return EmitPrimitiveDraw(LegacyPrimitive::Triangles, vertices);
    }
    template <typename Writer>
    bool WriteTriangleFan(std::size_t vertexCount, Writer writer) noexcept
    {
        if (!CanMutateState())
        {
            LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
            return false;
        }
        if (vertexCount != 0 && vertexCount < 3)
        {
            LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
            return false;
        }
        if (vertexCount == 0)
        {
            return true;
        }
        if (vertexCount == 4 && !UsesPolygonLineMode())
        {
            std::optional<IndexedTriangleReservation> reservation = ReserveIndexedTriangles(4, 6);
            if (!reservation.has_value())
            {
                return false;
            }
            writer(reservation->draw.vertices);
            std::uint32_t *index = reservation->draw.indices.data();
            *index++ = 0;
            *index++ = 1;
            *index++ = 2;
            *index++ = 0;
            *index++ = 2;
            *index = 3;
            return CommitIndexedTriangles(*reservation);
        }
        if (!recording_->HasEntryCapacity())
        {
            LatchFacadeFailure(RenderTapeFailure::EntryCapacity);
            return false;
        }
        const std::span<RenderTapeVertex> vertices = recording_->ReserveVertices(vertexCount);
        if (vertices.size() != vertexCount)
        {
            LatchFacadeFailure(RenderTapeFailure::VertexCapacity);
            return false;
        }
        writer(vertices);
        return EmitPrimitiveDraw(LegacyPrimitive::TriangleFan, vertices);
    }
    template <typename Writer>
    bool WriteIndexedTriangles(std::size_t vertexCount, std::size_t indexCount,
                               Writer writer) noexcept
    {
        std::optional<IndexedTriangleReservation> reservation =
            ReserveIndexedTriangles(vertexCount, indexCount);
        if (!reservation.has_value())
        {
            return false;
        }
        writer(reservation->draw.vertices, reservation->draw.indices);
        return CommitIndexedTriangles(*reservation);
    }
    bool DrawGeometry(const LogicalGeometryAssetLease &geometry, std::uint32_t indexOffset,
                      std::uint32_t indexCount) noexcept;
    bool BuildTrustedGeometryDraw(const LogicalGeometryAssetLease &geometry,
                                  std::uint32_t indexOffset, std::uint32_t indexCount,
                                  TrustedGeometryDraw &draw) noexcept;
    bool AppendTrustedGeometryDrawBatch(std::span<const TrustedGeometryDraw> draws) noexcept;
    std::uint64_t BeginGrassGeometryRun(const RenderTapeGrassConstants &grass) noexcept;
    bool DrawTerrainInstances(const LogicalGeometryAssetLease &geometry,
                              std::uint32_t instanceOffset, std::uint32_t instanceCount,
                              const RenderTapeTerrainConstants &terrain) noexcept;
    bool DrawGrassGeometry(const LogicalGeometryAssetLease &geometry, std::uint32_t indexOffset,
                           std::uint32_t indexCount, std::uint64_t runId) noexcept;
    std::uint64_t BeginQuadInstanceRun() noexcept;
    std::optional<SessionRenderTapeRecording::TrailSampleReservation> ReserveTrailSamples(
        std::size_t count) noexcept;
    std::uint64_t BeginTrailInstanceRun(const RenderTapeTrailConstants &trail) noexcept;
    bool DrawTrailInstance(const LogicalGeometryAssetLease &geometry,
                           const RenderTapeTrailInstance &instance, std::uint64_t runId,
                           bool blur = false) noexcept;
    std::uint64_t BeginParticleInstanceRun(const float camera[3][4]) noexcept;
    bool CanContinueQuadInstanceRun(std::uint64_t runId) const noexcept;
    bool DrawParticleInstance(const LogicalGeometryAssetLease &geometry,
                              const RenderTapeParticleInstance &instance,
                              std::uint64_t runId) noexcept;
    std::uint64_t BeginTextGlyphRun(LogicalRenderAssetRef asset) noexcept;
    bool DrawQuadInstance(const LogicalGeometryAssetLease &geometry,
                          const RenderTapeQuadInstance &instance, std::uint64_t runId) noexcept;
    bool DrawSpriteInstance(const LogicalGeometryAssetLease &geometry,
                            const RenderTapeQuadInstance &instance, std::uint64_t runId) noexcept;
    std::optional<std::uint32_t> AppendBoneMatrices(std::span<const RenderTapeBoneMatrix> matrices,
                                                    bool stablePose = false) noexcept;
    bool DrawBmdGeometry(const LogicalGeometryAssetLease &geometry, std::uint32_t vertexOffset,
                         std::uint32_t vertexCount, std::uint32_t indexOffset,
                         std::uint32_t indexCount, const RenderTapeBmdConstants &bmd) noexcept;
    bool DrawRigidInstances(const LogicalGeometryAssetLease &geometry, std::uint32_t vertexOffset,
                            std::uint32_t vertexCount, std::uint32_t indexOffset,
                            std::uint32_t indexCount,
                            std::span<const RenderTapeRigidInstance> instances,
                            const RenderTapeBmdConstants &bmd) noexcept;
    bool DrawBmdShadowGeometry(const LogicalGeometryAssetLease &geometry,
                               const LogicalGeometryAssetLease &terrain, std::uint32_t vertexOffset,
                               std::uint32_t vertexCount, std::uint32_t indexOffset,
                               std::uint32_t indexCount,
                               const RenderTapeBmdConstants &bmd) noexcept;
    bool Sphere(float radius, std::uint32_t slices, std::uint32_t stacks) noexcept;

    // ---- CPU matrix stack (section 5.4, 5.9) -------------------------

    bool MatrixMode(LegacyMatrixMode mode) noexcept;
    bool LoadIdentity() noexcept;
    bool LoadMatrix(const std::array<float, 16> &matrix) noexcept;
    bool MultMatrix(const std::array<float, 16> &matrix) noexcept;
    bool Translate(float x, float y, float z) noexcept;
    bool Rotate(float degrees, float x, float y, float z) noexcept;
    bool Scale(float x, float y, float z) noexcept;
    bool Ortho(float left, float right, float bottom, float top, float zNear, float zFar) noexcept;
    bool Perspective(float fovYDegrees, float aspect, float zNear, float zFar) noexcept;
    bool PushMatrix() noexcept;
    bool PopMatrix() noexcept;

    // ---- Server/client attribute stack (section 5.4) -----------------

    bool PushAttrib() noexcept;
    bool PopAttrib() noexcept;
    bool PushClientAttrib() noexcept;
    bool PopClientAttrib() noexcept;

    // ---- Individual finite state setters (section 5.9) ---------------

    // Common material changes check recording state once for the whole operation.
    bool SetOpaqueState() noexcept;
    bool SetAlphaTestState(bool depthWrite) noexcept;
    bool SetAdditiveState() noexcept;

    bool SetTextureEnable(bool enable) noexcept;
    bool SetDepthTestEnable(bool enable) noexcept;
    bool SetDepthWriteEnable(bool enable) noexcept;
    bool SetDepthFunc(RenderCompareFunction compare) noexcept;
    bool SetCullEnable(bool enable) noexcept;
    bool SetCullFace(RenderCullFace face) noexcept;
    bool SetFrontFace(RenderFrontFace face) noexcept;
    bool SetBlendEnable(bool enable) noexcept;
    bool SetBlendFunc(RenderBlendFactor source, RenderBlendFactor destination) noexcept;
    bool SetAlphaTestEnable(bool enable) noexcept;
    bool SetAlphaFunc(RenderCompareFunction compare, float reference) noexcept;
    bool SetFogEnable(bool enable) noexcept;
    bool SetFogMode(RenderFogMode mode) noexcept;
    bool SetFogColor(const std::array<float, 4> &color) noexcept;
    bool SetFogRange(float start, float end) noexcept;
    bool SetFogDensity(float density) noexcept;
    bool SetLightingEnable(bool enable) noexcept;
    bool SetColorMask(bool r, bool g, bool b, bool a) noexcept;
    bool SetStencilEnable(bool enable) noexcept;
    bool SetStencilFunc(RenderCompareFunction compare, std::uint32_t reference,
                        std::uint32_t readMask) noexcept;
    bool SetStencilOp(RenderStencilOperation onFail, RenderStencilOperation onDepthFail,
                      RenderStencilOperation onPass) noexcept;
    bool SetTextureEnvironment(RenderTextureEnvironment environment) noexcept;
    bool SetShadeMode(RenderShadeMode mode) noexcept;
    bool SetLineWidth(float width) noexcept;
    bool SetPolygonMode(RenderCullFace face, RenderPolygonMode mode) noexcept;

    // ---- Viewport, scissor, clear (section 5.5, 5.9) -----------------

    bool SetViewport(RenderTapeRect rect) noexcept;
    bool SetScissorEnable(bool enable) noexcept;
    bool SetScissor(RenderTapeRect rect) noexcept;
    bool SetClearColor(const std::array<float, 4> &color) noexcept;
    bool SetClearDepth(float depth) noexcept;
    bool SetClearStencilValue(std::uint32_t stencil) noexcept;
    bool Clear(bool color, bool depth, bool stencil) noexcept;
    // Full-target stencil reset; preserves the caller's pending render state.
    bool ClearStencil(std::uint32_t value) noexcept;

    // ---- Logical textures and owner requests (section 5.5, 5.9) ------

    // Full-image define/redefine (glTexImage2D). `pixels` is tightly packed
    // per `format`; RGB is expanded to RGBA8 with alpha 1.0F before it is
    // copied into the tape payload. glTexSubImage2D is out of scope here:
    // applying a subrectangle onto a prior immutable revision needs
    // CGlobalBitmap::TryLease, which Task 5 adds.
    // ponytail: full-image redefine only; add glTexSubImage2D once
    // CGlobalBitmap::TryLease lands in Task 5.
    bool DefineTexture2D(LogicalRenderAssetRef destination, std::uint32_t width,
                         std::uint32_t height, std::span<const std::byte> pixels,
                         LegacyPixelFormat format, RenderAssetRetention retention,
                         RenderSamplerIntent sampler) noexcept;
    bool AppendFrameOnlyTextureQuad(LogicalRenderAssetRef destination, std::uint32_t width,
                                    std::uint32_t height, std::span<const std::byte> rgba8,
                                    RenderSamplerIntent sampler,
                                    const std::array<std::array<float, 2>, 4> &vertices) noexcept;
    void BindTexture(LogicalRenderAssetRef asset) noexcept;
    bool CopyTargetToLogicalTexture(SessionId sourceSession, SessionGeneration sourceGeneration,
                                    std::uint64_t sourceSurfaceGeneration,
                                    RenderTapeRect sourceRect,
                                    LogicalRenderAssetRef destination) noexcept;
    bool DownloadTargetRgba8(SessionId sourceSession, SessionGeneration sourceGeneration,
                             std::uint64_t sourceSurfaceGeneration,
                             std::uint64_t sourceFrameSequence, RenderTapeRect rect,
                             bool verticallyFlipped, std::uint64_t requestId) noexcept;

    // ---- CPU state queries needed by preserved game logic (5.7) ------

    const std::array<float, 16> &CurrentMatrix(LegacyMatrixMode mode) const noexcept;
    const std::array<float, 4> &CurrentColor() const noexcept
    {
        return pendingConstants_.color;
    }
    const std::array<float, 3> &CurrentNormal() const noexcept
    {
        return pendingConstants_.normal;
    }
    const std::array<float, 2> &CurrentTextureCoordinate() const noexcept
    {
        return currentTexCoord_;
    }
    bool UsesPolygonLineMode() const noexcept
    {
        return pendingConstants_.polygonFrontMode == RenderPolygonMode::Line;
    }
    RenderTapeRect Viewport() const noexcept
    {
        return pendingConstants_.viewport;
    }

  private:
    struct IndexedTriangleReservation final
    {
        SessionRenderTapeRecording::DrawReservation draw;
        std::array<SessionRenderTapeRecording::ReservedDrawRequest, 1> requests;
    };

    // One semantic's client-array binding (section 5.7): a non-owning view
    // over caller-owned storage plus the layout needed to read it. `enabled`
    // is tracked separately in clientArrayEnabled_ so PushClientAttrib can
    // snapshot both together without duplicating the descriptor array.
    struct ClientArrayBinding final
    {
        std::span<const std::byte> storage;
        std::uint32_t componentCount = 0;
        RenderClientArrayScalarType scalarType = RenderClientArrayScalarType::Float;
        std::int32_t stride = 0;
        bool normalized = false;
    };

    struct ClientAttribSnapshot final
    {
        std::array<ClientArrayBinding, 4> arrays;
        std::array<bool, 4> enabled{};
    };

    static constexpr std::size_t ModelViewStackDepth = 32;
    static constexpr std::size_t ProjectionStackDepth = 3;
    static constexpr std::size_t TextureStackDepth = 2;
    static constexpr std::size_t ServerAttribStackDepth = 16;
    static constexpr std::size_t ClientAttribStackDepth = 16;

    void ResetFrameState() noexcept;
    void ResetPassState(const SessionFogPassConstants &fog) noexcept;
    // True once a pass is open and recording hasn't failed -- what vertex
    // attribute latches (Color/Normal/TexCoord) require; real GL allows
    // those between Begin/End too, unlike state/matrix mutation.
    bool CanRecordInPass() const noexcept;
    // CanRecordInPass() plus "no primitive currently open" -- what every
    // state/matrix mutator and DrawArrays/Begin require.
    bool CanMutateState() const noexcept;
    std::span<std::array<float, 16>> CurrentMatrixStack() noexcept;
    std::span<const std::array<float, 16>> CurrentMatrixStack() const noexcept;
    std::size_t &CurrentMatrixDepth() noexcept;
    std::size_t CurrentMatrixDepth() const noexcept;
    std::array<float, 16> &ActiveMatrix() noexcept;
    bool ApplyMatrix(const std::array<float, 16> &next) noexcept;
    void AdvanceDrawStateGeneration() noexcept;
    template <typename Value> void SetDrawStateValue(Value &current, const Value &next) noexcept
    {
        if (current == next)
            return;
        current = next;
        AdvanceDrawStateGeneration();
    }
    void LatchFacadeFailure(
        RenderTapeFailure failure,
        std::source_location location = std::source_location::current()) noexcept;
    static std::uint32_t SourceRow(std::source_location location) noexcept
    {
        return location.line();
    }

    bool EmitPrimitiveDraw(LegacyPrimitive primitive,
                           std::span<const RenderTapeVertex> vertices) noexcept;
    std::optional<IndexedTriangleReservation> ReserveIndexedTriangles(
        std::size_t vertexCount, std::size_t indexCount) noexcept;
    bool CommitIndexedTriangles(const IndexedTriangleReservation &reservation) noexcept;
    bool CountClassifiedArrayTriangles(LegacyPrimitive primitive, std::int32_t first,
                                       std::int32_t count, std::size_t &fillIndexCount,
                                       std::size_t &lineIndexCount) noexcept;
    bool EmitExpandedTriangles(LegacyPrimitive primitive,
                               std::span<const RenderTapeVertex> vertices) noexcept;
    RenderTapeVertex LatchVertex(const std::array<float, 4> &position) const noexcept;
    bool ReadArrayVertices(std::int32_t first, std::int32_t count,
                           std::span<RenderTapeVertex> outVertices) noexcept;
    // Adjacent mesh passes can reuse an immutable palette in this recording.
    // Other requests append normally; no dictionary or per-frame node churn.
    std::span<const RenderTapeBoneMatrix> lastPalette_;
    std::uint32_t lastPaletteOffset_ = 0;
    std::size_t paletteAppends_ = 0;
    std::size_t paletteReuses_ = 0;
    SessionId ownerId_;
    std::optional<SessionRenderTapeRecording> recording_;
    SessionGeneration generation_;
    std::uint64_t frameSequence_ = 0;
    std::uint64_t surfaceGeneration_ = 0;
    std::uint32_t viewportWidth_ = 0;
    std::uint32_t viewportHeight_ = 0;

    bool passOpen_ = false;
    bool anyPassCompleted_ = false;
    RenderTapePass currentPass_ = RenderTapePass::Terrain;

    bool primitiveOpen_ = false;
    LegacyPrimitive currentPrimitive_ = LegacyPrimitive::Points;
    std::vector<RenderTapeVertex> immediateVertices_;
    std::array<float, 2> currentTexCoord_{0.0F, 0.0F};
    std::uint64_t nextQuadInstanceRunId_ = 1;
    std::uint64_t activeQuadInstanceRunId_ = 0;
    std::uint64_t quadInstanceRunStateGeneration_ = 0;
    RenderTapeConstants quadInstanceRunConstants_;
    LogicalRenderAssetRef quadInstanceRunTexture_;

    // Never empty, even before the first BeginPass: CurrentMatrix() is a
    // public query with no "Idle" guard, so .back() must always be safe.
    LegacyMatrixMode matrixMode_ = LegacyMatrixMode::ModelView;
    std::array<std::array<float, 16>, ModelViewStackDepth> modelView_{RenderTapeIdentityMatrix4x4};
    std::array<std::array<float, 16>, ProjectionStackDepth> projection_{
        RenderTapeIdentityMatrix4x4};
    std::array<std::array<float, 16>, TextureStackDepth> texture_{RenderTapeIdentityMatrix4x4};
    std::size_t modelViewDepth_ = 1;
    std::size_t projectionDepth_ = 1;
    std::size_t textureDepth_ = 1;

    std::array<RenderTapeConstants, ServerAttribStackDepth> attribStack_{};
    std::size_t attribDepth_ = 0;
    std::array<ClientArrayBinding, 4> clientArrays_;
    std::array<bool, 4> clientArrayEnabled_{};
    std::array<ClientAttribSnapshot, ClientAttribStackDepth> clientAttribStack_{};
    std::size_t clientAttribDepth_ = 0;

    RenderTapeConstants pendingConstants_;
    std::uint64_t drawStateGeneration_ = 1;
    std::array<float, 4> pendingClearColor_{0.0F, 0.0F, 0.0F, 0.0F};
    float pendingClearDepth_ = 1.0F;
    std::uint32_t pendingClearStencil_ = 0;
    LogicalRenderAssetRef currentTexture_;
};

//Character Buff

//TheBuffInfo

//TheBuffTimeControl

//TheBuffStateValueControl

inline unsigned long RGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    return (r) + (g << 8) + (b << 16) + (a << 24);
}
inline unsigned char GetAlpha(unsigned long rgba)
{
    return ((rgba) >> 24);
}
inline unsigned char GetRed(unsigned long rgba)
{
    return ((rgba) & 0xff);
}
inline unsigned char GetGreen(unsigned long rgba)
{
    return (((rgba) >> 8) & 0xff);
}
inline unsigned char GetBlue(unsigned long rgba)
{
    return (((rgba) >> 16) & 0xff);
}
