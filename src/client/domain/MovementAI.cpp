// Scene transition helpers shared by multiple scenes.

#include "domain/MovementAI.h"
#include "support/CoreMath.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationAudio.h"
#include "session/SessionUi.h"
#include "session/SessionKeeper.h"
#include "session/SessionRender.h"
#include "session/SessionWorkspace.h"
#include "domain/Events.h"
#include "render/Text.h"
#include "session/SessionGameplay.h"
#include "support/Scenes.h"
#include "ui/runtime/UiControls.h"
#include "support/Camera.h"
#include "domain/ItemsSkills.h"
#include "ui/features/Hud/HudLogic.h"
#include "data/ItemData.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "render/Textures.h"
#include "domain/CharacterPresentation.h"
#include "domain/WorldSimulation.h"
#include "render/World.h"
#include "data/Localization.h"
#include "domain/CharacterSystem.h"
#include "data/GameData.h"
#include "ui/session/UiSessionLogic.h"
#include "app/AppWindow.h"
#include "I18N/All.h"
#include "domain/Shop.h"
#include "app/ApplicationLoopFrame.h"
#include "render/ModelResources.h"
#include "data/WorldData.h"
#include "render/Terrain.h"
#include "domain/EffectsUpdate.h"
#include "app/ApplicationNetwork.h"
#include "domain/ChatSocial.h"
#include "domain/Quests.h"
#include "ui/features/World/WorldLogic.h"
#include "domain/MapSimulation.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "domain/Automation.h"

bool CDirection::GetTimeCheck(int DelayTime)
{
    int PresentTime = timeGetTime();

    if (g_bTimeCheck)
    {
        g_iBackupTime = PresentTime;
        g_bTimeCheck = false;
    }

    if (g_iBackupTime + DelayTime <= PresentTime)
    {
        g_bTimeCheck = true;
        return true;
    }
    return false;
}

bool SessionLegacyCalls::GetTimeCheck(int DelayTime)
{
    return sessionKeeper_.DirectionObject().GetTimeCheck(DelayTime);
}

namespace
{

float NormalizeAngleDegrees(float angle)
{
    angle = std::fmod(angle, MovementDetail::FULL_ROTATION_DEGREES);
    if (angle < 0.f)
    {
        angle += MovementDetail::FULL_ROTATION_DEGREES;
    }

    return angle;
}

int NormalizeAngleInt(int angle)
{
    angle %= static_cast<int>(MovementDetail::FULL_ROTATION_DEGREES);
    if (angle < 0)
    {
        angle += static_cast<int>(MovementDetail::FULL_ROTATION_DEGREES);
    }
    return angle;
}

float SignedAngleDelta(float from, float to)
{
    float delta = NormalizeAngleDegrees(to) - NormalizeAngleDegrees(from);
    if (delta > MovementDetail::HALF_ROTATION_DEGREES)
    {
        delta -= MovementDetail::FULL_ROTATION_DEGREES;
    }
    else if (delta < -MovementDetail::HALF_ROTATION_DEGREES)
    {
        delta += MovementDetail::FULL_ROTATION_DEGREES;
    }
    return delta;
}

float StepTowardsAngle(float current, float target, float maxDelta)
{
    const float clampedDelta = std::clamp(SignedAngleDelta(current, target), -maxDelta, maxDelta);
    return NormalizeAngleDegrees(current + clampedDelta);
}
} // namespace

float CreateAngle2D(const vec3_t from, const vec2_t to)
{
    return CreateAngle(from[0], from[1], to[0], to[1]);
}

float CreateAngle(float x1, float y1, float x2, float y2)
{
    const float dx = x2 - x1;
    const float dy = y2 - y1;

    if (std::fabs(dx) < std::numeric_limits<float>::epsilon() &&
        std::fabs(dy) < std::numeric_limits<float>::epsilon())
    {
        return 0.f;
    }

    const float angle = std::atan2(dx, -dy) * MovementDetail::RAD_TO_DEG;
    return NormalizeAngleDegrees(angle);
}

int TurnAngle(int iTheta, int iHeading, int maxTURN)
{
    if (maxTURN <= 0)
    {
        return NormalizeAngleInt(iTheta);
    }

    const float updated = StepTowardsAngle(static_cast<float>(iTheta), static_cast<float>(iHeading),
                                           static_cast<float>(maxTURN));
    return NormalizeAngleInt(static_cast<int>(std::lround(updated)));
}

float TurnAngle2(float angle, float a, float d)
{
    if (d <= 0.f)
    {
        return NormalizeAngleDegrees(angle);
    }

    return StepTowardsAngle(angle, a, d);
}

float FarAngle(float angle1, float angle2, bool absolute)
{
    const float delta = SignedAngleDelta(angle1, angle2);
    return absolute ? std::fabs(delta) : delta;
}

int CalcAngle(float PositionX, float PositionY, float TargetX, float TargetY)
{
    const float targetAngle = CreateAngle(PositionX, PositionY, TargetX, TargetY);
    return NormalizeAngleInt(static_cast<int>(std::lround(targetAngle)));
}

float MoveHumming(vec3_t Position, vec3_t Angle, vec3_t TargetPosition, float Turn)
{
    const float scaledTurn = Turn;
    float targetAngle = CreateAngle2D(Position, TargetPosition);
    Angle[2] = TurnAngle2(Angle[2], targetAngle, scaledTurn);
    vec3_t Range;
    VectorSubtract(Position, TargetPosition, Range);
    float distance = std::sqrt(Range[0] * Range[0] + Range[1] * Range[1]);
    targetAngle = 360.f - CreateAngle(Position[2], distance, TargetPosition[2], 0.f);
    Angle[0] = TurnAngle2(Angle[0], targetAngle, scaledTurn);
    return VectorLength(Range);
}

float SessionGameplayUnit::MoveHumming(vec3_t position, vec3_t angle, vec3_t target, float turn)
{
    return ::MoveHumming(position, angle, target, turn * FPS_ANIMATION_FACTOR);
}

void SessionGameplayUnit::MovePosition(vec3_t Position, vec3_t Angle, vec3_t Speed)
{
    float Matrix[3][4];
    AngleMatrix(Angle, Matrix);

    vec3_t Velocity;
    VectorRotate(Speed, Matrix, Velocity);
    VectorScale(Velocity, FPS_ANIMATION_FACTOR, Velocity);
    VectorAdd(Position, Velocity, Position);
}

std::uint8_t CalcTargetPos(float x, float y, int Tx, int Ty)
{
    const int PositionX = static_cast<int>(x / TERRAIN_SCALE);
    const int PositionY = static_cast<int>(y / TERRAIN_SCALE);
    const int TargetX = static_cast<int>(Tx / TERRAIN_SCALE);
    const int TargetY = static_cast<int>(Ty / TERRAIN_SCALE);

    const std::uint8_t dx = static_cast<std::uint8_t>(8 + TargetX - PositionX);
    const std::uint8_t dy = static_cast<std::uint8_t>(8 + TargetY - PositionY);

    return static_cast<std::uint8_t>((dx & 0x0F) | ((dy & 0x0F) << 4));
}

void Alpha(OBJECT *o, float animationFactor)
{
    if (o->AlphaEnable)
    {
        if (o->AlphaTarget > o->Alpha)
        {
            o->Alpha += 0.05f * animationFactor;
            if (o->Alpha > o->AlphaTarget)
                o->Alpha = o->AlphaTarget;
        }
        else if (o->AlphaTarget < o->Alpha)
        {
            o->Alpha -= 0.05f * animationFactor;
            if (o->Alpha < o->AlphaTarget)
                o->Alpha = o->AlphaTarget;
        }
    }
    else
        o->Alpha += (o->AlphaTarget - o->Alpha) * Core::Time::Blend(0.1f, animationFactor);
    if (o->BlendMeshLight > o->Alpha)
        o->BlendMeshLight = o->Alpha;
}

void MoveRotatingPosition(vec3_t position, vec3_t angle, const vec3_t localVelocity, int axis,
                          float turnRate, float frames)
{
    if (frames <= 0.f)
        return;
    float matrix[3][4];
    vec3_t velocity;
    AngleMatrix(angle, matrix);
    VectorRotate(localVelocity, matrix, velocity);
    if (turnRate == 0.f)
    {
        VectorAddScaled(position, velocity, position, frames);
        return;
    }
    // AngleMatrix is Rz * Ry * Rx. Each Euler angle rotates around this world-space axis.
    double rotationAxis[3]{0.0, 0.0, 1.0};
    if (axis == 0)
        for (int component = 0; component < 3; ++component)
            rotationAxis[component] = matrix[component][0];
    else if (axis == 1)
    {
        const double yaw = angle[2] * Q_PI / 180.0;
        rotationAxis[0] = -std::sin(yaw);
        rotationAxis[1] = std::cos(yaw);
        rotationAxis[2] = 0.0;
    }
    const double radians = turnRate * double(frames) * Q_PI / 180.0;
    const double along = frames * std::sin(radians) / radians;
    const double halfSine = std::sin(radians * 0.5);
    const double across = frames * (2.0 * halfSine * halfSine) / radians;
    const double projection = rotationAxis[0] * velocity[0] + rotationAxis[1] * velocity[1] +
                              rotationAxis[2] * velocity[2];
    const double cross[3]{rotationAxis[1] * velocity[2] - rotationAxis[2] * velocity[1],
                          rotationAxis[2] * velocity[0] - rotationAxis[0] * velocity[2],
                          rotationAxis[0] * velocity[1] - rotationAxis[1] * velocity[0]};
    for (int component = 0; component < 3; ++component)
    {
        const double parallel = rotationAxis[component] * projection;
        position[component] += float(parallel * frames + (velocity[component] - parallel) * along +
                                     cross[component] * across);
    }
    angle[axis] += turnRate * frames;
}

void MoveTurningObject(OBJECT &object, const vec3_t localVelocity, float previousYaw,
                       float turnRate, float frames, vec3_t finalVelocity)
{
    float matrix[3][4];
    AngleMatrix(object.Angle, object.Matrix);
    VectorRotate(localVelocity, object.Matrix, finalVelocity);
    const float difference = FarAngle(previousYaw, object.Angle[2], false);
    if (difference == 0.f || turnRate <= 0.f)
    {
        VectorAddScaled(object.Position, finalVelocity, object.Position, frames);
        return;
    }
    const double turning = (std::min)(double(frames), std::abs(double(difference)) / turnRate);
    const double radians = double(difference) * Q_PI / 180.0;
    const double straight = frames - turning;
    vec3_t angle{object.Angle[0], object.Angle[1], previousYaw}, initialVelocity;
    AngleMatrix(angle, matrix);
    VectorRotate(localVelocity, matrix, initialVelocity);
    const double along = turning * std::sin(radians) / radians;
    const double across = turning * (1.0 - std::cos(radians)) / radians;
    object.Position[0] += float(initialVelocity[0] * along - initialVelocity[1] * across +
                                finalVelocity[0] * straight);
    object.Position[1] += float(initialVelocity[0] * across + initialVelocity[1] * along +
                                finalVelocity[1] * straight);
    object.Position[2] += finalVelocity[2] * frames;
}

namespace
{
float FlockHeading(const OBJECT *o, int i, const OBJECT *Boids, int MAX)
{
    int NumBirds = 0;
    float TargetX = 0.f;
    float TargetY = 0.f;
    for (int j = 0; j < MAX; j++)
    {
        const OBJECT *t = &Boids[j];
        if (t->Live && j != i)
        {
            vec3_t Range;
            VectorSubtract(o->Position, t->Position, Range);
            const auto distance = VectorLength(Range);
            if (distance < 400.f)
            {
                float xdist = t->Direction[0] - t->Position[0];
                float ydist = t->Direction[1] - t->Position[1];
                if (distance < 80.f)
                {
                    xdist -= t->Direction[0] - o->Position[0];
                    ydist -= t->Direction[1] - o->Position[1];
                }
                else
                {
                    xdist += t->Direction[0] - o->Position[0];
                    ydist += t->Direction[1] - o->Position[1];
                }

                float pdist = std::sqrt(xdist * xdist + ydist * ydist);
                if (pdist == 0.f)
                    continue;
                TargetX += xdist / pdist;
                TargetY += ydist / pdist;
                NumBirds++;
            }
        }
    }
    if (NumBirds > 0)
    {
        TargetX = o->Position[0] + TargetX / NumBirds;
        TargetY = o->Position[1] + TargetY / NumBirds;

        return CreateAngle(o->Position[0], o->Position[1], TargetX, TargetY);
    }
    return o->Angle[2];
}
} // namespace

void SessionGameplayUnit::RefreshFlockSteering(OBJECT *objects, int count)
{
    for (int index = 0; index < count; ++index)
        if (objects[index].Live)
            objects[index].AmbientFlockHeading =
                FlockHeading(&objects[index], index, objects, count);
}

void SessionGameplayUnit::MoveBoid(OBJECT *o, int index, OBJECT *objects, int count, float frames,
                                   bool preparedSteering)
{
    const float heading =
        preparedSteering ? o->AmbientFlockHeading : FlockHeading(o, index, objects, count);
    o->Angle[2] = TurnAngle2(o->Angle[2], heading, o->Gravity * frames);
}

void SessionGameplayUnit::PushObject(vec3_t PushPosition, vec3_t Position, float Power,
                                     vec3_t Angle)
{
    Vector(0.f, 0.f, 0.f, Angle);
    Angle[2] = CreateAngle2D(PushPosition, Position) + 180.f;
    if (Angle[2] >= 360.f)
        Angle[2] -= 360.f;

    float Matrix[3][4];
    AngleMatrix(Angle, Matrix);
    vec3_t p1, p2;
    Vector(0.f, -Power, 0.f, p1);
    VectorRotate(p1, Matrix, p2);
    Position[2] = RequestTerrainHeight(Position[0], Position[1]);
}

void SessionGameplayUnit::SetAction_Fenrir_Skill(CHARACTER *c, OBJECT *o)
{
    if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
    {
        if (c->Weapon[0].Type != -1 && c->Weapon[1].Type != -1)
            SetAction(&c->Object, PLAYER_RAGE_FENRIR_TWO_SWORD);
        else if (c->Weapon[0].Type != -1 && c->Weapon[1].Type == -1)
            SetAction(&c->Object, PLAYER_RAGE_FENRIR_ONE_RIGHT);
        else if (c->Weapon[0].Type == -1 && c->Weapon[1].Type != -1)
            SetAction(&c->Object, PLAYER_RAGE_FENRIR_ONE_LEFT);
        else
            SetAction(&c->Object, PLAYER_RAGE_FENRIR);
    }
    else
    {
        if (c->Weapon[0].Type != -1 && c->Weapon[1].Type != -1)
            SetAction(o, PLAYER_FENRIR_SKILL_TWO_SWORD);
        else if (c->Weapon[0].Type != -1 && c->Weapon[1].Type == -1)
            SetAction(o, PLAYER_FENRIR_SKILL_ONE_RIGHT);
        else if (c->Weapon[0].Type == -1 && c->Weapon[1].Type != -1)
            SetAction(o, PLAYER_FENRIR_SKILL_ONE_LEFT);
        else
            SetAction(o, PLAYER_FENRIR_SKILL);
    }
}

void SessionGameplayUnit::SetAction_Fenrir_Damage(CHARACTER *c, OBJECT *o)
{
    if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
    {
        if (c->Weapon[0].Type != -1 && c->Weapon[1].Type != -1)
            SetAction(o, PLAYER_RAGE_FENRIR_DAMAGE_TWO_SWORD);
        else if (c->Weapon[0].Type != -1 && c->Weapon[1].Type == -1)
            SetAction(o, PLAYER_RAGE_FENRIR_DAMAGE_ONE_RIGHT);
        else if (c->Weapon[0].Type == -1 && c->Weapon[1].Type != -1)
            SetAction(o, PLAYER_RAGE_FENRIR_DAMAGE_ONE_LEFT);
        else
            SetAction(o, PLAYER_RAGE_FENRIR_DAMAGE);
    }
    else
    {
        if (c->Weapon[0].Type != -1 && c->Weapon[1].Type != -1)
            SetAction(o, PLAYER_FENRIR_DAMAGE_TWO_SWORD);
        else if (c->Weapon[0].Type != -1 && c->Weapon[1].Type == -1)
            SetAction(o, PLAYER_FENRIR_DAMAGE_ONE_RIGHT);
        else if (c->Weapon[0].Type != -1 && c->Weapon[1].Type != -1 &&
                 c->Weapon[1].Type == MODEL_BOLT)
            SetAction(o, PLAYER_FENRIR_DAMAGE_ONE_RIGHT);
        else if (c->Weapon[0].Type == -1 && c->Weapon[1].Type != -1)
            SetAction(o, PLAYER_FENRIR_DAMAGE_ONE_LEFT);
        else if (c->Weapon[0].Type != -1 && c->Weapon[1].Type != -1 &&
                 c->Weapon[0].Type == MODEL_ARROWS)
            SetAction(o, PLAYER_FENRIR_DAMAGE_ONE_LEFT);
        else // 맨손
            SetAction(o, PLAYER_FENRIR_DAMAGE);
    }
}

void SessionGameplayUnit::SetAction_Fenrir_Run(CHARACTER *c, OBJECT *o)
{
    if (c->Weapon[0].Type != -1 && c->Weapon[1].Type != -1 && c->Weapon[0].Type != MODEL_ARROWS &&
        c->Weapon[1].Type != MODEL_BOLT)
    {
        if (gCharacterManager.GetBaseClass(c->Class) == CLASS_ELF)
            SetAction(o, PLAYER_FENRIR_RUN_TWO_SWORD_ELF);
        else if (gCharacterManager.GetBaseClass(c->Class) == CLASS_DARK)
            SetAction(o, PLAYER_FENRIR_RUN_TWO_SWORD_MAGOM);
        else if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
            SetAction(o, PLAYER_RAGE_FENRIR_RUN_TWO_SWORD);
        else
            SetAction(o, PLAYER_FENRIR_RUN_TWO_SWORD);
    }
    else if (c->Weapon[0].Type != -1 && c->Weapon[1].Type == -1)
    {
        if (gCharacterManager.GetBaseClass(c->Class) == CLASS_ELF)
            SetAction(o, PLAYER_FENRIR_RUN_ONE_RIGHT_ELF);
        else if (gCharacterManager.GetBaseClass(c->Class) == CLASS_DARK)
            SetAction(o, PLAYER_FENRIR_RUN_ONE_RIGHT_MAGOM);
        else if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
            SetAction(o, PLAYER_RAGE_FENRIR_RUN_ONE_RIGHT);
        else
            SetAction(o, PLAYER_FENRIR_RUN_ONE_RIGHT);
    }
    else if (c->Weapon[0].Type != -1 && c->Weapon[1].Type != -1 && c->Weapon[1].Type == MODEL_BOLT)
        SetAction(o, PLAYER_FENRIR_RUN_ONE_RIGHT_ELF);
    else if (c->Weapon[0].Type == -1 && c->Weapon[1].Type != -1)
    {
        if (gCharacterManager.GetBaseClass(c->Class) == CLASS_ELF)
            SetAction(o, PLAYER_FENRIR_RUN_ONE_LEFT_ELF);
        else if (gCharacterManager.GetBaseClass(c->Class) == CLASS_DARK)
            SetAction(o, PLAYER_FENRIR_RUN_ONE_LEFT_MAGOM);
        else if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
            SetAction(o, PLAYER_RAGE_FENRIR_RUN_ONE_LEFT);
        else
            SetAction(o, PLAYER_FENRIR_RUN_ONE_LEFT);
    }
    else if (c->Weapon[0].Type != -1 && c->Weapon[1].Type != -1 &&
             c->Weapon[0].Type == MODEL_ARROWS)
        SetAction(o, PLAYER_FENRIR_RUN_ONE_LEFT_ELF);
    else
    {
        if (gCharacterManager.GetBaseClass(c->Class) == CLASS_ELF)
            SetAction(o, PLAYER_FENRIR_RUN_ELF);
        else if (gCharacterManager.GetBaseClass(c->Class) == CLASS_DARK)
            SetAction(o, PLAYER_FENRIR_RUN_MAGOM);
        else if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
            SetAction(o, PLAYER_RAGE_FENRIR_RUN);
        else
            SetAction(o, PLAYER_FENRIR_RUN);
    }
}

void SessionGameplayUnit::SetAction_Fenrir_Walk(CHARACTER *c, OBJECT *o)
{
    if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
    {
        if (c->Weapon[0].Type != -1 && c->Weapon[1].Type != -1)
            SetAction(o, PLAYER_RAGE_FENRIR_WALK_TWO_SWORD);
        else if (c->Weapon[0].Type != -1 && c->Weapon[1].Type == -1)
            SetAction(o, PLAYER_RAGE_FENRIR_WALK_ONE_RIGHT);
        else if (c->Weapon[0].Type == -1 && c->Weapon[1].Type != -1)
            SetAction(o, PLAYER_RAGE_FENRIR_WALK_ONE_LEFT);
        else
            SetAction(o, PLAYER_RAGE_FENRIR_WALK);
    }
    else
    {
        if (c->Weapon[0].Type != -1 && c->Weapon[1].Type != -1 &&
            c->Weapon[0].Type != MODEL_ARROWS && c->Weapon[1].Type != MODEL_BOLT)
            SetAction(o, PLAYER_FENRIR_WALK_TWO_SWORD);
        else if (c->Weapon[0].Type != -1 && c->Weapon[1].Type == -1)
            SetAction(o, PLAYER_FENRIR_WALK_ONE_RIGHT);
        else if (c->Weapon[0].Type != -1 && c->Weapon[1].Type != -1 &&
                 c->Weapon[1].Type == MODEL_BOLT)
            SetAction(o, PLAYER_FENRIR_WALK_ONE_RIGHT);
        else if (c->Weapon[0].Type == -1 && c->Weapon[1].Type != -1)
            SetAction(o, PLAYER_FENRIR_WALK_ONE_LEFT);
        else if (c->Weapon[0].Type != -1 && c->Weapon[1].Type != -1 &&
                 c->Weapon[0].Type == MODEL_ARROWS)
            SetAction(o, PLAYER_FENRIR_WALK_ONE_LEFT);
        else
            SetAction(o, PLAYER_FENRIR_WALK);
    }
}

bool IsAliceRideAction_UniDino(unsigned short byAction)
{
    if (byAction == PLAYER_SKILL_CHAIN_LIGHTNING_UNI ||
        byAction == PLAYER_SKILL_CHAIN_LIGHTNING_DINO ||
        byAction == PLAYER_SKILL_LIGHTNING_ORB_UNI || byAction == PLAYER_SKILL_LIGHTNING_ORB_DINO ||
        byAction == PLAYER_SKILL_DRAIN_LIFE_UNI || byAction == PLAYER_SKILL_DRAIN_LIFE_DINO)
    {
        return true;
    }

    return false;
}

bool IsAliceRideAction_Fenrir(unsigned short byAction)
{
    if (byAction == PLAYER_SKILL_CHAIN_LIGHTNING_FENRIR ||
        byAction == PLAYER_SKILL_LIGHTNING_ORB_FENRIR || byAction == PLAYER_SKILL_DRAIN_LIFE_FENRIR)
    {
        return true;
    }

    return false;
}

void SessionGameplayUnit::SetAction(OBJECT *o, int Action, bool bBlending)
{
    BMD *b = &Models[o->Type];
    if (Action >= b->NumActions)
        return;
    if (o->CurrentAction != Action)
    {
        o->EnableBoneMatrix = false;
        o->PriorAction = o->CurrentAction;
        o->PriorAnimationFrame = o->AnimationFrame;
        o->CurrentAction = Action;
        o->AnimationFrame = 0;
        if (bBlending == false)
        {
            o->PriorAnimationFrame = 0;
        }
    }
}

bool TestDistance(CHARACTER *c, vec3_t TargetPosition, float Range)
{
    vec3_t Range2;
    VectorSubtract(c->Object.Position, TargetPosition, Range2);
    float Distance = Range2[0] * Range2[0] + Range2[1] * Range2[1];
    float ZoneRange = Range;
    if (Distance <= ZoneRange * ZoneRange)
        return true;
    return false;
}

void LookAtTarget(OBJECT *o, const CHARACTER *targetCharacter)
{
    if (targetCharacter == nullptr)
    {
        return;
    }

    const auto targetObject = &targetCharacter->Object;
    const auto angle = CreateAngle2D(o->Position, targetObject->Position);
    const auto deltaAngle = FarAngle(o->Angle[2], angle);

    if (deltaAngle < 90.f)
    {
        o->HeadTargetAngle[0] = o->Angle[2] - angle;
        o->HeadTargetAngle[1] = (targetObject->Position[2] - (o->Position[2] + 50.f)) * 0.2f;
        if (o->HeadTargetAngle[0] < 0)
        {
            o->HeadTargetAngle[0] += 360.f;
        }

        if (o->HeadTargetAngle[1] < 0)
        {
            o->HeadTargetAngle[1] += 360.f;
        }
    }
    else
    {
        o->HeadTargetAngle[0] = 0.f;
        o->HeadTargetAngle[1] = 0.f;
    }
}

namespace
{
void PathTarget(const PATH_t &path, float &x, float &y)
{
    constexpr float Weights[4][4] = {{-0.0703125f, 0.8671875f, 0.2265625f, -0.0234375f},
                                     {-0.0625f, 0.5625f, 0.5625f, -0.0625f},
                                     {-0.0234375f, 0.2265625f, 0.8671875f, -0.0703125f},
                                     {0.f, 0.f, 1.f, 0.f}};
    x = y = 0.f;
    for (int sample = 0; sample < 4; ++sample)
    {
        const int node = std::clamp(int(path.CurrentPath) + sample - 1, 0, int(path.PathNum) - 1);
        const float weight = Weights[path.CurrentPathFloat][sample];
        x += (float(path.PathX[node]) + 0.5f) * TERRAIN_SCALE * weight;
        y += (float(path.PathY[node]) + 0.5f) * TERRAIN_SCALE * weight;
    }
}

bool ReachPathTarget(CHARACTER &character)
{
    auto &path = character.Path;
    if (++path.CurrentPathFloat < 4)
        return false;
    path.CurrentPathFloat = 0;
    ++path.CurrentPath;
    if (path.CurrentPath < path.PathNum - 1)
        return false;
    path.CurrentPath = path.PathNum - 1;
    character.PositionX = path.PathX[path.CurrentPath];
    character.PositionY = path.PathY[path.CurrentPath];
    return true;
}
} // namespace

bool MovePath(CHARACTER *c, float animationFactor, float &travel, bool Turn)
{
    return MovePath(c, animationFactor, travel, Turn, {});
}

bool MovePath(CHARACTER *c, float animationFactor, float &travel, bool Turn,
              const std::function<void(float)> &afterSegment)
{
    if (animationFactor <= 0.f || travel <= 0.f)
        return false;
    PATH_t &path = c->Path;
    std::lock_guard<SpinLock> guard(path.Lock);
    if (path.PathNum < 2 || path.CurrentPath >= path.PathNum - 1)
        return true;
    const float framesPerDistance = animationFactor / travel;
    auto &object = c->Object;
    while (travel > 0.f)
    {
        if (path.CurrentPathFloat == 0)
        {
            const int next = (std::min)(int(path.CurrentPath) + 1, int(path.PathNum) - 1);
            c->PositionX = path.PathX[next];
            c->PositionY = path.PathY[next];
        }
        float x, y;
        PathTarget(path, x, y);
        const float dx = x - object.Position[0], dy = y - object.Position[1];
        const float distance = std::hypot(dx, dy);
        const float used = (std::min)(travel, distance);
        if (distance > 0.f)
        {
            if (Turn)
            {
                const float angle = CreateAngle(object.Position[0], object.Position[1], x, y);
                const float difference = FarAngle(object.Angle[2], angle);
                object.MotionTrace.TurnYaw(used * framesPerDistance, object.Angle[2], angle,
                                           difference >= 45.f ? 1.f : 0.5f);
                object.Angle[2] =
                    difference >= 45.f
                        ? angle
                        : TurnAngle2(object.Angle[2], angle,
                                     difference *
                                         Core::Time::Blend(0.5f, used * framesPerDistance));
            }
            object.Position[0] += dx * (used / distance);
            object.Position[1] += dy * (used / distance);
            travel -= used;
        }
        constexpr float ArrivalTolerance = 0.01f; // World-coordinate float resolution.
        const bool arrived = distance - used <= ArrivalTolerance;
        if (arrived)
        {
            object.Position[0] = x;
            object.Position[1] = y;
        }
        if (afterSegment && used > 0.f)
            afterSegment(used * framesPerDistance);
        if (!arrived)
            return false;
        if (ReachPathTarget(*c))
            return true;
    }
    return false;
}

bool SessionGameplayUnit::PathFinding2(int sx, int sy, int tx, int ty, PATH_t *a, float fDistance,
                                       int iDefaultWall)
{
    std::lock_guard<SpinLock> guard(a->Lock);

    bool Success = false;
    bool Value = false;

    if (TheMapProcess().Crywolf1st().Get_State_Only_Elf() == true &&
        TheMapProcess().Crywolf1st().IsCyrWolf1st() == true)
    {
        if ((CharactersClient[TargetNpc].Object.Type >= MODEL_CRYWOLF_ALTAR1 &&
             CharactersClient[TargetNpc].Object.Type <= MODEL_CRYWOLF_ALTAR5))
        {
            Value = true;
        }
    }

    if (Value && fDistance == 0.f && sx != 0 && sy != 0 &&
        !sessionKeeper_.WorldUnit()->ClearPathDestination(tx + ty * TERRAIN_SIZE))
        return false;
    int Wall = iDefaultWall;

    bool PathFound = path.FindPath(sx, sy, tx, ty, true, Wall, g_ErrorReport, fDistance);
    if (!PathFound)
    {
        Wall = sessionKeeper_.WorldUnit()->RetryPathWall(sx, sy, tx, ty, Wall);

        PathFound = path.FindPath(sx, sy, tx, ty, false, Wall, g_ErrorReport, fDistance);
    }

    if (PathFound)
    {
        int PathNum = path.GetPath();
        if (PathNum > 1)
        {
            a->PathNum = PathNum;
            unsigned char *x = path.GetPathX();
            unsigned char *y = path.GetPathY();

            for (int i = 0; i < a->PathNum; i++)
            {
                a->PathX[i] = x[i];
                a->PathY[i] = y[i];
            }

            a->CurrentPath = 0;
            a->CurrentPathFloat = 0;

            Success = true;
        }
    }

    return Success;
}

/**
 * \brief A factor which should applied to all values which get an added offset, frame-by-frame.
 * E.g. you have an object which moves by 10 x positions at every frame on a 25fps basis,
 * you'll simply multiply this 10 positions with this factor. If you have a current
 * frame rate of 50 fps, this factor is 0.5f, so it moves just 5 positions in this frame.
 * Therefore, the speed of the game is maintained even when the FPS change dynamically.
 */
bool SessionGameplayUnit::rand_fps_check(int reference_frames)
{
    return Random.FpsCheck(reference_frames, static_cast<double>(FPS_ANIMATION_FACTOR));
}

void MoveDampedObject(OBJECT *object, float damping, float animationFactor)
{
    const float distance = damping * Core::Time::DampedDistance(damping, animationFactor);
    const float decay = std::pow(damping, animationFactor);
    VectorAddScaled(object->Position, object->Direction, object->Position, distance);
    VectorAddScaled(object->Angle, object->HeadAngle, object->Angle, distance);
    VectorScale(object->Direction, decay, object->Direction);
    VectorScale(object->HeadAngle, decay, object->HeadAngle);
}

void MoveFallingObject(OBJECT *o, float acceleration, float turnPerTick, float animationFactor)
{
    constexpr float TerminalSpeed = 150.f;
    const float acceleratingFrames =
        (std::min)(animationFactor, (std::max)(0.f, (TerminalSpeed - o->Gravity) / acceleration));
    o->Position[2] -=
        acceleratingFrames * (o->Gravity + acceleration * (acceleratingFrames + 1.f) * 0.5f) +
        (animationFactor - acceleratingFrames) * TerminalSpeed;
    o->Gravity = (std::min)(TerminalSpeed, o->Gravity + acceleration * animationFactor);
    o->Angle[0] = (std::max)(-90.f, o->Angle[0] - turnPerTick * animationFactor);
    o->m_bActionStart = true;
    o->Direction[1] += o->Direction[0] * animationFactor;
}

#ifdef _PVP_ADD_MOVE_SCROLL
extern CMurdererMove g_MurdererMove;
#endif // _PVP_ADD_MOVE_SCROLL

// Latched when a click opens an NPC conversation while the button is still held.
// The world click handler ignores the held button until it is physically released, so the
// same click can't fall through to ground movement and instantly close the NPC window.
// A fresh press (e.g. deliberately clicking the ground to walk away) still works normally.
// 4 Seconds
int SessionGameplayUnit::SearchArrow() const
{
    if (gCharacterManager.GetBaseClass(CharacterAttribute->Class) == CLASS_ELF)
    {
        int Arrow = 0;

        if (GetEquipedBowType() == BOWTYPE_BOW)
        {
            Arrow = ITEM_ARROWS;
        }
        else if (GetEquipedBowType() == BOWTYPE_CROSSBOW)
        {
            Arrow = ITEM_BOLT;
        }

        int iIndex =
            sessionKeeper_.GameData()->Inventory(InventoryRole::Player).FindItemReverseIndex(Arrow);
        return iIndex;
    }
    return -1;
}

int SessionGameplayUnit::SearchArrowCount() const
{
    int Count = 0;
    if (gCharacterManager.GetBaseClass(CharacterAttribute->Class) == CLASS_ELF)
    {
        int Arrow = 0;

        if (GetEquipedBowType() == BOWTYPE_BOW)
        {
            Arrow = ITEM_ARROWS;
        }
        else if (GetEquipedBowType() == BOWTYPE_CROSSBOW)
        {
            Arrow = ITEM_BOLT;
        }
        Count = sessionKeeper_.GameData()->Inventory(InventoryRole::Player).GetNumItemByType(Arrow);
    }
    return Count;
}

bool SessionGameplayUnit::CheckTile(CHARACTER *c, OBJECT *o, float Range)
{
    if (!c || !o)
        return false;

    float dx = o->Position[0] - (TargetX * TERRAIN_SCALE + TERRAIN_SCALE * 0.5f);
    float dy = o->Position[1] - (TargetY * TERRAIN_SCALE + TERRAIN_SCALE * 0.5f);

    // Compare squared distance with squared range to avoid sqrt calculation
    float squaredDistance = dx * dx + dy * dy;
    float squaredRange = (TERRAIN_SCALE * Range) * (TERRAIN_SCALE * Range);

    return squaredDistance <= squaredRange;
}

bool SessionGameplayUnit::CheckWall(int sx1, int sy1, int sx2, int sy2)
{
    int Index = TERRAIN_INDEX_REPEAT(sx1, sy1);

    int nx1, ny1, d1, d2, len1, len2;
    int px1 = sx2 - sx1;
    int py1 = sy2 - sy1;
    if (px1 < 0)
    {
        px1 = -px1;
        nx1 = -1;
    }
    else
        nx1 = 1;
    if (py1 < 0)
    {
        py1 = -py1;
        ny1 = -TERRAIN_SIZE;
    }
    else
        ny1 = TERRAIN_SIZE;
    if (px1 > py1)
    {
        len1 = px1;
        len2 = py1;
        d1 = ny1;
        d2 = nx1;
    }
    else
    {
        len1 = py1;
        len2 = px1;
        d1 = nx1;
        d2 = ny1;
    }

    int error = 0, count = 0;
    do
    {
        int _type = (SelectedCharacter >= 0 ? CharactersClient[SelectedCharacter].Object.Type : 0);
        if ((_type != MODEL_EVIL_GATE && _type != MODEL_LION_GATE && _type != MODEL_STAR_GATE &&
             _type != MODEL_RUSH_GATE) &&
            (TerrainWall[Index] >= TW_NOMOVE && (TerrainWall[Index] & TW_ACTION) != TW_ACTION &&
             (TerrainWall[Index] & TW_HEIGHT) != TW_HEIGHT &&
             (TerrainWall[Index] & TW_CAMERA_UP) != TW_CAMERA_UP))
        {
            return false;
        }
        error += len2;
        if (error > len1 / 2)
        {
            Index += d1;
            error -= len1;
        }
        Index += d2;
    } while (++count <= len1);
    return true;
}

int SessionGameplayUnit::getTargetCharacterKey(CHARACTER *c, int selected)
{
    if (sessionKeeper_.GameData()->GetPickedItem())
    {
        return -1;
    }

    if (c != Hero)
    {
        return -1;
    }

    if (!CharactersClient.IsValidIndex(selected))
    {
        return -1;
    }

    CHARACTER *sc = &CharactersClient[selected];

    if (gMapManager.InChaosCastle() == true)
    {
        return sc->Key;
    }

    if (EnableGuildWar && sc->PK >= PVP_MURDERER2 && sc->GuildMarkIndex != -1 &&
        wcscmp(GuildMark[Hero->GuildMarkIndex].GuildName,
               GuildMark[sc->GuildMarkIndex].GuildName) == 0)
    {
        return -1;
    }

    if (g_DuelMgr.IsDuelEnabled())
    {
        if (g_DuelMgr.IsDuelPlayer(sc, DUEL_ENEMY))
        {
            return sc->Key;
        }

        return -1;
    }

    if (sc->GuildRelationShip == GR_RIVAL || sc->GuildRelationShip == GR_RIVALUNION)
    {
        return sc->Key;
    }

    if (EnableGuildWar)
    {
        if (sc->GuildTeam == 2 && sc != Hero)
        {
            return sc->Key;
        }

        return -1;
    }

    if (IsStrifeMap(gMapManager.ContextMap()) && sc != Hero &&
        sc->m_byGensInfluence != Hero->m_byGensInfluence && !IsKeyDown(VK_MENU))
    {
        if (sc->GuildRelationShip == GR_NONE && !g_pPartyManager->IsPartyMember(SelectedCharacter))
        {
            return sc->Key;
        }

        if ((wcscmp(GuildMark[Hero->GuildMarkIndex].GuildName,
                    GuildMark[c->GuildMarkIndex].GuildName) == 0) ||
            g_pPartyManager->IsPartyMember(SelectedCharacter))
        {
            if (IsKeyDown(VK_CONTROL))
            {
                return sc->Key;
            }

            return -1;
        }
    }

    if ((sc->PK >= PVP_MURDERER2 && sc->Object.Kind == KIND_PLAYER) ||
        (IsKeyDown(VK_CONTROL) && sc != Hero))
    {
        return sc->Key;
    }

    if (gMapManager.IsCursedTemple())
    {
        if (cursedTemple_.IsPartyMember(selected))
        {
            return -1;
        }

        return sc->Key;
    }

    return sc->Key;
}
void SessionGameplayUnit::InitPath()
{
    path.SetMapDimensions(TERRAIN_SIZE, TERRAIN_SIZE, sessionKeeper_.WorldUnit()->MovementGrid());
}
