#pragma once
#include "domain/WorldSimulation.h"

// CullingConstants.h
// Centralized culling radius constants used across the codebase
// These values control frustum culling sphere tests for different object types

// Default culling radii - these are fallback values
// Actual values can be overridden at runtime via DevEditor.
// The item radius matches the legacy `TestFrustrum(o->Position, 400.f)` value;
// large item models (weapons, armor sets) extend further than 100 from their
// centre, so a tighter radius caused them to flicker at the edge of view as
// the centre point exited the frustum before the geometry did.
constexpr float DEFAULT_CULL_RADIUS_ITEM = 400.0f;
constexpr float DEFAULT_CULL_RADIUS_OBJECT = 100.0f; // Unified radius for all objects

struct AnimationPoseSample;

// Temporary render overrides. Canonical facts remain borrowed through source;
// neither this value nor a recorded tape owns a mutable character pointer.
struct ObjectDrawInput final
{
    ObjectDrawInput(const OBJECT *object)
        : source(object), owner(object->Owner), type(object->Type), scale(object->Scale),
          alpha(object->Alpha), animationFrame(object->AnimationFrame),
          priorAnimationFrame(object->PriorAnimationFrame), action(object->CurrentAction),
          priorAction(object->PriorAction), blendMesh(object->BlendMesh),
          blendLight(object->BlendMeshLight), blendU(object->BlendMeshTexCoordU),
          blendV(object->BlendMeshTexCoordV), hiddenMesh(object->HiddenMesh),
          shadow(object->EnableShadow), lightEnable(object->LightEnable),
          contrastEnable(object->ContrastEnable), materialJitterU(object->MaterialJitterU),
          materialJitterV(object->MaterialJitterV), renderShadow(object->m_bRenderShadow),
          skillCount(object->m_bySkillCount), bones(object->BoneTransform)
    {
        VectorCopy(object->Position, position);
        VectorCopy(object->Angle, angle);
        VectorCopy(object->HeadAngle, headAngle);
        VectorCopy(object->Light, light);
    }

    const OBJECT *source;
    const OBJECT *owner;
    int type;
    float scale;
    float alpha;
    float animationFrame;
    float priorAnimationFrame;
    unsigned short action;
    unsigned short priorAction;
    int blendMesh;
    float blendLight;
    float blendU;
    float blendV;
    int hiddenMesh;
    bool shadow;
    bool lightEnable;
    bool contrastEnable;
    float materialJitterU;
    float materialJitterV;
    bool applyBuffs = true;
    bool HasBuff(eBuffState buff) const
    {
        return applyBuffs && source->m_BuffMap.isBuff(buff);
    }
    bool renderShadow;
    unsigned char skillCount;
    const vec34_t *bones;
    bool stableBones = false;
    const RigidObjectPose *rigidPose = nullptr;
    const AnimationPoseSample *preparedPose = nullptr;
    vec3_t position;
    vec3_t angle;
    vec3_t headAngle;
    vec3_t light;
};

class OBJECT;
class BMD;

// A range in one spatial block's placement list. A null model keeps the
// original object path. Prepared opaque ranges retain their authored order.
struct WorldObjectDrawGroup final
{
    OBJECT *first = nullptr;
    OBJECT *end = nullptr;
    BMD *model = nullptr;
    bool lighting = false;
};

class SessionKeeper;

// Exact-session photo presentation context. Gameplay identity and route stay bound.
class WorldPreviewContext final
{
  public:
    explicit WorldPreviewContext(SessionKeeper &keeper) noexcept;
    ~WorldPreviewContext();

    WorldPreviewContext(const WorldPreviewContext &) = delete;
    WorldPreviewContext &operator=(const WorldPreviewContext &) = delete;

  private:
    bool &active_;
    const bool previous_;
};

// npc

// world item

void RenderPartObjectEdgeLight(BMD *b, OBJECT *o, int Flag, bool Translate, float Scale);
