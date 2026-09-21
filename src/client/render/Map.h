#pragma once
#include "render/FrameTape.h"
#include "support/CoreMath.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

class SessionFogPassConstants final
{
  public:
    SessionFogPassConstants() noexcept = default;

    static std::optional<SessionFogPassConstants> TryCreate(
        bool enabled, float viewFar, std::span<const float, 4> color) noexcept
    {
        const float start = viewFar;
        const float end = viewFar * 1.25F;
        if (!std::isfinite(viewFar) || viewFar <= 0.0F || !std::isfinite(start) ||
            !std::isfinite(end) || end <= start ||
            !std::all_of(color.begin(), color.end(),
                         [](float value) { return std::isfinite(value); }))
        {
            return std::nullopt;
        }

        SessionFogPassConstants result;
        result.enabled_ = enabled;
        result.start_ = start;
        result.end_ = end;
        std::copy(color.begin(), color.end(), result.color_.begin());
        return result;
    }

    bool Matches(bool enabled, float viewFar, std::span<const float, 4> color) const noexcept
    {
        const auto current = TryCreate(enabled, viewFar, color);
        return current.has_value() && current->enabled_ == enabled_ && current->start_ == start_ &&
               current->end_ == end_ && current->color_ == color_;
    }

    bool Enabled() const noexcept
    {
        return enabled_;
    }
    float Start() const noexcept
    {
        return start_;
    }
    float End() const noexcept
    {
        return end_;
    }
    const std::array<float, 4> &Color() const noexcept
    {
        return color_;
    }

  private:
    bool enabled_ = false;
    float start_ = 0.0F;
    float end_ = 1.0F;
    std::array<float, 4> color_{};
};

class ApplicationKeeper;
class SharedAllocationCounter;
class SessionBitmapView;
struct TerrainSharedGeometry;

enum class TerrainGeometryMaterialPass : std::uint8_t
{
    Base,
    Alpha,
    OceanBlend,
    AfterAlphaTest,
    AfterAlphaBlend,
};

struct TerrainGeometryDraw final
{
    int texture = 0;
    TerrainGeometryMaterialPass pass = TerrainGeometryMaterialPass::Base;
    std::uint32_t instanceOffset = 0;
    std::uint32_t instanceCount = 0;
    std::uint32_t finalInstance = 0;
    std::array<float, 2> uvScale{};
    bool water = false;
};

struct TerrainGeometryTile final
{
    std::array<TerrainGeometryDraw, 3> draws;
    TerrainGeometryDraw grassDraw;
    std::uint8_t drawCount = 0;
    bool hasGrass = false;
};

struct TerrainGeometryBlock final
{
    std::vector<TerrainGeometryDraw> draws;
    std::vector<TerrainGeometryDraw> afterDraws;
    std::vector<TerrainGeometryDraw> grassDraws;
    TerrainGeometryDraw finalDraw;
    TerrainGeometryDraw finalGrassDraw;
};

class TerrainGeometryCache final
{
  public:
    bool PrepareOnOwner(ApplicationKeeper &application, SessionId sessionId,
                        SessionGeneration generation,
                        std::atomic<std::uint64_t> &nextGeometryRevision, int world,
                        float specialHeight, const float *heights, const unsigned char *layer1,
                        const unsigned char *layer2, const float *layerAlpha,
                        const float (*lights)[3], const WORD *walls, bool pkField,
                        bool doppelGanger2, bool doppelGanger3, const SessionBitmapView &bitmaps,
                        float *grassTexture = nullptr, std::uint64_t contentRevision = 0) noexcept;
    void Invalidate() noexcept;

    const LogicalGeometryAssetLease &Lease() const noexcept
    {
        return lease_;
    }
    void SetLightSnapshot(std::shared_ptr<const TerrainLightSnapshot> light) noexcept;
    const TerrainGeometryTile &Tile(int x, int y) const noexcept;
    const TerrainGeometryBlock &Block(int x, int y) const noexcept;
    std::size_t StorageBytes(SharedAllocationCounter &allocations) const noexcept;

  private:
    int world_ = -1;
    std::uint64_t contentRevision_ = 0;
    float specialHeight_ = 0.0F;
    bool pkField_ = false;
    bool doppelGanger2_ = false;
    bool doppelGanger3_ = false;
    LogicalGeometryAssetLease lease_;
    std::shared_ptr<TerrainSharedGeometry> asset_;
};
