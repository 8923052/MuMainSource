#include "domain/EffectsUpdate.h"
#include "domain/CharacterSystem.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
#include "support/CoreMath.h"
#include "session/SessionGameplay.h"
#include "render/ModelGeometry.h"
#include "render/ModelResources.h"
#include "app/ApplicationDiagnostics.h"
#include "render/FrameTape.h"
#include "render/Textures.h"
#include "data/Localization.h"
#include "domain/ItemsSkills.h"
#include "domain/CharacterPresentation.h"
#include "domain/WorldSimulation.h"
#include "render/World.h"
#include "data/WorldData.h"
#include "render/Terrain.h"
#include "domain/MovementAI.h"
#include "session/SessionKeeper.h"
#include "session/SessionRender.h"
#include "app/ApplicationAudio.h"
#include "session/SessionNetwork.h"
#include "render/Sprites.h"
#include "ui/session/UiSessionLogic.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "domain/Events.h"
#include "domain/MapSimulation.h"
#include "session/SessionPresentation.h"
#include "domain/ChatSocial.h"
#include "data/CharacterData.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "domain/Quests.h"
#include "session/SessionAudio.h"
#include "session/SessionWorkspace.h"
#include "domain/Guild.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "render/Text.h"
#include "support/Scenes.h"
#include "ui/runtime/UiControls.h"
#include "I18N/All.h"
#include "domain/WorldPhysics.h"
#include "support/Camera.h"
#include "app/AppWindow.h"
#include "ui/features/Activities/ActivitiesLogic.h"
#include "ui/features/Dialogs/DialogsLogic.h"

EffectBirthStep::EffectBirthStep(float &frames, EffectBirthTiming &birth)
    : frames_(frames), previous_(frames), birth_(birth), previousStart_(birth.activeStartFraction),
      serial_(birth.serial)
{
    if (birth.remainingFrames >= 0.f)
    {
        frames_ = birth.remainingFrames;
        birth.activeStartFraction = birth.pendingStartFraction;
        birth.remainingFrames = -1.f;
    }
}

EffectBirthStep::~EffectBirthStep()
{
    frames_ = previous_;
    if (birth_.serial == serial_)
        birth_.activeStartFraction = previousStart_;
}

float EffectBirthTiming::FrameFraction(float local) const
{
    return activeStartFraction + (1.f - activeStartFraction) * local;
}

EffectEmissionScope::EffectEmissionScope(std::optional<float> &time, float remainingFrames,
                                         float sceneFrames, float intervalTail)
    : time_(time), previous_(time), remainingFrames_(remainingFrames), sceneFrames_(sceneFrames),
      intervalTail_(intervalTail)
{
    time_ = remainingFrames;
}

EffectEmissionScope::~EffectEmissionScope()
{
    time_ = previous_;
}
float EffectEmissionScope::RemainingFrames() const
{
    return remainingFrames_;
}
float EffectEmissionScope::SceneRemainingFrames() const
{
    return remainingFrames_ + intervalTail_;
}
float EffectEmissionScope::FrameFraction() const
{
    return sceneFrames_ > 0.f ? 1.f - SceneRemainingFrames() / sceneFrames_ : 1.f;
}

EffectEmissionScope EffectEmissionSequence::Iterator::operator*() const
{
    return EffectEmissionScope(*time, remainingAtStart - frames * (float(index) / count),
                               sceneFrames, intervalTail);
}

EffectEmissionSequence::Iterator &EffectEmissionSequence::Iterator::operator++()
{
    ++index;
    return *this;
}

bool EffectEmissionSequence::Iterator::operator!=(const Iterator &other) const
{
    return index != other.index;
}

EffectEmissionSequence::EffectEmissionSequence(std::optional<float> &time, int count, float frames,
                                               float remainingAtStart, float sceneFrames,
                                               float intervalTail)
    : time_(time), count_(count), frames_(frames), remainingAtStart_(remainingAtStart),
      sceneFrames_(sceneFrames), intervalTail_(intervalTail)
{
}

EffectEmissionSequence::Iterator EffectEmissionSequence::begin()
{
    return {&time_, 0, count_, frames_, remainingAtStart_, sceneFrames_, intervalTail_};
}

EffectEmissionSequence::Iterator EffectEmissionSequence::end()
{
    return {&time_, count_, count_, frames_, remainingAtStart_, sceneFrames_, intervalTail_};
}

EffectUpdateInterval::EffectUpdateInterval(float &frames, float &intervalTail,
                                           std::optional<float> &emissionTime, float duration,
                                           float offset)
    : frames_(frames), intervalTail_(intervalTail), emissionTime_(emissionTime),
      previousFrames_(frames), previousTail_(intervalTail), previousEmissionTime_(emissionTime)
{
    intervalTail_ += (std::max)(0.f, frames_ - offset - duration);
    frames_ = duration;
    emissionTime_ = 0.f;
}

EffectUpdateInterval::~EffectUpdateInterval()
{
    frames_ = previousFrames_;
    intervalTail_ = previousTail_;
    emissionTime_ = previousEmissionTime_;
}

void SessionGameplayUnit::AdvanceAgAddition(PARTICLE &particle, int index)
{
    constexpr float FadeLife = 10.f, FullAlpha = 0.7f, EndAlpha = 0.1f, FadeRate = 0.1f;
    constexpr float Growth[] = {1.05f, 1.02f, 1.03f};
    constexpr float Height[] = {-10.f, 10.f, 25.f};
    const float initialLife = particle.LifeTime + FPS_ANIMATION_FACTOR;
    const float untilFade = std::max(0.f, initialLife - FadeLife);
    const float initialAlpha = initialLife >= FadeLife ? FullAlpha : particle.Alpha;
    const float untilRenewal = untilFade + std::max(0.f, (initialAlpha - EndAlpha) / FadeRate);
    const float active = std::min({FPS_ANIMATION_FACTOR, std::max(0.f, initialLife), untilRenewal});
    particle.Alpha = initialAlpha - FadeRate * std::max(0.f, active - untilFade);
    particle.Scale *= std::pow(Growth[particle.SubType], active);
    VectorScale(particle.TurningForce, particle.Alpha, particle.Light);
    OBJECT *target = particle.Target;
    BMD &model = Models[target->Type];
    AnimationPoseSample pose(target, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    vec3_t offset{}, position;
    auto birth = EmissionTime(FPS_ANIMATION_FACTOR - active);
    pose.SampleBonePosition(model, *target, 18, offset, WorldTime, birth.FrameFraction(), position);
    position[2] += Height[particle.SubType];
    VectorCopy(position, particle.Position);
    if (untilRenewal > FPS_ANIMATION_FACTOR && initialLife > FPS_ANIMATION_FACTOR)
        return;
    const int subtype = particle.SubType;
    vec3_t angle, light;
    VectorCopy(particle.Angle, angle);
    VectorCopy(particle.Light, light);
    Particles.Retire(index);
    particle.Target = nullptr;
    particle.SocketBinding.reset();
    if (g_isCharacterBuff(target, eBuff_AG_Addition))
        CreateParticle(BITMAP_AG_ADDITION_EFFECT, position, angle, light, subtype, 1.f, target);
}

namespace
{
constexpr int MAX_BLUR_LIFETIME = 30;
constexpr int MAX_OBJECT_BLUR_LIFETIME = 30;
} // namespace
namespace
{
bool HasObjectBlurGap(const ObjectBlur &blur, const float *current, const float *nextFirst,
                      const float *nextSecond)
{
    if (blur.SubType != 113 && blur.SubType != 114)
        return false;
    constexpr float AuthoredGapDistance = 300.f;
    for (int axis = 0; axis < 3; ++axis)
        if (std::fabs(current[axis] - nextFirst[axis]) > AuthoredGapDistance ||
            std::fabs(current[axis] - nextSecond[axis]) > AuthoredGapDistance)
            return true;
    return false;
}
} // namespace

void AddBlur(Blur *blur, vec3_t first, vec3_t second, vec3_t light, int type)
{
    blur->Type = type;
    VectorCopy(light, blur->Light);
    blur->P1.Advance();
    blur->P2.Advance();
    VectorCopy(first, blur->P1[0]);
    VectorCopy(second, blur->P2[0]);
    blur->Number = std::min<int>(blur->Number + 1, MAX_BLUR_TAILS - 1);
}

void AddObjectBlur(ObjectBlur *blur, vec3_t first, vec3_t second, vec3_t light, int type)
{
    const bool gap = blur->Number > 0 && HasObjectBlurGap(*blur, first, blur->P1[0], blur->P2[0]);
    blur->Type = type;
    VectorCopy(light, blur->Light);
    blur->P1.Advance();
    blur->P2.Advance();
    blur->BreakAfter.Advance();
    VectorCopy(first, blur->P1[0]);
    VectorCopy(second, blur->P2[0]);
    blur->BreakAfter[0] = gap;
    blur->Number = std::min<int>(blur->Number + 1, MAX_OBJECT_BLUR_TAILS - 1);
}

void SessionGameplayUnit::CreateBlur(CHARACTER *Owner, vec3_t p1, vec3_t p2, vec3_t Light, int Type,
                                     bool Short, int SubType)
{
    for (auto &blur : g_blurs)
    {
        if (blur.Owner != Owner || (SubType > 0 && blur.SubType != SubType))
            continue;
        AddBlur(&blur, p1, p2, Light, Type);
        return;
    }
    auto *freeBlur = &g_blurs[g_blurs.Allocate()];
    freeBlur->P1.Reset(MAX_BLUR_TAILS);
    freeBlur->P2.Reset(MAX_BLUR_TAILS);
    freeBlur->Owner = Owner;
    freeBlur->Number = 0;
    freeBlur->LifeTime = Short ? 15 : MAX_BLUR_LIFETIME;
    freeBlur->SubType = SubType;
    AddBlur(freeBlur, p1, p2, Light, Type);
}

void SessionLegacyCalls::CreateSpark(int type, CHARACTER *character, vec_t *position, vec_t *angle)
{
    sessionKeeper_.Gameplay()->CreateSpark(type, character, position, angle);
}

void SessionLegacyCalls::CreateBlood(OBJECT *object)
{
    sessionKeeper_.Gameplay()->CreateBlood(object);
}

void SessionGameplayUnit::MoveBlurs()
{
    for (auto &blur : g_blurs)
    {
        const float previousLifetime = blur.LifeTime;
        blur.LifeTime -= FPS_ANIMATION_FACTOR;
        const int expiredSamples = static_cast<int>(ceilf(previousLifetime) - ceilf(blur.LifeTime));
        blur.Number = std::max<int>(blur.Number - expiredSamples, 0);

        for (int sample = 0; sample < expiredSamples && blur.Number > 0; ++sample)
        {
            blur.P1.RepeatFront();
            blur.P2.RepeatFront();
        }

        if (blur.LifeTime <= 0)
        {
            blur.Number = std::max<int>(blur.Number - expiredSamples, 0);
            if (blur.Number <= 0)
            {
                RetireBlur(g_blurs, blur);
            }
        }
    }
    MoveObjectBlurs();
}

void SessionGameplayUnit::ClearAllObjectBlurs()
{
    for (auto &blur : g_objectBlurs)
    {
        RetireBlur(g_objectBlurs, blur);
    }
}

void SessionGameplayUnit::CreateObjectBlur(OBJECT *Owner, vec3_t p1, vec3_t p2, vec3_t Light,
                                           int Type, bool Short, int SubType, int iLimitLifeTime)
{
    for (auto &blur : g_objectBlurs)
    {
        if (blur.Owner != Owner || (SubType > 0 && blur.SubType != SubType))
            continue;
        AddObjectBlur(&blur, p1, p2, Light, Type);
        return;
    }
    auto *freeBlur = &g_objectBlurs[g_objectBlurs.Allocate()];
    freeBlur->P1.Reset(MAX_OBJECT_BLUR_TAILS);
    freeBlur->P2.Reset(MAX_OBJECT_BLUR_TAILS);
    freeBlur->BreakAfter.Reset(MAX_OBJECT_BLUR_TAILS);
    freeBlur->Owner = Owner;
    freeBlur->Number = 0;
    freeBlur->LimitLifeTime =
        iLimitLifeTime > -1 ? iLimitLifeTime : (Short ? 15 : MAX_OBJECT_BLUR_LIFETIME);
    freeBlur->LifeTime = freeBlur->LimitLifeTime;
    freeBlur->SubType = SubType;
    AddObjectBlur(freeBlur, p1, p2, Light, Type);
}

void SessionGameplayUnit::MoveObjectBlurs()
{
    for (auto &blur : g_objectBlurs)
    {
        const float previousLifetime = blur.LifeTime;
        blur.LifeTime -= FPS_ANIMATION_FACTOR;
        const int expiredSamples = static_cast<int>(ceilf(previousLifetime) - ceilf(blur.LifeTime));
        blur.Number = std::max<int>(blur.Number - expiredSamples, 0);

        if (blur.LifeTime <= 0)
        {
            RetireBlur(g_objectBlurs, blur);
            continue;
        }

        for (int sample = 0; sample < expiredSamples && blur.Number > 0; ++sample)
        {
            blur.P1.RepeatFront();
            blur.P2.RepeatFront();
            blur.BreakAfter.Advance();
            blur.BreakAfter[0] = HasObjectBlurGap(blur, blur.P1[0], blur.P1[1], blur.P2[1]);
        }
    }
}

void SessionGameplayUnit::RemoveObjectBlurs(OBJECT *Owner, int SubType)
{
    for (auto &blur : g_objectBlurs)
    {
        if (blur.Owner == Owner)
        {
            if (SubType > 0 && blur.SubType != SubType)
            {
                continue;
            }
            RetireBlur(g_objectBlurs, blur);
        }
    }
}

void SessionLegacyCalls::CreateBlur(CHARACTER *owner, vec3_t first, vec3_t second, vec3_t light,
                                    int type, bool shortBlur, int subType)
{
    sessionKeeper_.Gameplay()->CreateBlur(owner, first, second, light, type, shortBlur, subType);
}

void SessionLegacyCalls::MoveBlurs()
{
    sessionKeeper_.Gameplay()->MoveBlurs();
}

void SessionLegacyCalls::ClearAllObjectBlurs()
{
    sessionKeeper_.Gameplay()->ClearAllObjectBlurs();
}

void SessionLegacyCalls::CreateObjectBlur(OBJECT *owner, vec3_t first, vec3_t second, vec3_t light,
                                          int type, bool shortBlur, int subType, int limitLifeTime)
{
    sessionKeeper_.Gameplay()->CreateObjectBlur(owner, first, second, light, type, shortBlur,
                                                subType, limitLifeTime);
}

void SessionLegacyCalls::MoveObjectBlurs()
{
    sessionKeeper_.Gameplay()->MoveObjectBlurs();
}

void SessionLegacyCalls::RemoveObjectBlurs(OBJECT *owner, int subType)
{
    sessionKeeper_.Gameplay()->RemoveObjectBlurs(owner, subType);
}

void SessionGameplayUnit::CreateSpark(int Type, CHARACTER *tc, vec3_t Position, vec3_t Angle)
{
    OBJECT *to = &tc->Object;
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    CreateParticle(BITMAP_SPARK + 1, Position, to->Angle, Light);
    vec3_t p, p2;
    float Matrix[3][4];
    Vector(0.f, 50.f, 0.f, p);
    AngleMatrix(Angle, Matrix);
    VectorRotate(p, Matrix, p2);
    VectorAdd(p2, Position, p2);
    for (int i = 0; i < 20; i++)
    {
        vec3_t a;
        Vector(Random.RangeFloat(0.f, 360.f), 0.f, Random.RangeFloat(0.f, 360.f), a);
        VectorAdd(a, Angle, a);
        CreateParticle(BITMAP_SPARK, Position, to->Angle, Light);
    }
}

void SessionGameplayUnit::CreateBlood(OBJECT *o)
{
    int BoneHead = Models[o->Type].BoneHead;
    if (BoneHead != -1)
    {
        if (o->Type == MODEL_ICE_MONSTER)
        {
            o->Live = false;
            for (int i = 0; i < 10; i++)
                CreateEffect(MODEL_ICE_SMALL, o->Position, o->Angle, o->Light);
        }
        else if (o->Type != MODEL_GHOST && o->Type != MODEL_ASSASSIN && o->Type != MODEL_ICE_QUEEN)
        {
            vec3_t p, Position;
            for (int i = 0; i < 2; i++)
            {
                Vector(Random.RangeFloat(-50.f, 50.f), Random.RangeFloat(-50.f, 50.f), 0.f, p);
                Models[o->Type].TransformPosition(o->BoneTransform[BoneHead], p, Position, true);
                const float rotation = Random.RangeFloat(0.f, 360.f);
                const float scale = Random.RangeFloat(0.8f, 1.2f);
                CreatePointer(BITMAP_BLOOD, Position, rotation, o->Light, scale);
            }
        }
    }
}

void SessionGameplayUnit::AdvanceEffectAnimation(OBJECT &effect)
{
    switch (effect.Type)
    {
    case BITMAP_LIGHT_MARKS: {
        constexpr float LightStep = 1.035f;
        constexpr float FadeTurningPoint = 35.f;
        const float decayTicks = std::clamp(
            effect.LifeTime + FPS_ANIMATION_FACTOR - FadeTurningPoint, 0.f, FPS_ANIMATION_FACTOR);
        const float exponent = FPS_ANIMATION_FACTOR - 2.f * decayTicks;
        VectorScale(effect.Light, powf(LightStep, exponent), effect.Light);
        return;
    }
    case MODEL_MULTI_SHOT1:
    case MODEL_MULTI_SHOT2:
    case MODEL_MULTI_SHOT3:
    case MODEL_DRAGON_LOWER_DUMMY:
        break;
    default:
        return;
    }

    // Preserve the former once-per-tick presentation step after normal movement.
    // Dragon-lower-dummy also retains its existing handler and shared-tail steps.
    constexpr float PresentationSpeedDivisor = 6.f;
    auto &model = sessionKeeper_.ModelPoolObject()[effect.Type];
    model.CurrentAction = effect.CurrentAction;
    model.PlayAnimation(&effect.AnimationFrame, &effect.PriorAnimationFrame, &effect.PriorAction,
                        effect.Velocity / PresentationSpeedDivisor, effect.Position, effect.Angle);
}

void SessionGameplayUnit::BeginEffectBirths()
{
    // Unscheduled one-shot calls observe the source's prepared endpoint.
    effectFrameActive_ = true;
    effectIntervalTail_ = 0.f;
    emissionRemainingFrames_ = 0.f;
    effectFrameFrames_ = FPS_ANIMATION_FACTOR;
}

void SessionGameplayUnit::RegisterEffectBirth(EffectBirthTiming &timing, NewEffect effect,
                                              int index, BirthParticlePool particlePool)
{
    timing = {};
    timing.serial = ++nextBirthSerial_;
    if (!effectFrameActive_ || !emissionRemainingFrames_)
        return;
    timing.remainingFrames = *emissionRemainingFrames_ + effectIntervalTail_;
    const float frame = effectFrameFrames_ > 0.f ? effectFrameFrames_ : FPS_ANIMATION_FACTOR;
    timing.pendingStartFraction = frame > 0.f ? 1.f - timing.remainingFrames / frame : 0.f;
    pendingBirths_.push_back({effect, index, timing.serial, particlePool});
}

void SessionGameplayUnit::FinishEffectBirths()
{
    // Factories append descendants. Read each entry by value because an update
    // may grow the queue, and use the birth identity when a slot was reused.
    for (std::size_t index = 0; index < pendingBirths_.size(); ++index)
    {
        const auto birth = pendingBirths_[index];
        std::visit(
            [&](auto *effect) {
                if (!effect->Live || effect->BirthTiming.serial != birth.serial ||
                    effect->BirthTiming.remainingFrames < 0.f)
                    return;
                EffectBirthStep step(FPS_ANIMATION_FACTOR, effect->BirthTiming);
                if (FPS_ANIMATION_FACTOR <= 0.f)
                    return;
                using Effect = std::remove_pointer_t<decltype(effect)>;
                if constexpr (std::is_same_v<Effect, OBJECT>)
                {
                    MoveEffect(effect, birth.index);
                    if (effect->BirthTiming.serial != birth.serial)
                        return;
                    AdvanceMapEffectVisual(*effect);
                    TheMapProcess().AdvanceObjectFade(*effect);
                }
                else if constexpr (std::is_same_v<Effect, JOINT>)
                    MoveJoint(effect, birth.index);
                else if (birth.particlePool == BirthParticlePool::Pointers)
                    MovePointerEntry(effect, birth.index);
                else if (birth.particlePool == BirthParticlePool::Points)
                    MovePointEntry(effect, birth.index);
                else
                    MoveParticleEntry(effect, birth.index);
            },
            birth.effect);
    }
    pendingBirths_.clear();
    emissionRemainingFrames_.reset();
    effectFrameFrames_ = 0.f;
    effectIntervalTail_ = 0.f;
    effectFrameActive_ = false;
}

EffectEmissionSequence SessionGameplayUnit::Emissions(double expectedCount)
{
    return Emissions(expectedCount, FPS_ANIMATION_FACTOR);
}

EffectEmissionSequence SessionGameplayUnit::Emissions(double expectedCount, float duration,
                                                      float offset)
{
    return EffectEmissionSequence(
        emissionRemainingFrames_, expectedCount == 0.0 ? 0 : Random.EmissionCount(expectedCount),
        duration, FPS_ANIMATION_FACTOR - offset,
        effectFrameFrames_ > 0.f ? effectFrameFrames_ : FPS_ANIMATION_FACTOR, effectIntervalTail_);
}

void SessionGameplayUnit::RetireEffect(OBJECT *object)
{
    if (const auto slot = Effects.Find(object))
        Effects.Retire(*slot);
    else if (const auto slot = g_SkillEffects.Storage().Find(object))
        g_SkillEffects.Storage().Retire(*slot);
    else
        object->Live = false;
}

void SessionLegacyCalls::RetireEffect(OBJECT *object)
{
    sessionKeeper_.Gameplay()->RetireEffect(object);
}

void SessionGameplayUnit::RetireGroundItem(ITEM_t &item)
{
    if (!item.Object.Live)
        return;
    OBJECT *target = &item.Object;
    RetireCharacterEffectTargets({&target, 1});
    target->Live = false;
}

void SessionGameplayUnit::ClearWorldEffects()
{
    // Called before world owners and their poses are released. Clear attachments
    // as well as liveness so reused storage cannot inherit an old world target.
    for (auto &effect : Effects)
        EffectDestructor(&effect);
    g_SkillEffects.DeleteAllEffects();
    const auto clearParticles = [](auto &particles) {
        for (auto &particle : particles)
        {
            particles.Retire(particle);
            particle.Target = nullptr;
            particle.SocketBinding.reset();
        }
    };
    clearParticles(Particles);
    clearParticles(Points);
    clearParticles(Pointers);
    for (auto &leaf : Leaves)
    {
        leaf.Live = false;
        leaf.Target = nullptr;
        leaf.SocketBinding.reset();
    }
    for (auto &joint : Joints)
    {
        Joints.Retire(joint);
        joint.Target = nullptr;
        joint.Tails.Clear();
    }
    for (auto &blur : g_blurs)
        RetireBlur(g_blurs, blur);
    for (auto &blur : g_objectBlurs)
        RetireBlur(g_objectBlurs, blur);
    Sprites.Clear();
}

void SessionGameplayUnit::AdvanceEffectShadow(OBJECT &effect, float previousLifetime)
{
    if (effect.Type != BITMAP_MAGIC && effect.Type != BITMAP_MAGIC + 1 &&
        effect.Type != BITMAP_LIGHTNING + 1)
        return;
    if (!g_pOption->GetRenderAllEffects())
        return;
    if (effect.Type == BITMAP_MAGIC && effect.SubType == 1)
    {
        constexpr float LightDecay = 1.f / 1.1f;
        constexpr float ScaleGrowth = 0.05f;
        const float fade = std::pow(LightDecay, FPS_ANIMATION_FACTOR);
        VectorScale(effect.Light, fade, effect.Light);
        effect.Scale += ScaleGrowth * FPS_ANIMATION_FACTOR;
    }
    else if (effect.Type == BITMAP_LIGHTNING + 1)
    {
        constexpr float ScaleGrowth = 0.2f;
        effect.Scale += ScaleGrowth * FPS_ANIMATION_FACTOR;
    }
    else if (effect.Type == BITMAP_MAGIC + 1 && effect.SubType == 7)
    {
        constexpr float RotationPerTick = 10.f;
        VectorCopy(effect.Owner->Position, effect.Position);
        effect.Angle[2] += RotationPerTick * FPS_ANIMATION_FACTOR;
    }
    else if (effect.Type == BITMAP_MAGIC + 1 && (effect.SubType == 6 || effect.SubType == 8))
    {
        // These children formerly originated in the presentation pass.
        SessionRandom::PresentationScope randomOrigin(sessionKeeper_.RandomForConstruction());
        constexpr float OwnerRingInterval = 25.f;
        constexpr float GroundRingInterval = 2.f;
        constexpr int OwnerRingSubtype = 7;
        constexpr int GroundRingSubtype = 9;
        const float interval = effect.SubType == 6 ? OwnerRingInterval : GroundRingInterval;
        const int subtype = effect.SubType == 6 ? OwnerRingSubtype : GroundRingSubtype;
        // Emit each crossed legacy lifetime boundary once, including fractional ticks.
        for (float boundary = (std::ceil(previousLifetime / interval) - 1.f) * interval;
             boundary > 0.f && boundary >= effect.LifeTime; boundary -= interval)
        {
            const float remaining =
                (std::max)(0.f, FPS_ANIMATION_FACTOR - (previousLifetime - boundary));
            auto birth = EmissionTime(remaining);
            vec3_t position;
            effect.MotionTrace.Sample(WorldTime, birth.FrameFraction(), effect.Position, position);
            CreateEffect(BITMAP_MAGIC + 1, position, effect.Angle, effect.Light, subtype,
                         effect.Owner);
        }
    }
}

void EffectSlotSet::Grow(std::size_t bits)
{
    for (std::size_t level = 0; bits != 0; ++level)
    {
        const auto words = (bits + WordBits - 1) / WordBits;
        if (level == levels_.size())
        {
            levels_.emplace_back(words, 0);
            if (level != 0)
                for (std::size_t child = 0; child < levels_[level - 1].size(); ++child)
                    if (levels_[level - 1][child] != 0)
                        levels_[level][child / WordBits] |= std::uint64_t{1} << (child % WordBits);
        }
        else
            levels_[level].resize(words, 0);
        if (words == 1)
            break;
        bits = words;
    }
}

bool EffectSlotSet::Set(std::size_t bit, bool present) noexcept
{
    for (std::size_t level = 0; level < levels_.size(); ++level)
    {
        auto &word = levels_[level][bit / WordBits];
        const auto mask = std::uint64_t{1} << (bit % WordBits);
        const auto previous = word;
        if (present)
            word |= mask;
        else
            word &= ~mask;
        if (previous == word)
            return level != 0;
        if ((previous != 0) == (word != 0))
            return true;
        present = word != 0;
        bit /= WordBits;
    }
    return true;
}

std::size_t EffectSlotSet::Next(std::size_t first) const noexcept
{
    return NextAtLevel(0, first);
}

std::size_t EffectSlotSet::NextAtLevel(std::size_t level, std::size_t first) const noexcept
{
    if (level == levels_.size())
        return End;
    const auto &words = levels_[level];
    auto wordIndex = first / WordBits;
    if (wordIndex >= words.size())
        return End;
    auto word = words[wordIndex];
    const auto firstMask = std::uint64_t{1} << (first % WordBits);
    // Dense runs normally continue at the immediately adjacent slot. Avoid
    // another bit search for that common case; gaps still use the hierarchy.
    if ((word & firstMask) != 0)
        return first;
    word &= ~(firstMask - 1);
    if (word == 0)
    {
        wordIndex = NextAtLevel(level + 1, wordIndex + 1);
        if (wordIndex == End)
            return End;
        word = words[wordIndex];
    }
    return wordIndex * WordBits + std::countr_zero(word);
}

void SessionGameplayUnit::AdvanceRaklionCloud(PARTICLE &cloud, int index)
{
    constexpr float Speed = 13.f, Growth = 0.065f, DecayDivisor = 1.6f, EndLight = 0.01f;
    const float totalLight = cloud.Light[0] + cloud.Light[1] + cloud.Light[2];
    const float untilDark =
        totalLight > EndLight ? std::log(totalLight / EndLight) / std::log(DecayDivisor) : 0.f;
    const float active = std::min(
        {FPS_ANIMATION_FACTOR, std::max(0.f, cloud.LifeTime + FPS_ANIMATION_FACTOR), untilDark});
    float matrix[3][4];
    vec3_t local{0.f, -Speed, 0.f}, velocity;
    AngleMatrix(cloud.Angle, matrix);
    VectorRotate(local, matrix, velocity);
    for (auto birth : Emissions(active / 10.0, active))
    {
        const float elapsed = FPS_ANIMATION_FACTOR - birth.RemainingFrames();
        const float radius = 70.f * (cloud.Scale + Growth * elapsed);
        vec3_t position, light;
        VectorAddScaled(cloud.Position, velocity, position, elapsed);
        for (int axis = 0; axis < 3; ++axis)
            position[axis] += radius * (WorldRandom() % 2000 - 1000) * 0.001f;
        VectorScale(cloud.Light, std::pow(1.f / DecayDivisor, elapsed), light);
        CreateParticle(BITMAP_SHINY + 6, position, cloud.Angle, light, 0, 0.5f);
    }
    VectorAddScaled(cloud.Position, velocity, cloud.Position, active);
    cloud.Scale += Growth * active;
    VectorScale(cloud.Light, std::pow(1.f / DecayDivisor, active), cloud.Light);
    if (untilDark <= FPS_ANIMATION_FACTOR)
        Particles.Retire(index);
}

namespace GameLogic::Effects::Behaviors
{
// MODEL_MAYASTONE4 / MODEL_MAYASTONE5: launch a stone with a random lifetime,
// size, spin and gravity, its initial direction rotated by the random spin.
// The rand() draws happen in the original order so the sequence is preserved.
void MoveBehavior::CreateMayaStone45(OBJECT *o)
{
    vec3_t p1;
    float Matrix[3][4];
    Vector(0.f, (float)(WorldRandom() % 256 + 64) * 0.1f, 0.f, p1);
    o->LifeTime = WorldRandom() % 16 + 32;
    o->Scale = (float)(WorldRandom() % 10) / 3.0f + 1.0f;
    o->Angle[2] = (float)(WorldRandom() % 360);
    AngleMatrix(o->Angle, Matrix);
    VectorRotate(p1, Matrix, o->Direction);
    o->Gravity = (float)(WorldRandom() % 16 + 8);
}

void MoveBehaviorLegacyCalls::CreateMayaStone45(OBJECT *o)
{
    owner_.CreateMayaStone45(o);
}

// MODEL_DESAIR: rides the joint addressed by m_sTargetIndex and sheds
// feather effects every 10 ticks of life.
bool MoveBehavior::MoveDesair(OBJECT *o, int, float)
{
    if (o->m_sTargetIndex < 0 || o->m_sTargetIndex >= Joints.Capacity())
        return true;
    JOINT *oj = &Joints[o->m_sTargetIndex];
    if (oj->Live == true)
    {
        VectorCopy(oj->Position, o->Position);
        VectorCopy(oj->Angle, o->Angle);
        constexpr float FeatherInterval = 10.f;
        for (float life =
                 Core::Time::ReferenceSample(o->LifeTime / FeatherInterval) * FeatherInterval;
             life > 0.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life);
             life -= FeatherInterval)
        {
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR -
                                                                 std::max(0.f, o->LifeTime - life));
            CreateEffect(MODEL_FEATHER, o->Position, o->Angle, o->Light, 2, NULL, -1, 0, 0, 0,
                         1.4f);
            CreateEffect(MODEL_FEATHER, o->Position, o->Angle, o->Light, 3, NULL, -1, 0, 0, 0,
                         1.4f);
        }
    }
    return true;
}

// MODEL_INFINITY_ARROW4: grows and fades while tracking the owner's hand bone.
bool MoveBehavior::MoveInfinityArrow4(OBJECT *o, int, float)
{
    if (o->Owner == NULL)
    {
        o->LifeTime = 0;
        return true;
    }
    vec3_t tmp = {0.f, 0.f, 0.f};
    OBJECT *pOwner = o->Owner;
    o->Scale *= pow(1.2f, FPS_ANIMATION_FACTOR);
    o->Light[0] *= pow(0.9f, FPS_ANIMATION_FACTOR);
    o->Light[1] *= pow(0.9f, FPS_ANIMATION_FACTOR);
    o->Light[2] *= pow(0.9f, FPS_ANIMATION_FACTOR);

    VectorTransform(tmp, pOwner->BoneTransform[29], o->Position);
    VectorScale(o->Position, pOwner->Scale, o->Position);
    VectorAdd(o->Position, pOwner->Position, o->Position);
    VectorCopy(pOwner->Angle, o->Angle);
    return true;
}

// MODEL_MAGIC_CAPSULE2: sticks to the owner and fades out over its last ticks.
bool MoveBehavior::MoveMagicCapsule2(OBJECT *o, int, float)
{
    if (o->Owner == NULL)
    {
        o->LifeTime = 0;
        return true;
    }
    VectorCopy(o->Owner->Position, o->Position);
    if (o->LifeTime < 10)
        o->BlendMeshLight = (float)o->LifeTime * 0.1f;
    return true;
}

// MODEL_SPEAR: trails a flare joint sized by subtype.
bool MoveBehavior::MoveSpear(OBJECT *o, int, float)
{
    if (1 == o->SubType)
        CreateJointFpsChecked(BITMAP_FLARE, o->Position, o->Position, o->Angle, 12, o, 100.0f);
    else if (0 == o->SubType)
        CreateJointFpsChecked(BITMAP_FLARE, o->Position, o->Position, o->Angle, 4, o, 50.0f);
    return true;
}

// MODEL_SUMMONER_SUMMON_NEIL_NIFE1..3: fade out near end of life, otherwise fade in.
bool MoveBehavior::MoveSummonerNeilNife(OBJECT *o, int, float)
{
    if (o->LifeTime < 20)
        o->Alpha -= (0.05f) * FPS_ANIMATION_FACTOR;
    else if (o->Alpha < 1.0f)
        o->Alpha += (0.05f) * FPS_ANIMATION_FACTOR;
    return true;
}

// MODEL_SUMMONER_SUMMON_NEIL_GROUND1..3: same fade-out, faster fade-in.
bool MoveBehavior::MoveSummonerNeilGround(OBJECT *o, int, float)
{
    if (o->LifeTime < 20)
        o->Alpha -= (0.05f) * FPS_ANIMATION_FACTOR;
    else if (o->Alpha < 1.0f)
        o->Alpha += (0.3f) * FPS_ANIMATION_FACTOR;
    return true;
}

// BITMAP_FIRE: continuously emits its fire particle from the owner.
bool MoveBehavior::MoveBitmapFire(OBJECT *o, int, float)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    CreateParticleFpsChecked(BITMAP_FIRE, o->Position, o->Angle, Light, 9, 1.f, o->Owner);
    return true;
}

// BITMAP_FIRE_RED: scatters red fire particles around its position.
bool MoveBehavior::MoveBitmapFireRed(OBJECT *o, int, float)
{
    vec3_t Position, Light;
    Vector((float)(WorldRandom() % 32 - 16), (float)(WorldRandom() % 32 - 16), 0.f, Position);
    VectorAdd(Position, o->Position, Position);
    Vector(1.0f, 0.4f, 0.4f, Light);
    CreateParticleFpsChecked(BITMAP_FIRE_RED, Position, o->Angle, Light, 0, o->Scale);
    return true;
}

// BITMAP_LIGHT_MARKS: dies with its owner, otherwise loops its lifetime.
bool MoveBehavior::MoveBitmapLightMarks(OBJECT *o, int, float)
{
    if (o->Owner == NULL || o->Owner->Live == false)
    {
        RetireEffect(o);
    }
    else if (o->LifeTime <= 5)
    {
        o->LifeTime = 65;
    }
    return true;
}

// MODEL_MAGIC1: hovers above the owner, spinning and fading over its life.
bool MoveBehavior::MoveMagic1(OBJECT *o, int, float)
{
    if (o->Owner == NULL)
    {
        o->LifeTime = 0;
        return true;
    }
    VectorCopy(o->Owner->Position, o->Position);
    o->Position[2] += (100.f) * FPS_ANIMATION_FACTOR;
    o->Angle[1] += (20.f) * FPS_ANIMATION_FACTOR;
    o->BlendMeshLight = o->LifeTime * 0.1f;
    return true;
}

// MODEL_MAYASTAR: pulses in size, spins, fades at end of life and shakes the screen.
bool MoveBehavior::MoveMayaStar(OBJECT *o, int, float)
{
    if (o->LifeTime <= 20)
    {
        o->Light[0] *= pow(1.0f / (1.2f), FPS_ANIMATION_FACTOR);
        o->Light[1] *= pow(1.0f / (1.2f), FPS_ANIMATION_FACTOR);
        o->Light[2] *= pow(1.0f / (1.2f), FPS_ANIMATION_FACTOR);
    }
    o->Scale += (sinf(WorldTime * 0.005f) * 2.0f) * FPS_ANIMATION_FACTOR;
    o->Angle[1] += (10.0f) * FPS_ANIMATION_FACTOR;
    EarthQuake = (float)(WorldRandom() % 6 - 6) * 0.5f;
    return true;
}
} // namespace GameLogic::Effects::Behaviors
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::MoveDesair(OBJECT *o, int index,
                                                                        float luminosity)
{
    return owner_.MoveDesair(o, index, luminosity);
} // OMF-01247
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::MoveInfinityArrow4(OBJECT *o,
                                                                                int index,
                                                                                float luminosity)
{
    return owner_.MoveInfinityArrow4(o, index, luminosity);
} // OMF-01248
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::MoveMagicCapsule2(OBJECT *o, int index,
                                                                               float luminosity)
{
    return owner_.MoveMagicCapsule2(o, index, luminosity);
} // OMF-01249
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::MoveSpear(OBJECT *o, int index,
                                                                       float luminosity)
{
    return owner_.MoveSpear(o, index, luminosity);
} // OMF-01250
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::MoveSummonerNeilNife(OBJECT *o,
                                                                                  int index,
                                                                                  float luminosity)
{
    return owner_.MoveSummonerNeilNife(o, index, luminosity);
} // OMF-01251
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::MoveSummonerNeilGround(
    OBJECT *o, int index, float luminosity)
{
    return owner_.MoveSummonerNeilGround(o, index, luminosity);
} // OMF-01252
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::MoveBitmapFire(OBJECT *o, int index,
                                                                            float luminosity)
{
    return owner_.MoveBitmapFire(o, index, luminosity);
} // OMF-01253
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::MoveBitmapFireRed(OBJECT *o, int index,
                                                                               float luminosity)
{
    return owner_.MoveBitmapFireRed(o, index, luminosity);
} // OMF-01254
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::MoveBitmapLightMarks(OBJECT *o,
                                                                                  int index,
                                                                                  float luminosity)
{
    return owner_.MoveBitmapLightMarks(o, index, luminosity);
} // OMF-01255
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::MoveMagic1(OBJECT *o, int index,
                                                                        float luminosity)
{
    return owner_.MoveMagic1(o, index, luminosity);
} // OMF-01256
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::MoveMayaStar(OBJECT *o, int index,
                                                                          float luminosity)
{
    return owner_.MoveMayaStar(o, index, luminosity);
} // OMF-01257

namespace
{
void SampleOwnerBone(const OBJECT &owner, BMD &model, int bone, double time, float fraction,
                     vec3_t position)
{
    AnimationPoseSample pose(&owner, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    vec3_t offset{};
    pose.SampleBonePosition(model, owner, bone, offset, time, fraction, position);
}

void AdvanceGroundedArrow(OBJECT &effect, const World &world, float frames)
{
    while (frames > 0.f)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            vec3_t angle;
            VectorCopy(effect.Angle, angle);
            angle[0] = (std::min)(50.f, angle[0] + (effect.SubType == 3 ? effect.Gravity : 0.f));
            effect.EffectMotionAngleRate[0] = angle[0] - effect.Angle[0];
            float matrix[3][4];
            AngleMatrix(angle, matrix);
            VectorRotate(effect.Direction, matrix, effect.EffectMotionVelocity);
            effect.EffectMotionFrames = 1.f;
        }
        float step = (std::min)(frames, effect.EffectMotionFrames);
        const auto contact =
            effect.EffectResting
                ? std::nullopt
                : world.FirstTerrainContact(effect.Position, effect.EffectMotionVelocity, 0.f, step,
                                            0.f, true);
        if (contact)
            step = contact->frames;
        VectorAddScaled(effect.Position, effect.EffectMotionVelocity, effect.Position, step);
        effect.Angle[0] += effect.EffectMotionAngleRate[0] * step;
        if (contact)
        {
            effect.Position[2] = contact->height + 10.f;
            Vector(0.f, 0.f, 0.f, effect.Direction);
            Vector(0.f, 0.f, 0.f, effect.EffectMotionVelocity);
            effect.EffectResting = true;
        }
        effect.EffectMotionFrames -= step;
        frames -= step;
        effect.MotionTrace.Advance(step, effect.Position);
    }
}

void AdvanceFuryStrike(OBJECT &effect, float frames)
{
    frames = (std::min)(frames, (std::max)(0.f, effect.LifeTime - 11.f));
    float elapsed = 0.f;
    while (frames > 0.f)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            const float life = static_cast<float>(std::lround(effect.LifeTime - elapsed));
            const bool held = life > 9.f && life < 16.f;
            const float count = held ? 12.5f : life;
            const float angle = (20.f - count) * (held ? 18.f : 15.f);
            vec3_t direction, endpoint;
            VectorCopy(effect.Direction, direction);
            direction[1] = std::sin(angle * Q_PI / 180.f) * 260.f;
            if (life == 15.f)
                effect.Gravity = -effect.Gravity;
            const float gravity = effect.Gravity + (held ? -8.f : 8.f);
            float matrix[3][4];
            AngleMatrix(effect.HeadAngle, matrix);
            VectorRotate(direction, matrix, endpoint);
            VectorAdd(endpoint, effect.StartPosition, endpoint);
            endpoint[2] += gravity + 200.f;
            VectorSubtract(endpoint, effect.Position, effect.EffectMotionVelocity);
            effect.EffectMotionAngleRate[1] = direction[1] - effect.Direction[1];
            effect.AmbientVerticalNoise = gravity - effect.Gravity;
            effect.EffectMotionFrames = 1.f;
        }
        const float step = (std::min)(frames, effect.EffectMotionFrames);
        VectorAddScaled(effect.Position, effect.EffectMotionVelocity, effect.Position, step);
        effect.Direction[1] += effect.EffectMotionAngleRate[1] * step;
        effect.Gravity += effect.AmbientVerticalNoise * step;
        effect.Angle[0] += 80.f * step;
        effect.EffectMotionFrames -= step;
        frames -= step;
        elapsed += step;
        effect.MotionTrace.Advance(step, effect.Position);
    }
}

void AdvanceApproachingGhost(OBJECT &effect, const OBJECT &hero, float frames, float totalFrames,
                             double worldTime)
{
    const float approachingFrames = frames;
    while (frames > 0.f)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            vec3_t target, endpoint;
            hero.MotionTrace.Sample(
                worldTime,
                effect.BirthTiming.FrameFraction((approachingFrames - frames) / totalFrames),
                hero.Position, target);
            target[0] += 300.f;
            target[1] -= 300.f;
            VectorCopy(effect.Position, endpoint);
            const float timer = effect.Timer + 1.f;
            // Preserve the authored ordered comparisons at each reference sample.
            for (int axis = 0; axis < 2; ++axis)
            {
                if (endpoint[axis] < target[axis])
                    endpoint[axis] += timer * 0.2f;
                if (endpoint[axis] > target[axis])
                    endpoint[axis] -= timer * 0.2f;
            }
            float yaw = effect.Angle[2];
            if (yaw < 45.f)
                yaw += timer * 0.3f;
            if (yaw > 45.f)
                yaw -= timer * 0.3f;
            VectorSubtract(endpoint, effect.Position, effect.EffectMotionVelocity);
            effect.EffectMotionAngleRate[2] = yaw - effect.Angle[2];
            effect.EffectMotionFrames = 1.f;
        }
        const float step = (std::min)(frames, effect.EffectMotionFrames);
        VectorAddScaled(effect.Position, effect.EffectMotionVelocity, effect.Position, step);
        effect.Angle[2] += effect.EffectMotionAngleRate[2] * step;
        effect.Timer += step;
        effect.EffectMotionFrames -= step;
        frames -= step;
        effect.MotionTrace.Advance(step, effect.Position);
    }
}

void AdvanceAcceleratingPitch(OBJECT &effect, float acceleration, float pitchRate, float frames)
{
    while (frames > 0.f)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            vec3_t direction, angle;
            VectorCopy(effect.Direction, direction);
            VectorCopy(effect.Angle, angle);
            direction[1] += acceleration;
            angle[0] += pitchRate;
            float matrix[3][4];
            AngleMatrix(angle, matrix);
            VectorRotate(direction, matrix, effect.EffectMotionVelocity);
            effect.EffectMotionFrames = 1.f;
        }
        const float step = (std::min)(frames, effect.EffectMotionFrames);
        VectorAddScaled(effect.Position, effect.EffectMotionVelocity, effect.Position, step);
        effect.Direction[1] += acceleration * step;
        effect.Angle[0] += pitchRate * step;
        effect.EffectMotionFrames -= step;
        frames -= step;
        effect.MotionTrace.Advance(step, effect.Position);
    }
}

template <class Emit>
void AdvanceJavelin(OBJECT &effect, float frames, SessionRandom &random, double worldTime,
                    Emit emit)
{
    constexpr float ChaseLife = 25.f, HomeLife = 20.f, ArrivalRadius = 100.f;
    constexpr float Acceleration = 1.5f, DirectionAcceleration = -8.f, DirectionLimit = -50.f;
    const float totalFrames = frames;
    float elapsed = 0.f;
    while (frames > 0.f)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            if (effect.Owner)
            {
                effect.Owner->MotionTrace.Sample(
                    worldTime, effect.BirthTiming.FrameFraction(elapsed / totalFrames),
                    effect.Owner->Position, effect.StartPosition);
                effect.StartPosition[2] += 150.f;
            }
            const float life = Core::Time::ReferenceSample(effect.LifeTime - elapsed);
            vec3_t position, angle;
            VectorCopy(effect.Position, position);
            VectorCopy(effect.HeadAngle, angle);
            effect.AmbientSteering = false;
            if (life < HomeLife)
            {
                const float distance =
                    ::MoveHumming(position, angle, effect.StartPosition, effect.Velocity);
                effect.AmbientSteering = distance < ArrivalRadius;
                if (effect.AmbientSteering)
                    VectorCopy(effect.StartPosition, position);
            }
            float matrix[3][4];
            vec3_t travel;
            AngleMatrix(angle, matrix);
            VectorRotate(effect.Direction, matrix, travel);
            VectorAdd(position, travel, position);
            VectorSubtract(position, effect.Position, effect.EffectMotionVelocity);
            for (int axis = 0; axis < 3; ++axis)
                effect.EffectMotionAngleRate[axis] =
                    std::remainder(angle[axis] - effect.HeadAngle[axis], 360.f);
            effect.AmbientSpeedNoise = life < ChaseLife ? Acceleration : 0.f;
            effect.AmbientVerticalNoise =
                life < ChaseLife
                    ? (std::max)(DirectionLimit, effect.Direction[1] + DirectionAcceleration) -
                          effect.Direction[1]
                    : 0.f;
            effect.AmbientTurnRate =
                life < ChaseLife ? random.RangeInt(0, RAND_MAX) % 30 + 30.f : effect.Gravity + 10.f;
            effect.AmbientNoiseFrames = effect.AmbientTurnRate - effect.Gravity;
            effect.EffectMotionFrames = 1.f;
        }
        const float step = (std::min)(frames, effect.EffectMotionFrames);
        const float referenceLife = Core::Time::ReferenceSample(effect.LifeTime - elapsed + 1.f -
                                                                effect.EffectMotionFrames);
        VectorAddScaled(effect.Position, effect.EffectMotionVelocity, effect.Position, step);
        VectorAddScaled(effect.HeadAngle, effect.EffectMotionAngleRate, effect.HeadAngle, step);
        effect.Velocity += effect.AmbientSpeedNoise * step;
        effect.Direction[1] += effect.AmbientVerticalNoise * step;
        effect.Gravity += effect.AmbientNoiseFrames * step;
        effect.Angle[2] += effect.AmbientTurnRate * step;
        effect.Scale += (0.015f + (effect.AmbientSteering ? 0.04f : 0.f)) * step;
        effect.EffectMotionFrames -= step;
        frames -= step;
        elapsed += step;
        // The authored vertical orbit replaces flight height after each reference step.
        if (effect.SubType == 1 || effect.SubType == 2)
        {
            const float height = std::sin((effect.LifeTime - elapsed + 1.f) * 0.1f) * 30.f;
            float anchorHeight = effect.StartPosition[2];
            if (effect.Owner)
            {
                vec3_t ownerPosition;
                effect.Owner->MotionTrace.Sample(
                    worldTime, effect.BirthTiming.FrameFraction(elapsed / totalFrames),
                    effect.Owner->Position, ownerPosition);
                anchorHeight = ownerPosition[2] + 150.f;
            }
            effect.Position[2] = anchorHeight + (effect.SubType == 1 ? height : -height);
        }
        effect.MotionTrace.Advance(step, effect.Position);
        if (referenceLife < HomeLife)
            emit(elapsed - step, step, effect.AmbientSteering, referenceLife);
    }
}

std::optional<float> AdvanceLightningShock(OBJECT &effect, const World &world, float frames,
                                           double worldTime, const BMD &ownerModel)
{
    constexpr float ReleaseLife = 15.f, ReleaseAnimation = 6.f, GravityChange = 0.1f;
    const float totalFrames = frames;
    float elapsed = 0.f;
    while (frames > 0.f)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            auto phase = effect.Owner->MotionTrace.SampleAnimation(
                worldTime, effect.BirthTiming.FrameFraction(elapsed / totalFrames),
                {effect.Owner->AnimationFrame, effect.Owner->PriorAnimationFrame,
                 effect.Owner->CurrentAction, effect.Owner->PriorAction});
            const auto &action =
                ownerModel.Actions[phase.action == 255 ? phase.priorAction : phase.action];
            if (!action.Loop)
            {
                const float cycle = static_cast<float>(
                    (std::max)(1, action.NumAnimationKeys - (action.LockPositions ? 1 : 0)));
                phase.frame = std::fmod(phase.frame, cycle);
                if (phase.frame < 0.f)
                    phase.frame += cycle;
            }
            effect.AmbientSteering = phase.frame > ReleaseAnimation ||
                                     std::lround(effect.LifeTime - elapsed) < ReleaseLife;
            if (effect.AmbientSteering)
                Vector(0.f, -20.f, -75.f - effect.Velocity, effect.Direction);
            float matrix[3][4];
            AngleMatrix(effect.Angle, matrix);
            VectorRotate(effect.Direction, matrix, effect.EffectMotionVelocity);
            effect.AmbientVerticalNoise =
                effect.AmbientSteering ? effect.Gravity + GravityChange : 0.f;
            effect.EffectMotionFrames = 1.f;
        }
        const float duration = (std::min)(frames, effect.EffectMotionFrames);
        const auto contact =
            world.FirstTerrainContact(effect.Position, effect.EffectMotionVelocity, 0.f, duration);
        const float step = contact ? contact->frames : duration;
        VectorAddScaled(effect.Position, effect.EffectMotionVelocity, effect.Position, step);
        effect.Velocity += effect.AmbientVerticalNoise * step;
        if (effect.AmbientSteering)
            effect.Gravity += GravityChange * step;
        effect.EffectMotionFrames -= step;
        frames -= step;
        elapsed += step;
        effect.MotionTrace.Advance(step, effect.Position);
        if (contact)
        {
            effect.Position[2] = contact->height;
            return elapsed;
        }
    }
    return std::nullopt;
}

void AdvanceGuildProtection(OBJECT &effect, float frames)
{
    constexpr float FadeInStart = 120.f, FadeInEnd = 95.f, FadeOutStart = 50.f;
    constexpr float FadeInRate = 0.4f, FadeOutRate = 0.1f, TurnRate = 5.f;
    float life = effect.LifeTime;
    while (frames > 0.f)
    {
        float step = frames, rate = 0.f;
        if (life > FadeInStart)
            step = (std::min)(step, life - FadeInStart);
        else if (life > FadeInEnd)
        {
            step = (std::min)(step, life - FadeInEnd);
            rate = FadeInRate;
        }
        else if (life > FadeOutStart)
            step = (std::min)(step, life - FadeOutStart);
        else
            rate = -FadeOutRate;
        const float turning =
            rate > 0.f ? (std::max)(0.f, step - (std::max)(0.f, (1.f - effect.Alpha) / rate))
                       : (rate == 0.f && effect.Alpha >= 1.f ? step : 0.f);
        effect.Angle[2] += TurnRate * turning;
        effect.Alpha = std::clamp(effect.Alpha + rate * step, 0.f, 1.f);
        frames -= step;
        life -= step;
    }
}

void AdvanceTargetFade(OBJECT &effect, float &opacity, float frames)
{
    constexpr float FadeStart = 10.f, FadeRate = 0.05f;
    float remaining = (std::min)(frames, (std::max)(0.f, FadeStart - (effect.LifeTime - frames)));
    while (remaining > 0.f)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            effect.AmbientVerticalNoise = (std::max)(0.f, opacity - FadeRate);
            effect.EffectMotionFrames = 1.f;
        }
        const float step = (std::min)(remaining, effect.EffectMotionFrames);
        opacity = (std::max)(0.f, opacity - FadeRate * step);
        VectorScale(effect.Light, std::pow(effect.AmbientVerticalNoise, step), effect.Light);
        effect.EffectMotionFrames -= step;
        remaining -= step;
    }
}

void AdvanceTargetPulse(OBJECT &effect, float frames)
{
    if (frames <= 0.f)
        return;
    constexpr float Minimum = 0.8f, Maximum = 1.8f, Speed = 0.15f;
    constexpr double Width = Maximum - Minimum;
    const double height = std::clamp(double(effect.Scale - Minimum), 0.0, Width);
    const bool rising =
        effect.Scale < Maximum && (effect.Scale <= Minimum || effect.m_iAnimation == 1);
    const double phase =
        std::fmod((rising ? height : 2.0 * Width - height) + Speed * double(frames), 2.0 * Width);
    effect.Scale = Minimum + static_cast<float>(phase <= Width ? phase : 2.0 * Width - phase);
    effect.m_iAnimation = phase < Width ? 1 : 0;
}

void AdvanceFadingMagic(OBJECT &effect, float frames)
{
    constexpr float AlphaChange = 0.05f, FadeScale = 5.f;
    while (frames > 0.f)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            effect.AmbientVerticalNoise = (std::max)(0.f, effect.Alpha - AlphaChange);
            effect.EffectMotionFrames = 1.f;
        }
        const float step = (std::min)(frames, effect.EffectMotionFrames);
        const float rate = effect.AmbientVerticalNoise;
        const float untilFade = effect.Scale >= FadeScale
                                    ? 0.f
                                    : (rate > 0.f ? (FadeScale - effect.Scale) / rate : step);
        effect.Scale += rate * step;
        effect.Alpha = (std::max)(0.f, effect.Alpha - AlphaChange * step);
        VectorScale(effect.Light, std::pow(0.5f, (std::max)(0.f, step - untilFade)), effect.Light);
        effect.EffectMotionFrames -= step;
        frames -= step;
    }
}

void AdvanceDarkElfPulse(OBJECT &effect, float frames)
{
    constexpr float SwitchScale = 0.8f, Growth = 0.2f, Shrink = -0.4f;
    while (frames > 0.f)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            effect.AmbientVerticalNoise = effect.Scale < SwitchScale ? Growth : Shrink;
            effect.EffectMotionVelocity[0] = effect.Scale + effect.AmbientVerticalNoise;
            effect.EffectMotionFrames = 1.f;
        }
        const float step = (std::min)(frames, effect.EffectMotionFrames);
        effect.Scale += effect.AmbientVerticalNoise * step;
        effect.EffectMotionFrames -= step;
        if (effect.EffectMotionFrames <= 0.f)
            effect.Scale = effect.EffectMotionVelocity[0];
        frames -= step;
    }
}

void AdvanceFlyingIce(OBJECT &effect, float frames, SessionRandom &random)
{
    const bool sapitres = effect.Type == MODEL_EFFECT_SAPITRES_ATTACK_2;
    const float gravityChange = effect.SubType == 1 || !sapitres ? 0.5f : 0.f;
    const float yawRate = effect.SubType != 1 && !sapitres ? 20.f : 0.f;
    while (frames > 0.f)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            float matrix[3][4];
            if (sapitres && effect.SubType == 14)
            {
                VectorScale(effect.Direction, -(random.RangeInt(0, RAND_MAX) % 5 + 25.f),
                            effect.EffectMotionVelocity);
            }
            else
            {
                AngleMatrix(effect.Angle, matrix);
                VectorRotate(effect.Direction, matrix, effect.EffectMotionVelocity);
            }
            vec3_t angle, direction, second;
            VectorCopy(effect.Angle, angle);
            angle[2] += yawRate;
            VectorScale(effect.Direction, 0.9f, direction);
            AngleMatrix(angle, matrix);
            VectorRotate(direction, matrix, second);
            VectorAdd(effect.EffectMotionVelocity, second, effect.EffectMotionVelocity);
            effect.EffectMotionVelocity[2] += effect.Gravity;
            effect.EffectMotionFrames = 1.f;
        }
        const float step = (std::min)(frames, effect.EffectMotionFrames);
        VectorAddScaled(effect.Position, effect.EffectMotionVelocity, effect.Position, step);
        VectorScale(effect.Direction, std::pow(0.9f, step), effect.Direction);
        effect.Gravity += gravityChange * step;
        effect.Angle[2] += yawRate * step;
        effect.EffectMotionFrames -= step;
        frames -= step;
        effect.MotionTrace.Advance(step, effect.Position);
    }
}

void AdvanceBouncingCatapult(OBJECT &effect, const World &world, float frames,
                             SessionRandom &random)
{
    constexpr float Acceleration = 1.9f;
    float matrix[3][4];
    AngleMatrix(effect.HeadAngle, matrix);
    while (frames > 0.f)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            effect.EffectMotionAngleRate[0] = random.RangeInt(0, RAND_MAX) % 20 + 20.f;
            effect.EffectMotionAngleRate[1] = static_cast<float>(random.RangeInt(0, RAND_MAX) % 5);
            effect.EffectMotionFrames = 1.f;
        }
        vec3_t travel;
        VectorRotate(effect.Direction, matrix, travel);
        const float duration = (std::min)(frames, effect.EffectMotionFrames);
        vec3_t velocity{travel[0], travel[1], travel[2] - effect.Gravity + Acceleration * 0.5f};
        const auto contact =
            world.FirstTerrainContact(effect.Position, velocity, -Acceleration, duration);
        const float step = contact ? contact->frames : duration;
        float fall = 0.f;
        Core::Time::Advance(fall, effect.Gravity, Acceleration, step);
        VectorAddScaled(effect.Position, travel, effect.Position, step);
        effect.Position[2] -= fall;
        effect.Angle[0] += (effect.EffectMotionAngleRate[0] - effect.Scale * 16.f) * step;
        effect.Angle[1] += effect.EffectMotionAngleRate[1] * step;
        effect.EffectMotionFrames -= step;
        frames -= step;
        if (!contact)
            continue;
        effect.Position[2] = contact->height + 50.f;
        const float incoming =
            travel[2] - effect.Gravity + Acceleration * 0.5f - contact->surfaceVelocity;
        const float rebound = (std::max)(0.f, -incoming * 0.2f);
        Vector(0.f, 5.f, 0.f, effect.Direction);
        VectorRotate(effect.Direction, matrix, travel);
        effect.Gravity = travel[2] + Acceleration * 0.5f - rebound -
                         world.TerrainHeightRate(effect.Position, travel);
        effect.LifeTime -= 5.f;
        effect.Angle[0] -= effect.Scale * 112.f;
    }
}

void AdvanceSkullFlight(OBJECT &effect, const World &world, float frames, SessionRandom &random,
                        double worldTime)
{
    const float totalFrames = frames;
    float elapsed = 0.f;
    while (frames > 0.f)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            effect.Owner->MotionTrace.Sample(
                worldTime, effect.BirthTiming.FrameFraction(elapsed / totalFrames),
                effect.Owner->Position, effect.StartPosition);
            effect.StartPosition[2] += 200.f;
            vec3_t angle;
            VectorCopy(effect.Angle, angle);
            ::MoveHumming(effect.Position, angle, effect.StartPosition, 10.f);
            float pitch = effect.HeadAngle[0] + (random.RangeInt(0, RAND_MAX) % 32 - 16) * 0.2f;
            float yaw = effect.HeadAngle[2] + (random.RangeInt(0, RAND_MAX) % 32 - 16) * 0.8f;
            angle[0] += pitch;
            angle[2] += yaw;
            pitch *= 0.6f;
            yaw *= 0.8f;
            effect.AmbientSteering = std::lround(effect.LifeTime - elapsed) < 10;
            if (effect.AmbientSteering)
            {
                if (angle[0] <= 80.f)
                    angle[0] += 5.f;
            }
            else
            {
                const float height =
                    world.SampleTerrainHeight(effect.Position[0], effect.Position[1]);
                if (effect.Position[2] < height + 100.f || effect.Position[2] > height + 400.f)
                {
                    pitch = 0.f;
                    angle[0] = effect.Position[2] < height + 100.f ? -5.f : 5.f;
                }
            }
            effect.AmbientVerticalNoise = pitch - effect.HeadAngle[0];
            effect.AmbientTurnRate = yaw - effect.HeadAngle[2];
            for (int axis = 0; axis < 3; ++axis)
                effect.EffectMotionAngleRate[axis] =
                    std::remainder(angle[axis] - effect.Angle[axis], 360.f);
            float matrix[3][4];
            AngleMatrix(angle, matrix);
            VectorRotate(effect.Direction, matrix, effect.EffectMotionVelocity);
            effect.EffectMotionFrames = 1.f;
        }
        const float step = (std::min)(frames, effect.EffectMotionFrames);
        VectorAddScaled(effect.Position, effect.EffectMotionVelocity, effect.Position, step);
        VectorAddScaled(effect.Angle, effect.EffectMotionAngleRate, effect.Angle, step);
        effect.HeadAngle[0] += effect.AmbientVerticalNoise * step;
        effect.HeadAngle[2] += effect.AmbientTurnRate * step;
        if (effect.AmbientSteering)
        {
            effect.Alpha *= std::pow(1.f / 1.1f, step);
            effect.BlendMeshLight *= std::pow(1.f / 1.2f, step);
        }
        effect.EffectMotionFrames -= step;
        elapsed += step;
        frames -= step;
        effect.MotionTrace.Advance(step, effect.Position);
    }
}

struct HeadDebrisAdvance
{
    float groundFrames = 0.f;
    float airLife = 0.f;
};

HeadDebrisAdvance AdvanceHeadDebris(OBJECT &effect, const World &world, float frames,
                                    float clearance, float kick, float damping, float minimumSpeed,
                                    double scalePhaseEnd = 0., double scalePhasePerFrame = 0.)
{
    HeadDebrisAdvance result;
    float elapsed = 0.f;
    const auto launch = [&](float life, float surfaceVelocity) {
        const float speed = effect.HeadAngle[2] - surfaceVelocity + kick * life;
        effect.EffectResting = speed < minimumSpeed || speed <= effect.Gravity * 0.5f;
        effect.HeadAngle[2] = effect.EffectResting ? 0.f : speed + surfaceVelocity;
        effect.EffectMotionFrames = effect.EffectResting ? 1.f : 0.f;
    };
    while (frames > 0.f)
    {
        if (effect.EffectResting)
        {
            const float step = (std::min)(frames, effect.EffectMotionFrames);
            const float travel = Core::Time::DampedDistance(damping, step);
            effect.Position[0] += effect.HeadAngle[0] * travel;
            effect.Position[1] += effect.HeadAngle[1] * travel;
            effect.HeadAngle[0] *= std::pow(damping, step);
            effect.HeadAngle[1] *= std::pow(damping, step);
            effect.Position[2] =
                world.SampleTerrainHeight(effect.Position[0], effect.Position[1]) + clearance;
            result.groundFrames += step;
            effect.EffectMotionFrames -= step;
            elapsed += step;
            frames -= step;
            if (effect.EffectMotionFrames <= 0.f)
            {
                const float surfaceVelocity =
                    world.TerrainHeightRate(effect.Position, effect.HeadAngle);
                effect.HeadAngle[2] = surfaceVelocity - effect.Gravity;
                launch(effect.LifeTime - elapsed + 1.f, surfaceVelocity);
            }
            continue;
        }
        vec3_t velocity{effect.HeadAngle[0], effect.HeadAngle[1],
                        effect.HeadAngle[2] - effect.Gravity * 0.5f};
        const auto contact = world.FirstTerrainContact(effect.Position, velocity, -effect.Gravity,
                                                       frames, clearance);
        const float step = contact ? contact->frames : frames;
        effect.Position[0] += effect.HeadAngle[0] * step;
        effect.Position[1] += effect.HeadAngle[1] * step;
        float vertical = effect.HeadAngle[2] - effect.Gravity;
        Core::Time::Advance(effect.Position[2], vertical, -effect.Gravity, step);
        effect.HeadAngle[2] = vertical + effect.Gravity;
        if (effect.Type == MODEL_DOPPELGANGER_SLIME_CHIP)
        {
            const double phase = scalePhaseEnd - frames * scalePhasePerFrame;
            effect.Scale += static_cast<float>(
                0.1 * std::sin(phase + scalePhasePerFrame * (step + 1.) * 0.5) *
                std::sin(scalePhasePerFrame * step * 0.5) / std::sin(scalePhasePerFrame * 0.5));
        }
        result.airLife += step * (effect.LifeTime - elapsed - (step - 1.f) * 0.5f);
        elapsed += step;
        frames -= step;
        if (!contact)
            continue;
        effect.Position[2] = contact->height;
        effect.HeadAngle[0] *= damping;
        effect.HeadAngle[1] *= damping;
        result.groundFrames += 1.f;
        const float surfaceVelocity = world.TerrainHeightRate(effect.Position, effect.HeadAngle);
        effect.HeadAngle[2] += surfaceVelocity - contact->surfaceVelocity;
        launch(effect.LifeTime - elapsed + 1.f, surfaceVelocity);
    }
    return result;
}

bool AdvanceFallingPart(OBJECT &effect, float acceleration, float floor, float &frames)
{
    float matrix[3][4];
    AngleMatrix(effect.Angle, matrix);
    vec3_t velocity;
    VectorRotate(effect.Direction, matrix, velocity);
    const double height = double(effect.Position[2]) - floor;
    const double speed = velocity[2] + matrix[2][2] * acceleration * 0.5;
    const double force = matrix[2][2] * acceleration;
    double arrival = height <= 0. ? 0. : std::numeric_limits<double>::infinity();
    if (height > 0.)
    {
        if (force == 0.)
        {
            if (speed < 0.)
                arrival = -height / speed;
        }
        else
        {
            const double determinant = speed * speed - 2. * force * height;
            if (determinant >= 0.)
            {
                const double q = -0.5 * (speed + std::copysign(std::sqrt(determinant), speed));
                const double roots[]{q / (force * 0.5), height / q};
                for (const double root : roots)
                    if (root >= 0. && speed + force * root <= 0.)
                        arrival = (std::min)(arrival, root);
            }
        }
    }
    const bool reached = arrival <= frames;
    if (reached)
        frames = static_cast<float>(arrival);
    float vertical = effect.Direction[2] + acceleration, travel = 0.f;
    Core::Time::Advance(travel, vertical, acceleration, frames);
    effect.Direction[2] = vertical - acceleration;
    vec3_t local{effect.Direction[0] * frames, effect.Direction[1] * frames, travel}, movement;
    VectorRotate(local, matrix, movement);
    VectorAdd(effect.Position, movement, effect.Position);
    if (reached)
        effect.Position[2] = floor;
    effect.MotionTrace.Advance(frames, effect.Position);
    return reached;
}

void AdvanceSinkingPart(OBJECT &effect, float frames, float elapsed, SessionRandom &random)
{
    while (frames > 0.f)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            float turn = 0.f;
            if (effect.HeadAngle[0] == 0.f)
            {
                if (effect.Angle[0] > -30.f)
                    turn = -(random.RangeInt(0, RAND_MAX) % 3 + 1.f);
                else
                    effect.HeadAngle[0] = 1.f;
            }
            else
            {
                if (effect.Angle[0] < 30.f)
                    turn = random.RangeInt(0, RAND_MAX) % 3 + 1.f;
                else
                    effect.HeadAngle[0] = 0.f;
            }
            effect.EffectMotionAngleRate[0] = turn;
            vec3_t angle;
            VectorCopy(effect.Angle, angle);
            angle[0] += turn;
            float matrix[3][4];
            AngleMatrix(angle, matrix);
            VectorRotate(effect.Direction, matrix, effect.EffectMotionVelocity);
            effect.EffectMotionFrames = 1.f;
        }
        const float step = (std::min)(frames, effect.EffectMotionFrames);
        VectorAddScaled(effect.Position, effect.EffectMotionVelocity, effect.Position, step);
        effect.Angle[0] += effect.EffectMotionAngleRate[0] * step;
        const float fading =
            (std::max)(0.f, step - (std::max)(0.f, effect.LifeTime - elapsed - 99.f));
        effect.Alpha -= 0.1f * fading;
        effect.EffectMotionFrames -= step;
        elapsed += step;
        frames -= step;
        effect.MotionTrace.Advance(step, effect.Position);
    }
}

void AdvanceDragonFlight(OBJECT &effect, float frames)
{
    while (frames > 0.f)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            effect.Angle[1] = std::fmod(effect.Angle[1], 360.f);
            effect.Angle[2] = std::fmod(effect.Angle[2], 360.f);
            vec3_t angle, range;
            VectorCopy(effect.Angle, angle);
            VectorSubtract(effect.Position, effect.StartPosition, range);
            effect.Timer = effect.Kind == 0 ? 30.f : (effect.Kind == 1 ? -30.f : 0.f);
            angle[1] += std::clamp(effect.Timer - angle[1], -0.5f, 0.5f);
            if (VectorLength(range) <= 800.f)
            {
                effect.Kind = 2;
                effect.Distance = 0.f;
                effect.CollisionRange = true;
            }
            else
            {
                if (effect.CollisionRange)
                {
                    effect.Timer = angle[2];
                    effect.Gravity = -effect.Gravity;
                    effect.CollisionRange = false;
                }
                if (effect.Timer >= angle[2] - 60.f)
                {
                    angle[2] += 1.5f * effect.Gravity;
                    effect.Kind = 1;
                }
                else if (effect.Timer <= angle[2] + 60.f)
                {
                    angle[2] += 1.5f * effect.Gravity;
                    effect.Kind = 0;
                }
                else
                {
                    effect.CollisionRange = true;
                    effect.Kind = 2;
                }
            }
            VectorSubtract(angle, effect.Angle, effect.EffectMotionAngleRate);
            float matrix[3][4];
            AngleMatrix(angle, matrix);
            VectorRotate(effect.Direction, matrix, effect.EffectMotionVelocity);
            effect.EffectMotionFrames = 1.f;
        }
        const float step = (std::min)(frames, effect.EffectMotionFrames);
        VectorAddScaled(effect.Position, effect.EffectMotionVelocity, effect.Position, step);
        VectorAddScaled(effect.Angle, effect.EffectMotionAngleRate, effect.Angle, step);
        effect.EffectMotionFrames -= step;
        frames -= step;
    }
}

void AdvanceEffectGravity(OBJECT &effect, float direction, float acceleration, float frames)
{
    float travel = 0.f;
    Core::Time::Advance(travel, effect.Gravity, acceleration, frames);
    effect.Position[2] += direction * travel;
}
void PrepareHomingStep(OBJECT &effect, vec3_t target, float duration, float repetitions,
                       float acceleration, float pitchRate)
{
    const float steering = duration * repetitions;
    vec3_t angle, midpoint;
    VectorCopy(effect.Angle, angle);
    angle[0] += pitchRate * steering;
    ::MoveHumming(effect.Position, angle, target,
                  (effect.Velocity + acceleration * steering * 0.5f) * steering);
    for (int axis = 0; axis < 3; ++axis)
    {
        const float difference = FarAngle(effect.Angle[axis], angle[axis], false);
        effect.EffectMotionAngleRate[axis] = difference / duration;
        midpoint[axis] = effect.Angle[axis] + difference * 0.5f;
    }
    float matrix[3][4];
    AngleMatrix(midpoint, matrix);
    VectorRotate(effect.Direction, matrix, effect.EffectMotionVelocity);
    VectorScale(effect.EffectMotionVelocity, repetitions, effect.EffectMotionVelocity);
    effect.EffectMotionFrames = duration;
}

void AdvanceHomingEffect(OBJECT &effect, vec3_t target, float acceleration, float frames,
                         double worldTime)
{
    constexpr float SteeringInterval = 1.f / 16.f;
    const float totalFrames = frames;
    vec3_t offset;
    VectorSubtract(target, effect.Owner->Position, offset);
    while (frames > 0.f)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            effect.Owner->MotionTrace.Sample(
                worldTime, effect.BirthTiming.FrameFraction((totalFrames - frames) / totalFrames),
                effect.Owner->Position, target);
            VectorAdd(target, offset, target);
            PrepareHomingStep(effect, target, SteeringInterval, 1.f, acceleration, 0.f);
        }
        const float step = (std::min)(frames, effect.EffectMotionFrames);
        VectorAddScaled(effect.Position, effect.EffectMotionVelocity, effect.Position, step);
        effect.MotionTrace.TurnYaw(step, effect.Angle[2],
                                   effect.Angle[2] + effect.EffectMotionAngleRate[2] * step, 0.f);
        VectorAddScaled(effect.Angle, effect.EffectMotionAngleRate, effect.Angle, step);
        effect.Velocity += acceleration * step;
        effect.EffectMotionFrames -= step;
        frames -= step;
        effect.MotionTrace.Advance(step, effect.Position);
    }
}

template <class Emission>
void AdvanceHomingSpiral(OBJECT &effect, vec3_t target, SessionRandom &random, float frames,
                         double worldTime, Emission &&emit)
{
    constexpr float SteeringInterval = 1.f / 16.f, Growth = 0.1f, LateLife = 9.f;
    const float totalFrames = frames;
    vec3_t offset;
    VectorSubtract(target, effect.Owner->Position, offset);
    float elapsed = 0.f;
    while (frames > 0.f)
    {
        if (effect.AmbientNoiseFrames <= 0.f)
        {
            effect.AmbientSpeedNoise = (std::max)(0.f, std::ceil(effect.Gravity) - 1.f);
            effect.AmbientNoiseFrames = 1.f;
        }
        const float repetitions = effect.AmbientSpeedNoise;
        if (repetitions == 0.f)
        {
            const float step = (std::min)(frames, effect.AmbientNoiseFrames);
            effect.Gravity += Growth * step;
            effect.AmbientNoiseFrames -= step;
            elapsed += step;
            frames -= step;
            effect.MotionTrace.Advance(step, effect.Position);
            continue;
        }
        const float life = effect.LifeTime - elapsed;
        const bool late = life <= LateLife;
        const float acceleration = late ? 0.5f : 0.4f;
        if (effect.AmbientVerticalNoise <= 0.f)
        {
            effect.AmbientTurnRate =
                random.Next() % 2 == 0 ? (effect.Angle[0] < -90.f ? 20.f : -20.f) : 0.f;
            effect.AmbientVerticalNoise = 1.f; // One original inner-loop iteration.
        }
        if (effect.EffectMotionFrames <= 0.f)
        {
            float duration = (std::min)({SteeringInterval, effect.AmbientVerticalNoise,
                                         effect.AmbientNoiseFrames * repetitions}) /
                             repetitions;
            if (!late)
                duration = (std::min)(duration, life - LateLife);
            effect.Owner->MotionTrace.Sample(
                worldTime, effect.BirthTiming.FrameFraction(elapsed / totalFrames),
                effect.Owner->Position, target);
            VectorAdd(target, offset, target);
            PrepareHomingStep(effect, target, duration, repetitions, acceleration,
                              effect.AmbientTurnRate);
        }
        const float step = (std::min)(frames, effect.EffectMotionFrames);
        const float iterations = step * repetitions;
        VectorAddScaled(effect.Position, effect.EffectMotionVelocity, effect.Position, step);
        effect.MotionTrace.TurnYaw(step, effect.Angle[2],
                                   effect.Angle[2] + effect.EffectMotionAngleRate[2] * step, 0.f);
        VectorAddScaled(effect.Angle, effect.EffectMotionAngleRate, effect.Angle, step);
        effect.Velocity += acceleration * iterations;
        effect.Gravity += Growth * step;
        effect.EffectMotionFrames -= step;
        effect.AmbientNoiseFrames -= step;
        effect.AmbientVerticalNoise -= iterations;
        elapsed += step;
        frames -= step;
        effect.MotionTrace.Advance(step, effect.Position);
        if (effect.AmbientVerticalNoise <= 4.f * std::numeric_limits<float>::epsilon())
        {
            effect.AmbientVerticalNoise = 0.f;
            emit(totalFrames - frames, late);
        }
    }
}

void AdvanceRandomRotatingEffect(OBJECT &effect, SessionRandom &random, float frames, int axis,
                                 int range, float base)
{
    while (frames > 0.f)
    {
        if (effect.AmbientNoiseFrames <= 0.f)
        {
            effect.AmbientTurnRate = base + float(random.Next() % range);
            effect.AmbientNoiseFrames = 1.f;
        }
        const float step = (std::min)(frames, effect.AmbientNoiseFrames);
        if (axis == 2)
            effect.MotionTrace.TurnYaw(step, effect.Angle[2],
                                       effect.Angle[2] + effect.AmbientTurnRate * step, 0.f);
        MoveRotatingPosition(effect.Position, effect.Angle, effect.Direction, axis,
                             effect.AmbientTurnRate, step);
        effect.MotionTrace.Advance(step, effect.Position);
        effect.AmbientNoiseFrames -= step;
        frames -= step;
    }
}

void AdvanceShockWaveDrift(OBJECT &effect, SessionRandom &random, float frames)
{
    while (frames > 0.f)
    {
        if (effect.AmbientNoiseFrames <= 0.f)
        {
            effect.AmbientTurnRate = float(random.Next() % 5) * 0.1f;
            effect.AmbientVerticalNoise = float(int(random.Next() % 8) - 4);
            effect.AmbientSpeedNoise = float(int(random.Next() % 8) - 4);
            effect.AmbientNoiseFrames = 1.f;
        }
        const float step = (std::min)(frames, effect.AmbientNoiseFrames);
        effect.Scale += effect.AmbientTurnRate * step;
        effect.Position[0] += effect.AmbientVerticalNoise * step;
        effect.Position[1] += effect.AmbientSpeedNoise * step;
        effect.AmbientNoiseFrames -= step;
        frames -= step;
    }
}

float AdvanceCatapultFlight(OBJECT &effect, const World &world, float frames)
{
    constexpr float Deceleration = 0.45f, MinimumSpeed = 5.f, GravityChange = -0.1f;
    float animationAdvance = 0.f;
    float elapsed = 0.f;
    while (frames > 0.f && !effect.EffectResting)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            Vector(effect.Direction[0] * effect.Velocity, effect.Direction[1] * effect.Velocity,
                   effect.Direction[2], effect.EffectMotionVelocity);
            effect.EffectMotionFrames = 1.f;
        }
        const float duration = (std::min)(frames, effect.EffectMotionFrames);
        effect.Position[2] = effect.StartPosition[2];
        const auto contact =
            world.FirstTerrainContact(effect.Position, effect.EffectMotionVelocity, 0.f, duration);
        const float step = contact ? contact->frames : duration;
        VectorAddScaled(effect.Position, effect.EffectMotionVelocity, effect.Position, step);
        effect.StartPosition[2] = effect.Position[2];
        const float referenceEndSpeed =
            (std::max)(MinimumSpeed, effect.Velocity - Deceleration * effect.EffectMotionFrames);
        animationAdvance += referenceEndSpeed * step;
        effect.Velocity = (std::max)(MinimumSpeed, effect.Velocity - Deceleration * step);
        Core::Time::Advance(effect.Direction[2], effect.Gravity, GravityChange, step);
        effect.Angle[0] += 5.f * step;
        effect.Angle[1] += 5.f * step;
        effect.EffectMotionFrames -= step;
        elapsed += step;
        frames -= step;
        if (contact)
        {
            effect.Position[2] = contact->height;
            effect.StartPosition[2] = contact->height;
            effect.EffectResting = true;
        }
    }
    effect.EffectAnimationAdvance = animationAdvance + effect.Velocity * frames;
    return elapsed;
}

float AdvanceBlizzardMotion(OBJECT &effect, const World &world, SessionRandom &random, float frames)
{
    const float totalFrames = frames;
    const float radius = effect.SubType == 2 ? 50.f : 10.f;
    while (frames > 0.f && !effect.EffectResting)
    {
        if (effect.EffectMotionFrames <= 0.f)
        {
            effect.EffectMotionVelocity[0] =
                effect.StartPosition[0] + std::sin(float(random.Next() % 1000) * 0.01f) * radius -
                effect.Position[0];
            effect.EffectMotionVelocity[1] =
                effect.StartPosition[1] + std::sin(float(random.Next() % 1000) * 0.01f) * radius -
                effect.Position[1];
            effect.AmbientVerticalNoise = -float(random.Next() % 5);
            effect.EffectMotionFrames = 1.f;
        }
        const float duration = (std::min)(frames, effect.EffectMotionFrames);
        vec3_t motion{effect.EffectMotionVelocity[0], effect.EffectMotionVelocity[1],
                      effect.Gravity - effect.AmbientVerticalNoise * 0.5f};
        const auto contact = effect.SubType == 0
                                 ? world.FirstTerrainContact(effect.Position, motion,
                                                             effect.AmbientVerticalNoise, duration)
                                 : std::optional<TerrainContact>{};
        const float step = contact ? contact->frames : duration;
        effect.Position[0] += motion[0] * step;
        effect.Position[1] += motion[1] * step;
        Core::Time::Advance(effect.Position[2], effect.Gravity, effect.AmbientVerticalNoise, step);
        effect.StartPosition[0] -= 10.f * step;
        effect.EffectMotionFrames -= step;
        frames -= step;
        if (contact)
        {
            effect.Position[2] = contact->height;
            effect.EffectResting = true;
        }
        effect.MotionTrace.Advance(step, effect.Position);
    }
    return totalFrames - frames;
}
} // namespace

namespace GameLogic::Effects::Behaviors
{
MoveBehaviorLegacyCalls::MoveBehaviorLegacyCalls(SessionKeeper &keeper,
                                                 MoveBehavior &owner) noexcept
    : SessionLegacyCalls(keeper), owner_(owner)
{
}

MoveBehavior::MoveBehavior(SessionKeeper &keeper) noexcept
    : MoveBehaviorLegacyCalls(keeper, *this), Joints(keeper.JointsStorage()),
      gMapManager(keeper.MapManagerObject()), FPS_ANIMATION_FACTOR(keeper.FrameAnimationFactor()),
      WorldTime(keeper.FrameWorldTime())
{
}

// MODEL_DRAGON
bool MoveBehavior::Move_MODEL_DRAGON(OBJECT *o, int index, float Luminosity)
{
    {
        AdvanceDragonFlight(*o, FPS_ANIMATION_FACTOR);
        const float untilReset = (std::max)(0.f, o->LifeTime - 10.f);
        if (FPS_ANIMATION_FACTOR > untilReset)
        {
            const float remainder = std::fmod(FPS_ANIMATION_FACTOR - untilReset, 990.f);
            const float life = remainder == 0.f ? 10.f : 1000.f - remainder;
            o->LifeTime = life + FPS_ANIMATION_FACTOR;
        }
        BMD *model = &Models[o->Type];
        o->EffectAnimationAdvance = 0.f;
        if (FPS_ANIMATION_FACTOR > 0.f && model->NumActions > 0)
        {
            const int keys =
                (std::min)(40, (std::max)(1, model->Actions[0].NumAnimationKeys -
                                                 (model->Actions[0].LockPositions ? 1 : 0)));
            const float advanced = o->AnimationFrame + o->Velocity * FPS_ANIMATION_FACTOR;
            const bool crossed =
                std::floor(advanced) != std::floor(o->AnimationFrame) || o->AnimationFrame >= keys;
            o->AnimationFrame = std::fmod(advanced, static_cast<float>(keys));
            if (o->AnimationFrame < 0.f)
                o->AnimationFrame += keys;
            if (crossed)
            {
                const int current = static_cast<int>(o->AnimationFrame);
                o->PriorAnimationFrame = static_cast<float>(current > 0 ? current - 1 : keys - 1);
                o->PriorAction = 0;
            }
            o->CurrentAction = 0;
        }
    }
    return true;
}

// MODEL_ARROW_AUTOLOAD
bool MoveBehavior::Move_MODEL_ARROW_AUTOLOAD(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->SubType == 0)
        {
            OBJECT *pOwn = o->Owner;
            for (float life = Core::Time::ReferenceSample(o->LifeTime / 10.f) * 10.f;
                 life > 0.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life);
                 life -= 10.f)
            {
                auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                    FPS_ANIMATION_FACTOR - std::max(0.f, o->LifeTime - life));
                vec3_t position, angle;
                pOwn->MotionTrace.Sample(WorldTime, birth.FrameFraction(), pOwn->Position,
                                         position);
                VectorCopy(pOwn->Angle, angle);
                angle[2] = pOwn->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), angle[2]);
                CreateEffect(MODEL_ARROW_AUTOLOAD, position, angle, pOwn->Light, 1, pOwn);
            }
        }
        else if (o->SubType == 1)
        {
            vec3_t tmp = {-10.f, 5.f, 10.f};

            if (o->LifeTime < 20)
            {
                float flumi = (float)(o->LifeTime - 20) / 20.f;
                VectorScale(o->Direction, flumi, o->Light);
            }

            OBJECT *pOwner = o->Owner;
            VectorTransform(tmp, pOwner->BoneTransform[47], o->Position);
            VectorScale(o->Position, pOwner->Scale, o->Position);
            VectorAdd(o->Position, pOwner->Position, o->Position);
            VectorCopy(pOwner->Angle, o->Angle);
            o->Angle[0] += 85.f;
            o->Angle[1] += -17.f;
            o->Angle[2] += 20.f;
        }
    }
    return true;
}

// MODEL_INFINITY_ARROW
bool MoveBehavior::Move_MODEL_INFINITY_ARROW(OBJECT *o, int index, float Luminosity)
{
    {
        vec3_t tmp = {0.f, 0.f, 0.f};
        OBJECT *pOwner = o->Owner;
        if (o->SubType == 1)
        {
            o->Light[0] *= pow(0.95f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= pow(0.95f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= pow(0.95f, FPS_ANIMATION_FACTOR);
        }
        VectorTransform(tmp, pOwner->BoneTransform[29], o->Position);
        VectorScale(o->Position, pOwner->Scale, o->Position);
        VectorAdd(o->Position, pOwner->Position, o->Position);
        VectorCopy(pOwner->Angle, o->Angle);
    }
    return true;
}

// MODEL_INFINITY_ARROW1, MODEL_INFINITY_ARROW2, MODEL_INFINITY_ARROW3
bool MoveBehavior::Move_MODEL_INFINITY_ARROW1(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->SubType == 0)
        {
            o->Light[0] *= pow(0.95f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= pow(0.95f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= pow(0.95f, FPS_ANIMATION_FACTOR);
            VectorCopy(o->Owner->Position, o->Position);
        }
        else if (o->SubType == 1)
        {
            o->Light[0] *= pow(0.98f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= pow(0.98f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= pow(0.98f, FPS_ANIMATION_FACTOR);
            if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 40))
                CreateEffect(MODEL_INFINITY_ARROW3, o->Position, o->Angle, o->Light, 2, o);
            else if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 20))
                CreateEffect(MODEL_INFINITY_ARROW3, o->Position, o->Angle, o->Light, 3, o);
            VectorCopy(o->Owner->Position, o->Position);
        }
        else if (o->SubType == 2)
        {
            o->Light[0] *= pow(0.98f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= pow(0.98f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= pow(0.98f, FPS_ANIMATION_FACTOR);
            VectorCopy(o->Owner->Position, o->Position);
        }
        else if (o->SubType == 3)
        {
            o->Light[0] *= pow(0.98f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= pow(0.98f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= pow(0.98f, FPS_ANIMATION_FACTOR);
            VectorCopy(o->Owner->Position, o->Position);
        }
    }
    return true;
}

// MODEL_SHIELD_CRASH
bool MoveBehavior::Move_MODEL_SHIELD_CRASH(OBJECT *o, int index, float Luminosity)
{
    {
        float ftmp = 0.f;
        vec3_t nPos, nLight;

        if (o->LifeTime >= 0 && o->LifeTime < 8)
        {
            ftmp = (float)o->LifeTime / 8;
            VectorScale(o->Direction, ftmp, o->Light);
        }

        if (o->LifeTime >= 8 && o->LifeTime < 24)
        {
            ftmp = 1.f - ((float)(o->LifeTime - 24) / 16);
            VectorScale(o->Direction, ftmp, o->Light);
        }

        if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 23))
        {
            if (o->SubType == 1)
            {
                Vector(0.3f, 0.3f, 0.8f, nLight);
                CreateEffect(BITMAP_SHOCK_WAVE, o->Owner->Position, o->Owner->Angle, nLight, 9,
                             o->Owner, -1, 0, 0, 0);
            }
            else if (o->SubType == 2)
            {
                vec3_t vShockColor = {0.8f, 0.3f, 0.3f};
                CreateEffect(BITMAP_SHOCK_WAVE, o->Owner->Position, o->Owner->Angle, vShockColor, 9,
                             o->Owner, -1, 0, 0, 0);
            }
            else
            {
                vec3_t vShockColor = {1.f, 1.f, 1.f};
                CreateEffect(BITMAP_SHOCK_WAVE, o->Owner->Position, o->Owner->Angle, vShockColor, 9,
                             o->Owner, -1, 0, 0, 0);
            }
        }

        VectorCopy(o->Owner->Position, o->Position);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            o->Owner->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Owner->Position,
                                         nPos);

            if (o->SubType == 1)
            {
                Vector(0.f, 1.f, 5.f, nLight);
                CreateEffect(MODEL_SKILL_INFERNO, nPos, o->Angle, nLight, 10, o, 30, 0);
                nPos[2] += 120.f;
                CreateParticle(BITMAP_SMOKE, nPos, o->Angle, o->Light, 11, 2.f);
            }
            else if (o->SubType == 2)
            {
                Vector(0.8f, 0.3f, 0.3f, nLight);
                CreateEffect(MODEL_SKILL_INFERNO, nPos, o->Angle, nLight, 10, o, 30, 0);
            }
            else if (o->SubType == 0)
            {
                Vector(0.0f, 0.8f, 1.5f, nLight);
                CreateEffect(MODEL_SKILL_INFERNO, nPos, o->Angle, nLight, 2, o, 30, 0);
                nPos[2] += 120.f;
                CreateParticle(BITMAP_SMOKE, nPos, o->Angle, o->Light, 11, 2.f);
            }
        }
    }
    return true;
}

// MODEL_SHIELD_CRASH2
bool MoveBehavior::Move_MODEL_SHIELD_CRASH2(OBJECT *o, int index, float Luminosity)
{
    {
        float ftmp = 0.f;
        if (o->LifeTime >= 0 && o->LifeTime < 8)
        {
            ftmp = (float)o->LifeTime / 8;
            VectorScale(o->Direction, ftmp, o->Light);
        }

        if (o->LifeTime >= 8 && o->LifeTime < 24)
        {
            ftmp = 1.f - ((float)(o->LifeTime - 24) / 16);
            VectorScale(o->Direction, ftmp, o->Light);
        }

        VectorCopy(o->Owner->Position, o->Position);
    }
    return true;
}

// MODEL_IRON_RIDER_ARROW
bool MoveBehavior::Move_MODEL_IRON_RIDER_ARROW(OBJECT *o, int index, float Luminosity)
{
    {
        o->Scale *= pow(1.05f, FPS_ANIMATION_FACTOR);
        if (o->LifeTime < 5)
        {
            o->Light[0] *= pow(0.7f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= pow(0.7f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= pow(0.7f, FPS_ANIMATION_FACTOR);
        }
        vec3_t vPos;
        VectorScale(o->Direction, o->Velocity, vPos);
        VectorAddScaled(o->Position, vPos, o->Position, FPS_ANIMATION_FACTOR);
        CreateSprite(BITMAP_LIGHT + 3, o->Position, 4.0f, o->Light, o);
        CreateSprite(BITMAP_DS_EFFECT, o->Position, 2.5f, o->Light, o);
        CreateParticleFpsChecked(BITMAP_SPARK + 1, o->Position, o->Angle, o->Light, 10, 3.0f);

        VectorCopy(o->Position, o->EyeLeft);
        CreateEffectFpsChecked(MODEL_WAVES, o->Position, o->Angle, o->Light, 3, NULL, 0);
    }
    return true;
}

// MODEL_MULTI_SHOT3
bool MoveBehavior::Move_MODEL_MULTI_SHOT3(OBJECT *o, int index, float Luminosity)
{
    o->Scale += (0.25f) * FPS_ANIMATION_FACTOR;
    o->BlendMeshLight = (float)o->LifeTime / 18.f;
    o->Alpha = o->BlendMeshLight;
    return true;
}

// MODEL_MULTI_SHOT1
bool MoveBehavior::Move_MODEL_MULTI_SHOT1(OBJECT *o, int index, float Luminosity)
{
    o->Scale += (0.2f) * FPS_ANIMATION_FACTOR;
    o->BlendMeshLight = (float)o->LifeTime / 18.f;
    o->Alpha = o->BlendMeshLight;
    return true;
}

// MODEL_MULTI_SHOT2
bool MoveBehavior::Move_MODEL_MULTI_SHOT2(OBJECT *o, int index, float Luminosity)
{
    o->Scale += (0.3f) * FPS_ANIMATION_FACTOR;
    o->BlendMeshLight = (float)o->LifeTime / 18.f;
    o->Alpha = o->BlendMeshLight;
    return true;
}

void MoveBehavior::EmitBladeVolley(OBJECT &effect, float fraction)
{
    constexpr float Angles[] = {7.f, 21.f, 35.f, -7.f, -21.f, -35.f};
    int direction;
    do
    {
        direction = WorldRandom() % 6;
    } while (direction == effect.m_sTargetIndex);
    effect.m_sTargetIndex = direction;
    OBJECT &owner = *effect.Owner;
    vec3_t savedPosition, savedAngle;
    VectorCopy(owner.Position, savedPosition);
    VectorCopy(owner.Angle, savedAngle);
    owner.MotionTrace.Sample(WorldTime, fraction, savedPosition, owner.Position);
    owner.Angle[2] =
        owner.MotionTrace.SampleYaw(WorldTime, fraction, savedAngle[2]) + Angles[direction];
    for (int i = 0; i < 3; ++i)
    {
        vec3_t position, light{0.2f, 0.3f, 1.f};
        effect.MotionTrace.Sample(WorldTime, fraction, effect.Position, position);
        position[1] += WorldRandom() % 200 - 100;
        position[2] += WorldRandom() % 200 - 100;
        CreateSprite(BITMAP_SHINY + 1, position, (WorldRandom() % 8 + 8) * 0.2f, light, &owner,
                     float(WorldRandom() % 360));
        Vector(1.f, 1.f, 1.f, light);
        CreateSprite(BITMAP_SHINY + 1, position, (WorldRandom() % 8 + 8) * 0.07f, light, &owner,
                     float(WorldRandom() % 360));
        VectorCopy(owner.Position, position);
        position[2] += WorldRandom() % 80 - 40 + 200;
        CreateJoint(BITMAP_SPARK + 1, position, position, owner.Angle, 1, &owner, 18.7f);
    }
    CreateArrow(&CharactersClient[int(effect.PKKey)], &owner, nullptr, FindHotKey(effect.Skill), 1,
                0);
    VectorCopy(savedPosition, owner.Position);
    VectorCopy(savedAngle, owner.Angle);
}

// MODEL_BLADE_SKILL
bool MoveBehavior::Move_MODEL_BLADE_SKILL(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType == 0)
        o->Scale = std::max(0.8f, o->Scale - 0.1f * FPS_ANIMATION_FACTOR);
    if (o->SubType != 1)
        return true;
    o->BlendMesh = -2;
    o->BlendMeshLight = o->LifeTime / 14.f;
    o->Alpha = o->BlendMeshLight;
    for (float life = Core::Time::ReferenceSample(o->LifeTime / 2.f) * 2.f;
         life >= 3.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life); life -= 2.f)
    {
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR -
                                                             std::max(0.f, o->LifeTime - life));
        EmitBladeVolley(*o, birth.FrameFraction());
    }
    return true;
}

// MODEL_KENTAUROS_ARROW
bool MoveBehavior::Move_MODEL_KENTAUROS_ARROW(OBJECT *o, int index, float Luminosity)
{
    {
        if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 24))
        {
            vec3_t vDir;
            vec34_t vMat;
            Vector(0.f, -1.f, 0.f, vDir);
            AngleMatrix(o->Angle, vMat);
            VectorRotate(vDir, vMat, o->Direction);
            CreateJoint(BITMAP_FLARE + 1, o->Position, o->Position, o->Angle, 17, o, 15.f, 40);
        }
        if (o->LifeTime <= 24)
        {
            o->Scale *= pow(1.05f, FPS_ANIMATION_FACTOR);
            vec3_t vLight = {0.3f, 0.5f, 1.f};
            vec3_t vPos;
            VectorScale(o->Direction, o->Velocity, vPos);
            VectorAddScaled(o->Position, vPos, o->Position, FPS_ANIMATION_FACTOR);
            //			CreateParticle(BITMAP_FLAME, o->Position, o->Angle, vLight, 8, 3.5f);
            CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, vLight, 26, 0.2f);
            CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, vLight, 26, 0.2f);
            //			CreateJoint ( BITMAP_FLARE+1, o->Position, o->Position, o->Angle, 9, o, 10.f, 40 );
        }
        //			else
        o->Alpha += (0.001f) * FPS_ANIMATION_FACTOR;
    }
    return true;
}

// MODEL_WARP3, MODEL_WARP2, MODEL_WARP, MODEL_WARP6, MODEL_WARP5, MODEL_WARP4
bool MoveBehavior::Move_MODEL_WARP3(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->SubType == 0)
        {
            float fTemp1 = sinf(WorldTime * 0.0011f) * 0.2f;
            float fTemp2 = sinf(WorldTime * 0.0017f) * 0.2f;
            float fTemp3 = sinf(WorldTime * 0.0013f) * 0.2f;
            Vector(fTemp1 + 0.01f, fTemp2 + 0.01f, fTemp3 + 0.01f, o->Light);
            o->Angle[1] += (4.0f + o->Gravity) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 1)
        {
            float fTemp1 = sinf(WorldTime * 0.0011f) * 0.05f;
            Vector(0.0f + fTemp1, 0.2f + fTemp1, 0.1f + fTemp1, o->Light);
            o->Angle[1] += (2.0f + o->Gravity) * FPS_ANIMATION_FACTOR;
        }
    }
    return true;
}

// MODEL_GHOST
bool MoveBehavior::Move_MODEL_GHOST(OBJECT *o, int index, float Luminosity)
{
    {
        constexpr float SteeringInterval = 1.f / 16.f, FlightRadius = 500.f;
        const double referenceMilliseconds =
            1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
        double sampleTime = WorldTime - FPS_ANIMATION_FACTOR * referenceMilliseconds;
        float remaining = FPS_ANIMATION_FACTOR, elapsed = 0.f;
        while (remaining > 0.f)
        {
            if (o->AmbientNoiseFrames <= 0.f)
            {
                if (WorldRandom() % 40 == 0)
                {
                    o->Velocity = -o->Velocity;
                    o->Gravity = o->Velocity * float(WorldRandom() % 3 + 5);
                }
                if (WorldRandom() % 40 == 0)
                {
                    o->Velocity = -o->Velocity;
                    o->Distance = o->Velocity * float(WorldRandom() % 3 + 1);
                }
                const double phase =
                    std::fmod(std::floor(sampleTime + referenceMilliseconds), 10000.0) * 0.0001;
                const float wave = float(std::sin(phase));
                o->AmbientTurnRate = wave * o->Gravity;
                o->AmbientVerticalNoise = wave * o->Distance / 3.f;
                o->AmbientNoiseFrames = 1.f;
            }
            if (o->EffectMotionFrames <= 0.f)
            {
                o->EffectMotionFrames = (std::min)(SteeringInterval, o->AmbientNoiseFrames);
                vec3_t angle;
                VectorCopy(o->Angle, angle);
                angle[0] += o->AmbientVerticalNoise * o->EffectMotionFrames * 0.5f;
                angle[2] += o->AmbientTurnRate * o->EffectMotionFrames * 0.5f;
                float matrix[3][4];
                AngleMatrix(angle, matrix);
                VectorRotate(o->Direction, matrix, o->EffectMotionVelocity);
            }
            const float step = (std::min)(remaining, o->EffectMotionFrames);
            if (o->Kind == 0)
            {
                vec3_t offset;
                VectorSubtract(o->Position, o->StartPosition, offset);
                const double c = DotProduct(offset, offset) - FlightRadius * FlightRadius;
                const double speedSquared =
                    DotProduct(o->EffectMotionVelocity, o->EffectMotionVelocity);
                const double along = DotProduct(offset, o->EffectMotionVelocity);
                const double crossing =
                    c >= 0.0 ? 0.0
                    : speedSquared > 0.0
                        ? (-along + std::sqrt(along * along - speedSquared * c)) / speedSquared
                        : double(step) + 1.0;
                if (crossing <= step)
                {
                    // The shared tail subtracts the full update; preserve the crossing's remaining age.
                    o->LifeTime = 50.f + elapsed + float(crossing);
                    o->Kind = 1;
                }
            }
            VectorAddScaled(o->Position, o->EffectMotionVelocity, o->Position, step);
            o->Angle[0] += o->AmbientVerticalNoise * step;
            o->Angle[2] += o->AmbientTurnRate * step;
            o->EffectMotionFrames -= step;
            o->AmbientNoiseFrames -= step;
            sampleTime += step * referenceMilliseconds;
            elapsed += step;
            remaining -= step;
        }

        Vector(o->BlendMeshLight / 10.f, o->BlendMeshLight / 10.f, o->BlendMeshLight / 4.f,
               o->Light);
        CreateParticleFpsChecked(BITMAP_LIGHT, o->Position, o->Angle, o->Light, 9, o->Scale);
    }
    return true;
}

// MODEL_TREE_ATTACK
bool MoveBehavior::Move_MODEL_TREE_ATTACK(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    {
        Vector(1.0f, 1.0f, 1.0f, Light);
        constexpr float GrowthRate = 0.1f, FadeRate = 0.07f;
        const float untilFade = (std::max)(0.f, (1.f - GrowthRate - o->Scale) / GrowthRate);
        const float fading = (std::max)(0.f, FPS_ANIMATION_FACTOR - untilFade);
        o->Scale = (std::min)(1.f, o->Scale + GrowthRate * FPS_ANIMATION_FACTOR);
        o->Alpha -= FadeRate * fading;
    }
    return true;
}

// MODEL_BUTTERFLY01
bool MoveBehavior::Move_MODEL_BUTTERFLY01(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Position;
    float Height;
    {
        float remaining = FPS_ANIMATION_FACTOR;
        while (remaining > 0.f)
        {
            if (o->AmbientNoiseFrames <= 0.f)
            {
                o->AmbientTurnRate = float(WorldRandom() % 10) * (o->Kind > 0 ? 1.f : -1.f);
                if (WorldRandom() % 32 == 0)
                    o->Direction[2] = float(int(WorldRandom() % 15) - 7);
                o->Direction[2] += float(int(WorldRandom() % 15) - 7) * 0.2f;
                const float ground = RequestTerrainHeight(o->Position[0], o->Position[1]);
                if (o->Position[2] < ground + 50.f)
                    o->Direction[2] = o->Direction[2] * 0.8f + 1.f;
                if (o->Position[2] > ground + 150.f)
                    o->Direction[2] = o->Direction[2] * 0.8f - 1.f;
                o->AmbientVerticalNoise = float(int(WorldRandom() % 15) - 7) * 0.3f;
                o->AmbientSpeedNoise = float(WorldRandom() % 32 + 64) * 0.01f;
                o->AmbientNoiseFrames = 1.f;
            }
            const float step = (std::min)(remaining, o->AmbientNoiseFrames);
            const float previousYaw = o->Angle[2];
            o->Angle[2] += o->AmbientTurnRate * step;
            vec3_t velocity;
            MoveTurningObject(*o, o->Direction, previousYaw, std::abs(o->AmbientTurnRate), step,
                              velocity);
            o->Position[2] += o->AmbientVerticalNoise * step;
            o->AmbientNoiseFrames -= step;
            remaining -= step;
        }
        const float Luminosity = o->AmbientSpeedNoise;
        if (o->SubType == 0)
        {
            Vector(Luminosity * 0.4f, Luminosity * 0.8f, Luminosity * 0.6f, Light);
        }
        else if (o->SubType == 1)
        {
            Vector(Luminosity * 0.4f, Luminosity * 0.6f, Luminosity * 0.8f, Light);
        }
        else if (o->SubType == 2)
        {
            Vector(Luminosity * 0.6f, Luminosity * 0.8f, Luminosity * 0.4f, Light);
        }
        else if (o->SubType == 3)
        {
            Vector(Luminosity * 0.7f, Luminosity * 0.9f, Luminosity * 0.5f, Light);
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
            {
                vec3_t Position;
                Vector((float)(WorldRandom() % 16 - 8), (float)(WorldRandom() % 16 - 8),
                       (float)(WorldRandom() % 16 - 8), Position);
                VectorAdd(Position, o->Position, Position);
                CreateParticle(BITMAP_SPARK, Position, o->Angle, o->Light, 7);
            }
        }
        CreateSprite(BITMAP_LIGHT, o->Position, 1.f, Light, o);
    }
    return true;
}

// BITMAP_SKULL
bool MoveBehavior::Move_BITMAP_SKULL(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Position;
    if (o->SubType == 4)
    {
        AdvanceAcceleratingPitch(*o, -o->Velocity, -1.f, FPS_ANIMATION_FACTOR);
        CreateSprite(BITMAP_SKULL, o->Position, 10.0f, o->Light, NULL);
    }
    else
    {
        if (!o->Owner->Live)
        {
            o->LifeTime = 0;
            RetireEffect(o);
        }
        else
        {
            switch (o->SubType)
            {
            case 0:
                if (g_isCharacterBuff(o->Owner, eDeBuff_Defense))
                {
                    o->LifeTime = 10;
                    if (g_isCharacterBuff(o->Owner, eBuff_Cloaking))
                    {
                        o->Visible = false;
                        break;
                    }
                }
            case 1:
            case 3: {
                for (int i = 0; i < 3; ++i)
                {
                    float fParam = (float)i * Q_PI * 2 / 3.0f + o->LifeTime * 0.17f;
                    if (o->SubType == 3)
                    {
                        fParam = -fParam;
                    }
                    vec3_t Position;
                    float fDist =
                        50.f + 20.f * (float)sinf(i * 15.37f + (float)WorldTime * 0.0031f);
                    Position[0] = o->Owner->Position[0] + fDist * (float)sinf(fParam);
                    Position[1] = o->Owner->Position[1] + fDist * (float)cosf(fParam);
                    Position[2] = o->Owner->Position[2] +
                                  ((o->SubType == 3) ? 250.0f : 200.0f) * o->Owner->Scale;
                    CreateSprite(BITMAP_SKULL, Position, 1.0f, Light, o->Owner, 0.0f);
                }
            }
            break;
            case 5: {
                for (int i = 0; i < 3; ++i)
                {
                    float fParam = (float)i * Q_PI * 2 / 3.0f + o->LifeTime * 0.17f;
                    vec3_t Position;
                    Position[0] =
                        o->Owner->Position[0] + 50.0f * (float)sinf(fParam + WorldTime * 0.003f);
                    Position[1] =
                        o->Owner->Position[1] + 50.0f * (float)cosf(fParam + WorldTime * 0.003f);
                    Position[2] = o->Owner->Position[2] + (i + 1) * 50.0f * o->Owner->Scale;
                    vec3_t LightFlame = {0.6f, 0.6f, 1.0f};
                    CreateParticleFpsChecked(BITMAP_LIGHT, Position, o->Angle, LightFlame, 5, 0.7f);
                }
            }
            break;
            case 2: {
                vec3_t Light;
                Light[0] = Light[1] = Light[2] = 0.1f * (float)o->LifeTime;
                CreateSprite(BITMAP_SKULL, o->Position, 1.5f, Light, o->Owner, 0.0f);
            }
            break;
            }
        }
    }
    return true;
}

// MODEL__SPEAR
bool MoveBehavior::Move_MODEL__SPEAR(OBJECT *o, int index, float Luminosity)
{
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        vec3_t origin;
        o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, origin);
        for (int j = 0; j < 3; ++j)
        {
            vec3_t local{0.f, -100.f, 0.f}, position;
            vec3_t angle{float(WorldRandom() % 90), 0.f, float(WorldRandom() % 360)};
            float matrix[3][4];
            AngleMatrix(angle, matrix);
            VectorRotate(local, matrix, position);
            VectorSubtract(origin, position, position);
            CreateJoint(BITMAP_JOINT_HEALING, position, origin, angle, 6, nullptr, 5.f);
        }
    }
    return true;
}

// MODEL_HALLOWEEN_CANDY_BLUE, MODEL_HALLOWEEN_CANDY_ORANGE, MODEL_HALLOWEEN_CANDY_YELLOW, MODEL_HALLOWEEN_CANDY_RED, MODEL_HALLOWEEN_CANDY_HOBAK, MODEL_HALLOWEEN_CANDY_STAR
bool MoveBehavior::Move_MODEL_HALLOWEEN_CANDY_BLUE(OBJECT *o, int index, float Luminosity)
{
    float Height;
    if (o->SubType == 0 || o->SubType == 1)
    {
        AdvanceBouncingDebris(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR,
                              DebrisMotion::Candy);

        if (o->Type == MODEL_HALLOWEEN_CANDY_HOBAK)
        {
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.0))
            {
                CreateParticle(BITMAP_FIRE, o->Position, o->Angle, o->Light, 5,
                               0.4f + WorldRandom() % 10 * 0.01f);
            }
        }
    }
    return true;
}

// MODEL_HALLOWEEN_EX
bool MoveBehavior::Move_MODEL_HALLOWEEN_EX(OBJECT *o, int index, float Luminosity)
{
    return true;
}

// MODEL_XMAS_EVENT_BOX, MODEL_XMAS_EVENT_CANDY, MODEL_XMAS_EVENT_TREE, MODEL_XMAS_EVENT_SOCKS
bool MoveBehavior::Move_MODEL_XMAS_EVENT_BOX(OBJECT *o, int index, float Luminosity)
{
    AdvanceBouncingDebris(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR,
                          DebrisMotion::Christmas);
    return true;
}

// MODEL_XMAS_EVENT_ICEHEART
bool MoveBehavior::Move_MODEL_XMAS_EVENT_ICEHEART(OBJECT *o, int index, float Luminosity)
{
    o->Angle[2] += (10.f) * FPS_ANIMATION_FACTOR;
    if (o->Owner != NULL)
    {
        if (o->Owner->CurrentAction != PLAYER_SANTA_2)
        {
            RetireEffect(o);
        }
    }
    return true;
}

// MODEL_NEWYEARSDAY_EVENT_BEKSULKI, MODEL_NEWYEARSDAY_EVENT_CANDY, MODEL_NEWYEARSDAY_EVENT_MONEY, MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_GREEN, MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_RED, MODEL_NEWYEARSDAY_EVENT_PIG, MODEL_NEWYEARSDAY_EVENT_YUT
bool MoveBehavior::Move_MODEL_NEWYEARSDAY_EVENT_BEKSULKI(OBJECT *o, int index, float Luminosity)
{
    AdvanceBouncingDebris(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR,
                          DebrisMotion::NewYear);
    return true;
}

// MODEL_MOONHARVEST_MOON
bool MoveBehavior::Move_MODEL_MOONHARVEST_MOON(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType == 0)
    {
        o->Angle[2] += (5.0f) * FPS_ANIMATION_FACTOR;
        if (o->LifeTime < 25)
        {
            o->Light[0] -= (0.05f) * FPS_ANIMATION_FACTOR;
            o->Light[1] -= (0.06f) * FPS_ANIMATION_FACTOR;
            o->Light[2] -= (0.05f) * FPS_ANIMATION_FACTOR;
        }
    }
    else if (o->SubType == 1)
    {
        VectorAddScaled(o->Position, o->Direction, o->Position, FPS_ANIMATION_FACTOR);
        CreateParticleFpsChecked(BITMAP_SMOKELINE1 + WorldRandom() % 3, o->Position, o->Angle,
                                 o->Light, 0, 1.0f);
        //CreateParticle(BITMAP_SMOKE, o->Position, o->Angle,o->Light, 23, 1.0f);
        //CreateParticle(BITMAP_SMOKE, o->Position, o->Angle,o->Light, 8, 1.0f);
        CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 11, 1.0f);
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
        {
            CreateParticle(BITMAP_WATERFALL_3, o->Position, o->Angle, o->Light, 3, 3.f);
        }
    }
    else if (o->SubType == 2)
    {
        o->Scale += (0.01f) * FPS_ANIMATION_FACTOR;
    }
    return true;
}

// MODEL_MOONHARVEST_GAM, MODEL_MOONHARVEST_SONGPUEN1, MODEL_MOONHARVEST_SONGPUEN2
bool MoveBehavior::Move_MODEL_MOONHARVEST_GAM(OBJECT *o, int index, float Luminosity)
{
    AdvanceBouncingDebris(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR,
                          DebrisMotion::MoonHarvest);
    return true;
}

// MODEL_SPEARSKILL
bool MoveBehavior::Move_MODEL_SPEARSKILL(OBJECT *o, int index, float Luminosity)
{
    return true;
}

// BITMAP_FIRE_CURSEDLICH
bool MoveBehavior::Move_BITMAP_FIRE_CURSEDLICH(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType == 0)
    {
        OBJECT *pObject = o->Owner;
        if (pObject->Live == false)
        {
            RetireEffect(o);
            return true;
        }
        BMD *pModel = &Models[pObject->Type];
        vec3_t vPos, vRelative;
        Vector(0.0f, 0.0f, 0.0f, vRelative);
        pModel->TransformPosition(pObject->BoneTransform[o->Skill], vRelative, vPos, false);
        VectorScale(vPos, pModel->BodyScale, vPos);
        VectorAdd(vPos, pObject->Position, o->Position);

        vec3_t vLight;
        Vector(o->Alpha * 0.3f, o->Alpha * 0.3f, o->Alpha * 0.3f, vLight);
        CreateParticleFpsChecked(BITMAP_FIRE_CURSEDLICH, o->Position, o->Angle, vLight, 3, 1,
                                 pObject);
    }
    else if (o->SubType == 1)
    {
        vec3_t vFirePosition;
        for (int i = 0; i < 1; ++i)
        {
            Vector(o->Position[0] + (WorldRandom() % 100 - 50) * 1.0f,
                   o->Position[1] + (WorldRandom() % 100 - 50) * 1.0f,
                   o->Position[2] + (WorldRandom() % 10 + 5) * 1.0f, vFirePosition);
            float fScale = (WorldRandom() % 5 + 13) * 0.1f;
            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticleFpsChecked(BITMAP_FIRE_HIK1, vFirePosition, o->Angle, o->Light, 1,
                                         fScale);
                break;
            case 1:
                CreateParticleFpsChecked(BITMAP_FIRE_CURSEDLICH, vFirePosition, o->Angle, o->Light,
                                         5, fScale);
                break;
            case 2:
                CreateParticleFpsChecked(BITMAP_FIRE_HIK3, vFirePosition, o->Angle, o->Light, 1,
                                         fScale);
                break;
            }
        }
    }
    else if (o->SubType == 12)
    {
        vec3_t vFirePosition;
        for (int i = 0; i < 1; ++i)
        {
            Vector(o->Position[0] + (WorldRandom() % 50 - 25) * 1.0f,
                   o->Position[1] + (WorldRandom() % 50 - 25) * 1.0f,
                   o->Position[2] + (WorldRandom() % 10 + 5) * 1.0f, vFirePosition);
            float fScale = ((WorldRandom() % 5 + 13) * 0.05f) * o->Scale;

            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticleFpsChecked(BITMAP_FIRE_HIK1, vFirePosition, o->Angle, o->Light, 1,
                                         fScale);
                break;
            case 1:
                CreateParticleFpsChecked(BITMAP_FIRE_CURSEDLICH, vFirePosition, o->Angle, o->Light,
                                         5, fScale);
                break;
            case 2:
                CreateParticleFpsChecked(BITMAP_FIRE_HIK3, vFirePosition, o->Angle, o->Light, 1,
                                         fScale);
                break;
            }
        }
    }
    else if (o->SubType == 2)
    {
        if (o->Owner == NULL)
        {
            RetireEffect(o);
            return true;
        }

        vec3_t vFirePosition;
        for (int i = 0; i < 8; ++i)
        {
            int iNumBones = Models[o->Owner->Type].NumBones;
            Models[o->Owner->Type].TransformByObjectBone(vFirePosition, o->Owner,
                                                         WorldRandom() % iNumBones);

            vec3_t vLightFire;
            Vector(1.0f, 0.2f, 0.0f, vLightFire);
            CreateSprite(BITMAP_LIGHT, vFirePosition, 4.0f, vLightFire, o->Owner);

            vec3_t vLight;
            Vector(1.0f, 1.0f, 1.0f, vLight);
            float fScale = (WorldRandom() % 5 + 13) * 0.1f;
            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticleFpsChecked(BITMAP_FIRE_HIK1, vFirePosition, o->Angle, vLight, 0,
                                         fScale);
                break;
            case 1:
                CreateParticleFpsChecked(BITMAP_FIRE_CURSEDLICH, vFirePosition, o->Angle, vLight, 4,
                                         fScale);
                break;
            case 2:
                CreateParticleFpsChecked(BITMAP_FIRE_HIK3, vFirePosition, o->Angle, vLight, 0,
                                         fScale);
                break;
            }
        }
    }
    else if (o->SubType == 3)
    {
        for (int i = 0; i < 2; ++i)
        {
            float fScale = (WorldRandom() % 5 + 18) * 0.03f;

            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticleFpsChecked(BITMAP_FIRE_HIK1, o->Position, o->Angle, o->Light, 0,
                                         fScale);
                break;
            case 1:
                CreateParticleFpsChecked(BITMAP_FIRE_CURSEDLICH, o->Position, o->Angle, o->Light, 4,
                                         fScale);
                break;
            case 2:
                CreateParticleFpsChecked(BITMAP_FIRE_HIK3, o->Position, o->Angle, o->Light, 0,
                                         fScale);
                break;
            }
        }
    }
    else if (o->SubType == 4)
    {
        for (int i = 0; i < 2; ++i)
        {
            float fScale = (WorldRandom() % 5 + 18) * 0.03f;
            Vector(0.6f, 0.9f, 0.1f, o->Light);
            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticleFpsChecked(BITMAP_FIRE_HIK1_MONO, o->Position, o->Angle, o->Light, 0,
                                         fScale);
                break;
            case 1:
                CreateParticleFpsChecked(BITMAP_FIRE_HIK2_MONO, o->Position, o->Angle, o->Light, 4,
                                         fScale);
                break;
            case 2:
                CreateParticleFpsChecked(BITMAP_FIRE_HIK3_MONO, o->Position, o->Angle, o->Light, 0,
                                         fScale);
                break;
            }
        }
    }
    return true;
}

// MODEL_SUMMONER_WRISTRING_EFFECT
bool MoveBehavior::Move_MODEL_SUMMONER_WRISTRING_EFFECT(OBJECT *o, int index, float Luminosity)
{
    {
        OBJECT *pObject = o->Owner;
        BMD *pModel = &Models[pObject->Type];
        //pModel->Animation(BoneTransform, o->AnimationFrame, o->PriorAnimationFrame, o->PriorAction, o->Angle, o->HeadAngle);
        vec3_t vPos, vRelative;
        Vector(0.0f, 0.0f, 0.0f, vRelative);
        pModel->TransformPosition(pObject->BoneTransform[37], vRelative, vPos, false);
        VectorScale(vPos, pModel->BodyScale, vPos);
        VectorAdd(vPos, pObject->Position, o->Position);

        if (pObject->Live)
            o->LifeTime = 100.f; //무한

        BMD *b = &Models[o->Type];
        b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction,
                         o->Velocity / 5.f, o->Position, o->Angle);
    }
    return true;
}

// MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT
bool MoveBehavior::Move_MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType == 0)
    {
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
                o->Alpha -= (0.03f) * FPS_ANIMATION_FACTOR;
            else
                DeleteEffect(MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT, o->Owner);
        }
        else
        {
            if (Hero->SafeZone || sinf(WorldTime * 0.0004f + o->Skill * 0.024f) < 0.3f)
                o->Kind = 1;
            if (o->Alpha < 1.0f)
                o->Alpha += (0.03f) * FPS_ANIMATION_FACTOR;
        }

        if (pObject->Live)
            o->LifeTime = 100.f;

        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.0))
        {
            vec3_t vLight;
            Vector(o->Alpha * 0.3f, o->Alpha * 0.3f, o->Alpha * 0.3f, vLight);
            CreateParticle(BITMAP_FIRE_CURSEDLICH, o->Position, o->Angle, vLight, 1, 1, pObject);
        }
    }
    else if (o->SubType == 1)
    {
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
        {
            OBJECT *pObject = o->Owner;
            if (pObject->Live == false)
            {
                RetireEffect(o);
                return true;
            }
            BMD *pModel = &Models[pObject->Type];
            int iNumBones = pModel->NumBones;
            CreateEffect(BITMAP_FIRE_CURSEDLICH, pObject->Position, pObject->Angle, pObject->Light,
                         0, pObject, -1, WorldRandom() % iNumBones);
        }
    }
    return true;
}

// MODEL_SUMMONER_EQUIP_HEAD_NEIL
bool MoveBehavior::Move_MODEL_SUMMONER_EQUIP_HEAD_NEIL(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType == 0)
    {
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
                o->Alpha -= (0.03f) * FPS_ANIMATION_FACTOR;
            else
                DeleteEffect(MODEL_SUMMONER_EQUIP_HEAD_NEIL, o->Owner);
        }
        else
        {
            if (Hero->SafeZone || sinf(WorldTime * 0.0004f + o->Skill * 0.024f) < 0.3f)
                o->Kind = 1;
            if (o->Alpha < 1.0f)
                o->Alpha += (0.03f) * FPS_ANIMATION_FACTOR;
        }

        if (pObject->Live)
            o->LifeTime = 100.f;

        vec3_t vLight;
        Vector(o->Alpha, o->Alpha, o->Alpha, vLight);
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.0))
        {
            CreateParticle(BITMAP_LIGHT + 2, o->Position, o->Angle, o->Light, 3, 0.30f, pObject);
        }

        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
        {
            CreateParticle(BITMAP_LIGHT + 2, o->Position, o->Angle, o->Light, 3, 0.30f, pObject);
        }
        //DeleteJoint(MODEL_SPEARSKILL, o, 15);
    }
    else if (o->SubType == 1)
    {
        OBJECT *pObject = o->Owner;
        if (pObject->Live == false)
        {
            RetireEffect(o);
            return true;
        }
        vec3_t vColor;
        Vector(1.0f, 1.0f, 1.0f, vColor);
        CreateParticleFpsChecked(BITMAP_LIGHT + 2, pObject->Position, pObject->Angle, vColor, 5,
                                 0.1f, pObject);
    }
    return true;
}

// MODEL_SUMMONER_CASTING_EFFECT1, MODEL_SUMMONER_CASTING_EFFECT11, MODEL_SUMMONER_CASTING_EFFECT111, MODEL_SUMMONER_CASTING_EFFECT2, MODEL_SUMMONER_CASTING_EFFECT22, MODEL_SUMMONER_CASTING_EFFECT222, MODEL_SUMMONER_CASTING_EFFECT4
bool MoveBehavior::Move_MODEL_SUMMONER_CASTING_EFFECT1(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->LifeTime < 20)
            o->BlendMeshLight -= (0.03f) * FPS_ANIMATION_FACTOR;
        else if (o->BlendMeshLight < 0.5f)
            o->BlendMeshLight += (0.05f) * FPS_ANIMATION_FACTOR;

        switch (o->Type)
        {
        case MODEL_SUMMONER_CASTING_EFFECT1:
            o->Angle[2] -= (3.0f) * FPS_ANIMATION_FACTOR;
            break;
        case MODEL_SUMMONER_CASTING_EFFECT11:
            o->Angle[2] += (3.0f) * FPS_ANIMATION_FACTOR;
            break;
        case MODEL_SUMMONER_CASTING_EFFECT111:
            o->Angle[2] -= (3.0f) * FPS_ANIMATION_FACTOR;
            break;
        case MODEL_SUMMONER_CASTING_EFFECT2:
            o->Angle[2] += (3.0f) * FPS_ANIMATION_FACTOR;
            break;
        case MODEL_SUMMONER_CASTING_EFFECT22:
            o->Angle[2] -= (3.0f) * FPS_ANIMATION_FACTOR;
            break;
        case MODEL_SUMMONER_CASTING_EFFECT222:
            o->Angle[2] += (3.0f) * FPS_ANIMATION_FACTOR;
            break;
        case MODEL_SUMMONER_CASTING_EFFECT4:
            o->Scale += (0.6f) * FPS_ANIMATION_FACTOR;
            break;
        }
    }
    return true;
}

void MoveBehavior::EmitSahamuttFlames(OBJECT &effect)
{
    BMD &model = Models[effect.Type];
    AnimationPoseSample pose(&effect, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    constexpr int FlameBones[] = {13, 23, 39, 49, 3, 4, 5, 61};
    std::array<vec34_t, MAX_BONES> bones;
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        ObjectDrawInput draw(&effect);
        effect.MotionTrace.Sample(WorldTime, birth.FrameFraction(), effect.Position, draw.position);
        draw.bones =
            pose.EvaluateAtTime(model, effect, WorldTime, birth.FrameFraction(), bones.data());
        vec3_t light{effect.Alpha * 0.3f, effect.Alpha * 0.3f, effect.Alpha * 0.3f};
        for (int bone : FlameBones)
        {
            vec3_t position;
            model.TransformByObjectBone(position, draw, bone);
            for (float scale : {5.f, 4.f, 3.f})
                CreateParticle(BITMAP_FIRE_CURSEDLICH, position, effect.Angle, light, 2, scale,
                               &effect);
        }
    }
}

// MODEL_SUMMONER_SUMMON_SAHAMUTT
bool MoveBehavior::Move_MODEL_SUMMONER_SUMMON_SAHAMUTT(OBJECT *o, int index, float Luminosity)
{
    vec3_t Position;
    float Matrix[3][4];
    {
        if (o->LifeTime < 20)
        {
            SetAction(o, 0);
            o->Alpha -= (0.05f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->CurrentAction == 0 && o->Alpha < 0.3f)
        {
            o->Alpha += (0.05f) * FPS_ANIMATION_FACTOR;
            {
                o->Angle[2] = CreateAngle2D(o->Position, o->HeadTargetAngle);
                float dx = o->HeadTargetAngle[0] - o->Position[0];
                float dy = o->HeadTargetAngle[1] - o->Position[1];
                o->Distance = sqrtf(dx * dx + dy * dy);
            }
        }
        else
        {
            SetAction(o, 1);
            if (o->AnimationFrame >= 12.0f)
            {
                o->AnimationFrame = 12.0f;
            }

            if (o->AnimationFrame >= 11.0f)
            {
                if (o->Alpha > 0)
                    o->Alpha -= (0.3f) * FPS_ANIMATION_FACTOR;
                else
                    o->Alpha = 0;
            }
            else if (o->AnimationFrame < 3.0f && o->Alpha < 0.7f)
            {
                o->Alpha += (0.05f) * FPS_ANIMATION_FACTOR;
            }

            if (o->AnimationFrame > 4.0f && o->AnimationFrame < 12.0f)
            {
                AngleMatrix(o->Angle, Matrix);
                vec3_t vMoveDir, Position;
                if (o->AnimationFrame < 10.0f)
                {
                    Vector(0, o->Distance / -13.0f, 0, vMoveDir);
                }
                else
                {
                    Vector(0, o->Distance / -45.0f, 0, vMoveDir);
                }
                VectorRotate(vMoveDir, Matrix, Position);
                VectorAddScaled(o->Position, Position, o->Position, FPS_ANIMATION_FACTOR);
            }
            if (o->AnimationFrame <= 4.0f)
            {
                o->Angle[2] = CreateAngle2D(o->Position, o->HeadTargetAngle);
                float dx = o->HeadTargetAngle[0] - o->Position[0];
                float dy = o->HeadTargetAngle[1] - o->Position[1];
                o->Distance = sqrtf(dx * dx + dy * dy);
            }
        }
        o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);

        o->MotionTrace.Advance(FPS_ANIMATION_FACTOR, o->Position);

        if (o->AnimationFrame >= 11.0f)
        {
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                vec3_t position;
                o->MotionTrace.Sample(WorldTime, birthTime.FrameFraction(), o->Position, position);
                CreateBomb3(position, o->SubType);
                if (WorldRandom() % 2 == 0)
                    PlayBuffer(SOUND_SUMMON_EXPLOSION);
            }
        }

        EmitSahamuttFlames(*o);
    }
    return true;
}

// MODEL_SUMMONER_SUMMON_NEIL
bool MoveBehavior::Move_MODEL_SUMMONER_SUMMON_NEIL(OBJECT *o, int index, float Luminosity)
{
    float Matrix[3][4];
    {
        if (o->LifeTime < 20)
            o->Alpha -= (0.05f) * FPS_ANIMATION_FACTOR;
        else if (o->Alpha < 0.7f)
            o->Alpha += (0.04f) * FPS_ANIMATION_FACTOR;

        if (o->AnimationFrame > 8 && o->Skill == 0)
        {
            o->Skill = 1;
            CreateEffect(MODEL_SUMMONER_SUMMON_NEIL_NIFE1, o->HeadTargetAngle, o->Angle, o->Light,
                         o->SubType);
            if (o->SubType >= 1)
                CreateEffect(MODEL_SUMMONER_SUMMON_NEIL_NIFE2, o->HeadTargetAngle, o->Angle,
                             o->Light, o->SubType);
            if (o->SubType >= 2)
                CreateEffect(MODEL_SUMMONER_SUMMON_NEIL_NIFE3, o->HeadTargetAngle, o->Angle,
                             o->Light, o->SubType);
        }
        if (o->AnimationFrame > 10 && o->Skill == 1)
        {
            o->Skill = 2;

            AngleMatrix(o->Angle, Matrix);
            vec3_t vMoveDir, vPosition;
            Vector(0, -60.0f, 0, vMoveDir);
            VectorRotate(vMoveDir, Matrix, vPosition);
            VectorAdd(o->Position, vPosition, vPosition);
            CreateEffect(MODEL_SUMMONER_SUMMON_NEIL_GROUND1, vPosition, o->Angle, o->Light,
                         o->SubType, o);

            CreateEffect(MODEL_SUMMONER_SUMMON_NEIL_GROUND1, o->HeadTargetAngle, o->Angle, o->Light,
                         o->SubType);
            if (o->SubType >= 1)
                CreateEffect(MODEL_SUMMONER_SUMMON_NEIL_GROUND2, o->HeadTargetAngle, o->Angle,
                             o->Light, o->SubType);
            if (o->SubType >= 2)
                CreateEffect(MODEL_SUMMONER_SUMMON_NEIL_GROUND3, o->HeadTargetAngle, o->Angle,
                             o->Light, o->SubType);

            PlayBuffer(SOUND_SUMMON_REQUIEM);
        }
        // 			if (o->AnimationFrame > 11 && o->AnimationFrame < 12)
        // 				EarthQuake = (float)(-WorldRandom()%2-5)*0.1f;
    }
    return true;
}

// MODEL_SUMMONER_SUMMON_LAGUL
bool MoveBehavior::Move_MODEL_SUMMONER_SUMMON_LAGUL(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->SubType == 0)
        {
            int anEffectVolume[3] = {5, 4, 3};
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR /
                                                                   anEffectVolume[(int)o->PKKey]))
            {
                vec3_t vPos, vLight;

                VectorCopy(o->HeadTargetAngle, vPos);
                vPos[0] += (float)(WorldRandom() % 500 - 250);
                vPos[1] += (float)(WorldRandom() % 500 - 250);
                Vector(1.0f, 1.0f, 1.0f, vLight);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, 57, 3.5f);

                VectorCopy(o->HeadTargetAngle, vPos);
                vPos[0] += (float)(WorldRandom() % 400 - 200);
                vPos[1] += (float)(WorldRandom() % 400 - 200);
                Vector(0.6f, 0.1f, 1.f, vLight);
                CreateEffect(BITMAP_CLOUD, vPos, o->Angle, vLight, 0, NULL, -1, 0, 0, 0, 2.0f);

                Vector(0.6f, 0.1f, 1.f, vLight);
                CreateParticle(BITMAP_TWINTAIL_WATER, vPos, o->Angle, vLight, 1);
            }
        }
        else if (o->SubType == 1)
        {
            SetAction(o, 0);

            if (o->LifeTime < 30)
                o->Alpha = o->LifeTime / 40.f;
            else if (o->LifeTime > 144)
                o->Alpha = (160 - o->LifeTime) / 20.f;
        }
    }
    return true;
}

// BITMAP_MAGIC
bool MoveBehavior::Move_BITMAP_MAGIC(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType == 0)
    {
        CreateEffectFpsChecked(BITMAP_MAGIC, o->Position, o->Angle, o->Light, 1);
        if (o->LifeTime > 5 && o->LifeTime < 10)
        {
            CreateParticleFpsChecked(BITMAP_FLARE, o->Position, o->Angle, o->Light, 0, 0.19f, o);
        }
    }
    else if (o->SubType == 2 || o->SubType == 3 || o->SubType == 7)
    {
        if (o->LifeTime > 5 && o->LifeTime < 10)
        {
            if (o->SubType == 3)
                CreateParticleFpsChecked(BITMAP_FLARE, o->Position, o->Angle, o->Light, 10, 0.19f,
                                         o);
            else
                CreateParticleFpsChecked(BITMAP_FLARE, o->Position, o->Angle, o->Light, 0, 0.19f,
                                         o);
        }
    }
    else if (o->SubType == 4)
    {
        CreateParticleFpsChecked(BITMAP_FLARE, o->Position, o->Angle, o->Light, 12, 0.19f, o);
    }
    else if (o->SubType == 8)
    {
        o->Scale += (1.8f) * FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 9)
    {
        const float growing = (std::min)(FPS_ANIMATION_FACTOR, (std::max)(0.f, o->LifeTime - 19.f));
        o->Alpha = (std::max)(0.f, (std::min)(1.f, o->Alpha + 0.05f * growing) -
                                       0.05f * (FPS_ANIMATION_FACTOR - growing));

        o->HeadAngle[0] += (4.0f) * FPS_ANIMATION_FACTOR;
        o->HeadAngle[1] -= (8.0f) * FPS_ANIMATION_FACTOR;
        o->HeadAngle[2] += (4.0f) * FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 10)
    {
        const float growing = (std::min)(FPS_ANIMATION_FACTOR, (std::max)(0.f, o->LifeTime - 19.f));
        o->Alpha = (std::max)(0.f, (std::min)(1.f, o->Alpha + 0.05f * growing) -
                                       0.03f * (FPS_ANIMATION_FACTOR - growing));
    }
    else if (o->SubType == 11)
    {
        o->HeadAngle[0] += (2.0f) * FPS_ANIMATION_FACTOR;
        o->HeadAngle[1] -= (2.0f) * FPS_ANIMATION_FACTOR;
        o->HeadAngle[2] += (2.0f) * FPS_ANIMATION_FACTOR;

        AdvanceTargetFade(*o, o->Alpha, FPS_ANIMATION_FACTOR);
    }
    else if (o->SubType == 12)
    {
        AdvanceFadingMagic(*o, FPS_ANIMATION_FACTOR);
    }
    else if (o->SubType == 13 || o->SubType == 14)
    {
        constexpr float Growth = 1.1f, FadeScale = 4.f, EndScale = 8.f;
        const float untilEnd =
            o->Scale > 0.f ? (std::max)(0.f, std::log(EndScale / o->Scale) / std::log(Growth))
                           : FPS_ANIMATION_FACTOR;
        const float frames = (std::min)(FPS_ANIMATION_FACTOR, untilEnd);
        const float untilFade =
            o->Scale > 0.f ? (std::max)(0.f, std::log(FadeScale / o->Scale) / std::log(Growth))
                           : frames;
        o->Scale *= std::pow(Growth, frames);
        const float fading = (std::max)(0.f, frames - untilFade);
        VectorScale(o->Light, std::pow(0.95f, frames) * std::pow(0.5f, fading), o->Light);
        if (FPS_ANIMATION_FACTOR > 0.f && untilEnd <= FPS_ANIMATION_FACTOR)
        {
            o->Scale = EndScale;
            RetireEffect(o);
        }
    }
    return true;
}

// BITMAP_OUR_INFLUENCE_GROUND, BITMAP_ENEMY_INFLUENCE_GROUND
bool MoveBehavior::Move_BITMAP_OUR_INFLUENCE_GROUND(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType == 0)
    {
        if (o->Owner == NULL)
        {
            RetireEffect(o);
            return true;
        }
        if (o->Owner->Live == false)
            RetireEffect(o);

        VectorCopy(o->Owner->Position, o->Position);

        o->Alpha -= (0.02f) * FPS_ANIMATION_FACTOR;
        o->Scale += (0.01f) * FPS_ANIMATION_FACTOR;

        if (o->Alpha < 0.f)
        {
            o->Alpha = 1.0f;
            o->Scale = 0.6f;
        }

        if (o->LifeTime < 25)
            o->AlphaTarget -= (0.02f) * FPS_ANIMATION_FACTOR;
        else if (o->AlphaTarget < 1.0f)
            o->AlphaTarget += (0.02f) * FPS_ANIMATION_FACTOR;

        if (1 <= o->LifeTime)
            o->LifeTime = 50;
    }
    return true;
}

// BITMAP_MAGIC_ZIN
bool MoveBehavior::Move_BITMAP_MAGIC_ZIN(OBJECT *o, int index, float Luminosity)
{
    switch (o->SubType)
    {
    case 0:
        if (o->LifeTime < 20)
            o->Alpha -= (0.05f) * FPS_ANIMATION_FACTOR;
        else if (o->Alpha < 1.0f)
            o->Alpha += (0.05f) * FPS_ANIMATION_FACTOR;
        break;
    case 1:
        if (o->LifeTime < 20)
            o->Alpha -= (0.03f) * FPS_ANIMATION_FACTOR;
        else if (o->Alpha < 0.7f)
            o->Alpha += (0.06f) * FPS_ANIMATION_FACTOR;
        break;
    case 2:
        if (o->Scale < 3.5f)
            o->Scale += (0.1f) * FPS_ANIMATION_FACTOR;
        if (o->LifeTime < 20)
            o->Alpha -= (0.05f) * FPS_ANIMATION_FACTOR;
        else if (o->Alpha < 1.0f)
            o->Alpha += (0.05f) * FPS_ANIMATION_FACTOR;
        break;
    }
    return true;
}

// BITMAP_PIN_LIGHT
bool MoveBehavior::Move_BITMAP_PIN_LIGHT(OBJECT *o, int index, float Luminosity)
{
    vec3_t Position;
    switch (o->SubType)
    {
    case 3:
    case 0:
        Position[0] = o->Position[0] + (float)(WorldRandom() % 500 - 250);
        Position[1] = o->Position[1] + (float)(WorldRandom() % 500 - 250);
        Position[2] = o->Position[2] - (float)(WorldRandom() % 100) + 150.0f;
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
        {
            if (o->SubType == 3)
                CreateParticle(o->Type, Position, o->Angle, o->Light, 1, o->Scale);
            else
                CreateParticle(o->Type, Position, o->Angle, o->Light, 0, o->Scale);
        }
        break;
    case 1:
    case 2:
        if (o->Owner == NULL || o->Owner->Live == false)
            RetireEffect(o);
        else
        {
            o->LifeTime = 100;
            BMD *pModel = &Models[o->Owner->Type];
            int iBone = WorldRandom() % pModel->NumBones;
            vec3_t vRelativePos, vWorldPos;
            Vector(0.f, 0.f, 100.f, vRelativePos);
            if (!pModel->Bones[iBone].Dummy)
            {
                pModel->TransformPosition(o->Owner->BoneTransform[iBone], vRelativePos, vWorldPos,
                                          false);
                VectorScale(vWorldPos, pModel->BodyScale, vWorldPos);
                VectorAdd(vWorldPos, o->Owner->Position, vWorldPos);
                vWorldPos[2] -= 20.f;
                for (auto birthTime :
                     sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
                {
                    CreateParticle(o->Type, vWorldPos, o->Angle, o->Light, 0, o->Scale);
                }
            }
        }
        break;
    case 4:
        if (o->Owner == NULL || o->Owner->Live == false || o->Alpha <= 0.0f)
        {
            RetireEffect(o);
        }
        else
        {
            o->Scale -= (0.02f) * FPS_ANIMATION_FACTOR;
            o->Alpha -= (0.001f) * FPS_ANIMATION_FACTOR;

            OBJECT *Owner = o->Owner;
            BMD *pModel = &Models[o->Owner->Type];
            vec3_t vWorldPos, vRelativePos;
            Vector(0.f, 0.f, 0.f, vRelativePos);

            if (!pModel->Bones[11].Dummy)
            {
                pModel->BodyScale = Owner->Scale;
                pModel->Animation(BoneTransform, Owner->AnimationFrame, Owner->PriorAnimationFrame,
                                  Owner->PriorAction, Owner->Angle, Owner->HeadAngle, false, false);
                pModel->TransformByObjectBone(vWorldPos, Owner, 11);
                VectorCopy(vWorldPos, o->Position);

                CreateSprite(BITMAP_PIN_LIGHT, vWorldPos, o->Scale, o->Light, Owner, o->Angle[1]);
            }
        }
        break;
    }
    return true;
}

// BITMAP_ORORA
bool MoveBehavior::Move_BITMAP_ORORA(OBJECT *o, int index, float Luminosity)
{
    if (o->Owner == NULL || o->Owner->Live == false)
        RetireEffect(o);
    else
    {
        if (o->LifeTime <= 5 || Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 5.f))
        {
            vec3_t position, angle, light;
            VectorCopy(o->Position, position);
            VectorCopy(o->Angle, angle);
            VectorCopy(o->Light, light);
            const int type = o->Type;
            const int subtype = o->SubType;
            OBJECT *owner = o->Owner;
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 5.f));
            RetireEffect(o);
            CreateEffect(type, position, angle, light, subtype, owner);
        }
    }
    return true;
}

namespace
{
void SampleGatheringPosition(OBJECT &effect, BMD &model, double time, float fraction,
                             vec3_t position)
{
    constexpr int GatheringBone = 33;
    vec3_t offset{0.f, 0.f, 10.f};
    ObjectDrawInput draw(effect.Owner);
    VectorCopy(effect.StartPosition, draw.position);
    AnimationPoseSample pose(effect.Owner, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    std::array<vec34_t, MAX_BONES> bones;
    draw.bones = pose.EvaluateAtTime(model, *effect.Owner, time, fraction, bones.data(),
                                     effect.Owner->BoneTransform);
    model.TransformByObjectBone(position, draw, GatheringBone, offset);
}
} // namespace

void MoveBehavior::EmitGatheringBirth(OBJECT &effect, vec3_t origin, bool lightning)
{
    constexpr int Siblings = 3;
    for (int i = 0; i < Siblings; ++i)
    {
        vec3_t local{0.f, effect.SubType == 3 ? 25.f : 120.f, 0.f}, position;
        vec3_t angle{float(WorldRandom() % 360), 0.f, float(WorldRandom() % 360)};
        float matrix[3][4];
        AngleMatrix(angle, matrix);
        VectorRotate(local, matrix, position);
        VectorAdd(origin, position, position);
        if (lightning)
            CreateJoint(BITMAP_JOINT_THUNDER, position, origin, angle, 3, nullptr, 10.f, 10, 10);
        else if (effect.SubType == 3)
            CreateParticle(BITMAP_SPARK + 1, position, angle, effect.Light, 26,
                           (WorldRandom() % 10 + 5) / 25.f, &effect);
        else
        {
            vec3_t light{1.f, 1.f, 1.f};
            CreateParticle(BITMAP_SPARK + 1, position, angle, light, 2,
                           (WorldRandom() % 50 + 10) / 100.f, &effect);
        }
    }
}

// BITMAP_GATHERING
bool MoveBehavior::Move_BITMAP_GATHERING(OBJECT *o, int index, float Luminosity)
{
    const bool attached = o->SubType == 1 || o->SubType == 2;
    if (attached)
    {
        BMD &model = Models[o->Owner->Type];
        SampleGatheringPosition(*o, model, WorldTime, 1.f, o->Position);
        for (float life = Core::Time::ReferenceSample(o->LifeTime);
             life > 0.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life); --life)
        {
            const bool lightning = static_cast<int>(life) % 2 == 0;
            if (!lightning && o->SubType == 2)
                continue;
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR -
                                                                 std::max(0.f, o->LifeTime - life));
            vec3_t origin;
            SampleGatheringPosition(*o, model, WorldTime, birth.FrameFraction(), origin);
            EmitGatheringBirth(*o, origin, lightning);
        }
    }
    else
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            EmitGatheringBirth(*o, o->Position, false);
    if (o->SubType == 3)
        return true;
    for (int i = 0; i < 3; ++i)
        CreateSprite(BITMAP_SHINY + 1, o->Position,
                     (WorldRandom() % 8 + 8) * (attached ? 0.2f : 0.3f), o->Light, o,
                     float(WorldRandom() % 360));
    return true;
}

void MoveBehavior::EmitThunderCloud(OBJECT &effect)
{
    const auto jitter = [&](const vec3_t source, vec3_t position) {
        VectorCopy(source, position);
        position[0] += WorldRandom() % 100 - 50;
        position[1] += WorldRandom() % 100 - 50;
    };
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
    {
        vec3_t start, end;
        jitter(effect.StartPosition, start);
        jitter(effect.Position, end);
        const float scale = (WorldRandom() % 400 + 100) / 10.f;
        CreateJoint(BITMAP_JOINT_THUNDER, start, end, effect.Angle, effect.SubType == 1 ? 33 : 16,
                    nullptr, scale);
    }
    vec3_t light{0.45f, 0.45f, 0.7f}, position;
    const float magicFrames = std::min(FPS_ANIMATION_FACTOR, std::max(0.f, effect.LifeTime - 10.f));
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(magicFrames / 2.0, magicFrames))
    {
        jitter(effect.Position, position);
        CreateEffect(BITMAP_MAGIC + 1, position, effect.Angle, light, 11, &effect);
    }
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.0))
    {
        jitter(effect.Position, position);
        CreateParticle(BITMAP_SMOKE, position, effect.Angle, light, 54, 2.8f);
    }
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.0))
        CreateEffect(MODEL_STONE1 + WorldRandom() % 2, effect.Position, effect.Angle, light, 13);
    const float energyFrames = std::min(FPS_ANIMATION_FACTOR, std::max(0.f, effect.LifeTime - 5.f));
    Vector(0.15f, 0.15f, 0.4f, light);
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(energyFrames / 5.0, energyFrames))
    {
        jitter(effect.Position, position);
        CreateEffect(BITMAP_CHROME_ENERGY2, position, effect.Angle, light, 0);
    }
}

// BITMAP_JOINT_THUNDER
bool MoveBehavior::Move_BITMAP_JOINT_THUNDER(OBJECT *o, int index, float Luminosity)
{
    {
        EmitThunderCloud(*o);

        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(
                 o->Owner != NULL && o->Owner->BoneTransform != NULL ? FPS_ANIMATION_FACTOR / 5.f
                                                                     : 0.f))
        {
            BMD *pTargetModel = &Models[o->Owner->Type];
            int iNumBones = pTargetModel->NumBones;
            float fRandom;
            vec3_t vLight, vRelativePos, vPos, vAngle;
            Vector(0.0f, 0.0f, 0.0f, vRelativePos);
            VectorCopy(o->Angle, vAngle);
            const float fraction = birthTime.FrameFraction();
            AnimationPoseSample pose(o->Owner, pTargetModel->BoneHead, pTargetModel->BodyHeight,
                                     false, pTargetModel->PoseAssetIdentity());
            std::array<vec34_t, MAX_BONES> bones;
            pose.EvaluateAtTime(*pTargetModel, *o->Owner, WorldTime, fraction, bones.data());
            vec3_t origin;
            o->Owner->MotionTrace.Sample(WorldTime, fraction, o->Owner->Position, origin);
            for (int i = 0; i < iNumBones; ++i)
            {
                if (iNumBones > 100 && WorldRandom() % iNumBones > iNumBones / 10)
                    continue;
                else if (iNumBones > 50 && WorldRandom() % iNumBones > iNumBones / 5)
                    continue;
                else if (iNumBones > 20 && WorldRandom() % iNumBones > iNumBones / 2)
                    continue;
                VectorTransform(vRelativePos, bones[i], vPos);
                VectorScale(vPos, o->Owner->Scale, vPos);
                VectorAdd(vPos, origin, vPos);

                Vector(0.2f, 0.2f, 0.8f, vLight);
                fRandom = 3.0f + ((float)(WorldRandom() % 20 - 10) * 0.1f);
                CreateParticle(BITMAP_LIGHT, vPos, vAngle, vLight, 5, fRandom);
            }
        }
    }
    return true;
}

// BITMAP_IMPACT
bool MoveBehavior::Move_BITMAP_IMPACT(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    float Matrix[3][4];
    {
        float ScaleBk = 0.f;
        if (o->Scale < 2.f)
        {
            o->Scale += (0.1f) * FPS_ANIMATION_FACTOR;
            ScaleBk = o->Scale;
        }
        else
        {
            if (o->Scale < 2.4f)
                o->Scale += (0.02f) * FPS_ANIMATION_FACTOR;
            else
                o->Scale = 2.f;

            if (o->Scale >= 2.2f)
                ScaleBk = 2.2f - (o->Scale - 2.2f);
            else
                ScaleBk = o->Scale;
        }
        vec3_t Light, P, dp;

        if (o->LifeTime <= 40)
        {
            o->Alpha -= (0.1f) * FPS_ANIMATION_FACTOR;
            o->BlendMeshLight -= (0.1f) * FPS_ANIMATION_FACTOR;
        }
        float Matrix[3][4];
        Vector(-10.f, -30.f, 0.f, P);
        Vector(o->Light[0] * o->Alpha, o->Light[1] * o->Alpha, o->Light[2] * o->Alpha, Light);
        AngleMatrix(o->Owner->Angle, Matrix);
        VectorRotate(P, Matrix, dp);
        VectorAdd(dp, o->Owner->Position, o->Position);
        o->Position[2] += 130.f;
        CreateSprite(BITMAP_IMPACT, o->Position, ScaleBk, Light, o);
        CreateSprite(BITMAP_SHINY + 1, o->Position, ScaleBk, Light, o, -WorldTime * 0.08f);
        if (o->Scale > 1.f)
            CreateSprite(BITMAP_ORORA, o->Position, ScaleBk, Light, o, -WorldTime * 0.08f);
        if (o->Scale > 2.f)
            CreateSprite(BITMAP_ORORA, o->Position, ScaleBk, Light, o, WorldTime * 0.08f);
    }
    return true;
}

// BITMAP_FLAME
bool MoveBehavior::Move_BITMAP_FLAME(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    float Matrix[3][4];
    vec3_t p;
    if (o->SubType == 0)
    {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            for (int j = 0; j < 6; j++)
            {
                Vector((float)(WorldRandom() % 50 - 25), (float)(WorldRandom() % 50 - 25), 0.f,
                       Position);
                VectorAdd(Position, o->Position, Position);
                CreateParticle(BITMAP_FLAME, Position, o->Angle, Light);
            }
        }
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 8.0))
        {
            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, o->Position, o->Angle, o->Light);
        }

        Vector(Luminosity * 1.f, Luminosity * 0.4f, Luminosity * 0.f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
        if (o->Owner == &Hero->Object)
        {
            constexpr float AttackInterval = 20.f;
            for (float life =
                     Core::Time::ReferenceSample(o->LifeTime / AttackInterval) * AttackInterval;
                 life > 0.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life);
                 life -= AttackInterval)
                AttackCharacterRange(o->Skill, o->Position, 150.f, o->Weapon, o->PKKey);
        }
    }
    else if (o->SubType == 1 || o->SubType == 2)
    {
        for (int j = 0; j < 18; j++)
        {
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.0))
            {
                Vector(0.f, 250.f, 0.f, p);
                Vector(0.f, 0.f, j * 20.f, Angle);
                AngleMatrix(Angle, Matrix);
                VectorRotate(p, Matrix, Position);
                VectorAdd(Position, o->Position, Position);
                Position[0] += WorldRandom() % 64 - 32;
                Position[1] += WorldRandom() % 64 - 32;
                if (o->SubType == 1)
                    CreateParticle(BITMAP_FLAME, Position, o->Angle, Light, 0, 1.2f);
                else if (o->SubType == 2)
                    CreateParticle(BITMAP_FIRE + 3, Position, o->Angle, Light, 13, 2.5f);
            }
        }
    }
    else if (o->SubType == 3)
    {
        for (int j = 0; j < 3; j++)
        {
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.0))
            {
                CreateParticle(BITMAP_FLAME, o->Position, o->Angle, Light, 6);
                Vector((float)(WorldRandom() % 10 - 5), (float)(WorldRandom() % 10 - 5), 40.f,
                       Position);
                VectorAdd(Position, o->Position, Position);
                CreateParticle(BITMAP_TRUE_FIRE, Position, o->Angle, Light, 0, 2.8f);
            }
        }
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            Vector((float)(WorldRandom() % 10 - 5), (float)(WorldRandom() % 10 - 5), -40.f,
                   Position);
            VectorAdd(Position, o->Position, Position);
            CreateParticle(BITMAP_SMOKE, Position, o->Angle, Light, 21, 0.8f);

            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, o->Position, o->Angle, o->Light, 12);
        }
        Vector(Luminosity * 1.f, Luminosity * 0.4f, Luminosity * 0.f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
    }
    else if (o->SubType == 4)
    {
        o->Scale += (20.f) * FPS_ANIMATION_FACTOR;
        for (int j = 0; j < 18; j++)
        {
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.0))
            {
                Vector(0.f, 150.f - o->Scale + 20.f * birthTime.RemainingFrames(), 0.f, p);
                Vector(0.f, 0.f, j * 20.f, Angle);
                AngleMatrix(Angle, Matrix);
                VectorRotate(p, Matrix, Position);
                VectorAdd(Position, o->Position, Position);
                Position[0] += WorldRandom() % 64 - 32;
                Position[1] += WorldRandom() % 64 - 32;
                CreateParticle(BITMAP_FLAME, Position, o->Angle, Light, 0, 1.2f);
            }
        }
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, o->Position, o->Angle, o->Light, 12);
    }
    else if (o->SubType == 5)
    {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            Vector((float)(WorldRandom() % 32 - 16), (float)(WorldRandom() % 32 - 16), 0.f,
                   Position);
            VectorAdd(Position, o->Position, Position);
            CreateParticle(BITMAP_FLAME, Position, o->Angle, Light, 0, o->Scale);
        }
    }
    else if (o->SubType == 6)
    {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            Vector((float)(WorldRandom() % 32 - 16), (float)(WorldRandom() % 32 - 16), 0.f,
                   Position);
            VectorAdd(Position, o->Position, Position);
            CreateParticle(BITMAP_FLAME, Position, o->Angle, o->Light, 12, o->Scale);
        }
    }
    return true;
}

// MODEL_RAKLION_BOSS_CRACKEFFECT
bool MoveBehavior::Move_MODEL_RAKLION_BOSS_CRACKEFFECT(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType == 0)
    {
        o->Alpha -= (0.03f) * FPS_ANIMATION_FACTOR;
    }
    return true;
}

// MODEL_RAKLION_BOSS_MAGIC
bool MoveBehavior::Move_MODEL_RAKLION_BOSS_MAGIC(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType == 0)
    {
        o->Alpha -= (0.03f) * FPS_ANIMATION_FACTOR;
    }
    return true;
}

// BITMAP_FIRE_HIK2_MONO
bool MoveBehavior::Move_BITMAP_FIRE_HIK2_MONO(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    float Matrix[3][4];
    vec3_t p;
    if (o->SubType == 0)
    {
        for (int i = 0; i < 2; ++i)
        {
            Vector((float)(WorldRandom() % 30 - 15), (float)(WorldRandom() % 30 - 15), 0.f, p);
            VectorAdd(p, o->Position, Position);
            if (WorldRandom() % 3 != 0)
            {
                CreateParticleFpsChecked(BITMAP_FLAME, Position, o->Angle, o->Light, 11, 1.4f);
            }

            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 8.0))
            {
                CreateEffect(MODEL_ICE_SMALL, Position, o->Angle, o->Light, 0);
            }
        }
    }
    else if (o->SubType == 1)
    {
        for (int j = 0; j < 9; j++)
        {
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.0))
            {
                Vector(0.f, 250.f, 0.f, p);
                Vector(0.f, 0.f, j * 40.f, Angle);
                AngleMatrix(Angle, Matrix);
                VectorRotate(p, Matrix, Position);
                VectorAdd(Position, o->Position, Position);
                Position[0] += WorldRandom() % 64 - 32;
                Position[1] += WorldRandom() % 64 - 32;
                CreateParticle(BITMAP_FIRE + 3, Position, o->Angle, Light, 13, 2.5f);
            }
        }
    }
    return true;
}

// BITMAP_CLOUD
bool MoveBehavior::Move_BITMAP_CLOUD(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType == 0)
    {
        o->Light[0] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
        o->Light[1] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
        o->Light[2] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
        o->Scale += (0.03f) * FPS_ANIMATION_FACTOR;
    }
    return true;
}

// MODEL_CHAIN_LIGHTNING
bool MoveBehavior::Move_MODEL_CHAIN_LIGHTNING(OBJECT *o, int index, float Luminosity)
{
    {
        switch (o->SubType)
        {
        case 0:
        case 1:
        case 2: {
            OBJECT *pSourceObj = o->Owner;
            CHARACTER *pTargetChar = &CharactersClient[FindCharacterIndex(o->m_sTargetIndex)];
            OBJECT *pTargetObj = &pTargetChar->Object;

            if (pSourceObj == NULL || pSourceObj->Live == false || pTargetObj == NULL ||
                pTargetObj->Live == false || (pTargetChar->Dead > 0) == true)
            {
                o->LifeTime = 0;
                RetireEffect(o);
                break;
            }

            BMD *pSourceModel = &Models[pSourceObj->Type];
            BMD *pTargetModel = &Models[pTargetObj->Type];

            vec3_t vLight, vRelativePos, vPos, vAngle;
            Vector(0.0f, 0.0f, 0.0f, vRelativePos);

            if (o->SubType == 1 || o->SubType == 2)
            {
                if (pSourceObj == pTargetObj)
                    break;
            }

            if (o->SubType == 0)
            {
                VectorCopy(pSourceObj->Position, pSourceModel->BodyOrigin);

                Vector(0.4f, 0.4f, 1.0f, vLight);
                pSourceModel->TransformPosition(pSourceObj->BoneTransform[37], vRelativePos, vPos,
                                                true);
                Vector(-60.f, 0.f, pSourceObj->Angle[2], vAngle);
                CreateJointFpsChecked(BITMAP_JOINT_THUNDER, vPos, pTargetObj->Position, vAngle, 0,
                                      pTargetObj, 50.f, -1, 0, 0, -1, vLight);
                CreateJointFpsChecked(BITMAP_JOINT_THUNDER, vPos, pTargetObj->Position, vAngle, 0,
                                      pTargetObj, 10.f, -1, 0, 0, -1, vLight);
                Vector(0.f, 0.f, (pSourceObj->Angle[2]) + 60.f, vAngle);
                CreateJointFpsChecked(BITMAP_JOINT_THUNDER, vPos, pTargetObj->Position, vAngle, 0,
                                      pTargetObj, 50.f, -1, 0, 0, -1, vLight);
                CreateJointFpsChecked(BITMAP_JOINT_THUNDER, vPos, pTargetObj->Position, vAngle, 0,
                                      pTargetObj, 10.f, -1, 0, 0, -1, vLight);

                pSourceModel->TransformPosition(pSourceObj->BoneTransform[28], vRelativePos, vPos,
                                                true);
                Vector(-60.f, 0.f, pSourceObj->Angle[2], vAngle);
                CreateJointFpsChecked(BITMAP_JOINT_THUNDER, vPos, pTargetObj->Position, vAngle, 0,
                                      pTargetObj, 50.f, -1, 0, 0, -1, vLight);
                CreateJointFpsChecked(BITMAP_JOINT_THUNDER, vPos, pTargetObj->Position, vAngle, 0,
                                      pTargetObj, 10.f, -1, 0, 0, -1, vLight);
                Vector(0.f, 0.f, (pSourceObj->Angle[2]) - 60.f, vAngle);
                CreateJointFpsChecked(BITMAP_JOINT_THUNDER, vPos, pTargetObj->Position, vAngle, 0,
                                      pTargetObj, 50.f, -1, 0, 0, -1, vLight);
                CreateJointFpsChecked(BITMAP_JOINT_THUNDER, vPos, pTargetObj->Position, vAngle, 0,
                                      pTargetObj, 10.f, -1, 0, 0, -1, vLight);
            }
            else if (o->SubType == 1 || o->SubType == 2)
            {
                VectorCopy(pSourceObj->Position, vPos);
                vPos[2] += 80.0f;
                //Vector(0.f, 0.f, (pSourceObj->Angle[2])-60.f, vAngle);
                vec3_t vTargetPos;
                VectorCopy(pTargetObj->Position, vTargetPos);
                vTargetPos[2] += 80.0f;
                //Vector(0.f, 0.f, 0.f, vAngle);
                CreateJointFpsChecked(BITMAP_JOINT_THUNDER, vPos, pTargetObj->Position,
                                      pTargetObj->Angle, 0, pTargetObj, 50.f, -1, 0, 0, -1, vLight);
                CreateJointFpsChecked(BITMAP_JOINT_THUNDER, vPos, pTargetObj->Position,
                                      pTargetObj->Angle, 0, pTargetObj, 10.f, -1, 0, 0, -1, vLight);
            }

            if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 15))
            {
                int iNumBones = pTargetModel->NumBones;
                float fRandom;
                for (int i = 0; i < iNumBones; i++)
                {
                    VectorCopy(pTargetObj->Position, pTargetModel->BodyOrigin);
                    pTargetModel->TransformPosition(pTargetObj->BoneTransform[i], vRelativePos,
                                                    vPos, true);

                    Vector(0.2f, 0.2f, 0.8f, vLight);
                    fRandom = 3.0f + ((float)(WorldRandom() % 20 - 10) * 0.1f);
                    CreateParticle(BITMAP_LIGHT, vPos, vAngle, vLight, 5, fRandom);
                }
            }
        }
        break;
        }
    }
    return true;
}

// MODEL_ALICE_DRAIN_LIFE
bool MoveBehavior::Move_MODEL_ALICE_DRAIN_LIFE(OBJECT *o, int index, float Luminosity)
{
    {
        int iNumBones = 0;

        vec3_t vSourcePos, vTargetPos, vLight, vRelativePos;
        Vector(0.0f, 0.0f, 0.0f, vRelativePos);

        OBJECT *pSourceObj = o->Owner;
        OBJECT *pTargetObj = pSourceObj->Owner;
        BMD *pSourceModel = &Models[pSourceObj->Type];
        BMD *pTargetModel = &Models[pTargetObj->Type];

        if (pSourceObj == NULL || pTargetObj == NULL || pSourceObj->Live == false ||
            pTargetObj->Live == false)
        {
            return true;
        }

        int iRandom = WorldRandom() % 10;
        int iCnt = 0;
        switch (iRandom)
        {
        case 0:
            iCnt = 0;
            break;
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
            iCnt = 1;
            break;
        case 8:
        case 9:
            iCnt = 2;
            break;
        }

        for (int i = 0; i < iCnt; i++)
        {
            VectorCopy(pSourceObj->Position, vSourcePos);
            vSourcePos[0] += ((float)((WorldRandom() % 80 - 40)));
            vSourcePos[1] += ((float)((WorldRandom() % 60 - 30)));
            vSourcePos[2] += (80.0f + ((float)((WorldRandom() % 180 - 100))));
            Vector(1.0f, 0.2f, 0.2f, vLight);
            CreateParticleFpsChecked(BITMAP_LIGHT + 2, vSourcePos, pSourceObj->Angle, vLight, 7,
                                     1.8f);
        }

        if (o->LifeTime <= 60)
        {
            for (int i = 0; i < iCnt; i++)
            {
                VectorCopy(pTargetObj->Position, vTargetPos);
                vTargetPos[0] += ((float)((WorldRandom() % 80 - 40)));
                vTargetPos[1] += ((float)((WorldRandom() % 60 - 30)));
                vTargetPos[2] += (80.0f + ((float)((WorldRandom() % 180 - 100))));
                Vector(1.0f, 0.2f, 0.2f, vLight);
                CreateParticleFpsChecked(BITMAP_LIGHT + 2, vTargetPos, pTargetObj->Angle, vLight, 7,
                                         1.8f);
            }
        }

        if (o->LifeTime <= 70 && o->LifeTime >= 66)
        {
            int iRandom = WorldRandom() % 10;
            int iCnt2 = 0;
            switch (iRandom)
            {
            case 0:
                iCnt2 = 0;
                break;
            case 1:
            case 2:
            case 3:
                iCnt2 = 1;
                break;
            case 4:
            case 5:
                iCnt2 = 2;
            case 6:
            case 7:
            case 8:
                iCnt2 = 3;
                break;
            case 9:
                iCnt2 = 4;
                break;
            }

            float fMatrix[3][4];
            vec3_t vDir;
            for (int i = 0; i < iCnt2; i++)
            {
                VectorCopy(pSourceObj->Position, pSourceModel->BodyOrigin);
                pSourceModel->TransformPosition(pSourceObj->BoneTransform[18], vRelativePos,
                                                vSourcePos, true);

                AngleMatrix(pSourceObj->Angle, fMatrix);
                vDir[0] = fMatrix[0][1];
                vDir[1] = fMatrix[1][1];
                vDir[2] = fMatrix[2][1];

                VectorNormalize(vDir);

                vSourcePos[0] =
                    vSourcePos[0] + ((vDir[0]) * 100.0f) + (float)((WorldRandom() % 10) * 5);
                vSourcePos[1] =
                    vSourcePos[1] + ((vDir[1]) * 100.0f) + (float)((WorldRandom() % 10) * 5);
                vSourcePos[2] += (float)((WorldRandom() % 10) * 5);

                VectorCopy(pTargetObj->Position, vTargetPos);
                vTargetPos[2] += 100.f + (float)(((WorldRandom() % 10 - 5)) * 4); // 80~120

                Vector(0.8f, 0.1f, 0.2f, vLight);
                CreateJointFpsChecked(BITMAP_DRAIN_LIFE_GHOST, vSourcePos, vTargetPos, o->Angle, 0,
                                      pSourceObj, 40.f, 0, 0, 0, -1, vLight);
            }
        }

        if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 64))
        {
            const float offset = (std::max)(0.f, o->LifeTime - 64.f);
            auto birthTime = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - offset);
            const float fraction = birthTime.FrameFraction();
            AnimationPoseSample pose(pTargetObj, pTargetModel->BoneHead, pTargetModel->BodyHeight,
                                     false, pTargetModel->PoseAssetIdentity());
            std::array<vec34_t, MAX_BONES> bones;
            pose.EvaluateAtTime(*pTargetModel, *pTargetObj, WorldTime, fraction, bones.data());
            vec3_t targetOrigin, targetAngle;
            pTargetObj->MotionTrace.Sample(WorldTime, fraction, pTargetObj->Position, targetOrigin);
            pSourceObj->MotionTrace.Sample(WorldTime, fraction, pSourceObj->Position, vSourcePos);
            vSourcePos[2] += 80.f;
            VectorCopy(pTargetObj->Angle, targetAngle);
            targetAngle[2] = pTargetObj->MotionTrace.SampleYaw(WorldTime, fraction, targetAngle[2]);
            Vector(1.f, 0.f, 0.1f, vLight);
            for (int i = 0; i < pTargetModel->NumBones; ++i)
            {
                if (!sessionKeeper_.Random()->FpsCheck(2, 1.f))
                    continue;
                VectorTransform(vRelativePos, bones[i], vTargetPos);
                VectorScale(vTargetPos, pTargetObj->Scale, vTargetPos);
                VectorAdd(vTargetPos, targetOrigin, vTargetPos);
                CreateJoint(BITMAP_JOINT_ENERGY, vTargetPos, vSourcePos, targetAngle, 45,
                            pSourceObj, 10.f, -1, 0, 0, -1, vLight);
            }
        }
    }
    return true;
}

// MODEL_ALICE_BUFFSKILL_EFFECT, MODEL_ALICE_BUFFSKILL_EFFECT2
bool MoveBehavior::Move_MODEL_ALICE_BUFFSKILL_EFFECT(OBJECT *o, int index, float Luminosity)
{
    float Matrix[3][4];
    {
        if (o->SubType == 0 || o->SubType == 1 || o->SubType == 2)
        {
            if (o->Owner == NULL || o->Owner->Live == false)
            {
                RetireEffect(o);
            }

            VectorCopy(o->Owner->Position, o->Position);
            o->Position[2] += (100) * FPS_ANIMATION_FACTOR;
            if (o->LifeTime > 20)
            {
                o->Alpha += (0.05f) * FPS_ANIMATION_FACTOR;
                o->BlendMeshLight += (0.05f) * FPS_ANIMATION_FACTOR;
            }
            else
            {
                o->Alpha -= (0.05f) * FPS_ANIMATION_FACTOR;
                o->BlendMeshLight -= (0.05f) * FPS_ANIMATION_FACTOR;
                if (o->Alpha < 0.f)
                {
                    RetireEffect(o);
                }
            }

            if (o->Type == MODEL_ALICE_BUFFSKILL_EFFECT)
            {
                o->Angle[2] += (8.f) * FPS_ANIMATION_FACTOR;
            }
            else if (o->Type == MODEL_ALICE_BUFFSKILL_EFFECT2)
            {
                o->Angle[2] -= (8.f) * FPS_ANIMATION_FACTOR;
            }

            o->Scale += (0.035f) * FPS_ANIMATION_FACTOR;

            float fRot = (WorldTime * 0.0006f) * 360.0f;
            vec3_t vLight;

            // flare01
            if (o->SubType == 0)
            {
                Vector(0.8f * o->Alpha, 0.1f * o->Alpha, 0.9f * o->Alpha, vLight);
            }
            else if (o->SubType == 1)
            {
                Vector(1.0f * o->Alpha, 1.0f * o->Alpha, 1.0f * o->Alpha, vLight);
            }
            else if (o->SubType == 2)
            {
                Vector(0.8f * o->Alpha, 0.5f * o->Alpha, 0.2f * o->Alpha, vLight);
            }

            if (o->SubType == 0 || o->SubType == 2)
            {
                CreateSprite(BITMAP_LIGHT, o->Position, 5.f, vLight, o, 0.f, 0);
                CreateSprite(BITMAP_LIGHT, o->Position, 5.f, vLight, o, 0.f, 0);
            }
            else if (o->SubType == 1)
            {
                CreateSprite(BITMAP_LIGHT, o->Position, 5.f, vLight, o, 0.f, 1);
                CreateSprite(BITMAP_LIGHT, o->Position, 5.f, vLight, o, 0.f, 1);
            }

            // shiny04
            if (o->SubType == 0)
            {
                Vector(0.7f * o->Alpha, 0.6f * o->Alpha, 0.9f * o->Alpha, vLight);
            }
            else if (o->SubType == 1)
            {
                Vector(1.0f * o->Alpha, 1.0f * o->Alpha, 1.0f * o->Alpha, vLight);
            }
            else if (o->SubType == 2)
            {
                Vector(0.8f * o->Alpha, 0.5f * o->Alpha, 0.2f * o->Alpha, vLight);
            }

            if (o->SubType == 0 || o->SubType == 2)
            {
                CreateSprite(BITMAP_SHINY + 5, o->Position, 2.0f, vLight, o, fRot);
                CreateSprite(BITMAP_SHINY + 5, o->Position, 1.0f, vLight, o, -fRot);
            }
            else if (o->SubType == 1)
            {
                CreateSprite(BITMAP_SHINY + 5, o->Position, 2.0f, vLight, o, fRot, 1);
                CreateSprite(BITMAP_SHINY + 5, o->Position, 1.0f, vLight, o, -fRot, 1);
            }

            vec3_t vAngle, vPos, vWorldPos;
            float Matrix[3][4];
            Vector(0.f, -200.f, 0.f, vPos);
            for (int i = 0; i < 3; ++i)
            {
                Vector((float)(WorldRandom() % 90), 0.f, (float)(WorldRandom() % 360), vAngle);
                AngleMatrix(vAngle, Matrix);
                VectorRotate(vPos, Matrix, vWorldPos);
                VectorSubtract(o->Position, vWorldPos, vWorldPos);

                if (o->SubType == 0)
                {
                    Vector(0.7f, 0.5f, 0.7f, vLight);
                }
                else if (o->SubType == 1)
                {
                    Vector(1.0f, 1.0f, 1.0f, vLight);
                }
                else if (o->SubType == 2)
                {
                    Vector(0.8f, 0.5f, 0.2f, vLight);
                }

                if (o->SubType == 0 || o->SubType == 2)
                {
                    CreateJointFpsChecked(BITMAP_JOINT_HEALING, vWorldPos, o->Position, vAngle, 15,
                                          o, 5.f, 0, 0, 0, 0, vLight);
                }
                else if (o->SubType == 1)
                {
                    CreateJointFpsChecked(BITMAP_JOINT_HEALING, vWorldPos, o->Position, vAngle, 16,
                                          o, 5.f, 0, 0, 0, 0, vLight);
                }
            }
        }
        else if (o->SubType == 3 || o->SubType == 4)
        {
            if (o->Owner == NULL || o->Owner->Live == false)
            {
                RetireEffect(o);
            }
            else
            {
                o->LifeTime = 100;

                BMD *pModel = &Models[o->Owner->Type];
                int iBone = WorldRandom() % pModel->NumBones;

                vec3_t vRelativePos, vWorldPos;
                Vector(0.f, 0.f, 0.f, vRelativePos);

                if (!pModel->Bones[iBone].Dummy)
                {
                    pModel->TransformPosition(o->Owner->BoneTransform[iBone], vRelativePos,
                                              vWorldPos, false);
                    VectorScale(vWorldPos, pModel->BodyScale, vWorldPos);
                    VectorAdd(vWorldPos, o->Owner->Position, vWorldPos);

                    if (o->SubType == 3)
                    {
                        CreateParticleFpsChecked(BITMAP_LIGHT + 2, vWorldPos, o->Angle, o->Light, 6,
                                                 o->Scale);
                        iBone = WorldRandom() % pModel->NumBones;
                        pModel->TransformPosition(o->Owner->BoneTransform[iBone], vRelativePos,
                                                  vWorldPos, false);
                        VectorScale(vWorldPos, pModel->BodyScale, vWorldPos);
                        VectorAdd(vWorldPos, o->Owner->Position, vWorldPos);
                        CreateParticleFpsChecked(BITMAP_LIGHT + 2, vWorldPos, o->Angle, o->Light, 6,
                                                 o->Scale);
                    }
                    else if (o->SubType == 4)
                    {
                        for (auto birthTime :
                             sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
                        {
                            vec3_t position;
                            VectorCopy(vWorldPos, position);
                            position[2] -= 20.f;
                            CreateParticle(BITMAP_TWINTAIL_WATER, position, o->Angle, o->Light, 2);
                        }
                    }
                }
            }
        }
    }
    return true;
}

void MoveBehavior::EmitLightningShockAir(OBJECT &effect, float duration)
{
    BMD &model = Models[effect.Owner->Type];
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(duration, duration))
    {
        vec3_t origin, position, light{1.f, 0.3f, 0.3f};
        effect.MotionTrace.Sample(WorldTime, birth.FrameFraction(), effect.Position, origin);
        CreateParticle(BITMAP_MAGIC, origin, effect.Angle, light, 0, 0.8f);
        Vector(1.f, 0.7f, 0.4f, light);
        for (int i = 0; i < 11; ++i)
        {
            if (WorldRandom() % 3 != 0)
                continue;
            const float scale = (WorldRandom() % 80 + 32) * 0.01f;
            for (int axis = 0; axis < 3; ++axis)
                position[axis] = origin[axis] + WorldRandom() % 70 - 35;
            CreateParticle(BITMAP_LIGHTNING_MEGA1 + WorldRandom() % 3, position, effect.Angle,
                           light, 0, scale);
        }
        Vector(1.f, 0.5f, 0.4f, light);
        AnimationPoseSample pose(effect.Owner, model.BoneHead, model.BodyHeight, false,
                                 model.PoseAssetIdentity());
        for (int i = 0; i < 2; ++i)
        {
            const float scale = (WorldRandom() % 60 + 22) * 0.01f;
            const int bone = WorldRandom() % 41;
            vec3_t offset;
            for (int axis = 0; axis < 3; ++axis)
                offset[axis] = WorldRandom() % 30 - 15;
            pose.SampleBonePosition(model, *effect.Owner, bone, offset, WorldTime,
                                    birth.FrameFraction(), position);
            CreateParticle(BITMAP_LIGHTNING_MEGA1 + WorldRandom() % 3, position, effect.Angle,
                           light, 0, scale);
        }
    }
}

// MODEL_LIGHTNING_SHOCK
bool MoveBehavior::Move_MODEL_LIGHTNING_SHOCK(OBJECT *o, int index, float Luminosity)
{
    {
        vec3_t vLight;

        if (o->SubType == 0)
        {
            const auto landed =
                AdvanceLightningShock(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR,
                                      WorldTime, Models[o->Owner->Type]);

            OBJECT *pObject = o;

            float fRot = (WorldTime * 0.0006f) * 360.0f;
            float fScale = 0.8f;

            // shiny
            Vector(1.0f, 0.4f, 0.4f, vLight);
            CreateSprite(BITMAP_SHINY + 1, o->Position, 4.0f * fScale, vLight, o, fRot);
            CreateSprite(BITMAP_SHINY + 1, o->Position, 3.0f * fScale, vLight, o, -fRot);
            // magic_ground
            Vector(1.0f, 0.2f, 0.2f, vLight);
            CreateSprite(BITMAP_MAGIC, o->Position, 1.0f * fScale, vLight, o, fRot);
            CreateSprite(BITMAP_MAGIC, o->Position, 0.5f * fScale, vLight, o, -fRot);
            // pin_light
            Vector(1.0f, 0.4f, 0.4f, vLight);
            CreateSprite(BITMAP_PIN_LIGHT, o->Position, 2.0f * fScale, vLight, o,
                         (float)(WorldRandom() % 360));
            CreateSprite(BITMAP_PIN_LIGHT, o->Position, 2.0f * fScale, vLight, o,
                         (float)(WorldRandom() % 360));

            vec3_t vPos, vLightFlare{1.f, 0.2f, 0.1f};
            for (int i = 0; i < 11; ++i)
            {
                Vector(o->Position[0] + WorldRandom() % 70 - 35,
                       o->Position[1] + WorldRandom() % 70 - 35,
                       o->Position[2] + WorldRandom() % 70 - 35, vPos);
                CreateSprite(BITMAP_LIGHT, vPos, 2.2f, vLightFlare, pObject);
            }
            EmitLightningShockAir(*o, landed.value_or(FPS_ANIMATION_FACTOR));

            if (landed)
            {
                auto birth =
                    sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - *landed);
                CreateEffect(MODEL_LIGHTNING_SHOCK, o->Position, o->Angle, o->Light, 1, o);
                EffectDestructor(o);
            }
        }
        else if (o->SubType == 1)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                OBJECT *pObject = o;

                float fScale = 0.8f;
                vec3_t vPos, vLightFlare;
                Vector(1.0f, 0.7f, 0.4f, vLight);
                Vector(1.0f, 0.2f, 0.1f, vLightFlare);
                for (int i = 0; i < 11; ++i)
                {
                    fScale = (float)(WorldRandom() % 80 + 32) * 0.01f * 1.0f;
                    Vector(o->Position[0] + (WorldRandom() % 70 - 35) * 1.0f,
                           o->Position[1] + (WorldRandom() % 70 - 35) * 1.0f,
                           o->Position[2] + (WorldRandom() % 70 - 35) * 1.0f, vPos);
                    CreateParticle(BITMAP_LIGHTNING_MEGA1 + WorldRandom() % 3, vPos, pObject->Angle,
                                   vLight, 0, fScale); // 전기
                }

                vec34_t Matrix;
                vec3_t vAngle, vDirection, vPosition;
                float fAngle;
                Vector(1.0f, 0.0f, 0.0f, vLight);

                for (int i = 0; i < 6; ++i)
                {
                    fScale = (float)(WorldRandom() % 60 + 22) * 0.01f * 1.0f;
                    Vector(0.f, WorldRandom() % 400, 0.f, vDirection);
                    fAngle = o->Angle[2] + WorldRandom() % 360;
                    Vector(0.f, 0.f, fAngle, vAngle);
                    AngleMatrix(vAngle, Matrix);
                    VectorRotate(vDirection, Matrix, vPosition);
                    VectorAdd(vPosition, o->Position, vPosition);
                    vPosition[2] = RequestTerrainHeight(vPosition[0], vPosition[1]) + 20;

                    CreateParticle(BITMAP_LIGHTNING_MEGA1 + WorldRandom() % 3, vPosition,
                                   pObject->Angle, vLight, 0, fScale); // 전기
                }

                VectorCopy(o->Position, vPosition);
                vPosition[2] = RequestTerrainHeight(vPosition[0], vPosition[1]) + 10;

                Vector(1.0f, 0.0f, 0.0f, vLight);

                for (int i = 0; i < 2; i++)
                    CreateParticle(BITMAP_SMOKE, vPosition, o->Angle, vLight, 58);

                if (WorldRandom() % 2 == 0)
                {
                    Vector(1.0f, 0.0f, 0.0f, vLight);
                    CreateParticle(BITMAP_SMOKE, vPosition, o->Angle, vLight, 54, 2.8f);
                }

                CreateEffect(MODEL_STONE1 + WorldRandom() % 2, vPosition, o->Angle, vLight, 13, o);
            }
        }
        else if (o->SubType == 2)
        {
            if (o->Owner != NULL && o->Owner->BoneTransform != NULL)
            {
                BMD *pTargetModel = &Models[o->Owner->Type];
                int iNumBones = pTargetModel->NumBones;
                float fRandom;
                vec3_t vLight, vRelativePos, vPos, vAngle{};
                Vector(0.0f, 0.0f, 0.0f, vRelativePos);
                for (int i = 0; i < iNumBones; i++)
                {
                    VectorCopy(o->Owner->Position, pTargetModel->BodyOrigin);
                    pTargetModel->TransformPosition(o->Owner->BoneTransform[i], vRelativePos, vPos,
                                                    true);

                    Vector(0.8f, 0.0f, 0.0f, vLight);
                    for (auto birthTime :
                         sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 60.0))
                    {
                        SampleOwnerBone(*o->Owner, *pTargetModel, i, WorldTime,
                                        birthTime.FrameFraction(), vPos);
                        fRandom = 5.0f + ((float)(WorldRandom() % 20 - 10) * 0.1f);
                        CreateParticle(BITMAP_LIGHT, vPos, vAngle, vLight, 5, fRandom);
                    }
                    Vector(1.0f, 0.8f, 0.4f, vLight);
                    for (auto birthTime :
                         sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 5.0))
                    {
                        SampleOwnerBone(*o->Owner, *pTargetModel, i, WorldTime,
                                        birthTime.FrameFraction(), vPos);
                        fRandom = (float)(WorldRandom() % 70 + 22) * 0.01f * 1.0f;
                        CreateParticle(BITMAP_LIGHTNING_MEGA1 + WorldRandom() % 3, vPos, vAngle,
                                       vLight, 0, fRandom);
                    }
                }

                Vector(1.0f, 0.0f, 0.0f, vLight);
                //for(i=0; i<2; i++)
                CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, vLight, 58);

                for (auto birthTime :
                     sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.0))
                {
                    Vector(1.0f, 0.0f, 0.0f, vLight);
                    CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, vLight, 54, 2.8f);
                }
            }
        }
    }
    return true;
}

// MODEL_SKILL_BLAST
bool MoveBehavior::Move_MODEL_SKILL_BLAST(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Position;
    float Height;
    Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
    if (o->Position[2] < Height)
    {
        o->Position[2] = Height;
        Vector(0.f, 0.f, 0.f, o->Direction);
        vec3_t Position;
        Vector(o->Position[0], o->Position[1], o->Position[2] + 80.f, Position);
        for (int j = 0; j < 6; j++)
        {
            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, o->Position, o->Angle, o->Light);
        }
        CreateParticle(BITMAP_SHINY + 4, Position, o->Angle, Light);
        CreateParticle(BITMAP_EXPLOTION, Position, o->Angle, Light);
        if (o->Owner == &Hero->Object)
            AttackCharacterRange(o->Skill, o->Position, 150.f, o->Weapon, o->PKKey);
        RetireEffect(o);
    }
    VectorCopy(o->Position, o->EyeLeft);
    //CreateSprite(BITMAP_SHINY+4,o->Position,2.5f,Light,o);
    Vector(Luminosity * 0.2f, Luminosity * 0.4f, Luminosity * 1.f, Light);
    AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
    return true;
}

// MODEL_WAVE
bool MoveBehavior::Move_MODEL_WAVE(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    if (o->Scale > 2.f)
    {
        o->Scale += (0.1f) * FPS_ANIMATION_FACTOR;
        o->Position[0] -= (1.f) * FPS_ANIMATION_FACTOR;
        o->Position[2] -= (1.5f) * FPS_ANIMATION_FACTOR;
        o->BlendMeshLight = o->LifeTime / 30.f;
    }
    else
    {
        o->Scale += (1.2f) * FPS_ANIMATION_FACTOR;
        o->Position[0] -= (1.2f) * FPS_ANIMATION_FACTOR;
        o->Position[2] -= (1.8f) * FPS_ANIMATION_FACTOR;
    }

    Vector(-Luminosity * 0.5f, -Luminosity * 0.5f, -Luminosity * 0.5f, Light);
    AddTerrainLight(o->Position[0], o->Position[1], Light, 5, PrimaryTerrainLight);
    return true;
}

// MODEL_TAIL
bool MoveBehavior::Move_MODEL_TAIL(OBJECT *o, int index, float Luminosity)
{
    AdvanceEffectGravity(*o, -1.f, 60.f, FPS_ANIMATION_FACTOR);
    o->BlendMeshLight = o->LifeTime / 20.f;
    return true;
}

// MODEL_WAVE_FORCE
bool MoveBehavior::Move_MODEL_WAVE_FORCE(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    o->BlendMeshLight = o->LifeTime / 20.f;
    Vector(-Luminosity * 0.5f, -Luminosity * 0.5f, -Luminosity * 0.5f, Light);
    AddTerrainLight(o->Position[0], o->Position[1], Light, 5, PrimaryTerrainLight);
    return true;
}

// MODEL_SKILL_INFERNO
bool MoveBehavior::Move_MODEL_SKILL_INFERNO(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    if (o->SubType == 2)
    {
        o->Scale += (0.04f) * FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 8)
    {
        o->Scale += (0.04f) * FPS_ANIMATION_FACTOR;
        if (o->LifeTime < 10)
        {
            o->Light[0] *= pow(0.8f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= pow(0.8f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= pow(0.8f, FPS_ANIMATION_FACTOR);
        }
    }
    else if (o->SubType == 3)
    {
        VectorCopy(o->Owner->Position, o->Position);
        o->Position[0] = o->Owner->Owner->Position[0] + 150.f;
    }
    else if (o->SubType == 4)
    {
        VectorCopy(o->Owner->Position, o->Position);

        constexpr float ScaleRate = 0.003f, GravityPerScale = 24.f;
        const float scaleTravel =
            FPS_ANIMATION_FACTOR * (o->Scale + ScaleRate * (FPS_ANIMATION_FACTOR + 1.f) * 0.5f);
        o->Scale += ScaleRate * FPS_ANIMATION_FACTOR;
        o->Gravity += GravityPerScale * scaleTravel;
        o->Position[2] += o->Gravity;
    }
    else if (o->SubType == 5)
    {
        o->Position[2] += (2.f) * FPS_ANIMATION_FACTOR;
        o->Angle[2] += (20.f) * FPS_ANIMATION_FACTOR;
        o->BlendMeshLight = o->LifeTime / 20.f;
        Vector(Luminosity * 0.1f, Luminosity * 0.3f, Luminosity * 0.8f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 5, PrimaryTerrainLight);
    }
    else if (o->SubType == 6)
    {
        o->Scale += (0.01f) * FPS_ANIMATION_FACTOR;
        o->BlendMeshLight = o->LifeTime / 5.f * 0.1f;
        Vector(Luminosity * 0.8f, Luminosity * 0.3f, Luminosity * 0.1f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
    }

    if (o->SubType < 4)
    {
        o->BlendMeshLight = o->LifeTime / 20.f;
        if (o->SubType != 2)
        {
            Vector(-Luminosity * 0.5f, -Luminosity * 0.5f, -Luminosity * 0.5f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 5, PrimaryTerrainLight);
        }
    }
    else if (o->SubType == 8)
    {
        o->BlendMeshLight = o->LifeTime / 20.f;
    }
    else if (o->SubType == 9)
    {
        o->BlendMeshLight = o->LifeTime / 80.f;
        o->Position[2] += (o->Gravity) * FPS_ANIMATION_FACTOR;
        o->Light[0] *= pow(1.0f / (1.04f), FPS_ANIMATION_FACTOR);
        o->Light[2] *= pow(1.0f / (1.04f), FPS_ANIMATION_FACTOR);
        o->Alpha -= (0.01f) * FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 10)
    {
        o->Scale += (0.04f) * FPS_ANIMATION_FACTOR;
        BMD *b = &Models[o->Type];
        VectorCopy(o->Light, b->BodyLight);
    }
    return true;
}

// MODEL_MAGIC_CIRCLE1
bool MoveBehavior::Move_MODEL_MAGIC_CIRCLE1(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    if (o->SubType == 2)
    {
        o->Scale += (0.015f) * FPS_ANIMATION_FACTOR;
        VectorCopy(o->Owner->Position, o->Position);
        Vector(0.1f, 0.0f, 0.0f, o->Light);
    }
    else if (o->SubType == 1)
    {
        o->Scale += (0.01f) * FPS_ANIMATION_FACTOR;
        Vector(Luminosity * 1.0f, Luminosity * 0.0f, Luminosity * 0.0f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
    }
    else
    {
        VectorCopy(o->Owner->Position, o->Position);
        o->Scale += (0.01f) * FPS_ANIMATION_FACTOR;
    }
    return true;
}

// MODEL_PROTECT
bool MoveBehavior::Move_MODEL_PROTECT(OBJECT *o, int index, float Luminosity)
{
    o->BlendMeshLight = 1.3f;

    VectorCopy(o->Owner->Position, o->Position);
    o->Angle[2] += (10.f) * FPS_ANIMATION_FACTOR;
    if (!o->Owner->Live || !o->Owner->Visible)
        RetireEffect(o);
    return true;
}

// MODEL_POISON
bool MoveBehavior::Move_MODEL_POISON(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    o->BlendMeshLight = o->LifeTime * 0.1f;
    o->Alpha = o->LifeTime * 0.1f;
    Vector(Luminosity * 0.3f, Luminosity * 1.f, Luminosity * 0.6f, Light);
    AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
    return true;
}

// MODEL_SAW
bool MoveBehavior::Move_MODEL_SAW(OBJECT *o, int index, float Luminosity)
{
    o->Angle[2] -= (30.f) * FPS_ANIMATION_FACTOR;
    return true;
}

// MODEL_LASER
bool MoveBehavior::Move_MODEL_LASER(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType != 0 && o->SubType != 3)
    {
        const float frames = FPS_ANIMATION_FACTOR;
        constexpr float AccelerationRate = 1.f;
        o->EffectAnimationAdvance =
            frames * (o->Velocity + AccelerationRate * (frames + 1.f) * 0.5f);
        vec3_t travel{o->Direction[0] * frames, 0.f, o->Direction[2] * frames};
        // Sum post-update direction while its acceleration also grows each authored tick.
        travel[1] = frames * (o->Direction[1] - o->Velocity * (frames + 1.f) * 0.5f -
                              AccelerationRate * (frames * frames - 1.f) / 6.f);
        o->Direction[1] -= frames * (o->Velocity + AccelerationRate * (frames - 1.f) * 0.5f);
        o->Velocity += AccelerationRate * frames;
        float matrix[3][4];
        vec3_t movement;
        AngleMatrix(o->Angle, matrix);
        VectorRotate(travel, matrix, movement);
        VectorAdd(o->Position, movement, o->Position);
    }
    return true;
}

// MODEL_SKILL_WHEEL1
bool MoveBehavior::Move_MODEL_SKILL_WHEEL1(OBJECT *o, int index, float Luminosity)
{
    for (float sample = Core::Time::ReferenceSample(o->LifeTime);
         sample >= 1.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, sample);
         sample -= 1.f)
    {
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR -
                                                             (std::max)(0.f, o->LifeTime - sample));
        CreateEffect(MODEL_SKILL_WHEEL2, o->Position, o->Angle, o->Light, 4 - int(sample), o->Owner,
                     o->PKKey, o->Skill, o->Kind);
    }
    return true;
}

// MODEL_SKILL_WHEEL2
bool MoveBehavior::Move_MODEL_SKILL_WHEEL2(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    float Matrix[3][4];
    float Height;
    vec3_t p;
    switch (o->SubType)
    {
    case 1:
        o->Alpha = 0.6f;
        break;
    case 2:
        o->Alpha = 0.5f;
        break;
    case 3:
        o->Alpha = 0.4f;
        break;
    case 4:
        o->Alpha = 0.3f;
        break;
    }
    if (o->Owner->Weapon >= MODEL_SPEAR - MODEL_SWORD &&
        o->Owner->Weapon < MODEL_SPEAR - MODEL_SWORD + MAX_ITEM_INDEX)
    {
        Vector(0.f, -180.f, 0.f, p);
    }
    else
    {
        Vector(0.f, -150.f, 0.f, p);
    }

    AngleMatrix(o->Angle, Matrix);
    VectorRotate(p, Matrix, Position);
    VectorAdd(o->Owner->Position, Position, o->Position);
    o->Angle[2] -= 18 * FPS_ANIMATION_FACTOR;

    Vector(Luminosity * 0.3f, Luminosity * 0.3f, Luminosity * 0.3f, Light);
    AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);

    for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.0))
    {
        CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 3);
    }

    Vector(1.f, 1.f, 1.f, Light);
    Height = 20.f;
    if (gMapManager.InHellas())
    {
        Height = 60.f;
        Vector((float)(WorldRandom() % 60 + 60 - 90), 0.f, (float)(WorldRandom() % 30 + 90), Angle);
        VectorAdd(Angle, o->Angle, Angle);
        VectorCopy(o->Position, Position);
        Position[0] += WorldRandom() % 20 - 10;
        Position[1] += WorldRandom() % 20 - 10;
        Position[2] += 120.f;
        Vector(0.5f, 0.5f, 0.5f, Light);
        CreateParticleFpsChecked(BITMAP_WATERFALL_5, Position, Angle, Light, 3);
    }
    else
    {
        for (int j = 0; j < 4; j++)
        {
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.0))
            {
                Vector((float)(WorldRandom() % 60 + 60 - 90), 0.f, (float)(WorldRandom() % 30 + 90),
                       Angle);
                VectorAdd(Angle, o->Angle, Angle);
                VectorCopy(o->Position, Position);
                Position[0] += WorldRandom() % 20 - 10;
                Position[1] += WorldRandom() % 20 - 10;
                CreateJoint(BITMAP_JOINT_SPARK, Position, Position, Angle);
                if (sessionKeeper_.Random()->FpsCheck(4, 1.0))
                    CreateParticle(BITMAP_SPARK, Position, Angle, Light);
            }
        }
    }
    VectorCopy(o->Position, Position);
    Position[2] += Height;
    Vector(1.f, 0.8f, 0.6f, Light);
    CreateSprite(BITMAP_LIGHT, Position, 2.f, Light, o);

    if (gMapManager.InHellas())
        EmitWaterWaves(*o, *o, 4.f, -150);
    return true;
}

void MoveBehavior::EmitWaterWaves(OBJECT &object, const OBJECT &source, float period, int height)
{
    const float active = std::min(FPS_ANIMATION_FACTOR, std::max(0.f, object.LifeTime));
    const int count = Core::Time::Periods(object.LifeTime, active, period);
    const float first = std::floor(Core::Time::ReferenceSample(object.LifeTime) / period) * period;
    for (int event = 0; event < count; ++event)
    {
        const float elapsed = std::max(0.f, object.LifeTime - (first - event * period));
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - elapsed);
        vec3_t position;
        source.MotionTrace.Sample(WorldTime, birth.FrameFraction(), source.Position, position);
        AddWaterWave(static_cast<int>(position[0] / TERRAIN_SCALE),
                     static_cast<int>(position[1] / TERRAIN_SCALE), 2, height);
    }
}

// MODEL_SKILL_FISSURE
bool MoveBehavior::Move_MODEL_SKILL_FISSURE(OBJECT *o, int index, float Luminosity)
{
    vec3_t Angle;
    vec3_t Position;
    {
        if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 8))
        {
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR -
                                                                 std::max(0.f, o->LifeTime - 8.f));
            for (int i = 0; i < 16; ++i)
            {
                Angle[0] = -10.f;
                Angle[1] = 0.f;
                Angle[2] = i * 10.f;
                vec3_t Position;
                VectorCopy(o->Position, Position);
                Position[2] += 100.f;

                //if ( (i%8)==0 )
                //CreateEffect(BITMAP_MAGIC+1,o->Position,Angle,o->Light,4,o);

                Position[0] += WorldRandom() % 600 - 200;
                Position[1] += WorldRandom() % 600 - 400;
                CreateJoint(BITMAP_FLARE, Position, Position, Angle, 24, NULL, 90);
            }
        }
        if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 2))
        {
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR -
                                                                 std::max(0.f, o->LifeTime - 2.f));
            Vector(0.f, 0.f, WorldRandom() % 360, Angle);
            CreateEffect(MODEL_FISSURE, o->Position, Angle, o->Light, 0, o);
            CreateEffect(MODEL_FISSURE_LIGHT, o->Position, Angle, o->Light, 0, o);
            RetireEffect(o);
        }
    }
    return true;
}

// MODEL_FISSURE
bool MoveBehavior::Move_MODEL_FISSURE(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    float Matrix[3][4];
    vec3_t p;
    {
        double fLevel = sinf(Q_PI * double(o->LifeTime) / 120.f) * -0.5f;
        vec3_t Light;
        Vector(fLevel, fLevel, fLevel + 0.1f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 10, PrimaryTerrainLight);

        EarthQuake = (float)(WorldRandom() % 4 - 2) * 0.1f;

        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
        {
            Vector(0.f, (float)(WorldRandom() % 150) + 300.f, 0.f, p);
            Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);
            AngleMatrix(Angle, Matrix);
            VectorRotate(p, Matrix, Position);
            VectorAdd(Position, o->Position, Position);

            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, Position, o->Angle, o->Light, 0);
        }
    }
    return true;
}

// MODEL_SKILL_FURY_STRIKE
bool MoveBehavior::Move_MODEL_SKILL_FURY_STRIKE(OBJECT *o, int index, float Luminosity)
{
    AdvanceFuryStrike(*o, FPS_ANIMATION_FACTOR);
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    float Matrix[3][4];
    vec3_t p;
    {
        vec3_t p, Position, Pos[5];
        vec3_t Angle;
        short scale = 150;
        float Matrix[3][4], ang[5];
        int TargetX, TargetY;

        Vector(0.f, 0.f, 0.f, Angle);
        Vector(0.f, 0.f, 0.f, p);

        if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 11))
        {
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR -
                                                                 std::max(0.f, o->LifeTime - 11.f));
            vec3_t light;
            Vector(1.f, 1.f, 1.f, light);
            Vector(0.f, 0.f, 0.f, Angle);

            if (o->Kind != 3)
            {
                Vector(-25.f, -80.f, 0.f, p);
                AngleMatrix(o->Owner->Angle, Matrix);
                VectorRotate(p, Matrix, Position);
                VectorAdd(Position, o->Position, o->StartPosition);
            }

            float AddHeight = 25.f;

            if (gMapManager.InHellas() == true)
            {
                if (o->Kind == 0 || o->Kind == 2)
                {
                    int PositionX = (int)(o->StartPosition[0] / TERRAIN_SCALE);
                    int PositionY = (int)(o->StartPosition[1] / TERRAIN_SCALE);
                    AddWaterWave(PositionX, PositionY, 2, -1000);
                }
                else
                {
                    AddHeight = 100.f;
                }
            }

            o->StartPosition[2] =
                RequestTerrainHeight(o->StartPosition[0], o->StartPosition[1]) + AddHeight;
            AddHeight = 3.f;

            CreateParticle(BITMAP_EXPLOTION, o->StartPosition, Angle, light, 0, 0.5f);

            if (o->Kind == 0)
            {
                for (int j = 0; j < 8; j++)
                {
                    {
                        Vector((float)(WorldRandom() % 60 - 60.f), 0.f,
                               (float)(WorldRandom() % 30 + 90), Angle);
                        VectorAdd(Angle, o->Angle, Angle);
                        VectorCopy(o->StartPosition, Position);
                        Position[0] += WorldRandom() % 20 - 10;
                        Position[1] += WorldRandom() % 20 - 10;
                        CreateJoint(BITMAP_JOINT_SPARK, Position, Position, Angle);
                        if (WorldRandom() % 8 == 0)
                            CreateParticle(BITMAP_SPARK, Position, Angle, Light);
                    }
                }
            }
            Vector(0.f, 0.f, 0.f, Angle);

            if (o->Kind == 0)
                CreateEffect(MODEL_WAVE, o->StartPosition, Angle, o->Light);

            o->StartPosition[2] -= 27; //* FPS_ANIMATION_FACTOR;

            if (o->Owner != NULL)
            {
                if (o->Owner->Type != MODEL_WEREWOLF_HERO)
                {
                    CreateEffect(MODEL_SKILL_FURY_STRIKE + 3, o->StartPosition, Angle, o->Light, 0,
                                 o, scale);
                    CreateEffect(MODEL_SKILL_FURY_STRIKE + 1, o->StartPosition, Angle, o->Light, 0,
                                 o, scale);
                    CreateEffect(MODEL_SKILL_FURY_STRIKE + 2, o->StartPosition, Angle, o->Light, 0,
                                 o, scale);
                }
            }

            for (int i = 0; i < 5; ++i)
            {
                Vector(0.f, (float)(WorldRandom() % 150 + 100), 0.f, p);
                Vector(0.f, 0.f, (float)(o->SubType + (i * 72.f)), Angle);
                AngleMatrix(Angle, Matrix);
                VectorRotate(p, Matrix, Position);
                VectorAdd(Position, o->StartPosition, Position);

                Position[2] += 3;
                scale = WorldRandom() % 50 + 40;

                Vector(0.f, 0.f, 45 + (float)(WorldRandom() % 30 - 15), Angle);

                TargetX = (int)(Position[0] / TERRAIN_SCALE);
                TargetY = (int)(Position[1] / TERRAIN_SCALE);

                WORD wall = TerrainWall[TERRAIN_INDEX(TargetX, TargetY)];

                if ((wall & TW_NOMOVE) != TW_NOMOVE && (wall & TW_NOGROUND) != TW_NOGROUND &&
                    (wall & TW_WATER) != TW_WATER)
                {
                    if (gMapManager.InHellas() == true)
                    {
                        AddHeight = 100.f;
                    }
                    Position[2] = RequestTerrainHeight(Position[0], Position[1]) + AddHeight;

                    CreateEffect(MODEL_SKILL_FURY_STRIKE + 4, Position, Angle, o->Light, 0,
                                 o->Owner, scale);
                    CreateEffect(MODEL_SKILL_FURY_STRIKE + 5, Position, Angle, o->Light, 0,
                                 o->Owner, scale);
                }
            }
        }
        else if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 10))
        {
            for (int j = 0; j < 5; ++j)
            {
                VectorCopy(o->StartPosition, Pos[j]);
                ang[j] = 0.f;
            }

            int count = 0;
            int random;

            for (int j = 0; j < 4; ++j)
            {
                Vector(0.f, WorldRandom() % 15 + 85.f, 0.f, p);

                if (j >= 3)
                    count = WorldRandom();

                for (int i = 0; i < 5; ++i)
                {
                    if ((count % 2) == 0)
                        random = WorldRandom() % 30 + 50;
                    else
                        random = -(WorldRandom() % 30 + 50);

                    ang[i] += random;
                    Angle[2] = ang[i] + (i * (WorldRandom() % 10 + 62));

                    AngleMatrix(Angle, Matrix);
                    VectorRotate(p, Matrix, Position);
                    VectorAdd(Position, Pos[i], Pos[i]);

                    TargetX = (int)(Pos[i][0] / TERRAIN_SCALE);
                    TargetY = (int)(Pos[i][1] / TERRAIN_SCALE);

                    WORD wall = TerrainWall[TERRAIN_INDEX(TargetX, TargetY)];

                    if ((wall & TW_NOMOVE) != TW_NOMOVE || (wall & TW_NOGROUND) != TW_NOGROUND ||
                        (wall & TW_WATER) != TW_WATER)
                    {
                        if (gMapManager.InHellas() == true)
                            Pos[i][2] = RequestTerrainHeight(Pos[i][0], Pos[i][1]) + 100.f;
                        else
                            Pos[i][2] = RequestTerrainHeight(Pos[i][0], Pos[i][1]) + 3;
                        Angle[2] += 270.f;

                        CreateEffect(MODEL_SKILL_FURY_STRIKE + 7, Pos[i], Angle, o->Light, 0,
                                     o->Owner, 100);
                        CreateEffect(MODEL_SKILL_FURY_STRIKE + 8, Pos[i], Angle, o->Light, 0,
                                     o->Owner, 100);
                    }
                }

                count++;
            }
            o->LifeTime = 0;

            PlayBuffer(SOUND_FURY_STRIKE3);
        }
        else
        {
            if (o->Kind == 0)
            {
                if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 13))
                {
                    vec3_t pos;
                    Vector(0.f, 0.f, 0.f, Angle);
                    Vector(-25.f, -40.f, 0.f, p);
                    AngleMatrix(o->Owner->Angle, Matrix);
                    VectorRotate(p, Matrix, Position);
                    VectorAdd(Position, o->Position, pos);

                    vec3_t position;
                    //                            pos[2] -= 50.f;
                    for (int i = 0; i < 4; ++i)
                    {
                        position[0] = pos[0];
                        position[1] = pos[1];
                        position[2] = pos[2] - (i * 50);
                        CreateEffect(MODEL_TAIL, position, Angle, o->Light);
                    }
                    pos[0] += WorldRandom() % 30 + 20;
                    pos[2] += WorldRandom() % 500 - 250;

                    for (int i = 0; i < 4; ++i)
                    {
                        position[0] = pos[0];
                        position[1] = pos[1];
                        position[2] = pos[2] - (i * 30);
                        CreateEffect(MODEL_TAIL, position, Angle, o->Light);
                    }
                    PlayBuffer(SOUND_FURY_STRIKE2);
                }
            }
        }
    }
    return true;
}

// MODEL_BALGAS_SKILL
bool MoveBehavior::Move_MODEL_BALGAS_SKILL(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    o->BlendMeshLight = o->LifeTime * 0.1f;
    //        o->BlendMeshTexCoordU = -(float)o->LifeTime*0.2f;
    Vector(Luminosity * 0.3f, Luminosity * 0.6f, Luminosity, Light);
    {
        BMD *b = &Models[o->Type];
        b->CurrentAction = o->CurrentAction;
        b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction,
                         o->Velocity * 2.0f, o->Position, o->Angle);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
    }
    return true;
}

// MODEL_CHANGE_UP_EFF
bool MoveBehavior::Move_MODEL_CHANGE_UP_EFF(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    {
        vec3_t Loc;
        int STwo = 2;
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / STwo))
        {
            //               Vector(0.f,0.f,(float)(WorldRandom()%360),o->Angle);

            if (o->SubType == 2)
            {
                VectorCopy(o->Owner->Position, o->Position);
                o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 22.0f;
            }

            Loc[0] = o->Position[0] + WorldRandom() % 200 - 100;
            Loc[1] = o->Position[1] + WorldRandom() % 200 - 100;
            Loc[2] = o->Position[2] - 200;
            if (o->SubType == 0 || o->SubType == 2)
            {
                CreateJoint(BITMAP_FLARE, Loc, Loc, o->Angle, 50, NULL, 40);
            }
            //				else
            //				if(o->SubType == 1)
            //					CreateJoint(BITMAP_FLARE,Loc,Loc,o->Angle,25,o,50.f);
            //CreateJoint(BITMAP_FLARE,Loc,Loc,o->Angle,51,NULL,30);

            if (o->LifeTime > 40)
                CreateEffect(BITMAP_MAGIC, o->Position, o->Angle, Light, 4, o, 4.0f);
        }
        if (o->LifeTime <= 1 && o->SubType == 0)
            SetPlayerStop(Hero);

        if (o->SubType == 0)
        {
            BMD *b = &Models[o->Type];
            b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction,
                             o->Velocity / 3.f, o->Position, o->Angle);
        }
        else if (o->SubType == 2)
        {
            BMD *b = &Models[o->Type];
            b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction,
                             o->Velocity / 3.f, o->Position, o->Angle);
        }
    }
    return true;
}

// MODEL_CHANGE_UP_NASA
bool MoveBehavior::Move_MODEL_CHANGE_UP_NASA(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->SubType == 0)
        {
            if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 100))
            {
                CreateEffect(MODEL_CHANGE_UP_NASA, o->Position, o->Angle, o->Light, 1, o);
            }
            else if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 70))
            {
                CreateEffect(MODEL_CHANGE_UP_NASA, o->Position, o->Angle, o->Light, 2, o);
            }
            if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 40))
            {
                CreateEffect(MODEL_CHANGE_UP_NASA, o->Position, o->Angle, o->Light, 3, o);
            }
        }
        if (o->SubType >= 1 && o->SubType <= 3)
        {
            o->Scale += (0.02f) * FPS_ANIMATION_FACTOR;
            o->BlendMeshLight = o->LifeTime * 0.005f;
        }
        else if (o->SubType == 0)
            o->BlendMeshLight = o->LifeTime * 0.01f;
    }
    return true;
}

// MODEL_CHANGE_UP_CYLINDER
bool MoveBehavior::Move_MODEL_CHANGE_UP_CYLINDER(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    float Matrix[3][4];
    {
        if (o->SubType == 2)
        {
            VectorCopy(o->Owner->Position, o->Position);
            o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);
        }

        vec3_t Go;
        AngleMatrix(Angle, Matrix);
        VectorRotate(Go, Matrix, Position);
        VectorAdd(Position, o->Position, Position);

        if (o->SubType == 1)
        {
            o->BlendMeshLight = o->LifeTime * 0.015f;
            o->Scale += (0.08f) * FPS_ANIMATION_FACTOR;
        }
        //			Vector(Luminosity*0.3f,Luminosity*0.6f,Luminosity,Light);
        //			AddTerrainLight(Position[0],Position[1],Light,3,PrimaryTerrainLight);
    }
    return true;
}

// MODEL_DARK_ELF_SKILL
bool MoveBehavior::Move_MODEL_DARK_ELF_SKILL(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    float Matrix[3][4];
    {
        vec3_t Go;
        AdvanceDarkElfPulse(*o, FPS_ANIMATION_FACTOR);
        o->BlendMeshLight = o->LifeTime * 0.1f;
        o->BlendMeshTexCoordU = -(float)o->LifeTime * 0.2f;
        Vector(0.0f, -500.f, 0.0f, Go);
        vec3_t Angle;
        VectorCopy(o->Angle, Angle);
        Angle[2] += 7;

        AngleMatrix(Angle, Matrix);
        VectorRotate(Go, Matrix, Position);
        VectorAdd(Position, o->Position, Position);

        for (int j = 0; j < 6; j++)
        {
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.0))
                CreateParticle(BITMAP_SMOKE, Position, Angle, o->Light, 25);
        }
        Vector(Luminosity * 0.3f, Luminosity * 0.6f, Luminosity, Light);
        AddTerrainLight(Position[0], Position[1], Light, 3, PrimaryTerrainLight);
    }
    return true;
}

// MODEL_MAGIC2
bool MoveBehavior::Move_MODEL_MAGIC2(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    o->BlendMeshLight = o->LifeTime * 0.1f;
    o->BlendMeshTexCoordU = -(float)o->LifeTime * 0.2f;
    //VectorAdd(o->Position,o->Direction,o->Position);

    for (int j = 0; j < 4; j++)
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.0))
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 3);

    Vector(Luminosity * 0.3f, Luminosity * 0.6f, Luminosity, Light);
    AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);

    if (o->SubType == 2)
    {
        o->HiddenMesh = 0;
        vec3_t Light2;
        VectorCopy(o->Angle, Angle);
        VectorCopy(o->Position, Position);
        Angle[2] += WorldRandom() % 10 - 5;
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.0))
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 11,
                           (float)(WorldRandom() % 32 + 80) * 0.015f);

        Vector(1.f, 1.f, 1.f, Light2);
        Position[2] += 50.f;
        CreateSprite(BITMAP_SHINY + 1, Position, 1.5f, Light2, NULL, (float)(WorldRandom() % 360));
        CreateSprite(BITMAP_SHINY + 1, Position, 1.5f, Light2, NULL, (float)(WorldRandom() % 360));

        CreateSprite(BITMAP_LIGHT, Position, 3.5f, Light, NULL, (float)(WorldRandom() % 360));
    }
    return true;
}

// MODEL_STORM
bool MoveBehavior::Move_MODEL_STORM(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    switch (o->SubType)
    {
    case 0:
        o->BlendMeshLight = o->LifeTime * 0.1f;
        o->BlendMeshTexCoordU = -(float)o->LifeTime * 0.1f;
        //VectorAdd(o->Position,o->Direction,o->Position);
        CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 3);
        Vector(90.f, 0.f, o->Angle[2], Angle);
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
        {
            Vector(o->Position[0] - 200.f, o->Position[1], o->Position[2] + 700.f, Position);
            CreateJoint(BITMAP_JOINT_THUNDER, Position, o->Position, Angle, 0, o, 10.f);
        }
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
        {
            Vector(o->Position[0] + 200.f, o->Position[1], o->Position[2] + 700.f, Position);
            CreateJoint(BITMAP_JOINT_THUNDER, Position, o->Position, Angle, 0, o, 10.f);
        }
        o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.0))
            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, o->Position, o->Angle, o->Light, 2);
        Vector(-Luminosity * 0.4f, -Luminosity * 0.3f, -Luminosity * 0.2f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 5, PrimaryTerrainLight);

        if (IsBattleCastleStart())
        {
            DWORD att = TERRAIN_ATTRIBUTE(o->Position[0], o->Position[1]);
            if ((att & TW_NOATTACKZONE) == TW_NOATTACKZONE)
            {
                o->Velocity = 0.f;
                Vector(0.f, 0.f, 0.f, o->Direction);
                o->LifeTime *= pow(1.0f / (5.f), FPS_ANIMATION_FACTOR);
                break;
            }
        }
        if (o->Owner == &Hero->Object)
        {
            constexpr float AttackInterval = 15.f;
            for (float life =
                     Core::Time::ReferenceSample(o->LifeTime / AttackInterval) * AttackInterval;
                 life > 0.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life);
                 life -= AttackInterval)
                AttackCharacterRange(o->Skill, o->Position, 150.f, o->Weapon, o->PKKey);
        }
        break;

    case 1:
        o->BlendMeshLight = o->LifeTime * 0.01f;
        o->BlendMeshTexCoordU = -(float)o->LifeTime * 0.1f;
        AdvanceRandomRotatingEffect(*o, *sessionKeeper_.Random(), FPS_ANIMATION_FACTOR, 2, 30,
                                    30.f);

        VectorCopy(o->Position, Position);
        Position[2] += 100.f;
        CreateParticleFpsChecked(BITMAP_SMOKE, Position, o->Angle, o->Light, 3);
        CreateParticleFpsChecked(BITMAP_BUBBLE, Position, o->Angle, o->Light, 3, 0.1f);

        Vector(-Luminosity * 0.1f, -Luminosity * 0.3f, -Luminosity * 1.f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], o->Light, 5, PrimaryTerrainLight);
        break;

    case 2:
        o->BlendMeshLight = o->LifeTime * 0.1f;
        o->BlendMeshTexCoordU = -(float)o->LifeTime * 0.1f;

        o->Gravity = (float)(WorldRandom() % 360);

        VectorCopy(o->Position, Position);
        Position[2] += 100.f;
        CreateParticleFpsChecked(BITMAP_SMOKE, Position, o->Angle, o->Light, 3);
        CreateParticleFpsChecked(BITMAP_BUBBLE, Position, o->Angle, o->Light, 3, 0.1f);

        Vector(-Luminosity * 0.1f, -Luminosity * 0.3f, -Luminosity * 1.f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], o->Light, 5, PrimaryTerrainLight);
        break;
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
        EarthQuake = (float)(WorldRandom() % 8 - 4) * 0.1f;
        CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 28);
        CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 29);
        CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 30);
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, o->Position, o->Angle, o->Light, 2);
        break;
    case 8: {
        o->BlendMeshLight = o->LifeTime * 0.1f;
        o->BlendMeshTexCoordU = -(float)o->LifeTime * 0.1f;

        Vector(-Luminosity * 0.1f, -Luminosity * 0.3f, -Luminosity * 1.f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], o->Light, 5, PrimaryTerrainLight);
    }
    break;
    }
    return true;
}

// MODEL_SUMMON
bool MoveBehavior::Move_MODEL_SUMMON(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->LifeTime >= 30)
        {
            o->Light[0] *= pow(1.03f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= pow(1.03f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= pow(1.03f, FPS_ANIMATION_FACTOR);
        }
        else
        {
            o->Light[0] *= pow(1.0f / (1.08f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= pow(1.0f / (1.08f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= pow(1.0f / (1.08f), FPS_ANIMATION_FACTOR);
        }
        o->Angle[2] += (1.0f) * FPS_ANIMATION_FACTOR;
    }
    return true;
}

// MODEL_STORM2
bool MoveBehavior::Move_MODEL_STORM2(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    float Matrix[3][4];
    {
        if (o->SubType == 0)
        {
            o->Angle[1] += (40.0f) * FPS_ANIMATION_FACTOR;

            if (o->LifeTime <= 5)
            {
                o->Light[0] *= pow(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= pow(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= pow(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
            }
        }
        else if (o->SubType == 1)
        {
            vec3_t Pos1, Pos2, Direction;

            float Matrix[3][4];

            for (int i = 0; i < 2; i++)
            {
                Angle[0] = o->Angle[0];
                Angle[1] = o->Angle[1];
                Angle[2] = o->Angle[2] / 3.0f + 180.0f * i;

                Vector(0.0f, 200.0f, 0.0f, Direction);
                AngleMatrix(Angle, Matrix);
                VectorRotate(Direction, Matrix, Position);
                VectorAdd(Position, o->Position, Position);

                Vector(0.3f, 0.5f, 0.6f, Light);
                CreateParticleFpsChecked(BITMAP_CLUD64, Position, o->Angle, Light, 1, 1.0f);
            }

            Vector(90.f, 0.f, 0.0f, Angle);

            Pos1[0] = o->Position[0] + WorldRandom() % 600 - 300.0f;
            Pos1[1] = o->Position[1] + WorldRandom() % 600 - 300.0f;
            Pos1[2] = o->Position[2];

            Pos2[0] = Pos1[0] + WorldRandom() % 100 - 50.0f;
            Pos2[1] = Pos1[1] + WorldRandom() % 100 - 50.0f;
            Pos2[2] = Pos1[2];

            CreateJointFpsChecked(BITMAP_JOINT_THUNDER, Pos1, Pos2, Angle, 20, o, 20.f);

            Pos2[0] = Pos1[0] + WorldRandom() % 100 - 50.0f;
            Pos2[1] = Pos1[1] + WorldRandom() % 100 - 50.0f;
            Pos2[2] = Pos1[2];
            CreateEffectFpsChecked(MODEL_STONE1 + WorldRandom() % 2, Pos2, o->Angle, Light, 2);

            o->Angle[2] += (o->Gravity) * FPS_ANIMATION_FACTOR;
            o->Light[0] *= pow(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= pow(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= pow(1.0f / (1.02f), FPS_ANIMATION_FACTOR);

            o->Alpha += (0.01f) * FPS_ANIMATION_FACTOR;

            if (o->Light[0] <= 0.05f)
                RetireEffect(o);
        }
        else if (o->SubType == 2)
        {
            o->Angle[1] += (40.0f) * FPS_ANIMATION_FACTOR;

            if (o->LifeTime <= 5)
            {
                o->Light[0] *= pow(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= pow(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= pow(1.0f / (1.01f), FPS_ANIMATION_FACTOR);
            }

            CreateEffectFpsChecked(MODEL_STONE1 + WorldRandom() % 2, o->Position, o->Angle,
                                   o->Light, 2);
        }
    }
    return true;
}

// MODEL_STORM3
bool MoveBehavior::Move_MODEL_STORM3(OBJECT *o, int index, float Luminosity)
{
    vec3_t Angle;
    {
        if (o->LifeTime <= 30)
        {
            o->Light[0] *= pow(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= pow(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= pow(1.0f / (1.1f), FPS_ANIMATION_FACTOR);
        }
        o->Angle[1] += (30.0f) * FPS_ANIMATION_FACTOR;

        Vector(90.f, 0.f, o->Angle[2], Angle);

        for (int i = 0; i < 5; i++)
        {
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
            {
                vec3_t Pos1, Pos2;

                Pos1[0] = o->StartPosition[0] + WorldRandom() % 2000 - 1000.0f;
                Pos1[1] = o->StartPosition[1] + WorldRandom() % 2000 - 1000.0f;
                Pos1[2] = o->StartPosition[2];

                Pos2[0] = Pos1[1] + WorldRandom() % 200 - 100.0f;
                Pos2[1] = Pos1[1] + WorldRandom() % 200 - 100.0f;

                CreateJoint(BITMAP_JOINT_THUNDER, Pos1, Pos2, Angle, 20, o, 20.f);
            }
        }
    }
    return true;
}

// MODEL_MAYASTONE1, MODEL_MAYASTONE2, MODEL_MAYASTONE3
bool MoveBehavior::Move_MODEL_MAYASTONE1(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    float Height;
    {
        bool success = false;

        VectorCopy(o->HeadAngle, o->Angle);

        float Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
        if (o->Position[2] < Height)
            success = true;

        if (success)
        {
            o->Position[2] = Height;
            Vector(0.f, 0.f, 0.f, o->Direction);
            vec3_t Position;
            Vector(o->Position[0], o->Position[1], o->Position[2] + 80.f, Position);

            BYTE smokeNum = 15;

            Vector(0.0f, 0.6f, 1.f, Light);
            Vector(0.f, 0.f, 0.f, Angle);
            CreateEffect(MODEL_SKILL_INFERNO, o->Position, Angle, Light, 2, o, 30, 0);

            for (int i = 0; i < smokeNum; ++i)
            {
                Position[0] = o->Position[0] + (WorldRandom() % 160 - 80);
                Position[1] = o->Position[1] + (WorldRandom() % 160 - 100);
                Position[2] = o->Position[2] + 50;

                Vector(0.3f, 0.5f, 1.0f, Light);
                CreateParticle(BITMAP_SMOKE, Position, o->Angle, Light, 11,
                               (float)(WorldRandom() % 32 + 80) * 0.025f);
            }

            for (int j = 0; j < 6; j++)
                CreateEffect(MODEL_MAYASTONE4 + WorldRandom() % 2, o->Position, o->Angle, Light);

            CreateParticle(BITMAP_EXPLOTION, Position, o->Angle, Light, 0, 4.0f);

            RetireEffect(o);
        }
    }
    return true;
}

// MODEL_MAYASTONE4, MODEL_MAYASTONE5
bool MoveBehavior::Move_MODEL_MAYASTONE4(OBJECT *o, int index, float Luminosity)
{
    float Height;
    {
        AdvanceBouncingDebris(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR,
                              DebrisMotion::MayaStone);

        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.0))
            CreateParticle(BITMAP_FIRE, o->Position, o->Angle, o->Light, 15);
    }
    return true;
}

// MODEL_MAYASTONEFIRE
bool MoveBehavior::Move_MODEL_MAYASTONEFIRE(OBJECT *o, int index, float Luminosity)
{
    float Height;
    {
        VectorCopy(o->HeadAngle, o->Angle);

        float Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
        if (o->Position[2] < Height)
            RetireEffect(o);
    }
    return true;
}

// MODEL_MAYAHANDSKILL
bool MoveBehavior::Move_MODEL_MAYAHANDSKILL(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType != 0 && o->SubType != 1)
        return true;
    VectorScale(o->Light, std::pow(1.f / 1.17f, FPS_ANIMATION_FACTOR), o->Light);
    if (o->SubType == 1)
    {
        vec3_t position;
        VectorCopy(o->Position, position);
        position[2] += 100.f;
        CreateParticleFpsChecked(BITMAP_SMOKE, position, o->Angle, o->StartPosition, 17, 10.f);
        return true;
    }

    float remaining = FPS_ANIMATION_FACTOR;
    while (remaining > 0.f)
    {
        if (o->EffectMotionFrames <= 0.f)
        {
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(remaining);
            vec3_t position, direction{0.f, (-80.f - WorldRandom() % 30) * o->Scale, 0.f};
            float matrix[3][4];
            AngleMatrix(o->Angle, matrix);
            VectorRotate(direction, matrix, o->EffectMotionVelocity);
            o->EffectMotionFrames = 1.f;
            VectorCopy(o->Position, position);
            position[2] += 100.f;
            CreateParticle(BITMAP_SMOKE, position, o->Angle, o->StartPosition, 17, 10.f);
        }
        const float step = Core::Time::ReferenceStep(remaining, o->EffectMotionFrames);
        VectorAddScaled(o->Position, o->EffectMotionVelocity, o->Position, step);
        o->MotionTrace.Advance(step, o->Position);
        remaining -= step;
        o->EffectMotionFrames -= step;
        if (o->EffectMotionFrames <= 0.f)
        {
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(remaining);
            CreateEffect(MODEL_MAYAHANDSKILL, o->Position, o->Angle, o->StartPosition, 1, nullptr,
                         -1, 0, 0, 0, o->Scale + 0.2f);
        }
    }
    if (o->Light[0] <= 0.05f)
        RetireEffect(o);
    return true;
}

// MODEL_CIRCLE
bool MoveBehavior::Move_MODEL_CIRCLE(OBJECT *o, int index, float Luminosity)
{
    vec3_t Angle;
    vec3_t Position;
    o->BlendMeshLight = o->LifeTime * 0.1f;
    if (o->SubType == 1)
    {
        const float stop = 44.f - o->Owner->m_bySkillCount;
        const float active = (std::min)(FPS_ANIMATION_FACTOR, (std::max)(0.f, o->LifeTime - stop));
        for (float sample = Core::Time::ReferenceSample(o->LifeTime);
             sample > stop && Core::Time::Reaches(o->LifeTime, active, sample); sample -= 1.f)
        {
            const float offset = (std::max)(0.f, o->LifeTime - sample);
            auto birthTime = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - offset);
            vec3_t position;
            VectorCopy(o->Position, position);
            position[2] += 100.f;
            for (int i = 0; i < 36; ++i)
            {
                vec3_t angle{-10.f, 0.f, float(i * (10 + WorldRandom() % 10))};
                CreateJoint(BITMAP_JOINT_SPIRIT, position, position, angle, 6, o, 60.f, 0, 0);
                if (sample == stop + 1.f)
                {
                    angle[2] = i * 10.f;
                    CreateJoint(BITMAP_JOINT_SPIRIT, position, position, angle, 7, o, 60.f, 0, 0);
                }
            }
        }
    }
    else if (o->SubType == 2)
    {
        if (o->LifeTime > 240)
        {
            o->BlendMeshLight = (250 - o->LifeTime) * 0.1f;
        }
        else
        {
            o->BlendMeshLight = o->LifeTime * 0.1f;
        }
    }
    else if (o->SubType == 3)
    {
        if (o->LifeTime > 10)
        {
            o->BlendMeshLight = (30 - o->LifeTime) * 0.1f;
        }
        else
        {
            o->BlendMeshLight = o->LifeTime * 0.1f;
        }
    }
    else if (o->SubType == 4)
    {
        const float stop = 44.f - o->Owner->m_bySkillCount;
        const float active = (std::min)(FPS_ANIMATION_FACTOR, (std::max)(0.f, o->LifeTime - stop));
        for (float sample = Core::Time::ReferenceSample(o->LifeTime);
             sample > stop && Core::Time::Reaches(o->LifeTime, active, sample); sample -= 1.f)
        {
            const float offset = (std::max)(0.f, o->LifeTime - sample);
            auto birthTime = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - offset);
            vec3_t Angle, Position;
            for (int i = 0; i < 36; ++i)
            {
                Angle[0] = -10.f;
                Angle[1] = 0.f;
                Angle[2] = i * 10.f;

                VectorCopy(o->Position, Position);
                Position[2] += 100.f;
                CreateJoint(BITMAP_JOINT_SPIRIT, Position, Position, Angle, 22, o, 2.0f, 0, 0);
                CreateJoint(BITMAP_JOINT_SPIRIT, Position, Position, Angle, 23, o, 1.75f, 0, 0);
            }
        }
    }
    return true;
}

// MODEL_CIRCLE_LIGHT
bool MoveBehavior::Move_MODEL_CIRCLE_LIGHT(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    float Matrix[3][4];
    vec3_t p;
    if (o->SubType != 3 && o->SubType != 4)
    {
        int value = 4;

        if (o->LifeTime >= 30)
            o->BlendMeshLight = (40 - o->LifeTime) * 0.1f;
        else
            o->BlendMeshLight = o->LifeTime * 0.1f;
        o->BlendMeshTexCoordU = -(float)o->LifeTime * 0.01f;
        EarthQuake = (float)(WorldRandom() % 6 - 6) * 0.1f;

        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / value))
        {
            vec3_t p, Position;
            vec3_t Angle;
            float Matrix[3][4];
            Vector(0.f, (float)(WorldRandom() % 300), 0.f, p);
            Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);
            AngleMatrix(Angle, Matrix);
            VectorRotate(p, Matrix, Position);
            VectorAdd(Position, o->Position, Position);
            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, Position, o->Angle, o->Light, 1);
        }
        Vector(Luminosity * 1.f, Luminosity * 0.8f, Luminosity * 0.2f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 4, PrimaryTerrainLight);
    }
    else if (o->SubType == 3)
    {
        int value = 2;

        if (o->LifeTime >= 240)
            o->BlendMeshLight = (250 - o->LifeTime) * 0.1f;
        else
            o->BlendMeshLight = o->LifeTime * 0.1f;

        o->BlendMeshLight = std::min<float>(0.5f, o->BlendMeshLight);
        o->BlendMeshTexCoordU = -(float)o->LifeTime * 0.01f;
        const float active = (std::min)(FPS_ANIMATION_FACTOR, (std::max)(0.f, o->LifeTime - 30.f));
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(active / value, active))
        {
            vec3_t p, Position;
            vec3_t Angle;
            float Matrix[3][4];
            Vector(0.f, (float)(WorldRandom() % 200), 0.f, p);
            Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);
            AngleMatrix(Angle, Matrix);
            VectorRotate(p, Matrix, Position);
            VectorAdd(Position, o->Position, Position);

            CreateParticle(BITMAP_FLARE_BLUE, Position, o->Angle, o->Light, 0);
            if (o->LifeTime - (FPS_ANIMATION_FACTOR - birthTime.RemainingFrames()) > 40.f)
            {
                Position[2] += 600.f;
                Angle[2] = 45.f;
                CreateJoint(BITMAP_FLARE_BLUE, Position, Position, Angle, 19, NULL, 40);
            }
        }
        Vector(o->BlendMeshLight, o->BlendMeshLight, o->BlendMeshLight, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 4, PrimaryTerrainLight);
    }
    else if (o->SubType == 4)
    {
        int value = 5;

        if (o->LifeTime >= 10)
            o->BlendMeshLight = (20 - o->LifeTime) * 0.1f;
        else
            o->BlendMeshLight = o->LifeTime * 0.1f;

        o->BlendMeshLight = std::min<float>(0.5f, o->BlendMeshLight);
        o->BlendMeshTexCoordU = -(float)o->LifeTime * 0.01f;
        const float active = (std::min)(FPS_ANIMATION_FACTOR, (std::max)(0.f, o->LifeTime - 5.f));
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(active / value, active))
        {
            vec3_t p, Position;
            vec3_t Angle;
            float Matrix[3][4];
            Vector(0.f, (float)(WorldRandom() % 100), 0.f, p);
            Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);
            AngleMatrix(Angle, Matrix);
            VectorRotate(p, Matrix, Position);
            VectorAdd(Position, o->Position, Position);

            CreateParticle(BITMAP_FLARE_BLUE, Position, o->Angle, o->Light, 0);
            Position[2] += 600.f;
            Angle[2] = 45.f;
            CreateJoint(BITMAP_FLARE_BLUE, Position, Position, Angle, 19, NULL, 40);
        }
        Vector(o->BlendMeshLight, o->BlendMeshLight, o->BlendMeshLight, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 4, PrimaryTerrainLight);
    }
    return true;
}

// MODEL_ICE_SMALL, MODEL_METEO1, MODEL_METEO2, MODEL_BOSS_ATTACK, MODEL_EFFECT_SAPITRES_ATTACK_2
bool MoveBehavior::Move_MODEL_ICE_SMALL(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType == 0 || o->SubType == 10 || o->SubType == 12 || o->SubType == 13)
    {
        GameLogic::Effects::AdvanceBouncingDebris(*o, *sessionKeeper_.WorldUnit(),
                                                  FPS_ANIMATION_FACTOR,
                                                  GameLogic::Effects::DebrisMotion::IceStone);
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(
                 o->SubType == 0 || o->SubType == 12 || o->SubType == 13
                     ? FPS_ANIMATION_FACTOR / 10.f
                     : 0.f))
        {
            vec3_t position;
            o->MotionTrace.Sample(WorldTime, birthTime.FrameFraction(), o->Position, position);
            if (o->Type == MODEL_ICE_SMALL || o->Type == MODEL_METEO1 || o->Type == MODEL_METEO2)
                CreateParticle(BITMAP_SMOKE, position, o->Angle, o->Light);
            else if (o->Type == MODEL_STONE1 || o->Type == MODEL_STONE2)
                CreateParticle(BITMAP_FIRE, position, o->Angle, o->Light, 1 + WorldRandom() % 3);
        }
        return true;
    }
    AdvanceFlyingIce(*o, FPS_ANIMATION_FACTOR, *sessionKeeper_.Random());
    if (o->Type == MODEL_EFFECT_SAPITRES_ATTACK_2 && o->SubType == 14)
    {
        const float fading =
            (std::max)(0.f, FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 10.f));
        o->BlendMeshLight -= 0.1f * fading;
    }
    return true;
}

// MODEL_SKULL
bool MoveBehavior::Move_MODEL_SKULL(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    if (o->SubType == 1 && o->Owner != NULL)
    {
        if (Core::Time::Periods(o->LifeTime, FPS_ANIMATION_FACTOR, 10.f) > 0)
        {
            VectorCopy(o->Position, o->m_vDeadPosition);
        }

        AdvanceSkullFlight(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR,
                           *sessionKeeper_.Random(), WorldTime);

        CreateParticleFpsChecked(BITMAP_FIRE, o->Position, o->Angle, Light, 5, 1.2f);

        Vector(Luminosity, Luminosity * 0.3f, Luminosity * 0.1f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 4, PrimaryTerrainLight);
    }
    return true;
}

// MODEL_CUNDUN_PART1, MODEL_CUNDUN_PART2, MODEL_CUNDUN_PART3, MODEL_CUNDUN_PART4, MODEL_CUNDUN_PART5, MODEL_CUNDUN_PART6, MODEL_CUNDUN_PART7, MODEL_CUNDUN_PART8, MODEL_ILLUSION_OF_KUNDUN
bool MoveBehavior::Move_MODEL_CUNDUN_PART1(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    switch (o->SubType)
    {
    case 1:
        o->Alpha -= (0.01f) * FPS_ANIMATION_FACTOR;
        break;
    case 2:
    case 3: {
        const float threshold = 300.f + (34 - o->Skill) * 5.f - 1.f;
        const float early =
            (std::min)(FPS_ANIMATION_FACTOR, (std::max)(0.f, o->LifeTime - threshold));
        if (o->SubType != 2)
        {
            const float emitting = (std::min)(early, (std::max)(0.f, o->LifeTime - 385.f));
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(emitting / 2.0, emitting))
            {
                vec3_t angle{0.f, 0.f, static_cast<float>(WorldRandom() % 360)};
                if (sessionKeeper_.Random()->FpsCheck(2, 1.0))
                    CreateJoint(BITMAP_JOINT_SPIRIT2, o->Position, o->Position, angle, 8, NULL,
                                50.f, 0, 0);
            }
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(emitting / 3.0, emitting))
            {
                vec3_t light{0.5f, 0.5f, 0.5f};
                CreateParticle(BITMAP_SMOKE + 3, o->Position, o->Angle, light, 2,
                               (WorldRandom() % 32 + 48) * 0.02f);
            }
        }
        o->MotionTrace.Advance(early, o->Position);
        float active = FPS_ANIMATION_FACTOR - early;
        float elapsed = early;
        if (active > 0.f && !o->EffectResting)
        {
            if (o->Position[2] > 350.f && o->Direction[0] == 0.f && o->Direction[1] == 0.f &&
                o->Direction[2] == 0.f)
            {
                o->Direction[0] = static_cast<float>((-1 + WorldRandom() % 2) * 2);
                o->Direction[1] = static_cast<float>((-1 + WorldRandom() % 2) * 2);
            }
            float falling = 0.f;
            bool landed = o->Position[2] <= 350.f;
            if (!landed)
            {
                falling = active;
                landed = AdvanceFallingPart(*o, -6.f, 350.f, falling);
            }
            active -= falling;
            elapsed += falling;
            if (landed)
            {
                o->EffectResting = true;
                o->EffectMotionFrames = 0.f;
                Vector(0.f, 0.f, -3.f, o->Direction);
                auto impactTime = sessionKeeper_.Gameplay()->EmissionTime(active);
                AddWaterWave(static_cast<int>(o->Position[0] / TERRAIN_SCALE),
                             static_cast<int>(o->Position[1] / TERRAIN_SCALE), 2, -1000);
                vec3_t light{0.3f, 0.3f, 0.3f};
                for (int i = 0; i < 100; ++i)
                {
                    vec3_t position, angle;
                    VectorCopy(o->Position, position);
                    const float heading = static_cast<float>(WorldRandom() % 360);
                    const float distance = static_cast<float>(WorldRandom() % 30 + 30);
                    position[0] += sinf(heading) * distance;
                    position[1] += cosf(heading) * distance;
                    position[2] -= 30.f;
                    VectorCopy(o->Angle, angle);
                    angle[2] += WorldRandom() % 180;
                    CreateParticle(BITMAP_WATERFALL_5, position, angle, light, 2);
                }
            }
        }
        if (o->EffectResting)
        {
            AdvanceSinkingPart(*o, active, elapsed, *sessionKeeper_.Random());
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(active, active, elapsed))
            {
                vec3_t position;
                VectorCopy(o->Position, position);
                position[0] += WorldRandom() % 40 - 20;
                position[1] += WorldRandom() % 40 - 20;
                position[2] -= 50.f;
                CreateParticle(BITMAP_BUBBLE, position, o->Angle, o->Light, 4);
            }
        }
        break;
    }
    case 4: {
        const float active =
            (std::max)(0.f, FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 139.f));
        o->EffectAnimationAdvance = o->Velocity * active;
        if (active == 0.f)
            o->AnimationFrame = 0.f;
        o->Alpha -= 0.01f * active;
        o->MotionTrace.Advance(FPS_ANIMATION_FACTOR - active, o->Position);
        float falling = active;
        if (falling > 0.f && AdvanceFallingPart(*o, -3.5f, o->StartPosition[2], falling))
            Vector(0.f, 0.f, 0.f, o->Direction);
        break;
    }
    case 5: {
        const float active =
            (std::max)(0.f, FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 149.f));
        o->EffectAnimationAdvance = o->Velocity * active;
        if (active == 0.f)
            o->AnimationFrame = 0.f;
        o->Alpha -= 0.01f * active;
        break;
    }
    }
    return true;
}

// MODEL_CURSEDTEMPLE_STATUE_PART1, MODEL_CURSEDTEMPLE_STATUE_PART2
bool MoveBehavior::Move_MODEL_CURSEDTEMPLE_STATUE_PART1(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    VectorCopy(o->Light, Light);
    constexpr float Floor = 290.f, FadeRate = 0.01f;
    float remaining = FPS_ANIMATION_FACTOR;
    while (remaining > 0.f && !o->EffectResting)
    {
        if (o->AmbientNoiseFrames <= 0.f)
        {
            o->AmbientTurnRate = float(WorldRandom() % 10);
            o->AmbientNoiseFrames = 1.f;
        }
        const double height = (std::max)(0.0, double(o->Position[2]) - Floor);
        const double speed = o->Direction[2] - o->Gravity * 0.5;
        const double root = std::sqrt(speed * speed + 2.0 * o->Gravity * height);
        const double contact = height == 0.0 ? 0.0
                               : speed < 0.0 ? 2.0 * height / (root - speed)
                                             : (speed + root) / o->Gravity;
        float step = (std::min)(remaining, o->AmbientNoiseFrames);
        const bool hits = contact <= step;
        if (hits)
            step = float(contact);
        const float previousYaw = o->Angle[2];
        o->Angle[2] -= o->AmbientTurnRate * step;
        vec3_t local{o->Direction[0], o->Direction[1], 0.f}, velocity;
        MoveTurningObject(*o, local, previousYaw, o->AmbientTurnRate, step, velocity);
        float vertical = o->Direction[2] - o->Gravity;
        Core::Time::Advance(o->Position[2], vertical, -o->Gravity, step);
        o->Direction[2] = vertical + o->Gravity;
        o->AmbientNoiseFrames -= step;
        remaining -= step;
        if (hits)
        {
            o->Position[2] = Floor;
            Vector(0.f, 0.f, 0.f, o->Direction);
            o->EffectResting = true;
            EarthQuake = float(WorldRandom() % 4 + 1) * 0.1f;
        }
    }
    if (o->EffectResting)
        o->Alpha = (std::max)(0.f, o->Alpha - FadeRate * remaining);
    return true;
}

// MODEL_XMAS2008_SNOWMAN_HEAD
bool MoveBehavior::Move_MODEL_XMAS2008_SNOWMAN_HEAD(OBJECT *o, int index, float Luminosity)
{
    // The strict prior-life check first advances gravity at authored life 44.
    const float active =
        std::clamp(44.f - (o->LifeTime - FPS_ANIMATION_FACTOR), 0.f, FPS_ANIMATION_FACTOR);
    const float early = FPS_ANIMATION_FACTOR - active;
    if (early > 0.f)
    {
        float matrix[3][4];
        vec3_t velocity;
        AngleMatrix(o->Angle, matrix);
        VectorRotate(o->Direction, matrix, velocity);
        VectorAddScaled(o->Position, velocity, o->Position, early);
    }
    AdvanceBouncingDebris(*o, *sessionKeeper_.WorldUnit(), active, DebrisMotion::Snowman);
    return true;
}

// MODEL_XMAS2008_SNOWMAN_BODY
bool MoveBehavior::Move_MODEL_XMAS2008_SNOWMAN_BODY(OBJECT *o, int index, float Luminosity)
{
    {
        BMD *b = &Models[o->Type];
        b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction,
                         o->Velocity / 5.f, o->Position, o->Angle);
    }
    return true;
}

// MODEL_DOPPELGANGER_SLIME_CHIP
bool MoveBehavior::Move_MODEL_DOPPELGANGER_SLIME_CHIP(OBJECT *o, int index, float Luminosity)
{
    AdvanceHeadDebris(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR, 20.f, 1.6f, 0.8f, 5.f,
                      WorldTime * 0.015,
                      15. / sessionKeeper_.ApplicationConfig().legacyReferenceFps);
    const float fading = (std::max)(0.f, FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 9.f));
    VectorScale(o->Light, powf(0.8f, fading), o->Light);

    if (o->SubType == 0)
    {
        o->Angle[0] +=
            0.35f * FPS_ANIMATION_FACTOR * (o->LifeTime - (FPS_ANIMATION_FACTOR - 1.f) * 0.5f);
        o->Angle[1] +=
            0.35f * FPS_ANIMATION_FACTOR * (o->LifeTime - (FPS_ANIMATION_FACTOR - 1.f) * 0.5f);
    }
    else
    {
        o->Angle[0] -=
            0.35f * FPS_ANIMATION_FACTOR * (o->LifeTime - (FPS_ANIMATION_FACTOR - 1.f) * 0.5f);
        o->Angle[1] -=
            0.35f * FPS_ANIMATION_FACTOR * (o->LifeTime - (FPS_ANIMATION_FACTOR - 1.f) * 0.5f);
    }
    return true;
}

// MODEL_WATER_WAVE
bool MoveBehavior::Move_MODEL_WATER_WAVE(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    o->Scale += 0.02f * FPS_ANIMATION_FACTOR;
    const float age = (std::max)(0.f, 20.f - o->LifeTime + FPS_ANIMATION_FACTOR);
    const float sampleAge = (std::max)(0.f, age - 1.f);
    o->Gravity = (std::max)(60.f, 120.f - 20.f * age);
    float speed = o->Velocity - 10.f, travel = 0.f;
    Core::Time::Advance(travel, speed, -10.f, FPS_ANIMATION_FACTOR);
    o->Velocity = speed + 10.f;
    o->EffectAnimationAdvance = travel;
    Vector(0.f, o->Velocity, 0.f, o->Direction);
    float matrix[3][4];
    AngleMatrix(o->Angle, matrix);
    o->Position[0] += matrix[0][1] * travel;
    o->Position[1] += matrix[1][1] * travel;
    o->Position[2] = o->StartPosition[2] + (std::max)(60.f, 120.f - 20.f * sampleAge) +
                     matrix[2][1] * o->Velocity;
    Vector(Luminosity * 0.1f, Luminosity * 0.3f, Luminosity * 0.6f, Light);

    for (int j = 0; j < 4; j++)
    {
        CreateParticleFpsChecked(BITMAP_WATERFALL_5, o->Position, o->Angle, Light, 1);
    }

    AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
    return true;
}

// MODEL_STAFF_OF_DESTRUCTION
bool MoveBehavior::Move_MODEL_STAFF_OF_DESTRUCTION(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Position;
    float matrix[3][4];
    vec3_t velocity;
    AngleMatrix(o->Angle, matrix);
    VectorRotate(o->Direction, matrix, velocity);
    const auto contact = sessionKeeper_.WorldUnit()->FirstTerrainContact(o->Position, velocity, 0.f,
                                                                         FPS_ANIMATION_FACTOR);
    VectorAddScaled(o->Position, velocity, o->Position,
                    contact ? contact->frames : FPS_ANIMATION_FACTOR);
    if (contact)
    {
        o->Position[2] = contact->height;
        VectorCopy(o->Position, Position);
        Position[2] += 80;
        CreateParticle(BITMAP_EXPLOTION, Position, o->Angle, Light);

        for (int j = 0; j < 6; j++)
        {
            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, o->Position, o->Angle, o->Light);
        }
        RetireEffect(o);
    }
    Vector(Luminosity * 0.2f, Luminosity * 0.4f, Luminosity * 1.f, Light);
    AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
    return true;
}

// MODEL_PIERCING
bool MoveBehavior::Move_MODEL_PIERCING(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    if (o->Owner->Live == false)
    {
        o->LifeTime = -1;
        return true;
    }

    o->Gravity += (90.f) * FPS_ANIMATION_FACTOR;
    VectorCopy(o->Owner->Angle, o->Angle);
    VectorCopy(o->Owner->Angle, o->HeadAngle);
    VectorCopy(o->Position, o->StartPosition);
    VectorCopy(o->Owner->Position, o->Position);
    if (o->SubType == 1)
    {
        Vector(1.f, 1.f, 1.f, Light);

        CreateJointFpsChecked(BITMAP_JOINT_THUNDER, o->Position, o->Position, o->Angle, 3, NULL,
                              20.f, 7); //  전기
        CreateSprite(BITMAP_SHINY + 1, o->Position, (float)(WorldRandom() % 8 + 8) * 0.2f, Light, o,
                     (float)(WorldRandom() % 360));
    }

    if (gMapManager.InHellas())
    {
        if (o->Owner != NULL && o->Owner->Owner != NULL && o->Owner->Owner == (&Hero->Object))
        {
            EmitWaterWaves(*o, *o->Owner, 1.f, -200);
        }
    }

    o->HeadAngle[1] = o->Gravity;
    return true;
}

// MODEL_ARROW_BEST_CROSSBOW
bool MoveBehavior::Move_MODEL_ARROW_BEST_CROSSBOW(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    float Matrix[3][4];
    if (o->LifeTime > 25)
    {
        o->BlendMeshLight = (30 - o->LifeTime) / 40.f;
    }
    else
    {
        o->BlendMeshLight = 1.f;
    }
    AdvanceRandomRotatingEffect(*o, *sessionKeeper_.Random(), FPS_ANIMATION_FACTOR, 1, 60, 30.f);
    VectorCopy(o->Position, o->EyeLeft);

    VectorCopy(o->Angle, Angle);
    AngleMatrix(Angle, Matrix);
    VectorRotate(o->Direction, Matrix, Position);
    VectorAdd(o->EyeLeft, Position, o->EyeLeft);

    Vector(Luminosity * 1.f, Luminosity * 0.4f, Luminosity * 0.2f, Light);
    AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
    CheckClientArrow(o);
    return true;
}

// MODEL_ARROW_DOUBLE
bool MoveBehavior::Move_MODEL_ARROW_DOUBLE(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    MoveRotatingPosition(o->Position, o->Angle, o->Direction, 1, 30.f, FPS_ANIMATION_FACTOR);
    VectorCopy(o->Position, o->EyeLeft);
    Vector(Luminosity * 0.2f, Luminosity * 0.4f, Luminosity * 1.f, Light);
    AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
    CheckClientArrow(o);
    return true;
}

// MODEL_ARROW_HOLY
bool MoveBehavior::Move_MODEL_ARROW_HOLY(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    if (o->SubType == 1)
    {
        MoveRotatingPosition(o->StartPosition, o->Angle, o->Direction, 1, 60.f,
                             FPS_ANIMATION_FACTOR);

        if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 13))
            CreateEffect(MODEL_PIERCING, o->Position, o->Angle, o->Light, 3, o);
        CheckClientArrow(o);

        Vector(Luminosity * 0.9f, Luminosity * 0.4f, Luminosity * 0.6f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
    }
    else
    {
        MoveRotatingPosition(o->StartPosition, o->Angle, o->Direction, 1, 30.f,
                             FPS_ANIMATION_FACTOR);

        Vector(Luminosity * 0.2f, Luminosity * 0.4f, Luminosity * 1.f, Light);
        AddTerrainLight(o->StartPosition[0], o->StartPosition[1], Light, 2, PrimaryTerrainLight);

        if (o->SubType != 0)
        {
            if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 13))
            {
                CreateEffect(MODEL_PIERCING, o->Position, o->Angle, o->Light, 0, o);
            }
            else if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 30))
            {
                o->AttackPoint[0] = 0;
                o->Kind = 1;
            }
        }
    }
    return true;
}

// MODEL_ARROW
bool MoveBehavior::Move_MODEL_ARROW(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    float Height;
    Vector(Luminosity * 0.8f, Luminosity * 0.5f, Luminosity * 0.2f, Light);
    AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);

    if (o->SubType == 3 || o->SubType == 4)
    {
        AdvanceGroundedArrow(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR);
        CreateParticleFpsChecked(BITMAP_FIRE, o->Position, o->Angle, Light, 5);
    }
    else if (o->SubType == 5)
    {
        Vector(Luminosity * 0.1f, Luminosity * 0.6f, Luminosity * 0.3f, Light);
        CreateParticleFpsChecked(BITMAP_FIRE, o->Position, o->Angle, Light);
        CheckClientArrow(o);
    }
    else
    {
        CreateParticleFpsChecked(BITMAP_FIRE, o->Position, o->Angle, Light);
        CheckClientArrow(o);
    }
    return true;
}

void MoveBehavior::EmitRotatingArrowTrail(OBJECT &effect)
{
    const bool lace = effect.Type == MODEL_LACEARROW;
    const bool flowers = !lace && effect.SubType != 1;
    const float turn = flowers ? 30.f : 60.f;
    const auto sample = [&](float elapsed, vec3_t position, vec3_t angle) {
        VectorCopy(effect.Position, position);
        VectorCopy(effect.Angle, angle);
        MoveRotatingPosition(position, angle, effect.Direction, 1, turn, elapsed);
    };
    const auto emitGroup = [&](vec3_t origin, vec3_t angle) {
        const int count = flowers ? 4 : lace ? 3 : 2;
        vec3_t light{0.4f, lace ? 0.2f : 1.f, lace ? 1.f : 0.2f};
        for (int i = 0; i < count; ++i)
        {
            vec3_t position{origin[0] + WorldRandom() % 32 - 16,
                            origin[1] + WorldRandom() % 64 - 32,
                            origin[2] + WorldRandom() % 32 - 16};
            if (flowers)
                CreateParticle(BITMAP_FLOWER01 + WorldRandom() % 3, position, angle, effect.Light);
            else
                CreateParticle(BITMAP_FLARE, position, angle, light, 5, 0.2f);
        }
    };
    if (!flowers)
    {
        for (float life = Core::Time::ReferenceSample(effect.LifeTime / 2.f) * 2.f;
             life > (lace ? 0.f : 25.f) &&
             Core::Time::Reaches(effect.LifeTime, FPS_ANIMATION_FACTOR, life);
             life -= 2.f)
        {
            const float elapsed = std::max(0.f, effect.LifeTime - life);
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - elapsed);
            vec3_t position, angle;
            sample(elapsed, position, angle);
            emitGroup(position, angle);
        }
    }
    if (!flowers && !lace)
        return;
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        vec3_t position, angle;
        sample(FPS_ANIMATION_FACTOR - birth.RemainingFrames(), position, angle);
        if (flowers)
            emitGroup(position, angle);
        else
            CreateEffect(MODEL_WAVES, position, angle, effect.Light, 3, nullptr, 0);
    }
}

// MODEL_ARROW_STEEL, MODEL_ARROW_THUNDER, MODEL_ARROW_LASER, MODEL_ARROW_V, MODEL_ARROW_SAW, MODEL_ARROW_NATURE, MODEL_ARROW_WING, MODEL_LACEARROW
bool MoveBehavior::Move_MODEL_ARROW_STEEL(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Position;
    if (o->Type == MODEL_ARROW_NATURE || o->Type == MODEL_LACEARROW)
        EmitRotatingArrowTrail(*o);
    if (o->Type == MODEL_ARROW_NATURE)
    {
        if (o->SubType == 1)
        {
            Vector(0.1f, 0.4f, 0.1f, Light);
            CreateSprite(BITMAP_LIGHTNING + 1, o->Position, 0.3f, Light, o, (int)WorldTime * 0.1f);
            CreateSprite(BITMAP_LIGHTNING + 1, o->Position, 0.7f, Light, o, -(int)WorldTime * 0.1f);

            Vector(Luminosity * 0.2f, Luminosity * 0.8f, Luminosity * 0.2f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);

            MoveRotatingPosition(o->Position, o->Angle, o->Direction, 1, 60.f,
                                 FPS_ANIMATION_FACTOR);
        }
        else
        {
            CreateSprite(BITMAP_LIGHTNING + 1, o->Position, 0.5f, o->Light, o,
                         (int)WorldTime * 0.1f);
            CreateSprite(BITMAP_LIGHTNING + 1, o->Position, 1.f, o->Light, o,
                         -(int)WorldTime * 0.1f);

            MoveRotatingPosition(o->Position, o->Angle, o->Direction, 1, 30.f,
                                 FPS_ANIMATION_FACTOR);

            Vector(Luminosity * 0.6f, Luminosity * 0.8f, Luminosity * 0.8f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
        }
    }
    else if (o->Type == MODEL_LACEARROW)
    {
        MoveRotatingPosition(o->Position, o->Angle, o->Direction, 1, 60.f, FPS_ANIMATION_FACTOR);

        VectorCopy(o->Position, o->EyeLeft);

        Vector(Luminosity * 0.6f, Luminosity * 0.2f, Luminosity * 0.8f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
    }
    else if (o->Type == MODEL_ARROW_WING)
    {
        CreateSprite(BITMAP_LIGHTNING + 1, o->Position, 0.5f, o->Light, o, (int)WorldTime * 0.1f);
        CreateSprite(BITMAP_LIGHTNING + 1, o->Position, 1.f, o->Light, o, -(int)WorldTime * 0.1f);
        float matrix[3][4];
        vec3_t velocity;
        AngleMatrix(o->Angle, matrix);
        VectorRotate(o->Direction, matrix, velocity);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            vec3_t origin;
            VectorAddScaled(o->Position, velocity, origin,
                            FPS_ANIMATION_FACTOR - birth.RemainingFrames());
            for (int j = 0; j < 4; ++j)
            {
                Vector(origin[0] + WorldRandom() % 16 - 8, origin[1] + WorldRandom() % 16 - 8,
                       origin[2] + WorldRandom() % 16 - 8, Position);
                CreateParticle(BITMAP_BUBBLE, Position, o->Angle, o->Light, 1);
            }
            CreateParticle(BITMAP_SMOKE, origin, o->Angle, o->Light, 0);
        }

        Vector(Luminosity * 0.6f, Luminosity * 0.8f, Luminosity * 0.8f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
    }
    CheckClientArrow(o);
    return true;
}

// MODEL_DARK_SCREAM_FIRE, MODEL_DARK_SCREAM
bool MoveBehavior::Move_MODEL_DARK_SCREAM_FIRE(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->Type == MODEL_DARK_SCREAM_FIRE)
        {
            o->Scale -= (0.14f) * FPS_ANIMATION_FACTOR;
        }
        if (o->Type == MODEL_DARK_SCREAM)
        {
            o->Scale -= (0.04f) * FPS_ANIMATION_FACTOR;
        }
        if (o->Scale < 0.1f)
        {
            o->Scale = 0.f;
        }

        o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 3.f;

        CreateParticleFpsChecked(BITMAP_FLAME, o->Position, o->Angle, o->Light, 8,
                                 (o->Scale - 0.4f) * 3.5f);
        if (o->Type == MODEL_DARK_SCREAM)
        {
            CheckClientArrow(o);
        }
    }
    return true;
}

// MODEL_CURSEDTEMPLE_HOLYITEM
bool MoveBehavior::Move_MODEL_CURSEDTEMPLE_HOLYITEM(OBJECT *o, int index, float Luminosity)
{
    {
        if (!o->Owner->Live)
        {
            RetireEffect(o);
            return true;
        }

        if (o->LifeTime < 10)
            o->LifeTime = 999999;

        BMD *b = &Models[o->Owner->Type];
        vec3_t vRelativePos, vWorldPos;
        Vector(70.f, 0.f, 0.f, vRelativePos);

        b->BodyScale = o->Owner->Scale;
        VectorCopy(o->Owner->Position, b->BodyOrigin);
        b->TransformPosition(o->Owner->BoneTransform[20], vRelativePos, vWorldPos, true);

        //vWorldPos[0] = o->Owner->Position[0];
        //vWorldPos[1] = o->Owner->Position[1];

        Vector(1.0f, 1.0f, 1.0f, o->Light);
        VectorCopy(vWorldPos, o->Position);

        b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction,
                         o->Velocity / 3.f, o->Position, o->Angle);
    }
    return true;
}

// MODEL_CURSEDTEMPLE_PRODECTION_SKILL
bool MoveBehavior::Move_MODEL_CURSEDTEMPLE_PRODECTION_SKILL(OBJECT *o, int index, float Luminosity)
{
    vec3_t Position;
    {
        if (!o->Owner->Live)
        {
            RetireEffect(o);
            return true;
        }

        if (o->LifeTime < 10)
            o->LifeTime = 999999;

        vec3_t RelativePos, Position;
        BMD *herobmd = &Models[o->Owner->Type];
        VectorCopy(o->Owner->Position, herobmd->BodyOrigin);
        Vector(-23.f, 0.f, 0.f, RelativePos);
        herobmd->TransformPosition(o->Owner->BoneTransform[2], RelativePos, Position, true);

        Vector(1.0f, 1.0f, 1.0f, o->Light);
        VectorCopy(Position, o->Position);
        VectorCopy(o->Owner->Angle, o->Angle);

        o->Alpha = 0.3f;
        o->Scale = 1.0f;

        BMD *effbmd = &Models[o->Type];

        effbmd->SetBodyLight(o->Light);
        effbmd->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction,
                              o->Velocity / 3.f, o->Position, o->Angle);

        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            vec3_t position;
            o->Owner->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Owner->Position,
                                         position);
            CreateEffect(BITMAP_SHOCK_WAVE, position, o->Angle, o->Light, 10, o->Owner);
        }
    }
    return true;
}

// MODEL_CURSEDTEMPLE_RESTRAINT_SKILL
bool MoveBehavior::Move_MODEL_CURSEDTEMPLE_RESTRAINT_SKILL(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    {
        if (!o->Owner->Live)
        {
            RetireEffect(o);
            return true;
        }

        if (o->LifeTime < 10)
            o->LifeTime = 999999;

        Vector(1.0f, 1.0f, 1.0f, o->Light);
        VectorCopy(o->Owner->Position, o->Position);
        VectorCopy(o->Owner->Angle, o->Angle);
        o->Alpha = 0.6f;
        o->Scale = 1.0f;

        BMD *b = &Models[o->Type];
        b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction,
                         o->Velocity / 3.f, o->Position, o->Angle);

        vec3_t vRelativePos, Light;
        Vector(0.4f, 0.4f, 0.8f, Light);
        Vector(0.f, 0.f, 0.f, vRelativePos);
    }
    return true;
}

// MODEL_ARROW_SPARK
bool MoveBehavior::Move_MODEL_ARROW_SPARK(OBJECT *o, int index, float Luminosity)
{
    {
        o->Angle[1] += (35.0f) * FPS_ANIMATION_FACTOR;
        CheckClientArrow(o);
    }
    return true;
}

// MODEL_ARROW_RING
bool MoveBehavior::Move_MODEL_ARROW_RING(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    {
        VectorCopy(o->Position, o->EyeLeft);
        Vector(0.0f, 1.0f, 0.1f, o->Light);
        float matrix[3][4];
        vec3_t velocity;
        AngleMatrix(o->Angle, matrix);
        VectorRotate(o->Direction, matrix, velocity);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            vec3_t position;
            VectorAddScaled(o->Position, velocity, position,
                            FPS_ANIMATION_FACTOR - birth.RemainingFrames());
            CreateEffect(MODEL_WAVES, position, o->Angle, o->Light, 4, nullptr, 0);
        }

        Vector(Luminosity * 0.6f, Luminosity * 0.2f, Luminosity * 0.8f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);

        CheckClientArrow(o);
    }
    return true;
}

// MODEL_ARROW_TANKER
bool MoveBehavior::Move_MODEL_ARROW_TANKER(OBJECT *o, int index, float Luminosity)
{
    {
        Vector(1.0f, 0.0f, 0.0f, o->Light);
        AddTerrainLight(o->Position[0], o->Position[1], o->Light, 2, PrimaryTerrainLight);
    }
    return true;
}

// MODEL_ARROW_BOMB
bool MoveBehavior::Move_MODEL_ARROW_BOMB(OBJECT *o, int index, float Luminosity)
{
    vec3_t light{1.f, 0.6f, 0.4f};
    CreateSprite(BITMAP_LIGHT, o->Position, 1.f, light, o, (int)WorldTime * 0.1f);
    CreateSprite(BITMAP_LIGHT, o->Position, 2.f, light, o, -(int)WorldTime * 0.1f);
    const float ground = RequestTerrainHeight(o->Position[0], o->Position[1]);
    const auto sample = [&](float frames, vec3_t position) {
        vec3_t direction;
        float gravity = o->Gravity;
        VectorCopy(o->Position, position);
        VectorCopy(o->Direction, direction);
        GameLogic::Effects::Motion::AdvanceJump(position, gravity, direction, o->Angle, ground,
                                                frames);
    };
    const float active = std::min(FPS_ANIMATION_FACTOR, std::max(0.f, o->LifeTime));
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(active, active))
    {
        vec3_t origin;
        sample(FPS_ANIMATION_FACTOR - birth.RemainingFrames(), origin);
        for (int j = 0; j < 4; ++j)
        {
            vec3_t position{origin[0] + WorldRandom() % 16 - 8, origin[1] + WorldRandom() % 16 - 8,
                            origin[2] + WorldRandom() % 16 - 8};
            CreateParticle(BITMAP_BUBBLE, position, o->Angle, o->Light, 1);
        }
    }
    if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 1.f))
    {
        const float elapsed = std::max(0.f, o->LifeTime - 1.f);
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - elapsed);
        vec3_t position;
        sample(elapsed, position);
        CreateBomb(position, true);
        if (o->Owner == &Hero->Object && o->SubType != 99)
            AttackCharacterRange(o->Skill, position, 100.f, o->Weapon, o->PKKey);
    }
    MoveJump(o);
    Vector(Luminosity * 0.6f, Luminosity * 0.8f, Luminosity * 0.8f, light);
    AddTerrainLight(o->Position[0], o->Position[1], light, 2, PrimaryTerrainLight);
    CheckClientArrow(o);
    return true;
}

void MoveBehavior::EmitDarkStingerFeathers(OBJECT &effect, BMD &model, vec3_t velocity)
{
    AnimationPoseSample pose(&effect, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    for (float life : {30.f, 28.f, 26.f, 24.f})
    {
        if (!Core::Time::Reaches(effect.LifeTime, FPS_ANIMATION_FACTOR, life))
            continue;
        const float elapsed = std::max(0.f, effect.LifeTime - life);
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - elapsed);
        vec3_t position, offset{}, light{0.6f, 0.7f, 0.9f};
        pose.SampleBonePosition(model, effect, 1, offset, WorldTime, birth.FrameFraction(),
                                position);
        VectorAddScaled(position, velocity, position, elapsed);
        const int pairs = WorldRandom() % 3;
        for (int i = 0; i < pairs; ++i)
        {
            CreateEffect(MODEL_FEATHER, position, effect.Angle, light, 0, nullptr, -1, 0, 0, 0,
                         0.6f);
            CreateEffect(MODEL_FEATHER, position, effect.Angle, light, 1, nullptr, -1, 0, 0, 0,
                         0.6f);
        }
        if (life != 30.f)
            continue;
        Vector(0.4f, 0.4f, 0.9f, light);
        CreateJoint(BITMAP_FLARE + 1, position, position, effect.Angle, 18, &effect, 90.f, 40, 0, 0,
                    -1, light);
    }
}

// MODEL_ARROW_DARKSTINGER
bool MoveBehavior::Move_MODEL_ARROW_DARKSTINGER(OBJECT *o, int index, float Luminosity)
{
    CheckClientArrow(o);
    BMD &model = Models[o->Type];
    vec3_t position, velocity, light{0.6f, 0.7f, 0.9f};
    model.Animation(BoneTransform, o->AnimationFrame, o->PriorAnimationFrame, o->PriorAction,
                    o->Angle, o->HeadAngle, false, false);
    model.TransformByObjectBone(position, o, 0);
    float matrix[3][4];
    AngleMatrix(o->Angle, matrix);
    VectorRotate(o->Direction, matrix, velocity);
    VectorAdd(position, velocity, position);
    CreateSprite(BITMAP_LIGHT, position, 3.f, light, o);
    CreateSprite(BITMAP_LIGHT, position, 2.f, light, o);
    EmitDarkStingerFeathers(*o, model, velocity);
    return true;
}

// MODEL_DUNGEON_STONE01
bool MoveBehavior::Move_MODEL_DUNGEON_STONE01(OBJECT *o, int index, float Luminosity)
{
    float Height;
    AdvanceEffectGravity(*o, 1.f, -1.f, FPS_ANIMATION_FACTOR);
    Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
    if (o->Position[2] < Height)
    {
        o->Position[2] = Height;
        o->Gravity = -o->Gravity * 0.4f;
        o->LifeTime -= (4) * FPS_ANIMATION_FACTOR;
        o->Direction[1] -= (2.f) * FPS_ANIMATION_FACTOR;
    }
    return true;
}

// MODEL_WARCRAFT
bool MoveBehavior::Move_MODEL_WARCRAFT(OBJECT *o, int index, float Luminosity)
{
    VectorCopy(o->Owner->Position, o->Position);
    return true;
}

// BITMAP_FIRECRACKERRISE
bool MoveBehavior::Move_BITMAP_FIRECRACKERRISE(OBJECT *o, int index, float Luminosity)
{
    constexpr float LaunchInterval = 5.f;
    for (float sample = Core::Time::ReferenceSample(o->LifeTime / LaunchInterval) * LaunchInterval;
         sample > 0.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, sample);
         sample -= LaunchInterval)
    {
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR -
                                                             std::max(0.f, o->LifeTime - sample));
        if (WorldRandom() % 3 == 0)
            CreateEffect(BITMAP_FIRECRACKER, o->Position, o->Angle, o->Light);
    }
    return true;
}

// BITMAP_FIRECRACKER
bool MoveBehavior::Move_BITMAP_FIRECRACKER(OBJECT *o, int index, float Luminosity)
{
    float matrix[3][4];
    vec3_t velocity, position, light;
    AngleMatrix(o->Angle, matrix);
    VectorRotate(o->Direction, matrix, velocity);
    constexpr float ExplosionLife = 1.f;
    if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, ExplosionLife))
    {
        const float elapsed = std::max(0.f, o->LifeTime - ExplosionLife);
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - elapsed);
        VectorAddScaled(o->Position, velocity, position, elapsed);
        const int subtype = WorldRandom() % 30;
        Vector((WorldRandom() % 3) * .3f + .4f, (WorldRandom() % 4) * .1f, .0f, light);
        constexpr int ExplosionParticles = 80;
        for (int j = 0; j < ExplosionParticles; ++j)
            CreateParticle(BITMAP_FIRECRACKER, position, o->Angle, light, subtype);
        PlayBuffer(SOUND_FIRECRACKER2, o);
    }
    const float active = std::min(FPS_ANIMATION_FACTOR, std::max(0.f, o->LifeTime));
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(active, active))
    {
        VectorAddScaled(o->Position, velocity, position,
                        FPS_ANIMATION_FACTOR - birth.RemainingFrames());
        CreateParticle(BITMAP_FIRECRACKER, position, o->Angle, o->Light, -1);
    }
    Vector(Luminosity * .4f, Luminosity * 0.3f, Luminosity * 0.2f, light);
    AddTerrainLight(o->Position[0], o->Position[1], light, 1, PrimaryTerrainLight);
    return true;
}

// BITMAP_FIRECRACKER0001
bool MoveBehavior::Move_BITMAP_FIRECRACKER0001(OBJECT *o, int index, float Luminosity)
{
    constexpr float BurstLives[] = {31.f, 24.f, 17.f, 9.f, 1.f};
    for (float life : BurstLives)
    {
        if (!Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life))
            continue;
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR -
                                                             std::max(0.f, o->LifeTime - life));
        vec3_t position{o->Position[0] + (WorldRandom() % 200 - 100),
                        o->Position[1] + (WorldRandom() % 200 - 100), o->Position[2]};
        CreateJoint(BITMAP_JOINT_SPIRIT, position, position, o->Angle, 25, o, 1.f, -1, o->SubType);
    }
    return true;
}

// BITMAP_FIRECRACKER0002
bool MoveBehavior::Move_BITMAP_FIRECRACKER0002(OBJECT *o, int index, float Luminosity)
{
    vec3_t Position;
    {
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 5.0))
        {
            vec3_t vLight;
            Vector(0.3f + (WorldRandom() % 700) * 0.001f, 0.3f + (WorldRandom() % 700) * 0.001f,
                   0.3f + (WorldRandom() % 700) * 0.001f, vLight);
            Vector(o->Position[0] + (WorldRandom() % 300 - 150),
                   o->Position[1] + (WorldRandom() % 300 - 150), o->Position[2], Position);
            float fScale = 0.5f + (WorldRandom() % 5) * 0.1f;
            CreateEffect(BITMAP_FIRECRACKER0003, Position, o->Angle, vLight, 0, o, -1, 0, 0, 0,
                         fScale);
        }
    }
    return true;
}

// BITMAP_FIRECRACKER0003
bool MoveBehavior::Move_BITMAP_FIRECRACKER0003(OBJECT *o, int index, float Luminosity)
{
    {
        int iFrame;
        if (o->LifeTime > 15 - 8)
        {
            iFrame = (15 - o->LifeTime) / 8.0f * 7;
            if (iFrame >= 7)
                iFrame = 6;

            if (iFrame == 2)
            {
                // 					CreateSprite(BITMAP_DS_SHOCK, o->Position, o->Scale*1.5f, o->Light, o);
            }
        }
        else
        {
            iFrame = 6;
            o->Position[2] -= (1.0f) * FPS_ANIMATION_FACTOR;
            o->Light[0] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            o->Scale *= pow(1.02f, FPS_ANIMATION_FACTOR);
        }
        CreateSprite(BITMAP_FIRECRACKER0001 + iFrame, o->Position, o->Scale, o->Light, o,
                     o->Angle[2]);
    }
    return true;
}

// BITMAP_SWORD_FORCE
bool MoveBehavior::Move_BITMAP_SWORD_FORCE(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Position;
    if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR,
                            30.f)) // at the first frame of the effect
    {
        VectorCopy(o->Position, Position);
        Position[2] += 100.f;
        if (o->SubType == 1)
        {
            vec3_t Light;
            Vector(1.f, 1.f, 1.f, Light);
            CreateJoint(BITMAP_JOINT_FORCE, Position, Position, o->HeadAngle, 10, o->Owner, 150.f,
                        o->PKKey, o->Skill, 0, -1, Light);
        }
        else if (o->SubType == 0)
            CreateJoint(BITMAP_JOINT_FORCE, Position, Position, o->HeadAngle, 0, o->Owner, 150.f,
                        o->PKKey, o->Skill);
        else
            CreateJoint(BITMAP_JOINT_FORCE, Position, Position, o->HeadAngle, 8, o->Owner, 150.f,
                        o->PKKey, o->Skill);
    }
    return true;
}

// BITMAP_BLIZZARD
bool MoveBehavior::Move_BITMAP_BLIZZARD(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Position;
    float Height;
    if (o->LifeTime <= 15)
    {
        if (o->SubType == 0)
        {
            o->Position[0] = o->StartPosition[0] + sinf((WorldRandom() % 1000) * 0.01f) * 10.f;
            o->Position[1] = o->StartPosition[1] + sinf((WorldRandom() % 1000) * 0.01f) * 10.f;
            AdvanceEffectGravity(*o, 1.f, -2.f, FPS_ANIMATION_FACTOR);

            o->StartPosition[0] -= (10.f) * FPS_ANIMATION_FACTOR;

            CreateParticleFpsChecked(BITMAP_FIRE + 2, o->Position, o->Angle, Light, 7, o->Scale);

            o->Light[0] += (0.1f) * FPS_ANIMATION_FACTOR;
            o->Light[1] = o->Light[0];
            o->Light[2] = o->Light[0];
            CreateSprite(BITMAP_SHINY + 1, o->Position, (float)(WorldRandom() % 4 + 4) * 0.2f,
                         o->Light, o, (float)(WorldRandom() % 360));
            CreateSprite(BITMAP_LIGHT, o->Position, 1.f, o->Light, o, (float)(WorldRandom() % 360));
        }
        else if (o->SubType == 1)
        {
            o->Position[0] = o->StartPosition[0];
            o->Position[1] = o->StartPosition[1];
            AdvanceEffectGravity(*o, 1.f, -2.f, FPS_ANIMATION_FACTOR);

            o->StartPosition[0] -= (10.f) * FPS_ANIMATION_FACTOR;

            CreateParticleFpsChecked(BITMAP_FIRE + 2, o->Position, o->Angle, Light, 11, o->Scale);

            o->Light[0] += (0.1f) * FPS_ANIMATION_FACTOR;
            o->Light[1] = o->Light[0];
            o->Light[2] = o->Light[0];
            CreateSprite(BITMAP_SHINY + 1, o->Position, (float)(WorldRandom() % 4 + 4) * 0.2f,
                         o->Light, o, (float)(WorldRandom() % 360));
            CreateSprite(BITMAP_LIGHT, o->Position, 1.f, o->Light, o, (float)(WorldRandom() % 360));

            Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
            if (o->Position[2] < Height)
            {
                Vector(0.24f, 0.28f, 0.8f, Light);
                VectorCopy(o->Position, Position);
                Position[2] += 50.f;
                CreateParticleFpsChecked(BITMAP_SMOKE, Position, o->Angle, Light, 11,
                                         (float)(WorldRandom() % 32 + 80) * 0.025f);

                for (auto birthTime :
                     sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 5.0))
                    CreateEffect(MODEL_ICE_SMALL, Position, o->Angle, o->Light);
            }
        }
    }
    return true;
}

// BITMAP_SHOTGUN
bool MoveBehavior::Move_BITMAP_SHOTGUN(OBJECT *o, int index, float Luminosity)
{
    vec3_t light{Luminosity * 0.5f, Luminosity * 0.5f, Luminosity * 0.8f};
    AddTerrainLight(o->Position[0], o->Position[1], light, 2, PrimaryTerrainLight);
    Vector(1.f, 1.f, 1.f, light);
    float matrix[3][4];
    vec3_t velocity, angle, offset, position, origin;
    AngleMatrix(o->Angle, matrix);
    VectorRotate(o->Direction, matrix, velocity);
    VectorCopy(o->Angle, angle);
    angle[2] += 90.f;
    AngleMatrix(angle, matrix);
    vec3_t side{0.f, 20.f, 0.f};
    VectorRotate(side, matrix, offset);
    const float active = std::min(FPS_ANIMATION_FACTOR, std::max(0.f, o->LifeTime));
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(active, active))
    {
        const float elapsed = FPS_ANIMATION_FACTOR - birth.RemainingFrames();
        VectorAddScaled(o->Position, velocity, origin, elapsed);
        const float scale = ((15.f - o->LifeTime + elapsed) / 20.f) * (WorldRandom() % 3 + 2);
        VectorAdd(origin, offset, position);
        CreateParticle(BITMAP_FIRE + 2, position, o->Angle, light, 10, scale);
        VectorSubtract(origin, offset, position);
        CreateParticle(BITMAP_FIRE + 2, position, o->Angle, light, 10, scale);
    }
    constexpr float ExplosionLife = 1.f;
    if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, ExplosionLife))
    {
        const float elapsed = std::max(0.f, o->LifeTime - ExplosionLife);
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - elapsed);
        VectorAddScaled(o->Position, velocity, position, elapsed);
        CreateBomb2(position, false);
    }
    return true;
}

// MODEL_SHINE
bool MoveBehavior::Move_MODEL_SHINE(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType != 0 || o->Owner == nullptr)
        return true;
    constexpr float EmissionEndLife = 10.f;
    const float active =
        std::min(FPS_ANIMATION_FACTOR, std::max(0.f, o->LifeTime - EmissionEndLife));
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(active, active))
    {
        vec3_t position;
        o->Owner->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Owner->Position,
                                     position);
        const float offset = float(WorldRandom() % 128 - 64) - 50.f;
        position[0] += offset;
        position[1] += offset + 100.f;
        position[2] += 360.f;
        const float scale = (WorldRandom() % 30 + 50) / 100.f;
        CreateParticle(BITMAP_SHINY, position, o->Angle, o->Light, 2, scale);
    }
    return true;
}

// MODEL_BLIZZARD
bool MoveBehavior::Move_MODEL_BLIZZARD(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Position;
    float Height;
    if (o->SubType == 0)
    {
        const float flying = AdvanceBlizzardMotion(*o, *sessionKeeper_.WorldUnit(),
                                                   *sessionKeeper_.Random(), FPS_ANIMATION_FACTOR);

        for (auto birth : sessionKeeper_.Gameplay()->Emissions(flying, flying))
        {
            o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, Position);
            CreateParticle(BITMAP_SMOKE, Position, o->Angle, Light, 0, 1.5f);
            if (WorldRandom() % 2 == 0)
                CreateParticle(BITMAP_ENERGY, Position, o->Angle, Light, 1, 0.5f);
            else
                CreateParticle(BITMAP_FIRE + 2, Position, o->Angle, Light, 7, o->Scale);
        }

        o->Light[0] += (0.1f) * FPS_ANIMATION_FACTOR;
        o->Light[1] = o->Light[0];
        o->Light[2] = o->Light[0];
        CreateSprite(BITMAP_SHINY + 1, o->Position, (float)(WorldRandom() % 4 + 4) * 0.2f, o->Light,
                     o, (float)(WorldRandom() % 360));
        CreateSprite(BITMAP_LIGHT, o->Position, 1.f, o->Light, o, (float)(WorldRandom() % 360));

        Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
        if (o->EffectResting)
        {
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - flying);
            Vector(0.24f, 0.28f, 0.8f, Light);
            VectorCopy(o->Position, Position);
            Position[2] += 50.f;
            CreateParticle(BITMAP_SMOKE, Position, o->Angle, Light, 11,
                           (float)(WorldRandom() % 32 + 80) * 0.025f);

            if (WorldRandom() % 5 == 0)
            {
                CreateEffect(MODEL_ICE_SMALL, Position, o->Angle, o->Light);
            }

            CreateEffect(MODEL_BLIZZARD, Position, o->Angle, o->Light, 1, NULL, o->PKKey);

            RetireEffect(o);
        }
    }
    else if (o->SubType == 1)
    {
        o->BlendMeshLight *= pow(1.0f / (1.1f), FPS_ANIMATION_FACTOR);

        if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 18) && o->PKKey == 1)
            PlayBuffer(SOUND_SUDDEN_ICE2);
    }
    else if (o->SubType == 2)
    {
        const float flying = AdvanceBlizzardMotion(*o, *sessionKeeper_.WorldUnit(),
                                                   *sessionKeeper_.Random(), FPS_ANIMATION_FACTOR);

        Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
        if (o->Position[2] > Height)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(flying / 2.0, flying))
            {
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, Position);
                CreateParticle(BITMAP_FIRE + 2, Position, o->Angle, Light, 7, o->Scale);
            }

            o->Light[0] += (0.1f) * FPS_ANIMATION_FACTOR;
            o->Light[1] = o->Light[0];
            o->Light[2] = o->Light[0];
            CreateSprite(BITMAP_SHINY + 1, o->Position, (float)(WorldRandom() % 4 + 4) * 0.2f,
                         o->Light, o, (float)(WorldRandom() % 360));
            CreateSprite(BITMAP_LIGHT, o->Position, 1.f, o->Light, o, (float)(WorldRandom() % 360));
        }
    }
    return true;
}

// MODEL_ARROW_DRILL
bool MoveBehavior::Move_MODEL_ARROW_DRILL(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType != 0 && o->SubType != 2)
        return true;
    constexpr float TurnRate = 30.f, ExplosionLife = 1.f;
    const auto sample = [&](float elapsed, vec3_t position, vec3_t angle) {
        VectorCopy(o->Position, position);
        VectorCopy(o->Angle, angle);
        MoveRotatingPosition(position, angle, o->Direction, 1, TurnRate, elapsed);
    };
    vec3_t light{1.f, 1.f, 1.f};
    const float active = std::min(FPS_ANIMATION_FACTOR, std::max(0.f, o->LifeTime));
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(active, active))
    {
        vec3_t position, angle;
        sample(FPS_ANIMATION_FACTOR - birth.RemainingFrames(), position, angle);
        for (int i = 0; i < 3; ++i)
            CreateParticle(BITMAP_SMOKE, position, angle, light, 13);
    }
    if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, ExplosionLife))
    {
        const float elapsed = std::max(0.f, o->LifeTime - ExplosionLife);
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - elapsed);
        vec3_t position, angle;
        sample(elapsed, position, angle);
        CreateBomb(position, true);
    }
    MoveRotatingPosition(o->Position, o->Angle, o->Direction, 1, TurnRate, FPS_ANIMATION_FACTOR);
    VectorCopy(o->Position, o->EyeLeft);
    Vector(Luminosity, Luminosity * 0.4f, Luminosity * 0.2f, light);
    AddTerrainLight(o->Position[0], o->Position[1], light, 2, PrimaryTerrainLight);
    CheckClientArrow(o);
    return true;
}

// MODEL_COMBO
bool MoveBehavior::Move_MODEL_COMBO(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType == 0)
    {
        const float active = std::clamp(o->LifeTime - 4.f, 0.f, FPS_ANIMATION_FACTOR);
        Core::Time::Advance(o->Scale, o->Gravity, 0.1f, active);
        o->BlendMeshLight *= pow(1.0f / (1.4f), FPS_ANIMATION_FACTOR);
    }
    return true;
}

// MODEL_WAVES
bool MoveBehavior::Move_MODEL_WAVES(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType == 0)
    {
        const float active = std::clamp(o->LifeTime - 4.f, 0.f, FPS_ANIMATION_FACTOR);
        Core::Time::Advance(o->Scale, o->Gravity, 0.1f, active);
        o->BlendMeshLight *= pow(1.0f / (1.4f), FPS_ANIMATION_FACTOR);
    }
    else if (o->SubType == 1)
    {
        Core::Time::Advance(o->Scale, o->Gravity, 0.07f, FPS_ANIMATION_FACTOR);
        if (o->Scale > 2.f)
            o->Scale = 2.f;
        o->BlendMeshLight *= pow(1.0f / (1.5f), FPS_ANIMATION_FACTOR);
    }
    else if (o->SubType == 2)
    {
        Core::Time::Advance(o->Scale, o->Gravity, 0.01f, FPS_ANIMATION_FACTOR);
        if (o->Scale > 1.5f)
            o->Scale = 1.5f;
        o->BlendMeshLight *= pow(1.0f / (1.5f), FPS_ANIMATION_FACTOR);
    }
    else if (o->SubType == 3)
    {
        Core::Time::Advance(o->Scale, o->Gravity, 0.005f, FPS_ANIMATION_FACTOR);
        if (o->Scale > 1.5f)
            o->Scale = 1.5f;
        o->BlendMeshLight *= pow(1.0f / (1.3f), FPS_ANIMATION_FACTOR);
        o->Angle[1] += (45.f) * FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 4)
    {
        Core::Time::Advance(o->Scale, o->Gravity, 0.002f, FPS_ANIMATION_FACTOR);
        if (o->Scale > 2.5f)
            o->Scale = 2.5f;
        o->BlendMeshLight *= pow(1.0f / (1.2f), FPS_ANIMATION_FACTOR);
        o->Angle[1] += (45.f) * FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 5)
    {
        Core::Time::Advance(o->Scale, o->Gravity, 0.015f, FPS_ANIMATION_FACTOR);
        if (o->Scale > 2.5f)
            o->Scale = 2.5f;
        o->BlendMeshLight *= pow(1.0f / (1.3f), FPS_ANIMATION_FACTOR);
        o->Angle[1] += (45.f) * FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 6)
    {
        Core::Time::Advance(o->Scale, o->Gravity, 0.015f, FPS_ANIMATION_FACTOR);
        if (o->Scale > 2.5f)
            o->Scale = 2.5f;
        o->BlendMeshLight *= pow(1.0f / (1.3f), FPS_ANIMATION_FACTOR);
        o->Angle[1] += (45.f) * FPS_ANIMATION_FACTOR;
    }
    return true;
}

// MODEL_AIR_FORCE
bool MoveBehavior::Move_MODEL_AIR_FORCE(OBJECT *o, int index, float Luminosity)
{
    vec3_t Position;
    float Matrix[3][4];
    vec3_t p;
    if (o->Owner == NULL)
    {
        RetireEffect(o);
        return false;
    }
    if (o->SubType == 0)
    {
        o->BlendMeshLight *= pow(1.0f / (1.5f), FPS_ANIMATION_FACTOR);
        o->Scale += (0.2f) * FPS_ANIMATION_FACTOR;
    }

    if (o->SubType == 1)
    {
        o->BlendMeshLight = o->LifeTime / 10.f;

        VectorCopy(o->Owner->Position, o->Position);
        VectorCopy(o->Owner->Angle, o->Angle);
        Vector(0.f, -70.f, 0.f, p);
        AngleMatrix(o->Angle, Matrix);
        VectorRotate(p, Matrix, Position);
        Position[2] += 100.f;
        VectorAdd(o->Position, Position, o->Position);
    }
    return true;
}

// MODEL_PIERCING2
bool MoveBehavior::Move_MODEL_PIERCING2(OBJECT *o, int index, float Luminosity)
{
    constexpr float Deceleration = 12.f;
    float remaining = FPS_ANIMATION_FACTOR;
    while (remaining > 0.f)
    {
        if (o->EffectMotionFrames <= 0.f)
        {
            vec3_t direction;
            VectorCopy(o->Direction, direction);
            direction[1] = (std::min)(0.f, direction[1] + Deceleration);
            float matrix[3][4];
            AngleMatrix(o->Angle, matrix);
            VectorRotate(direction, matrix, o->EffectMotionVelocity);
            o->EffectMotionFrames = 1.f;
        }
        const float step = (std::min)(remaining, o->EffectMotionFrames);
        VectorAddScaled(o->Position, o->EffectMotionVelocity, o->Position, step);
        o->MotionTrace.Advance(step, o->Position);
        o->Direction[1] = (std::min)(0.f, o->Direction[1] + Deceleration * step);
        o->EffectMotionFrames -= step;
        remaining -= step;
    }

    const float endLife = o->SubType == 1 || o->SubType == 2 ? 1.f : 5.f;
    const float active = (std::min)(FPS_ANIMATION_FACTOR, (std::max)(0.f, o->LifeTime - endLife));
    o->BlendMeshLight *= std::pow(1.f / 1.6f, active);
    for (float sample = Core::Time::ReferenceSample(o->LifeTime);
         sample > endLife && Core::Time::Reaches(o->LifeTime, active, sample); sample -= 1.f)
    {
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR -
                                                             (std::max)(0.f, o->LifeTime - sample));
        vec3_t position;
        o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
        CreateEffect(MODEL_WAVES, position, o->Angle, o->Light, 2, nullptr, int(sample));
    }
    return true;
}

// MODEL_DEASULER
bool MoveBehavior::Move_MODEL_DEASULER(OBJECT *o, int index, float Luminosity)
{
    {
        vec3_t v3RotateAngleRelative;
        o->Visible = true;

        float fCurrentRate = 1.0f - ((float)o->LifeTime / (float)o->ExtState);

        if (o->m_Interpolates.m_vecInterpolatesAngle.size() > 0)
        {
            o->m_Interpolates.GetAngleCurrent(v3RotateAngleRelative, fCurrentRate);

            o->Angle[0] = v3RotateAngleRelative[0] + o->HeadAngle[0];
            o->Angle[1] = v3RotateAngleRelative[1] + o->HeadAngle[1];
            o->Angle[2] = v3RotateAngleRelative[2] + o->HeadAngle[2];
        }

        if (o->m_Interpolates.m_vecInterpolatesPos.size() > 0)
        {
            o->m_Interpolates.GetPosCurrent(o->Position, fCurrentRate);
        }

        if (o->m_Interpolates.m_vecInterpolatesScale.size() > 0)
        {
            o->m_Interpolates.GetScaleCurrent(o->Scale, fCurrentRate);
        }

        if (o->m_Interpolates.m_vecInterpolatesScale.size() > 0)
        {
            o->m_Interpolates.GetAlphaCurrent(o->Alpha, fCurrentRate);
        }

        float fRateBlurStart, fRateBlurEnd, fRateShadowStart, fRateShadowEnd, fRateJointStart,
            fRateJointEnd;
        fRateBlurStart = fRateBlurEnd = 0.0f;
        fRateShadowStart = fRateShadowEnd = 0.0f;
        fRateJointStart = fRateJointEnd = 0.0f;
        fRateBlurStart = 0.0f;
        fRateBlurEnd = 0.90f;

        int iTYPESWORDFORCE = 0;  // 1: FORCE OF SWORD
        int iTYPESWORDSHADOW = 0; // 1: SHADOW SWORD

        iTYPESWORDFORCE = 1;
        iTYPESWORDSHADOW = 0;

        if (iTYPESWORDFORCE == 1)
        {
            if (fCurrentRate > fRateBlurStart && fCurrentRate < fRateBlurEnd)
            {
                BMD *b = &Models[o->Type];
                vec3_t vLightBlur;
                Vector(1.0f, 1.0f, 1.0f, vLightBlur);
                float fPreRate = 1.0f - (float)((o->LifeTime) + 1) / (float)(o->ExtState);
                SETLIMITS(fPreRate, 1.0f, 0.0f);
                if (fPreRate < fCurrentRate)
                {
                    for (int Loop_bk = 0; Loop_bk < 2; Loop_bk++)
                    {
                        float fStartRate, fEndRate;

                        fStartRate = 1.0f - (float)((o->LifeTime) + 2) / (float)(o->ExtState);
                        fEndRate = 1.0f - (float)((o->LifeTime) + 1) / (float)(o->ExtState);

                        SETLIMITS(fStartRate, 1.0f, 0.0f);
                        SETLIMITS(fEndRate, 1.0f, 0.0f);

                        vec3_t *arrEachBonePos;
                        arrEachBonePos = new vec3_t[b->NumBones];

                        vec3_t v3CurBlurAngle, v3CurBlurPos;
                        int iAccess = 20;
                        int iBone01, iBone02;
                        int iBlurIdentity, iTypeBlur;
                        float fUnit;
                        float fCurrentRateUnit = fStartRate;
                        float fScale = o->Scale;

                        if (Loop_bk == 0)
                        {
                            iBone01 = 4;
                            iBone02 = 2;
                            iBlurIdentity = 113;
                        }
                        else
                        {
                            iBone01 = 5;
                            iBone02 = 1;
                            iBlurIdentity = 114;
                        }
                        iTypeBlur = 10;

                        fUnit = (fEndRate - fStartRate) / (float)iAccess;

                        for (int i = 0; i < iAccess; i++)
                        {
                            fCurrentRateUnit += fUnit;

                            o->m_Interpolates.GetAngleCurrent(v3CurBlurAngle, fCurrentRateUnit);
                            o->m_Interpolates.GetPosCurrent(v3CurBlurPos, fCurrentRateUnit);

                            VectorAdd(v3CurBlurAngle, o->HeadAngle, v3CurBlurAngle);

                            b->AnimationTransformOnlySelf(arrEachBonePos, v3CurBlurAngle,
                                                          v3CurBlurPos, fScale);

                            CreateObjectBlur(o, arrEachBonePos[iBone01], arrEachBonePos[iBone02],
                                             vLightBlur, iTypeBlur, false, iBlurIdentity, 20);

                            for (auto birthTime :
                                 sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.0))
                            {
                                vec3_t vAngle, vRandomDir, vRandomDirPosition,
                                    vResultRandomPosition;
                                vec34_t matRandomRotation;
                                vec3_t vPosition;

                                VectorCopy(arrEachBonePos[1], vPosition);

                                float fRandDistance = (float)(WorldRandom() % 100) + 100;
                                Vector(0.0f, fRandDistance, 0.0f, vRandomDir);

                                CreateParticle(BITMAP_FIRE + 2, vPosition, o->Angle, o->Light, 17,
                                               1.35f);

                                Vector((float)(WorldRandom() % 360), 0.f,
                                       (float)(WorldRandom() % 360), vAngle);
                                AngleMatrix(vAngle, matRandomRotation);
                                VectorRotate(vRandomDir, matRandomRotation, vRandomDirPosition);
                                VectorAdd(vPosition, vRandomDirPosition, vResultRandomPosition);
                                CreateJoint(BITMAP_JOINT_THUNDER, vResultRandomPosition, vPosition,
                                            vAngle, 3, NULL, 10.f, 10, 10);
                            }
                        }
                        delete[] arrEachBonePos;
                    }
                } // if( fPreRate < fCurrentRate )
            }
        }
    }
    return true;
}

// MODEL_DEATH_SPI_SKILL
bool MoveBehavior::Move_MODEL_DEATH_SPI_SKILL(OBJECT *o, int index, float Luminosity)
{
    vec3_t Position;
    float Matrix[3][4];
    vec3_t p;
    {
        if (o->SubType == 0)
        {
            if (o->Owner != NULL)
            {
                VectorCopy(o->Owner->Position, p);
                VectorAdd(p, o->StartPosition, p);

                AdvanceHomingSpiral(
                    *o, p, *sessionKeeper_.Random(), FPS_ANIMATION_FACTOR, WorldTime,
                    [&](float offset, bool late) {
                        auto birthTime =
                            sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - offset);
                        CreateEffect(MODEL_TAIL, o->Position, o->Angle, o->Light, 0, o);
                        if (late)
                            CreateEffect(BITMAP_MAGIC + 1, o->Position, o->Angle, o->Light, 1, o);
                    });
                PlayBuffer(SOUND_ATTACK_FIRE_BUST_EXP);
            }
        }
        else if (o->SubType == 1)
        {
            if (o->LifeTime < 5)
            {
                o->Alpha *= pow(1.0f / (1.3f), FPS_ANIMATION_FACTOR);
            }
        }
    }
    return true;
}

// MODEL_PIER_PART
bool MoveBehavior::Move_MODEL_PIER_PART(OBJECT *o, int index, float Luminosity)
{
    vec3_t Angle;
    vec3_t Position;
    float Matrix[3][4];
    vec3_t p;
    if (o->SubType == 0)
    {
        if (o->Owner != NULL)
        {
            VectorCopy(o->Owner->Position, p);
            VectorAdd(p, o->StartPosition, p);

            AdvanceHomingSpiral(
                *o, p, *sessionKeeper_.Random(), FPS_ANIMATION_FACTOR, WorldTime,
                [&](float offset, bool) {
                    auto birthTime =
                        sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - offset);
                    CreateEffect(MODEL_PIER_PART, o->Position, o->Angle, o->Light, 1, o);
                });
            PlayBuffer(SOUND_ATTACK_FIRE_BUST_EXP);
        }
    }
    else if (o->SubType == 2)
    {
        if (o->Owner != NULL)
        {
            VectorCopy(o->Owner->Position, p);

            AdvanceHomingEffect(*o, p, 2.4f, FPS_ANIMATION_FACTOR, WorldTime);

            for (float life = Core::Time::ReferenceSample(o->LifeTime / 3.f) * 3.f;
                 life > 0.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life);
                 life -= 3.f)
            {
                auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                    FPS_ANIMATION_FACTOR - std::max(0.f, o->LifeTime - life));
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, Position);
                Vector(-90.f, 0.f,
                       o->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), o->Angle[2]),
                       Angle);
                CreateJoint(BITMAP_JOINT_FORCE, Position, Position, Angle, 2, NULL, 150.f);
            }
        }
    }
    else if (o->SubType == 1)
    {
        if (o->LifeTime < 5)
        {
            o->Alpha *= pow(1.0f / (1.3f), FPS_ANIMATION_FACTOR);
        }
    }
    return true;
}

// BITMAP_FLARE_FORCE
bool MoveBehavior::Move_BITMAP_FLARE_FORCE(OBJECT *o, int index, float Luminosity)
{
    return true;
}

// MODEL_DARKLORD_SKILL
bool MoveBehavior::Move_MODEL_DARKLORD_SKILL(OBJECT *o, int index, float Luminosity)
{
    return true;
}

// MODEL_GROUND_STONE, MODEL_GROUND_STONE2
bool MoveBehavior::Move_MODEL_GROUND_STONE(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Position;
    if (o->LifeTime > 32 && o->LifeTime < 37)
    {
        Position[0] = o->Position[0] + 60;
        Position[1] = o->Position[1] - 60;
        Position[2] = o->Position[2] + 50.f;

        Vector(1.f, 0.8f, 0.6f, Light);
        CreateParticleFpsChecked(BITMAP_SMOKE, Position, o->Angle, Light, 11, 2.f, o);
        CreateEffectFpsChecked(MODEL_STONE1 + WorldRandom() % 2, Position, o->Angle, o->Light, 10);
    }
    //        o->BlendMeshTexCoordU = -(int)WorldTime%2000 * 0.0005f;

    if (o->LifeTime < 8)
    {
        o->Alpha *= pow(1.0f / (1.3f), FPS_ANIMATION_FACTOR);
        o->BlendMeshLight *= pow(1.0f / (2.f), FPS_ANIMATION_FACTOR);
    }

    Vector(0.79f, 0.72f, 0.49f, Light);
    AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
    return true;
}

// BITMAP_TWLIGHT
bool MoveBehavior::Move_BITMAP_TWLIGHT(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->SubType == 0)
        {
            o->Scale -= (0.1f) * FPS_ANIMATION_FACTOR;
            o->Angle[2] += (10.f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 1)
        {
            o->Scale -= (0.1f) * FPS_ANIMATION_FACTOR;
            o->Angle[2] += (5.f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 2)
        {
            o->Scale -= (0.1f) * FPS_ANIMATION_FACTOR;
            o->Angle[2] += (15.f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 3)
        {
            VectorCopy(o->Owner->Position, o->Position);
            o->Scale -= (0.15f) * FPS_ANIMATION_FACTOR;
            o->Angle[2] += (10.f) * FPS_ANIMATION_FACTOR;
            if (o->LifeTime >= 20)
            {
                o->Alpha += 0.1f * FPS_ANIMATION_FACTOR;
                o->PKKey += FPS_ANIMATION_FACTOR;
                o->Light[0] = o->EyeRight[0] * (o->PKKey * 0.1f);
                o->Light[1] = o->EyeRight[1] * (o->PKKey * 0.1f);
                o->Light[2] = o->EyeRight[2] * (o->PKKey * 0.1f);
            }
            else if (o->LifeTime <= 10)
            {
                o->PKKey -= FPS_ANIMATION_FACTOR;
                o->Alpha -= 0.1f * FPS_ANIMATION_FACTOR;
                o->Light[0] = o->EyeRight[0] * (o->PKKey * 0.1f);
                o->Light[1] = o->EyeRight[1] * (o->PKKey * 0.1f);
                o->Light[2] = o->EyeRight[2] * (o->PKKey * 0.1f);
            }
        }
    }
    return true;
}

// BITMAP_SHOCK_WAVE
bool MoveBehavior::Move_BITMAP_SHOCK_WAVE(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType == 0)
    {
        o->Scale -= (1.f) * FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 1)
    {
        AdvanceShockWaveDrift(*o, *sessionKeeper_.Random(), FPS_ANIMATION_FACTOR);
    }
    else if (o->SubType == 2)
    {
        o->Scale += ((WorldRandom() % 5) / 40.f) * FPS_ANIMATION_FACTOR;

        o->Position[0] += (WorldRandom() % 8 - 4.f) * FPS_ANIMATION_FACTOR;
        o->Position[1] += (WorldRandom() % 8 - 4.f) * FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 3)
    {
        o->Scale += (2.f) * FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 4)
    {
        o->Scale += (0.3f) * FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 5)
    {
        o->Scale -= (0.4f) * FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 6)
    {
        for (float life = Core::Time::ReferenceSample(o->LifeTime / 8.f) * 8.f;
             life > 0.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life);
             life -= 8.f)
        {
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR -
                                                                 std::max(0.f, o->LifeTime - life));
            CreateEffect(BITMAP_SHOCK_WAVE, o->Position, o->Angle, o->Light, 5);
        }
        return true;
    }
    else if (o->SubType == 7)
    {
        o->Scale += (2.5f) * FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 8)
    {
        o->Scale += (1.f) * FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 9)
    {
        o->Scale += (0.8f) * FPS_ANIMATION_FACTOR;
        VectorCopy(o->Owner->Position, o->Position);
    }
    else if (o->SubType == 10)
    {
        o->Scale -= (0.02f) * FPS_ANIMATION_FACTOR;
        VectorCopy(o->Owner->Position, o->Position);
    }
    else if (o->SubType == 11)
    {
        AdvanceShockWaveDrift(*o, *sessionKeeper_.Random(), FPS_ANIMATION_FACTOR);
    }
    else if (o->SubType == 12)
    {
        const float active = std::clamp(o->LifeTime - 4.f, 0.f, FPS_ANIMATION_FACTOR);
        o->Scale += 0.25f * active * (o->LifeTime - 4.f - (active - 1.f) * 0.5f);
    }
    else if (o->SubType == 13)
    {
        o->Scale += (0.08f) * FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 14)
    {
        VectorCopy(o->Owner->Position, o->Position);
        o->Scale -= (0.15f) * FPS_ANIMATION_FACTOR;
        if (o->LifeTime >= 20)
        {
            o->Alpha += 0.1f * FPS_ANIMATION_FACTOR;
            o->PKKey += FPS_ANIMATION_FACTOR;
            o->Light[0] = o->EyeRight[0] * (o->PKKey * 0.1f);
            o->Light[1] = o->EyeRight[1] * (o->PKKey * 0.1f);
            o->Light[2] = o->EyeRight[2] * (o->PKKey * 0.1f);
        }
        else if (o->LifeTime <= 10)
        {
            o->PKKey -= FPS_ANIMATION_FACTOR;
            o->Alpha -= 0.1f * FPS_ANIMATION_FACTOR;
            o->Light[0] = o->EyeRight[0] * (o->PKKey * 0.1f);
            o->Light[1] = o->EyeRight[1] * (o->PKKey * 0.1f);
            o->Light[2] = o->EyeRight[2] * (o->PKKey * 0.1f);
        }
        return true;
    }
    if (o->Scale < 0)
    {
        o->Scale = 0;
    }
    if (o->SubType >= 0 && o->SubType <= 3)
    {
        if (o->LifeTime <= 20)
        {
            Luminosity = o->LifeTime / 20.f;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
        }
        else
        {
            Luminosity = (40 - o->LifeTime) / 20.f;
            Vector(Luminosity, Luminosity, Luminosity, o->Light);
        }
    }
    else
    {
        if (o->LifeTime < 6)
        {
            o->Light[0] *= pow(1.0f / (1.3f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= pow(1.0f / (1.3f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= pow(1.0f / (1.3f), FPS_ANIMATION_FACTOR);
        }
    }
    return true;
}

// BITMAP_DAMAGE_01_MONO
bool MoveBehavior::Move_BITMAP_DAMAGE_01_MONO(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType == 0)
    {
        o->Scale += (5.f) * FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 1)
    {
        o->Scale += (0.5f) * FPS_ANIMATION_FACTOR;
        if (o->Scale > 3.5f)
        {
            //o->Scale = 6.0f;

            o->Light[0] *= pow(0.5f, FPS_ANIMATION_FACTOR);
            o->Light[1] *= pow(0.5f, FPS_ANIMATION_FACTOR);
            o->Light[2] *= pow(0.5f, FPS_ANIMATION_FACTOR);
        }
    }
    return true;
}

// BITMAP_FLARE
bool MoveBehavior::Move_BITMAP_FLARE(OBJECT *o, int index, float Luminosity)
{
    vec3_t light{1.f, 1.f, 1.f};
    if (o->SubType >= 1 && o->SubType <= 3)
    {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            if (o->SubType == 1)
                CreateParticle(BITMAP_FLARE, o->Position, o->Angle, light, 11, 1.f);
            else if (o->SubType == 2)
                CreateParticle(BITMAP_FLARE_BLUE, o->Position, o->Angle, light, 1, 1.f);
            else
            {
                Vector(0.9f, 0.4f, 0.1f, light);
                const float scale = 3.5f + (WorldRandom() % 20 - 10) * 0.1f;
                CreateParticle(BITMAP_LIGHT, o->Position, o->Angle, light, 5, scale);
            }
        }
        return true;
    }
    constexpr float FlareInterval = 2.f;
    for (float life = Core::Time::ReferenceSample(o->LifeTime / FlareInterval) * FlareInterval;
         life > 0.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life);
         life -= FlareInterval)
    {
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR -
                                                             std::max(0.f, o->LifeTime - life));
        vec3_t position{o->Position[0], o->Position[1], o->Position[2] + 100.f};
        CreateJoint(BITMAP_FLARE_BLUE, position, position, o->Angle, 14, o, 40.f);
        CreateJoint(BITMAP_FLARE_BLUE, position, position, o->Angle, 15, o, 40.f);
        if (WorldRandom() % 2 == 0 && life > 5.f && life < 15.f)
        {
            Vector(o->Position[0] - 200.f, o->Position[1], o->Position[2] + 700.f, position);
            CreateJoint(BITMAP_JOINT_THUNDER, position, o->Position, o->Angle, 0, o, 20.f);
        }
    }
    return true;
}

// MODEL_CUNDUN_DRAGON_HEAD
bool MoveBehavior::Move_MODEL_CUNDUN_DRAGON_HEAD(OBJECT *o, int index, float Luminosity)
{
    vec3_t Angle;
    vec3_t Position;
    AdvanceRandomRotatingEffect(*o, *sessionKeeper_.Random(), FPS_ANIMATION_FACTOR, 2, 10, 10.f);

    if (o->Position[2] > 300 && o->Position[2] < 600)
    {
        vec3_t Angle;
        Angle[0] = Angle[1] = 0;
        Position[0] = o->Position[0];
        Position[1] = o->Position[1];
        Position[2] = 350;
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
        {
            Angle[2] = (float)(WorldRandom() % 24 * 30);
            CreateJoint(BITMAP_JOINT_SPIRIT2, Position, Position, Angle, 14, NULL, 100.f, 0, 0);
        }
    }
    return true;
}

// MODEL_CUNDUN_PHOENIX
bool MoveBehavior::Move_MODEL_CUNDUN_PHOENIX(OBJECT *o, int index, float Luminosity)
{
    if (o->LifeTime < 5)
    {
        o->Alpha = o->LifeTime * 0.2f;
    }
    return true;
}

// MODEL_CUNDUN_GHOST
bool MoveBehavior::Move_MODEL_CUNDUN_GHOST(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    if (o->Owner != NULL)
    {
        if (o->Owner->PKKey == 0)
        {
            o->AnimationFrame = 0;
        }
        else
        {
            o->Owner = NULL;
        }
    }
    else
    {
        // These conditions originally read the animation before its reference
        // advance. Keep that one-reference phase while consuming crossed time.
        const float approaching =
            o->Velocity > 0.f
                ? (std::min)(FPS_ANIMATION_FACTOR,
                             (std::max)(0.f, (6.f + o->Velocity - o->AnimationFrame) / o->Velocity))
                : (o->AnimationFrame <= 6.f ? FPS_ANIMATION_FACTOR : 0.f);
        if (approaching > 0.f)
        {
            AdvanceApproachingGhost(*o, Hero->Object, approaching, FPS_ANIMATION_FACTOR, WorldTime);
            Vector(1.f, 1.f, 1.f, Light);
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(approaching, approaching))
            {
                const float fraction = birthTime.FrameFraction();
                o->MotionTrace.Sample(WorldTime, fraction, o->Position, Position);
                Position[0] += WorldRandom() % 1200 - 600;
                Position[1] += WorldRandom() % 1200 - 600;
                Vector(0.f, 0.f, WorldRandom() % 10 * 20.f, Angle);
                CreateEffect(MODEL_FIRE, Position, Angle, Light, 0, NULL, 0);
            }
            o->Scale += 0.02f * approaching;
            EarthQuake = (float)(WorldRandom() % 8 - 8) * 0.1f;
        }
        const float rising = FPS_ANIMATION_FACTOR - approaching;
        if (rising > 0.f)
        {
            float risingSpeed = (o->PKKey + 1.f) * 0.8f;
            Core::Time::Advance(o->Position[2], risingSpeed, 0.8f, rising);
            o->PKKey += rising;
            o->MotionTrace.Advance(rising, o->Position);
            Vector(1.f, 1.f, 1.f, Light);
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(rising, rising, approaching))
            {
                const float fraction = birthTime.FrameFraction();
                o->MotionTrace.Sample(WorldTime, fraction, o->Position, Position);
                Position[0] += WorldRandom() % 120 - 60;
                Position[1] += WorldRandom() % 120 - 60;
                Position[2] += WorldRandom() % 60;
                for (int smoke = 0; smoke < 3; ++smoke)
                    CreateParticle(BITMAP_SMOKE, Position, o->Angle, Light, 20, 10.f);
            }
        }
    }
    const float beforeFade =
        o->Velocity > 0.f
            ? (std::min)(FPS_ANIMATION_FACTOR,
                         (std::max)(0.f, (3.f + o->Velocity - o->AnimationFrame) / o->Velocity))
            : (o->AnimationFrame <= 3.f ? FPS_ANIMATION_FACTOR : 0.f);
    if (o->Alpha > 0.2f)
        o->Alpha = (std::max)(0.2f, o->Alpha - 0.02f * (FPS_ANIMATION_FACTOR - beforeFade));
    PrepareWorldObjectPose(*o);
    vec3_t origin{2.f, 10.f, 0.f};
    Models[o->Type].TransformPosition(BoneTransform[6], origin, Position, false);
    Vector(1.f, 0.2f, 0.f, Light);
    CreateParticleFpsChecked(BITMAP_SMOKE, Position, o->Angle, Light, 17, 3.f);
    return true;
}

// MODEL_CUNDUN_SKILL
bool MoveBehavior::Move_MODEL_CUNDUN_SKILL(OBJECT *o, int index, float Luminosity)
{
    vec3_t Angle;
    if (o->SubType == 0)
    {
        if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 30))
        {
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 30.f));
            for (int i = 0; i < 10; ++i)
            {
                vec3_t Angle;
                Vector(0, 0, o->Angle[2] + 320 + i * 8, Angle);
                CreateEffect(MODEL_CUNDUN_PHOENIX, o->Position, Angle, o->Light, 0);
            }
        }
    }
    else if (o->SubType == 1)
    {
        vec3_t Angle;
        Angle[0] = 0;
        Angle[1] = 0;
        if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 30))
        {
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 30.f));
            for (int i = 0; i < 20; ++i)
            {
                Angle[2] = (float)(o->PKKey * 30);
                CreateEffect(MODEL_CUNDUN_DRAGON_HEAD, o->Position, Angle, o->Light);
            }
        }
        const float offset = (std::min)(FPS_ANIMATION_FACTOR, (std::max)(0.f, 6.f - o->PKKey));
        const float duration =
            (std::min)(FPS_ANIMATION_FACTOR - offset, (std::max)(0.f, 10.f - o->PKKey - offset));
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(duration, duration, offset))
        {
            Angle[2] = (o->PKKey + FPS_ANIMATION_FACTOR - birth.RemainingFrames()) * 30.f;
            CreateEffect(MODEL_CUNDUN_DRAGON_HEAD, o->Position, Angle, o->Light);
        }
        o->PKKey += FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 2)
    {
        if (o->PKKey == 0 && o->LifeTime < 10)
        {
            o->PKKey = 1;
        }
    }
    return true;
}

// MODEL_BATTLE_GUARD2
bool MoveBehavior::Move_MODEL_BATTLE_GUARD2(OBJECT *o, int index, float Luminosity)
{
    vec3_t Position;
    if (o->SubType == 0)
    {
        for (float sample = Core::Time::ReferenceSample(o->LifeTime / 5.f) * 5.f;
             sample >= 0.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, sample);
             sample -= 5.f)
        {
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - sample));
            VectorCopy(o->Position, Position);

            Position[0] += WorldRandom() % 100 - 50.f;
            Position[1] -= WorldRandom() % 30 + 15.f;
            CreateEffect(MODEL_FLY_BIG_STONE1, Position, o->Angle, o->Light, 2);
        }
    }
    if (o->LifeTime < 5)
    {
        o->Alpha *= pow(1.0f / (1.3f), FPS_ANIMATION_FACTOR);
    }
    return true;
}

// MODEL_ARROW_TANKER_HIT
bool MoveBehavior::Move_MODEL_ARROW_TANKER_HIT(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    float Height;
    {
        float Height = RequestTerrainHeight(o->Position[0], o->Position[1]);

        if (o->Position[2] < Height + 80.0f)
        {
            if (o->Alpha <= 0.1f)
                o->LifeTime = 0;
            o->Alpha *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);

            Vector(0.0f, 0.0f, -30.0f, o->Direction);
            Vector(1.f, 0.6f, 0.2f, Light);
            CreateParticleFpsChecked(BITMAP_ADV_SMOKE + 1, o->Position, o->Angle, Light);

            if (o->HiddenMesh != 99)
            {
                CreateInferno(o->Position, 1);
                VectorCopy(o->Position, o->StartPosition);
                CreateEffect(BITMAP_CRATER, o->Position, o->Angle, o->Light);
            }

            EarthQuake = (float)(WorldRandom() % 8 - 8) * 0.1f;
            EarthQuake *= pow(1.0f / (2.f), FPS_ANIMATION_FACTOR);

            o->HiddenMesh = 99;
        }

        if (o->HiddenMesh == 99)
        {
            o->Position[0] += ((float)(WorldRandom() % 100 - 50));
            o->Position[1] += ((float)(WorldRandom() % 100 - 50));
            o->Position[2] += ((float)(WorldRandom() % 100 - 50));
            o->Scale = 2.2f;
        }

        Vector(1.f, 0.4f, 0.f, o->Light);
        if (o->Visible)
            EmitTankerTrail(*o);

        if (o->HiddenMesh == 99)
        {
            o->Scale = 1.0f;
            VectorCopy(o->StartPosition, o->Position);
        }

        Vector(1.0f, 0.0f, 0.0f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 4, PrimaryTerrainLight);
    }
    return true;
}

void MoveBehavior::ApplyCatapultGroundImpact(OBJECT &object, bool collideWithObjects)
{
    if (object.HiddenMesh == 99)
        return;
    constexpr float ImpactRange = 350.f;
    constexpr float ObstacleHeight = 250.f;
    if (object.Visible)
    {
        SessionRandom::PresentationScope presentation(*sessionKeeper_.Random());
        CreateEffect(BITMAP_CRATER, object.Position, object.Angle, object.Light);
        PlayBuffer(SOUND_BC_CATAPULT_HIT, &object);
    }
    if (collideWithObjects)
        CollisionEffectToObject(&object, ImpactRange, ObstacleHeight, true);
    if (object.SubType == 88 && object.Owner == nullptr)
        CollisionHeroCharacter(object.Position, ImpactRange, PLAYER_HIGH_SHOCK);
    else if (object.Visible && (object.SubType == 99 || object.SubType == 88))
    {
        SessionRandom::PresentationScope presentation(*sessionKeeper_.Random());
        vec3_t light{1.f, 0.3f, 0.1f};
        CreateEffect(BITMAP_SHOCK_WAVE, object.Position, object.Angle, light, 7);
    }
}

void MoveBehavior::ApplyCatapultTerrainImpact(OBJECT &object, float height)
{
    constexpr float DeformableHeight = 150.f;
    constexpr int BattlefieldEndRow = 104;
    constexpr float CraterDepth = -30.f;
    constexpr int CraterRadius = 2;
    if (height <= DeformableHeight)
        return;
    SessionRandom::PresentationScope presentation(*sessionKeeper_.Random());
    EarthQuake = static_cast<float>(WorldRandom() % 8 - 8) * 0.1f;
    if (static_cast<int>(object.Position[1] / TERRAIN_SCALE) < BattlefieldEndRow)
    {
        sessionKeeper_.WorldUnit()->ChangeTerrainHeight(object.Position[0], object.Position[1],
                                                        CraterDepth, CraterRadius);
        EarthQuake *= pow(0.5f, FPS_ANIMATION_FACTOR);
    }
}

void MoveBehavior::EmitTankerTrail(OBJECT &object)
{
    SessionRandom::PresentationScope presentation(*sessionKeeper_.Random());
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        vec3_t position, light{1.f, 1.f, 1.f};
        object.MotionTrace.Sample(WorldTime, birth.FrameFraction(), object.Position, position);
        CreateParticle(BITMAP_FIRE + 1, position, object.Angle, light, 8, object.Scale - 0.4f,
                       &object);
        CreateParticle(BITMAP_SMOKE, position, object.Angle, light, 38, object.Scale, &object);
        Vector(1.f, 0.4f, 0.f, light);
        CreateParticle(BITMAP_FIRE + 1, position, object.Angle, light, 9, object.Scale - 0.4f,
                       &object);
    }
}

// MODEL_FLY_BIG_STONE1
bool MoveBehavior::Move_MODEL_FLY_BIG_STONE1(OBJECT *o, int index, float Luminosity)
{
    float flightFrames = 0.f;
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Position;
    float Matrix[3][4];
    float Height;
    {
        if (o->SubType == 2)
        {
            AdvanceBouncingCatapult(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR,
                                    *sessionKeeper_.Random());

            if (o->LifeTime < 10)
            {
                o->Alpha *= pow(1.0f / (1.5f), FPS_ANIMATION_FACTOR);
            }

            for (float life = Core::Time::ReferenceSample(o->LifeTime / 3.f) * 3.f;
                 life > 0.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life);
                 life -= 3.f)
            {
                auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                    FPS_ANIMATION_FACTOR - std::max(0.f, o->LifeTime - life));
                Vector(1.f, 0.6f, 0.2f, Light);
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, Position);
                CreateParticle(BITMAP_ADV_SMOKE + 1, Position, o->Angle, Light, 1, 0.2f);
            }
        }
        else
        {
            if (o->SubType <= 1)
            {
                flightFrames =
                    AdvanceCatapultFlight(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR);

                if (o->Position[2] > 1000.f)
                {
                    // Keep physical altitude; visibility alone changes above this height.
                    o->HiddenMesh = -2;
                }
                else
                {
                    o->HiddenMesh = -1;
                    Vector(Luminosity * 1.f, Luminosity * 0.5f, Luminosity * 0.1f, Light);
                    CreateParticleFpsChecked(BITMAP_FIRE, o->Position, o->Angle, Light, 5, 2.f);
                }

                if (o->SubType == 0)
                {
                    o->Position[2] = o->StartPosition[2];
                }
                else if (o->Owner == (&Hero->Object))
                {
                    o->Position[2] = o->StartPosition[2];

                    g_pCatapultWindow->SetCameraPos(o->Position[0], o->Position[1],
                                                    (std::min)(800.f, o->Position[2]));
                }
            }

            if (o->SubType == 88 || o->SubType == 99)
            {
                if (o->SubType == 99 && o->Owner == (&Hero->Object))
                {
                    g_pCatapultWindow->SetCameraPos(o->Position[0], o->Position[1], o->Position[2]);
                }

                if (o->LifeTime > o->DamageTime)
                {
                    Vector(1.f, 0.6f, 0.2f, Light);
                    CreateParticleFpsChecked(BITMAP_ADV_SMOKE + 1, o->Position, o->Angle, Light);
                    CreateParticleFpsChecked(BITMAP_FIRE, o->Position, o->Angle, Light, 2, 2.f);

                    if (o->SubType == 88 && o->Owner != NULL)
                    {
                        CollisionHeroCharacter(o->Position, ((15 - o->LifeTime) * 20) + 350.f,
                                               PLAYER_HIGH_SHOCK);
                    }
                    else if (o->SubType == 99)
                    {
                        CollisionTempCharacter(o->Position, ((40 - o->LifeTime) * 7) + 350.f,
                                               PLAYER_HIGH_SHOCK);
                    }
                }
            }
            else
            {
                float Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
                if (o->LifeTime < 250 && o->Position[2] < Height + 300.f && o->SubType == 1 &&
                    o->Kind != 0 && o->Skill != 0)
                {
                    SocketClient->ToGameServer()->SendWeaponExplosionRequest(
                        MAKELONG(o->Skill, o->Kind));
                    o->Kind = 0;
                    o->Skill = 0;
                }

                if (o->Position[2] < Height || o->EffectResting)
                {
                    o->Position[2] = Height - 5.f;
                    if (o->SubType == 0)
                    {
                        o->LifeTime = 15.f + flightFrames;
                        o->DamageTime = 0;
                        o->SubType = 88;
                    }
                    else if (o->SubType == 1)
                    {
                        o->LifeTime = 40.f + flightFrames;
                        o->DamageTime = 25;
                        o->SubType = 99;
                    }
                    ApplyCatapultGroundImpact(*o, true);
                    o->HiddenMesh = 99;

                    ApplyCatapultTerrainImpact(*o, Height);
                }
                else
                {
                    bool collision;
                    if (o->SubType == 1)
                    {
                        collision = CollisionEffectToObject(o, 200.f, 250.f, false, true);
                    }
                    else
                    {
                        collision = CollisionEffectToObject(o, 200.f, 250.f, false);
                    }
                    if (collision && o->Visible)
                    {
                        PlayBuffer(SOUND_BC_CATAPULT_HIT, o);
                    }
                }
            }
            Vector(-0.5f, -0.5f, -0.5f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 4, PrimaryTerrainLight);
        }
    }
    return true;
}

// MODEL_FLY_BIG_STONE2
bool MoveBehavior::Move_MODEL_FLY_BIG_STONE2(OBJECT *o, int index, float Luminosity)
{
    float flightFrames = 0.f;
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    float Height;
    {
        if (o->SubType <= 1)
        {
            flightFrames =
                AdvanceCatapultFlight(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR);

            if (o->Position[2] > 1000.f)
            {
                // Keep physical altitude; visibility alone changes above this height.
                o->HiddenMesh = -2;
            }
            else
            {
                o->HiddenMesh = -1;
                Vector(1.f, 1.f, 1.f, Light);

                for (auto birthTime :
                     sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
                {
                    CreateParticle(BITMAP_ADV_SMOKE + 1, o->Position, o->Angle, Light, 1, 2.f);
                }
            }

            if (o->SubType == 0)
            {
                o->Position[2] = o->StartPosition[2];
            }
            else if (o->Owner == (&Hero->Object))
            {
                o->Position[2] = o->StartPosition[2];

                g_pCatapultWindow->SetCameraPos(o->Position[0], o->Position[1],
                                                (std::min)(800.f, o->Position[2]));
            }
        }

        Vector(-0.5f, -0.5f, -0.5f, Light);
        if (o->SubType == 88 || o->SubType == 99)
        {
            if (o->SubType == 99 && o->Owner == (&Hero->Object))
            {
                g_pCatapultWindow->SetCameraPos(o->Position[0], o->Position[1], o->Position[2]);
            }

            if (o->LifeTime > o->DamageTime)
            {
                CreateParticleFpsChecked(BITMAP_ADV_SMOKE + 1, o->Position, o->Angle, o->Light);

                if (o->SubType == 88 && o->Owner != NULL)
                {
                    CollisionHeroCharacter(o->Position, ((15 - o->LifeTime) * 20) + 350.f,
                                           PLAYER_HIGH_SHOCK);
                }
                else if (o->SubType == 99)
                {
                    CollisionTempCharacter(o->Position, ((40 - o->LifeTime) * 7) + 350.f,
                                           PLAYER_HIGH_SHOCK);
                }
            }
        }
        else
        {
            float Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
            if (o->Position[2] < Height + 300.f && o->SubType == 1 && o->Kind != 0 && o->Skill != 0)
            {
                SocketClient->ToGameServer()->SendWeaponExplosionRequest(
                    MAKELONG(o->Skill, o->Kind));
                o->Kind = 0;
                o->Skill = 0;
            }

            if (o->Position[2] < Height || o->EffectResting)
            {
                o->Position[2] = Height - 5.f;
                if (o->SubType == 0)
                {
                    o->LifeTime = 15.f + flightFrames;
                    o->DamageTime = 0;
                    o->SubType = 88;
                }
                else if (o->SubType == 1)
                {
                    o->LifeTime = 40.f + flightFrames;
                    o->DamageTime = 25;
                    o->SubType = 99;
                }
                ApplyCatapultGroundImpact(*o, false);
                o->HiddenMesh = 99;

                ApplyCatapultTerrainImpact(*o, Height);
            }
        }
        AddTerrainLight(o->Position[0], o->Position[1], Light, 4, PrimaryTerrainLight);
    }
    return true;
}

// MODEL_BIG_STONE_PART1, MODEL_BIG_STONE_PART2, MODEL_WALL_PART1, MODEL_WALL_PART2, MODEL_GOLEM_STONE
bool MoveBehavior::Move_MODEL_BIG_STONE_PART1(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    float Height;
    if (o->Type == MODEL_BIG_STONE_PART2 && o->SubType == 3)
    {
        constexpr float AccelerationRate = 0.3f;
        float animationAdvance = 0.f;
        float remaining = FPS_ANIMATION_FACTOR;
        while (remaining > 0.f && !o->EffectResting)
        {
            if (o->EffectMotionFrames <= 0.f)
            {
                vec3_t direction;
                VectorCopy(o->Direction, direction);
                direction[2] -=
                    o->Velocity; // The original movement uses direction after acceleration.
                float matrix[3][4];
                AngleMatrix(o->Angle, matrix);
                VectorRotate(direction, matrix, o->EffectMotionVelocity);
                o->EffectMotionFrames = 1.f;
            }
            const float duration = (std::min)(remaining, o->EffectMotionFrames);
            const auto contact = sessionKeeper_.WorldUnit()->FirstTerrainContact(
                o->Position, o->EffectMotionVelocity, 0.f, duration);
            const float step = contact ? contact->frames : duration;
            VectorAddScaled(o->Position, o->EffectMotionVelocity, o->Position, step);
            o->Direction[2] -= step * (o->Velocity + AccelerationRate * (step - 1.f) * 0.5f);
            animationAdvance += step * (o->Velocity + AccelerationRate * (step + 1.f) * 0.5f);
            o->Velocity += AccelerationRate * step;
            o->EffectMotionFrames -= step;
            remaining -= step;
            if (contact)
            {
                o->Position[2] = contact->height;
                o->EffectResting = true;
                Vector(0.f, 0.f, 0.f, o->Direction);
                Vector(0.f, 0.f, 0.f, o->Angle);
                o->HiddenMesh = 0;
            }
        }
        o->EffectAnimationAdvance = animationAdvance + o->Velocity * remaining;
        if (o->EffectResting)
        {
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.0))
                CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 24, 1.25f * o->Scale);
        }
        else
        {
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
                CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 24, 0.2f);
        }
        return true;
    }
    AdvanceBouncingDebris(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR,
                          DebrisMotion::Wall);
    Height = RequestTerrainHeight(o->Position[0], o->Position[1]);

    o->Alpha = o->LifeTime / 10.f;
    if (o->Type == MODEL_GOLEM_STONE)
    {
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.0))
        {
            CreateParticle(BITMAP_TRUE_FIRE, o->Position, o->Angle, Light, 5, 2.8f);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 21, 1.8f);
        }
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.0))
        {
            CreateParticle(BITMAP_SMOKE + 1, o->Position, o->Angle, Light);
        }
    }
    else if (o->Type == MODEL_BIG_STONE_PART1 && o->SubType == 2)
    {
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.0))
        {
            Vector(0.2f, 0.5f, 0.35f, Light);
            vec3_t smokePosition{o->Position[0], o->Position[1], Height};
            CreateParticle(BITMAP_SMOKE, smokePosition, o->Angle, Light, 11, 2.f);
        }
    }
    return true;
}

// MODEL_GATE_PART1, MODEL_GATE_PART2, MODEL_GATE_PART3
bool MoveBehavior::Move_MODEL_GATE_PART1(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Position;
    float Height;
    AdvanceBouncingDebris(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR,
                          DebrisMotion::Heavy);

    for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.0))
    {
        CreateParticle(BITMAP_SMOKE + 1, o->Position, o->Angle, Light);
    }
    return true;
}

// MODEL_AURORA
bool MoveBehavior::Move_MODEL_AURORA(OBJECT *o, int index, float Luminosity)
{
    if (o->Owner != NULL && o->Owner->Live == true)
    {
        if (IsBattleCastleStart() == false)
        {
            o->HiddenMesh = -2;
        }
        else
        {
            o->HiddenMesh = -1;
        }
        o->LifeTime = 2;
        o->BlendMeshLight = sinf(WorldTime * 0.001f) * 0.1f + 0.2f;
    }
    else
    {
        RetireEffect(o);
    }
    o->BlendMeshTexCoordU = WorldTime * 0.0005f;
    return true;
}

// MODEL_FENRIR_THUNDER
bool MoveBehavior::Move_MODEL_FENRIR_THUNDER(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType == 0)
    {
        if (o->Live)
        {
            if (o->m_iAnimation == 0)
            {
                o->Alpha += (0.3f) * FPS_ANIMATION_FACTOR;
                if (o->Alpha >= 1.0f)
                {
                    o->m_iAnimation = 1;
                    o->Alpha = 1.0f;
                }

                if (o->Alpha < 0.0f)
                {
                    o->Alpha = 1.0f;
                }
            }
            else if (o->m_iAnimation == 1)
            {
                o->Alpha -= (0.3f) * FPS_ANIMATION_FACTOR;
                if (o->Alpha <= 0.0f)
                {
                    o->Alpha = 0.0f;
                    RetireEffect(o);
                }
            }
            o->Angle[0] += (0.15f) * FPS_ANIMATION_FACTOR;
            o->Angle[1] += (0.15f) * FPS_ANIMATION_FACTOR;
            o->Angle[2] += (0.15f) * FPS_ANIMATION_FACTOR;
        }
    }
    else if (o->SubType == 1)
    {
        if (o->Live)
        {
            if (o->m_iAnimation == 0)
            {
                o->Alpha += (0.3f) * FPS_ANIMATION_FACTOR;
                if (o->Alpha >= 1.0f)
                {
                    o->m_iAnimation = 1;
                    o->Alpha = 1.0f;
                }

                if (o->Alpha < 0.0f)
                {
                    o->Alpha = 1.0f;
                }
            }
            else if (o->m_iAnimation == 1)
            {
                o->Alpha -= (0.3f) * FPS_ANIMATION_FACTOR;
                if (o->Alpha <= 0.0f)
                {
                    o->Alpha = 0.0f;
                    RetireEffect(o);
                }
            }
            o->Angle[0] += (0.15f) * FPS_ANIMATION_FACTOR;
            o->Angle[1] += (0.15f) * FPS_ANIMATION_FACTOR;
            o->Angle[2] += (0.15f) * FPS_ANIMATION_FACTOR;
        }
    }
    return true;
}

// MODEL_FALL_STONE_EFFECT
bool MoveBehavior::Move_MODEL_FALL_STONE_EFFECT(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->SubType == 0 || o->SubType == 1)
        {
            AdvanceEffectGravity(*o, -1.f, 0.1f, FPS_ANIMATION_FACTOR);
            o->Angle[0] += (0.5f) * FPS_ANIMATION_FACTOR;
            o->Angle[1] += (0.5f) * FPS_ANIMATION_FACTOR;
            o->Angle[2] += (0.5f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 2)
        {
            vec3_t velocity{0.f, 0.f, -o->Gravity + 0.05f};
            const auto contact = sessionKeeper_.WorldUnit()->FirstTerrainContact(
                o->Position, velocity, -0.1f, FPS_ANIMATION_FACTOR);
            AdvanceEffectGravity(*o, -1.f, 0.1f, contact ? contact->frames : FPS_ANIMATION_FACTOR);
            if (contact)
            {
                auto birth =
                    sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - contact->frames);
                o->Position[2] = contact->height;
                vec3_t vLight;
                Vector(0.5f, 0.5f, 0.5f, vLight);
                CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, vLight, 11,
                               (float)(WorldRandom() % 20 + 30) * 0.020f);

                int iRand = WorldRandom() % 2 + 2;
                for (int i = 0; i < iRand; ++i)
                {
                    float fScale = 0.03f + (WorldRandom() % 10) / 40.0f + o->Scale * 0.3f;
                    CreateEffect(MODEL_FALL_STONE_EFFECT, o->Position, o->Angle, o->Light, 3, NULL,
                                 -1, 0, 0, 0, fScale);
                }

                RetireEffect(o);
            }
        }
        else if (o->SubType == 3)
        {
            const auto motion = AdvanceHeadDebris(*o, *sessionKeeper_.WorldUnit(),
                                                  FPS_ANIMATION_FACTOR, 0.f, 1.f, 0.6f, 0.5f);
            const float spin =
                0.5f * FPS_ANIMATION_FACTOR * (o->LifeTime - (FPS_ANIMATION_FACTOR - 1.f) * 0.5f);
            o->Angle[0] += spin;
            o->Angle[1] += spin;
            o->Alpha -= 0.15f * motion.groundFrames;
            VectorScale(o->Light, powf(1.f / 1.08f, motion.groundFrames), o->Light);
        }
    }
    return true;
}

// MODEL_FENRIR_FOOT_THUNDER
bool MoveBehavior::Move_MODEL_FENRIR_FOOT_THUNDER(OBJECT *o, int index, float Luminosity)
{
    if (o->Live == true)
    {
        o->Angle[0] += (0.1f) * FPS_ANIMATION_FACTOR;

        if (o->SubType == 1)
        {
            o->Light[1] -= (0.05f) * FPS_ANIMATION_FACTOR;
            o->Light[2] -= (0.05f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 2)
        {
            o->Light[0] -= (0.05f) * FPS_ANIMATION_FACTOR;
            o->Light[1] -= (0.05f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 3)
        {
            o->Light[0] -= (0.05f) * FPS_ANIMATION_FACTOR;
            o->Light[2] -= (0.05f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 4)
        {
            o->Light[2] -= (0.05f) * FPS_ANIMATION_FACTOR;
        }
        o->Alpha -= (0.05f) * FPS_ANIMATION_FACTOR;
        if (o->Alpha <= 0.0f)
            RetireEffect(o);

        constexpr DWORD ThunderFrameMilliseconds = 200;
        const DWORD frames = (timeGetTime() - o->m_dwTime) / ThunderFrameMilliseconds;
        o->m_iAnimation += frames;
        o->m_dwTime += frames * ThunderFrameMilliseconds;
        if (o->m_iAnimation > 4)
            RetireEffect(o);
    }
    return true;
}

// MODEL_TWINTAIL_EFFECT
bool MoveBehavior::Move_MODEL_TWINTAIL_EFFECT(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->SubType == 0)
        {
            o->Alpha -= (0.01f) * FPS_ANIMATION_FACTOR;
            if (o->Alpha <= 0.0f)
            {
                RetireEffect(o);
            }

            o->Light[0] *= pow(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= pow(1.0f / (1.02f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= pow(1.0f / (1.02f), FPS_ANIMATION_FACTOR);

            if (timeGetTime() - o->m_dwTime > 1000)
            {
                if (o->m_iAnimation == 0)
                {
                    o->m_iAnimation = 1;
                }
                else
                {
                    o->m_iAnimation = 0;
                }

                o->m_dwTime = timeGetTime();
            }

            if (o->m_iAnimation == 0)
            {
                o->Scale -= (0.015f) * FPS_ANIMATION_FACTOR;
                if (o->Scale <= 0.0f)
                {
                    o->Scale = 0.0f;
                }
            }
            else
            {
                o->Scale += (0.02f) * FPS_ANIMATION_FACTOR;
                if (o->Scale >= 1.2f)
                {
                    o->Scale = 1.2f;
                }
            }
        }
        else if (o->SubType == 1 || o->SubType == 2)
        {
            o->Alpha -= (0.01f) * FPS_ANIMATION_FACTOR;
            if (o->Alpha <= 0.0f)
            {
                RetireEffect(o);
            }

            o->Light[0] *= pow(1.0f / (1.04f), FPS_ANIMATION_FACTOR);
            o->Light[1] *= pow(1.0f / (1.04f), FPS_ANIMATION_FACTOR);
            o->Light[2] *= pow(1.0f / (1.04f), FPS_ANIMATION_FACTOR);

            if (o->SubType == 1)
                o->Angle[0] = -(WorldTime * 0.3f);
            else if (o->SubType == 2)
                o->Angle[0] = -(WorldTime * 0.1f);

            o->Scale -= (0.02f) * FPS_ANIMATION_FACTOR;
        }
    }
    return true;
}

// MODEL_TOWER_GATE_PLANE
bool MoveBehavior::Move_MODEL_TOWER_GATE_PLANE(OBJECT *o, int index, float Luminosity)
{
    o->LifeTime = 100;
    if (o->Owner != NULL && o->Live == true)
    {
        o->Position[2] = o->StartPosition[2] + sinf(WorldTime * 0.001f) * 200.f + 200.f;
    }
    return true;
}

// BITMAP_CRATER
bool MoveBehavior::Move_BITMAP_CRATER(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    if (o->LifeTime < 10)
    {
        o->Light[0] = o->LifeTime / 10.f;
        o->Light[1] = o->Light[0];
        o->Light[2] = o->Light[0];
        o->Alpha = o->Light[0];
    }
    Vector(-0.5f, -0.5f, -0.5f, Light);
    AddTerrainLight(o->Position[0], o->Position[1], Light, (int)(o->StartPosition[0] - 1),
                    PrimaryTerrainLight);
    return true;
}

// BITMAP_CHROME_ENERGY2
bool MoveBehavior::Move_BITMAP_CHROME_ENERGY2(OBJECT *o, int index, float Luminosity)
{
    if (o->LifeTime < 10)
    {
        o->Light[0] *= pow(0.8f, FPS_ANIMATION_FACTOR);
        o->Light[1] *= pow(0.8f, FPS_ANIMATION_FACTOR);
        o->Light[2] *= pow(0.8f, FPS_ANIMATION_FACTOR);
    }
    return true;
}

// MODEL_STUN_STONE
bool MoveBehavior::Move_MODEL_STUN_STONE(OBJECT *o, int index, float Luminosity)
{
    float Height;
    if (o->SubType == 0)
    {
        AdvanceEffectGravity(*o, 1.f, -15.f, FPS_ANIMATION_FACTOR);

        Height = RequestTerrainHeight(o->Position[0], o->Position[1]) + 50;
        if (o->Position[2] <= Height)
        {
            o->Position[2] = Height;
            if (o->ExtState == 0)
            {
                CreateEffect(BITMAP_CRATER, o->Position, o->Angle, o->Light, 1);
                o->ExtState = 1;
            }
        }
        else
        {
            VectorAddScaled(o->Position, o->StartPosition, o->Position,
                            Core::Time::DampedDistance(0.9f, FPS_ANIMATION_FACTOR));
            VectorScale(o->StartPosition, std::pow(0.9f, FPS_ANIMATION_FACTOR), o->StartPosition);

            o->HeadAngle[0] -= (o->Scale * 32.f) * FPS_ANIMATION_FACTOR;

            for (int events = Core::Time::Periods(o->LifeTime, FPS_ANIMATION_FACTOR, 3.f);
                 events > 0; --events)
            {
                CreateParticle(BITMAP_ADV_SMOKE + 1, o->Position, o->Angle, o->Light, 1, 1.f);
            }
        }
        o->Light[0] = o->LifeTime / 10.f;
        o->Light[1] = o->Light[0];
        o->Light[2] = o->Light[0];
        o->Alpha = o->Light[0];
    }
    else if (o->SubType == 1)
    {
        for (float life = Core::Time::ReferenceSample(o->LifeTime / 3.f) * 3.f;
             life > 0.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life);
             life -= 3.f)
        {
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR -
                                                                 std::max(0.f, o->LifeTime - life));
            CreateEffect(MODEL_STUN_STONE, o->Position, o->Angle, o->Light);
        }
        AddTerrainLight(o->Position[0], o->Position[1], o->Light, 1, PrimaryTerrainLight);
    }
    return true;
}

// MODEL_SKIN_SHELL
bool MoveBehavior::Move_MODEL_SKIN_SHELL(OBJECT *o, int index, float Luminosity)
{
    AdvanceBouncingDebris(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR,
                          DebrisMotion::SkinShell);
    o->Alpha = o->LifeTime / 10.f;
    return true;
}

// MODEL_MANA_RUNE
bool MoveBehavior::Move_MODEL_MANA_RUNE(OBJECT *o, int index, float Luminosity)
{
    const float sampleLife = o->LifeTime - FPS_ANIMATION_FACTOR + 1.f;
    if (o->SubType == 0)
    {
        o->HiddenMesh = sampleLife > 43.f ? -2 : 0;
        const float growing =
            (std::max)(0.f, (std::min)(o->LifeTime, 40.f) -
                                (std::max)(o->LifeTime - FPS_ANIMATION_FACTOR, 30.f));
        if (growing > 0.f)
        {
            float emitting = growing;
            float emissionOffset = (std::max)(0.f, o->LifeTime - 40.f);
            if (o->Scale < 1.f)
            {
                const double height = 1. - o->Scale;
                const double speed = o->Gravity - 0.075;
                const double root = std::sqrt(speed * speed + 0.3 * height);
                const double arrival =
                    speed >= 0. ? 2. * height / (speed + root) : (root - speed) / 0.15;
                const float moving = (std::min)(growing, static_cast<float>(arrival));
                emitting -= moving;
                emissionOffset += moving;
                Core::Time::Advance(o->Scale, o->Gravity, 0.15f, moving);
                if (arrival <= growing)
                    o->Scale = 1.f;
            }
            if (o->Scale >= 1.f)
            {
                o->Scale = 1.f;
                o->Gravity = 0.01f;
                for (auto birthTime :
                     sessionKeeper_.Gameplay()->Emissions(emitting, emitting, emissionOffset))
                    CreateEffect(MODEL_MANA_RUNE, o->Position, o->Angle, o->Light, 1);
            }
        }
        const float shrinking =
            (std::max)(0.f, FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 14.f));
        if (shrinking > 0.f)
        {
            float travel = 0.f;
            Core::Time::Advance(travel, o->Gravity, 0.2f, shrinking);
            o->Scale = (std::max)(0.f, o->Scale - travel);
            o->Alpha = sampleLife / 20.f;
            o->Position[2] -= 10.f * shrinking;
        }
    }
    else if (o->SubType == 1)
    {
        o->Scale = (std::max)(1.f, o->Scale - 0.02f * FPS_ANIMATION_FACTOR);
        o->Alpha = sampleLife / 20.f;
    }
    return true;
}

// MODEL_SKILL_JAVELIN
bool MoveBehavior::Move_MODEL_SKILL_JAVELIN(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    AdvanceJavelin(
        *o, FPS_ANIMATION_FACTOR, *sessionKeeper_.Random(), WorldTime,
        [&](float offset, float duration, bool arrived, float referenceLife) {
            const auto emitParticle = [&](const auto &birth) {
                vec3_t position, angle;
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
                VectorCopy(o->Angle, angle);
                const float afterBirth =
                    duration - (FPS_ANIMATION_FACTOR - birth.RemainingFrames() - offset);
                angle[2] -= o->AmbientTurnRate * afterBirth;
                CreateParticle(BITMAP_POUNDING_BALL, position, angle, o->Light, 1);
            };
            if (arrived)
            {
                if (static_cast<int>(referenceLife) % 3 != 0 ||
                    !Core::Time::Reaches(o->LifeTime - offset, duration, referenceLife))
                    return;
                auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                    FPS_ANIMATION_FACTOR - std::max(0.f, o->LifeTime - referenceLife));
                emitParticle(birth);
                return;
            }
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(duration, duration, offset))
            {
                emitParticle(birth);
            }
        });
    o->BlendMeshLight = o->LifeTime / 10.f;
    o->Alpha = o->BlendMeshLight;

    Vector(1.f, 0.6f, 0.3f, Light);
    AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
    return true;
}

// MODEL_ARROW_IMPACT
bool MoveBehavior::Move_MODEL_ARROW_IMPACT(OBJECT *o, int index, float Luminosity)
{
    vec3_t Position;
    AdvanceAcceleratingPitch(*o, -8.f, -5.f, FPS_ANIMATION_FACTOR);

    if (o->LifeTime < 2 && o->SubType == 0 && o->Owner != NULL)
    {
        o->Angle[0] = 90.f;
        o->SubType = 1;

        VectorCopy(o->Owner->Position, Position);
        Position[0] += WorldRandom() % 100 - 50.f;
        Position[1] += WorldRandom() % 100 - 50.f;
        Position[2] += 1200.f;
        CreateJoint(BITMAP_FLASH, Position, Position, o->Angle, 2, o, 50.f);
        CreateJoint(BITMAP_FLASH, Position, Position, o->Angle, 3, o, 50.f);
    }
    else if (o->SubType == 1)
    {
        RetireEffect(o);
    }
    return true;
}

// MODEL_SWORD_FORCE
bool MoveBehavior::Move_MODEL_SWORD_FORCE(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    if (o->SubType == 0 || o->SubType == 2)
    {
        const float frames = FPS_ANIMATION_FACTOR;
        const float growing = (std::min)(frames, (std::max)(0.f, o->LifeTime - 12.f));
        o->Scale += 0.9f * growing - 0.05f * (frames - growing);
        constexpr float Acceleration = -2.f;
        vec3_t travel{o->Direction[0] * frames,
                      (o->Direction[1] + Acceleration * (frames + 1.f) * 0.5f) * frames,
                      o->Direction[2] * frames};
        float matrix[3][4];
        AngleMatrix(o->Angle, matrix);
        vec3_t worldTravel;
        VectorRotate(travel, matrix, worldTravel);
        VectorAdd(o->Position, worldTravel, o->Position);
        o->Direction[1] += Acceleration * frames;
        const auto samplePosition = [&](float remaining, vec3_t position) {
            vec3_t suffix{o->Direction[0] * remaining,
                          (o->Direction[1] + Acceleration * (1.f - remaining) * 0.5f) * remaining,
                          o->Direction[2] * remaining};
            vec3_t rotated;
            VectorRotate(suffix, matrix, rotated);
            VectorSubtract(o->Position, rotated, position);
        };
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(growing, growing))
        {
            samplePosition(birth.RemainingFrames(), Position);
            CreateEffect(MODEL_SWORD_FORCE, Position, o->Angle, o->Light, o->SubType == 2 ? 3 : 1,
                         o);
        }
        const float fading = frames - growing;
        if (fading > 0.f)
        {
            o->BlendMeshLight = (float)o->LifeTime / 18.f;
            o->Alpha = o->BlendMeshLight;
            Vector(o->Alpha, o->Alpha, o->Alpha, o->Light);
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(fading, fading, growing))
            {
                samplePosition(birth.RemainingFrames(), Position);
                Position[0] += WorldRandom() % 30 - 15.f;
                Position[1] += WorldRandom() % 30 - 15.f;
                Position[2] -= 100.f;
                const float alpha = (o->LifeTime - frames + birth.RemainingFrames()) / 18.f;
                vec3_t light{alpha, alpha, alpha};
                for (int i = 0; i < 4; ++i)
                {
                    Vector(float(WorldRandom() % 60 + 150), 0.f, o->Angle[2], Angle);
                    CreateJoint(BITMAP_JOINT_SPARK, Position, Position, Angle);
                    CreateParticle(BITMAP_FIRE, Position, Angle, light, o->SubType == 2 ? 18 : 2,
                                   1.5f);
                }
            }
        }
        Vector(1.f, 0.8f, 0.6f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 1, PrimaryTerrainLight);
    }
    else if (o->SubType == 1 || o->SubType == 3)
    {
        o->BlendMeshLight = (float)o->LifeTime / 10.f;
        o->Alpha = o->BlendMeshLight;
        o->Light[0] = o->Alpha;
        o->Light[1] = o->Alpha;
        o->Light[2] = o->Alpha;
    }
    return true;
}

// MODEL_PROTECTGUILD
bool MoveBehavior::Move_MODEL_PROTECTGUILD(OBJECT *o, int index, float Luminosity)
{
    vec3_t p;
    {
        if (o->Owner == NULL)
        {
            o->LifeTime = 0;
            return true;
        }
        AdvanceGuildProtection(*o, FPS_ANIMATION_FACTOR);
        if (o->LifeTime > 120)
        {
            if (o->LifeTime < 125 && o->LifeTime > 120)
            {
                for (int i = 0; i < 5; ++i)
                    CreateParticleFpsChecked(BITMAP_SPARK, o->Position, o->Angle, o->Light, 6, 1.5f,
                                             o);
            }
        }
        BMD *b = &Models[o->Owner->Type];
        vec3_t tempPosition;
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(o->Owner->BoneTransform[20], p, tempPosition, true);
        o->Position[0] = tempPosition[0] + (o->Owner->Position[0] - Hero->Object.Position[0]);
        o->Position[1] = tempPosition[1] + (o->Owner->Position[1] - Hero->Object.Position[1]);

        float fHeight = o->Owner->Position[2] + tempPosition[2] - o->Owner->Position[2] + 60;
        if (fHeight < o->Position[2])
            o->Position[2] +=
                (fHeight - o->Position[2]) * Core::Time::Blend(0.1f, FPS_ANIMATION_FACTOR);
        else
            o->Position[2] +=
                (fHeight - o->Position[2]) * Core::Time::Blend(0.5f, FPS_ANIMATION_FACTOR);

        if (o->LifeTime < 60 && o->LifeTime > 39)
        {
            vec3_t p;
            for (int i = 0; i < 1; ++i)
            {
                float fAngle = WorldRandom() % 360;
                Vector(o->Position[0] + (WorldRandom() % 26 - 13) * sinf(fAngle),
                       o->Position[1] + (WorldRandom() % 26 - 13) * cosf(fAngle),
                       //o->Position[2] + 45+(10-o->LifeTime)*1.5f+WorldRandom()%5, p);
                       o->Position[2] + 48 - o->LifeTime + (WorldRandom() % 5), p);
                CreateParticleFpsChecked(BITMAP_SPARK, p, o->Angle, o->Light, 5, 2.0f, o);
            }
        }
    }
    return true;
}

// MODEL_MOVE_TARGETPOSITION_EFFECT
bool MoveBehavior::Move_MODEL_MOVE_TARGETPOSITION_EFFECT(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->SubType == 0)
        {
            //float fScale = 0.f;
            vec3_t vLight, vPos, vAngle, vRelativePos;
            Vector(1.0f, 0.7f, 0.3f, vLight);
            Vector(0.0f, 0.0f, 0.0f, vAngle);
            Vector(0.0f, 0.0f, 0.0f, vRelativePos);
            VectorCopy(o->Position, vPos);
            vPos[2] += 110.f;

            {
                CreateParticleFpsChecked(BITMAP_SPARK + 1, vPos, o->Angle, vLight, 24, 1.0f, o);
            }

            for (float life = Core::Time::ReferenceSample(o->LifeTime / 15.f) * 15.f;
                 life > 0.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life);
                 life -= 15.f)
            {
                auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                    FPS_ANIMATION_FACTOR - std::max(0.f, o->LifeTime - life));
                CreateEffect(BITMAP_TARGET_POSITION_EFFECT1, o->Position, vAngle, vLight,
                             0); //, NULL, -1, 0, 0, 0, fScale );
            }

            AdvanceTargetFade(*o, o->BlendMeshLight, FPS_ANIMATION_FACTOR);
        }
    }
    return true;
}

// BITMAP_TARGET_POSITION_EFFECT1
bool MoveBehavior::Move_BITMAP_TARGET_POSITION_EFFECT1(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->SubType == 0)
        {
            o->Scale -= (0.04f) * FPS_ANIMATION_FACTOR;
            if (o->Scale <= 0.2f)
            {
                RetireEffect(o);
            }
            AdvanceTargetFade(*o, o->Alpha, FPS_ANIMATION_FACTOR);
        }
    }
    return true;
}

// BITMAP_TARGET_POSITION_EFFECT2
bool MoveBehavior::Move_BITMAP_TARGET_POSITION_EFFECT2(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->SubType == 0)
        {
            AdvanceTargetPulse(*o, FPS_ANIMATION_FACTOR);

            AdvanceTargetFade(*o, o->Alpha, FPS_ANIMATION_FACTOR);
        }
    }
    return true;
}

// MODEL_EFFECT_SAPITRES_ATTACK
bool MoveBehavior::Move_MODEL_EFFECT_SAPITRES_ATTACK(OBJECT *o, int index, float Luminosity)
{
    constexpr float AttackInterval = 6.f;
    for (float life = Core::Time::ReferenceSample(o->LifeTime / AttackInterval) * AttackInterval;
         life > 0.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life);
         life -= AttackInterval)
    {
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR -
                                                             std::max(0.f, o->LifeTime - life));
        o->Position[0] += float(WorldRandom() % 120 - 60);
        o->Position[1] += float(WorldRandom() % 120 - 60);
        CreateEffect(MODEL_EFFECT_SAPITRES_ATTACK_1, o->Position, o->Angle, o->Light, 0, o->Owner);
    }
    return true;
}

// MODEL_EFFECT_THUNDER_NAPIN_ATTACK_1
bool MoveBehavior::Move_MODEL_EFFECT_THUNDER_NAPIN_ATTACK_1(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType != 0 && o->SubType != 1)
        return true;
    vec3_t position, light{0.4f, 0.7f, 1.f}, flare{0.1f, 0.2f, 0.8f};
    const auto offset = [&](vec3_t value) {
        if (o->SubType == 0)
            return;
        value[0] += WorldRandom() % 200 - 100;
        value[1] += WorldRandom() % 200 - 100;
        value[2] += WorldRandom() % 100 - 50;
    };
    const float duration = (std::min)(FPS_ANIMATION_FACTOR, (std::max)(0.f, o->LifeTime - 3.f));
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(duration, duration))
    {
        o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
        offset(position);
        o->Scale = 0.8f + WorldRandom() % 10 * 0.1f;
        for (int axis = 0; axis < 3; ++axis)
            position[axis] += 3.f * (WorldRandom() % 40 - 20);
        CreateParticle(BITMAP_LIGHTNING_MEGA1 + WorldRandom() % 3, position, o->Angle, light, 0,
                       o->Scale);
    }
    VectorCopy(o->Position, position);
    offset(position);
    CreateSprite(BITMAP_LIGHT, position, 8.f, flare, o);
    CreateSprite(BITMAP_LIGHT, position, 8.f, flare, o);
    return true;
}

void MoveBehavior::EmitSakuraPetals(OBJECT &effect, BMD &model)
{
    AnimationPoseSample pose(&effect, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    std::array<vec34_t, MAX_BONES> bones;
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        ObjectDrawInput draw(&effect);
        effect.Owner->MotionTrace.Sample(WorldTime, birth.FrameFraction(), effect.Owner->Position,
                                         draw.position);
        draw.bones =
            pose.EvaluateAtTime(model, effect, WorldTime, birth.FrameFraction(), bones.data());
        vec3_t gray{0.3f, 0.3f, 0.3f}, pink{1.f, 0.6f, 0.8f};
        for (int bone : {1, 2})
        {
            vec3_t position;
            model.TransformByObjectBone(position, draw, bone);
            constexpr int PetalsPerBone = 7;
            for (int i = 0; i < PetalsPerBone; ++i)
            {
                CreateParticle(BITMAP_SHINY + 1, position, effect.Angle, effect.Light, 5, 0.8f,
                               &effect);
                CreateParticle(BITMAP_CHERRYBLOSSOM_EVENT_PETAL, position, effect.Angle,
                               WorldRandom() % 7 == 3 ? pink : gray, 0, 0.5f);
            }
        }
    }
}

// MODEL_EFFECT_SKURA_ITEM
bool MoveBehavior::Move_MODEL_EFFECT_SKURA_ITEM(OBJECT *o, int index, float Luminosity)
{
    {
        if ((o->SubType == 0) || (o->SubType == 1))
        {
            if (o->Owner->Live && o->Live)
            {
                vec3_t vRelativePos, vtaWorldPos, vLight1, vLight2;
                BMD *b = &Models[MODEL_EFFECT_SKURA_ITEM];

                VectorCopy(o->Owner->Position, o->Position);
                VectorCopy(o->Position, b->BodyOrigin);
                Vector(0.f, 0.f, 0.f, vRelativePos);
                Vector(1.f, 0.6f, 0.8f, o->Light);
                Vector(0.3f, 0.3f, 0.3f, vLight1);
                Vector(1.f, 0.6f, 0.8f, vLight2);

                b->Animation(BoneTransform, o->AnimationFrame, o->PriorAnimationFrame,
                             o->PriorAction, o->Angle, o->HeadAngle);

                b->TransformPosition(BoneTransform[1], vRelativePos, vtaWorldPos, false);
                CreateSprite(BITMAP_SHINY + 1, vtaWorldPos, 1.8f, o->Light, o, +WorldTime * 0.08f);

                b->TransformPosition(BoneTransform[2], vRelativePos, vtaWorldPos, false);
                CreateSprite(BITMAP_SHINY + 1, vtaWorldPos, 1.8f, o->Light, o, -WorldTime * 0.08f);

                EmitSakuraPetals(*o, *b);
                for (float life : {30.f, 15.f, 4.f})
                {
                    if (!Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life))
                        continue;
                    auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                        FPS_ANIMATION_FACTOR - std::max(0.f, o->LifeTime - life));
                    vec3_t vAngle;

                    BMD *characterBMD = &Models[o->Owner->Type];
                    AnimationPoseSample ownerPose(o->Owner, characterBMD->BoneHead,
                                                  characterBMD->BodyHeight, false,
                                                  characterBMD->PoseAssetIdentity());
                    ownerPose.SampleBonePosition(*characterBMD, *o->Owner, 20, vRelativePos,
                                                 WorldTime, birth.FrameFraction(), vtaWorldPos);
                    vec3_t burstOrigin;
                    VectorCopy(vtaWorldPos, burstOrigin);

                    if (WorldRandom() % 2 == 0)
                    {
                        vtaWorldPos[0] += WorldRandom() % 40 + 10;
                    }
                    else
                    {
                        vtaWorldPos[0] -= WorldRandom() % 30 + 10;
                    }

                    vtaWorldPos[2] += WorldRandom() % 110 + 50;

                    Vector(0.7f, 0.71f, 1.0f, vLight1);
                    Vector(0.8f, 0.85f, 1.0f, vLight2);

                    for (int k = 0; k < 70; ++k)
                    {
                        if (WorldRandom() % 3 == 0)
                        {
                            CreateParticle(BITMAP_CHERRYBLOSSOM_EVENT_PETAL, vtaWorldPos, o->Angle,
                                           WorldRandom() % 7 == 3 ? vLight2 : vLight1, 0, 0.4f);
                        }
                        CreateParticle(BITMAP_CHERRYBLOSSOM_EVENT_FLOWER, vtaWorldPos, o->Angle,
                                       WorldRandom() % 4 == 3 ? vLight2 : vLight1, 0, 0.4f);
                    }

                    Vector(0.f, 1.f, 0.f, vAngle);
                    Vector(1.f, 0.6f, 0.8f, vLight2);
                    CreateParticle(BITMAP_SHOCK_WAVE, vtaWorldPos, vAngle, vLight2, 4, 0.005f);

                    VectorCopy(burstOrigin, vtaWorldPos);

                    vtaWorldPos[2] += 70;
                    Vector(1.f, 0.3f, 0.6f, vLight2);
                    CreateSprite(BITMAP_SHINY + 1, vtaWorldPos, 1.8f, o->Light, o,
                                 -WorldTime * 0.08f);

                    StopBuffer(SOUND_CHERRYBLOSSOM_EFFECT1, true);
                    PlayBuffer(SOUND_CHERRYBLOSSOM_EFFECT1, o->Owner);
                }

                b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction,
                                 o->Velocity / 3.f, o->Position, o->Angle);
            }
        }
    }
    return true;
}

// MODEL_BLOW_OF_DESTRUCTION
bool MoveBehavior::Move_MODEL_BLOW_OF_DESTRUCTION(OBJECT *o, int index, float Luminosity)
{
    float Matrix[3][4];
    {
        if (o->SubType == 0)
        {
            vec3_t vLight;
            if (o->LifeTime <= 24)
            {
                if (o->LifeTime >= 15.f)
                {
                    EarthQuake = (float)(WorldRandom() % 8 - 4) * 0.1f;
                }
                else
                {
                    EarthQuake = 0.f;
                }

                if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 23))
                {
                    auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                        FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 23.f));
                    Vector(0.3f, 0.3f, 1.0f, vLight);
                    CreateEffect(MODEL_NIGHTWATER_01, o->Position, o->Angle, vLight, 0, o);
                    CreateEffect(MODEL_NIGHTWATER_01, o->Position, o->Angle, vLight, 0, o);
                    CreateEffect(MODEL_KNIGHT_PLANCRACK_A, o->Position, o->Angle, vLight, 0, o, 0,
                                 0, 0, 0, 1.2f);

                    vec3_t vDir, vPos, vAngle;
                    VectorSubtract(o->StartPosition, o->Position, vDir);
                    float fLength = VectorLength(vDir);
                    VectorNormalize(vDir);
                    int iNum = (int)(fLength / 100) + 1;
                    for (int i = 0; i < iNum; ++i)
                    {
                        VectorScale(vDir, 55.f * i, vPos);
                        VectorAdd(o->Position, vPos, vPos);
                        VectorCopy(o->Owner->Angle, vAngle);
                        if (i % 2 == 0)
                        {
                            vAngle[2] += (WorldRandom() % 20 + 10);
                        }
                        else
                        {
                            vAngle[2] -= (WorldRandom() % 20 + 10);
                        }
                        CreateEffect(MODEL_KNIGHT_PLANCRACK_B, vPos, vAngle, vLight, 0, o, 0, 0, 0,
                                     0, 1.0f);
                    }
                }

                o->Light[0] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            }
        }
        else if (o->SubType == 1)
        {
            if (o->LifeTime <= 24)
            {
                if (o->LifeTime >= 15)
                {
                    vec3_t vPos, vLight;
                    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
                    {
                        for (int i = 0; i < 15; ++i)
                        {
                            VectorCopy(o->Position, vPos);
                            vPos[0] += WorldRandom() % 300 - 150;
                            vPos[1] += WorldRandom() % 300 - 150;
                            vPos[2] += WorldRandom() % 300 - 150;
                            float fScale = 1.6f + WorldRandom() % 10 * 0.1f;
                            Vector(0.5f, 0.5f, 1.0f, vLight);
                            int index =
                                (WorldRandom() % 2) ? BITMAP_WATERFALL_5 : BITMAP_WATERFALL_3;
                            CreateParticle(index, vPos, o->Angle, vLight, 8, fScale);
                            Vector(1.0f, 1.0f, 1.0f, vLight);
                            if (WorldRandom() % 2 == 0)
                            {
                                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, 55, 1.f);
                            }
                        }
                    }
                }

                if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 23))
                {
                    auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                        FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 23.f));
                    vec3_t vLight;
                    Vector(0.5f, 0.5f, 1.f, vLight);

                    Vector(0.3f, 0.3f, 1.0f, vLight);
                    CreateEffect(MODEL_NIGHTWATER_01, o->Position, o->Angle, vLight, 0, o, -1, 0, 0,
                                 0, 2.f);
                    CreateEffect(MODEL_NIGHTWATER_01, o->Position, o->Angle, vLight, 0, o, -1, 0, 0,
                                 0, 1.f);
                    CreateEffect(MODEL_RAKLION_BOSS_CRACKEFFECT, o->Position, o->Angle, vLight, 0,
                                 o, -1, 0, 0, 0, 0.2f);

                    vec3_t vPos, vResult;
                    vec3_t vAngle;
                    float Matrix[3][4];
                    for (int i = 0; i < (5 + WorldRandom() % 3); ++i)
                    {
                        Vector(0.f, (float)(WorldRandom() % 150), 0.f, vPos);
                        Vector(0.f, 0.f, (float)(WorldRandom() % 360), vAngle);
                        AngleMatrix(vAngle, Matrix);
                        VectorRotate(vPos, Matrix, vResult);
                        VectorAdd(vResult, o->Position, vResult);

                        CreateEffect(MODEL_STONE1 + WorldRandom() % 2, vResult, o->Angle, o->Light,
                                     13);
                    }
                }

                o->Light[0] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            }
        }
        else if (o->SubType == 2)
        {
            vec3_t vLight;
            if (o->LifeTime <= 35.0f && o->LifeTime >= 15.0f)
            {
                EarthQuake = (float)(WorldRandom() % 8 - 4) * 0.1f;
            }
            else
            {
                EarthQuake = 0.f;
            }

            if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 30))
            {
                Vector(1.0f, 1.0f, 1.0f, vLight);
                vec3_t vDir, vPos, vAngle;
                VectorSubtract(o->StartPosition, o->Position, vDir);
                VectorLength(vDir);
                VectorNormalize(vDir);
                VectorScale(vDir, 55.f, vPos);
                VectorAdd(o->Position, vPos, vPos);
                VectorCopy(o->Owner->Angle, vAngle);
                vAngle[2] += (WorldRandom() % 40 + 140);
                CreateEffect(MODEL_DRAGON_LOWER_DUMMY, vPos, vAngle, vLight, 0, o, 0, 0, 0, 0,
                             0.8f);

                Vector(1.0f, 1.0f, 1.0f, vLight);
                CreateEffect(BITMAP_LIGHT_RED, vPos, vAngle, vLight, 4, o, -1, 0, 0, 0, 3.0f);
            }
        }
    }
    return true;
}

// MODEL_NIGHTWATER_01
bool MoveBehavior::Move_MODEL_NIGHTWATER_01(OBJECT *o, int index, float Luminosity)
{
    {
        o->Alpha -= (0.04f) * FPS_ANIMATION_FACTOR;
    }
    return true;
}

// MODEL_KNIGHT_PLANCRACK_A
bool MoveBehavior::Move_MODEL_KNIGHT_PLANCRACK_A(OBJECT *o, int index, float Luminosity)
{
    if (o->SubType == 0)
    {
        o->Alpha -= (0.04f) * FPS_ANIMATION_FACTOR;
    }
    else if (o->SubType == 1)
    {
        //if (o->LifeTime < 10)
        {
            o->Alpha *= pow(0.9f, FPS_ANIMATION_FACTOR);
        }
    }
    return true;
}

// MODEL_KNIGHT_PLANCRACK_B
bool MoveBehavior::Move_MODEL_KNIGHT_PLANCRACK_B(OBJECT *o, int index, float Luminosity)
{
    {
        o->Alpha -= (0.04f) * FPS_ANIMATION_FACTOR;
    }
    return true;
}

// MODEL_EFFECT_FLAME_STRIKE
bool MoveBehavior::Move_MODEL_EFFECT_FLAME_STRIKE(OBJECT *o, int index, float Luminosity)
{
    vec3_t p;
    {
        if (o->SubType == 0)
        {
            if ((o->LifeTime < 20 && o->Alpha < 0.1f) || o->Owner == NULL)
            {
                EffectDestructor(o);
                return true;
            }
            if (o->LifeTime < 20)
                o->Alpha -= (0.1f) * FPS_ANIMATION_FACTOR;
            else if (o->Alpha < 1.0f)
                o->Alpha += (0.1f) * FPS_ANIMATION_FACTOR;

            OBJECT *pObject = o;
            BMD *pModel = &Models[pObject->Type];
            OBJECT *pOwner = pObject->Owner;
            BMD *pOwnerModel = &Models[pOwner->Type];

            // blur
            float Start_Frame = 5.0f;
            float End_Frame = 13.0f;

            if (pOwner->AnimationFrame > End_Frame || pObject->AI == 1)
            {
                pObject->AI = 1;
            }
            else if (pOwner->CurrentAction == PLAYER_SKILL_FLAMESTRIKE)
            {
                pObject->AnimationFrame = pOwner->AnimationFrame;

                pOwnerModel->BodyScale = pOwner->Scale;
                pOwnerModel->CurrentAction = pOwner->CurrentAction;
                VectorCopy(pOwner->Angle, pOwnerModel->BodyAngle);
                VectorCopy(pOwner->Position, pOwnerModel->BodyOrigin);

                pModel->BodyScale = pObject->Scale;
                pModel->CurrentAction = pObject->CurrentAction;
                VectorCopy(pObject->Angle, pModel->BodyAngle);
                VectorCopy(pObject->Position, pModel->BodyOrigin);
                pModel->CurrentAnimation = pObject->AnimationFrame;
                pModel->CurrentAnimationFrame = (int)pObject->AnimationFrame;

                vec3_t vLight;
                Vector(1.0f, 1.0f, 1.0f, vLight);

                vec3_t StartPos, StartRelative;
                vec3_t EndPos, EndRelative;

                pOwnerModel->CurrentAction = pOwner->CurrentAction;
                pModel->CurrentAction = pObject->CurrentAction;
                float fOwnerActionSpeed =
                    pOwnerModel->Actions[pOwnerModel->CurrentAction].PlaySpeed;
                float fOwnerSpeedPerFrame = fOwnerActionSpeed / 10.f;
                float fOwnerAnimationFrame = pOwner->AnimationFrame - fOwnerActionSpeed;

                float fActionSpeed = pObject->Velocity;
                float fSpeedPerFrame = fActionSpeed / 10.f;
                float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;

                for (int i = 0; i < 10; i++)
                {
                    pOwnerModel->AnimationAtFrame(BoneTransform, fOwnerAnimationFrame,
                                                  pOwner->PriorAnimationFrame, pOwner->PriorAction,
                                                  pOwner->Angle, pOwner->HeadAngle, false, false);
                    pOwnerModel->RotationPosition(BoneTransform[33], p, p); // ParentMatrix

                    pModel->AnimationAtFrame(BoneTransform, fAnimationFrame,
                                             pObject->PriorAnimationFrame, pObject->PriorAction,
                                             pObject->Angle, pObject->HeadAngle, true,
                                             true); // BoneTransform

                    if (fOwnerAnimationFrame >= Start_Frame && fOwnerAnimationFrame <= End_Frame)
                    {
                        Vector(0.f, 0.f, 0.f, StartRelative);
                        Vector(0.f, 0.f, 0.f, EndRelative);
                        pModel->TransformPosition(BoneTransform[9], StartRelative, StartPos, true);
                        pModel->TransformPosition(BoneTransform[6], EndRelative, EndPos, true);
                        CreateObjectBlur(pObject, StartPos, EndPos, vLight, 2, false,
                                         o->m_iAnimation + 1);
                        CreateObjectBlur(pObject, StartPos, EndPos, vLight, 2, false,
                                         o->m_iAnimation + 2);

                        pModel->TransformPosition(BoneTransform[8], StartRelative, StartPos, true);
                        pModel->TransformPosition(BoneTransform[5], EndRelative, EndPos, true);
                        CreateObjectBlur(pObject, StartPos, EndPos, vLight, 5, false,
                                         o->m_iAnimation + 3);
                    }

                    fOwnerAnimationFrame += fOwnerSpeedPerFrame;
                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
    }
    return true;
}

// MODEL_1_STREAMBREATHFIRE
bool MoveBehavior::Move_MODEL_1_STREAMBREATHFIRE(OBJECT *o, int index, float Luminosity)
{
    {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            vec3_t position;
            o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
            CreateParticle(BITMAP_WATERFALL_3, position, o->Angle, o->Light, 11, 0.6f);
            CreateParticle(BITMAP_SMOKE, position, o->Angle, o->Light, 52, 0.6f);
        }

        if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 15.f))
        {
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 15.f));
            const float fForce = 20.0f;

            float matRotation[3][4];
            vec3_t vPos;
            vec3_t vDir, vDir_;

            Vector(0.0f, -fForce, 0.0f, vDir);
            AngleMatrix(o->Angle, matRotation);

            VectorRotate(vDir, matRotation, vDir_);

            VectorCopy(o->Position, vPos);
            //vPos[2] += 100.f;

            CreateEffect(MODEL_MOONHARVEST_MOON, vPos, vDir_, o->Light, 1, NULL, -1, 0, 0, 0, 0.3f);
        }
    }
    return true;
}

// MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_HEAD, MODEL_PKFIELD_ASSASSIN_EFFECT_RED_HEAD
bool MoveBehavior::Move_MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_HEAD(OBJECT *o, int index,
                                                                 float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    {
        const auto motion = AdvanceHeadDebris(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR,
                                              20.f, 0.6f, 0.8f, 5.f);
        o->Alpha -= 0.05f * motion.groundFrames;
        const float spin = (o->SubType == 0 ? 0.15f : -0.15f) * motion.airLife;
        o->Angle[0] += spin;
        o->Angle[1] += spin;

        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            vec3_t position, light{1.f, 1.f, 1.f};
            o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
            const int subtype = o->Type == MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_HEAD ? 4 : 3;
            CreateEffect(BITMAP_FIRE_CURSEDLICH, position, o->Angle, light, subtype, o);
        }
    }
    return true;
}

// MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_BODY, MODEL_PKFIELD_ASSASSIN_EFFECT_RED_BODY
bool MoveBehavior::Move_MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_BODY(OBJECT *o, int index,
                                                                 float Luminosity)
{
    {
        if (o->LifeTime < 20)
        {
            o->Alpha = 0.1f * o->LifeTime;
        }
    }
    return true;
}

// MODEL_LAVAGIANT_FOOTPRINT_R, MODEL_LAVAGIANT_FOOTPRINT_V
bool MoveBehavior::Move_MODEL_LAVAGIANT_FOOTPRINT_R(OBJECT *o, int index, float Luminosity)
{
    {
        o->Angle[0] += (0.1f) * FPS_ANIMATION_FACTOR;

        o->Alpha -= (0.01f) * FPS_ANIMATION_FACTOR;
        if (o->Alpha <= 0.0f)
            RetireEffect(o);

        o->Light[0] *= pow(0.93f, FPS_ANIMATION_FACTOR);
        o->Light[1] *= pow(0.93f, FPS_ANIMATION_FACTOR);
        o->Light[2] *= pow(0.93f, FPS_ANIMATION_FACTOR);
    }
    return true;
}

// MODEL_PROJECTILE
bool MoveBehavior::Move_MODEL_PROJECTILE(OBJECT *o, int index, float Luminosity)
{
    {
        const float attached = std::clamp(o->LifeTime - 38.f, 0.f, FPS_ANIMATION_FACTOR);
        const float released = FPS_ANIMATION_FACTOR - attached;
        o->EffectAnimationAdvance =
            o->Velocity * FPS_ANIMATION_FACTOR + 0.2f * released * (released + 1.f) * 0.5f;
        float remainingAttachment = attached;
        while (remainingAttachment > 0.f)
        {
            const float step = (std::min)(1.f, remainingAttachment);
            const float fraction = (attached - remainingAttachment + step) / FPS_ANIMATION_FACTOR;
            SampleOwnerBone(*o->Owner, Models[o->Owner->Type], 9, WorldTime,
                            o->BirthTiming.FrameFraction(fraction), o->Position);
            o->MotionTrace.Advance(step, o->Position);
            remainingAttachment -= step;
        }
        if (released > 0.f)
        {
            if (o->Gravity == 0.0f)
            {
                o->Gravity = 0.1f;

                o->Direction[0] = o->Position[0] - o->StartPosition[0];
                o->Direction[1] = o->Position[1] - o->StartPosition[1];
                o->Direction[2] = o->Position[2] - o->StartPosition[2];
                VectorNormalize(o->Direction);
            }
            constexpr float SteeringInterval = 1.f / 16.f, PitchRate = 0.5f, Acceleration = 0.2f;
            float remaining = released;
            while (remaining > 0.f)
            {
                if (o->EffectMotionFrames <= 0.f)
                {
                    vec3_t angle, rotated;
                    VectorCopy(o->Angle, angle);
                    angle[0] += PitchRate * SteeringInterval * 0.5f;
                    float matrix[3][4];
                    AngleMatrix(angle, matrix);
                    VectorRotate(o->Direction, matrix, rotated);
                    const float speed =
                        o->Velocity + Acceleration * (SteeringInterval + 1.f) * 0.5f;
                    Vector(o->Direction[0] * speed, o->Direction[1] * speed, o->Direction[2],
                           o->EffectMotionVelocity);
                    VectorAdd(o->EffectMotionVelocity, rotated, o->EffectMotionVelocity);
                    o->EffectMotionFrames = SteeringInterval;
                }
                const float step = (std::min)(remaining, o->EffectMotionFrames);
                VectorAddScaled(o->Position, o->EffectMotionVelocity, o->Position, step);
                o->Angle[0] += PitchRate * step;
                o->Velocity += Acceleration * step;
                o->EffectMotionFrames -= step;
                remaining -= step;
                o->MotionTrace.Advance(step, o->Position);
            }
        }
    }
    return true;
}

// MODEL_DOOR_CRUSH_EFFECT_PIECE01, MODEL_DOOR_CRUSH_EFFECT_PIECE02, MODEL_DOOR_CRUSH_EFFECT_PIECE03, MODEL_DOOR_CRUSH_EFFECT_PIECE04, MODEL_DOOR_CRUSH_EFFECT_PIECE05, MODEL_DOOR_CRUSH_EFFECT_PIECE06, MODEL_DOOR_CRUSH_EFFECT_PIECE07, MODEL_DOOR_CRUSH_EFFECT_PIECE08, MODEL_DOOR_CRUSH_EFFECT_PIECE09, MODEL_DOOR_CRUSH_EFFECT_PIECE11, MODEL_DOOR_CRUSH_EFFECT_PIECE12, MODEL_DOOR_CRUSH_EFFECT_PIECE13, MODEL_STATUE_CRUSH_EFFECT_PIECE01, MODEL_STATUE_CRUSH_EFFECT_PIECE02, MODEL_STATUE_CRUSH_EFFECT_PIECE03
bool MoveBehavior::Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    float Matrix[3][4];
    vec3_t p;
    {
        const auto motion = AdvanceHeadDebris(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR,
                                              20.f, 0.6f, 0.8f, 5.f);
        o->Alpha -= 0.05f * motion.groundFrames;
        const float spin = (o->SubType == 0 ? 0.15f : -0.15f) * motion.airLife;
        o->Angle[0] += spin;
        o->Angle[1] += spin;
        if (motion.groundFrames > 0.f)
        {
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(
                     motion.groundFrames / 4.0, motion.groundFrames,
                     FPS_ANIMATION_FACTOR - motion.groundFrames))
            {
                Vector(0.f, (float)(WorldRandom() % 80) + 50.f, 0.f, p);
                Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);
                AngleMatrix(Angle, Matrix);
                VectorRotate(p, Matrix, Position);
                VectorAdd(Position, o->Position, Position);

                CreateEffect(MODEL_STONE1 + WorldRandom() % 2, Position, o->Angle, o->Light, 0);
            }
        }
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.0))
        {
            CreateParticle(BITMAP_SMOKE + 1, o->Position, o->Angle, o->Light, 6);
        }
    }
    return true;
}

// MODEL_STATUE_CRUSH_EFFECT_PIECE04, MODEL_DOOR_CRUSH_EFFECT_PIECE10
bool MoveBehavior::Move_MODEL_STATUE_CRUSH_EFFECT_PIECE04(OBJECT *o, int index, float Luminosity)
{
    {
        o->LifeTime -= (1) * FPS_ANIMATION_FACTOR;

        if (o->LifeTime < 40)
        {
            o->Alpha -= (0.1f) * FPS_ANIMATION_FACTOR;
        }
    }
    return true;
}

// MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_, MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_, MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_, MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_, MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_, MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE
void MoveBehavior::EmitGaionImpact(OBJECT &object, vec3_t angle)
{
    constexpr int Spokes = 5, Stones = 20, Clouds = 6;
    constexpr float Radius = 200.f, SpokeTurn = 72.f, CloudSpread = 200.f;
    vec3_t position, light{1.f, 1.f, 1.f};
    for (int spoke = 0; spoke < Spokes; ++spoke)
    {
        vec3_t radial{0.f, Radius, 0.f}, rotation{0.f, 0.f, angle[2] + spoke * SpokeTurn};
        float matrix[3][4];
        AngleMatrix(rotation, matrix);
        VectorRotate(radial, matrix, position);
        VectorAdd(position, object.Light, position);
        CreateEffect(BITMAP_JOINT_THUNDER, position, angle, light, 1);
    }
    VectorCopy(object.Light, position);
    CreateEffect(BITMAP_CRATER, position, angle, light, 2, nullptr, -1, 0, 0, 0, 1.5f);
    for (int stone = 0; stone < Stones; ++stone)
        CreateEffect(MODEL_STONE2, position, angle, light);
    Vector(0.7f, 0.7f, 1.f, light);
    for (int cloud = 0; cloud < Clouds; ++cloud)
    {
        vec3_t cloudPosition;
        for (int axis = 0; axis < 3; ++axis)
            cloudPosition[axis] =
                position[axis] + WorldRandom() % int(CloudSpread) - CloudSpread * 0.5f;
        CreateParticle(BITMAP_CLUD64, cloudPosition, angle, light, 7, 2.f);
    }
    Vector(0.3f, 0.2f, 1.f, light);
    CreateEffect(BITMAP_SHOCK_WAVE, position, angle, light, 11);
    CreateEffect(BITMAP_SHOCK_WAVE, position, angle, light, 11);
    constexpr float ThunderHeight = 100.f;
    position[2] += ThunderHeight;
    Vector(0.f, 0.2f, 1.f, light);
    CreateEffect(MODEL_EFFECT_THUNDER_NAPIN_ATTACK_1, position, angle, light, 1);
}

void MoveBehavior::EmitGaionEvent(OBJECT &object, float life)
{
    const bool firstPair = object.Type == MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_ ||
                           object.Type == MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_;
    const bool secondPair = object.Type == MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_ ||
                            object.Type == MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_;
    const bool mainSword = object.Type == MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_;
    const bool frameStrike = object.Type == MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE;
    const float rate = 1.f - life / object.ExtState;
    vec3_t position, angle;
    VectorCopy(object.Position, position);
    VectorCopy(object.Angle, angle);
    float scale = object.Scale;
    if (!object.m_Interpolates.m_vecInterpolatesPos.empty())
        object.m_Interpolates.GetPosCurrent(position, rate);
    if (!object.m_Interpolates.m_vecInterpolatesAngle.empty())
    {
        object.m_Interpolates.GetAngleCurrent(angle, rate);
        VectorAdd(angle, object.HeadAngle, angle);
    }
    if (!object.m_Interpolates.m_vecInterpolatesScale.empty())
        object.m_Interpolates.GetScaleCurrent(scale, rate);
    auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR -
                                                         std::max(0.f, object.LifeTime - life));
    if (object.SubType == 1)
    {
        if (life == object.ExtState - 1 && (firstPair || secondPair))
            PlayBuffer(firstPair ? SOUND_BLOODATTACK : SOUND_ATTACK_MELEE_HIT5);
        if (frameStrike && life == object.ExtState - 5)
            PlayBuffer(SOUND_SKILL_FLAME_STRIKE);
        if ((firstPair && life == 5.f) || (secondPair && life == 3.f) ||
            (frameStrike && life == 10.f) || (mainSword && life == 1.f))
        {
            const int type = frameStrike ? MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_
                             : mainSword ? MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE
                                         : object.Type;
            CreateEffect(type, position, angle, object.Light, mainSword ? 1 : 11, object.Owner, -1,
                         0, 0, 0, scale);
        }
        return;
    }
    if (life == object.ExtState)
        PlayBuffer(SOUND_ASSASSIN);
    if (life == object.ExtState / 2 + 5)
        PlayBuffer(SOUND_FURY_STRIKE2);
    if (life == object.ExtState / 2 + 2)
        PlayBuffer(SOUND_SKILL_GIGANTIC_STORM);
    if (mainSword && life == 40.f)
        EmitGaionImpact(object, angle);
    if (life == 15.f)
        CreateEffect(object.Type, position, angle, object.Light, 12, object.Owner, -1, 0, 0, 0,
                     scale);
    if ((firstPair || secondPair) && life == 0.f)
    {
        auto &owner = *object.Owner;
        owner.MotionTrace.Sample(WorldTime, birth.FrameFraction(), owner.Position, position);
        VectorCopy(owner.Angle, angle);
        angle[2] = owner.MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), angle[2]);
        CreateEffect(object.Type, position, angle, owner.Light, 11, &owner, -1, 0, 0, 0, scale);
    }
}

void MoveBehavior::AdvanceGaionEvents(OBJECT &object)
{
    if (FPS_ANIMATION_FACTOR <= 0.f || object.LifeTime <= 0.f ||
        (object.SubType != 1 && object.SubType != 3))
        return;
    // A small authored event list; equal markers share one emission scope.
    std::array<float, 6> markers = object.SubType == 1
                                       ? std::array<float, 6>{float(object.ExtState - 1),
                                                              float(object.ExtState - 5),
                                                              10.f,
                                                              5.f,
                                                              3.f,
                                                              1.f}
                                       : std::array<float, 6>{float(object.ExtState),
                                                              float(object.ExtState / 2 + 5),
                                                              float(object.ExtState / 2 + 2),
                                                              40.f,
                                                              15.f,
                                                              0.f};
    std::sort(markers.begin(), markers.end(), std::greater<float>());
    for (std::size_t index = 0; index < markers.size(); ++index)
    {
        const float life = markers[index];
        if (index > 0 && life == markers[index - 1])
            continue;
        // Retirement owns an inclusive zero endpoint; ordinary samples keep Reaches' convention.
        const bool crossed = life == 0.f
                                 ? object.LifeTime <= FPS_ANIMATION_FACTOR
                                 : Core::Time::Reaches(object.LifeTime, FPS_ANIMATION_FACTOR, life);
        if (crossed)
            EmitGaionEvent(object, life);
    }
}

bool MoveBehavior::Move_MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_(OBJECT *o, int index,
                                                                     float Luminosity)
{
    AdvanceGaionEvents(*o);
    vec3_t Angle;
    vec3_t Position;
    float Matrix[3][4];
    {
        if (o->SubType == 0)
        {
        }
        else if (o->SubType == 1)
        {
            float fEPSILON = 0.000001f;
            float fRateAlpha_EraseOver = 0.7f;
            vec3_t v3RotateAngleRelative;
            o->Visible = true;

            float fCurrentRate = 1.0f - ((float)o->LifeTime / (float)o->ExtState);

            if (o->m_Interpolates.m_vecInterpolatesAngle.size() > 0)
            {
                o->m_Interpolates.GetAngleCurrent(v3RotateAngleRelative, fCurrentRate);

                o->Angle[0] = v3RotateAngleRelative[0] + o->HeadAngle[0];
                o->Angle[1] = v3RotateAngleRelative[1] + o->HeadAngle[1];
                o->Angle[2] = v3RotateAngleRelative[2] + o->HeadAngle[2];
            }

            // 6. Position
            if (o->m_Interpolates.m_vecInterpolatesPos.size() > 0)
            {
                o->m_Interpolates.GetPosCurrent(o->Position, fCurrentRate);
            }

            // 8. Scale
            if (o->m_Interpolates.m_vecInterpolatesScale.size() > 0)
            {
                o->m_Interpolates.GetScaleCurrent(o->Scale, fCurrentRate);
            }

            // 9. Alpha
            if (o->m_Interpolates.m_vecInterpolatesScale.size() > 0)
            {
                o->m_Interpolates.GetAlphaCurrent(o->Alpha, fCurrentRate);
            }

            float fRateBlurStart, fRateBlurEnd, fRateShadowStart, fRateShadowEnd, fRateJointStart,
                fRateJointEnd;
            fRateBlurStart = fRateBlurEnd = 0.0f;
            fRateShadowStart = fRateShadowEnd = 0.0f;
            fRateJointStart = fRateJointEnd = 0.0f;
            switch (o->Type)
            {
            case MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_: {
                // PlayBuffer ( SOUND_ATTACK_FIRE_BUST_EXP );
                fRateBlurStart = 0.1f;
                fRateBlurEnd = 0.90f;
            }
            break;
            case MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_: {
                fRateBlurStart = 0.1f;
                fRateBlurEnd = 0.90f;
            }
            break;
            case MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_: {
                fRateBlurStart = 0.3f;
                fRateBlurEnd = 1.01f;
                //fRateBlurStart = 0.2f; fRateBlurEnd = 0.90f;

                fRateShadowStart = 0.3f;
                fRateShadowEnd = 1.01f;
                fRateJointStart = 0.25f;
                fRateJointEnd = 0.29f;
            }
            break;
            case MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_: {
                fRateBlurStart = 0.3f;
                fRateBlurEnd = 1.01f;

                fRateShadowStart = 0.3f;
                fRateShadowEnd = 1.01f;
                fRateJointStart = 0.25f;
                fRateJointEnd = 0.29f;
            }
            break;
            case MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_: {
                fRateBlurStart = 0.0f;
                fRateBlurEnd = 0.0f;
            }
            break;
            case MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE: {
                fRateBlurStart = 0.10f;
                fRateBlurEnd = 1.01f;
                //						fRateBlurStart = 0.0f; fRateBlurEnd = 0.0f;
            }
            break;
            }

            int iTYPESWORDFORCE = 0;  // 1: FORCE OF SWORD
            int iTYPESWORDSHADOW = 0; // 1: SHADOW SWORD
            int iTYPESWORDJOINT = 0;  // 1: JOINT OF SWORD

            switch (o->Type)
            {
            case MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE: {
                iTYPESWORDFORCE = 1;
                iTYPESWORDSHADOW = 0;
            }
            break;
            case MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_:
            case MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_:  // ATTACK2
            case MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_: // ATTACK2
            {
                iTYPESWORDFORCE = 1;
                iTYPESWORDSHADOW = 0;
            }
            break;
            case MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_:  // ATTACK1
            case MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_: // ATTACK1
            {
                iTYPESWORDFORCE = 0;
                iTYPESWORDSHADOW = 1;
                iTYPESWORDJOINT = 0;
            }
            break;
            }

            if (iTYPESWORDFORCE == 1)
            {
                if (fCurrentRate > fRateBlurStart && fCurrentRate < fRateBlurEnd)
                {
                    BMD *b = &Models[o->Type];
                    vec3_t vLightBlur;
                    Vector(1.0f, 1.0f, 1.0f, vLightBlur);
                    float fPreRate = 1.0f - (float)((o->LifeTime) + 1) / (float)(o->ExtState);
                    SETLIMITS(fPreRate, 1.0f, 0.0f);
                    if (fPreRate < fCurrentRate)
                    {
                        float fStartRate, fEndRate;

                        fStartRate = 1.0f - (float)((o->LifeTime) + 2) / (float)(o->ExtState);
                        fEndRate = 1.0f - (float)((o->LifeTime) + 1) / (float)(o->ExtState);

                        SETLIMITS(fStartRate, 1.0f, 0.0f);
                        SETLIMITS(fEndRate, 1.0f, 0.0f);

                        vec3_t *arrEachBonePos;
                        arrEachBonePos = new vec3_t[b->NumBones];

                        vec3_t v3CurBlurAngle, v3CurBlurPos;
                        int iAccess = 10;
                        int iBone01, iBone02;
                        int iBlurIdentity, iTypeBlur;
                        int iBlurAccessTimeAttk01, iBlurAccessTimeAttk02, iBlurAccessTimeAttk03,
                            iBlurAccessTimeAttk04;
                        float fUnit;
                        float fCurrentRateUnit = fStartRate;
                        float fScale = o->Scale;

                        iBlurAccessTimeAttk01 = 3;
                        iBlurAccessTimeAttk02 = 20;
                        iBlurAccessTimeAttk03 = 5;
                        iBlurAccessTimeAttk04 = 6;

                        switch (o->Type)
                        {
                        case MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_: {
                            iAccess = iBlurAccessTimeAttk02;
                            iBone01 = 3, iBone02 = 12;
                            iBlurIdentity = 113;
                            //iTypeBlur = 2;
                            iTypeBlur = 13;
                        }
                        break;
                        case MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_: {
                            iAccess = iBlurAccessTimeAttk02;
                            iBone01 = 13, iBone02 = 1;
                            iBlurIdentity = 119;
                            //iTypeBlur = 2;
                            iTypeBlur = 13;
                        }
                        break;
                        case MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_: {
                            iAccess = iBlurAccessTimeAttk01;
                            iBone01 = 4, iBone02 = 6;
                            iBlurIdentity = 133;
                            iTypeBlur = 10;
                        }
                        break;
                        case MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_: {
                            iAccess = iBlurAccessTimeAttk01;
                            iBone01 = 4, iBone02 = 6;
                            iBlurIdentity = 122;
                            iTypeBlur = 10;
                        }
                        break;
                        case MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE: {
                            iAccess = iBlurAccessTimeAttk04;
                            iBone01 = 4, iBone02 = 9;
                            iBlurIdentity = 155;
                            iTypeBlur = 2;
                        }
                        break;
                        } // switch(o->Type)

                        fUnit = (fEndRate - fStartRate) / (float)iAccess;

                        switch (o->Type)
                        {
                        case MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_:
                        case MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_: {
                            for (int i = 0; i < iAccess; i++)
                            {
                                fCurrentRateUnit += fUnit;

                                o->m_Interpolates.GetAngleCurrent(v3CurBlurAngle, fCurrentRateUnit);
                                o->m_Interpolates.GetPosCurrent(v3CurBlurPos, fCurrentRateUnit);

                                VectorAdd(v3CurBlurAngle, o->HeadAngle, v3CurBlurAngle);

                                b->AnimationTransformOnlySelf(arrEachBonePos, v3CurBlurAngle,
                                                              v3CurBlurPos, fScale);

                                CreateObjectBlur(o, arrEachBonePos[iBone01],
                                                 arrEachBonePos[iBone02], vLightBlur, iTypeBlur,
                                                 false, iBlurIdentity, 25);

                                for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(
                                         FPS_ANIMATION_FACTOR / 2.0))
                                {
                                    vec3_t vAngle, vRandomDir, vRandomDirPosition,
                                        vResultRandomPosition;
                                    vec34_t matRandomRotation;
                                    vec3_t vPosition;

                                    if (o->Type == MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_)
                                    {
                                        VectorCopy(arrEachBonePos[12], vPosition);
                                    }
                                    else
                                    {
                                        VectorCopy(arrEachBonePos[1], vPosition);
                                    }

                                    float fRandDistance = (float)(WorldRandom() % 100) + 100;
                                    Vector(0.0f, fRandDistance, 0.0f, vRandomDir);

                                    CreateParticle(BITMAP_FIRE, vPosition, o->Angle, o->Light, 5,
                                                   1.35f);

                                    Vector((float)(WorldRandom() % 360), 0.f,
                                           (float)(WorldRandom() % 360), vAngle);
                                    AngleMatrix(vAngle, matRandomRotation);
                                    VectorRotate(vRandomDir, matRandomRotation, vRandomDirPosition);
                                    VectorAdd(vPosition, vRandomDirPosition, vResultRandomPosition);
                                    CreateJoint(BITMAP_JOINT_THUNDER, vResultRandomPosition,
                                                vPosition, vAngle, 3, NULL, 10.f, 10, 10);
                                }
                            }
                        }
                        break;
                        case MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_:
                        case MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_: {
                            for (int i = 0; i < iAccess; i++)
                            {
                                fCurrentRateUnit += fUnit;

                                o->m_Interpolates.GetAngleCurrent(v3CurBlurAngle, fCurrentRateUnit);
                                o->m_Interpolates.GetPosCurrent(v3CurBlurPos, fCurrentRateUnit);

                                VectorAdd(v3CurBlurAngle, o->HeadAngle, v3CurBlurAngle);

                                b->AnimationTransformOnlySelf(arrEachBonePos, v3CurBlurAngle,
                                                              v3CurBlurPos, fScale);

                                CreateObjectBlur(o, arrEachBonePos[iBone01],
                                                 arrEachBonePos[iBone02], vLightBlur, iTypeBlur,
                                                 false, iBlurIdentity, 3);

                                if (o->Type == MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_ ||
                                    o->Type == MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_)
                                {
                                    vec3_t vAngle, vRandomDir, vRandomDirPosition,
                                        vResultRandomPosition;
                                    vec34_t matRandomRotation;
                                    vec3_t vPosition;

                                    if (o->Type == MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_)
                                    {
                                        VectorCopy(arrEachBonePos[12], vPosition);
                                    }
                                    else
                                    {
                                        VectorCopy(arrEachBonePos[1], vPosition);
                                    }

                                    float fRandDistance = (float)(WorldRandom() % 100) + 100;
                                    Vector(0.0f, fRandDistance, 0.0f, vRandomDir);

                                    CreateParticleFpsChecked(BITMAP_FIRE, vPosition, o->Angle,
                                                             o->Light, 0);

                                    Vector((float)(WorldRandom() % 360), 0.f,
                                           (float)(WorldRandom() % 360), vAngle);
                                    AngleMatrix(vAngle, matRandomRotation);
                                    VectorRotate(vRandomDir, matRandomRotation, vRandomDirPosition);
                                    VectorAdd(vPosition, vRandomDirPosition, vResultRandomPosition);
                                    CreateJointFpsChecked(BITMAP_JOINT_THUNDER,
                                                          vResultRandomPosition, vPosition, vAngle,
                                                          3, NULL, 10.f, 10, 10);
                                }
                            }
                        }
                        break;
                        case MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE: {
                            float fRateBlur = 0.f;

                            for (int i = 0; i < iAccess; i++)
                            {
                                fCurrentRateUnit += fUnit;

                                o->m_Interpolates.GetAngleCurrent(v3CurBlurAngle, fCurrentRateUnit);
                                o->m_Interpolates.GetPosCurrent(v3CurBlurPos, fCurrentRateUnit);

                                VectorAdd(v3CurBlurAngle, o->HeadAngle, v3CurBlurAngle);

                                fRateBlur = (float)i / (float)iAccess;
                                b->AnimationTransformOnlySelf(arrEachBonePos, v3CurBlurAngle,
                                                              v3CurBlurPos, o->Scale, o,
                                                              o->Velocity, fRateBlur);

                                CreateObjectBlur(o, arrEachBonePos[iBone01],
                                                 arrEachBonePos[iBone02], vLightBlur, 13, false,
                                                 iBlurIdentity + 2, 11);
                            }
                        }
                        break;
                        } // Switch(o->Type)

                        delete[] arrEachBonePos;
                    } // if( fPreRate < fCurrentRate )
                }
            }

            if (iTYPESWORDSHADOW == 1)
            {
                if (fCurrentRate > fRateShadowStart && fCurrentRate < fRateShadowEnd)
                {
                    CreateEffectFpsChecked(o->Type, o->Position, o->Angle, o->Light, 20, o, -1, 0,
                                           0, 0, o->Scale);
                }
            }

            if (iTYPESWORDJOINT == 1)
            {
                if (fCurrentRate > fRateJointStart && fCurrentRate < fRateJointEnd)
                {
                    CreateJointFpsChecked(BITMAP_FLARE + 1, o->Position, o->Position, o->Angle, 20,
                                          o, 160.f, 40);
                }
            }

            switch (o->Type)
            {
            case MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_:
            case MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_:
            case MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_:
            case MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_: {
                vec3_t v3LightTerrain;
                switch (o->Type)
                {
                case MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_: {
                    Vector(0.0f, 0.2f, 0.9f, v3LightTerrain);
                    AddTerrainLight(o->Position[0], o->Position[1], v3LightTerrain, 2,
                                    PrimaryTerrainLight);
                }
                break;
                case MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_: {
                    Vector(0.0f, 0.2f, 0.9f, v3LightTerrain);
                    AddTerrainLight(o->Position[0], o->Position[1], v3LightTerrain, 2,
                                    PrimaryTerrainLight);
                }
                break;
                case MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_: {
                    Vector(0.0f, 0.2f, 0.9f, v3LightTerrain);
                    AddTerrainLight(o->Position[0], o->Position[1], v3LightTerrain, 2,
                                    PrimaryTerrainLight);
                }
                break;
                case MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_: {
                    Vector(0.0f, 0.2f, 0.9f, v3LightTerrain);
                    AddTerrainLight(o->Position[0], o->Position[1], v3LightTerrain, 2,
                                    PrimaryTerrainLight);
                }
                break;
                }
            }
            break;
            case MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_: {
                // 3. APPEAR EFFECT
                if (fCurrentRate >= 0.0f && fCurrentRate <= 0.6f)
                {
                    o->Visible = true; // MoveEffect CreateEffect
                    BMD *b = &Models[o->Type];
                    vec3_t *arrEachBonePos;
                    vec3_t v3LightModify;

                    // 1. BonePosition Particle
                    arrEachBonePos = new vec3_t[b->NumBones];

                    // - APPEAR WITH FIRE EFFECT
                    vec3_t vRelativePos, vAngle;
                    vec3_t v3CurrentHighHierarchyNodePos;

                    Vector(4.f, 0.f, 0.0f, vRelativePos);
                    VectorCopy(o->Angle, vAngle);

                    BMD *pBMDSwordModel = &Models[o->Type];
                    BMD *pOwnerModel = &Models[o->Owner->Type];

                    int arrBoneIdxs[] = {4, 2, 8, 10, 6};
                    int iBoneIdx =
                        arrBoneIdxs[o->Type - MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_];

                    pBMDSwordModel->AnimationTransformWithAttachHighModel(
                        o->Owner, pOwnerModel, iBoneIdx, v3CurrentHighHierarchyNodePos,
                        arrEachBonePos);

                    // - END EFFECT
                    vec3_t v3LightTerrain;
                    Vector(0.9f, 0.4f, 0.1f, v3LightTerrain);

                    AddTerrainLight(o->Position[0], o->Position[1], v3LightTerrain, 2,
                                    PrimaryTerrainLight);

                    {
                        Vector(1.0f, 1.0f, 1.0f, v3LightModify);

                        for (int j = 0; j < 11; ++j)
                        {
                            for (auto birthTime :
                                 sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 20.0))
                            {
                                CreateEffect(BITMAP_FIRE_CURSEDLICH, arrEachBonePos[j], vAngle,
                                             v3LightModify, 12, o, -1, 0, 0, 0, o->Scale);
                            }
                        }
                    }

                    delete[] arrEachBonePos;
                } // if( fCurrentRate >= 0.0f && fCurrentRate <= 0.01f )
            }
            break;
            case MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE: {
                if (fCurrentRate >= 0.0f && fCurrentRate <= 0.5f)
                {
                    // - APPEAR WITH FIRE EFFECT
                    vec3_t vAngle;
                    vec3_t v3LightModify, v3PosModify;

                    // 1. BonePosition Particle
                    BMD *b = &Models[o->Type];
                    auto *arrEachBonePos = new vec3_t[b->NumBones];

                    b->AnimationTransformOnlySelf(arrEachBonePos, o->Angle, o->Position, o->Scale,
                                                  o);

                    VectorCopy(o->Angle, vAngle);
                    Vector(1.0f, 1.0f, 1.0f, v3LightModify);

                    for (int i = 0; i < b->NumBones; i++)
                    {
                        v3PosModify[0] = arrEachBonePos[i][0] + (float)((WorldRandom() % 80) - 40);
                        v3PosModify[1] = arrEachBonePos[i][1] + (float)((WorldRandom() % 80) - 40);
                        v3PosModify[2] = arrEachBonePos[i][2] + (float)((WorldRandom() % 80) - 40);
                        for (auto birthTime :
                             sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.0))
                        {
                            CreateEffect(BITMAP_FIRE_CURSEDLICH, v3PosModify, vAngle, v3LightModify,
                                         12, o, -1, 0, 0, 0, o->Scale * 1.5f);
                        }
                    }

                    delete[] arrEachBonePos;
                }

                vec3_t v3LightTerrain;
                Vector(0.9f, 0.4f, 0.1f, v3LightTerrain);

                AddTerrainLight(o->Position[0], o->Position[1], v3LightTerrain, 2,
                                PrimaryTerrainLight);
            }
            break;
            }
        }
        else if (o->SubType == 3)
        {
            o->Visible = true; // MoveEffect
            vec3_t v3RotateAngleRelative;

            // 2. RATE %
            float fCurrentRate = 1.0f - ((float)o->LifeTime / (float)o->ExtState);

            // 4. Angle
            if (o->m_Interpolates.m_vecInterpolatesAngle.size() > 0)
            {
                o->m_Interpolates.GetAngleCurrent(v3RotateAngleRelative, fCurrentRate);

                o->Angle[0] = v3RotateAngleRelative[0] + o->HeadAngle[0];
                o->Angle[1] = v3RotateAngleRelative[1] + o->HeadAngle[1];
                o->Angle[2] = v3RotateAngleRelative[2] + o->HeadAngle[2];
            }

            // 6. Position
            if (o->m_Interpolates.m_vecInterpolatesPos.size() > 0)
            {
                o->m_Interpolates.GetPosCurrent(o->Position, fCurrentRate);
            }

            // 8. Scale
            if (o->m_Interpolates.m_vecInterpolatesScale.size() > 0)
            {
                o->m_Interpolates.GetScaleCurrent(o->Scale, fCurrentRate);
            }

            // 9. Alpha
            if (o->m_Interpolates.m_vecInterpolatesAlpha.size() > 0)
            {
                o->m_Interpolates.GetAlphaCurrent(o->Alpha, fCurrentRate);
            }

            switch (o->Type)
            {
            case MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_:
            case MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_:
            case MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_:
            case MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_: {
                // - COLOR.
                vec3_t v3LightTerrain;
                Vector(0.0f, 0.2f, 0.9f, v3LightTerrain);
                AddTerrainLight(o->Position[0], o->Position[1], v3LightTerrain, 2,
                                PrimaryTerrainLight);
            }
            break;
            }

            int iSwordOnTheLand = (o->ExtState / 2) + 5;
            if (o->LifeTime <= iSwordOnTheLand && o->LifeTime > 10)
            {
                EarthQuake = (float)(WorldRandom() % 2 - 2) * 0.5f;
            }

        }
        else if (o->SubType == 11)
        {
            float fCurrentRate = 1.0f - ((float)o->LifeTime / (float)o->ExtState);

            if (o->m_Interpolates.m_vecInterpolatesScale.size() > 0)
            {
                o->m_Interpolates.GetAlphaCurrent(o->Alpha, fCurrentRate);
            }

            // 13. APPEAR EFFECT들
            if (fCurrentRate >= 0.0f && fCurrentRate <= 0.5f)
            {
                o->Visible = true; // MoveEffect CreateEffect
                BMD *b = &Models[o->Type];
                vec3_t *arrEachBonePos;
                vec3_t v3LightModify;

                // 1. BonePosition Particle
                arrEachBonePos = new vec3_t[b->NumBones];

                // - APPEAR WITH FIRE EFFECT
                vec3_t vRelativePos, vAngle;
                vec3_t v3CurrentHighHierarchyNodePos;

                Vector(4.f, 0.f, 0.0f, vRelativePos);
                VectorCopy(o->Angle, vAngle);

                BMD *pBMDSwordModel = &Models[o->Type];
                BMD *pOwnerModel = &Models[o->Owner->Type];

                int arrBoneIdxs[] = {4, 2, 8, 10, 6}; //INDEX.
                int iBoneIdx = arrBoneIdxs[o->Type - MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_];

                pBMDSwordModel->AnimationTransformWithAttachHighModel(
                    o->Owner, pOwnerModel, iBoneIdx, v3CurrentHighHierarchyNodePos, arrEachBonePos);

                vec3_t v3LightTerrain;
                Vector(0.9f, 0.4f, 0.1f, v3LightTerrain);

                AddTerrainLight(o->Position[0], o->Position[1], v3LightTerrain, 2,
                                PrimaryTerrainLight);

                switch (o->Type)
                {
                case MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_:
                case MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_: {
                    for (int j = 0; j < 11; ++j)
                    {
                        Vector(1.0f, 1.0f, 1.0f, v3LightModify);
                        for (auto birthTime :
                             sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
                        {
                            CreateParticle(BITMAP_POUNDING_BALL, arrEachBonePos[j], o->Angle,
                                           v3LightModify, 3, 1.3f);
                        }
                    }
                }
                break;
                case MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_:
                case MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_: {
                    Vector(0.3f, 0.4f, 1.0f, v3LightModify);

                    for (int j = 0; j < 11; ++j)
                    {
                        for (auto birthTime :
                             sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
                        {
                            CreateParticle(BITMAP_FIRE_HIK1_MONO, arrEachBonePos[j], o->Angle,
                                           v3LightModify, 10, o->Scale);
                        }
                    }
                }
                break;
                case MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_: {
                    Vector(1.0f, 1.0f, 1.0f, v3LightModify);

                    for (int j = 0; j < 11; ++j)
                    {
                        for (auto birthTime :
                             sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 20.0))
                        {
                            CreateEffect(BITMAP_FIRE_CURSEDLICH, arrEachBonePos[j], vAngle,
                                         v3LightModify, 12, o, -1, 0, 0, 0, o->Scale);
                        }
                    }
                }
                break;
                default: {
                }
                break;
                }
                // 3. APPEAR EFFECT

                delete[] arrEachBonePos;
            } // if( fCurrentRate >= 0.0f && fCurrentRate <= 0.01f )

            // 2. ALpha
            if (o->m_Interpolates.m_vecInterpolatesAlpha.size() > 0)
            {
                o->m_Interpolates.GetAlphaCurrent(o->Alpha, fCurrentRate);
            }
        } // else if( o->SubType==11 )
        else if (o->SubType == 12) // END EFFECT
        {
            // 2. RATE %
            float fCurrentRate = 1.0f - ((float)o->LifeTime / (float)o->ExtState);

            // 9. (4) Alpha
            if (o->m_Interpolates.m_vecInterpolatesScale.size() > 0)
            {
                o->m_Interpolates.GetAlphaCurrent(o->Alpha, fCurrentRate);
            }

            // 3. APPEAR EFFECT들
            if (fCurrentRate >= 0.0f && fCurrentRate <= 0.6f)
            {
                o->Visible = true; // MoveEffect CreateEffect
                BMD *b = &Models[o->Type];
                vec3_t *arrEachBonePos; // Bone
                vec3_t v3LightModify;

                // 1. BonePosition Particle
                arrEachBonePos = new vec3_t[b->NumBones];

                // - APPEAR WITH FIRE EFFECT
                vec3_t vRelativePos, vAngle;
                vec3_t v3CurrentHighHierarchyNodePos; //ATTACH

                Vector(4.f, 0.f, 0.0f, vRelativePos);
                VectorCopy(o->Angle, vAngle);

                BMD *pBMDSwordModel = &Models[o->Type];
                BMD *pOwnerModel = &Models[o->Owner->Type];

                int arrBoneIdxs[] = {4, 2, 8, 10, 6}; //INDEX.
                int iBoneIdx = arrBoneIdxs[o->Type - MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_];

                pBMDSwordModel->AnimationTransformWithAttachHighModel(
                    o->Owner, pOwnerModel, iBoneIdx, v3CurrentHighHierarchyNodePos, arrEachBonePos);

                // EFFECT
                vec3_t v3LightTerrain;
                Vector(0.9f, 0.4f, 0.1f, v3LightTerrain);

                AddTerrainLight(o->Position[0], o->Position[1], v3LightTerrain, 2,
                                PrimaryTerrainLight);

                {
                    Vector(0.3f, 0.4f, 1.0f, v3LightModify);
                    for (int j = 0; j < 11; ++j)
                    {
                        for (auto birthTime :
                             sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.0))
                        {
                            CreateParticle(BITMAP_FIRE_HIK1_MONO, arrEachBonePos[j], o->Angle,
                                           v3LightModify, 10, o->Scale);
                        }
                    }
                }
                delete[] arrEachBonePos;
            }

            // 2. ALpha
            if (o->m_Interpolates.m_vecInterpolatesAlpha.size() > 0)
            {
                o->m_Interpolates.GetAlphaCurrent(o->Alpha, fCurrentRate);
            }
        } // else if( o->SubType==11 )
        else if (o->SubType == 20) //ALPHA
        {
            o->Visible = true; // MoveEffect CreateEffect Move
            //float	fRateStatic = 0.6f;
            float fCurrentRate = (float)(o->LifeTime) / (float)(o->ExtState);
            vec3_t v3RotateAngleRelative;

            if (o->m_Interpolates.m_vecInterpolatesAngle.size() > 0)
            {
                o->m_Interpolates.GetAngleCurrent(v3RotateAngleRelative, fCurrentRate);

                o->Angle[0] = v3RotateAngleRelative[0];
                o->Angle[1] = v3RotateAngleRelative[1];
                o->Angle[2] = v3RotateAngleRelative[2];
            }

            // 6. Position
            if (o->m_Interpolates.m_vecInterpolatesPos.size() > 0)
            {
                o->m_Interpolates.GetPosCurrent(o->Position, fCurrentRate);
            }

            // 8. Scale
            if (o->m_Interpolates.m_vecInterpolatesScale.size() > 0)
            {
                o->m_Interpolates.GetScaleCurrent(o->Scale, fCurrentRate);
            }

            // 9. Alpha
            if (o->m_Interpolates.m_vecInterpolatesAlpha.size() > 0)
            {
                o->m_Interpolates.GetAlphaCurrent(o->Alpha, fCurrentRate);
            }
        }
    }

    return true;
}

// MODEL_EMPIREGUARDIAN_BLOW_OF_DESTRUCTION
bool MoveBehavior::Move_MODEL_EMPIREGUARDIAN_BLOW_OF_DESTRUCTION(OBJECT *o, int index,
                                                                 float Luminosity)
{
    float Matrix[3][4];
    {
        if (o->SubType == 0)
        {
            vec3_t vLight;
            if (o->LifeTime <= 24)
            {
                if (o->LifeTime >= 15.f)
                {
                    EarthQuake = (float)(WorldRandom() % 8 - 4) * 0.1f;
                }
                else
                {
                    EarthQuake = 0.f;
                }

                if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 23))
                {
                    auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                        FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 23.f));
                    Vector(0.3f, 0.3f, 1.0f, vLight);
                    //CreateEffect(MODEL_NIGHTWATER_01, o->Position, o->Angle, vLight, 0, o);
                    //CreateEffect(MODEL_NIGHTWATER_01, o->Position, o->Angle, vLight, 0, o);

                    CreateEffect(MODEL_NIGHTWATER_01, o->Position, o->Angle, vLight, 0, o, o->PKKey,
                                 o->Kind, o->Skill, o->m_bySkillSerialNum, o->Scale * 1.0f);
                    CreateEffect(MODEL_NIGHTWATER_01, o->Position, o->Angle, vLight, 0, o, o->PKKey,
                                 o->Kind, o->Skill, o->m_bySkillSerialNum, o->Scale * 1.0f);

                    CreateEffect(MODEL_KNIGHT_PLANCRACK_A, o->Position, o->Angle, vLight, 0, o, 0,
                                 0, 0, 0, o->Scale * 0.2f);

                    vec3_t vDir, vPos, vAngle;
                    VectorSubtract(o->StartPosition, o->Position, vDir);
                    float fLength = VectorLength(vDir);
                    VectorNormalize(vDir);
                    int iNum = (int)(fLength / 100) + 1;
                    for (int i = 0; i < iNum; ++i)
                    {
                        VectorScale(vDir, 55.f * i, vPos);
                        VectorAdd(o->Position, vPos, vPos);
                        VectorCopy(o->Owner->Angle, vAngle);
                        if (i % 2 == 0)
                        {
                            vAngle[2] += (WorldRandom() % 20 + 10);
                        }
                        else
                        {
                            vAngle[2] -= (WorldRandom() % 20 + 10);
                        }
                        CreateEffect(MODEL_KNIGHT_PLANCRACK_B, vPos, vAngle, vLight, 0, o, 0, 0, 0,
                                     0, o->Scale);
                    }
                }

                o->Light[0] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            }
        }
        else if (o->SubType == 1)
        {
            if (o->LifeTime <= 24)
            {
                if (o->LifeTime >= 15)
                {
                    vec3_t vPos, vLight;
                    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
                    {
                        for (int i = 0; i < 15; ++i)
                        {
                            VectorCopy(o->Position, vPos);
                            vPos[0] += WorldRandom() % 400 - 200;
                            vPos[1] += WorldRandom() % 400 - 200;
                            vPos[2] += WorldRandom() % 400 - 200;
                            float fScale = (o->Scale * 1.6f) + WorldRandom() % 10 * 0.1f;
                            Vector(0.3f, 0.3f, 1.0f, vLight);
                            int index =
                                (WorldRandom() % 2) ? BITMAP_WATERFALL_5 : BITMAP_WATERFALL_3;
                            CreateParticle(index, vPos, o->Angle, vLight, 8, fScale);
                            Vector(1.0f, 1.0f, 1.0f, vLight);
                            if (WorldRandom() % 2 == 0)
                            {
                                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, 55,
                                               o->Scale * 0.9f);
                            }
                        }
                    }
                }

                if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 23))
                {
                    auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                        FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 23.f));
                    vec3_t vLight;
                    Vector(0.5f, 0.5f, 1.f, vLight);

                    Vector(0.3f, 0.3f, 1.0f, vLight);
                    CreateEffect(MODEL_NIGHTWATER_01, o->Position, o->Angle, vLight, 0, o, -1, 0, 0,
                                 0, o->Scale * 1.1f);
                    CreateEffect(MODEL_NIGHTWATER_01, o->Position, o->Angle, vLight, 0, o, -1, 0, 0,
                                 0, o->Scale * 1.2f);
                    CreateEffect(MODEL_RAKLION_BOSS_CRACKEFFECT, o->Position, o->Angle, vLight, 0,
                                 o, -1, 0, 0, 0, o->Scale * 0.4f);

                    vec3_t vPos, vResult;
                    vec3_t vAngle;
                    float Matrix[3][4];
                    for (int i = 0; i < (5 + WorldRandom() % 3); ++i)
                    {
                        Vector(0.f, (float)(WorldRandom() % 150), 0.f, vPos);
                        Vector(0.f, 0.f, (float)(WorldRandom() % 360), vAngle);
                        AngleMatrix(vAngle, Matrix);
                        VectorRotate(vPos, Matrix, vResult);
                        VectorAdd(vResult, o->Position, vResult);

                        CreateEffect(MODEL_STONE1 + WorldRandom() % 2, vResult, o->Angle, o->Light,
                                     13);
                    }
                }

                o->Light[0] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
                o->Light[1] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
                o->Light[2] *= pow(1.0f / (1.05f), FPS_ANIMATION_FACTOR);
            }
        }
    }
    return true;
}

// MODEL_EFFECT_SD_AURA
bool MoveBehavior::Move_MODEL_EFFECT_SD_AURA(OBJECT *o, int index, float Luminosity)
{
    {
        vec3_t Temp_Pos;
        BMD *b = &Models[o->Owner->Type];
        b->TransformByObjectBone(Temp_Pos, o->Owner, 18);
        Temp_Pos[2] -= 30.0f;
        VectorCopy(Temp_Pos, o->Position);
        o->LifeTime = 100;
    }
    return true;
}

// BITMAP_WATERFALL_4
bool MoveBehavior::Move_BITMAP_WATERFALL_4(OBJECT *o, int index, float Luminosity)
{
    {
        vec3_t Temp_Pos;
        BMD *b = &Models[o->Owner->Type];
        b->TransformByObjectBone(Temp_Pos, o->Owner, o->PKKey);

        o->Timer += (0.1f) * FPS_ANIMATION_FACTOR;
        o->Distance += (2.1f) * FPS_ANIMATION_FACTOR;
        o->Angle[0] += (0.7f) * FPS_ANIMATION_FACTOR;

        o->Light[0] *= pow(0.95f, FPS_ANIMATION_FACTOR);
        o->Light[1] *= pow(0.95f, FPS_ANIMATION_FACTOR);
        o->Light[2] *= pow(0.95f, FPS_ANIMATION_FACTOR);
        //o->Alpha *= pow(0.92f, FPS_ANIMATION_FACTOR);

        vec3_t vPos;
        for (int i = 0; i < 3; i++)
        {
            switch (i)
            {
            case 0:
                vPos[0] = cosf(o->Timer) * o->Distance + Temp_Pos[0];
                vPos[1] = Temp_Pos[1];
                vPos[2] = sinf(o->Timer) * o->Distance + Temp_Pos[2];
                break;
            case 1:
                vPos[0] = sinf(o->Timer) * o->Distance + Temp_Pos[0];
                vPos[1] = cosf(o->Timer) * o->Distance + Temp_Pos[1];
                vPos[2] = Temp_Pos[2];
                break;
            case 2:
                vPos[0] = Temp_Pos[0];
                vPos[1] = sinf(o->Timer) * o->Distance + Temp_Pos[1];
                vPos[2] = cosf(o->Timer) * o->Distance + Temp_Pos[2];
                break;
            }

            CreateSprite(BITMAP_WATERFALL_4, vPos, o->Scale, o->Light, o, o->Angle[0]);
        }
    }
    return true;
}

// MODEL_WOLF_HEAD_EFFECT
bool MoveBehavior::Move_MODEL_WOLF_HEAD_EFFECT(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t p;
    {
        if (o->SubType == 0)
        {
            o->BlendMeshLight = (float)o->LifeTime / 20.0f;
            o->Alpha = o->BlendMeshLight;
            vec3_t Angle;
            if (o->LifeTime >= 3)
            {
                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
                {
                    VectorCopy(o->Owner->Angle, Angle);
                    int nRandom = WorldRandom() % 6;
                    float _Value = 7.0f + WorldRandom() % 3;
                    if (nRandom / 3)
                        o->Owner->Angle[2] = Angle[2] + (_Value * (nRandom % 3 * 2 + 1));
                    else
                        o->Owner->Angle[2] = Angle[2] - (_Value * (nRandom % 3 * 2 + 1));

                    int _Temp = WorldRandom() % 3;
                    if (_Temp == 1)
                        o->Owner->Angle[0] = Angle[0] + (_Value * (nRandom % 3 * 2 + 1));
                    else if (_Temp == 2)
                        o->Owner->Angle[0] = Angle[0] - (_Value * (nRandom % 3 * 2 + 1));

                    for (int i = 0; i < 3; i++)
                    {
                        BMD *b = &Models[o->Owner->Type];
                        b->TransformByObjectBone(p, o->Owner, 27);
                        VectorCopy(p, o->Position);
                        o->Scale = 1.5f;
                        Vector(0.2f, 0.3f, 1.0f, Light);
                        p[1] += WorldRandom() % 20 - 10;
                        p[2] += WorldRandom() % 28 + 6;
                        CreateJoint(BITMAP_PIN_LIGHT, p, p, o->Owner->Angle, 0, o->Owner, 5.0f);
                    }
                    CreateEffect(MODEL_WOLF_HEAD_EFFECT, o->Owner->Position, o->Owner->Angle,
                                 o->Owner->Light, 1, o->Owner);
                    CreateEffect(MODEL_WOLF_HEAD_EFFECT, o->Owner->Position, o->Owner->Angle,
                                 o->Owner->Light, 2, o->Owner);
                    o->Owner->Angle[2] = Angle[2];
                    o->Owner->Angle[0] = Angle[0];
                }
            }
        }
        else if (o->SubType == 1)
        {
            o->Owner->Velocity += (2.5f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 2)
        {
            o->Owner->Velocity += (5.5f) * FPS_ANIMATION_FACTOR;
        }
    }
    return true;
}

// BITMAP_SBUMB
bool MoveBehavior::Move_BITMAP_SBUMB(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->LifeTime < 17)
        {
            vec3_t vLight, vPosition;
            Vector(1.0f, 1.0f, 1.0f, vLight);
            if (o->Owner->m_sTargetIndex < 0)
                return true;

            auto &target = CharactersClient[o->Owner->m_sTargetIndex].Object;
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                target.MotionTrace.Sample(WorldTime, birth.FrameFraction(), target.Position,
                                          vPosition);
                vPosition[0] += WorldRandom() % 80 - 40;
                vPosition[1] += WorldRandom() % 80 - 40;
                vPosition[2] += WorldRandom() % 120 + 30;
                const float scale = 1.5f + WorldRandom() % 10 * 0.02f;
                CreateParticle(BITMAP_SBUMB, vPosition, o->Angle, vLight, 0, scale * o->Scale,
                               o->Owner);
            }
        }
        if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 15) ||
            Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 9) ||
            Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 6))
        {
            StopBuffer(SOUND_RAGESKILL_THRUST_ATTACK, true);
            PlayBuffer(SOUND_RAGESKILL_THRUST_ATTACK);
        }
    }
    return true;
}

// MODEL_DOWN_ATTACK_DUMMY_L
bool MoveBehavior::Move_MODEL_DOWN_ATTACK_DUMMY_L(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    {
        if (o->Owner->Live)
        {
            BMD *pModel = &Models[o->Type];
            pModel->Animation(BoneTransform, o->AnimationFrame, o->PriorAnimationFrame,
                              o->PriorAction, o->Angle, o->HeadAngle, false, false);

            if (o->AnimationFrame >= 5.0f && o->LifeTime > 50)
            {
                if (o->Owner->m_sTargetIndex < 0)
                    return true;
                CreateBomb3(CharactersClient[o->Owner->m_sTargetIndex].Object.Position, 3, 0.9f);
                StopBuffer(SOUND_RAGESKILL_STAMP_ATTACK, true);
                PlayBuffer(SOUND_RAGESKILL_STAMP_ATTACK);
                o->LifeTime = 40;
            }

            if (o->AnimationFrame >= 8.0f)
            {
                RetireEffect(o);
                return false;
            }

            vec3_t _StartPos, _EndPos;
            pModel->TransformByObjectBone(_StartPos, o, 0);
            CreateJointFpsChecked(BITMAP_FORCEPILLAR, _StartPos, _StartPos, o->Angle, 0, o, 50.0f);

            VectorCopy(o->Owner->Angle, o->Angle);
            VectorCopy(o->Owner->Position, o->Position);

            vec3_t Light;
            Vector(0.47f, 0.36f, 0.24f, Light);
            pModel->TransformByObjectBone(_EndPos, o, 0);
            CreateSprite(BITMAP_FLARE, _EndPos, 5.0f, Light, o);
        }
        else
        {
            RetireEffect(o);
        }
    }
    return true;
}

// MODEL_DOWN_ATTACK_DUMMY_R
bool MoveBehavior::Move_MODEL_DOWN_ATTACK_DUMMY_R(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    {
        if (o->Owner->Live)
        {
            BMD *pModel = &Models[o->Type];
            pModel->Animation(BoneTransform, o->AnimationFrame, o->PriorAnimationFrame,
                              o->PriorAction, o->Angle, o->HeadAngle, false, false);

            if (o->AnimationFrame >= 6.0f || !o->Owner->Live)
            {
                RetireEffect(o);
                CreateEffect(MODEL_DOWN_ATTACK_DUMMY_L, o->Owner->Position, o->Owner->Angle,
                             o->Owner->Light, 0, o->Owner);
                return true;
            }

            if (o->AnimationFrame >= 3.0f && o->LifeTime > 50)
            {
                if (o->Owner->m_sTargetIndex < 0)
                    return true;
                CreateBomb3(CharactersClient[o->Owner->m_sTargetIndex].Object.Position, 3, 0.9f);
                StopBuffer(SOUND_RAGESKILL_STAMP_ATTACK, true);
                PlayBuffer(SOUND_RAGESKILL_STAMP_ATTACK);
                o->LifeTime = 40;
            }

            vec3_t _StartPos, _EndPos;
            pModel->TransformByObjectBone(_StartPos, o, 0);
            CreateJointFpsChecked(BITMAP_FORCEPILLAR, _StartPos, _StartPos, o->Angle, 1, o, 50.0f);

            VectorCopy(o->Owner->Angle, o->Angle);
            VectorCopy(o->Owner->Position, o->Position);

            vec3_t Light;
            Vector(0.47f, 0.36f, 0.24f, Light);
            pModel->TransformByObjectBone(_EndPos, o, 0);
            CreateSprite(BITMAP_FLARE, _EndPos, 5.0f, Light, o);
        }
        else
        {
            RetireEffect(o);
        }
    }
    return true;
}

// BITMAP_SWORDEFF
bool MoveBehavior::Move_BITMAP_SWORDEFF(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    {
        if (o->Owner->AnimationFrame >
            Models[o->Owner->Type].Actions[PLAYER_SKILL_GIANTSWING].NumAnimationKeys)
            RetireEffect(o);

        BMD *b = &Models[o->Owner->Type];
        vec3_t _StartPos, _EndPos;
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            SampleOwnerBone(*o->Owner, *b, 5, WorldTime, birth.FrameFraction(), _StartPos);
            SampleOwnerBone(*o->Owner, *b, 4, WorldTime, birth.FrameFraction(), _EndPos);
            CreateJoint(BITMAP_SWORDEFF, _StartPos, _EndPos, o->Owner->Angle, 0, o->Owner, 100.0f);
        }

        if (o->Owner->AnimationFrame > 6 && o->LifeTime > 50)
        {
            o->LifeTime = 45;
            vec3_t Light, Angle;
            Vector(0.4f, 0.5f, 1.0f, Light);
            VectorCopy(o->Owner->Angle, Angle);

            vec3_t position, Relative;
            Vector(-180.0f, 100.0f, 0.0f, Relative);
            b->TransformByObjectBone(position, o->Owner, 0, Relative);

            Vector(60.0f, 0.0f, Angle[2] - 180, Angle);
            CreateEffect(MODEL_SHOCKWAVE01, position, Angle, Light, 0, o->Owner, -1, 0, 0, 0, 1.0f);
            CreateEffect(MODEL_WOLF_HEAD_EFFECT2, o->Position, o->Angle, o->Light, 3, o->Owner);

            if (o->Owner->m_sTargetIndex < 0)
                return true;

            CreateEffect(BITMAP_DAMAGE1, CharactersClient[o->Owner->m_sTargetIndex].Object.Position,
                         Angle, Light, 0, o->Owner, -1, 0, 0, 0, 1.4f);
            StopBuffer(SOUND_RAGESKILL_GIANTSWING_ATTACK, true);
            PlayBuffer(SOUND_RAGESKILL_GIANTSWING_ATTACK);
        }

        if (o->Owner->AnimationFrame > 11 && o->LifeTime > 20)
        {
            o->LifeTime = 20;
            vec3_t Light, Angle, vPosition;
            Vector(0.5f, 0.5f, 1.0f, Light);
            VectorCopy(o->Owner->Angle, Angle);
            vec3_t position, Relative;
            Vector(-80.0f, 120.0f, 0.0f, Relative);
            b->TransformByObjectBone(position, o->Owner, 4, Relative);
            Vector(90.f, 0.0f, Angle[2] - 180, Angle);
            CreateEffect(MODEL_SHOCKWAVE02, position, Angle, Light, 0, o->Owner, -1, 0, 0, 0, 0.5f);
            CreateEffect(MODEL_WOLF_HEAD_EFFECT2, o->Position, o->Angle, o->Light, 5, o->Owner);

            Vector(1.0f, 1.0f, 1.0f, Light);
            Vector(-80.0f, 20.0f, 0.0f, vPosition);
            b->TransformByObjectBone(vPosition, o->Owner, 4, vPosition);
            Vector(90.0f, Angle[1], Angle[2], Angle);
            CreateEffect(MODEL_SHOCKWAVE_SPIN01, vPosition, Angle, Light, 0, o->Owner, -1, 0, 0, 0,
                         0.5f);
            CreateEffect(MODEL_SHOCKWAVE_SPIN01, vPosition, Angle, Light, 1, o->Owner, -1, 0, 0, 0,
                         0.9f);

            if (o->Owner->m_sTargetIndex < 0)
                return true;

            VectorCopy(CharactersClient[o->Owner->m_sTargetIndex].Object.Position, vPosition);
            CreateEffect(BITMAP_SHINY + 4, vPosition, o->Angle, Light, 0, o->Owner, -1, 0, 0, 0,
                         1.9f);
            StopBuffer(SOUND_RAGESKILL_GIANTSWING_ATTACK, true);
            PlayBuffer(SOUND_RAGESKILL_GIANTSWING_ATTACK);
        }
    }
    return true;
}

// MODEL_SHOCKWAVE01
bool MoveBehavior::Move_MODEL_SHOCKWAVE01(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->SubType == 1 || o->SubType == 2)
        {
            o->Alpha = 1.0f;
            o->Scale *= pow(1.24f, FPS_ANIMATION_FACTOR);
            o->Position[2] *= pow(1.19f, FPS_ANIMATION_FACTOR);
            VectorScale(o->Light, std::pow(0.45f, FPS_ANIMATION_FACTOR), o->Light);
            if (o->Light[0] < 0.001f)
            {
                Vector(0.0f, 0.0f, 0.0f, o->Light);
            }
        }
        else if (o->SubType == 3)
        {
            o->Scale *= pow(1.15f, FPS_ANIMATION_FACTOR);
            VectorAddScaled(o->Position, o->StartPosition, o->Position,
                            -0.99f * Core::Time::DampedDistance(0.99f, FPS_ANIMATION_FACTOR));
            VectorScale(o->StartPosition, std::pow(0.99f, FPS_ANIMATION_FACTOR), o->StartPosition);
            VectorScale(o->Light, std::pow(0.7f, FPS_ANIMATION_FACTOR), o->Light);
            if (o->Light[0] > 0.6f)
            {
                o->SubType = 5;
            }
            o->Position[2] = 180.0f + o->Scale * 80.0f;
        }
        else if (o->SubType == 4)
        {
            o->Scale *= pow(1.15f, FPS_ANIMATION_FACTOR);
            VectorAddScaled(o->Position, o->StartPosition, o->Position,
                            -1.04f * Core::Time::DampedDistance(1.04f, FPS_ANIMATION_FACTOR));
            VectorScale(o->StartPosition, std::pow(1.04f, FPS_ANIMATION_FACTOR), o->StartPosition);
            VectorScale(o->Light, std::pow(1.5f, FPS_ANIMATION_FACTOR), o->Light);
            if (o->Light[0] > 0.6f)
            {
                o->SubType = 5;
            }
            o->Position[2] = 160.0f + o->Scale * 10.0f;
        }
        else if (o->SubType == 5)
        {
            o->Scale *= pow(1.15f, FPS_ANIMATION_FACTOR);
            VectorAddScaled(o->Position, o->StartPosition, o->Position,
                            -1.04f * Core::Time::DampedDistance(1.04f, FPS_ANIMATION_FACTOR));
            VectorScale(o->StartPosition, std::pow(1.04f, FPS_ANIMATION_FACTOR), o->StartPosition);
            VectorScale(o->Light, std::pow(0.5f, FPS_ANIMATION_FACTOR), o->Light);
            o->Position[2] = 160.0f + o->Scale * 10.0f;
        }
        else
        {
            o->Alpha = 1.0f;
            o->Scale += (0.2f) * FPS_ANIMATION_FACTOR;
            VectorAddScaled(o->Position, o->StartPosition, o->Position,
                            -0.99f * Core::Time::DampedDistance(0.99f, FPS_ANIMATION_FACTOR));
            VectorScale(o->StartPosition, std::pow(0.99f, FPS_ANIMATION_FACTOR), o->StartPosition);
            VectorScale(o->Light, std::pow(0.65f, FPS_ANIMATION_FACTOR), o->Light);
            o->Position[2] = 220.0f + o->Scale * 80.0f;
        }
    }
    return true;
}

// MODEL_SHOCKWAVE02
bool MoveBehavior::Move_MODEL_SHOCKWAVE02(OBJECT *o, int index, float Luminosity)
{
    {
        o->Alpha = 1.0f;
        VectorScale(o->Light, std::pow(0.8f, FPS_ANIMATION_FACTOR), o->Light);
        o->Scale += (0.1f) * FPS_ANIMATION_FACTOR;
        VectorAddScaled(o->Position, o->StartPosition, o->Position,
                        -0.95f * Core::Time::DampedDistance(0.95f, FPS_ANIMATION_FACTOR));
        VectorScale(o->StartPosition, std::pow(0.95f, FPS_ANIMATION_FACTOR), o->StartPosition);
    }
    return true;
}

// BITMAP_DAMAGE1
bool MoveBehavior::Move_BITMAP_DAMAGE1(OBJECT *o, int index, float Luminosity)
{
    if (o->Owner->m_sTargetIndex < 0)
        return true;
    OBJECT &target = CharactersClient[o->Owner->m_sTargetIndex].Object;
    constexpr float PulseLives[] = {16.f, 12.f, 8.f, 4.f};
    for (float life : PulseLives)
    {
        if (!Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life))
            continue;
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR -
                                                             std::max(0.f, o->LifeTime - life));
        vec3_t light{0.6f, 0.94f, 1.f}, position;
        target.MotionTrace.Sample(WorldTime, birth.FrameFraction(), target.Position, position);
        position[0] += WorldRandom() % 90 - 40;
        position[1] += WorldRandom() % 90 - 40;
        position[2] += WorldRandom() % 100 + 50;
        const float scale = 1.5f + WorldRandom() % 5 * 0.01f;
        CreateParticle(BITMAP_DAMAGE1, position, o->Angle, light, 0, scale * o->Scale, o->Owner);
    }
    return true;
}

// MODEL_SHOCKWAVE_SPIN01
bool MoveBehavior::Move_MODEL_SHOCKWAVE_SPIN01(OBJECT *o, int index, float Luminosity)
{
    {
        o->Scale *= pow(1.01f, FPS_ANIMATION_FACTOR);
        o->Angle[1] -= (10.0f) * FPS_ANIMATION_FACTOR;
        if (o->SubType == 1)
        {
            o->Angle[1] += (20.0f) * FPS_ANIMATION_FACTOR;
        }
        VectorScale(o->Light, std::pow(0.9f, FPS_ANIMATION_FACTOR), o->Light);
        o->Alpha = 1.0f;
    }
    return true;
}

// BITMAP_EVENT_CLOUD
bool MoveBehavior::Move_BITMAP_EVENT_CLOUD(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->SubType == 1)
        {
            o->Scale *= pow(1.08f, FPS_ANIMATION_FACTOR);
            if (o->Scale > 5)
            {
                o->Scale = 5.0f;
            }
            o->Angle[0] = -(int)WorldTime * 0.5f;
            VectorScale(o->Light, std::pow(0.9f, FPS_ANIMATION_FACTOR), o->Light);
        }
        else
        {
            o->Scale *= pow(1.08f, FPS_ANIMATION_FACTOR);
            if (o->Scale > 3)
            {
                o->Scale = 3.0f;
            }
            o->Angle[0] = -(int)WorldTime * 0.5f;
            VectorScale(o->Light, std::pow(0.9f, FPS_ANIMATION_FACTOR), o->Light);
        }
    }
    return true;
}

// MODEL_WINDFOCE
bool MoveBehavior::Move_MODEL_WINDFOCE(OBJECT *o, int index, float Luminosity)
{
    {
        o->Angle[2] = -(int)WorldTime * 0.3f;

        if (o->SubType == 0 || o->SubType == 4 || o->SubType == 5)
        {
            o->Scale += (0.1f) * FPS_ANIMATION_FACTOR;
            if (o->Scale > 3)
            {
                o->Scale = 3;
            }
            o->Alpha *= pow(0.92f, FPS_ANIMATION_FACTOR);
            VectorScale(o->Light, std::pow(0.9f, FPS_ANIMATION_FACTOR), o->Light);
            for (float life : {40.f, 30.f})
            {
                if (!Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life))
                    continue;
                auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                    FPS_ANIMATION_FACTOR - std::max(0.f, o->LifeTime - life));
                vec3_t angle;
                VectorCopy(o->Angle, angle);
                const double birthTime =
                    WorldTime - birth.SceneRemainingFrames() * 1000.0 /
                                    sessionKeeper_.ApplicationConfig().legacyReferenceFps;
                angle[2] = -float(birthTime) * 0.3f;
                vec3_t vLight;
                if (o->SubType == 0)
                {
                    Vector(0.5f, 0.55f, 1.0f, vLight);
                }
                else if (o->SubType == 4)
                {
                    Vector(0.85f, 0.2f, 1.0f, vLight);
                }
                else if (o->SubType == 5)
                {
                    Vector(1.0f, 0.2f, 0.0f, vLight);
                }
                CreateEffect(MODEL_WINDFOCE, o->Position, angle, vLight, 3, o, -1, 0, 0, 0, 1.0f);
            }
        }
        else if (o->SubType == 1) //지속적인거
        {
            if (o->Owner != NULL && o->Owner->Live == true &&
                (g_isCharacterBuff(o->Owner, eBuff_Att_up_Ourforces) ||
                 g_isCharacterBuff(o->Owner, eBuff_Hp_up_Ourforces) ||
                 g_isCharacterBuff(o->Owner, eBuff_Def_up_Ourforces)))
            {
                o->LifeTime = 10;
            }
            else
            {
                o->LifeTime = 0;
            }

            if (g_isCharacterBuff(o->Owner, eBuff_Cloaking))
                return true;

            Vector(0.6f, 0.6f, 1.0f, o->Light);
            auto fFadeInOut = (float)(sinf(WorldTime * 0.001f) + 1.0f);
            VectorScale(o->Light, fFadeInOut * 0.25f + 0.2f, o->Light);
            o->Scale = 1.0f;
            VectorCopy(o->Owner->Position, o->Position);
        }
        else if (o->SubType == 2)
        {
            o->Angle[2] = -(int)WorldTime * 0.9f;
            o->Scale += (0.1f) * FPS_ANIMATION_FACTOR;
            if (o->Scale > 3)
            {
                o->Scale = 3;
            }
            o->Alpha *= pow(0.92f, FPS_ANIMATION_FACTOR);
            VectorScale(o->Light, std::pow(0.85f, FPS_ANIMATION_FACTOR), o->Light);

            for (float life : {60.f, 50.f})
            {
                if (!Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life))
                    continue;
                auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                    FPS_ANIMATION_FACTOR - std::max(0.f, o->LifeTime - life));
                vec3_t angle;
                VectorCopy(o->Angle, angle);
                const double birthTime =
                    WorldTime - birth.SceneRemainingFrames() * 1000.0 /
                                    sessionKeeper_.ApplicationConfig().legacyReferenceFps;
                angle[2] = -float(birthTime) * 0.9f;
                vec3_t vLight;
                Vector(1.0f, 0.5f, 0.0f, vLight);
                CreateEffect(MODEL_WINDFOCE, o->Position, angle, vLight, 3, o, -1, 0, 0, 0, 1.0f);
            }
        }
        else if (o->SubType == 3)
        {
            o->Angle[2] = -(int)WorldTime * 0.9f;
            o->Scale += (0.1f) * FPS_ANIMATION_FACTOR;
            if (o->Scale > 3)
            {
                o->Scale = 3;
            }
            o->Alpha *= pow(0.92f, FPS_ANIMATION_FACTOR);
            VectorScale(o->Light, std::pow(0.85f, FPS_ANIMATION_FACTOR), o->Light);
        }
    }
    return true;
}

// BITMAP_LIGHT_RED
bool MoveBehavior::Move_BITMAP_LIGHT_RED(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    vec3_t Angle;
    vec3_t Position;
    {
        if (o->Owner != NULL && o->Owner->Live == true &&
            (g_isCharacterBuff(o->Owner, eBuff_Hp_up_Ourforces) ||
             g_isCharacterBuff(o->Owner, eBuff_Att_up_Ourforces) ||
             g_isCharacterBuff(o->Owner, eBuff_Def_up_Ourforces)))
        {
            o->LifeTime = 10;

            if (g_isCharacterBuff(o->Owner, eBuff_Def_up_Ourforces))
            {
                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 70.f))
                {
                    if (!SearchEffect(MODEL_WINDFOCE_MIRROR, o->Owner))
                    {
                        vec3_t vLight;
                        Vector(0.6f, 0.6f, 1.0f, vLight);
                        CreateEffect(MODEL_WINDFOCE_MIRROR, o->Owner->Position, o->Angle, vLight, 0,
                                     o->Owner, -1, 0, 0, 0, 1.0f);
                    }
                }
            }
        }
        else if (o->SubType == 3 || o->SubType == 4)
        {
            vec3_t Light;
            float Luminosity = (float)(WorldRandom() % 5) * 0.1f;
            Vector(Luminosity + 0.4f, Luminosity + 0.0f, Luminosity + 0.0f, Light);
            int range = 2;
            if (o->SubType == 4)
            {
                range = 3;
            }
            AddTerrainLight(o->Position[0], o->Position[1], Light, range, PrimaryTerrainLight);
        }
        else
        {
            o->LifeTime = 0;
        }

        if (g_isCharacterBuff(o->Owner, eBuff_Cloaking) && (o->SubType != 3))
            return true;

        if (g_isCharacterBuff(o->Owner, eBuff_Att_up_Ourforces) || o->SubType == 1)
        {
            vec3_t Light, Angle;
            Vector(0.f, 0.f, 0.f, Angle);
            BMD *b = &Models[o->Owner->Type];
            vec3_t Position;

            if (g_isCharacterBuff(o->Owner, eBuff_Att_up_Ourforces))
            {
                const float remaining = -float(o->EffectEmissionAge);
                for (float sample = Core::Time::ReferenceSample(remaining * 0.5f) * 2.f;
                     Core::Time::Reaches(remaining, FPS_ANIMATION_FACTOR, sample); sample -= 2.f)
                {
                    auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                        FPS_ANIMATION_FACTOR - (std::max)(0.f, remaining - sample));
                    VectorCopy(o->Position, Position);
                    Vector(1.0f, 0.12f, 0.0f, Light);
                    SampleOwnerBone(*o->Owner, *b, (WorldRandom() % 2) ? 26 : 35, WorldTime,
                                    birth.FrameFraction(), Position);
                    CreateParticle(BITMAP_SMOKELINE1, Position, o->Angle, Light, 5, 0.005f,
                                   o->Owner);
                }
            }
        }
    }
    return true;
}

// MODEL_WINDFOCE_MIRROR
bool MoveBehavior::Move_MODEL_WINDFOCE_MIRROR(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->Owner == NULL || o->Owner->Live == false ||
            !g_isCharacterBuff(o->Owner, eBuff_Def_up_Ourforces))
        {
            RetireEffect(o);
            return true;
        }
        o->Angle[2] = (int)WorldTime * 0.4f;
        o->Scale *= pow(1.015f, FPS_ANIMATION_FACTOR);

        if (o->Scale > 1.5f)
        {
            o->Scale = 1.5f;
        }
        VectorScale(o->Light, std::pow(0.9f, FPS_ANIMATION_FACTOR), o->Light);
        VectorCopy(o->Owner->Position, o->Position);
    }
    return true;
}

// BITMAP_SWORD_EFFECT_MONO
bool MoveBehavior::Move_BITMAP_SWORD_EFFECT_MONO(OBJECT *o, int index, float Luminosity)
{
    {
        vec3_t vLight;
        VectorCopy(o->Light, vLight);
        VectorScale(vLight, 0.1f, vLight);
        for (float sample = Core::Time::ReferenceSample(o->LifeTime);
             Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, sample); sample -= 1.f)
        {
            if (int(sample) % 3 == 0)
                continue;
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - sample));
            CreateParticle(BITMAP_SWORD_EFFECT_MONO, o->Position, o->Angle, o->Light, 0, o->Scale,
                           o);
        }

        if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 14))
        {
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 14.f));
            Vector(0.4f, 0.4f, 1.0f, vLight);
            if (o->SubType == 0)
            {
                Vector(0.4f, 0.4f, 1.0f, vLight);
            }
            else if (o->SubType == 1)
            {
                Vector(0.7f, 0.5f, 1.0f, vLight);
            }
            else if (o->SubType == 2)
            {
                Vector(1.0f, 0.12f, 0.0f, vLight);
            }
            CreateEffect(MODEL_SHOCKWAVE01, o->Position, o->Angle, vLight, 2, o->Owner, -1, 0, 0, 0,
                         1.0f);
        }
    }
    return true;
}

// MODEL_WOLF_HEAD_EFFECT2
bool MoveBehavior::Move_MODEL_WOLF_HEAD_EFFECT2(OBJECT *o, int index, float Luminosity)
{
    vec3_t Angle;
    {
        if (o->SubType == 4 || o->SubType == 6)
        {
            o->Owner->Velocity += (2.5f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 3)
        {
            o->BlendMeshLight = (float)o->LifeTime / 20.0f;
            o->Alpha = o->BlendMeshLight;

            VectorCopy(o->Owner->Angle, o->Angle);

            vec3_t Angle;
            if (o->LifeTime >= 3)
            {
                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
                {
                    VectorCopy(o->Owner->Angle, Angle);
                    int nRandom = WorldRandom() % 6;
                    float _Value = 2.0f + WorldRandom() % 3;
                    if (nRandom / 3)
                        o->Owner->Angle[2] = Angle[2] + (_Value * (nRandom % 3 * 2 + 1));
                    else
                        o->Owner->Angle[2] = Angle[2] - (_Value * (nRandom % 3 * 2 + 1));

                    int _Temp = WorldRandom() % 3;
                    if (_Temp == 1)
                        o->Owner->Angle[0] = Angle[0] + (_Value * (nRandom % 3 * 2 + 1));
                    else if (_Temp == 2)
                        o->Owner->Angle[0] = Angle[0] - (_Value * (nRandom % 3 * 2 + 1));

                    vec3_t pos;
                    BMD *b = &Models[o->Owner->Type];
                    Vector(0.0f, 0.0f, 0.0f, pos);
                    if (o->SubType == 5)
                        b->TransformByObjectBone(pos, o->Owner, 27, pos);
                    else
                        b->TransformByObjectBone(pos, o->Owner, 4, pos);

                    VectorCopy(pos, o->Position);

                    CreateEffect(MODEL_WOLF_HEAD_EFFECT2, o->Owner->Position, o->Owner->Angle,
                                 o->Owner->Light, 4, o->Owner);
                    CreateEffect(MODEL_WOLF_HEAD_EFFECT2, o->Owner->Position, o->Owner->Angle,
                                 o->Owner->Light, 6, o->Owner);
                    o->Owner->Angle[2] = Angle[2];
                    o->Owner->Angle[0] = Angle[0];
                }
            }
        }
        else if (o->SubType == 4)
        {
            VectorCopy(o->Owner->Angle, o->Angle);
        }
        else if (o->SubType == 5)
        {
            vec3_t pos;
            BMD *b = &Models[o->Owner->Type];
            Vector(0.0f, 0.0f, 0.0f, pos);
            b->TransformByObjectBone(pos, o->Owner, 27, pos);
            VectorCopy(pos, o->Position);
            VectorCopy(o->Owner->Angle, o->Angle);
        }
    }
    return true;
}

// MODEL_SHOCKWAVE_GROUND01
bool MoveBehavior::Move_MODEL_SHOCKWAVE_GROUND01(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->SubType == 1)
        {
            o->Scale *= pow(1.2f, FPS_ANIMATION_FACTOR);
            VectorScale(o->Light, std::pow(0.9f, FPS_ANIMATION_FACTOR), o->Light);
        }
        else if (o->SubType == 2)
        {
            o->Scale *= pow(1.2f, FPS_ANIMATION_FACTOR);
            VectorScale(o->Light, std::pow(0.7f, FPS_ANIMATION_FACTOR), o->Light);
        }
        else
        {
            o->Scale *= pow(1.2f, FPS_ANIMATION_FACTOR);
            VectorScale(o->Light, std::pow(0.8f, FPS_ANIMATION_FACTOR), o->Light);
        }
    }
    return true;
}

void MoveBehavior::EmitDragonKickWind(OBJECT &effect, float fraction)
{
    BMD &model = Models[effect.Owner->Type];
    const float samples = effect.Velocity * 10.f;
    const float actionSpeed = Models[effect.Type].Actions[effect.CurrentAction].PlaySpeed;
    const float sampleStep = actionSpeed / samples;
    effect.AlphaTarget = sampleStep * effect.Velocity / actionSpeed;
    ObjectDrawInput owner(effect.Owner), draw(&effect);
    const auto phase = effect.Owner->MotionTrace.SampleAnimation(
        WorldTime, fraction,
        {owner.animationFrame, owner.priorAnimationFrame, owner.action, owner.priorAction});
    owner.angle[2] = effect.Owner->MotionTrace.SampleYaw(WorldTime, fraction, owner.angle[2]);
    effect.MotionTrace.Sample(WorldTime, fraction, effect.Position, draw.position);
    draw.bones = BoneTransform;
    float frame = phase.frame - actionSpeed;
    for (int i = 0; i < samples; ++i)
    {
        model.AnimationAtFrame(BoneTransform, frame, phase.priorFrame, phase.priorAction,
                               owner.angle, owner.headAngle, false, false, nullptr, phase.action);
        vec3_t first, second;
        model.TransformByObjectBone(first, draw, 1);
        model.TransformByObjectBone(second, draw, 2);
        CreateJoint(BITMAP_GROUND_WIND, first, second, effect.Angle, i, &effect, 150.f);
        frame += sampleStep;
    }
}

// MODEL_DRAGON_KICK_DUMMY
bool MoveBehavior::Move_MODEL_DRAGON_KICK_DUMMY(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->Owner->AnimationFrame >
                Models[o->Owner->Type].Actions[PLAYER_SKILL_DRAGONKICK].NumAnimationKeys ||
            o->Owner->CurrentAction != PLAYER_SKILL_DRAGONKICK)
        {
            o->LifeTime = 0;
            RetireEffect(o);
            return true;
        }

        if (o->Owner->Live)
        {
            if (o->Owner->AnimationFrame > 4 && o->Owner->AnimationFrame < 5)
            {
                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
                    EmitDragonKickWind(*o, birth.FrameFraction());
            }

            vec3_t vPosition;
            if (o->Owner->m_sTargetIndex < 0)
                return true;
            VectorCopy(CharactersClient[o->Owner->m_sTargetIndex].Object.Position, vPosition);
            if (o->Owner->AnimationFrame > 5)
            {
                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
                {
                    auto &target = CharactersClient[o->Owner->m_sTargetIndex].Object;
                    target.MotionTrace.Sample(WorldTime, birth.FrameFraction(), target.Position,
                                              vPosition);
                    for (int j = 0; j < 2; ++j)
                    {
                        vec3_t vPos;
                        float fLuminosity =
                            (float)sinf(
                                (WorldTime -
                                 birth.SceneRemainingFrames() * 1000.0 /
                                     sessionKeeper_.ApplicationConfig().legacyReferenceFps) *
                                0.002f) *
                            0.2f;
                        VectorCopy(vPosition, vPos);
                        vPos[2] += 30.0f;
                        float fScale =
                            ((float)(WorldRandom() % 40 + 50) * 0.01f * 1.5f) * o->Scale * 1.8f;
                        for (int i = 0; i < 4; ++i)
                        {
                            CreateParticle(BITMAP_ENERGY, vPos, o->Angle, o->Light, 7, fScale);
                            CreateParticle(BITMAP_LIGHTNING_MEGA1, vPos, o->Angle, o->Light, 0,
                                           fScale * 0.9f);

                            CreateParticle(BITMAP_SPARK + 1, vPos, o->Angle, o->Light, 3,
                                           1.0f + (fLuminosity * 0.05f));
                            CreateParticle(BITMAP_SPARK + 1, vPos, o->Angle, o->Light, 8,
                                           1.0f + (fLuminosity * 0.05f));

                            if (i % 2)
                            {
                                CreateParticle(BITMAP_SPARK, vPos, o->Angle, o->Light, 13);
                            }

                            if (i % 2)
                            {
                                Vector(vPos[0] + WorldRandom() % 40 + 20.0f,
                                       vPos[1] + WorldRandom() % 30 + 10.0f,
                                       vPos[2] + WorldRandom() % 30 + 25.0f, vPos);
                            }
                            else
                            {
                                Vector(vPos[0] - WorldRandom() % 20 - 10.0f,
                                       vPos[1] + WorldRandom() % 30 - 10.0f,
                                       vPos[2] + WorldRandom() % 30 + 30.0f, vPos);
                            }
                        }
                    }
                }
            }
            VectorCopy(o->Owner->Angle, o->Angle);
            VectorCopy(o->Owner->Position, o->Position);

            if (o->Owner->AnimationFrame > 8)
            {
                o->LifeTime = 150;
            }
            if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 150))
            {
                PlayBuffer(SOUND_RAGESKILL_DRAGONKICK_ATTACK);
            }
        }
        else
        {
            o->LifeTime = 0;
            RetireEffect(o);
        }
    }
    return true;
}

void MoveBehavior::EmitDragonLoreSamples(OBJECT *o, float fraction)
{
    BMD *pModel = &Models[o->Owner->Type];
    ObjectDrawInput draw(o->Owner);
    const auto phase = o->Owner->MotionTrace.SampleAnimation(
        WorldTime, fraction,
        {draw.animationFrame, draw.priorAnimationFrame, draw.action, draw.priorAction});
    o->Owner->MotionTrace.Sample(WorldTime, fraction, o->Owner->Position, draw.position);
    draw.angle[2] = o->Owner->MotionTrace.SampleYaw(WorldTime, fraction, draw.angle[2]);
    draw.bones = BoneTransform;
    constexpr float SamplesPerAnimationFrame = 10.f;
    const float samples = o->Velocity * SamplesPerAnimationFrame;
    const float actionSpeed = pModel->Actions[phase.action].PlaySpeed;
    const float sampleTravel = actionSpeed / samples;
    float animationFrame = phase.frame - actionSpeed;
    for (int i = 0; i < samples; ++i)
    {
        pModel->AnimationAtFrame(BoneTransform, animationFrame, phase.priorFrame, phase.priorAction,
                                 draw.angle, draw.headAngle, false, false, nullptr, phase.action);
        vec3_t first, second;
        pModel->TransformByObjectBone(first, draw, 36);
        pModel->TransformByObjectBone(second, draw, 28);
        CreateJoint(BITMAP_LAVA, first, first, draw.angle, i, o->Owner, 20.f);
        CreateJoint(BITMAP_LAVA, second, second, draw.angle, 7 + i, o->Owner, 20.f);
        animationFrame += sampleTravel;
    }
}

// BITMAP_LAVA
bool MoveBehavior::Move_BITMAP_LAVA(OBJECT *o, int index, float Luminosity)
{
    vec3_t Angle;
    float Matrix[3][4];
    {
        const float remaining = -float(o->EffectEmissionAge);
        for (float sample = Core::Time::ReferenceSample(remaining);
             Core::Time::Reaches(remaining, FPS_ANIMATION_FACTOR, sample); sample -= 1.f)
        {
            const float offset = (std::max)(0.f, remaining - sample);
            auto birthTime = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - offset);
            const float fraction = birthTime.FrameFraction();
            const auto phase = o->Owner->MotionTrace.SampleAnimation(
                WorldTime, fraction,
                {o->Owner->AnimationFrame, o->Owner->PriorAnimationFrame, o->Owner->CurrentAction,
                 o->Owner->PriorAction});
            if (phase.action != PLAYER_SKILL_DRAGONLORE || phase.frame <= 2.f || phase.frame >= 8.f)
                continue;
            EmitDragonLoreSamples(o, fraction);
        }
        if (o->Owner->AnimationFrame >
                Models[o->Owner->Type].Actions[PLAYER_SKILL_DRAGONLORE].NumAnimationKeys ||
            o->Owner->CurrentAction != PLAYER_SKILL_DRAGONLORE)
        {
            RetireEffect(o);
            o->LifeTime = 0;
            return true;
        }
        vec3_t vLight, vAngle, vPos;
        Vector(1.0f, 1.0f, 1.0f, vLight);
        VectorCopy(o->Owner->Angle, vAngle);

        if (o->Owner->AnimationFrame > 5 && o->LifeTime > 100)
        {
            Vector(1.0f, 0.12f, 0.0f, vLight);
            CreateEffect(MODEL_SHOCKWAVE01, o->Owner->Position, vAngle, vLight, 1, o->Owner, -1, 0,
                         0, 0, 1.0f);
            o->LifeTime = 95;
        }

        if (o->Owner->AnimationFrame > 5 && o->LifeTime > 80)
        {
            Vector(1.0f, 0.1f, 0.0f, vLight);
            CreateEffect(MODEL_SHOCKWAVE_GROUND01, o->Position, o->Angle, vLight, 1, o, -1, 0, 0, 0,
                         1.0f);
            o->LifeTime = 80;
        }

        if (o->Owner->AnimationFrame > 5)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                Vector(1.0f, 1.0f, 1.0f, vLight);
                float fScale = o->Scale * (WorldRandom() % 5 + 18) * 0.1f;
                VectorCopy(o->Position, vPos);
                vPos[2] +=
                    (80.f - o->LifeTime + FPS_ANIMATION_FACTOR - birth.RemainingFrames()) * 7.f;

                switch (WorldRandom() % 3)
                {
                case 0:
                    CreateParticle(BITMAP_FIRE_HIK1, vPos, o->Angle, vLight, 0, fScale);
                    break;
                case 1:
                    CreateParticle(BITMAP_FIRE_CURSEDLICH, vPos, o->Angle, vLight, 4, fScale);
                    break;
                case 2:
                    CreateParticle(BITMAP_FIRE_HIK3, vPos, o->Angle, vLight, 0, fScale);
                    break;
                }

                vec3_t PosL, Angle, outPos, tempPos;
                VectorCopy(vPos, tempPos);
                VectorScale(vLight, 0.5f, vLight);
                for (int i = 0; i < 8; ++i)
                {
                    if (i % 2 == 0)
                        continue;

                    float Matrix[3][4];
                    Vector(WorldRandom() % 100 - 50, WorldRandom() % 100 - 50, 0.0f, PosL);
                    Vector((float)(WorldRandom() % 90), 0.0f, 45.0f * i, Angle);
                    AngleMatrix(Angle, Matrix);
                    VectorRotate(PosL, Matrix, outPos);
                    VectorAdd(vPos, outPos, vPos);
                    vPos[2] = tempPos[2];
                    CreateParticle(BITMAP_FIRE_HIK1, vPos, o->Angle, vLight, 5, fScale);

                    vec3_t TempPos;
                    VectorCopy(vPos, TempPos);
                    TempPos[2] = 250.0f;
                    CreateParticle(BITMAP_SPARK, TempPos, o->Angle, o->Light, 12, 1.5f);
                }
            }
        }
    }
    return true;
}

// MODEL_DRAGON_LOWER_DUMMY
bool MoveBehavior::Move_MODEL_DRAGON_LOWER_DUMMY(OBJECT *o, int index, float Luminosity)
{
    {
        BMD *pModel = &Models[o->Type];
        pModel->CurrentAction = o->CurrentAction;
        pModel->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction,
                              o->Velocity, o->Position, o->Angle);

        if (o->AnimationFrame > 1 && o->AnimationFrame < 6)
        {
            vec3_t temp;
            VectorCopy(o->Position, temp);
            temp[2] = 250.0f;
            CreateParticleFpsChecked(BITMAP_SPARK, temp, o->Angle, o->Light, 12, 1.5f);
        }

        if (o->AnimationFrame > 1 && o->LifeTime > 80)
        {
            vec3_t vLight;
            Vector(1.0f, 1.0f, 1.0f, vLight);
            CreateEffect(BITMAP_LIGHT_RED, o->Position, o->Angle, vLight, 1, o, -1, 0, 0, 0, 15.0f);
            o->LifeTime = 80;
        }
        o->Alpha *= pow(0.96f, FPS_ANIMATION_FACTOR);
    }
    return true;
}

// MODEL_TARGETMON_EFFECT
bool MoveBehavior::Move_MODEL_TARGETMON_EFFECT(OBJECT *o, int index, float Luminosity)
{
    {
        if (o->Owner == NULL || o->Owner->Live == false)
        {
            RetireEffect(o);
            o->LifeTime = 0;
            return true;
        }

        if (o->SubType == 1 && o->Owner->m_sTargetIndex < 0)
            return true;

        BMD *pModel = &Models[o->Owner->Type];
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            vec3_t position, light{1.f, 1.f, 1.f};
            const int bone = WorldRandom() % (pModel->NumBones - 1);
            SampleOwnerBone(*o->Owner, *pModel, bone, WorldTime, birth.FrameFraction(), position);
            const float scale = o->Scale * (WorldRandom() % 5 + 5) * 0.1f;
            for (int i = 0; i < 3; ++i)
            {
                switch (WorldRandom() % 3)
                {
                case 0:
                    CreateParticle(BITMAP_FIRE_HIK1, position, o->Angle, light, 0, scale);
                    break;
                case 1:
                    CreateParticle(BITMAP_FIRE_CURSEDLICH, position, o->Angle, light, 4, scale);
                    break;
                case 2:
                    CreateParticle(BITMAP_FIRE_HIK3, position, o->Angle, light, 0, scale);
                    break;
                }
            }
        }
    }
    return true;
}

// MODEL_VOLCANO_OF_MONK
bool MoveBehavior::Move_MODEL_VOLCANO_OF_MONK(OBJECT *o, int index, float Luminosity)
{
    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);
    {
        if (o->SubType == 1)
        {
            vec3_t vLight, vPos;
            Vector(1.0f, 1.0f, 1.0f, vLight);
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                VectorCopy(o->Position, vPos);
                const float age =
                    80.f - o->LifeTime + FPS_ANIMATION_FACTOR - birth.RemainingFrames();
                vPos[2] += age * (0.9f + WorldRandom() % 5 * 0.1f) + o->SubType * 20.f;
                if (vPos[2] > 400.f)
                    continue;
                const float scale = o->Scale * (WorldRandom() % 5 + 8) * 0.19f;
                switch (WorldRandom() % 3)
                {
                case 0:
                    CreateParticle(BITMAP_FIRE_HIK1, vPos, o->Angle, vLight, 6, scale);
                    break;
                case 1:
                    CreateParticle(BITMAP_FIRE_CURSEDLICH, vPos, o->Angle, vLight, 9, scale);
                    break;
                case 2:
                    CreateParticle(BITMAP_FIRE_HIK3, vPos, o->Angle, vLight, 6, scale);
                    break;
                }
            }

            if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 10))
            {
                auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                    FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 10.f));
                for (int i = 0; i < 4; ++i)
                {
                    CreateEffect(MODEL_VOLCANO_STONE, o->Position, o->Angle, vLight, 0, o, -1, 0, 0,
                                 0, 1.0f);
                }

                VectorCopy(o->Position, vPos);
                Vector(1.0f, 0.1f, 0.0f, vLight);
                CreateEffect(MODEL_SHOCKWAVE_GROUND01, vPos, o->Angle, vLight, 2, o, -1, 0, 0, 0,
                             1.0f);
                Vector(1.0f, 1.0f, 1.0f, vLight);
            }
            if (o->LifeTime < 20)
            {
                o->Alpha = o->LifeTime * 0.1f;
            }
            if (o->LifeTime < 30)
            {
                o->BlendMeshLight = o->LifeTime * 0.03f;
            }

            vec3_t Light;
            float Luminosity = (float)(WorldRandom() % 5) * 0.01f;
            Vector(Luminosity + 1.0f, Luminosity + 0.4f, Luminosity + 0.4f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 1, PrimaryTerrainLight);
        }
        else if (o->SubType == 2 || o->SubType == 3)
        {
            if (o->Owner == NULL || o->Owner->Live == false)
            {
                RetireEffect(o);
                o->LifeTime = 0;
                return true;
            }

            if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 2))
            {
                auto birth = sessionKeeper_.Gameplay()->EmissionTime(
                    FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 2.f));
                CreateEffect(MODEL_VOLCANO_OF_MONK, o->Position, o->Angle, o->Light, 1, o, -1, 0, 0,
                             0, 1.0f);
                o->LifeTime = 0;
                RetireEffect(o);
            }
            o->Position[2] = -100.0f;
        }
    }
    return true;
}

// MODEL_VOLCANO_STONE
bool MoveBehavior::Move_MODEL_VOLCANO_STONE(OBJECT *o, int index, float Luminosity)
{
    {
        const auto motion = AdvanceHeadDebris(*o, *sessionKeeper_.WorldUnit(), FPS_ANIMATION_FACTOR,
                                              0.f, 1.f, 0.6f, 0.5f);
        const float spin =
            0.3f * FPS_ANIMATION_FACTOR * (o->LifeTime - (FPS_ANIMATION_FACTOR - 1.f) * 0.5f);
        o->Angle[0] += spin;
        o->Angle[1] += spin;
        o->Alpha -= 0.05f * motion.groundFrames;
        o->Scale -= (0.03f) * FPS_ANIMATION_FACTOR;

        vec3_t vLight;
        Vector(1.0f, 1.0f, 1.0f, vLight);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            vec3_t position;
            o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
            const float scaleAtBirth = o->Scale + 0.03f * birth.RemainingFrames();
            const float scale = scaleAtBirth * (WorldRandom() % 5 + 5) * 0.1f;
            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticle(BITMAP_FIRE_HIK1, position, o->Angle, vLight, 0, scale);
                break;
            case 1:
                CreateParticle(BITMAP_FIRE_CURSEDLICH, position, o->Angle, vLight, 4, scale);
                break;
            case 2:
                CreateParticle(BITMAP_FIRE_HIK3, position, o->Angle, vLight, 0, scale);
                break;
            }
        }
    }
    return true;
}

const std::vector<std::pair<int, MoveHandler>> &ExtractedMoveHandlers()
{
    static const std::vector<std::pair<int, MoveHandler>> handlers = {
        {MODEL_DRAGON, &MoveBehavior::Move_MODEL_DRAGON},
        {MODEL_ARROW_AUTOLOAD, &MoveBehavior::Move_MODEL_ARROW_AUTOLOAD},
        {MODEL_INFINITY_ARROW, &MoveBehavior::Move_MODEL_INFINITY_ARROW},
        {MODEL_INFINITY_ARROW1, &MoveBehavior::Move_MODEL_INFINITY_ARROW1},
        {MODEL_INFINITY_ARROW2, &MoveBehavior::Move_MODEL_INFINITY_ARROW1},
        {MODEL_INFINITY_ARROW3, &MoveBehavior::Move_MODEL_INFINITY_ARROW1},
        {MODEL_SHIELD_CRASH, &MoveBehavior::Move_MODEL_SHIELD_CRASH},
        {MODEL_SHIELD_CRASH2, &MoveBehavior::Move_MODEL_SHIELD_CRASH2},
        {MODEL_IRON_RIDER_ARROW, &MoveBehavior::Move_MODEL_IRON_RIDER_ARROW},
        {MODEL_MULTI_SHOT3, &MoveBehavior::Move_MODEL_MULTI_SHOT3},
        {MODEL_MULTI_SHOT1, &MoveBehavior::Move_MODEL_MULTI_SHOT1},
        {MODEL_MULTI_SHOT2, &MoveBehavior::Move_MODEL_MULTI_SHOT2},
        {MODEL_BLADE_SKILL, &MoveBehavior::Move_MODEL_BLADE_SKILL},
        {MODEL_KENTAUROS_ARROW, &MoveBehavior::Move_MODEL_KENTAUROS_ARROW},
        {MODEL_WARP3, &MoveBehavior::Move_MODEL_WARP3},
        {MODEL_WARP2, &MoveBehavior::Move_MODEL_WARP3},
        {MODEL_WARP, &MoveBehavior::Move_MODEL_WARP3},
        {MODEL_WARP6, &MoveBehavior::Move_MODEL_WARP3},
        {MODEL_WARP5, &MoveBehavior::Move_MODEL_WARP3},
        {MODEL_WARP4, &MoveBehavior::Move_MODEL_WARP3},
        {MODEL_GHOST, &MoveBehavior::Move_MODEL_GHOST},
        {MODEL_TREE_ATTACK, &MoveBehavior::Move_MODEL_TREE_ATTACK},
        {MODEL_BUTTERFLY01, &MoveBehavior::Move_MODEL_BUTTERFLY01},
        {BITMAP_SKULL, &MoveBehavior::Move_BITMAP_SKULL},
        {MODEL__SPEAR, &MoveBehavior::Move_MODEL__SPEAR},
        {MODEL_HALLOWEEN_CANDY_BLUE, &MoveBehavior::Move_MODEL_HALLOWEEN_CANDY_BLUE},
        {MODEL_HALLOWEEN_CANDY_ORANGE, &MoveBehavior::Move_MODEL_HALLOWEEN_CANDY_BLUE},
        {MODEL_HALLOWEEN_CANDY_YELLOW, &MoveBehavior::Move_MODEL_HALLOWEEN_CANDY_BLUE},
        {MODEL_HALLOWEEN_CANDY_RED, &MoveBehavior::Move_MODEL_HALLOWEEN_CANDY_BLUE},
        {MODEL_HALLOWEEN_CANDY_HOBAK, &MoveBehavior::Move_MODEL_HALLOWEEN_CANDY_BLUE},
        {MODEL_HALLOWEEN_CANDY_STAR, &MoveBehavior::Move_MODEL_HALLOWEEN_CANDY_BLUE},
        {MODEL_HALLOWEEN_EX, &MoveBehavior::Move_MODEL_HALLOWEEN_EX},
        {MODEL_XMAS_EVENT_BOX, &MoveBehavior::Move_MODEL_XMAS_EVENT_BOX},
        {MODEL_XMAS_EVENT_CANDY, &MoveBehavior::Move_MODEL_XMAS_EVENT_BOX},
        {MODEL_XMAS_EVENT_TREE, &MoveBehavior::Move_MODEL_XMAS_EVENT_BOX},
        {MODEL_XMAS_EVENT_SOCKS, &MoveBehavior::Move_MODEL_XMAS_EVENT_BOX},
        {MODEL_XMAS_EVENT_ICEHEART, &MoveBehavior::Move_MODEL_XMAS_EVENT_ICEHEART},
        {MODEL_NEWYEARSDAY_EVENT_BEKSULKI, &MoveBehavior::Move_MODEL_NEWYEARSDAY_EVENT_BEKSULKI},
        {MODEL_NEWYEARSDAY_EVENT_CANDY, &MoveBehavior::Move_MODEL_NEWYEARSDAY_EVENT_BEKSULKI},
        {MODEL_NEWYEARSDAY_EVENT_MONEY, &MoveBehavior::Move_MODEL_NEWYEARSDAY_EVENT_BEKSULKI},
        {MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_GREEN,
         &MoveBehavior::Move_MODEL_NEWYEARSDAY_EVENT_BEKSULKI},
        {MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_RED,
         &MoveBehavior::Move_MODEL_NEWYEARSDAY_EVENT_BEKSULKI},
        {MODEL_NEWYEARSDAY_EVENT_PIG, &MoveBehavior::Move_MODEL_NEWYEARSDAY_EVENT_BEKSULKI},
        {MODEL_NEWYEARSDAY_EVENT_YUT, &MoveBehavior::Move_MODEL_NEWYEARSDAY_EVENT_BEKSULKI},
        {MODEL_MOONHARVEST_MOON, &MoveBehavior::Move_MODEL_MOONHARVEST_MOON},
        {MODEL_MOONHARVEST_GAM, &MoveBehavior::Move_MODEL_MOONHARVEST_GAM},
        {MODEL_MOONHARVEST_SONGPUEN1, &MoveBehavior::Move_MODEL_MOONHARVEST_GAM},
        {MODEL_MOONHARVEST_SONGPUEN2, &MoveBehavior::Move_MODEL_MOONHARVEST_GAM},
        {MODEL_SPEARSKILL, &MoveBehavior::Move_MODEL_SPEARSKILL},
        {BITMAP_FIRE_CURSEDLICH, &MoveBehavior::Move_BITMAP_FIRE_CURSEDLICH},
        {MODEL_SUMMONER_WRISTRING_EFFECT, &MoveBehavior::Move_MODEL_SUMMONER_WRISTRING_EFFECT},
        {MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT,
         &MoveBehavior::Move_MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT},
        {MODEL_SUMMONER_EQUIP_HEAD_NEIL, &MoveBehavior::Move_MODEL_SUMMONER_EQUIP_HEAD_NEIL},
        {MODEL_SUMMONER_CASTING_EFFECT1, &MoveBehavior::Move_MODEL_SUMMONER_CASTING_EFFECT1},
        {MODEL_SUMMONER_CASTING_EFFECT11, &MoveBehavior::Move_MODEL_SUMMONER_CASTING_EFFECT1},
        {MODEL_SUMMONER_CASTING_EFFECT111, &MoveBehavior::Move_MODEL_SUMMONER_CASTING_EFFECT1},
        {MODEL_SUMMONER_CASTING_EFFECT2, &MoveBehavior::Move_MODEL_SUMMONER_CASTING_EFFECT1},
        {MODEL_SUMMONER_CASTING_EFFECT22, &MoveBehavior::Move_MODEL_SUMMONER_CASTING_EFFECT1},
        {MODEL_SUMMONER_CASTING_EFFECT222, &MoveBehavior::Move_MODEL_SUMMONER_CASTING_EFFECT1},
        {MODEL_SUMMONER_CASTING_EFFECT4, &MoveBehavior::Move_MODEL_SUMMONER_CASTING_EFFECT1},
        {MODEL_SUMMONER_SUMMON_SAHAMUTT, &MoveBehavior::Move_MODEL_SUMMONER_SUMMON_SAHAMUTT},
        {MODEL_SUMMONER_SUMMON_NEIL, &MoveBehavior::Move_MODEL_SUMMONER_SUMMON_NEIL},
        {MODEL_SUMMONER_SUMMON_LAGUL, &MoveBehavior::Move_MODEL_SUMMONER_SUMMON_LAGUL},
        {BITMAP_MAGIC, &MoveBehavior::Move_BITMAP_MAGIC},
        {BITMAP_OUR_INFLUENCE_GROUND, &MoveBehavior::Move_BITMAP_OUR_INFLUENCE_GROUND},
        {BITMAP_ENEMY_INFLUENCE_GROUND, &MoveBehavior::Move_BITMAP_OUR_INFLUENCE_GROUND},
        {BITMAP_MAGIC_ZIN, &MoveBehavior::Move_BITMAP_MAGIC_ZIN},
        {BITMAP_PIN_LIGHT, &MoveBehavior::Move_BITMAP_PIN_LIGHT},
        {BITMAP_ORORA, &MoveBehavior::Move_BITMAP_ORORA},
        {BITMAP_GATHERING, &MoveBehavior::Move_BITMAP_GATHERING},
        {BITMAP_JOINT_THUNDER, &MoveBehavior::Move_BITMAP_JOINT_THUNDER},
        {BITMAP_IMPACT, &MoveBehavior::Move_BITMAP_IMPACT},
        {BITMAP_FLAME, &MoveBehavior::Move_BITMAP_FLAME},
        {MODEL_RAKLION_BOSS_CRACKEFFECT, &MoveBehavior::Move_MODEL_RAKLION_BOSS_CRACKEFFECT},
        {MODEL_RAKLION_BOSS_MAGIC, &MoveBehavior::Move_MODEL_RAKLION_BOSS_MAGIC},
        {BITMAP_FIRE_HIK2_MONO, &MoveBehavior::Move_BITMAP_FIRE_HIK2_MONO},
        {BITMAP_CLOUD, &MoveBehavior::Move_BITMAP_CLOUD},
        {MODEL_CHAIN_LIGHTNING, &MoveBehavior::Move_MODEL_CHAIN_LIGHTNING},
        {MODEL_ALICE_DRAIN_LIFE, &MoveBehavior::Move_MODEL_ALICE_DRAIN_LIFE},
        {MODEL_ALICE_BUFFSKILL_EFFECT, &MoveBehavior::Move_MODEL_ALICE_BUFFSKILL_EFFECT},
        {MODEL_ALICE_BUFFSKILL_EFFECT2, &MoveBehavior::Move_MODEL_ALICE_BUFFSKILL_EFFECT},
        {MODEL_LIGHTNING_SHOCK, &MoveBehavior::Move_MODEL_LIGHTNING_SHOCK},
        {MODEL_SKILL_BLAST, &MoveBehavior::Move_MODEL_SKILL_BLAST},
        {MODEL_WAVE, &MoveBehavior::Move_MODEL_WAVE},
        {MODEL_TAIL, &MoveBehavior::Move_MODEL_TAIL},
        {MODEL_WAVE_FORCE, &MoveBehavior::Move_MODEL_WAVE_FORCE},
        {MODEL_SKILL_INFERNO, &MoveBehavior::Move_MODEL_SKILL_INFERNO},
        {MODEL_MAGIC_CIRCLE1, &MoveBehavior::Move_MODEL_MAGIC_CIRCLE1},
        {MODEL_PROTECT, &MoveBehavior::Move_MODEL_PROTECT},
        {MODEL_POISON, &MoveBehavior::Move_MODEL_POISON},
        {MODEL_SAW, &MoveBehavior::Move_MODEL_SAW},
        {MODEL_LASER, &MoveBehavior::Move_MODEL_LASER},
        {MODEL_SKILL_WHEEL1, &MoveBehavior::Move_MODEL_SKILL_WHEEL1},
        {MODEL_SKILL_WHEEL2, &MoveBehavior::Move_MODEL_SKILL_WHEEL2},
        {MODEL_SKILL_FISSURE, &MoveBehavior::Move_MODEL_SKILL_FISSURE},
        {MODEL_FISSURE, &MoveBehavior::Move_MODEL_FISSURE},
        {MODEL_SKILL_FURY_STRIKE, &MoveBehavior::Move_MODEL_SKILL_FURY_STRIKE},
        {MODEL_BALGAS_SKILL, &MoveBehavior::Move_MODEL_BALGAS_SKILL},
        {MODEL_CHANGE_UP_EFF, &MoveBehavior::Move_MODEL_CHANGE_UP_EFF},
        {MODEL_CHANGE_UP_NASA, &MoveBehavior::Move_MODEL_CHANGE_UP_NASA},
        {MODEL_CHANGE_UP_CYLINDER, &MoveBehavior::Move_MODEL_CHANGE_UP_CYLINDER},
        {MODEL_DARK_ELF_SKILL, &MoveBehavior::Move_MODEL_DARK_ELF_SKILL},
        {MODEL_MAGIC2, &MoveBehavior::Move_MODEL_MAGIC2},
        {MODEL_STORM, &MoveBehavior::Move_MODEL_STORM},
        {MODEL_SUMMON, &MoveBehavior::Move_MODEL_SUMMON},
        {MODEL_STORM2, &MoveBehavior::Move_MODEL_STORM2},
        {MODEL_STORM3, &MoveBehavior::Move_MODEL_STORM3},
        {MODEL_MAYASTONE1, &MoveBehavior::Move_MODEL_MAYASTONE1},
        {MODEL_MAYASTONE2, &MoveBehavior::Move_MODEL_MAYASTONE1},
        {MODEL_MAYASTONE3, &MoveBehavior::Move_MODEL_MAYASTONE1},
        {MODEL_MAYASTONE4, &MoveBehavior::Move_MODEL_MAYASTONE4},
        {MODEL_MAYASTONE5, &MoveBehavior::Move_MODEL_MAYASTONE4},
        {MODEL_MAYASTONEFIRE, &MoveBehavior::Move_MODEL_MAYASTONEFIRE},
        {MODEL_MAYAHANDSKILL, &MoveBehavior::Move_MODEL_MAYAHANDSKILL},
        {MODEL_CIRCLE, &MoveBehavior::Move_MODEL_CIRCLE},
        {MODEL_CIRCLE_LIGHT, &MoveBehavior::Move_MODEL_CIRCLE_LIGHT},
        {MODEL_ICE_SMALL, &MoveBehavior::Move_MODEL_ICE_SMALL},
        {MODEL_METEO1, &MoveBehavior::Move_MODEL_ICE_SMALL},
        {MODEL_METEO2, &MoveBehavior::Move_MODEL_ICE_SMALL},
        {MODEL_BOSS_ATTACK, &MoveBehavior::Move_MODEL_ICE_SMALL},
        {MODEL_EFFECT_SAPITRES_ATTACK_2, &MoveBehavior::Move_MODEL_ICE_SMALL},
        {MODEL_SKULL, &MoveBehavior::Move_MODEL_SKULL},
        {MODEL_CUNDUN_PART1, &MoveBehavior::Move_MODEL_CUNDUN_PART1},
        {MODEL_CUNDUN_PART2, &MoveBehavior::Move_MODEL_CUNDUN_PART1},
        {MODEL_CUNDUN_PART3, &MoveBehavior::Move_MODEL_CUNDUN_PART1},
        {MODEL_CUNDUN_PART4, &MoveBehavior::Move_MODEL_CUNDUN_PART1},
        {MODEL_CUNDUN_PART5, &MoveBehavior::Move_MODEL_CUNDUN_PART1},
        {MODEL_CUNDUN_PART6, &MoveBehavior::Move_MODEL_CUNDUN_PART1},
        {MODEL_CUNDUN_PART7, &MoveBehavior::Move_MODEL_CUNDUN_PART1},
        {MODEL_CUNDUN_PART8, &MoveBehavior::Move_MODEL_CUNDUN_PART1},
        {MODEL_ILLUSION_OF_KUNDUN, &MoveBehavior::Move_MODEL_CUNDUN_PART1},
        {MODEL_CURSEDTEMPLE_STATUE_PART1, &MoveBehavior::Move_MODEL_CURSEDTEMPLE_STATUE_PART1},
        {MODEL_CURSEDTEMPLE_STATUE_PART2, &MoveBehavior::Move_MODEL_CURSEDTEMPLE_STATUE_PART1},
        {MODEL_XMAS2008_SNOWMAN_HEAD, &MoveBehavior::Move_MODEL_XMAS2008_SNOWMAN_HEAD},
        {MODEL_XMAS2008_SNOWMAN_BODY, &MoveBehavior::Move_MODEL_XMAS2008_SNOWMAN_BODY},
        {MODEL_DOPPELGANGER_SLIME_CHIP, &MoveBehavior::Move_MODEL_DOPPELGANGER_SLIME_CHIP},
        {MODEL_WATER_WAVE, &MoveBehavior::Move_MODEL_WATER_WAVE},
        {MODEL_STAFF_OF_DESTRUCTION, &MoveBehavior::Move_MODEL_STAFF_OF_DESTRUCTION},
        {MODEL_PIERCING, &MoveBehavior::Move_MODEL_PIERCING},
        {MODEL_ARROW_BEST_CROSSBOW, &MoveBehavior::Move_MODEL_ARROW_BEST_CROSSBOW},
        {MODEL_ARROW_DOUBLE, &MoveBehavior::Move_MODEL_ARROW_DOUBLE},
        {MODEL_ARROW_HOLY, &MoveBehavior::Move_MODEL_ARROW_HOLY},
        {MODEL_ARROW, &MoveBehavior::Move_MODEL_ARROW},
        {MODEL_ARROW_STEEL, &MoveBehavior::Move_MODEL_ARROW_STEEL},
        {MODEL_ARROW_THUNDER, &MoveBehavior::Move_MODEL_ARROW_STEEL},
        {MODEL_ARROW_LASER, &MoveBehavior::Move_MODEL_ARROW_STEEL},
        {MODEL_ARROW_V, &MoveBehavior::Move_MODEL_ARROW_STEEL},
        {MODEL_ARROW_SAW, &MoveBehavior::Move_MODEL_ARROW_STEEL},
        {MODEL_ARROW_NATURE, &MoveBehavior::Move_MODEL_ARROW_STEEL},
        {MODEL_ARROW_WING, &MoveBehavior::Move_MODEL_ARROW_STEEL},
        {MODEL_LACEARROW, &MoveBehavior::Move_MODEL_ARROW_STEEL},
        {MODEL_DARK_SCREAM_FIRE, &MoveBehavior::Move_MODEL_DARK_SCREAM_FIRE},
        {MODEL_DARK_SCREAM, &MoveBehavior::Move_MODEL_DARK_SCREAM_FIRE},
        {MODEL_CURSEDTEMPLE_HOLYITEM, &MoveBehavior::Move_MODEL_CURSEDTEMPLE_HOLYITEM},
        {MODEL_CURSEDTEMPLE_PRODECTION_SKILL,
         &MoveBehavior::Move_MODEL_CURSEDTEMPLE_PRODECTION_SKILL},
        {MODEL_CURSEDTEMPLE_RESTRAINT_SKILL,
         &MoveBehavior::Move_MODEL_CURSEDTEMPLE_RESTRAINT_SKILL},
        {MODEL_ARROW_SPARK, &MoveBehavior::Move_MODEL_ARROW_SPARK},
        {MODEL_ARROW_RING, &MoveBehavior::Move_MODEL_ARROW_RING},
        {MODEL_ARROW_TANKER, &MoveBehavior::Move_MODEL_ARROW_TANKER},
        {MODEL_ARROW_BOMB, &MoveBehavior::Move_MODEL_ARROW_BOMB},
        {MODEL_ARROW_DARKSTINGER, &MoveBehavior::Move_MODEL_ARROW_DARKSTINGER},
        {MODEL_DUNGEON_STONE01, &MoveBehavior::Move_MODEL_DUNGEON_STONE01},
        {MODEL_WARCRAFT, &MoveBehavior::Move_MODEL_WARCRAFT},
        {BITMAP_FIRECRACKERRISE, &MoveBehavior::Move_BITMAP_FIRECRACKERRISE},
        {BITMAP_FIRECRACKER, &MoveBehavior::Move_BITMAP_FIRECRACKER},
        {BITMAP_FIRECRACKER0001, &MoveBehavior::Move_BITMAP_FIRECRACKER0001},
        {BITMAP_FIRECRACKER0002, &MoveBehavior::Move_BITMAP_FIRECRACKER0002},
        {BITMAP_FIRECRACKER0003, &MoveBehavior::Move_BITMAP_FIRECRACKER0003},
        {BITMAP_SWORD_FORCE, &MoveBehavior::Move_BITMAP_SWORD_FORCE},
        {BITMAP_BLIZZARD, &MoveBehavior::Move_BITMAP_BLIZZARD},
        {BITMAP_SHOTGUN, &MoveBehavior::Move_BITMAP_SHOTGUN},
        {MODEL_SHINE, &MoveBehavior::Move_MODEL_SHINE},
        {MODEL_BLIZZARD, &MoveBehavior::Move_MODEL_BLIZZARD},
        {MODEL_ARROW_DRILL, &MoveBehavior::Move_MODEL_ARROW_DRILL},
        {MODEL_COMBO, &MoveBehavior::Move_MODEL_COMBO},
        {MODEL_WAVES, &MoveBehavior::Move_MODEL_WAVES},
        {MODEL_AIR_FORCE, &MoveBehavior::Move_MODEL_AIR_FORCE},
        {MODEL_PIERCING2, &MoveBehavior::Move_MODEL_PIERCING2},
        {MODEL_DEASULER, &MoveBehavior::Move_MODEL_DEASULER},
        {MODEL_DEATH_SPI_SKILL, &MoveBehavior::Move_MODEL_DEATH_SPI_SKILL},
        {MODEL_PIER_PART, &MoveBehavior::Move_MODEL_PIER_PART},
        {BITMAP_FLARE_FORCE, &MoveBehavior::Move_BITMAP_FLARE_FORCE},
        {MODEL_DARKLORD_SKILL, &MoveBehavior::Move_MODEL_DARKLORD_SKILL},
        {MODEL_GROUND_STONE, &MoveBehavior::Move_MODEL_GROUND_STONE},
        {MODEL_GROUND_STONE2, &MoveBehavior::Move_MODEL_GROUND_STONE},
        {BITMAP_TWLIGHT, &MoveBehavior::Move_BITMAP_TWLIGHT},
        {BITMAP_SHOCK_WAVE, &MoveBehavior::Move_BITMAP_SHOCK_WAVE},
        {BITMAP_DAMAGE_01_MONO, &MoveBehavior::Move_BITMAP_DAMAGE_01_MONO},
        {BITMAP_FLARE, &MoveBehavior::Move_BITMAP_FLARE},
        {MODEL_CUNDUN_DRAGON_HEAD, &MoveBehavior::Move_MODEL_CUNDUN_DRAGON_HEAD},
        {MODEL_CUNDUN_PHOENIX, &MoveBehavior::Move_MODEL_CUNDUN_PHOENIX},
        {MODEL_CUNDUN_GHOST, &MoveBehavior::Move_MODEL_CUNDUN_GHOST},
        {MODEL_CUNDUN_SKILL, &MoveBehavior::Move_MODEL_CUNDUN_SKILL},
        {MODEL_BATTLE_GUARD2, &MoveBehavior::Move_MODEL_BATTLE_GUARD2},
        {MODEL_ARROW_TANKER_HIT, &MoveBehavior::Move_MODEL_ARROW_TANKER_HIT},
        {MODEL_FLY_BIG_STONE1, &MoveBehavior::Move_MODEL_FLY_BIG_STONE1},
        {MODEL_FLY_BIG_STONE2, &MoveBehavior::Move_MODEL_FLY_BIG_STONE2},
        {MODEL_BIG_STONE_PART1, &MoveBehavior::Move_MODEL_BIG_STONE_PART1},
        {MODEL_BIG_STONE_PART2, &MoveBehavior::Move_MODEL_BIG_STONE_PART1},
        {MODEL_WALL_PART1, &MoveBehavior::Move_MODEL_BIG_STONE_PART1},
        {MODEL_WALL_PART2, &MoveBehavior::Move_MODEL_BIG_STONE_PART1},
        {MODEL_GOLEM_STONE, &MoveBehavior::Move_MODEL_BIG_STONE_PART1},
        {MODEL_GATE_PART1, &MoveBehavior::Move_MODEL_GATE_PART1},
        {MODEL_GATE_PART2, &MoveBehavior::Move_MODEL_GATE_PART1},
        {MODEL_GATE_PART3, &MoveBehavior::Move_MODEL_GATE_PART1},
        {MODEL_AURORA, &MoveBehavior::Move_MODEL_AURORA},
        {MODEL_FENRIR_THUNDER, &MoveBehavior::Move_MODEL_FENRIR_THUNDER},
        {MODEL_FALL_STONE_EFFECT, &MoveBehavior::Move_MODEL_FALL_STONE_EFFECT},
        {MODEL_FENRIR_FOOT_THUNDER, &MoveBehavior::Move_MODEL_FENRIR_FOOT_THUNDER},
        {MODEL_TWINTAIL_EFFECT, &MoveBehavior::Move_MODEL_TWINTAIL_EFFECT},
        {MODEL_TOWER_GATE_PLANE, &MoveBehavior::Move_MODEL_TOWER_GATE_PLANE},
        {BITMAP_CRATER, &MoveBehavior::Move_BITMAP_CRATER},
        {BITMAP_CHROME_ENERGY2, &MoveBehavior::Move_BITMAP_CHROME_ENERGY2},
        {MODEL_STUN_STONE, &MoveBehavior::Move_MODEL_STUN_STONE},
        {MODEL_SKIN_SHELL, &MoveBehavior::Move_MODEL_SKIN_SHELL},
        {MODEL_MANA_RUNE, &MoveBehavior::Move_MODEL_MANA_RUNE},
        {MODEL_SKILL_JAVELIN, &MoveBehavior::Move_MODEL_SKILL_JAVELIN},
        {MODEL_ARROW_IMPACT, &MoveBehavior::Move_MODEL_ARROW_IMPACT},
        {MODEL_SWORD_FORCE, &MoveBehavior::Move_MODEL_SWORD_FORCE},
        {MODEL_PROTECTGUILD, &MoveBehavior::Move_MODEL_PROTECTGUILD},
        {MODEL_MOVE_TARGETPOSITION_EFFECT, &MoveBehavior::Move_MODEL_MOVE_TARGETPOSITION_EFFECT},
        {BITMAP_TARGET_POSITION_EFFECT1, &MoveBehavior::Move_BITMAP_TARGET_POSITION_EFFECT1},
        {BITMAP_TARGET_POSITION_EFFECT2, &MoveBehavior::Move_BITMAP_TARGET_POSITION_EFFECT2},
        {MODEL_EFFECT_SAPITRES_ATTACK, &MoveBehavior::Move_MODEL_EFFECT_SAPITRES_ATTACK},
        {MODEL_EFFECT_THUNDER_NAPIN_ATTACK_1,
         &MoveBehavior::Move_MODEL_EFFECT_THUNDER_NAPIN_ATTACK_1},
        {MODEL_EFFECT_SKURA_ITEM, &MoveBehavior::Move_MODEL_EFFECT_SKURA_ITEM},
        {MODEL_BLOW_OF_DESTRUCTION, &MoveBehavior::Move_MODEL_BLOW_OF_DESTRUCTION},
        {MODEL_NIGHTWATER_01, &MoveBehavior::Move_MODEL_NIGHTWATER_01},
        {MODEL_KNIGHT_PLANCRACK_A, &MoveBehavior::Move_MODEL_KNIGHT_PLANCRACK_A},
        {MODEL_KNIGHT_PLANCRACK_B, &MoveBehavior::Move_MODEL_KNIGHT_PLANCRACK_B},
        {MODEL_EFFECT_FLAME_STRIKE, &MoveBehavior::Move_MODEL_EFFECT_FLAME_STRIKE},
        {MODEL_1_STREAMBREATHFIRE, &MoveBehavior::Move_MODEL_1_STREAMBREATHFIRE},
        {MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_HEAD,
         &MoveBehavior::Move_MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_HEAD},
        {MODEL_PKFIELD_ASSASSIN_EFFECT_RED_HEAD,
         &MoveBehavior::Move_MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_HEAD},
        {MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_BODY,
         &MoveBehavior::Move_MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_BODY},
        {MODEL_PKFIELD_ASSASSIN_EFFECT_RED_BODY,
         &MoveBehavior::Move_MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_BODY},
        {MODEL_LAVAGIANT_FOOTPRINT_R, &MoveBehavior::Move_MODEL_LAVAGIANT_FOOTPRINT_R},
        {MODEL_LAVAGIANT_FOOTPRINT_V, &MoveBehavior::Move_MODEL_LAVAGIANT_FOOTPRINT_R},
        {MODEL_PROJECTILE, &MoveBehavior::Move_MODEL_PROJECTILE},
        {MODEL_DOOR_CRUSH_EFFECT_PIECE01, &MoveBehavior::Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01},
        {MODEL_DOOR_CRUSH_EFFECT_PIECE02, &MoveBehavior::Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01},
        {MODEL_DOOR_CRUSH_EFFECT_PIECE03, &MoveBehavior::Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01},
        {MODEL_DOOR_CRUSH_EFFECT_PIECE04, &MoveBehavior::Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01},
        {MODEL_DOOR_CRUSH_EFFECT_PIECE05, &MoveBehavior::Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01},
        {MODEL_DOOR_CRUSH_EFFECT_PIECE06, &MoveBehavior::Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01},
        {MODEL_DOOR_CRUSH_EFFECT_PIECE07, &MoveBehavior::Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01},
        {MODEL_DOOR_CRUSH_EFFECT_PIECE08, &MoveBehavior::Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01},
        {MODEL_DOOR_CRUSH_EFFECT_PIECE09, &MoveBehavior::Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01},
        {MODEL_DOOR_CRUSH_EFFECT_PIECE11, &MoveBehavior::Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01},
        {MODEL_DOOR_CRUSH_EFFECT_PIECE12, &MoveBehavior::Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01},
        {MODEL_DOOR_CRUSH_EFFECT_PIECE13, &MoveBehavior::Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01},
        {MODEL_STATUE_CRUSH_EFFECT_PIECE01, &MoveBehavior::Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01},
        {MODEL_STATUE_CRUSH_EFFECT_PIECE02, &MoveBehavior::Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01},
        {MODEL_STATUE_CRUSH_EFFECT_PIECE03, &MoveBehavior::Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01},
        {MODEL_STATUE_CRUSH_EFFECT_PIECE04, &MoveBehavior::Move_MODEL_STATUE_CRUSH_EFFECT_PIECE04},
        {MODEL_DOOR_CRUSH_EFFECT_PIECE10, &MoveBehavior::Move_MODEL_STATUE_CRUSH_EFFECT_PIECE04},
        {MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_,
         &MoveBehavior::Move_MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_},
        {MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_,
         &MoveBehavior::Move_MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_},
        {MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_,
         &MoveBehavior::Move_MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_},
        {MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_,
         &MoveBehavior::Move_MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_},
        {MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_,
         &MoveBehavior::Move_MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_},
        {MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE,
         &MoveBehavior::Move_MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_},
        {MODEL_EMPIREGUARDIAN_BLOW_OF_DESTRUCTION,
         &MoveBehavior::Move_MODEL_EMPIREGUARDIAN_BLOW_OF_DESTRUCTION},
        {MODEL_EFFECT_SD_AURA, &MoveBehavior::Move_MODEL_EFFECT_SD_AURA},
        {BITMAP_WATERFALL_4, &MoveBehavior::Move_BITMAP_WATERFALL_4},
        {MODEL_WOLF_HEAD_EFFECT, &MoveBehavior::Move_MODEL_WOLF_HEAD_EFFECT},
        {BITMAP_SBUMB, &MoveBehavior::Move_BITMAP_SBUMB},
        {MODEL_DOWN_ATTACK_DUMMY_L, &MoveBehavior::Move_MODEL_DOWN_ATTACK_DUMMY_L},
        {MODEL_DOWN_ATTACK_DUMMY_R, &MoveBehavior::Move_MODEL_DOWN_ATTACK_DUMMY_R},
        {BITMAP_SWORDEFF, &MoveBehavior::Move_BITMAP_SWORDEFF},
        {MODEL_SHOCKWAVE01, &MoveBehavior::Move_MODEL_SHOCKWAVE01},
        {MODEL_SHOCKWAVE02, &MoveBehavior::Move_MODEL_SHOCKWAVE02},
        {BITMAP_DAMAGE1, &MoveBehavior::Move_BITMAP_DAMAGE1},
        {MODEL_SHOCKWAVE_SPIN01, &MoveBehavior::Move_MODEL_SHOCKWAVE_SPIN01},
        {BITMAP_EVENT_CLOUD, &MoveBehavior::Move_BITMAP_EVENT_CLOUD},
        {MODEL_WINDFOCE, &MoveBehavior::Move_MODEL_WINDFOCE},
        {BITMAP_LIGHT_RED, &MoveBehavior::Move_BITMAP_LIGHT_RED},
        {MODEL_WINDFOCE_MIRROR, &MoveBehavior::Move_MODEL_WINDFOCE_MIRROR},
        {BITMAP_SWORD_EFFECT_MONO, &MoveBehavior::Move_BITMAP_SWORD_EFFECT_MONO},
        {MODEL_WOLF_HEAD_EFFECT2, &MoveBehavior::Move_MODEL_WOLF_HEAD_EFFECT2},
        {MODEL_SHOCKWAVE_GROUND01, &MoveBehavior::Move_MODEL_SHOCKWAVE_GROUND01},
        {MODEL_DRAGON_KICK_DUMMY, &MoveBehavior::Move_MODEL_DRAGON_KICK_DUMMY},
        {BITMAP_LAVA, &MoveBehavior::Move_BITMAP_LAVA},
        {MODEL_DRAGON_LOWER_DUMMY, &MoveBehavior::Move_MODEL_DRAGON_LOWER_DUMMY},
        {MODEL_TARGETMON_EFFECT, &MoveBehavior::Move_MODEL_TARGETMON_EFFECT},
        {MODEL_VOLCANO_OF_MONK, &MoveBehavior::Move_MODEL_VOLCANO_OF_MONK},
        {MODEL_VOLCANO_STONE, &MoveBehavior::Move_MODEL_VOLCANO_STONE},
    };
    return handlers;
}
} // namespace GameLogic::Effects::Behaviors
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_DRAGON(OBJECT *o, int index,
                                                                               float Luminosity)
{
    return owner_.Move_MODEL_DRAGON(o, index, Luminosity);
} // OMF-01258
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_ARROW_AUTOLOAD(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_ARROW_AUTOLOAD(o, index, Luminosity);
} // OMF-01259
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_INFINITY_ARROW(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_INFINITY_ARROW(o, index, Luminosity);
} // OMF-01260
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_INFINITY_ARROW1(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_INFINITY_ARROW1(o, index, Luminosity);
} // OMF-01261
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SHIELD_CRASH(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SHIELD_CRASH(o, index, Luminosity);
} // OMF-01262
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SHIELD_CRASH2(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SHIELD_CRASH2(o, index, Luminosity);
} // OMF-01263
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_IRON_RIDER_ARROW(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_IRON_RIDER_ARROW(o, index, Luminosity);
} // OMF-01264
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_MULTI_SHOT3(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_MULTI_SHOT3(o, index, Luminosity);
} // OMF-01265
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_MULTI_SHOT1(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_MULTI_SHOT1(o, index, Luminosity);
} // OMF-01266
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_MULTI_SHOT2(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_MULTI_SHOT2(o, index, Luminosity);
} // OMF-01267
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_BLADE_SKILL(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_BLADE_SKILL(o, index, Luminosity);
} // OMF-01268
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_KENTAUROS_ARROW(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_KENTAUROS_ARROW(o, index, Luminosity);
} // OMF-01269
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_WARP3(OBJECT *o, int index,
                                                                              float Luminosity)
{
    return owner_.Move_MODEL_WARP3(o, index, Luminosity);
} // OMF-01270
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_GHOST(OBJECT *o, int index,
                                                                              float Luminosity)
{
    return owner_.Move_MODEL_GHOST(o, index, Luminosity);
} // OMF-01271
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_TREE_ATTACK(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_TREE_ATTACK(o, index, Luminosity);
} // OMF-01272
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_BUTTERFLY01(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_BUTTERFLY01(o, index, Luminosity);
} // OMF-01273
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_SKULL(OBJECT *o, int index,
                                                                               float Luminosity)
{
    return owner_.Move_BITMAP_SKULL(o, index, Luminosity);
} // OMF-01274
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL__SPEAR(OBJECT *o, int index,
                                                                               float Luminosity)
{
    return owner_.Move_MODEL__SPEAR(o, index, Luminosity);
} // OMF-01275
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_HALLOWEEN_CANDY_BLUE(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_HALLOWEEN_CANDY_BLUE(o, index, Luminosity);
} // OMF-01276
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_HALLOWEEN_EX(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_HALLOWEEN_EX(o, index, Luminosity);
} // OMF-01277
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_XMAS_EVENT_BOX(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_XMAS_EVENT_BOX(o, index, Luminosity);
} // OMF-01278
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_XMAS_EVENT_ICEHEART(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_XMAS_EVENT_ICEHEART(o, index, Luminosity);
} // OMF-01279
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_NEWYEARSDAY_EVENT_BEKSULKI(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_NEWYEARSDAY_EVENT_BEKSULKI(o, index, Luminosity);
} // OMF-01280
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_MOONHARVEST_MOON(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_MOONHARVEST_MOON(o, index, Luminosity);
} // OMF-01281
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_MOONHARVEST_GAM(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_MOONHARVEST_GAM(o, index, Luminosity);
} // OMF-01282
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SPEARSKILL(OBJECT *o,
                                                                                   int index,
                                                                                   float Luminosity)
{
    return owner_.Move_MODEL_SPEARSKILL(o, index, Luminosity);
} // OMF-01283
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_FIRE_CURSEDLICH(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_FIRE_CURSEDLICH(o, index, Luminosity);
} // OMF-01284
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SUMMONER_WRISTRING_EFFECT(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SUMMONER_WRISTRING_EFFECT(o, index, Luminosity);
} // OMF-01285
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::
    Move_MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT(OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT(o, index, Luminosity);
} // OMF-01286
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SUMMONER_EQUIP_HEAD_NEIL(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SUMMONER_EQUIP_HEAD_NEIL(o, index, Luminosity);
} // OMF-01287
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SUMMONER_CASTING_EFFECT1(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SUMMONER_CASTING_EFFECT1(o, index, Luminosity);
} // OMF-01288
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SUMMONER_SUMMON_SAHAMUTT(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SUMMONER_SUMMON_SAHAMUTT(o, index, Luminosity);
} // OMF-01289
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SUMMONER_SUMMON_NEIL(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SUMMONER_SUMMON_NEIL(o, index, Luminosity);
} // OMF-01290
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SUMMONER_SUMMON_LAGUL(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SUMMONER_SUMMON_LAGUL(o, index, Luminosity);
} // OMF-01291
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_MAGIC(OBJECT *o, int index,
                                                                               float Luminosity)
{
    return owner_.Move_BITMAP_MAGIC(o, index, Luminosity);
} // OMF-01292
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_OUR_INFLUENCE_GROUND(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_OUR_INFLUENCE_GROUND(o, index, Luminosity);
} // OMF-01293
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_MAGIC_ZIN(OBJECT *o,
                                                                                   int index,
                                                                                   float Luminosity)
{
    return owner_.Move_BITMAP_MAGIC_ZIN(o, index, Luminosity);
} // OMF-01294
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_PIN_LIGHT(OBJECT *o,
                                                                                   int index,
                                                                                   float Luminosity)
{
    return owner_.Move_BITMAP_PIN_LIGHT(o, index, Luminosity);
} // OMF-01295
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_ORORA(OBJECT *o, int index,
                                                                               float Luminosity)
{
    return owner_.Move_BITMAP_ORORA(o, index, Luminosity);
} // OMF-01296
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_GATHERING(OBJECT *o,
                                                                                   int index,
                                                                                   float Luminosity)
{
    return owner_.Move_BITMAP_GATHERING(o, index, Luminosity);
} // OMF-01297
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_JOINT_THUNDER(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_JOINT_THUNDER(o, index, Luminosity);
} // OMF-01298
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_IMPACT(OBJECT *o,
                                                                                int index,
                                                                                float Luminosity)
{
    return owner_.Move_BITMAP_IMPACT(o, index, Luminosity);
} // OMF-01299
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_FLAME(OBJECT *o, int index,
                                                                               float Luminosity)
{
    return owner_.Move_BITMAP_FLAME(o, index, Luminosity);
} // OMF-01300
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_RAKLION_BOSS_CRACKEFFECT(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_RAKLION_BOSS_CRACKEFFECT(o, index, Luminosity);
} // OMF-01301
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_RAKLION_BOSS_MAGIC(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_RAKLION_BOSS_MAGIC(o, index, Luminosity);
} // OMF-01302
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_FIRE_HIK2_MONO(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_FIRE_HIK2_MONO(o, index, Luminosity);
} // OMF-01303
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_CLOUD(OBJECT *o, int index,
                                                                               float Luminosity)
{
    return owner_.Move_BITMAP_CLOUD(o, index, Luminosity);
} // OMF-01304
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_CHAIN_LIGHTNING(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_CHAIN_LIGHTNING(o, index, Luminosity);
} // OMF-01305
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_ALICE_DRAIN_LIFE(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_ALICE_DRAIN_LIFE(o, index, Luminosity);
} // OMF-01306
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_ALICE_BUFFSKILL_EFFECT(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_ALICE_BUFFSKILL_EFFECT(o, index, Luminosity);
} // OMF-01307
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_LIGHTNING_SHOCK(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_LIGHTNING_SHOCK(o, index, Luminosity);
} // OMF-01308
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SKILL_BLAST(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SKILL_BLAST(o, index, Luminosity);
} // OMF-01309
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_WAVE(OBJECT *o, int index,
                                                                             float Luminosity)
{
    return owner_.Move_MODEL_WAVE(o, index, Luminosity);
} // OMF-01310
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_TAIL(OBJECT *o, int index,
                                                                             float Luminosity)
{
    return owner_.Move_MODEL_TAIL(o, index, Luminosity);
} // OMF-01311
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_WAVE_FORCE(OBJECT *o,
                                                                                   int index,
                                                                                   float Luminosity)
{
    return owner_.Move_MODEL_WAVE_FORCE(o, index, Luminosity);
} // OMF-01312
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SKILL_INFERNO(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SKILL_INFERNO(o, index, Luminosity);
} // OMF-01313
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_MAGIC_CIRCLE1(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_MAGIC_CIRCLE1(o, index, Luminosity);
} // OMF-01314
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_PROTECT(OBJECT *o,
                                                                                int index,
                                                                                float Luminosity)
{
    return owner_.Move_MODEL_PROTECT(o, index, Luminosity);
} // OMF-01315
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_POISON(OBJECT *o, int index,
                                                                               float Luminosity)
{
    return owner_.Move_MODEL_POISON(o, index, Luminosity);
} // OMF-01316
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SAW(OBJECT *o, int index,
                                                                            float Luminosity)
{
    return owner_.Move_MODEL_SAW(o, index, Luminosity);
} // OMF-01317
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_LASER(OBJECT *o, int index,
                                                                              float Luminosity)
{
    return owner_.Move_MODEL_LASER(o, index, Luminosity);
} // OMF-01318
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SKILL_WHEEL1(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SKILL_WHEEL1(o, index, Luminosity);
} // OMF-01319
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SKILL_WHEEL2(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SKILL_WHEEL2(o, index, Luminosity);
} // OMF-01320
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SKILL_FISSURE(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SKILL_FISSURE(o, index, Luminosity);
} // OMF-01321
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_FISSURE(OBJECT *o,
                                                                                int index,
                                                                                float Luminosity)
{
    return owner_.Move_MODEL_FISSURE(o, index, Luminosity);
} // OMF-01322
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SKILL_FURY_STRIKE(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SKILL_FURY_STRIKE(o, index, Luminosity);
} // OMF-01323
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_BALGAS_SKILL(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_BALGAS_SKILL(o, index, Luminosity);
} // OMF-01324
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_CHANGE_UP_EFF(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_CHANGE_UP_EFF(o, index, Luminosity);
} // OMF-01325
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_CHANGE_UP_NASA(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_CHANGE_UP_NASA(o, index, Luminosity);
} // OMF-01326
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_CHANGE_UP_CYLINDER(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_CHANGE_UP_CYLINDER(o, index, Luminosity);
} // OMF-01327
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_DARK_ELF_SKILL(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_DARK_ELF_SKILL(o, index, Luminosity);
} // OMF-01328
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_MAGIC2(OBJECT *o, int index,
                                                                               float Luminosity)
{
    return owner_.Move_MODEL_MAGIC2(o, index, Luminosity);
} // OMF-01329
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_STORM(OBJECT *o, int index,
                                                                              float Luminosity)
{
    return owner_.Move_MODEL_STORM(o, index, Luminosity);
} // OMF-01330
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SUMMON(OBJECT *o, int index,
                                                                               float Luminosity)
{
    return owner_.Move_MODEL_SUMMON(o, index, Luminosity);
} // OMF-01331
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_STORM2(OBJECT *o, int index,
                                                                               float Luminosity)
{
    return owner_.Move_MODEL_STORM2(o, index, Luminosity);
} // OMF-01332
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_STORM3(OBJECT *o, int index,
                                                                               float Luminosity)
{
    return owner_.Move_MODEL_STORM3(o, index, Luminosity);
} // OMF-01333
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_MAYASTONE1(OBJECT *o,
                                                                                   int index,
                                                                                   float Luminosity)
{
    return owner_.Move_MODEL_MAYASTONE1(o, index, Luminosity);
} // OMF-01334
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_MAYASTONE4(OBJECT *o,
                                                                                   int index,
                                                                                   float Luminosity)
{
    return owner_.Move_MODEL_MAYASTONE4(o, index, Luminosity);
} // OMF-01335
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_MAYASTONEFIRE(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_MAYASTONEFIRE(o, index, Luminosity);
} // OMF-01336
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_MAYAHANDSKILL(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_MAYAHANDSKILL(o, index, Luminosity);
} // OMF-01337
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_CIRCLE(OBJECT *o, int index,
                                                                               float Luminosity)
{
    return owner_.Move_MODEL_CIRCLE(o, index, Luminosity);
} // OMF-01338
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_CIRCLE_LIGHT(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_CIRCLE_LIGHT(o, index, Luminosity);
} // OMF-01339
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_ICE_SMALL(OBJECT *o,
                                                                                  int index,
                                                                                  float Luminosity)
{
    return owner_.Move_MODEL_ICE_SMALL(o, index, Luminosity);
} // OMF-01340
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SKULL(OBJECT *o, int index,
                                                                              float Luminosity)
{
    return owner_.Move_MODEL_SKULL(o, index, Luminosity);
} // OMF-01341
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_CUNDUN_PART1(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_CUNDUN_PART1(o, index, Luminosity);
} // OMF-01342
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_CURSEDTEMPLE_STATUE_PART1(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_CURSEDTEMPLE_STATUE_PART1(o, index, Luminosity);
} // OMF-01343
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_XMAS2008_SNOWMAN_HEAD(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_XMAS2008_SNOWMAN_HEAD(o, index, Luminosity);
} // OMF-01344
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_XMAS2008_SNOWMAN_BODY(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_XMAS2008_SNOWMAN_BODY(o, index, Luminosity);
} // OMF-01345
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_DOPPELGANGER_SLIME_CHIP(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_DOPPELGANGER_SLIME_CHIP(o, index, Luminosity);
} // OMF-01346
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_WATER_WAVE(OBJECT *o,
                                                                                   int index,
                                                                                   float Luminosity)
{
    return owner_.Move_MODEL_WATER_WAVE(o, index, Luminosity);
} // OMF-01347
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_STAFF_OF_DESTRUCTION(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_STAFF_OF_DESTRUCTION(o, index, Luminosity);
} // OMF-01348
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_PIERCING(OBJECT *o,
                                                                                 int index,
                                                                                 float Luminosity)
{
    return owner_.Move_MODEL_PIERCING(o, index, Luminosity);
} // OMF-01349
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_ARROW_BEST_CROSSBOW(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_ARROW_BEST_CROSSBOW(o, index, Luminosity);
} // OMF-01350
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_ARROW_DOUBLE(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_ARROW_DOUBLE(o, index, Luminosity);
} // OMF-01351
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_ARROW_HOLY(OBJECT *o,
                                                                                   int index,
                                                                                   float Luminosity)
{
    return owner_.Move_MODEL_ARROW_HOLY(o, index, Luminosity);
} // OMF-01352
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_ARROW(OBJECT *o, int index,
                                                                              float Luminosity)
{
    return owner_.Move_MODEL_ARROW(o, index, Luminosity);
} // OMF-01353
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_ARROW_STEEL(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_ARROW_STEEL(o, index, Luminosity);
} // OMF-01354
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_DARK_SCREAM_FIRE(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_DARK_SCREAM_FIRE(o, index, Luminosity);
} // OMF-01355
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_CURSEDTEMPLE_HOLYITEM(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_CURSEDTEMPLE_HOLYITEM(o, index, Luminosity);
} // OMF-01356
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::
    Move_MODEL_CURSEDTEMPLE_PRODECTION_SKILL(OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_CURSEDTEMPLE_PRODECTION_SKILL(o, index, Luminosity);
} // OMF-01357
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::
    Move_MODEL_CURSEDTEMPLE_RESTRAINT_SKILL(OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_CURSEDTEMPLE_RESTRAINT_SKILL(o, index, Luminosity);
} // OMF-01358
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_ARROW_SPARK(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_ARROW_SPARK(o, index, Luminosity);
} // OMF-01359
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_ARROW_RING(OBJECT *o,
                                                                                   int index,
                                                                                   float Luminosity)
{
    return owner_.Move_MODEL_ARROW_RING(o, index, Luminosity);
} // OMF-01360
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_ARROW_TANKER(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_ARROW_TANKER(o, index, Luminosity);
} // OMF-01361
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_ARROW_BOMB(OBJECT *o,
                                                                                   int index,
                                                                                   float Luminosity)
{
    return owner_.Move_MODEL_ARROW_BOMB(o, index, Luminosity);
} // OMF-01362
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_ARROW_DARKSTINGER(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_ARROW_DARKSTINGER(o, index, Luminosity);
} // OMF-01363
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_DUNGEON_STONE01(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_DUNGEON_STONE01(o, index, Luminosity);
} // OMF-01364
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_WARCRAFT(OBJECT *o,
                                                                                 int index,
                                                                                 float Luminosity)
{
    return owner_.Move_MODEL_WARCRAFT(o, index, Luminosity);
} // OMF-01365
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_FIRECRACKERRISE(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_FIRECRACKERRISE(o, index, Luminosity);
} // OMF-01366
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_FIRECRACKER(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_FIRECRACKER(o, index, Luminosity);
} // OMF-01367
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_FIRECRACKER0001(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_FIRECRACKER0001(o, index, Luminosity);
} // OMF-01368
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_FIRECRACKER0002(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_FIRECRACKER0002(o, index, Luminosity);
} // OMF-01369
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_FIRECRACKER0003(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_FIRECRACKER0003(o, index, Luminosity);
} // OMF-01370
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_SWORD_FORCE(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_SWORD_FORCE(o, index, Luminosity);
} // OMF-01371
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_BLIZZARD(OBJECT *o,
                                                                                  int index,
                                                                                  float Luminosity)
{
    return owner_.Move_BITMAP_BLIZZARD(o, index, Luminosity);
} // OMF-01372
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_SHOTGUN(OBJECT *o,
                                                                                 int index,
                                                                                 float Luminosity)
{
    return owner_.Move_BITMAP_SHOTGUN(o, index, Luminosity);
} // OMF-01373
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SHINE(OBJECT *o, int index,
                                                                              float Luminosity)
{
    return owner_.Move_MODEL_SHINE(o, index, Luminosity);
} // OMF-01374
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_BLIZZARD(OBJECT *o,
                                                                                 int index,
                                                                                 float Luminosity)
{
    return owner_.Move_MODEL_BLIZZARD(o, index, Luminosity);
} // OMF-01375
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_ARROW_DRILL(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_ARROW_DRILL(o, index, Luminosity);
} // OMF-01376
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_COMBO(OBJECT *o, int index,
                                                                              float Luminosity)
{
    return owner_.Move_MODEL_COMBO(o, index, Luminosity);
} // OMF-01377
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_WAVES(OBJECT *o, int index,
                                                                              float Luminosity)
{
    return owner_.Move_MODEL_WAVES(o, index, Luminosity);
} // OMF-01378
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_AIR_FORCE(OBJECT *o,
                                                                                  int index,
                                                                                  float Luminosity)
{
    return owner_.Move_MODEL_AIR_FORCE(o, index, Luminosity);
} // OMF-01379
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_PIERCING2(OBJECT *o,
                                                                                  int index,
                                                                                  float Luminosity)
{
    return owner_.Move_MODEL_PIERCING2(o, index, Luminosity);
} // OMF-01380
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_DEASULER(OBJECT *o,
                                                                                 int index,
                                                                                 float Luminosity)
{
    return owner_.Move_MODEL_DEASULER(o, index, Luminosity);
} // OMF-01381
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_DEATH_SPI_SKILL(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_DEATH_SPI_SKILL(o, index, Luminosity);
} // OMF-01382
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_PIER_PART(OBJECT *o,
                                                                                  int index,
                                                                                  float Luminosity)
{
    return owner_.Move_MODEL_PIER_PART(o, index, Luminosity);
} // OMF-01383
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_FLARE_FORCE(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_FLARE_FORCE(o, index, Luminosity);
} // OMF-01384
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_DARKLORD_SKILL(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_DARKLORD_SKILL(o, index, Luminosity);
} // OMF-01385
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_GROUND_STONE(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_GROUND_STONE(o, index, Luminosity);
} // OMF-01386
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_TWLIGHT(OBJECT *o,
                                                                                 int index,
                                                                                 float Luminosity)
{
    return owner_.Move_BITMAP_TWLIGHT(o, index, Luminosity);
} // OMF-01387
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_SHOCK_WAVE(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_SHOCK_WAVE(o, index, Luminosity);
} // OMF-01388
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_DAMAGE_01_MONO(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_DAMAGE_01_MONO(o, index, Luminosity);
} // OMF-01389
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_FLARE(OBJECT *o, int index,
                                                                               float Luminosity)
{
    return owner_.Move_BITMAP_FLARE(o, index, Luminosity);
} // OMF-01390
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_CUNDUN_DRAGON_HEAD(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_CUNDUN_DRAGON_HEAD(o, index, Luminosity);
} // OMF-01391
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_CUNDUN_PHOENIX(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_CUNDUN_PHOENIX(o, index, Luminosity);
} // OMF-01392
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_CUNDUN_GHOST(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_CUNDUN_GHOST(o, index, Luminosity);
} // OMF-01393
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_CUNDUN_SKILL(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_CUNDUN_SKILL(o, index, Luminosity);
} // OMF-01394
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_BATTLE_GUARD2(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_BATTLE_GUARD2(o, index, Luminosity);
} // OMF-01395
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_ARROW_TANKER_HIT(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_ARROW_TANKER_HIT(o, index, Luminosity);
} // OMF-01396
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_FLY_BIG_STONE1(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_FLY_BIG_STONE1(o, index, Luminosity);
} // OMF-01397
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_FLY_BIG_STONE2(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_FLY_BIG_STONE2(o, index, Luminosity);
} // OMF-01398
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_BIG_STONE_PART1(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_BIG_STONE_PART1(o, index, Luminosity);
} // OMF-01399
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_GATE_PART1(OBJECT *o,
                                                                                   int index,
                                                                                   float Luminosity)
{
    return owner_.Move_MODEL_GATE_PART1(o, index, Luminosity);
} // OMF-01400
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_AURORA(OBJECT *o, int index,
                                                                               float Luminosity)
{
    return owner_.Move_MODEL_AURORA(o, index, Luminosity);
} // OMF-01401
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_FENRIR_THUNDER(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_FENRIR_THUNDER(o, index, Luminosity);
} // OMF-01402
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_FALL_STONE_EFFECT(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_FALL_STONE_EFFECT(o, index, Luminosity);
} // OMF-01403
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_FENRIR_FOOT_THUNDER(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_FENRIR_FOOT_THUNDER(o, index, Luminosity);
} // OMF-01404
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_TWINTAIL_EFFECT(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_TWINTAIL_EFFECT(o, index, Luminosity);
} // OMF-01405
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_TOWER_GATE_PLANE(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_TOWER_GATE_PLANE(o, index, Luminosity);
} // OMF-01406
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_CRATER(OBJECT *o,
                                                                                int index,
                                                                                float Luminosity)
{
    return owner_.Move_BITMAP_CRATER(o, index, Luminosity);
} // OMF-01407
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_CHROME_ENERGY2(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_CHROME_ENERGY2(o, index, Luminosity);
} // OMF-01408
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_STUN_STONE(OBJECT *o,
                                                                                   int index,
                                                                                   float Luminosity)
{
    return owner_.Move_MODEL_STUN_STONE(o, index, Luminosity);
} // OMF-01409
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SKIN_SHELL(OBJECT *o,
                                                                                   int index,
                                                                                   float Luminosity)
{
    return owner_.Move_MODEL_SKIN_SHELL(o, index, Luminosity);
} // OMF-01410
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_MANA_RUNE(OBJECT *o,
                                                                                  int index,
                                                                                  float Luminosity)
{
    return owner_.Move_MODEL_MANA_RUNE(o, index, Luminosity);
} // OMF-01411
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SKILL_JAVELIN(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SKILL_JAVELIN(o, index, Luminosity);
} // OMF-01412
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_ARROW_IMPACT(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_ARROW_IMPACT(o, index, Luminosity);
} // OMF-01413
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SWORD_FORCE(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SWORD_FORCE(o, index, Luminosity);
} // OMF-01414
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_PROTECTGUILD(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_PROTECTGUILD(o, index, Luminosity);
} // OMF-01415
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_MOVE_TARGETPOSITION_EFFECT(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_MOVE_TARGETPOSITION_EFFECT(o, index, Luminosity);
} // OMF-01416
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_TARGET_POSITION_EFFECT1(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_TARGET_POSITION_EFFECT1(o, index, Luminosity);
} // OMF-01417
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_TARGET_POSITION_EFFECT2(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_TARGET_POSITION_EFFECT2(o, index, Luminosity);
} // OMF-01418
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_EFFECT_SAPITRES_ATTACK(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_EFFECT_SAPITRES_ATTACK(o, index, Luminosity);
} // OMF-01419
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::
    Move_MODEL_EFFECT_THUNDER_NAPIN_ATTACK_1(OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_EFFECT_THUNDER_NAPIN_ATTACK_1(o, index, Luminosity);
} // OMF-01420
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_EFFECT_SKURA_ITEM(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_EFFECT_SKURA_ITEM(o, index, Luminosity);
} // OMF-01421
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_BLOW_OF_DESTRUCTION(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_BLOW_OF_DESTRUCTION(o, index, Luminosity);
} // OMF-01422
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_NIGHTWATER_01(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_NIGHTWATER_01(o, index, Luminosity);
} // OMF-01423
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_KNIGHT_PLANCRACK_A(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_KNIGHT_PLANCRACK_A(o, index, Luminosity);
} // OMF-01424
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_KNIGHT_PLANCRACK_B(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_KNIGHT_PLANCRACK_B(o, index, Luminosity);
} // OMF-01425
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_EFFECT_FLAME_STRIKE(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_EFFECT_FLAME_STRIKE(o, index, Luminosity);
} // OMF-01426
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_1_STREAMBREATHFIRE(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_1_STREAMBREATHFIRE(o, index, Luminosity);
} // OMF-01427
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::
    Move_MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_HEAD(OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_HEAD(o, index, Luminosity);
} // OMF-01428
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::
    Move_MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_BODY(OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_BODY(o, index, Luminosity);
} // OMF-01429
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_LAVAGIANT_FOOTPRINT_R(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_LAVAGIANT_FOOTPRINT_R(o, index, Luminosity);
} // OMF-01430
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_PROJECTILE(OBJECT *o,
                                                                                   int index,
                                                                                   float Luminosity)
{
    return owner_.Move_MODEL_PROJECTILE(o, index, Luminosity);
} // OMF-01431
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01(o, index, Luminosity);
} // OMF-01432
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_STATUE_CRUSH_EFFECT_PIECE04(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_STATUE_CRUSH_EFFECT_PIECE04(o, index, Luminosity);
} // OMF-01433
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::
    Move_MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_(OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_(o, index, Luminosity);
} // OMF-01434
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::
    Move_MODEL_EMPIREGUARDIAN_BLOW_OF_DESTRUCTION(OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_EMPIREGUARDIAN_BLOW_OF_DESTRUCTION(o, index, Luminosity);
} // OMF-01435
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_EFFECT_SD_AURA(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_EFFECT_SD_AURA(o, index, Luminosity);
} // OMF-01436
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_WATERFALL_4(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_WATERFALL_4(o, index, Luminosity);
} // OMF-01437
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_WOLF_HEAD_EFFECT(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_WOLF_HEAD_EFFECT(o, index, Luminosity);
} // OMF-01438
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_SBUMB(OBJECT *o, int index,
                                                                               float Luminosity)
{
    return owner_.Move_BITMAP_SBUMB(o, index, Luminosity);
} // OMF-01439
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_DOWN_ATTACK_DUMMY_L(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_DOWN_ATTACK_DUMMY_L(o, index, Luminosity);
} // OMF-01440
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_DOWN_ATTACK_DUMMY_R(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_DOWN_ATTACK_DUMMY_R(o, index, Luminosity);
} // OMF-01441
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_SWORDEFF(OBJECT *o,
                                                                                  int index,
                                                                                  float Luminosity)
{
    return owner_.Move_BITMAP_SWORDEFF(o, index, Luminosity);
} // OMF-01442
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SHOCKWAVE01(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SHOCKWAVE01(o, index, Luminosity);
} // OMF-01443
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SHOCKWAVE02(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SHOCKWAVE02(o, index, Luminosity);
} // OMF-01444
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_DAMAGE1(OBJECT *o,
                                                                                 int index,
                                                                                 float Luminosity)
{
    return owner_.Move_BITMAP_DAMAGE1(o, index, Luminosity);
} // OMF-01445
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SHOCKWAVE_SPIN01(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SHOCKWAVE_SPIN01(o, index, Luminosity);
} // OMF-01446
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_EVENT_CLOUD(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_EVENT_CLOUD(o, index, Luminosity);
} // OMF-01447
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_WINDFOCE(OBJECT *o,
                                                                                 int index,
                                                                                 float Luminosity)
{
    return owner_.Move_MODEL_WINDFOCE(o, index, Luminosity);
} // OMF-01448
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_LIGHT_RED(OBJECT *o,
                                                                                   int index,
                                                                                   float Luminosity)
{
    return owner_.Move_BITMAP_LIGHT_RED(o, index, Luminosity);
} // OMF-01449
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_WINDFOCE_MIRROR(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_WINDFOCE_MIRROR(o, index, Luminosity);
} // OMF-01450
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_SWORD_EFFECT_MONO(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_BITMAP_SWORD_EFFECT_MONO(o, index, Luminosity);
} // OMF-01451
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_WOLF_HEAD_EFFECT2(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_WOLF_HEAD_EFFECT2(o, index, Luminosity);
} // OMF-01452
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_SHOCKWAVE_GROUND01(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_SHOCKWAVE_GROUND01(o, index, Luminosity);
} // OMF-01453
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_DRAGON_KICK_DUMMY(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_DRAGON_KICK_DUMMY(o, index, Luminosity);
} // OMF-01454
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_BITMAP_LAVA(OBJECT *o, int index,
                                                                              float Luminosity)
{
    return owner_.Move_BITMAP_LAVA(o, index, Luminosity);
} // OMF-01455
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_DRAGON_LOWER_DUMMY(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_DRAGON_LOWER_DUMMY(o, index, Luminosity);
} // OMF-01456
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_TARGETMON_EFFECT(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_TARGETMON_EFFECT(o, index, Luminosity);
} // OMF-01457
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_VOLCANO_OF_MONK(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_VOLCANO_OF_MONK(o, index, Luminosity);
} // OMF-01458
bool GameLogic::Effects::Behaviors::MoveBehaviorLegacyCalls::Move_MODEL_VOLCANO_STONE(
    OBJECT *o, int index, float Luminosity)
{
    return owner_.Move_MODEL_VOLCANO_STONE(o, index, Luminosity);
} // OMF-01459

namespace GameLogic::Effects
{
void ApplyCreateParams(OBJECT *o, const CreateParams &params)
{
    if (params.lifeTime)
        o->LifeTime = *params.lifeTime;
    if (params.scale)
        o->Scale = *params.scale;
    if (params.velocity)
        o->Velocity = *params.velocity;
    if (params.gravity)
        o->Gravity = *params.gravity;
    if (params.hiddenMesh)
        o->HiddenMesh = *params.hiddenMesh;
    if (params.blendMesh)
        o->BlendMesh = *params.blendMesh;
    if (params.blendMeshLight)
        o->BlendMeshLight = *params.blendMeshLight;
    if (params.alpha)
        o->Alpha = *params.alpha;
    if (params.light)
        VectorCopy(params.light->data(), o->Light);
    if (params.copyLightToDirection)
        VectorCopy(o->Light, o->Direction);
}

namespace
{
struct Entry
{
    int type;
    EffectDescriptor descriptor;
};

// The migrated effects. Each row states only what differs from the
// common initialisation CreateEffect applies to every effect. Effects
// absent from this list keep being handled by the legacy switch
// statements in ZzzEffect.cpp.
const std::vector<Entry> &Entries()
{
    static const std::vector<Entry> entries = [] {
        std::vector<Entry> e;
        auto add = [&e](std::initializer_list<int> types, const EffectDescriptor &d) {
            for (int type : types)
                e.push_back({type, d});
        };

        // MODEL_DESAIR: short-lived, slightly oversized; rides a joint
        // and sheds feathers (see Behaviors::MoveDesair). Default render.
        add({MODEL_DESAIR}, {.create = CreateParams{.lifeTime = 52.f, .scale = 1.4f},
                             .move = &Behaviors::MoveBehavior::MoveDesair});

        // --- Effects with creation parameters and a move handler ----
        add({MODEL_MAGIC_CAPSULE2},
            {.create = CreateParams{.lifeTime = 20.f, .blendMesh = 0, .blendMeshLight = 1.0f},
             .move = &Behaviors::MoveBehavior::MoveMagicCapsule2});
        add({MODEL_SPEAR}, {.create = CreateParams{.lifeTime = 10.f},
                            .move = &Behaviors::MoveBehavior::MoveSpear});
        add({MODEL_SUMMONER_SUMMON_NEIL_NIFE1, MODEL_SUMMONER_SUMMON_NEIL_NIFE2,
             MODEL_SUMMONER_SUMMON_NEIL_NIFE3},
            {.create = CreateParams{.lifeTime = 50.f, .scale = 1.0f, .alpha = 1.0f},
             .move = &Behaviors::MoveBehavior::MoveSummonerNeilNife});
        add({MODEL_SUMMONER_SUMMON_NEIL_GROUND1, MODEL_SUMMONER_SUMMON_NEIL_GROUND2,
             MODEL_SUMMONER_SUMMON_NEIL_GROUND3},
            {.create = CreateParams{.lifeTime = 50.f, .scale = 1.0f, .alpha = 0.0f},
             .move = &Behaviors::MoveBehavior::MoveSummonerNeilGround});
        add({BITMAP_FIRE_RED}, {.create = CreateParams{.lifeTime = 40.f},
                                .move = &Behaviors::MoveBehavior::MoveBitmapFireRed});
        add({BITMAP_LIGHT_MARKS}, {.create = CreateParams{.lifeTime = 65.f},
                                   .move = &Behaviors::MoveBehavior::MoveBitmapLightMarks});
        add({MODEL_MAGIC1}, {.create = CreateParams{.lifeTime = 20.f, .blendMesh = 0},
                             .move = &Behaviors::MoveBehavior::MoveMagic1});
        add({MODEL_MAYASTAR}, {.create = CreateParams{.lifeTime = 50.f, .scale = 50.0f},
                               .move = &Behaviors::MoveBehavior::MoveMayaStar});
        add({BITMAP_FIRE}, {.create = CreateParams{.lifeTime = 1000.f},
                            .move = &Behaviors::MoveBehavior::MoveBitmapFire});
        add({MODEL_INFINITY_ARROW4},
            {.create = CreateParams{.lifeTime = 15.f,
                                    .scale = 1.f,
                                    .light = std::array<float, 3>{1.f, 0.5f, 0.3f},
                                    .copyLightToDirection = true},
             .move = &Behaviors::MoveBehavior::MoveInfinityArrow4});

        // --- Data-only effects (move/render still in the legacy switch)
        add({BITMAP_IMPACT},
            {.create = CreateParams{.lifeTime = 80.f, .scale = 0.f, .blendMesh = -2}});
        add({MODEL_PROTECT},
            {.create = CreateParams{.lifeTime = 10000.f, .velocity = 0.3f, .blendMesh = 0}});
        add({MODEL_CURSEDTEMPLE_HOLYITEM, MODEL_CURSEDTEMPLE_PRODECTION_SKILL,
             MODEL_CURSEDTEMPLE_RESTRAINT_SKILL},
            {.create = CreateParams{.lifeTime = 9999999.f}});
        add({MODEL_SKILL_FISSURE}, {.create = CreateParams{.lifeTime = 20.f}});
        add({MODEL_FISSURE, MODEL_FISSURE_LIGHT},
            {.create = CreateParams{.lifeTime = 120.f, .scale = 0.8f}});
        add({MODEL_BALGAS_SKILL},
            {.create = CreateParams{.lifeTime = 20.f, .scale = 1.0f, .blendMesh = 0}});
        add({MODEL_BLOOD}, {.create = CreateParams{.lifeTime = 10.f, .blendMesh = 0}});
        add({MODEL_POISON},
            {.create = CreateParams{.lifeTime = 40.f, .scale = 1.0f, .blendMesh = 1}});
        add({BITMAP_SWORDEFF}, {.create = CreateParams{.lifeTime = 200.f}});
        add({BATTLE_CASTLE_WALL1, BATTLE_CASTLE_WALL2, BATTLE_CASTLE_WALL3, BATTLE_CASTLE_WALL4},
            {.create = CreateParams{.lifeTime = 2.f}});
        add({MODEL_CUNDUN_GHOST},
            {.create = CreateParams{.lifeTime = 200.f,
                                    .scale = 1.80f,
                                    .velocity = 0.08f,
                                    .blendMesh = -2,
                                    .light = std::array<float, 3>{0.5f, 0.5f, 0.5f}}});

        // --- Randomised / directional creation via an onCreate hook ---
        add({MODEL_MAYASTONE4, MODEL_MAYASTONE5},
            {.onCreate = &Behaviors::MoveBehavior::CreateMayaStone45});

        // --- Move handlers mechanically extracted from MoveEffect -----
        // (see Behaviors/MoveHandlers.cpp). Merge into an existing entry
        // when the type already has create params; otherwise add a
        // move-only entry. Creation and rendering for these types still
        // run through the legacy switches unless listed above.
        for (const auto &[type, move] : Behaviors::ExtractedMoveHandlers())
        {
            auto it = std::find_if(e.begin(), e.end(),
                                   [type = type](const Entry &en) { return en.type == type; });
            if (it != e.end())
                it->descriptor.move = move;
            else
                e.push_back({type, EffectDescriptor{.move = move}});
        }

        return e;
    }();
    return entries;
}

// Type-indexed lookup table built once from Entries(). Pointers are
// stable because Entries() holds a single static vector that is never
// mutated after construction.
const std::vector<const EffectDescriptor *> &Table()
{
    static const std::vector<const EffectDescriptor *> table = [] {
        const auto &entries = Entries();
        int maxType = -1;
        for (const auto &entry : entries)
            maxType = (entry.type > maxType) ? entry.type : maxType;

        std::vector<const EffectDescriptor *> t(maxType + 1, nullptr);
        for (const auto &entry : entries)
            t[entry.type] = &entry.descriptor;
        return t;
    }();
    return table;
}
} // namespace

const EffectDescriptor *Lookup(int type)
{
    const auto &table = Table();
    if (type < 0 || type >= (int)table.size())
        return nullptr;
    return table[type];
}
} // namespace GameLogic::Effects

namespace GameLogic::Effects::Motion
{
Core::Time::BounceResult AdvanceJump(float position[3], float &gravity, float direction[3],
                                     const float angle[3], float groundHeight, float frames)
{
    const auto bounce =
        Core::Time::AdvanceBouncing(position[2], gravity, groundHeight, 1.f, 0.4f, frames);
    float matrix[3][4];
    vec3_t local{direction[0] * frames, direction[1] * bounce.dampedFrames, direction[2] * frames},
        movement;
    AngleMatrix(angle, matrix);
    VectorRotate(local, matrix, movement);
    VectorAdd(position, movement, position);
    direction[1] *= bounce.damping;
    return bounce;
}
} // namespace GameLogic::Effects::Motion

int SessionVisualUnit::CreateSprite(int Type, const vec3_t Position, float Scale,
                                    const vec3_t Light, const OBJECT *Owner, float Rotation,
                                    int SubType)
{
    if (!g_pOption->GetRenderAllEffects())
    {
        return false;
    }

    if (mapSpriteOutput_ != nullptr && !*mapSpriteOutput_)
        *mapSpriteOutput_ = std::make_unique<SessionSpriteStorage>();
    auto &output = mapSpriteOutput_ != nullptr ? **mapSpriteOutput_ : Sprites;
    return Render::Sprites::AppendPreparedSprite(output, Type, Position, Scale, Light, Owner,
                                                 Rotation, SubType);
}

CSummonSystem::CSummonSystem(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), gMapManager(keeper.MapManagerObject())
{
}

CSummonSystem::~CSummonSystem()
{
    m_EquipEffectRandom.clear();
}

void CSummonSystem::RetireCharacter(CHARACTER *character)
{
    RemoveEquipEffects(character);
    ForgetCharacterPhase(&character->Object);
}

void CSummonSystem::ForgetCharacterPhase(const OBJECT *character)
{
    m_EquipEffectRandom.erase(character);
}

void CSummonSystem::SetPlayerSummon(CHARACTER *pCharacter, OBJECT *pObject)
{
    switch (pCharacter->Helper.Type)
    {
    case MODEL_HORN_OF_UNIRIA:
        SetAction(pObject, PLAYER_SKILL_SUMMON_UNI);
        break;
    case MODEL_HORN_OF_DINORANT:
        SetAction(pObject, PLAYER_SKILL_SUMMON_DINO);
        break;
    case MODEL_HORN_OF_FENRIR:
        SetAction(pObject, PLAYER_SKILL_SUMMON_FENRIR);
        break;
    default:
        SetAction(pObject, PLAYER_SKILL_SUMMON);
        break;
    }
}

void CSummonSystem::CastSummonSkill(int iSkill, CHARACTER *pCharacter, OBJECT *pObject,
                                    int iTargetPos_X, int iTargetPos_Y)
{
    pObject->SetAngleZ(CreateAngle(pObject->Position[0], pObject->Position[1],
                                   (float)iTargetPos_X * TERRAIN_SCALE,
                                   (float)iTargetPos_Y * TERRAIN_SCALE));
    SetPlayerSummon(pCharacter, pObject);
    CreateCastingEffect(pObject->Position, pObject->Angle, iSkill);
    RemoveEquipEffect_Summon(pCharacter);
    CreateSummonObject(iSkill, pCharacter, pObject, (float)iTargetPos_X * TERRAIN_SCALE,
                       (float)iTargetPos_Y * TERRAIN_SCALE);
}

float RequestTerrainHeight(float xf, float yf);
void SessionGameplayUnit::AdvanceAttackEffects(CHARACTER *c, OBJECT *o)
{
    const float elapsed = FPS_ANIMATION_FACTOR;
    float remaining = elapsed;
    c->WorldVisualAttackFrameTime = WorldTime;
    c->WorldVisualAttackStart = c->AttackTime;
    c->WorldVisualAttackFrames = 0.f;
    c->WorldVisualAttackAction = o->CurrentAction;
    // Attack events are authored on integer ticks. Visit every crossed tick,
    // while continuous effects receive only the elapsed fraction of that tick.
    while (remaining > 0.f)
    {
        const float untilNextTick = std::floor(c->AttackTime) + 1.f - c->AttackTime;
        const float frames = (std::min)(remaining, untilNextTick);
        {
            auto interval = EffectInterval(frames, elapsed - remaining);
            auto tickStart = EmissionTime(frames);
            AttackStage(c, o);
            if ((o->Type == MODEL_IRON_KNIGHT || o->Type == MODEL_DARK_IRON_KNIGHT) &&
                o->CurrentAction == MONSTER01_ATTACK2)
                g_iLimitAttackTime = 13; // Include the authored tick-12 impact and combo.
            AttackEffect(c);
            c->AttackTime += frames;
        }
        remaining -= frames;
        c->WorldVisualAttackFrames += frames;
        if (c->AttackTime >= g_iLimitAttackTime)
            break;
    }
}

void SessionGameplayUnit::AttackEffect(CHARACTER *c)
{
    OBJECT *o = &c->Object;
    BMD *b = &Models[o->Type];
    int i;
    vec3_t Angle, Light;
    vec3_t p, Position;
    float Luminosity = (float)(WorldRandom() % 6 + 2) * 0.1f;
    Vector(0.f, 0.f, 0.f, p);
    Vector(1.f, 1.f, 1.f, Light);

    if (TheMapProcess().AttackEffectMonster(c, o, b))
        return;

    switch (c->MonsterIndex)
    {
    case MONSTER_CHAOS_CASTLE_1:
    case MONSTER_CHAOS_CASTLE_3:
    case MONSTER_CHAOS_CASTLE_5:
    case MONSTER_CHAOS_CASTLE_7:
    case MONSTER_CHAOS_CASTLE_9:
    case MONSTER_CHAOS_CASTLE_11:
    case MONSTER_CHAOS_CASTLE_13:
        break;

    case MONSTER_MAGIC_SKELETON_1:
    case MONSTER_MAGIC_SKELETON_2:
    case MONSTER_MAGIC_SKELETON_3:
    case MONSTER_MAGIC_SKELETON_4:
    case MONSTER_MAGIC_SKELETON_5:
    case MONSTER_MAGIC_SKELETON_6:
    case MONSTER_MAGIC_SKELETON_7:
        if ((c->Skill) == AT_SKILL_BOSS)
        {
            if (rand_fps_check(2))
            {
                if (c->CheckAttackTime(1))
                {
                    CreateEffect(MODEL_SKILL_INFERNO, o->Position, o->Angle, o->Light, 1);
                    c->SetLastAttackEffectTime();
                }
            }
            else
            {
                Vector(o->Position[0] + WorldRandom() % 1024 - 512,
                       o->Position[1] + WorldRandom() % 1024 - 512, o->Position[2], Position);
                CreateEffect(MODEL_FIRE, Position, o->Angle, o->Light);
                PlayBuffer(SOUND_METEORITE01);
            }
        }
        break;

    case MONSTER_MOLT:
        break;
    case MONSTER_ALQUAMOS:
        break;
    case MONSTER_QUEEN_RAINER:
        if (c->CheckAttackTime(5))
        {
            if (c->TargetCharacter != -1)
            {
                CHARACTER *tc = &CharactersClient[c->TargetCharacter];
                OBJECT *to = &tc->Object;

                for (int i = 0; i < 20; ++i)
                {
                    CreateEffect(BITMAP_BLIZZARD, to->Position, to->Angle, Light);
                }
            }

            c->SetLastAttackEffectTime();
        }
        break;
    case MONSTER_OMEGA_WING:
    case MONSTER_MEGA_CRUST:
    case MONSTER_ALPHA_CRUST:
        if (c->Object.CurrentAction == MONSTER01_ATTACK1 ||
            c->Object.CurrentAction == MONSTER01_ATTACK2)
        {
            if (c->CheckAttackTime(5))
            {
                CreateInferno(o->Position);
                CreateEffect(MODEL_SKILL_INFERNO, o->Position, o->Angle, o->Light);
                c->SetLastAttackEffectTime();
            }
        }
        break;
    case MONSTER_PHANTOM_KNIGHT:
        if ((c->Skill) == AT_SKILL_BOSS)
        {
            if (c->CheckAttackTime(14))
            {
                vec3_t Angle = {0.0f, 0.0f, 0.0f};
                int iCount = 36;

                for (int i = 0; i < iCount; ++i)
                {
                    //Angle[2] = ( float)i * 10.0f;
                    Angle[0] = (float)(WorldRandom() % 360);
                    Angle[1] = (float)(WorldRandom() % 360);
                    Angle[2] = (float)(WorldRandom() % 360);
                    vec3_t Position;
                    VectorCopy(o->Position, Position);
                    Position[2] += 100.f;
                    CreateJoint(BITMAP_JOINT_SPIRIT, Position, Position, Angle, 1, NULL, 60.f, 0,
                                0);
                }

                c->SetLastAttackEffectTime();
            }
        }
        break;
    case MONSTER_DRAKAN:
    case MONSTER_GREAT_DRAKAN:
        if (c->Object.CurrentAction == MONSTER01_ATTACK1)
        {
            if (c->CheckAttackTime(11))
            {
                CreateInferno(o->Position);
                CreateEffect(MODEL_SKILL_INFERNO, o->Position, o->Angle, o->Light);

                for (int k = 0; k < 5; ++k)
                {
                    Vector(1.f, 0.5f, 0.f, Light);
                    VectorCopy(o->Angle, Angle);
                    Angle[0] += 45.f;
                    VectorCopy(o->Position, Position);
                    Position[0] += (float)(WorldRandom() % 1001 - 500);
                    Position[1] += (float)(WorldRandom() % 1001 - 500);
                    Position[2] += 500.f;
                    VectorCopy(Position, o->StartPosition);
                    CreateEffect(MODEL_PIERCING + 1, Position, Angle, Light, 1, o);
                }

                c->SetLastAttackEffectTime();
            }
        }
        else
        {
            if (c->CheckAttackTime(13))
            {
                Vector(1.f, 0.5f, 0.f, Light);
                Vector(-50.f, 100.f, 0.f, p);
                VectorCopy(o->Angle, Angle);
                Angle[0] += 45.f;
                b->TransformPosition(o->BoneTransform[11], p, Position, true);
                VectorCopy(Position, o->StartPosition);
                CreateEffect(MODEL_PIERCING + 1, Position, Angle, Light, 1, o);
                PlayBuffer(SOUND_METEORITE01);
                c->SetLastAttackEffectTime();
            }
            else if (c->CheckAttackTime(9))
            {
                Vector(1.f, 0.5f, 0.f, Light);
                Vector(0.f, 0.f, 0.f, p);
                VectorCopy(o->Angle, Angle);
                Angle[0] += 45.f;
                b->TransformPosition(o->BoneTransform[11], p, Position, true);
                c->SetLastAttackEffectTime();
            }
        }

        break;
    case MONSTER_DARK_PHOENIX:
        if ((c->Skill) == AT_SKILL_BOSS)
        {
            if (c->CheckAttackTime(2) || c->CheckAttackTime(6))
            {
                vec3_t Angle = {0.0f, 0.0f, 0.0f};
                int iCount = 40;
                for (i = 0; i < iCount; ++i)
                {
                    //Angle[2] = ( float)i * 10.0f;
                    Angle[0] = (float)(WorldRandom() % 360);
                    Angle[1] = (float)(WorldRandom() % 360);
                    Angle[2] = (float)(WorldRandom() % 360);
                    vec3_t Position;
                    VectorCopy(o->Position, Position);
                    Position[2] += 100.f;
                    CreateJoint(BITMAP_JOINT_SPIRIT, Position, Position, Angle, 3, NULL, 50.f, 0,
                                0);
                }

                c->SetLastAttackEffectTime();
            }
        }
        break;
    case MONSTER_DEATH_BEAM_KNIGHT:
    case MONSTER_BEAM_KNIGHT:
        if (c->MonsterIndex == MONSTER_DEATH_BEAM_KNIGHT)
        {
            if (c->CheckAttackTime(1))
            {
                CreateInferno(o->Position);
                CreateEffect(MODEL_SKILL_INFERNO, o->Position, o->Angle, o->Light);
                c->SetLastAttackEffectTime();
            }
            if ((c->Skill) == AT_SKILL_BOSS)
            {
                if (c->MonsterIndex == MONSTER_DEATH_BEAM_KNIGHT && rand_fps_check(1))
                {
                    Vector(o->Position[0] + WorldRandom() % 800 - 400,
                           o->Position[1] + WorldRandom() % 800 - 400, o->Position[2], Position);
                    CreateEffect(MODEL_SKILL_BLAST, Position, o->Angle, o->Light);
                }

                if (c->CheckAttackTime(14))
                {
                    for (int i = 0; i < 18; i++)
                    {
                        VectorCopy(o->Angle, Angle);
                        Angle[2] += i * 20.f;
                        CreateEffect(MODEL_STAFF_OF_DESTRUCTION, o->Position, Angle, o->Light);
                    }
                    c->SetLastAttackEffectTime();
                }
            }
        }
        else
        {
            if (c->CheckAttackTime(1))
            {
                //CreateInferno(o->Position);
                CreateEffect(MODEL_SKILL_INFERNO, o->Position, o->Angle, o->Light);
                c->SetLastAttackEffectTime();
            }
        }
        break;
    case MONSTER_CURSED_KING:
        if ((c->Skill) == AT_SKILL_BOSS)
        {
            if (c->CheckAttackTime(1))
            {
                CreateEffect(MODEL_SKILL_INFERNO, o->Position, o->Angle, o->Light, 1);
                c->SetLastAttackEffectTime();
            }
        }
        break;
    case MONSTER_GOLDEN_SOLDIER:
    case MONSTER_IRON_WHEEL:
    case MONSTER_SOLDIER:
        if (c->CheckAttackTime(1))
        {
            Vector(60.f, -110.f, 0.f, p);
            b->TransformPosition(o->BoneTransform[c->Weapon[0].LinkBone], p, Position, true);
            CreateEffect(MODEL_ARROW_BOMB, o->Position, o->Angle, o->Light, 0, o);
            if (c->MonsterIndex == MONSTER_IRON_WHEEL)
            {
                vec3_t Angle;
                VectorCopy(o->Angle, Angle);
                Angle[2] += 20.f;
                CreateEffect(MODEL_ARROW_BOMB, o->Position, Angle, o->Light, 0, o);
                Angle[2] -= 40.f;
                CreateEffect(MODEL_ARROW_BOMB, o->Position, Angle, o->Light, 0, o);
            }

            c->SetLastAttackEffectTime();
        }
        break;
    case MONSTER_GOLDEN_TITAN:
    case MONSTER_TANTALLOS:
    case MONSTER_ZAIKAN:
        if (c->CheckAttackTime(1))
        {
            CreateInferno(o->Position);
            c->SetLastAttackEffectTime();
        }
        if (c->CheckAttackTime(14))
        {
            if (c->MonsterIndex == MONSTER_ZAIKAN)
            {
                if ((c->Skill) == AT_SKILL_BOSS)
                {
                    for (i = 0; i < 18; i++)
                    {
                        VectorCopy(o->Angle, Angle);
                        Angle[2] += i * 20.f;
                        CreateEffect(MODEL_STAFF_OF_DESTRUCTION, o->Position, Angle, o->Light);
                    }
                }
            }

            c->SetLastAttackEffectTime();
        }
        break;
    case MONSTER_HYDRA: {
        const int attackTime = (int)c->AttackTime;
        if (attackTime % 5 == 1 && c->CheckAttackTime(attackTime))
        {
            b->TransformPosition(o->BoneTransform[63], p, Position, true);
            CreateEffect(BITMAP_BOSS_LASER + 1, Position, o->Angle, o->Light);
            c->SetLastAttackEffectTime();
        }

        if ((c->Skill) == AT_SKILL_BOSS)
        {
            if (c->CheckAttackTime(1))
            {
                VectorCopy(o->Angle, Angle);
                Angle[2] += 20.f;
                VectorCopy(o->Position, p);
                p[2] += 50.f;
                Luminosity = (15 - c->AttackTime) * 0.1f;
                Vector(Luminosity * 0.3f, Luminosity * 0.6f, Luminosity * 1.f, Light);

                for (int i = 0; i < 9; i++)
                {
                    Angle[2] += 40.f;
                    CreateEffect(BITMAP_BOSS_LASER, p, Angle, Light);
                }

                c->SetLastAttackEffectTime();
            }
        }
        break;
    }
    case MONSTER_RED_DRAGON:
        if ((c->Skill) == AT_SKILL_BOSS)
        {
            if (c->CheckAttackTime(1))
            {
                Vector(0.f, 0.f, 0.f, p);
                b->TransformPosition(o->BoneTransform[11], p, Position, true);
                Vector(o->Angle[0] - 20.f, o->Angle[1], o->Angle[2] - 30.f, Angle);
                CreateEffect(MODEL_FIRE, Position, Angle, o->Light, 2);
                Vector(o->Angle[0] - 30.f, o->Angle[1], o->Angle[2], Angle);
                CreateEffect(MODEL_FIRE, Position, Angle, o->Light, 2);
                Vector(o->Angle[0] - 20.f, o->Angle[1], o->Angle[2] + 30.f, Angle);
                CreateEffect(MODEL_FIRE, Position, Angle, o->Light, 2);
                PlayBuffer(SOUND_METEORITE01);
                c->SetLastAttackEffectTime();
            }
            EmitBossMeteors(*o);
        }
        break;
    case MONSTER_DEATH_GORGON:
        if ((c->Skill) == AT_SKILL_BOSS)
        {
            if (c->CheckAttackTime(1))
            {
                for (int i = 0; i < 18; i++)
                {
                    Vector(0.f, 0.f, i * 20.f, Angle);
                    CreateEffect(MODEL_FIRE, o->Position, Angle, o->Light, 1, o);
                }
                PlayBuffer(SOUND_METEORITE01);
                c->SetLastAttackEffectTime();
            }
        }
        break;
    case MONSTER_BALROG:
    case MONSTER_METAL_BALROG:
        if ((c->Skill) == AT_SKILL_BOSS)
        {
            if (c->CheckAttackTime(1))
            {
                CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, o->Light);
                CreateEffect(MODEL_CIRCLE_LIGHT, o->Position, o->Angle, o->Light);
                PlayBuffer(SOUND_HELLFIRE);
                c->SetLastAttackEffectTime();
            }

            EmitBossMeteors(*o);
        }
        break;
    case MONSTER_METEORITE_TRAP: //함정
        if (c->Skill == AT_SKILL_BOSS)
            EmitBossMeteors(*o);
        break;
    case MONSTER_BAHAMUT: //물고기
        for (auto birth : Emissions(FPS_ANIMATION_FACTOR))
        {
            ObjectDrawInput emitter(o);
            o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, emitter.position);
            AnimationPoseSample pose(emitter, b->BoneHead, b->BodyHeight, false,
                                     b->PoseAssetIdentity());
            std::array<vec34_t, MAX_BONES> bones;
            emitter.bones =
                pose.EvaluateAtTime(*b, *o, WorldTime, birth.FrameFraction(), bones.data());
            for (int i = 0; i < 4; i++)
            {
                Vector((float)(WorldRandom() % 32 - 16), (float)(WorldRandom() % 32 - 16),
                       (float)(WorldRandom() % 32 - 16), p);
                b->TransformByObjectBone(Position, emitter, 2, p);
                CreateParticle(BITMAP_BUBBLE, Position, emitter.angle, Light);
                CreateParticle(BITMAP_BLOOD + 1, Position, emitter.angle, Light);
            }
        }
        break;
    default:
        break;
    }

    if (CharactersClient.IsValidIndex(c->TargetCharacter))
    {
        CHARACTER *tc = &CharactersClient[c->TargetCharacter];
        OBJECT *to = &tc->Object;
        const auto emitGroup = [&](const auto &emit) {
            for (auto birth : Emissions(FPS_ANIMATION_FACTOR))
            {
                ObjectDrawInput emitter(o);
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position,
                                      emitter.position);
                emitter.angle[2] =
                    o->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), emitter.angle[2]);
                AnimationPoseSample pose(emitter, b->BoneHead, b->BodyHeight, false,
                                         b->PoseAssetIdentity());
                std::array<vec34_t, MAX_BONES> bones;
                emitter.bones =
                    pose.EvaluateAtTime(*b, *o, WorldTime, birth.FrameFraction(), bones.data());
                vec3_t targetPosition;
                to->MotionTrace.Sample(WorldTime, birth.FrameFraction(), to->Position,
                                       targetPosition);
                emit(emitter, targetPosition, birth);
            }
        };

        if ((c->Skill) == AT_SKILL_ENERGYBALL)
        {
            switch (c->MonsterIndex)
            {
            case MONSTER_CHAOS_CASTLE_2: //  카오스캐슬 궁수.
            case MONSTER_CHAOS_CASTLE_4:
            case MONSTER_CHAOS_CASTLE_6:
            case MONSTER_CHAOS_CASTLE_8:
            case MONSTER_CHAOS_CASTLE_10:
            case MONSTER_CHAOS_CASTLE_12:
            case MONSTER_CHAOS_CASTLE_14:
                if (c->Weapon[0].Type == MODEL_GREAT_REIGN_CROSSBOW)
                {
                    if (c->CheckAttackTime(8))
                    {
                        CreateArrows(c, o, o, 0, 0, 0);
                        c->SetLastAttackEffectTime();
                    }
                }
                else if (c->Object.CurrentAction == MONSTER01_ATTACK1)
                {
                    if (c->CheckAttackTime(15))
                    {
                        CalcAddPosition(o, -20.f, -90.f, 100.f, Position);
                        CreateEffect(BITMAP_BOSS_LASER, Position, o->Angle, Light, 0, o);
                        c->SetLastAttackEffectTime();
                    }
                }
                else if (c->Object.CurrentAction == MONSTER01_ATTACK2)
                {
                    if (c->CheckAttackTime(8))
                    {
                        if (rand_fps_check(2))
                        {
                            CreateEffect(MODEL_SKILL_BLAST, to->Position, o->Angle, o->Light, 0, o);
                            CreateEffect(MODEL_SKILL_BLAST, to->Position, o->Angle, o->Light, 0, o);
                        }
                        else
                        {
                            Vector(0.8f, 0.5f, 0.1f, Light);
                            CreateEffect(MODEL_FIRE, to->Position, o->Angle, Light, 6);
                            CreateEffect(MODEL_FIRE, to->Position, o->Angle, Light, 6);
                        }
                        c->SetLastAttackEffectTime();
                    }
                }
                break;

            case MONSTER_MAGIC_SKELETON_1: //  마법 해골.
            case MONSTER_MAGIC_SKELETON_2:
            case MONSTER_MAGIC_SKELETON_3:
            case MONSTER_MAGIC_SKELETON_4:
            case MONSTER_MAGIC_SKELETON_5:
            case MONSTER_MAGIC_SKELETON_6:
            case MONSTER_MAGIC_SKELETON_7:
                if (c->CheckAttackTime(14))
                {
                    Vector(0.f, 0.f, 0.f, p);
                    b->TransformPosition(o->BoneTransform[33], p, Position, true);
                    VectorCopy(o->Angle, Angle);
                    CreateEffect(MODEL_PIERCING + 1, Position, Angle, Light, 1);
                    CreateJoint(BITMAP_JOINT_THUNDER, Position, Position, Angle, 2, to, 50.f);
                    c->SetLastAttackEffectTime();
                }
                break;

            case MONSTER_GIANT_OGRE_1: //. 자이언트오거1
            case MONSTER_GIANT_OGRE_2: //. 자이언트오거2
            case MONSTER_GIANT_OGRE_3: //. 자이언트오거3
            case MONSTER_GIANT_OGRE_4: //. 자이언트오거4
            case MONSTER_GIANT_OGRE_5: //. 자이언트오거5
            case MONSTER_GIANT_OGRE_6: //. 자이언트오거6
            case MONSTER_GIANT_OGRE_7:
                if (c->CheckAttackTime(13))
                {
                    Vector(1.0f, 1.0f, 1.0f, Light);
                    Vector(60.f, 30.f, 0.f, p);
                    b->TransformPosition(o->BoneTransform[6], p, Position, true);

                    Vector(o->Angle[0], o->Angle[1], o->Angle[2], Angle);
                    CreateEffect(MODEL_FIRE, Position, Angle, o->Light, 5);
                    c->SetLastAttackEffectTime();
                }
                break;

            case MONSTER_DARK_PHOENIX: //불사조공격
                if (c->CheckAttackTime(14))
                {
                    Vector(0.f, 0.f, 0.f, p);
                    DarkPhoenixAttackPosition(*c, false, Position);

                    VectorCopy(o->Angle, Angle);
                    CreateEffect(MODEL_PIERCING + 1, Position, Angle, Light, 1);
                    CreateJoint(BITMAP_JOINT_THUNDER, Position, Position, Angle, 2, to, 50.f);
                    c->SetLastAttackEffectTime();
                }
                break;
            case MONSTER_DRAKAN:
            case MONSTER_GREAT_DRAKAN:
                if (c->Object.CurrentAction == MONSTER01_ATTACK2)
                {
                    if (c->CheckAttackTime(13))
                    {
                        Vector(1.f, 0.5f, 0.f, Light);
                        Vector(-50.f, 100.f, 0.f, p);
                        VectorCopy(o->Angle, Angle);
                        Angle[0] += 45.f;
                        b->TransformPosition(o->BoneTransform[11], p, Position, true);
                        CreateEffect(MODEL_PIERCING + 1, Position, Angle, Light, 1);
                        CreateJoint(BITMAP_JOINT_THUNDER, Position, to->Position, Angle, 2, to,
                                    50.f);
                        c->SetLastAttackEffectTime();
                    }
                }
                break;
            case MONSTER_ALQUAMOS:
                if (c->CheckAttackTime(1))
                {
                    for (int i = 0; i < 4; ++i)
                    {
                        CreateJoint(BITMAP_FLARE, o->Position, o->Position, Angle, 7, to, 50.f);
                        // CreateJoint(BITMAP_FLARE, Position, Position, Angle, 7, to, 50.f);
                    }

                    c->SetLastAttackEffectTime();
                }
                break;
            case MONSTER_BEAM_KNIGHT:
                emitGroup([&](ObjectDrawInput &emitter, vec3_t targetPosition,
                              const EffectEmissionScope &birth) {
                    for (int i = 0; i < 6; i++)
                    {
                        int Hand = 0;
                        if (i >= 3)
                            Hand = 1;
                        b->TransformByObjectBone(Position, emitter, c->Weapon[Hand].LinkBone,
                                                 p); //에러
                        Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);
                        CreateJoint(BITMAP_JOINT_THUNDER, Position, targetPosition, Angle, 2, to,
                                    50.f);
                        CreateJoint(BITMAP_JOINT_THUNDER, Position, targetPosition, Angle, 2, to,
                                    10.f);
                    }

                    for (int i = 0; i < 4; i++)
                    {
                        int Hand = 0;
                        if (i >= 2)
                            Hand = 1;
                        b->TransformByObjectBone(Position, emitter, c->Weapon[Hand].LinkBone, p);
                        Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);
                        CreateJoint(BITMAP_JOINT_LASER + 1, Position, targetPosition, Angle, 0, to,
                                    50.f);
                        CreateParticle(BITMAP_FIRE, Position, emitter.angle, o->Light);
                    }
                });

                if (c->CheckAttackTime(1))
                {
                    PlayBuffer(SOUND_EVIL);
                    c->SetLastAttackEffectTime();
                }

                break;
            case MONSTER_VEPAR:
                if (c->CheckAttackTime(1))
                {
                    PlayBuffer(SOUND_EVIL);
                    c->SetLastAttackEffectTime();
                }

                emitGroup([&](ObjectDrawInput &emitter, vec3_t targetPosition,
                              const EffectEmissionScope &birth) {
                    for (int i = 0; i < 4; i++)
                    {
                        int Hand = 0;
                        if (i >= 2)
                            Hand = 1;
                        Vector(0.f, 0.f, 0.f, Angle);
                        b->TransformByObjectBone(Position, emitter, c->Weapon[Hand].LinkBone, p);
                        CreateJoint(BITMAP_BLUR + 1, Position, targetPosition, Angle, 1, to, 50.f);
                        CreateJoint(BITMAP_BLUR + 1, Position, targetPosition, Angle, 1, to, 10.f);
                    }
                });
                break;
            case MONSTER_DEVIL:
                if (c->CheckAttackTime(1))
                {
                    PlayBuffer(SOUND_EVIL);
                    c->SetLastAttackEffectTime();
                }

                emitGroup([&](ObjectDrawInput &emitter, vec3_t targetPosition,
                              const EffectEmissionScope &birth) {
                    for (int i = 0; i < 4; i++)
                    {
                        int Hand = 0;
                        if (i >= 2)
                            Hand = 1;
                        b->TransformByObjectBone(Position, emitter, c->Weapon[Hand].LinkBone, p);
                        Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);
                        CreateJoint(BITMAP_JOINT_LASER + 1, Position, targetPosition, Angle, 0, to,
                                    50.f);
                        CreateParticle(BITMAP_FIRE, Position, emitter.angle, o->Light);
                    }
                });
                break;
            case MONSTER_CURSED_KING: {
                if (c->CheckAttackTime(1))
                {
                    PlayBuffer(SOUND_THUNDER01);
                    c->SetLastAttackEffectTime();
                }

                emitGroup([&](ObjectDrawInput &emitter, vec3_t targetPosition,
                              const EffectEmissionScope &birth) {
                    float fAngle =
                        (float)((int)(45.f -
                                      ((c->AttackTime + FPS_ANIMATION_FACTOR -
                                        birth.RemainingFrames()) *
                                           3 +
                                       (int)(WorldTime - birth.SceneRemainingFrames() * 1000.0 /
                                                             sessionKeeper_.ApplicationConfig()
                                                                 .legacyReferenceFps) /
                                           10)) %
                                90) +
                        180.f;

                    for (int i = 0; i < 4; i++)
                    {
                        b->TransformByObjectBone(Position, emitter, c->Weapon[i % 2].LinkBone, p);
                        Vector(0.f, 0.f, fAngle, Angle);
                        CreateJoint(BITMAP_JOINT_LASER + 1, Position, targetPosition, Angle, 1, to,
                                    50.f);
                        CreateParticle(BITMAP_FIRE, Position, emitter.angle, o->Light);
                        fAngle += 270.f;
                    }
                });
            }
            break;

            default:
                break;
            }
        }
        else if ((c->Skill) == AT_SKILL_LIGHTNING || c->Skill == AT_SKILL_LIGHTNING_STR ||
                 c->Skill == AT_SKILL_LIGHTNING_STR_MG)
        {
            switch (c->MonsterIndex)
            {
            case MONSTER_MAGIC_SKELETON_1: //  마법 해골.
            case MONSTER_MAGIC_SKELETON_2:
            case MONSTER_MAGIC_SKELETON_3:
            case MONSTER_MAGIC_SKELETON_4:
            case MONSTER_MAGIC_SKELETON_5:
            case MONSTER_MAGIC_SKELETON_6:
            case MONSTER_MAGIC_SKELETON_7: {
                if (c->CheckAttackTime(1))
                {
                    PlayBuffer(SOUND_THUNDER01);
                    c->SetLastAttackEffectTime();
                }
                emitGroup([&](ObjectDrawInput &emitter, vec3_t targetPosition,
                              const EffectEmissionScope &birth) {
                    float fAngle =
                        (float)(45.f -
                                (int)((c->AttackTime + FPS_ANIMATION_FACTOR -
                                       birth.RemainingFrames()) *
                                          3 +
                                      (int)(WorldTime - birth.SceneRemainingFrames() * 1000.0 /
                                                            sessionKeeper_.ApplicationConfig()
                                                                .legacyReferenceFps) /
                                          10) %
                                    90) +
                        180.f;
                    for (int i = 0; i < 4; i++)
                    {
                        b->TransformByObjectBone(Position, emitter, c->Weapon[i % 2].LinkBone, p);
                        Vector(0.f, 0.f, fAngle, Angle);
                        CreateJoint(BITMAP_JOINT_LASER + 1, Position, targetPosition, Angle, 1, to,
                                    50.f);
                        CreateParticle(BITMAP_FIRE, Position, emitter.angle, o->Light);
                        fAngle += 270.f;
                    }
                });
            }
            break;

            case MONSTER_DARK_PHOENIX: //불사조공격
                if (8 <= c->AttackTime)
                    for (auto birth : Emissions(FPS_ANIMATION_FACTOR))
                    {
                        Vector(0.f, 0.f, 0.f, p);
                        DarkPhoenixAttackPosition(*c, true, Position, birth.FrameFraction());
                        vec3_t targetPosition;
                        to->MotionTrace.Sample(WorldTime, birth.FrameFraction(), to->Position,
                                               targetPosition);
                        for (int i = 0; i < 4; ++i)
                        {
                            Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);
                            CreateJoint(BITMAP_JOINT_THUNDER, Position, targetPosition, Angle, 0,
                                        to, 80.f);
                        }
                    }
                break;
            case MONSTER_DEVIL: //데빌
                if (c->CheckAttackTime(1))
                {
                    PlayBuffer(SOUND_EVIL);
                    c->SetLastAttackEffectTime();
                }

                emitGroup([&](ObjectDrawInput &emitter, vec3_t targetPosition,
                              const EffectEmissionScope &birth) {
                    for (int i = 0; i < 4; i++)
                    {
                        int Hand = 0;
                        if (i >= 2)
                            Hand = 1;
                        b->TransformByObjectBone(Position, emitter, c->Weapon[Hand].LinkBone, p);
                        Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);
                        CreateJoint(BITMAP_JOINT_LASER + 1, Position, targetPosition, Angle, 0, to,
                                    50.f);
                        CreateParticle(BITMAP_FIRE, Position, emitter.angle, o->Light);
                    }
                });
                break;
            case MONSTER_CURSED_WIZARD:
                emitGroup([&](ObjectDrawInput &emitter, vec3_t targetPosition,
                              const EffectEmissionScope &birth) {
                    for (int i = 0; i < 4; i++)
                    {
                        int Hand = 0;
                        if (i >= 2)
                            Hand = 1;
                        b->TransformByObjectBone(Position, emitter, c->Weapon[Hand].LinkBone, p);
                        Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);
                        CreateJoint(BITMAP_JOINT_THUNDER, Position, targetPosition, Angle, 0, to,
                                    50.f);
                        CreateJoint(BITMAP_JOINT_THUNDER, Position, targetPosition, Angle, 0, to,
                                    10.f);
                        CreateParticle(BITMAP_ENERGY, Position, emitter.angle, Light);
                    }
                });
                break;
            case MONSTER_LIZARD_KING: //리자드킹
                emitGroup([&](ObjectDrawInput &emitter, vec3_t targetPosition,
                              const EffectEmissionScope &birth) {
                    for (int i = 0; i < 6; i++)
                    {
                        int Hand = 0;
                        if (i >= 3)
                            Hand = 1;
                        b->TransformByObjectBone(Position, emitter, c->Weapon[Hand].LinkBone,
                                                 p); //에러
                        Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);
                        CreateJoint(BITMAP_JOINT_THUNDER, Position, targetPosition, Angle, 2, to,
                                    50.f);
                        CreateJoint(BITMAP_JOINT_THUNDER, Position, targetPosition, Angle, 2, to,
                                    10.f);
                    }
                });
                break;
            case MONSTER_POISON_SHADOW:
                emitGroup([&](ObjectDrawInput &emitter, vec3_t targetPosition,
                              const EffectEmissionScope &birth) {
                    if (o->Type == MODEL_PLAYER)
                    {
                        Vector(0.f, 0.f, 0.f, p);
                    }
                    else
                    {
                        Vector(0.f, -130.f, 0.f, p);
                    }
                    b->TransformByObjectBone(Position, emitter, c->Weapon[0].LinkBone, p);
                    Vector(-60.f, 0.f, emitter.angle[2], Angle);
                    CreateJoint(BITMAP_JOINT_THUNDER, Position, targetPosition, Angle, 0, to, 50.f);
                    CreateJoint(BITMAP_JOINT_THUNDER, Position, targetPosition, Angle, 0, to, 10.f);
                    CreateParticle(BITMAP_ENERGY, Position, emitter.angle, Light);
                });
                break;
            case MONSTER_ILLUSION_SORCERER_SPIRIT1_LIGHTNING:
            case MONSTER_ILLUSION_SORCERER_SPIRIT2_LIGHTNING:
            case MONSTER_ILLUSION_SORCERER_SPIRIT3_LIGHTNING:
            case MONSTER_ILLUSION_SORCERER_SPIRIT4_LIGHTNING:
            case MONSTER_ILLUSION_SORCERER_SPIRIT5_LIGHTNING:
            case MONSTER_ILLUSION_SORCERER_SPIRIT6_LIGHTNING: {
                emitGroup([&](ObjectDrawInput &emitter, vec3_t targetPosition,
                              const EffectEmissionScope &birth) {
                    Vector(8.f, 0.f, 0.f, Light);
                    b->TransformByObjectBone(Position, emitter, 17, p);
                    Vector(-60.f, 0.f, emitter.angle[2], Angle);
                    //CreateJoint(BITMAP_JOINT_THUNDER,Position,targetPosition,Angle,0,to,50.f);
                    CreateJoint(BITMAP_JOINT_THUNDER, Position, targetPosition, Angle, 21, to,
                                50.f);
                    CreateParticle(BITMAP_ENERGY, Position, emitter.angle, Light);

                    b->TransformByObjectBone(Position, emitter, 41, p);
                    Vector(-60.f, 0.f, emitter.angle[2], Angle);
                    //CreateJoint(BITMAP_JOINT_THUNDER,Position,targetPosition,Angle,0,to,50.f);
                    CreateJoint(BITMAP_JOINT_THUNDER, Position, targetPosition, Angle, 21, to,
                                50.f);
                    CreateParticle(BITMAP_ENERGY, Position, emitter.angle, Light);
                });
            }
            break;
            // 플레이어 이거나 기타 몬스터가 전기(번개)를 사용했을시
            default:
                emitGroup([&](ObjectDrawInput &emitter, vec3_t targetPosition,
                              const EffectEmissionScope &birth) {
                    if (b->NumBones < c->Weapon[0].LinkBone)
                        return;

                    if (o->Type == MODEL_PLAYER)
                    {
                        Vector(0.f, 0.f, 0.f, p);
                    }
                    else
                    {
                        Vector(0.f, -130.f, 0.f, p);
                    }
                    b->TransformByObjectBone(Position, emitter, c->Weapon[0].LinkBone, p);
                    Vector(-60.f, 0.f, emitter.angle[2], Angle);
                    CreateJoint(BITMAP_JOINT_THUNDER, Position, targetPosition, Angle, 0, to, 50.f);
                    CreateJoint(BITMAP_JOINT_THUNDER, Position, targetPosition, Angle, 0, to, 10.f);
                    CreateParticle(BITMAP_ENERGY, Position, emitter.angle, Light);
                });
                break;
            }
        }
        else
        {
        }
    }
}

void SessionGameplayUnit::CreateWeaponBlur(CHARACTER *c, OBJECT *o, BMD *b)
{
    if (o->AnimationFrame >= 3.f || (o->CurrentAction == PLAYER_ATTACK_TWO_HAND_SWORD_TWO))
    {
        int Hand = 0;
        int Hand2 = 0;
        if (o->Type == MODEL_PLAYER)
        {
            if (o->CurrentAction == PLAYER_ATTACK_SWORD_LEFT1 ||
                o->CurrentAction == PLAYER_ATTACK_SWORD_LEFT2)
            {
                Hand = 1;
                Hand2 = 1;
            }
        }
        int BlurType = 0;
        int BlurMapping = 0;
        int Type = c->Weapon[Hand].Type;
        int Level = c->Weapon[Hand].Level;
        if (o->Type == MODEL_PLAYER)
        {
            if (o->CurrentAction == PLAYER_ATTACK_ONE_FLASH ||
                o->CurrentAction == PLAYER_ATTACK_RUSH)
            {
                BlurType = 1;
                BlurMapping = 2;
            }
            else if (o->CurrentAction == PLAYER_ATTACK_SKILL_SWORD1 ||
                     o->CurrentAction == PLAYER_ATTACK_SKILL_SWORD2 ||
                     o->CurrentAction == PLAYER_ATTACK_SKILL_SWORD3 ||
                     o->CurrentAction == PLAYER_ATTACK_SKILL_SWORD4)
            {
                // SWORD1 is Falling Slash: it was missing here, so a sword-wielder's Falling Slash
                // got no trail while Lunge/Uppercut/Cyclone (SWORD2/3/4) and Slash (SWORD5, below) did.
                BlurType = 1;
                if (Type == MODEL_LIGHTING_SWORD || Type == MODEL_DARK_REIGN_BLADE ||
                    Type == MODEL_RUNE_BLADE)
                    BlurMapping = 1;
                else
                    BlurMapping = 2;
            }
            else if (o->CurrentAction == PLAYER_ATTACK_STRIKE)
            {
                BlurType = 1;
                BlurMapping = 2;
            }
            else if (o->CurrentAction == PLAYER_SKILL_LIGHTNING_ORB ||
                     o->CurrentAction == PLAYER_SKILL_LIGHTNING_ORB_UNI ||
                     o->CurrentAction == PLAYER_SKILL_LIGHTNING_ORB_DINO ||
                     o->CurrentAction == PLAYER_SKILL_LIGHTNING_ORB_FENRIR)
            {
                BlurType = 1;
                BlurMapping = 1;
            }
            else if (o->CurrentAction == PLAYER_SKILL_BLOW_OF_DESTRUCTION &&
                     o->AnimationFrame >= 2.f && o->AnimationFrame <= 8.f)
            {
                BlurType = 1;
                BlurMapping = 2;
            }
            else if (o->CurrentAction == PLAYER_ATTACK_SKILL_SWORD5)
            {
                BlurType = 1;
                if (Type == MODEL_CRYSTAL_SWORD)
                    BlurMapping = 1;
                else
                    BlurMapping = 2;
            }
            else if (Type >= MODEL_SWORD && Type < MODEL_SWORD + MAX_ITEM_INDEX)
            {
                if ((o->CurrentAction >= PLAYER_ATTACK_SWORD_RIGHT1 &&
                     o->CurrentAction <= PLAYER_ATTACK_TWO_HAND_SWORD3) ||
                    o->CurrentAction == PLAYER_ATTACK_TWO_HAND_SWORD_TWO)
                {
                    BlurType = 1;
                    if (Type == MODEL_DARK_BREAKER)
                    {
                        BlurMapping = 6;
                    }
                    else if (o->CurrentAction == PLAYER_ATTACK_TWO_HAND_SWORD3 ||
                             o->CurrentAction == PLAYER_ATTACK_TWO_HAND_SWORD_TWO)
                    {
                        if (Type == MODEL_SWORD_DANCER)
                            BlurMapping = 2;
                        else
                            BlurMapping = 1;
                    }
                }
            }
            else if (Type == MODEL_TOMAHAWK ||
                     Type >= MODEL_BATTLE_AXE && Type < MODEL_MACE + MAX_ITEM_INDEX)
            {
                if (o->CurrentAction >= PLAYER_ATTACK_SKILL_SWORD1 &&
                    o->CurrentAction <= PLAYER_ATTACK_SKILL_SWORD5)
                {
                    BlurType = 1;
                    BlurMapping = 2;
                }
            }
            else if (Type >= MODEL_SPEAR && Type < MODEL_SPEAR + MAX_ITEM_INDEX)
            {
                if (o->CurrentAction >= PLAYER_ATTACK_SPEAR1 &&
                    o->CurrentAction <= PLAYER_ATTACK_SCYTHE3)
                {
                    BlurType = 3;
                    if (Type == MODEL_DRAGON_SPEAR)
                    {
                        BlurType = 1;
                        BlurMapping = 0;
                    }
                    else if (o->CurrentAction == PLAYER_ATTACK_SCYTHE3)
                        BlurMapping = 1;
                }
            }
        }
        else
        {
            if (c->MonsterIndex == MONSTER_MEGA_CRUST || c->MonsterIndex == MONSTER_ALPHA_CRUST ||
                c->MonsterIndex == MONSTER_OMEGA_WING)
            {
                if (o->CurrentAction >= MONSTER01_ATTACK1 && o->CurrentAction <= MONSTER01_ATTACK2)
                {
                    BlurType = 1;
                    BlurMapping = 6;
                }
            }
            else if (o->Type == MODEL_AEGIS)
            {
                if (o->CurrentAction == MONSTER01_ATTACK1)
                {
                    BlurType = 5;
                    BlurMapping = 2;
                    Hand = 0;
                    Hand2 = 1;
                }
            }
            else if (o->Type == MODEL_DEATH_CENTURION)
            {
                if (o->CurrentAction >= MONSTER01_ATTACK1 && o->CurrentAction <= MONSTER01_ATTACK2)
                {
                    BlurType = 4;
                    BlurMapping = 0;
                    Level = 99;
                }
            }
            else if (o->Type == MODEL_SHRIKER)
            {
                if (o->CurrentAction >= MONSTER01_ATTACK1 && o->CurrentAction <= MONSTER01_ATTACK2)
                {
                    if (o->SubType == 9)
                    {
                        BlurType = 1;
                        BlurMapping = 2;
                    }
                    else
                    {
                        BlurType = 1;
                        BlurMapping = 0;
                        Level = 99;
                        Type = 0;
                    }
                }
            }
            else if (Type >= MODEL_SWORD && Type < MODEL_SWORD + MAX_ITEM_INDEX)
            {
                if (o->CurrentAction >= MONSTER01_ATTACK1 && o->CurrentAction <= MONSTER01_ATTACK2)
                    BlurType = 1;
            }
        }
        if (BlurType > 0)
        {
            vec3_t Light;
            vec3_t Pos1, Pos2;
            vec3_t p, p2;
            switch (BlurType)
            {
            case 1:
                Vector(0.f, -20.f, 0.f, Pos1);
                break;
            case 2:
                Vector(0.f, -80.f, 0.f, Pos1);
                break;
            case 3:
                Vector(0.f, -100.f, 0.f, Pos1);
                break;
            }
            Vector(0.f, -120.f, 0.f, Pos2);

            if (BlurType == 4)
            {
                Vector(0.f, 0.f, 0.f, Pos1);
                Vector(0.f, -200.f, 0.f, Pos2);
            }
            else if (BlurType == 5)
            {
                Vector(0.f, 0.f, 0.f, Pos1);
                Vector(0.f, -20.f, 0.f, Pos2);
            }

            if (Type == MODEL_DOUBLE_BLADE || Type == MODEL_CHAOS_DRAGON_AXE ||
                Type == MODEL_BILL_OF_BALROG)
            {
                Vector(1.f, 0.2f, 0.2f, Light);
            }
            else if (Level == 99)
            {
                Vector(0.3f, 0.2f, 1.f, Light);
            }
            else if (BlurMapping == 0)
            {
                if (Level >= 7)
                {
                    Vector(1.f, 0.6f, 0.2f, Light);
                }
                else if (Level >= 5)
                {
                    Vector(0.2f, 0.4f, 1.f, Light);
                }
                else if (Level >= 3)
                {
                    Vector(1.f, 0.2f, 0.2f, Light);
                }
                else
                {
                    Vector(0.8f, 0.8f, 0.8f, Light);
                }
            }
            else
            {
                Vector(1.f, 1.f, 1.f, Light);
            }

            if ((o->Type != MODEL_PLAYER || Type == MODEL_KATACHE || Type == MODEL_GLADIUS ||
                 Type == MODEL_SWORD_OF_SALAMANDER || Type == MODEL_LEGENDARY_SWORD ||
                 Type == MODEL_SERPENT_SPEAR) &&
                o->Type != MODEL_AEGIS && o->Type != MODEL_DEATH_CENTURION &&
                o->Type != MODEL_SHRIKER)
            {
                b->TransformPosition(o->BoneTransform[c->Weapon[Hand].LinkBone], Pos1, p, true);
                b->TransformPosition(o->BoneTransform[c->Weapon[Hand2].LinkBone], Pos2, p2, true);
                CreateBlur(c, p, p2, Light, BlurMapping);
            }
            else if (g_CMonkSystem.IsSwordformGloves(Type))
            {
                g_CMonkSystem.MoveBlurEffect(c, o, b, FPS_ANIMATION_FACTOR);
            }
            else
            {
                constexpr float inter = 10.f;
                // The trail spans backward over one animation slice and lays its points along it.
                // Scaling that slice by FPS_ANIMATION_FACTOR (REFERENCE_FPS / FPS) collapses it at
                // high frame rates - the points pile onto a single weapon position and the streak
                // disappears. Span a fixed PlaySpeed slice so the trail renders the same at any FPS.
                const float playSpeed = b->Actions[b->CurrentAction].PlaySpeed;
                float animationFrame = o->AnimationFrame - playSpeed;
                const float priorAnimationFrame = o->PriorAnimationFrame;
                const float animationSpeed = playSpeed / inter;
                for (int i = 0; i < (int)(inter); ++i)
                {
                    b->AnimationAtFrame(BoneTransform, animationFrame, priorAnimationFrame,
                                        o->PriorAction, o->Angle, o->HeadAngle);

                    b->TransformPosition(BoneTransform[c->Weapon[Hand].LinkBone], Pos1, p, false);
                    b->TransformPosition(BoneTransform[c->Weapon[Hand2].LinkBone], Pos2, p2, false);

                    if (o->Type == MODEL_AEGIS && i % 2)
                    {
                        CreateParticle(BITMAP_FIRE + 3, p2, o->Angle, Light, 12);
                    }

                    if (c->Weapon[0].Type != -1 || c->Weapon[1].Type != -1)
                        CreateBlur(c, p, p2, Light, BlurMapping, true);

                    animationFrame += animationSpeed;
                }
            }
        }
        TheMapProcess().MoveBlurEffect(c, o, b);
    }
    else
    {
        VectorCopy(o->Position, o->StartPosition);
    }
}

void SessionVisualUnit::AdvanceObjectVisual(OBJECT *o)
{
    BMD *b = &Models[o->Type];
    const float luminosity = static_cast<float>(WorldRandom() % 30 + 70) * 0.01f;
    TheMapProcess().AdvanceObjectVisual(o, b, luminosity);
}

void SessionVisualUnit::MoveObject(OBJECT *o)
{
    o->MotionTrace.Begin(WorldTime, FPS_ANIMATION_FACTOR, o->Position);
    TheMapProcess().PrepareObjectUpdate(o);
    o->AnimationCycleEnded = false;
    Alpha(o, FPS_ANIMATION_FACTOR);
    if (o->Alpha < 0.01f)
        return;
    BMD *b = &Models[o->Type];
    b->CurrentAction = o->CurrentAction;

    const float fSpeed = TheMapProcess().ObjectAnimationSpeed(o, *b, o->Velocity);
    const ObjectMotionTrace::AnimationPhase phase{o->AnimationFrame, o->PriorAnimationFrame,
                                                  o->CurrentAction, o->PriorAction};
    o->AnimationCycleEnded = !b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame,
                                               &o->PriorAction, fSpeed, o->Position, o->Angle);
    o->MotionTrace.AdvanceAnimation(FPS_ANIMATION_FACTOR, phase, fSpeed * FPS_ANIMATION_FACTOR);

    vec3_t p;
    vec3_t Light;
    float Luminosity;
    Vector(0.f, 0.f, 0.f, p);
    if (SceneFlag == LOG_IN_SCENE || SceneFlag == CHARACTER_SCENE)
    {
        switch (o->Type)
        {
        case MODEL_LOGO:
            o->BlendMeshTexCoordV = -((int)WorldTime % 4000 * 0.00025f);
            break;
        case MODEL_WAVEBYSHIP:
            o->BlendMeshTexCoordV = -((int)WorldTime % 4000 * 0.00025f);
            break;
        case MODEL_MUGAME:
            if (GetLoginCameraWalkCut() == 0)
                Luminosity = GetLoginCameraCount() * 0.02f;
            else
            {
                Luminosity = 1.5f;
            }
            Vector(Luminosity * 1.f, Luminosity * 1.f, Luminosity * 1.f, o->Light);
            o->BlendMeshLight = Luminosity;
            break;
        }
    }

    if (TheMapProcess().MoveObject(o, g_timer2StartTickTime))
        return;
}

void SessionVisualUnit::PrepareWorldObjectPose(OBJECT &object, float fraction)
{
    BoneScale = 1.f;
    BMD &model = Models[object.Type];
    model.BodyHeight = 0.f;
    model.ContrastEnable = object.ContrastEnable;
    BodyLight(&object, &model);
    model.BodyScale = object.Scale;
    model.CurrentAction = object.CurrentAction;
    VectorCopy(object.Position, model.BodyOrigin);
    if (object.Type == MODEL_CASTLE_GATE)
        model.BodyOrigin[1] += 60.f;
    else if (object.Type == MODEL_STATUE_OF_SAINT)
        model.BodyOrigin[1] += 120.f;
    if (fraction == 1.f)
        model.Animation(BoneTransform, object.AnimationFrame, object.PriorAnimationFrame,
                        object.PriorAction, object.Angle, object.HeadAngle, false, true);
    else
    {
        ObjectDrawInput draw(&object);
        VectorCopy(model.BodyOrigin, draw.position);
        AnimationPoseSample pose(draw, model.BoneHead, model.BodyHeight, true,
                                 model.PoseAssetIdentity());
        pose.EvaluateAtTime(model, object, WorldTime, fraction, BoneTransform);
    }
}

void SessionVisualUnit::AdvanceHiddenObjectBlock(OBJECT_BLOCK &block)
{
    const bool hasAction = g_iActionWorld >= 0 && g_iActionObjectType >= 0 && g_iActionTime >= 0;
    for (OBJECT *object = block.Head; object; object = object->Next)
    {
        if (!object->Live)
            continue;
        object->Visible = false;
        if (hasAction)
            ActionObject(object);
    }
}

void SessionVisualUnit::MoveObjects()
{
    auto &maps = TheMapProcess();
    const bool cullBlocks = maps.Presentation().objectBlockCulling;
    int objectCount = 0;
    maps.PrepareObjectEffects(objectCount, visibleObject);
    visibleObject = 0;
    constexpr float EffectCycleFrames = 40.f;
    if (FPS_ANIMATION_FACTOR > 0.f)
        Time_Effect = std::fmod(Time_Effect + FPS_ANIMATION_FACTOR, EffectCycleFrames);
    for (int block = 0; block < 16 * 16; ++block)
    {
        auto &objects = ObjectBlock[block];
        const bool wasVisible = objects.Visible;
        objects.Visible = TestFrustrum2D(static_cast<float>((block / 16) * 16 + 8),
                                         static_cast<float>((block % 16) * 16 + 8), -180.f);
        if (cullBlocks && !objects.Visible)
        {
            if (wasVisible || g_iActionTime >= 0)
                AdvanceHiddenObjectBlock(objects);
            continue;
        }
        for (OBJECT *object = objects.Head; object != nullptr; object = object->Next)
        {
            if (!object->Live)
                continue;
            object->Visible = maps.ObjectVisible(*object, objects.Visible);
            maps.AdvanceObjectVisibility(*object);
            if (object->Visible)
            {
                MoveObject(object);
                maps.MoveObjectEffects(object, objectCount, visibleObject);
            }
            ActionObject(object);
            if (object->RigidPoseDirty)
                PrepareRigidObjectPose(*object);
            if (!object->Visible || !maps.ObjectEffectsVisible(*object))
                continue;
            maps.AdvanceObjectFade(*object);
            AdvanceObjectVisual(object);
        }
    }
    if (g_iActionTime >= 0.f && FPS_ANIMATION_FACTOR > 0.f)
    {
        if (g_iActionTime <= FPS_ANIMATION_FACTOR)
        {
            if (g_iActionWorld == gMapManager.ContextMap())
                maps.FinishObjectAction();
            ClearActionObject();
        }
        else
            g_iActionTime -= FPS_ANIMATION_FACTOR;
    }
}

void SessionVisualUnit::AdvanceTopGradeWeaponVisual(CHARACTER &character)
{
    auto *c = &character;
    if (gMapManager.InChaosCastle() || (gMapManager.IsCursedTemple() && !c->SafeZone))
        return;
    auto *b = &Models[c->Object.Type];
    int weaponIndex, weaponIndex2, Level;
    PART_t *w;
    vec3_t vRelativePos, vPos, vLight;
    float fLight2, fScale;
    for (int i = 0; i < 2; i++)
    {
        w = &c->Weapon[i];
        Level = w->Level;
        if (Level < 15 || w->Type == -1)
            continue;

        if (MODEL_BOW <= w->Type && w->Type < MODEL_STAFF)
        {
            weaponIndex = 27;
            weaponIndex2 = 28;
        }
        else
        {
            weaponIndex = (i == 0 ? 27 : 36);
            weaponIndex2 = (i == 0 ? 28 : 37);
        }

        switch (Level)
        {
        case 15: {
            Vector(0.0f, 0.0f, 0.0f, vRelativePos);
            b->TransformByObjectBone(vPos, &c->Object, weaponIndex, vRelativePos);
            Vector(1.0f, 0.6f, 0.0f, vLight);
            CreateSprite(BITMAP_LIGHT, vPos, 0.6f, vLight, &c->Object);

            Vector(10.0f, 0.0f, 0.0f, vRelativePos);
            b->TransformByObjectBone(vPos, &c->Object, weaponIndex, vRelativePos);
            Vector(1.0f, 0.6f, 0.0f, vLight);
            CreateSprite(BITMAP_LIGHT, vPos, 0.6f, vLight, &c->Object);

            Vector(20.0f, 0.0f, 0.0f, vRelativePos);
            b->TransformByObjectBone(vPos, &c->Object, weaponIndex, vRelativePos);
            Vector(1.0f, 0.6f, 0.0f, vLight);
            CreateSprite(BITMAP_LIGHT, vPos, 0.6f, vLight, &c->Object);

            Vector(10.0f, 0.0f, 0.0f, vRelativePos);
            b->TransformByObjectBone(vPos, &c->Object, weaponIndex2, vRelativePos);

            fLight2 = absf(sinf(WorldTime * 0.002f));
            Vector(0.7f * fLight2 + 0.3f, 0.1f * fLight2 + 0.1f, 0.0f, vLight);
            CreateSprite(BITMAP_MAGIC, vPos, 0.35f, vLight, &c->Object);

            Vector(1.0f, 1.0f, 1.0f, vLight);
            fScale = (float)(WorldRandom() % 60) * 0.01f;
            CreateSprite(BITMAP_FLARE_RED, vPos, 0.6f * fScale + 0.4f, vLight, &c->Object);

            Vector(1.0f, 0.2f, 0.0f, vLight);
            fScale = (float)(WorldRandom() % 80 + 10) * 0.01f * 1.0f;
            CreateParticleFpsChecked(BITMAP_LIGHTNING_MEGA1 + WorldRandom() % 3, vPos,
                                     c->Object.Angle, vLight, 0, fScale);
        }
        break;
        } //switch
    } //for
}

using namespace SEASON3B;

bool SessionLegacyCalls::CreateCursedTempleSkillEffect(CHARACTER *character, int skillIndex,
                                                       int subType)
{
    return sessionKeeper_.Visual()->CreateCursedTempleSkillEffect(character, skillIndex, subType);
}

bool AttackCharacterRange(int Index, vec3_t Position, float Range, BYTE Serial, short PKKey,
                          WORD SkillSerialNum)
{
    return false; // we don't send this packet anymore
}

void MoveCharacter(CHARACTER *c, OBJECT *o);

bool SessionVisualUnit::CreateCursedTempleSkillEffect(CHARACTER *c, int skillindex, int SubType)
{
    if (!c)
        return false;

    OBJECT *o = &c->Object;
    BMD *b = &Models[o->Type];

    vec3_t vRelativePos, vtaWorldPos, vLight, vAngle;

    switch (skillindex)
    {
    case AT_SKILL_CURSED_TEMPLE_PRODECTION: {
        DeleteEffect(MODEL_CURSEDTEMPLE_PRODECTION_SKILL, o);

        if (o->Live && !SearchEffect(MODEL_CURSEDTEMPLE_PRODECTION_SKILL, o))
        {
            Vector(0.3f, 0.3f, 0.8f, o->Light);
            CreateEffect(MODEL_CURSEDTEMPLE_PRODECTION_SKILL, o->Position, o->Angle, o->Light, 0,
                         o);
            CreateEffect(MODEL_SHIELD_CRASH, o->Position, o->Angle, o->Light, 1, o);
            CreateEffect(BITMAP_SHOCK_WAVE, o->Position, o->Angle, o->Light, 10, o);
        }
    }
        return true;
    case AT_SKILL_CURSED_TEMPLE_RESTRAINT: {
        DeleteEffect(MODEL_CURSEDTEMPLE_RESTRAINT_SKILL, o);

        if (o->Live && !SearchEffect(MODEL_CURSEDTEMPLE_RESTRAINT_SKILL, o))
        {
            Vector(0.4f, 1.f, 0.8f, vLight);
            CreateEffect(BITMAP_SHOCK_WAVE, o->Position, o->Angle, vLight, 5);
            CreateEffect(MODEL_SHIELD_CRASH, o->Position, o->Angle, vLight, 1, o);

            for (int i = 1; i < 40; i++)
            {
                vec3_t Position, Angle, Light;
                VectorCopy(o->Position, Position);
                Vector(0.f, 0.f, i * (90.f / 4.f), Angle);
                Vector(0.6f, 1.f, 0.8f, Light);

                Position[0] += cosf(Q_PI / 180.f * 10.0f * i) * (float)(rand() % 240 + 140);
                Position[1] += sinf(Q_PI / 180.f * 10.0f * i) * (float)(rand() % 240 + 140);
                Position[2] += rand() % 200 + 100;

                Vector(0.f, 0.f, 0.f, vRelativePos);
                VectorCopy(o->Position, b->BodyOrigin);

                b->TransformPosition(o->BoneTransform[rand() % 50], vRelativePos, vtaWorldPos,
                                     true);

                Vector(0.7f, 1.f, 0.4f, Light);

                if (rand() % 2 == 1)
                {
                    CreateJoint(BITMAP_JOINT_ENERGY, Position, vtaWorldPos, Angle, 44, o, 15.f, -1,
                                0, 0, -1, Light);
                }
            }
        }
    }
        return true;
    case AT_SKILL_CURSED_TEMPLE_SUBLIMATION: {
        if (SubType == 0)
        {
            Vector(300.f, 0.f, 0.f, vAngle);

            Vector(0.8f, 0.3f, 0.3f, vLight);
            CreateEffect(MODEL_SKILL_INFERNO, o->Position, o->Angle, vLight, 8, o, 10, 0);

            Vector(0.8f, 0.3f, 0.3f, vLight);
            CreateEffect(MODEL_SHIELD_CRASH, o->Position, o->Angle, vLight, 2, o);

            Vector(0.3f, 0.3f, 0.8f, vLight);
            CreateEffect(BITMAP_SHOCK_WAVE, o->Position, o->Angle, vLight, 5, o);

            Vector(150.f, 0.f, 0.f, vRelativePos);
            Vector(0.8f, 0.3f, 0.3f, vLight);
            VectorCopy(o->Position, b->BodyOrigin);

            b->TransformPosition(o->BoneTransform[20], vRelativePos, vtaWorldPos, true);
            CreateParticle(BITMAP_CURSEDTEMPLE_EFFECT_MASKER, vtaWorldPos, o->Angle, vLight, 0,
                           1.5f);

            for (int i = 1; i < 40; i++)
            {
                vec3_t Position, Angle, Light;
                VectorCopy(o->Position, Position);
                Vector(0.f, 0.f, 0.f, Angle);

                Position[0] += cosf(Q_PI / 180.f * 10.0f * i) * (float)(rand() % 50 + 20);
                Position[1] += sinf(Q_PI / 180.f * 10.0f * i) * (float)(rand() % 50 + 20);
                Position[2] -= 120.f;

                if (rand() % 3 == 1)
                {
                    if (rand() % 2 == 1)
                    {
                        Vector(0.3f, 0.3f, 0.8f, Light);
                    }
                    else
                    {
                        Vector(1.f, 0.5f, 0.5f, Light);
                    }
                    CreateParticle(BITMAP_EFFECT, Position, Angle, Light, 2);
                }
            }
        }
        else
        {
            Vector(150.f, 0.f, 0.f, vRelativePos);
            Vector(300.f, 0.f, 0.f, vAngle);

            Vector(0.8f, 0.3f, 0.3f, vLight);
            CreateEffect(MODEL_SKILL_INFERNO, o->Position, o->Angle, vLight, 8, o, 10, 0);

            Vector(0.8f, 0.3f, 0.3f, vLight);
            CreateEffect(MODEL_SHIELD_CRASH, o->Position, o->Angle, vLight, 2, o);

            Vector(0.3f, 0.3f, 0.8f, vLight);
            CreateEffect(BITMAP_SHOCK_WAVE, o->Position, o->Angle, vLight, 5, o);

            VectorCopy(o->Position, b->BodyOrigin);

            Vector(0.6f, 0.0f, 0.0f, vLight);
            b->TransformPosition(o->BoneTransform[31], vRelativePos, vtaWorldPos, true);
            CreateEffect(MODEL_FENRIR_THUNDER, vtaWorldPos, o->Angle, vLight, 2, o);
            CreateEffect(MODEL_FENRIR_THUNDER, vtaWorldPos, o->Angle, vLight, 2, o);
            CreateEffect(MODEL_FENRIR_THUNDER, vtaWorldPos, o->Angle, vLight, 2, o);
            CreateParticle(BITMAP_CLUD64, vtaWorldPos, o->Angle, vLight, 4, 1.f);

            b->TransformPosition(o->BoneTransform[40], vRelativePos, vtaWorldPos, true);
            CreateEffect(MODEL_FENRIR_THUNDER, vtaWorldPos, o->Angle, vLight, 2, o);
            CreateEffect(MODEL_FENRIR_THUNDER, vtaWorldPos, o->Angle, vLight, 2, o);
            CreateEffect(MODEL_FENRIR_THUNDER, vtaWorldPos, o->Angle, vLight, 2, o);
            CreateEffect(MODEL_FENRIR_THUNDER, vtaWorldPos, o->Angle, vLight, 2, o);
            CreateParticle(BITMAP_CLUD64, vtaWorldPos, o->Angle, vLight, 4, 1.f);
        }
    }
        return true;
    }
    return false;
}

void SessionVisualUnit::CreateSnowBursts(OBJECT &object, vec3_t light)
{
    constexpr std::array<std::array<float, 3>, 3> kSnowBurstOffsets{{
        {50.f, 50.f, 50.f},
        {-50.f, -50.f, 50.f},
        {50.f, -50.f, 50.f},
    }};

    for (const auto &offset : kSnowBurstOffsets)
    {
        vec3_t position;
        VectorCopy(object.Position, position);
        position[0] += offset[0];
        position[1] += offset[1];
        position[2] += offset[2];

        CreateParticle(BITMAP_EXPLOTION_MONO, position, object.Angle, light, 0, 0.6f);
        for (int i = 0; i < 2; ++i)
        {
            CreateEffect(MODEL_ICE_SMALL, position, object.Angle, light, 0);
            CreateParticle(BITMAP_CLUD64, position, object.Angle, light, 3, 1.0f);
            CreateParticle(BITMAP_CLUD64, position, object.Angle, light, 3, 1.0f);
            CreateEffect(MODEL_HALLOWEEN_CANDY_STAR, position, object.Angle, light, 1);
            CreateParticle(BITMAP_SNOW_EFFECT_1, position, object.Angle, light, 0, 0.5f);
            CreateParticle(BITMAP_SNOW_EFFECT_1, position, object.Angle, light, 0, 0.5f);
        }
    }
}

void SessionLegacyCalls::CreateSnowBursts(OBJECT &object, vec_t *light)
{
    sessionKeeper_.Visual()->CreateSnowBursts(object, light);
}
