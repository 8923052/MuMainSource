#pragma once
#include "render/Assets.h"
#include <cstdint>

class BMD;

namespace Render::Effects
{
// These debris families use the ordinary textured model draw. Actual local-pose
// invariance and material eligibility are still established during asset load.
bool RequestsRigidGeometry(int type) noexcept;
} // namespace Render::Effects

namespace Render::Effects
{
struct ParticleDrawRun final
{
    std::uint64_t id = 0;
    LogicalRenderAssetRef asset;
};

struct RigidEffectRun final
{
    BMD *model = nullptr;
    bool lighting = false;
};
} // namespace Render::Effects
