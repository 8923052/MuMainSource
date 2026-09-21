#include "domain/EffectsUpdate.h"
#include "support/CoreMath.h"
#include "session/SessionGameplay.h"
#include "domain/CharacterPresentation.h"
#include "domain/WorldSimulation.h"
#include "render/World.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationLoopFrame.h"
#include "render/Textures.h"
#include "session/SessionRender.h"
#include "session/SessionKeeper.h"
#include "render/ModelResources.h"
#include "data/Localization.h"
#include "domain/CharacterSystem.h"
#include "domain/ItemsSkills.h"
#include "data/WorldData.h"
#include "render/Terrain.h"
#include "domain/MovementAI.h"
#include "session/SessionNetwork.h"
#include "render/ModelGeometry.h"
#include "domain/MapSimulation.h"
#include "ui/session/UiSessionLogic.h"
#include "domain/Events.h"
#include "session/SessionPresentation.h"

void SessionGameplayUnit::EmitBossMeteors(OBJECT &object)
{
    for (auto birth : Emissions(FPS_ANIMATION_FACTOR))
    {
        vec3_t position;
        object.MotionTrace.Sample(WorldTime, birth.FrameFraction(), object.Position, position);
        position[0] += WorldRandom() % 1024 - 512;
        position[1] += WorldRandom() % 1024 - 512;
        CreateEffect(MODEL_FIRE, position, object.Angle, object.Light);
        PlayBuffer(SOUND_METEORITE01);
    }
}

void SessionGameplayUnit::AdvanceExplosionRings(PARTICLE &particle)
{
    constexpr float InitialLife = 30.f, LastAnimatedLife = 11.f;
    constexpr float BurstLife[] = {20.f, 10.f, 1.f};
    constexpr float Brightness[] = {1.f, 0.6f, 0.3f};
    constexpr int RingSamples = 18;
    particle.Frame = (InitialLife - std::max(LastAnimatedLife, particle.LifeTime)) * 0.5f;
    const float previousLife = particle.LifeTime + FPS_ANIMATION_FACTOR;
    for (int ring = 0; ring < 3; ++ring)
    {
        if (!Core::Time::Reaches(previousLife, FPS_ANIMATION_FACTOR, BurstLife[ring]))
            continue;
        auto birth =
            EmissionTime(FPS_ANIMATION_FACTOR - std::max(0.f, previousLife - BurstLife[ring]));
        vec3_t light{Brightness[ring], Brightness[ring], Brightness[ring]};
        for (int sample = 0; sample < RingSamples; ++sample)
        {
            vec3_t local{0.f, 200.f * (ring + 1), 0.f}, angle{0.f, 0.f, sample * 20.f}, position;
            float matrix[3][4];
            AngleMatrix(angle, matrix);
            VectorRotate(local, matrix, position);
            VectorAdd(particle.Position, position, position);
            CreateParticle(BITMAP_SMOKE, position, particle.Angle, light, 35, 2.5f);
            CreateParticle(BITMAP_EXPLOTION, position, particle.Angle, light, 1);
        }
    }
}

void SessionGameplayUnit::EmitFallingShinyChildren(PARTICLE &particle)
{
    constexpr float EmissionLife = 60.f, Damping = 1.f / 1.04f, EndLight = 0.2f;
    const float initialLife = particle.LifeTime + FPS_ANIMATION_FACTOR;
    const float offset = std::max(0.f, initialLife - EmissionLife);
    const float untilDark = particle.Light[0] > EndLight
                                ? std::log(particle.Light[0] / EndLight) / std::log(1.01f)
                                : 0.f;
    const float active = std::min({FPS_ANIMATION_FACTOR, std::max(0.f, initialLife), untilDark});
    const float emitting = std::max(0.f, active - offset);
    float matrix[3][4];
    vec3_t velocity;
    AngleMatrix(particle.Angle, matrix);
    VectorRotate(particle.Velocity, matrix, velocity);
    for (auto birth : Emissions(emitting / 5.0, emitting, offset))
    {
        const float elapsed = FPS_ANIMATION_FACTOR - birth.RemainingFrames();
        const float late = std::max(0.f, elapsed - offset);
        const float distance = elapsed - late + Core::Time::DampedDistance(Damping, late);
        vec3_t position, light;
        VectorCopy(particle.Position, position);
        if (particle.bEnableMove)
            VectorAddScaled(position, velocity, position, distance);
        float gravity = particle.Gravity, travel = 0.f;
        Core::Time::Advance(travel, gravity, 0.5f, late);
        position[2] -= 0.1f * travel;
        position[0] += WorldRandom() % 40 - 20;
        position[1] += WorldRandom() % 40 - 20;
        Vector(0.8f + WorldRandom() % 200 * 0.001f, 0.5f + WorldRandom() % 200 * 0.001f,
               0.1f + WorldRandom() % 100 * 0.001f, light);
        CreateParticle(BITMAP_SHINY, position, particle.Angle, light, 8);
    }
}

void SessionGameplayUnit::EmitLightningOrbParticles(OBJECT &effect)
{
    float matrix[3][4];
    vec3_t velocity;
    AngleMatrix(effect.Angle, matrix);
    VectorRotate(effect.Direction, matrix, velocity);
    const float active = std::min(FPS_ANIMATION_FACTOR, std::max(0.f, effect.LifeTime));
    constexpr float Boundaries[] = {14.f, 13.f, 5.f, 0.f};
    const int bands = effect.SubType == 0 ? 1 : 4;
    for (int band = 0; band < bands; ++band)
    {
        const float upper = band == 0 ? effect.LifeTime : Boundaries[band - 1];
        const float lower = effect.SubType == 0 ? 0.f : Boundaries[band];
        const float offset = std::max(0.f, effect.LifeTime - upper);
        const float duration = std::max(0.f, std::min(effect.LifeTime, upper) -
                                                 std::max(effect.LifeTime - active, lower));
        for (auto birth : Emissions(duration, duration, offset))
        {
            const float elapsed = FPS_ANIMATION_FACTOR - birth.RemainingFrames();
            vec3_t position, light;
            VectorAddScaled(effect.Position, velocity, position, elapsed);
            if (effect.SubType == 0)
            {
                Vector(0.4f, 0.4f, 1.5f, light);
                CreateParticle(BITMAP_MAGIC, position, effect.Angle, light, 0, 1.f);
                for (int i = 0; i < 3; ++i)
                    CreateParticle(BITMAP_SPARK + 1, position, effect.Angle, effect.Light, 13, 1.f);
                continue;
            }
            const float life = effect.LifeTime - elapsed;
            vec3_t fadingLight;
            VectorScale(effect.Light, std::pow(1.f / 1.08f, elapsed), fadingLight);
            if (life > 14.f)
                for (int i = 0; i < 5; ++i)
                    CreateParticle(BITMAP_SPARK + 1, position, effect.Angle, fadingLight, 20, 1.f);
            Vector(0.4f, 0.3f, 1.f, light);
            if (life > 13.f)
                for (int i = 0; i < 2; ++i)
                    CreateParticle(BITMAP_SHOCK_WAVE, position, effect.Angle, light, 0, 0.3f);
            Vector(0.2f, 0.2f, 1.f, light);
            for (int i = 0; i < 2; ++i)
                CreateEffect(MODEL_FENRIR_THUNDER, position, effect.Angle, light, 3, &effect);
            if (life <= 5.f)
                for (int i = 0; i < 2; ++i)
                    CreateParticle(BITMAP_SMOKE, position, effect.Angle, fadingLight, 40);
        }
    }
}

void SessionLegacyCalls::CreateMagicShiny(CHARACTER *character, int hand)
{
    sessionKeeper_.Gameplay()->CreateMagicShiny(character, hand);
}

void SessionLegacyCalls::CreateTeleportBegin(OBJECT *object)
{
    sessionKeeper_.Gameplay()->CreateTeleportBegin(object);
}

void SessionLegacyCalls::CreateTeleportEnd(OBJECT *object)
{
    sessionKeeper_.Gameplay()->CreateTeleportEnd(object);
}

void SessionLegacyCalls::CreateArrow(CHARACTER *character, OBJECT *object, OBJECT *target,
                                     WORD skillIndex, WORD skill, WORD skillKey)
{
    sessionKeeper_.Gameplay()->CreateArrow(character, object, target, skillIndex, skill, skillKey);
}

void SessionLegacyCalls::CreateArrows(CHARACTER *character, OBJECT *object, OBJECT *target,
                                      WORD skillIndex, WORD skill, WORD skillKey)
{
    sessionKeeper_.Gameplay()->CreateArrows(character, object, target, skillIndex, skill, skillKey);
}

void SessionGameplayUnit::CreateMagicShiny(CHARACTER *c, int Hand)
{
    OBJECT *o = &c->Object;
    BMD *b = &Models[o->Type];
    vec3_t p, Position;
    Vector(0.f, 0.f, 0.f, p);
    //for(int i=0;i<1;i++)
    {
        b->TransformPosition(o->BoneTransform[c->Weapon[Hand].LinkBone], p, Position, true);
        //VectorCopy(o->Position,Position);
        //Position[2] += 140.f;
        vec3_t Light;
        //Vector(o->Alpha,o->Alpha,o->Alpha,Light);
        Vector(1.f, 0.5f, 0.2f, Light);
        CreateParticle(BITMAP_SHINY + 1, Position, o->Angle, Light, Hand + 0, 0.f, o);
        CreateParticle(BITMAP_SHINY + 1, Position, o->Angle, Light, Hand + 2, 0.f, o);
    }
}

void SessionGameplayUnit::CreateTeleportBegin(OBJECT *o)
{
    SetAttackSpeed();
    SetAction(o, PLAYER_SKILL_TELEPORT);
    o->AlphaTarget = 0.f;
    o->Teleport = TELEPORT_BEGIN;
    CreateEffect(BITMAP_SPARK + 1, o->Position, o->Angle, o->Light);
    PlayBuffer(SOUND_MAGIC);
}

void SessionGameplayUnit::CreateTeleportEnd(OBJECT *o)
{
    SetAttackSpeed();
    SetAction(o, PLAYER_SKILL_TELEPORT);
    o->AnimationFrame = 5.f;
    o->Teleport = TELEPORT_END;
    o->AlphaTarget = 1.f;
    CreateEffect(BITMAP_SPARK + 1, o->Position, o->Angle, o->Light);
    PlayBuffer(SOUND_MAGIC);
}

void SessionGameplayUnit::CreateArrow(CHARACTER *c, OBJECT *o, OBJECT *to, WORD SkillIndex,
                                      WORD Skill, WORD SKKey)
{
    vec3_t ArrowPos;
    VectorCopy(o->Position, ArrowPos);
    if (c->Helper.Type == MODEL_HORN_OF_FENRIR)
    {
        ArrowPos[2] += 30.0f; // slightly elevate when riding a fenrir
    }

    int SubType = 0;
    int Right = c->Weapon[0].Type;
    int Left = c->Weapon[1].Type;
    if (c == Hero)
    {
        Right = CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT].Type;
        Left = CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT].Type;
        if (Right != -1)
            Right += MODEL_ITEM;
        if (Left != -1)
            Left += MODEL_ITEM;
    }

    if (SKKey == AT_SKILL_PENETRATION || SKKey == AT_SKILL_PENETRATION_STR)
    {
        SubType = 2;
        PlayBuffer(SOUND_BOW01, o);
    }
    CurrentSkill = SKKey;
    if (SKKey == AT_SKILL_ICE_ARROW || SKKey == AT_SKILL_ICE_ARROW_STR)
    {
        PlayBuffer(SOUND_ICEARROW, o);
    }

    if (Skill == 2)
    {
        SubType = 99;
    }

    if (SKKey == AT_SKILL_DEEPIMPACT)
    {
        CreateEffect(MODEL_ARROW_IMPACT, ArrowPos, o->Angle, o->Light, 0, to, o->PKKey, SkillIndex,
                     Skill);
    }
    else
    {
        switch (Right)
        {
        case MODEL_CROSSBOW:
            CreateEffect(MODEL_ARROW_STEEL, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_GOLDEN_CROSSBOW:
            CreateEffect(MODEL_ARROW_STEEL, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_ARQUEBUS:
            CreateEffect(MODEL_ARROW_SAW, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_LIGHT_CROSSBOW:
            CreateEffect(MODEL_ARROW_LASER, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_SERPENT_CROSSBOW:
            CreateEffect(MODEL_ARROW_THUNDER, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_BLUEWING_CROSSBOW:
            CreateEffect(MODEL_ARROW_WING, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_AQUAGOLD_CROSSBOW:
            CreateEffect(MODEL_ARROW_BOMB, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_SAINT_CROSSBOW:
            CreateEffect(MODEL_ARROW_DOUBLE, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_DIVINE_CB_OF_ARCHANGEL:
            CreateEffect(MODEL_ARROW_BEST_CROSSBOW, ArrowPos, o->Angle, o->Light, SubType, o,
                         o->PKKey, SkillIndex, Skill);
            break;
        case MODEL_GREAT_REIGN_CROSSBOW:
            CreateEffect(MODEL_ARROW_DRILL, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        }
        switch (Left)
        {
        case MODEL_BOW:
            CreateEffect(MODEL_ARROW, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_SMALL_BOW:
            CreateEffect(MODEL_ARROW, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_ELVEN_BOW:
            CreateEffect(MODEL_ARROW_V, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_BATTLE_BOW:
            CreateEffect(MODEL_ARROW, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_TIGER_BOW:
            CreateEffect(MODEL_ARROW, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_SILVER_BOW:
            CreateEffect(MODEL_ARROW, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_CHAOS_NATURE_BOW:
            CreateEffect(MODEL_ARROW_NATURE, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_CELESTIAL_BOW:
            CreateEffect(MODEL_ARROW_HOLY, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_ARROW_VIPER_BOW:
            CreateEffect(MODEL_LACEARROW, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_SYLPH_WIND_BOW:
            CreateEffect(MODEL_ARROW_SPARK, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_ALBATROSS_BOW:
            CreateEffect(MODEL_ARROW_RING, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        case MODEL_STINGER_BOW:
            CreateEffect(MODEL_ARROW_DARKSTINGER, ArrowPos, o->Angle, o->Light, SubType, o,
                         o->PKKey, SkillIndex, Skill);
            break;
        case MODEL_AIR_LYN_BOW:
            CreateEffect(MODEL_ARROW_GAMBLE, ArrowPos, o->Angle, o->Light, SubType, o, o->PKKey,
                         SkillIndex, Skill);
            break;
        }
    }
}

void SessionGameplayUnit::CreateArrows(CHARACTER *c, OBJECT *o, OBJECT *to, WORD SkillIndex,
                                       WORD Skill, WORD SKKey)
{
    if (SKKey == AT_SKILL_PENETRATION || SKKey == AT_SKILL_PENETRATION_STR ||
        SKKey == AT_SKILL_ICE_ARROW || SKKey == AT_SKILL_ICE_ARROW_STR ||
        SKKey == AT_SKILL_DEEPIMPACT)
    {
        CreateArrow(c, o, to, SkillIndex, Skill, SKKey);
        CharacterMachine->PacketSerial++;
    }
    else
    {
        if (Skill == 1)
        {
            if (c->Weapon[0].Type == MODEL_DIVINE_CB_OF_ARCHANGEL ||
                c->Weapon[0].Type == MODEL_GREAT_REIGN_CROSSBOW ||
                c->Weapon[1].Type == MODEL_ALBATROSS_BOW ||
                c->Weapon[1].Type == MODEL_STINGER_BOW || c->Weapon[1].Type == MODEL_AIR_LYN_BOW)
            {
                o->Angle[2] += 5.f; //15.f;//7.5f;
                CreateArrow(c, o, to, SkillIndex, Skill, SKKey);
                o->Angle[2] += 10.f; //15.f;//7.5f;
                CreateArrow(c, o, to, SkillIndex, Skill, SKKey);
                o->Angle[2] -= 20.f; //45.f;//22.5f;
                CreateArrow(c, o, to, SkillIndex, Skill, SKKey);
                o->Angle[2] -= 10.f; //15.f;//7.5f;
                CreateArrow(c, o, to, SkillIndex, Skill, SKKey);
                o->Angle[2] += 30.f; //30.f;//15.f;
                CharacterMachine->PacketSerial++;
            }
            else
            {
                CreateArrow(c, o, to, SkillIndex, Skill, SKKey);
                o->Angle[2] += 15.f;
                CreateArrow(c, o, to, SkillIndex, Skill, SKKey);
                o->Angle[2] -= 30.f;
                CreateArrow(c, o, to, SkillIndex, Skill, SKKey);
                o->Angle[2] += 15.f;
                CharacterMachine->PacketSerial++;
            }
        }
        else
        {
            CreateArrow(c, o, to, SkillIndex, Skill, SKKey);
        }

        if (c->Weapon[0].Type == MODEL_GREAT_REIGN_CROSSBOW)
        {
            vec3_t p, Position, Light;
            BMD *b = &Models[o->Type];

            Vector(1.f, 1.f, 1.f, Light);
            Vector(0.f, 10.f, -130.f, p);
            b->TransformPosition(o->BoneTransform[c->Weapon[0].LinkBone], p, Position, true);

            vec3_t a;
            for (int i = 0; i < 15; ++i)
            {
                Vector(Random.RangeFloat(0.f, 360.f), 0.f, 0.f, a);
                if (Random.FpsCheck(2, 1.f))
                {
                    CreateJoint(BITMAP_JOINT_SPARK, Position, Position, a, 3);
                }
                CreateParticle(BITMAP_SPARK, Position, a, Light, 0);
            }
        }
    }
}

namespace
{
void AdvanceOpacityLight(PARTICLE &particle, float rate, float frames)
{
    while (frames > 0.f)
    {
        if (particle.AlphaNoiseFrames <= 0.f)
        {
            particle.AlphaNoiseRate = (std::max)(0.f, particle.Alpha - rate);
            particle.AlphaNoiseFrames = 1.f;
        }
        const float step = (std::min)(frames, particle.AlphaNoiseFrames);
        particle.Alpha = (std::max)(0.f, particle.Alpha - rate * step);
        VectorScale(particle.Light, std::pow(particle.AlphaNoiseRate, step), particle.Light);
        particle.AlphaNoiseFrames -= step;
        frames -= step;
    }
}

void AdvanceChangingSpark(PARTICLE &particle, float frames)
{
    const bool slowing = particle.SubType == 25;
    const float boundary = slowing ? 19.f : 9.f;
    const float oldLife = particle.LifeTime + frames;
    const bool change = frames > 0.f && oldLife > boundary && particle.LifeTime <= boundary &&
                        (slowing || particle.Rotation == 0.f);
    const float before = change ? std::clamp(oldLife - boundary, 0.f, frames) : frames;
    float matrix[3][4];
    vec3_t velocity;
    AngleMatrix(particle.Angle, matrix);
    VectorRotate(particle.Velocity, matrix, velocity);
    if (particle.bEnableMove)
        VectorAddScaled(particle.Position, velocity, particle.Position, before);
    if (!change)
        return;
    if (slowing)
        for (float &component : particle.Velocity)
            component -= 1.f;
    else
    {
        VectorScale(particle.Velocity, -1.f, particle.Velocity);
        particle.Rotation = 1.f;
    }
    VectorRotate(particle.Velocity, matrix, velocity);
    if (particle.bEnableMove)
        VectorAddScaled(particle.Position, velocity, particle.Position, frames - before);
}

void AdvanceParticleWind(SessionPhysicsStorage &physics, SessionRandom &random, float frames,
                         vec3_t travel)
{
    physics.particleWindUpdateFrames = frames;
    physics.particleWindSamples.clear();
    constexpr float Acceleration = 0.0006f, VelocityLimit = 0.6f, WindLimit = 1.7f;
    while (frames > 0.f)
    {
        if (physics.particleWindFrames <= 0.f)
        {
            for (int axis = 0; axis < 2; ++axis)
            {
                const float force = (int(random.Next() % 2001) - 1000) * Acceleration;
                physics.particleWindVelocity[axis] = std::clamp(
                    physics.particleWindVelocity[axis] + force, -VelocityLimit, VelocityLimit);
            }
            physics.particleWindFrames = 1.f;
        }
        const float step = (std::min)(frames, physics.particleWindFrames);
        for (int axis = 0; axis < 2; ++axis)
        {
            // The original tick updates wind before applying it to particles.
            const float tickWind =
                std::clamp(physics.particleWind[axis] +
                               physics.particleWindVelocity[axis] * physics.particleWindFrames,
                           -WindLimit, WindLimit);
            travel[axis] += tickWind * step;
            physics.particleWind[axis] =
                std::clamp(physics.particleWind[axis] + physics.particleWindVelocity[axis] * step,
                           -WindLimit, WindLimit);
        }
        physics.particleWindFrames -= step;
        frames -= step;
        physics.particleWindSamples.push_back(
            {physics.particleWindUpdateFrames - frames, travel[0], travel[1]});
    }
}

void AdvanceWindLight(PARTICLE &particle, SessionRandom &random, float frames,
                      const vec3_t windTravel, float scale)
{
    VectorAddScaled(particle.Position, windTravel, particle.Position, scale);
    particle.Position[2] += 5.f * scale * frames;
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
        {
            // These light subtypes own TurningForce as held horizontal drift.
            for (int axis = 0; axis < 2; ++axis)
                particle.TurningForce[axis] = float(int(random.Next() % 2001) - 1000) * 0.0004f;
            particle.MotionIntervalFrames = 1.f;
        }
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        for (int axis = 0; axis < 2; ++axis)
            particle.Position[axis] += particle.TurningForce[axis] * scale * step;
        particle.MotionIntervalFrames -= step;
        frames -= step;
    }
}

void AdvanceRisingLight(PARTICLE &particle, float frames, double endTime,
                        double millisecondsPerFrame)
{
    constexpr float MeanSpeed = 10.f, WaveSpeed = 5.f;
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
        {
            particle.MotionIntervalFrames = 1.f;
            const double untilEnd = frames - particle.MotionIntervalFrames;
            const double samplePhase =
                endTime + particle.LifeTime - untilEnd * (millisecondsPerFrame - 1.0);
            particle.TurningForce[0] = MeanSpeed + WaveSpeed * float(std::sin(samplePhase));
        }
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        particle.Position[2] += particle.TurningForce[0] * step;
        particle.MotionIntervalFrames -= step;
        frames -= step;
    }
}

void FollowParticleTarget(PARTICLE &particle)
{
    VectorSubtract(particle.Position, particle.StartPosition, particle.Position);
    VectorCopy(particle.Target->Position, particle.StartPosition);
    VectorAdd(particle.Position, particle.StartPosition, particle.Position);
}

void AdvanceAttachedFlame(PARTICLE &particle, float frames)
{
    constexpr float TravelSpeed = 8.f;
    float matrix[3][4];
    vec3_t local{0.f, -TravelSpeed, 0.f}, travel;
    AngleMatrix(particle.Angle, matrix);
    VectorRotate(local, matrix, travel);
    VectorAddScaled(particle.Position, travel, particle.Position, frames);
    FollowParticleTarget(particle);
}

float AdvanceGravityAfter(PARTICLE &particle, float acceleration, float frames)
{
    float travel = 0.f;
    Core::Time::Advance(travel, particle.Gravity, acceleration, frames);
    return travel + acceleration * frames;
}

void AdvanceAttachedFire(PARTICLE &particle, SessionRandom &random, float frames)
{
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
        {
            // Preserve the original integer division: a 0 or 1 kick, not a fraction.
            particle.TurningForce[0] = 0.004f + float((random.Next() % 60 + 60) / 100);
            particle.MotionIntervalFrames = 1.f;
        }
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        particle.Scale -= AdvanceGravityAfter(particle, particle.TurningForce[0], step) / 90.f;
        particle.MotionIntervalFrames -= step;
        frames -= step;
    }
    VectorAdd(particle.Target->Position, particle.StartPosition, particle.Position);
    particle.Position[2] += particle.Gravity * 10.f;
}

void AdvanceParticleRotation(PARTICLE &particle, SessionRandom &random, int base, int range,
                             float frames)
{
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
        {
            particle.TurningForce[0] = float(base + random.Next() % range);
            particle.MotionIntervalFrames = 1.f;
        }
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        particle.Rotation += particle.TurningForce[0] * step;
        particle.MotionIntervalFrames -= step;
        frames -= step;
    }
}

bool IsFireParticle(const PARTICLE &particle)
{
    return particle.Type == BITMAP_FIRE || particle.Type == BITMAP_FIRE + 1 ||
           particle.Type == BITMAP_FIRE + 2 || particle.Type == BITMAP_FIRE + 3;
}

float FireVelocityDamping(const PARTICLE &particle)
{
    if (particle.Type == BITMAP_FIRE + 1)
        return particle.SubType == 0 || particle.SubType == 1 || particle.SubType == 3 ||
                       particle.SubType == 7
                   ? 1.05f
                   : 1.f;
    switch (particle.SubType)
    {
    case 0:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        return 1.f;
    default:
        return 0.98f;
    }
}

void AdvanceDampedVelocity(PARTICLE &particle, float damping, float frames)
{
    if (particle.bEnableMove)
    {
        const float distance =
            damping == 1.f ? frames : Core::Time::DampedDistance(damping, frames);
        vec3_t local, travel;
        float matrix[3][4];
        VectorScale(particle.Velocity, distance, local);
        AngleMatrix(particle.Angle, matrix);
        VectorRotate(local, matrix, travel);
        VectorAdd(particle.Position, travel, particle.Position);
    }
    VectorScale(particle.Velocity, std::pow(damping, frames), particle.Velocity);
}

void AdvanceSlantingGravity(PARTICLE &particle, float acceleration, float horizontalScale,
                            float frames)
{
    const float travel = AdvanceGravityAfter(particle, acceleration, frames);
    particle.Position[0] += horizontalScale * travel;
    particle.Position[2] += travel;
}

void AdvanceRandomSlantingGravity(PARTICLE &particle, SessionRandom &random, float frames)
{
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
        {
            particle.TurningForce[0] = -0.2f * float(random.Next() % 2 + 1);
            particle.MotionIntervalFrames = 1.f;
        }
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        AdvanceSlantingGravity(particle, -0.1f, particle.TurningForce[0], step);
        particle.MotionIntervalFrames -= step;
        frames -= step;
    }
}

void AdvanceRandomSmokeGravity(PARTICLE &particle, SessionRandom &random, float frames)
{
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
        {
            particle.TurningForce[0] = float(random.Next() % 20) / 500.f;
            particle.TurningForce[1] = float(int(random.Next() % 100) - 20) / 100.f;
            particle.TurningForce[2] = float(int(random.Next() % 100) - 20) / 100.f;
            particle.ScalarNoiseRate = float(random.Next() % 10) / 1000.f;
            particle.MotionIntervalFrames = 1.f;
        }
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        particle.Position[0] += particle.TurningForce[1] * step;
        particle.Position[1] += particle.TurningForce[2] * step;
        particle.Position[2] += AdvanceGravityAfter(particle, particle.TurningForce[0], step);
        particle.Scale += particle.ScalarNoiseRate * step;
        particle.MotionIntervalFrames -= step;
        frames -= step;
    }
}

void AdvanceSmokeWaveGravity(PARTICLE &particle, SessionRandom &random, float acceleration,
                             float frames, double endTime, double millisecondsPerFrame)
{
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
        {
            particle.MotionIntervalFrames = 1.f;
            const double sampleTime = endTime - (frames - 1.f) * millisecondsPerFrame;
            particle.TurningForce[0] = 0.05f * float(std::sin(sampleTime));
            if (particle.SubType == 64)
                particle.ScalarNoiseRate = float(random.Next() % 15 + 15) * 0.01f;
        }
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        const float travel = AdvanceGravityAfter(particle, acceleration, step);
        particle.Position[0] += particle.TurningForce[0] * travel;
        particle.Position[2] += travel;
        if (particle.SubType == 64)
            particle.Scale += particle.ScalarNoiseRate * step;
        particle.MotionIntervalFrames -= step;
        frames -= step;
    }
}

void AdvanceExpandingSmoke(PARTICLE &particle, float frames)
{
    constexpr float Growth = 1.01f;
    const float travel = particle.Scale * Core::Time::DampedDistance(Growth, frames) +
                         AdvanceGravityAfter(particle, -0.05f, frames);
    particle.Position[0] += particle.TurningForce[0] * travel;
    particle.Position[1] += particle.TurningForce[1] * travel;
    particle.Scale *= std::pow(Growth, frames);
}

void AdvanceRisingSmokeScale(PARTICLE &particle, SessionRandom &random, float frames)
{
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
        {
            particle.ScalarNoiseRate = float(random.Next() % 15 + 4) * 0.001f;
            particle.TurningForce[0] = float(random.Next() % 10 + 10) * 0.1f * particle.Angle[0];
            particle.MotionIntervalFrames = 1.f;
        }
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        float travel = 0.f;
        Core::Time::Advance(travel, particle.Scale, particle.ScalarNoiseRate, step);
        particle.Position[2] += 6.f * (travel + particle.ScalarNoiseRate * step);
        particle.Rotation += particle.TurningForce[0] * step;
        particle.MotionIntervalFrames -= step;
        frames -= step;
    }
}

bool IsFlowerParticle(const PARTICLE &particle)
{
    return particle.Type == BITMAP_FLOWER01 || particle.Type == BITMAP_FLOWER01 + 1 ||
           particle.Type == BITMAP_FLOWER01 + 2;
}

void PrepareFlowerMotion(PARTICLE &particle, SessionRandom &random)
{
    Vector(0.f, 0.f, 0.f, particle.TurningForce);
    if (particle.Frame == 0.f && (particle.SubType == 0 || particle.SubType == 1))
    {
        const int range = particle.SubType == 0 ? 32 : 8;
        const float scale = particle.SubType == 0 ? 0.1f : 0.05f;
        for (int axis = 0; axis < 3; ++axis)
            particle.TurningForce[axis] =
                particle.SubType == 1 && axis == 2
                    ? -float(random.Next() % range) * scale
                    : float(int(random.Next() % range) - range / 2) * scale;
    }
    particle.Gravity = float(random.Next() % 16); // Held sprite rotation rate.
    particle.MotionIntervalFrames = 1.f;
}

void FlowerIntervalVelocity(const PARTICLE &particle, vec3_t velocity)
{
    // Hold the authored endpoint displacement through this random-force interval.
    const float elapsed = 1.f - particle.MotionIntervalFrames;
    vec3_t before;
    for (int axis = 0; axis < 3; ++axis)
    {
        before[axis] = particle.Velocity[axis] - particle.TurningForce[axis] * elapsed;
        velocity[axis] = before[axis] + particle.TurningForce[axis];
    }
    if (particle.bEnableMove)
    {
        float matrix[3][4];
        vec3_t rotated;
        AngleMatrix(particle.Angle, matrix);
        VectorRotate(before, matrix, rotated);
        VectorAdd(velocity, rotated, velocity);
    }
}

void AdvanceFlowerToGround(PARTICLE &particle, float frames, const World &world)
{
    vec3_t velocity;
    FlowerIntervalVelocity(particle, velocity);
    const auto contact = world.FirstTerrainContact(particle.Position, velocity, 0.f, frames);
    const float step = contact ? contact->frames : frames;
    VectorAddScaled(particle.Position, velocity, particle.Position, step);
    if (!contact)
    {
        VectorAddScaled(particle.Velocity, particle.TurningForce, particle.Velocity, step);
        return;
    }
    particle.Position[2] = contact->height;
    Vector(0.f, 0.f, 0.f, particle.Velocity);
    particle.Frame = 1.f;
}

void AdvanceFlowerParticle(PARTICLE &particle, SessionRandom &random, float frames,
                           const World &world)
{
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
            PrepareFlowerMotion(particle, random);
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        if (particle.Frame == 0.f)
            AdvanceFlowerToGround(particle, step, world);
        else
            particle.Position[2] =
                (std::max)(particle.Position[2],
                           world.SampleTerrainHeight(particle.Position[0], particle.Position[1]));
        particle.Rotation += particle.Gravity * step;
        particle.MotionIntervalFrames -= step;
        frames -= step;
    }
}

void AdvanceDampedDualMotion(PARTICLE &particle, float damping, bool worldAfterDamping,
                             float frames)
{
    vec3_t travel;
    VectorScale(particle.Velocity, Core::Time::DampedDistance(damping, frames), travel);
    VectorAddScaled(particle.Position, travel, particle.Position,
                    worldAfterDamping ? damping : 1.f);
    if (particle.bEnableMove)
    {
        float matrix[3][4];
        vec3_t rotated;
        AngleMatrix(particle.Angle, matrix);
        VectorRotate(travel, matrix, rotated);
        VectorAdd(particle.Position, rotated, particle.Position);
    }
    VectorScale(particle.Velocity, std::pow(damping, frames), particle.Velocity);
}

void AdvanceBouncingParticle(PARTICLE &particle, float frames, const World &world)
{
    if (frames <= 0.f)
        return;
    const bool lightBounce =
        particle.Type == BITMAP_CHERRYBLOSSOM_EVENT_FLOWER ||
        (particle.Type == BITMAP_SPARK + 1 && (particle.SubType == 20 || particle.SubType == 22 ||
                                               particle.SubType == 28 || particle.SubType == 29));
    const float gravityScale = lightBounce ? 0.5f : 1.f;
    const float clearance = lightBounce ? 3.f : 0.f;
    vec3_t velocity{};
    if (!lightBounce)
        VectorCopy(particle.Velocity, velocity);
    if (particle.bEnableMove)
    {
        float matrix[3][4];
        vec3_t rotated;
        AngleMatrix(particle.Angle, matrix);
        VectorRotate(particle.Velocity, matrix, rotated);
        VectorAdd(velocity, rotated, velocity);
    }
    const float gravity = lightBounce                                      ? 0.75f
                          : particle.SubType == 7                          ? 0.6f
                          : particle.SubType == 9                          ? 0.4f
                          : particle.SubType == 5 || particle.SubType == 6 ? 2.3f
                                                                           : 2.f;
    const float restitution = lightBounce ? 0.3f : 0.6f;
    const float impactAge = lightBounce ? 2.f : 4.f;
    // These particle owners have no sprite-frame animation; Frame records settled contact.
    while (frames > 0.f)
    {
        if (particle.Frame == 1.f)
        {
            VectorAddScaled(particle.Position, velocity, particle.Position, frames);
            particle.Position[2] =
                world.SampleTerrainHeight(particle.Position[0], particle.Position[1]) + clearance;
            particle.Gravity = (-velocity[2] - gravity * 0.5f) / gravityScale;
            particle.LifeTime -= impactAge * frames;
            return;
        }
        float verticalVelocity = gravityScale * particle.Gravity + velocity[2];
        vec3_t motion{velocity[0], velocity[1], verticalVelocity + gravity * 0.5f};
        const auto contact =
            world.FirstTerrainContact(particle.Position, motion, -gravity, frames, clearance);
        const float step = contact ? contact->frames : frames;
        particle.Position[0] += velocity[0] * step;
        particle.Position[1] += velocity[1] * step;
        Core::Time::Advance(particle.Position[2], verticalVelocity, -gravity, step);
        particle.Gravity = (verticalVelocity - velocity[2]) / gravityScale;
        if (!contact)
            return;
        particle.Position[2] = contact->height;
        const float incoming = verticalVelocity + gravity * 0.5f - contact->surfaceVelocity;
        const float rebound = (std::max)(0.f, -incoming * restitution);
        particle.Gravity =
            (rebound + contact->surfaceVelocity - gravity * 0.5f - velocity[2]) / gravityScale;
        particle.LifeTime -= impactAge;
        frames -= step;
        const float resolution =
            std::nextafter(contact->height, std::numeric_limits<float>::infinity()) -
            contact->height;
        if (rebound * rebound / (2.f * gravity) <= resolution)
            particle.Frame = 1.f;
    }
}

void PrepareHoverSpark(PARTICLE &particle, float verticalDrift, float ground)
{
    constexpr float FallingInitial = 77.f, Rising = 88.f, Falling = 99.f;
    if (particle.Position[2] <= ground)
        particle.Frame = Rising;
    const bool rising = particle.Frame == Rising;
    const float rate = rising ? 1.f / 1.2f : particle.Frame == FallingInitial ? 1.03f : 1.2f;
    const float speed =
        rising ? (std::max)(0.1f, particle.Gravity * rate) : particle.Gravity * rate;
    particle.TurningForce[0] = verticalDrift + (rising ? speed : -speed);
    particle.TurningForce[1] = speed;
    particle.TurningForce[2] = rising && speed <= 0.1f ? Falling : particle.Frame;
    particle.MotionIntervalFrames = 1.f;
}

void AdvanceHoverSpark(PARTICLE &particle, float frames, const World &world)
{
    frames = std::clamp(particle.LifeTime + frames, 0.f, frames);
    if (frames <= 0.f)
        return;
    constexpr float Clearance = 2.f, Rising = 88.f;
    vec3_t drift{};
    if (particle.bEnableMove)
    {
        float matrix[3][4];
        AngleMatrix(particle.Angle, matrix);
        VectorRotate(particle.Velocity, matrix, drift);
    }
    while (frames > 0.f)
    {
        const float ground =
            world.SampleTerrainHeight(particle.Position[0], particle.Position[1]) + Clearance;
        particle.Position[2] = (std::max)(particle.Position[2], ground);
        if (particle.MotionIntervalFrames <= 0.f)
        {
            PrepareHoverSpark(particle, drift[2], ground);
            particle.ScalarNoiseRate = 0.f; // Ground-follow state for this steering interval.
        }
        float step = (std::min)(frames, particle.MotionIntervalFrames);
        vec3_t motion{drift[0], drift[1], particle.TurningForce[0]};
        auto contact =
            particle.ScalarNoiseRate == 0.f
                ? world.FirstTerrainContact(particle.Position, motion, 0.f, step, Clearance)
                : std::optional<TerrainContact>{};
        if (contact && contact->frames == 0.f && particle.Frame == Rising)
        {
            // The slope rises faster than this interval's lift. Follow it until steering refreshes.
            particle.ScalarNoiseRate = 1.f;
            contact.reset();
        }
        if (contact)
            step = contact->frames;
        VectorAddScaled(particle.Position, motion, particle.Position, step);
        if (particle.ScalarNoiseRate != 0.f)
            particle.Position[2] =
                world.SampleTerrainHeight(particle.Position[0], particle.Position[1]) + Clearance;
        particle.Gravity +=
            (particle.TurningForce[1] - particle.Gravity) * (step / particle.MotionIntervalFrames);
        particle.MotionIntervalFrames -= step;
        frames -= step;
        if (contact)
        {
            particle.Position[2] = contact->height;
            particle.Frame = Rising;
            particle.MotionIntervalFrames = 0.f;
        }
        else if (particle.MotionIntervalFrames <= 0.f)
            particle.Frame = particle.TurningForce[2];
    }
}

void AdvanceFadingCloud(PARTICLE &particle, SessionRandom &random, float frames)
{
    constexpr float Damping = 0.7f, FadeStartLife = 41.f;
    const float late = std::clamp(FadeStartLife - particle.LifeTime, 0.f, frames);
    const float early = frames - late;
    vec3_t travel{particle.Velocity[0] * early, particle.Velocity[1] * early,
                  particle.Velocity[2] * frames};
    for (int axis = 0; axis < 2; ++axis)
        Core::Time::AdvanceDamped(travel[axis], particle.Velocity[axis], Damping, 0.f, late);
    if (particle.bEnableMove)
    {
        float matrix[3][4];
        vec3_t rotated;
        AngleMatrix(particle.Angle, matrix);
        VectorRotate(travel, matrix, rotated);
        VectorAdd(particle.Position, rotated, particle.Position);
    }
    particle.Position[2] += 0.2f * late;
    particle.Scale += 0.003f * early;
    if (particle.Alpha < 1.f)
        particle.Alpha = (std::min)(1.f, particle.Alpha + 0.2f * early);
    float remaining = late;
    while (remaining > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
        {
            particle.ScalarNoiseRate = float(random.Next() % 10) * 0.01f;
            particle.MotionIntervalFrames = 1.f;
        }
        const float step = (std::min)(remaining, particle.MotionIntervalFrames);
        particle.Alpha = (std::max)(0.f, particle.Alpha - particle.ScalarNoiseRate * step);
        particle.MotionIntervalFrames -= step;
        remaining -= step;
    }
}

float RandomScalarTravel(float &remaining, float &rate, SessionRandom &random, float frames,
                         int range, int offset, float unit)
{
    float travel = 0.f;
    while (frames > 0.f)
    {
        if (remaining <= 0.f)
        {
            rate = float(int(random.Next() % range) + offset) * unit;
            remaining = 1.f;
        }
        const float step = (std::min)(frames, remaining);
        travel += rate * step;
        remaining -= step;
        frames -= step;
    }
    return travel;
}

std::uint64_t NextParticleRandom(std::uint64_t &state, SessionRandom &random)
{
    if (state == 0)
        return random.Next();
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return state;
}

float ParticleRandomScalarTravel(std::uint64_t &state, float &remaining, float &rate,
                                 SessionRandom &random, float frames, int range, int offset,
                                 float unit)
{
    constexpr float BoundaryTolerance = 0.0001f;
    float travel = 0.f;
    while (frames > 0.f)
    {
        if (remaining <= BoundaryTolerance)
        {
            rate = float(int(NextParticleRandom(state, random) % range) + offset) * unit;
            remaining = 1.f;
        }
        const float step = (std::min)(frames, remaining);
        travel += rate * step;
        remaining -= step;
        frames -= step;
        if (remaining < BoundaryTolerance)
            remaining = 0.f;
        if (frames < BoundaryTolerance)
            frames = 0.f;
    }
    return travel;
}

float ParticleScaleTravel(PARTICLE &particle, SessionRandom &random, float frames, int range,
                          int offset, float unit)
{
    return ParticleRandomScalarTravel(particle.ScaleRandomState, particle.ScaleNoiseFrames,
                                      particle.ScaleNoiseRate, random, frames, range, offset, unit);
}

void AdvanceParticleAlpha(PARTICLE &particle, SessionRandom &random, float frames, float fadeLife,
                          float fadeRate, int riseRange, int riseOffset, float riseUnit,
                          float maximum = 1.f)
{
    const float fading = std::clamp(fadeLife - particle.LifeTime, 0.f, frames);
    const float rising = frames - fading;
    if (rising > 0.f)
    {
        if (particle.Alpha < maximum || particle.MotionRandomSeed != 0)
            particle.Alpha += ParticleRandomScalarTravel(
                particle.AlphaRandomState, particle.AlphaNoiseFrames, particle.AlphaNoiseRate,
                random, rising, riseRange, riseOffset, riseUnit);
        particle.Alpha = (std::min)(maximum, particle.Alpha);
    }
    particle.Alpha = (std::max)(0.f, particle.Alpha - fadeRate * fading);
}

void AdvanceCloudDamping(PARTICLE &particle, SessionRandom &random, float frames)
{
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
        {
            particle.ScaleNoiseRate = 0.98f + float(int(random.Next() % 20) - 10) * 0.002f;
            for (int axis = 0; axis < 3; ++axis)
                particle.TurningForce[axis] = 0.95f + float(int(random.Next() % 20) - 10) * 0.002f;
            particle.AlphaNoiseRate = 0.95f + float(int(random.Next() % 20) - 10) * 0.002f;
            particle.MotionIntervalFrames = 1.f;
        }
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        particle.Scale *= std::pow(particle.ScaleNoiseRate, step);
        particle.Alpha *= std::pow(particle.AlphaNoiseRate, step);
        for (int axis = 0; axis < 3; ++axis)
            particle.Light[axis] *= std::pow(particle.TurningForce[axis], step);
        particle.MotionIntervalFrames -= step;
        frames -= step;
    }
}

void AdvanceGhostRotation(PARTICLE &particle, SessionRandom &random, float frames, double worldTime,
                          double referenceMilliseconds)
{
    double sampleTime = worldTime - frames * referenceMilliseconds;
    while (frames > 0.f)
    {
        if (particle.RotationNoiseFrames <= 0.f)
        {
            const float speed = 0.2f + float(random.Next() % 4) * 0.1f;
            const double wave = std::sin((sampleTime + referenceMilliseconds) * 0.001);
            particle.ScalarNoiseRate = wave > 0.0 ? speed : -speed;
            particle.RotationNoiseFrames = 1.f;
        }
        const float step = (std::min)(frames, particle.RotationNoiseFrames);
        particle.Rotation += particle.ScalarNoiseRate * step;
        particle.RotationNoiseFrames -= step;
        sampleTime += step * referenceMilliseconds;
        frames -= step;
    }
}

void AdvanceWaterfallRotation(PARTICLE &particle, float frames, double worldTime,
                              double referenceMilliseconds)
{
    double sampleTime = worldTime - frames * referenceMilliseconds;
    while (frames > 0.f)
    {
        if (particle.RotationNoiseFrames <= 0.f)
        {
            const double endpoint = sampleTime + referenceMilliseconds;
            particle.ScalarNoiseRate = float(std::fmod(std::floor(endpoint), 360.0)) * 0.01f;
            particle.RotationNoiseFrames = 1.f;
        }
        const float step = (std::min)(frames, particle.RotationNoiseFrames);
        particle.Rotation += particle.ScalarNoiseRate * step;
        particle.RotationNoiseFrames -= step;
        sampleTime += step * referenceMilliseconds;
        frames -= step;
    }
}

void AdvanceRandomParticleRotation(PARTICLE &particle, SessionRandom &random, float frames)
{
    constexpr int FullTurn = 360;
    while (frames > 0.f)
    {
        if (particle.RotationNoiseFrames <= 0.f)
        {
            particle.Rotation = float(random.Next() % FullTurn);
            particle.RotationNoiseFrames = 1.f;
        }
        const float step = (std::min)(frames, particle.RotationNoiseFrames);
        particle.RotationNoiseFrames -= step;
        frames -= step;
    }
}

void AdvanceFlareWorldMotion(PARTICLE &particle, float referenceLife, float frames)
{
    const float damping = referenceLife < 20.f ? 0.6f : 1.f;
    const float distance = Core::Time::DampedDistance(damping, frames);
    for (int axis = 0; axis < 2; ++axis)
    {
        particle.Position[axis] += particle.TurningForce[axis] * distance;
        particle.TurningForce[axis] *= std::pow(damping, frames);
    }
    float travel = 0.f;
    Core::Time::Advance(travel, particle.TurningForce[2], -particle.Gravity, frames);
    particle.Position[2] += travel - particle.Gravity * frames;
}

void AdvanceRotatingFlare(PARTICLE &particle, float frames)
{
    constexpr float Acceleration = 0.05f;
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
            particle.MotionIntervalFrames = 1.f;
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        const float referenceLife = particle.LifeTime + frames - particle.MotionIntervalFrames;
        const float angleRate = 0.5f * referenceLife;
        const float elapsed = 1.f - particle.MotionIntervalFrames;
        vec3_t angle{particle.Angle[0] - angleRate * elapsed,
                     particle.Angle[1] - angleRate * elapsed, particle.Angle[2]};
        vec3_t velocity{particle.Velocity[0] - Acceleration * elapsed,
                        particle.Velocity[1] - Acceleration * elapsed, particle.Velocity[2]};
        if (particle.bEnableMove)
        {
            float matrix[3][4];
            vec3_t travel;
            AngleMatrix(angle, matrix);
            VectorRotate(velocity, matrix, travel);
            VectorAddScaled(particle.Position, travel, particle.Position, step);
        }
        AdvanceFlareWorldMotion(particle, referenceLife, step);
        const float alphaRate = referenceLife < 10.f ? 0.1f : 0.f;
        if (particle.SubType == 7)
        {
            const float alpha =
                (std::max)(0.f, particle.Alpha - alphaRate * particle.MotionIntervalFrames);
            const float fade = std::pow(alpha, step);
            const float glow =
                referenceLife / 8.f * 0.02f * Core::Time::DampedDistance(alpha, step);
            for (float &light : particle.Light)
                light = light * fade + glow;
        }
        particle.Alpha -= alphaRate * step;
        particle.Velocity[0] += Acceleration * step;
        particle.Velocity[1] += Acceleration * step;
        particle.Angle[0] += angleRate * step;
        particle.Angle[1] += angleRate * step;
        particle.MotionIntervalFrames -= step;
        frames -= step;
    }
}

void PrepareBubbleMotion(PARTICLE &particle, SessionRandom &random)
{
    const bool verticalOnly = particle.SubType == 2 || particle.SubType == 4;
    const bool narrow = particle.SubType == 1;
    for (int axis = 0; axis < 2; ++axis)
        particle.TurningForce[axis] =
            verticalOnly ? 0.f
                         : float(int(random.Next() % (narrow ? 30 : 20)) - (narrow ? 15 : 10)) *
                               (narrow ? 1.f : 2.5f);
    particle.TurningForce[2] =
        float(random.Next() % 20 + 10) * (verticalOnly || narrow ? 0.3f : 2.5f);
    particle.MotionIntervalFrames = 1.f;
}

bool AdvanceBubble(PARTICLE &particle, SessionRandom &random, float frames)
{
    switch (particle.SubType)
    {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
        break;
    default:
        return false;
    }
    const bool verticalOnly = particle.SubType == 2 || particle.SubType == 4;
    const float scaleRate = particle.SubType == 1 ? 0.002f : verticalOnly ? 0.005f : 0.f;
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
            PrepareBubbleMotion(particle, random);
        float step = (std::min)(frames, particle.MotionIntervalFrames);
        const float basis = particle.SubType == 3 ? particle.Gravity
                            : verticalOnly        ? 1.f
                                                  : particle.Scale;
        const bool bounded = particle.SubType == 4 || particle.SubType == 5;
        const float limit = particle.SubType == 4 ? 350.f : 550.f;
        const float remainingDistance = limit - particle.Position[2];
        const float speed = particle.TurningForce[2] * basis;
        const bool arrives = bounded && remainingDistance <= speed * step;
        if (arrives)
            step = (std::max)(0.f, remainingDistance / speed);
        const float distance =
            step * (basis + (particle.SubType == 1 ? scaleRate * (step - 1.f) * 0.5f : 0.f));
        VectorAddScaled(particle.Position, particle.TurningForce, particle.Position, distance);
        particle.Scale += scaleRate * step;
        if (!verticalOnly)
            particle.Frame += step;
        particle.MotionIntervalFrames -= step;
        frames -= step;
        if (arrives)
        {
            particle.Position[2] = limit;
            return true;
        }
    }
    return false;
}

bool OwnsWaterfallMotion(const PARTICLE &particle)
{
    return particle.Type == BITMAP_WATERFALL_1 || particle.Type == BITMAP_WATERFALL_3 ||
           particle.Type == BITMAP_WATERFALL_4 || particle.Type == BITMAP_WATERFALL_5 ||
           particle.Type == BITMAP_WATERFALL_2;
}

float WaterfallAcceleration(const PARTICLE &particle)
{
    if (particle.Type == BITMAP_WATERFALL_1)
        return particle.SubType == 1 ? -0.3f : 0.1f;
    if (particle.Type == BITMAP_WATERFALL_2)
        return particle.SubType == 3 ? 0.08f : 0.1f;
    if (particle.Type == BITMAP_WATERFALL_5)
    {
        switch (particle.SubType)
        {
        case 0:
        case 3:
        case 5:
        case 9:
            return 0.1f;
        case 2:
            return -1.f;
        case 8:
            return -0.6f;
        default:
            return 0.f;
        }
    }
    switch (particle.SubType)
    {
    case 2:
    case 5:
    case 6:
    case 10:
    case 11:
    case 12:
    case 13:
    case 15:
        return 0.f;
    case 3:
        return -0.05f;
    case 7:
        return 0.01f;
    case 8:
        return -0.6f;
    case 14:
        return -3.1f; // Both the subtype and shared tail accelerate this variant.
#ifdef ASG_ADD_MAP_KARUTAN
    case 16:
        return -0.1f;
#endif
    default:
        return -2.5f;
    }
}

void AdvanceWaterfallMotion(PARTICLE &particle, float frames)
{
    const float acceleration = WaterfallAcceleration(particle);
    vec3_t travel{particle.Velocity[0] * frames, particle.Velocity[1] * frames, 0.f};
    Core::Time::Advance(travel[2], particle.Velocity[2], acceleration, frames);
    if (particle.bEnableMove)
    {
        float matrix[3][4];
        vec3_t rotated;
        AngleMatrix(particle.Angle, matrix);
        VectorRotate(travel, matrix, rotated);
        VectorAdd(particle.Position, rotated, particle.Position);
    }
    if (particle.Type == BITMAP_WATERFALL_2 && particle.SubType == 5)
    {
        // Its extra world step uses velocity after the authored acceleration.
        travel[2] += (particle.Gravity + acceleration) * frames;
        VectorAdd(particle.Position, travel, particle.Position);
    }
}

void AdvanceRisingWaterfall(PARTICLE &particle, SessionRandom &random, float frames)
{
    const float initialLife = particle.LifeTime + frames;
    frames = (std::min)(frames, (std::max)(0.f, initialLife));
    float elapsed = 0.f;
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
        {
            // The final live reference sample is life 1; life 0 has no visible inverse-age size.
            const float sampleLife = (std::max)(1.f, initialLife - elapsed - 1.f);
            particle.ScalarNoiseRate = float(random.Next() % 5) + 0.3f * sampleLife;
            // This subtype overwrote generic velocity motion. Cache only its visible rates.
            particle.Velocity[0] = 0.03f + 2.f / sampleLife;
            particle.Velocity[1] = float(random.Next() % 5) + 10.f;
            particle.Velocity[2] = sampleLife < 10.f ? 1.f / 1.1f : 1.f;
            VectorSubtract(particle.StartPosition, particle.Position, particle.TurningForce);
            particle.TurningForce[2] +=
                particle.Velocity[1] + particle.Gravity + particle.ScalarNoiseRate;
            particle.MotionIntervalFrames = 1.f;
        }
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        VectorAddScaled(particle.Position, particle.TurningForce, particle.Position, step);
        particle.StartPosition[2] += particle.Velocity[1] * step;
        particle.Gravity += particle.ScalarNoiseRate * step;
        particle.Scale += particle.Velocity[0] * step;
        particle.Rotation += 50.f * step;
        for (int axis = 0; axis < 3; ++axis)
            particle.Light[axis] *=
                std::pow((axis == 0 ? 0.8f : 0.77f) * particle.Velocity[2], step);
        particle.MotionIntervalFrames -= step;
        elapsed += step;
        frames -= step;
    }
}

void AdvanceWaterfallMist(PARTICLE &particle, SessionRandom &random, float frames)
{
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
            particle.MotionIntervalFrames = 1.f;
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        AdvanceWaterfallMotion(particle, step);
        particle.MotionIntervalFrames -= step;
        frames -= step;
        // The authored update chooses horizontal velocity after moving.
        if (particle.MotionIntervalFrames <= 0.f)
            for (int axis = 0; axis < 2; ++axis)
                particle.Velocity[axis] = float(int(random.Next() % 20) - 10) * 0.1f;
    }
}

void AdvanceParticleGravity(PARTICLE &particle, float direction, float acceleration, float frames)
{
    float travel = 0.f;
    Core::Time::Advance(travel, particle.Gravity, acceleration, frames);
    particle.Position[2] += direction * travel;
}

void AdvanceHorizontalDrift(PARTICLE &particle, SessionRandom &random, int range, int offset,
                            float frames, float unit = 1.f)
{
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
        {
            for (int axis = 0; axis < 2; ++axis)
                particle.TurningForce[axis] = float(int(random.Next() % range) - offset) * unit;
            particle.MotionIntervalFrames = 1.f;
        }
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        for (int axis = 0; axis < 2; ++axis)
            particle.Position[axis] += particle.TurningForce[axis] * step;
        particle.MotionIntervalFrames -= step;
        frames -= step;
    }
}

bool IsDriftingSmoke(const PARTICLE &particle)
{
    return particle.Type == BITMAP_ADV_SMOKE + 1 ||
           (particle.Type == BITMAP_CLOUD && (particle.SubType == 6 || particle.SubType == 14));
}

bool IsFallingShiny(const PARTICLE &particle)
{
    return particle.Type == BITMAP_SHINY &&
           (particle.SubType == 6 || particle.SubType == 8 || particle.SubType == 9);
}

void AdvanceFallingShiny(PARTICLE &particle, SessionRandom &random, float frames)
{
    constexpr float FadeLife = 60.f, Damping = 1.f / 1.04f;
    const float late = std::clamp(FadeLife - particle.LifeTime, 0.f, frames);
    const float distance = frames - late + Core::Time::DampedDistance(Damping, late);
    if (particle.bEnableMove)
    {
        vec3_t local, travel;
        float matrix[3][4];
        VectorScale(particle.Velocity, distance, local);
        AngleMatrix(particle.Angle, matrix);
        VectorRotate(local, matrix, travel);
        VectorAdd(particle.Position, travel, particle.Position);
    }
    VectorScale(particle.Velocity, std::pow(Damping, late), particle.Velocity);
    AdvanceParticleGravity(particle, -0.1f, 0.5f, late);
    float remaining = late;
    while (remaining > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
        {
            particle.TurningForce[0] = float(random.Next() % 2);
            particle.MotionIntervalFrames = 1.f;
        }
        const float step = (std::min)(remaining, particle.MotionIntervalFrames);
        particle.MotionIntervalFrames -= step;
        remaining -= step;
    }
}

void AdvanceDescendingShiny(PARTICLE &particle, SessionRandom &random, float frames)
{
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
        {
            particle.ScalarNoiseRate = -float(random.Next() % 8) * 0.05f;
            particle.MotionIntervalFrames = 1.f;
        }
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        vec3_t travel{particle.Velocity[0] * step, particle.Velocity[1] * step, 0.f};
        Core::Time::Advance(travel[2], particle.Velocity[2], particle.ScalarNoiseRate, step);
        if (particle.bEnableMove)
        {
            float matrix[3][4];
            vec3_t rotated;
            AngleMatrix(particle.Angle, matrix);
            VectorRotate(travel, matrix, rotated);
            VectorAdd(particle.Position, rotated, particle.Position);
        }
        travel[2] +=
            particle.ScalarNoiseRate * step; // Extra world movement uses post-update velocity.
        VectorAdd(particle.Position, travel, particle.Position);
        particle.MotionIntervalFrames -= step;
        frames -= step;
    }
}

bool OwnsIntegratedParticleMotion(const PARTICLE &particle)
{
    return particle.Type == BITMAP_SMOKE || particle.Type == BITMAP_ADV_SMOKE ||
           IsDriftingSmoke(particle) || (particle.Type == BITMAP_FLAME && particle.SubType == 10) ||
           particle.Type == BITMAP_TRUE_FIRE || particle.Type == BITMAP_TRUE_BLUE ||
           OwnsWaterfallMotion(particle) || IsFallingShiny(particle) || IsFireParticle(particle) ||
           IsFlowerParticle(particle) || particle.Type == BITMAP_BLOOD ||
           particle.Type == BITMAP_BLOOD + 1 || particle.Type == BITMAP_SPARK ||
           particle.Type == BITMAP_SMOKE + 1 || particle.Type == BITMAP_SMOKE + 4 ||
           particle.Type == BITMAP_FIRECRACKER || particle.Type == BITMAP_LEAF_TOTEMGOLEM ||
           (particle.Type == BITMAP_CHERRYBLOSSOM_EVENT_FLOWER && particle.SubType == 0) ||
           (particle.Type == BITMAP_LIGHT + 2 &&
            (particle.SubType == 5 || particle.SubType == 6 || particle.SubType == 7)) ||
           (particle.Type == BITMAP_CLOUD && particle.SubType == 23) ||
           (particle.Type == BITMAP_SHINY && particle.SubType == 2) ||
           (particle.Type == BITMAP_SPARK + 1 &&
            (particle.SubType == 9 || particle.SubType == 16 || particle.SubType == 18 ||
             particle.SubType == 20 || particle.SubType == 22 || particle.SubType == 23 ||
             particle.SubType == 24 || particle.SubType == 25 || particle.SubType == 28 ||
             particle.SubType == 29 || particle.SubType == 30 || particle.SubType == 31));
}

void AdvanceGrowingSmoke(PARTICLE &particle, float frames)
{
    constexpr float Growth = 1.03f;
    vec3_t travel, rotated;
    const float distance =
        Core::Time::DampedDistance(Growth, frames) * (Growth + (particle.bEnableMove ? 1.f : 0.f));
    VectorScale(particle.Velocity, distance, travel);
    float matrix[3][4];
    AngleMatrix(particle.Angle, matrix);
    VectorRotate(travel, matrix, rotated);
    VectorAdd(particle.Position, rotated, particle.Position);
    VectorScale(particle.Velocity, std::pow(Growth, frames), particle.Velocity);
}

void AdvanceFadingSmoke(PARTICLE &particle, float frames)
{
    if (frames <= 0.f)
        return;
    constexpr float ActiveLife = 6.f, StopLife = 5.f, GravityRate = 0.02f;
    constexpr float VelocityDamping = 1.05f * 0.4f;
    const float startingLife = particle.LifeTime + frames;
    const float active = std::clamp(startingLife - ActiveLife, 0.f, frames);
    const float moving = std::clamp(startingLife - StopLife, 0.f, frames);
    const float speedScale = std::pow(VelocityDamping, active);
    const float distance =
        Core::Time::DampedDistance(VelocityDamping, active) + speedScale * (moving - active);
    if (particle.bEnableMove)
    {
        float matrix[3][4];
        vec3_t local, travel;
        AngleMatrix(particle.Angle, matrix);
        VectorScale(particle.Velocity, distance, local);
        VectorRotate(local, matrix, travel);
        VectorAdd(particle.Position, travel, particle.Position);
    }
    VectorScale(particle.Velocity, particle.LifeTime <= StopLife ? 0.f : speedScale,
                particle.Velocity);
    const float gravityTravel = active * (particle.Gravity + GravityRate * (active + 1.f) * 0.5f);
    particle.Gravity += GravityRate * frames;
    particle.Scale += gravityTravel;
    particle.Position[2] += gravityTravel * 20.f;
    if (particle.LifeTime > ActiveLife)
    {
        const float luminosity = particle.LifeTime / 24.f;
        Vector(luminosity, luminosity, luminosity, particle.Light);
        return;
    }
    if (startingLife >= ActiveLife)
        Vector(ActiveLife / 24.f, ActiveLife / 24.f, ActiveLife / 24.f, particle.Light);
    VectorScale(particle.Light, std::pow(0.5f, frames - active), particle.Light);
}

void AdvanceReleasingSmoke(PARTICLE &particle, float frames)
{
    constexpr float ReleaseLife = 23.f, ScaleRate = -0.08f, GravityPerScale = 0.1f;
    if (particle.LifeTime >= ReleaseLife)
    {
        VectorAdd(particle.Target->Position, particle.StartPosition, particle.Position);
        return;
    }
    if (particle.LifeTime + frames > ReleaseLife)
        VectorAdd(particle.Target->Position, particle.StartPosition, particle.Position);
    const float active = std::clamp(ReleaseLife - particle.LifeTime, 0.f, frames);
    AdvanceDampedVelocity(particle, 1.f, active);
    const float acceleration = GravityPerScale * particle.Scale;
    constexpr float Jerk = GravityPerScale * ScaleRate;
    const float travel = particle.Gravity * active + acceleration * active * (active + 1.f) * 0.5f +
                         Jerk * active * (active - 1.f) * (active + 1.f) / 6.f;
    particle.Gravity += acceleration * active + Jerk * active * (active - 1.f) * 0.5f;
    particle.Scale += ScaleRate * active;
    particle.Position[1] += travel;
    particle.Position[2] += travel;
}

void AcceleratedParticlePositionAt(const PARTICLE &particle, const vec3_t acceleration,
                                   float frames, vec3_t position)
{
    VectorCopy(particle.Position, position);
    if (!particle.bEnableMove)
        return;
    vec3_t travel;
    for (int axis = 0; axis < 3; ++axis)
        travel[axis] =
            frames * (particle.Velocity[axis] + acceleration[axis] * (frames - 1.f) * 0.5f);
    float matrix[3][4];
    vec3_t rotated;
    AngleMatrix(particle.Angle, matrix);
    VectorRotate(travel, matrix, rotated);
    VectorAdd(position, rotated, position);
}

void AdvanceAcceleratedVelocity(PARTICLE &particle, const vec3_t acceleration, float frames)
{
    AcceleratedParticlePositionAt(particle, acceleration, frames, particle.Position);
    VectorAddScaled(particle.Velocity, acceleration, particle.Velocity, frames);
}

void AdvanceWanderingSpark(PARTICLE &particle, SessionRandom &random, float frames)
{
    const float initialLife = particle.LifeTime + frames;
    float elapsed = 0.f;
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
        {
            Vector(0.f, 0.f, 0.f, particle.TurningForce);
            // This subtype loses two life units per reference tick; the force check sits between them.
            const float sampleLife = initialLife - 2.f * elapsed - 1.f;
            if (sampleLife <= 45.f)
            {
                particle.TurningForce[0] = std::sin(Q_PI / 180.f * float(random.Next() % 360));
                particle.TurningForce[1] = std::cos(Q_PI / 180.f * float(random.Next() % 360));
            }
            particle.MotionIntervalFrames = 1.f;
        }
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        AdvanceAcceleratedVelocity(particle, particle.TurningForce, step);
        particle.MotionIntervalFrames -= step;
        elapsed += step;
        frames -= step;
    }
}

void FadeGroundLeaf(PARTICLE &particle, float frames)
{
    particle.Alpha = (std::max)(0.f, particle.Alpha - 0.05f * frames);
    Vector(particle.Alpha, particle.Alpha, particle.Alpha, particle.Light);
}

void AdvanceTotemLeaf(PARTICLE &particle, float frames, const World &world)
{
    constexpr float Clearance = 20.f;
    while (frames > 0.f)
    {
        const float ground =
            world.SampleTerrainHeight(particle.Position[0], particle.Position[1]) + Clearance;
        if (particle.Position[2] <= ground && VectorLength(particle.Velocity) == 0.f)
        {
            particle.Position[2] = ground;
            FadeGroundLeaf(particle, frames);
            return;
        }
        if (particle.MotionIntervalFrames <= 0.f)
            particle.MotionIntervalFrames = 1.f;
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        vec3_t local, velocity{};
        VectorCopy(particle.Velocity, local);
        local[2] -= particle.Gravity * (1.f - particle.MotionIntervalFrames);
        if (particle.bEnableMove)
        {
            float matrix[3][4];
            AngleMatrix(particle.Angle, matrix);
            VectorRotate(local, matrix, velocity);
        }
        const auto contact =
            world.FirstTerrainContact(particle.Position, velocity, 0.f, step, Clearance);
        const float flight = contact ? contact->frames : step;
        VectorAddScaled(particle.Position, velocity, particle.Position, flight);
        particle.Scale += 0.01f * flight;
        if (contact)
        {
            particle.Position[2] = contact->height;
            Vector(0.f, 0.f, 0.f, particle.Velocity);
            FadeGroundLeaf(particle, frames - flight);
            return;
        }
        particle.Velocity[2] += particle.Gravity * step;
        particle.MotionIntervalFrames -= step;
        frames -= step;
    }
}

void AdvanceSmokeVelocity(PARTICLE &particle, float frames)
{
    if (particle.SubType == 36)
        return; // Its release owner consumes the active interval.
    float damping = 1.f;
    switch (particle.SubType)
    {
    case 33:
    case 49:
    case 62:
        damping = 0.001f;
        break;
    case 1:
    case 3:
    case 11:
    case 14:
    case 32:
    case 40:
    case 41:
    case 53:
    case 56:
    case 58:
        damping = 0.4f;
        break;
    }
    if (particle.SubType != 7 && particle.SubType != 63)
    {
        AdvanceDampedVelocity(particle, damping, frames);
        return;
    }
    vec3_t acceleration{0.f, particle.SubType == 7 ? -0.1f : 1.5f, 0.f};
    AdvanceAcceleratedVelocity(particle, acceleration, frames);
}

void AdvanceDampedParticleMotion(PARTICLE &particle, float frames)
{
    constexpr float Damping = 0.95f;
    const bool drifting = IsDriftingSmoke(particle);
    const bool horizontal = particle.Type == BITMAP_ADV_SMOKE && particle.SubType == 3;
    vec3_t travel{};
    Core::Time::AdvanceDamped(travel[0], particle.Velocity[0], Damping, 0.f, frames);
    Core::Time::AdvanceDamped(travel[1], particle.Velocity[1], Damping, horizontal ? 0.1f : 0.f,
                              frames);
    const bool fire = particle.Type == BITMAP_TRUE_FIRE || particle.Type == BITMAP_TRUE_BLUE;
    Core::Time::Advance(travel[2], particle.Velocity[2],
                        drifting             ? 0.6f
                        : horizontal || fire ? 0.f
                                             : 0.3f,
                        frames);
    // Fire has the rotated step; advanced smoke also has a world-space step.
    if (particle.Type != BITMAP_TRUE_FIRE && particle.Type != BITMAP_TRUE_BLUE)
        VectorAdd(particle.Position, travel, particle.Position);
    if (particle.bEnableMove)
    {
        float matrix[3][4];
        vec3_t rotated;
        AngleMatrix(particle.Angle, matrix);
        VectorRotate(travel, matrix, rotated);
        VectorAdd(particle.Position, rotated, particle.Position);
    }
}

void AdvanceSmokeDrift(PARTICLE &particle, SessionRandom &random, float frames)
{
    while (frames > 0.f)
    {
        if (particle.MotionIntervalFrames <= 0.f)
        {
            for (int axis = 0; axis < 3; ++axis)
                particle.TurningForce[axis] =
                    float(int(random.Next() % 4) - 2) * (axis == 2 ? 0.8f : 1.f);
            // Drifting smoke uses Gravity as its held sprite rotation rate.
            particle.Gravity = 1.f + float(random.Next() % 2);
            particle.MotionIntervalFrames = 1.f;
        }
        const float step = (std::min)(frames, particle.MotionIntervalFrames);
        VectorAddScaled(particle.Position, particle.TurningForce, particle.Position, step);
        particle.Rotation += particle.Gravity * step;
        particle.MotionIntervalFrames -= step;
        frames -= step;
    }
}

void AdvanceBlueFlare(PARTICLE &particle, float frames)
{
    if (frames <= 0.f)
        return;
    constexpr float Acceleration = 0.4f, SpeedLimit = 8.f;
    constexpr float FadeAge = 5.f, LateDamping = 1.f / 1.2f, Damping = 1.f / 1.1f;
    if (particle.SubType == 0)
    {
        const float acceleratingFrames =
            std::clamp((SpeedLimit - particle.Velocity[2]) / Acceleration, 0.f, frames);
        vec3_t travel{particle.Velocity[0] * frames, particle.Velocity[1] * frames, 0.f};
        Core::Time::Advance(travel[2], particle.Velocity[2], Acceleration, acceleratingFrames);
        if (acceleratingFrames < frames)
        {
            particle.Velocity[2] = SpeedLimit;
            travel[2] += SpeedLimit * (frames - acceleratingFrames);
        }
        if (particle.bEnableMove)
        {
            float matrix[3][4];
            vec3_t rotated;
            AngleMatrix(particle.Angle, matrix);
            VectorRotate(travel, matrix, rotated);
            VectorAdd(particle.Position, rotated, particle.Position);
        }
        particle.Scale = 0.2f;
        const float fadeFrames = std::clamp(FadeAge - particle.LifeTime, 0.f, frames);
        VectorScale(particle.Light, std::pow(LateDamping, fadeFrames), particle.Light);
    }
    else if (particle.SubType == 1)
    {
        VectorScale(particle.Light, std::pow(Damping, frames), particle.Light);
        particle.Scale += 1.5f * frames;
    }
}

void InitializeParticle(PARTICLE *o, int Type, int SubType, vec3_t Position, vec3_t Angle,
                        vec3_t Light, float Scale, OBJECT *Owner)
{
    o->Type = Type;
    o->TexType = Type;
    o->SubType = SubType;

    VectorCopy(Position, o->Position);
    VectorCopy(Position, o->StartPosition);
    VectorCopy(Light, o->Light);
    o->Scale = Scale;
    o->Gravity = 0.f;
    o->LifeTime = 2;
    o->Frame = 0;
    o->MotionIntervalFrames = 0.f;
    o->RotationNoiseFrames = 0.f;
    o->ScalarNoiseRate = 0.f;
    o->ScaleNoiseFrames = o->ScaleNoiseRate = 0.f;
    o->AlphaNoiseFrames = o->AlphaNoiseRate = 0.f;
    o->MotionRandomSeed = 0;
    o->AlphaRandomState = 0;
    o->ScaleRandomState = 0;
    if (IsFallingShiny(*o))
        o->TurningForce[0] = 1.f;
    o->Target = Owner;
    o->Rotation = 0.f;
    o->bEnableMove = true;
    VectorCopy(Angle, o->Angle);
    Vector(0.f, 0.f, 0.f, o->Velocity);
}

void AdvanceAttachedCloudLifetime(SessionEffectPool<PARTICLE> &particles, PARTICLE &particle)
{
    constexpr float AttachedCloudLifetime = 50.f;
    if (particle.Target->Live)
        particle.LifeTime = AttachedCloudLifetime;
    if (particle.LifeTime > 0.f)
        return;
    particle.Target->HiddenMesh = 0;
    particles.Retire(particle);
}

bool AdvanceNamedSocketParticle(SessionEffectPool<PARTICLE> &particles, PARTICLE &particle,
                                float animationFactor)
{
    const auto &binding = *particle.SocketBinding;
    const auto *target = binding.source ? binding.source->object : nullptr;
    if (target == nullptr)
    {
        particles.Retire(particle);
        particle.Target = nullptr;
        particle.SocketBinding.reset();
        return false;
    }
    const auto &matrix = target->BoneTransform[particle.iNumBone];
    vec3_t position{matrix[0][3], matrix[1][3], matrix[2][3]};
    VectorScale(position, target->Scale, position);
    VectorAdd(position, target->Position, position);
    vec3_t direction;
    VectorSubtract(particle.Position, position, direction);
    const float distance = VectorLength(direction);
    constexpr float scalePerDistance = 0.003f;
    constexpr float lightPerDistance = 0.08f;
    particle.Scale -= animationFactor * (distance * scalePerDistance);
    for (float &light : particle.Light)
        light -= distance * lightPerDistance * animationFactor;
    return true;
}
} // namespace

void SessionGameplayUnit::HandPosition(PARTICLE *o)
{
    OBJECT *Owner = o->Target;
    BMD *b = &Models[Owner->Type];
    vec3_t p;
    switch (o->Type)
    {
    case BITMAP_FLARE_RED:
    case BITMAP_SHINY + 2:
        Vector(0.f, -120.f, 0.f, p);
        break;
    default:
        Vector(0.f, 0.f, 0.f, p);
        break;
    }
    VectorCopy(Owner->Position, b->BodyOrigin);
    b->TransformPosition(Owner->BoneTransform[Hero->Weapon[o->SubType % 2].LinkBone], p,
                         o->Position, true);
}

void SessionLegacyCalls::HandPosition(PARTICLE *particle)
{
    sessionKeeper_.Gameplay()->HandPosition(particle);
}

int SessionGameplayUnit::CreateParticleFpsChecked(int Type, vec3_t Position, vec3_t Angle,
                                                  vec3_t Light, int SubType, float Scale,
                                                  OBJECT *Owner)
{
    int particle = 0;
    for (auto birthTime : Emissions(FPS_ANIMATION_FACTOR))
    {
        particle = CreateParticle(Type, Position, Angle, Light, SubType, Scale, Owner);
    }
    return particle;
}

int SessionGameplayUnit::CreateParticle(int Type, vec3_t Position, vec3_t Angle, vec3_t Light,
                                        int SubType, float Scale, OBJECT *Owner)
{
    if (!g_pOption->GetRenderAllEffects())
    {
        return false;
    }

    const int i = Particles.Allocate();
    PARTICLE *o = &Particles[i];
    o->SocketBinding.reset();
    o->PresentationRandom = sessionKeeper_.RandomForConstruction().IsPresentation();
    InitializeParticle(o, Type, SubType, Position, Angle, Light, Scale, Owner);
    RegisterEffectBirth(o->BirthTiming, o, i);

    vec3_t p;
    float Matrix[3][4];
    switch (o->Type)
    {
    case BITMAP_EFFECT:
        if (o->SubType == 1)
        {
            o->TexType = BITMAP_EXT_LOG_IN + 2;
            o->Scale = (float)(WorldRandom() % 10 + 20) * 0.01f;
            o->Gravity = (float)(WorldRandom() % 10 + 20) * 0.05f;
        }
        else if (o->SubType == 2)
        {
            o->Gravity = 0.0f;
            o->Scale = (float)(WorldRandom() % 5 + 12) * 0.1f;
        }
        else if (o->SubType == 3)
        {
            o->TexType = BITMAP_CLUD64;
            o->Scale = (float)(WorldRandom() % 5 + 10) * 0.01f;
            o->Gravity = (float)(WorldRandom() % 10 + 20) * 0.05f;
        }
        else if (o->SubType == 0)
        {
            o->Gravity = 0.0f;
            o->Scale = (float)(WorldRandom() % 5 + 20) * 0.1f;
        }
        o->LifeTime = 20 + WorldRandom() % 3;
        if (o->SubType == 2)
        {
            o->LifeTime = 40 + WorldRandom() % 3;
        }
        o->Position[0] += ((float)(WorldRandom() % 50 - 25));
        o->Position[1] += ((float)(WorldRandom() % 50 - 25));
        o->Position[2] += ((float)(WorldRandom() % 200 - 100) + 250.0f);

        if (o->SubType == 4)
        {
            o->Gravity = 0.0f;
            o->Scale = (float)(WorldRandom() % 5 + 20) * 0.1f * 1.3f;
            o->Position[0] = Position[0] + (float)(WorldRandom() % 80 - 40) * 1.3f;
            o->Scale = (float)(WorldRandom() % 5 + 20) * 0.1f;

            o->Position[0] = Position[0] + (float)(WorldRandom() % 80 - 40);
            o->Position[2] -= (100.f);
        }
        else if (o->SubType == 5)
        {
            o->TexType = BITMAP_EXT_LOG_IN + 2;
            o->Scale = (float)(WorldRandom() % 10 + 20) * 0.01f * 1.3f;
            o->Gravity = (float)(WorldRandom() % 10 + 20) * 0.05f * 1.3f;
            o->Position[2] -= (100.f);
        }
        else if (o->SubType == 6)
        {
            o->Gravity = (float)(WorldRandom() % 20 + 80) * 0.1f;
            o->Scale = (float)(WorldRandom() % 5 + 20) * 0.1f;
            o->Position[2] -= (200.f);
        }
        else if (o->SubType == 7)
        {
            o->TexType = BITMAP_EXT_LOG_IN + 2;
            o->Scale = (float)(WorldRandom() % 10 + 20) * 0.01f;
            o->Gravity = (float)(WorldRandom() % 20 + 80) * 0.1f;

            o->Position[2] -= (100.f);
        }
        break;

    case BITMAP_FLOWER01:
    case BITMAP_FLOWER01 + 1:
    case BITMAP_FLOWER01 + 2:
        o->LifeTime = 15 + WorldRandom() % 10;
        o->Scale = (float)(WorldRandom() % 8 + 4) * 0.03f;
        Vector(1.f, 1.f, 1.f, o->Light);
        o->Velocity[0] = (float)(WorldRandom() % 32 - 16) * 0.1f;
        o->Velocity[1] = (float)(WorldRandom() % 32 - 16) * 0.1f;
        o->Velocity[2] = (float)(WorldRandom() % 32 - 32) * 0.1f;
        break;
    case BITMAP_FLARE_BLUE:
        if (o->SubType == 0)
        {
            o->LifeTime = 30 + WorldRandom() % 10;
            Vector(1.f, 1.f, 1.f, o->Light);
            Vector(0.f, 0.f, (WorldRandom() % 100) / 50.f, o->Velocity);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 26 + (WorldRandom() % 2);
            o->Gravity = 0;
            o->Velocity[0] = 0;
            o->Scale = Scale + (float)(WorldRandom() % 6) * 0.1f;
            o->Rotation = (float)(WorldRandom() % 360);
        }
        break;
    case BITMAP_FLARE + 1:
        if (o->SubType == 0)
        {
            o->LifeTime = 110 + WorldRandom() % 10;
            o->Gravity = ((WorldRandom() % 100) / 100.f) * 2.f + 1.f;
            o->Velocity[0] = (float)(WorldRandom() % 100 - 50);
            o->Scale = Scale + (float)(WorldRandom() % 2) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);

            Vector(0.f, 0.f, 0.f, o->Angle);

            VectorCopy(o->Position, o->StartPosition);
        }
        break;
    case BITMAP_BUBBLE:
        switch (o->SubType)
        {
        case 0:
        case 1:
            o->LifeTime = 30 + WorldRandom() % 10;
            o->Scale = (float)(WorldRandom() % 6 + 4) * 0.03f;
            Vector(1.f, 1.f, 1.f, o->Light);
            break;
        case 2:
            o->LifeTime = 30 + WorldRandom() % 10;
            o->Scale = (float)(WorldRandom() % 6 + 4) * 0.03f;
            Vector(1.f, 1.f, 1.f, o->Light);
            PlayBuffer(SOUND_DEATH_BUBBLE);
            break;
        case 3:
            o->LifeTime = 30 + WorldRandom() % 10;
            o->Scale = (float)(WorldRandom() % 6 + 4) * 0.03f + Scale;
            o->Gravity = (float)(WorldRandom() % 6 + 4) * 0.03f;
            Vector(1.f, 1.f, 1.f, o->Light);
            break;
        case 4:
            o->LifeTime = 30 + WorldRandom() % 10;
            o->Scale = (float)(WorldRandom() % 6 + 4) * 0.03f;
            Vector(1.f, 1.f, 1.f, o->Light);
            break;
        case 5:
            o->LifeTime = 30 + WorldRandom() % 10;
            o->Scale = (float)(WorldRandom() % 6 + 4) * 0.04f;
            Vector(1.f, 1.f, 1.f, o->Light);
            break;
        }
        break;
    case BITMAP_LIGHTNING + 1:
        switch (o->SubType)
        {
        case 0:
            o->Scale = 0.15f;
            break;
        case 4:
            o->Scale = 0.25f;
            break;
        case 1:
            o->LifeTime = 10;
            o->Scale = 0.f;
            break;
        case 2:
            o->LifeTime = 20;
            o->Scale = 1.f;
            break;
        case 3:
            o->LifeTime = 1;
            o->Scale = Scale;
            break;
        case 5:
            o->Scale = Scale;
            break;
        }

        if (o->SubType == 4)
        {
            VectorCopy(Light, o->Light);
        }
        else if (o->SubType == 5)
        {
            VectorCopy(Light, o->Light);
        }
        else
        {
            Vector(Light[0] + 0.5f, Light[1] + 0.5f, Light[2] + 0.5f, o->Light);
        }
        break;
    case BITMAP_LIGHTNING:
        o->LifeTime = 10;
        o->Scale = 1.8f;
        o->Angle[0] = 30.f;
        Vector((float)(WorldRandom() % 4 + 6) * 0.1f, (float)(WorldRandom() % 4 + 6) * 0.1f,
               (float)(WorldRandom() % 4 + 6) * 0.1f, o->Light);
        o->Position[2] += (260.f);
        break;
    case BITMAP_CHROME_ENERGY2:
        o->LifeTime = 8;
        Vector(0.f, 0.f, 0.f, o->Velocity);
        o->Scale = Scale * (float)(WorldRandom() % 64 + 128) * 0.01f;
        o->Rotation = (float)(WorldRandom() % 360);
        break;
    case BITMAP_FIRE_CURSEDLICH:
    case BITMAP_FIRE_HIK2_MONO:
        if (o->SubType == 0)
        {
            o->LifeTime = (WorldRandom() % 12 + 8);
            Vector(0.f, 0.f, 0.f, o->Velocity);
            o->Scale = (float)(WorldRandom() % 30 + 20) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (WorldRandom() % 15 + 15) * 0.01f;
        }
        else if (o->SubType == 1)
        {
            o->Position[0] += ((WorldRandom() % 10 - 5) * 0.2f);
            o->Position[1] += ((WorldRandom() % 10 - 5) * 0.2f);
            o->Position[2] += ((WorldRandom() % 10 - 5) * 0.2f);
            o->LifeTime = (WorldRandom() % 28 + 4);
            Vector(0.f, 0.f, 0.f, o->Velocity);
            o->Scale = (float)(WorldRandom() % 5 + 50) * 0.016f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (WorldRandom() % 5 + 10) * 0.01f;
            VectorCopy(o->Target->Position, o->StartPosition);
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = (WorldRandom() % 12 + 8);
            Vector(0.f, 0.f, 0.f, o->Velocity);
            o->Scale = (float)(WorldRandom() % 30 + 20) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (WorldRandom() % 15 + 15) * 0.01f;
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = (WorldRandom() % 12 + 16);
            Vector(0.f, 0.f, 0.f, o->Velocity);
            o->Scale = (float)(WorldRandom() % 30 + 20) * 0.02f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (WorldRandom() % 15 + 15) * 0.02f;
            VectorCopy(o->Target->Position, o->StartPosition);
        }
        else if (o->SubType == 4 || o->SubType == 9)
        {
            o->LifeTime = WorldRandom() % 5 + 12;
            o->Scale = (float)(WorldRandom() % 72 + 72) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 24 + 64) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        else if (o->SubType == 5)
        {
            o->LifeTime = WorldRandom() % 5 + 24;
            o->Scale = (float)(WorldRandom() % 72 + 72) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 24 + 74) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        else if (o->SubType == 6)
        {
            o->LifeTime = WorldRandom() % 5 + 24;
            o->Scale = (float)(WorldRandom() % 72 + 72) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 24 + 100) * 0.1f;
            VectorCopy(o->Angle, o->Velocity);
            float fRand = (float)(WorldRandom() % 50) * 0.03f;
            Vector(2.5f + fRand, -5.0f - fRand, 0.f, o->Velocity);
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        else if (o->SubType == 7)
        {
            o->LifeTime = WorldRandom() % 5 + 12;
            o->Scale = (float)(WorldRandom() % 72 + 52) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 14 + 44) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        else if (o->SubType == 8)
        {
            o->LifeTime = 8;
            o->Scale = (float)(WorldRandom() % 42 + 12) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 14 + 44) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
            VectorCopy(o->Target->Position, o->StartPosition);
        }
        break;
    case BITMAP_LEAF_TOTEMGOLEM:
        o->LifeTime = WorldRandom() % 10 + 40;
        Vector(0.f, 0.f, 0.f, o->Velocity);
        o->Scale = (float)(WorldRandom() % 20 + 10) * 0.04f;
        o->Rotation = (float)(WorldRandom() % 360);
        o->Gravity = (WorldRandom() % 40 + 30) * -0.05f;
        o->Velocity[0] = WorldRandom() % 10 - 5;
        o->Velocity[1] = WorldRandom() % 10 - 5;
        o->Velocity[2] = WorldRandom() % 5 + 10;
        o->Alpha = 1.0f;
        break;
    case BITMAP_FIRE:
    case BITMAP_FIRE + 2:
    case BITMAP_FIRE + 3: {
        switch (o->SubType)
        {
        case 0: {
            o->LifeTime = 24;
            Vector(0.f, -(float)(WorldRandom() % 16 + 32) * 0.1f, 0.f, o->Velocity);
            o->Scale = (float)(WorldRandom() % 64 + 128) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
        }
        break;
        case 1: {
            o->LifeTime = 24;
            Vector(0.f, -(float)(WorldRandom() % 16 + 32) * 0.1f, 0.f, o->Velocity);
            o->Scale = (float)(WorldRandom() % 4 + 10) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
        }
        break;
        case 17:
        case 5: {
            if (o->SubType == 17)
                o->LifeTime = 10;
            else
                o->LifeTime = 24;
            Vector(0.f, -(float)(WorldRandom() % 16 + 32) * 0.1f, 0.f, o->Velocity);
            o->Scale = Scale * (float)(WorldRandom() % 64 + 128) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
        }
        break;
        case 7: {
            o->LifeTime = 24;
            Vector(0.f, -(float)(WorldRandom() % 16 + 32) * 0.1f, 0.f, o->Velocity);
            Vector(0.f, -(float)(WorldRandom() % 32 - 16) * 0.1f, 0.f, o->Velocity);
            o->Scale = (float)(WorldRandom() % 64 + 128) * 0.008f + Scale;
            o->Rotation = (float)(WorldRandom() % 360);
        }
        break;
        case 9: {
            o->LifeTime = 24;
            Vector(0.f, -(float)(WorldRandom() % 16 + 32) * 0.1f, 0.f, o->Velocity);
            int range = WorldRandom() % 60 - 30;
            o->StartPosition[0] = (float)range;
            o->StartPosition[1] = (float)range;
            o->StartPosition[2] = 190.f - abs(range) * 1.5f;
            o->Rotation = (float)(WorldRandom() % 360);
        }
        break;
        case 10: {
            o->LifeTime = 24;
            Vector(0.f, -(float)(WorldRandom() % 16 + 32) * 0.1f, 0.f, o->Velocity);
            o->Rotation = (float)(WorldRandom() % 360);
        }
        break;
        case 11: {
            o->LifeTime = 24;
            Vector(0.f, -(float)(WorldRandom() % 32 - 16) * 0.1f, 0.f, o->Velocity);
            o->Scale = (float)(WorldRandom() % 64 + 128) * 0.008f + Scale;
            o->Rotation = (float)(WorldRandom() % 360);
        }
        break;
        case 12: {
            o->LifeTime = 24;
            Vector(0.f, -(float)(WorldRandom() % 16 + 32) * 0.1f, 0.f, o->Velocity);
            o->Rotation = (float)(WorldRandom() % 360);
            o->Scale = Scale * (float)(WorldRandom() % 16 + 150) * 0.012f;
        }
        break;
        case 13: {
            o->LifeTime = 20;
            Vector(0.f, 0.f, (float)(WorldRandom() % 128 + 128) * 0.15f, o->Velocity);
            o->Rotation = (float)(WorldRandom() % 360);
            o->Scale = (float)(WorldRandom() % 16 + 150) * 0.012f;
        }
        break;
        case 14: {
            o->LifeTime = 24;
            o->Scale = 1.5f;
            o->TexType = BITMAP_CLUD64;
            Vector(0.f, 0.0f, 0.f, o->Velocity);
            //	o->Gravity = (float)(WorldRandom()%60 - 30)/10.0f;
            o->Rotation = (float)(WorldRandom() % 360);
        }
        break;
        case 15: {
            o->LifeTime = 10;
            o->Scale = (float)(WorldRandom() % 64 + 128) * 0.01f;
            o->TexType = BITMAP_CLUD64;
            Vector(0.f, 0.0f, 0.f, o->Velocity);
            o->Rotation = (float)(WorldRandom() % 360);
        }
        break;
        case 16: {
            o->LifeTime = 4;
        }
        break;
        case 18: {
            o->LifeTime = 24;
            Vector(0.f, -(float)(WorldRandom() % 32 - 16) * 0.1f, 0.f, o->Velocity);
            o->Rotation = (float)(WorldRandom() % 360);
            o->TexType = BITMAP_GROUND_SMOKE;
            Vector(1.0f, 1.0f, 1.0f, o->Light);
        }
        break;
        default: {
            o->LifeTime = 24;
            Vector(0.f, -(float)(WorldRandom() % 32 - 16) * 0.1f, 0.f, o->Velocity);
            o->Rotation = (float)(WorldRandom() % 360);
        }
        }
    }
    break;
    case BITMAP_FIRE + 1:
        if (o->SubType == 1)
        {
            o->LifeTime = 3;
            Vector(0.f, -(float)(WorldRandom() % 8 + 32) * 0.3f, 0.f, o->Velocity);
            //o->Scale = 0.8f;
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 5;
            Vector(0.f, -(float)(WorldRandom() % 8 + 32) * 0.1f, 0.f, o->Velocity);
            //o->Scale = 0.8f;
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = 7;
            Vector(0.f, -(float)(WorldRandom() % 8 + 32) * 0.1f, 0.f, o->Velocity);
            //o->Scale = 0.8f;
        }
        else if (o->SubType == 4)
        {
            o->LifeTime = 100;
            o->Gravity = ((WorldRandom() % 100) / 100.f) * 4.f + 1.f;
            o->Velocity[0] = (float)(WorldRandom() % 300 - 150);
            o->Scale = Scale + (float)(WorldRandom() % 6) * 0.15f;
            o->Rotation = (float)(WorldRandom() % 360);

            Vector(0.f, 0.f, 0.f, o->Angle);
            VectorCopy(o->Position, o->StartPosition);
        }
        else if (o->SubType == 5)
        {
            o->LifeTime = 6;
            o->Gravity = ((WorldRandom() % 100) / 100.f) * 4.f + o->Angle[0] * 1.2f; //45.f;
            o->Scale = Scale + (float)(WorldRandom() % 6) * 0.20f;
            o->Rotation = (float)(WorldRandom() % 360);

            VectorCopy(o->Position, o->StartPosition);
        }
        else if (o->SubType == 6)
        {
            o->LifeTime = 6;
            o->Gravity = ((WorldRandom() % 100) / 100.f) * 4.f + 5.f;
            o->Scale = Scale + (float)(WorldRandom() % 6) * 0.10f;
            o->Rotation = (float)(WorldRandom() % 360);

            VectorCopy(o->Position, o->StartPosition);
        }
        else if (o->SubType == 7)
        {
            o->LifeTime = 10;
        }
        else if (o->SubType == 8)
        {
            o->Position[0] += ((float)(WorldRandom() % 20 - 10));
            o->Position[1] += ((float)(WorldRandom() % 20 - 10));
            o->Position[2] += ((float)(WorldRandom() % 20 - 10));
            o->LifeTime = 32;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 5 + 5) / 5.0f;
        }
        else if (o->SubType == 9)
        {
            o->TexType = BITMAP_LIGHT + 2;
            o->LifeTime = 32;
            //o->Scale    = Scale+(float)(WorldRandom()%6)*0.10f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 5 + 5) / 10.0f;
        }
        else if (o->SubType == 0)
        {
            o->LifeTime = 12;
            Vector(0.f, -(float)(WorldRandom() % 8 + 32) * 0.3f, 0.f, o->Velocity);
            o->Scale = 0.8f;
        }
        o->Rotation = (float)(WorldRandom() % 360);
        break;
    case BITMAP_FLAME:
        switch (o->SubType)
        {
        case 0:
            o->LifeTime = 20;
            Vector(0.f, 0.f, (float)(WorldRandom() % 128 + 128) * 0.15f, o->Velocity);
            o->Scale = Scale * (float)(WorldRandom() % 64 + 64) * 0.01f;
            break;
        case 6:
            o->LifeTime = 10;
            Vector(0.f, 0.f, (float)(WorldRandom() % 128 + 256) * 0.12f, o->Velocity);
            o->Scale = Scale * (float)(WorldRandom() % 64 + 64) * 0.01f;
            break;
        case 8:
        case 7: {
            o->LifeTime = 33;
            o->Scale = -(float)(WorldRandom() % 20) / 100.0f + o->Scale;
            o->Rotation = (float)(WorldRandom() % 360) - 180.0f;
            o->StartPosition[0] = o->Position[0];
            o->Position[0] += (((float)(WorldRandom() % 2) / 2.0f - 0.0f) * o->Scale);
            o->Gravity = ((float)(WorldRandom() % 100) / 100.0f + 1.8f) * o->Scale;
            float fTemp = 0.0f;
            if (WorldRandom() % 10 >= 3)
                fTemp = ((float)(WorldRandom() % 20) / 10.0f - 1.0f) * o->Scale;
            Vector(0.0f, fTemp, 0.0f, o->Velocity);
            o->Position[1] += (o->Position[0] - o->StartPosition[0]);
        }
        break;
        case 1:
            o->LifeTime = 15;
            o->Scale += (float)(WorldRandom() % 32 + 32) * 0.01f;
            Vector(0.f, (float)(WorldRandom() % 4 + 4) * 0.15f, 0.f, o->Velocity);
            break;
        case 5:
            o->LifeTime = 4;
            Vector(0.f, (float)(WorldRandom() % 4 + 4) * 0.15f, 0.f, o->Velocity);
            break;
        case 2: {
            float Luminosity;

            o->LifeTime = 10;
            o->Scale += (float)(WorldRandom() % 32 + 32) * 0.01f;

            float inter = Light[0] * (WorldRandom() % 80) / 100.0f;
            Vector(0.f, inter, 0.f, o->Velocity);
            MovePosition(o->Position, o->Angle, o->Velocity);

            inter = (Light[0] - inter) / 15.0f;
            Vector(0.f, inter, 0.f, o->Velocity);

            //  색.
            Luminosity = (float)sinf(WorldTime * 0.002f) * 0.3f + 0.7f;
            Vector(Luminosity, Luminosity * 0.5f, Luminosity * 0.5f, o->Light);
        }
        break;

        case 3: {
            float Luminosity;

            o->LifeTime = 10;
            o->Scale += (float)(WorldRandom() % 32 + 32) * 0.01f;
            Vector(0.f, 0.f, 0.f, o->Velocity);

            Luminosity = (float)sinf(WorldTime * 0.002f) * 0.3f + 0.7f;
            Vector(Luminosity, Luminosity * 0.5f, Luminosity * 0.5f, o->Light);
        }
        break;
        case 4: {
            float Luminosity;

            o->LifeTime = 5;
            o->Scale += (float)(WorldRandom() % 32 + 32) * 0.01f;
            o->Gravity = 10.f;
            VectorCopy(o->Target->Position, o->StartPosition);

            Luminosity = (float)sinf(WorldTime * 0.002f) * 0.3f + 0.7f;
            Vector(Luminosity, Luminosity * 0.5f, Luminosity * 0.5f, o->Light);
        }
        break;
        case 9: {
            o->LifeTime = 40;
            o->Scale += -(float)(WorldRandom() % 20) / 100.0f;
            o->Rotation = (float)(WorldRandom() % 360) - 180.0f;
            o->StartPosition[0] = o->Position[0];
            o->Position[0] += (((float)(WorldRandom() % 2) / 2.0f - 0.0f) * o->Scale);
            o->Gravity = ((float)(WorldRandom() % 100) / 100.0f + 1.8f) * o->Scale + 2.0f;
            float fTemp = 0.0f;
            if (WorldRandom() % 10 >= 3)
            {
                fTemp = ((float)(WorldRandom() % 20) / 10.0f - 1.0f) * o->Scale;
            }
            Vector(0.0f, fTemp * 2.0f, 0.0f, o->Velocity);
            o->Position[1] += (o->Position[0] - o->StartPosition[0]);
        }
        break;
        case 10:
            o->LifeTime = 20 + WorldRandom() % 5;
            o->Rotation = 0.f;
            o->Scale = Scale * 0.5f + (float)(WorldRandom() % 10) * 0.02f;
            o->Velocity[0] = (float)(WorldRandom() % 10 + 5) * 0.4f;
            o->Velocity[1] = (float)(WorldRandom() % 10 - 5) * 0.4f;
            o->Velocity[2] = (float)(WorldRandom() % 10 + 5) * 0.2f;
            break;
        case 11:
            o->LifeTime = 20;
            o->Rotation = (float)(WorldRandom() % 360);
            o->TexType = BITMAP_FIRE_HIK2_MONO;
            Vector(0.f, 0.f, (float)(WorldRandom() % 128 + 128) * 0.15f, o->Velocity);
            o->Scale = Scale * (float)(WorldRandom() % 64 + 64) * 0.01f;
            break;
        case 12:
            o->LifeTime = 10;
            Vector(0.f, 0.f, (float)(WorldRandom() % 128 + 128) * 0.15f, o->Velocity);
            o->Scale = Scale * (float)(WorldRandom() % 64 + 64) * 0.01f;
            break;
        }
        break;
    case BITMAP_FIRE_RED:
        o->LifeTime = 20;
        Vector(0.f, 0.f, (float)(WorldRandom() % 100 + 100) * 0.15f, o->Velocity);
        o->Scale = Scale * (float)(WorldRandom() % 64 + 64) * 0.01f;
        break;
    case BITMAP_RAIN_CIRCLE:
    case BITMAP_RAIN_CIRCLE + 1:
        o->LifeTime = 20;
        o->Scale = (WorldRandom() % 6 + 8) * 0.1f;
        if (o->Type == BITMAP_RAIN_CIRCLE + 1 || o->SubType == 1)
        {
            o->Position[0] += ((WorldRandom() % 10 - 5) * 1.f);
            o->Position[1] += ((WorldRandom() % 10 - 5) * 1.f);
            o->Position[2] += ((WorldRandom() % 10 - 5) * 1.f);
        }
        if (o->SubType == 1)
        {
            o->Scale = Scale;
            o->Gravity = WorldRandom() % 100 / 1000.f;
            o->Velocity[1] = 1.f;
        }
        break;
    case BITMAP_ENERGY: //thunder energy
        o->Scale = Scale * (float)(WorldRandom() % 8 + 6) * 0.1f;
        o->Rotation = (float)(WorldRandom() % 360);
        o->Gravity = 20.f;

        if (o->SubType == 1)
        {
            o->LifeTime = 10;
            Vector(0.5f, 0.5f, 0.5f, o->Light);
        }
        else if (o->SubType == 2)
        {
            o->TexType = BITMAP_MAGIC + 1;
            o->LifeTime = 10;
            o->Scale = 0.3f + Scale * (float)(WorldRandom() % 8 + 6) * 0.1f;
            VectorCopy(o->Light, Light);
            o->Rotation = 0;
            o->Gravity = 0;
        }
        // ChainLighting
        else if (o->SubType == 3 || o->SubType == 4 || o->SubType == 5)
        {
            o->LifeTime = 25;
        }
        else if (o->SubType == 6)
        {
            o->LifeTime = 20;
        }
        else if (o->SubType == 7)
        {
            o->LifeTime = 5;
        }
        break;
    case BITMAP_MAGIC: {
        if (o->SubType == 0)
        {
            o->LifeTime = 10;
            o->Scale = Scale;
        }
    }
    break;
    case BITMAP_FLARE: //
        o->LifeTime = 60;
        if (o->SubType == 0 || o->SubType == 3 || o->SubType == 6 || o->SubType == 10)
        {
            o->Gravity = ((WorldRandom() % 100) / 100.f) * 4.f + 1.f;
        }
        else if (o->SubType == 2)
        {
            o->Gravity = ((WorldRandom() % 100) / 100.f) * 5.f + 5.f;
        }
        else if (o->SubType == 4)
        {
            o->LifeTime = 40;
            o->Gravity = ((WorldRandom() % 100) / 100.f) * 5.f + 1.f;
        }
        else if (o->SubType == 5)
        {
            o->LifeTime = 40;
            o->Gravity = ((WorldRandom() % 100) / 100.f) * 5.f + 1.f;
        }
        o->Velocity[0] = (float)(WorldRandom() % 300 - 150);
        o->Scale = Scale + (float)(WorldRandom() % 6) * 0.01f;
        o->Rotation = (float)(WorldRandom() % 360);

        Vector(0.f, 0.f, 0.f, o->Angle);

        VectorCopy(o->Position, o->StartPosition);
        if (o->SubType == 10)
        {
            float count = (o->Velocity[0] + o->LifeTime) * 0.1f;
            o->StartPosition[0] = o->StartPosition[0] + sinf(count) * 40.f;
            o->StartPosition[1] = o->StartPosition[1] - cosf(count) * 40.f;
        }
        else if (o->SubType == 11)
        {
            o->LifeTime = 26 + (WorldRandom() % 2);
            o->Gravity = 0;
            o->Velocity[0] = 0;
            o->Scale = Scale + (float)(WorldRandom() % 6) * 0.1f;
            o->Rotation = (float)(WorldRandom() % 360);
        }
        else if (o->SubType == 12)
        {
            o->LifeTime = 80;
            o->Gravity = ((WorldRandom() % 100) / 100.f) * 4.f + 1.f;
            Vector(0.2f, 0.6f, 1.f, o->Light);
        }
        break;
    case BITMAP_LIGHT + 2:
        if (o->SubType == 0)
        {
            o->LifeTime = 16;
            o->Scale += (float)(WorldRandom() % 32 + 48) * 0.01f;
            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Rotation = (float)((int)WorldTime % 360);
        }
        else if (o->SubType == 1)
        {
            o->TexType = BITMAP_SMOKE;
            o->LifeTime = 20;
            o->Scale += (float)(WorldRandom() % 10 + 10) * 0.01f;
            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Rotation = (float)((int)WorldTime % 360);
            o->Gravity = (float)(WorldRandom() % 10 + 40) * 0.04f;
            o->Position[1] += (10.f);
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 10;
            o->Scale -= (float)(WorldRandom() % 40 + 48) * 0.008f;
            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Rotation = (float)((int)WorldTime % 360);
            o->Gravity = 0.0f;
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = 20;
            o->Scale += (float)(WorldRandom() % 32 + 48) * 0.008f;
            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Rotation = (float)((int)WorldTime % 360);
            VectorCopy(o->Target->Position, o->StartPosition);
        }
        else if (o->SubType == 4)
        {
            o->LifeTime = 16;
            o->Scale += (float)(WorldRandom() % 32 + 48) * 0.01f;
            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Rotation = (float)((int)WorldTime % 360);
        }
        else if (o->SubType == 5)
        {
            o->TexType = BITMAP_ADV_SMOKE;

            BMD *pModel = &Models[o->Target->Type];
            int iNumBones = pModel->NumBones;

            vec3_t vRelative, p1;
            Vector(0.0f, 0.0f, 0.0f, vRelative);
            pModel->TransformPosition(o->Target->BoneTransform[WorldRandom() % iNumBones],
                                      vRelative, o->Position, false);
            VectorScale(o->Position, pModel->BodyScale, o->Position);
            VectorAdd(o->Position, o->Target->Position, o->Position);

            o->Velocity[0] = 0;
            o->Velocity[1] = 0;
            o->Velocity[2] = 0;

            o->LifeTime = 21;
            o->Scale = (float)(WorldRandom() % 4 + 8) * 0.01f;
            o->Gravity = (float)(WorldRandom() % 1 + 4) * 0.1f;

            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            Vector(0.f, (float)(WorldRandom() % 2 + 60) * 0.1f, 0.f, p1);
            VectorRotate(p1, Matrix, o->TurningForce);

            o->TurningForce[2] += (4.0f);
            o->Rotation = (float)((int)WorldTime % 360);
            o->Alpha = 1.0f;
        }
        else if (o->SubType == 6)
        {
            o->LifeTime = 21;
            o->Scale = Scale + (float)(WorldRandom() % 4 + 8) * 0.01f;
            o->Gravity = (float)(WorldRandom() % 1 + 1) * 0.1f;

            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            vec3_t p1;
            Vector(0.f, (float)(WorldRandom() % 2 + 60) * 0.1f, 0.f, p1);
            VectorRotate(p1, Matrix, o->TurningForce);

            o->TurningForce[2] += (2.0f);
            o->Rotation = (float)((int)WorldTime % 360);
            o->Alpha = 1.0f;

            o->Velocity[0] = 0;
            o->Velocity[1] = 0;
            o->Velocity[2] = 0;
        }
        else if (o->SubType == 7)
        {
            o->LifeTime = 18;
            o->Scale = Scale + (float)(WorldRandom() % 4 + 8) * 0.01f;
            o->Gravity = (float)(WorldRandom() % 1 + 1) * 0.1f;

            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            vec3_t p1;
            Vector(0.f, (float)(WorldRandom() % 2 + 60) * 0.1f, 0.f, p1);
            VectorRotate(p1, Matrix, o->TurningForce);

            o->TurningForce[2] += (2.0f);
            o->Rotation = (float)((int)WorldTime % 360);
            o->Alpha = 1.0f;

            o->Velocity[0] = 0;
            o->Velocity[1] = 0;
            o->Velocity[2] = 0;
        }
        break;
    case BITMAP_MAGIC + 1:
        o->LifeTime = 10;
        o->Scale += (float)(WorldRandom() % 32 + 48) * 0.001f;
        o->Angle[0] = (float)(WorldRandom() % 360);
        o->Rotation = (float)((int)WorldTime % 360);
        break;
    case BITMAP_BLUE_BLUR:
        switch (SubType)
        {
        case 0:
            o->LifeTime = 30;
            o->Scale = (float)(WorldRandom() % 64 + 64) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 32 + 60) * 0.1f;
            break;
        case 1:
            o->LifeTime = 30;
            o->Scale = (float)(WorldRandom() % 64 + 64) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 32 + 60) * 0.1f;
            o->Position[2] -= (45.00f);
            o->TexType = BITMAP_POUNDING_BALL;
            break;
        }
        break;
    case BITMAP_CLUD64: {
        if (o->SubType == 0 || o->SubType == 2)
        {
            if (WorldRandom() % 4 != 0)
                o->TexType = BITMAP_SMOKE;

            o->LifeTime = 40;
            o->Scale = (float)(WorldRandom() % 20 + 250) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Position[0] += ((float)(WorldRandom() % 20 - 10));
            o->Position[1] += ((float)(WorldRandom() % 20 - 10));
            o->Position[2] -= (20.f);
            o->Gravity = (float)(WorldRandom() % 10 + 5) * 0.1f;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 12;
            o->Scale += (float)(WorldRandom() % 30) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Position[2] += (30.0f);
        }
        else if (o->SubType == 3)
        {
            if (WorldRandom() % 2 != 0)
                o->TexType = BITMAP_SMOKE;
            o->LifeTime = 50;
            o->Scale = Scale + (float)(WorldRandom() % 10 + 10) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Position[0] += ((float)(WorldRandom() % 20 - 10));
            o->Position[1] += ((float)(WorldRandom() % 20 - 10));
            o->Position[2] -= (20.f);
            o->Gravity = (float)(WorldRandom() % 10 + 5) * 0.1f;
        }
        else if (o->SubType == 4)
        {
            if (WorldRandom() % 2 != 0)
                o->TexType = BITMAP_SMOKE;
            o->LifeTime = 4;
            o->Scale = Scale + (float)(WorldRandom() % 10 + 10) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Position[0] += ((float)(WorldRandom() % 20 - 10));
            o->Position[1] += ((float)(WorldRandom() % 20 - 10));
            o->Position[2] -= (20.f);
            o->Gravity = (float)(WorldRandom() % 10 + 5) * 0.1f;
        }
        else if (o->SubType == 5)
        {
            if (WorldRandom() % 2 != 0)
                o->TexType = BITMAP_SMOKE;
            o->LifeTime = 50;
            o->Scale = Scale + (float)(WorldRandom() % 10 + 10) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Position[0] += ((float)(WorldRandom() % 20 - 10));
            o->Position[1] += ((float)(WorldRandom() % 20 - 10));
            o->Position[2] -= (20.f);
            o->Gravity = (float)(WorldRandom() % 10 + 5) * 0.1f;
        }
        else if (o->SubType == 6) // ◎
        {
            o->LifeTime = 25;
            o->Scale = (float)(WorldRandom() % 8 + 50) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 10 + 35) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
            VectorCopy(o->Target->Position, o->StartPosition);
        }
        else if (o->SubType == 7)
        {
            o->LifeTime = 40;
            o->Velocity[0] = (float)(WorldRandom() % 20 - 10) * 0.1f;
            o->Velocity[1] = (float)(WorldRandom() % 20 - 10) * 0.1f;
            //o->Velocity[2] = (float)(WorldRandom()%20-10)*0.2f;
            o->Position[2] += (80.0f);
            o->Gravity = 0;
            o->Alpha = 0.5f;
        }
        else if (o->SubType == 8)
        {
            o->LifeTime = 20;
            o->Velocity[0] = (float)(WorldRandom() % 20 - 10) * 0.1f;
            o->Velocity[1] = (float)(WorldRandom() % 20 - 10) * 0.1f;
            //o->Velocity[2] = (float)(WorldRandom()%20-10)*0.2f;
            o->Position[2] += (80.0f);
            o->Gravity = 0;
            o->Alpha = 0.5f;
        }
        else if (o->SubType == 9)
        {
            if (WorldRandom() % 2 != 0)
                o->TexType = BITMAP_SMOKE;
            o->LifeTime = 40;
            o->Alpha = 0.5f;
            float fIntervalScale = Scale * 0.1f;
            o->Scale += ((float((WorldRandom() % 20) - 10) / 2.0f) * ((fIntervalScale / 2.f)));
            o->Rotation = (float)(WorldRandom() % 360);
            o->Velocity[0] = (float)(WorldRandom() % 20 - 10) * 0.03f;
            o->Velocity[1] = (float)(WorldRandom() % 20 - 10) * 0.03f;
            o->Velocity[2] = -((1.2f) + ((float)(WorldRandom() % 20 - 10) * 0.025f));
            o->Gravity = 2.f + ((float)(WorldRandom() % 20 - 10) * 0.05f);
        }
        else if (o->SubType == 10) // BITMAP_FIRE_CURSEDLICH o->SubType == 1과 비슷.
        {
            o->Position[0] += ((WorldRandom() % 10 - 5) * 0.2f);
            o->Position[1] += ((WorldRandom() % 10 - 5) * 0.2f);
            o->Position[2] += ((WorldRandom() % 10 - 5) * 0.2f);
            o->LifeTime = (WorldRandom() % 28 + 4);
            Vector(0.f, 0.f, 0.f, o->Velocity);
            o->Scale = (float)(WorldRandom() % 5 + 70) * 0.016f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (WorldRandom() % 5 + 10) * 0.01f;
            VectorCopy(o->Target->Position, o->StartPosition);
        }
        else if (o->SubType == 11)
        {
            if (WorldRandom() % 4 != 0)
                o->TexType = BITMAP_SMOKE;

            o->LifeTime = 30;
            o->Scale = Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Position[0] += ((float)(WorldRandom() % 20 - 10));
            o->Position[1] += ((float)(WorldRandom() % 20 - 10));
            o->Position[2] += ((float)(WorldRandom() % 20 - 10));
            o->Gravity = -1.5f;
        }
    }
    break;
    case BITMAP_LIGHT + 3: {
        if (o->SubType == 0)
        {
            o->LifeTime = WorldRandom() % 10 + 140;
            o->Gravity = WorldRandom() % 10 + 5.0f;
            o->Scale = (WorldRandom() % 20 + 20.f) / 100.f * Scale;
            o->Position[0] += ((float)(WorldRandom() % 100 - 50));
            o->Position[1] += ((float)(WorldRandom() % 100 - 50));
        }
        else if (o->SubType == 1)
        {
            o->Scale = 0.1f;
            o->StartPosition[0] = o->Scale;
            o->LifeTime = WorldRandom() % 10 + 20;
            o->Position[0] += ((float)(WorldRandom() % 50 - 25));
            o->Position[1] += ((float)(WorldRandom() % 50 - 25));
            o->Position[2] += ((float)(WorldRandom() % 50 - 25));

            o->Gravity = 1.0f;

            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Angle[1] = (float)(WorldRandom() % 360);
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            Vector(0.5f, 0.5f, 0.5f, p);
            VectorScale(p, 0.8f, p);
            VectorRotate(p, Matrix, o->Velocity);

            o->Rotation = WorldRandom() % 20 - 10;
        }
    }
    break;
    case BITMAP_TWINTAIL_WATER: {
        if (o->SubType == 0)
        {
            o->LifeTime = 50 + WorldRandom() % 10;
            o->Scale = (float)(WorldRandom() % 32 + 140) * 0.01f;
            o->Position[0] += ((float)(WorldRandom() % 120 - 60));
            o->Position[1] += ((float)(WorldRandom() % 100 - 50));
            o->Position[2] += ((float)(WorldRandom() % 100));
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 20 + WorldRandom() % 10;
            o->Scale = (float)(WorldRandom() % 32 + 50) * 0.01f;
            o->Position[0] += ((float)(WorldRandom() % 80 - 40));
            o->Position[1] += ((float)(WorldRandom() % 80 - 40));
            o->Position[2] += ((float)(WorldRandom() % 80));
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 60 + WorldRandom() % 10;
            o->Scale = (float)(WorldRandom() % 32 + 50) * 0.02f;
            o->Position[0] += ((float)(WorldRandom() % 40 - 20));
            o->Position[1] += ((float)(WorldRandom() % 40 - 20));
            o->Position[2] += ((float)(WorldRandom() % 40));
        }
        o->Angle[0] = (float)(WorldRandom() % 360);
        o->Angle[2] = (float)(WorldRandom() % 360);
        o->Gravity = (float)(WorldRandom() % 10 + 20) * 0.1f;
    }
    break;
    case BITMAP_SMOKE:
        switch (SubType)
        {
        case 0:
        case 61:
        case 4:
        case 9: //
        case 23:
            o->LifeTime = 16;
            if (o->Type == MODEL_SLAUGHTERER)
                o->Scale = (float)(WorldRandom() % 3 + 28) * 0.01f;
            else
                o->Scale = (float)(WorldRandom() % 32 + 48) * 0.01f;
            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Rotation = (float)((int)WorldTime % 360);
            break;
        case 42:
            o->LifeTime = 16;
            o->Scale = (float)(WorldRandom() % 32 + 48) * 0.006f;
            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Rotation = (float)((int)WorldTime % 360);
            break;
        case 17:
            VectorCopy(o->Light, o->TurningForce);
            o->LifeTime = 12;
            o->Scale = Scale * (float)(WorldRandom() % 32 + 8) * 0.01f;
            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Rotation = (float)((int)WorldTime % 360);
            break;
        case 36:
        case 37:
            if (o->SubType == 37)
            {
                VectorCopy(o->Light, o->TurningForce);
                o->Position[2] -= (15.0f * o->TurningForce[2]);
                o->Position[1] -= (15.0f * o->TurningForce[1]);
                Vector(1.0f, 1.0f, 1.0f, o->Light);
            }
            else
                Vector(1.0f, 0.3f, 0.0f, o->Light);
            o->LifeTime = 25;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Angle[0] = (float)(WorldRandom() % 360);
            Vector((float)(WorldRandom() % 50 - 25), (float)(WorldRandom() % 50 - 25),
                   -(float)(WorldRandom() % 50), o->StartPosition);
            break;
        case 38:
            o->TexType = BITMAP_WATERFALL_2;
            o->LifeTime = 35;
            o->Rotation = (float)(WorldRandom() % 360);
            break;
        case 34:
        case 35:
            Vector(1.0f, 0.2f, 0.2f, o->Light);
            o->LifeTime = 30;
            o->Rotation = (float)(WorldRandom() % 360);
            if (SubType == 35)
            {
                Vector(0.0f, -5.0f, 0.0f, o->Velocity);
            }
            else
            {
                Vector(0.0f, 0.0f, 0.0f, o->Velocity);
            }
            break;
        case 33: {
            o->LifeTime = WorldRandom() % 150;
            o->Scale = Scale + 0.1f;
            o->Angle[0] = (float)(WorldRandom() % 90 - 45);
            o->Angle[2] = (float)(WorldRandom() % 360);
            Vector(0.f, -(float)(WorldRandom() % 8), 0.f, o->Velocity);
        }
        break;
        case 32: {
            o->LifeTime = 10;
            o->Scale = (float)(WorldRandom() % 32 + 80) * 0.01f;
            o->Angle[0] = (float)(WorldRandom() % 90 - 45);
            o->Angle[2] = (float)(WorldRandom() % 360);
            Vector(0.f, -(float)(WorldRandom() % 8 + 40), 0.f, o->Velocity);
        }
        break;
        case 10:
            o->LifeTime = 16;
            break;
        case 12:
            o->LifeTime = 20;
            o->Scale = (float)(WorldRandom() % 64 + 64) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 32 + 60) * 0.1f;
            break;
        case 13:
            o->LifeTime = 20;
            o->Scale = (float)(WorldRandom() % 64 + 64) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 32 + 60) * 0.1f;
            break;
        case 18:
            o->LifeTime = 20;
            o->Scale = (float)(WorldRandom() % 64 + 64) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 32 + 60) * 0.1f;
            break;
        case 3:
            o->LifeTime = 10;
            o->Scale = (float)(WorldRandom() % 32 + 80) * 0.01f;
            o->Angle[0] = (float)(WorldRandom() % 90 - 45);
            o->Angle[2] = (float)(WorldRandom() % 360);
            Vector(0.f, -(float)(WorldRandom() % 8 + 40), 0.f, o->Velocity);
            break;
        case 11:
        case 14:
            o->LifeTime = 50;
            o->Position[0] += ((float)(WorldRandom() % 64 - 32));
            o->Position[1] += ((float)(WorldRandom() % 64 - 32));
            o->Position[2] += ((float)(WorldRandom() % 64 + 32));
            o->Angle[0] = (float)(WorldRandom() % 90 - 45);
            o->Angle[2] = (float)(WorldRandom() % 360);
            o->Rotation = (float)(WorldRandom() % 360);
            VectorCopy(o->Light, o->TurningForce);
            Vector(0.f, -(float)(WorldRandom() % 8 + 40), 0.f, o->Velocity);
            break;
        case 15:
            o->LifeTime = 80;
            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Rotation = (float)((int)WorldTime % 360);
            break;
        case 53:
        case 56:
        case 1:
            o->LifeTime = 50;
            o->Scale = (float)(WorldRandom() % 32 + 80) * 0.01f;
            o->Position[0] += ((float)(WorldRandom() % 64 - 32));
            o->Position[1] += ((float)(WorldRandom() % 64 - 32));
            o->Position[2] += ((float)(WorldRandom() % 64 + 32));
            o->Angle[0] = (float)(WorldRandom() % 90 - 45);
            o->Angle[2] = (float)(WorldRandom() % 360);
            Vector(0.f, -(float)(WorldRandom() % 8 + 40), 0.f, o->Velocity);
            break;
        case 2:
            o->LifeTime = 50;
            o->Scale = (float)(WorldRandom() % 64 + 64) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 32 + 60) * 0.1f;
            break;
        case 16:
            o->LifeTime = 50;
            o->Scale = (float)(WorldRandom() % 64 + 64) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 32 + 60) * 0.1f;
            break;
        case 5:
            o->LifeTime = 20;
            o->Scale = (float)(WorldRandom() % 64 + 98) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 32 + 60) * 0.1f;
            break;
        case 6:
            o->LifeTime = 30;
            o->Gravity = (float)(WorldRandom() % 1000);
            o->Scale = (float)(WorldRandom() % 20 + 180) * 0.01f;
            o->Position[0] += ((float)(WorldRandom() % 200 - 100) * Scale);
            o->Position[1] += ((float)(WorldRandom() % 200 - 100) * Scale);
            o->Position[2] += ((float)(WorldRandom() % 20 + 20));
            o->Rotation = o->Position[2];
            Vector(0.f, 0.f, 0.f, o->Velocity);
            break;

        case 7:
            o->LifeTime = 30;
            Vector(0.f, (float)(WorldRandom() % 4 + 6) * o->Scale, 0.f, o->Velocity);

            o->Gravity = (float)(WorldRandom() % 200 / 200.0f);
            o->Scale *= (float)(WorldRandom() % 20 + 120) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            break;
        case 8:
            o->LifeTime = 24;
            Vector(0.f, -(float)(WorldRandom() % 8 + 32) * 0.3f, 0.f, o->Velocity);
            o->Scale *= 0.8f;
            o->Rotation = (float)(WorldRandom() % 360);
            break;
        case 19:
            VectorCopy(o->Light, o->TurningForce);
            o->LifeTime = 40;
            o->Scale = Scale * (float)(WorldRandom() % 32 + 8) * 0.01f;
            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Rotation = (float)((int)WorldTime % 360);
            break;
        case 20:
            o->LifeTime = 40;
            o->Scale = Scale * (float)(WorldRandom() % 32 + 8) * 0.01f;
            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Rotation = (float)((int)WorldTime % 360);
            break;
        case 21:
            o->LifeTime = 80;
            o->Scale *= (float)(WorldRandom() % 64 + 64) * 0.005f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 32 + 60) * 0.1f;
            break;
        case 22:
            o->LifeTime = 60;
            o->Scale *= (float)(WorldRandom() % 64 + 64) * 0.005f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 32 + 60) * 0.1f;
            break;
        case 24:
        case 57:
            o->LifeTime = 32;
            o->Scale = Scale + (float)(WorldRandom() % 32 + 48) * 0.01f * Scale;
            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Rotation = (float)((int)WorldTime % 360);
            break;
        case 25:
            o->LifeTime = 25;
            o->Scale = (float)(WorldRandom() % 64 + 64) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 32 + 60) * 0.1f;
            break;
        case 26:
            o->LifeTime = 25;
            o->Scale = (float)(WorldRandom() % 64 + 64) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 32 + 60) * 0.1f;
            break;
        case 27:
            o->LifeTime = 30;
            o->Rotation = (float)(WorldRandom() % 360);
            o->TexType = BITMAP_CHROME3;
            break;
        case 28:
            o->LifeTime = 12;
            o->Scale = 2.3f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = 33.0f;
            o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 15;
            o->TexType = BITMAP_POUNDING_BALL;
            break;
        case 29:
            o->LifeTime = 12;
            o->Scale = 2.3f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = 33.0f;
            o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 15;
            break;
        case 30:
            o->LifeTime = 4;
            o->Scale = 1.8f;
            o->Gravity = 15.0f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 15;
            o->Position[0] += ((float)sinf(WorldTime) * 5.0f);
            o->Position[1] += ((float)sinf(WorldTime) * 5.0f);
            break;
        case 31:
            o->LifeTime = 15;
            o->Scale = Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 32 + 60) * 0.1f;
            break;
        case 40: {
            o->LifeTime = 50;
            o->Scale = (float)(WorldRandom() % 32 + 80) * 0.01f;
            o->Position[0] += ((float)(WorldRandom() % 64 - 32));
            o->Position[1] += ((float)(WorldRandom() % 64 - 32));
            o->Position[2] += ((float)(WorldRandom() % 64 + 32));
            o->Angle[0] = (float)(WorldRandom() % 90 - 45);
            o->Angle[2] = (float)(WorldRandom() % 360);
            Vector(0.f, -(float)(WorldRandom() % 8 + 40), 0.f, o->Velocity);
        }
        break;
        case 41: {
            o->LifeTime = 50;
            o->Scale = (float)(WorldRandom() % 32 + 80) * 0.01f;
            o->Position[0] += ((float)(WorldRandom() % 64 - 32));
            o->Position[1] += ((float)(WorldRandom() % 64 - 32));
            o->Position[2] += ((float)(WorldRandom() % 64 + 32));
            o->Angle[0] = (float)(WorldRandom() % 90 - 45);
            o->Angle[2] = (float)(WorldRandom() % 360);
            Vector(0.f, -(float)(WorldRandom() % 8 + 40), 0.f, o->Velocity);
            o->Gravity = (float)(WorldRandom() % 100 - 50) * 0.1f;
        }
        break;
        case 43: {
            o->TexType = BITMAP_CLUD64;
            o->LifeTime = 40;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Angle[0] = 90.0f;
            Vector(WorldRandom() % 20 / 10.0f - 1.0f, 2.0f, 15.0f, o->Velocity);
        }
        break;
        case 44: {
            o->LifeTime = 60;
            o->Scale += (float)(WorldRandom() % 10) / 2.0f;
            o->Position[0] += ((float)(WorldRandom() % 500) - 250.0f);
            o->Position[1] += ((float)(WorldRandom() % 700) - 350.0f);
            o->Position[2] += ((float)(WorldRandom() % 100) + 150.0f);
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 50) - 25.0f;
            Vector(0.1f, 0.2f, 0.3f, o->Light);
        }
        break;
        case 45: {
            o->TexType = BITMAP_LIGHT + 2;
            o->LifeTime = 15;
            o->Scale += (float)(WorldRandom() % 10) / 20.0f;
            o->Position[0] += ((float)(WorldRandom() % 40) - 20.0f);
            o->Position[1] += ((float)(WorldRandom() % 40) - 20.0f);
            o->Position[2] += ((float)(WorldRandom() % 40) - 20.0f);
        }
        break;
        case 46: {
            //o->TexType = BITMAP_CLOUD;
            o->LifeTime = 60 + (int)(o->Scale * 10.0f);
            o->Scale += (float)(WorldRandom() % 50) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 30 + 50) * 0.05f;
        }
        break;
        case 47: {
            o->TexType = BITMAP_MAGIC + 1;
            o->LifeTime = 5;
            o->Scale += (float)(WorldRandom() % 50) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
        }
        break;
        case 59:
        case 48:
            o->LifeTime = 40;
            o->Scale = (float)(WorldRandom() % 64 + 64) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 32 + 60) * 0.1f;
            break;
        case 49: {
            o->TexType = BITMAP_CLOUD;
            o->LifeTime = WorldRandom() % 50 + 100;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Scale = Scale * 0.4f;
            if (Random.FpsCheck(2, 1.0))
                o->Angle[0] = 1.0f;
            else
                o->Angle[0] = -1.0f;
        }
        break;
        case 50:
            o->LifeTime = 20;
            o->Scale = (float)(WorldRandom() % 32 + 32) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 16 + 16) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
            break;
        case 51:
            o->LifeTime = 25;
            o->Scale = (float)(WorldRandom() % 64 + 64) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 32 + 60) * 0.1f;
            VectorCopy(Light, o->TurningForce);
            break;
        case 52: {
            o->LifeTime = 40;
            o->Scale = Scale + (float)(WorldRandom() % 10 + 10) * 0.05f;
            o->Rotation = (float)(WorldRandom() % 360);

            vec3_t vAngle, vPos, vPos2;
            Vector(0.f, 10.f, 0.f, vPos);
            float fAngle = o->Angle[2] + (float)(WorldRandom() % 100 - 50) + 150;
            Vector(0.f, 0.f, fAngle, vAngle);
            AngleMatrix(vAngle, Matrix);
            VectorRotate(vPos, Matrix, vPos2);
            VectorCopy(vPos2, o->Velocity);
        }
        break;
        case 54: {
            //o->TexType = BITMAP_CLOUD;
            o->LifeTime = (int)(o->Scale * 8.0f);
            o->Scale += (float)(WorldRandom() % 50) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 30 + 50) * 0.05f;
        }
        break;
        case 55: {
            o->LifeTime = 30;
            o->Scale = Scale + (float)(WorldRandom() % 32 + 48) * 0.01f * Scale;
            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Rotation = (float)((int)WorldTime % 360);
        }
        break;
        case 58:
            o->LifeTime = 50;
            o->Scale = (float)(WorldRandom() % 32 + 80) * 0.01f;
            o->Position[0] += ((float)(WorldRandom() % 64 - 32));
            o->Position[1] += ((float)(WorldRandom() % 64 - 32));
            o->Position[2] += ((float)(WorldRandom() % 64 + 32));
            o->Angle[0] = (float)(WorldRandom() % 90 - 45);
            o->Angle[2] = (float)(WorldRandom() % 360);
            Vector(0.f, -(float)(WorldRandom() % 8 + 40), 0.f, o->Velocity);
            VectorCopy(o->Light, o->StartPosition);
            break;
        case 60: {
            Vector(0.4f, 0.4f, 0.4f, o->Light);
            o->LifeTime = 60;
            o->Scale *= (float)(WorldRandom() % 64 + 64) * 0.005f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 32 + 60) * 0.1f;
        }
        break;
        case 62: {
            o->LifeTime = 50;
            //	Vector(0.9f,0.4f,0.1f,o->Light);
            //	o->Scale = (float)(WorldRandom()%32+80)*0.01f;
            o->Scale = o->Scale;
            o->Position[0] += ((float)(WorldRandom() % 64 - 32));
            o->Position[1] += ((float)(WorldRandom() % 64 - 32));
            o->Angle[0] = (float)(WorldRandom() % 90 - 45);
            o->Angle[2] = (float)(WorldRandom() % 360);
            Vector(0.f, -(float)(WorldRandom() % 8), 0.f, o->Velocity);
        }
        break;
        case 63: {
            o->TexType = BITMAP_CLUD64;
            o->LifeTime = 10;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Angle[0] = 90.0f;
            Vector(0.0f, 30.0f, 15.0f, o->Velocity);
        }
        break;
        case 64: {
            o->LifeTime = 30;
            o->Scale *= 0.3f;

            o->Velocity[0] = 0.0f;
            o->Velocity[1] = 10.0f;
            o->Velocity[2] = 0.0f;

            const float ANG_REVISION = 20.0f;
            float fAng;
            fAng = (float)((WorldRandom() % (int)ANG_REVISION) + 10);

            o->Angle[0] = o->Angle[0] + fAng;
        }
        break;
        case 65: {
            o->LifeTime = 45;
            o->Scale *= (float)(WorldRandom() % 64 + 64) * 0.005f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 10 + 18) * 0.1f;
        }
        break;
        case 66: {
            //o->TexType = BITMAP_CLOUD;
            o->LifeTime = (int)(o->Scale * 30.0f);
            o->Scale += (float)(WorldRandom() % 50) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 30 + 50) * 0.01f;
            int iAngle = WorldRandom() % 360;
            o->TurningForce[0] = sinf(iAngle / Q_PI * 180);
            o->TurningForce[1] = cosf(iAngle / Q_PI * 180);
        }
        break;
        case 67: {
            o->LifeTime = 45;
            o->Alpha = 1.0f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 32 + 60) * 0.1f;
        }
        break;
        case 68: {
            o->LifeTime = 45;
            o->Scale *= (float)(WorldRandom() % 64 + 64) * 0.005f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 10 + 18) * 0.1f;
            o->Alpha = 1.0f;

            vec3_t v3BasisDir, v3Dir;
            float matAngle[3][4];
            memset(matAngle, 0, sizeof(float) * 3 * 4);
            Vector(0.0f, 0.0f, 1.0f, v3BasisDir);
            AngleMatrix(o->Angle, matAngle);
            VectorRotate(v3BasisDir, matAngle, v3Dir);

            o->Velocity[0] = v3Dir[0] * 0.6f;
            o->Velocity[1] = v3Dir[1] * 0.6f;
            o->Velocity[2] = v3Dir[2] * 0.6f;
        }
        break;
#ifdef ASG_ADD_MAP_KARUTAN
        case 69:
            o->LifeTime = 60;
            o->Scale *= (float)(WorldRandom() % 64 + 64) * 0.005f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 16 + 30) * 0.1f;
            VectorCopy(Light, o->Angle);
            break;
#endif // ASG_ADD_MAP_KARUTAN
        }
        break;
    case BITMAP_SMOKE + 1:
    case BITMAP_SMOKE + 4:
        o->LifeTime = 32;
        o->Position[0] += ((float)(WorldRandom() % 16 - 8));
        o->Position[1] += ((float)(WorldRandom() % 16 - 8));

        if (o->Type == BITMAP_SMOKE + 4)
        {
            o->Scale = (float)(WorldRandom() % 32 + 32) * 0.01f;
            o->Scale *= Scale;
        }
        else
        {
            o->Scale = (float)(WorldRandom() % 32 + 32) * 0.01f * Scale;
        }

        if (SubType == 0)
        {
            AngleMatrix(Angle, Matrix);
            Vector(0.f, 3.f, 0.f, p);
        }
        else
        {
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            Vector(0.f, 15.f, 0.f, p);
        }
        VectorRotate(p, Matrix, o->Velocity);
        o->Angle[0] = (float)(WorldRandom() % 360);

        if (SubType == 1)
        {
            o->LifeTime = 40;
            Vector(0.f, 0.0f, 0.0f, o->Velocity);
            o->Position[2] += ((float)(WorldRandom() % 16 - 8));
        }
        else if (SubType == 6)
        {
            o->LifeTime = 40;
            Vector(0.f, 0.0f, 0.0f, o->Velocity);
            o->Position[2] += ((float)(WorldRandom() % 16 - 8));
        }
        break;
    case BITMAP_SMOKE + 2:
        o->LifeTime = 50;
        o->Scale = (float)(WorldRandom() % 32 + 64) * 0.01f;
        Vector(0.f, 0.f, (float)(WorldRandom() % 360), o->Angle);
        AngleMatrix(o->Angle, Matrix);
        Vector(0.f, (float)(WorldRandom() % 64), (float)(WorldRandom() % 16 + 16), p);
        VectorRotate(p, Matrix, o->StartPosition);
        Vector(0.f, 0.f, 0.f, o->Angle);
        //o->Rotation = (float)(WorldRandom()%360);
        //Vector(0.f,-(float)(WorldRandom()%8+40),0.f,o->Velocity);
        break;
    case BITMAP_SMOKE + 3:
        switch (SubType)
        {
        case 0:
            VectorCopy(o->Light, o->TurningForce);
            o->LifeTime = 55;
            //o->Scale = (float)(WorldRandom()%32+48)*0.01f;
            o->Angle[0] = -2 + (float)(WorldRandom() % 4);
            o->Rotation = (float)((int)WorldTime % 360);
            break;
        case 1:
            VectorCopy(o->Light, o->TurningForce);
            o->LifeTime = 30;
            //o->Scale = (float)(WorldRandom()%32+48)*0.01f;
            o->Angle[0] = -2 + (float)(WorldRandom() % 4);
            o->Rotation = (float)((int)WorldTime % 360);
            break;
        case 2:
            VectorCopy(o->Light, o->TurningForce);
            o->LifeTime = 55;
            //o->Scale = (float)(WorldRandom()%32+48)*0.01f;
            o->Angle[0] = -2 + (float)(WorldRandom() % 4);
            o->Rotation = (float)((int)WorldTime % 360);
            break;
        case 3:
            o->TexType = BITMAP_CLUD64;
            o->LifeTime = 60;
            o->Scale *= (float)(WorldRandom() % 64 + 64) * 0.02f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Position[0] += ((float)(WorldRandom() % 250 - 125));
            o->Position[1] += ((float)(WorldRandom() % 250 - 125));
            o->Position[2] -= ((float)(WorldRandom() % 45));
            o->Gravity = (float)(WorldRandom() % 100 - 50) * 0.1f;
            Vector(1.0f, 1.0f, 1.0f, o->Light);
            break;
        case 4:
            o->TexType = BITMAP_WATERFALL_3;
            o->LifeTime = 30;
            o->Scale *= (float)(WorldRandom() % 64 + 64) * 0.02f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Position[0] += ((float)(WorldRandom() % 250 - 125));
            o->Position[1] += ((float)(WorldRandom() % 250 - 125));
            o->Position[2] -= ((float)(WorldRandom() % 45));
            o->Gravity = (float)(WorldRandom() % 100 - 50) * 0.1f;
            Vector(1.0f, 1.0f, 1.0f, o->Light);
            break;
        }
        break;
    case BITMAP_SMOKELINE1:
    case BITMAP_SMOKELINE2:
    case BITMAP_SMOKELINE3:
        if (o->SubType == 0 || o->SubType == 5)
        {
            if (gMapManager.ContextMap() == WD_63PK_FIELD)
            {
                o->LifeTime = 25;
            }
            else
            {
                o->LifeTime = 35;
            }
            o->Scale = (float)(WorldRandom() % 96 + 96) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 16 + 12) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
            if (o->SubType == 5)
            {
                VectorCopy(o->Target->Position, o->StartPosition);
            }
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 25;
            o->Scale = (float)(WorldRandom() % 50 + 55) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 4 + 35) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
            VectorCopy(o->Target->Position, o->StartPosition);
        }
        else if ((o->SubType == 2) || (o->SubType == 3))
        {
            o->LifeTime = 45;
            o->Scale = (float)(WorldRandom() % 96 + 96) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 16 + 12) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        else if (o->SubType == 4)
        {
            o->Position[0] += ((float)(WorldRandom() % 20 - 10) * 2.f);
            o->Position[1] += ((float)(WorldRandom() % 20 - 10) * 2.f);
            o->Position[2] += ((float)(WorldRandom() % 20 - 10) * 2.f);

            o->Scale = o->Scale + ((float)(WorldRandom() % 20 - 10) * (o->Scale * 0.03f));
            o->LifeTime = 45;
            o->Alpha = 1.0f;

            o->Gravity = 0.2f;
            o->Rotation = (float)(WorldRandom() % 360);
        }
        break;
    case BITMAP_LIGHTNING_MEGA1:
    case BITMAP_LIGHTNING_MEGA2:
    case BITMAP_LIGHTNING_MEGA3: {
        switch (o->SubType)
        {
        case 0: {
            o->LifeTime = 5;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = 0;
            o->Alpha = 1.0f;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        break;
        }
    }
    break;
    case BITMAP_FIRE_HIK1:
    case BITMAP_FIRE_HIK1_MONO:
        if (o->SubType == 0 || o->SubType == 6)
        {
            o->LifeTime = WorldRandom() % 5 + 27;
            o->Scale = (float)(WorldRandom() % 72 + 72) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 24 + 64) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = WorldRandom() % 5 + 54;
            o->Scale = (float)(WorldRandom() % 72 + 72) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 24 + 74) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = WorldRandom() % 5 + 27;
            o->Scale = (float)(WorldRandom() % 72 + 72) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 24 + 100) * 0.1f;
            float fRand = (float)(WorldRandom() % 50) * 0.03f;
            Vector(2.5f + fRand, -7.0f - fRand, 0.f, o->Velocity);
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = WorldRandom() % 5 + 27;
            o->Scale = (float)(WorldRandom() % 72 + 52) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 14 + 44) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        else if (o->SubType == 4)
        {
            o->LifeTime = 8; //WorldRandom()%5+5;
            o->Scale = (float)(WorldRandom() % 42 + 12) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 14 + 44) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
            VectorCopy(o->Target->Position, o->StartPosition);
        }
        else if (o->SubType == 5)
        {
            o->LifeTime = WorldRandom() % 5 + 27;
            o->Scale = (float)(WorldRandom() % 72 + 72) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 24 + 64) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        if (o->SubType == 10)
        {
            o->LifeTime = WorldRandom() % 5 + 47;
            o->Scale = (float)(WorldRandom() % 72 + 72) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 24 + 64) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        break;

    case BITMAP_FIRE_HIK3:
    case BITMAP_FIRE_HIK3_MONO:
        if (o->SubType == 0 || o->SubType == 6)
        {
            o->LifeTime = WorldRandom() % 5 + 17;
            o->Scale = (float)(WorldRandom() % 72 + 72) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 24 + 64) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = WorldRandom() % 5 + 34;
            o->Scale = (float)(WorldRandom() % 72 + 72) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 24 + 74) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = WorldRandom() % 5 + 17;
            o->Scale = (float)(WorldRandom() % 72 + 72) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 24 + 100) * 0.1f;
            float fRand = (float)(WorldRandom() % 50) * 0.03f;
            Vector(2.5f + fRand, -5.0f - fRand, 0.f, o->Velocity);
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = WorldRandom() % 5 + 17;
            o->Scale = (float)(WorldRandom() % 72 + 52) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 14 + 44) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        else if (o->SubType == 4)
        {
            o->LifeTime = WorldRandom() % 5 + 34;
            o->Scale = (float)(WorldRandom() % 72 + 72) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 10 + 40) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        else if (o->SubType == 5)
        {
            o->LifeTime = 8; //WorldRandom()%5+5;
            o->Scale = (float)(WorldRandom() % 42 + 12) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 14 + 44) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
            VectorCopy(o->Target->Position, o->StartPosition);
        }
        break;
    case BITMAP_LIGHT + 1:
        o->LifeTime = 20 + WorldRandom() % 8;
        if (o->SubType == 1)
        {
            o->LifeTime = 5;
            o->Angle[0] = -2 + (float)(WorldRandom() % 4);
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 19;
            o->Position[1] += (-30.0f + (float)(WorldRandom() % 60));
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = 7;
            o->TexType = BITMAP_SHINY + 1;
        }
        else if (o->SubType == 4)
        {
            o->LifeTime = 4;
            o->Rotation = -360.0f + (float)(WorldRandom() % 180);
            o->TexType = BITMAP_SHINY + 1;
        }
        else if (o->SubType == 5)
        {
            o->LifeTime = 3;
        }
        break;
    case BITMAP_SPARK:
        o->Scale = (float)(WorldRandom() % 4 + 4) * 0.1f;
        if (o->SubType == 0 || o->SubType == 2 || o->SubType == 3 || o->SubType == 4 ||
            o->SubType == 6 || o->SubType == 11 || o->SubType == 13)
        {
            o->LifeTime = WorldRandom() % 16 + 24;
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            o->Gravity = (float)(WorldRandom() % 16 + 6);
            Vector(0.f, (float)(WorldRandom() % 20 + 20) * 0.1f, 0.f, p);

            if (o->SubType == 2)
            {
                o->Scale *= 2.f;
                VectorScale(p, 3.f, p);
            }
            else if (o->SubType == 4)
            {
                o->Scale *= 3.f;
                VectorScale(p, 10.f, p);
            }
            else if (o->SubType == 6)
            {
                o->LifeTime = WorldRandom() % 4 + 4;
                o->Scale = Scale;
                o->Gravity = 0.0f;
                o->TexType = BITMAP_SPARK + 1;
                Vector(0.f, (float)(WorldRandom() % 20 + 20) * 0.2f, 0.f, p);
            }
            else if (o->SubType == 11)
            {
                Vector(1.0f, 0.3f, 0.3f, o->Light);
            }
            else if (o->SubType == 13)
            {
                o->Scale *= 1.6f;
                VectorScale(p, 1.5f, p);
                o->TexType = BITMAP_SPARK + 1;
            }

            VectorRotate(p, Matrix, o->Velocity);
        }
        else if (o->SubType == 7)
        {
            o->LifeTime = WorldRandom() % 12 + 12;
        }
        else if (o->SubType == 8 || o->SubType == 10)
        {
            if (o->SubType == 10)
            {
                o->TexType = BITMAP_CLUD64;
            }
            else
                o->TexType = BITMAP_SPARK + 1;

            o->LifeTime = WorldRandom() % 16 + 24;
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            o->Gravity = (float)(WorldRandom() % 16 + 6);
            Vector(0.f, (float)(WorldRandom() % 60 - 30) * 0.1f, 0.f, p);
            o->Scale *= 1.5f;
            VectorScale(p, 3.f, p);
            VectorRotate(p, Matrix, o->Velocity);
        }
        else if (o->SubType == 9)
        {
            o->Scale = Scale * 1.2f + (float)(WorldRandom() % 4 + 4) * 0.1f;
            o->LifeTime = WorldRandom() % 16 + 50;
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            o->Gravity = (float)(WorldRandom() % 2);
            Vector(0.f, (float)(WorldRandom() % 10 + 10) * 0.05f, 0.f, p);
            VectorRotate(p, Matrix, o->Velocity);
        }
        else if (o->SubType == 12)
        {
            o->Scale = Scale + (float)(WorldRandom() % 4 + 4) * 0.1f;
            o->LifeTime = WorldRandom() % 16 + 24;
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            o->Gravity = (float)(WorldRandom() % 16 + 6);
            Vector(0.f, (float)(WorldRandom() % 20 + 20) * 0.1f, 0.f, p);
            VectorRotate(p, Matrix, o->Velocity);
        }
        else
        {
            if (o->SubType == 5)
            {
                VectorCopy(o->Target->Position, o->StartPosition);
                o->Scale = Scale;
                o->LifeTime = WorldRandom() % 4 + 12;
                o->Gravity = 0;
                o->TexType = BITMAP_SPARK + 1;
            }
            else
            {
                o->LifeTime = WorldRandom() % 8 + 8;
            }

            if (o->SubType == 7)
            {
                o->Scale += Scale;
            }
        }
        break;
    case BITMAP_SPARK + 1:
        switch (o->SubType)
        {
        case 1:
            Vector((float)(WorldRandom() % 360), (float)(WorldRandom() % 360),
                   (float)(WorldRandom() % 360), o->Angle);
            AngleMatrix(o->Angle, Matrix);
            Vector(0.f, -50.f, 0.f, p);
            VectorRotate(p, Matrix, o->Velocity);
            break;
        case 2:
            o->LifeTime = 15;
            Vector(0.f, 0.f, 0.f, o->Angle);
            VectorCopy(o->Position, o->StartPosition);
            o->Velocity[0] = (o->Target->Position[0] - o->Position[0]) / 10.f;
            o->Velocity[1] = (o->Target->Position[1] - o->Position[1]) / 10.f;
            o->Velocity[2] = (o->Target->Position[2] - o->Position[2]) / 10.f;
            break;
        case 3:
            o->LifeTime = 5;
            break;
        case 4:
            o->LifeTime = 10;
            o->Alpha = 0.1f;
            Vector(0.f, 0.f, 0.f, o->Angle);
            VectorCopy(o->Position, o->StartPosition);
            o->Velocity[0] = (o->Target->Position[0] - o->Position[0]) / 6.f;
            o->Velocity[1] = (o->Target->Position[1] - o->Position[1]) / 6.f;
            o->Velocity[2] = (o->Target->Position[2] - o->Position[2]) / 6.f;
            break;
        case 5:
            o->LifeTime = 10;
            Vector(0.f, 0.f, Angle[2], o->Angle);
            AngleMatrix(o->Angle, Matrix);
            Vector(-(WorldRandom() % 2 + 2.f), 0.f, 0.f, p);
            VectorRotate(p, Matrix, o->Velocity);
            break;
        case 6:
            if (Random.FpsCheck(50, 1.0))
            {
                o->LifeTime = 50;
                o->Gravity = 1.0f + (float)(WorldRandom() % 20) / 5.f;
                o->Scale = (float)(WorldRandom() % 5) / 10.0f + 1.0f;
            }
            else
                o->LifeTime = 0;
            break;
        case 7:
            o->Alpha = 1.0f;
            o->Position[1] += (-30.0f + (float)(WorldRandom() % 60));
            o->Position[2] += (-30.0f + (float)(WorldRandom() % 60));
            o->Position[3] += (-30.0f + (float)(WorldRandom() % 60));
            o->LifeTime = 30;
            o->Gravity = 1.3f;
            o->Scale = (float)(WorldRandom() % 5) / 10.0f + 1.0f;
            break;
        case 8:
            o->LifeTime = 10;
            break;
        case 9: {
            o->LifeTime = 30;
            Vector(0.0f, (float)(WorldRandom() % 360), 0.0f, o->Angle);
            AngleMatrix(o->Angle, Matrix);
            Vector(0.0f, 0.0f, (float)(WorldRandom() % 10) + 160.0f, p);
            VectorRotate(p, Matrix, o->Velocity);
            VectorAdd(o->Velocity, o->Position, o->Position);
            Vector(12.0f, 0.0f, -2.0f, o->Velocity);
        }
        break;
        case 10:
            o->Alpha = 1.0f;
            o->Velocity[0] = -4.0f + (float)(WorldRandom() % 8);
            o->Velocity[1] = 0.0f;
            o->Velocity[2] = -4.0f + (float)(WorldRandom() % 8);
            o->LifeTime = 20;
            o->Scale = (float)(WorldRandom() % 5) / 10.0f + 1.0f;
            break;
        case 13:
        case 11:
            o->Alpha = 1.0f;
            o->Velocity[0] = -4.0f + (float)(WorldRandom() % 8);
            o->Velocity[1] = (float)(WorldRandom() % 4);
            o->Velocity[2] = (float)(WorldRandom() % 4);
            o->LifeTime = 30;
            o->Scale = (float)(WorldRandom() % 5) / 10.0f + 1.0f;
            break;
        case 12:
            o->TexType = BITMAP_SHINY;
            o->Alpha = 1.0f;

            vec3_t p1, p2;
            Vector(0.f, -100.f, 0.f, p1);
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(p1, Matrix, p2);
            VectorAdd(o->Position, p2, o->Position);

            o->Position[0] += (-8.0f + (float)(WorldRandom() % 16));
            o->Position[1] += (-8.0f + (float)(WorldRandom() % 16));
            o->Position[2] += (150.0f);

            o->Velocity[0] = -1.0f + (float)(WorldRandom() % 20) / 10.f;
            o->Velocity[1] = -1.0f + (float)(WorldRandom() % 20) / 10.f;
            o->Velocity[2] = -(float)(WorldRandom() % 3) - 3.0f;
            o->LifeTime = 40;
            o->Scale += -(float)(WorldRandom() % 15) / 10.0f;
            break;
        case 14:
            if (Random.FpsCheck(2, 1.0))
            {
                o->Alpha = 1.0f;
                o->Velocity[0] = -4.0f + (float)(WorldRandom() % 4);
                o->Velocity[1] = (float)(WorldRandom() % 4);
                o->Velocity[2] = (float)(WorldRandom() % 4);
                o->LifeTime = 30;
                o->Scale = (float)(WorldRandom() % 5) / 10.0f + 0.4f;
            }
            break;
        case 15:
            if (Random.FpsCheck(3, 1.0))
            {
                o->Alpha = 1.0f;
                o->LifeTime = 35;
                o->Velocity[2] = -((float)(WorldRandom() % 5) + 5) * 0.5f;
            }
            break;
        case 16:
        case 18: {
            o->Alpha = 1.0f;
            o->LifeTime = 100;
            if (o->SubType == 18)
            {
                o->LifeTime = 80;
            }
            o->Frame = 77;
            o->Scale = -(float)(WorldRandom() % 3) / 3.0f + Scale + 0.01f;

            o->Velocity[0] = (float)(WorldRandom() % 50) / 10.0f - 2.5f;
            o->Velocity[1] = (float)(WorldRandom() % 4) / 10.0f - 0.2f;
            if (o->SubType == 18)
            {
                o->Velocity[1] = (float)(WorldRandom() % 50) / 10.0f - 2.5f;
            }
            o->Velocity[2] = (float)(WorldRandom() % 50) / 10.0f - 2.5f;

            vec3_t p1, p2;
            Vector(o->Velocity[0] * 10.0f, o->Velocity[1] * 10.0f, o->Velocity[2] * 10.0f + 100.0f,
                   p1);
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(p1, Matrix, p2);
            VectorAdd(o->Position, p2, o->Position);

            o->Gravity = 3.5f;
        }
        break;
        case 17: {
            o->LifeTime = 200;
            o->Scale += (float)(WorldRandom() % 3) * 0.1f;
            o->Gravity = (float)(WorldRandom() % 5) * 0.3f + 2.0f;
            o->Rotation = (float)(WorldRandom() % 20) * 0.0001f + 0.002f;
            o->Alpha = (float)(WorldRandom() % 10) * 0.1f + 0.5f;
            VectorCopy(o->Position, o->StartPosition);
        }
        break;
        case 19: {
            o->LifeTime = 50;
            o->Gravity = 1.0f + (float)(WorldRandom() % 20) * 0.1;
            o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) - 5.f;
            o->Scale = (float)(WorldRandom() % 10) * 0.08f + 0.8f;
            VectorCopy(Light, o->Light);
        }
        break;
        case 20: {
            o->Scale = Scale + (WorldRandom() % 10) * 0.02f;
            o->LifeTime = WorldRandom() % 10 + 60;
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            o->Gravity = (float)(WorldRandom() % 10 + 15);
            Vector((float)(WorldRandom() % 40 - 20) * 0.1f, (float)(WorldRandom() % 40 - 20) * 0.1f,
                   0.f, p);
            VectorScale(p, 2.5f, p);
            VectorRotate(p, Matrix, o->Velocity);
        }
        break;

        case 21: {
            vec3_t vSpeed;
            vSpeed[0] = -8.0f + (float)(WorldRandom() % 16);
            vSpeed[1] = -8.0f + (float)(WorldRandom() % 16);
            vSpeed[2] = -8.0f + (float)(WorldRandom() % 16);

            VectorNormalize(vSpeed);
            VectorScale(vSpeed, 12.0f, vSpeed);
            VectorCopy(vSpeed, o->Velocity);

            o->LifeTime = WorldRandom() % 10 + 20;
            o->Scale = (float)(WorldRandom() % 20) / 20.0f + 1.0f;
        }
        break;
        case 22: {
            vec3_t vSpeed;
            vSpeed[0] = -8.0f + (float)(WorldRandom() % 16);
            vSpeed[1] = -8.0f + (float)(WorldRandom() % 16);
            vSpeed[2] = -8.0f + (float)(WorldRandom() % 16);

            VectorNormalize(vSpeed);
            VectorScale(vSpeed, 14.0f, vSpeed);
            VectorCopy(vSpeed, o->Velocity);

            o->Scale = (float)(WorldRandom() % 20) / 20.0f + 1.0f;
            o->LifeTime = WorldRandom() % 10 + 20;
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            o->Gravity = 20.0f; //(float)(WorldRandom()%10+15);
            Vector((float)(WorldRandom() % 40 - 20) * 0.1f, (float)(WorldRandom() % 40 - 20) * 0.1f,
                   0.f, p);
            VectorScale(p, 2.5f, p);
            VectorRotate(p, Matrix, o->Velocity);
        }
        break;
        case 23:
            o->Alpha = 1.0f;
            o->Velocity[0] = (float)(-1 + WorldRandom() % 3) * 1.5f;
            o->Velocity[1] = (float)(-1 + WorldRandom() % 3) * 1.5f;
            o->Velocity[2] = (float)(-1 + WorldRandom() % 3) * 1.5f;
            o->LifeTime = 20;
            o->Rotation = 0;
            o->Scale = (float)(WorldRandom() % 5) / 10.0f + 0.8f;
            break;
        case 24:
            o->Alpha = 1.0f;
            o->Velocity[0] = (float)(-1 + WorldRandom() % 3) * 2.0f;
            o->Velocity[1] = (float)(-1 + WorldRandom() % 3) * 2.0f;
            o->Velocity[2] = (float)(-1 + WorldRandom() % 3) * 2.0f;
            o->LifeTime = 16;
            o->Rotation = 0;
            o->Scale = (float)(WorldRandom() % 5) / 10.0f + 0.4f;
            break;
        case 25:
            o->Alpha = 1.0f;
            o->Velocity[0] = -2.0f + (float)(WorldRandom() % 6);
            o->Velocity[1] = (float)(WorldRandom() % 4);
            o->Velocity[2] = (float)(WorldRandom() % 4);
            o->LifeTime = 20;
            break;
        case 26:
            o->LifeTime = 20;
            Vector(0.f, 0.f, 0.f, o->Angle);
            VectorCopy(o->Position, o->StartPosition);
            o->Velocity[0] = (o->Target->Position[0] - o->Position[0]) / 20.f;
            o->Velocity[1] = (o->Target->Position[1] - o->Position[1]) / 20.f;
            o->Velocity[2] = (o->Target->Position[2] - o->Position[2]) / 20.f;
            //Vector(1.f, 0.f, 0.f, o->Light);
            break;
        case 27: {
            vec3_t vSpeed;
            vSpeed[0] = -8.0f + (float)(WorldRandom() % 16);
            vSpeed[1] = -8.0f + (float)(WorldRandom() % 16);
            vSpeed[2] = -8.0f + (float)(WorldRandom() % 16);

            VectorNormalize(vSpeed);
            VectorScale(vSpeed, 12.0f, vSpeed);
            VectorCopy(vSpeed, o->Velocity);

            o->LifeTime = WorldRandom() % 10 + 10;
            o->Scale = (float)(WorldRandom() % 20) / 20.0f + 1.0f;
        }
        break;
        case 28: {
            o->Scale = (float)(WorldRandom() % 20) / 20.0f + 1.0f;
            o->LifeTime = WorldRandom() % 10 + 20;
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            o->Gravity = 20.0f;
            Vector((float)(WorldRandom() % 40 - 20) * 0.1f, (float)(WorldRandom() % 40 - 20) * 0.1f,
                   0.f, p);
            VectorScale(p, 2.5f, p);
            VectorRotate(p, Matrix, o->Velocity);
        }
        break;
        case 29: {
            o->Scale = (float)(WorldRandom() % 20) / 20.0f + 0.5f;
            o->LifeTime = WorldRandom() % 10 + 10;
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            o->Gravity = -20.0f;
            Vector((float)(WorldRandom() % 40 - 20) * 0.1f, (float)(WorldRandom() % 40 - 20) * 0.1f,
                   0.f, p);
            VectorScale(p, 2.5f, p);
            VectorRotate(p, Matrix, o->Velocity);
        }
        break;
        case 30: {
            o->LifeTime = WorldRandom() % 5 + 50;

            o->Scale = (float)(WorldRandom() % 10) / 10.0f + 0.2f;

            o->Velocity[0] = -2.0f + (float)(WorldRandom() % 5);
            o->Velocity[1] = -2.0f + (float)(WorldRandom() % 5);
            o->Velocity[2] = -2.0f + (float)(WorldRandom() % 5);

            o->Gravity = 0.5f;
        }
        break;
        case 31: {
            o->Scale = (float)(WorldRandom() % 4 + 4) * 0.1f;
            o->LifeTime = WorldRandom() % 16 + 24;
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            o->Gravity = (float)(WorldRandom() % 16 + 6);
            Vector(0.f, (float)(WorldRandom() % 20 + 20) * 0.1f, 0.f, p);

            VectorScale(p, 3.f, p);

            VectorRotate(p, Matrix, o->Velocity);
        }
        break;
        }
        break;
    case BITMAP_SPARK + 2:
        if (o->SubType == 0)
        {
            o->LifeTime = 16;
            o->Scale = Scale + (float)(WorldRandom() % 10 - 5) * 0.03f;
            o->Position[0] += ((float)(WorldRandom() % 10 - 5));
            o->Position[1] += ((float)(WorldRandom() % 10 - 5));
            o->Position[2] += ((float)(WorldRandom() % 10 - 5));
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 12;
        }
        else if (o->SubType == 2 || o->SubType == 3)
        {
            o->Scale = 0.4f;
            o->LifeTime = 16;
        }
        break;

    case BITMAP_EXPLOTION_MONO:
    case BITMAP_EXPLOTION:
        o->LifeTime = 20;
        o->Scale = Scale;
        if (o->SubType == 2)
        {
            o->LifeTime = 30;
        }

        {
            if (o->SubType == 10)
            {
                PlayBuffer(SOUND_MOONRABBIT_EXPLOSION);
            }
            else
            {
                if (SceneFlag != LOG_IN_SCENE)
                    PlayBuffer(SOUND_EXPLOTION01);
            }
        }
        break;
    case BITMAP_SUMMON_SAHAMUTT_EXPLOSION:
        o->LifeTime = 16;
        o->Scale = Scale;
        break;
    case BITMAP_SPOT_WATER:
        o->LifeTime = 32;
        o->Scale = (float)((WorldRandom() % 170 + 1) * 0.01f);
        break;
    case BITMAP_FLARE_RED:
        o->LifeTime = 18;
        HandPosition(o);
        break;

    case BITMAP_EXPLOTION + 1:
        o->LifeTime = 12;
        o->Scale = Scale;
        PlayBuffer(SOUND_EXPLOTION01);
        break;

    case BITMAP_SHINY:
        o->LifeTime = 18;
        o->Angle[0] = 45.f;
        if (o->SubType == 2)
        {
            o->LifeTime = 25;
            o->Rotation = (float)(WorldRandom() % 360);

            o->Velocity[0] = -0.1f;
            o->Velocity[1] = -0.5f;
            o->Velocity[2] = -1.f;
        }
        else if (o->SubType == 3)
        {
            o->Alpha = 1.0f;
            o->Velocity[0] = -4.0f + (float)(WorldRandom() % 8);
            o->Velocity[1] = (float)(WorldRandom() % 4);
            o->Velocity[2] = (float)(WorldRandom() % 4);
            o->LifeTime = 30;
            o->Scale = (float)(WorldRandom() % 10) / 20.0f + 0.5f;
        }
        else if (o->SubType == 4)
        {
            o->Scale = Scale + (WorldRandom() % 10) * 0.02f;
            o->LifeTime = WorldRandom() % 10 + 15;
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            o->Gravity = (float)(WorldRandom() % 10 + 10);
            Vector((float)(WorldRandom() % 40 - 20) * 0.1f, (float)(WorldRandom() % 40 - 20) * 0.1f,
                   0.f, p);
            VectorScale(p, 2.0f, p);
            VectorRotate(p, Matrix, o->Velocity);
        }
        else if (o->SubType == 5)
        {
            o->LifeTime = WorldRandom() % 10 + 10;
            o->Scale += (WorldRandom() % 10) * 0.02f;
            o->Position[0] += ((float)(WorldRandom() % 10 - 5));
            o->Position[1] += ((float)(WorldRandom() % 10 - 5));
            o->Gravity = -(float)(WorldRandom() % 10) * 0.3f;
        }
        else if (o->SubType == 6)
        {
            o->Scale = (float)(WorldRandom() % 5) / 10.0f + 0.5f;
            o->StartPosition[0] = o->Scale;
            o->LifeTime = WorldRandom() % 10 + 60;
            o->Gravity = 20.0f;

            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Angle[1] = (float)(WorldRandom() % 360);
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            Vector(1, 1, 1, p);
            VectorScale(p, 8.0f, p);
            VectorRotate(p, Matrix, o->Velocity);

            o->Rotation = WorldRandom() % 20 - 10;
        }
        else if (o->SubType == 7)
        {
            o->Scale = 0.1f; //(float)(WorldRandom()%5)/6.0f+0.5f;
            o->StartPosition[0] = o->Scale;
            o->LifeTime = WorldRandom() % 10 + 20;
            o->Position[0] += ((float)(WorldRandom() % 50 - 25));
            o->Position[1] += ((float)(WorldRandom() % 50 - 25));
            o->Position[2] += ((float)(WorldRandom() % 50 - 25));

            o->Gravity = 1.0f;

            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Angle[1] = (float)(WorldRandom() % 360);
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            Vector(0.5f, 0.5f, 0.5f, p);
            VectorScale(p, 0.8f, p);
            VectorRotate(p, Matrix, o->Velocity);

            o->Rotation = WorldRandom() % 20 - 10;
        }
        else if (o->SubType == 8)
        {
            o->Scale = (float)(WorldRandom() % 5) / 10.0f + 0.5f;
            o->StartPosition[0] = o->Scale;
            o->LifeTime = WorldRandom() % 10 + 40;
            o->Gravity = 20.0f;
            Vector(0, 0, 1.0f, o->Velocity);

            o->Rotation = WorldRandom() % 20 - 10;
        }
        else if (o->SubType == 9)
        {
            o->Scale = (float)(WorldRandom() % 5) / 10.0f + 0.5f;
            o->StartPosition[0] = o->Scale;
            o->LifeTime = WorldRandom() % 10 + 60;
            o->Gravity = 20.0f;

            o->Angle[0] = (float)(WorldRandom() % 360);
            o->Angle[1] = (float)(WorldRandom() % 360);
            o->Angle[2] = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            Vector(1, 1, 1, p);
            VectorScale(p, 8.0f, p);
            VectorRotate(p, Matrix, o->Velocity);

            o->Rotation = WorldRandom() % 20 - 10;
        }
        break;
    case BITMAP_CHERRYBLOSSOM_EVENT_PETAL: {
        if (o->SubType == 0)
        {
            o->Alpha = 1.0f;
            o->LifeTime = 30;
            o->Scale = (float)(WorldRandom() % 10) / 20.0f + 0.5f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = -(float)(WorldRandom() % 10 + 10);
            Vector(0.0f, 0.0f, 0.0f, o->Angle);
            o->Velocity[2] = -4.0f + (float)(WorldRandom() % 8);
            o->Velocity[1] = (float)(WorldRandom() % 4);
            o->Velocity[0] = (float)(WorldRandom() % 4);
        }
        else if (o->SubType == 1)
        {
            o->Alpha = 1.0f;
            o->LifeTime = 70;
            o->Alpha = 1.0f;

            o->Velocity[0] = -2.0f + (float)(WorldRandom() % 6);
            o->Velocity[1] = (float)(WorldRandom() % 4);
            o->Velocity[2] = (float)(WorldRandom() % 4);

            //o->Velocity[0] = -2.0f + (float)(WorldRandom()%2);
            //o->Velocity[1] = (float)(WorldRandom()%2);
            //o->Velocity[2] = (float)(WorldRandom()%2);
            o->Rotation = (float)(WorldRandom() % 360);
        }
    }
    break;

    case BITMAP_CHERRYBLOSSOM_EVENT_FLOWER: {
        if (o->SubType == 0)
        {
            vec3_t vSpeed;
            vSpeed[0] = -8.0f + (float)(WorldRandom() % 16);
            vSpeed[1] = -8.0f + (float)(WorldRandom() % 16);
            vSpeed[2] = -8.0f + (float)(WorldRandom() % 16);

            VectorNormalize(vSpeed);
            VectorScale(vSpeed, 14.0f, vSpeed);
            VectorCopy(vSpeed, o->Velocity);

            o->Alpha = 1.0f;
            //o->Scale = (float)(WorldRandom()%20)/20.0f+1.0f;	//(1~2 20단계)
            o->LifeTime = WorldRandom() % 30 + 20;
            o->Angle[2] = (float)(WorldRandom() % 360);
            o->Rotation = (float)(WorldRandom() % 360);
            AngleMatrix(o->Angle, Matrix);
            o->Gravity = 20.0f; //(float)(WorldRandom()%10+15);
            Vector((float)(WorldRandom() % 40 - 20) * 0.1f, (float)(WorldRandom() % 40 - 20) * 0.1f,
                   0.f, p);
            VectorScale(p, 2.5f, p);
            VectorRotate(p, Matrix, o->Velocity);
        }
    }
    break;
    case BITMAP_SHINY + 1:
        if (o->SubType == 5)
        {
            o->LifeTime = 14;
            //Vector(0.f,-(float)(WorldRandom()%16+32)*0.1f,0.f,o->Velocity);
            //o->Scale = (float)(WorldRandom()%70+40)*0.009f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = 0.0f;
            Vector(0.f, 0.f, 0.f, o->Velocity);
            //Vector ( 0.5f, 0.5f, 0.5f, o->Light );
        }
        else
        {
            o->LifeTime = 36;
            o->Angle[0] = 45.f;
            if (o->SubType != 99)
            {
                HandPosition(o);
            }
        }
        break;
    case BITMAP_SHINY + 2:
        o->LifeTime = 18;
        HandPosition(o);
        break;
    case BITMAP_SHINY + 4:
        o->LifeTime = 20;
        if (o->SubType == 1)
        {
            o->LifeTime = 15;
            o->Gravity = 0.f;
            o->Rotation = (float)(WorldRandom() % 360);
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 8;
        }
        break;
    case BITMAP_SHINY + 6:
        if (o->SubType == 0)
        {
            o->Alpha = 1.0f;
            o->Position[0] += (-5.0f + (float)(WorldRandom() % 10));
            o->Position[1] += (-5.0f + (float)(WorldRandom() % 10));
            o->Position[2] += (-5.0f + (float)(WorldRandom() % 10));
            o->LifeTime = 30;
            o->Gravity = 1.3f;
            o->Scale += (float)(WorldRandom() % 3) / 10.0f;
        }
        else if (o->SubType == 1)
        {
            o->Alpha = 1.0f;
            o->LifeTime = WorldRandom() % 3 + 10;
            o->Scale = 0.3f;

            o->Position[0] += ((float)(WorldRandom() % 50 - 25));
            o->Position[1] += ((float)(WorldRandom() % 50 - 25));
            o->Position[2] += ((float)(WorldRandom() % 50 - 25));
            o->Rotation = WorldRandom() % 20 - 10;
        }
        break;
    case BITMAP_PIN_LIGHT:
        o->Alpha = 1.0f;

        {
            o->Position[0] += (-5.0f + (float)(WorldRandom() % 10));
            o->Position[1] += (-5.0f + (float)(WorldRandom() % 10));
            o->Position[2] += (-5.0f + (float)(WorldRandom() % 10));
        }
        o->LifeTime = 30;
        o->Gravity = 10.0f;
        o->Scale += (float)((WorldRandom() % 5) / 10.0f);
        if (o->SubType == 2)
        {
            o->Gravity = -5.0f;
        }
        else if (o->SubType == 3)
        {
            o->Gravity = -5.0f;
            o->TexType = BITMAP_SHINY + 6;
            o->Rotation = WorldRandom() % 360;
        }
        break;
    case BITMAP_ORORA:
        o->Scale = 0.2f;
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
        break;
    case BITMAP_SNOW_EFFECT_1:
    case BITMAP_SNOW_EFFECT_2:
        o->Scale = Scale + (WorldRandom() % 10) * 0.02f;
        o->LifeTime = WorldRandom() % 10 + 30;
        o->Angle[2] = (float)(WorldRandom() % 360);
        AngleMatrix(o->Angle, Matrix);
        o->Gravity = (float)(WorldRandom() % 10 + 20);
        Vector((float)(WorldRandom() % 40 - 20) * 0.1f, (float)(WorldRandom() % 40 - 20) * 0.1f,
               0.f, p);
        VectorScale(p, 1.5f, p);
        VectorRotate(p, Matrix, o->Velocity);
        break;

    case BITMAP_DS_EFFECT:
        o->Scale = Scale + (WorldRandom() % 10) * 0.02f;
        o->LifeTime = 100;
        break;
    case BITMAP_BLOOD:
    case BITMAP_BLOOD + 1:
        o->LifeTime = 12;
        o->Scale = (float)(WorldRandom() % 4 + 8) * 0.05f;
        o->Angle[0] = (float)(WorldRandom() % 360);
        //o->Gravity = (float)(WorldRandom()%16+32)*0.1f;
        AngleMatrix(Angle, Matrix);
        Vector(0.f, -(float)(WorldRandom() % 16 + 8), (float)(WorldRandom() % 6 - 3), p);
        VectorRotate(p, Matrix, o->Velocity);
        if (o->Type == BITMAP_BLOOD + 1)
        {
            Vector(0.1f, 0.f, 0.f, o->Light);
        }
        break;
    case BITMAP_FIRECRACKER:
        o->Angle[0] = o->Angle[1] = o->Angle[2] = 0.0f;
        o->Rotation = (float)(WorldRandom() % 360);
        if (SubType == -1)
        {
            o->LifeTime = 2;
            o->Scale = 0.25f;
        }
        else
        {
            int iSize = 15 + SubType;
            o->Velocity[0] = (float)(WorldRandom() % iSize - iSize / 2) * 0.15f;
            o->Velocity[1] = (float)(WorldRandom() % iSize - iSize / 2) * 0.15f;
            o->Velocity[2] = (float)(WorldRandom() % iSize - iSize / 2) * 0.15f + 10.0f;
            o->LifeTime = 50;
            o->Scale = 0.10f;
        }
        break;
    case BITMAP_SWORD_FORCE:
        o->LifeTime = 10;
        o->Gravity = 0.1f;
        o->Rotation = o->Angle[2] + 135.f;
        break;
    case BITMAP_CLOUD:
        if (o->SubType == 6)
        {
            o->LifeTime = 25 + WorldRandom() % 5;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Scale = 0.2f;
            o->Velocity[0] = (float)(WorldRandom() % 10 + 5) * 0.4f;
            o->Velocity[1] = 0.f;
            o->Velocity[2] = (float)(WorldRandom() % 10 + 5) * 0.2f;
        }
        else if (o->SubType == 8)
        {
            o->LifeTime = 300;
            o->Gravity = (float)(WorldRandom() % 1000);
            o->Alpha = 0.1f;

            o->Position[0] += ((float)(WorldRandom() % 500 - 250));
            o->Position[1] += ((float)(WorldRandom() % 500 - 250));
            o->Position[2] += ((float)(WorldRandom() % 20 - 20));

            o->StartPosition[1] = (WorldRandom() % 100) / 100.f;
            o->StartPosition[2] = o->Position[2];

            o->Scale = (float)(WorldRandom() % 20 + 180) * 0.01f;
            o->TurningForce[0] = Scale + WorldRandom() % 30 / 100.f;
            Vector(0.f, 0.f, 0.f, o->Velocity);
        }
        else if (o->SubType == 9)
        {
            o->LifeTime = 300;
            o->Gravity = (float)(WorldRandom() % 1000);
            o->Alpha = 0.1f;

            o->Position[0] += ((float)(WorldRandom() % 500 - 250));
            o->Position[1] += ((float)(WorldRandom() % 500 - 250));
            o->Position[2] += ((float)(WorldRandom() % 20 - 20));

            o->StartPosition[1] = (WorldRandom() % 100) / 100.f;
            o->StartPosition[2] = o->Position[2];

            o->Scale = (((float)(WorldRandom() % 20 + 180) * 0.01f) * o->Scale);
            o->TurningForce[0] = WorldRandom() % 10 / 100.f;
            Vector(0.f, 0.f, 0.f, o->Velocity);
        }
        else if (o->SubType == 10)
        {
            Vector(0.0f, 0.0f, 0.0f, o->Light);
            o->LifeTime = 500;
            o->Position[0] += ((float)(WorldRandom() % 400 - 200));
            o->Position[1] += ((float)(WorldRandom() % 400 - 200));
            o->Position[2] += ((float)(WorldRandom() % 400 - 200));
            VectorCopy(o->Position, o->StartPosition);
            o->Scale = (float)(WorldRandom() % 15 + 10) / 15.0f + Scale;
            float fTemp = (float)(WorldRandom() % 10 + 5) * 0.12f;
            o->Velocity[0] = fTemp;
            o->Velocity[1] = fTemp;
            o->Velocity[2] = 0.0f;
        }
        else if (o->SubType == 11)
        {
            o->LifeTime = 500;
            o->Position[0] += ((float)(WorldRandom() % 400 - 200));
            o->Position[1] += ((float)(WorldRandom() % 400 - 200));
            o->Position[2] += ((float)(WorldRandom() % 400 - 200));
            VectorCopy(o->Position, o->StartPosition);
            o->Scale = (float)(WorldRandom() % 15 + 15) / 30.0f + Scale;
            float fTemp = (float)(WorldRandom() % 10 + 5) * 0.12f;
            o->Velocity[0] = fTemp;
            o->Velocity[1] = fTemp;
            o->Velocity[2] = 0.0f;
        }
        else if (o->SubType == 12)
        {
            o->LifeTime = 30;
            o->Gravity = (float)(WorldRandom() % 1000);

            o->Position[0] += ((float)(WorldRandom() % 500 - 250)); //*Scale);
            o->Position[1] += ((float)(WorldRandom() % 500 - 250)); //*Scale);
            o->Position[2] += ((float)(WorldRandom() % 20 + 20));

            o->StartPosition[1] = (WorldRandom() % 100) / 100.f;
            o->StartPosition[2] = o->Position[2];

            o->Scale = (float)(WorldRandom() % 20 + 180) * 0.01f;
            o->TurningForce[0] = Scale + WorldRandom() % 30 / 100.f;
            Vector(0.f, 0.f, 0.f, o->Velocity);
        }
        else if (o->SubType == 13)
        {
            o->LifeTime = 30;
            o->Rotation = WorldRandom() % 360;
            o->Gravity = (float)(WorldRandom() % 50 - 25) / 10.0f;
        }
        else if (o->SubType == 14)
        {
            Vector(0.f, 0.f, 0.f, Light);
            o->LifeTime = 25 + WorldRandom() % 5;
            o->Rotation = float(WorldRandom() % 360);
            o->Scale = Scale * 0.2f;
            o->Velocity[0] = (float)(WorldRandom() % 10 + 5) * 0.4f;
            o->Velocity[1] = 0.f;
            o->Velocity[2] = (float)(WorldRandom() % 10 + 5) * 0.2f;
        }
        else if (o->SubType == 15)
        {
            o->LifeTime = 500;

            o->Position[0] += ((float)(WorldRandom() % 400 - 200));
            o->Position[1] += ((float)(WorldRandom() % 400 - 200));
            o->Position[2] += ((float)(WorldRandom() % 400 - 200));
            VectorCopy(o->Position, o->StartPosition);

            o->Rotation = WorldRandom() % 360;

            o->Scale = (float)(WorldRandom() % 15 + 15) / 30.0f + Scale;

            float fTemp = (float)(WorldRandom() % 10 + 5) * 0.08f;
            o->Velocity[0] = fTemp;
            o->Velocity[1] = fTemp;
            o->Velocity[2] = 0.0f;

            VectorCopy(o->Light, o->TurningForce);
            o->Light[0] = 0.f;
            o->Light[1] = 0.f;
            o->Light[2] = 0.f;
        }
        else if (o->SubType == 16)
        {
            Vector(0.0f, 0.0f, 0.0f, o->Light);

            o->LifeTime = 500;

            o->Position[0] += ((float)(WorldRandom() % 400 - 200));
            o->Position[1] += ((float)(WorldRandom() % 400 - 200));
            o->Position[2] += ((float)(WorldRandom() % 400 - 200));
            VectorCopy(o->Position, o->StartPosition);

            o->Rotation = WorldRandom() % 360;

            o->Scale = (float)(WorldRandom() % 15 + 10) / 15.0f + Scale;

            float fTemp = (float)(WorldRandom() % 10 + 5) * 0.05f;
            o->Velocity[0] = fTemp;
            o->Velocity[1] = fTemp;
            o->Velocity[2] = 0.0f;
        }
        else if (o->SubType == 17)
        {
            o->TexType = BITMAP_EVENT_CLOUD;
            o->LifeTime = 500;

            o->Position[0] += ((float)(WorldRandom() % 900 - 450));
            o->Position[1] += ((float)(WorldRandom() % 900 - 450));
            o->Position[2] += ((float)(WorldRandom() % 20 + 50));
            VectorCopy(o->Position, o->StartPosition);

            o->Rotation = WorldRandom() % 360;
            o->Scale = (float)(WorldRandom() % 20 + 20) / 80.0f + Scale / 3.f;

            float fTemp = (float)(WorldRandom() % 10 + 5) * 0.006f;
            o->Velocity[0] = fTemp;
            o->Velocity[1] = fTemp;
            o->Velocity[2] = 0.0f;

            VectorCopy(o->Light, o->TurningForce);
            o->Light[0] = 0.f;
            o->Light[1] = 0.f;
            o->Light[2] = 0.f;
        }
        else if (o->SubType == 18)
        {
            o->TexType = BITMAP_CHROME + 2;

            o->LifeTime = 160;
            o->Light[0] = 0.f;
            o->Light[1] = 0.f;
            o->Light[2] = 0.f;
            o->Gravity = (float)(WorldRandom() % 1000);

            VectorCopy(Position, o->Position);
            o->Position[2] += ((float)(WorldRandom() % 80));
            o->Rotation = WorldRandom() % 360;

            o->StartPosition[0] = (WorldRandom() % 200 - 10) / 10.0f;
            o->StartPosition[1] = (WorldRandom() % 200 - 10) / 10.0f;

            o->Scale = (float)(WorldRandom() % 70 + 5) * 0.02f;
            o->TurningForce[0] = float(WorldRandom() % 40 + 10) / 10000.f;
            o->TurningForce[1] = WorldRandom() % 120 + 80;
            Vector(0.f, 0.f, 0.f, o->Velocity);
        }
        else if (o->SubType == 19)
        {
            o->TexType = BITMAP_CHROME + 2;

            o->LifeTime = 60;
            o->Light[0] = 0.f;
            o->Light[1] = 0.f;
            o->Light[2] = 0.f;
            o->Gravity = (float)(WorldRandom() % 1000);

            o->Position[0] += ((float)(WorldRandom() % 500 - 250)); //*Scale)
            o->Position[1] += ((float)(WorldRandom() % 500 - 250)); //*Scale)
            o->Position[2] += ((float)(WorldRandom() % 20 + 20));
            o->Rotation = WorldRandom() % 360;

            o->StartPosition[1] = (WorldRandom() % 100) / 100.f;
            o->StartPosition[2] = o->Position[2];

            o->Scale = (float)(WorldRandom() % 90 + 220) * 0.02f;
            o->TurningForce[0] = Scale + WorldRandom() % 20 / 100.f;
            Vector(0.f, 0.f, 0.f, o->Velocity);
        }
        else if (o->SubType == 20)
        {
            o->LifeTime = 300;
            o->Gravity = (float)(WorldRandom() % 1000);
            o->Alpha = 0.1f;

            o->Position[0] += ((float)(WorldRandom() % 500 - 250));
            o->Position[1] += ((float)(WorldRandom() % 500 - 250));
            o->Position[2] += ((float)(WorldRandom() % 20 - 20));

            o->StartPosition[1] = (WorldRandom() % 100) / 100.f;
            o->StartPosition[2] = o->Position[2];

            o->Scale = (((float)(WorldRandom() % 20 + 180) * 0.01f) * o->Scale);
            o->TurningForce[0] = WorldRandom() % 10 / 100.f;
            Vector(0.f, 0.f, 0.f, o->Velocity);
        }
        else if (o->SubType == 21)
        {
            o->LifeTime = 100;
            o->Gravity = (float)(WorldRandom() % 1000);
            o->Alpha = 0.6f;

            o->Position[0] += ((float)(WorldRandom() % 200 - 100));
            o->Position[1] += ((float)(WorldRandom() % 200 - 100));
            o->Position[2] += ((float)(WorldRandom() % 20 - 20));

            o->StartPosition[1] = (WorldRandom() % 100) / 100.f;
            o->StartPosition[2] = o->Position[2];

            o->Scale = (((float)(WorldRandom() % 20 + 180) * 0.01f) * o->Scale);
            o->TurningForce[0] = WorldRandom() % 10 / 100.f;
            Vector(0.f, 0.f, 0.f, o->Velocity);
        }
        else if (o->SubType == 22)
        {
            o->LifeTime = 80;
            o->Gravity = (float)(WorldRandom() % 1000);
            o->Alpha = 0.1f;

            o->Position[0] += ((float)(WorldRandom() % 20 - 10));
            o->Position[1] += ((float)(WorldRandom() % 20 - 10));
            o->Position[2] += ((float)(WorldRandom() % 20 - 20));

            o->Scale = (((float)(WorldRandom() % 20 + 50) * 0.003f) * o->Scale);
            Vector(0.f, 0.f, 0.f, o->TurningForce);

            float fAngle = o->Angle[2] + WorldRandom() % 360;
            vec3_t vAngle, vDirection;
            Vector(0.f, 0.f, fAngle, vAngle);
            AngleMatrix(vAngle, Matrix);
            Vector(0.f, (WorldRandom() % 10) * 0.1f + 0.2f, 0.f, vDirection);
            VectorRotate(vDirection, Matrix, o->Velocity);
            o->Velocity[2] = 0;
        }
        else if (o->SubType == 23)
        {
            o->LifeTime = 80;
            o->Gravity = (float)(WorldRandom() % 1000);
            o->Alpha = 0.1f;

            o->Position[0] += ((float)(WorldRandom() % 6 - 3));
            o->Position[1] += ((float)(WorldRandom() % 6 - 3));
            o->Position[2] += ((float)(WorldRandom() % 6 - 3));

            o->Scale = 0; //(((float)(WorldRandom()%20+20)*0.001f) * o->Scale);
            Vector(0.f, 0.f, 0.f, o->TurningForce);
            o->TurningForce[0] = (Random.FpsCheck(2, 1.0) ? 1.0f : -1.0f);

            float fAngle = o->Angle[2] + 90 + (WorldRandom() % 40) - 20;
            vec3_t vAngle, vDirection;
            Vector(0.f, 0.f, fAngle, vAngle);
            AngleMatrix(vAngle, Matrix);
            Vector(0.f, (WorldRandom() % 10) * 0.2f + 1.0f, 0.f, vDirection);
            VectorRotate(vDirection, Matrix, o->Velocity);
            o->Velocity[2] = 0;
        }
        else
        {
            o->LifeTime = 30;
            o->Gravity = (float)(WorldRandom() % 1000);

            o->Position[0] += ((float)(WorldRandom() % 500 - 250)); //*Scale)
            o->Position[1] += ((float)(WorldRandom() % 500 - 250)); //*Scale)
            o->Position[2] += ((float)(WorldRandom() % 20 + 20));

            o->StartPosition[1] = (WorldRandom() % 100) / 100.f;
            o->StartPosition[2] = o->Position[2];

            o->Scale = (float)(WorldRandom() % 20 + 180) * 0.01f;
            o->TurningForce[0] = Scale + WorldRandom() % 30 / 100.f;
            Vector(0.f, 0.f, 0.f, o->Velocity);
        }
        break;
    case BITMAP_TORCH_FIRE: {
        o->LifeTime = WorldRandom() % 5 + 60;
        o->Scale = (float)(WorldRandom() % 30 + 30) * 0.02f + Scale / 3.f;

        o->Position[0] += ((float)(WorldRandom() % 4 - 2) * 0.25f);
        o->Position[1] += ((float)(WorldRandom() % 4 - 2) * 0.25f);
        o->Position[2] += ((float)(WorldRandom() % 10 - 5));

        o->Gravity = (float)(WorldRandom() % 2 + 10) * 0.5f;
    }
    break;

    case BITMAP_GHOST_CLOUD1:
    case BITMAP_GHOST_CLOUD2: {
        if (o->SubType == 0)
        {
            o->LifeTime = 500;
            o->Position[0] += ((float)(WorldRandom() % 400 - 200));

            o->Position[1] += ((float)(WorldRandom() % 400 - 200));
            o->Position[2] += ((float)(WorldRandom() % 400 - 200));
            VectorCopy(o->Position, o->StartPosition);
            o->Scale = (float)(WorldRandom() % 15 + 15) / 30.0f + Scale;
            float fTemp = (float)(WorldRandom() % 10 + 5) * 0.08f;
            o->Velocity[0] = fTemp;
            o->Velocity[1] = fTemp;
            o->Velocity[2] = 0.0f;

            //o->Rotation = WorldRandom()%360;

            VectorCopy(o->Light, o->TurningForce);
            o->Light[0] = 0.f;
            o->Light[1] = 0.f;
            o->Light[2] = 0.f;
        }
    }
    break;
    case BITMAP_LIGHT:
        if (0 == o->SubType || 8 == o->SubType)
        {
            o->LifeTime = WorldRandom() % 10 + 10;
            o->Gravity = WorldRandom() % 10 + 10.f;
            o->Scale = (WorldRandom() % 50 + 50.f) / 100.f * Scale;
        }
        else if (o->SubType == 9)
        {
            o->LifeTime = 40 * o->Scale;
        }
        else if (o->SubType == 7)
        {
            o->LifeTime = WorldRandom() % 10 + 30;
            o->Gravity = WorldRandom() % 10 + 10.f;
            o->Scale = (WorldRandom() % 15 + 30.f) / 100.f * Scale;
        }
        else if (6 == o->SubType)
        {
            o->LifeTime = WorldRandom() % 10 + 10;
            o->Gravity = WorldRandom() % 10 + 10.f;
            o->Scale = (WorldRandom() % 50 + 50.f) / 100.f * Scale;
        }
        else if (1 == o->SubType)
        {
            o->LifeTime = 50;
        }
        else if (2 == o->SubType)
        {
            o->LifeTime = WorldRandom() % 10 + 10;
            o->Gravity = WorldRandom() % 10 + 10.f;
            o->Scale = (WorldRandom() % 50 + 50.f) / 100.f * Scale;
        }
        else if (3 == o->SubType)
        {
            o->LifeTime = 10;
        }
        else if (o->SubType == 4)
        {
            o->LifeTime = 10;
            o->Gravity = 0.f;
            o->Rotation = Scale;
            o->Scale = 2.f;
        }
        else if (o->SubType == 5)
        {
            o->LifeTime = 50;
        }
        else if (o->SubType == 6)
        {
            o->LifeTime = 2;
        }
        else if (o->SubType == 10)
        {
            o->LifeTime = 100;
            o->Scale = Scale + (WorldRandom() % 10) * 0.02f;
        }
        else if (o->SubType == 11)
        {
            o->LifeTime = WorldRandom() % 10 + 130;
            o->Gravity = WorldRandom() % 2 + 2.f;
            o->Scale = (WorldRandom() % 20 + 20.f) / 100.f * Scale;
        }
        else if (o->SubType == 12)
        {
            o->LifeTime = WorldRandom() % 10 + 100;
            o->Gravity = WorldRandom() % 2 + 2.f;
            o->Scale = (WorldRandom() % 20 + 20.f) / 100.f * Scale;
        }
        else if (o->SubType == 13)
        {
            o->LifeTime = WorldRandom() % 10 + 100;
            o->Gravity = WorldRandom() % 2 + 2.f;
            o->Scale = (WorldRandom() % 20 + 20.f) / 100.f * Scale;
        }
        else if (o->SubType == 14)
        {
            o->LifeTime = 50;
            o->Scale = Scale + (WorldRandom() % 10) * 0.02f;
        }
        else if (o->SubType == 15)
        {
            o->LifeTime = WorldRandom() % 10 + 100;
            o->Gravity = WorldRandom() % 3 + 2.f;
            o->Scale = (WorldRandom() % 20 + 20.f) / 100.f * Scale;
            VectorCopy(Position, o->StartPosition);
            o->StartPosition[2] = 0;
            VectorCopy(Light, o->TurningForce);
            o->Alpha = 0;
        }
        break;
    case BITMAP_POUNDING_BALL:
        if (o->SubType == 0)
        {
            o->LifeTime = 24;
            Vector(0.f, -(float)(WorldRandom() % 16 + 32) * 0.1f, 0.f, o->Velocity);
            o->Scale = (float)(WorldRandom() % 128 + 128) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);

            Vector(0.5f, 0.5f, 0.5f, o->Light);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 15;
            Vector(0.f, -(float)(WorldRandom() % 16 + 32) * 0.1f, 0.f, o->Velocity);
            o->Scale = (float)(WorldRandom() % 128 + 128) * 0.01f;
            o->Rotation = (float)(WorldRandom() % 360);

            Vector(0.5f, 0.5f, 0.5f, o->Light);
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 20;
            //Vector(0.f,-(float)(WorldRandom()%16+32)*0.1f,0.f,o->Velocity);
            o->Scale = (float)(WorldRandom() % 128 + 80) * 0.009f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = 0.0f;
            Vector(0.f, 0.f, 0.f, o->Velocity);
            Vector(0.5f, 0.5f, 0.5f, o->Light);
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = WorldRandom() % 5 + 47;
            o->Scale = (float)(WorldRandom() % 72 + 72) * 0.01f * Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (float)(WorldRandom() % 24 + 64) * 0.1f;
            o->Alpha = 0;
            VectorCopy(Light, o->TurningForce);
            Vector(0, 0, 0, o->Light);
        }
        break;
    case BITMAP_ADV_SMOKE:
        o->LifeTime = 20 + WorldRandom() % 5;
        o->Rotation = 0.f;
        if (o->SubType == 0)
        {
            o->Scale = 0.5f + (float)(WorldRandom() % 10) * 0.02f;
            o->Velocity[0] = (float)(WorldRandom() % 10 + 5) * 0.4f;
            o->Velocity[1] = (float)(WorldRandom() % 10 - 5) * 0.4f;
            o->Velocity[2] = (float)(WorldRandom() % 10 + 5) * 0.2f;
        }
        else if (o->SubType == 2)
        {
            o->Scale = Scale * 0.5f + (float)(WorldRandom() % 10) * 0.02f;
            o->Velocity[0] = (float)(WorldRandom() % 10 + 5) * 0.4f;
            o->Velocity[1] = (float)(WorldRandom() % 10 - 5) * 0.4f;
            o->Velocity[2] = (float)(WorldRandom() % 10 + 5) * 0.2f;
        }
        else if (o->SubType == 3)
        {
            o->Scale = Scale * 0.5f + (float)(WorldRandom() % 10) * 0.02f;
            o->Alpha = 0.5f;
            o->Velocity[0] = 0;
            o->Velocity[1] = (float)(WorldRandom() % 10 - 5) * 0.4f;
            o->Velocity[2] = 0;
        }
        else
        {
            o->Scale = 1.f + (float)(WorldRandom() % 10) * 0.1f;
            o->Velocity[0] = (float)(WorldRandom() % 10 + 5) * 0.2f;
            o->Velocity[1] = (float)(WorldRandom() % 10 - 5) * 0.2f;
            o->Velocity[2] = (float)(WorldRandom() % 10 + 5) * 0.1f;
        }
        break;
    case BITMAP_ADV_SMOKE + 1:
        o->LifeTime = 25 + WorldRandom() % 5;
        o->Rotation = (float)(WorldRandom() % 360);
        o->Scale = 0.5f;
        if (o->SubType == 1)
        {
            o->Scale *= Scale;
        }
        o->Velocity[0] = (float)(WorldRandom() % 10 + 5) * 0.4f;
        o->Velocity[1] = 0.f;
        o->Velocity[2] = (float)(WorldRandom() % 10 + 5) * 0.2f;

    case BITMAP_TRUE_FIRE:
    case BITMAP_TRUE_BLUE:
        if (o->SubType == 3 || o->SubType == 4)
            o->SocketBinding = boneManager_.BindSocket(
                o->Target, o->SubType == 3 ? L"Monster82_LHand" : L"Monster82_RHand", o->iNumBone);
        if (o->SubType == 7)
        {
            o->Scale = Scale + (float)(WorldRandom() % 30) / 100.f;
            o->LifeTime = 15;
            o->Position[0] += ((float)(WorldRandom() % 10) / 10.f - 0.5f);
            o->Position[1] += ((float)(WorldRandom() % 10) / 10.f - 0.5f);
            o->Position[2] += ((float)(WorldRandom() % 10) / 10.f - 0.5f);
        }
        else if (o->SubType == 5)
            o->LifeTime = 20;
        else if (o->SubType == 6)
            o->LifeTime = 32;
        else if (o->SubType == 8)
        {
            o->LifeTime = 15;
        }
        else if (o->SubType == 9)
        {
            o->LifeTime = 20;
        }
        else
        {
            o->LifeTime = 24;
        }

        if (o->SubType == 8 || o->SubType == 9)
        {
            o->Velocity[0] = (float)(WorldRandom() % 4 - 2) * 0.4f;
            o->Velocity[1] = 0.f;
            o->Velocity[2] = (float)(WorldRandom() % 4 + 2) * 0.2f;
        }
        else
        {
            o->Velocity[0] = (float)(WorldRandom() % 10 - 5) * 0.4f;
            o->Velocity[1] = 0.f;
            o->Velocity[2] = (float)(WorldRandom() % 10 + 5) * 0.2f;
        }

        VectorCopy(o->Position, o->StartPosition);
        break;

    case BITMAP_HOLE:
        o->LifeTime = 30;
        o->Rotation = (float)(WorldRandom() % 360);
        Vector(0.1f, 0.1f, 0.1f, o->Light);
        break;
    case BITMAP_WATERFALL_1:
        o->LifeTime = 30;
        o->Rotation = (float)(WorldRandom() % 360);
        o->Velocity[2] = (float)(-(WorldRandom() % 5 + 7));

        Vector(0.2f, 0.2f, 0.2f, o->Light);

        if (o->SubType == 0)
        {
            o->Scale = 1.6f;
        }
        else if (o->SubType == 1)
        {
            o->Scale = (float)(WorldRandom() % 20 + 80) * 0.01f;
            o->TexType = BITMAP_CLOUD + 2;
        }
        else if (o->SubType == 2)
        {
            o->Scale = 1.6f + Scale;
        }
        break;

    case BITMAP_WATERFALL_5:
        if (o->SubType == 0)
        {
            o->LifeTime = 30;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Scale = 1.6f;
            o->Velocity[2] = (float)(-(WorldRandom() % 5 + 7));

            Vector(0.2f, 0.2f, 0.2f, o->Light);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 20;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Scale = 1.f;
            o->Position[2] += (50.f);
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 20;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Scale = 0.5f;
            o->Position[2] += (50.f);
            o->Velocity[2] = (float)((WorldRandom() % 5 + 10));
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = 20;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Scale = 1.6f;
            o->Velocity[2] = (float)(-(WorldRandom() % 5 + 7));
        }
        else if (o->SubType == 4)
        {
            o->LifeTime = 6;
            o->Gravity = ((WorldRandom() % 100) / 100.f) * 4.f + o->Angle[0] * 1.2f;
            o->Scale = Scale + (float)(WorldRandom() % 6) * 0.20f;
            o->Rotation = (float)(WorldRandom() % 360);

            VectorCopy(o->Position, o->StartPosition);
        }
        else if (o->SubType == 5)
        {
            o->LifeTime = 30;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Scale = 1.6f * Scale;
            o->Velocity[2] = (float)(-(WorldRandom() % 5 + 10));

            Vector(0.2f, 0.2f, 0.2f, o->Light);
        }
        else if (o->SubType == 6)
        {
            o->LifeTime = 30;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Scale = Scale + 0.6f;
            o->Velocity[2] = -(WorldRandom() % 5 + 12);

            Vector(0.2f, 0.2f, 0.2f, o->Light);
        }
        else if (o->SubType == 7)
        {
            o->TexType = Random.FpsCheck(2, 1.0) ? BITMAP_WATERFALL_4 : BITMAP_WATERFALL_5;
            o->LifeTime = 30;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Scale = (float)(WorldRandom() % 50) * 0.05f + Scale;
            o->Velocity[0] = -(WorldRandom() % 2 + 1);
            o->Velocity[1] = -(WorldRandom() % 2 + 3);
            o->Velocity[2] = (WorldRandom() % 3 + 3);
        }
        else if (o->SubType == 8)
        {
            o->LifeTime = 30;
            o->Velocity[2] = WorldRandom() % 3 + 1;
            o->Scale += (WorldRandom() % 5 + 5) * 0.05f;
        }
        else if (o->SubType == 9)
        {
            o->LifeTime = 30;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Scale = 0.6f + Scale;
            o->Velocity[2] = (float)(-(WorldRandom() % 5 + 7));

            Vector(0.2f, 0.2f, 0.2f, o->Light);
        }
        break;

    case BITMAP_PLUS:
        o->LifeTime = 20;
        o->Scale = Scale;
        o->Position[0] += (WorldRandom() % 30 - 15.f);
        o->Position[1] += (WorldRandom() % 30 - 15.f);
        o->Position[2] += (240.f);
        break;

    case BITMAP_WATERFALL_2:
        o->LifeTime = 30;
        o->Rotation = (float)(WorldRandom() % 360);

        if (o->SubType == 2)
        {
            o->LifeTime = 40;
            o->Scale = (WorldRandom() % 6 + 6) * 0.1f + Scale;
        }
        else if (o->SubType == 6)
        {
            o->LifeTime = WorldRandom() % 50 + 20;
            o->Gravity = (float)(WorldRandom() % 20 + 10);
            o->Scale *= 0.2f;
            VectorCopy(o->Position, o->StartPosition);
        }
        else
            o->Scale = (WorldRandom() % 6 + 6) * 0.1f;

        o->Velocity[2] = (float)(-(WorldRandom() % 3 + 3));

        if (SceneFlag == CHARACTER_SCENE)
        {
            Vector(0.25f, 0.25f, 0.25f, o->Light);
        }
        else
        {
            Vector(0.4f, 0.4f, 0.4f, o->Light);
        }

        o->Position[0] += (WorldRandom() % 20 - 10.f);
        o->Position[1] += (WorldRandom() % 20 - 10.f);
        o->Position[2] += (WorldRandom() % 40 - 20.f);

        if (o->SubType == 1)
        {
            Vector(0.0f, 0.4f, 0.4f, o->Light);
            o->LifeTime = 50;
            o->Velocity[2] = (float)(-(WorldRandom() % 3 + 3));
        }
        if (o->SubType == 3)
        {
            o->TexType = BITMAP_LIGHT + 2;
            VectorCopy(Light, o->Light);
            o->LifeTime = 12;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Scale = (WorldRandom() % 3 + 3) * 0.18f;
            o->Velocity[2] = 2.0f;

            o->Position[0] += (WorldRandom() % 20 - 10.f);
            o->Position[1] += (WorldRandom() % 20 - 10.f);
            o->Position[2] += (WorldRandom() % 40 - 20.f);
        }
        if (o->SubType == 4)
        {
            o->LifeTime = 70;
            o->Scale = (WorldRandom() % 6 + 6) * 0.1f + Scale;
        }
        if (o->SubType == 5)
        {
            o->LifeTime = 40;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = (WorldRandom() % 2 + 2);
            o->Scale = (WorldRandom() % 6 + 6) * 0.1f + Scale;
            o->Velocity[0] = -(WorldRandom() % 2 + 2);
            o->Velocity[1] = -(WorldRandom() % 2 + 2);
            o->Velocity[2] = (WorldRandom() % 2 + 1);
            o->Position[0] += (WorldRandom() % 60 - 30.f);
            o->Position[1] += (WorldRandom() % 60 - 30.f);
            o->Position[2] += (WorldRandom() % 10);
            break;
        }
        if (o->SubType == 11)
        {
            o->LifeTime = 30;
            o->Velocity[2] = WorldRandom() % 5 + 5;
            o->Scale = (WorldRandom() % 10 + 10) * 0.05f * Scale;
        }
        break;
    case BITMAP_WATERFALL_3:
    case BITMAP_WATERFALL_4:
        if (o->SubType == 0)
        {
            o->LifeTime = 20;
            o->Velocity[2] = (float)(WorldRandom() % 5 + 2);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 10;
            o->Velocity[2] = (float)(WorldRandom() % 2 + 2);
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 6;
            o->Gravity = ((WorldRandom() % 100) / 100.f) * 4.f + o->Angle[0] * 1.2f;
            o->Scale = Scale + (float)(WorldRandom() % 6) * 0.10f;
            o->Rotation = (float)(WorldRandom() % 360);

            VectorCopy(o->Position, o->StartPosition);
            break;
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = 60;
            o->Velocity[2] = -1.0f;
            o->Scale *= (WorldRandom() % 10 + 15) * 0.02f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Position[0] += (WorldRandom() % 40 - 20.f);
            o->Position[2] += (WorldRandom() % 25 - 10.f);
            break;
        }
        else if (o->SubType == 12)
        {
            o->Rotation = (float)(WorldRandom() % 360);
            o->LifeTime = (WorldRandom() % 2 - 1) + 5;
            float fIntervalScale = Scale * 0.3f;
            o->Scale += ((float((WorldRandom() % 20) - 10) / 2.0f) * ((fIntervalScale / 2.f)));
            o->Position[0] += ((float)(WorldRandom() % 20) - 10.f);
            o->Position[1] += ((float)(WorldRandom() % 20) - 10.f);
            o->Position[2] += ((float)(WorldRandom() % 20) - 10.f);

            break;
        }
#ifdef ASG_ADD_MAP_KARUTAN
        else if (o->SubType == 16)
        {
            o->LifeTime = 30;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Scale = 0.6f + Scale;
            o->Velocity[2] = (float)(WorldRandom() % 3 + 5);
            break;
        }
#endif // ASG_ADD_KARURAN

        o->Rotation = (float)(WorldRandom() % 360);
        o->Scale = (WorldRandom() % 10 + 10) * 0.02f;

        if (o->SubType == 4)
        {
            o->LifeTime = 20;
            o->Velocity[2] = WorldRandom() % 5 + 2;
            o->Scale = (WorldRandom() % 10 + 10) * 0.02f + Scale;
        }
        else if (o->SubType == 5 || o->SubType == 6)
        {
            o->LifeTime = 20;
            o->Gravity = ((WorldRandom() % 100) / 100.f) * 2.f;
            o->Scale = (WorldRandom() % 10 + 10) * 0.02f + Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Position[0] += (WorldRandom() % 40 - 20.f);
            o->Position[1] += (WorldRandom() % 40 - 20.f);
            o->Position[2] -= (50.f);
            break;
        }
        else if (o->SubType == 7)
        {
            o->LifeTime = 60;
            o->Velocity[2] = (WorldRandom() % 4) * 0.1f + 0.8f;
            o->Scale *= (WorldRandom() % 10 + 15) * 0.02f;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Position[0] += (WorldRandom() % 18 - 9.f);
            o->Position[2] += (WorldRandom() % 18 - 9.f);
            break;
        }
        else if (o->SubType == 8)
        {
            o->LifeTime = 30;
            o->Velocity[2] = WorldRandom() % 5 + 5;
            o->Scale = (WorldRandom() % 10 + 10) * 0.05f;
        }
        else if (o->SubType == 9)
        {
            o->LifeTime = 30;
            o->Velocity[2] = WorldRandom() % 5 + 2;
            o->Scale = (WorldRandom() % 5 + 5) * 0.05f + Scale;
        }
        else if (o->SubType == 10)
        {
            o->LifeTime = 20;
            o->Gravity = ((WorldRandom() % 200) / 100.f) + 2.f;
            o->Scale = (WorldRandom() % 10 + 10) * 0.02f + Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Velocity[0] = -(WorldRandom() % 2 + 1);
            o->Velocity[1] = -(WorldRandom() % 2 + 2);
        }
        else if (o->SubType == 11)
        {
            o->LifeTime = 20;
            o->Scale = Scale + (WorldRandom() % 10 - 5) * 0.05f;
            o->Rotation = (float)(WorldRandom() % 360);

            vec3_t vAngle, vPos, vPos2;
            Vector(0.f, 5.f, 0.f, vPos);
            float fAngle = o->Angle[2] + (float)(WorldRandom() % 90 - 45) + 150;
            Vector(0.f, 0.f, fAngle, vAngle);
            AngleMatrix(vAngle, Matrix);
            VectorRotate(vPos, Matrix, vPos2);
            VectorCopy(vPos2, o->Velocity);

            break;
        }
        else if (o->SubType == 13)
        {
            o->LifeTime = 30;
            o->Scale = Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Gravity = 2.5f + (float)(WorldRandom() % 10) * 0.1f;
            break;
        }
        else if (o->SubType == 14)
        {
            o->LifeTime = 30;
            o->Velocity[2] = WorldRandom() % 5 + 5;
            o->Scale = (WorldRandom() % 10 + 10) * 0.05f * Scale;
        }
        else if (o->SubType == 15)
        {
            o->LifeTime = 20;
            o->Gravity = ((WorldRandom() % 100) / 100.f) * 2.f;
            o->Scale = (WorldRandom() % 10 + 10) * 0.02f + Scale;
            o->Rotation = (float)(WorldRandom() % 360);
            o->Position[0] += (WorldRandom() % 40 - 20.f);
            o->Position[1] += (WorldRandom() % 40 - 20.f);
            o->Position[2] -= (50.f);
            break;
        }
        o->Position[0] += (WorldRandom() % 40 - 20.f);
        o->Position[1] += (WorldRandom() % 40 - 20.f);
        o->Position[2] += (WorldRandom() % 20 - 10.f);
        break;
    case BITMAP_SHOCK_WAVE:
        if (o->SubType == 3)
        {
            o->LifeTime = 7;
        }
        else if (o->SubType == 0)
        {
            o->LifeTime = 7;
            o->Scale = Scale;
            VectorCopy(Light, o->Light);
        }
        if (o->SubType == 4)
        {
            o->Alpha = 1.0f;
            o->LifeTime = 7;
            o->Scale = Scale;
            o->Gravity = 6.f;
            VectorCopy(Light, o->Light);
        }
        break;
    case BITMAP_GM_AURORA:
        o->LifeTime = 20;
        break;
    case BITMAP_CURSEDTEMPLE_EFFECT_MASKER: {
        o->LifeTime = 30;
    }
    break;
    case BITMAP_RAKLION_CLOUDS: {
        o->Alpha = 1.f;
        o->LifeTime = 32;
        o->Rotation = (float)(WorldRandom() % 360);
    }
    break;
    case BITMAP_CHROME2: {
        o->LifeTime = WorldRandom() % 5 + 5;
        o->Rotation = WorldRandom() % 360;
        // 					o->Gravity  = WorldRandom()%3+2.f;
        // 					o->Scale    = (WorldRandom()%20+20.f)/100.f*Scale;
        VectorCopy(Position, o->StartPosition);
        o->Scale *= 1.0f + (WorldRandom() % 10) * 0.03f;
        // 					o->StartPosition[2] = 0;
        // 					VectorCopy(Light, o->TurningForce);
        // 					o->Alpha = 0;
    }
    break;
    case BITMAP_AG_ADDITION_EFFECT: {
        float _Scale;
        if (o->SubType == 0)
        {
            o->LifeTime = 33 + WorldRandom() % 5;
            o->Rotation = (float)(WorldRandom() % 90) + 270;
            _Scale = (WorldRandom() % 20 + 20.0f) / 50.0f * 0.5f;
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 27 + WorldRandom() % 5;
            o->Rotation = (float)(WorldRandom() % 90);
            _Scale = (WorldRandom() % 20 + 20.0f) / 50.0f * 1.5f;
        }
        else if (o->SubType == 2)
        {
            o->LifeTime = 38 + WorldRandom() % 5;
            o->Rotation = (float)(WorldRandom() % 90) + 135;
            _Scale = (WorldRandom() % 20 + 20.0f) / 50.0f * 1.0f;
        }
        o->Scale = _Scale;
        o->Gravity = (float)(WorldRandom() % 16 + 12) * 0.1f;
        o->Alpha = 0;
        Vector(1.0f, 0.0f, 0.6f, Light);
        VectorCopy(Light, o->TurningForce);
        Vector(0, 0, 0, o->Light);
    }
    break;
    case BITMAP_SBUMB: {
        o->LifeTime = 4;
        o->Scale = Scale;
    }
    break;
    case BITMAP_DAMAGE1: {
        o->LifeTime = 5;
        o->Scale = Scale;
    }
    break;
    case BITMAP_SWORD_EFFECT_MONO: {
        o->LifeTime = 20;
        o->Scale = Scale;
    }
    break;
    case BITMAP_DAMAGE2: {
        o->LifeTime = 15;
        o->Scale = Scale * 0.9f + (WorldRandom() % 2 + 2) * 0.1f;
        o->Position[2] += (80.0f + WorldRandom() % 20);
    }
    break;
    }
    auto &textures = sessionKeeper_.TextureNamespace();
    o->RenderTexture = SessionTextureNamespace::IsApplicationSharedLogicalIndex(o->TexType)
                           ? nullptr
                           : &textures.BindProperties(o->TexType);
    o->TypeTexture = o->Type == o->TexType ? o->RenderTexture
                     : SessionTextureNamespace::IsApplicationSharedLogicalIndex(o->Type)
                         ? nullptr
                         : &textures.BindProperties(o->Type);
    o->AdditionalTexture =
        o->Type == BITMAP_SHINY + 6 ? &textures.BindProperties(BITMAP_LIGHT) : nullptr;
    return i;
}

void SessionGameplayUnit::AdvanceLightningParticle(PARTICLE *o)
{
    vec3_t Light;
    float Luminosity;
    const float lifeTime = (std::max)(0.f, o->LifeTime);
    o->Rotation = (float)((int)WorldTime % 1000) * 0.001f;
    Luminosity = (float)(lifeTime) / 10.f;
    Vector(Luminosity * 0.5f, Luminosity * 1.f, Luminosity * 0.8f, Light);
    AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
    if (o->SubType == 2)
    {
        o->Scale += FPS_ANIMATION_FACTOR * 0.1f;
        Vector(Luminosity * 0.5f, Luminosity * 1.f, Luminosity * 0.8f, o->Light);
        VectorCopy(o->Target->Position, o->Position);
        o->Position[2] += 80.f;
    }
}

void SessionGameplayUnit::MoveParticles()
{
    if (!g_pOption->GetRenderAllEffects())
        return;
    vec3_t windTravel{};
    AdvanceParticleWind(sessionKeeper_.PhysicsStorage(), Random, FPS_ANIMATION_FACTOR, windTravel);
    for (auto cursor = Particles.begin(), end = Particles.end(); cursor != end; ++cursor)
        MoveParticleEntry(&*cursor, cursor.Index());
}

void SessionGameplayUnit::MoveParticleEntry(PARTICLE *o, int i)
{
    EffectBirthStep birthStep(FPS_ANIMATION_FACTOR, o->BirthTiming);
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    SessionRandom::PresentationScope randomOrigin(sessionKeeper_.RandomForConstruction(),
                                                  o->PresentationRandom);
    const auto birthSerial = o->BirthTiming.serial;
    o->LifeTime -= FPS_ANIMATION_FACTOR;
    const bool expired = o->LifeTime <= (o->MotionRandomSeed == 0 ? 0.f : 0.0001f);
    // Keep the parent slot occupied while its final update creates descendants.

    vec3_t Position;
    vec3_t TargetPosition;
    vec3_t Light;
    float Luminosity = 0.0f;
    float Height;
    float Matrix[3][4];

    if (o->bEnableMove && !(o->Type == BITMAP_FLARE_BLUE && o->SubType == 0) &&
        !OwnsIntegratedParticleMotion(*o))
    {
        MovePosition(o->Position, o->Angle, o->Velocity);
    }

    if (o->Type == BITMAP_WATERFALL_2 && o->SubType == 6)
        AdvanceRisingWaterfall(*o, Random, FPS_ANIMATION_FACTOR);
    else if (o->Type == BITMAP_WATERFALL_2 && o->SubType != 5)
        AdvanceWaterfallMist(*o, Random, FPS_ANIMATION_FACTOR);
    else if (OwnsWaterfallMotion(*o))
        AdvanceWaterfallMotion(*o, FPS_ANIMATION_FACTOR);

    if (o->Type == BITMAP_SHINY && o->SubType == 9)
        EmitFallingShinyChildren(*o);
    if (IsFallingShiny(*o))
        AdvanceFallingShiny(*o, Random, FPS_ANIMATION_FACTOR);

    if (IsFireParticle(*o))
        AdvanceDampedVelocity(*o, FireVelocityDamping(*o), FPS_ANIMATION_FACTOR);

    if (o->Type == BITMAP_SMOKE && o->SubType != 8 && o->SubType != 68)
        AdvanceSmokeVelocity(*o, FPS_ANIMATION_FACTOR);

    switch (o->Type)
    {
    case BITMAP_EFFECT:
        if (o->LifeTime >= 10)
        {
            if (o->SubType == 2)
            {
                o->Light[0] *= powf(1.0f / (1.03f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.0f / (1.03f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.0f / (1.03f), FPS_ANIMATION_FACTOR);
            }
            else
            {
                o->Light[0] *= powf(1.16f, FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.16f, FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.16f, FPS_ANIMATION_FACTOR);
            }
        }
        else
        {
            if (o->SubType == 2)
            {
                o->Light[0] *= powf(1.0f / (1.03f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.0f / (1.03f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.0f / (1.03f), FPS_ANIMATION_FACTOR);
            }
            else
            {
                o->Light[0] *= powf(1.0f / (1.16f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.0f / (1.16f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.0f / (1.16f), FPS_ANIMATION_FACTOR);
            }
        }
        o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
        break;
    case BITMAP_FLOWER01:
    case BITMAP_FLOWER01 + 1:
    case BITMAP_FLOWER01 + 2:
        AdvanceFlowerParticle(*o, Random, FPS_ANIMATION_FACTOR, *sessionKeeper_.WorldUnit());
        break;
    case BITMAP_FLARE_BLUE:
        AdvanceBlueFlare(*o, FPS_ANIMATION_FACTOR);
        break;
    case BITMAP_FLARE + 1:
        if (o->SubType == 0)
        {
            float count = (o->Velocity[0] + o->LifeTime) * 0.05f;
            o->Position[0] = o->StartPosition[0] + sinf(count) * (105.f + o->Scale * -250);
            o->Position[1] = o->StartPosition[1] - cosf(count) * (105.f + o->Scale * -250);
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;

            o->Scale -= 0.0008f * FPS_ANIMATION_FACTOR;
        }
        break;
    case BITMAP_BLUE_BLUR:
        switch (o->SubType)
        {
        case 1:
        case 0:
            Luminosity = (float)(o->LifeTime) / 20.f;
            Vector(Luminosity * 1.f, Luminosity * 1.f, Luminosity * 1.f, o->Light);
            if (o->SubType == 1)
            {
                o->Scale += FPS_ANIMATION_FACTOR * 0.19f;
                o->Position[2] += (5.00f) * FPS_ANIMATION_FACTOR;
            }
            else
                o->Scale += FPS_ANIMATION_FACTOR * 0.05f;

            break;
        }
        break;
    case BITMAP_LIGHT + 2:
        if (o->SubType == 3)
        {
            o->Gravity += 0.2f * FPS_ANIMATION_FACTOR;

            //o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Scale -= 0.03f * FPS_ANIMATION_FACTOR;

            if (o->Scale < 0 || !o->Target->Live)
                Particles.Retire(i);
            VectorSubtract(o->Position, o->StartPosition, o->Position);
            VectorCopy(o->Target->Position, o->StartPosition);
            VectorAdd(o->Position, o->StartPosition, o->Position);
        }
        else if (o->SubType == 5)
        {
            AdvanceRotatingFlare(*o, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.04f;

            Vector(o->Alpha * 1.0f, o->Alpha * 0.0f, o->Alpha * 0.6f, o->Light);

            if (o->Alpha < 0 || !o->Target->Live)
                Particles.Retire(i);
            VectorSubtract(o->Position, o->StartPosition, o->Position);
            VectorCopy(o->Target->Position, o->StartPosition);
            VectorAdd(o->Position, o->StartPosition, o->Position);

            o->Rotation += (1.0f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 6)
        {
            AdvanceRotatingFlare(*o, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.04f;

            Vector(o->Alpha * 1.0f, o->Alpha * 1.0f, o->Alpha * 1.0f, o->Light);

            if (o->Alpha < 0)
            {
                Particles.Retire(i);
            }

            o->Rotation += (2.0f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 7)
        {
            AdvanceRotatingFlare(*o, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.04f;
            if (o->TexType != BITMAP_LIGHT)
            {
                o->TexType = BITMAP_LIGHT;
                o->RenderTexture = &sessionKeeper_.TextureNamespace().BindProperties(BITMAP_LIGHT);
            }

            if (o->Alpha < 0)
            {
                Particles.Retire(i);
            }

            o->Rotation += (2.0f) * FPS_ANIMATION_FACTOR;

            Luminosity = (o->LifeTime + (FPS_ANIMATION_FACTOR - 1.f) * 0.5f) / 8.f * 0.02f;
        }
        else
        {
            Luminosity = (o->LifeTime + (FPS_ANIMATION_FACTOR - 1.f) * 0.5f) / 8.f * 0.02f;
            o->Light[0] += (Luminosity)*FPS_ANIMATION_FACTOR;
            o->Light[1] += (Luminosity)*FPS_ANIMATION_FACTOR;
            o->Light[2] += (Luminosity)*FPS_ANIMATION_FACTOR;
            o->Position[2] += AdvanceGravityAfter(*o, 0.2f, FPS_ANIMATION_FACTOR);
            o->Scale -= 0.1f * FPS_ANIMATION_FACTOR;
        }
        break;
    case BITMAP_GM_AURORA: {
        Luminosity = (o->LifeTime + (FPS_ANIMATION_FACTOR - 1.f) * 0.5f) / 8.f * 0.04f;
        o->Light[0] -= (Luminosity)*FPS_ANIMATION_FACTOR;
        o->Light[1] -= (Luminosity)*FPS_ANIMATION_FACTOR;
        o->Light[2] -= (Luminosity)*FPS_ANIMATION_FACTOR;
    }
    break;
    case BITMAP_MAGIC + 1: {
        o->Light[0] *= powf(0.9f, FPS_ANIMATION_FACTOR);
        o->Light[1] *= powf(0.9f, FPS_ANIMATION_FACTOR);
        o->Light[2] *= powf(0.9f, FPS_ANIMATION_FACTOR);
        o->Scale += FPS_ANIMATION_FACTOR * 0.1f;
    }
    break;
    case BITMAP_BUBBLE:
        if (AdvanceBubble(*o, Random, FPS_ANIMATION_FACTOR))
        {
            o->LifeTime = 0.f;
            Particles.Retire(i);
        }
        break;

    case BITMAP_EXPLOTION_MONO:
    case BITMAP_EXPLOTION:
        o->Frame = (20 - o->LifeTime) / 2;
        if (o->SubType == 2)
            AdvanceExplosionRings(*o);
        if (o->SubType != 1)
        {
            Luminosity = (float)(o->LifeTime) / 20.f;
            Vector(Luminosity * 0.5f, Luminosity * 0.3f, Luminosity * 0.1f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 4, PrimaryTerrainLight);
        }
        break;
    case BITMAP_SUMMON_SAHAMUTT_EXPLOSION:
        o->Frame = (16 - o->LifeTime);
        break;
    case BITMAP_SPOT_WATER:
        o->Frame = (32 - o->LifeTime) / 4;
        break;
    case BITMAP_EXPLOTION + 1:
        o->Frame = (12 - o->LifeTime) / 3;
        break;
    case BITMAP_LIGHTNING + 1:
        AdvanceLightningParticle(o);
        break;
    case BITMAP_LIGHTNING:
        o->Frame = WorldRandom() % 4;
        Luminosity = (float)(o->LifeTime) / 5.f;
        Vector(Luminosity * 1.f, Luminosity * 1.f, Luminosity * 1.f, o->Light);
        Vector(-Luminosity * 0.6f, -Luminosity * 0.6f, -Luminosity * 0.6f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 6, PrimaryTerrainLight);
        Vector(Luminosity * 0.2f, Luminosity * 0.4f, Luminosity * 1.f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 4, PrimaryTerrainLight);
        break;
    case BITMAP_CHROME_ENERGY2:
        o->Gravity = 0.0f;
        Luminosity = (float)(o->LifeTime) / 24.f;
        o->Scale -= 0.04f * FPS_ANIMATION_FACTOR;
        o->Rotation += (5) * FPS_ANIMATION_FACTOR;
        o->Frame = (23 - o->LifeTime) / 6;
        break;
    case BITMAP_FIRE_CURSEDLICH:
    case BITMAP_FIRE_HIK2_MONO:
        if (o->SubType == 0)
        {
            o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 20, 10, 0.001f);
            Luminosity -= 0.05f * FPS_ANIMATION_FACTOR;
            o->Position[2] += o->Gravity * 10.f * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 1)
        {
            o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 5, 15, 0.0016f);
            o->Position[2] += o->Gravity * 10.f * FPS_ANIMATION_FACTOR;

            if (o->Scale < 0 || !o->Target->Live)
                Particles.Retire(i);
            VectorSubtract(o->Position, o->StartPosition, o->Position);
            VectorCopy(o->Target->Position, o->StartPosition);
            VectorAdd(o->Position, o->StartPosition, o->Position);
        }
        else if (o->SubType == 2)
        {
            o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 20, 10, 0.004f);
            Luminosity -= 0.05f * FPS_ANIMATION_FACTOR;
            o->Position[2] += o->Gravity * 25.f * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 3)
        {
            o->Scale -= 0.03f * FPS_ANIMATION_FACTOR; //(WorldRandom()%20+10)*0.001f;
            Luminosity -= 0.05f * FPS_ANIMATION_FACTOR;
            o->Position[2] += o->Gravity * 10.f * FPS_ANIMATION_FACTOR;

            if (o->Scale < 0)
                Particles.Retire(i);

            if (o->Target->Live)
            {
                VectorSubtract(o->Position, o->StartPosition, o->Position);
                VectorCopy(o->Target->Position, o->StartPosition);
                VectorAdd(o->Position, o->StartPosition, o->Position);
            }
        }
        else if (o->SubType == 4 || o->SubType == 9)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 10.f, 0.2f, 2, 2, 0.1f);

            if (o->MotionRandomSeed == 0 && o->LifeTime < 10.f && o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0)
            {
                o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 3, 6, 0.01f);
                if (o->MotionRandomSeed != 0)
                    o->Scale = (std::max)(0.f, o->Scale);
            }
            else if (o->MotionRandomSeed == 0)
            {
                Particles.Retire(i);
            }
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (3.0f) * FPS_ANIMATION_FACTOR;
            if (o->SubType == 9)
            {
                o->Rotation = 0.0f;
                o->Position[2] += o->Gravity * 1.2f * FPS_ANIMATION_FACTOR;
            }
        }
        else if (o->SubType == 5)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 15.f, 0.2f, 2, 2, 0.1f);

            if (o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0)
            {
                o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 2, 5, 0.01f);
            }
            else
            {
                Particles.Retire(i);
            }
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (3.0f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 6)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 10.f, 0.2f, 2, 2, 0.1f);

            if (o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0)
            {
                o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 3, 6, 0.01f);
            }
            else
            {
                Particles.Retire(i);
            }
            o->Position[0] += (o->Velocity[0]) * FPS_ANIMATION_FACTOR;
            o->Position[1] += (o->Velocity[1]) * FPS_ANIMATION_FACTOR;
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (3.0f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 7)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 10.f, 0.2f, 2, 2, 0.1f);

            if (o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0)
            {
                o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 3, 3, 0.01f);
            }
            else
            {
                Particles.Retire(i);
            }
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += 3.0f * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 8)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 5.f, 0.1f, 2, 2, 0.1f);

            if (o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);

            o->Scale += ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 3, 8, 0.01f);
            if (o->LifeTime < 3)
            {
                o->Scale += (10 - o->LifeTime) * 0.02f * FPS_ANIMATION_FACTOR;
            }

            AdvanceAttachedFlame(*o, FPS_ANIMATION_FACTOR);

            o->Rotation += (5.0f) * FPS_ANIMATION_FACTOR;
        }
        break;
    case BITMAP_LEAF_TOTEMGOLEM:
        AdvanceTotemLeaf(*o, FPS_ANIMATION_FACTOR, *sessionKeeper_.WorldUnit());
        break;
    case BITMAP_FIRE:
    case BITMAP_FIRE + 2:
    case BITMAP_FIRE + 3:
        switch (o->SubType)
        {
        case 0: {
            const float gravityTravel = AdvanceGravityAfter(*o, 0.004f, FPS_ANIMATION_FACTOR);
            Luminosity = (float)(o->LifeTime) / 24.f;
            o->Scale -= 0.04f * FPS_ANIMATION_FACTOR;
            o->Frame = (23 - o->LifeTime) / 6;
            o->Position[2] += gravityTravel * 10.f;
        }
        break;
        case 17:
        case 5:
        case 6: {
            const float gravityTravel = AdvanceGravityAfter(*o, 0.004f, FPS_ANIMATION_FACTOR);
            Luminosity = (float)(o->LifeTime) / 24.f;
            o->Scale -= 0.04f * FPS_ANIMATION_FACTOR;
            o->Rotation += (5) * FPS_ANIMATION_FACTOR;
            o->Frame = (23 - o->LifeTime) / 6;
            o->Position[2] += gravityTravel * 10.f;
        }
        break;
        case 7: {
            const float gravityTravel = AdvanceGravityAfter(*o, 0.004f, FPS_ANIMATION_FACTOR);
            Luminosity = (float)(o->LifeTime) / 24.f;
            o->Frame = (15 - o->LifeTime) / 6;
            o->Scale -= 0.04f * FPS_ANIMATION_FACTOR;
            o->Frame = (23 - o->LifeTime) / 6;
            o->Position[2] += gravityTravel * 10.f;
        }
        break;
        case 8: {
            const float gravityTravel = AdvanceGravityAfter(*o, 0.004f, FPS_ANIMATION_FACTOR);
            Luminosity = (float)(o->LifeTime) / 24.f;
            o->Scale *= powf(0.95f, FPS_ANIMATION_FACTOR);
            o->Rotation += (5) * FPS_ANIMATION_FACTOR;
            o->Frame = (23 - o->LifeTime) / 6;
            o->Position[2] += gravityTravel * 10.f;
        }
        break;
        case 9: {
            AdvanceAttachedFire(*o, Random, FPS_ANIMATION_FACTOR);
            Luminosity = o->LifeTime / 24.f;
            o->Frame = (23 - o->LifeTime) / 6;
        }
        break;
        case 11: {
            const float gravityTravel = AdvanceGravityAfter(*o, 0.004f, FPS_ANIMATION_FACTOR);
            Luminosity = (float)(o->LifeTime) / 24.f;
            if (o->LifeTime > 12)
            {
                o->Frame = (24 - o->LifeTime) / 3;
            }
            o->Scale += FPS_ANIMATION_FACTOR * 0.04f;
            o->Frame = (23 - o->LifeTime) / 6;
            o->Position[2] += gravityTravel * 10.f;
        }
        break;
        case 10: {
            const float gravityTravel = AdvanceGravityAfter(*o, 0.004f, FPS_ANIMATION_FACTOR);
            Luminosity = (float)(o->LifeTime) / 24.f;
            o->Scale *= powf(0.95f, FPS_ANIMATION_FACTOR);
            o->Frame = (23 - o->LifeTime) / 6;
            o->Position[2] += gravityTravel * 10.f;
        }
        break;
        case 12: {
            const float gravityTravel = AdvanceGravityAfter(*o, 0.004f, FPS_ANIMATION_FACTOR);
            Luminosity = (float)(o->LifeTime) / 24.f;
            AdvanceParticleRotation(*o, Random, 10, 10, FPS_ANIMATION_FACTOR);
            o->Scale -= 0.04f * FPS_ANIMATION_FACTOR;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
            o->Frame = (23 - o->LifeTime) / 6;
            o->Position[2] += gravityTravel * 10.f;
        }
        break;
        case 13: {
            const float gravityTravel = AdvanceGravityAfter(*o, 0.004f, FPS_ANIMATION_FACTOR);
            Luminosity = (float)(o->LifeTime) / 24.f;
            AdvanceParticleRotation(*o, Random, 10, 10, FPS_ANIMATION_FACTOR);
            o->Scale -= 0.04f * FPS_ANIMATION_FACTOR;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
            o->Frame = (23 - o->LifeTime) / 6;
            o->Position[2] += gravityTravel * 10.f;
        }
        break;
        case 14: {
            const float gravityTravel = AdvanceGravityAfter(*o, 0.004f, FPS_ANIMATION_FACTOR);
            Luminosity = (float)(o->LifeTime) / 24.f;
            o->Rotation += gravityTravel;
            o->Scale += FPS_ANIMATION_FACTOR * 0.03f;
            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            if (o->Light[2] <= 0.05f || o->Scale <= 0.0f)
                Particles.Retire(i);
            o->Frame = (23 - o->LifeTime) / 6;
            o->Position[2] += gravityTravel * 10.f;
        }
        break;
        case 15: {
            const float gravityTravel = AdvanceGravityAfter(*o, 0.004f, FPS_ANIMATION_FACTOR);
            Luminosity = (float)(o->LifeTime) / 24.f;
            o->Scale -= 0.04f * FPS_ANIMATION_FACTOR;
            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            if (o->Light[2] <= 0.05f || o->Scale <= 0.0f)
                Particles.Retire(i);
            o->Frame = (23 - o->LifeTime) / 6;
            o->Position[2] += gravityTravel * 10.f;
        }
        break;
        case 16: {
            o->Frame = (3 - o->LifeTime);
            o->Light[0] *= powf(1.0f / (1.7f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.7f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.7f), FPS_ANIMATION_FACTOR);
            o->Scale *= powf(1.1f, FPS_ANIMATION_FACTOR);
        }
        break;
        case 18: {
            const float gravityTravel = AdvanceGravityAfter(*o, 0.004f, FPS_ANIMATION_FACTOR);
            Luminosity = (float)(o->LifeTime) / 24.f;
            o->Scale += gravityTravel;
            o->Frame = (23 - o->LifeTime) / 6;
            o->Position[2] += gravityTravel * 10.f;
            VectorScale(o->Light, std::pow(0.95f, FPS_ANIMATION_FACTOR), o->Light);
        }
        break;
        default: {
            const float gravityTravel = AdvanceGravityAfter(*o, 0.004f, FPS_ANIMATION_FACTOR);
            Luminosity = (float)(o->LifeTime) / 24.f;
            o->Scale += gravityTravel;
            o->Frame = (23 - o->LifeTime) / 6;
            o->Position[2] += gravityTravel * 10.f;
        }
        break;
        }
        break;
    case BITMAP_FIRE + 1:
        if (o->SubType == 1)
        {
            //					o->Gravity += 0.02f;
            //					o->Scale += o->Gravity;
            o->Position[2] += o->Gravity * 20.f * FPS_ANIMATION_FACTOR;
            Luminosity = (float)(o->LifeTime) * 0.2f;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
        }
        else if (o->SubType == 2)
        {
            //					o->Gravity += 0.02f;
            //					o->Scale += o->Gravity;
            //     				VectorScale(o->Velocity,1.05f,o->Velocity);
            //					o->Position[2] += (o->Gravity*20.f) * FPS_ANIMATION_FACTOR;
            Luminosity = (float)(o->LifeTime) * 0.2f;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
        }
        else if (o->SubType == 3)
        {
            const float gravityTravel = AdvanceGravityAfter(*o, 0.02f, FPS_ANIMATION_FACTOR);
            o->Scale += gravityTravel;
            o->Position[2] += gravityTravel * 20.f;
            Luminosity = (float)(o->LifeTime) * 0.2f;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
        }
        else if (o->SubType == 4)
        {
            //     				Luminosity = (float)(o->LifeTime)/5.f;
            //					Vector(o->TurningForce[0]*Luminosity,o->TurningForce[1]*Luminosity,o->TurningForce[2]*Luminosity,o->Light);
            float count = (o->Velocity[0] + o->LifeTime) * 0.1f;
            o->Position[0] = o->StartPosition[0] + sinf(count) * 120.f;
            o->Position[1] = o->StartPosition[1] - cosf(count) * 120.f;
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Scale -= 0.002f * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 5)
        {
            //					Luminosity = (float)(o->LifeTime)*0.2f;
            //					Vector(o->TurningForce[0]*Luminosity,o->TurningForce[1]*Luminosity,o->TurningForce[2]*Luminosity,o->Light);
            o->Position[0] += ((float)(cos(o->Angle[2]) * 20.0f)) * FPS_ANIMATION_FACTOR;
            o->Position[1] += ((float)(sin(o->Angle[2]) * 20.0f)) * FPS_ANIMATION_FACTOR;
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (1) * FPS_ANIMATION_FACTOR;
            //o->Scale += FPS_ANIMATION_FACTOR * 0.003f;
        }
        else if (o->SubType == 6)
        {
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (4) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 7)
        {
            Luminosity = (float)(o->LifeTime) * 0.2f;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
        }
        else if (o->SubType == 8 || o->SubType == 9)
        {
            o->Scale += FPS_ANIMATION_FACTOR * 0.13f;
            o->Rotation += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Light[0] *= powf(1.0f / (1.13f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.13f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.13f), FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 0)
        {
            const float gravityTravel = AdvanceGravityAfter(*o, 0.02f, FPS_ANIMATION_FACTOR);
            o->Scale += gravityTravel;
            o->Position[2] += gravityTravel * 20.f;
            Luminosity = (float)(o->LifeTime) * 0.2f;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
        }
        break;
    case BITMAP_FLAME:
        if (o->LifeTime <= 0)
        {
            Particles.Retire(i);
            break;
        }

        switch (o->SubType)
        {
        case 1:
            if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 10))
            {
                o->Velocity[1] += (32 * 0.2f) * FPS_ANIMATION_FACTOR;
                o->Scale -= 0.15f * FPS_ANIMATION_FACTOR;
            }
            AdvanceRandomParticleRotation(*o, Random, FPS_ANIMATION_FACTOR);
            o->Light[0] -= (0.05f) * FPS_ANIMATION_FACTOR;
            o->Light[1] -= (0.05f) * FPS_ANIMATION_FACTOR;
            o->Light[2] -= (0.05f) * FPS_ANIMATION_FACTOR;
            break;
        case 5:
            if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 10))
            {
                o->Velocity[1] += (32 * 0.2f) * FPS_ANIMATION_FACTOR;
                o->Scale *= powf(0.8f, FPS_ANIMATION_FACTOR);
            }
            AdvanceRandomParticleRotation(*o, Random, FPS_ANIMATION_FACTOR);
            o->Light[0] -= (0.05f) * FPS_ANIMATION_FACTOR;
            o->Light[1] -= (0.05f) * FPS_ANIMATION_FACTOR;
            o->Light[2] -= (0.05f) * FPS_ANIMATION_FACTOR;
            break;
        case 2:
            if (o->LifeTime < 10)
            {
                //                        o->Light[0] *= powf(1.0f / ((15-o->LifeTime)), FPS_ANIMATION_FACTOR);
                //                        o->Light[1] *= powf(1.0f / ((15-o->LifeTime)), FPS_ANIMATION_FACTOR);
                //                        o->Light[2] *= powf(1.0f / ((15-o->LifeTime)), FPS_ANIMATION_FACTOR);
            }
            AdvanceRandomParticleRotation(*o, Random, FPS_ANIMATION_FACTOR);
            o->Light[0] -= (0.05f) * FPS_ANIMATION_FACTOR;
            o->Light[1] -= (0.05f) * FPS_ANIMATION_FACTOR;
            o->Light[2] -= (0.05f) * FPS_ANIMATION_FACTOR;
            break;

        case 3:
            AdvanceRandomParticleRotation(*o, Random, FPS_ANIMATION_FACTOR);
            break;

        case 4:
            FollowParticleTarget(*o);
            AdvanceParticleGravity(*o, 1.f, 0.1f, FPS_ANIMATION_FACTOR);
            AdvanceRandomParticleRotation(*o, Random, FPS_ANIMATION_FACTOR);
            o->Light[0] -= (0.05f) * FPS_ANIMATION_FACTOR;
            o->Light[1] -= (0.05f) * FPS_ANIMATION_FACTOR;
            o->Light[2] -= (0.05f) * FPS_ANIMATION_FACTOR;
            break;
        case 8: {
            if (o->StartPosition[0] < o->Position[0])
                o->Rotation += (2.0f) * FPS_ANIMATION_FACTOR;
            else
                o->Rotation -= (2.0f) * FPS_ANIMATION_FACTOR;

            o->Position[2] += (o->Gravity / 2.f) * FPS_ANIMATION_FACTOR;
            o->Scale -= o->Gravity * FPS_ANIMATION_FACTOR / 95.f;
            if (o->Scale <= 0.0f)
                Particles.Retire(i);

            o->Light[0] *= powf(1.0f / (1.007f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.007f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.007f), FPS_ANIMATION_FACTOR);
        }
        break;
        case 7: {
            if (o->StartPosition[0] < o->Position[0])
                o->Rotation += (2.0f) * FPS_ANIMATION_FACTOR;
            else
                o->Rotation -= (2.0f) * FPS_ANIMATION_FACTOR;

            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Scale -= o->Gravity * FPS_ANIMATION_FACTOR / 95.f;
            if (o->Scale <= 0.0f)
                Particles.Retire(i);

            o->Light[0] *= powf(1.0f / (1.007f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.007f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.007f), FPS_ANIMATION_FACTOR);
        }
        break;
        case 9: {
            if (o->StartPosition[0] < o->Position[0])
                o->Rotation += (0.5f) * FPS_ANIMATION_FACTOR;
            else
                o->Rotation -= (0.5f) * FPS_ANIMATION_FACTOR;

            o->Position[2] += o->Gravity * 1.2f * FPS_ANIMATION_FACTOR;
            o->Scale -= o->Gravity * FPS_ANIMATION_FACTOR / 98.f;
            if (o->Scale <= 0.0f)
                Particles.Retire(i);

            o->Light[0] *= powf(1.0f / (1.008f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.008f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.008f), FPS_ANIMATION_FACTOR);
        }
        break;
        case 10:
            AdvanceDampedParticleMotion(*o, FPS_ANIMATION_FACTOR);
            o->Light[0] = (float)(o->LifeTime) / 10.f;
            o->Light[1] = o->Light[0];
            o->Light[2] = o->Light[0];
            o->Scale += FPS_ANIMATION_FACTOR * 0.07f;
            break;
        case 11:
            o->Light[0] *= powf(1.0f / (1.008f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.008f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.008f), FPS_ANIMATION_FACTOR);
            break;
        }
        break;
    case BITMAP_FIRE_RED:
        if (o->LifeTime <= 0)
            Particles.Retire(i);
        break;
    case BITMAP_RAIN_CIRCLE:
    case BITMAP_RAIN_CIRCLE + 1:
        if (o->LifeTime <= 0)
            Particles.Retire(i);
        if (o->SubType == 1)
        {
            o->Scale += o->Gravity * FPS_ANIMATION_FACTOR;
        }
        else
        {
            o->Scale += FPS_ANIMATION_FACTOR * 0.03f;
        }
        if (TheMapProcess().Crywolf1st().IsCyrWolf1st() == true)
        {
            Vector(1.0f, 1.0f, 0.7f, o->Light);
        }
        else
        {
            if (o->SubType == 2)
            {
                Vector(0.03f, 0.03f, 0.03f, Light);
                VectorSubtractScaled(o->Light, Light, o->Light, FPS_ANIMATION_FACTOR);
            }
            else
            {
                Vector(0.05f, 0.05f, 0.05f, Light);
                VectorSubtractScaled(o->Light, Light, o->Light, FPS_ANIMATION_FACTOR);
            }
        }
        break;
    case BITMAP_ENERGY: {
        o->Rotation += (o->Gravity) * FPS_ANIMATION_FACTOR;

        if (o->SubType == 1)
        {
            o->Light[1] = o->Light[2] = o->Light[0] = o->LifeTime / 15.f;
            o->Scale += FPS_ANIMATION_FACTOR * 0.1f;
        }
        else if (o->SubType == 2)
        {
            o->Light[0] -= (0.01f) * FPS_ANIMATION_FACTOR;
            o->Light[1] -= (0.01f) * FPS_ANIMATION_FACTOR;
            o->Light[2] -= (0.01f) * FPS_ANIMATION_FACTOR;
        }
        // ChainLighting
        else if (o->SubType == 3 || o->SubType == 4 || o->SubType == 5)
        {
            BMD *pModel = &Models[o->Target->Type];
            vec3_t vRelativePos, vPos;
            Vector(0.f, 0.f, 0.f, vRelativePos);

            if (o->SubType == 3)
            {
                pModel->TransformPosition(o->Target->BoneTransform[37], vRelativePos, vPos, true);
                VectorCopy(vPos, o->Position);
            }
            else if (o->SubType == 4)
            {
                pModel->TransformPosition(o->Target->BoneTransform[28], vRelativePos, vPos, true);
                VectorCopy(vPos, o->Position);
            }
            else if (o->SubType == 5)
            {
                VectorCopy(o->Target->Position, o->Position);
                o->Position[2] += (80.f) * FPS_ANIMATION_FACTOR;
            }

            o->Light[0] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 7)
        {
            VectorScale(o->Light, powf(0.97f, FPS_ANIMATION_FACTOR), o->Light);
        }
    }
    break;
    case BITMAP_MAGIC:
        if (o->SubType == 0)
        {
            o->Scale -= 0.05f * FPS_ANIMATION_FACTOR;

            o->Light[0] -= (0.01f) * FPS_ANIMATION_FACTOR;
            o->Light[1] -= (0.01f) * FPS_ANIMATION_FACTOR;
            o->Light[2] -= (0.01f) * FPS_ANIMATION_FACTOR;
        }
        break;
    case BITMAP_FLARE: //
        if (o->SubType == 0 || o->SubType == 3 || o->SubType == 6 || o->SubType == 10)
        {
            float count = (o->Velocity[0] + o->LifeTime) * 0.1f;
            if (o->SubType == 10)
            {
                o->Position[0] = o->StartPosition[0];
                o->Position[1] = o->StartPosition[1];
            }
            else
            {
                o->Position[0] = o->StartPosition[0] + sinf(count) * 40.f;
                o->Position[1] = o->StartPosition[1] - cosf(count) * 40.f;
            }
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;

            o->Scale -= 0.002f * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 2)
        {
            o->Position[0] = o->StartPosition[0];
            o->Position[1] = o->StartPosition[1];
            o->Position[2] += o->Gravity * ((60 - o->LifeTime) / 10) * FPS_ANIMATION_FACTOR;
            o->Scale -= 0.002f * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 5)
        {
            float count = (o->Velocity[0] + o->LifeTime) * 0.1f;
            o->Position[0] = o->StartPosition[0] + sinf(count) * 40.f;
            o->Position[1] = o->StartPosition[1] - cosf(count) * 40.f;
            o->Position[2] -= (o->Gravity) * FPS_ANIMATION_FACTOR;
            o->Scale -= 0.002f * FPS_ANIMATION_FACTOR;

            o->StartPosition[0] += (2.5f) * FPS_ANIMATION_FACTOR;
        }

        if (o->SubType == 4)
        {
            float count = (o->Velocity[0] + o->LifeTime) * 0.1f;
            o->Position[0] = o->StartPosition[0] + sinf(count) * 40.f;
            o->Position[1] = o->StartPosition[1] - cosf(count) * 40.f;
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Scale -= 0.004f * FPS_ANIMATION_FACTOR;

            if (o->LifeTime <= 30)
            {
                o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            }
        }
        else if (o->LifeTime <= 20)
        {
            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 11)
        {
            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Scale += 1.5f * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 12)
        {
            float count = (o->Velocity[0] + o->LifeTime) * 0.1f;

            {
                o->Position[0] = o->StartPosition[0] + sinf(count) * 110.f;
                o->Position[1] = o->StartPosition[1] - cosf(count) * 110.f;
            }
            o->Position[2] += ((o->Gravity + 0.1f)) * FPS_ANIMATION_FACTOR;

            o->Scale -= 0.002f * FPS_ANIMATION_FACTOR;
            if (o->LifeTime <= 35)
            {
                o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            }
        }
        if (o->SubType == 0)
        {
            if ((o->Target->CurrentAction >= PLAYER_WALK_MALE &&
                 o->Target->CurrentAction <= PLAYER_RUN_RIDE_WEAPON) ||
                (o->Target->CurrentAction >= PLAYER_ATTACK_SKILL_SWORD1 &&
                 o->Target->CurrentAction <= PLAYER_ATTACK_SKILL_SWORD5) ||
                (o->Target->CurrentAction == PLAYER_RAGE_UNI_RUN ||
                 o->Target->CurrentAction == PLAYER_RAGE_UNI_RUN_ONE_RIGHT))
            {
                o->SubType = 1;
                o->LifeTime = std::min<int>(20, o->LifeTime);
                Vector(0.f, 0.f, 0.f, o->Velocity);
                VectorCopy(o->Position, o->StartPosition);
            }
        }
        break;
    case BITMAP_FLARE_RED:
        o->Scale = sinf(o->LifeTime * 10.f * (Q_PI / 180.f)) * 3.f;
        HandPosition(o);
        break;
    case BITMAP_CLUD64: {
        if (o->SubType == 0)
        {
            Luminosity = (float)(o->LifeTime) / 10.f;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
            o->Position[2] -= AdvanceGravityAfter(*o, -0.01f, FPS_ANIMATION_FACTOR);
            o->Scale -= 0.03f * FPS_ANIMATION_FACTOR;
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.05f;
        }
        else if (o->SubType == 1 || o->SubType == 2)
        {
            o->Scale += FPS_ANIMATION_FACTOR * 0.5f;

            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);

            if (o->Light[0] <= 0.05f)
                Particles.Retire(i);
        }
        else if (o->SubType == 3)
        {
            o->Scale += FPS_ANIMATION_FACTOR * 0.08f;

            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);

            if (o->Light[0] <= 0.05f)
                Particles.Retire(i);
        }
        else if (o->SubType == 5)
        {
            o->Scale += FPS_ANIMATION_FACTOR * 0.08f;

            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);

            if (o->Light[0] <= 0.05f)
                Particles.Retire(i);
        }
        else if (o->SubType == 6)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 10.f, 0.2f, 2, 3, 0.1f);

            if (o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0)
            {
                o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 5, 55, 0.001f);
            }
            else if (o->Scale < 0.1f)
            {
                Particles.Retire(i);
            }
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            VectorSubtract(o->Position, o->StartPosition, o->Position);
            VectorCopy(o->Target->Position, o->StartPosition);
            VectorAdd(o->Position, o->StartPosition, o->Position);
        }
        else if (o->SubType == 7)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 10.f, 0.1f, 20, 0, 0.005f);
            VectorScale(o->Light,
                        std::pow(0.9f, std::clamp(10.f - o->LifeTime, 0.f, FPS_ANIMATION_FACTOR)),
                        o->Light);

            VectorAddScaled(o->Position, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);
            o->Scale += ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 10, 0, 0.01f);
        }
        else if (o->SubType == 8)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 10.f, 0.1f, 20, 0, 0.005f);
            VectorScale(o->Light,
                        std::pow(0.9f, std::clamp(10.f - o->LifeTime, 0.f, FPS_ANIMATION_FACTOR)),
                        o->Light);

            VectorAddScaled(o->Position, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);
            o->Scale += ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 10, 0, 0.01f);
        }
        else if (o->SubType == 9)
        {
            AdvanceCloudDamping(*o, Random, FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 10)
        {
            o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 5, 15, 0.0016f);
            o->Position[2] += o->Gravity * 10.f * FPS_ANIMATION_FACTOR;

            if (o->Scale < 0 || !o->Target->Live)
                Particles.Retire(i);
            VectorSubtract(o->Position, o->StartPosition, o->Position);
            VectorCopy(o->Target->Position, o->StartPosition);
            VectorAdd(o->Position, o->StartPosition, o->Position);
        }
        if (o->SubType == 11)
        {
            Luminosity = (float)(o->LifeTime) / 10.f;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
            o->Position[2] -= AdvanceGravityAfter(*o, -0.01f, FPS_ANIMATION_FACTOR);
            o->Scale -= 0.03f * FPS_ANIMATION_FACTOR;
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.05f;
        }
    }
    break;
    case BITMAP_LIGHT + 3: {
        if (o->SubType == 0)
        {
            float _Angle = (o->Velocity[0] + o->LifeTime) * 0.1f;
            o->Position[0] = o->StartPosition[0] + sinf(_Angle) * 35.f;
            o->Position[1] = o->StartPosition[1] - cosf(_Angle) * 35.f;
            o->Position[2] += o->Gravity * 0.1f * FPS_ANIMATION_FACTOR;
            o->Scale -= FPS_ANIMATION_FACTOR * 0.001f;
        }
#ifdef PJH_ADD_PANDA_PET
        else if (o->SubType == 1)
        {
            o->Scale = sinf(o->LifeTime * 2.f * (Q_PI / 180.f));
        }
#endif //PJH_ADD_PANDA_PET
    }
    break;

    case BITMAP_TWINTAIL_WATER: {
        if (o->SubType == 0 || o->SubType == 1)
        {
            o->Scale -= FPS_ANIMATION_FACTOR * 0.013f;
        }
        else if (o->SubType == 2)
        {
            o->Scale -= FPS_ANIMATION_FACTOR * 0.026f;
        }

        o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
        o->Alpha -= FPS_ANIMATION_FACTOR * 0.05f;
        o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
        o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
        o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
    }
    break;
    case BITMAP_SMOKE:
        switch (o->SubType)
        {
        case 0:
            Luminosity = (float)(o->LifeTime) / 8.f;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
            o->Position[2] += AdvanceGravityAfter(*o, 0.2f, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            break;
        case 36:
            AdvanceReleasingSmoke(*o, FPS_ANIMATION_FACTOR);
            o->Rotation += (0.01f) * FPS_ANIMATION_FACTOR;
            break;
        case 37:
            o->Position[2] -= (o->TurningForce[2] * 0.8f) * FPS_ANIMATION_FACTOR;
            o->Position[1] -= (o->TurningForce[1] * 0.8f) * FPS_ANIMATION_FACTOR;
            o->Scale += FPS_ANIMATION_FACTOR * 0.08f;
            o->Rotation += (0.01f) * FPS_ANIMATION_FACTOR;
            o->Light[0] *= powf(1.0f / (1.03f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.03f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.03f), FPS_ANIMATION_FACTOR);
            break;
        case 38:
            o->Scale += FPS_ANIMATION_FACTOR * 0.09f;
            o->Light[0] *= powf(1.0f / (1.04f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.04f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.04f), FPS_ANIMATION_FACTOR);
            break;
        case 33:
            Luminosity = (float)(o->LifeTime) / 8.f;
            Vector(Luminosity * 0.4f, Luminosity * 0.4f, Luminosity, o->Light);
            Core::Time::Advance(o->Position[2], o->Scale, 0.02f, FPS_ANIMATION_FACTOR);
            o->Position[2] += 0.02f * FPS_ANIMATION_FACTOR;
            break;
        case 32:
            Luminosity = (float)(o->LifeTime) / 8.f;
            Vector(Luminosity * 0.8f, Luminosity * 0.8f, Luminosity, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.1f;
            break;
        case 23:
            Luminosity = (float)(o->LifeTime) / 8.f;
            Vector(Luminosity * 0.1f, Luminosity, Luminosity * 0.6f, o->Light);
            o->Position[2] += AdvanceGravityAfter(*o, 0.2f, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            break;
        case 3:
            Luminosity = (float)(o->LifeTime) / 8.f;
            Vector(Luminosity * 0.8f, Luminosity * 0.8f, Luminosity, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.1f;
            break;
        case 11:
        case 14:
            Luminosity = (float)(o->LifeTime) / 50.f;
            Vector(Luminosity * o->TurningForce[0], Luminosity * o->TurningForce[1],
                   Luminosity * o->TurningForce[2], o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            o->Position[2] -= (1.f) * FPS_ANIMATION_FACTOR;
            break;
        case 17:
            Luminosity = (float)(o->LifeTime) / 8.f;
            Luminosity = (Luminosity > 1.0f ? 1.0f - Luminosity : Luminosity);
            Vector(Luminosity * o->TurningForce[0], Luminosity * o->TurningForce[1],
                   Luminosity * o->TurningForce[2], o->Light);
            o->Position[2] += AdvanceGravityAfter(*o, 0.2f, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            break;
        case 15:
            Luminosity = (float)(o->LifeTime) / 40.f;
            Vector(Luminosity * 0.5f, Luminosity * 0.5f, Luminosity * 0.5f, o->Light);
            AdvanceRandomSmokeGravity(*o, Random, FPS_ANIMATION_FACTOR);
            o->Rotation = o->Scale;
            break;
        case 1:
            Luminosity = (float)(o->LifeTime) / 50.f;
            Vector(Luminosity * 0.5f, Luminosity * 1.f, Luminosity * 0.8f, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            break;
        case 2:
            Luminosity = (float)(o->LifeTime) / 50.f;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
            AdvanceSlantingGravity(*o, -0.1f, -0.2f, FPS_ANIMATION_FACTOR);
            o->Scale -= FPS_ANIMATION_FACTOR * 0.01f;
            break;
        case 16:
            Luminosity = (float)(o->LifeTime) / 50.f;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
            AdvanceSlantingGravity(*o, -0.1f, -0.2f, FPS_ANIMATION_FACTOR);
            o->Scale -= FPS_ANIMATION_FACTOR * 0.01f;
            break;
        case 12:
            Luminosity = (float)(o->LifeTime) / 40.f;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            break;
        case 13:
            Luminosity = (float)(o->LifeTime) / 20.f;
            Vector(Luminosity * 1.f, Luminosity * 0.5f, Luminosity * 0.1f, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.09f;
            break;
        case 18:
            Luminosity = (float)(o->LifeTime) / 20.f;
            Vector(Luminosity * 1.f, Luminosity * 1.f, Luminosity * 1.f, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.09f;
            break;
        case 4:
            Luminosity = (float)(o->LifeTime) / 8.f;
            Vector(Luminosity * 120.f / 255.f, Luminosity * 100.7f / 255.f,
                   Luminosity * 80.f / 255.f, o->Light);
            o->Position[2] += AdvanceGravityAfter(*o, 0.2f, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            break;
        case 5:
            Luminosity = (float)(o->LifeTime) / 10.f;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
            AdvanceRandomSlantingGravity(*o, Random, FPS_ANIMATION_FACTOR);
            o->Scale -= FPS_ANIMATION_FACTOR * 0.01f;
            break;
        case 6:
            Luminosity = 0.6f;
            o->LifeTime = 10;
            o->Position[2] = o->Rotation + sinf((WorldTime + o->Gravity) / 5000.0f) * 20.0f;
            o->Scale =
                sinf(((int)(o->Gravity + WorldTime) % 1800 * 0.1f * (Q_PI / 180.f))) * 0.5f + 1.8f;

            Vector(Luminosity * 0.6f, Luminosity * 0.5f, Luminosity * 0.4f, o->Light);
            break;
        case 7:
            Luminosity = 1.0f;

            o->Scale += FPS_ANIMATION_FACTOR * 0.03f;
            o->Position[2] -= AdvanceGravityAfter(*o, 1.0f, FPS_ANIMATION_FACTOR);
            if (o->LifeTime < 5)
            {
                Luminosity = (float)(o->LifeTime) / 8.f;
                o->Scale -= FPS_ANIMATION_FACTOR * 0.1f;
            }
            Vector(Luminosity * 0.725f, Luminosity * 0.572f, Luminosity * 0.333f, o->Light);
            break;
        case 8:
            AdvanceFadingSmoke(*o, FPS_ANIMATION_FACTOR);
            break;
        case 9:
            Luminosity = (float)(o->LifeTime) / 10.f;
            Vector(Luminosity * 250.f / 255.f, Luminosity * 156.7f / 255.f, 0.f, o->Light);
            o->Position[2] += AdvanceGravityAfter(*o, 0.5f, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.1f;
            break;
        case 10:
            Luminosity = (float)(o->LifeTime) / 16.f;
            Vector(Luminosity * 1.f, Luminosity * 0.1f, Luminosity * 0.1f, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            break;
        case 19:
            Luminosity = (float)(o->LifeTime) / 40.f;
            Vector(Luminosity * 0.5f, Luminosity * 0.5f, Luminosity * 0.5f, o->Light);
            AdvanceRandomSmokeGravity(*o, Random, FPS_ANIMATION_FACTOR);
            o->Rotation = o->Scale;
            break;
        case 20:
            Luminosity = (float)(o->LifeTime) / 40.f;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
            AdvanceRandomSmokeGravity(*o, Random, FPS_ANIMATION_FACTOR);
            o->Rotation = o->Scale;
            break;
        case 21:
            Luminosity = (float)(o->LifeTime) / 50.f;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
            AdvanceSlantingGravity(*o, -0.1f, -0.2f, FPS_ANIMATION_FACTOR);
            o->Scale -= FPS_ANIMATION_FACTOR * 0.01f;
            break;
        case 22:
            Luminosity = (float)(o->LifeTime) / 50.f;
            Vector(Luminosity * 0.9f, Luminosity * 0.5f, Luminosity * 0.5f, o->Light);
            AdvanceSmokeWaveGravity(*o, Random, -0.1f, FPS_ANIMATION_FACTOR, WorldTime,
                                    1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps);
            o->Scale += FPS_ANIMATION_FACTOR * 0.04f;
            break;
        case 24:
            Luminosity = (float)(o->LifeTime) / 32.f;
            Vector(Luminosity * 0.2f, Luminosity * 0.5f, Luminosity * 0.35f, o->Light);
            o->Position[2] += AdvanceGravityAfter(*o, 0.1f, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            break;
        case 25:
            Luminosity = (float)(o->LifeTime) / 20.f;
            Vector(Luminosity * 1.f, Luminosity * 1.f, Luminosity * 1.f, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.09f;
            break;
        case 35:
            o->Light[0] *= powf(1.0f / (1.04f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.04f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.04f), FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.06f;
            break;
        case 34:
            o->Light[0] *= powf(1.0f / (1.04f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.04f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.04f), FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.06f;
            break;
        case 26:
            Luminosity = (float)(o->LifeTime) / 20.f;
            Vector(o->Light[0] * Luminosity * 1.f, o->Light[1] * Luminosity * 1.f,
                   o->Light[2] * Luminosity * 1.f, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.15f;
            break;
        case 27:
            Vector(o->Light[0] * 0.92f, o->Light[1] * 0.92f, o->Light[2] * 0.92f, o->Light);
            o->Scale -= FPS_ANIMATION_FACTOR * 0.05f;
            break;
        case 28:
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            if (o->LifeTime >= 9)
                o->Scale -= FPS_ANIMATION_FACTOR * 0.4f;
            else
                o->Scale *= powf(1.2f, FPS_ANIMATION_FACTOR);
            Vector(o->Light[0] * 0.92f, o->Light[1] * 0.92f, o->Light[2] * 0.92f, o->Light);
            break;
        case 29:
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            if (o->LifeTime >= 9)
                o->Scale -= FPS_ANIMATION_FACTOR * 0.4f;
            else
                o->Scale *= powf(1.2f, FPS_ANIMATION_FACTOR);
            Vector(1.0f, 1.0f, 1.0f, o->Light);
            break;
        case 30:
            o->Scale += FPS_ANIMATION_FACTOR * 0.15f;
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            Vector(1.0f, 3.0f, 1.0f, o->Light);
            break;
        case 31:
            o->Scale -= FPS_ANIMATION_FACTOR * 0.05f;
            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            if (o->Scale <= 0.0f && o->Light[2] <= 0.0f)
                Particles.Retire(i);
            break;
        case 40: {
            Luminosity = (float)(o->LifeTime) / 50.f;
            Vector(Luminosity * 0.5f, Luminosity * 0.5f, Luminosity * 1.0f, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
        }
        break;
        case 41: {
            Luminosity = (float)(o->LifeTime) / 50.f;
            Vector(Luminosity * 0.5f, Luminosity * 1.f, Luminosity * 0.8f, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            o->Position[2] += AdvanceGravityAfter(*o, 0.2f, FPS_ANIMATION_FACTOR);
        }
        break;
        case 42:
            Luminosity = (float)(o->LifeTime) / 8.f;
            Vector(Luminosity * 127.f / 255.f, Luminosity * 255.f / 255.f,
                   Luminosity * 200.f / 255.f, o->Light);
            o->Position[2] += AdvanceGravityAfter(*o, 0.2f, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            break;
        case 43: {
            o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.08f;
        }
        break;
        case 44: {
            if (o->LifeTime >= 30)
            {
                o->Light[0] *= powf(1.07f, FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.07f, FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.07f, FPS_ANIMATION_FACTOR);
            }
            else
            {
                o->Light[0] *= powf(1.0f / (1.07f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.0f / (1.07f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.0f / (1.07f), FPS_ANIMATION_FACTOR);
            }
            o->Scale += FPS_ANIMATION_FACTOR * 0.02f;
            o->Position[2] += (0.8f) * FPS_ANIMATION_FACTOR;
            o->Rotation += (o->Gravity / 50.0f) * FPS_ANIMATION_FACTOR;
        }
        break;
        case 45: {
            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
        }
        break;
        case 46: {
            o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);

            if (o->Light[0] <= 0.05f)
                o->LifeTime = 0;

            o->Position[2] +=
                1.5f * (o->Scale * Core::Time::DampedDistance(1.01f, FPS_ANIMATION_FACTOR) +
                        AdvanceGravityAfter(*o, -0.05f, FPS_ANIMATION_FACTOR));
            o->Scale *= std::pow(1.01f, FPS_ANIMATION_FACTOR);
        }
        break;
        case 47: {
            o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);

            if (o->Light[0] <= 0.05f)
                o->LifeTime = 0;

            o->Scale += 1.0f * FPS_ANIMATION_FACTOR;
        }
        break;
        case 59:
            Luminosity = (float)(o->LifeTime) / 40.f;
            Vector(Luminosity * 0.9f, Luminosity * 0.9f, Luminosity * 0.9f, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.09f;
            break;
        case 48:
            Luminosity = (float)(o->LifeTime) / 40.f;
            Vector(Luminosity * 0.4f, Luminosity * 0.4f, Luminosity * 0.4f, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.09f;
            break;
        case 49: {
            if (o->LifeTime < 100)
            {
                o->Light[0] *= powf(0.97f, FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(0.97f, FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(0.97f, FPS_ANIMATION_FACTOR);
            }
            else
            {
                o->Light[0] *= powf(1.03f, FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.03f, FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.03f, FPS_ANIMATION_FACTOR);
            }

            if (o->Light[0] <= 0.01f)
                o->LifeTime = 0;

            AdvanceRisingSmokeScale(*o, Random, FPS_ANIMATION_FACTOR);
        }
        break;
        case 50:
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 10.f, 0.1f, 3, 2, 0.1f, 0.5f);
            if (o->Alpha < 0.1f)
            {
                Particles.Retire(i);
            }
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.02f;
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            break;
        case 51:
            Luminosity = (float)(o->LifeTime) / 20.f;
            VectorScale(o->TurningForce, Luminosity, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.09f;
            break;
        case 52: {
            VectorAddScaled(o->Position, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.06f;
            o->Light[0] *= powf(1.0f / (1.07f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.07f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.07f), FPS_ANIMATION_FACTOR);
        }
        break;
        case 53:
            Luminosity = (float)(o->LifeTime) / 50.f;
            Vector(Luminosity * 1.f, Luminosity * 1.f, Luminosity * 1.f, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            break;
        case 54: {
            o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);

            if (o->Light[0] <= 0.05f)
                o->LifeTime = 0;

            o->Position[2] +=
                1.5f * (o->Scale * Core::Time::DampedDistance(1.01f, FPS_ANIMATION_FACTOR) +
                        AdvanceGravityAfter(*o, -0.05f, FPS_ANIMATION_FACTOR));
            o->Scale *= std::pow(1.01f, FPS_ANIMATION_FACTOR);
        }
        break;
        case 55: {
            o->Position[2] += AdvanceGravityAfter(*o, 0.1f, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;

            o->Light[0] *= powf(1.0f / (1.08f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.08f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.08f), FPS_ANIMATION_FACTOR);
        }
        break;
        case 56:
            Luminosity = (float)(o->LifeTime) / 50.f;
            Vector(Luminosity * 0.5f, Luminosity * 0.1f, Luminosity * 0.8f, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            break;
        case 57:
            Luminosity = (float)(o->LifeTime) / 32.f;
            Vector(Luminosity * 0.5f, Luminosity * 0.1f, Luminosity * 0.8f, o->Light);
            o->Position[2] += AdvanceGravityAfter(*o, 0.1f, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            break;
        case 58:
            Luminosity = (float)(o->LifeTime) / 50.f;
            Vector(Luminosity * o->StartPosition[0], Luminosity * o->StartPosition[1],
                   Luminosity * o->StartPosition[2], o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            break;
        case 60: {
            Luminosity = (float)(o->LifeTime) / 50.f;
            Vector(Luminosity * 0.4f, Luminosity * 0.4f, Luminosity * 0.4f, o->Light);
            AdvanceSmokeWaveGravity(*o, Random, -0.1f, FPS_ANIMATION_FACTOR, WorldTime,
                                    1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps);
            o->Scale += FPS_ANIMATION_FACTOR * 0.04f;
        }
        break;
        case 61: {
            Luminosity = (float)(o->LifeTime) / 8.f;
            o->Position[2] += AdvanceGravityAfter(*o, 0.2f, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
        }
        break;
        case 62: {
            Luminosity = (float)(o->LifeTime) / 50.f;
            //Vector(Luminosity*0.9f,Luminosity*0.4f,Luminosity*0.1f,o->Light);
            Vector(Luminosity * 0.9f, Luminosity * 0.9f, Luminosity * 0.9f, o->Light);
            o->Scale += FPS_ANIMATION_FACTOR * 0.03f;
        }
        break;
        case 63: {
            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.5f;
        }
        break;
        case 64: {
            Luminosity = (float)(o->LifeTime) / 24.f;
            Vector(Luminosity * 0.1f, Luminosity * 0.7f, Luminosity * 0.4f, o->Light);

            MovePosition(o->Position, o->Angle, o->Velocity);

            o->Rotation += 0.05f * FPS_ANIMATION_FACTOR; //(WorldRandom()%100)/100.f) ;

            AdvanceSmokeWaveGravity(*o, Random, -0.1f, FPS_ANIMATION_FACTOR, WorldTime,
                                    1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps);

            if (16 > o->LifeTime)
            {
                o->Alpha -= FPS_ANIMATION_FACTOR * 0.1f;
            }
        }
        break;
        case 65: {
            Luminosity = (float)(o->LifeTime) / 40.f;
            Vector(Luminosity * 0.4f, Luminosity * 0.4f, Luminosity * 0.4f, o->Light);
            AdvanceSmokeWaveGravity(*o, Random, -0.05f, FPS_ANIMATION_FACTOR, WorldTime,
                                    1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps);
        }
        break;
        case 66: {
            o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);

            if (o->Light[0] <= 0.05f)
                o->LifeTime = 0;

            AdvanceExpandingSmoke(*o, FPS_ANIMATION_FACTOR);
        }
        break;
        case 67: {
            AdvanceOpacityLight(*o, 0.01f, FPS_ANIMATION_FACTOR);
            if (o->Alpha < 0.1f)
                Particles.Retire(i);

            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            o->Rotation += (0.01f) * FPS_ANIMATION_FACTOR;
        }
        break;
        case 68: {
            Luminosity = (float)(o->LifeTime) / 40.f;
            Vector(Luminosity * 0.4f, Luminosity * 0.4f, Luminosity * 0.4f, o->Light);
            AdvanceGrowingSmoke(*o, FPS_ANIMATION_FACTOR);

            if (30 > o->LifeTime)
            {
                o->Alpha -= FPS_ANIMATION_FACTOR * 0.02f;
            }
        }
        break;
#ifdef ASG_ADD_MAP_KARUTAN
        case 69:
            Luminosity = (float)(o->LifeTime) / 50.f;
            Vector(Luminosity * o->Angle[0], Luminosity * o->Angle[1], Luminosity * o->Angle[2],
                   o->Light);
            AdvanceSmokeWaveGravity(*o, Random, -0.04f, FPS_ANIMATION_FACTOR, WorldTime,
                                    1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps);
            o->Scale += FPS_ANIMATION_FACTOR * 0.04f;
            break;
#endif // ASG_ADD_MAP_KARUTAN
        }
        break;
    case BITMAP_SMOKE + 2:
        Luminosity = 1.f;
        if (o->LifeTime < 10)
            Luminosity -= (float)(10 - o->LifeTime) * 0.1f;
        Vector(Luminosity, Luminosity, Luminosity, o->Light);
        AngleMatrix(o->Angle, Matrix);
        VectorRotate(o->StartPosition, Matrix, Position);
        VectorCopy(o->Target->Position, TargetPosition);
        VectorAdd(TargetPosition, Position, o->Position);
        o->Angle[1] += (5.f) * FPS_ANIMATION_FACTOR;
        o->Scale -= FPS_ANIMATION_FACTOR * 0.01f;
        break;
    case BITMAP_SMOKE + 3:
        switch (o->SubType)
        {
        case 0:
            if (o->TurningForce[0] > 0.1f)
                o->TurningForce[0] -= (0.015f) * FPS_ANIMATION_FACTOR;
            if (o->TurningForce[1] > 0.1f)
                o->TurningForce[0] -= (0.005f) * FPS_ANIMATION_FACTOR;
            //o->TurningForce[1] -= (0.001f) * FPS_ANIMATION_FACTOR;
            Luminosity = (float)(o->LifeTime) / 55.f;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            o->Position[2] += AdvanceGravityAfter(*o, 0.2f, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.03f;
            o->Rotation += (o->Angle[0]) * FPS_ANIMATION_FACTOR;
            break;
        case 1:
            Luminosity = (float)(o->LifeTime) / 55.f;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            o->Position[2] += AdvanceGravityAfter(*o, 0.1f, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.03f;
            o->Rotation += (o->Angle[0]) * FPS_ANIMATION_FACTOR;
            break;
        case 2:
            if (o->TurningForce[1] > 0.1f)
                o->TurningForce[1] -= (0.005f) * FPS_ANIMATION_FACTOR;
            if (o->TurningForce[2] > 0.1f)
                o->TurningForce[2] -= (0.015f) * FPS_ANIMATION_FACTOR;
            Luminosity = (float)(o->LifeTime) / 55.f;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            o->Position[2] += AdvanceGravityAfter(*o, 0.2f, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.03f;
            o->Rotation += (o->Angle[0]) * FPS_ANIMATION_FACTOR;
            break;
        case 3:
            if (o->Light[0] <= 0.05f)
                Particles.Retire(i);
            o->Light[0] *= powf(1.0f / (1.012f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.012f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.012f), FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.01f;
            o->Rotation += (o->Gravity / 2.0f) * FPS_ANIMATION_FACTOR;

            if (o->Gravity <= 0.0f)
                o->Gravity = -o->Gravity;
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            break;
        case 4:
            if (o->Light[0] <= 0.05f)
                Particles.Retire(i);
            o->Light[0] *= powf(1.0f / (1.012f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.012f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.012f), FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.01f;
            o->Rotation += (o->Gravity / 2.0f) * FPS_ANIMATION_FACTOR;

            if (o->Gravity <= 0.0f)
                o->Gravity = -o->Gravity;
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            break;
        }
        break;
    case BITMAP_SMOKELINE1:
    case BITMAP_SMOKELINE2:
    case BITMAP_SMOKELINE3:
        if (o->SubType == 0 || o->SubType == 5)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 10.f, 0.1f, 5, 2, 0.1f, 0.7f);

            if (o->Alpha < 0.1f)
            {
                Particles.Retire(i);
            }
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            o->Scale += ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 3, 6, 0.01f); //0.07f;
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            if (o->SubType == 5)
            {
                VectorSubtract(o->Position, o->StartPosition, o->Position);
                VectorCopy(o->Target->Position, o->StartPosition);
                VectorAdd(o->Position, o->StartPosition, o->Position);
            }
        }
        else if (o->SubType == 1)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 10.f, 0.2f, 5, 2, 0.1f);

            if (o->Alpha < 0.1f)
            {
                Particles.Retire(i);
            }
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0)
            {
                o->Scale -=
                    ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 10, 10, 0.001f); //0.07f;
            }
            else
            {
                Particles.Retire(i);
            }
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            VectorSubtract(o->Position, o->StartPosition, o->Position);
            VectorCopy(o->Target->Position, o->StartPosition);
            VectorAdd(o->Position, o->StartPosition, o->Position);
        }
        else if ((o->SubType == 2) || (o->SubType == 3))
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 10.f, 0.1f, 5, 2, 0.1f, 0.7f);

            if (o->Alpha < 0.1f)
            {
                Particles.Retire(i);
            }
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            o->Scale += ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 3, 6, 0.01f);
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 4)
        {
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (0.8f) * FPS_ANIMATION_FACTOR;
            o->Scale += FPS_ANIMATION_FACTOR * 0.01f;

            AdvanceOpacityLight(*o, 0.01f, FPS_ANIMATION_FACTOR);
            if (o->Alpha < 0.1f)
                Particles.Retire(i);
        }
        break;
    case BITMAP_LIGHTNING_MEGA1:
    case BITMAP_LIGHTNING_MEGA2:
    case BITMAP_LIGHTNING_MEGA3: {
        switch (o->SubType)
        {
        case 0: {
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.15f;

            if (o->Alpha < 0.1f)
            {
                Particles.Retire(i);
            }
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
        }
        break;
        }
    }
    break;
    case BITMAP_FIRE_HIK1:
    case BITMAP_FIRE_HIK1_MONO:
        if (o->SubType == 0 || o->SubType == 6)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 15.f, 0.2f, 2, 2, 0.1f);

            if (o->MotionRandomSeed == 0 && o->LifeTime < 15.f && o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0 || o->MotionRandomSeed != 0)
            {
                o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 3, 5, 0.01f);
                if (o->MotionRandomSeed != 0)
                    o->Scale = (std::max)(0.f, o->Scale);
            }
            else if (o->MotionRandomSeed == 0)
            {
                Particles.Retire(i);
            }
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (3.0f) * FPS_ANIMATION_FACTOR;
            if (o->SubType == 6)
            {
                o->Rotation = 0.0f;
                o->Position[2] += o->Gravity * 1.2f * FPS_ANIMATION_FACTOR;
            }
        }
        else if (o->SubType == 1)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 20.f, 0.2f, 2, 2, 0.1f);

            if (o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0)
            {
                o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 2, 4, 0.01f);
            }
            else
            {
                Particles.Retire(i);
            }
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (3.0f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 2)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 10.f, 0.2f, 2, 2, 0.1f);

            if (o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0)
            {
                o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 3, 7, 0.01f);
            }
            else
            {
                Particles.Retire(i);
            }
            o->Position[0] += (o->Velocity[0]) * FPS_ANIMATION_FACTOR;
            o->Position[1] += (o->Velocity[1]) * FPS_ANIMATION_FACTOR;
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (3.0f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 10)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 15.f, 0.2f, 2, 2, 0.1f);

            if (o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0)
            {
                o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 3, 5, 0.01f);
            }
            else
            {
                Particles.Retire(i);
            }
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (3.0f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 3)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 15.f, 0.2f, 2, 2, 0.1f);

            if (o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0)
            {
                o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 3, 2, 0.01f);
            }
            else
            {
                Particles.Retire(i);
            }
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (3.0f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 4)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 5.f, 0.1f, 2, 2, 0.1f);

            if (o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);

            o->Scale += ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 3, 8, 0.01f);
            if (o->LifeTime < 3)
            {
                o->Scale += (10 - o->LifeTime) * 0.02f * FPS_ANIMATION_FACTOR;
            }

            AdvanceAttachedFlame(*o, FPS_ANIMATION_FACTOR);

            o->Rotation += (5.0f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 5)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 15.f, 0.2f, 2, 2, 0.1f);

            if (o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0)
            {
                o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 3, 5, 0.01f);
            }
            else
            {
                Particles.Retire(i);
            }
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (3.0f) * FPS_ANIMATION_FACTOR;
        }
        break;
    case BITMAP_FIRE_HIK3:
    case BITMAP_FIRE_HIK3_MONO:
        if (o->SubType == 0 || o->SubType == 6)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 10.f, 0.2f, 2, 2, 0.1f);

            if (o->MotionRandomSeed == 0 && o->LifeTime < 10.f && o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0 || o->MotionRandomSeed != 0)
            {
                o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 3, 7, 0.01f);
                if (o->MotionRandomSeed != 0)
                    o->Scale = (std::max)(0.f, o->Scale);
            }
            else if (o->MotionRandomSeed == 0)
            {
                Particles.Retire(i);
            }
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (3.0f) * FPS_ANIMATION_FACTOR;
            if (o->SubType == 6)
            {
                o->Rotation = 0.0f;
                o->Position[2] += o->Gravity * 1.2f * FPS_ANIMATION_FACTOR;
            }
        }
        else if (o->SubType == 1)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 15.f, 0.2f, 2, 2, 0.1f);

            if (o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0)
            {
                o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 2, 6, 0.01f);
            }
            else
            {
                Particles.Retire(i);
            }
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (3.0f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 2)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 10.f, 0.2f, 2, 2, 0.1f);

            if (o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0)
            {
                o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 3, 7, 0.01f);
            }
            else
            {
                Particles.Retire(i);
            }
            o->Position[0] += (o->Velocity[0]) * FPS_ANIMATION_FACTOR;
            o->Position[1] += (o->Velocity[1]) * FPS_ANIMATION_FACTOR;
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (3.0f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 3)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 10.f, 0.2f, 2, 2, 0.1f);

            if (o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0)
            {
                o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 3, 7, 0.01f);
            }
            else
            {
                Particles.Retire(i);
            }
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (3.0f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 4)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 15.f, 0.2f, 2, 2, 0.1f);

            if (o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0)
            {
                o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 4, 5, 0.01f);
            }
            else
            {
                Particles.Retire(i);
            }
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR; //*((float)o->LifeTime/39.0f)));
            o->Rotation += (3.0f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 5)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 4.f, 0.1f, 2, 2, 0.1f);

            if (o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);

            o->Scale += ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 3, 8, 0.01f);
            if (o->LifeTime < 3)
            {
                o->Scale += (10 - o->LifeTime) * 0.02f * FPS_ANIMATION_FACTOR;
            }

            AdvanceAttachedFlame(*o, FPS_ANIMATION_FACTOR);

            o->Rotation += (5.0f) * FPS_ANIMATION_FACTOR;
        }
        break;
    case BITMAP_LIGHT + 1:
        if (o->SubType == 2)
        {
            o->Scale *= powf(0.92f, FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 3)
        {
            o->Scale *= powf(1.3f, FPS_ANIMATION_FACTOR);
            o->Light[0] *= powf(0.9f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(0.9f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(0.9f, FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 5)
        {
            o->LifeTime = 5;
            o->Light[0] *= powf(0.7f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(0.7f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(0.7f, FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 4)
        {
        }
        else
        {
            AdvanceParticleGravity(*o, -1.f, 1.f, FPS_ANIMATION_FACTOR);
            o->Position[1] -= (1.f) * FPS_ANIMATION_FACTOR;
            o->Scale *= powf(0.95f, FPS_ANIMATION_FACTOR);
        }
        break;
    case BITMAP_BLOOD:
    case BITMAP_BLOOD + 1:
        o->Frame = (12 - o->LifeTime) / 3;
        //o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
        //o->Gravity -= 1.5f;
        AdvanceDampedDualMotion(*o, 0.95f, true, FPS_ANIMATION_FACTOR);
        break;
    case BITMAP_SPARK:
        Luminosity = (float)(o->LifeTime) / 16.f;
        if (o->LifeTime < 0)
        {
            Particles.Retire(i);
        }
        if (o->SubType == 11)
        {
            Vector(Luminosity, Luminosity * 0.3f, Luminosity * 0.3f, o->Light);
        }
        else if (o->SubType != 8)
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
        AdvanceBouncingParticle(*o, FPS_ANIMATION_FACTOR, *sessionKeeper_.WorldUnit());
        if (o->SubType == 6)
        {
            Vector(0.9f, 0.9f, 0.9f, o->Light);
        }
        else if (o->SubType == 8 || o->SubType == 10)
        {
            o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
        }
        if (o->SubType == 5 || o->SubType == 6)
        {
            vec3_t p;
            VectorSubtract(o->StartPosition, o->Target->Position, p);
            VectorCopy(o->Target->Position, o->StartPosition);
            VectorSubtract(o->Position, p, o->Position);
        }
        break;
    case BITMAP_SPARK + 1:
        switch (o->SubType)
        {
        case 0:
            o->Scale -= FPS_ANIMATION_FACTOR * 0.5f;
            if (o->Scale < 0.2f)
                Particles.Retire(i);
            break;
        case 1:
            o->Scale -= FPS_ANIMATION_FACTOR * 2.f;
            if (o->Scale < 0.2f)
                Particles.Retire(i);
            break;
        case 2:
            if (o->LifeTime <= 0)
                Particles.Retire(i);
            if (o->LifeTime > 5 && o->Target->Live == 0)
                o->LifeTime = 5;

            o->Light[0] = o->LifeTime / 5.f;
            o->Light[1] = o->LifeTime / 5.f;
            o->Light[2] = o->LifeTime / 5.f;

            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            VectorAddScaled(o->StartPosition, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);
            VectorCopy(o->Position, o->StartPosition);
            break;
        case 3:
            o->Scale *= powf(0.8f, FPS_ANIMATION_FACTOR);
            break;
        case 4:
            if (o->LifeTime <= 0)
                Particles.Retire(i);
            if (o->LifeTime > 5 && o->Target->Live == 0)
                o->LifeTime = 5;

            o->Light[0] = o->LifeTime / 5.f;
            o->Light[1] = o->LifeTime / 5.f;
            o->Light[2] = o->LifeTime / 5.f;

            o->Scale += FPS_ANIMATION_FACTOR * 0.08f;

            VectorAddScaled(o->StartPosition, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);
            VectorCopy(o->Position, o->StartPosition);

            vec3_t p;
            VectorSubtract(o->Target->Position, o->Target->StartPosition, p);
            VectorAdd(o->Position, p, o->Position);
            break;
        case 5:
            o->Scale *= powf(0.9f, FPS_ANIMATION_FACTOR);
            o->Light[0] *= powf(1.0f / (1.03f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.03f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.03f), FPS_ANIMATION_FACTOR);
            break;
        case 6: {
            if (o->LifeTime <= 0)
                Particles.Retire(i);
            AdvanceParticleGravity(*o, -1.f, 0.1f, FPS_ANIMATION_FACTOR);
            o->Position[1] -= FPS_ANIMATION_FACTOR;
            float fLight = (float)(WorldRandom() % 10) / 100.0f + 0.7f;
            Vector(o->Light[0], fLight, o->Light[2], o->Light);
        }
        break;
        case 7: {
            o->Scale -= FPS_ANIMATION_FACTOR * 0.02f;
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.001f;
            if (o->Alpha <= 0.0f)
                Particles.Retire(i);
            o->Position[2] -= (o->Gravity) * FPS_ANIMATION_FACTOR;
            float fLight = (float)(WorldRandom() % 50) / 100.0f + 0.2f;
            Vector(0.0f, fLight, 0.0f, o->Light);
        }
        break;
        case 8: {
            o->Scale -= FPS_ANIMATION_FACTOR * 0.5f;
            if (o->Scale < 0.2f)
                Particles.Retire(i);
        }
        break;
        case 9: {
            vec3_t acceleration{-1.2f, 0.f, -1.f};
            AdvanceAcceleratedVelocity(*o, acceleration, FPS_ANIMATION_FACTOR);
            o->Scale -= FPS_ANIMATION_FACTOR * 0.08f;
            if (o->LifeTime <= 0)
                Particles.Retire(i);
            if (o->Scale < 0.2f)
                Particles.Retire(i);
        }
        break;
        case 10: {
            o->Light[0] *= powf(1.0f / (1.08f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.08f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.08f), FPS_ANIMATION_FACTOR);
            o->Scale -= FPS_ANIMATION_FACTOR * 0.03f;
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.001f;
            if (o->Alpha <= 0.0f)
                Particles.Retire(i);
        }
        break;
        case 13:
        case 11: {
            o->Light[0] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Scale -= FPS_ANIMATION_FACTOR * 0.04f;
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.001f;
            if (o->Alpha <= 0.0f)
                Particles.Retire(i);
        }
        break;
        case 12: {
            float fLight = WorldRandom() % 2;
            Vector(fLight - 0.6f, fLight - 0.6f, fLight - 0.8f, o->Light);
            o->Scale -= FPS_ANIMATION_FACTOR * 0.04f;
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.001f;
            if (o->Alpha <= 0.0f)
                Particles.Retire(i);
        }
        break;
        case 14: {
            o->Light[0] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Scale -= FPS_ANIMATION_FACTOR * 0.01f;
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.001f;
            if (o->Alpha <= 0.0f)
                Particles.Retire(i);
        }
        break;
        case 15: {
            o->Light[0] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.001f;
            if (o->Alpha <= 0.0f)
                Particles.Retire(i);
        }
        break;
        case 16:
        case 18: {
            AdvanceHoverSpark(*o, FPS_ANIMATION_FACTOR, *sessionKeeper_.WorldUnit());

            o->Alpha -= FPS_ANIMATION_FACTOR * 0.007f;
            if (o->Alpha <= 0.0f)
                Particles.Retire(i);
        }
        break;
        case 17: {
            if (o->Position[2] >= o->StartPosition[2] + 350.0f)
            {
                o->Light[0] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            }

            if (o->Light[0] <= 0.05f)
                Particles.Retire(i);

            o->Position[0] +=
                (sinf(WorldTime * o->Rotation) * o->Gravity * 0.3f) * FPS_ANIMATION_FACTOR;
            //o->Position[1] += (sinf(WorldTime*o->Rotation)*o->Gravity*0.3f) * FPS_ANIMATION_FACTOR;
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
        }
        break;
        case 19: {
            if (o->LifeTime <= 0)
                Particles.Retire(i);

            AdvanceParticleGravity(*o, 1.f, 0.1f, FPS_ANIMATION_FACTOR);

            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
        }
        break;
        case 20: {
            o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);

            if (o->Light[0] <= 0.05f)
                Particles.Retire(i);

            AdvanceBouncingParticle(*o, FPS_ANIMATION_FACTOR, *sessionKeeper_.WorldUnit());
        }
        break;
        case 21: {
            o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
        }
        break;
        case 22: {
            o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);

            if (o->Light[0] <= 0.05f)
                Particles.Retire(i);

            AdvanceBouncingParticle(*o, FPS_ANIMATION_FACTOR, *sessionKeeper_.WorldUnit());
        }
        break;
        case 23: {
            AdvanceChangingSpark(*o, FPS_ANIMATION_FACTOR);
            o->Light[0] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Scale -= FPS_ANIMATION_FACTOR * 0.02f;
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.0001f;
            if (o->Alpha <= 0.0f)
                Particles.Retire(i);
        }
        break;
        case 24: {
            AdvanceChangingSpark(*o, FPS_ANIMATION_FACTOR);

            AdvanceOpacityLight(*o, 0.05f,
                                std::clamp(11.f - o->LifeTime, 0.f, FPS_ANIMATION_FACTOR));

            o->Scale -= FPS_ANIMATION_FACTOR * 0.02f;
        }
        break;
        case 25: {
            AdvanceChangingSpark(*o, FPS_ANIMATION_FACTOR);

            o->Light[0] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Scale = sinf(o->LifeTime * (Q_PI / 40.f));
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.001f;
            if (o->Alpha <= 0.0f)
                Particles.Retire(i);
        }
        break;
        case 26: {
            if (o->LifeTime <= 0)
                Particles.Retire(i);

            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);

            o->Scale -= FPS_ANIMATION_FACTOR * 0.05f;
            VectorAddScaled(o->StartPosition, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);
            VectorCopy(o->Position, o->StartPosition);
        }
        break;
        case 27: {
            o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);

            o->Scale *= powf(1.0f / (1.03f), FPS_ANIMATION_FACTOR);
        }
        break;
        case 28: {
            o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);

            o->Scale *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);

            if (o->Light[0] <= 0.03f)
                Particles.Retire(i);

            AdvanceBouncingParticle(*o, FPS_ANIMATION_FACTOR, *sessionKeeper_.WorldUnit());
        }
        break;
        case 29: {
            o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);

            o->Scale *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);

            if (o->Light[0] <= 0.03f)
                Particles.Retire(i);

            AdvanceBouncingParticle(*o, FPS_ANIMATION_FACTOR, *sessionKeeper_.WorldUnit());
        }
        break;
        case 30: {
            AdvanceWanderingSpark(*o, Random, FPS_ANIMATION_FACTOR);

            o->Light[0] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);

            o->Scale -= FPS_ANIMATION_FACTOR * 0.01f;

            o->LifeTime -= 1 * FPS_ANIMATION_FACTOR;
            if (o->LifeTime <= 0)
                Particles.Retire(i);
        }
        break;
        case 31:
            AdvanceBouncingParticle(*o, FPS_ANIMATION_FACTOR, *sessionKeeper_.WorldUnit());
            break;
        }
        break;
    case BITMAP_SPARK + 2: {
        if (o->SubType == 0)
        {
            o->Frame = (16 - o->LifeTime) / 4;
        }
        else if (o->SubType == 1)
        {
            for (int events = Core::Time::Periods(o->LifeTime, FPS_ANIMATION_FACTOR, 3.f);
                 events > 0; --events)
            {
                vec3_t vPos;
                VectorCopy(o->Position, vPos);
                vPos[0] += ((float)(WorldRandom() % 100 - 50));
                vPos[1] += ((float)(WorldRandom() % 100 - 50));
                vPos[2] += ((float)(WorldRandom() % 100 - 50));
                CreateBomb(vPos, true);
            }
        }
        else if ((o->SubType == 2 || o->SubType == 3) && o->Target != NULL)
        {
            o->Frame = (16 - o->LifeTime) / 4;

            // 플레이어 모델
            BMD *pModel = &Models[o->Target->Type];
            vec3_t vPos;

            switch (o->SubType)
            {
            case 2:
                // 플레이어 왼손
                pModel->TransformByObjectBone(vPos, o->Target, 37);
                break;
            case 3:
                // 플레이어 오른손
                pModel->TransformByObjectBone(vPos, o->Target, 28);
                break;
            }

            VectorCopy(vPos, o->Position);
        }
    }
    break;

    case BITMAP_SMOKE + 1:
    case BITMAP_SMOKE + 4:
        Luminosity = (float)(o->LifeTime) / 32.f;
        Vector(Luminosity, Luminosity, Luminosity, o->Light);
        o->Scale += FPS_ANIMATION_FACTOR * 0.08f;
        //o->Angle[0] += (4.f) * FPS_ANIMATION_FACTOR;
        AdvanceDampedDualMotion(*o, 0.9f, false, FPS_ANIMATION_FACTOR);

        if (o->SubType == 6)
        {
            o->Scale -= FPS_ANIMATION_FACTOR * 0.05f;
            o->Position[2] += (4.0f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType != 5)
        {
            const auto texture = Bitmaps.GetTextureProperties(o->Type);
            const float textureHeight = texture.has_value() ? texture->height : 0.0F;
            o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) +
                             textureHeight * o->Scale * 0.5f;
        }
        break;

    case BITMAP_SHINY:
        if (o->SubType == 2)
        {
            o->Rotation -= (12.f) * FPS_ANIMATION_FACTOR;

            AdvanceDescendingShiny(*o, Random, FPS_ANIMATION_FACTOR);

            o->Light[0] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);

            CreateSprite(BITMAP_LIGHT, o->Position, o->Scale / 1.5f, o->Light, NULL);
        }
        else if (o->SubType == 3)
        {
            o->Light[0] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Scale -= FPS_ANIMATION_FACTOR * 0.04f;
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.001f;
            if (o->Alpha <= 0.0f)
                Particles.Retire(i);
        }
        else if (o->SubType == 4)
        {
            o->Rotation += (20.f) * FPS_ANIMATION_FACTOR;
            AdvanceParticleGravity(*o, 0.5f, -1.5f, FPS_ANIMATION_FACTOR);

            o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);

            o->Scale -= FPS_ANIMATION_FACTOR * 0.01f;

            VectorAddScaled(o->Position, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 5)
        {
            o->Rotation += (10.f) * FPS_ANIMATION_FACTOR;
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;

            o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);

            o->Scale -= FPS_ANIMATION_FACTOR * 0.01f;
        }
        else if (o->SubType == 6)
        {
            o->Light[0] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);

            if (o->Light[0] < 0.2f)
            {
                Particles.Retire(i);
            }

            o->StartPosition[0] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
            if (o->LifeTime < 60 && o->TurningForce[0] == 0.f)
            {
                o->Scale = 0;
            }
            else
            {
                o->Scale = o->StartPosition[0];
            }
        }
        else if (o->SubType == 7)
        {
            o->Scale = sinf(o->LifeTime * 2.f * (Q_PI / 180.f));
        }
        else if (o->SubType == 8)
        {
            o->Light[0] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);

            if (o->Light[0] < 0.2f)
            {
                Particles.Retire(i);
            }

            o->StartPosition[0] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
            if (o->LifeTime < 60 && o->TurningForce[0] == 0.f)
            {
                o->Scale = 0;
            }
            else
            {
                o->Scale = o->StartPosition[0];
            }
        }
        else if (o->SubType == 9)
        {
            o->Light[0] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);

            o->StartPosition[0] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
            if (o->LifeTime < 60 && o->TurningForce[0] == 0.f)
            {
                o->Scale = 0;
            }
            else
            {
                o->Scale = o->StartPosition[0];
            }

            if (o->Light[0] < 0.2f)
            {
                Particles.Retire(i);
            }
        }
        else
        {
            o->Scale = sinf(o->LifeTime * 10.f * (Q_PI / 180.f));
            if (o->SubType == 1)
            {
                o->Scale *= 0.75f;
                o->Rotation -= (12.f) * FPS_ANIMATION_FACTOR;
            }
        }
        break;
    case BITMAP_CHERRYBLOSSOM_EVENT_PETAL: {
        if (o->SubType == 0)
        {
            AdvanceRandomParticleRotation(*o, Random, FPS_ANIMATION_FACTOR);
            o->Scale = sinf(o->LifeTime * (Q_PI / 180.f));
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.001f;
            if (o->Alpha <= 0.0f)
                Particles.Retire(i);
        }
        else if (o->SubType == 1)
        {
            if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 69))
            {
                o->Velocity[0] -= (1.f) * FPS_ANIMATION_FACTOR;
                o->Velocity[1] -= (1.f) * FPS_ANIMATION_FACTOR;
                o->Velocity[2] -= (1.f) * FPS_ANIMATION_FACTOR;
            }

            o->Light[0] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);

            //o->Scale = sinf(o->LifeTime*(Q_PI/240.f));
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.001f;
            //o->Rotation = (float)(WorldRandom()%360);
            if (o->Alpha <= 0.0f)
                Particles.Retire(i);
        }
    }
    break;
    case BITMAP_CHERRYBLOSSOM_EVENT_FLOWER: {
        if (o->SubType == 0)
        {
            o->Scale = sinf(o->LifeTime * (Q_PI / 180.f));
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.002f;
            if (o->Alpha <= 0.0f)
                Particles.Retire(i);

            AdvanceBouncingParticle(*o, FPS_ANIMATION_FACTOR, *sessionKeeper_.WorldUnit());
            AdvanceRandomParticleRotation(*o, Random, FPS_ANIMATION_FACTOR);
        }
    }
    break;
    case BITMAP_SHINY + 1:
        if (o->SubType == 99)
        {
            VectorCopy(o->Target->Position, o->Position);
            o->Position[2] += 50.f;
            //                    o->Scale *= powf(0.75f, FPS_ANIMATION_FACTOR);
            o->Rotation -= (1.f) * FPS_ANIMATION_FACTOR;
        }
        else
        {
            if (o->SubType == 5)
            {
                VectorCopy(o->Target->Angle, o->Angle);
                o->Scale -= FPS_ANIMATION_FACTOR * 0.06f;
                o->Position[2] += o->Gravity * 10.f * FPS_ANIMATION_FACTOR;
            }
            else
            {
                o->Scale = sinf(o->LifeTime * 5.f * (Q_PI / 180.f)) * 5.f;
                //Luminosity = (float)(o->LifeTime)/36.f;
                //Vector(Luminosity*0.6f,Luminosity*0.8f,Luminosity,o->Light);
                if (o->SubType >= 2)
                {
                    o->Scale *= 0.75f;
                    o->Rotation -= (12.f) * FPS_ANIMATION_FACTOR;
                }
                HandPosition(o);
            }
        }
        break;
    case BITMAP_SHINY + 2:
        o->Scale = sinf(o->LifeTime * 10.f * (Q_PI / 180.f)) * 3.f;
        HandPosition(o);
        break;
    case BITMAP_SHINY + 4:
        if (o->SubType == 0)
        {
            o->Scale = sinf(o->LifeTime * 10.f * (Q_PI / 180.f)) * 3.f + 2.f;
            Luminosity = (float)(o->LifeTime) / 5.f;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
        }
        else if (o->SubType == 1)
        {
            constexpr float Lifetime = 15.f, PhaseLimit = 90.f;
            const float age = (std::max)(0.f, Lifetime - o->LifeTime);
            const float priorAge = (std::max)(0.f, age - 1.f);
            // The factory starts phase at zero. Sum the authored growing increments once from age.
            const float visiblePhase = (std::min)(PhaseLimit, 3.f * priorAge * (priorAge + 1.f));
            o->Gravity = (std::min)(PhaseLimit, 3.f * age * (age + 1.f));
            o->Scale = std::sin(visiblePhase * (Q_PI / 180.f)) * 3.f + 1.5f;
            const float fading = std::clamp(6.f - o->LifeTime, 0.f, FPS_ANIMATION_FACTOR);
            VectorScale(o->Light, std::pow(0.5f, fading), o->Light);
        }
        else if (o->SubType == 2)
        {
            o->Scale *= powf(1.1f, FPS_ANIMATION_FACTOR);
            VectorScale(o->Light, powf(0.9f, FPS_ANIMATION_FACTOR), o->Light);
        }
        break;
    case BITMAP_SHINY + 6:
        if (o->SubType == 0)
        {
            o->Rotation += (5.f) * FPS_ANIMATION_FACTOR;
            o->Scale -= FPS_ANIMATION_FACTOR * 0.02f;
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.001f;
            if (o->Alpha <= 0.0f)
                Particles.Retire(i);
            o->Position[2] -= (o->Gravity) * FPS_ANIMATION_FACTOR;
            const float lightChange =
                RandomScalarTravel(o->MotionIntervalFrames, o->ScalarNoiseRate, Random,
                                   FPS_ANIMATION_FACTOR, 10, -5, 0.01f);
            o->Light[0] += lightChange;
            o->Light[1] += lightChange;
            o->Light[2] += lightChange;
        }
        else if (o->SubType == 1)
        {
            o->Scale -= FPS_ANIMATION_FACTOR * 0.01f;
            o->Rotation -= (5.f) * FPS_ANIMATION_FACTOR;
        }

        break;
    case BITMAP_PIN_LIGHT: {
        o->Scale -= FPS_ANIMATION_FACTOR * 0.02f;
        o->Alpha -= FPS_ANIMATION_FACTOR * 0.001f;
        if (o->Alpha <= 0.0f)
            Particles.Retire(i);
        if (o->SubType == 1)
        {
            float Matrix[3][4];
            vec3_t p;
            Vector(0.f, -150.f, 0.f, p);
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(o->Position, Matrix, o->Position);
        }
        else
        {
            o->Position[2] -= (o->Gravity) * FPS_ANIMATION_FACTOR;
        }
        float fLight = (float)(WorldRandom() % 10) / 100.0f - 0.05f;
        o->Light[0] += (fLight)*FPS_ANIMATION_FACTOR;
        o->Light[1] += (fLight)*FPS_ANIMATION_FACTOR;
        o->Light[2] += (fLight)*FPS_ANIMATION_FACTOR;
    }
    break;
    case BITMAP_ORORA: {
        float fScale, fLight;
        BMD *pModel = &Models[o->Target->Type];
        vec3_t vRelativePos, vPos;
        Vector(0.f, 0.f, 0.f, vRelativePos);

        switch (o->SubType)
        {
        case 0:
            fScale = 0.01f;
            fLight = 1.05f;
            o->Rotation += (5.f) * FPS_ANIMATION_FACTOR;
            pModel->TransformByObjectBone(vPos, o->Target, 37);
            break;
        case 1:
            fScale = 0.01f;
            fLight = 1.05f;
            o->Rotation -= (5.f) * FPS_ANIMATION_FACTOR;
            pModel->TransformByObjectBone(vPos, o->Target, 28);
            break;
        case 2:
            fScale = 0.04f;
            //						fLight = 1.12f;
            fLight = 1.33f;
            o->Rotation += (20.f) * FPS_ANIMATION_FACTOR;
            pModel->TransformByObjectBone(vPos, o->Target, 37);
            break;
        case 3:
            fScale = 0.04f;
            //						fLight = 1.12f;
            fLight = 1.33f;
            o->Rotation -= (20.f) * FPS_ANIMATION_FACTOR;
            pModel->TransformByObjectBone(vPos, o->Target, 28);
            break;
        }

        VectorCopy(vPos, o->Position);
        o->Scale += fScale * FPS_ANIMATION_FACTOR;
        if (o->Scale >= 0.8f)
        {
            o->Light[0] *= powf(1.0f / (fLight), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (fLight), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (fLight), FPS_ANIMATION_FACTOR);
        }
    }
    break;
    case BITMAP_SNOW_EFFECT_1:
    case BITMAP_SNOW_EFFECT_2:
        o->Rotation += (20.f) * FPS_ANIMATION_FACTOR;
        AdvanceParticleGravity(*o, 0.5f, -1.5f, FPS_ANIMATION_FACTOR);

        o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
        o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
        o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);

        if (o->LifeTime < 10)
            o->Scale += FPS_ANIMATION_FACTOR * 0.01f;
        else
            o->Scale -= 0.01f * FPS_ANIMATION_FACTOR;

        VectorAddScaled(o->Position, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);
        break;

    case BITMAP_DS_EFFECT:
        o->Rotation += (10.f) * FPS_ANIMATION_FACTOR;

        if (o->Target != NULL)
        {
            if (o->Target->CurrentAction != PLAYER_SANTA_2)
            {
                Particles.Retire(i);
            }
        }
        break;
    case BITMAP_FIRECRACKER: {
        vec3_t acceleration{0.f, 0.f, -0.5f};
        AdvanceAcceleratedVelocity(*o, acceleration, FPS_ANIMATION_FACTOR);
    }
    break;
    case BITMAP_SWORD_FORCE:
        o->Scale += AdvanceGravityAfter(*o, 0.01f, FPS_ANIMATION_FACTOR);
        Luminosity = (float)(o->LifeTime) / 10.f;
        Vector(Luminosity, Luminosity, Luminosity, o->Light);
        break;
    case BITMAP_TORCH_FIRE:
        if (o->LifeTime <= 0)
        {
            Particles.Retire(i);
        }
        else
        {
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;

            if (o->Position[2] >= 500.f)
            {
                Particles.Retire(i);
            }
            if (o->LifeTime >= 20.f)
            {
                o->Scale -= FPS_ANIMATION_FACTOR * 0.02f;
                if (o->Scale <= 0.02f)
                {
                    Particles.Retire(i);
                }
            }
        }
        break;

    case BITMAP_GHOST_CLOUD1:
    case BITMAP_GHOST_CLOUD2: {
        if (o->SubType == 0)
        {
            VectorAddScaled(o->Position, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);

            vec3_t vTemp;
            VectorSubtract(o->Position, o->StartPosition, vTemp);
            float fTemp = VectorLength(vTemp);

            AdvanceGhostRotation(*o, Random, FPS_ANIMATION_FACTOR, WorldTime,
                                 1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps);

            if (fTemp <= 20.f)
            {
                for (int i = 0; i < 3; ++i)
                {
                    o->Light[i] = o->TurningForce[i] * 0.1f * (500 - o->LifeTime);
                    if (o->Light[i] > o->TurningForce[i])
                    {
                        o->Light[i] = o->TurningForce[i];
                    }
                }
            }
            else
            {
                o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            }

            if (fTemp > 500.0f)
            {
                Particles.Retire(i);
            }
        }
    }
    break;
    case BITMAP_CLOUD:
        if (o->SubType == 6)
        {
            AdvanceDampedParticleMotion(*o, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            o->Light[0] = (float)(o->LifeTime) / 50.f;
            o->Light[1] = o->Light[0];
            o->Light[2] = o->Light[0];
            AdvanceSmokeDrift(*o, Random, FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 8)
        {
            if (o->LifeTime <= 0)
            {
                Particles.Retire(i);
            }
            else if (o->LifeTime > 150)
            {
                if (o->Alpha < 1.0f)
                    o->Alpha += FPS_ANIMATION_FACTOR * 0.04;
                o->Position[2] += (1.f) * FPS_ANIMATION_FACTOR;
            }
            else
            {
                if (o->Alpha > 0.1f)
                    o->Alpha -= FPS_ANIMATION_FACTOR * 0.025f;
                o->Position[2] -= (0.5f) * FPS_ANIMATION_FACTOR;
            }
        }
        else if (o->SubType == 9)
        {
            if (o->LifeTime <= 0)
            {
                Particles.Retire(i);
            }
            else if (o->LifeTime > 150)
            {
                if (o->Alpha < 1.0f)
                    o->Alpha += FPS_ANIMATION_FACTOR * 0.04;
                o->Position[2] += (1.f) * FPS_ANIMATION_FACTOR;
            }
            else
            {
                if (o->Alpha > 0.1f)
                    o->Alpha -= FPS_ANIMATION_FACTOR * 0.025f;
                o->Position[2] -= (0.5f) * FPS_ANIMATION_FACTOR;
            }
        }
        else if (o->SubType == 10)
        {
            VectorAddScaled(o->Position, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);
            o->TurningForce[0] = 1.5f;

            vec3_t vTemp;
            VectorSubtract(o->Position, o->StartPosition, vTemp);
            float fTemp = VectorLength(vTemp);

            if (fTemp <= 300.0f)
            {
                o->Light[0] += (0.01f) * FPS_ANIMATION_FACTOR;
                o->Light[1] += (0.01f) * FPS_ANIMATION_FACTOR;
                o->Light[2] += (0.01f) * FPS_ANIMATION_FACTOR;
                if (o->Light[0] >= 0.15f)
                    Vector(0.15f, 0.15f, 0.15f, o->Light);
            }
            else if (fTemp > 300.0f && fTemp <= 500.0f)
            {
                o->Light[0] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            }
            else
            {
                Particles.Retire(i);
            }
        }
        else if (o->SubType == 11)
        {
            VectorAddScaled(o->Position, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);
            o->TurningForce[0] = 1.5f;

            vec3_t vTemp;
            VectorSubtract(o->Position, o->StartPosition, vTemp);
            float fTemp = VectorLength(vTemp);

            o->Rotation += 0.5f * FPS_ANIMATION_FACTOR +
                           RandomScalarTravel(o->RotationNoiseFrames, o->ScalarNoiseRate, Random,
                                              FPS_ANIMATION_FACTOR, 2, 0, 1.f);

            if (fTemp > 500.0f)
            {
                Particles.Retire(i);
            }
        }
        else if (o->SubType == 13)
        {
            o->Rotation += (o->Gravity * 1.5f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 14)
        {
            AdvanceDampedParticleMotion(*o, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            o->Light[0] = (float)(o->LifeTime) / 50.f;
            o->Light[1] = o->Light[0];
            o->Light[2] = o->Light[0];
            AdvanceSmokeDrift(*o, Random, FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 15)
        {
            if (o->LifeTime <= 10)
            {
                Particles.Retire(i);
            }

            VectorAddScaled(o->Position, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);

            vec3_t vTemp;
            VectorSubtract(o->Position, o->StartPosition, vTemp);
            float fTemp = VectorLength(vTemp);

            o->Rotation += 0.5f * FPS_ANIMATION_FACTOR +
                           RandomScalarTravel(o->RotationNoiseFrames, o->ScalarNoiseRate, Random,
                                              FPS_ANIMATION_FACTOR, 2, 0, 1.f);

            if (fTemp <= 50.f)
            {
                for (int i = 0; i < 3; ++i)
                {
                    o->Light[i] = o->TurningForce[i] * 0.1f * (500 - o->LifeTime);
                    if (o->Light[i] > o->TurningForce[i])
                    {
                        o->Light[i] = o->TurningForce[i];
                    }
                }
            }
            else
            {
                o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);

                if (o->Light[0] < 0.001f)
                    Particles.Retire(i);
            }

            if (fTemp > 500.0f)
            {
                Particles.Retire(i);
            }
        }
        else if (o->SubType == 16)
        {
            if (o->LifeTime <= 10)
            {
                Particles.Retire(i);
            }

            VectorAddScaled(o->Position, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);

            vec3_t vTemp;
            VectorSubtract(o->Position, o->StartPosition, vTemp);
            float fTemp = VectorLength(vTemp);

            o->Rotation += 0.5f * FPS_ANIMATION_FACTOR +
                           RandomScalarTravel(o->RotationNoiseFrames, o->ScalarNoiseRate, Random,
                                              FPS_ANIMATION_FACTOR, 2, 0, 1.f);

            if (fTemp <= 50.0f)
            {
                o->Light[0] += (0.01f) * FPS_ANIMATION_FACTOR;
                o->Light[1] += (0.01f) * FPS_ANIMATION_FACTOR;
                o->Light[2] += (0.01f) * FPS_ANIMATION_FACTOR;
                if (o->Light[0] >= 0.2f)
                    Vector(0.2f, 0.2f, 0.2f, o->Light);
            }
            else
            {
                o->Light[0] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);

                if (o->Light[0] < 0.001f)
                    Particles.Retire(i);
            }

            if (fTemp >= 100.f)
            {
                Particles.Retire(i);
            }
        }
        else if (o->SubType == 17)
        {
            if (o->LifeTime <= 10)
            {
                Particles.Retire(i);
            }
            VectorAddScaled(o->Position, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);

            vec3_t vTemp;
            VectorSubtract(o->Position, o->StartPosition, vTemp);
            float fTemp = VectorLength(vTemp);

            o->Rotation += 0.08f * FPS_ANIMATION_FACTOR +
                           RandomScalarTravel(o->RotationNoiseFrames, o->ScalarNoiseRate, Random,
                                              FPS_ANIMATION_FACTOR, 2, 0, 0.15f);

            if (o->LifeTime >= 400.f)
            {
                for (int i = 0; i < 3; ++i)
                {
                    o->Light[i] = o->TurningForce[i] * 0.02f * (500 - o->LifeTime);
                    if (o->Light[i] > o->TurningForce[i])
                    {
                        o->Light[i] = o->TurningForce[i];
                    }
                }
            }
            else
            {
                o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);

                if (o->Light[0] < 0.015f)
                    Particles.Retire(i);
            }

            if (fTemp > 500.0f)
            {
                Particles.Retire(i);
            }
        }
        else if (o->SubType == 18)
        {
            Luminosity = 1.0f;
            if (o->LifeTime > 90)
            {
                o->Light[0] += (0.002f) * FPS_ANIMATION_FACTOR;
                o->Light[1] += (0.002f) * FPS_ANIMATION_FACTOR;
                o->Light[2] += (0.0017f) * FPS_ANIMATION_FACTOR;
            }
            else if (o->LifeTime < 30)
            {
                o->Light[0] -= (0.004f) * FPS_ANIMATION_FACTOR;
                o->Light[1] -= (0.004f) * FPS_ANIMATION_FACTOR;
                o->Light[2] -= (0.0033f) * FPS_ANIMATION_FACTOR;
            }
            if (o->Scale > 0)
                o->Scale -= FPS_ANIMATION_FACTOR * 0.001f;
            if (o->TurningForce[1] > 0)
                o->TurningForce[1] -= (0.5f) * FPS_ANIMATION_FACTOR;
            o->Position[0] =
                o->Target->Position[0] + o->StartPosition[0] +
                cosf((WorldTime + o->Rotation) * o->TurningForce[0]) * o->TurningForce[1];
            o->Position[1] =
                o->Target->Position[1] + o->StartPosition[1] +
                sinf((WorldTime + o->Rotation) * o->TurningForce[0]) * o->TurningForce[1];
            o->Position[2] += RandomScalarTravel(o->MotionIntervalFrames, o->ScalarNoiseRate,
                                                 Random, FPS_ANIMATION_FACTOR, 10, 0, 0.1f);

            if (o->LifeTime <= 0)
            {
                o->Target->HiddenMesh = 0;
                Particles.Retire(i);
            }
        }
        else if (o->SubType == 19)
        {
            Luminosity = 1.0f;
            if (o->LifeTime > 30)
            {
                o->Light[0] += (0.01f) * FPS_ANIMATION_FACTOR;
                o->Light[1] += (0.01f) * FPS_ANIMATION_FACTOR;
                o->Light[2] += (0.01f) * FPS_ANIMATION_FACTOR;
                o->Scale += FPS_ANIMATION_FACTOR * 0.01f;
            }
            else
            {
                o->Light[0] -= (0.01f) * FPS_ANIMATION_FACTOR;
                o->Light[1] -= (0.01f) * FPS_ANIMATION_FACTOR;
                o->Light[2] -= (0.01f) * FPS_ANIMATION_FACTOR;
            }
            //o->Position[0] -= (4.0f+(WorldRandom()%100)*0.1f) * FPS_ANIMATION_FACTOR;
            o->Position[1] += 8.f * FPS_ANIMATION_FACTOR +
                              RandomScalarTravel(o->MotionIntervalFrames, o->ScalarNoiseRate,
                                                 Random, FPS_ANIMATION_FACTOR, 200, 0, 0.1f);
            //o->Position[2] = o->StartPosition[2] + sinf((WorldTime+o->Gravity)/5000.0f)*20.0f;

            if (o->LifeTime <= 0)
            {
                o->Target->HiddenMesh = 0;
                Particles.Retire(i);
            }
        }
        else if (o->SubType == 20)
        {
            if (o->LifeTime <= 0)
            {
                Particles.Retire(i);
            }
            else if (o->LifeTime > 150)
            {
                if (o->Alpha < 1.0f)
                    o->Alpha += FPS_ANIMATION_FACTOR * 0.04;
                o->Position[2] += (1.f) * FPS_ANIMATION_FACTOR;
            }
            else
            {
                if (o->Alpha > 0.1f)
                    o->Alpha -= FPS_ANIMATION_FACTOR * 0.025f;
                o->Position[2] -= (0.5f) * FPS_ANIMATION_FACTOR;
            }
        }
        else if (o->SubType == 21)
        {
            if (o->LifeTime <= 0)
            {
                Particles.Retire(i);
            }
            else if (o->LifeTime > 50)
            {
                if (o->Alpha < 1.0f)
                    o->Alpha += FPS_ANIMATION_FACTOR * 0.04;
                o->Position[2] += (2.f) * FPS_ANIMATION_FACTOR;
            }
            else
            {
                if (o->Alpha > 0.1f)
                    o->Alpha -= FPS_ANIMATION_FACTOR * 0.005f;
                o->Position[2] -= (1.0f) * FPS_ANIMATION_FACTOR;
            }
        }
        else if (o->SubType == 22)
        {
            if (o->LifeTime <= 0)
            {
                Particles.Retire(i);
            }
            else if (o->LifeTime > 40)
            {
                if (o->Alpha < 1.0f)
                    o->Alpha += FPS_ANIMATION_FACTOR * 0.1f;
                o->Position[2] += (0.5f) * FPS_ANIMATION_FACTOR;
            }
            else
            {
                if (o->Alpha > 0.0f)
                    o->Alpha -= FPS_ANIMATION_FACTOR * 0.025f;
                else
                    Particles.Retire(i);
                o->Position[2] -= (0.5f) * FPS_ANIMATION_FACTOR;
            }

            o->Scale += FPS_ANIMATION_FACTOR * 0.001f;
        }
        else if (o->SubType == 23)
        {
            AdvanceFadingCloud(*o, Random, FPS_ANIMATION_FACTOR);
            if (o->LifeTime <= 0.f || o->Alpha <= 0.f)
                Particles.Retire(i);
            o->Rotation += (o->TurningForce[0] * 5.0f) * FPS_ANIMATION_FACTOR;
        }
        else
        {
            Luminosity = 0.6f;
            AdvanceAttachedCloudLifetime(Particles, *o);

            o->Position[2] = o->StartPosition[2] + sinf((WorldTime + o->Gravity) / 5000.0f) * 20.0f;
        }
        switch (o->SubType)
        {
        case 1:
        case 4:
            o->Rotation = (WorldTime * 0.02f * o->TurningForce[0]) + o->StartPosition[1];
            break;

        case 2:
        case 5:
            o->Rotation = (WorldTime * (-0.02f) * o->TurningForce[0]) + o->StartPosition[1];
            break;
        }
        break;

    case BITMAP_LIGHT:
        if (0 == o->SubType || 2 == o->SubType || o->SubType == 7 || o->SubType == 8)
        {
            float fScale = (0 == o->SubType || o->SubType == 8) ? 0.5f : 4.f;

            if (o->SubType == 7)
            {
                AdvanceRisingLight(*o, FPS_ANIMATION_FACTOR, WorldTime,
                                   1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps);
            }
            else
            {
                vec3_t windTravel{};
                sessionKeeper_.PhysicsStorage().ParticleWindTravel(FPS_ANIMATION_FACTOR,
                                                                   windTravel);
                AdvanceWindLight(*o, Random, FPS_ANIMATION_FACTOR, windTravel, fScale);
            }
            if (0 == o->SubType || o->SubType == 8)
            {
                o->Scale -= FPS_ANIMATION_FACTOR * 0.05f;
            }
            else if (o->SubType == 7)
            {
                //                        o->Scale = sin ( o->Gravity+WorldTime*0.001f )*0.2f+0.5f;
                o->Rotation += (30.f) * FPS_ANIMATION_FACTOR;
            }
            else if (1 == o->SubType)
            {
                o->Scale *= powf(0.98f, FPS_ANIMATION_FACTOR);
            }
            else if (5 == o->SubType)
            {
                o->Scale *= powf(0.95f, FPS_ANIMATION_FACTOR);
            }
            else
            {
                o->Scale *= powf(0.95f, FPS_ANIMATION_FACTOR);
            }
            if (o->SubType == 7)
            {
                if (o->LifeTime < 10)
                {
                    o->Light[0] *= powf(1.0f / (1.2f), FPS_ANIMATION_FACTOR);
                    o->Light[1] *= powf(1.0f / (1.2f), FPS_ANIMATION_FACTOR);
                    o->Light[2] *= powf(1.0f / (1.2f), FPS_ANIMATION_FACTOR);
                }
            }
            else
            {
                Vector(o->Scale, o->Scale, o->Scale, o->Light);
            }
            if (1 == o->SubType)
            {
                Vector(0.3f * o->Scale, 0.3f * o->Scale, 0.3f * o->Scale, o->Light);
            }
            if (5 == o->SubType)
            {
                Vector(0.3f * o->Scale, 0.3f * o->Scale, 0.3f * o->Scale, o->Light);
            }
            if (((0 == o->SubType || o->SubType == 8) && o->Scale <= 0.1f) ||
                (o->SubType == 7 && o->Scale <= 0.1f) || (2 == o->SubType && o->Scale <= 0.3f))
            {
                o->LifeTime = -1;
                Particles.Retire(i);
            }
            if (8 == o->SubType)
            {
                Vector(1.0f * o->Scale, 0.5f * o->Scale, 0.3f * o->Scale, o->Light);
            }
        }
        else if (o->SubType == 6)
        {
            AdvanceHorizontalDrift(*o, Random, 2001, 1000, FPS_ANIMATION_FACTOR, 0.0002f);
            o->Position[2] += 2.5f * FPS_ANIMATION_FACTOR;
            o->Scale *= powf(0.95f, FPS_ANIMATION_FACTOR);

            //                    Vector(o->Scale,o->Scale,o->Scale,o->Light);
            o->Light[0] *= powf(0.95f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(0.95f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(0.95f, FPS_ANIMATION_FACTOR);
            if (o->Scale <= 0.1f)
            {
                o->LifeTime = -1;
                Particles.Retire(i);
            }
        }
        else if (1 == o->SubType)
        {
            VectorScale(o->Light, powf(0.9f, FPS_ANIMATION_FACTOR), o->Light);
            o->Scale *= powf(0.95f, FPS_ANIMATION_FACTOR);
        }
        else if (5 == o->SubType)
        {
            VectorScale(o->Light, powf(0.9f, FPS_ANIMATION_FACTOR), o->Light);
            o->Scale *= powf(0.95f, FPS_ANIMATION_FACTOR);
        }
        else if (3 == o->SubType)
        {
            VectorScale(o->Light, powf(0.9f, FPS_ANIMATION_FACTOR), o->Light);
            o->Scale *= powf(0.85f, FPS_ANIMATION_FACTOR);
            o->Gravity += (5.f) * FPS_ANIMATION_FACTOR;

            VectorCopy(o->Target->Position, o->Position);

            o->Position[0] += 2.f;
            o->Position[1] -= 2.f;
            o->Position[2] += o->Gravity;
        }
        else if (o->SubType == 4)
        {
            if (o->Target != NULL && o->Target->Type == MODEL_PLAYER &&
                o->Target->Kind == KIND_PLAYER)
            {
                OBJECT *Owner = o->Target;
                BMD *b = &Models[Owner->Type];

                b->TransformPosition(Owner->BoneTransform[(int)o->Rotation], o->Angle, o->Position,
                                     false);

                VectorAdd(o->Position, Owner->Position, o->Position);
            }
            else
            {
                Particles.Retire(i);
            }

            o->Gravity += RandomScalarTravel(o->MotionIntervalFrames, o->ScalarNoiseRate, Random,
                                             FPS_ANIMATION_FACTOR, 40, 60, 0.095f);
            o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 400, 400, 0.0001f);
            o->Position[2] += o->Gravity;

            o->Light[0] *= powf(1.0f / (1.35f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.35f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.35f), FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 9)
        {
            o->Scale -= FPS_ANIMATION_FACTOR * 0.011f;
            if (!o->Live)
                o->LifeTime = 0;
            if (o->Scale <= 0.0f)
                o->LifeTime = 0;

            if (o->LifeTime >= 75)
            {
                o->Light[0] *= powf(1.05f, FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.05f, FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.05f, FPS_ANIMATION_FACTOR);
            }
            else
            {
                o->Light[0] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            }
        }
        else if (o->SubType == 10)
        {
            o->Rotation -= (10) * FPS_ANIMATION_FACTOR;
            if (o->Target != NULL)
            {
                if (o->Target->CurrentAction != PLAYER_SANTA_2)
                {
                    Particles.Retire(i);
                }
            }
        }
        else if (o->SubType == 11)
        {
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Scale -= FPS_ANIMATION_FACTOR * 0.0005f;
        }
        else if (o->SubType == 12)
        {
            const float growing =
                std::clamp(o->LifeTime + FPS_ANIMATION_FACTOR - 81.f, 0.f, FPS_ANIMATION_FACTOR);
            o->Scale += 0.02f * growing - 0.005f * (FPS_ANIMATION_FACTOR - growing);
        }
        else if (o->SubType == 13)
        {
            o->Position[1] -= (o->Gravity) * FPS_ANIMATION_FACTOR;
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Scale -= FPS_ANIMATION_FACTOR * 0.001f;
        }
        else if (o->SubType == 14)
        {
            VectorScale(o->Light, std::pow(0.98f, FPS_ANIMATION_FACTOR), o->Light);
            o->Scale *= powf(0.95f, FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 15)
        {
            o->StartPosition[2] += RandomScalarTravel(o->MotionIntervalFrames, o->ScalarNoiseRate,
                                                      Random, FPS_ANIMATION_FACTOR, 10, 5, 0.01f);
            o->Position[0] = o->StartPosition[0] + sinf(o->StartPosition[2]) * o->Gravity * 2;
            o->Position[1] = o->StartPosition[1] + cosf(o->StartPosition[2]) * o->Gravity * 2;
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Scale -= FPS_ANIMATION_FACTOR * 0.001f;
            const float growing =
                std::clamp(o->LifeTime + FPS_ANIMATION_FACTOR - 21.f, 0.f, FPS_ANIMATION_FACTOR);
            const float fading = FPS_ANIMATION_FACTOR - growing;
            o->Scale = (std::max)(0.f, o->Scale - 0.01f * fading);
            o->Alpha = (std::max)(0.f, (std::min)(1.f, o->Alpha + 0.1f * growing) - 0.1f * fading);
            o->Light[0] = o->TurningForce[0] * o->Alpha;
            o->Light[1] = o->TurningForce[1] * o->Alpha;
            o->Light[2] = o->TurningForce[2] * o->Alpha;
        }
        break;
    case BITMAP_POUNDING_BALL:
        if (o->SubType == 0 && o->SubType == 1)
        {
            o->Gravity += (0.004f) * FPS_ANIMATION_FACTOR;
            o->Scale -= FPS_ANIMATION_FACTOR * 0.02f;

            o->Frame = (23 - o->LifeTime) / 6;
            o->Position[2] += o->Gravity * 10.f * FPS_ANIMATION_FACTOR;

            if (o->SubType == 1)
            {
                o->Light[0] = o->LifeTime / 10.f;
                o->Light[1] = o->Light[0];
                o->Light[2] = o->Light[0];
            }
        }
        else if (o->SubType == 2)
        {
            VectorCopy(o->Target->Angle, o->Angle);

            o->Scale -= FPS_ANIMATION_FACTOR * 0.06f;
            o->Position[2] += o->Gravity * 10.f * FPS_ANIMATION_FACTOR;
            o->Light[0] = o->LifeTime / 10.f;
            o->Light[1] = o->Light[0];
            o->Light[2] = o->Light[0];
        }
        else if (o->SubType == 3)
        {
            AdvanceParticleAlpha(*o, Random, FPS_ANIMATION_FACTOR, 15.f, 0.2f, 2, 2, 0.1f);

            if (o->Alpha < 0.1f)
                Particles.Retire(i);
            Luminosity = o->Alpha;
            Vector(o->TurningForce[0] * Luminosity, o->TurningForce[1] * Luminosity,
                   o->TurningForce[2] * Luminosity, o->Light);
            if (o->Scale > 0)
            {
                o->Scale -= ParticleScaleTravel(*o, Random, FPS_ANIMATION_FACTOR, 3, 5, 0.01f);
            }
            else
            {
                Particles.Retire(i);
            }
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (3.0f) * FPS_ANIMATION_FACTOR;
        }
        break;
    case BITMAP_ADV_SMOKE:
        AdvanceDampedParticleMotion(*o, FPS_ANIMATION_FACTOR);
        o->Light[0] = (float)(o->LifeTime) / 10.f;
        o->Light[1] = o->Light[0];
        o->Light[2] = o->Light[0];
        if (o->SubType == 0)
        {
            o->Scale += FPS_ANIMATION_FACTOR * 0.07f;
        }
        else if (o->SubType == 2)
        {
            o->Scale += FPS_ANIMATION_FACTOR * 0.07f;
        }
        else if (o->SubType == 3)
        {
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.2f;
        }

        else
        {
            o->Scale += FPS_ANIMATION_FACTOR * 0.09f;
        }
        break;
    case BITMAP_ADV_SMOKE + 1:
        AdvanceDampedParticleMotion(*o, FPS_ANIMATION_FACTOR);
        o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
        if (o->SubType == 2)
        {
            o->Light[0] = (float)(o->LifeTime) / 25.f * 2 - 1.f;
            o->Light[1] = o->Light[0];
            o->Light[2] = o->Light[0];
        }
        else
        {
            o->Light[0] = (float)(o->LifeTime) / 25.f;
            o->Light[1] = o->Light[0];
            o->Light[2] = o->Light[0];
        }
        AdvanceSmokeDrift(*o, Random, FPS_ANIMATION_FACTOR);
        break;

    case BITMAP_TRUE_FIRE:
    case BITMAP_TRUE_BLUE:
        AdvanceDampedParticleMotion(*o, FPS_ANIMATION_FACTOR);
        if ((o->SubType == 1 || o->SubType == 2) && o->Target != NULL)
        {
            if (o->Target->CurrentAction == 1)
                o->LifeTime -= 2 * FPS_ANIMATION_FACTOR;
        }

        if (o->SocketBinding && !AdvanceNamedSocketParticle(Particles, *o, FPS_ANIMATION_FACTOR))
            break;

        if (o->SubType == 8)
        {
            vec3_t vPos, vRelativePos;
            if (o->Target != NULL)
            {
                BMD *pModel = &Models[o->Target->Type];
                VectorCopy(o->Target->Position, pModel->BodyOrigin);
                Vector(6.f, 6.f, 0.f, vRelativePos);
                pModel->TransformPosition(o->Target->BoneTransform[20], vRelativePos, vPos, true);
            }

            o->Position[0] = vPos[0];
            o->Position[1] = vPos[1];
        }

        if (o->SubType == 7)
        {
            o->Scale -= FPS_ANIMATION_FACTOR * 0.1f;
            o->Position[2] += (2.0f) * FPS_ANIMATION_FACTOR;
        }
        else
            o->Scale -= FPS_ANIMATION_FACTOR * 0.02f;

        if (o->Scale < 0)
            o->Scale = 0.f;

        if (o->Type == 6)
            o->Position[2] += (2.0f) * FPS_ANIMATION_FACTOR;
        else
            o->Position[2] += (1.f) * FPS_ANIMATION_FACTOR;

        o->Light[0] = (float)(o->LifeTime) / 25.f;
        o->Light[1] = o->Light[0];
        o->Light[2] = o->Light[0];
        break;

    case BITMAP_HOLE:
        o->Rotation += (5.f) * FPS_ANIMATION_FACTOR;
        if (o->LifeTime < 20)
        {
            o->Scale -= FPS_ANIMATION_FACTOR * 0.08f;
            if (o->LifeTime < 10)
            {
                o->Light[0] *= powf(1.0f / (1.3f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.0f / (1.3f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.0f / (1.3f), FPS_ANIMATION_FACTOR);
            }
        }
        else if (o->LifeTime < 30)
        {
            o->Light[0] *= powf(1.1f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.1f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.1f, FPS_ANIMATION_FACTOR);
        }
        break;
    case BITMAP_WATERFALL_1:
        o->Scale -= FPS_ANIMATION_FACTOR * 0.005f;
        if (o->LifeTime < 5)
        {
            o->Light[0] *= powf(1.0f / (1.2f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.2f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.2f), FPS_ANIMATION_FACTOR);
        }
        else if (o->LifeTime > 20)
        {
            o->Light[0] *= powf(1.1f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.1f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.1f, FPS_ANIMATION_FACTOR);
        }

        if (o->SubType == 1)
        {
            if (o->Light[0] > 0.5f)
            {
                Vector(0.5f, 0.5f, 0.5f, o->Light);
            }
            o->Rotation += FPS_ANIMATION_FACTOR;
            o->Scale += FPS_ANIMATION_FACTOR * 0.01f;
        }
        break;

    case BITMAP_WATERFALL_5:
        if (o->SubType == 0)
        {
            o->Scale -= FPS_ANIMATION_FACTOR * 0.005f;
        }
        else if (o->SubType == 1)
        {
            o->Scale += FPS_ANIMATION_FACTOR * 0.1f;
            AdvanceHorizontalDrift(*o, Random, 10, 5, FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 2)
        {
            if (o->Scale < 1.0f)
                o->Scale += FPS_ANIMATION_FACTOR * 0.1f;
            AdvanceHorizontalDrift(*o, Random, 10, 5, FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 3)
        {
            o->Scale -= FPS_ANIMATION_FACTOR * 0.005f;
            o->Rotation += (4.f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 4)
        {
            o->Position[0] += ((float)(cos(o->Angle[2]) * 20.0f)) * FPS_ANIMATION_FACTOR;
            o->Position[1] += ((float)(sin(o->Angle[2]) * 20.0f)) * FPS_ANIMATION_FACTOR;
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (1) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 5)
        {
            o->Scale -= FPS_ANIMATION_FACTOR * 0.005f;
        }
        else if (o->SubType == 7)
        {
            o->Scale -= FPS_ANIMATION_FACTOR * 0.005f;
            o->Rotation += (1) * FPS_ANIMATION_FACTOR;
            o->Position[0] += (o->Velocity[0]) * FPS_ANIMATION_FACTOR;
            o->Position[1] += (o->Velocity[1]) * FPS_ANIMATION_FACTOR;

            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 8)
        {
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;

            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            break;
        }
        else if (o->SubType == 9)
        {
            o->Scale -= FPS_ANIMATION_FACTOR * 0.005f;
        }

        if (o->LifeTime < 8)
        {
            o->Light[0] *= powf(1.0f / (1.2f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.2f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.2f), FPS_ANIMATION_FACTOR);
        }
        else if (o->LifeTime > 20)
        {
            o->Light[0] *= powf(1.1f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.1f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.1f, FPS_ANIMATION_FACTOR);
        }
        break;

    case BITMAP_PLUS:
        o->Scale -= FPS_ANIMATION_FACTOR * 0.01f;
        AdvanceHorizontalDrift(*o, Random, 2, 1, FPS_ANIMATION_FACTOR);
        o->Position[2] += (2.f) * FPS_ANIMATION_FACTOR;

        o->Light[0] = o->LifeTime / 20.f;
        o->Light[1] = o->LifeTime / 20.f;
        o->Light[2] = o->LifeTime / 20.f;
        break;

    case BITMAP_WATERFALL_2:
        if (o->SubType == 6)
            break; // Rising motion also owns size and light.
        if (o->SubType == 5)
        {
            o->Scale += FPS_ANIMATION_FACTOR * 0.03f;

            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            break;
        }

        o->Scale += FPS_ANIMATION_FACTOR * 0.03f;
        if (o->LifeTime < 10)
        {
            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
        }

        if (o->SubType == 1)
        {
            o->Light[0] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
        }
        if (o->SubType == 3)
        {
            o->Scale -= FPS_ANIMATION_FACTOR * 0.038f;
            o->Light[0] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
        }
        if (o->SubType == 4)
        {
            o->Rotation -= (1.1f) * FPS_ANIMATION_FACTOR;
        }
        break;

    case BITMAP_WATERFALL_3:
    case BITMAP_WATERFALL_4:
        if (o->SubType == 2)
        {
            o->Position[0] += ((float)(cos(o->Angle[2]) * 20.0f)) * FPS_ANIMATION_FACTOR;
            o->Position[1] += ((float)(sin(o->Angle[2]) * 20.0f)) * FPS_ANIMATION_FACTOR;
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
            o->Rotation += (1) * FPS_ANIMATION_FACTOR;
            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            break;
        }
        else if (o->SubType == 3)
        {
            o->Scale += FPS_ANIMATION_FACTOR * 0.005f;
            o->Light[0] *= powf(0.97f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(0.97f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(0.97f, FPS_ANIMATION_FACTOR);
            break;
        }
        else if (o->SubType == 5)
        {
            AdvanceParticleGravity(*o, -1.f, -0.05f, FPS_ANIMATION_FACTOR);
            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            break;
        }
        else if (o->SubType == 6)
        {
            AdvanceParticleGravity(*o, 1.f, 0.05f, FPS_ANIMATION_FACTOR);

            o->Alpha -= FPS_ANIMATION_FACTOR * 0.01f;

            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            break;
        }
        else if (o->SubType == 7)
        {
            o->Light[0] *= powf(0.97f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(0.97f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(0.97f, FPS_ANIMATION_FACTOR);
            break;
        }
        else if (o->SubType == 8)
        {
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            break;
        }
        else if (o->SubType == 10)
        {
            o->Position[0] += (o->Velocity[0]) * FPS_ANIMATION_FACTOR;
            o->Position[1] += (o->Velocity[1]) * FPS_ANIMATION_FACTOR;
            AdvanceParticleGravity(*o, -1.f, -0.05f, FPS_ANIMATION_FACTOR);
            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            break;
        }
        else if (o->SubType == 11)
        {
            VectorAddScaled(o->Position, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.01f;
            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            break;
        }
        else if (o->SubType == 12)
        {
            AdvanceWaterfallRotation(*o, FPS_ANIMATION_FACTOR, WorldTime,
                                     1000.0 /
                                         sessionKeeper_.ApplicationConfig().legacyReferenceFps);
            o->Scale *= powf(0.92f, FPS_ANIMATION_FACTOR);
            o->Light[0] *= powf(0.92f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(0.92f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(0.92f, FPS_ANIMATION_FACTOR);
            o->Alpha *= powf(0.8f, FPS_ANIMATION_FACTOR);

            break;
        }
        else if (o->SubType == 13)
        {
            o->Position[2] -= (o->Gravity) * FPS_ANIMATION_FACTOR;
            o->Scale += FPS_ANIMATION_FACTOR * 0.001f;
            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);

            break;
        }
        else if (o->SubType == 14)
        {
            o->Scale += FPS_ANIMATION_FACTOR * 0.05f;
            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 15)
        {
            AdvanceParticleGravity(*o, -1.f, -0.05f, FPS_ANIMATION_FACTOR);
            o->Scale -= FPS_ANIMATION_FACTOR * 0.05f;

            o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            break;
        }
#ifdef ASG_ADD_MAP_KARUTAN
        else if (o->SubType == 16)
        {
            o->Scale -= FPS_ANIMATION_FACTOR * 0.005f;
            if (o->LifeTime < 8)
            {
                o->Light[0] *= powf(1.0f / (1.2f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.0f / (1.2f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.0f / (1.2f), FPS_ANIMATION_FACTOR);
            }
            else if (o->LifeTime > 20)
            {
                o->Light[0] *= powf(1.1f, FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.1f, FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.1f, FPS_ANIMATION_FACTOR);
            }
            break;
        }
#endif // ASG_ADD_MAP_KARUTAN
        o->Scale += FPS_ANIMATION_FACTOR * 0.005f;
        o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
        o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
        o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
        break;

    case BITMAP_SHOCK_WAVE: {
        if (o->SubType == 3)
        {
            o->Light[0] *= powf(1.0f / (1.8f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.8f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.8f), FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.3f;
        }
        else if (o->SubType == 0)
        {
            o->Light[0] *= powf(1.0f / (1.5f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.5f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.5f), FPS_ANIMATION_FACTOR);
            o->Scale += FPS_ANIMATION_FACTOR * 0.8f;
        }
        if (o->SubType == 4)
        {
            o->Alpha -= FPS_ANIMATION_FACTOR * 0.001f;
            if (o->Alpha <= 0.0f)
                Particles.Retire(i);
            o->Light[0] *= powf(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= powf(1.0f / (1.03f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= powf(1.0f / (1.03f), FPS_ANIMATION_FACTOR);
            float growth = 0.f;
            Core::Time::Advance(growth, o->Gravity, 2.4f, FPS_ANIMATION_FACTOR);
            o->Scale += growth * 0.015f;
        }
    }
    break;
    case BITMAP_CURSEDTEMPLE_EFFECT_MASKER: {
        o->Light[0] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
        o->Light[1] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
        o->Light[2] *= powf(1.0f / (1.1f), FPS_ANIMATION_FACTOR);

        o->Scale += FPS_ANIMATION_FACTOR * 0.02f;

        if (o->SubType == 0)
        {
            vec3_t Light;
            Vector(0.8f, 0.3f, 0.3f, Light);
            for (auto birthTime : Emissions(FPS_ANIMATION_FACTOR / 2.f))
                CreateParticle(BITMAP_CURSEDTEMPLE_EFFECT_MASKER, o->StartPosition, o->Angle, Light,
                               1, 1.3f);
        }
    }
    break;
    case BITMAP_RAKLION_CLOUDS:
        AdvanceRaklionCloud(*o, i);
        break;
    case BITMAP_CHROME2: {
        if (o->Scale > 0)
            o->Scale -= FPS_ANIMATION_FACTOR * 0.1f;
        else
            o->Scale = 0;

        o->Rotation += (1.0f) * FPS_ANIMATION_FACTOR;
        o->Alpha -= FPS_ANIMATION_FACTOR * 0.05f;
        if (o->Target != NULL)
        {
            o->Position[0] = o->Target->Position[0] - o->StartPosition[0];
            o->Position[1] = o->Target->Position[1] - o->StartPosition[1];
            o->Position[2] = o->Target->Position[2] - o->StartPosition[2];
        }
    }
    break;
    case BITMAP_AG_ADDITION_EFFECT:
        AdvanceAgAddition(*o, i);
        break;
    case BITMAP_SBUMB: {
        o->Frame = 4 - o->LifeTime;
    }
    break;
    case BITMAP_DAMAGE1: {
        o->Scale *= powf(1.2f, FPS_ANIMATION_FACTOR);
        VectorScale(o->Light, std::pow(0.8f, FPS_ANIMATION_FACTOR), o->Light);
    }
    break;
    case BITMAP_SWORD_EFFECT_MONO: {
        if (o->SubType == 1)
        {
            o->Scale *= powf(0.8f, FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 0)
        {
            o->Scale *= powf(1.2f, FPS_ANIMATION_FACTOR);
        }
        if (o->Scale > 6.0f)
        {
            o->SubType = 1;
            break;
        }
        else if (o->SubType == 1)
        {
            if (o->Scale < 1)
            {
                Particles.Retire(i);
                break;
            }
        }

        VectorScale(o->Light, std::pow(0.94f, FPS_ANIMATION_FACTOR), o->Light);
    }
    break;
    case BITMAP_DAMAGE2: {
        o->Scale *= powf(1.2f, FPS_ANIMATION_FACTOR);
        VectorScale(o->Light, std::pow(0.55f, FPS_ANIMATION_FACTOR), o->Light);
    }
    break;
    }
    if (o->BirthTiming.serial != birthSerial)
        return;
    if (expired)
        Particles.Retire(i);
    if (!o->Live)
    {
        o->Target = nullptr;
        o->SocketBinding.reset();
    }
}

void SessionLegacyCalls::MoveParticles()
{
    sessionKeeper_.Gameplay()->MoveParticles();
}

int SessionLegacyCalls::CreateParticleFpsChecked(int type, vec3_t position, vec3_t angle,
                                                 vec3_t light, int subType, float scale,
                                                 OBJECT *owner)
{
    return sessionKeeper_.Gameplay()->CreateParticleFpsChecked(type, position, angle, light,
                                                               subType, scale, owner);
}
int SessionLegacyCalls::CreateParticle(int type, vec3_t position, vec3_t angle, vec3_t light,
                                       int subType, float scale, OBJECT *owner)
{
    return sessionKeeper_.Gameplay()->CreateParticle(type, position, angle, light, subType, scale,
                                                     owner);
}

void SessionGameplayUnit::CreatePoint(vec3_t Position, int Value, vec3_t Color, float scale,
                                      bool bMove, bool bRepeatedly)
{
    if (!g_pOption->GetRenderAllEffects())
    {
        return;
    }

    const int i = Points.Allocate();
    PARTICLE *o = &Points[i];
    RegisterEffectBirth(o->BirthTiming, o, i, BirthParticlePool::Points);
    o->Target = nullptr;
    o->SocketBinding.reset();
    o->Type = Value;
    VectorCopy(Position, o->Position);
    o->Position[2] += 140.f;
    VectorCopy(Color, o->Angle);
    o->bRepeatedly = bRepeatedly;
    o->fRepeatedlyHeight = RequestTerrainHeight(o->Position[0], o->Position[1]) + 140.0f;
    o->Gravity = 10.f;
    o->Scale = scale;
    o->LifeTime = 0;
    o->bEnableMove = bMove;
    return;
}

void SessionLegacyCalls::CreatePoint(vec3_t position, int value, vec3_t color, float scale,
                                     bool move, bool repeatedly)
{
    sessionKeeper_.Gameplay()->CreatePoint(position, value, color, scale, move, repeatedly);
}

void SessionGameplayUnit::MovePointEntry(PARTICLE *o, int index)
{
    EffectBirthStep birthStep(FPS_ANIMATION_FACTOR, o->BirthTiming);
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    o->LifeTime -= FPS_ANIMATION_FACTOR;
    if (o->LifeTime < 0)
    {
        if (o->bRepeatedly && o->Position[2] > o->fRepeatedlyHeight)
        {
            o->Gravity = 10.0f;
            o->bRepeatedly = false;
        }
        if (o->bEnableMove)
        {
            o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
        }
        o->Gravity -= 0.3f * FPS_ANIMATION_FACTOR;
        if (o->Gravity <= 0.f)
            Points.Retire(index);
        if (o->Type != -2)
        {
            o->Scale -= 5.f * FPS_ANIMATION_FACTOR; //20.f;
            if (o->Scale < 15.f)
                o->Scale = 15.f;
        }
    }
}

void SessionGameplayUnit::MovePoints()
{
    if (!g_pOption->GetRenderAllEffects())
        return;
    for (auto cursor = Points.begin(), end = Points.end(); cursor != end; ++cursor)
        MovePointEntry(&*cursor, cursor.Index());
}

void SessionLegacyCalls::MovePoints()
{
    sessionKeeper_.Gameplay()->MovePoints();
}

void SessionGameplayUnit::CreatePointer(int Type, vec3_t Position, float Angle, vec3_t Light,
                                        float Scale)
{
    const int index = Pointers.Allocate();
    auto &pointer = Pointers[index];
    RegisterEffectBirth(pointer.BirthTiming, &pointer, index, BirthParticlePool::Pointers);
    pointer.Target = nullptr;
    pointer.SocketBinding.reset();
    pointer.Type = Type;
    VectorCopy(Position, pointer.Position);
    VectorCopy(Light, pointer.Light);
    pointer.Angle[2] = Angle;
    pointer.Alpha = 1.f;
    pointer.Scale = Scale;
    switch (Type)
    {
    case BITMAP_BLOOD:
    case BITMAP_BLOOD + 1:
    case BITMAP_FOOT:
        pointer.LifeTime = 50 + Random.RangeInt(0, 31);
        break;
    }
}

void SessionLegacyCalls::CreatePointer(int type, vec3_t position, float angle, vec3_t light,
                                       float scale)
{
    sessionKeeper_.Gameplay()->CreatePointer(type, position, angle, light, scale);
}

void SessionGameplayUnit::MovePointerEntry(PARTICLE *o, int index)
{
    EffectBirthStep birthStep(FPS_ANIMATION_FACTOR, o->BirthTiming);
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    o->LifeTime -= FPS_ANIMATION_FACTOR;
    switch (o->Type)
    {
    case BITMAP_CURSOR + 5:
        o->Scale -= 0.05f * FPS_ANIMATION_FACTOR;
        if (o->Scale < 0.1f)
            Pointers.Retire(index);
        break;
    case BITMAP_BLOOD:
    case BITMAP_BLOOD + 1:
    case BITMAP_FOOT:
        if (o->Type == BITMAP_BLOOD)
            o->Scale += 0.004f * FPS_ANIMATION_FACTOR;
        Vector(0.1f, 0.f, 0.f, o->Light);
        if (o->LifeTime <= 0.f)
            Pointers.Retire(index);
        if (o->LifeTime < 50.f)
            o->Alpha = o->LifeTime * 0.02f;
        break;
    }
}

void SessionGameplayUnit::MovePointers()
{
    for (auto cursor = Pointers.begin(), end = Pointers.end(); cursor != end; ++cursor)
        MovePointerEntry(&*cursor, cursor.Index());
}

void SessionLegacyCalls::MovePointers()
{
    sessionKeeper_.Gameplay()->MovePointers();
}

void SessionGameplayUnit::EmitSwellMagicBirths(OBJECT &effect)
{
    constexpr float BurstLives[] = {45.f, 35.f, 25.f};
    for (float life : BurstLives)
    {
        if (!Core::Time::Reaches(effect.LifeTime, FPS_ANIMATION_FACTOR, life))
            continue;
        auto birth = EmissionTime(FPS_ANIMATION_FACTOR - std::max(0.f, effect.LifeTime - life));
        vec3_t position, light{0.4f, 0.3f, 0.9f};
        effect.Owner->MotionTrace.Sample(WorldTime, birth.FrameFraction(), effect.Owner->Position,
                                         position);
        for (int i = 0; i < 2; ++i)
            CreateEffect(BITMAP_SHOCK_WAVE, position, effect.Angle, light, 14, effect.Owner, -1, 0,
                         0, 0, 5.f);
        CreateEffect(BITMAP_TWLIGHT, position, effect.Angle, light, 3, effect.Owner, -1, 0, 0, 0,
                     6.f);
        if (life != BurstLives[0])
            continue;
        EmitSwellHandPair(effect, birth);
    }
    constexpr float GhostEndLife = 29.f; // Include the authored life-30 sample.
    const float active =
        std::min(FPS_ANIMATION_FACTOR, std::max(0.f, effect.LifeTime - GhostEndLife));
    for (auto birth : Emissions(active, active))
    {
        vec3_t position, light{0.3f, 0.2f, 0.9f};
        effect.Owner->MotionTrace.Sample(WorldTime, birth.FrameFraction(), effect.Owner->Position,
                                         position);
        for (int i = 0; i < 2; ++i)
            CreateJoint(BITMAP_2LINE_GHOST, position, position, effect.Angle, 1, effect.Owner,
                        20.f + WorldRandom() % 10, -1, 0, 0, -1, light);
    }
}

void SessionGameplayUnit::EmitSwellHandPair(OBJECT &effect, const EffectEmissionScope &birth)
{
    BMD &model = Models[effect.Owner->Type];
    AnimationPoseSample pose(effect.Owner, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    for (int bone : {28, 37})
    {
        vec3_t offset{}, position, light{0.2f, 0.2f, 0.9f};
        pose.SampleBonePosition(model, *effect.Owner, bone, offset, WorldTime,
                                birth.FrameFraction(), position);
        CreateEffect(MODEL_ARROWSRE06, position, effect.Angle, light, 1, effect.Owner, bone);
    }
}

void SessionGameplayUnit::CreateBonfire(vec3_t Position, vec3_t Angle)
{
    Position[0] += Random.RangeFloat(-8, 7);
    Position[1] += Random.RangeFloat(-8, 7);
    Position[2] += Random.RangeFloat(-8, 7);
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    CreateParticle(BITMAP_FIRE, Position, Angle, Light, Random.RangeInt(0, 3));
    if (Random.FpsCheck(4, 1.f))
    {
        CreateParticle(BITMAP_SPARK, Position, Angle, Light);
        vec3_t a;
        Vector(-Random.RangeFloat(30, 89), 0.f, Random.RangeFloat(0.f, 360.f), a);
        CreateJoint(BITMAP_JOINT_SPARK, Position, Position, a);
    }
    const float Luminosity = Random.RangeFloat(6, 11) * 0.1f;
    Vector(Luminosity, Luminosity * 0.6f, Luminosity * 0.4f, Light);
    AddTerrainLight(Position[0], Position[1], Light, 4, PrimaryTerrainLight);
}

namespace
{
void SampleFirePosition(const OBJECT &object, const vec3_t offset, double time, float fraction,
                        vec3_t position)
{
    vec3_t angle, origin;
    VectorCopy(object.Angle, angle);
    angle[2] = object.MotionTrace.SampleYaw(time, fraction, angle[2]);
    object.MotionTrace.Sample(time, fraction, object.Position, origin);
    float matrix[3][4];
    AngleMatrix(angle, matrix);
    VectorRotate(offset, matrix, position);
    VectorAdd(position, origin, position);
}
} // namespace

void SessionGameplayUnit::CreateFire(int Type, OBJECT *o, float x, float y, float z)
{
    vec3_t offset{x, y, z}, position, light;
    for (auto birth : Emissions(FPS_ANIMATION_FACTOR / 2.f))
    {
        const float fraction = birth.FrameFraction();
        SampleFirePosition(*o, offset, WorldTime, fraction, position);
        for (int axis = 0; axis < 3; ++axis)
            position[axis] += Random.RangeFloat(-8, 7);
        switch (Type)
        {
        case 0: {
            const float luminosity = Random.RangeFloat(6, 11) * 0.1f;
            Vector(luminosity, luminosity * 0.6f, luminosity * 0.4f, light);
            CreateParticle(BITMAP_FIRE, position, o->Angle, light, Random.RangeInt(0, 3));
            break;
        }
        case 1:
            CreateParticle(BITMAP_SMOKE, position, o->Angle, o->Light);
            break;
        case 2:
            CreateParticle(BITMAP_SMOKE, position, o->Angle, o->Light, 2);
            break;
        }
    }
    if (Type != 0)
        return;
    SampleFirePosition(*o, offset, WorldTime, 1.f, position);
    const float luminosity = Random.RangeFloat(6, 11) * 0.1f;
    Vector(luminosity, luminosity * 0.6f, luminosity * 0.4f, light);
    AddTerrainLight(position[0], position[1], light, 4, PrimaryTerrainLight);
}

void SessionGameplayUnit::CheckSkull(OBJECT *o)
{
    vec3_t Position;
    VectorCopy(Hero->Object.Position, Position);
    if (Hero->Object.CurrentAction >= PLAYER_WALK_MALE &&
            Hero->Object.CurrentAction <= PLAYER_RUN_RIDE_WEAPON ||
        (Hero->Object.CurrentAction == PLAYER_RAGE_UNI_RUN ||
         Hero->Object.CurrentAction == PLAYER_RAGE_UNI_RUN_ONE_RIGHT))
    {
        if (o->Direction[0] < 0.1f)
        {
            float dx = Position[0] - o->Position[0];
            float dy = Position[1] - o->Position[1];
            float Distance = std::hypot(dx, dy);
            if (Distance < 50.f)
            {
                Vector(-dx * 0.4f, -dy * 0.4f, 0.f, o->Direction);
                o->HeadAngle[1] = -dx * 4.f;
                o->HeadAngle[0] = -dy * 4.f;
                PlayBuffer(SOUND_BONE2, o);
            }
        }
    }
    MoveDampedObject(o, 0.6f, FPS_ANIMATION_FACTOR);
}

void SessionGameplayUnit::MoveAirLeaf(PARTICLE *o, bool rotate, bool discreteNoise)
{
    const auto noise = [&](int low, int high) {
        return discreteNoise ? float(Random.RangeInt(low, high)) : Random.RangeFloat(low, high);
    };
    float remaining = FPS_ANIMATION_FACTOR;
    while (remaining > 0.f)
    {
        if (o->MotionIntervalFrames <= 0.f)
        {
            // Refresh random force once per reference frame; keep moving between refreshes.
            for (int axis = 0; axis < 3; ++axis)
                o->Velocity[axis] += noise(-8, 7) * 0.1f;
            if (rotate)
            {
                o->TurningForce[0] += noise(-4, 3) * 0.02f;
                o->TurningForce[1] += noise(-8, 7) * 0.02f;
                o->TurningForce[2] += noise(-4, 3) * 0.02f;
            }
            o->MotionIntervalFrames = 1.f;
        }
        const float frames = (std::min)(remaining, o->MotionIntervalFrames);
        VectorAddScaled(o->Position, o->Velocity, o->Position, frames);
        if (rotate)
            VectorAddScaled(o->Angle, o->TurningForce, o->Angle, frames);
        o->MotionIntervalFrames -= frames;
        remaining -= frames;
    }
}

void SessionGameplayUnit::MoveEtcLeaf(PARTICLE *o)
{
    float Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
    if (o->Position[2] <= Height)
    {
        o->Position[2] = Height;
        o->Light[0] -= 0.05f * FPS_ANIMATION_FACTOR;
        o->Light[1] -= 0.05f * FPS_ANIMATION_FACTOR;
        o->Light[2] -= 0.05f * FPS_ANIMATION_FACTOR;
        if (o->Light[0] <= 0.f)
            o->Live = false;
    }
    else
    {
        MoveAirLeaf(o, false, false);
    }
}

namespace
{
void PrepareRainBirthMotion(double worldTime, int &speed, int &angle)
{
    constexpr float SpeedVariation = 10.f, BaseSpeed = 30.f, AngleVariation = 20.f;
    speed = static_cast<int>(sinf(worldTime * 0.001f)) * SpeedVariation + BaseSpeed;
    angle = static_cast<int>(sinf(worldTime * 0.0005f + 50.f)) * AngleVariation;
}
} // namespace

bool SessionGameplayUnit::MoveLeaves()
{
    if (!g_pOption->GetRenderAllEffects())
    {
        return false;
    }

    if (FPS_ANIMATION_FACTOR <= 0.f)
        return true;

    const int iMaxLeaves = TheMapProcess().PrepareWeather();

    RainCurrent +=
        std::clamp(RainTarget - RainCurrent, -FPS_ANIMATION_FACTOR, FPS_ANIMATION_FACTOR);

    PrepareRainBirthMotion(WorldTime, RainSpeed, RainAngle);
    constexpr float RainPhaseSpeed = 20.f, RainPhasePeriod = 2000.f;
    RainPosition = std::fmod(RainPosition + RainPhaseSpeed * FPS_ANIMATION_FACTOR, RainPhasePeriod);

    for (int i = 0; i < iMaxLeaves; i++)
    {
        PARTICLE *o = &Leaves[i];
        if (!o->Live)
        {
            Vector(1.f, 1.f, 1.f, o->Light);
            o->Live = true;
            o->MotionIntervalFrames = 0.f;

            if (!TheMapProcess().CreateWeather(o, i))
                o->Live = false;
        }
        else
        {
            if (TheMapProcess().MoveWeather(o))
                continue;
            MoveEtcLeaf(o);
        }
    }
    return true;
}

void SessionLegacyCalls::CheckSkull(OBJECT *object)
{
    sessionKeeper_.Gameplay()->CheckSkull(object);
}

void SessionLegacyCalls::MoveEtcLeaf(PARTICLE *particle)
{
    sessionKeeper_.Gameplay()->MoveEtcLeaf(particle);
}

bool SessionLegacyCalls::MoveLeaves()
{
    return sessionKeeper_.Gameplay()->MoveLeaves();
}

void SessionLegacyCalls::CreateBonfire(vec_t *position, vec_t *angle)
{
    sessionKeeper_.Gameplay()->CreateBonfire(position, angle);
}

void SessionLegacyCalls::CreateFire(int type, OBJECT *object, float x, float y, float z)
{
    sessionKeeper_.Gameplay()->CreateFire(type, object, x, y, z);
}

void SessionLegacyCalls::MoveAirLeaf(PARTICLE *particle, bool rotate, bool discreteNoise)
{
    sessionKeeper_.Gameplay()->MoveAirLeaf(particle, rotate, discreteNoise);
}

void SessionGameplayUnit::EmitWingItemParticles(OBJECT &owner, const ObjectDrawInput &draw,
                                                int type, const vec34_t *preparedBones,
                                                const CharacterLinkedItemVisual *linkedItem,
                                                const CharacterDrawInput *parentDraw)
{
    const bool dark = draw.type == MODEL_WINGS_OF_DARKNESS;
    if (!dark && type != MODEL_WING_OF_STORM && type != MODEL_WING_OF_ILLUSION &&
        type != MODEL_WING_OF_RUIN)
        return;
    const double rate = dark || type == MODEL_WING_OF_RUIN ? 1.0 : 0.5;
    for (auto birth : Emissions(FPS_ANIMATION_FACTOR * rate))
    {
        std::optional<ItemBirthPose> pose;
        if (linkedItem)
            pose.emplace(sessionKeeper_, *sessionKeeper_.Visual(), *parentDraw, *linkedItem, birth);
        else
            pose.emplace(sessionKeeper_, draw, type, preparedBones, birth);
        const auto *bones = pose->Bones();
        const auto sampledDraw = pose->Draw();
        const double time = WorldTime - birth.SceneRemainingFrames() * 1000.0 /
                                            sessionKeeper_.ApplicationConfig().legacyReferenceFps;
        if (dark)
            EmitDarkWingLinks(owner, sampledDraw, bones, time);
        else if (type == MODEL_WING_OF_STORM)
            EmitStormWingThunder(owner, sampledDraw, bones);
        else if (type == MODEL_WING_OF_ILLUSION)
            EmitIllusionWingShiny(sampledDraw, bones, time);
        else
            EmitRuinWingChrome(sampledDraw, bones);
    }
}

void SessionGameplayUnit::EmitDarkWingLinks(OBJECT &owner, const ObjectDrawInput &draw,
                                            const vec34_t *bones, double time)
{
    auto &model = Models[MODEL_WINGS_OF_DARKNESS];
    const float scale = 23.f + std::sin(time * 0.004) * 3.f;
    vec3_t offset{}, angle;
    VectorCopy(draw.angle, angle);
    for (int side = 0; side < 2; ++side)
        for (int index = 0; index < 5; ++index)
        {
            vec3_t first, second;
            model.TransformPosition(bones[(side == 0 ? 22 : 7) - index], offset, first, true);
            model.TransformPosition(bones[side == 0 ? 30 - index : 11 + index], offset, second,
                                    true);
            CreateJoint(BITMAP_JOINT_THUNDER, second, first, angle, 14, &owner, scale);
            CreateJoint(BITMAP_JOINT_SPIRIT, first, second, angle, 4, &owner, scale + 5.f);
        }
}

void SessionGameplayUnit::EmitStormWingThunder(OBJECT &owner, const ObjectDrawInput &draw,
                                               const vec34_t *bones)
{
    constexpr int sockets[] = {11, 21, 29, 63, 81, 89};
    auto &model = Models[MODEL_WING_OF_STORM];
    for (int bone : sockets)
    {
        if (WorldRandom() % 20 != 0)
            continue;
        vec3_t offset{}, position, light{0.6f, 0.6f, 0.9f}, angle;
        model.TransformPosition(bones[bone], offset, position, true);
        VectorCopy(draw.angle, angle);
        CreateEffect(MODEL_FENRIR_THUNDER, position, angle, light, 1, &owner);
    }
}

void SessionGameplayUnit::EmitIllusionWingShiny(const ObjectDrawInput &draw, const vec34_t *bones,
                                                double time)
{
    auto &model = Models[MODEL_WING_OF_ILLUSION];
    const float pulse = (std::sin(time * 0.004) + 1.f) * 0.05f;
    vec3_t offset{}, position, angle, light{0.8f + pulse, 0.8f + pulse, 0.3f + pulse};
    VectorCopy(draw.angle, angle);
    for (int bone : {13, 31})
    {
        model.TransformPosition(bones[bone], offset, position, true);
        CreateParticle(BITMAP_SHINY, position, angle, light, 5, 0.5f);
    }
}

void SessionGameplayUnit::EmitRuinWingChrome(const ObjectDrawInput &draw, const vec34_t *bones)
{
    constexpr int sockets[] = {7,  16, 25, 57, 48, 39, 11, 22, 31, 63, 54, 40, 10,
                               21, 30, 62, 53, 41, 9,  20, 29, 61, 52, 42, 8,  19,
                               28, 60, 51, 43, 18, 27, 59, 50, 17, 26, 58, 49};
    auto &model = Models[MODEL_WING_OF_RUIN];
    vec3_t offset{}, position, angle, light{0.6f, 0.4f, 0.7f};
    VectorCopy(draw.angle, angle);
    for (int index = 0; index < 38; ++index)
    {
        model.TransformPosition(bones[sockets[index]], offset, position, true);
        const float scale = index < 6 ? 0.1f : index < 18 ? 0.3f : index < 30 ? 0.5f : 0.7f;
        CreateParticle(BITMAP_CHROME_ENERGY2, position, angle, light, 0, scale);
    }
}

void SessionVisualUnit::EmitAlternatingSmoke(OBJECT &object, AlternatingSmokeStyle style)
{
    constexpr float HalfPeriod = 2.f, Period = 4.f;
    float remaining = FPS_ANIMATION_FACTOR;
    while (remaining > 0.f)
    {
        const float boundary = object.Timer < HalfPeriod ? HalfPeriod : Period;
        float untilBoundary = boundary - object.Timer;
        const float step = Core::Time::ReferenceStep(remaining, untilBoundary);
        object.Timer += step;
        remaining -= step;
        if (step != untilBoundary)
            continue;
        object.Timer = boundary == Period ? 0.f : HalfPeriod;
        if (!sessionKeeper_.Random()->FpsCheck(2, 1.f))
            continue;
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(remaining);
        vec3_t light{1.f, 1.f, 1.f};
        if (style == AlternatingSmokeStyle::Barracks)
        {
            if (boundary == HalfPeriod)
            {
                if (sessionKeeper_.Random()->FpsCheck(2, 1.f))
                {
                    Vector(0.f, 0.f, 0.f, light);
                    CreateParticle(BITMAP_ADV_SMOKE + 1, object.Position, object.Angle, light, 1,
                                   object.Scale);
                }
                Vector(1.f, 0.4f, 0.4f, light);
                CreateParticle(BITMAP_ADV_SMOKE, object.Position, object.Angle, light, 2,
                               object.Scale);
            }
            else
            {
                Vector(1.f, 0.4f, 0.4f, light);
                CreateParticle(BITMAP_CLOUD, object.Position, object.Angle, light, 14,
                               object.Scale);
                CreateParticle(BITMAP_ADV_SMOKE, object.Position, object.Angle, light, 2,
                               object.Scale * 2.f);
            }
            continue;
        }
        if (boundary == HalfPeriod)
        {
            CreateParticle(BITMAP_ADV_SMOKE + 1, object.Position, object.Angle, light);
            CreateParticle(BITMAP_ADV_SMOKE, object.Position, object.Angle, light, 0);
            continue;
        }
        CreateParticle(BITMAP_CLOUD, object.Position, object.Angle, light, 6);
        CreateParticle(BITMAP_ADV_SMOKE, object.Position, object.Angle, light, 1);
        if (style != AlternatingSmokeStyle::Flare)
            continue;
        Vector(1.f, 0.8f, 0.8f, light);
        CreateParticle(BITMAP_FLARE, object.Position, object.Angle, light, 4, 0.19f);
    }
}

void SessionVisualUnit::EmitBerserkerSmoke(OBJECT &object, BMD &model, int chance)
{
    constexpr int BoneStride = 5;
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        const float fraction = birth.FrameFraction();
        ObjectDrawInput draw(&object);
        object.MotionTrace.Sample(WorldTime, fraction, object.Position, draw.position);
        AnimationPoseSample pose(draw, model.BoneHead, model.BodyHeight, false,
                                 model.PoseAssetIdentity());
        std::array<vec34_t, MAX_BONES> bones;
        draw.bones = pose.EvaluateAtTime(model, object, WorldTime, fraction, bones.data());
        vec3_t light{1.f, 0.1f, 0.1f}, position;
        for (int bone = 0; bone < model.NumBones; bone += BoneStride)
        {
            if (!sessionKeeper_.Random()->FpsCheck(chance, 1.f))
                continue;
            model.TransformByObjectBone(position, draw, bone);
            CreateParticle(BITMAP_SMOKE, position, object.Angle, light, 50, 1.f);
            CreateParticle(BITMAP_SMOKELINE1 + WorldRandom() % 3, position, object.Angle, light, 0,
                           1.f);
        }
    }
}

void SessionVisualUnit::AdvanceButcherVisual(OBJECT &object, BMD &model, bool alive)
{
    constexpr float SmokeInterval = 4.f;
    vec3_t light{1.f, 1.f, 1.f}, position, offset{};
    AnimationPoseSample pose(&object, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / SmokeInterval))
    {
        const float fraction = birth.FrameFraction();
        pose.SampleBonePosition(model, object, 6, offset, WorldTime, fraction, position);
        position[1] += 50.f;
        CreateParticle(BITMAP_SMOKE, position, object.Angle, light, 61);
    }
    if (alive)
        for (auto birth :
             sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / SmokeInterval))
        {
            const float fraction = birth.FrameFraction();
            object.MotionTrace.Sample(WorldTime, fraction, object.Position, position);
            position[0] += WorldRandom() % 64 - 32;
            position[1] += WorldRandom() % 64 - 32;
            position[2] += WorldRandom() % 32 - 16;
            CreateParticle(BITMAP_SMOKE + 1, position, object.Angle, light, 0);
        }
    if (object.Type != MODEL_TERRIBLE_BUTCHER)
        return;
    Vector(1.f, 0.2f, 0.1f, light);
    for (int bone = 2; bone < 35; ++bone)
    {
        if ((bone >= 12 && bone <= 20) || (bone >= 24 && bone <= 32))
            continue;
        model.TransformByObjectBone(position, &object, bone);
        CreateSprite(BITMAP_LIGHT, position, 3.1f, light, &object);
    }
    EmitButcherFire(object, model);
}

void SessionVisualUnit::EmitButcherFire(OBJECT &object, BMD &model)
{
    constexpr int FireParticles = 30, FireBones = 50;
    constexpr float FireScale = 0.8f;
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        const float fraction = birth.FrameFraction();
        ObjectDrawInput draw(&object);
        object.MotionTrace.Sample(WorldTime, fraction, object.Position, draw.position);
        AnimationPoseSample pose(draw, model.BoneHead, model.BodyHeight, false,
                                 model.PoseAssetIdentity());
        std::array<vec34_t, MAX_BONES> bones;
        draw.bones = pose.EvaluateAtTime(model, object, WorldTime, fraction, bones.data());
        vec3_t light{1.f, 1.f, 1.f}, position;
        for (int i = 0; i < FireParticles; ++i)
        {
            model.TransformByObjectBone(position, draw, WorldRandom() % FireBones);
            position[0] += WorldRandom() % 10 - 20;
            position[1] += WorldRandom() % 10 - 20;
            position[2] += WorldRandom() % 10 - 20;
            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticle(BITMAP_FIRE_HIK1, position, object.Angle, light, 0, FireScale);
                break;
            case 1:
                CreateParticle(BITMAP_FIRE_CURSEDLICH, position, object.Angle, light, 4, FireScale);
                break;
            case 2:
                CreateParticle(BITMAP_FIRE_HIK3, position, object.Angle, light, 0, FireScale);
                break;
            }
        }
    }
}

void SessionVisualUnit::EmitBoneLightning(OBJECT &object, BMD &model, int subtype)
{
    constexpr float Interval = 30.f;
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / Interval))
    {
        const float fraction = birth.FrameFraction();
        PrepareWorldObjectPose(object, fraction);
        vec3_t offset{}, start, end;
        model.TransformPosition(BoneTransform[1], offset, start, false);
        model.TransformPosition(BoneTransform[2], offset, end, false);
        CreateJoint(BITMAP_JOINT_THUNDER, start, end, object.Angle, subtype, nullptr, 50.f);
    }
}

void SessionVisualUnit::AdvanceEnergyNode(OBJECT &object, BMD &model, int lightningSubtype)
{
    constexpr float LightningInterval = 20.f;
    vec3_t offset{}, position, light;
    const float luminosity = (sinf(WorldTime * 0.001f) + 1.f) * 0.5f;
    Vector(luminosity, luminosity, luminosity, light);
    PrepareWorldObjectPose(object);
    model.TransformPosition(BoneTransform[4], offset, position, false);
    CreateSprite(BITMAP_SPARK + 1, position, 10.f, light, &object);
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        const float fraction = birth.FrameFraction();
        PrepareWorldObjectPose(object, fraction);
        model.TransformPosition(BoneTransform[4], offset, position, false);
        CreateParticle(BITMAP_ENERGY, position, object.Angle, light, 0, 1.5f);
    }
    for (auto birth :
         sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / LightningInterval))
    {
        const float fraction = birth.FrameFraction();
        PrepareWorldObjectPose(object, fraction);
        model.TransformPosition(BoneTransform[4], offset, position, false);
        vec3_t start, end;
        VectorCopy(position, start);
        VectorCopy(position, end);
        start[0] -= 50.f + WorldRandom() % 100;
        end[0] += WorldRandom() % 80;
        start[1] -= WorldRandom() % 50;
        end[1] += WorldRandom() % 50;
        start[2] += 10.f;
        end[2] += 10.f;
        CreateJoint(BITMAP_JOINT_THUNDER, start, end, object.Angle, lightningSubtype, nullptr,
                    40.f);
    }
}

void SessionVisualUnit::EmitKentaurosDeathSmoke(OBJECT &object, BMD &model, vec3_t light)
{
    constexpr int sockets[]{
        CharacterSocket::KENTAUROS_BIP_TAIL,    CharacterSocket::KENTAUROS_BIP_TAIL_1,
        CharacterSocket::KENTAUROS_BIP_TAIL_2,  CharacterSocket::KENTAUROS_BIP_SPAIN_1,
        CharacterSocket::KENTAUROS_BIP_SPAIN_2, CharacterSocket::KENTAUROS_BIP_SPAIN_3};
    AnimationPoseSample pose(&object, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    std::array<vec34_t, MAX_BONES> bones;
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        ObjectDrawInput draw(&object);
        object.MotionTrace.Sample(WorldTime, birth.FrameFraction(), object.Position, draw.position);
        draw.bones =
            pose.EvaluateAtTime(model, object, WorldTime, birth.FrameFraction(), bones.data());
        for (int index = 0; index < 6; ++index)
        {
            vec3_t position;
            model.TransformByObjectBone(position, draw, sockets[index]);
            const int variant = index % 2 == 0 ? 3 : 1;
            CreateParticle(BITMAP_SMOKE + variant, position, object.Angle, light, variant, 0.3f);
        }
    }
}

void SessionVisualUnit::EmitGenociderDust(OBJECT &object, BMD &model, vec3_t light)
{
    AnimationPoseSample pose(&object, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        vec3_t position, offset{};
        pose.SampleBonePosition(model, object, CharacterSocket::GENO_WP, offset, WorldTime,
                                birth.FrameFraction(), position);
        CreateParticle(BITMAP_SMOKE + 1, position, object.Angle, light, 1, object.Scale);
        CreateEffect(MODEL_STONE1 + WorldRandom() % 2, position, object.Angle, light);
    }
}

void SessionVisualUnit::EmitMedusaParticles(OBJECT &object, BMD &model, bool dying)
{
    std::array<vec34_t, MAX_BONES> bones;
    const auto sample = [&](float fraction) {
        ObjectDrawInput draw(&object);
        object.MotionTrace.Sample(WorldTime, fraction, object.Position, draw.position);
        AnimationPoseSample pose(draw, model.BoneHead, model.BodyHeight, false,
                                 model.PoseAssetIdentity());
        draw.bones = pose.EvaluateAtTime(model, object, WorldTime, fraction, bones.data());
        return draw;
    };
    vec3_t position, light;
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        auto draw = sample(birth.FrameFraction());
        model.TransformByObjectBone(position, draw, 33);
        Vector(1.f, 0.f, 0.f, light);
        CreateJoint(BITMAP_JOINT_ENERGY, position, draw.position, object.Angle, 55, &object, 6.f,
                    -1, 0, 0, -1, light);
        CreateJoint(BITMAP_JOINT_ENERGY, position, draw.position, object.Angle, 56, &object, 6.f,
                    -1, 0, 0, -1, light);
        model.TransformByObjectBone(position, draw, 68);
        Vector(0.1f, 0.6f, 0.3f, light);
        CreateJoint(BITMAP_JOINT_ENERGY, position, draw.position, object.Angle, 57, &object, 10.f,
                    67, 0, 0, 15, light);
        CreateJoint(BITMAP_JOINT_ENERGY, position, draw.position, object.Angle, 57, &object, 10.f,
                    70, 0, 0, 15, light);
    }
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
    {
        auto draw = sample(birth.FrameFraction());
        model.TransformByObjectBone(position, draw, 68);
        Vector(0.9f, 1.f, 0.9f, light);
        CreateEffect(BITMAP_WATERFALL_4, position, object.Angle, light, 0, &object, 68);
    }
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
    {
        auto draw = sample(birth.FrameFraction());
        model.TransformByObjectBone(position, draw, 5);
        Vector(0.25f, 1.f, 0.f, light);
        CreateParticle(BITMAP_SMOKE, position, object.Angle, light, 50, 1.f);
        CreateParticle(BITMAP_SMOKELINE1 + WorldRandom() % 3, position, object.Angle, light, 0,
                       1.f);
    }
    if (!dying)
        return;
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
    {
        auto draw = sample(birth.FrameFraction());
        Vector(0.4f, 0.9f, 0.6f, light);
        for (int bone : {0, 5, 124})
        {
            model.TransformByObjectBone(position, draw, bone);
            CreateParticle(BITMAP_SMOKE, position, object.Angle, light, 1);
            CreateParticle(BITMAP_SMOKE, position, object.Angle, light, 1);
        }
    }
}

void SessionVisualUnit::AdvanceBurningNapin(OBJECT &object, BMD &model, bool ice)
{
    constexpr int FireBones[] = {10, 61, 72, 21, 122, 116};
    vec3_t position, spriteLight{1.f, ice ? 1.f : 0.2f, ice ? 1.f : 0.f};
    for (int i = 0; i < 6; ++i)
    {
        model.TransformByObjectBone(position, &object, FireBones[i]);
        CreateSprite(BITMAP_LIGHT, position, i >= 4 ? 1.f : 4.f, spriteLight, &object);
    }
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        const float fraction = birth.FrameFraction();
        ObjectDrawInput draw(&object);
        object.MotionTrace.Sample(WorldTime, fraction, object.Position, draw.position);
        AnimationPoseSample pose(draw, model.BoneHead, model.BodyHeight, false,
                                 model.PoseAssetIdentity());
        std::array<vec34_t, MAX_BONES> bones;
        draw.bones = pose.EvaluateAtTime(model, object, WorldTime, fraction, bones.data());
        vec3_t light{ice ? 0.25f : 1.f, ice ? 0.6f : 1.f, ice ? 0.7f : 1.f};
        for (int i = 0; i < 6; ++i)
        {
            const int spread = i >= 4 ? 10 : 20;
            const float scale = i >= 4 ? 0.7f : 1.2f;
            vec3_t offset;
            for (int axis = 0; axis < 3; ++axis)
                offset[axis] = float(WorldRandom() % spread - spread / 2);
            model.TransformByObjectBone(position, draw, FireBones[i], offset);
            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticle(BITMAP_FIRE_HIK1, position, object.Angle, light, 0, scale);
                break;
            case 1:
                CreateParticle(BITMAP_FIRE_CURSEDLICH, position, object.Angle, light, 4, scale);
                break;
            case 2:
                CreateParticle(BITMAP_FIRE_HIK3, position, object.Angle, light, 0, scale);
                break;
            }
        }
    }
}
