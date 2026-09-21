#include "render/Effects.h"
#include "app/AppWindow.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationLoopFrame.h"
#include "data/GameData.h"
#include "data/Localization.h"
#include "data/WorldData.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/WorldSimulation.h"
#include "render/ModelGeometry.h"
#include "render/ModelResources.h"
#include "render/Terrain.h"
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
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"

namespace Render::Effects
{
bool RequestsRigidGeometry(int type) noexcept
{
    switch (type)
    {
    case MODEL_STONE1:
    case MODEL_STONE2:
    case MODEL_BIG_STONE1:
    case MODEL_BIG_STONE2:
    case MODEL_SNOW1:
    case MODEL_SNOW2:
    case MODEL_SNOW3:
        return true;
    default:
        return false;
    }
}
} // namespace Render::Effects

void SessionRenderUnit::FlushRigidEffects(RigidEffectRun &run)
{
    if (run.model == nullptr)
        return;
    run.model->RenderRigidInstances(effectRigidInstances_, false);
    effectRigidInstances_.clear();
    run.model = nullptr;
}

bool SessionRenderUnit::QueueRigidEffect(const OBJECT &effect, RigidEffectRun &run)
{
    if (!Render::Effects::RequestsRigidGeometry(effect.Type) || effect.Alpha < 0.99f ||
        effect.BlendMesh != -1 || effect.HiddenMesh != -1 || effect.RenderType != 0 ||
        effect.EnableBoneMatrix || effect.BoneTransform != nullptr || effect.m_BuffMap.isBuff() ||
        EditFlag != EDIT_NONE || LegacyRender().UsesPolygonLineMode())
        return false;
    auto &model = Models[effect.Type];
    // One prepared mesh preserves placement order even for alpha-tested art.
    // Animated, head/stream/skin, scripted and multi-mesh materials remain on
    // their established draw path; no per-frame mesh or pose validation occurs.
    if (model.NumBones != 1 || model.Bones[0].Dummy || model.rigidInstanceMeshes_.size() != 1)
        return false;
    if (run.model != &model || run.lighting != effect.LightEnable)
    {
        FlushRigidEffects(run);
        run.model = &model;
        run.lighting = effect.LightEnable;
    }
    ObjectDrawInput draw(&effect);
    model.BodyHeight = 0.f;
    model.BodyScale = draw.scale;
    model.CurrentAction = draw.action;
    model.ContrastEnable = draw.contrastEnable;
    VectorCopy(draw.position, model.BodyOrigin);
    BodyLight(draw, &model);
    TheMapProcess().PrepareObjectLight(draw, model);
    BoneScale = 1.f;
    RenderTapeRigidInstance instance;
    float transform[3][4];
    AngleMatrix(draw.angle, transform);
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
            transform[row][column] *= draw.scale;
        transform[row][3] = draw.position[row];
    }
    // Preserve the reference operation order: root * local bone, then raw vertex.
    // Baking the local pose into vertices changes rounding at texture/depth edges.
    const auto *local =
        reinterpret_cast<const float(*)[4]>(model.sharedAsset_->invariantLocalPose.data());
    R_ConcatTransforms(transform, local, reinterpret_cast<float(*)[4]>(&instance.row0));
    instance.bodyLight = {model.BodyLight[0], model.BodyLight[1], model.BodyLight[2], draw.alpha};
    instance.baseColor = {model.BodyLight[0], model.BodyLight[1], model.BodyLight[2], 1.f};
    effectRigidInstances_.push_back(instance);
    return true;
}

void SessionRenderUnit::RenderWheelWeapon(const OBJECT *effect)
{
    const int type = effect->Owner->Weapon + MODEL_SWORD;
    auto draw = PrepareItemDraw(*effect, type);
    draw.angle[2] += effect->Direction[2];
    draw.angle[1] = 90.f;
    constexpr float WheelHeight = 100.f;
    draw.position[2] += WheelHeight;
    auto &model = Models[type];
    model.Skin = gCharacterManager.GetBaseClass(Hero->Class);
    model.CurrentAction = draw.action;
    VectorCopy(draw.position, model.BodyOrigin);
    model.Animation(BoneTransform, draw.animationFrame, draw.priorAnimationFrame, draw.priorAction,
                    draw.angle, draw.headAngle, false, false);
    vec3_t light;
    RequestTerrainLight(draw.position[0], draw.position[1], light);
    VectorAdd(light, draw.light, light);
    RenderPartObject(draw, type, nullptr, light, effect->Alpha, effect->Owner->WeaponLevel, 0, 0,
                     true, true, true);
}

void SessionRenderUnit::RenderFuryStrike(const OBJECT *effect)
{
    if (effect->LifeTime <= 10.f || effect->Kind != 0)
        return;
    const int type = effect->Owner->Weapon + MODEL_SWORD;
    auto draw = PrepareItemDraw(*effect, type);
    draw.owner = nullptr;
    draw.contrastEnable = false;
    draw.renderShadow = false;
    draw.skillCount = 0;
    auto &model = Models[type];
    model.Skin = gCharacterManager.GetBaseClass(Hero->Class);
    model.CurrentAction = draw.action;
    VectorCopy(draw.position, model.BodyOrigin);
    model.Animation(BoneTransform, draw.animationFrame, draw.priorAnimationFrame, draw.priorAction,
                    draw.angle, draw.headAngle, false, false);
    vec3_t light;
    RequestTerrainLight(draw.position[0], draw.position[1], light);
    VectorAdd(light, draw.light, light);
    RenderPartObject(draw, type, nullptr, light, effect->Alpha, effect->Owner->WeaponLevel, 0, 0,
                     true, true, true);
}

void SessionRenderUnit::RenderSkillSpear(const OBJECT *effect)
{
    ObjectDrawInput draw(effect);
    draw.blendLight = 1.f;
    auto &model = Models[MODEL_SPEARSKILL];
    model.Animation(BoneTransform, draw.animationFrame, draw.priorAnimationFrame, draw.priorAction,
                    draw.angle, draw.headAngle, false, false);
    constexpr float LightPerLifetime = 0.05f;
    vec3_t light;
    VectorScale(draw.light, effect->LifeTime * LightPerLifetime, light);
    RenderPartObject(draw, MODEL_SPEARSKILL, nullptr, light, 1.f, 0, 0, 0, true, true, true, false,
                     RENDER_BRIGHT | RENDER_TEXTURE);
}

namespace
{
float ShadowBrightness(const OBJECT &effect)
{
    constexpr int Levels = 4, MinimumLevel = 8;
    constexpr float BrightnessPerLevel = 0.1f;
    return (effect.AppearanceRandom % Levels + MinimumLevel) * BrightnessPerLevel;
}
} // namespace

void SessionRenderUnit::RenderEffectShadows()
{
    if (!g_pOption->GetRenderAllEffects())
        return;
    for (const auto *pool : {&Effects, &g_SkillEffects.Storage()})
        for (const auto &effect : *pool)
            if (effect.Visible)
                RenderEffectShadow(effect);
}

void SessionRenderUnit::RenderEffectShadow(const OBJECT &effect)
{
    const auto *o = &effect;
    vec3_t Light;
    float Luminosity = ShadowBrightness(effect);
    float Rotation, Scale;
    EnableAlphaBlend();

    switch (o->Type)
    {
    case BITMAP_MAGIC:
        if (o->SubType == 1)
        {
            RenderTerrainAlphaBitmap(BITMAP_MAGIC, o->Position[0], o->Position[1], o->Scale,
                                     o->Scale, o->Light, -o->Angle[2]);
        }
        else if (o->SubType == 8)
        {
            RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->Scale, o->Scale,
                                     o->Light, -o->Angle[2]);
            RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->Scale * 0.8f,
                                     o->Scale * 0.8f, o->Light, -o->Angle[2]);
            RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->Scale * 1.2f,
                                     o->Scale * 1.2f, o->Light, -o->Angle[2]);
        }
        else if (o->SubType == 9)
        {
            vec3_t vLight;
            Vector(o->Light[0] * o->Alpha, o->Light[1] * o->Alpha, o->Light[2] * o->Alpha, vLight);
            RenderTerrainAlphaBitmap(BITMAP_SUMMON_IMPACT, o->Position[0], o->Position[1], o->Scale,
                                     o->Scale, vLight, o->HeadAngle[1]);
            //RenderTerrainAlphaBitmap ( BITMAP_SUMMON_IMPACT, o->Position[0], o->Position[1], o->Scale*0.8f, o->Scale*0.8f, vLight, o->HeadAngle[0] );
            RenderTerrainAlphaBitmap(BITMAP_SUMMON_IMPACT, o->Position[0], o->Position[1],
                                     o->Scale * 1.2f, o->Scale * 1.2f, vLight, o->HeadAngle[2]);
        }
        else if (o->SubType == 10)
        {
            EnableAlphaBlendMinus();
            vec3_t vLight;
            Vector(o->Light[0] * o->Alpha, o->Light[1] * o->Alpha, o->Light[2] * o->Alpha, vLight);
            for (int i = 0; i < 5; ++i)
            {
                if (o->LifeTime < 3 && i > 0)
                    continue;
                else if (o->LifeTime < 6 && i > 1)
                    continue;
                else if (o->LifeTime < 9 && i > 2)
                    continue;
                else if (o->LifeTime < 12 && i > 3)
                    continue;
                else if (o->LifeTime < 16 && i > 4)
                    continue;
                RenderTerrainAlphaBitmap(BITMAP_LIGHT, o->Position[0], o->Position[1], o->Scale,
                                         o->Scale, vLight, -o->Angle[2]);
            }
        }
        else if (o->SubType == 11)
        {
            vec3_t vLight;
            Vector(o->Light[0] * o->Alpha, o->Light[1] * o->Alpha, o->Light[2] * o->Alpha, vLight);
            RenderTerrainAlphaBitmap(BITMAP_SUMMON_IMPACT, o->Position[0], o->Position[1], o->Scale,
                                     o->Scale, vLight, o->HeadAngle[1]);
            RenderTerrainAlphaBitmap(BITMAP_SUMMON_IMPACT, o->Position[0], o->Position[1],
                                     o->Scale * 1.2f, o->Scale * 1.2f, vLight, o->HeadAngle[2]);
        }
        else if (o->SubType == 12)
        {
            RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->Scale, o->Scale,
                                     o->Light, -o->Angle[2]);
            RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->Scale, o->Scale,
                                     o->Light, -o->Angle[2]);
        }
        else if (o->SubType == 13 || o->SubType == 14)
        {
            RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->Scale, o->Scale,
                                     o->Light, -o->Angle[2]);
            RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->Scale, o->Scale,
                                     o->Light, -o->Angle[2]);
        }
        break;
    case BITMAP_MAGIC + 1:
        Luminosity = 1.f;
        if (o->LifeTime < 5)
        {
            Luminosity -= (float)(5 - o->LifeTime) * 0.2f;
        }
        else if (o->SubType == 7)
        {
            if (o->LifeTime > 30)
                Luminosity = (float)(40 - o->LifeTime) * 0.1f;
        }

        if (o->SubType == 4 || o->SubType == 10)
        {
            Scale = o->Scale; //(WorldRandom()%100)/100.f*4.f;
            if (Luminosity == 1.f)
            {
                Luminosity = sinf((60 - o->LifeTime) * 0.05f) * 1.f + 0.5f;
            }
        }
        else if (o->SubType == 6)
        {
            Scale = (80 - o->LifeTime) * 0.05f;
        }
        else if (o->SubType == 7)
        {
            Scale = (o->LifeTime) * 0.07f;
        }
        else if (o->SubType == 9)
        {
            Scale = o->Scale;
        }
        else if (o->SubType == 11)
        {
            Scale = (20 - o->LifeTime) * 0.15f;
        }
        else if (o->SubType == 12)
        {
            Scale = (20 - o->LifeTime) * 0.15f;
        }
        else if (o->SubType == 13)
        {
            Scale = o->Scale;
        }
        else
        {
            Scale = (20 - o->LifeTime) * 0.15f;
        }
        if (o->SubType != 5 && o->SubType != 6 && o->SubType != 8)
        {
            switch (o->SubType)
            {
            case 0:
                Vector(Luminosity * 0.4f, Luminosity * 0.6f, Luminosity * 1.f, Light);
                break;
            case 1:
                Vector(Luminosity * 0.4f, Luminosity * 0.6f, Luminosity * 1.f, Light);
                break;
            case 2:
                Vector(Luminosity * 0.4f, Luminosity * 1.f, Luminosity * 0.6f, Light);
                break;
            case 3:
                Vector(Luminosity * 1.f, Luminosity * 0.6f, Luminosity * 0.4f, Light);
                break;
            case 4:
                Vector(Luminosity * 1.f, Luminosity * 0.5f, Luminosity * 0.1f, Light);
                break;
            case 7:
                Vector(Luminosity * 0.9f, Luminosity * 0.4f, Luminosity * 0.2f, Light);
                break;
            case 9:
                Vector(Luminosity * 0.9f, Luminosity * 0.4f, Luminosity * 0.2f, Light);
                break;
            case 10:
                Vector(Luminosity * 0.1f, Luminosity * 0.5f, Luminosity * 1.f, Light);
                break;
            case 11:
                Vector(Luminosity * o->Light[0], Luminosity * o->Light[1], Luminosity * o->Light[2],
                       Light);
                break;
            case 12:
                EnableAlphaBlendMinus();
                Vector(Luminosity * o->Light[0], Luminosity * o->Light[1], Luminosity * o->Light[2],
                       Light);
                break;
            case 13:
                Vector(o->Light[0], o->Light[1], o->Light[2], Light);
                break;
            }
            RenderTerrainAlphaBitmap(BITMAP_MAGIC + 1, o->Position[0], o->Position[1], Scale, Scale,
                                     Light, -o->Angle[2]);
        }
        break;

    case BITMAP_FIRE_HIK2_MONO:
        if (o->SubType == 0)
        {
            Vector(Luminosity * 1.f, Luminosity * 1.0f, Luminosity * 1.0f, Light);
            RenderTerrainAlphaBitmap(BITMAP_LIGHTNING + 1, o->Position[0], o->Position[1], 2.f, 2.f,
                                     Light, -o->Angle[2]);
        }
        break;

    case BITMAP_CLOUD:
        if (o->SubType == 0)
        {
            Vector(Luminosity * o->Light[0], Luminosity * o->Light[1], Luminosity * o->Light[2],
                   Light);
            RenderTerrainAlphaBitmap(BITMAP_CLOUD, o->Position[0], o->Position[1], o->Scale,
                                     o->Scale, Light, -o->Angle[2]);
        }
        break;
    case MODEL_RAKLION_BOSS_MAGIC: {
        if (o->SubType == 0)
        {
            Vector(Luminosity * 0.4f, Luminosity * 0.4f, Luminosity * 1.f, Light);
            RenderTerrainAlphaBitmap(BITMAP_LIGHT, o->Position[0], o->Position[1], 15.f, 15.f,
                                     Light, -o->Angle[2]);
            Vector(Luminosity * 1.0f, Luminosity * 1.0f, Luminosity * 1.f, Light);
            RenderTerrainAlphaBitmap(BITMAP_LIGHT, o->Position[0], o->Position[1], 5.f, 5.f, Light,
                                     -o->Angle[2]);
        }
    }
    break;
    case MODEL_EFFECT_BROKEN_ICE0:
    case MODEL_EFFECT_BROKEN_ICE1:
    case MODEL_EFFECT_BROKEN_ICE2:
    case MODEL_EFFECT_BROKEN_ICE3: {
        if (o->SubType == 1)
        {
            Vector(Luminosity * o->Light[0], Luminosity * o->Light[0], Luminosity * o->Light[0],
                   Light);
            RenderTerrainAlphaBitmap(BITMAP_LIGHT, o->Position[0], o->Position[1], 5.f, 5.f, Light,
                                     -o->Angle[2]);
        }
    }
    break;
    case BITMAP_MAGIC + 2:
        EnableAlphaBlend();
        Rotation = (int)WorldTime % 3600 / (float)10.f;

        Luminosity = 1.f;
        if (o->SubType != 2)
        {
            RenderCircle(BITMAP_MAGIC + 2, o->Position, 90.f, 130.f, 200.f, Rotation, 0.f, 0.f);
            RenderCircle(BITMAP_MAGIC + 2, o->Position, 90.f, 130.f, 200.f, -Rotation, 0.f, 0.f);

            if (o->LifeTime < 5)
                Luminosity -= (float)(5 - o->LifeTime) * 0.2f;
            Scale = (20 - o->LifeTime) * 0.15f;
        }
        else if (o->SubType == 2)
        {
            if (o->LifeTime > 10)
            {
                Scale = (20 - o->LifeTime) * 0.55f;
            }
            else
            {
                Luminosity -= (float)(10 - o->LifeTime) * 0.1f;
            }
        }
        Vector(Luminosity * 1.f, Luminosity * 0.4f, Luminosity * 0.2f, Light);
        RenderTerrainAlphaBitmap(BITMAP_MAGIC + 1, o->Position[0], o->Position[1], Scale, Scale,
                                 Light, -o->Angle[2]);
        break;

    case BITMAP_MAGIC_ZIN: {
        vec3_t vLight;
        switch (o->SubType)
        {
        case 0:
            Vector(o->Light[0] * o->Alpha * 2.f, o->Light[1] * o->Alpha * 2.f,
                   o->Light[2] * o->Alpha * 2.f, vLight);
            break;
        case 1:
            Vector(o->Light[0] * o->Alpha / 2.5f, o->Light[1] * o->Alpha / 2.5f,
                   o->Light[2] * o->Alpha / 2.5f, vLight);
            break;
        case 2:
            Vector(o->Light[0] * o->Alpha, o->Light[1] * o->Alpha, o->Light[2] * o->Alpha, vLight);
            break;
        }
        RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->Scale, o->Scale,
                                 vLight, o->HeadAngle[1]);
    }
    break;

#ifdef ASG_ADD_INFLUENCE_GROUND_EFFECT
    case BITMAP_OUR_INFLUENCE_GROUND:
        if (o->SubType == 0)
        {
            vec3_t vLight;

            Vector(0.6f * o->Alpha, 0.9f * o->Alpha, 1.0f * o->Alpha, vLight);
            RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->Scale, o->Scale,
                                     vLight, 45.f);

            Vector(0.6f * o->AlphaTarget, 0.9f * o->AlphaTarget, 1.0f * o->AlphaTarget, vLight);
            RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], 0.8f, 0.8f, vLight,
                                     45.f);

            Vector(0.2f * o->AlphaTarget, 0.8f * o->AlphaTarget, 1.0f * o->AlphaTarget, vLight);
            RenderTerrainAlphaBitmap(BITMAP_LIGHT, o->Position[0], o->Position[1], 2.0f, 2.0f,
                                     vLight);
            RenderTerrainAlphaBitmap(BITMAP_LIGHT, o->Position[0], o->Position[1], 2.0f, 2.0f,
                                     vLight);
        }
        break;

    case BITMAP_ENEMY_INFLUENCE_GROUND:
        if (o->SubType == 0)
        {
            vec3_t vLight;

            Vector(1.0f * o->Alpha, 0.3f * o->Alpha, 0.2f * o->Alpha, vLight);
            RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->Scale * 1.6f,
                                     o->Scale * 1.6f, vLight);

            Vector(1.0f * o->AlphaTarget, 0.3f * o->AlphaTarget, 0.2f * o->AlphaTarget, vLight);
            RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], 1.15f, 1.15f, vLight);

            Vector(1.0f * o->AlphaTarget, 1.0f * o->AlphaTarget, 1.0f * o->AlphaTarget, vLight);
            RenderTerrainAlphaBitmap(BITMAP_LIGHT_RED, o->Position[0], o->Position[1], 1.8f, 1.8f,
                                     vLight);
            Vector(1.0f * o->AlphaTarget, 0.0f * o->AlphaTarget, 0.0f * o->AlphaTarget, vLight);
            RenderTerrainAlphaBitmap(BITMAP_LIGHT, o->Position[0], o->Position[1], 2.0f, 2.0f,
                                     vLight);
        }
        break;
#endif // ASG_ADD_INFLUENCE_GROUND_EFFECT
    case BITMAP_FLAME:
        if (o->SubType != 3 && o->SubType != 6)
        {
            Luminosity = ShadowBrightness(effect);
            Vector(Luminosity, Luminosity, Luminosity, Light);
            RenderTerrainAlphaBitmap(BITMAP_FLAME, o->Position[0], o->Position[1], 2.f, 2.f, Light,
                                     -o->Angle[2]);
        }
        break;
    case BITMAP_LIGHTNING + 1:
        Luminosity = (float)(o->LifeTime) * 0.1f;
        Vector(Luminosity, Luminosity, Luminosity, Light);
        RenderTerrainAlphaBitmap(BITMAP_LIGHTNING + 1, o->Position[0], o->Position[1], o->Scale,
                                 o->Scale, Light, -o->Angle[2]);
        break;
    case BITMAP_TWLIGHT:
    case BITMAP_SHOCK_WAVE:
        if (o->Type == BITMAP_SHOCK_WAVE && gMapManager.InHellas() && o->SubType != 6)
        {
            DisableDepthMask();
            RenderWaterTerrain(o->Type, o->Position[0], o->Position[1], o->Scale, o->Scale,
                               o->Light, -o->Angle[2]);
            EnableDepthMask();
        }
        else
        {
            RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->Scale, o->Scale,
                                     o->Light, -o->Angle[2]);
        }
        break;

    case BITMAP_DAMAGE_01_MONO:
        if (o->SubType == 0)
        {
            RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->Scale, o->Scale,
                                     o->Light, -o->Angle[2]);
        }
        else if (o->SubType == 1)
        {
            RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->Scale, o->Scale,
                                     o->Light, -o->Angle[2]);
        }
        break;

    case BITMAP_CRATER:
        EnableAlphaTest();
        RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->StartPosition[0],
                                 o->StartPosition[1], o->Light);
        break;
    case MODEL_BLOW_OF_DESTRUCTION:
        if (o->SubType == 0)
        {
            if (o->LifeTime <= 24)
            {
                RenderTerrainAlphaBitmap(BITMAP_FLARE_BLUE, o->Position[0], o->Position[1], 4.f,
                                         4.f, o->Light, -o->Angle[2]);
            }
        }
        else if (o->SubType == 1)
        {
            if (o->LifeTime <= 24)
            {
                RenderTerrainAlphaBitmap(BITMAP_FLARE_BLUE, o->Position[0], o->Position[1], 6.f,
                                         6.f, o->Light, -o->Angle[2]);
            }
        }
        break;
    case BITMAP_CHROME_ENERGY2:
        EnableAlphaBlend();
        RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->StartPosition[0],
                                 o->StartPosition[1], o->Light);
        break;
    case BITMAP_TARGET_POSITION_EFFECT1: {
        if (o->SubType == 0)
        {
            RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->Scale, o->Scale,
                                     o->Light);
        }
    }
    break;
    case BITMAP_TARGET_POSITION_EFFECT2: {
        if (o->SubType == 0)
        {
            RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->Scale, o->Scale,
                                     o->Light);
        }
    }
    break;
    case BITMAP_RING_OF_GRADATION: {
        if (o->SubType == 0)
        {
            RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->Scale, o->Scale,
                                     o->Light);
        }
    }
    break;
    }
}

namespace
{
std::array<float, 2> JointTextureInterval(const JOINT &joint, int j, double worldTime)
{
    const auto *o = &joint;
    float Light1, Light2;
    if (o->bTileMapping)
    {
        Light1 = ((int)o->NumTails - (j)) / 16.f;
        Light2 = ((int)o->NumTails - (j + 1)) / 16.f;
    }
    else if (o->m_byReverseUV == 3)
    {
        Light1 = 1.f - (j) / (float)(o->MaxTails - 1);
        Light2 = 1.f - (j + 1) / (float)(o->MaxTails - 1);
    }
    else
    {
        Light1 = ((int)o->NumTails - (j)) / (float)(o->MaxTails - 1);
        Light2 = ((int)o->NumTails - (j + 1)) / (float)(o->MaxTails - 1);
    }

    float Scroll = (float)((int)worldTime % 1000) * 0.001f;
    if (o->Type == BITMAP_JOINT_THUNDER || o->Type == BITMAP_JOINT_THUNDER + 1)
    {
        Light1 *= 2.f;
        Light2 *= 2.f;
        Light1 -= Scroll;
        Light2 -= Scroll;
    }
    if (o->Type == BITMAP_FLARE_FORCE && o->SubType >= 0 && o->SubType <= 4 ||
        (o->SubType >= 11 && o->SubType <= 13) //^ 펜릴 스킬 관련
    )
    {
        Light1 = ((int)o->NumTails - (j)) / (float)((o->MaxTails - 1) / 2);
        Light2 = ((int)o->NumTails - (j + 1)) / (float)((o->MaxTails - 1) / 2);
        Light1 -= Scroll;
        Light2 -= Scroll;
    }
    if (o->bTileMapping)
    {
        Scroll *= 2.f;
        Light1 *= 2.f;
        Light2 *= 2.f;
        Light1 -= Scroll;
        Light2 -= Scroll;
    }
    return {Light1, Light2};
}

std::optional<SessionRenderTapeRecording::TrailSampleReservation> RecordJointSamples(
    LegacyRenderFacade &render, const JOINT &joint, std::uint32_t faceMask)
{
    const std::size_t stride = faceMask == 3 ? 2 : 1;
    auto allocation = render.ReserveTrailSamples((joint.NumTails + 1) * stride);
    if (!allocation)
        return std::nullopt;
    std::size_t next = 0;
    for (int index = 0; index <= joint.NumTails; ++index)
    {
        const auto &tail = joint.Tails[index];
        for (const int face : {RENDER_FACE_ONE, RENDER_FACE_TWO})
        {
            if ((faceMask & face) == 0)
                continue;
            const int edge = face == RENDER_FACE_ONE ? 2 : 0;
            allocation->samples[next++] = {
                {tail[edge][0], tail[edge][1], tail[edge][2]},
                {tail[edge + 1][0], tail[edge + 1][1], tail[edge + 1][2]},
                {}};
        }
    }
    return allocation;
}
} // namespace

void SessionRenderUnit::RenderJoints(BYTE bRenderOneMore)
{
    for (const auto &joint : Joints)
    {
        if (joint.NumTails <= 0 || joint.RenderFace == 0)
            continue;
        if (joint.Type == BITMAP_JOINT_ENERGY && joint.SubType == 54 &&
            joint.Target->CurrentAction != MONSTER01_ATTACK1)
            continue;
        if (bRenderOneMore == 1 && joint.byOnlyOneRender == 2)
            continue;
        if (bRenderOneMore == 2 && joint.byOnlyOneRender == 1)
            continue;
        RenderJoint(joint);
    }
}

void SessionRenderUnit::RenderJoint(const JOINT &joint)
{
    const auto *o = &joint;
    vec3_t trailLight;
    VectorCopy(o->Light, trailLight);
    constexpr float HealingSegmentFade = 0.9978f;
    const float healingFade =
        o->Type == BITMAP_JOINT_HEALING && (o->SubType == 9 || o->SubType == 10)
            ? HealingSegmentFade
            : 1.f;
    switch (o->RenderType)
    {
    case RENDER_TYPE_ALPHA_BLEND:
        EnableAlphaBlend();
        break;

    case RENDER_TYPE_ALPHA_TEST:
        EnableAlphaTest();
        break;

    case RENDER_TYPE_ALPHA_BLEND_MINUS:
        EnableAlphaBlendMinus();
        break;

    case RENDER_TYPE_ALPHA_BLEND_OTHER:
        EnableAlphaBlend2();
        break;
    }

    if (o->Type == BITMAP_JOINT_HEALING && o->SubType == 8)
    {
        DisableDepthTest();
    }

    if (o->Type == MODEL_SPEARSKILL)
    {
        float fAlpha;
        switch (o->SubType)
        {
        case 0:
        case 1:
        case 2:
        case 4:
        case 9:
        case 10:
            fAlpha = (float)std::min<int>(o->LifeTime, 20) * 0.05f;
            glColor3f(fAlpha * o->Light[0], fAlpha * o->Light[1], fAlpha * o->Light[2]);
            break;
        case 3:
        case 5:
        case 6:
        case 7:
        case 8:
        case 16:
        case 14:
        case 17:
            glColor3f(o->Light[0], o->Light[1], o->Light[2]);
            break;
        case 15:
            glColor3f(o->Light[0], o->Light[1], o->Light[2]);
            EnableAlphaBlendMinus();
            break;
        }
    }
    else if (o->Type == BITMAP_FLARE_BLUE && o->SubType == 20)
    {
        EnableAlphaBlend2();
        glColor3fv(o->Light);
    }
    else if (o->Type == BITMAP_SMOKE && o->SubType == 0)
    {
        float fAlpha = (float)std::min<int>(o->LifeTime, 20) * 0.1f;
        glColor3f(fAlpha * o->Light[0], fAlpha * o->Light[1], fAlpha * o->Light[2]);
    }
    else if (o->Type == BITMAP_JOINT_SPARK)
    {
        if (o->SubType == 5)
            BindTexture(o->TexType);
    }
    else
    {
        glColor3fv(o->Light);
    }

    BindTexture(o->TexType);
    const bool forceSingleFace = o->Type == BITMAP_JOINT_FORCE && o->SubType == 0;
    std::uint32_t faceMask = forceSingleFace ? RENDER_FACE_TWO : o->RenderFace & 3;
#ifdef GUILD_WAR_EVENT
    if (o->Type == BITMAP_FLARE && o->SubType == 22)
        faceMask = 0;
#endif
    std::uint64_t trailRunId = 0;
    float secondFaceScroll = float(int(WorldTime) % 1000) * 0.001f;
    if (o->bTileMapping)
        secondFaceScroll *= 2.f;
    const float secondFaceUOffset =
        o->Type == BITMAP_JOINT_THUNDER || o->Type == BITMAP_JOINT_THUNDER + 1
            ? secondFaceScroll * 2.f
            : 0.f;
    if (faceMask != 0)
    {
        const auto allocation = RecordJointSamples(LegacyRender(), joint, faceMask);
        if (!allocation)
            return;
        trailRunId = LegacyRender().BeginTrailInstanceRun(
            {allocation->offset, static_cast<std::uint32_t>(allocation->samples.size()), faceMask,
             secondFaceUOffset});
    }
    const auto drawSegment = [&](int index, float firstU, float nextU, float firstV,
                                 float secondV) {
        const auto &color = LegacyRender().CurrentColor();
        const RenderTapeTrailInstance instance{
            {firstU, nextU, firstV}, static_cast<std::uint32_t>(index), color};
        (void)LegacyRender().DrawTrailInstance(effectQuadGeometry_, instance, trailRunId);
        (void)LegacyRender().TexCoord2(nextU +
                                           ((faceMask & RENDER_FACE_TWO) ? secondFaceUOffset : 0.f),
                                       (faceMask & RENDER_FACE_TWO) ? firstV : secondV);
    };

    for (int j = 0; j < (int)o->NumTails; j++)
    {
        if (o->Type == BITMAP_SMOKE && o->SubType == 0 && j < 1)
        {
            continue;
        }

        if (o->Type == BITMAP_JOINT_HEALING && (o->SubType == 9 || o->SubType == 10) &&
            j == (int)o->NumTails - 1)
        {
            continue;
        }

        const auto &currentTail = o->Tails[j];

        const auto interval = JointTextureInterval(joint, j, WorldTime);
        const float Light1 = interval[0], Light2 = interval[1];
        if (o->Type == BITMAP_JOINT_FORCE && o->SubType == 0)
        {
            float Luminosity = ((float)((o->MaxTails - j) / (float)(o->MaxTails)) * 2);
            Luminosity *= o->Light[0];
            glColor3f(Luminosity, Luminosity, Luminosity);

            drawSegment(j, Light1, Light2, 0.f, 1.f);
        }
        else
        {
            if (o->Type == MODEL_SPEARSKILL &&
                (o->SubType == 0 || o->SubType == 4 || o->SubType == 9))
            {
                float scale = 0.7f;
                vec3_t Light;
                if (o->Target != NULL)
                {
                    float fJointHeight;

                    if (o->SubType == 9)
                        fJointHeight = (currentTail[0][2] - (o->Target->Position[2] + 180)) * 0.01f;
                    else
                        fJointHeight = (currentTail[0][2] - (o->Target->Position[2] + 50)) * 0.01f;

                    if (fJointHeight > 0)
                    {
                        Vector(o->Light[0] - fJointHeight, o->Light[1] - fJointHeight,
                               o->Light[2] - fJointHeight, Light);
                        glColor3fv(Light);
                    }
                    else
                    {
                        VectorCopy(o->Light, Light);
                        glColor3fv(o->Light); //1.f,1.f,1.f);
                    }
                }
                else
                {
                    glColor3f(1.f, 1.f, 1.f);
                }

                if (j == ((int)o->NumTails / 2))
                {
                    vec3_t Position;

                    Vector(0.f, 0.f, 0.f, Position);
                    for (int k = 0; k < 4; ++k)
                    {
                        VectorAdd(currentTail[k], Position, Position);
                    }
                    VectorScale(Position, 0.25f, Position);

                    CreateSprite(BITMAP_FLARE_BLUE, Position, scale, Light, NULL);
                }
            }
            else if (o->Type == BITMAP_JOINT_HEALING && (o->SubType == 9 || o->SubType == 10))
            {
                if (o->Target != NULL)
                {
                    float scale = 0.7f;
                    vec3_t Light;
                    float fJointHeight = (j) * 0.01f;
                    VectorScale(trailLight, healingFade, trailLight);
                    Vector(trailLight[0] - fJointHeight, trailLight[1] - fJointHeight,
                           trailLight[2] - fJointHeight, Light);
                    glColor3fv(Light);

                    vec3_t Position;

                    Vector(0.f, 0.f, 0.f, Position);
                    for (int k = 0; k < 4; ++k)
                    {
                        VectorAdd(currentTail[k], Position, Position);
                    }
                    VectorScale(Position, 0.25f, Position);

                    if (o->SubType == 9)
                        scale = 0.5f;
                    CreateSprite(BITMAP_FLARE_BLUE, Position, scale, Light, NULL);
                }
            }
            else if (o->Type == BITMAP_JOINT_THUNDER + 1 && o->SubType == 0)
            {
                int tail = (int)(o->Light[2]);
                if (tail == j)
                {
                    float l = o->Light[2] - j;
                    glColor3f(l, l, l);
                }
                else if (tail < j)
                {
                    glColor3f(0.f, 0.f, 0.f);
                }
                else
                {
                    glColor3f(0.7f, 0.7f, 0.7f);
                }
            }
            else if (o->Type == BITMAP_FLARE + 1 && o->SubType == 6)
            {
                if (j == 0)
                {
                    vec3_t Position;
                    Vector(0.f, 0.f, 0.f, Position);
                    for (int k = 0; k < 4; ++k)
                    {
                        VectorAdd(currentTail[k], Position, Position);
                    }
                    VectorScale(Position, 0.25f, Position);

                    CreateSprite(BITMAP_FLARE_BLUE, Position, 0.5f, o->Light, NULL,
                                 o->HeadSpriteAngles[0], 3);
                    CreateSprite(BITMAP_SHINY + 1, Position, 1.5f, o->Light, NULL,
                                 o->HeadSpriteAngles[1], 3);
                }
            }
            else if (o->Type == BITMAP_FLARE + 1 && o->SubType == 8)
            {
                if (j == 0)
                {
                    vec3_t Position;
                    Vector(0.f, 0.f, 0.f, Position);
                    for (int k = 0; k < 4; ++k)
                    {
                        VectorAdd(currentTail[k], Position, Position);
                    }
                    VectorScale(Position, 0.25f, Position);

                    CreateSprite(BITMAP_FLARE_BLUE, Position, 0.3f, o->Light, NULL,
                                 o->HeadSpriteAngles[0], 3);
                    CreateSprite(BITMAP_SHINY + 1, Position, 1.f, o->Light, NULL,
                                 o->HeadSpriteAngles[1], 3);
                }
            }
            else if (o->Type == BITMAP_FLARE_FORCE && (o->SubType >= 0 && o->SubType <= 4) ||
                     (o->SubType >= 11 && o->SubType <= 13))
            {
                float Luminosity = ((float)(((int)o->NumTails - 1 - j) / (float)(o->MaxTails)) * 2);

                glColor3f(o->Light[0] * Luminosity, o->Light[1] * Luminosity,
                          o->Light[2] * Luminosity);
            }
            else if (o->Type == BITMAP_JOINT_FORCE && o->SubType == 1)
            {
                float Luminosity = (1.f - ((int)o->NumTails - j) / (float)(o->NumTails)) * 2.f;

                glColor3f(o->Light[0] * Luminosity, o->Light[1] * Luminosity,
                          o->Light[2] * Luminosity);
            }
#ifdef GUILD_WAR_EVENT
            if (o->Type == BITMAP_FLARE && o->SubType == 22)
            {
                vec3_t t_bias;
                VectorSubtract(o->Target->Position, o->StartPosition, t_bias);
                glMatrixMode(GL_MODELVIEW);
                glPushMatrix();
                glTranslatef(t_bias[0], t_bias[1], t_bias[2]);

                glBegin(GL_QUADS);
                glTexCoord2f(Light1, 1.f);
                glVertex3fv(currentTail[2]);
                glTexCoord2f(Light1, 0.f);
                glVertex3fv(currentTail[3]);
                glTexCoord2f(Light2, 0.f);
                glVertex3fv(o->Tails[j + 1][3]);
                glTexCoord2f(Light2, 1.f);
                glVertex3fv(o->Tails[j + 1][2]);
                glTexCoord2f(Light1, 0.f);
                glVertex3fv(currentTail[0]);
                glTexCoord2f(Light1, 1.f);
                glVertex3fv(currentTail[1]);
                glTexCoord2f(Light2, 1.f);
                glVertex3fv(o->Tails[j + 1][1]);
                glTexCoord2f(Light2, 0.f);
                glVertex3fv(o->Tails[j + 1][0]);
                glEnd();

                glPopMatrix();
                continue;
            }
#endif //GUILD_WAR_EVENT

            float V1 = 0.f;
            float V2 = 1.f;
            float L1 = Light1;
            float L2 = Light2;
            if (o->m_byReverseUV == 1)
            {
                V1 = 1.f;
                V2 = 0.f;
            }
            else if (o->m_byReverseUV == 2)
            {
                L1 = 1.f - L1;
                L2 = 1.f - L2;
            }

            if (faceMask != 0)
                drawSegment(j, L1, L2, V1, V2);
        }
    }

    if (o->Type == BITMAP_JOINT_HEALING && o->SubType == 8)
    {
        EnableDepthTest();
    }
    if (o->Type == BITMAP_JOINT_THUNDER + 1 && o->SubType == 6)
    {
        vec3_t Light;
        EnableAlphaBlend();
        Vector(o->Velocity, o->Velocity, o->Velocity, Light);
        RenderTerrainAlphaBitmap(BITMAP_MAGIC + 1, o->TargetPosition[0], o->TargetPosition[1], 2.f,
                                 2.f, Light);
        DisableAlphaBlend();
    }
}

void SessionRenderUnit::RenderDestructionHighlight(const OBJECT &effect)
{
    constexpr float Height = 65.f, BaseScale = 3.f, ScaleStep = 0.011f;
    constexpr int Variants = 10;
    vec3_t light{0.5f, 0.5f, 1.f};
    vec3_t position{effect.Position[0], effect.Position[1], effect.Position[2] + Height};
    const float scale = BaseScale + (effect.AppearanceRandom % Variants) * ScaleStep;
    CreateSprite(BITMAP_SWORD_EFFECT_MONO, position, scale, light, nullptr);
}

void SessionRenderUnit::RenderEffects(bool bRenderBlendMesh)
{
    RigidEffectRun rigidRun;
    effectRigidInstances_.clear();
    for (const auto *pool : {&Effects, &g_SkillEffects.Storage()})
        for (const auto &effect : *pool)
        {
            const OBJECT *o = &effect;

            if (EffectVisible(*o))
            {
                if (bRenderBlendMesh)
                {
                    if (o->BlendMesh == -1 || o->BlendMesh < -2)
                        continue;
                    const BMD &b = Models[o->Type];
                    if (b.NumMeshs < o->BlendMesh)
                        continue;
                    //if ( (o->Position[2]+o->BoundingBoxMax[2])<350.f ) continue;
                }

                if (QueueRigidEffect(effect, rigidRun))
                    continue;
                FlushRigidEffects(rigidRun);
                switch (o->Type)
                {
                case MODEL_DESAIR:
                case MODEL_DRAGON:
                case MODEL_WARP3:
                case MODEL_WARP2:
                case MODEL_WARP:
                case MODEL_WARP6:
                case MODEL_WARP5:
                case MODEL_WARP4:
                case MODEL_GHOST:
                case MODEL_TREE_ATTACK:
                    RenderObject(o);
                    break;
                case MODEL_MULTI_SHOT1:
                case MODEL_MULTI_SHOT2:
                case MODEL_MULTI_SHOT3: {
                    BMD *b = &Models[o->Type];
                    Vector(1.f, 1.f, 1.f, b->BodyLight);
                    RenderObject(o);
                }
                break;
                case MODEL_STORM:

                    if (o->SubType == 3 || o->SubType == 4 || o->SubType == 5 || o->SubType == 6 ||
                        o->SubType == 7)
                    {
                        vec3_t Light;
                        EnableAlphaBlend();
                        Vector(0.3f, 0.3f, 0.3f, Light);
                        auto Rotation = (float)(WorldTime);
                        RenderTerrainAlphaBitmap(BITMAP_POUNDING_BALL, o->Position[0],
                                                 o->Position[1], 2.f, 2.f, Light, Rotation);
                    }
                    else
                        RenderObject(o);
                    break;
                case MODEL_STORM3:
                case MODEL_MAYAHANDSKILL:
                    break;
                case MODEL_SUMMON:
                case MODEL_STORM2:
                case MODEL_MAYASTAR:
                case MODEL_MAYASTONE1:
                case MODEL_MAYASTONE2:
                case MODEL_MAYASTONE3:
                case MODEL_MAYASTONE4:
                case MODEL_MAYASTONE5:
                case MODEL_MAYASTONEFIRE:
                case MODEL_BUTTERFLY01:
                    RenderObject(o);
                    break;
                case 9:
                    RenderObject(o);
                    break;

                case MODEL_HALLOWEEN_CANDY_BLUE:
                case MODEL_HALLOWEEN_CANDY_ORANGE:
                case MODEL_HALLOWEEN_CANDY_YELLOW:
                case MODEL_HALLOWEEN_CANDY_RED:
                case MODEL_HALLOWEEN_CANDY_HOBAK:
                case MODEL_HALLOWEEN_CANDY_STAR:
                    RenderObject(o);
                    break;
                case MODEL_HALLOWEEN:
                case MODEL_HALLOWEEN_EX:
                    break;

                case MODEL_XMAS_EVENT_BOX:
                case MODEL_XMAS_EVENT_CANDY:
                case MODEL_XMAS_EVENT_TREE:
                case MODEL_XMAS_EVENT_SOCKS:
                    RenderObject(o);
                    break;
                case MODEL_XMAS_EVENT_ICEHEART:
                    RenderObject(o);
                    break;

                case MODEL_NEWYEARSDAY_EVENT_BEKSULKI:
                case MODEL_NEWYEARSDAY_EVENT_CANDY:
                case MODEL_NEWYEARSDAY_EVENT_MONEY:
                case MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_GREEN:
                case MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_RED:
                case MODEL_NEWYEARSDAY_EVENT_PIG:
                case MODEL_NEWYEARSDAY_EVENT_YUT:
                    RenderObject(o);
                    break;
                case MODEL_MOONHARVEST_MOON: {
                    if (o->SubType == 0)
                    {
                        vec3_t vLight;
                        // (Shockwave)
                        Vector(0.6f, 0.8f, 0.6f, vLight);
                        constexpr float ShockwaveDegreesPerTick = 3.f;
                        const float rotation =
                            -(GameLogic::Effects::MoonHarvestLifetime - o->LifeTime) *
                            ShockwaveDegreesPerTick;
                        CreateSprite(BITMAP_SHOCK_WAVE, o->Position, 0.8f, vLight, o, rotation);
                        // Flare1
                        Vector(0.8f, 0.6f, 0.f, vLight);
                        CreateSprite(BITMAP_LIGHT, o->Position, 5.0f, vLight, o, 0);
                        RenderObject(o);
                    }
                    else if (o->SubType == 1)
                    {
                        CreateSprite(BITMAP_LIGHT, o->Position, 5.0f, o->Light, o, 0);
                        CreateSprite(BITMAP_LIGHT, o->Position, 3.0f, o->Light, o, 0);
                        CreateSprite(BITMAP_LIGHT, o->Position, 3.0f, o->Light, o, 0);
                        CreateSprite(BITMAP_SHINY + 6, o->Position, 2.0f, o->Light, o, 0);

                        RenderObject(o);
                    }
                    else if (o->SubType == 2)
                    {
                        RenderObject(o);
                    }
                }
                break;
                case MODEL_MOONHARVEST_GAM:
                case MODEL_MOONHARVEST_SONGPUEN1:
                case MODEL_MOONHARVEST_SONGPUEN2:
                    RenderObject(o);
                    break;

                case MODEL_CHANGE_UP_EFF:
                    RenderObject(o);
                    break;
                case MODEL_CURSEDTEMPLE_HOLYITEM:
                case MODEL_CURSEDTEMPLE_PRODECTION_SKILL: {
                    RenderObject(o);
                }
                break;
                case MODEL_CURSEDTEMPLE_RESTRAINT_SKILL: {
                    RenderObject(o);
                }
                break;
                case MODEL_SPEARSKILL:
                    RenderSkillSpear(o);
                    break;
                case BITMAP_FIRE_CURSEDLICH:
                    break;
                case MODEL_SUMMONER_WRISTRING_EFFECT:
                    RenderObject(o);
                    break;
                case MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT:
                    if (o->SubType == 0)
                    {
                        RenderObject(o);
                    }
                    break;
                case MODEL_SUMMONER_EQUIP_HEAD_NEIL:
                    if (o->SubType == 0)
                    {
                        RenderObject(o);
                    }
                    break;
                case MODEL_SUMMONER_CASTING_EFFECT1:
                case MODEL_SUMMONER_CASTING_EFFECT11:
                case MODEL_SUMMONER_CASTING_EFFECT111:
                case MODEL_SUMMONER_CASTING_EFFECT2:
                case MODEL_SUMMONER_CASTING_EFFECT22:
                case MODEL_SUMMONER_CASTING_EFFECT222:
                case MODEL_SUMMONER_CASTING_EFFECT4:
                    RenderObject(o);
                    break;
                case MODEL_SUMMONER_SUMMON_SAHAMUTT:
                    RenderObject(o);
                    break;
                case MODEL_SUMMONER_SUMMON_NEIL:
                    RenderObject(o);
                    break;
                case MODEL_SUMMONER_SUMMON_NEIL_NIFE1:
                case MODEL_SUMMONER_SUMMON_NEIL_NIFE2:
                case MODEL_SUMMONER_SUMMON_NEIL_NIFE3:
                    RenderObject(o);
                    break;
                case MODEL_SUMMONER_SUMMON_NEIL_GROUND1:
                case MODEL_SUMMONER_SUMMON_NEIL_GROUND2:
                case MODEL_SUMMONER_SUMMON_NEIL_GROUND3:
                    RenderObject(o);
                    break;
                case MODEL_SUMMONER_SUMMON_LAGUL:
                    if (o->SubType == 1)
                    {
                        BMD *pModel = &Models[o->Type];
                        vec3_t vPos, vLight;

                        const int nBoneCount = 6;
                        int nBone[nBoneCount] = {54, 55, 56, 57, 58, 59};
                        Vector(0.2f, 0.3f, 1.0f, vLight);
                        pModel->Animation(BoneTransform, o->AnimationFrame, o->PriorAnimationFrame,
                                          o->PriorAction, o->Angle, o->HeadAngle, false, false);
                        for (int i = 0; i < nBoneCount; ++i)
                        {
                            pModel->TransformByObjectBone(vPos, o, nBone[i]);
                            CreateSprite(BITMAP_SHINY + 6, vPos, o->Scale * 0.3f, vLight, NULL);
                        }

                        RenderObject(o);
                    }
                    break;
                case MODEL_EFFECT_BROKEN_ICE0:
                case MODEL_EFFECT_BROKEN_ICE1:
                case MODEL_EFFECT_BROKEN_ICE2:
                case MODEL_EFFECT_BROKEN_ICE3:
                    RenderObject(o);
                    break;
                case MODEL_SKILL_WHEEL1:
                    break;
                case MODEL_SKILL_WHEEL2:
                    RenderWheelWeapon(o);
                    break;
                case MODEL_SKILL_FURY_STRIKE:
                    RenderFuryStrike(o);
                    break;
                case MODEL_ARROW_HOLY:
                    break;
                case BITMAP_BOSS_LASER:
                case BITMAP_BOSS_LASER + 1:
                case BITMAP_BOSS_LASER + 2:
                    RenderBossLaser(*o);
                    break;

                case MODEL_GATE:
                case MODEL_GATE + 1:
                case MODEL_STONE_COFFIN:
                case MODEL_STONE_COFFIN + 1:
                case MODEL_STAFF_OF_DESTRUCTION:
                case MODEL_CLOUD:
                    RenderObject(o);
                    break;
                case MODEL_SHINE:
                    break;
                case MODEL_WAVE_FORCE:
                    RenderObject(o);
                    break;
                case MODEL_MAGIC_CAPSULE2:
                    RenderObject(o);
                    break;
                case MODEL_AIR_FORCE:
                case MODEL_PIER_PART:
                    RenderObject(o);
                    break;
                case MODEL_PIERCING2:
                    if (o->SubType == 1 || o->SubType == 2)
                        break;
                    RenderObject(o);
                    break;
                case MODEL_PIERCING:
                    if (o->SubType == 3)
                        break;
                    RenderObject(o);
                    break;
                case MODEL_TOWER_GATE_PLANE: {
                    RenderObject(o);
                    ObjectDrawInput reflected(o);
                    constexpr float MirrorHeight = 400.f;
                    reflected.position[2] =
                        o->StartPosition[2] + MirrorHeight - (o->Position[2] - o->StartPosition[2]);
                    RenderObject(reflected);
                    break;
                }

                case BATTLE_CASTLE_WALL1:
                case BATTLE_CASTLE_WALL2:
                case BATTLE_CASTLE_WALL3:
                case BATTLE_CASTLE_WALL4:
                    RenderObject(o);
                    break;
                case MODEL_FENRIR_THUNDER:
                    RenderObject(o);
                    break;
                case MODEL_FALL_STONE_EFFECT:
                    RenderObject(o);
                    break;
                case MODEL_FENRIR_FOOT_THUNDER: {
                    EnableAlphaBlend();
                    RenderTerrainAlphaBitmap(BITMAP_FENRIR_FOOT_THUNDER1 + (o->m_iAnimation % 5),
                                             o->Position[0], o->Position[1], 0.6f, 0.6f, o->Light);
                    DisableAlphaBlend();
                }
                break;
                case MODEL_TWINTAIL_EFFECT: {
                    if (o->SubType == 0)
                    {
                        EnableAlphaBlend();
                        RenderTerrainAlphaBitmap(BITMAP_SPARK + 1, o->Position[0], o->Position[1],
                                                 o->Scale, o->Scale, o->Light, 0.f, o->Alpha);
                        DisableAlphaBlend();
                    }
                    else if (o->SubType == 1 || o->SubType == 2)
                    {
                        EnableAlphaBlend();
                        RenderTerrainAlphaBitmap(BITMAP_CLOUD, o->Position[0], o->Position[1],
                                                 o->Scale, o->Scale, o->Light, o->Angle[0],
                                                 o->Alpha);
                        DisableAlphaBlend();
                    }
                }
                break;
                case MODEL_CUNDUN_PART1:
                case MODEL_CUNDUN_PART2:
                case MODEL_CUNDUN_PART3:
                case MODEL_CUNDUN_PART4:
                case MODEL_CUNDUN_PART5:
                case MODEL_CUNDUN_PART6:
                case MODEL_CUNDUN_PART7:
                case MODEL_CUNDUN_PART8:
                case MODEL_ILLUSION_OF_KUNDUN:
                case MODEL_CUNDUN_DRAGON_HEAD:
                case MODEL_CUNDUN_PHOENIX:
                case MODEL_CUNDUN_GHOST:
                    RenderObject(o);
                    break;
                case MODEL_CURSEDTEMPLE_STATUE_PART1:
                case MODEL_CURSEDTEMPLE_STATUE_PART2:
                    RenderObject(o);
                    break;
                case MODEL_XMAS2008_SNOWMAN_HEAD:
                case MODEL_XMAS2008_SNOWMAN_BODY:
                    RenderObject(o);
                    break;
#ifdef PJH_ADD_PANDA_CHANGERING
                case MODEL_PANDA:
                    RenderObject(o);
                    break;
#endif //PJH_ADD_PANDA_CHANGERING
                case MODEL_TOTEMGOLEM_PART1:
                case MODEL_TOTEMGOLEM_PART2:
                case MODEL_TOTEMGOLEM_PART3:
                case MODEL_TOTEMGOLEM_PART4:
                case MODEL_TOTEMGOLEM_PART5:
                case MODEL_TOTEMGOLEM_PART6:
                    RenderObject(o);
                    break;
                case MODEL_SHADOW_PAWN_ANKLE_LEFT:
                case MODEL_SHADOW_PAWN_ANKLE_RIGHT:
                case MODEL_SHADOW_PAWN_BELT:
                case MODEL_SHADOW_PAWN_CHEST:
                case MODEL_SHADOW_PAWN_HELMET:
                case MODEL_SHADOW_PAWN_KNEE_LEFT:
                case MODEL_SHADOW_PAWN_KNEE_RIGHT:
                case MODEL_SHADOW_PAWN_WRIST_LEFT:
                case MODEL_SHADOW_PAWN_WRIST_RIGHT:

                case MODEL_SHADOW_KNIGHT_ANKLE_LEFT:
                case MODEL_SHADOW_KNIGHT_ANKLE_RIGHT:
                case MODEL_SHADOW_KNIGHT_BELT:
                case MODEL_SHADOW_KNIGHT_CHEST:
                case MODEL_SHADOW_KNIGHT_HELMET:
                case MODEL_SHADOW_KNIGHT_KNEE_LEFT:
                case MODEL_SHADOW_KNIGHT_KNEE_RIGHT:
                case MODEL_SHADOW_KNIGHT_WRIST_LEFT:
                case MODEL_SHADOW_KNIGHT_WRIST_RIGHT:

                case MODEL_SHADOW_ROOK_ANKLE_LEFT:
                case MODEL_SHADOW_ROOK_ANKLE_RIGHT:
                case MODEL_SHADOW_ROOK_BELT:
                case MODEL_SHADOW_ROOK_CHEST:
                case MODEL_SHADOW_ROOK_HELMET:
                case MODEL_SHADOW_ROOK_KNEE_LEFT:
                case MODEL_SHADOW_ROOK_KNEE_RIGHT:
                case MODEL_SHADOW_ROOK_WRIST_LEFT:
                case MODEL_SHADOW_ROOK_WRIST_RIGHT:

                case MODEL_ICE_GIANT_PART1:
                case MODEL_ICE_GIANT_PART2:
                case MODEL_ICE_GIANT_PART3:
                case MODEL_ICE_GIANT_PART4:
                case MODEL_ICE_GIANT_PART5:
                case MODEL_ICE_GIANT_PART6:
                    RenderObject(o);
                    break;

                case MODEL_PROTECTGUILD:
                    RenderObject(o);
                    break;
                case MODEL_ARROW_AUTOLOAD:
                    if (o->SubType == 1)
                        RenderObject(o);
                    break;

                case MODEL_INFINITY_ARROW:
                    if (o->SubType == 1)
                        RenderObject(o);
                    break;

                case MODEL_INFINITY_ARROW1:
                case MODEL_INFINITY_ARROW2:
                case MODEL_INFINITY_ARROW3:
                case MODEL_INFINITY_ARROW4:
                    RenderObject(o);
                    break;

                case MODEL_ARROW_BEST_CROSSBOW:
                    RenderObject(o);
                    break;

                case MODEL_ALICE_BUFFSKILL_EFFECT:
                case MODEL_ALICE_BUFFSKILL_EFFECT2: {
                    if (o->SubType == 0 || o->SubType == 1 || o->SubType == 2)
                    {
                        RenderObject(o);
                    }
                }
                break;
                case MODEL_RAKLION_BOSS_CRACKEFFECT: {
                    RenderObject(o);
                }
                break;
                case MODEL_RAKLION_BOSS_MAGIC: {
                    RenderObject(o);
                }
                break;
                case MODEL_LAVAGIANT_FOOTPRINT_R:
                case MODEL_LAVAGIANT_FOOTPRINT_V: {
                    EnableAlphaBlend();
                    if (o->Type == MODEL_LAVAGIANT_FOOTPRINT_R)
                    {
                        RenderTerrainAlphaBitmap(BITMAP_LAVAGIANT_FOOTPRINT_R, o->Position[0],
                                                 o->Position[1], o->Scale, o->Scale, o->Light);
                    }
                    else
                    {
                        RenderTerrainAlphaBitmap(BITMAP_LAVAGIANT_FOOTPRINT_V, o->Position[0],
                                                 o->Position[1], o->Scale, o->Scale, o->Light);
                    }
                    DisableAlphaBlend();
                }
                break;
#ifdef PBG_ADD_CHARACTERSLOT
                case MODEL_SLOT_LOCK: {
                    RenderObject(o);
                }
                break;
#endif //PBG_ADD_CHARACTERSLOT
                case MODEL_MOVE_TARGETPOSITION_EFFECT: {
                    RenderObject(o);
                }
                break;
                case MODEL_EFFECT_SAPITRES_ATTACK_1:
                case MODEL_EFFECT_SAPITRES_ATTACK_2: {
                    RenderObject(o);
                }
                break;
                case MODEL_BLOW_OF_DESTRUCTION: {
                    if (o->SubType == 0)
                    {
                        if (o->LifeTime <= 24)
                        {
                            RenderDestructionHighlight(*o);
                        }
                    }
                    else if (o->SubType == 1)
                    {
                        if (o->LifeTime <= 24)
                        {
                            vec3_t vLight, vPos;
                            Vector(o->Light[0] * 0.5f, o->Light[1] * 0.5f, o->Light[2] * 1.0f,
                                   vLight);
                            VectorCopy(o->Position, vPos);
                            vPos[2] += 100.f;
                            CreateSprite(BITMAP_LIGHT, vPos, o->Scale, vLight, NULL);
                        }
                    }
                }
                break;
                case MODEL_NIGHTWATER_01:
                case MODEL_KNIGHT_PLANCRACK_A:
                case MODEL_KNIGHT_PLANCRACK_B: {
                    RenderObject(o);
                }
                break;
                case MODEL_EFFECT_FLAME_STRIKE: {
                    if (o->SubType == 0)
                    {
                        const OBJECT *pObject = o;
                        BMD *pModel = &Models[pObject->Type];
                        const OBJECT *pOwner = pObject->Owner;
                        BMD *pOwnerModel = &Models[pOwner->Type];

                        vec3_t vPos, p;
                        Vector(0.0f, 0.0f, 0.0f, p);
                        pOwnerModel->RotationPosition(pOwner->BoneTransform[33], p, vPos);
                        VectorAdd(pOwner->Position, vPos, pModel->BodyOrigin);
                        const vec3_t angle{};

                        OBB_t OBB{};
                        vec3_t Temp{};
                        pModel->Animation(BoneTransform, pObject->AnimationFrame,
                                          pObject->PriorAnimationFrame, pObject->PriorAction, angle,
                                          angle, true);
                        pModel->Transform(BoneTransform, Temp, Temp, &OBB, true);

                        BodyLight(pObject, pModel);
                        pModel->BodyScale = pObject->Scale;

                        pModel->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, pObject->Alpha, 0,
                                           pObject->Alpha, pObject->BlendMeshTexCoordU,
                                           pObject->BlendMeshTexCoordV, -1);
                        float fV = (((int)(WorldTime * 0.05) % 16) / 4) * 0.25f;
                        pModel->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, pObject->Alpha, 1,
                                           pObject->Alpha, pObject->BlendMeshTexCoordU, fV);
                    }
                }
                break;
                case BITMAP_LIGHT_MARKS: {
                    BMD *pModel = &Models[o->Owner->Type];
                    vec3_t vPos;
                    const int nBoneCount = 14;
                    int nBone[nBoneCount] = {20, 20, 19, 18, 17, 2, 35, 26, 36, 27, 37, 28, 39, 30};
                    float fScale[nBoneCount] = {1.5f, 1.5f, 0.6f, 1.1f, 0.9f, 0.8f, 0.6f,
                                                0.6f, 0.8f, 0.8f, 0.8f, 0.8f, 0.7f, 0.7f};
                    for (int i = 0; i < nBoneCount; ++i)
                    {
                        pModel->TransformByObjectBone(vPos, o->Owner, nBone[i]);
                        CreateSprite(o->Type, vPos, o->Scale * fScale[i], o->Light, o->Owner);
                    }
                }
                break;
                case MODEL_SWELL_OF_MAGICPOWER_BUFF_EFF: {
                    if (o->SubType == 0)
                    {
                        BMD *pModel = &Models[o->Owner->Type];
                        vec3_t vPos, vDLight;
                        if (o->Owner->Type != MODEL_PLAYER)
                            break;

                        float fLumi = (absf((sinf(WorldTime * 0.001f))) + 0.2f) * 0.5f;
                        // 							if( fLumi >= 0.5f)
                        // 							{
                        // 								fLumi = 0.5f;
                        // 							}
                        Vector(fLumi * 0.7f, fLumi * 0.3f, fLumi * 0.9f, vDLight);

                        for (int i = 0; i < pModel->NumBones; i++)
                        {
                            pModel->TransformByObjectBone(vPos, o->Owner, i);

                            CreateSprite(BITMAP_LIGHT, vPos, 1.8f, vDLight, o);
                        }
                    }
                }
                break;
                case MODEL_PROJECTILE: {
                    RenderObject(o);
                }
                break;

                case MODEL_DOOR_CRUSH_EFFECT_PIECE01:
                case MODEL_DOOR_CRUSH_EFFECT_PIECE02:
                case MODEL_DOOR_CRUSH_EFFECT_PIECE03:
                case MODEL_DOOR_CRUSH_EFFECT_PIECE04:
                case MODEL_DOOR_CRUSH_EFFECT_PIECE05:
                case MODEL_DOOR_CRUSH_EFFECT_PIECE06:
                case MODEL_DOOR_CRUSH_EFFECT_PIECE07:
                case MODEL_DOOR_CRUSH_EFFECT_PIECE08:
                case MODEL_DOOR_CRUSH_EFFECT_PIECE09:
                case MODEL_DOOR_CRUSH_EFFECT_PIECE10:
                case MODEL_DOOR_CRUSH_EFFECT_PIECE11:
                case MODEL_DOOR_CRUSH_EFFECT_PIECE12:
                case MODEL_DOOR_CRUSH_EFFECT_PIECE13:
                case MODEL_STATUE_CRUSH_EFFECT_PIECE01:
                case MODEL_STATUE_CRUSH_EFFECT_PIECE02:
                case MODEL_STATUE_CRUSH_EFFECT_PIECE03:
                case MODEL_STATUE_CRUSH_EFFECT_PIECE04: {
                    RenderObject(o);
                }
                break;
                case MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_:
                case MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_:
                case MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_:
                case MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_:
                case MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_: {
                    BMD *pBMDSwordModel = &Models[o->Type];

                    vec3_t arrEachBoneTranslations[MAX_BONES];
                    PrepareGaionSwordPose(*o, arrEachBoneTranslations);

                    pBMDSwordModel->LightEnable = true;

                    Vector(1.0f, 1.0f, 1.0f, pBMDSwordModel->BodyLight);
                    pBMDSwordModel->RenderMesh(0, RENDER_TEXTURE, o->Alpha, o->BlendMesh,
                                               o->BlendMeshLight, o->BlendMeshTexCoordU,
                                               o->BlendMeshTexCoordV);

                    Vector(1.0f, 1.0f, 1.0f, pBMDSwordModel->BodyLight);
                    pBMDSwordModel->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, o->Alpha,
                                               o->BlendMesh, o->BlendMeshLight,
                                               o->BlendMeshTexCoordU, o->BlendMeshTexCoordV);

                    Vector(0.2f, 0.2f, 1.0f, pBMDSwordModel->BodyLight);
                    constexpr float edgeAlpha = 1.f;
                    pBMDSwordModel->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, edgeAlpha,
                                               o->BlendMesh, o->BlendMeshLight,
                                               o->BlendMeshTexCoordU, o->BlendMeshTexCoordV);

                    if (o->SubType != 20)
                    {
                        vec3_t vRelative, vLight;
                        vec3_t vPos_SwordEffectRed01, vPos_SwordEffectRed02, vPos_SwordEffectEdge01,
                            vPos_SwordEffectEdge02, vPos_SwordEffectEdge03, vPos_SwordEffectEdge04,
                            vPos_SwordEffectEdge05, vPos_SwordEffectEdge06, vPos_SwordEffectEdge07,
                            vPos_SwordEffectEdge08, vPos_SwordEffectEdge09;
                        float fLumi1, fLumi2;
                        int arrBoneIdxs_SwordEffectRed01[] = {2, 9, 1, 3,
                                                              10}; // SWORD MainEffect01 BoneINDEX.
                        int arrBoneIdxs_SwordEffectRed02[] = {3, 10, 2, 10,
                                                              11}; // SWORD MainEffect02 BoneINDEX.
                        int arrBoneIdxs_SwordEffectEdge01[] = {13, 13, 3, 4,
                                                               1}; // SWORD EdgeEffect01 BoneINDEX.
                        int arrBoneIdxs_SwordEffectEdge02[] = {12, 6, 4, 12,
                                                               2}; // SWORD EdgeEffect02 BoneINDEX.
                        int arrBoneIdxs_SwordEffectEdge03[] = {5, 1, 5, 2,
                                                               3}; // SWORD EdgeEffect03 BoneINDEX.
                        int arrBoneIdxs_SwordEffectEdge04[] = {6, 7, 6, 9,
                                                               4}; // SWORD EdgeEffect04 BoneINDEX.
                        int arrBoneIdxs_SwordEffectEdge05[] = {1, 2, 12, 5,
                                                               5}; // SWORD EdgeEffect05 BoneINDEX.
                        int arrBoneIdxs_SwordEffectEdge06[] = {8, 4, 8, 7,
                                                               6}; // SWORD EdgeEffect06 BoneINDEX.
                        int arrBoneIdxs_SwordEffectEdge07[] = {9, 3, 9, 6,
                                                               7}; // SWORD EdgeEffect07 BoneINDEX.
                        int arrBoneIdxs_SwordEffectEdge08[] = {10, 11, 10, 8,
                                                               8}; // SWORD EdgeEffect08 BoneINDEX.
                        int arrBoneIdxs_SwordEffectEdge09[] = {4, 8, 7, 1,
                                                               9}; // SWORD EdgeEffect09 BoneINDEX.
                        //MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_
                        //MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_
                        //MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_
                        //MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_
                        //MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_

                        int iBoneIdx_SwordEffectMain01 = arrBoneIdxs_SwordEffectRed01
                            [o->Type - MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_];
                        int iBoneIdx_SwordEffectMain02 = arrBoneIdxs_SwordEffectRed02
                            [o->Type - MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_];

                        int iBoneIdx_SwordEffectEdge01 = arrBoneIdxs_SwordEffectEdge01
                            [o->Type - MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_];
                        int iBoneIdx_SwordEffectEdge02 = arrBoneIdxs_SwordEffectEdge02
                            [o->Type - MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_];
                        int iBoneIdx_SwordEffectEdge03 = arrBoneIdxs_SwordEffectEdge03
                            [o->Type - MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_];
                        int iBoneIdx_SwordEffectEdge04 = arrBoneIdxs_SwordEffectEdge04
                            [o->Type - MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_];
                        int iBoneIdx_SwordEffectEdge05 = arrBoneIdxs_SwordEffectEdge05
                            [o->Type - MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_];
                        int iBoneIdx_SwordEffectEdge06 = arrBoneIdxs_SwordEffectEdge06
                            [o->Type - MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_];
                        int iBoneIdx_SwordEffectEdge07 = arrBoneIdxs_SwordEffectEdge07
                            [o->Type - MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_];
                        int iBoneIdx_SwordEffectEdge08 = arrBoneIdxs_SwordEffectEdge08
                            [o->Type - MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_];
                        int iBoneIdx_SwordEffectEdge09 = arrBoneIdxs_SwordEffectEdge09
                            [o->Type - MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_];

                        fLumi1 = 1.0f;
                        fLumi2 = 1.0f;
                        Vector(0.0f, 0.0f, 0.0f, vRelative);

                        Vector(0.0f, 0.0f, 0.0f, vLight);

                        // 2-2. 기본 Jewel Effect //
                        Vector(fLumi1 * 1.0f, fLumi1 * 0.4f, fLumi1 * 0.1f, vLight);

                        //			if( MODEL_MONSTER01+164 == o->Owner->Type )	// 2-3-1.
                        {
                            VectorCopy(arrEachBoneTranslations[iBoneIdx_SwordEffectMain01],
                                       vPos_SwordEffectRed01);
                            VectorCopy(arrEachBoneTranslations[iBoneIdx_SwordEffectMain02],
                                       vPos_SwordEffectRed02);
                        }
                        // 			else // 2-3-1. Effect를 통한 렌더의 경우.
                        // 			{
                        // 				pBMDSwordModel->TransformByObjectBone(vPos_SwordEffectRed01, o, iBoneIdx_SwordEffectMain01, vRelative);
                        // 				pBMDSwordModel->TransformByObjectBone(vPos_SwordEffectRed02, o, iBoneIdx_SwordEffectMain02, vRelative);
                        // 			}

                        CreateSprite(BITMAP_LIGHT_RED, vPos_SwordEffectRed01, 1.3f, vLight, o);
                        CreateSprite(BITMAP_LIGHT_RED, vPos_SwordEffectRed02, 1.3f, vLight, o);
                        // 2-2. 기본 Jewel Effect //

                        // 2-3. 기본 Edge Effect
                        {
                            VectorCopy(arrEachBoneTranslations[iBoneIdx_SwordEffectEdge01],
                                       vPos_SwordEffectEdge01);
                            VectorCopy(arrEachBoneTranslations[iBoneIdx_SwordEffectEdge02],
                                       vPos_SwordEffectEdge02);
                            VectorCopy(arrEachBoneTranslations[iBoneIdx_SwordEffectEdge03],
                                       vPos_SwordEffectEdge03);
                            VectorCopy(arrEachBoneTranslations[iBoneIdx_SwordEffectEdge04],
                                       vPos_SwordEffectEdge04);
                            VectorCopy(arrEachBoneTranslations[iBoneIdx_SwordEffectEdge05],
                                       vPos_SwordEffectEdge05);
                            VectorCopy(arrEachBoneTranslations[iBoneIdx_SwordEffectEdge06],
                                       vPos_SwordEffectEdge06);
                            VectorCopy(arrEachBoneTranslations[iBoneIdx_SwordEffectEdge07],
                                       vPos_SwordEffectEdge07);

                            VectorCopy(arrEachBoneTranslations[iBoneIdx_SwordEffectEdge08],
                                       vPos_SwordEffectEdge08);
                            VectorCopy(arrEachBoneTranslations[iBoneIdx_SwordEffectEdge09],
                                       vPos_SwordEffectEdge09);
                        }

                        Vector(fLumi1 * 0.3f, fLumi1 * 0.3f, fLumi1 * 1.0f, vLight);
                        CreateSprite(BITMAP_LIGHT, vPos_SwordEffectEdge01, 1.5f, vLight, o);
                        CreateSprite(BITMAP_LIGHT, vPos_SwordEffectEdge02, 1.5f, vLight, o);
                        CreateSprite(BITMAP_LIGHT, vPos_SwordEffectEdge03, 1.5f, vLight, o);
                        CreateSprite(BITMAP_LIGHT, vPos_SwordEffectEdge04, 1.5f, vLight, o);
                        CreateSprite(BITMAP_LIGHT, vPos_SwordEffectEdge05, 1.5f, vLight, o);
                        CreateSprite(BITMAP_LIGHT, vPos_SwordEffectEdge06, 1.5f, vLight, o);
                        CreateSprite(BITMAP_LIGHT, vPos_SwordEffectEdge07, 1.5f, vLight, o);

                        CreateSprite(BITMAP_LIGHT, vPos_SwordEffectEdge08, 1.0f, vLight, o);
                        CreateSprite(BITMAP_LIGHT, vPos_SwordEffectEdge09, 1.0f, vLight, o);
                    }
                }
                break;
                case MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE: {
                    RenderObject(o);
                }
                break;
                case MODEL_EMPIREGUARDIAN_BLOW_OF_DESTRUCTION: {
                    if (o->SubType == 0)
                    {
                        if (o->LifeTime <= 24)
                        {
                            RenderDestructionHighlight(*o);
                        }
                    }
                    else if (o->SubType == 1)
                    {
                        if (o->LifeTime <= 24)
                        {
                            vec3_t vLight, vPos;
                            Vector(o->Light[0] * 0.5f, o->Light[1] * 0.5f, o->Light[2] * 1.0f,
                                   vLight);
                            VectorCopy(o->Position, vPos);
                            vPos[2] += 100.f;
                            CreateSprite(BITMAP_LIGHT, vPos, o->Scale, vLight, NULL);
                        }
                    }
                }
                break;
                case BITMAP_WATERFALL_4: {
                    //CreateSprite(BITMAP_WATERFALL_4, o->Position, o->Scale, o->Light, o, o->Angle[0]);
                }
                break;
                case BITMAP_EVENT_CLOUD: {
                    EnableAlphaBlend();
                    RenderTerrainAlphaBitmap(BITMAP_EVENT_CLOUD, o->Position[0], o->Position[1],
                                             o->Scale, o->Scale, o->Light, o->Angle[0], o->Alpha);
                    DisableAlphaBlend();
                }
                break;
                case MODEL_SHOCKWAVE02: {
                    RenderObject(o);
                }
                break;
                case MODEL_DRAGON_LOWER_DUMMY: {
                    RenderObject(o);
                }
                break;
                case BITMAP_LIGHT_RED: {
                    vec3_t vLight;
                    VectorCopy(o->Light, vLight);
                    float fScale = 1.0f;
                    fScale = 3.5f;
                    Vector(1.0f, 1.0f, 1.0f, vLight);
                    float fFadeInOut = (float)(sinf(WorldTime * 0.0005f) + 1.0f) * 0.25f + 0.2f;
                    if (o->SubType == 3 || o->SubType == 4)
                    {
                        fScale = o->Scale;
                        fFadeInOut = o->LifeTime * 0.01f;
                    }
                    VectorScale(vLight, fFadeInOut, vLight);

                    if (o->SubType == 0)
                    {
                        EnableAlphaBlend();
                        RenderTerrainAlphaBitmap(BITMAP_LIGHT_RED, o->Owner->Position[0],
                                                 o->Owner->Position[1], fScale, fScale, vLight,
                                                 o->Angle[0], o->Alpha);
                        RenderTerrainAlphaBitmap(BITMAP_LIGHT_RED, o->Owner->Position[0],
                                                 o->Owner->Position[1], fScale, fScale, vLight,
                                                 o->Angle[0], o->Alpha);
                        DisableAlphaBlend();
                    }
                    else if (o->SubType == 3)
                    {
                        EnableAlphaBlend();
                        RenderTerrainAlphaBitmap(BITMAP_LIGHT_RED, o->Owner->Position[0],
                                                 o->Owner->Position[1], fScale, fScale, vLight,
                                                 o->Angle[0], o->Alpha);
                        DisableAlphaBlend();
                    }
                    else if (o->SubType == 4)
                    {
                        EnableAlphaBlend();
                        RenderTerrainAlphaBitmap(BITMAP_LIGHT_RED, o->Position[0], o->Position[1],
                                                 fScale, fScale, vLight, o->Angle[0], o->Alpha);
                        RenderTerrainAlphaBitmap(BITMAP_LIGHT_RED, o->Position[0], o->Position[1],
                                                 fScale, fScale, vLight, o->Angle[0], o->Alpha);
                        DisableAlphaBlend();
                    }

                    if (o->SubType == 0)
                    {
                        vec3_t Light, Angle;
                        int arr[17] = {12, 17, 5, 10, 36, 27, 37, 28, 11,
                                       35, 2,  3, 36, 20, 27, 4,  26};
                        BMD *b = &Models[o->Owner->Type];
                        vec3_t _vPos;
                        Vector(0.f, 0.f, 0.f, Angle);

                        for (int i = 0; i < 17; ++i)
                        {
                            float fFadeInOut =
                                (float)(sinf(WorldTime * 0.01f) + 1.0f) * 0.25f + 0.2f;
                            Vector(1.0f, 1.0f, 1.0f, Light);

                            if (i == 0 || i == 1 || i == 2)
                            {
                                //Vector(0.0f, 0.0f, 15.0f, _vPos);
                                Vector(0.0f, 0.0f, 0.0f, _vPos);
                                b->TransformByObjectBone(_vPos, o->Owner, arr[i], _vPos);
                                VectorScale(Light, fFadeInOut, Light);
                                CreateSprite(BITMAP_LIGHT_RED, _vPos, 1.5f, Light, o);
                            }
                            else if (i == 5 || i == 12 || i == 7 || i == 8 || i == 9 || i == 14 ||
                                     i == 15 || i == 16)
                            {
                                //Vector(0.0f, 5.0f, 0.0f, _vPos);
                                Vector(0.0f, 0.0f, 0.0f, _vPos);
                                b->TransformByObjectBone(_vPos, o->Owner, arr[i], _vPos);
                                fFadeInOut =
                                    (float)(sinf(WorldTime * 0.005f) + 1.0f) * 0.25f + 0.5f;
                                VectorScale(Light, fFadeInOut, Light);
                                CreateSprite(BITMAP_LIGHT_RED, _vPos, 1.5f, Light, o);
                            }
                            else
                            {
                                Vector(0.0f, 0.0f, 0.0f, _vPos);
                                b->TransformByObjectBone(_vPos, o->Owner, arr[i], _vPos);
                                fFadeInOut = (float)(sinf(WorldTime * 0.01f) + 1.0f) * 0.3f + 0.3f;
                                VectorScale(Light, fFadeInOut, Light);
                                CreateSprite(BITMAP_LIGHT_RED, _vPos, 1.5f, Light, o);
                            }
                        }
                    }
                }
                break;
                default:
                    if (o->Type >= MODEL_SKILL_BEGIN && o->Type < MODEL_SKILL_END)
                    {
                        RenderObject(o);
                    }
                    break;
                }
            }
        }
    FlushRigidEffects(rigidRun);
}

void SessionRenderUnit::RenderAfterEffects(bool bRenderBlendMesh)
{
    if (!g_Direction.m_CKanturu.IsMayaScene(gMapManager.ContextMap()))
        return;

    for (const auto *pool : {&Effects, &g_SkillEffects.Storage()})
        for (const auto &effect : *pool)
        {
            const OBJECT *o = &effect;
            if (o->m_bRenderAfterCharacter)
            {
                if (EffectVisible(*o))
                {
                    if (bRenderBlendMesh)
                    {
                        if (o->BlendMesh == -1 || o->BlendMesh < -2)
                            continue;
                        const BMD &b = Models[o->Type];
                        if (b.NumMeshs < o->BlendMesh)
                            continue;
                    }

                    switch (o->Type)
                    {
                    case MODEL_STORM3:
                    case MODEL_MAYASTAR:
                    case MODEL_MAYAHANDSKILL:
                        RenderObject_AfterCharacter(o);
                    }
                }
            }
        }
}

namespace
{
std::array<std::array<float, 3>, 3> TorchCenters(const PARTICLE &particle)
{
    constexpr float Spacing = 10.f;
    std::array<std::array<float, 3>, 3> centers;
    for (std::size_t index = 0; index < centers.size(); ++index)
        centers[index] = {particle.Position[0], particle.Position[1],
                          particle.Position[2] - Spacing * index};
    return centers;
}
} // namespace

void SessionRenderUnit::RenderParticles(BYTE byRenderOneMore)
{
    constexpr float WaterUpperHeight = 350.f;
    constexpr float WaterLowerHeight = 300.f;
    if (!g_pOption->GetRenderAllEffects())
        return;
    ParticleDrawRun run;
    for (auto cursor = Particles.begin(), end = Particles.end(); cursor != end; ++cursor)
    {
        const int index = cursor.Index();
        const auto &particle = *cursor;
        if (byRenderOneMore == 1 && particle.Position[2] > WaterUpperHeight)
            continue;
        if (byRenderOneMore == 2 && particle.Position[2] <= WaterLowerHeight)
            continue;
        RenderParticle(particle, index, run);
    }
}

void SessionRenderUnit::RenderParticle(const PARTICLE &particle, int index, ParticleDrawRun &run)
{
    const auto *o = &particle;
    float rotation = o->Rotation;
    // Shared font/UI indexes retain the existing application-owner lookup;
    // ordinary effect textures borrow facts bound on creation and revision changes.
    const auto sharedProperties = o->RenderTexture == nullptr
                                      ? Bitmaps.GetTextureProperties(o->TexType)
                                      : std::optional<SessionTextureProperties>{};
    const auto &pBitmap =
        o->RenderTexture != nullptr ? o->RenderTexture->properties : sharedProperties;
    if (!pBitmap)
    {
        return;
    }
    float Width = pBitmap->width * o->Scale;
    float Height = pBitmap->height * o->Scale;
    if (pBitmap->components == 3)
    {
        EnableAlphaBlend();
    }
    else
    {
        EnableAlphaTest(false);
    }

    if (o->Type == BITMAP_LIGHT && o->SubType == 6)
    {
        EnableDepthTest();
    }
    if (o->Type == BITMAP_EXPLOTION && o->SubType == 5)
    {
        DisableDepthTest();
    }
    const auto draw = [&](int texture, const SessionTexturePropertiesSlot *binding,
                          const vec3_t position, float width, float height, const vec3_t light,
                          float rotation = 0.f, float u = 0.f, float v = 0.f, float uWidth = 1.f,
                          float vHeight = 1.f) {
        char components;
        if (binding != nullptr)
        {
            components = binding->properties ? binding->properties->components : 0;
            if (run.id == 0 || run.asset != binding->asset)
            {
                LegacyRender().BindTexture(binding->asset);
                run.asset = binding->asset;
                run.id = 0;
            }
        }
        else
        {
            const auto properties = Bitmaps.GetTextureProperties(texture);
            components = properties ? properties->components : 0;
            BindTexture(texture);
            run.id = 0;
        }
        if (components == 3)
            glColor3fv(light);
        else
            glColor4f(light[0], light[1], light[2],
                      texture == BITMAP_BLOOD + 1 || texture == BITMAP_FONT_HIT ? 1.f : light[0]);
        const RenderTapeParticleInstance instance{
            {position[0], position[1], position[2], width * 0.5f},
            {height * 0.5f, rotation * (Q_PI / 180.f), 0.f, 0.f},
            {u, v, uWidth, vHeight},
            LegacyRender().CurrentColor()};
        if (!LegacyRender().CanContinueQuadInstanceRun(run.id))
            run.id = LegacyRender().BeginParticleInstanceRun(g_Camera.Matrix);
        if (!LegacyRender().DrawParticleInstance(effectQuadGeometry_, instance, run.id))
            run.id = 0;
        glTexCoord2f(u, v);
    };
    int Frame;
    switch (o->Type)
    {
    case BITMAP_WATERFALL_1:
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        break;

    case BITMAP_BUBBLE:
        Frame = static_cast<int>(o->Frame) % 9;
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, 0.f,
             Frame % 3 * 0.25f + 0.005f, Frame / 3 * 0.25f + 0.005f, 0.25f - 0.01f, 0.25f - 0.01f);
        break;
    case BITMAP_SPOT_WATER:
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height * 0.125, o->Light,
             o->Angle[0], 0.f, static_cast<int>(o->Frame) % 8 * 0.125f, 1.f, 0.125f);
        break;

    case BITMAP_SPARK + 2:
        if (o->SubType == 0 || o->SubType == 2 || o->SubType == 3)
        {
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, 0.f,
                 static_cast<int>(o->Frame) % 2 * 0.5f, static_cast<int>(o->Frame) / 2 * 0.5f, 0.5f,
                 0.5f);
        }
        break;

    case BITMAP_EXPLOTION_MONO:
    case BITMAP_EXPLOTION:
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, 0.f,
             static_cast<int>(o->Frame) % 4 * 0.25f + 0.005f,
             static_cast<int>(o->Frame) / 4 * 0.25f + 0.005f, 0.25f - 0.01f, 0.25f - 0.01f);
        break;
    case BITMAP_EXPLOTION + 1:
        draw(o->TexType, o->RenderTexture, o->Position, Width * 0.25f, Height, o->Light,
             o->Angle[0], static_cast<int>(o->Frame) % 4 * 0.25f, 0.f, 0.25f, 1.f);
        break;
    case BITMAP_SUMMON_SAHAMUTT_EXPLOSION:
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, 0.f,
             static_cast<int>(o->Frame) % 4 * 0.25f + 0.005f,
             static_cast<int>(o->Frame) / 4 * 0.25f + 0.005f, 0.25f - 0.01f, 0.25f - 0.01f);
        break;
    case BITMAP_CLUD64: {
        if (o->SubType == 0 || o->SubType == 5 || o->SubType == 11)
        {
            EnableAlphaBlendMinus();
        }

        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
    }
    break;
    case BITMAP_TORCH_FIRE: {
        for (const auto &center : TorchCenters(particle))
            draw(o->Type, o->TypeTexture, center.data(), Width, Height, o->Light, rotation);
    }
    break;
    case BITMAP_GHOST_CLOUD1:
    case BITMAP_GHOST_CLOUD2: {
        draw(o->Type, o->TypeTexture, o->Position, Width, Height, o->Light, rotation);
    }
    break;
    case BITMAP_LIGHT + 3: {
        draw(o->Type, o->TypeTexture, o->Position, Width, Height, o->Light, rotation);
    }
    break;
    case BITMAP_TWINTAIL_WATER: {
        EnableAlphaBlend();
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
    }
    break;
    case BITMAP_SMOKE:
        if (o->SubType == 2 || o->SubType == 5 || o->SubType == 12 || o->SubType == 14 ||
            o->SubType == 15 || o->SubType == 20 || o->SubType == 21 || o->SubType == 29)
            EnableAlphaBlendMinus();
        if (o->SubType == 37 || o->SubType == 38 || o->SubType == 59)
            EnableAlphaBlendMinus();
        if (o->SubType == 6)
        {
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light);
        }
        else
        {
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        }
        break;
    case BITMAP_SMOKE + 1:
    case BITMAP_SMOKE + 4:
        EnableAlphaBlend3();
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        break;
    case BITMAP_ADV_SMOKE + 1:
        if (o->SubType == 2)
        {
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
            EnableAlphaBlend3();
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        }
        else
        {
            EnableAlphaBlend3();
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        }
        break;
    case BITMAP_SMOKE + 3:
        if (o->SubType == 3 || o->SubType == 4)
        {
            EnableAlphaBlendMinus();
        }
        else
        {
            EnableAlphaBlend3();
        }
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        break;
    case BITMAP_LIGHTNING:
        draw(o->TexType, o->RenderTexture, o->Position, Width * 0.25f, Height, o->Light,
             o->Angle[0], static_cast<int>(o->Frame) % 4 * 0.25f, 0.f, 0.25f, 1.f);
        break;
    case BITMAP_BLOOD + 1:
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, 0.f,
             static_cast<int>(o->Frame) % 2 * 0.5f, static_cast<int>(o->Frame) / 2 * 0.5f, 0.5f,
             0.5f);
        break;
    case BITMAP_CHROME_ENERGY2:
        draw(o->TexType, o->RenderTexture, o->Position, Width * 0.25f, Height, o->Light, rotation,
             static_cast<int>(o->Frame) % 4 * 0.25f, 0.f, 0.25f, 1.f);
        break;
    case BITMAP_FIRE_CURSEDLICH:
    case BITMAP_FIRE_HIK2_MONO:
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        break;
    case BITMAP_LEAF_TOTEMGOLEM:
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        break;
    case BITMAP_FIRE:
    case BITMAP_FIRE + 2:
    case BITMAP_FIRE + 3:
        if (o->SubType == 17 || o->SubType == 5 || o->SubType == 7 || o->SubType == 8 ||
            o->SubType == 11 || o->SubType == 12 || o->SubType == 13)
        {
            draw(o->TexType, o->RenderTexture, o->Position, Width * 0.25f, Height, o->Light,
                 rotation, static_cast<int>(o->Frame) % 4 * 0.25f, 0.f, 0.25f, 1.f);
        }
        else if (o->SubType == 18)
        {
            EnableAlphaBlend3();
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        }
        else if (o->SubType == 14 || o->SubType == 15)
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        else
        {
            draw(o->TexType, o->RenderTexture, o->Position, Width * 0.25f, Height, o->Light,
                 o->Angle[0], static_cast<int>(o->Frame) % 4 * 0.25f, 0.f, 0.25f, 1.f);
        }
        break;
    case BITMAP_FIRECRACKER: {
        int iCount = index % 8 + 22;
        vec3_t Position;
        vec3_t Light;
        int iTemp = o->LifeTime / 4 + o->SubType;
        //int iTemp = 0;
        int iColor = iTemp / 10;
        int iColorChange = iTemp % 10;
        for (int j = iCount; j >= 0; --j)
        {
            for (int k = 0; k < 3; ++k)
            {
                Position[k] = o->Position[k] - (float)j * o->Velocity[k] * 0.1f;
                Light[k] = (float)(std::min<int>(iCount - j, 10)) *
                           (o->Light[(k + iColor) % 3] * (10 - iColorChange) +
                            o->Light[(k + iColor + 1) % 3] * iColorChange) *
                           ((float)std::min<int>(o->LifeTime, 10) * 0.001f);
            }
            draw(o->TexType, o->RenderTexture, Position, Width, Height, Light, rotation);
        }
    }
    break;
    case BITMAP_FLARE:
        if (o->SubType == 11)
        {
            Width = pBitmap->width * 0.5f * o->Scale;
            Height = pBitmap->height * 0.4f;

            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        }
        else if (o->SubType != 4)
        {
            if (o->LifeTime != 60)
                draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        }
        else
        {
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        }
        break;
    case BITMAP_FLARE_BLUE:
        if (o->SubType == 0)
        {
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        }
        else if (o->SubType == 1)
        {
            Width = pBitmap->width * 0.2f * o->Scale;
            Height = pBitmap->height * 0.3f;
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        }
        break;
    case BITMAP_FLARE + 1:
        if (o->SubType == 0)
        {
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        }
        break;
    case BITMAP_LIGHT + 2:
        if (o->SubType == 3 || o->SubType == 4 || o->SubType == 6) // || o->SubType == 7)
        {
            EnableAlphaBlendMinus();
        }
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        break;
    case BITMAP_MAGIC + 1:
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        break;
    case BITMAP_CLOUD:
        switch (o->SubType)
        {
        case 10:
        case 12:
        case 7:
        case 14:
        case 16:
            EnableAlphaBlendMinus();
            break;
        case 0:
        case 8:
        case 3:
        case 18:
            if ((index % 2) == 0)
            {
                rotation = (WorldTime * 0.02f * o->TurningForce[0]) + o->StartPosition[1];
            }
            else
            {
                rotation = (WorldTime * (-0.02f) * o->TurningForce[0]) + o->StartPosition[1];
            }
            break;
        }
        if (o->SubType == 8 || o->SubType == 9 || o->SubType == 20 || o->SubType == 21)
        {
            vec3_t Light;
            Light[0] = o->Light[0] * o->Alpha;
            Light[1] = o->Light[1] * o->Alpha;
            Light[2] = o->Light[2] * o->Alpha;
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, Light, rotation);
        }
        else if (o->SubType == 17)
        {
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        }
        else if (o->SubType == 18)
        {
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        }
        else if (o->SubType == 19)
        {
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        }
        else
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        break;
    case BITMAP_SPARK:
        if (o->SubType == 10)
            EnableAlphaBlendMinus();
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        break;
    case BITMAP_FLAME:
        if (o->SubType == 11)
        {
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        }
        else
        {
            draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        }
        break;
    case BITMAP_CURSEDTEMPLE_EFFECT_MASKER:
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        break;
    case BITMAP_SHINY + 6:
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        draw(BITMAP_LIGHT, o->AdditionalTexture, o->Position, Width, Height, o->Light, rotation);
        break;
    case BITMAP_SMOKELINE2: {
        if (o->SubType == 3)
        {
            EnableAlphaBlendMinus();
        }
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
    }
    break;
    case BITMAP_SBUMB: {
        draw(o->TexType, o->RenderTexture, o->Position, Width * 0.25f, Height, o->Light, 0.0f,
             static_cast<int>(o->Frame) % 4 * 0.25f + 0.005f, 0.0f, 0.25f - 0.01f, 1.0f);
    }
    break;
    case BITMAP_DAMAGE1: {
        constexpr float DamageBrightness = 2.0f;
        vec3_t light;
        VectorScale(o->Light, DamageBrightness, light);
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, light, rotation);
    }
    break;
    case BITMAP_SWORD_EFFECT_MONO: {
        vec3_t vPos;
        VectorCopy(o->Position, vPos);
        constexpr float SwordHeight = 31.f;
        vPos[2] += SwordHeight * o->Scale;
        draw(o->TexType, o->RenderTexture, vPos, Width * 0.9f, Height * 1.1f, o->Light, rotation);
    }
    break;
    case BITMAP_DAMAGE2: {
        vec3_t vLight;
        VectorCopy(o->Light, vLight);
        VectorScale(vLight, 1.4f, vLight);
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, vLight, rotation);
    }
    break;
    case BITMAP_TRUE_FIRE:
    default:
        draw(o->TexType, o->RenderTexture, o->Position, Width, Height, o->Light, rotation);
        break;
    }
}

template <class BlurType>
void SessionRenderUnit::RenderBlurSegments(const BlurType &blur, bool fade, std::uint64_t &runId)
{
    if (blur.Number < 2)
        return;
    auto &render = LegacyRender();
    const auto allocation = render.ReserveTrailSamples(blur.Number);
    if (!allocation)
        return;
    for (int index = 0; index < blur.Number; ++index)
    {
        const auto *first = blur.P1[index];
        const auto *second = blur.P2[index];
        allocation->samples[index] = {
            {first[0], first[1], first[2]}, {second[0], second[1], second[2]}, {}};
    }
    if (!render.CanContinueQuadInstanceRun(runId))
        runId = render.BeginTrailInstanceRun({0, 0, 1, 0.f});
    int lastEndpoint = -1;
    for (int index = 0; index < blur.Number - 1; ++index)
    {
        if constexpr (std::is_same_v<BlurType, ObjectBlur>)
            if (blur.BreakAfter[index])
                continue;
        const RenderTapeTrailInstance instance{{float(blur.Number), fade ? 1.f : 0.f, float(index)},
                                               allocation->offset +
                                                   static_cast<std::uint32_t>(index),
                                               {blur.Light[0], blur.Light[1], blur.Light[2], 1.f}};
        (void)render.DrawTrailInstance(effectQuadGeometry_, instance, runId, true);
        lastEndpoint = index + 1;
    }
    // Preserve the facade latches left by the last emitted endpoint.
    if (lastEndpoint < 0)
        return;
    const float light = fade ? (blur.Number - lastEndpoint) / float(blur.Number) : 1.f;
    render.Color3(blur.Light[0] * light, blur.Light[1] * light, blur.Light[2] * light);
    render.TexCoord2(lastEndpoint / float(blur.Number), 1.f);
}

void SessionRenderUnit::RenderBlurs()
{
    std::uint64_t runId = 0;
    for (const auto &blur : g_blurs)
    {
        const int type = blur.Type;
        int texture = BITMAP_BLUR + type;
        if (type == 3)
            texture = BITMAP_BLUR2;
        else if (type == 4)
            texture = BITMAP_BLUR;
        else if (type == 5)
            texture = BITMAP_BLUR + 3;
        if (blur.Owner->Level == 0 && (type <= 3 || (type >= 5 && type <= 10)))
            EnableAlphaBlend();
        else
            EnableAlphaBlendMinus();
        if (blur.Number < 2)
            continue;
        BindTexture(texture);
        RenderBlurSegments(blur, blur.Owner->Level == 0, runId);
    }
    RenderObjectBlurs();
}

void SessionRenderUnit::RenderObjectBlurs()
{
    std::uint64_t runId = 0;
    for (const auto &blur : g_objectBlurs)
    {
        const int type = blur.Type;
        int texture = BITMAP_BLUR + type;
        if (type == 3)
            texture = BITMAP_BLUR2;
        else if (type == 4)
            texture = BITMAP_BLUR;
        else if (type == 5)
            texture = BITMAP_LAVA;
        EnableAlphaBlend();
        if (blur.Number < 2)
            continue;
        BindTexture(texture);
        RenderBlurSegments(blur, true, runId);
    }
}

// 3D Ư��ȿ�� ���� �Լ�
// *** �Լ� ����: 3

void SessionRenderUnit::RenderLeaves()
{
    if (!g_pOption->GetRenderAllEffects())
    {
        return;
    }

    const auto *definition = sessionKeeper_.WorldContextDefinition();
    const MapPresentationPolicy fallback;
    const auto &policy = definition ? definition->presentation : fallback;
    if (policy.weatherAlphaBlend)
        EnableAlphaBlend();
    else
        EnableAlphaTest();

    glColor3f(1.f, 1.f, 1.f);
#ifdef DEVIAS_XMAS_EVENT
    int iMaxLeaves;
    if (policy.extraSnowLeaves)
        iMaxLeaves = MAX_LEAVES_DOUBLE;
    else
        iMaxLeaves = MAX_LEAVES;

    for (int i = 0; i < iMaxLeaves; i++)
#else  // DEVIAS_XMAS_EVENT
    for (int i = 0; i < MAX_LEAVES; i++)
#endif // DEVIAS_XMAS_EVENT
    {
        const PARTICLE *o = &Leaves[i];
        if (o->Live && Bitmaps.FindTexture(o->Type))
        {
            BindTexture(o->Type);
            if (policy.weatherSprites)
            {
                RenderSprite(o->Type, o->Position, o->Scale, o->Scale, o->Light);
            }
            else
            {
                glPushMatrix();
                glTranslatef(o->Position[0], o->Position[1], o->Position[2]);
                float Matrix[3][4];
                AngleMatrix(o->Angle, Matrix);

                if (policy.weatherTurningForce)
                    RenderPlane3D(o->TurningForce[0], o->TurningForce[1], Matrix);
                else
                {
                    if (o->Type == BITMAP_RAIN)
                    {
                        if (!policy.weatherRainState || weather == 1)
                            RenderPlane3D(1.f, 20.f, Matrix);
                    }
                    else if (o->Type == BITMAP_FIRE_SNUFF)
                        RenderPlane3D(o->Scale * 2.f, o->Scale * 4.f, Matrix);
                    else
                    {
                        RenderPlane3D(3.f, 3.f, Matrix);
                    }
                }
                glPopMatrix();
            }
        }
    }
}

void SessionRenderUnit::RenderCircle(int Type, const vec3_t ObjectPosition, float ScaleBottom,
                                     float ScaleTop, float Height, float Rotation, float LightTop,
                                     float TextureV)
{
    BindTexture(Type);

    vec3_t Light[4];
    Vector(1.f, 1.f, 1.f, Light[0]);
    Vector(1.f, 1.f, 1.f, Light[1]);
    Vector(LightTop, LightTop, LightTop, Light[2]);
    Vector(LightTop, LightTop, LightTop, Light[3]);

    float Num = 12.f;
    for (float x = 0.f; x < Num; x += 1.f)
    {
        float UV[4][2];
        TEXCOORD(UV[0], (x) * (1.f / Num), 1.f);
        TEXCOORD(UV[1], (x + 1.f) * (1.f / Num), 1.f);
        TEXCOORD(UV[2], (x + 1.f) * (1.f / Num), 0.f);
        TEXCOORD(UV[3], (x) * (1.f / Num), 0.f);

        vec3_t Angle;
        float Matrix1[3][4];
        float Matrix2[3][4];
        Angle[0] = 0.f;
        Angle[1] = 0.f;
        Angle[2] = (x) * 30.f + Rotation;
        AngleIMatrix(Angle, Matrix1);
        Angle[2] = (x + 1.f) * 30.f + Rotation;
        AngleIMatrix(Angle, Matrix2);

        vec3_t p, Position[4];
        Vector(0.f, ScaleBottom, 0.f, p);
        VectorRotate(p, Matrix1, Position[0]);
        VectorAdd(ObjectPosition, Position[0], Position[0]);
        Vector(0.f, ScaleBottom, 0.f, p);
        VectorRotate(p, Matrix2, Position[1]);
        VectorAdd(ObjectPosition, Position[1], Position[1]);
        Vector(0.f, ScaleTop, Height, p);
        VectorRotate(p, Matrix2, Position[2]);
        VectorAdd(ObjectPosition, Position[2], Position[2]);
        Vector(0.f, ScaleTop, Height, p);
        VectorRotate(p, Matrix1, Position[3]);
        VectorAdd(ObjectPosition, Position[3], Position[3]);

        glBegin(GL_QUADS);
        for (int i = 0; i < 4; i++)
        {
            glTexCoord2f(UV[i][0], UV[i][1] + TextureV);
            glColor3fv(Light[i]);
            glVertex3fv(Position[i]);
        }
        glEnd();
    }
}

void SessionRenderUnit::RenderCircle2D(int Type, vec3_t ScreenPosition, float ScaleBottom,
                                       float ScaleTop, float Height, float Rotation, float TextureV,
                                       float TextureVScale)
{
    vec3_t ObjectPosition;
    VectorCopy(ScreenPosition, ObjectPosition);
    ObjectPosition[1] = WindowHeight - ObjectPosition[1];

    BindTexture(Type);

    float Num = 12.f;
    for (float x = 0.f; x < Num; x += 1.f)
    {
        float UV[4][2];
        TEXCOORD(UV[0], (x) * (1.f / Num), 1.f);
        TEXCOORD(UV[1], (x + 1.f) * (1.f / Num), 1.f);
        TEXCOORD(UV[2], (x + 1.f) * (1.f / Num), TextureVScale);
        TEXCOORD(UV[3], (x) * (1.f / Num), TextureVScale);

        vec3_t Angle;
        float Matrix1[3][4];
        float Matrix2[3][4];
        Angle[0] = 90.f;
        Angle[1] = 0.f;
        Angle[2] = (x) * 30.f + Rotation;
        AngleIMatrix(Angle, Matrix1);
        Angle[2] = (x + 1.f) * 30.f + Rotation;
        AngleIMatrix(Angle, Matrix2);

        vec3_t Light[4];
        float Luminosity;
        vec3_t p, Position[4];
        Vector(0.f, ScaleBottom, 0.f, p);
        VectorRotate(p, Matrix1, Position[0]);
        Luminosity =
            0.5f + 0.5f * (-Position[0][0] - Position[0][2]) /
                       sqrtf(Position[0][0] * Position[0][0] + Position[0][2] * Position[0][2]);
        Vector(Luminosity, Luminosity, Luminosity, Light[0]);
        VectorCopy(Light[0], Light[3]);
        Vector(0.f, ScaleBottom, 0.f, p);
        VectorRotate(p, Matrix2, Position[1]);
        Luminosity =
            0.5f + 0.5f * (-Position[1][0] - Position[1][2]) /
                       sqrtf(Position[1][0] * Position[1][0] + Position[1][2] * Position[1][2]);
        Vector(Luminosity, Luminosity, Luminosity, Light[1]);
        VectorCopy(Light[1], Light[2]);
        Vector(0.f, ScaleTop, Height, p);
        VectorRotate(p, Matrix2, Position[2]);
        Vector(0.f, ScaleTop, Height, p);
        VectorRotate(p, Matrix1, Position[3]);

        glBegin(GL_QUADS);
        for (int i = 0; i < 4; i++)
        {
            glTexCoord2f(UV[i][0], UV[i][1] + TextureV);
            glColor3fv(Light[i]);
            VectorAdd(ObjectPosition, Position[i], Position[i]);
            glVertex2f(Position[i][0], Position[i][1]);
        }
        glEnd();
    }
}

void SessionRenderUnit::RenderNumberPoints(const vec3_t Position, int Num, const vec3_t Color,
                                           float Alpha, float Scale)
{
    vec3_t p;
    VectorCopy(Position, p);
    vec3_t Light[4];
    VectorCopy(Color, Light[0]);
    VectorCopy(Color, Light[1]);
    VectorCopy(Color, Light[2]);
    VectorCopy(Color, Light[3]);

    char Text[32];
    itoa(Num, Text, 10);
    p[0] -= strlen(Text) * 5.f;
    unsigned int Length = strlen(Text);
    p[0] -= Length * Scale * 0.125f;
    p[1] -= Length * Scale * 0.125f;

    float sinTh = sinf((float)(ANGLE_TO_RAD * (g_Camera.Angle[2])));
    float cosTh = cosf((float)(ANGLE_TO_RAD * (g_Camera.Angle[2])));

    for (unsigned int i = 0; i < Length; i++)
    {
        float UV[4][2];
        float u = (float)(Text[i] - 48) * 16.f / 256.f;
        TEXCOORD(UV[0], u, 16.f / 32.f);
        TEXCOORD(UV[1], u + 16.f / 256.f, 16.f / 32.f);
        TEXCOORD(UV[2], u + 16.f / 256.f, 0.f);
        TEXCOORD(UV[3], u, 0.f);
        RenderSpriteUV(BITMAP_FONT + 1, p, Scale, Scale, UV, Light, Alpha);
        p[0] += Scale / 0.7071067f * cosTh / 2;
        p[1] -= Scale / 0.7071067f * sinTh / 2;
    }
}

void SessionRenderUnit::RenderPoints(BYTE byRenderOneMore)
{
    if (!g_pOption->GetRenderAllEffects())
    {
        return;
    }

    EnableAlphaTest();
    DisableDepthTest();
    for (auto cursor = Points.begin(), end = Points.end(); cursor != end; ++cursor)
    {
        const int i = cursor.Index();
        const PARTICLE *o = &*cursor;
        if (byRenderOneMore == 1)
        {
            if (o->Position[2] > 350.f)
                continue;
        }
        else if (byRenderOneMore == 2)
        {
            if (o->Position[2] <= 300.f)
                continue;
        }
        else if (o->bRepeatedly)
        {
            if (o->Position[2] <= o->fRepeatedlyHeight)
                continue;
        }

        if (o->Type > -1)
        {
            RenderNumberPoints(o->Position, o->Type, o->Angle, o->Gravity * 0.4f, o->Scale);
        }
        else
        {
            RenderNumber(o->Position, o->Type, o->Angle, o->Gravity * 0.4f, o->Scale);
        }
    }
}

void SessionRenderUnit::RenderPointers()
{
    EnableAlphaBlend();
    for (auto cursor = Pointers.begin(), end = Pointers.end(); cursor != end; ++cursor)
    {
        const int i = cursor.Index();
        const PARTICLE *o = &*cursor;
        const auto texture = Bitmaps.GetTextureProperties(o->Type);
        if (texture.has_value() && texture->components == 3)
            EnableAlphaBlend();
        else
            EnableAlphaTest();
        RenderTerrainAlphaBitmap(o->Type, o->Position[0], o->Position[1], o->Scale, o->Scale,
                                 o->Light, -o->Angle[2], o->Alpha);
    }
}

void SessionRenderUnit::RenderSprite(const OBJECT *o, const OBJECT *Owner)
{
    SessionSprite sprite;
    sprite.Type = o->Type;
    sprite.SubType = o->SubType;
    sprite.AnimationFrame = o->AnimationFrame;
    sprite.Scale = o->Scale;
    sprite.Angle[2] = o->Angle[2];
    VectorCopy(o->Position, sprite.Position);
    VectorCopy(o->Light, sprite.Light);
    (void)RenderSprite(&sprite, 0);
}

std::uint64_t SessionRenderUnit::RenderSprite(const SessionSprite *o, std::uint64_t runId)
{
    float Scale = o->AnimationFrame * o->Scale;

    const auto pBitmap = Bitmaps.GetTextureProperties(o->Type);
    if (!pBitmap)
    {
        return 0;
    }
    float Width = pBitmap->width * Scale;
    float Height = pBitmap->height * Scale;

    if (o->Type == BITMAP_FORMATION_MARK)
    {
        float u = 0.0f, v = 0.0f, uw, vw;
        uw = 0.33f;
        vw = 0.33f;
        switch (o->SubType)
        {
        case 0:
            u = 0.f;
            v = 0.f;
            break;

        case 1:
            u = 0.33f;
            v = 0.f;
            break;

        case 2:
            u = 0.66f;
            v = 0.f;
            break;

        case 3:
            u = 0.f;
            v = 0.33f;
            break;

        case 4:
            u = 0.33f;
            v = 0.33f;
            break;

        case 5:
            u = 0.66f;
            v = 0.33f;
            break;

        case 6:
            u = 0.f;
            v = 0.66f;
            break;

        case 7:
            u = 0.33f;
            v = 0.66f;
            break;
        }

        return RenderSpriteInstance(o->Type, o->Position, 64, 64, o->Light, o->Angle[2], u, v, uw,
                                    vw, pBitmap->components, runId);
    }
    return RenderSpriteInstance(o->Type, o->Position, Width, Height, o->Light, o->Angle[2], 0.f,
                                0.f, 1.f, 1.f, pBitmap->components, runId);
}

void SessionRenderUnit::RenderSprites(BYTE byRenderOneMore)
{
    if (!g_pOption->GetRenderAllEffects())
    {
        return;
    }

    int activeTexture = -1;
    int activeBlendState = -1;
    std::uint64_t activeRunId = 0;
    for (std::size_t i = 0; i < Sprites.used; ++i)
    {
        const auto *o = &Sprites[i];
        if (byRenderOneMore == 1)
        {
            if (o->Position[2] > 350.f)
            {
                continue;
            }
        }
        else if (byRenderOneMore == 2)
        {
            if (o->Position[2] <= 300.f)
            {
                continue;
            }
        }

        if (o->Live)
        {
            int blendState = -1;
            if (o->Type == BITMAP_FORMATION_MARK)
            {
                EnableAlphaTest();
                blendState = 2;
            }
            else if (o->SubType == 0)
            {
                EnableAlphaBlend();
                blendState = 0;
            }
            else if (o->SubType == 1)
            {
                EnableAlphaBlendMinus();
                blendState = 1;
            }
            else if (o->SubType == 2)
            {
                EnableAlphaTest();
                blendState = 2;
            }
            else if (o->SubType == 3)
            {
                EnableAlphaBlend2();
                blendState = 3;
            }
            if (o->Type != activeTexture || blendState != activeBlendState || blendState < 0)
            {
                activeRunId = 0;
            }
            activeRunId = RenderSprite(o, activeRunId);
            activeTexture = activeRunId != 0 ? o->Type : -1;
            activeBlendState = activeRunId != 0 ? blendState : -1;
        }
    }
}

// CharacterScene.cpp - Character selection scene implementation

// Forward declaration
BOOL Util_CheckOption(std::wstring lpszCommandLine, wchar_t cOption, std::wstring &lpszString);

/**
 * @brief Renders special effects for the selected character (aurora, particles).
 */
void SessionRenderUnit::RenderSelectedCharacterEffects()
{
    if (SelectedHero == -1)
        return;

    const OBJECT *o = &CharactersClient[SelectedHero].Object;
    if (!o->Live)
        return;

    // Aurora luminance pulses between (BASE - AMPLITUDE) and (BASE + AMPLITUDE)
    // with period ~2π / FREQUENCY ms.
    constexpr float AURORA_FREQUENCY = 0.0015f;
    constexpr float AURORA_AMPLITUDE = 0.3f;
    constexpr float AURORA_BASE_LUMINANCE = 0.5f;

    vec3_t vLight;
    Vector(1.0f, 1.0f, 1.f, vLight);
    float fLumi = sinf(WorldTime * AURORA_FREQUENCY) * AURORA_AMPLITUDE + AURORA_BASE_LUMINANCE;
    Vector(fLumi * vLight[0], fLumi * vLight[1], fLumi * vLight[2], vLight);

    EnableAlphaBlend();
    RenderTerrainAlphaBitmap(BITMAP_GM_AURORA, o->Position[0], o->Position[1], 1.8f, 1.8f, vLight,
                             WorldTime * 0.01f);
    RenderTerrainAlphaBitmap(BITMAP_GM_AURORA, o->Position[0], o->Position[1], 1.2f, 1.2f, vLight,
                             -WorldTime * 0.01f);
    DisableAlphaBlend();
}

void SessionLegacyCalls::RenderSelectedCharacterEffects()
{
    sessionKeeper_.Renderer()->RenderSelectedCharacterEffects();
}

// OMF-01767
// OMF-01769
// OMF-01770

void MoveCharacter(CHARACTER *c, OBJECT *o);

void SessionRenderUnit::RenderBossLaser(const OBJECT &object)
{
    constexpr int segmentCount = 20;
    vec3_t position, light;
    VectorCopy(object.Position, position);
    for (int segment = 0; segment < segmentCount; ++segment)
    {
        const float rotation = object.HeadAngle[0] - (segment + 1) * object.HeadAngle[1];
        if (object.SubType == 1)
        {
            Vector(1.f, 1.f, 1.f, light);
            CreateSprite(BITMAP_FIRE + 1, position, object.Scale, light, &object, rotation);
            EnableAlphaBlend();
            Vector(0.2f, 0.f, 0.f, light);
            RenderTerrainAlphaBitmap(BITMAP_SMOKE, position[0], position[1], 4.f, 4.f, light,
                                     -rotation);
            DisableAlphaBlend();
        }
        else if (object.SubType == 2)
        {
            Vector(0.f, 0.2f, 1.f, light);
            CreateSprite(BITMAP_FIRE + 1, position, object.Scale, light, &object, rotation);
        }
        else
            CreateSprite(BITMAP_SPARK + 1, position, object.Scale, object.Light, &object);
        VectorAdd(position, object.Direction, position);
        if (IsBattleCastleStart() &&
            (TERRAIN_ATTRIBUTE(position[0], position[1]) & TW_NOATTACKZONE))
            return;
    }
}

bool SessionRenderUnit::EffectVisible(const OBJECT &object)
{
    return (gMapManager.ContextMap() == WD_39KANTURU_3RD &&
            (object.Type == MODEL_STORM3 || object.Type == MODEL_MAYASTAR)) ||
           TestFrustrum(object.Position, 400.f);
}

// Draw-only billboards append after the tick batch; they do not call visual advancement.
int SessionRenderUnit::CreateSprite(int type, const vec3_t position, float scale,
                                    const vec3_t light, const OBJECT *owner, float rotation,
                                    int subType)
{
    if (!g_pOption->GetRenderAllEffects())
        return false;
    return Render::Sprites::AppendPreparedSprite(Sprites, type, position, scale, light, owner,
                                                 rotation, subType);
}
