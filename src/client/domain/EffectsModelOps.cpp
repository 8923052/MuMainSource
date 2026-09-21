#include "domain/EffectsUpdate.h"
#include "support/CoreMath.h"
#include "session/SessionKeeper.h"
#include "session/SessionGameplay.h"
#include "session/SessionRender.h"
#include "render/World.h"
#include "domain/CharacterPresentation.h"
#include "domain/WorldSimulation.h"
#include "render/ModelResources.h"
#include "render/ModelGeometry.h"
#include "app/ApplicationLoopFrame.h"
#include "domain/CharacterSystem.h"
#include "render/Textures.h"
#include "data/Localization.h"
#include "domain/ItemsSkills.h"
#include "data/WorldData.h"
#include "render/Terrain.h"
#include "domain/MovementAI.h"
#include "app/ApplicationAudio.h"
#include "ui/session/UiSessionLogic.h"
#include "domain/Events.h"
#include "domain/MapSimulation.h"

namespace
{
bool HasWingVisual(int type)
{
    switch (type)
    {
    case MODEL_WINGS_OF_DARKNESS:
    case MODEL_WING_OF_STORM:
    case MODEL_WING_OF_ETERNAL:
    case MODEL_WING_OF_ILLUSION:
    case MODEL_WING_OF_RUIN:
    case MODEL_WING_OF_DIMENSION:
        return true;
    default:
        return false;
    }
}
} // namespace

void SessionGameplayUnit::EmitStandalonePartVisual(OBJECT &owner, const ObjectDrawInput &draw,
                                                   int type, const vec3_t light)
{
    if (draw.alpha <= 0.01f)
        return;
    SessionRandom::PresentationScope visualOrigin(sessionKeeper_.RandomForConstruction());
    auto &model = Models[type];
    model.CurrentAction = draw.action;
    model.BodyScale = draw.scale;
    ItemHeight(type, &model);
    VectorCopy(draw.position, model.BodyOrigin);
    model.Animation(BoneTransform, draw.animationFrame, draw.priorAnimationFrame, draw.priorAction,
                    draw.angle, draw.headAngle, false, false);
    EmitWingItemVisual(owner, draw, type, BoneTransform);
    if (!draw.shadow && draw.HasBuff(eDeBuff_Stun))
    {
        DeleteEffect(BITMAP_SKULL, &owner, 5);
        vec3_t position, angle, effectLight;
        VectorCopy(draw.position, position);
        VectorCopy(draw.angle, angle);
        VectorCopy(light, effectLight);
        CreateEffect(BITMAP_SKULL, position, angle, effectLight, 5, &owner);
    }
}

void SessionGameplayUnit::AdvanceDroppedItemVisual(OBJECT &item, int index)
{
    if (!HasWingVisual(item.Type) && (item.EnableShadow || !item.m_BuffMap.isBuff(eDeBuff_Stun)))
        return;
    ObjectDrawInput draw(&item);
    draw.position[2] = TheMapProcess().ItemDrawHeight(item, index);
    vec3_t light;
    RequestTerrainLight(draw.position[0], draw.position[1], light);
    VectorAdd(light, draw.light, light);
    EmitStandalonePartVisual(item, draw, item.Type, light);
}

void SessionGameplayUnit::AdvanceWeaponEffect(OBJECT &effect)
{
    switch (effect.Type)
    {
    case MODEL_SKILL_WHEEL2: {
        constexpr float SpinPerTick = 30.f;
        effect.Direction[2] -= SpinPerTick * FPS_ANIMATION_FACTOR;
        break;
    }
    case MODEL_SKILL_FURY_STRIKE:
        if (effect.LifeTime <= 10.f || effect.Kind != 0)
            return;
        break;
    case MODEL_SPEARSKILL: {
        if (effect.EnableShadow || !effect.m_BuffMap.isBuff(eDeBuff_Stun))
            return;
        ObjectDrawInput draw(&effect);
        draw.blendLight = 1.f;
        vec3_t light;
        constexpr float LightPerLifetime = 0.05f;
        VectorScale(draw.light, effect.LifeTime * LightPerLifetime, light);
        EmitStandalonePartVisual(effect, draw, MODEL_SPEARSKILL, light);
        return;
    }
    default:
        return;
    }
    const int type = effect.Owner->Weapon + MODEL_SWORD;
    if (!HasWingVisual(type))
        return;
    SessionRandom::PresentationScope visualOrigin(sessionKeeper_.RandomForConstruction());
    auto draw = sessionKeeper_.Renderer()->PrepareItemDraw(effect, type);
    if (effect.Type == MODEL_SKILL_WHEEL2)
    {
        draw.angle[2] += effect.Direction[2];
        draw.angle[1] = 90.f;
        constexpr float WheelHeight = 100.f;
        draw.position[2] += WheelHeight;
    }
    else
    {
        draw.owner = nullptr;
        draw.contrastEnable = false;
        draw.renderShadow = false;
        draw.skillCount = 0;
    }
    vec3_t light;
    RequestTerrainLight(draw.position[0], draw.position[1], light);
    VectorAdd(light, draw.light, light);
    EmitStandalonePartVisual(effect, draw, type, light);
}

void SessionGameplayUnit::EmitWingItemVisual(OBJECT &owner, const ObjectDrawInput &draw, int Type,
                                             const vec34_t *bones,
                                             const CharacterLinkedItemVisual *linkedItem,
                                             const CharacterDrawInput *parentDraw)
{
    EmitWingItemParticles(owner, draw, Type, bones, linkedItem, parentDraw);
    auto *o = &owner;
    auto *b = &Models[Type];
    if (draw.type == MODEL_WINGS_OF_DARKNESS)
    {
        vec3_t posCenter, p, Light;
        float Scale = sinf(WorldTime * 0.004f) * 0.3f + 0.3f;

        Scale = (Scale * 10.f) + 20.f;

        Vector(0.6f, 0.3f, 0.8f, Light);

        Vector(0.f, 0.f, 0.f, p);

        for (int i = 0; i < 5; ++i)
        {
            b->TransformPosition(bones[22 - i], p, posCenter, true);

            CreateSprite(BITMAP_FLARE_BLUE, posCenter, Scale / 28.f, Light, o);
        }

        for (int i = 0; i < 5; ++i)
        {
            b->TransformPosition(bones[7 - i], p, posCenter, true);

            CreateSprite(BITMAP_FLARE_BLUE, posCenter, Scale / 28.f, Light, o);
        }
    }
    else if (Type == MODEL_WING_OF_STORM)
    {
        vec3_t vRelativePos, vPos, vLight;
        Vector(0.f, 0.f, 0.f, vRelativePos);
        Vector(0.f, 0.f, 0.f, vPos);
        Vector(0.f, 0.f, 0.f, vLight);

        float fLuminosity = absf(sinf(WorldTime * 0.0004f)) * 0.4f;
        Vector(0.5f + fLuminosity, 0.5f + fLuminosity, 0.5f + fLuminosity, vLight);
        int iBone[] = {9,  20, 19, 10, 18, 28, 27, 36, 35, 38, 37,  53, 48,
                       62, 70, 72, 71, 78, 79, 80, 87, 90, 91, 106, 102};
        float fScale = 0.f;

        for (int i = 0; i < 25; ++i)
        {
            b->TransformPosition(bones[iBone[i]], vRelativePos, vPos, true);
            fScale = 0.5f; // (WorldRandom()%10) * 0.05f + 0.3f;
            CreateSprite(BITMAP_CLUD64, vPos, fScale, vLight, o, WorldTime * 0.01f, 1);
        }

        int iBoneLight[] = {64, 61, 69, 77, 86, 98, 97, 99, 104, 103, 105,
                            12, 8,  17, 26, 34, 52, 44, 51, 50,  49,  45};

        fScale = absf(sinf(WorldTime * 0.003f)) * 0.2f;

        for (int i = 0; i < 22; ++i)
        {
            b->TransformPosition(bones[iBoneLight[i]], vRelativePos, vPos, true);
            if (iBoneLight[i] == 12 || iBoneLight[i] == 64 || iBoneLight[i] == 98 ||
                iBoneLight[i] == 52)
            {
                Vector(0.9f, 0.0f, 0.0f, vLight);
                CreateSprite(BITMAP_LIGHT, vPos, fScale + 1.4f, vLight, o);
            }
            else
            {
                Vector(0.8f, 0.5f, 0.2f, vLight);
                CreateSprite(BITMAP_LIGHT, vPos, fScale + 0.3f, vLight, o);
            }
        }
    }
    else if (Type == MODEL_WING_OF_ETERNAL)
    {
        vec3_t p, Position, Light;
        Vector(0.f, 0.f, 0.f, p);
        float Scale = absf(sinf(WorldTime * 0.003f)) * 0.2f;
        float Luminosity = absf(sinf(WorldTime * 0.003f)) * 0.3f;

        Vector(0.5f + Luminosity, 0.5f + Luminosity, 0.6f + Luminosity, Light);
        //int iRedFlarePos[] = { 25, 32, 53, 15, 9, 35 };
        int iRedFlarePos[] = {24, 31, 15, 8, 53, 35};
        for (int i = 0; i < 6; ++i)
        {
            b->TransformPosition(bones[iRedFlarePos[i]], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, Scale + 1.3f, Light, o);
        }

        Vector(0.1f, 0.1f, 0.9f, Light);
        //int iGreenFlarePos[] = { 23, 22, 24, 34, 5, 31, 14, 12, 27, 8, 6, 7, 16, 13, 56, 37, 58, 40, 39, 38 };
        int iGreenFlarePos[] = {22, 23, 25, 29, 30, 28, 32, 13, 16,
                                14, 12, 9,  7,  6,  57, 58, 40, 39};

        for (int i = 0; i < 18; ++i)
        {
            b->TransformPosition(bones[iGreenFlarePos[i]], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, Scale + 1.5f, Light, o);
        }
        int iGreenFlarePos2[] = {56, 38, 51, 45};

        for (int i = 0; i < 4; ++i)
        {
            b->TransformPosition(bones[iGreenFlarePos2[i]], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, Scale + 0.5f, Light, o);
        }
    }
    else if (Type == MODEL_WING_OF_ILLUSION)
    {
        vec3_t p, Position, Light;
        Vector(0.f, 0.f, 0.f, p);
        float Scale = absf(sinf(WorldTime * 0.002f)) * 0.2f;
        float Luminosity = absf(sinf(WorldTime * 0.002f)) * 0.4f;

        Vector(0.5f + Luminosity, 0.0f + Luminosity, 0.0f + Luminosity, Light);
        int iRedFlarePos[] = {5, 6, 7, 8, 18, 19, 23, 24, 25, 27, 37, 38};
        for (int i = 0; i < 12; ++i)
        {
            b->TransformPosition(bones[iRedFlarePos[i]], p, Position, true);
            CreateSprite(BITMAP_FLARE, Position, Scale + 0.6f, Light, o);
        }

        Vector(0.0f + Luminosity, 0.5f + Luminosity, 0.0f + Luminosity, Light);
        int iGreenFlarePos[] = {4, 9, 13, 14, 26, 32, 31, 33};

        for (int i = 0; i < 8; ++i)
        {
            b->TransformPosition(bones[iGreenFlarePos[i]], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 1.3f, Light, o);
        }

        Vector(1.0f, 1.0f, 1.0f, Light);
        float fLumi = (sinf(WorldTime * 0.004f) + 1.0f) * 0.05f;
        Vector(0.8f + fLumi, 0.8f + fLumi, 0.3f + fLumi, Light);
        CreateSprite(BITMAP_LIGHT, Position, 0.4f, Light, o, 0.5f);
    }
    else if (Type == MODEL_WING_OF_RUIN)
    {
        vec3_t p, Position, Light;
        Vector(0.f, 0.f, 0.f, p);
        float Scale = absf(sinf(WorldTime * 0.003f)) * 0.2f;
        float Luminosity = absf(sinf(WorldTime * 0.003f)) * 0.3f;

        Vector(0.7f + Luminosity, 0.5f + Luminosity, 0.8f + Luminosity, Light);
        int iRedFlarePos[] = {6, 15, 24, 56, 47, 38};
        for (int i = 0; i < 6; ++i)
        {
            b->TransformPosition(bones[iRedFlarePos[i]], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, Scale + 1.5f, Light, o);
        }
    }
    else if (Type == MODEL_WING_OF_DIMENSION)
    {
        vec3_t p, Position, Light;
        Vector(0.f, 0.f, 0.f, p);
        float Scale = absf(sinf(WorldTime * 0.002f)) * 0.2f;
        float Luminosity = absf(sinf(WorldTime * 0.002f)) * 0.4f;

        Vector((1.0f + Luminosity) / 2.f, (0.7f + Luminosity) / 2.f, (0.2f + Luminosity) / 2.f,
               Light);
        int iFlarePos0[] = {7, 30, 31, 43, 8, 20};

        int icnt;
        for (icnt = 0; icnt < 2; ++icnt)
        {
            b->TransformPosition(bones[iFlarePos0[icnt]], p, Position, true);
            CreateSprite(BITMAP_FLARE, Position, Scale + 2.0f, Light, o);
        }
        Vector((1.0f + Luminosity) / 4.f, (0.7f + Luminosity) / 4.f, (0.2f + Luminosity) / 4.f,
               Light);
        for (; icnt < 6; ++icnt)
        {
            b->TransformPosition(bones[iFlarePos0[icnt]], p, Position, true);
            CreateSprite(BITMAP_FLARE, Position, Scale + 0.5f, Light, o);
        }

        Vector((0.5f + Luminosity) / 2.f, (0.1f + Luminosity) / 2.f, (0.4f + Luminosity) / 2.f,
               Light);
        int iGreenFlarePos[] = {29, 38, 42, 19, 15, 6};

        for (int i = 0; i < 6; ++i)
        {
            b->TransformPosition(bones[iGreenFlarePos[i]], p, Position, true);
            CreateSprite(BITMAP_FLARE, Position, Scale + 2.0f, Light, o);
        }
    }
}

namespace
{
bool HasEffectFlicker(const OBJECT &effect)
{
    switch (effect.Type)
    {
    case BITMAP_FIRE_HIK2_MONO:
    case BITMAP_CLOUD:
    case MODEL_RAKLION_BOSS_MAGIC:
    case MODEL_BLOW_OF_DESTRUCTION:
    case MODEL_EMPIREGUARDIAN_BLOW_OF_DESTRUCTION:
        return effect.SubType == 0;
    case MODEL_EFFECT_BROKEN_ICE0:
    case MODEL_EFFECT_BROKEN_ICE1:
    case MODEL_EFFECT_BROKEN_ICE2:
    case MODEL_EFFECT_BROKEN_ICE3:
        return effect.SubType == 1;
    case BITMAP_FLAME:
        return effect.SubType != 3 && effect.SubType != 6;
    default:
        return false;
    }
}

void InitializeSkullEffect(OBJECT *o)
{
    o->LifeTime = 1000;
    if (1 == o->SubType)
    {
        o->LifeTime -= (60);
    }
    else if (2 == o->SubType)
    {
        o->LifeTime = 8;
        o->Position[2] += (150.f);
    }
    else if (o->SubType == 4)
    {
        o->LifeTime = 20;
        o->Velocity = 2.f;
        Vector(0.f, -10.f, 0.f, o->Direction);
    }
}

constexpr BYTE g_byUpperBoneLocation[7] = {25, 26, 27, 20, 34, 35, 36};
} // namespace

bool SessionGameplayUnit::CheckCharacterRange(OBJECT *so, float Range, short PKKey, BYTE Kind)
{
    for (int i = 0; i < CharactersClient.Size(); i++)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        CHARACTER *c = &CharactersClient[i];
        OBJECT *o = &c->Object;

        if (!o->Live || (Kind != 0 && Kind == o->Kind))
            continue;

        if (so->Owner != o)
        {
            float dx = so->Position[0] - o->Position[0];
            float dy = so->Position[1] - o->Position[1];
            float Distance = sqrtf(dx * dx + dy * dy);

            if ((c != Hero || (Kind == KIND_MONSTER)) && c->Dead == 0 && Distance <= Range)
            {
                return true;
            }
        }
    }
    return false;
}

bool SessionLegacyCalls::CheckCharacterRange(OBJECT *source, float range, short pkKey, BYTE kind)
{
    return sessionKeeper_.Gameplay()->CheckCharacterRange(source, range, pkKey, kind);
}

void SessionGameplayUnit::CreateHealing(OBJECT *o)
{
    for (auto birth : Emissions(FPS_ANIMATION_FACTOR))
    {
        vec3_t origin;
        o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, origin);
        for (int j = 0; j < 3; ++j)
        {
            vec3_t angle{float(WorldRandom() % 90), 0.f, float(WorldRandom() % 360)};
            vec3_t local{0.f, -200.f, 0.f}, position;
            float matrix[3][4];
            AngleMatrix(angle, matrix);
            VectorRotate(local, matrix, position);
            VectorSubtract(origin, position, position);
            position[2] += 120.f;
            CreateJoint(BITMAP_JOINT_HEALING, position, origin, angle,
                        o->SubType == 5 ? 11 : o->SubType, o->Owner, 5.f);
        }
    }
}

void SessionGameplayUnit::CreateForce(OBJECT *o, vec3_t Pos)
{
    vec3_t Angle, p, Position;
    float Matrix[3][4];
    Vector(0.f, -500.f, 0.f, p);
    for (int j = 0; j < 3; j++)
    {
        Vector((float)(WorldRandom() % 90), 0.f, (float)(WorldRandom() % 360), Angle);
        AngleMatrix(Angle, Matrix);
        VectorRotate(p, Matrix, Position);
        VectorSubtract(Pos, Position, Position);
        Position[2] += 120.f;
        CreateJoint(BITMAP_JOINT_HEALING, Position, Pos, Angle, 8, o, 10.f);
    }
}

void SessionGameplayUnit::EffectDestructor(OBJECT *o)
{
    switch (o->Type)
    {
    case MODEL_EFFECT_FLAME_STRIKE:
        RemoveObjectBlurs(o, 1);
        RemoveObjectBlurs(o, 2);
        RemoveObjectBlurs(o, 3);
        break;
    case MODEL_SUMMONER_SUMMON_LAGUL:
        for (int i = 48; i <= 53; ++i)
        {
            DeleteJoint(BITMAP_JOINT_ENERGY, o, i);
        }
        break;
    }

    RetireEffect(o);
    o->Owner = NULL;
}

void SessionLegacyCalls::EffectDestructor(OBJECT *object)
{
    sessionKeeper_.Gameplay()->EffectDestructor(object);
}

void SessionGameplayUnit::TerminateOwnerEffectObject(int iOwnerObjectType)
{
    if (iOwnerObjectType < 0)
    {
        return;
    }

    for (auto cursor = Effects.begin(), end = Effects.end(); cursor != end; ++cursor)
    {
        const int i = cursor.Index();
        OBJECT *o = &*cursor;

        if (o->Owner != NULL && o->Type == MODEL_AIR_FORCE && o->Owner->Type == iOwnerObjectType)
        {
            o->Owner = NULL;
        }
    }
}

void SessionLegacyCalls::TerminateOwnerEffectObject(int ownerObjectType)
{
    sessionKeeper_.Gameplay()->TerminateOwnerEffectObject(ownerObjectType);
}

bool SessionGameplayUnit::DeleteEffect(int Type, OBJECT *Owner, int iSubType)
{
    bool bDelete = false;
    for (auto cursor = Effects.begin(), end = Effects.end(); cursor != end; ++cursor)
    {
        const int i = cursor.Index();
        OBJECT *o = &*cursor;
        if (o->Type == Type)
        {
            if (iSubType == -1 || iSubType == o->SubType)
            {
                if (o->Owner == Owner)
                {
                    EffectDestructor(o);
                    bDelete = true;
                }
            }
        }
    }

    return g_SkillEffects.DeleteEffect(Type, Owner, iSubType) || bDelete;
}

void SessionGameplayUnit::RetireCharacterEffectTargets(std::span<OBJECT *const> targets)
{
    if (targets.empty())
        return;
    const std::unordered_set<const OBJECT *> retired(targets.begin(), targets.end());
    // Cold retirement invalidates pet targets once for the entire departing batch.
    for (int index = 0; index < CharactersClient.Size(); ++index)
    {
        if (!CharactersClient.IsValidIndex(index))
            continue;
        auto &pet = CharactersClient.WorldVisuals(index).darkSpirit;
        if (pet)
            pet->ForgetTargets(retired);
    }
    for (auto &effect : Effects)
        if (effect.Owner && retired.contains(effect.Owner))
            EffectDestructor(&effect);
    for (auto &value : g_SkillEffects.Storage())
    {
        auto *effect = &value;
        if (effect->Owner && retired.contains(effect->Owner))
            EffectDestructor(effect);
    }
    for (auto &joint : Joints)
    {
        if (!joint.Target || !retired.contains(joint.Target))
            continue;
        Joints.Retire(joint);
        joint.Target = nullptr;
        joint.Tails.Clear();
    }
    for (auto &particle : Particles)
    {
        if (!particle.Target || !retired.contains(particle.Target))
            continue;
        Particles.Retire(particle);
        particle.Target = nullptr;
        particle.SocketBinding.reset();
    }
    for (auto &sprite : Sprites.objects)
    {
        if (!sprite.Owner || !retired.contains(sprite.Owner))
            continue;
        sprite.Live = false;
        sprite.Owner = nullptr;
    }
    for (auto *target : targets)
        RemoveObjectBlurs(target);
    for (auto &blur : g_blurs)
    {
        if (blur.Owner && retired.contains(&blur.Owner->Object))
            RetireBlur(g_blurs, blur);
    }
}

void SessionGameplayUnit::DeleteEffect(int efftype)
{
    for (auto cursor = Effects.begin(), end = Effects.end(); cursor != end; ++cursor)
    {
        const int i = cursor.Index();
        OBJECT *o = &*cursor;
        if (o->Type == efftype)
        {
            EffectDestructor(o);
        }
    }

    g_SkillEffects.DeleteEffect(efftype);
}

bool SessionGameplayUnit::DeleteParticle(int iType)
{
    if (!g_pOption->GetRenderAllEffects())
    {
        return false;
    }

    for (auto cursor = Particles.begin(), end = Particles.end(); cursor != end; ++cursor)
    {
        const int i = cursor.Index();
        PARTICLE *o = &*cursor;
        if (o->Type == iType)
        {
            Particles.Retire(i);
            o->SocketBinding.reset();
            o->Target = NULL;
            return true;
        }
    }

    return false;
}

bool SessionLegacyCalls::DeleteParticle(int type)
{
    return sessionKeeper_.Gameplay()->DeleteParticle(type);
}

bool SessionGameplayUnit::SearchEffect(int iType, OBJECT *pOwner, int iSubType)
{
    for (auto cursor = Effects.begin(), end = Effects.end(); cursor != end; ++cursor)
    {
        const int i = cursor.Index();
        OBJECT *o = &*cursor;
        if (o->Type == iType && o->Owner == pOwner)
        {
            if (iSubType == -1 || o->SubType == iSubType)
            {
                return true;
            }
        }
    }

    return g_SkillEffects.SearchEffect(iType, pOwner, iSubType);
    return false;
}

BOOL SessionGameplayUnit::FindSameEffectOfSameOwner(int iType, OBJECT *pOwner)
{
    for (auto cursor = Effects.begin(), end = Effects.end(); cursor != end; ++cursor)
    {
        const int i = cursor.Index();
        OBJECT *o = &*cursor;
        if (o->Type == iType && o->Owner == pOwner)
        {
            return (TRUE);
        }
    }

    return g_SkillEffects.FindSameEffectOfSameOwner(iType, pOwner);
    return (FALSE);
}

void SessionGameplayUnit::CheckTargetRange(OBJECT *o)
{
    const OBJECT *target = o->Owner;
    if (!target || !target->Live)
        return;
    constexpr float ContactRange = 100.f;
    const float dx = o->Position[0] - target->Position[0];
    const float dy = o->Position[1] - target->Position[1];
    const float range = sqrtf(dx * dx + dy * dy);
    if (range <= ContactRange)
        ApplyProjectileImpact(*o, range);
}

void SessionGameplayUnit::ApplyProjectileImpact(OBJECT &projectile, float Range)
{
    auto *o = &projectile;
    if (o->ProjectileImpactApplied)
        return;
    if (o->Type == MODEL_WOOSISTONE && Range > 30.f)
    {
        o->LifeTime = 100.f;
        return;
    }
    o->ProjectileImpactApplied = true;
    o->LifeTime = 1;
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    switch (o->Type)
    {
    case MODEL_ARROW:
    case MODEL_ARROW_STEEL:
    case MODEL_ARROW_THUNDER:
    case MODEL_ARROW_LASER:
    case MODEL_ARROW_V:
    case MODEL_ARROW_SAW:
    case MODEL_ARROW_NATURE:
    case MODEL_ARROW_WING:
    case MODEL_ARROW_BOMB:
    case MODEL_LACEARROW:
    case MODEL_DARK_SCREAM:
    case MODEL_DARK_SCREAM_FIRE:
    case MODEL_ARROW_SPARK:
    case MODEL_ARROW_RING:
    case MODEL_ARROW_TANKER:
    case MODEL_ARROW_DARKSTINGER:
    case MODEL_ARROW_GAMBLE:
        break;
    case MODEL_FIRE:
        if (o->SubType == 1)
        {
            for (int j = 0; j < 2; j++)
                CreateEffect(MODEL_STONE1 + WorldRandom() % 2, o->Position, o->Angle, o->Light);
        }
        break;

    case BITMAP_ENERGY:
        CreateParticle(BITMAP_SPARK + 1, o->Position, o->Angle, Light, 1, 6.f);
        break;

    case MODEL_LIGHTNING_ORB: {
        CreateEffect(MODEL_LIGHTNING_ORB, o->Position, o->Angle, o->Light, 1);
    }
    break;

    case MODEL_SNOW1: {
        for (int j = 0; j < 2; j++)
        {
            CreateEffect(MODEL_SNOW2 + WorldRandom() % 2, o->Position, o->Angle, o->Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, o->Light);
        }
        PlayBuffer(SOUND_BREAK01);
    }
    break;
    case MODEL_WOOSISTONE:
        for (int j = 0; j < 20; ++j)
        {
            CreateEffect(MODEL_WOOSISTONE, o->Position, o->Angle, o->Light, 1);
            CreateParticle(BITMAP_FIRE, o->Position, o->Angle, o->Light, 0, 1, o);
        }
        PlayBuffer(SOUND_BREAK01);
        break;
    }
}

void SessionGameplayUnit::CreateEffectFpsChecked(int Type, vec3_t Position, vec3_t Angle,
                                                 vec3_t Light, int SubType, OBJECT *Owner,
                                                 short PKKey, WORD SkillIndex, WORD Skill,
                                                 WORD SkillSerialNum, float Scale, int sTargetIndex)
{
    for (auto birthTime : Emissions(FPS_ANIMATION_FACTOR))
    {
        CreateEffect(Type, Position, Angle, Light, SubType, Owner, PKKey, SkillIndex, Skill,
                     SkillSerialNum, Scale, sTargetIndex);
    }
}

void SessionGameplayUnit::CreateEffect(int Type, vec3_t Position, vec3_t Angle, vec3_t Light,
                                       int SubType, OBJECT *Owner, short PKKey, WORD SkillIndex,
                                       WORD Skill, WORD SkillSerialNum, float Scale,
                                       int sTargetIndex)
{
    auto &pool =
        g_SkillEffects.IsSkillEffect(Type, Position, Angle, Light, SubType, Owner, PKKey,
                                     SkillIndex, Skill, SkillSerialNum, Scale, sTargetIndex)
            ? g_SkillEffects.Storage()
            : Effects;
    const int icntEffect = pool.Allocate();
    OBJECT *o = &pool[icntEffect];
    RegisterEffectBirth(o->BirthTiming, o, icntEffect);
    o->EffectEmissionAge = 0.0;
    o->PresentationRandom = sessionKeeper_.RandomForConstruction().IsPresentation();
    o->Type = Type;
    o->MotionTrace.Reset();
    o->EffectMotionFrames = 0.f;
    o->AmbientNoiseFrames = 0.f;
    o->AmbientVerticalNoise = 0.f;
    o->EffectResting = false;
    o->ProjectileImpactApplied = false;
    o->SubType = SubType;
    o->AppearanceRandom = HasEffectFlicker(*o) ? static_cast<unsigned short>(WorldRandom()) : 0;
    o->LightEnable = true;
    o->HiddenMesh = -1;
    o->BlendMesh = -1;
    o->BlendMeshLight = 1.f;
    o->BlendMeshTexCoordU = 0.f;
    o->BlendMeshTexCoordV = 0.f;
    o->AnimationFrame = 0.f;
    o->AlphaEnable = false;
    o->Alpha = 1.f;
    o->PriorAnimationFrame = 0.f;

    if (Scale <= 0.0f)
        o->Scale = 0.9f;
    else
        o->Scale = Scale;

    if (Owner)
        o->Owner = Owner;
    else
        o->Owner = NULL;

    o->Velocity = 0.3f;
    o->PKKey = PKKey;
    o->Kind = Skill;
    o->Skill = SkillIndex;
    o->RenderType = 0;
    o->m_bRenderAfterCharacter = Type == MODEL_STORM3 || Type == MODEL_MAYAHANDSKILL;
    o->AttackPoint[0] = 0;
    o->CurrentAction = 0;
    o->m_bySkillSerialNum = (BYTE)SkillSerialNum;
    o->m_sTargetIndex = sTargetIndex;

    //Vector(1.f,1.f,1.f,o->Light);
    VectorCopy(Light, o->Light);
    VectorCopy(Angle, o->Angle);
    VectorCopy(Position, o->Position);
    o->Visible = sessionKeeper_.Renderer()->EffectVisible(*o);
    Vector(0.f, 0.f, 0.f, o->Direction);
    float Matrix[3][4];
    vec3_t p1, p2;

    // Data-driven effects: apply their parameter table row and any
    // one-shot creation hook, then we're done. Types whose creation is
    // not registry-driven fall through to the legacy switch below.
    if (const GameLogic::Effects::EffectDescriptor *desc = GameLogic::Effects::Lookup(Type);
        desc && (desc->create || desc->onCreate))
    {
        if (desc->create)
            GameLogic::Effects::ApplyCreateParams(o, *desc->create);
        if (desc->onCreate)
            (effectMoveBehavior_.*desc->onCreate)(o);
        return;
    }

    switch (Type)
    {
    case MODEL_DRAGON: {
        o->CollisionRange = true;
        o->Kind = 0;
        o->Timer = 0.0f;

        o->Velocity = 1.0f;
        o->Angle[1] = 0.0f;
        o->LifeTime = 1000;
        o->Gravity = 1.0f;
        o->Distance = 1.0f;
        o->Position[2] += 3400.f;
        Vector(0.f, -35.0f, 0.f, o->Direction);
        VectorCopy(o->Position, o->StartPosition);
        Vector(1.0f, 1.0f, 1.0f, o->Light);
    }
    break;
    case MODEL_ARROW_AUTOLOAD: {
        if (o->SubType == 0)
        {
            o->LifeTime = 40;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 40;
            o->Scale = 1.2f;
            Vector(1.0f, 0.8f, 0.2f, o->Light);
            VectorCopy(o->Light, o->Direction);
        }
    }
    break;
    case MODEL_INFINITY_ARROW: {
        if (o->SubType == 0)
        {
            o->LifeTime = 40;
            CreateEffect(MODEL_INFINITY_ARROW, Owner->Position, o->Angle, o->Light, 1, Owner);
            CreateEffect(MODEL_INFINITY_ARROW1, Owner->Position, o->Angle, o->Light, 0, Owner);
            CreateEffect(MODEL_INFINITY_ARROW2, Owner->Position, o->Angle, o->Light, 0, Owner);
            CreateEffect(MODEL_INFINITY_ARROW3, Owner->Position, o->Angle, o->Light, 0, Owner);
            CreateEffect(MODEL_INFINITY_ARROW4, Owner->Position, o->Angle, o->Light, 0, Owner);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 40;
            o->Scale = 1.0f;
            Vector(1.f, 1.f, 1.f, o->Light);
            VectorCopy(o->Light, o->Direction);
        }
    }
    break;
    case MODEL_INFINITY_ARROW1:
    case MODEL_INFINITY_ARROW2:
    case MODEL_INFINITY_ARROW3: {
        o->LifeTime = 40.f;
        o->Scale = 1.f;
        Vector(1.f, 1.f, 1.f, o->Light);
        VectorCopy(o->Light, o->Direction);

        if (o->SubType == 1 || o->SubType == 2 || o->SubType == 3)
        {
            o->LifeTime = 60.0f;
            o->Scale = 1.f;
        }
    }
    break;

    case MODEL_SHIELD_CRASH: {
        o->LifeTime = 24;
        o->Scale = 1.1f;
        o->Gravity = o->Velocity;
        Vector(0.5f, 0.5f, 1.f, o->Light);
        VectorCopy(o->Light, o->Direction);

        if (o->SubType == 0)
        {
            CreateEffect(MODEL_SHIELD_CRASH2, Owner->Position, Owner->Angle, o->Light, 0, o->Owner);
        }
        else
        {
            if (o->SubType == 2)
            {
                o->Scale = 0.7;
            }
            o->HiddenMesh = 0;
        }
    }
    break;

    case MODEL_SHIELD_CRASH2: {
        o->LifeTime = 24;
        o->Scale = 1.1f;
        o->Gravity = o->Velocity;
        Vector(0.5f, 0.5f, 1.f, o->Light);
        VectorCopy(o->Light, o->Direction);
    }
    break;

    case MODEL_IRON_RIDER_ARROW: {
        o->LifeTime = 12;
        o->Scale = 0.5f;
        o->Velocity = 70.f;
        Vector(0.8f, 1.0f, 0.8f, o->Light);

        vec3_t vDir;
        vec34_t vMat;
        Vector(0.f, -1.f, 0.f, vDir);
        AngleMatrix(o->Angle, vMat);
        VectorRotate(vDir, vMat, o->Direction);
        CreateJoint(BITMAP_JOINT_HEALING, o->Position, o->Position, o->Angle, 14, o, 30.f);
    }
    break;
    case MODEL_BLADE_SKILL: {
        o->LifeTime = 10;
        o->Scale = 1.5f;
        if (o->SubType == 1)
        {
            o->Scale = 1.f;
            o->LifeTime = 14;
        }
    }
    break;
    case MODEL_KENTAUROS_ARROW: {
        o->LifeTime = 34;
        o->Scale = 0.7f;
        o->Velocity = 70.f;
        o->Alpha = 0.f;
        Vector(1.0f, 1.0f, 1.0f, o->Light);

        //					CreateJoint ( BITMAP_JOINT_FORCE, o->Position, o->Position, o->Angle, 7, o, 150.f, 40 );
        //					CreateJoint ( BITMAP_FLARE+1, o->Position, o->Position, o->Angle, 14, o, 50.f, 40 );
        //					CreateJoint(BITMAP_JOINT_ENERGY,o->Position,o->Position,o->Angle,5,o,100.f);
        //					CreateJoint(BITMAP_JOINT_HEALING, o->Position, o->Position, o->Angle, 14, o, 30.f);
    }
    break;
    case MODEL_WARP3:
    case MODEL_WARP6:
        o->LifeTime = 0xffffff;
        o->BlendMesh = -2;
        o->Scale = 0.6f;
        break;
    case MODEL_WARP2:
    case MODEL_WARP:
    case MODEL_WARP5:
    case MODEL_WARP4: {
        Vector(0.0f, 0.0f, 0.0f, Light);
        o->BlendMesh = -2;
        o->LifeTime = 0xffffff;
        o->Scale = 1.3f + (float)(WorldRandom() % 50) / 100.f;
        o->Gravity = (float)(WorldRandom() % 80) / 10.f;
        o->Velocity = (float)(WorldRandom() % 100) / 1000.f + 0.01f;
    }
    break;
    case MODEL_GHOST: {
        o->Kind = 0;
        o->BlendMeshLight = 1.0f;
        o->Velocity = 1.0f;
        o->LifeTime = 1000;
        o->Gravity = 1.0f;
        o->Distance = 1.0f;
        o->Position[2] += ((float)(WorldRandom() % 200) - 100.f);
        o->Position[1] += ((float)(WorldRandom() % 200) - 100.f);
        o->Position[0] += ((float)(WorldRandom() % 200) - 100.f);
        o->Angle[2] += ((float)(WorldRandom() % 360));
        Vector(0.f, (float)(WorldRandom() % 8) * 0.1f - 5.0f, 0.f, o->Direction);
        o->Scale = o->Scale + (float)(WorldRandom() % 5) / 40.f;
        VectorCopy(o->Position, o->StartPosition);
        Vector(0.01f, 0.01f, 0.03f, o->Light);
    }
    break;
    case MODEL_TREE_ATTACK:
        o->Position[2] += (20.0f);
        o->LifeTime = 20;
        o->LightEnable = false;
        o->Scale = 0.05f;
        break;
    case MODEL_BUTTERFLY01:
        if (SceneFlag == CHARACTER_SCENE)
            o->HiddenMesh = -2;
        o->LifeTime = 250;
        o->Velocity = 0.3f;
        o->LightEnable = false;
        o->Kind = WorldRandom() % 2;

        Vector(0.f, -5.f * o->Owner->Scale, 0.f, o->Direction);

        if (o->SubType == 3)
            o->Scale = 0.9f;
        else
            o->Scale = 0.25f;
        break;
    case 9:
        o->LifeTime = 30;
        o->Scale = (WorldRandom() % 3) / 10.f + 0.6f;
        o->Velocity = 10.f;
        o->PKKey = 0;
        Vector(-0.1f, -0.1f, -0.5f, o->Direction);
        break;
    case BITMAP_SKULL:
        InitializeSkullEffect(o);
        break;
    case MODEL__SPEAR:
        o->Angle[0] = o->Angle[1] = o->Angle[2] = 0.0f;
        o->LifeTime = 5;
        break;

    case MODEL_SPEARSKILL:
        o->LifeTime = 20;
        o->Scale = 1.5f;
        o->Direction[0] = 5.0f * sinf(o->Angle[2] * Q_PI / 180.0f);
        o->Direction[1] = -5.0f * cosf(o->Angle[2] * Q_PI / 180.0f);
        break;
    case BITMAP_FIRE_CURSEDLICH:
        if (o->SubType == 0)
        {
            o->BlendMesh = -2;
            o->LifeTime = 10;
            o->Scale = 0.7f;
            VectorCopy(Position, o->Position);
            Vector(0.f, 0.f, 0.f, o->Direction);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 50;
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 20;
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = 10;
        }
        else if (o->SubType == 12)
        {
            o->LifeTime = 20;
        }
        break;
    case MODEL_SWELL_OF_MAGICPOWER: {
        if (o->SubType == 0)
        {
            o->LifeTime = 45;
        }
    }
    break;
    case MODEL_ARROWSRE06: {
        if (o->SubType == 0)
        {
            o->LifeTime = 30;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 40;
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 30;
            VectorCopy(Position, o->Position);
        }
    }
    break;
    case MODEL_SWELL_OF_MAGICPOWER_BUFF_EFF: {
        if (o->SubType == 0)
        {
            o->LifeTime = 999;
            o->Timer = WorldTime;
        }
    }
    break;
    case MODEL_SUMMONER_WRISTRING_EFFECT:
        o->BlendMesh = -2;
        o->LifeTime = 100;
        o->Scale = 0.7f;
        VectorCopy(Position, o->Position);
        Vector(0.f, 0.f, 0.f, o->Direction);
        break;
    case MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT:
        if (o->SubType == 0)
        {
            o->LifeTime = 100;
            o->Scale = 0.8f;
            Vector(0.f, 0.f, 0.f, o->Direction);
            o->Alpha = 0.0f;
            OBJECT *pObject = o->Owner;
            o->Position[0] = pObject->Position[0] + cosf(WorldTime * 0.003f) * 40.0f;
            o->Position[1] = pObject->Position[1] + sinf(WorldTime * 0.003f) * 40.0f;
            o->Position[2] =
                pObject->Position[2] + (sinf(WorldTime * 0.0010f) + 2.0f) * 80.0f - 60.0f;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 100000;
        }
        break;
    case MODEL_SUMMONER_EQUIP_HEAD_NEIL:
        if (o->SubType == 0)
        {
            o->LifeTime = 100;
            o->Scale = 0.8f;
            o->Alpha = 0.0f;
            Vector(0.f, 0.f, 0.f, o->Direction);
            OBJECT *pObject = o->Owner;
            o->Position[0] = pObject->Position[0] + cosf(WorldTime * 0.003f) * 40.0f;
            o->Position[1] = pObject->Position[1] + sinf(WorldTime * 0.003f) * 40.0f;
            o->Position[2] =
                pObject->Position[2] + (sinf(WorldTime * 0.0010f) + 2.0f) * 80.0f - 60.0f;
            CreateJoint(MODEL_SPEARSKILL, o->Position, o->Position, o->Angle, 15, o, 40.0f, -1, 0,
                        0);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 100000;
        }
        break;
    case MODEL_SUMMONER_EQUIP_HEAD_LAGUL: {
        o->LifeTime = 100;
        o->Scale = 0.8f;
        Vector(0.f, 0.f, 0.f, o->Direction);
        o->Alpha = 0.0f;
        OBJECT *pObject = o->Owner;
        o->Position[0] = pObject->Position[0] + cosf(WorldTime * 0.003f) * 40.0f;
        o->Position[1] = pObject->Position[1] + sinf(WorldTime * 0.003f) * 40.0f;
        o->Position[2] = pObject->Position[2] + (sinf(WorldTime * 0.0010f) + 2.0f) * 80.0f - 60.0f;
        CreateJoint(MODEL_SPEARSKILL, o->Position, o->Position, o->Angle, 17, o, 40.0f, -1, 0, 0);
    }
    break;
    case MODEL_SUMMONER_CASTING_EFFECT1:
    case MODEL_SUMMONER_CASTING_EFFECT11:
    case MODEL_SUMMONER_CASTING_EFFECT111:
    case MODEL_SUMMONER_CASTING_EFFECT2:
    case MODEL_SUMMONER_CASTING_EFFECT22:
    case MODEL_SUMMONER_CASTING_EFFECT222: {
        o->LifeTime = 40;
        if (o->SubType = 0)
            o->Scale = 1.0f;
        o->Alpha = 1.0f;
        Vector(0.f, 0.f, 0.f, o->Direction);
        o->BlendMesh = 0;
        o->BlendMeshLight = 0.0f;
    }
    break;
    case MODEL_SUMMONER_CASTING_EFFECT4:
        o->LifeTime = 25;
        o->Scale = 1.0f;
        o->Alpha = 1.0f;
        Vector(0.f, 0.f, 0.f, o->Direction);
        o->BlendMesh = 0;
        o->BlendMeshLight = 0.0f;
        break;
    case MODEL_SUMMONER_SUMMON_SAHAMUTT:
        o->LifeTime = 80;
        if (o->SubType == 2)
            o->Scale = 0.7f;
        else if (o->SubType == 1)
            o->Scale = 0.5f;
        else if (o->SubType == 0)
            o->Scale = 0.35f;
        o->Alpha = 0.0f;
        o->Velocity = 0.5f;

        VectorCopy(Light, o->HeadTargetAngle);
        Vector(1.0f, 1.0f, 1.0f, o->Light);
        break;
    case MODEL_SUMMONER_SUMMON_NEIL:
        o->LifeTime = 80;
        o->Scale = 1.0f;
        o->Alpha = 0.0f;
        o->Velocity = 0.35f;
        o->Skill = 0;
        o->Position[2] += (10.0f);

        VectorCopy(Light, o->HeadTargetAngle);
        Vector(1.0f, 1.0f, 1.0f, o->Light);
        break;
    case MODEL_SUMMONER_SUMMON_LAGUL:
        if (o->SubType == 0)
        {
            o->LifeTime = 160;
            o->Scale = 1.0f;
            o->Velocity = 0.5f;
            VectorCopy(Position, o->HeadTargetAngle);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 160;
            o->Alpha = 0.0f;
            ((JOINT *)o->Owner)->Target = o;
            o->Owner = NULL;

            for (int i = 48; i <= 53; ++i)
            {
                CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, i, o, 10.f);
            }
        }
        break;
    case BITMAP_ENERGY:
        if (o->SubType == 0)
        {
            o->LifeTime = 20;
            Vector(0.f, -60.f, 0.f, o->Direction);
            o->Position[2] += (100.f);
        }
        break;
    case MODEL_LIGHTNING_ORB: {
        if (o->SubType == 0)
        {
            o->LifeTime = 20;
            Vector(0.f, -60.f, 0.f, o->Direction);
            o->Position[2] += (100.f);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 18;
        }
    }
    break;
    // ChainLighting
    case MODEL_CHAIN_LIGHTNING: {
        switch (o->SubType)
        {
        case 0:
        case 1:
        case 2: {
            o->LifeTime = 20;
        }
        break;
        }
    }
    break;
    // Drain Life
    case MODEL_ALICE_DRAIN_LIFE: {
        if (o->SubType == 0)
        {
            o->LifeTime = 70;
        }
    }
    break;
    case MODEL_ALICE_BUFFSKILL_EFFECT: {
        if (o->SubType == 0 || o->SubType == 1 || o->SubType == 2)
        {
            o->LifeTime = 34;
            o->Position[2] += (100);
            VectorCopy(Light, o->Light);
            o->AlphaEnable = true;
            o->Alpha = 0.f;
            o->BlendMeshLight = 0.f;
            o->Angle[2] = 0.f;
            o->Scale = 0.1f;
        }
        else if (o->SubType == 3 || o->SubType == 4)
        {
            o->LifeTime = 100;

            if (o->SubType == 3)
            {
                o->Scale = 1.5f;
            }
            else if (o->SubType == 4)
            {
                o->Scale = 1.f;
            }
        }
    }
    break;

    case MODEL_ALICE_BUFFSKILL_EFFECT2: {
        o->LifeTime = 35;
        o->Position[2] += (100);
        VectorCopy(Light, o->Light);
        o->AlphaEnable = true;
        o->Alpha = 0.f;
        o->BlendMeshLight = 0.f;
        o->Angle[2] = 0.f;
        o->Scale = 0.15f;
    }
    break;
    case MODEL_LIGHTNING_SHOCK: {
        if (o->SubType == 0)
        {
            o->LifeTime = 20;
            o->Position[2] += (280.f);
            o->Velocity = 0.0f;
            o->Gravity = 1.0f;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 12;
            vec3_t vLight;
            Vector(1.0f, 0.8f, 0.5f, vLight);
            CreateEffect(BITMAP_DAMAGE_01_MONO, o->Position, o->Angle, vLight, 1);
            Vector(1.0f, 0.0f, 0.0f, vLight);
            CreateEffect(BITMAP_DAMAGE_01_MONO, o->Position, o->Angle, vLight, 1);

            // magic_ground
            vec34_t Matrix;
            vec3_t vAngle, vDirection, vPosition;
            float fAngle;
            Vector(1.0f, 0.2f, 0.05f, vLight);
            for (int i = 0; i < 5; ++i)
            {
                Vector(0.f, 150.f, 0.f, vDirection);
                fAngle = o->Angle[2] + i * 72.f;
                Vector(0.f, 0.f, fAngle, vAngle);
                AngleMatrix(vAngle, Matrix);
                VectorRotate(vDirection, Matrix, vPosition);
                VectorAdd(vPosition, o->Position, vPosition);

                CreateEffect(BITMAP_MAGIC, vPosition, o->Angle, vLight, 12);
            }

            Vector(1.0f, 0.4f, 0.2f, vLight);
            VectorCopy(o->Position, vPosition);
            vPosition[2] = RequestTerrainHeight(vPosition[0], vPosition[1]) + 10;

            for (int i = 0; i < 3; ++i)
                CreateEffect(MODEL_KNIGHT_PLANCRACK_A, vPosition, o->Angle, vLight, 1, o, 0, 0, 0,
                             0, WorldRandom() % 4 * 0.1f + 1.0f);
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 15;
            vec3_t vLight;
            vec3_t vPosition;
            Vector(1.0f, 0.4f, 0.2f, vLight);
            VectorCopy(o->Position, vPosition);
            vPosition[2] = RequestTerrainHeight(vPosition[0], vPosition[1]) + 10;

            for (int i = 0; i < 3; ++i)
                CreateEffect(MODEL_KNIGHT_PLANCRACK_A, o->Position, o->Angle, vLight, 1, o, 0, 0, 0,
                             0, WorldRandom() % 4 * 0.1f + 0.5f);
        }
    }
    break;

    case BITMAP_SPARK + 1:
        o->LifeTime = 10;
        //Vector(0.f,0.f,60.f,o->Direction);
        //o->Position[2] += 100.f;
        break;
    case BITMAP_BOSS_LASER:
    case BITMAP_BOSS_LASER + 1:
    case BITMAP_BOSS_LASER + 2:
        // Tick-start rotation and per-segment step for the prepared laser pattern.
        o->HeadAngle[0] = o->Angle[2];
        o->HeadAngle[1] = 0.f;
        o->LifeTime = 20;
        switch (Type)
        {
        case BITMAP_BOSS_LASER:
            Vector(0.5f, 0.7f, 1.f, o->Light);
            Vector(0.f, -50.f, 0.f, p1);
            o->Scale = 16.f;

            if (SubType == 1)
            {
                Vector(0.f, -50.f, 0.f, p1);
                o->Scale = 3.0f;
                Vector(1.0f, 1.0f, 1.0f, o->Light);
            }
            else if (SubType == 2)
            {
                o->LifeTime = 35;
                Vector(0.f, -50.f, 0.f, p1);
                o->Scale = 2.5f;
                Vector(1.0f, 1.0f, 1.0f, o->Light);
            }
            break;
        case BITMAP_BOSS_LASER + 1:
            Vector(1.f, 0.4f, 0.2f, o->Light);
            Vector(0.f, -50.f, 0.f, p1);
            o->Scale = 16.f;
            break;
        case BITMAP_BOSS_LASER + 2:
            Vector(1.f, 0.4f, 0.2f, o->Light);
            Vector(0.f, -15.f, 0.f, p1);
            o->Scale = 5.f;
            break;
        }
        //o->Scale = 10.f;
        //Vector(0.f,-30.f,0.f,p1);
        AngleMatrix(o->Angle, Matrix);
        VectorRotate(p1, Matrix, o->Direction);
        if (o->Owner == &Hero->Object)
        {
            VectorCopy(o->Position, Position);
            Vector(0.f, -150.f, 0.f, p1);
            AngleMatrix(o->Angle, Matrix);
            vec3_t Direction;
            VectorRotate(p1, Matrix, Direction);
            int Number = 4;
            for (int j = 0; j < Number; j++)
            {
                VectorAdd(Position, Direction, Position);
                AttackCharacterRange(o->Skill, Position, 150.f, o->Weapon, o->PKKey);
            }
        }
        break;
    case BITMAP_LIGHTNING + 1:
        if (o->SubType == 0)
        {
            o->LifeTime = 10;
            o->Scale = 1.5f;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 50;
            o->Alpha = 0.01f;
        }
        break;
    case BITMAP_LIGHT:
        if (o->SubType == 0)
        {
            //Vector( 0.5f, 0.5f, 1.0f, o->Light);
            Vector(0.3f, 0.3f, 0.3f, o->Light);
            float fAngle = 70.0f * Q_PI / 180.0f;
            float fAngle2 = 30.0f; //( float)( WorldRandom() % 360);
            //float fSpeed = ( float)( 18 + WorldRandom() % 10);
            float fSpeed = (float)(9 + WorldRandom() % 5) * 0.5f;
            o->Direction[0] = fSpeed * cosf(fAngle) * sinf(fAngle2 * Q_PI / 180.0f);
            o->Direction[1] = fSpeed * cosf(fAngle) * cosf(fAngle2 * Q_PI / 180.0f);
            o->Direction[2] = fSpeed * sinf(fAngle);
            o->Scale = 3.f;
            //o->LifeTime = 100;
            o->LifeTime = 400;
        }
        else if (o->SubType == 1 || o->SubType == 2)
        {
            o->LifeTime = 1000;
            o->Velocity = 0.f;
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = 30;
            o->Velocity = 0.f;
        }
        break;
    case BITMAP_FIRE + 1:
        o->LifeTime = 10;
        AngleMatrix(o->Angle, Matrix);
        Vector(0.f, -60.f, 0.f, p1);
        VectorRotate(p1, Matrix, p2);
        VectorAdd(o->Position, p2, o->Position) o->Position[2] += (130.f);
        break;
    case BITMAP_FLAME:
        if (o->SubType == 0)
        {
            o->LifeTime = 40;
            o->Weapon = CharacterMachine->PacketSerial;
        }
        else if (o->SubType == 1 || o->SubType == 2)
        {
            o->LifeTime = 10;
            Vector(0.f, 0.f, 0.f, o->Angle);
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = 15;
        }
        else if (o->SubType == 4)
        {
            o->Scale = 0.01f;
            o->LifeTime = 10;
            Vector(0.f, 0.f, 0.f, o->Angle);
        }
        else if (o->SubType == 5)
        {
            o->LifeTime = 20;
        }
        else if (o->SubType == 6)
        {
            o->LifeTime = 40;
        }
        break;
    case MODEL_RAKLION_BOSS_CRACKEFFECT:
        if (o->SubType == 0)
        {
            o->LifeTime = 40;
            o->Scale = Scale + 1.f;
            o->Position[2] += (30.f);
            o->Angle[2] = WorldRandom() % 360;
        }
        break;
    case MODEL_RAKLION_BOSS_MAGIC:
        if (o->SubType == 0)
        {
            o->LifeTime = 35;
            o->Scale = Scale;
        }
        break;
    case BITMAP_FIRE_HIK2_MONO:
        if (o->SubType == 0)
        {
            o->LifeTime = 60;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 20;
            Vector(0.f, 0.f, 0.f, o->Angle);
        }
        break;
    case BITMAP_CLOUD:
        if (o->SubType == 0)
        {
            o->LifeTime = 60;
            o->Scale = Scale;
            o->Angle[2] = (float)(WorldRandom() % 360);
        }
        break;
    case BITMAP_MAGIC:
        o->LifeTime = 20;
        o->Scale = 0.5f;
        if (o->SubType == 0)
        {
            o->LifeTime = 15;
        }
        else if (o->SubType == 8)
        {
            o->LifeTime = 30;
            o->Scale = 1.f;
        }
        else if (o->SubType == 9)
        {
            o->LifeTime = 40;
            o->Scale = 2.4f;
            Vector(0, 0, 0, o->HeadAngle);
        }
        else if (o->SubType == 10)
        {
            o->LifeTime = 44;
            o->Scale = 12.f;
            o->Alpha = 0.0f;
        }
        else if (o->SubType == 11)
        {
            o->LifeTime = 24;
            o->Scale = 0.8f;
            Vector(0, 0, 0, o->HeadAngle);
        }
        else if (o->SubType == 12)
        {
            o->LifeTime = 20;
            o->Scale = Scale * 0.1f;
        }
        else if (o->SubType == 13 || o->SubType == 14)
        {
            o->LifeTime = 30;
            o->Scale = 1.0f;
            VectorCopy(Light, o->Light);
        }
        break;

    case BITMAP_MAGIC + 1:
    case BITMAP_MAGIC + 2:
        o->LifeTime = 20;
        if (o->SubType == 4 || o->SubType == 10)
        {
            o->LifeTime = 40;
            o->Scale = ((WorldRandom() % 50) + 50) / 100.f * 4.f;
        }
        else if (o->SubType == 6)
        {
            o->LifeTime = 60;
            o->Scale = ((WorldRandom() % 50) + 50) / 100.f * 4.f;
            VectorCopy(Position, o->StartPosition);
        }
        else if (o->SubType == 7)
        {
            o->LifeTime = 40;
            o->Angle[2] = WorldRandom() % 360;
        }
        else if (o->SubType == 8)
        {
            o->LifeTime = 20;
        }
        else if (o->SubType == 9)
        {
            o->LifeTime = 10;
            o->Scale = 0.1f;
        }
        else if (o->SubType == 11)
        {
            o->LifeTime = 20;
        }
        else if (o->SubType == 12)
        {
            o->LifeTime = 20;
        }
        else if (o->SubType == 13)
        {
            o->LifeTime = 40;
            o->Scale = Scale;
        }
        break;
    case BITMAP_OUR_INFLUENCE_GROUND:
    case BITMAP_ENEMY_INFLUENCE_GROUND:
        if (o->SubType == 0)
        {
            o->LifeTime = 50;
            o->Scale = 0.6f;
            o->Alpha = 1.0f;
            o->AlphaTarget = 0.75f;
        }
        break;
    case BITMAP_MAGIC_ZIN:
        switch (o->SubType)
        {
        case 0:
            o->LifeTime = 50;
            break;
        case 1:
            o->LifeTime = 40;
            o->Alpha = 0.f;
            break;
        case 2:
            o->LifeTime = 30;
            break;
        }
        break;
    case BITMAP_SHINY + 6:
        switch (o->SubType)
        {
        case 0:
            o->LifeTime = 24;
            break;
        case 1:
        case 2:
            o->LifeTime = 100;
            break;
        case 3:
            o->LifeTime = 24;
            o->RenderType = RENDER_TYPE_ALPHA_BLEND_MINUS;
            break;
        }
        break;

    case BITMAP_PIN_LIGHT:
        switch (o->SubType)
        {
        case 3:
        case 0:
            o->LifeTime = 40;
            break;
        case 1:
        case 2:
            o->LifeTime = 100;
            break;
        case 4: {
            o->Alpha = 1.0f;
            o->LifeTime = 30;
            o->Scale += ((float)(WorldRandom() % 5) / 10.0f);
            o->Angle[1] = WorldRandom() % 360;
        }
        break;
        }
        break;
    case BITMAP_ORORA:
        switch (o->SubType)
        {
        case 0:
        case 1:
            o->LifeTime = 100;
            break;
        case 2:
        case 3:
            o->LifeTime = 25;
            break;
        }
        CreateParticle(o->Type, o->Position, o->Angle, o->Light, o->SubType, 1.0f, o->Owner);
        break;

    case BITMAP_SPARK + 2:
        o->LifeTime = 100;
        break;

    case BITMAP_GATHERING:
        o->LifeTime = 10;

        switch (o->SubType)
        {
        case 0:
            Vector(0.f, -100.f, 0.f, p1);
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(p1, Matrix, p2);
            VectorAdd(o->Position, p2, o->Position);

            o->Position[2] += (150.f);
            break;
        case 1:
        case 2:
            VectorCopy(Position, o->StartPosition);
            o->LifeTime = 20;
            break;
        case 3:
            o->LifeTime = 10;
            Vector(-10.f, 10.f, 0.f, p1);
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(p1, Matrix, p2);
            VectorAdd(o->Position, p2, o->Position);
            break;
        }
        break;
    case BITMAP_JOINT_THUNDER: {
        o->LifeTime = 20;

        VectorCopy(o->Position, o->StartPosition);
        o->StartPosition[2] += (800.0f);
    }
    break;
    case MODEL_STAFF_OF_DESTRUCTION:
        o->LifeTime = 30;
        o->BlendMesh = -2;
        o->Scale = 1.f;
        o->Position[2] += (280.f);
        o->Angle[0] += (20.f);
        Vector(0.f, -80.f, -10.f, o->Direction);
        break;
    case MODEL_WAVE:
        o->BlendMesh = 0;
        o->BlendMeshLight = 1.5f;
        o->Scale = 0.5f;
        o->LifeTime = 15;
        o->Position[2] -= (15.f);

        Vector(1.f, 1.f, 1.f, o->Light);
        break;
    case MODEL_TAIL:
        o->BlendMesh = -2;
        o->LifeTime = 6;
        o->Scale = 1.f;
        o->Gravity = 80.f;
        Vector(0.5f, 0.5f, 0.5f, o->Light);
        Vector(0.f, 0.f, 45.f, o->Angle);
        break;
    case MODEL_SKILL_BLAST:
        o->LifeTime = 30;
        o->BlendMesh = 0;
        o->Scale = (float)(WorldRandom() % 8 + 10) * 0.1f;
        o->Position[0] += (float)(WorldRandom() % 100 + 200);
        o->Position[1] += (float)(WorldRandom() % 100 - 50);
        o->Position[2] += (float)(WorldRandom() % 500 + 300);
        Vector(0.f, 0.f, -50.f - WorldRandom() % 50, o->Direction);
        Vector(0.f, 20.f, 0.f, o->Angle);
        VectorCopy(o->Position, o->EyeLeft);
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 5, o, 100.f);
        o->Weapon = CharacterMachine->PacketSerial;
        break;
    case MODEL_WAVE_FORCE:
        o->BlendMesh = -2;
        o->Scale = 0.9f;
        o->Velocity = 0.5f;
        o->LifeTime = 12;
        o->Skill = 0;
        o->PKKey = -1;
        o->Scale = PKKey / 100.f;
        o->BlendMeshLight = 0.1f;
        break;
    case MODEL_SKILL_INFERNO:
        o->BlendMesh = -2;
        o->Scale = 0.9f;
        o->Velocity = 0.5f;
        switch (o->SubType)
        {
        case 0:
            Vector(0.8f, 0.8f, 0.8f, o->Light);
            o->LifeTime = 15;
            break;
        case 1:
            Vector(1.0f, .5f, .2f, o->Light);
            o->LifeTime = 35;
            break;
        case 2:
            o->LifeTime = 12;
            o->HiddenMesh = SkillIndex;
            o->Skill = 0;
            o->PKKey = -1;
            o->Distance = PKKey;
            o->Scale = PKKey / 100.f;
            o->BlendMeshLight = 0.1f;
            break;
        case 8:
            o->LifeTime = 12;
            o->HiddenMesh = SkillIndex;
            o->Skill = 0;
            o->PKKey = -1;
            o->Distance = PKKey;
            o->Scale = PKKey / 100.f;
            o->BlendMeshLight = 0.1f;
            o->Velocity *= 4;
            break;
        case 3:
            Vector(0.8f, 0.8f, 0.8f, o->Light);
            o->LifeTime = 4;
            o->HiddenMesh = 1;
            o->Scale = 0.02f;
            o->Velocity = 1.f;
            Vector(90.f, 0.f, 0.f, o->Angle);
            break;
        case 4:
            Vector(0.8f, 0.8f, 0.8f, o->Light);
            o->LifeTime = 35;
            o->HiddenMesh = 1;
            o->Scale = 0.1f;
            o->Gravity = 0.f;
            break;
        case 5:
            Vector(0.8f, 0.8f, 0.8f, o->Light);
            o->LifeTime = 15;
            break;
        case 6:
            o->LifeTime = 5;
            o->HiddenMesh = SkillIndex;
            o->Skill = 0;
            o->PKKey = -1;
            o->Distance = PKKey;
            o->Scale = PKKey / 100.f;
            o->Velocity = 0.1f;
            o->BlendMeshLight = 0.1f;
            break;
        case 9: {
            o->LifeTime = 13;
            Vector(0.1f, 1.0f, 0.2f, o->Light);
            o->Scale = 0.45f;
            o->BlendMeshLight = 0.1f;
            o->Gravity = 2.0f;
        }
        break;
        case 10: {
            o->LifeTime = 12;
            o->HiddenMesh = SkillIndex;
            o->Skill = 0;
            o->PKKey = -1;
            o->Distance = PKKey;
            o->Scale = PKKey / 100.f;
            o->BlendMeshLight = 0.1f;
        }
        break;
        }
        if (o->Owner == &Hero->Object && o->SubType < 2)
        {
            o->Weapon = CharacterMachine->PacketSerial;

            AttackCharacterRange(o->Skill, o->Position, 400.f, o->Weapon, o->PKKey);
        }
        break;
    case MODEL_BOSS_ATTACK:
        o->LifeTime = 50;
        o->Scale = 0.8f;
        o->Velocity = 1.f;
        o->Angle[0] = 0.f;
        o->BlendMesh = 0;
        o->Direction[1] = -50.f;
        o->Gravity = -10.f;
        break;
    case MODEL_MAGIC_CIRCLE1:
        o->LifeTime = 30;
        o->Scale = 0.7f;
        o->BlendMesh = -2;
        if (o->SubType == 2)
        {
            o->LifeTime = 15;
            o->Velocity = 0.3f;
            o->Scale = 0.7f;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 20;
            o->Velocity = 0.1f;
            o->HiddenMesh = 0;
        }
        else
        {
            o->Velocity = 0.1f;
        }
        break;
    case MODEL_BIG_METEO1:
    case MODEL_BIG_METEO2:
    case MODEL_BIG_METEO3:
        o->LifeTime = 100;
        o->Scale = (float)(WorldRandom() % 10 + 4) * 0.1f;
        Vector(0.f, -15.f / o->Scale, -30.f / o->Scale, o->Direction);
        o->SubType = 1;
        break;
    case MODEL_PIERCING:
        o->LifeTime = 100;
        o->BlendMesh = 0;
        o->BlendMeshLight = 1.f;
        o->Gravity = 0;

        switch (SubType)
        {
        case 0: {
            o->Scale = 12.f;
            VectorCopy(o->Owner->Position, o->StartPosition);
        }
        break;
        case 1:
            o->HiddenMesh = 0;
            o->Scale = 24.f;
            break;
        case 2:
            o->HiddenMesh = 0;
            o->Scale = 12.f;
            o->LifeTime = 10;
            break;
        case 3: {
            o->Scale = 10.f;
            Vector(0.9f, 0.4f, 0.6f, o->Light);
            VectorCopy(o->Owner->Position, o->StartPosition);
        }
        break;
        }

        CreateJoint(BITMAP_FLARE + 1, o->Position, o->Position, o->Angle, 0, o, o->Scale, 30,
                    SubType);
        CreateJoint(BITMAP_FLARE + 1, o->Position, o->Position, o->Angle, 1, o, o->Scale, 30,
                    SubType);
        CreateJoint(BITMAP_FLARE + 1, o->Position, o->Position, o->Angle, 2, o, o->Scale, 30,
                    SubType);
        CreateJoint(BITMAP_FLARE + 1, o->Position, o->Position, o->Angle, 3, o, o->Scale, 30,
                    SubType);
        PlayBuffer(SOUND_FLASH);
        o->Scale = 1.f;
        break;

    case MODEL_PIERCING + 1:
        o->Velocity = 1.f;
        o->LifeTime = 30;
        AngleMatrix(o->Angle, Matrix);
        Vector(-10.f, -60.f, 135.f, p1);
        VectorRotate(p1, Matrix, p2);
        VectorAdd(o->Position, p2, o->Position);

        o->Scale = 0.8f;
        o->Direction[1] = -70.f;

        CreateEffect(MODEL_PIERCING, o->Position, o->Angle, o->Light, SubType, o);
        break;

    case MODEL_ARROW_BEST_CROSSBOW:
        o->LifeTime = 30;
        o->BlendMesh = -2;
        o->Scale = 1.f;
        o->Position[2] += (130.f);
        Vector(0.f, -70.f, 0.f, o->Direction);
        VectorCopy(o->Position, o->EyeLeft);
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 5, o, 100.f);

        if (o->SubType != 0)
        {
            CreateEffect(MODEL_PIERCING, o->Position, o->Angle, o->Light, 0, o);
            o->AttackPoint[0] = 0;
            o->Kind = 1;
        }

        o->Weapon = CharacterMachine->PacketSerial;
        break;

    case MODEL_ARROW_DOUBLE:
        o->LifeTime = 30;
        o->BlendMesh = -2;
        o->Scale = 1.f;
        o->Position[2] += (130.f);
        Vector(0.f, -70.f, 0.f, o->Direction);
        VectorCopy(o->Position, o->EyeLeft);
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 5, o, 100.f);

        if (o->SubType != 0)
        {
            CreateEffect(MODEL_PIERCING, o->Position, o->Angle, o->Light, 0, o);
            o->AttackPoint[0] = 0;
            o->Kind = 1;
        }

        o->Weapon = CharacterMachine->PacketSerial;
        break;

    case MODEL_ARROW_HOLY:
        o->LifeTime = 30;
        o->BlendMesh = -2;
        o->Scale = 1.f;
        o->Position[2] += (130.f);

        Vector(0.f, -60.f, 0.f, o->Direction);
        AngleMatrix(o->Angle, Matrix);
        Vector(-10.f, -100.f, 15.f, p1);
        VectorRotate(p1, Matrix, p2);
        VectorCopy(o->Position, o->StartPosition);
        VectorAdd(o->StartPosition, p2, o->StartPosition);

        Vector(0.f, 0.f, o->Angle[2], Angle);

        if (o->SubType == 1)
        {
            for (int i = 0; i < 3; ++i)
            {
                if (i == 1)
                    CreateJoint(BITMAP_FLARE, o->StartPosition, o->StartPosition, Angle, 25, o,
                                50.f, -1, 1);
                else
                    CreateJoint(BITMAP_FLARE, o->StartPosition, o->StartPosition, Angle, 25, o,
                                50.f);
            }
        }
        else
        {
            for (int i = 0; i < 4; ++i)
            {
                if (i == 1)
                    CreateJoint(BITMAP_FLARE, o->StartPosition, o->StartPosition, Angle, 11, o,
                                50.f, -1, 1);
                else
                    CreateJoint(BITMAP_FLARE, o->StartPosition, o->StartPosition, Angle, 11, o,
                                50.f);
            }
        }

        o->Weapon = CharacterMachine->PacketSerial;
        break;
    case MODEL_MULTI_SHOT1:
    case MODEL_MULTI_SHOT2:
    case MODEL_MULTI_SHOT3:
    case MODEL_ARROW:
    case MODEL_ARROW_STEEL:
    case MODEL_ARROW_THUNDER:
    case MODEL_ARROW_LASER:
    case MODEL_ARROW_V:
    case MODEL_ARROW_SAW:
    case MODEL_ARROW_NATURE:
    case MODEL_ARROW_WING:
    case MODEL_LACEARROW:
    case MODEL_DARK_SCREAM:
    case MODEL_DARK_SCREAM_FIRE:
    case MODEL_ARROW_SPARK:
    case MODEL_ARROW_RING:
    case MODEL_ARROW_TANKER:
    case MODEL_ARROW_DARKSTINGER:
    case MODEL_ARROW_GAMBLE:
        if (Type == MODEL_ARROW)
            o->BlendMesh = 1;
        else if (Type == MODEL_ARROW_NATURE)
            o->BlendMesh = -2;
        else if (Type == MODEL_ARROW_THUNDER || Type == MODEL_ARROW_LASER ||
                 Type == MODEL_ARROW_V || Type == MODEL_ARROW_WING)
            o->BlendMesh = 0;

        o->Velocity = 1.f;
        o->LifeTime = 30;
        AngleMatrix(o->Angle, Matrix);
        Vector(-10.f, -60.f, 135.f, p1);
        VectorRotate(p1, Matrix, p2);
        VectorAdd(o->Position, p2, o->Position);
        if (Type == MODEL_ARROW_WING)
        {
            o->Scale = 1.8f;
            o->Direction[1] = -50.f;
            o->Gravity = -10.f;
        }
        else if (Type == MODEL_ARROW_TANKER)
        {
            o->Angle[0] -= (18.0f);
            o->Scale = 1.0f;
            o->Direction[1] = -42.0f;
            o->Direction[2] = 4.0f;
        }
        else if (Type == MODEL_MULTI_SHOT1)
        {
            o->LifeTime = 16;
            o->BlendMesh = -2;
            o->Scale = 0.f;
        }
        else if (Type == MODEL_MULTI_SHOT2)
        {
            o->LifeTime = 13;
            o->BlendMesh = -2;
            o->Scale = 0.f;
        }
        else if (Type == MODEL_MULTI_SHOT3)
        {
            o->LifeTime = 12;
            o->BlendMesh = -2;
            o->Scale = 0.f;
        }
        else if (Type == MODEL_DARK_SCREAM || Type == MODEL_DARK_SCREAM_FIRE)
        {
            o->LifeTime = 19;
            o->Scale = 2.3f;
            o->Direction[1] = -35.f;
            //					o->Direction[1] = -5.f;
            if (Type == MODEL_DARK_SCREAM)
            {
                o->Scale = 0.9f;
                o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 3.f;
                vec3_t Light;
                Vector(0.0f, 1.0f, 0.0f, Light);
                if (SubType == 0)
                {
                    CreateJoint(BITMAP_JOINT_FORCE, o->Position, o->Position, o->Angle, 7, o, 150.f,
                                40);
                }
                else if (SubType == 1)
                {
                    CreateJoint(BITMAP_JOINT_FORCE, o->Position, o->Position, o->Angle, 20, o,
                                400.f, 40);
                }
                o->Position[2] += (20.f);
                vec3_t ap, P, dp;
                VectorCopy(o->Position, ap);
                Vector(0.f, -20.f, 0.f, P);
                AngleMatrix(o->Angle, o->Matrix);
                VectorRotate(P, o->Matrix, dp);
                VectorAdd(dp, o->Position, o->Position);
                CreateParticle(BITMAP_BLUE_BLUR, o->Position, o->Angle, Light, 1, 1.f);
            }
        }
        else if (Type == MODEL_ARROW_SPARK)
        {
            o->Scale = 1.0f;
            o->Direction[1] = -70.f;
            CreateJoint(BITMAP_FLARE + 1, o->Position, o->Position, o->Angle, 14, o, 50.f, 40);
        }
        else if (Type == MODEL_ARROW_DARKSTINGER)
        {
            o->Scale = 0.8f;
            o->Direction[1] = -70.f;
            o->LifeTime = 30;
        }
        else if (Type == MODEL_ARROW_GAMBLE)
        {
            o->Scale = 0.8f;
            o->Direction[1] = -70.f;
            o->LifeTime = 30;
        }
        else
        {
            o->Scale = 0.8f;
            o->Direction[1] = -70.f;
        }

        if (o->SubType == 2)
        {
            CreateEffect(MODEL_PIERCING, o->Position, o->Angle, o->Light, 0, o);
            o->AttackPoint[0] = 0;
            o->Kind = 1;
        }
        //. Create Effect
        if (Type == MODEL_ARROW_NATURE && o->SubType == 1)
        { //. 녹색 띠 생성
            CreateJoint(BITMAP_FLARE + 1, o->Position, o->Position, o->Angle, 13, o, 20.f, 40);
            //					CreateJoint ( BITMAP_FLARE+1, o->Position, o->Position, o->Angle, 6, o, 20.f, 40 );
            //					CheckTargetRange(o);
        }

        if (Type == MODEL_ARROW && (o->SubType == 3 || o->SubType == 4))
        {
            o->LifeTime = 40;
            o->Scale = 1.5f;
            o->Gravity = (WorldRandom() % 100 + 50) / 15.f;
            VectorCopy(Hero->Object.Position, o->StartPosition);
            if (o->SubType == 3)
            {
                o->Direction[1] = -(WorldRandom() % 30 + 50.f);
                o->Angle[0] = -(WorldRandom() % 20 + 45.f);
            }
            else
            {
                o->Direction[1] = -(WorldRandom() % 15 + 55.f);
                o->Angle[0] = -10.f;
            }
        }
        else if (Type == MODEL_LACEARROW)
        {
            o->BlendMesh = 1;
            o->Angle[1] = (float)(WorldRandom() % 360);
            CreateJoint(BITMAP_FLARE + 1, o->Position, o->Position, o->Angle, 12, o, 40.f, 80);
        }
        else if (Type == MODEL_ARROW_RING)
        {
            o->BlendMesh = 0;
            o->Scale = 1.0f;
            o->Direction[1] = -70.f;
            o->Angle[1] = (float)(WorldRandom() % 360);
            CreateJoint(BITMAP_FLARE + 1, o->Position, o->Position, o->Angle, 15, o, 50.f, 40);
        }
        o->Weapon = CharacterMachine->PacketSerial;
        break;
    case MODEL_ARROW_BOMB:
        o->Velocity = 1.f;
        o->LifeTime = 30;
        AngleMatrix(o->Angle, Matrix);
        Vector(-10.f, -60.f, 135.f, p1);
        VectorRotate(p1, Matrix, p2);
        VectorAdd(o->Position, p2, o->Position);

        o->BlendMesh = 0;
        o->Scale = 1.f;
        o->Direction[1] = -30.f;
        o->Gravity = -10.f;
        o->LifeTime = 40;

        if (o->SubType == 2)
        {
            o->AttackPoint[0] = 0;
            o->Kind = 1;
        }

        o->Weapon = CharacterMachine->PacketSerial;
        break;
    case MODEL_SAW:
        o->LifeTime = 10;
        o->Position[2] += (130.f);
        AngleMatrix(o->Angle, Matrix);
        Vector(0.f, -60.f, 0.f, p1);
        VectorRotate(p1, Matrix, o->Direction);
        break;
    case MODEL_LASER:
        if (o->SubType == 0 || o->SubType == 3)
        {
            o->LifeTime = 1;
            o->BlendMesh = 0;
            o->BlendMeshLight = o->Light[0];
            o->Scale = 1.3f;
            o->RenderType = RENDER_DARK;
        }
        else
        {
            o->LifeTime = 30;
            o->Velocity = 1.f;
            o->BlendMesh = 0;
            o->BlendMeshLight = 1.f;
            o->Scale = 1.3f;
            o->RenderType = RENDER_DARK;

            o->Position[2] += (150.f);
            Vector(0.f, -1.f, 0.f, o->Direction);
            Vector(1.f, 0.f, 0.f, o->Light);
            Vector(30.f, 0.f, Angle[2], o->Angle);

            CreateJoint(BITMAP_JOINT_FORCE, o->Position, o->Position, o->Angle, 1, o, 180.f);
        }
        break;
    case MODEL_SKILL_WHEEL1:
        o->LifeTime = 5;
        CharacterMachine->PacketSerial++;
        break;
    case MODEL_SKILL_WHEEL2:
        o->LifeTime = 25; //
        o->Weapon = CharacterMachine->PacketSerial;
        break;
    case MODEL_SKILL_FURY_STRIKE: {
        VectorCopy(o->Angle, o->HeadAngle);
        VectorCopy(o->Position, o->StartPosition);

        o->LifeTime = 20; //18;
        o->SubType = WorldRandom() % 100;
        o->Angle[2] += 330.f;
        o->HeadAngle[0] += 80.f;
        o->HeadAngle[2] += 180.f;
        o->Gravity = 50.f;

        o->Weapon = CharacterMachine->PacketSerial++;
    }
    break;
    case MODEL_SKILL_FURY_STRIKE + 1:
        if (o->SubType == 0)
        {
            o->LifeTime = 35;
            o->Scale = PKKey / 100.f;
            o->BlendMesh = 0;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 60;
            o->Scale = PKKey / 100.f;
            o->BlendMesh = 0;
        }
        break;
    case MODEL_SKILL_FURY_STRIKE + 2:
        if (o->SubType == 0 || o->SubType == 1)
        {
            o->LifeTime = 20;
            o->Scale = PKKey / 100.f;
            o->BlendMesh = 0;
            if (SubType == 1)
                o->RenderType = RENDER_DARK;
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 50;
            o->Scale = PKKey / 100.f;
            o->BlendMesh = 0;
        }
        break;
    case MODEL_SKILL_FURY_STRIKE + 3:
        o->LifeTime = 35;
        o->Scale = PKKey / 100.f;
        break;
    case MODEL_SKILL_FURY_STRIKE + 4:
        o->LifeTime = 35;
        o->Scale = PKKey / 100.f;
        o->BlendMesh = 0;
        break;
    case MODEL_SKILL_FURY_STRIKE + 5:
        o->LifeTime = 40;
        o->Scale = PKKey / 100.f;
        o->BlendMesh = 0;
        if (SubType == 1)
            o->RenderType = RENDER_DARK;
        break;
    case MODEL_SKILL_FURY_STRIKE + 6:
        o->LifeTime = 35;
        o->Scale = PKKey / 100.f;
        break;
    case MODEL_SKILL_FURY_STRIKE + 7:
        o->LifeTime = 40;
        o->Scale = PKKey / 100.f;
        o->BlendMesh = 0;
        break;
    case MODEL_SKILL_FURY_STRIKE + 8:
        o->LifeTime = 40;
        o->Scale = PKKey / 100.f;
        o->BlendMesh = 0;
        if (SubType == 1)
            o->RenderType = RENDER_DARK;
        break;
    case MODEL_CHANGE_UP_EFF:
        o->BlendMesh = -2;
        o->LifeTime = 100;
        o->Scale = 0.7f;
        o->Position[0] = Position[0];
        o->Position[1] = Position[1];
        o->Position[2] = Position[2] + 22.f;
        Vector(0.f, 0.f, 0.f, o->Direction);
        if (o->SubType == 1)
        {
            o->Scale = 0.4f;
            o->LifeTime = 10;
            o->BlendMeshLight = 0.7f;
        }
        break;
    case MODEL_CHANGE_UP_NASA:
        o->BlendMesh = -2;
        o->LifeTime = 100;
        o->Scale = 0.9f;
        if (o->SubType >= 1 && o->SubType <= 3)
            o->LifeTime = 80;
        //					o->Scale = 0.f;
        o->Position[0] = Position[0];
        o->Position[1] = Position[1];
        o->Position[2] = Position[2] + 12.f;
        Vector(0.f, 0.f, 0.f, o->Direction);
        //				Vector(0.1f,0.1f,0.1f,b->BodyLight);
        break;
    case MODEL_CHANGE_UP_CYLINDER:
        o->BlendMesh = -2;
        o->LifeTime = 100;
        o->Scale = 0.9f;
        Vector(0.f, 0.f, 1.f, o->Direction);
        if (o->SubType == 1)
        {
            //o->Light
            Vector(0.f, 0.f, 0.f, o->Light);
            o->BlendMesh = -2;
            o->LifeTime = 10;
            o->Scale = 0.1f;
        }
        break;
    case MODEL_DARK_ELF_SKILL:
        o->BlendMesh = -2;
        o->LifeTime = 30;
        o->Scale = 0.0f;
        Vector(0.f, -40.f, 0.f, o->Direction);
        break;
    case MODEL_MAGIC2:
        o->BlendMesh = 0;
        o->LifeTime = 20;
        Vector(0.f, -60.f, 0.f, o->Direction);
        if (o->SubType == 2)
        {
            o->Weapon = CharacterMachine->PacketSerial++;
        }
        break;
    case MODEL_STORM:
        switch (o->SubType)
        {
        case 0:
            o->LifeTime = 59;
            o->BlendMesh = 0;
            Vector(0.f, -10.f, 0.f, o->Direction);
            //Vector(0.f,-50.f,0.f,o->Direction);
            o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);
            o->Weapon = CharacterMachine->PacketSerial++;
            break;

        case 1:
            o->LifeTime = 30;
            o->HiddenMesh = -2;
            o->Scale = 1.f;
            o->BlendMesh = 0;
            Vector(0.f, 0.f, 0.f, o->Direction);
            break;

        case 2:
            o->LifeTime = 59;
            o->HiddenMesh = -2;
            o->Scale = 1.f;
            o->BlendMesh = 0;
            Vector(0.f, -10.f, 0.f, o->Direction);
            Vector(1.f, 1.f, 1.f, o->Light);
            break;
        case 3:
        case 4:
        case 5:
        case 6:
        case 7: {
            o->LifeTime = 60;
            o->HiddenMesh = -2;
            o->Scale = 1.0f;

            if (o->SubType == 3)
            {
                Vector(0.f, -12.f, 0.f, o->Direction);
            }
            else if (o->SubType == 4)
            {
                Vector(7.f, -6.f, 0.f, o->Direction);
            }
            else if (o->SubType == 5)
            {
                Vector(-7.f, -6.f, 0.f, o->Direction);
            }
            else if (o->SubType == 6)
            {
                Vector(4.f, 5.f, 0.f, o->Direction);
            }
            else
            {
                Vector(-4.f, 5.f, 0.f, o->Direction);
            }
        }
        break;
        case 8: {
            o->LifeTime = 100;
            o->BlendMesh = 0;
            o->Scale = 2.0f;
        }
        break;
        }
        break;
    case MODEL_SUMMON: {
        o->LifeTime = 60;
        o->BlendMesh = 0;
        if (o->SubType == 0)
            o->Scale = 0.7f;
        else
            o->Scale = 1.2f;
        o->Angle[2] += (WorldRandom() % 360);
    }
    break;
    case MODEL_STORM2: {
        if (o->SubType == 0)
        {
            o->Angle[0] = 90.0f;
            o->LifeTime = 35;
            o->BlendMesh = 0;
            o->Scale = 0.5 + WorldRandom() % 10 / 100.0f;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 60;
            o->BlendMesh = 0;
            o->Gravity = WorldRandom() % 10 + 30.0f;
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 100;
            o->Angle[0] = 90.0f;
            o->BlendMesh = 0;
            o->Scale = 1.0f;
        }
    }
    break;
    case MODEL_STORM3: {
        o->LifeTime = 50;
        o->BlendMesh = 0;
        o->Angle[0] = 90.0f;
        o->Scale = 3.5f;
        VectorCopy(Hero->Object.Position, o->StartPosition);
    }
    break;
    case MODEL_MAYASTONE1:
    case MODEL_MAYASTONE2:
    case MODEL_MAYASTONE3: {
        o->LifeTime = 40;
        o->Scale = 6.0f + (float)(WorldRandom() % 8 + 15) * 0.1f;
        Vector(0.f, 0.f, -60.f, o->Direction);
        Vector(0.f, 30.f, 0.f, o->HeadAngle);

        CreateJoint(BITMAP_SMOKE, o->Position, o->Position, o->HeadAngle, 2, o, 100.f);
    }
    break;
    case MODEL_MAYASTONEFIRE: {
        if (o->SubType == MODEL_MAYASTONE1)
            o->Scale = Scale * 0.8f;
        else if (o->SubType == MODEL_MAYASTONE2)
            o->Scale = Scale * 0.6f;
        else if (o->SubType == MODEL_MAYASTONE3)
            o->Scale = Scale * 0.5f;
        o->LifeTime = 40;
        Vector(0.f, 0.f, -60.f, o->Direction);
        Vector(0.f, 30.f, 0.f, o->HeadAngle);
    }
    break;
    case MODEL_MAYAHANDSKILL: {
        if (o->SubType == 0)
        {
            o->LifeTime = 20;
            VectorCopy(o->Light, o->StartPosition);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 20;
            VectorCopy(o->Light, o->StartPosition);
        }
    }
    break;
    case MODEL_CIRCLE:
        o->LifeTime = 45;
        o->BlendMesh = 0;
        if (o->SubType == 0)
        {
            if (o->Owner == &Hero->Object)
            {
                o->Weapon = CharacterMachine->PacketSerial++;
                AttackCharacterRange(o->Skill, o->Position, 300.f, o->Weapon, o->PKKey);
            }
        }
        else if (o->SubType == 1 || o->SubType == 4)
        {
            o->LifeTime = 45;
            o->Scale = 1.f;
            o->HiddenMesh = -2;
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 250;
            o->Scale = 1.f;
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = 30;
            o->Scale = 1.f;
        }
        break;
    case MODEL_CIRCLE_LIGHT:
        o->LifeTime = 40;
        o->BlendMesh = 0;
        //o->BlendMeshLight = 0.f;
        if (SubType == 1)
            o->RenderType = RENDER_DARK;
        else if (o->SubType == 2)
        {
            o->LifeTime = 40;
            o->Scale = 1.f;
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = 250;
            o->Scale = 1.f;
        }
        else if (o->SubType == 4)
        {
            o->LifeTime = 20;
            o->Scale = 1.f;
        }
        break;
    case MODEL_ICE:
        switch (o->SubType)
        {
        case 0:
            o->LifeTime = 50;
            o->Scale = 0.8f;
            o->Velocity = 1.f;
            o->Angle[0] = 0.f;
            o->BlendMesh = 0;
            break;

        case 1:
        case 2:
            o->LifeTime = 20;
            o->Scale = 0.8f;
            o->Angle[0] = -20.f;
            o->BlendMesh = 0;
            o->BlendMeshLight = 0.5f;
            o->Gravity = 5.f;
            Vector(0.f, 0.f, (float)(WorldRandom() % 360), o->HeadAngle);

            for (int i = 0; i < 3; ++i)
            {
                vec3_t Position;
                Vector(o->Position[0] + (float)(WorldRandom() % 64 - 32),
                       o->Position[1] + (float)(WorldRandom() % 64 - 32),
                       o->Position[2] + (float)(WorldRandom() % 128 + 32), Position);
                CreateParticle(BITMAP_SMOKE, Position, o->Angle, o->Light);
            }
            Vector(0.4f, 0.3f, 0.2f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);

            VectorCopy(o->Position, o->StartPosition);
            break;
        }
        break;
    case MODEL_SNOW1:
        AngleMatrix(o->Angle, Matrix);
        Vector(0.f, -40.f, 150.f, p1);
        VectorRotate(p1, Matrix, p2);
        VectorAdd(o->Position, p2, o->Position);
        o->Direction[1] = -40.f;
        o->Direction[2] = 10.f;
        o->LifeTime = 20;
        o->Scale = 1.2f;
        break;
    case MODEL_WOOSISTONE:
        if (o->SubType == 1)
        {
            o->Direction[0] = 0;
            o->Direction[1] = 0;
            o->Direction[2] = 0;

            o->LifeTime = WorldRandom() % 16 + 20;
            o->Scale = (float)(WorldRandom() % 13 + 3) * 0.04f;
            o->Gravity = (float)(WorldRandom() % 3 + 3);

            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            Vector(0.f, (float)(WorldRandom() % 128 + 64) * 0.1f, 0.f, p1);
            VectorRotate(p1, Matrix, o->HeadAngle);
            o->HeadAngle[2] += (15.0f);
            break;
        }
        else
        {
            AngleMatrix(o->Angle, Matrix);
            Vector(0.f, -60.f, 150.f, p1);
            VectorRotate(p1, Matrix, p2);
            VectorAdd(o->Position, p2, o->Position);
            o->Direction[1] = -40.f;
            o->Direction[2] = 10.f;
            o->LifeTime = 20;
            o->Scale = 1.0f;
            break;
        }
    case MODEL_SKULL:
        if (o->SubType == 0)
        {
            o->LifeTime = 1;
            o->Scale = 2.3f;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 49;
            o->Scale = 2.f;
            o->BlendMesh = -2;
            o->BlendMeshLight = 0.5f;
            Vector(0.f, 0.f, 0.f, o->HeadAngle);
            Vector(0.f, -45.f, 0.f, o->Direction);
            VectorCopy(o->Position, o->StartPosition);
            VectorCopy(o->Position, o->m_vDeadPosition);

            VectorCopy(o->Position, o->EyeLeft);
            VectorCopy(o->Position, o->EyeRight);

            CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 10, o, 30.f);
            CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 11, o, 30.f);
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
        switch (o->SubType)
        {
        case 1:
            o->LifeTime = 100;
            o->Scale = 2.0f;
            break;
        case 2:
        case 3:
            Vector(o->Position[0], o->Position[1],
                   RequestTerrainHeight(o->Position[0], o->Position[1]) - (float)o->PKKey - 80,
                   o->StartPosition);
            Vector(0, 0, 0, o->Direction);
            o->LifeTime = 250 * 2;
            o->Scale = 2.0f;
            o->HeadAngle[0] = (float)(WorldRandom() % 2);
            if (o->SubType == 2)
                o->HeadAngle[0] = 0;
            break;
        case 4:
            Vector(o->Position[0], o->Position[1],
                   RequestTerrainHeight(o->Position[0], o->Position[1]) - (float)o->PKKey,
                   o->StartPosition);
            Vector(0, 0, 0, o->Direction);
        case 5:
            o->CurrentAction = 0;
            o->LifeTime = 170;
            o->Scale = 2.0f;
            break;
        }
        o->PKKey = -1;
        o->Owner = Owner;
        break;
    case MODEL_CURSEDTEMPLE_STATUE_PART1:
    case MODEL_CURSEDTEMPLE_STATUE_PART2:
        o->LifeTime = 180 + (WorldRandom() % 40);
        o->Scale = 0.1f + (WorldRandom() % 6) * 0.1f;
        o->Velocity = 0.f;
        o->PKKey = -1;
        o->Owner = Owner;
        o->Gravity = 2.5f;
        o->Direction[0] = (float)((-15 + (WorldRandom() % 30)));
        o->Direction[1] = (float)((-15 + (WorldRandom() % 30)));
        o->Direction[2] = 0;
        o->Angle[0] = 0; //( float)( WorldRandom() % 360);
        o->Angle[1] = 0; //( float)( WorldRandom() % 360);
        o->Angle[2] = (float)(WorldRandom() % 360);
        o->Light[0] = o->Light[1] = o->Light[2] = 0.5 + (WorldRandom() % 6) * 0.1f;
        break;
    case MODEL_XMAS2008_SNOWMAN_HEAD: {
        o->LifeTime = 50;
        o->Scale = 1.3f;

        o->Angle[2] = o->Owner->Angle[2];
        AngleMatrix(o->Angle, Matrix);
        o->Gravity = 5.0f;
        vec3_t p;
        Vector((float)(WorldRandom() % 10 - 5) * 0.1f, (float)(WorldRandom() % 60 - 40) * 0.1f,
               0.0f, p);
        VectorScale(p, 1.3f, p);
        VectorRotate(p, Matrix, o->Direction);
        o->m_iAnimation = WorldRandom() % 3;
    }
    break;
    case MODEL_XMAS2008_SNOWMAN_BODY: {
        o->LifeTime = 50;
        o->Scale = 1.3f;
        o->Velocity = 1.2f;

        o->PKKey = -1;
        o->Owner = Owner;

        o->Direction[0] = o->Owner->Direction[0];
        o->Direction[1] = o->Owner->Direction[1];
        o->Direction[2] = o->Owner->Direction[2];
    }
    break;
    case MODEL_TOTEMGOLEM_PART1:
    case MODEL_TOTEMGOLEM_PART2:
    case MODEL_TOTEMGOLEM_PART3:
    case MODEL_TOTEMGOLEM_PART4:
    case MODEL_TOTEMGOLEM_PART5:
    case MODEL_TOTEMGOLEM_PART6:
        o->LifeTime = 30 + (WorldRandom() % 30);
        o->Scale = 0.17f; //0.1f+(WorldRandom()%6)*0.1f;
        o->Velocity = 0.f;
        o->PKKey = -1;
        o->Owner = Owner;
        o->Gravity = 3.5f;

        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        Vector(0.f, (float)(WorldRandom() % 64 + 48) * 0.1f, 0.f, p1);
        VectorRotate(p1, Matrix, o->HeadAngle);
        o->HeadAngle[2] = 15;

        o->SubType = WorldRandom() % 2;

        o->Light[0] = o->Light[1] = o->Light[2] = 0.5 + (WorldRandom() % 6) * 0.1f;
        o->Direction[0] = 0;
        o->Direction[1] = 0;
        o->Direction[2] = 0;
        break;
#ifdef ASG_ADD_KARUTAN_MONSTERS
    case MODEL_CONDRA_ARM_L:
    case MODEL_CONDRA_ARM_L2:
    case MODEL_CONDRA_SHOULDER:
    case MODEL_CONDRA_ARM_R:
    case MODEL_CONDRA_ARM_R2:
    case MODEL_CONDRA_CONE_L:
    case MODEL_CONDRA_CONE_R:
    case MODEL_CONDRA_PELVIS:
    case MODEL_CONDRA_STOMACH:
    case MODEL_CONDRA_NECK:

    case MODEL_NARCONDRA_ARM_L:
    case MODEL_NARCONDRA_ARM_L2:
    case MODEL_NARCONDRA_SHOULDER_L:
    case MODEL_NARCONDRA_SHOULDER_R:
    case MODEL_NARCONDRA_ARM_R:
    case MODEL_NARCONDRA_ARM_R2:
    case MODEL_NARCONDRA_ARM_R3:
    case MODEL_NARCONDRA_CONE_1:
    case MODEL_NARCONDRA_CONE_2:
    case MODEL_NARCONDRA_CONE_3:
    case MODEL_NARCONDRA_CONE_4:
    case MODEL_NARCONDRA_CONE_5:
    case MODEL_NARCONDRA_CONE_6:
    case MODEL_NARCONDRA_PELVIS:
    case MODEL_NARCONDRA_STOMACH:
    case MODEL_NARCONDRA_NECK:
        o->LifeTime = 30 + (WorldRandom() % 30);
        o->Scale = 1.4f;
        o->Velocity = 0.f;
        o->PKKey = -1;
        o->Owner = Owner;
        o->Gravity = 3.5f;

        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        Vector(0.f, (float)(WorldRandom() % 64 + 48) * 0.1f, 0.f, p1);
        VectorRotate(p1, Matrix, o->HeadAngle);
        o->HeadAngle[2] = 15;

        o->SubType = WorldRandom() % 2;

        o->Light[0] = o->Light[1] = o->Light[2] = 0.5 + (WorldRandom() % 6) * 0.1f;
        o->Direction[0] = 0;
        o->Direction[1] = 0;
        o->Direction[2] = 0;
        break;
#endif // ASG_ADD_KARUTAN_MONSTERS
    case MODEL_DOPPELGANGER_SLIME_CHIP:
        o->LifeTime = 30 + (WorldRandom() % 10);
        o->Scale = 0.1f + (WorldRandom() % 7) * 0.1f;
        o->Velocity = 0.f;
        o->PKKey = -1;
        o->Owner = Owner;
        o->Gravity = 4.0f;

        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        Vector(0.f, (float)(WorldRandom() % 64 + 48) * 0.1f, 0.f, p1);
        VectorRotate(p1, Matrix, o->HeadAngle);
        o->HeadAngle[2] = 35 + WorldRandom() % 5;

        o->SubType = WorldRandom() % 2;

        VectorCopy(Light, o->Light);
        o->Direction[0] = 0;
        o->Direction[1] = 0;
        o->Direction[2] = 0;

        o->Angle[0] = (float)(WorldRandom() % 360);
        o->Angle[1] = (float)(WorldRandom() % 360);
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

    case MODEL_ICE_GIANT_PART1:
    case MODEL_ICE_GIANT_PART2:
    case MODEL_ICE_GIANT_PART3:
    case MODEL_ICE_GIANT_PART4:
    case MODEL_ICE_GIANT_PART5:
    case MODEL_ICE_GIANT_PART6:

        o->LifeTime = 30 + (WorldRandom() % 30);
        o->Scale = 1.1f; //0.1f+(WorldRandom()%6)*0.1f;
        o->Velocity = 0.f;
        o->PKKey = -1;
        o->Owner = Owner;
        o->Gravity = 3.5f;

        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        Vector(0.f, (float)(WorldRandom() % 64 + 48) * 0.1f, 0.f, p1);
        VectorRotate(p1, Matrix, o->HeadAngle);
        o->HeadAngle[2] = 15;

        o->SubType = WorldRandom() % 2;

        o->Light[0] = o->Light[1] = o->Light[2] = 0.5 + (WorldRandom() % 6) * 0.1f;
        o->Direction[0] = 0;
        o->Direction[1] = 0;
        o->Direction[2] = 0;
        break;
    case MODEL_SHADOW_ROOK_ANKLE_LEFT:
    case MODEL_SHADOW_ROOK_ANKLE_RIGHT:
    case MODEL_SHADOW_ROOK_BELT:
    case MODEL_SHADOW_ROOK_CHEST:
    case MODEL_SHADOW_ROOK_HELMET:
    case MODEL_SHADOW_ROOK_KNEE_LEFT:
    case MODEL_SHADOW_ROOK_KNEE_RIGHT:
    case MODEL_SHADOW_ROOK_WRIST_LEFT:
    case MODEL_SHADOW_ROOK_WRIST_RIGHT:
        o->LifeTime = 30 + (WorldRandom() % 30);
        o->Scale = 1.3f; //0.1f+(WorldRandom()%6)*0.1f;
        o->Velocity = 0.f;
        o->PKKey = -1;
        o->Owner = Owner;
        o->Gravity = 3.5f;

        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        Vector(0.f, (float)(WorldRandom() % 64 + 48) * 0.1f, 0.f, p1);
        VectorRotate(p1, Matrix, o->HeadAngle);
        o->HeadAngle[2] = 15;

        o->SubType = WorldRandom() % 2;

        o->Light[0] = o->Light[1] = o->Light[2] = 0.5 + (WorldRandom() % 6) * 0.1f;
        o->Direction[0] = 0;
        o->Direction[1] = 0;
        o->Direction[2] = 0;
        break;

    case MODEL_WATER_WAVE:
        o->BlendMesh = -2;
        o->BlendMeshLight = 0.2f;
        o->Scale = 0.4f;
        o->LifeTime = 20;
        o->Gravity = 120.f;
        o->Velocity = -40.f;
        Vector(0.f, o->Velocity, 0.f, o->Direction);
        Vector(1.f, 1.f, 1.f, o->Light);
        VectorCopy(o->Position, o->StartPosition);
        break;

    case MODEL_FIRE:
        o->BlendMesh = 1;
        if (o->SubType == 0)
        {
            o->LifeTime = 40;
            o->Scale = (float)(WorldRandom() % 8 + 10) * 0.1f;
            o->Position[0] += 130.f + (float)(WorldRandom() % 32);
            o->Position[2] += 400.f;
            Vector(0.f, 0.f, -50.f, o->Direction);
            Vector(0.f, 20.f, 0.f, o->Angle);
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 40;
            o->Scale = (float)(WorldRandom() % 8 + 10) * 0.1f;
            Vector(0.f, 0.f, -50.f, o->Direction);
            //o->Angle[0] = -20.f;
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = 80;
            o->Scale = 0.3f;
            Vector(0.f, -12.f, 0.f, o->Direction);
            //					PlayBuffer( SOUND_PHOENIXFIRE);
        }
        else if (o->SubType == 4)
        {
            o->LifeTime = 40;
            o->Scale = (float)(WorldRandom() % 10 + 15) * 0.1f;
            o->Position[0] += 130.f + (float)(WorldRandom() % 32);
            o->Position[2] += 400.f;
            Vector(0.f, -(WorldRandom() % 20 + 10.f), -(WorldRandom() % 10 + 20.f), o->Direction);
            Vector(0.f, 20.f, 0.f, o->Angle);
        }
        else if (o->SubType == 5)
        {
            o->LifeTime = 40;
            o->Gravity = 5.f;
            Vector(0.f, -30.f, 0.f, o->Direction);
        }
        else if (o->SubType == 6)
        {
            o->LifeTime = 40;
            o->HiddenMesh = -2;
            o->Scale = (float)(WorldRandom() % 8 + 15) * 0.1f;
            o->PKKey = -1;
            o->Velocity = PKKey;
            o->Position[0] += (float)(WorldRandom() % 100 + 200);
            o->Position[1] += (float)(WorldRandom() % 100 - 50);
            o->Position[2] += (float)(WorldRandom() % 300 + 500);
            Vector(0.f, 0.f, -50.f - WorldRandom() % 50, o->Direction);
            Vector(0.f, 20.f, 0.f, o->HeadAngle);

            CreateJoint(BITMAP_SMOKE, o->Position, o->Position, o->HeadAngle, 0, o, 100.f);
        }
        else if (o->SubType == 7)
        {
            o->LifeTime = 40;
            o->HiddenMesh = -2;
            o->Scale = (float)(WorldRandom() % 8 + 15) * 0.1f;
            o->PKKey = -1;
            o->Velocity = PKKey;
            o->Position[0] += (float)(WorldRandom() % 100 + 200);
            o->Position[1] += (float)(WorldRandom() % 100 - 50);
            o->Position[2] += (float)(WorldRandom() % 300 + 500);
            Vector(0.f, 0.f, -50.f - WorldRandom() % 50, o->Direction);
            Vector(0.f, 20.f, 0.f, o->HeadAngle);
        }
        else if (o->SubType == 8)
        {
            o->LifeTime = 40;
            o->HiddenMesh = -2;
            o->Scale = (float)(WorldRandom() % 8 + 15) * 0.1f;
            o->PKKey = -1;
            o->Velocity = PKKey;
            //					o->Position[0] += WorldRandom()%100+200;
            //					o->Position[1] += WorldRandom()%100-50;
            o->Position[2] += (float)(WorldRandom() % 300 + 500);
            Vector(0.f, 0.f, -50.f - WorldRandom() % 50, o->Direction);
            Vector(0.f, (float)(5 + WorldRandom() % 5), 0.f, o->HeadAngle);

            CreateJoint(BITMAP_SMOKE, o->Position, o->Position, o->HeadAngle, 0, o, 60.f);
        }
        else if (o->SubType == 9)
        {
            o->BlendMeshLight = 0.f;
            o->LifeTime = 600;
            o->Scale = (float)(WorldRandom() % 10 + 20) * 0.13f;
            o->Position[0] += (130.f + WorldRandom() % 32);
            o->Position[2] += (400.f + WorldRandom() % 32);
            Vector(0.f, -WorldRandom() % 5 - 8.0f, -10.f - WorldRandom() % 10, o->Direction);
            Vector(0.f, 0.f, 0.f, o->Angle);
        }
        else
        {
            o->LifeTime = 60;
            o->Scale = (float)(WorldRandom() % 4 + 8) * 0.1f;
            o->Position[2] += (120.f);
            Vector(0.f, -50.f, 0.f, o->Direction);
        }
        break;
    case MODEL_BONE1:
        o->Position[2] += (50.f);
    case MODEL_BONE2:
        o->Position[2] += (100.f);
    case MODEL_BIG_STONE1:
    case MODEL_BIG_STONE2:
        if (o->SubType == 5)
        {
            o->LifeTime = 60;
            o->Scale = (float)(WorldRandom() % 4 + 8) * 0.1f;
            VectorCopy(o->Position, o->StartPosition);
            VectorCopy(o->Owner->Position, o->Position);
            break;
        }
    case MODEL_SNOW2:
    case MODEL_SNOW3:
    case MODEL_STONE1:
    case MODEL_STONE2:
        if (o->SubType == 5)
        {
            o->LifeTime = 60;
            o->Scale = (float)(WorldRandom() % 4 + 8) * 0.1f;
            break;
        }
        else if (o->SubType == 11)
        {
            o->LifeTime = 40;
            o->Scale = (float)(WorldRandom() % 4 + 8) * 0.8f;
            o->Angle[2] = (float)(WorldRandom() % 360);
            break;
        }
        else if (o->SubType == 10)
        {
            Vector(0.f, (float)(WorldRandom() % 256 + 64) * 0.2, 0.f, p1);
            o->LifeTime = WorldRandom() % 16 + 32;
            o->Scale = (float)(WorldRandom() % 4 + 15) * 0.05f;
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(p1, Matrix, o->Direction);
            o->Gravity = (float)(WorldRandom() % 16 + 28);
            break;
        }
        else if (o->SubType == 12)
        {
            Vector(0.f, (float)(WorldRandom() % 256 + 64) * 0.1f, 0.f, p1);
            o->LifeTime = WorldRandom() % 16 + 32;
            o->Scale = (float)(WorldRandom() % 4 + 15) * 0.1f;
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(p1, Matrix, o->Direction);
            o->Gravity = (float)(WorldRandom() % 16 + 8);
            break;
        }
        else if (o->SubType == 13 || o->SubType == 14)
        {
            o->Direction[0] = 0;
            o->Direction[1] = 0;
            o->Direction[2] = 0;

            o->LifeTime = WorldRandom() % 16 + 20;
            o->Scale = (float)(WorldRandom() % 13 + 3) * 0.08f;
            o->Scale *= Scale;
            o->Gravity = (float)(WorldRandom() % 3 + 3);

            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            Vector(0.f, (float)(WorldRandom() % 128 + 64) * 0.1f, 0.f, p1);
            VectorRotate(p1, Matrix, o->HeadAngle);
            o->HeadAngle[2] += (15.0f);
            break;
        }

    case MODEL_ICE_SMALL:
    case MODEL_METEO1:
    case MODEL_METEO2:
    case MODEL_EFFECT_SAPITRES_ATTACK_2:
        if (o->Type == MODEL_BIG_STONE1 || o->Type == MODEL_BIG_STONE2)
        {
            Vector((float)(WorldRandom() % 128 - 64), (float)(WorldRandom() % 128 - 64),
                   (float)(WorldRandom() % 180), p1);
            VectorAdd(o->Position, p1, o->Position);
        }

        if (Type == MODEL_ICE_SMALL)
        {
            o->BlendMesh = 0;
            o->BlendMeshLight = 0.3f;
            Vector(0.f, (float)(WorldRandom() % 256 + 64) * 0.1f, 0.f, p1);
            o->Position[2] += (50.f);
            if (o->SubType == 13)
            {
                Vector(0.f, (float)(WorldRandom() % 256 + 64) * 0.2, 0.f, p1);
                o->LifeTime = WorldRandom() % 16 + 32;
                o->Scale = (float)(WorldRandom() % 4 + 15) * 0.05f;
                o->Angle[2] = (float)(WorldRandom() % 360);
                AngleMatrix(o->Angle, Matrix);
                VectorRotate(p1, Matrix, o->Direction);
                o->Gravity = (float)(WorldRandom() % 16 + 28);
            }
            //						o->Scale += 50.f;
        }
        else if (Type == MODEL_EFFECT_SAPITRES_ATTACK_2)
        {
            o->BlendMesh = 0;
            o->BlendMeshLight = 1.0f;
            Vector(0.f, (float)(WorldRandom() % 256 + 64) * 0.1f, 0.f, p1);
            o->Position[2] += (50.f);
        }
        else
        {
            if (o->SubType == 1)
            {
                Vector(0.f, 0.f, 0.f, p1);
            }
            else
            {
                Vector(0.f, (float)(WorldRandom() % 256 + 64) * 0.1f, 0.f, p1);
            }
        }
        if (o->SubType == 13)
        {
            o->LifeTime = WorldRandom() % 16 + 100;
            o->Scale = (float)(WorldRandom() % 4 + 8) * 0.1f;
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(p1, Matrix, o->Direction);
            o->Gravity = (float)(WorldRandom() % 16 + 8);
        }
        else if (Type == MODEL_EFFECT_SAPITRES_ATTACK_2 && o->SubType == 14)
        {
            o->LifeTime = WorldRandom() % 5 + 15;
            o->Angle[0] = (float)(WorldRandom() % 60 - 30);
            o->Angle[1] = (float)(WorldRandom() % 60 - 30);
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(p1, Matrix, o->Direction);
            VectorNormalize(o->Direction);
            o->Gravity = 0.f;
        }
        else
        {
            o->LifeTime = WorldRandom() % 16 + 32;
            o->Scale = (float)(WorldRandom() % 4 + 8) * 0.1f;
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(p1, Matrix, o->Direction);
            o->Gravity = (float)(WorldRandom() % 16 + 8);
        }
        break;

    case MODEL_EFFECT_BROKEN_ICE0:
    case MODEL_EFFECT_BROKEN_ICE1:
    case MODEL_EFFECT_BROKEN_ICE2:
    case MODEL_EFFECT_BROKEN_ICE3:
#ifdef ASG_ADD_KARUTAN_MONSTERS
    case MODEL_CONDRA_STONE:
    case MODEL_CONDRA_STONE1:
    case MODEL_CONDRA_STONE2:
    case MODEL_CONDRA_STONE3:
    case MODEL_CONDRA_STONE4:
    case MODEL_CONDRA_STONE5:
    case MODEL_NARCONDRA_STONE:
    case MODEL_NARCONDRA_STONE1:
    case MODEL_NARCONDRA_STONE2:
    case MODEL_NARCONDRA_STONE3:
#endif // ASG_ADD_KARUTAN_MONSTERS
        if (o->SubType == 0)
        {
            o->Direction[0] = 0;
            o->Direction[1] = 0;
            o->Direction[2] = 0;

            o->LifeTime = WorldRandom() % 16 + 35;
            o->Scale = (float)(WorldRandom() % 13 + 3) * 0.2f;
            o->Gravity = (float)(WorldRandom() % 3 + 3);

            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            Vector(0.f, (float)(WorldRandom() % 128 + 64) * 0.1f, 0.f, p1);
            VectorRotate(p1, Matrix, o->HeadAngle);
            o->HeadAngle[2] += (25.0f);
            break;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 40;
            o->Scale = Scale + (float)(WorldRandom() % 8 + 15) * 0.1f;
            Vector(0.f, 0.f, -60.f, o->Direction);
            Vector(0.f, 30.f, 0.f, o->HeadAngle);
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 100;
            o->Scale = Scale;
            o->Gravity = 20.f + (float)(WorldRandom() % 20) * 0.5f;
        }
        break;
    case MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_HEAD:
    case MODEL_PKFIELD_ASSASSIN_EFFECT_RED_HEAD: {
        o->LifeTime = 50 + (WorldRandom() % 30);
        o->Scale = 1.0f;
        o->Velocity = 1.0f;
        o->PKKey = -1;
        o->Owner = Owner;
        o->Gravity = 3.5f;

        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        Vector(0.f, (float)(WorldRandom() % 64 + 25) * 0.2f, 0.f, p1);
        VectorRotate(p1, Matrix, o->HeadAngle);
        o->HeadAngle[2] = 25;

        o->SubType = WorldRandom() % 2;

        o->Direction[0] = 0;
        o->Direction[1] = 0;
        o->Direction[2] = 0;
    }
    break;
    case MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_BODY:
    case MODEL_PKFIELD_ASSASSIN_EFFECT_RED_BODY: {
        o->Scale = 1.0f;
        o->LifeTime = 40 + (WorldRandom() % 30);
        o->Velocity = 0.4f; //b->Actions[MONSTER01_DIE].PlaySpeed
        o->CurrentAction = MONSTER01_DIE;
    }
    break;
    case MODEL_DUNGEON_STONE01:
        o->LifeTime = WorldRandom() % 16 + 24;
        o->Scale = (float)(WorldRandom() % 8 + 6) * 0.1f;
        o->Gravity = -(float)(WorldRandom() % 4);
        Vector((float)(WorldRandom() % 64 - 32), -(float)(WorldRandom() % 32 + 50),
               (float)(WorldRandom() % 128 + 200), p1);
        AngleMatrix(o->Angle, Matrix);
        VectorRotate(p1, Matrix, p2);
        VectorAdd(o->Position, p2, o->Position);
        break;
    case MODEL_WARCRAFT:
        o->LifeTime = 50;
        o->Scale = 0.7f;
        if (SubType == 0)
            o->BlendMesh = -3;
        else
            o->BlendMesh = -4;
        Vector(0.f, 0.f, 45.f, o->Angle);
        break;
    case BITMAP_FIRECRACKERRISE:
        ZeroMemory(o->Angle, sizeof(o->Angle));
        o->Position[2] = 100.0f;
        o->LifeTime = 15 * 5;
        break;
    case BITMAP_FIRECRACKER:
        Vector(1.0f, 1.0f, 1.0f, o->Light);
        Vector((float)(WorldRandom() % 9 - 4), (float)(WorldRandom() % 9 - 4), 26.f, o->Direction);

        if (o->SubType == 1)
            o->LifeTime = 4 + (WorldRandom() % 3);
        else
            o->LifeTime = 12;

        PlayBuffer(SOUND_FIRECRACKER1, o);
        break;
    case BITMAP_FIRECRACKER0001: {
        o->LifeTime = 31;
        Vector(0, 0, 0.f, o->Direction);
    }
    break;
    case BITMAP_FIRECRACKER0002: {
        o->LifeTime = 30;

        CreateParticle(BITMAP_EXPLOTION_MONO, o->Position, o->Angle, o->Light, 0, 0.6f);

        vec3_t vLight;
        Vector(1.0f, 1.0f, 1.0f, vLight);
        for (int i = 0; i < 60; i++)
        {
            CreateParticle(BITMAP_SPARK + 1, o->Position, o->Angle, vLight, 27);
        }

        for (int i = 0; i < 30; i++)
        {
            CreateParticle(BITMAP_SPARK + 1, o->Position, o->Angle, vLight, 28);
        }

        Vector(o->Position[0] + (WorldRandom() % 100 - 50),
               o->Position[1] + (WorldRandom() % 100 - 50), o->Position[2], Position);
        CreateSprite(BITMAP_DS_SHOCK, Position, WorldRandom() % 10 * 0.1f + 1.5f, o->Light, o);

        for (int i = 0; i < 60; i++)
        {
            Vector(0.3f + (WorldRandom() % 700) * 0.001f, 0.3f + (WorldRandom() % 700) * 0.001f,
                   0.3f + (WorldRandom() % 700) * 0.001f, vLight);
            CreateParticle(BITMAP_SHINY, o->Position, o->Angle, vLight, 6);
        }

        if (o->SubType == 1)
        {
            CreateEffect(MODEL_HALLOWEEN_CANDY_STAR, o->Position, o->Angle, vLight, 1);
            CreateEffect(WorldRandom() % 4 + MODEL_XMAS_EVENT_BOX, o->Position, o->Angle, vLight, 0,
                         o);
            CreateEffect(WorldRandom() % 4 + MODEL_XMAS_EVENT_BOX, o->Position, o->Angle, vLight, 0,
                         o);
        }

        PlayBuffer(SOUND_XMAS_FIRECRACKER, o);
    }
    break;
    case BITMAP_FIRECRACKER0003: {
        o->LifeTime = 15;
        o->Angle[2] = WorldRandom() % 360;
    }
    break;
    case BITMAP_SWORD_FORCE:
        o->LifeTime = 30;
        if (o->SubType == 0 || o->SubType == 1)
            Vector(0.8f, 0.8f, 0.8f, o->Light);
        Vector(0.f, 0.f, Angle[2] + 45.f, o->HeadAngle);
        VectorCopy(o->Position, o->StartPosition);
        break;
    case MODEL_CLOUD:
        o->LifeTime = 2;
        o->BlendMesh = 0;
        o->Scale = 10.f;
        o->Position[1] += (200.f);
        o->Position[2] -= (190.f);
        o->LightEnable = false;
        break;
    case BITMAP_BLIZZARD: {
        o->LifeTime = WorldRandom() % 15 + 15;
        o->Gravity = -20.f;
        o->Velocity = (float)(WorldRandom() % 360);
        Vector(0.f, 0.f, 0.f, o->Light);

        int rangeX, rangeY, rangeZ;
        if (o->SubType == 1)
        {
            rangeX = 300;
            rangeY = 150;
            rangeZ = 700;
            o->Scale = 0.5f;
            o->Gravity -= (WorldRandom() % 20 + 10);
        }
        else
        {
            rangeX = 200;
            rangeY = 100;
            rangeZ = 500;
            o->Scale = 0.f;
        }

        o->Position[0] = o->Position[0] + WorldRandom() % rangeX - rangeY;
        o->Position[1] = o->Position[1] + WorldRandom() % rangeX - rangeY;
        o->Position[2] = o->Position[2] + 500.f;
        o->Position[0] += (100.f);

        VectorCopy(o->Position, o->StartPosition);
        PlayBuffer(SOUND_METEORITE01);
    }
    break;
    case BITMAP_SHOTGUN: {
        o->LifeTime = 10;
        o->Velocity = 1.f;
        Vector(0.f, -30.f, 0.f, o->Direction);
        AngleMatrix(o->Angle, Matrix);
        Vector(0.f, -20.f, 50.f, p1);
        VectorRotate(p1, Matrix, p2);
        VectorAdd(o->Position, p2, o->Position);

        vec3_t Angle, Pos, p3;
        Vector(-20.f, -20.f, 60.f, p1);
        VectorRotate(p1, Matrix, p2);
        VectorAdd(Position, p2, Pos);
        VectorCopy(o->Angle, Angle);
        for (int i = 0; i < 20; ++i)
        {
            Angle[0] = o->Angle[0] + WorldRandom() % 20 + 5;
            Angle[1] += i * 18;
            CreateJoint(BITMAP_JOINT_SPARK, Pos, Pos, Angle, 1);
        }

        Vector(30.f, -20.f, 60.f, p3);
        VectorRotate(p3, Matrix, p2);
        VectorAdd(Position, p2, Pos);
        VectorCopy(o->Angle, Angle);

        for (int i = 0; i < 20; ++i)
        {
            Angle[0] = o->Angle[0] + WorldRandom() % 20 + 5;
            Angle[1] += i * 18;
            CreateJoint(BITMAP_JOINT_SPARK, Pos, Pos, Angle, 1);
        }
    }
    break;
    case MODEL_GATE:
    case MODEL_GATE + 1:
        Vector(0.f, (float)(WorldRandom() % 128 + 64) * 0.1f, 0.f, p1);
        o->Position[2] += (50.f);
        o->LifeTime = WorldRandom() % 16 + 32;
        o->Scale = (float)(WorldRandom() % 4 + 8) * 0.1f;
        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        VectorRotate(p1, Matrix, o->Direction);
        o->Gravity = (float)(WorldRandom() % 5 + 2);
        if (o->Type == MODEL_GATE && o->SubType == 0)
        {
            o->SubType = 1;
            o->Gravity += ((float)(WorldRandom() % 5));
        }
        else if (o->SubType == 2)
        {
            o->Scale *= 0.6f;
        }

        o->Angle[0] = (float)(WorldRandom() % 360);
        o->Angle[1] = (float)(WorldRandom() % 360);
        o->Angle[2] = (float)(WorldRandom() % 360);
        break;

    case MODEL_STONE_COFFIN:
    case MODEL_STONE_COFFIN + 1:
        Vector(0.f, (float)(WorldRandom() % 128 + 32) * 0.1f, 0.f, p1);
        o->Position[2] += (50.f);
        o->LifeTime = WorldRandom() % 16 + 32;
        o->Scale = (float)(WorldRandom() % 4 + 8) * 0.1f;
        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        VectorRotate(p1, Matrix, o->Direction);
        o->Gravity = (float)(WorldRandom() % 5 + 2);
        if (o->Type == MODEL_STONE_COFFIN + 1 && o->SubType == 0)
        {
            o->SubType = 1;
            o->Gravity += ((float)(WorldRandom() % 5));
        }

        o->Angle[0] = (float)(WorldRandom() % 360);
        o->Angle[1] = (float)(WorldRandom() % 360);
        o->Angle[2] = (float)(WorldRandom() % 360);
        break;
    case MODEL_SHINE:
        if (o->SubType == 0)
        {
            vec3_t Pos;
            o->LifeTime = 50;
            for (int j = 0; j < 10; ++j)
            {
                Pos[0] = o->Position[0] + (j - 5) * 12.f - 30.f;
                Pos[1] = o->Position[1] + (j - 5) * 12.f + 30.f;
                Pos[2] = o->Position[2] - 300.f;

                CreateJoint(BITMAP_FLARE, Pos, o->Position, o->Angle, 16, o->Owner, 120.f);
            }
        }
        break;
    case MODEL_BLIZZARD:
        if (o->SubType == 0 || o->SubType == 2)
        {
            o->BlendMesh = -2;
            o->LifeTime = WorldRandom() % 15 + 15;
            o->Gravity = -20.f;
            o->Velocity = (float)(WorldRandom() % 360);
            Vector(0.f, 0.f, 0.f, o->Light);

            int rangeX, rangeY, rangeZ;

            rangeX = 300;
            rangeY = 150;
            rangeZ = 700;
            o->Scale = 0.5f;
            o->Gravity -= (WorldRandom() % 30 + 10);

            o->Position[0] = o->Position[0] + WorldRandom() % rangeX - rangeY;
            o->Position[1] = o->Position[1] + WorldRandom() % rangeX - rangeY;
            o->Position[2] = o->Position[2] + 600.f;
            o->Position[0] += (100.f);

            VectorCopy(o->Position, o->StartPosition);

            if (o->SubType == 2)
            {
                CreateJoint(BITMAP_JOINT_THUNDER + 1, o->Position, o->Position, o->Angle, 4, o,
                            60.f + WorldRandom() % 10);
                if (Random.FpsCheck(2, 1.0))
                    CreateJoint(BITMAP_JOINT_THUNDER + 1, o->Position, o->Position, o->Angle, 4, o,
                                60.f + WorldRandom() % 10);
            }
        }
        else if (o->SubType == 1)
        {
            o->BlendMesh = -2;
            o->LifeTime = 20;
            o->Velocity = 0.f;
        }
        break;

    case MODEL_ARROW_DRILL:
        if (o->SubType == 0 || o->SubType == 2)
        {
            o->LifeTime = 30;
            o->BlendMesh = -2;
            o->Scale = 1.f;
            o->Gravity = -10.f;
            o->Position[2] += (130.f);

            if (o->SubType != 0)
            {
                CreateEffect(MODEL_PIERCING, o->Position, o->Angle, o->Light, 0, o);
                o->AttackPoint[0] = 0;
                o->Kind = 1;
            }

            Vector(0.f, -70.f, 0.f, o->Direction);
            VectorCopy(o->Position, o->EyeLeft);
            CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 5, o, 100.f);

            o->Weapon = CharacterMachine->PacketSerial;
        }
        break;

    case MODEL_COMBO:
        o->LifeTime = 20;
        o->Gravity = 0.1f;
        o->BlendMesh = -2;
        o->BlendMeshLight = 1.f;
        Vector(0.f, 0.f, 0.f, o->Angle);
        VectorCopy(o->Position, o->StartPosition);
        o->Position[2] += (50.f);

        if (o->SubType == 0)
        {
            for (int j = 0; j < 60; ++j)
            {
                CreateJoint(BITMAP_LIGHT, o->Position, o->Position, o->Angle, 0, NULL,
                            (float)(WorldRandom() % 40 + 70));
            }
        }
        break;
    case MODEL_AIR_FORCE:
        if (o->SubType == 0)
        {
            o->LifeTime = 15;
            o->BlendMeshLight = 1.0f;
            o->Scale = 0.6f;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 20;
            o->BlendMeshLight = 1.0f;
            o->Scale = 1.2f;
        }
        break;
    case MODEL_WAVES:
        o->LifeTime = 20;
        o->Gravity = 0.1f;
        o->BlendMesh = -2;
        o->BlendMeshLight = 1.f;
        Vector(0.f, 0.f, 0.f, o->Angle);
        VectorCopy(o->Position, o->StartPosition);
        o->Position[2] += (50.f);

        if (o->SubType == 0)
        {
            for (int j = 0; j < 60; ++j)
            {
                CreateJoint(BITMAP_LIGHT, o->Position, o->Position, o->Angle, 0, NULL,
                            (float)(WorldRandom() % 40 + 70));
            }
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 15;
            o->RenderType = RENDER_NODEPTH;
            o->Position[2] += (80.f);
            o->Scale = 0.1f + WorldRandom() % 50 / 100.f;
            o->Gravity = 0.01f;

            o->Angle[0] = 90.f;
            o->Angle[2] = Angle[2];

            for (int j = 0; j < 2; ++j)
            {
                CreateJoint(BITMAP_PIERCING, o->Position, o->Position, o->Angle, 0, NULL,
                            (float)(WorldRandom() % 40 + 70));
            }

            o->Position[0] += (WorldRandom() % 100 - 50.f);
            o->Position[1] += (WorldRandom() % 100 - 50.f);
        }
        else if (o->SubType == 2 || o->SubType == 3 || o->SubType == 4)
        {
            o->LifeTime = 15;
            o->PKKey = -1;
            o->Scale = PKKey * 0.05f;
            o->Gravity = 0.01f;
            o->Position[2] -= (50.f);

            o->Angle[0] = 90.f;
            o->Angle[2] = Angle[2];
        }
        else if (o->SubType == 5 || o->SubType == 6)
        {
            o->LifeTime = 10;
            o->PKKey = -1;
            o->Scale = PKKey * 0.05f;
            o->Gravity = 0.08f;
            o->Position[2] -= (50.f);

            o->Angle[0] = -90.f;
            o->Angle[2] = Angle[2];
        }
        break;
    case MODEL_PIERCING2:
        o->Scale = 2.0f;
        if (o->SubType == 1)
            o->LifeTime = 6;
        else if (o->SubType == 2)
        {
            o->Scale = 3.0f;
            o->LifeTime = 10;
        }
        else
            o->LifeTime = 10;
        o->BlendMesh = -2;
        Vector(0.f, -60.f, 0.f, o->Direction);
        VectorCopy(o->Position, o->StartPosition);
        o->Position[2] += (130.f);
        break;
    case MODEL_DEASULER:
        if (o->SubType == 0)
        {
            const int TOTAL_LIFETIME = 55;
            vec3_t v3PosProcess01, v3PosProcessFinal, v3DirModify, v3PosModify, v3PosTargetModify;

            o->ExtState = TOTAL_LIFETIME;
            o->LifeTime = TOTAL_LIFETIME;
            o->Gravity = 0.0f;
            o->HiddenMesh = 1;
            o->Distance = 0.0f;
            o->Alpha = 1.0f;
            o->ChromeEnable = false;

            Vector(o->Angle[0], o->Angle[1], o->Angle[2], o->HeadAngle);
            VectorCopy(o->Position, o->StartPosition);
            VectorSubtract(o->Light, o->Position, o->Direction);

            o->Distance =
                sqrt(o->Direction[0] * o->Direction[0] + o->Direction[1] * o->Direction[1] +
                     o->Direction[2] * o->Direction[2]);

            VectorDivFSelf(o->Direction, o->Distance);

            float fDistanceResult = 0;
            vec3_t v3DirResult;

            vec3_t v3PosModify02;
            float fTotalDist = 1700.0f, fFirstDist = 0.0f, fRateFirstDist = 0.0f;
            VectorCopy(o->Owner->Position, v3PosModify02);
            VectorCopy(o->Position, v3PosModify);

            v3PosModify02[2] = o->Light[2];
            v3PosModify[2] = o->Light[2];
            VectorCopy(o->Light, v3PosTargetModify);
            v3PosModify[2] = v3PosModify[2] + 100.0f;
            v3PosModify02[2] = v3PosModify02[2] + 100.0f;
            v3PosTargetModify[2] = v3PosTargetModify[2] + 100.0f;
            VectorDistNormalize(v3PosModify02, v3PosTargetModify, v3DirModify);

            fDistanceResult = o->Distance * 0.3f;
            fFirstDist = fDistanceResult;
            VectorMulF(v3DirModify, fDistanceResult, v3DirResult);
            VectorAdd(v3PosModify, v3DirResult, v3PosProcess01);

            fDistanceResult = fTotalDist;
            VectorMulF(v3DirModify, fDistanceResult, v3DirResult);
            VectorAdd(v3PosModify, v3DirResult, v3PosProcessFinal);

            fRateFirstDist = fFirstDist / fTotalDist;

            o->m_Interpolates.ClearContainer();

            fRateFirstDist = fFirstDist / fTotalDist;

            CInterpolateContainer::INTERPOLATE_FACTOR InsertFactor;
            InsertFactor.fRateStart = 0.0f;         // Start
            InsertFactor.fRateEnd = fRateFirstDist; // 01 Ready
            Vector(0.0f, 0.0f, 0.0f, InsertFactor.v3Start);
            Vector(0.0f, 90.0f, 0.0f, InsertFactor.v3End);
            o->m_Interpolates.m_vecInterpolatesAngle.push_back(InsertFactor);

            InsertFactor.fRateStart = fRateFirstDist; // 02 First Final
            InsertFactor.fRateEnd = 1.01f;
            Vector(0.0f, 90.0f, 0.0f, InsertFactor.v3Start);
            Vector(0.0f, 90.0f, 2560.0f, InsertFactor.v3End);
            //Vector(90.0f, 0.0f, 1000.0f, InsertFactor.v3End);
            o->m_Interpolates.m_vecInterpolatesAngle.push_back(InsertFactor);
            InsertFactor.fRateStart = 0.0f;
            InsertFactor.fRateEnd = fRateFirstDist;
            VectorCopy(o->Position, InsertFactor.v3Start);
            VectorCopy(v3PosProcess01, InsertFactor.v3End);
            o->m_Interpolates.m_vecInterpolatesPos.push_back(InsertFactor);

            InsertFactor.fRateStart = fRateFirstDist;
            InsertFactor.fRateEnd = 1.01f;
            VectorCopy(v3PosProcess01, InsertFactor.v3Start);
            VectorCopy(v3PosProcessFinal, InsertFactor.v3End);
            o->m_Interpolates.m_vecInterpolatesPos.push_back(InsertFactor);
            o->m_Interpolates.GetAngleCurrent(o->Angle, 0.0f);
            o->m_Interpolates.GetPosCurrent(o->Position, 0.0f);
        }
        break;
    case MODEL_DEATH_SPI_SKILL:
        if (o->SubType == 0)
        {
            o->LifeTime = 30;
            o->Gravity = 1.0f;
            o->Velocity = 10.f;
            o->HiddenMesh = 1;
            o->Scale = 0.3f;
            o->BlendMesh = -1;
            o->Alpha = 0.8f;
            Vector(1.f, 1.f, 1.f, o->Light);
            Vector(0.f, -26.f, 0.f, o->Direction);
            VectorCopy(Light, o->StartPosition);
            VectorCopy(o->Angle, o->HeadAngle);
            Vector(0.f, 0.f, Angle[2], o->Angle);
        }
        else if (o->SubType == 1)
        {
            //					o->LifeTime     = 20;
            o->LifeTime = o->Owner->LifeTime;
            o->HiddenMesh = 1;
            o->Scale = 0.2f;
            o->BlendMesh = -1;
            o->Alpha = (float)((20 - o->LifeTime) / 5.f);
            Vector(0.3f, 0.4f, 1.f, o->Light);
            Vector(0.f, 0.f, 0.f, o->Direction);

            CreateParticle(BITMAP_FIRE + 2, o->Position, o->Angle, o->Light, 0);
            //                    CreateParticle ( BITMAP_FIRE+1, o->Position, o->Angle, o->Light, 0, 1.f, o );
        }
        break;
    case MODEL_PIER_PART:
        if (o->SubType == 0)
        {
            o->LifeTime = 20;
            o->Gravity = 2.f;
            o->Velocity = 10.f;
            o->HiddenMesh = 1;
            o->Scale = 1.2f;
            Vector(1.f, 1.f, 1.f, o->Light);
            Vector(0.f, -26.f, 0.f, o->Direction);
            VectorCopy(Light, o->StartPosition);
            VectorCopy(o->Angle, o->HeadAngle);
            Vector(0.f, 0.f, Angle[2], o->Angle);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = o->Owner->LifeTime;
            o->HiddenMesh = 0;
            o->Scale = 0.5f;
            o->Alpha = (float)((20 - o->LifeTime) / 5.f);
            Vector(0.f, 0.f, 0.f, o->Direction);

            CreateParticle(BITMAP_FIRE + 1, o->Position, o->Angle, o->Light, 0, 1.f, o);
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 20;
            o->Velocity = 50.f;
            o->HiddenMesh = -2;
            o->Position[2] -= (20.f);

            Vector(0.f, -40.f, 0.f, o->Direction);
            VectorCopy(o->Angle, o->HeadAngle);
            Vector(0.f, 0.f, Angle[2], o->Angle);
        }
        break;

    case BITMAP_FLARE_FORCE:
        if (o->SubType == 0)
        {
            o->LifeTime = 0;
            if (o->Owner != NULL)
            {
                CreateJoint(BITMAP_FLARE_FORCE, Position, Position, Angle, 1, o->Owner, 100.f);
                CreateJoint(BITMAP_FLARE_FORCE, Position, Position, Angle, 0, o->Owner, 250.f);
                CreateJoint(BITMAP_FLARE_FORCE, Position, Position, Angle, 2, o->Owner, 100.f);
                CreateJoint(BITMAP_FLARE_FORCE, Position, Position, Angle, 3, o->Owner, 100.f);
                CreateJoint(BITMAP_FLARE_FORCE, Position, Position, Angle, 4, o->Owner, 100.f);
            }
        }
        else if (o->SubType == 1)
        {
            CreateJoint(BITMAP_FLARE_FORCE, o->Position, o->Position, o->Angle, 5, o->Owner, 20.f,
                        PKKey, SkillIndex);
            CreateJoint(BITMAP_FLARE_FORCE, o->Position, o->Position, o->Angle, 6, o->Owner, 20.f,
                        PKKey, SkillIndex);
            CreateJoint(BITMAP_FLARE_FORCE, o->Position, o->Position, o->Angle, 7, o->Owner, 20.f,
                        PKKey, SkillIndex);
        }
        else if (o->SubType >= 2 && o->SubType <= 4)
        {
            CreateJoint(BITMAP_FLARE_FORCE, Position, Position, Angle, 8 + (o->SubType - 2),
                        o->Owner, 100.f); // 8, 9, 10
            CreateJoint(BITMAP_FLARE_FORCE, Position, Position, Angle, 0, o->Owner, 150.f);
        }
        else if (o->SubType >= 5 && o->SubType <= 7)
        {
            CreateJoint(BITMAP_FLARE_FORCE, Position, Position, Angle, 11 + (o->SubType - 5),
                        o->Owner, 100.f); // 12, 13, 14
            CreateJoint(BITMAP_FLARE_FORCE, Position, Position, Angle, 1, o->Owner, 100.f);
        }
        break;

    case MODEL_DARKLORD_SKILL: {
        o->LifeTime = 10;
        o->Scale = 0.2f;
        o->Velocity = 0.1f;

        if (o->SubType <= 1)
        {
            float angle = 45.f + (-90.f * o->SubType);

            if (Random.FpsCheck(2, 1.0))
            {
                if (o->SubType)
                {
                    angle = 45.f - 90.f;
                }
                else
                {
                    angle = 45.f;
                }
            }
            Vector(45.f, angle, 0.f, o->Angle);
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 12;
            o->Velocity = 0.4f;
        }
    }
    break;

    case MODEL_GROUND_STONE: {
        int TargetX = (int)(o->Position[0] / TERRAIN_SCALE);
        int TargetY = (int)(o->Position[1] / TERRAIN_SCALE);

        WORD wall = TerrainWall[TERRAIN_INDEX(TargetX, TargetY)];

        if ((wall & TW_NOMOVE) != TW_NOMOVE && (wall & TW_NOGROUND) != TW_NOGROUND &&
            (wall & TW_WATER) != TW_WATER)
        {
            o->LifeTime = 40;
            o->Scale = 1.2f + WorldRandom() % 30 / 100.f;
            o->Velocity = 0.3f;
            o->Angle[2] = (float)(WorldRandom() % 360);
            SetAction(o, 0);
        }
        else
        {
            RetireEffect(o);
        }
    }
    break;
    case MODEL_GROUND_STONE2: {
        int TargetX = (int)(o->Position[0] / TERRAIN_SCALE);
        int TargetY = (int)(o->Position[1] / TERRAIN_SCALE);

        WORD wall = TerrainWall[TERRAIN_INDEX(TargetX, TargetY)];

        if ((wall & TW_NOMOVE) != TW_NOMOVE && (wall & TW_NOGROUND) != TW_NOGROUND &&
            (wall & TW_WATER) != TW_WATER)
        {
            o->LifeTime = 40;
            o->Scale = 1.f + WorldRandom() % 30 / 100.f;
            o->Velocity = 0.3f;
            o->Angle[2] = (float)(WorldRandom() % 360);
            SetAction(o, 0);
        }
        else
        {
            RetireEffect(o);
        }
    }
    break;
    case BITMAP_TWLIGHT: {
        if (o->SubType == 0 || o->SubType == 1 || o->SubType == 2)
        {
            o->LifeTime = 80;
            o->Scale = 8.f;
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = 30;
            o->Alpha = 0.0f;
            o->PKKey = 0;
            VectorCopy(o->Light, o->EyeRight);
        }
    }
    break;
    case BITMAP_SHOCK_WAVE:
        if (o->SubType == 0)
        {
            o->LifeTime = 30;
            o->Scale = 20.f;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 20;
            o->Scale = (WorldRandom() % 10 + 10.f) / 10.f;
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 20;
            o->Scale = (WorldRandom() % 10 + 10.f) / 10.f;
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = 15;
            o->Scale = 1.f;
        }
        else if (o->SubType == 4)
        {
            o->LifeTime = 10;
            o->Scale = (WorldRandom() % 6 + 6.f) / 10.f;
        }
        else if (o->SubType == 5)
        {
            o->LifeTime = 20;
            o->Scale = 9.f;
        }
        else if (o->SubType == 6)
        {
            o->LifeTime = 50;
            o->Scale = 9.f;
        }
        else if (o->SubType == 7)
        {
            o->LifeTime = 10;
            o->Scale = 1.f;
        }
        else if (o->SubType == 8)
        {
            o->LifeTime = 10;
            o->Scale = (WorldRandom() % 10 + 10.f) / 5.f;
        }
        else if (o->SubType == 9)
        {
            o->LifeTime = 5;
            o->Scale = 5.f;
        }
        else if (o->SubType == 10)
        {
            o->LifeTime = 1;
            o->Scale = 2.f;

            o->Light[0] *= 2.3f;
            o->Light[1] *= 2.3f;
            o->Light[2] *= 2.3f;
        }
        else if (o->SubType == 11)
        {
            o->LifeTime = 20;
            o->Scale = (WorldRandom() % 10 + 10.f) / 10.f;
        }
        else if (o->SubType == 12)
        {
            o->LifeTime = 10;
            o->Scale = Scale;
        }
        else if (o->SubType == 13)
        {
            o->LifeTime = 40;
            o->Scale = (WorldRandom() % 10 + 20.f) / 7.f;
        }
        else if (o->SubType == 14)
        {
            o->LifeTime = 30;
            o->Alpha = 0.0f;
            o->PKKey = 0;
            VectorCopy(o->Light, o->EyeRight);
        }
        break;
    case BITMAP_DAMAGE_01_MONO:
        if (SubType == 0)
        {
            o->LifeTime = 20;
            o->Scale = Scale;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 10;
            o->Scale = 0.1f;
            o->Alpha = 1.0f;
        }
        break;
    case BITMAP_FLARE:
        if (SubType == 1 || SubType == 2)
        {
            o->LifeTime = 30;
        }
        else if (SubType == 3)
        {
            o->LifeTime = 60;
        }
        else
        {
            o->LifeTime = 30;
        }
        break;
    case BITMAP_JOINT_THUNDER + 1:
        o->LifeTime = 10;

        if (o->Owner != NULL && o->SubType == 0)
        {
            float Matrix[3][4];
            vec3_t p, p2;

            Vector(-25.f, -80.f, 0.f, p);
            AngleMatrix(o->Owner->Angle, Matrix);
            VectorRotate(p, Matrix, p2);
            VectorAdd(p2, o->Position, o->Position);
        }
        break;
    case MODEL_CUNDUN_DRAGON_HEAD: {
        o->LifeTime = 30;
        o->BlendMesh = -2;
        o->Scale = 1.0f;
        Vector(0.5, 0.5, 0.5, o->Light);
        o->Alpha = 0;
        vec3_t Position;
        VectorCopy(o->Position, Position);
        o->Position[2] += (100);

        auto fAngle = float(WorldRandom() % 360);
        auto fDistance = float(WorldRandom() % 600 + 200);
        Position[0] = o->Position[0] + sinf(fAngle) * fDistance;
        Position[1] = o->Position[1] + cosf(fAngle) * fDistance;
        Position[2] = 0;
        CreateJoint(BITMAP_JOINT_SPIRIT2, Position, Position, o->Angle, 18, o, 100.f, 0, 0);
        o->Angle[0] = -30;
        o->Angle[1] = 0;
        o->Angle[2] = (float)(WorldRandom() % 360);
    }
    break;
    case MODEL_CUNDUN_PHOENIX: {
        o->Velocity = 0.34f;
        o->LifeTime = 20;
        o->BlendMesh = -2;
        o->Scale = 0.7f;
        Vector(0.5, 0.5, 0.5, o->Light);
        o->Alpha = 0;
        vec3_t Position, Angle;
        VectorCopy(o->Position, Position);
        //o->Position[2] += 100;
        Vector(0, 0, o->Angle[2] * Q_PI / 180.0f, Angle);
        CreateJoint(BITMAP_JOINT_SPIRIT, Position, Position, Angle, 15, o, 100.f, 0, 0);
        //					o->Angle[2] -= 180;
        o->AnimationFrame = float(WorldRandom() % 5);
    }
    break;
    case MODEL_CUNDUN_SKILL:
        switch (o->SubType)
        {
        case 0:
            o->LifeTime = 30;
            break;
        case 1:
            o->LifeTime = 30;
            break;
        case 2:
            o->LifeTime = 40;
            o->PKKey = 0;
            CreateEffect(MODEL_CUNDUN_GHOST, o->Position, o->Angle, o->Light, 0, o);
            break;
        };
        break;

    case MODEL_HALLOWEEN_EX: {
        if (o->SubType == 0)
        {
            o->LifeTime = 0;
            int iEffectType;
            for (int i = 0; i < 24; ++i)
            {
                //iEffectType = WorldRandom()%6 + MODEL_HALLOWEEN_CANDY_BLUE;
                iEffectType = WorldRandom() % 8;
                switch (iEffectType)
                {
                case 0:
                    iEffectType = MODEL_HALLOWEEN_CANDY_BLUE;
                    break;
                case 1:
                    iEffectType = MODEL_HALLOWEEN_CANDY_ORANGE;
                    break;
                case 2:
                    iEffectType = MODEL_HALLOWEEN_CANDY_YELLOW;
                    break;
                case 3:
                    iEffectType = MODEL_HALLOWEEN_CANDY_RED;
                    break;
                case 4:
                    iEffectType = MODEL_HALLOWEEN_CANDY_HOBAK;
                    break;
                case 5:
                    iEffectType = MODEL_HALLOWEEN_CANDY_STAR;
                    break;
                case 6:
                    iEffectType = MODEL_HALLOWEEN_CANDY_HOBAK;
                    break;
                case 7:
                    iEffectType = MODEL_HALLOWEEN_CANDY_STAR;
                    break;
                }
                //iEffectType = MODEL_HALLOWEEN_CANDY_STAR;
                CreateEffect(iEffectType, o->Position, o->Angle, o->Light, 0);
            }
        }
    }
    break;
    case MODEL_HALLOWEEN_CANDY_BLUE:
    case MODEL_HALLOWEEN_CANDY_ORANGE:
    case MODEL_HALLOWEEN_CANDY_YELLOW:
    case MODEL_HALLOWEEN_CANDY_RED:
    case MODEL_HALLOWEEN_CANDY_HOBAK:
    case MODEL_HALLOWEEN_CANDY_STAR: {
        if (o->SubType == 0)
        {
            if (o->Type == MODEL_HALLOWEEN_CANDY_HOBAK)
                o->Scale = 2.0f + (WorldRandom() % 10 - 5) * 0.02f;
            else if (o->Type == MODEL_HALLOWEEN_CANDY_STAR)
                o->Scale = 2.0f + (WorldRandom() % 10 - 5) * 0.02f;
            else
                o->Scale = 0.6f + (WorldRandom() % 10 - 5) * 0.02f;
            o->LifeTime = WorldRandom() % 10 + 50;
            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Angle[1] = (float)(WorldRandom() % 360);
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            o->Gravity = (float)(WorldRandom() % 10 + 10);
            vec3_t p;
            Vector((float)(WorldRandom() % 60 - 30) * 0.1f, (float)(WorldRandom() % 60 - 30) * 0.1f,
                   0.f, p);
            VectorScale(p, 2.0f, p);
            VectorRotate(p, Matrix, o->Direction);
            o->m_iAnimation = WorldRandom() % 3;
        }
        else if (o->SubType == 1)
        {
            o->Scale = 2.0f + (WorldRandom() % 10 - 5) * 0.02f;
            o->LifeTime = WorldRandom() % 10 + 40;
            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Angle[1] = (float)(WorldRandom() % 360);
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            o->Gravity = (float)(WorldRandom() % 10 + 10);
            vec3_t p;
            Vector((float)(WorldRandom() % 60 - 30) * 0.1f, (float)(WorldRandom() % 60 - 30) * 0.1f,
                   -1.f, p);
            VectorScale(p, 1.5f, p);
            VectorRotate(p, Matrix, o->Direction);
            o->m_iAnimation = WorldRandom() % 3;
        }
    }
    break;

    case MODEL_XMAS_EVENT_BOX:
    case MODEL_XMAS_EVENT_CANDY:
    case MODEL_XMAS_EVENT_TREE:
    case MODEL_XMAS_EVENT_SOCKS: {
        o->Scale = 0.7f + (WorldRandom() % 10 - 5) * 0.02f;
        if (o->Type == MODEL_XMAS_EVENT_BOX)
        {
            o->Scale += (0.3f);
        }
        o->LifeTime = WorldRandom() % 10 + 50;
        o->Angle[0] = (float)(WorldRandom() % 360);
        o->Angle[1] = (float)(WorldRandom() % 360);
        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        o->Gravity = (float)(WorldRandom() % 5 + 15);
        vec3_t vPos;
        Vector((float)(WorldRandom() % 60 - 30) * 0.1f, (float)(WorldRandom() % 60 - 30) * 0.1f,
               -1.f, vPos);
        VectorScale(vPos, 1.5f, vPos);
        VectorRotate(vPos, Matrix, o->Direction);
        o->m_iAnimation = WorldRandom() % 3;
    }
    break;
    case MODEL_XMAS_EVENT_ICEHEART: {
        o->Scale = 4.0f + (WorldRandom() % 10 - 5) * 0.02f;
        o->LifeTime = 100;
    }
    break;

    case MODEL_NEWYEARSDAY_EVENT_BEKSULKI:
    case MODEL_NEWYEARSDAY_EVENT_CANDY:
    case MODEL_NEWYEARSDAY_EVENT_MONEY:
    case MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_GREEN:
    case MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_RED:
    case MODEL_NEWYEARSDAY_EVENT_YUT:
    case MODEL_NEWYEARSDAY_EVENT_PIG: {
        if (o->Type == MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_GREEN)
        {
            if (Random.FpsCheck(2, 1.0))
                o->Type = MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_RED;
        }
        else if (o->Type == MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_RED)
        {
            if (Random.FpsCheck(2, 1.0))
                o->Type = MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_GREEN;
        }

        o->LifeTime = WorldRandom() % 10 + 50;
        o->Scale = 1.6f + (WorldRandom() % 10 - 5) * 0.02f;

        if (o->Type == MODEL_NEWYEARSDAY_EVENT_BEKSULKI)
        {
            o->Scale = 2.5f + (WorldRandom() % 10 - 5) * 0.02f;
        }
        if (o->Type == MODEL_NEWYEARSDAY_EVENT_CANDY)
        {
            o->Scale = 3.0f + (WorldRandom() % 10 - 5) * 0.02f;
        }
        else if (o->Type == MODEL_NEWYEARSDAY_EVENT_PIG)
        {
            o->Scale = 1.0f + (WorldRandom() % 10 - 5) * 0.02f;
        }

        o->Angle[0] = (float)(WorldRandom() % 360);
        o->Angle[1] = (float)(WorldRandom() % 360);
        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        o->Gravity = (float)(WorldRandom() % 10 + 10);
        vec3_t p;
        Vector((float)(WorldRandom() % 10 - 5) * 0.1f, (float)(WorldRandom() % 60 - 40) * 0.1f,
               0.0f, p);
        VectorScale(p, 1.3f, p);
        VectorRotate(p, Matrix, o->Direction);
        o->m_iAnimation = WorldRandom() % 3;
    }
    break;

    case MODEL_MOONHARVEST_MOON:
        if (o->SubType == 0)
        {
            o->Alpha = 0.6f;
            o->LifeTime = GameLogic::Effects::MoonHarvestLifetime;
            VectorCopy(Position, o->Position);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 50;
            VectorCopy(o->Angle, o->Direction);
            Vector(0.f, 0.f, 0.f, o->Angle);
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 1;
            VectorCopy(Position, o->Position);
        }
        break;

    case MODEL_MOONHARVEST_GAM:
    case MODEL_MOONHARVEST_SONGPUEN1:
    case MODEL_MOONHARVEST_SONGPUEN2: {
        o->LifeTime = WorldRandom() % 10 + 50;
        if (o->Type == MODEL_MOONHARVEST_GAM)
        {
            o->Scale = 0.5f + (WorldRandom() % 10 - 5) * 0.02f;
        }
        else
        {
            o->Scale = 0.8f + (WorldRandom() % 10 - 5) * 0.02f;
        }
        o->Angle[0] = (float)(WorldRandom() % 360);
        o->Angle[1] = (float)(WorldRandom() % 360);
        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        o->Gravity = (float)(WorldRandom() % 10 + 10);
        vec3_t p;
        Vector((float)(WorldRandom() % 10 - 5) * 0.1f, (float)(WorldRandom() % 60 - 30) * 0.1f,
               0.0f, p);
        VectorScale(p, 1.2f, p);
        VectorRotate(p, Matrix, o->Direction);
    }
    break;
    case MODEL_BATTLE_GUARD2:
        if (o->SubType == 0)
        {
            o->LifeTime = 20;
            o->HiddenMesh = 3;
            o->Velocity = 0.33f;
            SetAction(o, 2);

            Vector(0.f, 0.f, 0.f, o->Angle);
        }
        break;
    case MODEL_ARROW_TANKER_HIT: {
        o->LifeTime = 100;
        o->Scale = 1.0f;

        Vector(1.f, 1.f, 1.f, o->Light);

        VectorCopy(Angle, o->m_vDeadPosition);
        VectorCopy(Position, o->StartPosition);

        if (o->SubType == 0)
        {
            Vector(40.f, 0.f, 90.f, o->Angle);
            Vector(0.0f, -80.0f, 0.0f, o->Direction);
        }
        else if (o->SubType == 1)
        {
            Vector(40.f, 0.f, -90.f, o->Angle);
            Vector(0.0f, -80.0f, 0.0f, o->Direction);
        }
        else if (o->SubType == 2)
        {
            Vector(40.f, 0.f, 0.f, o->Angle);
            Vector(0.0f, -80.0f, 0.0f, o->Direction);
        }
    }
    break;
    case MODEL_FLY_BIG_STONE1:
        if (o->SubType <= 1)
        {
            o->LifeTime = 300;
            o->Velocity = WorldRandom() % 200 / 100.f + 28.f;
            o->Gravity = 2.f;
            o->Scale = 2.f;
            o->PKKey = PKKey;
            o->Kind = Skill;
            o->Skill = SkillIndex;

            switch ((int)o->PKKey)
            {
            case 1:
                o->Velocity = 29.f;
                VectorCopy(Angle, o->m_vDeadPosition);
                break;

            default:
                o->m_vDeadPosition[0] =
                    Hero->Object.Position[0] + (WorldRandom() % 16 - 8) * TERRAIN_SCALE;
                if (Random.FpsCheck(5, 1.0))
                    o->m_vDeadPosition[1] = 114 * TERRAIN_SCALE;
                else
                    o->m_vDeadPosition[1] = (WorldRandom() % 33 + 98) * TERRAIN_SCALE;
                o->m_vDeadPosition[2] = 100.f;
                break;
            }
            Vector(0.f, 0.f, 0.f, o->Angle);
            Vector(1.f, 1.f, 1.f, o->Light);
            VectorCopy(Position, o->StartPosition);

            VectorSubtract(o->m_vDeadPosition, o->StartPosition, o->Direction);
            VectorScale(o->Direction, 0.001f, o->Direction);
            o->Direction[2] = 5.f;
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 50;
            o->Scale = WorldRandom() % 8 / 20.f + 0.7f;
            o->Gravity = 10.f;
            o->Angle[0] = (float)(WorldRandom() % 360);

            Vector(0.f, 0.f, 0.f, o->Direction);
            Vector(0.f, 0.f, (float)(WorldRandom() % 360), o->HeadAngle);
        }
        break;

    case MODEL_FLY_BIG_STONE2:
        if (o->SubType <= 1)
        {
            o->LifeTime = 300;
            o->Velocity = WorldRandom() % 200 / 100.f + 28.f;
            o->Gravity = 2.f;
            o->Scale = 2.f;
            o->PKKey = PKKey;
            o->Kind = Skill;
            o->Skill = SkillIndex;

            switch ((int)o->PKKey)
            {
            case 1:
                o->Velocity = 29.f;
                VectorCopy(Angle, o->m_vDeadPosition);
                break;

            default:
                o->m_vDeadPosition[0] =
                    Hero->Object.Position[0] + (WorldRandom() % 16 - 8) * TERRAIN_SCALE;
                o->m_vDeadPosition[1] = (WorldRandom() % 30 + 80) * TERRAIN_SCALE;
                o->m_vDeadPosition[2] = 100.f;
                break;
            }
            Vector(0.f, 0.f, 0.f, o->Angle);
            Vector(1.f, 1.f, 1.f, o->Light);
            VectorCopy(Position, o->StartPosition);

            VectorSubtract(o->m_vDeadPosition, o->StartPosition, o->Direction);
            VectorScale(o->Direction, 0.001f, o->Direction);
            o->Direction[2] = 5.f;
        }
        break;

    case MODEL_BIG_STONE_PART1:
    case MODEL_BIG_STONE_PART2:
    case MODEL_WALL_PART1:
    case MODEL_WALL_PART2:
    case MODEL_GOLEM_STONE:
        if (o->Type >= MODEL_WALL_PART1 && o->Type <= MODEL_WALL_PART2)
        {
            Vector(0.f, (float)(WorldRandom() % 256 + 128) * 0.1f, 0.f, p1);
            o->Scale = (float)(WorldRandom() % 2 + 7) * 0.1f;
            o->Gravity = (float)(WorldRandom() % 5 + 6);
        }
        else if (o->Type == MODEL_GOLEM_STONE)
        {
            Vector(0.f, (float)(WorldRandom() % 128 + 32) * 0.1f, 0.f, p1);
            o->Scale = (float)(WorldRandom() % 2 + 10) * 0.25f;
            o->Gravity = (float)(WorldRandom() % 5 + 2);
        }
        else if ((o->Type == MODEL_BIG_STONE_PART1 || o->Type == MODEL_BIG_STONE_PART2) &&
                 o->SubType == 2)
        {
            Vector(0.f, (float)(WorldRandom() % 128 + 32) * 0.1f, 0.f, p1);
            o->Scale = 0.6F + (float)(WorldRandom() % 2 + 4) * 0.12f;
            o->Gravity = (float)(WorldRandom() % 5 + 2);
        }
        else if (o->Type == MODEL_BIG_STONE_PART2 && o->SubType == 3)
        {
            o->LifeTime = WorldRandom() % 16 + 32;
            o->Scale = 1.2f + (float)(WorldRandom() % 3 + 2) * 0.12f;
            o->Direction[2] = -(WorldRandom() % 5 + 20);
            o->Velocity = 1.8f;
            break;
        }
        else
        {
            Vector(0.f, (float)(WorldRandom() % 128 + 128) * 0.1f, 0.f, p1);
            o->Scale = (float)(WorldRandom() % 7 + 10) * 0.1f;
            o->Gravity = (float)(WorldRandom() % 5 + 2);
        }
        o->LifeTime = WorldRandom() % 16 + 32;
        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        VectorRotate(p1, Matrix, o->Direction);

        o->Angle[0] = (float)(WorldRandom() % 360);
        o->Angle[1] = (float)(WorldRandom() % 360);
        o->Angle[2] = (float)(WorldRandom() % 360);

        o->Position[2] += (50.f);
        break;

    case MODEL_GATE_PART1:
    case MODEL_GATE_PART2:
    case MODEL_GATE_PART3:
        Vector(0.f, (float)(WorldRandom() % 128 + 64) * 0.1f, 0.f, p1);
        o->Position[2] += (50.f);
        o->LifeTime = WorldRandom() % 16 + 32;
        o->Scale = (float)(WorldRandom() % 2 + 7) * 0.1f;
        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        VectorRotate(p1, Matrix, o->Direction);
        o->Gravity = (float)(WorldRandom() % 5 + 2);
        if (o->Type == MODEL_GATE_PART1 && o->SubType == 0)
        {
            o->SubType = 1;
            o->Gravity += ((float)(WorldRandom() % 5));
        }
        else if (o->SubType == 2)
        {
            o->Scale *= 0.6f;
        }

        o->Angle[0] = (float)(WorldRandom() % 360);
        o->Angle[1] = (float)(WorldRandom() % 360);
        o->Angle[2] = (float)(WorldRandom() % 360);
        break;

    case MODEL_AURORA:
        o->LifeTime = 100;
        o->Scale = PKKey / 100.f;
        o->BlendMesh = 0;
        o->BlendMeshLight = 0.f;
        break;

    case MODEL_TOWER_GATE_PLANE:
        o->LifeTime = 100;
        o->BlendMeshLight = 0.3f;
        VectorCopy(o->Position, o->StartPosition);
        break;

    case BITMAP_CRATER:
        if (o->SubType == 0)
        {
            o->LifeTime = 60;
            o->StartPosition[0] = 4.5;
            o->StartPosition[1] = 4.5;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 30;
            o->StartPosition[0] = 2.5;
            o->StartPosition[1] = 2.5;
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 40;
            o->StartPosition[0] = 3.0;
            o->StartPosition[1] = 3.0;
        }

        Vector(1.f, 1.f, 1.f, o->Light);
        break;

    case BITMAP_CHROME_ENERGY2:
        if (o->SubType == 0)
        {
            o->LifeTime = 30;
            o->StartPosition[0] = 1.5;
            o->StartPosition[1] = 1.5;
        }
        break;

    case MODEL_STUN_STONE:
        if (o->SubType == 0)
        {
            Vector(WorldRandom() % 256 / 64.f - 2.f, -(float)(WorldRandom() % 200 + 64) * 0.1f, 0.f,
                   p1);

            o->LifeTime = 40;
            o->Scale = 1.1f + WorldRandom() % 100 / 100.f;
            o->Gravity = -(float)(WorldRandom() % 16 + 10);

            o->ExtState = 0;
            o->Position[2] += (600.f);

            AngleMatrix(Angle, Matrix);
            VectorRotate(p1, Matrix, o->StartPosition);

            Vector(1.f, 1.f, 1.f, o->Light);
            Vector(-20.f, 0.f, WorldRandom() % 360, o->Angle);
            Vector(0.f, 0.f, Angle[2], o->HeadAngle);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 22;
            o->HiddenMesh = -2;

            Vector(0.f, -25.f, 0.f, o->Direction);
            Vector(-1.f, -1.f, -1.f, o->Light);
        }
        break;

    case MODEL_SKIN_SHELL:
        Vector(0.f, (float)(WorldRandom() % 128 + 32) * 0.1f, 0.f, p1);
        o->Position[2] += (50.f);
        o->LifeTime = WorldRandom() % 16 + 32;
        o->Scale = (float)(WorldRandom() % 3 + 3) * 0.1f;
        o->Alpha = 0.5f;
        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        VectorRotate(p1, Matrix, o->Direction);
        o->Gravity = (float)(WorldRandom() % 5 + 2);
        o->Gravity += ((float)(WorldRandom() % 5));

        o->Angle[0] = (float)(WorldRandom() % 360);
        o->Angle[1] = (float)(WorldRandom() % 360);
        o->Angle[2] = (float)(WorldRandom() % 360);

        if (o->SubType == 0)
        {
            Vector(0.5f, 0.5f, 0.5f, o->Light);
        }
        else if (o->SubType == 1)
        {
            Vector(0.1f, 0.6f, 1.f, o->Light);
        }
        break;

    case MODEL_MANA_RUNE:
        if (o->SubType == 0)
        {
            o->LifeTime = 50;
            o->Scale = 0.f;
            o->Gravity = 0.1f;
            o->Alpha = 0.3f;
            o->HiddenMesh = 0;

            o->Position[2] += 300.f;

            Vector(0.f, 0.f, 45.f, o->Angle);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 10;
            o->Scale = 1.1f;
            o->Gravity = 0.1f;
            o->HiddenMesh = -1;
            o->BlendMesh = -2;
            o->BlendMeshLight = 0.4f;
        }
        break;
    case MODEL_SKILL_JAVELIN: {
        o->LifeTime = 35;
        o->Gravity = 2.f;
        o->Velocity = 10.f;
        o->Scale = 1.2f;

        Vector(0.f, -5.f, 0.f, o->Direction);
        VectorCopy(o->Angle, o->HeadAngle);
        VectorCopy(o->Owner->Position, o->StartPosition);
        o->Position[2] += (150.f);

        float Ang = WorldRandom() % 80 + 10;

        o->HeadAngle[2] += (o->SubType * Ang - Ang);
    }
    break;
    case MODEL_FENRIR_THUNDER: {
        if (o->SubType == 0)
        {
            o->LifeTime = 100;
            o->Scale = 0.3f + (float)(WorldRandom() % 100) * 0.002f;
            o->m_iAnimation = 0;
            o->Alpha = 0.7f;

            o->Angle[0] = WorldRandom() % 360;
            o->Angle[1] = WorldRandom() % 360;
            o->Angle[2] = WorldRandom() % 360;

            VectorCopy(Light, o->Light);

            vec3_t vPos;
            Vector(0.0f, 0.0f, 0.0f, vPos);
            BMD *p_b = &Models[o->Owner->Type];
            int irandom = WorldRandom() % 30;
            int sourceBone = -1;

            if (irandom >= 1 && irandom <= 2)
            {
                sourceBone = 10;
            }
            else if (irandom == 3)
            {
                o->Scale -= (0.2f);
                sourceBone = 14;
            }
            else if (irandom >= 4 && irandom <= 5)
            {
                sourceBone = 2;
            }
            else if (irandom >= 6 && irandom <= 7)
            {
                o->Scale -= (0.2f);
                if (Random.FpsCheck(2, 1.0))
                    sourceBone = 50;
                else
                    sourceBone = 51;
            }
            else if (irandom == 8)
            {
                o->Scale -= (0.2f);
                sourceBone = 53;
            }
            else if (irandom >= 9 && irandom <= 10)
            {
                RetireEffect(o);
                o->Alpha = 0.0f;
            }
            else
            {
                o->Scale += (0.1f);
                o->Position[0] += (WorldRandom() % 240 - 120);
                o->Position[1] += (WorldRandom() % 10 - 5);
                o->Position[2] += (110);
            }

            if (sourceBone >= 0)
            {
                const float fraction = o->BirthTiming.remainingFrames >= 0.f
                                           ? o->BirthTiming.pendingStartFraction
                                           : 1.f;
                AnimationPoseSample pose(o->Owner, p_b->BoneHead, p_b->BodyHeight, false,
                                         p_b->PoseAssetIdentity());
                pose.SampleBonePosition(*p_b, *o->Owner, sourceBone, vPos, WorldTime, fraction,
                                        o->Position);
            }

            vec3_t vLight;
            vLight[0] = o->Light[0] - 0.3f;
            vLight[1] = o->Light[1] - 0.3f;
            vLight[2] = o->Light[2] - 0.3f;
            CreateSprite(BITMAP_LIGHT, o->Position, 2.0f, vLight, o);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 100;
            o->Scale = 0.3f + (float)(WorldRandom() % 100) * 0.002f;
            o->Alpha = 1.0f;
            o->m_iAnimation = 0;

            o->Angle[0] = WorldRandom() % 360;
            o->Angle[1] = WorldRandom() % 360;
            o->Angle[2] = WorldRandom() % 360;

            VectorCopy(Light, o->Light);

            o->Position[0] += (WorldRandom() % 40 - 20);
            o->Position[1] += (WorldRandom() % 40 - 20);
            o->Position[2] += (WorldRandom() % 40 - 20);

            vec3_t vLight;
            vLight[0] = o->Light[0] - 0.3f;
            vLight[1] = o->Light[1] - 0.3f;
            vLight[2] = o->Light[2] - 0.3f;
            CreateSprite(BITMAP_LIGHT, o->Position, 2.0f, vLight, o);
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 4;
            o->Scale = 0.1f + (float)(WorldRandom() % 100) * 0.002f;
            o->Alpha = 1.0f;
            o->m_iAnimation = 0;

            o->Angle[0] = WorldRandom() % 360;
            o->Angle[1] = WorldRandom() % 360;
            o->Angle[2] = WorldRandom() % 360;

            VectorCopy(Light, o->Light);

            o->Position[0] += (WorldRandom() % 40 - 20);
            o->Position[1] += (WorldRandom() % 40 - 20);
            o->Position[2] += (WorldRandom() % 40 - 20);

            vec3_t vLight;
            vLight[0] = o->Light[0] - 0.3f;
            vLight[1] = o->Light[1] - 0.3f;
            vLight[2] = o->Light[2] - 0.3f;
            CreateSprite(BITMAP_LIGHT, o->Position, 2.0f, vLight, o);
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = 4;
            o->Scale = 0.5f + (float)(WorldRandom() % 100) * 0.002f;
            o->Alpha = 1.0f;
            o->m_iAnimation = 0;

            o->Angle[0] = WorldRandom() % 360;
            o->Angle[1] = WorldRandom() % 360;
            o->Angle[2] = WorldRandom() % 360;

            VectorCopy(Light, o->Light);

            o->Position[0] += (WorldRandom() % 160 - 80);
            o->Position[1] += (WorldRandom() % 160 - 80);
            o->Position[2] += (WorldRandom() % 160 - 80);

            vec3_t vLight;
            vLight[0] = o->Light[0] - 0.3f;
            vLight[1] = o->Light[1] - 0.3f;
            vLight[2] = o->Light[2] - 0.3f;
            CreateSprite(BITMAP_LIGHT, o->Position, 2.0f, vLight, o);
        }
    }
    break;
    case MODEL_FALL_STONE_EFFECT: {
        if (o->SubType == 0 || o->SubType == 1)
        {
            o->LifeTime = WorldRandom() % 5 + 100;

            if (o->SubType == 0)
                o->Scale = (float)(WorldRandom() % 20 + 5) * 0.005f + o->Scale * 0.05f;
            else
                o->Scale = (float)(WorldRandom() % 10 + 5) * 0.02f + o->Scale * 0.05f;

            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Angle[1] = (float)(WorldRandom() % 360);
            o->Angle[2] = (float)(WorldRandom() % 360);

            o->Position[0] += ((float)(WorldRandom() % 100 - 50));
            o->Position[1] += ((float)(WorldRandom() % 100 - 50));

            o->Gravity = (float)(WorldRandom() % 10 + 10) * 0.5f;
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 100;
            o->Scale = o->Scale + (float)(WorldRandom() % 20 + 5) * 0.05f;

            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Angle[1] = (float)(WorldRandom() % 360);
            o->Angle[2] = (float)(WorldRandom() % 360);

            o->Gravity = (float)(WorldRandom() % 20 + 40) * 0.5f + o->Scale * 2.f;
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = WorldRandom() % 5 + 15;
            o->Scale = Scale;
            o->Gravity = (float)(WorldRandom() % 3 + 3);

            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            Vector(0.f, (float)(WorldRandom() % 60 + 30) * 0.1f, 0.f, p1);
            VectorRotate(p1, Matrix, o->HeadAngle);
            o->HeadAngle[2] += (25.0f);

            Vector(0, 0, 0, o->Direction);
        }
    }
    break;
    case MODEL_FENRIR_FOOT_THUNDER:
        o->LifeTime = 200;
        o->m_iAnimation = 0;
        VectorCopy(Position, o->Position);
        VectorCopy(Light, o->Light);
        o->Alpha = 1.0f;
        o->m_dwTime = timeGetTime();
        o->Position[2] = 0.0f;
        break;
    case MODEL_TWINTAIL_EFFECT: {
        if (o->SubType == 0)
        {
            o->LifeTime = 200;
            VectorCopy(Position, o->Position);
            VectorCopy(Light, o->Light);
            o->Alpha = 1.0f;
            o->Position[2] = 0.0f;
            o->m_dwTime = timeGetTime();
            o->m_iAnimation = 0;
        }
        else if (o->SubType == 1 || o->SubType == 2)
        {
            o->LifeTime = 50;
            o->Scale = 3.5f;
            VectorCopy(Position, o->Position);
            VectorCopy(Light, o->Light);
            o->Alpha = 1.0f;
            o->Position[2] = 0.0f;
            o->Angle[0] = 0.0f;
        }
    }
    break;
    case MODEL_FENRIR_SKILL_DAMAGE: {
        switch (o->SubType)
        {
        case 1:
            Vector(0.6f, 0.2f, 0.2f, o->Light);
            break;
        case 2:
            Vector(0.2f, 0.2f, 0.4f, o->Light);
            break;
        case 3:
            Vector(0.6f, 0.8f, 0.6f, o->Light);
            break;
        }

        vec3_t vPos;
        int irandom;

        BMD *p_b = &Models[o->Owner->Type];
        for (int i = 0; i < 10; i++)
        {
            irandom = WorldRandom() % p_b->NumBones;
            Vector(0.0f, 0.0f, 0.0f, vPos);
            p_b->TransformPosition(o->Owner->BoneTransform[irandom], vPos, o->Position, true);
            CreateParticle(BITMAP_ENERGY, o->Position, o->Angle, o->Light, 2);
        }
    }
    break;
    case MODEL_ARROW_IMPACT:
        o->Velocity = 1.f;
        o->LifeTime = 20;
        o->Scale = 1.8f;
        o->BlendMesh = -2;
        o->Direction[1] = -30.f;

        Vector(0.3f, 0.8f, 1.f, o->Light);

        AngleMatrix(o->Angle, Matrix);
        Vector(-10.f, -80.f, 200.f, p1);
        VectorRotate(p1, Matrix, p2);
        VectorAdd(o->Position, p2, o->Position);

        o->Angle[0] = -30.f;
        CreateJoint(BITMAP_FLASH, o->Position, o->Position, o->Angle, 4, o, 40.f, 50);

        o->Weapon = CharacterMachine->PacketSerial;
        break;
    case BITMAP_JOINT_FORCE:
        if (o->SubType == 0)
        {
            o->LifeTime = 20;
            Vector(0.f, -550.f, 0.f, o->Direction);
            Vector(-90.f, 0.f, Angle[2], o->Angle);
            Vector(0.f, 0.f, WorldRandom() % 360, o->HeadAngle);
            CreateJoint(BITMAP_JOINT_FORCE, o->Position, o->Position, o->Angle, 3, NULL, 80.f);
            VectorCopy(Position, o->StartPosition);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 20;
            Vector(0.f, -250.f, 0.f, o->Direction);
            Vector(-90.f, 0.f, Angle[2], o->Angle);
            Vector(0.f, 0.f, WorldRandom() % 360, o->HeadAngle);
            VectorCopy(Position, o->StartPosition);
        }
        break;
    case MODEL_SWORD_FORCE:
        o->Velocity = 0.25f;
        if (o->SubType == 0 || o->SubType == 2)
        {
            o->LifeTime = 15;
            o->Scale = 0.f;

            o->Position[2] += (100.f);
            Vector(0.f, -10.f, 0.f, o->Direction);
        }
        else if (o->SubType == 1 || o->SubType == 3)
        {
            o->LifeTime = 5;
            o->Scale = 3.5f;
        }
        o->BlendMesh = 0;
        o->BlendMeshLight = 1.0f;
        break;
    case MODEL_PROTECTGUILD: {
        o->Alpha = 0;
        o->Angle[2] = +45.0f;

        BMD *b = &Models[o->Owner->Type];
        vec3_t tempPosition, p;
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(o->Owner->BoneTransform[20], p, tempPosition, true);
        o->Position[0] = tempPosition[0];
        o->Position[1] = tempPosition[1];
        o->Position[2] = o->Owner->Position[2] + tempPosition[2] - o->Owner->Position[2] + 60;

        o->LifeTime = 130;
        o->Scale = 3.2f;
        Vector(1.0f, 1.0f, 1.0f, o->Light);
        VectorCopy(o->Position, o->StartPosition);
        for (int i = 0; i < 10; ++i)
        {
            float fAngle = WorldRandom() % 360;
            Vector(o->Position[0] + (WorldRandom() % 20 + 15) * sinf(fAngle),
                   o->Position[1] + (WorldRandom() % 20 + 15) * cosf(fAngle), o->Position[2], p);
            CreateParticle(BITMAP_SPARK + 1, p, o->Angle, o->Light, 4, 0.6f, o);
        }
    }
    break;
    case MODEL_MOVE_TARGETPOSITION_EFFECT: {
        if (o->SubType == 0)
        {
            o->LifeTime = 30;
            o->BlendMesh = 0;

            vec3_t vLight, vPos;
            Vector(1.0f, 0.7f, 0.3f, vLight);
            VectorCopy(o->Position, vPos);
            vPos[2] += 85.f;

            DeleteEffect(BITMAP_MAGIC, NULL, 11);
            DeleteEffect(BITMAP_TARGET_POSITION_EFFECT2, NULL, 0);
            DeleteEffect(BITMAP_TARGET_POSITION_EFFECT1, NULL, 0);
            DeleteJoint(MODEL_SPEARSKILL, NULL, 16);

            CreateEffect(BITMAP_MAGIC, o->Position, o->Angle, vLight, 11);
            CreateEffect(BITMAP_TARGET_POSITION_EFFECT2, o->Position, o->Angle, vLight, 0);
            CreateJoint(MODEL_SPEARSKILL, vPos, vPos, Angle, 16, o, 5.0f);
            CreateJoint(MODEL_SPEARSKILL, vPos, vPos, Angle, 16, o, 5.0f);
            CreateJoint(MODEL_SPEARSKILL, vPos, vPos, Angle, 16, o, 5.0f);
            CreateJoint(MODEL_SPEARSKILL, vPos, vPos, Angle, 16, o, 5.0f);
        }
    }
    break;

    case BITMAP_TARGET_POSITION_EFFECT1: {
        if (o->SubType == 0)
        {
            o->LifeTime = 20;
            o->Scale = 1.2f;
        }
    }
    break;
    case BITMAP_TARGET_POSITION_EFFECT2: {
        if (o->SubType == 0)
        {
            o->LifeTime = 30;
            o->Scale = 1.8f;
        }
    }
    break;
    case MODEL_EFFECT_SAPITRES_ATTACK: {
        if (o->SubType == 0)
        {
            o->LifeTime = 20;

            for (int i = 0; i < 10; i++)
            {
                CreateEffect(MODEL_EFFECT_SAPITRES_ATTACK_2, o->Position, o->Angle, o->Light, 14);
            }
        }
    }
    break;
    case MODEL_EFFECT_SAPITRES_ATTACK_1: {
        if (o->SubType == 0)
        {
            o->BlendMesh = 0;
            o->BlendMeshLight = 1.0f;
            o->Position[2] += (100.f);
            o->LifeTime = 17;
            o->Scale = 1.1f;
            VectorSubtract(o->Owner->Position, o->Position, o->Direction);
            VectorNormalize(o->Direction);
            o->Angle[2] = CreateAngle2D(o->Position, o->Owner->Position);
        }
    }
    break;
    case MODEL_EFFECT_THUNDER_NAPIN_ATTACK_1: {
        if (o->SubType == 0)
        {
            o->LifeTime = 30;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 30;
        }
    }
    break;

    case MODEL_EFFECT_SKURA_ITEM: {
        if ((o->SubType == 0) || (o->SubType == 1))
        {
            o->LifeTime = 52;
        }
    }
    break;

    case MODEL_EFFECT_TRACE: {
        if (o->SubType == 0)
        {
            o->LifeTime = 50;
            VectorCopy(o->Position, o->EyeLeft);
            CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 17, o, o->Scale,
                        -1, 0, 0, -1, o->Light);
        }
    }
    break;
    case MODEL_STAR_SHINE: {
        switch (o->SubType)
        {
        case 0: {
            o->LifeTime = 30;
            o->Alpha = 0.2f;
            o->Angle[0] = (float)(WorldRandom() % 360);
        }
        break;
        }
    }
    break;
    case MODEL_FEATHER: {
        switch (o->SubType)
        {
        case 0:
        case 1:
        case 2:
        case 3: {
            vec3_t vOriginPos;
            VectorCopy(o->Position, vOriginPos);

            o->Position[0] += ((float)(WorldRandom() % 20 - 10) * 4.f);
            o->Position[1] += ((float)(WorldRandom() % 20 - 10) * 4.f);
            o->Position[2] += ((float)(WorldRandom() % 20 - 10) * 4.f);

            VectorSubtract(vOriginPos, o->Position, o->Direction);
            VectorNormalize(o->Direction);
            int iAddDirection = ((float)(WorldRandom() % 10 - 5) * 0.08f);
            o->Direction[0] += (iAddDirection);
            o->Direction[1] += (iAddDirection);
            o->Direction[2] += (iAddDirection);

            o->Scale = o->Scale + ((float)(WorldRandom() % 20 - 10) * (o->Scale * 0.03f));
            o->LifeTime = 30 + (WorldRandom() % 20 - 10);
            if (o->SubType == 2 || o->SubType == 3)
                o->LifeTime = 100;
            o->Alpha = 0.6f + ((float)(WorldRandom() % 10) * 0.02f);

            o->Gravity = 0.1f;

            if (o->SubType == 2 || o->SubType == 3)
                o->Gravity = 0.f;

            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Angle[1] = (float)(WorldRandom() % 360);
            o->Angle[2] = (float)(WorldRandom() % 360);

            o->EyeRight[0] = (float)(WorldRandom() % 10 - 5);
            o->EyeRight[1] = (float)(WorldRandom() % 10 - 5);
            o->EyeRight[2] = (float)(WorldRandom() % 10 - 5);
        }
        break;
        }
    }
    break;
    case MODEL_FEATHER_FOREIGN: {
        switch (o->SubType)
        {
        case 4: {
            vec3_t vOriginPos;
            VectorCopy(o->Position, vOriginPos);

            o->Position[0] += ((float)(WorldRandom() % 20 - 10) * 4.f);
            o->Position[1] += ((float)(WorldRandom() % 20 - 10) * 4.f);
            o->Position[2] += ((float)(WorldRandom() % 20 - 10) * 4.f);

            VectorSubtract(vOriginPos, o->Position, o->Direction);
            VectorNormalize(o->Direction);
            int iAddDirection = ((float)(WorldRandom() % 10 - 5) * 0.08f);
            o->Direction[0] += (iAddDirection);
            o->Direction[1] += (iAddDirection);
            o->Direction[2] += (iAddDirection);

            o->Scale = o->Scale + ((float)(WorldRandom() % 20 - 10) * (o->Scale * 0.03f));
            o->LifeTime = 25;
            o->Alpha = 0.3f;

            o->Gravity = 0.1f;
            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Angle[1] = (float)(WorldRandom() % 360);
            o->Angle[2] = (float)(WorldRandom() % 360);

            o->EyeRight[0] = (float)(WorldRandom() % 10 - 5);
            o->EyeRight[1] = (float)(WorldRandom() % 10 - 5);
            o->EyeRight[2] = (float)(WorldRandom() % 10 - 5);
        }
        break;
        }
    }
    break;
    case MODEL_BLOW_OF_DESTRUCTION: {
        if (o->SubType == 0)
        {
            o->LifeTime = 40;
            vec3_t vPos, vPos2;
            float Matrix[3][4];
            Vector(-20.f, -100.f, 0.f, vPos);
            AngleMatrix(o->Owner->Angle, Matrix);
            VectorRotate(vPos, Matrix, vPos2);
            VectorAdd(vPos2, o->Position, o->Position);
            VectorCopy(o->Light, o->StartPosition);
            Vector(1.2f, 1.2f, 1.2f, o->Light);
            CreateEffect(MODEL_BLOW_OF_DESTRUCTION, o->StartPosition, o->Angle, o->Light, 1,
                         o->Owner);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 40;
            Vector(1.2f, 1.2f, 1.2f, o->Light);
            o->Position[2] = 150.f;
            o->Scale = 5.f;
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 50;
            vec3_t vPos, vPos2;
            float Matrix[3][4];
            Vector(0.f, 0.f, 0.f, vPos);
            AngleMatrix(o->Owner->Angle, Matrix);
            VectorRotate(vPos, Matrix, vPos2);
            VectorAdd(vPos2, o->Position, o->Position);
            if (o->Owner->m_sTargetIndex < 0)
                break;
            VectorCopy(CharactersClient[o->Owner->m_sTargetIndex].Object.Position,
                       o->StartPosition);
            o->Scale = 0.5f;
        }
    }
    break;
    case MODEL_NIGHTWATER_01: {
        o->LifeTime = 25;
        o->Alpha = 1.f;
        o->Angle[2] = WorldRandom() % 360;
    }
    break;
    case MODEL_KNIGHT_PLANCRACK_A:
        if (o->SubType == 0)
        {
            o->LifeTime = 25;
            o->Alpha = 1.f;
            o->Angle[2] = WorldRandom() % 360;
            o->Scale = Scale + WorldRandom() % 10 * 0.05f;
            o->Position[2] += (10.f);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 20;
            o->Alpha = 1.f;
            o->Angle[2] = WorldRandom() % 360;
            o->Scale = Scale + WorldRandom() % 10 * 0.05f;
            o->Position[2] += (10.f);
        }
        break;
    case MODEL_KNIGHT_PLANCRACK_B: {
        o->LifeTime = 25;
        o->Alpha = 1.f;
        o->Position[2] += (15.f);
        o->Angle[2] += (90.f);
    }
    break;
    case MODEL_EFFECT_FLAME_STRIKE:
        if (o->SubType == 0)
        {
            o->Alpha = 0;
            o->LifeTime = 35;
            o->Velocity = Models[o->Owner->Type].Actions[PLAYER_SKILL_FLAMESTRIKE].PlaySpeed;
            o->AI = 0;
            o->m_iAnimation = WorldRandom();
        }
        break;
    case MODEL_STREAMOFICEBREATH: {
        const float LENSCALAR = 50.0f;
        const float RANDOMOFFSET_ANGLE = 5.0f;
        const float RANDOMOFFSET_SCALE = 0.01f;
        const float RANDOMOFFSET_POSITION = 20.0f;
        const float RANDNUM = ((float)((WorldRandom() % 2000) - 1000) * 0.001f);

        // 1. Set Value
        o->LifeTime = 17;
        o->Light[0] = 0.6f;
        o->Light[1] = 0.6f;
        o->Light[2] = 0.6f;

        // 2. Calculate Position
        vec3_t v3Len_, v3LenBasis_, v3PosBasis_;
        float matRotation[3][4];

        Vector(0.0f, -1.0f, 0.0f, v3LenBasis_);
        AngleMatrix(o->Angle, matRotation);
        VectorRotate(v3LenBasis_, matRotation, v3Len_);

        v3PosBasis_[0] = o->Position[0] + (v3Len_[0] * LENSCALAR);
        v3PosBasis_[1] = o->Position[1] + (v3Len_[1] * LENSCALAR);
        v3PosBasis_[2] = o->Position[2] + (v3Len_[2] * LENSCALAR);

        VectorCopy(v3PosBasis_, o->StartPosition);
        // 3. Reality Position, Angle, Scaling
        for (INT i_ = 0; i_ < 4; ++i_)
        {
            vec3_t v3ResultAngle, v3ResultPos;
            float fResultScale;
            float fCurrentOffsetScale, fCurrentOffsetPos, fCurrentOffsetAngle;
            fCurrentOffsetScale = RANDOMOFFSET_SCALE * RANDNUM;
            fCurrentOffsetPos = RANDOMOFFSET_POSITION * RANDNUM;
            fCurrentOffsetAngle = RANDOMOFFSET_ANGLE * RANDNUM;

            v3ResultPos[0] = v3PosBasis_[0] + fCurrentOffsetPos;
            v3ResultPos[1] = v3PosBasis_[1] + fCurrentOffsetPos;
            v3ResultPos[2] = v3PosBasis_[2] + fCurrentOffsetPos;

            v3ResultAngle[0] = o->Angle[0] + fCurrentOffsetAngle;
            v3ResultAngle[1] = o->Angle[1] + fCurrentOffsetAngle;
            v3ResultAngle[2] = o->Angle[2] + fCurrentOffsetAngle;

            fResultScale = o->Scale + fCurrentOffsetScale;

            CreateParticle(BITMAP_RAKLION_CLOUDS, v3ResultPos, v3ResultAngle, o->Light, 0,
                           fResultScale);
        }
    }
    break;
    case MODEL_1_STREAMBREATHFIRE: {
        o->LifeTime = 30;
    }
    break;
    case MODEL_LAVAGIANT_FOOTPRINT_R:
    case MODEL_LAVAGIANT_FOOTPRINT_V: {
        o->LifeTime = 200;
        VectorCopy(Position, o->Position);
        VectorCopy(Light, o->Light);
        o->Alpha = 1.0f;
        o->Position[2] = 0.0f;
        o->m_dwTime = timeGetTime();
        o->Scale = o->Scale;
    }
    break;
    case MODEL_EFFECT_FIRE_HIK3_MONO: {
        int iRandNum = (WorldRandom() % 100);

        if (iRandNum > 20)
        {
            CreateParticle(BITMAP_FIRE_HIK3_MONO, o->Position, o->Angle, o->Light, 1, o->Scale);
        }
    }
    break;
    case MODEL_PROJECTILE: {
        o->LifeTime = 50;
        o->Gravity = 0.0f;
        o->Velocity = 70.0f;
        VectorCopy(Position, o->StartPosition);
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
    case MODEL_DOOR_CRUSH_EFFECT_PIECE11:
    case MODEL_DOOR_CRUSH_EFFECT_PIECE12:
    case MODEL_DOOR_CRUSH_EFFECT_PIECE13:
    case MODEL_STATUE_CRUSH_EFFECT_PIECE01:
    case MODEL_STATUE_CRUSH_EFFECT_PIECE02:
    case MODEL_STATUE_CRUSH_EFFECT_PIECE03: {
        o->LifeTime = 30 + (WorldRandom() % 30);
        o->Velocity = 0.f;
        o->PKKey = -1;
        o->Owner = Owner;
        o->Gravity = 2.3f;

        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        Vector(0.f, (float)(WorldRandom() % 64 + 48) * 0.1f, 0.f, p1);
        VectorRotate(p1, Matrix, o->HeadAngle);
        o->HeadAngle[2] = 15;

        o->SubType = WorldRandom() % 2;

        o->Light[0] = o->Light[1] = o->Light[2] = 0.5 + (WorldRandom() % 6) * 0.1f;
        o->Direction[0] = 0;
        o->Direction[1] = 0;
        o->Direction[2] = 0;
    }
    break;
    case MODEL_STATUE_CRUSH_EFFECT_PIECE04:
    case MODEL_DOOR_CRUSH_EFFECT_PIECE10: {
        o->LifeTime = 100;
        o->Scale = Scale;

        o->PKKey = -1;
        o->Owner = Owner;
    }
    break;
    case MODEL_DOOR_CRUSH_EFFECT: {
        switch (o->SubType)

        {
        case 0: {
            vec3_t vPos;
            for (int i = 0; i < 9; i++)
            {
                Vector(o->Position[0] + (WorldRandom() % 200 - 100),
                       o->Position[1] + (WorldRandom() % 200 - 100),
                       o->Position[2] + (WorldRandom() % 200 - 100), vPos);
                CreateEffect(MODEL_DOOR_CRUSH_EFFECT_PIECE01 + i, vPos, o->Angle, o->Light, 0,
                             o->Owner, 0, 0);
            }
        }
        break;
        case 1: {
            vec3_t vPos;
            for (int i = 0; i < 9; i++)
            {
                Vector(o->Position[0] + (WorldRandom() % 200 - 100),
                       o->Position[1] + (WorldRandom() % 200 - 100),
                       o->Position[2] + (WorldRandom() % 200 - 100), vPos);
                CreateEffect(MODEL_DOOR_CRUSH_EFFECT_PIECE11 + (i % 3), vPos, o->Angle, o->Light, 0,
                             o->Owner, 0, 0);
            }
        }
        break;
        }
    }
    break;
    case MODEL_STATUE_CRUSH_EFFECT: {
        for (int i = 0; i < 6; i++)
        {
            CreateEffect(MODEL_STATUE_CRUSH_EFFECT_PIECE01 + (i % 3), o->Position, o->Angle,
                         o->Light, 0, o->Owner, 0, 0);
        }
    }
    break;
    case MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_:
    case MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_:
    case MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_:
    case MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_:
    case MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_:
    case MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE: {
        OBJECT *O = &(Hero->Object);
        for (int i = 113; i <= 155; ++i)
        {
            RemoveObjectBlurs(O, i);
        }

        if (o->SubType == 0)
        {
            o->LifeTime = 2;
            o->ChromeEnable = true;
        }
        else if (o->SubType == 3)
        {
            const int TOTAL_LIFETIME = 60;
            vec3_t v3PosStart, v3PosTarget;
            vec3_t arv3PosProcess[4];

            o->ExtState = TOTAL_LIFETIME;
            o->LifeTime = TOTAL_LIFETIME;
            o->Gravity = 0.0f;
            o->HiddenMesh = 1;
            o->Distance = 0.0f;
            o->Alpha = 1.0f;
            o->ChromeEnable = false;

            Vector(o->Angle[0], o->Angle[1], o->Angle[2], o->HeadAngle);

            VectorCopy(o->Position, o->StartPosition);
            VectorCopy(o->Position, v3PosStart);
            VectorCopy(o->Light, v3PosTarget);

            vec3_t v3DirDistAD;
            vec3_t v3PosStartModify, v3PosTargetModify;
            const int iLimitArea1 = 200;
            float fDistAD, fDistAB, fDistCD;
            float fHeightTerrainTarget = RequestTerrainHeight(v3PosTarget[0], v3PosTarget[1]);
            int iOffsetDist = 100;
            VectorCopy(v3PosStart, v3PosStartModify);
            VectorCopy(v3PosTarget, v3PosTargetModify);
            v3PosTargetModify[2] = v3PosStartModify[2];
            fDistAD = VectorDistance3D_DirDist(v3PosStartModify, v3PosTargetModify, v3DirDistAD);

            fDistAB = 900.0f + (WorldRandom() % iOffsetDist - (iOffsetDist / 2));
            fDistCD;
            VectorCopy(v3PosStart, arv3PosProcess[0]);
            arv3PosProcess[1][0] =
                arv3PosProcess[0][0] + (float)(WorldRandom() % iLimitArea1 - (iLimitArea1 / 2));
            arv3PosProcess[1][1] =
                arv3PosProcess[0][1] + (float)(WorldRandom() % iLimitArea1 - (iLimitArea1 / 2));
            arv3PosProcess[1][2] = v3PosStart[2] + fDistAB;
            VectorAdd(arv3PosProcess[1], v3DirDistAD, arv3PosProcess[2]);

            VectorCopy(v3PosTargetModify, arv3PosProcess[3]);
            arv3PosProcess[3][0] =
                arv3PosProcess[2][0]; // + (float)(WorldRandom()%iLimitArea2 - (iLimitArea2/2));
            arv3PosProcess[3][1] =
                arv3PosProcess[2][1]; // + (float)(WorldRandom()%iLimitArea2 - (iLimitArea2/2));
            arv3PosProcess[3][2] = fHeightTerrainTarget + (float)iOffsetDist * 1.2f +
                                   ((WorldRandom() % iOffsetDist) - (iOffsetDist / 2));

            float arfRates[] = {0.0f, 0.22f, 0.35f, 0.50f, 1.01f};
            float fOffsetRate = (0.03f * (o->Type - MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_));

            o->m_Interpolates.ClearContainer();
            CInterpolateContainer::INTERPOLATE_FACTOR InsertFactor;
            InsertFactor.fRateStart = arfRates[0] + fOffsetRate;
            InsertFactor.fRateEnd = arfRates[1] + fOffsetRate;
            VectorCopy(v3PosStart, InsertFactor.v3Start);
            VectorCopy(arv3PosProcess[1], InsertFactor.v3End);
            o->m_Interpolates.m_vecInterpolatesPos.push_back(InsertFactor);

            InsertFactor.fRateStart = arfRates[1] + fOffsetRate;
            InsertFactor.fRateEnd = arfRates[2] + fOffsetRate;
            VectorCopy(arv3PosProcess[1], InsertFactor.v3Start);
            VectorCopy(arv3PosProcess[2], InsertFactor.v3End);
            o->m_Interpolates.m_vecInterpolatesPos.push_back(InsertFactor);

            InsertFactor.fRateStart = arfRates[2] + fOffsetRate;
            InsertFactor.fRateEnd = arfRates[3] + fOffsetRate;
            VectorCopy(arv3PosProcess[2], InsertFactor.v3Start);
            VectorCopy(arv3PosProcess[3], InsertFactor.v3End);
            o->m_Interpolates.m_vecInterpolatesPos.push_back(InsertFactor);

            InsertFactor.fRateStart = arfRates[3] + fOffsetRate;
            InsertFactor.fRateEnd = arfRates[4];
            VectorCopy(arv3PosProcess[3], InsertFactor.v3Start);
            VectorCopy(arv3PosProcess[3], InsertFactor.v3End);
            o->m_Interpolates.m_vecInterpolatesPos.push_back(InsertFactor);

            CInterpolateContainer::INTERPOLATE_FACTOR_F InsertFactorF;
            InsertFactorF.fRateStart = arfRates[0];
            InsertFactorF.fRateEnd = arfRates[3];
            InsertFactorF.fStart = 1.0f;
            InsertFactorF.fEnd = 1.0f;

            InsertFactorF.fRateStart = arfRates[3];
            InsertFactorF.fRateEnd = arfRates[4];
            InsertFactorF.fStart = 1.0f;
            InsertFactorF.fEnd = 0.0f;
            o->m_Interpolates.m_vecInterpolatesAlpha.push_back(InsertFactorF);
            o->m_Interpolates.GetAngleCurrent(o->Angle, 0.0f);
            o->m_Interpolates.GetPosCurrent(o->Position, 0.0f);
            o->m_Interpolates.GetAlphaCurrent(o->Alpha, 0.0f);

            CreateJoint(BITMAP_FLARE + 1, o->Position, o->Position, o->Angle, 20, o, 160.f, 40);
        }
        else if (o->SubType == 11)
        {
            const int TOTALLIFETIME = 24;
            o->LifeTime = TOTALLIFETIME;
            o->ExtState = TOTALLIFETIME;
            o->ChromeEnable = true;
            o->Scale = Scale;

            o->m_Interpolates.ClearContainer();

            CInterpolateContainer::INTERPOLATE_FACTOR_F InsertFactorF;
            InsertFactorF.fRateStart = 0.0f;
            InsertFactorF.fRateEnd = 0.61f;
            InsertFactorF.fStart = 0.0f;
            InsertFactorF.fEnd = 1.0f;
            o->m_Interpolates.m_vecInterpolatesAlpha.push_back(InsertFactorF);

            InsertFactorF.fRateStart = 0.61f;
            InsertFactorF.fRateEnd = 1.01f;
            InsertFactorF.fStart = 1.0f;
            InsertFactorF.fEnd = 1.0f;
            o->m_Interpolates.m_vecInterpolatesAlpha.push_back(InsertFactorF);
        }
        else if (o->SubType == 12)
        {
            const int TOTALLIFETIME = 14;
            o->LifeTime = TOTALLIFETIME;
            o->ExtState = TOTALLIFETIME;
            o->ChromeEnable = true;
            o->Scale = Scale;
            o->m_Interpolates.ClearContainer();

            CInterpolateContainer::INTERPOLATE_FACTOR_F InsertFactorF;
            InsertFactorF.fRateStart = 0.0f;
            InsertFactorF.fRateEnd = 1.01f;
            InsertFactorF.fStart = 0.0f;
            InsertFactorF.fEnd = 1.0f;
            o->m_Interpolates.m_vecInterpolatesAlpha.push_back(InsertFactorF);
        }
        else if (o->SubType == 20)
        {
            const int TOTALLIFETIME = 10;
            o->LifeTime = TOTALLIFETIME;
            o->ExtState = TOTALLIFETIME;
            o->ChromeEnable = false;

            switch (o->Type)
            {
            case MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_:
            case MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_: {
                o->LifeTime = 10;
            }
            break;
            case MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_:
            case MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_: {
                o->LifeTime = 5;
            }
            break;
            case MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE: {
                o->LifeTime = 5;
            }
            }
            o->m_Interpolates.ClearContainer();
            CInterpolateContainer::INTERPOLATE_FACTOR InsertFactor;
            InsertFactor.fRateStart = 0.0f;
            InsertFactor.fRateEnd = 1.0f;
            VectorCopy(o->Angle, InsertFactor.v3Start);
            VectorCopy(o->Angle, InsertFactor.v3End);

            o->m_Interpolates.m_vecInterpolatesAngle.push_back(InsertFactor);
            InsertFactor.fRateStart = 0.0f;
            InsertFactor.fRateEnd = 1.0f;
            VectorCopy(o->Position, InsertFactor.v3Start);
            VectorCopy(o->Position, InsertFactor.v3End);
            o->m_Interpolates.m_vecInterpolatesPos.push_back(InsertFactor);

            CInterpolateContainer::INTERPOLATE_FACTOR_F InsertFactorF;
            InsertFactorF.fRateStart = 0.0f;
            InsertFactorF.fRateEnd = 1.0f;
            InsertFactorF.fStart = 0.7f;
            InsertFactorF.fEnd = 0.0f;
            o->m_Interpolates.m_vecInterpolatesAlpha.push_back(InsertFactorF);

            InsertFactorF.fRateStart = 0.0f;
            InsertFactorF.fRateEnd = 1.0f;
            InsertFactorF.fStart = o->Scale;
            InsertFactorF.fEnd = o->Scale;
            o->m_Interpolates.m_vecInterpolatesScale.push_back(InsertFactorF);
        }
        else // SubType == 1
        {
            switch (o->Type)
            {
            case MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_: {
                if (o->SubType == 1)
                {
                    const int TOTAL_LIFETIME = 35;
                    vec3_t v3PosProcess01, v3PosProcessFinal, v3DirModify, v3PosModify,
                        v3PosTargetModify;

                    o->ExtState = TOTAL_LIFETIME;
                    o->LifeTime = TOTAL_LIFETIME;
                    o->Gravity = 0.0f;
                    o->HiddenMesh = 1;
                    o->Distance = 0.0f;
                    o->Alpha = 1.0f;
                    o->ChromeEnable = false;

                    Vector(o->Angle[0], o->Angle[1], o->Angle[2], o->HeadAngle);
                    VectorCopy(o->Position, o->StartPosition);
                    VectorSubtract(o->Light, o->Position, o->Direction);
                    o->Distance =
                        sqrt(o->Direction[0] * o->Direction[0] + o->Direction[1] * o->Direction[1] +
                             o->Direction[2] * o->Direction[2]);

                    VectorDivFSelf(o->Direction, o->Distance);

                    float fDistanceResult = 0;
                    vec3_t v3DirResult;
                    vec3_t v3PosModify02;
                    float fTotalDist = 1700.0f, fFirstDist = 0.0f, fRateFirstDist = 0.0f;
                    VectorCopy(o->Owner->Position, v3PosModify02);
                    VectorCopy(o->Position, v3PosModify);

                    v3PosModify02[2] = o->Light[2];
                    v3PosModify[2] = o->Light[2];
                    VectorCopy(o->Light, v3PosTargetModify);
                    v3PosModify[2] = v3PosModify[2] + 100.0f;
                    v3PosModify02[2] = v3PosModify02[2] + 100.0f;
                    v3PosTargetModify[2] = v3PosTargetModify[2] + 100.0f;
                    VectorDistNormalize(v3PosModify02, v3PosTargetModify, v3DirModify);

                    fDistanceResult = o->Distance * 0.3f;
                    fFirstDist = fDistanceResult;
                    VectorMulF(v3DirModify, fDistanceResult, v3DirResult);
                    VectorAdd(v3PosModify, v3DirResult, v3PosProcess01);

                    fDistanceResult = fTotalDist;
                    VectorMulF(v3DirModify, fDistanceResult, v3DirResult);
                    VectorAdd(v3PosModify, v3DirResult, v3PosProcessFinal);

                    fRateFirstDist = fFirstDist / fTotalDist;

                    o->m_Interpolates.ClearContainer();

                    fRateFirstDist = fFirstDist / fTotalDist;

                    CInterpolateContainer::INTERPOLATE_FACTOR InsertFactor;
                    InsertFactor.fRateStart = 0.0f;
                    InsertFactor.fRateEnd = fRateFirstDist;
                    Vector(0.0f, 0.0f, 0.0f, InsertFactor.v3Start);
                    Vector(90.0f, 0.0f, 0.0f, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesAngle.push_back(InsertFactor);

                    InsertFactor.fRateStart = fRateFirstDist;
                    InsertFactor.fRateEnd = 1.01f;
                    Vector(90.0f, 0.0f, 0.0f, InsertFactor.v3Start);
                    Vector(90.0f, 0.0f, 1560.0f, InsertFactor.v3End);

                    o->m_Interpolates.m_vecInterpolatesAngle.push_back(InsertFactor);
                    InsertFactor.fRateStart = 0.0f;
                    InsertFactor.fRateEnd = fRateFirstDist;
                    VectorCopy(o->Position, InsertFactor.v3Start);
                    VectorCopy(v3PosProcess01, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesPos.push_back(InsertFactor);

                    InsertFactor.fRateStart = fRateFirstDist;
                    InsertFactor.fRateEnd = 1.01f;
                    VectorCopy(v3PosProcess01, InsertFactor.v3Start);
                    VectorCopy(v3PosProcessFinal, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesPos.push_back(InsertFactor);

                    o->m_Interpolates.GetAngleCurrent(o->Angle, 0.0f);
                    o->m_Interpolates.GetPosCurrent(o->Position, 0.0f);
                }
            } // MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_
            break;
            case MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_: {
                if (o->SubType == 1)
                {
                    const int TOTAL_LIFETIME = 35;
                    vec3_t v3PosProcess01, v3PosProcessFinal, v3DirModify, v3PosModify,
                        v3PosTargetModify;

                    o->ExtState = TOTAL_LIFETIME;
                    o->LifeTime = TOTAL_LIFETIME;
                    o->Gravity = 0.0f;
                    o->HiddenMesh = 1;
                    o->Distance = 0.0f;
                    o->Alpha = 1.0f;
                    o->ChromeEnable = false;

                    Vector(o->Angle[0], o->Angle[1], o->Angle[2], o->HeadAngle);
                    VectorCopy(o->Position, o->StartPosition);
                    VectorSubtract(o->Light, o->Position, o->Direction);

                    o->Distance =
                        sqrt(o->Direction[0] * o->Direction[0] + o->Direction[1] * o->Direction[1] +
                             o->Direction[2] * o->Direction[2]);

                    VectorDivFSelf(o->Direction, o->Distance);

                    float fDistanceResult = 0;
                    vec3_t v3DirResult;

                    vec3_t v3PosModify02;
                    float fTotalDist = 1700.0f, fFirstDist = 0.0f, fRateFirstDist = 0.0f;
                    VectorCopy(o->Owner->Position, v3PosModify02);
                    VectorCopy(o->Position, v3PosModify);

                    v3PosModify02[2] = o->Light[2];
                    v3PosModify[2] = o->Light[2];
                    VectorCopy(o->Light, v3PosTargetModify);
                    v3PosModify[2] = v3PosModify[2] + 100.0f;
                    v3PosModify02[2] = v3PosModify02[2] + 100.0f;
                    v3PosTargetModify[2] = v3PosTargetModify[2] + 100.0f;
                    VectorDistNormalize(v3PosModify02, v3PosTargetModify, v3DirModify);

                    fDistanceResult = o->Distance * 0.3f;
                    fFirstDist = fDistanceResult;
                    VectorMulF(v3DirModify, fDistanceResult, v3DirResult);
                    VectorAdd(v3PosModify, v3DirResult, v3PosProcess01);

                    fDistanceResult = fTotalDist;
                    VectorMulF(v3DirModify, fDistanceResult, v3DirResult);
                    VectorAdd(v3PosModify, v3DirResult, v3PosProcessFinal);

                    fRateFirstDist = fFirstDist / fTotalDist;

                    o->m_Interpolates.ClearContainer();

                    CInterpolateContainer::INTERPOLATE_FACTOR InsertFactor;
                    InsertFactor.fRateStart = 0.0f;
                    InsertFactor.fRateEnd = fRateFirstDist;
                    Vector(0.0f, 0.0f, 0.0f, InsertFactor.v3Start);
                    Vector(90.0f, 0.0f, 0.0f, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesAngle.push_back(InsertFactor);

                    InsertFactor.fRateStart = fRateFirstDist;
                    InsertFactor.fRateEnd = 1.01f;
                    Vector(90.0f, 0.0f, 0.0f, InsertFactor.v3Start);
                    Vector(90.0f, 0.0f, -1560.0f, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesAngle.push_back(InsertFactor);

                    InsertFactor.fRateStart = 0.0f;
                    InsertFactor.fRateEnd = fRateFirstDist;
                    VectorCopy(o->Position, InsertFactor.v3Start);
                    VectorCopy(v3PosProcess01, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesPos.push_back(InsertFactor);

                    InsertFactor.fRateStart = fRateFirstDist;
                    InsertFactor.fRateEnd = 1.01f;
                    VectorCopy(v3PosProcess01, InsertFactor.v3Start);
                    VectorCopy(v3PosProcessFinal, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesPos.push_back(InsertFactor);

                    o->m_Interpolates.GetAngleCurrent(o->Angle, 0.0f);
                    o->m_Interpolates.GetPosCurrent(o->Position, 0.0f);
                }
            } // MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_
            break;
            case MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_: {
                if (o->SubType == 1) // 일반공격 Animation
                {
                    //const int	TOTAL_LIFETIME = 24;
                    const int TOTAL_LIFETIME = 30;
                    vec3_t v3PosProcess01, v3PosProcessFinal, v3AngleOffset, v3Dir, v3DirPower,
                        v3DirModify, v3DirModifyR;

                    o->ExtState = TOTAL_LIFETIME;
                    o->LifeTime = TOTAL_LIFETIME;
                    o->Gravity = 0.2f;
                    o->HiddenMesh = 1;
                    o->Distance = 0.0f;
                    o->Alpha = 1.0f;
                    o->ChromeEnable = false;

                    Vector(o->Angle[0], o->Angle[1], o->Angle[2], o->HeadAngle);
                    VectorCopy(o->Position, o->StartPosition);
                    vec3_t v3PosStartModify, v3PosEndModify;
                    VectorCopy(o->Light, v3PosEndModify);
                    VectorCopy(o->StartPosition, v3PosStartModify);
                    v3PosStartModify[2] = v3PosEndModify[2];

                    VectorSubtract(v3PosEndModify, v3PosStartModify, v3DirModify);
                    VectorSubtract(o->Light, o->StartPosition, v3Dir);

                    o->Distance =
                        sqrt(v3Dir[0] * v3Dir[0] + v3Dir[1] * v3Dir[1] + v3Dir[2] * v3Dir[2]);

                    v3Dir[0] /= o->Distance;
                    v3Dir[1] /= o->Distance;
                    v3Dir[2] /= o->Distance;

                    VectorNormalize(v3DirModify);
                    VectorCopy(v3Dir, o->Direction);

                    Vector(0.0f, 0.0f, -40.0f, v3AngleOffset);
                    AngleMatrix(v3AngleOffset, Matrix);
                    VectorRotate(v3DirModify, Matrix, v3DirModifyR);
                    VectorMulF(v3DirModifyR, o->Distance * 0.4f, v3DirPower);
                    VectorAdd(o->StartPosition, v3DirPower, v3PosProcess01);
                    v3PosProcess01[2] -= 100.0f;

                    Vector(0.0f, 0.0f, 24.0f, v3AngleOffset);
                    AngleMatrix(v3AngleOffset, Matrix);
                    VectorRotate(v3DirModify, Matrix, v3DirModifyR);
                    VectorMulF(v3DirModifyR, o->Distance + 700.0f, v3DirPower);
                    VectorAdd(o->StartPosition, v3DirPower, v3PosProcessFinal);
                    v3PosProcessFinal[2] -= 290.0f;

                    o->m_Interpolates.ClearContainer();

                    CInterpolateContainer::INTERPOLATE_FACTOR InsertFactor;
                    InsertFactor.fRateStart = 0.0f;
                    InsertFactor.fRateEnd = 0.32f;
                    Vector(0.0f, 0.0f, 0.0f, InsertFactor.v3Start);
                    Vector(90.0f, 0.0f, 0.0f, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesAngle.push_back(InsertFactor);

                    InsertFactor.fRateStart = 0.32f;
                    InsertFactor.fRateEnd = 1.01f;
                    Vector(90.0f, 0.0f, 0.0f, InsertFactor.v3Start);
                    Vector(90.0f, 0.0f, 340.0f, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesAngle.push_back(InsertFactor);
                    InsertFactor.fRateStart = 0.0f;
                    InsertFactor.fRateEnd = 0.32f;
                    VectorCopy(o->Position, InsertFactor.v3Start);
                    VectorCopy(v3PosProcess01, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesPos.push_back(InsertFactor);

                    InsertFactor.fRateStart = 0.32f;
                    InsertFactor.fRateEnd = 1.01f;
                    VectorCopy(v3PosProcess01, InsertFactor.v3Start);
                    VectorCopy(v3PosProcessFinal, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesPos.push_back(InsertFactor);

                    CInterpolateContainer::INTERPOLATE_FACTOR_F InsertFactorF;
                    InsertFactorF.fRateStart = 0.0f;
                    InsertFactorF.fRateEnd = 0.15f;
                    InsertFactorF.fStart = 1.0f;
                    InsertFactorF.fEnd = 1.0f;
                    o->m_Interpolates.m_vecInterpolatesAlpha.push_back(InsertFactorF);

                    InsertFactorF.fRateStart = 0.15f;
                    InsertFactorF.fRateEnd = 0.75f;
                    InsertFactorF.fStart = 1.0f;
                    InsertFactorF.fEnd = 1.0f;
                    o->m_Interpolates.m_vecInterpolatesAlpha.push_back(InsertFactorF);

                    InsertFactorF.fRateStart = 0.75f;
                    InsertFactorF.fRateEnd = 1.01f;
                    InsertFactorF.fStart = 1.0f;
                    InsertFactorF.fEnd = 0.0f;
                    o->m_Interpolates.m_vecInterpolatesAlpha.push_back(InsertFactorF);

                    o->m_Interpolates.GetAngleCurrent(o->Angle, 0.0f);
                    o->m_Interpolates.GetPosCurrent(o->Position, 0.0f);
                    o->m_Interpolates.GetAlphaCurrent(o->Alpha, 0.0f);
                }
            } // MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_
            break;
            case MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_: {
                if (o->SubType == 1)
                {
                    //const int	TOTAL_LIFETIME = 22;
                    const int TOTAL_LIFETIME = 30;
                    vec3_t v3PosProcess01, v3PosProcessFinal, v3AngleOffset, v3Dir, v3DirPower,
                        v3DirModify, v3DirModifyR;

                    o->ExtState = TOTAL_LIFETIME;
                    o->LifeTime = TOTAL_LIFETIME;
                    o->Gravity = 0.2f;
                    o->HiddenMesh = 1;
                    o->Distance = 0.0f;
                    o->Alpha = 1.0f;
                    o->ChromeEnable = false;

                    Vector(o->Angle[0], o->Angle[1], o->Angle[2], o->HeadAngle);
                    VectorCopy(o->Position, o->StartPosition);
                    vec3_t v3PosStartModify, v3PosEndModify;
                    VectorCopy(o->Light, v3PosEndModify);
                    VectorCopy(o->StartPosition, v3PosStartModify);
                    v3PosStartModify[2] = v3PosEndModify[2];

                    VectorSubtract(v3PosEndModify, v3PosStartModify, v3DirModify);
                    VectorSubtract(o->Light, o->StartPosition, v3Dir);

                    o->Distance =
                        sqrt(v3Dir[0] * v3Dir[0] + v3Dir[1] * v3Dir[1] + v3Dir[2] * v3Dir[2]);

                    v3Dir[0] /= o->Distance;
                    v3Dir[1] /= o->Distance;
                    v3Dir[2] /= o->Distance;

                    VectorNormalize(v3DirModify);
                    VectorCopy(v3Dir, o->Direction);

                    Vector(0.0f, 0.0f, 50.0f, v3AngleOffset);
                    AngleMatrix(v3AngleOffset, Matrix);
                    VectorRotate(v3DirModify, Matrix, v3DirModifyR);
                    VectorMulF(v3DirModifyR, o->Distance * 0.4f, v3DirPower);
                    VectorAdd(o->StartPosition, v3DirPower, v3PosProcess01);
                    v3PosProcess01[2] -= 100.0f;

                    Vector(0.0f, 0.0f, -26.0f, v3AngleOffset);
                    AngleMatrix(v3AngleOffset, Matrix);
                    VectorRotate(v3DirModify, Matrix, v3DirModifyR);
                    VectorMulF(v3DirModifyR, o->Distance + 700.0f, v3DirPower);
                    VectorAdd(o->StartPosition, v3DirPower, v3PosProcessFinal);
                    v3PosProcessFinal[2] -= 280.0f;

                    o->m_Interpolates.ClearContainer();

                    CInterpolateContainer::INTERPOLATE_FACTOR InsertFactor;
                    InsertFactor.fRateStart = 0.0f; // Start
                    InsertFactor.fRateEnd = 0.4f;   // 01 Ready
                    Vector(0.0f, 0.0f, 0.0f, InsertFactor.v3Start);
                    Vector(110.0f, 0.0f, 0.0f, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesAngle.push_back(InsertFactor); // 1#

                    InsertFactor.fRateStart = 0.4f; // 01 Ready
                    InsertFactor.fRateEnd = 1.01f;  // 02 First Final
                    Vector(110.0f, 0.0f, 0.0f, InsertFactor.v3Start);
                    Vector(60.0f, 0.0f, -300.0f, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesAngle.push_back(InsertFactor);

                    InsertFactor.fRateStart = 0.0f;
                    InsertFactor.fRateEnd = 0.33f;
                    VectorCopy(o->Position, InsertFactor.v3Start);
                    VectorCopy(v3PosProcess01, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesPos.push_back(InsertFactor); // 2#

                    InsertFactor.fRateStart = 0.33f;
                    InsertFactor.fRateEnd = 1.01f;
                    VectorCopy(v3PosProcess01, InsertFactor.v3Start);
                    VectorCopy(v3PosProcessFinal, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesPos.push_back(InsertFactor);

                    CInterpolateContainer::INTERPOLATE_FACTOR_F InsertFactorF;
                    InsertFactorF.fRateStart = 0.0f;
                    InsertFactorF.fRateEnd = 0.15f;
                    InsertFactorF.fStart = 1.0f;
                    InsertFactorF.fEnd = 1.0f;
                    o->m_Interpolates.m_vecInterpolatesAlpha.push_back(InsertFactorF); // 2#

                    InsertFactorF.fRateStart = 0.15f;
                    InsertFactorF.fRateEnd = 0.75f;
                    InsertFactorF.fStart = 1.0f;
                    InsertFactorF.fEnd = 1.0f;
                    o->m_Interpolates.m_vecInterpolatesAlpha.push_back(InsertFactorF); // 2#

                    InsertFactorF.fRateStart = 0.75f;
                    InsertFactorF.fRateEnd = 1.01f;
                    InsertFactorF.fStart = 1.0f;
                    InsertFactorF.fEnd = 0.0f;
                    o->m_Interpolates.m_vecInterpolatesAlpha.push_back(InsertFactorF);

                    o->m_Interpolates.GetAngleCurrent(o->Angle, 0.0f);
                    o->m_Interpolates.GetPosCurrent(o->Position, 0.0f);
                    o->m_Interpolates.GetAlphaCurrent(o->Alpha, 0.0f);
                }
            } // MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_
            break;
            case MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_: //
            {
                if (o->SubType == 1)
                {
                    const int TOTAL_LIFETIME = 10;

                    o->ExtState = TOTAL_LIFETIME;
                    o->LifeTime = TOTAL_LIFETIME;
                    o->ChromeEnable = true;
                    //o->Scale		= 0.8f;

                    o->m_Interpolates.ClearContainer();

                    CInterpolateContainer::INTERPOLATE_FACTOR_F InsertFactorF;
                    InsertFactorF.fRateStart = 0.0f;
                    InsertFactorF.fRateEnd = 1.01f;
                    InsertFactorF.fStart = 1.0f;
                    InsertFactorF.fEnd = 0.0f;
                    o->m_Interpolates.m_vecInterpolatesAlpha.push_back(InsertFactorF);
                }
            } // MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_
            break;
            case MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE: {
                if (o->SubType == 1)
                {
                    const int TOTAL_LIFETIME = 36;
                    vec3_t v3PosProcess01, v3DirModify, v3PosModify, v3PosTargetModify;

                    o->ExtState = TOTAL_LIFETIME;
                    o->LifeTime = TOTAL_LIFETIME;
                    o->Gravity = 0.0f;
                    o->HiddenMesh = 1;
                    o->Distance = 0.0f;
                    o->Alpha = 1.0f;
                    o->ChromeEnable = false;
                    o->Scale = 1.0f;
                    o->Visible = false;
                    o->Velocity = 0.4f;

                    BMD *b = &Models[o->Type];
                    //b->Velocity = 10.f;

                    Vector(o->Angle[0], o->Angle[1], o->Angle[2], o->HeadAngle);
                    VectorSubtract(o->Light, o->Position, o->Direction);

                    o->Distance =
                        sqrt(o->Direction[0] * o->Direction[0] + o->Direction[1] * o->Direction[1] +
                             o->Direction[2] * o->Direction[2]);

                    VectorDivFSelf(o->Direction, o->Distance);

                    VectorCopy(o->Owner->Position, v3PosModify);
                    v3PosModify[2] = o->Light[2];
                    VectorCopy(o->Light, v3PosTargetModify);

                    v3PosModify[2] = v3PosModify[2] + 100.0f;
                    v3PosTargetModify[2] = v3PosTargetModify[2] + 100.0f;
                    VectorDistNormalize(v3PosTargetModify, v3PosModify, v3DirModify);

                    //vc = va + scale*vb
                    VectorMA(v3PosTargetModify, 200.0f, v3DirModify, v3PosProcess01);

                    VectorCopy(v3PosProcess01, o->StartPosition);

                    o->m_Interpolates.ClearContainer();

                    CInterpolateContainer::INTERPOLATE_FACTOR InsertFactor;
                    InsertFactor.fRateStart = 0.0f;
                    InsertFactor.fRateEnd = 0.28f;
                    Vector(0.0f, 0.0f, 0.0f, InsertFactor.v3Start);
                    Vector(0.0f, 0.0f, 0.0f, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesAngle.push_back(InsertFactor); // 1#

                    InsertFactor.fRateStart = 0.28f;
                    InsertFactor.fRateEnd = 0.36f;
                    Vector(0.0f, 0.0f, 0.0f, InsertFactor.v3Start);
                    Vector(-90.0f, 0.0f, 0.0f, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesAngle.push_back(InsertFactor); // 2#

                    InsertFactor.fRateStart = 0.36f;
                    InsertFactor.fRateEnd = 1.01f;
                    Vector(-90.0f, 0.0f, 0.0f, InsertFactor.v3Start);
                    Vector(-90.0f, 0.0f, 880.0f, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesAngle.push_back(InsertFactor);

                    InsertFactor.fRateStart = 0.0f;
                    InsertFactor.fRateEnd = 1.01f;
                    VectorCopy(v3PosProcess01, InsertFactor.v3Start);
                    VectorCopy(v3PosProcess01, InsertFactor.v3End);
                    o->m_Interpolates.m_vecInterpolatesPos.push_back(InsertFactor); // 2#

                    CInterpolateContainer::INTERPOLATE_FACTOR_F InsertFactorF;
                    InsertFactorF.fRateStart = 0.0f;
                    InsertFactorF.fRateEnd = 1.01f;
                    InsertFactorF.fStart = o->Scale * 1.0f;
                    InsertFactorF.fEnd = o->Scale * 1.0f;
                    o->m_Interpolates.m_vecInterpolatesScale.push_back(InsertFactorF); // 2#

                    InsertFactorF.fRateStart = 0.0f;
                    InsertFactorF.fRateEnd = 0.15f;
                    InsertFactorF.fStart = 0.0f;
                    InsertFactorF.fEnd = 1.0f;
                    o->m_Interpolates.m_vecInterpolatesAlpha.push_back(InsertFactorF); // 2#

                    InsertFactorF.fRateStart = 0.15f;
                    InsertFactorF.fRateEnd = 0.75f;
                    InsertFactorF.fStart = 1.0f;
                    InsertFactorF.fEnd = 1.0f;
                    o->m_Interpolates.m_vecInterpolatesAlpha.push_back(InsertFactorF); // 2#

                    InsertFactorF.fRateStart = 0.75f;
                    InsertFactorF.fRateEnd = 1.01f;
                    InsertFactorF.fStart = 1.0f;
                    InsertFactorF.fEnd = 0.0f;
                    o->m_Interpolates.m_vecInterpolatesAlpha.push_back(InsertFactorF); // 2#

                    o->m_Interpolates.GetAngleCurrent(o->Angle, 0.0f);
                    o->m_Interpolates.GetPosCurrent(o->Position, 0.0f);
                    o->m_Interpolates.GetAlphaCurrent(o->Alpha, 0.0f);
                    o->m_Interpolates.GetScaleCurrent(o->Scale, 0.0f);
                }
            }
            }
        }
    }
    break;
    case MODEL_EMPIREGUARDIAN_BLOW_OF_DESTRUCTION: {
        if (o->SubType == 0)
        {
            o->LifeTime = 40;

            vec3_t vPos, vPos2;
            float Matrix[3][4];
            Vector(-20.f, -100.f, 0.f, vPos);
            AngleMatrix(o->Owner->Angle, Matrix);
            VectorRotate(vPos, Matrix, vPos2);
            VectorAdd(vPos2, o->Position, o->Position);
            VectorCopy(o->Light, o->StartPosition);
            Vector(1.2f, 1.2f, 1.2f, o->Light);

            CreateEffect(MODEL_EMPIREGUARDIAN_BLOW_OF_DESTRUCTION, o->StartPosition, o->Angle,
                         o->Light, 1, o->Owner, -1, 0, 0, 0, o->Scale);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 40;
            Vector(1.2f, 1.2f, 1.2f, o->Light);
            o->Position[2] = 150.f;
            o->Scale = o->Scale * 2.f;
        }
    }
    break;
#ifdef PBG_ADD_CHARACTERSLOT
    case MODEL_SLOT_LOCK: {
        o->Scale *= 2.0f;
        o->Position[2] = 300.0f;
        float temptime = sinf(WorldTime * 0.0005f);
        Vector(0.0f, 0.0f, o->Angle[2] + temptime * 10, o->Angle);
        o->LifeTime = 1000;
    }
    break;
#endif //PBG_ADD_CHARACTERSLOT
    case BITMAP_RING_OF_GRADATION: {
        if (o->SubType == 0)
        {
            o->LifeTime = 20;
        }
    }
    break;
    case MODEL_EFFECT_UMBRELLA_DIE: {
        if (o->SubType == 0)
        {
            o->LifeTime = 30;
        }
    }
    break;
    case MODEL_EFFECT_UMBRELLA_GOLD: {
        o->LifeTime = WorldRandom() % 10 + 50;
        o->Scale = 2.0f + (WorldRandom() % 10 - 5) * 0.2f;

        o->Angle[0] = (float)(WorldRandom() % 360);
        o->Angle[1] = (float)(WorldRandom() % 360);
        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        o->Gravity = (float)(WorldRandom() % 10 + 10);
        vec3_t p;
        Vector((float)(WorldRandom() % 10 - 5) * 0.1f, (float)(WorldRandom() % 60 - 30) * 0.1f,
               0.0f, p);
        VectorScale(p, 1.2f, p);
        VectorRotate(p, Matrix, o->Direction);

        o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 100.f;
    }
    break;
    case MODEL_EFFECT_EG_GUARDIANDEFENDER_ATTACK2: {
        o->LifeTime = 20;
        o->Scale = 0.9f;
    }
    break;
    case MODEL_EFFECT_SD_AURA: {
        o->LifeTime = 1000;
        o->Scale = 1.0f;
    }
    break;
    case BITMAP_WATERFALL_4: {
        o->LifeTime = 80;
        o->Scale = (WorldRandom() % 20 * 0.01f) + 0.1f;
        o->Angle[0] = (float)(WorldRandom() % 360);
        o->Distance = WorldRandom() % 10 + 5.0f;
        o->Timer = (float)(WorldRandom() % 360);
    }
    break;
    case MODEL_WOLF_HEAD_EFFECT: {
        BMD *b = &Models[o->Owner->Type];
        b->TransformByObjectBone(p1, o->Owner, 27);
        VectorCopy(p1, o->Position);
        if (o->SubType == 0)
        {
            o->Scale = 1.0f;
            o->LifeTime = 10.0f / Models[o->Owner->Type].Actions[PLAYER_SKILL_THRUST].PlaySpeed;
        }
        else if (o->SubType == 1)
        {
            o->Velocity = 0.1f;
            o->Scale = 1.2f + WorldRandom() % 5 * 0.1f;
            o->Direction[1] = -50.f;
            o->LifeTime = 2 + WorldRandom() % 2;
        }
        else if (o->SubType == 2)
        {
            o->Velocity = 0.1f;
            o->Scale = 0.8f + WorldRandom() % 3 * 0.1f;
            o->Direction[1] = -40.f;
            o->LifeTime = 3 + WorldRandom() % 3;
            o->Angle[2] += ((WorldRandom() % 10 - 5.0f));
        }
    }
    break;
    case BITMAP_SBUMB: {
        o->LifeTime = 20;
        o->Scale = Scale;
        BMD *b = &Models[o->Owner->Type];
        b->TransformByObjectBone(p1, o->Owner, 27);
        o->Position[2] = p1[2];
    }
    break;
    case MODEL_DOWN_ATTACK_DUMMY_L:
    case MODEL_DOWN_ATTACK_DUMMY_R: {
        VectorCopy(Position, o->Position);
        o->Velocity = Models[o->Owner->Type].Actions[PLAYER_SKILL_STAMP].PlaySpeed * 2.0f;
        o->LifeTime = 100;
        Models[MODEL_DOWN_ATTACK_DUMMY_L].Actions[0].PlaySpeed =
            Models[o->Owner->Type].Actions[PLAYER_SKILL_STAMP].PlaySpeed;
        Models[MODEL_DOWN_ATTACK_DUMMY_R].Actions[0].PlaySpeed =
            Models[o->Owner->Type].Actions[PLAYER_SKILL_STAMP].PlaySpeed;
    }
    break;
    case MODEL_SHOCKWAVE01: {
        if (o->Owner == NULL)
        {
            RetireEffect(o);
            break;
        }

        o->Scale = Scale;
        VectorCopy(Light, o->Light);
        if (o->SubType == 1)
        {
            o->Velocity = Models[o->Owner->Type].Actions[PLAYER_SKILL_DRAGONLORE].PlaySpeed;
            o->LifeTime = 5.0f / o->Velocity;
        }
        else if (o->SubType == 2)
        {
            o->Velocity = 0.3f;
            o->LifeTime = 5.0f / o->Velocity;
        }
        else if (o->SubType == 3)
        {
            o->Velocity =
                Models[o->Owner->Type].Actions[PLAYER_SKILL_DARKSIDE_READY].PlaySpeed * 2.0f;
            o->LifeTime = 5.0f / o->Velocity;
            o->Scale = Scale;
            vec3_t vPos, vDir;
            VectorCopy(o->Position, vPos);
            VectorCopy(o->Owner->StartPosition, vDir);
            vPos[2] = RequestTerrainHeight(vPos[0], vPos[1]);
            vDir[2] = RequestTerrainHeight(vDir[0], vDir[1]);
            VectorSubtract(vPos, vDir, vDir);
            VectorNormalize(vDir);
            VectorScale(vDir, 30, vDir);
            VectorCopy(vDir, o->StartPosition);
            vec3_t Transpos;
            VectorScale(vDir, 5.0f, vDir);
            VectorCopy(vDir, Transpos);
            VectorAdd(o->Position, Transpos, o->Position);
        }
        else if (o->SubType == 4)
        {
            o->Velocity = 0.6f;
            o->LifeTime = 12.0f;
            o->Scale = Scale;
            vec3_t vPos, vDir;
            VectorCopy(o->Position, vPos);
            VectorCopy(o->Owner->StartPosition, vDir);
            vPos[2] = RequestTerrainHeight(vPos[0], vPos[1]);
            vDir[2] = RequestTerrainHeight(vDir[0], vDir[1]);
            VectorSubtract(vPos, vDir, vDir);
            VectorNormalize(vDir);
            VectorScale(vDir, 30, vDir);
            VectorCopy(vDir, o->StartPosition);
            vec3_t Transpos;
            VectorNormalize(vDir);
            VectorScale(vDir, 150.0f, vDir);
            VectorCopy(vDir, Transpos);
            VectorAdd(o->Position, Transpos, o->Position);
        }
        else
        {
            o->Velocity = Models[o->Owner->Type].Actions[PLAYER_SKILL_GIANTSWING].PlaySpeed * 2.0f;
            o->LifeTime = 5.0f / o->Velocity;

            vec3_t vPos, vDir;
            VectorCopy(o->Position, vPos);
            VectorCopy(o->Owner->Position, vDir);
            vPos[2] = RequestTerrainHeight(vPos[0], vPos[1]);
            vDir[2] = RequestTerrainHeight(vDir[0], vDir[1]);
            VectorSubtract(vPos, vDir, vDir);
            VectorNormalize(vDir);
            VectorScale(vDir, 30, vDir);
            VectorCopy(vDir, o->StartPosition);
        }
    }
    break;
    case MODEL_SHOCKWAVE02: {
        o->Scale = Scale;
        VectorCopy(Light, o->Light);
        o->Velocity = Models[o->Owner->Type].Actions[PLAYER_SKILL_GIANTSWING].PlaySpeed * 2.0f;
        o->LifeTime = 14.0f / o->Velocity;

        vec3_t vPos, vDir;
        VectorCopy(o->Position, vPos);
        VectorCopy(o->Owner->Position, vDir);
        vPos[2] = RequestTerrainHeight(vPos[0], vPos[1]);
        vDir[2] = RequestTerrainHeight(vDir[0], vDir[1]);
        VectorSubtract(vPos, vDir, vDir);
        VectorNormalize(vDir);
        VectorScale(vDir, 25, vDir);
        VectorCopy(vDir, o->StartPosition);
    }
    break;
    case BITMAP_DAMAGE1: {
        o->LifeTime = 20;
        o->Scale = Scale;
        BMD *b = &Models[o->Owner->Type];
        b->TransformByObjectBone(p1, o->Owner, 27);
        o->Position[2] = p1[2];
    }
    break;
    case MODEL_SHOCKWAVE_SPIN01: {
        o->Scale = Scale;
        VectorCopy(Light, o->Light);
        o->Velocity = Models[o->Owner->Type].Actions[PLAYER_SKILL_GIANTSWING].PlaySpeed;
        o->LifeTime = 8.0f / o->Velocity;
        if (o->SubType == 0)
        {
            o->Velocity *= 2.0f;
        }
        else if (o->SubType == 1)
        {
            o->Velocity *= 3.0f;
        }
    }
    break;
    case BITMAP_EVENT_CLOUD: {
        o->Scale = Scale;
        o->LifeTime = 30;
        VectorCopy(Light, o->Light);
        if (o->SubType == 1)
        {
            o->LifeTime = 30;
        }
    }
    break;
    case MODEL_WINDFOCE: {
        o->LifeTime = 50;
        VectorCopy(Light, o->Light);
        o->Scale = Scale;

        if (o->SubType == 1)
        {
            o->LifeTime = 999;
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 70;
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = 70;
        }
    }
    break;
    case BITMAP_SHINY + 4: {
        o->LifeTime = 16;
        o->Scale = Scale;
    }
    break;
    case BITMAP_LIGHT_RED: {
        o->LifeTime = 9999;
        o->Velocity = 0.0f;
        o->Scale = 1.5f;
        Vector(1.0f, 1.0f, 1.0f, o->Light);
        VectorCopy(Position, o->Position);
        if (o->SubType == 1)
        {
            o->LifeTime = 50;
        }
        else if (o->SubType == 3 || o->SubType == 4)
        {
            o->Scale = Scale;
            o->LifeTime = 100;
            if (o->SubType == 4)
            {
                o->LifeTime = 50;
            }
            vec3_t Light;
            float Luminosity = (float)(WorldRandom() % 5) * 0.01f;
            Vector(Luminosity + 1.0f, Luminosity + 0.2f, Luminosity + 0.2f, Light);
            int range = 2;
            if (o->SubType == 4)
            {
                range = 4;
            }
            AddTerrainLight(o->Position[0], o->Position[1], Light, range, PrimaryTerrainLight);
        }
    }
    break;
    case MODEL_WINDFOCE_MIRROR: {
        VectorCopy(Light, o->Light);
        o->Scale = Scale;
        o->LifeTime = 50;
    }
    break;
    case BITMAP_SWORD_EFFECT_MONO: {
        o->LifeTime = 15;
        VectorCopy(Light, o->Light);
        o->Scale = Scale;
    }
    break;
    case MODEL_WOLF_HEAD_EFFECT2: {
        BMD *b = &Models[o->Owner->Type];
        b->TransformByObjectBone(p1, o->Owner, 27);
        VectorCopy(p1, o->Position);

        if (o->SubType == 3 || o->SubType == 5)
        {
            b->TransformByObjectBone(p1, o->Owner, 5);
            VectorCopy(p1, o->Position);
            o->Scale = 1.0f;
            if (o->SubType == 5)
            {
                o->Scale = 1.5f;
            }
            o->LifeTime = 4.0f / Models[o->Owner->Type].Actions[PLAYER_SKILL_GIANTSWING].PlaySpeed;
        }
        else if (o->SubType == 4)
        {
            o->Velocity = 0.1f;
            o->Scale = 0.7f + WorldRandom() % 3 * 0.1f;
            o->Direction[1] = -40.0f;
            o->LifeTime = 5 + WorldRandom() % 2;
        }
        else if (o->SubType == 6)
        {
            o->Velocity = 0.1f;
            o->Scale = 1.2f + WorldRandom() % 5 * 0.1f;
            o->Direction[1] = -50.f;
            o->LifeTime = 2 + WorldRandom() % 2;
        }
        Vector(0.4f, 0.4f, 0.6f, o->Light);
    }
    break;
    case MODEL_SHOCKWAVE_GROUND01: {
        if (o->SubType == 0)
        {
            o->LifeTime = 20;
            VectorCopy(Light, o->Light);
            o->Scale = Scale;
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 10;
            VectorCopy(Light, o->Light);
            o->Scale = Scale;
        }
        else
        {
            o->LifeTime = 50;
            VectorCopy(Light, o->Light);
            o->Scale = Scale;
        }
    }
    break;
    case MODEL_DRAGON_KICK_DUMMY: {
        VectorCopy(Position, o->Position);
        // ani keyframe 10 : 19
        o->LifeTime = 200;
        Models[MODEL_DRAGON_KICK_DUMMY].Actions[0].PlaySpeed =
            Models[o->Owner->Type].Actions[PLAYER_SKILL_DRAGONKICK].PlaySpeed;
        o->Velocity = Models[MODEL_DRAGON_KICK_DUMMY].Actions[0].PlaySpeed * 2.0f;
        VectorCopy(Light, o->Light);
    }
    break;
    case BITMAP_LAVA: {
        o->LifeTime = 999;
        VectorCopy(o->Owner->Position, o->StartPosition);
        o->Scale = Scale;
        o->Velocity = Models[o->Owner->Type].Actions[PLAYER_SKILL_DRAGONLORE].PlaySpeed;
    }
    break;
    case MODEL_DRAGON_LOWER_DUMMY: {
        VectorCopy(Light, o->Light);
        o->Scale = Scale;
        o->Velocity = 0.3f;
        o->LifeTime = 300;
        o->Alpha = 1.0f;
        vec3_t vPos;
        VectorCopy(o->Position, vPos);
        vPos[2] = RequestTerrainHeight(vPos[0], vPos[1]);
        o->Position[2] = vPos[2] + 10.0f;
        o->Angle[2] = 45.0f + WorldRandom() % 180;
        Models[o->Type].Actions[o->CurrentAction].PlaySpeed = 0.03f;
        o->m_iAnimation = 0;
    }
    break;
    case MODEL_TARGETMON_EFFECT: {
        o->Scale = Scale;
        VectorCopy(Light, o->Light);
        o->LifeTime = 100;
    }
    break;
    case MODEL_VOLCANO_OF_MONK: {
        if (o->SubType == 1)
        {
            o->Scale = Scale;
            VectorCopy(Light, o->Light);
            VectorCopy(Position, o->Position);
            o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);
            Models[o->Type].Actions[0].PlaySpeed = 0.1f;
            o->LifeTime = 100;
            o->Velocity = 0.6f;
            vec3_t vLight;
            Vector(1.0f, 1.0f, 1.0f, vLight);
            for (int cnt = 0; cnt < 4; ++cnt)
            {
                CreateEffect(MODEL_VOLCANO_STONE, o->Position, o->Angle, vLight, o->SubType, o, -1,
                             0, 0, 0, 1.0f);
            }
            CreateEffect(BITMAP_LIGHT_RED, o->Position, o->Angle, vLight, 3, o, -1, 0, 0, 0, 3.0f);
        }
        else if (o->SubType == 2 || o->SubType == 3)
        {
            o->LifeTime = 20 + WorldRandom() % 10;
            if (o->SubType == 3)
            {
                o->LifeTime = 30 + WorldRandom() % 20;
            }
            VectorCopy(Light, o->Light);
            VectorCopy(Position, o->Position);
        }
    }
    break;
    case MODEL_VOLCANO_STONE: {
        o->Scale = Scale + (float)(WorldRandom() % 20 + 5) * 0.06f;
        VectorCopy(Light, o->Light);
        VectorCopy(Position, o->Position);
        Vector(0.0f, 0.0f, 0.0f, o->Direction);
        o->LifeTime = WorldRandom() % 10 + 30;
        o->Gravity = (float)(WorldRandom() % 2 + 2);
        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        Vector(0.f, (float)(WorldRandom() % 128 + 64) * 0.1f, 0.f, p1);
        VectorRotate(p1, Matrix, o->HeadAngle);
        o->HeadAngle[2] += (25.0f);
    }
    break;
    case MODEL_PHOENIX_SHOT: {
        if (o->Live)
        {
        }
    }
    break;
    }
    return;
}

namespace
{
void AdvanceEffectPosition(OBJECT *o, int Turn, float frames)
{
    if (Turn)
    {
        float Matrix[3][4];
        vec3_t Angle;
        vec3_t Position;
        VectorCopy(o->Angle, Angle);
        AngleMatrix(Angle, Matrix);
        VectorRotate(o->Direction, Matrix, Position);
        VectorAddScaled(o->Position, Position, o->Position, frames);
    }
    else
    {
        VectorAddScaled(o->Position, o->Direction, o->Position, frames);
    }
}

void UmbrellaPositionAt(const OBJECT &effect, float frames, vec3_t position)
{
    constexpr float HorizontalSpeed = 2.2f, VerticalSpeed = 1.5f, GravityRate = -1.5f;
    VectorCopy(effect.Position, position);
    position[0] += effect.Direction[0] * HorizontalSpeed * frames;
    position[1] += effect.Direction[1] * HorizontalSpeed * frames;
    float velocity = effect.Gravity * VerticalSpeed;
    Core::Time::Advance(position[2], velocity, GravityRate * VerticalSpeed, frames);
}

void AdvanceUmbrellaFlight(OBJECT &effect, float frames)
{
    constexpr float GravityRate = -1.5f, SpinRate = 10.f;
    vec3_t position;
    UmbrellaPositionAt(effect, frames, position);
    VectorCopy(position, effect.Position);
    effect.Gravity += GravityRate * frames;
    effect.Angle[0] += SpinRate * frames;
}

void AdvanceDebrisMotion(OBJECT &effect, float frames)
{
    constexpr float Acceleration = 3.f, TurnPerSpeed = 5.f;
    const float travel = frames * (effect.Velocity + Acceleration * (frames - 1.f) * 0.5f);
    const float turn =
        TurnPerSpeed * frames * (effect.Velocity + Acceleration * (frames + 1.f) * 0.5f);
    VectorAddScaled(effect.Position, effect.Direction, effect.Position, travel);
    effect.Velocity += Acceleration * frames;
    effect.Angle[0] += turn;
}

void AdvanceBouncingFire(OBJECT &effect, const World &world, float frames)
{
    constexpr float Gravity = 2.f, LaunchSpeed = 10.f, ContactLift = 10.f;
    float matrix[3][4];
    AngleMatrix(effect.Angle, matrix);
    while (frames > 0.f)
    {
        vec3_t velocity;
        VectorRotate(effect.Direction, matrix, velocity);
        float vertical = effect.Gravity + velocity[2];
        vec3_t motion{velocity[0], velocity[1], vertical + Gravity * 0.5f};
        const auto contact = world.FirstTerrainContact(effect.Position, motion, -Gravity, frames);
        const float step = contact ? contact->frames : frames;
        effect.Position[0] += velocity[0] * step;
        effect.Position[1] += velocity[1] * step;
        Core::Time::Advance(effect.Position[2], vertical, -Gravity, step);
        effect.Gravity = vertical - velocity[2];
        if (!contact)
            return;
        effect.Position[2] = contact->height + ContactLift;
        effect.Gravity = LaunchSpeed;
        effect.Direction[1] *= 0.5f;
        effect.Scale *= 1.1f;
        frames -= step;
    }
}

} // namespace

void SessionGameplayUnit::AdvanceWaterDebris(OBJECT &object, float frames)
{
    if (frames <= 0.f)
        return;
    constexpr float waterHeight = 350.f, acceleration = 3.f;
    float impact = frames + 1.f;
    if (object.PKKey == 0 && object.Position[2] <= waterHeight)
        impact = 0.f;
    else if (object.PKKey == 0 && object.Direction[2] < 0.f)
    {
        const float travel = (waterHeight - object.Position[2]) / object.Direction[2];
        const float velocity = object.Velocity - acceleration * 0.5f;
        impact = 2.f * travel /
                 (std::sqrt(velocity * velocity + 2.f * acceleration * travel) + velocity);
    }
    if (impact > std::min(frames, object.LifeTime))
    {
        AdvanceDebrisMotion(object, frames);
        return;
    }
    AdvanceDebrisMotion(object, impact);
    object.MotionTrace.Advance(impact, object.Position);
    object.PKKey = 1;
    auto birth = EmissionTime(frames - impact);
    AddWaterWave(static_cast<int>(object.Position[0] / TERRAIN_SCALE),
                 static_cast<int>(object.Position[1] / TERRAIN_SCALE), 2, -1000);
    AdvanceDebrisMotion(object, frames - impact);
    object.MotionTrace.Advance(frames - impact, object.Position);
}

void GameLogic::Effects::AdvanceBouncingDebris(OBJECT &effect, const World &world, float frames,
                                               DebrisMotion kind)
{
    constexpr float SteeringInterval = 1.f / 16.f;
    const bool ice = kind == DebrisMotion::IceStone;
    const bool maya = kind == DebrisMotion::MayaStone || ice;
    const bool shell = kind == DebrisMotion::SkinShell;
    const bool postGravity = kind == DebrisMotion::Wall;
    const bool heavy = kind == DebrisMotion::Heavy || maya || shell || postGravity;
    const bool gate = effect.Type == MODEL_GATE || effect.Type == MODEL_GATE + 1 ||
                      effect.Type == MODEL_GATE_PART1 || effect.Type == MODEL_GATE_PART2 ||
                      effect.Type == MODEL_GATE_PART3;
    const bool snowman = kind == DebrisMotion::Snowman;
    const bool tallBounce =
        kind == DebrisMotion::NewYear || kind == DebrisMotion::MoonHarvest || snowman;
    const float gravityScale = heavy ? 1.f : tallBounce ? 1.5f : 0.5f;
    const bool wall = effect.Type == MODEL_WALL_PART1 || effect.Type == MODEL_WALL_PART2;
    const float gravity = postGravity ? (wall ? 6.f : 3.f)
                          : heavy     ? (gate ? 4.f : 3.f)
                                      : 1.5f * gravityScale;
    const float positionLag = postGravity ? -gravity : 0.f;
    const float restitution = heavy ? (gate || maya ? 0.5f : 0.2f) : 0.3f;
    const float impactAge = maya ? 4.f : heavy ? 5.f : 2.f;
    const float airSpin = snowman ? 0.f : maya ? -32.f : heavy ? -16.f : tallBounce ? 10.f : 20.f;
    const float groundSpin = heavy ? -128.f : airSpin;
    const int spinAxis =
        heavy || snowman || kind == DebrisMotion::MoonHarvest ? 0 : effect.m_iAnimation;
    const float spinScale = heavy ? effect.Scale : 1.f;
    const float worldXY = tallBounce ? 2.2f : 1.f;
    const float worldZ = kind == DebrisMotion::MoonHarvest ? 0.f : 1.f;
    const float clearance = kind == DebrisMotion::Christmas ? 3.f : 0.f;
    const float damping =
        heavy ? (shell || (!maya && !postGravity && effect.SubType == 1) ? 0.9f * 1.1f : 0.9f)
              : 1.f;
    while (frames > 0.f)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            // Hold a midpoint direction through each local steering interval, including render-frame splits.
            vec3_t direction, angle, rotated;
            VectorScale(effect.Direction,
                        Core::Time::DampedDistance(damping, SteeringInterval) / SteeringInterval,
                        direction);
            VectorCopy(effect.Angle, angle);
            angle[spinAxis] +=
                spinScale * (effect.EffectResting ? groundSpin : airSpin) * SteeringInterval * 0.5f;
            float matrix[3][4];
            AngleMatrix(angle, matrix);
            VectorRotate(direction, matrix, rotated);
            if (ice && effect.SubType == 13)
            {
                VectorScale(rotated, 1.f + damping, effect.EffectMotionVelocity);
            }
            else
            {
                Vector(direction[0] * worldXY, direction[1] * worldXY, direction[2] * worldZ,
                       effect.EffectMotionVelocity);
                VectorAddScaled(effect.EffectMotionVelocity, rotated, effect.EffectMotionVelocity,
                                damping);
            }
            effect.EffectMotionFrames = SteeringInterval;
        }
        const float duration = (std::min)(frames, effect.EffectMotionFrames);
        if (effect.EffectResting)
        {
            VectorAddScaled(effect.Position, effect.EffectMotionVelocity, effect.Position,
                            duration);
            effect.Position[2] =
                world.SampleTerrainHeight(effect.Position[0], effect.Position[1]) + clearance;
            effect.Gravity =
                (-effect.EffectMotionVelocity[2] - gravity * 0.5f - positionLag) / gravityScale;
            effect.Angle[spinAxis] += spinScale * groundSpin * duration;
            effect.LifeTime -= impactAge * duration;
            VectorScale(effect.Direction, std::pow(damping, duration), effect.Direction);
            effect.EffectMotionFrames -= duration;
            frames -= duration;
            continue;
        }
        float vertical =
            gravityScale * effect.Gravity + effect.EffectMotionVelocity[2] + positionLag;
        vec3_t motion{effect.EffectMotionVelocity[0], effect.EffectMotionVelocity[1],
                      vertical + gravity * 0.5f};
        const auto contact =
            world.FirstTerrainContact(effect.Position, motion, -gravity, duration, clearance);
        const float step = contact ? contact->frames : duration;
        effect.Position[0] += motion[0] * step;
        effect.Position[1] += motion[1] * step;
        Core::Time::Advance(effect.Position[2], vertical, -gravity, step);
        effect.Gravity = (vertical - effect.EffectMotionVelocity[2] - positionLag) / gravityScale;
        effect.Angle[spinAxis] += spinScale * airSpin * step;
        VectorScale(effect.Direction, std::pow(damping, step), effect.Direction);
        effect.EffectMotionFrames -= step;
        frames -= step;
        if (!contact)
            continue;
        effect.Position[2] = contact->height;
        const float incoming = vertical + gravity * 0.5f - contact->surfaceVelocity;
        const float rebound = (std::max)(0.f, -incoming * restitution);
        effect.Gravity = (rebound + contact->surfaceVelocity - gravity * 0.5f -
                          effect.EffectMotionVelocity[2] - positionLag) /
                         gravityScale;
        effect.Angle[spinAxis] += spinScale * (groundSpin - airSpin);
        effect.LifeTime -= impactAge;
        const float resolution =
            std::nextafter(contact->height, std::numeric_limits<float>::infinity()) -
            contact->height;
        effect.EffectResting = rebound * rebound / (2.f * gravity) <= resolution;
    }
}

namespace
{
float EffectAdvanceRate(const OBJECT &effect)
{
    // A 50% recursive repeat originally averaged two updates per authored tick.
    const bool repeated = (effect.Type == BITMAP_LIGHT && effect.SubType == 0) ||
                          (effect.Type == MODEL_FIRE && effect.SubType == 3);
    return repeated ? 2.f : 1.f;
}
} // namespace

void SessionGameplayUnit::SlideUmbrellaOnGround(OBJECT &effect, float frames)
{
    AdvanceUmbrellaFlight(effect, frames);
    effect.Position[2] = RequestTerrainHeight(effect.Position[0], effect.Position[1]);
    effect.Gravity = -0.75f; // Zero derivative of the authored position-before-velocity integral.
    effect.LifeTime -= 2.f * frames;
}

void SessionGameplayUnit::AdvanceUmbrellaGold(OBJECT &effect, float frames)
{
    constexpr float VerticalAcceleration = 2.25f, Restitution = 0.3f;
    while (frames > 0.f)
    {
        const float ground = RequestTerrainHeight(effect.Position[0], effect.Position[1]);
        const float velocity = 1.5f * effect.Gravity + VerticalAcceleration * 0.5f;
        const float heightResolution =
            std::nextafter(ground, std::numeric_limits<float>::infinity()) - ground;
        if (effect.Position[2] <= ground && velocity >= 0.f &&
            velocity * velocity / (2.f * VerticalAcceleration) <= heightResolution)
        {
            SlideUmbrellaOnGround(effect, frames);
            return;
        }
        vec3_t motion{effect.Direction[0] * 2.2f, effect.Direction[1] * 2.2f, velocity};
        const auto contact = sessionKeeper_.WorldUnit()->FirstTerrainContact(
            effect.Position, motion, -VerticalAcceleration, frames);
        if (!contact)
        {
            AdvanceUmbrellaFlight(effect, frames);
            return;
        }
        AdvanceUmbrellaFlight(effect, contact->frames);
        effect.Position[2] = contact->height;
        // Reflect speed relative to the moving ground height, so slopes do not add energy.
        const float incoming =
            1.5f * effect.Gravity + VerticalAcceleration * 0.5f - contact->surfaceVelocity;
        const float rebound = (std::max)(0.f, -incoming * Restitution);
        effect.Gravity = (rebound + contact->surfaceVelocity - VerticalAcceleration * 0.5f) / 1.5f;
        effect.LifeTime -= 2.f;
        frames -= contact->frames;
        const float contactResolution =
            std::nextafter(contact->height, std::numeric_limits<float>::infinity()) -
            contact->height;
        if (rebound * rebound / (2.f * VerticalAcceleration) <= contactResolution)
        {
            SlideUmbrellaOnGround(effect, frames);
            return;
        }
    }
}

void SessionGameplayUnit::MoveParticle(OBJECT *o, int Turn)
{
    AdvanceEffectPosition(o, Turn, FPS_ANIMATION_FACTOR);
}

void SessionGameplayUnit::MoveParticle(OBJECT *o, vec3_t angle)
{
    float Matrix[3][4];
    vec3_t Angle;
    vec3_t Position;
    VectorCopy(angle, Angle);
    AngleMatrix(Angle, Matrix);
    VectorRotate(o->Direction, Matrix, Position);
    VectorAddScaled(o->Position, Position, o->Position, FPS_ANIMATION_FACTOR);
}

bool SessionGameplayUnit::MoveJump(OBJECT *o)
{
    const float height = RequestTerrainHeight(o->Position[0], o->Position[1]);
    const auto bounce = GameLogic::Effects::Motion::AdvanceJump(
        o->Position, o->Gravity, o->Direction, o->Angle, height, FPS_ANIMATION_FACTOR);
    return bounce.impacts > 0 || bounce.restingFrames > 0.f;
}

void SessionGameplayUnit::CreateBomb(vec3_t p, bool Exp, int SubType)
{
    vec3_t Position, Light;
    VectorCopy(p, Position);
    Position[2] += 80.f;
    vec3_t Angle;

    Vector(1.f, 1.f, 1.f, Light);

    if (SubType != 5)
    {
        for (int j = 0; j < 20; j++)
        {
            Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, (float)(WorldRandom() % 30), Angle);
            if (SubType == 2)
            {
                Vector(0.7f, 0.7f, 1.f, Light);
                CreateParticle(BITMAP_SPARK, Position, Angle, Light, 8);
            }
            else if (SubType == 3)
            {
                Vector(1.0f, 0.7f, 0.4f, Light);
                CreateParticle(BITMAP_SPARK, Position, Angle, Light, 8);
            }
            else if (SubType == 4)
            {
                if (j == 5)
                    break;
                Vector(1.0f, 0.5f, 0.3f, Light);
                CreateParticle(BITMAP_SPARK, Position, Angle, Light, 8);
            }
            else if (SubType == 6)
            {
                Vector(0.3f, 0.3f, 0.3f, Light);
                CreateParticle(BITMAP_SPARK, Position, Angle, Light, 10);
            }
            else
            {
                CreateParticle(BITMAP_SPARK, Position, Angle, Light, 2);
            }
        }
    }

    Vector(0.7f, 0.7f, 0.7f, Light);

    if (Exp)
    {
        if (SubType == 2 || SubType == 5 || SubType == 6)
        {
            Vector(0.3f, 0.6, 1.f, Light);
            if (SubType == 6)
            {
                Vector(0.3f, 0.3f, 0.3f, Light);
            }
            CreateParticle(BITMAP_EXPLOTION_MONO, Position, Angle, Light);
        }
        else if (SubType == 3)
        {
            Vector(1.0f, 0.6, 0.3f, Light);
            CreateParticle(BITMAP_EXPLOTION_MONO, Position, Angle, Light);
        }
        else if (SubType == 4)
        {
            Vector(1.0f, 0.4, 0.2f, Light);
            CreateParticle(BITMAP_EXPLOTION_MONO, Position, Angle, Light, 0, 0.5f);
            Vector(1.0f, 0.6, 0.2f, Light);
            CreateParticle(BITMAP_EXPLOTION_MONO, Position, Angle, Light, 0, 0.3f);
        }
        else
        {
            CreateParticle(BITMAP_EXPLOTION, Position, Angle, Light);
        }
    }
}

void SessionGameplayUnit::CreateBomb2(vec3_t p, bool Exp, int SubType, float Scale)
{
    vec3_t Position, Light;
    VectorCopy(p, Position);
    Position[2] += 30.f;
    vec3_t Angle;
    Vector(1.f, 1.f, 1.f, Light);
    for (int j = 0; j < 20; j++)
    {
        Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, (float)(WorldRandom() % 30), Angle);
        CreateParticle(BITMAP_SPARK, Position, Angle, Light, 2);
    }
    Vector(1.f, 1.f, 1.f, Light);
    Vector(0.f, 0.f, 0.f, Angle);
    if (Exp)
    {
        CreateParticle(BITMAP_EXPLOTION_MONO, Position, Angle, Light, 0, 4.f + Scale);
    }
    else
    {
        CreateParticle(BITMAP_EXPLOTION + 1, Position, Angle, Light, 0, 4.f);
    }
}

void SessionGameplayUnit::CreateBomb3(vec3_t vPos, int iSubType, float fScale)
{
    vec3_t vBombPos, vAngle, vLight;
    VectorCopy(vPos, vBombPos);
    Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, (float)(WorldRandom() % 30), vAngle);
    Vector(1.0f, 1.0f, 1.0f, vLight);
    for (int i = 0; i < 2; ++i)
    {
        if (iSubType == 2)
        {
            if (WorldRandom() % 10 == 0)
                break;
            Vector(vPos[0] + WorldRandom() % 150 - 75, vPos[1] + WorldRandom() % 150 - 75,
                   vPos[2] + WorldRandom() % 130 + 30, vBombPos);
        }
        else if (iSubType == 1)
        {
            if (i == 1)
                break;
            else if (WorldRandom() % 5 == 0)
                break;
            Vector(vPos[0] + WorldRandom() % 80 - 40, vPos[1] + WorldRandom() % 80 - 40,
                   vPos[2] + WorldRandom() % 120 + 30, vBombPos);
        }
        else if (iSubType == 3)
        {
            Vector(vPos[0] + WorldRandom() % 90 - 40, vPos[1] + WorldRandom() % 90 - 40,
                   vPos[2] + WorldRandom() % 100 + 70, vBombPos);
        }
        else
        {
            if (i == 1)
                break;
            else if (WorldRandom() % 3 != 0)
                break;
            Vector(vPos[0] + WorldRandom() % 30 - 15, vPos[1] + WorldRandom() % 30 - 15,
                   vPos[2] + WorldRandom() % 80 + 30, vBombPos);
        }
        if (fScale != 1.0f)
            CreateParticle(BITMAP_SUMMON_SAHAMUTT_EXPLOSION, vBombPos, vAngle, vLight, 0, fScale);
        else
            CreateParticle(BITMAP_SUMMON_SAHAMUTT_EXPLOSION, vBombPos, vAngle, vLight, 0,
                           0.15f * (WorldRandom() % 10));
    }

    Vector(1.0f, 0.5f, 0.2f, vLight);
    if (fScale != 1.0f)
        CreateParticle(BITMAP_MAGIC + 1, vBombPos, vAngle, vLight, 0, 2.2f * fScale);
    else
        CreateParticle(BITMAP_MAGIC + 1, vBombPos, vAngle, vLight, 0, 1.0f);
    Vector(1.0f, 0.6f, 0.2f, vLight);

    for (int i = 0; i < 3; ++i)
    {
        if (fScale != 1.0f)
            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, vBombPos, vAngle, vLight, 14, NULL, -1,
                         0, 0, 0, 2.2f * fScale);
        else
            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, vBombPos, vAngle, vLight, 13);
    }
    Vector(1.0f, 1.0f, 1.0f, vLight);

    int nParCnt = 5;
    if (iSubType == 3)
    {
        nParCnt = 20;
    }

    for (int i = 0; i < nParCnt; ++i)
    {
        CreateParticle(BITMAP_SPARK, vBombPos, vAngle, vLight, 2);
    }
}

void SessionGameplayUnit::CreateInferno(vec3_t Position, int SubType)
{
    vec3_t p, p2, Angle, Light;
    float Matrix[3][4];
    Vector(1.f, 1.f, 1.f, Light);
    for (int j = 0; j < 8; j++)
    {
        if (SubType == 2 || SubType == 3 || SubType == 5)
        {
            Vector(0.f, -240.f, 0.f, p);
        }
        else
            Vector(0.f, -220.f, 0.f, p);
        Vector(0.f, 0.f, j * 45.f, Angle);
        AngleMatrix(Angle, Matrix);
        VectorRotate(p, Matrix, p2);
        VectorAdd(Position, p2, p2);
        CreateBomb(p2, true, SubType);
        if (SubType == 1)
        {
            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, p2, Angle, Light, 0);
            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, p2, Angle, Light, 0);
        }
        else if (SubType == 2 || SubType == 3)
        {
            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, p2, Angle, Light, 10);
            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, p2, Angle, Light, 10);
            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, p2, Angle, Light, 10);
        }
        else if (SubType == 5)
        {
            Vector(0.3f, 0.6f, 1.0f, Light);
            for (int i = 0; i < 3; ++i)
                CreateEffect(MODEL_EFFECT_BROKEN_ICE0 + WorldRandom() % 3, p2, Angle, Light, 0);
        }
        else
        {
            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, p2, Angle, Light, 1);
            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, p2, Angle, Light, 0);
        }
    }
}

void SessionGameplayUnit::CheckClientArrow(OBJECT *o)
{
    if (IsBattleCastleStart())
    {
        DWORD att = TERRAIN_ATTRIBUTE(o->Position[0], o->Position[1]);
        if ((att & TW_NOATTACKZONE) == TW_NOATTACKZONE)
        {
            o->Velocity = 0.f;
            Vector(0.f, 0.f, 0.f, o->Direction);
            o->LifeTime *= pow(1.0f / (2.f), FPS_ANIMATION_FACTOR);
            return;
        }
    }

    if (o->Type == MODEL_DARK_SCREAM)
    {
        float range = 100.f;
        short TKey = o->PKKey;

        AttackCharacterRange(Hero->CurrentSkill, o->Position, range, Hero->Object.Weapon, TKey,
                             o->AttackPoint[0]);
    }

    if (!o->Kind || o->SubType == 99)
    {
        if (CheckCharacterRange(o, 100.f, o->PKKey))
        {
            int Skill = CharacterAttribute->Skill[o->Skill];

            switch (Skill)
            {
            case AT_SKILL_PENETRATION:
            case AT_SKILL_PENETRATION_STR:
                if (o->SubType == 2)
                {
                    CreateJoint(BITMAP_FLARE, o->Position, o->Position, o->Angle, 6, o, 30.0f);
                }
                else
                {
                    RetireEffect(o);
                    CreateBomb(o->Position, true);
                }
                break;
            case AT_SKILL_ICE_ARROW:
            case AT_SKILL_ICE_ARROW_STR:
                RetireEffect(o);
                if (o->Type == MODEL_ARROW_BOMB || o->Type == MODEL_ARROW_HOLY)
                    CreateBomb(o->Position, true);
                break;

            default:
                RetireEffect(o);
                if (o->Type == MODEL_ARROW_BOMB || o->Type == MODEL_ARROW_HOLY)
                {
                    if (o->SubType == 1)
                    {
                    }
                    else
                    {
                        CreateBomb(o->Position, true);
                    }
                }
                break;
            }
        }
    }
    else
    {
        if (rand_fps_check(2))
            if (o->Owner == &Hero->Object)
            {
                float range = 100.f;
                int Skill = CharacterAttribute->Skill[o->Skill];

                if ((Skill == AT_SKILL_PENETRATION || Skill == AT_SKILL_PENETRATION_STR) &&
                    o->Type == MODEL_ARROW_BOMB)
                {
                    float dx = o->Position[0] - Hero->Object.Position[0];
                    float dy = o->Position[1] - Hero->Object.Position[1];
                    float Distance = sqrtf(dx * dx + dy * dy);
                    if (Distance > 500)
                    {
                        return;
                    }
                }

                if ((Skill == AT_SKILL_PENETRATION || Skill == AT_SKILL_PENETRATION_STR
#ifdef PJH_FIX_4_BUGFIX_7
                     || Skill == AT_SKILL_DARK_SCREAM
#endif //#ifdef PJH_FIX_4_BUGFIX_7
                     ) &&
                    o->AttackPoint[0] > 0)
                {
                    range = 200.f;
#ifdef PJH_FIX_4_BUGFIX_7
                    if (Skill == AT_SKILL_DARK_SCREAM)
                        range = 100.f;
#endif //#ifdef PJH_FIX_4_BUGFIX_7
                    o->AttackPoint[0]--;
                }
                else if (AttackCharacterRange(o->Skill, o->Position, range, o->Weapon, o->PKKey))
                {
                    switch (Skill)
                    {
#ifdef PJH_FIX_4_BUGFIX_7
                    case AT_SKILL_DARK_SCREAM: {
                        o->AttackPoint[0] = 2;
                    }
                    break;
#endif //#ifdef PJH_FIX_4_BUGFIX_7
                    case AT_SKILL_PENETRATION:
                    case AT_SKILL_PENETRATION_STR:
                        if (o->SubType == 2)
                        {
                            if (o->Type == MODEL_ARROW_HOLY && o->LifeTime > 14)
                            {
                                o->AttackPoint[0] = 5;
                            }
                            else if (o->Type == MODEL_ARROW_BOMB)
                            {
                                o->AttackPoint[0] = 5;
                            }
                            else
                            {
                                o->AttackPoint[0] = 2;
                            }
                            CreateJoint(BITMAP_FLARE, o->Position, o->Position, o->Angle, 6, o,
                                        30.0f);
                        }
                        else
                        {
                            RetireEffect(o);
                            CreateBomb(o->Position, true);
                        }
                        break;
                    case AT_SKILL_ICE_ARROW:
                    case AT_SKILL_ICE_ARROW_STR:
                        RetireEffect(o);
                        if (o->Type == MODEL_ARROW_BOMB || o->Type == MODEL_ARROW_HOLY)
                            CreateBomb(o->Position, true);
                        break;

                    default:
                        RetireEffect(o);
                        PlayBuffer(
                            static_cast<ESound>(SOUND_ATTACK_MELEE_HIT1 + 5 + WorldRandom() % 4),
                            o);
                        if (o->Type == MODEL_ARROW_BOMB || o->Type == MODEL_ARROW_HOLY)
                            CreateBomb(o->Position, true);
                        break;
                    }
                }
            }
    }
#ifdef USE_SELFCHECKCODE
    END_OF_FUNCTION(Pos_SelfCheck01);
Pos_SelfCheck01:;
#endif
}

void SessionGameplayUnit::EmitThunderBursts(OBJECT &object, float frames)
{
    if (object.SubType != 0)
        return;
    constexpr float LastBurstLife = 4.f, BurstInterval = 2.f, BaseScale = 50.f;
    for (float life =
             std::min(LastBurstLife,
                      Core::Time::ReferenceSample(object.LifeTime / BurstInterval) * BurstInterval);
         life > 0.f && Core::Time::Reaches(object.LifeTime, frames, life); life -= BurstInterval)
    {
        auto birth = EmissionTime(frames - std::max(0.f, object.LifeTime - life));
        vec3_t position;
        object.MotionTrace.Sample(WorldTime, birth.FrameFraction(), object.Position, position);
        position[0] += WorldRandom() % 64 - 32.f;
        position[1] += WorldRandom() % 64 - 32.f;
        CreateJoint(BITMAP_JOINT_THUNDER + 1, position, position, object.Angle, 5, nullptr,
                    BaseScale + WorldRandom() % 10);
    }
}

float SessionGameplayUnit::EffectLuminosity(float lifetime)
{
    constexpr float FadeTicks = 5.f;
    constexpr float FadePerTick = 0.2f;
    const float flicker = static_cast<float>(WorldRandom() % 4 + 7) * 0.1f;
    return (std::max)(0.f, flicker - (std::max)(0.f, FadeTicks - lifetime) * FadePerTick);
}

void SessionGameplayUnit::MoveEffect(OBJECT *o, int iIndex)
{
    EffectBirthStep birthStep(FPS_ANIMATION_FACTOR, o->BirthTiming);
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    const auto birthSerial = o->BirthTiming.serial;
    const float initialLife = o->LifeTime;
    const float advanceRate = EffectAdvanceRate(*o);
    const float effectFrames = FPS_ANIMATION_FACTOR * advanceRate;
    o->MotionTrace.Begin(WorldTime, effectFrames, o->Position, o->BirthTiming.FrameFraction(0.f));
    o->EffectAnimationAdvance.reset();
    o->Visible = sessionKeeper_.Renderer()->EffectVisible(*o);
    SessionRandom::PresentationScope randomOrigin(sessionKeeper_.RandomForConstruction(),
                                                  o->PresentationRandom);
    if (HasEffectFlicker(*o) && Core::Time::Periods(o->LifeTime, effectFrames, 1.f) > 0)
        o->AppearanceRandom = static_cast<unsigned short>(WorldRandom());
    vec3_t Light;
    vec3_t Angle;
    int Index;
    vec3_t p, Position;
    float Matrix[3][4];
    float Height;
    float Luminosity = EffectLuminosity(o->LifeTime);
    Vector(1.f, 1.f, 1.f, Light);

    // Registry-driven per-frame behaviour. A false return mirrors the legacy
    // cases that `return` out of the switch, skipping the shared tail below.
    if (const GameLogic::Effects::EffectDescriptor *desc = GameLogic::Effects::Lookup(o->Type);
        desc && desc->move)
    {
        if (!(effectMoveBehavior_.*desc->move)(o, iIndex, Luminosity))
        {
            if (o->BirthTiming.serial == birthSerial && !o->Live)
                EffectDestructor(o);
            return;
        }
    }
    else
        switch (o->Type)
        {
        case BITMAP_BOSS_LASER:
        case BITMAP_BOSS_LASER + 1:
        case BITMAP_BOSS_LASER + 2:
            AdvanceBossLaser(*o);
            break;

        case 9:
            AdvanceWaterDebris(*o, effectFrames);
            break;

        case MODEL_SUMMONER_EQUIP_HEAD_LAGUL: {
            OBJECT *pObject = o->Owner;
            o->Position[0] =
                pObject->Position[0] + cosf(WorldTime * 0.003f + o->Skill * 0.024f) * 60.0f;
            o->Position[1] =
                pObject->Position[1] + sinf(WorldTime * 0.003f + o->Skill * 0.024f) * 60.0f;
            o->Position[2] = pObject->Position[2] +
                             (sinf(WorldTime * 0.0010f + o->Skill * 0.024f) + 2.0f) * 80.0f - 60.0f;

            if (o->StartPosition[0] != o->Position[0] - pObject->Position[0])
            {
                float fAngle = CreateAngle(o->StartPosition[0], o->StartPosition[1],
                                           o->Position[0] - pObject->Position[0],
                                           o->Position[1] - pObject->Position[1]);
                o->Angle[2] = fAngle + 0;
            }
            VectorSubtract(o->Position, pObject->Position, o->StartPosition);

            if (o->Kind == 1)
            {
                if (o->Alpha > 0.0f)
                    o->Alpha -= (0.03f) * effectFrames;
                else
                    DeleteEffect(MODEL_SUMMONER_EQUIP_HEAD_LAGUL, o->Owner);
            }
            else
            {
                if (Hero->SafeZone || sinf(WorldTime * 0.0004f + o->Skill * 0.024f) < 0.3f)
                    o->Kind = 1;
                if (o->Alpha < 1.0f)
                    o->Alpha += (0.03f) * effectFrames;
            }

            if (pObject->Live)
                o->LifeTime = 100.f;

            for (auto birthTime : Emissions(effectFrames / 1.0))
            {
                vec3_t vLight;
                Vector(o->Alpha * 0.7f, o->Alpha * 0.3f, o->Alpha * 1.f, vLight);
                CreateParticle(BITMAP_CLUD64, o->Position, o->Angle, vLight, 10, 1.f, pObject);
            }
        }
            {
                if (o->LifeTime < 20)
                    o->BlendMeshLight -= (0.03f) * effectFrames;
                else if (o->BlendMeshLight < 0.5f)
                    o->BlendMeshLight += (0.05f) * effectFrames;

                o->BlendMeshTexCoordV += (0.05f) * effectFrames;
            }
            break;

        case BITMAP_FIRE + 1:
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticleFpsChecked(BITMAP_FIRE + 1, o->Position, o->Angle, Light, 1);
            Vector(Luminosity * 1.f, Luminosity * 0.6f, Luminosity * 0.3f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
            break;
        case BITMAP_MAGIC + 1:
        case BITMAP_MAGIC + 2:
            if (o->SubType >= 1 && o->SubType != 4 && o->SubType != 6 && o->SubType != 7 &&
                o->SubType != 8 && o->SubType != 9 && o->SubType != 10 && o->SubType != 11 &&
                o->SubType != 12 && o->SubType != 13)
            {
                CreateHealing(o);
            }
#ifdef ENABLE_POTION_EFFECT
            if (o->SubType == 15)
            {
                if (Core::Time::Reaches(o->LifeTime, effectFrames, 1))
                {
                    if ((o->State & STATE_HP_RECOVERY) == STATE_HP_RECOVERY)
                        o->State ^= STATE_HP_RECOVERY;
                }
            }
#endif // ENABLE_POTION_EFFECT
            else if (o->SubType == 6)
            {
                o->BlendMeshLight = (WorldRandom() % 10) * 0.1f;
                EmitMagicBoneCloud(*o);
                o->Owner->Alpha = o->LifeTime / 80.f;
            }
            else if (o->SubType == 9)
            {
                o->Scale += (0.3f) * effectFrames;
                o->Light[0] *= pow(1.0f / (1.1f), effectFrames);
                o->Light[1] *= pow(1.0f / (1.1f), effectFrames);
                o->Light[2] *= pow(1.0f / (1.1f), effectFrames);
            }
            else if (o->Type == BITMAP_MAGIC + 1 && o->SubType == 13)
            {
                const float fade = pow(1.f / 1.05f, effectFrames);
                VectorScale(o->Light, fade, o->Light);
            }
            break;

        case BITMAP_SHINY + 6:
            switch (o->SubType)
            {
            case 0:
                EmitShinyModelCloud(*o);
                break;
            case 1:
            case 2:
                if (o->Owner == nullptr || !o->Owner->Live)
                    RetireEffect(o);
                else
                {
                    o->LifeTime = 100.f;
                    EmitShinyModelCloud(*o);
                }
                break;
            case 3: {
                vec3_t Position, P, dp;
                vec3_t vFirePosition;

                float Matrix[3][4];
                int iNumBones = Models[o->Owner->Type].NumBones;
                Models[o->Owner->Type].TransformByObjectBone(vFirePosition, o->Owner,
                                                             WorldRandom() % iNumBones);
                Vector(0.f, -20.f, 0.f, P);
                AngleMatrix(o->Owner->Angle, Matrix);
                VectorRotate(P, Matrix, dp);
                VectorAdd(dp, vFirePosition, Position);
                CreateSprite(BITMAP_SHINY + 6, Position, 2.0f, o->Light, o, 0, 1);
            }
            break;
            }
            break;

        case BITMAP_SPARK + 2:
            if (o->Owner == NULL || o->Owner->Live == false)
                RetireEffect(o);
            else
            {
                o->LifeTime = 100;

                for (auto birthTime : Emissions(effectFrames / 60.0))
                    CreateParticle(o->Type, o->Position, o->Angle, o->Light, o->SubType, 0.5f,
                                   o->Owner);
            }
            break;

        case BITMAP_SPARK + 1: {
            constexpr int Layers = 18;
            constexpr float ParticleScale = 6.f, LayerHeight = ParticleScale * 4.f;
            const float active = std::min(FPS_ANIMATION_FACTOR, std::max(0.f, o->LifeTime));
            for (auto birth : Emissions(active, active))
            {
                vec3_t position, light;
                VectorCopy(o->Position, position);
                const float luminosity =
                    (o->LifeTime - FPS_ANIMATION_FACTOR + birth.RemainingFrames()) * 0.1f;
                Vector(luminosity, luminosity, luminosity, light);
                for (int layer = 0; layer < Layers; ++layer)
                {
                    position[2] += LayerHeight;
                    CreateParticle(BITMAP_SPARK + 1, position, o->Angle, light, 1,
                                   layer == 0 ? ParticleScale * 2.f : ParticleScale);
                }
            }
            break;
        }
        case BITMAP_ENERGY:
            if (o->SubType == 0)
            {
                Luminosity = o->LifeTime * 0.2f;
                Vector(Luminosity, Luminosity, Luminosity, Light);
                float matrix[3][4];
                vec3_t velocity;
                AngleMatrix(o->Angle, matrix);
                VectorRotate(o->Direction, matrix, velocity);
                const float active = std::min(FPS_ANIMATION_FACTOR, std::max(0.f, o->LifeTime));
                for (auto birth : Emissions(active, active))
                {
                    const float elapsed = FPS_ANIMATION_FACTOR - birth.RemainingFrames();
                    vec3_t position, light;
                    VectorAddScaled(o->Position, velocity, position, elapsed);
                    const float luminosity = (o->LifeTime - elapsed) * 0.2f;
                    Vector(luminosity, luminosity, luminosity, light);
                    CreateParticle(BITMAP_ENERGY, position, o->Angle, light);
                    CreateParticle(BITMAP_SPARK + 1, position, o->Angle, light, 0, 4.f);
                }
                Vector(Luminosity * 0.2f, Luminosity * 0.4f, Luminosity * 1.f, Light);
                AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
                CheckTargetRange(o);
            }
            break;
        case MODEL_LIGHTNING_ORB: {
            float fRot = (WorldTime * 0.0006f) * 360.0f;
            vec3_t vLight;

            if (o->SubType == 0)
            {
                // shiny
                Vector(0.1f, 0.7f, 1.5f, vLight);
                CreateSprite(BITMAP_SHINY + 1, o->Position, 4.0f, vLight, o, fRot);
                CreateSprite(BITMAP_SHINY + 1, o->Position, 3.0f, vLight, o, -fRot);
                // magic_ground
                Vector(0.1f, 0.1f, 1.5f, vLight);
                CreateSprite(BITMAP_MAGIC, o->Position, 1.0f, vLight, o, fRot);
                CreateSprite(BITMAP_MAGIC, o->Position, 0.5f, vLight, o, -fRot);
                // pin_light
                Vector(0.5f, 0.5f, 1.5f, vLight);
                CreateSprite(BITMAP_PIN_LIGHT, o->Position, 2.0f, vLight, o,
                             (float)(WorldRandom() % 360));
                CreateSprite(BITMAP_PIN_LIGHT, o->Position, 2.0f, vLight, o,
                             (float)(WorldRandom() % 360));

                EmitLightningOrbParticles(*o);

                CheckTargetRange(o);
            }
            else if (o->SubType == 1)
            {
                vec3_t vLight;
                Vector(1.0f, 1.0f, 1.0f, vLight);
                if (o->LifeTime >= 5)
                {
                    Vector(0.1f, 0.5f, 1.5f, vLight);
                    CreateSprite(BITMAP_SHINY + 5, o->Position, 3.0f, vLight, o, fRot);
                    CreateSprite(BITMAP_SHINY + 5, o->Position, 2.0f, vLight, o, -fRot);
                    // pin_light
                    Vector(o->Light[0] * 0.3f, o->Light[1] * 0.3f, o->Light[2] * 1.0f, vLight);
                    CreateSprite(BITMAP_PIN_LIGHT, o->Position, 4.0f, vLight, o,
                                 (float)(WorldRandom() % 360));
                    CreateSprite(BITMAP_PIN_LIGHT, o->Position, 4.0f, vLight, o,
                                 (float)(WorldRandom() % 360));
                    // thunder
                    CreateSprite(BITMAP_ENERGY, o->Position, 4.0f, o->Light, o, fRot);
                }

                EmitLightningOrbParticles(*o);

                o->Light[0] *= pow(1.0f / (1.08f), effectFrames);
                o->Light[1] *= pow(1.0f / (1.08f), effectFrames);
                o->Light[2] *= pow(1.0f / (1.08f), effectFrames);
            }
        }
        break;
            // ChainLighting

        case BITMAP_LIGHTNING + 1:
            if (o->SubType == 0)
            {
                Luminosity = o->LifeTime * 0.2f;
                Vector(Luminosity * 0.2f, Luminosity * 0.5f, Luminosity * 1.f, Light);
                AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
            }
            else if (o->SubType == 1)
            {
                o->Alpha += (0.01f) * effectFrames;
                if (o->Alpha >= 1.0f)
                {
                    o->Alpha = 1.0f;
                }
            }
            break;
        case BITMAP_LIGHT:
            if (o->SubType == 0)
            {
                VectorAddScaled(o->Position, o->Direction, o->Position, effectFrames);
                //o->Direction[2] -= .35f;
                o->Direction[2] -= .01f * effectFrames;
                //VectorScale(o->Light, std::pow(0.9f, effectFrames), o->Light);
                for (auto birthTime : Emissions(effectFrames))
                    CreateParticle(BITMAP_LIGHT, o->Position, o->Angle, o->Light, 1, o->Scale);
                if (o->Direction[2] < -2.f)
                {
                    o->LifeTime = 0;
                }
            }
            else if (o->SubType == 1)
            {
                if (o->Owner != NULL && o->Owner->Live == true &&
                    g_isCharacterBuff(o->Owner, eBuff_Life))
                    o->LifeTime = 10;
                else
                    o->LifeTime = 0;

                if (g_isCharacterBuff(o->Owner, eBuff_Cloaking))
                    break;

                for (auto birthTime : Emissions(effectFrames))
                {
                    Vector(1.f, 0.5f, 0.1f, Light);
                    Vector(0.f, 0.f, 0.f, Angle);

                    Index = WorldRandom() % 7;
                    Luminosity = g_byUpperBoneLocation[Index];
                    CreateParticle(BITMAP_LIGHT, o->Position, Angle, Light, 4, Luminosity,
                                   o->Owner);

                    Luminosity = g_byUpperBoneLocation[6 - Index];
                    CreateParticle(BITMAP_LIGHT, o->Position, Angle, Light, 4, Luminosity,
                                   o->Owner);
                }
            }
            else if (o->SubType == 2)
            {
                if (o->Owner != NULL && o->Owner->Live == true &&
                    g_isCharacterBuff(o->Owner, eBuff_AddAG))
                    o->LifeTime = 10;
                else
                    o->LifeTime = 0;

                if (g_isCharacterBuff(o->Owner, eBuff_Cloaking))
                    break;

                o->Velocity += effectFrames;

                constexpr float refreshPeriod = 50.f;
                const float previous = o->Velocity - effectFrames;
                for (float event = (std::floor(previous / refreshPeriod) + 1.f) * refreshPeriod;
                     event <= o->Velocity; event += refreshPeriod)
                {
                    auto birth = EmissionTime(o->Velocity - event);
                    if (o->Owner != NULL)
                    {
                        vec3_t Position;
                        o->Angle[2] += 50.f;
                        o->Owner->MotionTrace.Sample(WorldTime, birth.FrameFraction(),
                                                     o->Owner->Position, Position);

                        o->Position[0] = Position[0] + sinf(o->Angle[2] * 0.1f) * 80.f;
                        o->Position[1] = Position[1] + cosf(o->Angle[2] * 0.1f) * 80.f;
                        o->Position[2] = Position[2] + 50;

                        CreateJoint(BITMAP_JOINT_HEALING, o->Position, o->Position, o->Angle, 9,
                                    o->Owner, 15.f);
                    }
                }
            }
            else if (o->SubType == 3) // Ancient blur
            {
                o->Velocity += effectFrames;
                constexpr float refreshPeriod = 15.f;
                for (float life =
                         Core::Time::ReferenceSample(o->LifeTime / refreshPeriod) * refreshPeriod;
                     life > 0.f && Core::Time::Reaches(o->LifeTime, effectFrames, life);
                     life -= refreshPeriod)
                {
                    auto birth = EmissionTime(effectFrames - std::max(0.f, o->LifeTime - life));
                    if (o->Owner != NULL)
                    {
                        vec3_t Position;
                        o->Angle[2] += 180;
                        o->Owner->MotionTrace.Sample(WorldTime, birth.FrameFraction(),
                                                     o->Owner->Position, Position);

                        o->Position[0] = Position[0] + sinf(o->Angle[2] * 0.1f) * 50.f;
                        o->Position[1] = Position[1] + cosf(o->Angle[2] * 0.1f) * 50.f;
                        o->Position[2] = Position[2] + 20;

                        CreateJoint(BITMAP_JOINT_HEALING, o->Position, o->Position, o->Angle, 9,
                                    o->Owner, 10.f);
                    }
                }
            }
            break;
        case MODEL_BIG_METEO1:
        case MODEL_BIG_METEO2:
        case MODEL_BIG_METEO3:
            Index = TERRAIN_INDEX_REPEAT((int)o->Position[0] / 100, (int)o->Position[1] / 100);
            if ((TerrainWall[Index] & TW_NOGROUND) != TW_NOGROUND)
            {
                Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
                if (o->Position[2] < Height)
                {
                    o->Position[2] = Height;
                    vec3_t position, angle, light;
                    VectorCopy(o->Position, position);
                    VectorCopy(o->Angle, angle);
                    VectorCopy(o->Light, light);
                    auto birth = EmissionTime(effectFrames);
                    RetireEffect(o);
                    EarthQuake = (float)(WorldRandom() % 8 - 8) * 0.1f;
                    for (int j = 0; j < 5; j++)
                    {
                        CreateEffect(MODEL_METEO1 + WorldRandom() % 2, position, angle, light, 0);
                    }
                    return;
                }
            }
            VectorAddScaled(o->Position, o->Direction, o->Position, effectFrames);
            o->Angle[0] += (10.f / o->Scale) * effectFrames;
            for (auto birth : Emissions(effectFrames))
            {
                vec3_t position, angle;
                VectorAddScaled(o->Position, o->Direction, position, -birth.RemainingFrames());
                VectorCopy(o->Angle, angle);
                angle[0] -= (10.f / o->Scale) * birth.RemainingFrames();
                CreateParticle(BITMAP_SMOKE, position, angle, o->Light, 3);
            }
            //CreateParticle(BITMAP_ENERGY,o->Position,o->Angle,Light);
            //CreateParticle(BITMAP_SPARK+1,o->Position,o->Angle,Light,0,4.f);
            return;
        case MODEL_SKILL_FURY_STRIKE + 1:
            o->BlendMeshLight = (o->LifeTime * 0.1f) / 3.f;
            if (o->LifeTime < 10)
            {
                o->Position[2] -= (0.5f) * effectFrames;
            }
            if (o->LifeTime > 15 && Core::Time::Periods(o->LifeTime, effectFrames, 3.f) > 0)
            {
                EarthQuake = (float)(WorldRandom() % 8 - 4) * 0.1f;
            }
            break;
        case MODEL_SKILL_FURY_STRIKE + 3:
            o->BlendMeshLight = (o->LifeTime * 0.1f) / 10.f;
            if (o->LifeTime < 13)
            {
                o->Position[2] -= (0.5f) * effectFrames;
            }
            break;
        case MODEL_SKILL_FURY_STRIKE + 2:
            if (o->Scale > 50)
                o->LifeTime = 0;
            if (o->LifeTime >= 10)
                o->BlendMeshLight = ((20 - o->LifeTime) * 0.1f);
            else
            {
                if (o->LifeTime < 5)
                {
                    o->Position[2] -= (0.5f) * effectFrames;
                }
                o->BlendMeshLight = (o->LifeTime * 0.1f);
            }

            for (float sample = (std::min)(9.f, Core::Time::ReferenceSample(o->LifeTime));
                 sample >= 5.f && Core::Time::Reaches(o->LifeTime, effectFrames, sample);
                 sample -= 1.f)
            {
                if (!Random.FpsCheck(10, 1.f))
                    continue;
                auto birthTime =
                    EmissionTime(FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - sample));

                vec3_t p, Position;
                vec3_t Angle;
                float Matrix[3][4];
                Vector(0.f, (float)(WorldRandom() % 150), 0.f, p);
                Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);
                AngleMatrix(Angle, Matrix);
                VectorRotate(p, Matrix, Position);
                VectorAdd(Position, o->Position, Position);

                CreateEffect(MODEL_STONE1 + WorldRandom() % 2, Position, o->Angle, o->Light, 0);
            }

            o->BlendMeshTexCoordU = -(float)(int)o->LifeTime * 0.01f;

            if (o->Owner != NULL)
                if (o->Owner->Type == MODEL_WEREWOLF_HERO)
                {
                    Vector(Luminosity * 0.0f, Luminosity * 0.0f, Luminosity * 1.0f, Light);
                }
                else
                    Vector(Luminosity * 1.0f, Luminosity * 0.0f, Luminosity * 0.0f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 1, PrimaryTerrainLight);
            break;
        case MODEL_SKILL_FURY_STRIKE + 4:
            o->BlendMeshLight = (o->LifeTime * 0.1f) / 3.f;
            if (o->LifeTime < 10)
            {
                o->Position[2] -= (0.5f) * effectFrames;
            }
            break;
        case MODEL_SKILL_FURY_STRIKE + 6:
            o->BlendMeshLight = (o->LifeTime * 0.1f) / 10.f;
            if (o->LifeTime < 13)
            {
                o->Position[2] -= (0.5f) * effectFrames;
            }
            break;
        case MODEL_SKILL_FURY_STRIKE + 5:
            if (o->LifeTime >= 30)
                o->BlendMeshLight = ((40 - o->LifeTime) * 0.1f);
            else
            {
                if (o->LifeTime < 15)
                {
                    o->Position[2] -= (0.5f) * effectFrames;
                }

                o->BlendMeshLight = (o->LifeTime * 0.1f);
            }

            for (float sample = (std::min)(29.f, Core::Time::ReferenceSample(o->LifeTime));
                 sample >= 5.f && Core::Time::Reaches(o->LifeTime, effectFrames, sample);
                 sample -= 1.f)
            {
                if (!Random.FpsCheck(15, 1.f))
                    continue;
                auto birthTime =
                    EmissionTime(FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - sample));

                vec3_t p, Position;
                vec3_t Angle;
                float Matrix[3][4];
                Vector(0.f, (float)(WorldRandom() % 150), 0.f, p);
                Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);
                AngleMatrix(Angle, Matrix);
                VectorRotate(p, Matrix, Position);
                VectorAdd(Position, o->Position, Position);

                CreateEffect(MODEL_STONE1 + WorldRandom() % 2, Position, o->Angle, o->Light, 0);
            }

            o->BlendMeshTexCoordU = -(float)(int)o->LifeTime * 0.01f;

            if (o->Owner != NULL)
            {
                if (o->Owner->Type == MODEL_WEREWOLF_HERO)
                {
                    Vector(Luminosity * 0.0f, Luminosity * 0.0f, Luminosity * 1.0f, Light);
                }
                else
                {
                    Vector(Luminosity * 1.0f, Luminosity * 0.0f, Luminosity * 0.0f, Light);
                }
            }
            AddTerrainLight(o->Position[0], o->Position[1], Light, 1, PrimaryTerrainLight);
            break;
        case MODEL_SKILL_FURY_STRIKE + 7:
            o->BlendMeshLight = (o->LifeTime * 0.1f) / 3.f;
            if (o->LifeTime < 10)
            {
                o->Position[2] -= (0.5f) * effectFrames;
            }
            break;
        case MODEL_SKILL_FURY_STRIKE + 8:
            if (o->LifeTime >= 30)
                o->BlendMeshLight = ((40 - o->LifeTime) * 0.1f);
            else
            {
                o->BlendMeshLight = (o->LifeTime * 0.1f);
            }
            if (o->LifeTime < 15)
            {
                o->Position[2] -= (0.5f) * effectFrames;
            }

            if (o->LifeTime >= 13 && rand_fps_check(5))
            {
                vec3_t Position;

                Position[0] = o->Position[0];
                Position[1] = o->Position[1];
                Position[2] = o->Position[2] + 10;
            }

            o->BlendMeshTexCoordU = -(float)(int)o->LifeTime * 0.01f;

            if (o->Owner != NULL)
            {
                if (o->Owner->Type == MODEL_WEREWOLF_HERO)
                {
                    Vector(Luminosity * 0.0f, Luminosity * 0.0f, Luminosity * 1.0f, Light);
                }
                else
                {
                    Vector(Luminosity * 1.0f, Luminosity * 0.0f, Luminosity * 0.0f, Light);
                }
            }

            AddTerrainLight(o->Position[0], o->Position[1], Light, 1, PrimaryTerrainLight);
            break;

        case MODEL_SNOW2:
        case MODEL_SNOW3:
        case MODEL_STONE1:
        case MODEL_STONE2:
            if (o->SubType == 5)
            {
                VectorAdd(o->Owner->Position, o->StartPosition, o->Position);
                break;
            }
            else if (o->SubType == 11)
            {
                if (o->LifeTime < 10)
                {
                    o->Alpha *= pow(0.8f, effectFrames);
                }
                break;
            }
            else if (o->SubType == 13 || o->SubType == 14)
            {
                o->HeadAngle[2] -= (o->Gravity) * effectFrames;

                o->Position[0] += (o->HeadAngle[0]) * effectFrames;
                o->Position[1] += (o->HeadAngle[1]) * effectFrames;
                o->Position[2] += (o->HeadAngle[2]) * effectFrames;

                Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
                o->Angle[0] += (0.5f * o->LifeTime) * effectFrames;
                o->Angle[1] += (0.5f * o->LifeTime) * effectFrames;
                if (o->Position[2] + o->Direction[2] <= Height)
                {
                    o->Position[2] = Height;
                    o->HeadAngle[0] *= pow(0.6f, effectFrames);
                    o->HeadAngle[1] *= pow(0.6f, effectFrames);
                    o->HeadAngle[2] += (1.0f * o->LifeTime) * effectFrames;
                    if (o->HeadAngle[2] < 0.5f)
                        o->HeadAngle[2] = 0;

                    o->Alpha -= (0.1f) * effectFrames;
                }

                // 			if (rand_fps_check(3))
                // 			{
                // 				CreateParticle(BITMAP_FIRE_CURSEDLICH, o->Position, o->Angle, o->Light, 0, 1, o);
                // 			}
                break;
            }
        case MODEL_BONE1:
        case MODEL_BONE2:
        case MODEL_BIG_STONE1:
        case MODEL_BIG_STONE2:
            if (o->SubType == 5)
            {
                if (o->Owner->Live == 0)
                    RetireEffect(o);
                VectorAdd(o->Owner->Position, o->StartPosition, o->Position);
                break;
            }
        case MODEL_EFFECT_BROKEN_ICE0:
        case MODEL_EFFECT_BROKEN_ICE1:
        case MODEL_EFFECT_BROKEN_ICE2:
        case MODEL_EFFECT_BROKEN_ICE3:
#ifdef ASG_ADD_KARUTAN_MONSTERS
        case MODEL_CONDRA_STONE:
        case MODEL_CONDRA_STONE1:
        case MODEL_CONDRA_STONE2:
        case MODEL_CONDRA_STONE3:
        case MODEL_CONDRA_STONE4:
        case MODEL_CONDRA_STONE5:
        case MODEL_NARCONDRA_STONE:
        case MODEL_NARCONDRA_STONE1:
        case MODEL_NARCONDRA_STONE2:
        case MODEL_NARCONDRA_STONE3:
#endif // ASG_ADD_KARUTAN_MONSTERS
            if (o->SubType == 0)
            {
                o->HeadAngle[2] -= (o->Gravity) * effectFrames;

                o->Position[0] += (o->HeadAngle[0]) * effectFrames;
                o->Position[1] += (o->HeadAngle[1]) * effectFrames;
                o->Position[2] += (o->HeadAngle[2]) * effectFrames;

                Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
                o->Angle[0] += (0.5f * o->LifeTime) * effectFrames;
                o->Angle[1] += (0.5f * o->LifeTime) * effectFrames;
                if (o->Position[2] + o->Direction[2] <= Height)
                {
                    o->Position[2] = Height;
                    o->HeadAngle[0] *= pow(0.6f, effectFrames);
                    o->HeadAngle[1] *= pow(0.6f, effectFrames);
                    o->HeadAngle[2] += (1.0f * o->LifeTime) * effectFrames;
                    if (o->HeadAngle[2] < 0.5f)
                        o->HeadAngle[2] = 0;

                    o->Alpha -= (0.1f) * effectFrames;
                }

                for (auto birthTime : Emissions(effectFrames / 30.0))
                    CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, o->Light);
            }
            else if (o->SubType == 1)
            {
                bool bSuccess = false;

                VectorCopy(o->HeadAngle, o->Angle);

                float fHeight = RequestTerrainHeight(o->Position[0], o->Position[1]);
                if (o->Position[2] < fHeight)
                {
                    bSuccess = true;
                }

                if (bSuccess)
                {
                    o->Position[2] = fHeight;
                    Vector(0.f, 0.f, 0.f, o->Direction);
                    vec3_t vPos;
                    Vector(o->Position[0], o->Position[1], o->Position[2] + 80.f, vPos);

                    Vector(0.0f, 0.6f, 1.f, Light);
                    Vector(0.f, 0.f, 0.f, Angle);

                    CreateEffect(MODEL_SKILL_INFERNO, o->Position, Angle, Light, 2, o);

                    Vector(0.2f, 0.4f, 0.8f, Light);
                    for (int i = 0; i < 8; ++i)
                    {
                        vPos[0] = o->Position[0] + (WorldRandom() % 80 - 40);
                        vPos[1] = o->Position[1] + (WorldRandom() % 80 - 40);
                        vPos[2] = o->Position[2] + 50;

                        CreateParticle(BITMAP_SMOKE, vPos, o->Angle, Light, 11,
                                       (float)(WorldRandom() % 40 + 60) * 0.025f);
                    }

                    for (int j = 0; j < 6; j++)
                    {
                        CreateEffect(MODEL_EFFECT_BROKEN_ICE0 + WorldRandom() % 3, vPos, o->Angle,
                                     Light, 0);
                    }

                    Vector(0.6f, 0.6f, 1.f, Light);
                    vPos[0] = o->Position[0];
                    vPos[1] = o->Position[1];
                    vPos[2] = o->Position[2] + 100;
                    CreateParticle(BITMAP_EXPLOTION_MONO, vPos, o->Angle, Light, 1, 1.5f);

                    RetireEffect(o);
                }
            }
            else if (o->SubType == 2)
            {
                o->Position[2] -= (o->Gravity) * effectFrames;

                float fHeight = RequestTerrainHeight(o->Position[0], o->Position[1]);
                if (o->Position[2] < fHeight)
                {
                    CreateEffect(o->Type, o->Position, o->Angle, o->Light, 0);

                    RetireEffect(o);
                }
            }
            break;
        case MODEL_SNOW1:
            o->Direction[2] -= (2.5f) * effectFrames;
            CheckTargetRange(o);
            break;
        case MODEL_WOOSISTONE:
            if (o->SubType == 1)
            {
                o->HeadAngle[2] -= (o->Gravity) * effectFrames;

                o->Position[0] += (o->HeadAngle[0]) * effectFrames;
                o->Position[1] += (o->HeadAngle[1]) * effectFrames;
                o->Position[2] += (o->HeadAngle[2]) * effectFrames;

                Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
                o->Angle[0] += (0.5f * o->LifeTime) * effectFrames;
                o->Angle[1] += (0.5f * o->LifeTime) * effectFrames;
                if (o->Position[2] + o->Direction[2] <= Height)
                {
                    o->Position[2] = Height;
                    o->HeadAngle[0] *= pow(0.6f, effectFrames);
                    o->HeadAngle[1] *= pow(0.6f, effectFrames);
                    o->HeadAngle[2] += (1.0f * o->LifeTime) * effectFrames;
                    if (o->HeadAngle[2] < 0.5f)
                        o->HeadAngle[2] = 0;

                    o->Alpha -= (0.1f) * effectFrames;
                }
            }
            else
            {
                o->Direction[2] -= (2.0f) * effectFrames;
                CheckTargetRange(o);
            }
            break;
        case MODEL_TOTEMGOLEM_PART1:
        case MODEL_TOTEMGOLEM_PART2:
        case MODEL_TOTEMGOLEM_PART3:
        case MODEL_TOTEMGOLEM_PART4:
        case MODEL_TOTEMGOLEM_PART5:
        case MODEL_TOTEMGOLEM_PART6:
#ifdef ASG_ADD_KARUTAN_MONSTERS
        case MODEL_CONDRA_ARM_L:
        case MODEL_CONDRA_ARM_L2:
        case MODEL_CONDRA_SHOULDER:
        case MODEL_CONDRA_ARM_R:
        case MODEL_CONDRA_ARM_R2:
        case MODEL_CONDRA_CONE_L:
        case MODEL_CONDRA_CONE_R:
        case MODEL_CONDRA_PELVIS:
        case MODEL_CONDRA_STOMACH:
        case MODEL_CONDRA_NECK:
        case MODEL_NARCONDRA_ARM_L:
        case MODEL_NARCONDRA_ARM_L2:
        case MODEL_NARCONDRA_SHOULDER_L:
        case MODEL_NARCONDRA_SHOULDER_R:
        case MODEL_NARCONDRA_ARM_R:
        case MODEL_NARCONDRA_ARM_R2:
        case MODEL_NARCONDRA_ARM_R3:
        case MODEL_NARCONDRA_CONE_1:
        case MODEL_NARCONDRA_CONE_2:
        case MODEL_NARCONDRA_CONE_3:
        case MODEL_NARCONDRA_CONE_4:
        case MODEL_NARCONDRA_CONE_5:
        case MODEL_NARCONDRA_CONE_6:
        case MODEL_NARCONDRA_PELVIS:
        case MODEL_NARCONDRA_STOMACH:
        case MODEL_NARCONDRA_NECK:
#endif // ASG_ADD_KARUTAN_MONSTERS
            o->HeadAngle[2] -= (o->Gravity) * effectFrames;
            VectorCopy(o->Light, Light);

            o->Position[0] += (o->HeadAngle[0]) * effectFrames;
            o->Position[1] += (o->HeadAngle[1]) * effectFrames;
            o->Position[2] += (o->HeadAngle[2]) * effectFrames;

            Height = RequestTerrainHeight(o->Position[0], o->Position[1]) + 20;

            if (o->Position[2] + o->Direction[2] <= Height)
            {
                o->Position[2] = Height;
                o->HeadAngle[0] *= pow(0.8f, effectFrames);
                o->HeadAngle[1] *= pow(0.8f, effectFrames);
                o->HeadAngle[2] += (0.6f * o->LifeTime) * effectFrames;
                if (o->HeadAngle[2] < 5.0f)
                    o->HeadAngle[2] = 0;

                o->Alpha -= (0.05f) * effectFrames;
            }
            else
            {
                if (o->SubType == 0)
                {
                    o->Angle[0] += (0.15f * o->LifeTime) * effectFrames;
                    o->Angle[1] += (0.15f * o->LifeTime) * effectFrames;
                }
                else
                {
                    o->Angle[0] -= (0.15f * o->LifeTime) * effectFrames;
                    o->Angle[1] -= (0.15f * o->LifeTime) * effectFrames;
                }
            }
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

            o->HeadAngle[2] -= (o->Gravity) * effectFrames;
            VectorCopy(o->Light, Light);

            o->Position[0] += (o->HeadAngle[0]) * effectFrames;
            o->Position[1] += (o->HeadAngle[1]) * effectFrames;
            o->Position[2] += (o->HeadAngle[2]) * effectFrames;

            Height = RequestTerrainHeight(o->Position[0], o->Position[1]) + 20;

            if (o->Position[2] + o->Direction[2] <= Height)
            {
                o->Position[2] = Height;
                o->HeadAngle[0] *= pow(0.5f, effectFrames);
                o->HeadAngle[1] *= pow(0.5f, effectFrames);
                o->HeadAngle[2] += (0.6f * o->LifeTime) * effectFrames;
                if (o->HeadAngle[2] < 5.0f)
                    o->HeadAngle[2] = 0;

                o->Alpha -= (0.05f) * effectFrames;
            }
            else
            {
                if (o->SubType == 0)
                {
                    o->Angle[0] += (0.15f * o->LifeTime) * effectFrames;
                    o->Angle[1] += (0.15f * o->LifeTime) * effectFrames;
                }
                else
                {
                    o->Angle[0] -= (0.15f * o->LifeTime) * effectFrames;
                    o->Angle[1] -= (0.15f * o->LifeTime) * effectFrames;
                }
            }
            break;

        case MODEL_PIERCING + 1:
            for (auto birthTime : Emissions(effectFrames / 2.0))
            {
                CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 0);
            }
            CreateParticleFpsChecked(BITMAP_FIRE, o->Position, o->Angle, Light, 5);
            CreateParticleFpsChecked(BITMAP_FIRE, o->Position, o->Angle, Light, 5);

            Vector(Luminosity * 0.6f, Luminosity * 0.8f, Luminosity * 0.8f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
            break;

        case MODEL_ARROW_GAMBLE: {
            vec3_t vLight;
            Vector(0.2f, 0.8f, 0.5f, vLight);

            o->Angle[1] += (60.f) * effectFrames;

            for (int j = 0; j < 2; j++)
            {
                Vector((float)(WorldRandom() % 32 - 16), (float)(WorldRandom() % 64 - 32),
                       (float)(WorldRandom() % 32 - 16), Position);
                VectorAdd(Position, o->Position, Position);

                CreateParticleFpsChecked(BITMAP_SPARK + 1, Position, o->Angle, vLight, 30, 1.f);
            }

            VectorCopy(o->Position, o->EyeLeft);
            CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, vLight, 24, 1.0f);

            Vector(Luminosity * 0.2f, Luminosity * 0.8f, Luminosity * 0.5f, vLight);
            AddTerrainLight(o->Position[0], o->Position[1], vLight, 2, PrimaryTerrainLight);

            CheckClientArrow(o);
        }
        break;
        case MODEL_FEATHER_FOREIGN: {
            switch (o->SubType)
            {
            case 4: {
                o->Light[0] *= pow(0.97f, effectFrames);
                o->Light[1] *= pow(0.97f, effectFrames);
                o->Light[2] *= pow(0.97f, effectFrames);

                o->Alpha *= pow(0.97f, effectFrames);

                VectorAddScaled(o->Angle, o->EyeRight, o->Angle, effectFrames);
            }
            break;
            }
        }
        break;
        case MODEL_ICE:
            //o->Position[0] += (o->Owner->Position[0]-o->Position[0])*0.3f;
            //o->Position[1] += (o->Owner->Position[1]-o->Position[1])*0.3f;
            switch (o->SubType)
            {
            case 0:
                if (o->AnimationFrame >= 5.f)
                {
                    o->Velocity = 0.f;
                    for (auto birthTime : Emissions(effectFrames / 2.0))
                    {
                        vec3_t Position;
                        Vector(o->Position[0] + (float)(WorldRandom() % 64 - 32),
                               o->Position[1] + (float)(WorldRandom() % 64 - 32),
                               o->Position[2] + (float)(WorldRandom() % 128 + 32), Position);
                        CreateParticle(BITMAP_SMOKE, Position, o->Angle, o->Light);
                    }
                    //AttackRange(o->Position,200.f,10);
                    o->Alpha -= (0.05f) * effectFrames;
                    if (o->Alpha < 0.f)
                        RetireEffect(o);
                }
                Vector(-Luminosity * 0.4f, -Luminosity * 0.3f, -Luminosity * 0.2f, Light);
                //Vector(Luminosity*0.2f,Luminosity*0.4f,Luminosity,Light);
                AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
                break;

            case 1:
            case 2:
                if (o->Owner != NULL)
                {
                    if (o->Owner->Live && g_isCharacterBuff(o->Owner, eDeBuff_Harden))
                    {
#ifdef GUILD_WAR_EVENT
                        if (o->Owner->Type == MODEL_BALL)
                            break;
#endif
                        if (o->Owner->Type == MODEL_PLAYER)
                        {
                            if ((o->Owner->CurrentAction >= PLAYER_WALK_MALE &&
                                 o->Owner->CurrentAction <= PLAYER_RUN_RIDE_WEAPON) ||
                                (o->Owner->CurrentAction >= PLAYER_FLY_RIDE &&
                                 o->Owner->CurrentAction <= PLAYER_FLY_RIDE_WEAPON) ||
                                (o->Owner->CurrentAction == PLAYER_RAGE_UNI_RUN ||
                                 o->Owner->CurrentAction == PLAYER_RAGE_UNI_RUN_ONE_RIGHT))
                            {
                                o->AnimationFrame = 0.f;
                                o->PriorAnimationFrame = 0.f;
                                o->HiddenMesh = -2;
                            }
                            else
                            {
                                o->HiddenMesh = -1;
                            }
                        }
                        else
                        {
                            if (o->Owner->CurrentAction != 2)
                            {
                                o->HiddenMesh = -1;
                            }
                            else
                            {
                                o->PriorAnimationFrame = 0.f;
                                o->AnimationFrame = 0.f;
                                o->HiddenMesh = -2;
                            }
                        }
                        VectorCopy(o->Owner->Position, o->Position);
                        if (o->AnimationFrame >= 4.f)
                            o->AnimationFrame = 4.f;
                        o->LifeTime = 10;
                    }
                    else
                    {
                        o->Alpha -= (0.05f) * effectFrames;
                    }

                    if (o->SubType == 1)
                    {
                        o->HeadAngle[2] += (20.f) * effectFrames;
                        o->Gravity = sinf(WorldTime * 0.01f) * 20.f + 30;

                        Vector(60.f, 0.f, 0.f, p);
                        AngleMatrix(o->HeadAngle, Matrix);
                        VectorRotate(p, Matrix, Position);
                        VectorAdd(o->Owner->Position, Position, Position);

                        Position[2] += o->Gravity;
                        Vector(1.f, 1.f, 1.f, Light);
                        CreateParticleFpsChecked(BITMAP_FIRE + 2, Position, o->Angle, Light, 10,
                                                 2.f);
                    }
                }
                else
                {
                    RetireEffect(o);
                }
                break;
            }
            break;
        case MODEL_FIRE:
            if (o->SubType == 0 || o->SubType == 2 || o->SubType == 4 || o->SubType == 6 ||
                o->SubType == 7 || o->SubType == 8)
            {
                bool success = false;
                float Height;
                if (o->SubType == 4)
                {
                    int PositionX = (int)(o->Position[0] / TERRAIN_SCALE);
                    int PositionY = (int)(o->Position[1] / TERRAIN_SCALE);
                    int WallIndex = TERRAIN_INDEX_REPEAT(PositionX, PositionY);
                    int Wall = TerrainWall[WallIndex] & TW_NOGROUND;

                    if (Wall != TW_NOGROUND)
                    {
                        Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
                        if (o->Position[2] < Height)
                        {
                            success = true;
                            EarthQuake = (float)(WorldRandom() % 4 - 4) * 0.1f;

                            for (int i = 0; i < CharactersClient.Size(); i++)
                            {
                                if (!CharactersClient.IsValidIndex(i))
                                    continue;
                                CHARACTER *tc = &CharactersClient[i];
                                OBJECT *to = &tc->Object;
                                float dx = o->Position[0] - to->Position[0];
                                float dy = o->Position[1] - to->Position[1];
                                float Distance = sqrtf(dx * dx + dy * dy);
                                if (to->Live && tc != Hero && tc->Dead == 0 && Distance <= 200)
                                {
                                    if (to->Type == MODEL_PLAYER)
                                    {
                                        if (tc->Helper.Type == MODEL_HORN_OF_FENRIR)
                                            SetAction_Fenrir_Damage(tc, to);
                                        else
                                            SetAction(to, PLAYER_SHOCK);
                                    }
                                    else
                                    {
                                        SetAction(to, MONSTER01_SHOCK);
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
                        if (o->Position[2] < -200)
                        {
                            success = true;
                            Height = o->Position[2];
                        }
                    }
                }
                else
                {
                    if (o->SubType == 6 || o->SubType == 7 || o->SubType == 8)
                    {
                        Vector(0.5f, 0.5f, 1.f, Light);
                        CreateSprite(BITMAP_LIGHT, o->Position, 3.f, Light, o);
                        Vector(1.f, 1.f, 1.f, Light);
                        CreateSprite(BITMAP_SHINY + 1, o->Position, 3.f, Light, o,
                                     (float)(WorldRandom() % 360), 1);
                        CreateSprite(BITMAP_SHINY + 1, o->Position, 4.f, o->Light, o,
                                     (float)(WorldRandom() % 360));

                        VectorCopy(o->HeadAngle, o->Angle);
                    }
                    float AddHeight = 0.f;
                    if (gMapManager.InHellas())
                    {
                        AddHeight = 50.f;
                    }
                    Height = RequestTerrainHeight(o->Position[0], o->Position[1]) + AddHeight;
                    if (o->Position[2] < Height)
                    {
                        success = true;
                    }
                }

                if (success)
                {
                    auto birth = EmissionTime(FPS_ANIMATION_FACTOR);
                    o->Position[2] = Height;
                    Vector(0.f, 0.f, 0.f, o->Direction);
                    vec3_t Position;
                    Vector(o->Position[0], o->Position[1], o->Position[2] + 80.f, Position);
                    if (o->SubType == 6 || o->SubType == 7 || o->SubType == 8)
                    {
                        BYTE smokeNum = 15;

                        Vector(0.f, 0.5f, 0.f, Light);
                        Vector(0.f, 0.f, 0.f, Angle);
                        CreateEffect(MODEL_SKILL_INFERNO, o->Position, Angle, Light, 2, o, 30, 0);

                        if (o->SubType == 7)
                        {
                            smokeNum = 7;
                        }
                        else if (o->SubType == 8)
                        {
                            smokeNum = 5;
                        }

                        for (int i = 0; i < smokeNum; ++i)
                        {
                            Position[0] = o->Position[0] + (WorldRandom() % 160 - 80);
                            Position[1] = o->Position[1] + (WorldRandom() % 160 - 100);
                            Position[2] = o->Position[2] + 50;

                            Vector(0.1f, 0.5f, 0.1f, Light);
                            CreateParticle(BITMAP_SMOKE, Position, o->Angle, Light, 11,
                                           (float)(WorldRandom() % 32 + 80) * 0.025f);
                        }

                        if (o->SubType != 7 && o->SubType != 8)
                        {
                            for (int j = 0; j < 6; j++)
                            {
                                CreateEffect(MODEL_STONE1 + WorldRandom() % 2, o->Position,
                                             o->Angle, Light);
                            }
                        }
                        PlayBuffer(SOUND_DEATH_POISON2);
                    }
                    else
                    {
                        for (int j = 0; j < 6; j++)
                        {
                            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, o->Position, o->Angle,
                                         o->Light);
                        }
                    }
                    CreateParticle(BITMAP_EXPLOTION, Position, o->Angle, Light);

                    RetireEffect(o);
                }
            }
            else if (o->SubType == 5)
            {
                AdvanceBouncingFire(*o, *sessionKeeper_.WorldUnit(), effectFrames);
                Height = RequestTerrainHeight(o->Position[0], o->Position[1]);

                int PositionX = (int)(o->Position[0] / TERRAIN_SCALE);
                int PositionY = (int)(o->Position[1] / TERRAIN_SCALE);
                int WallIndex = TERRAIN_INDEX_REPEAT(PositionX, PositionY);
                int Wall = TerrainWall[WallIndex] & TW_NOGROUND;

                if (Core::Time::Reaches(o->LifeTime, effectFrames, 1) || Wall == TW_NOGROUND)
                {
                    o->Position[2] = Height;

                    Vector(0.f, 0.f, 0.f, o->Direction);
                    vec3_t Position;
                    Vector(o->Position[0], o->Position[1], o->Position[2] + 80.f, Position);
                    CreateParticle(BITMAP_EXPLOTION, Position, o->Angle, Light);
                    for (int j = 0; j < 6; j++)
                    {
                        CreateEffect(MODEL_STONE1 + WorldRandom() % 2, o->Position, o->Angle,
                                     o->Light);
                    }
                    RetireEffect(o);
                }
            }
            else if (o->SubType == 9)
            {
                CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, o->Direction, 36,
                                         1.0f + o->Scale, o);
                CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, o->Direction, 37,
                                         2.0f + o->Scale, o);
                Vector(1.0f, 0.2f, 0.2f, o->Light);
                AddTerrainLight(o->Position[0], o->Position[1], o->Light, 4, PrimaryTerrainLight);

                float Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
                if (o->Position[2] < Height)
                {
                    RetireEffect(o);
                    break;
                }
            }

            if (o->SubType == 5)
            {
                o->HiddenMesh = 0;
                o->BlendMeshLight = 0.f;

                Vector(Luminosity * 1.f, Luminosity * 0.1f, Luminosity * 0.f, Light);
                AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
                for (auto birth : Emissions(FPS_ANIMATION_FACTOR * 0.8))
                    CreateParticle(BITMAP_POUNDING_BALL, o->Position, o->Angle, Light);
            }
            else
            {
                if (o->SubType == 3)
                {
                    o->HiddenMesh = 0;
                    o->BlendMeshLight = 0.f;
                }
                else if (o->SubType == 0)
                    o->BlendMeshLight = (float)(WorldRandom() % 4 + 4) * 0.1f;
                else if (o->SubType == 6 || o->SubType == 7)
                    o->BlendMeshLight = (float)(WorldRandom() % 4 + 4) * 0.1f;
                else
                    o->BlendMeshLight = 0.f;

                if (o->SubType == 6 || o->SubType == 7 || o->SubType == 8)
                {
                    Vector(Luminosity * 0.1f, Luminosity * 1.f, Luminosity * 0.f, Light);
                }
                else
                {
                    Vector(Luminosity * 1.f, Luminosity * 0.1f, Luminosity * 0.f, Light);
                }
                AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
                if (o->SubType == 6 || o->SubType == 7)
                {
                    for (int j = 0; j < 2; j++)
                    {
                        Position[0] = o->Position[0] + WorldRandom() % 50 - 25;
                        Position[1] = o->Position[1];
                        Position[2] = o->Position[2];
                        CreateParticleFpsChecked(BITMAP_FIRE, Position, o->Angle, Light, 5);
                    }
                }
                else if (o->SubType != 3)
                {
                    CreateParticleFpsChecked(BITMAP_FIRE, o->Position, o->Angle, Light, 5);
                }
                else
                {
                    vec3_t Pos;
                    vec3_t vRot;
                    GetMagicScrew(iIndex * 5371, vRot);
                    VectorScale(vRot, 50.f, vRot);
                    VectorCopy(o->Position, Pos);
                    VectorAdd(Pos, vRot, Pos);

                    Vector(0.0f, 0.0f, 0.0f, Angle);
                    Vector(1.0f, 1.0f, 1.0f, Light);
                    for (auto birthTime : Emissions(effectFrames))
                        CreateParticle(BITMAP_FIRE, Pos, Angle, Light, 8, o->Scale * 2.5f);
                    CreateSprite(BITMAP_SHINY + 1, Pos, o->Scale * 3.0f, Light, NULL,
                                 (float)(WorldRandom() % 360));
                }
                //                CreateParticleFpsChecked(BITMAP_SMOKE,o->Position,o->Angle,Light, 5);
                if (o->SubType == 1)
                {
                    CheckTargetRange(o);
                }
            }
            break;

        case MODEL_GATE:
        case MODEL_GATE + 1:
            GameLogic::Effects::AdvanceBouncingDebris(*o, *sessionKeeper_.WorldUnit(), effectFrames,
                                                      GameLogic::Effects::DebrisMotion::Heavy);

            for (auto birthTime : Emissions(effectFrames / 10.0))
            {
                CreateParticle(BITMAP_SMOKE + 1, o->Position, o->Angle, Light);
            }
            break;

        case MODEL_STONE_COFFIN:
        case MODEL_STONE_COFFIN + 1:
            GameLogic::Effects::AdvanceBouncingDebris(*o, *sessionKeeper_.WorldUnit(), effectFrames,
                                                      GameLogic::Effects::DebrisMotion::Heavy);

            o->Alpha = o->LifeTime / 10.f;
            for (auto birthTime : Emissions(effectFrames / 10.0))
            {
                CreateParticle(BITMAP_SMOKE + 1, o->Position, o->Angle, Light);
            }
            break;

        case BITMAP_JOINT_THUNDER + 1:
            EmitThunderBursts(*o, effectFrames);
            break;

        case BITMAP_JOINT_FORCE:
            if (o->SubType == 0 || o->SubType == 1)
            {
                constexpr float FirstStrikeLife = 10.f, StrikeInterval = 2.f, StrikeTurn = 72.f;
                for (float life = FirstStrikeLife; life > 0.f; life -= StrikeInterval)
                {
                    if (!Core::Time::Reaches(o->LifeTime, effectFrames, life))
                        continue;
                    auto birth =
                        EmissionTime(FPS_ANIMATION_FACTOR - std::max(0.f, o->LifeTime - life));
                    Vector(90.f, 0.f, 0.f, o->Angle);
                    o->HeadAngle[2] += StrikeTurn;
                    AngleMatrix(o->HeadAngle, Matrix);
                    VectorRotate(o->Direction, Matrix, Position);
                    VectorAdd(o->StartPosition, Position, Position);
                    if (o->SubType == 0)
                    {
                        Position[2] += WorldRandom() % 400 + 700.f;
                        CreateJoint(BITMAP_FLASH, Position, Position, o->Angle, 5, o, 110.f);
                    }
                    else
                    {
                        Position[2] += 100.f;
                        CreateJoint(BITMAP_JOINT_THUNDER + 1, Position, Position, o->Angle, 6, o,
                                    80.f);
                        CreateJoint(BITMAP_JOINT_THUNDER + 1, Position, Position, o->Angle, 6, o,
                                    80.f);
                    }
                }
            }
            break;

        case MODEL_EFFECT_SAPITRES_ATTACK_1: {
            if (o->SubType == 0)
            {
                float fMoveSpeed = 40.0f;
                o->Position[0] += ((o->Direction[0] * fMoveSpeed)) * effectFrames;
                o->Position[1] += ((o->Direction[1] * fMoveSpeed)) * effectFrames;

                vec3_t vLight;
                Vector(0.1f, 0.3f, 1.0f, vLight);
                CreateSprite(BITMAP_LIGHT, o->Position, 3.0f, vLight, o);

                CheckTargetRange(o);

                if (Core::Time::Reaches(o->LifeTime, effectFrames, 1))
                {
                    for (int i = 0; i < 3; i++)
                    {
                        CreateEffect(MODEL_EFFECT_SAPITRES_ATTACK_2, o->Position, o->Angle, vLight,
                                     0);
                    }
                }
            }
        }
        break;

        case MODEL_SWELL_OF_MAGICPOWER: {
            if (o->LifeTime <= 20)
                o->BlendMeshLight *= pow(0.86f, effectFrames);
            if (o->SubType == 0)
            {
                if (o->Owner->Type != MODEL_PLAYER)
                    break;

                BMD *pModel = &Models[o->Owner->Type];
                VectorCopy(o->Owner->Position, o->Position);
                vec3_t vLight, vPos;

                EmitSwellMagicBirths(*o);

                Vector(0.7f, 0.3f, 0.9f, vLight);
                if (o->LifeTime <= 20)
                {
                    vec3_t vDLight;
                    float fDynamicLightVal = o->LifeTime * 0.05f;
                    Vector(fDynamicLightVal * vLight[0], fDynamicLightVal * vLight[1],
                           fDynamicLightVal * vLight[2], vDLight);
                    for (int i = 0; i < pModel->NumBones; i++)
                    {
                        pModel->TransformByObjectBone(vPos, o->Owner, i);

                        CreateSprite(BITMAP_LIGHT, vPos, 1.5f, vDLight, o);
                    }
                }
            }
        }
        break;
        case MODEL_ARROWSRE06: {
            if (o->LifeTime <= 10)
                o->BlendMeshLight *= pow(0.8f, effectFrames);
            if (o->SubType == 0)
            {
                if (o->Owner->Type != MODEL_PLAYER)
                    break;

                BMD *pModel = &Models[o->Owner->Type];
                vec3_t vPos;
                pModel->TransformByObjectBone(vPos, o->Owner, o->PKKey);
                VectorCopy(vPos, o->Position);
                o->Scale -= (1.0f) * effectFrames;
                if (o->LifeTime <= 15)
                {
                    o->Scale += (0.5f) * effectFrames;
                }
            }
            else if (o->SubType == 1)
            {
                if (o->Owner->Type != MODEL_PLAYER)
                    break;

                BMD *pModel = &Models[o->Owner->Type];
                vec3_t vPos;
                pModel->TransformByObjectBone(vPos, o->Owner, o->PKKey);
                VectorCopy(vPos, o->Position);

                if (o->LifeTime >= 15)
                {
                    o->Scale *= pow(1.05f, effectFrames);
                }
                else
                {
                    o->Scale *= pow(0.95f, effectFrames);
                }

                CreateSprite(BITMAP_LIGHT, o->Position, o->Scale, o->Light, o->Owner);
                CreateSprite(BITMAP_LIGHT, o->Position, o->Scale * 0.8f, o->Light, o->Owner);

                if (o->LifeTime <= 10)
                {
                    o->Alpha *= pow(0.95f, effectFrames);
                }
            }
#ifdef PJH_ADD_PANDA_CHANGERING
            else if (o->SubType == 2)
            {
                vec3_t vRelativePos;
                BMD *b = &Models[o->Owner->Type];
                if (o->Owner->Type != MODEL_PLAYER)
                    break;

                if (o->LifeTime > 15)
                {
                    o->Scale *= pow(0.9f, effectFrames);
                }
                else if (Core::Time::Reaches(o->LifeTime, effectFrames, 15))
                {
                    o->Scale = 2.f;
                }
                else
                {
                    o->Scale *= pow(0.65f, effectFrames);
                }

                if (o->PKKey == 0)
                {
                    Vector(13.f, 14.f, -10.f, vRelativePos);
                    b->TransformPosition(o->Owner->BoneTransform[20], vRelativePos, o->Position);
                    VectorAdd(o->Position, o->Owner->Position, o->Position);
                }
                else
                {
                    Vector(13.f, 14.f, 10.f, vRelativePos);
                    b->TransformPosition(o->Owner->BoneTransform[20], vRelativePos, o->Position);
                    VectorAdd(o->Position, o->Owner->Position, o->Position);
                }

                CreateSprite(BITMAP_LIGHT, o->Position, o->Scale, o->Light, o->Owner);
                CreateSprite(BITMAP_LIGHT, o->Position, o->Scale * 0.8f, o->Light, o->Owner);

                if (o->LifeTime <= 10)
                {
                    o->Alpha *= pow(0.95f, effectFrames);
                }
            }
#endif //PJH_ADD_PANDA_CHANGERING
        }
        break;
        case MODEL_SWELL_OF_MAGICPOWER_BUFF_EFF: {
            if (o->SubType == 0)
            {
                VectorCopy(o->Owner->Position, o->Position);
                constexpr float HandPeriodMilliseconds = 6000.f;
                while (WorldTime - o->Timer >= HandPeriodMilliseconds)
                {
                    o->Timer += HandPeriodMilliseconds;
                    auto birth = EmissionTime(
                        float((WorldTime - o->Timer) *
                              sessionKeeper_.ApplicationConfig().legacyReferenceFps / 1000.0));
                    EmitSwellHandPair(*o, birth);
                }
                o->LifeTime = 999.f;
            }
        }
        break;
        case MODEL_STAR_SHINE: {
            if (o->SubType == 0)
            {
                if (o->LifeTime <= 10)
                {
                    o->Scale *= pow(0.9f, effectFrames);
                    o->Alpha *= pow(0.9f, effectFrames);
                    o->Light[0] *= pow(0.9f, effectFrames);
                    o->Light[1] *= pow(0.9f, effectFrames);
                    o->Light[2] *= pow(0.9f, effectFrames);
                }
                else if (o->LifeTime <= 20)
                {
                    CreateSprite(BITMAP_SHINY, o->Position, o->Scale, o->Light, o, o->Angle[0]);
                }
                else if (o->LifeTime <= 30)
                {
                    o->Scale *= pow(1.1f, effectFrames);
                    o->Alpha *= pow(1.1f, effectFrames);
                    o->Light[0] *= pow(1.1f, effectFrames);
                    o->Light[1] *= pow(1.1f, effectFrames);
                    o->Light[2] *= pow(1.1f, effectFrames);
                }

                CreateSprite(BITMAP_SHINY, o->Position, o->Scale, o->Light, o, o->Angle[0]);
            }
        }
        break;
        case MODEL_FEATHER: {
            switch (o->SubType)
            {
            case 2: {
                o->Light[0] *= pow(0.97f, effectFrames);
                o->Light[1] *= pow(0.97f, effectFrames);
                o->Light[2] *= pow(0.97f, effectFrames);

                o->Scale *= pow(0.97f, effectFrames);
                o->Alpha *= pow(0.97f, effectFrames);
                VectorAddScaled(o->Angle, o->EyeRight, o->Angle, effectFrames);
            }
            break;
            case 0: {
                o->Light[0] *= pow(0.97f, effectFrames);
                o->Light[1] *= pow(0.97f, effectFrames);
                o->Light[2] *= pow(0.97f, effectFrames);

                o->Alpha *= pow(0.97f, effectFrames);

                VectorAddScaled(o->Angle, o->EyeRight, o->Angle, effectFrames);
            }
            break;
            case 3: {
                o->Light[0] *= pow(0.97f, effectFrames);
                o->Light[1] *= pow(0.97f, effectFrames);
                o->Light[2] *= pow(0.97f, effectFrames);

                o->Alpha *= pow(0.97f, effectFrames);

                o->Scale *= pow(0.97f, effectFrames);

                VectorAddScaled(o->Angle, o->EyeRight, o->Angle, effectFrames);
            }
            break;
            case 1: {
                o->Light[0] *= pow(0.97f, effectFrames);
                o->Light[1] *= pow(0.97f, effectFrames);
                o->Light[2] *= pow(0.97f, effectFrames);

                o->Alpha *= pow(0.97f, effectFrames);

                o->Scale *= pow(0.99f, effectFrames);

                VectorAddScaled(o->Angle, o->EyeRight, o->Angle, effectFrames);
            }
            break;
            }
        }
        break;

        case MODEL_STREAMOFICEBREATH: {
            if (o->SubType != 0)
                break;
            const float active = std::min(FPS_ANIMATION_FACTOR, std::max(0.f, o->LifeTime - 1.f));
            for (auto birth : Emissions(active, active))
                for (int i = 0; i < 4; ++i)
                {
                    const float noise = (WorldRandom() % 2000 - 1000) * 0.001f;
                    vec3_t position, angle, light{0.5f, 0.6f, 0.94f};
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        position[axis] = o->StartPosition[axis] + 20.f * noise;
                        angle[axis] = o->Angle[axis] + 15.f * noise;
                    }
                    CreateParticle(BITMAP_RAKLION_CLOUDS, position, angle, light, 1,
                                   o->Scale + 0.01f * noise);
                }
        }
        break;
        case BITMAP_RING_OF_GRADATION: {
            if (o->SubType == 0)
            {
                if (Core::Time::Reaches(o->LifeTime, effectFrames, 20))
                    break;

                o->Scale += (0.1f) * effectFrames;

                o->Light[0] *= pow(1.0f / (1.1f), effectFrames);
                o->Light[1] *= pow(1.0f / (1.1f), effectFrames);
                o->Light[2] *= pow(1.0f / (1.1f), effectFrames);

                o->Alpha *= pow(1.0f / (1.1f), effectFrames);
            }
        }
        break;
        case MODEL_EFFECT_UMBRELLA_DIE: {
            if (o->SubType != 0)
                break;
            for (float life : {28.f, 18.f, 8.f})
            {
                if (!Core::Time::Reaches(o->LifeTime, effectFrames, life))
                    continue;
                auto birth = EmissionTime(FPS_ANIMATION_FACTOR - std::max(0.f, o->LifeTime - life));
                vec3_t light{1.f, 0.2f, 0.5f}, position;
                o->Owner->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Owner->Position,
                                             position);
                CreateEffect(BITMAP_RING_OF_GRADATION, position, o->Angle, light, 0, o->Owner, 0, 0,
                             0, 0, 1.2f);
            }
        }
        break;
        case MODEL_EFFECT_UMBRELLA_GOLD:
            AdvanceUmbrellaGold(*o, effectFrames);
            break;
        case MODEL_EFFECT_EG_GUARDIANDEFENDER_ATTACK2: {
            if (o->LifeTime <= 18)
            {
                o->Light[0] *= pow(1.0f / (1.4f), effectFrames);
                o->Light[1] *= pow(1.0f / (1.4f), effectFrames);
                o->Light[2] *= pow(1.0f / (1.4f), effectFrames);
                o->Scale *= pow(1.1f, effectFrames);
            }
            CreateSprite(BITMAP_SHOCK_WAVE, o->Position, o->Scale, o->Light, o->Owner);
            CreateParticleFpsChecked(BITMAP_FIRE + 2, o->Position, o->Angle, o->Light, 16, 2.5f);
        }
        break;
        case BITMAP_SHINY + 4: {
            for (auto birth : Emissions(FPS_ANIMATION_FACTOR))
            {
                vec3_t position;
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
                position[0] += WorldRandom() % 100 - 40;
                position[1] += WorldRandom() % 100 - 40;
                position[2] += WorldRandom() % 100 + 50;
                CreateParticle(BITMAP_SHINY + 4, position, o->Angle, o->Light, 2, o->Scale);
            }
        }
        break;
        }

    if (o->BirthTiming.serial != birthSerial)
        return;

    if (o->Type == MODEL_SKILL_WHEEL1 || o->Type == MODEL_SKILL_WHEEL2 ||
        o->Type == MODEL_SKILL_FURY_STRIKE ||
        ((o->Type == MODEL_STONE1 || o->Type == MODEL_STONE2) && o->SubType == 5) ||
        (o->Type == MODEL_ARROW_DRILL && o->SubType == 3) || o->Type == MODEL_PIER_PART ||
        o->Type == MODEL_DEATH_SPI_SKILL || o->Type == MODEL_CHANGE_UP_EFF)
    {
    }
    else
    {
        if (o->Type >= MODEL_BIRD01 && o->Type < MODEL_SKILL_END)
        {
            BMD *b = &Models[o->Type];
            b->CurrentAction = o->CurrentAction;
            const ObjectMotionTrace::AnimationPhase phase{o->AnimationFrame, o->PriorAnimationFrame,
                                                          o->CurrentAction, o->PriorAction};
            // Sword force historically played twice in this tail. Combine its travel
            // before playback so termination cannot feed action 255 into a second call.
            const float animationTravel =
                o->CurrentAction == 255
                    ? 0.f
                    : o->EffectAnimationAdvance.value_or(o->Velocity * effectFrames) *
                          (o->Type == MODEL_SWORD_FORCE ? 2.f : 1.f);
            o->MotionTrace.AdvanceAnimation(effectFrames, phase, animationTravel);
            if (o->CurrentAction != 255 &&
                b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction,
                                 animationTravel, o->Position, o->Angle, 1.f) == false)
            {
                const float lastKey = static_cast<float>(
                    (std::max)(0, b->Actions[b->CurrentAction].NumAnimationKeys - 1));
                o->AnimationFrame = lastKey;
                o->PriorAnimationFrame = lastKey;
                o->PriorAction = b->CurrentAction;
                o->CurrentAction = 255;
            }
        }
        if (o->Type < BITMAP_BOSS_LASER || o->Type > BITMAP_BOSS_LASER + 2)
        {
            switch (o->Type)
            {
            case MODEL_SAW:
            case MODEL_SPEARSKILL:
                AdvanceEffectPosition(o, false, effectFrames);
                break;
            case MODEL_ARROW:
                if (o->SubType != 3 && o->SubType != 4)
                    AdvanceEffectPosition(o, true, effectFrames);
                break;
            case MODEL_ARROW_DRILL:
                if (o->SubType != 0 && o->SubType != 2)
                    AdvanceEffectPosition(o, true, effectFrames);
                break;
            case MODEL_ARROW_HOLY:
            case MODEL_ARROW_DOUBLE:
            case MODEL_ARROW_NATURE:
            case MODEL_LACEARROW:
            case MODEL_ARROW_BEST_CROSSBOW:
            case MODEL_CUNDUN_DRAGON_HEAD:
                break;
            case MODEL_STORM:
                if (o->SubType != 1)
                    AdvanceEffectPosition(o, true, effectFrames);
                break;
            case MODEL_LASER:
                if (o->SubType == 0 || o->SubType == 3)
                    AdvanceEffectPosition(o, true, effectFrames);
                break;
            case MODEL_ICE_SMALL:
            case MODEL_METEO1:
            case MODEL_METEO2:
            case MODEL_BOSS_ATTACK:
            case MODEL_EFFECT_SAPITRES_ATTACK_2:
                break;
            case MODEL_SKULL:
                if (o->SubType != 1 || o->Owner == nullptr)
                    AdvanceEffectPosition(o, true, effectFrames);
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
                if (o->SubType != 2 && o->SubType != 3 && o->SubType != 4)
                    AdvanceEffectPosition(o, true, effectFrames);
                break;
            case BITMAP_SKULL:
                if (o->SubType != 4)
                    AdvanceEffectPosition(o, true, effectFrames);
                break;
            case MODEL_STAFF_OF_DESTRUCTION:
            case MODEL_ARROW_IMPACT:
                break;
            case MODEL_LIGHTNING_SHOCK:
                if (o->SubType != 0)
                    AdvanceEffectPosition(o, true, effectFrames);
                break;
            case MODEL_EFFECT_UMBRELLA_GOLD:
            case MODEL_WATER_WAVE:
                break;
            case MODEL_DRAGON:
                break;
            case MODEL_BLIZZARD:
                if (o->SubType != 0 && o->SubType != 2)
                    AdvanceEffectPosition(o, true, effectFrames);
                break;
            case MODEL_FIRE:
                if (o->SubType != 5)
                    AdvanceEffectPosition(o, true, effectFrames);
                break;
            case MODEL_HALLOWEEN_CANDY_BLUE:
            case MODEL_HALLOWEEN_CANDY_ORANGE:
            case MODEL_HALLOWEEN_CANDY_YELLOW:
            case MODEL_HALLOWEEN_CANDY_RED:
            case MODEL_HALLOWEEN_CANDY_HOBAK:
            case MODEL_HALLOWEEN_CANDY_STAR:
                if (o->SubType != 0 && o->SubType != 1)
                    AdvanceEffectPosition(o, true, effectFrames);
                break;
            case MODEL_XMAS_EVENT_BOX:
            case MODEL_XMAS_EVENT_CANDY:
            case MODEL_XMAS_EVENT_TREE:
            case MODEL_XMAS_EVENT_SOCKS:
            case MODEL_NEWYEARSDAY_EVENT_BEKSULKI:
            case MODEL_NEWYEARSDAY_EVENT_CANDY:
            case MODEL_NEWYEARSDAY_EVENT_MONEY:
            case MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_GREEN:
            case MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_RED:
            case MODEL_NEWYEARSDAY_EVENT_PIG:
            case MODEL_NEWYEARSDAY_EVENT_YUT:
            case MODEL_MOONHARVEST_GAM:
            case MODEL_MOONHARVEST_SONGPUEN1:
            case MODEL_MOONHARVEST_SONGPUEN2:
            case MODEL_CURSEDTEMPLE_STATUE_PART1:
            case MODEL_CURSEDTEMPLE_STATUE_PART2:
            case MODEL_PROJECTILE:
            case MODEL_PIERCING2:
            case MODEL_XMAS2008_SNOWMAN_HEAD:
            case MODEL_BIG_STONE_PART2:
            case MODEL_BIG_STONE_PART1:
            case MODEL_WALL_PART1:
            case MODEL_WALL_PART2:
            case MODEL_GOLEM_STONE:
            case MODEL_GATE_PART1:
            case MODEL_GATE_PART2:
            case MODEL_GATE_PART3:
            case MODEL_MAYASTONE4:
            case MODEL_MAYASTONE5:
            case MODEL_SKIN_SHELL:
            case MODEL_BUTTERFLY01: // The butterfly controller owns its yaw arc and vertical motion.
            case MODEL_GHOST:       // The ghost controller owns curved travel.
            case MODEL_GATE:
            case MODEL_GATE + 1:
            case MODEL_STONE_COFFIN:
            case MODEL_STONE_COFFIN + 1: // The debris owner consumes both motion contributions.
            case MODEL_ARROW_BOMB: // MoveJump already consumed horizontal and vertical movement.
            case MODEL_FLY_BIG_STONE1:
            case MODEL_FLY_BIG_STONE2:
                break;

            case MODEL_SKILL_JAVELIN:
                break;
            case MODEL_SWORD_FORCE: {
                if (o->SubType != 0 && o->SubType != 2)
                    AdvanceEffectPosition(o, true, effectFrames);
            }
            break;
            default:
                AdvanceEffectPosition(o, true, effectFrames);
                break;
            }
        }
    }
    const bool particleEmitter =
        o->Type == MODEL_FISSURE || (o->Type == MODEL_SUMMONER_SUMMON_LAGUL && o->SubType == 1);
    if (particleEmitter && o->Live)
    {
        AdvanceEffectAnimation(*o);
        EmitModelEffectParticles(*o,
                                 (std::min)(FPS_ANIMATION_FACTOR, (std::max)(0.f, initialLife)));
    }
    o->EffectEmissionAge += effectFrames;
    o->LifeTime -= effectFrames;
    if (SceneFlag == MAIN_SCENE && o->Live)
        AdvanceEffectShadow(*o, initialLife);
    if (o->LifeTime <= 0 || !o->Live)
    {
        EffectDestructor(o);
    }
    else
    {
        if (!particleEmitter)
            AdvanceEffectAnimation(*o);
        AdvanceWeaponEffect(*o);
    }
}

void SessionGameplayUnit::MoveEffects()
{
    for (auto cursor = Effects.begin(), end = Effects.end(); cursor != end; ++cursor)
    {
        const int i = cursor.Index();
        OBJECT *o = &*cursor;
        EffectBirthStep birthStep(FPS_ANIMATION_FACTOR, o->BirthTiming);
        if (FPS_ANIMATION_FACTOR <= 0.f)
            continue;
        const auto birthSerial = o->BirthTiming.serial;
        MoveEffect(o, i);
        if (o->BirthTiming.serial != birthSerial)
            continue;
        AdvanceMapEffectVisual(*o);
        TheMapProcess().AdvanceObjectFade(*o);
    }
    g_SkillEffects.MoveEffects();
}

void SessionLegacyCalls::CreateEffectFpsChecked(int type, vec3_t position, vec3_t angle,
                                                vec3_t light, int subType, OBJECT *owner,
                                                short pkKey, WORD skillIndex, WORD skill,
                                                WORD skillSerialNumber, float scale,
                                                int targetIndex)
{
    sessionKeeper_.Gameplay()->CreateEffectFpsChecked(type, position, angle, light, subType, owner,
                                                      pkKey, skillIndex, skill, skillSerialNumber,
                                                      scale, targetIndex);
}

void SessionLegacyCalls::CreateEffect(int type, vec3_t position, vec3_t angle, vec3_t light,
                                      int subType, OBJECT *owner, short pkKey, WORD skillIndex,
                                      WORD skill, WORD skillSerialNumber, float scale,
                                      int targetIndex)
{
    sessionKeeper_.Gameplay()->CreateEffect(type, position, angle, light, subType, owner, pkKey,
                                            skillIndex, skill, skillSerialNumber, scale,
                                            targetIndex);
}

void SessionLegacyCalls::MoveParticle(OBJECT *object, int turn)
{
    sessionKeeper_.Gameplay()->MoveParticle(object, turn);
}

void SessionLegacyCalls::MoveParticle(OBJECT *object, vec3_t angle)
{
    sessionKeeper_.Gameplay()->MoveParticle(object, angle);
}

bool SessionLegacyCalls::MoveJump(OBJECT *object)
{
    return sessionKeeper_.Gameplay()->MoveJump(object);
}

void SessionLegacyCalls::MoveEffect(OBJECT *object, int index)
{
    sessionKeeper_.Gameplay()->MoveEffect(object, index);
}

void SessionLegacyCalls::MoveEffects()
{
    sessionKeeper_.Gameplay()->MoveEffects();
}

void SessionLegacyCalls::CheckClientArrow(OBJECT *object)
{
    sessionKeeper_.Gameplay()->CheckClientArrow(object);
}

void SessionGameplayUnit::CreateMyGensInfluenceGroundEffect()
{
    DeleteEffect(BITMAP_OUR_INFLUENCE_GROUND, &Hero->Object, 0);
    if (IsStrifeMap(gMapManager.ContextMap()))
    {
        vec3_t vTemp = {0.f, 0.f, 0.f};
        CreateEffect(BITMAP_OUR_INFLUENCE_GROUND, Hero->Object.Position, vTemp, vTemp, 0,
                     &Hero->Object);
    }
}

void SessionLegacyCalls::CreateMyGensInfluenceGroundEffect()
{
    sessionKeeper_.Gameplay()->CreateMyGensInfluenceGroundEffect();
}

void SessionLegacyCalls::CreateHealing(OBJECT *object)
{
    sessionKeeper_.Gameplay()->CreateHealing(object);
}

void SessionLegacyCalls::CreateForce(OBJECT *object, vec_t *position)
{
    sessionKeeper_.Gameplay()->CreateForce(object, position);
}

bool SessionLegacyCalls::DeleteEffect(int type, OBJECT *owner, int subType)
{
    return sessionKeeper_.Gameplay()->DeleteEffect(type, owner, subType);
}

void SessionLegacyCalls::DeleteEffect(int effectType)
{
    sessionKeeper_.Gameplay()->DeleteEffect(effectType);
}

bool SessionLegacyCalls::SearchEffect(int type, OBJECT *owner, int subType)
{
    return sessionKeeper_.Gameplay()->SearchEffect(type, owner, subType);
}

BOOL SessionLegacyCalls::FindSameEffectOfSameOwner(int type, OBJECT *owner)
{
    return sessionKeeper_.Gameplay()->FindSameEffectOfSameOwner(type, owner);
}

void SessionLegacyCalls::CheckTargetRange(OBJECT *object)
{
    sessionKeeper_.Gameplay()->CheckTargetRange(object);
}

void SessionLegacyCalls::CreateBomb(vec_t *position, bool explode, int subType)
{
    sessionKeeper_.Gameplay()->CreateBomb(position, explode, subType);
}

void SessionLegacyCalls::CreateBomb2(vec_t *position, bool explode, int subType, float scale)
{
    sessionKeeper_.Gameplay()->CreateBomb2(position, explode, subType, scale);
}

void SessionLegacyCalls::CreateBomb3(vec_t *position, int subType, float scale)
{
    sessionKeeper_.Gameplay()->CreateBomb3(position, subType, scale);
}

void SessionLegacyCalls::CreateInferno(vec_t *position, int subType)
{
    sessionKeeper_.Gameplay()->CreateInferno(position, subType);
}

void SessionGameplayUnit::AdvanceBossLaser(OBJECT &object)
{
    constexpr int segmentCount = 20;
    const bool rotating = object.SubType == 1 || object.SubType == 2;
    object.HeadAngle[0] = object.Angle[2];
    object.HeadAngle[1] = rotating ? 0.1f * FPS_ANIMATION_FACTOR : 0.f;
    if (object.SubType == 1)
    {
        Vector(0.2f, 0.f, 0.f, object.Light);
    }
    else if (object.SubType == 2)
    {
        Vector(0.f, 0.2f, 1.f, object.Light);
    }
    vec3_t position;
    VectorCopy(object.Position, position);
    for (int segment = 0; segment < segmentCount; ++segment)
    {
        object.Angle[2] -= object.HeadAngle[1];
        VectorAdd(position, object.Direction, position);
        if (IsBattleCastleStart() &&
            (TERRAIN_ATTRIBUTE(position[0], position[1]) & TW_NOATTACKZONE))
            break;
        AddTerrainLight(position[0], position[1], object.Light, 2, PrimaryTerrainLight);
    }
}

void SessionGameplayUnit::AdvanceModelEffectVisual(OBJECT &object)
{
    switch (object.Type)
    {
    case MODEL_SKULL:
    case MODEL_ARROW_AUTOLOAD:
    case MODEL_INFINITY_ARROW:
    case MODEL_SUMMONER_SUMMON_NEIL:
    case MODEL_ARROW_DRILL:
        break;
    default:
        return;
    }
    if (object.Alpha < 0.01f)
        return;
    auto *o = &object;
    BMD *b = &Models[object.Type];
    BoneScale = 1.f;
    b->BodyHeight = 0.f;
    b->BodyScale = object.Scale;
    b->CurrentAction = object.CurrentAction;
    VectorCopy(object.Position, b->BodyOrigin);
    b->Animation(BoneTransform, object.AnimationFrame, object.PriorAnimationFrame,
                 object.PriorAction, object.Angle, object.HeadAngle, false, true);
    if (object.Type == MODEL_SKULL)
    {
        vec3_t Light, p;
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(BoneTransform[2], p, o->EyeLeft, false);
        b->TransformPosition(BoneTransform[3], p, o->EyeRight, false);
        Vector(1.f, 0.f, 0.f, Light);
        CreateSprite(BITMAP_LIGHT, o->EyeLeft, 1.f, Light, o);
        CreateSprite(BITMAP_LIGHT, o->EyeRight, 1.f, Light, o);
        Vector(0.5f, 0.5f, 0.5f, Light);
        CreateSprite(BITMAP_SHINY + 1, o->EyeLeft, 0.5f, Light, o, (float)(WorldRandom() % 360));
        CreateSprite(BITMAP_SHINY + 1, o->EyeRight, 0.5f, Light, o, (float)(WorldRandom() % 360));
    }
    else if (object.Type == MODEL_ARROW_AUTOLOAD)
    {
        vec3_t Light, p1, p2;
        Vector(0.f, 0.f, 0.f, p1);
        Vector(1.f, 0.8f, 0.3f, Light);
        if (o->LifeTime > 15)
        {
            b->TransformPosition(BoneTransform[1], p1, p2);
            CreateParticleFpsChecked(BITMAP_LIGHT + 1, p2, o->Angle, Light, 5, 0.6f);
            b->TransformPosition(BoneTransform[3], p1, p2);
            CreateParticleFpsChecked(BITMAP_LIGHT + 1, p2, o->Angle, Light, 5, 0.8f);
        }
    }
    else if (object.Type == MODEL_INFINITY_ARROW)
    {
        vec3_t p1, p2;
        Vector(0.f, 0.f, 0.f, p1);
        for (int idx = 1; idx <= 9; ++idx)
        {
            if (idx == 5)
                continue;
            b->TransformPosition(BoneTransform[idx], p1, p2);
            CreateJointFpsChecked(BITMAP_FLARE + 1, p2, o->Position, o->Angle, 16, o, 20.f);
        }
    }
    else if (object.Type == MODEL_SUMMONER_SUMMON_NEIL)
    {
        float Start_Frame = 0.f;
        float End_Frame = 10.0f;
        if (o->AnimationFrame >= Start_Frame && o->AnimationFrame <= End_Frame &&
            o->CurrentAction == 0)
        {
            vec3_t Light;
            Vector(1.0f, 0.0f, 0.0f, Light);
            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;
            float fActionSpeed = o->Velocity;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);
                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);
                b->TransformPosition(BoneTransform[51], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[59], EndRelative, EndPos, false);
                CreateObjectBlur(o, StartPos, EndPos, Light, 2);
                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    else
    {
        vec3_t Position, p, Light;
        Vector(1.f, 0.6f, 0.4f, Light);
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(BoneTransform[1], p, Position);
        CreateSprite(BITMAP_SHINY + 1, Position, (float)(sinf(WorldTime * 0.002f) * 0.3f + 1.3f),
                     Light, o, (float)(WorldRandom() % 360));
        CreateSprite(BITMAP_LIGHT, Position, 1.5f, Light, o, 90.f);
        CreateSprite(BITMAP_SHINY + 1, Position, (float)(sinf(WorldTime * 0.002f) * 0.3f + 1.3f),
                     Light, o, (float)(WorldRandom() % 360));
        CreateParticleFpsChecked(BITMAP_SPARK, Position, o->Angle, Light);
    }
}

void SessionGameplayUnit::AdvanceMapEffectVisual(OBJECT &object)
{
    if (!object.Live)
        return;
    const bool stoneImpact =
        (object.Type == MODEL_FLY_BIG_STONE1 || object.Type == MODEL_FLY_BIG_STONE2) &&
        object.HiddenMesh == 99;
    if (stoneImpact)
        object.HiddenMesh = -2;
    if (!object.Visible)
        return;
    SessionRandom::PresentationScope presentation(sessionKeeper_.RandomForConstruction());
    AdvanceModelEffectVisual(object);
    vec3_t light;
    if (object.Type == MODEL_ARROW_TANKER_HIT || object.Type == MODEL_ARROW_TANKER)
    {
        Vector(1.f, 1.f, 1.f, light);
        CreateParticleFpsChecked(BITMAP_FIRE + 1, object.Position, object.Angle, light, 8,
                                 object.Scale - 0.4f, &object);
        CreateParticleFpsChecked(BITMAP_FIRE + 1, object.Position, object.Angle, light, 8,
                                 object.Scale - 0.4f, &object);
        CreateParticleFpsChecked(BITMAP_SMOKE, object.Position, object.Angle, light, 38,
                                 object.Scale, &object);
        Vector(1.f, 0.4f, 0.f, light);
        CreateParticleFpsChecked(BITMAP_FIRE + 1, object.Position, object.Angle, light, 9,
                                 object.Scale - 0.4f, &object);
    }
    else if (object.Type == MODEL_CURSEDTEMPLE_HOLYITEM)
    {
        Vector(0.9f, 0.6f, 0.6f, light);
        CreateParticleFpsChecked(BITMAP_POUNDING_BALL, object.Position, object.Angle, light, 2,
                                 0.1f, &object);
    }
    else if (stoneImpact)
    {
        PrepareWorldObjectPose(object);
        BMD &model = Models[object.Type];
        model.Transform(BoneTransform, object.BoundingBoxMin, object.BoundingBoxMax, &object.OBB,
                        false);
        EmitMeshEffects(model, 0,
                        object.Type == MODEL_FLY_BIG_STONE1 ? MODEL_BIG_STONE_PART1
                                                            : MODEL_BIG_STONE_PART2);
    }
}

void SessionLegacyCalls::AdvanceMapEffectVisual(OBJECT &object)
{
    sessionKeeper_.Gameplay()->AdvanceMapEffectVisual(object);
}

void SessionGameplayUnit::EmitModelEffectParticles(OBJECT &effect, float frames)
{
    if (frames <= 0.f)
        return;
    const bool lagul = effect.Type == MODEL_SUMMONER_SUMMON_LAGUL && effect.SubType == 1;
    if (!lagul && effect.Type != MODEL_FISSURE)
        return;
    SessionRandom::PresentationScope visualOrigin(sessionKeeper_.RandomForConstruction(),
                                                  lagul || effect.PresentationRandom);
    auto &model = sessionKeeper_.ModelPoolObject()[effect.Type];
    AnimationPoseSample pose(&effect, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    if (lagul)
    {
        vec3_t smokeLight{0.6f, 0.5f, 0.9f}, waterLight{0.7f, 0.7f, 1.f};
        for (auto birthTime : Emissions(frames / 5.f, frames))
        {
            vec3_t position, offset{};
            const float fraction = birthTime.FrameFraction();
            pose.SampleBonePosition(model, effect, WorldRandom() % model.NumBones, offset,
                                    WorldTime, fraction, position);
            CreateParticle(BITMAP_SMOKE, position, effect.Angle, smokeLight, 57, 2.f);
            CreateParticle(BITMAP_TWINTAIL_WATER, position, effect.Angle, waterLight, 1);
        }
        return;
    }
    for (auto birthTime : Emissions(frames, frames))
    {
        const float fraction = birthTime.FrameFraction();
        std::array<vec34_t, MAX_BONES> bones;
        pose.EvaluateAtTime(model, effect, WorldTime, fraction, bones.data());
        vec3_t origin, light{1.f, 1.f, 1.f};
        effect.MotionTrace.Sample(WorldTime, fraction, effect.Position, origin);
        for (int bone = 0; bone < model.NumBones; ++bone)
        {
            if (model.Bones[bone].Dummy || !Random.FpsCheck(12, 1.f))
                continue;
            vec3_t zero{}, position;
            VectorTransform(zero, bones[bone], position);
            VectorScale(position, effect.Scale, position);
            VectorAdd(position, origin, position);
            CreateParticle(BITMAP_TRUE_FIRE, position, effect.Angle, light, 3, 4.f, &effect);
            CreateParticle(BITMAP_SMOKE, position, effect.Angle, light, 21, 1.f);
        }
    }
}

void SessionGameplayUnit::EmitShinyModelCloud(OBJECT &effect)
{
    if (effect.SubType == 0)
    {
        for (auto birth : Emissions(FPS_ANIMATION_FACTOR))
        {
            vec3_t position{effect.Position[0] + WorldRandom() % 500 - 250.f,
                            effect.Position[1] + WorldRandom() % 500 - 250.f,
                            effect.Position[2] - WorldRandom() % 100 + 150.f};
            CreateParticle(effect.Type, position, effect.Angle, effect.Light, 0, effect.Scale);
        }
        return;
    }
    BMD &model = Models[effect.Owner->Type];
    AnimationPoseSample pose(effect.Owner, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    for (auto birth : Emissions(FPS_ANIMATION_FACTOR / 2.0))
    {
        const int bone = WorldRandom() % model.NumBones;
        if (model.Bones[bone].Dummy)
            continue;
        vec3_t offset{0.f, 0.f, 100.f}, position;
        pose.SampleBonePosition(model, *effect.Owner, bone, offset, WorldTime,
                                birth.FrameFraction(), position);
        position[2] -= 20.f;
        CreateParticle(effect.Type, position, effect.Angle, effect.Light, 0, effect.Scale);
    }
}

void SessionGameplayUnit::EmitGolemMeshDebris(vec3_t position, int iSubType, vec3_t angle,
                                              vec3_t Light, int &iEffectCount)
{
    if (Random.FpsCheck(45, 1.f) && iEffectCount < 20)
    {
        if (iSubType == 0)
        {
            CreateEffect(MODEL_GOLEM_STONE, position, angle, Light);
        }
        else if (iSubType == 1)
        {
            CreateEffect(MODEL_BIG_STONE_PART1, position, angle, Light, 2);
            CreateEffect(MODEL_BIG_STONE_PART2, position, angle, Light, 2);
        }
        iEffectCount++;
    }
}

void SessionGameplayUnit::EmitMeshEffects(BMD &model, int i, int iType, int iSubType, vec3_t Angle,
                                          VOID *obj)
{
    if (i >= model.NumMeshs || i < 0)
        return;

    Mesh_t *m = &model.Meshs[i];
    if (m->NumTriangles <= 0)
        return;
    model.EnsureCpuTransforms();

    vec3_t angle, Light;
    int iEffectCount = 0;

    Vector(0.f, 0.f, 0.f, angle);
    Vector(1.f, 1.f, 1.f, Light);
    for (int j = 0; j < m->NumTriangles; j++)
    {
        Triangle_t *tp = &m->Triangles[j];
        for (int k = 0; k < tp->Polygon; k++)
        {
            int vi = tp->VertexIndex[k];

            switch (iType)
            {
            case MODEL_STONE_COFFIN:
                if (iSubType == 0)
                {
                    if (Random.FpsCheck(2, 1.f))
                    {
                        CreateEffect(MODEL_STONE_COFFIN + 1, VertexTransform[i][vi], angle, Light);
                    }
                    if (Random.FpsCheck(10, 1.f))
                    {
                        CreateEffect(MODEL_STONE_COFFIN, VertexTransform[i][vi], angle, Light);
                    }
                }
                else if (iSubType == 1)
                {
                    CreateEffect(MODEL_STONE_COFFIN + 1, VertexTransform[i][vi], angle, Light, 2);
                }
                else if (iSubType == 2)
                {
                    CreateEffect(MODEL_STONE_COFFIN + 1, VertexTransform[i][vi], angle, Light, 3);
                }
                else if (iSubType == 3)
                {
                    CreateEffect(MODEL_STONE_COFFIN + WorldRandom() % 2, VertexTransform[i][vi],
                                 angle, Light, 4);
                }
                break;
            case MODEL_GATE:
                if (iSubType == 1)
                {
                    Vector(0.2f, 0.2f, 0.2f, Light);
                    if (Random.FpsCheck(5, 1.f))
                    {
                        CreateEffect(MODEL_GATE + 1, VertexTransform[i][vi], angle, Light, 2);
                    }
                    if (Random.FpsCheck(10, 1.f))
                    {
                        CreateEffect(MODEL_GATE, VertexTransform[i][vi], angle, Light, 2);
                    }
                }
                else if (iSubType == 0)
                {
                    Vector(0.2f, 0.2f, 0.2f, Light);
                    if (Random.FpsCheck(12, 1.f))
                    {
                        CreateEffect(MODEL_GATE + 1, VertexTransform[i][vi], angle, Light);
                    }
                    if (Random.FpsCheck(50, 1.f))
                    {
                        CreateEffect(MODEL_GATE, VertexTransform[i][vi], angle, Light);
                    }
                }
                break;
            case MODEL_BIG_STONE_PART1:
                if (Random.FpsCheck(3, 1.f))
                {
                    CreateEffect(MODEL_BIG_STONE_PART1 + WorldRandom() % 2, VertexTransform[i][vi],
                                 angle, Light, 1);
                }
                break;

            case MODEL_BIG_STONE_PART2:
                if (Random.FpsCheck(3, 1.f))
                {
                    CreateEffect(MODEL_BIG_STONE_PART1 + WorldRandom() % 2, VertexTransform[i][vi],
                                 angle, Light);
                }
                break;

            case MODEL_WALL_PART1:
                if (Random.FpsCheck(3, 1.f))
                {
                    CreateEffect(MODEL_WALL_PART1 + WorldRandom() % 2, VertexTransform[i][vi],
                                 angle, Light);
                }
                break;

            case MODEL_GATE_PART1:
                Vector(0.2f, 0.2f, 0.2f, Light);
                if (Random.FpsCheck(12, 1.f))
                {
                    CreateEffect(MODEL_GATE_PART1 + 1, VertexTransform[i][vi], angle, Light);
                }
                if (Random.FpsCheck(40, 1.f))
                {
                    CreateEffect(MODEL_GATE_PART1, VertexTransform[i][vi], angle, Light);
                }
                if (Random.FpsCheck(40, 1.f))
                {
                    CreateEffect(MODEL_GATE_PART1 + 2, VertexTransform[i][vi], angle, Light);
                }
                break;
            case MODEL_GOLEM_STONE:
                EmitGolemMeshDebris(VertexTransform[i][vi], iSubType, angle, Light, iEffectCount);
                break;
            case MODEL_SKIN_SHELL:
                if (Random.FpsCheck(8, 1.f))
                {
                    CreateEffect(MODEL_SKIN_SHELL, VertexTransform[i][vi], angle, Light, iSubType);
                }
                break;
            case BITMAP_LIGHT:
                Vector(0.08f, 0.08f, 0.08f, Light);
                if (iSubType == 0)
                {
                    CreateSprite(BITMAP_LIGHT, VertexTransform[i][vi], model.BodyScale, Light,
                                 nullptr);
                }
                else if (iSubType == 1)
                {
                    Vector(1.f, 0.8f, 0.2f, Light);
                    if ((j % 22) == 0)
                    {
                        auto *o = (OBJECT *)obj;

                        angle[0] = -(float)(WorldRandom() % 90);
                        angle[1] = 0.f;
                        angle[2] = Angle[2] + (float)(WorldRandom() % 120 - 60);
                        CreateJoint(BITMAP_JOINT_SPIRIT, VertexTransform[i][vi], o->Position, angle,
                                    13, o, 20.f, 0, 0);
                    }
                }
                break;
            case BITMAP_BUBBLE:
                Vector(1.f, 1.f, 1.f, Light);
                if (Random.FpsCheck(30, 1.f))
                {
                    CreateParticle(BITMAP_BUBBLE, VertexTransform[i][vi], angle, Light, 2);
                }
                break;
            }
        }
    }
}
