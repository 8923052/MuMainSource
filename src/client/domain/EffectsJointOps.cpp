#include "domain/EffectsUpdate.h"
#include "support/CoreMath.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "render/Textures.h"
#include "render/ModelResources.h"
#include "data/Localization.h"
#include "domain/CharacterSystem.h"
#include "domain/ItemsSkills.h"
#include "domain/CharacterPresentation.h"
#include "domain/WorldSimulation.h"
#include "render/World.h"
#include "data/WorldData.h"
#include "render/Terrain.h"
#include "domain/MovementAI.h"
#include "app/ApplicationAudio.h"
#include "session/SessionNetwork.h"
#include "session/SessionRender.h"
#include "render/ModelGeometry.h"

void SessionGameplayUnit::EmitAttachedEnergyParticles(JOINT &joint)
{
    const bool blue = joint.SubType == 2 || joint.SubType == 3;
    const bool red = joint.SubType == 14 || joint.SubType == 15;
    vec3_t attachment;
    VectorSubtract(joint.Position, joint.Target->Position, attachment);
    for (auto birth : Emissions(FPS_ANIMATION_FACTOR))
    {
        vec3_t position, light;
        joint.Target->MotionTrace.Sample(WorldTime, birth.FrameFraction(), joint.Target->Position,
                                         position);
        VectorAdd(position, attachment, position);
        const double time = WorldTime - birth.SceneRemainingFrames() * 1000.0 /
                                            sessionKeeper_.ApplicationConfig().legacyReferenceFps;
        const float luminosity = std::sin(time * 0.002) * (blue || red ? 0.3f : 0.2f) + 0.8f;
        if (blue)
        {
            Vector(luminosity * 0.5f, luminosity * 0.1f, luminosity, light);
        }
        else if (red)
        {
            Vector(luminosity, luminosity * 0.1f, luminosity * 0.1f, light);
        }
        else
        {
            Vector(luminosity, luminosity, luminosity, light);
        }
        if (!red)
            VectorMul(light, joint.Light, light);
        CreateParticle(BITMAP_LIGHTNING + 1, position, joint.Angle, light, blue ? 0 : 4);
    }
}

namespace
{
bool HasSampledBoneTrail(const JOINT &joint)
{
    return joint.Type == BITMAP_JOINT_ENERGY &&
           ((joint.SubType >= 48 && joint.SubType <= 53) || joint.SubType == 57);
}

void AdvanceBoneTrail(JOINT &joint, BMD &model, int bone, float frames, double worldTime)
{
    AnimationPoseSample pose(joint.Target, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    const float totalFrames = frames;
    vec3_t offset{};
    float matrix[3][4];
    AngleMatrix(joint.Angle, matrix);
    while (frames > 0.f)
    {
        const float step = (std::min)(frames, 1.f - joint.TailSampleFrames);
        frames -= step;
        pose.SampleBonePosition(
            model, *joint.Target, bone, offset, worldTime,
            joint.BirthTiming.FrameFraction((totalFrames - frames) / totalFrames), joint.Position);
        GameLogic::Effects::CreateTimedTail(&joint, matrix, step);
    }
}

bool AdvanceForceArcSample(JOINT &joint, float &samples)
{
    constexpr float Radius = 145.f, SampleTurn = -11.f;
    const bool growing = joint.NumTails < joint.MaxTails - 1;
    if (joint.MotionFrames <= 0.f)
    {
        if (growing)
        {
            VectorCopy(joint.Position, joint.StartPosition);
            vec3_t local{0.f, -Radius, 0.f}, offset, endpoint;
            float matrix[3][4];
            AngleMatrix(joint.Direction, matrix);
            VectorRotate(local, matrix, offset);
            VectorAdd(joint.TargetPosition, offset, endpoint);
            VectorSubtract(endpoint, joint.Position, joint.MotionVelocity);
        }
        joint.MotionFrames = 1.f;
    }
    const float step = (std::min)(samples, joint.MotionFrames);
    if (growing)
    {
        VectorAddScaled(joint.Position, joint.MotionVelocity, joint.Position, step);
        joint.Direction[2] += SampleTurn * step;
    }
    joint.MotionFrames -= step;
    samples -= step;
    if (joint.MotionFrames > 0.f)
        return false;
    if (growing)
    {
        float matrix[3][4];
        AngleMatrix(joint.Angle, matrix);
        GameLogic::Effects::CreateTail(&joint, matrix);
    }
    return true;
}

void PrepareFlareHeadAngles(JOINT &joint, SessionRandom &random)
{
    if (joint.Type != BITMAP_FLARE + 1 || (joint.SubType != 6 && joint.SubType != 8))
        return;
    constexpr int FullTurn = 360;
    for (float &angle : joint.HeadSpriteAngles)
        angle = static_cast<float>(random.RangeInt(0, RAND_MAX) % FullTurn);
}

float ScrewDirection(double phase, vec3_t result, float speedRate)
{
    constexpr double PitchSpeed = 0.048, YawSpeed = 0.0613, RollSpeed = 0.1113;
    constexpr double PitchOffset = 55555, RollOffset = 11111;
    const double pitch = (phase + PitchOffset) * PitchSpeed * speedRate;
    const double yaw = phase * YawSpeed * speedRate;
    const double roll = (phase + RollOffset) * RollSpeed * speedRate;
    const double x = std::sin(pitch) * std::cos(yaw);
    const double y = std::sin(pitch) * std::sin(yaw);
    const double z = std::cos(pitch);
    const double sinRoll = std::sin(roll), cosRoll = std::cos(roll);
    result[2] = static_cast<float>(x);
    result[1] = static_cast<float>(sinRoll * y + cosRoll * z);
    result[0] = static_cast<float>(cosRoll * y - sinRoll * z);
    return static_cast<float>(sinRoll);
}

float SpiritPitchIntegral(float lifetime)
{
    constexpr float Period = 12.f, HalfPeriod = Period * 0.5f;
    const float phase = lifetime - std::floor(lifetime / Period) * Period;
    return HalfPeriod - std::abs(phase - HalfPeriod);
}

void AdvanceGrowingSpirit(JOINT &joint, float frames, double worldTime)
{
    constexpr float PhaseRate = 30.f, BaseSpeed = 30.f, SpeedPerPhase = 0.1f;
    if (joint.Target)
        joint.Target->MotionTrace.BeginDrivenMotion(worldTime, frames, joint.Position, false,
                                                    joint.BirthTiming.FrameFraction(0.f));
    while (frames > 0.f)
    {
        if (joint.SpiritMotionFrames <= 0.f)
        {
            const float speed = BaseSpeed + SpeedPerPhase * (joint.SpiritPhase + PhaseRate);
            Vector(std::sin(joint.Angle[2]) * speed, -std::cos(joint.Angle[2]) * speed, 0.f,
                   joint.MotionVelocity);
            joint.SpiritMotionFrames = 1.f;
        }
        const float step = (std::min)(frames, joint.SpiritMotionFrames);
        VectorAddScaled(joint.Position, joint.MotionVelocity, joint.Position, step);
        joint.SpiritPhase += PhaseRate * step;
        joint.SpiritMotionFrames -= step;
        frames -= step;
        if (joint.Target)
            joint.Target->MotionTrace.Advance(step, joint.Position);
    }
}

void AdvanceSpiritPitch(JOINT &joint, float frames, float startingLife)
{
    constexpr float PitchPerTick = 6.f, InitialPitch = -90.f;
    // The authored update samples the remaining integer lifetime before decrementing it.
    const float end = startingLife + 1.f;
    joint.SpiritPhase +=
        PitchPerTick * (SpiritPitchIntegral(end) - SpiritPitchIntegral(end - frames));
    joint.Angle[0] = InitialPitch - joint.SpiritPhase;
}

void AdvanceTurningSpirit(JOINT &joint, float frames, double worldTime)
{
    constexpr float Acceleration = 3.f, YawPerTick = 10.f;
    if (joint.Target)
        joint.Target->MotionTrace.BeginDrivenMotion(worldTime, frames, joint.Position, false,
                                                    joint.BirthTiming.FrameFraction(0.f));
    float elapsed = 0.f;
    while (frames > 0.f)
    {
        if (joint.SpiritMotionFrames <= 0.f)
        {
            joint.SpiritMotionFrames = 1.f;
            float matrix[3][4];
            vec3_t velocity{0.f, -joint.Velocity, 0.f};
            AngleMatrix(joint.Angle, matrix);
            // Subtype 18 owns Direction as the held reference-interval velocity.
            VectorRotate(velocity, matrix, joint.Direction);
        }
        const float step = (std::min)(frames, joint.SpiritMotionFrames);
        VectorAddScaled(joint.Position, joint.Direction, joint.Position, step);
        joint.Velocity += Acceleration * step;
        AdvanceSpiritPitch(joint, step, joint.LifeTime - elapsed);
        joint.Angle[2] += YawPerTick * step;
        joint.SpiritMotionFrames -= step;
        elapsed += step;
        frames -= step;
        if (joint.Target)
            joint.Target->MotionTrace.Advance(step, joint.Position);
    }
}

bool AdvanceJointToTarget(JOINT &joint, const vec3_t velocity, float radius, float &frames,
                          int dimensions = 3)
{
    double a = 0., projection = 0., c = -double(radius) * radius;
    for (int axis = 0; axis < dimensions; ++axis)
    {
        const double offset = double(joint.Position[axis]) - joint.TargetPosition[axis];
        a += double(velocity[axis]) * velocity[axis];
        projection += offset * velocity[axis];
        c += offset * offset;
    }
    bool contact = c <= 0.;
    if (contact)
        frames = 0.f;
    else if (a > 0. && projection < 0.)
    {
        const double determinant = projection * projection - a * c;
        if (determinant >= 0.)
        {
            const double arrival = c / (-projection + std::sqrt(determinant));
            if (arrival <= frames)
            {
                frames = static_cast<float>(arrival);
                contact = true;
            }
        }
    }
    VectorAddScaled(joint.Position, velocity, joint.Position, frames);
    return contact;
}

bool ClipJointEscape(const JOINT &joint, const vec3_t velocity, float radius, float &frames)
{
    double speedSquared = 0., projection = 0., range = -double(radius) * radius;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double offset = double(joint.Position[axis]) - joint.TargetPosition[axis];
        speedSquared += double(velocity[axis]) * velocity[axis];
        projection += offset * velocity[axis];
        range += offset * offset;
    }
    if (range > 0.)
    {
        frames = 0.f;
        return true;
    }
    if (speedSquared == 0.)
        return false;
    const double root = std::sqrt(projection * projection - speedSquared * range);
    const double departure = projection >= 0. && projection + root > 0.
                                 ? -range / (projection + root)
                                 : (-projection + root) / speedSquared;
    if (departure > frames)
        return false;
    frames = static_cast<float>(departure);
    return true;
}

bool OwnsMovingLightning(const JOINT &joint)
{
    if (joint.Type == MODEL_FENRIR_SKILL_THUNDER || joint.Type == BITMAP_BLUR + 1 ||
        joint.Type == BITMAP_JOINT_LASER + 1)
        return true;
    if (joint.Type != BITMAP_JOINT_THUNDER)
        return false;
    switch (joint.SubType)
    {
    case 0:
    case 1:
    case 2:
    case 3:
    case 11:
    case 16:
    case 17:
    case 20:
    case 21:
    case 25:
    case 26:
    case 27:
    case 28:
    case 33:
        return true;
    default:
        return false;
    }
}

template <class Arrival>
void AdvanceMovingLightning(JOINT &joint, float frames, SessionRandom &random, double worldTime,
                            Arrival &&emitArrival)
{
    const bool fenrir = joint.Type == MODEL_FENRIR_SKILL_THUNDER;
    const bool thunder = joint.Type == BITMAP_JOINT_THUNDER;
    const bool damped = !fenrir && !thunder;
    const float targetRise = thunder && joint.SubType == 20 ? 100.f : 0.f;
    const float sampleCount = static_cast<float>(joint.MaxTails);
    const float totalSamples = frames * sampleCount;
    float samples = totalSamples;
    while (samples > 0.f)
    {
        if (joint.SpiritMotionFrames <= 0.f)
        {
            joint.SpiritMotionFrames = sampleCount;
            joint.MotionChecksTarget = false;
        }
        if (joint.MotionFrames <= 0.f)
        {
            if (joint.Target && (fenrir || (thunder ? joint.SubType != 3 && joint.SubType != 11 &&
                                                          joint.SubType != 17 &&
                                                          joint.SubType != 20 && joint.SubType != 26
                                                    : joint.SubType != 2)))
            {
                joint.Target->MotionTrace.Sample(
                    worldTime,
                    joint.BirthTiming.FrameFraction((totalSamples - samples) / totalSamples),
                    joint.Target->Position, joint.TargetPosition);
                joint.TargetPosition[2] += 80.f;
            }
            vec3_t position, angle, direction, travel{0.f, -joint.Velocity, 0.f}, before{};
            float matrix[3][4];
            VectorCopy(joint.Position, position);
            if (joint.SpiritMotionFrames == sampleCount)
            {
                AngleMatrix(joint.Angle, matrix);
                VectorRotate(travel, matrix, before);
                VectorAdd(position, before, position);
            }
            VectorCopy(joint.Angle, angle);
            const float turn =
                thunder
                    ? (joint.SubType == 20                                                   ? 100.f
                       : joint.SubType == 25                                                 ? 40.f
                       : (joint.SubType == 11 || joint.SubType == 17 || joint.SubType == 26) ? 25.f
                                                                                             : 50.f)
                : fenrir ? 50.f
                         : 25.f;
            vec3_t steeringTarget;
            VectorCopy(joint.TargetPosition, steeringTarget);
            steeringTarget[2] += targetRise;
            ::MoveHumming(position, angle, steeringTarget, turn);
            VectorCopy(joint.Direction, direction);
            for (int axis : {0, 2})
            {
                const int range =
                    damped || (thunder && (joint.SubType == 1 || joint.SubType == 28)) ? 256 : 1024;
                const float divisor =
                    thunder && (joint.SubType == 1 || joint.SubType == 28) ? 1.f : joint.Scale;
                const float strength = thunder && joint.SubType == 11 ? 0.7f : 1.f;
                const float noise =
                    (random.RangeInt(0, RAND_MAX) % range - range / 2) * strength / divisor;
                direction[axis] = damped ? (direction[axis] + noise) * 0.8f : noise;
            }
            if (damped)
                direction[1] *= 0.8f;
            for (int axis = 0; axis < 3; ++axis)
                joint.MotionAngleRate[axis] =
                    std::remainder(angle[axis] - joint.Angle[axis], 360.f);
            joint.MotionAcceleration = direction[0] - joint.Direction[0];
            joint.MotionScale = direction[2] - joint.Direction[2];
            VectorAdd(angle, direction, angle);
            AngleMatrix(angle, matrix);
            VectorRotate(travel, matrix, joint.MotionVelocity);
            VectorAdd(joint.MotionVelocity, before, joint.MotionVelocity);
            joint.MotionFrames = 1.f;
        }
        float step = (std::min)(samples, (std::min)(joint.MotionFrames, joint.SpiritMotionFrames));
        bool contact = false, escaped = false;
        if (joint.MotionChecksTarget)
            emitArrival((totalSamples - samples) / sampleCount, step / sampleCount);
        else
        {
            vec3_t relativeVelocity;
            VectorCopy(joint.MotionVelocity, relativeVelocity);
            relativeVelocity[2] -= targetRise;
            if (thunder && joint.SubType == 3)
            {
                escaped = ClipJointEscape(joint, relativeVelocity, 150.f, step);
                VectorAddScaled(joint.Position, relativeVelocity, joint.Position, step);
            }
            else
                contact = AdvanceJointToTarget(joint, relativeVelocity,
                                               joint.Velocity * (damped ? 2.f : 1.5f), step);
            joint.Position[2] += targetRise * step;
            joint.TargetPosition[2] += targetRise * step;
            VectorAddScaled(joint.Angle, joint.MotionAngleRate, joint.Angle, step);
            joint.Direction[0] += joint.MotionAcceleration * step;
            joint.Direction[2] += joint.MotionScale * step;
            if (damped)
                joint.Direction[1] *= std::pow(0.8f, step);
            vec3_t angle;
            float matrix[3][4];
            VectorAdd(joint.Angle, joint.Direction, angle);
            AngleMatrix(angle, matrix);
            GameLogic::Effects::CreateTimedTail(&joint, matrix, step);
        }
        if (escaped)
        {
            joint.LifeTime = 0.f;
            return;
        }
        samples -= step;
        joint.MotionFrames -= step;
        joint.SpiritMotionFrames -= step;
        if (contact)
        {
            joint.MotionChecksTarget = true;
            joint.MotionFrames = joint.SpiritMotionFrames;
        }
        if (joint.SpiritMotionFrames <= 0.f)
        {
            float matrix[3][4];
            AngleMatrix(joint.Angle, matrix);
            GameLogic::Effects::CreateTail(&joint, matrix);
        }
    }
}

void AdvanceLaserJoint(JOINT &joint, float frames, double worldTime)
{
    const float totalFrames = frames;
    float elapsed = 0.f;
    while (frames > 0.f)
    {
        if (joint.MotionFrames <= 0.f)
        {
            joint.Target->MotionTrace.Sample(worldTime,
                                             joint.BirthTiming.FrameFraction(elapsed / totalFrames),
                                             joint.Target->Position, joint.TargetPosition);
            joint.TargetPosition[2] += 130.f;
            constexpr float SteeringStep = 1.f / 16.f;
            vec3_t endAngle, midpoint;
            VectorCopy(joint.Angle, endAngle);
            const float dx = joint.Position[0] - joint.TargetPosition[0];
            const float dy = joint.Position[1] - joint.TargetPosition[1];
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float turn = distance > 0.f ? 3000.f / distance : 360.f / SteeringStep;
            ::MoveHumming(joint.Position, endAngle, joint.TargetPosition, turn * SteeringStep);
            for (int axis = 0; axis < 3; ++axis)
            {
                const float change = std::remainder(endAngle[axis] - joint.Angle[axis], 360.f);
                joint.MotionAngleRate[axis] = change / SteeringStep;
                midpoint[axis] = joint.Angle[axis] + change * 0.5f;
            }
            float matrix[3][4];
            vec3_t velocity{0.f, -joint.Velocity, 0.f};
            AngleMatrix(midpoint, matrix);
            VectorRotate(velocity, matrix, joint.MotionVelocity);
            joint.MotionFrames = SteeringStep;
        }
        float step = (std::min)(frames, joint.MotionFrames);
        if (!joint.Collision &&
            AdvanceJointToTarget(joint, joint.MotionVelocity, joint.Velocity * 2.f, step, 2))
        {
            joint.Collision = true;
            // The common lifetime tail subtracts this update's full duration.
            joint.LifeTime = 5.f + elapsed + step;
        }
        else if (joint.Collision)
            VectorAddScaled(joint.Position, joint.MotionVelocity, joint.Position, step);
        VectorAddScaled(joint.Angle, joint.MotionAngleRate, joint.Angle, step);
        joint.MotionFrames -= step;
        elapsed += step;
        frames -= step;
    }
}

bool OwnsEnergyMotion(const JOINT &joint)
{
    if (joint.Type != BITMAP_JOINT_ENERGY)
        return false;
    switch (joint.SubType)
    {
    case 0:
    case 1:
    case 6:
    case 9:
    case 12:
    case 13:
    case 16:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
        return true;
    default:
        return false;
    }
}

enum class JointArrival
{
    None,
    Target,
    Escaped
};

JointArrival AdvanceSteeringJoint(JOINT &joint, float frames, SessionRandom &random,
                                  double worldTime)
{
    const float totalFrames = frames;
    float elapsed = 0.f;
    while (frames > 0.f)
    {
        if (joint.MotionFrames <= 0.f)
        {
            float matrix[3][4];
            vec3_t velocity{0.f, -joint.Velocity, 0.f}, endpoint, endAngle;
            AngleMatrix(joint.Angle, matrix);
            VectorRotate(velocity, matrix, joint.MotionVelocity);
            VectorCopy(joint.Angle, endAngle);
            float endSpeed = joint.Velocity;
            const long life = std::lround(joint.LifeTime - elapsed);
            joint.MotionChecksTarget = false;
            if (joint.Type == BITMAP_FLARE + 1)
            {
                const bool early = life >= (joint.SubType == 6 ? 30 : 25);
                if (early)
                {
                    endAngle[2] -= 10.f;
                    joint.MotionVelocity[2] += 2.5f;
                }
                if (!early || joint.SubType == 6)
                {
                    endSpeed = (std::min)(joint.Velocity + 5.f, 30.f);
                    vec3_t target;
                    if (joint.SubType == 5)
                    {
                        VectorCopy(joint.TargetPosition, target);
                    }
                    else
                    {
                        joint.Target->MotionTrace.Sample(
                            worldTime,
                            joint.BirthTiming.FrameFraction((totalFrames - frames) / totalFrames),
                            joint.Target->Position, target);
                    }
                    if (joint.SubType == 6)
                        target[2] += 150.f;
                    VectorAdd(joint.Position, joint.MotionVelocity, endpoint);
                    const float yaw = endAngle[2];
                    const float distance = ::MoveHumming(endpoint, endAngle, target, endSpeed);
                    if (distance <= 70.f &&
                        std::abs(std::remainder(endAngle[2] - yaw, 360.f)) > 20.f &&
                        endSpeed >= 20.f)
                        endSpeed -= joint.SubType == 5 ? 5.f : 10.f;
                }
            }
            else if (joint.SubType == 42)
            {
                if (life >= 60)
                    joint.MotionVelocity[2] += 1.f;
                else
                    endSpeed = (std::min)(joint.Velocity + 5.f, 30.f);
                joint.Target->MotionTrace.Sample(
                    worldTime,
                    joint.BirthTiming.FrameFraction((totalFrames - frames) / totalFrames),
                    joint.Target->Position, joint.TargetPosition);
                joint.TargetPosition[2] += 260.f;
                VectorAdd(joint.Position, joint.MotionVelocity, endpoint);
                const float distance =
                    ::MoveHumming(endpoint, endAngle, joint.TargetPosition, endSpeed);
                joint.MotionChecksTarget = true;
                if (distance <= 70.f &&
                    std::abs(std::remainder(endAngle[2] - joint.Angle[2], 360.f)) > 20.f &&
                    endSpeed >= 20.f)
                    endSpeed -= 10.f;
            }
            else if (joint.SubType == 43)
            {
                endSpeed = (std::min)(joint.Velocity + 5.f, 40.f);
                joint.Target->MotionTrace.Sample(
                    worldTime,
                    joint.BirthTiming.FrameFraction((totalFrames - frames) / totalFrames),
                    joint.Target->Position, joint.TargetPosition);
                joint.TargetPosition[2] += 100.f;
                VectorAdd(joint.Position, joint.MotionVelocity, endpoint);
                endAngle[2] = CreateAngle2D(endpoint, joint.TargetPosition);
                ::MoveHumming(endpoint, endAngle, joint.TargetPosition, endSpeed);
                joint.MotionChecksTarget = true;
            }
            else if (life >= 100 && joint.SubType != 44)
            {
                float yaw = -10.f, rise = 5.f;
                if (joint.SubType == 0 || joint.SubType == 45)
                {
                    yaw = 10.f;
                    rise = 6.f;
                }
                else if (joint.SubType == 12 || joint.SubType == 13 || joint.SubType == 16 ||
                         joint.SubType == 46)
                {
                    const bool fast = joint.SubType == 16 || joint.SubType == 46;
                    yaw = fast ? -joint.MultiUse * 20.f : (joint.SubType == 12 ? 15.f : -20.f);
                    rise = fast ? 8.f : 6.f;
                    joint.MotionVelocity[0] +=
                        joint.MultiUse * (random.RangeInt(0, RAND_MAX) % 5 + (fast ? 3 : 0));
                    joint.MotionVelocity[1] +=
                        joint.MultiUse * (random.RangeInt(0, RAND_MAX) % 5 + (fast ? 3 : 0));
                }
                endAngle[2] += yaw;
                joint.MotionVelocity[2] += rise;
            }
            else
            {
                const bool middle44 = joint.SubType == 44 && life <= 120 && life >= 10;
                const bool fast = joint.SubType == 12 || joint.SubType == 13 ||
                                  joint.SubType == 16 || joint.SubType == 46;
                const float acceleration =
                    middle44 ? (life >= 20 ? 10.f : 12.f) : (fast ? 7.f : 5.f);
                endSpeed = (std::min)(joint.Velocity + acceleration,
                                      middle44 ? 20.f : (fast ? 40.f : 30.f));
                if (middle44 && life < 20)
                {
                    joint.MotionVelocity[0] +=
                        std::cos(Q_PI / (random.RangeInt(0, RAND_MAX) % 180 + 150.f));
                    joint.MotionVelocity[1] +=
                        std::sin(Q_PI / (random.RangeInt(0, RAND_MAX) % 180 + 150.f));
                }
                else if (joint.SubType != 6 && joint.SubType != 9)
                {
                    joint.Target->MotionTrace.Sample(
                        worldTime,
                        joint.BirthTiming.FrameFraction((totalFrames - frames) / totalFrames),
                        joint.Target->Position, joint.TargetPosition);
                    joint.TargetPosition[2] += 120.f;
                }
                VectorAdd(joint.Position, joint.MotionVelocity, endpoint);
                const float distance =
                    ::MoveHumming(endpoint, endAngle, joint.TargetPosition, endSpeed);
                joint.MotionChecksTarget = !middle44;
                if (!middle44 && distance <= 70.f &&
                    std::abs(std::remainder(endAngle[2] - joint.Angle[2], 360.f)) > 20.f &&
                    endSpeed >= 20.f)
                    endSpeed -= 10.f;
            }
            for (int axis = 0; axis < 3; ++axis)
                joint.MotionAngleRate[axis] =
                    std::remainder(endAngle[axis] - joint.Angle[axis], 360.f);
            joint.MotionAcceleration = endSpeed - joint.Velocity;
            joint.MotionFrames = 1.f;
        }
        float step = (std::min)(frames, joint.MotionFrames);
        bool escaped = false;
        if (joint.Type == BITMAP_JOINT_ENERGY && joint.SubType == 43)
        {
            escaped = ClipJointEscape(joint, joint.MotionVelocity, 550.f, step);
        }
        bool contact = false;
        if (joint.MotionChecksTarget)
            contact = AdvanceJointToTarget(joint, joint.MotionVelocity,
                                           joint.SubType == 43 ? 60.f : 35.f, step);
        else
            VectorAddScaled(joint.Position, joint.MotionVelocity, joint.Position, step);
        VectorAddScaled(joint.Angle, joint.MotionAngleRate, joint.Angle, step);
        joint.Velocity += joint.MotionAcceleration * step;
        joint.MotionFrames -= step;
        elapsed += step;
        frames -= step;
        if (contact)
            return JointArrival::Target;
        if (escaped)
            return JointArrival::Escaped;
    }
    return JointArrival::None;
}

void AdvanceWanderingSpirit(JOINT &joint, float frames, SessionRandom &random, const World &world,
                            double worldTime)
{
    const float totalFrames = frames;
    if (joint.SubType == 24)
        joint.Target->MotionTrace.BeginDrivenMotion(worldTime, frames, joint.Position, true,
                                                    joint.BirthTiming.FrameFraction(0.f));
    while (frames > 0.f)
    {
        if (joint.MotionFrames <= 0.f)
        {
            float matrix[3][4];
            vec3_t velocity{0.f, -joint.Velocity, 0.f}, endpoint, endAngle, target;
            AngleMatrix(joint.Angle, matrix);
            VectorRotate(velocity, matrix, joint.MotionVelocity);
            VectorAdd(joint.Position, joint.MotionVelocity, endpoint);
            VectorCopy(joint.Angle, endAngle);
            if (joint.SubType == 24)
            {
                VectorCopy(joint.TargetPosition, target);
                target[2] += 80.f;
            }
            else
            {
                joint.Target->MotionTrace.Sample(
                    worldTime,
                    joint.BirthTiming.FrameFraction((totalFrames - frames) / totalFrames),
                    joint.Target->Position, joint.TargetPosition);
                joint.TargetPosition[2] += 80.f;
                VectorCopy(joint.TargetPosition, target);
            }
            ::MoveHumming(endpoint, endAngle, target,
                          joint.SubType == 5 ? 2.f : (joint.SubType == 24 ? 5.f : 10.f));
            float pitchForce = joint.Direction[0], yawForce = joint.Direction[2];
            if (joint.SubType != 5)
            {
                pitchForce += (random.RangeInt(0, RAND_MAX) % 32 - 16) * 0.2f;
                yawForce += (random.RangeInt(0, RAND_MAX) % 32 - 16) * 0.8f;
                endAngle[0] += pitchForce;
                endAngle[2] += yawForce;
                pitchForce *= 0.6f;
                yawForce *= 0.8f;
            }
            if (joint.SubType != 24)
            {
                const float ground = world.SampleTerrainHeight(endpoint[0], endpoint[1]);
                if (endpoint[2] < ground + 100.f || endpoint[2] > ground + 400.f)
                {
                    pitchForce = 0.f;
                    endAngle[0] = endpoint[2] < ground + 100.f ? -5.f : 5.f;
                }
            }
            joint.MotionAcceleration = pitchForce - joint.Direction[0];
            // These variants do not otherwise use Direction[1].
            joint.Direction[1] = yawForce - joint.Direction[2];
            for (int axis = 0; axis < 3; ++axis)
                joint.MotionAngleRate[axis] =
                    std::remainder(endAngle[axis] - joint.Angle[axis], 360.f);
            joint.MotionFrames = 1.f;
        }
        float step = (std::min)(frames, joint.MotionFrames);
        if (joint.SubType != 24 && !joint.Collision)
        {
            if (AdvanceJointToTarget(joint, joint.MotionVelocity, joint.Velocity * 2.f, step, 2))
                joint.Collision = true;
        }
        else
            VectorAddScaled(joint.Position, joint.MotionVelocity, joint.Position, step);
        if (joint.SubType == 24)
        {
            joint.Target->MotionTrace.TurnYaw(
                step, joint.Angle[2], joint.Angle[2] + joint.MotionAngleRate[2] * step, 0.f);
            joint.Target->MotionTrace.Advance(step, joint.Position);
        }
        VectorAddScaled(joint.Angle, joint.MotionAngleRate, joint.Angle, step);
        joint.Direction[0] += joint.MotionAcceleration * step;
        joint.Direction[2] += joint.Direction[1] * step;
        joint.MotionFrames -= step;
        frames -= step;
    }
}

void AdvanceScrewSpirit(JOINT &joint, float frames, double endPhase, SessionRandom &random,
                        double worldTime)
{
    const float totalFrames = frames;
    float elapsed = 0.f;
    while (frames > 0.f)
    {
        if (joint.MotionFrames <= 0.f)
        {
            float matrix[3][4];
            vec3_t velocity{0.f, -joint.Velocity, 0.f}, endpoint, endAngle;
            AngleMatrix(joint.Angle, matrix);
            VectorRotate(velocity, matrix, joint.MotionVelocity);
            VectorCopy(joint.Angle, endAngle);
            float speed = joint.Velocity;
            joint.MotionChecksTarget = false;
            if (joint.SubType == 3 || std::lround(joint.LifeTime - elapsed) > 28)
            {
                vec3_t screw;
                ScrewDirection(endPhase - frames + 1.f, screw, 1.f);
                VectorAddScaled(joint.MotionVelocity, screw, joint.MotionVelocity, 50.f);
            }
            else if (joint.Target != nullptr)
            {
                if (random.FpsCheck(2, 1.0))
                    endAngle[0] += endAngle[0] < -90.f ? 20.f : -20.f;
                vec3_t target;
                joint.Target->MotionTrace.Sample(
                    worldTime,
                    joint.BirthTiming.FrameFraction((totalFrames - frames) / totalFrames),
                    joint.Target->Position, target);
                target[0] += random.RangeInt(0, RAND_MAX) % 150 - 70.f;
                target[1] += random.RangeInt(0, RAND_MAX) % 150 - 70.f;
                target[2] += 50.f;
                VectorAdd(joint.Position, joint.MotionVelocity, endpoint);
                const float distance =
                    ::MoveHumming(endpoint, endAngle, target, joint.Velocity - 20.f);
                speed = (std::min)(50.f, joint.Velocity + 1.f);
                joint.MotionChecksTarget = distance < 50.f;
            }
            for (int axis = 0; axis < 3; ++axis)
                joint.MotionAngleRate[axis] =
                    std::remainder(endAngle[axis] - joint.Angle[axis], 360.f);
            joint.MotionAcceleration = speed - joint.Velocity;
            joint.MotionFrames = 1.f;
        }
        const float step = (std::min)(frames, joint.MotionFrames);
        const float referenceLife =
            joint.LifeTime - elapsed +
            (1.f - joint.MotionFrames) * (joint.MotionChecksTarget ? 2.f : 1.f);
        VectorAddScaled(joint.Position, joint.MotionVelocity, joint.Position, step);
        VectorAddScaled(joint.Angle, joint.MotionAngleRate, joint.Angle, step);
        joint.Velocity += joint.MotionAcceleration * step;
        if (joint.SubType == 13 && joint.Target != nullptr)
        {
            if (referenceLife - (joint.MotionChecksTarget ? 1.f : 0.f) < 10.f)
                VectorScale(joint.Light, std::pow(1.f / 1.2f, step), joint.Light);
            if (joint.MotionChecksTarget)
                joint.LifeTime -= step;
        }
        joint.MotionFrames -= step;
        elapsed += step;
        frames -= step;
    }
}

void AdvanceRisingFlare(JOINT &joint, float frames, SessionRandom &random)
{
    float elapsed = 0.f;
    float matrix[3][4];
    AngleMatrix(joint.Angle, matrix);
    while (frames > 0.f)
    {
        if (joint.MotionFrames <= 0.f)
        {
            const float phase = joint.Direction[1] * (joint.SubType == 15 ? -0.1f : 0.1f);
            joint.MotionVelocity[0] =
                joint.TargetPosition[0] + std::cos(phase) * joint.Velocity - joint.Position[0];
            joint.MotionVelocity[1] =
                joint.TargetPosition[1] - std::sin(phase) * joint.Velocity - joint.Position[1];
            joint.MotionVelocity[2] = joint.Direction[2] - matrix[2][1] * joint.Velocity;
            joint.MotionAngleRate[0] = (random.RangeInt(0, RAND_MAX) % 200 + 200) / 100.f;
            joint.MotionAcceleration = std::lround(joint.LifeTime - elapsed) < 20 ? 5.f : 0.f;
            joint.MotionFrames = 1.f;
        }
        const float step = (std::min)(frames, joint.MotionFrames);
        VectorAddScaled(joint.Position, joint.MotionVelocity, joint.Position, step);
        joint.Direction[1] += 4.f * step;
        joint.Direction[2] += joint.MotionAngleRate[0] * step;
        joint.Velocity += joint.MotionAcceleration * step;
        joint.Scale += step;
        joint.MotionFrames -= step;
        elapsed += step;
        frames -= step;
    }
}

void AdvanceTurningFlare(JOINT &joint, float frames)
{
    float elapsed = 0.f;
    while (frames > 0.f)
    {
        if (joint.MotionFrames <= 0.f)
        {
            const long life = std::lround(joint.LifeTime - elapsed);
            joint.MotionChecksTarget = life < 20 && life >= 8;
            joint.MotionScale = 0.f;
            if (joint.MotionChecksTarget)
            {
                joint.Velocity = 200.f;
                joint.MotionScale = (std::min)(
                    std::round(joint.Direction[2]),
                    static_cast<float>((std::max)(1, joint.MaxTails - 1 - joint.NumTails)));
            }
            else
            {
                float matrix[3][4];
                vec3_t velocity{0.f, -joint.Velocity, 0.f};
                AngleMatrix(joint.Angle, matrix);
                VectorRotate(velocity, matrix, joint.MotionVelocity);
                if (life < 8)
                    joint.Velocity = 0.f;
            }
            joint.MotionFrames = 1.f;
        }
        const float step = (std::min)(frames, joint.MotionFrames);
        if (joint.MotionChecksTarget)
        {
            float samples = step * joint.MotionScale;
            while (samples > 0.f)
            {
                if (joint.SpiritMotionFrames <= 0.f)
                {
                    vec3_t angle, radial{0.f, -200.f, 0.f}, endpoint;
                    VectorCopy(joint.HeadAngle, angle);
                    const float turn = joint.Direction[1] + 1.5f;
                    switch (static_cast<int>(joint.MultiUse))
                    {
                    case 0:
                        angle[0] += 3.f;
                        angle[2] -= turn;
                        break;
                    case 1:
                        angle[0] -= 3.f;
                        angle[2] -= turn;
                        break;
                    case 2:
                        angle[0] += 3.f;
                        angle[2] += turn;
                        break;
                    case 3:
                        angle[0] -= 3.f;
                        angle[2] += turn;
                        break;
                    case 4:
                        angle[2] -= turn;
                        break;
                    case 5:
                        angle[0] -= turn;
                        break;
                    }
                    float matrix[3][4];
                    AngleMatrix(angle, matrix);
                    VectorRotate(radial, matrix, endpoint);
                    VectorAdd(joint.StartPosition, endpoint, endpoint);
                    VectorSubtract(endpoint, joint.Position, joint.MotionVelocity);
                    VectorSubtract(angle, joint.HeadAngle, joint.MotionAngleRate);
                    joint.MotionAcceleration = (std::min)(15.f, turn) - joint.Direction[1];
                    joint.SpiritMotionFrames = 1.f;
                }
                const float sample = (std::min)(samples, joint.SpiritMotionFrames);
                VectorAddScaled(joint.Position, joint.MotionVelocity, joint.Position, sample);
                VectorAddScaled(joint.HeadAngle, joint.MotionAngleRate, joint.HeadAngle, sample);
                joint.Direction[1] += joint.MotionAcceleration * sample;
                float matrix[3][4];
                AngleMatrix(joint.HeadAngle, matrix);
                GameLogic::Effects::CreateTimedTail(&joint, matrix, sample);
                joint.SpiritMotionFrames -= sample;
                samples -= sample;
                if (joint.NumTails >= joint.MaxTails - 1)
                    joint.Velocity = 0.f;
            }
            joint.Direction[2] += 4.f * step;
        }
        else
        {
            VectorAddScaled(joint.Position, joint.MotionVelocity, joint.Position, step);
            if (joint.LifeTime - elapsed < 8.f)
            {
                float matrix[3][4];
                AngleMatrix(joint.HeadAngle, matrix);
                GameLogic::Effects::CreateTimedTail(&joint, matrix, step);
            }
        }
        joint.MotionFrames -= step;
        elapsed += step;
        frames -= step;
    }
}

void AdvanceJointRise(JOINT &joint, float frames, float acceleration, float movementCount = 1.f,
                      float postStepOffset = 0.f)
{
    if (frames <= 0.f)
        return;
    float speed = movementCount * joint.Direction[2] + postStepOffset;
    Core::Time::Advance(joint.Position[2], speed, movementCount * acceleration, frames);
    joint.Direction[2] = (speed - postStepOffset) / movementCount;
    joint.Position[0] = joint.TargetPosition[0];
    joint.Position[1] = joint.TargetPosition[1];
}

void AdvanceGhostJoint(JOINT &joint, float frames)
{
    float elapsed = 0.f;
    while (frames > 0.f)
    {
        if (joint.MotionFrames <= 0.f)
        {
            float matrix[3][4];
            vec3_t velocity{0.f, -joint.Velocity, 0.f}, endpoint, endAngle;
            AngleMatrix(joint.Angle, matrix);
            VectorRotate(velocity, matrix, joint.MotionVelocity);
            VectorCopy(joint.Angle, endAngle);
            if (joint.SubType == 0)
            {
                const int life = static_cast<int>(std::lround(joint.LifeTime - elapsed));
                joint.m_sTargetIndex = life % 16 <= 7 ? 10 : -10;
                endAngle[2] += joint.m_sTargetIndex;
            }
            else
            {
                VectorAdd(joint.Position, joint.MotionVelocity, endpoint);
                const float yaw = CreateAngle2D(endpoint, joint.TargetPosition);
                const float dx = endpoint[0] - joint.TargetPosition[0];
                const float dy = endpoint[1] - joint.TargetPosition[1];
                const float distance = std::sqrt(dx * dx + dy * dy);
                endAngle[2] = yaw - (distance > 60.f ? 65.f : 90.f);
                if (distance > 60.f)
                {
                    const float pitch =
                        360.f - CreateAngle(endpoint[2], distance, joint.TargetPosition[2], 0.f);
                    endAngle[0] = TurnAngle2(endAngle[0], pitch, 3.f);
                }
            }
            for (int axis = 0; axis < 3; ++axis)
                joint.MotionAngleRate[axis] =
                    std::remainder(endAngle[axis] - joint.Angle[axis], 360.f);
            joint.MotionFrames = 1.f;
        }
        const float step = (std::min)(frames, joint.MotionFrames);
        VectorAddScaled(joint.Position, joint.MotionVelocity, joint.Position, step);
        VectorAddScaled(joint.Angle, joint.MotionAngleRate, joint.Angle, step);
        joint.MotionFrames -= step;
        elapsed += step;
        frames -= step;
    }
}

void AdvanceSpinningSpirit(JOINT &joint, float frames)
{
    while (frames > 0.f)
    {
        if (joint.MotionFrames <= 0.f)
        {
            float matrix[3][4];
            vec3_t velocity{0.f, -joint.Velocity, 0.f};
            AngleMatrix(joint.Angle, matrix);
            VectorRotate(velocity, matrix, joint.MotionVelocity);
            joint.Velocity = 50.f;
            joint.MotionFrames = 1.f;
        }
        const float step = (std::min)(frames, joint.MotionFrames);
        VectorAddScaled(joint.Position, joint.MotionVelocity, joint.Position, step);
        joint.Angle[2] -= 50.f * step;
        joint.MotionFrames -= step;
        frames -= step;
    }
}

void AdvanceRisingSpirit(JOINT &joint, float frames, SessionRandom &random, int noiseWidth = 10,
                         float acceleration = 1.f)
{
    while (frames > 0.f)
    {
        if (joint.MotionFrames <= 0.f)
        {
            float matrix[3][4];
            vec3_t velocity{0.f, -joint.Velocity, 0.f};
            AngleMatrix(joint.Angle, matrix);
            VectorRotate(velocity, matrix, joint.MotionVelocity);
            joint.MotionVelocity[0] +=
                random.RangeInt(0, RAND_MAX) % noiseWidth - noiseWidth * 0.5f;
            joint.MotionVelocity[1] +=
                random.RangeInt(0, RAND_MAX) % noiseWidth - noiseWidth * 0.5f;
            joint.MotionVelocity[2] += joint.Velocity;
            joint.MotionFrames = 1.f;
        }
        const float step = (std::min)(frames, joint.MotionFrames);
        VectorAddScaled(joint.Position, joint.MotionVelocity, joint.Position, step);
        joint.Velocity += acceleration * step;
        joint.MotionFrames -= step;
        frames -= step;
    }
}

void AdvancePitchingSpirit(JOINT &joint, float frames, SessionRandom &random)
{
    float elapsed = 0.f;
    while (frames > 0.f)
    {
        if (joint.MotionFrames <= 0.f)
        {
            float matrix[3][4];
            vec3_t velocity{0.f, -joint.Velocity, 0.f};
            AngleMatrix(joint.Angle, matrix);
            VectorRotate(velocity, matrix, joint.MotionVelocity);
            float pitch = joint.Angle[0];
            if (std::lround(joint.LifeTime - elapsed) < 42)
            {
                joint.Velocity = 50.f;
                pitch = (std::max)(180.f, pitch);
                if (pitch <= 360.f)
                    pitch += random.RangeInt(0, RAND_MAX) % 3 + 2.f;
            }
            else
            {
                joint.Velocity = 10.f;
                pitch = 110.f;
            }
            joint.MotionAngleRate[0] = pitch - joint.Angle[0];
            joint.MotionFrames = 1.f;
        }
        const float step = (std::min)(frames, joint.MotionFrames);
        VectorAddScaled(joint.Position, joint.MotionVelocity, joint.Position, step);
        joint.Angle[0] += joint.MotionAngleRate[0] * step;
        joint.MotionFrames -= step;
        elapsed += step;
        frames -= step;
    }
}

void AdvanceHomingJoint(JOINT &joint, vec3_t target, float frames, double worldTime)
{
    const bool drain = joint.Type == BITMAP_DRAIN_LIFE_GHOST;
    const float acceleration = drain || joint.SubType == 6 || joint.SubType == 7 ? 2.f : 4.f;
    const float totalFrames = frames;
    const bool fixedTarget =
        !drain && (joint.SubType == 6 || joint.SubType == 7 || joint.SubType == 8 ||
                   joint.SubType == 12 || joint.SubType == 13 || joint.SubType == 17);
    float elapsed = 0.f;
    while (frames > 0.f)
    {
        if (joint.MotionFrames <= 0.f)
        {
            float matrix[3][4];
            vec3_t velocity{0.f, -joint.Velocity, 0.f}, endpoint, endAngle;
            AngleMatrix(joint.Angle, matrix);
            VectorRotate(velocity, matrix, joint.MotionVelocity);
            VectorAdd(joint.Position, joint.MotionVelocity, endpoint);
            VectorCopy(joint.Angle, endAngle);
            vec3_t sampledTarget;
            VectorCopy(target, sampledTarget);
            if (!fixedTarget && joint.Target)
            {
                vec3_t ownerPosition;
                joint.Target->MotionTrace.Sample(
                    worldTime, joint.BirthTiming.FrameFraction(elapsed / totalFrames),
                    joint.Target->Position, ownerPosition);
                for (int axis = 0; axis < 3; ++axis)
                    sampledTarget[axis] += ownerPosition[axis] - joint.Target->Position[axis];
            }
            const bool straight =
                !drain && (joint.SubType == 6 || joint.SubType == 7 || joint.SubType == 12);
            const bool earlySteering =
                !drain && (joint.SubType == 8 || joint.SubType == 13 || joint.SubType == 17);
            if (!straight && (!earlySteering || std::lround(joint.LifeTime - elapsed) > 10))
                ::MoveHumming(endpoint, endAngle, sampledTarget, drain ? joint.Velocity : 10.f);
            for (int axis = 0; axis < 3; ++axis)
                joint.MotionAngleRate[axis] =
                    std::remainder(endAngle[axis] - joint.Angle[axis], 360.f);
            joint.MotionFrames = 1.f;
        }
        const float step = (std::min)(frames, joint.MotionFrames);
        VectorAddScaled(joint.Position, joint.MotionVelocity, joint.Position, step);
        VectorAddScaled(joint.Angle, joint.MotionAngleRate, joint.Angle, step);
        joint.Velocity += acceleration * step;
        joint.MotionFrames -= step;
        elapsed += step;
        frames -= step;
    }
}

void AdvanceHealingOrbit(JOINT &joint, float frames)
{
    const bool rising = joint.SubType == 9;
    const float initialLife = rising ? 90.f : 80.f;
    const float initialRadius = rising ? 50.f : 80.f;
    const float age = initialLife - joint.LifeTime;
    const float active = (std::min)(frames, (std::max)(0.f, initialRadius + 1.f - age));
    const float endAge = age + active;
    const float rate = rising || joint.Collision ? 2.f : -2.f;
    joint.MultiUse += rate * active;
    joint.Direction[0] = (std::max)(0.f, initialRadius - endAge);
    // Preserve the authored one-reference lag, rather than one rendering update.
    const float lag = (std::min)(1.f, endAge);
    const float phase = (joint.MultiUse - rate * lag) * 0.1f;
    const float sampleAge = endAge - lag;
    const float radius = (std::max)(0.f, initialRadius - sampleAge);
    joint.Position[0] = joint.TargetPosition[0] + std::sin(phase) * radius;
    joint.Position[1] = joint.TargetPosition[1] + std::cos(phase) * radius;
    joint.Position[2] = joint.TargetPosition[2] +
                        (rising ? 10.f + sampleAge : 300.f * (1.f - sampleAge / initialLife));
    const float growing = (std::min)(active, (std::max)(0.f, (rising ? 10.f : 12.f) - age));
    const float fading = (std::max)(0.f, endAge - 41.f) - (std::max)(0.f, age - 41.f);
    VectorScale(joint.Light,
                std::pow(rising ? 1.25f : 1.2f, growing) * std::pow(1.f / 1.1f, fading),
                joint.Light);
    if (endAge >= initialRadius + 1.f)
        joint.LifeTime = 0.f;
}

float AdvanceJointAcceleration(JOINT &joint, float frames, float accelerationChange)
{
    const double f = frames;
    const double pairs = f * (f - 1.) * 0.5;
    const double triples = pairs * (f - 2.) / 3.;
    const float travel = static_cast<float>(joint.Velocity * f + joint.Direction[2] * pairs +
                                            accelerationChange * triples);
    joint.Velocity += static_cast<float>(joint.Direction[2] * f + accelerationChange * pairs);
    joint.Direction[2] += accelerationChange * frames;
    return travel;
}

void AdvanceAcceleratingForce(JOINT &joint, float frames)
{
    const float boundary = joint.SubType == 7 || joint.SubType == 20 ? 19.f : 14.f;
    const float early = (std::min)(frames, (std::max)(0.f, joint.LifeTime - boundary));
    float travel = AdvanceJointAcceleration(joint, early, joint.Direction[0]);
    travel += AdvanceJointAcceleration(joint, frames - early, 0.5f);
    float matrix[3][4];
    vec3_t velocity{0.f, -travel, 0.f}, movement;
    AngleMatrix(joint.Angle, matrix);
    VectorRotate(velocity, matrix, movement);
    VectorAdd(joint.Position, movement, joint.Position);
}

void AdvanceStraightTrail(JOINT &joint, float frames, int samples, float acceleration)
{
    const bool piercing = joint.Type == BITMAP_PIERCING;
    float matrix[3][4];
    AngleMatrix(joint.Angle, matrix);
    while (frames > 0.f)
    {
        if (joint.MotionFrames <= 0.f)
        {
            vec3_t velocity{};
            velocity[piercing ? 2 : 1] = -joint.Velocity * samples;
            VectorRotate(velocity, matrix, joint.MotionVelocity);
            // Piercing changes speed after all 30 authored samples.
            joint.MotionFrames = piercing ? 1.f : 1.f / samples;
        }
        const float step = (std::min)(frames, joint.MotionFrames);
        VectorAddScaled(joint.Position, joint.MotionVelocity, joint.Position, step);
        joint.Velocity += acceleration * step;
        GameLogic::Effects::CreateTimedTail(&joint, matrix, step * samples);
        joint.MotionFrames -= step;
        frames -= step;
    }
}

void AdvanceExpandingForce(JOINT &joint, float frames)
{
    const float waiting = (std::min)(frames, (std::max)(0.f, joint.Weapon));
    joint.Weapon -= waiting;
    frames -= waiting;
    while (frames > 0.f)
    {
        if (joint.MotionFrames <= 0.f)
        {
            if (joint.NumTails >= joint.MaxTails - 1)
                return;
            // Direction is otherwise unused by these force variants.
            joint.Direction[0] = (std::max)(0.f, std::round(joint.MultiUse) - 1.f);
            joint.MotionFrames = 1.f;
        }
        const float step = (std::min)(frames, joint.MotionFrames);
        float samples = step * joint.Direction[0];
        while (samples > 0.f)
        {
            float matrix[3][4];
            AngleMatrix(joint.Angle, matrix);
            if (joint.SpiritMotionFrames <= 0.f)
            {
                vec3_t local{0.f, joint.Velocity, 0.f}, endpoint;
                VectorRotate(local, matrix, joint.MotionVelocity);
                VectorAdd(joint.StartPosition, joint.MotionVelocity, endpoint);
                if (joint.SubType != 0)
                {
                    vec3_t angle{0.f, joint.TargetPosition[1] - 20.f, joint.Angle[2]};
                    vec3_t radius{0.f, 0.f, joint.TargetPosition[0]}, offset;
                    float orbitMatrix[3][4];
                    AngleMatrix(angle, orbitMatrix);
                    VectorRotate(radius, orbitMatrix, offset);
                    if (joint.SubType == 3 || joint.SubType == 4)
                    {
                        VectorSubtract(endpoint, offset, endpoint);
                    }
                    else
                    {
                        VectorAdd(endpoint, offset, endpoint);
                    }
                }
                VectorSubtract(endpoint, joint.Position, joint.MotionAngleRate);
                joint.SpiritMotionFrames = 1.f;
            }
            const float sample = (std::min)(samples, joint.SpiritMotionFrames);
            VectorAddScaled(joint.StartPosition, joint.MotionVelocity, joint.StartPosition, sample);
            VectorAddScaled(joint.Position, joint.MotionAngleRate, joint.Position, sample);
            joint.Velocity -= 2.f * sample;
            if (joint.SubType != 0)
            {
                // The authored branch uses negative rotation for all these variants.
                joint.TargetPosition[1] -= 20.f * sample;
                joint.TargetPosition[0] -= 2.5f * sample;
            }
            GameLogic::Effects::CreateTimedTail(&joint, matrix, sample);
            joint.SpiritMotionFrames -= sample;
            samples -= sample;
        }
        joint.MultiUse += 2.f * step;
        joint.MotionFrames -= step;
        frames -= step;
    }
}

float ForceTrailRadius(float age)
{
    constexpr float InitialRadius = 30.f, RadiusPerSample = 0.15f;
    const float whole = std::floor(age);
    const float samples = whole * (whole + 1.f) * 0.5f + (age - whole) * (whole + 1.f);
    return InitialRadius - RadiusPerSample * samples;
}

bool RebuildsThunderGeometry(const JOINT &joint)
{
    if (joint.Type != BITMAP_JOINT_THUNDER)
        return false;
    switch (joint.SubType)
    {
    case 4:
    case 5:
    case 6:
    case 7:
    case 9:
    case 12:
    case 18:
    case 19:
    case 22:
    case 23:
    case 24:
        return true;
    default:
        return false;
    }
}

bool IsRebuiltLightning(const JOINT &joint)
{
    return RebuildsThunderGeometry(joint) || (joint.Type == BITMAP_JOINT_THUNDER + 1 &&
                                              (joint.SubType != 0 || joint.LifeTime > 15.f));
}

bool IsSpiralFlare(const JOINT &joint)
{
    return (joint.Type == BITMAP_FLARE || joint.Type == BITMAP_FLARE_BLUE) &&
           (joint.SubType == 4 || joint.SubType == 12);
}

void PositionSpiralFlare(JOINT &joint, float life, const vec3_t origin, float distance)
{
    const float phase = (joint.Direction[0] + life) * 0.1f;
    const float radius = joint.SubType == 12 ? 26.f : (std::max)(life + 40.f, 10.f) * 0.65f;
    const float horizontal = -std::cos(phase) * radius;
    const float height =
        std::sin(phase) * radius - (joint.SubType == 12 ? (90.f - life) * 0.3f : 0.f);
    const float heading = (90.f - joint.Angle[2]) * Q_PI / 180.f;
    joint.TargetPosition[0] = origin[0] + distance * joint.Direction[1];
    joint.TargetPosition[1] = origin[1] + distance * joint.Direction[2];
    joint.Position[0] = horizontal * std::sin(heading) + joint.TargetPosition[0];
    joint.Position[1] = horizontal * std::cos(heading) + joint.TargetPosition[1];
    joint.Position[2] = height + joint.TargetPosition[2];
}

void AdvanceSpiralFlare(JOINT &joint, float frames)
{
    // Original recursion consumed the first partial group, then 12 samples per 25 FPS tick.
    constexpr float SamplesPerTick = 12.f;
    const float initialLife = joint.SubType == 12 ? 70.f : 110.f;
    const float alignedLife = std::floor(initialLife / SamplesPerTick) * SamplesPerTick;
    const float firstRate = initialLife - alignedLife;
    const float firstFrames = (std::max)(0.f, (joint.LifeTime - alignedLife) / firstRate);
    const float distance = (std::min)(frames, firstFrames) * firstRate +
                           (std::max)(0.f, frames - firstFrames) * SamplesPerTick;
    const float oldLife = joint.LifeTime;
    const float newLife = (std::max)(-1.f, oldLife - distance);
    vec3_t origin;
    VectorCopy(joint.TargetPosition, origin);
    float matrix[3][4];
    AngleMatrix(joint.Angle, matrix);
    // Trail entries describe authored geometry, so sample crossed positions, independent of render FPS.
    for (float sample = std::floor(oldLife + 0.0001f) - 1.f; sample >= newLife - 0.0001f;
         sample -= 1.f)
    {
        PositionSpiralFlare(joint, sample + 1.f, origin, oldLife - sample);
        GameLogic::Effects::CreateTail(&joint, matrix);
    }
    PositionSpiralFlare(joint, newLife + 1.f, origin, oldLife - newLife);
    const float fadeFrames = (std::max)(0.f, 9.f - newLife) - (std::max)(0.f, 9.f - oldLife);
    VectorScale(joint.Light, std::pow(1.f / 1.3f, fadeFrames), joint.Light);
    joint.LifeTime = newLife;
}
} // namespace

void SessionGameplayUnit::CreateJointFpsChecked(int Type, vec3_t Position, vec3_t TargetPosition,
                                                vec3_t Angle, int SubType, OBJECT *Target,
                                                float Scale, short PKKey, WORD SkillIndex,
                                                WORD SkillSerialNum, int iChaIndex,
                                                const float *vPriorColor, short int sTargetindex)
{
    for (auto birthTime : Emissions(FPS_ANIMATION_FACTOR))
    {
        CreateJoint(Type, Position, TargetPosition, Angle, SubType, Target, Scale, PKKey,
                    SkillIndex, SkillSerialNum, iChaIndex, vPriorColor, sTargetindex);
    }
}

void SessionGameplayUnit::CreateJoint(int Type, vec3_t Position, vec3_t TargetPosition,
                                      vec3_t Angle, int SubType, OBJECT *Target, float Scale,
                                      short PKKey, WORD SkillIndex, WORD SkillSerialNum,
                                      int iChaIndex, const float *vPriorColor,
                                      short int sTargetindex)
{
    const int i = Joints.Allocate();
    JOINT *o = &Joints[i];
    RegisterEffectBirth(o->BirthTiming, o, i);
    o->Tails.Reset(1);
    o->PresentationRandom = sessionKeeper_.RandomForConstruction().IsPresentation();
    o->Type = Type;
    o->TexType = o->Type;
    o->SubType = SubType;
    o->RenderType = RENDER_TYPE_ALPHA_BLEND;
    o->RenderFace = RENDER_FACE_ONE | RENDER_FACE_TWO;
    o->Collision = false;
    o->PKKey = PKKey;
    o->SpiritPhase = static_cast<float>(PKKey);
    o->SpiritMotionFrames = 0.f;
    o->MotionFrames = 0.f;
    o->MotionAcceleration = 0.f;
    o->MotionScale = 0.f;
    o->MotionChecksTarget = false;
    o->Skill = SkillIndex;
    o->Velocity = 0.f;
    o->Target = NULL;
    o->m_bCreateTails = true;
    o->byOnlyOneRender = 0;
    o->bTileMapping = false;
    o->m_byReverseUV = 0;
    o->m_bySkillSerialNum = (BYTE)SkillSerialNum;
    o->m_sTargetIndex = sTargetindex;
    VectorCopy(Position, o->Position);
    VectorCopy(Angle, o->Angle);
    if (vPriorColor)
    {
        VectorCopy(vPriorColor, o->Light);
    }
    else
    {
        Vector(1.f, 1.f, 1.f, o->Light);
    }
    Vector(0.f, 0.f, 0.f, o->Direction);
    if (Target == NULL)
    {
        VectorCopy(TargetPosition, o->TargetPosition);
    }
    else if (MODEL_SPEARSKILL == Type && o->SubType == 2)
    {
        VectorCopy(TargetPosition, o->TargetPosition);
        o->Target = Target;
    }
    else if (Type == MODEL_SPEARSKILL && (o->SubType == 4 || o->SubType == 9))
    {
        if (iChaIndex != -1)
            o->m_iChaIndex = iChaIndex;
        o->Target = Target;
    }
    else if (Type == MODEL_SPEARSKILL && (o->SubType == 10 || o->SubType == 11))
    {
        if (iChaIndex != -1)
        {
            o->m_iChaIndex = FindCharacterIndex(iChaIndex);
        }
        o->Target = Target;
    }
    else
    {
        o->Target = Target;
    }

    o->NumTails = 0;
    float Matrix[3][4];
    vec3_t tailPosition, p;

    bool bCreateStartTail = true;
    if (Type == BITMAP_FLARE + 1 && o->SubType == 8)
    {
        bCreateStartTail = false;
        o->NumTails = -1;
    }
    else if (Type == BITMAP_SCOLPION_TAIL)
    {
        bCreateStartTail = false;
        o->NumTails = -1;
    }
    else if (Type == BITMAP_JOINT_ENERGY && (o->SubType == 10 || o->SubType == 11))
    {
        bCreateStartTail = false;
        o->NumTails = -1;
    }
    else if (Type == BITMAP_JOINT_ENERGY && (o->SubType == 14 || o->SubType == 15))
    {
        bCreateStartTail = false;
        o->NumTails = -1;
    }
    else if (Type == BITMAP_JOINT_ENERGY && (o->SubType == 55 || o->SubType == 56))
    {
        bCreateStartTail = false;
        o->NumTails = -1;
    }
    else if (Type == BITMAP_JOINT_ENERGY && o->SubType == 57)
    {
        bCreateStartTail = false;
        o->NumTails = -1;
    }

    if (Type == BITMAP_FLARE_FORCE && (o->SubType == 5 || o->SubType == 6 || o->SubType == 7))
    {
        bCreateStartTail = false;
    }

    if (Type == MODEL_SPEARSKILL && (o->SubType == 5 || o->SubType == 6 || o->SubType == 7))
    {
        bCreateStartTail = false;
    }

    if ((Type == BITMAP_FLASH) && (o->SubType == 6))
        bCreateStartTail = false;

    if (bCreateStartTail)
    {
        AngleMatrix(o->Angle, Matrix);
        Vector(-o->Scale * 0.5f, 0.f, 0.f, tailPosition);
        VectorRotate(tailPosition, Matrix, p);
        VectorAdd(o->Position, p, o->Tails[0][0]);
        Vector(o->Scale * 0.5f, 0.f, 0.f, tailPosition);
        VectorRotate(tailPosition, Matrix, p);
        VectorAdd(o->Position, p, o->Tails[0][1]);
        Vector(0.f, 0.f, -o->Scale * 0.5f, tailPosition);
        VectorRotate(tailPosition, Matrix, p);
        VectorAdd(o->Position, p, o->Tails[0][2]);
        Vector(0.f, 0.f, o->Scale * 0.5f, tailPosition);
        VectorRotate(tailPosition, Matrix, p);
        VectorAdd(o->Position, p, o->Tails[0][3]);
    }

    vec3_t BitePosition;
    switch (Type)
    {
    case BITMAP_SCOLPION_TAIL:
        o->Scale = Scale;
        o->LifeTime = 120;
        o->SetMaxTails(28);
        o->Velocity = 3.f;
        break;
    case BITMAP_JOINT_ENERGY:
        o->Scale = Scale;
        switch (o->SubType)
        {
        case 0:
        case 1:
        case 6:
        case 7:
        case 12:
        case 13:
        case 16:
        case 44:
        case 45:
        case 46:
            o->LifeTime = 120;
            o->SetMaxTails(8);
            o->Velocity = 3.f;
            if (o->SubType == 44)
            {
                o->LifeTime = 45;
                o->SetMaxTails(20);
            }
            if (o->SubType == 12 || o->SubType == 13 || o->SubType == 16 || o->SubType == 46)
            {
                if (WorldRandom() % 2)
                    o->MultiUse = 1;
                else
                    o->MultiUse = -1;
                o->Velocity = 10.f;
                o->Angle[0] = (float)(WorldRandom() % 45);
                o->Angle[1] = (float)(WorldRandom() % 45);
            }
            break;
        case 9:
            Vector(0.2f, 0.2f, 0.2f, o->Light);
            o->LifeTime = 120;
            o->SetMaxTails(8);
            o->Velocity = 3.f;
            break;
        case 17:
            o->Velocity = 0.f;
            o->LifeTime = 12;
            VectorCopy(o->Target->EyeLeft, o->Position);
            break;
        case 47: {
            o->Velocity = 0.f;
            o->LifeTime = 12;
            VectorCopy(o->Target->EyeRight, o->Position);
        }
        break;
        case 2:
        case 3:
        case 4:
        case 5:
        case 8:
        case 10:
        case 11:
        case 14:
        case 15:
        case 18:
        case 19:
        case 20:
        case 21:
        case 26:
        case 27:
        case 28:
        case 29:
        case 30:
        case 31:
        case 32:
        case 33:
            o->Velocity = 0.f;
            o->LifeTime = 999999999;
            if (o->SubType == 5)
                o->SetMaxTails(10);
            else if (o->SubType == 10 || o->SubType == 11)
                o->SetMaxTails(5);
            else if (o->SubType == 14 || o->SubType == 15)
                o->SetMaxTails(15);
            else
                o->SetMaxTails(20);
            if (o->SubType == 3 || o->SubType == 11 || o->SubType == 15)
            {
                o->SetMaxTails(20);
                VectorCopy(o->Target->EyeRight, o->Position);
            }
            else if (o->SubType == 18 || o->SubType == 28)
            {
                o->SetMaxTails(20);
                VectorCopy(o->Target->EyeLeft, o->Position)
            }
            else if (o->SubType == 19 || o->SubType == 29)
            {
                o->SetMaxTails(20);
                VectorCopy(o->Target->EyeRight, o->Position)
            }
            else if (o->SubType == 20 || o->SubType == 30)
            {
                o->SetMaxTails(20);
                VectorCopy(o->Target->EyeLeft2, o->Position)
            }
            else if (o->SubType == 21 || o->SubType == 31)
            {
                o->SetMaxTails(20);
                VectorCopy(o->Target->EyeRight2, o->Position)
            }
            else if (o->SubType == 26 || o->SubType == 32)
            {
                o->SetMaxTails(20);
                VectorCopy(o->Target->EyeLeft3, o->Position)
            }
            else if (o->SubType == 27 || o->SubType == 33)
            {
                o->SetMaxTails(20);
                VectorCopy(o->Target->EyeRight3, o->Position)
            }
            else
            {
                VectorCopy(o->Target->EyeLeft, o->Position);
            }
            o->TexType = BITMAP_JOINT_ENERGY;
            if ((o->SubType >= 28 && o->SubType <= 33))
            {
                o->TexType = BITMAP_FLARE;
            }
            break;
        case 22:
            o->Velocity = 0.f;
            o->LifeTime = 999999999;
            o->SetMaxTails(10);
            VectorCopy(o->Target->EyeLeft, o->Position);
            break;
        case 23:
            o->Velocity = 0.f;
            o->LifeTime = 999999999;
            o->SetMaxTails(10);
            VectorCopy(o->Target->EyeRight, o->Position);
            break;
        case 24:
            o->Velocity = 0.f;
            o->LifeTime = 999999999;
            o->SetMaxTails(10);
            VectorCopy(o->Target->EyeLeft, o->Position);
            break;
        case 25:
            o->Velocity = 0.f;
            o->LifeTime = 999999999;
            o->SetMaxTails(10);
            VectorCopy(o->Target->EyeRight, o->Position);
            break;
        case 40:
        case 41:
            o->Velocity = 0.f;
            o->LifeTime = 999999999;
            if (o->SubType == 5)
                o->SetMaxTails(10);
            VectorCopy(TargetPosition, o->Position);
            break;
        case 42: {
            o->LifeTime = 100;
            o->SetMaxTails(6);
            o->Velocity = 3.f;
        }
        break;
        case 43: {
            o->LifeTime = 100;
            o->SetMaxTails(6);
            o->Velocity = 3.f;
            o->Scale = Scale;
        }
        break;
        case 48:
        case 49:
        case 50:
        case 51:
        case 52:
        case 53:
            o->LifeTime = 999999999;
            o->SetMaxTails(3);
            Vector(0.5f, 0.5f, 0.9f, o->Light);
            break;
        case 54:
            o->Velocity = 0.f;
            o->LifeTime = 999999999;
            o->SetMaxTails(30);
            o->TexType = BITMAP_SPARK + 1;
            switch (o->PKKey)
            {
            case 0:
                VectorCopy(o->Target->EyeRight2, o->Position);
                break;
            case 1:
                VectorCopy(o->Target->EyeLeft2, o->Position);
                break;
            case 2:
                VectorCopy(o->Target->EyeRight3, o->Position);
                break;
            case 3:
                VectorCopy(o->Target->EyeLeft3, o->Position);
                break;
            }
            break;
        case 55:
        case 56: {
            o->Velocity = 0.f;
            o->SetMaxTails(8);
            o->LifeTime = 10;
            o->TexType = BITMAP_JOINT_ENERGY;

            switch (o->SubType)
            {
            case 55:
                VectorCopy(o->Target->EyeLeft, o->Position);
                break; //left
            case 56:
                VectorCopy(o->Target->EyeRight, o->Position);
                break; //rifht
            }
        }
        break;
        case 57: {
            o->Velocity = 0.f;
            o->SetMaxTails(static_cast<int>(iChaIndex));
            o->LifeTime = 10;
            o->TexType = BITMAP_JOINT_ENERGY;
        }
        break;
        }

        if (!vPriorColor)
        {
            switch (o->SubType)
            {
            case 0:
                Vector(0.4f, 0.3f, 0.2f, o->Light);
                break;
            case 1:
                Vector(0.1f, 0.1f, 0.5f, o->Light);
                break;
            case 2:
            case 3:
                Vector(0.5f, 0.1f, 1.f, o->Light);
                break;
            case 4:
                Vector(0.3f, 0.15f, 0.1f, o->Light);
                break;
            case 5:
                Vector(0.5f, 0.1f, 1.f, o->Light);
                break;
            case 6:
                Vector(0.4f, 0.3f, 0.2f, o->Light);
                break;
            case 8:
                Vector(1.f, 0.f, 0.5f, o->Light);
                break;
            case 10:
            case 11:
                Vector(1.f, 0.3f, 0.1f, o->Light);
                break;
            case 12:
            case 13:
                Vector(0.7f, 0.3f, 1.f, o->Light);
                break;
            case 14:
            case 15:
                Vector(1.0f, 0.1f, 0.1f, o->Light);
                break;
            case 16:
                Vector(0.4f, 0.2f, 0.4f, o->Light);
                break;
            case 17:
                Vector(0.8f, 0.2f, 1.0f, o->Light);
                break;
            case 18:
            case 19:
            case 20:
            case 21:
            case 26:
            case 27:
                Vector(0.8f, 0.5f, 1.0f, o->Light);
                break;
            case 42:
                Vector(0.0f, 0.0f, 0.0f, o->Light);
                break;
            case 43:
                Vector(2.5f, 0.0f, 0.0f, o->Light);
                break;
            case 46:
                Vector(0.1f, 0.25f, 0.1f, o->Light);
                break;
            case 47:
                Vector(0.9f, 0.f, 0.f, o->Light);
                break;
            }
        }

        break;

    case BITMAP_JOINT_HEALING:
        o->LifeTime = 12;
        o->Scale = Scale;
        o->SetMaxTails(2);
        o->Velocity = 0.f;

        if (o->SubType == 4)
        {
            o->LifeTime = 30;
            Vector((float)(WorldRandom() % 64 - 32), -10.f, 0.f, o->TargetPosition);
        }
        else if (o->SubType == 5)
        {
            o->Scale += WorldRandom() % 10 - 5;
            o->Velocity = (float)(WorldRandom() % 20 + 6);
            o->LifeTime = WorldRandom() % 8 + 8;
            o->SetMaxTails(8);
            Vector(1.f, 1.f, 1.f, o->Light);
        }
        else if (o->SubType == 6)
        {
            o->SetMaxTails(4);
            Vector(1.f, 1.f, 0.5f, o->Light);
        }
        else if (o->SubType == 7)
        {
            o->SetMaxTails(4);
            Vector(1.f, 1.f, 0.f, o->Light);
        }
        else if (o->SubType == 8)
        {
            o->LifeTime = 17;
            o->SetMaxTails(3);
            o->Light[0] = 0.5f;
            o->Light[1] = 0.5f;
            o->Light[2] = (WorldRandom() % 128) / 255.f + 0.5f;
            VectorCopy(TargetPosition, o->TargetPosition);
            o->TargetPosition[2] += 100.f;
        }
        else if (o->SubType == 9)
        {
            o->LifeTime = 90;
            o->SetMaxTails(20);
            o->NumTails = 0;
            o->MultiUse = (int)o->Angle[2];
            o->Velocity = 0.f;
            o->Direction[0] = 50.f;
            o->Light[0] = 1.f / (11.f);
            o->Light[1] = 0.5f / (11.f);
            o->Light[2] = 1.f / (11.f);
            VectorCopy(o->Target->Position, o->TargetPosition);
            VectorCopy(o->TargetPosition, o->Position);
        }
        else if (o->SubType == 10)
        {
            o->LifeTime = 80;
            o->SetMaxTails(20);
            o->NumTails = 0;
            o->MultiUse = (int)o->Angle[2];
            o->Velocity = 0.f;
            o->Direction[0] = 80.f;
            if (o->MultiUse == 225 || o->MultiUse == 405)
            {
                o->Collision = true;
            }
            else
            {
                o->Collision = false;
            }
            o->Light[0] = 1.f / (11.f);
            o->Light[1] = 0.5f / (11.f);
            o->Light[2] = 1.f / (11.f);
            VectorCopy(o->Target->Position, o->TargetPosition);
            VectorCopy(o->TargetPosition, o->Position);
        }
        else if (o->SubType == 13)
        {
            o->LifeTime = 17;
            o->SetMaxTails(10);
            o->Light[0] = 1.0f;
            o->Light[1] = 0.3f;
            o->Light[2] = 0.2f;
            VectorCopy(TargetPosition, o->TargetPosition);
            o->TargetPosition[2] += 200.f;
        }
        else if (o->SubType == 14)
        {
            o->LifeTime = 10;
            o->SetMaxTails(10);
            o->Light[0] = 0.8f;
            o->Light[1] = 1.0f;
            o->Light[2] = 0.8f;
            VectorCopy(TargetPosition, o->TargetPosition);
        }
        else if (o->SubType == 15)
        {
            o->LifeTime = 10;
            o->Scale = Scale;
            o->SetMaxTails(2);
            o->Velocity = 0.f;
        }
        else if (o->SubType == 16)
        {
            o->LifeTime = 10;
            o->Scale = Scale;
            o->SetMaxTails(2);
            o->Velocity = 0.f;
            o->RenderType = RENDER_TYPE_ALPHA_BLEND_MINUS;
        }
        else if (o->SubType == 17)
        {
            o->LifeTime = 17;
            o->SetMaxTails(3);
            o->Light[0] = (WorldRandom() % 128) / 255.f + 0.6f;
            o->Light[1] = 0.1f;
            o->Light[2] = 0.f;
            VectorCopy(TargetPosition, o->TargetPosition);
            o->TargetPosition[2] += 100.f;
        }
        break;

    case BITMAP_2LINE_GHOST: {
        if (o->SubType == 0)
        {
            o->RenderType = RENDER_TYPE_ALPHA_BLEND_MINUS;
            o->Velocity = 40.f + WorldRandom() % 20;
            if (Random.FpsCheck(2, 1.0))
                o->LifeTime = 67;
            else
                o->LifeTime = 75;
            o->SetMaxTails(26);
            o->Scale = Scale + (WorldRandom() % 200 + 1);
            o->Direction[0] = 0;
            o->m_sTargetIndex = 2;
            //				CreateEffect(MODEL_DESAIR,o->Position,o->Angle,o->Light,0, (OBJECT*)o);
            if (WorldRandom() % 3 < 2)
            {
                Vector(0.5f, 0.5f, 0.5f, o->Light);
                CreateEffect(MODEL_DESAIR, o->Position, o->Angle, o->Light, 0, NULL, -1, 0, 0, 0,
                             1.f, i);
            }
        }
        else if (o->SubType == 1)
        {
            o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            o->Scale = Scale;
            o->LifeTime = 25 + WorldRandom() % 10;
            o->SetMaxTails(15);
            o->Velocity = 20.f + WorldRandom() % 10;
            VectorCopy(o->Position, o->TargetPosition);

            vec3_t vAngle, vDir, vRandPos;
            Vector(0.f, 0.f, (float)(WorldRandom() % 360), vAngle);
            AngleMatrix(vAngle, Matrix);
            Vector(0.f, 300.0f, 0.f, vDir);
            VectorRotate(vDir, Matrix, vRandPos);

            o->Position[0] += vRandPos[0];
            o->Position[1] += vRandPos[1];
            o->Position[2] = (WorldRandom() % 100) + 200;
            o->TargetPosition[2] =
                o->Position[2] - (WorldRandom() % 100 - 100) * (Random.FpsCheck(2, 1.0) ? 1 : -1);

            VectorSubtract(o->TargetPosition, o->Position, o->Direction);

            o->Angle[2] = CreateAngle2D(o->Position, o->TargetPosition);
        }
    }
    break;
    case BITMAP_JOINT_SPIRIT:
    case BITMAP_JOINT_SPIRIT2:
        o->RenderType = RENDER_TYPE_ALPHA_BLEND_MINUS;
        switch (o->SubType)
        {
        case 0:
            o->Weapon = CharacterMachine->PacketSerial;
            o->Velocity = 70.f;
            o->LifeTime = 49;
            o->Scale = Scale;
            o->SetMaxTails(6);
            break;
        case 1:
            o->Velocity = 70.f;
            o->LifeTime = 49;
            o->Scale = Scale;
            o->SetMaxTails(6);
            PlayBuffer(SOUND_BRANDISH_SWORD03);
            break;
        case 2:
        case 21:
            o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            o->RenderFace = RENDER_FACE_TWO;
            o->Velocity = 50.f;
            o->LifeTime = 20;
            o->Scale = Scale;
            o->SetMaxTails(3);
            Vector(0.5f, 0.5f, 0.5f, o->Light);
            VectorCopy(o->Position, o->StartPosition);
            //PlayBuffer();
            break;
        case 3:
            o->Velocity = 140.f;
            o->LifeTime = 49;
            o->Scale = Scale;
            o->SetMaxTails(10);
            if (o->Type == BITMAP_JOINT_SPIRIT2)
            {
                Vector(1.f, 1.f, 1.f, o->Light);
            }
            else
            {
                o->RenderType = RENDER_TYPE_ALPHA_BLEND;
                Vector(1.0f, 0.5f, 0.1f, o->Light);
            }
            break;
        case 4: {
            o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            o->LifeTime = 0;
            o->Scale = Scale;
            o->SetMaxTails(10);
            Vector(0.f, 0.f, 0.f, o->Angle);
            Vector(0.3f, 0.3f, 1.f, o->Light);
            o->StartPosition[0] = (TargetPosition[0] - o->Position[0]) / o->MaxTails;
            o->StartPosition[1] = (TargetPosition[1] - o->Position[1]) / o->MaxTails;
            o->StartPosition[2] = (TargetPosition[2] - o->Position[2]) / o->MaxTails;
            VectorCopy(o->StartPosition, tailPosition);

            AngleMatrix(o->Angle, Matrix);

            for (int i = 0; i < (o->MaxTails - 1); i++)
            {
                if (o->Target == NULL)
                {
                    tailPosition[0] = o->StartPosition[0]; // + WorldRandom()%4-2;
                    tailPosition[1] = o->StartPosition[1]; // + WorldRandom()%4-2;
                }
                else
                {
                    tailPosition[0] = o->StartPosition[0]; // + WorldRandom()%8-4;
                    tailPosition[1] = o->StartPosition[1]; // + WorldRandom()%8-4;
                }
                VectorAdd(o->Position, tailPosition, o->Position);
                GameLogic::Effects::CreateTail(o, Matrix);
                o->Position[0] -= tailPosition[0];
                o->Position[0] += o->StartPosition[0];
                o->Position[1] -= tailPosition[1];
                o->Position[1] += o->StartPosition[1];
            }
            VectorCopy(o->TargetPosition, o->Position);
        }
        break;
        case 5:
            o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            o->Weapon = CharacterMachine->PacketSerial;
            o->Velocity = 30.f;
            o->LifeTime = 49;
            o->Scale = Scale;
            o->SetMaxTails(12);
            break;
        case 6:
            o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            o->RenderFace = RENDER_FACE_TWO;
            o->Velocity = 50.f;
            o->LifeTime = 20;
            o->Scale = Scale;
            o->SetMaxTails(5);
            o->PKKey = WorldRandom() % 5;

            if (o->PKKey)
            {
                o->m_bCreateTails = false;
            }

            switch (o->Skill)
            {
            case 0:
                Vector(0.3f, 0.3f, 1.f, o->Light);
                break;
            case 1:
                o->Velocity = 10.f;
                Vector(0.5f, 0.5f, 0.5f, o->Light);
                break;
            }
            VectorCopy(o->Position, o->StartPosition);
            //PlayBuffer();
            break;
        case 7:
            o->RenderFace = 0;
            o->Velocity = 10.f;
            o->LifeTime = 20;
            o->Scale = Scale;
            o->SetMaxTails(5);
            switch (o->Skill)
            {
            case 0:
                Vector(0.3f, 0.3f, 1.f, o->Light);
                break;
            case 1:
                o->Velocity = 10.f;
                Vector(0.5f, 0.5f, 0.5f, o->Light);
                break;
            }
            VectorCopy(o->Position, o->StartPosition);
            //PlayBuffer();
            break;
        case 8:
            o->LifeTime = 49;
            o->Scale = Scale;
            o->SetMaxTails(15);
            break;
        case 9:
            o->RenderFace = RENDER_FACE_TWO;
            o->Velocity = 10.f;
            o->LifeTime = 20;
            o->Scale = Scale;
            o->SetMaxTails(15);
            Vector(1.f, 1.f, 1.f, o->Light);
            Vector(-90.f, 0.f, 0.f, o->Angle);
            Vector(0.f, 0.f, 0.f, o->Direction);
            VectorCopy(o->Position, o->StartPosition);
            break;
        case 10:
            o->LifeTime = 20;
            o->Scale = Scale;
            o->SetMaxTails(6);
            break;
        case 11:
            o->Velocity = 0; //30.f;
            o->LifeTime = 50;
            o->Scale = Scale;
            o->SetMaxTails(1);
            break;
        case 12:
            o->Velocity = 0; //30.f;
            o->LifeTime = 30;
            o->Scale = Scale;
            o->SetMaxTails(1);
            break;
        case 13:
            if (o->Type == BITMAP_JOINT_SPIRIT)
            {
                o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            }
            o->Velocity = 40.f;
            o->LifeTime = 20;
            o->Scale = Scale;
            o->SetMaxTails(10);
            Vector(1.0f, 0.6f, 0.4f, o->Light);
            break;
        case 14:
            o->Velocity = 0;
            o->LifeTime = 10;
            o->Scale = Scale;
            o->SetMaxTails(1);
            o->Angle[0] = 20;
            break;
        case 15:
            o->Velocity = 0;
            o->LifeTime = 20;
            o->Scale = Scale;
            o->SetMaxTails(1);
            break;
        case 16:
            o->Velocity = 0;
            o->LifeTime = 50;
            o->Scale = Scale;
            o->SetMaxTails(1);
            break;
        case 17:
            o->Velocity = 0;
            o->LifeTime = 100;
            o->Scale = Scale;
            o->SetMaxTails(1);
            break;
        case 18:
            if (o->Type == BITMAP_JOINT_SPIRIT2)
            {
                o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            }
            o->Velocity = 50.f;
            o->LifeTime = 39;
            o->Scale = Scale;
            o->SetMaxTails(15);
            Vector(0.7f, 0.7f, 0.7f, o->Light);
            break;
        case 19:
            if (o->Type == BITMAP_JOINT_SPIRIT)
            {
                o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            }
            o->Velocity = 70.f;
            o->LifeTime = 49;
            o->Scale = Scale;
            o->SetMaxTails(9);
            Vector(0.1f, 0.5f, 0.2f, o->Light);
            break;
        case 20:
            if (o->Type == BITMAP_JOINT_SPIRIT2)
            {
                o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            }
            o->LifeTime = 49;
            o->Scale = Scale;
            o->SetMaxTails(15);
            break;
        case 22:
            o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            o->RenderFace = RENDER_FACE_TWO;
            o->Velocity = 60.f;
            o->LifeTime = 20;
            o->Scale = Scale;
            o->SetMaxTails(0);
            VectorCopy(o->Position, o->StartPosition);
            break;
        case 23:
            o->RenderFace = 0;
            o->Velocity = 10.f;
            o->LifeTime = 20;
            o->Scale = Scale;
            o->SetMaxTails(0);
            VectorCopy(o->Position, o->StartPosition);
            break;
        case 24: {
            o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            o->RenderFace = RENDER_FACE_TWO;
            o->Velocity = 10.f;
            o->LifeTime = 160;
            o->Scale = Scale;
            o->SetMaxTails(40);
            Vector(0.0f, 0.0f, 0.0f, o->Light);

            o->Position[0] += (float)(WorldRandom() % 300 - 150);
            o->Position[1] += (float)(WorldRandom() % 300 - 150);

            float fRargleScale = o->Scale / 70.f;
            if (0.9f <= fRargleScale)
            {
                vec3_t vLight;
                Vector(0.0f, 0.0f, 0.1f, vLight);
                CreateEffect(MODEL_SUMMONER_SUMMON_LAGUL, o->Position, o->Angle, vLight, 1,
                             (OBJECT *)o, -1, 0, 0, 0, fRargleScale);
            }
        }
        break;
        case 25: {
            o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            o->TexType = BITMAP_SHINY;
            o->Velocity = 9.f;
            o->LifeTime = 26;
            o->Scale = Scale;
            o->SetMaxTails(30);
            o->Skill = SkillIndex;
            Vector(0.9f, 0.8f, 1.f, o->Light);
            Vector(-90.f, 0.f, 0.f, o->Angle);
            Vector(0.f, 0.f, 0.f, o->Direction);
            VectorCopy(o->Position, o->StartPosition);
        }
        break;
        }
        break;
    case BITMAP_JOINT_LASER:
        o->bTileMapping = true;
        o->Velocity = 70.f;
        o->LifeTime = 49;
        o->Scale = Scale;
        o->SetMaxTails(6);
        break;
    case BITMAP_JOINT_SPARK:
        switch (o->SubType)
        {
        case 0:
            o->Scale = 2.f;
            o->Velocity = (float)(WorldRandom() % 20 + 6);
            o->LifeTime = WorldRandom() % 8 + 8;
            o->SetMaxTails(2);
            Vector(1.f, 1.f, 1.f, o->Light);
            break;
        case 1:
            o->Scale = 2.f;
            o->Velocity = (float)(WorldRandom() % 20 + 16);
            o->LifeTime = WorldRandom() % 4 + 4;
            o->SetMaxTails(2);
            Vector(1.f, 1.f, 1.f, o->Light);
            break;
        case 2:
            o->Scale = 4.f;
            o->Velocity = 30.0f;
            o->LifeTime = WorldRandom() % 4 + 4;
            o->SetMaxTails(2);
            Vector(0.3f, 0.3f, 1.f, o->Light);
            break;
        case 3:
            o->Scale = 2.f;
            o->Velocity = (float)(WorldRandom() % 20 + 6);
            o->LifeTime = WorldRandom() % 8 + 8;
            o->SetMaxTails(2);
            Vector(1.f, 1.f, 1.f, o->Light);
            break;
        case 4:
            o->Scale = Scale * 2 + 4.f;
            o->Velocity = (float)(WorldRandom() % 5 + 4);
            o->LifeTime = WorldRandom() % 10 + 8;
            o->SetMaxTails(4);
            Vector(1.f, 0.2f, 0.2f, o->Light);
            break;
        case 5:
            o->Scale = 2.f;
            o->Velocity = (float)(WorldRandom() % 20 + 6);
            o->LifeTime = WorldRandom() % 8 + 8;
            o->SetMaxTails(2);
            Vector(0.7f, 0.1f, 0.1f, o->Light);
            break;
        }
        break;

    case BITMAP_SMOKE:
        o->Scale = Scale;
        Vector(1.f, 1.f, 1.f, o->Light);
        if (o->SubType == 0)
        {
            o->SetMaxTails(20);
            o->LifeTime = 20;
            o->Velocity = 0.f;
            o->TexType = BITMAP_FLARE;
        }
        else if (o->SubType == 1)
        {
            o->SetMaxTails(15);
            o->LifeTime = 15;
            o->Velocity = 0.f;
        }
        else if (o->SubType == 2)
        {
            o->SetMaxTails(25);
            o->LifeTime = 22;
            o->Velocity = 0.f;
            o->TexType = BITMAP_FLARE;
            Vector(0.1f, 0.3f, 1.0f, o->Light);
        }
        break;
    case MODEL_FENRIR_SKILL_THUNDER:
        o->Scale = Scale;
        o->SetMaxTails(50);
        o->Velocity = 50.f;
        o->LifeTime = 20;
        o->bTileMapping = true;

        if (o->SubType == 0)
        {
            o->TexType = BITMAP_JOINT_THUNDER;
            Vector(0.7f, 1.0f, 0.7f, o->Light);
        }
        else if (o->SubType == 1)
        {
            o->TexType = BITMAP_JOINT_THUNDER;
            Vector(1.0f, 0.6f, 0.6f, o->Light);
        }
        else if (o->SubType == 2)
        {
            o->TexType = BITMAP_JOINT_THUNDER;
            Vector(0.7f, 0.7f, 1.0f, o->Light);
        }
        else if (o->SubType == 3)
        {
            o->TexType = BITMAP_JOINT_THUNDER;
            Vector(0.9f, 0.9f, 0.3f, o->Light);
        }
        else if (o->SubType == 4)
        {
            o->TexType = BITMAP_FLASH;
            Vector(0.1f, 0.8f, 0.1f, o->Light);
        }
        else if (o->SubType == 5)
        {
            o->TexType = BITMAP_FLASH;
            Vector(1.0f, 0.3f, 0.2f, o->Light);
        }
        else if (o->SubType == 6)
        {
            o->TexType = BITMAP_FLASH;
            Vector(0.2f, 0.3f, 1.0f, o->Light);
        }
        else if (o->SubType == 7)
        {
            o->TexType = BITMAP_FLASH;
            Vector(0.8f, 0.8f, 0.1f, o->Light);
        }
        break;
    case BITMAP_JOINT_LASER + 1:
        o->bTileMapping = true;
    case BITMAP_BLUR + 1:
        o->Scale = 60.f;
        o->Velocity = 40.f;
        o->SetMaxTails(50);
        o->LifeTime = 2;
        if (o->SubType == 0)
        {
            Vector(1.f, 1.f, 1.f, o->Light);
        }
        else if (o->SubType == 3)
        {
            o->LifeTime = 20;
            o->Velocity = 0.f;
            Vector(1.f, 1.f, 1.f, o->Light);
        }
        else
        {
            if (o->Type == BITMAP_JOINT_LASER + 1)
            {
                Vector(.35f, .1f, 1.f, o->Light);
            }
            else
            {
                Vector(0.f, 0.3f, 1.f, o->Light);
            }
        }
        break;
    case BITMAP_JOINT_THUNDER:
        o->Scale = Scale;
        o->SetMaxTails(50);
        o->Velocity = 50.f;
        switch (o->SubType)
        {
        case 0:
        case 20:
            o->LifeTime = 2;
            break;

        case 1:
            o->Velocity = 20.f + (float)(WorldRandom() % 10);
            o->LifeTime = WorldRandom() % 8 + 8;
            break;

        case 2:
            o->LifeTime = 2;
            Vector(1.f, 0.1f, 0.f, o->Light);
            Vector(0.f, -150.f, 0.f, BitePosition); //42
            Models[MODEL_PLAYER].TransformPosition(o->Target->BoneTransform[33], BitePosition,
                                                   o->TargetPosition, true);
            break;

        case 3:
            o->Velocity = SkillIndex;
            o->PKKey = -1;
            o->LifeTime = PKKey;
            o->SetMaxTails(static_cast<int>(SkillIndex));
            Vector(0.5f, 0.5f, 1.f, o->Light);
            VectorCopy(o->Position, o->StartPosition);
            break;

        case 4:
            o->Velocity = 60.f;
            o->LifeTime = 20;
            o->SetMaxTails(10);
            Vector(0.f, 0.f, 0.f, o->Direction);
            Vector(0.5f, 0.5f, 1.f, o->Light);
            VectorCopy(TargetPosition, o->TargetPosition);
            VectorCopy(o->Position, o->StartPosition);
            break;

        case 5:
            o->Velocity = -60.f;
            o->LifeTime = 20;
            o->SetMaxTails(10);
            Vector(0.f, 0.f, 0.f, o->Direction);
            Vector(0.5f, 0.5f, 1.f, o->Light);
            VectorCopy(TargetPosition, o->TargetPosition);
            VectorCopy(o->Position, o->StartPosition);
            o->TargetPosition[2] += 600.f;
            break;

        case 6:
            o->LifeTime = WorldRandom() % 20 + 6;
            o->m_bCreateTails = false;
            o->Velocity = 15.f + (float)(WorldRandom() % 10);

            o->Light[0] = (WorldRandom() % 10) / 15.f + 0.1f;
            o->Light[1] = o->Light[0];
            o->Light[2] = o->Light[0];
            VectorCopy(o->Position, o->StartPosition);
            break;

        case 7:
            o->bTileMapping = TRUE;
            o->Velocity = 5.f;
            o->SetMaxTails(3);
            o->LifeTime = 1;
            Vector(0.5f, 0.5f, 1.f, o->Light);
            VectorCopy(o->Position, o->StartPosition);

            ::MoveHumming(o->Position, o->Angle, o->TargetPosition, 360.f);
            break;

        case 8:
            o->Scale = 5.f;
            o->Velocity = (float)(WorldRandom() % 10 + 6);
            o->LifeTime = WorldRandom() % 8 + 8;
            o->SetMaxTails(2);
            Vector(1.f, 1.f, 1.f, o->Light);
            break;

        case 9: {
            o->Velocity = 80.f + (float)(WorldRandom() % 20);
            o->LifeTime = 30;

            o->Light[0] = (WorldRandom() % 10) / 15.f + 0.1f;
            o->Light[1] = o->Light[0];
            o->Light[2] = o->Light[0];
            VectorCopy(o->Position, o->StartPosition);
        }
        break;

        case 10: {
            o->bTileMapping = TRUE;
            o->LifeTime = 0;
            o->Scale = Scale;
            o->SetMaxTails(10);
            Vector(0.f, 0.f, 0.f, o->Angle);
            Vector(0.3f, 0.3f, 1.f, o->Light);
            o->StartPosition[0] = (TargetPosition[0] - o->Position[0]) / o->MaxTails;
            o->StartPosition[1] = (TargetPosition[1] - o->Position[1]) / o->MaxTails;
            o->StartPosition[2] = (TargetPosition[2] - o->Position[2]) / o->MaxTails;
            VectorCopy(o->StartPosition, tailPosition);

            AngleMatrix(o->Angle, Matrix);

            for (int i = 0; i < (o->MaxTails - 1); i++)
            {
                if (o->Target == NULL)
                {
                    tailPosition[0] = o->StartPosition[0] + WorldRandom() % 8 - 4;
                    tailPosition[1] = o->StartPosition[1] + WorldRandom() % 8 - 4;
                }
                else
                {
                    tailPosition[0] = o->StartPosition[0] + WorldRandom() % 16 - 8;
                    tailPosition[1] = o->StartPosition[1] + WorldRandom() % 16 - 8;
                }
                VectorAdd(o->Position, tailPosition, o->Position);
                GameLogic::Effects::CreateTail(o, Matrix);
                o->Position[0] -= tailPosition[0];
                o->Position[0] += o->StartPosition[0];
                o->Position[1] -= tailPosition[1];
                o->Position[1] += o->StartPosition[1];
            }
            VectorCopy(o->TargetPosition, o->Position);
            //                        CreateTail ( o, Matrix );
        }
        break;
        case 11:
            Vector(1.0f, 0.5f, 0.1f, o->Light);
            o->LifeTime = 2;
            break;
        case 12:
            Vector(1.0f, 0.1f, 0.1f, o->Light);
            o->LifeTime = 10;
            break;
        case 13:
            Vector(1.0f, 0.1f, 0.1f, o->Light);
            o->LifeTime = 2;
            o->SubType = 11;
            break;
        case 14: {
            o->LifeTime = 0;
            o->Scale = Scale;
            o->SetMaxTails(10);
            Vector(0.f, 0.f, 0.f, o->Angle);
            Vector(0.3f, 0.3f, 1.f, o->Light);
            o->StartPosition[0] = (TargetPosition[0] - o->Position[0]) / o->MaxTails;
            o->StartPosition[1] = (TargetPosition[1] - o->Position[1]) / o->MaxTails;
            o->StartPosition[2] = (TargetPosition[2] - o->Position[2]) / o->MaxTails;
            VectorCopy(o->StartPosition, tailPosition);

            AngleMatrix(o->Angle, Matrix);

            for (int i = 0; i < (o->MaxTails - 1); i++)
            {
                if (o->Target == NULL)
                {
                    tailPosition[0] = o->StartPosition[0];
                    tailPosition[1] = o->StartPosition[1];
                }
                else
                {
                    tailPosition[0] = o->StartPosition[0];
                    tailPosition[1] = o->StartPosition[1];
                }
                VectorAdd(o->Position, tailPosition, o->Position);
                GameLogic::Effects::CreateTail(o, Matrix);
                o->Position[0] -= tailPosition[0];
                o->Position[0] += o->StartPosition[0];
                o->Position[1] -= tailPosition[1];
                o->Position[1] += o->StartPosition[1];
            }
            VectorCopy(o->TargetPosition, o->Position);
            //                        CreateTail ( o, Matrix );
        }
        break;
        case 15:
            o->RenderFace = 0;
            o->LifeTime = 80;
            o->SetMaxTails(0);
            o->MultiUse = 0;

            for (int j = 0; j < CharactersClient.Size(); j++)
            {
                if (!CharactersClient.IsValidIndex(j))
                    continue;
                CHARACTER *tc = &CharactersClient[j];
                OBJECT *to = &tc->Object;

                float dx = o->Position[0] - to->Position[0];
                float dy = o->Position[1] - to->Position[1];
                float Distance = sqrtf(dx * dx + dy * dy);
                if (Distance <= 400 && to->Live && tc->Dead == 0 && to->Kind == KIND_MONSTER &&
                    to->Visible && to != Target)
                {
                    o->TargetIndex[(int)o->MultiUse] = j;
                    o->MultiUse += 1.f;
                }

                if (o->MultiUse >= std::size(o->TargetIndex))
                    break;
            }
            o->Weapon = o->MultiUse * 15;
            o->MultiUse = 0;

            VectorCopy(TargetPosition, o->StartPosition);
            o->StartPosition[2] += 150.f;
            break;
        case 16:
            o->Velocity = 20.f + (float)(WorldRandom() % 10);
            o->LifeTime = WorldRandom() % 2 + 2;
            break;
        case 17:
            o->Velocity = 20.f + (float)(WorldRandom() % 10);
            o->LifeTime = 10;
            break;
        case 18:
            o->Velocity = 80.f;
            o->SetMaxTails(5);
            o->LifeTime = 10;
            o->Light[0] = o->Light[1] = o->Light[2] = 1.0f;
            VectorCopy(o->Position, o->StartPosition);
            break;
        case 19: {
            o->SetMaxTails(30);
            o->LifeTime = 2;
            o->Velocity = o->Scale * 3.0f;
            VectorCopy(o->Position, o->StartPosition);
        }
        break;
        case 21:
            o->LifeTime = 2;
            Vector(1.f, 0.5f, 0.4f, o->Light);
            Vector(0.f, -150.f, 0.f, BitePosition); //42
            Models[MODEL_PLAYER].TransformPosition(o->Target->BoneTransform[33], BitePosition,
                                                   o->TargetPosition, true);
            break;
            // ChainLighting
        case 22:
        case 23:
        case 24:
            o->LifeTime = 15;
            o->Scale = Scale + (WorldRandom() % 50 + 50) * 0.1f;
            o->SetMaxTails(30);
            o->Velocity = 20.f;
            break;
        case 25: {
            o->LifeTime = 20;
            o->SetMaxTails(8);
            //o->Velocity = 50.f;
            o->Velocity = 20.f + (float)(WorldRandom() % 10);
        }
        break;
        case 26:
            o->Velocity = 20.f + (float)(WorldRandom() % 10);
            o->LifeTime = WorldRandom() % 8 + 8;
            //o->SetMaxTails(10);
            break;
        case 27:
        case 28: {
            o->LifeTime = 2;
            VectorCopy(vPriorColor, o->Light);
        }
        break;
        case 33:
            Vector(0.3f, 0.3f, 1.0f, o->Light);
            o->Velocity = 20.f + (float)(WorldRandom() % 10);
            o->LifeTime = WorldRandom() % 2 + 2;
            break;
        }
        break;

    case BITMAP_JOINT_THUNDER + 1:
        o->Scale = Scale;
        o->TexType = BITMAP_JOINT_THUNDER;

        if (o->SubType == 0)
        {
            o->SetMaxTails(50);

            o->Velocity = 80.f + (float)(WorldRandom() % 20);
            o->LifeTime = 20;

            o->Light[2] = (float)o->MaxTails;
            VectorCopy(o->Position, o->StartPosition);
        }
        else if (o->SubType == 1 || o->SubType == 2 || o->SubType == 3 || o->SubType == 5 ||
                 o->SubType == 6 || o->SubType == 7)
        {
            o->SetMaxTails(50);
            o->LifeTime = 20;
            o->Position[0] += (float)(WorldRandom() % 10 - 5);
            o->Position[1] += WorldRandom() % 10 - 5;

            if (o->SubType == 7)
            {
                o->m_bCreateTails = false;
                o->Position[2] += 1050.0f;
            }
            else
            {
                o->Position[2] += 800.f;
            }
            Vector(0.f, 0.f, 0.f, o->Angle);
            VectorCopy(TargetPosition, o->TargetPosition);

            if (o->SubType == 1)
            {
                o->m_bCreateTails = false;
            }
            VectorCopy(o->Position, o->StartPosition);

            if (o->SubType == 5)
            {
                Vector(1.f, 0.5f, 0.2f, o->Light);
            }
            else if (o->SubType == 6)
            {
                o->Velocity = 0.3f;
                Vector(1.0f, 1.0f, 1.0f, o->Light);
                o->Position[2] += 700.f;
            }
        }
        else if (o->SubType == 4)
        {
            o->SetMaxTails(50);
            o->LifeTime = 20;
            o->m_bCreateTails = false;
            o->byOnlyOneRender = 2;
            o->TargetIndex[0] = (int)(o->MaxTails / 1.5f);
            o->TargetIndex[1] = o->MaxTails - 1;
            o->StartPosition[0] = (float)(1.f / o->MaxTails);
            o->StartPosition[1] = (float)(1.f / (o->MaxTails - o->TargetIndex[0] - 1));
            Vector(0.f, 0.f, 0.f, o->Angle);
            Vector(1.f, 0.8f, 1.f, o->Light);
            VectorCopy(TargetPosition, o->TargetPosition);
            o->TargetPosition[2] += 100.f;
            Angle[2] = CreateAngle2D(o->Position, o->TargetPosition);
        }
        else if (o->SubType == 8)
        {
            o->SetMaxTails(50);
            o->LifeTime = 20;
            o->m_bCreateTails = false;

            o->Position[0] += (float)(WorldRandom() % 10 - 5);
            o->Position[1] += WorldRandom() % 10 - 5;
            o->Position[2] += 1100.0f;

            Vector(0.f, 0.f, 0.f, o->Angle);
            VectorCopy(TargetPosition, o->TargetPosition);
            VectorCopy(o->Position, o->StartPosition);
        }
        else if (o->SubType == 9)
        {
            o->SetMaxTails(50);
            o->LifeTime = 20;
            o->m_bCreateTails = false;

            o->Position[0] -= 50.0f;

            Vector(0.f, 0.f, 0.f, o->Angle);
            VectorCopy(TargetPosition, o->TargetPosition);
            VectorCopy(o->Position, o->StartPosition);
        }
        else if (o->SubType == 10)
        {
            o->SetMaxTails(50);
            o->LifeTime = 20;
            o->m_bCreateTails = false;

            o->Position[1] -= 350.0f;

            Vector(0.f, 0.f, 0.f, o->Angle);
            VectorCopy(TargetPosition, o->TargetPosition);
            VectorCopy(o->Position, o->StartPosition);
        }
        else if (o->SubType == 11)
        {
            o->Velocity = 80.f + (float)(WorldRandom() % 20);
            o->Light[2] += 0.2f;
            o->LifeTime = 5;
            o->SetMaxTails(30);
            VectorCopy(o->Position, o->StartPosition);
            VectorCopy(TargetPosition, o->TargetPosition);
        }
        else if (o->SubType == 12)
        {
            o->Velocity = 80.f + (float)(WorldRandom() % 20);
            o->Light[2] = (float)o->MaxTails;
            o->SetMaxTails(30);
            o->LifeTime = 4;
            o->m_bCreateTails = false;
            VectorCopy(o->Position, o->StartPosition);

            vec3_t p1, p2;
            Vector(0.f, o->Scale / 1.0f, 0.f, p1);
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(p1, Matrix, p2);

            Vector(p2[0] * o->Scale / 2.0f, p2[1] * o->Scale / 2.0f, p2[2] * o->Scale / 2.0f, p2);
            VectorAdd(o->Position, p2, o->Position);
            VectorCopy(o->Position, o->StartPosition);

            int iScale = 1;
            iScale = (int)(o->Scale / 4.0f);
            o->TargetPosition[0] = TargetPosition[0] + WorldRandom() % (iScale * 2) - iScale;
            o->TargetPosition[1] = TargetPosition[1] + WorldRandom() % (iScale * 2) - iScale;
            o->TargetPosition[2] = TargetPosition[2] + WorldRandom() % (iScale * 2) - iScale;
        }
        break;
    case BITMAP_JOINT_FIRE:
        o->Scale = 70.f;
        o->Velocity = 50.f;
        o->SetMaxTails(8);
        o->LifeTime = 20;
        Vector(0.f, 0.f, 130.f, BitePosition);
        VectorAdd(o->TargetPosition, BitePosition, o->TargetPosition);
        //PlayBuffer(SOUND_MAGIC_FIRE01);
        break;
    case BITMAP_SPARK + 1:
        o->LifeTime = 100;
        o->SetMaxTails(20);
        o->Scale = 10.f;
        if (o->SubType == 0)
        {
            o->Direction[2] = (float)(WorldRandom() % 20 + 35);
            o->Scale = WorldRandom() % 20 + 20.f;
            o->LifeTime = 25;
        }
        else if (o->SubType == 1)
        {
            o->Scale = Scale;
            o->Velocity = (float)(WorldRandom() % 55 + 14);
            o->LifeTime = WorldRandom() % 10 + 8;
            o->SetMaxTails(4);
        }
        Vector(1.f, 1.f, 1.f, o->Light);
        VectorCopy(o->Position, o->TargetPosition);
        break;
    case MODEL_SPEARSKILL:
        VectorCopy(o->Target->Position, o->TargetPosition);
        switch (o->SubType)
        {
        case 0:
            Vector(1.f, 1.f, 1.f, o->Light);
            o->LifeTime = 999999; //30 * 24;
            o->SetMaxTails(30);
            o->TexType = BITMAP_FLARE_BLUE;
            break;
        case 4:
        case 9:
            Vector(.4f, .8f, .2f, o->Light);
            o->LifeTime = 10000; //30 * 24;
            o->SetMaxTails(30);
            o->TexType = BITMAP_FLARE_BLUE;
            break;
        case 10:
        case 11:
            Vector(1.f, 0.6f, 0.6f, o->Light);
            o->LifeTime = 10000;
            o->SetMaxTails(30);
            o->TexType = BITMAP_LUCKY_SEAL_EFFECT;
            break;
        case 1:
            Vector(.2f, .2f, .2f, o->Light);
            o->LifeTime = 999999;
            o->SetMaxTails(30);
            o->TexType = BITMAP_JOINT_SPIRIT;
            break;
        case 2:
            VectorCopy(TargetPosition, o->TargetPosition);
            Vector(1.0f, .3f, .3f, o->Light);
            o->LifeTime = 20;
            o->SetMaxTails(5);
            o->TexType = BITMAP_FLARE_FORCE;
            break;
        case 3:
            if (o->Target != NULL)
            {
                VectorCopy(o->Target->Light, o->Light);
            }
            else
            {
                Vector(0.5f, 0.f, 0.f, o->Light);
            }
            Vector(-90.f, 0.f, 0.f, o->Angle);
            Vector(0.f, (float)(WorldRandom() % 500), 0.f, o->Direction);
            o->Velocity = WorldRandom() % 5 + 5.f;
            o->LifeTime = 999999;
            o->SetMaxTails(30);
            o->TexType = BITMAP_JOINT_SPIRIT;
            break;
        case 5:
        case 6:
        case 7:
            o->RenderFace = RENDER_FACE_ONE;
            o->LifeTime = 60;
            o->SetMaxTails(30);
            o->Weapon = 0;
            o->m_bCreateTails = false;

            if (o->SubType == 5)
            {
                Vector(1.f, 1.f, 0.8f, o->Light);
                o->TexType = BITMAP_FLARE + 1;
            }
            else if (o->SubType == 6)
            {
                Vector(1.f, 0.8f, 1.f, o->Light);
                o->TexType = BITMAP_FLARE + 1;
            }
            else if (o->SubType == 7)
            {
                Vector(0.8f, 1.0f, 1.0f, o->Light);
                o->TexType = BITMAP_FLARE + 1;
            }
            Vector(0.f, 800.f, 0.f, o->Direction);
            VectorCopy(TargetPosition, o->StartPosition);

            AngleMatrix(o->Angle, Matrix);
            VectorRotate(o->Direction, Matrix, tailPosition);
            VectorAdd(o->StartPosition, tailPosition, o->Position);

            o->NumTails = -1;
            GameLogic::Effects::CreateTail(o, Matrix);
            break;
        case 8:
            o->RenderFace = RENDER_FACE_ONE;
            o->LifeTime = 40;
            o->SetMaxTails(30);
            o->m_bCreateTails = false;

            Vector(0.5f, 0.5f, 0.5f, o->Light);
            o->TexType = BITMAP_LIGHT;
            Vector(0.f, -40.f, 0.f, o->Direction);
            VectorCopy(TargetPosition, o->StartPosition);

            AngleMatrix(o->Angle, Matrix);
            VectorRotate(o->Direction, Matrix, tailPosition);
            VectorAdd(o->StartPosition, tailPosition, o->Position);
            break;
        case 14:
            VectorCopy(vPriorColor, o->Light);
            o->LifeTime = 100;
            o->SetMaxTails(30);
            o->TexType = BITMAP_LIGHT;
            break;
        case 15:
            Vector(1.0f, 1.0f, 1.0f, o->Light);
            o->LifeTime = 100;
            o->SetMaxTails(20);
            o->TexType = BITMAP_JOINT_SPIRIT;
            VectorCopy(o->Target->Owner->Position, o->StartPosition);
            break;
        case 16:
            Vector(1.f, 1.f, 0.f, o->Light);
            o->LifeTime = 100;
            o->SetMaxTails(30);
            o->TexType = BITMAP_LIGHT;
            break;
        case 17:
            Vector(0.7f, 0.2f, 1.f, o->Light);
            o->LifeTime = 100;
            o->SetMaxTails(20);
            o->TexType = BITMAP_JOINT_SPIRIT;
            VectorCopy(o->Target->Owner->Position, o->StartPosition);
            break;
        }
        o->Scale = Scale;
        break;
    case BITMAP_FLARE:
    case BITMAP_FLARE_BLUE:
        o->LifeTime = 100;
        o->SetMaxTails(20);
        o->Scale = 10.f;
        if (o->SubType == 0 || o->SubType == 18)
        {
            if (o->Type == BITMAP_FLARE && o->SubType == 18)
            {
                o->RenderType = RENDER_TYPE_ALPHA_BLEND_OTHER;
            }

            o->Scale = Scale;
            o->Direction[2] = (WorldRandom() % 150) / 100.f;
            o->Direction[1] = (float)(WorldRandom() % 500 - 250);
            o->Velocity = 40.f;

            if (o->Scale > 10.f)
            {
                o->LifeTime = 50;
                o->Direction[2] = (WorldRandom() % 250 + 200) / 100.f;
            }

            VectorCopy(o->Target->Light, o->Light);
        }
        else if (o->SubType == 20)
        {
            if (o->Target == NULL)
            {
                Joints.Retire(*o);
                break;
            }
            o->LifeTime = 30;
            o->SetMaxTails(10);
            o->Scale = Scale;
            o->TexType = BITMAP_FIRECRACKER;

            Vector(0.8f, 0.3f, 1.f, o->Light);

            BMD *b = &Models[o->Target->Type];
            vec3_t p;
            Vector(0.0f, 0.0f, 0.0f, p);
            b->TransformPosition(o->Target->BoneTransform[33], p, o->Position, true);
        }
        else if (o->SubType == 10)
        {
            o->Scale = Scale;
            o->Direction[2] = (WorldRandom() % 150) / 100.f;
            o->Direction[1] = (float)(WorldRandom() % 500 - 250);
            if (o->SubType == 25)
                o->Velocity = 140.f;
            else
                o->Velocity = 40.f;

            if (o->Scale > 10.f)
            {
                o->LifeTime = 50;
                o->Direction[2] = (WorldRandom() % 250 + 200) / 100.f;
            }

            VectorCopy(o->Target->Light, o->Light);
        }
        else if (o->SubType == 14 || o->SubType == 15)
        {
            o->RenderType = RENDER_TYPE_ALPHA_BLEND_OTHER;
            o->Scale = Scale;
            o->TexType = BITMAP_JOINT_SPIRIT;
            o->Direction[2] = (WorldRandom() % 150) / 100.f;
            o->Direction[1] = (float)(WorldRandom() % 500 - 250);
            o->Velocity = 30.f;

            //VectorCopy ( o->Target->Light, o->Light );
            Vector(0.2f, 0.4f, 0.8f, o->Light);

            if (o->Scale > 10.f)
            {
                o->Direction[2] = (WorldRandom() % 250 + 150) / 100.f;
            }
            o->LifeTime = 20;

            o->Position[2] += WorldRandom() % 25;
        }
        else if (o->SubType == 16)
        {
            o->LifeTime = 60;
            o->m_bCreateTails = false;
            Vector(0.1f, 0.1f, 0.1f, o->Light);
            VectorCopy(TargetPosition, o->TargetPosition);
            o->Scale = Scale;
            for (int j = 0; j < o->MaxTails; ++j)
            {
                o->Position[2] += 50.f;

                GameLogic::Effects::CreateTail(o, Matrix);
            }
        }
        else if (o->SubType == 17)
        {
            o->Position[0] += WorldRandom() % 500 - 500;
            o->Position[1] += WorldRandom() % 500 - 500;

            o->Direction[0] = 0.f;
            o->Direction[1] = 0.f;
            o->Direction[2] = 0.f; //(float)(WorldRandom()%5+2);
            o->Velocity = (WorldRandom() % 200 + 10) / 25.f;
            o->Scale = Scale;
            o->LifeTime = 20 + WorldRandom() % 10;

            Vector(0.f, 0.f, 1.f, o->Light);
        }
        else if (o->SubType == 2 || o->SubType == 24 || o->SubType == 50 || o->SubType == 51)
        {
            if (o->SubType == 24)
                o->TexType = BITMAP_FLARE_RED;
            else if (o->SubType == 50 || o->SubType == 51)
                o->TexType = BITMAP_FLARE_BLUE;
            o->Direction[2] = (float)(WorldRandom() % 20 + 35);
            o->Scale = Scale;
            o->LifeTime = 25 + WorldRandom() % 50;

            Vector(1.f, 1.f, 1.f, o->Light);
        }
#ifdef GUILD_WAR_EVENT
        else if (o->SubType == 21)
        {
            o->Direction[2] = (float)(WorldRandom() % 20 + 35);
            o->Scale = Scale;
            o->LifeTime = 40 + WorldRandom() % 10;
            o->Light[0] = 0.7f;
            o->Light[1] = 0.5f + (float)(WorldRandom() % 127) / 255.f;
            o->Light[2] = 0.5f + (float)(WorldRandom() % 127) / 255.f;
        }
        else if (o->SubType == 22)
        {
            VectorCopy(o->Target->Position, o->StartPosition);
            o->Direction[2] = (float)(WorldRandom() % 20 + 35);
            o->Scale = Scale;
            o->LifeTime = 40 + WorldRandom() % 10;
            o->Light[0] = 0.5f + (float)(WorldRandom() % 127) / 255.f;
            o->Light[1] = 0.2f + (float)(WorldRandom() % 204) / 255.f;
            o->Light[2] = 0.2f + (float)(WorldRandom() % 204) / 255.f;
        }
        else if (o->SubType == 40)
        {
            o->Scale = Scale;
            o->LifeTime = 50;
            o->SetMaxTails(50);
            Vector(1.f, 1.f, 1.f, o->Light);

            o->Direction[0] = -2.0f * (float)sinf(-o->Angle[2] * Q_PI / 180.0f);
            o->Direction[1] = -2.0f * (float)cosf(-o->Angle[2] * Q_PI / 180.0f);
            o->Direction[2] = 0.f;
        }
#endif //GUILD_WAR_EVENT

        else if (o->SubType == 41)
        {
            o->Scale = Scale;
            o->LifeTime = 40;
            o->SetMaxTails(50);
            float rbias = (float)(WorldRandom() % 300) / 1000;
            float gbias = (float)(WorldRandom() % 300) / 1000;
            Vector(1.f - rbias, 1.f - gbias, 1.f, o->Light);
            o->Direction[2] = (float)(WorldRandom() % 5 + 5);
        }
        else if (o->SubType == 42)
        {
            o->Direction[1] = -15.f;
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(o->Direction, Matrix, tailPosition);
            VectorAdd(o->Position, tailPosition, o->Position);
            VectorCopy(o->Position, o->StartPosition);
            o->Direction[1] = -50.f;
            o->m_bCreateTails = false;
            o->Scale = Scale;
            Vector(1.f, 1.f, 1.f, o->Light);
        }
        else if (o->SubType == 19)
        {
            o->Direction[2] = -(float)(WorldRandom() % 20 + 35);
            o->Scale = Scale;
            o->LifeTime = 25 + WorldRandom() % 50;

            Vector(1.f, 1.f, 1.f, o->Light);
        }
        else if (o->SubType == 3)
        {
            o->Velocity = 50.f;
            o->Scale = Scale;
            o->LifeTime = 5;
            o->SetMaxTails(10);

            float Matrix[3][4];
            Vector(0.f, 45.f, -90.f, o->Angle);
            AngleMatrix(o->Angle, Matrix);
            Vector(0.f, 100, 0.f, p);
            VectorRotate(p, Matrix, tailPosition);
            VectorAdd(o->Position, tailPosition, o->Position);

            CreateSprite(BITMAP_SHINY + 1, o->Position, (float)(WorldRandom() % 8 + 8) * 0.3f,
                         o->Light, NULL, (float)(WorldRandom() % 360));

            Vector(1.f, 1.f, 1.f, o->Light);
            o->Angle[0] *= -1.f;
            o->Angle[1] *= -1.f;
            o->Angle[2] *= -1.f;
        }
        else if (o->SubType == 4 || o->SubType == 6 || o->SubType == 12)
        {
            o->Scale = Scale;
            VectorCopy(TargetPosition, o->TargetPosition);
            if (o->SubType == 12)
            {
                o->LifeTime = 70;
                o->SetMaxTails(50);
                //Vector(1.f,1.f,1.f,o->Light);
                Vector(0.1f, 0.1f, 1.f, o->Light);

                //o->Direction[1] = WorldRandom()%150/100.f;
                o->Direction[0] = (float)(WorldRandom() % 360);
                o->Direction[1] = -4.f * (float)sinf(-o->Angle[2] * Q_PI / 180.0f);
                o->Direction[2] = -4.f * (float)cosf(-o->Angle[2] * Q_PI / 180.0f);
            }
            else
            {
                if (o->SubType == 6)
                {
                    o->LifeTime = 20;
                    o->SetMaxTails(30);
                    if (o->Type == BITMAP_FLARE_BLUE)
                    {
                        o->LifeTime = 15;
                        o->SetMaxTails(15);
                    }
                    Vector(1.f, 1.f, 1.f, o->Light);
                }
                else
                {
                    o->LifeTime = 110;
                    o->SetMaxTails(200);

                    VectorCopy(o->Target->Light, o->Light);
                }
                o->Direction[0] = (float)(WorldRandom() % 360);
                o->Direction[1] = -2.0f * (float)sinf(-o->Angle[2] * Q_PI / 180.0f);
                o->Direction[2] = -2.0f * (float)cosf(-o->Angle[2] * Q_PI / 180.0f);
            }
        }
        else if (o->SubType == 5)
        {
            o->LifeTime = 2;
            o->SetMaxTails(3);
            o->Direction[2] = -(float)(WorldRandom() % 3 + 40);

            Vector(1.f, 1.f, 1.f, o->Light);
        }
        else if (o->SubType == 7)
        {
            Vector(0.2f, 0.2f, 1.0f, o->Light);
            o->MultiUse = WorldRandom() % 10;
            o->LifeTime = 30 + o->MultiUse;
            o->SetMaxTails(15);
            o->Direction[0] = (float)(WorldRandom() % 3000);
            o->Scale = 30.f;
        }
        else if (o->SubType == 8)
        {
            Vector(0.f, 0.f, 0.f, o->Angle);
            Vector(0.f, 0.f, 0.f, o->Direction);
            VectorCopy(o->Position, o->StartPosition);

            Vector(0.f, -50.f, 0.f, p);
            AngleMatrix(o->TargetPosition, Matrix);
            VectorRotate(p, Matrix, tailPosition);
            VectorAdd(o->StartPosition, tailPosition, o->Position);
        }
        else if (o->SubType == 9) //
        {
            o->LifeTime = 0;
            o->Scale = Scale;
            o->SetMaxTails(10);
            Vector(0.f, 0.f, 0.f, o->Angle);
            Vector(0.3f, 0.3f, 1.f, o->Light);
            o->StartPosition[0] = (TargetPosition[0] - o->Position[0]) / o->MaxTails;
            o->StartPosition[1] = (TargetPosition[1] - o->Position[1]) / o->MaxTails;
            o->StartPosition[2] = (TargetPosition[2] - o->Position[2]) / o->MaxTails;

            AngleMatrix(o->Angle, Matrix);

            for (int i = 0; i < o->MaxTails; i++)
            {
                VectorAdd(o->Position, o->StartPosition, o->Position);
                GameLogic::Effects::CreateTail(o, Matrix);
            }
        }
        else if (o->SubType == 11)
        {
            Vector(0.2f, 0.2f, 1.0f, o->Light);
            o->MultiUse = 0;
            o->LifeTime = 30;
            o->SetMaxTails(15);
            o->Direction[0] = (float)(WorldRandom() % 3000);
            o->Scale = 30.f;
        }
        else if (o->SubType == 25)
        {
            Vector(0.9f, 0.4f, 0.6f, o->Light);
            o->MultiUse = 0;
            o->LifeTime = 30;
            o->SetMaxTails(15);
            o->Direction[0] = (float)(WorldRandom() % 3000);
            o->Scale = 30.f;
        }
        else if (o->SubType == 13)
        {
            o->Direction[2] = (float)(WorldRandom() % 20 + 35);
            o->Scale = Scale;
            o->LifeTime = 25 + WorldRandom() % 50;

            Vector(0.5f, 0.5f, 0.5f, o->Light);
        }
        else if (o->SubType == 23)
        {
            o->LifeTime = 20 + (4 - PKKey);
            o->Scale = Scale;
            o->SetMaxTails(15);
            o->NumTails = -1;
            o->Velocity = 0.f;
            o->Direction[0] = 1.f;
            o->Direction[1] = 5.f;
            o->Direction[2] = 1.f;
            o->m_bCreateTails = false;

            VectorCopy(o->Angle, o->HeadAngle);

            o->MultiUse = PKKey;
            switch ((int)o->MultiUse)
            {
            case 0:
                o->HeadAngle[2] += 90.f;
                o->Position[2] += 200.f;
                break;
            case 1:
                o->HeadAngle[2] += 90.f;
                o->Position[2] += 10.f;
                break;
            case 2:
                o->HeadAngle[2] -= 90.f;
                o->Position[2] += 200.f;
                break;
            case 3:
                o->HeadAngle[2] -= 90.f;
                o->Position[2] += 10.f;
                break;
            case 4:
                o->HeadAngle[2] += 180.f;
                o->Position[2] += 100.f;
                break;
            case 5:
                o->HeadAngle[0] = 90.f;
                o->Position[2] += 100.f;
                o->LifeTime = 10;
                o->SetMaxTails(20);
                o->RenderFace = RENDER_FACE_TWO;
                break;
            }
            VectorCopy(o->Position, o->StartPosition);
        }
        else if (o->SubType == 43)
        {
            o->LifeTime = 100;
            o->SetMaxTails(0);
            o->m_bCreateTails = false;
        }
        else if (o->SubType == 44)
        {
            o->Scale = Scale;
            o->LifeTime = 15;
            o->SetMaxTails(30);
            o->m_bCreateTails = true;
            o->Direction[2] = (float)(WorldRandom() % 2 + 2);
        }
        else if (o->SubType == 45 || o->SubType == 46)
        {
            Vector(0.2f, 0.2f, 1.0f, o->Light);
            o->MultiUse = WorldRandom() % 10;
            o->LifeTime = 30 + o->MultiUse;
            o->SetMaxTails(15);
            o->Direction[0] = (float)(WorldRandom() % 3000);
            o->Scale = 30.f;
        }
        else if (o->SubType == 47)
        {
            Vector(0.7f, 0.7f, 1.0f, o->Light);
            o->MultiUse = WorldRandom() % 10;
            o->LifeTime = 30 + o->MultiUse;
            o->SetMaxTails(15);
            o->Direction[0] = (float)(WorldRandom() % 3000);
            o->Scale = 30.f;
        }
        else
        {
            o->m_bCreateTails = false;
        }
        VectorCopy(TargetPosition, o->TargetPosition);
        break;

    case BITMAP_FLARE + 1:
        o->LifeTime = PKKey;
        o->PKKey = 0;
        o->SetMaxTails(50);
        switch (o->Skill)
        {
        case 0:
            o->Scale = 20.f;
            Vector(0.5f, 0.5f, 0.5f, o->Light);
            break;

        case 1:
            o->Scale = 40.f;
            Vector(1.f, 0.5f, 0.f, o->Light);
            break;
        case 3:
            o->Scale = 20.f;
            VectorCopy(Target->Light, o->Light);
            break;
        }
        switch (o->SubType)
        {
        case 0:
            o->Velocity = 0.f;
            Vector(Scale * 1.5f, 0.f, 0.f, o->Direction);
            break;

        case 1:
            o->Velocity = 90.f;
            Vector(Scale * 1.5f, 0.f, 0.f, o->Direction);
            break;

        case 2:
            o->Velocity = 180.f;
            Vector(Scale * 1.5f, 0.f, 0.f, o->Direction);
            break;

        case 3:
            o->Velocity = 240.f;
            Vector(Scale * 1.5f, 0.f, 0.f, o->Direction);
            break;

        case 4: {
            o->RenderType = RENDER_TYPE_ALPHA_BLEND_MINUS;
            o->LifeTime = 0;
            o->Scale = Scale;
            o->SetMaxTails(10);

            Vector(0.f, 0.f, 0.f, o->Angle);
            Vector(1.f, 1.f, 1.f, o->Light);
            o->StartPosition[0] = (TargetPosition[0] - o->Position[0]) / o->MaxTails;
            o->StartPosition[1] = (TargetPosition[1] - o->Position[1]) / o->MaxTails;
            o->StartPosition[2] = (TargetPosition[2] - o->Position[2]) / o->MaxTails;

            AngleMatrix(o->Angle, Matrix);

            for (int i = 0; i < o->MaxTails; i++)
            {
                VectorAdd(o->Position, o->StartPosition, o->Position);
                GameLogic::Effects::CreateTail(o, Matrix);
            }
        }
        break;

        case 5:
            Vector(1.f, 1.f, 1.f, o->Light);
            o->RenderType = RENDER_TYPE_ALPHA_BLEND_OTHER;
            o->RenderFace = RENDER_FACE_TWO;
            o->LifeTime = 50;
            o->SetMaxTails(8);
            o->Velocity = 3.f;
            o->byOnlyOneRender = 2;
            break;

        case 6:
            Vector(1.f, 1.f, 1.f, o->Light);
            o->RenderType = RENDER_TYPE_ALPHA_BLEND_OTHER;
            o->Scale = Scale;
            o->SetMaxTails(16);
            o->Velocity = 5.f;
            break;

        case 7:
            Vector(1.f, 1.f, 1.f, o->Light);
            Vector(0.f, 0.f, 0.f, o->Direction);
            o->RenderType = RENDER_TYPE_ALPHA_BLEND_OTHER;
            o->Scale = Scale;
            o->SetMaxTails(16);
            o->Velocity = 10.f;
            o->TexType = BITMAP_FLARE;
            o->Direction[0] = 15.f;
            o->Position[0] = TargetPosition[0] + (float)cos((float)(WorldRandom() % 360)) * 40;
            o->Position[1] = TargetPosition[1] - (float)sin((float)(WorldRandom() % 360)) * 40;
            VectorCopy(o->Position, o->StartPosition);
            break;
        case 8:
        case 9:
            o->RenderFace = RENDER_FACE_TWO;
        case 10:
        case 11:
            o->RenderType = RENDER_TYPE_ALPHA_BLEND_OTHER;
            o->Scale = Scale;
            o->SetMaxTails(20);
            o->LifeTime = 100;
            o->Velocity = 0.f;
            o->byOnlyOneRender = 1;
            o->MultiUse = SkillIndex;

            if (o->SubType == 10 || o->SubType == 11)
            {
                Vector(0.7f, 0.7f, 0.7f, o->Light);
                o->SetMaxTails(10);
            }
            else
            {
                Vector(1.f, 1.f, 1.f, o->Light);
            }
            Vector(0.f, 0.f, 0.f, o->Direction);

            if (o->Target == NULL)
            {
                Joints.Retire(*o);
            }
            VectorCopy(o->Target->Position, o->Position);
            VectorCopy(o->Target->Angle, o->Angle);
            if (o->SubType == 8)
            {
                o->Position[2] = 300.f;
            }
            else if (o->SubType == 9)
            {
                o->Position[2] += o->MultiUse;
            }
            break;

        case 12:
            Vector(0.6f, 0.2f, 0.8f, o->Light);
            o->RenderType = RENDER_TYPE_ALPHA_BLEND_OTHER;
            o->Scale = Scale;
            o->SetMaxTails(16);
            o->Velocity = 70.f;
            break;
        case 13:
            Vector(0.7f, 0.7f, 0.3f, o->Light);
            o->RenderType = RENDER_TYPE_ALPHA_BLEND_OTHER;
            o->Scale = Scale;
            o->SetMaxTails(16);
            o->Velocity = 70.f;
            break;
        case 14:
            Vector(1.0f, 1.0f, 1.0f, o->Light);
            o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            o->Scale = Scale;
            o->SetMaxTails(16);
            o->Velocity = 70.f;
            break;
        case 15:
            Vector(0.4f, 0.9f, 0.5f, o->Light);
            o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            o->Scale = Scale;
            o->SetMaxTails(16);
            o->Velocity = 70.f;
            break;
        case 16:
            o->RenderType = RENDER_TYPE_ALPHA_BLEND_OTHER;
            o->RenderFace = RENDER_FACE_TWO;
            o->LifeTime = 35;
            o->SetMaxTails(8);
            o->Velocity = 7.f;
            o->byOnlyOneRender = 2;
            Vector(1.f, 0.5f, 0.3f, o->Light);
            //					VectorCopy(Target->Light, o->Light);
            break;
        case 17:
            Vector(1.0f, 1.0f, 1.0f, o->Light);
            o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            o->Scale = Scale; //+50.f;
            o->SetMaxTails(16);
            o->Velocity = 70.f;
            break;
        case 18: {
            if (vPriorColor)
            {
                VectorCopy(vPriorColor, o->Light);
            }
            o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            o->Scale = Scale;
            o->SetMaxTails(7);
            o->Velocity = 70.f;
        }
        break;
        case 19: {
            Vector(0.5f, 0.5f, 1.0f, o->Light);
            o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            o->Scale = Scale;
            o->SetMaxTails(14);
            o->Velocity = 30.f;
            o->LifeTime = 50;
        }
        break;
        case 20: {
            Vector(0.5f, 0.5f, 1.0f, o->Light);
            o->RenderType = RENDER_TYPE_ALPHA_BLEND;
            o->Scale = Scale;
            o->SetMaxTails(14);
            o->Velocity = 30.f;
            o->LifeTime = 50;
        }
        break;
        }
        VectorCopy(TargetPosition, o->TargetPosition);
        break;
    case BITMAP_JOINT_FORCE:
        if (o->SubType == 0 || o->SubType == 10)
        {
            o->LifeTime = 20;
            o->m_bCreateTails = false;
            o->Scale = Scale;
            o->Velocity = 0.f;
            o->SetMaxTails(18);
            o->NumTails = -1;

            Vector(0.f, -180.f, 0.f, p);
            VectorCopy(o->Position, o->TargetPosition);
            o->Angle[2] += 30.f;
            VectorCopy(o->Angle, o->Direction);

            AngleMatrix(o->Angle, Matrix);
            VectorRotate(p, Matrix, tailPosition);
            VectorAdd(o->Position, tailPosition, o->Position);
            //					Vector(0.f,1.f,0.f,o->Light);
        }
        else if (o->SubType == 1)
        {
            o->Scale = Scale;
            o->SetMaxTails(15);
            o->LifeTime = 30;
            o->Velocity = 0.f;
            o->byOnlyOneRender = 1;
            o->Weapon = 0;

            Vector(1.f, 1.f, 1.f, o->Light);
            Vector(0.f, 0.f, 0.f, o->Direction);

            if (o->Target == NULL)
            {
                Joints.Retire(*o);
            }
            VectorCopy(o->Target->Position, o->Position);
            VectorCopy(o->Target->Angle, o->Angle);
        }
        else if (o->SubType >= 2 && o->SubType <= 6)
        {
            o->LifeTime = 20;
            o->Scale = Scale;
            o->Velocity = 8.f;
            o->Direction[0] = 3.5f;
            o->Direction[2] = 1.f;
            o->SetMaxTails(12);
            o->NumTails = -1;
            o->MultiUse = 5;
            if (o->SubType == 3)
            {
                o->TexType = BITMAP_FLARE;
                o->MultiUse = 10;
                o->Velocity = 10.f;
                o->Direction[2] = 2.f;
                o->m_byReverseUV = 0;
                o->Direction[0] = 10.5f;
            }
            else if (o->SubType == 4)
            {
                o->LifeTime = 20;
                o->SetMaxTails(13);
                o->TexType = BITMAP_HOLE;
                o->Direction[0] = 15.5f;
                o->Direction[2] = 3.f;
                o->MultiUse = 10;
            }
            else if (o->SubType == 5)
            {
                o->TexType = BITMAP_LAVA;
            }
            else if (o->SubType == 6)
            {
                o->TexType = BITMAP_LAVA;
                o->Velocity = 20.f;
                o->LifeTime = 16;
            }
            else
            {
                o->Position[2] += 10.f;
                o->TexType = BITMAP_INFERNO;
                o->LifeTime = 15;
                o->MultiUse = 5;
                o->Direction[0] = 5.f;
                o->Direction[2] = 5.f;
            }
            Vector(0.f, 0.f, o->Angle[2], o->HeadAngle);
            VectorCopy(o->Position, o->StartPosition);
        }
        else if (o->SubType == 7)
        {
            o->Scale = Scale;
            o->Velocity = 8.f;
            o->Direction[0] = 3.5f;
            o->Direction[2] = 1.f;
            o->SetMaxTails(13);
            o->NumTails = -1;
            o->MultiUse = 5;

            o->TexType = BITMAP_INFERNO;
            o->Velocity = 10.f;
            o->LifeTime = 20;
        }
        else if (o->SubType == 8) // SubType : 7
        {
            o->LifeTime = 20;
            o->m_bCreateTails = false;
            o->Scale = Scale;
            o->Velocity = 0.f;
            o->SetMaxTails(18);
            o->NumTails = -1;

            Vector(0.f, -180.f, 0.f, p);
            VectorCopy(o->Position, o->TargetPosition);
            o->Angle[2] += 30.f;
            VectorCopy(o->Angle, o->Direction);

            AngleMatrix(o->Angle, Matrix);
            VectorRotate(p, Matrix, tailPosition);
            VectorAdd(o->Position, tailPosition, o->Position);
            //					Vector(1.f,1.f,1.f,o->Light);
        }
        else if (o->SubType == 20) // SubType : 7
        {
            o->Scale = Scale;
            o->Velocity = 8.f;
            o->Direction[0] = 3.5f;
            o->Direction[2] = 1.f;
            o->SetMaxTails(15);
            o->NumTails = -1;
            o->MultiUse = 5;

            o->TexType = BITMAP_INFERNO;
            o->Velocity = 10.f;
            o->LifeTime = 20;
        }
        break;
    case BITMAP_LIGHT:
        o->m_bCreateTails = false;
        if (o->SubType == 0)
        {
            o->LifeTime = 20;
            o->SetMaxTails(30);
            o->Scale = Scale;

            o->Velocity = WorldRandom() % 10 + 10.f;
            Vector(-(float)(WorldRandom() % 40) + 30.f, 0.f, (float)(WorldRandom() % 360),
                   o->Angle);
            Vector(0.f, 0.f, 0.f, o->Direction);
            Vector(0.1f, 0.5f, 1.f, o->Light);

            VectorCopy(o->Position, o->StartPosition);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 10;
            o->SetMaxTails(20);
            o->Scale = Scale;
            o->Skill = WorldRandom() % 5;

            o->Velocity = WorldRandom() % 5 + 1.f;
            Vector(30.f, 0.f, (float)(WorldRandom() % 360), o->Angle);
            Vector(0.f, 0.f, 0.f, o->Direction);
            Vector(1.f, 0.8f, 0.6f, o->Light);

            VectorCopy(o->Position, o->StartPosition);
        }
        break;

    case BITMAP_PIERCING:
        o->m_bCreateTails = false;
        if (o->SubType == 0)
        {
            o->LifeTime = 10;
            o->SetMaxTails(30);
            o->Scale = Scale;

            o->Velocity = 20.f;
            Vector(-90.f, 0.f, Angle[2], o->Angle);
            Vector(0.f, 0.f, 0.f, o->Direction);
            if (Random.FpsCheck(2, 1.0))
            {
                Vector(1.f, 0.8f, 0.6f, o->Light);
            }
            else
            {
                Vector(1.f, 1.f, 1.f, o->Light);
            }

            VectorCopy(o->Position, o->StartPosition);
        }
        else if (o->SubType == 1)
        {
            o->LifeTime = 10;
            o->bTileMapping = TRUE;
            o->SetMaxTails(30);
            o->Scale = Scale;

            o->Velocity = 20.f;
            Vector(-90.f, 0.f, Angle[2], o->Angle);
            Vector(0.f, 0.f, 0.f, o->Direction);
            if (Random.FpsCheck(2, 1.0))
            {
                Vector(1.f, 0.8f, 0.6f, o->Light);
            }
            else
            {
                Vector(1.f, 1.f, 1.f, o->Light);
            }

            VectorCopy(o->Position, o->StartPosition);
        }
        break;
    case BITMAP_FLARE_FORCE:
        o->LifeTime = 20;
        o->m_bCreateTails = false;
        o->SetMaxTails(30);
        o->Scale = Scale;
        o->MultiUse = 1;
        o->Velocity = -3.f;
        o->Position[2] += 150.f;
        Vector(0.f, 0.f, 0.f, o->Direction);
        Vector(0.f, 0.f, 0.f, o->TargetPosition);
        Vector(1.f, 0.8f, 1.f, o->Light);
        VectorCopy(o->Position, o->StartPosition);
        o->Weapon = 0;

        if (o->SubType >= 1 && o->SubType <= 4 || (o->SubType >= 11 && o->SubType <= 13))
        {
            o->TexType = BITMAP_JOINT_THUNDER;
            o->Velocity = -3.f;
            o->TargetPosition[0] = 80.f;
            o->TargetPosition[1] = 180.f;
            o->Weapon = WorldRandom() % 3 + 2;
            if (o->SubType == 2 || o->SubType == 4 || (o->SubType >= 11 && o->SubType <= 13))
            {
                switch (o->SubType)
                {
                case 11:
                    Vector(0.7f, 1.0f, 0.7f, o->Light);
                    break;
                case 12:
                    Vector(1.0f, 0.6f, 0.6f, o->Light);
                    break;
                case 13:
                    Vector(0.7f, 0.7f, 1.0f, o->Light);
                    break;
                }

                o->TargetPosition[1] = -180.f;
            }

            if (o->SubType == 3 || o->SubType == 4)
            {
                o->Weapon = 0;
            }

            o->LifeTime += o->Weapon;

            AngleMatrix(o->TargetPosition, Matrix);
            Vector(0.f, 0.f, o->TargetPosition[0], p);
            VectorRotate(p, Matrix, tailPosition);
            VectorAdd(o->StartPosition, tailPosition, o->Position);
        }
        else if (o->SubType != 0)
        {
            o->LifeTime = 15;
            o->Skill = 0;
            o->SetMaxTails(1);
            o->Weapon = 30;
            o->MultiUse = SkillIndex;
            o->Velocity = 0.f;
            o->TargetPosition[0] = 30.f;
            o->TargetPosition[2] = (float)(o->SubType * 90);
            Vector(1.f, 0.8f, 1.f, o->Light);
            Vector(0.f, -4.f, 0.f, o->Direction);
            VectorCopy(o->Position, o->StartPosition);

            if (o->PKKey != -1)
                o->Direction[1] = 0.f;
        }

        if (o->SubType >= 5 && o->SubType <= 7)
        {
            o->TexType = BITMAP_FIRECRACKER;
        }
        else if (o->SubType == 0)
        {
            o->TexType = BITMAP_JOINT_THUNDER;
        }
        break;
    case BITMAP_FLASH:
        if (o->SubType <= 3 || o->SubType == 5)
        {
            o->SetMaxTails(10);
            o->LifeTime = 40;
            o->Velocity = 70.f;
            o->Scale = Scale;
            o->m_byReverseUV = WorldRandom() % 2;
            o->MultiUse = 150;
            o->PKKey = 0;
            if (o->SubType == 2)
            {
                o->Velocity = 30.f;
                o->MultiUse = 300;
                o->TexType = BITMAP_FLARE_FORCE;
            }
            else if (o->SubType == 3)
            {
                o->Velocity = 30.f;
                o->MultiUse = 300;
                o->TexType = BITMAP_FLARE_BLUE;
            }

            if (o->SubType == 5)
            {
                o->TexType = BITMAP_FLARE;
                o->SetMaxTails(15);
                o->Velocity = WorldRandom() % 20 + 10.f;
                o->m_byReverseUV = 2;
            }
            else
            {
                Vector(90.f, 0.f, 0.f, o->Angle);
            }
            VectorCopy(o->Angle, o->HeadAngle);
        }
        else if (o->SubType == 4)
        {
            o->Scale = Scale;
            o->SetMaxTails(20);
            o->LifeTime = 30;
            o->Velocity = 0.f;
            o->byOnlyOneRender = 1;
            o->Weapon = 0;
            o->TexType = BITMAP_FLARE_BLUE;

            Vector(1.f, 1.f, 1.f, o->Light);
            Vector(0.f, 0.f, 0.f, o->Direction);

            if (o->Target == NULL)
            {
                Joints.Retire(*o);
            }
            VectorCopy(o->Target->Position, o->Position);
            VectorCopy(o->Target->Angle, o->Angle);
        }
        else if (o->SubType == 6)
        {
            o->Scale = Scale;
            o->SetMaxTails(30);
            o->LifeTime = 25;
            o->Weapon = o->LifeTime;
            o->Velocity = (float)(70 + WorldRandom() % 3) * Q_PI / 180;

            Vector(0.8f, 0.8f, 1.0f, o->Light);
            Vector(0.f, -(40.f + (float)(WorldRandom() % 4)), sinf(o->Velocity) * 10.f,
                   o->Direction);
            o->Position[1] -= 20.f;
            o->Position[2] += 130.f;
            VectorCopy(o->Position, o->StartPosition);
            VectorCopy(Angle, o->Angle);
            AngleMatrix(o->Angle, Matrix);
            o->m_bCreateTails = false;
            o->m_byReverseUV = 3;
            GameLogic::Effects::CreateTailAxis(o, Matrix, 1);
            o->NumTails = 0;
        }
        else if (o->SubType == 7)
        {
            //6 Opener
            o->m_bCreateTails = false;
            o->LifeTime = 0;
            for (int i = 0; i < 3; ++i)
            {
                vec3_t Pos;
                AngleMatrix(o->Angle, Matrix);
                Vector((i - 1) * 100, -50.f, 0.f, Pos);
                VectorRotate(Pos, Matrix, tailPosition);
                VectorAdd(tailPosition, o->Position, Pos);
                CreateJoint(BITMAP_FLASH, Pos, Pos, o->Angle, 6, NULL, 40.f);
            }
        }
        break;
    case BITMAP_DRAIN_LIFE_GHOST: {
        switch (o->SubType)
        {
        case 0: {
            o->RenderType = RENDER_TYPE_ALPHA_BLEND;

            // o->Target
            if (Random.FpsCheck(2, 1.0))
            {
                o->Angle[2] += 90.f;
            }
            else
            {
                o->Angle[2] -= 90.f;
            }

            VectorCopy(TargetPosition, o->TargetPosition);
            o->Angle[0] += (float)((WorldRandom() % 100) - 50);              // -140 ~ 140
            o->Angle[1] += (float)((WorldRandom() % 100) - 50);              // -140 ~ 140
            o->Angle[2] += (float)((WorldRandom() % 100) - 50);              // -140 ~ 140
            o->Velocity = (float)(1 + ((float)(WorldRandom() % 10) * 0.2f)); // 20 ~ 40
            o->LifeTime = (float)(30 + (WorldRandom() % 20 - 10));           // 20 ~ 30
            o->Scale = Scale + (float)((WorldRandom() % 60 - 30));           // Scale-30 ~ Scale+30
            o->SetMaxTails(static_cast<int>((float)(20 + (WorldRandom() % 10 - 5)))); // 5 ~ 15
        }
        break;
        }
    }
    break;
    case BITMAP_PIN_LIGHT: {
        o->Scale = Scale;
        o->Velocity = (float)(WorldRandom() % 60 + 15);
        o->LifeTime = WorldRandom() % 3 + 1;
        o->SetMaxTails(5);
    }
    break;
    case BITMAP_FORCEPILLAR: {
        o->RenderType = RENDER_TYPE_ALPHA_BLEND;
        o->RenderFace = RENDER_FACE_TWO;
        o->Scale = Scale;
        o->LifeTime = 7;
        o->SetMaxTails(5);
        o->m_bCreateTails = true;
        Vector(0.95f, 0.72f, 0.48f, o->Light);
    }
    break;
    case BITMAP_SWORDEFF: {
        o->RenderType = RENDER_TYPE_ALPHA_BLEND;
        o->RenderFace = RENDER_FACE_TWO;
        o->Scale = Scale;
        o->LifeTime = 10;
        o->SetMaxTails(50);
        o->m_bCreateTails = true;
        Vector(0.6f, 0.6f, 1.0f, o->Light);
    }
    break;
    case BITMAP_GROUND_WIND: {
        o->RenderType = RENDER_TYPE_ALPHA_BLEND;
        o->RenderFace = RENDER_FACE_TWO;
        o->Scale = Scale;
        o->LifeTime = 10;
        o->SetMaxTails(8);
        o->m_bCreateTails = true;
        Vector(0.7f, 0.7f, 1.0f, o->Light);
    }
    break;
    case BITMAP_LAVA: {
        o->RenderType = RENDER_TYPE_ALPHA_BLEND;
        o->RenderFace = RENDER_FACE_TWO;
        o->Scale = Scale;
        o->LifeTime = 10;
        o->SetMaxTails(10);
        o->m_bCreateTails = true;
        Vector(1.0f, 1.0f, 1.0f, o->Light);
        o->Velocity = Models[o->Target->Type].Actions[o->Target->CurrentAction].PlaySpeed;
    }
    break;
    }

    if (OwnsMovingLightning(*o) || HasSampledBoneTrail(*o) ||
        (o->Type == BITMAP_JOINT_THUNDER + 1 && o->SubType == 0))
        o->m_bCreateTails = false;
    PrepareFlareHeadAngles(*o, Random);
    o->TailSampleFrames = 0.f;
    if (o->NumTails < 0)
    {
        AngleMatrix(o->Angle, Matrix);
        GameLogic::Effects::CreateTail(o, Matrix);
        o->NumTails = -1;
    }
    std::memcpy(o->LastTailSample, o->Tails[0], sizeof(o->LastTailSample));

    return;
}

void SessionGameplayUnit::DeleteJoint(int Type, OBJECT *Target, int SubType)
{
    constexpr int AllTypes = -1;
    for (auto cursor = Joints.begin(), end = Joints.end(); cursor != end; ++cursor)
    {
        const int i = cursor.Index();
        JOINT &joint = *cursor;
        if ((Type != AllTypes && joint.Type != Type) ||
            (Target != nullptr && joint.Target != Target) ||
            (SubType != -1 && joint.SubType != SubType))
            continue;

        Joints.Retire(joint);
        joint.Target = nullptr;
        joint.Tails.Clear();
    }
}

bool SessionGameplayUnit::SearchJoint(int Type, OBJECT *Target, int SubType)
{
    for (auto cursor = Joints.begin(), end = Joints.end(); cursor != end; ++cursor)
    {
        const int i = cursor.Index();
        JOINT *o = &*cursor;
        if (o->Type == Type && o->Target == Target)
        {
            if (SubType == -1 || o->SubType == SubType)
                return true;
        }
    }
    return false;
}

void SessionLegacyCalls::DeleteJoint(int type, OBJECT *target, int subType)
{
    sessionKeeper_.Gameplay()->DeleteJoint(type, target, subType);
}

bool SessionLegacyCalls::SearchJoint(int type, OBJECT *target, int subType)
{
    return sessionKeeper_.Gameplay()->SearchJoint(type, target, subType);
}

void SessionGameplayUnit::EmitLightningArrival(JOINT *joint, float duration, float offset)
{
    const bool fenrir = joint->Type == MODEL_FENRIR_SKILL_THUNDER;
    const bool thunder = joint->Type == BITMAP_JOINT_THUNDER;
    if ((fenrir || thunder) && joint->Scale == 50.f)
    {
        for (auto birthTime : Emissions(duration, duration, offset))
            CreateParticle(BITMAP_ENERGY, joint->Position, joint->Angle, joint->Light);
        for (auto birthTime : Emissions(duration / 8.f, duration, offset))
            CreateParticle(BITMAP_SMOKE, joint->Position, joint->Angle, joint->Light);
    }
    else if (!fenrir && !thunder)
    {
        for (auto birthTime : Emissions(duration / 2.f, duration, offset))
            CreateParticle(BITMAP_FIRE, joint->Position, joint->Angle, joint->Light);
    }
    if (!thunder || (joint->SubType != 0 && joint->SubType != 27) || joint->Scale != 50.f)
        return;
    const double millisecondsPerFrame =
        1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    const double frameStart = WorldTime - FPS_ANIMATION_FACTOR * millisecondsPerFrame;
    const double endTime = frameStart + (offset + duration) * millisecondsPerFrame;
    for (double time = frameStart + offset * millisecondsPerFrame; time < endTime;)
    {
        const double period = std::floor(time / 1000.0) * 1000.0;
        const bool active = time < period + 500.0;
        const double end = (std::min)(endTime, period + (active ? 500.0 : 1000.0));
        if (active)
        {
            const float activeFrames = float((end - time) / millisecondsPerFrame);
            const float activeOffset = float((time - frameStart) / millisecondsPerFrame);
            for (auto birthTime : Emissions(activeFrames / 16.f, activeFrames, activeOffset))
            {
                vec3_t position{joint->TargetPosition[0] + WorldRandom() % 100 - 50.f,
                                joint->TargetPosition[1] + WorldRandom() % 100 - 50.f,
                                joint->TargetPosition[2] + WorldRandom() % 120 - 60.f};
                CreateJoint(BITMAP_JOINT_THUNDER, position, joint->TargetPosition, joint->Angle, 28,
                            joint->Target, 6.f + WorldRandom() % 8, -1, 0, 0, 0, joint->Light);
            }
        }
        time = end;
    }
}

void SessionGameplayUnit::MoveLightningRibbon(JOINT *o)
{
    vec3_t Light;
    const bool fenrir = o->Type == MODEL_FENRIR_SKILL_THUNDER;
    const bool thunder = o->Type == BITMAP_JOINT_THUNDER;
    AdvanceMovingLightning(
        *o, FPS_ANIMATION_FACTOR, Random, WorldTime,
        [&](float offset, float duration) { EmitLightningArrival(o, duration, offset); });
    // Lighting follows the retained ribbon, including frames with only partial travel.
    if ((!fenrir && !thunder) ||
        (o->Scale >= 50.f &&
         (fenrir || o->SubType == 0 || o->SubType == 2 || o->SubType == 11 || o->SubType == 27)))
    {
        for (int sample = 0; sample <= o->NumTails; ++sample)
        {
            vec3_t center;
            for (int axis = 0; axis < 3; ++axis)
                center[axis] = (o->Tails[sample][0][axis] + o->Tails[sample][1][axis]) * 0.5f;
            const float brightness = (4 + ((sample + static_cast<unsigned>(o->PKKey)) & 3)) *
                                     (fenrir || thunder ? 0.04f : 0.05f);
            if (fenrir)
            {
                Vector(brightness * 0.4f, brightness * 0.4f, brightness * 0.4f, Light);
            }
            else if (thunder)
            {
                if (o->SubType == 27)
                {
                    VectorCopy(o->Light, Light);
                }
                else if (o->SubType == 2)
                {
                    Vector(brightness * 0.4f, brightness * 0.1f, brightness * 0.1f, Light);
                }
                else
                {
                    Vector(brightness * 0.1f, brightness * 0.1f, brightness * 0.5f, Light);
                }
            }
            else if (o->Type == BITMAP_JOINT_LASER + 1)
            {
                if (o->SubType == 1)
                {
                    Vector(brightness, brightness * 0.1f, brightness * 0.1f, Light);
                }
                else
                {
                    Vector(brightness, brightness * 0.6f, brightness * 0.3f, Light);
                }
            }
            else
            {
                Vector(0.f, brightness * 0.1f, brightness * 0.2f, Light);
            }
            AddTerrainLight(center[0], center[1], Light, 2, PrimaryTerrainLight);
        }
    }
}

void SessionGameplayUnit::MoveJoint(JOINT *o, int iIndex)
{
    EffectBirthStep birthStep(FPS_ANIMATION_FACTOR, o->BirthTiming);
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    const auto birthSerial = o->BirthTiming.serial;
    SessionRandom::PresentationScope randomOrigin(sessionKeeper_.RandomForConstruction(),
                                                  o->PresentationRandom);
    if (o->Type == BITMAP_FLARE + 1 && (o->SubType == 6 || o->SubType == 8) &&
        Core::Time::Periods(o->LifeTime, FPS_ANIMATION_FACTOR, 1.f) > 0)
        PrepareFlareHeadAngles(*o, Random);
    if (IsSpiralFlare(*o))
    {
        AdvanceSpiralFlare(*o, FPS_ANIMATION_FACTOR);
        if (o->LifeTime < 0.f)
            Joints.Retire(*o);
        return;
    }
    float Height;
    vec3_t Light;
    float Luminosity;
    vec3_t Position, p;

    float Distance;
    float dx = o->Position[0] - o->TargetPosition[0];
    float dy = o->Position[1] - o->TargetPosition[1];

    float Matrix[3][4];
    AngleMatrix(o->Angle, Matrix);

    if (!(o->Type == BITMAP_JOINT_FORCE && o->SubType == 0) &&
        !(o->Type == BITMAP_JOINT_THUNDER && o->SubType == 15) && o->Type != BITMAP_LIGHT &&
        o->Type != BITMAP_PIERCING && o->Type != BITMAP_FLARE_FORCE &&
        o->Type != BITMAP_JOINT_FIRE && o->Type != BITMAP_JOINT_LASER &&
        !((o->Type == BITMAP_FLARE || o->Type == BITMAP_FLARE_BLUE) && o->SubType == 1) &&
        !(o->Type == MODEL_SPEARSKILL &&
          (o->SubType == 5 || o->SubType == 6 || o->SubType == 7 || o->SubType == 8)) &&
        !((o->Type == BITMAP_FLARE) && (o->SubType == 42)) &&
        !(o->Type == BITMAP_FLASH && (o->SubType <= 3 || o->SubType == 5 || o->SubType == 6)) &&
        !(o->Type == BITMAP_2LINE_GHOST && (o->SubType == 0 || o->SubType == 1)) &&
        !(o->Type == BITMAP_JOINT_THUNDER + 1 && o->SubType == 0) && !OwnsMovingLightning(*o) &&
        !OwnsEnergyMotion(*o) &&
        !((o->Type == BITMAP_FLARE || o->Type == BITMAP_FLARE_BLUE) &&
          (o->SubType == 0 || o->SubType == 10 || o->SubType == 14 || o->SubType == 15 ||
           o->SubType == 18 || o->SubType == 23)) &&
        !(o->Type == BITMAP_FLARE + 1 &&
          (o->SubType == 5 || o->SubType == 6 || o->SubType == 7 || o->SubType == 16)) &&
        !(o->Type == BITMAP_DRAIN_LIFE_GHOST && o->SubType == 0) &&
        !(o->Type == BITMAP_JOINT_FORCE &&
          ((o->SubType >= 2 && o->SubType <= 7) || o->SubType == 20)) &&
        !(o->Type == BITMAP_JOINT_HEALING && o->SubType != 4 && o->SubType != 5 &&
          o->SubType != 9 && o->SubType != 10 && o->SubType != 14) &&
        !((o->Type == BITMAP_JOINT_SPIRIT || o->Type == BITMAP_JOINT_SPIRIT2) &&
          (o->SubType == 0 || o->SubType == 3 || o->SubType == 5 || o->SubType == 8 ||
           o->SubType == 9 || o->SubType == 10 || o->SubType == 13 || o->SubType == 18 ||
           o->SubType == 19 || o->SubType == 20 || o->SubType == 24 || o->SubType == 25)))
    {
        if (o->Velocity != 0.0f)
        {
            float acceleration = 0.f;
            if (o->Type == BITMAP_JOINT_SPARK)
                acceleration =
                    o->SubType == 1 ? 0.3f : (o->SubType == 3 || o->SubType == 4 ? 0.1f : 0.f);
            else if ((o->Type == BITMAP_SPARK + 1 && o->SubType == 1) ||
                     o->Type == BITMAP_PIN_LIGHT)
                acceleration = 0.1f;
            else if ((o->Type == BITMAP_JOINT_SPIRIT || o->Type == BITMAP_JOINT_SPIRIT2) &&
                     (o->SubType == 2 || o->SubType == 6 || o->SubType == 7 || o->SubType == 21 ||
                      o->SubType == 22 || o->SubType == 23))
                acceleration = 5.f;
            float travel = 0.f;
            Core::Time::Advance(travel, o->Velocity, acceleration, FPS_ANIMATION_FACTOR);
            Vector(0.f, -travel, 0.f, p);
            VectorRotate(p, Matrix, Position);
            VectorAdd(o->Position, Position, o->Position);
        }
    }

    switch (o->Type)
    {
    case BITMAP_JOINT_LASER:
        AdvanceLaserJoint(*o, FPS_ANIMATION_FACTOR, WorldTime);
        AngleMatrix(o->Angle, Matrix);
        //light
        Luminosity = o->LifeTime * 0.1f;
        Vector(Luminosity, Luminosity, Luminosity, o->Light);
        Luminosity = -(float)(WorldRandom() % 4 + 4) * 0.01f;
        Vector(Luminosity, Luminosity, Luminosity, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 4, PrimaryTerrainLight);
        break;
    case BITMAP_SCOLPION_TAIL:
        VectorCopy(o->Target->EyeLeft, o->Position);
        if (!o->Target->Live)
        {
            Joints.Retire(*o);
            return;
        }
        break;
    case BITMAP_JOINT_ENERGY:
        switch (o->SubType)
        {
        case 2:
        case 3:
        case 4:
        case 5:
        case 8:
        case 10:
        case 11:
        case 14:
        case 15:
        case 17:
        case 18:
        case 19:
        case 20:
        case 21:
        case 26:
        case 27:
        case 28:
        case 29:
        case 30:
        case 31:
        case 32:
        case 33:
        case 22:
        case 23:
        case 24:
        case 25:
        case 47:
        case 48:
        case 49:
        case 50:
        case 51:
        case 52:
        case 53:
        case 54:
        case 55:
        case 56:
        case 57:
            if (!o->Target->Live)
            {
                Joints.Retire(*o);
                return;
            }
            switch (o->SubType)
            {
                //. Left
            case 2:
            case 4:
            case 5:
            case 8:
            case 10:
            case 14:
            case 17:
            case 18:
            case 28:
            case 22:
            case 24:
                VectorCopy(o->Target->EyeLeft, o->Position);
                if (o->SubType == 8)
                {
                    o->Scale += (10.1f) * FPS_ANIMATION_FACTOR;
                }
                break;
            case 20:
            case 30:
                VectorCopy(o->Target->EyeLeft2, o->Position);
                break;
            case 26:
            case 32:
                VectorCopy(o->Target->EyeLeft3, o->Position);
                break;
                //. Right
            case 3:
            case 11:
            case 15:
            case 19:
            case 29:
            case 23:
            case 25:
            case 47:
                VectorCopy(o->Target->EyeRight, o->Position);
                break;
            case 21:
            case 31:
                VectorCopy(o->Target->EyeRight2, o->Position);
                break;
            case 54:
                switch (o->PKKey)
                {
                case 0:
                    VectorCopy(o->Target->EyeRight2, o->Position);
                    break;
                case 1:
                    VectorCopy(o->Target->EyeLeft2, o->Position);
                    break;
                case 2:
                    VectorCopy(o->Target->EyeRight3, o->Position);
                    break;
                case 3:
                    VectorCopy(o->Target->EyeLeft3, o->Position);
                    break;
                }
                break;
            case 27:
            case 33:
                VectorCopy(o->Target->EyeRight3, o->Position);
                break;
            case 55:
                VectorCopy(o->Target->EyeLeft, o->Position);
                break;
            case 56:
                VectorCopy(o->Target->EyeRight, o->Position);
                break;
            case 48:
            case 49:
            case 50:
            case 51:
            case 52:
            case 53:
            case 57: {
                constexpr int bones[]{24, 28, 32, 44, 48, 52};
                const int bone = o->SubType == 57 ? o->PKKey : bones[o->SubType - 48];
                AdvanceBoneTrail(*o, Models[o->Target->Type], bone, FPS_ANIMATION_FACTOR,
                                 WorldTime);
                break;
            }
            }
            switch (o->SubType)
            {
            case 2:
            case 3:
            case 14:
            case 15:
            case 22:
            case 23:
                EmitAttachedEnergyParticles(*o);
                break;
            case 24:
            case 25:
                Luminosity = sinf(WorldTime * 0.002f) * 0.1f + 0.2f;
                Vector(Luminosity, Luminosity, Luminosity, Light);
                VectorMul(Light, o->Light, Light);
                CreateSprite(BITMAP_FLARE + 1, o->Position, Luminosity, Light, o->Target);
                break;
            case 54:
                Luminosity = sinf(WorldTime * 0.002f) * 0.1f;
                Vector(Luminosity, Luminosity, Luminosity, Light);
                VectorMul(Light, o->Light, Light);
                CreateSprite(BITMAP_BLUR, o->Position, Luminosity, Light, o->Target);
                break;
            }
            break;
        case 0:
        case 1:
        case 6:
        case 9:
        case 12:
        case 13:
        case 16:
        case 44:
        case 45:
        case 46: {
            if (AdvanceSteeringJoint(*o, FPS_ANIMATION_FACTOR, Random, WorldTime) ==
                JointArrival::Target)
            {
                Joints.Retire(*o);
                CreateParticle(BITMAP_LIGHTNING + 1, o->Position, o->Angle, o->Light, 1);
                PlayBuffer(SOUND_GET_ENERGY);
            }
            AngleMatrix(o->Angle, Matrix);
            Luminosity = (float)(WorldRandom() % 4 + 8) * 0.03f;
            Vector(Luminosity * 0.4f, Luminosity, Luminosity * 0.8f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);

            if (o->SubType == 6 || o->SubType == 9)
            {
                CreateParticleFpsChecked(BITMAP_LIGHTNING + 1, o->Position, o->Angle, o->Light, 3,
                                         0.05f);
            }
            else if (o->SubType == 12 || o->SubType == 13)
            {
                CreateSprite(BITMAP_LIGHT, o->Position, 1.f, o->Light, NULL);
                CreateSprite(BITMAP_SHINY + 1, o->Position, 1.f, o->Light, NULL,
                             (float)(WorldRandom() % 360));
            }
            else if (o->SubType == 44)
            {
                CreateSprite(BITMAP_LIGHT, o->Position, 0.2f, o->Light, NULL);
                CreateSprite(BITMAP_SHINY + 1, o->Position, 0.1f, o->Light, NULL,
                             (float)(WorldRandom() % 360));
            }
            else if (o->SubType == 16)
            {
                Vector(0.4f, 0.8f, 1.0f, Light);
                CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, Light, 31, 0.8f);
            }
            else if (o->SubType == 46)
            {
                Vector(0.4f, 1.0f, 0.4f, Light);
                CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, Light, 31, 0.8f);
            }
            else if (o->SubType == 45)
            {
                Vector(1.f, 1.f, 1.f, Light);
                CreateSprite(BITMAP_FLARE_RED, o->Position, 0.3f, Light, o->Target);
            }
            else
            {
                CreateParticleFpsChecked(BITMAP_LIGHTNING + 1, o->Position, o->Angle, o->Light);
            }
        }
        break;
        case 42: {
            const float early =
                (std::min)(FPS_ANIMATION_FACTOR, (std::max)(0.f, o->LifeTime - 59.f));
            o->Light[0] += 0.4f * 0.005f * early;
            o->Light[1] += 0.3f * 0.005f * early;
            o->Light[2] += 2.2f * 0.005f * early;
            if (FPS_ANIMATION_FACTOR > early)
                Vector(0.4f, 0.3f, 2.2f, o->Light);
            if (AdvanceSteeringJoint(*o, FPS_ANIMATION_FACTOR, Random, WorldTime) ==
                JointArrival::Target)
            {
                Joints.Retire(*o);
                PlayBuffer(SOUND_GET_ENERGY);
            }
            AngleMatrix(o->Angle, Matrix);

            Luminosity = (float)(WorldRandom() % 4 + 8) * 0.03f;
            Vector(Luminosity * 0.4f, Luminosity, Luminosity * 0.8f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);

            CreateParticleFpsChecked(BITMAP_LIGHTNING + 1, o->Position, o->Angle, o->Light, 5,
                                     0.12f);
        }
        break;
        case 43: {
            const auto arrival = AdvanceSteeringJoint(*o, FPS_ANIMATION_FACTOR, Random, WorldTime);
            AngleMatrix(o->Angle, Matrix);
            if (arrival == JointArrival::Target)
            {
                Joints.Retire(*o);
                vec3_t bomb;
                VectorCopy(o->Position, bomb);
                bomb[2] -= 60.f;
                CreateBomb(bomb, true, 4);
            }
            else if (arrival == JointArrival::Escaped)
                Joints.Retire(*o);

            Luminosity = (float)(WorldRandom() % 4 + 8) * 0.03f;
            Vector(Luminosity * 1.0f, Luminosity * 0.4, Luminosity * 0.3f, Light);
            //Vector(Luminosity*0.3f,Luminosity*0.4,Luminosity*1.0f,Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 1, PrimaryTerrainLight);

            float fLumi;
            vec3_t vPos, vLight;
            VectorCopy(o->Position, vPos);
            fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.1f;
            Vector(2.0f + fLumi, 1.0f + fLumi, 1.0f + fLumi, vLight);
            CreateSprite(BITMAP_POUNDING_BALL, vPos, 0.7f + fLumi, vLight, NULL,
                         (WorldTime / 10.0f));
            fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 1.0f;
            Vector(2.0f + (WorldRandom() % 10) * 0.03f, 0.4f + (WorldRandom() % 10) * 0.03f,
                   0.4f + (WorldRandom() % 10) * 0.03f, vLight);
            CreateSprite(BITMAP_LIGHT, vPos, 2.0f, vLight, NULL, -(WorldTime * 0.1f));
            CreateSprite(BITMAP_LIGHT, vPos, 2.0f, vLight, NULL, (WorldTime * 0.12f));
            Vector(1.0f, 0.6f, 0.4f, vLight);
            CreateParticleFpsChecked(BITMAP_SMOKE, vPos, o->Angle, vLight, 31, 1.0f);
        }
        break;
        }
        break;

    case BITMAP_JOINT_HEALING:
        if (o->SubType == 4)
        {
            VectorCopy(o->Target->Position, o->Position);
            VectorAdd(o->TargetPosition, o->Position, o->Position);

            o->Velocity += FPS_ANIMATION_FACTOR * 10.f;
            o->Position[2] += (std::max)(0.f, o->Velocity - 10.f);
            Luminosity = (12 - o->LifeTime) * 0.1f;
            Vector(Luminosity * 0.4f, Luminosity * 0.6f, Luminosity * 1.f, o->Light);
        }
        else if (o->SubType == 5)
        {
            Luminosity = (12 - o->LifeTime) * 0.1f;
            Vector(Luminosity * 0.4f, Luminosity * 0.6f, Luminosity * 1.f, o->Light);
        }
        else if (o->SubType == 9 || o->SubType == 10)
        {
            VectorSubtract(o->TargetPosition, o->Target->Position, Position);
            VectorCopy(o->Target->Position, o->TargetPosition);
            for (int j = o->NumTails - 1; j >= 0; --j)
            {
                for (int k = 0; k < 4; ++k)
                    VectorSubtract(o->Tails[j][k], Position, o->Tails[j][k]);
            }
            AdvanceHealingOrbit(*o, FPS_ANIMATION_FACTOR);
        }
        else if (o->SubType == 14)
        {
            if (o->LifeTime < 5)
            {
                float fLumi = o->LifeTime / 5.f;
                VectorScale(o->Light, powf(fLumi, FPS_ANIMATION_FACTOR), o->Light);
            }
            o->Scale *= powf(1.05f, FPS_ANIMATION_FACTOR);
            VectorCopy(o->Target->Position, o->Position);
        }
        else if (o->SubType == 15)
        {

            VectorCopy(o->Target->Position, p);

            AdvanceHomingJoint(*o, p, FPS_ANIMATION_FACTOR, WorldTime);
            AngleMatrix(o->Angle, Matrix);

            VectorScale(o->Light, powf(1.f / 1.08f, FPS_ANIMATION_FACTOR), o->Light);

            CreateSprite(BITMAP_SHINY + 1, p, (float)(WorldRandom() % 4 + 4) * 0.2f, o->Light,
                         o->Target, (float)(WorldRandom() % 360));
        }
        else if (o->SubType == 16)
        {

            VectorCopy(o->Target->Position, p);

            AdvanceHomingJoint(*o, p, FPS_ANIMATION_FACTOR, WorldTime);
            AngleMatrix(o->Angle, Matrix);

            VectorScale(o->Light, powf(1.f / 1.08f, FPS_ANIMATION_FACTOR), o->Light);

            CreateSprite(BITMAP_SHINY + 1, p, (float)(WorldRandom() % 4 + 4) * 0.2f, o->Light,
                         o->Target, (float)(WorldRandom() % 360), 1);
        }
        else
        {
            if (o->SubType == 6 || o->SubType == 7 || o->SubType == 12)
            {
                VectorCopy(o->TargetPosition, p);
            }
            else
            {
                if (o->SubType == 8 || o->SubType == 13 || o->SubType == 17)
                {
                    VectorCopy(o->TargetPosition, p);

                    if (o->LifeTime < 6)
                    {
                        VectorScale(o->Light, powf(1.f / 1.8f, FPS_ANIMATION_FACTOR), o->Light);
                    }

                    if (o->SubType == 13)
                        CreateSprite(BITMAP_LIGHT, o->Position, 1.f, o->Light, NULL);
                    else
                        CreateSprite(BITMAP_LIGHT, o->Position, 1.f, o->Light, NULL);
                }
                else
                {
                    VectorCopy(o->Target->Position, p);
                    p[2] += 120.f;
                }
            }
            AdvanceHomingJoint(*o, p, FPS_ANIMATION_FACTOR, WorldTime);
            AngleMatrix(o->Angle, Matrix);
            Luminosity = (12 - o->LifeTime) * 0.1f;
            switch (o->SubType)
            {
            case 1:
                Vector(Luminosity * 0.4f, Luminosity * 0.6f, Luminosity * 1.f, o->Light);
                break;
            case 2:
                Vector(Luminosity * 0.4f, Luminosity * 1.f, Luminosity * 0.6f, o->Light);
                break;
            case 3:
                Vector(Luminosity * 1.f, Luminosity * 0.6f, Luminosity * 0.4f, o->Light);
                break;
            case 11:
                Vector(Luminosity * 0.9f, Luminosity * 0.49f, Luminosity * 0.04f, o->Light);
                break;
            case 12:
                Vector(Luminosity * 0.9f, Luminosity * 0.39f, Luminosity * 0.03f, o->Light);
                break;
            }
            if (o->SubType == 6)
            {
                if (o->LifeTime <= 10)
                {
                    float fAlpha = (float)(6 - abs(o->LifeTime - 6)) * 0.15f;
                    vec3_t Light;
                    Light[0] = Light[1] = Light[2] = fAlpha;
                    CreateSprite(BITMAP_SHINY + 1, p, (float)(WorldRandom() % 8 + 8) * 0.05f, Light,
                                 o->Target, (float)(WorldRandom() % 360));
                }
            }
            else if (o->SubType != 7 && o->SubType != 8 && o->SubType != 12)
            {
                if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 1))
                {
                    CreateSprite(BITMAP_SHINY + 1, p, (float)(WorldRandom() % 8 + 8) * 0.2f,
                                 o->Light, o->Target, (float)(WorldRandom() % 360));
                }
                if (o->SubType == 11)
                {
                    Luminosity = (float)(WorldRandom() % 4 + 4) * 0.01f;
                    Vector(Luminosity, Luminosity, Luminosity, Light);
                    AddTerrainLight(p[0], p[1], Light, 1, PrimaryTerrainLight);
                }
            }
        }
        break;
    case BITMAP_2LINE_GHOST: {
        if (o->SubType == 0)
        {
            AdvanceGhostJoint(*o, FPS_ANIMATION_FACTOR);
            AngleMatrix(o->Angle, Matrix);
            CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 59, 1.0f);
        }
        else if (o->SubType == 1)
        {
            AdvanceGhostJoint(*o, FPS_ANIMATION_FACTOR);
            AngleMatrix(o->Angle, Matrix);
        }
    }
    break;
    case BITMAP_JOINT_SPIRIT:
    case BITMAP_JOINT_SPIRIT2:
        if (0 == o->SubType || o->SubType == 5 || o->SubType == 19)
        {
            if (o->Scale == 80.f)
            {
                if (o->SubType == 5)
                    CreateEffectFpsChecked(MODEL_LASER, o->Position, o->Angle, o->Light, 3);
                else
                {
                    CreateEffectFpsChecked(MODEL_LASER, o->Position, o->Angle, o->Light);
                }

                if (IsBattleCastleStart())
                {
                    DWORD att = TERRAIN_ATTRIBUTE(o->Position[0], o->Position[1]);
                    if ((att & TW_NOATTACKZONE) == TW_NOATTACKZONE)
                    {
                        o->Velocity = 0.f;
                        o->MotionFrames = 0.f;
                        o->LifeTime *= powf(1.f / 5.f, FPS_ANIMATION_FACTOR);
                        break;
                    }
                }

#ifndef CSK_EVIL_SKILL
                if (o->Target == &Hero->Object)
                {
                    constexpr float AttackInterval = 15.f;
                    for (float life = Core::Time::ReferenceSample(o->LifeTime / AttackInterval) *
                                      AttackInterval;
                         life > 0.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life);
                         life -= AttackInterval)
                        AttackCharacterRange(o->Skill, o->Position, 150.f, o->Weapon, o->PKKey,
                                             o->m_bySkillSerialNum);
                }
#endif // CSK_EVIL_SKILL
            }
            else if (o->SubType == 19)
            {
                CreateEffect(MODEL_SKULL, o->Position, o->Angle, o->Light);
            }
            AdvanceWanderingSpirit(*o, FPS_ANIMATION_FACTOR, Random, *sessionKeeper_.WorldUnit(),
                                   WorldTime);
            AngleMatrix(o->Angle, Matrix);

            //light
            if (o->SubType != 19)
            {
                Luminosity = o->LifeTime * 0.1f;
                Vector(Luminosity, Luminosity, Luminosity, o->Light);
                Luminosity = -(float)(WorldRandom() % 4 + 4) * 0.01f;
                Vector(Luminosity, Luminosity, Luminosity, Light);
                AddTerrainLight(o->Position[0], o->Position[1], Light, 4, PrimaryTerrainLight);
            }
        }
        else if (1 == o->SubType)
        {
            Vector(.8f, 0.4f, 1.f, Light);
            CreateSprite(BITMAP_LIGHT, o->Position, 4.0f, Light, o->Target,
                         (float)(WorldRandom() % 360), 0);
        }
        else if (o->SubType == 3 || o->SubType == 13)
        {
            const double phase =
                iIndex + Core::Time::ReferenceFrames(
                             WorldTime, sessionKeeper_.ApplicationConfig().legacyReferenceFps);
            AdvanceScrewSpirit(*o, FPS_ANIMATION_FACTOR, phase, Random, WorldTime);
            AngleMatrix(o->Angle, Matrix);
            float Scale = 3.f;
            if (o->SubType == 3 || o->LifeTime - FPS_ANIMATION_FACTOR + 1.f > 28.f)
            {
                Vector(1.f, 0.5f, 0.1f, Light);
            }
            else
            {
                VectorCopy(o->Light, Light);
                if (o->Target != nullptr)
                    Scale = 2.f * ((o->LifeTime - FPS_ANIMATION_FACTOR + 1.f) / 10.f) + 2.f;
            }
            CreateSprite(BITMAP_LIGHT, o->Position, Scale, Light, o->Target,
                         (float)(WorldRandom() % 360), 0);
            CreateSprite(BITMAP_SHINY + 1, o->Position, Scale / 2.f, Light, o->Target,
                         (float)(WorldRandom() % 360), 0);
        }
        else if (o->SubType == 2 || o->SubType == 6 || o->SubType == 7 || o->SubType == 21 ||
                 o->SubType == 22 || o->SubType == 23)
        {
            vec3_t Angle;

            switch (o->SubType)
            {
            case 2:
                Vector(1.f, 0.5f, 0.1f, Light);
                break;
            case 21:
                Vector(0.1f, 0.5f, 1.f, Light);
                break;
            case 22:
            case 23:
                switch (o->Skill)
                {
                case 0:
                    Vector(1.0f, 1.0f, 1.0f, Light);
                    break;
                case 1:
                    Vector(1.0f, 1.0f, 1.0f, Light);
                    break;
                }
                break;
            case 7:
            case 6:
                switch (o->Skill)
                {
                case 0:
                    Vector(0.3f, 0.3f, 1.f, Light);
                    break;
                case 1:
                    Vector(0.5f, 0.5f, 0.5f, Light);
                    break;
                }
                break;
            }

            if (o->LifeTime < 10)
            {
                VectorScale(o->Light, powf(1.f / 1.2f, FPS_ANIMATION_FACTOR), o->Light);
                VectorCopy(o->Light, Light);
            }
            else if (o->LifeTime > 18 && o->LifeTime < 20 && (o->SubType == 2 || o->SubType == 21))
            {
                Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);

                Position[0] = o->StartPosition[0] + WorldRandom() % 200 - 100;
                Position[1] = o->StartPosition[1] + WorldRandom() % 200 - 100;
                Position[2] = o->StartPosition[2] - 200;

                if (o->SubType == 2)
                    CreateJointFpsChecked(BITMAP_FLARE, Position, Position, Angle, 2, NULL, 40);
            }

            if (o->SubType == 22 || o->SubType == 23)
            {
                CreateSprite(BITMAP_FLAME, o->Position, (o->Scale + (20 - o->LifeTime) / 5), Light,
                             o->Target, (float)(WorldRandom() % 360), 0);
            }
            else
                CreateSprite(BITMAP_LIGHT, o->Position, (4.0f + (20 - o->LifeTime) / 5), Light,
                             o->Target, (float)(WorldRandom() % 360), 0);
        }
        else if (8 == o->SubType)
        {
            AdvancePitchingSpirit(*o, FPS_ANIMATION_FACTOR, Random);
            AngleMatrix(o->Angle, Matrix);
        }
        else if (10 == o->SubType)
        {
            AdvanceSpinningSpirit(*o, FPS_ANIMATION_FACTOR);
            AngleMatrix(o->Angle, Matrix);
        }
        else if (o->SubType == 9)
        {
            AdvanceRisingSpirit(*o, FPS_ANIMATION_FACTOR, Random);

            if (o->LifeTime < 10)
            {
                VectorScale(o->Light, powf(1.f / 1.15f, FPS_ANIMATION_FACTOR), o->Light);
            }
        }
        else if (o->SubType == 11)
        {
            Vector(1, 1, 1, Light);
            vec3_t Position;
            VectorCopy(o->Position, Position);

            o->Angle[0] = (float)o->LifeTime; // 임시로 -_-
            CreateParticleFpsChecked(BITMAP_FIRE + 1, Position, o->Angle, Light, 5, 0.9f);
            for (auto birthTime : Emissions(FPS_ANIMATION_FACTOR / 200.f))
            {
                CreateJoint(BITMAP_JOINT_SPIRIT, Position, Position, o->Angle, 12, NULL, 0.9f);
            }
            //			CreateSprite(BITMAP_FIRE+1,Position,2.0f,Light,o->Target,(float)(WorldRandom()%360),0);
            o->Position[0] += (cosf(o->Angle[2]) * 20.0f) * FPS_ANIMATION_FACTOR;
            o->Position[1] += (sinf(o->Angle[2]) * 20.0f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 12)
        {
            Vector(1, 1, 1, Light);
            vec3_t Position;
            VectorCopy(o->Position, Position);

            Vector(1.0f, 0.2f, 0.1f, Light);
            //if (WorldRandom()%2 == 1)
            CreateParticleFpsChecked(BITMAP_SMOKE + 3, Position, o->Angle, Light, 0,
                                     (float)(WorldRandom() % 32 + 48) * 0.01f);

            Vector(1, 1, 1, Light);
            CreateParticleFpsChecked(BITMAP_FIRE + 1, Position, o->Angle, Light, 6, 0.6f);
        }
        else if (o->SubType == 14)
        {
            Vector(1, 1, 1, Light);
            vec3_t Position;
            VectorCopy(o->Position, Position);

            o->Angle[0] -= (2.0f) * FPS_ANIMATION_FACTOR;
            for (auto birthTime : Emissions(FPS_ANIMATION_FACTOR))
            {
                int r = WorldRandom() % 5;
                if (r == 0)
                    CreateParticle(BITMAP_WATERFALL_3, o->Position, o->Angle, o->Light, 2);
                else if (r <= 2)
                    CreateParticle(BITMAP_WATERFALL_4, o->Position, o->Angle, o->Light, 2);
                else
                    CreateParticle(BITMAP_WATERFALL_5, o->Position, o->Angle, o->Light, 4);
            }
        }
        else if (o->SubType == 15)
        {
            Vector(1, 1, 1, Light);
            vec3_t Position;
            VectorCopy(o->Position, Position);
            AdvanceGrowingSpirit(*o, FPS_ANIMATION_FACTOR, WorldTime);

            if (o->LifeTime < 35)
            {
                CreateParticleFpsChecked(BITMAP_FIRE, Position, o->Angle, Light, 7, 1.0f);

                for (auto birthTime : Emissions(FPS_ANIMATION_FACTOR / 2.f))
                {
                    Position[0] -= (sinf(o->Angle[2]) * (40.0f - o->SpiritPhase * 0.1f));
                    Position[1] += (cosf(o->Angle[2]) * (40.0f - o->SpiritPhase * 0.1f));
                    Position[2] = 350;
                    vec3_t Angle;

                    for (int i = 0; i < 2; ++i)
                    {
                        Vector(0.f, 0.f, i * 3.f, Angle);
                        CreateJoint(BITMAP_JOINT_SPIRIT, Position, Position, Angle, 14, NULL, 0.9f);
                    }
                }
                if (o->Target != NULL)
                {
                    if (o->Target->Angle[0] != 0)
                        o->Target->Angle[0] = 0;
                    if (o->Target->LifeTime > 5)
                        o->Target->Alpha = 1.0f;
                    VectorCopy(o->Position, Position);

                    //Position[2] = 350;
                    VectorCopy(Position, o->Target->Position);
                    if (o->Target->Angle[0] != 0)
                        o->Target->Angle[0] = 0;
                    o->Target->Angle[1] = 0;
                    //					VectorCopy(o->Angle, o->Target->Angle);
                }
            }
            //CreateParticle(BITMAP_FIRE+1,Position,o->Angle,Light,5,0.9f);
            //			CreateSprite(BITMAP_FIRE+1,Position,2.0f,Light,o->Target,(float)(WorldRandom()%360),0);
        }
        else if (o->SubType == 16)
        {
            Vector(1, 1, 1, Light);
            vec3_t Position;
            VectorCopy(o->Position, Position);

            o->Angle[0] = (float)o->LifeTime;
            CreateParticleFpsChecked(BITMAP_WATERFALL_5, o->Position, o->Angle, o->Light, 4);
            o->Position[0] += (cosf(o->Angle[2]) * 30.0f) * FPS_ANIMATION_FACTOR;
            o->Position[1] += (sinf(o->Angle[2]) * 30.0f) * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 17)
        {
            Vector(1, 1, 1, Light);
            vec3_t Position, Angle;
            VectorCopy(o->Position, Position);

            if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 100)) //% 10 == 0)
            {
                for (int i = 0; i < 120; ++i)
                {
                    Vector(0.f, 0.f, i * 3.f, Angle);
                    CreateJoint(BITMAP_JOINT_SPIRIT, Position, o->Position, Angle, 16, NULL, 0.9f);
                }
            }
        }
        else if (o->SubType == 18)
        {
            AdvanceTurningSpirit(*o, FPS_ANIMATION_FACTOR, WorldTime);
            AngleMatrix(o->Angle, Matrix);

            if (o->Target != NULL)
            {
                if (o->Target->LifeTime > 5)
                    o->Target->Alpha = 1.0f;
                VectorCopy(o->Position, o->Target->Position);
            }

            VectorCopy(o->Position, Position);
            Vector(1, 1, 1, Light);
            CreateParticleFpsChecked(BITMAP_SMOKE, Position, o->Angle, Light, 19, 7.f);
        }
        else if (o->SubType == 20)
        {
            AdvancePitchingSpirit(*o, FPS_ANIMATION_FACTOR, Random);
            AngleMatrix(o->Angle, Matrix);
        }
        else if (o->SubType == 24)
        {
            if (o->Target == NULL || o->Target->Live == false)
            {
                Joints.Retire(*o);
                break;
            }

            AdvanceWanderingSpirit(*o, FPS_ANIMATION_FACTOR, Random, *sessionKeeper_.WorldUnit(),
                                   WorldTime);
            AngleMatrix(o->Angle, Matrix);

            if (o->LifeTime < 30)
                Luminosity = o->LifeTime / 30.f;
            else if (o->LifeTime > 144)
                Luminosity = (160 - o->LifeTime) / 15.f;
            else
                Luminosity = 1.0f;
            Vector(0.7f * Luminosity, 0.7f * Luminosity, 0.9f * Luminosity, o->Light);

            if (o->Target != NULL)
            {
                VectorCopy(o->Position, o->Target->Position);
                VectorCopy(o->Angle, o->Target->Angle);
            }
        }
        else if (o->SubType == 25)
        {
            const float rising =
                (std::min)(FPS_ANIMATION_FACTOR, (std::max)(0.f, o->LifeTime - 9.f));
            constexpr float ExplosionLife = 10.f;
            if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, ExplosionLife))
            {
                const float beforeBurst = (std::max)(0.f, o->LifeTime - ExplosionLife);
                AdvanceRisingSpirit(*o, beforeBurst, Random, 16, 0.f);
                {
                    auto birth = EmissionTime(FPS_ANIMATION_FACTOR - beforeBurst);
                    CreateEffect(BITMAP_FIRECRACKER0002, o->Position, o->Angle, o->Light, o->Skill);
                }
                AdvanceRisingSpirit(*o, rising - beforeBurst, Random, 16, 0.f);
            }
            else
                AdvanceRisingSpirit(*o, rising, Random, 16, 0.f);
            o->Scale += 3.f * rising;
            const float fading = FPS_ANIMATION_FACTOR - rising;
            if (fading > 0.f)
            {
                AngleMatrix(o->Angle, Matrix);
                Vector(0.f, -o->Velocity, 0.f, p);
                VectorRotate(p, Matrix, Position);
                VectorAddScaled(o->Position, Position, o->Position, fading);
                VectorScale(o->Light, powf(1.f / 1.45f, fading), o->Light);
                if (o->Light[0] < 0.2f)
                    Joints.Retire(*o);
            }

            CreateSprite(BITMAP_LIGHT, o->Position, 0.5f, o->Light, o->Target);
            CreateSprite(BITMAP_DS_SHOCK, o->Position, 0.15f, o->Light, o->Target);
        }
        break;
    case BITMAP_JOINT_SPARK:
        if (o->SubType == 1)
        {
            o->Light[0] *= powf(1.f / 1.4f, FPS_ANIMATION_FACTOR);
            o->Light[1] = o->Light[0];
            o->Light[2] = o->Light[0];
        }
        else if (o->SubType == 3)
        {
            o->Light[0] *= powf(1.f / 1.1f, FPS_ANIMATION_FACTOR);
            o->Light[1] = o->Light[0];
            o->Light[2] = o->Light[0];
        }
        else if (o->SubType == 4)
        {
            o->Light[0] *= powf(1.f / 1.1f, FPS_ANIMATION_FACTOR);
            o->Light[1] = o->Light[0];
            o->Light[2] = o->Light[0];
        }
        break;
    case BITMAP_JOINT_FIRE: {
        AngleMatrix(o->Angle, Matrix);
        // The authored update moves twice along this same fixed direction.
        Vector(0.f, -2.f * o->Velocity, 0.f, p);
        VectorRotate(p, Matrix, Position);
        float flightFrames = FPS_ANIMATION_FACTOR;
        const bool arrived = AdvanceJointToTarget(*o, Position, o->Velocity, flightFrames);
        GameLogic::Effects::CreateTimedTail(o, Matrix, flightFrames);
        if (arrived)
        {
            auto birth = EmissionTime(FPS_ANIMATION_FACTOR - flightFrames);
            Joints.Retire(*o);
            CreateParticle(BITMAP_EXPLOTION, o->Position, o->Angle, o->Light);
            break;
        }
        Luminosity = (float)(WorldRandom() % 4 + 4) * 0.1f;
        Vector(Luminosity, Luminosity * 0.6f, Luminosity * 0.2f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 4, PrimaryTerrainLight);
        break;
    }
    case MODEL_SPEARSKILL: // 방어막
        CHARACTER *c;
        if (o->m_iChaIndex != -1)
        {
            c = &CharactersClient[o->m_iChaIndex];
            if (o->SubType == 4)
            {
                if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
                    o->SubType = 9;
            }
            else if (o->SubType == 9)
            {
                if (c->Helper.Type != MODEL_HORN_OF_FENRIR || c->SafeZone)
                    o->SubType = 4;
            }
            else if (o->SubType == 10)
            {
                if ((c->Helper.Type == MODEL_HORN_OF_UNIRIA ||
                     c->Helper.Type == MODEL_HORN_OF_DINORANT ||
                     c->Helper.Type == MODEL_DARK_HORSE_ITEM ||
                     c->Helper.Type == MODEL_HORN_OF_FENRIR) &&
                    c->SafeZone == false)
                {
                    o->SubType = 11;
                }
            }
            else if (o->SubType == 11)
            {
                if ((c->Helper.Type != MODEL_HORN_OF_UNIRIA &&
                     c->Helper.Type != MODEL_HORN_OF_DINORANT &&
                     c->Helper.Type != MODEL_DARK_HORSE_ITEM &&
                     c->Helper.Type != MODEL_HORN_OF_FENRIR) ||
                    c->SafeZone == true)
                {
                    o->SubType = 10;
                }
            }
        }
        if (2 == o->SubType)
        {
            if (!o->Target || !o->Target->Live)
            {
                o->Target = NULL;
                break;
            }
            o->Scale = (float)o->LifeTime * 3.0f;
            float fRate1 =
                std::max<float>(0.0f, std::min<float>((float)(o->LifeTime - 10) / (float)10, 1.0f));
            float fRate2 = 1.0f - fRate1;

            vec3_t MagicPos;
            GetMagicScrew(iIndex * 17721, MagicPos, 1.4f);
            //VectorScale(MagicPos, 300.0f, MagicPos);
            VectorAdd(MagicPos, o->TargetPosition, MagicPos);
            vec3_t TargetPos;
            VectorCopy(o->Target->m_vPosSword, TargetPos);
            //TargetPos[2] += (120.0f) * FPS_ANIMATION_FACTOR;

            for (int i = 0; i < 3; ++i)
            {
                o->Position[i] = fRate2 * TargetPos[i] + fRate1 * MagicPos[i];
            }
        }
        else if (o->SubType == 3)
        {
            if (!o->Target->Live)
            {
                o->LifeTime = 0;
                Joints.Retire(*o);
                break;
            }
            if (rand_fps_check(5))
            {
                o->LifeTime -= 5 * FPS_ANIMATION_FACTOR;
            }
            else if (rand_fps_check(2))
            {
                o->LifeTime += FPS_ANIMATION_FACTOR;
            }
            o->Direction[0] = sinf((o->LifeTime - o->Direction[1]) * 0.05f) * 3.f;
            o->Position[0] += (o->Direction[0]) * FPS_ANIMATION_FACTOR;

            float fAlpha;
            if (o->Target != NULL)
            {
                if (o->Target->LifeTime > 50)
                {
                    fAlpha = (100 - o->Target->LifeTime) / 40.f;
                }
                else
                {
                    fAlpha = o->Target->LifeTime / 10.f;
                }

                if (fAlpha > 1.f)
                    fAlpha = 1.f;

                o->Light[0] = o->Target->Light[0] * fAlpha;
                o->Light[1] = o->Target->Light[1] * fAlpha;
                o->Light[2] = o->Target->Light[2] * fAlpha;
            }
        }
        else if (o->SubType == 5 || o->SubType == 6 || o->SubType == 7)
        {
            float remaining = FPS_ANIMATION_FACTOR;
            while (remaining > 0.f)
            {
                if (o->SpiritMotionFrames <= 0.f)
                {
                    o->SpiritMotionFrames = 1.f;
                    o->MotionScale = o->Scale;
                }
                if (o->MotionFrames <= 0.f)
                {
                    float radius = o->Direction[1], weapon = std::round(o->Weapon);
                    float height = o->StartPosition[2], scale = o->MotionScale;
                    vec3_t angle, radial{0.f, radius, 0.f}, endpoint;
                    VectorCopy(o->Angle, angle);
                    angle[2] += 10.f;
                    AngleMatrix(angle, Matrix);
                    VectorRotate(radial, Matrix, endpoint);
                    VectorAdd(o->StartPosition, endpoint, endpoint);
                    if (weapon == 0.f)
                    {
                        radius *= 0.95f;
                        if (radius < 10.f)
                            radius = -10.f;
                    }
                    if (weapon > 40.f)
                    {
                        radius -= 20.f;
                        angle[2] -= 5.f;
                        scale += 15.f;
                    }
                    o->MotionChecksTarget = radius < 0.f && weapon == 40.f && o->SubType <= 6;
                    if (radius < 0.f)
                    {
                        weapon += 1.f;
                        if (weapon < 20.f)
                        {
                            radius = -30.f;
                            scale = 40.f;
                        }
                        else
                            height += 5.f;
                    }
                    VectorSubtract(endpoint, o->Position, o->MotionVelocity);
                    o->MotionAngleRate[0] = angle[2] - o->Angle[2];
                    o->MotionAngleRate[1] = radius - o->Direction[1];
                    o->MotionAngleRate[2] = height - o->StartPosition[2];
                    o->MotionAcceleration = weapon - o->Weapon;
                    // Direction X/Z are zero in the authored radial vector; X holds the scale change.
                    o->Direction[0] = scale - o->MotionScale;
                    o->MotionFrames = 1.f;
                }
                const float step =
                    (std::min)(remaining, (std::min)(o->SpiritMotionFrames, o->MotionFrames / 3.f));
                const float sample = step * 3.f;
                VectorAddScaled(o->Position, o->MotionVelocity, o->Position, sample);
                o->Angle[2] += o->MotionAngleRate[0] * sample;
                o->Direction[1] += o->MotionAngleRate[1] * sample;
                o->StartPosition[2] += o->MotionAngleRate[2] * sample;
                o->Weapon += o->MotionAcceleration * sample;
                o->MotionScale += o->Direction[0] * sample;
                o->MotionFrames = (std::max)(0.f, o->MotionFrames - sample);
                o->SpiritMotionFrames = (std::max)(0.f, o->SpiritMotionFrames - step);
                remaining -= step;
                vec3_t tailAngle;
                VectorCopy(o->Angle, tailAngle);
                tailAngle[2] += (10.f - o->MotionAngleRate[0]) * (1.f - o->MotionFrames);
                AngleMatrix(tailAngle, Matrix);
                o->Scale = o->MotionScale - o->Direction[0] * (1.f - o->MotionFrames);
                GameLogic::Effects::CreateTimedTail(o, Matrix, sample);
                o->Scale = o->MotionScale - 5.f * (1.f - o->SpiritMotionFrames);
                if (o->MotionFrames <= 0.f && o->MotionChecksTarget)
                {
                    vec3_t burst;
                    VectorCopy(o->StartPosition, burst);
                    burst[2] -= o->MotionAngleRate[2];
                    CreateEffect(BITMAP_SHOCK_WAVE, burst, o->Angle, o->Light, 3);
                    o->MotionChecksTarget = false;
                }
            }
            const float fading =
                (std::max)(0.f, FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 9.f));
            VectorScale(o->Light, powf(1.f / 1.2f, fading), o->Light);
        }
        else if (o->SubType == 8)
        {
            o->Angle[2] += (25.f) * FPS_ANIMATION_FACTOR;
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(o->Direction, Matrix, Position);
            o->StartPosition[2] += 15.f * FPS_ANIMATION_FACTOR;
            VectorAdd(o->StartPosition, Position, o->Position);
            const float age = (std::max)(0.f, 40.f - o->LifeTime + FPS_ANIMATION_FACTOR);
            o->Position[2] -= 15.f * (std::min)(1.f, age);
            GameLogic::Effects::CreateTimedTail(o, Matrix, FPS_ANIMATION_FACTOR);
        }
        else
        {
            if (!o->Target->Live)
            {
                o->LifeTime = 0;
                Joints.Retire(*o);
                break;
            }

            if (o->SubType == 10 || o->SubType == 11)
            {
                float fLumi = (float)(WorldRandom() % 4 + 4) * 0.1f;
                Vector(fLumi * 1.5f, fLumi * 0.6f, fLumi * 0.6f, Light);
                AddTerrainLight(o->Position[0], o->Position[1], Light, 1, PrimaryTerrainLight);

                for (auto birthTime : Emissions(FPS_ANIMATION_FACTOR / 10.f))
                {
                    const float sparkLight = fLumi * 0.2f;
                    Vector(0.9f + sparkLight, 0.5f + sparkLight, 0.5f + sparkLight, Light);
                    CreateParticle(BITMAP_SPARK + 1, o->Position, o->Angle, Light, 19);
                }
            }

            if (o->SubType == 4 || o->SubType == 9)
            {
                if (g_isCharacterBuff(o->Target, eBuff_Defense) ||
                    g_isCharacterBuff(o->Target, eBuff_HelpNpc))
                {
                    o->LifeTime = 100;
                }
                else
                {
                    o->LifeTime = 0;
                    Joints.Retire(*o);
                    break;
                }
            }

            if (0 == o->SubType || o->SubType == 4 || o->SubType == 9 || o->SubType == 10 ||
                o->SubType == 11 || o->SubType == 14 || o->SubType == 16)
            {
                for (int j = o->NumTails - 1; j >= 0; j--)
                {
                    for (int k = 0; k < 4; k++)
                        VectorSubtract(o->Tails[j][k], o->TargetPosition, o->Tails[j][k]);
                }
            }

            if (o->SubType == 14)
            {
                vec3_t vRelative;
                Vector(0, 0, 0, vRelative);
                BMD *pModel = &Models[o->Target->Type];
                pModel->TransformPosition(o->Target->BoneTransform[37], vRelative,
                                          o->TargetPosition, false);
                VectorScale(o->TargetPosition, pModel->BodyScale, o->TargetPosition);
                VectorAdd(o->Target->Position, o->TargetPosition, o->TargetPosition);
            }
            else if (o->SubType == 15 || o->SubType == 17)
            {
                if (o->Target->Owner == NULL)
                {
                    Joints.Retire(*o);
                    break;
                }

                for (int j = o->NumTails - 1; j >= 0; j--)
                {
                    for (int k = 0; k < 4; k++)
                        VectorSubtract(o->Tails[j][k], o->StartPosition, o->Tails[j][k]);
                }
                VectorCopy(o->Target->Owner->Position, o->StartPosition);
                VectorCopy(o->Target->Position, o->TargetPosition);

                for (int j = o->NumTails - 1; j >= 0; j--)
                {
                    for (int k = 0; k < 4; k++)
                        VectorAdd(o->Tails[j][k], o->StartPosition, o->Tails[j][k]);
                }
            }
            else
            {
                VectorCopy(o->Target->Position, o->TargetPosition);
                o->TargetPosition[2] += 10.f;
            }

            if (0 == o->SubType || o->SubType == 4 || o->SubType == 9 || o->SubType == 10 ||
                o->SubType == 11 || o->SubType == 14 || o->SubType == 16)
            {
                for (int j = o->NumTails - 1; j >= 0; j--)
                {
                    for (int k = 0; k < 4; k++)
                        VectorAdd(o->Tails[j][k], o->TargetPosition, o->Tails[j][k]);
                }
            }
            double phase = Core::Time::ReferenceFrames(
                WorldTime, sessionKeeper_.ApplicationConfig().legacyReferenceFps);
            phase = ((iIndex % 2) ? phase : -phase) + static_cast<double>(iIndex) * 53731;
            vec3_t vDir;
            const float fSinAdd = ScrewDirection(phase, vDir, o->SubType == 1 ? 0.5f : 1.f);

            switch (o->SubType)
            {
            case 0:
            case 4:
                o->Position[0] =
                    o->TargetPosition[0] + vDir[0] * 80.0f; // + ( float)( WorldRandom() % 11 - 5);
                o->Position[1] =
                    o->TargetPosition[1] + vDir[1] * 80.0f; // + ( float)( WorldRandom() % 11 - 5);
                o->Position[2] = 110.0f + o->TargetPosition[2] + vDir[2] * 120.0f;
                break;
            case 9:
                o->Position[0] =
                    o->TargetPosition[0] + vDir[0] * 80.0f; // + ( float)( WorldRandom() % 11 - 5);
                o->Position[1] =
                    o->TargetPosition[1] + vDir[1] * 80.0f; // + ( float)( WorldRandom() % 11 - 5);
                o->Position[2] = 140.0f + o->TargetPosition[2] + vDir[2] * 120.0f;
                break;
            case 10:
                o->Position[0] =
                    o->TargetPosition[0] + vDir[0] * 60.0f; // + ( float)( WorldRandom() % 11 - 5);
                o->Position[1] =
                    o->TargetPosition[1] + vDir[1] * 60.0f; // + ( float)( WorldRandom() % 11 - 5);
                o->Position[2] = 50.0f + o->TargetPosition[2] + vDir[2] * 60.0f;
                break;
            case 11:
                o->Position[0] =
                    o->TargetPosition[0] + vDir[0] * 60.0f; // + ( float)( WorldRandom() % 11 - 5);
                o->Position[1] =
                    o->TargetPosition[1] + vDir[1] * 60.0f; // + ( float)( WorldRandom() % 11 - 5);
                o->Position[2] = 100.0f + o->TargetPosition[2] + vDir[2] * 60.0f;
                break;
            case 1:
                o->Position[0] =
                    o->TargetPosition[0] + vDir[0] * 70.0f; // + ( float)( WorldRandom() % 11 - 5);
                o->Position[1] =
                    o->TargetPosition[1] + vDir[1] * 70.0f; // + ( float)( WorldRandom() % 11 - 5);
                o->Position[2] = o->TargetPosition[2] + vDir[2] * 140.0f;

                Vector(0.2f, 0.2f, 0.4f + 0.2f * fSinAdd, o->Light);
                break;
            case 14: // 소환 손목링
                if (o->Target != NULL)
                {
                    if (o->Target->Live)
                        o->LifeTime = 100.f; //무한
                    else
                    {
                        DeleteJoint(MODEL_SPEARSKILL, o->Target, 14);
                        break;
                    }

                    o->Position[0] = o->TargetPosition[0] + vDir[0] * 15.0f;
                    o->Position[1] = o->TargetPosition[1] + vDir[1] * 15.0f;
                    o->Position[2] = o->TargetPosition[2] + vDir[2] * 15.0f;
                }
                break;
            case 15:
                if (o->Target != NULL)
                {
                    if (o->Target->Live)
                    {
                        o->LifeTime = 100.f;
                        Vector(o->Target->Alpha, o->Target->Alpha, o->Target->Alpha, o->Light);
                    }
                    else
                    {
                        DeleteJoint(MODEL_SPEARSKILL, o->Target, 15);
                        break;
                    }
                    VectorCopy(o->TargetPosition, o->Position);
                }
                break;
            case 16: {
                if (o->Target != NULL)
                {
                    o->Position[0] = o->TargetPosition[0] +
                                     vDir[0] * 20.0f; // + ( float)( WorldRandom() % 11 - 5);
                    o->Position[1] = o->TargetPosition[1] +
                                     vDir[1] * 20.0f; // + ( float)( WorldRandom() % 11 - 5);
                    o->Position[2] = 100.0f + o->TargetPosition[2] + vDir[2] * 40.0f;
                }
            }
            break;
            case 17:
                if (o->Target != NULL)
                {
                    if (o->Target->Live)
                    {
                        o->LifeTime = 100.f;
                    }
                    else
                    {
                        DeleteJoint(MODEL_SPEARSKILL, o->Target, 17);
                        break;
                    }
                    VectorCopy(o->TargetPosition, o->Position);
                }
                break;
            }
        }
        break;
    case BITMAP_SMOKE:
        if (o->Target != NULL && o->Target->Live &&
            (o->Target->Type == MODEL_MAYASTONE1 || o->Target->Type == MODEL_MAYASTONE2 ||
             o->Target->Type == MODEL_MAYASTONE3 || o->Target->Type == MODEL_FIRE))
        {
            VectorCopy(o->Target->Position, o->Position);
        }
        break;
    case MODEL_FENRIR_SKILL_THUNDER:
    case BITMAP_BLUR + 1:
    case BITMAP_JOINT_LASER + 1: {
        MoveLightningRibbon(o);
        AngleMatrix(o->Angle, Matrix);
        break;
    }
    case BITMAP_JOINT_THUNDER: {
        if (OwnsMovingLightning(*o))
        {
            MoveLightningRibbon(o);
            break;
        }
        const bool rebuilt = RebuildsThunderGeometry(*o);
        const float sampleStep = rebuilt ? 1.f : FPS_ANIMATION_FACTOR;
        if ((o->SubType == 6 && o->LifeTime > 4) || o->SubType == 8)
            break;

        switch (o->SubType)
        {
        case 4:
        case 5:
            VectorCopy(o->TargetPosition, o->Position);
            if (o->SubType == 4)
            {
                VectorCopy(o->TargetPosition, Position);
                Position[2] += (30.f);
                CreateSprite(BITMAP_SHINY + 1, Position, (float)(WorldRandom() % 8 + 8) * 0.2f,
                             o->Light, NULL, (float)(WorldRandom() % 360));
            }
            break;

        case 9:
            Position[0] = o->TargetPosition[0] + WorldRandom() % 200 - 100;
            Position[1] = o->TargetPosition[1] + WorldRandom() % 200 - 100;
            Position[2] = o->TargetPosition[2];
        case 6:
        case 7:
        case 18:
        case 19:
            VectorCopy(o->StartPosition, o->Position);
            break;
        }

        for (int j = 0; j < o->MaxTails; j++)
        {
            if (o->SubType == 15)
                break;

            switch (o->SubType)
            {
            case 0:
            case 1:
            case 2:
            case 16:
            case 21:
            case 27:
            case 28:
            case 33:
                if (o->Target)
                {
                    VectorCopy(o->Target->Position, o->TargetPosition);
                    o->TargetPosition[2] += 80.f;
                }

                Distance =
                    ::MoveHumming(o->Position, o->Angle, o->TargetPosition, (50.f) * sampleStep);

                break;
            case 3:
                Distance =
                    ::MoveHumming(o->Position, o->Angle, o->TargetPosition, (50.f) * sampleStep);
                break;

            case 4:
                Position[0] = o->TargetPosition[0];
                Position[1] = o->TargetPosition[1];
                Position[2] = o->TargetPosition[2] - 300.f;

                Distance =
                    ::MoveHumming(o->Position, o->Angle, Position, (-10) * sampleStep); //-25.f);
                break;
            case 5:
                Position[0] = o->TargetPosition[0];
                Position[1] = o->TargetPosition[1];
                Position[2] = o->TargetPosition[2] + 600.f;

                Distance =
                    ::MoveHumming(Position, o->Angle, o->Position, (-10) * sampleStep); //-25.f);
                break;

            case 6:
                VectorCopy(o->Target->Position, o->TargetPosition);
                o->TargetPosition[0] += ((2050.f + WorldRandom() % 200));
                o->TargetPosition[1] += ((2050.f + WorldRandom() % 200));
                o->TargetPosition[2] -= (10000.f);

                Distance =
                    ::MoveHumming(o->Position, o->Angle, o->TargetPosition,
                                  ((float)(WorldRandom() % 100 + 50)) * sampleStep); //-25.f);
                break;

            case 7:
                Distance = ::MoveHumming(o->Position, o->Angle, o->TargetPosition,
                                         ((float)(WorldRandom() % 100 + 50)) * sampleStep);
                break;

            case 9:
                Distance = ::MoveHumming(o->Position, o->Angle, Position,
                                         ((float)(WorldRandom() % 80 + 60.f)) * sampleStep);
                break;
            case 12: {
                BMD *b = &Models[o->Target->Type];
                vec3_t Pos1;
                Vector(0.0f, -100.0f, 0.0f, Pos1);
                VectorTransform(Pos1, o->Target->BoneTransform[3], o->Position);
                VectorScale(o->Position, o->Target->Scale, o->Position);
                VectorAdd(o->Position, o->Target->Position, o->Position);
            }
            break;
            case 18:
                Distance =
                    ::MoveHumming(o->Position, o->Angle, o->TargetPosition, (110.f) * sampleStep);
                break;
            case 19:
                Distance = ::MoveHumming(o->Position, o->Angle, o->TargetPosition,
                                         (25.f + o->Scale) * sampleStep);
                break;
            case 20:
                o->TargetPosition[2] += 100.f * sampleStep;
                Distance =
                    ::MoveHumming(o->Position, o->Angle, o->TargetPosition, (100.f) * sampleStep);
                break;
                // ChainLighting
            case 22:
            case 23:
            case 24: {
                if (o->Target)
                {
                    if (j == 0) // ????
                    {
                        OBJECT *pSourceObj = o->Target;
                        CHARACTER *pTargetChar =
                            &CharactersClient[FindCharacterIndex(o->m_sTargetIndex)];
                        OBJECT *pTargetObj = &pTargetChar->Object;
                        vec3_t vRelativePos, vPos, vAngle;
                        BMD *pModel = &Models[pSourceObj->Type];
                        Vector(0.f, 0.f, 0.f, vRelativePos);
                        if (o->SubType == 22)
                        {
                            pModel->TransformPosition(pSourceObj->BoneTransform[37], vRelativePos,
                                                      vPos, true);
                        }
                        else if (o->SubType == 23)
                        {
                            pModel->TransformPosition(pSourceObj->BoneTransform[28], vRelativePos,
                                                      vPos, true);
                        }
                        else if (o->SubType == 24)
                        {
                            VectorCopy(pSourceObj->Position, vPos);
                            vPos[2] += (80.0f);
                        }

                        o->Direction[0] = (float)(WorldRandom() % 1024 - 512 / o->Scale);
                        o->Direction[2] = (float)(WorldRandom() % 1024 - 512 / o->Scale);
                        VectorAdd(o->Angle, o->Direction, vAngle);
                        float fMatrix[3][4];
                        AngleMatrix(vAngle, fMatrix);
                        GameLogic::Effects::CreateTail(o, fMatrix);

                        VectorCopy(vPos, o->Position);
                        VectorCopy(pTargetObj->Position, o->TargetPosition);

                        o->TargetPosition[2] += 80.f;
                    }

                    Distance = ::MoveHumming(o->Position, o->Angle, o->TargetPosition,
                                             (o->Velocity) * sampleStep);
                }
                else
                {
                    assert(!"Debuggen");
                }
            }
            break;
            case 25: {
                if (o->Target)
                {
                    VectorCopy(o->Target->Position, o->TargetPosition);
                    o->TargetPosition[2] += 80.f;
                }

                Distance =
                    ::MoveHumming(o->Position, o->Angle, o->TargetPosition, (40.f) * sampleStep);
            }
            break;
            default:
                Distance =
                    ::MoveHumming(o->Position, o->Angle, o->TargetPosition, (25.f) * sampleStep);
                break;
            }

            if (o->SubType == 1 || o->SubType == 28)
            {
                o->Direction[0] = (float)(WorldRandom() % 256 - 128);
                o->Direction[2] = (float)(WorldRandom() % 256 - 128);
            }
            else if (o->SubType == 4)
            {
                o->Direction[0] = (float)(WorldRandom() % 64 - 32);
            }
            else if (o->SubType == 5)
            {
                o->Direction[0] = (float)(WorldRandom() % 32 - 16);
            }
            else if (o->SubType == 6)
            {
                o->Direction[0] = (float)(WorldRandom() % 100 + 20);
                o->Direction[2] = (float)(WorldRandom() % 100 + 20);
            }
            else if (o->SubType == 7)
            {
                o->Direction[0] = (float)(WorldRandom() % 100 + 20);
                o->Direction[2] = (float)(WorldRandom() % 100 + 20);
            }
            else if (o->SubType == 11)
            {
                o->Direction[0] = (float)(WorldRandom() % 1024 - 512) * 0.7f / o->Scale;
                o->Direction[2] = (float)(WorldRandom() % 1024 - 512) * 0.7f / o->Scale;
            }
            else if (o->SubType == 18)
            {
                o->Direction[0] = (float)(WorldRandom() % 64 - 32);
                o->Direction[2] = (float)(WorldRandom() % 64 - 32);
            }
            else if (o->SubType == 19)
            {
                o->Direction[0] = (float)(WorldRandom() % 1024 - 512) / o->Scale;
                o->Direction[1] = (float)(WorldRandom() % 1024 - 512) / o->Scale;
                o->Direction[2] = (float)(WorldRandom() % 1024 - 512) / o->Scale;
            }
            else
            {
                o->Direction[0] = (float)(WorldRandom() % 1024 - 512) / o->Scale;
                o->Direction[2] = (float)(WorldRandom() % 1024 - 512) / o->Scale;
            }

            vec3_t Angle;
            VectorAdd(o->Angle, o->Direction, Angle);
            float Matrix[3][4];
            AngleMatrix(Angle, Matrix);
            if (rebuilt)
                GameLogic::Effects::CreateTail(o, Matrix);
            else
                GameLogic::Effects::CreateTimedTail(o, Matrix, FPS_ANIMATION_FACTOR);

            if (o->SubType == 3)
            {
                if (Distance > 150)
                {
                    o->LifeTime = 0;
                }
            }
            else if (o->SubType != 12 && Distance < o->Velocity * 1.5f &&
                     (o->SubType != 6 && o->SubType != 9 && o->SubType != 7 && o->SubType != 18 &&
                      o->SubType != 19))
            {
                if (o->Scale == 50.f)
                    EmitLightningArrival(o, FPS_ANIMATION_FACTOR, 0.f);
                break;
            }

            if (o->Scale >= 50.f && o->SubType != 4 && o->SubType != 7)
            {
                if (o->SubType == 0 || o->SubType == 11 || o->SubType == 2 || o->SubType == 18 ||
                    o->SubType == 27)
                {
                    Luminosity = (float)(WorldRandom() % 4 + 4) * 0.04f;

                    if (o->SubType == 0 || o->SubType == 11 || o->SubType == 18 || o->SubType == 27)
                    {
                        Vector(Luminosity * 0.1f, Luminosity * 0.1f, Luminosity * 0.5f, Light);
                    }
                    else
                    {
                        Vector(Luminosity * 0.4f, Luminosity * 0.1f, Luminosity * 0.1f, Light);
                    }

                    if (o->SubType == 27)
                        VectorCopy(o->Light, Light);

                    AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
                }
            }

            Vector(0.f, -o->Velocity, 0.f, Position);
            VectorRotate(Position, Matrix, p);
            VectorAddScaled(o->Position, p, o->Position, sampleStep);
        }
        if (o->SubType == 7 || o->SubType == 18)
        {
            VectorCopy(o->TargetPosition, o->Position);
        }

        else if (o->SubType == 15 &&
                 Core::Time::Periods(o->LifeTime, FPS_ANIMATION_FACTOR, 2.f) > 0)
        {
            CreateJoint(BITMAP_JOINT_THUNDER, o->Position, o->StartPosition, o->Angle, 0, NULL,
                        50.f);
            CreateJoint(BITMAP_JOINT_THUNDER, o->Position, o->StartPosition, o->Angle, 0, NULL,
                        10.f);
            CreateParticle(BITMAP_ENERGY, o->Position, o->Angle, Light);

            vec3_t Angle;
            VectorCopy(o->StartPosition, Position);
            Position[2] += (100.f);

            for (int i = 0; i < o->MultiUse; ++i)
            {
                if ((i % 15) == 0)
                {
                    CHARACTER *tc = &CharactersClient[o->TargetIndex[i / 15]];
                    OBJECT *to = &tc->Object;

                    if (to->Live && tc->Dead == 0 && to->Kind == KIND_MONSTER && to->Visible)
                    {
                        VectorCopy(to->Position, o->TargetPosition);
                        VectorCopy(o->Angle, Angle);
                        o->TargetPosition[2] += 100.f;
                        Angle[2] = CreateAngle2D(o->Position, o->TargetPosition);

                        CreateJoint(BITMAP_JOINT_THUNDER, Position, o->TargetPosition, Angle, 0,
                                    NULL, 50.f);
                        CreateJoint(BITMAP_JOINT_THUNDER, Position, o->TargetPosition, Angle, 0,
                                    NULL, 10.f);

                        VectorCopy(o->TargetPosition, Position);
                    }
                }
            }

            if (o->MultiUse < o->Weapon)
            {
                if (Core::Time::Periods(o->LifeTime, FPS_ANIMATION_FACTOR, 2.f) > 0)
                    o->MultiUse += FPS_ANIMATION_FACTOR;
            }
        }
        break;
    }
    case BITMAP_JOINT_THUNDER + 1:
        if (o->SubType == 0)
        {
            if (o->LifeTime > 15)
            {
                Position[0] = o->TargetPosition[0] + WorldRandom() % 200 - 100;
                Position[1] = o->TargetPosition[1] + WorldRandom() % 100 - 50;
                Position[2] = o->TargetPosition[2];

                VectorCopy(o->StartPosition, o->Position);

                for (int j = 0; j < o->MaxTails; j++)
                {
                    Distance = ::MoveHumming(o->Position, o->Angle, Position,
                                             (float)(WorldRandom() % 80 + 60.f));

                    o->Direction[0] = (float)(WorldRandom() % 1400 - 700) / o->Scale;
                    o->Direction[2] = (float)(WorldRandom() % 1400 - 700) / o->Scale;

                    vec3_t Angle;
                    VectorAdd(o->Angle, o->Direction, Angle);
                    float Matrix[3][4];
                    AngleMatrix(Angle, Matrix);
                    GameLogic::Effects::CreateTail(o, Matrix);

                    Vector(0.f, -o->Velocity, 0.f, p);
                    vec3_t vTempPos;
                    VectorCopy(p, vTempPos);
                    VectorRotate(vTempPos, Matrix, p);
                    VectorAdd(o->Position, p, o->Position);
                }
            }
            if (o->LifeTime > 15.f)
            {
                AngleMatrix(o->Angle, Matrix);
                GameLogic::Effects::CreateTail(o, Matrix);
                o->TailSampleFrames = 0.f;
                std::memcpy(o->LastTailSample, o->Tails[0], sizeof(o->LastTailSample));
            }
            const float fadeFrames =
                (std::max)(0.f, FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 15.f));
            if (fadeFrames > 0.f)
            {
                // The rebuilt bolt ends at life 15; only the remaining time drifts.
                AngleMatrix(o->Angle, Matrix);
                Vector(0.f, -o->Velocity * fadeFrames, 0.f, p);
                VectorRotate(p, Matrix, Position);
                VectorAdd(o->Position, Position, o->Position);
                o->Light[2] -= 10.12f * fadeFrames;
                GameLogic::Effects::CreateTimedTail(o, Matrix, fadeFrames);
            }
        }
        else if (o->SubType == 1 || o->SubType == 2 || o->SubType == 3 || o->SubType == 5 ||
                 o->SubType == 6 || o->SubType == 7) //  위에서 아래로 내려오는 번개.

        {
            VectorCopy(o->StartPosition, o->Position);
            AngleMatrix(o->Angle, Matrix);
            for (int i = 0; i < o->MaxTails - 5; ++i)
            {
                if (o->SubType == 6)
                {
                    o->Position[0] += (WorldRandom() % 20 - 10);
                    o->Position[1] += (WorldRandom() % 20 - 10);
                    o->Position[2] -= (13.f);
                }
                else
                {
                    o->Position[0] += (WorldRandom() % 20 - 10);
                    o->Position[1] += (WorldRandom() % 20 - 10);

                    if (o->SubType == 7)
                    {
                        o->Position[2] -= (20.0f);
                    }
                    else
                    {
                        o->Position[2] -= (16.f);
                    }
                }
                GameLogic::Effects::CreateTail(o, Matrix);
            }
            VectorSubtract(o->TargetPosition, o->Position, Position);
            VectorScale(Position, 0.2f, Position);

            for (int i = o->MaxTails - 5; i < o->MaxTails - 1; ++i)
            {
                VectorAdd(o->Position, Position, o->Position);
                o->Position[0] += (WorldRandom() % 20 - 10);
                o->Position[1] += (WorldRandom() % 20 - 10);
                GameLogic::Effects::CreateTail(o, Matrix);
            }
            VectorCopy(o->TargetPosition, o->Position);
            GameLogic::Effects::CreateTail(o, Matrix);

            if (o->LifeTime < 4)
            {
                VectorScale(o->Light, powf(1.f / 1.2f, FPS_ANIMATION_FACTOR), o->Light);
            }

            if (o->SubType == 2)
            {
                Vector(o->Light[0] * 0.5f, o->Light[1] * 0.6f, o->Light[2], Light);

                CreateSprite(BITMAP_SHINY + 1, o->TargetPosition, 1.5f, Light, NULL,
                             (float)((WorldRandom() % 360)));
                CreateSprite(BITMAP_SHINY + 1, o->TargetPosition, 1.5f, Light, NULL,
                             (float)((WorldRandom() % 360)));
                CreateParticleFpsChecked(BITMAP_TRUE_BLUE, o->TargetPosition, o->Angle, Light);
            }
            else if (o->SubType == 3)
            {
                Vector(o->Light[0] * 0.5f, o->Light[1] * 0.6f, o->Light[2], Light);

                CreateEffect(MODEL_ICE, o->TargetPosition, o->Angle, Light);
            }
            else if (o->SubType == 6)
            {
                constexpr float GroundFlashDecay = 1.f / 1.1f;
                o->Velocity *= powf(GroundFlashDecay, FPS_ANIMATION_FACTOR);
                o->TargetPosition[2] =
                    RequestTerrainHeight(o->TargetPosition[0], o->TargetPosition[1]) + 30.f;
                CreateParticleFpsChecked(BITMAP_TRUE_BLUE, o->TargetPosition, o->Angle, o->Light, 0,
                                         2.0f);
            }
        }
        else if (o->SubType == 4)
        {
            if (o->Target != NULL)
            {
                VectorCopy(o->Target->Position, o->Position);
                AngleMatrix(o->Angle, Matrix);

                VectorSubtract(o->TargetPosition, o->Position, Position);
                VectorScale(Position, o->StartPosition[0], Position);

                for (int i = 0; i < o->TargetIndex[0]; ++i)
                {
                    GameLogic::Effects::CreateTail(o, Matrix);
                    VectorAdd(o->Position, Position, o->Position);
                    o->Position[0] += (WorldRandom() % 20 - 10);
                    o->Position[1] += (WorldRandom() % 20 - 10);
                    o->Position[2] += (WorldRandom() % 20 - 10);
                }

                float width = o->TargetIndex[1] / 2.f;

                VectorSubtract(o->TargetPosition, o->Position, Position);
                VectorScale(Position, o->StartPosition[1], Position);

                for (int i = o->TargetIndex[0]; i < o->TargetIndex[1]; ++i)
                {
                    VectorAdd(o->Position, Position, o->Position);
                    GameLogic::Effects::CreateTail(o, Matrix);
                }
                VectorCopy(o->TargetPosition, o->Position);
                GameLogic::Effects::CreateTail(o, Matrix);

                CreateSprite(BITMAP_SHINY + 1, o->TargetPosition, 1.5f, o->Light, NULL,
                             (float)((WorldRandom() % 360)));
                CreateSprite(BITMAP_SHINY + 1, o->TargetPosition, 1.5f, o->Light, NULL,
                             (float)((WorldRandom() % 360)));
                if (o->LifeTime < 8)
                {
                    VectorScale(o->Light, powf(1.f / 1.4f, FPS_ANIMATION_FACTOR), o->Light);
                }
            }
            else
            {
                Joints.Retire(*o);
            }
        }
        else if (o->SubType == 8)
        {
            VectorCopy(o->StartPosition, o->Position);
            AngleMatrix(o->Angle, Matrix);
            for (int i = 0; i < o->MaxTails - 5; ++i)
            {
                o->Position[0] += (WorldRandom() % 10 - 5);
                o->Position[1] += (WorldRandom() % 10 - 5);
                o->Position[2] -= (20.0f);
                GameLogic::Effects::CreateTail(o, Matrix);
            }
            VectorSubtract(o->TargetPosition, o->Position, Position);
            VectorScale(Position, 0.2f, Position);

            for (int i = o->MaxTails - 5; i < o->MaxTails - 1; ++i)
            {
                VectorAdd(o->Position, Position, o->Position);
                o->Position[0] += (WorldRandom() % 10 - 5);
                o->Position[1] += (WorldRandom() % 10 - 5);
                GameLogic::Effects::CreateTail(o, Matrix);
            }
            VectorCopy(o->TargetPosition, o->Position);
            GameLogic::Effects::CreateTail(o, Matrix);

            if (o->LifeTime < 4)
            {
                VectorScale(o->Light, powf(1.f / 1.2f, FPS_ANIMATION_FACTOR), o->Light);
            }
        }
        else if (o->SubType == 9)
        {
            VectorCopy(o->StartPosition, o->Position);
            AngleMatrix(o->Angle, Matrix);
            for (int i = 0; i < o->MaxTails - 5; ++i)
            {
                o->Position[0] += (21.0f);
                GameLogic::Effects::CreateTail(o, Matrix);
            }
            VectorSubtract(o->TargetPosition, o->Position, Position);
            VectorScale(Position, 0.2f, Position);

            for (int i = o->MaxTails - 5; i < o->MaxTails - 1; ++i)
            {
                VectorAdd(o->Position, Position, o->Position);
                o->Position[1] += (WorldRandom() % 20 - 10);
                o->Position[2] += (WorldRandom() % 20 - 10);
                GameLogic::Effects::CreateTail(o, Matrix);
            }
            VectorCopy(o->TargetPosition, o->Position);
            GameLogic::Effects::CreateTail(o, Matrix);

            if (o->LifeTime < 4)
            {
                VectorScale(o->Light, powf(1.f / 1.2f, FPS_ANIMATION_FACTOR), o->Light);
            }
        }
        else if (o->SubType == 10)
        {
            VectorCopy(o->StartPosition, o->Position);
            AngleMatrix(o->Angle, Matrix);
            for (int i = 0; i < o->MaxTails - 5; ++i)
            {
                o->Position[1] += (20.0f);
                GameLogic::Effects::CreateTail(o, Matrix);
            }
            VectorSubtract(o->TargetPosition, o->Position, Position);
            VectorScale(Position, 0.2f, Position);

            for (int i = o->MaxTails - 5; i < o->MaxTails - 1; ++i)
            {
                VectorAdd(o->Position, Position, o->Position);
                o->Position[0] += (WorldRandom() % 20 - 10);
                o->Position[2] += (WorldRandom() % 20 - 10);
                GameLogic::Effects::CreateTail(o, Matrix);
            }
            VectorCopy(o->TargetPosition, o->Position);
            GameLogic::Effects::CreateTail(o, Matrix);

            if (o->LifeTime < 4)
            {
                VectorScale(o->Light, powf(1.f / 1.2f, FPS_ANIMATION_FACTOR), o->Light);
            }
        }
        else if (o->SubType == 11)
        {
            vec3_t p1, p2;
            VectorCopy(o->StartPosition, o->Position);
            Vector(0.f, o->Scale / 1.3f, 0.f, p1);
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(p1, Matrix, p2);

            for (int i = 0; i < o->MaxTails - 5; i++)
            {
                VectorAdd(o->Position, p2, o->Position);
                GameLogic::Effects::CreateTail(o, Matrix);
                VectorCopy(o->Position, o->TargetPosition);
            }
            VectorCopy(o->StartPosition, o->Position);

            for (int i = 0; i < o->MaxTails - 5; i++)
            {
                int iScale = 1;
                VectorAdd(o->Position, p2, o->Position);
                iScale = (int)(o->Scale / 8.0f);
                o->Position[0] += (WorldRandom() % (iScale * 2) - iScale);
                o->Position[1] += (WorldRandom() % (iScale * 2) - iScale);
                o->Position[2] += (WorldRandom() % (iScale * 2) - iScale);
                GameLogic::Effects::CreateTail(o, Matrix);
            }
            VectorCopy(o->TargetPosition, o->Position);
            GameLogic::Effects::CreateTail(o, Matrix);
        }
        else if (o->SubType == 12)
        {
            VectorCopy(o->StartPosition, o->Position);
            AngleMatrix(o->Angle, Matrix);
            for (int i = 0; i < o->MaxTails - 1; ++i)
            {
                int iScale = 1;
                iScale = (int)(o->Scale / 5.0f);
                o->Position[0] += (WorldRandom() % (iScale * 2) - iScale);
                o->Position[1] += (WorldRandom() % (iScale * 2) - iScale);
                o->Position[2] += (WorldRandom() % (iScale * 2) - iScale);
                GameLogic::Effects::CreateTail(o, Matrix);
                VectorSubtract(o->TargetPosition, o->Position, Position);
                VectorScale(Position, 0.08f, Position);
                VectorAdd(o->Position, Position, o->Position);
            }
            VectorCopy(o->TargetPosition, o->Position);
            GameLogic::Effects::CreateTail(o, Matrix);
        }
        else if (o->SubType == 0 || o->SubType == 2 || o->SubType == 3 || o->SubType == 4 ||
                 o->SubType == 5 || o->SubType == 6 || o->SubType == 8 || o->SubType == 9 ||
                 o->SubType == 10)
        {
            if (o->LifeTime <= 10)
            {
                o->m_bCreateTails = false;
            }
            else
            {
                o->m_bCreateTails = true;
            }
        }
        break;
    case BITMAP_SPARK + 1:
        if (o->SubType == 0)
        {
            float verticalSpeed = o->Direction[2] + 5.f;
            Core::Time::Advance(o->Position[2], verticalSpeed, 5.f, FPS_ANIMATION_FACTOR);
            o->Direction[2] = verticalSpeed - 5.f;
            o->Position[0] = o->TargetPosition[0];
            o->Position[1] = o->TargetPosition[1];
        }
        if (o->SubType == 1)
        {
            o->Light[0] *= powf(1.f / 1.1f, FPS_ANIMATION_FACTOR);
            o->Light[1] = o->Light[0];
            o->Light[2] = o->Light[0];
        }
        else

            if (o->LifeTime < 5)
        {
            VectorScale(o->Light, powf(1.f / 1.3f, FPS_ANIMATION_FACTOR), o->Light);
        }
        break;
    case BITMAP_FLARE:
    case BITMAP_FLARE_BLUE:
        if (o->SubType == 0 || o->SubType == 10 || o->SubType == 18)
        {
            const float sampleLife = o->LifeTime - FPS_ANIMATION_FACTOR + 1.f;
            float count;
            if (o->PKKey != -1)
                count = (o->Direction[1] + sampleLife) / o->PKKey;
            else if (o->SubType == 18)
            {
                count = (o->Direction[1] + sampleLife * 2.f) * 0.1f;
                if (o->Skill == 1)
                    count = -count;
            }
            else
                count = (o->Direction[1] + sampleLife) * 0.1f;
            const float acceleration = o->SubType == 18 ? 0.1f : 0.f;
            float travel = 0.f;
            Core::Time::Advance(travel, o->Velocity, acceleration, FPS_ANIMATION_FACTOR);
            const float radius = o->Velocity - acceleration;
            o->Position[0] = o->TargetPosition[0] + cosf(count) * radius;
            o->Position[1] = o->TargetPosition[1] - sinf(count) * radius;
            const float rise = o->SubType == 10 ? -1.f : (o->SubType == 18 ? 1.1f : 1.f);
            o->Position[2] += o->Direction[2] * rise * FPS_ANIMATION_FACTOR - Matrix[2][1] * travel;
            if (o->SubType == 18)
                o->Scale += 0.2f * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 20)
        {
            if (o->Target == NULL)
            {
                Joints.Retire(*o);
                break;
            }

            BMD *b = &Models[o->Target->Type];
            vec3_t p;
            Vector(0.0f, 0.0f, 0.0f, p);
            b->TransformPosition(o->Target->BoneTransform[33], p, o->Position, true);
        }
        else if (o->SubType == 14 || o->SubType == 15)
        {
            if (o->Target != nullptr)
                AdvanceRisingFlare(*o, FPS_ANIMATION_FACTOR, Random);
            else
            {
                Vector(0.f, -o->Velocity, 0.f, p);
                VectorRotate(p, Matrix, Position);
                VectorAddScaled(o->Position, Position, o->Position, FPS_ANIMATION_FACTOR);
            }
        }
        else if (o->SubType == 2 || o->SubType == 24 || o->SubType == 50 || o->SubType == 51)
        {
            const float early = o->SubType == 50 ? 0.f
                                                 : (std::min)(FPS_ANIMATION_FACTOR,
                                                              (std::max)(0.f, o->LifeTime - 25.f));
            if (o->SubType == 51)
            {
                AdvanceJointRise(*o, early, 10.f, 1.f, 10.f);
                // Both authored rise operations run after the life-25 boundary.
                AdvanceJointRise(*o, FPS_ANIMATION_FACTOR - early, 15.f, 2.f, 25.f);
            }
            else
                AdvanceJointRise(*o, FPS_ANIMATION_FACTOR - early, 5.f, 1.f, 5.f);
        }
#ifdef GUILD_WAR_EVENT
        else if (o->SubType == 21 || o->SubType == 22)
        {
            const float active =
                (std::max)(0.f, FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 25.f));
            AdvanceJointRise(*o, active, 5.f, 1.f, 5.f);
        }
#endif //GUILD_WAR_EVENT
        else if (o->SubType == 41)
        {
            const float active =
                (std::max)(0.f, FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 20.f));
            AdvanceJointRise(*o, active, 1.f, 1.f, 1.f);
            VectorScale(o->Light, powf(1.f / 1.1f, active), o->Light);
            AddTerrainLight(o->Position[0], o->Position[1], o->Light, 1, PrimaryTerrainLight);
        }
        else if (o->SubType == 42)
        {
            o->Angle[2] += (15.f) * FPS_ANIMATION_FACTOR;

            if (o->Target->Live)
                o->LifeTime = 100.f;
            else
            {
                DeleteJoint(BITMAP_FLARE, o->Target, 42);
                break;
            }

            AngleMatrix(o->Angle, Matrix);
            VectorRotate(o->Direction, Matrix, Position);
            VectorAdd(o->StartPosition, Position, o->Position);

            int tBias = WorldRandom() % 100;
            if (((int)o->Angle[2] % 45 == 0) && tBias <= 50)
            {
                vec3_t Angle;
                Vector(0.f, 0.f, 0.f, Angle);
                CreateJointFpsChecked(BITMAP_FLARE, o->Position, o->Position, Angle, 41, NULL,
                                      o->Scale);
            }
            AddTerrainLight(o->TargetPosition[0], o->TargetPosition[1], o->Light, 2,
                            PrimaryTerrainLight);
        }
        else if (o->SubType == 19)
        {
            const float active =
                (std::max)(0.f, FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 25.f));
            AdvanceJointRise(*o, active, -5.f, 1.f, -5.f);
        }
        else if (o->SubType == 40)
        {
            constexpr float InitialLife = 50.f, Radius = 50.f;
            const float age = (std::max)(0.f, InitialLife - o->LifeTime + FPS_ANIMATION_FACTOR);
            const float lag = (std::min)(1.f, age);
            const float sampleAge = age - lag;
            o->Position[0] += o->Direction[0] * 12.f * FPS_ANIMATION_FACTOR;
            o->Position[1] += o->Direction[1] * 12.f * FPS_ANIMATION_FACTOR;
            vec3_t base;
            VectorCopy(o->Position, base);
            const float endScale = o->Scale * powf(1.1f, FPS_ANIMATION_FACTOR);
            o->Scale = endScale / powf(1.1f, lag);
            o->m_bCreateTails = false;
            o->NumTails = 0;
            AngleMatrix(o->Angle, Matrix);
            const float heading = (90.f - o->Angle[2]) * Q_PI / 180.f;
            for (int i = 0; i < 200; ++i)
            {
                const float theta = i * Q_PI / 6.f;
                const float horizontal = Radius * cosf(theta);
                o->Position[0] = base[0] - o->Direction[0] * 12.f * lag +
                                 o->Direction[0] * sampleAge * (i + 1) + horizontal * cosf(heading);
                o->Position[1] = base[1] - o->Direction[1] * 12.f * lag +
                                 o->Direction[1] * sampleAge * (i + 1) + horizontal * sinf(heading);
                o->Position[2] = base[2] + Radius * sinf(theta);
                GameLogic::Effects::CreateTail(o, Matrix);
            }
            VectorCopy(base, o->Position);
            VectorCopy(base, o->TargetPosition);
            o->Scale = endScale;
            float remaining = FPS_ANIMATION_FACTOR;
            while (remaining > 0.f)
            {
                if (o->MotionFrames <= 0.f)
                {
                    o->Light[0] = 0.5f + (WorldRandom() % 64) / 255.f;
                    o->Light[1] = 0.5f + (WorldRandom() % 64) / 255.f;
                    o->Light[2] = 0.5f + (WorldRandom() % 128) / 255.f;
                    o->MotionFrames = 1.f;
                }
                const float step = (std::min)(remaining, o->MotionFrames);
                o->MotionFrames -= step;
                remaining -= step;
            }
        }
        else if (o->SubType == 6)
        {
            const float initialLife = o->Type == BITMAP_FLARE_BLUE ? 15.f : 20.f;
            const float sampleLife =
                (std::min)(initialLife, o->LifeTime - FPS_ANIMATION_FACTOR + 1.f);
            const float phase =
                o->Direction[0] + sampleLife * (o->Type == BITMAP_FLARE_BLUE ? 0.8f : 0.5f);
            const float radius = (std::max)(sampleLife, 1.f) * 2.f;
            const float horizontal = -cosf(phase) * radius;
            const float heading = (90.f - o->Angle[2]) * Q_PI / 180.f;
            o->TargetPosition[0] += o->Direction[1] * FPS_ANIMATION_FACTOR;
            o->TargetPosition[1] += o->Direction[2] * FPS_ANIMATION_FACTOR;
            o->Position[0] = horizontal * sinf(heading) + o->TargetPosition[0];
            o->Position[1] = horizontal * cosf(heading) + o->TargetPosition[1];
            o->Position[2] = sinf(phase) * radius + o->TargetPosition[2];
        }
        else if (o->SubType == 5)
        {
            AdvanceJointRise(*o, FPS_ANIMATION_FACTOR, -60.f, 1.f, -60.f);
            float remaining = FPS_ANIMATION_FACTOR;
            while (remaining > 0.f)
            {
                if (o->MotionFrames <= 0.f)
                {
                    o->Scale = WorldRandom() % 4 + 6.f;
                    o->MotionFrames = 1.f;
                }
                const float step = (std::min)(remaining, o->MotionFrames);
                o->MotionFrames -= step;
                remaining -= step;
            }
        }
        else if (o->SubType == 7 || o->SubType == 11 || o->SubType == 25 || o->SubType == 45 ||
                 o->SubType == 46 || o->SubType == 47)
        {
            if (o->SubType == 11 || o->SubType == 25)
            {
                if (o->Target->Live == false)
                {
                    Joints.Retire(*o);
                    o->LifeTime = -1;
                    break;
                }
            }

            double phase = Core::Time::ReferenceFrames(
                WorldTime, sessionKeeper_.ApplicationConfig().legacyReferenceFps);
            phase = ((iIndex % 2) ? phase : -phase) + static_cast<double>(iIndex) * 53731;
            vec3_t vDir;
            ScrewDirection(phase, vDir, o->SubType == 11 ? 1.5f : 1.f);

            float fLife = (float)o->LifeTime * 40.f / 30.f;
            float fPos;

            if (o->SubType == 11 || o->SubType == 25)
            {
                if (fLife < 20.f)
                {
                    fPos = fLife * 4.f;
                }
                else
                {
                    fPos = fLife * 1.0f + 60.f;
                }
            }
            else
            {
                if (fLife < 10.f)
                { // 끝
                    fPos = fLife * 7.0f;
                }
                else
                {
                    fPos = fLife * 1.0f + 60.f;
                }
            }
            fPos = fPos / (float)(30 + o->MultiUse) * 30.f;
            if (Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 30))
            {
                PlayBuffer(SOUND_METEORITE01);
            }

            float fCircle;
            if (o->SubType == 11 || o->SubType == 25)
            {
                fCircle = std::min<float>(std::max<float>(0.f, fLife - 10) * 5.f, 150.f);
            }
            else
            {
                fCircle = std::min<float>(std::max<float>(0.f, 40.f - fLife) * 15.f, 150.f);
            }
            o->Position[0] = o->TargetPosition[0] + vDir[0] * fCircle;
            o->Position[1] = o->TargetPosition[1] + vDir[1] * fCircle;
            o->Position[2] = o->TargetPosition[2] + vDir[2] * fCircle;

            float fLastTarget;
            for (int k = 0; k < 3; ++k)
            {
                if (o->SubType == 11 || o->SubType == 25)
                    fLastTarget =
                        (100.f - fPos) *
                        (o->Target->StartPosition[k] +
                         25.f * static_cast<float>(std::cos((static_cast<double>(iIndex) * 51231 +
                                                             k * 3711 + phase / 10.0) *
                                                            0.01)));
                else
                    fLastTarget =
                        (100.f - fPos) *
                        (o->Target->Position[k] +
                         25.f * static_cast<float>(std::cos((static_cast<double>(iIndex) * 51231 +
                                                             k * 3711 + phase / 10.0) *
                                                            0.01)));

                o->Position[k] = (fPos * o->Position[k] + fLastTarget) * 0.01f;
            }
            if (o->SubType != 11 && o->SubType != 25)
            {
                o->Position[2] += 100.0f;
            }

            vec3_t Light = {.5f, .5f, 1.0f};

            if (o->SubType == 11)
            {
                Vector(0.3f, 0.3f, 0.5f, Light);
                if (o->Skill == 1)
                {
                    VectorCopy(o->Position, o->Target->Position);
                    CheckClientArrow(o->Target);
                }
            }
            if (o->SubType == 25)
            {
                Vector(0.9f, 0.4f, 0.6f, Light);
                if (o->Skill == 1)
                {
                    VectorCopy(o->Position, o->Target->Position);
                    CheckClientArrow(o->Target);
                }
            }
            if (o->SubType == 45 && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 1.f))
            {
                auto birth = EmissionTime(FPS_ANIMATION_FACTOR - std::max(0.f, o->LifeTime - 1.f));
                vec3_t position, angle;
                o->Target->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Target->Position,
                                              position);
                VectorCopy(o->Target->Angle, angle);
                angle[2] =
                    o->Target->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), angle[2]);
                CreateEffect(MODEL_CHANGE_UP_EFF, position, angle, o->Target->Light, 1, o->Target);
                CreateEffect(MODEL_CHANGE_UP_CYLINDER, position, angle, o->Target->Light, 1,
                             o->Target);
            }
            {
                if (o->SubType != 47)
                {
                    CreateSprite(BITMAP_SHINY + 1, o->Position,
                                 (float)(WorldRandom() % 2 + 8) * 0.10f, Light, o->Target,
                                 (float)(WorldRandom() % 360));
                    CreateSprite(BITMAP_LIGHT, o->Position, (float)(WorldRandom() % 2 + 8) * 0.18f,
                                 Light, o->Target, (float)(WorldRandom() % 360));
                    CreateSprite(BITMAP_LIGHT, o->Position, (float)(WorldRandom() % 2 + 8) * 0.18f,
                                 Light, o->Target, (float)(WorldRandom() % 360));
                }
            }

            if (o->SubType != 11 && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, 1.f) &&
                o->SubType != 45 && o->SubType != 46 && o->SubType != 47)
            {
                vec3_t Angle = {0.0f, 0.0f, 0.0f};
                vec3_t Light = {1.f, 1.f, 1.f};
                CreateParticle(BITMAP_EXPLOTION, o->Position, Angle, Light, 1, 0.6f);
            }
        }
        else if (o->SubType == 8)
        {
            Vector(0.f, -50.f, 0.f, p);
            AngleMatrix(o->TargetPosition, Matrix);
            VectorRotate(p, Matrix, Position);
            VectorAdd(o->StartPosition, Position, o->Position);
            AngleMatrix(o->Angle, Matrix);

            o->TargetPosition[2] += 10.f * FPS_ANIMATION_FACTOR;
        }
        else if (o->SubType == 9)
        {
            AngleMatrix(o->Angle, Matrix);

            for (int i = 0; i < o->MaxTails; i++)
            {
                VectorAdd(o->Position, o->StartPosition, o->Position);
                GameLogic::Effects::CreateTail(o, Matrix);
            }
        }
        else if (o->SubType == 13)
        {
            const float active =
                (std::max)(0.f, FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 25.f));
            AdvanceJointRise(*o, active, 1.f, 1.f, 1.f);
        }
        else if (o->SubType == 17)
        {
            if (((o->Target->CurrentAction >=
                  PLAYER_WALK_MALE /*&& o->Target->CurrentAction<=PLAYER_RUN_RIDE_WEAPON*/) ||
                 (o->Target->CurrentAction >= PLAYER_ATTACK_SKILL_SWORD1 &&
                  o->Target->CurrentAction <= PLAYER_ATTACK_SKILL_SWORD5) &&
                     (o->Target->CurrentAction != PLAYER_RAGE_UNI_STOP_ONE_RIGHT)))
            {
                o->LifeTime -= 10 * FPS_ANIMATION_FACTOR;
            }
            else
            {
                float remaining = FPS_ANIMATION_FACTOR;
                while (remaining > 0.f)
                {
                    if (o->MotionFrames <= 0.f)
                    {
                        o->MotionVelocity[0] = sinf((WorldRandom() % 360) * 0.1f) * 4.f;
                        o->MotionVelocity[1] = cosf((WorldRandom() % 360) * 0.1f) * 4.f;
                        o->MotionFrames = 1.f;
                    }
                    const float step = (std::min)(remaining, o->MotionFrames);
                    o->Direction[0] += o->MotionVelocity[0] * step;
                    o->Direction[1] += o->MotionVelocity[1] * step;
                    o->MotionFrames -= step;
                    remaining -= step;
                }
                o->Direction[2] += o->Velocity * FPS_ANIMATION_FACTOR;
                o->Position[0] = o->TargetPosition[0] + o->Direction[0];
                o->Position[1] = o->TargetPosition[1] + o->Direction[1];
                o->Position[2] = o->TargetPosition[2] + o->Direction[2];
            }

            if (o->LifeTime < 12)
            {
                VectorScale(o->Light, powf(1.f / 1.3f, FPS_ANIMATION_FACTOR), o->Light);
            }
        }
        else if (o->SubType == 16 && o->Target != NULL)
        {
            VectorSubtract(o->TargetPosition, o->Target->Position, Position);
            VectorCopy(o->Target->Position, o->TargetPosition);

            for (int j = o->NumTails - 1; j >= 0; j--)
            {
                for (int k = 0; k < 4; k++)
                    VectorSubtract(o->Tails[j][k], Position, o->Tails[j][k]);
            }

            if (o->LifeTime < 20)
            {
                VectorScale(o->Light, powf(1.f / 1.1f, FPS_ANIMATION_FACTOR), o->Light);
            }
            else if (o->LifeTime < 40)
            {
                o->Light[0] = 0.25f;
                o->Light[1] = 0.25f;
                o->Light[2] = 0.25f;
            }
            else
            {
                o->Light[0] *= powf(1.04f, FPS_ANIMATION_FACTOR);
                o->Light[1] *= powf(1.04f, FPS_ANIMATION_FACTOR);
                o->Light[2] *= powf(1.04f, FPS_ANIMATION_FACTOR);
            }
        }
        else if (o->SubType == 23)
        {
            AdvanceTurningFlare(*o, FPS_ANIMATION_FACTOR);
            AngleMatrix(o->HeadAngle, Matrix);
        }
        else if (o->SubType == 43)
        {
            if (o->Target->Live)
            {
                o->LifeTime = 100.f; //무한

                VectorCopy(o->Target->Position, o->Position);
                o->Position[2] += 30.f;
                for (auto birth : Emissions(FPS_ANIMATION_FACTOR / 3.f))
                {
                    vec3_t position;
                    o->Target->MotionTrace.Sample(WorldTime, birth.FrameFraction(),
                                                  o->Target->Position, position);
                    position[2] += 30.f;
                    position[0] += WorldRandom() % 60 - 30;
                    position[1] += WorldRandom() % 60 - 30;
                    CreateJoint(BITMAP_FLARE, position, position, o->Angle, 44, o->Target, o->Scale,
                                0, 0, 0, 0, o->Light);
                }
            }
            else
            {
                DeleteJoint(BITMAP_FLARE, o->Target, 43);
                break;
            }
        }
        else if (o->SubType == 44)
        {
            if (o->Target->Live == false)
            {
                Joints.Retire(*o);
                break;
            }
            else
            {
                float active =
                    (std::max)(0.f, FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 20.f));
                const double height = 400. - o->Position[2];
                const double speed = o->Direction[2] + 0.5;
                const double root = std::sqrt(speed * speed + 2. * (std::max)(0., height));
                const double arrival =
                    height <= 0. ? 0. : (speed >= 0. ? 2. * height / (speed + root) : root - speed);
                const bool reached = active > 0.f && arrival <= active;
                if (reached)
                    active = static_cast<float>(arrival);
                AdvanceJointRise(*o, active, 1.f, 1.f, 1.f);
                VectorScale(o->Light, powf(1.f / 1.05f, active), o->Light);
                if (reached)
                {
                    o->Position[2] = 400.f;
                    Joints.Retire(*o);
                }
                if (active > 0.f || reached)
                {
                    CreateSprite(BITMAP_PIN_LIGHT, o->Position, 1.5f, o->Light, NULL);
                    CreateSprite(BITMAP_PIN_LIGHT, o->Position, 0.5f, o->Light, NULL);
                }
            }
        }

        if (o->SubType != 5 && o->SubType != 7 && o->SubType != 11 ||
            (o->SubType == 14 || o->SubType == 15 || o->SubType == 16))
        {
            if (o->LifeTime < 10)
            {
                VectorScale(o->Light, powf(1.f / 1.3f, FPS_ANIMATION_FACTOR), o->Light);
            }
        }

        if ((o->SubType == 0 || o->SubType == 10 || o->SubType == 18) && o->Scale < 40.f)
        {
            if (((o->Target->CurrentAction >= PLAYER_WALK_MALE &&
                  o->Target->CurrentAction <= PLAYER_RUN_RIDE_WEAPON) ||
                 (o->Target->CurrentAction >= PLAYER_ATTACK_SKILL_SWORD1 &&
                  o->Target->CurrentAction <= PLAYER_ATTACK_SKILL_SWORD5) ||
                 (o->Target->CurrentAction == PLAYER_RAGE_UNI_RUN ||
                  o->Target->CurrentAction == PLAYER_RAGE_UNI_RUN_ONE_RIGHT)))
            {
                o->SubType = 1;
                o->m_bCreateTails = false;
                o->LifeTime = (std::min)(10.f, o->LifeTime);
            }
        }
        break;
    case BITMAP_FLARE + 1:
        if (o->SubType == 5)
        {
            AdvanceSteeringJoint(*o, FPS_ANIMATION_FACTOR, Random, WorldTime);
            AngleMatrix(o->Angle, Matrix);
            const float fading =
                (std::max)(0.f, FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 9.f));
            if (fading > 0.f)
            {
                if (fading > 0.f)
                {
                    o->Light[0] *= powf(1.f / 1.2f, fading);
                    o->Light[1] = o->Light[0];
                    o->Light[2] = o->Light[0];
                }
            }
            Luminosity = (float)(WorldRandom() % 4 + 8) * 0.03f;
            Vector(Luminosity * 0.4f, Luminosity * 0.8f, Luminosity * 0.4f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
        }
        else if (o->SubType == 16)
        {
            AdvanceSteeringJoint(*o, FPS_ANIMATION_FACTOR, Random, WorldTime);
            AngleMatrix(o->Angle, Matrix);
            const float fading =
                (std::max)(0.f, FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 9.f));
            if (fading > 0.f)
            {
                if (fading > 0.f)
                {
                    o->Light[0] *= powf(1.f / 1.2f, fading);
                    o->Light[1] = o->Light[0];
                    o->Light[2] = o->Light[0];
                }
            }
            Luminosity = 1.f;
            Vector(Luminosity * 1.0f, Luminosity * 1.0f, Luminosity * 1.0f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 1, PrimaryTerrainLight);
        }
        else if (o->SubType == 6)
        {
            if (o->Target != NULL)
            {
                AdvanceSteeringJoint(*o, FPS_ANIMATION_FACTOR, Random, WorldTime);
                AngleMatrix(o->Angle, Matrix);
                const float fading =
                    (std::max)(0.f, FPS_ANIMATION_FACTOR - (std::max)(0.f, o->LifeTime - 14.f));
                if (fading > 0.f)
                {
                    o->Light[0] *= powf(1.f / 1.2f, fading);
                    o->Light[1] = o->Light[0];
                    o->Light[2] = o->Light[0];
                }
                Luminosity = (float)(WorldRandom() % 4 + 8) * 0.03f;
                Vector(Luminosity * 0.4f, Luminosity, Luminosity * 0.8f, Light);
                AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
            }
            else
            {
                Joints.Retire(*o);
            }
        }
        else if (o->SubType == 7)
        {
            float travel = 0.f;
            Core::Time::Advance(travel, o->Velocity, -0.1f, FPS_ANIMATION_FACTOR);
            o->Direction[0] -= 0.1f * FPS_ANIMATION_FACTOR;
            const float sampleLife = o->LifeTime - FPS_ANIMATION_FACTOR + 1.f;
            o->Position[0] =
                o->StartPosition[0] + sinf(sampleLife / 2.5f) * (o->Direction[0] + 0.1f);
            o->Position[1] = o->StartPosition[1];
            o->Position[2] += travel * (1.f - Matrix[2][1]);

            if (o->LifeTime < 15)
            {
                VectorScale(o->Light, powf(1.f / 1.2f, FPS_ANIMATION_FACTOR), o->Light);
            }
        }
        else if (o->SubType == 8 || o->SubType == 9)
        {
            if (o->Target == NULL)
            {
                Joints.Retire(*o);
                break;
            }
            Joints.SetLive(iIndex, o->Target->Live);
            VectorCopy(o->Target->Position, o->Position);
            //            Vector ( 0.f, 0.f, o->Target->Angle[2], o->Angle );

            if (o->SubType == 8)
            {
                o->Position[2] = 300.f;
            }
            else if (o->SubType == 9)
            {
                o->Position[2] += o->MultiUse;
            }
        }
        else if (o->SubType == 12 || o->SubType == 13 || o->SubType == 14 || o->SubType == 17 ||
                 o->SubType == 15 || o->SubType == 18)
        {
            if (o->Target != NULL)
            {
                if (o->Target->Live == false)
                {
                    Joints.Retire(*o);
                }
                if (o->SubType == 17 && o->Live == true)
                {
                    Vector(1.0f, 1.0f, 0.2f, o->Light);
                }
                if (o->SubType == 14 && o->Live == true)
                {
                    CreateSprite(BITMAP_SPARK + 1, o->Position, 5.0f, o->Light, NULL);

                    CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 34,
                                             1.0f);
                    CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 35,
                                             1.0f);
                    //CreateParticle(BITMAP_SPARK+1, o->Position, o->Angle, o->Light, 12, 1.0f);
                    Vector(0.7f, 0.3f, 0.3f, o->Light);
                    CreateParticleFpsChecked(BITMAP_SPARK + 1, o->Position, o->Angle, o->Light, 10,
                                             4.0f);
                    Vector(1.0f, 1.0f, 0.2f, o->Light);
                }
                else if (o->SubType == 15 && o->Live == true)
                {
                    CreateParticleFpsChecked(BITMAP_SHINY, o->Position, o->Angle, o->Light, 3,
                                             0.5f);
                    CreateParticleFpsChecked(BITMAP_SHINY, o->Position, o->Angle, o->Light, 3,
                                             0.5f);
                }

                if (o->LifeTime < 15)
                {
                    VectorScale(o->Light, powf(1.f / 1.2f, FPS_ANIMATION_FACTOR), o->Light);
                }

                if (o->SubType != 18)
                {
                    Luminosity = (float)(WorldRandom() % 4 + 8) * 0.03f;
                    Vector(Luminosity * 0.4f, Luminosity, Luminosity * 0.8f, Light);
                    AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
                }
            }
            else
            {
                Joints.Retire(*o);
            }
        }
        else if (o->SubType == 19)
        {
            vec3_t pos;
            float Mat[3][4];

            vec3_t Angle{};
            VectorSubtract(o->Target->Position, o->Target->StartPosition, pos);
            VectorCopy(o->Target->StartPosition, o->TargetPosition);
            //VectorCopy( o->Target->HeadAngle, Angle );
            AngleMatrix(o->Angle, Mat);

            pos[0] /= 3.f;
            pos[1] /= 3.f;
            pos[2] /= 3.f;
            //Angle[1] += (o->Velocity - 90) * FPS_ANIMATION_FACTOR;

            for (int j = 0; j < 3; j++)
            {
                Angle[1] += 30.f;

                VectorAdd(o->TargetPosition, pos, o->TargetPosition);

                vec3_t position;
                AngleMatrix(Angle, Matrix);
                VectorRotate(o->Direction, Matrix, position);
                VectorAdd(o->TargetPosition, position, o->Position);

                if ((int)o->NumTails < (o->MaxTails - 1) || o->Skill != 0)
                    GameLogic::Effects::CreateTail(o, Mat);
            }
        }
        else if (o->SubType == 20)
        {
            vec3_t pos;
            float Mat[3][4];

            vec3_t Angle{};
            VectorSubtract(o->Target->Position, o->Target->StartPosition, pos);
            VectorCopy(o->Target->StartPosition, o->TargetPosition);
            //VectorCopy( o->Target->HeadAngle, Angle );
            AngleMatrix(o->Angle, Mat);

            pos[0] /= 3.f;
            pos[1] /= 3.f;
            pos[2] /= 3.f;
            //Angle[1] += (o->Velocity - 90) * FPS_ANIMATION_FACTOR;

            for (int j = 0; j < 3; j++)
            {
                VectorAdd(o->TargetPosition, pos, o->TargetPosition);

                vec3_t position;
                AngleMatrix(Angle, Matrix);
                VectorRotate(o->Direction, Matrix, position);
                VectorAdd(o->TargetPosition, position, o->Position);

                if ((int)o->NumTails < (o->MaxTails - 1) || o->Skill != 0)
                    GameLogic::Effects::CreateTail(o, Mat);
            }
        }
        else
        {
            vec3_t pos;
            float Mat[3][4];
            if (o->Target->Live)
            {
                vec3_t Angle;
                VectorSubtract(o->Target->Position, o->Target->StartPosition, pos);
                VectorCopy(o->Target->StartPosition, o->TargetPosition);
                VectorCopy(o->Target->HeadAngle, Angle);
                AngleMatrix(o->Angle, Mat);

                pos[0] /= 3.f;
                pos[1] /= 3.f;
                pos[2] /= 3.f;
                Angle[1] += o->Velocity - 90.f;

                for (int j = 0; j < 3; j++)
                {
                    Angle[1] += 30.f;
                    VectorAdd(o->TargetPosition, pos, o->TargetPosition);

                    vec3_t position;
                    AngleMatrix(Angle, Matrix);
                    VectorRotate(o->Direction, Matrix, position);
                    VectorAdd(o->TargetPosition, position, o->Position);

                    if ((int)o->NumTails < (o->MaxTails - 1) || o->Skill != 0)
                        GameLogic::Effects::CreateTail(o, Mat);
                }

                if (o->LifeTime < 15)
                {
                    VectorScale(o->Light, powf(1.f / 1.5f, FPS_ANIMATION_FACTOR), o->Light);
                }
            }
            else
            {
                o->LifeTime = -1;
                VectorCopy(o->TargetPosition, o->Position);
                o->Position[2] += 50.f;
            }
        }
        break;

    case BITMAP_JOINT_FORCE:
        if (o->SubType == 0 || o->SubType == 8 || o->SubType == 10)
        {
            constexpr int SamplesPerFrame = 8;
            float samples = FPS_ANIMATION_FACTOR * SamplesPerFrame;
            while (samples > 0.f)
            {
                const bool growing = o->NumTails < o->MaxTails - 1;
                if (!AdvanceForceArcSample(*o, samples))
                    continue;
                const int sample = static_cast<int>(o->MotionScale);
                o->MotionScale = static_cast<float>((sample + 1) % SamplesPerFrame);
                if (growing)
                {
                    if (o->SubType != 8 && Random.FpsCheck(2, 1.f))
                        CreateParticle(BITMAP_FIRE, o->Position, o->Angle, o->Light, 0);
                    vec3_t source;
                    VectorCopy(o->SubType == 8 ? o->Position : o->StartPosition, source);
                    CreateJoint(BITMAP_JOINT_THUNDER, source, o->Position, o->Angle, 3, NULL,
                                WorldRandom() % 10 + 5.f, 5, 10);
                    CreateJoint(BITMAP_JOINT_THUNDER, source, o->Position, o->Angle, 3, NULL,
                                WorldRandom() % 8 + 4.f, 5, 10);
                }
                bool attack = o->SubType == 10;
#ifndef SV_RANGE_ATTACK_CHECK
                attack = attack || o->SubType == 0;
#endif
                const float life =
                    o->LifeTime - FPS_ANIMATION_FACTOR + (samples + sample + 1.f) / SamplesPerFrame;
                if (attack && o->Target == &Hero->Object && life > 18.f && sample % 5 == 0)
                {
                    if (IsBattleCastleStart() &&
                        (TERRAIN_ATTRIBUTE(o->Position[0], o->Position[1]) & TW_NOATTACKZONE) != 0)
                    {
                        o->Velocity = 0.f;
                        o->LifeTime *= 1.f / 5.f;
                        break;
                    }
                    AttackCharacterRange(o->Skill, o->Position, o->SubType == 10 ? 225.f : 150.f,
                                         o->Weapon, o->PKKey);
                }
            }
            if (o->SubType != 8)
            {
                const float life = o->LifeTime - FPS_ANIMATION_FACTOR + 1.f;
                const float fading = (std::min)(FPS_ANIMATION_FACTOR, (std::max)(0.f, 10.f - life));
                if (o->SubType == 0 || o->m_bySkillSerialNum == 0)
                {
                    const float brightness =
                        life >= 10.f ? life / 30.f
                                     : (fading < FPS_ANIMATION_FACTOR ? 10.f / 30.f : o->Light[0]) *
                                           std::pow(1.f / 1.5f, fading);
                    Vector(brightness, brightness, brightness, o->Light);
                }
                else
                    VectorScale(o->Light, std::pow(1.f / 1.5f, fading), o->Light);
            }
        }
        else if (o->SubType == 1)
        {
            if (o->Target == NULL)
            {
                Joints.Retire(*o);
                break;
            }
            Joints.SetLive(iIndex, o->Target->Live);
            VectorCopy(o->Target->Position, o->Position);

            o->Scale -= (1.f) * FPS_ANIMATION_FACTOR;
            o->Position[2] += 100.f;

            if (o->LifeTime < 15)
            {
                VectorScale(o->Light, powf(1.f / 1.3f, FPS_ANIMATION_FACTOR), o->Light);
            }
        }
        else if (o->SubType >= 2 && o->SubType <= 6)
        {
            AdvanceAcceleratingForce(*o, FPS_ANIMATION_FACTOR);
            if (o->SubType != 4)
            {
                if (o->NumTails >= o->MaxTails - 1)
                {
                    o->m_bCreateTails = false;
                }
            }
            if (o->LifeTime < o->MultiUse)
            {
                VectorScale(o->Light, powf(1.f / 1.3f, FPS_ANIMATION_FACTOR), o->Light);
            }
            if (o->SubType == 2)
            {
                constexpr float ImpactInterval = 5.f;
                for (float life =
                         Core::Time::ReferenceSample(o->LifeTime / ImpactInterval) * ImpactInterval;
                     life > 0.f && Core::Time::Reaches(o->LifeTime, FPS_ANIMATION_FACTOR, life);
                     life -= ImpactInterval)
                {
                    auto birth =
                        EmissionTime(FPS_ANIMATION_FACTOR - std::max(0.f, o->LifeTime - life));
                    CreateEffect(MODEL_SKILL_INFERNO, o->StartPosition, o->HeadAngle, o->Light, 6,
                                 nullptr, 20, 0);
                }
            }
            else if (o->SubType == 4)
            {
                Vector(0.1f, 0.6f, 1.f, Light);

                CreateJointFpsChecked(BITMAP_FLARE_BLUE, o->Position, o->Position, o->Angle, 6,
                                      NULL, 30.0f);
                CreateSprite(BITMAP_LIGHT, o->Position, 1.6f, Light, NULL);
                CreateSprite(BITMAP_SHINY + 1, o->Position, 1.5f, Light, NULL, WorldTime * 0.1f);
            }
        }
        else if (o->SubType == 7 || o->SubType == 20)
        {
            AdvanceAcceleratingForce(*o, FPS_ANIMATION_FACTOR);
            if (o->NumTails >= o->MaxTails - 1)
            {
                o->m_bCreateTails = false;
            }
            if (o->LifeTime < o->MultiUse)
            {
                VectorScale(o->Light, powf(1.f / 1.3f, FPS_ANIMATION_FACTOR), o->Light);
            }
            o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 3.f;
        }
        break;

    case BITMAP_LIGHT:
        if (o->SubType == 0)
        {
            const float active =
                (std::min)(FPS_ANIMATION_FACTOR, (std::max)(0.f, o->LifeTime - 16.f));
            AdvanceStraightTrail(*o, active, 10, 0.f);
        }
        else if (o->SubType == 1)
        {
            const float active = o->Skill == 0 ? FPS_ANIMATION_FACTOR
                                               : (std::min)(FPS_ANIMATION_FACTOR,
                                                            (std::max)(0.f, o->LifeTime - 3.f));
            AdvanceStraightTrail(*o, active, 5, 2.5f);
        }
        VectorScale(o->Light, powf(1.f / 1.2f, FPS_ANIMATION_FACTOR), o->Light);
        break;
    case BITMAP_PIERCING:
        if (o->SubType == 0 || o->SubType == 1)
        {
            const float active =
                (std::min)(FPS_ANIMATION_FACTOR, (std::max)(0.f, o->LifeTime - 9.f));
            AdvanceStraightTrail(*o, active, 30, -2.f);
            o->Velocity -= 2.f * (FPS_ANIMATION_FACTOR - active);
            VectorScale(o->Light, powf(1.f / 1.4f, FPS_ANIMATION_FACTOR), o->Light);
        }
        break;
    case BITMAP_FLARE_FORCE:
        if (o->SubType == 5 || o->SubType == 6 || o->SubType == 7)
        {
            if (o->Target != NULL)
            {
                vec3_t Direction, Angle;

                Vector(0.f, 20.f, 0.f, p);
                VectorCopy(o->Direction, Direction);
                BMD *b = &Models[o->Target->Type];
                VectorCopy(o->Target->Position, b->BodyOrigin);
                b->TransformPosition(o->Target->BoneTransform[(int)o->MultiUse], p,
                                     o->StartPosition, true);
                VectorCopy(o->StartPosition, o->Position);

                constexpr float InitialLife = 15.f;
                const float age =
                    (std::min)(InitialLife + 1.f, InitialLife - o->LifeTime + FPS_ANIMATION_FACTOR);
                const int MaxTails =
                    (std::min)(static_cast<int>(o->Weapon),
                               (std::max)(1, static_cast<int>(std::ceil(age - 0.0001f))));
                const int budget = (std::min)(static_cast<int>(o->Weapon),
                                              1 + static_cast<int>(std::floor(age + 0.0001f)));
                const float radius = ForceTrailRadius(age);
                o->SetMaxTails(budget);
                o->NumTails = 0;
                o->TargetPosition[1] = o->TargetPosition[2];

                for (int i = 0; i < MaxTails; ++i)
                {
                    VectorRotate(Direction, o->Target->BoneTransform[(int)o->MultiUse], Position);
                    const float fraction = std::clamp(age - i, 0.f, 1.f);
                    VectorAddScaled(o->StartPosition, Position, o->StartPosition, fraction);
                    // VectorAdd(o->StartPosition, Position, o->StartPosition);

                    if (o->SubType % 2)
                    {
                        o->TargetPosition[1] += 40.f * fraction;
                    }
                    else
                    {
                        o->TargetPosition[1] -= 40.f * fraction;
                    }

                    Vector(o->TargetPosition[1], 0.f, 0.f, Angle);
                    AngleMatrix(Angle, Matrix);
                    Vector(0.f, 0.f, radius + 0.15f * (MaxTails - i), p);
                    VectorRotate(p, Matrix, Position);
                    VectorAdd(o->StartPosition, Position, o->Position);
                    // VectorAddScaled(o->StartPosition, Position, o->Position, FPS_ANIMATION_FACTOR);

                    GameLogic::Effects::CreateTail(
                        o, o->Target->BoneTransform[0]); //(int)o->MultiUse]);

                    if (o->PKKey == -1)
                        Direction[1] -= 0.1f;

                    if ((i % 2) == 0)
                    {
                        CreateSprite(BITMAP_FLARE, o->StartPosition, o->Light[0] / 2.f, o->Light,
                                     NULL);
                    }
                }
                o->TargetPosition[0] = radius;
                o->NumTails = MaxTails - 1;
            }
            else
            {
                Joints.Retire(*o);
            }
            if (o->LifeTime < 7)
            {
                VectorScale(o->Light, powf(1.f / 1.5f, FPS_ANIMATION_FACTOR), o->Light);
            }
        }
        else if ((o->SubType >= 0 && o->SubType <= 4) || (o->SubType >= 11 && o->SubType <= 13))
        {
            AdvanceExpandingForce(*o, FPS_ANIMATION_FACTOR);
        }

        if ((o->SubType >= 0 && o->SubType <= 4 || (o->SubType >= 11 && o->SubType <= 13)) &&
            o->LifeTime < 10)
        {
            VectorScale(o->Light, powf(1.f / 1.3f, FPS_ANIMATION_FACTOR), o->Light);
        }
        break;

    case BITMAP_FLASH:
        if (o->SubType <= 3 || o->SubType == 5)
        {
            float remaining = FPS_ANIMATION_FACTOR;
            const auto prepareTravel = [&]() {
                vec3_t local{0.f, -o->Velocity, 0.f}, second;
                float matrix[3][4];
                AngleMatrix(o->Angle, matrix);
                VectorRotate(local, matrix, o->MotionVelocity);
                local[1] = -(o->Velocity + 20.f * o->MotionFrames);
                AngleMatrix(o->HeadAngle, matrix);
                VectorRotate(local, matrix, second);
                VectorAdd(o->MotionVelocity, second, o->MotionVelocity);
            };
            while (remaining > 0.f)
            {
                if (o->MotionFrames <= 0.f)
                {
                    o->MotionFrames = 1.f;
                    o->MotionAngleRate[2] = o->SubType == 2 ? WorldRandom() % 10 + 10.f : 0.f;
                    prepareTravel();
                }
                float step = (std::min)(remaining, o->MotionFrames);
                const auto contact = sessionKeeper_.WorldUnit()->FirstTerrainContact(
                    o->Position, o->MotionVelocity, 0.f, step, -o->MultiUse);
                if (contact)
                    step = contact->frames;
                VectorAddScaled(o->Position, o->MotionVelocity, o->Position, step);
                o->Angle[2] += o->MotionAngleRate[2] * step;
                o->Velocity += 20.f * step;
                o->MotionFrames -= step;
                remaining -= step;
                AngleMatrix(o->Angle, Matrix);
                if (o->m_bCreateTails)
                    GameLogic::Effects::CreateTimedTail(o, Matrix, step);
                if (contact)
                {
                    o->Position[2] = contact->height;
                    if (o->m_bCreateTails && o->PKKey != 1)
                    {
                        if (o->SubType < 2)
                        {
                            if (o->Target != NULL)
                            {
                                BMD *b = &Models[MODEL_SHADOW_BODY];
                                b->Animation(BoneTransform, 0.f, 0.f, 0, o->Target->Angle,
                                             o->Target->HeadAngle, false, true);
                                b->Transform(BoneTransform, o->Target->BoundingBoxMin,
                                             o->Target->BoundingBoxMax, &o->Target->OBB, false);

                                if (o->SubType == 0)
                                {
                                    EmitMeshEffects(*b, 0, MODEL_SKIN_SHELL);
                                }
                                else if (o->SubType == 1)
                                {
                                    EmitMeshEffects(*b, 0, MODEL_SKIN_SHELL, 1);
                                }
                            }
                        }
                        else
                        {
                            if (o->SubType == 5)
                            {
                                Vector(1.f, 0.8f, 0.3f, Light);
                            }
                            else
                            {
                                Vector(0.3f, 0.8f, 1.f, Light);
                            }
                            CreateEffect(BITMAP_SHOCK_WAVE, o->Position, o->Angle, Light, 4);
                        }
                    }
                    if (o->SubType == 5)
                    {
                        o->PKKey = 1;
                        o->Velocity = -20.f;
                        VectorScale(o->Light, 1.f / 1.2f, o->Light);
                        prepareTravel();
                    }
                    else
                    {
                        o->m_bCreateTails = false;
                        const float grounded = (std::min)(remaining, o->MotionFrames);
                        VectorAddScaled(o->Position, o->MotionVelocity, o->Position, grounded);
                        o->Position[2] =
                            RequestTerrainHeight(o->Position[0], o->Position[1]) - o->MultiUse;
                        o->Angle[2] += o->MotionAngleRate[2] * grounded;
                        o->Velocity += 20.f * grounded;
                        o->MotionFrames -= grounded;
                        remaining -= grounded;
                        VectorScale(o->Light, powf(1.f / 1.2f, grounded), o->Light);
                    }
                }
            }
            if (o->SubType < 2)
            {
                CreateSprite(BITMAP_FLARE, o->Position, 1.f, o->Light, NULL,
                             (float)(WorldRandom() % 360));
            }
            else if (o->SubType == 5)
            {
                CreateSprite(BITMAP_FLARE, o->Position, 2.5f, o->Light, NULL,
                             (float)(WorldRandom() % 360));
            }
            else
            {
                Vector(0.f, 1.f, 1.f, Light);
                CreateSprite(BITMAP_SHINY + 2, o->Position, 1.6f, Light, NULL,
                             (float)(WorldRandom() % 360));
            }
        }
        else if (o->SubType == 4)
        {
            if (o->Target == NULL)
            {
                Joints.Retire(*o);
                break;
            }
            Joints.SetLive(iIndex, o->Target->Live);
            VectorCopy(o->Target->Position, o->Position);

            if (o->LifeTime < 15)
            {
                VectorScale(o->Light, powf(1.f / 1.3f, FPS_ANIMATION_FACTOR), o->Light);
            }
        }
        else if (o->SubType == 6)
        {
            AngleMatrix(o->Angle, Matrix);
            float remaining = FPS_ANIMATION_FACTOR;
            while (remaining > 0.f)
            {
                if (o->MotionFrames <= 0.f)
                {
                    o->MotionAngleRate[1] = static_cast<float>(WorldRandom() % 5 - 2);
                    o->MotionFrames = 1.f;
                    vec3_t velocity;
                    VectorCopy(o->Direction, velocity);
                    velocity[1] += o->MotionAngleRate[1];
                    velocity[2] -= 10.f;
                    VectorRotate(velocity, Matrix, o->MotionVelocity);
                }
                float step = (std::min)(remaining, o->MotionFrames);
                const auto contact = sessionKeeper_.WorldUnit()->FirstTerrainContact(
                    o->Position, o->MotionVelocity, 0.f, step);
                if (contact)
                    step = contact->frames;
                VectorAddScaled(o->Position, o->MotionVelocity, o->Position, step);
                o->Direction[1] += o->MotionAngleRate[1] * step;
                o->Direction[2] -= 10.f * step;
                o->MotionFrames -= step;
                remaining -= step;
                if (contact)
                {
                    o->Position[2] = contact->height + 5.f;
                    if (o->Direction[2] <= 0.f)
                        o->Direction[2] = std::sin(o->Velocity) * 20.f;
                    CreateJoint(BITMAP_JOINT_THUNDER, o->Position, o->Position, o->Angle, 4, NULL,
                                60.f);
                    vec3_t smoke;
                    VectorCopy(o->Position, smoke);
                    smoke[2] += 30.f;
                    CreateParticle(BITMAP_ADV_SMOKE + 1, smoke, o->Angle, o->Light, 2, 3.f);
                    vec3_t velocity;
                    VectorCopy(o->Direction, velocity);
                    velocity[1] += o->MotionAngleRate[1] * o->MotionFrames;
                    velocity[2] -= 10.f * o->MotionFrames;
                    VectorRotate(velocity, Matrix, o->MotionVelocity);
                }
            }

            if ((o->Weapon - o->LifeTime) < 10)
            {
                CreateParticleFpsChecked(BITMAP_EXPLOTION + 1, o->StartPosition, o->Angle, o->Light,
                                         0, 3.f);
                o->Scale = 100.f;
            }
            else
            {
                o->Scale = 40.f;
            }

            GameLogic::Effects::CreateTimedTail(o, Matrix, FPS_ANIMATION_FACTOR, false, 1);

            if (o->LifeTime < (o->Weapon / 2))
            {
                VectorScale(o->Light, powf(1.f / 1.3f, FPS_ANIMATION_FACTOR), o->Light);
            }
        }
        break;
    case BITMAP_DRAIN_LIFE_GHOST: {
        switch (o->SubType)
        {
        case 0: {
            OBJECT *pSourceObj = o->Target;
            OBJECT *pTargetObj = pSourceObj->Owner;
            vec3_t vTargetPos;
            VectorCopy(pTargetObj->Position, vTargetPos);
            vTargetPos[2] = o->TargetPosition[2];

            AdvanceHomingJoint(*o, vTargetPos, FPS_ANIMATION_FACTOR, WorldTime);
            AngleMatrix(o->Angle, Matrix);

            if (o->LifeTime < 10)
            {
                VectorScale(o->Light, powf(1.f / 1.2f, FPS_ANIMATION_FACTOR), o->Light);
                VectorCopy(o->Light, Light);
            }

            VectorCopy(o->Light, Light);
            float Scale = 2.f * (o->LifeTime / 10) + 2.f;

            //CreateSprite(BITMAP_LIGHT,o->Position,Scale,Light,o->Target,(float)(WorldRandom()%360),0);
            //CreateSprite(BITMAP_SHINY+1,o->Position,Scale/2.f,Light,o->Target,(float)(WorldRandom()%360),0);
        }
        break;
        }
    }
    break;
    case BITMAP_PIN_LIGHT: {
        VectorScale(o->Light, powf(0.9f, FPS_ANIMATION_FACTOR), o->Light);
    }
    break;
    case BITMAP_FORCEPILLAR: {
        if (!o->Target->Live || (o->Target->Type != MODEL_DOWN_ATTACK_DUMMY_L &&
                                 o->Target->Type != MODEL_DOWN_ATTACK_DUMMY_R))
        {
            Joints.Retire(*o);
            return;
        }
        Models[o->Target->Type].Animation(BoneTransform, o->Target->AnimationFrame,
                                          o->Target->PriorAnimationFrame, o->Target->PriorAction,
                                          o->Target->Angle, o->Target->HeadAngle, false, false,
                                          nullptr, o->Target->CurrentAction);

        Models[o->Target->Type].TransformByObjectBone(o->Position, o->Target, 0);
    }
    break;
    case BITMAP_SWORDEFF: {
        if (!o->Target->Live)
        {
            Joints.Retire(*o);
            return;
        }
        Models[o->Target->Type].Animation(BoneTransform, o->Target->AnimationFrame,
                                          o->Target->PriorAnimationFrame, o->Target->PriorAction,
                                          o->Target->Angle, o->Target->HeadAngle, false, false,
                                          nullptr, o->Target->CurrentAction);

        Models[o->Target->Type].TransformByObjectBone(o->Position, o->Target, 5);
    }
    break;
    case BITMAP_GROUND_WIND: {
        if (!o->Target->Live || (o->Target->Type != MODEL_DRAGON_KICK_DUMMY))
        {
            Joints.Retire(*o);
            return;
        }
        float TargetAniFrame = o->Target->AnimationFrame + (o->SubType * o->Target->AlphaTarget);
        Models[o->Target->Type].AnimationAtFrame(
            BoneTransform, TargetAniFrame, o->Target->PriorAnimationFrame, o->Target->PriorAction,
            o->Target->Angle, o->Target->HeadAngle, false, false, nullptr,
            o->Target->CurrentAction);

        Models[o->Target->Type].TransformByObjectBone(o->Position, o->Target, 2);

        o->Position[2] += 20.f;
        VectorScale(o->Light, powf(0.7f, FPS_ANIMATION_FACTOR), o->Light);
    }
    break;
    case BITMAP_LAVA: {
        if (!o->Target->Live)
        {
            Joints.Retire(*o);
            return;
        }
        int TempFrame = 0;
        TempFrame = o->SubType;
        if (o->SubType >= 7)
            TempFrame -= 7;

        float TargetAniFrame =
            o->Target->AnimationFrame - (((o->Velocity * 10.0f) - TempFrame) * 0.1f);
        Models[o->Target->Type].AnimationAtFrame(
            BoneTransform, TargetAniFrame, o->Target->PriorAnimationFrame, o->Target->PriorAction,
            o->Target->Angle, o->Target->HeadAngle, false, false, nullptr,
            o->Target->CurrentAction);

        ObjectDrawInput draw(o->Target);
        draw.bones = BoneTransform;
        if (o->SubType >= 7)
        {
            Models[o->Target->Type].TransformByObjectBone(o->Position, draw, 28);
        }
        else
        {
            Models[o->Target->Type].TransformByObjectBone(o->Position, draw, 36);
        }
        VectorScale(o->Light, powf(0.8f, FPS_ANIMATION_FACTOR), o->Light);
    }
    break;
    }

    if (o->BirthTiming.serial != birthSerial)
        return;

    if (o->m_bCreateTails)
    {
        if (o->Type == BITMAP_JOINT_ENERGY && o->SubType == 54)
            GameLogic::Effects::CreateTimedTail(o, Matrix, FPS_ANIMATION_FACTOR, true);
        else if (IsRebuiltLightning(*o))
        {
            GameLogic::Effects::CreateTail(o, Matrix);
            o->TailSampleFrames = 0.f;
            std::memcpy(o->LastTailSample, o->Tails[0], sizeof(o->LastTailSample));
        }
        else
            GameLogic::Effects::CreateTimedTail(o, Matrix, FPS_ANIMATION_FACTOR);
    }

    o->LifeTime -= FPS_ANIMATION_FACTOR;
    if (o->LifeTime < 0)
    {
        Joints.Retire(*o);
    }
}

void SessionGameplayUnit::MoveJoints()
{
    // Spirit controllers move models used by other joints. Resolve those
    // sources first, even when pool reuse placed a child in an earlier slot.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (auto cursor = Joints.begin(), end = Joints.end(); cursor != end; ++cursor)
        {
            JOINT *o = &*cursor;
            const bool drivesModel =
                (o->Type == BITMAP_JOINT_SPIRIT || o->Type == BITMAP_JOINT_SPIRIT2) &&
                (o->SubType == 15 || o->SubType == 18 || o->SubType == 24);
            if (drivesModel != (pass == 0))
                continue;
            MoveJoint(o, cursor.Index());
            if (!o->Live)
            {
                o->Target = nullptr;
                o->Tails.Clear();
            }
        }
    }
}

void SessionLegacyCalls::MoveJoint(JOINT *joint, int index)
{
    sessionKeeper_.Gameplay()->MoveJoint(joint, index);
}

void SessionLegacyCalls::MoveJoints()
{
    sessionKeeper_.Gameplay()->MoveJoints();
}

void SessionGameplayUnit::GetMagicScrew(int iParam, vec3_t vResult, float fSpeedRate)
{
    const double phase =
        iParam + Core::Time::ReferenceFrames(WorldTime,
                                             sessionKeeper_.ApplicationConfig().legacyReferenceFps);
    ScrewDirection(phase, vResult, fSpeedRate);
}

void SessionLegacyCalls::CreateJointFpsChecked(int type, vec3_t position, vec3_t targetPosition,
                                               vec3_t angle, int subType, OBJECT *target,
                                               float scale, short pkKey, WORD skillIndex,
                                               WORD skillSerialNumber, int characterIndex,
                                               const float *priorColor, short targetIndex)
{
    sessionKeeper_.Gameplay()->CreateJointFpsChecked(
        type, position, targetPosition, angle, subType, target, scale, pkKey, skillIndex,
        skillSerialNumber, characterIndex, priorColor, targetIndex);
}

void SessionLegacyCalls::CreateJoint(int type, vec3_t position, vec3_t targetPosition, vec3_t angle,
                                     int subType, OBJECT *target, float scale, short pkKey,
                                     WORD skillIndex, WORD skillSerialNumber, int characterIndex,
                                     const float *priorColor, short targetIndex)
{
    sessionKeeper_.Gameplay()->CreateJoint(type, position, targetPosition, angle, subType, target,
                                           scale, pkKey, skillIndex, skillSerialNumber,
                                           characterIndex, priorColor, targetIndex);
}

void SessionLegacyCalls::GetMagicScrew(int parameter, vec3_t result, float speedRate)
{
    sessionKeeper_.Gameplay()->GetMagicScrew(parameter, result, speedRate);
}

void SessionGameplayUnit::EmitMagicBoneCloud(OBJECT &effect)
{
    BMD &model = Models[effect.Owner->Type];
    AnimationPoseSample pose(effect.Owner, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    std::array<vec34_t, MAX_BONES> bones;
    const float active = std::min(FPS_ANIMATION_FACTOR, std::max(0.f, effect.LifeTime));
    for (auto birth : Emissions(active, active))
    {
        ObjectDrawInput draw(effect.Owner);
        VectorCopy(effect.StartPosition, draw.position);
        draw.angle[2] =
            effect.Owner->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), draw.angle[2]);
        draw.bones = pose.EvaluateAtTime(model, *effect.Owner, WorldTime, birth.FrameFraction(),
                                         bones.data());
        const float life = effect.LifeTime - (FPS_ANIMATION_FACTOR - birth.RemainingFrames());
        const int trials = static_cast<int>(std::ceil(life));
        vec3_t light{1.f, 1.f, 1.f};
        for (int trial = 0; trial < trials; ++trial)
        {
            const int bone = WorldRandom() % model.NumBones;
            if (model.Bones[bone].Dummy)
                continue;
            vec3_t offset{-50.f + WorldRandom() % 100, -10.f + WorldRandom() % 20,
                          -10.f + WorldRandom() % 20},
                position;
            model.TransformByObjectBone(position, draw, bone, offset);
            CreateParticle(BITMAP_LIGHT, position, draw.angle, light, 8);
        }
    }
}

namespace GameLogic::Effects
{
namespace
{
void WriteTail(JOINT::Tail &tail, const JOINT &joint, float matrix[3][4], BYTE axis)
{
    const float halfWidth = joint.Scale * 0.5f;
    const int secondAxis = axis == 0 ? 2 : 1;
    for (int edge = 0; edge < 4; ++edge)
    {
        vec3_t offset{}, rotated;
        offset[edge < 2 ? 0 : secondAxis] = edge % 2 == 0 ? -halfWidth : halfWidth;
        VectorRotate(offset, matrix, rotated);
        VectorAdd(joint.Position, rotated, tail[edge]);
    }
}
} // namespace

void CreateTailAxis(JOINT *joint, float matrix[3][4], BYTE axis)
{
    joint->NumTails = (std::min)(joint->NumTails + 1, joint->MaxTails - 1);
    joint->Tails.Advance();
    WriteTail(joint->Tails[0], *joint, matrix, axis);
}

void CreateTail(JOINT *joint, float matrix[3][4], bool blur)
{
    for (int sample = 0; sample < (blur ? 2 : 1); ++sample)
    {
        CreateTailAxis(joint, matrix, 0);
        if (blur && sample == 0 && joint->NumTails > 1)
            for (int edge = 0; edge < 4; ++edge)
                for (int coordinate = 0; coordinate < 3; ++coordinate)
                    joint->Tails[0][edge][coordinate] =
                        (joint->Tails[0][edge][coordinate] + joint->Tails[1][edge][coordinate]) /
                        2.f;
    }
}
void CreateTimedTail(JOINT *joint, float matrix[3][4], float referenceFrames, bool blur, BYTE axis)
{
    if (joint->MaxTails <= 0 || referenceFrames <= 0.f)
        return;
    JOINT::Tail previous, current;
    std::memcpy(previous, joint->Tails[0], sizeof(previous));
    WriteTail(current, *joint, matrix, axis);
    const double frames = referenceFrames * (blur ? 2.0 : 1.0);
    const double elapsed = joint->TailSampleFrames + frames;
    const double crossings = std::floor(elapsed + 0.0001);
    // A stall only needs the samples that can still be displayed.
    const int samples =
        static_cast<int>((std::min)(crossings, static_cast<double>(joint->MaxTails)));
    for (int index = 0; index < samples; ++index)
    {
        const float fraction = static_cast<float>(
            (crossings - samples + index + 1 - joint->TailSampleFrames) / frames);
        std::memcpy(joint->Tails[0],
                    index == 0 && joint->TailSampleFrames == 0.f ? previous : joint->LastTailSample,
                    sizeof(current));
        joint->Tails.Advance();
        for (int edge = 0; edge < 4; ++edge)
            for (int axis = 0; axis < 3; ++axis)
                joint->Tails[0][edge][axis] =
                    previous[edge][axis] + (current[edge][axis] - previous[edge][axis]) * fraction;
        std::memcpy(joint->LastTailSample, joint->Tails[0], sizeof(current));
    }
    joint->NumTails = (std::min)(joint->NumTails + samples, joint->MaxTails - 1);
    joint->TailSampleFrames = static_cast<float>((std::max)(0.0, elapsed - crossings));
    // Keep the leading edge continuous between authored history samples.
    std::memcpy(joint->Tails[0], current, sizeof(current));
}

} // namespace GameLogic::Effects
